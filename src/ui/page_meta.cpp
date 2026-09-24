// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M4-W3-T12）— 元数据页：**生效值显示**
//
// 规格 = docs/v0.3.0-design.md §5.1/§5.2 + §9.3（元数据页内容规格）+ docs/mockups/meta-dark.html
// （精确规格；meta-light.html 的浅色对照）+ meta-dark.png / meta-light.png（视觉参照）。
//
// 数据源与"同源"义务（§5.1，本页的**第一硬约束**）：
//   * 生效值一律来自 W1-T8 的纯函数 `pp::preview_effective(probe, rules, exception)`，
//     本文件**不另算**任何时间/GPS 合成（无第二条显示路径）；
//   * 喂进去的 `rules` 就是提交运行用的那一份 —— `build_rules()` 同时是 `rules()` 的实现，
//     即"显示 = 即将写入"（与 build_plan 在 core 侧同源，见 core/metadata.h 的 PP-FROZEN(0.3.0)
//     头）。
//
// 卡片（§9.3 逐条；mockup 控件集合/文案对齐，偏离处见文末"落地口径"）：
//   ① 关键信息（只读，常显）：拍摄时间 · 相机 · 镜头 · 曝光(ISO/快门/光圈/焦距) ·
//      尺寸/位深/通道 · 色彩空间(ICC 有无) · 文件大小/格式 · GPS 有无；
//   ② 时间偏移：seg（Δ 偏移 | 时区语义）+ 6 spin + 生效对照行（原→生效 + 来源字段标注）；
//   ③ GPS：生效坐标（只读，跟随选中）+ MAP（跟随 center_on）+ 三按钮 + 批量规则折叠；
//   ④ 隐私 / 文件时间 / 例外行（含「EXIF / XMP 标签编辑…」「单文件例外 (N) ⚑」两个折叠入口）。
//
// 跟随语义（§5.2 / D4 / D8）：一切显示跟随文件列表选中项；**无选中 → 首个文件 + 「(1/N)」提示**；
//   多选（≥2）→ 时间卡换成逐行小表（文件名 | 原 → 生效，上限 8 行 + 「…还有 N 个」）；
//   地图只显**生效值单钉**（D4：不画新旧对比），无生效 GPS → 不 center_on（保持选点位置）且不画钉。
//
// .mod（§9.2「改动值 = 强调边框+强调色字+700」；mockup .spin.mod = meta-dark.html:144/367）：
//   改动值统一走 `theme::set_mod(widget, changed)` —— Δ 非零的 spin、时区 from≠to 的 combo、
//   生效值与原值不同的时间/坐标显示框。未修改时两个值相同也照常显示（§5.2 末段）。
//
// 排版（§9.3 硬约束）：表单行 = `theme::Metrics::form_label_w`(70) 右对齐标签列 +
//   `form_col_gap`(12) 列距 + `form_row_h`(32) 行高；全页禁用折行（ElidedLabel = 省略号 +
//   悬浮全文）。时间卡的 6 个偏移 spin 沿用 mockup 的 `.fields` 紧凑条（"年 [+0]" 横排，
//   `.fld{gap:5px}` / `.spin{52×26}`）—— 它不是"标签/值"表单行，不占 70px 轴；其余表单行
//   （关键信息卡、GPS 生效值行、批量规则行）共用同一标签轴。
//
// 落地口径（§5.1/§5.2 未逐字给出处，逐条记账）：
//   * **时间卡不再有「启用」复选框**（mockup/§9.3 的控件集合 = seg + 6 spin + 生效对照行）：
//     全零 Δ 与 from==to 的时区本就等价于"无规则"（`TimeShift::is_noop()`；写路径
//     `build_plan` 与显示路径 `preview_effective` 用同一判据跳过）→ 语义零损失。
//   * **GPS 卡的批量规则输入**（启用/纬度/经度/海拔/方位/时间戳）收进「批量 GPS 规则」折叠行：
//     mockup 的可见面（生效坐标 + MAP + 三按钮）逐条保留；v0.2 冻结的 rules() 语义要求
//     "规则存在性"必须显式可控（否则把跟随文件的源 GPS 当规则写回会污染其它文件）→ 折叠行内
//     保留「启用」。这是**控件集合 ⊃ mockup** 的唯一一处（mockup 自身也有"可选折叠"形态）。
//   * 「地图选点」= 取地图**视口中心**为规则坐标（配合拖拽定位）；点击地图仍照旧直接取点
//     （v0.2 行为不变，ui-smoke 的 amap 边界断言继续走点击路径）。
//   * 生效值为空时的占位文案：隐私剥除开 → 「将被移除」（§5.1）；清除 GPS 勾选 → 「将清除 GPS」；
//     其余（无源 GPS 且无规则）→ 「无 GPS，规则将写入」（§5.2 逐字）。
//   * 时间卡的「来源字段」按 EXIF 字段名标注：DateTimeOriginal / CreateDate / ModifyDate
//     （core/metadata.h 的字段表口径；三行各自一源）。
//   * 异步性（§5.2 末段）：probe 摘要在列表添加时由 thumbs 通道收集（`MetaSelectionItem::info`）；
//     本页只对**跟随项**（≤ 8）做一次 `read_metadata`（QtConcurrent + 序号守卫，M2-T7 口径），
//     之后每次规则变化都是纯函数重算（微秒级）→ 即时刷新。
//   * M1b/M2 的"扫描前 200 个文件找首个含时间者"预览（及其上限文案）随 §5.2 的跟随口径**退场**
//     （显示源不再是"批内首个有时间者"而是"选中项/首个文件"）。
//
// 自验钩子（供 --ui-smoke 探针与 .cache 自验程序读；非用户可见）：
//   * 动态属性（同时挂在 MetaState 子对象与页面本身）：pp_meta_follow_count /
//     pp_meta_follow_name / pp_meta_follow_pending / pp_meta_follow_stale_dropped /
//     pp_meta_time_new / pp_meta_gps_effective / pp_meta_multi_rows；
//   * ElidedLabel 的 `ppFullText`：显示文本可能按宽度省略成 "…"，语义值（全文）挂在这枚属性上，
//     自验断言按语义值比对；
//   * objectName（§9.3 pp-* 命名法）：pp-meta-info-<field> / pp-meta-time-<old|new|src>-<i> /
//     pp-meta-gps-<lat|lon|alt|dir|ts> / pp-meta-map / pp-meta-seg-<delta|tz> /
//     pp-meta-delta-<years…seconds> / pp-meta-multi-<name|old|new>-<i> / pp-meta-info-hint /
//     pp-meta-multi-more 等；既有 gps_lat/gps_lon/privacy_strip/… 保留原名（外部无引用，续用）。
//   * 显示层用到的 Exiv2 标签名逐个在 0.28.9 上验过（`ISOSpeedRatings` 存在、
//     `PhotographicSensitivity` 不存在且会抛异常 → exif_text 兜住构造失败）。
#include "ui/page_meta.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QFileInfo>
#include <QFrame>
#include <QFuture>
#include <QFutureWatcher>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mapwidget/mapwidget.h"
#include "ui/theme.h"

namespace pp::ui {
namespace {

// PageMeta 头文件（PP-FROZEN）没有成员声明，全部状态挂在一个子 QObject 上，
// 经动态属性从 PageMeta* 取回（O(1)，随父对象析构）。
constexpr const char *kStateProperty = "pp_meta_state";
constexpr const char *kStateObjectName = "pp_meta_state";

// §5.2 多选小表上限（超过 → 「…还有 N 个」）
constexpr int kMultiRowCap = 8;
// mockup 逐条几何：.map-box{height:196px} / .gps-in{height:26px} / .spin{width:52px;height:26px}
constexpr int kMapHeight = 196;
constexpr int kGpsBoxHeight = 26;
constexpr int kSpinWidth = 52;
constexpr int kSpinHeight = 26;
// mockup .effect{margin:4px 14px 12px; padding:9px 12px}
constexpr int kCardPadX = 14;

// 时间三字段的来源标注（core/metadata.h 的字段表：ExifTool 名）
constexpr const char *kTimeSourceLabels[3] = {"DateTimeOriginal", "CreateDate", "ModifyDate"};

// §9.1 FIX2 口径：控件禁用时清空占位文本（Qt 占位色不随 setEnabled 变化，像素实测 ink 与
// 启用态相同 → 会被误认为可编辑）；启用时恢复原始文案（不新造文案）。
constexpr const char *kLatPlaceholder = QT_TR_NOOP("例如 31.230400");
constexpr const char *kLonPlaceholder = QT_TR_NOOP("例如 121.473700");
constexpr const char *kSearchPlaceholder = QT_TR_NOOP("搜索地点");
// §5.2 逐字：无 GPS → 空值提示
constexpr const char *kNoGpsPlaceholder = QT_TR_NOOP("无 GPS，规则将写入");
constexpr const char *kWillRemoveText = QT_TR_NOOP("将被移除");
constexpr const char *kGpsClearPlaceholder = QT_TR_NOOP("将清除 GPS");
constexpr const char *kNoneText = QT_TR_NOOP("（无）");

// ---------------------------------------------------------------------------
// 单行省略标签（§9.3「全界面禁止文案折行（超长省略号）」；与 mainwindow.cpp 的 ElidedLabel 同口径）
// ---------------------------------------------------------------------------

class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) { setWordWrap(false); }
    void set_full_text(const QString &text) {
        full_text_ = text;
        // 探针可读的**语义值**（显示文本可能按宽度省略成 "…"；自验断言按此口径比对）
        setProperty("ppFullText", text);
        setToolTip(text);
        updateGeometry(); // sizeHint 随全文变化（否则布局按"已省略文本"定宽 → 永远省略）
        apply_elide();
    }
    QString full_text() const { return full_text_; }
    QSize sizeHint() const override {
        const QSize base = QLabel::sizeHint();
        if (full_text_.isEmpty())
            return base;
        return QSize(fontMetrics().horizontalAdvance(full_text_) + 2, base.height());
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        apply_elide();
    }

private:
    void apply_elide() {
        if (full_text_.isEmpty()) {
            QLabel::clear();
            return;
        }
        if (width() <= 0) { // 布局尚未定宽：先出全文，resizeEvent 时再省略
            QLabel::setText(full_text_);
            return;
        }
        QLabel::setText(fontMetrics().elidedText(full_text_, Qt::ElideRight, std::max(0, width())));
    }
    QString full_text_;
};

// ---------------------------------------------------------------------------
// 文本格式化（只做显示层排版；数值本身来自 preview_effective / probe 摘要）
// ---------------------------------------------------------------------------

// "2024:03:01 10:00:00" → "2024-03-01 10:00:00"（mockup 的拍摄时间/生效时间口径）
QString format_datetime(const QString &exif_dt) {
    if (exif_dt.size() < 10) {
        return exif_dt;
    }
    QString date = exif_dt.left(10);
    date.replace(QLatin1Char(':'), QLatin1Char('-'));
    return date + exif_dt.mid(10);
}

QString datetime_text(const std::optional<pp::DateTimeVal> &v) {
    return v.has_value() ? format_datetime(QString::fromStdString(v->value))
                         : PageMeta::tr(kNoneText);
}

QString coord_text(double v, int decimals) {
    return QString::number(v, 'f', decimals) + QStringLiteral("°");
}

QString format_size(qint64 bytes) {
    if (bytes <= 0) {
        return QStringLiteral("—");
    }
    const double mb = double(bytes) / (1024.0 * 1024.0);
    if (mb >= 1.0) {
        return QStringLiteral("%1 MB").arg(QString::number(mb, 'f', 2));
    }
    return QStringLiteral("%1 KB").arg(QString::number(double(bytes) / 1024.0, 'f', 1));
}

