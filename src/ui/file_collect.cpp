// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 纯收集 + 后台扫盘（file_collect）（R-200：filelistmodel 一 TU 6
// 类按所有权拆分产物；M7-R2.1-F3）
//
// 拆分口径 = **行为零变化**：类/函数体自 filelistmodel.cpp 逐字搬入（共享纯件入
// ui/filelistmodel_util.h 单源）；objectName / 信号 / 公开面逐位不变；
// 冻结头 filelistmodel.h 逐字未动。拆分步序见 build/m7r2-e2/handoff.md ⑥-2。
#include "ui/filelistmodel.h"

#include "ui/filelistmodel_util.h"
#include "ui/icons.h"
#include "ui/thumbnails.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QMetaObject>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QRunnable>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QThreadPool>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

#include "core/fsops.h"
#include "ui/theme.h"

namespace pp::ui {

// 单个输入项 → 候选文件（目录递归；点文件/点目录跳过；不支持者按去重路径计数，v1.1）。
// R-148：枚举/读取失败的输入项**计数**（原实现静默忽略，用户不知道漏了什么）——目录打不开 /
// 枚举中断 / 输入项不存在或不可读，各计 1 项「未能读取」。
// M7-R2-GATE（tidy bugprone-easily-swappable-parameters）：unsupported/unreadable = 两个语义
// 各别的出参计数（不支持 vs 未能读取），唯一调用点（本文件 scan_inputs）实参名逐一对应；
// 形参序按语义分组保持，不为消音而重排。
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
void collect_from(const std::filesystem::path &input, std::vector<std::filesystem::path> &files,
                  std::set<QString> &unsupported_seen, std::size_t &unsupported,
                  std::size_t &unreadable) {
    // 不支持文件：仅首次遇到（自上次 clear 以来）才计数（§2.8 v1.1 裁定）
    const auto count_unsupported = [&](const std::filesystem::path &p) {
        if (unsupported_seen.insert(fs_string(p.lexically_normal())).second)
            ++unsupported;
    };
    std::error_code ec;
    if (std::filesystem::is_directory(input, ec)) {
        std::filesystem::recursive_directory_iterator it(
            input, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) {
            ++unreadable; // R-148：目录打不开（权限/竞态）→ 计入「未能读取」
            return;
        }
        const std::filesystem::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) {
                ++unreadable; // R-148：枚举中断（该子树的剩余项读不到）
                break;
            }
            const std::filesystem::path &p = it->path();
            const std::string name = p.filename().string();
            if (!name.empty() && name.front() == '.') { // 点文件/点目录
                std::error_code dir_ec;
                if (it->is_directory(dir_ec))
                    it.disable_recursion_pending();
                continue;
            }
            std::error_code type_ec;
            if (!it->is_regular_file(type_ec)) {
                if (type_ec)
                    ++unreadable; // R-148：条目 stat 失败（被删/无权限）
                continue;
            }
            if (is_supported_ext(p)) {
                files.push_back(p.lexically_normal());
            } else {
                count_unsupported(p); // 不支持：计数但不入列
            }
        }
        return;
    }
    std::error_code file_ec;
    if (std::filesystem::is_regular_file(input, file_ec)) {
        if (is_supported_ext(input)) {
            files.push_back(input.lexically_normal());
        } else {
            count_unsupported(input);
        }
        return;
    }
    // R-148：不存在/非普通文件/不可读 → 计入「未能读取」（原为静默忽略）
    ++unreadable;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

ScanBatch scan_inputs(const QStringList &paths) {
    ScanBatch batch;
    std::vector<std::filesystem::path> found;
    std::set<QString> unsupported_seen;
    std::size_t unsupported = 0;
    for (const QString &p : paths) {
        if (p.isEmpty())
            continue;
        collect_from(fs_path(p), found, unsupported_seen, unsupported, batch.unreadable);
    }
    // M2-T16b：批次内按路径字典序排序后再追加 —— UI 行序不再依赖 readdir 顺序。
    std::sort(found.begin(), found.end(),
              [](const std::filesystem::path &a, const std::filesystem::path &b) {
                  return a.string() < b.string();
              });
    batch.files.reserve(found.size());
    for (const std::filesystem::path &f : found) {
        ScannedPath item;
        item.path = fs_string(f);
        item.dedup_key = dedup_key(f); // R-149：别名归一（worker 内做，GUI 零 IO）
        std::error_code size_ec;
        const std::uintmax_t bytes = std::filesystem::file_size(f, size_ec);
        item.size_bytes = size_ec ? -1 : static_cast<qint64>(bytes); // M5-T5 行内「大小」
        batch.files.push_back(std::move(item));
    }
    batch.unsupported_keys.assign(unsupported_seen.begin(), unsupported_seen.end());
    return batch;
}

// R-025：后台扫盘任务（串行走 scan_pool_，与 EXIF 摘要同池 —— 都是只读 IO，不与缩略图/转码
// 抢核）。结果经 Qt 队列投递回 GUI 线程（context = 模型自身，模型析构则事件自动作废）。
class AddPathsRunnable : public QRunnable {
public:
    AddPathsRunnable(FileListModel *model, QStringList paths)
        : model_(model), paths_(std::move(paths)) {
        setAutoDelete(true);
    }
    void run() override {
        ScanBatch batch = scan_inputs(paths_);
        FileListModel *const model = model_;
        QMetaObject::invokeMethod(
            model,
            [model, batch] {
                model->apply_scanned_paths(batch.files, batch.unsupported_keys, batch.unreadable);
            },
            Qt::QueuedConnection);
    }

private:
    FileListModel *model_ = nullptr;
    QStringList paths_;
};

// R-200：跨 TU 衔接工厂（util 头声明；实现即本文件 Runnable）
QRunnable *make_add_paths_task(FileListModel *model, QStringList paths) {
    return new AddPathsRunnable(model, std::move(paths));
}
} // namespace pp::ui
