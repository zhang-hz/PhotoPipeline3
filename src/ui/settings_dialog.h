// PP-FROZEN(file) — src/ui/settings_dialog.h
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — settings dialog (M1b frozen)
//
// M5-T10（W-G3 §7.4 重做）：**公开 API 逐字不变**（构造 + `settings()`）；实现重写，
//   并对本头**加性**扩展一个「弹窗套件」`dialogkit`（自绘标题栏 40 / 下划线页签 / 底栏 48 /
//   焦点环 / 弹窗 QSS）—— 落点说明见下方 dialogkit 注释。
#pragma once
#include "core/settings.h"
#include "ui/icons.h" // 标题栏矢量图标（C3：禁字形图标）
#include "ui/theme.h" // dialogkit 的令牌入参（§4 单源）

#include <QColor>
#include <QDialog>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

namespace pp::ui {

// ===========================================================================
// dialogkit —— 弹窗套件（§7.4 设置对话框 / §4.3 圆角 / §5 R4 控件规格 / §11 无障碍）
// ---------------------------------------------------------------------------
// 落点：本套件落在 settings_dialog.{h,cpp}（settings 是 §7.4 的**母版**弹窗），
// exif_editor / presets_dialog 复用同一实现 —— 三个弹窗一套 chrome 与控件语言，
// 实现只有一份（M3 铁律六 单实现）。
//
// 度量取值：§7.4 逐字（660 宽 / 标题栏 40 / 底栏 48）+ tokens.css `.dlg*` 对照值
// （页签条 34 / 页签 32 / 下划线 2 / 正文内边距 14·18·10）；两处冲突以 §7.4 为准（A11）。
// 本套件零色值字面量：颜色一律从 `theme::Tokens` 取（C1）。
// ===========================================================================
namespace dialogkit {

inline constexpr int kDialogWidth = 660;  // §7.4「660px 宽」
inline constexpr int kTitleBarH = 40;     // §7.4「标题栏 40px」
inline constexpr int kTabsBarH = 34;      // tokens.css .dlg-tabs{height:34px}
inline constexpr int kTabH = 32;          // tokens.css .dlg-tab{height:32px}
inline constexpr int kTabPadX = 13;       // tokens.css .dlg-tab{padding:0 13px}
inline constexpr int kTabBarPadX = 14;    // tokens.css .dlg-tabs{padding:0 14px}
inline constexpr int kTabUnderlinePx = 2; // tokens.css .dlg-tab{border-bottom:2px}
inline constexpr int kTitlePadLeft = 14;  // tokens.css .dlg-tb{padding:0 4px 0 14px}
inline constexpr int kTitleIconPx = 17;   // tokens.css .dlg-tb svg{width:17px}
inline constexpr int kFooterH = 48;       // §7.4「底栏 48px」
inline constexpr int kFooterPadX = 18;    // tokens.css .dlg-ft{padding:0 18px}
inline constexpr int kBodyPadTop = 14;    // tokens.css .dlg-body{padding:14px 18px 10px}
inline constexpr int kBodyPadX = 18;
inline constexpr int kBodyPadBottom = 10;
inline constexpr int kFocusRingPx = 1;  // §11「焦点环自绘（accent 描边 1px，禁系统虚线框）」
inline constexpr int kFocusRingPad = 2; // 环画在控件盒外 2px（不压控件自身描边）

// 控件选择器词汇（与 ui/page_output.cpp 的 ppField 取值同源：input / combo / button /
// primary / hint。弹窗内同义控件用同一组钩子 → 三弹窗与主页面的控件语言一致）。
inline constexpr const char *kFieldProperty = "ppField";
inline constexpr const char *kFieldInput = "input";
inline constexpr const char *kFieldCombo = "combo";
inline constexpr const char *kFieldButton = "button";
inline constexpr const char *kFieldPrimary = "primary";
inline constexpr const char *kFieldHint = "hint";

// objectName（探针/走查定位；§10 测试钩子 pp-*）
inline constexpr const char *kTitleBarName = "pp-dlg-titlebar";
inline constexpr const char *kTabsName = "pp-dlg-tabs";
inline constexpr const char *kPagesName = "pp-dlg-pages";
inline constexpr const char *kFooterName = "pp-dlg-footer";
inline constexpr const char *kOkName = "pp-dlg-ok";
inline constexpr const char *kCancelName = "pp-dlg-cancel";
inline constexpr const char *kDefaultsName = "pp-dlg-defaults";
inline constexpr const char *kFocusRingName = "pp-focus-ring";

// 弹窗 QSS（标题栏/按钮/输入控件/下划线页签/焦点抑制/.mod；选择器只认 pp-*/ppField 钩子）。
// 三弹窗共用这一条来源：弹窗是独立顶层窗口，主窗的 style_sheet 不覆盖其子树。
QString dialog_qss(const theme::Tokens &t);

// 自绘标题栏（§7.4）：[矢量图标 17][标题 12/700][……][关闭 caption 46×32]；
// 本体是可拖区（Frameless::attach 的 caption 入参），关闭钮 = 命中区外的拖拽排除项。
struct TitleBar {
    QWidget *widget = nullptr;
    QLabel *title = nullptr;
    QPushButton *close = nullptr; // 46×32，悬停 #c42b1c（§3.3），必有 accessibleName+tooltip
};
TitleBar make_title_bar(QWidget *parent, icons::Name icon, const QString &title);
// 主题重放：标题栏的矢量图标换色 + 关闭 caption 两态色（图标取色不吃 QSS）
void restyle_title_bar(const TitleBar &bar, const theme::Tokens &t);

// 下划线页签（§7.4 / tokens.css .dlg-tabs）：段 = QAbstractButton（键盘可达、角色 = button
// group），选中段 2px accent 下划线 + 通栏 hairline 基线；**不用 QTabWidget**（修 4-05：
// 无障碍树重复 Tab 节点；页签序由 texts() 逐字取证）。
class UnderlineTabs : public QWidget {
    Q_OBJECT
public:
    explicit UnderlineTabs(QWidget *parent = nullptr);

