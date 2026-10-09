// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M5-T2）— formkit：表单行构造器 + 自绘控件库
// （契约 C2「表单唯一入口」；设计说明书 v6 §5 表单布局体系 R1–R4 / §6 控件语义决策表 /
// §10 Qt 实现映射 / §11 无障碍）
//
// C2：页面的表单行一律经本文件构造 —— R1–R4 在**构造层**强制（pitch 28、标签轨 80 右对齐、
// 列距 12、字段宽仅 88 / 160 / flex、双列 80/12/轨/24/80/12/轨、单位入 16px 槽、范围入 10px
// 注释），页面不得绕开 formkit 手摆栅格。
//
// 取值单源：
//   * 几何（栅格 + 控件度量）= `ui/formkit_layout.h`（零 Qt、可单测；构造器与单测同一份常量）；
//   * 颜色/字阶/圆角 = `ui/theme.h` 令牌（C1）。本文件不写任何色值字面量，不做平台分叉
//     （M3 铁律六），也不引平台头。
//
// 控件语义（§6 决策表；不得混用）：
//   * SwitchControl      —— 仅"立即生效的布尔"
//   * check_row / CheckBox —— 表单布尔（随表单提交）
//   * SegmentedControl   —— 互斥模式唯一形态
//   * CheckableCard      —— 多选集合（整卡可点 + 预留对勾位 → 选中零形变）
//   * ChipButton         —— 批量快捷打标（键帽 + 文字，固定宽）
//   * PillBadge          —— 状态四色 / 中性计数；完成态唯一形态 = PillBadge::completed()
//   * ProgressStrip      —— C8：实心=已写盘 / 斜纹=合成 / 绿=完成 / 琥珀=异常 / 红=失败
//   * IconButton         —— 图标钮（28×28 r4 悬停显底；必有 accessibleName + tooltip，§11）
//
// 主题切换：自绘控件持有令牌副本（set_tokens，页面在重建 QSS 时同步调用）；行构造器产出的
// 标签/槽/注释走 theme.h 的 `ppFk` 动态属性 QSS（随窗口级样式表自动换色，无需页面介入）。

#pragma once

// 包含顺序：theme.h 自 A5/T4 起显式引 QtWidgets 侧（qApp 的定义在 <QApplication>），
// 本头不再需要「先引 <QApplication> 再引 theme.h」的规避 —— 顺序依赖已消除（原注释随 A5
// 修复作废）。
#include "ui/formkit_layout.h"
#include "ui/icons.h" // M5-T3：勾形/胶囊前置图标一律走 icons 库（C3；与 theme.h 无环）
#include "ui/theme.h"

#include <QAbstractButton>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QList>
#include <QRectF>
#include <QSize>
#include <QSpinBox>
#include <QString>
#include <QWidget>

