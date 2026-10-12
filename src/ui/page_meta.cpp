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
//   ③ GPS：纬度/经度（双列轨，fill）｜坐标显示（M6-T10：WGS-84 标签 + DMS 切换 + 复制）｜
//      海拔/方位（同）｜时间戳｜**操作行固定在地图上方**
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
// M6-T9 选点闭环（§4.4 / R3）：搜索 → search_results_ready → 多钉（map 的 set_result_pins）+
//   可点列表 → 点结果行（search_select）或点图钉（pin_clicked）→ 主钉落到该点 + 规则框回填
//   （**一律 WGS-84**；map 边界保证，amap 的 GCJ-02 偏移已在 parse/绘制两侧抵消）→ 点地图
//   微调（既有 point_selected 回填）→ 「地图选点」取视口中心（既有）→ run_reverse →
//   reverse_finished 地址回显（用户据此确认位置对不对）。两条取点入口同一句反馈
//   （已选结果：<名>）；换地图源 = 换地理编码源 → 结果钉随列表一并清（不留点不动的旧钉）。
//
// M6-T10 坐标基准透明化（§4.5 R4）：GPS 卡新增「坐标」显示行（紧贴经纬度行）——
//   ① 固定小标签「WGS-84（标准 GPS）」（Info 矢量图标 + tooltip：EXIF GPS 即 WGS-84 标准，
//      高德 GCJ-02 火星偏移已在内部抵消）；②『DMS』切换钮 + 坐标框右键菜单：十进制度 ↔
//      度分（pp::map::deg_to_dms 单源）互切；③『复制』链把**当前格式**坐标写剪贴板。
//   三者一律**只显示 / 只复制，不改写入值**—— 规则框 gps_lat/gps_lon 恒为十进制度
//   （build_rules 只读规则框），显示层与写入层在这一点上不共享状态。
//   边界精度提示：选点落在 coord.cpp 粗盒（72.004–137.8347E / 0.8293–55.8271N）边缘带
//   （≤0.5°）内 → gps_status 追加「该区域坐标精度存疑（国境/港澳台边界）」；生效坐标的
//   悬浮提示同句（盒内深处与境外不提示 —— 粗盒外偏移恒 identity，两向一致）。
//
// M6-T12 地图体验件（§5.3/§7-T12 R5）：地图块加**覆盖层**（map 的子布局 —— 卡内行节奏/地图
//   几何零改动）：① 左上「光标: lat, lon」读数 chip（接 mapwidget 的 cursor_moved，WGS-84、
//   约 100ms 节流；同一读数另挂坐标行 tooltip；**只回显，不写规则框、不动 gps_status**——
//   状态行留给选点/搜索结果等语义消息）；② 右下「－/＋」缩放钮（矢量图标 24×24 → map 的
//   zoom_in/zoom_out，视口中心锚点）。比例尺由 mapwidget 自绘（左下，白底 α190 同版权行口径）。
//   「混合叠标注」（map_sat_labels，T11 落地存储）的消费点 = MainWindow 启动/设置变更时的
//   map->set_sat_labels（本页只做覆盖层，不持设置值）。
//
// M6-T13 大地图（§7-T13 R5）：覆盖层右上「展开」钮 → 模态「地图」对话框（ui/map_dialog.*），
//   内含**同一只** MapWidget —— 重挂载（reparent）而非新建：图层/图钉/视口/在途瓦片请求随
//   控件走（零同步），关闭后挂回卡片行并复位固定高（卡片几何逐位复原；`pp-meta-map`
//   objectName 随控件、不变）。对话框默认不弹（只在点「展开」时出现）；尺寸跨展开记忆。
//
// 自验钩子（--ui-smoke 的 meta-geom / meta-* 探针读；非用户可见）：
//   * 动态属性（同时挂 MetaState 与页面本身）：pp_meta_follow_count / pp_meta_follow_name /
//     pp_meta_follow_pending / pp_meta_follow_stale_dropped / pp_meta_time_new /
//     pp_meta_gps_effective / pp_meta_multi_rows；M6-T8 加性：pp_meta_gps_layer（"road"|
//     "satellite"|"hybrid"）/ pp_meta_gps_results（结果数）/ pp_meta_gps_suggest（联想条数）/
//     pp_meta_gps_address（逆地理回显文本）；M6-T10 加性：pp_meta_gps_dms（显示档 bool）；
//   * ElidedLabel 的 `ppFullText`：显示文本可能按宽度省略成 "…"，语义值（全文）挂在这枚属性上，
//     断言按语义值比对；
//   * objectName（pp-* 命名法）：pp-meta-info-<time|camera|lens|exposure|size|color|file|gps|wb|
//     rating> / pp-meta-info-row-<0…4> / pp-meta-info-empty / pp-meta-info-empty-row /
//     pp-meta-info-hint / pp-meta-info-pill / pp-meta-info-gps-pill / pp-meta-gps-pill /
//     pp-meta-time-<old|new|note|src>-0 / pp-meta-time-empty / pp-meta-time-pill /
//     pp-meta-delta-<years|months|days|hours|minutes|seconds> / pp-meta-tz-<from|to> /
//     pp-meta-seg-delta / pp-meta-seg-tz / pp-meta-multi-<name|old|new>-<i> / pp-meta-multi-more /
//     pp-meta-gps-<lat|lon|alt|dir>（生效值，只读；**另一档**坐标读数与时间戳在 tooltip）/
//     pp-meta-gps-<read|pick|clear> /
//     pp-meta-gps-<provider|search-edit|search-button> / pp-meta-gps-search-results（M6-T8 起 =
//     可点结果列表）/ pp-meta-gps-<layer|suggest|address>（M6-T8 加性）/ pp-meta-gps-<datum|
//     dms|copy>（M6-T10 加性：WGS-84 标签 / DMS 切换 / 复制）/ pp-meta-map /
//     pp-meta-map-<cursor|zoom-in|zoom-out>（M6-T12 加性：覆盖层光标读数 chip / 缩放钮）/
//     pp-meta-map-<expand|dialog|close>（M6-T13 加性：覆盖层「展开」钮 / 大地图对话框 /
//     其底栏「关闭」钮）/ pp-meta-map-placeholder（M6-T13 加性：展开期卡片位的同高空占位）/
//     pp-meta-gps-rule-toggle / pp-meta-privacy-pill / pp-meta-keyword-<i> / gps_status /
//     gps_warning / privacy_strip / mtime_sync / gps_enable / gps_more_toggle /
//     gps_lat|gps_lon|gps_alt|gps_dir|gps_ts（规则输入）/ exception_label / exception_list /
//     tag_add_button / tag_row|tag_key|tag_value|tag_remove；
//   * 地图离线 banner（mapwidget 内硬编码黄底）：本页只做**宿主侧**收口 —— 文案去括号（进 tooltip）
//     与中性灰覆盖 QSS（修 1-38/1-39；mapwidget 文件零改动，令牌由本页铺）。
#include "ui/page_meta.h"

