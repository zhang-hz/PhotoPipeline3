// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — file list model + delegate (M1b-U4)
//
// 规格落地（docs/m1b-tasks.md §2.8 + §3.1）：
//   - add_paths：文件与/或目录；目录递归自走；支持扩展名 = pp::input_extensions()
//     大小写不敏感；点文件/点目录跳过；不支持的普通文件只计数不入列（§2.8 v1.1：
//     按 lexically_normal 路径去重，重复 add 不重复累计；clear() 同时归零集合与计数）；
//     lexically_normal 去重；新增行入缩略图队列（endInsertRows 之后行号才有效）
//   - M2-T16b：每个 add_paths 批次内按路径字典序排序（跨批次仍为追加语义）
//   - StateTextRole 文本与徽标颜色共用本 TU 的 §3.1 十二态表（唯一来源）
//   - apply_thumb stale-guard：row 越界或 path 不匹配 → 丢弃（删行/清空后的在途结果）
//   - entries()：FileEntry 值语义深拷贝（src/base_dir/exception）供 Scheduler
#include "ui/filelistmodel.h"
#include "ui/thumbnails.h" // 冻结的 filelistmodel.h 依赖 Thumbnailer 已声明（不自带该头）

#include <QAbstractItemView>
#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QImage>
#include <QModelIndex>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionViewItem>
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

namespace pp::ui {
namespace {

// ---- §3.1 FileState 12 态映射（文本 + 颜色）唯一表；顺序 = pp::FileState 枚举顺序 ----
struct StateStyle {
    const char *text;
    const char *color;
};
constexpr StateStyle kStateStyle[] = {
    {"排队", "#808080"}, // Queued
    {"探测", "#0088cc"}, // Probing
    {"解码", "#0088cc"}, // Decoding
    {"旋转", "#0088cc"}, // Orienting
    {"色彩", "#0088cc"}, // Coloring
    {"合成", "#0088cc"}, // Flattening
    {"编码", "#0088cc"}, // Encoding
    {"写入", "#0088cc"}, // Writing
    {"完成", "#22aa22"}, // Done
    {"跳过", "#999999"}, // Skipped
    {"失败", "#dd3333"}, // Failed
    {"取消", "#999999"}, // Cancelled
};
static_assert(std::size(kStateStyle) == 12, "§3.1 十二态映射必须齐全");

const StateStyle &state_style(pp::FileState st) {
    const std::size_t i = static_cast<std::size_t>(st);
    return kStateStyle[i < std::size(kStateStyle) ? i : 0];
}

QString state_text(pp::FileState st) { return QString::fromUtf8(state_style(st).text); }
QColor state_color(pp::FileState st) { return QColor(QLatin1String(state_style(st).color)); }

constexpr int kRowHeight = 56;                     // §2.8 行高
constexpr int kThumbBox = 48;                      // §2.8 缩略图 48×48
constexpr const char *kExceptionColor = "#dd8800"; // §3.1 例外徽标 ⚑
constexpr const char *kSubLineColor = "#808080";   // 第二行灰字

std::filesystem::path fs_path(const QString &s) {
    return std::filesystem::path(s.toStdString()).lexically_normal();
}
QString fs_string(const std::filesystem::path &p) { return QString::fromStdString(p.string()); }

// 目录自走/去重/stale-guard 全部走同一规范化形式（lexically_normal 字符串）
QString norm_string(const QString &s) { return fs_string(fs_path(s)); }

// 扩展名白名单（大小写不敏感，pp::input_extensions() 的项不含点）
bool is_supported_ext(const std::filesystem::path &p) {
    std::string ext = p.extension().string();
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    const std::vector<std::string> &exts = pp::input_extensions();
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

// 单个输入项 → 候选文件（目录递归；点文件/点目录跳过；不支持者按去重路径计数，v1.1）
void collect_from(const std::filesystem::path &input, std::vector<std::filesystem::path> &files,
                  std::set<QString> &unsupported_seen, std::size_t &unsupported) {
    // 不支持文件：仅首次遇到（自上次 clear 以来）才计数（§2.8 v1.1 裁定）
    const auto count_unsupported = [&](const std::filesystem::path &p) {
        if (unsupported_seen.insert(fs_string(p.lexically_normal())).second)
            ++unsupported;
    };
    std::error_code ec;
    if (std::filesystem::is_directory(input, ec)) {
        std::filesystem::recursive_directory_iterator it(
            input, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec)
            return; // 目录打不开（权限/竞态）→ 忽略该输入项
        const std::filesystem::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec)
                break; // 目录项枚举失败 → 停止该子树
            const std::filesystem::path &p = it->path();
            const std::string name = p.filename().string();
            if (!name.empty() && name.front() == '.') { // 点文件/点目录
                std::error_code dir_ec;
                if (it->is_directory(dir_ec))
                    it.disable_recursion_pending();
                continue;
            }
            std::error_code type_ec;
            if (!it->is_regular_file(type_ec))
                continue;
            if (is_supported_ext(p)) {
                files.push_back(p.lexically_normal());
            } else {
                count_unsupported(p); // 不支持：计数但不入列
            }
        }
        return;
    }
    if (std::filesystem::is_regular_file(input, ec)) {
        if (is_supported_ext(input)) {
            files.push_back(input.lexically_normal());
        } else {
            count_unsupported(input);
        }
    }
    // 不存在/非普通文件 → 忽略（冻结接口无错误通道；unsupported_count 只统计普通文件）
}

} // namespace

