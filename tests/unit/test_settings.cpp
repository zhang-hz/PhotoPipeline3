// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1b-U1 — settings store unit tests (hand-written assertions).
//
// Contract: docs/m1-tasks.md §3.14 (PP-FROZEN header) / docs/m1b-tasks.md §2.2 / §4.1:
// defaults for a missing file, full-field round-trip, comment + unknown-key preservation,
// atomic write failure into a missing directory.
// Every failure prints "FAIL <case>: <detail>"; main() returns the failure count.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#include "core/settings.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string show(const fs::path &p) { return p.string(); }

// Scratch under the build dir (ctest runs in the build dir, like test_fsops).
fs::path make_temp_dir(const std::string &name) {
    std::error_code ec;
    const fs::path d = fs::current_path(ec) / ".pp_test_tmp" / name;
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

// Distinct non-default value per field, for round-trip checks.
pp::AppSettings non_defaults() {
    pp::AppSettings s;
    s.workers = 12;
    s.budget_gb = 6;
    s.flatten_gray = 0.25;
    s.log_level = "debug";
    s.map_provider = "amap";
    s.amap_key = "KEY-1234-abc";
    s.tile_cache_mb = 128;
    s.rotate_orientation = false;
    s.last_format = "avif";
    s.last_preset = "/home/u/presets/hdr.json";
    s.last_out_root = "/data/out dir";
    // M4-T15（【机械性】本测试是全字段往返的唯一断言点，随 §3.6 追加字段同步补齐）：
    //   * stagger_ms / thread_budget —— W1-T7 落地（T7 自测在 test_scheduler_contract.cpp）；
    //   * output_template / split_by_format —— W3-T15 落地。
    s.stagger_ms = 275;
    s.thread_budget = 9;
    s.output_template = "$dir/$format/$file";
    s.split_by_format = true;
    // M5-T11（A28）：主题档（词表 system|light|dark；此处取非默认档以证往返）
    s.ui_theme_mode = "dark";
    // M6-T11（C6）：图层档（词表 road|satellite|hybrid）+ 混合叠标注（此处取非默认档/值）
    s.map_style = "satellite";
    s.map_sat_labels = false;
    return s;
}

bool same(const pp::AppSettings &a, const pp::AppSettings &b) {
    return a.workers == b.workers && a.budget_gb == b.budget_gb &&
           a.flatten_gray == b.flatten_gray && a.log_level == b.log_level &&
           a.map_provider == b.map_provider && a.amap_key == b.amap_key &&
           a.tile_cache_mb == b.tile_cache_mb && a.rotate_orientation == b.rotate_orientation &&
           a.last_format == b.last_format && a.last_preset == b.last_preset &&
           a.last_out_root == b.last_out_root && a.stagger_ms == b.stagger_ms &&
           a.thread_budget == b.thread_budget && a.output_template == b.output_template &&
           a.split_by_format == b.split_by_format && a.ui_theme_mode == b.ui_theme_mode &&
           a.map_style == b.map_style && a.map_sat_labels == b.map_sat_labels;
}

std::string dump(const pp::AppSettings &s) { return pp::settings_to_string(s); }

} // namespace