namespace pp::ui::formkit {

// ---------------------------------------------------------------------------
// 几何对齐（C1 单源）：本头复述的栅格/度量值与 theme.h 令牌逐条一致 → 漂移即编译失败。
// 表单栅格真源 = formkit_layout.h（标签轨 80 / pitch 28）；M4 的迁移期双源
// theme::Metrics::form_label_w(70) / form_row_h(32) 已随 W-G2/W-G3 迁完在 A10/T11 退役
// （值只剩本头+theme.h 一份，见 theme.h Metrics 的同级注释）。
// ---------------------------------------------------------------------------
static_assert(kRowPitch == 28, "R1 行 pitch 28");
static_assert(kLabelW == 80, "R2 标签轨 80");
static_assert(kColGap == theme::Metrics::form_col_gap, "R2 列距 12");
static_assert(kFieldShort == theme::Metrics::field_w_short, "R2 短档 88");
static_assert(kFieldStd == theme::Metrics::field_w, "R2 标准档 160");
static_assert(kControlH == theme::Metrics::spin_h, "R4 spin 高 24");
static_assert(kControlH == theme::Metrics::combo_h, "R4 下拉高 24");
static_assert(kControlH == theme::Metrics::input_h, "R4 文本输入高 24");
static_assert(kSpinArrowW == theme::Metrics::spin_arrow_w, "R4 spin 箭头列 17");
static_assert(kSpinArrowH == theme::Metrics::spin_arrow_h, "R4 箭头 ▲▼ 各 12");
static_assert(kSpinPadX == theme::Metrics::spin_pad_x, "R4 spin 值区内距 7");
static_assert(kComboArrowPx == theme::Metrics::combo_arrow_px, "R4 下拉箭头 8");
static_assert(kSegH == theme::Metrics::seg_h && kSegW == theme::Metrics::seg_w, "R4 分段 24/88");
static_assert(kSegRadius == theme::Metrics::seg_radius, "R4 分段 r3");
static_assert(kSwitchW == theme::Metrics::switch_w && kSwitchH == theme::Metrics::switch_h,
              "R4 开关 30×16");
static_assert(kSwitchKnob == theme::Metrics::switch_knob, "R4 开关钮 12");
static_assert(kCheckPx == theme::Metrics::checkbox_px, "R4 复选框 13×13");
static_assert(kCheckRadius == theme::Metrics::checkbox_radius, "R4 复选框 r2");
static_assert(kBtnH == theme::Metrics::btn_h, "R4 按钮高 26");
static_assert(kPillH == theme::Metrics::pill_h && kPillRadius == theme::Metrics::pill_radius,
              "R4 pill 16/r3");
static_assert(kProgressH == theme::Metrics::progress_h, "R4 进度条高 6");
static_assert(kProgressRadius == theme::Metrics::progress_radius, "R4 进度条 r99");
static_assert(kIconBtn == theme::Metrics::icon_btn, "R4 图标钮 28");
static_assert(kIconBtnRadius == theme::Metrics::icon_btn_radius, "R4 图标钮 r4");
static_assert(kNotePx == static_cast<int>(theme::Typography::hint_px), "R3 注释 10px");
static_assert(kPillPx10 == static_cast<int>(theme::Typography::badge_px * 10.0), "R4 徽章 9.5px");
static_assert(kPillWeight == theme::Typography::badge_weight, "R4 徽章 700");

// 自绘控件的默认令牌：跟随系统主题的 v6 令牌集（页面在重建 QSS 时用 set_tokens 覆盖）。
inline theme::Tokens default_tokens() { return theme::tokens(theme::preferred_theme_mode()); }

// ---------------------------------------------------------------------------
// 行构造器（C2；R1–R4 在构造层强制）
// ---------------------------------------------------------------------------

// 单列行：[80 右对齐标签][12][字段轨]；轨内 = 控件（+ 单位槽 16 + 范围注释 10px）。
// 返回行容器与轨内辅助件（未给 unit/note 时为 nullptr）。
struct Row {
    QWidget *row = nullptr;  // 行容器（固定高 28；放进卡内 QVBoxLayout）
    QLabel *label = nullptr; // 标签轨（80px 右对齐）
    QLabel *unit = nullptr;  // 16px 单位槽（R3：单位不占标签位）
    QLabel *note = nullptr;  // 10px 范围注释（R3）
};

Row frow(QWidget *parent, const QString &label, QWidget *field,
         FieldWidth width = FieldWidth::Standard, const QString &unit = QString(),
         const QString &note = QString());

// 双列行的一格：[80 右对齐标签][轨：控件 (+ 单位槽 16)]
struct FieldCell {
    QString label;
    QWidget *field = nullptr;
    FieldWidth width = FieldWidth::Standard;
    QString unit;
};

// 双列行：[80][12][轨][24][80][12][轨] —— 第二标签在固定 x（second_label_x(轨)），
// 不随内容漂移（R2）。
struct DoubleRow {
    QWidget *row = nullptr;
    QLabel *label1 = nullptr;
    QLabel *label2 = nullptr;
};

DoubleRow double_row(QWidget *parent, const FieldCell &left, const FieldCell &right);

// kv 双列行：两对 [标签 80 右对齐][字段轨]，值右对齐 + tabular（§4.2「数值」档）。
struct KvRow {
    QWidget *row = nullptr;
    QLabel *key1 = nullptr;
    QLabel *value1 = nullptr;
    QLabel *key2 = nullptr;
    QLabel *value2 = nullptr;
};

KvRow kv_row(QWidget *parent, const QString &key1, const QString &value1, const QString &key2,
             const QString &value2, FieldWidth width = FieldWidth::Standard);

// 表单布尔勾选框：13×13 r2 自绘（选中 = acc 填充 + 白勾；勾形是矢量路径 —— §3.3 禁字形图标），
// 本体是 QCheckBox（无障碍角色 = checkbox，§11）；文字由本控件自己画在勾选框右侧（语序固定
// "勾选框→文字"，点击文字同样切换）。
class CheckBox : public QCheckBox {
public:
    explicit CheckBox(QWidget *parent = nullptr);
    void set_tokens(const theme::Tokens &t);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    // 整件可点（勾选框 + 右侧文字都在本件内；语序固定"勾选框→文字"）
    bool hitButton(const QPoint &pos) const override;

private:
    theme::Tokens tokens_ = default_tokens();
};

// check 行（表单布尔）：[标签 80 右对齐][12][13×13 勾选框 → 文字] —— 语序固定，文字与勾选框
// 同轨（R4）；标签为空串时标签轨仍按 R2 保留（网格不错位）。
struct CheckRow {
    QWidget *row = nullptr;
    QLabel *label = nullptr; // 标签轨（80 右对齐）
    CheckBox *box = nullptr; // 勾选框（含右侧文字）
};

CheckRow check_row(QWidget *parent, const QString &label, const QString &text,
                   const QString &object_name = QString());

// ---------------------------------------------------------------------------
// 数值控件（K2 裁定：保留 spinbox）
//   * 高 24、值区右对齐 + tabular（QFont tnum；Qt QSS 不能表达 text-align/tnum → C++ 落地）；
//   * 箭头列 17px（▲▼ 各 12）与盒高由 theme.h 的 `ppFk="spin"` QSS 落地（加性扩展）；
//   * 魔法值：范围下端显示"自动"（specialValueText）；单位入 16px 槽、范围入 10px 注释。
// ---------------------------------------------------------------------------
struct SpinSpec {
    int min = 0;
    int max = 100;
    int value = 0;
    bool auto_magic = false; // true → 范围下端 = 魔法值档，显示"自动"（§7.4「0 = 自动」）
    QString unit;            // 非空 → 16px 单位槽
    QString note;            // 非空 → 10px 范围注释（"1–10"）
    QString object_name;     // 探针钩子（pp-*，§10）
};

struct DoubleSpinSpec {
    double min = 0.0;
    double max = 100.0;
    double value = 0.0;
    int decimals = 1;
    bool auto_magic = false;
    QString unit;
    QString note;
    QString object_name;
};

QSpinBox *spin_box(QWidget *parent, const SpinSpec &spec, FieldWidth width = FieldWidth::Standard);
QDoubleSpinBox *dspin_box(QWidget *parent, const DoubleSpinSpec &spec,
                          FieldWidth width = FieldWidth::Standard);

// 单列数值行 = spin_box + frow（把"控件 + 单位槽 + 范围注释"一次摆好）
struct SpinRow {
    Row row;
    QSpinBox *spin = nullptr;
};

SpinRow spin_row(QWidget *parent, const QString &label, const SpinSpec &spec,
                 FieldWidth width = FieldWidth::Standard);

// ---------------------------------------------------------------------------
// 自绘控件（取色取形全走 theme.h 令牌；零平台分叉）
// ---------------------------------------------------------------------------

// 开关（R4 30×16 药丸 + 12 钮）：**仅**用于"立即生效的布尔"（§6 决策表）。
class SwitchControl : public QAbstractButton {
public:
    explicit SwitchControl(QWidget *parent = nullptr);
    void set_tokens(const theme::Tokens &t);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    theme::Tokens tokens_ = default_tokens();
};

// 分段控件（互斥模式唯一形态；R4 高 24 r3 段宽 88 等宽）：选中段填充 acc，选中/悬停
// **零宽度变化**（段尺寸固定）。段本体是 QAbstractButton（键盘/无障碍语义），容器自绘底与描边。
// A27：段高由 Segment **自持**（sizeHint + fixedHeight = kSegH − 2，见 formkit_layout.h 的
// seg_inner_h）—— QAbstractButton 无基类 sizeHint，调用点不得再自打段高补丁。
class SegmentedControl : public QWidget {
    Q_OBJECT
public:
    explicit SegmentedControl(QWidget *parent = nullptr);

