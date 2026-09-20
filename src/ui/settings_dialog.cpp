// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — settings dialog (M1b-U6)
//
// 规格：docs/m1b-tasks.md §2.10（三页规格/文案，冻结）+ §2.2 U1 落地口径
//       （INI 键名 = AppSettings 字段名 snake_case；本对话框编辑的就是这些字段）。
//
// 落地说明（冻结头无任何私有成员，故状态不在成员里）：
//   ① 可编辑字段的状态就在子控件里，settings() 用 objectName 查找读回；
//   ② 构造时的原始 AppSettings 快照存成 dynamic property（pp_orig_*），用于
//      非确定路径（取消/关闭）回填，以及回填 UI 中不存在的字段
//      （last_format / last_preset / last_out_root —— §2.10 三页无这些控件，
//        但 settings() 必须返回完整结构，否则 MainWindow 保存会丢“上次会话”）。
//   ③ settings() 仅在确定路径（accept → result()==Accepted）返回编辑值；
//      取消/未 exec 时返回构造快照。
#include "ui/settings_dialog.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

#include "core/logger.h"
#include "core/version.h"

namespace pp::ui {
namespace {

// objectName（settings() 读回用；同时供 U10 --ui-smoke / 走查定位）
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

constexpr int kWorkersMax = 64;         // §2.10：QSpinBox(0..64)
constexpr int kBudgetMax = 64;          // §2.10：QSpinBox(0..64)
constexpr int kTileCacheMin = 16;       // §2.10：QSpinBox(16..512)
constexpr int kTileCacheMax = 512;
constexpr int kSliderMax = 100;         // §2.10：QSlider(0..100) → flatten_gray = v/100
// §9.1 U6-FIX：关于页版本清单可见行数钳制（对话框尺寸贴合内容）
constexpr int kVersionRowsMin = 3;
constexpr int kVersionRowsMax = 6;
constexpr int kVersionRowsPad = 8;

// 日志级别 5 档（§2.10 冻结；LogLevel 的第 6 档 critical 不出现，值非法 → 回退 info，
// 与 main.cpp 的 parse_log_level 一致）。itemData = 落盘值，itemText = 展示文案。
constexpr const char* kLevelIds[] = {"trace", "debug", "info", "warn", "error"};

// 色块背景灰度 = 滑杆值（§2.10）
void apply_swatch(QLabel* swatch, int gray) {
    swatch->setAutoFillBackground(true);
    QPalette pal = swatch->palette();
    pal.setColor(QPalette::Window, QColor(gray, gray, gray));
    swatch->setPalette(pal);
}

int gray_to_slider(double g) {
    const double clamped = std::clamp(g, 0.0, 1.0);
    return static_cast<int>(std::lround(clamped * kSliderMax));
}

// M2-T7 #28c：首次 show 后把对话框高度贴合当前页（构造期页几何未落定，
// 增量法在 show 之后才可靠）。冻结头无成员/槽位 → 用局部事件过滤器承载。
class FitOnShow : public QObject {
public:
    FitOnShow(QObject* parent, std::function<void()> fit)
        : QObject(parent), fit_(std::move(fit)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Show && fit_) {
            fit_();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> fit_;
};

pp::AppSettings original_values(const QObject* o) {
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
    return s;
}

}  // namespace

SettingsDialog::SettingsDialog(const pp::AppSettings& current, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("设置"));

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

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName("settings_tabs");

    // ---- 页 1：运行 ----
    auto* page_run = new QWidget(tabs);
    auto* form_run = new QFormLayout(page_run);

    auto* workers = new QSpinBox(page_run);
    workers->setObjectName(kWorkers);
    workers->setRange(0, kWorkersMax);
    workers->setSpecialValueText(tr("自动（物理核数）"));   // 0 = 自动
    workers->setValue(std::clamp(current.workers, 0, kWorkersMax));
    form_run->addRow(tr("并发 worker 数"), workers);

    auto* budget = new QSpinBox(page_run);
    budget->setObjectName(kBudgetGb);
    budget->setRange(0, kBudgetMax);
    budget->setSpecialValueText(tr("自动"));                // 0 = 自动
    budget->setValue(std::clamp(current.budget_gb, 0, kBudgetMax));
    form_run->addRow(tr("内存预算 (GB)"), budget);

    auto* flatten = new QSlider(Qt::Horizontal, page_run);
    flatten->setObjectName(kFlattenGray);
    flatten->setRange(0, kSliderMax);
    const int gray0 = gray_to_slider(current.flatten_gray);
    flatten->setValue(gray0);

