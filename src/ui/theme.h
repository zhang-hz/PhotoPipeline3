// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M5-T1）— GUI 主题 tokens **单源**（设计说明书 v6 §4「设计令牌」）
//
// 真源 = docs/qt-gui-design-spec.md（v6）§4.1 颜色 / §4.2 字阶 / §4.3 间距与圆角 + §5 R4 控件规格；
// 视觉对照 = docs/mockups-v2/tokens.css（规范与对照冲突处以规范逐字为准；逐条偏差见 M5-T1 报告）。
// 本文件只出**值**与少量生成函数（Tokens / QPalette / QSS / QFont / set_mod），不建控件、
// 不持状态；set_mod/repolish 只对调用方传入的控件做 setProperty + unpolish/polish。
//
// 边界（M5-T1 划出 / M5-T4 已落地）：§4 令牌 + R4 控件度量在本文件；§5 表单栅格（标签轴 70→80 /
// 行 pitch 32→28）归 M5-T2 formkit（C2 契约）；§3 骨架尺寸（顶栏 52→44 / 状态栏 48→30 /
// 栏宽 258→330·587 / 窗口内边距 12·10→10·8）+ §6.2 一体式分段导航（92×30）+ caption 单轨
// 46×32 由 M5-T4 壳层在本文件换锚（值仍只此处一份，调用点零逻辑改动由取值单源保证）。
//
// 命中几何的单源（M4-W2-fix 第 8 条）：缩放带与 caption 命中带高度都**不在本文件取值**，
// 而是从 platform/frameless.h 转发（kResizeBorderPx / kResizeBorderMaximizedPx /
// kCaptionHitHeightPx）—— 窗口命中判定与 QSS 尺寸永远同源。
//
// 明暗跟随系统：system_theme_mode() 读 QStyleHints::colorScheme（Qt 6.5+；Windows/Linux
// 桌面由平台主题喂入）。Unknown（含 offscreen 平台）→ 深色，理由 = 原型基准是 meta-dark。
//
// 单位口径：px 均为**逻辑像素**（Qt 设备无关像素），与 mockup CSS px 同口径；QFont 走
// setPointSizeF(px*72/96) 换算（Qt 的 px↔pt 定义就是 96dpi 逻辑像素），因此 QSS 里不出现
// 小数 px（Qt QSS 的 font-size 只吃整数）；§4.2 的 11.5 / 9.5px 走 QFont::setPointSizeF。

#pragma once

// A5（M5-T4 修复）：本文件用 `qApp->style()`（style_mirrors_system_accent，QApplication 的成员），
// 但此前只引 <QGuiApplication> —— qApp 由 <QApplication> 定为 QApplication*，只引 QGui 侧时
// qApp 展开成 QGuiApplication*（无 style()）而编译失败：包含顺序雷（formkit.h 曾被迫在引本头之前
// 显式先引 <QApplication> 规避，见 C2 契约）。此处显式引 QtWidgets 侧（本文件本来就用
// QWidget/QStyle/QPalette，QtWidgets 依赖既存），隐式包含依赖清零。
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPalette>
#include <QString>
#include <QStyle>
#include <QStyleHints>
#include <QWidget> // set_mod/repolish 的入参类型（只做 setProperty + unpolish/polish）
#include <Qt>
#include <QtGlobal>

#include <cmath>

#include "platform/frameless.h" // 命中区/缩放带常量单源（kResizeBorderPx 等）
#include "ui/icons.h"           // 图标线宽单源（M5-T3：§3.3 等线宽 1.4–1.6px）

namespace pp::ui {
namespace theme {

enum class ThemeMode { Light, Dark };

// ---------------------------------------------------------------------------
// 排印（§4.2 字阶）：Segoe UI Variable Text（回退 Microsoft YaHei UI）。
// 五个字阶角色逐条对应 §4.2 表；数值（tabular-nums + 右对齐）走 font(..., tabular=true)
// 与各页的 AlignRight —— 令牌层只给字号/字重/字特性，位置由 §4.2「数值」行约束各页。
// ---------------------------------------------------------------------------
struct Typography {
    static constexpr const char *family = "Segoe UI Variable Text"; // §4.2 字族（逐字）
    static constexpr const char *family_ui = "Microsoft YaHei UI";  // §4.2 中文回退（逐字）
    static constexpr const char *family_mono = "Consolas";          // 日志尾/模板输入（§7.3 等宽）
    static constexpr double caption_px = 13.0;                      // §3.3 品牌标题 13/700
    static constexpr int caption_weight = 700;
    static constexpr double base_px = 11.5;       // §4.2 正文·值（QApplication 基准）
    static constexpr double card_title_px = 12.0; // §4.2 卡题 12/700
    static constexpr int card_title_weight = 700;
    // §4.2 徽标 9.5/700 —— 状态/计数 pill 与导航编号圆同一档
    static constexpr double badge_px = 9.5;
    static constexpr int badge_weight = 700;
    // §6.2 导航编号圆（内嵌数字圆）：字号/字重取 §4.2 徽标档 9.5/700（裁定 A4：tokens.css 的
    // 9px 作废）。M5-T4：v4 的 .step 三独立钮 → v6 一体式分段导航（单控件），命名随之换锚。
    // R-267（M7-WP4）：取值**引用**徽标档常量（原写死 9.5 的自重复清零）。
    static constexpr double nav_num_px = badge_px;
    static constexpr int nav_num_weight = badge_weight;
    // §6.2 导航段文案：tokens.css .nav-strip button 的 12px/600（§4.2 字阶表未列导航档，
    // 按 A11 口径取对照值；段宽 92 等宽由 Metrics 承担）。
    static constexpr double nav_text_px = 12.0;
    static constexpr int nav_text_weight = 600;
    static constexpr double hint_px = 10.0;  // §4.2 辅助 10/400（txt3：hint/范围/单位说明）
    static constexpr double body_px = 11.5;  // §4.2 正文·值 11.5/400（字段值、控件文字）
    static constexpr double small_px = 11.0; // §4.2 标签 11/400（txt2：表单标签、kv 标签）
    static constexpr double mono_px = 11.5;  // 等宽（日志尾/路径模板）
};

// ---------------------------------------------------------------------------
// 尺寸基准（§3 骨架，归 M5-T4）+ 设计令牌（§4.3 间距/圆角）+ 控件度量（§5 R4 逐行）。
// 除本任务列明的 §4/R4 项外，其余取值原样保留（调用点零逻辑改动由取值单源保证）。
// ---------------------------------------------------------------------------
struct Metrics {
    // ---- §3 骨架（M5-T4 壳层 v6 换锚：44 / 30 / 330·587 / 10·8）----
    static constexpr int toolbar_height = 44; // §3.1 顶栏 44
    static constexpr int bottom_height = 30;  // §3.1 状态栏 30
    static constexpr int main_pad_top = 8;    // §3.2 窗口内边距 上下 8
    static constexpr int main_pad_x = 10;     // §3.2 窗口内边距 左右 10
    static constexpr int main_pad_bottom = 8;
    // §3.2 栏宽固定轴：左 330（280–400 可调）/ 右 587（520–680 可调）/ 中弹性（mockup
    // .col-l/.col-r 为 `flex:0 0`、.col-m 为 `flex:1 1 0`）。"可调"= 用户拖分栏手柄的上下界，
    // 基线（= 初始尺寸）恒为 330 / 587；窗口缩放只动中栏（§12-16「栏宽固定轴，切页列不错位」）。
    static constexpr int left_width = 330;
    static constexpr int left_width_min = 280;
    static constexpr int left_width_max = 400;
    static constexpr int right_width = 587;
    static constexpr int right_width_min = 520;
    static constexpr int right_width_max = 680;
    static constexpr int min_window_w = 1180; // §3.1 最小窗口
    static constexpr int min_window_h = 720;
    static constexpr int calibrated_w = 1440; // §3.1 参考尺寸
    static constexpr int calibrated_h = 900;
    // 中栏弹性：1440 校准落点 = 1440 − 2×10（窗口内边距）− 330 − 587 − 2×8（列距/分栏手柄）
    // = 487（§3.2 三栏基线的算术结果，探针取证）。
    static constexpr int mid_width_1440 = 487;
    // 弹性因子：只有中栏吃窗口余量（左/右 = 0 → 固定轴）；mid_stretch ≠ 0 是"弹性列"的语义位。
    static constexpr int mid_stretch = 1;
    static constexpr int right_stretch = 0;
    // ---- caption（§3.1：Win11 规格 46×32）----
    static constexpr int caption_btn_w = 46; // .cap{width:46px}
    // **单轨（M5-T4，裁定 A1）**：可见面与命中区同为 46×32（§3.3 逐字「46×32、透明底、悬停灰、
    // 关闭悬停 #c42b1c」）—— 命中几何真源 = platform/frameless.h::kCaptionHitHeightPx
    // （窗口侧单源，此处只转发）；M4 的"46×满高 hover 面 + 居中 32px 命中带"双轨随顶栏 44
    // 换锚收敛（frameless 命中语义零弱化：带外按下不触发、HTMAXBUTTON Snap 保留）。
    static constexpr int caption_btn_hit_h = pp::platform::kCaptionHitHeightPx;
    static constexpr int caption_btn_h = caption_btn_hit_h; // 单轨：可见面 = 命中面
    // 命中区/缩放带宽度：**单源规则**（M4-W2-fix 第 8 条）—— 真源 = platform/frameless.h
    // （§3.1 6px；最大化 1px），本处只转发（改 frameless.h 的常量即全局传导）。
    static constexpr int resize_border = pp::platform::kResizeBorderPx;
    static constexpr int resize_border_max = pp::platform::kResizeBorderMaximizedPx;
    static constexpr int icon_btn = 28;       // R4 图标钮 28×28（v4 32 → v6 28）
    static constexpr int icon_btn_radius = 4; // R4 图标钮 r4
    // §3.3「图标一律 SVG 等线宽（1.4–1.6px）」：真源 = ui/icons.h::kStrokePx（图标库的线宽口径：
    // 24 网格是形、1.5px 是**屏幕线宽**），此处只转发 —— 与缩放带/命中带同一条单源规则（M5-T3
    // 加性扩展）。
    static constexpr qreal icon_stroke_px = pp::ui::icons::kStrokePx;
    // §3.3 顶栏盒（v6：tokens.css `.toolbar{height:44px;display:flex;align-items:center;gap:12px;
    // padding:0 6px 0 14px}` + `.brand{gap:8px}` + 品牌图标 20）——三段式（品牌区｜导航区｜
    // 工具与窗口控制区）由调用方按这三个值 + 居中的导航区落地（修 5-01：中段不得大片空置）。
    static constexpr int titlebar_pad_left = 14;
    static constexpr int titlebar_pad_right = 6;
    static constexpr int toolbar_gap = 12; // .toolbar{gap:12px}（三段之间的基准距）
    static constexpr int brand_gap = 8;    // .brand{gap:8px}
    static constexpr int brand_icon = 20;  // §3.3「品牌区（图标 20…）」
    // §6.2 一体式分段导航（**单控件**；M5-T4 把 v4 的 .step 三独立钮换锚为此）：共享描边 +
    // 段间 hairline + 选中段填充 acc + 数字圆内嵌；段宽 92、高 30（= 段所在行高 = 导航条高）。
    static constexpr int nav_h = 30;     // §6.2 高 30
    static constexpr int nav_seg_w = 92; // §6.2 段宽 92（等宽 → 选中/悬停零宽度变化，§12-4）
    static constexpr int nav_radius = 6; // tokens.css .nav-strip{border-radius:6px}
    static constexpr int nav_border = 1; // 共享描边 1px（段间 hairline 同宽 = --ctl-bd）
    static constexpr int nav_num = 15;   // .nav-strip .n{width:15px;height:15px;border-radius:99px}
    static constexpr int nav_num_gap = 6; // .nav-strip button{gap:6px}（数字圆 ↔ 文案）
    // 状态栏盒（§3.1 高 30；tokens.css `.statusbar{padding:0 12px;gap:8px;font-size:11px}`）
    static constexpr int statusbar_pad_x = 12;
    static constexpr int statusbar_gap = 8;
    static constexpr int statusbar_sep_h = 14; // 段间分隔 hairline 高（分区用，修 5-13）
    // ---- §4.3 间距令牌：4 / 8 / 12 / 16（卡间距 8、卡内边距 10–12、列距 12、组内 8）----
    static constexpr int spacing_xs = 4;  // 令牌 4
    static constexpr int spacing_s = 8;   // 令牌 8（卡间距 / 组内 / 控件间）
    static constexpr int spacing_m = 12;  // 令牌 12（列距 / 卡内边距）
    static constexpr int spacing_l = 16;  // 令牌 16
    static constexpr int gap = spacing_s; // 卡间距 8（§4.3；同时是 splitter handle 宽 = 列间距）
    static constexpr int group_gap = spacing_s;       // §4.3 组内 8（组内控件/磁贴行距）
    static constexpr int card_pad_top = 10;           // R1 卡体上内边距 10
    static constexpr int card_pad_x = spacing_m;      // §4.3 卡内边距 10–12（左右 12）
    static constexpr int card_pad_bottom = spacing_m; // R1 卡体下内边距 12
    // ---- §4.3 圆角：卡片 6 / 控件 3 / 按钮 3–4 / pill 3 / 圆点 99 / 画布 4 / 对话框 6 ----
    static constexpr int card_radius = 6;    // §4.3 卡片（v4 8 → v6 6）
    static constexpr int control_radius = 3; // §4.3 控件（v4 4 → v6 3）
    static constexpr int btn_radius = 3;     // §4.3 按钮 3（.btn）；主按钮/.go 取 4
    static constexpr int stage_radius = 4;   // §4.3 画布（v4 6 → v6 4）
    static constexpr int dialog_radius = 6;  // §4.3 对话框
    static constexpr int go_radius = 4;      // §4.3 按钮 3–4 的上档（.btn-go r4）
    static constexpr int go_height = 22;     // R4 主 CTA 状态栏 22px（v4 34 → v6 22）
    static constexpr int cta_h = go_height;  // 主 CTA 别名（同一取值，两处命名都可用）
    static constexpr int go_pad_x = 18;      // .btn-go{padding:0 18px}
    static constexpr int pill_radius = 3;    // §4.3 pill 3（计数/状态）
    // QSS 侧等效值：Qt 的 QSS border-radius 超过盒半高时退化为直角，故 QSS 一律用 ≤ 半高的
    // 半径；v6 pill 本身 r3（盒高 16）→ 直接同一值（R-214：旧别名 pill_radius_qss 已删，取值
    // 只剩 pill_radius 一处）。
    static constexpr int pill_h = 16;     // R4 徽章 pill 高 16
    static constexpr int pill_pad_x = 7;  // .pill{padding:0 7px}
    static constexpr int pill_pad_y = 0;  // 竖内距 0：16px 盒高 = 9.5px 字（≈13–14px）+ 2px 边
    static constexpr int dot_px = 8;      // §4.3 圆点 8px
    static constexpr int dot_radius = 99; // §4.3 圆点 r99（规格值，供自绘消费）
    // .app-title .ver{padding:0 6px;border-radius:99px}（版本药丸 = 胶囊口径，与 r3 的 pill
    // 不同槽）
    static constexpr int ver_pad_x = 6;
    static constexpr int ver_pad_y = 1;
    static constexpr int ver_radius_qss = 8; // 99 的 QSS 等效（16–18px 盒 → 半高）
    // ---- R4 控件度量（逐行）----
    static constexpr int field_w = 160;      // R2 标准字段宽 160（下拉/独立数值/短文本）
    static constexpr int field_w_short = 88; // R2 短档 88（组内小数值）
    static constexpr int spin_h = 24;        // R4 spinbox 高 24
    static constexpr int spin_arrow_w = 17;  // R4 箭头列 17px
    static constexpr int spin_arrow_h = 12;  // R4 箭头列内 ▲/▼ 各 12（= 24 / 2）
    // M5-T2 加性扩展（登记见报告）：R4 表未列 spin 值区内距，取 mockup `.spin .v{padding:0 7px}`
    static constexpr int spin_pad_x = 7;              // spinbox 值区内距（左右）
    static constexpr int combo_h = 24;                // R4 下拉高 24（宽 160 = field_w）
    static constexpr int combo_arrow_px = 8;          // R4 下拉箭头 8px 居右（.dd .car）
    static constexpr int seg_h = 24;                  // R4 分段控件高 24
    static constexpr int seg_w = 88;                  // R4 分段段宽 88（等宽）
    static constexpr int seg_radius = control_radius; // R4 分段 r3
    static constexpr int btn_h = 26;                  // R4 按钮高 26（v4 28 → v6 26）
    static constexpr int progress_h = 6;              // R4 进度条高 6
    static constexpr int progress_radius = 99;        // R4 进度条 r99（规格值，供自绘消费）
    static constexpr int thumb_px = 46;               // R4 缩略图 46×46（§7.5 文件行）
    static constexpr int thumb_radius = 4;            // R4 缩略图 r4
    static constexpr int switch_knob = 12;            // R4 开关钮 12（轨 30×16）
    // 运行期锁定（G5）的"变暗"系数：mockup .icon-btn.dim{opacity:.4} / .step.dim{opacity:.45}
    // （整钮不透明度；图标钮走 QSS 预合成色，导航段走自绘 painter.setOpacity —— M5-T4 随
    // 一体式分段导航改名，取值与语义不变）
    static constexpr double icon_dim_opacity = 0.4;
    static constexpr double nav_dim_opacity = 0.45;
    static constexpr int mod_weight = 700;    // .spin.mod{font-weight:700}
    static constexpr int mod_border_px = 1;   // .spin.mod 边框 1px
    static constexpr int checkbox_px = 13;    // R4 复选框 13×13（v4 15 → v6 13）
    static constexpr int checkbox_radius = 2; // R4 复选框 r2
    static constexpr int switch_w = 30;       // R4 开关 30×16 药丸（v4 34×18 → v6 30×16）
    static constexpr int switch_h = 16;
    static constexpr int slider_track_px = 4; // §7.4 alpha 底色行滑条 4px 轨 + 13px 柄
    static constexpr int slider_handle_px = 13;
    static constexpr int mini_progress_px = progress_h; // R4 迷你进度条（任务表逐输出行）
    static constexpr int mini_progress_radius = progress_radius;
    static constexpr int big_progress_px = progress_h; // R4 总览合成条
    static constexpr int progress_border_px = 1;       // .obar/.bigbar{border:1px solid --ctl-bd}
    static constexpr int synth_stripe_deg = 115;       // .obar i.synth 条纹角度
    static constexpr int synth_stripe_band_px = 6;     // 单色带宽度（周期 = 2×6px）
    // 条纹周期（= 2×band，mockup 的 `#3aa3dc 0 6px, #4cc2ff 6px 12px`）：迷你条按它铺
    // RepeatSpread 渐变；单列一个常量是为了让"115°/6px/12px"三个几何量都在 token 表里可查。
    static constexpr int synth_stripe_period_px = 12;
    static constexpr int input_h = 24; // R4 文本输入高 24（v4 28 → v6 24）
    // R-112（M7-WP4）：M4 遗留死常量 `row_h = 32` 已删 —— 表单行 pitch 真源 = formkit_layout.h
    // `kRowPitch = 28`（R1），零调用的第二行高不得与真源并存（R-216 双行高并存随之清零）。
    // ---- 按钮/下拉盒内距（R-186/R-270 令牌化：QSS 魔法值全部走本表）----
    // R-186：页内按钮 padding 12（page_run）vs 11（page_output/page_meta/dialogkit）双份漂移 →
    // 统一为本表单源（主次按钮同一横向内距；主按钮实心靠底色区分，不靠内距）。
    static constexpr int btn_pad_x = 12;
    static constexpr int btn_h_content = btn_h - 2; // QSS min/max-height（内容盒 24 = 26 − 2×1 边）
    static constexpr int combo_pad_x = 8;           // 下拉值区内距（.dd 文字 0 8px）
    // 下拉箭头位宽（= ::drop-down width = combo_arrow_px×2 + 8）与值区右内距**同一取值**：
    // R-270 点名的「combo 26 vs 24」两值漂移（padding-right 26 硬编码 vs 箭头位 24
    // 计算值）随本行归一。
    static constexpr int combo_arrow_slot = combo_arrow_px * 2 + 8;
    // ---- 滚动条（UX-11：SPA 式细浮层 —— 轨道透明、滑块 6px、悬停加宽 8px；两主题一套、
    // 弹窗与主窗一套，见 theme::scrollbar_qss）----
    static constexpr int scroll_w = 6;           // 滑块/滚动条宽 6px（原 10px 实心轨退役）
    static constexpr int scroll_w_hover = 8;     // 悬停加宽 8px
    static constexpr int scroll_radius = 3;      // 滑块圆角 3（= 半宽）
    static constexpr int scroll_handle_min = 24; // 滑块最小长度
    // ---- 覆盖层几何（UX-6⑦：与 OverlayTokens 配套；浮层不随主题变）----
    static constexpr int overlay_pad_x = 8; // 预览徽标 .pv-badge{padding:3px 8px}
    static constexpr int overlay_pad_y = 3;
    static constexpr int overlay_radius = 4;
    static constexpr int zoombar_radius = 4; // .zoombar{border-radius:4px}
    static constexpr int zoom_text_btn_pad_x = 6;
    // ---- §5 表单栅格（R1/R2；归 M5-T2 formkit：标签轴 80 / pitch 28 / 双列 24）----
    // 取值单源 = ui/formkit_layout.h（kLabelW/kRowPitch/kDoubleGap），formkit.h 以 static_assert
    // 与本表逐条对齐。A10/T11 退役的**迁移期双源**（M4 的 form_label_w=70 / form_row_h=32：
    // W-G2/W-G3 页面迁完后已无消费者，grep 全仓只剩本注）：不再保留转发，避免"两处取值"。
    static constexpr int form_col_gap = spacing_m; // R2 列距 12（formkit 仍消费）
};

// ---------------------------------------------------------------------------
// Tokens：§4.1 表全量双主题值（备注给出规范行/对照 CSS 变量名）
// ---------------------------------------------------------------------------
struct Tokens {
    ThemeMode mode = ThemeMode::Dark;
    bool dark() const { return mode == ThemeMode::Dark; }

