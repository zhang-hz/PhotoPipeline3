// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T1 — fsops unit tests (hand-written assertions).
//
// Contract: docs/m1-tasks.md §3.2 (PP-FROZEN) / §4.1.
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
    // ---- mirror_path: 5 cases (nested / other root / ext change / empty ext / unicode) ----
    {
        const fs::path got = pp::mirror_path("/data/in/a/b.png", "/data/in", "/out", "jpg");
        check(got == fs::path("/out/a/b.jpg"), "mirror/nested",
              "expect /out/a/b.jpg, got " + show(got));
    }
    {
        const fs::path got = pp::mirror_path("/elsewhere/x.png", "/data/in", "/out", "jpg");
        check(got == fs::path("/out/x.jpg"), "mirror/other-root",
              "expect /out/x.jpg, got " + show(got));
    }
    {
        const fs::path got = pp::mirror_path("/data/in/pic.tif", "/data/in", "/out", "webp");
        check(got == fs::path("/out/pic.webp"), "mirror/ext-change",
              "expect /out/pic.webp, got " + show(got));
    }
    {
        const fs::path got = pp::mirror_path("/data/in/pic.tif", "/data/in", "/out", "");
        check(got == fs::path("/out/pic.tif"), "mirror/ext-empty",
              "expect /out/pic.tif, got " + show(got));
    }
    {
        const fs::path got = pp::mirror_path("/data/\u7167\u7247/\u5b50\u76ee\u5f55/\u56fe.png",
                                             "/data/\u7167\u7247", "/out", "jpg");
        check(got == fs::path("/out/\u5b50\u76ee\u5f55/\u56fe.jpg"), "mirror/unicode",
              "expect /out/子目录/图.jpg, got " + show(got));
    }
    {
        // base_dir == src -> not a prefix relation for a file; falls back to the filename
        const fs::path got = pp::mirror_path("/data/in", "/data/in", "/out", "jpg");
        check(got == fs::path("/out/in.jpg"), "mirror/base-equals-src",
              "expect /out/in.jpg, got " + show(got));
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

    if (g_failed == 0) {
        std::printf("test_fsops: OK\n");
        return 0;
    }
    std::printf("test_fsops: FAILED (%d)\n", g_failed);
    return g_failed;
}
