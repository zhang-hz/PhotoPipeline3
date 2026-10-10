// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M5-T7）— 元数据页：v6 §7.1 四卡组（右栏）
//
// 规格 = docs/qt-gui-design-spec.md §7.1（卡表）/ §5（R1–R4 表单体系）/ §6（控件语义）/
// §8（状态与空态）/ §10（Qt 映射）+ docs/mockups-v2/meta-dark|meta-light.{png,html}（视觉终稿）+
// docs/m5-adjudications.md A2（双列行栅格）/ A6（硬编码色收口进令牌）/ A7（按钮高 26）/
// A8（改值零字形）。
//
// 四卡（§7.1 逐行；全部信息行走 formkit 行构造器 —— C2「表单唯一入口」）：
//   ① 关键信息：kv 双列 5 行 = 拍摄时间/相机、镜头/曝光、尺寸/色彩、文件/GPS(+「已有」胶囊)、
//      白平衡/评级(矢量星)；空态（未选择文件）收敛为**一行**（修 1-23），不再平铺空值。
//   ② 时间偏移：模式（分段控件）｜日期偏移（3×spin88 + 年月日 16px 槽 + 「−99…+99」注释）｜
//      时间偏移（3×spin88 + 时分秒槽 + 「UTC±HH:MM」注释）｜生效时间（删除线原值 → 强调色生效值
//      + 「日期 · Δ · 字段名」注释）。时区语义模式的「从/到」行保留（§7.1 表未列；该模式的功能
//      必需控件，与 M4 同一控件集合）。
//   ③ GPS：纬度/经度（双列轨，fill）｜海拔/方位（同）｜时间戳｜**操作行固定在地图上方**
//      （修 1-37：核心动作不再落进内滚盲区）｜图层（M6-T8：Seg 三态 → set_tile_style）｜
//      地图源（OSM 下拉，有标签）｜地点搜索（M6-T8：200ms 防抖联想）｜分类 chips（M6-T8）｜
//      搜索结果（M6-T8：可点列表 = 名称 + 地址 + 距离）｜地图（高 136 · 图钉 · 版权行）｜
//      地址（M6-T8：逆地理回显）｜批量 GPS 规则（折叠行，默认折叠；内为规则编辑面）。
//   ④ 隐私与文件时间：隐私剥除（开关+说明+「标签编辑…」链）｜时间同步（开关+说明+「例外…」链）｜
//      关键字（中性胶囊组 + 「+N」 + 「编辑…」链）。
//
// 生效值显示（M4-T12 语义零改动；§5.1/§5.2 + D4）：
//   * 生效值一律出自纯函数 `pp::preview_effective(probe, rules, exception)`（**唯一**合成源），
//     喂进去的 rules 就是 rules()/build_rules() 产出的那一份 → 显示 = 即将写入（两张皮检测）；
//   * .mod（A8）＝ 强调边框 + 强调色字 + 700、**无字形前缀**；
//   * 生效时间对照：未修改时两值相同也照常显示；改值行 = 强调档；另两个时间字段
//     （CreateDate/ModifyDate）的对照进标签 tooltip（1-30「细节进 tooltip」，信息不丢）；
//   * 多选（≥2）→ 逐行小表（文件名 | 原 → 生效，上限 8 行 + 「…还有 N 个」）；
//   * GPS：只显**生效坐标** + 地图 center_on 生效坐标 + 单钉（D4：不画新旧对比；无生效值 →
//     不画钉、不动视口）；
//   * 状态与占位分离（修 1-33）：GPS 状态（已有/无/将被移除/将清除）= 卡头**中性胶囊**；
//     placeholder 只留输入示例（规则输入框），生效值框不再用状态文案占位。
//
// 自验钩子（--ui-smoke 的 meta-geom / meta-* 探针读；非用户可见）：
//   * 动态属性（同时挂 MetaState 与页面本身）：pp_meta_follow_count / pp_meta_follow_name /
//     pp_meta_follow_pending / pp_meta_follow_stale_dropped / pp_meta_time_new /
//     pp_meta_gps_effective / pp_meta_multi_rows；M6-T8 加性：pp_meta_gps_layer（"road"|
//     "satellite"|"hybrid"）/ pp_meta_gps_results（结果数）/ pp_meta_gps_suggest（联想条数）/
//     pp_meta_gps_address（逆地理回显文本）；
//   * ElidedLabel 的 `ppFullText`：显示文本可能按宽度省略成 "…"，语义值（全文）挂在这枚属性上，
//     断言按语义值比对；
//   * objectName（pp-* 命名法）：pp-meta-info-<time|camera|lens|exposure|size|color|file|gps|wb|
//     rating> / pp-meta-info-row-<0…4> / pp-meta-info-empty / pp-meta-info-empty-row /
//     pp-meta-info-hint / pp-meta-info-pill / pp-meta-info-gps-pill / pp-meta-gps-pill /
//     pp-meta-time-<old|new|note|src>-0 / pp-meta-time-empty / pp-meta-time-pill /
//     pp-meta-delta-<years|months|days|hours|minutes|seconds> / pp-meta-tz-<from|to> /
//     pp-meta-seg-delta / pp-meta-seg-tz / pp-meta-multi-<name|old|new>-<i> / pp-meta-multi-more /
//     pp-meta-gps-<lat|lon|alt|dir>（生效值，只读；DMS 与时间戳读数在 tooltip）/
//     pp-meta-gps-<read|pick|clear> /
//     pp-meta-gps-<provider|search-edit|search-button> / pp-meta-gps-search-results（M6-T8 起 =
//     可点结果列表）/ pp-meta-gps-<layer|suggest|address>（M6-T8 加性）/ pp-meta-map /
//     pp-meta-gps-rule-toggle / pp-meta-privacy-pill / pp-meta-keyword-<i> / gps_status /
//     gps_warning / privacy_strip / mtime_sync / gps_enable / gps_more_toggle /
//     gps_lat|gps_lon|gps_alt|gps_dir|gps_ts（规则输入）/ exception_label / exception_list /
//     tag_add_button / tag_row|tag_key|tag_value|tag_remove；
//   * 地图离线 banner（mapwidget 内硬编码黄底）：本页只做**宿主侧**收口 —— 文案去括号（进 tooltip）
//     与中性灰覆盖 QSS（修 1-38/1-39；mapwidget 文件零改动，令牌由本页铺）。
#include "ui/page_meta.h"

#include "ui/formkit.h" // M5-T7：全部表单行走 formkit 行构造器（R1–R4 构造层强制）
#include "ui/icons.h"   // 折叠插入符/删除钮一律矢量图标（C3）

#include <QAbstractSpinBox>
#include <QApplication>
#include <QButtonGroup>
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
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mapwidget/geocoder.h" // M6-T8：联想/结果/逆地理的信号类型（SuggestItem/GeocodeResult）
#include "mapwidget/layers.h"   // M6-T8：图层支持性提示（tile_source_by_id / TileStyle）
#include "mapwidget/mapwidget.h"
#include "mapwidget/providers.h" // 地图源下拉的 id/label 单源（provider_ids/provider_by_id）
#include "ui/theme.h"

