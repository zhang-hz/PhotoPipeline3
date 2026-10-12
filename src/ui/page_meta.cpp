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

// ---------------------------------------------------------------------------
// 页面状态（挂在 PageMeta 下的子 QObject）
// ---------------------------------------------------------------------------

// R-045（M7-WP7）：换源清理的**唯一**函数 —— 卡片下拉与 set_map_provider 两路共用
// （原只接在卡片下拉，设置对话框换源不清旧钉/旧结果/联想 → "点不动/点错代"边缘态）。
void clear_search_state(MetaState *st) {
    st->results.clear();
    st->result_row_by_key.clear();
    set_hook(st, "pp_meta_gps_results", 0);
    fill_result_rows(st); // 旧结果让位（新结果到位前不留陈旧行）
    st->map->clear_result_pins();
    st->suggest_list->clear();
    st->suggest_list->setVisible(false);
    set_hook(st, "pp_meta_gps_suggest", 0);
    st->map->run_suggest(QString()); // R-012：在途联想一并清算（世代推进 + abort）
}

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

// ---------------------------------------------------------------------------
// M6-T10 坐标基准透明化（§4.5 R4）：显示档（十进制度 ↔ 度分）+ 复制 + 边界精度提示。
//   显示 ≠ 写入：规则框（gps_lat/gps_lon）恒为十进制度（build_rules 只读规则框）；
//   本节只重排**只读生效值框**的文本与悬浮提示（DMS 切换不触发 center_on/规则信号）。
// ---------------------------------------------------------------------------

// 生效坐标框文本（**唯一**排版点）：十进制度（6 位小数 + °）或度分（pp::map::deg_to_dms 单源）
QString coord_field_text(const MetaState *st, double value, bool is_lat) {
    return st->dms_mode ? pp::map::deg_to_dms(value, is_lat) : coord_text(value, 6);
}

// 坐标框悬浮提示：**当前档之外的另一种读数**（十进制 → 度分；度分 → 十进制）+ 边界精度提示
// （粗盒边缘带才提示，§4.5）+ 时间戳（既有信息不丢）
QString coord_tooltip(const MetaState *st, const pp::GpsData &gps) {
    QString tip = st->dms_mode ? coord_text(gps.lat, 6) + QLatin1Char(' ') + coord_text(gps.lon, 6)
                               : dms_string(gps.lat, gps.lon);
    if (pp::map::coord_precision_suspect(gps.lat, gps.lon)) {
        tip += QStringLiteral("\n") + PageMeta::tr(kPrecisionSuspectText);
    }
    if (gps.timestamp.has_value()) {
        tip += QStringLiteral("\n%1: %2")
                   .arg(PageMeta::tr("时间戳"), QString::fromStdString(*gps.timestamp));
    }
    return tip;
}

// 坐标框重排（**唯一**落笔点）：生效值刷新与 DMS 切换都走这里 → 两种格式同源。
// 无生效坐标 → 两框清空（与既有空态一致）。
void apply_coord_fields(MetaState *st) {
    const auto set_view = [](QLineEdit *edit, const QString &text) {
        edit->setText(text);
        edit->setPlaceholderText(QString()); // 生效值框不承担状态（修 1-33）
    };
    if (!st->shown_gps.has_value()) {
        for (QLineEdit *edit : {st->view_lat, st->view_lon}) {
            set_view(edit, QString());
            edit->setToolTip(QString());
        }
        return;
    }
    const pp::GpsData &gps = *st->shown_gps;
    set_view(st->view_lat, coord_field_text(st, gps.lat, true));
    set_view(st->view_lon, coord_field_text(st, gps.lon, false));
    const QString tip = coord_tooltip(st, gps);
    st->view_lat->setToolTip(tip);
    st->view_lon->setToolTip(tip);
}

// 显示档切换（**唯一**入口：坐标行『DMS』切换钮 / 坐标框右键菜单）：只重排坐标框 + 同步
// 切换钮自身态；不动地图视口 / 胶囊 / 规则（纯显示层，且不改写入值）。
void set_dms_mode(MetaState *st, bool dms) {
    if (st->dms_mode == dms) {
        return;
    }
    st->dms_mode = dms;
    if (st->dms_toggle != nullptr) {
        const QSignalBlocker blocker(st->dms_toggle); // 程序化同步不回头触发 toggled
        st->dms_toggle->setChecked(dms);
    }
    set_hook(st, "pp_meta_gps_dms", dms);
    apply_coord_fields(st);
}

// 复制当前格式坐标（**只读显示值**：与坐标框所见逐字一致；不改规则/不写 EXIF）
void copy_coords(MetaState *st) {
    const QString lat_text = st->view_lat->text();
    const QString lon_text = st->view_lon->text();
    if (lat_text.isEmpty() || lon_text.isEmpty()) {
        st->gps_status->setText(PageMeta::tr("当前没有可复制的坐标"));
        return;
    }
    const QString text = lat_text + QStringLiteral(", ") + lon_text;
    QApplication::clipboard()->setText(text);
    st->gps_status->setText(PageMeta::tr("已复制坐标：%1").arg(text));
}

