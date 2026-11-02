// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页 GUI 薄层（B.1 拆分步 B3：meta_rules；0.4.2 选择作用域语义）
//
// Contract: docs/m13-tasks.md §4 C2（0.4.1 选择项级编辑）+ 0.4.2 用户需求 1/2
//           （选择作用域 = 文件列表**行选中集合**；无选中 = 空态禁写；多选逐字段聚合）。
// 语义（0.4.2）：
//   * **唯一可写真源** = FileEntry.exception（optional<MetadataOverride>）；控件只是选择项聚合的
//     视图（C2.1：UI 不维护第二份可写状态）。**不存在全局批量规则**——`rules()` 只是选中项
//     编辑状态的聚合快照（预设保存用），`apply_rules()` 是预设 → 选中文件 override 的替换式转换。
//   * 纯内核（聚合/字段级合并/预设聚合/文本聚合）在 ui/meta_selection.{h,cpp}（无 GUI 可单测）；
//     本单元只承担「读控件 → 纯函数 → 写回 follow.items + 发信号 + 局部刷新」。
//   * add_tag_row / clear_tag_rows = 隐私卡标签编辑行的**显式清单**（R-180：替代
//     findChild("tag_key") 隐式约定）。
// 实现沿革：自 page_meta.cpp 逐字迁出（M7-R2.1-F2）；0.4.2 删除 RuleInputs/build_rules/
// apply_state 等「批量规则」死面（用户需求 5：清理语义残余；调用点零残留）。
#pragma once

#include "core/metadata.h"
#include "ui/meta_selection.h" // M13（0.4.1）/ C2：EditField / 聚合纯函数面

#include <QString>
#include <QStringList>
#include <array>
#include <optional>
#include <utility>
#include <vector>

class QComboBox;
class QLineEdit;
class QWidget;

namespace pp::ui {
namespace meta {
struct MetaState; // 完整定义见 meta_state.h（本头只被 meta_state.h / meta_rules.cpp 消费）
} // namespace meta
} // namespace pp::ui

namespace pp::ui::meta {

// 时间卡「生效时间」注释行用的控件现值（seg/六分量/时区两偏移，纯显示辅助；不参与写入——
// 写入唯一真源 = override，见 commit_field）。单选路径与 0.4.1 逐字同口径。
pp::TimeShift current_time_shift(const MetaState *st);

// —— 坐标解析（R-100：失败回退 dms_to_deg）——
bool parse_coord(const QString &text, double lo, double hi, double &out);
std::optional<double> parse_optional(const QString &text, double lo, double hi);
bool parse_coord(const QLineEdit *edit, double lo, double hi, double &out);
std::optional<double> parse_optional(const QLineEdit *edit, double lo, double hi);

QString format_6(double v);

// —— 时区下拉（R-233：魔法集合显式化 + 表外偏移动态补项）——
void fill_timezone_combo(QComboBox *combo);
void select_timezone(QComboBox *combo, int offset_min);

// —— 标签编辑行（隐私卡内容；load_tag_rows 装载用）——
void clear_tag_rows(MetaState *st);
void add_tag_row(MetaState *st, const QString &key, const QString &value);

// ===========================================================================
// M13（0.4.1）/ C2：选择项级编辑 —— UI 薄层（实现 = meta_rules.cpp）
//
// 纯内核（聚合/字段级合并/预设聚合/文本聚合）在 ui/meta_selection.{h,cpp}（可无 GUI 单测）；
// 本段只承担「读控件 → 纯函数 → 写回 follow.items + 发信号 + 局部刷新」的薄层职责
// （C2.1：控件值只是选择项聚合的视图；唯一可写状态 = MetadataOverride）。
// ===========================================================================

// 选择项快照（follow.items → 纯值投影；commit/load/预设/rules() 共用）
SelectionSnapshotList selection_snapshots(const MetaState *st);

// 单字段提交（C2.4）：读对应控件现值 → 对每个选中文件字段级合并 → 就地更新
// st->follow.items[i].exception → 发 PageMeta::selection_edits_committed（MainWindow 写模型）
// → 只刷新生效值与聚合（不 reload 控件，避免打字光标被重置）。
// 卡开关 false = 清除该卡全部字段；GpsLat/GpsLon 两值都解析成功才提交。
void commit_field(MetaState *st, EditField field);

// 选择变化后按聚合装载全部输入控件（C2.3）：uniform 直接填 / mixed 统一 mixed 视觉 /
// absent 用既有默认或空态；卡头开关三态；mixed 字段数写 hook::kMixedFields。
void load_inputs_from_selection(MetaState *st);
// GPS 输入面装载（C2.3：override.gps 优先、否则 probe 原始 GPS 读回 → 多选「生效坐标聚合」）。
// probe 异步到位后（page_meta.cpp 的 ProbeScanner::changed 连接）与「从文件读取」入口复载。
void load_gps_inputs_from_selection(MetaState *st);

// mixed 视觉 helper（C2.3；QSS 锚 = theme::kFkProperty 无关的 hook::kMixedProperty）：
// apply_mixed = 置 ppMixed=true + placeholder「多个值」+ tooltip 列逐文件值；
// clear_mixed = 复原（ppMixed=false + placeholder 还原）；两者都 repolish 触发 QSS。
void apply_mixed(QLineEdit *edit, bool mixed, const QStringList &values);
void clear_mixed(QLineEdit *edit);
void apply_mixed_combo(QComboBox *combo, bool mixed, const QStringList &values);
// 只读标签 / 通用控件（QLabel、ElidedLabel、SegmentedControl 等）：mixed 时置属性 + tooltip。
void apply_mixed_widget(QWidget *widget, bool mixed, const QStringList &values);

// 预设载入（C2.6）：BatchRules 替换式转换为 st->follow.items 每项的 MetadataOverride
// （含 time_shift/gps/gps_clear/exif+xmp_edits/strip_privacy/sync_mtime/modernize_exif/
// strip_creator_tool；存在数据的卡 *_enabled 置 true）→ 就地更新快照 → refresh_all。
// 同时发 selection_edits_committed（运行需把 override 落到模型；独立 PageMeta 探针不依赖信号）。
void apply_preset_rules(MetaState *st, const pp::BatchRules &r);

// rules() 语义 = 选择项编辑状态聚合快照（预设保存用；C2.6）
pp::BatchRules rules_from_selection_state(const MetaState *st);

} // namespace pp::ui::meta