    // window（§4.1 win）：亮 = 渐变 #e9ecf2→#e3e5ea→#e7efe9（中停 45%）；暗 = 纯色 #22252b
    QColor window;                 // 亮 #e3e5ea（= 45% 停）/ 暗 #22252b
    QColor window_grad0;           // 0% 停
    QColor window_grad1;           // 45% 停
    QColor window_grad2;           // 100% 停
    double window_mid_stop = 0.45; // 中停百分比（tokens.css body 45%；暗色纯色时三停同值）
    // card（§4.1 card / card-bd）
    QColor card;
    QColor card_bd;
    QColor card_hi; // 卡内高亮底（tokens.css --card-hi；§4.1 表外，随对照值）
    // text 三级（§4.1 txt / txt2 / txt3）
    QColor text;
    QColor text2;
    QColor text3;
    // accent（§4.1 acc / acc-dim / acc-bd / acc-on）；accent = 系统强调色优先（无则 mockup 兜底）
    QColor accent;
    QColor accent_dim;
    QColor accent_bd;
    QColor on_accent; // 强调底上的文字（v6：亮 #ffffff / 暗 #0c1b24）
    // M4 遗留字段（R-214 复核后**保留**）：v6 主按钮字 = acc-on，QSS 已切 on_accent；本字段
    // 继续以旧口径给值（两个主题都 #0c1b24）。消费点 = theme 自检探针（mainwindow.cpp 的
    // tokens 取证行逐字钉住两主题取值）——"零读回"的自述已随复核更正，字段不是死值。
    QColor go_text;
    // 语义四色（§4.1 ok/warn/err + 末行「同色 8–10% 底 + 30–34% 描边 + 同色字」）
    QColor ok;
    QColor ok_dim;
    QColor ok_bd;
    QColor warn;
    QColor warn_dim;
    QColor warn_bd;
    QColor err;
    QColor err_dim;
    QColor err_bd;
    // control（§4.1 ctl / ctl-bd）
    QColor control;
    QColor control_bd;
    QColor control_solid; // control 合成到窗口底（QPalette::Base/Button 用，不透明）
    // 骨架辅助（§4.1 表外既有字段，随对照值）
    QColor line; // 顶栏下沿/状态栏上沿 hairline（v6 = --card-bd 口径）
    // R-114（M7-WP4）：M4 遗留字段 `bottom_bg` 已删 —— v6 复核结论是状态栏**无独立底色**
    // （tokens.css .statusbar 只有上沿 --card-bd 分隔线，底 = 窗口底；QSS 已改透明），
    // "保留供既有调用点读回"自述经 grep 复核为**零读回**（src/ tests/ 全仓无消费点）。
    // §6.2 选中段数字圆底：tokens.css `.nav-strip button.on .n{background:rgba(255,255,255,.25);
    // border-color:transparent}`（双主题同值，画在 acc 填充之上；§4.1 表外，随对照值）
    QColor nav_num_sel_bg;
    QColor stage_bg;    // §4.1 画布底 #16181d（不随主题变）
    QColor close_hover; // §3.1 关闭悬停 #c42b1c（双主题同值）
    // §3.1「关闭悬停 #c42b1c（白字）」的字色（M5-T3 加性扩展：caption 字形已矢量化，悬停换色
    // 由控件显式落地（QIcon 取色不吃 QSS 的 :hover color）→ 字色必须有令牌可读；tokens.css
    // `.cap.close:hover{color:#fff}` 逐字，双主题同值）
    QColor close_hover_fg;
    // 开关（R4 30×16 药丸）的关态轨/滑钮（M5-T2 加性扩展，登记见报告）：**§4.1 表外**，
    // 取 mockup `.sw{background:#c3c6cf}` / `.sw::after{background:#fff}`（双主题同值）；
    // 开态轨 = accent、开态钮 = on_accent（§4.1 令牌）。
    QColor switch_rail_off;
    QColor switch_knob_off;
    // 运行期锁定（G5）的图标钮禁用态：= .icon-btn 按 opacity .4 合成后的等效不透明色（QSS 不能设
    // 控件不透明度；等价做法是预合成 —— 见 Metrics::icon_dim_opacity 注释）。M5-T4：v6 图标钮是
    // **透明底**（悬停显底）→ 禁用态只用 icon_disabled_fg（图标淡化）；bg/bd 两字段保留不删
    // （既有读回口径；不再进 QSS）。
    QColor icon_disabled_bg;
    QColor icon_disabled_bd;
    QColor icon_disabled_fg;
    // 舞台框边（v6 舞台底 #16181d 固定深色 → 框边为固定浅色）
    QColor stage_bd;
    // 进度条四态（C8 语义不变：实心=已写盘 / 斜纹=合成 / 绿=完成 / 琥珀=异常 / 红=失败）：
    // 轨 = control，边框 = control_bd；实心 = accent，完成 = ok，失败 = err；
    // 合成（斜纹）= 双色带 progress_fill_from→to，角度/带宽见 Metrics（115° / 6px）
    QColor progress_fill_from; // #3aa3dc（.bar i.synth 第一带）
    QColor progress_fill_to;   // #4cc2ff（.bar i.synth 第二带）
    // 改动值 .mod（R4/§4.1：强调边框 + 强调色字；font-weight 700 见 mod_qss）
    QColor mod_bd;   // = accent_bd
    QColor mod_text; // = accent
    // 半径（§4.3；QSS 也从这里取值，不写死字面量）
    int radius_card = Metrics::card_radius;
    int radius_control = Metrics::control_radius;
    int radius_stage = Metrics::stage_radius;
    int radius_pill = Metrics::pill_radius;
    // 卡片阴影（§4.1 shadow：亮色 0 1px 4px rgba(16,24,40,.07)；暗色无投影）
    QColor shadow_color;
    int shadow_blur = 0;
    int shadow_dy = 0;
    // 开始运行钮投影（.go box-shadow 0 2px 10px；深 rgba(76,194,255,.25) / 浅 rgba(0,103,192,.20)）
    QColor go_shadow_color;
    int go_shadow_blur = 0;
    int go_shadow_dy = 0;
};

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
inline qreal px_to_pt(qreal px) { return px * 72.0 / 96.0; }

// QSS 颜色字面量：不透明 → #rrggbb；带 alpha → rgba(r,g,b,p%)（Qt QSS 的 alpha 取百分比）
inline QString css_color(const QColor &c) {
    if (c.alpha() == 255)
        return c.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1,%2,%3,%4%)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(static_cast<int>(std::lround(c.alphaF() * 100.0)));
}

// 把半透明前景 over 合成到不透明背景 under 上（QPalette 不接受半透明色）
inline QColor composite(const QColor &over, const QColor &under) {
    const qreal a = over.alphaF();
    return QColor(static_cast<int>(std::lround(over.red() * a + under.red() * (1.0 - a))),
                  static_cast<int>(std::lround(over.green() * a + under.green() * (1.0 - a))),
                  static_cast<int>(std::lround(over.blue() * a + under.blue() * (1.0 - a))));
}

// 以不透明度 alpha 把前景 over 合成到背景 under 上（CSS `opacity` 对**整钮**的等效静态色）。
// 用途：运行期锁定的图标钮（mockup .icon-btn.dim{opacity:.4}）—— QSS 无法表达控件级不透明度，
// 故把"底色/边框/字形"三者分别按同一 alpha 预合成到窗口底上，视觉等价（不含被禁用的交互反馈）。
inline QColor at_opacity(const QColor &color, const QColor &under, double alpha) {
    QColor scaled = color;
    scaled.setAlphaF(std::clamp(alpha, 0.0, 1.0) * color.alphaF());
    return composite(scaled, under);
}

// accent = 系统强调色。判据 = **调色板的 Highlight 是否来自平台样式**：
//   * Qt 自带样式（Fusion / Windows / …）的 Highlight 是自家硬编码蓝（#0078d7 / 非激活
//     #308cc6），与系统强调色无关；
//   * 平台镜像样式（Windows 11 / windowsvista / 各桌面平台主题）才把系统强调色喂进调色板。
// 用**样式名**而不是颜色值做判据 —— 颜色判据会把"用户恰好把强调色设成 #0078d7"的机器
// 误判为"没拿到系统色"。进程内一次性解析并缓存（必须先于主题 QPalette 安装求值，
// 否则会读回自己装进去的值）。
inline bool style_mirrors_system_accent() {
    if (qApp == nullptr)
        return false;
    const QStyle *style = qApp->style();
    if (style == nullptr)
        return false;
    const QString name = style->objectName().toLower();
    static const char *const kQtOwnStyles[] = {"fusion", "windows",   "motif",
                                               "cde",    "plastique", "cleanlooks"};
    for (const char *own : kQtOwnStyles) {
        if (name == QLatin1String(own))
            return false;
    }
    return !name.isEmpty();
}

inline const QColor *system_accent_cached() {
    static const QColor cached = [] {
        if (!style_mirrors_system_accent())
            return QColor();
        const QColor highlight = QGuiApplication::palette().color(QPalette::Highlight);
        if (!highlight.isValid() || highlight.alpha() == 0)
            return QColor();
        return highlight;
    }();
    return cached.isValid() ? &cached : nullptr;
}

// accent：系统强调色优先；无 → §4.1 对照值（深 #4cc2ff / 浅 #0067c0）
inline QColor accent_color(ThemeMode mode) {
    if (const QColor *sys = system_accent_cached())
        return *sys;
    return mode == ThemeMode::Dark ? QColor(0x4c, 0xc2, 0xff) : QColor(0x00, 0x67, 0xc0);
}

// 是否拿到了系统强调色（自检据此判定 accent 应等于系统色还是 mockup 兜底）
inline bool system_accent_available() { return system_accent_cached() != nullptr; }

// ---------------------------------------------------------------------------
// 值工厂：唯一给值点。全部字面量 = §4.1 颜色表逐字（与 docs/mockups-v2/tokens.css 对照）。
// ---------------------------------------------------------------------------
inline Tokens tokens(ThemeMode mode) {
    const auto rgba = [](int r, int g, int b, double a) {
        return QColor(r, g, b, static_cast<int>(std::lround(a * 255.0)));
    };
    Tokens t;
    t.mode = mode;
    if (mode == ThemeMode::Dark) {
        // win = #22252b（纯色；tokens.css body.dark --win）
        t.window = QColor(0x22, 0x25, 0x2b);
        t.window_grad0 = QColor(0x22, 0x25, 0x2b);
        t.window_grad1 = QColor(0x22, 0x25, 0x2b);
        t.window_grad2 = QColor(0x22, 0x25, 0x2b);
        t.card = rgba(255, 255, 255, 0.05);      // --card
        t.card_bd = rgba(255, 255, 255, 0.10);   // --card-bd
        t.card_hi = rgba(255, 255, 255, 0.07);   // --card-hi
        t.text = QColor(0xec, 0xec, 0xf0);       // --txt
        t.text2 = QColor(0xa9, 0xaa, 0xb4);      // --txt2
        t.text3 = QColor(0x7a, 0x7b, 0x86);      // --txt3
        t.accent_dim = rgba(76, 194, 255, 0.10); // --acc-dim（§4.1 10%）
        t.accent_bd = rgba(76, 194, 255, 0.34);  // --acc-bd（§4.1 34%）
        t.on_accent = QColor(0x0c, 0x1b, 0x24);  // --acc-on
        t.go_text = QColor(0x0c, 0x1b, 0x24);    // M4 遗留字段（见 Tokens::go_text 注释）
        t.ok = QColor(0x4c, 0xc4, 0x7c);
        t.ok_dim = rgba(76, 196, 124, 0.10);
        t.ok_bd = rgba(76, 196, 124, 0.32);
        t.warn = QColor(0xe0, 0xa9, 0x4a);
        t.warn_dim = rgba(224, 169, 74, 0.10);
        t.warn_bd = rgba(224, 169, 74, 0.32);
        t.err = QColor(0xff, 0x7a, 0x8a);
        t.err_dim = rgba(255, 122, 138, 0.10);
        t.err_bd = rgba(255, 122, 138, 0.32);
        t.control = rgba(255, 255, 255, 0.07);           // --ctl
        t.control_bd = rgba(255, 255, 255, 0.12);        // --ctl-bd
        t.line = t.card_bd;                              // 顶栏/状态栏 hairline = --card-bd
        t.progress_fill_from = QColor(0x3a, 0xa3, 0xdc); // 斜纹带 1
        t.progress_fill_to = QColor(0x4c, 0xc2, 0xff);   // 斜纹带 2
        t.shadow_color = QColor(0, 0, 0, 0);
        t.shadow_blur = 0; // §4.1 暗色无投影
        t.shadow_dy = 0;
        t.go_shadow_color = rgba(76, 194, 255, 0.25); // .go box-shadow（§4.1 表外，保留）
        t.go_shadow_blur = 10;
        t.go_shadow_dy = 2;
    } else {
        // win = linear-gradient(150deg,#e9ecf2 0%,#e3e5ea 45%,#e7efe9 100%)（tokens.css :root
        // --win）
        t.window = QColor(0xe3, 0xe5, 0xea); // = 45% 停
        t.window_grad0 = QColor(0xe9, 0xec, 0xf2);
        t.window_grad1 = QColor(0xe3, 0xe5, 0xea);
        t.window_grad2 = QColor(0xe7, 0xef, 0xe9);
        t.card = QColor(0xff, 0xff, 0xff);      // --card
        t.card_bd = rgba(0, 0, 0, 0.09);        // --card-bd（§4.1 .09）
        t.card_hi = QColor(0xf7, 0xf8, 0xfa);   // --card-hi
        t.text = QColor(0x1b, 0x1c, 0x20);      // --txt
        t.text2 = QColor(0x5d, 0x5f, 0x68);     // --txt2
        t.text3 = QColor(0x8b, 0x8d, 0x96);     // --txt3
        t.accent_dim = rgba(0, 103, 192, 0.08); // --acc-dim（§4.1 8%）
        t.accent_bd = rgba(0, 103, 192, 0.32);  // --acc-bd（§4.1 32%）
        t.on_accent = QColor(0xff, 0xff, 0xff); // --acc-on
        t.go_text = QColor(0x0c, 0x1b, 0x24);   // M4 遗留字段（见 Tokens::go_text 注释）
        t.ok = QColor(0x1d, 0x7a, 0x3e);
        t.ok_dim = rgba(29, 122, 62, 0.08);
        t.ok_bd = rgba(29, 122, 62, 0.30);
        t.warn = QColor(0x9a, 0x6a, 0x00);
        t.warn_dim = rgba(154, 106, 0, 0.08);
        t.warn_bd = rgba(154, 106, 0, 0.30);
        t.err = QColor(0xc4, 0x2b, 0x3a);
        t.err_dim = rgba(196, 43, 58, 0.08);
        t.err_bd = rgba(196, 43, 58, 0.30);
        t.control = QColor(0xee, 0xf0, 0xf4); // --ctl
        t.control_bd = rgba(0, 0, 0, 0.12);   // --ctl-bd
        t.line = t.card_bd;                   // 顶栏/状态栏 hairline = --card-bd
        // 斜纹双色带（C8 语义不变；run 页原型双主题同值）
        t.progress_fill_from = QColor(0x3a, 0xa3, 0xdc);
        t.progress_fill_to = QColor(0x4c, 0xc2, 0xff);
        t.shadow_color = rgba(16, 24, 40, 0.07); // §4.1 0 1px 4px rgba(16,24,40,.07)
        t.shadow_blur = 4;
        t.shadow_dy = 1;
        t.go_shadow_color = rgba(0, 103, 192, 0.20); // .go box-shadow（§4.1 表外，保留）
        t.go_shadow_blur = 10;
        t.go_shadow_dy = 2;
    }
    t.accent = accent_color(mode);
    t.mod_bd = t.accent_bd;                      // R4 .mod = 强调边框
    t.mod_text = t.accent;                       // R4 .mod = 强调色字
    t.stage_bg = QColor(0x16, 0x18, 0x1d);       // §4.1 画布底（主题无关）
    t.stage_bd = rgba(255, 255, 255, 0.08);      // 舞台框边（固定深色舞台上的浅描边）
    t.close_hover = QColor(0xc4, 0x2b, 0x1c);    // §3.1 关闭悬停（双主题同值）
    t.close_hover_fg = QColor(0xff, 0xff, 0xff); // §3.1 关闭悬停白字（M5-T3 加性扩展）
    // §6.2 选中段数字圆底 = rgba(255,255,255,.25)（tokens.css 逐字，双主题同值）
    t.nav_num_sel_bg = rgba(255, 255, 255, 0.25);
    t.switch_rail_off = QColor(0xc3, 0xc6, 0xcf); // mockup .sw（关态轨，双主题同值）
    t.switch_knob_off = QColor(0xff, 0xff, 0xff); // mockup .sw::after（关态钮，双主题同值）
    t.control_solid = composite(t.control, t.window);
    // 锁定态图标钮（.icon-btn.dim{opacity:.4} 的静态等效）：三色都按 .4 合成到窗口底
    t.icon_disabled_bg =
        at_opacity(t.control, t.window, Metrics::icon_dim_opacity); // 底（--ctl 本身带透明）
    t.icon_disabled_bd = at_opacity(composite(t.control_bd, t.window), t.window,
                                    Metrics::icon_dim_opacity); // 边（--ctl-bd 合成成实体色再淡出）
    t.icon_disabled_fg = at_opacity(t.text2, t.window, Metrics::icon_dim_opacity);
    return t;
}

// 明暗跟随系统（§9）；Unknown → 深色（原型基准 meta-dark）
inline ThemeMode system_theme_mode() {
    if (qApp == nullptr)
        return ThemeMode::Dark;
    switch (QGuiApplication::styleHints()->colorScheme()) {
    case Qt::ColorScheme::Light:
        return ThemeMode::Light;
    case Qt::ColorScheme::Dark:
        return ThemeMode::Dark;
    default:
        return ThemeMode::Dark;
    }
}

// ---------------------------------------------------------------------------
// 持久化主题档（A28/T11）：AppSettings.ui_theme_mode 的**进程级覆盖**（"system"|"light"|"dark"，
// 空 = 未注入 = 跟随系统）。启动期由 MainWindow 从设置注入一次（mainwindow.cpp 的
// apply_startup_theme）；设置弹窗在用户选档时就地更新 → 同一进程内后续所有
// preferred_theme_mode() 调用点（图标/自绘件初值/弹窗）取到**同一档**，不再是"各处各读一套"。
// 存 QByteArray 而非 ThemeMode：`system` 无法用 ThemeMode 表达（"未强制" ≠ "浅色"）。
// ---------------------------------------------------------------------------
inline QByteArray &persisted_theme_mode_ref() {
    static QByteArray mode;
    return mode;
}

// 词表外/空值 → 清掉覆盖（等价"跟随系统"）
inline void set_persisted_theme_mode(const QByteArray &mode) {
    const QByteArray value = mode.trimmed().toLower();
    const bool forced = value == QByteArrayLiteral("light") || value == QByteArrayLiteral("dark");
    persisted_theme_mode_ref() = forced ? value : QByteArray();
}

inline QByteArray persisted_theme_mode() { return persisted_theme_mode_ref(); }

// 当前生效档的**原始档名**（"system"/"light"/"dark"）：设置弹窗的三档回填与探针取值都走它
// （与 preferred_theme_mode() 同一判据顺序，只是保留"未强制"这一档）。
inline QByteArray theme_mode_id() {
    const QByteArray forced = qgetenv("PP_UI_THEME").trimmed().toLower();
    if (forced == QByteArrayLiteral("dark") || forced == QByteArrayLiteral("light"))
        return forced;
    return persisted_theme_mode_ref(); // 空 = 跟随系统
}

// 启动/走查主题（**单源**：GUI 与 main.cpp 的启动后处理都走这一条）：PP_UI_THEME=dark|light
// 强制；未设置 → 持久化档（A28；设置·通用 三档的跨进程落地）；再未设置 → 跟随系统。
// 强制档供双主题截图走查用（系统同一时刻只能是一种模式），也用于启动期 DWM 沉浸式深色标题栏
// 的对齐（M4-W2-fix 第 11 条：窗口边框不再按系统色滞后）——故强制档优先级最高。
inline ThemeMode preferred_theme_mode() {
    const QByteArray forced = qgetenv("PP_UI_THEME").trimmed().toLower();
    if (forced == QByteArrayLiteral("dark"))
        return ThemeMode::Dark;
    if (forced == QByteArrayLiteral("light"))
        return ThemeMode::Light;
    const QByteArray &persisted = persisted_theme_mode_ref();
    if (persisted == QByteArrayLiteral("dark"))
        return ThemeMode::Dark;
    if (persisted == QByteArrayLiteral("light"))
        return ThemeMode::Light;
    return system_theme_mode();
}

// ---------------------------------------------------------------------------
// 生成函数
// ---------------------------------------------------------------------------
inline QString window_gradient_qss(const Tokens &t) {
    // §4.1 win 的 Qt 落地：CSS linear-gradient(150deg,…) 只支持盒坐标两点式，
    // (0.25,0)→(0.75,1) 对应 150°（CSS 角 = 从正上起顺时针）。亮色三停、暗色三停同值（纯色）。
    return QStringLiteral(
               "qlineargradient(x1:0.25, y1:0, x2:0.75, y2:1, stop:0 %1, stop:%2 %3, stop:1 %4)")
        .arg(css_color(t.window_grad0))
        .arg(t.window_mid_stop, 0, 'g', 3)
        .arg(css_color(t.window_grad1), css_color(t.window_grad2));
}

inline QFont font(double px, int weight = QFont::Normal, bool tabular = false) {
    QFont f(QString::fromLatin1(Typography::family));
    f.setFamilies({QString::fromLatin1(Typography::family), QStringLiteral("Segoe UI"),
                   QString::fromLatin1(Typography::family_ui), QStringLiteral("Noto Sans SC")});
    f.setPointSizeF(px_to_pt(px));
    f.setWeight(QFont::Weight(weight));
    if (tabular)
        f.setFeature(QFont::Tag("tnum"), 1); // §4.2 数值 tabular-nums
    return f;
}

inline QPalette palette(const Tokens &t) {
    QPalette p;
    p.setColor(QPalette::Window, t.window);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.control_solid); // 输入类控件底（--ctl 合成）
    p.setColor(QPalette::AlternateBase, t.card_hi);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::Button, t.control_solid);
    p.setColor(QPalette::ButtonText, t.text);
    p.setColor(QPalette::BrightText, t.err);
    p.setColor(QPalette::Highlight, t.accent);
    p.setColor(QPalette::HighlightedText, t.on_accent);
    p.setColor(QPalette::ToolTipBase, t.card_hi);
    p.setColor(QPalette::ToolTipText, t.text);
    p.setColor(QPalette::PlaceholderText, t.text3);
    p.setColor(QPalette::Mid, t.control_bd);
    p.setColor(QPalette::Midlight, t.card);
    p.setColor(QPalette::Dark, t.line);
    p.setColor(QPalette::Link, t.accent);
    p.setColor(QPalette::LinkVisited, t.accent);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, t.text3);
    return p;
}

