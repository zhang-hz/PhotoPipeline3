// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页「时间偏移」卡（B.1 拆分步 B5：meta_card_time）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（步序 B5）；契约面见 meta_state.h。
// 内容 = build_time_card + refresh_time_section + refresh_time_inputs + clear_multi_rows
//        + time_card_intents —— 自 page_meta.cpp 逐字迁出（M7-R2.1-F2；行为零变化）。
#include "ui/meta_probe_context.h" // R-182/R-258：钩子/词表单源
#include "ui/meta_state.h"

#include "ui/contextmenu.h"
#include "ui/formkit.h"
#include "ui/icons.h"
#include "ui/meta_format.h"
#include "ui/meta_rules.h"
#include "ui/page_meta.h"
#include "ui/theme.h"

#include <QActionGroup>
#include <QClipboard>
#include <QComboBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pp::ui::meta {

// 清空多选小表（**保留** multi_more —— 它是常驻子控件、仅在有截断行时才入列，重复入列/
// 误删会造成 use-after-free）
void clear_multi_rows(MetaState *st) {
    if (st->time.multi_grid == nullptr) {
        return;
    }
    while (QLayoutItem *item = st->time.multi_grid->takeAt(0)) {
        // R-180：常驻件判定 = 显式标记（原指针比较的隐式约定；multi_more 构建时标记）
        if (item->widget() != nullptr && !is_resident(item->widget())) {
            delete item->widget();
        }
        delete item;
    }
}

