// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页拆分共享内核（B.1 拆分步 B1：meta_state）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（page_meta 拆分蓝图）
// 本单元承载拆分后的**跨 TU 共享面**：
//   * MetaState（页面状态；B1 终态 = Core + FollowModel + 四卡子聚合）与 state_of/set_hook；
//   * 页面共享常量（自验钩子名/几何档/文案）、页内别名、页内小控件（DeltaSpin/RatingStars）、
//     表单行构造器（TrackGroup/make_*）与四卡共用卡头；
//   * 跨单元函数面的声明（各 meta_*.cpp 定义；卡片构建/刷新入口在此声明）。
// page_meta.h 逐字节冻结（B.1 硬约束）——本头是**新增内部头**，不动冻结面。
#pragma once

#include "core/metadata.h"
#include "mapwidget/geocoder.h"  // MetaState::results 元素类型（GeocodeResult）
#include "ui/formkit.h"          // 表单行构造器 + 自绘件（PillBadge/SwitchControl/…）
#include "ui/icons.h"            // RatingStars 形 = icons 库 star 路径（单源）
#include "ui/meta_map_overlay.h" // MapOverlayController（B6：覆盖层状态聚合）
#include "ui/meta_probe.h"       // ProbeSnapshot（FollowModel::probes 元素类型）
#include "ui/meta_rules.h"       // RuleInputs + 规则装配声明面（B3）
#include "ui/page_meta.h"        // MetaSelectionItem（FollowModel::items 元素类型）
#include "ui/theme.h"            // theme::Tokens（MetaState 值成员）
#include <QApplication>          // theme.h 要求包含者先引入
#include <QHash>
#include <QList>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QSize>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVariant>
#include <QWidget>
#include <array>
#include <optional>
#include <utility>
#include <vector>

class QAction;
class QActionGroup;
class QComboBox;
class QFrame;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace pp::map {
class MapWidget;
} // namespace pp::map