// ---------------------------------------------------------------- FileListModel

FileListModel::FileListModel(QObject *parent) : QAbstractListModel(parent) {}

int FileListModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(rows_.size());
}

QVariant FileListModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() < 0 || std::size_t(idx.row()) >= rows_.size())
        return {};
    const FileRow &r = rows_[std::size_t(idx.row())];
    switch (role) {
    case ThumbRole:
        return QVariant::fromValue(r.thumb);
    case NameRole:
        return fs_string(r.entry.src.filename());
    case PathRole:
        return fs_string(r.entry.src);
    case DimRole:
        return r.probe_ok ? QStringLiteral("%1×%2").arg(r.info.width).arg(r.info.height)
                          : QString();
    case FormatRole:
        return r.probe_ok ? QString::fromStdString(r.info.format).toUpper() : QString();
    case StateRole:
        return int(r.state);
    case StateTextRole:
        return state_text(r.state);
    case ExceptionRole:
        return r.has_exception();
    case ErrorRole:
        return r.error;
    default:
        return {};
    }
}

Qt::ItemFlags FileListModel::flags(const QModelIndex &idx) const {
    if (!idx.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void FileListModel::add_paths(const QStringList &paths) {
    if (paths.isEmpty())
        return;

    std::vector<std::filesystem::path> files;
    std::size_t unsupported = 0;
    for (const QString &p : paths) {
        if (p.isEmpty())
            continue;
        collect_from(fs_path(p), files, unsupported_paths_, unsupported);
    }

    // M2-T16b：批次内按路径字典序排序后再追加 —— UI 行序不再依赖 readdir 顺序（可预测，并使
    // 01-meta.png 等截图 / T9 回归基线确定化）。跨批次仍是追加语义：多次 add 的先后顺序保留。
    std::sort(files.begin(), files.end(),
              [](const std::filesystem::path &a, const std::filesystem::path &b) {
                  return a.string() < b.string();
              });

    // 去重：已入列（known_paths_）与本批次内重复都跳过
    std::vector<std::filesystem::path> fresh;
    std::set<QString> batch_keys;
    for (const std::filesystem::path &f : files) {
        const QString key = fs_string(f);
        if (known_paths_.contains(key) || !batch_keys.insert(key).second)
            continue;
        fresh.push_back(f);
    }

    const std::size_t first_new = rows_.size();
    if (!fresh.empty()) {
        beginInsertRows(QModelIndex(), int(first_new), int(first_new + fresh.size() - 1));
        rows_.reserve(rows_.size() + fresh.size());
        for (const std::filesystem::path &f : fresh) {
            FileRow row;
            row.entry.src = f;
            row.entry.base_dir = f.parent_path(); // §2.8：base_dir = src.parent
            known_paths_.insert(fs_string(f));
            rows_.push_back(std::move(row));
        }
        endInsertRows();
    }
    unsupported_ += unsupported;

    // 新增行 → 缩略图入队（此时行号已生效）
    if (thumbs_) {
        for (std::size_t i = first_new; i < rows_.size(); ++i) {
            thumbs_->request(int(i), fs_string(rows_[i].entry.src));
        }
    }
    if (!fresh.empty() || unsupported != 0)
        emit content_changed();
}

void FileListModel::remove_rows(const QList<int> &rows) {
    if (rows.isEmpty() || rows_.empty())
        return;

    std::vector<int> idx;
    idx.reserve(std::size_t(rows.size()));
    for (int r : rows) {
        if (r >= 0 && std::size_t(r) < rows_.size())
            idx.push_back(r);
    }
    if (idx.empty())
        return;
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());

    // 从大到小删除，避免行号漂移（任意输入顺序都正确）
    for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
        const int r = *it;
        beginRemoveRows(QModelIndex(), r, r);
        known_paths_.erase(fs_string(rows_[std::size_t(r)].entry.src));
        rows_.erase(rows_.begin() + r);
        endRemoveRows();
    }
    // 在途/排队结果由 apply_thumb stale-guard（行号漂移 → path 不匹配）丢弃；
    // 行号下移后重新入队仍缺探测信息的行（否则被删行之后的行会永久失去缩略图）
    enqueue_pending_thumbs();
    emit content_changed();
}