void refresh_time_section(MetaState *st, const std::vector<pp::EffectivePreview> &previews) {
    const bool empty = st->follow.items.isEmpty();
    const bool multi = previews.size() >= 2;
    st->time.effect_row->setVisible(!empty && !multi);
    st->time.time_empty_row->setVisible(empty);
    st->time.multi_host->setVisible(!empty && multi);

    const pp::TimeShift shift = current_time_shift(st);
    const auto clear_single = [st] {
        st->time.time_old->set_full_text(QString());
        st->time.time_new->set_full_text(QString());
        st->time.time_note->set_full_text(QString());
        st->time.time_src->set_full_text(QString());
        theme::set_mod(st->time.time_new, false);
    };

    if (previews.empty()) {
        clear_single();
        clear_multi_rows(st);
        st->time.multi_more->set_full_text(QString());
        st->time.multi_more->setVisible(false);
        set_hook(st, hook::kMultiRows, 0);
        set_hook(st, hook::kTimeNew, QString());
        return;
    }

    if (!multi) {
        const pp::EffectivePreview &preview = previews.front();
        st->time.time_old->set_full_text(datetime_text(preview.datetime_original.original));
        st->time.time_new->set_full_text(preview.strip_privacy
                                             ? PageMeta::tr(kWillRemoveText)
                                             : datetime_text(preview.datetime_original.effective));
        QStringList note_parts;
        const QString date = date_part(preview.datetime_original.original);
        if (!date.isEmpty()) {
            note_parts << date;
        }
        note_parts << delta_text(shift);
        st->time.time_note->set_full_text(note_parts.join(QStringLiteral(" · ")) +
                                          QStringLiteral(" ·"));
        st->time.time_src->set_full_text(QString::fromLatin1(kPrimaryTimeSource));
        theme::set_mod(st->time.time_new, preview.datetime_original.changed);
        // 另两个时间字段（CreateDate / ModifyDate）的对照进标签 tooltip（1-30：细节进 tooltip）
        const std::array<const pp::EffectiveField<std::optional<pp::DateTimeVal>> *, 2> others = {
            &preview.datetime_digitized, &preview.datetime_modify};
        QString tip = PageMeta::tr("跟随选中文件；未修改时两值相同");
        for (std::size_t i = 0; i < others.size(); ++i) {
            tip += QStringLiteral("\n%1: %2 → %3")
                       .arg(QString::fromLatin1(kSecondaryTimeSources[i]),
                            datetime_text(others[i]->original),
                            preview.strip_privacy ? PageMeta::tr(kWillRemoveText)
                                                  : datetime_text(others[i]->effective));
        }
        st->time.effect_label->setToolTip(tip);
        set_hook(st, hook::kTimeNew,
                 st->time.time_new->full_text()); // 自验钩子（探针读生效时间）
        set_hook(st, hook::kMultiRows, 0);
        return;
    }

    // 多选（≥2）：逐行小表（文件名 | 原 → 生效），上限 8 行 + 「…还有 N 个」（§5.2）
    clear_single(); // 单文件对照行让位（文本一并清空：探针据此判定"多选态"）
    if (st->time.multi_grid == nullptr) {
        return;
    }
    clear_multi_rows(st);
    // 小表行数上限 = kMultiRowCap；"…还有 N 个" 的 N 按**选中项总数**算（probe 只覆盖前 8 个）
    const int total =
        std::max(static_cast<int>(st->follow.items.size()), static_cast<int>(previews.size()));
    const int rows = std::min(static_cast<int>(previews.size()), kMultiRowCap);
    for (int i = 0; i < rows; ++i) {
        const pp::EffectivePreview &preview = previews[std::size_t(i)];
        const QString name = QFileInfo(st->follow.items.at(i).path).fileName();
        auto *name_label = new ElidedLabel(st->time.multi_host);
        name_label->setObjectName(objname::kMultiName1.arg(i));
        name_label->setProperty(kFieldProperty, fieldprop::kMultiName);
        name_label->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        name_label->setMinimumHeight(formkit::kRowPitch);
        name_label->set_full_text(name);
        auto *old_label = new ElidedLabel(st->time.multi_host);
        old_label->setObjectName(objname::kMultiOld1.arg(i));
        old_label->setProperty(kFieldProperty, fieldprop::kOld);
        QFont old_font = theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true);
        old_font.setStrikeOut(true);
        old_label->setFont(old_font);
        old_label->setMinimumHeight(formkit::kRowPitch);
        old_label->set_full_text(datetime_text(preview.datetime_original.original));
        auto *arrow = new QLabel(QStringLiteral("→"), st->time.multi_host);
        arrow->setObjectName(objname::kMultiArrow);
        arrow->setProperty(kFieldProperty, fieldprop::kArrow);
        arrow->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        auto *new_label = new ElidedLabel(st->time.multi_host);
        new_label->setObjectName(objname::kMultiNew1.arg(i));
        new_label->setProperty(kFieldProperty, fieldprop::kNew);
        new_label->setFont(theme::font(theme::Typography::body_px, QFont::Bold, /*tabular=*/true));
        new_label->setMinimumHeight(formkit::kRowPitch);
        new_label->set_full_text(preview.strip_privacy
                                     ? PageMeta::tr(kWillRemoveText)
                                     : datetime_text(preview.datetime_original.effective));
        theme::set_mod(new_label, preview.datetime_original.changed);
        st->time.multi_grid->addWidget(name_label, i, 0);
        st->time.multi_grid->addWidget(old_label, i, 1);
        st->time.multi_grid->addWidget(arrow, i, 2);
        st->time.multi_grid->addWidget(new_label, i, 3);
    }
    const int rest = total - rows;
    st->time.multi_more->set_full_text(rest > 0 ? PageMeta::tr("…还有 %1 个").arg(rest)
                                                : QString());
    st->time.multi_more->setVisible(rest > 0);
    if (rest > 0) {
        st->time.multi_grid->addWidget(st->time.multi_more, rows, 0, 1, 4);
    }
    set_hook(st, hook::kMultiRows, rows);
    // 自验钩子：多选时生效时间不落在"单文件生效时间"钩子上（探针据此区分两种形态）
    set_hook(st, hook::kTimeNew, rows > 0 ? QStringLiteral("<multi>") : QString());
}

// ---------------------------------------------------------------------------
// 控件可用性 / 提示
// ---------------------------------------------------------------------------

