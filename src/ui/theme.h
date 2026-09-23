// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M4-T9）— GUI 主题 tokens **单源**（设计 §9.2）
//
// 真源 = docs/mockups/*.html 的 :root CSS 变量与逐条规则（§9.4：HTML=精确规格）+ 设计 §9.2
// token 表；两者逐项一致（对照表见 M4-T9 自查留证：token 行 + 尺寸行 + 控件规约行）。
// 本文件只出**值**与少量生成函数（Tokens / QPalette / QSS / QFont / set_mod），不建控件、
// 不持状态；set_mod/repolish 只对调用方传入的控件做 setProperty + unpolish/polish。
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
// 小数 px（Qt QSS 的 font-size 只吃整数）。

#pragma once

#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QString>
#include <QStyle>
#include <QStyleHints>
#include <QWidget> // set_mod/repolish 的入参类型（只做 setProperty + unpolish/polish）
#include <Qt>
#include <QtGlobal>

#include <cmath>

#include "platform/frameless.h" // 命中区/缩放带常量单源（kResizeBorderPx 等）

namespace pp::ui {
namespace theme {

enum class ThemeMode { Light, Dark };

// ---------------------------------------------------------------------------
// 排印（§9.2）：Segoe UI Variable Text，基准 12.5px；卡题 12.5px/700；徽标 10px；数值
// tabular-nums。数值 px 取自 mockup HTML 的 font-size 声明（逐项见 T9 对照表）。
// ---------------------------------------------------------------------------
struct Typography {
    static constexpr const char *family = "Segoe UI Variable Text"; // 基准字族（§9.2 逐字）
    static constexpr const char *family_ui = "Microsoft YaHei UI";  // 中文回退（mockup 字族栈）
    static constexpr const char *family_mono = "Consolas";          // .tpl-in
    static constexpr double caption_px = 13.0;                      // .app-title / .step / .go
    static constexpr double base_px = 12.5;                         // body 基准（§9.2）
    static constexpr double card_title_px = 12.5;                   // .card-h .t
    static constexpr int card_title_weight = 700;                   // = QFont::Bold
    static constexpr double badge_px = 10.0;                        // .pill（§9.2 徽标）
    static constexpr int badge_weight = 600;                        // .pill font-weight
    static constexpr double step_badge_px = 10.5;                   // .step .n（编号徽标）
    static constexpr int step_badge_weight = 700;
    static constexpr double hint_px = 10.5;  // .hint / .ghead
    static constexpr double body_px = 11.5;  // .fname / .kv-v / .status
    static constexpr double small_px = 11.0; // .kv-k / .fld / .gl
    static constexpr double mono_px = 11.5;  // .tpl-in
};

// ---------------------------------------------------------------------------
// 尺寸基准（§9.1）+ 控件规约（§9.2）。全部取自 mockup CSS 的逐条声明。
// ---------------------------------------------------------------------------
struct Metrics {
    // ---- 骨架（§9.1 尺寸基准，1440×900 校准）----
    static constexpr int toolbar_height = 52; // .toolbar{height:52px}
    static constexpr int bottom_height = 48;  // .bottom{height:48px}
    static constexpr int main_pad_top = 10;   // .main{padding:10px 12px 8px}
    static constexpr int main_pad_x = 12;
    static constexpr int main_pad_bottom = 8;
    static constexpr int gap = 10;             // .main{gap:10px}；同时用作 splitter handle 宽
    static constexpr int left_width = 258;     // §9.1 左栏固定 / .col-left{flex:0 0 258px}
    static constexpr int left_width_max = 280; // §9.1「280 可调」
    static constexpr int min_window_w = 1180;  // §9.1 最小窗口
    static constexpr int min_window_h = 720;
    static constexpr int calibrated_w = 1440; // §9.1 1440×900 校准
    static constexpr int calibrated_h = 900;
    // 中/右栏弹性：1440 校准落点 = mockup 实测 537/601（.col-mid/.col-right flex:1 1 420/480px）；
    // 窗口缩放时 Qt 按当前尺寸等比分配 → 比例恒为 537:601≈1:1.119（探针取证）。§9.1 文本写的
    // 「1.15」与原型实测 1.111/1.119 冲突 → 按 §9.4 以 mockup 为准（记 T9 偏差），故这里的
    // stretch 常数取与实现一致的口径（100:112），不再虚标 115。
    static constexpr int mid_width_1440 = 537;
    static constexpr int right_width_1440 = 601;
    static constexpr int mid_stretch = 100;
    static constexpr int right_stretch = 112;
    static constexpr int ratio_tolerance_permille = 30; // 弹性比容差（±0.03，覆盖 Qt 取整）
    // ---- caption（§9.1：Win11 规格 46×32 命中区）----
    static constexpr int caption_btn_w = 46; // .capbtn{width:46px}
    // 双轨（M4-W2-fix 第 5 条，主对话裁定）：
    //   * **命中区**高 = 46×32 —— §9.1 明文；真源 = platform/frameless.h::
    //     kCaptionHitHeightPx（窗口侧命中几何单源），此处只转发；
    //   * **hover 高亮面**高 = 52 = 顶栏高 —— mockup .capbtns{align-self:stretch}（原型里
    //     三钮整高，关闭钮 hover 的 #c42b1c 铺满顶栏）→ 控件自身尺寸取满高，QSS :hover 画满，
    //     命中判定只在居中的 32px 带内成立（frameless 的 rect_of/build_box 落地）。
    static constexpr int caption_btn_hit_h = pp::platform::kCaptionHitHeightPx;
    static constexpr int caption_btn_h = toolbar_height; // 视觉面（hover/字形盒）
    // 命中区/缩放带宽度：**单源规则**（M4-W2-fix 第 8 条）—— 真源 = platform/frameless.h
    // （§9.1 6px；最大化 1px），本处只转发（改 frameless.h 的常量即全局传导）。
    static constexpr int resize_border = pp::platform::kResizeBorderPx;
    static constexpr int resize_border_max = pp::platform::kResizeBorderMaximizedPx;
    static constexpr int icon_btn = 32;          // .icon-btn{width:32px;height:32px}
    static constexpr int titlebar_pad_left = 18; // .toolbar{padding:0 18px}
    // 步骤切换组（.steps{gap:6px;margin-left:22px} / .step{padding:6px 16px;gap:8px}）
    static constexpr int step_gap = 6;
    // .steps{margin-left:22px}；实现取 20（= 22 − 2）是 §9.4 允许的 ±4px 布局微调：
    // Qt 与浏览器对同一字号的字宽差使标题/版本药丸累计宽 3–5px，微调后步骤组落点与原型对齐
    // （原型首步左沿 211 → 实现 212；step2/3 徽标与文案落在 ±2px 内）。
    static constexpr int step_margin_left = 20;
    static constexpr int step_pad_x = 16;
    static constexpr int step_pad_y = 6;
    static constexpr int step_badge = 18; // .step .n{width:18px;height:18px;border-radius:99px}
    static constexpr int step_badge_gap = 8;
    static constexpr int step_radius = 6; // .step{border-radius:6px}
    // ---- 卡片 / 控件（§9.2 控件规约）----
    static constexpr int card_radius = 8;    // .card{border-radius:8px}
    static constexpr int control_radius = 4; // --ctl 系列统一 r=4
    static constexpr int stage_radius = 6;   // .pv-stage{border-radius:6px}
    static constexpr int go_radius = 6;      // .go{border-radius:6px}
    static constexpr int go_height = 34;     // .go{height:34px}
    static constexpr int go_pad_x = 26;      // .go{padding:0 26px}
    static constexpr int pill_radius = 99;   // .pill{border-radius:99px}（规格值，供自绘消费）
    // QSS 侧等效值：Qt 的 QSS border-radius 超过盒半高时退化为直角（实测 99px → 矩形，
    // 与原型胶囊形状不符），故 QSS 一律用 ≤ 半高的半径（.pill/.ver 盒高 ≈16–18px → 9px）
    static constexpr int pill_radius_qss = 9;
    static constexpr int pill_pad_x = 7; // .pill{padding:1px 7px}
    static constexpr int pill_pad_y = 1;
    // .app-title .ver{padding:1px 6px}（版本药丸的内距与 .pill 不同，单列）
    static constexpr int ver_pad_x = 6;
    static constexpr int ver_pad_y = 1;
    // 运行期锁定（G5）的"变暗"系数：mockup .icon-btn.dim{opacity:.4} / .step.dim{opacity:.45}
    // （整钮不透明度；图标钮走 QSS 预合成色，步钮走自绘 painter.setOpacity）
    static constexpr double icon_dim_opacity = 0.4;
    static constexpr double step_dim_opacity = 0.45;
    static constexpr int mod_weight = 700;  // .spin.mod{font-weight:700}
    static constexpr int mod_border_px = 1; // .spin.mod 边框 1px
    static constexpr int checkbox_px = 15;  // .cbox 15px r=3
    static constexpr int checkbox_radius = 3;
    static constexpr int switch_w = 34; // .sw 34×18 药丸
    static constexpr int switch_h = 18;
    static constexpr int slider_track_px = 4; // §9.2 滑条 4px 轨 + 13px 柄
    static constexpr int slider_handle_px = 13;
    static constexpr int mini_progress_px = 8; // §9.2 迷你进度条 8px r=99（.obar）
    static constexpr int mini_progress_radius = 99;
    static constexpr int big_progress_px = 10;   // run-dark .bigbar{height:10px;border-radius:99px}
    static constexpr int progress_border_px = 1; // .obar/.bigbar{border:1px solid --ctl-bd}
    static constexpr int synth_stripe_deg = 115; // .obar i.synth 条纹角度
    static constexpr int synth_stripe_band_px = 6; // 单色带宽度（周期 = 2×6px）
    static constexpr int input_h = 28;             // .search / .mini-btn / .btn 高度
    static constexpr int row_h = 32;
    // ---- §9.3 排版硬约束（表单栅格；T12–T14 共用同一标签轴）----
    static constexpr int form_label_w = 70; // .grow{grid-template-columns:70px 1fr}
    static constexpr int form_col_gap = 12; // gap:12px
    static constexpr int form_row_h = 32;   // min-height:32px
};

// ---------------------------------------------------------------------------
// Tokens：§9.2 表全量双主题值（备注给出对应 mockup CSS 变量名/规则）
// ---------------------------------------------------------------------------
struct Tokens {
    ThemeMode mode = ThemeMode::Dark;
    bool dark() const { return mode == ThemeMode::Dark; }