namespace pp::ui {
namespace {

using formkit::FieldWidth;

constexpr const char *kStateProperty = "pp_meta_state";
constexpr const char *kStateObjectName = "pp_meta_state";
// 页面级动态属性钩子（QSS 选择器锚）：与 theme.h 的 ppFk/ppMod 并列，页面私有不外泄。
constexpr const char *kFieldProperty = "ppMetaField";

// §7.1 时间卡的对照行主字段（core/metadata.h 的字段表：ExifTool 名）；另两字段进标签 tooltip。
constexpr const char *kPrimaryTimeSource = "DateTimeOriginal";
constexpr const char *kSecondaryTimeSources[2] = {"CreateDate", "ModifyDate"};

// §5.2 多选小表上限（超过 → 「…还有 N 个」）
constexpr int kMultiRowCap = 8;
// mockups-v2 视觉终稿 .map{height:136px}（M4 的 196 来自 v1 原型；v2 为 §7.1 的对照基准）
constexpr int kMapHeight = 136;
// 评级星（§7.1「评级(★)」；A8 零字形 → 矢量星）
constexpr int kStarPx = 12;
constexpr int kStarGap = 2;

// 输入示例占位（修 1-33：placeholder 只留输入示例 —— 状态一律走卡头胶囊）
constexpr const char *kLatPlaceholder = QT_TR_NOOP("例如 31.230400");
constexpr const char *kLonPlaceholder = QT_TR_NOOP("例如 121.473700");
constexpr const char *kAltPlaceholder = QT_TR_NOOP("例如 12.0");
constexpr const char *kDirPlaceholder = QT_TR_NOOP("例如 87.5");
constexpr const char *kTsPlaceholder = QT_TR_NOOP("例如 2024:03:01 10:00:00");
constexpr const char *kSearchPlaceholder = QT_TR_NOOP("搜索地点");
constexpr const char *kSearchResultPlaceholder = QT_TR_NOOP("搜索后在此选择");

// M6-T8（§4.3/§5.1 搜索区）：
//   * 联想防抖 200ms —— 输入停顿后一次 run_suggest（每类请求在 map 内单在途）；
//   * 结果列表可视 3 行 / 联想下拉可视 5 行（超出列表自滚动）；结果行高 = 名称 + 地址/距离两行。
constexpr int kSuggestDebounceMs = 200;
constexpr int kSuggestVisibleRows = 5;
constexpr int kResultVisibleRows = 3;
constexpr int kResultRowH = 36;
constexpr int kResultIconPx = 12;
constexpr double kSuggestZoom = 16.0; // 联想定位的缩放档（与 search_select 的 ≥12 同向）
// 图层 Seg 三态（与 pp::map::TileStyle 同序；键 = 自验钩子读数）
constexpr std::array<const char *, 3> kLayerStyleKeys = {"road", "satellite", "hybrid"};
// 分类 chips 文案 = 直接进 run_search 的类目关键词（§4.3 分类 POI）
constexpr std::array<const char *, 6> kCategoryKeywords = {
    QT_TR_NOOP("餐饮"), QT_TR_NOOP("酒店"), QT_TR_NOOP("加油站"),
    QT_TR_NOOP("景点"), QT_TR_NOOP("地铁"), QT_TR_NOOP("商场")};
// GPS 卡头状态胶囊文案（修 1-33：状态用独立徽标；§5.2 的两条状态语逐字保留）
constexpr const char *kHasGpsText = QT_TR_NOOP("已有 GPS");
constexpr const char *kNoGpsText = QT_TR_NOOP("无 GPS");
constexpr const char *kWillRemoveText = QT_TR_NOOP("将被移除");
constexpr const char *kGpsClearText = QT_TR_NOOP("将清除 GPS");
// 空态一行（修 1-23/1-31：未选择文件 → 一行说明，不平铺空值）
constexpr const char *kNoSelectionText = QT_TR_NOOP("未选择文件，请在左侧列表选择照片");
constexpr const char *kNoKeywordsText = QT_TR_NOOP("无关键字");

// ---------------------------------------------------------------------------
// 单行省略标签（§12-14「全界面禁止文案折行（超长省略号）」；与 mainwindow.cpp 的 ElidedLabel
// 同口径）
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

// 日期部分（生效行注释的「日期」段）；空值 → 空串
QString date_part(const std::optional<pp::DateTimeVal> &v) {
    if (!v.has_value() || v->value.size() < 10) {
        return QString();
    }
    return format_datetime(QString::fromStdString(v->value)).left(10);
}

QString datetime_text(const std::optional<pp::DateTimeVal> &v) {
    return v.has_value() ? format_datetime(QString::fromStdString(v->value)) : QStringLiteral("—");
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

// EXIF 字符串读取（显示层只读）
std::string exif_text(const Exiv2::ExifData &exif, const char *key) {
    std::unique_ptr<Exiv2::ExifKey> exif_key;
    try {
        exif_key = std::make_unique<Exiv2::ExifKey>(key);
    } catch (const std::exception &) {
        return {}; // 未知标签名 = 无值（不抛、不猜）
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

// 白平衡（§7.1「白平衡」）：Exif.Photo.WhiteBalance（0=自动/1=手动）+ ColorTemperature（K）
// —— mockup 口径「自动 · 6200K」；缺项即略过，全缺 → "—"。
QString white_balance_text(const Exiv2::ExifData &exif) {
    QStringList parts;
    const std::string wb = exif_text(exif, "Exif.Photo.WhiteBalance");
    if (!wb.empty()) {
        const int v = std::atoi(wb.c_str());
        parts << (v == 0 ? PageMeta::tr("自动")
                         : (v == 1 ? PageMeta::tr("手动") : QString::fromStdString(wb)));
    }
    const std::string kelvin = exif_text(exif, "Exif.Photo.ColorTemperature");
    if (!kelvin.empty()) {
        parts << QStringLiteral("%1K").arg(QString::fromStdString(kelvin));
    }
    return parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral(" · "));
}

// 评级（§7.1「评级(★)」）：Exif.Image.Rating 优先，XMP xmp:Rating 兜底；无 → nullopt（显示 "—"）
std::optional<int> rating_of(const pp::SourceMeta &meta) {
    const std::string exif_value = exif_text(meta.exif, "Exif.Image.Rating");
    if (!exif_value.empty()) {
        bool ok = false;
        const int value = QString::fromStdString(exif_value).toInt(&ok);
        if (ok) {
            return std::clamp(value, 0, 5);
        }
    }
    try {
        const Exiv2::XmpKey key("Xmp.xmp.Rating");
        const auto it = meta.xmp.findKey(key);
        if (it != meta.xmp.end()) {
            bool ok = false;
            const int value = QString::fromStdString(it->toString()).toInt(&ok);
            if (ok) {
                return std::clamp(value, 0, 5);
            }
        }
    } catch (const std::exception &) {
        // 未知 XMP 键 = 无值
    }
    return std::nullopt;
}

// 关键字（§7.1「关键字 胶囊组」）：Xmp.dc.subject（数组；非数组值按单项处理）
QStringList keywords_of(const pp::SourceMeta &meta) {
    QStringList out;
    try {
        const Exiv2::XmpKey key("Xmp.dc.subject");
        const auto it = meta.xmp.findKey(key);
        if (it == meta.xmp.end()) {
            return out;
        }
        const std::size_t count = std::max<std::size_t>(1, it->count());
        for (std::size_t i = 0; i < count; ++i) {
            const QString value = QString::fromStdString(it->toString(i)).trimmed();
            if (!value.isEmpty() && !out.contains(value)) {
                out << value;
            }
        }
    } catch (const std::exception &) {
        // 未知 XMP 键 = 无值
    }
    return out;
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

// Δ 模式摘要（生效行注释的「Δ」段；mockup「+8h」口径）：y/mo/d/h/m/s，全零 → ±0
QString delta_text(const pp::TimeShift &shift) {
    if (shift.mode == pp::TimeShift::Mode::TimezoneSemantic) {
        return QStringLiteral("UTC%1 → UTC%2")
            .arg(QString::fromStdString(pp::offset_time_string(shift.from_offset_min)),
                 QString::fromStdString(pp::offset_time_string(shift.to_offset_min)));
    }
    QStringList parts;
    const auto add = [&parts](int value, const QString &unit) {
        if (value == 0) {
            return;
        }
        parts << QStringLiteral("%1%2%3")
                     .arg(value > 0 ? QStringLiteral("+") : QStringLiteral("−"))
                     .arg(std::abs(value))
                     .arg(unit);
    };
    add(shift.years, PageMeta::tr("y"));
    add(shift.months, PageMeta::tr("mo"));
    add(shift.days, PageMeta::tr("d"));
    add(shift.hours, PageMeta::tr("h"));
    add(shift.minutes, PageMeta::tr("m"));
    add(shift.seconds, PageMeta::tr("s"));
    return parts.isEmpty() ? PageMeta::tr("±0") : parts.join(QStringLiteral(" "));
}

// ---------------------------------------------------------------------------
// 页内小控件
// ---------------------------------------------------------------------------

// 时间偏移 spin（R4 数值控件）：mockup 的「+8」符号显示在 Qt 里只有 textFromValue 一处干净落点，
// 故为 QSpinBox 的**极小覆写**（其余设置与 formkit::spin_box 逐条一致：ppFk=spin / 右对齐 /
// tabular / 高 24 / 短档 88 —— 由 meta-geom 探针钉住宽度 88 与高度 24）。
class DeltaSpin : public QSpinBox {
public:
    DeltaSpin(QWidget *parent, const QString &object_name) : QSpinBox(parent) {
        setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkSpin));
        setFont(theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
        setAlignment(Qt::AlignRight | Qt::AlignVCenter); // §4.2「数值」：右对齐
        setFixedSize(formkit::kFieldShort, formkit::kControlH);
        setObjectName(object_name);
        setRange(-99, 99); // §7.1 注释「−99…+99」= 范围（R3：范围入注释）
        setValue(0);
    }

protected:
    QString textFromValue(int value) const override {
        return (value < 0 ? QStringLiteral("−") : QStringLiteral("+")) +
               QString::number(std::abs(value));
    }
};

// 评级（§7.1「评级(★)」；A8 零字形 → **矢量星**）：形 = icons 库的 star 路径（单源），
// 实心 = acc 填充、空心 = text3 描边；无任何字形字符。
class RatingStars : public QWidget {
public:
    explicit RatingStars(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(kStarPx * 5 + kStarGap * 4, kStarPx);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    void set_rating(int stars) {
        const int value = std::clamp(stars, 0, 5);
        if (value == rating_) {
            return;
        }
        rating_ = value;
        update();
    }
    void set_tokens(const theme::Tokens &t) {
        tokens_ = t;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QPainterPath star = icons::path(icons::Name::Star);
        const double scale = double(kStarPx) / icons::kGrid;
        for (int i = 0; i < 5; ++i) {
            QTransform transform;
            transform.translate(i * (kStarPx + kStarGap), 0);
            transform.scale(scale, scale);
            const QPainterPath boxed = transform.map(star);
            if (i < rating_) {
                p.setPen(Qt::NoPen);
                p.setBrush(tokens_.accent);
                p.drawPath(boxed);
            } else {
                p.setPen(QPen(tokens_.text3, 1.0));
                p.setBrush(Qt::NoBrush);
                p.drawPath(boxed);
            }
        }
    }

private:
    int rating_ = 0;
    theme::Tokens tokens_ = formkit::default_tokens();
};

// ---------------------------------------------------------------------------
// 页面状态（挂在 PageMeta 下的子 QObject）
// ---------------------------------------------------------------------------

// 跟随项的 probe 快照（后台线程产出；只含值语义，不触碰 GUI）
struct ProbeSnapshot {
    QString path;
    pp::SourceMeta meta;
    bool ok = false;
};

struct MetaState : QObject {
    PageMeta *page = nullptr;
    bool loading = false; // apply_rules 期间抑制信号与重复刷新
    theme::Tokens tokens;

    std::array<QFrame *, 4> cards{};

    // ---- 卡片 ① 关键信息（kv 双列 5 行 + 空态一行）----
    formkit::PillBadge *info_pill = nullptr;
    ElidedLabel *info_hint = nullptr;
    std::array<QWidget *, 5> info_row{};
    std::array<ElidedLabel *, 8> info_value{}; // time/camera/lens/exposure/size/color/file/gps
    ElidedLabel *info_wb = nullptr;
    RatingStars *info_rating = nullptr;
    QLabel *info_rating_none = nullptr;
    formkit::PillBadge *info_gps_pill = nullptr;
    QWidget *info_empty_row = nullptr;
    ElidedLabel *info_empty = nullptr;

    // ---- 卡片 ② 时间偏移 ----
    formkit::PillBadge *time_pill = nullptr;
    ElidedLabel *time_hint = nullptr;
    formkit::SegmentedControl *seg = nullptr;
    QWidget *delta_row = nullptr;
    std::array<QSpinBox *, 6> delta{};
    QWidget *time_row = nullptr;
    QWidget *tz_row = nullptr;
    QComboBox *tz_from = nullptr;
    QComboBox *tz_to = nullptr;
    QWidget *effect_row = nullptr;
    QLabel *effect_label = nullptr;
    ElidedLabel *time_old = nullptr;
    QLabel *time_arrow = nullptr;
    ElidedLabel *time_new = nullptr;
    ElidedLabel *time_note = nullptr;
    ElidedLabel *time_src = nullptr;
    QWidget *time_empty_row = nullptr;
    ElidedLabel *time_empty = nullptr;
    QWidget *multi_host = nullptr;
    QGridLayout *multi_grid = nullptr;
    ElidedLabel *multi_more = nullptr;

    // ---- 卡片 ③ GPS ----
    formkit::PillBadge *gps_pill = nullptr;
    ElidedLabel *gps_hint = nullptr;
    QLabel *gps_warn = nullptr;
    QLineEdit *view_lat = nullptr;
    QLineEdit *view_lon = nullptr;
    QLineEdit *view_alt = nullptr;
    QLineEdit *view_dir = nullptr;
    pp::map::MapWidget *map = nullptr;
    QComboBox *provider = nullptr;
    QLineEdit *search_edit = nullptr;
    QPushButton *search_btn = nullptr;
    QListWidget *search_results = nullptr; // M6-T8：可点结果列表（原纯文本下拉）
    // ---- M6-T8 加性（图层 / 联想 / 分类 / 地址回显）----
    formkit::SegmentedControl *layer_seg = nullptr;
    QTimer *suggest_timer = nullptr; // 输入防抖 200ms（联想）
    QListWidget *suggest_list = nullptr;
    QButtonGroup *category_group = nullptr;
    QList<QPushButton *> category_chips;
    std::vector<pp::map::GeocodeResult> results; // 搜索结果（与 map 内部 results 同序）
    QWidget *address_row = nullptr;
    ElidedLabel *address_value = nullptr;
    QPushButton *read_gps = nullptr;
    QPushButton *pick_map = nullptr;
    QPushButton *clear_gps = nullptr;
    QLabel *gps_status = nullptr;
    QToolButton *rule_toggle = nullptr;
    QWidget *rule_host = nullptr;
    formkit::CheckBox *gps_enable = nullptr;
    QToolButton *more_toggle = nullptr; // 规则的可选字段折叠（海拔/方位/时间戳）
    QLineEdit *lat = nullptr;
    QLineEdit *lon = nullptr;
    QLineEdit *alt = nullptr;
    QLineEdit *dir = nullptr;
    QLineEdit *ts = nullptr;

    // ---- 卡片 ④ 隐私与文件时间 ----
    formkit::PillBadge *privacy_pill = nullptr;
    formkit::SwitchControl *strip = nullptr;
    formkit::SwitchControl *mtime = nullptr;
    QToolButton *link_tags = nullptr;
    QToolButton *link_exc = nullptr;
    QToolButton *link_keywords = nullptr;
    QWidget *keyword_box = nullptr;
    QHBoxLayout *keyword_row = nullptr;
    ElidedLabel *keyword_none = nullptr;
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

    // ---- 主题重放的控件集（持有 tokens 副本的自绘控件）----
    QList<formkit::PillBadge *> pills;         // 常驻胶囊（卡头/GPS 值格）
    QList<formkit::PillBadge *> keyword_pills; // 关键字胶囊（随跟随项重建 → 每次刷新重填）
    QList<formkit::SwitchControl *> switches;

    void add_pill(formkit::PillBadge *pill) {
        pill->set_tokens(tokens);
        pills.append(pill);
    }
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

// 自验钩子（动态属性）：同时挂在 MetaState 子对象（自验程序）与页面本身（--ui-smoke 探针只持有
// PageMeta*）——非用户可见的只读读数。
void set_hook(MetaState *st, const char *name, const QVariant &value) {
    st->setProperty(name, value);
    if (st->page != nullptr) {
        st->page->setProperty(name, value);
    }
}

// ---------------------------------------------------------------------------
// formkit 行构造的页内辅助（R2/R3 的组内规则）
//   页面自有的"多控件一行"（年月日 / 时分秒 / 生效对照 / 开关行 / 操作行 / 关键字行）在**一个
//   字段轨内**并列：单位入 16px 槽、行内距 6px、组内距 8px —— 三个取值都取自 formkit
//   （formkit_layout.h 的 kUnitSlotW / kInlineGap + 令牌 group_gap）；行/标签轨/轨宽仍由
//   formkit 的行构造器给出。
// ---------------------------------------------------------------------------

struct TrackGroup {
    QWidget *host = nullptr;
    QHBoxLayout *line = nullptr;

    QWidget *widget() const { return host; }
    void add(QWidget *item, int stretch = 0) { line->addWidget(item, stretch, Qt::AlignVCenter); }
    void gap(int px) {
        if (px > 0) {
            line->addSpacing(px);
        }
    }
    void stretch() { line->addStretch(1); }
};

TrackGroup make_group(QWidget *parent) {
    TrackGroup out;
    out.host = new QWidget(parent);
    out.line = new QHBoxLayout(out.host);
    out.line->setContentsMargins(0, 0, 0, 0);
    out.line->setSpacing(0);
    return out;
}

// 单位槽（R3：单位入 16px 槽）—— 与 formkit 的 unit_slot 同取值同着色钩子
QLabel *unit_slot(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkUnit));
    label->setFont(theme::font(theme::Typography::hint_px));
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setFixedWidth(formkit::kUnitSlotW);
    return label;
}

// 10px 注释（R3：范围/提示入注释）——与 formkit 的 range_note 同取值同着色钩子
ElidedLabel *note_label(QWidget *parent, const QString &object_name = QString()) {
    auto *label = new ElidedLabel(parent);
    if (!object_name.isEmpty()) {
        label->setObjectName(object_name);
    }
    label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkNote));
    label->setFont(theme::font(theme::Typography::hint_px));
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
}

// 文本链（§5 R4「文字链无框」；高 26 = A7 按钮换锚）
QToolButton *make_link(QWidget *parent, const QString &object_name, const QString &text) {
    auto *link = new QToolButton(parent);
    link->setObjectName(object_name);
    link->setProperty(kFieldProperty, QStringLiteral("link"));
    link->setText(text);
    link->setAutoRaise(true);
    link->setCursor(Qt::PointingHandCursor);
    link->setFixedHeight(theme::Metrics::btn_h);
    link->setFont(theme::font(theme::Typography::body_px, 600));
    return link;
}

// 次按钮（§5 R4：高 26 + 描边；A7）
QPushButton *make_button(QWidget *parent, const QString &object_name, const QString &text) {
    auto *button = new QPushButton(text, parent);
    button->setObjectName(object_name);
    button->setProperty(kFieldProperty, QStringLiteral("btn"));
    button->setFixedHeight(theme::Metrics::btn_h);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

// 只读生效值框（§7.1 GPS 卡的生效坐标；mockup .gps-in 口径：--ctl 底 + r3 + tabular）
QLineEdit *make_view_field(QWidget *parent, const QString &object_name) {
    auto *edit = new QLineEdit(parent);
    edit->setObjectName(object_name);
    edit->setProperty(kFieldProperty, QStringLiteral("view"));
    edit->setReadOnly(true);
    edit->setFixedHeight(formkit::kControlH);
    edit->setFont(theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
    return edit;
}

// 规则输入框（R4 文本输入高 24；placeholder = 输入示例，修 1-33）
QLineEdit *make_rule_field(QWidget *parent, const QString &object_name) {
    auto *edit = new QLineEdit(parent);
    edit->setObjectName(object_name);
    edit->setProperty(kFieldProperty, QStringLiteral("rule"));
    edit->setFixedHeight(formkit::kControlH);
    return edit;
}

// 分类 chip（M6-T8/§5.1「分类 chips」）：**可勾选的文字 chip** —— 高/半径取 formkit 的 chip 度量
// （kChipH/kChipRadius），配色走本页 `ppMetaField="chip"` QSS（与 formkit::ChipButton 同色同源：
// 未选 = 透明底 + --ctl-bd；悬停 = --ctl 底；选中 = --acc-dim 底 + --acc-bd 边）。互斥选中由
// QButtonGroup 持有；无键帽、无字形。
QPushButton *make_chip(QWidget *parent, const QString &object_name, const QString &text) {
    auto *chip = new QPushButton(text, parent);
    chip->setObjectName(object_name);
    chip->setProperty(kFieldProperty, QStringLiteral("chip"));
    chip->setCheckable(true);
    chip->setCursor(Qt::PointingHandCursor);
    chip->setFixedHeight(formkit::kChipH);
    chip->setFont(theme::font(theme::Typography::small_px, QFont::DemiBold));
    chip->setToolTip(PageMeta::tr("按类目搜索：%1").arg(text));
    return chip;
}

// ---------------------------------------------------------------------------
// 规则装配（**唯一**规则源：rules() 与生效值显示都走这里 → 显示=写入）
// ---------------------------------------------------------------------------

pp::TimeShift current_time_shift(const MetaState *st) {
    pp::TimeShift shift;
    if (st->seg->current_index() == 0) {
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

void refresh_info_card(MetaState *st, const std::vector<pp::EffectivePreview> &previews) {
    const bool empty = st->items.isEmpty();
    // 空态（修 1-23）：5 行 kv 收起，只留一行说明
    for (QWidget *row : st->info_row) {
        row->setVisible(!empty);
    }
    st->info_empty_row->setVisible(empty);
    if (empty || previews.empty()) {
        for (ElidedLabel *label : st->info_value) {
            label->set_full_text(QString());
        }
        st->info_wb->set_full_text(QString());
        st->info_rating->setVisible(false);
        st->info_rating_none->setVisible(!empty);
        st->info_gps_pill->setVisible(false);
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
        st->info_value[7]->set_full_text(QStringLiteral("%1, %2").arg(
            coord_text(preview.gps.original->lat, 4), coord_text(preview.gps.original->lon, 4)));
        st->info_gps_pill->setVisible(true);
    } else {
        st->info_value[7]->set_full_text(QStringLiteral("—"));
        st->info_gps_pill->setVisible(false);
    }
    // 白平衡 / 评级（§7.1 第 5 行）
    st->info_wb->set_full_text(probe.ok ? white_balance_text(probe.meta.exif)
                                        : QStringLiteral("—"));
    const std::optional<int> rating = probe.ok ? rating_of(probe.meta) : std::nullopt;
    st->info_rating->setVisible(rating.has_value());
    st->info_rating->set_rating(rating.value_or(0));
    st->info_rating_none->setVisible(!rating.has_value());
}

// 清空多选小表（**保留** multi_more —— 它是常驻子控件、仅在有截断行时才入列，重复入列/
// 误删会造成 use-after-free）
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
    const bool empty = st->items.isEmpty();
    const bool multi = previews.size() >= 2;
    st->effect_row->setVisible(!empty && !multi);
    st->time_empty_row->setVisible(empty);
    st->multi_host->setVisible(!empty && multi);

    const pp::TimeShift shift = current_time_shift(st);
    const auto clear_single = [st] {
        st->time_old->set_full_text(QString());
        st->time_new->set_full_text(QString());
        st->time_note->set_full_text(QString());
        st->time_src->set_full_text(QString());
        theme::set_mod(st->time_new, false);
    };

    if (previews.empty()) {
        clear_single();
        clear_multi_rows(st);
        st->multi_more->set_full_text(QString());
        st->multi_more->setVisible(false);
        set_hook(st, "pp_meta_multi_rows", 0);
        set_hook(st, "pp_meta_time_new", QString());
        return;
    }

    if (!multi) {
        const pp::EffectivePreview &preview = previews.front();
        st->time_old->set_full_text(datetime_text(preview.datetime_original.original));
        st->time_new->set_full_text(preview.strip_privacy
                                        ? PageMeta::tr(kWillRemoveText)
                                        : datetime_text(preview.datetime_original.effective));
        QStringList note_parts;
        const QString date = date_part(preview.datetime_original.original);
        if (!date.isEmpty()) {
            note_parts << date;
        }
        note_parts << delta_text(shift);
        st->time_note->set_full_text(note_parts.join(QStringLiteral(" · ")) + QStringLiteral(" ·"));
        st->time_src->set_full_text(QString::fromLatin1(kPrimaryTimeSource));
        theme::set_mod(st->time_new, preview.datetime_original.changed);
        // 另两个时间字段（CreateDate / ModifyDate）的对照进标签 tooltip（1-30：细节进 tooltip）
        const std::array<const pp::EffectiveField<std::optional<pp::DateTimeVal>> *, 2> others = {
            &preview.datetime_digitized, &preview.datetime_modify};
        QString tip = PageMeta::tr("跟随选中文件；未修改时两值相同");
        for (std::size_t i = 0; i < others.size(); ++i) {
            tip += QStringLiteral("\n%1: %2 → %3")
                       .arg(QString::fromLatin1(kSecondaryTimeSources[i]),
                            datetime_text(others[i]->original),
                            preview.strip_privacy ? PageMeta::tr(kWillRemoveText)
                                                  : datetime_text(others[i]->effective));
        }
        st->effect_label->setToolTip(tip);
        set_hook(st, "pp_meta_time_new", st->time_new->full_text()); // 自验钩子（探针读生效时间）
        set_hook(st, "pp_meta_multi_rows", 0);
        return;
    }

    // 多选（≥2）：逐行小表（文件名 | 原 → 生效），上限 8 行 + 「…还有 N 个」（§5.2）
    clear_single(); // 单文件对照行让位（文本一并清空：探针据此判定"多选态"）
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
        name_label->setProperty(kFieldProperty, QStringLiteral("multi-name"));
        name_label->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        name_label->setMinimumHeight(formkit::kRowPitch);
        name_label->set_full_text(name);
        auto *old_label = new ElidedLabel(st->multi_host);
        old_label->setObjectName(QStringLiteral("pp-meta-multi-old-%1").arg(i));
        old_label->setProperty(kFieldProperty, QStringLiteral("old"));
        QFont old_font = theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true);
        old_font.setStrikeOut(true);
        old_label->setFont(old_font);
        old_label->setMinimumHeight(formkit::kRowPitch);
        old_label->set_full_text(datetime_text(preview.datetime_original.original));
        auto *arrow = new QLabel(QStringLiteral("→"), st->multi_host);
        arrow->setObjectName(QStringLiteral("pp-meta-multi-arrow"));
        arrow->setProperty(kFieldProperty, QStringLiteral("arrow"));
        arrow->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        auto *new_label = new ElidedLabel(st->multi_host);
        new_label->setObjectName(QStringLiteral("pp-meta-multi-new-%1").arg(i));
        new_label->setProperty(kFieldProperty, QStringLiteral("new"));
        new_label->setFont(theme::font(theme::Typography::body_px, QFont::Bold, /*tabular=*/true));
        new_label->setMinimumHeight(formkit::kRowPitch);
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
    const bool empty = st->items.isEmpty();
    const bool strip = preview != nullptr && preview->strip_privacy;
    const std::optional<pp::GpsData> effective =
        (preview != nullptr && !strip) ? preview->gps.effective : std::nullopt;
    const bool changed = preview != nullptr && preview->gps.changed;
    const auto set_view = [](QLineEdit *edit, const QString &text) {
        edit->setText(text);
        edit->setPlaceholderText(QString()); // 生效值框不承担状态（修 1-33）
    };
    if (empty || preview == nullptr) {
        for (QLineEdit *edit : {st->view_lat, st->view_lon, st->view_alt, st->view_dir}) {
            set_view(edit, QString());
            edit->setToolTip(QString());
        }
        theme::set_mod(st->view_lat, false);
        theme::set_mod(st->view_lon, false);
        st->gps_pill->setVisible(false);
        st->map->clear_marker();
        set_hook(st, "pp_meta_gps_effective", QString());
        return;
    }
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
        // DMS 读数 + 时间戳收进悬浮提示（不占控件面，信息不丢）
        QString tip = dms_string(effective->lat, effective->lon);
        if (effective->timestamp.has_value()) {
            tip += QStringLiteral("\n%1: %2")
                       .arg(PageMeta::tr("时间戳"), QString::fromStdString(*effective->timestamp));
        }
        st->view_lat->setToolTip(tip);
        st->view_lon->setToolTip(tip);
    } else {
        for (QLineEdit *edit : {st->view_lat, st->view_lon, st->view_alt, st->view_dir}) {
            set_view(edit, QString());
            edit->setToolTip(QString());
        }
    }
    // .mod（A8）：生效坐标与原值不同 → 强调边框+强调色字
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
    // 状态 = 独立徽标（修 1-33）：隐私剥除 > 清除 > 生效值有无
    const bool clearing = st->clear_gps->isChecked();
    const QString state = strip ? PageMeta::tr(kWillRemoveText)
                                : (clearing ? PageMeta::tr(kGpsClearText)
                                            : (effective.has_value() ? PageMeta::tr(kHasGpsText)
                                                                     : PageMeta::tr(kNoGpsText)));
    st->gps_pill->setText(state);
    st->gps_pill->setVisible(true);
    st->gps_pill->setToolTip(
        strip
            ? PageMeta::tr("输出不含 EXIF / XMP（含 GPS）")
            : (clearing ? PageMeta::tr("输出不含 GPS（清除优先于坐标）")
                        : (effective.has_value() ? PageMeta::tr("源文件或批量规则提供坐标")
                                                 : PageMeta::tr("源文件无坐标；批量规则可写入"))));
}

// 关键字胶囊组（§7.1「关键字 胶囊组」）：跟随项的 XMP dc:subject；超过 3 枚 → 「+N」
void refresh_keywords(MetaState *st) {
    if (st->keyword_row == nullptr) {
        return;
    }
    // 胶囊是**重建件**：本函数每次刷新都换一批 —— 令牌重放清单先清空（否则 apply_style 会对
    // 已 deleteLater 的旧胶囊取 tokens → 悬垂指针）
    st->keyword_pills.clear();
    // 说明位是常驻子控件（与 multi_more 同口径：重复入列/误删会造成悬垂引用）→ 只清胶囊
    while (st->keyword_row->count() > 1) {
        QLayoutItem *item = st->keyword_row->takeAt(1);
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
    QStringList keywords;
    if (!st->probes.empty() && !st->items.isEmpty()) {
        keywords = keywords_of(st->probes.front().meta);
    }
    st->keyword_none->setVisible(keywords.isEmpty());
    constexpr int kShown = 3;
    for (int i = 0; i < std::min<int>(kShown, keywords.size()); ++i) {
        auto *pill = formkit::PillBadge::of(st->keyword_box, formkit::PillBadge::Tone::Neutral,
                                            keywords.at(i));
        pill->setObjectName(QStringLiteral("pp-meta-keyword-%1").arg(i));
        pill->set_tokens(st->tokens);
        st->keyword_pills.append(pill);
        st->keyword_row->addWidget(pill, 0, Qt::AlignVCenter);
    }
    if (keywords.size() > kShown) {
        auto *more = formkit::PillBadge::of(st->keyword_box, formkit::PillBadge::Tone::Neutral,
                                            QStringLiteral("+%1").arg(keywords.size() - kShown));
        more->setObjectName(QStringLiteral("pp-meta-keyword-more"));
        more->set_tokens(st->tokens);
        st->keyword_pills.append(more);
        st->keyword_row->addWidget(more, 0, Qt::AlignVCenter);
    }
}

void refresh_effective(MetaState *st) {
    st->info_hint->set_full_text(follow_hint(st));
    // 时间卡范围胶囊（mockup「影响 12 / 24」的语义位）：页面知道的只有批内文件数，除数/被除数
    // 同源 → 不编造"N / M"。
    st->time_pill->setText(PageMeta::tr("影响 %1 个文件").arg(st->batch_files.size()));
    if (st->probe_pending) {
        refresh_info_card(st, {});
        refresh_time_section(st, {});
        refresh_gps_section(st, nullptr);
        refresh_keywords(st);
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
    refresh_keywords(st);
}

// ---------------------------------------------------------------------------
// 控件可用性 / 提示
// ---------------------------------------------------------------------------

void refresh_time_inputs(MetaState *st) {
    // 时间卡无「启用」开关：seg 选中的那一组恒可用，另一组隐藏
    const bool delta_mode = st->seg->current_index() == 0;
    st->delta_row->setVisible(delta_mode);
    st->time_row->setVisible(delta_mode);
    st->tz_row->setVisible(!delta_mode);
    // .mod（A8：改动值 = 强调边框+强调色字）
    for (QSpinBox *spin : st->delta) {
        theme::set_mod(spin, spin->value() != 0);
    }
    const bool tz_changed = st->tz_from->currentData().toInt() != st->tz_to->currentData().toInt();
    theme::set_mod(st->tz_from, tz_changed);
    theme::set_mod(st->tz_to, tz_changed);
}

// GPS 规则勾选但坐标非法 → 卡片黄字提示（真警告才用 warn 档令牌；A6：0xcc8800 收口）
void refresh_gps_warning(MetaState *st) {
    double lat = 0, lon = 0;
    const bool valid =
        parse_coord(st->lat, -90.0, 90.0, lat) && parse_coord(st->lon, -180.0, 180.0, lon);
    const bool show = st->gps_enable->isChecked() && !st->clear_gps->isChecked() && !valid;
    st->gps_warn->setVisible(show);
}

// 折叠入口的插入符 = **矢量图标**（▸ 收起 / ▾ 展开），不再是字形文本（C3/A8）
void set_fold_icon(MetaState *st, QToolButton *button, bool expanded) {
    if (button == nullptr) {
        return;
    }
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    icons::apply(button, expanded ? icons::Name::ChevronDown : icons::Name::ChevronRight,
                 st->tokens.text3, st->tokens.text3, 12);
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
    // placeholder = 输入示例（修 1-33）；禁用时清空（占位色不随 setEnabled 变化）
    st->lat->setPlaceholderText(editable ? PageMeta::tr(kLatPlaceholder) : QString());
    st->lon->setPlaceholderText(editable ? PageMeta::tr(kLonPlaceholder) : QString());
    st->alt->setPlaceholderText(editable ? PageMeta::tr(kAltPlaceholder) : QString());
    st->dir->setPlaceholderText(editable ? PageMeta::tr(kDirPlaceholder) : QString());
    st->ts->setPlaceholderText(editable ? PageMeta::tr(kTsPlaceholder) : QString());
    st->search_edit->setPlaceholderText(locked ? QString() : PageMeta::tr(kSearchPlaceholder));
    st->search_edit->setEnabled(!locked);
    st->search_btn->setEnabled(!locked);
    st->search_results->setEnabled(!locked);
    st->suggest_list->setEnabled(!locked);
    st->layer_seg->setEnabled(!locked);
    for (QPushButton *chip : st->category_chips) {
        chip->setEnabled(!locked); // 分类 chip 也是搜索入口（与搜索钮同锁）
    }
    st->provider->setEnabled(!locked);
    st->read_gps->setEnabled(!locked && st->from_selection && !st->items.isEmpty());
    st->pick_map->setEnabled(!locked);
    st->rule_toggle->setText(rule_on ? PageMeta::tr("批量 GPS 规则 · 已启用")
                                     : PageMeta::tr("批量 GPS 规则"));
    set_fold_icon(st, st->rule_toggle, st->rule_toggle->isChecked());
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
// M6-T8 搜索区（图层切换 / 联想 / 分类 / 结果列表 / 地址回显；§4.3–§4.5、§5.1–§5.3）
// ---------------------------------------------------------------------------

// 距离读数（§5.1 结果行「距离」；-1/NaN = 未知 → 空串，不编造）
QString distance_text(double distance_m) {
    if (!std::isfinite(distance_m) || distance_m < 0) {
        return QString();
    }
    if (distance_m >= 1000.0) {
        return QStringLiteral("%1 km").arg(QString::number(distance_m / 1000.0, 'f', 1));
    }
    return QStringLiteral("%1 m").arg(QString::number(distance_m, 'f', 0));
}

pp::map::TileStyle layer_style_of(int index) {
    switch (index) {
    case 1:
        return pp::map::TileStyle::Satellite;
    case 2:
        return pp::map::TileStyle::Hybrid;
    default:
        return pp::map::TileStyle::Road;
    }
}

// 图层/源支持性提示（§5.2「源不支持 → 自动回退并 tooltip 说明」）：回退本身是 mapwidget 的
// select_source 单源行为，本页只把「正在回退」告诉用户（tooltip 不承载行为）。
void refresh_layer_hint(MetaState *st) {
    if (st->layer_seg == nullptr) {
        return;
    }
    const int index = std::clamp(st->layer_seg->current_index(), 0, 2);
    const QString id = st->provider != nullptr ? st->provider->currentData().toString() : QString();
    const pp::map::TileSource *source = pp::map::tile_source_by_id(id);
    const pp::map::TileStyle style = layer_style_of(index);
    const bool supported =
        source == nullptr || style == pp::map::TileStyle::Road || source->supports(style);
    st->layer_seg->setToolTip(
        supported ? PageMeta::tr("地图图层：矢量路网 / 卫星影像 / 混合（影像 + 标注）")
                  : PageMeta::tr("当前地图源不支持该图层，已回退到 Esri 影像"));
}

// 结果行（§5.1「名称 + 地址 + 距离」）：第一行 = 图钉图标 + 名称 + 距离，第二行 = 地址；
// 颜色一律走本页 QSS 钩子（主题重放无需重排文字）。行件由列表以 setItemWidget 承载 —— 单击
// 事件仍由列表（viewport）收到 → itemClicked → search_select(i)。
QWidget *make_result_row(MetaState *st, int index, const pp::map::GeocodeResult &result) {
    auto *row = new QWidget();
    row->setObjectName(QStringLiteral("pp-meta-gps-result-%1").arg(index));
    auto *v = new QVBoxLayout(row);
    v->setContentsMargins(4, 3, 4, 3);
    v->setSpacing(1);
    auto *top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(formkit::kInlineGap);
    auto *pin = new QLabel(row);
    pin->setPixmap(icons::pixmap(icons::Name::MapPin, kResultIconPx, st->tokens.accent,
                                 row->devicePixelRatioF()));
    pin->setFixedSize(kResultIconPx, kResultIconPx);
    top->addWidget(pin, 0, Qt::AlignVCenter);
    auto *name = new ElidedLabel(row);
    name->setObjectName(QStringLiteral("pp-meta-gps-result-name-%1").arg(index));
    name->setProperty(kFieldProperty, QStringLiteral("result-name"));
    name->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
    name->set_full_text(result.title);
    top->addWidget(name, 1);
    const QString distance = distance_text(result.distance_m);
    if (!distance.isEmpty()) {
        auto *dist = new QLabel(distance, row);
        dist->setProperty(kFieldProperty, QStringLiteral("result-dist"));
        dist->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        top->addWidget(dist, 0, Qt::AlignVCenter);
    }
    v->addLayout(top);
    auto *address = new ElidedLabel(row);
    address->setObjectName(QStringLiteral("pp-meta-gps-result-address-%1").arg(index));
    address->setProperty(kFieldProperty, QStringLiteral("result-addr"));
    address->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    address->set_full_text(result.address);
    auto *bottom = new QHBoxLayout();
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->setSpacing(0);
    bottom->addSpacing(kResultIconPx + formkit::kInlineGap); // 与名称左缘对齐
    bottom->addWidget(address, 1);
    v->addLayout(bottom);
    return row;
}

// 结果列表的**唯一**渲染点（搜索回填、换源清空、主题重放都走这里 → 图标色/空态同源）：
// 无结果 = 一行不可选的占位（修 1-36 的占位文案保留；命中处理器按索引越界忽略它）。
void fill_result_rows(MetaState *st) {
    if (st->search_results == nullptr) {
        return;
    }
    st->search_results->clear();
    if (st->results.empty()) {
        auto *empty =
            new QListWidgetItem(PageMeta::tr(kSearchResultPlaceholder), st->search_results);
        empty->setForeground(st->tokens.text3);
        empty->setFlags(Qt::ItemIsEnabled);
        return;
    }
    for (std::size_t i = 0; i < st->results.size(); ++i) {
        const pp::map::GeocodeResult &result = st->results[i];
        auto *item = new QListWidgetItem(st->search_results);
        item->setSizeHint(QSize(0, kResultRowH));
        QStringList tip;
        tip << result.title;
        if (!result.address.isEmpty()) {
            tip << result.address;
        }
        const QString distance = distance_text(result.distance_m);
        if (!distance.isEmpty()) {
            tip << distance;
        }
        item->setToolTip(tip.join(QLatin1Char('\n')));
        st->search_results->setItemWidget(item, make_result_row(st, int(i), result));
    }
}

// 分类 chips 的互斥勾选由 QButtonGroup 持有；手输关键词时摘掉选中（摘除需临时关闭互斥）。
void clear_category_checks(MetaState *st) {
    QAbstractButton *checked =
        st->category_group != nullptr ? st->category_group->checkedButton() : nullptr;
    if (checked == nullptr) {
        return;
    }
    st->category_group->setExclusive(false);
    checked->setChecked(false);
    st->category_group->setExclusive(true);
}

// 搜索发射（**唯一**入口：搜索钮 / 回车 / 分类 chip）：先收联想与旧结果，再交给 map 异步搜。
void start_search(MetaState *st, bool from_category) {
    const QString query = st->search_edit->text().trimmed();
    if (query.isEmpty()) {
        st->gps_status->setText(PageMeta::tr("请输入搜索关键词"));
        return;
    }
    st->suggest_timer->stop();
    st->suggest_list->clear();
    st->suggest_list->setVisible(false);
    if (!from_category) {
        clear_category_checks(st); // 手输关键词 = 与类目选择脱钩
    }
    st->results.clear();
    set_hook(st, "pp_meta_gps_results", 0);
    fill_result_rows(st); // 旧结果让位（新结果到位前不留陈旧行）
    st->map->run_search(query);
}

// 选点（**唯一**入口：地图点击 / 地图选点 / 结果行 / 图钉 / 联想）：回填规则坐标 + 状态 +
// 触发逆地理回显（§4.5「选点后 gps_status 显示地址」）；主钉/居中由 refresh_effective 的
// 生效值管线收口（D4：只显生效值单钉）。
void apply_picked_point(MetaState *st, double lat, double lon, const QString &status) {
    st->loading = true;
    st->lat->setText(format_6(lat));
    st->lon->setText(format_6(lon));
    st->gps_enable->setChecked(true);
    st->loading = false;
    st->gps_status->setText(status);
    refresh_gps_inputs(st);
    refresh_gps_warning(st);
    refresh_effective(st);
    notify_rules_changed(st);
    // 地址回显先清（旧地址属于上一个点，不能挂在新点上）；结果由 reverse_finished 回填。
    st->address_value->set_full_text(QStringLiteral("—"));
    set_hook(st, "pp_meta_gps_address", QString());
    st->map->run_reverse(lat, lon);
}

// 结果行选中（行点击 / 回车激活）：唯一结果源在 map 内部（results 与列表同序）。
void select_result(MetaState *st, int index) {
    if (index < 0 || index >= int(st->results.size())) {
        return; // 占位行 / 越界 = 不动作（§8 空态）
    }
    st->map->search_select(index); // 居中 + 主钉 + point_selected
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
    lay->setSpacing(theme::Metrics::group_gap);

    auto *key_edit = new QLineEdit(row);
    key_edit->setObjectName(QStringLiteral("tag_key"));
    key_edit->setProperty(kFieldProperty, QStringLiteral("rule"));
    key_edit->setFixedHeight(formkit::kControlH);
    key_edit->setPlaceholderText(PageMeta::tr("Exif.Image.Artist 或 Xmp.dc.title"));
    key_edit->setText(key);
    auto *value_edit = new QLineEdit(row);
    value_edit->setObjectName(QStringLiteral("tag_value"));
    value_edit->setProperty(kFieldProperty, QStringLiteral("rule"));
    value_edit->setFixedHeight(formkit::kControlH);
    value_edit->setText(value);
    value_edit->setToolTip(PageMeta::tr("值留空 = 删除该标签"));
    auto *remove = new QPushButton(row);
    remove->setObjectName(QStringLiteral("tag_remove"));
    remove->setFixedSize(formkit::kIconBtn, formkit::kIconBtn); // 图标钮 28×28（R4）
    remove->setToolTip(PageMeta::tr("删除此行"));
    remove->setAccessibleName(PageMeta::tr("删除此行")); // §11：图标钮必有 accessibleName
    icons::apply(remove, icons::Name::Clear, st->tokens.text2, st->tokens.text3, 12);

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
        st->seg->set_current_index(s.mode == pp::TimeShift::Mode::Delta ? 0 : 1);
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
        st->seg->set_current_index(0);
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
// 样式（tokens 单源 = ui/theme.h；本页只把 tokens 铺到自己的控件上；A6：硬编码色一律收口）
// ---------------------------------------------------------------------------

void apply_style(MetaState *st) {
    const theme::Tokens &t = st->tokens;
    const auto css = [](const QColor &c) { return theme::css_color(c); };
    const QString field = QString::fromLatin1(kFieldProperty);

    QString qss;
    // formkit 行内辅助件（ppFk：标签轨/值/单位/注释 + R4 spin 盒）+ .mod + 胶囊：页面自足
    // （独立冒烟实例不再依赖窗口 QSS；与窗口 QSS 同值同源，重复规则无副作用）
    qss += theme::formkit_qss(t);
    qss += theme::mod_qss(t);
    qss += theme::pill_qss(t);
    // 卡头（mockup .card-h .t / .hint）
    qss += QStringLiteral("QLabel#pp-card-title { color: ") + css(t.text) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel#pp-card-hint { color: ") + css(t.text3) + QStringLiteral("; }\n");
    // 只读生效值框 / 规则输入框（mockup .gps-in / .tbx：--ctl 底 + --ctl-bd 边 + r3 + 高 24）
    qss += QStringLiteral("QLineEdit[") + field + QStringLiteral("=\"view\"], QLineEdit[") + field +
           QStringLiteral("=\"rule\"] { background: ") + css(t.control) +
           QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
           QStringLiteral("px; color: ") + css(t.text) + QStringLiteral("; padding: 0 8px; }\n");
    qss += QStringLiteral("QLineEdit[") + field +
           QStringLiteral("=\"view\"]:disabled, QLineEdit[") + field +
           QStringLiteral("=\"rule\"]:disabled { color: ") + css(t.text3) + QStringLiteral("; }\n");
    // 下拉规则已随 GUI-R2 收口到 theme::formkit_qss 的 `ppFk="combo"`（本页不再自带 QComboBox
    // 规则） 次按钮（R4：高 26 + 描边；A7）
    qss += QStringLiteral("QPushButton[") + field + QStringLiteral("=\"btn\"] { background: ") +
           css(t.control) + QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
           QStringLiteral("px; color: ") + css(t.text2) +
           QStringLiteral("; padding: 0 11px; font-weight: 600; }\n");
    qss += QStringLiteral("QPushButton[") + field + QStringLiteral("=\"btn\"]:disabled { color: ") +
           css(t.text3) + QStringLiteral("; background: transparent; border-color: ") +
           css(t.control_bd) + QStringLiteral("; }\n");
    // 文字链（无框）与危险链（清除 GPS = 红链；红只表达破坏性动作 §4.1）
    qss += QStringLiteral("QToolButton[") + field +
           QStringLiteral("=\"link\"] { border: none; background: transparent; color: ") +
           css(t.accent) + QStringLiteral("; font-weight: 600; padding: 0 2px; }\n");
    qss += QStringLiteral("QPushButton[") + field +
           QStringLiteral("=\"danger\"] { border: none; background: transparent; color: ") +
           css(t.err) + QStringLiteral("; font-weight: 600; padding: 0 2px; }\n");
    qss += QStringLiteral("QPushButton[") + field +
           QStringLiteral("=\"danger\"]:checked { "
                          "background: ") +
           css(t.err_dim) + QStringLiteral("; border-radius: ") +
           QString::number(t.radius_control) + QStringLiteral("px; }\n");
    // 折叠行（插入符 = 矢量图标）
    qss += QStringLiteral("QToolButton[") + field +
           QStringLiteral("=\"fold\"] { border: none; background: transparent; color: ") +
           css(t.text2) + QStringLiteral("; font-weight: 600; }\n");
    // 说明/提示行（mockup .sub-hint）
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"sub\"], QLabel[") + field +
           QStringLiteral("=\"hint\"] { color: ") + css(t.text3) + QStringLiteral("; }\n");
    // 生效对照（mockup .tline）：原 = 删除线 + --txt2；新 = 强调色（.mod 时再叠强调边框）；
    // 箭头/注释/来源 = --txt3；多选小表名 = --txt
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"old\"] { color: ") +
           css(t.text2) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"new\"] { color: ") +
           css(t.accent) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"arrow\"], QLabel[") + field +
           QStringLiteral("=\"note\"], QLabel[") + field +
           QStringLiteral("=\"multi-more\"] "
                          "{ color: ") +
           css(t.text3) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"multi-name\"] { color: ") +
           css(t.text) + QStringLiteral("; }\n");
    // 空态一行（§8：中性灰说明；绝不警示色）
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"empty\"] { color: ") +
           css(t.text3) + QStringLiteral("; }\n");
    // M6-T8 搜索区：结果列表 / 联想下拉（--ctl 底 + --ctl-bd 边 + r3；选中 = --acc-dim 底，
    // 悬停 = --card-hi 底 —— 与卡片/控件同一套令牌）
    const QString list_rule = QStringLiteral("QListWidget#pp-meta-gps-search-results, "
                                             "QListWidget#pp-meta-gps-suggest");
    qss += list_rule + QStringLiteral(" { background: ") + css(t.control) +
           QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
           QStringLiteral("px; color: ") + css(t.text) + QStringLiteral("; outline: none; }\n");
    qss += list_rule + QStringLiteral("::item { padding: 2px 4px; border-radius: ") +
           QString::number(t.radius_control) + QStringLiteral("px; }\n");
    qss += list_rule + QStringLiteral("::item:hover { background: ") + css(t.card_hi) +
           QStringLiteral("; }\n");
    qss += list_rule + QStringLiteral("::item:selected { background: ") + css(t.accent_dim) +
           QStringLiteral("; color: ") + css(t.text) + QStringLiteral("; }\n");
    // 结果行内文案（名称 = --txt；地址/距离 = --txt3）+ 地址回显行（--txt2）
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"result-name\"] { color: ") +
           css(t.text) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"result-addr\"], QLabel[") +
           field + QStringLiteral("=\"result-dist\"] { color: ") + css(t.text3) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"address\"] { color: ") +
           css(t.text2) + QStringLiteral("; }\n");
    // 分类 chip（与 formkit::ChipButton 同色同源：未选 = 透明底 + --ctl-bd；悬停 = --ctl 底；
    // 选中 = --acc-dim 底 + --acc-bd 边 + --txt 字）
    qss += QStringLiteral("QPushButton[") + field +
           QStringLiteral("=\"chip\"] { background: transparent; border: 1px solid ") +
           css(t.control_bd) + QStringLiteral("; border-radius: ") +
           QString::number(formkit::kChipRadius) + QStringLiteral("px; color: ") + css(t.text2) +
           QStringLiteral("; padding: 0 9px; }\n");
    qss += QStringLiteral("QPushButton[") + field +
           QStringLiteral("=\"chip\"]:hover { background: ") + css(t.control) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QPushButton[") + field +
           QStringLiteral("=\"chip\"]:checked { background: ") + css(t.accent_dim) +
           QStringLiteral("; border-color: ") + css(t.accent_bd) + QStringLiteral("; color: ") +
           css(t.text) + QStringLiteral("; }\n");
    qss += QStringLiteral("QPushButton[") + field +
           QStringLiteral("=\"chip\"]:disabled { color: ") + css(t.text3) +
           QStringLiteral("; border-color: ") + css(t.control_bd) + QStringLiteral("; }\n");
    // 真警告（GPS 坐标无效）：A6 —— 原硬编码 0xcc8800 收口为 warn 令牌
    qss += QStringLiteral("QLabel#gps_warning { color: ") + css(t.warn) + QStringLiteral("; }\n");
    // 卡片（§4.3 r6 + --card-bd 描边）
    qss += QStringLiteral("QFrame#pp-card { background: ") + css(t.card) +
           QStringLiteral("; border: 1px solid ") + css(t.card_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_card) +
           QStringLiteral("px; }\n");
    qss += QStringLiteral("QWidget#pp-meta-content { background: transparent; }\n");
    st->page->setStyleSheet(qss);

    // 地图离线 banner 的**宿主侧**收口（修 1-38/1-39：中性灰说明 + 括号进 tooltip；
    // mapwidget 文件零改动 —— 令牌/文案由本页铺）
    if (st->map != nullptr) {
        const QList<QLabel *> labels = st->map->findChildren<QLabel *>();
        for (QLabel *banner : labels) {
            if (!banner->text().contains(PageMeta::tr("地图离线"))) {
                continue;
            }
            banner->setText(PageMeta::tr("地图离线"));
            banner->setToolTip(PageMeta::tr("可手动输入坐标"));
            banner->setStyleSheet(
                QStringLiteral("QLabel { background: ") + css(t.card_hi) +
                QStringLiteral("; border: 1px solid ") + css(t.card_bd) +
                QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
                QStringLiteral("px; color: ") + css(t.text3) + QStringLiteral("; padding: 4px; }"));
            break;
        }
    }

    // M5-T3：矢量图标随 tokens 重放（QIcon 取色不吃 QSS 的 color）；M6-T8 加：搜索钮放大镜
    set_fold_icon(st, st->rule_toggle, st->rule_toggle != nullptr && st->rule_toggle->isChecked());
    set_fold_icon(st, st->more_toggle, st->more_toggle != nullptr && st->more_toggle->isChecked());
    set_fold_icon(st, st->link_tags, st->tag_host != nullptr && st->tag_host->isVisible());
    set_fold_icon(st, st->link_exc, st->exc_host != nullptr && st->exc_host->isVisible());
    set_fold_icon(st, st->link_keywords, st->tag_host != nullptr && st->tag_host->isVisible());
    if (st->search_btn != nullptr) {
        icons::apply(st->search_btn, icons::Name::Search, t.text2, t.text3, 12);
    }
    for (QPushButton *remove :
         st->page->findChildren<QPushButton *>(QStringLiteral("tag_remove"))) {
        icons::apply(remove, icons::Name::Clear, t.text2, t.text3, 12);
    }
    // 自绘控件（胶囊/开关/分段/评级）持令牌副本
    for (formkit::PillBadge *pill : st->pills) {
        pill->set_tokens(t);
    }
    for (formkit::PillBadge *pill : st->keyword_pills) {
        pill->set_tokens(t);
    }
    for (formkit::SwitchControl *sw : st->switches) {
        sw->set_tokens(t);
    }
    if (st->seg != nullptr) {
        st->seg->set_tokens(t);
    }
    if (st->layer_seg != nullptr) {
        st->layer_seg->set_tokens(t); // M6-T8：图层 Seg 自绘（段填充/字色取令牌）
    }
    if (st->info_rating != nullptr) {
        st->info_rating->set_tokens(t);
    }
    // M6-T8：结果行重排（图钉图标取 --acc；空态占位取 --txt3 —— 与 QSS 同一次主题重放）
    if (st->search_results != nullptr) {
        fill_result_rows(st);
    }

    // 浅色卡阴影（§4.1 shadow；深色无阴影）
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

// 卡头（§7.1/mockup .card-h：标题 12/700 + **中性状态胶囊**（1-24/1-25：卡头右置元素统一规格）+
// 右端 hint（10px/--txt3，超长省略））
struct CardHeader {
    QWidget *row = nullptr;
    QLabel *title = nullptr;
    formkit::PillBadge *pill = nullptr;
    ElidedLabel *hint = nullptr;
};

CardHeader make_header(MetaState *st, QWidget *parent, const QString &title,
                       const QString &pill_name, const QString &hint_name) {
    CardHeader out;
    out.row = new QWidget(parent);
    auto *layout = new QHBoxLayout(out.row);
    layout->setContentsMargins(theme::Metrics::card_pad_x, 9, theme::Metrics::card_pad_x, 7);
    layout->setSpacing(theme::Metrics::group_gap);
    out.title = new QLabel(title, out.row);
    out.title->setObjectName(QStringLiteral("pp-card-title"));
    out.title->setFont(
        theme::font(theme::Typography::card_title_px, theme::Typography::card_title_weight));
    layout->addWidget(out.title);
    out.pill = new formkit::PillBadge(out.row);
    if (!pill_name.isEmpty()) {
        out.pill->setObjectName(pill_name);
    }
    out.pill->set_tone(formkit::PillBadge::Tone::Neutral); // 1-24/1-25：卡头右置一律中性
    out.pill->setVisible(false);
    layout->addWidget(out.pill);
    st->add_pill(out.pill);
    layout->addStretch(1);
    out.hint = new ElidedLabel(out.row);
    if (!hint_name.isEmpty()) {
        out.hint->setObjectName(hint_name);
    }
    out.hint->setProperty(kFieldProperty, QStringLiteral("hint"));
    out.hint->setFont(theme::font(theme::Typography::hint_px, 500));
    out.hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(out.hint, 1);
    return out;
}

// 卡体（R1：下内边距 12；上侧由卡头下沿承担）
QVBoxLayout *card_body(QWidget *card) {
    auto *body = new QWidget(card);
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                          theme::Metrics::card_pad_bottom);
    v->setSpacing(0);
    return v;
}

