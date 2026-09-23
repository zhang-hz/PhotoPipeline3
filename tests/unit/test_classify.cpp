// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M4-W1-T8 — core/classify unit tests (hand-written assertions).
//
// Contract: docs/v0.3.0-design.md §3.6 行 `core/classify.h`（新）+ §6.2（分类子系统）；
//           PP-FROZEN 头见 core/classify.h。
// Covers: 默认模板 / CRUD（含热键冲突与保留虚拟分类「全部」）/ 归属（一文件一分类）/
//         路径规范化（绝对 + Win 大小写折叠 + 尾分隔符折叠）/ classes.json 持久化往返
//         （含自愈读入、原子写、幂等字节）/ 惰性清理 / GroupKey 三态 + 无分组 /
//         §3.6 `class_file` 默认值与往返（注册表位置的单源）。
// Scratch files live under <repo>/.cache/tmp/m4-t8/ (never outside the repository).
// Every failure prints "FAIL <case>: <detail>"; main() returns the number of failures.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/classify.h"
#include "core/settings.h"
#include "platform/paths.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool ok, const std::string &c, const std::string &d) {
    if (!ok) {
        std::printf("FAIL %s: %s\n", c.c_str(), d.c_str());
        ++g_failed;
    }
}

std::string show(const std::string &s) { return "'" + s + "'"; }
std::string show(const fs::path &p) { return "'" + p.string() + "'"; }

fs::path repo_root() {
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::is_directory(p / "tests" / "golden", ec))
            return p;
        if (!p.has_parent_path() || p.parent_path() == p)
            break;
        p = p.parent_path();
    }
    return {};
}

