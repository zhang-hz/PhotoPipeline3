// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 运行页行模型数据语义（M7-WP9 / 附录 B.2「run_rows_model（无 Qt Widgets）」）。
//
// 契约（docs/deep-review-0.3.2.md 附录 B.2）：进度折算 / 0 字节判定 / 终态映射的**纯数据语义**
// 从 page_run.cpp 抽出为可链接单元（R-109），供 tests/unit/test_smoke.cpp 直接断言。
// 值取自 pp::StageWeights（core/progress.h 单源）；函数体自 page_run.cpp 逐字搬入/等价改写
// （out_completion/run_frac 拆纯核 + 原签名转发，值语义逐字一致）。OutRow/FileRow 控件束仍留
// 在 page_run.cpp（B.2 的 run_page_view 行不在本次拆分面）。
#pragma once

#include <QtGlobal> // qint64

#include <algorithm>

#include "core/pipeline.h" // pp::Stage / pp::FileState
#include "core/progress.h" // pp::StageWeights（折算权重单源）

namespace pp::ui::runpage {

// ===========================================================================
// 行状态
// ===========================================================================

enum class OutKind { Queued, Active, Done, Failed, Skipped, Cancelled };

bool settled_kind(OutKind k) { return k != OutKind::Queued && k != OutKind::Active; }

inline bool out_zero_bytes(OutKind kind, qint64 bytes) {
    // 0 字节完成的输出 = **异常**（§8/§7.3；修 3-10）：条/标记/胶囊一律琥珀，绝不落绿色成功态
    return kind == OutKind::Done && bytes <= 0;
}

// §7.1 运行级：Σ 输出完成度 / 总输出数；终态输出按已结算计 1.0
// 输出行完成度（§7.1）：已结算 → 1.0；进行中 = (encode·enc + metawrite·meta)/Σ 权重，clamp[0,1]
inline double out_completion(bool settled, float enc, float meta) {
    if (settled)
        return 1.0;
    const double own = (pp::StageWeights::encode * enc + pp::StageWeights::metawrite * meta) /
                       (pp::StageWeights::encode + pp::StageWeights::metawrite);
    return std::clamp(own, 0.0, 1.0);
}

// 运行级完成度（§7.1）：Σ 输出完成度 / (文件数 × 格式数)；空批 → 0；clamp[0,1]
inline double run_frac(int file_count, int formats, double completion_sum) {
    const double total = static_cast<double>(file_count) * static_cast<double>(formats);
    if (total <= 0.0)
        return 0.0;
    return std::clamp(completion_sum / total, 0.0, 1.0);
}
} // namespace pp::ui::runpage