// probe 摘要的通道数 → 名称（core/types.h：channels ∈ {1,2,3,4}）
QString channels_text(const pp::ImageInfo &info) {
    switch (info.channels) {
    case 1:
        return info.has_alpha ? PageMeta::tr("灰度 + α") : PageMeta::tr("灰度");
    case 2:
        return PageMeta::tr("灰度 + α");
    case 3:
        return QStringLiteral("RGB");
    case 4:
        return QStringLiteral("RGBA");
    default:
        return QStringLiteral("—");
    }
}

// EXIF 字符串读取（显示层只读；§1.7 例外：本文件允许直接调 Exiv2 API）
std::string exif_text(const Exiv2::ExifData &exif, const char *key) {
    std::unique_ptr<Exiv2::ExifKey> exif_key;
    try {
        exif_key = std::make_unique<Exiv2::ExifKey>(key);
    } catch (const std::exception &) {
        return {}; // 未知标签名 = 无值（不抛、不猜；0.28.9 无 PhotographicSensitivity 等名字）
    }
    const auto it = exif.findKey(*exif_key);
    if (it == exif.end()) {
        return {};
    }
    std::string value = it->toString();
    while (!value.empty() && (value.back() == '\0' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

QString camera_text(const Exiv2::ExifData &exif) {
    const QString make = QString::fromStdString(exif_text(exif, "Exif.Image.Make")).trimmed();
    const QString model = QString::fromStdString(exif_text(exif, "Exif.Image.Model")).trimmed();
    if (!make.isEmpty() && !model.isEmpty() && !model.startsWith(make, Qt::CaseInsensitive)) {
        return make + QLatin1Char(' ') + model;
    }
    if (!model.isEmpty()) {
        return model;
    }
    return make.isEmpty() ? QStringLiteral("—") : make;
}

QString lens_text(const Exiv2::ExifData &exif) {
    const QString model = QString::fromStdString(exif_text(exif, "Exif.Photo.LensModel")).trimmed();
    if (!model.isEmpty()) {
        return model;
    }
    const QString make = QString::fromStdString(exif_text(exif, "Exif.Photo.LensMake")).trimmed();
    return make.isEmpty() ? QStringLiteral("—") : make;
}

// 曝光 = ISO · 快门 · 光圈 · 焦距（§5.2 字段集；缺项即略过，全缺 → "—"）
QString exposure_text(const Exiv2::ExifData &exif) {
    QStringList parts;
    const std::string iso = exif_text(exif, "Exif.Photo.ISOSpeedRatings");
    if (!iso.empty()) {
        parts << QStringLiteral("ISO %1").arg(QString::fromStdString(iso));
    }
    const auto shutter = exif.findKey(Exiv2::ExifKey("Exif.Photo.ExposureTime"));
    if (shutter != exif.end()) {
        const Exiv2::Rational r = shutter->toRational();
        if (r.first > 0 && r.second > 0) {
            parts << (r.second == 1
                          ? QStringLiteral("%1s").arg(r.first)
                          : QStringLiteral("1/%1s").arg(std::lround(double(r.second) / r.first)));
        }
    }
    const auto fnumber = exif.findKey(Exiv2::ExifKey("Exif.Photo.FNumber"));
    if (fnumber != exif.end()) {
        const Exiv2::Rational r = fnumber->toRational();
        if (r.first > 0 && r.second > 0) {
            parts << QStringLiteral("f/%1").arg(
                QString::number(double(r.first) / r.second, 'f', 2));
        }
    }
    const auto focal = exif.findKey(Exiv2::ExifKey("Exif.Photo.FocalLength"));
    if (focal != exif.end()) {
        const Exiv2::Rational r = focal->toRational();
        if (r.first > 0 && r.second > 0) {
            parts << QStringLiteral("%1mm").arg(
                QString::number(double(r.first) / r.second, 'f', 1));
        }
    }
    return parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral(" · "));
}

QString dms_component(double value, bool latitude) {
    const double abs_v = std::isfinite(value) ? std::fabs(value) : 0.0;
    double tenths = std::round(abs_v * 36000.0) / 10.0; // 0.1" 网格 → 无 60.0" 进位问题
    const int deg = int(tenths / 3600.0);
    tenths -= deg * 3600.0;
    const int min = int(tenths / 60.0);
    const double sec = tenths - min * 60.0;
    const QChar hemi = latitude ? (value < 0 ? QLatin1Char('S') : QLatin1Char('N'))
                                : (value < 0 ? QLatin1Char('W') : QLatin1Char('E'));
    return QStringLiteral("%1°%2'%3\"%4")
        .arg(deg)
        .arg(min)
        .arg(QString::number(sec, 'f', 1))
        .arg(hemi);
}

QString dms_string(double lat, double lon) {
    return dms_component(lat, true) + QLatin1Char(' ') + dms_component(lon, false);
}

// ---------------------------------------------------------------------------
// 页面状态（挂在 PageMeta 下的子 QObject）
// ---------------------------------------------------------------------------

// 跟随项的 probe 快照（后台线程产出；只含值语义，不触碰 GUI）
struct ProbeSnapshot {
    QString path;
    pp::SourceMeta meta;
    bool ok = false;
};

struct TimeRowWidgets {
    ElidedLabel *old = nullptr;
    ElidedLabel *fresh = nullptr;
    ElidedLabel *src = nullptr;
};

struct MetaState : QObject {
    PageMeta *page = nullptr;
    bool loading = false; // apply_rules 期间抑制信号与重复刷新
    theme::Tokens tokens;

    // ---- 卡片 ① 关键信息（只读）----
    std::array<QFrame *, 4> cards{};
    ElidedLabel *info_pill = nullptr;
    ElidedLabel *info_hint = nullptr;
    std::array<ElidedLabel *, 8> info_value{};

    // ---- 卡片 ② 时间偏移 ----
    ElidedLabel *time_hint = nullptr;
    QToolButton *seg_delta = nullptr;
    QToolButton *seg_tz = nullptr;
    QWidget *delta_row = nullptr;
    QWidget *tz_row = nullptr;
    std::array<QSpinBox *, 6> delta{};
    QComboBox *tz_from = nullptr;
    QComboBox *tz_to = nullptr;
    QFrame *effect = nullptr;
    QWidget *time_rows_host = nullptr;
    std::array<TimeRowWidgets, 3> time_row{};
    QWidget *multi_host = nullptr;
    QGridLayout *multi_grid = nullptr;
    ElidedLabel *multi_more = nullptr;

    // ---- 卡片 ③ GPS ----
    ElidedLabel *gps_hint = nullptr;
    QLabel *gps_warn = nullptr;
    QLineEdit *view_lat = nullptr;
    QLineEdit *view_lon = nullptr;
    QLineEdit *view_alt = nullptr;
    QLineEdit *view_dir = nullptr;
    QLineEdit *view_ts = nullptr;
    pp::map::MapWidget *map = nullptr;
    QLineEdit *search_edit = nullptr;
    QPushButton *search_btn = nullptr;
    QComboBox *search_results = nullptr;
    QPushButton *read_gps = nullptr;
    QPushButton *pick_map = nullptr;
    QPushButton *clear_gps = nullptr;
    QToolButton *rule_toggle = nullptr;
    QWidget *rule_host = nullptr;
    QCheckBox *gps_enable = nullptr;
    QToolButton *more_toggle = nullptr; // 批量规则的可选字段折叠（海拔/方位/时间戳）
    QLineEdit *lat = nullptr;
    QLineEdit *lon = nullptr;
    QLineEdit *alt = nullptr;
    QLineEdit *dir = nullptr;
    QLineEdit *ts = nullptr;
    QLabel *gps_status = nullptr;

    // ---- 卡片 ④ 隐私 / 时间 / 例外 ----
    QCheckBox *strip = nullptr;
    QCheckBox *mtime = nullptr;
    QToolButton *link_tags = nullptr;
    QToolButton *link_exc = nullptr;
    QWidget *tag_host = nullptr;
    QWidget *exc_host = nullptr;
    QVBoxLayout *tag_rows = nullptr;
    QPushButton *add_tag = nullptr;
    QLabel *exc_label = nullptr;
    QListWidget *exc_list = nullptr;

    // ---- 跟随数据（§5.2）----
    QStringList batch_files;
    QList<MetaSelectionItem> items; // 跟随项（选中项；无选中 → 首个文件）
    bool from_selection = false;    // false = 无选中 → 首个文件（提示 "(1/N)"）
    std::vector<ProbeSnapshot> probes;
    bool probe_pending = false;
    quint64 probe_generation = 0;
    int probe_stale_dropped = 0;
};

MetaState *state_of(const PageMeta *page) {
    return static_cast<MetaState *>(page->property(kStateProperty).value<QObject *>());
}

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------

bool parse_coord(const QLineEdit *edit, double lo, double hi, double &out) {
    bool ok = false;
    const double v = edit->text().trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(v) || v < lo || v > hi) {
        return false;
    }
    out = v;
    return true;
}

std::optional<double> parse_optional(const QLineEdit *edit, double lo, double hi) {
    const QString text = edit->text().trimmed();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    bool ok = false;
    const double v = text.toDouble(&ok);
    if (!ok || !std::isfinite(v) || v < lo || v > hi) {
        return std::nullopt;
    }
    return v;
}

QString format_6(double v) { return QString::number(v, 'f', 6); }

// 时区下拉：UTC-12:00..UTC+14:00 整点 + 半时区；显示 "UTC+08:00"，userData = 分钟数
void fill_timezone_combo(QComboBox *combo) {
    std::vector<int> offsets;
    for (int h = -12; h <= 14; ++h) {
        offsets.push_back(h * 60);
    }
    for (const int half : {-570, -210, 210, 270, 330, 390, 570, 630}) {
        offsets.push_back(half);
    }
    std::sort(offsets.begin(), offsets.end());
    for (const int off : offsets) {
        combo->addItem(
            QStringLiteral("UTC%1").arg(QString::fromStdString(pp::offset_time_string(off))), off);
    }
}

void select_timezone(QComboBox *combo, int offset_min) {
    const int idx = combo->findData(offset_min);
    combo->setCurrentIndex(idx >= 0 ? idx : combo->findData(480));
}

void notify_rules_changed(MetaState *st) {
    if (!st->loading) {
        emit st->page->rules_changed();
    }
}

// 自验钩子（动态属性）：同时挂在 MetaState 子对象（.cache/tmp 自验程序）与页面本身
// （--ui-smoke 探针只持有 PageMeta*，见 smoke_probe_meta）——非用户可见的只读读数。
void set_hook(MetaState *st, const char *name, const QVariant &value) {
    st->setProperty(name, value);
    if (st->page != nullptr) {
        st->page->setProperty(name, value);
    }
}

// ---------------------------------------------------------------------------
// 规则装配（**唯一**规则源：rules() 与生效值显示都走这里 → 显示=写入）
// ---------------------------------------------------------------------------

pp::TimeShift current_time_shift(const MetaState *st) {
    pp::TimeShift shift;
    if (st->seg_delta->isChecked()) {
        shift.mode = pp::TimeShift::Mode::Delta;
        shift.years = st->delta[0]->value();
        shift.months = st->delta[1]->value();
        shift.days = st->delta[2]->value();
        shift.hours = st->delta[3]->value();
        shift.minutes = st->delta[4]->value();
        shift.seconds = st->delta[5]->value();
    } else {
        shift.mode = pp::TimeShift::Mode::TimezoneSemantic;
        shift.from_offset_min = st->tz_from->currentData().toInt();
        shift.to_offset_min = st->tz_to->currentData().toInt();
    }
    return shift;
}

pp::BatchRules build_rules(const MetaState *st) {
    pp::BatchRules rules;
    // 时间：全零 Δ / from==to 时区 = noop（写路径与显示路径同一判据跳过，§5.1）
    rules.time_shift = current_time_shift(st);
    // 清除 GPS 与 gps 互斥（gps_clear 优先）
    if (st->clear_gps->isChecked()) {
        rules.gps_clear = true;
    } else if (st->gps_enable->isChecked()) {
        double lat = 0, lon = 0;
        if (parse_coord(st->lat, -90.0, 90.0, lat) && parse_coord(st->lon, -180.0, 180.0, lon)) {
            pp::GpsData gps;
            gps.lat = lat;
            gps.lon = lon;
            gps.altitude = parse_optional(st->alt, -100000.0, 100000.0);
            gps.direction = parse_optional(st->dir, 0.0, 359.99);
            const QString stamp = st->ts->text().trimmed();
            if (!stamp.isEmpty()) {
                gps.timestamp = stamp.toStdString();
            }
            rules.gps = gps;
        }
        // 非法坐标 → 忽略 gps（黄字提示由 refresh_gps_warning 负责）
    }
    for (int i = 0; i < st->tag_rows->count(); ++i) {
        QWidget *host = st->tag_rows->itemAt(i)->widget();
        if (host == nullptr) {
            continue;
        }
        const auto *key_edit = host->findChild<QLineEdit *>(QStringLiteral("tag_key"));
        const auto *value_edit = host->findChild<QLineEdit *>(QStringLiteral("tag_value"));
        if (key_edit == nullptr || value_edit == nullptr) {
            continue;
        }
        const std::string key = key_edit->text().trimmed().toStdString();
        if (key.empty()) {
            continue;
        }
        const QString value = value_edit->text();
        pp::TagEdit edit;
        edit.key = key;
        if (value.isEmpty()) {
            edit.remove = true; // 值留空 = 删除该标签
        } else {
            edit.value = value.toStdString();
        }
        if (key.rfind("Xmp.", 0) == 0) {
            rules.xmp_edits.push_back(std::move(edit));
        } else {
            rules.exif_edits.push_back(std::move(edit));
        }
    }
    rules.strip_privacy = st->strip->isChecked();
    rules.sync_mtime = st->mtime->isChecked();
    return rules;
}

// ---------------------------------------------------------------------------
// 跟随项 probe（异步；M2-T7 序号守卫口径：过期结果一律丢弃、不得乱序回填）
// ---------------------------------------------------------------------------

std::vector<ProbeSnapshot> scan_probes(const QStringList &paths) {
    std::vector<ProbeSnapshot> out;
    out.reserve(std::size_t(paths.size()));
    for (const QString &path : paths) {
        ProbeSnapshot snapshot;
        snapshot.path = path;
        snapshot.meta = pp::read_metadata(std::filesystem::path(path.toStdString()));
        snapshot.ok = snapshot.meta.error.empty();
        out.push_back(std::move(snapshot));
    }
    return out;
}

void refresh_effective(MetaState *st); // 定义见下（扫描回填要用）

void apply_probe_result(MetaState *st, quint64 generation,
                        const std::vector<ProbeSnapshot> &probes) {
    if (generation != st->probe_generation) {
        ++st->probe_stale_dropped;
        set_hook(st, "pp_meta_follow_stale_dropped", st->probe_stale_dropped);
        return;
    }
    st->probes = probes;
    st->probe_pending = false;
    set_hook(st, "pp_meta_follow_pending", false);
    refresh_effective(st);
}

// 只对**跟随项**取 probe（≤ 8：多选小表的行数上限；单文件跟随 = 1 次读）。
// GUI 线程只做值拷贝 + 挂 watcher，立即返回；结果回填前校验序号。
void request_probe_scan(MetaState *st) {
    QStringList paths;
    const int limit = std::min(static_cast<int>(st->items.size()), kMultiRowCap);
    for (int i = 0; i < limit; ++i) {
        paths << st->items.at(i).path;
    }
    const quint64 generation = ++st->probe_generation;
    st->probes.clear();
    st->probe_pending = !paths.isEmpty();
    set_hook(st, "pp_meta_follow_pending", st->probe_pending);
    set_hook(st, "pp_meta_follow_name", st->items.isEmpty() ? QString() : st->items.first().path);
    set_hook(st, "pp_meta_follow_count", static_cast<int>(st->items.size()));
    if (paths.isEmpty()) {
        refresh_effective(st);
        return;
    }
    auto *watcher = new QFutureWatcher<std::vector<ProbeSnapshot>>(st);
    QObject::connect(watcher, &QFutureWatcher<std::vector<ProbeSnapshot>>::finished, st,
                     [st, watcher, generation] {
                         const std::vector<ProbeSnapshot> result = watcher->future().result();
                         watcher->deleteLater();
                         apply_probe_result(st, generation, result);
                     });
    watcher->setFuture(QtConcurrent::run([paths] { return scan_probes(paths); }));
    refresh_effective(st); // 在途：先清空显示（不留上一批的陈旧文本）
}

// ---------------------------------------------------------------------------
// 生效值刷新（纯函数重算；每次规则变化即时刷新）
// ---------------------------------------------------------------------------

QString follow_hint(const MetaState *st) {
    if (st->items.isEmpty()) {
        return PageMeta::tr("无文件");
    }
    const QString name = QFileInfo(st->items.first().path).fileName();
    const int total = st->batch_files.size();
    const int index = st->batch_files.indexOf(st->items.first().path) + 1; // 1-based；0 = 未找到
    if (st->items.size() >= 2) {
        return PageMeta::tr("已选 %1 个 · %2").arg(st->items.size()).arg(name);
    }
    if (total > 0 && index > 0) {
        return QStringLiteral("%1 (%2/%3)").arg(name).arg(index).arg(total);
    }
    return name;
}

// 生效值为空时的占位文案（§5.1/§5.2；优先级：剥除 > 清除 > 无）
QString empty_effective_hint(const MetaState *st, bool strip) {
    if (strip) {
        return PageMeta::tr(kWillRemoveText);
    }
    if (st->clear_gps->isChecked()) {
        return PageMeta::tr(kGpsClearPlaceholder);
    }
    return PageMeta::tr(kNoGpsPlaceholder);
}

void refresh_info_card(MetaState *st, const std::vector<pp::EffectivePreview> &previews) {
    if (previews.empty()) {
        for (ElidedLabel *label : st->info_value) {
            label->set_full_text(QString());
        }
        return;
    }
    const MetaSelectionItem &item = st->items.first();
    const ProbeSnapshot &probe = st->probes.front();
    const pp::EffectivePreview &preview = previews.front();
    const pp::ImageInfo &info = item.info;

    // 拍摄时间 = 源 DateTimeOriginal（只读源事实；生效值在时间卡里对照显示）
    st->info_value[0]->set_full_text(probe.ok ? datetime_text(preview.datetime_original.original)
                                              : QStringLiteral("—"));
    st->info_value[1]->set_full_text(probe.ok ? camera_text(probe.meta.exif) : QStringLiteral("—"));
    st->info_value[2]->set_full_text(probe.ok ? lens_text(probe.meta.exif) : QStringLiteral("—"));
    st->info_value[3]->set_full_text(probe.ok ? exposure_text(probe.meta.exif)
                                              : QStringLiteral("—"));
    if (item.probe_ok) {
        st->info_value[4]->set_full_text(QStringLiteral("%1×%2 · %3 bit · %4")
                                             .arg(info.width)
                                             .arg(info.height)
                                             .arg(info.src_bitdepth)
                                             .arg(channels_text(info)));
        st->info_value[5]->set_full_text(info.has_icc ? PageMeta::tr("内嵌 ICC 配置文件")
                                                      : PageMeta::tr("无 ICC"));
        st->info_value[6]->set_full_text(QStringLiteral("%1 · %2").arg(
            format_size(item.file_size), QString::fromStdString(info.format).toUpper()));
    } else {
        st->info_value[4]->set_full_text(QStringLiteral("—"));
        st->info_value[5]->set_full_text(QStringLiteral("—"));
        st->info_value[6]->set_full_text(format_size(item.file_size));
    }
    if (preview.gps.original.has_value()) {
        st->info_value[7]->set_full_text(QStringLiteral("%1, %2  %3")
                                             .arg(coord_text(preview.gps.original->lat, 5),
                                                  coord_text(preview.gps.original->lon, 5),
                                                  PageMeta::tr("已有")));
    } else {
        st->info_value[7]->set_full_text(probe.ok ? PageMeta::tr(kNoneText) : QStringLiteral("—"));
    }
}

// 清空多选小表（**保留** multi_more —— 它是常驻子控件、仅在有截断行时才入列，重复入列/
// 误删会造成 use-after-free，见 M4-T12 自验抓到的堆损坏）
void clear_multi_rows(MetaState *st) {
    if (st->multi_grid == nullptr) {
        return;
    }
    while (QLayoutItem *item = st->multi_grid->takeAt(0)) {
        if (item->widget() != nullptr && item->widget() != st->multi_more) {
            delete item->widget();
        }
        delete item;
    }
}

void refresh_time_section(MetaState *st, const std::vector<pp::EffectivePreview> &previews) {
    const bool multi = previews.size() >= 2;
    st->time_rows_host->setVisible(!multi);
    st->multi_host->setVisible(multi);

    if (previews.empty()) {
        for (TimeRowWidgets &row : st->time_row) {
            row.old->set_full_text(QString());
            row.fresh->set_full_text(QString());
            row.src->set_full_text(QString());
            theme::set_mod(row.fresh, false);
        }
        clear_multi_rows(st);
        st->multi_more->set_full_text(QString());
        st->multi_more->setVisible(false);
        set_hook(st, "pp_meta_multi_rows", 0);
        set_hook(st, "pp_meta_time_new", QString());
        return;
    }

    if (!multi) {
        const pp::EffectivePreview &preview = previews.front();
        const std::array<const pp::EffectiveField<std::optional<pp::DateTimeVal>> *, 3> fields = {
            &preview.datetime_original, &preview.datetime_digitized, &preview.datetime_modify};
        for (std::size_t i = 0; i < fields.size(); ++i) {
            TimeRowWidgets &row = st->time_row[i];
            row.old->set_full_text(datetime_text(fields[i]->original));
            row.fresh->set_full_text(preview.strip_privacy ? PageMeta::tr(kWillRemoveText)
                                                           : datetime_text(fields[i]->effective));
            row.src->set_full_text(QString::fromLatin1(kTimeSourceLabels[i]));
            theme::set_mod(row.fresh, fields[i]->changed);
        }
        set_hook(st, "pp_meta_time_new",
                 st->time_row[0].fresh->full_text()); // 自验钩子（探针读生效时间）
        set_hook(st, "pp_meta_multi_rows", 0);
        return;
    }

    // 多选（≥2）：逐行小表（文件名 | 原 → 生效），上限 8 行 + 「…还有 N 个」（§5.2）
    if (st->multi_grid == nullptr) {
        return;
    }
    clear_multi_rows(st);
    // 小表行数上限 = kMultiRowCap；"…还有 N 个" 的 N 按**选中项总数**算（probe 只覆盖前 8 个）
    const int total =
        std::max(static_cast<int>(st->items.size()), static_cast<int>(previews.size()));
    const int rows = std::min(static_cast<int>(previews.size()), kMultiRowCap);
    for (int i = 0; i < rows; ++i) {
        const pp::EffectivePreview &preview = previews[std::size_t(i)];
        const QString name = QFileInfo(st->items.at(i).path).fileName();
        auto *name_label = new ElidedLabel(st->multi_host);
        name_label->setObjectName(QStringLiteral("pp-meta-multi-name-%1").arg(i));
        name_label->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        name_label->setMinimumHeight(theme::Metrics::form_row_h);
        name_label->set_full_text(name);
        auto *old_label = new ElidedLabel(st->multi_host);
        old_label->setObjectName(QStringLiteral("pp-meta-multi-old-%1").arg(i));
        QFont old_font = theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true);
        old_font.setStrikeOut(true);
        old_label->setFont(old_font);
        old_label->setMinimumHeight(theme::Metrics::form_row_h);
        old_label->set_full_text(datetime_text(preview.datetime_original.original));
        auto *arrow = new QLabel(QStringLiteral("→"), st->multi_host);
        arrow->setObjectName(QStringLiteral("pp-meta-multi-arrow"));
        arrow->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        auto *new_label = new ElidedLabel(st->multi_host);
        new_label->setObjectName(QStringLiteral("pp-meta-multi-new-%1").arg(i));
        new_label->setFont(theme::font(theme::Typography::body_px, QFont::Bold, /*tabular=*/true));
        new_label->setMinimumHeight(theme::Metrics::form_row_h);
        new_label->set_full_text(preview.strip_privacy
                                     ? PageMeta::tr(kWillRemoveText)
                                     : datetime_text(preview.datetime_original.effective));
        theme::set_mod(new_label, preview.datetime_original.changed);
        st->multi_grid->addWidget(name_label, i, 0);
        st->multi_grid->addWidget(old_label, i, 1);
        st->multi_grid->addWidget(arrow, i, 2);
        st->multi_grid->addWidget(new_label, i, 3);
    }
    const int rest = total - rows;
    st->multi_more->set_full_text(rest > 0 ? PageMeta::tr("…还有 %1 个").arg(rest) : QString());
    st->multi_more->setVisible(rest > 0);
    if (rest > 0) {
        st->multi_grid->addWidget(st->multi_more, rows, 0, 1, 4);
    }
    set_hook(st, "pp_meta_multi_rows", rows);
    // 自验钩子：多选时生效时间不落在"单文件生效时间"钩子上（探针据此区分两种形态）
    set_hook(st, "pp_meta_time_new", rows > 0 ? QStringLiteral("<multi>") : QString());
}

