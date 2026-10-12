// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 分组代理（FileGroupProxyModel）（R-200：filelistmodel 一 TU 6
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

// ============================================================================================
// M4-T11 · 节名格式化（键 → 显示名；mockup .ghead 文案）
// ============================================================================================

QString group_label(pp::GroupMode mode, const QString &key) {
    if (key.isEmpty())
        return QString::fromUtf8(pp::kNoGroupLabel.data(), int(pp::kNoGroupLabel.size()));
    switch (mode) {
    case pp::GroupMode::Month: {
        // "YYYY-MM" → "2024 年 1 月"（mockup .ghead："2024 年 5 月"）；形态不符 → 原样直出
        if (key.size() == 7 && key.at(4) == QLatin1Char('-')) {
            bool ok_year = false;
            bool ok_month = false;
            const int year = key.left(4).toInt(&ok_year);
            const int month = key.mid(5, 2).toInt(&ok_month);
            if (ok_year && ok_month && month >= 1 && month <= 12)
                return QStringLiteral("%1 年 %2 月").arg(year).arg(month);
        }
        return key;
    }
    case pp::GroupMode::Camera:
        return key;
    case pp::GroupMode::SourceFormat:
        return key.toUpper(); // 与行内 FormatRole（"JPEG"）同口径
    case pp::GroupMode::None:
        return key;
    }
    return key;
}

// ============================================================================================

// ============================================================================================

FileGroupProxyModel::FileGroupProxyModel(QObject *parent) : QAbstractProxyModel(parent) {
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(
        kRebuildDebounceMs); // 摘要/键逐行落定 → 去抖合并为一次重建（R-280 命名）
    connect(debounce_, &QTimer::timeout, this, [this] { rebuild(); });
}

void FileGroupProxyModel::setSourceModel(QAbstractItemModel *source) {
    if (sourceModel() != nullptr)
        disconnect(sourceModel(), nullptr, this, nullptr);
    QAbstractProxyModel::setSourceModel(source);
    if (source == nullptr) {
        rebuild();
        return;
    }
    connect(source, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { rebuild(); });
    connect(source, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &, int, int) { rebuild(); });
    connect(source, &QAbstractItemModel::modelReset, this, [this] { rebuild(); });
    connect(source, &QAbstractItemModel::layoutChanged, this, [this] { rebuild(); });
    connect(source, &QAbstractItemModel::dataChanged, this,
            &FileGroupProxyModel::on_source_data_changed);
    // 摘要批次就绪（源自根 FileListModel；本代理的 source 可能是过滤代理 → 沿 sourceModel 链
    // 走到根模型，避免对中间层的类型假设）
    QAbstractItemModel *root = source;
    while (auto *upstream = qobject_cast<QAbstractProxyModel *>(root))
        root = upstream->sourceModel();
    if (auto *flm = qobject_cast<FileListModel *>(root)) {
        connect(flm, &FileListModel::group_source_changed, this,
                &FileGroupProxyModel::schedule_rebuild);
    }
    rebuild();
}

void FileGroupProxyModel::set_group_mode(pp::GroupMode mode) {
    if (mode_ == mode)
        return;
    mode_ = mode;
    rebuild(); // 只关系节名文案；键由源模型给出
}

pp::GroupMode FileGroupProxyModel::group_mode() const { return mode_; }

void FileGroupProxyModel::schedule_rebuild() {
    if (debounce_ != nullptr)
        debounce_->start();
}

