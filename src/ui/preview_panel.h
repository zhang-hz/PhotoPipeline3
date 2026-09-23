// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M4-W2-T10）— 常驻输入预览面板（设计 §6.1 / mockup meta-dark.html 中栏）
//
// 落地范围（§6.1 逐条，行首 "// " 为注释包装）：
// clang-format off
// - 复用 `thumbs` 通道扩展：`decode_preview(path, 2048)` …… 独立低并发（≤2）后台线程池，不与转码抢核；
//   LRU 内存缓存（16 张 2048px 上限）。
// - 交互（D3）：点选/翻图（←/→）即时替换；缩放「适应 | 1:1」；徽标恒显「输入 · 未修改像素」；底部热键提示
//   `1 精选 2 待定 3 废片 0 清除`（热键表随分类注册表动态生成）。
// - 精度：预览**不做色彩管理之外的任何处理**；色彩管理渲染复用 ColorManager（缩略图口径 v1：sRGB 假定，与既有决策一致）。
// clang-format on
//
// 落地口径与边界（详见 m4-report 的 T10 节）：
//   * **解码/缓存全在 core**（`core/thumbs.h` 的 decode_preview/PreviewCache，可单测）；本类只承载
//     UI 状态 + ≤2 线程异步池 + 过期请求取消（快速翻图不堆积：队列只保留最新一条，在途结果按
//     token 判定后丢弃；在途解码不可中断 —— 与设计 §8.3「阶段边界检查、编码内不中断」同口径）。
//   * **渲染只做缩放/平移**（显示所必需），不做任何色彩/像素处理：徽标「输入 · 未修改像素」的字面
//     语义；「适应」= 等比缩放铺满舞台（可放大，与 mockup 的满幅舞台一致）、「1:1」= 原始像素居中 +
//     可拖拽平移（大图裁剪在舞台内）。**解码侧不放大**：core 的 decode_preview 结果 ≤ 源尺寸，
//     这里的放大只是绘制阶段的事（像素仍为源像素）。
//   * 热键提示表 = `pp::ClassRegistry` 的 classes（hotkey ≠ 0）+ 保留的 `0 清除` 一枚，**动态生成**
//     （T8 的 classify 接口消费点）；打标动作本身留给 W2-T11：本面板只发 `class_hotkey(QChar)`，
//     并由 `set_hotkeys_enabled(false)` 给 T11 留出「改由分类面板自管热键」的开关。
//   * 运行期（G5）语义：`set_locked(true)` 只锁**打标热键**（分类只读）；翻图/缩放仍可用（预览本身
//     无修改语义）。与 mainwindow 的 lock_for_run 连线。
//   * 键盘路由：←/→/数字 走**应用级事件过滤器**（`QApplication::installEventFilter`）而不是
//     QShortcut —— QListView/QAbstractItemView 会以 ShortcutOverride 吃掉方向键（列表有焦点时
//     快捷键根本不触发）；过滤器只在「焦点窗口 == 本面板窗口、焦点不在文本输入控件、无模态/弹窗」
//     时消费按键，故搜索框里正常输入数字/方向键不受影响。
#pragma once

#include "core/classify.h"
#include "core/thumbs.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <memory>

namespace pp::ui {

// 前置声明（不拉 ui/theme.h）：theme.h 依赖包含者先引入 <QApplication>（qApp->style()），
// 而本头会被 AUTOMOC 的 mocs_compilation.cpp 直接包含 —— 在头里 include theme.h 会把
// qApp 钉成 QGuiApplication 而编译失败（mainwindow.h 同口径：头文件不引 theme.h）。
// 值的完整定义只在 preview_panel.cpp 里需要。
namespace theme {
struct Tokens;
}

// 常驻输入预览面板（中栏）。文件集合与「当前项」由父窗口喂入（列表/过滤归左栏 T11；本面板只按
// **列表顺序**翻图）。索引口径 = 只读列表行号；`set_current(-1)`（无选中）回落到首个文件
// （设计 §5.2「无选中 → 显示首个文件」的同一条跟随口径）。
class PreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPanel(QWidget *parent = nullptr);
    ~PreviewPanel() override;

    // 主题 tokens（单源 ui/theme.h）：由 mainwindow 的 refresh_theme 驱动；同时更新舞台内
    // 徽标/缩放 chip/底条的字色。
    void set_tokens(const theme::Tokens &tokens);
    // 文件集合（全路径，列表顺序 = 翻图顺序）。集合变化会保持「当前项」不变（越界则收敛）。
    void set_files(const QStringList &paths);
    // 当前项（= 列表行号；-1 或无选中 → 首个文件；空列表 → 空舞台）
    void set_current(int index);
    int current_index() const;
    void next();
    void previous();

    // 缩放：「适应」(true) | 「1:1」(false)
    void set_zoom_fit(bool fit);
    bool zoom_fit() const;

    // 热键提示表的数据源（不持有；nullptr = 空表）。T11 接管注册表后同一实例即自动跟随。
    void set_class_registry(const pp::ClassRegistry *registry);
    // 运行期只读（G5）：锁打标热键（提示表置灰），翻图/缩放不受影响。
    void set_locked(bool locked);
    // T11 若自管热键（自己的 QShortcut/事件过滤器）→ 关掉本面板的热键消费，避免按键歧义。
    void set_hotkeys_enabled(bool on);

    // 卡头显示用（mainwindow 读取后写 pill / hint 两枚标签）
    QString current_file_name() const; // 文件名（无选中 → 空）
    QString current_info_text() const; // "4032×3024 · 8 bit · Display P3"（载入前为空）
    QString current_position_text() const; // "1 / 24"（空列表 → ""）
    QString current_path() const;

    // 自检/诊断（不改变状态）
    bool image_visible() const;   // 当前项是否已有图（含缓存命中）
    QSize displayed_size() const; // 舞台内绘制尺寸（适应/1:1 各自结果）
    int pending_requests() const; // 池中排队条数（0/1）

public slots:
    void reload(); // 强制重取当前项（缓存命中即瞬时）

signals:
    void display_changed();                          // 文件名/信息行/位置变化 → 卡头刷新
    void zoom_changed(bool fit);                     // 适应|1:1 切换
    void class_hotkey(QChar key);                    // 打标接线位（T11 接通；'0' = 清除）
    void preview_loaded(int index, qint64 ms, bool ok); // 出图诊断（250ms 目标取证）

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pp::ui
