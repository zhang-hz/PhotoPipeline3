// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T1 — filesystem helpers implementation.
// M4-T5 — §3.4 落地：mirror_path() 由 render_output_path()（路径模板解析）取代。
//
// Contract: docs/m1-tasks.md §3.2 (PP-FROZEN) / §4.1 + docs/v0.3.0-design.md §3.4 / §4.2.
// Interpretations where the contract is silent (reported in the task reports):
//   * with_extension(p, "") keeps the current extension (mirrors the old mirror_path rule
//     "new_ext 为空 → 保留原扩展名"); a leading '.' in new_ext is tolerated.
//   * is_inside(child, parent) is a subtree test: true for descendants AND for child==parent.
//   * collect_inputs() drops exact duplicates, sorts the result, and treats an empty
//     extension whitelist as "accept every regular file".
//   * template syntax (M4-T5): empty segments collapse silently ("a//b" == "a/b"); a segment
//     whose expansion is empty collapses as a whole; '$' followed by a non-identifier is an
//     error; the template may not be absolute, may not contain '\\' or a drive prefix, and may
//     not contain a ".." component (platform-independent lexical rules — no
//     std::filesystem::path parsing, so Windows and POSIX verdicts are identical).

#include "core/fsops.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <set>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace pp {
namespace {

std::string lower_copy(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string strip_dot(std::string_view s) {
    if (!s.empty() && s.front() == '.') {
        s.remove_prefix(1);
    }
    return std::string(s);
}

// Lexical absolute path (no symlink resolution): keeps mirror-path components exactly as the
// user spelled them while still making prefix comparison meaningful.
std::filesystem::path lexical_abs(const std::filesystem::path &p) {
    std::error_code ec;
    std::filesystem::path a = std::filesystem::absolute(p, ec);
    if (ec) {
        return p.lexically_normal();
    }
    return a.lexically_normal();
}

// weakly_canonical as required by §3.2 (falls back to lexical form when the call errors).
std::filesystem::path weakly(const std::filesystem::path &p) {
    std::error_code ec;
    std::filesystem::path c = std::filesystem::weakly_canonical(p, ec);
    if (ec) {
        return lexical_abs(p);
    }
    return c;
}

// Component-wise prefix test; both arguments must already be normalized.
bool path_prefix(const std::filesystem::path &parent, const std::filesystem::path &child) {
    auto pit = parent.begin();
    auto cit = child.begin();
    for (; pit != parent.end(); ++pit, ++cit) {
        if (cit == child.end() || *pit != *cit) {
            return false;
        }
    }
    return true;
}

bool ext_matches(const std::filesystem::path &p, const std::set<std::string> &want) {
    if (want.empty()) {
        return true;
    }
    return want.count(lower_copy(strip_dot(p.extension().string()))) != 0;
}

// ---------------------------------------------------------------- template --
bool is_symbol_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_';
}

// "stem" / "stem.ext"（ext 为空 → 无点主名；容忍调用方带前导点）
std::string file_name_of(const PathCtx &ctx) {
    const std::string ext = strip_dot(ctx.ext);
    return ext.empty() ? ctx.stem : ctx.stem + "." + ext;
}

// 展开单段；未知符号 / 裸 '$' → false 且 err 非空。
bool expand_segment(std::string_view seg, const PathCtx &ctx, std::string &out, std::string &err) {
    out.clear();
    out.reserve(seg.size());
    for (std::size_t i = 0; i < seg.size();) {
        if (seg[i] != '$') {
            out.push_back(seg[i]);
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        while (j < seg.size() && is_symbol_char(seg[j])) {
            ++j;
        }
        const std::string_view name = seg.substr(i + 1, j - (i + 1));
        if (name.empty()) {
            err = "output template: '$' is not followed by a symbol name";
            return false;
        }
        if (name == "format") {
            out += ctx.format_dir;
        } else if (name == "dir") {
            out += ctx.rel_dir.generic_string(); // '/' 分隔，$dir 展开后仍是相对片段
        } else if (name == "file") {
            out += file_name_of(ctx);
        } else if (name == "name") {
            out += ctx.stem;
        } else if (name == "ext") {
            out += strip_dot(ctx.ext);
        } else {
            err = "output template: unknown symbol '$" + std::string(name) + "'";
            return false;
        }
        i = j;
    }
    return true;
}

// 段是否为平台无关的"非法成分"（渲染后防线：$dir 理论上是相对路径，仍逐段复核）
bool is_dotdot_segment(std::string_view seg) { return seg == ".."; }

} // namespace

std::filesystem::path relative_dir(const std::filesystem::path &src,
                                   const std::filesystem::path &base_dir) {
    const std::filesystem::path src_abs = lexical_abs(src);
    const std::filesystem::path base_abs = lexical_abs(base_dir);
    if (!base_dir.empty() && src_abs != base_abs && path_prefix(base_abs, src_abs)) {
        return src_abs.lexically_relative(base_abs).parent_path();
    }
    return {};
}

bool validate_output_template(std::string_view tmpl, std::string *err) {
    const auto fail = [err](const std::string &msg) {
        if (err != nullptr) {
            *err = "output template: " + msg;
        }
        return false;
    };
    if (err != nullptr) {
        err->clear();
    }
    if (tmpl.empty()) {
        return fail("template is empty");
    }
    if (tmpl.front() == '/' || tmpl.front() == '\\') {
        return fail("absolute paths are not allowed");
    }
    if (tmpl.find('\\') != std::string_view::npos) {
        return fail("'\\' separators are not allowed (use '/')");
    }
    if (tmpl.size() >= 2 && tmpl[1] == ':') {
        return fail("drive-qualified paths are not allowed");
    }
    std::size_t start = 0;
    std::size_t last_named_seg = std::string_view::npos; // 最后一个非空段的起点（R-083）
    while (start <= tmpl.size()) {
        std::size_t end = tmpl.find('/', start);
        if (end == std::string_view::npos) {
            end = tmpl.size();
        }
        const std::string_view seg = tmpl.substr(start, end - start);
        if (is_dotdot_segment(seg)) {
            return fail("'..' is not allowed");
        }
        if (!seg.empty()) {
            std::string expanded;
            std::string sym_err;
            if (!expand_segment(seg, PathCtx{}, expanded, sym_err)) {
                if (err != nullptr) {
                    *err = sym_err;
                }
                return false;
            }
            last_named_seg = start;
        }
        if (end == tmpl.size()) {
            break;
        }
        start = end + 1;
    }
    // R-083：末段必须产出文件名（$file/$name/$ext 至少其一）。纯字面段结尾（"jpeg/"、
    // "photos-$format"）会把目录名当文件名渲染（Rename 后更是无扩展名怪文件）→ 校验层拒绝。
    if (last_named_seg != std::string_view::npos) {
        std::size_t end = tmpl.find('/', last_named_seg);
        if (end == std::string_view::npos) {
            end = tmpl.size();
        }
        const std::string_view seg = tmpl.substr(last_named_seg, end - last_named_seg);
        const bool file_name_seg = seg.find("$file") != std::string_view::npos ||
                                   seg.find("$name") != std::string_view::npos ||
                                   seg.find("$ext") != std::string_view::npos;
        if (!file_name_seg) {
            return fail("last segment must produce a file name (use $file/$name/$ext)");
        }
    }
    return true;
}

std::filesystem::path render_output_path(std::string_view tmpl, const PathCtx &ctx,
                                         const std::filesystem::path &out_root) {
    std::string err;
    if (!validate_output_template(tmpl, &err)) {
        return {}; // 非法模板 → 空 path（调用方先 validate 阻断）
    }
    std::filesystem::path rel;
    std::size_t start = 0;
    while (start <= tmpl.size()) {
        std::size_t end = tmpl.find('/', start);
        if (end == std::string_view::npos) {
            end = tmpl.size();
        }
        const std::string_view seg = tmpl.substr(start, end - start);
        std::string expanded;
        if (!expand_segment(seg, ctx, expanded, err)) {
            return {};
        }
        if (!expanded.empty()) {
            std::filesystem::path piece(expanded);
            for (const std::filesystem::path &part : piece) {
                if (is_dotdot_segment(part.string())) {
                    return {}; // $dir 展开出的 '..' 同样拒绝
                }
            }
            rel /= piece;
        }
        if (end == tmpl.size()) {
            break;
        }
        start = end + 1;
    }
    if (rel.empty()) {
        return {}; // 全部段都坍缩（例如 "$dir" 且源相对目录为空）→ 无文件名可用
    }
    return out_root / rel;
}

OutputPlan resolve_conflict(const std::filesystem::path &desired, ConflictPolicy policy,
                            const std::vector<std::filesystem::path> &reserved,
                            const std::vector<std::filesystem::path> &soft_reserved,
                            std::string &err) {
    err.clear();
    OutputPlan plan;
    plan.out_path = desired;

    std::set<std::string> reserved_norm;
    std::set<std::string> soft_norm;
    // NOTE(perf): caching these normalized keys across calls would only matter for batches
    // beyond ~10k files; no bottleneck observed at current batch sizes.
    for (const std::filesystem::path &r : reserved) {
        reserved_norm.insert(weakly(r).string());
    }
    for (const std::filesystem::path &r : soft_reserved) {
        soft_norm.insert(weakly(r).string());
    }
    const auto exists_on_disk = [](const std::filesystem::path &p) {
        std::error_code ec;
        return std::filesystem::exists(p, ec);
    };
    // Rename/Overwrite 的避让面 = 磁盘 + 硬保留 + 软保留（既有语义逐字不变）。
    const auto taken_any = [&](const std::filesystem::path &p) {
        return exists_on_disk(p) || reserved_norm.count(weakly(p).string()) != 0 ||
               soft_norm.count(weakly(p).string()) != 0;
    };
    // Skip 的判据（R-005）：磁盘存在或命中硬保留才跳过；仅软保留而磁盘缺失 → 回退正常写入。
    const auto taken_skip = [&](const std::filesystem::path &p) {
        return exists_on_disk(p) || reserved_norm.count(weakly(p).string()) != 0;
    };

    const bool taken = (policy == ConflictPolicy::Skip) ? taken_skip(desired) : taken_any(desired);
    if (!taken) {
        return plan; // rename_index 0, skip false
    }

    switch (policy) {
    case ConflictPolicy::Skip:
        plan.skip = true;
        return plan;
    case ConflictPolicy::Overwrite:
        return plan; // caller overwrites desired in place
    case ConflictPolicy::Rename:
        break;
    }

    const std::string stem = desired.stem().string();
    const std::string ext = desired.extension().string();
    for (int i = 1; i <= 10000; ++i) {
        std::string name = stem; // += 逐段拼接（同串同值；避免循环内临时串连加）
        name += " (";
        name += std::to_string(i);
        name += ")";
        name += ext;
        const std::filesystem::path cand = desired.parent_path() / name;
        if (!taken_any(cand)) {
            plan.out_path = cand;
            plan.rename_index = i;
            return plan;
        }
    }
    err = "cannot find a free output name for: " + desired.string();
    return plan;
}

OutputPlan resolve_conflict(const std::filesystem::path &desired, ConflictPolicy policy,
                            const std::vector<std::filesystem::path> &reserved, std::string &err) {
    // 既有 4 参签名（冻结）转发：软保留空集 → 既有行为逐字不变。
    static const std::vector<std::filesystem::path> kNoSoft;
    return resolve_conflict(desired, policy, reserved, kNoSoft, err);
}

bool is_inside(const std::filesystem::path &child, const std::filesystem::path &parent) {
    if (child.empty() || parent.empty()) {
        return false;
    }
    const std::filesystem::path c = weakly(child);
    const std::filesystem::path p = weakly(parent);
    return path_prefix(p, c);
}

std::vector<std::filesystem::path> collect_inputs(
    const std::vector<std::filesystem::path> &roots, const std::vector<std::string> &exts,
    const std::vector<std::filesystem::path> &exclude_roots, std::vector<std::string> &errors) {
    std::set<std::string> want;
    for (const std::string &e : exts) {
        const std::string l = lower_copy(strip_dot(e));
        if (!l.empty()) {
            want.insert(l);
        }
    }

    std::vector<std::filesystem::path> found;
    std::set<std::string> seen;

    const auto push_file = [&found, &seen](const std::filesystem::path &p) {
        const std::string key = lexical_abs(p).string();
        if (seen.insert(key).second) {
            found.push_back(p);
        }
    };
    // R-031：输出根子树整棵排除（is_inside = 子树判定，含根自身）。
    const auto excluded = [&exclude_roots](const std::filesystem::path &p) {
        for (const std::filesystem::path &x : exclude_roots) {
            if (!x.empty() && is_inside(p, x)) {
                return true;
            }
        }
        return false;
    };

    for (const std::filesystem::path &root : roots) {
        if (excluded(root)) {
            continue;
        }
        std::error_code ec;
        const std::filesystem::file_status st = std::filesystem::status(root, ec);
        if (ec || !std::filesystem::exists(st)) {
            errors.push_back("input path not found: " + root.string());
            continue;
        }
        if (std::filesystem::is_regular_file(st)) {
            if (ext_matches(root, want)) {
                push_file(root);
            }
            continue;
        }
        if (!std::filesystem::is_directory(st)) {
            errors.push_back("not a file or directory: " + root.string());
            continue;
        }
        try {
            // NOTE(limit): optional follow-symlink mode + progress callback for huge trees is
            // out of scope — symlink-cycle risk outweighs the benefit.
            const auto opts = std::filesystem::directory_options::skip_permission_denied;
            std::filesystem::recursive_directory_iterator it(root, opts);
            const std::filesystem::recursive_directory_iterator end;
            for (; it != end; ++it) {
                std::error_code fec;
                const std::filesystem::directory_entry &entry = *it;
                if (entry.is_directory(fec) && !fec) {
                    if (excluded(entry.path())) {
                        it.disable_recursion_pending(); // 排除子树不下降
                    }
                    continue;
                }
                if (!entry.is_regular_file(fec) || fec) {
                    continue; // directories, symlinks to nowhere, unreadable entries
                }
                if (excluded(entry.path())) {
                    continue;
                }
                if (ext_matches(entry.path(), want)) {
                    push_file(entry.path());
                }
            }
        } catch (const std::filesystem::filesystem_error &e) {
            errors.push_back("cannot read directory: " + root.string() + ": " + e.what());
        }
    }

    std::sort(found.begin(), found.end(),
              [](const std::filesystem::path &a, const std::filesystem::path &b) {
                  return a.string() < b.string();
              });
    return found;
}

std::vector<std::filesystem::path> collect_inputs(const std::vector<std::filesystem::path> &roots,
                                                  const std::vector<std::string> &exts,
                                                  std::vector<std::string> &errors) {
    // 既有 3 参签名（冻结）转发：空排除集 → 既有行为逐字不变。
    static const std::vector<std::filesystem::path> kNoExclude;
    return collect_inputs(roots, exts, kNoExclude, errors);
}

const std::vector<std::string> &input_extensions() {
    // §3.2 comment says "10 种输入" but lists 13 extensions; all 13 are delivered (M1-T1 report).
    static const std::vector<std::string> exts = {
        "tif",  "tiff", "png",  "jpg", "jpeg", "jxl", "heic",
        "heif", "avif", "webp", "bmp", "gif",  "tga",
    };
    return exts;
}

std::filesystem::path with_extension(const std::filesystem::path &p, std::string_view new_ext) {
    const std::string ext = strip_dot(new_ext);
    if (ext.empty()) {
        return p; // keep the original extension (§3.2 mirror_path rule)
    }
    std::filesystem::path out = p;
    out.replace_extension("." + ext);
    return out;
}

// ===========================================================================
// M7-WP1 · I1 原子写四件套 + I2 输出名/输出根校验（R-003/R-007/R-028）
// Contract: build/m7-wp1/handoff.md「裁定登记」（加性 + 追认）
// ===========================================================================

namespace {

// 进程 id（tmp 名的 PID 段；R-028 要求 PID/随机后缀，双开不互踩）。
unsigned long current_pid() {
#ifdef _WIN32
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

// 进程内单调序 + 随机 64 位（跨进程碰撞概率可忽略；PID 段再兜一层）。
std::string unique_suffix() {
    static std::atomic<unsigned long long> counter{0};
    static const unsigned long long rnd = [] {
        std::random_device rd;
        const unsigned long long a = (static_cast<unsigned long long>(rd()) << 32) ^ rd();
        return a ^ static_cast<unsigned long long>(
                       std::chrono::steady_clock::now().time_since_epoch().count());
    }();
    const unsigned long long n = counter.fetch_add(1, std::memory_order_relaxed);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%lu.%llx%llx", current_pid(), rnd, n);
    return buf;
}

const char *const kReservedNames[] = {
    "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4", "com5", "com6", "com7",
    "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};

bool is_reserved_device_name(std::string_view comp) {
    // Windows 规则：主名（第一个 '.' 之前）命中保留设备名即保留（CON.txt 同样非法）。
    const std::size_t dot = comp.find('.');
    const std::string base = lower_copy(comp.substr(0, dot));
    for (const char *r : kReservedNames) {
        if (base == r) {
            return true;
        }
    }
    return false;
}

} // namespace

std::filesystem::path atomic_temp_path(const std::filesystem::path &target) {
    if (target.empty() || target.filename().empty()) {
        return {};
    }
    const std::string name = target.filename().string() + "." + unique_suffix() + ".tmp";
    return target.parent_path() / name;
}

bool commit_temp_file(const std::filesystem::path &tmp, const std::filesystem::path &target,
                      std::string *err) {
    const auto fail = [err](const std::string &msg) {
        if (err != nullptr) {
            *err = msg;
        }
        return false;
    };
    if (err != nullptr) {
        err->clear();
    }
    if (tmp.empty() || target.empty()) {
        return fail("atomic commit: empty path");
    }
    std::error_code ec;
    // R-007：fs 入口经 normalize_extended_path 归一（超长路径可 rename/exists）。
    if (!std::filesystem::exists(normalize_extended_path(tmp), ec) || ec) {
        return fail("atomic commit: temp file missing: " + tmp.string());
    }
    // 标准语义：rename 覆盖已存在目标（POSIX rename / MSVC std::filesystem::rename）。
    std::filesystem::rename(normalize_extended_path(tmp), normalize_extended_path(target), ec);
    if (ec) {
        return fail("atomic commit: cannot replace " + target.string() + ": " + ec.message());
    }
    return true;
}

void discard_temp_file(const std::filesystem::path &tmp) noexcept {
    if (tmp.empty()) {
        return;
    }
    std::error_code ec;
    // R-007：fs 入口归一（超长路径的清场同样尽力而为）。
    std::filesystem::remove(normalize_extended_path(tmp), ec); // 失败不抛不报（noexcept 契约）
}

bool write_atomic_file(const std::filesystem::path &target, std::string_view data,
                       std::string *err) {
    const auto fail = [err](const std::string &msg) {
        if (err != nullptr) {
            *err = msg;
        }
        return false;
    };
    if (err != nullptr) {
        err->clear();
    }
    if (target.empty() || target.filename().empty()) {
        return fail("atomic write: target has no file name");
    }
    const std::filesystem::path tmp = atomic_temp_path(target);
    if (tmp.empty()) {
        return fail("atomic write: cannot build temp name for " + target.string());
    }
    {
        // R-007：写入口经 normalize_extended_path 归一 `\\?\`（超长路径可落盘）。
        std::ofstream out(normalize_extended_path(tmp), std::ios::binary | std::ios::trunc);
        if (!out) {
            discard_temp_file(tmp);
            return fail("atomic write: cannot create " + tmp.string());
        }
        if (!data.empty()) {
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
        out.flush();
        if (!out) {
            out.close();
            discard_temp_file(tmp);
            return fail("atomic write: write failed on " + tmp.string());
        }
    }
    // 字节校验（R-003 口径）：落盘 size 必须精确等于 data.size()，否则视为写失败并清场。
    std::error_code ec;
    // R-007：fs 入口归一（超长路径的 size/status/permissions 同口径）。
    const std::uintmax_t written = std::filesystem::file_size(normalize_extended_path(tmp), ec);
    if (ec || written != static_cast<std::uintmax_t>(data.size())) {
        discard_temp_file(tmp);
        return fail("atomic write: byte count mismatch on " + tmp.string() + " (wrote " +
                    std::to_string(written) + ", expected " + std::to_string(data.size()) + ")");
    }
    // 继承既有目标的权限位（preset_io 的既有行为收口到这里：覆盖不改权限）。
    std::error_code st_ec;
    const std::filesystem::file_status st =
        std::filesystem::status(normalize_extended_path(target), st_ec);
    if (!st_ec && std::filesystem::exists(st)) {
        std::error_code pm_ec;
        std::filesystem::permissions(normalize_extended_path(tmp), st.permissions(), pm_ec);
    }
    if (!commit_temp_file(tmp, target, err)) {
        discard_temp_file(tmp);
        return false;
    }
    return true;
}

// R-007（M7-R2B）：Windows 长路径 `\\?\` 归一（契约与幂等语义见 fsops.h 加性块）。
std::filesystem::path normalize_extended_path(const std::filesystem::path &p) {
#ifdef _WIN32
    if (p.empty())
        return p;
    std::filesystem::path q = p;
    q.make_preferred(); // `\\?\` 命名空间不接受 '/' → 统一为 '\'
    const std::wstring w = q.wstring();
    if (w.rfind(L"\\\\?\\", 0) == 0 || w.rfind(L"\\\\.\\", 0) == 0)
        return q; // 已归一 / 设备命名空间：幂等直通
    if (!p.is_absolute())
        return q; // 相对路径不归一（无稳定绝对语义，调用方自担 CWD 依赖）
    if (w.rfind(L"\\\\", 0) == 0)
        return std::filesystem::path(L"\\\\?\\UNC\\" + w.substr(2)); // UNC → \\?\UNC\...
    return std::filesystem::path(L"\\\\?\\" + w);
#else
    return p; // POSIX 无 MAX_PATH：恒原样（同一语义面，测试按平台断言）
#endif
}

bool validate_output_filename(const std::filesystem::path &p, std::string *err) {
    const auto fail = [err](const std::string &msg) {
        if (err != nullptr) {
            *err = msg;
        }
        return false;
    };
    if (err != nullptr) {
        err->clear();
    }
    if (p.empty()) {
        return fail("output path is empty");
    }
    if (p.filename().empty()) {
        return fail("output path has no file name: " + p.string());
    }
    // 长度硬线（259 整串 / 255 单段）**M7-R2B 解除**（R-007 收口）：写入口统一经
    // normalize_extended_path() 归一 `\\?\` 后超长路径可真实落盘，长度不再预检拦截；
    // 保留名/非法字符/尾随点空格防线保留（见 fsops.h 本函数契约）。
    for (const std::filesystem::path &comp : p) {
        if (comp.empty() || comp == p.root_name() || comp == p.root_directory()) {
            continue; // 盘符/根分隔符不参与文件名规则
        }
        const std::string s = comp.string();
        for (const char c : s) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20) {
                return fail("output path component contains a control character: " + s);
            }
            if (c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*') {
                return fail(std::string("output path component contains an illegal character '") +
                            c + "': " + s);
            }
        }
        if (s.back() == '.' || s.back() == ' ') {
            return fail("output path component ends with a dot or space: " + s);
        }
        if (is_reserved_device_name(s)) {
            return fail("output path component is a reserved device name: " + s);
        }
    }
    return true;
}

bool probe_output_root(const std::filesystem::path &dir, std::string *err) {
    const auto fail = [err](const std::string &msg) {
        if (err != nullptr) {
            *err = msg;
        }
        return false;
    };
    if (err != nullptr) {
        err->clear();
    }
    if (dir.empty()) {
        return fail("output root is empty");
    }
    std::error_code ec;
    // R-007：fs 入口归一（超长输出根同样可建可探）。
    const std::filesystem::path dir_x = normalize_extended_path(dir);
    std::filesystem::create_directories(dir_x, ec);
    if (ec) {
        return fail("output root is not creatable: " + dir.string() + ": " + ec.message());
    }
    const std::filesystem::file_status st = std::filesystem::status(dir_x, ec);
    if (ec || !std::filesystem::is_directory(st)) {
        return fail("output root is not a directory: " + dir.string());
    }
    // 试写探针（唯一名，写后即删）：只读介质/配额/共享限制在这里就暴露。
    const std::filesystem::path probe = dir / ("pp-write-probe." + unique_suffix() + ".tmp");
    {
        // R-007：写入口经 normalize_extended_path 归一（超长输出根同样可探）。
        std::ofstream out(normalize_extended_path(probe), std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail("output root is not writable: " + dir.string());
        }
        out << "pp write probe\n";
        out.flush();
        if (!out) {
            out.close();
            discard_temp_file(probe);
            return fail("output root is not writable: " + dir.string());
        }
    }
    discard_temp_file(probe);
    return true;
}

} // namespace pp
