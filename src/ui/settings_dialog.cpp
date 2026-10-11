// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — settings dialog（M1b-U6 落地 / M5-T10 §7.4 重做）
//
// 规格：docs/qt-gui-design-spec.md §7.4（设置对话框逐条）+ §5 R1–R4（表单体系）+ §6（控件语义）
//       + §11（无障碍）；开发期裁定 A6（硬编码色收口）/A7（按钮 26）/A8（零字形）/
//       A21（页签序 = 通用｜输出｜运行｜地图｜关于，输出在前与主步骤同序）。
//       review 台账：4-01/4-03/4-06（弹窗 chrome / 尺度 / 下划线页签）、4-08（语义控件）、
//       4-09/4-10（单位入槽 / 标点）、4-12（设置与运行页同名同词）、4-15/4-16（alpha 行三方一致）、
//       4-17（checkbox
//       行规格）、4-18/4-19（主按钮实心居右）、4-20（通用页签）、5-15（控件规格同源）。
//
// 落地口径（M1b §2.10 冻结契约，M5-T10 复核后**逐条保留**）：
//   ① 可编辑字段的状态就在子控件里，settings() 用 objectName 查找读回；构造快照存成
//      dynamic property（pp_orig_*），用于非确定路径（取消/关闭）回填与无控件字段回填
//      （last_format / last_preset / last_out_root —— §2.10 无这些控件，但 settings() 必须
//      返回完整结构，否则 MainWindow 保存会丢"上次会话"）。
//   ② settings() 仅在确定路径（accept → result()==Accepted）返回编辑值；取消/未 exec 返回构造快照。
//   ③ objectName 读回契约逐字保留：workers / budget_gb / flatten_gray / flatten_preview /
//      rotate_orientation / map_provider / amap_key / tile_cache_mb / log_level
//      + M4-T15 §9.3 三项 pp-stagger-ms / pp-thread-budget / pp-folder-structure。
//   ④ 行构造一律走 formkit（R1 pitch 28 / R2 标签轨 80 / R3 提示位 / R4 控件规格），本文件不手摆
//      栅格；弹窗 chrome（标题栏 40 / 下划线页签 / 底栏 48 / 焦点环 / QSS）走本文件的 dialogkit
//      （三弹窗共用一份实现，见 ui/settings_dialog.h）。
//   ⑤ 主题（通用页签，修 4-20 + A28/T11）：三档 = 跟随系统 / 亮色 / 暗色，接**现有**主题机制
//      theme::preferred_theme_mode() 的单源口径（PP_UI_THEME 强制档 > 持久化档 > 跟随系统）——
//      选择即时重放本对话框令牌 + 即时更新进程级持久化档；整窗重放归主窗 refresh_theme()
//      （open_settings() 在 accept 后重解析），跨进程落地 = AppSettings.ui_theme_mode（A28 新增键，
//      settings() 从段选中态写回、construct 时按同一映射回填）。
//   语言：仓库无 i18n 实现（无 .ts / QTranslator）→ 通用页签**不摆空行**（不放假下拉）。
#include "ui/settings_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPaintEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

#include "core/logger.h"
#include "core/version.h"
#include "platform/frameless.h" // 无边框接线（拖拽/八向缩放；与主窗同一机制）
#include "ui/formkit.h"
#include "ui/icons.h"
#include "ui/theme.h"

namespace pp::ui {
namespace {

QString T(const char *s) { return SettingsDialog::tr(s); }

// objectName（settings() 读回用；同时供 --ui-smoke / 走查定位）
// 注（M4-T15）：前 10 项的名称是 **M1b §2.10/§4.1 冻结契约**（docs/m1b-tasks.md:599
// 「可编辑字段读子控件 objectName（workers/budget_gb/…）」）→ 一字不改；M4-T15 追加的三项与
// M5-T10 新增控件按 W3 §9.3「pp-* 命名法」给钩子。
constexpr auto kWorkers = "workers";
constexpr auto kBudgetGb = "budget_gb";
constexpr auto kFlattenGray = "flatten_gray";
constexpr auto kFlattenPreview = "flatten_preview";
constexpr auto kRotateOrientation = "rotate_orientation";
constexpr auto kMapProvider = "map_provider";
constexpr auto kAmapKey = "amap_key";
constexpr auto kTileCacheMb = "tile_cache_mb";
constexpr auto kLogLevel = "log_level";
constexpr auto kLibraryVersions = "library_versions";
// —— M4-T15 §9.3 追加项（pp-* 钩子）——
constexpr auto kStaggerMs = "pp-stagger-ms";             // 交错启动 ms（0–2000）
constexpr auto kThreadBudget = "pp-thread-budget";       // 线程预算（0=自动）
constexpr auto kFolderStructure = "pp-folder-structure"; // 分文件夹默认结构（模板×开关）
// —— M5-T10 新增（pp-* 钩子）——
constexpr auto kThemeMode = "pp-theme-mode"; // 主题分段（通用页签）
// —— M6-T11 新增（pp-* 钩子；C6 加键的「地图」页签控件）——
constexpr auto kMapStyle = "pp-map-style";          // 图层（矢量/卫星/混合）
constexpr auto kMapSatLabels = "pp-map-sat-labels"; // 混合叠标注（布尔）
constexpr auto kPageGeneral = "pp-settings-page-general";
constexpr auto kPageOutput = "pp-settings-page-output";
constexpr auto kPageRun = "pp-settings-page-run";
constexpr auto kPageMap = "pp-settings-page-map";
constexpr auto kPageAbout = "pp-settings-page-about";
constexpr auto kRowTheme = "pp-settings-row-theme";
constexpr auto kRowAlpha = "pp-settings-row-alpha";
constexpr auto kLanguagePlaceholder = "pp-language"; // 不存在的"语言"假控件（探针证其不存在）

// 构造快照的 dynamic property 名
constexpr auto kOrigWorkers = "pp_orig_workers";
constexpr auto kOrigBudgetGb = "pp_orig_budget_gb";
constexpr auto kOrigFlattenGray = "pp_orig_flatten_gray";
constexpr auto kOrigRotate = "pp_orig_rotate_orientation";
constexpr auto kOrigMapProvider = "pp_orig_map_provider";
constexpr auto kOrigAmapKey = "pp_orig_amap_key";
constexpr auto kOrigTileCacheMb = "pp_orig_tile_cache_mb";
constexpr auto kOrigLogLevel = "pp_orig_log_level";
constexpr auto kOrigLastFormat = "pp_orig_last_format";
constexpr auto kOrigLastPreset = "pp_orig_last_preset";
constexpr auto kOrigLastOutRoot = "pp_orig_last_out_root";
constexpr auto kOrigStaggerMs = "pp_orig_stagger_ms";
constexpr auto kOrigThreadBudget = "pp_orig_thread_budget";
constexpr auto kOrigOutputTemplate = "pp_orig_output_template";
constexpr auto kOrigSplitByFormat = "pp_orig_split_by_format";
constexpr auto kOrigUiThemeMode = "pp_orig_ui_theme_mode";   // A28：主题档构造快照
constexpr auto kOrigMapStyle = "pp_orig_map_style";          // M6-T11：图层档构造快照
constexpr auto kOrigMapSatLabels = "pp_orig_map_sat_labels"; // M6-T11：混合叠标注构造快照

constexpr int kWorkersMax = 64;   // §2.10：QSpinBox(0..64) 同域
constexpr int kBudgetMax = 64;    // §2.10：QSpinBox(0..64)
constexpr int kTileCacheMin = 16; // §2.10：QSpinBox(16..512)
constexpr int kTileCacheMax = 512;
constexpr int kSliderMax = 100; // §2.10：QSlider(0..100) → flatten_gray = v/100
// M4-T15 §9.3：交错启动 ms 设置域 0–2000（0 = 关闭，§8.1）；线程预算 0 = 自动（逻辑核，§8.2）。
constexpr int kStaggerMax = 2000;
constexpr int kThreadBudgetMax = 64; // 与 workers 同档上限（§2.10 先例；0 = 自动）
// §9.1 U6-FIX：关于页版本清单可见行数钳制（对话框尺寸贴合内容）
constexpr int kVersionRowsMin = 3;
constexpr int kVersionRowsMax = 6;
constexpr int kVersionRowsPad = 8;
// alpha 合成底色预览块（tokens.css 对话框 alpha 行 width:30px;height:18px）
constexpr int kSwatchW = 30;
constexpr int kSwatchH = 18;
constexpr int kGrayMax = 255; // 灰度全域（修 4-15：滑杆 0–100 ↔ 灰度 0–255）

// §4.2「典型模板」三档 = 设置项「分文件夹默认结构」的可选项（模板串逐字取自设计 §4.2 联动段）；
// itemData(UserRole) = 模板串，itemData(UserRole+1) = split_by_format（= 模板含 $format 段，
// 与 ui/page_output.cpp 的 Impl::template_has_format 同一判据）。
constexpr const char *kStructureTemplates[] = {
    "$format/$dir/$file", // 开：按格式分文件夹
    "$dir/$file",         // 关：不分文件夹（平铺）
    "$dir/$format/$file", // 旧 MirrorFirst：镜像源目录（格式目录在源目录之下）
};

// §4.2 联动判据（GUI 单源 = ui/page_output.cpp 的 Impl::template_has_format）：任一"$format"段。
bool template_has_format(const QString &tmpl) {
    const QStringList segs = tmpl.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &seg : segs)
        if (seg == QLatin1String("$format"))
            return true;
    return false;
}

// 日志级别 5 档（§2.10 冻结；LogLevel 的第 6 档 critical 不出现，值非法 → 回退 info，
// 与 main.cpp 的 parse_log_level 一致）。itemData = 落盘值，itemText = 展示文案。
constexpr const char *kLevelIds[] = {"trace", "debug", "info", "warn", "error"};

// §4.2 联动开关值：split_by_format = 模板含 $format 段（GUI 判据单源 = page_output 同名函数）。
bool structure_split_flag(const QString &tmpl) { return template_has_format(tmpl); }

// —— A28：主题三档的档位 ↔ 档名单源映射（段序 = 跟随系统 / 亮色 / 暗色；与
//    AppSettings.ui_theme_mode 的词表逐字一致）——
constexpr const char *kThemeModeIds[] = {"system", "light", "dark"};

int theme_mode_index(const QString &id) {
    for (int i = 0; i < 3; ++i) {
        if (id == QLatin1String(kThemeModeIds[i]))
            return i;
    }
    return 0; // 词表外/空 → 跟随系统（与 core 的"值不合法 → 保留默认"同口径）
}

QString theme_mode_id(int index) { return QLatin1String(kThemeModeIds[std::clamp(index, 0, 2)]); }

// —— M6-T11（C6/§5.2）：图层三档的词表 ↔ 下拉项序单源映射（段序/项序 = 矢量 / 卫星 / 混合；
//    落盘值 = AppSettings.map_style 词表 road|satellite|hybrid，与 GPS 卡图层 Seg 的三态同序）——
constexpr const char *kMapStyleIds[] = {"road", "satellite", "hybrid"};

int map_style_index(const QString &id) {
    for (int i = 0; i < 3; ++i) {
        if (id == QLatin1String(kMapStyleIds[i]))
            return i;
    }
    return 0; // 词表外/空 → 矢量（与 core 的"值不合法 → 保留默认"同口径）
}

// —— 滑杆 ↔ 灰度（§7.4 alpha 行；修 4-15：旧实现把"滑杆值"直接当灰度用，最右端（白）
// 画成 #646464 深灰，滑杆 / 色块 / 端标三方矛盾）——
int gray_to_slider(double g) {
    const double clamped = std::clamp(g, 0.0, 1.0);
    return static_cast<int>(std::lround(clamped * kSliderMax));
}
int slider_to_gray(int v) {
    return static_cast<int>(
        std::lround(static_cast<double>(std::clamp(v, 0, kSliderMax)) * kGrayMax / kSliderMax));
}

// M2-T7 #28c 口径：首次 show 后把对话框居中到父窗（§7.4「660px 宽、居中」）。
class CenterOnShow : public QObject {
public:
    CenterOnShow(QObject *parent, std::function<void()> on_show)
        : QObject(parent), on_show_(std::move(on_show)) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Show && on_show_) {
            on_show_();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> on_show_;
};

pp::AppSettings original_values(const QObject *o) {
    pp::AppSettings s;
    s.workers = o->property(kOrigWorkers).toInt();
    s.budget_gb = o->property(kOrigBudgetGb).toInt();
    s.flatten_gray = o->property(kOrigFlattenGray).toDouble();
    s.log_level = o->property(kOrigLogLevel).toString().toStdString();
    s.map_provider = o->property(kOrigMapProvider).toString().toStdString();
    s.amap_key = o->property(kOrigAmapKey).toString().toStdString();
    s.tile_cache_mb = o->property(kOrigTileCacheMb).toInt();
    s.rotate_orientation = o->property(kOrigRotate).toBool();
    s.last_format = o->property(kOrigLastFormat).toString().toStdString();
    s.last_preset = o->property(kOrigLastPreset).toString().toStdString();
    s.last_out_root = o->property(kOrigLastOutRoot).toString().toStdString();
    // M4-T15 §9.3 追加项（交错启动 / 线程预算 / 分文件夹默认结构）
    s.stagger_ms = o->property(kOrigStaggerMs).toInt();
    s.thread_budget = o->property(kOrigThreadBudget).toInt();
    s.output_template = o->property(kOrigOutputTemplate).toString().toStdString();
    s.split_by_format = o->property(kOrigSplitByFormat).toBool();
    s.ui_theme_mode = o->property(kOrigUiThemeMode).toString().toStdString(); // A28
    // M6-T11（C6）：图层档 / 混合叠标注
    s.map_style = o->property(kOrigMapStyle).toString().toStdString();
    s.map_sat_labels = o->property(kOrigMapSatLabels).toBool();
    return s;
}

// —— 弹窗套件的局部实现件（dialogkit 的公开面见 ui/settings_dialog.h）——
namespace dk = dialogkit;

// 控件选择器钩子（与 page_output 的 ppField 词汇同源：input / combo / button / primary）
void hook_field(QWidget *w, const char *value) {
    if (w != nullptr)
        w->setProperty(dk::kFieldProperty, QString::fromLatin1(value));
}

// 单位槽 / 端标（16px 固定槽，§4.2 辅助 10px + --txt3）——与 formkit 单位槽同一属性/字号口径
// （行内辅助件不属栅格；formkit 未导出"任意行内槽"工厂，故按同一 ppFk=unit 契约复述）。
QLabel *inline_slot(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkUnit));
    label->setFont(theme::font(theme::Typography::hint_px));
    label->setAlignment(Qt::AlignCenter);
    label->setFixedWidth(formkit::kUnitSlotW);
    label->setWordWrap(false);
    return label;
}

