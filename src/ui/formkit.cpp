// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M5-T2）— formkit 实现：行构造器 + 自绘控件
//
// 几何全部取自 ui/formkit_layout.h（纯函数/常量，与单测同源）；颜色/字阶全部取自 ui/theme.h
// 令牌（本文件零色值字面量，除"透明"外）。零平台分叉（M3 铁律六）：无 #ifdef、无平台头。
//
// 行构造器落地的 R1–R4：
//   * R1：行容器固定高 28、行内单轴布局 spacing 0（列距/双列间档用**显式 addSpacing 项**，
//     卡内无额外 gap）；
//   * R2：行内 [80 标签][列距 12][轨]；双列行 [80][12][轨][24][80][12][轨] ——
//     x 只由 formkit_layout.h 的常量决定 ⇒ 第二标签 x = second_label_x(轨)，与内容无关
//     （定宽档轨道横向不参与拉伸，富余宽全部留给行尾）；
//   * R3：一个标签 + 一个设置项；单位进 16px 槽、范围进 10px 注释（都在轨内，不占标签位）；
//   * R4：控件盒高/宽按 §5 表（短 88 / 标准 160 / 长 flex 撑满轨）—— 定宽档控件即轨道宽，
//     不存在"拉伸格里放窄控件"的浮空死区。
//
// 自绘控件（Switch/Seg/CheckableCard/Chip/Pill/Progress/IconButton/CheckBox）：
// 令牌副本由页面 set_tokens 刷新；绘制只用 theme.h 的取值助手（pill_paint 等）与本文件的
// 几何计算，无平台分叉、无 QStyle 依赖。

#include "ui/formkit.h"

#include <QAbstractSpinBox>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>

#include "ui/theme.h"

namespace pp::ui::formkit {
namespace {

// 行容器（R1：固定高 28，卡内 pitch 不依赖控件 sizeHint）
QWidget *make_row(QWidget *parent) {
    auto *row = new QWidget(parent);
    row->setObjectName(QStringLiteral("pp-fk-row"));
    row->setFixedHeight(kRowPitch);
    row->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return row;
}

// 行内单轴布局（R2：列距以**显式 spacing 项**落地，不用 QLayout 的均分；R1：行内无纵向 gap）。
// 为什么不用 QGridLayout 的空列凑 24px 双列间档：spacing 与"剩余宽给谁"由 QGridLayout 自行
// 分配，实测会把富余宽摊到轨道列上（第二标签 x 漂移）——显式 addSpacing 让 x 只由常量决定。
QHBoxLayout *row_line(QWidget *row) {
    auto *line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(0);
    return line;
}

// 标签轨（80px 右对齐；字号/字重走 theme::font，颜色走 theme QSS 的 ppFk="label"）
QLabel *track_label(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkLabel));
    label->setFont(theme::font(theme::Typography::small_px));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setFixedWidth(kLabelW);
    label->setWordWrap(false);
    return label;
}

// 单位槽（16px；§4.2 辅助 10px + --txt3）
QLabel *unit_slot(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkUnit));
    label->setFont(theme::font(theme::Typography::hint_px));
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setFixedWidth(kUnitSlotW);
    label->setWordWrap(false);
    return label;
}

// 范围注释（10px + --txt3；"1–10" / "−99…+99"）
QLabel *range_note(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkNote));
    label->setFont(theme::font(theme::Typography::hint_px));
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setWordWrap(false);
    return label;
}

// kv 值（§4.2 正文·值 11.5 + 数值档：tabular + 右对齐）
QLabel *value_label(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkValue));
    label->setFont(theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setWordWrap(false);
    return label;
}

// R2 控件宽：短/标准 = 定值（轨即控件宽）；长 = 撑满轨（由轨的 stretch 分配剩余宽）
void apply_field_width(QWidget *field, FieldWidth width) {
    if (field == nullptr)
        return;
    if (width == FieldWidth::Flex) {
        field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        field->setMinimumWidth(0);
    } else {
        field->setFixedWidth(field_w(width));
    }
}