    int add_tab(const QString &text, const QString &object_name = QString());
    int count() const;
    int current_index() const;
    QStringList texts() const;        // 页签序（逐字；探针取证）
    QWidget *tab_at(int index) const; // 段控件（探针读 accessibleName/几何用）
    void set_tokens(const theme::Tokens &t);

    QSize sizeHint() const override;

public slots:
    void set_current_index(int index); // 程序化设置（不发射信号）

signals:
    void current_changed(int index);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    class Tab;
    Tab *tab_at_index(int index) const;
    void activate(int index);
    void step(int delta); // ←/→ 在页签间移动（§11 Tab 序/键盘可达）

    QList<Tab *> tabs_;
    int current_ = -1;
    theme::Tokens tokens_ = theme::tokens(theme::ThemeMode::Dark);
};

// 焦点环（§11）：跟随宿主的焦点控件，在其外 2px 自绘 accent 1px 圆角描边。
// 系统虚线框由 dialog_qss 的 `outline: none` 关闭（一处规则）；本件只负责"画在哪"。
class FocusRing : public QWidget {
public:
    explicit FocusRing(QWidget *host); // host = 顶层弹窗

    void set_tokens(const theme::Tokens &t);
    QColor pen_color() const { return tokens_.accent; } // 探针：描边色 = 令牌 accent
    QWidget *target() const { return target_; }
    QRect target_rect() const; // 焦点控件在宿主坐标下的盒（探针复核环位 = 盒外 2px）

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void sync(); // 焦点/几何变化 → 重定位（隐藏：焦点不在本宿主内）
    QWidget *host_ = nullptr;
    QWidget *target_ = nullptr;
    theme::Tokens tokens_ = theme::tokens(theme::ThemeMode::Dark);
};

// 26px 按钮工厂（R4 按钮高 26；主按钮实心 acc + acc-on 字 + 主操作居右，次按钮描边）。
QPushButton *make_button(QWidget *parent, const QString &text, bool primary);

// ---------------------------------------------------------------------------
// C7 几何探针（--ui-smoke 的 settings-geom 行；非用户可见）
// 值表驱动：返回 checked（条目数）与 bad（不符项说明）。判据实现与控件同 TU，
// 调用方（mainwindow 的 --ui-smoke 段）只负责打印与 smoke_fail。
// ---------------------------------------------------------------------------
struct GeomProbeReport {
    int checked = 0;
    QStringList bad;
};
GeomProbeReport settings_geom_probe(const QDialog &dialog);

} // namespace dialogkit

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const pp::AppSettings &current, QWidget *parent = nullptr);
    pp::AppSettings settings() const; // edited values (OK or Apply)
};

} // namespace pp::ui