ElidedLabel *make_info_value(QWidget *parent, const QString &object_name) {
    auto *value = new ElidedLabel(parent);
    value->setObjectName(object_name);
    value->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkValue));
    value->setFont(theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
    value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter); // kv 值：正文语序（mockup .kv-v 左起）
    return value;
}

QWidget *build_info_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    const CardHeader header =
        make_header(st, card, PageMeta::tr("关键信息"), QStringLiteral("pp-meta-info-pill"),
                    QStringLiteral("pp-meta-info-hint"));
    st->info_pill = header.pill;
    st->info_hint = header.hint;
    v->addWidget(header.row);

    auto *body = new QWidget(card);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                           theme::Metrics::card_pad_bottom);
    bv->setSpacing(0);

    // kv 双列 5 行（§7.1 逐行）：拍摄时间/相机、镜头/曝光、尺寸/色彩、文件/GPS、白平衡/评级
    const std::array<QString, 10> keys = {
        PageMeta::tr("拍摄时间"), PageMeta::tr("相机"), PageMeta::tr("镜头"), PageMeta::tr("曝光"),
        PageMeta::tr("尺寸"),     PageMeta::tr("色彩"), PageMeta::tr("文件"), PageMeta::tr("GPS"),
        PageMeta::tr("白平衡"),   PageMeta::tr("评级")};
    const std::array<const char *, 8> hooks = {
        "pp-meta-info-time", "pp-meta-info-camera", "pp-meta-info-lens", "pp-meta-info-exposure",
        "pp-meta-info-size", "pp-meta-info-color",  "pp-meta-info-file", "pp-meta-info-gps"};
    for (int i = 0; i < 5; ++i) {
        formkit::FieldCell left;
        formkit::FieldCell right;
        left.label = keys[std::size_t(i * 2)];
        right.label = keys[std::size_t(i * 2 + 1)];
        if (i == 4) {
            // 第 5 行：白平衡 / 评级（矢量星；无评级 → 「—」）
            st->info_wb = make_info_value(body, QStringLiteral("pp-meta-info-wb"));
            left.field = st->info_wb;
            TrackGroup rating = make_group(body);
            st->info_rating = new RatingStars(rating.widget());
            st->info_rating->setObjectName(QStringLiteral("pp-meta-info-rating"));
            rating.add(st->info_rating);
            st->info_rating_none = new QLabel(QStringLiteral("—"), rating.widget());
            rating.add(st->info_rating_none);
            rating.stretch();
            right.field = rating.widget();
        } else {
            ElidedLabel *left_value =
                make_info_value(body, QString::fromLatin1(hooks[std::size_t(i * 2)]));
            ElidedLabel *right_value =
                make_info_value(body, QString::fromLatin1(hooks[std::size_t(i * 2 + 1)]));
            st->info_value[std::size_t(i * 2)] = left_value;
            st->info_value[std::size_t(i * 2 + 1)] = right_value;
            left.field = left_value;
            if (i == 3) {
                // 第 4 行右格：GPS 值 + 「已有」胶囊（mockup .kv-v.cells + .pill.ok）
                TrackGroup gps = make_group(body);
                gps.add(right_value, 1);
                gps.gap(theme::Metrics::group_gap);
                st->info_gps_pill = formkit::PillBadge::of(
                    gps.widget(), formkit::PillBadge::Tone::Ok, PageMeta::tr("已有"));
                st->info_gps_pill->setObjectName(QStringLiteral("pp-meta-info-gps-pill"));
                st->info_gps_pill->setVisible(false);
                st->add_pill(st->info_gps_pill);
                gps.add(st->info_gps_pill);
                gps.stretch();
                right.field = gps.widget();
            } else {
                right.field = right_value;
            }
        }
        left.width = FieldWidth::Flex;
        right.width = FieldWidth::Flex;
        const formkit::DoubleRow row = formkit::double_row(body, left, right);
        row.row->setObjectName(QStringLiteral("pp-meta-info-row-%1").arg(i));
        bv->addWidget(row.row);
        st->info_row[std::size_t(i)] = row.row;
    }
    // 空态一行（修 1-23）：未选择文件 → 一行说明（行高仍 28，栅格不错位）
    {
        st->info_empty = new ElidedLabel(body);
        st->info_empty->setObjectName(QStringLiteral("pp-meta-info-empty"));
        st->info_empty->setProperty(kFieldProperty, QStringLiteral("empty"));
        st->info_empty->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        st->info_empty->set_full_text(PageMeta::tr(kNoSelectionText));
        const formkit::Row empty_row =
            formkit::frow(body, QString(), st->info_empty, FieldWidth::Flex);
        empty_row.row->setObjectName(QStringLiteral("pp-meta-info-empty-row"));
        st->info_empty_row = empty_row.row;
        st->info_empty_row->setVisible(false);
        bv->addWidget(st->info_empty_row);
    }
    v->addWidget(body);
    return card;
}