// 字段轨容器：控件（+ 单位槽 16 + 范围注释 10px）；控件垂直居中，轨宽 = 内容宽（定宽档）
// 或撑满（flex 档）。注释排在同轨内控件右侧 —— R3：提示不占标签位。
QWidget *make_track(QWidget *parent, QWidget *field, FieldWidth width, const QString &unit,
                    const QString &note, QLabel **unit_out, QLabel **note_out) {
    auto *track = new QWidget(parent);
    track->setObjectName(QStringLiteral("pp-fk-track"));
    auto *line = new QHBoxLayout(track);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(kInlineGap);
    const bool stretchy = field != nullptr && width == FieldWidth::Flex;
    if (field != nullptr) {
        apply_field_width(field, width); // R2：短/标准 = 定值，长 = 撑满轨
        line->addWidget(field, stretchy ? 1 : 0, Qt::AlignVCenter);
    }
    if (!unit.isEmpty()) {
        QLabel *slot = unit_slot(track, unit);
        line->addWidget(slot, 0, Qt::AlignVCenter);
        if (unit_out != nullptr)
            *unit_out = slot;
    }
    if (!note.isEmpty()) {
        QLabel *label = range_note(track, note);
        line->addWidget(label, 0, Qt::AlignVCenter);
        if (note_out != nullptr)
            *note_out = label;
    }
    if (!stretchy) {
        // 定宽档：轨宽 = 控件宽（R2「控件撑满其轨」），横向不再被拉宽 → 富余宽留给行尾
        track->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        line->addStretch(1);
    } else {
        track->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }
    return track;
}

// spinbox 公共设置（K2 裁定）：ppFk 钩子（盒/箭头列由 theme QSS 落地）+ 数值字体（tabular）+
// 值区右对齐（§4.2 数值档）+ R4 盒高 24 + R2 轨宽三档
void bind_spin_common(QAbstractSpinBox *spin, FieldWidth width, const QString &object_name) {
    spin->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkSpin));
    spin->setFont(theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
    spin->setAlignment(Qt::AlignRight); // §4.2「数值」：右对齐（QSS 无 text-align → C++ 落地）
    spin->setFixedHeight(kControlH);    // R4 高 24
    spin->setFrame(true);
    if (!object_name.isEmpty())
        spin->setObjectName(object_name);
    apply_field_width(spin, width);
}

QString tr_magic() {
    return QCoreApplication::translate("pp::ui::formkit", "自动"); // R4 魔法值文案（§7.4）
}

