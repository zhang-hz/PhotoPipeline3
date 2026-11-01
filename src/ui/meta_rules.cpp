// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页规则装配（B.1 拆分步 B3：meta_rules）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（步序 B3）；契约注释见 meta_rules.h。
// 实现 = page_meta.cpp 迁出（parse/时区表/commit_rule_change/tag 行/apply_state 逐字）+
// 规则装配**值聚合化**改写（RuleInputs 纯函数；输出逐字节不变，R-180 消灭 findChild 隐式约定）。
#include "ui/meta_rules.h"

#include "mapwidget/dms.h" // R-100：坐标解析失败回退 dms_to_deg
#include "ui/icons.h"
#include "ui/meta_probe_context.h" // R-182/R-258：钩子/词表单源
#include "ui/meta_state.h"
#include "ui/page_meta.h"

#include <QComboBox>
#include <QFileInfo> // M13：tags mixed 行的逐文件 tooltip（文件名）
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QPushButton>
#include <QSet> // M13：tags mixed 行跟踪
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace pp::ui::meta {

// ---------------------------------------------------------------------------
// 值聚合（GUI → RuleInputs；**唯一**读控件点）
// ---------------------------------------------------------------------------

RuleInputs rule_inputs(const MetaState *st) {
    RuleInputs in;
    in.time_enabled = st->time.time_enable != nullptr && st->time.time_enable->isChecked();
    in.gps_enabled = st->gps.gps_enable->isChecked();
    in.privacy_enabled =
        st->privacy.privacy_enable != nullptr && st->privacy.privacy_enable->isChecked();
    in.seg_index = st->time.seg->current_index();
    for (std::size_t i = 0; i < in.delta.size(); ++i) {
        in.delta[i] = st->time.delta[i]->value();
    }
    in.tz_from_min = st->time.tz_from->currentData().toInt();
    in.tz_to_min = st->time.tz_to->currentData().toInt();
    in.gps_clear = st->gps.clear_gps->isChecked();
    in.lat = st->gps.lat->text();
    in.lon = st->gps.lon->text();
    in.alt = st->gps.alt->text();
    in.dir = st->gps.dir->text();
    in.ts = st->gps.ts->text();
    in.tags.reserve(std::size_t(st->privacy.tag_edits.size()));
    for (const auto &row : st->privacy.tag_edits) {
        in.tags.emplace_back(row.first->text(), row.second->text());
    }
    in.strip_privacy = st->privacy.strip->isChecked();
    in.sync_mtime = st->privacy.mtime->isChecked();
    in.modernize_exif = st->privacy.modernize != nullptr && st->privacy.modernize->isChecked();
    return in;
}

// ---------------------------------------------------------------------------
// 规则装配（**唯一**规则源：rules() 与生效值显示都走这里 → 显示=写入）
// ---------------------------------------------------------------------------

pp::TimeShift current_time_shift(const RuleInputs &in) {
    pp::TimeShift shift;
    if (in.seg_index == 0) {
        shift.mode = pp::TimeShift::Mode::Delta;
        shift.years = in.delta[0];
        shift.months = in.delta[1];
        shift.days = in.delta[2];
        shift.hours = in.delta[3];
        shift.minutes = in.delta[4];
        shift.seconds = in.delta[5];
    } else {
        shift.mode = pp::TimeShift::Mode::TimezoneSemantic;
        shift.from_offset_min = in.tz_from_min;
        shift.to_offset_min = in.tz_to_min;
    }
    return shift;
}

pp::BatchRules build_rules(const RuleInputs &in) {
    pp::BatchRules rules;
    // UX-5（M7-WP7）：三卡「启用」开关 = 该卡是否参与规则。关闭 = build_rules **整卡跳过**
    // （参数保留但不装配、不写入）；打开 = 按当前参数装配。*_enabled 下传数据层（WP1 加性，
    // effective_rules 同判据）。
    rules.time_enabled = in.time_enabled;
    rules.gps_enabled = in.gps_enabled;
    rules.privacy_enabled = in.privacy_enabled;
    // 时间：全零 Δ / from==to 时区 = noop（写路径与显示路径同一判据跳过，§5.1）
    if (rules.time_enabled) {
        rules.time_shift = current_time_shift(in);
    }
    if (rules.gps_enabled) {
        // 清除 GPS 与 gps 互斥（gps_clear 优先）
        if (in.gps_clear) {
            rules.gps_clear = true;
        } else {
            double lat = 0, lon = 0;
            if (parse_coord(in.lat, -90.0, 90.0, lat) && parse_coord(in.lon, -180.0, 180.0, lon)) {
                pp::GpsData gps;
                gps.lat = lat;
                gps.lon = lon;
                gps.altitude = parse_optional(in.alt, -100000.0, 100000.0);
                gps.direction = parse_optional(in.dir, 0.0, 359.99);
                const QString stamp = in.ts.trimmed();
                if (!stamp.isEmpty()) {
                    gps.timestamp = stamp.toStdString();
                }
                rules.gps = gps;
            }
            // 非法坐标 → 忽略 gps（黄字提示由 refresh_gps_warning 负责）
        }
    }
    if (rules.privacy_enabled) {
        for (const auto &tag : in.tags) {
            const std::string key = tag.first.trimmed().toStdString();
            if (key.empty()) {
                continue;
            }
            const QString value = tag.second;
            pp::TagEdit edit;
            edit.key = key;
            if (value.isEmpty()) {
                edit.remove = true; // 值留空 = 删除该标签
            } else {
                edit.value = value.toStdString();
            }
            if (key.rfind("Xmp.", 0) == 0) {
                rules.xmp_edits.push_back(std::move(edit));
            } else {
                rules.exif_edits.push_back(std::move(edit));
            }
        }
        rules.strip_privacy = in.strip_privacy;
        rules.sync_mtime = in.sync_mtime;
        rules.modernize_exif = in.modernize_exif;
    }
    return rules;
}

// —— 兼容薄重载（调用点零改动）——
pp::TimeShift current_time_shift(const MetaState *st) {
    return current_time_shift(rule_inputs(st));
}

pp::BatchRules build_rules(const MetaState *st) { return build_rules(rule_inputs(st)); }

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------

// R-100（M7-WP7）：坐标解析**失败回退 dms_to_deg** —— 复制 "N 39°54.252'" 粘回规则框
// 不再被拒（显示层的度分读数可原样粘回）；纯十进制度仍走 toDouble 快路。
bool parse_coord(const QString &text_raw, double lo, double hi, double &out) {
    const QString text = text_raw.trimmed();
    bool ok = false;
    double v = text.toDouble(&ok);
    if (!ok) {
        const std::optional<double> parsed = pp::map::dms_to_deg(text);
        if (!parsed.has_value()) {
            return false;
        }
        v = *parsed;
    }
    if (!std::isfinite(v) || v < lo || v > hi) {
        return false;
    }
    out = v;
    return true;
}

std::optional<double> parse_optional(const QString &text_raw, double lo, double hi) {
    const QString text = text_raw.trimmed();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    bool ok = false;
    double v = text.toDouble(&ok);
    if (!ok) {
        const std::optional<double> parsed = pp::map::dms_to_deg(text);
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        v = *parsed;
    }
    if (!std::isfinite(v) || v < lo || v > hi) {
        return std::nullopt;
    }
    return v;
}

QString format_6(double v) { return QString::number(v, 'f', 6); }

// 时区下拉：UTC-12:00..UTC+14:00 整点 + 半时区/45 分时区；显示 "UTC+08:00"，userData = 分钟数
// R-233（M7-WP7）：魔法集合**显式化** —— 整点域 + 非整点实存偏移一张具名表（不再裸数字）；
// select_timezone 找不到时**不再静默回退 UTC+8**（+5:45 尼泊尔等合法偏移会被改写），
// 改为动态补一项保留原值。表外偏移（任意 ±MM:MM）同样按原值落位。
constexpr int kWholeHourOffsetCount = 27; // UTC-12:00 … UTC+14:00
// 实存非整点偏移（显式名单；R-233）：
//   -570 = UTC-09:30 Marquesas / -210 = UTC-03:30 Newfoundland / 210 = UTC+03:30 Iran /
//   270 = UTC+04:30 Afghanistan / 330 = UTC+05:30 India / 345 = UTC+05:45 Nepal /
//   390 = UTC+06:30 Myanmar / 525 = UTC+08:45 Eucla / 570 = UTC+09:30 ACST / 630 = UTC+10:30 Lord
//   Howe
constexpr int kFractionalHourOffsets[] = {-570, -210, 210, 270, 330, 345, 390, 525, 570, 630};

void fill_timezone_combo(QComboBox *combo) {
    std::vector<int> offsets;
    offsets.reserve(kWholeHourOffsetCount + std::size(kFractionalHourOffsets));
    for (int h = -12; h <= 14; ++h) {
        offsets.push_back(h * 60);
    }
    for (const int minute : kFractionalHourOffsets) {
        offsets.push_back(minute);
    }
    std::sort(offsets.begin(), offsets.end());
    for (const int off : offsets) {
        combo->addItem(
            QStringLiteral("UTC%1").arg(QString::fromStdString(pp::offset_time_string(off))), off);
    }
}

void select_timezone(QComboBox *combo, int offset_min) {
    int idx = combo->findData(offset_min);
    if (idx < 0) {
        // R-233：合法但不在预置表的偏移（如历史/特区偏移）→ 动态补一项**保留原值**；
        // 旧行为静默回退 UTC+8 会把 +5:45 之类的值改写成 +8:00（拍摄时刻语义被改错）。
        combo->addItem(
            QStringLiteral("UTC%1").arg(QString::fromStdString(pp::offset_time_string(offset_min))),
            offset_min);
        idx = combo->findData(offset_min);
    }
    combo->setCurrentIndex(idx);
}

// QLineEdit 薄包装（既有调用点零改动）：文本取值后走同一纯解析。
bool parse_coord(const QLineEdit *edit, double lo, double hi, double &out) {
    return parse_coord(edit->text(), lo, hi, out);
}

std::optional<double> parse_optional(const QLineEdit *edit, double lo, double hi) {
    return parse_optional(edit->text(), lo, hi);
}

// UX-4⑥（M7-WP7）：规则面变更的**统一收口**（原散在各接线 lambda 的四连刷）：输入态/黄字
// 警告/生效值显示/规则变更通知。

void commit_rule_change(MetaState *st) {
    refresh_gps_inputs(st);
    refresh_gps_warning(st);
    refresh_effective(st);
    notify_rules_changed(st);
}

// ---------------------------------------------------------------------------
// apply_rules（预设载入；不发信号）
// ---------------------------------------------------------------------------

void clear_tag_rows(MetaState *st) {
    st->privacy.tag_edits.clear(); // R-180：显式清单与布局同生共死
    while (st->privacy.tag_rows->count() > 0) {
        QLayoutItem *item = st->privacy.tag_rows->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
}

void add_tag_row(MetaState *st, const QString &key, const QString &value) {
    auto *row = new QWidget();
    row->setObjectName(objname::kTagRow);
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(theme::Metrics::group_gap);

    auto *key_edit = new QLineEdit(row);
    key_edit->setObjectName(objname::kTagKey);
    key_edit->setProperty(kFieldProperty, fieldprop::kRule);
    key_edit->setFixedHeight(formkit::kControlH);
    key_edit->setPlaceholderText(PageMeta::tr("Exif.Image.Artist 或 Xmp.dc.title"));
    key_edit->setText(key);
    auto *value_edit = new QLineEdit(row);
    value_edit->setObjectName(objname::kTagValue);
    value_edit->setProperty(kFieldProperty, fieldprop::kRule);
    value_edit->setFixedHeight(formkit::kControlH);
    value_edit->setText(value);
    value_edit->setToolTip(PageMeta::tr("值留空 = 删除该标签"));
    auto *remove = new QPushButton(row);
    remove->setObjectName(objname::kTagRemove);
    remove->setFixedSize(formkit::kIconBtn, formkit::kIconBtn); // 图标钮 28×28（R4）
    remove->setToolTip(PageMeta::tr("删除此行"));
    remove->setAccessibleName(PageMeta::tr("删除此行")); // §11：图标钮必有 accessibleName
    icons::apply(remove, icons::Name::Clear, st->core.tokens.text2, st->core.tokens.text3, 12);

    lay->addWidget(key_edit, 2);
    lay->addWidget(value_edit, 3);
    lay->addWidget(remove, 0);
    st->privacy.tag_rows->addWidget(row);
    // R-180（B3）：行内编辑器**显式登记**（键值对清单）——build_rules/rule_inputs
    // 不再 findChild("tag_key") 反查 objectName 隐式约定；objectName 仍留给探针。
    st->privacy.tag_edits.push_back({key_edit, value_edit});

    QObject::connect(key_edit, &QLineEdit::textEdited, st, [st] {
        notify_rules_changed(st);
        refresh_effective(st);
        commit_field(st, EditField::Tags); // M13：标签行编辑 = Tags 整表提交（选中文件）
    });
    QObject::connect(value_edit, &QLineEdit::textEdited, st, [st] {
        notify_rules_changed(st);
        refresh_effective(st);
        commit_field(st, EditField::Tags);
    });
    QObject::connect(remove, &QPushButton::clicked, st, [st, row, key_edit] {
        st->privacy.tag_rows->removeWidget(row);
        for (int i = 0; i < st->privacy.tag_edits.size(); ++i) {
            if (st->privacy.tag_edits[i].first == key_edit) {
                st->privacy.tag_edits.removeAt(i); // 与布局同序同行移除
                break;
            }
        }
        row->hide();
        row->deleteLater();
        notify_rules_changed(st);
        refresh_effective(st);
        commit_field(st, EditField::Tags); // 删除行 = 从选中文件移除该标签（baseline 差分）
    });
}

void apply_state(MetaState *st, const pp::BatchRules &r) {
    { // R-180：加载协议 RAII（作用域尾恢复，再进 refresh_all —— 与原顺序逐字一致）
        LoadingGuard guard(st);

        if (r.time_shift.has_value()) {
            const pp::TimeShift &s = *r.time_shift;
            st->time.seg->set_current_index(s.mode == pp::TimeShift::Mode::Delta ? 0 : 1);
            st->time.delta[0]->setValue(s.years);
            st->time.delta[1]->setValue(s.months);
            st->time.delta[2]->setValue(s.days);
            st->time.delta[3]->setValue(s.hours);
            st->time.delta[4]->setValue(s.minutes);
            st->time.delta[5]->setValue(s.seconds);
            select_timezone(st->time.tz_from, s.from_offset_min);
            select_timezone(st->time.tz_to, s.to_offset_min);
        } else {
            // 无时间规则 = 全零 Δ（noop；与"未配置"在两条路径上等价）
            st->time.seg->set_current_index(0);
            for (QSpinBox *spin : st->time.delta) {
                spin->setValue(0);
            }
        }

        st->gps.clear_gps->setChecked(r.gps_clear);
        if (r.gps.has_value()) {
            st->gps.lat->setText(format_6(r.gps->lat));
            st->gps.lon->setText(format_6(r.gps->lon));
            st->gps.alt->setText(r.gps->altitude.has_value() ? format_6(*r.gps->altitude)
                                                             : QString());
            st->gps.dir->setText(r.gps->direction.has_value() ? format_6(*r.gps->direction)
                                                              : QString());
            st->gps.ts->setText(r.gps->timestamp.has_value()
                                    ? QString::fromStdString(*r.gps->timestamp)
                                    : QString());
            st->gps.gps_status->setText(PageMeta::tr("已载入批量 GPS 规则"));
        } else {
            st->gps.lat->clear();
            st->gps.lon->clear();
            st->gps.alt->clear();
            st->gps.dir->clear();
            st->gps.ts->clear();
        }

        clear_tag_rows(st);
        for (const pp::TagEdit &edit : r.exif_edits) {
            add_tag_row(st, QString::fromStdString(edit.key),
                        edit.value.has_value() ? QString::fromStdString(*edit.value) : QString());
        }
        for (const pp::TagEdit &edit : r.xmp_edits) {
            add_tag_row(st, QString::fromStdString(edit.key),
                        edit.value.has_value() ? QString::fromStdString(*edit.value) : QString());
        }

        st->privacy.strip->setChecked(r.strip_privacy);
        st->privacy.mtime->setChecked(r.sync_mtime);
        if (st->privacy.modernize != nullptr)
            st->privacy.modernize->setChecked(r.modernize_exif);

        // UX-5：恢复**含值旧状态 = 开**（旧预设缺 *_enabled 键 → 默认 true，带值即开关置开）；
        // 显式 *_enabled=false 优先（预设里明确关掉的卡不复活）。新会话/空值 = 关（零变化）。
        const bool time_on = r.time_enabled && r.time_shift.has_value();
        const bool gps_on = r.gps_enabled && (r.gps.has_value() || r.gps_clear);
        const bool privacy_on =
            r.privacy_enabled && (r.strip_privacy || r.sync_mtime || r.modernize_exif ||
                                  !r.exif_edits.empty() || !r.xmp_edits.empty());
        st->time.time_enable->setChecked(time_on);
        st->privacy.privacy_enable->setChecked(privacy_on);
        set_gps_rule_enabled(st, gps_on);
    }
    refresh_all(st);
}

// ===========================================================================
// M13（0.4.1）/ C2：选择项级编辑 —— UI 薄层（纯内核 = ui/meta_selection.{h,cpp}）
//
// 职责边界（C2.1）：唯一可写状态 = st->follow.items[i].exception；本段只做
//   控件读值 → 纯函数合并 → 就地更新 follow.items → 发 selection_edits_committed →
//   刷新生效值/聚合（**不** reload 控件，避免打字光标被重置）。
// ===========================================================================
namespace {

using M13Override = pp::MetadataOverride;

QString mixed_tooltip_text(const QStringList &labels) {
    QString tip = PageMeta::tr(kMixedValueText);
    if (!labels.isEmpty()) {
        tip += QLatin1Char('\n') + labels.join(QStringLiteral("\n"));
    }
    return tip;
}

// 每文件生效 GPS（GpsLat/GpsLon/GpsAlt/GpsDir/GpsTs 的 base）：override.gps 优先；
// 否则 probe 到位时用 preview_effective(probe, {}, exception).gps.effective（源文件读回）；
// probe 未到位 → 退回 override.gps（nullopt = 无点）。
std::optional<pp::GpsData> effective_gps_for(const MetaState *st, int index) {
    if (index < 0 || index >= st->follow.items.size()) {
        return std::nullopt;
    }
    const std::optional<pp::MetadataOverride> ex = st->follow.items.at(index).exception;
    if (ex.has_value() && ex->gps.has_value()) {
        return ex->gps;
    }
    if (index < static_cast<int>(st->follow.probes.size())) {
        const pp::EffectivePreview preview =
            pp::preview_effective(st->follow.probes[std::size_t(index)].meta, pp::BatchRules{}, ex);
        return preview.gps.effective;
    }
    return ex.has_value() ? ex->gps : std::nullopt;
}

void emit_selection_edits(MetaState *st, const QList<MetaSelectionEdit> &edits) {
    if (st->core.page == nullptr || edits.isEmpty()) {
        return;
    }
    // 信号在 Qt6 为 public：薄层自由函数直接发射（Q_OBJECT 的 moc 定义同 TU 链）。
    emit st->core.page->selection_edits_committed(edits);
}

// 逐文件提交的公共收口：merge(item, index) 返回该文件合并后的 override（nullopt = 无修改）。
// 结果就地写回 follow.items（真源）→ 发增量信号 → 只刷新生效值/聚合。
template <class MergeFn> void commit_selection(MetaState *st, MergeFn merge) {
    QList<MetaSelectionEdit> edits;
    edits.reserve(st->follow.items.size());
    for (int i = 0; i < st->follow.items.size(); ++i) {
        MetaSelectionItem &item = st->follow.items[i];
        const std::optional<M13Override> merged = merge(item, i);
        item.exception = merged;
        MetaSelectionEdit edit;
        edit.path = item.path;
        edit.edit = merged;
        edits.append(edit);
    }
    emit_selection_edits(st, edits);
    refresh_effective(st);
}

// 读取 GPS 文本分量为 optional：空文本 = 清空（nullopt）；非法非空文本 → ok=false（不提交）
bool read_optional_field(const QLineEdit *edit, double lo, double hi, std::optional<double> &out,
                         bool &ok) {
    ok = true;
    const QString text = edit != nullptr ? edit->text().trimmed() : QString();
    if (text.isEmpty()) {
        out = std::nullopt;
        return true;
    }
    const std::optional<double> value = parse_optional(text, lo, hi);
    if (!value.has_value()) {
        ok = false;
        return false;
    }
    out = value;
    return true;
}

// Tags 行 → 提交输入（mixed 值行未编辑前不参与整表提交；删除的行按 baseline 差分移除）
void read_tag_input(MetaState *st, std::vector<pp::TagEdit> &tags,
                    std::vector<std::string> &remove_tags, QStringList &current_keys) {
    for (const auto &row : st->privacy.tag_edits) {
        const QString key = row.first != nullptr ? row.first->text().trimmed() : QString();
        if (key.isEmpty()) {
            continue;
        }
        current_keys << key;
        if (st->selection.tags_mixed.contains(row.second)) {
            continue; // mixed 行保持各文件原值（C2.4「标签只展示选择集中完全相同的编辑行」）
        }
        pp::TagEdit edit;
        edit.key = key.toStdString();
        const QString value = row.second != nullptr ? row.second->text() : QString();
        if (value.isEmpty()) {
            edit.remove = true; // 值留空 = 删除该标签（既有语义）
        } else {
            edit.value = value.toStdString();
        }
        tags.push_back(std::move(edit));
    }
    for (const QString &key : st->selection.tags_baseline) {
        if (!current_keys.contains(key)) {
            remove_tags.push_back(key.toStdString());
        }
    }
}

} // namespace

// —— 选择项快照（follow.items → 纯值投影）——
SelectionSnapshotList selection_snapshots(const MetaState *st) {
    SelectionSnapshotList out;
    out.reserve(static_cast<std::size_t>(st->follow.items.size()));
    for (const MetaSelectionItem &item : st->follow.items) {
        SelectionSnapshot snapshot;
        snapshot.path = item.path;
        snapshot.exception = item.exception;
        out.push_back(std::move(snapshot));
    }
    return out;
}

pp::BatchRules rules_from_selection_state(const MetaState *st) {
    return batch_rules_from_selection(selection_snapshots(st));
}

// —— mixed 视觉 helper（C2.3；ppMixed 动态属性 + QSS 锚 = hook::kMixedProperty）——
void apply_mixed_widget(QWidget *widget, bool mixed, const QStringList &values) {
    if (widget == nullptr) {
        return;
    }
    widget->setProperty(hook::kMixedProperty, mixed);
    widget->setToolTip(mixed ? mixed_tooltip_text(values) : QString());
    theme::repolish(widget);
}

void clear_mixed(QLineEdit *edit) {
    if (edit == nullptr) {
        return;
    }
    edit->setProperty(hook::kMixedProperty, false);
    edit->setToolTip(QString());
    // placeholder 由调用方按控件语义恢复（输入示例 / 空态）
    theme::repolish(edit);
}

void apply_mixed(QLineEdit *edit, bool mixed, const QStringList &values) {
    if (edit == nullptr) {
        return;
    }
    if (!mixed) {
        clear_mixed(edit);
        return;
    }
    edit->setText(QString()); // 空文本 + placeholder「多个值」（C2.3 渲染约定）
    edit->setPlaceholderText(PageMeta::tr(kMixedValueText));
    apply_mixed_widget(edit, true, values);
}

void apply_mixed_combo(QComboBox *combo, bool mixed, const QStringList &values) {
    apply_mixed_widget(combo, mixed, values);
}

// —— 单字段提交（C2.4）——
void commit_field(MetaState *st, EditField field) {
    if (st == nullptr || st->core.page == nullptr || st->core.loading) {
        return;
    }
    if (st->follow.items.isEmpty()) {
        return;
    }
    FieldInput input;
    // 1) 读控件现值（薄层：只有这里碰控件）
    switch (field) {
    case EditField::TimeEnable:
        input.flag = st->time.time_enable != nullptr && st->time.time_enable->isChecked();
        break;
    case EditField::TimeMode:
        input.mode = st->time.seg->current_index() == 0 ? pp::TimeShift::Mode::Delta
                                                        : pp::TimeShift::Mode::TimezoneSemantic;
        break;
    case EditField::TimeYears:
    case EditField::TimeMonths:
    case EditField::TimeDays:
    case EditField::TimeHours:
    case EditField::TimeMinutes:
    case EditField::TimeSeconds: {
        const int index = time_component_index(field);
        input.value = st->time.delta[std::size_t(index)]->value();
        break;
    }
    case EditField::TimeTzFrom:
        input.value = st->time.tz_from->currentData().toInt();
        break;
    case EditField::TimeTzTo:
        input.value = st->time.tz_to->currentData().toInt();
        break;
    case EditField::GpsEnable:
        input.flag = st->gps.gps_enable_switch != nullptr
                         ? st->gps.gps_enable_switch->isChecked()
                         : (st->gps.gps_enable != nullptr && st->gps.gps_enable->isChecked());
        break;
    case EditField::GpsClear:
        input.flag = st->gps.clear_gps != nullptr && st->gps.clear_gps->isChecked();
        break;
    case EditField::GpsLat:
    case EditField::GpsLon: {
        double lat = 0.0;
        double lon = 0.0;
        input.has_lat_lon = parse_coord(st->gps.lat, -90.0, 90.0, lat) &&
                            parse_coord(st->gps.lon, -180.0, 180.0, lon);
        if (!input.has_lat_lon) {
            refresh_gps_warning(st); // 黄字提示走既有通道（C2.4：不提交）
            return;
        }
        input.lat = lat;
        input.lon = lon;
        break;
    }
    case EditField::GpsAlt: {
        bool ok = false;
        read_optional_field(st->gps.alt, -100000.0, 100000.0, input.altitude, ok);
        if (!ok) {
            return;
        }
        break;
    }
    case EditField::GpsDir: {
        bool ok = false;
        read_optional_field(st->gps.dir, 0.0, 359.99, input.direction, ok);
        if (!ok) {
            return;
        }
        break;
    }
    case EditField::GpsTs: {
        const QString text = st->gps.ts != nullptr ? st->gps.ts->text().trimmed() : QString();
        input.timestamp = text.isEmpty() ? std::nullopt : std::optional<QString>(text);
        break;
    }
    case EditField::PrivacyEnable:
        input.flag =
            st->privacy.privacy_enable != nullptr && st->privacy.privacy_enable->isChecked();
        break;
    case EditField::PrivacyStrip:
        input.flag = st->privacy.strip != nullptr && st->privacy.strip->isChecked();
        break;
    case EditField::PrivacyMtime:
        input.flag = st->privacy.mtime != nullptr && st->privacy.mtime->isChecked();
        break;
    case EditField::PrivacyModernize:
        input.flag = st->privacy.modernize != nullptr && st->privacy.modernize->isChecked();
        break;
    case EditField::PrivacyCreatorTool:
        input.flag = st->privacy.creator_tool != nullptr && st->privacy.creator_tool->isChecked();
        break;
    case EditField::Tags: {
        QStringList current_keys;
        read_tag_input(st, input.tags, input.remove_tags, current_keys);
        // 提交后 baseline 与新行集合对齐（删除行已按差分移除）
        st->selection.tags_baseline = current_keys;
        // 已不存在的 mixed 行从跟踪集移除（指针悬垂防护）
        QSet<QLineEdit *> alive;
        for (const auto &row : st->privacy.tag_edits) {
            if (row.second != nullptr) {
                alive.insert(row.second);
            }
        }
        st->selection.tags_mixed.intersect(alive);
        break;
    }
    }
    // 1b) 时间字段新建 time_shift 的初值 = 当前控件显示值（C2.4 显示=写入）：
    //     用户把"无时间修改"的文件切到 Δ/时区模式时，新 override 必须与界面所见一致
    //     （否则界面显示 UTC+8 而 override 落 (0,0) —— 两张皮）。已有 shift 的文件仍只改
    //     被编辑字段（纯合并函数保证）。
    if (field == EditField::TimeMode || time_component_index(field) >= 0 ||
        field == EditField::TimeTzFrom || field == EditField::TimeTzTo) {
        pp::TimeShift seed;
        seed.mode = st->time.seg->current_index() == 0 ? pp::TimeShift::Mode::Delta
                                                       : pp::TimeShift::Mode::TimezoneSemantic;
        seed.years = st->time.delta[0]->value();
        seed.months = st->time.delta[1]->value();
        seed.days = st->time.delta[2]->value();
        seed.hours = st->time.delta[3]->value();
        seed.minutes = st->time.delta[4]->value();
        seed.seconds = st->time.delta[5]->value();
        seed.from_offset_min = st->time.tz_from->currentData().toInt();
        seed.to_offset_min = st->time.tz_to->currentData().toInt();
        input.seed_time = seed;
    }
    // 2) 字段级合并（每文件独立：GPS 分量用该文件生效 GPS 作 base 保留 alt/dir/ts）
    if (is_gps_component_field(field) || field == EditField::GpsClear) {
        commit_selection(st, [&](const MetaSelectionItem &item, int index) {
            return merge_gps_field_edit(item.exception, effective_gps_for(st, index), field, input);
        });
    } else {
        commit_selection(st, [&](const MetaSelectionItem &item, int) {
            return merge_field_edit(item.exception, field, input);
        });
    }
    // 3) 提交后只补控件可用性 + 清掉本字段的 mixed 视觉残留（不重铺其它输入文本）
    if (field == EditField::TimeEnable) {
        // 本次提交把选择集统一成 on/off → 开关回到二态（partial 只表示"选择集不一致"）
        const BoolAggregate agg =
            card_active_aggregate(selection_snapshots(st), EditField::TimeEnable);
        const QSignalBlocker blocker(st->time.time_enable);
        if (agg.state == Aggregate::Uniform) {
            st->time.time_enable->set_partial(false);
            st->time.time_enable->setChecked(agg.value);
        } else {
            st->time.time_enable->set_partial(true);
        }
    }
    if (field == EditField::TimeMode) {
        apply_mixed_widget(st->time.seg, false, {});
    }
    if (field == EditField::TimeTzFrom) {
        apply_mixed_combo(st->time.tz_from, false, {});
    }
    if (field == EditField::TimeTzTo) {
        apply_mixed_combo(st->time.tz_to, false, {});
    }
    if (const int index = time_component_index(field); index >= 0) {
        QSpinBox *spin = st->time.delta[std::size_t(index)];
        static_cast<DeltaSpin *>(spin)->set_mixed(false);
        apply_mixed_widget(spin, false, {});
    }
    if (field == EditField::TimeEnable || field == EditField::TimeMode ||
        time_component_index(field) >= 0 || field == EditField::TimeTzFrom ||
        field == EditField::TimeTzTo) {
        refresh_time_inputs(st);
    }
    if (field == EditField::GpsEnable || field == EditField::GpsClear) {
        refresh_gps_inputs(st);
    }
    if (field == EditField::PrivacyEnable) {
        refresh_privacy_inputs(st);
    }
}

// —— 输入面装载（C2.3）——
namespace {

// GPS 输入面（lat/lon/alt/dir/ts）← 选择项**生效 GPS 聚合**（override 优先 / 源读回兜底）。
// 返回 mixed 字段数（供 hook::kMixedFields 汇总）。
int load_gps_inputs_impl(MetaState *st) {
    const SelectionSnapshotList sel = selection_snapshots(st);
    SelectionSnapshotList effective = sel;
    for (int i = 0; i < static_cast<int>(effective.size()); ++i) {
        std::optional<pp::GpsData> gps;
        const std::optional<pp::MetadataOverride> &ex = sel[std::size_t(i)].exception;
        if (ex.has_value() && ex->gps.has_value()) {
            gps = ex->gps;
        } else if (i < static_cast<int>(st->follow.probes.size())) {
            const pp::EffectivePreview preview =
                pp::preview_effective(st->follow.probes[std::size_t(i)].meta, pp::BatchRules{}, ex);
            gps = preview.gps.effective;
        }
        effective[std::size_t(i)].exception = gps.has_value() ? normalize_override([&gps] {
            M13Override tmp;
            tmp.gps = gps;
            return tmp;
        }())
                                                              : std::nullopt;
    }
    int mixed = 0;
    const auto load_text = [&](QLineEdit *edit, const QString &text, bool is_mixed,
                               const QStringList &labels, const QString &placeholder) {
        if (edit == nullptr) {
            return;
        }
        if (is_mixed) {
            apply_mixed(edit, true, labels);
            ++mixed;
            return;
        }
        apply_mixed(edit, false, {});
        edit->setText(text);
        edit->setPlaceholderText(placeholder);
    };
    const auto text_or_empty = [](const std::optional<double> &value) {
        return value.has_value() ? format_6(*value) : QString();
    };

    const DoubleAggregate lat = gps_lat_aggregate(effective);
    load_text(st->gps.lat, lat.state == Aggregate::Uniform ? format_6(lat.value) : QString(),
              lat.state == Aggregate::Mixed, lat.labels, PageMeta::tr(kLatPlaceholder));
    const DoubleAggregate lon = gps_lon_aggregate(effective);
    load_text(st->gps.lon, lon.state == Aggregate::Uniform ? format_6(lon.value) : QString(),
              lon.state == Aggregate::Mixed, lon.labels, PageMeta::tr(kLonPlaceholder));
    const OptionalDoubleAggregate alt = gps_alt_aggregate(effective);
    load_text(st->gps.alt, alt.state == Aggregate::Uniform ? text_or_empty(alt.value) : QString(),
              alt.state == Aggregate::Mixed, alt.labels, PageMeta::tr(kAltPlaceholder));
    const OptionalDoubleAggregate dir = gps_dir_aggregate(effective);
    load_text(st->gps.dir, dir.state == Aggregate::Uniform ? text_or_empty(dir.value) : QString(),
              dir.state == Aggregate::Mixed, dir.labels, PageMeta::tr(kDirPlaceholder));
    const StringAggregate ts = gps_timestamp_aggregate(effective);
    load_text(st->gps.ts, ts.state == Aggregate::Uniform ? ts.value : QString(),
              ts.state == Aggregate::Mixed, ts.labels, PageMeta::tr(kTsPlaceholder));
    return mixed;
}

// 三态开关装载（on / off / partial）；返回 mixed 计数（partial 时 +1）
int load_switch(MetaState *st, formkit::SwitchControl *sw, const BoolAggregate &agg) {
    if (sw == nullptr) {
        return 0;
    }
    const QSignalBlocker blocker(sw);
    if (agg.state == Aggregate::Uniform && agg.value) {
        sw->set_partial(false);
        sw->setChecked(true);
        sw->setToolTip(PageMeta::tr(kCardEnableTooltip));
        apply_mixed_widget(sw, false, {});
        return 0;
    }
    if (agg.state == Aggregate::Mixed) {
        sw->setChecked(false);
        sw->set_partial(true);
        apply_mixed_widget(sw, true, agg.labels);
        return 1;
    }
    sw->setChecked(false); // off（含 absent）
    sw->set_partial(false);
    sw->setToolTip(PageMeta::tr(kCardEnableTooltip));
    apply_mixed_widget(sw, false, {});
    return 0;
}

// Tags 编辑行装载：uniform 行可编辑；mixed/仅部分文件有的键 → 只读「多个值」行 + tooltip。
void load_tag_rows(MetaState *st, const SelectionSnapshotList &sel) {
    clear_tag_rows(st);
    st->selection.tags_baseline.clear();
    st->selection.tags_mixed.clear();
    struct KeyState {
        int present = 0;
        bool uniform = true;
        bool remove = false;
        QString value;
        QStringList labels;
    };
    QHash<QString, KeyState> states;
    QStringList order;
    for (const SelectionSnapshot &snapshot : sel) {
        QHash<QString, pp::TagEdit> file_edits;
        // 显式指针 + nullptr 判据（clang-tidy bugprone-unchecked-optional-access 可识别；
        // 原先 optional 两次 -> 解引用只有第一处被识别，第二处误报）
        const pp::MetadataOverride *ex =
            snapshot.exception.has_value() ? &*snapshot.exception : nullptr;
        if (ex != nullptr) {
            for (const pp::TagEdit &edit : ex->exif_edits) {
                file_edits.insert(QString::fromStdString(edit.key), edit);
            }
            for (const pp::TagEdit &edit : ex->xmp_edits) {
                file_edits.insert(QString::fromStdString(edit.key), edit);
            }
        }
        for (const pp::TagEdit &edit : file_edits) {
            const QString key = QString::fromStdString(edit.key);
            if (!states.contains(key)) {
                order << key;
            }
            KeyState &state = states[key];
            ++state.present;
            const QString text =
                edit.remove
                    ? QStringLiteral("<删除>")
                    : (edit.value.has_value() ? QString::fromStdString(*edit.value) : QString());
            if (state.present == 1) {
                state.remove = edit.remove;
                state.value = text;
            } else if (state.remove != edit.remove || state.value != text) {
                state.uniform = false;
            }
            if (state.labels.size() < kAggregateLabelCap) {
                state.labels << QFileInfo(snapshot.path).fileName() + QStringLiteral(": ") + text;
            }
        }
    }
    for (const QString &key : order) {
        const KeyState &state = states[key];
        const bool uniform =
            state.uniform && state.present == static_cast<int>(sel.size()) && !sel.empty();
        if (uniform) {
            add_tag_row(st, key, state.remove ? QString() : state.value);
            st->selection.tags_baseline << key;
            continue;
        }
        add_tag_row(st, key, QString());
        auto &row = st->privacy.tag_edits.last();
        if (row.second != nullptr) {
            row.second->setReadOnly(true);
            apply_mixed(row.second, true, state.labels);
            st->selection.tags_mixed.insert(row.second);
        }
        st->selection.tags_baseline << key;
    }
}

} // namespace

void load_gps_inputs_from_selection(MetaState *st) {
    if (st == nullptr || st->core.page == nullptr) {
        return;
    }
    const LoadingGuard guard(st);
    load_gps_inputs_impl(st);
}

void load_inputs_from_selection(MetaState *st) {
    if (st == nullptr || st->core.page == nullptr) {
        return;
    }
    const LoadingGuard guard(st);
    st->selection.has_selection = st->follow.from_selection;
    const SelectionSnapshotList sel = selection_snapshots(st);
    int mixed = 0;
    // —— 时间卡：模式 / 六个 Δ / 两个时区下垃 ——
    const ModeAggregate mode = time_mode_aggregate(sel);
    {
        const QSignalBlocker blocker(st->time.seg);
        if (mode.state == Aggregate::Uniform) {
            st->time.seg->set_current_index(mode.value == pp::TimeShift::Mode::Delta ? 0 : 1);
            apply_mixed_widget(st->time.seg, false, {});
        } else {
            st->time.seg->set_current_index(0);
            if (mode.state == Aggregate::Mixed) {
                apply_mixed_widget(st->time.seg, true, mode.labels);
                ++mixed;
            } else {
                apply_mixed_widget(st->time.seg, false, {});
            }
        }
    }
    for (int i = 0; i < kTimeComponentCount; ++i) {
        QSpinBox *spin = st->time.delta[std::size_t(i)];
        if (spin == nullptr) {
            continue;
        }
        const IntAggregate agg = time_component_aggregate(sel, i);
        auto *delta = static_cast<DeltaSpin *>(spin);
        if (agg.state == Aggregate::Uniform) {
            delta->set_mixed(false);
            spin->setValue(agg.value);
            apply_mixed_widget(spin, false, {});
        } else if (agg.state == Aggregate::Mixed) {
            delta->set_mixed(true);
            apply_mixed_widget(spin, true, agg.labels);
            ++mixed;
        } else {
            delta->set_mixed(false);
            spin->setValue(0);
            apply_mixed_widget(spin, false, {});
        }
    }
    {
        const IntAggregate from = timezone_offset_aggregate(sel, true);
        const QSignalBlocker blocker(st->time.tz_from);
        if (from.state == Aggregate::Mixed) {
            select_timezone(st->time.tz_from, from.value);
            apply_mixed_combo(st->time.tz_from, true, from.labels);
            ++mixed;
        } else {
            select_timezone(st->time.tz_from, from.state == Aggregate::Uniform ? from.value : 480);
            apply_mixed_combo(st->time.tz_from, false, {});
        }
    }
    {
        const IntAggregate to = timezone_offset_aggregate(sel, false);
        const QSignalBlocker blocker(st->time.tz_to);
        if (to.state == Aggregate::Mixed) {
            select_timezone(st->time.tz_to, to.value);
            apply_mixed_combo(st->time.tz_to, true, to.labels);
            ++mixed;
        } else {
            select_timezone(st->time.tz_to, to.state == Aggregate::Uniform ? to.value : 480);
            apply_mixed_combo(st->time.tz_to, false, {});
        }
    }
    // —— 三卡 enable 三态（on / off / partial）——
    mixed +=
        load_switch(st, st->time.time_enable, card_active_aggregate(sel, EditField::TimeEnable));
    mixed += load_switch(st, st->gps.gps_enable_switch,
                         card_active_aggregate(sel, EditField::GpsEnable));
    mixed += load_switch(st, st->privacy.privacy_enable,
                         card_active_aggregate(sel, EditField::PrivacyEnable));
    // —— GPS 输入面（生效坐标聚合）+ Tags 编辑行 ——
    mixed += load_gps_inputs_impl(st);
    load_tag_rows(st, sel);
    // —— C2.3 的 mixed 字段数补齐：不在上述装载路径里的独立字段（GPS 清除 / 隐私四开关 /
    //    Tags 整表）—— 探针按 hook::kMixedFields 读「当前聚合里 mixed 的字段数」。 ——
    if (gps_clear_aggregate(sel).state == Aggregate::Mixed) {
        ++mixed;
    }
    for (const EditField flag : {EditField::PrivacyStrip, EditField::PrivacyMtime,
                                 EditField::PrivacyModernize, EditField::PrivacyCreatorTool}) {
        if (flag_aggregate(sel, flag).state == Aggregate::Mixed) {
            ++mixed;
        }
    }
    if (tags_aggregate(sel).state == Aggregate::Mixed) {
        ++mixed;
    }
    // —— 卡片可用性/三态视觉唯一落点（ui-gps 实现；不改输入文本）——
    refresh_time_inputs(st);
    refresh_gps_inputs(st);
    refresh_privacy_inputs(st);
    // mixed 字段数自验钩子（C2.3；同时挂 MetaState 与页面）
    st->selection.mixed_fields = mixed;
    set_hook(st, hook::kMixedFields, mixed);
}

// —— 预设载入（C2.6）：BatchRules 替换式 → 选中文件 override ——
void apply_preset_rules(MetaState *st, const pp::BatchRules &r) {
    if (st == nullptr || st->core.page == nullptr) {
        return;
    }
    {
        const LoadingGuard guard(st);
        QList<MetaSelectionEdit> edits;
        for (MetaSelectionItem &item : st->follow.items) {
            M13Override ov;
            if (r.time_shift.has_value()) {
                ov.time_shift = r.time_shift;
                ov.time_enabled = true;
            }
            if (r.gps_clear) {
                ov.gps_clear = true;
                ov.gps_enabled = true;
            } else if (r.gps.has_value()) {
                ov.gps = r.gps;
                ov.gps_enabled = true;
            }
            ov.exif_edits = r.exif_edits;
            ov.xmp_edits = r.xmp_edits;
            if (r.strip_privacy || r.sync_mtime || r.modernize_exif || r.strip_creator_tool ||
                !r.exif_edits.empty() || !r.xmp_edits.empty()) {
                ov.privacy_enabled = true;
                ov.strip_privacy = r.strip_privacy;
                ov.sync_mtime = r.sync_mtime;
                ov.modernize_exif = r.modernize_exif;
                ov.strip_creator_tool = r.strip_creator_tool;
            }
            item.exception = normalize_override(ov);
            MetaSelectionEdit edit;
            edit.path = item.path;
            edit.edit = item.exception;
            edits.append(edit);
        }
        emit_selection_edits(st, edits);
        // 就地更新快照后的显示面刷新（独立 PageMeta 探针路径不依赖宿主/信号）
        refresh_all(st);
        load_inputs_from_selection(st);
    }
}

} // namespace pp::ui::meta
