// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M10 / W3-T1：格式预设 core + JSON I/O 单测
//
// 设计契约：docs/v0.4.0-design.md §8（C23）/ §8.6 验收：
//   save→load→save 字节稳定（QJsonDocument::Indented 规范化）、版本/kind/format/未知键/
//   损坏 JSON/保留键往返/策略往返/applies_only_to_own_format/空 adaptive 不写字段。
// 另覆盖：normalize（补默认+锁+位深，幂等）、list（坏文件仍列出+标注）、delete（绝对路径守卫）、
// import/export、校验链的中文错误。
//
// M12（出厂预设外部化）：内置预设单一真源 = 仓库 `presets/formats/**` 的 JSON 数据文件（由 CMake
// 以 `PP_FACTORY_PRESETS_DIR` 注入绝对路径）。本测试直接校验工厂数据文件（8 格式 11 文件 /
// 每格式 default / 需求钉死字段）、工厂 seed（原字节复制、幂等、旧版 `_builtin-` 迁移、便携同目录
// 跳过复制、坏文件不写坏件）与 default 守卫（可保存覆盖 / 不可删除）。
//
// 注：新 `ui/format_preset_io.cpp` 与 pp_core 的既有适配层不同（未被 CMake 的 pp_core GLOB
// 收编，只在 UI 可执行目标里）；本测试按仓内先例（test_icons/test_meta_format）**直接包含
// 单实现 .cpp**，保证断言跑的就是生产代码（新增 UI .cpp 不另改 CMakeLists）。
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "core/adaptive.h"
#include "core/format_preset.h"
#include "core/params.h"
#include "env_compat.h"
#include "platform/paths.h" // M12：factory_format_presets_root() 的 exe 目录契约
// NOLINTNEXTLINE(bugprone-suspicious-include)：测试白盒包含（同既有模式）
#include "ui/format_preset_io.cpp" // 单实现（见文件头注）

// M12：工厂预设单一真源目录（仓库 presets/formats）由 CMake 单测循环注入；缺失即编译期失败
// （否则"工厂文件/seed 用例"会静默失效，属机械断言的假绿）。
#ifndef PP_FACTORY_PRESETS_DIR
#error "PP_FACTORY_PRESETS_DIR 未定义：CMakeLists.txt 单测 target 应注入仓库 presets/formats 路径"
#endif

namespace fs = std::filesystem;

