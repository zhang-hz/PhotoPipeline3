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
#include <thread>
#include <utility>

#ifdef _WIN32
// clang-format off
// 本块禁止 clang-format 排序：sddl.h 依赖 windows.h 已定义的类型（WINAPI/LPCWSTR…），
// 字母序会把 sddl.h 排到 windows.h 之前 → C2039（已实际踩中一次，全树编译阻断）。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sddl.h> // ConvertSidToStringSidW（SID 字符串化；advapi32）
#include <process.h>
// advapi32 导入库（OpenProcessToken/GetTokenInformation/ConvertSidToStringSidW）：随 .obj 的
// .drectve 段传播到最终链接，pp_core 的消费者（pp_test_fsops / photopipeline）无需改
// target_link_libraries（先例：src/core/secret.cpp 的 crypt32 同法）。Linux 零影响。
#pragma comment(lib, "advapi32.lib")
// clang-format on
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
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
        // R-007 余量收口（M7-R2.1-F4）：exists 探测经 normalize_extended_path 归一——超长
        // 路径下未归一的 exists() 恒假（Skip 判据失真 = 幽灵跳过面）；常规路径归一前后探测
        // 结果逐值相同（`\\?\` 前缀下 Win32 exists 语义不变）。
        return std::filesystem::exists(normalize_extended_path(p), ec);
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

// 瞬态文件锁识别（M7R21F1-2，D-1 收口）：Windows 上 rename 替换可能撞共享违规类瞬态锁
// （杀软/索引器/并发同名输出短时持有目标；实测 `Access is denied` @ fsops commit rename，
// build/m7r2-g/handoff.md G4 / docs/m7-report.md §6.4-1）。std::filesystem 的 error_code
// 在 Windows 取自 GetLastError（std::system_category）：ERROR_ACCESS_DENIED(5) /
// ERROR_SHARING_VIOLATION(32) / ERROR_LOCK_VIOLATION(33) 三类视为可重试瞬态；
// 其余平台无此错误面，恒 false（POSIX rename 语义不受影响）。
bool is_transient_share_error(const std::error_code &ec) {
#ifdef _WIN32
    return ec.category() == std::system_category() &&
           (ec.value() == 5 || ec.value() == 32 || ec.value() == 33);
#else
    (void)ec;
    return false;
#endif
}

// rename 有界重试预算：最多 3 次重试（合计 4 次尝试），退避 25/50/75ms。有限次数 = 不改变
// 失败语义（超预算后仍走原有失败消息与返回值口径）。
constexpr int kRenameRetryMax = 3;

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
    // 瞬态锁有界重试（M7R21F1-2）：仅 Windows 共享违规类错误、至多 kRenameRetryMax 次；
    // 非瞬态错误与非 Windows 一次失败即出（行为语义与失败消息逐字不变）。
    for (int attempt = 0;; ++attempt) {
        std::filesystem::rename(normalize_extended_path(tmp), normalize_extended_path(target), ec);
        if (!ec) {
            return true;
        }
        if (attempt >= kRenameRetryMax || !is_transient_share_error(ec)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25 * (attempt + 1)));
    }
    return fail("atomic commit: cannot replace " + target.string() + ": " + ec.message());
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

// R-239（M7-R2.1-F4）：ConflictPolicy ↔ 文本映射单源（fsops.h 加性块口径；词表逐字不变）。
const char *conflict_policy_name(ConflictPolicy p) {
    switch (p) {
    case ConflictPolicy::Skip:
        return "skip";
    case ConflictPolicy::Overwrite:
        return "overwrite";
    case ConflictPolicy::Rename:
        return "rename";
    }
    return "rename";
}