// 改动值 .mod（R4「强调边框+强调色字」，另带 700 字重）：
// 动态属性钩子 + QSS 属性选择器 —— 消费入口 = theme::set_mod(widget, changed)。
inline constexpr const char *kModProperty = "ppMod";
// 徽章 pill 的动态属性钩子（QLabel[ppPill="…"]）：色调取字符串值，见下面的 kPill* 常量。
// 历史值 true（bool）= accent 档（M4 口径），保持可渲染（既有调用点零改动）。
inline constexpr const char *kPillProperty = "ppPill";
// 底栏状态点的警醒态动态属性钩子（M5-T3 加性扩展）：true → err 色圆点（§9.1「未就绪不用红，
// 警醒用 err」）；色值单源在本文件的 QSS 里，调用方只翻转属性 + repolish。
// M5-T4：被**四档状态色**取代（§8「状态四档：中性=空/无事、绿=就绪/完成、琥珀=警告、红=失败；
// 状态点与状态词同色」—— 两档表达不了"中性/琥珀"，且空态不得用红）。属性保留不删（既有读回），
// 新调用点一律走 kStatusToneProperty。
inline constexpr const char *kDotAlertProperty = "ppDotAlert";
// 状态栏"状态点 + 状态词"的四档色调动态属性钩子（M5-T4，§8）：取值 = 下面的 kTone* 键；
// 点与词同色（同一条属性挂到 QLabel#pp-status-dot 与 QLabel#pp-status），色值单源在 QSS 里。
inline constexpr const char *kStatusToneProperty = "ppStatusTone";
inline constexpr const char *kToneNeutral = "n"; // 中性：空/无事发生（--txt3 点 / --txt2 词）
inline constexpr const char *kToneOk = "ok";     // 绿：就绪/完成
inline constexpr const char *kToneWarn = "warn"; // 琥珀：警告（例外/0 字节/预检未过）
inline constexpr const char *kToneErr = "err";   // 红：失败（只给失败；就绪/空不得用红）
// §4.1 末行：语义色胶囊 = 同色 8–10% 透明底 + 30–34% 描边 + 同色字；n = 中性（ctl 底/txt2 字）
inline constexpr const char *kPillNeutral = "n";
inline constexpr const char *kPillAccent = "acc";
inline constexpr const char *kPillOk = "ok";
inline constexpr const char *kPillWarn = "warn";
inline constexpr const char *kPillErr = "err";

