// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M13（0.4.1）选择项级元数据编辑：纯聚合 / 字段级合并内核实现
//
// Contract: docs/m13-tasks.md C2（4.1/4.2/4.4/4.6）+ §10 task-2；契约注释见 meta_selection.h。
// 本 TU 是**纯函数实现**：不 include 任何 QtWidgets 头、不触碰 MetaState / 磁盘 / 网络 ——
// 单测 tests/unit/test_meta_selection.cpp 直接链接本文件（pp_test_meta_selection 只链 QtCore）。
#include "ui/meta_selection.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace pp::ui::meta {

namespace {

using pp::BatchRules;
using pp::MetadataOverride;
using pp::TimeShift;

QString tr(const char *text) { return QCoreApplication::translate("PageMeta", text); }

QString file_name(const QString &path) { return QFileInfo(path).fileName(); }

QString bool_text(bool value) { return value ? tr(QT_TR_NOOP("开")) : tr(QT_TR_NOOP("关")); }

// 逐字段聚合公共实现：
//   values[i] = 第 i 个文件该字段的值（nullopt = 该文件未设置）；
//   display[i] = 该文件值的显示文本（tooltip 用）。
// 三态判据（C2.2）：全未设置 = Absent；present>0 但未覆盖全部文件、或值不全同 = Mixed；
// 全部文件都设置且同值 = Uniform。
template <class T>
FieldAggregate<T> aggregate_optional(const SelectionSnapshotList &sel,
                                     const std::vector<std::optional<T>> &values,
                                     const QStringList &display) {
    FieldAggregate<T> out;
    out.total = static_cast<int>(sel.size());
    const std::size_t count = std::min(
        {sel.size(), values.size(), static_cast<std::size_t>(display.size())}); // 防御：三表应同长
    bool uniform = true;
    bool first = true;
    for (std::size_t i = 0; i < count; ++i) {
        if (!values[i].has_value()) {
            uniform = false; // 有文件未设置 → 不可能是 Uniform
            continue;
        }
        ++out.present;
        if (first) {
            out.value = *values[i];
            first = false;
        } else if (*values[i] != out.value) {
            uniform = false;
        }
        if (out.labels.size() < kAggregateLabelCap) {
            out.labels << file_name(sel[i].path) + QStringLiteral(": ") + display.at(int(i));
        }
    }
    if (out.present == 0) {
        out.state = Aggregate::Absent;
        out.labels.clear();
    } else {
        out.state = uniform ? Aggregate::Uniform : Aggregate::Mixed;
        out.omitted = out.present - static_cast<int>(out.labels.size());
    }
    return out;
}

// 卡内四个布尔开关字段 → override 里的具体 optional<bool> 指针；其余字段返回 nullptr。
const std::optional<bool> *bool_flag_of(const MetadataOverride &ov, EditField field) {
    switch (field) {
    case EditField::PrivacyStrip:
        return &ov.strip_privacy;
    case EditField::PrivacyMtime:
        return &ov.sync_mtime;
    case EditField::PrivacyModernize:
        return &ov.modernize_exif;
    case EditField::PrivacyCreatorTool:
        return &ov.strip_creator_tool;
    default:
        return nullptr;
    }
}

std::optional<bool> *bool_flag_of(MetadataOverride &ov, EditField field) {
    switch (field) {
    case EditField::PrivacyStrip:
        return &ov.strip_privacy;
    case EditField::PrivacyMtime:
        return &ov.sync_mtime;
    case EditField::PrivacyModernize:
        return &ov.modernize_exif;
    case EditField::PrivacyCreatorTool:
        return &ov.strip_creator_tool;
    default:
        return nullptr;
    }
}

// 卡级字段 → 该卡 *_enabled 的 optional<bool> 指针
const std::optional<bool> *card_flag_of(const MetadataOverride &ov, EditField card_field) {
    switch (card_field) {
    case EditField::TimeEnable:
        return &ov.time_enabled;
    case EditField::GpsEnable:
        return &ov.gps_enabled;
    case EditField::PrivacyEnable:
        return &ov.privacy_enabled;
    default:
        return nullptr;
    }
}

std::optional<bool> *card_flag_of(MetadataOverride &ov, EditField card_field) {
    switch (card_field) {
    case EditField::TimeEnable:
        return &ov.time_enabled;
    case EditField::GpsEnable:
        return &ov.gps_enabled;
    case EditField::PrivacyEnable:
        return &ov.privacy_enabled;
    default:
        return nullptr;
    }
}

// 选择集共同值（TimeShift / GpsData 均无 operator==，用逐字段比较；不在 core 冻结类型上加运算符）
bool same_shift(const TimeShift &a, const TimeShift &b) {
    return a.mode == b.mode && a.years == b.years && a.months == b.months && a.days == b.days &&
           a.hours == b.hours && a.minutes == b.minutes && a.seconds == b.seconds &&
           a.from_offset_min == b.from_offset_min && a.to_offset_min == b.to_offset_min;
}

bool same_gps(const pp::GpsData &a, const pp::GpsData &b) {
    return a.lat == b.lat && a.lon == b.lon && a.altitude == b.altitude &&
           a.direction == b.direction && a.timestamp == b.timestamp;
}

std::optional<TimeShift> uniform_shift(const std::vector<std::optional<TimeShift>> &values) {
    std::optional<TimeShift> common;
    for (const auto &value : values) {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (!common.has_value()) {
            common = value;
        } else if (!same_shift(*common, *value)) {
            return std::nullopt;
        }
    }
    return common;
}

std::optional<pp::GpsData> uniform_gps(const std::vector<std::optional<pp::GpsData>> &values) {
    std::optional<pp::GpsData> common;
    for (const auto &value : values) {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (!common.has_value()) {
            common = value;
        } else if (!same_gps(*common, *value)) {
            return std::nullopt;
        }
    }
    return common;
}

// tags 的共同值：TagEdit 无 operator==（避免为已冻结值类型加比较运算），
// 用**显示文本**（key=value; … 单源 = user_tag_text）作等价判据；全设置且同文本才入预设。
QString user_tag_text(const std::vector<pp::TagEdit> &edits);
std::optional<std::vector<pp::TagEdit>>
uniform_tags(const std::vector<std::optional<std::vector<pp::TagEdit>>> &values) {
    if (values.empty() || !values.front().has_value()) {
        return std::nullopt;
    }
    const QString first = user_tag_text(*values.front());
    for (const auto &value : values) {
        if (!value.has_value() || user_tag_text(*value) != first) {
            return std::nullopt;
        }
    }
    return values.front();
}

std::optional<bool> uniform_bool_flag(const SelectionSnapshotList &sel, EditField field) {
    std::optional<bool> common;
    for (const auto &item : sel) {
        if (!item.exception.has_value()) {
            return std::nullopt;
        }
        const std::optional<bool> *flag = bool_flag_of(*item.exception, field);
        if (flag == nullptr || !flag->has_value()) {
            return std::nullopt; // 有文件未显式设置 → mixed/absent 不入预设
        }
        if (!common.has_value()) {
            common = **flag;
        } else if (*common != **flag) {
            return std::nullopt;
        }
    }
    return common;
}

QString user_tag_text(const std::vector<pp::TagEdit> &edits) {
    QStringList parts;
    for (const pp::TagEdit &edit : edits) {
        const QString key = QString::fromStdString(edit.key);
        if (edit.remove) {
            parts << key + QStringLiteral("=<删除>");
        } else {
            parts << key + QStringLiteral("=") +
                         (edit.value.has_value() ? QString::fromStdString(*edit.value) : QString());
        }
    }
    return parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral("; "));
}