void refresh_gps_section(MetaState *st, const pp::EffectivePreview *preview) {
    const bool empty = st->items.isEmpty();
    const bool strip = preview != nullptr && preview->strip_privacy;
    const std::optional<pp::GpsData> effective =
        (preview != nullptr && !strip) ? preview->gps.effective : std::nullopt;
    const bool changed = preview != nullptr && preview->gps.changed;
    // M6-T10：缓存这一份生效坐标 —— DMS 切换据此重排（同一份数据，不重算 preview）
    st->shown_gps = effective;
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
        apply_coord_fields(st); // 纬度/经度：十进制度 ↔ 度分单源排版 + 悬浮提示（M6-T10）
        set_view(st->view_alt,
                 effective->altitude.has_value()
                     ? QStringLiteral("%1 m").arg(QString::number(*effective->altitude, 'f', 1))
                     : QString());
        set_view(st->view_dir, effective->direction.has_value()
                                   ? coord_text(*effective->direction, 1)
                                   : QString());
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
        // R-047（M7-WP7）：仅**生效坐标变化**时 center_on —— 改海拔/方位/时间戳等无关字段的
        // 刷新不再把用户正在浏览的地图拽回文件 GPS 点；首现/坐标改变才跟随。
        const std::pair<double, double> coords{effective->lat, effective->lon};
        const bool moved = !st->last_centered.has_value() ||
                           std::fabs(st->last_centered->first - coords.first) > 1e-9 ||
                           std::fabs(st->last_centered->second - coords.second) > 1e-9;
        if (moved) {
            st->last_centered = coords;
            st->map->center_on(effective->lat, effective->lon);
        }
        set_hook(st, "pp_meta_gps_effective",
                 QStringLiteral("%1,%2").arg(QString::number(effective->lat, 'f', 6),
                                             QString::number(effective->lon, 'f', 6)));
    } else {
        st->map->clear_marker();
        st->last_centered.reset();
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
    // UX-5：gps_enable（折叠面）与卡头 pp-meta-gps-enable 是**同一状态**（双向同步）——
    // 关 = 整卡不参与规则 → 规则编辑面/清除钮置灰；**取点面（地图/搜索/联想/取中心/读文件）
    // 保持可用**（选点/联想自动勾上是既有行为，关着开关也能取点再自动启用）。
    const bool rule_on = st->gps_enable->isChecked();
    const bool editable = rule_on && !locked;
    st->gps_enable->setEnabled(!locked);
    st->clear_gps->setEnabled(rule_on || locked);
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
    if (st->category_btn != nullptr) {
        st->category_btn->setEnabled(!locked); // 分类下拉也是搜索入口（与搜索钮同锁）
    }
    for (QAction *action : st->category_actions) {
        action->setEnabled(!locked); // 分类下拉也是搜索入口（与搜索钮同锁）
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
    refresh_privacy_inputs(st);
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
// UX-4①：结果框是**地图浮层**（默认隐藏）—— 有结果才显示；空态只留占位行并隐藏浮层。
// R-223：同步重建「稳定键 → 行号」映射（跨层对齐不再靠序号同代）。
void fill_result_rows(MetaState *st) {
    if (st->search_results == nullptr) {
        return;
    }
    st->search_results->clear();
    st->result_row_by_key.clear();
    if (st->results.empty()) {
        auto *empty =
            new QListWidgetItem(PageMeta::tr(kSearchResultPlaceholder), st->search_results);
        empty->setForeground(st->tokens.text3);
        empty->setFlags(Qt::ItemIsEnabled);
        st->search_results->setVisible(false); // UX-4①：无结果 = 浮层隐藏
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
        st->result_row_by_key.insert(pp::map::result_key(result.title, result.lat, result.lon),
                                     int(i));
    }
    st->search_results->setVisible(true); // UX-4①：有结果显示
}

// 分类下拉的互斥勾选由 QActionGroup 持有（UX-4②）；手输关键词时摘掉选中（摘除需临时关闭互斥）。
void clear_category_checks(MetaState *st) {
    QAction *checked =
        st->category_group != nullptr ? st->category_group->checkedAction() : nullptr;
    if (checked == nullptr) {
        return;
    }
    st->category_group->setExclusive(false);
    checked->setChecked(false);
    st->category_group->setExclusive(true);
}

// 搜索发射（**唯一**入口：搜索钮 / 回车 / 分类下拉）：先收联想与旧结果，再交给 map 异步搜。
void start_search(MetaState *st, bool from_category) {
    const QString query = st->search_edit->text().trimmed();
    if (query.isEmpty()) {
        st->gps_status->setText(PageMeta::tr("请输入搜索关键词"));
        return;
    }
    st->suggest_timer->stop(); // R-313：发射搜索即停防抖（旧查询不回马枪）
    if (!from_category) {
        clear_category_checks(st); // 手输关键词 = 与类目选择脱钩
    }
    clear_search_state(st); // R-045 同一清理面：旧结果/钉/联想一并收干净
    st->map->run_search(query);
}

// 选点（**唯一**入口：地图点击 / 地图选点 / 结果行 / 图钉 / 联想）：回填规则坐标 + 状态 +
// 触发逆地理回显（§4.5「选点后 gps_status 显示地址」）；主钉/居中由 refresh_effective 的
// 生效值管线收口（D4：只显生效值单钉）。
void apply_picked_point(MetaState *st, double lat, double lon, const QString &status) {
    st->loading = true;
    st->lat->setText(format_6(lat));
    st->lon->setText(format_6(lon));
    set_gps_rule_enabled(st, true); // UX-5：选点自动勾上（卡头开关与折叠面同一状态）
    st->loading = false;
    // M6-T10（§4.5）：粗盒边缘带（国境/港澳台边缘）坐标精度存疑 → 状态行显式提示
    // （盒内深处与境外不加句 —— 境外偏移恒 identity，两向一致，无"界内界外不一致"）
    st->gps_status->setText(pp::map::coord_precision_suspect(lat, lon)
                                ? status + QStringLiteral(" · ") +
                                      PageMeta::tr(kPrecisionSuspectText)
                                : status);
    commit_rule_change(st); // UX-4⑥：规则面四连刷收口
    // 地址回显先清（旧地址属于上一个点，不能挂在新点上）；结果由 reverse_finished 回填。
    st->address_value->set_full_text(QStringLiteral("—"));
    set_hook(st, "pp_meta_gps_address", QString());
    st->map->run_reverse(lat, lon);
}

// 结果行选中（行点击 / 回车激活）：结果键对齐（R-223）—— 按稳定键让 map 定位（不再按序号）。
// M6-T9 闭环接线②：map 侧 search_select_key 负责居中(≥12 档) + 主钉 + point_selected（回填/
// 逆地理由 apply_picked_point 收口）；本页只补两件与「点图钉」入口对齐的用户反馈 ——
// 列表当前行跟随 + 结果级状态文案（两条入口同一句话，闭环一路到底）。
void select_result(MetaState *st, int index) {
    if (index < 0 || index >= int(st->results.size())) {
        return; // 占位行 / 越界 = 不动作（§8 空态）
    }
    const pp::map::GeocodeResult &result = st->results[std::size_t(index)];
    st->map->search_select_key(pp::map::result_key(result.title, result.lat, result.lon));
    st->search_results->setCurrentRow(index);
    st->search_results->setVisible(false); // UX-4①：选中即收起浮层
    st->gps_status->setText(PageMeta::tr("已选结果：%1").arg(result.title));
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

// ---------------------------------------------------------------------------
// UX-4（M7-WP7，ux-review-14.md §4 目标版面）：GPS 卡地图 App 化 —— build_gps_card 按
// 子区拆函数（R-155/E2-B5 的 GPS 段）：coords / ops / search / map / rule 五个子 builder +
// gps_wiring 统一接线；规则面变更收口 = commit_rule_change。版面要点：
//   ① 结果框/联想改**地图浮层**（默认隐藏，有结果显示，选中/清空/换源/失焦隐藏）；
//   ② 分类 chips 行删除 → 搜索框右侧「分类 ▾」下拉（6 类目 action 继承 pp-meta-gps-category-N）；
//   ③ 图层控件浮到地图左上角（半透明浮层，overlay 令牌）；
//   ④ 地址行删除（地址进 gps_status tooltip + 右键「复制地址」；pp-meta-gps-address 与
//      pp_meta_gps_address 钩子保留）；
//   ⑤ 地图 136 → 260（stretch 吃满腾出的空间）。
// ---------------------------------------------------------------------------

// 纬度/经度 + 坐标显示行（WGS-84 标签 / DMS 切换 / 复制）+ 海拔/方位（生效值只读）
void build_gps_coord_section(MetaState *st, QWidget *body, QVBoxLayout *bv) {
    // 生效坐标（只读；§5.2「GPS 卡显示生效坐标」）+ 海拔/方位（双列轨，fill）——§7.1 的
    // 控件集合 = 纬度/经度｜海拔/方位｜地图｜操作（生效时间戳读数进坐标行 tooltip，信息不丢）
    st->view_lat = make_view_field(body, QStringLiteral("pp-meta-gps-lat"));
    st->view_lon = make_view_field(body, QStringLiteral("pp-meta-gps-lon"));
    st->view_alt = make_view_field(body, QStringLiteral("pp-meta-gps-alt"));
    st->view_dir = make_view_field(body, QStringLiteral("pp-meta-gps-dir"));
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
    // 坐标显示行（M6-T10/§4.5 R4，紧贴经纬度行）：[ⓘ WGS-84（标准 GPS）] ……[DMS][复制]
    //   —— 基准面标签（tooltip 说明火星偏移已抵消）+ 显示档切换 + 复制；**只显示/只复制**，
    //   写入值一律十进制度（规则框在下方折叠面里，与本行无关）。
    {
        TrackGroup group = make_group(body);
        st->datum_icon = new QLabel(group.widget());
        st->datum_icon->setObjectName(QStringLiteral("pp-meta-gps-datum-icon"));
        st->datum_icon->setFixedSize(kDatumIconPx, kDatumIconPx);
        st->datum_icon->setAccessibleName(PageMeta::tr("WGS-84"));
        group.add(st->datum_icon);
        group.gap(formkit::kInlineGap);
        st->datum_label = new QLabel(PageMeta::tr("WGS-84（标准 GPS）"), group.widget());
        st->datum_label->setObjectName(QStringLiteral("pp-meta-gps-datum"));
        st->datum_label->setProperty(kFieldProperty, QStringLiteral("sub"));
        st->datum_label->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        st->datum_label->setToolTip(PageMeta::tr(kDatumTooltipText));
        group.add(st->datum_label);
        group.stretch();
        st->dms_toggle =
            make_chip(group.widget(), QStringLiteral("pp-meta-gps-dms"), PageMeta::tr("DMS"));
        st->dms_toggle->setToolTip(PageMeta::tr(kDmsToggleTooltipText));
        group.add(st->dms_toggle);
        group.gap(theme::Metrics::group_gap);
        st->copy_coords =
            make_link(group.widget(), QStringLiteral("pp-meta-gps-copy"), PageMeta::tr("复制"));
        st->copy_coords->setToolTip(PageMeta::tr(kCopyCoordsTooltipText));
        group.add(st->copy_coords);
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("坐标"), group.widget(), FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-meta-gps-datum-row"));
        st->datum_row = row.row; // M6-T12：光标坐标读数的 tooltip 面（见 cursor_moved 接线）
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
}

// 操作行（修 1-37：**固定在地图上方**，核心动作不再落进内滚盲区；按钮高 26 = A7）
void build_gps_ops_row(MetaState *st, QWidget *body, QVBoxLayout *bv) {
    TrackGroup group = make_group(body);
    st->read_gps =
        make_button(group.widget(), QStringLiteral("pp-meta-gps-read"), PageMeta::tr("从文件读取"));
    st->read_gps->setFont(theme::font(theme::Typography::body_px, 600));
    st->read_gps->setEnabled(false);
    st->pick_map =
        make_button(group.widget(), QStringLiteral("pp-meta-gps-pick"), PageMeta::tr("地图选点"));
    st->pick_map->setFont(theme::font(theme::Typography::body_px, 600));
    st->pick_map->setToolTip(PageMeta::tr("取地图中心为坐标（可先拖拽地图定位）"));
    st->clear_gps = new QPushButton(PageMeta::tr("清除 GPS"), group.widget());
    st->clear_gps->setObjectName(QStringLiteral("pp-meta-gps-clear"));
    st->clear_gps->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkLinkDanger));
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

// 地图源下拉（§7.1「OSM 下拉」；有标签；切换 = 本卡地图的显示源。UX-4 只删三个 -row：
// category/layer/address；provider 行保留）
void build_gps_provider_row(MetaState *st, QWidget *body, QVBoxLayout *bv) {
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

// 地点搜索行（UX-4②：[输入][分类 ▾][搜索] —— 6 类目从一整行 chips 收进搜索框右侧下拉）
void build_gps_search_row(MetaState *st, QWidget *body, QVBoxLayout *bv) {
    TrackGroup group = make_group(body);
    st->search_edit = make_rule_field(group.widget(), QStringLiteral("pp-meta-gps-search-edit"));
    st->search_edit->setPlaceholderText(PageMeta::tr(kSearchPlaceholder));
    group.add(st->search_edit, 1);
    group.gap(theme::Metrics::group_gap);
    // 「分类 ▾」下拉按钮：6 个类目 action（objectName pp-meta-gps-category-0..5 保留给 action；
    // 探针断言菜单项数 == 6）。点选 = 回填关键词 + 按类目 run_search（与原 chips 同链路）。
    st->category_btn = make_button(group.widget(), QStringLiteral("pp-meta-gps-category-menu"),
                                   PageMeta::tr(kCategoryButtonText));
    st->category_btn->setToolTip(PageMeta::tr(kCategoryButtonTooltip));
    st->category_menu = new QMenu(st->category_btn);
    st->category_group = new QActionGroup(st->category_btn);
    st->category_group->setExclusive(true);
    for (int i = 0; i < int(kCategoryKeywords.size()); ++i) {
        const QString keyword = PageMeta::tr(kCategoryKeywords[std::size_t(i)]);
        QAction *action = st->category_menu->addAction(keyword);
        action->setObjectName(QStringLiteral("pp-meta-gps-category-%1").arg(i));
        action->setCheckable(true);
        action->setToolTip(PageMeta::tr("按类目搜索：%1").arg(keyword));
        st->category_group->addAction(action);
        st->category_actions.append(action);
        QObject::connect(action, &QAction::triggered, st, [st, keyword] {
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
    st->category_btn->setMenu(st->category_menu); // 整键弹出（探针经 menu()->actions() 取证）
    group.add(st->category_btn);
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

// 地图块（UX-4③⑤）：260px 高 + 覆盖层 —— 图层浮层（左上，半透明底板）/ 展开钮（右上）/
// 联想与结果浮层（默认隐藏）/ 加载 chip（R-315）/ 光标读数 + 缩放钮（底部，避开左下比例尺）。
void build_gps_map_section(MetaState *st, QWidget *body, QVBoxLayout *bv) {
    TrackGroup group = make_group(body);
    st->map = new pp::map::MapWidget(group.widget());
    st->map->setObjectName(QStringLiteral("pp-meta-map"));
    // UX-4 stretch（M7-R2-B，主对话授权）：260px **起步**（§16-2 定档下限）+ 卡内拉伸吃富余空间
    // （ux-review §4 改进方案 5：「地图改 stretch=1 吃掉腾出的空间」）；不引入卡内滚动（页面
    // QScrollArea 是唯一滚动面）。原 setFixedHeight(260) 把富余高度挡在卡外（stretch 只是设计
    // 稿语句）—— 现 min 260 + 纵向 Expanding，行/卡 stretch=1 逐级把富余喂给地图。
    st->map->setMinimumHeight(kMapHeight);
    st->map->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    group.add(st->map, 1);
    st->map_host = group.widget(); // M6-T13：重挂载的原位（挂回时同一宿主 + 同一行槽）
    st->map_line = group.line;
    // 覆盖层 = map 的子布局（不进卡内行节奏；控件是 map 的子件 → 重挂载时随 map 一并进
    // 对话框，无需重建）。
    auto *overlay = new QVBoxLayout(st->map);
    overlay->setContentsMargins(6, 4, 6, 4);
    overlay->setSpacing(formkit::kInlineGap);
    // 顶行：图层浮层（左）……「展开」钮（右）
    auto *top_line = new QHBoxLayout();
    top_line->setContentsMargins(0, 0, 0, 0);
    top_line->setSpacing(formkit::kInlineGap);
    // UX-4③：图层控件浮到地图左上角 —— 半透明浮层底板（公共 overlay 令牌 hud_bg，与
    // mapwidget 自绘 HUD 同口径；不随主题变）。
    st->layer_plate = new QWidget(st->map);
    st->layer_plate->setObjectName(QStringLiteral("pp-meta-map-layer-plate"));
    st->layer_plate->setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2; "
                                                  "border-radius: 6px;")
                                       .arg(QString::fromLatin1(theme::overlay_tokens().hud_bg),
                                            QStringLiteral("rgba(0,0,0,0.12)")));
    auto *plate_line = new QHBoxLayout(st->layer_plate);
    plate_line->setContentsMargins(4, 3, 4, 3);
    plate_line->setSpacing(0);
    st->layer_seg = new formkit::SegmentedControl(st->layer_plate);
    st->layer_seg->setObjectName(QStringLiteral("pp-meta-gps-layer"));
    st->layer_seg->add_segment(PageMeta::tr("矢量"), QStringLiteral("pp-meta-gps-layer-road"));
    st->layer_seg->add_segment(PageMeta::tr("卫星"), QStringLiteral("pp-meta-gps-layer-satellite"));
    st->layer_seg->add_segment(PageMeta::tr("混合"), QStringLiteral("pp-meta-gps-layer-hybrid"));
    plate_line->addWidget(st->layer_seg);
    top_line->addWidget(st->layer_plate, 0, Qt::AlignLeft | Qt::AlignTop);
    top_line->addStretch(1);
    st->map_expand = make_map_hud_button(st->map, QStringLiteral("pp-meta-map-expand"),
                                         icons::Name::ZoomFit, PageMeta::tr(kMapExpandText));
    st->map_expand->setToolTip(PageMeta::tr(kMapExpandTooltipText));
    top_line->addWidget(st->map_expand, 0, Qt::AlignRight | Qt::AlignTop);
    overlay->addLayout(top_line);
    // UX-4①：联想下拉 + 搜索结果 = **地图浮层**（默认隐藏；有结果显示；选中/清空/换源/失焦
    // 隐藏）。objectName 保留（pp-meta-gps-suggest / pp-meta-gps-search-results /
    // pp-meta-gps-results-host）。
    st->suggest_list = new QListWidget(st->map);
    st->suggest_list->setObjectName(QStringLiteral("pp-meta-gps-suggest"));
    st->suggest_list->setAccessibleName(PageMeta::tr("搜索联想"));
    st->suggest_list->setToolTip(PageMeta::tr("选择联想项：地图定位到该地点"));
    st->suggest_list->setFixedHeight(kSuggestVisibleRows * formkit::kChipH + 6);
    st->suggest_list->setSelectionMode(QAbstractItemView::SingleSelection);
    st->suggest_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    st->suggest_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    st->suggest_list->setTextElideMode(Qt::ElideRight);
    st->suggest_list->setVisible(false);
    overlay->addWidget(st->suggest_list);
    {
        auto *host = new QWidget(st->map);
        host->setObjectName(QStringLiteral("pp-meta-gps-results-host"));
        auto *line = new QHBoxLayout(host);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(0);
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
        st->search_results->setVisible(false); // UX-4①：默认隐藏
        line->addWidget(st->search_results, 1);
        overlay->addWidget(host);
    }
    // R-315：瓦片加载反馈 chip（tiles_pending_changed → 显示/隐藏）
    st->map_loading = new QLabel(PageMeta::tr(kMapLoadingText), st->map);
    st->map_loading->setObjectName(QStringLiteral("pp-meta-map-loading"));
    st->map_loading->setProperty(kFieldProperty, QStringLiteral("map-hud"));
    st->map_loading->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    st->map_loading->setToolTip(PageMeta::tr("正在加载地图瓦片"));
    st->map_loading->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    st->map_loading->setVisible(false);
    overlay->addWidget(st->map_loading, 0, Qt::AlignLeft);
    overlay->addStretch(1);
    // 底行：光标读数 + 「－/＋」缩放钮（右下；左下留给 mapwidget 自绘比例尺）
    auto *bottom_line = new QHBoxLayout();
    bottom_line->setContentsMargins(0, 0, 0, 0);
    bottom_line->setSpacing(formkit::kInlineGap);
    st->map_cursor = new QLabel(PageMeta::tr(kMapCursorIdleText), st->map);
    st->map_cursor->setObjectName(QStringLiteral("pp-meta-map-cursor"));
    st->map_cursor->setProperty(kFieldProperty, QStringLiteral("map-hud"));
    st->map_cursor->setFont(theme::font(theme::Typography::hint_px, QFont::Normal, true));
    st->map_cursor->setToolTip(PageMeta::tr(kMapCursorTooltipText));
    st->map_cursor->setAttribute(Qt::WA_TransparentForMouseEvents, true); // 不吃地图鼠标事件
    bottom_line->addStretch(1);
    bottom_line->addWidget(st->map_cursor, 0, Qt::AlignRight | Qt::AlignBottom);
    st->zoom_out = make_map_hud_button(st->map, QStringLiteral("pp-meta-map-zoom-out"),
                                       icons::Name::ZoomOut, PageMeta::tr(kMapZoomOutText));
    st->zoom_in = make_map_hud_button(st->map, QStringLiteral("pp-meta-map-zoom-in"),
                                      icons::Name::ZoomIn, PageMeta::tr(kMapZoomInText));
    bottom_line->addWidget(st->zoom_out);
    bottom_line->addWidget(st->zoom_in);
    overlay->addLayout(bottom_line);
    // UX-4 stretch（M7-R2-B）：地图行 stretch=1 —— 卡体富余高全归地图（不吃行节奏）
    bv->addWidget(group.widget(), 1);
}

// 批量 GPS 规则折叠面（修 1-37：纯折叠行，默认折叠；内为规则编辑面）
void build_gps_rule_section(MetaState *st, QWidget *card, QVBoxLayout *v) {
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
    st->rule_altdir_row = make_rule_grid_pair(PageMeta::tr("海拔"), st->alt, QStringLiteral("m"),
                                              PageMeta::tr("方位"), st->dir, QStringLiteral("°"));
    st->rule_ts_row =
        formkit::frow(rule_body, PageMeta::tr("时间戳"), st->ts, FieldWidth::Flex).row;
    rule_v->addWidget(st->rule_altdir_row);
    rule_v->addWidget(st->rule_ts_row);
    for (QWidget *hidden : {st->rule_altdir_row, st->rule_ts_row}) {
        hidden->setVisible(false);
    }
    st->rule_host->setVisible(false);
    v->addWidget(st->rule_host);
}

// 统一接线（UX-4⑥ gps_wiring）：全部信号链一处收口；规则面变更走 commit_rule_change。
void gps_wiring(MetaState *st) {
    // 换源（UX-4①/R-045）：清旧钉/旧结果/联想走 clear_search_state 唯一清理面
    QObject::connect(st->provider, QOverload<int>::of(&QComboBox::currentIndexChanged), st,
                     [st](int index) {
                         const QString id = st->provider->itemData(index).toString();
                         if (!id.isEmpty()) {
                             st->map->set_provider(id);
                         }
                         refresh_layer_hint(st); // 换源 → 图层支持性提示随之更新（§5.2）
                         clear_search_state(st); // R-045：换源清理（卡片下拉/设置对话框同一函数）
                     });
    for (QLineEdit *edit : {st->lat, st->lon}) {
        QObject::connect(edit, &QLineEdit::textChanged, st, [st](const QString &) {
            if (st->loading) {
                return;
            }
            commit_rule_change(st);
        });
    }
    for (QLineEdit *edit : {st->alt, st->dir, st->ts}) {
        QObject::connect(edit, &QLineEdit::textChanged, st, [st](const QString &) {
            if (st->loading) {
                return;
            }
            commit_rule_change(st);
        });
    }
    // UX-5：卡头开关与折叠面 gps_enable = 同一状态的两个入口（双向同步；选点/联想自动勾上）
    const auto on_gps_enable_toggled = [st](bool on) {
        if (st->loading) {
            return;
        }
        set_gps_rule_enabled(st, on);
        commit_rule_change(st);
    };
    QObject::connect(st->gps_enable, &formkit::CheckBox::toggled, st, on_gps_enable_toggled);
    QObject::connect(st->gps_enable_switch, &formkit::SwitchControl::toggled, st,
                     on_gps_enable_toggled);
    QObject::connect(st->clear_gps, &QPushButton::toggled, st, [st](bool) {
        if (st->loading) {
            return;
        }
        commit_rule_change(st);
    });
    QObject::connect(st->more_toggle, &QToolButton::toggled, st, [st](bool on) {
        set_fold_icon(st, st->more_toggle, on); // 插入符 = 矢量图标（C3）
        st->rule_altdir_row->setVisible(on);
        st->rule_ts_row->setVisible(on);
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
        set_gps_rule_enabled(st, true); // UX-5：读取即启用（同一状态双入口）
        st->loading = false;
        st->gps_status->setText(PageMeta::tr("已从选中文件读取 GPS"));
        commit_rule_change(st);
    });

    // 地图选点：取视口中心为规则坐标（点击地图仍直接取点 —— v0.2 行为不变；M6-T8 起叠加地址回显）
    QObject::connect(st->pick_map, &QPushButton::clicked, st, [st] {
        apply_picked_point(st, st->map->center_lat(), st->map->center_lon(),
                           PageMeta::tr("已取地图中心为坐标"));
    });

    // 搜索（M6-T8）：搜索钮 / 回车 → run_search；输入防抖 200ms → run_suggest（联想浮层）
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
            st->map->run_suggest(QString()); // R-012：空查询也清算在途联想（世代推进 + abort）
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
                         st->suggest_list->setVisible(true); // UX-4①：有结果显示
                         set_hook(st, "pp_meta_gps_suggest", int(items.size()));
                     });
    QObject::connect(st->suggest_list, &QListWidget::itemClicked, st, [st](QListWidgetItem *item) {
        if (item == nullptr) {
            return;
        }
        st->suggest_timer->stop(); // R-313：点选即停防抖（旧查询不"回马枪"弹回联想框）
        st->suggest_list->setVisible(false);
        set_hook(st, "pp_meta_gps_suggest", 0);
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
    // 结果列表（M6-T8）：数据源 = search_results_ready（与 map 内部 results 同一批数据；
    // R-223：行/钉对齐按稳定键，不按序号）
    QObject::connect(
        st->map, &pp::map::MapWidget::search_results_ready, st,
        [st](const std::vector<pp::map::GeocodeResult> &results, const QString &error) {
            st->results = results;
            set_hook(st, "pp_meta_gps_results", int(results.size()));
            fill_result_rows(st); // UX-4①：有结果显示浮层 / 空态隐藏
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
    // 图钉命中（R-223 稳定键对齐）：按 pin_selected(key) 找行（pin_clicked(index) 的冻结面
    // 仍由 mapwidget 并发发射，供探针/旧消费方使用）—— 点钉不算「取点」。
    QObject::connect(st->map, &pp::map::MapWidget::pin_selected, st, [st](const QString &key) {
        const int index = st->result_row_by_key.value(key, -1);
        if (index < 0 || index >= int(st->results.size())) {
            return;
        }
        const pp::map::GeocodeResult &result = st->results[std::size_t(index)];
        st->search_results->setCurrentRow(index); // 列表跟随命中钉（不触发 search_select）
        st->search_results->setVisible(false);    // UX-4①：选中即收起浮层
        apply_picked_point(st, result.lat, result.lon,
                           PageMeta::tr("已选结果：%1").arg(result.title));
    });
    // 逆地理回显（M6-T8/§4.5 + UX-4④）：地址进 gps_status tooltip + 右键「复制地址」；
    // 视觉只留「已选点 · …」一行（pp-meta-gps-address 保留为隐藏承载 + 钩子不变）。
    QObject::connect(st->map, &pp::map::MapWidget::reverse_finished, st,
                     [st](const QString &address, const QString &error) {
                         const QString text = error.isEmpty() ? address : error;
                         st->address_value->set_full_text(text.isEmpty() ? QStringLiteral("—")
                                                                         : text);
                         set_hook(st, "pp_meta_gps_address", text);
                         st->gps_status->setToolTip(text.isEmpty() ? QString() : text);
                         if (error.isEmpty() && !address.isEmpty()) {
                             st->gps_status->setText(PageMeta::tr("已选点 · %1").arg(address));
                         }
                     });
    QObject::connect(st->map, &pp::map::MapWidget::point_selected, st,
                     [st](double lat, double lon) {
                         apply_picked_point(st, lat, lon, PageMeta::tr("已从地图选择坐标"));
                         // UX-4①「失焦隐藏」：地图上取点 = 离开搜索浮层
                         st->search_results->setVisible(false);
                         st->suggest_list->setVisible(false);
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
    // M6-T12（§5.3/§7-T12 体验件）：缩放钮 → map->zoom_in/zoom_out（视口中心锚点，中心不动）
    QObject::connect(st->zoom_in, &QToolButton::clicked, st, [st] { st->map->zoom_in(); });
    QObject::connect(st->zoom_out, &QToolButton::clicked, st, [st] { st->map->zoom_out(); });
    // M6-T13（§7-T13 R5）：展开钮 → 大地图对话框（同一 map 控件重挂载；模态，关闭即挂回）
    QObject::connect(st->map_expand, &QToolButton::clicked, st,
                     [st] { open_map_expanded_view(st); });
    // 光标坐标读数（接 map 的 cursor_moved，WGS-84、约 100ms 节流）：地图覆盖层 chip 显示
    // 「光标: lat, lon」；同一读数另挂坐标行 tooltip（§2.6：细节进 tooltip）—— 只读回显，
    // 不写规则框、不动 gps_status（状态行留给选点/搜索结果等语义消息）。
    QObject::connect(st->map, &pp::map::MapWidget::cursor_moved, st, [st](double lat, double lon) {
        const QString text = PageMeta::tr("光标: %1, %2").arg(format_6(lat), format_6(lon));
        st->map_cursor->setText(text);
        if (st->datum_row != nullptr) {
            st->datum_row->setToolTip(text);
        }
    });
    // R-315：瓦片加载有 UI 反馈（tiles_pending_changed → 「加载中…」chip；0 = 隐藏）。
    // search_finished **不接**：它与 search_results_ready 同一批数据（页面只消费后者，
    // 双接会把状态行刷两遍）—— 登记不接理由于 handoff。
    QObject::connect(st->map, &pp::map::MapWidget::tiles_pending_changed, st,
                     [st](int pending) { st->map_loading->setVisible(pending > 0); });
    // M6-T10（§4.5 R4）：显示档切换钮 + 复制链 + 坐标框右键菜单 —— 三条入口同一收口
    // （set_dms_mode / copy_coords）；右键菜单同时承担"复制"（只读框的默认菜单不可用）
    QObject::connect(st->dms_toggle, &QAbstractButton::toggled, st,
                     [st](bool on) { set_dms_mode(st, on); });
    QObject::connect(st->copy_coords, &QToolButton::clicked, st, [st] { copy_coords(st); });
    for (QLineEdit *edit : {st->view_lat, st->view_lon}) {
        edit->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(edit, &QLineEdit::customContextMenuRequested, st,
                         [st, edit](const QPoint &pos) {
                             QMenu menu(edit);
                             QAction *copy = menu.addAction(PageMeta::tr("复制坐标"));
                             QAction *dms = menu.addAction(PageMeta::tr("度分显示（DMS）"));
                             dms->setCheckable(true);
                             dms->setChecked(st->dms_mode);
                             const QAction *chosen = menu.exec(edit->mapToGlobal(pos));
                             if (chosen == copy) {
                                 copy_coords(st);
                             } else if (chosen == dms) {
                                 set_dms_mode(st, dms->isChecked()); // 菜单已翻转勾选态
                             }
                         });
    }
    // UX-4④：gps_status 右键「复制地址」（地址行删除后的唯一复制入口；tooltip 已挂地址）
    st->gps_status->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(st->gps_status, &QLabel::customContextMenuRequested, st,
                     [st](const QPoint &pos) {
                         const QString address = st->address_value->full_text();
                         if (address.isEmpty() || address == QStringLiteral("—")) {
                             return;
                         }
                         QMenu menu(st->gps_status);
                         QAction *copy = menu.addAction(PageMeta::tr(kCopyAddressText));
                         if (menu.exec(st->gps_status->mapToGlobal(pos)) == copy) {
                             QApplication::clipboard()->setText(address);
                             st->gps_status->setText(PageMeta::tr("已复制地址"));
                         }
                     });
    // 初始态（Road = map 的默认图层）+ 自验钩子读数
    refresh_layer_hint(st);
    set_hook(st, "pp_meta_gps_layer", QString::fromLatin1(kLayerStyleKeys[0]));
    set_hook(st, "pp_meta_gps_results", 0);
    set_hook(st, "pp_meta_gps_suggest", 0);
    set_hook(st, "pp_meta_gps_address", QString());
    set_hook(st, "pp_meta_gps_dms", false); // M6-T10：显示档（默认十进制度）
    fill_result_rows(st);                   // 空态 = 占位行（"搜索后在此选择"）+ 浮层隐藏
}

QWidget *build_gps_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    const CardHeader header =
        make_header(st, card, PageMeta::tr("GPS"), QStringLiteral("pp-meta-gps-pill"),
                    QStringLiteral("pp-meta-gps-hint"),
                    QStringLiteral("pp-meta-gps-enable")); // UX-5：卡头「启用」开关
    st->gps_pill = header.pill;
    st->gps_hint = header.hint;
    st->gps_hint->set_full_text(PageMeta::tr("跟随选中 · WGS-84"));
    st->gps_enable_switch = header.enable;
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

    build_gps_coord_section(st, body, bv);
    // UX-4④：地址行（pp-meta-gps-address-row）删除 —— 但 pp-meta-gps-address 与
    // pp_meta_gps_address 钩子保留：**隐藏承载 widget**（文本 = 逆地理回显，供 gps_status
    // tooltip 与右键「复制地址」取值；隐藏不占行节奏）。
    st->address_value = new ElidedLabel(body);
    st->address_value->setObjectName(QStringLiteral("pp-meta-gps-address"));
    st->address_value->setProperty(kFieldProperty, QStringLiteral("address"));
    st->address_value->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
    st->address_value->set_full_text(QStringLiteral("—"));
    st->address_value->setVisible(false);
    bv->addWidget(st->address_value);
    build_gps_ops_row(st, body, bv);
    build_gps_provider_row(st, body, bv);
    build_gps_search_row(st, body, bv);
    build_gps_map_section(st, body, bv);
    v->addWidget(body);
    st->gps_status = new QLabel(card);
    st->gps_status->setObjectName(QStringLiteral("gps_status"));
    st->gps_status->setProperty(kFieldProperty, QStringLiteral("sub"));
    st->gps_status->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    st->gps_status->setContentsMargins(theme::Metrics::card_pad_x, 4, theme::Metrics::card_pad_x,
                                       4);
    v->addWidget(st->gps_status);

    build_gps_rule_section(st, card, v);
    gps_wiring(st);
    return card;
}

contextmenu::GpsCardIntents gps_card_intents(MetaState *st) {
    contextmenu::GpsCardIntents in;
    in.running = st->page == nullptr || !st->page->isEnabled();
    in.enabled = st->gps_enable_switch != nullptr && st->gps_enable_switch->isChecked();
    in.enable_toggle = [st] {
        if (st->gps_enable_switch != nullptr)
            st->gps_enable_switch->toggle();
    };
    in.read_from_file = [st] {
        if (st->read_gps != nullptr)
            st->read_gps->click();
    };
    in.pick_on_map = [st] { open_map_expanded_view(st); };
    in.take_map_center = [st] {
        if (st->pick_map != nullptr)
            st->pick_map->click();
    };
    in.copy_coords = [st] {
        if (st->copy_coords != nullptr)
            st->copy_coords->click();
    };
    in.clear_gps = [st] {
        if (st->clear_gps != nullptr)
            st->clear_gps->click();
    };
    return in;
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