QWidget *build_time_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    const CardHeader header =
        make_header(st, card, PageMeta::tr("时间偏移"), QStringLiteral("pp-meta-time-pill"),
                    QStringLiteral("pp-meta-time-hint"));
    st->time_pill = header.pill;
    st->time_hint = header.hint;
    st->time_hint->set_full_text(PageMeta::tr("修改值以强调色标记"));
    v->addWidget(header.row);

    auto *body = new QWidget(card);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                           theme::Metrics::card_pad_bottom);
    bv->setSpacing(0);

    // 模式（§6：互斥模式唯一形态 = 分段控件，段宽 88 等宽）
    {
        TrackGroup group = make_group(body);
        st->seg = new formkit::SegmentedControl(group.widget());
        st->seg->setObjectName(QStringLiteral("pp-meta-seg"));
        st->seg->add_segment(PageMeta::tr("Δ 偏移"), QStringLiteral("pp-meta-seg-delta"));
        st->seg->add_segment(PageMeta::tr("时区语义"), QStringLiteral("pp-meta-seg-tz"));
        group.add(st->seg);
        group.stretch();
        bv->addWidget(
            formkit::frow(body, PageMeta::tr("模式"), group.widget(), FieldWidth::Flex).row);
    }
    // 日期偏移 / 时间偏移：3×spin88（短档）+ 单位槽 + 范围注释（§7.1 逐行）
    const auto offset_row = [&](const QString &label, const std::array<QString, 3> &units,
                                const std::array<const char *, 3> &hooks, const QString &note,
                                int first_index) {
        TrackGroup group = make_group(body);
        for (int i = 0; i < 3; ++i) {
            if (i > 0) {
                group.gap(theme::Metrics::group_gap);
            }
            auto *spin = new DeltaSpin(group.widget(), QString::fromLatin1(hooks[std::size_t(i)]));
            st->delta[std::size_t(first_index + i)] = spin;
            group.add(spin);
            group.gap(formkit::kInlineGap);
            group.add(unit_slot(group.widget(), units[std::size_t(i)]));
        }
        group.gap(theme::Metrics::group_gap);
        group.add(note_label(group.widget()));
        group.stretch();
        return formkit::frow(body, label, group.widget(), FieldWidth::Flex).row;
    };
    st->delta_row = offset_row(
        PageMeta::tr("日期偏移"), {PageMeta::tr("年"), PageMeta::tr("月"), PageMeta::tr("日")},
        {"pp-meta-delta-years", "pp-meta-delta-months", "pp-meta-delta-days"},
        PageMeta::tr("−99…+99"), 0);
    bv->addWidget(st->delta_row);
    st->time_row = offset_row(
        PageMeta::tr("时间偏移"), {PageMeta::tr("时"), PageMeta::tr("分"), PageMeta::tr("秒")},
        {"pp-meta-delta-hours", "pp-meta-delta-minutes", "pp-meta-delta-seconds"},
        PageMeta::tr("UTC±HH:MM"), 3);
    bv->addWidget(st->time_row);

    // 时区语义（从 / 到）：双列行（R2 标准档 160 —— A2 的第二标签 x = 276）
    {
        formkit::FieldCell left;
        formkit::FieldCell right;
        left.label = PageMeta::tr("从");
        right.label = PageMeta::tr("到");
        for (const bool is_from : {true, false}) {
            formkit::ComboSpec spec;
            spec.object_name =
                is_from ? QStringLiteral("pp-meta-tz-from") : QStringLiteral("pp-meta-tz-to");
            spec.accessible = is_from ? PageMeta::tr("源时区") : PageMeta::tr("目标时区");
            spec.tooltip = is_from ? PageMeta::tr("源时区（EXIF 记录所用的时区）")
                                   : PageMeta::tr("目标时区（改写后的墙钟时区）");
            auto *combo = formkit::combo_box(body, spec);
            fill_timezone_combo(combo);
            select_timezone(combo, 480);
            (is_from ? left : right).field = combo;
            (is_from ? st->tz_from : st->tz_to) = combo;
        }
        left.width = FieldWidth::Standard;
        right.width = FieldWidth::Standard;
        const formkit::DoubleRow row = formkit::double_row(body, left, right);
        row.row->setObjectName(QStringLiteral("pp-meta-tz-row"));
        st->tz_row = row.row;
        st->tz_row->setVisible(false);
        bv->addWidget(st->tz_row);
    }

    // 生效时间（§7.1）：[标签 生效时间][原（删除线）→ 生效（强调）] + 「日期 · Δ · 字段名」注释
    {
        TrackGroup group = make_group(body);
        st->time_old = new ElidedLabel(group.widget());
        st->time_old->setObjectName(QStringLiteral("pp-meta-time-old-0"));
        st->time_old->setProperty(kFieldProperty, QStringLiteral("old"));
        QFont old_font = theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true);
        old_font.setStrikeOut(true); // mockup .tline .old{text-decoration:line-through}
        st->time_old->setFont(old_font);
        group.add(st->time_old);
        group.gap(theme::Metrics::group_gap);
        st->time_arrow = new QLabel(QStringLiteral("→"), group.widget());
        st->time_arrow->setProperty(kFieldProperty, QStringLiteral("arrow"));
        st->time_arrow->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        group.add(st->time_arrow);
        group.gap(theme::Metrics::group_gap);
        st->time_new = new ElidedLabel(group.widget());
        st->time_new->setObjectName(QStringLiteral("pp-meta-time-new-0"));
        st->time_new->setProperty(kFieldProperty, QStringLiteral("new"));
        st->time_new->setFont(
            theme::font(theme::Typography::body_px, QFont::Bold, /*tabular=*/true));
        group.add(st->time_new);
        group.gap(theme::Metrics::group_gap);
        st->time_note = note_label(group.widget(), QStringLiteral("pp-meta-time-note-0"));
        group.add(st->time_note);
        group.gap(formkit::kInlineGap);
        st->time_src = note_label(group.widget(), QStringLiteral("pp-meta-time-src-0"));
        group.add(st->time_src);
        group.stretch();
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("生效时间"), group.widget(), FieldWidth::Flex);
        st->effect_label = row.label;
        row.row->setObjectName(QStringLiteral("pp-meta-effect-row"));
        st->effect_row = row.row;
        bv->addWidget(st->effect_row);
    }
    // 空态一行（修 1-31：未选择文件 → 一行说明）
    {
        st->time_empty = new ElidedLabel(body);
        st->time_empty->setObjectName(QStringLiteral("pp-meta-time-empty"));
        st->time_empty->setProperty(kFieldProperty, QStringLiteral("empty"));
        st->time_empty->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        st->time_empty->set_full_text(PageMeta::tr(kNoSelectionText));
        const formkit::Row row = formkit::frow(body, QString(), st->time_empty, FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-meta-time-empty-row"));
        st->time_empty_row = row.row;
        st->time_empty_row->setVisible(false);
        bv->addWidget(st->time_empty_row);
    }
    // 多选逐行小表（§5.2；上限 8 行 + 「…还有 N 个」）
    {
        st->multi_host = new QWidget(body);
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
        bv->addWidget(st->multi_host);
    }

    // ---- 连接（任一控件变化 → 通知 + 生效值刷新）----
    const auto on_time_changed = [st] {
        if (st->loading) {
            return;
        }
        refresh_time_inputs(st);
        refresh_effective(st);
        notify_rules_changed(st);
    };
    QObject::connect(st->seg, &formkit::SegmentedControl::current_changed, st,
                     [on_time_changed](int) { on_time_changed(); });
    for (QSpinBox *spin : st->delta) {
        QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), st,
                         [on_time_changed](int) { on_time_changed(); });
    }
    for (QComboBox *combo : {st->tz_from, st->tz_to}) {
        QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), st,
                         [on_time_changed](int) { on_time_changed(); });
    }
    v->addWidget(body);
    return card;
}

