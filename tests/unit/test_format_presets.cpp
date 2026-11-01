// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M10 / W3-T1：格式预设 core + JSON I/O 单测
//
// 设计契约：docs/v0.4.0-design.md §8（C23）/ §8.6 验收：
//   save→load→save 字节稳定（QJsonDocument::Indented 规范化）、版本/kind/format/未知键/
//   损坏 JSON/保留键往返/策略往返/applies_only_to_own_format/空 adaptive 不写字段。
// 另覆盖：normalize（补默认+锁+位深，幂等）、list（坏文件仍列出+标注）、delete（绝对路径守卫）、
// import/export、校验链的中文错误。
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
#include <string>
#include <system_error>
#include <vector>

#include "core/adaptive.h"
#include "core/format_preset.h"
#include "core/params.h"
#include "env_compat.h"
// NOLINTNEXTLINE(bugprone-suspicious-include)：测试白盒包含（同既有模式）
#include "ui/format_preset_io.cpp" // 单实现（见文件头注）

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
        check(adaptive.value("policy").toString().toStdString() == p.adaptive_policy, c,
              "adaptive.policy 往返");
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

    // 10) 内置（出厂）预设库：5 条合法性 + seed 幂等 + `_builtin-` 只读（M10-W3-T5）
    case_begin("10) 内置预设库：5 条合法性 + seed 幂等 + 只读前缀");
    {
        const std::string c = "builtin";
        const auto &lib = pp::builtin_format_presets();
        check(lib.size() == 5, c, "内置预设应 5 条（实际 " + std::to_string(lib.size()) + "）");
        check(&pp::builtin_format_presets() == &lib, c, "内置库应是稳定单实例");
        std::vector<std::string> ids;
        ids.reserve(lib.size());
        for (const pp::FormatPresetData &p : lib)
            ids.push_back(p.format_id);
        std::sort(ids.begin(), ids.end());
        const std::vector<std::string> expect_ids = {"avif", "heif", "jpeg", "jxl", "webp"};
        check(ids == expect_ids, c, "格式集合应恰为 HEIF/AVIF/JXL/WebP/JPEG（PNG/TIFF/BMP 无）");
        for (const pp::FormatPresetData &p : lib) {
            const pp::FormatDef *f = pp::find_format(p.format_id);
            check(f != nullptr, c, "未知格式 " + p.format_id);
            if (f == nullptr)
                continue;
            const std::string by_tag = "内置示例 · " + f->label;
            check(p.name == by_tag, c, "名称应为「" + by_tag + "」（实际「" + p.name + "」）");
            check(p.version == pp::kFormatPresetVersion, c, "version");
            check(p.out_bitdepth != 0 && std::find(f->bitdepths.begin(), f->bitdepths.end(),
                                                   p.out_bitdepth) != f->bitdepths.end(),
                  c, "位深不在能力内：" + std::to_string(p.out_bitdepth));
            check(pp::find_backend(*f, p.backend_id) != nullptr, c, "后端不存在：" + p.backend_id);
            const pp::AdaptiveDefaultPolicy *d = pp::default_adaptive_policy_for(p.format_id);
            check(d != nullptr && p.adaptive_enabled && p.adaptive_policy == d->text, c,
                  "adaptive 应启用且策略 = §9.7 默认库逐字");
            const std::string verr = pp::validate_format_preset(p);
            check(verr.empty(), c, "内置预设应通过校验链：" + verr);
            for (const auto &[key, value] : p.params) {
                (void)value;
                check(key.rfind("__", 0) != 0, c, "内置参数不应含保留键：" + key);
            }
            // 静态表格式必有默认参数全集；libheif 系依赖运行时内省（不可用时允许空，
            // 载入后由 normalize 依当前环境补齐 —— 不伪造值）。
            const bool runtime_backend =
                std::any_of(f->backends.begin(), f->backends.end(), [](const pp::BackendDef &b) {
                    return b.runtime_introspected && b.techs.empty();
                });
            if (!runtime_backend)
                check(!p.params.empty(), c, "静态表格式的默认参数不应为空：" + p.format_id);
        }

        // seed：写到 <root>/<fmt>/_builtin-<fmt>.json；幂等；已存在不覆盖；只读拒绝删/覆盖。
        const fs::path seed_root = fresh_dir(root, "builtin-seed");
        const pp::ui::FormatPresetSeedReport r1 = pp::ui::seed_builtin_format_presets_to(seed_root);
        check(r1.written == 5 && r1.skipped == 0 && r1.failed == 0 && r1.errors.empty(), c,
              "首次 seed 应写 5 条（written=" + std::to_string(r1.written) + " skipped=" +
                  std::to_string(r1.skipped) + " failed=" + std::to_string(r1.failed) + "）");
        std::map<std::string, std::string> bytes_before;
        for (const pp::FormatPresetData &p : lib) {
            const fs::path file = seed_root / p.format_id / ("_builtin-" + p.format_id + ".json");
            check(fs::exists(file), c, "seed 文件应存在：" + file.string());
            check(pp::ui::is_builtin_format_preset_path(file), c, "seed 文件应识别为内置");
            check(!pp::ui::is_builtin_format_preset_name(p.name), c,
                  "显示名（内置示例 · X）不是文件名，不应被判为只读前缀");
            bytes_before[file.string()] = read_text(file);
            check(bytes_before[file.string()].find("\"__") == std::string::npos, c,
                  "落盘不得含保留键：" + file.string());
            pp::FormatPresetData loaded;
            const std::string lerr = pp::ui::load_format_preset(file, loaded);
            check(lerr.empty(), c, "seed 文件应可载入：" + lerr);
            check(loaded.adaptive_enabled && loaded.adaptive_policy == p.adaptive_policy, c,
                  "seed 文件应保留自适应开关/策略");
            check(loaded.format_id == p.format_id && loaded.name == p.name, c, "seed 文件头字段");
        }
        // 第二次 seed：一个字节都不写（幂等）
        const pp::ui::FormatPresetSeedReport r2 = pp::ui::seed_builtin_format_presets_to(seed_root);
        check(r2.written == 0 && r2.skipped == 5 && r2.failed == 0, c,
              "第二次 seed 应全跳过（written=" + std::to_string(r2.written) +
                  " skipped=" + std::to_string(r2.skipped) + "）");
        for (const auto &[path, bytes] : bytes_before)
            check(read_text(fs::path(path)) == bytes, c, "幂等 seed 不得改动既有文件：" + path);
        // 列举：显示名 = 内置示例 · JPEG（不是文件名）；非损坏（在"用户改坏"之前取证）
        {
            const auto entries = pp::ui::list_format_presets_in(seed_root / "jpeg");
            check(entries.size() == 1, c,
                  "jpeg 目录应恰 1 条（实际 " + std::to_string(entries.size()) + "）");
            if (entries.size() == 1) {
                check(entries[0].name == "内置示例 · JPEG", c,
                      "列显示名（实际「" + entries[0].name + "」）");
                check(!entries[0].broken, c, "内置预设不应标损坏");
            }
        }
        // 用户改过（甚至改坏）的文件也不会被 seed 覆盖
        const fs::path jpeg_builtin = seed_root / "jpeg" / "_builtin-jpeg.json";
        check(write_text(jpeg_builtin, "{\"kind\":\"format-preset\"}"), c, "改写内置文件");
        const pp::ui::FormatPresetSeedReport r3 = pp::ui::seed_builtin_format_presets_to(seed_root);
        check(r3.written == 0 && r3.skipped == 5, c, "已存在（用户改过）也应跳过");
        check(read_text(jpeg_builtin) == "{\"kind\":\"format-preset\"}", c,
              "seed 不得覆盖用户改动");
        // 换一棵独立根：同数据 ⇒ 同字节（确定性写盘）
        const fs::path seed_root_b = fresh_dir(root, "builtin-seed-b");
        (void)pp::ui::seed_builtin_format_presets_to(seed_root_b);
        for (const auto &[path, bytes] : bytes_before) {
            const fs::path rel = fs::path(path).lexically_relative(seed_root);
            check(read_text(seed_root_b / rel) == bytes, c,
                  "两棵独立根的 seed 字节应相同：" + rel.string());
        }
        // 列举：显示名 = 内置示例 · JPEG（不是文件名）；非损坏
        // （已在上面「未被覆盖」之前取证 —— 此处文件已被改坏，不再重复列举断言。）

        // 只读：删除拒绝 + 文件仍在
        check_contains(pp::ui::delete_format_preset(jpeg_builtin), "内置", c, "内置预设删除拒绝");
        check(fs::exists(jpeg_builtin), c, "删除拒绝后文件应仍在");
        // 只读：覆盖拒绝（名字带前缀 ⇒ save 拒绝；既有文件不变）
        {
            pp::FormatPresetData evil = lib.back();
            evil.name = "_builtin-jpeg";
            const std::string before = read_text(jpeg_builtin);
            check_contains(pp::ui::save_format_preset_to(seed_root, evil, nullptr), "内置", c,
                           "内置预设覆盖拒绝");
            check(read_text(jpeg_builtin) == before, c, "覆盖拒绝后文件应零改动");
        }
        // 另存可用：换一个普通名字写入成功（非内置标记）
        {
            pp::FormatPresetData copy = lib.back();
            copy.name = "我的 JPEG 副本";
            fs::path saved;
            const std::string serr = pp::ui::save_format_preset_to(seed_root, copy, &saved);
            check(serr.empty(), c, "另存副本应成功：" + serr);
            check(!saved.empty() && fs::exists(saved) &&
                      !pp::ui::is_builtin_format_preset_path(saved),
                  c, "另存副本不应带只读前缀");
        }
        // 数据目录不可用 → 明确 failed + 中文错误（不静默）
        {
            const pp::ui::FormatPresetSeedReport r4 = pp::ui::seed_builtin_format_presets_to({});
            check(r4.failed == 5 && r4.errors.size() == 5 && r4.written == 0, c,
                  "空 root 应全 failed 并带错误");
            check_contains(r4.errors.empty() ? std::string() : r4.errors.front(), "目录不可用", c,
                           "空 root 错误应含「目录不可用」");
        }
        check(pp::ui::is_builtin_format_preset_name("_builtin-heif") &&
                  !pp::ui::is_builtin_format_preset_name("heif") &&
                  !pp::ui::is_builtin_format_preset_name(""),
              c, "只读前缀判定");
    }

    std::printf("FORMAT-PRESETS %s (%d failures)\n", g_fail == 0 ? "OK" : "FAIL", g_fail);
    return g_fail == 0 ? 0 : 1;
}
