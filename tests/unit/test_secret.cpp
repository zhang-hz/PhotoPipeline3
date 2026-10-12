// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M7-WP5 — 密钥落盘保护单测（R-094，docs/deep-review-0.3.2.md「key 明文存
// settings（settings.cpp:267-333）」）。
//
// 证据面：
//   * 封存/解封往返（空值 / ASCII / 含中文与 emoji / 长值）；
//   * 落盘形态分类：Empty / Sealed / LegacyPlain（0.3.2 明文老格式）/ Corrupt（解不开）；
//   * settings 集成：老明文读入兼容 → 首次保存升级（**明文不再落盘**）→ 保存两次字节不漂移
//     （DPAPI 封存非确定性，靠"等价即原行保留"收敛）；解不开的封存行保存时不被冲空；
//   * harden_file：非 Windows = 0600（编码档不是加密，访问控制=权限）。
//
// 每个失败打印 "FAIL <case>: <detail>"；main() 返回失败数（0 = 全绿）。

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "core/settings.h"
#include "platform/secret.h"

namespace fs = std::filesystem;
namespace secret = pp::platform::secret;

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

// Scratch under the build dir (ctest runs in the build dir, like test_settings).
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

const char *kind_name(secret::StoredKind k) {
    switch (k) {
    case secret::StoredKind::Empty:
        return "Empty";
    case secret::StoredKind::Sealed:
        return "Sealed";
    case secret::StoredKind::LegacyPlain:
        return "LegacyPlain";
    case secret::StoredKind::Corrupt:
        return "Corrupt";
    }
    return "?";
}

void check_roundtrip(const std::string &plain, const std::string &case_name) {
    const std::string stored = secret::seal(plain);
    if (plain.empty()) {
        check(stored.empty(), case_name + "/empty-stays-empty", "got '" + stored + "'");
        return;
    }
    check(!stored.empty() && stored != plain, case_name + "/stored-not-plaintext",
          "seal() 落盘串必须带前缀且不含明文，got '" + stored + "'");
    secret::StoredKind kind = secret::StoredKind::Empty;
    const std::string back = secret::unseal(stored, &kind);
    check(back == plain && kind == secret::StoredKind::Sealed, case_name + "/roundtrip",
          std::string("kind=") + kind_name(kind) + " back='" + back + "'");
}

} // namespace