    // 段宽 88 等宽（R4）；object_name 供探针定位（pp-*）
    void add_segment(const QString &text, const QString &object_name = QString());
    int count() const;
    int current_index() const;
    void set_current_index(int index); // 程序化设置（不发射信号）
    QWidget *segment(int index) const;

    void set_tokens(const theme::Tokens &t);

signals:
    void current_changed(int index);

protected:
    void paintEvent(QPaintEvent *event) override; // 容器底/描边/圆角（段本体自绘）

private:
    class Segment; // 段（QAbstractButton；定义在 .cpp）
    Segment *segment_at(int index) const;
    void activate(int index); // 段被点击/键盘切换 → 落选中 + 发信号
    void step(int delta);     // 段内 ←/→ 键在段之间移动选中（§11 Tab 序按视觉顺序）

    QList<Segment *> segments_;
    int current_ = -1;
    theme::Tokens tokens_ = default_tokens();
};

// 磁贴卡（多选集合唯一形态；整卡可点 + 预留对勾位）：固定宽、主字（12/700）+ 后缀（9.5），
// 勾选只换底色/勾形，**不改宽**（§12-4）。
class CheckableCard : public QAbstractButton {
public:
    explicit CheckableCard(QWidget *parent = nullptr);

    void set_labels(const QString &name, const QString &hint = QString());
    void set_tokens(const theme::Tokens &t);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString name_;
    QString hint_;
    theme::Tokens tokens_ = default_tokens();
};

// 徽章 pill（R4 高 16 r3 9.5px/700）：四语义色 ok/warn/err + 中性计数（n）；accent 档保留。
// 完成态**唯一形态** = completed()（绿胶囊"✓ 已完成"；§7.3/§8 —— 别处不得自造完成态胶囊）。
class PillBadge : public QLabel {
public:
    enum class Tone { Neutral, Accent, Ok, Warn, Err };

