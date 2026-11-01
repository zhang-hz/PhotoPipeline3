// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页规则装配（B.1 拆分步 B3：meta_rules）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（page_meta 拆分蓝图，步序 B3）
// 本单元 = 「**唯一**规则源」（rules() 与生效值显示都走这里 → 显示=写入）：
//   * `RuleInputs` 值聚合（B3 定稿：入参改值聚合）—— 规则装配**纯函数**化：不碰控件、
//     不查 objectName，可单测（R-181「不可单测」的规则半边随此收口）；
//   * `rule_inputs(st)` = **唯一**读控件点（GUI → 值聚合）；tag 行改**显式键值对清单**
//     （MetaState::tag_edits，add_tag_row 登记）—— 消灭 findChild("tag_key") 隐式约定（R-180）；
//   * 兼容面：`build_rules(const MetaState*)` / `current_time_shift(const MetaState*)` 薄重载
//     转发 rule_inputs（调用点零改动）。
// 实现自 page_meta.cpp 逐字迁出 + 值聚合化改写（M7-R2.1-F2）；输出逐字节不变。
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

// —— 值聚合（纯数据；规则装配的唯一输入面）——
struct RuleInputs {
    // UX-5：三卡「启用」（关闭 = 整卡不参与规则）
    bool time_enabled = false;
    bool gps_enabled = false;
    bool privacy_enabled = false;
    // 时间：seg 选中档（0 = Δ / 1 = 时区语义）+ Δ 六分量 + 时区两偏移（分钟）
    int seg_index = 0;
    std::array<int, 6> delta{};
    int tz_from_min = 0;
    int tz_to_min = 0;
    // GPS：清除钮 + 规则框原文（解析/校验在 build_rules 内）
    bool gps_clear = false;
    QString lat, lon, alt, dir, ts;
    // 标签编辑行：key/value **原文**（key 去空白后为空 = 跳过；value 空 = 删除该标签）
    std::vector<std::pair<QString, QString>> tags;
    // 隐私与文件时间
    bool strip_privacy = false;
    bool sync_mtime = false;
    bool modernize_exif = false; // 补全现代 EXIF（可选；仅补可推导字段）
};

// GUI → 值聚合（**唯一**读控件点）
RuleInputs rule_inputs(const MetaState *st);

// —— 纯规则装配（显示=写入的唯一合成源；可单测）——
pp::TimeShift current_time_shift(const RuleInputs &in);
pp::BatchRules build_rules(const RuleInputs &in);

// —— 兼容薄重载（调用点零改动）——
pp::TimeShift current_time_shift(const MetaState *st);
pp::BatchRules build_rules(const MetaState *st);

// —— 坐标解析（R-100：失败回退 dms_to_deg）——
bool parse_coord(const QString &text, double lo, double hi, double &out);
std::optional<double> parse_optional(const QString &text, double lo, double hi);
bool parse_coord(const QLineEdit *edit, double lo, double hi, double &out);
std::optional<double> parse_optional(const QLineEdit *edit, double lo, double hi);

QString format_6(double v);

// —— 时区下拉（R-233：魔法集合显式化 + 表外偏移动态补项）——
void fill_timezone_combo(QComboBox *combo);
void select_timezone(QComboBox *combo, int offset_min);

// —— 规则面变更收口（R-178：四连刷新序列唯一落点）——
void commit_rule_change(MetaState *st);

// —— 标签编辑行（隐私卡内容；apply_state 复用）——
void clear_tag_rows(MetaState *st);
void add_tag_row(MetaState *st, const QString &key, const QString &value);

// —— 预设载入（不发信号）——
void apply_state(MetaState *st, const pp::BatchRules &r);

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