int run_cases() {
    // ---- 封存/解封往返 ----
    check_roundtrip("", "secret/empty");
    check_roundtrip("KEY-1234-abc", "secret/ascii");
    check_roundtrip("密钥-中文-🎬", "secret/unicode");
    check_roundtrip(std::string(256, 'x'), "secret/long");
    check_roundtrip("+/=_-.", "secret/punct"); // base64 字母表边界字符

    // ---- 落盘形态分类 ----
    {
        secret::StoredKind kind = secret::StoredKind::LegacyPlain;
        check(secret::unseal("", &kind) == "" && kind == secret::StoredKind::Empty, "kind/empty",
              kind_name(kind));
        // 0.3.2 明文老格式：无前缀 → LegacyPlain 原样返回（读入兼容）
        kind = secret::StoredKind::Empty;
        check(secret::unseal("KEY-OLD-1", &kind) == "KEY-OLD-1" &&
                  kind == secret::StoredKind::LegacyPlain,
              "kind/legacy-plain", std::string(kind_name(kind)));
        // 编码档（ppsec2:）任何平台都可解
        const std::string encoded = std::string(secret::kPrefixEncoded) + "S0VZLTI=";
        kind = secret::StoredKind::Empty;
        check(secret::unseal(encoded, &kind) == "KEY-2" && kind == secret::StoredKind::Sealed,
              "kind/encoded-decodes",
              std::string(kind_name(kind)) + " back='" + secret::unseal(encoded) + "'");
        // 损坏的封存串（非法 base64）→ Corrupt、明文为空
        kind = secret::StoredKind::Empty;
        check(
            secret::unseal(std::string(secret::kPrefixEncoded) + "!!not-base64!!", &kind).empty() &&
                kind == secret::StoredKind::Corrupt,
            "kind/corrupt-base64", kind_name(kind));
        // DPAPI 档（ppsec1:）在 Windows 可解、其它平台如实判 Corrupt（不可跨平台解）
        const std::string dpapi_like = std::string(secret::kPrefixDpapi) + "AAAA";
        kind = secret::StoredKind::Empty;
        const std::string dpapi_back = secret::unseal(dpapi_like, &kind);
#ifdef _WIN32
        // AAAA 不是合法 DPAPI 密文 → 同样 Corrupt（分类只看"解不开"，不分平台）
#endif
        check(dpapi_back.empty() && kind == secret::StoredKind::Corrupt, "kind/dpapi-undecodable",
              std::string(kind_name(kind)));
        // seal() 的平台档位：Windows = ppsec1:（DPAPI），其它 = ppsec2:（编码）
        const std::string sealed = secret::seal("KEY-PLAT");
#ifdef _WIN32
        check(contains(sealed, secret::kPrefixDpapi), "platform/windows-dpapi-prefix", sealed);
#else
        check(contains(sealed, secret::kPrefixEncoded), "platform/posix-encoding-prefix", sealed);
#endif
    }

    // ---- settings 集成：老明文读入兼容 → 首次保存升级（明文不再落盘）→ 保存幂等 ----
    {
        const fs::path dir = make_temp_dir("secret_settings_upgrade");
        const fs::path file = dir / "settings.ini";
        // 0.3.2 老格式：明文 amap_key
        write_file(file, "# 0.3.2 settings\n"
                         "workers=4\n"
                         "amap_key=KEY-OLD-PLAIN\n"
                         "unknown_future_key=keep me\n");

        const pp::AppSettings loaded = pp::load_settings(file);
        check(loaded.amap_key == "KEY-OLD-PLAIN", "upgrade/legacy-load",
              "got '" + loaded.amap_key + "'");

        // 首次保存：键值不变，落盘形态升级（明文不得再出现在文件里）
        const std::string err = pp::save_settings(file, loaded);
        const std::string raw1 = read_all(file);
        check(err.empty(), "upgrade/save-no-error", "err=" + err);
        check(!contains(raw1, "KEY-OLD-PLAIN"), "upgrade/no-plaintext-on-disk",
              "明文仍在盘上（R-094 未生效）： " + raw1);
        check(contains(raw1, "amap_key="), "upgrade/key-line-present", raw1);
        check(contains(raw1, "# 0.3.2 settings") && contains(raw1, "unknown_future_key=keep me"),
              "upgrade/verbatim-surroundings", raw1);
        check(pp::load_settings(file).amap_key == "KEY-OLD-PLAIN", "upgrade/roundtrip",
              "got '" + pp::load_settings(file).amap_key + "'");

        // 保存两次：amap_key 行逐字不漂移（DPAPI 封存非确定性 → 等价即原行保留）
        const std::string err2 = pp::save_settings(file, pp::load_settings(file));
        const std::string raw2 = read_all(file);
        check(err2.empty() && raw2 == raw1, "upgrade/save-idempotent",
              "err=" + err2 + (raw2 == raw1 ? "" : "（第二次保存字节漂移）"));

        // 键值变化 → 重新封存（仍旧无明文），旧密文被换、读回是新值
        pp::AppSettings changed = pp::load_settings(file);
        changed.amap_key = "KEY-NEW-PLAIN";
        check(pp::save_settings(file, changed).empty(), "upgrade/change-save", "");
        const std::string raw3 = read_all(file);
        check(!contains(raw3, "KEY-NEW-PLAIN") && !contains(raw3, "KEY-OLD-PLAIN"),
              "upgrade/change-no-plaintext", raw3);
        check(pp::load_settings(file).amap_key == "KEY-NEW-PLAIN", "upgrade/change-roundtrip",
              "got '" + pp::load_settings(file).amap_key + "'");
    }

    // ---- 解不开的封存串：读入得空；保存时不被冲空（换机/换账号不丢密文）----
    {
        const fs::path dir = make_temp_dir("secret_settings_corrupt");
        const fs::path file = dir / "settings.ini";
        const std::string blob = std::string(secret::kPrefixEncoded) + "!!broken!!";
        write_file(file, "amap_key=" + blob + "\n");
        check(pp::load_settings(file).amap_key.empty(), "corrupt/load-empty", "");
        const pp::AppSettings s; // 未配置（空键）
        check(pp::save_settings(file, s).empty(), "corrupt/save-no-error", "");
        check(contains(read_all(file), "amap_key=" + blob), "corrupt/blob-preserved",
              "解不开的封存行被冲掉了： " + read_all(file));
    }

    // ---- harden_file：非 Windows = 0600；Windows = no-op true ----
    {
        const fs::path dir = make_temp_dir("secret_harden");
        const fs::path file = dir / "settings.ini";
        write_file(file, "amap_key=x\n");
        check(secret::harden_file(file), "harden/ok", "");
#ifndef _WIN32
        std::error_code ec;
        const auto perms = fs::status(file, ec).permissions();
        check(!ec && (perms & fs::perms::others_read) == fs::perms::none &&
                  (perms & fs::perms::group_write) == fs::perms::none,
              "harden/posix-0600", "permissions not tightened");
#endif
        check(!secret::harden_file({}), "harden/empty-path-fails", "");
        check(!secret::harden_file(dir / "no_such_file.ini"), "harden/missing-file-fails", "");
    }

    if (g_failed == 0) {
        std::printf("test_secret: OK\n");
        return 0;
    }
    std::printf("test_secret: FAILED (%d)\n", g_failed);
    return g_failed;
}

int main() {
    // main 不得让异常外泄（外泄=terminate，bugprone-exception-escape）：run_cases 整体罩住；
    // 异常路径返回 1（非零=失败，ctest 判定不变），断言与成功路径逐字不动。
    try {
        return run_cases();
    } catch (...) {
        std::printf("test_secret: FAILED (unexpected exception)\n");
        return 1;
    }
}
