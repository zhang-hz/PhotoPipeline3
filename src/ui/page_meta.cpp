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
// 自验钩子（--ui-smoke 的 meta-geom / meta-* 探针读；非用户可见）。本清单是人读明细
// **本体**（R-260：寄生从接口头 page_meta.h 移出后落此，见 meta_probe_context.h 头注）；
// 机读单源 = ui/meta_probe_context.h 的 `hook::`/`objname::` 词表 + `ProbeContext` 访问器
// （写侧 set_hook/setObjectName 与读侧探针一律走单源表；本清单只作契约文档，值串以单源表
// 为准，`<>` 记法在表中展开成 `%1` 模式或逐键常量）：
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

#include "ui/meta_format.h"        // B2（M7-R2.1-F2）：显示格式化纯函数（meta_format）
#include "ui/meta_probe_context.h" // R-182/R-258：钩子/词表单源
#include "ui/meta_state.h"         // B.1（M7-R2.1-F2）：拆分共享内核（MetaState/常量/构造器）

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
// B1（M7-R2.1-F2）：follow_hint / refresh_effective / refresh_all → meta_state.cpp

void build_ui(PageMeta *page) {
    auto *st = new MetaState();
    st->core.page = page;
    st->setObjectName(QString::fromLatin1(kStateObjectName));
    st->setParent(page);
    page->setProperty(kStateProperty, QVariant::fromValue(static_cast<QObject *>(st)));
    st->follow.probe = new ProbeScanner(st);
    // B4（R-181）：扫描发起/回填的**唯一** connect（同线程 direct = 既有 refresh_effective
    // 调用序逐位不变）。
    QObject::connect(st->follow.probe, &ProbeScanner::changed, st, [st] { refresh_effective(st); });
    st->core.tokens = theme::tokens(theme::preferred_theme_mode());

    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // 竖滚动条常驻：内容高随选中变化会把滚动条"顶"出来/收回去，内容宽随之 ±15px，
    // 卡内等分栅格/对轨件整体横移（与 page_output / page_run 同口径）
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    outer->addWidget(scroll);

    auto *content = new QWidget(scroll);
    content->setObjectName(objname::kContent);
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(theme::Metrics::gap); // 卡间距 8（§4.3）
    QFrame *info_card = qobject_cast<QFrame *>(build_info_card(st, content));
    QFrame *time_card = qobject_cast<QFrame *>(build_time_card(st, content));
    QFrame *gps_card = qobject_cast<QFrame *>(build_gps_card(st, content));
    QFrame *switch_card = qobject_cast<QFrame *>(build_switches_card(st, content));
    st->core.cards = {info_card, time_card, gps_card, switch_card};
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
    // UX-11 余量收口（M7-R2.1-F4）：page_meta 侧卡片防压缩 —— 与 page_output 四卡 /
    // page_run 三卡同口径（QScrollArea(widgetResizable) 会把 Preferred 件压向
    // minimumSizeHint，卡片按当前内容 sizeHint 钉下限；stretch 1 的 GPS 卡富余高照旧归它）。
    scroll->setWidget(content);

    apply_style(st);
    refresh_all(st);
    // UX-11 余量收口（M7-R2.1-F4）：page_meta 侧卡片防压缩 —— 与 page_output 四卡 /
    // page_run 三卡同口径（QScrollArea(widgetResizable) 会把 Preferred 件压向
    // minimumSizeHint，卡片按当前内容 sizeHint 钉下限）。两处按判据取舍（如实登记）：
    //   * 位置在 refresh_all **之后** —— sizeHint 取刷新态内容（关键字胶囊等动态件），
    //     建树瞬时的过期 hint 钉高会破坏 meta-geom 的行距 28 不变量（实测踩坑）；
    //   * **GPS 卡不锁** —— 它是 UX-4 stretch 的富余高载体（v->addWidget(gps_card, 1)），
    //     动态高被 meta-map-expand 的恢复基线（260/262）与 map 高度探针钉住，锁高与之互斥。
    for (QFrame *card : {info_card, time_card, switch_card})
        formkit::lock_card_size(card);
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
    st->follow.batch_files = paths;
    refresh_effective(st); // 「影响 N 个文件」与 (i/N) 跟随提示
}

void PageMeta::set_selection(const QList<MetaSelectionItem> &items, bool from_selection) {
    MetaState *st = state_of(this);
    st->follow.items = items;
    st->follow.from_selection = from_selection;
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
    st->privacy.exc_list->clear();
    for (const QString &path : paths) {
        st->privacy.exc_list->addItem(path);
    }
    st->privacy.exc_label->setText(paths.isEmpty() ? tr("无例外文件")
                                                   : tr("%1 个文件带元数据例外").arg(paths.size()));
    // 例外计数 = 卡头中性胶囊（1-24/1-25：右置元素统一规格；链接文案保持「例外…」）；
    // 无例外 → 不显示（§2.5「未发生的量不显示」）
    st->privacy.privacy_pill->setText(tr("例外 %1").arg(paths.size()));
    st->privacy.privacy_pill->setVisible(!paths.isEmpty());
}

void PageMeta::set_tokens(const theme::Tokens &tokens) {
    MetaState *st = state_of(this);
    st->core.tokens = tokens;
    apply_style(st);
}

void PageMeta::set_map_provider(const QString &provider_id, const QString &amap_key, int cache_mb) {
    MetaState *st = state_of(this);
    // R-104（M7-WP7，UI 侧词表回落）：手改 INI 写出词表外 id 时下拉不再"落空但值保留" ——
    // 不在 provider_ids() 词表内的一律回落 "osm"（settings.cpp 的持久层词表回落属跨界文件，
    // 登记 handoff「跨界需求」）。
    const QStringList known = pp::map::provider_ids();
    const QString id = known.contains(provider_id) ? provider_id : QStringLiteral("osm");
    st->gps.map->set_provider(id);
    st->gps.map->set_amap_key(amap_key);
    st->gps.map->set_cache_mb(cache_mb);
    if (st->gps.provider != nullptr) {
        const QSignalBlocker blocker(st->gps.provider);
        const int index = st->gps.provider->findData(id);
        if (index >= 0) {
            st->gps.provider->setCurrentIndex(index);
        }
    }
    // R-045：换源清理两路共用 clear_search_state（此处信号被挡不走下拉回调 → 补一次清理，
    // 否则设置对话框换源会留旧钉/旧结果/联想 → "点不动/点错代"边缘态）。
    clear_search_state(st);
    refresh_layer_hint(st); // M6-T8：换源后图层支持性提示随之更新（信号被挡，这里补一次）
}

} // namespace pp::ui
