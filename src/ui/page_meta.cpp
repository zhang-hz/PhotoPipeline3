// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — metadata page (M1b-U7)
//
// 行为规格落地（docs/m1b-tasks.md §2.11；PP-FROZEN 头 = src/ui/page_meta.h）：
//   整页 QScrollArea；卡片自上而下：1 时间偏移 / 2 GPS（内嵌 MapWidget 高 260）/
//   3 标签修改 / 4 隐私剥除 / 5 文件时间 / 6 例外。
//   1. 启用开关 + 方式（Δ 偏移 / 时区语义）；Δ = 年..秒 6×QSpinBox(-9999..9999)；
//      时区 = 从/到 两个 QComboBox（UTC-12:00..UTC+14:00 整点 + 半时区；显示
//      "UTC+08:00"，数据 = 分钟数）；底部两行 QLabel 前后对照预览，数据源 =
//      set_batch_files 的第一个含时间文件（pp::read_metadata → pp::effective_datetime），
//      任一控件变化即时刷新；无文件 → "无文件"，无时间 → "首文件无时间字段"。
//   2. GPS：启用开关；纬度/经度 QLineEdit（QDoubleValidator -90..90 / -180..180，6 位小数）
//      + 实时 DMS 只读 QLabel（31°13'49.4"N 121°28'25.3"E，§3.2 格式）；"更多字段" 可折叠
//      （海拔/方位角/时间戳，可空）；内嵌 MapWidget（固定高 260）；地图上方搜索行
//      （QLineEdit + QPushButton + 结果 QComboBox，接 search_finished / search_select /
//      point_selected）；"从选中文件读取坐标"（set_selected_files 为空时禁用；读首个选中
//      文件 GPS → 回填字段 + set_marker；无 GPS → 状态提示）；"清除 GPS" 勾选 → 坐标输入
//      禁用且 rules() 输出 gps_clear=true、无 gps。
//   3. 标签修改：动态行列表（键/值/✕）+ 添加标签；值留空 = 删除该标签；
//      键 "Xmp." 前缀 → xmp_edits，否则 exif_edits。
//   4. 隐私剥除 / 5. 文件时间（mtime） / 6. 例外（双击 → open_editor_requested，
//      右键 "清除例外" → clear_exception_requested）。
//   rules() 装配（冻结）：启用且字段合法才填 time_shift/gps；GPS 勾选但坐标非法 → 忽略 gps
//   且卡片顶部黄色 "GPS 坐标无效" 提示；规则变化 → rules_changed()；apply_rules 不发信号。
//
// §2.11 未逐字冻结处的最小选择（报告已记录）：
//   - 时区下拉默认 从 = 到 = UTC+08:00（启用后不改变墙钟；等价 no-op）。
//   - 预览上行 "首文件：<源值>"、下行 "→ <结果值>"；未启用 → "→ （未启用）"。
//   - 清除 GPS 勾选优先于坐标（与 gps 互斥），且不依赖 "启用" 开关。
//   - "从选中文件读取坐标" 一并回填可选字段（海拔/方位角/时间戳），有值才填。
//   - 时间预览源扫描上限 200（§2.11 U7 落地口径 2026-09-20 冻结）：前 200 个文件内
//     未找到时间字段 → "前 200 个文件未找到时间字段"，停止扫描。
//   - §9.1 U7 修正（2026-09-20）：时间偏移/GPS 卡 "启用" 未勾选 → 卡内其余控件
//     setEnabled(false)（"清除 GPS" 复选框语义独立、始终可用；地图控件不受限）；
//     rules()/apply_rules 语义不变（未启用=不出规则），仅控件可用性变化。
//   - M2-T7（#22）：扫描移出 GUI 线程 —— `QtConcurrent::run` 在全局线程池执行"扫前
//     kPreviewScanCap 个文件找含时间者"，`QFutureWatcher` 在 GUI 线程回填标签。
//     worker 只捕获 QStringList 值拷贝，不触碰任何 GUI 状态；每次请求 +1 序号，
//     结果回填前校验序号（旧批次/旧请求的结果一律作废，不得乱序回填）。
//     扫描在途时预览两行留空（不显示上一批的陈旧文本，也不预报未定论文案）；
//     上限 200 与全部冻结文案（含 "前 200 个文件未找到时间字段"）逐字不变。
//     自验钩子（动态属性，供 .cache/tmp 自验程序读；非用户可见）：
//     pp_preview_pending / pp_preview_generation / pp_preview_stale_dropped。
#include "ui/page_meta.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QFuture>
#include <QFutureWatcher>
#include <QGroupBox>
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
#include <optional>
#include <string>
#include <vector>

#include "mapwidget/mapwidget.h"