void FileListModel::clear() {
    if (thumbs_)
        thumbs_->clear_pending(); // 在途结果 → stale（apply_thumb 亦按越界丢弃）
    const bool changed = !rows_.empty() || unsupported_ != 0;
    beginResetModel();
    rows_.clear();
    known_paths_.clear();
    unsupported_paths_.clear(); // v1.1：去重集合与计数一起归零
    unsupported_ = 0;
    endResetModel();
    if (changed)
        emit content_changed();
}

std::size_t FileListModel::size() const { return rows_.size(); }
bool FileListModel::empty() const { return rows_.empty(); }

const FileRow &FileListModel::row(std::size_t i) const {
    static const FileRow kEmpty; // 越界返回空行（内部不变量：调用方先查 size()）
    Q_ASSERT(i < rows_.size());
    return i < rows_.size() ? rows_[i] : kEmpty;
}

std::size_t FileListModel::unsupported_count() const { return unsupported_; }

std::size_t FileListModel::exception_count() const {
    return std::size_t(std::count_if(rows_.begin(), rows_.end(),
                                     [](const FileRow &r) { return r.has_exception(); }));
}

std::vector<pp::FileEntry> FileListModel::entries() const {
    std::vector<pp::FileEntry> out;
    out.reserve(rows_.size());
    for (const FileRow &r : rows_)
        out.push_back(r.entry); // 值语义深拷贝（含 exception）
    return out;
}

void FileListModel::set_state(std::size_t i, pp::FileState st) {
    if (i >= rows_.size() || rows_[i].state == st)
        return;
    rows_[i].state = st;
    const QModelIndex idx = index(int(i));
    emit dataChanged(idx, idx, {StateRole, StateTextRole});
    emit content_changed(); // 各态计数属"摘要相关"
}

void FileListModel::set_exception(std::size_t i, std::optional<pp::MetadataOverride> ex) {
    if (i >= rows_.size())
        return;
    FileRow &r = rows_[i];
    const bool had = r.has_exception();
    r.entry.exception = std::move(ex);
    if (had == r.has_exception() && !had)
        return; // 无变化（都为空）
    const QModelIndex idx = index(int(i));
    emit dataChanged(idx, idx, {ExceptionRole});
    emit exception_changed(i);
}

void FileListModel::apply_thumb(int row, const QString &path, const pp::ImageInfo &info,
                                bool probe_ok, const QString &error, const QImage &thumb) {
    if (row < 0 || std::size_t(row) >= rows_.size())
        return; // stale：行越界（删行/清空）
    FileRow &r = rows_[std::size_t(row)];
    if (norm_string(path) != fs_string(r.entry.src))
        return; // stale：path 不匹配（行漂移）

    r.info = info;
    r.probe_ok = probe_ok;
    r.thumb = thumb;
    const bool was_failed = (r.state == pp::FileState::Failed);
    if (probe_ok) {
        r.error.clear();
    } else {
        r.error = error.isEmpty() ? tr("探测失败") : tr("探测失败：%1").arg(error);
        r.state = pp::FileState::Failed; // 探测失败 = 该行错误态（thumbs.h：error 非空）
    }
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx,
                     {ThumbRole, DimRole, FormatRole, StateRole, StateTextRole, ErrorRole});
    if (!probe_ok && !was_failed)
        emit content_changed();
}

void FileListModel::set_thumbnailer(Thumbnailer *t) {
    if (thumbs_ == t)
        return;
    if (thumbs_)
        disconnect(thumbs_, nullptr, this, nullptr);
    thumbs_ = t;
    if (!thumbs_)
        return;
    // 结果回填：ready 已在 GUI 线程发出（Thumbnailer 队列投递）→ 直接连接
    connect(thumbs_, &Thumbnailer::ready, this, &FileListModel::apply_thumb);
    enqueue_pending_thumbs();
}

void FileListModel::enqueue_pending_thumbs() {
    if (!thumbs_)
        return;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const FileRow &r = rows_[i];
        if (r.probe_ok || !r.error.isEmpty())
            continue; // 已有结果 / 已判失败
        thumbs_->request(int(i), fs_string(r.entry.src));
    }
}

// ------------------------------------------------------------------ FileDelegate

void FileDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // 背景/选中/焦点框交给样式；文字与图标自绘（清空避免默认绘制）
    const QWidget *const widget = opt.widget;
    QStyle *const style = widget ? widget->style() : QApplication::style();
    opt.text.clear();
    opt.icon = QIcon();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

    QRect area = opt.rect.adjusted(6, 4, -6, -4);
    // 事实（U4 实测）：QListView 用"未含竖滚动条"的视口宽度调 sizeHint（§2.8 冻结公式），
    // 竖滚动条出现后 item 比可见视口宽 ~14px → 右对齐徽标会被裁掉。以可见视口右边界收口。
    if (const QAbstractItemView *view = qobject_cast<const QAbstractItemView *>(widget)) {
        const int visible_right =
            view->horizontalScrollBar()->value() + view->viewport()->width() - 1;
        if (area.right() > visible_right)
            area.setRight(visible_right);
    }
    if (area.width() <= 0 || area.height() <= 0)
        return;

    const bool selected = opt.state & QStyle::State_Selected;
    const QColor primary = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    const QColor secondary = selected ? opt.palette.color(QPalette::HighlightedText)
                                      : QColor(QLatin1String(kSubLineColor));

    // 左：48×48 缩略图（等比缩放居中；无缩略图 → 空白占位框）
    const QRect thumb_box(area.left(), area.top() + (area.height() - kThumbBox) / 2, kThumbBox,
                          kThumbBox);
    const QImage thumb = index.data(FileListModel::ThumbRole).value<QImage>();
    painter->save();
    if (!thumb.isNull()) {
        const QImage scaled =
            thumb.scaled(kThumbBox, kThumbBox, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QRect target(thumb_box.left() + (kThumbBox - scaled.width()) / 2,
                           thumb_box.top() + (kThumbBox - scaled.height()) / 2, scaled.width(),
                           scaled.height());
        painter->drawImage(target, scaled);
    } else {
        painter->setPen(QPen(opt.palette.color(QPalette::Mid)));
        painter->drawRect(thumb_box.adjusted(0, 0, -1, -1));
    }
    painter->restore();

    // 右：状态徽标（§3.1 颜色，右对齐），例外时其左侧 "⚑"（#dd8800）
    const QString state_label = index.data(FileListModel::StateTextRole).toString();
    const int state_int = index.data(FileListModel::StateRole).toInt();
    const bool has_exception = index.data(FileListModel::ExceptionRole).toBool();
    QFont badge_font = opt.font;
    badge_font.setBold(true);
    const QFontMetrics badge_fm(badge_font);
    const QString flag = QString::fromUtf8("⚑");
    const int badge_w = badge_fm.horizontalAdvance(state_label);
    const int flag_adv = has_exception ? badge_fm.horizontalAdvance(flag) : 0;
    const int badge_left = area.right() - badge_w;
    const int flag_left = badge_left - (has_exception ? flag_adv + 6 : 0);

    painter->save();
    painter->setFont(badge_font);
    painter->setPen(state_color(pp::FileState(state_int)));
    painter->drawText(QRect(badge_left, area.top(), badge_w, area.height()),
                      Qt::AlignRight | Qt::AlignVCenter, state_label);
    if (has_exception) {
        painter->setPen(QColor(QLatin1String(kExceptionColor)));
        painter->drawText(QRect(flag_left, area.top(), flag_adv, area.height()),
                          Qt::AlignRight | Qt::AlignVCenter, flag);
    }
    painter->restore();

    // 中：第一行文件名（粗体），第二行 "1920×1080 · JPEG"（灰）；
    // 探测失败且无尺寸信息时第二行显示 ErrorRole 文本（失败色），避免只看到徽标无原因
    const QString name = index.data(FileListModel::NameRole).toString();
    QString sub = index.data(FileListModel::DimRole).toString();
    const QString format = index.data(FileListModel::FormatRole).toString();
    if (!format.isEmpty())
        sub = sub.isEmpty() ? format : sub + QStringLiteral(" · ") + format;
    bool sub_is_error = false;
    if (sub.isEmpty()) {
        const QString error = index.data(FileListModel::ErrorRole).toString();
        if (!error.isEmpty()) {
            sub = error;
            sub_is_error = true;
        }
    }

    const int text_left = thumb_box.right() + 10;
    const int text_width = flag_left - 8 - text_left;
    if (text_width > 0) {
        QFont name_font = opt.font;
        name_font.setBold(true);
        const QFontMetrics name_fm(name_font);
        const QFontMetrics sub_fm(opt.font);
        const int line_h = area.height() / 2;
        painter->save();
        painter->setFont(name_font);
        painter->setPen(primary);
        painter->drawText(QRect(text_left, area.top(), text_width, line_h),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          name_fm.elidedText(name, Qt::ElideMiddle, text_width));
        if (!sub.isEmpty()) {
            painter->setFont(opt.font);
            painter->setPen(sub_is_error ? state_color(pp::FileState::Failed) : secondary);
            painter->drawText(
                QRect(text_left, area.top() + line_h, text_width, area.height() - line_h),
                Qt::AlignLeft | Qt::AlignVCenter,
                sub_fm.elidedText(sub, Qt::ElideRight, text_width));
        }
        painter->restore();
    }
}

QSize FileDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(index);
    return QSize(option.rect.width(), kRowHeight); // §2.8：行高 56
}

} // namespace pp::ui