void refresh_gps_section(MetaState *st, const pp::EffectivePreview *preview) {
    const bool strip = preview != nullptr && preview->strip_privacy;
    const bool locked = st->clear_gps->isChecked();
    const std::optional<pp::GpsData> effective =
        (preview != nullptr && !strip) ? preview->gps.effective : std::nullopt;
    const QString placeholder = empty_effective_hint(st, strip);
    const bool changed = preview != nullptr && preview->gps.changed;
    const auto set_view = [&](QLineEdit *edit, const QString &text) {
        edit->setText(text);
        edit->setPlaceholderText(text.isEmpty() ? placeholder : QString());
        edit->setToolTip(QString());
    };
    if (effective.has_value()) {
        set_view(st->view_lat, coord_text(effective->lat, 6));
        set_view(st->view_lon, coord_text(effective->lon, 6));
        set_view(st->view_alt,
                 effective->altitude.has_value()
                     ? QStringLiteral("%1 m").arg(QString::number(*effective->altitude, 'f', 1))
                     : QString());
        set_view(st->view_dir, effective->direction.has_value()
                                   ? coord_text(*effective->direction, 1)
                                   : QString());
        set_view(st->view_ts, effective->timestamp.has_value()
                                  ? QString::fromStdString(*effective->timestamp)
                                  : QString());
        // DMS 读数（v0.2 的纬度/经度秒级读数）收进悬浮提示：不占 mockup 的控件面，信息不丢
        const QString dms = dms_string(effective->lat, effective->lon);
        st->view_lat->setToolTip(dms);
        st->view_lon->setToolTip(dms);
    } else {
        set_view(st->view_lat, QString());
        set_view(st->view_lon, QString());
        set_view(st->view_alt, QString());
        set_view(st->view_dir, QString());
        set_view(st->view_ts, QString());
    }
    // .mod（§9.2）：生效坐标与原值不同 → 强调边框+强调色字
    theme::set_mod(st->view_lat, changed && !st->view_lat->text().isEmpty());
    theme::set_mod(st->view_lon, changed && !st->view_lon->text().isEmpty());
    // D4：只显生效值**单钉**；无生效值 → 不画钉、不 center_on（保持当前视口/选点位置）
    if (effective.has_value()) {
        st->map->set_marker(effective->lat, effective->lon);
        st->map->center_on(effective->lat, effective->lon);
        set_hook(st, "pp_meta_gps_effective",
                 QStringLiteral("%1,%2").arg(QString::number(effective->lat, 'f', 6),
                                             QString::number(effective->lon, 'f', 6)));
    } else {
        st->map->clear_marker();
        set_hook(st, "pp_meta_gps_effective", QString());
    }
    Q_UNUSED(locked);
}