    auto* swatch = new QLabel(page_run);
    swatch->setObjectName(kFlattenPreview);
    swatch->setFixedSize(28, 18);
    swatch->setFrameShape(QFrame::Box);
    swatch->setToolTip(tr("alpha 合成底色预览"));
    apply_swatch(swatch, gray0);

    auto* flatten_row = new QHBoxLayout;
    flatten_row->addWidget(flatten, 1);
    flatten_row->addWidget(swatch);
    flatten_row->addWidget(new QLabel(tr("黑 ←→ 白"), page_run));
    form_run->addRow(tr("alpha 合成底色"), flatten_row);
    connect(flatten, &QSlider::valueChanged, swatch,
            [swatch](int v) { apply_swatch(swatch, v); });

    auto* rotate = new QCheckBox(tr("按 EXIF 方向旋转"), page_run);
    rotate->setObjectName(kRotateOrientation);
    rotate->setChecked(current.rotate_orientation);          // 默认开（settings 默认 true）
    form_run->addRow(rotate);

    tabs->addTab(page_run, tr("运行"));

    // ---- 页 2：地图 ----
    auto* page_map = new QWidget(tabs);
    auto* form_map = new QFormLayout(page_map);

    auto* provider = new QComboBox(page_map);
    provider->setObjectName(kMapProvider);
    provider->addItem(tr("OpenStreetMap（内置）"), QStringLiteral("osm"));
    provider->addItem(tr("高德地图"), QStringLiteral("amap"));
    {
        const int idx = provider->findData(QString::fromStdString(current.map_provider));
        provider->setCurrentIndex(idx >= 0 ? idx : 0);       // 未知值 → OSM（默认）
    }
    form_map->addRow(tr("底图提供方"), provider);

    auto* amap_key = new QLineEdit(page_map);
    amap_key->setObjectName(kAmapKey);
    amap_key->setEchoMode(QLineEdit::Password);              // 不回显明文
    amap_key->setText(QString::fromStdString(current.amap_key));
    form_map->addRow(tr("高德 Web 服务 Key"), amap_key);

    auto* tile_cache = new QSpinBox(page_map);
    tile_cache->setObjectName(kTileCacheMb);
    tile_cache->setRange(kTileCacheMin, kTileCacheMax);
    tile_cache->setValue(std::clamp(current.tile_cache_mb, kTileCacheMin, kTileCacheMax));
    form_map->addRow(tr("瓦片缓存 (MB)"), tile_cache);

    auto* level = new QComboBox(page_map);
    level->setObjectName(kLogLevel);
    level->addItem(tr("trace"), QLatin1String(kLevelIds[0]));
    level->addItem(tr("debug"), QLatin1String(kLevelIds[1]));
    level->addItem(tr("info"), QLatin1String(kLevelIds[2]));
    level->addItem(tr("warn"), QLatin1String(kLevelIds[3]));
    level->addItem(tr("error"), QLatin1String(kLevelIds[4]));
    {
        const int idx = level->findData(QString::fromStdString(current.log_level));
        level->setCurrentIndex(idx >= 0 ? idx : level->findData(QStringLiteral("info")));
    }
    form_map->addRow(tr("日志级别"), level);

    tabs->addTab(page_map, tr("地图"));

    // ---- 页 3：关于 ----
    auto* page_about = new QWidget(tabs);
    auto* vbox_about = new QVBoxLayout(page_about);

    // M2-T11 §2.2：版本号唯一来源 = 生成的 core/version.h（PP_VERSION_STRING），不再硬编码。
    auto* version = new QLabel(tr("PhotoPipeline %1").arg(QLatin1String(PP_VERSION_STRING)),
                               page_about);
    QFont version_font = version->font();
    version_font.setBold(true);
    version->setFont(version_font);
    version->setObjectName("app_version");
    vbox_about->addWidget(version);

    QStringList version_lines;
    for (const auto& [key, value] : pp::library_versions()) {
        version_lines << QString::fromStdString(key) + QStringLiteral("=") +
                             QString::fromStdString(value);
    }
    auto* versions = new QPlainTextEdit(version_lines.join(QLatin1Char('\n')), page_about);
    versions->setObjectName(kLibraryVersions);
    versions->setReadOnly(true);                             // §2.10：只读清单
    // §9.1 U6-FIX（尺寸贴合内容）：不要 QPlainTextEdit 默认 192px 的 sizeHint——版本清单
    // 最多占 rows 行高度，超出部分滚动（内容不裁切，可滚动查看）
    const int version_rows =
        std::clamp(static_cast<int>(version_lines.size()), kVersionRowsMin, kVersionRowsMax);
    versions->setMaximumHeight(versions->fontMetrics().lineSpacing() * version_rows +
                               2 * versions->frameWidth() + kVersionRowsPad);
    vbox_about->addWidget(versions, 1);