namespace pp::ui {
namespace {

// PageMeta 头文件（PP-FROZEN）没有成员声明，全部状态挂在一个子 QObject 上，
// 经动态属性从 PageMeta* 取回（O(1)，随父对象析构）。
constexpr const char* kStateProperty = "pp_meta_state";
constexpr const char* kStateObjectName = "pp_meta_state";

// §2.11 U7 落地口径（2026-09-20 冻结）：时间预览源扫描上限（防千级无时间批次阻塞）
constexpr int kPreviewScanCap = 200;
// 超出上限未命中时的冻结文案（tr 模板见 refresh_time_preview）
constexpr const char* kPreviewCapText = QT_TR_NOOP("前 200 个文件未找到时间字段");

// §9.1 FIX2：GPS 卡控件禁用时清空占位文本（Qt 占位色不随 setEnabled 变化，像素实测
// ink 与启用态相同 → 会被误认为可编辑）；启用时恢复原始文案（不新造文案）。
constexpr const char* kLatPlaceholder = QT_TR_NOOP("例如 31.230400");
constexpr const char* kLonPlaceholder = QT_TR_NOOP("例如 121.473700");
constexpr const char* kSearchPlaceholder = QT_TR_NOOP("搜索地点");

// ---------------------------------------------------------------------------
// DMS 格式化（§3.2：31°13'49.4"N 121°28'25.3"E，度分秒一位小数）
// ---------------------------------------------------------------------------

QString dms_component(double value, bool latitude) {
    const double abs_v = std::isfinite(value) ? std::fabs(value) : 0.0;
    double tenths = std::round(abs_v * 36000.0) / 10.0;  // 0.1" 网格 → 无 60.0" 进位问题
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
// GPS 读取（§1.7 例外：本文件允许直接调 Exiv2 API）
// ---------------------------------------------------------------------------

// Exif.GPSInfo.* 有理数 → double；缺键 / 分母 0 → false
bool rational_at(const Exiv2::ExifData& exif, const char* key, std::size_t n, double& out) {
    const auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it == exif.end() || n >= it->count()) {
        return false;
    }
    const Exiv2::Rational r = it->toRational(n);
    if (r.second == 0) {
        return false;
    }
    out = double(r.first) / double(r.second);
    return true;
}

// 度 + 分/60 + 秒/3600（EXIF 三有理数）
bool dms_at(const Exiv2::ExifData& exif, const char* key, double& out) {
    double d = 0, m = 0, s = 0;
    if (!rational_at(exif, key, 0, d) || !rational_at(exif, key, 1, m) ||
        !rational_at(exif, key, 2, s)) {
        return false;
    }
    out = d + m / 60.0 + s / 3600.0;
    return true;
}

std::string exif_string_at(const Exiv2::ExifData& exif, const char* key) {
    const auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it == exif.end()) {
        return {};
    }
    return it->toString();
}

// "2/1 30/1 0/1" → "02:30:00"
bool gps_time_string(const Exiv2::ExifData& exif, std::string& out) {
    const std::string raw = exif_string_at(exif, "Exif.GPSInfo.GPSTimeStamp");
    if (raw.empty()) {
        return false;
    }
    int parts[3] = {0, 0, 0};
    std::size_t idx = 0;
    std::size_t pos = 0;
    while (idx < 3 && pos <= raw.size()) {
        const std::size_t sp = raw.find(' ', pos);
        const std::string tok = raw.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos);
        const std::size_t slash = tok.find('/');
        try {
            const int num = std::stoi(slash == std::string::npos ? tok : tok.substr(0, slash));
            const int den = slash == std::string::npos ? 1 : std::stoi(tok.substr(slash + 1));
            parts[idx] = den != 0 ? num / den : 0;
        } catch (...) {
            return false;
        }
        ++idx;
        if (sp == std::string::npos) {
            break;
        }
        pos = sp + 1;
    }
    if (idx < 3) {
        return false;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", parts[0], parts[1], parts[2]);
    out = buf;
    return true;
}

// 首个选中文件 → GpsData（无 GPS → nullopt；metadata 读失败 → error 非空）
struct GpsReadOutcome {
    std::optional<pp::GpsData> gps;
    std::string datetime;  // GPSTimeStamp + GPSDateStamp，可空
    bool metadata_ok = false;
};

GpsReadOutcome read_gps_file(const QString& path) {
    GpsReadOutcome out;
    const pp::SourceMeta meta = pp::read_metadata(std::filesystem::path(path.toStdString()));
    out.metadata_ok = meta.error.empty();
    double lat = 0, lon = 0;
    if (!dms_at(meta.exif, "Exif.GPSInfo.GPSLatitude", lat) ||
        !dms_at(meta.exif, "Exif.GPSInfo.GPSLongitude", lon)) {
        return out;
    }
    if (exif_string_at(meta.exif, "Exif.GPSInfo.GPSLatitudeRef") == "S") {
        lat = -lat;
    }
    if (exif_string_at(meta.exif, "Exif.GPSInfo.GPSLongitudeRef") == "W") {
        lon = -lon;
    }
    pp::GpsData gps;
    gps.lat = lat;
    gps.lon = lon;
    double alt = 0;
    if (rational_at(meta.exif, "Exif.GPSInfo.GPSAltitude", 0, alt)) {
        gps.altitude = exif_string_at(meta.exif, "Exif.GPSInfo.GPSAltitudeRef") == "1" ? -alt : alt;
    }
    double dir = 0;
    if (rational_at(meta.exif, "Exif.GPSInfo.GPSImgDirection", 0, dir)) {
        gps.direction = dir;
    }
    std::string time_of_day;
    const std::string date = exif_string_at(meta.exif, "Exif.GPSInfo.GPSDateStamp");
    if (!date.empty() && gps_time_string(meta.exif, time_of_day)) {
        gps.timestamp = date + " " + time_of_day;
        out.datetime = *gps.timestamp;
    }
    out.gps = gps;
    return out;
}

// ---------------------------------------------------------------------------
// 页面状态（挂在 PageMeta 下的子 QObject）
// ---------------------------------------------------------------------------

struct MetaState : QObject {
    PageMeta* page = nullptr;
    bool loading = false;  // apply_rules 期间抑制信号与重复刷新

    // 卡片 1
    QCheckBox* time_enable = nullptr;
    QComboBox* time_mode = nullptr;
    QWidget* delta_row = nullptr;
    QWidget* tz_row = nullptr;
    std::array<QSpinBox*, 6> delta{};
    QComboBox* tz_from = nullptr;
    QComboBox* tz_to = nullptr;
    QLabel* preview_before = nullptr;
    QLabel* preview_after = nullptr;

    // 卡片 2
    QCheckBox* gps_enable = nullptr;
    QLabel* gps_warn = nullptr;
    QLineEdit* lat = nullptr;
    QLineEdit* lon = nullptr;
    QLabel* dms = nullptr;
    QToolButton* more_toggle = nullptr;
    QWidget* more_fields = nullptr;
    QLineEdit* alt = nullptr;
    QLineEdit* dir = nullptr;
    QLineEdit* ts = nullptr;
    QPushButton* read_gps = nullptr;
    QLabel* gps_status = nullptr;
    QCheckBox* gps_clear = nullptr;
    QLineEdit* search_edit = nullptr;
    QPushButton* search_btn = nullptr;
    QComboBox* search_results = nullptr;
    pp::map::MapWidget* map = nullptr;