void refresh_time_inputs(MetaState *st) {
    // UX-5：卡开关关 = 整卡不参与规则 → 卡内规则控件置灰（参数保留，恢复开关即回来）。
    // M13/C2.3：partial（选择集部分文件开启）按"可用"处理（可批量编辑部分开启的文件）。
    const bool card_on = st->time.time_enable == nullptr || st->time.time_enable->isChecked() ||
                         st->time.time_enable->is_partial();
    // seg 选中的那一组恒可见，另一组隐藏
    const bool delta_mode = st->time.seg->current_index() == 0;
    st->time.delta_row->setVisible(delta_mode);
    st->time.time_row->setVisible(delta_mode);
    st->time.tz_row->setVisible(!delta_mode);
    st->time.seg->setEnabled(card_on);
    for (QSpinBox *spin : st->time.delta) {
        spin->setEnabled(card_on);
        // M13：mixed 用 DeltaSpin「多个值」+ ppMixed 虚线视觉，不再叠 .mod 常态强调框
        const auto *delta = static_cast<const DeltaSpin *>(spin);
        if (!delta->is_mixed()) {
            theme::set_mod(spin, spin->value() != 0);
        }
    }
    const bool tz_changed =
        st->time.tz_from->currentData().toInt() != st->time.tz_to->currentData().toInt();
    st->time.tz_from->setEnabled(card_on);
    st->time.tz_to->setEnabled(card_on);
    const bool mixed_tz = st->time.tz_from->property(hook::kMixedProperty).toBool() ||
                          st->time.tz_to->property(hook::kMixedProperty).toBool();
    if (!mixed_tz) {
        theme::set_mod(st->time.tz_from, tz_changed);
        theme::set_mod(st->time.tz_to, tz_changed);
    }
}

