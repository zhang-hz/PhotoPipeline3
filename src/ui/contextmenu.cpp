// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 公共右键菜单件（M7-WP6 / UX-1）实现。
// 契约与设计要点见 contextmenu.h 头注释（docs/ux-review-14.md §1）。
#include "ui/contextmenu.h"

#include <QCoreApplication>
#include <QMenu>
#include <QWidget> // attach_card_menu：policy + popup

namespace pp::ui::contextmenu {
namespace {

QString tr_c(const char *text) { return QCoreApplication::translate("pp::ui::contextmenu", text); }

// 统一造一项（空意图 = 置灰不隐藏；运行中按调用方给的 enabled 落地；children 非空 = 子菜单）
void make_action(QMenu *menu, const Item &item) {
    if (item.separator) {
        menu->addSeparator();
        return;
    }
    if (!item.children.empty()) {
        QMenu *sub = menu->addMenu(item.text);
        sub->setEnabled(item.enabled);
        for (const Item &child : item.children)
            make_action(sub, child);
        return;
    }
    QAction *action = menu->addAction(item.text);
    action->setEnabled(item.enabled && static_cast<bool>(item.trigger));
    if (item.checkable) {
        action->setCheckable(true);
        action->setChecked(item.checked);
    }
    if (item.trigger) {
        const std::function<void()> trigger = item.trigger;
        QObject::connect(action, &QAction::triggered, menu, [trigger] { trigger(); });
    }
}

} // namespace

QMenu *menu(QWidget *parent, const std::vector<Item> &items) {
    auto *m = new QMenu(parent);
    // 样式不新写：QMenu 走 theme::style_sheet() 的既有规则（WP4 收口后已单源）。
    // 菜单生命周期由调用方负责（惯例 = WA_DeleteOnClose + popup）。
    for (const Item &item : items)
        make_action(m, item);
    return m;
}

void attach_card_menu(QWidget *card, const ItemsBuilder &builder) {
    if (card == nullptr || !builder)
        return;
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(card, &QWidget::customContextMenuRequested, card,
                     [card, builder](const QPoint &pos) {
                         const std::vector<Item> items = builder();
                         if (items.empty())
                             return;
                         QMenu *m = menu(card, items);
                         m->setAttribute(Qt::WA_DeleteOnClose);
                         m->popup(card->mapToGlobal(pos));
                     });
}

// ---------------------------------------------------------------------------
// 12 类卡的 actions 构造函数（docs/ux-review-14.md §1 表逐条；文案与表逐字对应）
// ---------------------------------------------------------------------------

std::vector<Item> file_list_actions(const FileListIntents &in) {
    const bool lock = in.running;
    std::vector<Item> items;
    // UX-2：勾选三项（作用于**当前可见（过滤后）**的行；作用域由调用方写进文案）
    items.push_back({tr_c("勾选全部"), in.check_all, !lock, false, false, false});
    items.push_back({tr_c("取消全部勾选"), in.uncheck_all, !lock, false, false, false});
    items.push_back({tr_c("反选勾选"), in.invert_checks, !lock, false, false, false});
    items.push_back(Item::sep());
    items.push_back({tr_c("编辑 EXIF…"), in.edit_exif, !lock, false, false, false});
    items.push_back({tr_c("移除选中"), in.remove_selected, !lock, false, false, false});
    items.push_back({tr_c("清空列表"), in.clear_list, !lock, false, false, false});
    items.push_back(Item::sep());
    {
        Item tag;
        tag.text = tr_c("按分类打标 →");
        tag.enabled = !lock && !in.tag_targets.empty();
        for (const auto &[name, trigger] : in.tag_targets)
            tag.children.push_back({name, trigger, !lock, false, false, false});
        items.push_back(std::move(tag));
    }
    items.push_back({tr_c("复制路径"), in.copy_path, !lock, false, false, false});
    return items;
}

std::vector<Item> info_card_actions(const InfoCardIntents &in) {
    const bool lock = in.running;
    return {{tr_c("复制全部信息"), in.copy_all, !lock, false, false, false},
            {tr_c("刷新（重读源文件）"), in.reload, !lock, false, false, false}};
}

std::vector<Item> time_card_actions(const TimeCardIntents &in) {
    const bool lock = in.running;
    return {{tr_c("启用"), in.enable_toggle, !lock, false, true, in.enabled},
            Item::sep(),
            {tr_c("重置为默认"), in.reset_defaults, !lock, false, false, false},
            {tr_c("从文件读取时间"), in.read_from_file, !lock, false, false, false},
            {tr_c("清除规则"), in.clear_rules, !lock, false, false, false}};
}

std::vector<Item> gps_card_actions(const GpsCardIntents &in) {
    const bool lock = in.running;
    return {{tr_c("启用"), in.enable_toggle, !lock, false, true, in.enabled},
            Item::sep(),
            {tr_c("从文件读取 GPS"), in.read_from_file, !lock, false, false, false},
            {tr_c("地图选点"), in.pick_on_map, !lock, false, false, false},
            {tr_c("取地图中心"), in.take_map_center, !lock, false, false, false},
            {tr_c("复制坐标"), in.copy_coords, !lock, false, false, false},
            {tr_c("清除 GPS"), in.clear_gps, !lock, false, false, false}};
}

std::vector<Item> privacy_card_actions(const PrivacyCardIntents &in) {
    const bool lock = in.running;
    return {{tr_c("启用"), in.enable_toggle, !lock, false, true, in.enabled},
            Item::sep(),
            {tr_c("隐私剥除"), in.strip_privacy_toggle, !lock, false, true, in.strip_privacy},
            {tr_c("时间同步"), in.sync_mtime_toggle, !lock, false, true, in.sync_mtime},
            {tr_c("编辑关键字…"), in.edit_keywords, !lock, false, false, false}};
}

std::vector<Item> format_card_actions(const FormatCardIntents &in) {
    const bool lock = in.running;
    return {{tr_c("全选格式"), in.check_all_formats, !lock, false, false, false},
            {tr_c("清除选择"), in.clear_formats, !lock, false, false, false},
            {tr_c("恢复默认格式"), in.restore_defaults, !lock, false, false, false}};
}

std::vector<Item> global_card_actions(const GlobalCardIntents &in) {
    const bool lock = in.running;
    return {{tr_c("重置为默认"), in.reset_defaults, !lock, false, false, false},
            {tr_c("浏览输出目录…"), in.browse_out_root, !lock, false, false, false}};
}

std::vector<Item> param_card_actions(const ParamCardIntents &in) {
    const bool lock = in.running;
    return {{tr_c("重置为默认"), in.reset_defaults, !lock, false, false, false},
            Item::sep(),
            {tr_c("复制参数"), in.copy_params, !lock, false, false, false},
            {tr_c("粘贴参数"), in.paste_params, !lock, false, false, false},
            {tr_c("展开全部高级参数"), in.expand_advanced, !lock, false, false, false}};
}

std::vector<Item> preset_card_actions(const PresetCardIntents &in) {
    const bool lock = in.running;
    return {{tr_c("另存为预设…"), in.save_as, !lock, false, false, false},
            {tr_c("覆盖保存"), in.overwrite_save, !lock, false, false, false},
            {tr_c("删除预设"), in.delete_preset, !lock, false, false, false}};
}

std::vector<Item> run_overview_actions(const RunOverviewIntents &in) {
    // 运行总览：运行中只留「取消剩余 / 打开输出目录」可点（其余按语义置灰）
    const bool lock = in.running;
    return {{tr_c("查看完整日志…"), in.open_log, true, false, false, false},
            {tr_c("打开输出目录"), in.open_output_dir, true, false, false, false},
            Item::sep(),
            {tr_c("取消剩余"), in.cancel_remaining, lock, false, false, false},
            {tr_c("再次运行"), in.run_again, !lock, false, false, false}};
}

std::vector<Item> task_row_actions(const TaskRowIntents &in) {
    const bool lock = in.running;
    return {{tr_c("复制失败原因"), in.copy_failure, !lock, false, false, false},
            {tr_c("跳到该文件"), in.goto_file, !lock, false, false, false},
            {tr_c("重试该文件"), in.retry_file, !lock, false, false, false}};
}

std::vector<Item> log_tail_actions(const LogTailIntents &in) {
    return {{tr_c("复制日志"), in.copy_log, true, false, false, false},
            {tr_c("打开日志文件"), in.open_log_file, true, false, false, false},
            {tr_c("清空显示"), in.clear_display, !in.running, false, false, false}};
}

} // namespace pp::ui::contextmenu