int main() {
    // ---- defaults: missing file loads the §3.14 defaults without error ----
    {
        const fs::path dir = make_temp_dir("settings_defaults");
        const pp::AppSettings want;
        const pp::AppSettings got = pp::load_settings(dir / "missing.ini");
        check(same(got, want), "defaults/all-fields", "got " + dump(got));
        check(want.workers == 0 && want.budget_gb == 0 && want.flatten_gray == 1.0 &&
                  want.log_level == "info" && want.map_provider == "osm" && want.amap_key.empty() &&
                  want.tile_cache_mb == 64 && want.rotate_orientation && want.last_format == "jxl",
              "defaults/frozen-values", "unexpected §3.14 default set");
        // 0.3.0 追加字段的默认值（§3.6 行；默认值取自 §3.2 RunConfig 同名面 —— 逐字核对）
        check(want.stagger_ms == 150 && want.thread_budget == 0 &&
                  want.output_template == "$format/$dir/$file" && !want.split_by_format,
              "defaults/0-3-0-fields", "got " + dump(want));
        // M5-T11/A28：主题档默认 = system（= 现行为：跟随系统，§9.2）；键缺省即现行为
        check(want.ui_theme_mode == "system", "defaults/ui-theme-mode-system", "got " + dump(want));
        // M6-T11（C6）：图层档 / 混合叠标注的默认值 = 现行为（road = 矢量档；true = 混合叠标注）
        check(want.map_style == "road" && want.map_sat_labels, "defaults/m6-map-keys",
              "got " + dump(want));
        // A directory is not a settings file either -> defaults, no error.
        const pp::AppSettings got_dir = pp::load_settings(dir);
        check(same(got_dir, want), "defaults/directory-path", "got " + dump(got_dir));
    }

    // ---- full-field round trip ----
    {
        const fs::path dir = make_temp_dir("settings_roundtrip");
        const fs::path file = dir / "settings.ini";
        const pp::AppSettings in = non_defaults();
        const std::string err = pp::save_settings(file, in);
        check(err.empty(), "roundtrip/save-no-error", "err=" + err);
        check(fs::is_regular_file(file), "roundtrip/file-written", show(file));
        check(!fs::exists(file.string() + ".tmp"), "roundtrip/tmp-removed",
              "leftover " + file.string() + ".tmp");

        const pp::AppSettings out = pp::load_settings(file);
        check(same(out, in), "roundtrip/all-fields", "in=" + dump(in) + " out=" + dump(out));

        // Saving twice must be stable (no duplicated keys, no drift).
        const std::string err2 = pp::save_settings(file, in);
        const pp::AppSettings again = pp::load_settings(file);
        check(err2.empty() && same(again, in), "roundtrip/idempotent", "err=" + err2);
    }

    // ---- comments and unknown keys survive a save; known keys are updated ----
    {
        const fs::path dir = make_temp_dir("settings_unknown");
        const fs::path file = dir / "settings.ini";
        write_file(file, "# PhotoPipeline settings\n"
                         "# second comment line\n"
                         "workers=3\n"
                         "unknown_future_key=keep me\n"
                         "  # indented comment\n"
                         "tile_cache_mb=16\n");

        pp::AppSettings s;
        s.workers = 8;
        s.map_provider = "amap";
        s.tile_cache_mb = 16; // already present in the file -> rewritten in place
        const std::string err = pp::save_settings(file, s);
        check(err.empty(), "unknown/save-no-error", "err=" + err);

        const std::string raw = read_all(file);
        check(contains(raw, "# PhotoPipeline settings"), "unknown/comment-kept", raw);
        check(contains(raw, "# second comment line"), "unknown/second-comment-kept", raw);
        check(contains(raw, "  # indented comment"), "unknown/indented-comment-kept", raw);
        check(contains(raw, "unknown_future_key=keep me"), "unknown/unknown-key-kept", raw);
        check(contains(raw, "workers=8"), "unknown/known-key-updated", raw);
        check(!contains(raw, "workers=3"), "unknown/old-value-gone", raw);
        check(contains(raw, "tile_cache_mb=16"), "unknown/untouched-known-key", raw);

        const pp::AppSettings out = pp::load_settings(file);
        check(out.workers == 8 && out.map_provider == "amap" && out.tile_cache_mb == 16,
              "unknown/reload", "got " + dump(out));
        // The unknown key is still there after a second save.
        const std::string err2 = pp::save_settings(file, out);
        check(err2.empty() && contains(read_all(file), "unknown_future_key=keep me"),
              "unknown/survives-second-save", "err=" + err2);
    }

    // ---- atomic write: missing parent directory -> non-empty error, nothing written ----
    {
        const fs::path dir = make_temp_dir("settings_atomic");
        const fs::path bad = dir / "no_such_subdir" / "settings.ini";
        const std::string err = pp::save_settings(bad, non_defaults());
        check(!err.empty(), "atomic/missing-dir-error", "err was empty");
        check(!fs::exists(bad), "atomic/no-file-created", show(bad));
        check(!fs::exists(bad.string() + ".tmp"), "atomic/no-tmp-left", show(bad));
    }

    // ---- invalid values are ignored (the default is kept), valid ones are applied ----
    {
        const fs::path dir = make_temp_dir("settings_invalid");
        const fs::path file = dir / "settings.ini";
        write_file(file, "workers=abc\n"
                         "budget_gb=\n"
                         "flatten_gray=0.5x\n"
                         "tile_cache_mb=999999999999999999999\n"
                         "rotate_orientation=maybe\n"
                         "log_level=\n");
        const pp::AppSettings def;
        const pp::AppSettings got = pp::load_settings(file);
        check(same(got, def), "invalid/keeps-defaults", "got " + dump(got));

        write_file(file, "workers=7\n"
                         "flatten_gray=0.125\n"
                         "rotate_orientation=off\n"
                         "map_provider=amap\n"
                         "amap_key=\n");
        const pp::AppSettings got2 = pp::load_settings(file);
        check(got2.workers == 7 && got2.flatten_gray == 0.125 && !got2.rotate_orientation &&
                  got2.map_provider == "amap" && got2.amap_key.empty(),
              "invalid/valid-applied", "got " + dump(got2));
    }

    // ---- 0.3.0 §3.6 追加字段（stagger/thread_budget/output_template/split_by_format）----
    // M4-T15：显式覆盖"非法值 → 保留默认"与"合法值 → 应用"两侧（其余字段靠全字段往返）。
    {
        const fs::path dir = make_temp_dir("settings_t030");
        const fs::path file = dir / "settings.ini";
        const pp::AppSettings def;

        // 合法：模板逐字应用；split_by_format 接受 true/off 等既有布尔词表
        write_file(file, "stagger_ms=0\n"
                         "thread_budget=-4\n"
                         "output_template=$dir/$file\n"
                         "split_by_format=on\n");
        const pp::AppSettings ok = pp::load_settings(file);
        check(ok.stagger_ms == 0 && ok.thread_budget == 0 && ok.output_template == "$dir/$file" &&
                  ok.split_by_format,
              "t030/applied", "got " + dump(ok));

        // 非法：未知 $ 符号 / `..` / 绝对路径 / 反斜杠 → 保留默认模板（不把非法模板带进内存）
        for (const char *bad :
             {"photos-$bogus/$file", "../$dir/$file", "/abs/$file", "$dir\\\\$file"}) {
            write_file(file, std::string("output_template=") + bad + "\n");
            const pp::AppSettings got = pp::load_settings(file);
            check(got.output_template == def.output_template,
                  std::string("t030/bad-template-kept-default/") + bad, "got " + dump(got));
        }
        // 空值 = 未配置 → 保留默认（同 class_file 口径）
        write_file(file, "output_template=\nsplit_by_format=\n");
        const pp::AppSettings empty = pp::load_settings(file);
        check(empty.output_template == def.output_template && !empty.split_by_format,
              "t030/empty-keeps-default", "got " + dump(empty));
    }

    // ---- M5-T11/A28：ui_theme_mode 的三档词表（system|light|dark）+ 非法值保留默认 ----
    {
        const fs::path dir = make_temp_dir("settings_theme_mode");
        const fs::path file = dir / "settings.ini";
        const pp::AppSettings def;

        // 三档逐一 set→save→load 往返（档名单源 = AppSettings 字段文本）
        for (const char *mode : {"light", "dark", "system"}) {
            pp::AppSettings s;
            s.ui_theme_mode = mode;
            const std::string err = pp::save_settings(file, s);
            const pp::AppSettings got = pp::load_settings(file);
            check(err.empty() && got.ui_theme_mode == mode &&
                      contains(read_all(file), std::string("ui_theme_mode=") + mode),
                  std::string("ui-theme/roundtrip/") + mode, "err=" + err + " got=" + dump(got));
        }
        // 大小写不敏感（手改配置文件容错）：Dark → dark
        write_file(file, "ui_theme_mode=Dark\n");
        check(pp::load_settings(file).ui_theme_mode == "dark", "ui-theme/case-insensitive",
              "got " + dump(pp::load_settings(file)));
        // 词表外/空值 → 保留默认（不把未知档带进内存）
        for (const char *bad : {"auto", "system2", "", "  ", "light dark"}) {
            write_file(file, std::string("ui_theme_mode=") + bad + "\n");
            check(pp::load_settings(file).ui_theme_mode == def.ui_theme_mode,
                  std::string("ui-theme/bad-keeps-default/") + bad,
                  "got " + dump(pp::load_settings(file)));
        }
    }

    // ---- M6-T11（C6）：map_style / map_sat_labels —— 三档往返 + 非法回落 + 老串缺键取默认 ----
    {
        const fs::path dir = make_temp_dir("settings_map_style");
        const fs::path file = dir / "settings.ini";
        const pp::AppSettings def;

        // 三档逐一 set→save→load 往返（档名 = AppSettings 字段文本 = settings.ini 落值）
        for (const char *style : {"road", "satellite", "hybrid"}) {
            pp::AppSettings s;
            s.map_style = style;
            const std::string err = pp::save_settings(file, s);
            const pp::AppSettings got = pp::load_settings(file);
            check(err.empty() && got.map_style == style &&
                      contains(read_all(file), std::string("map_style=") + style),
                  std::string("map-style/roundtrip/") + style, "err=" + err + " got=" + dump(got));
        }
        // 混合叠标注两端往返（true = 默认档；false = 关闭档；落值恒为 true/false 词表）
        for (const bool labels : {true, false}) {
            pp::AppSettings s;
            s.map_sat_labels = labels;
            const std::string err = pp::save_settings(file, s);
            const pp::AppSettings got = pp::load_settings(file);
            check(err.empty() && got.map_sat_labels == labels &&
                      contains(read_all(file),
                               std::string("map_sat_labels=") + (labels ? "true" : "false")),
                  std::string("map-labels/roundtrip/") + (labels ? "true" : "false"),
                  "err=" + err + " got=" + dump(got));
        }
        // 大小写不敏感（手改配置文件容错）：Satellite → satellite
        write_file(file, "map_style=Satellite\n");
        check(pp::load_settings(file).map_style == "satellite", "map-style/case-insensitive",
              "got " + dump(pp::load_settings(file)));
        // 词表外/空值 → 保留默认（不把未知档带进内存 —— GPS 卡三态 Seg 不会出现"第四档"）
        for (const char *bad : {"sat", "hybrid2", "esri", "", "  ", "road hybrid"}) {
            write_file(file, std::string("map_style=") + bad + "\n");
            check(pp::load_settings(file).map_style == def.map_style,
                  std::string("map-style/bad-keeps-default/") + bad,
                  "got " + dump(pp::load_settings(file)));
        }
        // 混合叠标注：既有布尔词表逐词可用；词表外 → 保留默认（true）
        const std::pair<const char *, bool> label_words[] = {
            {"true", true},   {"yes", true}, {"on", true},   {"1", true},
            {"false", false}, {"no", false}, {"off", false}, {"0", false}};
        for (const auto &[word, want] : label_words) {
            write_file(file, std::string("map_sat_labels=") + word + "\n");
            const pp::AppSettings got = pp::load_settings(file);
            check(got.map_sat_labels == want, std::string("map-labels/word/") + word,
                  "got " + dump(got));
        }
        write_file(file, "map_sat_labels=maybe\n");
        check(pp::load_settings(file).map_sat_labels == def.map_sat_labels,
              "map-labels/bad-keeps-default", "got " + dump(pp::load_settings(file)));

        // 老 INI 兼容（C6「老 INI 可读（缺键回落默认）」）：T11 之前的设置串（无两枚新键）→
        // 新键取默认，既有键照常解析（键面取 T11 前真实存在的一组）
        write_file(file, "# M6-T11 之前写出的 settings.ini\n"
                         "map_provider=amap\n"
                         "amap_key=KEY-OLD-1\n"
                         "tile_cache_mb=32\n"
                         "ui_theme_mode=dark\n");
        const pp::AppSettings old = pp::load_settings(file);
        check(old.map_provider == "amap" && old.amap_key == "KEY-OLD-1" &&
                  old.tile_cache_mb == 32 && old.ui_theme_mode == "dark" &&
                  old.map_style == def.map_style && old.map_sat_labels == def.map_sat_labels,
              "map-style/old-ini-defaults", "got " + dump(old));
    }

    // ---- settings_to_string: log snapshot contains every field ----
    {
        const pp::AppSettings s = non_defaults();
        const std::string t = pp::settings_to_string(s);
        const char *const keys[] = {"workers=12",
                                    "budget_gb=6",
                                    "flatten_gray=0.25",
                                    "log_level=debug",
                                    "map_provider=amap",
                                    "amap_key=<set>",
                                    "tile_cache_mb=128",
                                    "rotate_orientation=false",
                                    "last_format=avif",
                                    "last_preset=/home/u/presets/hdr.json",
                                    "last_out_root=/data/out dir",
                                    "stagger_ms=275",
                                    "thread_budget=9",
                                    "output_template=$dir/$format/$file",
                                    "split_by_format=true",
                                    "ui_theme_mode=dark",
                                    "map_style=satellite",
                                    "map_sat_labels=false"};
        for (const char *k : keys) {
            check(contains(t, k), std::string("to-string/") + k, "snapshot=" + t);
        }
        // The web-service key must never reach the log in clear text (main-dialogue ruling).
        check(!contains(t, "KEY-1234-abc"), "to-string/amap-key-redacted", "snapshot=" + t);

        // No key configured -> the field is reported as empty (not "<set>").
        pp::AppSettings no_key = s;
        no_key.amap_key.clear();
        const std::string t2 = pp::settings_to_string(no_key);
        check(contains(t2, "amap_key= ") || t2.ends_with("amap_key="), "to-string/amap-key-empty",
              "snapshot=" + t2);
        // Redaction is snapshot-only; the file carries the real value —— R-094/M7-WP5 起以
        // **封存形态**落盘（platform/secret：Windows DPAPI ppsec1: / 其它平台编码档 ppsec2:），
        // 明文不再落盘（断言收紧：文件不得含明文，且必须能读回原值；老明文兼容见 test_secret）。
        const fs::path dir = make_temp_dir("settings_redact");
        const fs::path file = dir / "settings.ini";
        const std::string err = pp::save_settings(file, s);
        const std::string raw = read_all(file);
        check(err.empty(), "to-string/save-no-error", "err=" + err);
        check(!contains(raw, "KEY-1234-abc"), "to-string/save-no-cleartext",
              "amap_key 明文落盘（R-094 未生效）： raw=" + raw);
        check(contains(raw, "amap_key=ppsec"), "to-string/save-sealed", "raw=" + raw);
        check(pp::load_settings(file).amap_key == s.amap_key, "to-string/save-roundtrip",
              "got '" + pp::load_settings(file).amap_key + "'");
    }

    // ---- M7-WP1 · R-050（默认构造无文件系统副作用）+ R-028（tmp 名唯一、失败/成功都不留）----
    {
        // R-050：class_file 默认惰性空串（原 `= default_class_file()` 会在默认构造时就 mkdir）
        const pp::AppSettings def;
        check(def.class_file.empty(), "m7wp1/class-file-lazy-default",
              "class_file default must stay unresolved (lazy), got " + show(def.class_file));
        check(!pp::default_class_file().empty(), "m7wp1/class-file-explicit-resolver",
              "default_class_file() must still resolve the §6.2 single source");
        // 空 class_file 从 INI 往返后仍是"未配置"（空值语义不变）
        const fs::path dir = make_temp_dir("settings_m7wp1");
        const fs::path file = dir / "settings.ini";
        check(pp::save_settings(file, def).empty(), "m7wp1/save-defaults", "");
        const pp::AppSettings back = pp::load_settings(file);
        check(back.class_file.empty(), "m7wp1/class-file-roundtrip-empty", show(back.class_file));

        // R-028：tmp 名带 PID/随机后缀 —— 保存后目录内不得残留任何 .tmp 形态文件
        check(pp::save_settings(file, non_defaults()).empty(), "m7wp1/save-again", "");
        std::error_code ec;
        std::size_t leftovers = 0;
        for (const fs::directory_entry &e : fs::directory_iterator(dir, ec)) {
            if (e.path().filename().string().find(".tmp") != std::string::npos)
                ++leftovers;
        }
        check(leftovers == 0, "m7wp1/no-tmp-leftovers",
              "temp files left behind: " + std::to_string(leftovers));
    }

    // ===== M7-R2B 新增断言（R-104 词表回落 / WP5 跨界 1 default_class_file；断言只增）=====
    {
        // R-104：map_provider 持久层词表回落（对照 map_style 口径：词表外/空值 → 保留默认）。
        const fs::path dir = make_temp_dir("settings_m7r2b");
        const fs::path file = dir / "settings.ini";
        write_file(file, "map_provider=foo\n");
        pp::AppSettings s = pp::load_settings(file);
        check(s.map_provider == "osm", "m7r2b/r104-unknown-fallback",
              "词表外值应保留默认 osm，got " + show(s.map_provider));
        write_file(file, "map_provider=AMAP\n");
        s = pp::load_settings(file);
        check(s.map_provider == "amap", "m7r2b/r104-case-insensitive",
              "词表值大小写不敏感，got " + show(s.map_provider));
        write_file(file, "map_provider=osm\n");
        s = pp::load_settings(file);
        check(s.map_provider == "osm", "m7r2b/r104-osm", "合法值原样收");
        write_file(file, "map_provider=\n");
        s = pp::load_settings(file);
        check(s.map_provider == "osm", "m7r2b/r104-empty-fallback", "空值应保留默认 osm");
        // 既有键零改动：合法值经 save→load 往返保持。
        pp::AppSettings r;
        r.map_provider = "amap";
        check(pp::save_settings(file, r).empty(), "m7r2b/r104-save", "");
        check(pp::load_settings(file).map_provider == "amap", "m7r2b/r104-roundtrip",
              "合法值应往返保持");

        // WP5 跨界 1（R-309 同款病灶收口）：default_class_file 走 platform::path_under 单源 ——
        // 空 data_dir 时返回空串（不再拼 CWD 相对名）；非空时 = data_dir()/classes.json。
        check(pp::default_class_file() ==
                  pp::platform::path_under(pp::platform::data_dir(), "classes.json").string(),
              "m7r2b/class-file-path-under",
              "default_class_file 必须走 path_under 单源（空 data_dir → 空串）");
        check(fs::path(pp::default_class_file()).filename() == "classes.json" ||
                  pp::default_class_file().empty(),
              "m7r2b/class-file-leaf",
              "叶子名应为 classes.json（或空 = 无默认位置），got " +
                  show(pp::default_class_file()));
    }

    if (g_failed == 0) {
        std::printf("test_settings: OK\n");
        return 0;
    }
    std::printf("test_settings: FAILED (%d)\n", g_failed);
    return g_failed;
}
