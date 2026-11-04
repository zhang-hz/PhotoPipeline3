// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页拆分共享内核（B.1 拆分步 B1：meta_state）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1；契约注释见 meta_state.h。
// 本 TU = 页面状态锚（state_of/set_hook）+ 共享胶水 + 表单行构造器 + 四卡共用卡头。
// 实现自 page_meta.cpp **逐字迁出**（M7-R2.1-F2；行为零变化）。
#include "ui/meta_state.h"
#include "ui/meta_probe_context.h" // R-182/R-258：钩子/词表单源

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QToolButton>
#include <QVariant>
#include <QWidget>

namespace pp::ui::meta {

// M13（0.4.1）/ C2.3：DeltaSpin 的 mixed 文本（tr 上下文 = PageMeta；textFromValue 为 const，
// 故取 QCoreApplication::translate 直接查表，文案单源 = meta_state.h 的 kMixedValueText）。
QString DeltaSpin::mixed_text() const {
    return QCoreApplication::translate("PageMeta", kMixedValueText);
}

// ---------------------------------------------------------------------------
// M15 / 0.4.2 用户反馈：时区下拉的 mixed 显示面（真正显示「多个值」，不是只换边框色）
//   * 常态：委托 formkit::Combo::paintEvent（逐像素不变）；
//   * mixed：盒/字同一条 QStylePainter(qss) 路径，只把 currentText 换成「多个值」，
//     再补画 trailing 矢量箭头（与 formkit::Combo 同形/同色口径，C3 单源 icons）。
//   * **零模型改动**：不增删 item、不改 currentIndex → 下拉弹层/选中/activated 全走既有路径。
// ---------------------------------------------------------------------------
MixedComboBox::MixedComboBox(QWidget *parent) : formkit::Combo(parent) {}

QString MixedComboBox::mixed_text() const {
    return QCoreApplication::translate("PageMeta", kMixedValueText);
}

QString MixedComboBox::display_text() const { return mixed_ ? mixed_text() : currentText(); }

void MixedComboBox::set_mixed(bool mixed) {
    if (mixed_ == mixed) {
        return;
    }
    mixed_ = mixed;
    update();
}

void MixedComboBox::set_tokens(const theme::Tokens &t) {
    tokens_ = t;
    formkit::Combo::set_tokens(t);
}

void MixedComboBox::paintEvent(QPaintEvent *event) {
    if (!mixed_) {
        formkit::Combo::paintEvent(event);
        return;
    }
    QStylePainter painter(this);
    painter.setPen(palette().color(QPalette::Text));
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    opt.currentText = mixed_text();
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);
    painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
    // trailing 矢量箭头：与 formkit::Combo::paintEvent 逐位同口径（同一 token/同一形/同一槽位）
    QPainter arrow(this);
    const int box = theme::Metrics::combo_arrow_px * 2 + 8;
    const QRectF slot(layoutDirection() == Qt::RightToLeft ? 0 : width() - box, 0, box, height());
    const QColor color = isEnabled() ? tokens_.text2 : tokens_.text3;
    icons::paint(arrow, icons::Name::ChevronDown,
                 slot.adjusted((box - theme::Metrics::combo_arrow_px) / 2.0, 0,
                               -(box - theme::Metrics::combo_arrow_px) / 2.0, 0),
                 color, icons::kStrokePx);
}

MetaState *state_of(const PageMeta *page) {
    // R-177（B1）：动态属性锚的**类型显式化** —— qobject_cast（MetaState 带 Q_OBJECT）替代
    // 裸 static_cast；属性未挂 = nullptr（与既有空态一致），类型不符 = 编程错误（Q_ASSERT 硬线）。
    QObject *anchor = page->property(kStateProperty).value<QObject *>();
    MetaState *st = qobject_cast<MetaState *>(anchor);
    Q_ASSERT(st != nullptr || anchor == nullptr);
    return st;
}

// 自验钩子（动态属性）：同时挂在 MetaState 子对象（自验程序）与页面本身（--ui-smoke 探针只持有

void set_hook(MetaState *st, const char *name, const QVariant &value) {
    st->setProperty(name, value);
    if (st->core.page != nullptr) {
        st->core.page->setProperty(name, value);
    }
}