#include "ui/meta_format.h" // B2（M7-R2.1-F2）：显示格式化纯函数（meta_format）
#include "ui/meta_state.h"  // B.1（M7-R2.1-F2）：拆分共享内核（MetaState/常量/构造器）

#include "ui/formkit.h"    // M5-T7：全部表单行走 formkit 行构造器（R1–R4 构造层强制）
#include "ui/icons.h"      // 折叠插入符/删除钮一律矢量图标（C3）
#include "ui/map_dialog.h" // M6-T13：大地图对话框（同一 map 控件的重挂载宿主）

#include <QAbstractSpinBox>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleValidator>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mapwidget/dms.h"      // M6-T10：坐标显示格式化（度分/逆解析/粗盒边界带）
#include "mapwidget/geocoder.h" // M6-T8：联想/结果/逆地理的信号类型（SuggestItem/GeocodeResult）
#include "mapwidget/layers.h"   // M6-T8：图层支持性提示（tile_source_by_id / TileStyle）
#include "mapwidget/mapwidget.h"
#include "mapwidget/providers.h" // 地图源下拉的 id/label 单源（provider_ids/provider_by_id）
#include "ui/contextmenu.h"      // M7-R2D1 / UX-1②③④⑤：公共右键菜单件（attach_card_menu）
#include "ui/theme.h"