namespace pp::ui {

namespace contextmenu {
struct InfoCardIntents;
struct TimeCardIntents;
struct GpsCardIntents;
struct PrivacyCardIntents;
} // namespace contextmenu

namespace meta {

// ---------------------------------------------------------------------------
// 页面共享常量（原 page_meta.cpp 匿名命名空间；inline constexpr = 每 TU 同值零开销）
// ---------------------------------------------------------------------------

constexpr const char *kStateProperty = "pp_meta_state";
constexpr const char *kStateObjectName = "pp_meta_state";
// 页面级动态属性钩子（QSS 选择器锚）：与 theme.h 的 ppFk/ppMod 并列，页面私有不外泄。
constexpr const char *kFieldProperty =
    theme::kFkProperty; // UX-6⑤/R-054：三条钩子轨并入 ppFk（旧 ppMetaField 轨退役）

// §7.1 时间卡的对照行主字段（core/metadata.h 的字段表：ExifTool 名）；另两字段进标签 tooltip。
constexpr const char *kPrimaryTimeSource = "DateTimeOriginal";
constexpr const char *kSecondaryTimeSources[2] = {"CreateDate", "ModifyDate"};

// §5.2 多选小表上限（超过 → 「…还有 N 个」）
constexpr int kMultiRowCap = 8;
// UX-4⑤（ux-review-14.md §4/§16-2 已定档）：地图 136 → **260**（"腾出的空间都给地图"；
// 展开期占位/挂回复位同用本常量 —— smoke_probe_meta 的 136 锚随之换 260）。
constexpr int kMapHeight = 260;
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
// M6-T10（§4.5 R4）：WGS-84 标签的矢量图标盒（与结果行图钉同规格）
constexpr int kDatumIconPx = 12;
constexpr double kSuggestZoom = 16.0; // 联想定位的缩放档（与 search_select 的 ≥12 同向）
// M6-T12（§5.3/§7-T12 体验件）：地图覆盖层 —— 「－/＋」缩放钮（矢量图标钮档）与光标坐标读数 chip
// （覆盖层是 map 的子布局：不进卡内行节奏，地图几何/内容零改动）
constexpr int kMapZoomBtnPx = 24;
constexpr int kMapZoomIconPx = 12;
constexpr const char *kMapCursorIdleText = QT_TR_NOOP("光标: —");
constexpr const char *kMapCursorTooltipText =
    QT_TR_NOOP("光标世界坐标（WGS-84，约 100ms 刷新）；移出地图后保留最后一次读数");
constexpr const char *kMapZoomInText = QT_TR_NOOP("放大（＋1 档）");
constexpr const char *kMapZoomOutText = QT_TR_NOOP("缩小（−1 档）");
// M6-T13（§7-T13 R5）：覆盖层右上的「展开」钮 —— 同一 map 控件重挂载到大地图对话框
// （图标 = zoom-fit 的四角外扩形，全仓唯一「变大视图」语义的既有矢量图标；C3 禁字形）
constexpr const char *kMapExpandText = QT_TR_NOOP("展开大地图");
constexpr const char *kMapExpandTooltipText =
    QT_TR_NOOP("在更大的视图里查看/选点（同一地图：图层、图钉与视口都跟随）");
// 展开期卡片位的同高空占位（名字入自验钩子清单：模态期卡片高度不变）
constexpr const char *kMapPlaceholderName = "pp-meta-map-placeholder";
// 图层 Seg 三态（与 pp::map::TileStyle 同序；键 = 自验钩子读数）
constexpr std::array<const char *, 3> kLayerStyleKeys = {"road", "satellite", "hybrid"};
// 分类 chips 文案 = 直接进 run_search 的类目关键词（§4.3 分类 POI）。UX-4②：6 类目由
// 一整行 chips 收进搜索框右侧「分类 ▾」下拉（objectName pp-meta-gps-category-0..5 随
// menu action 保留，探针改断言菜单项数 == 6）。
constexpr std::array<const char *, 6> kCategoryKeywords = {
    QT_TR_NOOP("餐饮"), QT_TR_NOOP("酒店"), QT_TR_NOOP("加油站"),
    QT_TR_NOOP("景点"), QT_TR_NOOP("地铁"), QT_TR_NOOP("商场")};
// 文案不含 ▾（U+25BE 在 §3.3 禁字形清单）—— 下拉箭头由 QMenu 的菜单指示器呈现（非字形字符）。
constexpr const char *kCategoryButtonText = QT_TR_NOOP("分类");
constexpr const char *kCategoryButtonTooltip = QT_TR_NOOP("按类目搜索（6 类）");
// UX-4④：地址行删除后，地址只进 gps_status 的 tooltip + 右键「复制地址」（视觉只留
// 「已选点 · …」一行）。
constexpr const char *kCopyAddressText = QT_TR_NOOP("复制地址");
// R-315：瓦片加载的 UI 反馈（tiles_pending_changed → 地图浮层「加载中…」chip）
constexpr const char *kMapLoadingText = QT_TR_NOOP("加载中…");
// UX-5：三卡「启用」开关（卡头右置）的 tooltip
constexpr const char *kCardEnableTooltip =
    QT_TR_NOOP("启用该卡：关闭时本卡不参与规则（参数保留但不写入）");
// GPS 卡头状态胶囊文案（修 1-33：状态用独立徽标；§5.2 的两条状态语逐字保留）
constexpr const char *kHasGpsText = QT_TR_NOOP("已有 GPS");
constexpr const char *kNoGpsText = QT_TR_NOOP("无 GPS");
constexpr const char *kWillRemoveText = QT_TR_NOOP("将被移除");
constexpr const char *kGpsClearText = QT_TR_NOOP("将清除 GPS");
// M6-T10（§4.5 R4 坐标基准透明化）：WGS-84 标签 / DMS 切换 / 复制 / 边界精度提示的固定文案
constexpr const char *kDatumTooltipText =
    QT_TR_NOOP("EXIF GPS 采用 WGS-84 标准；高德火星偏移（GCJ-02）已在内部抵消，写入为真实坐标");
constexpr const char *kDmsToggleTooltipText =
    QT_TR_NOOP("坐标显示切换：十进制度 ↔ 度分（DMS）；只影响显示与复制，不改写入值");
constexpr const char *kCopyCoordsTooltipText = QT_TR_NOOP("复制当前格式坐标（只读显示值）");
// R-046（M7-WP7）：文案与几何对齐 —— 判定是「粗包围盒边缘带」（dms.cpp），不是真实国境线
// /港澳台边界；原「（国境/港澳台边界）」把粗盒启发式说成了精确边界，且盒内深处（港澳台、
// 越南北部等）从不提示恰是最需要提示处 —— 文案改述真实判据。
constexpr const char *kPrecisionSuspectText = QT_TR_NOOP("该区域坐标精度存疑（靠近粗包围盒边界）");
// 空态一行（修 1-23/1-31：未选择文件 → 一行说明，不平铺空值）
constexpr const char *kNoSelectionText = QT_TR_NOOP("未选择文件，请在左侧列表选择照片");
constexpr const char *kNoKeywordsText = QT_TR_NOOP("无关键字");

// 单行省略标签（§12-14「全界面禁止文案折行（超长省略号）」；与 mainwindow.cpp 的 ElidedLabel
// 同口径）
// ---------------------------------------------------------------------------

// R-020/R-320（M7-WP4）：ElidedLabel 的 7 份逐字拷贝收敛到 formkit 唯一实现
// （本文件原 30+ 行自拷类删除；行为逐位等价 —— 全文 + ppFullText + tooltip + 省略号）。
using ElidedLabel = formkit::ElidedLabel;

// ---------------------------------------------------------------------------
// 文本格式化 → meta_format.{h,cpp}（B.1 拆分步 B2，M7-R2.1-F2；纯函数面整体迁出，
// tr 上下文保持 PageMeta、输出逐字节不变）
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// R-179（B7）：kFieldProperty 的取值**常量表**（原 20 种字面量 stringly 散落各构建器）——
// 值串 = QSS 选择器锚，逐字不变（仅换具名常量）；新增档位 = 加一行。
namespace fieldprop {
inline const QString kAddress = QStringLiteral("address");
inline const QString kArrow = QStringLiteral("arrow");
inline const QString kBtn = QStringLiteral("btn");
inline const QString kEmpty = QStringLiteral("empty");
inline const QString kFold = QStringLiteral("fold");
inline const QString kHint = QStringLiteral("hint");
inline const QString kLink = QStringLiteral("link");
inline const QString kMapHud = QStringLiteral("map-hud");
inline const QString kMapZoom = QStringLiteral("map-zoom");
inline const QString kMultiMore = QStringLiteral("multi-more");
inline const QString kMultiName = QStringLiteral("multi-name");
inline const QString kNew = QStringLiteral("new");
inline const QString kNote = QStringLiteral("note");
inline const QString kOld = QStringLiteral("old");
inline const QString kResultAddr = QStringLiteral("result-addr");
inline const QString kResultDist = QStringLiteral("result-dist");
inline const QString kResultName = QStringLiteral("result-name");
inline const QString kRule = QStringLiteral("rule");
inline const QString kSub = QStringLiteral("sub");
inline const QString kView = QStringLiteral("view");
} // namespace fieldprop

using formkit::FieldWidth; // 页内行构造统一入口（R2/R3 栅格；原 page_meta.cpp 同款）

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
        // §4.2「数值」：尾部对齐（R-218：逻辑坐标 —— LTR=右 / RTL=左；LTR 恒等）
        setAlignment(Qt::AlignTrailing | Qt::AlignVCenter);
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
        // R-218：星序 = 逻辑序（首星在 leading 端）——RTL 镜像排布；LTR 恒等（x' = x）。
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        for (int i = 0; i < 5; ++i) {
            QTransform transform;
            const int logical_x = i * (kStarPx + kStarGap);
            transform.translate(rtl ? width() - kStarPx - logical_x : logical_x, 0);
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

// —— Core：页面锚 / 加载协议 / 主题令牌 / 卡片与重放清单 ——
struct CoreState {

    PageMeta *page = nullptr;
    bool loading = false; // apply_rules 期间抑制信号与重复刷新
    theme::Tokens tokens;

    std::array<QFrame *, 4> cards{};

    // ---- 主题重放的控件集（持有 tokens 副本的自绘控件）----
    QList<formkit::PillBadge *> pills;         // 常驻胶囊（卡头/GPS 值格）
    QList<formkit::PillBadge *> keyword_pills; // 关键字胶囊（随跟随项重建 → 每次刷新重填）
    QList<formkit::SwitchControl *> switches;
};

// —— 跟随数据（§5.2）——
struct FollowModel {

    // ---- 跟随数据（§5.2）----
    QStringList batch_files;
    QList<MetaSelectionItem> items; // 跟随项（选中项；无选中 → 首个文件）
    bool from_selection = false;    // false = 无选中 → 首个文件（提示 "(1/N)"）
    std::vector<ProbeSnapshot> probes;
    // B4（R-181）：generation/pending/stale 归 ProbeScanner（唯一 IO 宿主）；本处只持句柄。
    ProbeScanner *probe = nullptr;
};

// —— 卡片 ① 关键信息（kv 双列 5 行 + 空态一行）——
struct InfoCardState {

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
};

// —— 卡片 ② 时间偏移 ——
struct TimeCardState {
    // ---- UX-5（M7-WP7）：三卡「启用」开关（卡头右置 SwitchControl 30×16）----
    // 语义：关闭 = 该卡整卡不参与规则（build_rules 跳过）+ 卡内规则控件置灰；打开 = 按当前
    // 参数装配。新会话初态 = 关（值为空 → 输出零变化）；恢复含值旧状态 = 开（apply_state）。
    formkit::SwitchControl *time_enable = nullptr;

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
};

// —— 卡片 ③ GPS ——
struct GpsCardState {
    formkit::SwitchControl *gps_enable_switch = nullptr; // objectName pp-meta-gps-enable

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
    QActionGroup *category_group = nullptr; // UX-4②：6 类目 = 下拉 action（互斥勾选）
    QList<QAction *> category_actions;
    std::vector<pp::map::GeocodeResult> results; // 搜索结果（与 map 内部 results 同序）
    QWidget *address_row = nullptr;
    ElidedLabel *address_value = nullptr;
    QPushButton *read_gps = nullptr;
    QPushButton *pick_map = nullptr;
    QPushButton *clear_gps = nullptr;
    QLabel *gps_status = nullptr;
    // ---- UX-4（M7-WP7）：地图 App 化 ----
    QPushButton *category_btn = nullptr; // 搜索框右侧「分类 ▾」（6 类目并入下拉）
    QMenu *category_menu = nullptr;      // 6 action（objectName pp-meta-gps-category-0..5 保留）

    std::optional<std::pair<double, double>> last_centered; // R-047：仅生效坐标变化才 center_on
    QHash<QString, int> result_row_by_key;                  // R-223：稳定键 → 结果行（跨层对齐）
    // ---- M6-T10 加性（坐标基准透明化 R4：WGS-84 标签 / DMS 显示档 / 复制 / 边界精度提示）----
    bool dms_mode = false;                // 坐标显示档（false = 十进制度；纯显示层，不入规则）
    std::optional<pp::GpsData> shown_gps; // 生效坐标缓存（DMS 切换只重排这一份，不重算 preview）
    QLabel *datum_icon = nullptr;         // 「WGS-84（标准 GPS）」前置 Info 矢量图标
    QLabel *datum_label = nullptr;
    QWidget *datum_row = nullptr;              // 坐标行（M6-T12：光标坐标读数的 tooltip 承载面）
    formkit::ChipButton *dms_toggle = nullptr; // DMS 切换（chip；与右键菜单同一收口）
    QToolButton *copy_coords = nullptr;        // 复制当前格式坐标（文字链）
    QToolButton *rule_toggle = nullptr;
    QWidget *rule_host = nullptr;
    QWidget *rule_altdir_row = nullptr; // 更多字段折叠的两行（gps_wiring 的折叠接线用）
    QWidget *rule_ts_row = nullptr;
    formkit::CheckBox *gps_enable = nullptr;
    QToolButton *more_toggle = nullptr; // 规则的可选字段折叠（海拔/方位/时间戳）
    QLineEdit *lat = nullptr;
    QLineEdit *lon = nullptr;
    QLineEdit *alt = nullptr;
    QLineEdit *dir = nullptr;
    QLineEdit *ts = nullptr;
};

// —— 卡片 ④ 隐私与文件时间 ——
struct PrivacyCardState {
    formkit::SwitchControl *privacy_enable = nullptr;

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
    // R-180（B3）：tag 行**显式键值对清单**（add_tag_row 登记 / clear_tag_rows 清空 /
    // 移除钮同序移除）—— 替代 findChild("tag_key") 隐式约定；顺序 = tag_rows 布局顺序。
    QList<std::pair<QLineEdit *, QLineEdit *>> tag_edits;
    QPushButton *add_tag = nullptr;
    QLabel *exc_label = nullptr;
    QListWidget *exc_list = nullptr;
};

// R-177（B1）：上帝结构拆为 Core + FollowModel + 四卡子聚合（上方六个具名聚合；
// 本结构只做**组合 + 状态锚**）。Q_OBJECT = 动态属性锚的显式化（state_of 走 qobject_cast）。
struct MetaState : QObject {
    Q_OBJECT
public:
    CoreState core;
    FollowModel follow;
    InfoCardState info;
    TimeCardState time;
    GpsCardState gps;
    PrivacyCardState privacy;
    // ---- M6-T12/13 加性：地图覆盖层 + 大地图（B6：状态归 MapOverlayController）----
    MapOverlayController overlay;

    // 常驻胶囊登记（tokens 重放清单；构建器调用点不变）
    void add_pill(formkit::PillBadge *pill) {
        pill->set_tokens(core.tokens);
        core.pills.append(pill);
    }
};

// R-180（M7-R2.1-F2）：loading 协议**显式化** —— 手查/手恢复收口为 RAII 守卫（可嵌套；
// 析构恢复进入前值）。判读点仍只在 notify_rules_changed（语义零变化）。
struct LoadingGuard {
    LoadingGuard(const LoadingGuard &) = delete;
    LoadingGuard &operator=(const LoadingGuard &) = delete;

    explicit LoadingGuard(MetaState *st) : st_(st), prev_(st->core.loading) {
        st->core.loading = true;
    }
    ~LoadingGuard() { st_->core.loading = prev_; }

private:
    MetaState *st_;
    bool prev_;
};

// R-180（M7-R2.1-F2）：关键信息值槽的**具名索引** —— 原 info_value[8] 双侧算术约定
// （构建器 i*2/i*2+1 与刷新侧 [0..7] 裸下标）收口为同一词表；顺序 = 槽位定义顺序。
enum InfoSlot : int {
    kInfoTime = 0,
    kInfoCamera = 1,
    kInfoLens = 2,
    kInfoExposure = 3,
    kInfoSize = 4,
    kInfoColor = 5,
    kInfoFile = 6,
    kInfoGps = 7,
    kInfoSlotCount = 8,
};

// state_of：PageMeta 的动态属性锚（**契约保留**：探针面/注册表经此取页面状态）。
// R-177（B1）：动态属性 static_cast 的显式化随 B1 落地（MetaState 带 Q_OBJECT + qobject_cast）。
MetaState *state_of(const PageMeta *page);

// 自验钩子（动态属性）：同时挂在 MetaState 子对象（自验程序）与页面本身（--ui-smoke 探针只持有

void set_hook(MetaState *st, const char *name, const QVariant &value);

// ---------------------------------------------------------------------------
// 跨单元函数面（定义落点见各 meta_*.cpp 头注释；本头只声明）
// ---------------------------------------------------------------------------

// —— 共享胶水（meta_state.cpp）——
void notify_rules_changed(MetaState *st);
void set_gps_rule_enabled(MetaState *st, bool on);
QString follow_hint(const MetaState *st);
void refresh_effective(MetaState *st);
void refresh_all(MetaState *st);

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
    void add(QWidget *item, int stretch = 0);
    void gap(int px);
    void stretch();
};

TrackGroup make_group(QWidget *parent);
QLabel *unit_slot(QWidget *parent, const QString &text);
ElidedLabel *note_label(QWidget *parent, const QString &object_name = QString());
QToolButton *make_link(QWidget *parent, const QString &object_name, const QString &text);
QPushButton *make_button(QWidget *parent, const QString &object_name, const QString &text);
QLineEdit *make_view_field(QWidget *parent, const QString &object_name);
QLineEdit *make_rule_field(QWidget *parent, const QString &object_name);
formkit::ChipButton *make_chip(QWidget *parent, const QString &object_name, const QString &text);
QToolButton *make_map_hud_button(QWidget *parent, const QString &object_name, icons::Name icon,
                                 const QString &tip);

// —— 卡头（meta_state.cpp；四卡共用）——
QFrame *make_card(QWidget *parent);
struct CardHeader {
    QWidget *row = nullptr;
    QLabel *title = nullptr;
    formkit::PillBadge *pill = nullptr;
    ElidedLabel *hint = nullptr;
    formkit::SwitchControl *enable = nullptr; // UX-5：卡头「启用」开关（enable_name 空 = 无）
};
CardHeader make_header(MetaState *st, QWidget *parent, const QString &title,
                       const QString &pill_name, const QString &hint_name,
                       const QString &enable_name = QString());

// —— 规则装配（meta_rules.cpp；B3）：声明面整体在 ui/meta_rules.h ——

// —— probe 异步扫描（meta_probe.cpp；B4）——
void request_probe_scan(MetaState *st);

// —— 卡片刷新（各 meta_card_*.cpp；B5）——
void refresh_info_card(MetaState *st, const std::vector<pp::EffectivePreview> &previews);
void refresh_time_section(MetaState *st, const std::vector<pp::EffectivePreview> &previews);
void refresh_time_inputs(MetaState *st);
void refresh_privacy_inputs(MetaState *st);
void refresh_gps_section(MetaState *st, const pp::EffectivePreview *preview);
void refresh_gps_inputs(MetaState *st);
void refresh_gps_warning(MetaState *st);
void refresh_keywords(MetaState *st);
void clear_multi_rows(MetaState *st);

// —— GPS 卡（meta_card_gps.cpp；B5）——
void fill_result_rows(MetaState *st);
void refresh_layer_hint(MetaState *st);
void clear_search_state(MetaState *st);
void apply_picked_point(MetaState *st, double lat, double lon, const QString &status);

// —— 地图覆盖层（meta_map_overlay.{h,cpp}；B6）：声明面在 ui/meta_map_overlay.h ——

// —— 样式（meta_style.cpp；B7）——
void apply_style(MetaState *st);
void set_fold_icon(MetaState *st, QToolButton *button, bool expanded);

// —— 卡片构建（各 meta_card_*.cpp；B5）——
QWidget *build_info_card(MetaState *st, QWidget *parent);
QWidget *build_time_card(MetaState *st, QWidget *parent);
QWidget *build_gps_card(MetaState *st, QWidget *parent);
QWidget *build_switches_card(MetaState *st, QWidget *parent);

// —— 卡右键菜单意图（各 meta_card_*.cpp；M7-R2D1 / UX-1②③④⑤）——
//   意图一律走**既有控件/收口**（按钮 click / 开关 toggle / 既有静态函数），不新增第二套
//   行为路径；菜单内容 = contextmenu 的四类 actions（12 类表逐条）。
contextmenu::InfoCardIntents info_card_intents(MetaState *st);
contextmenu::TimeCardIntents time_card_intents(MetaState *st);
contextmenu::GpsCardIntents gps_card_intents(MetaState *st);
contextmenu::PrivacyCardIntents privacy_card_intents(MetaState *st);

} // namespace meta
} // namespace pp::ui
