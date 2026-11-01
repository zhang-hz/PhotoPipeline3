// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M13（0.4.1）选择项级元数据编辑：纯聚合 / 字段级合并内核
//
// Contract: docs/m13-tasks.md C2（4.1 数据模型 / 4.2 聚合语义 / 4.4 提交语义 / 4.6 预设）
//           + §10 task-2；追认 = docs/m13-adjudications.md（Lead 收口）。
//
// 本单元是 C2 的**纯函数半边**（可无 GUI 单测：tests/unit/test_meta_selection.cpp 直接
// 编译本 TU，只依赖 core/metadata.h + QtCore；零 QtWidgets、零 MetaState）：
//   * EditField 词表：元数据页每个可编辑字段一值（C2.2 逐字）；
//   * 三态聚合：absent（所有选中文件都未设置）/ uniform（同值）/ mixed（多值）；
//   * 字段级合并 merge_field_edit（C2.4：只覆盖被编辑字段，不串扰其它字段）；
//   * 空 override 归一 nullopt（C2.1：MetadataOverride{} = 无修改 → 模型存 nullopt）；
//   * 预设聚合 batch_rules_from_selection（C2.6：uniform 字段入规则、*_enabled 全 on 才 true）；
//   * 只读值文本聚合 aggregate_texts（C2.5：多选 mixed → 「多个值」+ 逐文件 tooltip）。
//
// 唯一可写状态 = FileEntry.exception（optional<MetadataOverride>）；控件值只是选择项聚合的
// 视图（C2.0「UI 不维护第二份可写状态」）。QtWidgets 侧薄层（commit_field /
// load_inputs_from_selection / mixed 视觉）在 ui/meta_rules.{h,cpp}（声明面见该头 M13 段）。
#pragma once

#include "core/metadata.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <string>
#include <vector>