void FileGroupProxyModel::on_source_data_changed(const QModelIndex &top_left,
                                                 const QModelIndex &bottom_right,
                                                 const QVector<int> &roles) {
    // 文件行数据变化 → 原样转发给视图（勾选/缩略图/状态都靠它刷新）。
    // R-203（M7-R2E2）：代理是两列结构（勾选列 + 行内容列），转发不得只出列 0 —— 此前只发
    // mapFromSource 的单格，列 1 靠视图整行重绘续命（deep-review R-203/E6-08）。现按行发
    // [列 0, 列 1] 的 dataChanged 区间（行数/roles 语义逐字不变）。
    if (sourceModel() == nullptr)
        return;
    for (int r = top_left.row(); r <= bottom_right.row(); ++r) {
        const QModelIndex src = sourceModel()->index(r, 0);
        const QModelIndex proxy = mapFromSource(src);
        if (proxy.isValid()) {
            const QModelIndex last = index(proxy.row(), kColumnCount - 1, proxy.parent());
            emit dataChanged(proxy, last.isValid() ? last : proxy, roles);
        }
    }
    // 分组键变化 → 结构可能要变（去抖合并；不做逐行重建）
    for (int role : roles) {
        if (role == FileListModel::GroupKeyRole) {
            schedule_rebuild();
            break;
        }
    }
}

void FileGroupProxyModel::rebuild() {
    QAbstractItemModel *src = sourceModel();
    const int n = src != nullptr ? src->rowCount() : 0;
    beginResetModel();
    groups_.clear();
    row_group_.assign(std::size_t(n), -1);
    row_pos_.assign(std::size_t(n), -1);
    if (src != nullptr) {
        QHash<QString, int> by_key;
        for (int r = 0; r < n; ++r) {
            const QModelIndex si = src->index(r, 0);
            const QString key = si.data(FileListModel::GroupKeyRole).toString();
            auto it = by_key.find(key);
            int g = 0;
            if (it == by_key.end()) {
                groups_.push_back(Group{key, group_label(mode_, key), {}});
                g = int(groups_.size()) - 1;
                by_key.insert(key, g);
            } else {
                g = it.value();
            }
            groups_[std::size_t(g)].rows.push_back(r);
            row_group_[std::size_t(r)] = g;
            row_pos_[std::size_t(r)] = int(groups_[std::size_t(g)].rows.size()) - 1;
        }
    }
    ++rebuilds_;
    endResetModel();
}

QModelIndex FileGroupProxyModel::index(int row, int column, const QModelIndex &parent) const {
    if (row < 0 || column < 0 || column >= kColumnCount)
        return {};
    if (!parent.isValid()) {
        if (std::size_t(row) >= groups_.size())
            return {};
        return createIndex(row, column, quintptr(0)); // 分组节
    }
    if (parent.internalId() != 0)
        return {}; // 节的子节点不再有子节点
    const int g = parent.row();
    if (g < 0 || std::size_t(g) >= groups_.size())
        return {};
    if (std::size_t(row) >= groups_[std::size_t(g)].rows.size())
        return {};
    return createIndex(row, column, quintptr(g + 1)); // 文件行（父节点 = 节）
}

QModelIndex FileGroupProxyModel::parent(const QModelIndex &child) const {
    if (!child.isValid() || child.internalId() == 0)
        return {};
    const int g = int(child.internalId()) - 1;
    if (g < 0 || std::size_t(g) >= groups_.size())
        return {};
    return createIndex(g, 0, quintptr(0));
}

int FileGroupProxyModel::rowCount(const QModelIndex &parent) const {
    if (!parent.isValid())
        return int(groups_.size());
    if (parent.internalId() != 0)
        return 0;
    const int g = parent.row();
    if (g < 0 || std::size_t(g) >= groups_.size())
        return 0;
    return int(groups_[std::size_t(g)].rows.size());
}

int FileGroupProxyModel::columnCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);
    return kColumnCount;
}

bool FileGroupProxyModel::is_group(const QModelIndex &proxy_index) const {
    return proxy_index.isValid() && proxy_index.internalId() == 0;
}

QVariant FileGroupProxyModel::data(const QModelIndex &proxy_index, int role) const {
    if (!proxy_index.isValid())
        return {};
    if (is_group(proxy_index)) {
        const int g = proxy_index.row();
        if (g < 0 || std::size_t(g) >= groups_.size())
            return {};
        const Group &group = groups_[std::size_t(g)];
        switch (role) {
        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
            return header_text(group.label, int(group.rows.size()));
        case GroupCountRole:
            return int(group.rows.size());
        case FileListModel::GroupKeyRole:
            return group.key;
        default:
            return {};
        }
    }
    const QModelIndex src = mapToSource(proxy_index);
    return src.isValid() ? src.data(role) : QVariant();
}