// 自绘矢量图标件（标题栏 / 关于页）：颜色可换（主题重放），形 = icons 库（§3.3 禁字形）
class IconLabel : public QWidget {
public:
    IconLabel(icons::Name name, int px, QWidget *parent)
        : QWidget(parent), name_(name),
          color_(theme::tokens(theme::preferred_theme_mode()).accent) {
        setFixedSize(px, px);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
    void set_color(const QColor &color) {
        color_ = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        icons::paint(p, name_, QRectF(rect()), color_);
    }

private:
    icons::Name name_;
    QColor color_;
};

// caption 关闭钮（46×32；§3.3：透明底、悬停 #c42b1c 白字 —— 图标取色吃不到 QSS 的 :hover，
// 故悬停换色由本控件显式落地，与主窗 CaptionButton 同一口径）
class CaptionCloseButton : public QPushButton {
public:
    CaptionCloseButton(const QColor &normal, const QColor &hover, QWidget *parent = nullptr)
        : QPushButton(parent), normal_(normal), hover_(hover) {
        setText(QString());
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        apply_icon(false);
    }
    void set_colors(const QColor &normal, const QColor &hover) {
        normal_ = normal;
        hover_ = hover;
        apply_icon(underMouse());
    }

protected:
    void enterEvent(QEnterEvent *event) override {
        QPushButton::enterEvent(event);
        apply_icon(true);
    }
    void leaveEvent(QEvent *event) override {
        QPushButton::leaveEvent(event);
        apply_icon(false);
    }

private:
    void apply_icon(bool hovered) {
        icons::apply(this, icons::Name::CaptionClose, hovered ? hover_ : normal_, QColor(), 12);
    }
    QColor normal_;
    QColor hover_;
};

// alpha 合成底色预览块（§7.4：色块必须与滑杆值一致）。自绘 = 灰度即真值（无 QSS/调色板中间层）；
// ppGray 动态属性供探针读回（修 4-15 的取证点）。
class Swatch : public QWidget {
public:
    explicit Swatch(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName(QLatin1String(kFlattenPreview));
        setFixedSize(kSwatchW, kSwatchH);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        set_gray(kGrayMax);
    }
    void set_gray(int gray) {
        gray_ = std::clamp(gray, 0, kGrayMax);
        setProperty("ppGray", gray_);
        update();
    }
    int gray() const { return gray_; }
    void set_tokens(const theme::Tokens &t) {
        tokens_ = t;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(tokens_.control_bd, 1.0));
        p.setBrush(QColor(gray_, gray_, gray_));
        p.drawRoundedRect(box, theme::Metrics::control_radius, theme::Metrics::control_radius);
    }

private:
    int gray_ = kGrayMax;
    theme::Tokens tokens_ = theme::tokens(theme::preferred_theme_mode());
};

// 「自动/数值」下拉（修 4-08）：0 = 自动 独立成项 —— 不再把「自动（物理核数）」这类文本复合值
// 塞进 spinbox（↑↓ 行为不可预期）。itemData(0) = 0（自动），其余 = 数值本体。
QComboBox *make_auto_combo(QWidget *parent, const QString &object_name, int value, int max_value,
                           const QString &tooltip) {
    formkit::ComboSpec spec;
    spec.object_name = object_name;
    spec.accessible = tooltip; // §11：tooltip 即该下拉的语义名
    spec.tooltip = tooltip;
    QComboBox *combo = formkit::combo_box(parent, spec);
    combo->addItem(T("自动"), 0);
    for (int n = 1; n <= max_value; ++n)
        combo->addItem(QString::number(n), n);
    const int idx = combo->findData(std::clamp(value, 0, max_value));
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
    return combo;
}

// 数值 spinbox（formkit 单入口：R4 高 24 / 值区右对齐 tabular / 魔法值 = specialValueText）
QSpinBox *make_spin(QWidget *parent, const QString &object_name, int min, int max, int value,
                    bool magic, const QString &tooltip) {
    formkit::SpinSpec spec;
    spec.min = min;
    spec.max = max;
    spec.value = value;
    spec.auto_magic = magic;
    spec.object_name = object_name;
    auto *spin = formkit::spin_box(parent, spec, formkit::FieldWidth::Standard);
    if (!tooltip.isEmpty())
        spin->setToolTip(tooltip);
    return spin;
}

// 行落定（§11 无障碍）：标签伙伴关联（label.setBuddy）+ 控件名 = 标签文案；行宽/行序不变。
void bind_row(const formkit::Row &row, QWidget *field) {
    if (row.label != nullptr && field != nullptr) {
        row.label->setBuddy(field);
        field->setAccessibleName(row.label->text());
    }
}

} // namespace

