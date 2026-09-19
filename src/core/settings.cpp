// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1b-U1 — minimal INI settings store.
//
// Contract: docs/m1-tasks.md §3.14 (PP-FROZEN header) + docs/m1b-tasks.md §2.2:
//   * single level `key=value`, `#` starts a whole-line comment;
//   * unknown keys (and comments, and malformed lines) are kept verbatim on save;
//   * missing file -> all defaults, no error;
//   * saving is atomic: write `<file>.tmp`, then rename over `<file>`.
//
// Key names are the struct field names (snake_case); booleans are written `true`/`false`;
// doubles use the shortest `%.10g` form. Values that do not parse are ignored on load
// (the default is kept) — load_settings has no error channel by contract.

#include "core/settings.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace pp {
namespace {

bool is_space(unsigned char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string trim(std::string s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && is_space(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && is_space(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

char ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool parse_int(const std::string& v, int& out) {
    if (v.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long val = std::strtol(v.c_str(), &end, 10);
    if (errno != 0 || end == v.c_str() || *end != '\0') return false;
    if (val < INT_MIN || val > INT_MAX) return false;
    out = static_cast<int>(val);
    return true;
}

bool parse_double(const std::string& v, double& out) {
    if (v.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const double val = std::strtod(v.c_str(), &end);
    if (errno != 0 || end == v.c_str() || *end != '\0') return false;
    out = val;
    return true;
}

bool parse_bool(const std::string& v, bool& out) {
    std::string low;
    low.reserve(v.size());
    for (char c : v) low.push_back(ascii_lower(c));
    if (low == "true" || low == "yes" || low == "on" || low == "1") {
        out = true;
        return true;
    }
    if (low == "false" || low == "no" || low == "off" || low == "0") {
        out = false;
        return true;
    }
    return false;
}

std::string format_double(double d) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", d);
    return buf;
}

// Serialized field list, in a fixed order: used both for saving (values) and for the
// log snapshot. `key` is exactly the AppSettings field name.
std::vector<std::pair<std::string, std::string>> settings_pairs(const AppSettings& s) {
    std::vector<std::pair<std::string, std::string>> kv;
    kv.emplace_back("workers", std::to_string(s.workers));
    kv.emplace_back("budget_gb", std::to_string(s.budget_gb));
    kv.emplace_back("flatten_gray", format_double(s.flatten_gray));
    kv.emplace_back("log_level", s.log_level);
    kv.emplace_back("map_provider", s.map_provider);
    kv.emplace_back("amap_key", s.amap_key);
    kv.emplace_back("tile_cache_mb", std::to_string(s.tile_cache_mb));
    kv.emplace_back("rotate_orientation", s.rotate_orientation ? "true" : "false");
    kv.emplace_back("last_format", s.last_format);
    kv.emplace_back("last_preset", s.last_preset);
    kv.emplace_back("last_out_root", s.last_out_root);
    return kv;
}

void apply_pair(AppSettings& s, const std::string& key, const std::string& value) {
    if (key == "workers") {
        int v = 0;
        if (parse_int(value, v)) s.workers = v;
    } else if (key == "budget_gb") {
        int v = 0;
        if (parse_int(value, v)) s.budget_gb = v;
    } else if (key == "flatten_gray") {
        double v = 0;
        if (parse_double(value, v)) s.flatten_gray = v;
    } else if (key == "log_level") {
        if (!value.empty()) s.log_level = value;
    } else if (key == "map_provider") {
        if (!value.empty()) s.map_provider = value;
    } else if (key == "amap_key") {
        s.amap_key = value;  // empty is a legal value (no key configured)
    } else if (key == "tile_cache_mb") {
        int v = 0;
        if (parse_int(value, v)) s.tile_cache_mb = v;
    } else if (key == "rotate_orientation") {
        bool v = false;
        if (parse_bool(value, v)) s.rotate_orientation = v;
    } else if (key == "last_format") {
        s.last_format = value;
    } else if (key == "last_preset") {
        s.last_preset = value;
    } else if (key == "last_out_root") {
        s.last_out_root = value;
    }
}

// Split one raw line into (key, value); returns false for comments/blank/malformed lines.
bool split_line(const std::string& line, std::string& key, std::string& value) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#') return false;
    const std::size_t eq = t.find('=');
    if (eq == std::string::npos) return false;
    key = trim(t.substr(0, eq));
    value = trim(t.substr(eq + 1));
    return !key.empty();
}

}  // namespace

AppSettings load_settings(const std::filesystem::path& file) {
    AppSettings s;  // defaults (docs/m1-tasks.md §3.14)
    std::ifstream in(file, std::ios::binary);
    if (!in) return s;  // missing/unreadable file -> all defaults, no error

    std::string line;
    while (std::getline(in, line)) {
        std::string key;
        std::string value;
        if (!split_line(line, key, value)) continue;
        apply_pair(s, key, value);
    }
    return s;
}

std::string save_settings(const std::filesystem::path& file, const AppSettings& s) {
    const std::vector<std::pair<std::string, std::string>> pairs = settings_pairs(s);

    // 1) Read the current file so comments/unknown keys survive byte-for-byte.
    std::vector<std::string> lines;
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        std::ifstream in(file, std::ios::binary);
        if (!in) return "settings: cannot read " + file.string();
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
        if (in.bad()) return "settings: read error on " + file.string();
    }

    // 2) Rewrite known keys in place; unknown/comment/malformed lines are left untouched.
    std::vector<bool> seen(pairs.size(), false);
    for (std::string& line : lines) {
        std::string key;
        std::string value;
        if (!split_line(line, key, value)) continue;
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            if (pairs[i].first != key) continue;
            line = key + "=" + pairs[i].second;
            seen[i] = true;
            break;
        }
    }

    // 3) Append keys the file did not have yet (fresh file: all of them).
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (!seen[i]) lines.push_back(pairs[i].first + "=" + pairs[i].second);
    }

    // 4) Atomic write: `<file>.tmp` then rename. A missing parent directory fails here
    //    (creating it is platform::data_dir()'s job, not the settings store's).
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return "settings: cannot write " + tmp.string();
        for (const std::string& line : lines) out << line << '\n';
        out.flush();
        if (!out) {
            std::error_code rm_ec;
            std::filesystem::remove(tmp, rm_ec);
            return "settings: write failed on " + tmp.string();
        }
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp, rm_ec);
        return "settings: cannot replace " + file.string() + ": " + ec.message();
    }
    return {};
}

std::string settings_to_string(const AppSettings& s) {
    std::string out;
    for (const auto& [key, value] : settings_pairs(s)) {
        if (!out.empty()) out += ' ';
        out += key;
        out += '=';
        out += value;
    }
    return out;
}

}  // namespace pp
