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

#include <QString>
#include <array>
#include <optional>
#include <utility>
#include <vector>

class QComboBox;
class QLineEdit;

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

} // namespace pp::ui::meta