// 勾选标记（矢量路径：mockup 的 10×10 viewBox「M2 5l2.2 2.2L8 3」等比投到盒内；§3.3 禁字形图标）
void draw_check_mark(QPainter &p, const QRectF &box, const QColor &color) {
    const double side = std::min(box.width(), box.height()) * 0.8;
    const double scale = side / 10.0;
    const QPointF origin(box.center().x() - side / 2.0, box.center().y() - side / 2.0);
    const auto at = [&](double x, double y) {
        return QPointF(origin.x() + x * scale, origin.y() + y * scale);
    };
    QPainterPath path(at(2.0, 5.0));
    path.lineTo(at(4.2, 7.2));
    path.lineTo(at(8.0, 3.0));
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

// 13×13 r2 勾选框底（CheckBox 与 CheckableCard 共用；选中 = acc 填充 + 白勾）
void draw_check_box(QPainter &p, const QRectF &box, const theme::Tokens &t, bool checked,
                    bool dim) {
    const double radius = kCheckRadius;
    if (checked) {
        p.setPen(Qt::NoPen);
        p.setBrush(dim ? theme::at_opacity(t.accent, t.window, theme::Metrics::icon_dim_opacity)
                       : t.accent);
        p.drawRoundedRect(box, radius, radius);
        draw_check_mark(
            p, box,
            dim ? theme::at_opacity(t.on_accent, t.window, theme::Metrics::icon_dim_opacity)
                : t.on_accent);
    } else {
        p.setPen(QPen(dim ? theme::at_opacity(theme::composite(t.control_bd, t.window), t.window,
                                              theme::Metrics::icon_dim_opacity)
                          : t.control_bd,
                      1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// 行构造器
// ---------------------------------------------------------------------------
Row frow(QWidget *parent, const QString &label, QWidget *field, FieldWidth width,
         const QString &unit, const QString &note) {
    Row out;
    QWidget *row = make_row(parent);
    QHBoxLayout *line = row_line(row);
    out.row = row;
    // [80 右对齐标签][列距 12][字段轨]
    out.label = track_label(row, label);
    line->addWidget(out.label);
    line->addSpacing(kColGap);
    QWidget *track = make_track(row, field, width, unit, note, &out.unit, &out.note);
    line->addWidget(track, width == FieldWidth::Flex ? 1 : 0);
    if (width != FieldWidth::Flex)
        line->addStretch(1); // 行尾富余宽，轨道内 x 不被摊动
    return out;
}

DoubleRow double_row(QWidget *parent, const FieldCell &left, const FieldCell &right) {
    DoubleRow out;
    QWidget *row = make_row(parent);
    QHBoxLayout *line = row_line(row);
    out.row = row;
    // [80][12][轨][24][80][12][轨]（R2 逐字；间档用显式 spacing 项 → 第二标签固定 x）
    out.label1 = track_label(row, left.label);
    line->addWidget(out.label1);
    line->addSpacing(kColGap);
    line->addWidget(make_track(row, left.field, left.width, left.unit, QString(), nullptr, nullptr),
                    left.width == FieldWidth::Flex ? 1 : 0);
    line->addSpacing(kDoubleGap);
    out.label2 = track_label(row, right.label);
    line->addWidget(out.label2);
    line->addSpacing(kColGap);
    line->addWidget(
        make_track(row, right.field, right.width, right.unit, QString(), nullptr, nullptr),
        right.width == FieldWidth::Flex ? 1 : 0);
    if (left.width != FieldWidth::Flex && right.width != FieldWidth::Flex)
        line->addStretch(1); // 两栏都定宽：富余宽留行尾（第二标签 x 不被摊动）
    return out;
}

KvRow kv_row(QWidget *parent, const QString &key1, const QString &value1, const QString &key2,
             const QString &value2, FieldWidth width) {
    QLabel *const value1_label = value_label(parent, value1);
    QLabel *const value2_label = value_label(parent, value2);
    FieldCell left;
    left.label = key1;
    left.width = width;
    left.field = value1_label;
    FieldCell right;
    right.label = key2;
    right.width = width;
    right.field = value2_label;
    const DoubleRow row = double_row(parent, left, right);
    KvRow out;
    out.row = row.row;
    out.key1 = row.label1;
    out.key2 = row.label2;
    out.value1 = value1_label;
    out.value2 = value2_label;
    return out;
}

CheckRow check_row(QWidget *parent, const QString &label, const QString &text,
                   const QString &object_name) {
    CheckRow out;
    QWidget *row = make_row(parent);
    QHBoxLayout *line = row_line(row);
    out.row = row;
    // [80 右对齐标签][列距 12][13×13 勾选框 → 文字]（R4 语序固定；文字由勾选框自绘）
    out.label = track_label(row, label);
    line->addWidget(out.label);
    line->addSpacing(kColGap);
    out.box = new CheckBox(row);
    out.box->setText(text);
    if (!object_name.isEmpty())
        out.box->setObjectName(object_name);
    line->addWidget(out.box, 0, Qt::AlignVCenter);
    line->addStretch(1);
    return out;
}

// ---------------------------------------------------------------------------
// 数值控件（K2）
// ---------------------------------------------------------------------------
QSpinBox *spin_box(QWidget *parent, const SpinSpec &spec, FieldWidth width) {
    auto *spin = new QSpinBox(parent);
    bind_spin_common(spin, width, spec.object_name);
    spin->setRange(spec.min, spec.max);
    if (spec.auto_magic)
        spin->setSpecialValueText(tr_magic()); // 范围下端 = 魔法值档（R4）
    spin->setValue(spec.value);
    return spin;
}

QDoubleSpinBox *dspin_box(QWidget *parent, const DoubleSpinSpec &spec, FieldWidth width) {
    auto *spin = new QDoubleSpinBox(parent);
    bind_spin_common(spin, width, spec.object_name);
    spin->setDecimals(spec.decimals);
    spin->setRange(spec.min, spec.max);
    if (spec.auto_magic)
        spin->setSpecialValueText(tr_magic());
    spin->setValue(spec.value);
    return spin;
}

SpinRow spin_row(QWidget *parent, const QString &label, const SpinSpec &spec, FieldWidth width) {
    SpinRow out;
    out.spin = spin_box(parent, spec, width);
    out.row = frow(parent, label, out.spin, width, spec.unit, spec.note);
    return out;
}

// ---------------------------------------------------------------------------
// 开关（R4 30×16 药丸 + 12 钮）：仅"立即生效的布尔"
// ---------------------------------------------------------------------------
SwitchControl::SwitchControl(QWidget *parent) : QAbstractButton(parent) {
    setObjectName(QStringLiteral("pp-fk-switch"));
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(kSwitchW, kSwitchH); // R4 30×16（几何固定：切换/悬停零尺寸变化）
}

void SwitchControl::set_tokens(const theme::Tokens &t) {
    tokens_ = t;
    update();
}

QSize SwitchControl::sizeHint() const { return QSize(kSwitchW, kSwitchH); }

void SwitchControl::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const theme::Tokens &t = tokens_;
    const bool on = isChecked();
    const bool dim = !isEnabled();
    const QRectF track(0, 0, kSwitchW, kSwitchH);
    // 轨/钮双态色全走令牌：开=accent/on_accent，关=switch_rail_off/switch_knob_off（§4.1 表外
    // 的 mockup 值，M5-T2 已登记入令牌层）
    QColor rail = on ? t.accent : t.switch_rail_off;
    QColor knob = on ? t.on_accent : t.switch_knob_off;
    if (dim) {
        rail = theme::at_opacity(rail, t.window, theme::Metrics::icon_dim_opacity);
        knob = theme::at_opacity(knob, t.window, theme::Metrics::icon_dim_opacity);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(rail);
    p.drawRoundedRect(track, kSwitchH / 2.0, kSwitchH / 2.0);
    const double side = kSwitchKnob;
    const double y = (kSwitchH - side) / 2.0;
    const double x = on ? kSwitchW - side - 2.0 : 2.0;
    p.setBrush(knob);
    p.drawEllipse(QRectF(x, y, side, side));
}

// ---------------------------------------------------------------------------
// 分段控件（R4 高 24 r3 段宽 88 等宽；互斥模式唯一形态）
// ---------------------------------------------------------------------------
class SegmentedControl::Segment : public QAbstractButton {
public:
    Segment(SegmentedControl *owner, const QString &text) : QAbstractButton(owner), owner_(owner) {
        setText(text);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setFocusPolicy(Qt::StrongFocus);
        setFixedWidth(kSegW); // R4 段宽 88 等宽 → 选中/悬停零宽度变化
        setFont(theme::font(theme::Typography::body_px, QFont::DemiBold));
    }

    void set_tokens(const theme::Tokens &t) {
        tokens_ = t;
        update();
    }

protected:
    void nextCheckState() override {} // 勾选态由 SegmentedControl 统一持有

    void keyPressEvent(QKeyEvent *event) override {
        switch (event->key()) {
        case Qt::Key_Right:
        case Qt::Key_Down:
            owner_->step(+1);
            event->accept();
            return;
        case Qt::Key_Left:
        case Qt::Key_Up:
            owner_->step(-1);
            event->accept();
            return;
        default:
            break;
        }
        QAbstractButton::keyPressEvent(event);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const theme::Tokens &t = tokens_;
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(t.accent); // R4：选中段填充 acc
            p.drawRoundedRect(box, kSegRadius - 1, kSegRadius - 1);
        } else if (underMouse()) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme::at_opacity(t.control_bd, t.window, 1.0)); // 悬停仅换底，零形变
            p.drawRoundedRect(box, kSegRadius - 1, kSegRadius - 1);
        }
        p.setFont(theme::font(theme::Typography::body_px, QFont::DemiBold));
        p.setPen(isChecked() ? t.on_accent : t.text2);
        p.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    SegmentedControl *owner_;
    theme::Tokens tokens_ = default_tokens();
};

SegmentedControl::SegmentedControl(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("pp-fk-seg"));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedHeight(kSegH); // R4 高 24（容器；段本体 = 24 − 上下各 1px 描边）
    auto *line = new QHBoxLayout(this);
    line->setContentsMargins(1, 1, 1, 1); // 内容盒留出容器 1px 描边
    line->setSpacing(0);
}

void SegmentedControl::add_segment(const QString &text, const QString &object_name) {
    auto *segment = new Segment(this, text);
    if (!object_name.isEmpty())
        segment->setObjectName(object_name);
    segment->set_tokens(tokens_);
    static_cast<QHBoxLayout *>(layout())->addWidget(segment);
    segments_.append(segment);
    const int index = int(segments_.size()) - 1;
    connect(segment, &QAbstractButton::clicked, this, [this, index] { activate(index); });
    if (current_ < 0)
        set_current_index(0); // 首个段默认选中（互斥模式必有当前值）
}

int SegmentedControl::count() const { return int(segments_.size()); }

int SegmentedControl::current_index() const { return current_; }

SegmentedControl::Segment *SegmentedControl::segment_at(int index) const {
    if (index < 0 || index >= segments_.size())
        return nullptr;
    return segments_.at(index);
}

QWidget *SegmentedControl::segment(int index) const { return segment_at(index); }

void SegmentedControl::set_current_index(int index) {
    if (segment_at(index) == nullptr)
        return;
    current_ = index;
    for (int i = 0; i < segments_.size(); ++i)
        segments_.at(i)->setChecked(i == index);
    update();
}

void SegmentedControl::activate(int index) {
    if (index == current_)
        return;
    set_current_index(index);
    emit current_changed(index);
}

void SegmentedControl::step(int delta) {
    if (segments_.isEmpty())
        return;
    const int next = (current_ + delta + segments_.size()) % segments_.size();
    activate(next);
    if (Segment *segment = segment_at(next))
        segment->setFocus(Qt::OtherFocusReason);
}

void SegmentedControl::set_tokens(const theme::Tokens &t) {
    tokens_ = t;
    for (Segment *segment : segments_)
        segment->set_tokens(t);
    update();
}

void SegmentedControl::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const theme::Tokens &t = tokens_;
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(t.control_bd, 1.0)); // mockup .seg{--ctl 底 + --ctl-bd 边 + r3}
    p.setBrush(t.control);
    p.drawRoundedRect(box, kSegRadius, kSegRadius);
}