void refresh_effective(MetaState *st) {
    st->info_hint->set_full_text(follow_hint(st));
    st->time_hint->set_full_text(PageMeta::tr("影响 %1 / %2 个文件")
                                     .arg(st->batch_files.size())
                                     .arg(st->batch_files.size()));
    if (st->probe_pending) {
        refresh_info_card(st, {});
        refresh_time_section(st, {});
        refresh_gps_section(st, nullptr);
        return;
    }
    const pp::BatchRules rules = build_rules(st);
    std::vector<pp::EffectivePreview> previews;
    previews.reserve(st->probes.size());
    for (std::size_t i = 0; i < st->probes.size(); ++i) {
        const std::optional<pp::MetadataOverride> exception =
            i < std::size_t(st->items.size()) ? st->items.at(int(i)).exception
                                              : std::optional<pp::MetadataOverride>();
        previews.push_back(
            pp::preview_effective(st->probes[i].meta, rules, exception)); // §5.1 唯一合成源
    }
    refresh_info_card(st, previews);
    refresh_time_section(st, previews);
    refresh_gps_section(st, previews.empty() ? nullptr : &previews.front());
}

// ---------------------------------------------------------------------------
// 控件可用性 / 提示
// ---------------------------------------------------------------------------

void refresh_time_inputs(MetaState *st) {
    // 时间卡无「启用」开关（见头注释）：seg 选中的那一组恒可用，另一组隐藏
    const bool delta_mode = st->seg_delta->isChecked();
    st->delta_row->setVisible(delta_mode);
    st->tz_row->setVisible(!delta_mode);
    for (QSpinBox *spin : st->delta) {
        spin->setEnabled(true);
    }
    st->tz_from->setEnabled(true);
    st->tz_to->setEnabled(true);
    // .mod（mockup .spin.mod：改动值 = 强调边框+强调色字+700）
    for (QSpinBox *spin : st->delta) {
        theme::set_mod(spin, spin->value() != 0);
    }
    const bool tz_changed = st->tz_from->currentData().toInt() != st->tz_to->currentData().toInt();
    theme::set_mod(st->tz_from, tz_changed);
    theme::set_mod(st->tz_to, tz_changed);
}

// GPS 勾选但坐标非法 → 卡片顶部黄字提示（§2.11 冻结文案）
void refresh_gps_warning(MetaState *st) {
    double lat = 0, lon = 0;
    const bool valid =
        parse_coord(st->lat, -90.0, 90.0, lat) && parse_coord(st->lon, -180.0, 180.0, lon);
    const bool show = st->gps_enable->isChecked() && !st->clear_gps->isChecked() && !valid;
    st->gps_warn->setVisible(show);
}

void refresh_gps_inputs(MetaState *st) {
    const bool locked = st->clear_gps->isChecked();
    const bool rule_on = st->gps_enable->isChecked();
    const bool editable = rule_on && !locked;
    st->gps_enable->setEnabled(!locked);
    st->lat->setEnabled(editable);
    st->lon->setEnabled(editable);
    st->alt->setEnabled(editable);
    st->dir->setEnabled(editable);
    st->ts->setEnabled(editable);
    st->more_toggle->setEnabled(editable);
    // §9.1 FIX2：占位文本跟随各框自身可用性（不新造文案）
    st->lat->setPlaceholderText(editable ? PageMeta::tr(kLatPlaceholder) : QString());
    st->lon->setPlaceholderText(editable ? PageMeta::tr(kLonPlaceholder) : QString());
    st->search_edit->setPlaceholderText(locked ? QString() : PageMeta::tr(kSearchPlaceholder));
    st->search_edit->setEnabled(!locked);
    st->search_btn->setEnabled(!locked);
    st->search_results->setEnabled(!locked);
    st->read_gps->setEnabled(!locked && st->from_selection && !st->items.isEmpty());
    st->pick_map->setEnabled(!locked);
    st->rule_toggle->setText(rule_on ? PageMeta::tr("批量 GPS 规则 · 已启用 ▾")
                                     : PageMeta::tr("批量 GPS 规则 ▾"));
    if (locked) {
        st->gps_status->setText(PageMeta::tr("已勾选清除 GPS：输出将不含 GPS"));
    }
}

void refresh_all(MetaState *st) {
    refresh_time_inputs(st);
    refresh_gps_inputs(st);
    refresh_gps_warning(st);
    refresh_effective(st);
}

// ---------------------------------------------------------------------------
// apply_rules（预设载入；不发信号）
// ---------------------------------------------------------------------------

void clear_tag_rows(MetaState *st) {
    while (st->tag_rows->count() > 0) {
        QLayoutItem *item = st->tag_rows->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
}

void add_tag_row(MetaState *st, const QString &key, const QString &value) {
    auto *row = new QWidget();
    row->setObjectName(QStringLiteral("tag_row"));
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);

    auto *key_edit = new QLineEdit(row);
    key_edit->setObjectName(QStringLiteral("tag_key"));
    key_edit->setPlaceholderText(PageMeta::tr("Exif.Image.Artist 或 Xmp.dc.title"));
    key_edit->setText(key);
    auto *value_edit = new QLineEdit(row);
    value_edit->setObjectName(QStringLiteral("tag_value"));
    value_edit->setText(value);
    value_edit->setToolTip(PageMeta::tr("值留空 = 删除该标签"));
    auto *remove = new QPushButton(QStringLiteral("✕"), row);
    remove->setObjectName(QStringLiteral("tag_remove"));
    remove->setFixedWidth(28);
    remove->setToolTip(PageMeta::tr("删除此行"));

    lay->addWidget(key_edit, 2);
    lay->addWidget(value_edit, 3);
    lay->addWidget(remove, 0);
    st->tag_rows->addWidget(row);

    QObject::connect(key_edit, &QLineEdit::textChanged, st, [st] {
        notify_rules_changed(st);
        refresh_effective(st);
    });
    QObject::connect(value_edit, &QLineEdit::textChanged, st, [st] {
        notify_rules_changed(st);
        refresh_effective(st);
    });
    QObject::connect(remove, &QPushButton::clicked, st, [st, row] {
        st->tag_rows->removeWidget(row);
        row->hide();
        row->deleteLater();
        notify_rules_changed(st);
        refresh_effective(st);
    });
}

void apply_state(MetaState *st, const pp::BatchRules &r) {
    const bool prev_loading = st->loading;
    st->loading = true;

    if (r.time_shift.has_value()) {
        const pp::TimeShift &s = *r.time_shift;
        const bool delta_mode = s.mode == pp::TimeShift::Mode::Delta;
        st->seg_delta->setChecked(delta_mode);
        st->seg_tz->setChecked(!delta_mode);
        st->delta[0]->setValue(s.years);
        st->delta[1]->setValue(s.months);
        st->delta[2]->setValue(s.days);
        st->delta[3]->setValue(s.hours);
        st->delta[4]->setValue(s.minutes);
        st->delta[5]->setValue(s.seconds);
        select_timezone(st->tz_from, s.from_offset_min);
        select_timezone(st->tz_to, s.to_offset_min);
    } else {
        // 无时间规则 = 全零 Δ（noop；与"未配置"在两条路径上等价）
        st->seg_delta->setChecked(true);
        st->seg_tz->setChecked(false);
        for (QSpinBox *spin : st->delta) {
            spin->setValue(0);
        }
    }

    st->clear_gps->setChecked(r.gps_clear);
    st->gps_enable->setChecked(r.gps.has_value());
    if (r.gps.has_value()) {
        st->lat->setText(format_6(r.gps->lat));
        st->lon->setText(format_6(r.gps->lon));
        st->alt->setText(r.gps->altitude.has_value() ? format_6(*r.gps->altitude) : QString());
        st->dir->setText(r.gps->direction.has_value() ? format_6(*r.gps->direction) : QString());
        st->ts->setText(r.gps->timestamp.has_value() ? QString::fromStdString(*r.gps->timestamp)
                                                     : QString());
        st->gps_status->setText(PageMeta::tr("已载入批量 GPS 规则"));
    } else {
        st->lat->clear();
        st->lon->clear();
        st->alt->clear();
        st->dir->clear();
        st->ts->clear();
    }

    clear_tag_rows(st);
    for (const pp::TagEdit &edit : r.exif_edits) {
        add_tag_row(st, QString::fromStdString(edit.key),
                    edit.value.has_value() ? QString::fromStdString(*edit.value) : QString());
    }
    for (const pp::TagEdit &edit : r.xmp_edits) {
        add_tag_row(st, QString::fromStdString(edit.key),
                    edit.value.has_value() ? QString::fromStdString(*edit.value) : QString());
    }

    st->strip->setChecked(r.strip_privacy);
    st->mtime->setChecked(r.sync_mtime);

    st->loading = prev_loading;
    refresh_all(st);
}

