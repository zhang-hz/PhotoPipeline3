// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 字节自然语读数单源（deep-review-0.3.2.md R-254：natural_bytes/human_bytes
// 双实现收敛）
//
// 契约（M7-R2.1-F3）：同一事实只定义一次 ——
//   * 口径：1024 进制；B/KB 显示整数（截断）、MB 起 'f',1 一位小数；单位表 B|KB|MB|GB|TB|PB；
//   * 消费点：mainwindow_status.cpp 底栏「剩余磁盘」段（格式被 smoke 探针
//     `^\d+(\.\d)? (B|KB|MB|GB|TB|PB)$` 钉住，探针不动）与 page_output.cpp 磁盘可用行
//     （原匿名命名空间 human_bytes —— 与 natural_bytes 逐字同款的第二份实现，R-254 双实现）；
//   * header-only（R-109 路线，同 format_policy.h）；tests/unit/test_smoke.cpp 钉真值表。
// 注意：run_status_text.h 的 fmt_bytes/fmt_mem_bytes 是**另一口径**（修 3-10 字节口径 +
// A29 峰值内存「—」档位），精度/档位/单位表都与本函数不同 —— R-264「三套并存」是另一条
// 评审项（本包任务面只收 R-254 的双实现），此处刻意不与 fmt 族合并。
#pragma once

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

} // namespace pp::ui::byte_text