// ---------------------------------------------------------------------------
// 磁贴卡（多选集合唯一形态；整卡可点 + 预留对勾位）
// ---------------------------------------------------------------------------
CheckableCard::CheckableCard(QWidget *parent) : QAbstractButton(parent) {
    setObjectName(QStringLiteral("pp-fk-tile"));
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    setFixedHeight(kTileH);
}

void CheckableCard::set_labels(const QString &name, const QString &hint) {
    name_ = name;
    hint_ = hint;
    setText(name);
    setToolTip(hint.isEmpty() ? name : name + QLatin1Char(' ') + hint);
    updateGeometry();
    update();
}

void CheckableCard::set_tokens(const theme::Tokens &t) {
    tokens_ = t;
    update();
}

QSize CheckableCard::sizeHint() const {
    const QFontMetrics name_fm(theme::font(theme::Typography::card_title_px, QFont::Bold));
    const QFontMetrics hint_fm(theme::font(theme::Typography::badge_px));
    int w = kTilePadX * 2 + kCheckPx + kTileGap + name_fm.horizontalAdvance(name_);
    if (!hint_.isEmpty())
        w += kTileGap + hint_fm.horizontalAdvance(hint_);
    return QSize(w, kTileH); // 与勾选态无关 → 选中零形变
}

