// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 0.4.2 需求 5：显示面色彩管理（display colour management）。
//
// 背景（用户需求 5 原文）：「预览窗格，显示图片的色彩进行色彩管理……需要在屏幕上显示出照片真正的
// 色彩。现在色彩很灰，没有正确处理 ICC 和色域」。
//
// 根因（已查证 Qt 6.11.2 源码，见报告）：Qt 的 QImage::setColorSpace() **不产生任何绘制期变换**：
//   * qtbase 6.11 `src/gui/painting/qpaintengine_raster.cpp` 的 drawImage 路径没有任何
//     QColorTransform / convertedToColorSpace 调用；`QRasterBuffer::prepare()` 只把目标 QImage
//     的 colorSpace 拷进 raster buffer，唯一用途是 alpha blend 的 gamma 判断；
//   * Windows 平台插件 `src/plugins/platforms/windows/qwindowsscreen.cpp` 未覆写
//     `QPlatformScreen::colorSpace()`（默认 SRgb），即 Windows 上不存在显示色域上报；
//   * Qt 的显示色彩管理只覆盖 macOS / Wayland color-management-v1。
// 因此 R-309 那种「给 QImage 挂源 QColorSpace、等 Qt 绘制时转换」的做法在 Windows 上像素零变化，
// Display P3 / Adobe RGB 源按 sRGB 编码直出 → 发灰。本模块把变换**自己做**：
//   源像素（源 ICC / 假定 sRGB）--lcms2（相对比色 + 黑点补偿，与引擎口径一致）--> 显示 profile
//   编码的 8bit 像素；QImage 挂显示 profile 的 QColorSpace（语义如实：字节 = 显示编码）。
// 只作用于显示面（预览/缩略图），引擎/金样/回归零影响。
//
// 线程安全：system_display_profile() 与 to_display_encoded() 均可从预览池（≤2 线程）并发调用。
#pragma once
#include <QImage>
#include <cstdint>
#include <filesystem>
#include <string>

namespace pp {

// 一次显示 profile 获取的结果（POD；复制廉价）。
//   * icc 非空 + from_system=true → 系统显示设备 ICC 字节（Windows GetICMProfileW）；
//   * from_system=false → sRGB 回退：icc 为空（变换时用 lcms2 内建 sRGB），fallback_reason
//     非空（如实记录原因，进日志/诊断；不静默）。
// name = 描述名（ICC description / "sRGB"），UI 信息行与日志用。
struct DisplayProfile {
    std::string icc;
    std::string name;
    bool from_system = false;
    std::string fallback_reason;