// ---------------------------------------------------------------------------
// dialogkit 实现
// ---------------------------------------------------------------------------
namespace dialogkit {

QString dialog_qss(const theme::Tokens &t) {
    const auto css = [](const QColor &c) { return theme::css_color(c); };
    const QString field = QString::fromLatin1(kFieldProperty);
    const QString primary = QString::fromLatin1(kFieldPrimary);
    QString qss;
    // 弹窗卡片（§4.3 对话框 r6 + §4.1 card / card-bd）；标题栏"中性底" = --card-hi（§7.4）
    qss += QStringLiteral("QFrame#pp-dlg-card { background: ") + css(t.card) +
           QStringLiteral("; border: 1px solid ") + css(t.card_bd) +
           QStringLiteral("; "
                          "border-radius: ") +
           QString::number(theme::Metrics::dialog_radius) + QStringLiteral("px; }\n");
    qss +=
        QStringLiteral("QFrame#%1, QWidget#%1 { background: ").arg(QLatin1String(kTitleBarName)) +
        css(t.card_hi) + QStringLiteral("; border-bottom: 1px solid ") + css(t.card_bd) +
        QStringLiteral("; border-left: none; border-right: none; border-top: none; }\n");
    qss += QStringLiteral("QLabel#pp-dlg-title { color: ") + css(t.text) + QStringLiteral("; }\n");
    // 关闭 caption（46×32；透明底 + 悬停 #c42b1c —— §3.3 逐字）
    qss += QStringLiteral("QPushButton#pp-dlg-close { background: transparent; border: none; "
                          "padding: 0; }\n");
    qss += QStringLiteral("QPushButton#pp-dlg-close:hover { background: ") + css(t.close_hover) +
           QStringLiteral("; }\n");
    // 底栏（§7.4 48px；透明底 —— 露出卡片底）+ 正文页（透明）
    qss += QStringLiteral("QFrame#%1, QWidget#%1 { background: transparent; border: none; }\n")
               .arg(QLatin1String(kFooterName));
    qss += QStringLiteral("QStackedWidget#%1, QStackedWidget#%1 > QWidget { background: "
                          "transparent; }\n")
               .arg(QLatin1String(kPagesName));
    // UX-6⑥（M7-WP4）：输入/按钮/提示的跨页同形规则并入 theme::field_qss（原 11px padding
    // 等页内自拷值随之归一）；弹窗滚动条补 theme::scrollbar_qss —— UX-11「弹窗与主窗一套」。
    qss += theme::field_qss(t);
    qss += theme::scrollbar_qss(t);
    // 多行文本（关于页清单）：--ctl 底 + 等宽字（字族由 C++ 侧给）
    qss += QStringLiteral("QPlainTextEdit { background: ") + css(t.control) +
           QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: ") + QString::number(theme::Metrics::control_radius) +
           QStringLiteral("px; color: ") + css(t.text) + QStringLiteral("; }\n");
    // 表格 / 列表（EXIF 编辑器树、预设列表）：--ctl 底 + 选中 = --acc-dim
    qss += QStringLiteral("QTreeWidget, QListWidget { background: ") + css(t.control) +
           QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: ") + QString::number(theme::Metrics::control_radius) +
           QStringLiteral("px; color: ") + css(t.text) + QStringLiteral("; }\n");
    qss += QStringLiteral("QTreeWidget::item:selected, QListWidget::item:selected { "
                          "background: ") +
           css(t.accent_dim) + QStringLiteral("; color: ") + css(t.text) + QStringLiteral("; }\n");
    qss += QStringLiteral("QHeaderView::section { background: ") + css(t.card_hi) +
           QStringLiteral("; color: ") + css(t.text3) +
           QStringLiteral("; border: none; border-bottom: 1px solid ") + css(t.card_bd) +
           QStringLiteral("; padding: 4px 8px; }\n");
    // 页签（下划线式；EXIF 编辑器的 QTabWidget 与设置弹窗自绘页签同一视觉口径 —— 修 4-06）
    qss += QStringLiteral("QTabWidget::pane { border: none; border-top: 1px solid ") + css(t.line) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QTabBar::tab { background: transparent; color: ") + css(t.text2) +
           QStringLiteral("; padding: 5px 12px 6px; margin-right: 2px; border: none; "
                          "border-bottom: ") +
           QString::number(kTabUnderlinePx) +
           QStringLiteral("px solid transparent; font-weight: "
                          "600; }\n");
    qss += QStringLiteral("QTabBar::tab:selected { color: ") + css(t.text) +
           QStringLiteral("; border-bottom-color: ") + css(t.accent) + QStringLiteral("; }\n");
    qss += QStringLiteral("QTabBar::tab:hover:!selected { color: ") + css(t.text) +
           QStringLiteral("; }\n");
    // 滑杆（§7.4 alpha 行：4px 轨 + 13px 柄，柄 = 中性底 + 2px accent 环）
    qss += QStringLiteral("QSlider::groove:horizontal { height: ") +
           QString::number(theme::Metrics::slider_track_px) +
           QStringLiteral("px; border: 1px "
                          "solid ") +
           css(t.control_bd) + QStringLiteral("; border-radius: 99px; background: ") +
           css(t.control) + QStringLiteral("; }\n");
    qss += QStringLiteral("QSlider::sub-page:horizontal, QSlider::add-page:horizontal { "
                          "background: transparent; }\n");
    qss += QStringLiteral("QSlider::handle:horizontal { width: ") +
           QString::number(theme::Metrics::slider_handle_px) + QStringLiteral("px; height: ") +
           QString::number(theme::Metrics::slider_handle_px) +
           QStringLiteral("px; margin: -4px 0; border-radius: 99px; background: ") +
           css(theme::composite(t.card, t.window)) + QStringLiteral("; border: 2px solid ") +
           css(t.accent) + QStringLiteral("; }\n");
    // §11「焦点环自绘（accent 1px，禁用系统虚线框）」：系统焦点框一律关掉 —— 可见的焦点指示
    // 只有 dialogkit::FocusRing 一处（accent 1px 自绘环）。
    qss += QStringLiteral("QPushButton, QToolButton, QCheckBox, QRadioButton, QLineEdit, "
                          "QComboBox, QAbstractSpinBox, QSlider, QAbstractItemView, QGroupBox, "
                          "QTabBar, QPlainTextEdit, QListView, QTreeView { outline: none; }\n");
    // formkit 行内辅助件（标签/值/单位/注释/勾选/spin）与 .mod 改动值：弹窗是独立顶层窗口，
    // 主窗的 style_sheet 不覆盖其子树 → 这两条必须在弹窗自己的作用域里（M4-T12 同口径）。
    qss += theme::formkit_qss(t);
    qss += theme::mod_qss(t);
    return qss;
}

TitleBar make_title_bar(QWidget *parent, icons::Name icon, const QString &title) {
    const theme::Tokens t = theme::tokens(theme::preferred_theme_mode());
    TitleBar out;
    auto *bar = new QFrame(parent);
    bar->setObjectName(QLatin1String(kTitleBarName));
    bar->setFixedHeight(kTitleBarH);
    auto *line = new QHBoxLayout(bar);
    line->setContentsMargins(kTitlePadLeft, 0, 4, 0);
    line->setSpacing(theme::Metrics::brand_gap); // tokens.css .dlg-tb{gap:8px}
    auto *icon_label = new IconLabel(icon, kTitleIconPx, bar);
    icon_label->setObjectName(QStringLiteral("pp-dlg-title-icon"));
    line->addWidget(icon_label, 0, Qt::AlignVCenter);
    auto *text = new QLabel(title, bar);
    text->setObjectName(QStringLiteral("pp-dlg-title"));
    text->setFont(theme::font(theme::Typography::card_title_px, QFont::Bold)); // §4.2 卡题 12/700
    text->setWordWrap(false);
    line->addWidget(text, 0, Qt::AlignVCenter);
    line->addStretch(1);
    auto *close = new CaptionCloseButton(t.text, t.close_hover_fg, bar);
    close->setObjectName(QStringLiteral("pp-dlg-close"));
    close->setToolTip(T("关闭"));
    close->setAccessibleName(T("关闭")); // §11：图标钮必有 accessibleName + tooltip
    close->setFixedSize(theme::Metrics::caption_btn_w, theme::Metrics::caption_btn_h);
    line->addWidget(close, 0, Qt::AlignVCenter);
    out.widget = bar;
    out.title = text;
    out.close = close;
    return out;
}

void restyle_title_bar(const TitleBar &bar, const theme::Tokens &t) {
    if (bar.widget == nullptr)
        return;
    if (auto *icon = bar.widget->findChild<QWidget *>(QStringLiteral("pp-dlg-title-icon"))) {
        if (auto *label = dynamic_cast<IconLabel *>(icon))
            label->set_color(t.accent);
    }
    if (auto *close = dynamic_cast<CaptionCloseButton *>(bar.close))
        close->set_colors(t.text, t.close_hover_fg);
}

// caption ✕ → 关闭对话框（GUI-R2 修 3）。只接一次；重复调用是幂等的（Qt 的 connect 会重复
// 触发槽，故用 objectName 标记避开二次接线）。
void bind_close_button(const TitleBar &bar, QDialog *dialog) {
    if (bar.close == nullptr || dialog == nullptr)
        return;
    if (bar.close->property("ppCloseBound").toBool())
        return;
    bar.close->setProperty("ppCloseBound", true);
    QObject::connect(bar.close, &QPushButton::clicked, dialog, &QDialog::reject);
}

// —— 下划线页签 ——
class UnderlineTabs::Tab : public QAbstractButton {
public:
    Tab(UnderlineTabs *owner, const QString &text) : QAbstractButton(owner), owner_(owner) {
        setText(text);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(text); // §11：页签可读
        setToolTip(text);
        setFont(theme::font(theme::Typography::body_px, QFont::DemiBold));
        // 固定宽（文字 + 左右 13px）→ 选中/悬停零宽度变化（§12-4）
        setFixedSize(fontMetrics().horizontalAdvance(text) + 2 * kTabPadX, kTabH);
    }
    void set_tokens(const theme::Tokens &t) {
        tokens_ = t;
        update();
    }

protected:
    void nextCheckState() override {} // 勾选态由 UnderlineTabs 统一持有

    void keyPressEvent(QKeyEvent *event) override {
        switch (event->key()) {
        case Qt::Key_Right:
        case Qt::Key_Down:
            owner_->step(+1);
            event->accept();
            return;
        case Qt::Key_Left:
        case Qt::Key_Up:
            owner_->step(-1);
            event->accept();
            return;
        default:
            break;
        }
        QAbstractButton::keyPressEvent(event);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        const bool on = isChecked();
        if (on) { // 选中段下划线 2px（tokens.css .dlg-tab.on{border-bottom-color:--acc}）
            p.setPen(Qt::NoPen);
            p.setBrush(tokens_.accent);
            p.drawRect(QRectF(0, height() - kTabUnderlinePx, width(), kTabUnderlinePx));
        }
        p.setFont(theme::font(theme::Typography::body_px, QFont::DemiBold));
        p.setPen(on || underMouse() ? tokens_.text : tokens_.text2);
        p.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    UnderlineTabs *owner_;
    theme::Tokens tokens_ = theme::tokens(theme::preferred_theme_mode());
};

UnderlineTabs::UnderlineTabs(QWidget *parent) : QWidget(parent) {
    setObjectName(QLatin1String(kTabsName));
    setFixedHeight(kTabsBarH);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *line = new QHBoxLayout(this);
    line->setContentsMargins(kTabBarPadX, 0, kTabBarPadX, kTabUnderlinePx);
    line->setSpacing(0);
}

int UnderlineTabs::add_tab(const QString &text, const QString &object_name) {
    auto *tab = new Tab(this, text);
    if (!object_name.isEmpty())
        tab->setObjectName(object_name);
    tab->set_tokens(tokens_);
    static_cast<QHBoxLayout *>(layout())->addWidget(tab, 0, Qt::AlignBottom);
    tabs_.append(tab);
    const int index = int(tabs_.size()) - 1;
    connect(tab, &QAbstractButton::clicked, this, [this, index] { activate(index); });
    if (current_ < 0)
        set_current_index(0); // 首标签默认选中（互斥必有当前值）
    return index;
}

int UnderlineTabs::count() const { return int(tabs_.size()); }

int UnderlineTabs::current_index() const { return current_; }

QStringList UnderlineTabs::texts() const {
    QStringList out;
    for (const Tab *tab : tabs_)
        out << tab->text();
    return out;
}

QWidget *UnderlineTabs::tab_at(int index) const { return tab_at_index(index); }

UnderlineTabs::Tab *UnderlineTabs::tab_at_index(int index) const {
    if (index < 0 || index >= tabs_.size())
        return nullptr;
    return tabs_.at(index);
}

void UnderlineTabs::set_current_index(int index) {
    if (tab_at_index(index) == nullptr)
        return;
    current_ = index;
    for (int i = 0; i < tabs_.size(); ++i)
        tabs_.at(i)->setChecked(i == index);
    update();
}

void UnderlineTabs::activate(int index) {
    if (index == current_)
        return;
    set_current_index(index);
    emit current_changed(index);
}

void UnderlineTabs::step(int delta) {
    if (tabs_.isEmpty())
        return;
    const int next = (current_ + delta + tabs_.size()) % tabs_.size();
    activate(next);
    if (Tab *tab = tab_at_index(next))
        tab->setFocus(Qt::OtherFocusReason);
}

void UnderlineTabs::set_tokens(const theme::Tokens &t) {
    tokens_ = t;
    for (Tab *tab : tabs_)
        tab->set_tokens(t);
    update();
}

QSize UnderlineTabs::sizeHint() const { return QSize(0, kTabsBarH); }

void UnderlineTabs::paintEvent(QPaintEvent *) {
    // 通栏 hairline 基线（tokens.css .dlg-tabs{border-bottom:1px solid --card-bd}）
    QPainter p(this);
    p.setPen(QPen(tokens_.card_bd, 1.0));
    p.drawLine(0, height() - 1, width(), height() - 1);
}

// —— 焦点环 ——
FocusRing::FocusRing(QWidget *host) : QWidget(host), host_(host) {
    setObjectName(QLatin1String(kFocusRingName));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
    tokens_ = theme::tokens(theme::preferred_theme_mode());
    hide();
    if (host_ != nullptr) {
        host_->installEventFilter(this);
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *) { sync(); });
    }
}

void FocusRing::set_tokens(const theme::Tokens &t) {
    tokens_ = t;
    update();
}

QRect FocusRing::target_rect() const {
    if (target_ == nullptr || host_ == nullptr)
        return QRect();
    return QRect(target_->mapTo(host_, QPoint(0, 0)), target_->size());
}

bool FocusRing::eventFilter(QObject *watched, QEvent *event) {
    if (watched == host_) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Move:
        case QEvent::LayoutRequest:
        case QEvent::ChildAdded:
            sync();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

void FocusRing::sync() {
    QWidget *focused = QApplication::focusWidget();
    if (host_ == nullptr || focused == nullptr || focused == host_ ||
        focused->window() != host_->window() || !host_->isAncestorOf(focused) ||
        !focused->isVisible()) {
        target_ = nullptr;
        hide();
        return;
    }
    target_ = focused;
    const int pad = kFocusRingPad;
    setGeometry(
        QRect(target_->mapTo(host_, QPoint(0, 0)), target_->size()).adjusted(-pad, -pad, pad, pad));
    show();
    raise();
}

void FocusRing::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(tokens_.accent, kFocusRingPx)); // §11：accent 描边 1px
    p.setBrush(Qt::NoBrush);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.drawRoundedRect(box, theme::Metrics::control_radius + 2, theme::Metrics::control_radius + 2);
}