void notify_rules_changed(MetaState *st) {
    if (!st->core.loading) {
        emit st->core.page->rules_changed();
    }
}

// 0.4.2（需求 1/2）：GPS 卡「启用」的**唯一**写入点 —— 卡头开关（折叠面 gps_enable 复选框随
// 「批量 GPS 规则」概念删除，见 meta-cards / 需求 2；本函数保留为窄接口供选点闭环等程序化启停）。
void set_gps_rule_enabled(MetaState *st, bool on) {
    if (st->gps.gps_enable_switch == nullptr) {
        return;
    }
    const QSignalBlocker block_sw(st->gps.gps_enable_switch);
    st->gps.gps_enable_switch->setChecked(on);
}

// formkit 行构造的页内辅助（R2/R3 的组内规则）
//   页面自有的"多控件一行"（年月日 / 时分秒 / 生效对照 / 开关行 / 操作行 / 关键字行）在**一个
//   字段轨内**并列：单位入 16px 槽、行内距 6px、组内距 8px —— 三个取值都取自 formkit
//   （formkit_layout.h 的 kUnitSlotW / kInlineGap + 令牌 group_gap）；行/标签轨/轨宽仍由
//   formkit 的行构造器给出。
// ---------------------------------------------------------------------------

void TrackGroup::add(QWidget *item, int stretch) {
    line->addWidget(item, stretch, Qt::AlignVCenter);
}
void TrackGroup::gap(int px) {
    if (px > 0) {
        line->addSpacing(px);
    }
}
void TrackGroup::stretch() { line->addStretch(1); }

TrackGroup make_group(QWidget *parent) {
    TrackGroup out;
    out.host = new QWidget(parent);
    out.line = new QHBoxLayout(out.host);
    out.line->setContentsMargins(0, 0, 0, 0);
    out.line->setSpacing(0);
    return out;
}

// 单位槽（R3：单位入 16px 槽）—— 与 formkit 的 unit_slot 同取值同着色钩子
QLabel *unit_slot(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkUnit));
    label->setFont(theme::font(theme::Typography::hint_px));
    label->setAlignment(Qt::AlignLeading | Qt::AlignVCenter); // R-218：逻辑首端（LTR=左）
    label->setFixedWidth(formkit::kUnitSlotW);
    return label;
}

// 10px 注释（R3：范围/提示入注释）——与 formkit 的 range_note 同取值同着色钩子
ElidedLabel *note_label(QWidget *parent, const QString &object_name) {
    auto *label = new ElidedLabel(parent);
    if (!object_name.isEmpty()) {
        label->setObjectName(object_name);
    }
    label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkNote));
    label->setFont(theme::font(theme::Typography::hint_px));
    label->setAlignment(Qt::AlignLeading | Qt::AlignVCenter); // R-218：逻辑首端（LTR=左）
    return label;
}

// 文本链（§5 R4「文字链无框」；高 26 = A7 按钮换锚）
QToolButton *make_link(QWidget *parent, const QString &object_name, const QString &text) {
    auto *link = new QToolButton(parent);
    link->setObjectName(object_name);
    link->setProperty(kFieldProperty, fieldprop::kLink);
    link->setText(text);
    link->setAutoRaise(true);
    link->setCursor(Qt::PointingHandCursor);
    link->setFixedHeight(theme::Metrics::btn_h);
    link->setFont(theme::font(theme::Typography::body_px, 600));
    return link;
}

