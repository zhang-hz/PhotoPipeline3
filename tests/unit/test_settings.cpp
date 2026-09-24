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
           a.split_by_format == b.split_by_format;
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
                                    "split_by_format=true"};
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
        // Redaction is snapshot-only: the file keeps the real value.
        const fs::path dir = make_temp_dir("settings_redact");
        const fs::path file = dir / "settings.ini";
        const std::string err = pp::save_settings(file, s);
        check(err.empty() && contains(read_all(file), "amap_key=KEY-1234-abc"),
              "to-string/save-keeps-cleartext", "err=" + err + " raw=" + read_all(file));
    }

    if (g_failed == 0) {
        std::printf("test_settings: OK\n");
        return 0;
    }
    std::printf("test_settings: FAILED (%d)\n", g_failed);
    return g_failed;
}