QWidget *build_time_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    const CardHeader header =
        make_header(st, card, PageMeta::tr("时间偏移"), objname::kTimePill, objname::kTimeHint,
                    objname::kTimeEnable); // UX-5：卡头「启用」开关
    st->time.time_pill = header.pill;
    st->time.time_hint = header.hint;
    st->time.time_enable = header.enable;
    st->time.time_hint->set_full_text(PageMeta::tr("修改值以强调色标记"));
    v->addWidget(header.row);

    auto *body = new QWidget(card);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                           theme::Metrics::card_pad_bottom);
    bv->setSpacing(0);

    // 模式（§6：互斥模式唯一形态 = 分段控件，段宽 88 等宽）
    {
        TrackGroup group = make_group(body);
        st->time.seg = new formkit::SegmentedControl(group.widget());
        st->time.seg->setObjectName(objname::kSeg);
        st->time.seg->add_segment(PageMeta::tr("Δ 偏移"), objname::kSegDelta);
        st->time.seg->add_segment(PageMeta::tr("时区语义"), objname::kSegTz);
        group.add(st->time.seg);
        group.stretch();
        bv->addWidget(
            formkit::frow(body, PageMeta::tr("模式"), group.widget(), FieldWidth::Flex).row);
    }
    // 日期偏移 / 时间偏移：3×spin88（短档）+ 单位槽 + 范围注释（§7.1 逐行）
    const auto offset_row = [&](const QString &label, const std::array<QString, 3> &units,
                                const std::array<const char *, 3> &hooks, const QString &note,
                                int first_index) {
        TrackGroup group = make_group(body);
        for (int i = 0; i < 3; ++i) {
            if (i > 0) {
                group.gap(theme::Metrics::group_gap);
            }
            auto *spin = new DeltaSpin(group.widget(), QString::fromLatin1(hooks[std::size_t(i)]));
            st->time.delta[std::size_t(first_index) + std::size_t(i)] = spin;
            group.add(spin);
            group.gap(formkit::kInlineGap);
            group.add(unit_slot(group.widget(), units[std::size_t(i)]));
        }
        group.gap(theme::Metrics::group_gap);
        group.add(note_label(group.widget()));
        group.stretch();
        return formkit::frow(body, label, group.widget(), FieldWidth::Flex).row;
    };
    st->time.delta_row = offset_row(
        PageMeta::tr("日期偏移"), {PageMeta::tr("年"), PageMeta::tr("月"), PageMeta::tr("日")},
        {"pp-meta-delta-years", "pp-meta-delta-months", "pp-meta-delta-days"},
        PageMeta::tr("−99…+99"), 0);
    bv->addWidget(st->time.delta_row);
    st->time.time_row = offset_row(
        PageMeta::tr("时间偏移"), {PageMeta::tr("时"), PageMeta::tr("分"), PageMeta::tr("秒")},
        {"pp-meta-delta-hours", "pp-meta-delta-minutes", "pp-meta-delta-seconds"},
        PageMeta::tr("UTC±HH:MM"), 3);
    bv->addWidget(st->time.time_row);

    // 时区语义（从 / 到）：双列行（R2 标准档 160 —— A2 的第二标签 x = 276）
    {
        formkit::FieldCell left;
        formkit::FieldCell right;
        left.label = PageMeta::tr("从");
        right.label = PageMeta::tr("到");
        for (const bool is_from : {true, false}) {
            formkit::ComboSpec spec;
            spec.object_name = is_from ? objname::kTzFrom : objname::kTzTo;
            spec.accessible = is_from ? PageMeta::tr("源时区") : PageMeta::tr("目标时区");
            spec.tooltip = is_from ? PageMeta::tr("源时区（EXIF 记录所用的时区）")
                                   : PageMeta::tr("目标时区（改写后的墙钟时区）");
            auto *combo = formkit::combo_box(body, spec);
            fill_timezone_combo(combo);
            select_timezone(combo, 480);
            (is_from ? left : right).field = combo;
            (is_from ? st->time.tz_from : st->time.tz_to) = combo;
        }
        left.width = FieldWidth::Standard;
        right.width = FieldWidth::Standard;
        const formkit::DoubleRow row = formkit::double_row(body, left, right);
        row.row->setObjectName(objname::kTzRow);
        st->time.tz_row = row.row;
        st->time.tz_row->setVisible(false);
        bv->addWidget(st->time.tz_row);
    }

    // 生效时间（§7.1）：[标签 生效时间][原（删除线）→ 生效（强调）] + 「日期 · Δ · 字段名」注释
    {
        TrackGroup group = make_group(body);
        st->time.time_old = new ElidedLabel(group.widget());
        st->time.time_old->setObjectName(objname::kTimeOld0);
        st->time.time_old->setProperty(kFieldProperty, fieldprop::kOld);
        QFont old_font = theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true);
        old_font.setStrikeOut(true); // mockup .tline .old{text-decoration:line-through}
        st->time.time_old->setFont(old_font);
        group.add(st->time.time_old);
        group.gap(theme::Metrics::group_gap);
        st->time.time_arrow = new QLabel(QStringLiteral("→"), group.widget());
        st->time.time_arrow->setProperty(kFieldProperty, fieldprop::kArrow);
        st->time.time_arrow->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        group.add(st->time.time_arrow);
        group.gap(theme::Metrics::group_gap);
        st->time.time_new = new ElidedLabel(group.widget());
        st->time.time_new->setObjectName(objname::kTimeNew0);
        st->time.time_new->setProperty(kFieldProperty, fieldprop::kNew);
        st->time.time_new->setFont(
            theme::font(theme::Typography::body_px, QFont::Bold, /*tabular=*/true));
        group.add(st->time.time_new);
        group.gap(theme::Metrics::group_gap);
        st->time.time_note = note_label(group.widget(), objname::kTimeNote0);
        group.add(st->time.time_note);
        group.gap(formkit::kInlineGap);
        st->time.time_src = note_label(group.widget(), objname::kTimeSrc0);
        group.add(st->time.time_src);
        group.stretch();
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("生效时间"), group.widget(), FieldWidth::Flex);
        st->time.effect_label = row.label;
        row.row->setObjectName(objname::kEffectRow);
        st->time.effect_row = row.row;
        bv->addWidget(st->time.effect_row);
    }
    // 空态一行（修 1-31：未选择文件 → 一行说明）
    {
        st->time.time_empty = new ElidedLabel(body);
        st->time.time_empty->setObjectName(objname::kTimeEmpty);
        st->time.time_empty->setProperty(kFieldProperty, fieldprop::kEmpty);
        st->time.time_empty->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        st->time.time_empty->set_full_text(PageMeta::tr(kNoSelectionText));
        const formkit::Row row =
            formkit::frow(body, QString(), st->time.time_empty, FieldWidth::Flex);
        row.row->setObjectName(objname::kTimeEmptyRow);
        st->time.time_empty_row = row.row;
        st->time.time_empty_row->setVisible(false);
        bv->addWidget(st->time.time_empty_row);
    }
    // 多选逐行小表（§5.2；上限 8 行 + 「…还有 N 个」）
    {
        st->time.multi_host = new QWidget(body);
        st->time.multi_host->setObjectName(objname::kMulti);
        st->time.multi_grid = new QGridLayout(st->time.multi_host);
        st->time.multi_grid->setContentsMargins(0, 0, 0, 0);
        st->time.multi_grid->setHorizontalSpacing(theme::Metrics::form_col_gap);
        st->time.multi_grid->setVerticalSpacing(0);
        st->time.multi_grid->setColumnStretch(0, 3);
        st->time.multi_grid->setColumnStretch(1, 3);
        st->time.multi_grid->setColumnStretch(2, 0);
        st->time.multi_grid->setColumnStretch(3, 3);
        st->time.multi_more = new ElidedLabel(st->time.multi_host);
        st->time.multi_more->setObjectName(objname::kMultiMore);
        mark_resident(st->time.multi_more); // R-180：常驻子控件显式标记（禁删/禁重复入列）
        st->time.multi_more->setVisible(false);
        st->time.multi_more->setProperty(kFieldProperty, fieldprop::kMultiMore);
        st->time.multi_more->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        st->time.multi_host->setVisible(false);
        bv->addWidget(st->time.multi_host);
    }

    // ---- 连接（M13/C2.4：每个控件变化 → 对应 EditField 字段级提交）----
    // commit_field 内部：读控件 → 逐选中文件字段级合并 → 就地更新 follow.items → 发
    // selection_edits_committed（MainWindow 写模型）→ 只刷新生效值/聚合（不 reload 控件）。
    const auto commit = [st](EditField field) {
        if (st->core.loading) {
            return;
        }
        commit_field(st, field);
    };
    QObject::connect(st->time.seg, &formkit::SegmentedControl::current_changed, st,
                     [commit](int) { commit(EditField::TimeMode); });
    // 卡头开关三态（on/off/partial）：点击 → 该卡参与/退出（false = 清除该卡全部字段）
    QObject::connect(st->time.time_enable, &formkit::SwitchControl::toggled, st,
                     [commit](bool) { commit(EditField::TimeEnable); });
    for (int i = 0; i < kTimeComponentCount; ++i) {
        QObject::connect(st->time.delta[std::size_t(i)],
                         QOverload<int>::of(&QSpinBox::valueChanged), st,
                         [commit, i](int) { commit(time_component_field(i)); });
    }
    QObject::connect(st->time.tz_from, QOverload<int>::of(&QComboBox::currentIndexChanged), st,
                     [commit](int) { commit(EditField::TimeTzFrom); });
    QObject::connect(st->time.tz_to, QOverload<int>::of(&QComboBox::currentIndexChanged), st,
                     [commit](int) { commit(EditField::TimeTzTo); });
    v->addWidget(body);
    return card;
}