    auto* license = new QLabel(
        tr("本程序 GPL-3.0-or-later。所用库：Exiv2/x265 (GPLv2+)、libheif (LGPLv3)、"
           "Qt (LGPLv3)、OIIO (Apache-2.0)、libjxl/libwebp/SVT-AV1/libaom (BSD)、"
           "lcms2/spdlog (MIT)。"),
        page_about);
    license->setObjectName("license_text");
    license->setWordWrap(true);
    vbox_about->addWidget(license);

    tabs->addTab(page_about, tr("关于"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addWidget(tabs, 1);
    root->addWidget(buttons);

    // §9.1 U6-FIX（尺寸贴合内容）：固定 520×420 会在运行页留下 ~260px 空白；
    // 改为按布局 sizeHint 收缩（三页取最高者，About 页高度已按 §9.1 收敛）
    adjustSize();

    // M2-T7 #28c：QTabWidget::sizeHint() 取三页最高者 → 矮页下方仍留白。
    // 按**当前页** sizeHint 增量式贴合：切换页签时把对话框高度整体加减
    // (当前页 sizeHint − 当前页已分配高度)——高页变高、矮页变矮，页内控件不裁切
    // （增量法不依赖 tabbar/按钮盒/边距的具体数值，样式无关）。
    const auto fit_height_to_page = [this, tabs] {
        QWidget* page = tabs->currentWidget();
        if (page == nullptr) {
            return;
        }
        // 增量法 + 迭代收敛：一次 resize 后页几何才更新，故最多再校正 2 轮
        // （每轮 delta == 0 即停；触到 minimumSizeHint 时也不会无限循环）。
        for (int pass = 0; pass < 3; ++pass) {
            if (QLayout* lay = layout()) {
                lay->activate();   // 让新页先拿到已分配高度
            }
            const int delta = page->sizeHint().height() - page->height();
            if (delta == 0) {
                break;
            }
            resize(width(), height() + delta);
        }
    };
    connect(tabs, &QTabWidget::currentChanged, this,
            [fit_height_to_page](int) { fit_height_to_page(); });
    fit_height_to_page();
    // 构造期（未 show）页几何尚未落定，增量法可能算错；首次 show 后再贴合一次。
    installEventFilter(new FitOnShow(this, fit_height_to_page));
}

pp::AppSettings SettingsDialog::settings() const {
    // 非确定路径（取消 / 关闭 / 未 exec）→ 构造快照（§2.10“仅确定路径返回编辑值”）
    pp::AppSettings s = original_values(this);
    if (result() != QDialog::Accepted) return s;

    if (const auto* w = findChild<QSpinBox*>(QLatin1String(kWorkers))) {
        s.workers = w->value();
    }
    if (const auto* w = findChild<QSpinBox*>(QLatin1String(kBudgetGb))) {
        s.budget_gb = w->value();
    }
    if (const auto* w = findChild<QSlider*>(QLatin1String(kFlattenGray))) {
        s.flatten_gray = static_cast<double>(w->value()) / kSliderMax;   // 0.01 步长
    }
    if (const auto* w = findChild<QCheckBox*>(QLatin1String(kRotateOrientation))) {
        s.rotate_orientation = w->isChecked();
    }
    if (const auto* w = findChild<QComboBox*>(QLatin1String(kMapProvider))) {
        const QString id = w->currentData().toString();
        if (!id.isEmpty()) s.map_provider = id.toStdString();
    }
    if (const auto* w = findChild<QLineEdit*>(QLatin1String(kAmapKey))) {
        s.amap_key = w->text().toStdString();
    }
    if (const auto* w = findChild<QSpinBox*>(QLatin1String(kTileCacheMb))) {
        s.tile_cache_mb = w->value();
    }
    if (const auto* w = findChild<QComboBox*>(QLatin1String(kLogLevel))) {
        const QString id = w->currentData().toString();
        if (!id.isEmpty()) s.log_level = id.toStdString();
    }
    // last_format / last_preset / last_out_root 不被本对话框编辑 → 保持构造快照
    return s;
}

}  // namespace pp::ui