// ---------------------------------------------------------------------------
// 样式（tokens 单源 = ui/theme.h；本页只把 tokens 铺到自己的控件上）
// ---------------------------------------------------------------------------

constexpr const char *kFieldProperty = "ppMetaField";

void apply_style(MetaState *st) {
    const theme::Tokens &t = st->tokens;
    const auto css = [](const QColor &c) { return theme::css_color(c); };
    const QString mod = QString::fromLatin1(kFieldProperty);

    QString qss;
    // 卡片头/关键信息行（.kv-k / .kv-v）
    qss += QStringLiteral("QLabel#pp-meta-info-key { color: ") + css(t.text3) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel#pp-meta-info-value { color: ") + css(t.text) +
           QStringLiteral("; }\n");
    // 分段控件（mockup .seg：--ctl 底 + r=6 + 内 2px；选中格 = 亮底 + --txt）
    qss += QStringLiteral("QWidget#pp-meta-seg { background: ") + css(t.control) +
           QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: 6px; }\n");
    qss += QStringLiteral("QToolButton[") + mod +
           QStringLiteral("=\"seg\"] { background: "
                          "transparent; border: none; border-radius: 4px; color: ") +
           css(t.text2) + QStringLiteral("; padding: 4px 16px; font-weight: 600; }\n");
    qss += QStringLiteral("QToolButton[") + mod +
           QStringLiteral("=\"seg\"]:checked { background: ") +
           css(t.dark() ? QColor(255, 255, 255, 26) : QColor(255, 255, 255)) +
           QStringLiteral("; color: ") + css(t.text) + QStringLiteral("; }\n");
    // 数值框（mockup .spin：--ctl 底 + --ctl-bd 边 + r=4 + tabular）
    qss += QStringLiteral("QSpinBox[") + mod + QStringLiteral("=\"spin\"] { background: ") +
           css(t.control) + QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: 4px; color: ") + css(t.text) + QStringLiteral("; }\n");
    qss += QStringLiteral("QComboBox[") + mod + QStringLiteral("=\"combo\"] { background: ") +
           css(t.control) + QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: 4px; color: ") + css(t.text) +
           QStringLiteral("; padding: 0 6px; }\n");
    // 生效值显示框（mockup .gps-in：--ctl 底 + --ctl-bd 边 + r=4）
    qss += QStringLiteral("QLineEdit[") + mod + QStringLiteral("=\"view\"] { background: ") +
           css(t.control) + QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: 4px; color: ") + css(t.text) +
           QStringLiteral("; padding: 0 8px; }\n");
    // 生效对照块（mockup .effect：--card-hi 底 + --card-bd 边 + r=6）
    qss += QStringLiteral("QFrame#pp-meta-effect { background: ") + css(t.card_hi) +
           QStringLiteral("; border: 1px solid ") + css(t.card_bd) +
           QStringLiteral("; border-radius: 6px; }\n");
    qss += QStringLiteral("QLabel#pp-meta-effect-title { color: ") + css(t.text3) +
           QStringLiteral("; }\n");
    // 原（删除线）→ 生效（强调色）（mockup .tline；删除线在 QFont 侧落地 —— Qt QSS 无
    // text-decoration）
    qss += QStringLiteral("QLabel[") + mod + QStringLiteral("=\"old\"] { color: ") + css(t.text2) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + mod + QStringLiteral("=\"new\"] { color: ") + css(t.accent) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + mod + QStringLiteral("=\"arrow\"] { color: ") +
           css(t.text3) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + mod + QStringLiteral("=\"src\"] { color: ") + css(t.text3) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + mod + QStringLiteral("=\"multi-name\"] { color: ") +
           css(t.text) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + mod + QStringLiteral("=\"multi-more\"] { color: ") +
           css(t.text3) + QStringLiteral("; }\n");
    // 折叠入口 / 链接（mockup .link = accent 600；.switch-row 的 hint 文案）
    qss += QStringLiteral("QToolButton[") + mod +
           QStringLiteral("=\"fold\"] { border: none; "
                          "background: transparent; color: ") +
           css(t.text2) + QStringLiteral("; font-weight: 600; }\n");
    qss += QStringLiteral("QToolButton[") + mod +
           QStringLiteral("=\"link\"] { border: none; "
                          "background: transparent; color: ") +
           css(t.accent) + QStringLiteral("; font-weight: 600; }\n");
    qss += QStringLiteral("QLabel[") + mod + QStringLiteral("=\"sub\"] { color: ") + css(t.text3) +
           QStringLiteral("; }\n");
    // 危险动作（mockup .btn.danger = --err 字）
    qss += QStringLiteral("QPushButton[") + mod + QStringLiteral("=\"danger\"] { color: ") +
           css(t.err) + QStringLiteral("; }\n");
    // 卡片间距（§9.3 卡片间 10px = mockup .scroll{gap:10px}）
    qss += QStringLiteral("QWidget#pp-meta-content { background: transparent; }\n");
    st->page->setStyleSheet(qss);

    // 浅色卡阴影（§9.2 0 1px 4px rgba(16,24,40,.06)；深色无阴影）
    const qreal dpr = st->page->devicePixelRatioF() > 0.01 ? st->page->devicePixelRatioF() : 1.0;
    for (QFrame *card : st->cards) {
        if (card == nullptr) {
            continue;
        }
        if (t.shadow_blur > 0) {
            auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(card->graphicsEffect());
            if (shadow == nullptr) {
                shadow = new QGraphicsDropShadowEffect(card);
                card->setGraphicsEffect(shadow);
            }
            shadow->setColor(t.shadow_color);
            shadow->setBlurRadius(double(t.shadow_blur) * dpr);
            shadow->setXOffset(0);
            shadow->setYOffset(double(t.shadow_dy) * dpr);
        } else if (card->graphicsEffect() != nullptr) {
            card->setGraphicsEffect(nullptr); // 删旧效果（setGraphicsEffect(nullptr) 会析构它）
        }
    }
}

// ---------------------------------------------------------------------------
// 界面构建
// ---------------------------------------------------------------------------

QFrame *make_card(QWidget *parent) {
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("pp-card")); // theme QSS：QFrame#pp-card
    return card;
}

// 卡头（mockup .card-h）：标题（12.5px/700）+ 可选徽标（.pill）+ 右端 hint（超长省略）
QWidget *make_card_header(QWidget *parent, const QString &title, ElidedLabel **pill_out,
                          ElidedLabel **hint_out) {
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 9, 12, 7);
    layout->setSpacing(8);
    auto *label = new QLabel(title, row);
    label->setObjectName(QStringLiteral("pp-card-title"));
    label->setFont(
        theme::font(theme::Typography::card_title_px, theme::Typography::card_title_weight));
    layout->addWidget(label);
    if (pill_out != nullptr) {
        auto *pill = new ElidedLabel(row);
        pill->setProperty(theme::kPillProperty, true); // QSS：QLabel[ppPill="true"]（.pill）
        pill->setFont(theme::font(theme::Typography::badge_px, theme::Typography::badge_weight));
        pill->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        pill->setVisible(false);
        layout->addWidget(pill);
        *pill_out = pill;
    }
    layout->addStretch(1);
    if (hint_out != nullptr) {
        auto *hint = new ElidedLabel(row);
        hint->setObjectName(QStringLiteral("pp-card-hint"));
        hint->setFont(theme::font(theme::Typography::hint_px, 500));
        hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(hint, 1);
        *hint_out = hint;
    }
    return row;
}

QLabel *form_label(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("pp-meta-form-label"));
    label->setProperty(kFieldProperty, QStringLiteral("label"));
    label->setFont(theme::font(theme::Typography::small_px, QFont::Normal));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setMinimumHeight(theme::Metrics::form_row_h);
    label->setMinimumWidth(theme::Metrics::form_label_w);
    return label;
}