void CheckableCard::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const theme::Tokens &t = tokens_;
    const bool on = isChecked();
    const bool dim = !isEnabled();
    QColor bg = on ? t.accent_dim : QColor(Qt::transparent);
    QColor bd = on ? t.accent_bd : t.control_bd;
    if (dim) {
        bg = theme::at_opacity(on ? t.accent_dim : t.control, t.window,
                               theme::Metrics::icon_dim_opacity);
        bd = theme::at_opacity(theme::composite(on ? t.accent_bd : t.control_bd, t.window),
                               t.window, theme::Metrics::icon_dim_opacity);
    } else if (!on && underMouse()) {
        bg = t.control; // 悬停仅换底（零形变）
    }
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(bd, 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(box, kTileRadius, kTileRadius);

    // 预留对勾位（无论勾选与否都占位 → 固定宽、零形变）
    const QRectF mark(kTilePadX, (height() - kCheckPx) / 2.0, kCheckPx, kCheckPx);
    draw_check_box(p, mark, t, on, dim);

    const QFont name_font = theme::font(theme::Typography::card_title_px, QFont::Bold);
    const QFont hint_font = theme::font(theme::Typography::badge_px);
    const QFontMetrics name_fm(name_font);
    const QFontMetrics hint_fm(hint_font);
    const int hint_w = hint_.isEmpty() ? 0 : hint_fm.horizontalAdvance(hint_);
    const int text_x = int(mark.right()) + kTileGap;
    const QRect name_rect(text_x, 0, std::max(0, width() - text_x - kTilePadX - hint_w - kTileGap),
                          height());
    p.setFont(name_font);
    p.setPen(dim ? theme::at_opacity(t.text, t.window, theme::Metrics::icon_dim_opacity)
                 : (on ? t.text : t.text2));
    p.drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter,
               name_fm.elidedText(name_, Qt::ElideRight, name_rect.width()));
    if (hint_w > 0) {
        p.setFont(hint_font);
        p.setPen(dim ? theme::at_opacity(t.text3, t.window, theme::Metrics::icon_dim_opacity)
                     : (on ? t.accent : t.text3));
        p.drawText(QRect(width() - kTilePadX - hint_w, 0, hint_w, height()),
                   Qt::AlignRight | Qt::AlignVCenter, hint_);
    }
}