bool parse_conflict_policy(std::string_view s, ConflictPolicy &out) {
    if (s == "skip") {
        out = ConflictPolicy::Skip;
        return true;
    }
    if (s == "overwrite") {
        out = ConflictPolicy::Overwrite;
        return true;
    }
    if (s == "rename") {
        out = ConflictPolicy::Rename;
        return true;
    }
    return false;
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

// ===========================================================================
// M8-W1 加性追加：输出根结构化探针 + 错误分类 + icacls 参数构造
//   契约见 fsops.h「M8-W1 加性追加」节；旧 probe_output_root 改为转发（签名/返回值契约不变）。
//   根因（0.3.5 残余，docs/0.3.5-report.md:123-124）：
//     ① 旧实现试写失败只报路径、**丢弃 OS 错误码** → 上层只能拿文本猜「是否权限问题」，
//        磁盘满/只读介质也会被误判成权限不足并弹 UAC；
//     ② 提权命令经 `cmd /c`（`%VAR%` 展开/引号截断风险），路径含 `%` 时直接放弃提权。
//   修法：探针返回**结构化状态 + OS 错误码**（错误码分类在 core，可单测）；icacls 参数
//   构造成纯函数（不经 cmd、SID 授权），UI 只留 ShellExecuteExW 外壳。
// ===========================================================================

namespace {

// OS 错误文本（Win32：系统本地化文本；POSIX：strerror）。0/未知 → 空串。
std::string os_error_text(unsigned long code) {
#ifdef _WIN32
    if (code == 0) {
        return {};
    }
    wchar_t *buf = nullptr;
    const DWORD n = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring text;
    if (n > 0 && buf != nullptr) {
        text.assign(buf, n);
        while (!text.empty() &&
               (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
            text.pop_back();
        }
    }
    if (buf != nullptr) {
        ::LocalFree(buf);
    }
    if (text.empty()) {
        return {};
    }
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                            nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(),
                          bytes, nullptr, nullptr);
    return out;
#else
    if (code == 0) {
        return {};
    }
    const char *s = std::strerror(static_cast<int>(code));
    return s != nullptr ? std::string(s) : std::string();
#endif
}

// 路径 → UTF-8（Windows：宽字符正确转 UTF-8，避免 ANSI 代码页在中文路径上乱码；
// POSIX：字节原样）。路径只用于**诊断文案**，不参与任何判定。
std::string path_to_utf8(const std::filesystem::path &p) {
    const std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char *>(u8.data()), u8.size());
}

// 错误分类：是否「ACL/权限类」写入拒绝 —— 只有这类失败才值得申请管理员授权。
//   磁盘满(ERROR_DISK_FULL 112 / ENOSPC 28)、只读介质(ERROR_WRITE_PROTECT 19 / EROFS 30)、
//   共享冲突(32)、路径不存在(3) 一律 **不是** ACL 类（旧文本匹配会把它们误判成权限）。
bool is_acl_denial_code(unsigned long code) {
#ifdef _WIN32
    return code == 5u /* ERROR_ACCESS_DENIED */ || code == 1314u /* ERROR_PRIVILEGE_NOT_HELD */;
#else
    return code == static_cast<unsigned long>(EACCES) || code == static_cast<unsigned long>(EPERM);
#endif
}

// 最近的已存在目录（诊断用：根不存在/不可建时告诉用户「断在哪一层」）。
std::filesystem::path nearest_existing_dir(const std::filesystem::path &dir) {
    if (dir.empty()) {
        return {};
    }
    std::error_code ec;
    std::filesystem::path p = dir;
    while (!p.empty()) {
        const std::filesystem::file_status st = std::filesystem::status(p, ec);
        if (!ec && std::filesystem::is_directory(st)) {
            return p;
        }
        const std::filesystem::path parent = p.parent_path();
        if (parent.empty() || parent == p) {
            break;
        }
        p = parent;
    }
    return {};
}

// 诊断文案：<head>: <path>: <OS 文本> (win32=N / errno=N)。前缀沿用既有 "output root is …"
// （src/core/error_text.h 的前缀族继续命中「输出根目录不可用」）。
std::string probe_detail(const char *head, const std::filesystem::path &dir,
                         unsigned long os_error) {
    std::string msg = std::string(head) + ": " + path_to_utf8(dir);
    if (os_error != 0) {
        const std::string text = os_error_text(os_error);
        if (!text.empty()) {
            msg += ": " + text;
        }
#ifdef _WIN32
        msg += " (win32=" + std::to_string(os_error) + ")";
#else
        msg += " (errno=" + std::to_string(os_error) + ")";
#endif
    }
    return msg;
}

// 试写探针（唯一名，写后即删）：返回 OS 错误码（0 = 成功）。用底层 open/write 而非
// ofstream，唯一目的是**拿到精确错误码**（ofstream 只给 failbit，拿不到 5/112/19）。
unsigned long write_probe_once(const std::filesystem::path &dir) {
    const std::filesystem::path probe = dir / ("pp-write-probe." + unique_suffix() + ".tmp");
    static constexpr char kPayload[] = "pp write probe\n";
    constexpr std::size_t kPayloadLen = sizeof(kPayload) - 1;
#ifdef _WIN32
    const std::wstring wprobe = normalize_extended_path(probe).wstring();
    HANDLE h = ::CreateFileW(wprobe.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                             nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return static_cast<unsigned long>(::GetLastError());
    }
    DWORD written = 0;
    const BOOL ok = ::WriteFile(h, kPayload, static_cast<DWORD>(kPayloadLen), &written, nullptr);
    unsigned long code = ok ? 0u : static_cast<unsigned long>(::GetLastError());
    if (ok && written != kPayloadLen) {
        code = ERROR_WRITE_FAULT; // 短写 = 落盘失败，不能当成功
    }
    ::CloseHandle(h);
    if (code == 0u && !::DeleteFileW(wprobe.c_str())) {
        code = static_cast<unsigned long>(::GetLastError());
    }
    return code;
#else
    const std::string nprobe = normalize_extended_path(probe).string();
    const int fd = ::open(nprobe.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        return static_cast<unsigned long>(errno);
    }
    const ssize_t written = ::write(fd, kPayload, kPayloadLen);
    unsigned long code =
        written == static_cast<ssize_t>(kPayloadLen) ? 0u : static_cast<unsigned long>(errno);
    ::close(fd);
    if (code == 0u && ::unlink(nprobe.c_str()) != 0) {
        code = static_cast<unsigned long>(errno);
    }
    return code;
#endif
}

// Windows 命令行引号（CommandLineToArgvW/CreateProcess 兼容）：整体加引号；结尾反斜杠
// 在引号前必须翻倍，避免把闭引号转义掉。
std::string quote_windows_argument(std::string_view arg) {
    std::string out;
    out.reserve(arg.size() + 8);
    out.push_back('"');
    std::size_t trailing_backslashes = 0;
    for (const char c : arg) {
        if (c == '\\') {
            ++trailing_backslashes;
        } else {
            trailing_backslashes = 0;
        }
        out.push_back(c);
    }
    out.append(trailing_backslashes, '\\');
    out.push_back('"');
    return out;
}

bool has_unsafe_windows_path_char(std::string_view s) {
    for (const char c : s) {
        if (c == '"' || static_cast<unsigned char>(c) < 0x20) {
            return true;
        }
    }
    return false;
}

#ifdef _WIN32
std::string wide_to_utf8(std::wstring_view w) {
    if (w.empty()) {
        return {};
    }
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                            nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), bytes,
                          nullptr, nullptr);
    return out;
}
#endif

} // namespace