    bool is_fallback() const { return !from_system; }
};

// 纯函数：sRGB 回退 profile（无系统 profile 时的唯一回退口径）。
DisplayProfile make_srgb_display_fallback(std::string reason);

// 系统显示 profile。
//   * Windows：GetDC(NULL) + GetICMProfileW 取**主显示器** ICC，读文件并用 lcms2 校验
//     （可解析、RGB 色彩空间、3 通道）；任何失败 → sRGB 回退 + fallback_reason。
//   * Linux / macOS：不引入 colord / ColorSync 依赖（CI 风险），直接 sRGB 回退并登记原因
//     （如实：显示 profile 未接入，回退 sRGB；登记进报告「未决风险」）。
//   * 进程内缓存（互斥保护）；invalidate_display_profile_cache() 后下次调用重新获取。
//   * 诊断/取证开关（仅用于修复前后 A/B 截图；正常运行不设置）：
//       PP_DISPLAY_ICC=<path>    → 强制以该 ICC 文件作为显示 profile（from_system=true）
//       PP_DISPLAY_SRGB_FALLBACK=1 → 强制 sRGB 回退
//       PP_DISPLAY_ICC_DELAY_MS=<ms> → 仅诊断：在获取路径上做等长 sleep，模拟「部分 Windows
//         版本/环境下 GetICMProfileW / 读 ICC 文件长时间阻塞」的用户环境（上限 60s）。
//         用于机械化验证「GUI 线程绝不同步等待系统显示 profile」：设置 5000 时窗口必须照常
//         显示/响应，后台完成后才出现 display profile 切换日志。**不设置 = 行为不变**。
//   * 本函数是**阻塞调用**（系统调用 + 文件 I/O + lcms2 解析）：GUI 线程不得在启动期同步调用
//     （0.4.2 用户反馈「部分 Windows 打开后转圈无响应」的防御性根治，见 preview_panel.cpp 的
//     异步获取）；只能在后台上/已运行后的低频诊断路径调用。
DisplayProfile system_display_profile();
void invalidate_display_profile_cache();

// 系统显示 ICC 文件的读取上限（防御：损坏/异常系统配置可能指向超大文件；ICC 正常为
// 0.3–4 MB 量级，8 MB 已覆盖 DisplayP3/HDR 类 profile。超限 = 读取失败 → sRGB 回退 + 日志，
// 绝不把异常大文件整读进内存）。
constexpr std::uintmax_t kDisplayIccMaxBytes = 8ull * 1024 * 1024;

// ICC 文件读取（带 8 MB 上限；公开以便单测覆盖「正常/超限/缺失」三态与上限判据）：
//   * ok=true  → bytes = 文件字节；
//   * ok=false → error = 如实原因（stat 失败/非普通文件/超限/读取错误），进日志用。
// 只读常规文件且先 stat 后读（TOCTOU 二次校验）；不会整读超过 kDisplayIccMaxBytes 的文件。
struct DisplayIccFile {
    bool ok = false;
    std::string bytes;
    std::string error;
};
DisplayIccFile read_display_icc_file(const std::filesystem::path &path);

// —— 异步获取状态机（纯函数；GUI 侧 QTimer 轮询与单测共用同一判据，无时钟/无副作用）——
//   输入：done = 后台结果已写入共享状态；result_generation = 该结果所属请求代次；
//         current_generation = 面板当前代次；waited_ms = 自发起起的已等待毫秒；
//         timeout_ms = 超时门限（<=0 = 不设超时）。
//   返回值语义：
//     Ready   → done 且代次匹配 → 应用到显示面（**迟到结果也是 Ready**：GUI 线程从不等待，
//               迟到只意味着晚一点从 sRGB 切到正确编码，不会回退/二次跳变）；
//     Timeout → 未 done 且 waited_ms >= timeout_ms → 调用方记日志并**保持当前 profile**
//               （启动期即 sRGB）。纯函数不规定调用方后续策略：GUI 侧实现为「软超时」——
//               继续在硬上限内观察迟到结果（超时日志只报一次），期间显示面一直是 sRGB；
//     Stale   → done 但代次过期（屏幕变更/新一轮请求已接管）→ 丢弃，不应用；
//     Pending → 后台仍在获取且未超时 → 继续轮询。
//   优先级：done 优先于超时（结果已到就应用）；代次失配只在 done 后判定。
enum class DisplayProfileAsyncState : std::uint8_t { Pending, Ready, Stale, Timeout };

DisplayProfileAsyncState classify_display_profile_async(bool done, std::uint64_t result_generation,
                                                        std::uint64_t current_generation,
                                                        long long waited_ms, long long timeout_ms);

// UI 显示 profile 口径（预览面板与文件列表缩略图共用，保证两处同源）：
//   * 离屏平台（QGuiApplication::platformName() == "offscreen"；--ui-smoke / CI / 截图口径）
//     无物理显示器 → 强制 sRGB 回退，使显示面像素路径与既有版本逐字节一致（12 张冻结截图
//     / 冻结末行零变化由构造保证，不靠断言宽容）；
//   * 真实平台 → system_display_profile()（进程缓存；屏幕变更时先 invalidate 再取）。
// 需要已构造 QGuiApplication（UI 线程/预览 worker 调用均安全）。
DisplayProfile ui_display_profile();

// to_display_encoded() 的诊断结果（失败不抛异常：调用方回退原图，预览不中断）。
struct DisplayTransformResult {
    bool applied = false;  // true = 像素真的做了变换（源码 ≠ 显示编码）
    bool identity = false; // true = 源与显示同为 sRGB（假定），像素逐字节不变
    std::string error;     // 非空 = 失败原因（此时返回的原 QImage 未变换）
    std::string src_name;  // 源描述名（"ICC(Display P3)"/"assumed sRGB"）
    std::string dst_name;  // 显示描述名（"ICC(...)"/"sRGB"）
};

// 把已解码的 8bit RGBA 源编码像素变换到显示 profile 编码（相对比色 + 黑点补偿，intent/flags 与
// ColorManager 的档位 0 同源），返回新 QImage（Format_RGBA8888，尺寸/alpha 不变；本管线预览的
// alpha 恒 255）并挂显示 profile 的 QColorSpace（Qt 不解析该 ICC 时仅不挂，不影响像素）。
//   * src_icc 空 → 源按「假定 sRGB」处理（与引擎 M1 口径一致）；
//   * 源 ICC 不可解析 / 通道数与 RGB 不符 → 降级为假定 sRGB（错误进 result.error 之外的降级记录：
//     降级不是失败，像素仍经「假定 sRGB → 显示」变换）；
//   * 显示 profile 为 sRGB 回退 且 源为假定 sRGB → 恒等快路径（像素逐字节不变，只挂 sRGB）。
// 线程安全：句柄池互斥租借；大图按行带并行、每线程独立 lcms2 句柄（线程模型见 .cpp 说明）。
// 性能面：形参按值收（source），调用方 rvalue 传入（如 `to_display_encoded(std::move(image), …)`）
// 时零深拷贝、原地变换；lvalue 传入时靠 Qt 隐式共享的 COW（语义不变，多一次 detach 拷贝）。
QImage to_display_encoded(QImage source, const std::string &src_icc, const DisplayProfile &display,
                          DisplayTransformResult *result = nullptr);

} // namespace pp