// 合并后的编辑表：逐键覆盖既有项（按 key 前缀归属 EXIF/XMP，与 build_rules 同口径），
// 未出现在本轮输入里的既有项保持原样（C2.4「tags 合并」）。
void merge_tag_edits(std::vector<pp::TagEdit> &exif_edits, std::vector<pp::TagEdit> &xmp_edits,
                     const pp::TagEdit &edit) {
    const bool is_xmp = edit.key.rfind("Xmp.", 0) == 0;
    std::vector<pp::TagEdit> &target = is_xmp ? xmp_edits : exif_edits;
    for (pp::TagEdit &existing : target) {
        if (existing.key == edit.key) {
            existing = edit;
            return;
        }
    }
    target.push_back(edit);
}

void erase_tag_keys(std::vector<pp::TagEdit> &edits, const std::vector<std::string> &keys) {
    edits.erase(std::remove_if(edits.begin(), edits.end(),
                               [&keys](const pp::TagEdit &edit) {
                                   return std::find(keys.begin(), keys.end(), edit.key) !=
                                          keys.end();
                               }),
                edits.end());
}

// index/value 由「分量序号 + 分量值」语义固定（唯一调用点按此序传参；改序不会类型报错
// ⇒ 由单测按分量钉住）。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void set_time_component(TimeShift &shift, int index, int value) {
    switch (index) {
    case 0:
        shift.years = value;
        break;
    case 1:
        shift.months = value;
        break;
    case 2:
        shift.days = value;
        break;
    case 3:
        shift.hours = value;
        break;
    case 4:
        shift.minutes = value;
        break;
    case 5:
        shift.seconds = value;
        break;
    default:
        break;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// EditField 词表辅助
// ---------------------------------------------------------------------------

EditField time_component_field(int index) {
    static constexpr std::array<EditField, kTimeComponentCount> kFields = {
        EditField::TimeYears, EditField::TimeMonths,  EditField::TimeDays,
        EditField::TimeHours, EditField::TimeMinutes, EditField::TimeSeconds};
    if (index < 0 || index >= kTimeComponentCount) {
        return EditField::TimeEnable;
    }
    return kFields[std::size_t(index)];
}

int time_component_index(EditField field) {
    switch (field) {
    case EditField::TimeYears:
        return 0;
    case EditField::TimeMonths:
        return 1;
    case EditField::TimeDays:
        return 2;
    case EditField::TimeHours:
        return 3;
    case EditField::TimeMinutes:
        return 4;
    case EditField::TimeSeconds:
        return 5;
    default:
        return -1;
    }
}

bool is_card_field(EditField field) {
    return field == EditField::TimeEnable || field == EditField::GpsEnable ||
           field == EditField::PrivacyEnable;
}

bool is_gps_component_field(EditField field) {
    return field == EditField::GpsLat || field == EditField::GpsLon || field == EditField::GpsAlt ||
           field == EditField::GpsDir || field == EditField::GpsTs;
}

// ---------------------------------------------------------------------------
// 卡级 active（C2.2）
// ---------------------------------------------------------------------------

bool card_has_edits(const std::optional<MetadataOverride> &ov, EditField card_field) {
    if (!ov.has_value()) {
        return false;
    }
    switch (card_field) {
    case EditField::TimeEnable:
        return ov->time_shift.has_value();
    case EditField::GpsEnable:
        return ov->gps.has_value() || ov->gps_clear;
    case EditField::PrivacyEnable:
        return ov->strip_privacy.has_value() || ov->sync_mtime.has_value() ||
               ov->modernize_exif.has_value() || ov->strip_creator_tool.has_value() ||
               !ov->exif_edits.empty() || !ov->xmp_edits.empty();
    default:
        return false;
    }
}

bool card_active(const std::optional<MetadataOverride> &ov, EditField card_field) {
    if (!ov.has_value()) {
        return false;
    }
    const std::optional<bool> *flag = card_flag_of(*ov, card_field);
    // C2.2 / C1.1 同判据（显示 = 写入）：*_enabled 显式值优先 —— true = 参与；false = 该卡
    // 整体退出（引擎 effective_rules 对 *_enabled==false 会丢弃该卡的生效字段，含例外自身）。
    // flag 未设（nullopt = 继承）→ 该卡任一编辑字段存在即 active；无编辑 = off（半开态）。
    if (flag != nullptr && flag->has_value()) {
        return **flag;
    }
    return card_has_edits(ov, card_field);
}

BoolAggregate card_active_aggregate(const SelectionSnapshotList &sel, EditField card_field) {
    std::vector<std::optional<bool>> active;
    QStringList display;
    active.reserve(sel.size());
    display.reserve(static_cast<int>(sel.size()));
    for (const auto &item : sel) {
        const bool on = card_active(item.exception, card_field);
        active.emplace_back(on);
        display << bool_text(on);
    }
    BoolAggregate out = aggregate_optional(sel, active, display);
    out.value = (out.present > 0) && out.value;
    return out;
}

// ---------------------------------------------------------------------------
// 逐字段聚合（C2.2；纯函数）
// ---------------------------------------------------------------------------

BoolAggregate flag_aggregate(const SelectionSnapshotList &sel, EditField flag_field) {
    std::vector<std::optional<bool>> values;
    QStringList display;
    values.reserve(sel.size());
    for (const auto &item : sel) {
        std::optional<bool> value;
        if (item.exception.has_value()) {
            if (const std::optional<bool> *flag = bool_flag_of(*item.exception, flag_field)) {
                value = *flag;
            }
        }
        values.push_back(value);
        display << bool_text(value.value_or(false));
    }
    return aggregate_optional(sel, values, display);
}

ModeAggregate time_mode_aggregate(const SelectionSnapshotList &sel) {
    std::vector<std::optional<TimeShift::Mode>> values;
    QStringList display;
    for (const auto &item : sel) {
        std::optional<TimeShift::Mode> value;
        if (item.exception.has_value() && item.exception->time_shift.has_value()) {
            value = item.exception->time_shift->mode;
        }
        values.push_back(value);
        display << (value.has_value() && *value == TimeShift::Mode::TimezoneSemantic
                        ? tr(QT_TR_NOOP("时区语义"))
                        : tr(QT_TR_NOOP("Δ 偏移")));
    }
    return aggregate_optional(sel, values, display);
}

IntAggregate time_component_aggregate(const SelectionSnapshotList &sel, int component) {
    std::vector<std::optional<int>> values;
    QStringList display;
    for (const auto &item : sel) {
        std::optional<int> value;
        if (item.exception.has_value() && item.exception->time_shift.has_value()) {
            const TimeShift &shift = *item.exception->time_shift;
            switch (component) {
            case 0:
                value = shift.years;
                break;
            case 1:
                value = shift.months;
                break;
            case 2:
                value = shift.days;
                break;
            case 3:
                value = shift.hours;
                break;
            case 4:
                value = shift.minutes;
                break;
            case 5:
                value = shift.seconds;
                break;
            default:
                break;
            }
        }
        values.push_back(value);
        display << QString::number(value.value_or(0));
    }
    return aggregate_optional(sel, values, display);
}

IntAggregate timezone_offset_aggregate(const SelectionSnapshotList &sel, bool from) {
    std::vector<std::optional<int>> values;
    QStringList display;
    for (const auto &item : sel) {
        std::optional<int> value;
        if (item.exception.has_value() && item.exception->time_shift.has_value()) {
            value = from ? item.exception->time_shift->from_offset_min
                         : item.exception->time_shift->to_offset_min;
        }
        values.push_back(value);
        display << QString::fromStdString(pp::offset_time_string(value.value_or(0)));
    }
    return aggregate_optional(sel, values, display);
}

BoolAggregate gps_clear_aggregate(const SelectionSnapshotList &sel) {
    BoolAggregate out;
    out.total = static_cast<int>(sel.size());
    int yes = 0;
    for (const auto &item : sel) {
        const bool clear = item.exception.has_value() && item.exception->gps_clear;
        if (clear) {
            ++yes;
        }
        if (out.labels.size() < kAggregateLabelCap) {
            out.labels << file_name(item.path) + QStringLiteral(": ") + bool_text(clear);
        }
    }
    out.omitted = std::max(0, out.total - static_cast<int>(out.labels.size()));
    if (yes == 0) {
        out.state = Aggregate::Absent;
        out.labels.clear();
        out.omitted = 0;
        return out;
    }
    out.present = yes;
    if (yes == out.total) {
        out.state = Aggregate::Uniform;
        out.value = true;
    } else {
        out.state = Aggregate::Mixed;
    }
    return out;
}

DoubleAggregate gps_lat_aggregate(const SelectionSnapshotList &sel) {
    std::vector<std::optional<double>> values;
    QStringList display;
    for (const auto &item : sel) {
        std::optional<double> value;
        if (item.exception.has_value() && item.exception->gps.has_value()) {
            value = item.exception->gps->lat;
        }
        values.push_back(value);
        display << QString::number(value.value_or(0.0), 'f', 6);
    }
    return aggregate_optional(sel, values, display);
}

DoubleAggregate gps_lon_aggregate(const SelectionSnapshotList &sel) {
    std::vector<std::optional<double>> values;
    QStringList display;
    for (const auto &item : sel) {
        std::optional<double> value;
        if (item.exception.has_value() && item.exception->gps.has_value()) {
            value = item.exception->gps->lon;
        }
        values.push_back(value);
        display << QString::number(value.value_or(0.0), 'f', 6);
    }
    return aggregate_optional(sel, values, display);
}

OptionalDoubleAggregate gps_alt_aggregate(const SelectionSnapshotList &sel) {
    std::vector<std::optional<std::optional<double>>> values;
    QStringList display;
    for (const auto &item : sel) {
        std::optional<std::optional<double>> value;
        if (item.exception.has_value() && item.exception->gps.has_value()) {
            value = item.exception->gps->altitude;
        }
        values.push_back(value);
        display << (value.has_value() && value->has_value() ? QString::number(**value, 'f', 1)
                                                            : QStringLiteral("—"));
    }
    return aggregate_optional(sel, values, display);
}

OptionalDoubleAggregate gps_dir_aggregate(const SelectionSnapshotList &sel) {
    std::vector<std::optional<std::optional<double>>> values;
    QStringList display;
    for (const auto &item : sel) {
        std::optional<std::optional<double>> value;
        if (item.exception.has_value() && item.exception->gps.has_value()) {
            value = item.exception->gps->direction;
        }
        values.push_back(value);
        display << (value.has_value() && value->has_value() ? QString::number(**value, 'f', 1)
                                                            : QStringLiteral("—"));
    }
    return aggregate_optional(sel, values, display);
}

StringAggregate gps_timestamp_aggregate(const SelectionSnapshotList &sel) {
    std::vector<std::optional<QString>> values;
    QStringList display;
    for (const auto &item : sel) {
        std::optional<QString> value;
        if (item.exception.has_value() && item.exception->gps.has_value() &&
            item.exception->gps->timestamp.has_value()) {
            value = QString::fromStdString(*item.exception->gps->timestamp);
        }
        values.push_back(value);
        display << value.value_or(QStringLiteral("—"));
    }
    return aggregate_optional(sel, values, display);
}

TagsAggregate tags_aggregate(const SelectionSnapshotList &sel) {
    // TagEdit 无 operator== → 三态判据用显示文本（user_tag_text 单源）做等价比较
    TagsAggregate out;
    out.total = static_cast<int>(sel.size());
    std::optional<std::vector<pp::TagEdit>> first;
    QString first_text;
    bool uniform = true;
    for (const auto &item : sel) {
        std::optional<std::vector<pp::TagEdit>> value;
        if (item.exception.has_value() &&
            (!item.exception->exif_edits.empty() || !item.exception->xmp_edits.empty())) {
            std::vector<pp::TagEdit> combined = item.exception->exif_edits;
            combined.insert(combined.end(), item.exception->xmp_edits.begin(),
                            item.exception->xmp_edits.end());
            value = std::move(combined);
        }
        const QString text = value.has_value() ? user_tag_text(*value) : QStringLiteral("—");
        if (!value.has_value()) {
            uniform = false;
            continue;
        }
        ++out.present;
        if (!first.has_value()) {
            first = std::move(value);
            first_text = text;
        } else if (text != first_text) {
            uniform = false;
        }
        if (out.labels.size() < kAggregateLabelCap) {
            out.labels << file_name(item.path) + QStringLiteral(": ") + text;
        }
    }
    if (out.present == 0) {
        out.state = Aggregate::Absent;
        out.labels.clear();
    } else {
        out.state = uniform ? Aggregate::Uniform : Aggregate::Mixed;
        out.omitted = out.present - static_cast<int>(out.labels.size());
        if (first.has_value()) {
            out.value = std::move(*first);
        }
    }
    return out;
}

int mixed_field_count(const SelectionSnapshotList &sel) {
    int count = 0;
    const auto is_mixed = [](Aggregate state) { return state == Aggregate::Mixed; };
    for (const EditField card :
         {EditField::TimeEnable, EditField::GpsEnable, EditField::PrivacyEnable}) {
        if (is_mixed(card_active_aggregate(sel, card).state)) {
            ++count;
        }
    }
    if (is_mixed(time_mode_aggregate(sel).state)) {
        ++count;
    }
    for (int i = 0; i < kTimeComponentCount; ++i) {
        if (is_mixed(time_component_aggregate(sel, i).state)) {
            ++count;
        }
    }
    for (const bool from : {true, false}) {
        if (is_mixed(timezone_offset_aggregate(sel, from).state)) {
            ++count;
        }
    }
    if (is_mixed(gps_clear_aggregate(sel).state)) {
        ++count;
    }
    if (is_mixed(gps_lat_aggregate(sel).state)) {
        ++count;
    }
    if (is_mixed(gps_lon_aggregate(sel).state)) {
        ++count;
    }
    if (is_mixed(gps_alt_aggregate(sel).state)) {
        ++count;
    }
    if (is_mixed(gps_dir_aggregate(sel).state)) {
        ++count;
    }
    if (is_mixed(gps_timestamp_aggregate(sel).state)) {
        ++count;
    }
    for (const EditField flag : {EditField::PrivacyStrip, EditField::PrivacyMtime,
                                 EditField::PrivacyModernize, EditField::PrivacyCreatorTool}) {
        if (is_mixed(flag_aggregate(sel, flag).state)) {
            ++count;
        }
    }
    if (is_mixed(tags_aggregate(sel).state)) {
        ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// 字段级合并（C2.4）
// ---------------------------------------------------------------------------

std::optional<MetadataOverride> normalize_override(const MetadataOverride &ov) {
    return override_is_empty(ov) ? std::nullopt : std::optional<MetadataOverride>(ov);
}

bool override_is_empty(const MetadataOverride &ov) {
    return !ov.ignore_batch && !ov.time_shift.has_value() && !ov.gps.has_value() && !ov.gps_clear &&
           ov.exif_edits.empty() && ov.xmp_edits.empty() && !ov.strip_privacy.has_value() &&
           !ov.time_enabled.has_value() && !ov.gps_enabled.has_value() &&
           !ov.privacy_enabled.has_value() && !ov.sync_mtime.has_value() &&
           !ov.modernize_exif.has_value() && !ov.strip_creator_tool.has_value();
}

std::optional<MetadataOverride> apply_tags_edit(const std::optional<MetadataOverride> &current,
                                                const std::vector<pp::TagEdit> &tags,
                                                const std::vector<std::string> &remove_tags) {
    MetadataOverride ov = current.value_or(MetadataOverride{});
    for (const pp::TagEdit &edit : tags) {
        if (edit.key.empty()) {
            continue;
        }
        merge_tag_edits(ov.exif_edits, ov.xmp_edits, edit);
    }
    erase_tag_keys(ov.exif_edits, remove_tags);
    erase_tag_keys(ov.xmp_edits, remove_tags);
    return normalize_override(ov);
}

std::optional<MetadataOverride> merge_gps_field_edit(const std::optional<MetadataOverride> &current,
                                                     const std::optional<pp::GpsData> &base,
                                                     EditField field, const FieldInput &input) {
    MetadataOverride ov = current.value_or(MetadataOverride{});
    switch (field) {
    case EditField::GpsClear:
        if (input.flag) {
            ov.gps_clear = true;
            ov.gps.reset();
        } else {
            ov.gps_clear = false;
        }
        return normalize_override(ov);
    case EditField::GpsLat:
    case EditField::GpsLon: {
        if (!input.has_lat_lon) {
            return normalize_override(ov); // 两值未都解析成功 → 不提交（C2.4）
        }
        // 保留该文件已有 alt/dir/ts：override.gps 优先，否则用生效 GPS（源 probe 读回）
        pp::GpsData gps = ov.gps.has_value() ? *ov.gps : base.value_or(pp::GpsData{});
        gps.lat = std::clamp(input.lat, -90.0, 90.0);
        gps.lon = std::clamp(input.lon, -180.0, 180.0);
        ov.gps = gps;
        return normalize_override(ov);
    }
    case EditField::GpsAlt:
    case EditField::GpsDir:
    case EditField::GpsTs: {
        // 无 GPS 点的文件忽略/不创造点（C2.4）
        std::optional<pp::GpsData> gps = ov.gps.has_value() ? ov.gps : base;
        if (!gps.has_value()) {
            return normalize_override(ov);
        }
        if (field == EditField::GpsAlt) {
            gps->altitude = input.altitude;
        } else if (field == EditField::GpsDir) {
            gps->direction = input.direction;
        } else {
            gps->timestamp = input.timestamp.has_value()
                                 ? std::optional<std::string>(input.timestamp->toStdString())
                                 : std::nullopt;
        }
        ov.gps = gps;
        return normalize_override(ov);
    }
    default:
        // 非 GPS 分量：本函数只承担 GPS 字段，交由 merge_field_edit 处理
        return merge_field_edit(current, field, input);
    }
}

std::optional<MetadataOverride> merge_field_edit(const std::optional<MetadataOverride> &current,
                                                 EditField field, const FieldInput &input) {
    MetadataOverride ov = current.value_or(MetadataOverride{});
    if (is_card_field(field)) {
        std::optional<bool> *flag = card_flag_of(ov, field);
        if (flag == nullptr) {
            return normalize_override(ov);
        }
        *flag = input.flag;
        if (!input.flag) {
            // 卡关闭 = 清除该卡全部字段（C2.4；隐私卡连 tag 编辑一并清除 —— C1.1 注）
            switch (field) {
            case EditField::TimeEnable:
                ov.time_shift.reset();
                break;
            case EditField::GpsEnable:
                ov.gps.reset();
                ov.gps_clear = false;
                break;
            case EditField::PrivacyEnable:
                ov.strip_privacy.reset();
                ov.sync_mtime.reset();
                ov.modernize_exif.reset();
                ov.strip_creator_tool.reset();
                ov.exif_edits.clear();
                ov.xmp_edits.clear();
                break;
            default:
                break;
            }
        }
        return normalize_override(ov);
    }
    if (is_gps_component_field(field) || field == EditField::GpsClear) {
        const std::optional<pp::GpsData> base = ov.gps; // 纯函数面无源 probe → 以 override 自身为基
        return merge_gps_field_edit(current, base, field, input);
    }
    if (field == EditField::Tags) {
        return apply_tags_edit(current, input.tags, input.remove_tags);
    }
    if (const int index = time_component_index(field); index >= 0) {
        if (!ov.time_shift.has_value()) {
            // 新建时间修改：其余分量取当前控件显示值（input.seed_time）→ 显示 = 写入
            ov.time_shift = input.seed_time.value_or(TimeShift{});
        }
        set_time_component(*ov.time_shift, index, input.value);
        return normalize_override(ov);
    }
    switch (field) {
    case EditField::TimeMode:
        if (!ov.time_shift.has_value()) {
            ov.time_shift = input.seed_time.value_or(TimeShift{});
        }
        ov.time_shift->mode = input.mode;
        break;
    case EditField::TimeTzFrom:
    case EditField::TimeTzTo:
        if (!ov.time_shift.has_value()) {
            // 时区偏移第一次落值 = 时区语义模式（Δ 模式不使用这两字段）；另一偏移取显示值
            TimeShift shift = input.seed_time.value_or(TimeShift{});
            shift.mode = TimeShift::Mode::TimezoneSemantic;
            ov.time_shift = shift;
        }
        if (field == EditField::TimeTzFrom) {
            ov.time_shift->from_offset_min = input.value;
        } else {
            ov.time_shift->to_offset_min = input.value;
        }
        break;
    default: {
        std::optional<bool> *flag = bool_flag_of(ov, field);
        if (flag != nullptr) {
            *flag = input.flag;
        }
        break;
    }
    }
    return normalize_override(ov);
}

// ---------------------------------------------------------------------------
// 预设聚合（C2.6）
// ---------------------------------------------------------------------------

BatchRules batch_rules_from_selection(const SelectionSnapshotList &sel) {
    BatchRules rules;
    if (sel.empty()) {
        rules.time_enabled = false;
        rules.gps_enabled = false;
        rules.privacy_enabled = false;
        return rules;
    }
    const auto all_active = [&sel](EditField card) {
        for (const auto &item : sel) {
            if (!card_active(item.exception, card)) {
                return false;
            }
        }
        return true;
    };
    rules.time_enabled = all_active(EditField::TimeEnable);
    rules.gps_enabled = all_active(EditField::GpsEnable);
    rules.privacy_enabled = all_active(EditField::PrivacyEnable);

    std::vector<std::optional<TimeShift>> shifts;
    std::vector<std::optional<pp::GpsData>> gps_values;
    std::vector<std::optional<std::vector<pp::TagEdit>>> tags_values;
    bool all_clear = true;
    for (const auto &item : sel) {
        std::optional<TimeShift> shift;
        std::optional<pp::GpsData> gps;
        std::optional<std::vector<pp::TagEdit>> tags;
        if (item.exception.has_value()) {
            shift = item.exception->time_shift;
            gps = item.exception->gps;
            if (!item.exception->exif_edits.empty() || !item.exception->xmp_edits.empty()) {
                std::vector<pp::TagEdit> combined = item.exception->exif_edits;
                combined.insert(combined.end(), item.exception->xmp_edits.begin(),
                                item.exception->xmp_edits.end());
                tags = std::move(combined);
            }
        }
        if (!(item.exception.has_value() && item.exception->gps_clear)) {
            all_clear = false;
        }
        shifts.push_back(shift); // optional<TimeShift> 平凡可拷贝：值拷贝即可（move 无收益）
        gps_values.push_back(std::move(gps));
        tags_values.push_back(std::move(tags));
    }
    rules.time_shift = uniform_shift(shifts);
    rules.gps = uniform_gps(gps_values);
    rules.gps_clear = all_clear;

    if (const auto tags = uniform_tags(tags_values); tags.has_value()) {
        for (const pp::TagEdit &edit : *tags) {
            if (edit.key.rfind("Xmp.", 0) == 0) {
                rules.xmp_edits.push_back(edit);
            } else {
                rules.exif_edits.push_back(edit);
            }
        }
    }
    if (const auto value = uniform_bool_flag(sel, EditField::PrivacyStrip); value.has_value()) {
        rules.strip_privacy = *value;
    }
    if (const auto value = uniform_bool_flag(sel, EditField::PrivacyMtime); value.has_value()) {
        rules.sync_mtime = *value;
    }
    if (const auto value = uniform_bool_flag(sel, EditField::PrivacyModernize); value.has_value()) {
        rules.modernize_exif = *value;
    }
    if (const auto value = uniform_bool_flag(sel, EditField::PrivacyCreatorTool);
        value.has_value()) {
        rules.strip_creator_tool = *value;
    }
    return rules;
}

// ---------------------------------------------------------------------------
// 只读值文本聚合（C2.5）
// ---------------------------------------------------------------------------

TextAggregate aggregate_texts(const SelectionSnapshotList &sel, const QStringList &texts, int cap) {
    TextAggregate out;
    out.empty = sel.empty() || texts.isEmpty();
    if (out.empty) {
        return out;
    }
    const int limit = cap > 0 ? cap : kAggregateLabelCap;
    const int count = static_cast<int>(std::min(sel.size(), std::size_t(texts.size())));
    out.texts = texts.mid(0, count);
    out.uniform = true;
    for (int i = 1; i < count; ++i) {
        if (texts.at(i) != texts.at(0)) {
            out.uniform = false;
            break;
        }
    }
    out.value = texts.at(0);
    for (int i = 0; i < count; ++i) {
        if (out.labels.size() < limit) {
            out.labels << file_name(sel[std::size_t(i)].path) + QStringLiteral(": ") + texts.at(i);
        }
    }
    out.omitted = std::max(0, count - static_cast<int>(out.labels.size()));
    return out;
}

QString aggregate_display(const TextAggregate &agg, const QString &mixed_text) {
    if (agg.empty) {
        return QString();
    }
    if (agg.uniform) {
        return agg.value;
    }
    return mixed_text;
}

QString aggregate_tooltip(const TextAggregate &agg) {
    if (agg.empty || agg.uniform) {
        return QString();
    }
    QString tip = agg.labels.join(QStringLiteral("\n"));
    if (agg.omitted > 0) {
        if (!tip.isEmpty()) {
            tip += QLatin1Char('\n');
        }
        tip += tr(QT_TR_NOOP("…还有 %1 个")).arg(agg.omitted);
    }
    return tip;
}

} // namespace pp::ui::meta