QWidget *build_info_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(make_card_header(card, PageMeta::tr("关键信息"), &st->info_pill, &st->info_hint));
    st->info_hint->setObjectName(QStringLiteral("pp-meta-info-hint"));
    st->info_hint->setProperty(kFieldProperty, QStringLiteral("hint"));
    st->info_pill->set_full_text(PageMeta::tr("跟随选中"));
    st->info_pill->setVisible(true);

    auto *grid = new QGridLayout();
    grid->setContentsMargins(kCardPadX, 2, kCardPadX, 12);
    grid->setHorizontalSpacing(theme::Metrics::form_col_gap);
    grid->setVerticalSpacing(0);
    const std::array<QString, 8> keys = {
        PageMeta::tr("拍摄时间"), PageMeta::tr("相机"), PageMeta::tr("镜头"), PageMeta::tr("曝光"),
        PageMeta::tr("尺寸"),     PageMeta::tr("色彩"), PageMeta::tr("文件"), PageMeta::tr("GPS")};
    const std::array<const char *, 8> hooks = {
        "pp-meta-info-time", "pp-meta-info-camera", "pp-meta-info-lens", "pp-meta-info-exposure",
        "pp-meta-info-size", "pp-meta-info-color",  "pp-meta-info-file", "pp-meta-info-gps"};
    for (int i = 0; i < 8; ++i) {
        const int row = i / 2;
        const int col = (i % 2) * 2;
        grid->addWidget(form_label(card, keys[std::size_t(i)]), row, col);
        auto *value = new ElidedLabel(card);
        value->setObjectName(QString::fromLatin1(hooks[std::size_t(i)]));
        value->setProperty(kFieldProperty, QStringLiteral("value"));
        value->setFont(theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
        value->setMinimumHeight(theme::Metrics::form_row_h);
        grid->addWidget(value, row, col + 1);
        st->info_value[std::size_t(i)] = value;
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    v->addLayout(grid);
    return card;
}

QWidget *build_time_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(make_card_header(card, PageMeta::tr("时间偏移"), nullptr, &st->time_hint));
    st->time_hint->setObjectName(QStringLiteral("pp-meta-time-hint"));
    st->time_hint->setProperty(kFieldProperty, QStringLiteral("hint"));

    // 方式分段（mockup .seg：Δ 偏移 | 时区语义）
    auto *seg_row = new QHBoxLayout();
    seg_row->setContentsMargins(kCardPadX, 0, kCardPadX, 8);
    auto *seg = new QWidget(card);
    seg->setObjectName(QStringLiteral("pp-meta-seg"));
    auto *seg_layout = new QHBoxLayout(seg);
    seg_layout->setContentsMargins(2, 2, 2, 2);
    seg_layout->setSpacing(0);
    st->seg_delta = new QToolButton(seg);
    st->seg_delta->setObjectName(QStringLiteral("pp-meta-seg-delta"));
    st->seg_delta->setText(PageMeta::tr("Δ 偏移"));
    st->seg_tz = new QToolButton(seg);
    st->seg_tz->setObjectName(QStringLiteral("pp-meta-seg-tz"));
    st->seg_tz->setText(PageMeta::tr("时区语义"));
    auto *group = new QButtonGroup(st);
    group->setExclusive(true);
    group->addButton(st->seg_delta);
    group->addButton(st->seg_tz);
    for (QToolButton *button : {st->seg_delta, st->seg_tz}) {
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setProperty(kFieldProperty, QStringLiteral("seg"));
        button->setFont(theme::font(theme::Typography::body_px, 600));
        button->setCursor(Qt::PointingHandCursor);
        seg_layout->addWidget(button);
    }
    st->seg_delta->setChecked(true);
    seg_row->addWidget(seg, 0, Qt::AlignLeft);
    seg_row->addStretch(1);
    v->addLayout(seg_row);

    // Δ 模式：年..秒（mockup .fields 紧凑条：标签 + 52×26 数值框）
    st->delta_row = new QWidget(card);
    auto *dh = new QHBoxLayout(st->delta_row);
    dh->setContentsMargins(kCardPadX, 0, kCardPadX, 6);
    dh->setSpacing(8);
    const std::array<QString, 6> names = {PageMeta::tr("年"), PageMeta::tr("月"),
                                          PageMeta::tr("日"), PageMeta::tr("时"),
                                          PageMeta::tr("分"), PageMeta::tr("秒")};
    const std::array<const char *, 6> hooks = {"pp-meta-delta-years",   "pp-meta-delta-months",
                                               "pp-meta-delta-days",    "pp-meta-delta-hours",
                                               "pp-meta-delta-minutes", "pp-meta-delta-seconds"};
    for (int i = 0; i < 6; ++i) {
        auto *cell = new QHBoxLayout();
        cell->setSpacing(5);
        auto *label = new QLabel(names[std::size_t(i)], st->delta_row);
        label->setObjectName(QStringLiteral("pp-meta-form-label"));
        label->setFont(theme::font(theme::Typography::small_px, QFont::Normal));
        cell->addWidget(label);
        st->delta[std::size_t(i)] = new QSpinBox(st->delta_row);
        st->delta[std::size_t(i)]->setObjectName(QString::fromLatin1(hooks[std::size_t(i)]));
        st->delta[std::size_t(i)]->setProperty(kFieldProperty, QStringLiteral("spin"));
        st->delta[std::size_t(i)]->setRange(-9999, 9999);
        st->delta[std::size_t(i)]->setValue(0);
        st->delta[std::size_t(i)]->setFixedSize(kSpinWidth, kSpinHeight);
        // mockup .spin = 纯数值盒（无箭头）：52px 内塞进上下箭头会把数字挤没（实测截图），
        // 故隐藏箭头（滚动/键盘/输入仍可用，见 tooltip），几何与原型逐条一致
        st->delta[std::size_t(i)]->setButtonSymbols(QAbstractSpinBox::NoButtons);
        st->delta[std::size_t(i)]->setAlignment(Qt::AlignCenter);
        st->delta[std::size_t(i)]->setToolTip(
            PageMeta::tr("偏移量（可直接输入 / 滚轮 / ↑↓ 调整）"));
        st->delta[std::size_t(i)]->setFont(
            theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
        cell->addWidget(st->delta[std::size_t(i)]);
        dh->addLayout(cell);
    }
    dh->addStretch(1);
    st->delta_row->setMinimumHeight(theme::Metrics::form_row_h);
    v->addWidget(st->delta_row);

    // 时区模式：从 / 到（同一 70px 标签轴）
    st->tz_row = new QWidget(card);
    auto *th = new QGridLayout(st->tz_row);
    th->setContentsMargins(kCardPadX, 0, kCardPadX, 6);
    th->setHorizontalSpacing(theme::Metrics::form_col_gap);
    th->setVerticalSpacing(0);
    st->tz_from = new QComboBox(st->tz_row);
    st->tz_from->setObjectName(QStringLiteral("pp-meta-tz-from"));
    st->tz_from->setProperty(kFieldProperty, QStringLiteral("combo"));
    st->tz_from->setFixedHeight(kGpsBoxHeight);
    fill_timezone_combo(st->tz_from);
    select_timezone(st->tz_from, 480);
    st->tz_to = new QComboBox(st->tz_row);
    st->tz_to->setObjectName(QStringLiteral("pp-meta-tz-to"));
    st->tz_to->setProperty(kFieldProperty, QStringLiteral("combo"));
    st->tz_to->setFixedHeight(kGpsBoxHeight);
    fill_timezone_combo(st->tz_to);
    select_timezone(st->tz_to, 480);
    th->addWidget(form_label(st->tz_row, PageMeta::tr("从")), 0, 0);
    th->addWidget(st->tz_from, 0, 1);
    th->addWidget(form_label(st->tz_row, PageMeta::tr("到")), 0, 2);
    th->addWidget(st->tz_to, 0, 3);
    th->setColumnStretch(1, 1);
    th->setColumnStretch(3, 1);
    st->tz_row->setMinimumHeight(theme::Metrics::form_row_h);
    v->addWidget(st->tz_row);
    st->tz_row->setVisible(false);

    // 生效对照块（mockup .effect）：原（删除线）→ 生效（强调色）＋右端来源字段；
    // 多选（≥2）→ 逐行小表替换（§5.2）
    st->effect = new QFrame(card);
    st->effect->setObjectName(QStringLiteral("pp-meta-effect"));
    auto *ev = new QVBoxLayout(st->effect);
    ev->setContentsMargins(12, 9, 12, 12);
    ev->setSpacing(5);
    auto *title =
        new QLabel(PageMeta::tr("生效时间 · 跟随选中文件（未修改时两值相同）"), st->effect);
    title->setObjectName(QStringLiteral("pp-meta-effect-title"));
    title->setFont(theme::font(theme::Typography::hint_px, theme::Typography::card_title_weight));
    ev->addWidget(title);

    st->time_rows_host = new QWidget(st->effect);
    auto *rv = new QVBoxLayout(st->time_rows_host);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(2);
    for (int i = 0; i < 3; ++i) {
        auto *row = new QWidget(st->time_rows_host);
        auto *rh = new QHBoxLayout(row);
        rh->setContentsMargins(0, 0, 0, 0);
        rh->setSpacing(10);
        TimeRowWidgets widgets;
        widgets.old = new ElidedLabel(row);
        widgets.old->setObjectName(QStringLiteral("pp-meta-time-old-%1").arg(i));
        widgets.old->setProperty(kFieldProperty, QStringLiteral("old"));
        QFont old_font = theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true);
        old_font.setStrikeOut(true); // mockup .tline .old{text-decoration:line-through}
        widgets.old->setFont(old_font);
        auto *arrow = new QLabel(QStringLiteral("→"), row);
        arrow->setProperty(kFieldProperty, QStringLiteral("arrow"));
        arrow->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        widgets.fresh = new ElidedLabel(row);
        widgets.fresh->setObjectName(QStringLiteral("pp-meta-time-new-%1").arg(i));
        widgets.fresh->setProperty(kFieldProperty, QStringLiteral("new"));
        widgets.fresh->setFont(
            theme::font(theme::Typography::body_px, QFont::Bold, /*tabular=*/true));
        widgets.src = new ElidedLabel(row);
        widgets.src->setObjectName(QStringLiteral("pp-meta-time-src-%1").arg(i));
        widgets.src->setProperty(kFieldProperty, QStringLiteral("src"));
        widgets.src->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        widgets.src->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        // mockup .tline：原 → 生效 按内容宽排布（gap 10），来源字段靠右（.src{margin-left:auto}）
        rh->addWidget(widgets.old, 0);
        rh->addWidget(arrow, 0);
        rh->addWidget(widgets.fresh, 0);
        rh->addStretch(1);
        rh->addWidget(widgets.src, 0);
        row->setMinimumHeight(theme::Metrics::form_row_h - 8);
        rv->addWidget(row);
        st->time_row[std::size_t(i)] = widgets;
    }
    ev->addWidget(st->time_rows_host);

    st->multi_host = new QWidget(st->effect);
    st->multi_host->setObjectName(QStringLiteral("pp-meta-multi"));
    st->multi_grid = new QGridLayout(st->multi_host);
    st->multi_grid->setContentsMargins(0, 0, 0, 0);
    st->multi_grid->setHorizontalSpacing(theme::Metrics::form_col_gap);
    st->multi_grid->setVerticalSpacing(0);
    st->multi_grid->setColumnStretch(0, 3);
    st->multi_grid->setColumnStretch(1, 3);
    st->multi_grid->setColumnStretch(2, 0);
    st->multi_grid->setColumnStretch(3, 3);
    st->multi_more = new ElidedLabel(st->multi_host);
    st->multi_more->setObjectName(QStringLiteral("pp-meta-multi-more"));
    st->multi_more->setVisible(false);
    st->multi_more->setProperty(kFieldProperty, QStringLiteral("multi-more"));
    st->multi_more->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    st->multi_host->setVisible(false);
    ev->addWidget(st->multi_host);
    v->addWidget(st->effect);

    // ---- 连接（任一控件变化 → 通知 + 生效值刷新）----
    const auto on_mode_changed = [st](bool) {
        if (st->loading) {
            return;
        }
        refresh_time_inputs(st);
        refresh_effective(st);
        notify_rules_changed(st);
    };
    QObject::connect(st->seg_delta, &QToolButton::toggled, st, on_mode_changed);
    QObject::connect(st->seg_tz, &QToolButton::toggled, st, on_mode_changed);
    for (QSpinBox *spin : st->delta) {
        QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), st, [st](int) {
            if (st->loading) {
                return;
            }
            refresh_time_inputs(st);
            refresh_effective(st);
            notify_rules_changed(st);
        });
    }
    for (QComboBox *combo : {st->tz_from, st->tz_to}) {
        QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), st, [st](int) {
            if (st->loading) {
                return;
            }
            refresh_time_inputs(st);
            refresh_effective(st);
            notify_rules_changed(st);
        });
    }
    return card;
}

