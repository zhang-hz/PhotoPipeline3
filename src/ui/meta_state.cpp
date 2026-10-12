// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页拆分共享内核（B.1 拆分步 B1：meta_state）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1；契约注释见 meta_state.h。
// 本 TU = 页面状态锚（state_of/set_hook）+ 共享胶水 + 表单行构造器 + 四卡共用卡头。
// 实现自 page_meta.cpp **逐字迁出**（M7-R2.1-F2；行为零变化）。
#include "ui/meta_state.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVariant>
#include <QWidget>

namespace pp::ui::meta {

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

// UX-5（M7-WP7）：GPS 卡「启用」的**双向同步**唯一落点 —— 卡头开关与折叠面 gps_enable 是
// 同一状态的两个入口；选点/联想自动勾上沿用既有行为（apply_picked_point/read_gps 调这里）。
void set_gps_rule_enabled(MetaState *st, bool on) {
    const QSignalBlocker block_box(st->gps.gps_enable);
    const QSignalBlocker block_sw(st->gps.gps_enable_switch);
    st->gps.gps_enable->setChecked(on);
    if (st->gps.gps_enable_switch != nullptr) {
        st->gps.gps_enable_switch->setChecked(on);
    }
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
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
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
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
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

// 只读生效值框（§7.1 GPS 卡的生效坐标；mockup .gps-in 口径：--ctl 底 + r3 + tabular）
QLineEdit *make_view_field(QWidget *parent, const QString &object_name) {
    auto *edit = new QLineEdit(parent);
    edit->setObjectName(object_name);
    edit->setProperty(kFieldProperty, fieldprop::kView);
    edit->setReadOnly(true);
    edit->setFixedHeight(formkit::kControlH);
    edit->setFont(theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
    return edit;
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
    card->setObjectName(QStringLiteral("pp-card")); // theme QSS：QFrame#pp-card
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
    out.title->setObjectName(QStringLiteral("pp-card-title"));
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
    out.hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
        return PageMeta::tr("无文件");
    }
    const QString name = QFileInfo(st->follow.items.first().path).fileName();
    const int total = st->follow.batch_files.size();
    const int index =
        st->follow.batch_files.indexOf(st->follow.items.first().path) + 1; // 1-based；0 = 未找到
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
    // 时间卡范围胶囊（mockup「影响 12 / 24」的语义位）：页面知道的只有批内文件数，除数/被除数
    // 同源 → 不编造"N / M"。
    st->time.time_pill->setText(PageMeta::tr("影响 %1 个文件").arg(st->follow.batch_files.size()));
    if (st->follow.probe->pending()) {
        refresh_info_card(st, {});
        refresh_time_section(st, {});
        refresh_gps_section(st, nullptr);
        refresh_keywords(st);
        return;
    }
    const pp::BatchRules rules = build_rules(st);
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
    refresh_gps_section(st, previews.empty() ? nullptr : &previews.front());
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
