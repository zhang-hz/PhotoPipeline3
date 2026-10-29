// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T1 — fsops unit tests (hand-written assertions).
// M4-T5 — mirror_path 用例改造为 render_output_path（§4.2 路径模板真值表）+ 批内逐输出冲突。
//
// Contract: docs/m1-tasks.md §3.2 (PP-FROZEN) / §4.1 + docs/v0.3.0-design.md §3.4 / §4.2 / §4.4.
// Every failure prints "FAIL <case>: <detail>"; main() returns the number of failures.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
// clang-format off
// 本块禁止 clang-format 排序：sddl.h 依赖 windows.h 的类型（WINAPI/LPWSTR…），字母序会
// 把 sddl.h 排到 windows.h 之前 → C2039。处理与 src/core/fsops.cpp 相同。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sddl.h> // ConvertSidToStringSidW（当前用户 SID 的独立对账路径）
// clang-format on
#endif

#include "core/fsops.h"

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

// <.cache-free> scratch under the build dir (ctest runs in the build dir).
fs::path make_temp_dir(const std::string &name) {
    std::error_code ec;
    const fs::path d = fs::current_path(ec) / ".pp_test_tmp" / name;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

// Repository corpus: walk up from the ctest working directory to find tests/golden.
fs::path find_corpus() {
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::is_directory(p / "tests" / "golden", ec)) {
            return p / "tests" / "golden";
        }
        if (!p.has_parent_path() || p.parent_path() == p) {
            break;
        }
        p = p.parent_path();
    }
    return {};
}

void write_file(const fs::path &p, const std::string &text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f << text;
}

} // namespace

