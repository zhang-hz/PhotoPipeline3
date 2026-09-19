// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T1 — filesystem helpers implementation.
//
// Contract: docs/m1-tasks.md §3.2 (PP-FROZEN) / §4.1.
// Interpretations where §3.2 is silent (reported in the M1-T1 report):
//   * with_extension(p, "") keeps the current extension (mirrors the mirror_path rule
//     "new_ext 为空 → 保留原扩展名"); a leading '.' in new_ext is tolerated.
//   * is_inside(child, parent) is a subtree test: true for descendants AND for child==parent.
//   * collect_inputs() drops exact duplicates, sorts the result, and treats an empty
//     extension whitelist as "accept every regular file".

#include "core/fsops.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <system_error>
#include <utility>

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
std::filesystem::path lexical_abs(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path a = std::filesystem::absolute(p, ec);
    if (ec) {
        return p.lexically_normal();
    }
    return a.lexically_normal();
}

// weakly_canonical as required by §3.2 (falls back to lexical form when the call errors).
std::filesystem::path weakly(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path c = std::filesystem::weakly_canonical(p, ec);
    if (ec) {
        return lexical_abs(p);
    }
    return c;
}

// Component-wise prefix test; both arguments must already be normalized.
bool path_prefix(const std::filesystem::path& parent, const std::filesystem::path& child) {
    auto pit = parent.begin();
    auto cit = child.begin();
    for (; pit != parent.end(); ++pit, ++cit) {
        if (cit == child.end() || *pit != *cit) {
            return false;
        }
    }
    return true;
}

bool ext_matches(const std::filesystem::path& p, const std::set<std::string>& want) {
    if (want.empty()) {
        return true;
    }
    return want.count(lower_copy(strip_dot(p.extension().string()))) != 0;
}

}  // namespace

std::filesystem::path mirror_path(const std::filesystem::path& src,
                                  const std::filesystem::path& base_dir,
                                  const std::filesystem::path& out_root,
                                  std::string_view new_ext) {
    const std::filesystem::path src_abs = lexical_abs(src);
    const std::filesystem::path base_abs = lexical_abs(base_dir);
    std::filesystem::path rel;
    if (!base_dir.empty() && src_abs != base_abs && path_prefix(base_abs, src_abs)) {
        rel = src_abs.lexically_relative(base_abs);
    } else {
        rel = src.filename();
    }
    return with_extension(out_root / rel, new_ext);
}

OutputPlan resolve_conflict(const std::filesystem::path& desired, ConflictPolicy policy,
                            const std::vector<std::filesystem::path>& reserved,
                            std::string& err) {
    err.clear();
    OutputPlan plan;
    plan.out_path = desired;

    std::set<std::string> reserved_norm;
    // TODO(M2): cache these normalized keys across calls when batches exceed ~10k files.
    for (const std::filesystem::path& r : reserved) {
        reserved_norm.insert(weakly(r).string());
    }
    const auto taken = [&reserved_norm](const std::filesystem::path& p) {
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) {
            return true;
        }
        return reserved_norm.count(weakly(p).string()) != 0;
    };

    if (!taken(desired)) {
        return plan;  // rename_index 0, skip false
    }

    switch (policy) {
        case ConflictPolicy::Skip:
            plan.skip = true;
            return plan;
        case ConflictPolicy::Overwrite:
            return plan;  // caller overwrites desired in place
        case ConflictPolicy::Rename:
            break;
    }

    const std::string stem = desired.stem().string();
    const std::string ext = desired.extension().string();
    for (int i = 1; i <= 10000; ++i) {
        const std::filesystem::path cand =
            desired.parent_path() / (stem + " (" + std::to_string(i) + ")" + ext);
        if (!taken(cand)) {
            plan.out_path = cand;
            plan.rename_index = i;
            return plan;
        }
    }
    err = "cannot find a free output name for: " + desired.string();
    return plan;
}

bool is_inside(const std::filesystem::path& child, const std::filesystem::path& parent) {
    if (child.empty() || parent.empty()) {
        return false;
    }
    const std::filesystem::path c = weakly(child);
    const std::filesystem::path p = weakly(parent);
    return path_prefix(p, c);
}

std::vector<std::filesystem::path> collect_inputs(const std::vector<std::filesystem::path>& roots,
                                                  const std::vector<std::string>& exts,
                                                  std::vector<std::string>& errors) {
    std::set<std::string> want;
    for (const std::string& e : exts) {
        const std::string l = lower_copy(strip_dot(e));
        if (!l.empty()) {
            want.insert(l);
        }
    }

    std::vector<std::filesystem::path> found;
    std::set<std::string> seen;

    const auto push_file = [&found, &seen](const std::filesystem::path& p) {
        const std::string key = lexical_abs(p).string();
        if (seen.insert(key).second) {
            found.push_back(p);
        }
    };

    for (const std::filesystem::path& root : roots) {
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
            // TODO(M2): optional follow-symlink mode + progress callback for huge trees.
            const auto opts = std::filesystem::directory_options::skip_permission_denied;
            for (const std::filesystem::directory_entry& entry :
                 std::filesystem::recursive_directory_iterator(root, opts)) {
                std::error_code fec;
                if (!entry.is_regular_file(fec) || fec) {
                    continue;  // directories, symlinks to nowhere, unreadable entries
                }
                if (ext_matches(entry.path(), want)) {
                    push_file(entry.path());
                }
            }
        } catch (const std::filesystem::filesystem_error& e) {
            errors.push_back("cannot read directory: " + root.string() + ": " + e.what());
        }
    }

    std::sort(found.begin(), found.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  return a.string() < b.string();
              });
    return found;
}

const std::vector<std::string>& input_extensions() {
    // §3.2 comment says "10 种输入" but lists 13 extensions; all 13 are delivered (M1-T1 report).
    static const std::vector<std::string> exts = {
        "tif",  "tiff", "png", "jpg",  "jpeg", "jxl", "heic",
        "heif", "avif", "webp", "bmp", "gif",  "tga",
    };
    return exts;
}

std::filesystem::path with_extension(const std::filesystem::path& p, std::string_view new_ext) {
    const std::string ext = strip_dot(new_ext);
    if (ext.empty()) {
        return p;  // keep the original extension (§3.2 mirror_path rule)
    }
    std::filesystem::path out = p;
    out.replace_extension("." + ext);
    return out;
}

}  // namespace pp
