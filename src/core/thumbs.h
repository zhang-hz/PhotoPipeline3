// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — probe + thumbnail + 输入预览（M1b 冻结面 + 0.3.0/M4-W2-T10 落地）
//
// PP-FROZEN(0.3.0) —— 解冻裁定表 §3.6 行 `core/thumbs.h` **已落地**
//   落地任务 = W2-T10（常驻输入预览：`decode_preview` + ≤2 线程异步池 + LRU 缓存）；
//   原标注 PP-THAWED(0.3.0-M4-D20) 随落地再冻结为本标记（冻结头 SPDX 延续）。
//   裁定原文（本行为逐字抄录；行首 "// " 为注释包装）：
// clang-format off
// | `core/thumbs.h` | 追加 `decode_preview(path, max_px) -> QImage`（预览面板；先内嵌预览后全解码降采样） |
// clang-format on
//   §6.1 预览面板（本行细则的唯一来源；逐字摘录，行首 "// " 为注释包装）：
// clang-format off
// - 复用 `thumbs` 通道扩展：`decode_preview(path, 2048)` —— 优先内嵌预览（Exiv2/OIIO，≥64px 且长边 ≥2048 可降采样），
//   否则 OIIO 读整图后 LANCZOS3 降采样；独立低并发（≤2）后台线程池，不与转码抢核；LRU 内存缓存（16 张 2048px 上限）。
// - 交互（D3）：点选/翻图（←/→）即时替换；缩放「适应 | 1:1」；徽标恒显「输入 · 未修改像素」；底部热键提示
//   `1 精选 2 待定 3 废片 0 清除`（热键表随分类注册表动态生成）。
// - 精度：预览**不做色彩管理之外的任何处理**；色彩管理渲染复用 ColorManager（缩略图口径 v1：sRGB 假定，与既有决策一致）。
// clang-format on
//   本文件既有声明（ThumbImage / ThumbOutcome / make_thumbnail）不在本表行内 → 维持
//   PP-FROZEN(M1b)，实现与行为逐字不变（本任务零改动那三条路径）。
//
//   落地处置（M4-T1 上报条目，本任务裁定面）：
//     * 原注「本文件文件头现声明 core layer, no Qt，而 §3.6 的返回类型为 QImage」——**按冻结形态
//       落地**：Qt(Gui) 类型经本文件的追加面进入 core（`#include <QImage>`），pp_core 的链接面
//       追加 `Qt6::Gui`（既有的 `Qt6::Core` 是同一处置的先例）。落地面**只限本文件的 0.3.0 追加块**
//       （preview 三个声明 + PreviewCache）：`make_thumbnail` 通道保持零 Qt、零改动。
//     * 线程池（≤2 线程 / 取消过期请求）与渲染（ColorManager/徽标）不落在 core：core 只出
//       **纯解码 + LRU**（可单测、无 QObject、无线程状态），异步池与 UI 归 `ui/preview_panel.*`。
//     * 「先内嵌预览后全解码降采样」的实现口径：内嵌预览门槛沿用既有 64px
//       （kPreviewEmbeddedMinLongEdge，与 make_thumbnail 的 kMinPreviewLongEdge 同值同义）。
//       内嵌预览长边 ≥ max_px 时 LANCZOS3 精确降采样到 max_px；短于 max_px 时按其原生尺寸
//       呈现（**不放大**，与 §2.1 step 5 同口径）。两分支都不做色彩变换：像素即源文件像素
//       （徽标「输入 · 未修改像素」的字面语义），色彩空间按「缩略图口径 v1：sRGB 假定」
//       呈现描述名，不做 ICC→sRGB 变换。
#pragma once
#include "core/types.h"
#include <QImage>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace pp {

struct ThumbImage {
    int width = 0, height = 0;  // returned size (longest edge <= target)
    std::vector<uint8_t> rgba;  // width*height*4, 8-bit, sRGB-assumed, alpha on white
    bool from_embedded = false; // true = embedded preview used
};

struct ThumbOutcome {
    pp::ImageInfo info; // probe result (meaningful when probe_ok)
    bool probe_ok = false;
    std::string error;    // non-empty = probe failure (row error state)
    pp::ThumbImage thumb; // empty = thumbnail unavailable (probe still ok)
};

// Thread-safe, stateless: probe + thumbnail in one call.
//  1) probe_file(src): failure -> {probe_ok=false, error}
//  2) embedded preview: Exiv2 PreviewManager, largest preview with long edge >= 64;
//     decode its bytes via OIIO (memory reader), resample to target
//  3) no/failed preview -> full float32 decode + resample to target
//  4) thumb failure with probe_ok -> thumb empty, error stays empty
//  5) never upscale (scale = min(target/w, target/h, 1.0)); gray -> RGB; alpha -> white
ThumbOutcome make_thumbnail(const std::filesystem::path &src, int target_long_edge);