int main() {
    // ---- render_output_path：5 符号真值表（§4.2）----
    // ctx 基准：format_dir=jpeg / rel_dir=base / stem=rgb8 / ext=jpg
    const pp::PathCtx ctx{"jpeg", fs::path("base"), "rgb8", "jpg"};
    {
        const fs::path got = pp::render_output_path("$format/$dir/$file", ctx, "/out");
        check(got == fs::path("/out/jpeg/base/rgb8.jpg"), "tmpl/split-by-format",
              "expect /out/jpeg/base/rgb8.jpg, got " + show(got));
    }
    {
        const fs::path got = pp::render_output_path("$dir/$file", ctx, "/out");
        check(got == fs::path("/out/base/rgb8.jpg"), "tmpl/mirror-first(v0.2)", "got " + show(got));
    }
    {
        const fs::path got = pp::render_output_path("$dir/$format/$file", ctx, "/out");
        check(got == fs::path("/out/base/jpeg/rgb8.jpg"), "tmpl/dir-format-file",
              "got " + show(got));
    }
    {
        const fs::path got = pp::render_output_path("$file", ctx, "/out");
        check(got == fs::path("/out/rgb8.jpg"), "tmpl/file-only", "got " + show(got));
    }
    {
        const fs::path got = pp::render_output_path("$name.$ext", ctx, "/out");
        check(got == fs::path("/out/rgb8.jpg"), "tmpl/name-ext", "got " + show(got));
    }
    {
        const fs::path got = pp::render_output_path("$name", ctx, "/out");
        check(got == fs::path("/out/rgb8"), "tmpl/name-only", "got " + show(got));
    }
    {
        const fs::path got = pp::render_output_path("$format/$name-$ext.$ext", ctx, "/out");
        check(got == fs::path("/out/jpeg/rgb8-jpg.jpg"), "tmpl/mixed-symbols", "got " + show(got));
    }
    // 多级 $dir + 符号与字面段混排
    {
        const pp::PathCtx nested{"webp", fs::path("2024/05"), "IMG_2731", "webp"};
        const fs::path got = pp::render_output_path("photos-$format/$dir/$file", nested, "/out");
        check(got == fs::path("/out/photos-webp/2024/05/IMG_2731.webp"), "tmpl/literal-mixed",
              "expect /out/photos-webp/2024/05/IMG_2731.webp, got " + show(got));
    }
    // $dir 空段坍缩（rel_dir 为空）
    {
        const pp::PathCtx flat{"jpeg", fs::path(), "x", "jpg"};
        const fs::path got = pp::render_output_path("$format/$dir/$file", flat, "/out");
        check(got == fs::path("/out/jpeg/x.jpg"), "tmpl/dir-empty-collapse", "got " + show(got));
    }
    {
        // 混排段里的 $dir 展开为空 → 该段保留字面部分（只有"整段仅 $dir"才坍缩）
        const pp::PathCtx flat{"jpeg", fs::path(), "x", "jpg"};
        const fs::path got = pp::render_output_path("photos-$dir/$file", flat, "/out");
        check(got == fs::path("/out/photos-/x.jpg"), "tmpl/mixed-empty-dir-kept",
              "got " + show(got));
    }
    {
        // 全部段坍缩 → 无文件名可用 → 空 path（调用方按错误处理）
        const pp::PathCtx flat{"jpeg", fs::path(), "x", "jpg"};
        const fs::path got = pp::render_output_path("$dir/", flat, "/out");
        check(got.empty(), "tmpl/all-collapsed-empty", "got " + show(got));
    }
    {
        // 空段（"//"、尾 "/"）坍缩，不产生空目录名
        const fs::path got = pp::render_output_path("$format//$file/", ctx, "/out");
        check(got == fs::path("/out/jpeg/rgb8.jpg"), "tmpl/empty-segments", "got " + show(got));
    }
    {
        // ext 为空（仅元数据"保持原扩展名"由调用方把 ext 设为源扩展名表达）→ $file 无点
        const pp::PathCtx noext{"jpeg", fs::path("base"), "photo", ""};
        const fs::path got = pp::render_output_path("$dir/$file", noext, "/out");
        check(got == fs::path("/out/base/photo"), "tmpl/no-ext", "got " + show(got));
    }
    {
        // 相对 out_root（不规范化越界，纯拼接）
        const fs::path got = pp::render_output_path("$format/$file", ctx, "rel/out");
        check(got == fs::path("rel/out/jpeg/rgb8.jpg"), "tmpl/relative-root", "got " + show(got));
    }
    {
        // Unicode 段（波浪保持，不做任何转码）
        const pp::PathCtx uni{"jpeg", fs::path("\u5b50\u76ee\u5f55"), "\u56fe", "jpg"};
        const fs::path got = pp::render_output_path("$dir/$file", uni, "/out");
        check(got == fs::path("/out/\u5b50\u76ee\u5f55/\u56fe.jpg"), "tmpl/unicode",
              "expect /out/子目录/图.jpg, got " + show(got));
    }

    // ---- validate_output_template：合法/非法穷举 ----
    {
        struct Case {
            const char *tmpl;
            bool ok;
            const char *name;
        };
        const Case cases[] = {
            {"$format/$dir/$file", true, "valid/split"},
            {"$dir/$file", true, "valid/mirror"},
            {"$dir/$format/$file", true, "valid/dir-format"},
            {"photos-$format/$dir/$name-$ext.$ext", true, "valid/mixed"},
            {"$file", true, "valid/file-only"},
            {"", false, "invalid/empty"},
            {"$format/$bogus/$file", false, "invalid/unknown-symbol"},
            {"$Format/$file", false, "invalid/case-sensitive-symbol"},
            {"$format/$", false, "invalid/bare-dollar"},
            {"$format/$1/$file", false, "invalid/numeric-symbol"},
            {"../$file", false, "invalid/dotdot-head"},
            {"$format/../$file", false, "invalid/dotdot-middle"},
            {"..", false, "invalid/dotdot-only"},
            {"/abs/$file", false, "invalid/absolute-posix"},
            {"\\\\srv\\share\\$file", false, "invalid/absolute-unc"},
            {"$format\\$file", false, "invalid/backslash"},
            {"C:/out/$file", false, "invalid/drive-qualified"},
        };
        for (const Case &c : cases) {
            std::string err = "unset";
            const bool ok = pp::validate_output_template(c.tmpl, &err);
            check(ok == c.ok, c.name,
                  std::string("validate('") + c.tmpl + "') = " + (ok ? "true" : "false") +
                      " (want " + (c.ok ? "true" : "false") + "), err='" + err + "'");
            if (c.ok) {
                check(err.empty(), std::string(c.name) + "/err-cleared", "err='" + err + "'");
            } else {
                check(!err.empty(), std::string(c.name) + "/message", "no error message");
            }
        }
        // err 可为 nullptr（只问合法性的调用点）
        check(pp::validate_output_template("$dir/$file", nullptr), "validate/null-err-ok",
              "valid template rejected with nullptr err");
        check(!pp::validate_output_template("$bogus", nullptr), "validate/null-err-bad",
              "invalid template accepted with nullptr err");
        // 非法模板 → render 返回空 path（调用方先 validate 阻断）
        check(pp::render_output_path("$bogus/$file", ctx, "/out").empty(), "render/invalid-empty",
              "invalid template must render to an empty path");
    }

    // ---- relative_dir：PathCtx.rel_dir 的唯一来源（pipeline/scheduler 共用）----
    {
        const fs::path got = pp::relative_dir("/data/in/a/b.png", "/data/in");
        check(got == fs::path("a"), "reldir/nested", "expect a, got " + show(got));
        check(pp::relative_dir("/elsewhere/x.png", "/data/in").empty(), "reldir/other-root",
              "expect empty for a non-prefix base");
        check(pp::relative_dir("/data/in/x.png", "/data/in").empty(), "reldir/top-level",
              "expect empty for a file directly under the base");
        check(pp::relative_dir("/data/in/a/b.png", "").empty(), "reldir/empty-base",
              "expect empty for an empty base_dir");
    }

    // ---- 批内冲突：逐输出独立命名（§4.4）----
    // 同一源的 N 个输出各自独立解析序号：reserved 命中只影响对应的那一个 out_path。
    {
        const fs::path dir = make_temp_dir("fsops_conflict_outputs");
        write_file(dir / "x.jpg", "stale"); // jpeg 目标已存在
        std::vector<fs::path> reserved;
        std::string err;

        const pp::OutputPlan jpeg_plan =
            pp::resolve_conflict(dir / "x.jpg", pp::ConflictPolicy::Rename, reserved, err);
        check(jpeg_plan.out_path == dir / "x (1).jpg" && jpeg_plan.rename_index == 1,
              "conflict-outputs/jpeg-renamed", "got " + show(jpeg_plan.out_path));
        const pp::OutputPlan webp_plan =
            pp::resolve_conflict(dir / "x.webp", pp::ConflictPolicy::Rename, reserved, err);
        check(webp_plan.out_path == dir / "x.webp" && webp_plan.rename_index == 0,
              "conflict-outputs/webp-untouched", "got " + show(webp_plan.out_path));

        // 两个源的 jpeg 输出撞同一路径 → 各自的序号不互相污染（按 out_path 粒度登记）
        reserved.push_back(jpeg_plan.out_path); // 源 A 的 jpeg：x (1).jpg
        reserved.push_back(webp_plan.out_path); // 源 A 的 webp：x.webp
        const pp::OutputPlan second =
            pp::resolve_conflict(dir / "x.jpg", pp::ConflictPolicy::Rename, reserved, err);
        check(second.out_path == dir / "x (2).jpg" && second.rename_index == 2,
              "conflict-outputs/second-source", "got " + show(second.out_path));
        const pp::OutputPlan second_webp =
            pp::resolve_conflict(dir / "x.webp", pp::ConflictPolicy::Rename, reserved, err);
        check(second_webp.out_path == dir / "x (1).webp" && second_webp.rename_index == 1,
              "conflict-outputs/second-webp", "got " + show(second_webp.out_path));
    }

    // ---- with_extension ----
    {
        const fs::path got = pp::with_extension("/a/b/c.png", "tif");
        check(got == fs::path("/a/b/c.tif"), "with-ext/basic", "got " + show(got));
    }
    {
        const fs::path got = pp::with_extension("/a/b/noext", "png");
        check(got == fs::path("/a/b/noext.png"), "with-ext/no-prev-ext", "got " + show(got));
    }
    {
        const fs::path got = pp::with_extension("/a/b/c.png", "");
        check(got == fs::path("/a/b/c.png"), "with-ext/empty-keeps", "got " + show(got));
    }
    {
        const fs::path got = pp::with_extension("/a/b/c.png", ".jpg");
        check(got == fs::path("/a/b/c.jpg"), "with-ext/leading-dot", "got " + show(got));
    }

    // ---- resolve_conflict: 4 cases (skip / overwrite / rename sequence / batch reserved) ----
    {
        const fs::path dir = make_temp_dir("fsops_conflict");
        const fs::path desired = dir / "a.png";
        write_file(desired, "x");
        std::vector<fs::path> reserved;

        std::string err;
        const pp::OutputPlan p1 =
            pp::resolve_conflict(desired, pp::ConflictPolicy::Skip, reserved, err);
        check(p1.skip && p1.out_path == desired && err.empty(), "conflict/skip",
              "skip=" + std::to_string(p1.skip) + " out=" + show(p1.out_path) + " err=" + err);

        const pp::OutputPlan p2 =
            pp::resolve_conflict(desired, pp::ConflictPolicy::Overwrite, reserved, err);
        check(!p2.skip && p2.out_path == desired && err.empty(), "conflict/overwrite",
              "skip=" + std::to_string(p2.skip) + " out=" + show(p2.out_path) + " err=" + err);

        write_file(dir / "a (1).png", "x");
        const pp::OutputPlan p3 =
            pp::resolve_conflict(desired, pp::ConflictPolicy::Rename, reserved, err);
        check(!p3.skip && p3.out_path == dir / "a (2).png" && p3.rename_index == 2 && err.empty(),
              "conflict/rename-sequence",
              "expect a (2).png index=2, got " + show(p3.out_path) +
                  " index=" + std::to_string(p3.rename_index) + " err=" + err);

        const fs::path free_path = dir / "b.png"; // not on disk
        reserved.push_back(free_path);
        const pp::OutputPlan p4 =
            pp::resolve_conflict(free_path, pp::ConflictPolicy::Rename, reserved, err);
        check(!p4.skip && p4.out_path == dir / "b (1).png" && p4.rename_index == 1 && err.empty(),
              "conflict/reserved",
              "expect b (1).png index=1, got " + show(p4.out_path) +
                  " index=" + std::to_string(p4.rename_index) + " err=" + err);

        const pp::OutputPlan p5 =
            pp::resolve_conflict(dir / "c.png", pp::ConflictPolicy::Rename, reserved, err);
        check(!p5.skip && p5.out_path == dir / "c.png" && p5.rename_index == 0 && err.empty(),
              "conflict/free-name-untouched", "got " + show(p5.out_path) + " err=" + err);
    }

    // ---- is_inside: 3 cases (+ documented equal-path interpretation) ----
    {
        check(pp::is_inside("/a/b/c/d.png", "/a/b"), "inside/descendant", "expected true");
        check(!pp::is_inside("/a/x/d.png", "/a/b"), "inside/outside", "expected false");
        check(!pp::is_inside("/a/b", "/a/b/c"), "inside/parent-is-not-inside-child",
              "expected false");
        // §3.2 is silent about equality; M1-T1 interprets "之内" as subtree (equal -> true).
        check(pp::is_inside("/a/b", "/a/b"), "inside/equal", "expected true (subtree semantics)");
    }

    // ---- collect_inputs: corpus recursion (27 fixtures) + extension filtering ----
    const fs::path corpus = find_corpus();
    check(!corpus.empty(), "collect/corpus-found",
          "tests/golden not found walking up from " + show(fs::current_path()));
    if (!corpus.empty()) {
        std::vector<std::string> errors;
        const std::vector<fs::path> files =
            pp::collect_inputs({corpus}, pp::input_extensions(), errors);
        check(errors.empty(), "collect/corpus-no-errors",
              "errors=" + std::to_string(errors.size()) +
                  (errors.empty() ? std::string() : (" first=" + errors.front())));
        check(files.size() == 27, "collect/corpus-count",
              "expect 27 fixture files, got " + std::to_string(files.size()));
        for (const fs::path &f : files) {
            std::error_code ec;
            if (!fs::is_regular_file(f, ec)) {
                check(false, "collect/corpus-regular", "not a regular file: " + show(f));
                break;
            }
        }
        check(files.size() > 0 && files.front() < files.back(), "collect/sorted",
              "result must be sorted for deterministic batches");
    }

    {
        const fs::path dir = make_temp_dir("fsops_collect");
        write_file(dir / "one.png", "x");
        write_file(dir / "two.PNG", "x");   // case-insensitive whitelist
        write_file(dir / "three.txt", "x"); // filtered out
        write_file(dir / "sub" / "four.JPeG", "x");
        std::vector<std::string> errors;
        const std::vector<fs::path> pngs = pp::collect_inputs({dir}, {"png"}, errors);
        check(pngs.size() == 2, "collect/filter-png",
              "expect 2 png (case-insensitive), got " + std::to_string(pngs.size()));
        const std::vector<fs::path> jpegs = pp::collect_inputs({dir}, {"JPEG"}, errors);
        check(jpegs.size() == 1, "collect/filter-upper-ext",
              "expect 1 jpeg, got " + std::to_string(jpegs.size()));

        // A single file root is accepted as-is.
        const std::vector<fs::path> single =
            pp::collect_inputs({dir / "one.png"}, pp::input_extensions(), errors);
        check(single.size() == 1 && single.front() == dir / "one.png", "collect/single-file",
              "got " + std::to_string(single.size()));

        // Duplicate roots must not duplicate entries.
        const std::vector<fs::path> dup = pp::collect_inputs({dir, dir}, {"png"}, errors);
        check(dup.size() == 2, "collect/duplicate-roots",
              "expect 2 (deduplicated), got " + std::to_string(dup.size()));
    }

    {
        std::vector<std::string> errors;
        const std::vector<fs::path> none =
            pp::collect_inputs({"/definitely/not/here-pp"}, pp::input_extensions(), errors);
        check(none.empty() && errors.size() == 1, "collect/missing-root",
              "expect 0 files + 1 error, got " + std::to_string(none.size()) + " files, " +
                  std::to_string(errors.size()) + " errors");
    }

    // ---- input_extensions ----
    {
        const std::vector<std::string> &exts = pp::input_extensions();
        const std::vector<std::string> want = {"tif",  "tiff", "png",  "jpg", "jpeg", "jxl", "heic",
                                               "heif", "avif", "webp", "bmp", "gif",  "tga"};
        check(exts.size() == want.size(), "ext/count",
              "expect " + std::to_string(want.size()) + " extensions, got " +
                  std::to_string(exts.size()));
        for (const std::string &w : want) {
            bool found = false;
            for (const std::string &e : exts) {
                found = found || (e == w);
            }
            check(found, "ext/contains-" + w, "missing extension " + w);
        }
        for (const std::string &e : exts) {
            const bool clean = !e.empty() && e.front() != '.' &&
                               e.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string::npos;
            check(clean, "ext/lowercase-nodot", "bad extension entry: '" + e + "'");
        }
    }

    // ---- M7-WP1 · I1 原子写四件套（R-003/R-028）----
    {
        const fs::path dir = make_temp_dir("fsops_atomic");
        // 基本写 + 字节校验（内容逐字节回读）
        const fs::path target = dir / "a.bin";
        std::string err = "unset";
        check(pp::write_atomic_file(target, std::string_view("hello\x00world", 11), &err),
              "atomic/write-ok", err);
        check(err.empty(), "atomic/write-err-cleared", "err='" + err + "'");
        {
            std::ifstream in(target, std::ios::binary);
            const std::string got((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
            check(got.size() == 11 && got.substr(0, 5) == "hello", "atomic/write-bytes",
                  "got " + std::to_string(got.size()) + " bytes");
        }
        // tmp 名带 PID/随机后缀且互不相同（R-028：双开不互踩）
        const fs::path t1 = pp::atomic_temp_path(target);
        const fs::path t2 = pp::atomic_temp_path(target);
        check(!t1.empty() && !t2.empty() && t1 != t2, "atomic/temp-unique",
              "t1=" + show(t1) + " t2=" + show(t2));
        check(t1.parent_path() == dir && t1.extension() == ".tmp", "atomic/temp-shape",
              "got " + show(t1));
        // 失败清场：目标在不存在的目录 → 写失败且不留任何 tmp 兄弟
        const fs::path bad = dir / "missing-sub" / "x.bin";
        check(!pp::write_atomic_file(bad, std::string_view("x"), &err), "atomic/write-fail",
              "expected failure for a missing parent directory");
        check(!err.empty(), "atomic/write-fail-err", "expected a non-empty error");
        std::error_code ec;
        check(!fs::exists(bad, ec), "atomic/no-half-file", show(bad));
        // commit_temp_file 覆盖已存在目标（Windows rename 语义实证）+ 失败清场
        const fs::path tmp = pp::atomic_temp_path(target);
        write_file(tmp, "new-content");
        check(pp::commit_temp_file(tmp, target, &err), "atomic/commit-overwrite", err);
        {
            std::ifstream in(target, std::ios::binary);
            const std::string got((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
            check(got == "new-content", "atomic/commit-content", "got " + show(got));
        }
        const fs::path ghost = pp::atomic_temp_path(target);
        check(!pp::commit_temp_file(ghost, dir / "out.bin", &err), "atomic/commit-missing-tmp",
              "expected failure for a missing temp file");
        pp::discard_temp_file(ghost); // 幂等、noexcept
        pp::discard_temp_file(ghost);
        check(!fs::exists(ghost, ec), "atomic/discard-gone", show(ghost));
        // 目标已存在 → 权限位继承（覆盖不改权限）
        check(pp::write_atomic_file(target, std::string_view("again"), &err), "atomic/rewrite-ok",
              err);
    }

    // ---- M7-WP1 · I2 validate_output_filename（R-007）----
    {
        struct Case {
            const char *path;
            bool ok;
            const char *name;
        };
        const Case cases[] = {
            {"out/photo.jpg", true, "valid/simple"},
            {"out/sub dir/图-片 (1).JPG", true, "valid/unicode-spaces"},
            {"out/a.b.c.png", true, "valid/dots-in-stem"},
            {"CON", false, "invalid/con"},
            {"out/CON.jpg", false, "invalid/con-with-ext"},
            {"out/prn", false, "invalid/prn-lower"},
            {"out/AUX.png", false, "invalid/aux"},
            {"out/NUL", false, "invalid/nul"},
            {"out/COM1.jpg", false, "invalid/com1"},
            {"out/COM9.jpg", false, "invalid/com9"},
            {"out/LPT1.jpg", false, "invalid/lpt1"},
            {"out/LPT9.jpg", false, "invalid/lpt9"},
            {"out/COM10.jpg", true, "valid/com10-not-reserved"},
            {"out/a:b.jpg", false, "invalid/colon"},
            {"out/a<b.jpg", false, "invalid/lt"},
            {"out/a>b.jpg", false, "invalid/gt"},
            {"out/a\"b.jpg", false, "invalid/quote"},
            {"out/a|b.jpg", false, "invalid/pipe"},
            {"out/a?b.jpg", false, "invalid/question"},
            {"out/a*b.jpg", false, "invalid/star"},
            {"out/bad\nname.jpg", false, "invalid/control-char"},
            {"out/trailing.", false, "invalid/trailing-dot"},
            {"out/trailing ", false, "invalid/trailing-space"},
            {"out/", false, "invalid/no-file-name"},
            {"", false, "invalid/empty"},
        };
        for (const Case &c : cases) {
            std::string err;
            const bool ok = pp::validate_output_filename(fs::path(c.path), &err);
            check(ok == c.ok, c.name,
                  std::string("validate('") + c.path + "') = " + (ok ? "true" : "false") +
                      " (want " + (c.ok ? "true" : "false") + "), err='" + err + "'");
            if (!c.ok) {
                check(!err.empty(), std::string(c.name) + "/message", "no error message");
            }
        }
        // 长度（**断言换锚 · M7-R2B / R-007 收口**）：旧断言钉"单段 >255、整串 >259 判负"
        // （MAX_PATH 预检拦截）。R-007 任务明文解除该硬线（写入口经 normalize_extended_path
        // 归一 `\\?\` 后超长路径可真实落盘），行为变化经任务书授权 → 本两条**换锚为接受**
        // （判据收紧方向：长度不再当非法，保留名/非法字符/尾随点空格防线不动）。
        // 换锚登记：build/m7r2-b/handoff.md「裁定登记」节。
        std::string long_component = "out/";
        long_component.append(300, 'x');
        long_component += ".jpg";
        check(pp::validate_output_filename(fs::path(long_component), nullptr),
              "r007/long-component-accepted",
              "300-char component must be accepted (length hard line lifted)");
        std::string long_total = "out/";
        for (int i = 0; i < 40; ++i)
            long_total += "abcdefghij/";
        long_total += "photo.jpg";
        check(long_total.size() > 259 &&
                  pp::validate_output_filename(fs::path(long_total), nullptr),
              "r007/long-total-accepted",
              "total " + std::to_string(long_total.size()) +
                  " chars must be accepted (length hard line lifted)");
        // err 可为 nullptr
        check(pp::validate_output_filename(fs::path("out/ok.jpg"), nullptr), "valid/null-err",
              "valid path rejected with nullptr err");
    }

    // ---- M7-WP1 · I2 probe_output_root（R-007/R-073 引擎侧预检）----
    {
        const fs::path dir = make_temp_dir("fsops_probe_root") / "created" / "nested";
        std::string err;
        check(pp::probe_output_root(dir, &err), "probe/create-ok", err);
        std::error_code ec;
        check(fs::is_directory(dir, ec), "probe/dir-exists", show(dir));
        // 探针文件必须清掉
        std::size_t leftovers = 0;
        for (const fs::directory_entry &e : fs::directory_iterator(dir, ec)) {
            if (e.path().filename().string().find("pp-write-probe") != std::string::npos)
                ++leftovers;
        }
        check(leftovers == 0, "probe/no-leftovers",
              "leftover probes: " + std::to_string(leftovers));
        check(!pp::probe_output_root(fs::path(), &err), "probe/empty-dir", "expected failure");
        check(!err.empty(), "probe/empty-dir-err", "expected a non-empty error");
    }

    // ---- M7-WP1 · R-005 软保留（幽灵跳过防线）----
    {
        const fs::path dir = make_temp_dir("fsops_soft_reserved");
        const fs::path desired = dir / "ghost.png";
        std::vector<fs::path> hard;
        std::vector<fs::path> soft;
        std::string err;

        soft.push_back(desired); // 批内登记过、但磁盘上不存在（失败输出的幽灵占位）
        const pp::OutputPlan p1 =
            pp::resolve_conflict(desired, pp::ConflictPolicy::Skip, hard, soft, err);
        check(!p1.skip && p1.out_path == desired && err.empty(), "conflict/skip-soft-fallback",
              "skip=" + std::to_string(p1.skip) + " out=" + show(p1.out_path) + " err=" + err);

        write_file(desired, "x"); // 磁盘上真的存在 → Skip 必须跳过
        const pp::OutputPlan p2 =
            pp::resolve_conflict(desired, pp::ConflictPolicy::Skip, hard, soft, err);
        check(p2.skip && p2.out_path == desired, "conflict/skip-on-disk",
              "skip=" + std::to_string(p2.skip));

        hard.push_back(dir / "inflight.png"); // 同文件更早 target 的未落盘路径 = 硬保留
        const pp::OutputPlan p3 =
            pp::resolve_conflict(dir / "inflight.png", pp::ConflictPolicy::Skip, hard, soft, err);
        check(p3.skip, "conflict/skip-on-hard-reserved", "expected skip for an in-flight target");

        // Rename 对软保留照旧避让（既有语义逐字不变）
        const pp::OutputPlan p4 =
            pp::resolve_conflict(desired, pp::ConflictPolicy::Rename, hard, soft, err);
        check(p4.out_path == dir / "ghost (1).png" && p4.rename_index == 1,
              "conflict/rename-avoids-soft", "got " + show(p4.out_path));
    }

    // ---- M7-WP1 · R-031 collect_inputs 排除输出根子树 ----
    {
        const fs::path dir = make_temp_dir("fsops_exclude");
        write_file(dir / "in1.png", "x");
        write_file(dir / "sub" / "in2.png", "x");
        write_file(dir / "out" / "copy1.png", "x");          // 输出根
        write_file(dir / "out" / "deep" / "copy2.png", "x"); // 输出根子树
        std::vector<std::string> errors;
        const std::vector<fs::path> all = pp::collect_inputs({dir}, {"png"}, errors);
        check(all.size() == 4, "exclude/without-filter",
              "expect 4, got " + std::to_string(all.size()));
        const std::vector<fs::path> filtered =
            pp::collect_inputs({dir}, {"png"}, {dir / "out"}, errors);
        check(filtered.size() == 2, "exclude/out-root-excluded",
              "expect 2, got " + std::to_string(filtered.size()));
        for (const fs::path &f : filtered) {
            check(!pp::is_inside(f, dir / "out"), "exclude/no-output-inside",
                  "leaked output: " + show(f));
        }
        // 3 参旧签名转发（空排除集）→ 既有行为
        const std::vector<fs::path> legacy = pp::collect_inputs({dir}, {"png"}, errors);
        check(legacy.size() == 4, "exclude/legacy-signature",
              "expect 4, got " + std::to_string(legacy.size()));
    }

    // ---- M7-WP1 · R-083 模板末段必须产出文件名 ----
    {
        struct Case {
            const char *tmpl;
            bool ok;
            const char *name;
        };
        const Case cases[] = {
            {"$format/$dir/$file", true, "r083/valid-default"},
            {"$dir/$file", true, "r083/valid-mirror"},
            {"$file", true, "r083/valid-file"},
            {"$name", true, "r083/valid-name"},
            {"$name.$ext", true, "r083/valid-name-ext"},
            {"photos-$format/$dir/$name-$ext.$ext", true, "r083/valid-mixed"},
            {"jpeg/", false, "r083/invalid-literal-tail"},
            {"$format", false, "r083/invalid-dir-symbol-tail"},
            {"photos-$format", false, "r083/invalid-literal-mixed-tail"},
            {"$dir/", false, "r083/invalid-dir-only"},
            {"out/exports", false, "r083/invalid-nested-literal-tail"},
        };
        for (const Case &c : cases) {
            std::string err;
            const bool ok = pp::validate_output_template(c.tmpl, &err);
            check(ok == c.ok, c.name,
                  std::string("validate('") + c.tmpl + "') = " + (ok ? "true" : "false") +
                      " (want " + (c.ok ? "true" : "false") + "), err='" + err + "'");
            if (!ok) {
                check(pp::render_output_path(c.tmpl, ctx, "/out").empty(),
                      std::string(c.name) + "/render-empty",
                      "invalid template must render to an empty path");
            }
        }
    }

    // ===== M7-R2B 新增断言（R-007 长路径 `\\?\` 归一 + 259/255 硬线解除；断言只增）=====
    {
        // normalize_extended_path：幂等 / 相对不归一 / Windows 绝对与 UNC 归一。
        check(pp::normalize_extended_path({}).empty(), "m7r2b/np-empty", "空路径应原样返回");
        check(pp::normalize_extended_path("a/b.txt") == fs::path("a/b.txt"), "m7r2b/np-relative",
              "相对路径不归一");
#ifdef _WIN32
        check(pp::normalize_extended_path("C:\\out\\x.jpg") == fs::path("\\\\?\\C:\\out\\x.jpg"),
              "m7r2b/np-drive", "drive 绝对路径应归一 \\\\?\\ 形式");
        check(pp::normalize_extended_path("\\\\server\\share\\x.jpg") ==
                  fs::path("\\\\?\\UNC\\server\\share\\x.jpg"),
              "m7r2b/np-unc", "UNC 应归一 \\\\?\\UNC\\ 形式");
        check(pp::normalize_extended_path("C:/out/y.jpg") == fs::path("\\\\?\\C:\\out\\y.jpg"),
              "m7r2b/np-sep", "分隔符应归一为 '\\'（\\\\?\\ 命名空间不接受 '/'）");
        // 幂等：已归一的路径原样返回。
        const fs::path once = pp::normalize_extended_path("C:\\out\\x.jpg");
        check(pp::normalize_extended_path(once) == once, "m7r2b/np-idempotent", show(once));
#else
        check(pp::normalize_extended_path("/tmp/x.jpg") == fs::path("/tmp/x.jpg"), "m7r2b/np-posix",
              "非 Windows 恒原样返回");
#endif

        // 259/255 长度硬线解除：超长整串 / 超长段不再被 validate_output_filename 拦截
        // （保留名/非法字符/尾随点空格防线保留）。
        // 断言面平台同构（M7R21F1-3）：validate_output_filename 的组件级规则跨平台逐字一致
        // （fsops.cpp 无 #ifdef），故样例必须在两侧解析出**相同组件序列**——POSIX 的 fs::path
        // 不切分 `\`、`C:\out` 整串是一个组件（含 `:`，会被非法字符防线误拒），样例根按平台
        // 拼写（#ifdef 分支），断言两侧逐条相同、严格度不变。
#ifdef _WIN32
        const fs::path vof_root = fs::path("C:\\out");
        const char *vof_sep = "\\";
#else
        const fs::path vof_root = fs::path("out");
        const char *vof_sep = "/";
#endif
        std::string long_seg(260, 'a');
        long_seg += ".jpg";
        const fs::path long_component = vof_root / long_seg;
        std::string err;
        check(pp::validate_output_filename(long_component, &err), "m7r2b/vof-long-component",
              "单段 >255 不应再判负，err=" + err);
        std::string deep = vof_root.string();
        for (int i = 0; i < 8; ++i)
            deep += vof_sep + std::string(60, 'b');
        deep += std::string(vof_sep) + "x.jpg";
        check(deep.size() > 300, "m7r2b/vof-long-setup", "样例路径应 >300 字符");
        check(pp::validate_output_filename(fs::path(deep), &err), "m7r2b/vof-long-whole",
              "整串 >259 不应再判负，err=" + err);
        // 防线保留：非法字符 / 尾随点空格 / 保留设备名仍拒。
        check(!pp::validate_output_filename(vof_root / "a?b.jpg", &err), "m7r2b/vof-illegal-char",
              "非法字符必须仍判负");
        check(!pp::validate_output_filename(vof_root / "trailing.", &err), "m7r2b/vof-trailing-dot",
              "尾随点必须仍判负");
        check(!pp::validate_output_filename(vof_root / "trailing ", &err),
              "m7r2b/vof-trailing-space", "尾随空格必须仍判负");
        check(!pp::validate_output_filename(vof_root / "CON.txt", &err), "m7r2b/vof-reserved",
              "保留设备名必须仍判负");

        // 长路径真实写读闭环：>300 字符输出路径经 write_atomic_file 可落盘（写入口已归一）。
        // 测试侧建目录同样按归一口径调用（超长路径下 MSVC std::filesystem 裸调用会撞 MAX_PATH）。
        const fs::path root = make_temp_dir("m7r2b_longpath");
        fs::path deep_dir = root;
        for (int i = 0; i < 5; ++i)
            deep_dir /= std::string(60, 'c');
        std::error_code ec;
        fs::create_directories(pp::normalize_extended_path(deep_dir), ec);
        check(!ec, "m7r2b/long-mkdir", ec.message());
        const fs::path target = deep_dir / (std::string(40, 'd') + ".txt");
        check(target.string().size() > 300, "m7r2b/long-setup",
              "目标路径应 >300 字符，got " + std::to_string(target.string().size()));
        std::string werr;
        check(pp::write_atomic_file(target, "long-path-payload", &werr), "m7r2b/long-write",
              "超长路径写入失败: " + werr);
        check(fs::exists(pp::normalize_extended_path(target), ec), "m7r2b/long-exists",
              "超长路径产物不存在");
        std::string perr;
        check(pp::probe_output_root(deep_dir, &perr), "m7r2b/long-probe-root",
              "超长输出根探针失败: " + perr);
        fs::remove_all(root, ec);
    }

    // =======================================================================
    // M8-W1：输出目录写入权限 / UAC 流程（需求 1）—— 结构化探针 + 纯函数分类/构造
    //   契约：src/core/fsops.h「M8-W1 加性追加」节；根因见 docs/0.3.5-report.md:123-124。
    //   这里只测**可判定逻辑**（不弹 UAC、不改 ACL）：
    //     ① 探针状态分类与 OS 错误码/文案；② should_offer_elevation 的「非 ACL 类不弹」；
    //     ③ SID 校验；④ icacls 参数构造（不经 cmd / SID 授权 / 引号 / 相对路径拒绝）。
    //   ACL 集成实验（icacls /deny 构造不可写目录 → 探针 WriteDenied → /remove:d 还原）
    //   走人工证据脚本 build/evidence-w1/（不在 ctest 里改机器 ACL）。
    // =======================================================================
    {
        // ① 探针：正常目录 → Ok / 无 OS 错误 / detail 为空 / 无残留探针文件
        const fs::path ok_dir = make_temp_dir("m8w1_probe_ok");
        const pp::OutputRootProbe ok = pp::probe_output_root_ex(ok_dir);
        check(ok.status == pp::OutputRootProbeStatus::Ok, "m8w1/probe-ok", "status!=Ok");
        check(ok.os_error == 0, "m8w1/probe-ok-oserror", std::to_string(ok.os_error));
        check(ok.detail.empty(), "m8w1/probe-ok-detail", ok.detail);
        check(std::string(pp::output_root_probe_status_name(ok.status)) == "Ok",
              "m8w1/probe-status-name", pp::output_root_probe_status_name(ok.status));
        std::error_code ec;
        std::size_t leftovers = 0;
        for (const fs::directory_entry &e : fs::directory_iterator(ok_dir, ec)) {
            if (e.path().filename().string().find("pp-write-probe") != std::string::npos)
                ++leftovers;
        }
        check(leftovers == 0, "m8w1/probe-no-leftovers", std::to_string(leftovers));

        // ② 空路径 → EmptyPath（既有 probe_output_root 契约不变：false + 非空 err）
        const pp::OutputRootProbe empty = pp::probe_output_root_ex(fs::path());
        check(empty.status == pp::OutputRootProbeStatus::EmptyPath, "m8w1/probe-empty",
              "status!=EmptyPath");
        std::string empty_err;
        check(!pp::probe_output_root(fs::path(), &empty_err), "m8w1/probe-empty-legacy",
              "expected failure");
        check(empty_err == "output root is empty", "m8w1/probe-empty-legacy-text", empty_err);

        // ③ 输出根是已存在的普通文件 → NotDirectory（不再误报为「不可创建」）
        const fs::path file_root = ok_dir / "plain.txt";
        write_file(file_root, "x");
        const pp::OutputRootProbe not_dir = pp::probe_output_root_ex(file_root);
        check(not_dir.status == pp::OutputRootProbeStatus::NotDirectory, "m8w1/probe-file-root",
              "status!=NotDirectory");
        check(not_dir.detail.find("not a directory") != std::string::npos,
              "m8w1/probe-file-root-detail", not_dir.detail);

        // ④ 父级是普通文件 → NotCreatable，携带 OS 错误码 + 最近已存在目录（诊断）
        const pp::OutputRootProbe not_creatable = pp::probe_output_root_ex(file_root / "nested");
        check(not_creatable.status == pp::OutputRootProbeStatus::NotCreatable,
              "m8w1/probe-not-creatable", "status!=NotCreatable");
        check(not_creatable.os_error != 0, "m8w1/probe-not-creatable-code",
              "expected a non-zero OS error code");
        check(not_creatable.detail.find("not creatable") != std::string::npos,
              "m8w1/probe-not-creatable-detail", not_creatable.detail);
#ifdef _WIN32
        check(not_creatable.detail.find("(win32=") != std::string::npos,
              "m8w1/probe-not-creatable-win32-text", not_creatable.detail);
#else
        check(not_creatable.detail.find("(errno=") != std::string::npos,
              "m8w1/probe-not-creatable-errno-text", not_creatable.detail);
#endif
        check(!not_creatable.nearest_existing.empty(), "m8w1/probe-nearest-existing",
              "expected the nearest existing directory");
        check(not_creatable.detail.find("nearest existing directory") != std::string::npos,
              "m8w1/probe-nearest-existing-detail", not_creatable.detail);

        // ⑤ 错误分类：只有 ACL 类拒绝才值得申请管理员授权（磁盘满/只读介质等一律不弹）
        pp::OutputRootProbe fabricate;
        fabricate.status = pp::OutputRootProbeStatus::Ok;
        check(!pp::should_offer_elevation(fabricate), "m8w1/elev/ok", "Ok 不应提权");
        fabricate.status = pp::OutputRootProbeStatus::EmptyPath;
        check(!pp::should_offer_elevation(fabricate), "m8w1/elev/empty", "空路径不应提权");
        fabricate.status = pp::OutputRootProbeStatus::NotCreatable;
        fabricate.os_error = 5;
        check(!pp::should_offer_elevation(fabricate), "m8w1/elev/not-creatable",
              "不可创建不应提权（先给可操作文案）");
        fabricate.status = pp::OutputRootProbeStatus::NotDirectory;
        check(!pp::should_offer_elevation(fabricate), "m8w1/elev/not-dir", "非目录不应提权");
#ifdef _WIN32
        // 磁盘满(112)/只读介质(19)/共享冲突(32)/路径不存在(3) 都是 WriteFailed → 不弹 UAC
        fabricate.status = pp::OutputRootProbeStatus::WriteFailed;
        for (const unsigned long code : {112ul, 19ul, 32ul, 3ul}) {
            fabricate.os_error = code;
            check(!pp::should_offer_elevation(fabricate), "m8w1/elev/write-failed",
                  "WriteFailed 不应提权, code=" + std::to_string(code));
        }
        fabricate.status = pp::OutputRootProbeStatus::WriteDenied;
        fabricate.os_error = 5; // ERROR_ACCESS_DENIED
        check(pp::should_offer_elevation(fabricate), "m8w1/elev/write-denied",
              "ACL 类拒绝应进入提权流程");
#else
        fabricate.status = pp::OutputRootProbeStatus::WriteFailed;
        fabricate.os_error = 28; // ENOSPC
        check(!pp::should_offer_elevation(fabricate), "m8w1/elev/write-failed", "磁盘满不应提权");
        fabricate.status = pp::OutputRootProbeStatus::WriteDenied;
        fabricate.os_error = 13; // EACCES
        check(!pp::should_offer_elevation(fabricate), "m8w1/elev/no-uac-on-posix",
              "POSIX 无 UAC 通道");
#endif

        // ⑥ SID 校验（icacls 的 *S-1-… 形式；只接受规范串）
        check(pp::is_valid_sid("S-1-5-18"), "m8w1/sid/system", "S-1-5-18 应合法");
        check(pp::is_valid_sid("S-1-5-21-1004336348-1177238915-682003330-512"), "m8w1/sid/user",
              "典型用户 SID 应合法");
        check(pp::is_valid_sid("S-1-0-0"), "m8w1/sid/null", "S-1-0-0 应合法");
        check(pp::is_valid_sid("S-1-5-32-544"), "m8w1/sid/builtin", "内置组 SID 应合法");
        check(!pp::is_valid_sid(""), "m8w1/sid/empty", "空串应判负");
        check(!pp::is_valid_sid("S-1-5"), "m8w1/sid/no-sub", "无子权威应判负");
        check(!pp::is_valid_sid("S-2-5-18"), "m8w1/sid/revision", "revision!=1 应判负");
        check(!pp::is_valid_sid("S-1-5-"), "m8w1/sid/trailing-dash", "尾随 - 应判负");
        check(!pp::is_valid_sid("S-1-5-abc"), "m8w1/sid/non-digit", "非数字子权威应判负");
        check(!pp::is_valid_sid("s-1-5-18"), "m8w1/sid/lowercase", "小写 S 应判负");
        check(!pp::is_valid_sid("S-1-5-01-18"), "m8w1/sid/leading-zero",
              "前导零应判负（非规范串）");
        check(!pp::is_valid_sid("S-1-5-21-4294967296"), "m8w1/sid/sub-overflow",
              "子权威 > 2^32-1 应判负");
        check(!pp::is_valid_sid("S-1-281474976710656-5"), "m8w1/sid/authority-overflow",
              "authority > 2^48-1 应判负");
        check(!pp::is_valid_sid("S-1-5-18 "), "m8w1/sid/trailing-space", "尾随空格应判负");
        check(!pp::is_valid_sid("S-1-5-21-1-2-3-4-5-6-7-8-9-10-11-12-13-14-15"),
              "m8w1/sid/too-many-subs", "子权威 > 15 个应判负");

        // ⑦ icacls 参数构造：不经 cmd、SID 授权、引号正确、非法输入拒绝
        const std::string good_sid = "S-1-5-21-1004336348-1177238915-682003330-512";
        std::string params;
#ifdef _WIN32
        params = pp::icacls_grant_parameters(fs::path("C:/out dir"), good_sid);
        check(params == "\"C:\\out dir\" /grant *S-1-5-21-1004336348-1177238915-682003330-512:"
                        "(OI)(CI)M",
              "m8w1/icacls/params", params);
        check(pp::icacls_grant_parameters(fs::path("C:/out dir/"), good_sid) == params,
              "m8w1/icacls/trailing-slash",
              pp::icacls_grant_parameters(fs::path("C:/out dir/"), good_sid));
        // `%VAR%` 原样保留（ShellExecuteExW→CreateProcess 不做 cmd 展开：旧实现的功能空洞）
        const std::string pct = pp::icacls_grant_parameters(fs::path("C:/a%TMP%b"), good_sid);
        check(pct.find("%TMP%") != std::string::npos, "m8w1/icacls/percent-literal", pct);
        check(pct.find("/c ") == std::string::npos, "m8w1/icacls/no-cmd-c", pct);
        check(pct.find("cmd") == std::string::npos, "m8w1/icacls/no-cmd", pct);
        check(pp::icacls_grant_parameters(fs::path("C:/a\"b"), good_sid).empty(),
              "m8w1/icacls/quote-rejected", "含双引号路径必须拒绝（拒绝比猜安全）");
#else
        params = pp::icacls_grant_parameters(fs::path("/out dir"), good_sid);
        check(params == "\"/out dir\" /grant *S-1-5-21-1004336348-1177238915-682003330-512:"
                        "(OI)(CI)M",
              "m8w1/icacls/params", params);
        check(pp::icacls_grant_parameters(fs::path("/out dir/"), good_sid) == params,
              "m8w1/icacls/trailing-slash",
              pp::icacls_grant_parameters(fs::path("/out dir/"), good_sid));
        const std::string pct = pp::icacls_grant_parameters(fs::path("/a%TMP%b"), good_sid);
        check(pct.find("%TMP%") != std::string::npos, "m8w1/icacls/percent-literal", pct);
        check(pct.find("/c ") == std::string::npos, "m8w1/icacls/no-cmd-c", pct);
        check(pp::icacls_grant_parameters(fs::path("/a\"b"), good_sid).empty(),
              "m8w1/icacls/quote-rejected", "含双引号路径必须拒绝");
#endif
        check(pp::icacls_grant_parameters(fs::path("relative/dir"), good_sid).empty(),
              "m8w1/icacls/relative-rejected",
              "相对路径必须拒绝（提权进程工作目录不是本进程目录）");
        check(pp::icacls_grant_parameters(fs::path(), good_sid).empty(),
              "m8w1/icacls/empty-rejected", "空路径必须拒绝");
        check(pp::icacls_grant_parameters(fs::path("/tmp/x"), "not-a-sid").empty(),
              "m8w1/icacls/bad-sid-rejected", "非法 SID 必须拒绝");
        check(pp::icacls_grant_parameters(fs::path("/tmp/x"), "").empty(),
              "m8w1/icacls/empty-sid-rejected", "空 SID 必须拒绝");

#ifdef _WIN32
        // ⑧ 当前用户 SID：格式合法 + 稳定（同一进程两次调用一致）。
        //    与 WinAPI 独立路径对账：GetUserNameW → LookupAccountNameW → ConvertSidToStringSidW
        //    必须得到同一个 SID（域环境查询失败时只校验格式，不误红）。
        const std::string sid = pp::current_user_sid();
        check(pp::is_valid_sid(sid), "m8w1/current-sid/format", sid);
        check(pp::current_user_sid() == sid, "m8w1/current-sid/stable", "两次调用不一致");
        wchar_t user[256] = {};
        DWORD user_len = static_cast<DWORD>(sizeof(user) / sizeof(user[0]));
        bool matched = false;
        bool lookup_ok = false;
        if (::GetUserNameW(user, &user_len) != FALSE && user_len > 1) {
            wchar_t domain[256] = {};
            DWORD domain_len = static_cast<DWORD>(sizeof(domain) / sizeof(domain[0]));
            BYTE sid_buf[SECURITY_MAX_SID_SIZE] = {};
            DWORD sid_len = static_cast<DWORD>(sizeof(sid_buf));
            SID_NAME_USE use = SidTypeUnknown;
            if (::LookupAccountNameW(nullptr, user, sid_buf, &sid_len, domain, &domain_len, &use) !=
                FALSE) {
                lookup_ok = true;
                LPWSTR text = nullptr;
                if (::ConvertSidToStringSidW(sid_buf, &text) != FALSE && text != nullptr) {
                    char utf8[512] = {};
                    const int n =
                        ::WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8,
                                              static_cast<int>(sizeof(utf8)) - 1, nullptr, nullptr);
                    if (n > 0) {
                        matched = std::string(utf8) == sid;
                    }
                    ::LocalFree(text);
                }
            }
        }
        check(!lookup_ok || matched, "m8w1/current-sid/lookup-match",
              "token SID 与账户名解析的 SID 不一致");
#endif
    }

#ifdef _WIN32
    // ---- M8-W1 ACL 集成实验（opt-in：PP_FSOPS_ACL_INTEGRATION=1，默认 ctest 不跑）----
    //   真实 ACL 闭环（不经 cmd，产品同源 icacls.exe 直调）：
    //   A) icacls /deny 构造不可写目录 → probe_output_root_ex 必须判 WriteDenied + os_error=5
    //      + should_offer_elevation=true → /remove:d 还原后必须回到 Ok；
    //   B) 把产品 icacls_grant_parameters() 的输出原样交给真 icacls 执行 → 退出码 0 且复探 Ok
    //      （验证参数构造/SID 的 `*S-1-…` 语法真被系统接受）。
    //   注意：显式 deny ACE 的优先级高于 grant，所以 A 用 /remove:d 还原而不是用 grant「修」；
    //   真实用户场景是「目录 ACL 没给当前用户写权」（无 deny），B 覆盖该授权的执行面。
    //   无论中间成败都还原并删除临时目录。
    bool acl_integration = false;
#ifdef _WIN32
    {
        char *acl_env = nullptr;
        std::size_t acl_env_size = 0;
        if (::_dupenv_s(&acl_env, &acl_env_size, "PP_FSOPS_ACL_INTEGRATION") == 0 &&
            acl_env != nullptr) {
            acl_integration = std::string(acl_env) == "1";
            std::free(acl_env);
        }
    }
#else
    if (const char *acl_env = std::getenv("PP_FSOPS_ACL_INTEGRATION"); acl_env != nullptr) {
        acl_integration = std::string(acl_env) == "1";
    }
#endif
    if (acl_integration) {
        std::wstring icacls_exe;
        {
            wchar_t sys[MAX_PATH] = {};
            if (::GetSystemDirectoryW(sys, MAX_PATH) != 0) {
                icacls_exe = std::wstring(sys) + L"\\icacls.exe";
            }
        }
        const std::string acl_sid = pp::current_user_sid();
        auto utf8_to_wide = [](const std::string &s) {
            if (s.empty()) {
                return std::wstring();
            }
            const int n =
                ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<std::size_t>(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
            return w;
        };
        // 直接 CreateProcessW 跑 icacls（测试 harness；产品路径同样是 icacls.exe 直调、不经 cmd）
        auto run_icacls = [&icacls_exe](const std::wstring &args, unsigned long *exit_code) {
            if (icacls_exe.empty()) {
                return false;
            }
            std::wstring cmd = L"\"" + icacls_exe + L"\" " + args;
            std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
            mutable_cmd.push_back(L'\0');
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (::CreateProcessW(icacls_exe.c_str(), mutable_cmd.data(), nullptr, nullptr, FALSE,
                                 CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) == FALSE) {
                return false;
            }
            const DWORD wait = ::WaitForSingleObject(pi.hProcess, 30000);
            bool ok = false;
            if (wait == WAIT_OBJECT_0) {
                DWORD code = 1;
                ::GetExitCodeProcess(pi.hProcess, &code);
                if (exit_code != nullptr) {
                    *exit_code = code;
                }
                ok = true;
            }
            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);
            return ok;
        };
        auto quote_path = [](const fs::path &p) { return L"\"" + p.wstring() + L"\""; };
        if (icacls_exe.empty() || !pp::is_valid_sid(acl_sid)) {
            std::printf("SKIP m8w1-acl: icacls.exe/SID unavailable\n");
        } else {
            const std::wstring sid_w = utf8_to_wide(acl_sid);
            const fs::path deny_dir = make_temp_dir("m8w1_acl_deny");
            unsigned long deny_code = 1;
            const bool deny_ran =
                run_icacls(quote_path(deny_dir) + L" /deny *" + sid_w + L":(W)", &deny_code);
            if (!deny_ran || deny_code != 0) {
                std::printf("SKIP m8w1-acl: cannot apply deny ACE (exit=%lu)\n", deny_code);
            } else {
                const pp::OutputRootProbe denied = pp::probe_output_root_ex(deny_dir);
                check(denied.status == pp::OutputRootProbeStatus::WriteDenied,
                      "m8w1-acl/probe-denied",
                      std::string("status=") + pp::output_root_probe_status_name(denied.status) +
                          " detail=" + denied.detail);
                check(denied.os_error == 5ul, "m8w1-acl/probe-denied-code",
                      std::to_string(denied.os_error));
                check(denied.detail.find("win32=5") != std::string::npos,
                      "m8w1-acl/probe-denied-text", denied.detail);
                check(pp::should_offer_elevation(denied), "m8w1-acl/should-elevate",
                      "ACL 类拒绝应进入提权流程");
                std::printf(
                    "M8W1-ACL deny-ace: probe_status=%s os_error=%lu should_offer_elevation=%d"
                    " detail=%s\n",
                    pp::output_root_probe_status_name(denied.status), denied.os_error,
                    pp::should_offer_elevation(denied) ? 1 : 0, denied.detail.c_str());
                // 还原 deny ACE（无论后续断言结果都必须执行）
                unsigned long remove_code = 1;
                const bool remove_ran =
                    run_icacls(quote_path(deny_dir) + L" /remove:d *" + sid_w, &remove_code);
                check(remove_ran && remove_code == 0, "m8w1-acl/remove-deny",
                      "icacls /remove:d 还原失败, exit=" + std::to_string(remove_code));
                const pp::OutputRootProbe restored = pp::probe_output_root_ex(deny_dir);
                check(restored.status == pp::OutputRootProbeStatus::Ok, "m8w1-acl/probe-restored",
                      std::string("status=") + pp::output_root_probe_status_name(restored.status) +
                          " detail=" + restored.detail);
                std::printf("M8W1-ACL restore: probe_status=%s\n",
                            pp::output_root_probe_status_name(restored.status));
            }
            std::error_code rm_ec;
            fs::remove_all(deny_dir, rm_ec);

            // B) 产品参数构造 → 真 icacls 执行 → 复探 Ok
            const fs::path grant_dir = make_temp_dir("m8w1_acl_grant");
            const std::string grant_params = pp::icacls_grant_parameters(grant_dir, acl_sid);
            check(!grant_params.empty(), "m8w1-acl/grant-params", "参数构造为空");
            unsigned long grant_code = 1;
            const bool grant_ran = run_icacls(utf8_to_wide(grant_params), &grant_code);
            check(grant_ran && grant_code == 0, "m8w1-acl/grant-cmd",
                  "icacls 拒绝产品构造的参数, exit=" + std::to_string(grant_code) +
                      " params=" + grant_params);
            const pp::OutputRootProbe granted = pp::probe_output_root_ex(grant_dir);
            check(granted.status == pp::OutputRootProbeStatus::Ok, "m8w1-acl/probe-after-grant",
                  std::string("status=") + pp::output_root_probe_status_name(granted.status) +
                      " detail=" + granted.detail);
            std::printf("M8W1-ACL grant: icacls_exit=%lu params=%s verify_status=%s\n", grant_code,
                        grant_params.c_str(), pp::output_root_probe_status_name(granted.status));
            fs::remove_all(grant_dir, rm_ec);
        }
    }
#endif

    if (g_failed == 0) {
        std::printf("test_fsops: OK\n");
        return 0;
    }
    std::printf("test_fsops: FAILED (%d)\n", g_failed);
    return g_failed;
}
