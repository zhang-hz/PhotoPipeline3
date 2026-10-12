// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 分组委托 + 树视图（FileGroupDelegate / FileTreeView）（R-200：filelistmodel 一 TU
// 6 类按所有权拆分产物；M7-R2.1-F3）
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
// M4-T11 · FileGroupDelegate（勾选列 + 行内容列 + 分组节头）
// ============================================================================================

FileGroupDelegate::FileGroupDelegate(QObject *parent) : FileDelegate(parent) {}

QRect FileGroupDelegate::check_rect(const QRect &item_rect) {
    const int box = theme::Metrics::checkbox_px; // R4 复选框 13×13（A6：自持 15 副本已废除）
    return QRect(item_rect.left() + kCheckInset, item_rect.top() + (item_rect.height() - box) / 2,
                 box, box);
}

// R-218 / M7-R2E2 加性重载：rtl=false 与单参版逐值相同；rtl=true 镜像到列内另一侧。
QRect FileGroupDelegate::check_rect(const QRect &item_rect, bool rtl) {
    const QRect box = check_rect(item_rect);
    return rtl ? mirror_x(box, item_rect) : box;
}

int FileGroupDelegate::check_column_width() {
    return kCheckInset + theme::Metrics::checkbox_px + kCheckGap;
}

void FileGroupDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const {
    const theme::Tokens &t = tokens();
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    if (index.data(FileGroupProxyModel::GroupCountRole).isValid()) {
        // ---- 分组节头（mockup .ghead：10px/700 + letter-spacing .4 + --txt3；"组名 · N"）----
        const QString text = index.data(Qt::DisplayRole).toString();
        const int count = index.data(FileGroupProxyModel::GroupCountRole).toInt();
        painter->save();
        painter->setClipRect(option.rect);
        if (selected) // 节头不可选（flags 只给 ItemIsEnabled），此处仅为不变量兜底
            painter->fillRect(option.rect, selection_fill(t));
        QFont font = theme::font(theme::Typography::hint_px, 700);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 0.4f);
        painter->setFont(font);
        const QFontMetrics fm(font);
        // R-218：文本锚点走逻辑坐标 + 单点镜像（RTL 时右起）；LTR 路径逐字节不变。
        const bool rtl = rtl_of(option);
        const int left = option.rect.left() + kCheckInset;
        const int baseline_y = option.rect.top() + 7; // .ghead{padding:7px 6px 4px}
        const auto draw_at = [&](int logical_x, const QString &s, const QColor &c) {
            const int w = fm.horizontalAdvance(s);
            const int x =
                rtl ? (option.rect.left() + option.rect.right() - logical_x - w + 1) : logical_x;
            painter->setPen(c);
            painter->drawText(QPoint(x, baseline_y + fm.ascent()), s);
        };
        QColor label_color = t.text3; // = --txt3
        draw_at(left, text, label_color);
        // 计数部分（.cnt{opacity:.8}）：覆盖重绘 " · N"（尾部），色更淡
        if (count >= 0) {
            const QString suffix = FileGroupProxyModel::header_text(QString(), count).mid(1);
            const int cut = std::max(0, int(text.size() - suffix.size()));
            const QString prefix = text.left(cut);
            const int prefix_w = fm.horizontalAdvance(prefix);
            label_color.setAlphaF(float(kGroupCountAlphaPct) / 100.0f);
            draw_at(left + prefix_w, suffix, label_color);
        }
        painter->restore();
        return;
    }
    if (index.column() == FileGroupProxyModel::kCheckColumn) {
        // ---- 勾选列：样式画悬停/交替底（屏蔽其选中填充，选中面由本委托落地）→ 自绘 .cbx ----
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.icon = QIcon();
        opt.features &= ~QStyleOptionViewItem::HasCheckIndicator; // 自绘，不用样式指示器
        opt.state &= ~QStyle::State_Selected;
        QStyle *style = opt.widget != nullptr ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        if (selected)
            paint_selection(painter, option.rect, t, /*first_column=*/true, rtl_of(option));

        // .cbx 13×13 r2（mockup：未勾选 = --ctl-bd 边框 + --card 底；勾选 = accent 填充 + 勾形）
        // R-218：勾选框贴行首（RTL = 镜像到列内另一侧）；命中判定同一条几何（check_rect 重载）
        const QRect box = check_rect(option.rect, rtl_of(option));
        const bool on = index.data(Qt::CheckStateRole).toInt() == int(Qt::Checked);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(on ? t.accent : t.control_bd, 1.0));
        painter->setBrush(on ? QBrush(t.accent) : QBrush(t.card));
        painter->drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5),
                                 theme::Metrics::checkbox_radius, theme::Metrics::checkbox_radius);
        if (on) {
            // M5-T3：勾形 = 矢量路径（icons::check）；色 = acc-on（强调底上的字色，§4.1）
            icons::paint(*painter, icons::Name::Check, QRectF(box), t.on_accent);
        }
        painter->restore();
        return;
    }
    // ---- 行内容列：§7.5 三行布局（FileDelegate::paint）+ 行尾分类色点（mockup .chip-dot）----
    FileDelegate::paint(painter, option, index);
    const QVariant deco = index.data(Qt::DecorationRole);
    if (!deco.isValid())
        return;
    const QColor dot_color = deco.value<QColor>();
    if (!dot_color.isValid())
        return;
    const QString state_label = index.data(FileListModel::StateTextRole).toString();
    const bool has_exception = index.data(FileListModel::ExceptionRole).toBool();
    // R-276：与 FileDelegate::paint 同一条输入口径（视口尾缘收口后的行矩形）—— 原先这里用
    // 未收口的 option.rect，竖滚动条出现后色点与胶囊错位。
    const QRect row_rect = FileDelegate::visible_row_rect(
        option.rect, qobject_cast<const QAbstractItemView *>(option.widget));
    if (row_rect.width() <= 0 || row_rect.height() <= 0)
        return;
    // R-218：色点贴胶囊之前 —— 逻辑坐标算好再单点镜像（空间不足判据同一条，镜像前后等价）
    const bool rtl = rtl_of(option);
    const FileDelegate::RowRects rects =
        row_rects(row_rect, state_pill_width(state_label), has_exception);
    QRect dot(rects.pill.left() - FileDelegate::kPillGap - kChipDotPx,
              row_rect.center().y() - kChipDotPx / 2, kChipDotPx, kChipDotPx);
    if (dot.left() < rects.thumb.right() + 1)
        return; // 空间不足（极窄列）→ 不画，避免压到文件名
    if (rtl)
        dot = mirror_x(dot, row_rect);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(dot_color);
    painter->drawEllipse(dot);
    painter->restore();
}