namespace {

int g_fail = 0;

void fail(const std::string &c, const std::string &d) {
    std::printf("FAIL %s: %s\n", c.c_str(), d.c_str());
    ++g_fail;
}

void check(bool ok, const std::string &c, const std::string &d) {
    if (!ok)
        fail(c, d);
}

void check_contains(const std::string &haystack, const std::string &needle, const std::string &c,
                    const std::string &what) {
    check(haystack.find(needle) != std::string::npos, c,
          what + "：期望包含「" + needle + "」，实际「" + haystack + "」");
}

void case_begin(const char *name) {
    std::fprintf(stderr, "CASE %s\n", name);
    std::fflush(stderr);
}

fs::path fresh_dir(const fs::path &root, const std::string &name) {
    const fs::path dir = root / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

std::string read_text(const fs::path &file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool write_text(const fs::path &file, const std::string &text) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：参数语义由名字固定
QJsonObject parse_object_text(const std::string &text, const std::string &c) {
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray(text.data(), static_cast<qsizetype>(text.size())), &perr);
    check(perr.error == QJsonParseError::NoError && doc.isObject(), c,
          "JSON 解析失败：" + perr.errorString().toStdString());
    return doc.object();
}

// 完整 HEIF 快照（含保留键/自适应策略）：save→load→save 字节稳定的主用例。
pp::FormatPresetData make_heif_preset() {
    pp::FormatPresetData p;
    p.version = 1;
    p.name = "HEIF 高质量 444";
    p.format_id = "heif";
    p.backend_id = "x265";
    p.tech_id = "runtime";
    p.out_bitdepth = 10;
    p.params["quality"] = int64_t{90};
    p.params["chroma"] = std::string("444");
    p.params["heif_grid"] = int64_t{1}; // 自动（目录行是 int 枚举 0/1/2）
    p.params["heif_tile_w"] = int64_t{0};
    p.params["heif_tile_h"] = int64_t{0};
    p.params["x265:crf"] = 14.0;
    p.params["x265:level-idc"] = int64_t{0};
    p.params["tune"] = std::string("animation");
    p.heif_profile = int64_t{20}; // Main 4:4:4 10 Still Picture
    p.adaptive_enabled = true;
    const pp::AdaptiveDefaultPolicy *def = pp::default_adaptive_policy_for("heif");
    p.adaptive_policy = def != nullptr ? def->text : std::string();
    return p;
}

// 最小合法预设（jpeg，无保留键/无 adaptive）。
pp::FormatPresetData make_jpeg_preset() {
    pp::FormatPresetData p;
    p.name = "JPEG 最小";
    p.format_id = "jpeg";
    p.backend_id = "jpegli";
    p.tech_id = "dct";
    p.out_bitdepth = 8;
    p.params["quality"] = int64_t{90};
    return p;
}

// 参数快照等价：数值参数允许 int64 ⇄ double 互换（libheif 运行时内省键在 JSON 载入时没有
// 静态 ParamDef，整值 Float 会被还原成 Int；语义等价，且规范 JSON 字节稳定 —— 见
// docs/adaptive-policy.md §11）。其余类型逐字节相等。
bool params_equivalent(const pp::ParamSet &a, const pp::ParamSet &b) {
    if (a.size() != b.size())
        return false;
    const auto numeric = [](const pp::ParamValue &v, double &out) {
        if (const auto *i = std::get_if<int64_t>(&v)) {
            out = static_cast<double>(*i);
            return true;
        }
        if (const auto *d = std::get_if<double>(&v)) {
            out = *d;
            return true;
        }
        return false;
    };
    for (const auto &[key, va] : a) {
        const auto it = b.find(key);
        if (it == b.end())
            return false;
        const pp::ParamValue &vb = it->second;
        if (va == vb)
            continue;
        double x = 0.0, y = 0.0;
        if (numeric(va, x) && numeric(vb, y) && x == y)
            continue;
        return false;
    }
    return true;
}

// —— M12 工厂预设测试辅助 ——

// 8 个格式 id（与 core/format_tables 的 static_formats 一致；工厂目录结构 = 格式 id 子目录）。
std::vector<std::string> factory_formats() {
    return {"avif", "bmp", "heif", "jpeg", "jxl", "png", "tiff", "webp"};
}

// 目录下的普通 *.json 文件（非隐藏；按路径排序；与 list_format_presets_in 的过滤同口径）。
std::vector<fs::path> json_files_in(const fs::path &dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    const fs::directory_iterator end;
    for (fs::directory_iterator it(dir, ec); it != end; it.increment(ec)) {
        if (ec)
            break;
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.' || name.size() < 5)
            continue;
        std::string ext = name.substr(name.size() - 5);
        for (char &ch : ext)
            if (ch >= 'A' && ch <= 'Z')
                ch = static_cast<char>(ch - 'A' + 'a');
        if (ext != ".json")
            continue;
        std::error_code fec;
        if (!it->is_regular_file(fec))
            continue;
        out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// 递归复制目录树（seed 测试用：造临时工厂根/便携根）。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：src/dst 由复制语义固定
bool copy_tree(const fs::path &src, const fs::path &dst) {
    std::error_code ec;
    fs::create_directories(dst, ec);
    if (ec)
        return false;
    for (const fs::directory_entry &entry : fs::recursive_directory_iterator(src, ec)) {
        if (ec)
            return false;
        const fs::path rel = entry.path().lexically_relative(src);
        std::error_code op_ec;
        if (entry.is_directory()) {
            fs::create_directories(dst / rel, op_ec);
        } else if (entry.is_regular_file()) {
            fs::create_directories((dst / rel).parent_path(), op_ec);
            if (!op_ec)
                fs::copy_file(entry.path(), dst / rel, fs::copy_options::overwrite_existing, op_ec);
        }
        if (op_ec)
            return false;
    }
    return !ec;
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape) — 测试入口允许异常终止（非产品路径）
    const fs::path root = fs::current_path() / ".cache" / "tmp" /
                          ("m10_w3core_fmt_" + std::to_string(pptest::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    // 1) 保存 → 读取 → 再保存：字节稳定 + JSON 结构 + 路径规则
    case_begin("1) save→load→save 字节稳定 + JSON 结构");
    {
        const std::string c = "roundtrip";
        const fs::path root1 = fresh_dir(root, "rt");
        const pp::FormatPresetData p = make_heif_preset();
        fs::path saved;
        const std::string err = pp::ui::save_format_preset_to(root1, p, &saved);
        check(err.empty(), c, "save: " + err);
        check(!saved.empty() && saved.parent_path() == root1 / "heif", c,
              "saved path parent = " + saved.parent_path().string());
        check(saved.filename().string() == p.name + ".json", c, "saved filename");
        const std::string bytes1 = read_text(saved);
        check(!bytes1.empty(), c, "saved file empty");

        const QJsonObject o = parse_object_text(bytes1, c);
        check(o.value("kind").toString() == "format-preset", c, "kind");
        check(o.value("version").toInteger(-1) == 1, c, "version");
        check(o.value("name").toString().toStdString() == p.name, c, "name");
        check(o.value("format").toString() == "heif", c, "format");
        check(o.value("bitdepth").toInteger(-1) == 10, c, "bitdepth");
        const QJsonObject reserved = o.value("reserved").toObject();
        check(reserved.value("heif_profile").toInteger(-1) == 20, c, "reserved.heif_profile");
        const QJsonObject adaptive = o.value("adaptive").toObject();
        check(adaptive.value("enabled").toBool(false), c, "adaptive.enabled");
        // 需求 3：policy 落盘为 JSON 字符串数组（一行一个元素），零 \n 转义。
        const QJsonValue policy_v = adaptive.value("policy");
        check(policy_v.isArray(), c, "adaptive.policy 必须是数组（一行一个元素）");
        const QJsonArray policy_lines = policy_v.toArray();
        const std::vector<std::string> expect_lines =
            pp::adaptive_policy_to_lines(p.adaptive_policy);
        check(static_cast<std::size_t>(policy_lines.size()) == expect_lines.size(), c,
              "policy 行数 = " + std::to_string(policy_lines.size()) + "（期望 " +
                  std::to_string(expect_lines.size()) + "）");
        for (std::size_t i = 0;
             i < expect_lines.size() && i < static_cast<std::size_t>(policy_lines.size()); ++i) {
            check(policy_lines.at(static_cast<int>(i)).toString().toStdString() == expect_lines[i],
                  c, "policy[" + std::to_string(i) + "] 行内容");
        }
        check(bytes1.find("\\n") == std::string::npos, c,
              "落盘不得含 \\n 转义（policy 必须逐行可读）");
        const QJsonObject params = o.value("params").toObject();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            check(!it.key().startsWith("__"), c, "保留键不得落盘：" + it.key().toStdString());

        pp::FormatPresetData q;
        const std::string lerr = pp::ui::load_format_preset(saved, q);
        check(lerr.empty(), c, "load: " + lerr);
        check(q.name == p.name && q.format_id == "heif" && q.backend_id == "x265" &&
                  q.tech_id == "runtime" && q.out_bitdepth == 10,
              c, "loaded header fields");
        check(q.heif_profile.has_value() && *q.heif_profile == 20, c, "loaded heif_profile");
        check(q.adaptive_enabled && q.adaptive_policy == p.adaptive_policy, c, "loaded adaptive");
        check(q.params == p.params || (q.params.size() == p.params.size() &&
                                       q.params.at("quality") == p.params.at("quality")),
              c, "loaded params");

        const fs::path root2 = fresh_dir(root, "rt2");
        fs::path saved2;
        const std::string err2 = pp::ui::save_format_preset_to(root2, q, &saved2);
        check(err2.empty(), c, "resave: " + err2);
        check(read_text(saved2) == bytes1, c, "save→load→save 字节不稳定");
    }

    // 1b) M15：无损模式（HEIF RC 下拉第四档）经「参数集 → 格式预设 JSON → 回读」持久化。
    //     无损状态 = 显式 schema 参数 `lossless`（非保留键，随 params 落盘）；同时 chroma /
    //     x265:level-idc 等配置在无损下仍可保存回读（用户面要求 #2/#3）。
    case_begin("1b) 无损模式参数持久化（lossless/chroma/level 随格式预设往返）");
    {
        const std::string c = "lossless-persist";
        const fs::path dir = fresh_dir(root, "lossless");
        pp::FormatPresetData p = make_heif_preset();
        p.params["lossless"] = true;
        p.params["chroma"] = std::string("420");
        p.params["x265:level-idc"] = int64_t(31);
        fs::path saved;
        const std::string err = pp::ui::save_format_preset_to(dir, p, &saved);
        check(err.empty(), c, "save lossless preset: " + err);
        const std::string bytes = read_text(saved);
        check(bytes.find("\"lossless\": true") != std::string::npos, c,
              "无损状态必须以显式 lossless 参数落盘");
        pp::FormatPresetData q;
        const std::string lerr = pp::ui::load_format_preset(saved, q);
        check(lerr.empty(), c, "load lossless preset: " + lerr);
        const auto lit = q.params.find("lossless");
        check(lit != q.params.end() && std::holds_alternative<bool>(lit->second) &&
                  std::get<bool>(lit->second),
              c, "回读 lossless == true");
        const auto cit = q.params.find("chroma");
        check(cit != q.params.end() && std::holds_alternative<std::string>(cit->second) &&
                  std::get<std::string>(cit->second) == "420",
              c, "无损下 chroma 配置随预设往返（可配置，不被改写）");
        const auto lev = q.params.find("x265:level-idc");
        check(lev != q.params.end() && std::holds_alternative<int64_t>(lev->second) &&
                  std::get<int64_t>(lev->second) == 31,
              c, "无损下 x265:level-idc 配置随预设往返");
        const fs::path dir2 = fresh_dir(root, "lossless2");
        fs::path saved2;
        check(pp::ui::save_format_preset_to(dir2, q, &saved2).empty(), c, "resave lossless preset");
        check(read_text(saved2) == bytes, c, "无损预设 save→load→save 字节稳定");
        // 默认（有损）语义零变化：lossless=false 也是显式参数，回读仍为 false。
        pp::FormatPresetData d = make_heif_preset();
        d.name = "lossless-off";
        d.params["lossless"] = false;
        const fs::path dir3 = fresh_dir(root, "lossless-off");
        fs::path saved3;
        check(pp::ui::save_format_preset_to(dir3, d, &saved3).empty(), c, "save lossless=false");
        pp::FormatPresetData dq;
        check(pp::ui::load_format_preset(saved3, dq).empty(), c, "load lossless=false");
        const auto offit = dq.params.find("lossless");
        check(offit != dq.params.end() && std::holds_alternative<bool>(offit->second) &&
                  !std::get<bool>(offit->second),
              c, "默认有损语义零变化（lossless=false 往返）");
    }

    // 2) version/kind/format/未知键/损坏 JSON
    case_begin("2) 版本/kind/format/未知键/损坏 JSON");
    {
        const std::string c = "invalid-json";
        const fs::path dir = fresh_dir(root, "bad");
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：参数语义由名字固定
        auto expect_err = [&](const std::string &file, const std::string &text,
                              const std::string &needle, const std::string &what) {
            const fs::path f = dir / file;
            check(write_text(f, text), c, "write " + file);
            pp::FormatPresetData p;
            const std::string e = pp::ui::load_format_preset(f, p);
            check(!e.empty(), c, what + "：应当失败");
            check_contains(e, needle, c, what);
        };
        expect_err("v2.json", R"({"kind":"format-preset","version":2,"name":"x","format":"jpeg"})",
                   "不支持的格式预设版本", "version>1 显式报错");
        expect_err("nokind.json", R"({"version":1,"name":"x","format":"jpeg"})", "缺少 kind",
                   "kind 缺失拒绝");
        expect_err("global.json", R"({"version":2,"name":"x","outputs":[]})", "全局预设",
                   "缺 kind 且含 outputs → 提示全局预设");
        expect_err("wrongkind.json", R"({"kind":"global-preset","name":"x","format":"jpeg"})",
                   "不是格式预设", "kind 不符拒绝");
        expect_err("badformat.json",
                   R"({"kind":"format-preset","version":1,"name":"x","format":"gif"})", "未知格式",
                   "format 未知拒绝");
        expect_err("unknownparam.json",
                   R"({"kind":"format-preset","version":1,"name":"x","format":"jpeg",)"
                   R"("backend":"jpegli","tech":"dct","params":{"bogus_key":1}})",
                   "未知参数：bogus_key", "未知参数拒绝");
        expect_err("broken.json", R"({"kind":"format-preset",)", "JSON 解析失败", "损坏 JSON 报错");
        expect_err("nonobject.json", R"([1,2,3])", "根不是 JSON 对象", "根非对象拒绝");
        expect_err("namepath.json",
                   R"({"kind":"format-preset","version":1,"name":"a/b","format":"jpeg"})",
                   "非法文件名字符", "名字含 / 拒绝");
        // 未知根字段：忽略 + 记日志，不阻塞；再保存后不落盘（前向兼容）。
        const fs::path future = dir / "future.json";
        check(write_text(future, R"({"kind":"format-preset","version":1,"name":"未来字段",)"
                                 R"("format":"jpeg","backend":"jpegli","tech":"dct",)"
                                 R"("params":{"quality":90},"future_field":{"x":1}})"),
              c, "write future");
        pp::FormatPresetData fp;
        const std::string ferr = pp::ui::load_format_preset(future, fp);
        check(ferr.empty(), c, "未知根字段应被忽略：" + ferr);
        const fs::path root2 = fresh_dir(root, "future-out");
        fs::path rsaved;
        check(pp::ui::save_format_preset_to(root2, fp, &rsaved).empty(), c, "resave future");
        const std::string rb = read_text(rsaved);
        check(rb.find("future_field") == std::string::npos, c, "未知根字段不得写回");
    }

    // 3) 保留键往返：params 的 __ 前缀键不落盘，heif_profile 由 reserved 重建
    case_begin("3) 保留键往返（__ 前缀忽略 + reserved 重建）");
    {
        const std::string c = "reserved";
        pp::FormatPresetData p = make_heif_preset();
        p.params["__lossless"] = false;
        p.params["__heif_profile"] = int64_t{20};
        const fs::path dir = fresh_dir(root, "reserved");
        fs::path saved;
        const std::string err = pp::ui::save_format_preset_to(dir, p, &saved);
        check(err.empty(), c, "save: " + err);
        const QJsonObject o = parse_object_text(read_text(saved), c);
        const QJsonObject params = o.value("params").toObject();
        check(!params.contains("__lossless") && !params.contains("__heif_profile"), c,
              "params 不得写出 __ 前缀键");
        check(o.value("reserved").toObject().value("heif_profile").toInteger(-1) == 20, c,
              "reserved 重建 heif_profile");
        pp::FormatPresetData q;
        check(pp::ui::load_format_preset(saved, q).empty(), c, "load");
        check(q.params.find("__heif_profile") == q.params.end(), c, "load 不还原 __heif_profile");
        check(q.heif_profile.has_value() && *q.heif_profile == 20, c, "load heif_profile");
        // heif_profile 缺省 → 不写 reserved 字段。
        pp::FormatPresetData r = make_jpeg_preset();
        r.heif_profile.reset();
        fs::path saved2;
        check(pp::ui::save_format_preset_to(fresh_dir(root, "reserved2"), r, &saved2).empty(), c,
              "save jpeg");
        const QJsonObject o2 = parse_object_text(read_text(saved2), c);
        check(!o2.contains("reserved"), c, "无 heif_profile 时不写 reserved");
    }

    // 4) 空 adaptive 不写字段；策略文本与开关往返
    case_begin("4) 空 adaptive 不写字段 + 策略往返");
    {
        const std::string c = "adaptive";
        pp::FormatPresetData p = make_jpeg_preset();
        p.adaptive_enabled = false;
        p.adaptive_policy.clear();
        const fs::path dir = fresh_dir(root, "adaptive-empty");
        fs::path saved;
        check(pp::ui::save_format_preset_to(dir, p, &saved).empty(), c, "save");
        const QJsonObject o = parse_object_text(read_text(saved), c);
        check(!o.contains("adaptive"), c, "空 adaptive 不应写字段（旧文件零变化）");
        // 策略文本（含注释/换行）逐字节往返。
        pp::FormatPresetData q = make_heif_preset();
        const fs::path dir2 = fresh_dir(root, "adaptive-policy");
        fs::path saved2;
        check(pp::ui::save_format_preset_to(dir2, q, &saved2).empty(), c, "save policy");
        pp::FormatPresetData r;
        check(pp::ui::load_format_preset(saved2, r).empty(), c, "load policy");
        check(r.adaptive_enabled && r.adaptive_policy == q.adaptive_policy, c,
              "策略文本逐字节往返");
        // 非法策略：保存前被阻断（target 越界）。
        pp::FormatPresetData bad = make_jpeg_preset();
        bad.adaptive_enabled = true;
        bad.adaptive_policy = "target 200\nstage a\n  set chroma 444\n";
        fs::path ignored;
        const std::string e =
            pp::ui::save_format_preset_to(fresh_dir(root, "adaptive-bad"), bad, &ignored);
        check_contains(e, "自适应策略", c, "非法策略提示");
        check_contains(e, "target", c, "非法策略点名 target");
        check(!fs::exists(ignored), c, "校验失败不得写盘");
    }

    // 4b) 需求 3：adaptive.policy 数组持久化（写出数组 / 旧字符串兼容读 / 行号定位）
    case_begin("4b) adaptive.policy 数组持久化 + 旧字符串兼容读");
    {
        const std::string c = "policy-array";
        // 已知策略：含注释/空行/多 stage。
        pp::FormatPresetData p = make_jpeg_preset();
        p.adaptive_enabled = true;
        p.adaptive_policy =
            "# JPEG 策略\nversion 1\ntarget 90\nmax_rounds 6\noutput last\n\n"
            "stage quality-up\n  search quality from current up to 95 step 2 max_rounds 6\n";
        const fs::path dir = fresh_dir(root, "policy-array");
        fs::path saved;
        check(pp::ui::save_format_preset_to(dir, p, &saved).empty(), c, "save");
        const std::string bytes = read_text(saved);
        const QJsonObject o = parse_object_text(bytes, c);
        const QJsonArray lines = o.value("adaptive").toObject().value("policy").toArray();
        // 行数 = 8（含 1 个中间空行；末尾空行丢弃）。
        check(lines.size() == 8, c, "policy 数组行数 = " + std::to_string(lines.size()));
        check(lines.at(0).toString().toStdString() == "# JPEG 策略", c, "第 1 行");
        check(lines.at(5).toString().toStdString().empty(), c, "第 6 行为空行（保留）");
        check(bytes.find("\\n") == std::string::npos, c, "文件不得含 \\n 转义");
        // save→load→save 字节稳定 + 文本规范往返。
        pp::FormatPresetData q;
        check(pp::ui::load_format_preset(saved, q).empty(), c, "load");
        check(q.adaptive_policy == p.adaptive_policy, c,
              "加载文本应与保存文本逐字节一致（规范形：末尾 \\n）");
        const fs::path dir2 = fresh_dir(root, "policy-array2");
        fs::path saved2;
        check(pp::ui::save_format_preset_to(dir2, q, &saved2).empty(), c, "resave");
        check(read_text(saved2) == bytes, c, "save→load→save 字节稳定");

        // 旧字符串形态兼容读：\n 分隔 → 规范化后数组写出。
        const std::string legacy =
            R"({"kind":"format-preset","version":1,"name":"旧策略","format":"jpeg","backend":"jpegli","tech":"dct","bitdepth":8,"params":{"quality":90},"adaptive":{"enabled":true,"policy":"version 1\ntarget 88\n\nstage q\n  search quality from current up to 96 step 2\n"}})";
        const fs::path legacy_file = dir / "legacy.json";
        check(write_text(legacy_file, legacy), c, "write legacy");
        pp::FormatPresetData lp;
        const std::string lerr = pp::ui::load_format_preset(legacy_file, lp);
        check(lerr.empty(), c, "旧字符串策略应兼容读：" + lerr);
        check(lp.adaptive_enabled && lp.adaptive_policy ==
                                         "version 1\ntarget 88\n\nstage q\n"
                                         "  search quality from current up to 96 step 2\n",
              c, "旧字符串规范化文本（实际：" + lp.adaptive_policy + "）");
        // 旧字符串形态**再次保存**后升级为数组（文件内不再有 \n 转义）。
        const fs::path legacy_dir = fresh_dir(root, "policy-legacy");
        fs::path legacy_saved;
        check(pp::ui::save_format_preset_to(legacy_dir, lp, &legacy_saved).empty(), c,
              "legacy resave");
        check(read_text(legacy_saved).find("\\n") == std::string::npos, c,
              "旧字符串策略再保存应升级为数组");

        // 数组元素内嵌换行 ⇒ 显式报错（保住「下标+1 = 行号」定位口径）。
        const std::string embedded =
            R"({"kind":"format-preset","version":1,"name":"坏数组","format":"jpeg","params":{},"adaptive":{"enabled":true,"policy":["version 1\ntarget 90","stage q"]}})";
        const fs::path embedded_file = dir / "embedded.json";
        check(write_text(embedded_file, embedded), c, "write embedded");
        pp::FormatPresetData ep;
        const std::string eerr = pp::ui::load_format_preset(embedded_file, ep);
        check(!eerr.empty() && eerr.find("内嵌换行") != std::string::npos, c,
              "数组元素内嵌换行应报错：" + eerr);
        // 数组元素不是字符串 ⇒ 显式报错。
        const std::string nonstr =
            R"({"kind":"format-preset","version":1,"name":"坏元素","format":"jpeg","params":{},"adaptive":{"enabled":true,"policy":["version 1",5]}})";
        const fs::path nonstr_file = dir / "nonstr.json";
        check(write_text(nonstr_file, nonstr), c, "write nonstr");
        pp::FormatPresetData np;
        const std::string nerr = pp::ui::load_format_preset(nonstr_file, np);
        check(!nerr.empty() && nerr.find("不是字符串") != std::string::npos, c,
              "数组元素非字符串应报错：" + nerr);
        // policy 类型完全非法 ⇒ 显式报错。
        const std::string badtype =
            R"({"kind":"format-preset","version":1,"name":"坏类型","format":"jpeg","params":{},"adaptive":{"enabled":true,"policy":5}})";
        const fs::path badtype_file = dir / "badtype.json";
        check(write_text(badtype_file, badtype), c, "write badtype");
        pp::FormatPresetData bp;
        const std::string berr = pp::ui::load_format_preset(badtype_file, bp);
        check(!berr.empty() && berr.find("字符串或字符串数组") != std::string::npos, c,
              "policy 非法类型报错：" + berr);
        // 数组形态的错误行号 = 数组下标 + 1：target 非法写在第 2 行。
        const std::string line_err =
            R"({"kind":"format-preset","version":1,"name":"行号","format":"jpeg","params":{},"adaptive":{"enabled":true,"policy":["version 1","target 999","stage q","  search quality from current up to 90 step 2"]}})";
        const fs::path line_err_file = dir / "line.json";
        check(write_text(line_err_file, line_err), c, "write line");
        pp::FormatPresetData lp2;
        const std::string line_msg = pp::ui::load_format_preset(line_err_file, lp2);
        check(!line_msg.empty() && line_msg.find("第 2 行") != std::string::npos, c,
              "数组下标+1 = 行号（第 2 行）：" + line_msg);
    }

    // 5) applies_only_to_own_format + 校验链（位深/后端/技术/heif_profile）
    case_begin("5) 跨格式应用守卫 + 校验链");
    {
        const std::string c = "applies";
        const pp::FormatPresetData p = make_heif_preset();
        check(pp::check_format_preset_applies_to(p, "heif").empty(), c, "同格式应可应用");
        check_contains(pp::check_format_preset_applies_to(p, "avif"), "不能应用到格式", c,
                       "跨格式拒绝");
        check_contains(pp::check_format_preset_applies_to(p, ""), "目标格式为空", c, "空目标拒绝");
        check_contains(pp::check_format_preset_applies_to(p, "gif"), "不能应用到格式", c,
                       "未知/跨格式目标拒绝");
        // 显式后端/技术不存在
        pp::FormatPresetData bad = make_jpeg_preset();
        bad.backend_id = "nope";
        check(!pp::validate_format_preset(bad).empty(), c, "未知后端应失败");
        bad = make_jpeg_preset();
        bad.tech_id = "nope";
        check_contains(pp::validate_format_preset(bad), "未知技术", c, "未知技术应失败");
        // 位深
        bad = make_jpeg_preset();
        bad.out_bitdepth = 9;
        check_contains(pp::validate_format_preset(bad), "位深 9", c, "位深不合法");
        // heif_profile 作用域/枚举/信号
        bad = make_heif_preset();
        bad.heif_profile = int64_t{5}; // High（AV1）
        check_contains(pp::validate_format_preset(bad), "不能用于 HEIF", c, "AVIF 档位用于 HEIF");
        bad = make_heif_preset();
        bad.heif_profile = int64_t{99};
        check_contains(pp::validate_format_preset(bad), "越界", c, "档位越界");
        pp::FormatPresetData jxl = pp::FormatPresetData{};
        jxl.name = "JXL 带档位";
        jxl.format_id = "jxl";
        jxl.backend_id = "libjxl";
        jxl.tech_id = "vardct";
        jxl.out_bitdepth = 16;
        jxl.heif_profile = int64_t{0};
        check_contains(pp::validate_format_preset(jxl), "只适用于 HEIF/AVIF", c,
                       "jxl 带 heif_profile 拒绝");
        // 空后端/技术 = 合法（载入时解析首选）；heif 的 runtime 技术合法
        pp::FormatPresetData minimal = make_jpeg_preset();
        minimal.backend_id.clear();
        minimal.tech_id.clear();
        minimal.out_bitdepth = 0;
        check(pp::validate_format_preset(minimal).empty(), c,
              "空 backend/tech/bitdepth 应合法：" + pp::validate_format_preset(minimal));
        check_contains(pp::validate_format_preset(make_heif_preset()), "", c, "heif runtime 合法");
    }

    // 6) normalize：补默认 + 锁 + 位深；幂等
    case_begin("6) normalize 补默认/锁/位深 + 幂等");
    {
        const std::string c = "normalize";
        pp::FormatPresetData p;
        p.name = "HEIF 归一";
        p.format_id = "heif";
        std::string err;
        const std::vector<std::string> touched = pp::normalize_format_preset(p, &err);
        check(err.empty(), c, "normalize err: " + err);
        check(p.backend_id == "x265", c, "backend → 首选（实际 " + p.backend_id + "）");
        check(p.tech_id.empty(), c, "heif 运行时技术保持空");
        check(p.out_bitdepth == 8, c,
              "bitdepth → heif 默认 8（HEIF-03：8 位才是 Main Still "
              "Picture；实际 " +
                  std::to_string(p.out_bitdepth) + "）");
        check(std::find(touched.begin(), touched.end(), "backend") != touched.end(), c,
              "touched backend");
        check(std::find(touched.begin(), touched.end(), "bitdepth") != touched.end(), c,
              "touched bitdepth");
        std::string err2;
        const std::vector<std::string> touched2 = pp::normalize_format_preset(p, &err2);
        check(err2.empty() && touched2.empty(), c, "normalize 应幂等（第二次 touched 空）");

        // JXL：空参数 → 补默认值 + 选技术 + 默认位深 16
        pp::FormatPresetData j;
        j.name = "JXL 归一";
        j.format_id = "jxl";
        std::string jerr;
        const std::vector<std::string> jt = pp::normalize_format_preset(j, &jerr);
        check(jerr.empty(), c, "jxl normalize err: " + jerr);
        check(j.backend_id == "libjxl", c, "jxl backend");
        check(j.tech_id == "vardct", c, "jxl tech（lossless=false → vardct）");
        check(j.out_bitdepth == 16, c, "jxl bitdepth 16");
        check(!j.params.empty(), c, "jxl 默认参数应补齐");
        check(std::find(jt.begin(), jt.end(), "tech") != jt.end(), c, "touched tech");
        std::string jerr2;
        check(pp::normalize_format_preset(j, &jerr2).empty() && jerr2.empty(), c,
              "jxl normalize 幂等");

        pp::FormatPresetData unknown;
        unknown.name = "x";
        unknown.format_id = "gif";
        std::string uerr;
        pp::normalize_format_preset(unknown, &uerr);
        check(!uerr.empty(), c, "未知格式 normalize 应报错");
    }

    // 7) list：坏文件仍列出 + 标注；排序；非法格式 id 防护
    case_begin("7) list 坏文件标注/排序/路径防护");
    {
        const std::string c = "list";
        const fs::path dir = fresh_dir(root, "list");
        fs::path s1, s2;
        pp::FormatPresetData a = make_jpeg_preset();
        a.name = "A 预设";
        pp::FormatPresetData b = make_jpeg_preset();
        b.name = "B 预设";
        check(pp::ui::save_format_preset_to(dir / "formats", a, &s1).empty(), c, "save A");
        check(pp::ui::save_format_preset_to(dir / "formats", b, &s2).empty(), c, "save B");
        check(write_text(dir / "formats" / "jpeg" / "C 损坏.json", R"({"kind":"format-preset"})"),
              c, "write broken");
        const auto entries = pp::ui::list_format_presets_in(dir / "formats" / "jpeg");
        check(entries.size() == 3, c, "entries = " + std::to_string(entries.size()));
        if (entries.size() == 3) {
            check(entries[0].name == "A 预设" && !entries[0].broken, c, "entries[0]");
            check(entries[1].name == "B 预设" && !entries[1].broken, c, "entries[1]");
            check(entries[2].broken, c, "损坏文件应标 broken");
            check_contains(entries[2].error, "无效的格式预设", c, "损坏文件带原因");
            check(entries[2].name == "C 损坏", c, "损坏文件用 stem 作显示名");
        }
        check(pp::ui::list_format_presets_in(dir / "nope").empty(), c, "不存在目录 → 空表");
        check(pp::ui::list_format_presets("..").empty(), c, "非法 format id → 空表");
        check(pp::ui::format_preset_dir("..").empty(), c, "非法 format id → 空路径");
        check(pp::ui::format_preset_path("heif", "../x").empty(), c, "非法 name → 空路径");
    }

    // 8) delete：绝对路径守卫
    case_begin("8) delete 绝对路径守卫");
    {
        const std::string c = "delete";
        check_contains(pp::ui::delete_format_preset("relative.json"), "相对路径", c,
                       "相对路径拒绝");
        check_contains(pp::ui::delete_format_preset(""), "为空", c, "空路径拒绝");
        const fs::path dir = fresh_dir(root, "del");
        check_contains(pp::ui::delete_format_preset(dir / "missing.json"), "不存在", c,
                       "不存在拒绝");
        fs::path saved;
        check(pp::ui::save_format_preset_to(dir, make_jpeg_preset(), &saved).empty(), c, "save");
        check(pp::ui::delete_format_preset(saved).empty(), c, "绝对路径删除成功");
        check(!fs::exists(saved), c, "删除后文件应消失");
    }

    // 9) import / export（校验 + 规范化复制）
    case_begin("9) import/export 校验与规范化");
    {
        const std::string c = "import-export";
        const fs::path dir = fresh_dir(root, "io");
        const pp::FormatPresetData p = make_heif_preset();
        const fs::path exported = dir / "export.json";
        const std::string eerr = pp::ui::export_format_preset(p, exported);
        check(eerr.empty(), c, "export: " + eerr);
        check(fs::exists(exported), c, "导出文件存在");
        fs::path imported;
        const std::string ierr =
            pp::ui::import_format_preset_to(dir / "import-root", exported, "导入副本", &imported);
        check(ierr.empty(), c, "import: " + ierr);
        check(imported.parent_path() == dir / "import-root" / "heif", c, "import 目标目录");
        pp::FormatPresetData q;
        check(pp::ui::load_format_preset(imported, q).empty(), c, "imported load");
        check(q.name == "导入副本" && q.format_id == "heif", c, "imported name/format");
        // 改回原名后再次导出：规范化 JSON 应与原导出逐字节一致（证明导入=校验+规范化复制）。
        q.name = p.name;
        const fs::path exported2 = dir / "export2.json";
        check(pp::ui::export_format_preset(q, exported2).empty(), c, "export2");
        check(read_text(exported) == read_text(exported2), c, "规范化导出应字节一致");
        // 导入 name 空 = 保留文件内 name。
        fs::path imported2;
        check(
            pp::ui::import_format_preset_to(dir / "import-root2", exported, "", &imported2).empty(),
            c, "import keep name");
        pp::FormatPresetData q2;
        check(pp::ui::load_format_preset(imported2, q2).empty() && q2.name == p.name, c,
              "空 name 导入保留原名");
        // 导入非法文件 → 报错
        const fs::path bad = dir / "bad.json";
        check(write_text(bad, R"({"kind":"format-preset","version":1,"name":"x"})"), c,
              "write bad");
        fs::path ignored;
        check_contains(pp::ui::import_format_preset_to(dir / "ir2", bad, "", &ignored), "导入失败",
                       c, "非法文件导入拒绝");
        // 空根目录（data_dir 不可用）保存拒绝
        pp::FormatPresetData p2 = make_jpeg_preset();
        check_contains(pp::ui::save_format_preset_to("", p2, nullptr), "目录不可用", c,
                       "空数据目录拒绝");
    }

    // 10) 工厂预设库（M12：仓库 `presets/formats/**` = 出厂预设单一真源）：
    //     8 格式 11 文件全部 load+validate 通过、每格式恰一个 default、无 `_builtin-` 文件、
    //     无「内置示例 ·」名；人类可读/可改/可复制（4 空格缩进 + 策略行数组 + 运行时规范写盘形）。
    case_begin("10) 工厂预设库：8 格式 11 文件合法 + 每格式 default + 需求钉死");
    {
        const std::string c = "factory";
        const fs::path factory_root = fs::path(PP_FACTORY_PRESETS_DIR);
        check(fs::is_directory(factory_root), c,
              "工厂预设目录不存在（PP_FACTORY_PRESETS_DIR=" + factory_root.string() + "）");
        const std::vector<std::string> formats = factory_formats();
        std::map<std::string, int> count_by_format;
        std::map<std::string, pp::FormatPresetData> loaded; // key = "<fmt>/<stem>"
        const fs::path resave_root = fresh_dir(root, "factory-resave");
        int total_files = 0;
        for (const std::string &fmt : formats) {
            const fs::path dir = factory_root / fmt;
            check(fs::is_directory(dir), c, "工厂格式目录缺失：" + dir.string());
            const std::vector<fs::path> files = json_files_in(dir);
            count_by_format[fmt] = static_cast<int>(files.size());
            bool saw_default = false;
            for (const fs::path &file : files) {
                ++total_files;
                const std::string stem = file.stem().string();
                check(stem.rfind("_builtin-", 0) != 0, c,
                      "工厂库不得有 _builtin- 文件：" + file.string());
                pp::FormatPresetData p;
                const std::string lerr = pp::ui::load_format_preset(file, p);
                check(lerr.empty(), c, "工厂预设应可载入：" + file.string() + "：" + lerr);
                if (!lerr.empty())
                    continue;
                const std::string verr = pp::validate_format_preset(p);
                check(verr.empty(), c, "工厂预设应过校验链：" + file.string() + "：" + verr);
                check(p.format_id == fmt, c, "工厂预设 format 与目录不符：" + file.string());
                check(p.name.find("内置示例") == std::string::npos, c,
                      "工厂库不得再出现「内置示例 ·」名：" + p.name);
                check(p.name.rfind("_builtin-", 0) != 0, c,
                      "工厂预设 JSON name 不得带旧前缀：" + p.name);
                // 人类可读/可改/可复制是核心验收（简报 §1）：默认 4 空格缩进、键排序、策略逐行
                // 数组；且与运行时规范写盘**语义一致**（人写风格细节如空对象换行、`3.0`→`3`
                // 不苛求逐字节 —— 工厂文件是人类可编辑的数据，不是写盘产物）；规范写盘须幂等。
                const std::string bytes = read_text(file);
                check(bytes.find("\n    \"") != std::string::npos, c,
                      "工厂 JSON 应为 4 空格缩进：" + file.string());
                check(bytes.find("\\n") == std::string::npos, c,
                      "工厂 JSON 不得含 \\n 转义（策略必须逐行数组）：" + file.string());
                if (p.adaptive_enabled)
                    check(bytes.find("\"policy\": [") != std::string::npos, c,
                          "自适应策略必须是 JSON 字符串数组：" + file.string());
                fs::path resaved;
                const std::string serr = pp::ui::save_format_preset_to(resave_root, p, &resaved);
                check(serr.empty(), c, "工厂预设再保存失败：" + serr);
                if (serr.empty()) {
                    const std::string resaved_bytes = read_text(resaved);
                    const QJsonDocument factory_doc = QJsonDocument::fromJson(
                        QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())));
                    const QJsonDocument resaved_doc = QJsonDocument::fromJson(QByteArray(
                        resaved_bytes.data(), static_cast<qsizetype>(resaved_bytes.size())));
                    check(factory_doc == resaved_doc, c,
                          "工厂预设与运行时写盘语义不一致：" + file.string());
                    pp::FormatPresetData q;
                    check(pp::ui::load_format_preset(resaved, q).empty(), c,
                          "重载规范写盘：" + resaved.string());
                    fs::path resaved2;
                    check(pp::ui::save_format_preset_to(resave_root, q, &resaved2).empty() &&
                              read_text(resaved2) == resaved_bytes,
                          c,
                          std::string("运行时规范写盘应幂等（save→load→save 字节稳定）：") +
                              file.string());
                }
                std::string loaded_key = fmt;
                loaded_key += '/';
                loaded_key += stem;
                loaded[loaded_key] = p;
                saw_default = saw_default || (stem == "default");
            }
            check(saw_default, c, "每格式必须有 default.json：" + fmt);
        }
        check(total_files == 11, c,
              "工厂预设文件总数应 11（实际 " + std::to_string(total_files) + "）");
        check(count_by_format["heif"] == 2, c,
              "heif 应恰 2 个工厂文件（实际 " + std::to_string(count_by_format["heif"]) + "）");
        check(count_by_format["jxl"] == 3, c,
              "jxl 应恰 3 个工厂文件（实际 " + std::to_string(count_by_format["jxl"]) + "）");
        check(static_cast<int>(loaded.size()) == 11, c, "已载入工厂预设数应 11");
        for (const std::string &fmt : formats) {
            const auto it = loaded.find(fmt + "/default");
            check(it != loaded.end() && it->second.name == "default", c,
                  "每格式 default.json 的 JSON name 必须为 default：" + fmt);
        }

        // —— 需求钉死（简报 §4.3-2）——
        const auto find_loaded = [&loaded](const std::string &key) -> const pp::FormatPresetData * {
            const auto it = loaded.find(key);
            return it == loaded.end() ? nullptr : &it->second;
        };
        {
            // 需求 2/3：HEIF Apple自适应 = CRF 路径（x265:crf=20，不是 28）+ Main10 档位（2）。
            const pp::FormatPresetData *apple = find_loaded("heif/apple-adaptive");
            check(apple != nullptr, c, "缺少 heif/apple-adaptive.json");
            if (apple != nullptr) {
                check(apple->name == "Apple自适应", c,
                      "Apple自适应 显示名（实际「" + apple->name + "」）");
                check(apple->heif_profile.has_value() && *apple->heif_profile == 2, c,
                      "Apple自适应 reserved.heif_profile == 2（Main10）");
                const auto crf = apple->params.find("x265:crf");
                check(crf != apple->params.end() &&
                          params_equivalent(pp::ParamSet{{"x265:crf", crf->second}},
                                            pp::ParamSet{{"x265:crf", 20.0}}),
                      c, "Apple自适应 x265:crf == 20.0");
                check(apple->params.count("quality") == 0, c,
                      "Apple自适应 不得混入 quality（CRF 路径）");
            }
        }
        {
            // 需求 5：JPEG default = 视觉距离 3.0 + 自适应开启 + 断点步长策略（1–3 步长 1）。
            const pp::FormatPresetData *jpeg_def = find_loaded("jpeg/default");
            check(jpeg_def != nullptr, c, "缺少 jpeg/default.json");
            if (jpeg_def != nullptr) {
                const auto dist = jpeg_def->params.find("distance");
                check(dist != jpeg_def->params.end() &&
                          params_equivalent(pp::ParamSet{{"distance", dist->second}},
                                            pp::ParamSet{{"distance", 3.0}}),
                      c, "JPEG default distance == 3.0");
                const auto qm = jpeg_def->params.find("quality_mode");
                check(qm != jpeg_def->params.end() &&
                          std::holds_alternative<std::string>(qm->second) &&
                          std::get<std::string>(qm->second) == "distance",
                      c, "JPEG default quality_mode == distance");
                check(jpeg_def->adaptive_enabled, c, "JPEG default 自适应开启");
                check(jpeg_def->adaptive_policy.find("breakpoint 1 step 1") != std::string::npos, c,
                      "JPEG default 策略含 breakpoint 1 step 1（1–3 步长 1）");
                check(jpeg_def->adaptive_policy.find("step 0.1") != std::string::npos, c,
                      "JPEG default 策略含 0.1 精扫段（0.1–1 步长 0.1）");
                pp::AdaptivePolicy ast;
                pp::AdaptiveError aerr;
                check(pp::parse_and_validate_adaptive_policy(jpeg_def->adaptive_policy, "jpeg", ast,
                                                             &aerr),
                      c, "JPEG default 策略可解析：" + aerr.code + " " + aerr.message);
            }
        }
        {
            // 需求 2：JXL 无损归档 = lossless=true（无自适应）。
            const pp::FormatPresetData *lossless = find_loaded("jxl/lossless-archive");
            check(lossless != nullptr, c, "缺少 jxl/lossless-archive.json");
            if (lossless != nullptr) {
                check(lossless->name == "无损归档", c, "JXL 无损归档 显示名");
                const auto it = lossless->params.find("lossless");
                check(it != lossless->params.end() && std::holds_alternative<bool>(it->second) &&
                          std::get<bool>(it->second),
                      c, "JXL 无损归档 lossless == true");
                check(!lossless->adaptive_enabled && lossless->adaptive_policy.empty(), c,
                      "JXL 无损归档 无自适应");
            }
        }
        // 需求 2：HEIF/JXL 历史内建删除，只保留 Apple自适应/无损归档/自适应压缩/default。
        check(find_loaded("heif/default") != nullptr && find_loaded("jxl/default") != nullptr &&
                  find_loaded("jxl/adaptive-compress") != nullptr,
              c, "HEIF/JXL 工厂文件齐备（default/adaptive-compress）");
        {
            // M15：默认 HEIF 预设**语义零变化** —— lossless=false + chroma=420 + quality 路径
            // （无损第四档是用户可选项，不改变出厂默认）。
            const pp::FormatPresetData *heif_def = find_loaded("heif/default");
            check(heif_def != nullptr, c, "缺少 heif/default.json");
            if (heif_def != nullptr) {
                const auto l = heif_def->params.find("lossless");
                check(l != heif_def->params.end() && std::holds_alternative<bool>(l->second) &&
                          !std::get<bool>(l->second),
                      c, "HEIF default lossless == false（默认不发生语义变化）");
                const auto ch = heif_def->params.find("chroma");
                check(ch != heif_def->params.end() &&
                          std::holds_alternative<std::string>(ch->second) &&
                          std::get<std::string>(ch->second) == "420",
                      c, "HEIF default chroma == 420（无损不再改写色度）");
                check(heif_def->params.count("quality") != 0, c, "HEIF default 走质量路径");
                check(heif_def->params.count("x265:crf") == 0 &&
                          heif_def->params.count("x265:qp") == 0,
                      c, "HEIF default 不得混入 CRF/QP 路径键");
            }
        }
        // 默认识别单源（大小写不敏感；非 default 一律 false）。
        check(pp::is_default_format_preset_name("default") &&
                  pp::is_default_format_preset_name("Default") &&
                  pp::is_default_format_preset_name(std::string(pp::kFormatPresetDefaultName)) &&
                  !pp::is_default_format_preset_name("default2") &&
                  !pp::is_default_format_preset_name("") &&
                  !pp::is_default_format_preset_name("默认"),
              c, "is_default_format_preset_name 判定（大小写不敏感、非 default 拒绝）");
        // 工厂目录解析（纯函数契约，简报 §2.1）：exe 旁 presets/formats；非法格式 id → 空路径。
        const fs::path exe_dir = pp::platform::executable_dir();
        if (!exe_dir.empty()) {
            const fs::path expected_root = exe_dir / "presets" / "formats";
            check(pp::ui::factory_format_presets_root() == expected_root, c,
                  "factory_format_presets_root 应为 <exe 目录>/presets/formats（实际 " +
                      pp::ui::factory_format_presets_root().string() + "）");
            check(pp::ui::factory_format_preset_dir("jpeg") == expected_root / "jpeg", c,
                  "factory_format_preset_dir 拼接");
        }
        check(pp::ui::factory_format_preset_dir("..").empty(), c, "非法格式 id → 空路径");
        std::printf("FACTORY-PRESETS OK dir=%s files=%d (heif=2 jxl=3)\n",
                    factory_root.string().c_str(), total_files);
    }

    // 11) 工厂 seed（简报 §4.3-3）：原字节复制 / 幂等 / 旧版 `_builtin-` 迁移 / 便携同目录 /
    //     坏工厂文件不写坏件 / 空根中文报错。
    case_begin("11) 工厂 seed：字节复制/幂等/旧版迁移/便携同目录/坏文件");
    {
        const std::string c = "seed";
        const fs::path factory_root = fs::path(PP_FACTORY_PRESETS_DIR);
        const fs::path base = fresh_dir(root, "seed");

        // a) 首轮：数据目录预置 2 个旧版只读模板 → 迁移删除 + 11 个文件原字节复制。
        const fs::path data = base / "data";
        check(write_text(data / "jpeg" / "_builtin-jpeg.json", R"({"kind":"format-preset"})"), c,
              "写旧版 jpeg 模板");
        check(write_text(data / "heif" / "_builtin-heif-apple.json", "{}"), c, "写旧版 heif 模板");
        const pp::ui::FormatPresetSeedReport r1 =
            pp::ui::seed_factory_format_presets_to(factory_root, data);
        check(r1.written == 11 && r1.skipped == 0 && r1.failed == 0 && r1.removed_legacy == 2, c,
              "首轮 seed 读数（written=" + std::to_string(r1.written) +
                  " skipped=" + std::to_string(r1.skipped) +
                  " removed_legacy=" + std::to_string(r1.removed_legacy) +
                  " failed=" + std::to_string(r1.failed) + "）");
        check(!fs::exists(data / "jpeg" / "_builtin-jpeg.json"), c,
              "旧版 jpeg 只读模板应被迁移删除");
        check(!fs::exists(data / "heif" / "_builtin-heif-apple.json"), c,
              "旧版 heif 只读模板应被迁移删除");
        // 目标逐字节 == 工厂文件（seed 不得重新格式化）。
        for (const std::string &fmt : factory_formats()) {
            for (const fs::path &file : json_files_in(factory_root / fmt)) {
                const fs::path target = data / fmt / file.filename();
                check(fs::exists(target), c, "seed 目标缺失：" + target.string());
                if (fs::exists(target))
                    check(read_text(target) == read_text(file), c,
                          "seed 必须原字节复制（不得重新格式化）：" + target.string());
            }
        }

        // b) 次轮幂等：全 skipped，一个字节都不写。
        const pp::ui::FormatPresetSeedReport r2 =
            pp::ui::seed_factory_format_presets_to(factory_root, data);
        check(r2.written == 0 && r2.skipped == 11 && r2.failed == 0 && r2.removed_legacy == 0, c,
              "次轮 seed 应全跳过（written=" + std::to_string(r2.written) + " skipped=" +
                  std::to_string(r2.skipped) + " failed=" + std::to_string(r2.failed) + "）");

        // c) 用户改过的 default.json 永不被覆盖（一个字节都不动）。
        const fs::path user_default = data / "jpeg" / "default.json";
        check(write_text(user_default, "user-modified"), c, "改写 default.json");
        const pp::ui::FormatPresetSeedReport r3 =
            pp::ui::seed_factory_format_presets_to(factory_root, data);
        check(r3.written == 0 && r3.skipped == 11, c, "已有文件一律跳过");
        check(read_text(user_default) == "user-modified", c,
              "seed 不得覆盖用户改过的 default.json");

        // d) 工厂目录 == 数据目录（便携模式 data_dir == exe 目录）：只清理旧模板、不复制。
        const fs::path portable = base / "portable";
        check(copy_tree(factory_root, portable), c, "拷贝工厂树（便携根）");
        check(write_text(portable / "jpeg" / "_builtin-jpeg.json", "{}"), c, "写便携旧模板");
        const pp::ui::FormatPresetSeedReport r4 =
            pp::ui::seed_factory_format_presets_to(portable, portable);
        check(r4.written == 0 && r4.skipped == 0 && r4.failed == 0 && r4.removed_legacy == 1, c,
              "同目录 seed 应只清理（written=" + std::to_string(r4.written) +
                  " skipped=" + std::to_string(r4.skipped) +
                  " removed_legacy=" + std::to_string(r4.removed_legacy) + "）");
        check(!fs::exists(portable / "jpeg" / "_builtin-jpeg.json"), c, "同目录旧模板应被清除");
        check(fs::exists(portable / "jpeg" / "default.json"), c, "同目录 default 保留");

        // e) 坏工厂文件：failed=1 + 中文原因 + 不写坏文件；其余 11 个照常复制。
        const fs::path bad_factory = base / "bad-factory";
        check(copy_tree(factory_root, bad_factory), c, "拷贝工厂树（坏文件根）");
        check(write_text(bad_factory / "jpeg" / "broken.json",
                         R"({"kind":"format-preset","version":2,"name":"x","format":"jpeg"})"),
              c, "写坏工厂文件");
        const fs::path data2 = base / "data2";
        const pp::ui::FormatPresetSeedReport r5 =
            pp::ui::seed_factory_format_presets_to(bad_factory, data2);
        check(r5.written == 11 && r5.skipped == 0 && r5.failed == 1 && r5.errors.size() == 1, c,
              "坏工厂文件应 failed=1（written=" + std::to_string(r5.written) +
                  " failed=" + std::to_string(r5.failed) + "）");
        if (!r5.errors.empty())
            check_contains(r5.errors.front(), "校验失败", c, "坏工厂文件中文原因");
        check(!fs::exists(data2 / "jpeg" / "broken.json"), c, "坏工厂文件不得写出");

        // f) 数据根为空：显式 failed + 中文错误（不回落 CWD），不写文件。
        const pp::ui::FormatPresetSeedReport r6 =
            pp::ui::seed_factory_format_presets_to(factory_root, {});
        check(r6.failed > 0 && r6.written == 0 && !r6.errors.empty(), c,
              "空数据根应 failed>0 并带中文错误");
        if (!r6.errors.empty())
            check_contains(r6.errors.front(), "目录不可用", c, "空数据根中文原因");

        // g) 工厂目录为空（exe 目录不可读）：跳过复制（不失败），只做迁移清理。
        const fs::path data3 = base / "data3";
        check(write_text(data3 / "jpeg" / "_builtin-jpeg.json", "{}"), c,
              "写旧模板（空工厂根用例）");
        const pp::ui::FormatPresetSeedReport r7 = pp::ui::seed_factory_format_presets_to({}, data3);
        check(r7.written == 0 && r7.failed == 0 && r7.removed_legacy == 1, c,
              "空工厂目录应只清理不复制（failed=" + std::to_string(r7.failed) + "）");

        std::printf("SEED-REPORT first: written=%d skipped=%d removed_legacy=%d failed=%d\n",
                    r1.written, r1.skipped, r1.removed_legacy, r1.failed);
        std::printf("SEED-REPORT second: written=%d skipped=%d removed_legacy=%d failed=%d\n",
                    r2.written, r2.skipped, r2.removed_legacy, r2.failed);
    }

    // 12) default 预设守卫（简报 §2.5/§4.4）：可「保存」直接修改（覆盖 default.json）、
    //     不可删除（I/O 层中文拒绝）；普通预设可正常删除。
    case_begin("12) default 守卫：可保存覆盖 / 不可删除 / 普通预设可删");
    {
        const std::string c = "default-guard";
        const fs::path factory_root = fs::path(PP_FACTORY_PRESETS_DIR);
        const fs::path data = fresh_dir(root, "default-guard");
        const pp::ui::FormatPresetSeedReport r =
            pp::ui::seed_factory_format_presets_to(factory_root, data);
        check(r.written == 11 && r.failed == 0, c, "seed 前置（11 文件）");
        const fs::path def_file = data / "jpeg" / "default.json";
        pp::FormatPresetData def;
        check(pp::ui::load_format_preset(def_file, def).empty(), c, "加载 jpeg default");
        check(pp::is_default_format_preset_name(def.name), c, "default 判定（JSON name）");
        // 保存 = 直接修改 default.json（允许覆盖）。
        def.params["distance"] = 2.5;
        fs::path saved;
        const std::string serr = pp::ui::save_format_preset_to(data, def, &saved);
        check(serr.empty(), c, "default 保存应允许（= 直接修改它）：" + serr);
        check(saved == def_file, c, "default 保存目标应为原 default.json");
        pp::FormatPresetData modified;
        check(pp::ui::load_format_preset(def_file, modified).empty(), c, "重新加载 default");
        const auto dist = modified.params.find("distance");
        check(dist != modified.params.end() &&
                  params_equivalent(pp::ParamSet{{"distance", dist->second}},
                                    pp::ParamSet{{"distance", 2.5}}),
              c, "default 保存后参数生效");
        // 不可删除（中文原因；文件零改动）。
        const std::string del_err = pp::ui::delete_format_preset(def_file);
        check(!del_err.empty(), c, "删除 default 应拒绝");
        check_contains(del_err, "不可删除", c, "删除 default 的中文原因");
        check(fs::exists(def_file), c, "删除拒绝后 default 文件应仍在");
        // 普通预设可删除（另存副本 → 删除）。
        pp::FormatPresetData copy = modified;
        copy.name = "我的 JPEG 副本";
        fs::path copy_saved;
        check(pp::ui::save_format_preset_to(data, copy, &copy_saved).empty(), c, "另存副本");
        check(pp::ui::delete_format_preset(copy_saved).empty(), c, "普通预设应可删除");
        check(!fs::exists(copy_saved), c, "普通预设删除后应消失");
    }

    std::printf("FORMAT-PRESETS %s (%d failures)\n", g_fail == 0 ? "OK" : "FAIL", g_fail);
    return g_fail == 0 ? 0 : 1;
}
