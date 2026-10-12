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
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QPushButton>
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

    QObject::connect(key_edit, &QLineEdit::textChanged, st, [st] {
        notify_rules_changed(st);
        refresh_effective(st);
    });
    QObject::connect(value_edit, &QLineEdit::textChanged, st, [st] {
        notify_rules_changed(st);
        refresh_effective(st);
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

        // UX-5：恢复**含值旧状态 = 开**（旧预设缺 *_enabled 键 → 默认 true，带值即开关置开）；
        // 显式 *_enabled=false 优先（预设里明确关掉的卡不复活）。新会话/空值 = 关（零变化）。
        const bool time_on = r.time_enabled && r.time_shift.has_value();
        const bool gps_on = r.gps_enabled && (r.gps.has_value() || r.gps_clear);
        const bool privacy_on =
            r.privacy_enabled &&
            (r.strip_privacy || r.sync_mtime || !r.exif_edits.empty() || !r.xmp_edits.empty());
        st->time.time_enable->setChecked(time_on);
        st->privacy.privacy_enable->setChecked(privacy_on);
        set_gps_rule_enabled(st, gps_on);
    }
    refresh_all(st);
}

} // namespace pp::ui::meta
