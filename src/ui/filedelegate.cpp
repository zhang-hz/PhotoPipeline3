// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 行委托（FileDelegate）（R-200：filelistmodel 一 TU 6
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

// ------------------------------------------------------------------ FileDelegate

void FileDelegate::set_tokens(const theme::Tokens &t) { tokens_ = t; }

QFont FileDelegate::name_font() {
    return theme::font(theme::Typography::card_title_px, QFont::DemiBold); // §7.5 文件名 12/600
}

QFont FileDelegate::info_font() {
    return theme::font(theme::Typography::hint_px); // §4.2 辅助 10/400（裁定 A3）
}

QColor FileDelegate::selection_fill(const theme::Tokens &t) { return t.accent_dim; }
QColor FileDelegate::selection_bar(const theme::Tokens &t) { return t.accent; }

QString FileDelegate::join_items(const QStringList &items, const QFontMetrics &fm, int avail_w) {
    QString out;
    for (const QString &item : items) {
        if (item.isEmpty())
            continue;
        const int item_w = fm.horizontalAdvance(item);
        if (out.isEmpty()) {
            // 首项就放不下 → 本行留空：宁可少一行信息，也不用省略号吞掉尺寸/大小（§7.5 逐字）
            if (item_w > avail_w)
                break;
            out = item;
            continue;
        }
        const QString candidate = out + QStringLiteral(" · ") + item;
        if (fm.horizontalAdvance(candidate) > avail_w)
            break; // 放不下 → 减行内项（书写序保持，不截断、不省略号）
        out = candidate;
    }
    return out;
}

// R-276：行几何的**唯一输入口径** —— 视口尾缘收口（竖滚动条出现后 item 比可见视口宽 ~14px，
// 尾对齐胶囊/色点会被裁或错位）。两路绘制（FileDelegate::paint / FileGroupDelegate 色点）
// 一律先经本函数，再进 row_rects（收口规则只写这一份）。
// R-218（M7-R2E2）：收口边随方向镜像 —— LTR 收右缘（竖滚动条在右）/ RTL 收左缘（滚动条在左）；
// LTR 分支逐字节不变（旧式只收右缘）。
QRect FileDelegate::visible_row_rect(const QRect &item_rect, const QAbstractItemView *view) {
    QRect row_rect = item_rect;
    if (view != nullptr) {
        const bool rtl = view->layoutDirection() == Qt::RightToLeft;
        const int visible_right =
            view->horizontalScrollBar()->value() + view->viewport()->width() - 1;
        if (!rtl) {
            if (row_rect.right() > visible_right)
                row_rect.setRight(visible_right);
        } else {
            const int visible_left = view->horizontalScrollBar()->value();
            if (row_rect.left() < visible_left)
                row_rect.setLeft(visible_left);
        }
    }
    return row_rect;
}

FileDelegate::RowRects FileDelegate::row_rects(const QRect &item_rect, int pill_w, bool has_flag) {
    RowRects rects;
    const int thumb_px = theme::Metrics::thumb_px; // R4 缩略图 46（§7.5）
    rects.thumb = QRect(item_rect.left() + kRowInsetL,
                        item_rect.top() + (item_rect.height() - thumb_px) / 2, thumb_px, thumb_px);
    int right = item_rect.left() + item_rect.width() - kRowInsetR; // 右开边界
    const int pill_h = theme::Metrics::pill_h;                     // R4 胶囊高 16
    rects.pill =
        QRect(right - pill_w, item_rect.top() + (item_rect.height() - pill_h) / 2, pill_w, pill_h);
    right = rects.pill.left() - kPillGap;
    if (has_flag) {
        rects.flag = QRect(right - kExceptionMarkPx,
                           item_rect.top() + (item_rect.height() - kExceptionMarkPx) / 2,
                           kExceptionMarkPx, kExceptionMarkPx);
        right = rects.flag.left() - kPillGap;
    }
    const int text_x = rects.thumb.left() + thumb_px + kThumbTextGap;
    rects.text = QRect(text_x, item_rect.top() + kRowInsetY, std::max(0, right - text_x),
                       item_rect.height() - 2 * kRowInsetY);
    return rects;
}