// M5-T2（**加性扩展**，登记见报告）：formkit（C2）的行内辅助件与数值控件的动态属性钩子。
// 行构造器产出的标签轨/kv 值/单位槽/范围注释与 spinbox 只挂 `ppFk`，"表单栅格几何"
// 仍归 src/ui/formkit_layout.h（T2 契约），此处只负责**着色与盒度量**（颜色取自 §4 令牌）。
// 既有页面无人设该属性 ⇒ 主题 QSS 增量对现网控件零影响。
//
// UX-6⑤ / R-054（M7-WP4）：三条钩子轨**收敛到 ppFk** —— 页内的 `ppMetaField`（page_meta）
// 与 `ppField`（page_output / page_run / dialogkit）两条迁移期轨道退役，全部 setProperty 写
// 本属性、QSS 选择器只认本属性（页面本地 kFieldProperty 常量改指 theme::kFkProperty，旧常量
// 依 R-054 删除）。取值词表并入下面的 kFk* 家族（跨页同形词共用一条规则 = field_qss）。
inline constexpr const char *kFkProperty = "ppFk";
inline constexpr const char *kFkLabel = "label"; // 标签轨（§4.2 标签 11px / --txt2）
inline constexpr const char *kFkValue = "value"; // kv 值（--txt；右对齐 tabular 由 C++ 落地）
inline constexpr const char *kFkUnit = "unit";   // 16px 单位槽（§4.2 辅助 10px / --txt3）
inline constexpr const char *kFkNote = "note";   // 10px 范围注释（§4.2 辅助 10px / --txt3）
// R-215（M7-WP4）：旧 kFkCheck（check 行文字）已删 —— 勾选行文字由 formkit::CheckBox 自绘
// （C++ 侧取 --txt），该钩子与对应 QSS 规则全仓零 setProperty/零消费（死钩子 + 死规则）。
inline constexpr const char *kFkSpin = "spin"; // 数值控件（R4 spinbox 盒）
// 下拉（R4 combo 盒）。GUI-R2：下拉的样式钩子由本条**唯一**承载 —— 在此之前六个建造点各自
// 挂 `ppField`/`ppMetaField`/不挂属性，导致同一控件四种高度、亮色下整只不绘制
// （docs/gui-review-0.3.0-r2.md §1）。
inline constexpr const char *kFkCombo = "combo"; // 下拉（R4 combo 盒）
// ---- UX-6⑤ 并入的跨页字段词表（field_qss 的取值；页面 setProperty 一律引用这些常量）----
inline constexpr const char *kFkInput = "input"; // 文本输入（mockup .tbx：--ctl 底 + --ctl-bd 边）
inline constexpr const char *kFkView = "view";   // 只读生效值框（同 input 形态）
inline constexpr const char *kFkRule = "rule";   // 规则输入框（同 input 形态）
inline constexpr const char *kFkButton =
    "button"; // 次按钮（原 ppField="button" / ppMetaField="btn"）
inline constexpr const char *kFkPrimary = "primary"; // 主按钮（实心 acc）
inline constexpr const char *kFkLink = "link";       // 文字链（accent，无框）
inline constexpr const char *kFkLinkDanger =
    "link-danger";                             // 破坏性文字链（err，无框；旧 "danger" 描边档退役）
inline constexpr const char *kFkFold = "fold"; // 折叠行钮（chevron = 矢量图标）
inline constexpr const char *kFkHint = "hint"; // 说明/提示（--txt3）
inline constexpr const char *kFkSub = "sub";   // 次级说明（--txt3，同 hint）

// 自绘控件（ItemDelegate / 自绘 pill）的三色取法：同一张 §4.1 末行规则，避免各处复写色值。
struct PillPaint {
    QColor bg;
    QColor bd;
    QColor fg;
};

inline PillPaint pill_paint(const Tokens &t, const char *tone) {
    const QLatin1String name(tone);
    if (name == QLatin1String(kPillOk))
        return {t.ok_dim, t.ok_bd, t.ok};
    if (name == QLatin1String(kPillWarn))
        return {t.warn_dim, t.warn_bd, t.warn};
    if (name == QLatin1String(kPillErr))
        return {t.err_dim, t.err_bd, t.err};
    if (name == QLatin1String(kPillNeutral))
        return {t.control, t.control_bd, t.text2};
    return {t.accent_dim, t.accent_bd, t.accent}; // acc / true / 其它
}

// 徽章 pill 的单条 QSS 规则（R4 高 16 r3；竖内距 0 → 盒高 = 字高 + 2px 边）
inline QString pill_rule(const char *tone, const PillPaint &p) {
    return QStringLiteral("QLabel[%1=\"%2\"] { color: %3; background: %4; border: 1px solid %5; "
                          "border-radius: %6px; padding: %7px %8px; }\n")
        .arg(QString::fromLatin1(kPillProperty), QString::fromLatin1(tone), css_color(p.fg),
             css_color(p.bg), css_color(p.bd))
        .arg(Metrics::pill_radius)
        .arg(Metrics::pill_pad_y)
        .arg(Metrics::pill_pad_x);
}

// 徽章 pill 全色调规则：true（M4 历史值）与 acc 同档；n/ok/warn/err 四档语义色
inline QString pill_qss(const Tokens &t) {
    return pill_rule("true", pill_paint(t, kPillAccent)) +
           pill_rule(kPillAccent, pill_paint(t, kPillAccent)) +
           pill_rule(kPillNeutral, pill_paint(t, kPillNeutral)) +
           pill_rule(kPillOk, pill_paint(t, kPillOk)) +
           pill_rule(kPillWarn, pill_paint(t, kPillWarn)) +
           pill_rule(kPillErr, pill_paint(t, kPillErr));
}

