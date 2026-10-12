// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T1 — fsops unit tests (hand-written assertions).
// M4-T5 — mirror_path 用例改造为 render_output_path（§4.2 路径模板真值表）+ 批内逐输出冲突。
//
// Contract: docs/m1-tasks.md §3.2 (PP-FROZEN) / §4.1 + docs/v0.3.0-design.md §3.4 / §4.2 / §4.4.
// Every failure prints "FAIL <case>: <detail>"; main() returns the number of failures.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

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
        std::string long_seg(260, 'a');
        long_seg += ".jpg";
        const fs::path long_component = fs::path("C:\\out") / long_seg;
        std::string err;
        check(pp::validate_output_filename(long_component, &err), "m7r2b/vof-long-component",
              "单段 >255 不应再判负，err=" + err);
        std::string deep = "C:\\out";
        for (int i = 0; i < 8; ++i)
            deep += "\\" + std::string(60, 'b');
        deep += "\\x.jpg";
        check(deep.size() > 300, "m7r2b/vof-long-setup", "样例路径应 >300 字符");
        check(pp::validate_output_filename(fs::path(deep), &err), "m7r2b/vof-long-whole",
              "整串 >259 不应再判负，err=" + err);
        // 防线保留：非法字符 / 尾随点空格 / 保留设备名仍拒。
        check(!pp::validate_output_filename(fs::path("C:\\out\\a?b.jpg"), &err),
              "m7r2b/vof-illegal-char", "非法字符必须仍判负");
        check(!pp::validate_output_filename(fs::path("C:\\out\\trailing."), &err),
              "m7r2b/vof-trailing-dot", "尾随点必须仍判负");
        check(!pp::validate_output_filename(fs::path("C:\\out\\trailing "), &err),
              "m7r2b/vof-trailing-space", "尾随空格必须仍判负");
        check(!pp::validate_output_filename(fs::path("C:\\out\\CON.txt"), &err),
              "m7r2b/vof-reserved", "保留设备名必须仍判负");

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

    if (g_failed == 0) {
        std::printf("test_fsops: OK\n");
        return 0;
    }
    std::printf("test_fsops: FAILED (%d)\n", g_failed);
    return g_failed;
}
