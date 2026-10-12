// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页跟随项 probe（B.1 拆分步 B4：meta_probe）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（page_meta 拆分蓝图，步序 B4）
//   * ProbeSnapshot = 跟随项 probe 的**值语义快照**（后台线程产出；不触碰 GUI）；
//   * scan_probes = **唯一** IO 落点（R-181：UI 层内嵌 IO 收口到本单元 —— page_meta /
//     各卡 TU 不再直调 pp::read_metadata / QtConcurrent）。
#pragma once

#include "core/metadata.h"

#include <QString>
#include <vector>

namespace pp::ui::meta {

struct MetaState; // 完整定义见 meta_state.h（本头被 meta_state.h 与 meta_probe.cpp 消费）

// 跟随项的 probe 快照（后台线程产出；只含值语义，不触碰 GUI）
struct ProbeSnapshot {
    QString path;
    pp::SourceMeta meta;
    bool ok = false;
};

// 只读扫描跟随项（纯 IO；QtConcurrent 后台执行，GUI 线程零等待）
std::vector<ProbeSnapshot> scan_probes(const QStringList &paths);

// 世代守卫回填（过期结果一律丢弃、不得乱序回填 —— M2-T7 口径）
void apply_probe_result(MetaState *st, quint64 generation,
                        const std::vector<ProbeSnapshot> &probes);

// 发起一次跟随项扫描（GUI 线程只做值拷贝 + 挂 watcher，立即返回）
void request_probe_scan(MetaState *st);

} // namespace pp::ui::meta