// ============================================================================================
// PP-FROZEN(0.3.0) §3.6 · 输入预览（0.3.0 追加块；追加在文件末尾，既有声明一字不动）
// ============================================================================================

// §6.1「≥64px」：内嵌预览可用性门槛。与实现侧既有的 kMinPreviewLongEdge 同值同义（唯一的
// 门槛来源；thumbnails 与 preview 两个通道共用同一数字，防止两条路径漂移）。
inline constexpr int kPreviewEmbeddedMinLongEdge = 64;

// §6.1「LRU 内存缓存（16 张 2048px 上限）」的两个上限（单源；UI 侧不另写字面量）。
inline constexpr std::size_t kPreviewCacheCapacity = 16;
inline constexpr int kPreviewCacheMaxPx = 2048;

// `decode_preview()` 的完整结果（= 一次解码的全部可显示/可诊断信息）。返回类型是 QImage 的
// 冻结形态只给像素，故文件名/尺寸/位深/色彩空间这些**面板要显示的字面信息**由本结构承载
// （§6.1「文件名/尺寸/位深/色彩空间随图显示」）。
struct PreviewImage {
    QImage image;               // null = 失败或不可用（错误在 error）
    pp::ImageInfo info;         // probe 摘要（宽高/位深/通道/源格式/ICC 有无）
    std::string color_space;    // 源色彩空间描述名（ICC 描述 / "sRGB 假定"）；不做变换
    bool from_embedded = false; // true = 走内嵌预览分支
    std::string error;          // 非空 = 失败（probe/解码/通道数不支持的英文详情）
};

// PP-FROZEN(0.3.0) §3.6 冻结形态：`decode_preview(path, max_px) -> QImage`
//   path : 源文件；max_px : 长边上限（§6.1 的调用口径 = 2048，见 kPreviewCacheMaxPx）。
//   成功 → 8bit RGBA8888（白底合成，sRGB 假定）；失败 → null QImage（不抛异常）。
//   线程安全、无状态（可被 ≤2 线程的预览池并发调用）。
QImage decode_preview(const std::filesystem::path &src, int max_px);

// 同一次解码的完整结果（`decode_preview` = 本函数的 .image；面板与单测取证用）。
//   * 先内嵌预览（Exiv2 PreviewManager 的最大候选，长边 ≥ kPreviewEmbeddedMinLongEdge），
//     经 OIIO 内存读为 float32 → LANCZOS3 缩放到 max_px 内；
//   * 否则 OIIO 读整图（ImageCache 分块、单线程）→ LANCZOS3 缩放到 max_px 内；
//   * 一律不放大、不做色彩变换、失败不抛异常（error 非空 + image null）。
PreviewImage decode_preview_detailed(const std::filesystem::path &src, int max_px);

// §6.1「LRU 内存缓存（16 张 2048px 上限）」：按源路径缓存的最近使用池，
// **逐出最旧（LRU）**；容量与 max_px 由构造参数给定（默认 = §6.1 的 16 / 2048）。
//   * get() 命中 → 提升为最近使用并返回；未命中 → 解码（decode_preview_detailed）+ 插入；
//   * 插入后条目数 > capacity → 逐出最近最少使用的条目（evictions 计数）；
//   * 线程安全（内部互斥；解码在锁外进行，故 2 个预览 worker 可并发 miss）；
//   * 缓存的是 8bit RGBA（QImage 隐式共享，拷贝廉价）；内存口径 = Σ image.sizeInBytes()。
class PreviewCache {
public:
    explicit PreviewCache(std::size_t capacity = kPreviewCacheCapacity,
                          int max_px = kPreviewCacheMaxPx);
    ~PreviewCache();
    PreviewCache(const PreviewCache &) = delete;
    PreviewCache &operator=(const PreviewCache &) = delete;

    // 命中/未命中都在本调用内返回可用结果（未命中即同步解码）；error 非空的失败结果
    // **不入缓存**（避免把"文件暂时读不了"钉死）。
    PreviewImage get(const std::filesystem::path &src);

    std::size_t size() const;         // 当前条目数
    std::size_t capacity() const;     // 上限（条目数）
    int max_px() const;               // 长边上限（透传给解码）
    std::size_t hits() const;         // 命中次数
    std::size_t misses() const;       // 未命中次数（含解码失败）
    std::size_t evictions() const;    // 逐出次数（LRU）
    std::size_t memory_bytes() const; // Σ image.sizeInBytes()
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pp