// ---------------------------------------------------------------------------
// 徽章 pill（R4 高 16 r3 9.5px/700；四语义色 + 中性计数）
// ---------------------------------------------------------------------------
PillBadge::PillBadge(QWidget *parent) : QLabel(parent) {
    setObjectName(QStringLiteral("pp-fk-pill"));
    setFont(theme::font(double(kPillPx10) / 10.0, kPillWeight));
    setAlignment(Qt::AlignCenter);
    setFixedHeight(kPillH);
    setWordWrap(false);
}

void PillBadge::set_tone(Tone tone) {
    tone_ = tone;
    update();
}

void PillBadge::set_tokens(const theme::Tokens &t) {
    tokens_ = t;
    update();
}

// 徽章色调名（theme::pill_paint 的取色键 —— §4.1 末行的四语义 + 中性档单源）
static const char *pill_tone_name(PillBadge::Tone tone) {
    switch (tone) {
    case PillBadge::Tone::Neutral:
        return theme::kPillNeutral;
    case PillBadge::Tone::Accent:
        return theme::kPillAccent;
    case PillBadge::Tone::Ok:
        return theme::kPillOk;
    case PillBadge::Tone::Warn:
        return theme::kPillWarn;
    case PillBadge::Tone::Err:
        return theme::kPillErr;
    }
    return theme::kPillNeutral;
}

QSize PillBadge::sizeHint() const {
    const QFontMetrics fm(theme::font(double(kPillPx10) / 10.0, kPillWeight));
    return QSize(theme::Metrics::pill_pad_x * 2 + fm.horizontalAdvance(text()), kPillH);
}

void PillBadge::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const theme::PillPaint paint = theme::pill_paint(tokens_, pill_tone_name(tone_));
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(paint.bd, 1.0));
    p.setBrush(paint.bg);
    p.drawRoundedRect(box, kPillRadius, kPillRadius);
    p.setFont(theme::font(double(kPillPx10) / 10.0, kPillWeight));
    p.setPen(paint.fg);
    p.drawText(rect(), Qt::AlignCenter, text());
}

PillBadge *PillBadge::completed(QWidget *parent) {
    // §7.3/§8：完成态唯一形态（绿胶囊"✓ 已完成"）—— 别处不得自造
    return of(parent, Tone::Ok, QCoreApplication::translate("pp::ui::formkit", "✓ 已完成"));
}

PillBadge *PillBadge::count(QWidget *parent, int n) {
    return of(parent, Tone::Neutral, QString::number(n));
}

PillBadge *PillBadge::of(QWidget *parent, Tone tone, const QString &text) {
    auto *pill = new PillBadge(parent);
    pill->set_tone(tone);
    pill->setText(text);
    return pill;
}

// ---------------------------------------------------------------------------
// 打标 chip（键帽 + 文字，固定宽）
// ---------------------------------------------------------------------------
ChipButton::ChipButton(QWidget *parent) : QAbstractButton(parent) {
    setObjectName(QStringLiteral("pp-fk-chip"));
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedHeight(kChipH);
    setFont(theme::font(theme::Typography::small_px, QFont::DemiBold)); // §4.2 标签档 11/600
}

void ChipButton::set_key_text(const QString &key) {
    key_ = key;
    updateGeometry();
    update();
}

void ChipButton::set_tokens(const theme::Tokens &t) {
    tokens_ = t;
    update();
}

QSize ChipButton::sizeHint() const {
    const QFontMetrics fm(font());
    // 宽度只由键帽 + 文字决定（与勾选/悬停无关）→ 固定宽、零形变
    return QSize(kChipPadX * 2 + kChipKeyPx + kChipGap + fm.horizontalAdvance(text()), kChipH);
}

void ChipButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const theme::Tokens &t = tokens_;
    const bool on = isChecked();
    const bool dim = !isEnabled();
    QColor bg = on ? t.accent_dim : QColor(Qt::transparent);
    QColor bd = on ? t.accent_bd : t.control_bd;
    if (dim) {
        bg = theme::at_opacity(on ? t.accent_dim : t.control, t.window,
                               theme::Metrics::icon_dim_opacity);
        bd = theme::at_opacity(theme::composite(on ? t.accent_bd : t.control_bd, t.window),
                               t.window, theme::Metrics::icon_dim_opacity);
    } else if (!on && underMouse()) {
        bg = t.control;
    }
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(bd, 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(box, kChipRadius, kChipRadius);

    // 键帽（13×13 r2；选中 = acc 底 + acc-on 字；键面字取 §4.2 徽标档 9.5/700）
    const QRectF cap(kChipPadX, (height() - kChipKeyPx) / 2.0, kChipKeyPx, kChipKeyPx);
    if (!key_.isEmpty()) {
        p.setPen(QPen(on ? Qt::NoPen : t.control_bd, 1.0));
        p.setBrush(on ? QBrush(t.accent)
                      : QBrush(dim ? theme::at_opacity(t.control, t.window,
                                                       theme::Metrics::icon_dim_opacity)
                                   : t.control));
        p.drawRoundedRect(cap, kCheckRadius, kCheckRadius);
        p.setFont(theme::font(theme::Typography::badge_px, QFont::Bold));
        p.setPen(dim ? theme::at_opacity(t.on_accent, t.window, theme::Metrics::icon_dim_opacity)
                     : (on ? t.on_accent : t.text2));
        p.drawText(cap, Qt::AlignCenter, key_);
    }
    const int text_x = int(cap.right()) + kChipGap;
    const int text_w = std::max(0, width() - text_x - kChipPadX);
    p.setFont(font());
    p.setPen(dim ? theme::at_opacity(t.text, t.window, theme::Metrics::icon_dim_opacity)
                 : (on ? t.text : t.text2));
    p.drawText(QRect(text_x, 0, text_w, height()), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(font()).elidedText(text(), Qt::ElideRight, text_w));
}

// ---------------------------------------------------------------------------
// 进度条（R4 高 6 r99；C8 语义不变）
// ---------------------------------------------------------------------------
ProgressStrip::ProgressStrip(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("pp-fk-strip"));
    setFixedHeight(kProgressH);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ProgressStrip::set_tokens(const theme::Tokens &t) {
    tokens_ = t;
    update();
}

bool ProgressStrip::settled() const {
    return kind_ == Kind::Done || kind_ == Kind::Warn || kind_ == Kind::Failed;
}

void ProgressStrip::set_state(Kind kind, double frac, bool synthetic) {
    const double clamped = std::clamp(frac, 0.0, 1.0);
    const bool synth = synthetic && kind == Kind::Active; // 终态不画斜纹（§7.3 只描述进行中）
    if (kind_ == kind && frac_ == clamped && synthetic_ == synth)
        return; // 同值早退
    kind_ = kind;
    frac_ = clamped;
    synthetic_ = synth;
    // 探针协议与运行页迷你条同源（C8：run-bars 像素取证继续可判）
    const char *name = "pending";
    switch (kind_) {
    case Kind::Pending:
        name = "pending";
        break;
    case Kind::Active:
        name = "active";
        break;
    case Kind::Done:
        name = "done";
        break;
    case Kind::Warn:
        name = "warn";
        break;
    case Kind::Failed:
        name = "failed";
        break;
    }
    setProperty("ppBarKind", QString::fromLatin1(name));
    setProperty("ppBarSynthetic", synthetic_);
    setProperty("ppBarFrac", frac_);
    update();
}

QSize ProgressStrip::sizeHint() const { return QSize(80, kProgressH); }

QSize ProgressStrip::minimumSizeHint() const { return QSize(0, kProgressH); }

QColor ProgressStrip::fill_color() const {
    const theme::Tokens &t = tokens_;
    switch (kind_) {
    case Kind::Active:
        return t.accent; // 实心 = 已写盘
    case Kind::Done:
        return t.ok; // 绿 = 完成
    case Kind::Warn:
        return t.warn; // 琥珀 = 异常（0 字节）
    case Kind::Failed:
        return t.err; // 红 = 失败
    case Kind::Pending:
        break;
    }
    return t.text3;
}

// 斜纹（合成）= 双色带：115°/band 6px/period 12px（token 单源）。用**两个不同颜色**铺满，
// 与"实心单色"在像素取整下可判（C8）。
QBrush ProgressStrip::stripe_brush(const QRectF &fill) const {
    const theme::Tokens &t = tokens_;
    constexpr double kPi = 3.14159265358979323846;
    const double rad = double(theme::Metrics::synth_stripe_deg) * kPi / 180.0;
    const QPointF dir(std::sin(rad), -std::cos(rad));
    const double period = double(theme::Metrics::synth_stripe_period_px);
    const QPointF center = fill.center();
    QLinearGradient g(center - dir * (period / 2.0), center + dir * (period / 2.0));
    g.setSpread(QGradient::RepeatSpread);
    g.setColorAt(0.0, t.progress_fill_from);
    g.setColorAt(0.5, t.progress_fill_from);
    g.setColorAt(0.5001, t.progress_fill_to);
    g.setColorAt(1.0, t.progress_fill_to);
    return QBrush(g);
}

