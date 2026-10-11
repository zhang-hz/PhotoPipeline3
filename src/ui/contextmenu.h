// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 公共右键菜单件（M7-WP6 / UX-1）
//
// 契约：docs/ux-review-14.md §1「改进方案」——
//   1. 公共件 `src/ui/contextmenu.{h,cpp}`（**进公共层，不进各页**）：`menu(parent, items)`
//      单一构造点，菜单样式走 `theme`（与 0.3.1 已令牌化的 QMenu 同源，不新写样式）；
//   2. 每类卡片一个 `actions` 构造函数，**菜单内容不同**（12 类，见 §1 表）；
//   3. 运行中（lock_for_run）菜单项按语义置灰。
//
// 设计要点：
//   * 动作以 **意图回调**（std::function<void()>）表达，构造函数不摸任何页面内部状态 ——
//     这样 12 类 actions 可以全部落在本文件（单一来源），各卡片只负责把自己的意图接上；
//   * 运行中置灰 = 传 `enabled=false`（调用方按 lock_for_run 语义给），不在本件里判定；
//   * 样式不新写：QMenu 走 theme::style_sheet() 的既有 QMenu 规则（WP4 收口后已单源）。
#pragma once

#include <QList>
#include <QString>
#include <functional>
#include <vector>

class QAction;
class QMenu;
class QWidget;

namespace pp::ui::contextmenu {

// 菜单项（分隔线 = separator=true，其余字段忽略；children 非空 = 子菜单，trigger 忽略）
struct Item {
    QString text;
    std::function<void()> trigger; // 空 = 不可点（但不隐藏）
    bool enabled = true;
    bool separator = false;
    bool checkable = false;
    bool checked = false;
    std::vector<Item> children; // 子菜单（如「按分类打标 →」）

    static Item sep() {
        Item item;
        item.separator = true;
        return item;
    }
};

// 单一构造点：把 items 装成 QMenu（父件负责 WA_DeleteOnClose + popup/exec 的生命周期）
QMenu *menu(QWidget *parent, const std::vector<Item> &items);

// ---------------------------------------------------------------------------
// 12 类卡的 actions 构造函数（docs/ux-review-14.md §1 表逐条）
// 每个构造函数只接收**意图回调**；没接的意图传空 std::function（对应菜单项置灰）。
// ---------------------------------------------------------------------------

// ① 文件列表（含 UX-2 的勾选三项）
struct FileListIntents {
    std::function<void()> check_all;       // 勾选全部（当前可见行）
    std::function<void()> uncheck_all;     // 取消全部勾选
    std::function<void()> invert_checks;   // 反选勾选
    std::function<void()> edit_exif;       // 编辑 EXIF…
    std::function<void()> remove_selected; // 移除选中
    std::function<void()> clear_list;      // 清空列表
    // 「按分类打标 →」子菜单：(显示名, 打标回调)；调用方按分类注册表动态生成
    std::vector<std::pair<QString, std::function<void()>>> tag_targets;
    std::function<void()> copy_path; // 复制路径
    bool running = false;            // 运行中：全部置灰（G5 只读）
};
std::vector<Item> file_list_actions(const FileListIntents &in);

// ② 关键信息卡
struct InfoCardIntents {
    std::function<void()> copy_all;
    std::function<void()> reload; // 刷新（重读源文件）
    bool running = false;
};
std::vector<Item> info_card_actions(const InfoCardIntents &in);

// ③ 时间偏移卡
struct TimeCardIntents {
    std::function<void()> enable_toggle;
    bool enabled = false;
    std::function<void()> reset_defaults;
    std::function<void()> read_from_file;
    std::function<void()> clear_rules;
    bool running = false;
};
std::vector<Item> time_card_actions(const TimeCardIntents &in);

// ④ GPS 卡
struct GpsCardIntents {
    std::function<void()> enable_toggle;
    bool enabled = false;
    std::function<void()> read_from_file;
    std::function<void()> pick_on_map;
    std::function<void()> take_map_center;
    std::function<void()> copy_coords;
    std::function<void()> clear_gps;
    bool running = false;
};
std::vector<Item> gps_card_actions(const GpsCardIntents &in);

// ⑤ 隐私与文件时间卡
struct PrivacyCardIntents {
    std::function<void()> enable_toggle;
    bool enabled = false;
    std::function<void()> strip_privacy_toggle;
    bool strip_privacy = false;
    std::function<void()> sync_mtime_toggle;
    bool sync_mtime = false;
    std::function<void()> edit_keywords;
    bool running = false;
};
std::vector<Item> privacy_card_actions(const PrivacyCardIntents &in);

// ⑥ 输出格式卡
struct FormatCardIntents {
    std::function<void()> check_all_formats;
    std::function<void()> clear_formats;
    std::function<void()> restore_defaults;
    bool running = false;
};
std::vector<Item> format_card_actions(const FormatCardIntents &in);

// ⑦ 全局卡
struct GlobalCardIntents {
    std::function<void()> reset_defaults;
    std::function<void()> browse_out_root;
    bool running = false;
};
std::vector<Item> global_card_actions(const GlobalCardIntents &in);

// ⑧ 格式参数卡
struct ParamCardIntents {
    std::function<void()> reset_defaults;
    std::function<void()> copy_params;
    std::function<void()> paste_params;
    std::function<void()> expand_advanced;
    bool running = false;
};
std::vector<Item> param_card_actions(const ParamCardIntents &in);

// ⑨ 预设与预估卡
struct PresetCardIntents {
    std::function<void()> save_as;
    std::function<void()> overwrite_save;
    std::function<void()> delete_preset;
    bool running = false;
};
std::vector<Item> preset_card_actions(const PresetCardIntents &in);

// ⑩ 运行总览卡
struct RunOverviewIntents {
    std::function<void()> open_log;
    std::function<void()> open_output_dir;
    std::function<void()> cancel_remaining;
    std::function<void()> run_again;
    bool running = false;
};
std::vector<Item> run_overview_actions(const RunOverviewIntents &in);

// ⑪ 任务进度卡
struct TaskRowIntents {
    std::function<void()> copy_failure;
    std::function<void()> goto_file;
    std::function<void()> retry_file;
    bool running = false;
};
std::vector<Item> task_row_actions(const TaskRowIntents &in);

// ⑫ 日志尾卡
struct LogTailIntents {
    std::function<void()> copy_log;
    std::function<void()> open_log_file;
    std::function<void()> clear_display;
    bool running = false;
};
std::vector<Item> log_tail_actions(const LogTailIntents &in);

} // namespace pp::ui::contextmenu
