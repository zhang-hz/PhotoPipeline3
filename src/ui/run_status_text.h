// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 运行页状态/数值文案（M7-WP9 / 附录 B.2「run_status_text（纯函数）」）。
//
// 契约（docs/deep-review-0.3.2.md 附录 B.2）：阶段文案与数值格式族 = **纯函数**，从
// page_run.cpp 抽出为无 Qt Widgets 依赖的可链接单元（R-109：tests 可直接单测，不再被
// 「UI 逻辑类不进可链接库」阻断）。函数体自 page_run.cpp 逐字搬入；翻译走
// QCoreApplication::translate("PageRun", …) —— 与原 PageRun::tr 的上下文名同名，译文
// 查找面逐字一致（行为零变化）。
#pragma once

#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
#include <QtGlobal>

#include <algorithm>

#include "core/pipeline.h" // pp::Stage / pp::FileState
#include "ui/byte_text.h"  // R-264：字节显示事实单源（fmt_bytes/fmt_mem_bytes 转发）

namespace pp::ui::runpage {

// 阶段文字（§3.4 词汇表 = 解码/色彩/编码+行数/写入/完成/失败；探测/旋转/合成取自十二态表）
QString shared_stage_text(Stage s) {
    switch (s) {
    case Stage::Probe:
        return QCoreApplication::translate("PageRun", "探测");
    case Stage::Decode:
        return QCoreApplication::translate("PageRun",
                                           "解码 → 色彩"); // mockup：共享段的前后串联提示
    case Stage::Orient:
        return QCoreApplication::translate("PageRun", "旋转");
    case Stage::Color:
        return QCoreApplication::translate("PageRun", "色彩");
    default:
        return QCoreApplication::translate("PageRun", "处理中");
    }
}

QString write_stage_text(Stage s) {
    return s == Stage::Flatten ? QCoreApplication::translate("PageRun", "合成")
                               : QCoreApplication::translate("PageRun", "写入");
}

// ===========================================================================
// 数值/杂项
// ===========================================================================

// 总耗时：< 60 s → "9.8 s"；≥ 60 s → "mm:ss"（同一指标两种量纲，长跑不出现三位数秒）
QString fmt_duration(qint64 ms) {
    const qint64 clamped = std::max<qint64>(0, ms);
    if (clamped < 60'000) {
        return QCoreApplication::translate("PageRun", "%1 s")
            .arg(static_cast<double>(clamped) / 1000.0, 0, 'f', 1);
    }
    const qint64 total = clamped / 1000;
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

QString fmt_mb_s(double v) {
    return QCoreApplication::translate("PageRun", "%1 MB/s").arg(v, 0, 'f', 1);
}

// 字节口径（修 3-10：不做 KB 四舍五入 —— 0 字节就是 0 B，<1 KB 显真实字节）
// R-264（M7-R2.1-F4）：实现收敛进 ui/byte_text.h 单源（file_bytes），本处保留 runpage 旧名
// 转发（调用点零改动；输出逐字节不变，格式不换锚）。
QString fmt_bytes(quint64 bytes) { return byte_text::file_bytes(bytes); }

QString fmt_secs(double ms) { return QStringLiteral("%1s").arg(ms / 1000.0, 0, 'f', 1); }

// 峰值内存读数（A29）：量级换档 B→KB→MB→GB；0 = 本次无像素预算读数 → 「—」（§2.5 未发生的量）
// R-264（M7-R2.1-F4）：实现收敛进 ui/byte_text.h 单源（peak_mem_bytes），同上转发。
QString fmt_mem_bytes(quint64 bytes) { return byte_text::peak_mem_bytes(bytes); }

// 行名：只显示文件名（调用方可能传完整路径）
QString row_name(const QString &name) {
    const QString base = QFileInfo(name).fileName();
    return base.isEmpty() ? name : base;
}
bool is_terminal_state(pp::FileState s) {
    return s == pp::FileState::Done || s == pp::FileState::Skipped || s == pp::FileState::Failed ||
           s == pp::FileState::Cancelled;
}

// 格式名（mockup .ofmt 口径；未知 id 取大写）
QString format_name(const QString &format_id) {
    if (format_id.isEmpty())
        return QStringLiteral("—");
    if (format_id.compare(QStringLiteral("jpeg"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("JPEG");
    if (format_id.compare(QStringLiteral("webp"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("WebP");
    if (format_id.compare(QStringLiteral("jxl"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("JXL");
    return format_id.toUpper();
}
} // namespace pp::ui::runpage