Qt::ItemFlags FileGroupProxyModel::flags(const QModelIndex &proxy_index) const {
    if (!proxy_index.isValid())
        return Qt::NoItemFlags;
    if (is_group(proxy_index))
        return Qt::ItemIsEnabled; // 节头不可选/不可勾（控件集合与 mockup 一致）
    const QModelIndex src = mapToSource(proxy_index);
    return src.isValid() ? src.flags() : Qt::NoItemFlags;
}

bool FileGroupProxyModel::setData(const QModelIndex &proxy_index, const QVariant &value, int role) {
    if (!proxy_index.isValid() || is_group(proxy_index) || sourceModel() == nullptr)
        return false;
    const QModelIndex src = mapToSource(proxy_index);
    return src.isValid() ? sourceModel()->setData(src, value, role) : false;
}

QModelIndex FileGroupProxyModel::mapToSource(const QModelIndex &proxy_index) const {
    if (!proxy_index.isValid() || sourceModel() == nullptr)
        return {};
    if (is_group(proxy_index))
        return {}; // 节头无源索引
    const int g = int(proxy_index.internalId()) - 1;
    if (g < 0 || std::size_t(g) >= groups_.size())
        return {};
    const std::size_t pos = std::size_t(proxy_index.row());
    const std::vector<int> &rows = groups_[std::size_t(g)].rows;
    if (pos >= rows.size())
        return {};
    return sourceModel()->index(rows[pos], 0); // 源（过滤代理）单列
}

QModelIndex FileGroupProxyModel::mapFromSource(const QModelIndex &source_index) const {
    if (!source_index.isValid())
        return {};
    const int r = source_index.row();
    if (r < 0 || std::size_t(r) >= row_group_.size())
        return {};
    const int g = row_group_[std::size_t(r)];
    if (g < 0)
        return {};
    return createIndex(row_pos_[std::size_t(r)], 0, quintptr(g + 1));
}

int FileGroupProxyModel::group_count() const { return int(groups_.size()); }

QModelIndex FileGroupProxyModel::group_index(int group_row) const {
    return index(group_row, 0, QModelIndex());
}

QString FileGroupProxyModel::group_key_at(int group_row) const {
    if (group_row < 0 || std::size_t(group_row) >= groups_.size())
        return {};
    return groups_[std::size_t(group_row)].key;
}

QString FileGroupProxyModel::group_label_at(int group_row) const {
    if (group_row < 0 || std::size_t(group_row) >= groups_.size())
        return {};
    return groups_[std::size_t(group_row)].label;
}

int FileGroupProxyModel::group_size_at(int group_row) const {
    if (group_row < 0 || std::size_t(group_row) >= groups_.size())
        return 0;
    return int(groups_[std::size_t(group_row)].rows.size());
}

QString FileGroupProxyModel::header_text(const QString &label, int count) {
    return QStringLiteral("%1 · %2").arg(label).arg(count); // mockup .ghead：组名 + "· N"
}

QString FileGroupProxyModel::header_text_at(int group_row) const {
    return header_text(group_label_at(group_row), group_size_at(group_row));
}

int FileGroupProxyModel::rebuilds() const { return rebuilds_; }

void FileGroupProxyModel::set_collapsed(const QString &group_key, bool collapsed) {
    if (collapsed)
        collapsed_.insert(group_key);
    else
        collapsed_.erase(group_key);
}

bool FileGroupProxyModel::is_collapsed(const QString &group_key) const {
    return collapsed_.contains(group_key);
}

// M7-R2D1 / R-278：collapsed_keys() 已随死 API 清理移除（零消费；折叠记忆只有 is_collapsed
// 一个消费面）
} // namespace pp::ui