    // window（§9.2「系统 Mica + #22252b 兜底」/「#e3e5ea」）+ mockup body 渐变三停
    QColor window;       // 深 #22252b（= 渐变 42% 停）/ 浅 #e3e5ea（= 45% 停）
    QColor window_grad0; // body linear-gradient(150deg, …) 0% 停
    QColor window_grad1; // 42% / 45% 停
    QColor window_grad2; // 100% 停
    // card（§9.2）
    QColor card;    // --card
    QColor card_bd; // --card-bd
    QColor card_hi; // --card-hi
    // text 三级（§9.2）
    QColor text;  // --txt
    QColor text2; // --txt2
    QColor text3; // --txt3
    // accent（§9.2「系统强调色（mockup #4cc2ff / #0067c0）」）
    QColor accent;     // --acc
    QColor accent_dim; // --acc-dim
    QColor accent_bd;  // --acc-bd
    QColor on_accent;  // 强调色上的文字：.step.active .n（深 #10222c / 浅 #ffffff）
    QColor go_text;    // .go 的文字色（**双主题同值 #0c1b24**；与 on_accent 是两条独立声明）
    // 语义色（mockup :root --ok/--warn/--err）
    QColor ok;
    QColor warn;
    QColor err;
    // control（§9.2「rgba(255,255,255,.06) r=4」/「#eef0f4」）
    QColor control;       // --ctl
    QColor control_bd;    // --ctl-bd
    QColor control_solid; // control 合成到窗口底（QPalette::Base/Button 用，不透明）
    // 骨架辅助（mockup 逐条）
    QColor line;        // .toolbar{border-bottom} = .bottom{border-top}
    QColor bottom_bg;   // .bottom{background}
    QColor stage_bg;    // .pv-stage{background:#0d0f12}（主题无关深色舞台）
    QColor close_hover; // .capbtn.close:hover{background:#c42b1c}（§9.1 逐字，双主题同值）
    // 运行期锁定（G5）的图标钮禁用态：= .icon-btn 的底/边/字整体按 opacity .4 合成后的等效不透明色
    // （QSS 不能设控件不透明度；等价做法是预合成 —— 见 Metrics::icon_dim_opacity 注释）
    QColor icon_disabled_bg;
    QColor icon_disabled_bd;
    QColor icon_disabled_fg;
    // 舞台框边（.pv-stage{border:1px solid rgba(255,255,255,.08)}；深色舞台 = 主题无关固定值）
    QColor stage_bd;
    // 进度条四态（run-dark .obar/.bigbar + i / i.done / i.fail / i.synth）：
    // 轨 = control，边框 = control_bd；实心 = accent，完成 = ok，失败 = err；
    // 合成（斜纹）= 双色带 progress_fill_from→to，角度/带宽见 Metrics（115° / 6px）
    QColor progress_fill_from; // #3aa3dc（.obar i.synth 第一带；.bigbar i 渐变首色）
    QColor progress_fill_to;   // #4cc2ff（.obar i.synth 第二带；.bigbar i 渐变尾色）
    // 改动值 .mod（§9.2「强调边框+强调色字」；mockup .spin.mod + font-weight:700）
    QColor mod_bd;   // = accent_bd
    QColor mod_text; // = accent
    // 半径（§9.2；QSS 也从这里取值，不写死字面量）
    int radius_card = Metrics::card_radius;
    int radius_control = Metrics::control_radius;
    int radius_stage = Metrics::stage_radius;
    int radius_pill = Metrics::pill_radius;
    // 卡片阴影（§9.2 浅色卡「0 1px 4px rgba(16,24,40,.06)」；深色 = blur 0 无阴影）
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

// §9.2 accent = 系统强调色。判据 = **调色板的 Highlight 是否来自平台样式**：
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

// accent：系统强调色优先；无 → mockup 兜底（深 #4cc2ff / 浅 #0067c0）
inline QColor accent_color(ThemeMode mode) {
    if (const QColor *sys = system_accent_cached())
        return *sys;
    return mode == ThemeMode::Dark ? QColor(0x4c, 0xc2, 0xff) : QColor(0x00, 0x67, 0xc0);
}

// 是否拿到了系统强调色（自检据此判定 accent 应等于系统色还是 mockup 兜底）
inline bool system_accent_available() { return system_accent_cached() != nullptr; }

// ---------------------------------------------------------------------------
// 值工厂：唯一给值点。全部字面量与 docs/mockups/*.html 的 :root/规则逐字对应。
// ---------------------------------------------------------------------------
inline Tokens tokens(ThemeMode mode) {
    const auto rgba = [](int r, int g, int b, double a) {
        return QColor(r, g, b, static_cast<int>(std::lround(a * 255.0)));
    };
    Tokens t;
    t.mode = mode;
    if (mode == ThemeMode::Dark) {
        // body: linear-gradient(150deg,#2b3038 0%,#22252b 42%,#25302c 100%)
        t.window = QColor(0x22, 0x25, 0x2b); // §9.2 window 兜底（Mica 由 DWM 提供）
        t.window_grad0 = QColor(0x2b, 0x30, 0x38);
        t.window_grad1 = QColor(0x22, 0x25, 0x2b);
        t.window_grad2 = QColor(0x25, 0x30, 0x2c);
        t.card = rgba(255, 255, 255, 0.05);    // --card
        t.card_bd = rgba(255, 255, 255, 0.09); // --card-bd
        t.card_hi = rgba(255, 255, 255, 0.02); // --card-hi
        t.text = QColor(0xec, 0xec, 0xf0);     // --txt
        t.text2 = QColor(0xa9, 0xaa, 0xb4);    // --txt2
        t.text3 = QColor(0x7a, 0x7b, 0x86);    // --txt3
        t.accent_dim = rgba(76, 194, 255, 0.16);
        t.accent_bd = rgba(76, 194, 255, 0.45);
        t.on_accent = QColor(0x10, 0x22, 0x2c); // .step.active .n color
        t.go_text = QColor(0x0c, 0x1b, 0x24);   // .go color（双主题同值）
        t.ok = QColor(0x6c, 0xcb, 0x8a);
        t.warn = QColor(0xe3, 0xb3, 0x41);
        t.err = QColor(0xf1, 0x70, 0x7b);
        t.control = rgba(255, 255, 255, 0.06);    // --ctl
        t.control_bd = rgba(255, 255, 255, 0.13); // --ctl-bd
        t.line = rgba(255, 255, 255, 0.07);       // toolbar border-bottom / bottom border-top
        t.bottom_bg = rgba(0, 0, 0, 0.14);        // .bottom{background}
        t.progress_fill_from = QColor(0x3a, 0xa3, 0xdc); // .obar i.synth 带 1
        t.progress_fill_to = QColor(0x4c, 0xc2, 0xff);   // .obar i.synth 带 2 / .bigbar i 尾
        t.shadow_color = QColor(0, 0, 0, 0);
        t.shadow_blur = 0; // 深色卡无阴影（§9.2）
        t.shadow_dy = 0;
        t.go_shadow_color = rgba(76, 194, 255, 0.25); // .go box-shadow
        t.go_shadow_blur = 10;
        t.go_shadow_dy = 2;
    } else {
        // body: linear-gradient(150deg,#e9ecf2 0%,#e3e5ea 45%,#e7efe9 100%)
        t.window = QColor(0xe3, 0xe5, 0xea); // §9.2 window
        t.window_grad0 = QColor(0xe9, 0xec, 0xf2);
        t.window_grad1 = QColor(0xe3, 0xe5, 0xea);
        t.window_grad2 = QColor(0xe7, 0xef, 0xe9);
        t.card = QColor(0xff, 0xff, 0xff);    // --card
        t.card_bd = rgba(0, 0, 0, 0.08);      // --card-bd
        t.card_hi = QColor(0xf6, 0xf7, 0xfa); // --card-hi
        t.text = QColor(0x1b, 0x1c, 0x20);    // --txt
        t.text2 = QColor(0x5d, 0x5f, 0x68);   // --txt2
        t.text3 = QColor(0x8b, 0x8d, 0x96);   // --txt3
        t.accent_dim = rgba(0, 103, 192, 0.10);
        t.accent_bd = rgba(0, 103, 192, 0.35);
        t.on_accent = QColor(0xff, 0xff, 0xff); // .step.active .n color
        t.go_text = QColor(0x0c, 0x1b, 0x24);   // .go color（双主题同值）
        t.ok = QColor(0x1d, 0x7a, 0x3e);
        t.warn = QColor(0x9a, 0x6a, 0x00);
        t.err = QColor(0xc4, 0x2b, 0x3a);
        t.control = QColor(0xee, 0xf0, 0xf4);   // --ctl
        t.control_bd = rgba(0, 0, 0, 0.12);     // --ctl-bd
        t.line = rgba(0, 0, 0, 0.08);           // toolbar border-bottom / bottom border-top
        t.bottom_bg = rgba(255, 255, 255, 0.5); // .bottom{background}
        // 进度条双色带：run-dark 是唯一进度页原型（双主题同值）
        t.progress_fill_from = QColor(0x3a, 0xa3, 0xdc);
        t.progress_fill_to = QColor(0x4c, 0xc2, 0xff);
        t.shadow_color = rgba(16, 24, 40, 0.06);
        t.shadow_blur = 4; // 0 1px 4px rgba(16,24,40,.06)
        t.shadow_dy = 1;
        t.go_shadow_color = rgba(0, 103, 192, 0.20); // .go box-shadow
        t.go_shadow_blur = 10;
        t.go_shadow_dy = 2;
    }
    t.accent = accent_color(mode);
    t.mod_bd = t.accent_bd;                   // §9.2 .mod = 强调边框
    t.mod_text = t.accent;                    // §9.2 .mod = 强调色字
    t.stage_bg = QColor(0x0d, 0x0f, 0x12);    // .pv-stage（主题无关）
    t.stage_bd = rgba(255, 255, 255, 0.08);   // .pv-stage{border:1px solid rgba(255,255,255,.08)}
    t.close_hover = QColor(0xc4, 0x2b, 0x1c); // §9.1 关闭悬停（双主题同值）
    t.control_solid = composite(t.control, t.window);
    // 锁定态图标钮（.icon-btn.dim{opacity:.4} 的静态等效）：三色都按 .4 合成到窗口底
    t.icon_disabled_bg =
        at_opacity(t.control, t.window, Metrics::icon_dim_opacity); // 底（--ctl 本身带透明）
    t.icon_disabled_bd = at_opacity(composite(t.control_bd, t.window), t.window,
                                    Metrics::icon_dim_opacity); // 边（--ctl-bd 合成成实体色再淡出）
    t.icon_disabled_fg = at_opacity(t.text2, t.window, Metrics::icon_dim_opacity);
    return t;
}

// 明暗跟随系统（§9.2 末行）；Unknown → 深色（原型基准 meta-dark）
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
// 强制；未设置 → 跟随系统。强制档供 W5 双主题截图走查用（系统同一时刻只能是一种模式），
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
    // CSS linear-gradient(150deg, …) 的 Qt 近似：QSS 只支持盒坐标两点式，
    // (0.25,0)→(0.75,1) 对应 150°（CSS 角 = 从正上起顺时针；见 T9 对照表备注）。
    return QStringLiteral("qlineargradient(x1:0.25, y1:0, x2:0.75, y2:1, stop:0 %1, "
                          "stop:0.43 %2, stop:1 %3)")
        .arg(css_color(t.window_grad0), css_color(t.window_grad1), css_color(t.window_grad2));
}

inline QFont font(double px, int weight = QFont::Normal, bool tabular = false) {
    QFont f(QString::fromLatin1(Typography::family));
    f.setFamilies({QString::fromLatin1(Typography::family), QStringLiteral("Segoe UI"),
                   QString::fromLatin1(Typography::family_ui), QStringLiteral("Noto Sans SC")});
    f.setPointSizeF(px_to_pt(px));
    f.setWeight(QFont::Weight(weight));
    if (tabular)
        f.setFeature(QFont::Tag("tnum"), 1); // §9.2 数值 tabular-nums
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

// 改动值 .mod（§9.2「强调边框+强调色字」，mockup .spin.mod 另带 700 字重）：
// 动态属性钩子 + QSS 属性选择器 —— 消费入口 = theme::set_mod(widget, changed)。
inline constexpr const char *kModProperty = "ppMod";
// 卡头徽标 .pill 的动态属性钩子（QLabel[ppPill="true"]）：卡头/计数徽标共用一条规则
inline constexpr const char *kPillProperty = "ppPill";

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

// 骨架 QSS：只作用于 pp-* 钩子（§9.3 命名法）与 ppMod 动态属性，避免误伤既有页面控件
inline QString style_sheet(const Tokens &t) {
    const QString card = css_color(t.card), card_bd = css_color(t.card_bd);
    const QString radius_card = QString::number(t.radius_card);
    const QString radius_control = QString::number(t.radius_control);
    QString qss;
    // 窗口底：深色 = Mica 兜底渐变（§9.2 window）；浅色 = mockup body 渐变
    qss += QStringLiteral("QWidget#pp-root { background: ") + window_gradient_qss(t) +
           QStringLiteral("; }\n");
    // 顶栏（兼标题栏）：透明 → 露出窗口底；下沿 1px 分隔线
    qss += QStringLiteral("QWidget#pp-titlebar { background: transparent; border-bottom: 1px "
                          "solid ") +
           css_color(t.line) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel#pp-app-title { color: ") + css_color(t.text) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel#pp-app-ver { color: ") + css_color(t.text3) +
           QStringLiteral("; border: 1px solid ") + card_bd + QStringLiteral("; border-radius: ") +
           QString::number(Metrics::pill_radius_qss) + // .ver{border-radius:99px} → QSS 胶囊口径
           QStringLiteral("px; padding: ") + QString::number(Metrics::ver_pad_y) +
           QStringLiteral("px ") + QString::number(Metrics::ver_pad_x) + QStringLiteral("px; }\n");
    // caption 三钮（§9.1 能力表 + mockup .capbtn：46×满高；关闭悬停 #c42b1c）
    qss += QStringLiteral("QPushButton#pp-cap-min, QPushButton#pp-cap-max, "
                          "QPushButton#pp-cap-close { background: transparent; border: none; "
                          "color: ") +
           css_color(t.text2) + QStringLiteral("; }\n");
    qss += QStringLiteral("QPushButton#pp-cap-min:hover, QPushButton#pp-cap-max:hover { "
                          "background: ") +
           css_color(t.control) + QStringLiteral("; }\n");
    qss += QStringLiteral("QPushButton#pp-cap-close:hover { background: ") +
           css_color(t.close_hover) + QStringLiteral("; color: #ffffff; }\n");
    // 预设 / 设置图标钮（.icon-btn）
    qss += QStringLiteral("QToolButton#pp-presets, QToolButton#pp-settings { background: ") +
           css_color(t.control) + QStringLiteral("; border: 1px solid ") + css_color(t.control_bd) +
           QStringLiteral("; border-radius: ") + radius_control + QStringLiteral("px; color: ") +
           css_color(t.text2) + QStringLiteral("; }\n");
    qss += QStringLiteral("QToolButton#pp-presets:hover, QToolButton#pp-settings:hover { color: ") +
           css_color(t.text) + QStringLiteral("; }\n");
    // 运行期锁定（G5 + 设计 §9.3「设置/预设置灰」）：原型 .icon-btn.dim{opacity:.4} 整钮变暗。
    // QSS 无控件级不透明度 → 用 theme::at_opacity 预合成的等效底/边/字（见 tokens()）。
    // 顺序：写在 :hover 之后（同优先级下后者胜），保证禁用态不被 hover 提亮。
    qss += QStringLiteral("QToolButton#pp-presets:disabled, QToolButton#pp-settings:disabled { "
                          "background: ") +
           css_color(t.icon_disabled_bg) + QStringLiteral("; border: 1px solid ") +
           css_color(t.icon_disabled_bd) + QStringLiteral("; color: ") +
           css_color(t.icon_disabled_fg) + QStringLiteral("; }\n");
    // 卡片（.card：r=8 + 边框；浅色的 0 1px 4px 阴影由 ui 侧 QGraphicsDropShadowEffect 落地）
    qss += QStringLiteral("QFrame#pp-card { background: ") + card +
           QStringLiteral("; border: 1px solid ") + card_bd + QStringLiteral("; border-radius: ") +
           radius_card + QStringLiteral("px; }\n");
    qss += QStringLiteral("QLabel#pp-card-title { color: ") + css_color(t.text) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel#pp-card-hint { color: ") + css_color(t.text3) +
           QStringLiteral("; }\n");
    // 卡头徽标（.pill：accent 字 + accent-dim 底 + accent-bd 边 + r=99 + padding 1px 7px）
    qss += QStringLiteral("QLabel[%1=\"true\"] { color: ").arg(QString::fromLatin1(kPillProperty)) +
           css_color(t.accent) + QStringLiteral("; background: ") + css_color(t.accent_dim) +
           QStringLiteral("; border: 1px solid ") + css_color(t.accent_bd) +
           QStringLiteral("; border-radius: ") + QString::number(Metrics::pill_radius_qss) +
           QStringLiteral("px; padding: ") + QString::number(Metrics::pill_pad_y) +
           QStringLiteral("px ") + QString::number(Metrics::pill_pad_x) + QStringLiteral("px; }\n");
    // 预览舞台（.pv-stage）
    qss += QStringLiteral("QFrame#pp-preview-stage { background: ") + css_color(t.stage_bg) +
           QStringLiteral("; border: 1px solid ") + css_color(t.stage_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_stage) +
           QStringLiteral("px; }\n");
    // 底栏（.bottom）
    qss += QStringLiteral("QWidget#pp-bottombar { background: ") + css_color(t.bottom_bg) +
           QStringLiteral("; border-top: 1px solid ") + css_color(t.line) + QStringLiteral("; }\n");
    qss +=
        QStringLiteral("QLabel#pp-status { color: ") + css_color(t.text2) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel#pp-output-status { color: ") + css_color(t.text3) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel#pp-status-dot { color: ") + css_color(t.ok) +
           QStringLiteral("; }\n");
    // 开始运行（.go：accent 底 + **#0c1b24 字**（双主题同值）+ 13px/700 + r=6；
    // 禁用态 = --ctl 底 + --txt3 字 + 1px --ctl-bd 边（run-dark 的 .go 禁用变体）；
    // box-shadow 0 2px 10px 由 ui 侧 QGraphicsDropShadowEffect 落地）
    qss += QStringLiteral("QPushButton#pp-start { background: ") + css_color(t.accent) +
           QStringLiteral("; color: ") + css_color(t.go_text) +
           QStringLiteral("; border: none; border-radius: ") + QString::number(Metrics::go_radius) +
           QStringLiteral("px; padding: 0 ") + QString::number(Metrics::go_pad_x) +
           QStringLiteral("px; font-weight: 700; }\n");
    qss += QStringLiteral("QPushButton#pp-start:disabled { background: ") + css_color(t.control) +
           QStringLiteral("; color: ") + css_color(t.text3) +
           QStringLiteral("; border: 1px solid ") + css_color(t.control_bd) +
           QStringLiteral("; }\n");
    // splitter 手柄 = 列间距（.main gap:10px），透明不抢视觉
    qss += QStringLiteral("QSplitter#pp-splitter::handle { background: transparent; }\n");
    // 改动值 .mod（动态属性钩子，T12 消费）
    qss += mod_qss(t);
    return qss;
}

} // namespace theme
} // namespace pp::ui