const char *output_root_probe_status_name(OutputRootProbeStatus status) {
    switch (status) {
    case OutputRootProbeStatus::Ok:
        return "Ok";
    case OutputRootProbeStatus::EmptyPath:
        return "EmptyPath";
    case OutputRootProbeStatus::NotCreatable:
        return "NotCreatable";
    case OutputRootProbeStatus::NotDirectory:
        return "NotDirectory";
    case OutputRootProbeStatus::WriteDenied:
        return "WriteDenied";
    case OutputRootProbeStatus::WriteFailed:
        return "WriteFailed";
    }
    return "Unknown";
}

OutputRootProbe probe_output_root_ex(const std::filesystem::path &dir) {
    OutputRootProbe result;
    if (dir.empty()) {
        result.status = OutputRootProbeStatus::EmptyPath;
        result.detail = "output root is empty";
        return result;
    }
    std::error_code ec;
    // R-007：fs 入口归一（超长输出根同样可建可探）。
    const std::filesystem::path dir_x = normalize_extended_path(dir);
    // 先判「已存在但不是目录」（普通文件/设备）：create_directories 在这种路径上只会报
    // file exists，会被误诊成「不可创建」——分开成 NotDirectory 才能给对文案。
    {
        const std::filesystem::file_status pre = std::filesystem::status(dir_x, ec);
        if (!ec && std::filesystem::exists(pre) && !std::filesystem::is_directory(pre)) {
            result.status = OutputRootProbeStatus::NotDirectory;
            result.detail = "output root is not a directory: " + path_to_utf8(dir);
            return result;
        }
    }
    std::filesystem::create_directories(dir_x, ec);
    if (ec) {
        result.status = OutputRootProbeStatus::NotCreatable;
        result.os_error = static_cast<unsigned long>(ec.value());
        result.nearest_existing = nearest_existing_dir(dir);
        result.detail = probe_detail("output root is not creatable", dir, result.os_error);
        if (!result.nearest_existing.empty()) {
            result.detail +=
                "; nearest existing directory: " + path_to_utf8(result.nearest_existing);
        }
        return result;
    }
    const std::filesystem::file_status st = std::filesystem::status(dir_x, ec);
    if (ec) {
        result.status = OutputRootProbeStatus::NotDirectory;
        result.os_error = static_cast<unsigned long>(ec.value());
        result.detail = probe_detail("output root is not a directory", dir, result.os_error);
        return result;
    }
    if (!std::filesystem::is_directory(st)) {
        result.status = OutputRootProbeStatus::NotDirectory;
        result.detail = "output root is not a directory: " + path_to_utf8(dir);
        return result;
    }
    result.nearest_existing = dir;
    // 试写探针（唯一名，写后即删）：只读介质/配额/共享限制在这里就暴露。
    const unsigned long code = write_probe_once(dir);
    if (code != 0) {
        result.os_error = code;
        result.status = is_acl_denial_code(code) ? OutputRootProbeStatus::WriteDenied
                                                 : OutputRootProbeStatus::WriteFailed;
        result.detail = probe_detail("output root is not writable", dir, code);
        return result;
    }
    return result; // Ok
}

