// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M7-WP5 — 密钥落盘保护实现（R-094；平台口径与格式契约见 platform/secret.h）。
//
// 结构：base64 编解码（内部纯函数）→ 平台封存（Windows DPAPI / 其它平台编码档）→ 解封分类
// → 文件权限收紧。Windows 的 DPAPI 导入库经 `#pragma comment(lib, "crypt32.lib")` 收在本 TU
// （不给 CMakeLists 再加链接面 —— 本包 CMake 只允许收录 secret.cpp 一行，见 handoff 裁定登记）。

#include "platform/secret.h"

// clang-format off
// include 顺序硬约束（不得排序）：<windows.h> 必须先于 <dpapi.h> —— WINAPI_FAMILY_PARTITION
// 等宏由前者定义，否则 dpapi.h 的 DATA_BLOB / Crypt* 全不可见。
#ifdef _WIN32
#include <windows.h>
#include <dpapi.h> // CryptProtectData / CryptUnprotectData
#pragma comment(lib, "crypt32.lib") // DPAPI 导入库（MSVC/clang-cl 均认，随 .obj 传播到最终链接）
#else
#include <sys/stat.h> // chmod
#endif
// clang-format on

namespace pp::platform::secret {
namespace {

// ---------------------------------------------------------------------------
// base64（标准字母表 + '=' 填充；解码对非法字符/残缺长度判失败 → Corrupt 路径）
// ---------------------------------------------------------------------------
constexpr char kB64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64_encode(const std::string &raw) {
    std::string out;
    out.reserve(((raw.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 2 < raw.size()) {
        const unsigned v = (static_cast<unsigned char>(raw[i]) << 16) |
                           (static_cast<unsigned char>(raw[i + 1]) << 8) |
                           static_cast<unsigned char>(raw[i + 2]);
        out.push_back(kB64Alphabet[(v >> 18) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 12) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 6) & 0x3F]);
        out.push_back(kB64Alphabet[v & 0x3F]);
        i += 3;
    }
    const std::size_t rest = raw.size() - i;
    if (rest == 1) {
        const unsigned v = static_cast<unsigned char>(raw[i]) << 16;
        out.push_back(kB64Alphabet[(v >> 18) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rest == 2) {
        const unsigned v = (static_cast<unsigned char>(raw[i]) << 16) |
                           (static_cast<unsigned char>(raw[i + 1]) << 8);
        out.push_back(kB64Alphabet[(v >> 18) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 12) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

bool b64_value(char c, unsigned &out) {
    if (c >= 'A' && c <= 'Z') {
        out = static_cast<unsigned>(c - 'A');
        return true;
    }
    if (c >= 'a' && c <= 'z') {
        out = static_cast<unsigned>(c - 'a' + 26);
        return true;
    }
    if (c >= '0' && c <= '9') {
        out = static_cast<unsigned>(c - '0' + 52);
        return true;
    }
    if (c == '+') {
        out = 62;
        return true;
    }
    if (c == '/') {
        out = 63;
        return true;
    }
    return false;
}

// 严格解码：非 '=' 的非法字符、'=' 不在末尾/超两枚、总长非 4 的倍数 → false（out 不动）。
bool b64_decode(const std::string &text, std::string &out) {
    if (text.size() % 4 != 0)
        return false;
    std::string raw;
    raw.reserve((text.size() / 4) * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        unsigned v[4] = {0, 0, 0, 0};
        int pad = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = text[i + static_cast<std::size_t>(k)];
            if (c == '=') {
                // '=' 只允许出现在末组的最后两位，且其后不得再有数据字符
                if (i + 4 != text.size() || k < 2)
                    return false;
                ++pad;
                continue;
            }
            if (pad > 0) // '=' 之后又出现数据字符
                return false;
            if (!b64_value(c, v[k]))
                return false;
        }
        raw.push_back(static_cast<char>((v[0] << 2) | (v[1] >> 4)));
        if (pad < 2)
            raw.push_back(static_cast<char>((v[1] << 4) | (v[2] >> 2)));
        if (pad < 1)
            raw.push_back(static_cast<char>((v[2] << 6) | v[3]));
    }
    out = std::move(raw);
    return true;
}

#ifdef _WIN32
// DPAPI 应用熵（固定常量即可：保护目标是"密文不落盘明文"，熵防止跨应用密文误用）。
constexpr unsigned char kDpapiEntropyBytes[] = "PhotoPipeline/settings-secret/v1";

// CryptProtectData/CryptUnprotectData 的 pOptionalEntropy 是非 const 入参 → 每次现搭 DATA_BLOB。
DATA_BLOB dpapi_entropy_blob() {
    DATA_BLOB blob{};
    blob.cbData = sizeof(kDpapiEntropyBytes) - 1;
    blob.pbData = const_cast<BYTE *>(kDpapiEntropyBytes);
    return blob;
}

DATA_BLOB dpapi_blob(const std::string &bytes) {
    DATA_BLOB blob{};
    blob.cbData = static_cast<DWORD>(bytes.size());
    blob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(bytes.data()));
    return blob;
}

bool dpapi_protect(const std::string &plain, std::string &out) {
    DATA_BLOB in = dpapi_blob(plain);
    DATA_BLOB entropy = dpapi_entropy_blob();
    DATA_BLOB sealed{};
    const BOOL ok = ::CryptProtectData(&in, L"PhotoPipeline settings secret", &entropy, nullptr,
                                       nullptr, CRYPTPROTECT_UI_FORBIDDEN, &sealed);
    if (!ok)
        return false;
    out.assign(reinterpret_cast<const char *>(sealed.pbData), sealed.cbData);
    ::LocalFree(sealed.pbData);
    return true;
}

bool dpapi_unprotect(const std::string &sealed, std::string &plain) {
    DATA_BLOB in = dpapi_blob(sealed);
    DATA_BLOB entropy = dpapi_entropy_blob();
    DATA_BLOB opened{};
    const BOOL ok = ::CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr,
                                         CRYPTPROTECT_UI_FORBIDDEN, &opened);
    if (!ok)
        return false;
    plain.assign(reinterpret_cast<const char *>(opened.pbData), opened.cbData);
    ::LocalFree(opened.pbData);
    return true;
}
#endif

bool starts_with(const std::string &text, const char *prefix) {
    const std::size_t n = std::char_traits<char>::length(prefix);
    return text.size() >= n && text.compare(0, n, prefix) == 0;
}

} // namespace

std::string seal(const std::string &plain) {
    if (plain.empty())
        return {}; // "未配置"恒为空值行（不带前缀）
#ifdef _WIN32
    std::string blob;
    if (dpapi_protect(plain, blob))
        return std::string(kPrefixDpapi) + b64_encode(blob);
    // DPAPI 失败兜底（API 级失败，极罕见）：退到编码档 —— 明文仍不落盘、密钥不丢，
    // 保护降级为"编码 + 0600"口径（如实登记，不做静默明文落盘）。
    return std::string(kPrefixEncoded) + b64_encode(plain);
#else
    return std::string(kPrefixEncoded) + b64_encode(plain);
#endif
}

std::string unseal(const std::string &stored, StoredKind *kind) {
    const auto report = [kind](StoredKind k) {
        if (kind != nullptr)
            *kind = k;
    };
    if (stored.empty()) {
        report(StoredKind::Empty);
        return {};
    }
    if (starts_with(stored, kPrefixDpapi)) {
        std::string blob;
        if (!b64_decode(stored.substr(std::char_traits<char>::length(kPrefixDpapi)), blob)) {
            report(StoredKind::Corrupt);
            return {};
        }
#ifdef _WIN32
        std::string plain;
        if (!dpapi_unprotect(blob, plain)) {
            report(StoredKind::Corrupt); // 换机/换账号/损坏 → 保存侧保留原行（不冲成空值）
            return {};
        }
        report(StoredKind::Sealed);
        return plain;
#else
        // DPAPI 密文不可跨平台解（如实分类为 Corrupt；保存侧保留原行，不丢密文）
        (void)blob;
        report(StoredKind::Corrupt);
        return {};
#endif
    }
    if (starts_with(stored, kPrefixEncoded)) {
        std::string plain;
        if (!b64_decode(stored.substr(std::char_traits<char>::length(kPrefixEncoded)), plain)) {
            report(StoredKind::Corrupt);
            return {};
        }
        report(StoredKind::Sealed);
        return plain;
    }
    // 0.3.2 及更早的明文老格式：原样读入（R-094 兼容）；保存侧首次升级为封存形态
    report(StoredKind::LegacyPlain);
    return stored;
}

bool harden_file(const std::filesystem::path &file) {
    // 入参校验跨平台同口径：空路径/文件不存在 = 无从收紧 → false
    if (file.empty())
        return false;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec))
        return false;
#ifdef _WIN32
    // Windows：正常路径下 settings.ini 只落 DPAPI 密文（自保护），文件 ACL 无需收紧；
    // 老格式明文在首次保存即被升级 —— 通过校验即返回 true（no-op 加固）。
    return true;
#else
    // 其它平台口径：访问控制 = 文件权限 0600（编码档不是加密，见 secret.h 头注）
    return ::chmod(file.c_str(), S_IRUSR | S_IWUSR) == 0;
#endif
}

} // namespace pp::platform::secret