void ProgressStrip::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const theme::Tokens &t = tokens_;
    const qreal r = height() / 2.0; // R4 r99 → 半高
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(t.control_bd, theme::Metrics::progress_border_px));
    p.setBrush(t.control);
    p.drawRoundedRect(box, r, r);

    const double shown = settled() ? 1.0 : frac_; // 终态恒满格
    if (shown <= 0.0)
        return;
    const qreal inset = theme::Metrics::progress_border_px;
    const QRectF track = box.adjusted(inset, inset, -inset, -inset);
    const QRectF fill(track.left(), track.top(), track.width() * shown, track.height());
    if (fill.width() <= 0.0)
        return;
    p.setPen(Qt::NoPen);
    p.setBrush(synthetic_ ? stripe_brush(fill) : QBrush(fill_color()));
    p.drawRoundedRect(fill, fill.height() / 2.0, fill.height() / 2.0);
}

// ---------------------------------------------------------------------------
// 图标钮（R4 28×28 r4；§11：必有 accessibleName + tooltip）
// ---------------------------------------------------------------------------
IconButton::IconButton(QWidget *parent) : QAbstractButton(parent) {
    setObjectName(QStringLiteral("pp-fk-icon"));
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void IconButton::set_action(const QString &text) {
    setToolTip(text);        // §11：图标钮必有 tooltip
    setAccessibleName(text); // §11：图标钮必有 accessibleName
}

void IconButton::set_tokens(const theme::Tokens &t) {
    tokens_ = t;
    update();
}

QSize IconButton::sizeHint() const { return QSize(kIconBtn, kIconBtn); }

void IconButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const theme::Tokens &t = tokens_;
    const bool dim = !isEnabled();
    QColor bg = Qt::transparent;
    if (dim)
        bg = t.icon_disabled_bg;
    else if (isDown())
        bg = theme::composite(t.control_bd, t.control);
    else if (isChecked())
        bg = t.accent_dim;
    else if (underMouse())
        bg = t.control; // 悬停显底（R4）
    if (bg.alpha() > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(QRectF(rect()), kIconBtnRadius, kIconBtnRadius);
    }
    if (!icon().isNull()) {
        // 禁用态按 .dim 口径整钮变暗（QSS 无控件级不透明度；此处等价用 painter 不透明度）
        if (dim)
            p.setOpacity(theme::Metrics::icon_dim_opacity);
        icon().paint(&p, rect(), Qt::AlignCenter, dim ? QIcon::Disabled : QIcon::Normal,
                     isChecked() ? QIcon::On : QIcon::Off);
    }
}

// ---------------------------------------------------------------------------
// 表单布尔勾选框（13×13 r2 自绘 + 右侧文字）
// ---------------------------------------------------------------------------
CheckBox::CheckBox(QWidget *parent) : QCheckBox(parent) {
    setObjectName(QStringLiteral("pp-fk-check"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFixedHeight(kRowPitch); // 行高即命中区（R1；文字/勾选框都在同一行内）
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFont(theme::font(theme::Typography::body_px)); // §4.2 正文·值 11.5
}

void CheckBox::set_tokens(const theme::Tokens &t) {
    tokens_ = t;
    update();
}

QSize CheckBox::sizeHint() const {
    const QFontMetrics fm(font());
    return QSize(kCheckPx + kCheckGap + fm.horizontalAdvance(text()), kRowPitch);
}

bool CheckBox::hitButton(const QPoint &pos) const { return rect().contains(pos); }

void CheckBox::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const theme::Tokens &t = tokens_;
    const bool dim = !isEnabled();
    const QRectF mark(0, (height() - kCheckPx) / 2.0, kCheckPx, kCheckPx);
    draw_check_box(p, mark, t, isChecked(), dim);
    p.setFont(theme::font(theme::Typography::body_px));
    p.setPen(dim ? theme::at_opacity(t.text3, t.window, theme::Metrics::icon_dim_opacity) : t.text);
    const int text_x = kCheckPx + kCheckGap;
    const int text_w = std::max(0, width() - text_x);
    p.drawText(QRect(text_x, 0, text_w, height()), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(font()).elidedText(text(), Qt::ElideRight, text_w));
}

} // namespace pp::ui::formkit