contextmenu::TimeCardIntents time_card_intents(MetaState *st) {
    contextmenu::TimeCardIntents in;
    in.running = st->core.page == nullptr || !st->core.page->isEnabled();
    in.enabled = st->time.time_enable != nullptr && st->time.time_enable->isChecked();
    in.enable_toggle = [st] {
        if (st->time.time_enable != nullptr)
            st->time.time_enable->toggle();
    };
    // M13/C2.6：右键「复位默认值 / 清除规则」= **清除选中文件该卡字段**（commit 卡开关
    // false：清 time_shift + time_enabled=false），随后重铺输入面（0 / UTC+8 / 三态）。
    const auto clear_selected = [st] {
        if (st->core.loading) {
            return;
        }
        {
            LoadingGuard guard(st); // 程序化置开关不逐控件发 rules_changed
            if (st->time.time_enable != nullptr) {
                st->time.time_enable->setChecked(false);
            }
        }
        commit_field(st, EditField::TimeEnable);
        load_inputs_from_selection(st);
    };
    in.reset_defaults = clear_selected;
    in.read_from_file = [st] {
        // 「从文件读取时间」= 重新取跟随项 probe + 按选择项聚合装载（不产生编辑）
        request_probe_scan(st);
        load_inputs_from_selection(st);
    };
    in.clear_rules = clear_selected;
    return in;
}

} // namespace pp::ui::meta
