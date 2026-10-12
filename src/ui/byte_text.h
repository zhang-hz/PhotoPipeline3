// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 字节显示事实单源（deep-review-0.3.2.md R-254：natural_bytes/human_bytes
// 双实现收敛；R-264：三套字节格式化并存收敛）
//
// 契约（M7-R2.1-F3 起；M7-R2.1-F4 扩 R-264）：同一事实只定义一次 ——
//   * 事实面 = 「字节读数的显示规格」一表三档（各档精度/档位/单位不同是**有意的消费面规格**，
//     不是漂移；格式逐字节不变，探针口径不换锚）：
//       - natural_bytes：1024 进制；B/KB 显示整数（截断）、MB 起 'f',1；单位表
//         B|KB|MB|GB|TB|PB —— 消费点 = mainwindow_status.cpp「剩余磁盘」段（格式被 smoke
//         探针 `^\d+(\.\d)? (B|KB|MB|GB|TB|PB)$` 钉住）与 page_output.cpp 磁盘可用行；
//       - file_bytes（修 3-10 文件字节口径）：0 → "0 B"；<1024 → "%1 B" 整数；<1 MB →
//         "%1 KB" 'f',0；≥1 MB → "%1 MB" 'f',2 —— 消费点 = 运行页行内/任务面字节读数；
//       - peak_mem_bytes（A29 峰值内存）：0 → "—"（未发生的量不显示）；<1 MB → "%1 KB"
//         'f',0；<1 GB → "%1 MB" 'f',1；≥1 GB → "%1 GB" 'f',2。
//   * header-only（R-109 路线，同 format_policy.h）；tests/unit/test_smoke.cpp 钉真值表。
//   * run_status_text.h 的 fmt_bytes/fmt_mem_bytes = file_bytes/peak_mem_bytes 的转发面
//     （runpage 命名空间旧名保留，调用点零改动；R-264 收敛 = 实现唯一）。
#pragma once

#include <QCoreApplication>
#include <QString>

namespace pp::ui::byte_text {

// R-254 单源：B/KB 整数、MB+ 一位小数（原 mainwindow_status 的 natural_bytes 与
// page_output 的 human_bytes 两份逐字实现收敛于此；输出逐字节不变）。
inline QString natural_bytes(qint64 bytes) {
    constexpr double kKiB = 1024.0;
    static const char *const kUnits[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= kKiB && unit < 5) {
        value /= kKiB;
        ++unit;
    }
    return unit <= 1
               ? QStringLiteral("%1 %2").arg(qint64(value)).arg(QLatin1String(kUnits[unit]))
               : QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(QLatin1String(kUnits[unit]));
}

// R-264（M7-R2.1-F4）：字节口径（修 3-10：不做 KB 四舍五入 —— 0 字节就是 0 B，<1 KB 显
// 真实字节）。翻译上下文保持 "PageRun"（与原 run_status_text.h 逐字一致，译文查找面不变）。
inline QString file_bytes(quint64 bytes) {
    if (bytes == 0)
        return QStringLiteral("0 B");
    if (bytes < 1024)
        return QCoreApplication::translate("PageRun", "%1 B").arg(bytes);
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0)
        return QCoreApplication::translate("PageRun", "%1 KB").arg(kb, 0, 'f', 0);
    return QCoreApplication::translate("PageRun", "%1 MB").arg(kb / 1024.0, 0, 'f', 2);
}

// R-264（M7-R2.1-F4）：峰值内存读数（A29）：量级换档 B→KB→MB→GB；0 = 本次无像素预算读数
// → 「—」（§2.5 未发生的量不显示）。翻译上下文保持 "PageRun"。
inline QString peak_mem_bytes(quint64 bytes) {
    if (bytes == 0)
        return QStringLiteral("—");
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0)
        return QCoreApplication::translate("PageRun", "%1 KB").arg(kb, 0, 'f', 0);
    const double mb = kb / 1024.0;
    if (mb < 1024.0)
        return QCoreApplication::translate("PageRun", "%1 MB").arg(mb, 0, 'f', 1);
    return QCoreApplication::translate("PageRun", "%1 GB").arg(mb / 1024.0, 0, 'f', 2);
}

} // namespace pp::ui::byte_text