QWidget *build_gps_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    const CardHeader header =
        make_header(st, card, PageMeta::tr("GPS"), QStringLiteral("pp-meta-gps-pill"),
                    QStringLiteral("pp-meta-gps-hint"));
    st->gps_pill = header.pill;
    st->gps_hint = header.hint;
    st->gps_hint->set_full_text(PageMeta::tr("跟随选中 · WGS-84"));
    v->addWidget(header.row);

    auto *body = new QWidget(card);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                           theme::Metrics::card_pad_bottom);
    bv->setSpacing(0);

    st->gps_warn = new QLabel(PageMeta::tr("GPS 坐标无效"), card);
    st->gps_warn->setObjectName(QStringLiteral("gps_warning"));
    st->gps_warn->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    st->gps_warn->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x, 4);
    st->gps_warn->setVisible(false);
    v->addWidget(st->gps_warn);

    // 生效坐标（只读；§5.2「GPS 卡显示生效坐标」）+ 海拔/方位（双列轨，fill）——§7.1 的
    // 控件集合 = 纬度/经度｜海拔/方位｜地图｜操作（生效时间戳读数进坐标行 tooltip，信息不丢）
    st->view_lat = make_view_field(card, QStringLiteral("pp-meta-gps-lat"));
    st->view_lon = make_view_field(card, QStringLiteral("pp-meta-gps-lon"));
    st->view_alt = make_view_field(card, QStringLiteral("pp-meta-gps-alt"));
    st->view_dir = make_view_field(card, QStringLiteral("pp-meta-gps-dir"));
    {
        formkit::FieldCell l;
        formkit::FieldCell r;
        l.label = PageMeta::tr("纬度");
        r.label = PageMeta::tr("经度");
        l.field = st->view_lat;
        r.field = st->view_lon;
        l.width = FieldWidth::Flex;
        r.width = FieldWidth::Flex;
        const formkit::DoubleRow row = formkit::double_row(body, l, r);
        row.row->setObjectName(QStringLiteral("pp-meta-gps-coord-row"));
        bv->addWidget(row.row);
    }
    {
        formkit::FieldCell l;
        formkit::FieldCell r;
        l.label = PageMeta::tr("海拔");
        r.label = PageMeta::tr("方位");
        l.field = st->view_alt;
        r.field = st->view_dir;
        l.width = FieldWidth::Flex;
        r.width = FieldWidth::Flex;
        const formkit::DoubleRow row = formkit::double_row(body, l, r);
        row.row->setObjectName(QStringLiteral("pp-meta-gps-altdir-row"));
        bv->addWidget(row.row);
    }

    // 操作行（修 1-37：**固定在地图上方**，核心动作不再落进内滚盲区；按钮高 26 = A7）
    {
        TrackGroup group = make_group(body);
        st->read_gps = make_button(group.widget(), QStringLiteral("pp-meta-gps-read"),
                                   PageMeta::tr("从文件读取"));
        st->read_gps->setFont(theme::font(theme::Typography::body_px, 600));
        st->read_gps->setEnabled(false);
        st->pick_map = make_button(group.widget(), QStringLiteral("pp-meta-gps-pick"),
                                   PageMeta::tr("地图选点"));
        st->pick_map->setFont(theme::font(theme::Typography::body_px, 600));
        st->pick_map->setToolTip(PageMeta::tr("取地图中心为坐标（可先拖拽地图定位）"));
        st->clear_gps = new QPushButton(PageMeta::tr("清除 GPS"), group.widget());
        st->clear_gps->setObjectName(QStringLiteral("pp-meta-gps-clear"));
        st->clear_gps->setProperty(kFieldProperty, QStringLiteral("danger"));
        st->clear_gps->setCheckable(true);
        st->clear_gps->setFixedHeight(theme::Metrics::btn_h);
        st->clear_gps->setCursor(Qt::PointingHandCursor);
        st->clear_gps->setFont(theme::font(theme::Typography::body_px, 600));
        st->clear_gps->setToolTip(PageMeta::tr("输出不含 GPS（清除优先于坐标）"));
        group.add(st->read_gps);
        group.gap(theme::Metrics::group_gap);
        group.add(st->pick_map);
        group.gap(theme::Metrics::group_gap);
        group.add(st->clear_gps);
        group.stretch();
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("操作"), group.widget(), FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-meta-gps-ops-row"));
        bv->addWidget(row.row);
    }

    // 图层切换（M6-T8/§5.2）：formkit Seg 三态（矢量/卫星/混合）→ set_tile_style；地图源下拉保留
    {
        TrackGroup group = make_group(body);
        st->layer_seg = new formkit::SegmentedControl(group.widget());
        st->layer_seg->setObjectName(QStringLiteral("pp-meta-gps-layer"));
        st->layer_seg->add_segment(PageMeta::tr("矢量"), QStringLiteral("pp-meta-gps-layer-road"));
        st->layer_seg->add_segment(PageMeta::tr("卫星"),
                                   QStringLiteral("pp-meta-gps-layer-satellite"));
        st->layer_seg->add_segment(PageMeta::tr("混合"),
                                   QStringLiteral("pp-meta-gps-layer-hybrid"));
        group.add(st->layer_seg);
        group.stretch();
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("图层"), group.widget(), FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-meta-gps-layer-row"));
        bv->addWidget(row.row);
    }

    // 地图源（§7.1「OSM 下拉」；有标签；切换 = 本卡地图的显示源）
    {
        formkit::ComboSpec provider_spec;
        provider_spec.object_name = QStringLiteral("pp-meta-gps-provider");
        provider_spec.accessible = PageMeta::tr("地图源");
        provider_spec.tooltip = PageMeta::tr("地图瓦片源（设置·地图 的持久档在外层设置里）");
        st->provider = formkit::combo_box(body, provider_spec);
        for (const QString &id : pp::map::provider_ids()) {
            const pp::map::TileProvider *provider = pp::map::provider_by_id(id);
            st->provider->addItem(provider != nullptr ? provider->label : id, id);
        }
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("地图源"), st->provider, FieldWidth::Standard);
        row.row->setObjectName(QStringLiteral("pp-meta-gps-provider-row"));
        bv->addWidget(row.row);
    }
    // 地点搜索（label + flex 输入 + 搜索钮；M6-T8：输入防抖 200ms → 联想下拉）
    {
        TrackGroup group = make_group(body);
        st->search_edit =
            make_rule_field(group.widget(), QStringLiteral("pp-meta-gps-search-edit"));
        st->search_edit->setPlaceholderText(PageMeta::tr(kSearchPlaceholder));
        group.add(st->search_edit, 1);
        group.gap(theme::Metrics::group_gap);
        st->search_btn = make_button(group.widget(), QStringLiteral("pp-meta-gps-search-button"),
                                     PageMeta::tr("搜索"));
        st->search_btn->setFont(theme::font(theme::Typography::body_px, 600));
        group.add(st->search_btn);
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("地点搜索"), group.widget(), FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-meta-gps-search-row"));
        bv->addWidget(row.row);
    }
    // 联想下拉（M6-T8/§4.3）：搜索框正下方，默认隐藏；有建议才展开（无匹配/错误 → 收起）
    {
        st->suggest_list = new QListWidget(body);
        st->suggest_list->setObjectName(QStringLiteral("pp-meta-gps-suggest"));
        st->suggest_list->setAccessibleName(PageMeta::tr("搜索联想"));
        st->suggest_list->setToolTip(PageMeta::tr("选择联想项：地图定位到该地点"));
        st->suggest_list->setFixedHeight(kSuggestVisibleRows * formkit::kChipH + 6);
        st->suggest_list->setSelectionMode(QAbstractItemView::SingleSelection);
        st->suggest_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        st->suggest_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        st->suggest_list->setTextElideMode(Qt::ElideRight);
        st->suggest_list->setVisible(false);
        bv->addWidget(st->suggest_list);
    }
    // 分类 chips（M6-T8/§4.3/§5.1）：一排类目关键词，点选即按类目 run_search（互斥选中）
    {
        TrackGroup group = make_group(body);
        st->category_group = new QButtonGroup(group.widget());
        st->category_group->setExclusive(true);
        for (int i = 0; i < int(kCategoryKeywords.size()); ++i) {
            const QString keyword = PageMeta::tr(kCategoryKeywords[std::size_t(i)]);
            auto *chip = make_chip(group.widget(), QStringLiteral("pp-meta-gps-category-%1").arg(i),
                                   keyword);
            st->category_group->addButton(chip, i);
            st->category_chips.append(chip);
            if (i > 0) {
                group.gap(formkit::kInlineGap);
            }
            group.add(chip);
            QObject::connect(chip, &QPushButton::clicked, st, [st, keyword] {
                if (st->clear_gps->isChecked()) {
                    return; // 清除 GPS 锁定（控件已禁用；双保险）
                }
                {
                    const QSignalBlocker blocker(st->search_edit); // 关键词回填不算"手输"
                    st->search_edit->setText(keyword);
                }
                start_search(st, /*from_category=*/true);
            });
        }
        group.stretch();
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("分类"), group.widget(), FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-meta-gps-category-row"));
        bv->addWidget(row.row);
    }
    // 搜索结果（M6-T8：纯文本下拉 → **可点结果列表**；每行 = 名称 + 地址 + 距离，点行 →
    // search_select(index)。行高超出 R1 的 28 → 本块沿用 R2 的标签轨几何（80 / 列距 12），
    // 列表作为 flex 轨撑满；容器名不带 "-row" 后缀（不进 R1 行节奏探针）。
    {
        auto *host = new QWidget(body);
        host->setObjectName(QStringLiteral("pp-meta-gps-results-host"));
        auto *line = new QHBoxLayout(host);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(formkit::kColGap);
        auto *label = new QLabel(PageMeta::tr("搜索结果"), host);
        label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkLabel));
        label->setFont(theme::font(theme::Typography::small_px, QFont::Normal));
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setFixedSize(formkit::kLabelW, formkit::kRowPitch);
        line->addWidget(label, 0, Qt::AlignTop);
        st->search_results = new QListWidget(host);
        st->search_results->setObjectName(QStringLiteral("pp-meta-gps-search-results"));
        st->search_results->setAccessibleName(PageMeta::tr("搜索结果"));
        st->search_results->setToolTip(
            PageMeta::tr("点选结果：地图居中并落点（可在图上继续微调）"));
        st->search_results->setFixedHeight(kResultVisibleRows * kResultRowH + 4);
        st->search_results->setSelectionMode(QAbstractItemView::SingleSelection);
        st->search_results->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        st->search_results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        st->search_results->setTextElideMode(Qt::ElideRight);
        line->addWidget(st->search_results, 1);
        bv->addWidget(host);
    }
    // 地图（固定高 136；图钉由生效坐标驱动、版权行由地图自绘）
    {
        TrackGroup group = make_group(body);
        st->map = new pp::map::MapWidget(group.widget());
        st->map->setObjectName(QStringLiteral("pp-meta-map"));
        st->map->setFixedHeight(kMapHeight);
        group.add(st->map, 1);
        bv->addWidget(group.widget());
    }
    // 地址回显行（M6-T8/§4.5）：选点后由 reverse_finished 回填；未选点 = "—"
    {
        st->address_value = new ElidedLabel(body);
        st->address_value->setObjectName(QStringLiteral("pp-meta-gps-address"));
        st->address_value->setProperty(kFieldProperty, QStringLiteral("address"));
        st->address_value->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        st->address_value->set_full_text(QStringLiteral("—"));
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("地址"), st->address_value, FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-meta-gps-address-row"));
        st->address_row = row.row;
        bv->addWidget(st->address_row);
    }
    v->addWidget(body);
    st->gps_status = new QLabel(card);
    st->gps_status->setObjectName(QStringLiteral("gps_status"));
    st->gps_status->setProperty(kFieldProperty, QStringLiteral("sub"));
    st->gps_status->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    st->gps_status->setContentsMargins(theme::Metrics::card_pad_x, 4, theme::Metrics::card_pad_x,
                                       4);
    v->addWidget(st->gps_status);

    // 批量规则折叠行（修 1-37：拆分混合控件 → 纯折叠行，默认折叠）
    st->rule_toggle = new QToolButton(card);
    st->rule_toggle->setObjectName(QStringLiteral("pp-meta-gps-rule-toggle"));
    st->rule_toggle->setProperty(kFieldProperty, QStringLiteral("fold"));
    st->rule_toggle->setText(PageMeta::tr("批量 GPS 规则"));
    st->rule_toggle->setCheckable(true);
    st->rule_toggle->setChecked(false);
    st->rule_toggle->setAutoRaise(true);
    st->rule_toggle->setFixedHeight(theme::Metrics::btn_h);
    st->rule_toggle->setFont(theme::font(theme::Typography::body_px, 600));
    {
        auto *toggle_row = new QHBoxLayout();
        toggle_row->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                                       6);
        toggle_row->addWidget(st->rule_toggle, 0, Qt::AlignLeft);
        toggle_row->addStretch(1);
        v->addLayout(toggle_row);
    }

    // 规则编辑面（折叠内容）：启用（表单布尔）+ 纬度/经度 + 可选字段（海拔/方位/时间戳）
    st->rule_host = new QWidget(card);
    st->rule_host->setObjectName(QStringLiteral("pp-meta-gps-rule"));
    auto *rule_body = st->rule_host;
    auto *rule_v = new QVBoxLayout(rule_body);
    rule_v->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x, 10);
    rule_v->setSpacing(0);
    {
        const formkit::CheckRow enable = formkit::check_row(
            rule_body, QString(), PageMeta::tr("启用"), QStringLiteral("gps_enable"));
        st->gps_enable = enable.box;
        rule_v->addWidget(enable.row);
    }
    const auto make_rule_grid_pair = [&](const QString &label_a, QLineEdit *edit_a,
                                         const QString &unit_a, const QString &label_b,
                                         QLineEdit *edit_b, const QString &unit_b) {
        formkit::FieldCell l;
        formkit::FieldCell r;
        l.label = label_a;
        l.field = edit_a;
        l.width = FieldWidth::Flex;
        l.unit = unit_a;
        r.label = label_b;
        r.field = edit_b;
        r.width = FieldWidth::Flex;
        r.unit = unit_b;
        return formkit::double_row(rule_body, l, r).row;
    };
    st->lat = make_rule_field(rule_body, QStringLiteral("gps_lat"));
    st->lat->setValidator(new QDoubleValidator(-90.0, 90.0, 6, st->lat));
    st->lon = make_rule_field(rule_body, QStringLiteral("gps_lon"));
    st->lon->setValidator(new QDoubleValidator(-180.0, 180.0, 6, st->lon));
    rule_v->addWidget(make_rule_grid_pair(PageMeta::tr("纬度"), st->lat, QStringLiteral("°"),
                                          PageMeta::tr("经度"), st->lon, QStringLiteral("°")));
    st->more_toggle = new QToolButton(rule_body);
    st->more_toggle->setObjectName(QStringLiteral("gps_more_toggle"));
    st->more_toggle->setProperty(kFieldProperty, QStringLiteral("fold"));
    st->more_toggle->setText(PageMeta::tr("更多字段"));
    st->more_toggle->setCheckable(true);
    st->more_toggle->setChecked(false);
    st->more_toggle->setAutoRaise(true);
    st->more_toggle->setFixedHeight(theme::Metrics::btn_h);
    st->more_toggle->setFont(theme::font(theme::Typography::body_px, 600));
    {
        auto *more_row = new QHBoxLayout();
        more_row->setContentsMargins(0, 0, 0, 0);
        more_row->addWidget(st->more_toggle, 0, Qt::AlignLeft);
        more_row->addStretch(1);
        rule_v->addLayout(more_row);
    }
    st->alt = make_rule_field(rule_body, QStringLiteral("gps_alt"));
    st->dir = make_rule_field(rule_body, QStringLiteral("gps_dir"));
    st->ts = make_rule_field(rule_body, QStringLiteral("gps_ts"));
    QWidget *altdir_row = make_rule_grid_pair(PageMeta::tr("海拔"), st->alt, QStringLiteral("m"),
                                              PageMeta::tr("方位"), st->dir, QStringLiteral("°"));
    QWidget *ts_row =
        formkit::frow(rule_body, PageMeta::tr("时间戳"), st->ts, FieldWidth::Flex).row;
    rule_v->addWidget(altdir_row);
    rule_v->addWidget(ts_row);
    for (QWidget *hidden : {altdir_row, ts_row}) {
        hidden->setVisible(false);
    }
    st->rule_host->setVisible(false);
    v->addWidget(st->rule_host);

    // ---- 连接 ----
    QObject::connect(st->provider, QOverload<int>::of(&QComboBox::currentIndexChanged), st,
                     [st](int index) {
                         const QString id = st->provider->itemData(index).toString();
                         if (!id.isEmpty()) {
                             st->map->set_provider(id);
                         }
                         refresh_layer_hint(st); // 换源 → 图层支持性提示随之更新（§5.2）
                         // 换源 = 换地理编码源：旧结果/联想不再代表当前源 → 收干净
                         st->results.clear();
                         set_hook(st, "pp_meta_gps_results", 0);
                         fill_result_rows(st);
                         st->suggest_list->clear();
                         st->suggest_list->setVisible(false);
                         set_hook(st, "pp_meta_gps_suggest", 0);
                     });
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
    QObject::connect(st->gps_enable, &formkit::CheckBox::toggled, st, [st](bool) {
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
    QObject::connect(st->more_toggle, &QToolButton::toggled, st, [st, altdir_row, ts_row](bool on) {
        set_fold_icon(st, st->more_toggle, on); // 插入符 = 矢量图标（C3）
        altdir_row->setVisible(on);
        ts_row->setVisible(on);
    });
    QObject::connect(st->rule_toggle, &QToolButton::toggled, st, [st](bool on) {
        st->rule_host->setVisible(on);
        set_fold_icon(st, st->rule_toggle, on);
    });

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

    // 地图选点：取视口中心为规则坐标（点击地图仍直接取点 —— v0.2 行为不变；M6-T8 起叠加地址回显）
    QObject::connect(st->pick_map, &QPushButton::clicked, st, [st] {
        apply_picked_point(st, st->map->center_lat(), st->map->center_lon(),
                           PageMeta::tr("已取地图中心为坐标"));
    });

    // 搜索（M6-T8）：搜索钮 / 回车 → run_search；输入防抖 200ms → run_suggest（联想下拉）
    QObject::connect(st->search_btn, &QPushButton::clicked, st, [st] { start_search(st, false); });
    QObject::connect(st->search_edit, &QLineEdit::returnPressed, st->search_btn,
                     &QPushButton::click);
    st->suggest_timer = new QTimer(st);
    st->suggest_timer->setSingleShot(true);
    st->suggest_timer->setInterval(kSuggestDebounceMs);
    QObject::connect(st->suggest_timer, &QTimer::timeout, st,
                     [st] { st->map->run_suggest(st->search_edit->text().trimmed()); });
    QObject::connect(st->search_edit, &QLineEdit::textChanged, st, [st](const QString &text) {
        if (st->loading) {
            return; // 程序化回填（apply_rules/联想选中）不算输入
        }
        clear_category_checks(st); // 手输关键词 = 与类目选择脱钩
        if (text.trimmed().isEmpty()) {
            st->suggest_timer->stop();
            st->suggest_list->clear();
            st->suggest_list->setVisible(false);
            set_hook(st, "pp_meta_gps_suggest", 0);
            return;
        }
        st->suggest_timer->start(); // 200ms 防抖（§4.3）
    });
    QObject::connect(st->map, &pp::map::MapWidget::suggest_finished, st,
                     [st](const std::vector<pp::map::SuggestItem> &items, const QString &error) {
                         st->suggest_list->clear();
                         if (!error.isEmpty() || items.empty()) {
                             // 联想是增量面：无匹配/错误不打断输入（状态行不被联想刷屏）
                             st->suggest_list->setVisible(false);
                             set_hook(st, "pp_meta_gps_suggest", 0);
                             return;
                         }
                         for (const pp::map::SuggestItem &item : items) {
                             auto *row = new QListWidgetItem(st->suggest_list);
                             row->setText(item.address.isEmpty() ? item.title
                                                                 : QStringLiteral("%1 · %2").arg(
                                                                       item.title, item.address));
                             row->setToolTip(row->text());
                             row->setData(Qt::UserRole, item.lat);
                             row->setData(Qt::UserRole + 1, item.lon);
                             row->setData(Qt::UserRole + 2, item.has_point);
                             row->setData(Qt::UserRole + 3, item.title);
                         }
                         st->suggest_list->setVisible(true);
                         set_hook(st, "pp_meta_gps_suggest", int(items.size()));
                     });
    QObject::connect(st->suggest_list, &QListWidget::itemClicked, st, [st](QListWidgetItem *item) {
        if (item == nullptr) {
            return;
        }
        st->suggest_list->setVisible(false);
        const bool has_point = item->data(Qt::UserRole + 2).toBool();
        const double lat = item->data(Qt::UserRole).toDouble();
        const double lon = item->data(Qt::UserRole + 1).toDouble();
        if (has_point && std::isfinite(lat) && std::isfinite(lon)) {
            // 选建议 = 直接定位（§4.3）：主钉 + 居中（≥16 档）+ 选点闭环
            st->map->set_marker(lat, lon);
            st->map->center_on(lat, lon, int(kSuggestZoom));
            apply_picked_point(st, lat, lon, PageMeta::tr("已从联想选择坐标"));
            return;
        }
        // 无坐标建议（纯地址）：以标题再搜一次
        const QString title = item->data(Qt::UserRole + 3).toString();
        if (title.isEmpty()) {
            return;
        }
        {
            const QSignalBlocker blocker(st->search_edit);
            st->search_edit->setText(title);
        }
        start_search(st, false);
    });
    // 结果列表（M6-T8）：数据源 = search_results_ready（与 map 内部 results 同序）
    QObject::connect(
        st->map, &pp::map::MapWidget::search_results_ready, st,
        [st](const std::vector<pp::map::GeocodeResult> &results, const QString &error) {
            st->results = results;
            set_hook(st, "pp_meta_gps_results", int(results.size()));
            fill_result_rows(st);
            if (!error.isEmpty()) {
                st->gps_status->setText(
                    error); // 失败分类文案（离线/无 Key/请求失败/解析失败/无匹配）
            } else if (results.empty()) {
                st->gps_status->setText(PageMeta::tr("没有匹配的结果"));
            } else {
                st->gps_status->setText(PageMeta::tr("搜索到 %1 个结果").arg(results.size()));
            }
        });
    QObject::connect(st->search_results, &QListWidget::itemClicked, st,
                     [st](QListWidgetItem *item) {
                         select_result(st, item != nullptr ? st->search_results->row(item) : -1);
                     });
    QObject::connect(st->search_results, &QListWidget::itemActivated, st,
                     [st](QListWidgetItem *item) {
                         select_result(st, item != nullptr ? st->search_results->row(item) : -1);
                     });
    // 图钉命中（M6-T8 接线；闭环收口见 T9）：列表选中第 i 行 + 回填坐标（点钉不算「取点」）
    QObject::connect(st->map, &pp::map::MapWidget::pin_clicked, st, [st](int index) {
        if (index < 0 || index >= int(st->results.size())) {
            return;
        }
        const pp::map::GeocodeResult &result = st->results[std::size_t(index)];
        st->search_results->setCurrentRow(index); // 列表跟随命中钉（不触发 search_select）
        apply_picked_point(st, result.lat, result.lon,
                           PageMeta::tr("已选结果：%1").arg(result.title));
    });
    // 逆地理回显（M6-T8/§4.5）：成功 → 地址行 + 状态行「已选点 · 地址」；失败 → 只落地址行
    // （选点状态不被错误文案顶掉；失败分类文案本身带指引）
    QObject::connect(st->map, &pp::map::MapWidget::reverse_finished, st,
                     [st](const QString &address, const QString &error) {
                         const QString text = error.isEmpty() ? address : error;
                         st->address_value->set_full_text(text.isEmpty() ? QStringLiteral("—")
                                                                         : text);
                         set_hook(st, "pp_meta_gps_address", text);
                         if (error.isEmpty() && !address.isEmpty()) {
                             st->gps_status->setText(PageMeta::tr("已选点 · %1").arg(address));
                         }
                     });
    QObject::connect(st->map, &pp::map::MapWidget::point_selected, st,
                     [st](double lat, double lon) {
                         apply_picked_point(st, lat, lon, PageMeta::tr("已从地图选择坐标"));
                     });
    // 图层（M6-T8/§5.2）：三态 Seg → set_tile_style（基准不漂由 mapwidget 单源保证）
    QObject::connect(st->layer_seg, &formkit::SegmentedControl::current_changed, st,
                     [st](int index) {
                         const int style = std::clamp(index, 0, 2);
                         st->map->set_tile_style(layer_style_of(style));
                         refresh_layer_hint(st);
                         set_hook(st, "pp_meta_gps_layer",
                                  QString::fromLatin1(kLayerStyleKeys[std::size_t(style)]));
                     });
    // 初始态（Road = map 的默认图层）+ 自验钩子读数
    refresh_layer_hint(st);
    set_hook(st, "pp_meta_gps_layer", QString::fromLatin1(kLayerStyleKeys[0]));
    set_hook(st, "pp_meta_gps_results", 0);
    set_hook(st, "pp_meta_gps_suggest", 0);
    set_hook(st, "pp_meta_gps_address", QString());
    fill_result_rows(st); // 空态 = 占位行（"搜索后在此选择"）
    return card;
}

QWidget *build_switches_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    const CardHeader header = make_header(st, card, PageMeta::tr("隐私与文件时间"),
                                          QStringLiteral("pp-meta-privacy-pill"), QString());
    st->privacy_pill = header.pill;
    v->addWidget(header.row);

    auto *body = new QWidget(card);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                           theme::Metrics::card_pad_bottom);
    bv->setSpacing(0);
    // W-R-T13（A39 ⑥ 缺陷根治）：卡体必须进卡布局 —— 缺这一行时 body 是"有父无布局"的孤儿
    // （几何停在默认 100×30、行被压成 6px pitch 并盖住卡题），截图里表现为卡题上的"圆形伪影"。
    // 与其余三张卡（build_info_card/build_time_card/build_gps_card）同一条接线。
    v->addWidget(body);

    // 开关行（§6：独立布尔（立即生效）= 开关）：[开关 30×16][说明][……][链]
    const auto switch_row = [&](const QString &label, const QString &object_name,
                                formkit::SwitchControl **out, const QString &sub,
                                const QString &link_name, const QString &link_text,
                                QToolButton **link_out) {
        TrackGroup group = make_group(body);
        auto *sw = new formkit::SwitchControl(group.widget());
        sw->setObjectName(object_name);
        sw->set_tokens(st->tokens);
        st->switches.append(sw);
        *out = sw;
        group.add(sw);
        group.gap(theme::Metrics::group_gap);
        auto *note = new QLabel(sub, group.widget());
        note->setProperty(kFieldProperty, QStringLiteral("sub"));
        note->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        group.add(note);
        group.stretch();
        QToolButton *link = make_link(group.widget(), link_name, link_text);
        group.add(link);
        *link_out = link;
        const formkit::Row row = formkit::frow(body, label, group.widget(), FieldWidth::Flex);
        return row.row;
    };
    bv->addWidget(switch_row(PageMeta::tr("隐私剥除"), QStringLiteral("privacy_strip"), &st->strip,
                             PageMeta::tr("输出前移除全部 EXIF / XMP，保留 ICC"),
                             QStringLiteral("privacy_strip_link"), PageMeta::tr("标签编辑…"),
                             &st->link_tags));
    bv->addWidget(switch_row(PageMeta::tr("时间同步"), QStringLiteral("mtime_sync"), &st->mtime,
                             PageMeta::tr("输出 mtime 跟随拍摄时间"),
                             QStringLiteral("mtime_sync_link"), PageMeta::tr("例外…"),
                             &st->link_exc));
    // 关键字（§7.1：胶囊组 + 「编辑…」链）
    {
        TrackGroup group = make_group(body);
        // 胶囊组本体 = 独立子容器（随跟随项重建；说明位/胶囊都在它内部）
        st->keyword_box = new QWidget(group.widget());
        st->keyword_row = new QHBoxLayout(st->keyword_box);
        st->keyword_row->setContentsMargins(0, 0, 0, 0);
        st->keyword_row->setSpacing(formkit::kInlineGap);
        st->keyword_none = new ElidedLabel(st->keyword_box);
        st->keyword_none->setObjectName(QStringLiteral("pp-meta-keyword-none"));
        st->keyword_none->setProperty(kFieldProperty, QStringLiteral("note"));
        st->keyword_none->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        st->keyword_none->set_full_text(PageMeta::tr(kNoKeywordsText));
        st->keyword_row->addWidget(st->keyword_none, 0, Qt::AlignVCenter);
        group.add(st->keyword_box);
        group.stretch();
        st->link_keywords = make_link(group.widget(), QStringLiteral("pp-meta-keywords-link"),
                                      PageMeta::tr("编辑…"));
        group.add(st->link_keywords);
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("关键字"), group.widget(), FieldWidth::Flex);
        bv->addWidget(row.row);
    }

    // 标签编辑（折叠；「标签编辑…」/「编辑…」入口）
    st->tag_host = new QWidget(card);
    st->tag_host->setObjectName(QStringLiteral("pp-meta-tag-host"));
    auto *tv = new QVBoxLayout(st->tag_host);
    tv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x, 8);
    auto *rows_host = new QWidget(st->tag_host);
    rows_host->setObjectName(QStringLiteral("tag_rows_host"));
    st->tag_rows = new QVBoxLayout(rows_host);
    st->tag_rows->setContentsMargins(0, 0, 0, 0);
    st->tag_rows->setSpacing(theme::Metrics::group_gap);
    tv->addWidget(rows_host);
    st->add_tag =
        make_button(st->tag_host, QStringLiteral("tag_add_button"), PageMeta::tr("添加标签"));
    tv->addWidget(st->add_tag, 0, Qt::AlignLeft);
    st->tag_host->setVisible(false);
    v->addWidget(st->tag_host);

    // 例外列表（折叠；「例外…」入口）
    st->exc_host = new QWidget(card);
    st->exc_host->setObjectName(QStringLiteral("pp-meta-exception-host"));
    auto *xv = new QVBoxLayout(st->exc_host);
    xv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x, 8);
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

    QObject::connect(st->strip, &formkit::SwitchControl::toggled, st, [st](bool) {
        if (!st->loading) {
            refresh_effective(st);
            notify_rules_changed(st);
        }
    });
    QObject::connect(st->mtime, &formkit::SwitchControl::toggled, st, [st](bool) {
        if (!st->loading) {
            refresh_effective(st);
            notify_rules_changed(st);
        }
    });
    const auto toggle_tags = [st] {
        const bool on = !st->tag_host->isVisible();
        st->tag_host->setVisible(on);
        set_fold_icon(st, st->link_tags, on);
        set_fold_icon(st, st->link_keywords, on);
    };
    QObject::connect(st->link_tags, &QToolButton::clicked, st, [toggle_tags] { toggle_tags(); });
    QObject::connect(st->link_keywords, &QToolButton::clicked, st,
                     [toggle_tags] { toggle_tags(); });
    QObject::connect(st->link_exc, &QToolButton::clicked, st, [st] {
        const bool on = !st->exc_host->isVisible();
        st->exc_host->setVisible(on);
        set_fold_icon(st, st->link_exc, on);
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
    v->setSpacing(theme::Metrics::gap); // 卡间距 8（§4.3）
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
    refresh_effective(st); // 「影响 N 个文件」与 (i/N) 跟随提示
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
    // 例外计数 = 卡头中性胶囊（1-24/1-25：右置元素统一规格；链接文案保持「例外…」）；
    // 无例外 → 不显示（§2.5「未发生的量不显示」）
    st->privacy_pill->setText(tr("例外 %1").arg(paths.size()));
    st->privacy_pill->setVisible(!paths.isEmpty());
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
    if (st->provider != nullptr) {
        const QSignalBlocker blocker(st->provider);
        const int index = st->provider->findData(provider_id);
        if (index >= 0) {
            st->provider->setCurrentIndex(index);
        }
    }
    refresh_layer_hint(st); // M6-T8：换源后图层支持性提示随之更新（信号被挡，这里补一次）
}

} // namespace pp::ui