QSize FileGroupDelegate::sizeHint(const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const {
    if (index.data(FileGroupProxyModel::GroupCountRole).isValid())
        return QSize(option.rect.width(), kGroupHeaderHeight);
    if (index.column() == FileGroupProxyModel::kCheckColumn)
        return QSize(option.rect.width(), kFileRowHeight); // 与内容列同高（§7.5 行高 58）
    return FileDelegate::sizeHint(option, index);
}

bool FileGroupDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                    const QStyleOptionViewItem &option, const QModelIndex &index) {
    // 勾选一律由 FileTreeView 的显式命中处理（见其头注）：本委托**恒不响应** Qt 的自动勾选路径，
    // 避免「样式指示器几何 / 委托 editorEvent / 视图命中」三处各自判定同一枚 .cbox 而重复触发。
    Q_UNUSED(event);
    Q_UNUSED(model);
    Q_UNUSED(option);
    Q_UNUSED(index);
    return false;
}

// ============================================================================================
// M4-T11 · FileTreeView（视图侧两条显式行为）
// ============================================================================================

FileTreeView::FileTreeView(QWidget *parent) : QTreeView(parent) {}

void FileTreeView::mouseReleaseEvent(QMouseEvent *event) {
    if (event != nullptr && event->button() == Qt::LeftButton) {
        const QModelIndex index = indexAt(event->position().toPoint());
        if (index.isValid()) {
            if (index.data(FileGroupProxyModel::GroupCountRole).isValid()) {
                setExpanded(index, !isExpanded(index)); // 节头：单击即展开/折叠
            } else if (index.column() == FileGroupProxyModel::kCheckColumn &&
                       FileGroupDelegate::check_rect(visualRect(index),
                                                     layoutDirection() == Qt::RightToLeft)
                           .contains(event->position().toPoint())) {
                const bool on = index.data(Qt::CheckStateRole).toInt() == int(Qt::Checked);
                if (QAbstractItemModel *m = model()) {
                    m->setData(index, on ? int(Qt::Unchecked) : int(Qt::Checked),
                               Qt::CheckStateRole);
                }
                // 不 return：基类继续处理 current/selection（勾选与选中互不干扰）
            }
        }
    }
    QTreeView::mouseReleaseEvent(event);
}
} // namespace pp::ui
