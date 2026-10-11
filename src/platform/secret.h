// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M7-WP5 — 密钥落盘保护（R-094，docs/deep-review-0.3.2.md「key 明文存 settings」）。
//
// 口径（逐平台，如实登记）：
//   * Windows（ppsec1:）= **真加密** —— DPAPI CryptProtectData/CryptUnprotectData（用户范围，
//     密文只有当前登录用户可解），明文不再落盘；
//   * 其它平台（ppsec2:）= 可逆 **编码**（base64）+ 文件权限 0600（harden_file）—— **不是加密**！
//     负面清单禁依赖扩张（不引入 keyring/libsecret），无系统密钥环可借；编码只保证"明文不直出"
//     （肩窥/误贴/日志直读看不到明文），真正的访问控制 = 文件 0600。同机攻击者可自行解码，
//     此口径在本注释与 handoff 里写明，不做"看起来加密了"的暗示；
//   * 无前缀 = 0.3.2 及更早的**明文老格式**：读入原样兼容；首次保存即升级为上面两种之一
//     （升级不换键值，只换落盘形态 —— 裁定登记见 build/m7-wp5/handoff.md）。
//
// 落盘串格式：`<前缀><base64(载荷)>`；前缀是单源常量（本文件 kPrefixDpapi / kPrefixEncoded）。
// 解不开的封存串（换机/换账号/损坏）→ unseal 返回空 + kind=Corrupt，保存侧据此**保留原行**
// （不把不可恢复的密文冲成空值），见 core/settings.cpp 的 R-094 段。
//
// Qt-free（与 platform/paths 同口径，core/settings.cpp 直接消费）。

#pragma once

#include <filesystem>
#include <string>

namespace pp::platform::secret {

// 落盘串前缀（唯一真源；格式变更须换前缀版本号）
inline constexpr const char *kPrefixDpapi = "ppsec1:";
inline constexpr const char *kPrefixEncoded = "ppsec2:";

enum class StoredKind {
    Empty,       // 空串（未配置密钥）
    Sealed,      // 本模块的封存串且解封成功
    LegacyPlain, // 老格式明文（unseal 原样返回；保存时升级）
    Corrupt,     // 封存串但解不开（换机/换账号/损坏）→ unseal 返回空
};

// 明文 → 落盘串。空入参 → 空出参（"未配置"永远不带前缀）。
// Windows DPAPI 失败（极罕见）→ 兜底返回明文（老格式）：宁可降级保护也不丢用户密钥；
// 该兜底仅在 API 失败时触发，正常路径明文不落盘（登记裁定）。
std::string seal(const std::string &plain);

// 落盘串 → 明文。kind 缺省可空。老格式（无前缀）原样返回；封存串解不开 → 空 + Corrupt。
std::string unseal(const std::string &stored, StoredKind *kind = nullptr);

// 承载密钥的文件权限收紧：非 Windows = chmod 0600；Windows = no-op（true，DPAPI 密文自保护）。
// 失败返回 false（调用方不阻断 —— 权限是加固不是正确性前提）。
bool harden_file(const std::filesystem::path &file);

} // namespace pp::platform::secret
