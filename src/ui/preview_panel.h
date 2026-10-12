// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M4-W2-T10 落地 / M5-T6 v6 化）— 常驻输入预览面板
// （设计 §6.1 + §3.2 中栏骨架 + §8 空态/画布 + mockups-v2/meta-dark.png 中栏视觉参照）
//
// 落地范围（§6.1 逐条 + M5-T6 的 v6 改写，行首 "// " 为注释包装）：
// clang-format off
// - 复用 `thumbs` 通道扩展：`decode_preview(path, 2048)` …… 独立低并发（≤2）后台线程池，不与转码抢核；
//   LRU 内存缓存（16 张 2048px 上限）。
// - 交互（D3）：点选/翻图（←/→）即时替换；缩放「− | 100% | ＋ | 适应 | 1:1」；徽标「原图 · 未应用修改」；
//   底部热键提示 `1 精选 2 待定 3 废片 0 清除`（热键表随分类注册表动态生成）。
// - 精度：预览**不做色彩管理之外的任何处理**；色彩管理渲染复用 ColorManager（缩略图口径 v1：sRGB 假定，与既有决策一致）。
// clang-format on
//
// 落地口径与边界（详见 m4-report 的 T10 节 + M5-T6 报告）：
//   * **解码/缓存全在 core**（`core/thumbs.h` 的 decode_preview/PreviewCache，可单测）；本类只承载
//     UI 状态 + ≤2 线程异步池 + 过期请求取消（快速翻图不堆积：队列只保留最新一条，在途结果按
//     token 判定后丢弃；在途解码不可中断 —— 与设计 §8.3「阶段边界检查、编码内不中断」同口径）。
//   * **渲染只做缩放/平移**（显示所必需），不做任何色彩/像素处理：
//     徽标「原图 · 未应用修改」的字面语义（修 1-17/1-18）；
//     「适应」= 等比完整显示（contain，可放大）；
//     「1:1」= 原始像素居中并可拖拽平移（大图裁剪在舞台内）；
//     「±/滚轮」= 阶梯比例档（100% 档与 1:1 同值）。
//     **解码侧不放大**：core 的 decode_preview 结果 ≤ 源尺寸，
//     放大只发生在绘制阶段（像素仍为源像素）。
//   * **M5-T6（v6 §8 画布）**：舞台底 `#16181d`（主题无关）+ 空态矢量图标/引导语（修 1-16，禁止纯黑
//     空画布）；缩放控件组悬浮右下（修 1-19：图标钮 + 比例读数，非 checkbox 语义）；左下操作提示；
//     照片衬垫把 letterbox 与画布底略作区分（修 3-21）；小图放大默认最近邻（修 3-20）；
//     翻图钮 28×28（修 1-21）；无文件时打标 chip 与翻图钮同档禁用（修 1-22）。
//   * 热键提示表 = `pp::ClassRegistry` 的 classes（hotkey ≠ 0）+ 保留的 `0 清除` 一枚，**动态生成**
//     （T8 的 classify 接口消费点）；打标动作本身留给 W2-T11：本面板只发 `class_hotkey(QChar)`
//     （M7-R2D1/R-278：零消费的 `set_hotkeys_enabled` 开关已随死 API 清理移除 —— 打标热键的
//     开关面只有 `set_locked`（G5 运行期只读）一条）。
//   * 运行期（G5）语义：`set_locked(true)` 只锁**打标热键**（分类只读）；翻图/缩放仍可用（预览本身
//     无修改语义）。与 mainwindow 的 lock_for_run 连线。
//   * 键盘路由：←/→/数字 走**应用级事件过滤器**（`QApplication::installEventFilter`）而不是
//     QShortcut —— QListView/QAbstractItemView 会以 ShortcutOverride 吃掉方向键（列表有焦点时
//     快捷键根本不触发）；过滤器只在「焦点窗口 == 本面板窗口、焦点不在文本输入控件、无模态/弹窗」
//     时消费按键，故搜索框里正常输入数字/方向键不受影响。
//   * M7-R2D1 / R-314：焦点在 **QAbstractItemView（文件列表树等）** 时 ←/→ **不拦**（原应用级路由
//     把方向键全吞 → 分组节的键盘折叠/展开不可达）—— 翻图热键在非列表焦点下不变；数字打标
//     两路都认（冒烟 `preview-hotkey` 以文件树为发送目标）。
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
    // 徽标/缩放条/底条的字色（舞台本体是固定深色，不随主题变 —— §4.1 画布底行）。
    void set_tokens(const theme::Tokens &tokens);
    // 文件集合（全路径，列表顺序 = 翻图顺序）。集合变化会保持「当前项」不变（越界则收敛）。
    void set_files(const QStringList &paths);
    // 当前项（= 列表行号；-1 或无选中 → 首个文件；空列表 → 空舞台）
    void set_current(int index);
    int current_index() const;
    void next();
    void previous();

    // 缩放：「适应」(true) | 「1:1」(false) —— M5-T6 起 = 缩放控件组的两枚模式钮（`set_zoom_fit`
    // 与按钮点击同一条落地路径，信号只发一次）。
    void set_zoom_fit(bool fit);
    bool zoom_fit() const;
    // 步进缩放（§8 缩放条「− | 100% | ＋」档）：percent 夹取到阶梯档（10…800，见实现）；
    // 100 档与 1:1 同值但语义为「比例档」（不点亮模式钮）。
    void set_zoom_percent(int percent);
    int zoom_percent() const; // 当前有效比例（适应档 = 按舞台实测的比例读数）

    // 热键提示表的数据源（不持有；nullptr = 空表）。T11 接管注册表后同一实例即自动跟随。
    void set_class_registry(const pp::ClassRegistry *registry);
    // 运行期只读（G5）：锁打标热键（提示表置灰），翻图/缩放不受影响。
    void set_locked(bool locked);

    // —— M7-R2.1-F4 / C4 加性（既有签名逐字不变）：线性光重采样（UX-14 C4，仅显示面）
    // 透传给 PreviewCache（切换即清缓存重解）；默认 false = 既有预览路径逐字不变。
    void set_linear_resample(bool on);
    // M7-R2D1 / R-278 死 API 清理（零消费，全仓 grep 仅定义处）：
    //   * `set_hotkeys_enabled(bool)` —— 从未被调用；热键开关只有 set_locked 一条；
    //   * `current_path()` —— 零消费（消费方一律走 current_index + 列表模型）；
    //   * `preview_loaded(int,qint64,bool)` 信号 —— 零连接（250ms 取证目标已由 mN-report 落档）。

    // 卡头显示用（mainwindow 读取后写 pill / hint 两枚标签）
    QString current_file_name() const; // 文件名（无选中 → 空）
    QString current_info_text() const; // "4032×3024 · 8 bit · Display P3"（载入前「载入中…」；
                                       // 加载失败 → 「无法预览：…」——R-277：不再恒显「载入中…」）
    QString current_position_text() const; // "1 / 24"（空列表 → ""）

    // 自检/诊断（不改变状态）
    bool image_visible() const;    // 当前项是否已有图（含缓存命中）
    QSize displayed_size() const;  // 舞台内绘制尺寸（各缩放档的绘制结果）
    bool nearest_neighbor() const; // 修 3-20：当前绘制是否走最近邻（放大档；缩小 → 平滑）
    int pending_requests() const;  // 池中排队条数（0/1）

public slots:
    void reload(); // 强制重取当前项（缓存命中即瞬时）

signals:
    void display_changed();       // 文件名/信息行/位置变化 → 卡头刷新
    void zoom_changed(bool fit);  // 缩放变化（载荷 = 是否处于适应档）
    void class_hotkey(QChar key); // 打标接线位（T11 接通；'0' = 清除）
    // M7-R2D1 / R-278：preview_loaded(index, ms, ok)（出图诊断信号）零连接，随死 API 清理移除。
    // R-151（翻图/打标对象错位）：**仅**翻图动作（←/→/翻图钮）后发一次，载荷 = 新的当前项
    // （列表行号）。消费方据此把左栏选中同步过来 —— "所见即所标"。display_changed 不发它：
    // 那是选中驱动的跟随刷新，反向同步会吞掉多选（多选 → 预览只显示首个）。加性信号。
    void navigated(int index);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pp::ui
