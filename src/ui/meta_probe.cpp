// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页跟随项 probe（B.1 拆分步 B4：meta_probe）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（步序 B4）；契约注释见 meta_probe.h。
// R-181（M7-R2.1-F2）：**UI 层内嵌 IO 的唯一落点** —— pp::read_metadata / QtConcurrent
// 只在本 TU 出现（page_meta 与各卡 TU 零 IO）；scan_probes 主体自 page_meta.cpp 逐字迁出。
#include "ui/meta_probe.h"

#include "ui/meta_state.h" // MetaState（FollowModel::items/probes）+ set_hook + kMultiRowCap

#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <utility>
#include <vector>

namespace pp::ui::meta {

std::vector<ProbeSnapshot> scan_probes(const QStringList &paths) {
    std::vector<ProbeSnapshot> out;
    out.reserve(std::size_t(paths.size()));
    for (const QString &path : paths) {
        ProbeSnapshot snapshot;
        snapshot.path = path;
        snapshot.meta = pp::read_metadata(std::filesystem::path(path.toStdString()));
        snapshot.ok = snapshot.meta.error.empty();
        out.push_back(std::move(snapshot));
    }
    return out;
}

ProbeScanner::ProbeScanner(MetaState *st) : QObject(st), st_(st) {}

void ProbeScanner::apply_result(quint64 generation, const std::vector<ProbeSnapshot> &probes) {
    if (generation != generation_) {
        ++stale_dropped_;
        set_hook(st_, "pp_meta_follow_stale_dropped", stale_dropped_);
        return;
    }
    st_->probes = probes;
    pending_ = false;
    set_hook(st_, "pp_meta_follow_pending", false);
    emit changed();
}

// 只对**跟随项**取 probe（≤ 8：多选小表的行数上限；单文件跟随 = 1 次读）。
// GUI 线程只做值拷贝 + 挂 watcher，立即返回；结果回填前校验序号。
void ProbeScanner::request_scan() {
    QStringList paths;
    const int limit = std::min(static_cast<int>(st_->items.size()), kMultiRowCap);
    for (int i = 0; i < limit; ++i) {
        paths << st_->items.at(i).path;
    }
    const quint64 generation = ++generation_;
    st_->probes.clear();
    pending_ = !paths.isEmpty();
    set_hook(st_, "pp_meta_follow_pending", pending_);
    set_hook(st_, "pp_meta_follow_name",
             st_->items.isEmpty() ? QString() : st_->items.first().path);
    set_hook(st_, "pp_meta_follow_count", static_cast<int>(st_->items.size()));
    if (paths.isEmpty()) {
        emit changed();
        return;
    }
    auto *watcher = new QFutureWatcher<std::vector<ProbeSnapshot>>(this);
    QObject::connect(watcher, &QFutureWatcher<std::vector<ProbeSnapshot>>::finished, this,
                     [this, watcher, generation] {
                         const std::vector<ProbeSnapshot> result = watcher->future().result();
                         watcher->deleteLater();
                         apply_result(generation, result);
                     });
    watcher->setFuture(QtConcurrent::run([paths] { return scan_probes(paths); }));
    emit changed(); // 在途：先清空显示（不留上一批的陈旧文本）
}

// 自由函数面保持（meta_state.h 声明；调用点零改动）：转发到 scanner。
void request_probe_scan(MetaState *st) {
    if (st != nullptr && st->probe != nullptr) {
        st->probe->request_scan();
    }
}

} // namespace pp::ui::meta
