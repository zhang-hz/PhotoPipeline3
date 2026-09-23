// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M4-W2-T11）— 分类面板（设计 §6.2 / mockup 左栏 .cls-panel）
//
// 落地范围（§6.2 逐条，行首 "// " 为注释包装）：
// clang-format off
// - `ClassRegistry`：增删改名/颜色/热键（0-9）；"全部"为保留虚拟分类。默认模板：精选/待定/废片。
// - 归属：一文件一分类（可无）；打标即写 registry；持久化 `data_dir()/classes.json`（键=规范化绝对路径；文件不在列表时惰性清理）。
// - 圈选语义（D2）：……分类行右键菜单「全选该类/反选该类/清空勾选」；搜索过滤与勾选正交（勾选状态不因过滤丢失）。
// clang-format on
//
// 控件面（mockup meta-dark.html 左栏 `.cls-panel`，逐条）：
//   * 行 = 色点 9px + 类名 11.5px（--txt2；.on 时 --txt/700）+ 右端计数 10.5px（--txt3）；
//     首行 = 保留虚拟分类「全部」（计数 = 文件总数；色点用 --txt3，对应 mockup #c9c9d2）；
//   * 末行 = 全宽「＋ 新建分类…」按钮（`mini-btn` 口径，objectName pp-class-new）；
//   * 卡头 hint = 「1–9 键打标」（运行中 = 「运行中只读」，由 mainwindow 依 G5 切换）。
//   * 卡片框/卡头由 mainwindow 承载（与本窗口其余卡片同一 pp-card 口径）；本类只出**卡体**。
//
// 交互（§6.2 的落地口径；未逐字处已记偏差账）：
//   * **右键菜单**（唯一交互入口）：类行 = 「全选该类 / 反选该类 / 清空勾选」+（非「全部」行）
//     「重命名… / 颜色… / 热键… / 删除」；「全部」行只有前三项（保留虚拟分类不可增删改名/设色/
//     设热键 —— core/classify 的强校验，本类不绕过）。
//   * **打标**走预览面板的热键接线位（`PreviewPanel::class_hotkey` → mainwindow 写注册表），
//     「0 = 清除」；本类不新增点击即打标的语义（mockup 的 `.on` 只表示"当前选中项的归属"，
//     不做筛选 —— §6.2 未定义按类筛选，避免扩义）。
//   * `.cls-row.on` = **跟随文件列表选中项**的类别高亮（§5.2 同一条跟随口径；无选中/多选混合
//     → 高亮「全部」）——静态外观与 mockup（默认「全部」高亮）一致。
//   * 运行期只读（G5）：`set_locked(true)` → 行与「＋ 新建分类…」置灰、右键菜单不弹（mockup
//     run-dark 的 `.cls-new .mini-btn.dim` + hint「运行中只读」）。
//
// 状态与所有权：注册表与文件模型均为**外部所有**（mainwindow）；本类只读注册表渲染、经公开
// CRUD 方法写注册表并发 `registry_changed()`（消费端据此落盘 classes.json，§6.2「打标即写
// registry」的持久化面）。
#pragma once

#include "core/classify.h"

#include <QString>
#include <QStringList>
#include <QWidget>

#include <memory>

class QMenu;

namespace pp::ui {

class FileListModel;

// 前置声明（不拉 ui/theme.h）：theme.h 依赖包含者先引入 <QApplication>（qApp->style()），
// 而本头会被 AUTOMOC 的 mocs_compilation.cpp 直接包含（mainwindow.h 同口径）。
namespace theme {
struct Tokens;
}

class ClassifyPanel : public QWidget {
    Q_OBJECT
public:
    explicit ClassifyPanel(QWidget *parent = nullptr);
    ~ClassifyPanel() override;

    // 主题 tokens（单源 ui/theme.h）：由 mainwindow 的 refresh_theme 驱动
    void set_tokens(const theme::Tokens &tokens);
    // 注册表 / 文件模型（都不持有；nullptr = 空面板）
    void set_registry(pp::ClassRegistry *registry);
    void set_model(FileListModel *model);
    // 运行期只读（G5）：置灰 + 不弹菜单
    void set_locked(bool locked);
    bool locked() const;

    // 当前选中项的类别（`.cls-row.on` 高亮；空串 = 无分类 → 高亮「全部」）
    void set_current_class_id(const QString &class_id);

    // ---- 圈选（§6.2 分类行右键菜单的三项；公开 = 菜单与冒烟共用同一实现）----
    void check_class(const QString &class_id);  // 全选该类（class_id 空 = 「全部」= 全部文件）
    void invert_class(const QString &class_id); // 反选该类
    void clear_checks();                        // 清空勾选（全部取消）

    // ---- CRUD（§6.2「增删改名/颜色/热键（0-9）」；菜单经对话框调用，冒烟直调）----
    bool new_class(const QString &name, std::uint32_t rgb = 0, char hotkey = 0);
    bool rename_class(const QString &id, const QString &name);
    bool set_class_color(const QString &id, std::uint32_t rgb);
    bool set_class_hotkey(const QString &id, char key); // key 0 = 清除热键
    bool delete_class(const QString &id);

    // ---- 冒烟/诊断（不改变状态）----
    int row_count() const;              // 行数 = 1（「全部」）+ classes.size()
    QString row_id(int row) const;      // "" = 「全部」
    QString row_label(int row) const;   // 行名（「全部」/ 类名）
    int row_count_value(int row) const; // 行尾计数
    bool row_active(int row) const;     // .cls-row.on
    QStringList hotkey_labels() const;  // 热键行读数（"1=精选" …）
    QWidget *row_widget(int row) const; // 行部件（右键菜单命中/几何断言用）
    // 构造类行右键菜单（不 exec；冒烟据此断言项集合并按项触发）。父 = 本面板，调用方可 delete。
    QMenu *build_row_menu(int row);

signals:
    void registry_changed(); // CRUD/颜色/热键变化 → 消费端落盘 classes.json
    // 被拒操作（热键冲突 / 保留 id / 空名 …）的**非模态**反馈：不弹对话框（弹出会阻塞脚本化
    // 走查；面板的对话框只出现在用户主动打开的重命名/颜色/删除菜单项里）
    void error_occurred(const QString &message);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pp::ui