namespace pp::ui {
// B.1 拆分（M7-R2.1-F2）：本 TU 剩余实现归 pp::ui::meta（外部链接，声明面在
// ui/meta_state.h）—— 各步逐域迁出到 meta_*.cpp；PageMeta 方法留在 pp::ui。
namespace meta {

using formkit::FieldWidth;

// ---------------------------------------------------------------------------

// M6-T13（§7-T13 R5）：展开大地图 —— **同一控件重挂载**：map 暂挂到对话框内容区（图层/图钉/
// 视口/在途瓦片请求随控件走，零同步），exec() 返回后挂回卡片行并复位固定高（卡片几何逐位
// 复原）。对话框每次展开新建（主题档/尺寸记忆按时读/写本状态）。
// 卡片位：地图离场期间放**同高空占位**（模态期卡片不上下抖动；关闭时移除）。
void open_map_expanded_view(MetaState *st) {
    if (st == nullptr || st->map == nullptr || st->map_host == nullptr || st->map_line == nullptr) {
        return;
    }
    MapDialog dialog(st->map->window());
    if (st->map_dialog_size.isValid()) {
        dialog.resize(st->map_dialog_size); // 上次调整的尺寸（跨展开记忆）
    }
    if (st->map_expand != nullptr) {
        st->map_expand->setVisible(false); // 对话框自有「关闭」；展开期不重复该入口
    }
    auto *placeholder = new QWidget(st->map_host);
    placeholder->setObjectName(QString::fromLatin1(kMapPlaceholderName));
    placeholder->setFixedHeight(kMapHeight);
    st->map_line->addWidget(placeholder, 1, Qt::AlignVCenter);
    dialog.adopt(st->map);
    dialog.exec();    // 模态：返回即已关闭（Esc / 底栏关闭 / 标题栏 ✕ 同效）
    dialog.release(); // R-261：与 adopt 成对（对话框侧复原几何约束；析构另有兜底）
    // 挂回卡片行：先撤占位（占位不参与任何信号链，同步删）再挂 map；随后几何约束逐位复原
    // （宿主 + 行槽 + objectName 随控件、无需重挂）。UX-4 stretch（M7-R2-B）：复位口径 = 与
    // 初建同源（min 260 + 可拉伸），不再 setFixedHeight —— 探针断言改 `>= 260` + geometry 逐位。
    st->map->setParent(st->map_host);
    delete placeholder;
    st->map_line->addWidget(st->map, 1);
    st->map->setMinimumHeight(kMapHeight);
    st->map->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    st->map->show();
    if (st->map_expand != nullptr) {
        st->map_expand->setVisible(true);
    }
    st->map_dialog_size = dialog.size();
}

// ---------------------------------------------------------------------------
// 跟随项 probe → meta_probe.{h,cpp}（B.1 拆分步 B4：ProbeScanner 唯一 IO 宿主，R-181）
// ---------------------------------------------------------------------------

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

void refresh_effective(MetaState *st) {
    st->info_hint->set_full_text(follow_hint(st));
    // 时间卡范围胶囊（mockup「影响 12 / 24」的语义位）：页面知道的只有批内文件数，除数/被除数
    // 同源 → 不编造"N / M"。
    st->time_pill->setText(PageMeta::tr("影响 %1 个文件").arg(st->batch_files.size()));
    if (st->probe->pending()) {
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

// 折叠入口的插入符 = **矢量图标**（▸ 收起 / ▾ 展开），不再是字形文本（C3/A8）
void set_fold_icon(MetaState *st, QToolButton *button, bool expanded) {
    if (button == nullptr) {
        return;
    }
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    icons::apply(button, expanded ? icons::Name::ChevronDown : icons::Name::ChevronRight,
                 st->tokens.text3, st->tokens.text3, 12);
}

void refresh_all(MetaState *st) {
    refresh_time_inputs(st);
    refresh_gps_inputs(st);
    refresh_privacy_inputs(st);
    refresh_gps_warning(st);
    refresh_effective(st);
}

// ---------------------------------------------------------------------------
// 样式（tokens 单源 = ui/theme.h；本页只把 tokens 铺到自己的控件上；A6：硬编码色一律收口）
// ---------------------------------------------------------------------------

void apply_style(MetaState *st) {
    const theme::Tokens &t = st->tokens;
    const auto css = [](const QColor &c) { return theme::css_color(c); };

    // UX-6⑥（M7-WP4）：本页 QSS 全部收编进 theme.h —— 跨页同形规则 = theme::field_qss/
    // card_qss/formkit_qss/mod_qss/pill_qss，页专规则 = theme::page_meta_qss（含"为何不能
    // 公共"注释）；页内不再拼 QSS 字符串。与窗口 QSS 同值同源，重复规则无副作用（页面自足）。
    QString qss;
    qss += theme::formkit_qss(t);
    qss += theme::mod_qss(t);
    qss += theme::pill_qss(t);
    qss += theme::card_qss(t);
    qss += theme::field_qss(t);
    qss += theme::page_meta_qss(t);
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
    // M6-T12：地图缩放钮的矢量图标随令牌重放（覆盖层固定白底 → 图标恒取深灰 #404040）
    if (st->zoom_in != nullptr) {
        icons::apply(st->zoom_in, icons::Name::ZoomIn, theme::overlay_tokens().hud_fg,
                     theme::overlay_tokens().hud_fg_dim,
                     kMapZoomIconPx); // overlay 令牌组（不随主题变）
    }
    if (st->zoom_out != nullptr) {
        icons::apply(st->zoom_out, icons::Name::ZoomOut, theme::overlay_tokens().hud_fg,
                     theme::overlay_tokens().hud_fg_dim,
                     kMapZoomIconPx); // overlay 令牌组（不随主题变）
    }
    // M6-T13：展开钮同档（覆盖层固定白底 → 图标恒取深灰 #404040）
    if (st->map_expand != nullptr) {
        icons::apply(st->map_expand, icons::Name::ZoomFit, theme::overlay_tokens().hud_fg,
                     theme::overlay_tokens().hud_fg_dim,
                     kMapZoomIconPx); // overlay 令牌组（不随主题变）
    }
    // M6-T10：坐标行 WGS-84 标签的 Info 矢量图标随令牌重放（QLabel 承载的 QPixmap 取色不吃 QSS）
    if (st->datum_icon != nullptr) {
        st->datum_icon->setPixmap(icons::pixmap(icons::Name::Info, kDatumIconPx, t.text3,
                                                st->datum_icon->devicePixelRatioF()));
    }
    if (st->dms_toggle != nullptr) {
        const QSignalBlocker blocker(st->dms_toggle); // 主题重放不动显示档，只保持控件态与档一致
        st->dms_toggle->setChecked(st->dms_mode);
        st->dms_toggle->set_tokens(t); // R-184：chip = formkit 自绘件（令牌随主题重放）
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

void build_ui(PageMeta *page) {
    auto *st = new MetaState();
    st->page = page;
    st->setObjectName(QString::fromLatin1(kStateObjectName));
    st->setParent(page);
    page->setProperty(kStateProperty, QVariant::fromValue(static_cast<QObject *>(st)));
    st->probe = new ProbeScanner(st);
    // B4（R-181）：扫描发起/回填的**唯一** connect（同线程 direct = 既有 refresh_effective
    // 调用序逐位不变）。
    QObject::connect(st->probe, &ProbeScanner::changed, st, [st] { refresh_effective(st); });
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
    // UX-1②③④⑤（M7-R2D1）：一卡一菜单 —— 各接一行（意图构造 = *_card_intents）
    contextmenu::attach_card_menu(
        info_card, [st] { return contextmenu::info_card_actions(info_card_intents(st)); });
    contextmenu::attach_card_menu(
        time_card, [st] { return contextmenu::time_card_actions(time_card_intents(st)); });
    contextmenu::attach_card_menu(
        gps_card, [st] { return contextmenu::gps_card_actions(gps_card_intents(st)); });
    contextmenu::attach_card_menu(
        switch_card, [st] { return contextmenu::privacy_card_actions(privacy_card_intents(st)); });
    v->addWidget(info_card);
    v->addWidget(time_card);
    // UX-4 stretch（M7-R2-B）：富余高归 GPS 卡 → 逐级到地图（ux-review §4-5「吃掉腾出的空间」）；
    // 尾部 addStretch(1) 撤除 —— 富余不再被尾部空档截留（内容不足一屏时地图吃满右栏）。
    v->addWidget(gps_card, 1);
    v->addWidget(switch_card);
    scroll->setWidget(content);

    apply_style(st);
    refresh_all(st);
}

} // namespace meta

// 门面方法直呼拆分面（state_of / build_rules / apply_state / …）
using namespace meta;

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
    // R-104（M7-WP7，UI 侧词表回落）：手改 INI 写出词表外 id 时下拉不再"落空但值保留" ——
    // 不在 provider_ids() 词表内的一律回落 "osm"（settings.cpp 的持久层词表回落属跨界文件，
    // 登记 handoff「跨界需求」）。
    const QStringList known = pp::map::provider_ids();
    const QString id = known.contains(provider_id) ? provider_id : QStringLiteral("osm");
    st->map->set_provider(id);
    st->map->set_amap_key(amap_key);
    st->map->set_cache_mb(cache_mb);
    if (st->provider != nullptr) {
        const QSignalBlocker blocker(st->provider);
        const int index = st->provider->findData(id);
        if (index >= 0) {
            st->provider->setCurrentIndex(index);
        }
    }
    // R-045：换源清理两路共用 clear_search_state（此处信号被挡不走下拉回调 → 补一次清理，
    // 否则设置对话框换源会留旧钉/旧结果/联想 → "点不动/点错代"边缘态）。
    clear_search_state(st);
    refresh_layer_hint(st); // M6-T8：换源后图层支持性提示随之更新（信号被挡，这里补一次）
}

} // namespace pp::ui