    // 卡片 3
    QVBoxLayout* tag_rows = nullptr;
    QPushButton* add_tag = nullptr;

    // 卡片 4/5/6
    QCheckBox* strip = nullptr;
    QCheckBox* mtime = nullptr;
    QLabel* exc_label = nullptr;
    QListWidget* exc_list = nullptr;

    // 输入数据
    QStringList batch_files;
    QStringList selected_files;
    std::string preview_src;   // 首个含时间文件的 "YYYY:MM:DD HH:MM:SS"
    QString preview_path;      // 该文件路径（tooltip）
    bool preview_capped = false;  // 前 kPreviewScanCap 个文件内未找到时间字段
    // M2-T7 #22：异步扫描的序号守卫与在途标志（worker 不触碰本结构，只读值拷贝）
    quint64 preview_generation = 0;   // 每次 set_batch_files +1；回填前校验
    bool preview_pending = false;     // 扫描在途（预览两行留空）
    int preview_stale_dropped = 0;    // 序号过期被丢弃的结果数（自验钩子）
};

MetaState* state_of(const PageMeta* page) {
    return static_cast<MetaState*>(page->property(kStateProperty).value<QObject*>());
}

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------

bool parse_coord(const QLineEdit* edit, double lo, double hi, double& out) {
    bool ok = false;
    const double v = edit->text().trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(v) || v < lo || v > hi) {
        return false;
    }
    out = v;
    return true;
}