QWidget *build_gps_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(make_card_header(card, PageMeta::tr("GPS"), nullptr, &st->gps_hint));
    st->gps_hint->setObjectName(QStringLiteral("pp-meta-gps-hint"));
    st->gps_hint->setProperty(kFieldProperty, QStringLiteral("hint"));
    st->gps_hint->set_full_text(PageMeta::tr("地图跟随选中文件 · 显示生效值"));

    st->gps_warn = new QLabel(PageMeta::tr("GPS 坐标无效"), card);
    st->gps_warn->setObjectName(QStringLiteral("gps_warning"));
    QPalette warn_pal = st->gps_warn->palette();
    warn_pal.setColor(QPalette::WindowText, QColor(0xcc, 0x88, 0x00));
    st->gps_warn->setPalette(warn_pal);
    st->gps_warn->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    st->gps_warn->setContentsMargins(kCardPadX, 0, kCardPadX, 4);
    st->gps_warn->setVisible(false);
    v->addWidget(st->gps_warn);

    // 生效坐标（只读显示；§5.2「GPS 卡输入框显示生效坐标」，无 GPS → 空值提示）
    const auto make_view = [&](const QString &hook) {
        auto *edit = new QLineEdit(card);
        edit->setObjectName(hook);
        edit->setProperty(kFieldProperty, QStringLiteral("view"));
        edit->setReadOnly(true);
        edit->setFixedHeight(kGpsBoxHeight);
        edit->setFont(theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
        return edit;
    };
    st->view_lat = make_view(QStringLiteral("pp-meta-gps-lat"));
    st->view_lon = make_view(QStringLiteral("pp-meta-gps-lon"));
    st->view_alt = make_view(QStringLiteral("pp-meta-gps-alt"));
    st->view_dir = make_view(QStringLiteral("pp-meta-gps-dir"));
    st->view_ts = make_view(QStringLiteral("pp-meta-gps-ts"));
    auto *gps_grid = new QGridLayout();
    gps_grid->setContentsMargins(kCardPadX, 0, kCardPadX, 8);
    gps_grid->setHorizontalSpacing(theme::Metrics::form_col_gap);
    gps_grid->setVerticalSpacing(4);
    gps_grid->addWidget(form_label(card, PageMeta::tr("纬度")), 0, 0);
    gps_grid->addWidget(st->view_lat, 0, 1);
    gps_grid->addWidget(form_label(card, PageMeta::tr("经度")), 0, 2);
    gps_grid->addWidget(st->view_lon, 0, 3);
    gps_grid->addWidget(form_label(card, PageMeta::tr("海拔")), 1, 0);
    gps_grid->addWidget(st->view_alt, 1, 1);
    gps_grid->addWidget(form_label(card, PageMeta::tr("方位")), 1, 2);
    gps_grid->addWidget(st->view_dir, 1, 3);
    gps_grid->addWidget(form_label(card, PageMeta::tr("时间戳")), 2, 0);
    gps_grid->addWidget(st->view_ts, 2, 1, 1, 3);
    gps_grid->setColumnStretch(1, 1);
    gps_grid->setColumnStretch(3, 1);
    v->addLayout(gps_grid);

    // 搜索行（地图正上方；§2.11 保留）
    auto *search_row = new QHBoxLayout();
    search_row->setContentsMargins(kCardPadX, 0, kCardPadX, 6);
    search_row->setSpacing(8);
    st->search_edit = new QLineEdit(card);
    st->search_edit->setObjectName(QStringLiteral("pp-meta-gps-search-edit"));
    st->search_edit->setProperty(kFieldProperty, QStringLiteral("view"));
    st->search_edit->setPlaceholderText(PageMeta::tr(kSearchPlaceholder));
    st->search_edit->setFixedHeight(kGpsBoxHeight);
    st->search_btn = new QPushButton(PageMeta::tr("搜索"), card);
    st->search_btn->setObjectName(QStringLiteral("pp-meta-gps-search-button"));
    st->search_results = new QComboBox(card);
    st->search_results->setObjectName(QStringLiteral("pp-meta-gps-search-results"));
    st->search_results->setProperty(kFieldProperty, QStringLiteral("combo"));
    st->search_results->setFixedHeight(kGpsBoxHeight);
    search_row->addWidget(st->search_edit, 2);
    search_row->addWidget(st->search_btn, 0);
    search_row->addWidget(st->search_results, 3);
    v->addLayout(search_row);

    // 地图：固定高 196（mockup .map-box）
    auto *map_host = new QHBoxLayout();
    map_host->setContentsMargins(kCardPadX, 0, kCardPadX, 8);
    st->map = new pp::map::MapWidget(card);
    st->map->setObjectName(QStringLiteral("pp-meta-map"));
    st->map->setFixedHeight(kMapHeight);
    map_host->addWidget(st->map);
    v->addLayout(map_host);

    // 三按钮（mockup .btn-row：从选中文件读取 GPS / 地图选点 / 清除 GPS）
    auto *btn_row = new QHBoxLayout();
    btn_row->setContentsMargins(kCardPadX, 0, kCardPadX, 8);
    btn_row->setSpacing(8);
    st->read_gps = new QPushButton(PageMeta::tr("从选中文件读取 GPS"), card);
    st->read_gps->setObjectName(QStringLiteral("pp-meta-gps-read"));
    st->read_gps->setEnabled(false);
    st->pick_map = new QPushButton(PageMeta::tr("地图选点"), card);
    st->pick_map->setObjectName(QStringLiteral("pp-meta-gps-pick"));
    st->pick_map->setToolTip(PageMeta::tr("取地图中心为坐标（可先拖拽地图定位）"));
    st->clear_gps = new QPushButton(PageMeta::tr("清除 GPS"), card);
    st->clear_gps->setObjectName(QStringLiteral("pp-meta-gps-clear"));
    st->clear_gps->setProperty(kFieldProperty, QStringLiteral("danger"));
    st->clear_gps->setCheckable(true);
    st->clear_gps->setToolTip(PageMeta::tr("输出不含 GPS（清除优先于坐标）"));
    btn_row->addWidget(st->read_gps);
    btn_row->addWidget(st->pick_map);
    btn_row->addWidget(st->clear_gps);
    btn_row->addStretch(1);
    v->addLayout(btn_row);

    st->gps_status = new QLabel(card);
    st->gps_status->setObjectName(QStringLiteral("gps_status"));
    st->gps_status->setProperty(kFieldProperty, QStringLiteral("sub"));
    st->gps_status->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    st->gps_status->setContentsMargins(kCardPadX, 0, kCardPadX, 6);
    v->addWidget(st->gps_status);

    // 批量规则折叠行（rules() 的规则面；见头注释的控件集合说明）
    st->rule_toggle = new QToolButton(card);
    st->rule_toggle->setObjectName(QStringLiteral("pp-meta-gps-rule-toggle"));
    st->rule_toggle->setProperty(kFieldProperty, QStringLiteral("fold"));
    st->rule_toggle->setText(PageMeta::tr("批量 GPS 规则 ▾"));
    st->rule_toggle->setCheckable(true);
    st->rule_toggle->setChecked(false);
    st->rule_toggle->setAutoRaise(true);
    st->rule_toggle->setFont(theme::font(theme::Typography::body_px, 600));
    auto *toggle_row = new QHBoxLayout();
    toggle_row->setContentsMargins(kCardPadX, 0, kCardPadX, 6);
    toggle_row->addWidget(st->rule_toggle, 0, Qt::AlignLeft);
    toggle_row->addStretch(1);
    v->addLayout(toggle_row);

    st->rule_host = new QWidget(card);
    st->rule_host->setObjectName(QStringLiteral("pp-meta-gps-rule"));
    auto *rule_grid = new QGridLayout(st->rule_host);
    rule_grid->setContentsMargins(kCardPadX, 0, kCardPadX, 10);
    rule_grid->setHorizontalSpacing(theme::Metrics::form_col_gap);
    rule_grid->setVerticalSpacing(0);
    st->gps_enable = new QCheckBox(PageMeta::tr("启用"), st->rule_host);
    st->gps_enable->setObjectName(QStringLiteral("gps_enable"));
    st->gps_enable->setChecked(false);
    rule_grid->addWidget(st->gps_enable, 0, 0, 1, 2);
    const auto make_rule_edit = [&](const QString &hook) {
        auto *edit = new QLineEdit(st->rule_host);
        edit->setObjectName(hook);
        edit->setProperty(kFieldProperty, QStringLiteral("view"));
        edit->setFixedHeight(kGpsBoxHeight);
        return edit;
    };
    st->lat = make_rule_edit(QStringLiteral("gps_lat"));
    st->lat->setValidator(new QDoubleValidator(-90.0, 90.0, 6, st->lat));
    st->lat->setPlaceholderText(PageMeta::tr(kLatPlaceholder));
    st->lon = make_rule_edit(QStringLiteral("gps_lon"));
    st->lon->setValidator(new QDoubleValidator(-180.0, 180.0, 6, st->lon));
    st->lon->setPlaceholderText(PageMeta::tr(kLonPlaceholder));
    rule_grid->addWidget(form_label(st->rule_host, PageMeta::tr("纬度")), 1, 0);
    rule_grid->addWidget(st->lat, 1, 1);
    rule_grid->addWidget(form_label(st->rule_host, PageMeta::tr("经度")), 1, 2);
    rule_grid->addWidget(st->lon, 1, 3);
    st->more_toggle = new QToolButton(st->rule_host);
    st->more_toggle->setObjectName(QStringLiteral("gps_more_toggle"));
    st->more_toggle->setProperty(kFieldProperty, QStringLiteral("fold"));
    st->more_toggle->setText(PageMeta::tr("更多字段"));
    st->more_toggle->setCheckable(true);
    st->more_toggle->setChecked(false);
    st->more_toggle->setArrowType(Qt::RightArrow);
    st->more_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    st->more_toggle->setAutoRaise(true);
    st->more_toggle->setFont(theme::font(theme::Typography::body_px, 600));
    rule_grid->addWidget(st->more_toggle, 2, 0, 1, 2);
    st->alt = make_rule_edit(QStringLiteral("gps_alt"));
    st->alt->setPlaceholderText(PageMeta::tr("可空"));
    st->dir = make_rule_edit(QStringLiteral("gps_dir"));
    st->dir->setPlaceholderText(PageMeta::tr("可空"));
    st->ts = make_rule_edit(QStringLiteral("gps_ts"));
    st->ts->setPlaceholderText(QStringLiteral("YYYY:MM:DD HH:MM:SS"));
    rule_grid->addWidget(form_label(st->rule_host, PageMeta::tr("海拔 (m)")), 3, 0);
    rule_grid->addWidget(st->alt, 3, 1);
    rule_grid->addWidget(form_label(st->rule_host, PageMeta::tr("方位角 (°)")), 3, 2);
    rule_grid->addWidget(st->dir, 3, 3);
    rule_grid->addWidget(form_label(st->rule_host, PageMeta::tr("时间戳")), 4, 0);
    rule_grid->addWidget(st->ts, 4, 1, 1, 3);
    rule_grid->setColumnStretch(1, 1);
    rule_grid->setColumnStretch(3, 1);
    st->rule_host->setVisible(false);
    v->addWidget(st->rule_host);

    // ---- 连接 ----
    for (QLineEdit *edit : {st->lat, st->lon}) {
        QObject::connect(edit, &QLineEdit::textChanged, st, [st](const QString &) {
            if (st->loading) {
                return;
            }
            refresh_gps_warning(st);
            refresh_effective(st);
            notify_rules_changed(st);
        });
    }
    for (QLineEdit *edit : {st->alt, st->dir, st->ts}) {
        QObject::connect(edit, &QLineEdit::textChanged, st, [st](const QString &) {
            if (st->loading) {
                return;
            }
            refresh_effective(st);
            notify_rules_changed(st);
        });
    }
    QObject::connect(st->gps_enable, &QCheckBox::toggled, st, [st](bool) {
        if (st->loading) {
            return;
        }
        refresh_gps_inputs(st);
        refresh_gps_warning(st);
        refresh_effective(st);
        notify_rules_changed(st);
    });
    QObject::connect(st->clear_gps, &QPushButton::toggled, st, [st](bool) {
        if (st->loading) {
            return;
        }
        refresh_gps_inputs(st);
        refresh_gps_warning(st);
        refresh_effective(st);
        notify_rules_changed(st);
    });
    QObject::connect(st->more_toggle, &QToolButton::toggled, st, [st](bool on) {
        st->more_toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        st->alt->setVisible(on);
        st->dir->setVisible(on);
        st->ts->setVisible(on);
    });
    st->alt->setVisible(false);
    st->dir->setVisible(false);
    st->ts->setVisible(false);
    QObject::connect(st->rule_toggle, &QToolButton::toggled, st,
                     [st](bool on) { st->rule_host->setVisible(on); });

    // 从选中文件读取 GPS（坐标来自**同一个 probe**：与生效值同源，不另解析 EXIF）
    QObject::connect(st->read_gps, &QPushButton::clicked, st, [st] {
        if (st->items.isEmpty() || st->probes.empty()) {
            return;
        }
        const pp::BatchRules rules = build_rules(st);
        const pp::EffectivePreview preview =
            pp::preview_effective(st->probes.front().meta, rules, st->items.first().exception);
        if (!preview.gps.original.has_value()) {
            st->gps_status->setText(st->probes.front().ok
                                        ? PageMeta::tr("选中的文件没有 GPS 信息")
                                        : PageMeta::tr("无法读取选中文件的元数据"));
            return;
        }
        const pp::GpsData &gps = *preview.gps.original;
        st->loading = true;
        st->lat->setText(format_6(gps.lat));
        st->lon->setText(format_6(gps.lon));
        st->alt->setText(gps.altitude.has_value() ? format_6(*gps.altitude) : QString());
        st->dir->setText(gps.direction.has_value() ? format_6(*gps.direction) : QString());
        st->ts->setText(gps.timestamp.has_value() ? QString::fromStdString(*gps.timestamp)
                                                  : QString());
        st->gps_enable->setChecked(true);
        st->loading = false;
        st->gps_status->setText(PageMeta::tr("已从选中文件读取 GPS"));
        refresh_gps_inputs(st);
        refresh_gps_warning(st);
        refresh_effective(st);
        notify_rules_changed(st);
    });

    // 地图选点：取视口中心为规则坐标（点击地图仍直接取点 —— v0.2 行为不变）
    QObject::connect(st->pick_map, &QPushButton::clicked, st, [st] {
        st->loading = true;
        st->lat->setText(format_6(st->map->center_lat()));
        st->lon->setText(format_6(st->map->center_lon()));
        st->gps_enable->setChecked(true);
        st->loading = false;
        st->gps_status->setText(PageMeta::tr("已取地图中心为坐标"));
        refresh_gps_inputs(st);
        refresh_gps_warning(st);
        refresh_effective(st);
        notify_rules_changed(st);
    });

    // 搜索：地图异步 → search_finished；结果下拉 → search_select(i)
    QObject::connect(st->search_btn, &QPushButton::clicked, st, [st] {
        const QString query = st->search_edit->text().trimmed();
        if (query.isEmpty()) {
            st->gps_status->setText(PageMeta::tr("请输入搜索关键词"));
            return;
        }
        st->map->run_search(query);
    });
    QObject::connect(st->search_edit, &QLineEdit::returnPressed, st->search_btn,
                     &QPushButton::click);
    QObject::connect(st->map, &pp::map::MapWidget::search_finished, st,
                     [st](const QStringList &titles, const QString &error) {
                         st->search_results->clear();
                         if (!error.isEmpty()) {
                             st->gps_status->setText(error);
                             return;
                         }
                         st->search_results->addItems(titles);
                         st->gps_status->setText(
                             titles.isEmpty()
                                 ? PageMeta::tr("没有匹配的结果")
                                 : PageMeta::tr("搜索到 %1 个结果").arg(titles.size()));
                     });
    QObject::connect(st->search_results, QOverload<int>::of(&QComboBox::activated), st,
                     [st](int index) { st->map->search_select(index); });
    QObject::connect(st->map, &pp::map::MapWidget::point_selected, st,
                     [st](double lat, double lon) {
                         st->loading = true;
                         st->lat->setText(format_6(lat));
                         st->lon->setText(format_6(lon));
                         st->gps_enable->setChecked(true);
                         st->loading = false;
                         st->gps_status->setText(PageMeta::tr("已从地图选择坐标"));
                         refresh_gps_inputs(st);
                         refresh_gps_warning(st);
                         refresh_effective(st);
                         notify_rules_changed(st);
                     });
    return card;
}

