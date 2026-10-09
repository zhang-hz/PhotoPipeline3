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
    static constexpr double nav_num_px = 9.5;
    static constexpr int nav_num_weight = 700;
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
    // 半径；v6 pill 本身 r3（盒高 16）→ 直接同一值，旧版 99→9 的钳位不再需要。
    static constexpr int pill_radius_qss = pill_radius;
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
    static constexpr int row_h = 32;   // 兼容保留（表单行 pitch 见 R1:28 / form_row_h，T2 换锚）
    // ---- §5 表单栅格（R1/R2；归 M5-T2 formkit：标签轴 80 / pitch 28 / 双列 24 —— 本任务不动）----
    static constexpr int form_label_w = 70;        // R2 标签轴 80（T2 换锚）
    static constexpr int form_col_gap = spacing_m; // R2 列距 12
    static constexpr int form_row_h = 32;          // R1 行 pitch 28（T2 换锚）
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
    // M4 遗留字段（既有对外 API 保留不删）：v6 主按钮字 = acc-on，QSS 已切 on_accent；
    // 本字段继续以旧口径给值（两个主题都 #0c1b24），供既有探针/调用点读回。
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
    // M4 遗留字段（v6 复核结论：状态栏**无独立底色** —— tokens.css .statusbar 只有上沿
    // --card-bd 分隔线，底 = 窗口底；QSS 已改透明。字段与取值保留供既有调用点读回，不删）
    QColor bottom_bg;
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
        t.bottom_bg = rgba(0, 0, 0, 0.14);               // 状态栏底（既有权重；T4 复核）
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
        t.control = QColor(0xee, 0xf0, 0xf4);   // --ctl
        t.control_bd = rgba(0, 0, 0, 0.12);     // --ctl-bd
        t.line = t.card_bd;                     // 顶栏/状态栏 hairline = --card-bd
        t.bottom_bg = rgba(255, 255, 255, 0.5); // 状态栏底（既有权重；T4 复核）
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

// 启动/走查主题（**单源**：GUI 与 main.cpp 的启动后处理都走这一条）：PP_UI_THEME=dark|light
// 强制；未设置 → 跟随系统。强制档供双主题截图走查用（系统同一时刻只能是一种模式），
// 也用于启动期 DWM 沉浸式深色标题栏的对齐（M4-W2-fix 第 11 条：窗口边框不再按系统色滞后）。
inline ThemeMode preferred_theme_mode() {
    const QByteArray forced = qgetenv("PP_UI_THEME").trimmed().toLower();
    if (forced == QByteArrayLiteral("dark"))
        return ThemeMode::Dark;
    if (forced == QByteArrayLiteral("light"))
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
// 行构造器产出的标签轨/kv 值/单位槽/范围注释/勾选文字与 spinbox 只挂 `ppFk`，"表单栅格几何"
// 仍归 src/ui/formkit_layout.h（T2 契约），此处只负责**着色与盒度量**（颜色取自 §4 令牌）。
// 既有页面无人设该属性 ⇒ 主题 QSS 增量对现网控件零影响。
inline constexpr const char *kFkProperty = "ppFk";
inline constexpr const char *kFkLabel = "label"; // 标签轨（§4.2 标签 11px / --txt2）
inline constexpr const char *kFkValue = "value"; // kv 值（--txt；右对齐 tabular 由 C++ 落地）
inline constexpr const char *kFkUnit = "unit";   // 16px 单位槽（§4.2 辅助 10px / --txt3）
inline constexpr const char *kFkNote = "note";   // 10px 范围注释（§4.2 辅助 10px / --txt3）
inline constexpr const char *kFkCheck = "check"; // check 行文字（§4.2 正文·值 / --txt）
inline constexpr const char *kFkSpin = "spin";   // 数值控件（R4 spinbox 盒）

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
    qss += label_rule(kFkCheck, t.text);
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
    // 箭头列 17px（上下各 12）—— 只给**几何**（不设底色/边），箭头本体沿用样式绘制，
    // 待 M5-T3 的矢量图标集接入后再换 ::up-arrow/::down-arrow 的 image。
    const QString up_sel =
        spin + QStringLiteral("::up-button, ") + dspin + QStringLiteral("::up-button");
    const QString down_sel =
        spin + QStringLiteral("::down-button, ") + dspin + QStringLiteral("::down-button");
    qss += up_sel +
           QStringLiteral(" { subcontrol-origin: border; subcontrol-position: top right; "
                          "width: ") +
           QString::number(Metrics::spin_arrow_w) + QStringLiteral("px; height: ") +
           QString::number(Metrics::spin_arrow_h) + QStringLiteral("px; }\n");
    qss +=
        down_sel +
        QStringLiteral(" { subcontrol-origin: border; subcontrol-position: bottom right; width: ") +
        QString::number(Metrics::spin_arrow_w) + QStringLiteral("px; height: ") +
        QString::number(Metrics::spin_arrow_h) + QStringLiteral("px; }\n");
    qss += spin + QStringLiteral(":disabled, ") + dspin + QStringLiteral(":disabled { color: ") +
           css_color(t.text3) + QStringLiteral("; }\n");
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
    qss += QStringLiteral("QFrame#pp-card { background: ") + card +
           QStringLiteral("; border: 1px solid ") + card_bd + QStringLiteral("; border-radius: ") +
           radius_card + QStringLiteral("px; }\n");
    qss += QStringLiteral("QLabel#pp-card-title { color: ") + css_color(t.text) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel#pp-card-hint { color: ") + css_color(t.text3) +
           QStringLiteral("; }\n");
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
    // formkit（C2）行内辅助件 + R4 数值控件（M5-T2 加性扩展；选择器只认 ppFk）
    qss += formkit_qss(t);
    // 改动值 .mod（动态属性钩子）
    qss += mod_qss(t);
    return qss;
}

} // namespace theme
} // namespace pp::ui