bool probe_output_root(const std::filesystem::path &dir, std::string *err) {
    const OutputRootProbe probe = probe_output_root_ex(dir);
    if (probe.status == OutputRootProbeStatus::Ok) {
        if (err != nullptr) {
            err->clear();
        }
        return true;
    }
    if (err != nullptr) {
        *err = probe.detail.empty() ? std::string("output root is not writable") : probe.detail;
    }
    return false;
}

bool should_offer_elevation(const OutputRootProbe &probe) {
#ifdef _WIN32
    return probe.status == OutputRootProbeStatus::WriteDenied;
#else
    (void)probe;
    return false; // POSIX 无 UAC：权限不足由 UI 直接给可操作提示（不弹提权）
#endif
}

bool is_valid_sid(std::string_view sid) {
    // 形如 "S-1-5-21-<sub>-<sub>-<sub>-<rid>"：S-<revision>-<authority>-<sub>...
    if (sid.size() < 7 || sid[0] != 'S' || sid[1] != '-') {
        return false;
    }
    std::size_t pos = 2;
    const auto parse_decimal = [&sid, &pos](unsigned long long max_value,
                                            unsigned long long &out) -> bool {
        const std::size_t start = pos;
        unsigned long long value = 0;
        while (pos < sid.size() && sid[pos] >= '0' && sid[pos] <= '9') {
            const unsigned long long digit = static_cast<unsigned long long>(sid[pos] - '0');
            if (digit > max_value || value > (max_value - digit) / 10ull) {
                return false; // 溢出 max_value
            }
            value = value * 10ull + digit;
            ++pos;
        }
        if (pos == start) {
            return false; // 空段
        }
        if (pos - start > 1 && sid[start] == '0') {
            return false; // 前导零（规范 SID 串无前导零）
        }
        out = value;
        return true;
    };
    unsigned long long revision = 0;
    if (!parse_decimal(1ull, revision) || revision != 1ull) {
        return false;
    }
    if (pos >= sid.size() || sid[pos] != '-') {
        return false;
    }
    ++pos;
    unsigned long long authority = 0;
    if (!parse_decimal((1ull << 48) - 1ull, authority)) {
        return false;
    }
    std::size_t subs = 0;
    while (pos < sid.size()) {
        if (sid[pos] != '-' || subs >= 15) {
            return false;
        }
        ++pos;
        unsigned long long sub = 0;
        if (!parse_decimal(0xFFFFFFFFull, sub)) {
            return false;
        }
        ++subs;
    }
    return subs >= 1;
}

std::string current_user_sid() {
#ifdef _WIN32
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE) {
        return {};
    }
    DWORD size = 0;
    ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::string out;
    if (size > 0) {
        std::vector<unsigned char> buffer(size, 0);
        if (::GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != FALSE) {
            const auto *user = reinterpret_cast<const TOKEN_USER *>(buffer.data());
            if (user->User.Sid != nullptr) {
                LPWSTR text = nullptr;
                if (::ConvertSidToStringSidW(user->User.Sid, &text) != FALSE && text != nullptr) {
                    out = wide_to_utf8(text);
                    ::LocalFree(text);
                }
            }
        }
    }
    ::CloseHandle(token);
    return out;
#else
    return {};
#endif
}

std::string icacls_grant_parameters(const std::filesystem::path &root, std::string_view sid) {
    if (!is_valid_sid(sid) || root.empty() || !root.is_absolute()) {
        return {};
    }
    // 只用于命令行 → 词法归一（去尾随分隔符；不解析符号链接，不做任何磁盘访问）。
    std::string path_text = path_to_utf8(root.lexically_normal());
    while (path_text.size() > 1 && (path_text.back() == '\\' || path_text.back() == '/') &&
           !(path_text.size() == 3 && path_text[1] == ':')) {
        path_text.pop_back(); // 保留盘符根 "C:\"，其余去尾随分隔符
    }
    if (path_text.empty() || has_unsafe_windows_path_char(path_text)) {
        return {};
    }
    std::string params = quote_windows_argument(path_text);
    params += " /grant *";
    params.append(sid);
    params += ":(OI)(CI)M"; // 仅「修改」权限、仅该目录子树（安全面：不请求更宽权限）
    return params;
}

} // namespace pp