// QSS 重算：Qt 只在 polish 时求值属性选择器，动态属性变更后必须 unpolish+polish 才会换样式。
inline void repolish(QWidget *widget) {
    if (widget == nullptr || widget->style() == nullptr)
        return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

// 改动值 .mod 的落地入口（T12 起消费）：setProperty(ppMod) + 强制 QSS 重算。
// changed=true → 强调色边框 + 强调色字（+ 700 字重，见 mod_qss）；false → 复原。
inline void set_mod(QWidget *widget, bool changed) {
    if (widget == nullptr)
        return;
    widget->setProperty(kModProperty, changed);
    repolish(widget);
}

inline QString mod_qss(const Tokens &t) {
    const QString prop = QString::fromLatin1(kModProperty);
    return QStringLiteral("*[%1=\"true\"] { border: %2px solid %3; color: %4; }\n")
               .arg(prop)
               .arg(Metrics::mod_border_px)
               .arg(css_color(t.mod_bd), css_color(t.mod_text)) +
           QStringLiteral("QLabel[%1=\"true\"], QSpinBox[%1=\"true\"], QLineEdit[%1=\"true\"], "
                          "QComboBox[%1=\"true\"] { font-weight: %2; }\n")
               .arg(prop)
               .arg(Metrics::mod_weight);
}

// ---------------------------------------------------------------------------
// 覆盖层（overlay）令牌组（UX-6⑦，M7-WP4）：压在**照片 / 地图**上的浮层配色。
// **不随主题变**（两主题同值）：底下的像素可能是任意亮度（照片 / 卫星影像 / 路网），固定
// 深浅配色是明暗两主题下都可读的唯一解 —— 属合法的固定色，但必须**单源 + 注明**：
// 原先散在 preview_panel.cpp（kOverlay*/kZoom* 等 15 个字面量）与 page_meta.cpp（地图 HUD
// rgba(255,255,255,190)/#404040 等）的写死色全部收编到本组，调用点只引用不写字面量。
// ---------------------------------------------------------------------------
struct OverlayTokens {
    // 预览舞台浮层（深色半透明压在照片上）
    const char *pv_fg = "#dfe6ec";            // .pv-badge 字色
    const char *pv_bg = "rgba(10,12,16,62%)"; // .pv-badge 底
    const char *pv_bd = "rgba(255,255,255,12%)";
    const char *zoombar_bg = "rgba(18,20,26,85%)";        // .zoombar 底
    const char *zoombar_bd = "rgba(255,255,255,13%)";     // .zoombar 描边
    const char *zoombar_fg = "#c9ccd4";                   // .zoombar button 字
    const char *zoom_level_fg = "#ececf0";                // .zoombar 读数
    const char *zoom_hover_bg = "rgba(255,255,255,12%)";  // .zoombar button:hover
    const char *zoom_active_bg = "rgba(255,255,255,18%)"; // 模式钮当前档
    const char *zoom_sep_bg = "rgba(255,255,255,15%)";    // .zoombar .sep
    const char *stage_hint_fg = "rgba(236,236,240,50%)";  // .canvas .hint
    const char *empty_icon_fg = "rgba(223,230,236,60%)";  // 空态图标
    const char *empty_text_fg = "rgba(236,236,240,78%)";  // 空态引导语
    const char *photo_mat_bg = "#0d0f12";                 // 照片衬垫（比舞台底深一档）
    // QColor 侧（QColor 不解析 CSS 的 rgba() 字面量）
    QColor photo_mat_bd = QColor(255, 255, 255, 26); // = rgba(255,255,255,10%)
    QColor empty_icon = QColor(223, 230, 236, 153);  // = rgba(223,230,236,60%)
    // 地图 HUD / 缩放钮（白底浅字压在地图上）
    const char *hud_bg = "rgba(255,255,255,190)";
    const char *hud_bg_hover = "rgba(255,255,255,230)";
    const char *hud_bg_dim = "rgba(255,255,255,150)";
    QColor hud_fg = QColor(0x40, 0x40, 0x40);     // #404040
    QColor hud_fg_dim = QColor(0x9a, 0x9a, 0x9a); // 禁用图标
};
inline const OverlayTokens &overlay_tokens() {
    static const OverlayTokens k;
    return k;
}

// 预览缩放条"当前模式档"的动态属性钩子（ppZoomActive）：**不是 checked**（修 1-19：缩放
// 模式/比例切换由普通按钮承担，无勾选语义）；QSS 只按属性值换底色。单源在此
// （preview_panel.cpp 原自持常量已并入，UX-6⑦）。
inline constexpr const char *kZoomActiveProperty = "ppZoomActive";

// ---------------------------------------------------------------------------
// 字段类控件共享规则（UX-6④⑥，M7-WP4）：三条钩子轨并入 ppFk 后，**跨页同形**的输入框 /
// 按钮 / 文字链 / 折叠钮 / 提示行规则只在本函数维护一份（原 page_meta / page_output /
// page_run / dialogkit 四份拼装全部退役）。页专规则见 page_*_qss。
// ---------------------------------------------------------------------------
inline QString field_qss(const Tokens &t) {
    const QString prop = QString::fromLatin1(kFkProperty);
    const auto sel = [&prop](const char *value) {
        return QStringLiteral("[%1=\"%2\"]").arg(prop, QString::fromLatin1(value));
    };
    QString qss;
    // 输入类控件（mockup .tbx：--ctl 底 + --ctl-bd 边 + r3 + --txt 字；R4 高 24 = 内容盒 22
    // + 上下各 1px 边 —— 盒高在此单源夹定，页面/弹窗都不再各自写 min/max-height）
    const QString input_sel = QStringLiteral("QLineEdit") + sel(kFkInput) +
                              QStringLiteral(", QLineEdit") + sel(kFkView) +
                              QStringLiteral(", QLineEdit") + sel(kFkRule);
    qss += input_sel + QStringLiteral(" { background: ") + css_color(t.control) +
           QStringLiteral("; border: 1px solid ") + css_color(t.control_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
           QStringLiteral("px; color: ") + css_color(t.text) +
           QStringLiteral("; padding: 0 8px; min-height: ") +
           QString::number(Metrics::input_h - 2) + QStringLiteral("px; max-height: ") +
           QString::number(Metrics::input_h - 2) + QStringLiteral("px; }\n");
    qss += input_sel + QStringLiteral(":disabled { color: ") + css_color(t.text3) +
           QStringLiteral("; }\n");
    // 次按钮（R4：高 26 + 描边 + --txt2 字；悬停提亮到 --txt）。R-186：横向内距单源 =
    // Metrics::btn_pad_x（原 11/12 两值漂移归一）；**高度不在 QSS 夹**（盒高单源 =
    // formkit::make_button 的 setFixedHeight(btn_h)，R-213 的 spin/combo 盒模型矛盾同口径处置）。
    qss += QStringLiteral("QPushButton") + sel(kFkButton) + QStringLiteral(" { background: ") +
           css_color(t.control) + QStringLiteral("; border: 1px solid ") + css_color(t.control_bd) +
           QStringLiteral("; border-radius: ") + QString::number(Metrics::btn_radius) +
           QStringLiteral("px; color: ") + css_color(t.text2) +
           QStringLiteral("; font-weight: 600; padding: 0 ") + QString::number(Metrics::btn_pad_x) +
           QStringLiteral("px; }\n");
    qss += QStringLiteral("QPushButton") + sel(kFkButton) + QStringLiteral(":hover { color: ") +
           css_color(t.text) + QStringLiteral("; }\n");
    qss += QStringLiteral("QPushButton") + sel(kFkButton) +
           QStringLiteral(":disabled { background: ") + css_color(t.control) +
           QStringLiteral("; border-color: ") + css_color(t.control_bd) +
           QStringLiteral("; color: ") + css_color(t.text3) + QStringLiteral("; }\n");
    // 主按钮（R4：实心 acc 底 + acc-on 字；禁用 = 灰底灰字）
    qss += QStringLiteral("QPushButton") + sel(kFkPrimary) + QStringLiteral(" { background: ") +
           css_color(t.accent) + QStringLiteral("; border: 1px solid ") + css_color(t.accent) +
           QStringLiteral("; border-radius: ") + QString::number(Metrics::btn_radius) +
           QStringLiteral("px; color: ") + css_color(t.on_accent) +
           QStringLiteral("; font-weight: 600; padding: 0 ") + QString::number(Metrics::btn_pad_x) +
           QStringLiteral("px; }\n");
    qss += QStringLiteral("QPushButton") + sel(kFkPrimary) +
           QStringLiteral(":disabled { background: ") + css_color(t.control) +
           QStringLiteral("; border-color: ") + css_color(t.control_bd) +
           QStringLiteral("; color: ") + css_color(t.text3) + QStringLiteral("; }\n");
    // 文字链（无框；accent 档与破坏性 err 档同形只换色 —— A39 ②「红只表达破坏性动作」）
    const QString link_body = QStringLiteral(" { border: none; background: transparent; color: ");
    qss += QStringLiteral("QToolButton") + sel(kFkLink) + QStringLiteral(", QPushButton") +
           sel(kFkLink) + link_body + css_color(t.accent) +
           QStringLiteral("; font-weight: 600; padding: 0 2px; }\n");
    qss += QStringLiteral("QToolButton") + sel(kFkLink) + QStringLiteral(":disabled, QPushButton") +
           sel(kFkLink) + QStringLiteral(":disabled { color: ") + css_color(t.text3) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QPushButton") + sel(kFkLinkDanger) + link_body + css_color(t.err) +
           QStringLiteral("; font-weight: 600; padding: 0 2px; }\n");
    qss += QStringLiteral("QPushButton") + sel(kFkLinkDanger) + QStringLiteral(":checked { ") +
           QStringLiteral("background: ") + css_color(t.err_dim) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
           QStringLiteral("px; }\n");
    qss += QStringLiteral("QPushButton") + sel(kFkLinkDanger) +
           QStringLiteral(":disabled { color: ") + css_color(t.text3) + QStringLiteral("; }\n");
    // 折叠行（chevron = 矢量图标 —— C3 禁字形）
    qss += QStringLiteral("QToolButton") + sel(kFkFold) +
           QStringLiteral(" { border: none; "
                          "background: transparent; color: ") +
           css_color(t.text2) + QStringLiteral("; font-weight: 600; }\n");
    qss += QStringLiteral("QToolButton") + sel(kFkFold) + QStringLiteral(":disabled { color: ") +
           css_color(t.text3) + QStringLiteral("; }\n");
    // 说明/提示行（mockup .sub-hint）+ 非法值红字（ppInvalid 是独立语义属性，两条轨都认）
    qss += QStringLiteral("QLabel") + sel(kFkHint) + QStringLiteral(", QLabel") + sel(kFkSub) +
           QStringLiteral(" { color: ") + css_color(t.text3) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel") + sel(kFkHint) +
           QStringLiteral("[ppInvalid=\"true\"] { color: ") + css_color(t.err) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[ppInvalid=\"true\"] { color: ") + css_color(t.err) +
           QStringLiteral("; }\n");
    return qss;
}

// ---------------------------------------------------------------------------
// 滚动条（UX-11，M7-WP4）：SPA 式细浮层 —— 轨道透明、滑块 6px / r3、悬停加宽 8px 并加深；
// 两端箭头钮/页槽全退役。**两主题一套、弹窗与主窗一套**（本函数是唯一出处：style_sheet() 与
// dialogkit::dialog_qss 都引它，消除"弹窗 QSS 没有滚动条规则"的两套皮）。
// ---------------------------------------------------------------------------
inline QString scrollbar_qss(const Tokens &t) {
    // 悬停加深：--ctl-bd 是半透明描边色，悬停态把它合成到窗口底（等价"更实"一档）
    const QString handle = css_color(t.control_bd);
    const QString handle_hover = css_color(composite(t.control_bd, t.window));
    QString qss;
    for (int o = 0; o < 2; ++o) {
        const bool vert = (o == 0);
        const QString orient = vert ? QStringLiteral("vertical") : QStringLiteral("horizontal");
        const QString extent = vert ? QStringLiteral("width: ") +
                                          QString::number(Metrics::scroll_w) + QStringLiteral("px")
                                    : QStringLiteral("height: ") +
                                          QString::number(Metrics::scroll_w) + QStringLiteral("px");
        const QString extent_hover =
            vert ? QStringLiteral("width: ") + QString::number(Metrics::scroll_w_hover) +
                       QStringLiteral("px")
                 : QStringLiteral("height: ") + QString::number(Metrics::scroll_w_hover) +
                       QStringLiteral("px");
        const QString min_extent =
            vert ? QStringLiteral("min-height: ") + QString::number(Metrics::scroll_handle_min) +
                       QStringLiteral("px")
                 : QStringLiteral("min-width: ") + QString::number(Metrics::scroll_handle_min) +
                       QStringLiteral("px");
        const QString line_extent =
            vert ? QStringLiteral("height: 0px") : QStringLiteral("width: 0px");
        // 轨道透明（不占视觉）；滚动条本身只留 6px 滑块位
        qss += QStringLiteral("QScrollBar:%1 { background: transparent; %2; margin: 0; }\n")
                   .arg(orient, extent);
        qss += QStringLiteral("QScrollBar:%1:hover { %2; }\n").arg(orient, extent_hover);
        qss += QStringLiteral("QScrollBar::handle:%1 { background: ").arg(orient) + handle +
               QStringLiteral("; border-radius: ") + QString::number(Metrics::scroll_radius) +
               QStringLiteral("px; ") + min_extent + QStringLiteral("; }\n");
        qss += QStringLiteral("QScrollBar::handle:%1:hover { background: ").arg(orient) +
               handle_hover + QStringLiteral("; }\n");
        qss += QStringLiteral("QScrollBar::add-line:%1, QScrollBar::sub-line:%1 { %2; background: "
                              "none; border: none; }\n")
                   .arg(orient, line_extent);
        qss += QStringLiteral("QScrollBar::add-page:%1, QScrollBar::sub-page:%1 { background: "
                              "transparent; }\n")
                   .arg(orient);
    }
    return qss;
}

// 卡壳/卡题/hint（§4.3 r6 + §4.1 card；style_sheet 与各页自足口径共用一份）
inline QString card_qss(const Tokens &t) {
    QString qss;
    qss += QStringLiteral("QFrame#pp-card { background: ") + css_color(t.card) +
           QStringLiteral("; border: 1px solid ") + css_color(t.card_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_card) +
           QStringLiteral("px; }\n");
    qss += QStringLiteral("QLabel#pp-card-title { color: ") + css_color(t.text) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel#pp-card-hint { color: ") + css_color(t.text3) +
           QStringLiteral("; }\n");
    return qss;
}

// ---------------------------------------------------------------------------
// 页专规则（UX-6⑥，M7-WP4）：真正只属于某页的 QSS —— 按用户要求"在公共样式文件里开一个新的
// 样式"，全部落在本文件（theme::xxx_qss），页内不再自己拼 QSS 字符串；每段注明**为何不能公共**
// （只锚本页 objectName / 本页语义属性，公共化会误伤它页）。跨页同形规则已上提 field_qss。
// ---------------------------------------------------------------------------

// 元数据页专（生效对照 .tline / 搜索结果 / 地图 HUD / 分类 chip）：选择器只锚 pp-meta-* 与
// 本页语义值（old/new/arrow/…），公共化会把"原值删除线"误加到别页同名词上 → 不能公共。
inline QString page_meta_qss(const Tokens &t) {
    const QString prop = QString::fromLatin1(kFkProperty);
    const auto sel = [&prop](const char *value) {
        return QStringLiteral("QLabel[%1=\"%2\"]").arg(prop, QString::fromLatin1(value));
    };
    const OverlayTokens &ov = overlay_tokens();
    QString qss;
    // 生效对照（mockup .tline）：原 = 删除线 + --txt2；新 = 强调色（.mod 时再叠强调边框）；
    // 箭头/注释/来源 = --txt3；多选小表名 = --txt
    qss += sel("old") + QStringLiteral(" { color: ") + css_color(t.text2) + QStringLiteral("; }\n");
    qss +=
        sel("new") + QStringLiteral(" { color: ") + css_color(t.accent) + QStringLiteral("; }\n");
    qss += sel("arrow") + QStringLiteral(", ") + sel("note") + QStringLiteral(", ") +
           sel("multi-more") + QStringLiteral(" { color: ") + css_color(t.text3) +
           QStringLiteral("; }\n");
    qss += sel("multi-name") + QStringLiteral(" { color: ") + css_color(t.text) +
           QStringLiteral("; }\n");
    // 空态一行（§8：中性灰说明；绝不警示色）
    qss +=
        sel("empty") + QStringLiteral(" { color: ") + css_color(t.text3) + QStringLiteral("; }\n");
    // M6-T8 搜索区：结果列表 / 联想下拉（--ctl 底 + --ctl-bd 边 + r3；选中 = --acc-dim 底，
    // 悬停 = --card-hi 底 —— 与卡片/控件同一套令牌）
    const QString list_rule = QStringLiteral("QListWidget#pp-meta-gps-search-results, "
                                             "QListWidget#pp-meta-gps-suggest");
    qss += list_rule + QStringLiteral(" { background: ") + css_color(t.control) +
           QStringLiteral("; border: 1px solid ") + css_color(t.control_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
           QStringLiteral("px; color: ") + css_color(t.text) +
           QStringLiteral("; outline: none; }\n");
    qss += list_rule + QStringLiteral("::item { padding: 2px 4px; border-radius: ") +
           QString::number(t.radius_control) + QStringLiteral("px; }\n");
    qss += list_rule + QStringLiteral("::item:hover { background: ") + css_color(t.card_hi) +
           QStringLiteral("; }\n");
    qss += list_rule + QStringLiteral("::item:selected { background: ") + css_color(t.accent_dim) +
           QStringLiteral("; color: ") + css_color(t.text) + QStringLiteral("; }\n");
    // 结果行内文案（名称 = --txt；地址/距离 = --txt3）+ 地址回显行（--txt2）
    qss += sel("result-name") + QStringLiteral(" { color: ") + css_color(t.text) +
           QStringLiteral("; }\n");
    qss += sel("result-addr") + QStringLiteral(", ") + sel("result-dist") +
           QStringLiteral(" { color: ") + css_color(t.text3) + QStringLiteral("; }\n");
    qss += sel("address") + QStringLiteral(" { color: ") + css_color(t.text2) +
           QStringLiteral("; }\n");
    // M6-T12 地图覆盖层（§5.3）：光标读数 chip + 「－/＋」缩放钮 —— **定位色固定**（overlay
    // 令牌组：白底 α190 + 深灰字/图标，与 mapwidget 的版权行/图钉气泡同口径）：两者都压在地图
    // 上（可能是影像/卫星/路网，明暗主题都可能落在其上），固定白底是两主题下都可读的唯一解。
    qss += QStringLiteral("QLabel[") + prop + QStringLiteral("=\"map-hud\"] { background: ") +
           QString::fromLatin1(ov.hud_bg) + QStringLiteral("; border-radius: ") +
           QString::number(t.radius_control) + QStringLiteral("px; color: ") +
           css_color(ov.hud_fg) + QStringLiteral("; padding: 1px 5px; }\n");
    qss += QStringLiteral("QToolButton[") + prop + QStringLiteral("=\"map-zoom\"] { background: ") +
           QString::fromLatin1(ov.hud_bg) + QStringLiteral("; border: 1px solid ") +
           css_color(t.card_bd) + QStringLiteral("; border-radius: ") +
           QString::number(t.radius_control) + QStringLiteral("px; }\n");
    qss += QStringLiteral("QToolButton[") + prop +
           QStringLiteral("=\"map-zoom\"]:hover { background: ") +
           QString::fromLatin1(ov.hud_bg_hover) + QStringLiteral("; }\n");
    qss += QStringLiteral("QToolButton[") + prop +
           QStringLiteral("=\"map-zoom\"]:disabled { background: ") +
           QString::fromLatin1(ov.hud_bg_dim) + QStringLiteral("; }\n");
    // 分类 chip（与 formkit::ChipButton 同色同源：未选 = 透明底 + --ctl-bd；悬停 = --ctl 底；
    // 选中 = --acc-dim 底 + --acc-bd 边 + --txt 字）
    qss += QStringLiteral("QPushButton[") + prop +
           QStringLiteral("=\"chip\"] { background: "
                          "transparent; border: 1px solid ") +
           css_color(t.control_bd) + QStringLiteral("; border-radius: ") +
           QString::number(t.radius_control) + QStringLiteral("px; color: ") + css_color(t.text2) +
           QStringLiteral("; padding: 0 9px; }\n");
    qss += QStringLiteral("QPushButton[") + prop +
           QStringLiteral("=\"chip\"]:hover { background: ") + css_color(t.control) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QPushButton[") + prop +
           QStringLiteral("=\"chip\"]:checked { background: ") + css_color(t.accent_dim) +
           QStringLiteral("; border-color: ") + css_color(t.accent_bd) +
           QStringLiteral("; color: ") + css_color(t.text) + QStringLiteral("; }\n");
    qss += QStringLiteral("QPushButton[") + prop + QStringLiteral("=\"chip\"]:disabled { color: ") +
           css_color(t.text3) + QStringLiteral("; border-color: ") + css_color(t.control_bd) +
           QStringLiteral("; }\n");
    // 真警告（GPS 坐标无效）：A6 —— 原硬编码 0xcc8800 收口为 warn 令牌
    qss += QStringLiteral("QLabel#gps_warning { color: ") + css_color(t.warn) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QWidget#pp-meta-content { background: transparent; }\n");
    return qss;
}

// 输出页专（格式页签 / 「＋ 添加格式…」）：QTabBar 规则只服务本页页签（弹窗页签在 dialogkit
// 另有 kTabUnderlinePx 档）→ 不能公共。
inline QString page_output_qss(const Tokens &t) {
    QString qss;
    // 格式页签（mockup .dlg-tab：11.5/600 + 选中 = --txt 字 + 2px acc 下划线；页签行有分隔基线
    // —— 修 2-21）。GUI-R2：QTabBar / QTabWidget 本体**必须显式透明** —— 否则各自吃 palette 的
    // --ctl 底，在页签行画出两条 29px 灰带（设计稿这一行是白底 + 底部发丝线，没有任何色带）。
    qss += QStringLiteral("QTabWidget#pp-format-tabs, QTabBar#pp-format-tabs { background: "
                          "transparent; border: none; }\n");
    qss += QStringLiteral("QTabBar { background: transparent; border: none; }\n");
    qss +=
        QStringLiteral("QTabWidget#pp-format-tabs::pane { border: none; border-top: 1px solid ") +
        css_color(t.line) + QStringLiteral("; }\n");
    qss += QStringLiteral("QTabBar::tab { background: transparent; color: ") + css_color(t.text2) +
           QStringLiteral("; padding: 5px 12px 6px; margin-right: 2px; border: none; "
                          "border-bottom: 2px solid transparent; font-weight: 600; }\n");
    qss += QStringLiteral("QTabBar::tab:first { margin-left: ") +
           QString::number(Metrics::card_pad_x) + QStringLiteral("px; }\n");
    qss += QStringLiteral("QTabBar::tab:selected { color: ") + css_color(t.text) +
           QStringLiteral("; border-bottom-color: ") + css_color(t.accent) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QTabBar::tab:hover { color: ") + css_color(t.text) +
           QStringLiteral("; }\n");
    // 「＋ 添加格式…」（修 2-21：可用态 = 文字链式 accent，不再灰字像禁用）
    qss += QStringLiteral("QToolButton#pp-add-format { border: none; background: transparent; "
                          "color: ") +
           css_color(t.accent) +
           QStringLiteral("; padding: 0 10px; font-weight: 600; border-radius: ") +
           QString::number(Metrics::btn_radius) + QStringLiteral("px; }\n");
    qss += QStringLiteral("QToolButton#pp-add-format:hover { background: ") +
           css_color(t.accent_dim) + QStringLiteral("; }\n");
    qss += QStringLiteral("QToolButton#pp-add-format:disabled { color: ") + css_color(t.text3) +
           QStringLiteral("; background: transparent; }\n");
    // 容器透明（窗口底渐变透出）
    qss += QStringLiteral("QWidget#pp-output-content { background: transparent; }\n");
    qss += QStringLiteral("QScrollArea#pp-output-scroll { background: transparent; }\n");
    qss += QStringLiteral("QScrollArea#pp-output-scroll > QWidget > QWidget { background: "
                          "transparent; }\n");
    return qss;
}

// 运行页专（ppRun 语义色阶 / 表头 / 日志尾框）：ppRun 是本页的行语义属性（表头/组名/输出名/
// 原因/摘要五档），公共化会与别页 QLabel 撞值 → 不能公共。
inline QString page_run_qss(const Tokens &t) {
    const QString run = QStringLiteral("ppRun");
    const auto run_rule = [&run](const char *value, const QColor &color) {
        return QStringLiteral("QLabel[") + run + QStringLiteral("=\"") +
               QString::fromLatin1(value) + QStringLiteral("\"] { color: ") + css_color(color) +
               QStringLiteral("; }\n");
    };
    QString qss;
    // 表头/名称/原因/摘要（静态档：随页重建换色，无需 C++ 侧逐值写色）
    qss += run_rule("thead", t.text3);
    qss += run_rule("gname", t.text);
    qss += run_rule("oname", t.text2);
    qss += run_rule("reason", t.err);
    qss += run_rule("summary", t.text2);
    // 表头下沿（表头是 QFrame；骨架 QSS 只管 pp-card）
    qss += QStringLiteral("QFrame#pp-run-thead { border-bottom: 1px solid ") +
           css_color(t.card_bd) + QStringLiteral("; background: transparent; }\n");
    // 日志尾框（等宽内容由控件字体承担；框 = 卡内高亮底 + 描边 + 画布圆角档）
    qss += QStringLiteral("QFrame#pp-run-log-box { background: ") + css_color(t.card_hi) +
           QStringLiteral("; border: 1px solid ") + css_color(t.card_bd) +
           QStringLiteral("; border-radius: ") + QString::number(Metrics::stage_radius) +
           QStringLiteral("px; }\n");
    // 容器透明（窗口底渐变透出）
    qss += QStringLiteral("QWidget#pp-run-content { background: transparent; }\n");
    qss += QStringLiteral("QScrollArea#pp-run-scroll { background: transparent; }\n");
    qss += QStringLiteral("QScrollArea#pp-run-scroll > QWidget > QWidget { background: "
                          "transparent; }\n");
    return qss;
}

// 预览面板专（舞台浮层：徽标 / 缩放条 / 空态 / 翻图钮 / 热键条）：全部压在照片上的浮层，
// 配色走 overlay 令牌组（不随主题变）→ 与主题化控件规则混在 style_sheet 里会互相污染 → 不能公共。
inline QString preview_panel_qss(const Tokens &t) {
    const OverlayTokens &ov = overlay_tokens();
    QString qss;
    qss += QStringLiteral("QLabel#pp-preview-badge { color: %1; background: %2; border: 1px solid "
                          "%3; border-radius: %4px; padding: %5px %6px; }\n")
               .arg(QString::fromLatin1(ov.pv_fg), QString::fromLatin1(ov.pv_bg),
                    QString::fromLatin1(ov.pv_bd))
               .arg(Metrics::overlay_radius)
               .arg(Metrics::overlay_pad_y)
               .arg(Metrics::overlay_pad_x);
    // 缩放控件组（§8「悬浮右下单一控件组」；v2 .zoombar）：深色半透明底 + 1px 描边 + r4；
    // 各钮 28×28 透明底、悬停显底、当前模式档由 ppZoomActive 高亮（无勾选语义，修 1-19）
    qss += QStringLiteral("QWidget#pp-preview-zoombar { background: %1; border: 1px solid %2; "
                          "border-radius: %3px; }\n")
               .arg(QString::fromLatin1(ov.zoombar_bg), QString::fromLatin1(ov.zoombar_bd))
               .arg(Metrics::zoombar_radius);
    qss += QStringLiteral("QToolButton#pp-preview-zoom-out, QToolButton#pp-preview-zoom-in, "
                          "QToolButton#pp-preview-zoom-fit, QToolButton#pp-preview-zoom-1to1 { "
                          "background: transparent; border: none; border-radius: %1px; }\n")
               .arg(Metrics::icon_btn_radius);
    qss += QStringLiteral(
               "QToolButton#pp-preview-zoom-out:hover, QToolButton#pp-preview-zoom-in:hover, "
               "QToolButton#pp-preview-zoom-fit:hover, "
               "QToolButton#pp-preview-zoom-1to1:hover { background: %1; }\n")
               .arg(QString::fromLatin1(ov.zoom_hover_bg));
    qss +=
        QStringLiteral("QToolButton#pp-preview-zoom-fit[%1=\"true\"], "
                       "QToolButton#pp-preview-zoom-1to1[%1=\"true\"] { background: %2; }\n")
            .arg(QString::fromLatin1(kZoomActiveProperty), QString::fromLatin1(ov.zoom_active_bg));
    // A39 ④：模式钮 = 文字形态 → 字色取 .zoombar button（与 −/＋ 图标同色），左右内距 = v2
    // .zoombar 的 `padding:0 6px`（钮宽已按文本 + 内距定死，此处只补色/内距）。
    qss += QStringLiteral("QToolButton#pp-preview-zoom-fit, QToolButton#pp-preview-zoom-1to1 { "
                          "color: %1; padding: 0 %2px; }\n")
               .arg(QString::fromLatin1(ov.zoombar_fg))
               .arg(Metrics::zoom_text_btn_pad_x);
    qss += QStringLiteral("QFrame#pp-preview-zoom-sep { background: %1; border: none; }\n")
               .arg(QString::fromLatin1(ov.zoom_sep_bg));
    qss += QStringLiteral("QLabel#pp-preview-zoom-level { color: %1; }\n")
               .arg(QString::fromLatin1(ov.zoom_level_fg));
    // 左下操作提示（§8 逐字）与空态（§8：图标 + 引导语；修 1-16）
    qss += QStringLiteral("QLabel#pp-preview-hint { color: %1; }\n")
               .arg(QString::fromLatin1(ov.stage_hint_fg));
    qss += QStringLiteral("QLabel#pp-preview-empty-text { color: %1; }\n")
               .arg(QString::fromLatin1(ov.empty_text_fg));
    // 翻图钮 28×28（修 1-21）：R4 图标钮样式（悬停显底；禁用 = 灰图标，与打标 chip 同档禁用）
    qss += QStringLiteral("QToolButton#pp-preview-prev, QToolButton#pp-preview-next { color: %1; "
                          "background: %2; border: 1px solid %3; border-radius: %4px; }\n"
                          "QToolButton#pp-preview-prev:hover, QToolButton#pp-preview-next:hover { "
                          "background: %5; color: %6; }\n")
               .arg(css_color(t.text2), css_color(t.control), css_color(t.control_bd))
               .arg(Metrics::icon_btn_radius)
               .arg(css_color(t.control), css_color(t.text));
    qss += QStringLiteral("QToolButton#pp-preview-prev:disabled, "
                          "QToolButton#pp-preview-next:disabled { color: %1; }\n")
               .arg(css_color(t.text3));
    qss += QStringLiteral("QLabel#pp-preview-pos, QLabel#pp-preview-name, "
                          "QLabel#pp-preview-keyhint-label { color: %1; }\n")
               .arg(css_color(t.text3));
    // A17（T11）：打标 chip 的形态 = formkit::ChipButton 自绘（键帽 13×13 r2 + 文字；底色/描边/
    // 禁用态都在 formkit 内取 tokens）→ 这里不再有 chip 的 QSS 双实现；只为**分隔线**与
    // 「清除」文字链补两条规则（修 1-20：清除是破坏性动作，与打标 chip 用分隔线隔开）。
    qss += QStringLiteral("QFrame#pp-preview-key-sep { background: %1; border: none; }\n")
               .arg(css_color(t.control_bd));
    qss += QStringLiteral("QToolButton#pp-preview-clear { color: %1; background: transparent; "
                          "border: none; padding: 0 %2px; }\n"
                          "QToolButton#pp-preview-clear:hover { color: %3; }\n"
                          "QToolButton#pp-preview-clear:disabled { color: %4; }\n")
               .arg(css_color(t.text3))
               .arg(Metrics::zoom_text_btn_pad_x)
               .arg(css_color(t.text2), css_color(t.text3));
    qss += QStringLiteral("QLabel#pp-preview-keyhint-label:disabled { color: %1; }\n")
               .arg(css_color(t.text3));
    return qss;
}

// ---------------------------------------------------------------------------
// 令牌重放（I6 / R-064，M7-WP4）：自绘控件持有令牌副本，主题切换必须逐个 set_tokens ——
// 此前 paramform / page_output / settings_dialog 三处各写一份遍历且**都漏了 formkit 的
// Combo/Spin/DSpin 箭头**（R-064：主题切换后箭头残留旧主题色）。本处单点扫树重放：
//   * 实现 theme::Tokenized 的自绘控件（formkit 全族）逐个 set_tokens；
//   * 挂 kFk*/kPill*/kMod*/kStatusTone 动态属性的控件 repolish（属性选择器重新求值）。
// 调用方（页面/弹窗/paramform）只调本函数一次，不再自持遍历。
// ---------------------------------------------------------------------------
struct Tokenized {
    virtual ~Tokenized() = default;
    virtual void set_tokens(const Tokens &t) = 0;
};

inline void replay_tokens(QWidget *root, const Tokens &t) {
    if (root == nullptr)
        return;
    const QList<QWidget *> widgets = root->findChildren<QWidget *>();
    for (QWidget *w : widgets) {
        if (auto *tz = dynamic_cast<Tokenized *>(w))
            tz->set_tokens(t);
    }
    // 属性钩子控件：QSS 属性选择器在 unpolish/polish 后重新求值（窗口样式表变更时 Qt 会
    // 自动重算；页面自设样式表的场景由本处兜底）
    static const char *const kHookProps[] = {kFkProperty, kPillProperty, kModProperty,
                                             kStatusToneProperty};
    for (QWidget *w : widgets) {
        bool hooked = false;
        for (const char *prop : kHookProps) {
            if (w->property(prop).isValid()) {
                hooked = true;
                break;
            }
        }
        if (hooked)
            repolish(w);
    }
    if (auto *tz = dynamic_cast<Tokenized *>(root))
        tz->set_tokens(t);
}

// formkit（C2）辅助件与数值控件的 QSS（M5-T2 加性扩展，登记见报告）。
//   * 标签轨 / kv 值 / 单位槽 / 范围注释 / 勾选文字：只给**颜色**（字号与对齐由 C++ 侧
//     theme::font(...) + setAlignment 落地 —— QSS 的 font-size 只吃整数，§4.2 的 11.5/9.5/10.5
//     会失真）；
//   * spinbox（R4）：盒高 24（QSS 的 min/max-height 指内容盒 → 24 − 2×1px 边 = 22）、
//     箭头列 17px（▲▼ 各 12）；**值区右对齐 + tabular 由 C++ 落地**（Qt QSS 无 text-align，
//     也无 font-feature-settings 的 tnum 开关）。
//   * 选择器一律以 `ppFk` 动态属性为锚（既有页面无人设该属性 → 对现网控件零影响）。
inline QString formkit_qss(const Tokens &t) {
    const QString prop = QString::fromLatin1(kFkProperty);
    const auto label_rule = [&prop](const char *hook, const QColor &color) {
        return QStringLiteral("QLabel[%1=\"%2\"] { color: %3; }\n")
            .arg(prop)
            .arg(QString::fromLatin1(hook), css_color(color));
    };
    QString qss;
    qss += label_rule(kFkLabel, t.text2);
    qss += label_rule(kFkValue, t.text);
    qss += label_rule(kFkUnit, t.text3);
    qss += label_rule(kFkNote, t.text3);
    const QString spin =
        QStringLiteral("QSpinBox[%1=\"%2\"]").arg(prop).arg(QString::fromLatin1(kFkSpin));
    const QString dspin =
        QStringLiteral("QDoubleSpinBox[%1=\"%2\"]").arg(prop).arg(QString::fromLatin1(kFkSpin));
    qss += spin + QStringLiteral(", ") + dspin + QStringLiteral(" { background: ") +
           css_color(t.control) + QStringLiteral("; border: 1px solid ") + css_color(t.control_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
           // QSS 的 min/max-height 是**内容盒**高 → 24 − 上下各 1px 边 = 22（总高 24）
           QStringLiteral("px; color: ") + css_color(t.text) + QStringLiteral("; min-height: ") +
           QString::number(Metrics::spin_h - 2) + QStringLiteral("px; max-height: ") +
           QString::number(Metrics::spin_h - 2) + QStringLiteral("px; padding: 0 ") +
           QString::number(Metrics::spin_pad_x) + QStringLiteral("px; }\n");
    // 箭头列 17px（上下各 12）—— GUI-R2：`::up-arrow/::down-arrow` 置空、`::*-button` 只留几何
    // 且不画底/边 —— 平台样式不再画经典的**独立描边按钮格**（竖分隔 + 横分隔 + 加粗箭头）。
    // 箭头本体与列内两条发丝线由 formkit::Spin/DSpin 自绘（icons 矢量，C3 禁字形），
    // 与下拉（formkit::Combo）同一套做法。
    const QString up_sel =
        spin + QStringLiteral("::up-button, ") + dspin + QStringLiteral("::up-button");
    const QString down_sel =
        spin + QStringLiteral("::down-button, ") + dspin + QStringLiteral("::down-button");
    qss += up_sel +
           QStringLiteral(" { subcontrol-origin: border; subcontrol-position: top right; "
                          "width: ") +
           QString::number(Metrics::spin_arrow_w) + QStringLiteral("px; height: ") +
           QString::number(Metrics::spin_arrow_h) +
           QStringLiteral("px; border: none; background: transparent; }\n");
    qss += down_sel +
           QStringLiteral(" { subcontrol-origin: border; subcontrol-position: bottom right; "
                          "width: ") +
           QString::number(Metrics::spin_arrow_w) + QStringLiteral("px; height: ") +
           QString::number(Metrics::spin_arrow_h) +
           QStringLiteral("px; border: none; background: transparent; }\n");
    qss += spin + QStringLiteral("::up-arrow, ") + dspin +
           QStringLiteral("::up-arrow { image: none; width: 0px; height: 0px; }\n");
    qss += spin + QStringLiteral("::down-arrow, ") + dspin +
           QStringLiteral("::down-arrow { image: none; width: 0px; height: 0px; }\n");
    qss += spin + QStringLiteral(":disabled, ") + dspin + QStringLiteral(":disabled { color: ") +
           css_color(t.text3) + QStringLiteral("; }\n");
    // 下拉（R4 combo 盒 24 / r3 / --ctl 底 + --ctl-bd 边；箭头 8px 居右、**无分格按钮**）。
    // GUI-R2：`::drop-down` 只留箭头位几何、`::down-arrow` 置空 —— 平台样式不再画经典分格
    // 按钮；箭头本体由 formkit::Combo 自绘（icons::ChevronDown 矢量，C3 禁字形）。
    // 高度**不在此处夹**：盒高单源 = formkit::combo_box 的 setFixedHeight(combo_h)（QSS 的
    // min/max-height 是另一套盒模型口径，会把总高压成 22 → 同一控件两种高）。
    const QString combo =
        QStringLiteral("QComboBox[%1=\"%2\"]").arg(prop).arg(QString::fromLatin1(kFkCombo));
    qss += combo + QStringLiteral(" { background: ") + css_color(t.control) +
           QStringLiteral("; border: 1px solid ") + css_color(t.control_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
           QStringLiteral("px; color: ") + css_color(t.text) +
           // R-270：盒内距令牌化（combo_pad_x）；值区右内距 = 箭头位宽 combo_arrow_slot ——
           // 原「padding-right: 26px 硬编码 vs 箭头位 24 计算值」两值漂移随本行归一（同源取值）。
           QStringLiteral("; padding: 0 ") + QString::number(Metrics::combo_pad_x) +
           QStringLiteral("px; padding-right: ") + QString::number(Metrics::combo_arrow_slot) +
           QStringLiteral("px; }\n");
    qss += combo + QStringLiteral(":disabled { color: ") + css_color(t.text3) +
           QStringLiteral("; }\n");
    // 箭头位（= combo_arrow_slot = combo_arrow_px×2 + 8）：只给几何，不画分隔线/按钮底
    qss += combo + QStringLiteral("::drop-down { border: none; background: transparent; width: ") +
           QString::number(Metrics::combo_arrow_slot) + QStringLiteral("px; }\n");
    qss += combo + QStringLiteral("::down-arrow { image: none; width: 0px; height: 0px; }\n");
    // 下拉**弹层**（GUI-R2 举一反三：此前零规则 = 平台默认弹出列表，与本体两套皮）
    qss += combo + QStringLiteral(" QAbstractItemView { background: ") + css_color(t.card) +
           QStringLiteral("; border: 1px solid ") + css_color(t.card_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
           QStringLiteral("px; color: ") + css_color(t.text) +
           QStringLiteral("; selection-background-color: ") + css_color(t.accent_dim) +
           QStringLiteral("; selection-color: ") + css_color(t.text) +
           QStringLiteral("; padding: 2px; outline: none; }\n");
    qss +=
        combo + QStringLiteral(" QAbstractItemView::item { min-height: 24px; padding: 0 8px; }\n");
    return qss;
}

// 骨架 QSS：只作用于 pp-* 钩子与 ppMod/ppPill 动态属性，避免误伤既有页面控件
inline QString style_sheet(const Tokens &t) {
    const QString card = css_color(t.card), card_bd = css_color(t.card_bd);
    const QString radius_card = QString::number(t.radius_card);
    const QString radius_control = QString::number(t.radius_control);
    QString qss;
    // 窗口底：§4.1 win（亮 = 三停渐变；暗 = 纯色 #22252b）
    qss += QStringLiteral("QWidget#pp-root { background: ") + window_gradient_qss(t) +
           QStringLiteral("; }\n");
    // 顶栏（兼标题栏）：透明 → 露出窗口底；下沿 1px 分隔线（= --card-bd 口径）
    qss += QStringLiteral("QWidget#pp-titlebar { background: transparent; border-bottom: 1px "
                          "solid ") +
           css_color(t.line) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel#pp-app-title { color: ") + css_color(t.text) +
           QStringLiteral("; }\n");
    // 版本药丸（.ver：9.5px 字 + 99 口径胶囊；QSS 用半高等效半径）
    qss += QStringLiteral("QLabel#pp-app-ver { color: ") + css_color(t.text3) +
           QStringLiteral("; border: 1px solid ") + card_bd + QStringLiteral("; border-radius: ") +
           QString::number(Metrics::ver_radius_qss) + QStringLiteral("px; padding: ") +
           QString::number(Metrics::ver_pad_y) + QStringLiteral("px ") +
           QString::number(Metrics::ver_pad_x) + QStringLiteral("px; }\n");
    // caption 三钮（§3.1 46×32 命中区 + mockup .cap 满高视觉面；关闭悬停 #c42b1c）
    qss += QStringLiteral("QPushButton#pp-cap-min, QPushButton#pp-cap-max, "
                          "QPushButton#pp-cap-close { background: transparent; border: none; "
                          "color: ") +
           css_color(t.text2) + QStringLiteral("; }\n");
    qss += QStringLiteral("QPushButton#pp-cap-min:hover, QPushButton#pp-cap-max:hover { "
                          "background: ") +
           css_color(t.control) + QStringLiteral("; }\n");
    qss += QStringLiteral("QPushButton#pp-cap-close:hover { background: ") +
           css_color(t.close_hover) + QStringLiteral("; color: ") + css_color(t.close_hover_fg) +
           QStringLiteral("; }\n");
    // 预设 / 设置图标钮（R4 图标钮 28×28 r4「悬停显底」）—— v6 换锚（M5-T4）：常驻底/描边作废，
    // 透明底 + 悬停灰（.icon-btn{background:transparent;border:0} / .icon-btn:hover{--hov}）
    qss += QStringLiteral("QToolButton#pp-presets, QToolButton#pp-settings { background: "
                          "transparent; border: none; border-radius: ") +
           QString::number(Metrics::icon_btn_radius) + QStringLiteral("px; color: ") +
           css_color(t.text2) + QStringLiteral("; }\n");
    qss += QStringLiteral("QToolButton#pp-presets:hover, QToolButton#pp-settings:hover { "
                          "background: ") +
           css_color(t.control) + QStringLiteral("; color: ") + css_color(t.text) +
           QStringLiteral("; }\n");
    // 运行期锁定（G5 + 设计 §9「设置/预设置灰」）：原型 .icon-btn.dim{opacity:.4} 整钮变暗 ——
    // 透明底上等价于"图标按 .4 预合成"（token.icon_disabled_fg，见 tokens()），不再画底/边。
    // 顺序：写在 :hover 之后（同优先级下后者胜），保证禁用态不被 hover 提亮。
    qss += QStringLiteral("QToolButton#pp-presets:disabled, QToolButton#pp-settings:disabled { "
                          "background: transparent; border: none; color: ") +
           css_color(t.icon_disabled_fg) + QStringLiteral("; }\n");
    // 卡片（§4.3 r6 + --card-bd 描边；§4.1 shadow 由 ui 侧 QGraphicsDropShadowEffect 落地）
    // UX-6⑥：卡壳/卡题/hint 三规则收口 card_qss（与各页自足口径同一份）
    qss += card_qss(t);
    // 徽章 pill（§4.1 末行 + R4 高 16 r3）：色调全表
    qss += pill_qss(t);
    // 预览舞台（§4.1 画布底 #16181d 不随主题变 + §4.3 画布 r4）
    qss += QStringLiteral("QFrame#pp-preview-stage { background: ") + css_color(t.stage_bg) +
           QStringLiteral("; border: 1px solid ") + css_color(t.stage_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_stage) +
           QStringLiteral("px; }\n");
    // 状态栏（§3.1 高 30px；§3.2/mockup .statusbar = 窗口底 + 上沿 hairline，**无独立底色**；
    // 文案 11px。修 5-13：与内容区以 hairline 分隔 + 段间分区由分隔符承担）
    qss +=
        QStringLiteral("QWidget#pp-bottombar { background: transparent; border-top: 1px solid ") +
        css_color(t.line) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel#pp-status { color: ") + css_color(t.text2) +
           QStringLiteral("; }\n"); // 兜底（未设色调属性时 = 中性档词色）
    qss += QStringLiteral("QLabel#pp-output-status { color: ") + css_color(t.text3) +
           QStringLiteral("; }\n");
    // 段间分隔 hairline（修 5-13「状态栏加分隔与分区」；分隔色 = --txt3 的对照 weight 由 1px
    // 细线承担）
    qss += QStringLiteral("QFrame#pp-status-sep { background: ") + css_color(t.line) +
           QStringLiteral("; border: none; }\n");
    // A19（T11，§3.2 骨架行「● 已选 12 / 24 · 就绪｜输出：…｜剩余磁盘…」）：底栏第三段
    // 「剩余磁盘」= 次级读数（--txt3，与输出摘要同 weight），段前同样以 hairline 分区。
    qss += QStringLiteral("QLabel#pp-disk-status { color: ") + css_color(t.text3) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QFrame#pp-disk-sep { background: ") + css_color(t.line) +
           QStringLiteral("; border: none; }\n");
    // 状态四档（§8）：状态点与状态词**同色**；中性 = 空/无事（点 --txt3 / 词 --txt2，绝不用红，
    // 修 5-13/K3）。色值单源在本文件的 QSS 里 —— 调用方只设 kStatusToneProperty + repolish。
    {
        const struct {
            const char *tone;
            QColor word; // 状态词色
            QColor dot;  // 状态点色
        } tones[] = {
            {kToneNeutral, t.text2, t.text3},
            {kToneOk, t.ok, t.ok},
            {kToneWarn, t.warn, t.warn},
            {kToneErr, t.err, t.err},
        };
        const QString prop = QString::fromLatin1(kStatusToneProperty);
        for (const auto &tone : tones) {
            const QString value = QString::fromLatin1(tone.tone);
            qss += QStringLiteral("QLabel#pp-status[%1=\"%2\"] { color: %3; font-weight: 600; }\n")
                       .arg(prop, value, css_color(tone.word));
            qss +=
                QStringLiteral("QLabel#pp-status-dot[%1=\"%2\"] { background: %3; border-radius: "
                               "%4px; }\n")
                    .arg(prop, value, css_color(tone.dot))
                    .arg(Metrics::dot_px / 2);
        }
    }
    // 开始运行（R4 主 CTA：状态栏 22px 高实心 acc 底 + acc-on 字 + r4；
    // 禁用态 = --ctl 底 + --txt3 字 + 1px --ctl-bd 边；box-shadow 由 ui 侧落地）
    qss += QStringLiteral("QPushButton#pp-start { background: ") + css_color(t.accent) +
           QStringLiteral("; color: ") + css_color(t.on_accent) +
           QStringLiteral("; border: none; border-radius: ") + QString::number(Metrics::go_radius) +
           QStringLiteral("px; padding: 0 ") + QString::number(Metrics::go_pad_x) +
           QStringLiteral("px; font-weight: 700; }\n");
    qss += QStringLiteral("QPushButton#pp-start:disabled { background: ") + css_color(t.control) +
           QStringLiteral("; color: ") + css_color(t.text3) +
           QStringLiteral("; border: 1px solid ") + css_color(t.control_bd) +
           QStringLiteral("; }\n");
    // splitter 手柄 = 列间距（§4.3 卡间距 8），透明不抢视觉
    qss += QStringLiteral("QSplitter#pp-splitter::handle { background: transparent; }\n");
    // 滚动条（GUI-R2 举一反三起点；UX-11 换锚为 SPA 式细浮层）：唯一出处 = theme::scrollbar_qss
    // （轨道透明 / 滑块 6px / 悬停 8px；两主题一套、弹窗与主窗一套）。
    qss += scrollbar_qss(t);
    // GUI-R2 举一反三：平台默认皮的令牌化（此前这四类**零 QSS 规则**）
    // ① 右键/上下文菜单（分类打标、筛选、参数、异常列表 …）
    qss += QStringLiteral("QMenu { background: ") + css_color(t.card) +
           QStringLiteral("; border: 1px solid ") + css_color(t.card_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
           QStringLiteral("px; padding: 4px 2px; }\n");
    qss += QStringLiteral("QMenu::item { padding: 5px 24px 5px 12px; border-radius: ") +
           QString::number(t.radius_control) + QStringLiteral("px; color: ") + css_color(t.text) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QMenu::item:selected { background: ") + css_color(t.accent_dim) +
           QStringLiteral("; color: ") + css_color(t.text) + QStringLiteral("; }\n");
    qss += QStringLiteral("QMenu::item:disabled { color: ") + css_color(t.text3) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QMenu::separator { height: 1px; background: ") + css_color(t.line) +
           QStringLiteral("; margin: 4px 10px; }\n");
    // 勾选由代理样式画矢量（formkit::paint_menu_check）；这里只给指示器留几何位
    qss += QStringLiteral("QMenu::indicator { width: 16px; height: 16px; }\n");
    // GUI-R2：工具钮**不画下拉箭头**（设计稿 output.html 的 .ftools 里 ＋/−/筛选 都是纯图标钮，
    // 只有「按月份 ▾」那枚下拉带箭头）。＋/筛选 确有菜单，但语义由 tooltip（"添加文件 / 文件夹"、
    // "筛选…"）承担 —— 加/减/筛 三枚钮上挂箭头是实现自己长出来的。
    qss += QStringLiteral("QToolButton::menu-indicator { image: none; width: 0px; height: 0px; "
                          "subcontrol-origin: padding; subcontrol-position: bottom right; }\n");
    // ② 报错/询问框（on_start 的 6 个闸门、预设读写失败 …）—— 原生皮下是复古灰
    qss +=
        QStringLiteral("QMessageBox { background: ") + css_color(t.card) + QStringLiteral("; }\n");
    qss += QStringLiteral("QMessageBox QLabel { color: ") + css_color(t.text) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QMessageBox QPushButton { background: ") + css_color(t.control) +
           QStringLiteral("; border: 1px solid ") + css_color(t.control_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
           QStringLiteral("px; color: ") + css_color(t.text) +
           QStringLiteral("; font-weight: 600; padding: 0 14px; min-height: 24px; }\n");
    qss += QStringLiteral("QMessageBox QPushButton:hover { color: ") + css_color(t.text) +
           QStringLiteral("; }\n");
    // ③ 提示框（评审 1-49：辅助文字对比不足）
    qss += QStringLiteral("QToolTip { background: ") + css_color(t.card_hi) +
           QStringLiteral("; color: ") + css_color(t.text) +
           QStringLiteral("; border: 1px solid ") + css_color(t.card_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
           QStringLiteral("px; padding: 4px 8px; }\n");
    // ④ 分组盒（EXIF 编辑器 时间/GPS/更多字段）—— 扁平化，不再双层框线
    qss += QStringLiteral("QGroupBox { background: transparent; border: 1px solid ") +
           css_color(t.card_bd) + QStringLiteral("; border-radius: ") +
           QString::number(t.radius_card) +
           QStringLiteral("px; margin-top: 12px; padding: 8px "
                          "8px 8px 8px; color: ") +
           css_color(t.text) + QStringLiteral("; }\n");
    qss +=
        QStringLiteral("QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 6px; "
                       "color: ") +
        css_color(t.text2) + QStringLiteral("; }\n");
    qss += QStringLiteral("QGroupBox::indicator { width: 16px; height: 16px; }\n"); // 勾选同走矢量
    // formkit（C2）行内辅助件 + R4 数值控件（M5-T2 加性扩展；选择器只认 ppFk）
    qss += formkit_qss(t);
    // 字段类控件共享规则（UX-6④⑥：输入/按钮/文字链/折叠/提示，选择器只认 ppFk）
    qss += field_qss(t);
    // 改动值 .mod（动态属性钩子）
    qss += mod_qss(t);
    return qss;
}

} // namespace theme
} // namespace pp::ui