// 次按钮（§5 R4：高 26 + 描边；A7）
QPushButton *make_button(QWidget *parent, const QString &object_name, const QString &text) {
    auto *button = new QPushButton(text, parent);
    button->setObjectName(object_name);
    button->setProperty(kFieldProperty, fieldprop::kBtn);
    button->setFixedHeight(theme::Metrics::btn_h);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

// 规则输入框（R4 文本输入高 24；placeholder = 输入示例，修 1-33）
QLineEdit *make_rule_field(QWidget *parent, const QString &object_name) {
    auto *edit = new QLineEdit(parent);
    edit->setObjectName(object_name);
    edit->setProperty(kFieldProperty, fieldprop::kRule);
    edit->setFixedHeight(formkit::kControlH);
    return edit;
}

// 可勾选文字 chip（M6-T8/§5.1「分类 chips」；现存用点 = DMS 显示档切换）。
// R-184（M7-R2D1）：原实现是 formkit::ChipButton 的**手工复刻**（同度量/同色 QSS，人工对齐，
// deep-review E2-05「第二套 chip 渲染」）—— 现收编公共件（唯一渲染路径：键帽空 = 无键帽、
// tokens 自绘；本页 QSS 的 chip 规则随之不再参与本控件）。
formkit::ChipButton *make_chip(QWidget *parent, const QString &object_name, const QString &text) {
    auto *chip = new formkit::ChipButton(parent);
    chip->setObjectName(object_name);
    chip->setText(text);
    chip->setCheckable(true);
    chip->setCursor(Qt::PointingHandCursor);
    chip->setFixedHeight(formkit::kChipH);
    chip->setToolTip(text);
    return chip;
}

// M6-T12（§5.3）：地图覆盖层的图标钮 —— 24×24 图标钮（矢量图标，C3 无字形）；tooltip +
// accessibleName 成对（§11 图标钮必有）；取色不吃 QSS → 主题重放时显式重刷（见 apply_style）。
// M6-T13 起缩放钮之外的「展开」钮复用同一工厂（同一档外观 = HUD 白底钮）。
QToolButton *make_map_hud_button(QWidget *parent, const QString &object_name, icons::Name icon,
                                 const QString &tip) {
    auto *button = new QToolButton(parent);
    button->setObjectName(object_name);
    button->setProperty(kFieldProperty, fieldprop::kMapZoom);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    button->setFixedSize(kMapZoomBtnPx, kMapZoomBtnPx);
    button->setIconSize(QSize(kMapZoomIconPx, kMapZoomIconPx));
    button->setProperty(icons::kIconNameProperty, QString::fromLatin1(icons::key(icon)));
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QFrame *make_card(QWidget *parent) {
    auto *card = new QFrame(parent);
    card->setObjectName(objname::kPpCard); // theme QSS：QFrame#pp-card
    return card;
}

// 卡头（§7.1/mockup .card-h：标题 12/700 + **中性状态胶囊**（1-24/1-25：卡头右置元素统一规格）+
// 右端 hint（10px/--txt3，超长省略）+ UX-5「启用」开关（30×16，最右））
CardHeader make_header(MetaState *st, QWidget *parent, const QString &title,
                       const QString &pill_name, const QString &hint_name,
                       const QString &enable_name) {
    CardHeader out;
    out.row = new QWidget(parent);
    auto *layout = new QHBoxLayout(out.row);
    layout->setContentsMargins(theme::Metrics::card_pad_x, 9, theme::Metrics::card_pad_x, 7);
    layout->setSpacing(theme::Metrics::group_gap);
    out.title = new QLabel(title, out.row);
    out.title->setObjectName(objname::kPpCardTitle);
    out.title->setFont(
        theme::font(theme::Typography::card_title_px, theme::Typography::card_title_weight));
    layout->addWidget(out.title);
    out.pill = new formkit::PillBadge(out.row);
    if (!pill_name.isEmpty()) {
        out.pill->setObjectName(pill_name);
    }
    out.pill->set_tone(formkit::PillBadge::Tone::Neutral); // 1-24/1-25：卡头右置一律中性
    out.pill->setVisible(false);
    layout->addWidget(out.pill);
    st->add_pill(out.pill);
    layout->addStretch(1);
    out.hint = new ElidedLabel(out.row);
    if (!hint_name.isEmpty()) {
        out.hint->setObjectName(hint_name);
    }
    out.hint->setProperty(kFieldProperty, fieldprop::kHint);
    out.hint->setFont(theme::font(theme::Typography::hint_px, 500));
    out.hint->setAlignment(Qt::AlignTrailing | Qt::AlignVCenter); // R-218：逻辑尾端（LTR=右）
    layout->addWidget(out.hint, 1);
    if (!enable_name.isEmpty()) {
        // UX-5：卡头右置「启用」开关（formkit::SwitchControl 30×16；初态 = 关）
        out.enable = new formkit::SwitchControl(out.row);
        out.enable->setObjectName(enable_name);
        out.enable->setToolTip(PageMeta::tr(kCardEnableTooltip));
        out.enable->setAccessibleName(PageMeta::tr(kCardEnableTooltip));
        out.enable->set_tokens(st->core.tokens);
        st->core.switches.append(out.enable);
        layout->addWidget(out.enable, 0, Qt::AlignVCenter);
    }
    return out;
}

// ---------------------------------------------------------------------------
// 生效值刷新（纯函数重算；每次规则变化即时刷新）—— B1：薄壳收口后的编排层
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 跟随项 probe → meta_probe.{h,cpp}（B.1 拆分步 B4：ProbeScanner 唯一 IO 宿主，R-181）
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 生效值刷新（纯函数重算；每次规则变化即时刷新）
// ---------------------------------------------------------------------------

QString follow_hint(const MetaState *st) {
    if (st->follow.items.isEmpty()) {
        // 0.4.2（需求 1）：无行选中 = 空态（不再回退首个文件）；信息卡空态行另有完整说明。
        return PageMeta::tr("未选择文件");
    }
    QString name = QFileInfo(st->follow.items.first().path).fileName();
    const int total = static_cast<int>(st->follow.batch_files.size());
    const int index =
        static_cast<int>(st->follow.batch_files.indexOf(st->follow.items.first().path)) +
        1; // 1-based；0 = 未找到
    if (st->follow.items.size() >= 2) {
        return PageMeta::tr("已选 %1 个 · %2").arg(st->follow.items.size()).arg(name);
    }
    if (total > 0 && index > 0) {
        return QStringLiteral("%1 (%2/%3)").arg(name).arg(index).arg(total);
    }
    return name;
}

// (B1) QString follow_hint → meta_state.cpp

void refresh_effective(MetaState *st) {
    st->info.info_hint->set_full_text(follow_hint(st));
    // 0.4.2（需求 1/2）：「已选 N 个文件」胶囊 = 本次编辑会落到的文件数（= 行选中集合大小）。
    // 单选/无选中时不显示（单选对象已由文件列表高亮 + hint 文件名表达；无选中 = 空态）。
    // 0.4.1 起该文案已改为选择项口径；0.4.2 补上可见性落地（此前 setText 后胶囊始终隐藏）。
    const int selected = static_cast<int>(st->follow.items.size());
    if (selected >= 2) {
        st->time.time_pill->setText(PageMeta::tr("修改仅作用于选中的 %1 个文件").arg(selected));
        st->time.time_pill->setVisible(true);
    } else {
        st->time.time_pill->setText(QString());
        st->time.time_pill->setVisible(false);
    }
    if (st->follow.probe->pending()) {
        refresh_info_card(st, {});
        refresh_time_section(st, {});
        refresh_gps_section(st, {});
        refresh_keywords(st);
        return;
    }
    // M13（0.4.1）/ C2.1：UI 不再编辑全局 BatchRules —— 生效值唯一合成源 =
    // preview_effective(probe, BatchRules{}, per_file_override)（显示 = 即将写入）。
    const pp::BatchRules rules{};
    std::vector<pp::EffectivePreview> previews;
    previews.reserve(st->follow.probes.size());
    for (std::size_t i = 0; i < st->follow.probes.size(); ++i) {
        const std::optional<pp::MetadataOverride> exception =
            i < std::size_t(st->follow.items.size()) ? st->follow.items.at(int(i)).exception
                                                     : std::optional<pp::MetadataOverride>();

        previews.push_back(
            pp::preview_effective(st->follow.probes[i].meta, rules, exception)); // §5.1 唯一合成源
    }
    refresh_info_card(st, previews);
    refresh_time_section(st, previews);
    refresh_gps_section(st, previews);
    refresh_keywords(st);
}

// (B1) void refresh_effective → meta_state.cpp

void refresh_all(MetaState *st) {
    refresh_time_inputs(st);
    refresh_gps_inputs(st);
    refresh_privacy_inputs(st);
    refresh_gps_warning(st);
    refresh_effective(st);
}

} // namespace pp::ui::meta