    explicit PillBadge(QWidget *parent = nullptr);

    void set_tone(Tone tone);
    Tone tone() const { return tone_; }
    void set_tokens(const theme::Tokens &t);
    QSize sizeHint() const override;
    // 前置矢量图标（M5-T3 加性扩展）：图标盒固定（kPillIconPx + 4px 间距）→ 有无图标都不改
    // 胶囊高度与语义；完成态的勾形即由此承载（"已完成" 文案里不再有 ✓ 字形）。
    void set_icon(icons::Name name);
    bool has_icon() const { return has_icon_; }

    static PillBadge *completed(QWidget *parent); // "✓ 已完成" 绿胶囊（勾形 = 矢量图标）
    static PillBadge *count(QWidget *parent, int n);
    static PillBadge *of(QWidget *parent, Tone tone, const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Tone tone_ = Tone::Neutral;
    bool has_icon_ = false;
    icons::Name icon_name_ = icons::Name::Check;
    theme::Tokens tokens_ = default_tokens();
};

// 打标 chip（批量快捷打标唯一形态）：键帽（13×13 r2）+ 文字（10.5/600），固定宽 —— 勾选/悬停
// 只换底色，不改宽（与磁贴同口径）。
class ChipButton : public QAbstractButton {
public:
    explicit ChipButton(QWidget *parent = nullptr);

    void set_key_text(const QString &key); // 键帽文字（"1"）
    QString key_text() const { return key_; }
    void set_tokens(const theme::Tokens &t);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString key_;
    theme::Tokens tokens_ = default_tokens();
};

// 进度条（R4 高 6 r99）。C8 语义不变：
//   Pending（空）/ Active(实心 accent = 已写盘) / Active+synthetic（斜纹双色带 = 合成）/
//   Done（绿 = 完成）/ Warn（琥珀 = 异常，0 字节）/ Failed（红 = 失败）。
// 斜纹与实心必须可被像素取证区分 → 斜纹用 progress_fill_from/to 双色带（115°/6px/12px 令牌）。
class ProgressStrip : public QWidget {
public:
    enum class Kind { Pending, Active, Done, Warn, Failed };

    explicit ProgressStrip(QWidget *parent = nullptr);

    void set_tokens(const theme::Tokens &t);
    void set_state(Kind kind, double frac, bool synthetic = false);

    Kind kind() const { return kind_; }
    double frac() const { return frac_; }
    bool synthetic() const { return synthetic_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool settled() const;
    QColor fill_color() const;
    QBrush stripe_brush(const QRectF &fill) const;

    theme::Tokens tokens_ = default_tokens();
    Kind kind_ = Kind::Pending;
    double frac_ = 0.0;
    bool synthetic_ = false;
};

// 图标钮（R4 28×28 r4，悬停显底）。§11：图标钮必有 accessibleName + tooltip —— 由
// set_action(text) 一次设齐（图标本体走 QAbstractButton::setIcon，见 icons.h）。
class IconButton : public QAbstractButton {
public:
    explicit IconButton(QWidget *parent = nullptr);

    void set_action(const QString &text); // tooltip + accessibleName（同文案）
    void set_tokens(const theme::Tokens &t);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    theme::Tokens tokens_ = default_tokens();
};

} // namespace pp::ui::formkit