QWidget *build_switches_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(make_card_header(card, PageMeta::tr("隐私与文件时间"), nullptr, nullptr));

    const auto add_switch_row = [&](QCheckBox *&box, const QString &hook, const QString &text,
                                    const QString &sub, QToolButton *&link,
                                    const QString &link_text) {
        auto *row = new QWidget(card);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(kCardPadX, 8, kCardPadX, 8);
        h->setSpacing(10);
        auto *texts = new QVBoxLayout();
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(1);
        box = new QCheckBox(text, row);
        box->setObjectName(hook);
        box->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        auto *sub_label = new QLabel(sub, row);
        sub_label->setProperty(kFieldProperty, QStringLiteral("sub"));
        sub_label->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        sub_label->setIndent(theme::Metrics::checkbox_px + 6); // 与复选框文字左沿对齐
        texts->addWidget(box);
        texts->addWidget(sub_label);
        h->addLayout(texts);
        h->addStretch(1);
        link = new QToolButton(row);
        link->setObjectName(hook + QStringLiteral("_link"));
        link->setProperty(kFieldProperty, QStringLiteral("link"));
        link->setText(link_text);
        link->setAutoRaise(true);
        link->setCursor(Qt::PointingHandCursor);
        link->setFont(theme::font(theme::Typography::body_px, 600));
        h->addWidget(link, 0, Qt::AlignVCenter);
        v->addWidget(row);
        return row;
    };

    add_switch_row(st->strip, QStringLiteral("privacy_strip"), PageMeta::tr("隐私剥除"),
                   PageMeta::tr("输出前移除全部 EXIF/XMP（保留 ICC）"), st->link_tags,
                   PageMeta::tr("EXIF / XMP 标签编辑…"));
    add_switch_row(st->mtime, QStringLiteral("mtime_sync"), PageMeta::tr("文件时间同步"),
                   PageMeta::tr("输出文件 mtime 跟随拍摄时间"), st->link_exc,
                   PageMeta::tr("单文件例外 ()"));

    // 标签编辑（折叠；链接入口 = mockup 的「EXIF / XMP 标签编辑…」）
    st->tag_host = new QWidget(card);
    auto *tv = new QVBoxLayout(st->tag_host);
    tv->setContentsMargins(kCardPadX, 0, kCardPadX, 8);
    auto *rows_host = new QWidget(st->tag_host);
    rows_host->setObjectName(QStringLiteral("tag_rows_host"));
    st->tag_rows = new QVBoxLayout(rows_host);
    st->tag_rows->setContentsMargins(0, 0, 0, 0);
    tv->addWidget(rows_host);
    st->add_tag = new QPushButton(PageMeta::tr("添加标签"), st->tag_host);
    st->add_tag->setObjectName(QStringLiteral("tag_add_button"));
    tv->addWidget(st->add_tag, 0, Qt::AlignLeft);
    st->tag_host->setVisible(false);
    v->addWidget(st->tag_host);

    // 例外列表（折叠；链接入口 = mockup 的「单文件例外 (N) ⚑」）
    st->exc_host = new QWidget(card);
    auto *xv = new QVBoxLayout(st->exc_host);
    xv->setContentsMargins(kCardPadX, 0, kCardPadX, 8);
    st->exc_label = new QLabel(PageMeta::tr("无例外文件"), st->exc_host);
    st->exc_label->setObjectName(QStringLiteral("exception_label"));
    st->exc_label->setProperty(kFieldProperty, QStringLiteral("sub"));
    st->exc_label->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    xv->addWidget(st->exc_label);
    st->exc_list = new QListWidget(st->exc_host);
    st->exc_list->setObjectName(QStringLiteral("exception_list"));
    st->exc_list->setContextMenuPolicy(Qt::CustomContextMenu);
    st->exc_list->setMaximumHeight(120);
    xv->addWidget(st->exc_list);
    st->exc_host->setVisible(false);
    v->addWidget(st->exc_host);

    QObject::connect(st->strip, &QCheckBox::toggled, st, [st](bool) {
        if (!st->loading) {
            refresh_effective(st);
            notify_rules_changed(st);
        }
    });
    QObject::connect(st->mtime, &QCheckBox::toggled, st, [st](bool) {
        if (!st->loading) {
            refresh_effective(st);
            notify_rules_changed(st);
        }
    });
    QObject::connect(st->link_tags, &QToolButton::clicked, st, [st] {
        const bool on = !st->tag_host->isVisible();
        st->tag_host->setVisible(on);
        st->link_tags->setText(on ? PageMeta::tr("EXIF / XMP 标签编辑… ▾")
                                  : PageMeta::tr("EXIF / XMP 标签编辑…"));
    });
    QObject::connect(st->link_exc, &QToolButton::clicked, st, [st] {
        const bool on = !st->exc_host->isVisible();
        st->exc_host->setVisible(on);
        st->link_exc->setText(on ? PageMeta::tr("单文件例外 ⚑ ▾") : st->link_exc->text());
    });
    QObject::connect(st->add_tag, &QPushButton::clicked, st, [st] {
        add_tag_row(st, QString(), QString());
        st->tag_host->setVisible(true);
        notify_rules_changed(st);
    });
    QObject::connect(st->exc_list, &QListWidget::itemDoubleClicked, st,
                     [st](QListWidgetItem *item) {
                         if (item != nullptr) {
                             emit st->page->open_editor_requested(item->text());
                         }
                     });
    QObject::connect(st->exc_list, &QListWidget::customContextMenuRequested, st,
                     [st](const QPoint &pos) {
                         QListWidgetItem *item = st->exc_list->itemAt(pos);
                         if (item == nullptr) {
                             return;
                         }
                         QMenu menu(st->exc_list);
                         QAction *clear = menu.addAction(PageMeta::tr("清除例外"));
                         if (menu.exec(st->exc_list->viewport()->mapToGlobal(pos)) == clear) {
                             emit st->page->clear_exception_requested(item->text());
                         }
                     });
    return card;
}

void build_ui(PageMeta *page) {
    auto *st = new MetaState();
    st->page = page;
    st->setObjectName(QString::fromLatin1(kStateObjectName));
    st->setParent(page);
    page->setProperty(kStateProperty, QVariant::fromValue(static_cast<QObject *>(st)));
    st->tokens = theme::tokens(theme::preferred_theme_mode());

    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("pp-meta-content"));
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(theme::Metrics::gap); // mockup .scroll{gap:10px}
    QFrame *info_card = qobject_cast<QFrame *>(build_info_card(st, content));
    QFrame *time_card = qobject_cast<QFrame *>(build_time_card(st, content));
    QFrame *gps_card = qobject_cast<QFrame *>(build_gps_card(st, content));
    QFrame *switch_card = qobject_cast<QFrame *>(build_switches_card(st, content));
    st->cards = {info_card, time_card, gps_card, switch_card};
    v->addWidget(info_card);
    v->addWidget(time_card);
    v->addWidget(gps_card);
    v->addWidget(switch_card);
    v->addStretch(1);
    scroll->setWidget(content);

    apply_style(st);
    refresh_all(st);
}

} // namespace

// ---------------------------------------------------------------------------
// PageMeta（冻结接口；状态在 MetaState 子对象中）
// ---------------------------------------------------------------------------

PageMeta::PageMeta(QWidget *parent) : QWidget(parent) { build_ui(this); }

pp::BatchRules PageMeta::rules() const { return build_rules(state_of(this)); }

void PageMeta::apply_rules(const pp::BatchRules &r) { apply_state(state_of(this), r); }

void PageMeta::set_batch_files(const QStringList &paths) {
    MetaState *st = state_of(this);
    st->batch_files = paths;
    refresh_effective(st); // 「影响 N / N」与 (i/N) 跟随提示
}

void PageMeta::set_selection(const QList<MetaSelectionItem> &items, bool from_selection) {
    MetaState *st = state_of(this);
    st->items = items;
    st->from_selection = from_selection;
    request_probe_scan(st); // 只对跟随项取 probe（异步 + 序号守卫）
    refresh_all(st);
}

void PageMeta::set_selected_files(const QStringList &paths) {
    QList<MetaSelectionItem> items;
    for (const QString &path : paths) {
        MetaSelectionItem item;
        item.path = path;
        items.append(item);
    }
    set_selection(items, /*from_selection=*/!paths.isEmpty());
}

void PageMeta::set_exception_summary(const QStringList &paths) {
    MetaState *st = state_of(this);
    st->exc_list->clear();
    for (const QString &path : paths) {
        st->exc_list->addItem(path);
    }
    st->exc_label->setText(paths.isEmpty() ? tr("无例外文件")
                                           : tr("%1 个文件带元数据例外").arg(paths.size()));
    if (!st->exc_host->isVisible()) {
        st->link_exc->setText(tr("单文件例外 (%1) ⚑").arg(paths.size()));
    }
}

void PageMeta::set_tokens(const theme::Tokens &tokens) {
    MetaState *st = state_of(this);
    st->tokens = tokens;
    apply_style(st);
}

void PageMeta::set_map_provider(const QString &provider_id, const QString &amap_key, int cache_mb) {
    MetaState *st = state_of(this);
    st->map->set_provider(provider_id);
    st->map->set_amap_key(amap_key);
    st->map->set_cache_mb(cache_mb);
}

} // namespace pp::ui