QPushButton *make_button(QWidget *parent, const QString &text, bool primary) {
    // R-022（M7-WP4）：三份 make_button 收敛 formkit::make_button（本处只剩类型收敛转发）
    return static_cast<QPushButton *>(formkit::make_button(parent, text, primary));
}

// —— C7 几何探针（值表驱动；§7.4 逐条 + A21 页签序）——
GeomProbeReport settings_geom_probe(const QDialog &dialog) {
    GeomProbeReport out;
    const theme::Tokens t = theme::tokens(theme::preferred_theme_mode());
    const auto *titlebar = dialog.findChild<QWidget *>(QLatin1String(kTitleBarName));
    const auto *tabs = dialog.findChild<UnderlineTabs *>(QLatin1String(kTabsName));
    const auto *footer = dialog.findChild<QWidget *>(QLatin1String(kFooterName));
    const auto *ok = dialog.findChild<QPushButton *>(QLatin1String(kOkName));
    const auto *cancel = dialog.findChild<QPushButton *>(QLatin1String(kCancelName));
    const auto *defaults = dialog.findChild<QPushButton *>(QLatin1String(kDefaultsName));
    const auto *theme_row = dialog.findChild<QWidget *>(QLatin1String(kRowTheme));
    const auto *workers = dialog.findChild<QComboBox *>(QLatin1String(kWorkers));
    const auto *budget = dialog.findChild<QSpinBox *>(QLatin1String(kBudgetGb));
    const auto *stagger = dialog.findChild<QSpinBox *>(QLatin1String(kStaggerMs));
    const auto *threads = dialog.findChild<QComboBox *>(QLatin1String(kThreadBudget));
    auto *slider = dialog.findChild<QSlider *>(QLatin1String(kFlattenGray)); // 探针会两端取值
    const auto *swatch = dialog.findChild<QWidget *>(QLatin1String(kFlattenPreview));
    const auto *page_general = dialog.findChild<QWidget *>(QLatin1String(kPageGeneral));
    const auto *page_run = dialog.findChild<QWidget *>(QLatin1String(kPageRun));
    const auto *mode = dialog.findChild<formkit::SegmentedControl *>(QLatin1String(kThemeMode));

    struct Expect {
        const char *name;
        int got;
        int want;
    };
    // 值表（§7.4 逐字 + A21 页签序 + §5 R1/R2/R4）
    const Expect table[] = {
        {"width", dialog.width(), kDialogWidth},
        {"titlebar_h", titlebar != nullptr ? titlebar->height() : -1, kTitleBarH},
        {"tabs_h", tabs != nullptr ? tabs->height() : -1, kTabsBarH},
        {"tab_count", tabs != nullptr ? tabs->count() : -1, 5},
        {"footer_h", footer != nullptr ? footer->height() : -1, kFooterH},
        // R1：行 pitch 28（当前页实测 + 行容器固定高）
        {"row_pitch", theme_row != nullptr ? theme_row->height() : -1, formkit::kRowPitch},
        {"row_pitch_fixed",
         theme_row != nullptr && theme_row->minimumHeight() == formkit::kRowPitch &&
                 theme_row->maximumHeight() == formkit::kRowPitch
             ? formkit::kRowPitch
             : -1,
         formkit::kRowPitch},
        // R2：字段宽标准档 160（并行 / 内存预算 / 交错启动 / 线程预算同宽同列）
        {"field_w_workers", workers != nullptr ? workers->width() : -1, formkit::kFieldStd},
        {"field_w_budget", budget != nullptr ? budget->width() : -1, formkit::kFieldStd},
        {"field_w_stagger", stagger != nullptr ? stagger->width() : -1, formkit::kFieldStd},
        {"field_w_threads", threads != nullptr ? threads->width() : -1, formkit::kFieldStd},
        // 底栏主次：确定 = 主按钮（实心）且居右（§7.4 / 修 4-18/4-19）
        {"ok_primary",
         ok != nullptr && ok->property(kFieldProperty).toString() == QLatin1String(kFieldPrimary)
             ? 1
             : 0,
         1},
        {"ok_right_of_cancel",
         ok != nullptr && cancel != nullptr &&
                 ok->mapTo(&dialog, QPoint(0, 0)).x() > cancel->mapTo(&dialog, QPoint(0, 0)).x()
             ? 1
             : 0,
         1},
        {"defaults_left_of_cancel",
         defaults != nullptr && cancel != nullptr &&
                 defaults->mapTo(&dialog, QPoint(0, 0)).x() <
                     cancel->mapTo(&dialog, QPoint(0, 0)).x()
             ? 1
             : 0,
         1},
        // 主按钮实心 = QSS 规则存在且底色 = 令牌 accent（A6：色值单源）
        {"ok_filled_qss",
         dialog.styleSheet().contains(QStringLiteral("QPushButton[%1=\"%2\"] { background: %3")
                                          .arg(QLatin1String(kFieldProperty),
                                               QLatin1String(kFieldPrimary),
                                               theme::css_color(t.accent)))
             ? 1
             : 0,
         1},
        // §11：焦点环存在 + 系统虚线框关掉（outline: none）
        {"focus_ring",
         dialog.findChild<QWidget *>(QLatin1String(kFocusRingName)) != nullptr ? 1 : 0, 1},
        {"focus_outline_off", dialog.styleSheet().contains(QStringLiteral("outline: none")) ? 1 : 0,
         1},
        // 通用页签（修 4-20）：主题三档 + **不摆空行**（无"语言"假控件）
        {"theme_segments", mode != nullptr ? mode->count() : -1, 3},
        // 段几何（R4 段宽 88 等宽 / 段高 = 24 − 容器上下各 1px 描边）＋ 段组贴字段轨起点（R2）
        {"theme_seg_w",
         mode != nullptr && mode->segment(0) != nullptr ? mode->segment(0)->width() : -1,
         formkit::kSegW},
        // A27（T11）：段高自持 = formkit_layout.h 的 seg_inner_h()（kSegH − 2×描边 = 22）——
        // 期望值由「kSegH − 2」换锚成具名单源函数（语义不变，值同为 22）
        {"theme_seg_h",
         mode != nullptr && mode->segment(0) != nullptr ? mode->segment(0)->height() : -1,
         formkit::seg_inner_h()},
        // A27：三档段**全部可见**（段文字/选中填充可见的前提）+ 容器高 = R4 24
        {"theme_seg_visible",
         mode != nullptr && mode->count() == 3 && mode->segment(0) != nullptr &&
                 mode->segment(1) != nullptr && mode->segment(2) != nullptr &&
                 mode->segment(0)->isVisible() && mode->segment(1)->isVisible() &&
                 mode->segment(2)->isVisible() && mode->height() == formkit::kSegH
             ? 1
             : 0,
         1},
        // 段组贴字段轨起点（R2：页内 [标签 80][列距 12][轨]，无浮空）
        {"theme_field_x",
         mode != nullptr && page_general != nullptr ? mode->mapTo(page_general, QPoint(0, 0)).x()
                                                    : -1,
         kBodyPadX + formkit::field_track_x()},
        {"no_language_placeholder",
         dialog.findChild<QWidget *>(QLatin1String(kLanguagePlaceholder)) == nullptr ? 1 : 0, 1},
        // 运行页签语义控件（修 4-08）：「自动」= 下拉独立项（0），不是 spinbox 文本魔法值
        {"auto_control_kind",
         workers != nullptr && workers->count() > 1 && workers->itemData(0).toInt() == 0 &&
                 workers->itemText(0) == T("自动") && threads != nullptr && threads->count() > 1 &&
                 threads->itemData(0).toInt() == 0 && threads->itemText(0) == T("自动")
             ? 1
             : 0,
         1},
        // §7.4「数值=spinbox（魔法值「自动」+ 旁注「0 = 自动」）」仍在内存预算一行成立
        {"budget_magic", budget != nullptr && budget->specialValueText() == T("自动") ? 1 : 0, 1},
    };
    out.checked = int(sizeof(table) / sizeof(table[0]));
    for (const Expect &e : table) {
        if (e.got != e.want)
            out.bad << QStringLiteral("%1=%2(期望 %3)")
                           .arg(QString::fromLatin1(e.name))
                           .arg(e.got)
                           .arg(e.want);
    }
    if (mode != nullptr) {
        if (QWidget *first = mode->segment(0)) {
            first->setFocus(Qt::OtherFocusReason);
            QApplication::processEvents();
            auto *ring = dynamic_cast<FocusRing *>(
                dialog.findChild<QWidget *>(QLatin1String(kFocusRingName)));
            out.checked += 3;
            if (ring == nullptr || ring->target() != first)
                out.bad << QStringLiteral("focus_ring_target=wrong");
            else if (ring->geometry() != ring->target_rect().adjusted(-kFocusRingPad,
                                                                      -kFocusRingPad, kFocusRingPad,
                                                                      kFocusRingPad))
                out.bad << QStringLiteral("focus_ring_geom=wrong");
            else if (ring->pen_color() != t.accent)
                out.bad << QStringLiteral("focus_ring_color=%1(期望 %2)")
                               .arg(theme::css_color(ring->pen_color()),
                                    theme::css_color(t.accent));
        }
    }
    // —— 单位入 16px 槽 / 范围入 10px 注释（R3）——
    if (stagger != nullptr && stagger->parentWidget() != nullptr) {
        const QList<QLabel *> labels = stagger->parentWidget()->findChildren<QLabel *>();
        const QLabel *unit = nullptr;
        const QLabel *note = nullptr;
        for (QLabel *l : labels) {
            const QString hook = l->property(theme::kFkProperty).toString();
            if (hook == QLatin1String(theme::kFkUnit))
                unit = l;
            else if (hook == QLatin1String(theme::kFkNote))
                note = l;
        }
        out.checked += 3;
        if (unit == nullptr || unit->width() != formkit::kUnitSlotW || unit->text() != T("ms"))
            out.bad << QStringLiteral("unit_slot=%1(期望 %2/%3)")
                           .arg(unit != nullptr ? unit->width() : -1)
                           .arg(formkit::kUnitSlotW)
                           .arg(T("ms"));
        if (note == nullptr || note->text() != T("0–2000"))
            out.bad << QStringLiteral("range_note=%1(期望 %2)")
                           .arg(note != nullptr ? note->text() : QStringLiteral("<无>"),
                                T("0–2000"));
    } else {
        out.bad << QStringLiteral("stagger_row=missing");
    }
    // —— W-R-T13（A39 ①）：线程预算与运行页**同词**（review 4-12 全局措辞统一）——
    //   settings 侧 = 自动档项文本「自动」（值表 auto_control_kind 已钉）+ 行注逐字「0 = 自动」；
    //   运行页 kv 值 = 「自动（N 核）」（run-overview 探针按同一措辞核对）。两表面共词 = 「自动」；
    //   旧措辞「自适应」在两处都不得再出现。
    if (threads != nullptr && threads->parentWidget() != nullptr) {
        QString note_text;
        for (QLabel *l : threads->parentWidget()->findChildren<QLabel *>()) {
            if (l->property(theme::kFkProperty).toString() == QLatin1String(theme::kFkNote))
                note_text = l->text();
        }
        out.checked += 3;
        if (threads->itemText(0) != T("自动"))
            out.bad << QStringLiteral("thread_budget_word=%1(期望 %2)")
                           .arg(threads->itemText(0), T("自动"));
        if (note_text != T("0 = 自动"))
            out.bad << QStringLiteral("thread_budget_note=%1(期望 %2)")
                           .arg(note_text.isEmpty() ? QStringLiteral("<无>") : note_text,
                                T("0 = 自动"));
        if (threads->itemText(0).contains(T("自适应")) || note_text.contains(T("自适应")))
            out.bad << QStringLiteral("thread_budget_stale_wording");
    } else {
        out.bad << QStringLiteral("thread_budget_row=missing");
    }
    // —— 页签序逐字（A21：通用｜输出｜运行｜地图｜关于）——
    const QStringList want_tabs{T("通用"), T("输出"), T("运行"), T("地图"), T("关于")};
    out.checked += 1;
    if (tabs == nullptr || tabs->texts() != want_tabs) {
        out.bad << QStringLiteral("tab_order=%1(期望 %2)")
                       .arg(tabs != nullptr ? tabs->texts().join(QLatin1Char('/'))
                                            : QStringLiteral("<无页签条>"),
                            want_tabs.join(QLatin1Char('/')));
    }
    // —— alpha 行三方一致（修 4-15/4-16：滑杆值 / 预览色块 / 端标同源同侧）——
    if (slider != nullptr && swatch != nullptr && page_run != nullptr) {
        const int keep = slider->value();
        const QLabel *black = nullptr;
        const QLabel *white = nullptr;
        for (QLabel *l : page_run->findChildren<QLabel *>()) {
            if (l->text() == T("黑"))
                black = l;
            else if (l->text() == T("白"))
                white = l;
        }
        const auto swatch_gray = [swatch] { return swatch->property("ppGray").toInt(); };
        out.checked += 4;
        if (swatch_gray() != slider_to_gray(keep))
            out.bad << QStringLiteral("alpha_start=%1(期望 %2)")
                           .arg(swatch_gray())
                           .arg(slider_to_gray(keep));
        slider->setValue(kSliderMax); // 最右端 = 白（修 4-15 的取证点）
        const int at_right = swatch_gray();
        slider->setValue(0); // 最左端 = 黑
        const int at_left = swatch_gray();
        slider->setValue(keep); // 还原（探针不改变被检对象的终态）
        if (at_right != kGrayMax)
            out.bad << QStringLiteral("alpha_white_end=%1(期望 %2)").arg(at_right).arg(kGrayMax);
        if (at_left != 0)
            out.bad << QStringLiteral("alpha_black_end=%1(期望 0)").arg(at_left);
        if (black == nullptr || white == nullptr) {
            out.bad << QStringLiteral("alpha_end_labels=missing");
        } else if (black->mapTo(&dialog, QPoint(0, 0)).x() >=
                       slider->mapTo(&dialog, QPoint(0, 0)).x() ||
                   white->mapTo(&dialog, QPoint(0, 0)).x() <=
                       slider->mapTo(&dialog, QPoint(0, 0)).x()) {
            out.bad << QStringLiteral("alpha_end_labels=misplaced"); // 端标须贴滑杆两端
        }
    } else {
        out.bad << QStringLiteral("alpha_row=missing");
    }
    // —— 焦点环跟随（§11）：环位 = 焦点控件盒外 2px，描边色 = 令牌 accent ——
    if (mode != nullptr && page_general != nullptr)
        (void)page_general;
    if (mode != nullptr) {
        if (QWidget *first = mode->segment(0)) {
            first->setFocus(Qt::OtherFocusReason);
            QApplication::processEvents();
            auto *ring = dynamic_cast<FocusRing *>(
                dialog.findChild<QWidget *>(QLatin1String(kFocusRingName)));
            out.checked += 3;
            if (ring == nullptr || ring->target() != first)
                out.bad << QStringLiteral("focus_ring_target=wrong");
            else if (ring->geometry() != ring->target_rect().adjusted(-kFocusRingPad,
                                                                      -kFocusRingPad, kFocusRingPad,
                                                                      kFocusRingPad))
                out.bad << QStringLiteral("focus_ring_geom=wrong");
            else if (ring->pen_color() != t.accent)
                out.bad << QStringLiteral("focus_ring_color=%1(期望 %2)")
                               .arg(theme::css_color(ring->pen_color()),
                                    theme::css_color(t.accent));
        }
    }
    return out;
}

} // namespace dialogkit

