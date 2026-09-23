// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — probe + thumbnail (M1b frozen; UI-facing, core layer, no Qt)
//
// PP-THAWED(0.3.0-M4-D20) —— 解冻裁定表 §3.6 行 `core/thumbs.h`（依据 docs/v0.3.0-design.md §3.6）
//   裁定原文（本行为逐字抄录；行首 "// " 为注释包装）：
// clang-format off
// | `core/thumbs.h` | 追加 `decode_preview(path, max_px) -> QImage`（预览面板；先内嵌预览后全解码降采样） |
// clang-format on
//   落地任务 = W2-T10（预览面板）→ 落地后改标 PP-FROZEN(0.3.0)。
//   本文件既有声明（ThumbImage / ThumbOutcome / make_thumbnail）不在本表行内 → 维持 PP-FROZEN(M1b)。
#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "core/types.h"

namespace pp {

struct ThumbImage {
    int width = 0, height = 0;       // returned size (longest edge <= target)
    std::vector<uint8_t> rgba;       // width*height*4, 8-bit, sRGB-assumed, alpha on white
    bool from_embedded = false;      // true = embedded preview used
};

struct ThumbOutcome {
    pp::ImageInfo info;              // probe result (meaningful when probe_ok)
    bool probe_ok = false;
    std::string error;               // non-empty = probe failure (row error state)
    pp::ThumbImage thumb;            // empty = thumbnail unavailable (probe still ok)
};

// Thread-safe, stateless: probe + thumbnail in one call.
//  1) probe_file(src): failure -> {probe_ok=false, error}
//  2) embedded preview: Exiv2 PreviewManager, largest preview with long edge >= 64;
//     decode its bytes via OIIO (memory reader), resample to target
//  3) no/failed preview -> full float32 decode + resample to target
//  4) thumb failure with probe_ok -> thumb empty, error stays empty
//  5) never upscale (scale = min(target/w, target/h, 1.0)); gray -> RGB; alpha -> white
ThumbOutcome make_thumbnail(const std::filesystem::path& src, int target_long_edge);

// PP-THAWED(0.3.0-M4-D20) §3.6 · decode_preview（0.3.0 追加；追加在文件末尾）
//   0.3.0 冻结形态（设计 §3.6 逐字抄录）：`decode_preview(path, max_px) -> QImage`
//   （预览面板；先内嵌预览后全解码降采样）。
//   细则（§6.1 逐字摘录，供 T10 实现）：优先内嵌预览（Exiv2/OIIO，≥64px 且长边 ≥2048 可降采样），
//   否则 OIIO 读整图后 LANCZOS3 降采样；独立低并发（≤2）后台线程池，不与转码抢核；
//   LRU 内存缓存（16 张 2048px 上限）。精度：预览不做色彩管理之外的任何处理。
//   注（落地待裁定，T10 处置）：本文件文件头现声明 "core layer, no Qt"，而 §3.6 的返回类型为
//   `QImage` —— Qt 类型是否进入 core（或改由 ui/ 层包一层）需主对话裁定（M4-T1 已上报，W5 收口入 m4-report；
//   本任务零改动、未新增声明）。
// —— 0.3.0 追加声明位置（T10 落地）——

}  // namespace pp