fs::path make_temp_dir(const std::string &name) {
    std::error_code ec;
    const fs::path d = repo_root() / ".cache" / "tmp" / "m4-t8" / name;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

void write_file(const fs::path &p, const std::string &text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}

std::string read_all(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool contains(const std::string &hay, const std::string &needle) {
    return hay.find(needle) != std::string::npos;
}

const pp::ClassDef *by_id(const pp::ClassRegistry &r, const std::string &id) { return r.find(id); }

// 类表逐字段比较（id/name/rgb/hotkey；顺序敏感）
bool same_classes(const pp::ClassRegistry &a, const pp::ClassRegistry &b) {
    if (a.classes.size() != b.classes.size())
        return false;
    for (std::size_t i = 0; i < a.classes.size(); ++i) {
        const pp::ClassDef &x = a.classes[i];
        const pp::ClassDef &y = b.classes[i];
        if (x.id != y.id || x.name != y.name || x.rgb != y.rgb || x.hotkey != y.hotkey)
            return false;
    }
    return true;
}

std::string dump(const pp::ClassRegistry &r) {
    std::string s;
    for (const pp::ClassDef &c : r.classes) {
        s += c.id + "=" + c.name + "/" + std::to_string(c.rgb) + "/";
        s += (c.hotkey == 0 ? std::string("-") : std::string(1, c.hotkey));
        s += " ";
    }
    return s + "| assignments=" + std::to_string(r.assignment.size());
}

// ===========================================================================
// cases
// ===========================================================================

void test_defaults() {
    const pp::ClassRegistry r;
    check(r.classes.size() == 3, "defaults/size", std::to_string(r.classes.size()));
    check(r.classes.size() == 3 && r.classes[0].name == "精选" && r.classes[1].name == "待定" &&
              r.classes[2].name == "废片",
          "defaults/template", dump(r));
    // §6.1 底部热键提示 `1 精选 2 待定 3 废片 0 清除` → 默认模板热键 1/2/3
    check(r.classes.size() == 3 && r.classes[0].hotkey == '1' && r.classes[1].hotkey == '2' &&
              r.classes[2].hotkey == '3',
          "defaults/hotkeys", dump(r));
    check(r.assignment.empty(), "defaults/no-assignment", std::to_string(r.assignment.size()));
    // 「全部」= 保留虚拟分类：不可查、不入类表
    check(r.find(std::string(pp::ClassRegistry::kAllId)) == nullptr, "defaults/all-is-virtual",
          "find(\"all\") must be null");
    check(r.find("pick") != nullptr && r.find("") == nullptr && r.find("nope") == nullptr,
          "defaults/find", dump(r));
}

void test_crud() {
    pp::ClassRegistry r;
    const std::size_t base = r.classes.size();

    // ---- add ----
    check(r.add(pp::ClassDef{"travel", "旅行", 0x1565C0u, '4'}), "add/ok", dump(r));
    check(r.classes.size() == base + 1 && r.classes.back().name == "旅行", "add/appended", dump(r));
    check(!r.add(pp::ClassDef{"travel", "重复", 0, 0}), "add/duplicate-id", "must be rejected");
    check(!r.add(pp::ClassDef{"all", "全部", 0, 0}), "add/reserved-id", "must be rejected");
    check(!r.add(pp::ClassDef{"", "无名", 0, 0}), "add/empty-id", "must be rejected");
    check(!r.add(pp::ClassDef{"noname", "", 0, 0}), "add/empty-name", "must be rejected");
    check(!r.add(pp::ClassDef{"dup-hotkey", "抢键", 0, '1'}), "add/hotkey-taken",
          "hotkey '1' belongs to 精选");
    check(!r.add(pp::ClassDef{"zero-hotkey", "零键", 0, '0'}), "add/hotkey-zero-reserved",
          "'0' is reserved for clear");
    check(!r.add(pp::ClassDef{"bad-hotkey", "坏键", 0, 'a'}), "add/hotkey-invalid",
          "'a' is not in '1'..'9'");
    check(r.add(pp::ClassDef{"nohotkey", "无键", 0x123456u, 0}), "add/hotkey-none", dump(r));
    check(r.classes.size() == base + 2, "add/hotkey-none-count", dump(r));

    // ---- rename ----
    check(r.rename("travel", "远行"), "rename/ok", dump(r));
    check(by_id(r, "travel") != nullptr && by_id(r, "travel")->name == "远行", "rename/applied",
          dump(r));
    check(!r.rename("nope", "x"), "rename/unknown-id", "must be rejected");
    check(!r.rename(std::string(pp::ClassRegistry::kAllId), "x"), "rename/reserved-id",
          "must be rejected");
    check(!r.rename("travel", ""), "rename/empty-name", "must be rejected");
    check(by_id(r, "travel") != nullptr && by_id(r, "travel")->name == "远行",
          "rename/rejected-keeps-name", dump(r));

    // ---- color ----
    check(r.set_color("travel", 0xFF00FFu) && by_id(r, "travel")->rgb == 0xFF00FFu, "color/ok",
          dump(r));
    check(r.set_color("travel", 0xFFFFFFFFu) && by_id(r, "travel")->rgb == 0xFFFFFFu,
          "color/masked-to-24bit", dump(r));
    check(!r.set_color("nope", 1u), "color/unknown-id", "must be rejected");

    // ---- hotkey ----
    check(r.set_hotkey("travel", '9') && by_id(r, "travel")->hotkey == '9', "hotkey/ok", dump(r));
    check(!r.set_hotkey("travel", '2'), "hotkey/conflict", "'2' belongs to 待定");
    check(by_id(r, "travel") != nullptr && by_id(r, "travel")->hotkey == '9',
          "hotkey/conflict-keeps-owner", dump(r));
    check(by_id(r, "maybe") != nullptr && by_id(r, "maybe")->hotkey == '2',
          "hotkey/conflict-victim-untouched", dump(r));
    check(!r.set_hotkey("travel", '0'), "hotkey/zero-reserved", "must be rejected");
    check(!r.set_hotkey("travel", 'x'), "hotkey/invalid", "must be rejected");
    check(r.set_hotkey("nohotkey", 0) && by_id(r, "nohotkey")->hotkey == 0, "hotkey/clear",
          dump(r));
    check(r.set_hotkey("travel", 0) && by_id(r, "travel")->hotkey == 0, "hotkey/clear-second",
          dump(r));

    // ---- erase ----
    const std::size_t before_erase = r.classes.size();
    check(!r.erase("nope"), "erase/unknown-id", "must be rejected");
    check(!r.erase(std::string(pp::ClassRegistry::kAllId)), "erase/reserved-id",
          "must be rejected");
    check(r.erase("nohotkey"), "erase/ok", dump(r));
    check(r.classes.size() == before_erase - 1 && r.find("nohotkey") == nullptr, "erase/applied",
          dump(r));
}

void test_assignment() {
    pp::ClassRegistry r;
    const fs::path a = fs::current_path() / "m4t8_a.jpg";
    const fs::path b = fs::current_path() / "m4t8_b.jpg";

    check(!r.class_of(a).has_value(), "assign/none-by-default", "expected no class");
    r.assign(a, "pick");
    check(r.class_of(a).has_value() && *r.class_of(a) == "pick", "assign/ok",
          r.class_of(a).value_or("<none>"));
    check(r.assignment.size() == 1, "assign/one-entry", std::to_string(r.assignment.size()));

    // 一文件一分类：重复打标 = 覆盖（不新增条目）
    r.assign(a, "maybe");
    check(r.class_of(a).has_value() && *r.class_of(a) == "maybe", "assign/overwrites",
          r.class_of(a).value_or("<none>"));
    check(r.assignment.size() == 1, "assign/still-one-entry", std::to_string(r.assignment.size()));

    // 未知 id / 保留 id「全部」/ 空 id → 清除归属（陈旧 id 不落归属）
    r.assign(b, "pick");
    check(r.class_of(b).has_value(), "assign/second-file", "expected a class");
    r.assign(b, "nope");
    check(!r.class_of(b).has_value(), "assign/unknown-id-clears", "expected no class");
    r.assign(b, "pick");
    r.assign(b, std::string(pp::ClassRegistry::kAllId));
    check(!r.class_of(b).has_value(), "assign/all-clears", "「全部」不是归属");
    r.assign(b, "pick");
    r.clear_assignment(b);
    check(!r.class_of(b).has_value(), "assign/clear", "expected no class");
    check(r.assignment.size() == 1, "assign/clear-count", std::to_string(r.assignment.size()));

    // 删类 → 该类归属一并清除（不留陈旧项）
    r.assign(b, "reject");
    check(r.assignment.size() == 2, "assign/two-entries", std::to_string(r.assignment.size()));
    check(r.erase("reject"), "assign/erase-ok", "");
    check(r.assignment.size() == 1, "assign/erase-purges-assignments",
          std::to_string(r.assignment.size()));
    check(!r.class_of(b).has_value(), "assign/erased-class-has-no-class", "expected no class");

    // 键 = 规范化路径：同一文件的相对/尾分隔符写法命中同一项
    const fs::path rel = fs::path("m4t8_a.jpg");
    check(r.class_of(rel).has_value() && *r.class_of(rel) == "maybe", "assign/key-normalized",
          r.class_of(rel).value_or("<none>"));
}

void test_norm_path() {
    check(pp::ClassRegistry::norm_path(fs::path()) == std::string(), "norm/empty-path",
          show(pp::ClassRegistry::norm_path(fs::path())));

    const fs::path base = fs::current_path();
    const fs::path mixed = base / "Sub" / "Dir" / "Photo.JPG";
    const std::string n = pp::ClassRegistry::norm_path(mixed);
    check(!n.empty() && fs::path(n).is_absolute(), "norm/absolute", show(n));
    check(n == pp::ClassRegistry::norm_path(mixed), "norm/deterministic", show(n));
    check(n.find('\\') == std::string::npos, "norm/generic-separator", show(n));
    check(n.back() != '/', "norm/no-trailing-separator", show(n));
    // 尾分隔符 / ".." / 重复分隔符 折叠为同一键
    check(pp::ClassRegistry::norm_path(fs::path(n + "/")) == n, "norm/trailing-slash",
          show(pp::ClassRegistry::norm_path(fs::path(n + "/"))));
    check(pp::ClassRegistry::norm_path(base / "Sub" / "Dir" / ".." / "Dir" / "Photo.JPG") == n,
          "norm/dotdot",
          show(pp::ClassRegistry::norm_path(base / "Sub" / "Dir" / ".." / "Dir" / "Photo.JPG")));
    check(pp::ClassRegistry::norm_path(fs::path(n + "//")) == n, "norm/double-slash", show(n));
    // 相对路径 → 绝对（norm 的唯一口径）
    check(pp::ClassRegistry::norm_path(fs::path("m4t8_norm.jpg")) ==
              pp::ClassRegistry::norm_path(base / "m4t8_norm.jpg"),
          "norm/relative-to-absolute",
          show(pp::ClassRegistry::norm_path(fs::path("m4t8_norm.jpg"))));

    const std::string upper = pp::ClassRegistry::norm_path(base / "Sub" / "Dir" / "Photo.JPG");
    const std::string lower = pp::ClassRegistry::norm_path(base / "sub" / "dir" / "photo.jpg");
#if defined(_WIN32)
    // §3.6：Windows 折叠大小写（同一文件的两种写法必须同键）
    check(upper == lower, "norm/case-fold-windows", show(upper) + " vs " + show(lower));
    check(pp::ClassRegistry::norm_path(fs::path("C:/Data/Code/PhotoPipeline3/a.JPG")) ==
              pp::ClassRegistry::norm_path(fs::path("c:\\data\\code\\photopipeline3\\A.jpg")),
          "norm/case-fold-drive-and-seps", "drive letter + separators must fold");
#else
    // 其他平台保持大小写（§3.6：折叠仅 Windows）
    check(upper != lower, "norm/case-preserved-posix", show(upper) + " vs " + show(lower));
#endif
}

void test_persistence_roundtrip() {
    const fs::path dir = make_temp_dir("persistence");
    const fs::path file = dir / "classes.json";

    pp::ClassRegistry out;
    out.add(pp::ClassDef{"travel", "旅行", 0x1565C0u, '4'});
    out.rename("reject", "废片（重命名）");
    out.set_color("pick", 0x00897Bu);
    out.assign(dir / "one.jpg", "pick");
    out.assign(dir / "二 张.jpg", "travel"); // 非 ASCII 路径键

    std::string err;
    check(out.save(file, &err), "persist/save-ok", err);
    check(err.empty(), "persist/save-no-error", err);
    check(fs::is_regular_file(file), "persist/file-written", show(file));
    check(!fs::exists(file.string() + ".tmp"), "persist/tmp-removed",
          "leftover " + file.string() + ".tmp");
    const std::string raw = read_all(file);
    check(contains(raw, "\"classes\"") && contains(raw, "\"assignment\""), "persist/json-shape",
          raw.substr(0, 80));

    pp::ClassRegistry in;
    check(in.load(file, &err), "persist/load-ok", err);
    check(same_classes(in, out), "persist/classes-roundtrip", dump(in) + " vs " + dump(out));
    check(in.assignment == out.assignment, "persist/assignment-roundtrip",
          std::to_string(in.assignment.size()) + " vs " + std::to_string(out.assignment.size()));
    check(in.class_of(dir / "one.jpg").value_or("") == "pick", "persist/assign-lookup",
          in.class_of(dir / "one.jpg").value_or("<none>"));
    check(in.class_of(dir / "二 张.jpg").value_or("") == "travel", "persist/assign-unicode-key",
          in.class_of(dir / "二 张.jpg").value_or("<none>"));

    // 幂等：同一状态两次 save 逐字节相同
    const fs::path file2 = dir / "classes2.json";
    check(in.save(file2, &err) && read_all(file2) == raw, "persist/byte-idempotent", err);

    // 再存再读（稳定性）
    pp::ClassRegistry again;
    check(again.load(file2, &err) && same_classes(again, in), "persist/reload-stable", err);
}

void test_persistence_edge() {
    const fs::path dir = make_temp_dir("persistence_edge");
    const fs::path missing = dir / "missing" / "classes.json";
    std::string err;

    // 文件不存在 = 首次运行：保持默认模板并成功
    pp::ClassRegistry fresh;
    check(fresh.load(missing, &err), "edge/missing-ok", err);
    check(err.empty(), "edge/missing-no-error", err);
    check(fresh.classes.size() == 3 && fresh.classes[0].name == "精选", "edge/missing-defaults",
          dump(fresh));

    // 损坏 JSON → false + err，且注册表**不动**
    const fs::path broken = dir / "broken.json";
    write_file(broken, "{ \"classes\": [ { \"id\": ");
    pp::ClassRegistry keep;
    keep.rename("pick", "旧名");
    check(!keep.load(broken, &err), "edge/broken-rejected", "expected false");
    check(!err.empty(), "edge/broken-error", "expected a message");
    check(keep.classes.size() == 3 && keep.classes[0].name == "旧名", "edge/broken-untouched",
          dump(keep));

    // 非对象根 / 类型不符 → false
    write_file(dir / "root_array.json", "[]");
    check(!keep.load(dir / "root_array.json", &err), "edge/root-not-object", err);
    write_file(dir / "classes_obj.json", "{ \"classes\": {} }");
    check(!keep.load(dir / "classes_obj.json", &err), "edge/classes-not-array", err);
    write_file(dir / "assign_arr.json", "{ \"classes\": [], \"assignment\": [] }");
    check(!keep.load(dir / "assign_arr.json", &err), "edge/assignment-not-object", err);

    // 目录路径（不是注册表文件）→ 视为"不存在"（同 settings 口径）
    check(keep.load(dir, &err), "edge/directory-path", err);

    // 自愈读入：保留 id / 重复 id / 重复热键 / 非法 rgb / 陈旧归属 全部被丢弃或归零
    const fs::path heal = dir / "heal.json";
    write_file(heal, R"({
  "version": 1,
  "classes": [
    { "id": "all", "name": "全部", "rgb": "#000000", "hotkey": null },
    { "id": "a", "name": "甲", "rgb": "#112233", "hotkey": "1" },
    { "id": "a", "name": "重复 id", "rgb": "#445566", "hotkey": "4" },
    { "id": "b", "rgb": "zzzzzz", "hotkey": "1" },
    { "id": "c", "name": "丙", "rgb": 12345, "hotkey": "9" },
    { "id": "", "name": "空 id" },
    { "id": "d\u2603", "name": "转义", "hotkey": "8" }
  ],
  "assignment": {
    "c:/x/a.jpg": "a",
    "c:/x/b.jpg": "ghost",
    "c:/x/c.jpg": "all"
  }
})");
    pp::ClassRegistry healed;
    check(healed.load(heal, &err), "edge/heal-load-ok", err);
    check(healed.classes.size() == 4, "edge/heal-count", dump(healed));
    const pp::ClassDef *ha = healed.find("a");
    const pp::ClassDef *hb = healed.find("b");
    const pp::ClassDef *hc = healed.find("c");
    const pp::ClassDef *hd = healed.find("d\u2603");
    check(ha != nullptr && ha->name == "甲" && ha->rgb == 0x112233u && ha->hotkey == '1',
          "edge/heal-first-wins", dump(healed));
    check(hb != nullptr && hb->name == "b" && hb->rgb == 0 && hb->hotkey == 0,
          "edge/heal-bad-fields", dump(healed)); // 名前缺省=id；rgb 非法=0；热键冲突=0
    check(hc != nullptr && hc->name == "丙" && hc->rgb == 0 && hc->hotkey == '9',
          "edge/heal-rgb-type", dump(healed)); // rgb 非字符串 → 0
    check(hd != nullptr && hd->name == "转义" && hd->hotkey == '8', "edge/heal-u-escape",
          dump(healed));
    check(healed.find(std::string(pp::ClassRegistry::kAllId)) == nullptr, "edge/heal-reserved",
          "「全部」不入库");
    check(healed.assignment.size() == 1 && healed.assignment.begin()->second == "a",
          "edge/heal-stale-assignment", std::to_string(healed.assignment.size()));

    // 写盘失败（父目录被同名普通文件占住 → 目录建不出来）→ false + err，且不留 .tmp
    pp::ClassRegistry r;
    write_file(dir / "blocker", "x");
    std::string werr;
    const fs::path blocked = dir / "blocker" / "classes.json";
    check(!r.save(blocked, &werr), "edge/save-fails", "expected false");
    check(!werr.empty(), "edge/save-error-channel", "expected a message");
    check(!fs::exists(blocked) && !fs::exists(blocked.string() + ".tmp"), "edge/save-no-file",
          show(blocked));
}

void test_prune_missing() {
    const fs::path dir = make_temp_dir("prune");
    const fs::path keep = dir / "keep.jpg";
    const fs::path gone = dir / "gone.jpg";
    write_file(keep, "x");
    write_file(gone, "x");

    pp::ClassRegistry r;
    r.assign(keep, "pick");
    r.assign(gone, "reject");
    check(r.assignment.size() == 2, "prune/two-assignments", std::to_string(r.assignment.size()));

    std::error_code ec;
    fs::remove(gone, ec);
    check(!fs::exists(gone), "prune/file-removed", show(gone));

    r.prune_missing();
    check(r.assignment.size() == 1, "prune/removed-one", std::to_string(r.assignment.size()));
    check(r.class_of(keep).value_or("") == "pick", "prune/kept-existing",
          r.class_of(keep).value_or("<none>"));
    check(!r.class_of(gone).has_value(), "prune/dropped-missing", "expected no class");

    r.prune_missing(); // 幂等
    check(r.assignment.size() == 1, "prune/idempotent", std::to_string(r.assignment.size()));
}

void test_group_key() {
    pp::GroupSource src;
    src.datetime_original = "2024:03:01 10:00:00";
    src.camera_model = "  Canon EOS R5  ";
    src.source_format = ".CR2";

    check(pp::group_key(pp::GroupMode::Month, src) == "2024-03", "group/month-exif",
          show(pp::group_key(pp::GroupMode::Month, src)));
    check(pp::group_key(pp::GroupMode::Camera, src) == "Canon EOS R5", "group/camera-trim",
          show(pp::group_key(pp::GroupMode::Camera, src)));
    check(pp::group_key(pp::GroupMode::SourceFormat, src) == "cr2", "group/format-normalized",
          show(pp::group_key(pp::GroupMode::SourceFormat, src)));
    check(pp::group_key(pp::GroupMode::None, src).empty(), "group/none-mode", "expected empty key");
    check(pp::kNoGroupLabel == "无分组", "group/no-group-label", std::string(pp::kNoGroupLabel));

    // ISO 形态也接受（XMP 侧回读的日期经 effective_datetime 已转 EXIF 形态，此处为健壮性）
    pp::GroupSource iso;
    iso.datetime_original = "1999-12-31T23:59:59";
    check(pp::group_key(pp::GroupMode::Month, iso) == "1999-12", "group/month-iso",
          show(pp::group_key(pp::GroupMode::Month, iso)));

    // 缺失/非法 → 空键（消费端归入「无分组」节；不抛错、不猜值）
    const pp::GroupSource empty;
    check(pp::group_key(pp::GroupMode::Month, empty).empty(), "group/month-missing", "");
    check(pp::group_key(pp::GroupMode::Camera, empty).empty(), "group/camera-missing", "");
    check(pp::group_key(pp::GroupMode::SourceFormat, empty).empty(), "group/format-missing", "");
    pp::GroupSource bad;
    bad.datetime_original = "2024:13:01 00:00:00"; // 月非法
    check(pp::group_key(pp::GroupMode::Month, bad).empty(), "group/month-invalid", "");
    bad.datetime_original = "20240301";
    check(pp::group_key(pp::GroupMode::Month, bad).empty(), "group/month-short", "");
    bad.camera_model = "   ";
    check(pp::group_key(pp::GroupMode::Camera, bad).empty(), "group/camera-blank", "");
    bad.source_format = ".";
    check(pp::group_key(pp::GroupMode::SourceFormat, bad).empty(), "group/format-dot-only", "");
}

void test_settings_class_file() {
    // §3.6：class_file 默认 = data_dir()/classes.json（单源 default_class_file()）
    const std::string want = (fs::path(pp::platform::data_dir()) / "classes.json").string();
    const pp::AppSettings def;
    check(def.class_file == want, "settings/class-file-default",
          show(def.class_file) + " vs " + show(want));
    check(pp::default_class_file() == want, "settings/class-file-single-source",
          show(pp::default_class_file()));

    const fs::path dir = make_temp_dir("settings_class_file");
    const fs::path ini = dir / "settings.ini";
    const fs::path reg = dir / "custom-classes.json";

    pp::AppSettings s;
    s.class_file = reg.string();
    const std::string err = pp::save_settings(ini, s);
    check(err.empty(), "settings/save-no-error", err);
    const pp::AppSettings back = pp::load_settings(ini);
    check(back.class_file == s.class_file, "settings/class-file-roundtrip", show(back.class_file));
    check(contains(read_all(ini), "class_file=" + reg.string()), "settings/ini-has-key",
          read_all(ini));

    // 空值 = 未配置 → 保留默认（与其它键"非法即默认"的口径一致）
    const fs::path empty_ini = dir / "empty.ini";
    write_file(empty_ini, "class_file=\n");
    check(pp::load_settings(empty_ini).class_file == want, "settings/class-file-empty-default",
          show(pp::load_settings(empty_ini).class_file));

    // 注册表以 settings.class_file 为唯一路径源（§6.2 持久化路径的单源消费）
    pp::ClassRegistry out;
    out.assign(dir / "one.jpg", "reject");
    check(out.save(back.class_file), "settings/registry-save", back.class_file);
    pp::ClassRegistry in;
    check(in.load(back.class_file), "settings/registry-load", back.class_file);
    check(same_classes(in, out) && in.assignment == out.assignment, "settings/registry-roundtrip",
          dump(in));
}

} // namespace

int main() {
    test_defaults();
    test_crud();
    test_assignment();
    test_norm_path();
    test_persistence_roundtrip();
    test_persistence_edge();
    test_prune_missing();
    test_group_key();
    test_settings_class_file();

    if (g_failed == 0) {
        std::printf("test_classify: OK\n");
        return 0;
    }
    std::printf("test_classify: FAILED (%d)\n", g_failed);
    return g_failed;
}