// ===========================================================================
// SettingsDialog
// ===========================================================================

SettingsDialog::SettingsDialog(const pp::AppSettings &current, QWidget *parent) : QDialog(parent) {
    setWindowTitle(T("设置"));

    // 构造快照（含无对应控件的字段：last_*）
    setProperty(kOrigWorkers, current.workers);
    setProperty(kOrigBudgetGb, current.budget_gb);
    setProperty(kOrigFlattenGray, current.flatten_gray);
    setProperty(kOrigRotate, current.rotate_orientation);
    setProperty(kOrigMapProvider, QString::fromStdString(current.map_provider));
    setProperty(kOrigAmapKey, QString::fromStdString(current.amap_key));
    setProperty(kOrigTileCacheMb, current.tile_cache_mb);
    setProperty(kOrigLogLevel, QString::fromStdString(current.log_level));
    setProperty(kOrigLastFormat, QString::fromStdString(current.last_format));
    setProperty(kOrigLastPreset, QString::fromStdString(current.last_preset));
    setProperty(kOrigLastOutRoot, QString::fromStdString(current.last_out_root));
    // M4-T15 §9.3 追加项的快照（非确定路径回填 + settings() 完整结构）
    setProperty(kOrigStaggerMs, current.stagger_ms);
    setProperty(kOrigThreadBudget, current.thread_budget);
    setProperty(kOrigOutputTemplate, QString::fromStdString(current.output_template));
    setProperty(kOrigSplitByFormat, current.split_by_format);
    setProperty(kOrigUiThemeMode, QString::fromStdString(current.ui_theme_mode)); // A28
    // M6-T11（C6）：图层档 / 混合叠标注的快照（非确定路径回填 + settings() 完整结构）
    setProperty(kOrigMapStyle, QString::fromStdString(current.map_style));
    setProperty(kOrigMapSatLabels, current.map_sat_labels);

    theme::Tokens tokens = theme::tokens(theme::preferred_theme_mode());
    setFixedWidth(dk::kDialogWidth); // §7.4：660px 宽（居中见首次 show）
    setStyleSheet(dk::dialog_qss(tokens));

    // 主题重放需要的自绘件指针（通用页签的主题段在本构造末尾接线，见 "主题切换"）
    formkit::SegmentedControl *mode = nullptr;
    formkit::CheckBox *rotate_box = nullptr;     // 占位（check_row 的勾选框）
    formkit::CheckBox *sat_labels_box = nullptr; // M6-T11：混合叠标注（check_row 的勾选框）
    Swatch *swatch = nullptr;

    // ---- 卡片骨架（§7.4：自绘标题栏 40 + 下划线页签 + 正文 + 底栏 48）----
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("pp-dlg-card"));
    auto *card_line = new QVBoxLayout(card);
    card_line->setContentsMargins(0, 0, 0, 0);
    card_line->setSpacing(0);

    const dk::TitleBar title_bar = dk::make_title_bar(card, icons::Name::Image, T("设置"));
    card_line->addWidget(title_bar.widget);

    auto *tabs = new dk::UnderlineTabs(card);
    auto *pages = new QStackedWidget(card);
    pages->setObjectName(QLatin1String(dk::kPagesName));
    card_line->addWidget(tabs);
    card_line->addWidget(pages, 1);

    auto *footer = new QFrame(card);
    footer->setObjectName(QLatin1String(dk::kFooterName));
    footer->setFixedHeight(dk::kFooterH);
    auto *footer_line = new QHBoxLayout(footer);
    footer_line->setContentsMargins(dk::kFooterPadX, 0, dk::kFooterPadX, 0);
    footer_line->setSpacing(theme::Metrics::group_gap); // 8（tokens.css .dlg-ft{gap:8px}）

    // 页签序 = A21（输出在运行之前，与主步骤 元数据→输出→运行 同序）
    tabs->add_tab(T("通用"), QStringLiteral("pp-tab-general"));
    tabs->add_tab(T("输出"), QStringLiteral("pp-tab-output"));
    tabs->add_tab(T("运行"), QStringLiteral("pp-tab-run"));
    tabs->add_tab(T("地图"), QStringLiteral("pp-tab-map"));
    tabs->add_tab(T("关于"), QStringLiteral("pp-tab-about"));

    // ------------------------------------------------------------------
    // 页 0：通用（修 4-20：主题收纳于此；语言 = 无 i18n 实现 → 不摆空行）
    // ------------------------------------------------------------------
    auto *general = new QWidget(pages);
    general->setObjectName(QLatin1String(kPageGeneral));
    {
        auto *v = new QVBoxLayout(general);
        v->setContentsMargins(dk::kBodyPadX, dk::kBodyPadTop, dk::kBodyPadX, dk::kBodyPadBottom);
        v->setSpacing(0); // R1：行间无额外 gap

        mode = new formkit::SegmentedControl(general);
        mode->setObjectName(QLatin1String(kThemeMode));
        mode->setToolTip(T("应用主题；跟随系统 = 随 Windows 明暗设置（选后立即生效）"));
        mode->setAccessibleName(T("主题"));
        mode->add_segment(T("跟随系统"), QStringLiteral("pp-theme-system"));
        mode->add_segment(T("亮色"), QStringLiteral("pp-theme-light"));
        mode->add_segment(T("暗色"), QStringLiteral("pp-theme-dark"));
        for (int i = 0; i < mode->count(); ++i) {
            auto *segment = qobject_cast<QAbstractButton *>(mode->segment(i));
            if (segment != nullptr) {
                segment->setAccessibleName(T("主题") + QStringLiteral("：") + segment->text());
                segment->setToolTip(segment->text());
            }
        }
        // 当前档 = 单源口径（A28）：PP_UI_THEME 强制档 > 持久化档（AppSettings.ui_theme_mode，
        // 启动期已注入 theme.h）> 跟随系统 —— 与 theme::preferred_theme_mode() 同一判据顺序，
        // 只是保留"跟随系统"这一档（theme_mode_id() 给原始档名）。
        mode->set_current_index(theme_mode_index(QString::fromLatin1(theme::theme_mode_id())));
        mode->set_tokens(tokens);

        // A27（T11）：段高由 formkit::Segment **自持**（sizeHint/fixedHeight = kSegH − 2）——
        // 调用点临时补丁（setFixedHeight）已撤销；此处不再对段做任何几何修补。
        // 分段控件本体宽 = 3×88 + 2（R4 段宽 88 等宽）→ 轨用长档；容器**不**定宽（否则会被
        // 轨居中），内部段组贴左缘（R2：字段轨起点 x = 80 + 12，与其它行同轴）
        auto *mode_holder = new QWidget(general);
        auto *mh = new QHBoxLayout(mode_holder);
        mh->setContentsMargins(0, 0, 0, 0);
        mh->setSpacing(0);
        mh->addWidget(mode, 0, Qt::AlignVCenter);
        mh->addStretch(1);

        const formkit::Row row =
            formkit::frow(general, T("主题"), mode_holder, formkit::FieldWidth::Flex);
        row.row->setObjectName(QLatin1String(kRowTheme));
        bind_row(row, mode);
        v->addWidget(row.row);
        v->addStretch(1);
    }
    pages->addWidget(general);

    // ------------------------------------------------------------------
    // 页 1：输出（M4-T15 §9.3 追加项③：分文件夹默认结构）
    // ------------------------------------------------------------------
    auto *page_out = new QWidget(pages);
    page_out->setObjectName(QLatin1String(kPageOutput));
    {
        auto *v = new QVBoxLayout(page_out);
        v->setContentsMargins(dk::kBodyPadX, dk::kBodyPadTop, dk::kBodyPadX, dk::kBodyPadBottom);
        v->setSpacing(0);

        // 三档典型模板（§4.2 联动段逐字）+ 非典型当前值回显；落值面 = output_template +
        // split_by_format 一对（§3.6 两字段）。输出页启动期取这里的值作默认（见 MainWindow）。
        formkit::ComboSpec structure_spec;
        structure_spec.object_name = QLatin1String(kFolderStructure);
        structure_spec.accessible = T("分文件夹默认结构");
        auto *structure = formkit::combo_box(page_out, structure_spec);
        {
            const auto add_item = [structure](const QString &text, const QString &tmpl) {
                structure->addItem(text, tmpl);
                structure->setItemData(structure->count() - 1, structure_split_flag(tmpl),
                                       Qt::UserRole + 1);
            };
            add_item(T("按格式分文件夹（$format/$dir/$file）"),
                     QString::fromLatin1(kStructureTemplates[0]));
            add_item(T("不分文件夹（$dir/$file）"), QString::fromLatin1(kStructureTemplates[1]));
            add_item(T("镜像源目录（$dir/$format/$file）"),
                     QString::fromLatin1(kStructureTemplates[2]));
            const QString cur = QString::fromStdString(current.output_template);
            int idx = structure->findData(cur);
            if (idx < 0) {
                // 非典型模板（手改 settings.ini / 未来预设回写）：首项如实回显当前值，
                // 选中它即原值往返（不静默改写用户配置）
                structure->insertItem(0, T("当前设置（%1）").arg(cur), cur);
                structure->setItemData(0, structure_split_flag(cur), Qt::UserRole + 1);
                idx = 0;
            }
            structure->setCurrentIndex(idx);
        }
        // 长文本字段（模板串）→ R2 长档 flex（160 档会吞掉模板串）
        const QString structure_tip =
            T("新建批次的输出目录结构默认值（§4.2 路径模板；可在输出页逐批次改）。\n"
              "$format = 格式目录名，$dir = 源相对目录，$file = 文件名.新扩展名");
        structure->setToolTip(structure_tip);
        const formkit::Row structure_row =
            formkit::frow(page_out, T("分文件夹默认结构"), structure, formkit::FieldWidth::Flex);
        bind_row(structure_row, structure);
        v->addWidget(structure_row.row);
        v->addStretch(1);
    }
    pages->addWidget(page_out);

    // ------------------------------------------------------------------
    // 页 2：运行（§7.4 数值行；修 4-08 语义控件；4-12 与运行页同名同词）
    // ------------------------------------------------------------------
    auto *page_run = new QWidget(pages);
    page_run->setObjectName(QLatin1String(kPageRun));
    QSlider *flatten = nullptr;
    {
        auto *v = new QVBoxLayout(page_run);
        v->setContentsMargins(dk::kBodyPadX, dk::kBodyPadTop, dk::kBodyPadX, dk::kBodyPadBottom);
        v->setSpacing(0);

        // 并行（= 运行页 kv「并行」；修 4-12）：worker 数上限；0 = 自动（物理核数）。
        // 修 4-08：禁用 spinbox 承载「自动（物理核数）」文本魔法值 → 自动/数值 下拉。
        auto *workers =
            make_auto_combo(page_run, QLatin1String(kWorkers), current.workers, kWorkersMax,
                            T("同时处理的文件数上限（worker）；自动 = 物理核数"));
        const formkit::Row workers_row = formkit::frow(
            page_run, T("并行"), workers, formkit::FieldWidth::Standard, QString(), T("0 = 自动"));
        bind_row(workers_row, workers);
        v->addWidget(workers_row.row);

        // 内存预算（§7.4「数值=spinbox（魔法值「自动」+ 旁注「0 = 自动」）」；0 = 自动）
        auto *budget =
            make_spin(page_run, QLatin1String(kBudgetGb), 0, kBudgetMax, current.budget_gb, true,
                      T("内存预算上限；0 = 自动（RAM 的一半，上限 8 GB）"));
        const formkit::Row budget_row =
            formkit::frow(page_run, T("内存预算"), budget, formkit::FieldWidth::Standard,
                          QStringLiteral("GB"), T("0 = 自动"));
        bind_row(budget_row, budget);
        v->addWidget(budget_row.row);

        // 交错启动 ms（0–2000，0 = 关闭；M4-T15 §9.3 既有项：域/单位/范围注释不变）
        auto *stagger =
            make_spin(page_run, QLatin1String(kStaggerMs), 0, kStaggerMax, current.stagger_ms, true,
                      T("worker 取件后的最小启动间隔（§8.1）；0 = 关闭交错（立即启动）"));
        stagger->setSingleStep(10);
        const formkit::Row stagger_row = formkit::frow(
            page_run, T("交错启动"), stagger, formkit::FieldWidth::Standard, T("ms"), T("0–2000"));
        bind_row(stagger_row, stagger);
        v->addWidget(stagger_row.row);

        // 线程预算（0 = 自动 / 逻辑核；修 4-08 同上 → 下拉）
        auto *threads = make_auto_combo(
            page_run, QLatin1String(kThreadBudget), current.thread_budget, kThreadBudgetMax,
            T("总活跃线程上限 T（§8.2，含编码器内部线程）；自动 = 逻辑核数"));
        const formkit::Row threads_row =
            formkit::frow(page_run, T("线程预算"), threads, formkit::FieldWidth::Standard,
                          QString(), T("0 = 自动"));
        bind_row(threads_row, threads);
        v->addWidget(threads_row.row);

        // alpha 合成底色（§7.4）：黑 [滑杆] 白 端标贴滑杆两端 + 预览色块（与滑杆值**一致**）
        // + 「预览」注释（修 4-15/4-16：滑杆-色块-端标同源同侧，色块带注释）
        auto *alpha_track = new QWidget(page_run);
        auto *ar = new QHBoxLayout(alpha_track);
        ar->setContentsMargins(0, 0, 0, 0);
        ar->setSpacing(formkit::kInlineGap);
        flatten = new QSlider(Qt::Horizontal, alpha_track);
        flatten->setObjectName(QLatin1String(kFlattenGray));
        flatten->setRange(0, kSliderMax);
        const int gray0 = gray_to_slider(current.flatten_gray);
        flatten->setValue(gray0);
        flatten->setToolTip(T("alpha 合成底色灰度：0 = 黑，100 = 白"));
        flatten->setAccessibleName(T("alpha 合成底色"));
        swatch = new Swatch(alpha_track);
        swatch->set_tokens(tokens);
        swatch->set_gray(slider_to_gray(gray0)); // 修 4-15：色块 = 滑杆值的灰度映射
        swatch->setToolTip(T("alpha 合成底色预览（与滑杆值一致）"));
        ar->addWidget(inline_slot(alpha_track, T("黑")));
        ar->addWidget(flatten, 1);
        ar->addWidget(inline_slot(alpha_track, T("白")));
        ar->addWidget(swatch);
        const formkit::Row alpha_row =
            formkit::frow(page_run, T("alpha 合成底色"), alpha_track, formkit::FieldWidth::Flex,
                          QString(), T("预览"));
        alpha_row.row->setObjectName(QLatin1String(kRowAlpha));
        bind_row(alpha_row, flatten);
        v->addWidget(alpha_row.row);
        connect(flatten, &QSlider::valueChanged, this, [this](int v) {
            if (auto *sw =
                    dynamic_cast<Swatch *>(findChild<QWidget *>(QLatin1String(kFlattenPreview))))
                sw->set_gray(slider_to_gray(v)); // 滑杆 → 色块（同一映射函数，单源）
        });

        // EXIF 方向（修 4-17：check 行 = [标签轨 80 右对齐][13×13 勾选框 → 文字]）
        const formkit::CheckRow rotate =
            formkit::check_row(page_run, T("EXIF 方向"), T("按 EXIF 方向自动旋转输出"),
                               QLatin1String(kRotateOrientation));
        rotate_box = rotate.box;
        rotate_box->setChecked(current.rotate_orientation); // 默认开（settings 默认 true）
        rotate_box->setToolTip(T("按 EXIF 方向自动旋转输出；关闭则保留原始像素方向"));
        rotate_box->setAccessibleName(T("按 EXIF 方向自动旋转输出"));
        rotate_box->set_tokens(tokens);
        v->addWidget(rotate.row);
        v->addStretch(1);
    }
    pages->addWidget(page_run);

    // ------------------------------------------------------------------
    // 页 3：地图（§2.10 冻结四项；单位入槽、范围入注释）
    // ------------------------------------------------------------------
    auto *page_map = new QWidget(pages);
    page_map->setObjectName(QLatin1String(kPageMap));
    {
        auto *v = new QVBoxLayout(page_map);
        v->setContentsMargins(dk::kBodyPadX, dk::kBodyPadTop, dk::kBodyPadX, dk::kBodyPadBottom);
        v->setSpacing(0);

        formkit::ComboSpec map_provider_spec;
        map_provider_spec.object_name = QLatin1String(kMapProvider);
        map_provider_spec.accessible = T("底图提供方");
        map_provider_spec.tooltip = T("底图来源；高德需填 Web 服务 Key");
        auto *provider = formkit::combo_box(page_map, map_provider_spec);
        provider->addItem(T("OpenStreetMap（内置）"), QStringLiteral("osm"));
        provider->addItem(T("高德地图"), QStringLiteral("amap"));
        {
            const int idx = provider->findData(QString::fromStdString(current.map_provider));
            provider->setCurrentIndex(idx >= 0 ? idx : 0); // 未知值 → OSM（默认）
        }
        const formkit::Row provider_row =
            formkit::frow(page_map, T("底图提供方"), provider, formkit::FieldWidth::Standard);
        bind_row(provider_row, provider);
        v->addWidget(provider_row.row);

        // 图层（M6-T11/C6：map_style 的落值面；与 GPS 卡图层 Seg 同词表同序 矢量|卫星|混合）
        formkit::ComboSpec layer_spec;
        layer_spec.object_name = QLatin1String(kMapStyle);
        layer_spec.accessible = T("图层");
        layer_spec.tooltip = T("地图图层：矢量路网 / 卫星影像 / 混合（影像 + 标注）");
        auto *layer = formkit::combo_box(page_map, layer_spec);
        layer->addItem(T("矢量"), QLatin1String(kMapStyleIds[0]));
        layer->addItem(T("卫星"), QLatin1String(kMapStyleIds[1]));
        layer->addItem(T("混合"), QLatin1String(kMapStyleIds[2]));
        layer->setCurrentIndex(map_style_index(QString::fromStdString(current.map_style)));
        const formkit::Row layer_row =
            formkit::frow(page_map, T("图层"), layer, formkit::FieldWidth::Standard);
        bind_row(layer_row, layer);
        v->addWidget(layer_row.row);

        // 混合叠标注（M6-T11/C6：map_sat_labels；勾选行同「EXIF 方向」一行一形）
        const formkit::CheckRow sat_labels =
            formkit::check_row(page_map, T("混合叠标注"), T("混合图层在影像上叠加道路与地名标注"),
                               QLatin1String(kMapSatLabels));
        sat_labels_box = sat_labels.box;
        sat_labels_box->setChecked(current.map_sat_labels);
        sat_labels_box->setToolTip(T("混合图层在影像上叠加道路与地名标注（关闭 = 纯影像）"));
        sat_labels_box->setAccessibleName(T("混合图层在影像上叠加道路与地名标注"));
        sat_labels_box->set_tokens(tokens);
        v->addWidget(sat_labels.row);

        auto *amap_key = new QLineEdit(page_map);
        amap_key->setObjectName(QLatin1String(kAmapKey));
        amap_key->setEchoMode(QLineEdit::Password); // 不回显明文
        amap_key->setText(QString::fromStdString(current.amap_key));
        amap_key->setPlaceholderText(T("在高德开放平台申请"));
        amap_key->setFont(theme::font(theme::Typography::body_px));
        hook_field(amap_key, dk::kFieldInput);
        amap_key->setToolTip(T("高德 Web 服务 Key（不回显明文）"));
        const formkit::Row key_row =
            formkit::frow(page_map, T("高德 Web 服务 Key"), amap_key, formkit::FieldWidth::Flex);
        bind_row(key_row, amap_key);
        v->addWidget(key_row.row);

        auto *tile_cache =
            make_spin(page_map, QLatin1String(kTileCacheMb), kTileCacheMin, kTileCacheMax,
                      current.tile_cache_mb, false, T("地图瓦片的本地缓存上限"));
        const formkit::Row tile_row =
            formkit::frow(page_map, T("瓦片缓存"), tile_cache, formkit::FieldWidth::Standard,
                          QStringLiteral("MB"), T("16–512"));
        bind_row(tile_row, tile_cache);
        v->addWidget(tile_row.row);

        formkit::ComboSpec level_spec;
        level_spec.object_name = QLatin1String(kLogLevel);
        level_spec.accessible = T("日志级别");
        level_spec.tooltip = T("日志详细程度；trace 最详细");
        auto *level = formkit::combo_box(page_map, level_spec);
        level->addItem(QStringLiteral("trace"), QLatin1String(kLevelIds[0]));
        level->addItem(QStringLiteral("debug"), QLatin1String(kLevelIds[1]));
        level->addItem(QStringLiteral("info"), QLatin1String(kLevelIds[2]));
        level->addItem(QStringLiteral("warn"), QLatin1String(kLevelIds[3]));
        level->addItem(QStringLiteral("error"), QLatin1String(kLevelIds[4]));
        {
            // R-157（M7-WP9）：落盘值合法性判定单源 = pp::log_level_from_text（core/logger.h；
            // 与启动侧/mainwindow 同一条判据 —— 词表外与 "critical" 都判负）。判负 → 回退 info，
            // 与原 findData 失败回退行为逐字一致。
            pp::LogLevel parsed = pp::LogLevel::Info;
            const int idx = pp::log_level_from_text(current.log_level, parsed)
                                ? level->findData(QString::fromStdString(current.log_level))
                                : -1;
            level->setCurrentIndex(idx >= 0 ? idx : level->findData(QStringLiteral("info")));
        }
        const formkit::Row level_row =
            formkit::frow(page_map, T("日志级别"), level, formkit::FieldWidth::Standard);
        bind_row(level_row, level);
        v->addWidget(level_row.row);
        v->addStretch(1);
    }
    pages->addWidget(page_map);

    // ------------------------------------------------------------------
    // 页 4：关于（版本唯一来源 = core/version.h；库清单只读等宽；许可全文 tooltip）
    // ------------------------------------------------------------------
    auto *page_about = new QWidget(pages);
    page_about->setObjectName(QLatin1String(kPageAbout));
    {
        auto *v = new QVBoxLayout(page_about);
        v->setContentsMargins(dk::kBodyPadX, dk::kBodyPadTop, dk::kBodyPadX, dk::kBodyPadBottom);
        v->setSpacing(theme::Metrics::group_gap);

        auto *brand_row = new QWidget(page_about);
        auto *bl = new QHBoxLayout(brand_row);
        bl->setContentsMargins(0, 0, 0, 0);
        bl->setSpacing(theme::Metrics::brand_gap);
        bl->addWidget(new IconLabel(icons::Name::Image, theme::Metrics::brand_icon, brand_row));
        auto *name = new QLabel(QStringLiteral("PhotoPipeline"), brand_row);
        name->setFont(theme::font(theme::Typography::caption_px, QFont::Bold)); // §3.3 品牌 13/700
        bl->addWidget(name);
        // M2-T11 §2.2：版本号唯一来源 = 生成的 core/version.h（PP_VERSION_STRING），不再硬编码。
        auto *version = new formkit::PillBadge(brand_row);
        version->setObjectName(QStringLiteral("app_version"));
        version->set_tone(formkit::PillBadge::Tone::Neutral);
        version->setText(QLatin1String(PP_VERSION_STRING));
        version->set_tokens(tokens);
        version->setToolTip(T("版本 %1").arg(QLatin1String(PP_VERSION_STRING)));
        bl->addWidget(version);
        bl->addStretch(1);
        v->addWidget(brand_row);

        QStringList version_lines;
        for (const auto &[key, value] : pp::library_versions()) {
            version_lines << QString::fromStdString(key) + QStringLiteral("=") +
                                 QString::fromStdString(value);
        }
        auto *versions = new QPlainTextEdit(version_lines.join(QLatin1Char('\n')), page_about);
        versions->setObjectName(QLatin1String(kLibraryVersions));
        versions->setReadOnly(true); // §2.10：只读清单
        versions->setFont(theme::font(theme::Typography::mono_px));
        versions->setAccessibleName(T("库版本清单"));
        versions->setToolTip(T("本次构建链接的第三方库版本"));
        // §9.1 U6-FIX（尺寸贴合内容）：不要 QPlainTextEdit 默认 192px 的 sizeHint——版本清单
        // 最多占 rows 行高度，超出部分滚动（内容不裁切，可滚动查看）
        const int version_rows =
            std::clamp(static_cast<int>(version_lines.size()), kVersionRowsMin, kVersionRowsMax);
        versions->setMaximumHeight(versions->fontMetrics().lineSpacing() * version_rows +
                                   2 * versions->frameWidth() + kVersionRowsPad);
        v->addWidget(versions, 1);

        // §9.3（W3-T13/T15 排版收口）：全界面禁止文案折行 → 本标签不开自动折行（word wrap）。
        // 本文是**多句许可清单**（不是可省略的单行文案），故不用单行省略标签（省掉半句许可信息
        // 不可接受），改为在源码原有的自然断句处显式换行（许可清单文字与库序不变）。
        // M5-T10：对话框宽度按 §7.4 钉死 660 → 断行点按新的正文宽（660 − 2×18 = 624）**复测**
        // 重排（每行实测 ≤ 620：620/300/550），不留右侧裁切。
        auto *license =
            new QLabel(T("本程序 GPL-3.0-or-later。所用库：Exiv2/x265 (GPLv2+)、libheif (LGPLv3)、"
                         "\nQt (LGPLv3)、OIIO (Apache-2.0)、"
                         "\nlibjxl/libwebp/SVT-AV1/libaom (BSD)、lcms2/spdlog (MIT)。"),
                       page_about);
        license->setObjectName(QStringLiteral("license_text"));
        license->setWordWrap(false);
        license->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkNote));
        license->setFont(theme::font(theme::Typography::hint_px));
        license->setToolTip(license->text());
        v->addWidget(license);
    }
    pages->addWidget(page_about);

    // ---- 底栏（§7.4：[恢复默认] ……… [取消][确定]；主按钮实心居右）----
    auto *defaults_btn = dk::make_button(footer, T("恢复默认"), /*primary=*/false);
    defaults_btn->setObjectName(QLatin1String(dk::kDefaultsName));
    defaults_btn->setToolTip(T("把设置恢复为出厂默认（点确定后生效）"));
    auto *cancel_btn = dk::make_button(footer, T("取消"), /*primary=*/false);
    cancel_btn->setObjectName(QLatin1String(dk::kCancelName));
    auto *ok_btn = dk::make_button(footer, T("确定"), /*primary=*/true);
    ok_btn->setObjectName(QLatin1String(dk::kOkName));
    ok_btn->setDefault(true);
    footer_line->addWidget(defaults_btn);
    footer_line->addStretch(1);
    footer_line->addWidget(cancel_btn);
    footer_line->addWidget(ok_btn);
    card_line->addWidget(footer);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(card, 1);

    connect(tabs, &dk::UnderlineTabs::current_changed, this,
            [pages](int index) { pages->setCurrentIndex(index); });
    connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok_btn, &QPushButton::clicked, this, &QDialog::accept);

    // 恢复默认（§7.4）：把**可编辑面**复位到 AppSettings 默认（不改 last_* / class_file）
    connect(defaults_btn, &QPushButton::clicked, this, [this] {
        const pp::AppSettings def;
        if (auto *w = findChild<QComboBox *>(QLatin1String(kWorkers))) {
            const int idx = w->findData(def.workers);
            w->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        if (auto *w = findChild<QSpinBox *>(QLatin1String(kBudgetGb)))
            w->setValue(def.budget_gb);
        if (auto *w = findChild<QSpinBox *>(QLatin1String(kStaggerMs)))
            w->setValue(def.stagger_ms);
        if (auto *w = findChild<QComboBox *>(QLatin1String(kThreadBudget))) {
            const int idx = w->findData(def.thread_budget);
            w->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        if (auto *w = findChild<QSpinBox *>(QLatin1String(kTileCacheMb)))
            w->setValue(def.tile_cache_mb);
        if (auto *w = findChild<QLineEdit *>(QLatin1String(kAmapKey)))
            w->setText(QString::fromStdString(def.amap_key));
        if (auto *w = findChild<QCheckBox *>(QLatin1String(kRotateOrientation)))
            w->setChecked(def.rotate_orientation);
        if (auto *w = findChild<QSlider *>(QLatin1String(kFlattenGray)))
            w->setValue(gray_to_slider(def.flatten_gray));
        if (auto *w = findChild<QComboBox *>(QLatin1String(kMapProvider))) {
            const int idx = w->findData(QString::fromStdString(def.map_provider));
            w->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        // M6-T11（C6）：图层档 / 混合叠标注也属可编辑面 → 复位到默认（road / true）
        if (auto *w = findChild<QComboBox *>(QLatin1String(kMapStyle)))
            w->setCurrentIndex(map_style_index(QString::fromStdString(def.map_style)));
        if (auto *w = findChild<QCheckBox *>(QLatin1String(kMapSatLabels)))
            w->setChecked(def.map_sat_labels);
        if (auto *w = findChild<QComboBox *>(QLatin1String(kLogLevel))) {
            const int idx = w->findData(QString::fromStdString(def.log_level));
            w->setCurrentIndex(idx >= 0 ? idx : w->findData(QStringLiteral("info")));
        }
        if (auto *w = findChild<QComboBox *>(QLatin1String(kFolderStructure))) {
            const int idx = w->findData(QString::fromStdString(def.output_template));
            w->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        // A28：主题档也是可编辑面 → 复位到默认档（system = 跟随系统）。程序化 set_current_index
        // 不发射信号 → 与其余字段同一"确定后才落地"语义（不即时改主题）。
        if (auto *w = findChild<formkit::SegmentedControl *>(QLatin1String(kThemeMode)))
            w->set_current_index(theme_mode_index(QString::fromStdString(def.ui_theme_mode)));
    });

    // ---- 主题切换（通用页签，修 4-20 + A28）：即时生效（本对话框就地重放）+ 持久化
    //      （写进 AppSettings.ui_theme_mode，见 settings()）----
    connect(mode, &formkit::SegmentedControl::current_changed, this,
            [this, swatch, rotate_box, sat_labels_box, mode](int index) {
                // A28：档位 → 档名单源映射；"跟随系统"= 撤掉强制档 + 清持久化覆盖
                const QString id = theme_mode_id(index);
                if (id == QLatin1String("light") || id == QLatin1String("dark"))
                    qputenv("PP_UI_THEME", id.toLatin1());
                else
                    qunsetenv("PP_UI_THEME"); // 跟随系统：撤掉强制档
                // 进程级持久化档就地更新：主窗在下一次 open_settings() 的 accept 后按同一条
                // 单源顺序重解析（PP_UI_THEME 强制档 > 持久化档 > 跟随系统）。
                theme::set_persisted_theme_mode(id.toLatin1());
                const theme::Tokens t = theme::tokens(theme::preferred_theme_mode());
                setStyleSheet(dk::dialog_qss(t));
                // R-064（M7-WP4）：自绘件令牌重放收口 theme::replay_tokens **单点**（原逐件
                // 调用漏 formkit Combo/Spin/DSpin 箭头 → 主题切换后箭头残留旧主题色；且漏
                // UnderlineTabs → UX-12 亮色页签标题色错）。Tokenized 全族 + 属性钩子 repolish。
                theme::replay_tokens(this, t);
                dk::restyle_title_bar(
                    dk::TitleBar{findChild<QFrame *>(QLatin1String(dk::kTitleBarName)),
                                 findChild<QLabel *>(QStringLiteral("pp-dlg-title")),
                                 findChild<QPushButton *>(QStringLiteral("pp-dlg-close"))},
                    t);
            });

    // §11：自绘焦点环（accent 1px；系统虚线框由 dialog_qss 的 outline:none 关闭）
    auto *ring = new dk::FocusRing(this);
    ring->set_tokens(tokens);

    // §9.1 U6-FIX（尺寸贴合内容）：高度贴合**当前页**（页签切换时重算），660 宽固定。
    const auto fit_height = [this, pages] {
        QWidget *page = pages->currentWidget();
        if (page == nullptr)
            return;
        if (QLayout *lay = page->layout())
            lay->activate();
        setFixedHeight(dk::kTitleBarH + dk::kTabsBarH + page->sizeHint().height() + dk::kFooterH);
    };
    // 高度贴合挂在 **QStackedWidget::currentChanged**（页切换的唯一收敛点）：点页签与程序化
    // set_current_index 两条路径都会重算高度（不依赖"页签信号恰好被接线"的偶然性）。
    connect(pages, &QStackedWidget::currentChanged, this, [fit_height](int) { fit_height(); });
    fit_height();
    // 首次 show：居中到父窗（§7.4「660px 宽、居中」）并再贴合一次（构造期页几何未落定）
    installEventFilter(new CenterOnShow(this, [this, fit_height] {
        fit_height();
        if (QWidget *p = parentWidget(); p != nullptr) {
            move(p->mapToGlobal(QPoint((p->width() - width()) / 2, (p->height() - height()) / 2)));
        }
    }));

    // 无边框接线（拖拽 / 八向缩放入口与主窗同一机制：platform/frameless）
    pp::platform::Frameless::attach(this, title_bar.widget, {title_bar.close});
    dk::bind_close_button(title_bar, this); // GUI-R2 修 3：caption ✕ 必须真关窗
}

pp::AppSettings SettingsDialog::settings() const {
    // 非确定路径（取消 / 关闭 / 未 exec）→ 构造快照（§2.10“仅确定路径返回编辑值”）
    pp::AppSettings s = original_values(this);
    if (result() != QDialog::Accepted)
        return s;

    if (const auto *w = findChild<QComboBox *>(QLatin1String(kWorkers))) {
        s.workers = std::clamp(w->currentData().toInt(), 0, kWorkersMax);
    }
    if (const auto *w = findChild<QSpinBox *>(QLatin1String(kBudgetGb))) {
        s.budget_gb = w->value();
    }
    if (const auto *w = findChild<QSlider *>(QLatin1String(kFlattenGray))) {
        s.flatten_gray = static_cast<double>(w->value()) / kSliderMax; // 0.01 步长
    }
    if (const auto *w = findChild<QCheckBox *>(QLatin1String(kRotateOrientation))) {
        s.rotate_orientation = w->isChecked();
    }
    if (const auto *w = findChild<QComboBox *>(QLatin1String(kMapProvider))) {
        const QString id = w->currentData().toString();
        if (!id.isEmpty())
            s.map_provider = id.toStdString();
    }
    if (const auto *w = findChild<QLineEdit *>(QLatin1String(kAmapKey))) {
        s.amap_key = w->text().toStdString();
    }
    if (const auto *w = findChild<QSpinBox *>(QLatin1String(kTileCacheMb))) {
        s.tile_cache_mb = w->value();
    }
    if (const auto *w = findChild<QComboBox *>(QLatin1String(kLogLevel))) {
        const QString id = w->currentData().toString();
        if (!id.isEmpty())
            s.log_level = id.toStdString();
    }
    // M4-T15 §9.3 追加项读回（交错启动 / 线程预算 / 分文件夹默认结构）
    if (const auto *w = findChild<QSpinBox *>(QLatin1String(kStaggerMs))) {
        s.stagger_ms = std::clamp(w->value(), 0, kStaggerMax);
    }
    if (const auto *w = findChild<QComboBox *>(QLatin1String(kThreadBudget))) {
        s.thread_budget = std::clamp(w->currentData().toInt(), 0, kThreadBudgetMax);
    }
    if (const auto *w = findChild<QComboBox *>(QLatin1String(kFolderStructure))) {
        const QString tmpl = w->currentData().toString();
        if (!tmpl.isEmpty()) {
            s.output_template = tmpl.toStdString();
            s.split_by_format = w->currentData(Qt::UserRole + 1).toBool();
        }
    }
    // A28：主题三档写回 —— 从**段选中态**取值（与其它字段"从控件取值"同口径；信号是否发射
    // 不影响落库，程序化 set_current_index 同样成立）。
    if (const auto *w = findChild<formkit::SegmentedControl *>(QLatin1String(kThemeMode))) {
        s.ui_theme_mode = theme_mode_id(w->current_index()).toStdString();
    }
    // M6-T11（C6）：图层档 / 混合叠标注写回（同"从控件取值"口径）
    if (const auto *w = findChild<QComboBox *>(QLatin1String(kMapStyle))) {
        const QString id = w->currentData().toString();
        if (!id.isEmpty())
            s.map_style = id.toStdString();
    }
    if (const auto *w = findChild<QCheckBox *>(QLatin1String(kMapSatLabels))) {
        s.map_sat_labels = w->isChecked();
    }
    // last_format / last_preset / last_out_root 不被本对话框编辑 → 保持构造快照
    return s;
}

} // namespace pp::ui