std::optional<double> parse_optional(const QLineEdit* edit, double lo, double hi) {
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
void fill_timezone_combo(QComboBox* combo) {
    std::vector<int> offsets;
    for (int h = -12; h <= 14; ++h) {
        offsets.push_back(h * 60);
    }
    for (const int half : {-570, -210, 210, 270, 330, 390, 570, 630}) {
        offsets.push_back(half);
    }
    std::sort(offsets.begin(), offsets.end());
    for (const int off : offsets) {
        combo->addItem(QStringLiteral("UTC%1").arg(QString::fromStdString(pp::offset_time_string(off))),
                       off);
    }
}

void select_timezone(QComboBox* combo, int offset_min) {
    const int idx = combo->findData(offset_min);
    combo->setCurrentIndex(idx >= 0 ? idx : combo->findData(480));
}

void notify_rules_changed(MetaState* st) {
    if (!st->loading) {
        emit st->page->rules_changed();
    }
}

// ---------------------------------------------------------------------------
// 预览 / 提示刷新
// ---------------------------------------------------------------------------

// M2-T7 #22：worker 结果（值语义；跨线程传递，不含任何 GUI 指针）
struct PreviewScanResult {
    bool found = false;
    std::string src;   // "YYYY:MM:DD HH:MM:SS"
    QString path;
    bool capped = false;
};

// M2-T7 #22：worker 体（全局线程池线程执行；只读入参值拷贝，不触碰 GUI 状态）。
// 扫描上限 kPreviewScanCap 与文案口径（2026-09-20 冻结）逐字不变：
//   前 200 个文件内命中 → found；未命中且批内还有第 201 个 → capped。
PreviewScanResult scan_preview_source(const QStringList& files) {
    PreviewScanResult out;
    const int limit = std::min(static_cast<int>(files.size()), kPreviewScanCap);
    for (int i = 0; i < limit; ++i) {
        const pp::SourceMeta meta =
            pp::read_metadata(std::filesystem::path(files.at(i).toStdString()));
        const std::string dt = pp::effective_datetime(meta.exif, meta.xmp);
        if (!dt.empty()) {
            out.found = true;
            out.src = dt;
            out.path = files.at(i);
            return out;
        }
    }
    out.capped = files.size() > kPreviewScanCap;
    return out;
}

// 序号过期 → 丢弃（不得乱序回填）；序号匹配 → 回填并刷新标签。
void refresh_time_preview(MetaState* st);   // 定义见下（request/apply 都要用）
void apply_preview_result(MetaState* st, quint64 generation, const PreviewScanResult& r) {
    if (generation != st->preview_generation) {
        ++st->preview_stale_dropped;
        st->setProperty("pp_preview_stale_dropped", st->preview_stale_dropped);
        return;
    }
    st->preview_pending = false;
    st->preview_src = r.src;
    st->preview_path = r.path;
    st->preview_capped = r.capped;
    st->setProperty("pp_preview_pending", false);
    refresh_time_preview(st);
}

// M2-T7 #22：发起异步扫描（GUI 线程只做值拷贝 + 挂 watcher，立即返回）。
// 每个请求一个 watcher（各自只盯自己的 future，finished 必对应自身结果），
// 旧请求的 watcher 在结果被丢弃后 deleteLater。
void request_preview_scan(MetaState* st) {
    st->preview_src.clear();
    st->preview_path.clear();
    st->preview_capped = false;
    const quint64 generation = ++st->preview_generation;
    st->preview_pending = !st->batch_files.isEmpty();
    st->setProperty("pp_preview_generation", qulonglong(generation));
    st->setProperty("pp_preview_pending", st->preview_pending);
    if (!st->preview_pending) {
        return;   // 空批：无扫描，预览回到 "无文件"
    }
    const QStringList files = st->batch_files;   // 值拷贝：worker 不读 st
    auto* watcher = new QFutureWatcher<PreviewScanResult>(st);
    QObject::connect(watcher, &QFutureWatcher<PreviewScanResult>::finished, st,
                     [st, watcher, generation] {
                         const PreviewScanResult r = watcher->future().result();
                         watcher->deleteLater();
                         apply_preview_result(st, generation, r);
                     });
    watcher->setFuture(QtConcurrent::run([files] { return scan_preview_source(files); }));
}

pp::TimeShift current_time_shift(const MetaState* st) {
    pp::TimeShift shift;
    if (st->time_mode->currentIndex() == 0) {
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

void refresh_time_preview(MetaState* st) {
    if (st->batch_files.isEmpty()) {
        st->preview_before->setText(PageMeta::tr("无文件"));
        st->preview_after->clear();
        st->preview_before->setToolTip(QString());
        return;
    }
    if (st->preview_pending) {
        // M2-T7 #22：扫描在途 → 两行留空。既不显示上一批的陈旧结果（会被误读成新批次
        // 的预览），也不预报未定论文案；worker 结果一到（序号校验通过）立即回填。
        st->preview_before->clear();
        st->preview_after->clear();
        st->preview_before->setToolTip(QString());
        return;
    }
    if (st->preview_src.empty()) {
        st->preview_before->setText(st->preview_capped ? PageMeta::tr(kPreviewCapText)
                                                       : PageMeta::tr("首文件无时间字段"));
        st->preview_after->clear();
        st->preview_before->setToolTip(QString());
        return;
    }
    const QString src = QString::fromStdString(st->preview_src);
    st->preview_before->setText(PageMeta::tr("首文件：%1").arg(src));
    st->preview_before->setToolTip(st->preview_path);
    if (!st->time_enable->isChecked()) {
        st->preview_after->setText(PageMeta::tr("→ %1（未启用）").arg(src));
        return;
    }
    const pp::TimeShift shift = current_time_shift(st);
    bool ok = false;
    const std::string dst = shift.mode == pp::TimeShift::Mode::Delta
                                ? pp::shift_exif_datetime(st->preview_src, shift, ok)
                                : pp::reinterpret_timezone(st->preview_src, shift.from_offset_min,
                                                           shift.to_offset_min, ok);
    if (!ok) {
        st->preview_after->setText(PageMeta::tr("→ （时间无法解析）"));
        return;
    }
    st->preview_after->setText(PageMeta::tr("→ %1").arg(QString::fromStdString(dst)));
}

void refresh_dms(MetaState* st) {
    double lat = 0, lon = 0;
    if (parse_coord(st->lat, -90.0, 90.0, lat) && parse_coord(st->lon, -180.0, 180.0, lon)) {
        st->dms->setText(dms_string(lat, lon));
    } else {
        st->dms->clear();
    }
}

// GPS 勾选但坐标非法 → 卡片顶部黄字提示（§2.11 冻结）
void refresh_gps_warning(MetaState* st) {
    double lat = 0, lon = 0;
    const bool valid = parse_coord(st->lat, -90.0, 90.0, lat) &&
                       parse_coord(st->lon, -180.0, 180.0, lon);
    const bool show = st->gps_enable->isChecked() && !st->gps_clear->isChecked() && !valid;
    st->gps_warn->setVisible(show);
}

// §9.1 U7 修正：卡片 "启用" 未勾选 → 卡内其余控件禁用（rules() 语义不变）。
void refresh_time_inputs(MetaState* st) {
    const bool on = st->time_enable->isChecked();
    st->time_mode->setEnabled(on);
    for (QSpinBox* spin : st->delta) {
        spin->setEnabled(on);
    }
    st->tz_from->setEnabled(on);
    st->tz_to->setEnabled(on);
}

// 坐标/搜索控件可用性 = GPS "启用" ∧ ¬"清除 GPS"；"清除 GPS" 复选框自身始终可用
// （语义独立）；"读取坐标" 另需有选中文件。地图控件不受限（点图回填会自行勾选启用）。
void refresh_gps_inputs(MetaState* st) {
    const bool card_on = st->gps_enable->isChecked();
    const bool locked = st->gps_clear->isChecked();
    const bool coords = card_on && !locked;
    st->lat->setEnabled(coords);
    st->lon->setEnabled(coords);
    st->more_toggle->setEnabled(card_on);
    st->more_fields->setEnabled(coords);
    st->alt->setEnabled(coords);
    st->dir->setEnabled(coords);
    st->ts->setEnabled(coords);
    st->search_edit->setEnabled(card_on);
    st->search_btn->setEnabled(card_on);
    st->search_results->setEnabled(card_on);
    st->read_gps->setEnabled(coords && !st->selected_files.isEmpty());
    // §9.1 FIX2：占位文本跟随各框自身可用性（lat/lon 另受 "清除 GPS" 影响）
    st->lat->setPlaceholderText(coords ? PageMeta::tr(kLatPlaceholder) : QString());
    st->lon->setPlaceholderText(coords ? PageMeta::tr(kLonPlaceholder) : QString());
    st->search_edit->setPlaceholderText(card_on ? PageMeta::tr(kSearchPlaceholder) : QString());
    if (locked) {
        st->gps_status->setText(PageMeta::tr("已勾选清除 GPS：输出将不含 GPS"));
        st->map->clear_marker();
    }
}

void refresh_all(MetaState* st) {
    refresh_time_preview(st);
    refresh_dms(st);
    refresh_gps_warning(st);
    refresh_time_inputs(st);
    refresh_gps_inputs(st);
}

// ---------------------------------------------------------------------------
// rules() 装配
// ---------------------------------------------------------------------------

pp::BatchRules build_rules(const MetaState* st) {
    pp::BatchRules rules;
    if (st->time_enable->isChecked()) {
        rules.time_shift = current_time_shift(st);
    }
    // 清除 GPS 与 gps 互斥（gps_clear 优先）
    if (st->gps_clear->isChecked()) {
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
        QWidget* host = st->tag_rows->itemAt(i)->widget();
        if (host == nullptr) {
            continue;
        }
        const auto* key_edit = host->findChild<QLineEdit*>(QStringLiteral("tag_key"));
        const auto* value_edit = host->findChild<QLineEdit*>(QStringLiteral("tag_value"));
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
            edit.remove = true;  // 值留空 = 删除该标签
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
// apply_rules（预设载入；不发信号）
// ---------------------------------------------------------------------------

void clear_tag_rows(MetaState* st) {
    while (st->tag_rows->count() > 0) {
        QLayoutItem* item = st->tag_rows->takeAt(0);
        if (QWidget* w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
}

void add_tag_row(MetaState* st, const QString& key, const QString& value) {
    auto* row = new QWidget();
    row->setObjectName(QStringLiteral("tag_row"));
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);

    auto* key_edit = new QLineEdit(row);
    key_edit->setObjectName(QStringLiteral("tag_key"));
    key_edit->setPlaceholderText(PageMeta::tr("Exif.Image.Artist 或 Xmp.dc.title"));
    key_edit->setText(key);
    auto* value_edit = new QLineEdit(row);
    value_edit->setObjectName(QStringLiteral("tag_value"));
    value_edit->setText(value);
    value_edit->setToolTip(PageMeta::tr("值留空 = 删除该标签"));
    auto* remove = new QPushButton(QStringLiteral("✕"), row);
    remove->setObjectName(QStringLiteral("tag_remove"));
    remove->setFixedWidth(28);
    remove->setToolTip(PageMeta::tr("删除此行"));

    lay->addWidget(key_edit, 2);
    lay->addWidget(value_edit, 3);
    lay->addWidget(remove, 0);
    st->tag_rows->addWidget(row);

    QObject::connect(key_edit, &QLineEdit::textChanged, st, [st] { notify_rules_changed(st); });
    QObject::connect(value_edit, &QLineEdit::textChanged, st, [st] { notify_rules_changed(st); });
    QObject::connect(remove, &QPushButton::clicked, st, [st, row] {
        st->tag_rows->removeWidget(row);
        row->hide();
        row->deleteLater();
        notify_rules_changed(st);
    });
}

void apply_state(MetaState* st, const pp::BatchRules& r) {
    const bool prev_loading = st->loading;
    st->loading = true;

    st->time_enable->setChecked(r.time_shift.has_value());
    if (r.time_shift.has_value()) {
        const pp::TimeShift& s = *r.time_shift;
        st->time_mode->setCurrentIndex(s.mode == pp::TimeShift::Mode::Delta ? 0 : 1);
        st->delta[0]->setValue(s.years);
        st->delta[1]->setValue(s.months);
        st->delta[2]->setValue(s.days);
        st->delta[3]->setValue(s.hours);
        st->delta[4]->setValue(s.minutes);
        st->delta[5]->setValue(s.seconds);
        select_timezone(st->tz_from, s.from_offset_min);
        select_timezone(st->tz_to, s.to_offset_min);
    }
    st->delta_row->setVisible(st->time_mode->currentIndex() == 0);
    st->tz_row->setVisible(st->time_mode->currentIndex() == 1);

    st->gps_clear->setChecked(r.gps_clear);
    st->gps_enable->setChecked(r.gps.has_value());
    if (r.gps.has_value()) {
        st->lat->setText(format_6(r.gps->lat));
        st->lon->setText(format_6(r.gps->lon));
        st->alt->setText(r.gps->altitude.has_value() ? format_6(*r.gps->altitude) : QString());
        st->dir->setText(r.gps->direction.has_value() ? format_6(*r.gps->direction) : QString());
        st->ts->setText(r.gps->timestamp.has_value()
                            ? QString::fromStdString(*r.gps->timestamp)
                            : QString());
        st->map->set_marker(r.gps->lat, r.gps->lon);
        st->gps_status->setText(PageMeta::tr("已载入批量 GPS 规则"));
    } else if (r.gps_clear) {
        st->map->clear_marker();
    }

    clear_tag_rows(st);
    for (const pp::TagEdit& edit : r.exif_edits) {
        add_tag_row(st, QString::fromStdString(edit.key),
                    edit.value.has_value() ? QString::fromStdString(*edit.value) : QString());
    }
    for (const pp::TagEdit& edit : r.xmp_edits) {
        add_tag_row(st, QString::fromStdString(edit.key),
                    edit.value.has_value() ? QString::fromStdString(*edit.value) : QString());
    }

    st->strip->setChecked(r.strip_privacy);
    st->mtime->setChecked(r.sync_mtime);

    st->loading = prev_loading;
    refresh_all(st);
}

// ---------------------------------------------------------------------------
// 界面构建
// ---------------------------------------------------------------------------

QLabel* grey_hint(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, QColor(0x80, 0x80, 0x80));
    label->setPalette(pal);
    return label;
}

QLabel* yellow_hint(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, QColor(0xcc, 0x88, 0x00));
    label->setPalette(pal);
    return label;
}

QWidget* build_time_card(MetaState* st, QWidget* parent) {
    auto* card = new QGroupBox(PageMeta::tr("时间偏移"), parent);
    auto* v = new QVBoxLayout(card);

    st->time_enable = new QCheckBox(PageMeta::tr("启用"), card);
    st->time_enable->setObjectName(QStringLiteral("time_enable"));
    st->time_enable->setChecked(false);
    v->addWidget(st->time_enable);

    auto* mode_row = new QHBoxLayout();
    mode_row->addWidget(new QLabel(PageMeta::tr("方式"), card));
    st->time_mode = new QComboBox(card);
    st->time_mode->setObjectName(QStringLiteral("time_mode"));
    st->time_mode->addItem(PageMeta::tr("Δ 偏移（时钟拨错）"));
    st->time_mode->addItem(PageMeta::tr("时区语义（旅行照片）"));
    mode_row->addWidget(st->time_mode, 1);
    v->addLayout(mode_row);

    // Δ 模式：年 月 日 时 分 秒
    st->delta_row = new QWidget(card);
    auto* dh = new QHBoxLayout(st->delta_row);
    dh->setContentsMargins(0, 0, 0, 0);
    const std::array<QString, 6> names = {PageMeta::tr("年"), PageMeta::tr("月"), PageMeta::tr("日"),
                                         PageMeta::tr("时"), PageMeta::tr("分"), PageMeta::tr("秒")};
    const std::array<QString, 6> kDeltaNames = {
        QStringLiteral("time_years"), QStringLiteral("time_months"), QStringLiteral("time_days"),
        QStringLiteral("time_hours"), QStringLiteral("time_minutes"), QStringLiteral("time_seconds")};
    for (int i = 0; i < 6; ++i) {
        auto* box = new QVBoxLayout();
        box->addWidget(new QLabel(names[std::size_t(i)], st->delta_row));
        st->delta[i] = new QSpinBox(st->delta_row);
        // §2.11 U7 落地口径冻结的 objectName 契约：time_years…time_seconds
        st->delta[i]->setObjectName(kDeltaNames[std::size_t(i)]);
        st->delta[i]->setRange(-9999, 9999);
        st->delta[i]->setValue(0);
        box->addWidget(st->delta[i]);
        dh->addLayout(box);
    }
    v->addWidget(st->delta_row);

    // 时区模式：从 / 到
    st->tz_row = new QWidget(card);
    auto* th = new QHBoxLayout(st->tz_row);
    th->setContentsMargins(0, 0, 0, 0);
    th->addWidget(new QLabel(PageMeta::tr("从"), st->tz_row));
    st->tz_from = new QComboBox(st->tz_row);
    st->tz_from->setObjectName(QStringLiteral("time_tz_from"));
    fill_timezone_combo(st->tz_from);
    select_timezone(st->tz_from, 480);
    th->addWidget(st->tz_from, 1);
    th->addWidget(new QLabel(PageMeta::tr("到"), st->tz_row));
    st->tz_to = new QComboBox(st->tz_row);
    st->tz_to->setObjectName(QStringLiteral("time_tz_to"));
    fill_timezone_combo(st->tz_to);
    select_timezone(st->tz_to, 480);
    th->addWidget(st->tz_to, 1);
    v->addWidget(st->tz_row);

    // 前后对照预览（两行）
    st->preview_before = new QLabel(card);
    st->preview_before->setObjectName(QStringLiteral("time_preview_before"));
    st->preview_after = new QLabel(card);
    st->preview_after->setObjectName(QStringLiteral("time_preview_after"));
    v->addWidget(st->preview_before);
    v->addWidget(st->preview_after);

    st->time_mode->setCurrentIndex(0);
    st->delta_row->setVisible(true);
    st->tz_row->setVisible(false);

    QObject::connect(st->time_enable, &QCheckBox::toggled, st, [st](bool) {
        if (!st->loading) {
            refresh_time_inputs(st);   // §9.1：未启用 → 卡内控件禁用
            refresh_time_preview(st);
            notify_rules_changed(st);
        }
    });
    QObject::connect(st->time_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), st,
                     [st](int idx) {
                         if (!st->loading) {
                             st->delta_row->setVisible(idx == 0);
                             st->tz_row->setVisible(idx == 1);
                             refresh_time_preview(st);
                             notify_rules_changed(st);
                         }
                     });
    for (QSpinBox* spin : st->delta) {
        QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), st, [st](int) {
            if (!st->loading) {
                refresh_time_preview(st);
                notify_rules_changed(st);
            }
        });
    }
    QObject::connect(st->tz_from, QOverload<int>::of(&QComboBox::currentIndexChanged), st,
                     [st](int) {
                         if (!st->loading) {
                             refresh_time_preview(st);
                             notify_rules_changed(st);
                         }
                     });
    QObject::connect(st->tz_to, QOverload<int>::of(&QComboBox::currentIndexChanged), st, [st](int) {
        if (!st->loading) {
            refresh_time_preview(st);
            notify_rules_changed(st);
        }
    });
    return card;
}

QWidget* build_gps_card(MetaState* st, QWidget* parent) {
    auto* card = new QGroupBox(PageMeta::tr("GPS"), parent);
    auto* v = new QVBoxLayout(card);

    st->gps_warn = yellow_hint(PageMeta::tr("GPS 坐标无效"), card);
    st->gps_warn->setObjectName(QStringLiteral("gps_warning"));
    st->gps_warn->setVisible(false);
    v->addWidget(st->gps_warn);

    st->gps_enable = new QCheckBox(PageMeta::tr("启用"), card);
    st->gps_enable->setObjectName(QStringLiteral("gps_enable"));
    st->gps_enable->setChecked(false);
    v->addWidget(st->gps_enable);

    auto* lat_row = new QHBoxLayout();
    lat_row->addWidget(new QLabel(PageMeta::tr("纬度"), card));
    st->lat = new QLineEdit(card);
    st->lat->setObjectName(QStringLiteral("gps_lat"));
    st->lat->setValidator(new QDoubleValidator(-90.0, 90.0, 6, st->lat));
    st->lat->setPlaceholderText(PageMeta::tr(kLatPlaceholder));
    lat_row->addWidget(st->lat, 1);
    v->addLayout(lat_row);

    auto* lon_row = new QHBoxLayout();
    lon_row->addWidget(new QLabel(PageMeta::tr("经度"), card));
    st->lon = new QLineEdit(card);
    st->lon->setObjectName(QStringLiteral("gps_lon"));
    st->lon->setValidator(new QDoubleValidator(-180.0, 180.0, 6, st->lon));
    st->lon->setPlaceholderText(PageMeta::tr(kLonPlaceholder));
    lon_row->addWidget(st->lon, 1);
    v->addLayout(lon_row);

    st->dms = new QLabel(card);
    st->dms->setObjectName(QStringLiteral("gps_dms"));
    v->addWidget(st->dms);

    // 更多字段（可折叠）
    st->more_toggle = new QToolButton(card);
    st->more_toggle->setObjectName(QStringLiteral("gps_more_toggle"));
    st->more_toggle->setText(PageMeta::tr("更多字段"));
    st->more_toggle->setCheckable(true);
    st->more_toggle->setChecked(false);
    st->more_toggle->setArrowType(Qt::RightArrow);
    st->more_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    st->more_toggle->setAutoRaise(true);
    v->addWidget(st->more_toggle, 0, Qt::AlignLeft);

    st->more_fields = new QWidget(card);
    st->more_fields->setObjectName(QStringLiteral("gps_more_fields"));
    auto* form = new QFormLayout(st->more_fields);
    form->setContentsMargins(0, 0, 0, 0);
    st->alt = new QLineEdit(st->more_fields);
    st->alt->setObjectName(QStringLiteral("gps_alt"));
    st->alt->setPlaceholderText(PageMeta::tr("可空"));
    st->dir = new QLineEdit(st->more_fields);
    st->dir->setObjectName(QStringLiteral("gps_dir"));
    st->dir->setPlaceholderText(PageMeta::tr("可空"));
    st->ts = new QLineEdit(st->more_fields);
    st->ts->setObjectName(QStringLiteral("gps_ts"));
    st->ts->setPlaceholderText(QStringLiteral("YYYY:MM:DD HH:MM:SS"));
    form->addRow(PageMeta::tr("海拔 (m)"), st->alt);
    form->addRow(PageMeta::tr("方位角 (°)"), st->dir);
    form->addRow(PageMeta::tr("时间戳"), st->ts);
    st->more_fields->setVisible(false);
    v->addWidget(st->more_fields);

    auto* read_row = new QHBoxLayout();
    st->read_gps = new QPushButton(PageMeta::tr("从选中文件读取坐标"), card);
    st->read_gps->setObjectName(QStringLiteral("gps_read_button"));
    st->read_gps->setEnabled(false);
    read_row->addWidget(st->read_gps);
    st->gps_clear = new QCheckBox(PageMeta::tr("清除 GPS"), card);
    st->gps_clear->setObjectName(QStringLiteral("gps_clear"));
    read_row->addWidget(st->gps_clear);
    read_row->addStretch(1);
    v->addLayout(read_row);

    st->gps_status = new QLabel(card);
    st->gps_status->setObjectName(QStringLiteral("gps_status"));
    v->addWidget(st->gps_status);

    // 搜索行（地图正上方）
    auto* search_row = new QHBoxLayout();
    st->search_edit = new QLineEdit(card);
    st->search_edit->setObjectName(QStringLiteral("gps_search_edit"));
    st->search_edit->setPlaceholderText(PageMeta::tr(kSearchPlaceholder));
    st->search_btn = new QPushButton(PageMeta::tr("搜索"), card);
    st->search_btn->setObjectName(QStringLiteral("gps_search_button"));
    st->search_results = new QComboBox(card);
    st->search_results->setObjectName(QStringLiteral("gps_search_results"));
    search_row->addWidget(st->search_edit, 2);
    search_row->addWidget(st->search_btn, 0);
    search_row->addWidget(st->search_results, 3);
    v->addLayout(search_row);

    st->map = new pp::map::MapWidget(card);
    st->map->setObjectName(QStringLiteral("gps_map"));
    st->map->setFixedHeight(260);  // §2.11：固定高 260，宽随卡片
    v->addWidget(st->map);

    // ---- 连接 ----
    for (QLineEdit* edit : {st->lat, st->lon}) {
        QObject::connect(edit, &QLineEdit::textChanged, st, [st](const QString&) {
            if (!st->loading) {
                refresh_dms(st);
                refresh_gps_warning(st);
                notify_rules_changed(st);
            }
        });
    }
    for (QLineEdit* edit : {st->alt, st->dir, st->ts}) {
        QObject::connect(edit, &QLineEdit::textChanged, st, [st](const QString&) {
            if (!st->loading) {
                notify_rules_changed(st);
            }
        });
    }
    QObject::connect(st->gps_enable, &QCheckBox::toggled, st, [st](bool) {
        if (!st->loading) {
            refresh_gps_inputs(st);    // §9.1：未启用 → 卡内控件禁用（清除 GPS 除外）
            refresh_gps_warning(st);
            notify_rules_changed(st);
        }
    });
    QObject::connect(st->gps_clear, &QCheckBox::toggled, st, [st](bool) {
        if (!st->loading) {
            refresh_gps_inputs(st);
            refresh_gps_warning(st);
            notify_rules_changed(st);
        }
    });
    QObject::connect(st->more_toggle, &QToolButton::toggled, st, [st](bool on) {
        st->more_fields->setVisible(on);
        st->more_toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });

    // 从选中文件读取坐标（Exiv2 直调；§1.7 例外）
    QObject::connect(st->read_gps, &QPushButton::clicked, st, [st] {
        if (st->selected_files.isEmpty()) {
            return;
        }
        const GpsReadOutcome outcome = read_gps_file(st->selected_files.first());
        if (!outcome.gps.has_value()) {
            st->gps_status->setText(outcome.metadata_ok
                                        ? PageMeta::tr("选中的文件没有 GPS 信息")
                                        : PageMeta::tr("无法读取选中文件的元数据"));
            return;
        }
        const pp::GpsData& gps = *outcome.gps;
        st->lat->setText(format_6(gps.lat));
        st->lon->setText(format_6(gps.lon));
        st->alt->setText(gps.altitude.has_value() ? format_6(*gps.altitude) : QString());
        st->dir->setText(gps.direction.has_value() ? format_6(*gps.direction) : QString());
        st->ts->setText(gps.timestamp.has_value() ? QString::fromStdString(*gps.timestamp)
                                                 : QString());
        st->map->set_marker(gps.lat, gps.lon);
        st->map->center_on(gps.lat, gps.lon);
        st->gps_enable->setChecked(true);
        st->gps_status->setText(PageMeta::tr("已从选中文件读取坐标"));
        refresh_dms(st);
        refresh_gps_warning(st);
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
                     [st](const QStringList& titles, const QString& error) {
                         st->search_results->clear();
                         if (!error.isEmpty()) {
                             st->gps_status->setText(error);
                             return;
                         }
                         st->search_results->addItems(titles);
                         st->gps_status->setText(titles.isEmpty()
                                                     ? PageMeta::tr("没有匹配的结果")
                                                     : PageMeta::tr("搜索到 %1 个结果")
                                                           .arg(titles.size()));
                     });
    QObject::connect(st->search_results, QOverload<int>::of(&QComboBox::activated), st,
                     [st](int index) { st->map->search_select(index); });
    QObject::connect(st->map, &pp::map::MapWidget::point_selected, st, [st](double lat, double lon) {
        st->lat->setText(format_6(lat));
        st->lon->setText(format_6(lon));
        st->map->set_marker(lat, lon);
        st->gps_enable->setChecked(true);
        st->gps_status->setText(PageMeta::tr("已从地图选择坐标"));
        refresh_dms(st);
        refresh_gps_warning(st);
        notify_rules_changed(st);
    });
    return card;
}

QWidget* build_tag_card(MetaState* st, QWidget* parent) {
    auto* card = new QGroupBox(PageMeta::tr("标签修改"), parent);
    auto* v = new QVBoxLayout(card);

    auto* host = new QWidget(card);
    host->setObjectName(QStringLiteral("tag_rows_host"));
    st->tag_rows = new QVBoxLayout(host);
    st->tag_rows->setContentsMargins(0, 0, 0, 0);
    v->addWidget(host);

    st->add_tag = new QPushButton(PageMeta::tr("添加标签"), card);
    st->add_tag->setObjectName(QStringLiteral("tag_add_button"));
    v->addWidget(st->add_tag, 0, Qt::AlignLeft);
    QObject::connect(st->add_tag, &QPushButton::clicked, st, [st] {
        add_tag_row(st, QString(), QString());
        notify_rules_changed(st);
    });
    return card;
}

QWidget* build_privacy_card(MetaState* st, QWidget* parent) {
    auto* card = new QGroupBox(PageMeta::tr("隐私剥除"), parent);
    auto* v = new QVBoxLayout(card);
    st->strip = new QCheckBox(PageMeta::tr("剥除全部 EXIF / XMP（保留 ICC 与像素）"), card);
    st->strip->setObjectName(QStringLiteral("privacy_strip"));
    v->addWidget(st->strip);
    v->addWidget(grey_hint(PageMeta::tr("优先级最高的规则"), card));
    QObject::connect(st->strip, &QCheckBox::toggled, st, [st](bool) { notify_rules_changed(st); });
    return card;
}

QWidget* build_mtime_card(MetaState* st, QWidget* parent) {
    auto* card = new QGroupBox(PageMeta::tr("文件时间"), parent);
    auto* v = new QVBoxLayout(card);
    st->mtime = new QCheckBox(PageMeta::tr("输出文件修改时间同步拍摄时间 (mtime)"), card);
    st->mtime->setObjectName(QStringLiteral("mtime_sync"));
    v->addWidget(st->mtime);
    QObject::connect(st->mtime, &QCheckBox::toggled, st, [st](bool) { notify_rules_changed(st); });
    return card;
}

QWidget* build_exception_card(MetaState* st, QWidget* parent) {
    auto* card = new QGroupBox(PageMeta::tr("例外"), parent);
    auto* v = new QVBoxLayout(card);
    st->exc_label = new QLabel(PageMeta::tr("无例外文件"), card);
    st->exc_label->setObjectName(QStringLiteral("exception_label"));
    v->addWidget(st->exc_label);
    st->exc_list = new QListWidget(card);
    st->exc_list->setObjectName(QStringLiteral("exception_list"));
    st->exc_list->setContextMenuPolicy(Qt::CustomContextMenu);
    v->addWidget(st->exc_list);

    QObject::connect(st->exc_list, &QListWidget::itemDoubleClicked, st, [st](QListWidgetItem* item) {
        if (item != nullptr) {
            emit st->page->open_editor_requested(item->text());
        }
    });
    QObject::connect(st->exc_list, &QListWidget::customContextMenuRequested, st,
                     [st](const QPoint& pos) {
                         QListWidgetItem* item = st->exc_list->itemAt(pos);
                         if (item == nullptr) {
                             return;
                         }
                         QMenu menu(st->exc_list);
                         QAction* clear = menu.addAction(PageMeta::tr("清除例外"));
                         if (menu.exec(st->exc_list->viewport()->mapToGlobal(pos)) == clear) {
                             emit st->page->clear_exception_requested(item->text());
                         }
                     });
    return card;
}

void build_ui(PageMeta* page) {
    auto* st = new MetaState();
    st->page = page;
    st->setObjectName(QString::fromLatin1(kStateObjectName));
    st->setParent(page);
    page->setProperty(kStateProperty, QVariant::fromValue(static_cast<QObject*>(st)));

    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    outer->addWidget(scroll);

    auto* content = new QWidget(scroll);
    auto* v = new QVBoxLayout(content);
    v->addWidget(build_time_card(st, content));
    v->addWidget(build_gps_card(st, content));
    v->addWidget(build_tag_card(st, content));
    v->addWidget(build_privacy_card(st, content));
    v->addWidget(build_mtime_card(st, content));
    v->addWidget(build_exception_card(st, content));
    v->addStretch(1);
    scroll->setWidget(content);

    refresh_all(st);
}

}  // namespace

// ---------------------------------------------------------------------------
// PageMeta（冻结接口；状态在 MetaState 子对象中）
// ---------------------------------------------------------------------------

PageMeta::PageMeta(QWidget* parent) : QWidget(parent) { build_ui(this); }

pp::BatchRules PageMeta::rules() const { return build_rules(state_of(this)); }

void PageMeta::apply_rules(const pp::BatchRules& r) { apply_state(state_of(this), r); }

void PageMeta::set_batch_files(const QStringList& paths) {
    MetaState* st = state_of(this);
    st->batch_files = paths;
    // M2-T7 #22：扫描异步化（QtConcurrent 线程池 + QFutureWatcher 回填，序号守卫）；
    // 新批次立即作废旧请求的结果，预览在扫描在途时留空。
    request_preview_scan(st);
    refresh_time_preview(st);
}

void PageMeta::set_selected_files(const QStringList& paths) {
    MetaState* st = state_of(this);
    st->selected_files = paths;
    refresh_gps_inputs(st);
}

void PageMeta::set_exception_summary(const QStringList& paths) {
    MetaState* st = state_of(this);
    st->exc_list->clear();
    for (const QString& path : paths) {
        st->exc_list->addItem(path);
    }
    st->exc_label->setText(paths.isEmpty()
                               ? tr("无例外文件")
                               : tr("%1 个文件带元数据例外").arg(paths.size()));
}

void PageMeta::set_map_provider(const QString& provider_id, const QString& amap_key, int cache_mb) {
    MetaState* st = state_of(this);
    st->map->set_provider(provider_id);
    st->map->set_amap_key(amap_key);
    st->map->set_cache_mb(cache_mb);
}

}  // namespace pp::ui