// R-218 / M7-R2E2 加性重载：rtl=false 与三参版逐值相同；rtl=true 四矩形绕 item_rect 中线镜像
// （逻辑坐标算好再单点镜像；ui_smoke 的 rtl-mirror 探针钉死镜像关系）。
FileDelegate::RowRects FileDelegate::row_rects(const QRect &item_rect, int pill_w, bool has_flag,
                                               bool rtl) {
    RowRects rects = row_rects(item_rect, pill_w, has_flag);
    if (rtl) {
        rects.thumb = mirror_x(rects.thumb, item_rect);
        rects.pill = mirror_x(rects.pill, item_rect);
        rects.text = mirror_x(rects.text, item_rect);
        if (!rects.flag.isEmpty())
            rects.flag = mirror_x(rects.flag, item_rect);
    }
    return rects;
}

void FileDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    const QWidget *const widget = opt.widget;
    QStyle *const style = widget ? widget->style() : QApplication::style();
    const bool selected = (opt.state & QStyle::State_Selected) != 0;
    const theme::Tokens &t = tokens_;

    // 行背景：悬停/交替底交给样式；**选中底由本委托自绘**（修 3-17 的高亮强化）→ 先屏蔽样式
    // 的选中填充，否则样式蓝底会盖住 accent-dim 淡底。
    {
        QStyleOptionViewItem bg_opt = opt;
        bg_opt.state &= ~QStyle::State_Selected;
        bg_opt.text.clear();
        bg_opt.icon = QIcon();
        // GUI-R2：勾选框一律自绘（FileGroupDelegate 的勾选列），样式指示器在**任何列**都不得
        // 出现 —— 否则内容列左缘会多画一枚原生勾选框（"两排选择框"）。
        bg_opt.features &= ~QStyleOptionViewItem::HasCheckIndicator;
        style->drawControl(QStyle::CE_ItemViewItem, &bg_opt, painter, widget);
    }

    // 事实（U4 实测）：QListView/QTreeView 用"未含竖滚动条"的视口宽度调 sizeHint，竖滚动条出现
    // 后 item 比可见视口宽 ~14px → 右对齐胶囊会被裁掉。收口规则单源 = visible_row_rect（R-276）。
    const QRect row_rect =
        visible_row_rect(opt.rect, qobject_cast<const QAbstractItemView *>(widget));
    if (row_rect.width() <= 0 || row_rect.height() <= 0)
        return;

    // 选中面（修 3-17）：整行淡强调底 + 左缘 2px 强调条（行首由列 0 承担，见 FileGroupDelegate）
    if (selected)
        painter->fillRect(row_rect, selection_fill(t));

    const QString state_label = index.data(FileListModel::StateTextRole).toString();
    const bool has_exception = index.data(FileListModel::ExceptionRole).toBool();
    // R-218：几何走逻辑坐标 + 单点镜像（RTL 时缩略图/信息区/胶囊/例外标记整体镜像）
    const bool rtl = rtl_of(opt);
    const RowRects rects = row_rects(row_rect, state_pill_width(state_label), has_exception, rtl);

    // 左：46×46 缩略图（r4 圆角；无缩略图 → --ctl 占位块，mockup .thumb{background:var(--ctl)}）
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QPainterPath thumb_clip;
    thumb_clip.addRoundedRect(QRectF(rects.thumb), theme::Metrics::thumb_radius,
                              theme::Metrics::thumb_radius);
    painter->setClipPath(thumb_clip);
    painter->fillRect(rects.thumb, t.control);
    const QImage thumb = index.data(FileListModel::ThumbRole).value<QImage>();
    if (!thumb.isNull()) {
        const QImage scaled = thumb.scaled(theme::Metrics::thumb_px, theme::Metrics::thumb_px,
                                           Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QRect target(rects.thumb.left() + (rects.thumb.width() - scaled.width()) / 2,
                           rects.thumb.top() + (rects.thumb.height() - scaled.height()) / 2,
                           scaled.width(), scaled.height());
        painter->drawImage(target, scaled);
    }
    painter->restore();

    // 中：三行信息 —— 文件名（12/600）+「尺寸 · 大小 · 格式」+「日期 · 机型」（10px，--txt3）。
    // 信息行**禁止截断**：放不下减行内项（join_items），绝不产出省略号；失败行的原因在
    // 第三行（无 EXIF 信息时）以 err 色兜底，否则由右侧"失败"胶囊表达。
    const QString name = index.data(FileListModel::NameRole).toString();
    QStringList meta1;
    const QString dims = index.data(FileListModel::DimRole).toString();
    const QString size = index.data(FileListModel::SizeRole).toString();
    const QString format = index.data(FileListModel::FormatRole).toString();
    if (!dims.isEmpty())
        meta1 << dims;
    if (!size.isEmpty())
        meta1 << size;
    if (!format.isEmpty())
        meta1 << format;
    QStringList meta2;
    const QString date = index.data(FileListModel::ShotDateRole).toString();
    const QString camera = index.data(FileListModel::CameraRole).toString();
    if (!date.isEmpty())
        meta2 << date;
    if (!camera.isEmpty())
        meta2 << camera;
    const QString error = index.data(FileListModel::ErrorRole).toString();
    bool line3_is_error = false;
    if (meta2.isEmpty() && !error.isEmpty()) {
        meta2 << error; // 失败行的原因（行内唯一可见处；行内文案与 §7.5 的字段优先级无关）
        line3_is_error = true;
    }

    const QFont name_draw_font = name_font();
    const QFont info_draw_font = info_font();
    const QFontMetrics name_fm(name_draw_font);
    const QFontMetrics info_fm(info_draw_font);
    const QString line2 = join_items(meta1, info_fm, rects.text.width());
    const QString line3 = join_items(meta2, info_fm, rects.text.width());
    if (rects.text.width() > 0) {
        const int name_h = name_fm.height();
        const int info_h = info_fm.height();
        const int block = name_h + (line2.isEmpty() ? 0 : info_h) + (line3.isEmpty() ? 0 : info_h);
        int y = rects.text.top() + std::max(0, (rects.text.height() - block) / 2);
        painter->save();
        painter->setFont(name_draw_font);
        painter->setPen(t.text);
        painter->drawText(QRect(rects.text.left(), y, rects.text.width(), name_h),
                          text_align(rtl) | Qt::AlignVCenter,
                          name_fm.elidedText(name, Qt::ElideMiddle, rects.text.width()));
        y += name_h;
        painter->setFont(info_draw_font);
        painter->setPen(t.text3);
        if (!line2.isEmpty()) {
            painter->drawText(QRect(rects.text.left(), y, rects.text.width(), info_h),
                              text_align(rtl) | Qt::AlignVCenter, line2);
            y += info_h;
        }
        if (!line3.isEmpty()) {
            painter->setPen(line3_is_error ? t.err : t.text3);
            painter->drawText(QRect(rects.text.left(), y, rects.text.width(), info_h),
                              text_align(rtl) | Qt::AlignVCenter, line3);
        }
        painter->restore();
    }

    // 例外标记（矢量 tag 图标；§3.1 例外 = 警告档色 —— A6：原 #dd8800 收口为令牌 warn 档）
    if (has_exception && !rects.flag.isEmpty()) {
        painter->save();
        icons::paint(*painter, icons::Name::Tag, QRectF(rects.flag),
                     theme::pill_paint(t, kExceptionTone).fg);
        painter->restore();
    }

    // 右：状态胶囊（R4 16 高 r3 9.5/700；色 = theme::pill_paint 单源 —— §8 四档语义）
    // R-069：色调改由模型出（StateToneRole）—— 0 字节产物事实（行级）才能把"已完成"
    // 改判成琥珀；原实现按 FileState 枚举自算，行级事实进不来。
    const QByteArray tone = index.data(FileListModel::StateToneRole).toString().toLatin1();
    draw_state_pill(painter, rects.pill, t, tone.constData(), state_label);
}

QSize FileDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(index);
    return QSize(option.rect.width(), kFileRowHeight); // §7.5：行高 58
}
} // namespace pp::ui