namespace pp::ui::meta {

// ---------------------------------------------------------------------------
// EditField 词表（C2.2 逐字；顺序 = 契约顺序，新增只能追加在末尾）
// ---------------------------------------------------------------------------
enum class EditField {
    TimeEnable,
    TimeMode,
    TimeYears,
    TimeMonths,
    TimeDays,
    TimeHours,
    TimeMinutes,
    TimeSeconds,
    TimeTzFrom,
    TimeTzTo,
    GpsEnable,
    GpsClear,
    GpsLat,
    GpsLon,
    GpsAlt,
    GpsDir,
    GpsTs,
    PrivacyEnable,
    PrivacyStrip,
    PrivacyMtime,
    PrivacyModernize,
    PrivacyCreatorTool,
    Tags,
};

// Δ 六分量在 EditField 词表里的顺序（years..seconds ↔ 0..5；与 pp::TimeShift 成员同序）
constexpr int kTimeComponentCount = 6;
// 「每个 Δ 分量」的 EditField（0 = 年 … 5 = 秒）；越界返回 TimeEnable（不合法输入的安全值）
EditField time_component_field(int index);
// EditField 是 Δ 分量 → 返回 0..5，否则 -1
int time_component_index(EditField field);
// 卡级字段（TimeEnable / GpsEnable / PrivacyEnable）
bool is_card_field(EditField field);
// GPS 数值/文本分量（GpsLat/GpsLon/GpsAlt/GpsDir/GpsTs）
bool is_gps_component_field(EditField field);

// ---------------------------------------------------------------------------
// 三态聚合（C2.2）
// ---------------------------------------------------------------------------
enum class Aggregate {
    Absent,  // 所有选中文件都未设置该字段
    Uniform, // 所有选中文件的值相同
    Mixed,   // 至少两值不同 → UI 明确显示「多个值」
};

// 选择项快照（`MetaSelectionItem` 的纯值投影；只用于聚合，不含 probe/QWidget）
struct SelectionSnapshot {
    QString path;
    std::optional<pp::MetadataOverride> exception;
};
using SelectionSnapshotList = std::vector<SelectionSnapshot>;

// 单字段聚合结果。labels = 显式设置该字段的文件 `文件名: 显示值`（选择序，最多
// kAggregateLabelCap 条；omitted = 超出的条数），供 mixed tooltip 直接消费。
// `present == 0` ⇔ state == Absent；state == Uniform 时 value 有效。
constexpr int kAggregateLabelCap = 8;

template <class T> struct FieldAggregate {
    Aggregate state = Aggregate::Absent;
    int total = 0;   // 选中文件总数
    int present = 0; // 显式设置该字段的文件数（含 mixed 的每一方）
    int omitted = 0; // labels 截断掉的文件数
    T value{};       // state == Uniform 时有效
    QStringList labels;
};

using BoolAggregate = FieldAggregate<bool>;
using IntAggregate = FieldAggregate<int>;
using ModeAggregate = FieldAggregate<pp::TimeShift::Mode>;
using DoubleAggregate = FieldAggregate<double>;
using OptionalDoubleAggregate = FieldAggregate<std::optional<double>>;
using StringAggregate = FieldAggregate<QString>;
using TagsAggregate = FieldAggregate<std::vector<pp::TagEdit>>;

// —— 卡级 active（C2.2 逐字）——
// 文件该卡 active ⇔ 该卡 *_enabled == true **或**该卡任一编辑字段存在；
// *_enabled == false 且无编辑 = off（半开态）。唯一可写状态 = override，故本函数是纯判据。
bool card_has_edits(const std::optional<pp::MetadataOverride> &ov, EditField card_field);
bool card_active(const std::optional<pp::MetadataOverride> &ov, EditField card_field);
// 选择集聚合：全 active → Uniform true / 全 off → Uniform false / 否则 Mixed。
BoolAggregate card_active_aggregate(const SelectionSnapshotList &sel, EditField card_field);

// —— 逐字段聚合（全部纯函数；`labels` 供混合值 tooltip）——
BoolAggregate flag_aggregate(const SelectionSnapshotList &sel, EditField flag_field);
ModeAggregate time_mode_aggregate(const SelectionSnapshotList &sel);
IntAggregate time_component_aggregate(const SelectionSnapshotList &sel, int component);
IntAggregate timezone_offset_aggregate(const SelectionSnapshotList &sel, bool from);
BoolAggregate gps_clear_aggregate(const SelectionSnapshotList &sel);
DoubleAggregate gps_lat_aggregate(const SelectionSnapshotList &sel);
DoubleAggregate gps_lon_aggregate(const SelectionSnapshotList &sel);
OptionalDoubleAggregate gps_alt_aggregate(const SelectionSnapshotList &sel);
OptionalDoubleAggregate gps_dir_aggregate(const SelectionSnapshotList &sel);
StringAggregate gps_timestamp_aggregate(const SelectionSnapshotList &sel);
TagsAggregate tags_aggregate(const SelectionSnapshotList &sel);
// 选择集里处于 mixed 态的字段数（C2.3 钩子 pp_meta_mixed_fields 的读数来源）
int mixed_field_count(const SelectionSnapshotList &sel);

// ---------------------------------------------------------------------------
// 字段编辑输入（commit 的纯数据面；UI 薄层从控件读值后填充）
// ---------------------------------------------------------------------------
struct FieldInput {
    bool flag = false; // 卡开关 / 布尔开关 / GPS 清除的意图值
    int value = 0;     // Δ 分量值（int）/ 时区偏移（分钟）
    pp::TimeShift::Mode mode = pp::TimeShift::Mode::Delta;
    // 时间字段**新建**该文件 time_shift 时的初值（= 当前控件显示值；C2.4 显示=写入：
    // 用户从"无时间修改"切到某模式时，新 override 必须与界面所见一致，不能落到 (0,0) 默认）。
    // 已有 time_shift 的文件仍只改被编辑字段（不串扰）。
    std::optional<pp::TimeShift> seed_time;
    bool has_lat_lon = false; // GpsLat/GpsLon：两值都解析成功才 true（C2.4）
    double lat = 0;
    double lon = 0;
    std::optional<double> altitude;       // GpsAlt（nullopt = 清空该分量）
    std::optional<double> direction;      // GpsDir
    std::optional<QString> timestamp;     // GpsTs（nullopt = 清空）
    std::vector<pp::TagEdit> tags;        // Tags：整表提交的编辑行
    std::vector<std::string> remove_tags; // Tags：从文件既有编辑里删除的键
};

// 字段级合并（C2.4）。语义：
//   * 只改被编辑字段：例 TimeDays 只覆盖 days，其余 Δ 分量/模式/时区保持该文件原值；
//   * 卡开关 false → 清除该卡全部字段并置 *_enabled=false；true → 只置 *_enabled=true；
//   * 卡内字段先于开关写入也不丢（合并顺序无关）；
//   * 结果归一：空 override → nullopt（C2.1）。
std::optional<pp::MetadataOverride>
merge_field_edit(const std::optional<pp::MetadataOverride> &current, EditField field,
                 const FieldInput &input);

// GPS 分量合并（C2.4「GpsLat/GpsLon 两值都解析成功才落 GpsData，并保留该文件已有
// alt/dir/ts」）。base = 该文件当前**生效** GPS（override.gps 优先，否则源 probe 读回；
// nullopt = 无 GPS 点）——UI 薄层从 preview/probe 取，纯测可传 current.gps：
//   * GpsLat/GpsLon：input.has_lat_lon 才提交，并保留 base（或 override 自身）的 alt/dir/ts；
//   * GpsAlt/GpsDir/GpsTs：仅当 base 有 GPS 点时写该分量（文件无 GPS 点 → 忽略/不创造点）；
//   * GpsClear：true → gps_clear=true + gps.reset()；false → gps_clear=false。
std::optional<pp::MetadataOverride>
merge_gps_field_edit(const std::optional<pp::MetadataOverride> &current,
                     const std::optional<pp::GpsData> &base, EditField field,
                     const FieldInput &input);

// Tags 整表合并（C2.4「选择集共有的编辑行 + 用户新编辑」；键归属 = `Xmp.` 前缀判定，
// 与既有 build_rules 同口径）：tags 逐键覆盖/追加，remove_tags 逐键删除，其余保持原样。
std::optional<pp::MetadataOverride>
apply_tags_edit(const std::optional<pp::MetadataOverride> &current,
                const std::vector<pp::TagEdit> &tags, const std::vector<std::string> &remove_tags);

// 空 override 判定 / 归一（C2.1：MetadataOverride{} = 无修改）
bool override_is_empty(const pp::MetadataOverride &ov);
std::optional<pp::MetadataOverride> normalize_override(const pp::MetadataOverride &ov);

// ---------------------------------------------------------------------------
// 预设聚合（C2.6）：当前选择项编辑状态 → pp::BatchRules（预设保存用）
//   * uniform 字段入规则、mixed/absent 留空；
//   * *_enabled：选择集全 active → true，否则 false；
//   * 不含 per-file 差异（旧预设格式只能表达单值规则）。
// ---------------------------------------------------------------------------
pp::BatchRules batch_rules_from_selection(const SelectionSnapshotList &sel);

// ---------------------------------------------------------------------------
// 只读值文本聚合（C2.5：关键信息卡 / GPS 生效坐标多选）
// ---------------------------------------------------------------------------
struct TextAggregate {
    bool empty = true;   // 无选中
    bool uniform = true; // 所有选中文件文本相同
    QString value;       // uniform 时 = 唯一文本
    QStringList texts;   // 逐文件显示文本（与 sel 同序）
    QStringList labels;  // mixed tooltip：`文件名: 文本`（≤ cap 条）
    int omitted = 0;     // labels 截断掉的条数
};

// texts 与 sel 同序逐文件给出；cap = tooltip 条数上限（默认 kAggregateLabelCap）。
TextAggregate aggregate_texts(const SelectionSnapshotList &sel, const QStringList &texts,
                              int cap = kAggregateLabelCap);
// uniform → 值；mixed → mixed_text（调用方传 PageMeta::tr(kMixedValueText)）；空 → 空串。
QString aggregate_display(const TextAggregate &agg, const QString &mixed_text);
// mixed 的 tooltip（逐文件值 ≤cap 条 + 「…还有 N 个」）；uniform/空 → 空串。
QString aggregate_tooltip(const TextAggregate &agg);

} // namespace pp::ui::meta
