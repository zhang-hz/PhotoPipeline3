// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页「GPS」卡（B.1 拆分步 B5：meta_card_gps）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（步序 B5；build_gps_card 7 子 builder +
// gps_wiring 独立成段，风险最大点单 TU 内聚）；契约面见 meta_state.h。
// 内容 = 坐标基准显示（DMS/复制/边界提示）+ 搜索区（联想/分类/结果/选点闭环）+ 刷新族
//        + 7 子 builder + gps_wiring + gps_card_intents
// —— 自 page_meta.cpp 逐字迁出（M7-R2.1-F2；行为零变化）。
#include "ui/meta_probe_context.h" // R-182/R-258：钩子/词表单源
#include "ui/meta_state.h"

#include "mapwidget/dms.h"
#include "mapwidget/geocoder.h"
#include "mapwidget/layers.h"
#include "mapwidget/mapwidget.h"
#include "mapwidget/providers.h"
#include "ui/contextmenu.h"
#include "ui/formkit.h"
#include "ui/icons.h"
#include "ui/meta_format.h"
#include "ui/meta_rules.h"
#include "ui/page_meta.h"
#include "ui/theme.h"

#include <QActionGroup>
#include <QClipboard>
#include <QComboBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pp::ui::meta {

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 页面状态（挂在 PageMeta 下的子 QObject）
// ---------------------------------------------------------------------------

// R-045（M7-WP7）：换源清理的**唯一**函数 —— 卡片下拉与 set_map_provider 两路共用
// （原只接在卡片下拉，设置对话框换源不清旧钉/旧结果/联想 → "点不动/点错代"边缘态）。
void clear_search_state(MetaState *st) {
    st->gps.results.clear();
    st->gps.result_row_by_key.clear();
    set_hook(st, hook::kGpsResults, 0);
    fill_result_rows(st); // 旧结果让位（新结果到位前不留陈旧行）
    st->gps.map->clear_result_pins();
    st->gps.suggest_list->clear();
    st->gps.suggest_list->setVisible(false);
    set_hook(st, hook::kGpsSuggest, 0);
    st->gps.map->run_suggest(QString()); // R-012：在途联想一并清算（世代推进 + abort）
}

// ---------------------------------------------------------------------------
// M6-T10 坐标基准透明化（§4.5 R4）：显示档（十进制度 ↔ 度分）+ 复制 + 边界精度提示。
//   显示 ≠ 写入：规则框（gps_lat/gps_lon）恒为十进制度（build_rules 只读规则框）；
//   本节只重排**只读生效值框**的文本与悬浮提示（DMS 切换不触发 center_on/规则信号）。
// ---------------------------------------------------------------------------

// 生效坐标框文本（**唯一**排版点）：十进制度（6 位小数 + °）或度分（pp::map::deg_to_dms 单源）
QString coord_field_text(const MetaState *st, double value, bool is_lat) {
    return st->gps.dms_mode ? pp::map::deg_to_dms(value, is_lat) : coord_text(value, 6);
}

// 坐标框悬浮提示：**当前档之外的另一种读数**（十进制 → 度分；度分 → 十进制）+ 边界精度提示
// （粗盒边缘带才提示，§4.5）+ 时间戳（既有信息不丢）
QString coord_tooltip(const MetaState *st, const pp::GpsData &gps) {
    QString tip = st->gps.dms_mode
                      ? coord_text(gps.lat, 6) + QLatin1Char(' ') + coord_text(gps.lon, 6)
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
    if (!st->gps.shown_gps.has_value()) {
        for (QLineEdit *edit : {st->gps.view_lat, st->gps.view_lon}) {
            set_view(edit, QString());
            edit->setToolTip(QString());
        }
        return;
    }
    const pp::GpsData &gps = *st->gps.shown_gps;
    set_view(st->gps.view_lat, coord_field_text(st, gps.lat, true));
    set_view(st->gps.view_lon, coord_field_text(st, gps.lon, false));
    const QString tip = coord_tooltip(st, gps);
    st->gps.view_lat->setToolTip(tip);
    st->gps.view_lon->setToolTip(tip);
}

// 显示档切换（**唯一**入口：坐标行『DMS』切换钮 / 坐标框右键菜单）：只重排坐标框 + 同步
// 切换钮自身态；不动地图视口 / 胶囊 / 规则（纯显示层，且不改写入值）。
void set_dms_mode(MetaState *st, bool dms) {
    if (st->gps.dms_mode == dms) {
        return;
    }
    st->gps.dms_mode = dms;
    if (st->gps.dms_toggle != nullptr) {
        const QSignalBlocker blocker(st->gps.dms_toggle); // 程序化同步不回头触发 toggled
        st->gps.dms_toggle->setChecked(dms);
    }
    set_hook(st, hook::kGpsDms, dms);
    apply_coord_fields(st);
}

// 复制当前格式坐标（**只读显示值**：与坐标框所见逐字一致；不改规则/不写 EXIF）
void copy_coords(MetaState *st) {
    const QString lat_text = st->gps.view_lat->text();
    const QString lon_text = st->gps.view_lon->text();
    if (lat_text.isEmpty() || lon_text.isEmpty()) {
        st->gps.gps_status->setText(PageMeta::tr("当前没有可复制的坐标"));
        return;
    }
    const QString text = lat_text + QStringLiteral(", ") + lon_text;
    QApplication::clipboard()->setText(text);
    st->gps.gps_status->setText(PageMeta::tr("已复制坐标：%1").arg(text));
}

void refresh_gps_section(MetaState *st, const pp::EffectivePreview *preview) {
    const bool empty = st->follow.items.isEmpty();
    const bool strip = preview != nullptr && preview->strip_privacy;
    const std::optional<pp::GpsData> effective =
        (preview != nullptr && !strip) ? preview->gps.effective : std::nullopt;
    const bool changed = preview != nullptr && preview->gps.changed;
    // M6-T10：缓存这一份生效坐标 —— DMS 切换据此重排（同一份数据，不重算 preview）
    st->gps.shown_gps = effective;
    const auto set_view = [](QLineEdit *edit, const QString &text) {
        edit->setText(text);
        edit->setPlaceholderText(QString()); // 生效值框不承担状态（修 1-33）
    };
    if (empty || preview == nullptr) {
        for (QLineEdit *edit :
             {st->gps.view_lat, st->gps.view_lon, st->gps.view_alt, st->gps.view_dir}) {
            set_view(edit, QString());
            edit->setToolTip(QString());
        }
        theme::set_mod(st->gps.view_lat, false);
        theme::set_mod(st->gps.view_lon, false);
        st->gps.gps_pill->setVisible(false);
        st->gps.map->clear_marker();
        set_hook(st, hook::kGpsEffective, QString());
        return;
    }
    if (effective.has_value()) {
        apply_coord_fields(st); // 纬度/经度：十进制度 ↔ 度分单源排版 + 悬浮提示（M6-T10）
        set_view(st->gps.view_alt,
                 effective->altitude.has_value()
                     ? QStringLiteral("%1 m").arg(QString::number(*effective->altitude, 'f', 1))
                     : QString());
        set_view(st->gps.view_dir, effective->direction.has_value()
                                       ? coord_text(*effective->direction, 1)
                                       : QString());
    } else {
        for (QLineEdit *edit :
             {st->gps.view_lat, st->gps.view_lon, st->gps.view_alt, st->gps.view_dir}) {
            set_view(edit, QString());
            edit->setToolTip(QString());
        }
    }
    // .mod（A8）：生效坐标与原值不同 → 强调边框+强调色字
    theme::set_mod(st->gps.view_lat, changed && !st->gps.view_lat->text().isEmpty());
    theme::set_mod(st->gps.view_lon, changed && !st->gps.view_lon->text().isEmpty());
    // D4：只显生效值**单钉**；无生效值 → 不画钉、不 center_on（保持当前视口/选点位置）
    if (effective.has_value()) {
        st->gps.map->set_marker(effective->lat, effective->lon);
        // R-047（M7-WP7）：仅**生效坐标变化**时 center_on —— 改海拔/方位/时间戳等无关字段的
        // 刷新不再把用户正在浏览的地图拽回文件 GPS 点；首现/坐标改变才跟随。
        const std::pair<double, double> coords{effective->lat, effective->lon};
        const bool moved = !st->gps.last_centered.has_value() ||
                           std::fabs(st->gps.last_centered->first - coords.first) > 1e-9 ||
                           std::fabs(st->gps.last_centered->second - coords.second) > 1e-9;
        if (moved) {
            st->gps.last_centered = coords;
            st->gps.map->center_on(effective->lat, effective->lon);
        }
        set_hook(st, hook::kGpsEffective,
                 QStringLiteral("%1,%2").arg(QString::number(effective->lat, 'f', 6),
                                             QString::number(effective->lon, 'f', 6)));
    } else {
        st->gps.map->clear_marker();
        st->gps.last_centered.reset();
        set_hook(st, hook::kGpsEffective, QString());
    }
    // 状态 = 独立徽标（修 1-33）：隐私剥除 > 清除 > 生效值有无
    const bool clearing = st->gps.clear_gps->isChecked();
    const QString state = strip ? PageMeta::tr(kWillRemoveText)
                                : (clearing ? PageMeta::tr(kGpsClearText)
                                            : (effective.has_value() ? PageMeta::tr(kHasGpsText)
                                                                     : PageMeta::tr(kNoGpsText)));
    st->gps.gps_pill->setText(state);
    st->gps.gps_pill->setVisible(true);
    st->gps.gps_pill->setToolTip(
        strip
            ? PageMeta::tr("输出不含 EXIF / XMP（含 GPS）")
            : (clearing ? PageMeta::tr("输出不含 GPS（清除优先于坐标）")
                        : (effective.has_value() ? PageMeta::tr("源文件或批量规则提供坐标")
                                                 : PageMeta::tr("源文件无坐标；批量规则可写入"))));
}

// GPS 规则勾选但坐标非法 → 卡片黄字提示（真警告才用 warn 档令牌；A6：0xcc8800 收口）
void refresh_gps_warning(MetaState *st) {
    double lat = 0, lon = 0;
    const bool valid =
        parse_coord(st->gps.lat, -90.0, 90.0, lat) && parse_coord(st->gps.lon, -180.0, 180.0, lon);
    const bool show = st->gps.gps_enable->isChecked() && !st->gps.clear_gps->isChecked() && !valid;
    st->gps.gps_warn->setVisible(show);
}

void refresh_gps_inputs(MetaState *st) {
    const bool locked = st->gps.clear_gps->isChecked();
    // UX-5：gps_enable（折叠面）与卡头 pp-meta-gps-enable 是**同一状态**（双向同步）——
    // 关 = 整卡不参与规则 → 规则编辑面/清除钮置灰；**取点面（地图/搜索/联想/取中心/读文件）
    // 保持可用**（选点/联想自动勾上是既有行为，关着开关也能取点再自动启用）。
    const bool rule_on = st->gps.gps_enable->isChecked();
    const bool editable = rule_on && !locked;
    st->gps.gps_enable->setEnabled(!locked);
    st->gps.clear_gps->setEnabled(rule_on || locked);
    st->gps.lat->setEnabled(editable);
    st->gps.lon->setEnabled(editable);
    st->gps.alt->setEnabled(editable);
    st->gps.dir->setEnabled(editable);
    st->gps.ts->setEnabled(editable);
    st->gps.more_toggle->setEnabled(editable);
    // placeholder = 输入示例（修 1-33）；禁用时清空（占位色不随 setEnabled 变化）
    st->gps.lat->setPlaceholderText(editable ? PageMeta::tr(kLatPlaceholder) : QString());
    st->gps.lon->setPlaceholderText(editable ? PageMeta::tr(kLonPlaceholder) : QString());
    st->gps.alt->setPlaceholderText(editable ? PageMeta::tr(kAltPlaceholder) : QString());
    st->gps.dir->setPlaceholderText(editable ? PageMeta::tr(kDirPlaceholder) : QString());
    st->gps.ts->setPlaceholderText(editable ? PageMeta::tr(kTsPlaceholder) : QString());
    st->gps.search_edit->setPlaceholderText(locked ? QString() : PageMeta::tr(kSearchPlaceholder));
    st->gps.search_edit->setEnabled(!locked);
    st->gps.search_btn->setEnabled(!locked);
    st->gps.search_results->setEnabled(!locked);
    st->gps.suggest_list->setEnabled(!locked);
    st->gps.layer_seg->setEnabled(!locked);
    if (st->gps.category_btn != nullptr) {
        st->gps.category_btn->setEnabled(!locked); // 分类下拉也是搜索入口（与搜索钮同锁）
    }
    for (QAction *action : st->gps.category_actions) {
        action->setEnabled(!locked); // 分类下拉也是搜索入口（与搜索钮同锁）
    }
    st->gps.provider->setEnabled(!locked);
    st->gps.read_gps->setEnabled(!locked && st->follow.from_selection &&
                                 !st->follow.items.isEmpty());
    st->gps.pick_map->setEnabled(!locked);
    st->gps.rule_toggle->setText(rule_on ? PageMeta::tr("批量 GPS 规则 · 已启用")
                                         : PageMeta::tr("批量 GPS 规则"));
    set_fold_icon(st, st->gps.rule_toggle, st->gps.rule_toggle->isChecked());
    if (locked) {
        st->gps.gps_status->setText(PageMeta::tr("已勾选清除 GPS：输出将不含 GPS"));
    }
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
    if (st->gps.layer_seg == nullptr) {
        return;
    }
    const int index = std::clamp(st->gps.layer_seg->current_index(), 0, 2);
    const QString id =
        st->gps.provider != nullptr ? st->gps.provider->currentData().toString() : QString();
    const pp::map::TileSource *source = pp::map::tile_source_by_id(id);
    const pp::map::TileStyle style = layer_style_of(index);
    const bool supported =
        source == nullptr || style == pp::map::TileStyle::Road || source->supports(style);
    st->gps.layer_seg->setToolTip(
        supported ? PageMeta::tr("地图图层：矢量路网 / 卫星影像 / 混合（影像 + 标注）")
                  : PageMeta::tr("当前地图源不支持该图层，已回退到 Esri 影像"));
}

// 结果行（§5.1「名称 + 地址 + 距离」）：第一行 = 图钉图标 + 名称 + 距离，第二行 = 地址；
// 颜色一律走本页 QSS 钩子（主题重放无需重排文字）。行件由列表以 setItemWidget 承载 —— 单击
// 事件仍由列表（viewport）收到 → itemClicked → search_select(i)。
QWidget *make_result_row(MetaState *st, int index, const pp::map::GeocodeResult &result) {
    auto *row = new QWidget();
    row->setObjectName(objname::kGpsResult1.arg(index));
    auto *v = new QVBoxLayout(row);
    v->setContentsMargins(4, 3, 4, 3);
    v->setSpacing(1);
    auto *top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(formkit::kInlineGap);
    auto *pin = new QLabel(row);
    pin->setPixmap(icons::pixmap(icons::Name::MapPin, kResultIconPx, st->core.tokens.accent,
                                 row->devicePixelRatioF()));
    pin->setFixedSize(kResultIconPx, kResultIconPx);
    top->addWidget(pin, 0, Qt::AlignVCenter);
    auto *name = new ElidedLabel(row);
    name->setObjectName(objname::kGpsResultName1.arg(index));
    name->setProperty(kFieldProperty, fieldprop::kResultName);
    name->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
    name->set_full_text(result.title);
    top->addWidget(name, 1);
    const QString distance = distance_text(result.distance_m);
    if (!distance.isEmpty()) {
        auto *dist = new QLabel(distance, row);
        dist->setProperty(kFieldProperty, fieldprop::kResultDist);
        dist->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        top->addWidget(dist, 0, Qt::AlignVCenter);
    }
    v->addLayout(top);
    auto *address = new ElidedLabel(row);
    address->setObjectName(objname::kGpsResultAddress1.arg(index));
    address->setProperty(kFieldProperty, fieldprop::kResultAddr);
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
    if (st->gps.search_results == nullptr) {
        return;
    }
    st->gps.search_results->clear();
    st->gps.result_row_by_key.clear();
    if (st->gps.results.empty()) {
        auto *empty =
            new QListWidgetItem(PageMeta::tr(kSearchResultPlaceholder), st->gps.search_results);
        empty->setForeground(st->core.tokens.text3);
        empty->setFlags(Qt::ItemIsEnabled);
        st->gps.search_results->setVisible(false); // UX-4①：无结果 = 浮层隐藏
        return;
    }
    for (std::size_t i = 0; i < st->gps.results.size(); ++i) {
        const pp::map::GeocodeResult &result = st->gps.results[i];
        auto *item = new QListWidgetItem(st->gps.search_results);
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
        st->gps.search_results->setItemWidget(item, make_result_row(st, int(i), result));
        st->gps.result_row_by_key.insert(pp::map::result_key(result.title, result.lat, result.lon),
                                         int(i));
    }
    st->gps.search_results->setVisible(true); // UX-4①：有结果显示
}

// 分类下拉的互斥勾选由 QActionGroup 持有（UX-4②）；手输关键词时摘掉选中（摘除需临时关闭互斥）。
void clear_category_checks(MetaState *st) {
    QAction *checked =
        st->gps.category_group != nullptr ? st->gps.category_group->checkedAction() : nullptr;
    if (checked == nullptr) {
        return;
    }
    st->gps.category_group->setExclusive(false);
    checked->setChecked(false);
    st->gps.category_group->setExclusive(true);
}

// 搜索发射（**唯一**入口：搜索钮 / 回车 / 分类下拉）：先收联想与旧结果，再交给 map 异步搜。
void start_search(MetaState *st, bool from_category) {
    const QString query = st->gps.search_edit->text().trimmed();
    if (query.isEmpty()) {
        st->gps.gps_status->setText(PageMeta::tr("请输入搜索关键词"));
        return;
    }
    st->gps.suggest_timer->stop(); // R-313：发射搜索即停防抖（旧查询不回马枪）
    if (!from_category) {
        clear_category_checks(st); // 手输关键词 = 与类目选择脱钩
    }
    clear_search_state(st); // R-045 同一清理面：旧结果/钉/联想一并收干净
    st->gps.map->run_search(query);
}

// 选点（**唯一**入口：地图点击 / 地图选点 / 结果行 / 图钉 / 联想）：回填规则坐标 + 状态 +
// 触发逆地理回显（§4.5「选点后 gps_status 显示地址」）；主钉/居中由 refresh_effective 的
// 生效值管线收口（D4：只显生效值单钉）。
void apply_picked_point(MetaState *st, double lat, double lon, const QString &status) {
    {
        LoadingGuard guard(st); // R-180：加载协议 RAII（抑制 rules_changed）
        st->gps.lat->setText(format_6(lat));
        st->gps.lon->setText(format_6(lon));
        set_gps_rule_enabled(st, true); // UX-5：选点自动勾上（卡头开关与折叠面同一状态）
    }
    // M6-T10（§4.5）：粗盒边缘带（国境/港澳台边缘）坐标精度存疑 → 状态行显式提示
    // （盒内深处与境外不加句 —— 境外偏移恒 identity，两向一致，无"界内界外不一致"）
    st->gps.gps_status->setText(pp::map::coord_precision_suspect(lat, lon)
                                    ? status + QStringLiteral(" · ") +
                                          PageMeta::tr(kPrecisionSuspectText)
                                    : status);
    commit_rule_change(st); // UX-4⑥：规则面四连刷收口
    // 地址回显先清（旧地址属于上一个点，不能挂在新点上）；结果由 reverse_finished 回填。
    st->gps.address_value->set_full_text(QStringLiteral("—"));
    set_hook(st, hook::kGpsAddress, QString());
    st->gps.map->run_reverse(lat, lon);
}

// 结果行选中（行点击 / 回车激活）：结果键对齐（R-223）—— 按稳定键让 map 定位（不再按序号）。
// M6-T9 闭环接线②：map 侧 search_select_key 负责居中(≥12 档) + 主钉 + point_selected（回填/
// 逆地理由 apply_picked_point 收口）；本页只补两件与「点图钉」入口对齐的用户反馈 ——
// 列表当前行跟随 + 结果级状态文案（两条入口同一句话，闭环一路到底）。
void select_result(MetaState *st, int index) {
    if (index < 0 || index >= int(st->gps.results.size())) {
        return; // 占位行 / 越界 = 不动作（§8 空态）
    }
    const pp::map::GeocodeResult &result = st->gps.results[std::size_t(index)];
    st->gps.map->search_select_key(pp::map::result_key(result.title, result.lat, result.lon));
    st->gps.search_results->setCurrentRow(index);
    st->gps.search_results->setVisible(false); // UX-4①：选中即收起浮层
    st->gps.gps_status->setText(PageMeta::tr("已选结果：%1").arg(result.title));
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
    st->gps.view_lat = make_view_field(body, objname::kPpMetaGpsLat);
    st->gps.view_lon = make_view_field(body, objname::kPpMetaGpsLon);
    st->gps.view_alt = make_view_field(body, objname::kPpMetaGpsAlt);
    st->gps.view_dir = make_view_field(body, objname::kPpMetaGpsDir);
    {
        formkit::FieldCell l;
        formkit::FieldCell r;
        l.label = PageMeta::tr("纬度");
        r.label = PageMeta::tr("经度");
        l.field = st->gps.view_lat;
        r.field = st->gps.view_lon;
        l.width = FieldWidth::Flex;
        r.width = FieldWidth::Flex;
        const formkit::DoubleRow row = formkit::double_row(body, l, r);
        row.row->setObjectName(objname::kGpsCoordRow);
        bv->addWidget(row.row);
    }
    // 坐标显示行（M6-T10/§4.5 R4，紧贴经纬度行）：[ⓘ WGS-84（标准 GPS）] ……[DMS][复制]
    //   —— 基准面标签（tooltip 说明火星偏移已抵消）+ 显示档切换 + 复制；**只显示/只复制**，
    //   写入值一律十进制度（规则框在下方折叠面里，与本行无关）。
    {
        TrackGroup group = make_group(body);
        st->gps.datum_icon = new QLabel(group.widget());
        st->gps.datum_icon->setObjectName(objname::kGpsDatumIcon);
        st->gps.datum_icon->setFixedSize(kDatumIconPx, kDatumIconPx);
        st->gps.datum_icon->setAccessibleName(PageMeta::tr("WGS-84"));
        group.add(st->gps.datum_icon);
        group.gap(formkit::kInlineGap);
        st->gps.datum_label = new QLabel(PageMeta::tr("WGS-84（标准 GPS）"), group.widget());
        st->gps.datum_label->setObjectName(objname::kGpsDatum);
        st->gps.datum_label->setProperty(kFieldProperty, fieldprop::kSub);
        st->gps.datum_label->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        st->gps.datum_label->setToolTip(PageMeta::tr(kDatumTooltipText));
        group.add(st->gps.datum_label);
        group.stretch();
        st->gps.dms_toggle = make_chip(group.widget(), objname::kGpsDms, PageMeta::tr("DMS"));
        st->gps.dms_toggle->setToolTip(PageMeta::tr(kDmsToggleTooltipText));
        group.add(st->gps.dms_toggle);
        group.gap(theme::Metrics::group_gap);
        st->gps.copy_coords = make_link(group.widget(), objname::kGpsCopy, PageMeta::tr("复制"));
        st->gps.copy_coords->setToolTip(PageMeta::tr(kCopyCoordsTooltipText));
        group.add(st->gps.copy_coords);
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("坐标"), group.widget(), FieldWidth::Flex);
        row.row->setObjectName(objname::kGpsDatumRow);
        st->gps.datum_row = row.row; // M6-T12：光标坐标读数的 tooltip 面（见 cursor_moved 接线）
        bv->addWidget(row.row);
    }
    {
        formkit::FieldCell l;
        formkit::FieldCell r;
        l.label = PageMeta::tr("海拔");
        r.label = PageMeta::tr("方位");
        l.field = st->gps.view_alt;
        r.field = st->gps.view_dir;
        l.width = FieldWidth::Flex;
        r.width = FieldWidth::Flex;
        const formkit::DoubleRow row = formkit::double_row(body, l, r);
        row.row->setObjectName(objname::kGpsAltdirRow);
        bv->addWidget(row.row);
    }
}

// 操作行（修 1-37：**固定在地图上方**，核心动作不再落进内滚盲区；按钮高 26 = A7）
void build_gps_ops_row(MetaState *st, QWidget *body, QVBoxLayout *bv) {
    TrackGroup group = make_group(body);
    st->gps.read_gps = make_button(group.widget(), objname::kGpsRead, PageMeta::tr("从文件读取"));
    st->gps.read_gps->setFont(theme::font(theme::Typography::body_px, 600));
    st->gps.read_gps->setEnabled(false);
    st->gps.pick_map = make_button(group.widget(), objname::kGpsPick, PageMeta::tr("地图选点"));
    st->gps.pick_map->setFont(theme::font(theme::Typography::body_px, 600));
    st->gps.pick_map->setToolTip(PageMeta::tr("取地图中心为坐标（可先拖拽地图定位）"));
    st->gps.clear_gps = new QPushButton(PageMeta::tr("清除 GPS"), group.widget());
    st->gps.clear_gps->setObjectName(objname::kGpsClear);
    st->gps.clear_gps->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkLinkDanger));
    st->gps.clear_gps->setCheckable(true);
    st->gps.clear_gps->setFixedHeight(theme::Metrics::btn_h);
    st->gps.clear_gps->setCursor(Qt::PointingHandCursor);
    st->gps.clear_gps->setFont(theme::font(theme::Typography::body_px, 600));
    st->gps.clear_gps->setToolTip(PageMeta::tr("输出不含 GPS（清除优先于坐标）"));
    group.add(st->gps.read_gps);
    group.gap(theme::Metrics::group_gap);
    group.add(st->gps.pick_map);
    group.gap(theme::Metrics::group_gap);
    group.add(st->gps.clear_gps);
    group.stretch();
    const formkit::Row row =
        formkit::frow(body, PageMeta::tr("操作"), group.widget(), FieldWidth::Flex);
    row.row->setObjectName(objname::kGpsOpsRow);
    bv->addWidget(row.row);
}

// 地图源下拉（§7.1「OSM 下拉」；有标签；切换 = 本卡地图的显示源。UX-4 只删三个 -row：
// category/layer/address；provider 行保留）
void build_gps_provider_row(MetaState *st, QWidget *body, QVBoxLayout *bv) {
    formkit::ComboSpec provider_spec;
    provider_spec.object_name = objname::kGpsProvider;
    provider_spec.accessible = PageMeta::tr("地图源");
    provider_spec.tooltip = PageMeta::tr("地图瓦片源（设置·地图 的持久档在外层设置里）");
    st->gps.provider = formkit::combo_box(body, provider_spec);
    for (const QString &id : pp::map::provider_ids()) {
        const pp::map::TileProvider *provider = pp::map::provider_by_id(id);
        st->gps.provider->addItem(provider != nullptr ? provider->label : id, id);
    }
    const formkit::Row row =
        formkit::frow(body, PageMeta::tr("地图源"), st->gps.provider, FieldWidth::Standard);
    row.row->setObjectName(objname::kGpsProviderRow);
    bv->addWidget(row.row);
}

// 地点搜索行（UX-4②：[输入][分类 ▾][搜索] —— 6 类目从一整行 chips 收进搜索框右侧下拉）
void build_gps_search_row(MetaState *st, QWidget *body, QVBoxLayout *bv) {
    TrackGroup group = make_group(body);
    st->gps.search_edit = make_rule_field(group.widget(), objname::kGpsSearchEdit);
    st->gps.search_edit->setPlaceholderText(PageMeta::tr(kSearchPlaceholder));
    group.add(st->gps.search_edit, 1);
    group.gap(theme::Metrics::group_gap);
    // 「分类 ▾」下拉按钮：6 个类目 action（objectName pp-meta-gps-category-0..5 保留给 action；
    // 探针断言菜单项数 == 6）。点选 = 回填关键词 + 按类目 run_search（与原 chips 同链路）。
    st->gps.category_btn =
        make_button(group.widget(), objname::kGpsCategoryMenu, PageMeta::tr(kCategoryButtonText));
    st->gps.category_btn->setToolTip(PageMeta::tr(kCategoryButtonTooltip));
    st->gps.category_menu = new QMenu(st->gps.category_btn);
    st->gps.category_group = new QActionGroup(st->gps.category_btn);
    st->gps.category_group->setExclusive(true);
    for (int i = 0; i < int(kCategoryKeywords.size()); ++i) {
        const QString keyword = PageMeta::tr(kCategoryKeywords[std::size_t(i)]);
        QAction *action = st->gps.category_menu->addAction(keyword);
        action->setObjectName(objname::kGpsCategory1.arg(i));
        action->setCheckable(true);
        action->setToolTip(PageMeta::tr("按类目搜索：%1").arg(keyword));
        st->gps.category_group->addAction(action);
        st->gps.category_actions.append(action);
        QObject::connect(action, &QAction::triggered, st, [st, keyword] {
            if (st->gps.clear_gps->isChecked()) {
                return; // 清除 GPS 锁定（控件已禁用；双保险）
            }
            {
                const QSignalBlocker blocker(st->gps.search_edit); // 关键词回填不算"手输"
                st->gps.search_edit->setText(keyword);
            }
            start_search(st, /*from_category=*/true);
        });
    }
    st->gps.category_btn->setMenu(
        st->gps.category_menu); // 整键弹出（探针经 menu()->actions() 取证）
    group.add(st->gps.category_btn);
    group.gap(theme::Metrics::group_gap);
    st->gps.search_btn =
        make_button(group.widget(), objname::kGpsSearchButton, PageMeta::tr("搜索"));
    st->gps.search_btn->setFont(theme::font(theme::Typography::body_px, 600));
    group.add(st->gps.search_btn);
    const formkit::Row row =
        formkit::frow(body, PageMeta::tr("地点搜索"), group.widget(), FieldWidth::Flex);
    row.row->setObjectName(objname::kGpsSearchRow);
    bv->addWidget(row.row);
}

// 地图块（UX-4③⑤）：260px 高 + 覆盖层 —— 图层浮层（左上，半透明底板）/ 展开钮（右上）/
// 联想与结果浮层（默认隐藏）/ 加载 chip（R-315）/ 光标读数 + 缩放钮（底部，避开左下比例尺）。
void build_gps_map_section(MetaState *st, QWidget *body, QVBoxLayout *bv) {
    TrackGroup group = make_group(body);
    st->gps.map = new pp::map::MapWidget(group.widget());
    st->gps.map->setObjectName(objname::kMap);
    // UX-4 stretch（M7-R2-B，主对话授权）：260px **起步**（§16-2 定档下限）+ 卡内拉伸吃富余空间
    // （ux-review §4 改进方案 5：「地图改 stretch=1 吃掉腾出的空间」）；不引入卡内滚动（页面
    // QScrollArea 是唯一滚动面）。原 setFixedHeight(260) 把富余高度挡在卡外（stretch 只是设计
    // 稿语句）—— 现 min 260 + 纵向 Expanding，行/卡 stretch=1 逐级把富余喂给地图。
    st->gps.map->setMinimumHeight(kMapHeight);
    st->gps.map->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    group.add(st->gps.map, 1);
    st->overlay.map_host = group.widget(); // M6-T13：重挂载的原位（挂回时同一宿主 + 同一行槽）
    st->overlay.map_line = group.line;
    // 覆盖层 = map 的子布局（不进卡内行节奏；控件是 map 的子件 → 重挂载时随 map 一并进
    // 对话框，无需重建）。
    auto *overlay = new QVBoxLayout(st->gps.map);
    overlay->setContentsMargins(6, 4, 6, 4);
    overlay->setSpacing(formkit::kInlineGap);
    // 顶行：图层浮层（左）……「展开」钮（右）
    auto *top_line = new QHBoxLayout();
    top_line->setContentsMargins(0, 0, 0, 0);
    top_line->setSpacing(formkit::kInlineGap);
    // UX-4③：图层控件浮到地图左上角 —— 半透明浮层底板（公共 overlay 令牌 hud_bg，与
    // mapwidget 自绘 HUD 同口径；不随主题变）。
    st->overlay.layer_plate = new QWidget(st->gps.map);
    st->overlay.layer_plate->setObjectName(objname::kMapLayerPlate);
    st->overlay.layer_plate->setStyleSheet(
        QStringLiteral("background: %1; border: 1px solid %2; "
                       "border-radius: 6px;")
            .arg(QString::fromLatin1(theme::overlay_tokens().hud_bg),
                 QStringLiteral("rgba(0,0,0,0.12)")));
    auto *plate_line = new QHBoxLayout(st->overlay.layer_plate);
    plate_line->setContentsMargins(4, 3, 4, 3);
    plate_line->setSpacing(0);
    st->gps.layer_seg = new formkit::SegmentedControl(st->overlay.layer_plate);
    st->gps.layer_seg->setObjectName(objname::kGpsLayer);
    st->gps.layer_seg->add_segment(PageMeta::tr("矢量"), objname::kGpsLayerRoad);
    st->gps.layer_seg->add_segment(PageMeta::tr("卫星"), objname::kGpsLayerSatellite);
    st->gps.layer_seg->add_segment(PageMeta::tr("混合"), objname::kGpsLayerHybrid);
    plate_line->addWidget(st->gps.layer_seg);
    // R-218：覆盖层四角贴靠一律逻辑坐标（leading/trailing）——RTL 镜像；LTR 恒等
    top_line->addWidget(st->overlay.layer_plate, 0, Qt::AlignLeading | Qt::AlignTop);
    top_line->addStretch(1);
    st->overlay.map_expand = make_map_hud_button(
        st->gps.map, objname::kMapExpand, icons::Name::ZoomFit, PageMeta::tr(kMapExpandText));
    st->overlay.map_expand->setToolTip(PageMeta::tr(kMapExpandTooltipText));
    top_line->addWidget(st->overlay.map_expand, 0, Qt::AlignTrailing | Qt::AlignTop);
    overlay->addLayout(top_line);
    // UX-4①：联想下拉 + 搜索结果 = **地图浮层**（默认隐藏；有结果显示；选中/清空/换源/失焦
    // 隐藏）。objectName 保留（pp-meta-gps-suggest / pp-meta-gps-search-results /
    // pp-meta-gps-results-host）。
    st->gps.suggest_list = new QListWidget(st->gps.map);
    st->gps.suggest_list->setObjectName(objname::kGpsSuggest);
    st->gps.suggest_list->setAccessibleName(PageMeta::tr("搜索联想"));
    st->gps.suggest_list->setToolTip(PageMeta::tr("选择联想项：地图定位到该地点"));
    st->gps.suggest_list->setFixedHeight(kSuggestVisibleRows * formkit::kChipH + 6);
    st->gps.suggest_list->setSelectionMode(QAbstractItemView::SingleSelection);
    st->gps.suggest_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    st->gps.suggest_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    st->gps.suggest_list->setTextElideMode(Qt::ElideRight);
    st->gps.suggest_list->setVisible(false);
    overlay->addWidget(st->gps.suggest_list);
    {
        auto *host = new QWidget(st->gps.map);
        host->setObjectName(objname::kGpsResultsHost);
        auto *line = new QHBoxLayout(host);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(0);
        st->gps.search_results = new QListWidget(host);
        st->gps.search_results->setObjectName(objname::kGpsSearchResults);
        st->gps.search_results->setAccessibleName(PageMeta::tr("搜索结果"));
        st->gps.search_results->setToolTip(
            PageMeta::tr("点选结果：地图居中并落点（可在图上继续微调）"));
        st->gps.search_results->setFixedHeight(kResultVisibleRows * kResultRowH + 4);
        st->gps.search_results->setSelectionMode(QAbstractItemView::SingleSelection);
        st->gps.search_results->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        st->gps.search_results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        st->gps.search_results->setTextElideMode(Qt::ElideRight);
        st->gps.search_results->setVisible(false); // UX-4①：默认隐藏
        line->addWidget(st->gps.search_results, 1);
        overlay->addWidget(host);
    }
    // R-315：瓦片加载反馈 chip（tiles_pending_changed → 显示/隐藏）
    st->overlay.map_loading = new QLabel(PageMeta::tr(kMapLoadingText), st->gps.map);
    st->overlay.map_loading->setObjectName(objname::kMapLoading);
    st->overlay.map_loading->setProperty(kFieldProperty, fieldprop::kMapHud);
    st->overlay.map_loading->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    st->overlay.map_loading->setToolTip(PageMeta::tr("正在加载地图瓦片"));
    st->overlay.map_loading->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    st->overlay.map_loading->setVisible(false);
    overlay->addWidget(st->overlay.map_loading, 0, Qt::AlignLeading);
    overlay->addStretch(1);
    // 底行：光标读数 + 「－/＋」缩放钮（右下；左下留给 mapwidget 自绘比例尺）
    auto *bottom_line = new QHBoxLayout();
    bottom_line->setContentsMargins(0, 0, 0, 0);
    bottom_line->setSpacing(formkit::kInlineGap);
    st->overlay.map_cursor = new QLabel(PageMeta::tr(kMapCursorIdleText), st->gps.map);
    st->overlay.map_cursor->setObjectName(objname::kMapCursor);
    st->overlay.map_cursor->setProperty(kFieldProperty, fieldprop::kMapHud);
    st->overlay.map_cursor->setFont(theme::font(theme::Typography::hint_px, QFont::Normal, true));
    st->overlay.map_cursor->setToolTip(PageMeta::tr(kMapCursorTooltipText));
    st->overlay.map_cursor->setAttribute(Qt::WA_TransparentForMouseEvents,
                                         true); // 不吃地图鼠标事件
    bottom_line->addStretch(1);
    bottom_line->addWidget(st->overlay.map_cursor, 0, Qt::AlignTrailing | Qt::AlignBottom);
    st->overlay.zoom_out = make_map_hud_button(st->gps.map, objname::kMapZoomOut,
                                               icons::Name::ZoomOut, PageMeta::tr(kMapZoomOutText));
    st->overlay.zoom_in = make_map_hud_button(st->gps.map, objname::kMapZoomIn, icons::Name::ZoomIn,
                                              PageMeta::tr(kMapZoomInText));
    bottom_line->addWidget(st->overlay.zoom_out);
    bottom_line->addWidget(st->overlay.zoom_in);
    overlay->addLayout(bottom_line);
    // UX-4 stretch（M7-R2-B）：地图行 stretch=1 —— 卡体富余高全归地图（不吃行节奏）
    bv->addWidget(group.widget(), 1);
}

// 批量 GPS 规则折叠面（修 1-37：纯折叠行，默认折叠；内为规则编辑面）
void build_gps_rule_section(MetaState *st, QWidget *card, QVBoxLayout *v) {
    st->gps.rule_toggle = new QToolButton(card);
    st->gps.rule_toggle->setObjectName(objname::kGpsRuleToggle);
    st->gps.rule_toggle->setProperty(kFieldProperty, fieldprop::kFold);
    st->gps.rule_toggle->setText(PageMeta::tr("批量 GPS 规则"));
    st->gps.rule_toggle->setCheckable(true);
    st->gps.rule_toggle->setChecked(false);
    st->gps.rule_toggle->setAutoRaise(true);
    st->gps.rule_toggle->setFixedHeight(theme::Metrics::btn_h);
    st->gps.rule_toggle->setFont(theme::font(theme::Typography::body_px, 600));
    {
        auto *toggle_row = new QHBoxLayout();
        toggle_row->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                                       6);
        toggle_row->addWidget(st->gps.rule_toggle, 0, Qt::AlignLeading);
        toggle_row->addStretch(1);
        v->addLayout(toggle_row);
    }

    // 规则编辑面（折叠内容）：启用（表单布尔）+ 纬度/经度 + 可选字段（海拔/方位/时间戳）
    st->gps.rule_host = new QWidget(card);
    st->gps.rule_host->setObjectName(objname::kGpsRule);
    auto *rule_body = st->gps.rule_host;
    auto *rule_v = new QVBoxLayout(rule_body);
    rule_v->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x, 10);
    rule_v->setSpacing(0);
    {
        const formkit::CheckRow enable =
            formkit::check_row(rule_body, QString(), PageMeta::tr("启用"), objname::kGpsEnable);
        st->gps.gps_enable = enable.box;
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
    st->gps.lat = make_rule_field(rule_body, objname::kGpsLat);
    st->gps.lat->setValidator(new QDoubleValidator(-90.0, 90.0, 6, st->gps.lat));
    st->gps.lon = make_rule_field(rule_body, objname::kGpsLon);
    st->gps.lon->setValidator(new QDoubleValidator(-180.0, 180.0, 6, st->gps.lon));
    rule_v->addWidget(make_rule_grid_pair(PageMeta::tr("纬度"), st->gps.lat, QStringLiteral("°"),
                                          PageMeta::tr("经度"), st->gps.lon, QStringLiteral("°")));
    st->gps.more_toggle = new QToolButton(rule_body);
    st->gps.more_toggle->setObjectName(objname::kGpsMoreToggle);
    st->gps.more_toggle->setProperty(kFieldProperty, fieldprop::kFold);
    st->gps.more_toggle->setText(PageMeta::tr("更多字段"));
    st->gps.more_toggle->setCheckable(true);
    st->gps.more_toggle->setChecked(false);
    st->gps.more_toggle->setAutoRaise(true);
    st->gps.more_toggle->setFixedHeight(theme::Metrics::btn_h);
    st->gps.more_toggle->setFont(theme::font(theme::Typography::body_px, 600));
    {
        auto *more_row = new QHBoxLayout();
        more_row->setContentsMargins(0, 0, 0, 0);
        more_row->addWidget(st->gps.more_toggle, 0, Qt::AlignLeading);
        more_row->addStretch(1);
        rule_v->addLayout(more_row);
    }
    st->gps.alt = make_rule_field(rule_body, objname::kGpsAlt);
    st->gps.dir = make_rule_field(rule_body, objname::kGpsDir);
    st->gps.ts = make_rule_field(rule_body, objname::kGpsTs);
    st->gps.rule_altdir_row =
        make_rule_grid_pair(PageMeta::tr("海拔"), st->gps.alt, QStringLiteral("m"),
                            PageMeta::tr("方位"), st->gps.dir, QStringLiteral("°"));
    st->gps.rule_ts_row =
        formkit::frow(rule_body, PageMeta::tr("时间戳"), st->gps.ts, FieldWidth::Flex).row;
    rule_v->addWidget(st->gps.rule_altdir_row);
    rule_v->addWidget(st->gps.rule_ts_row);
    for (QWidget *hidden : {st->gps.rule_altdir_row, st->gps.rule_ts_row}) {
        hidden->setVisible(false);
    }
    st->gps.rule_host->setVisible(false);
    v->addWidget(st->gps.rule_host);
}

// 统一接线（UX-4⑥ gps_wiring）：全部信号链一处收口；规则面变更走 commit_rule_change。
void gps_wiring(MetaState *st) {
    // 换源（UX-4①/R-045）：清旧钉/旧结果/联想走 clear_search_state 唯一清理面
    QObject::connect(st->gps.provider, QOverload<int>::of(&QComboBox::currentIndexChanged), st,
                     [st](int index) {
                         const QString id = st->gps.provider->itemData(index).toString();
                         if (!id.isEmpty()) {
                             st->gps.map->set_provider(id);
                         }
                         refresh_layer_hint(st); // 换源 → 图层支持性提示随之更新（§5.2）
                         clear_search_state(st); // R-045：换源清理（卡片下拉/设置对话框同一函数）
                     });
    for (QLineEdit *edit : {st->gps.lat, st->gps.lon}) {
        QObject::connect(edit, &QLineEdit::textChanged, st, [st](const QString &) {
            if (st->core.loading) {
                return;
            }
            commit_rule_change(st);
        });
    }
    for (QLineEdit *edit : {st->gps.alt, st->gps.dir, st->gps.ts}) {
        QObject::connect(edit, &QLineEdit::textChanged, st, [st](const QString &) {
            if (st->core.loading) {
                return;
            }
            commit_rule_change(st);
        });
    }
    // UX-5：卡头开关与折叠面 gps_enable = 同一状态的两个入口（双向同步；选点/联想自动勾上）
    const auto on_gps_enable_toggled = [st](bool on) {
        if (st->core.loading) {
            return;
        }
        set_gps_rule_enabled(st, on);
        commit_rule_change(st);
    };
    QObject::connect(st->gps.gps_enable, &formkit::CheckBox::toggled, st, on_gps_enable_toggled);
    QObject::connect(st->gps.gps_enable_switch, &formkit::SwitchControl::toggled, st,
                     on_gps_enable_toggled);
    QObject::connect(st->gps.clear_gps, &QPushButton::toggled, st, [st](bool) {
        if (st->core.loading) {
            return;
        }
        commit_rule_change(st);
    });
    QObject::connect(st->gps.more_toggle, &QToolButton::toggled, st, [st](bool on) {
        set_fold_icon(st, st->gps.more_toggle, on); // 插入符 = 矢量图标（C3）
        st->gps.rule_altdir_row->setVisible(on);
        st->gps.rule_ts_row->setVisible(on);
    });
    QObject::connect(st->gps.rule_toggle, &QToolButton::toggled, st, [st](bool on) {
        st->gps.rule_host->setVisible(on);
        set_fold_icon(st, st->gps.rule_toggle, on);
    });

    // 从选中文件读取 GPS（坐标来自**同一个 probe**：与生效值同源，不另解析 EXIF）
    QObject::connect(st->gps.read_gps, &QPushButton::clicked, st, [st] {
        if (st->follow.items.isEmpty() || st->follow.probes.empty()) {
            return;
        }
        const pp::BatchRules rules = build_rules(st);
        const pp::EffectivePreview preview = pp::preview_effective(
            st->follow.probes.front().meta, rules, st->follow.items.first().exception);
        if (!preview.gps.original.has_value()) {
            st->gps.gps_status->setText(st->follow.probes.front().ok
                                            ? PageMeta::tr("选中的文件没有 GPS 信息")
                                            : PageMeta::tr("无法读取选中文件的元数据"));
            return;
        }
        const pp::GpsData &gps = *preview.gps.original;
        {
            LoadingGuard guard(st); // R-180：加载协议 RAII
            st->gps.lat->setText(format_6(gps.lat));
            st->gps.lon->setText(format_6(gps.lon));
            st->gps.alt->setText(gps.altitude.has_value() ? format_6(*gps.altitude) : QString());
            st->gps.dir->setText(gps.direction.has_value() ? format_6(*gps.direction) : QString());
            st->gps.ts->setText(gps.timestamp.has_value() ? QString::fromStdString(*gps.timestamp)
                                                          : QString());
            set_gps_rule_enabled(st, true); // UX-5：读取即启用（同一状态双入口）
        }
        st->gps.gps_status->setText(PageMeta::tr("已从选中文件读取 GPS"));
        commit_rule_change(st);
    });

    // 地图选点：取视口中心为规则坐标（点击地图仍直接取点 —— v0.2 行为不变；M6-T8 起叠加地址回显）
    QObject::connect(st->gps.pick_map, &QPushButton::clicked, st, [st] {
        apply_picked_point(st, st->gps.map->center_lat(), st->gps.map->center_lon(),
                           PageMeta::tr("已取地图中心为坐标"));
    });

    // 搜索（M6-T8）：搜索钮 / 回车 → run_search；输入防抖 200ms → run_suggest（联想浮层）
    QObject::connect(st->gps.search_btn, &QPushButton::clicked, st,
                     [st] { start_search(st, false); });
    QObject::connect(st->gps.search_edit, &QLineEdit::returnPressed, st->gps.search_btn,
                     &QPushButton::click);
    st->gps.suggest_timer = new QTimer(st);
    st->gps.suggest_timer->setSingleShot(true);
    st->gps.suggest_timer->setInterval(kSuggestDebounceMs);
    QObject::connect(st->gps.suggest_timer, &QTimer::timeout, st,
                     [st] { st->gps.map->run_suggest(st->gps.search_edit->text().trimmed()); });
    QObject::connect(st->gps.search_edit, &QLineEdit::textChanged, st, [st](const QString &text) {
        if (st->core.loading) {
            return; // 程序化回填（apply_rules/联想选中）不算输入
        }
        clear_category_checks(st); // 手输关键词 = 与类目选择脱钩
        if (text.trimmed().isEmpty()) {
            st->gps.suggest_timer->stop();
            st->gps.suggest_list->clear();
            st->gps.suggest_list->setVisible(false);
            set_hook(st, hook::kGpsSuggest, 0);
            st->gps.map->run_suggest(QString()); // R-012：空查询也清算在途联想（世代推进 + abort）
            return;
        }
        st->gps.suggest_timer->start(); // 200ms 防抖（§4.3）
    });
    QObject::connect(st->gps.map, &pp::map::MapWidget::suggest_finished, st,
                     [st](const std::vector<pp::map::SuggestItem> &items, const QString &error) {
                         st->gps.suggest_list->clear();
                         if (!error.isEmpty() || items.empty()) {
                             // 联想是增量面：无匹配/错误不打断输入（状态行不被联想刷屏）
                             st->gps.suggest_list->setVisible(false);
                             set_hook(st, hook::kGpsSuggest, 0);
                             return;
                         }
                         for (const pp::map::SuggestItem &item : items) {
                             auto *row = new QListWidgetItem(st->gps.suggest_list);
                             row->setText(item.address.isEmpty() ? item.title
                                                                 : QStringLiteral("%1 · %2").arg(
                                                                       item.title, item.address));
                             row->setToolTip(row->text());
                             row->setData(Qt::UserRole, item.lat);
                             row->setData(Qt::UserRole + 1, item.lon);
                             row->setData(Qt::UserRole + 2, item.has_point);
                             row->setData(Qt::UserRole + 3, item.title);
                         }
                         st->gps.suggest_list->setVisible(true); // UX-4①：有结果显示
                         set_hook(st, hook::kGpsSuggest, int(items.size()));
                     });
    QObject::connect(
        st->gps.suggest_list, &QListWidget::itemClicked, st, [st](QListWidgetItem *item) {
            if (item == nullptr) {
                return;
            }
            st->gps.suggest_timer->stop(); // R-313：点选即停防抖（旧查询不"回马枪"弹回联想框）
            st->gps.suggest_list->setVisible(false);
            set_hook(st, hook::kGpsSuggest, 0);
            const bool has_point = item->data(Qt::UserRole + 2).toBool();
            const double lat = item->data(Qt::UserRole).toDouble();
            const double lon = item->data(Qt::UserRole + 1).toDouble();
            if (has_point && std::isfinite(lat) && std::isfinite(lon)) {
                // 选建议 = 直接定位（§4.3）：主钉 + 居中（≥16 档）+ 选点闭环
                st->gps.map->set_marker(lat, lon);
                st->gps.map->center_on(lat, lon, int(kSuggestZoom));
                apply_picked_point(st, lat, lon, PageMeta::tr("已从联想选择坐标"));
                return;
            }
            // 无坐标建议（纯地址）：以标题再搜一次
            const QString title = item->data(Qt::UserRole + 3).toString();
            if (title.isEmpty()) {
                return;
            }
            {
                const QSignalBlocker blocker(st->gps.search_edit);
                st->gps.search_edit->setText(title);
            }
            start_search(st, false);
        });
    // 结果列表（M6-T8）：数据源 = search_results_ready（与 map 内部 results 同一批数据；
    // R-223：行/钉对齐按稳定键，不按序号）
    QObject::connect(
        st->gps.map, &pp::map::MapWidget::search_results_ready, st,
        [st](const std::vector<pp::map::GeocodeResult> &results, const QString &error) {
            st->gps.results = results;
            set_hook(st, hook::kGpsResults, int(results.size()));
            fill_result_rows(st); // UX-4①：有结果显示浮层 / 空态隐藏
            if (!error.isEmpty()) {
                st->gps.gps_status->setText(
                    error); // 失败分类文案（离线/无 Key/请求失败/解析失败/无匹配）
            } else if (results.empty()) {
                st->gps.gps_status->setText(PageMeta::tr("没有匹配的结果"));
            } else {
                st->gps.gps_status->setText(PageMeta::tr("搜索到 %1 个结果").arg(results.size()));
            }
        });
    QObject::connect(
        st->gps.search_results, &QListWidget::itemClicked, st, [st](QListWidgetItem *item) {
            select_result(st, item != nullptr ? st->gps.search_results->row(item) : -1);
        });
    QObject::connect(
        st->gps.search_results, &QListWidget::itemActivated, st, [st](QListWidgetItem *item) {
            select_result(st, item != nullptr ? st->gps.search_results->row(item) : -1);
        });
    // 图钉命中（R-223 稳定键对齐）：按 pin_selected(key) 找行（pin_clicked(index) 的冻结面
    // 仍由 mapwidget 并发发射，供探针/旧消费方使用）—— 点钉不算「取点」。
    QObject::connect(st->gps.map, &pp::map::MapWidget::pin_selected, st, [st](const QString &key) {
        const int index = st->gps.result_row_by_key.value(key, -1);
        if (index < 0 || index >= int(st->gps.results.size())) {
            return;
        }
        const pp::map::GeocodeResult &result = st->gps.results[std::size_t(index)];
        st->gps.search_results->setCurrentRow(index); // 列表跟随命中钉（不触发 search_select）
        st->gps.search_results->setVisible(false);    // UX-4①：选中即收起浮层
        apply_picked_point(st, result.lat, result.lon,
                           PageMeta::tr("已选结果：%1").arg(result.title));
    });
    // 逆地理回显（M6-T8/§4.5 + UX-4④）：地址进 gps_status tooltip + 右键「复制地址」；
    // 视觉只留「已选点 · …」一行（pp-meta-gps-address 保留为隐藏承载 + 钩子不变）。
    QObject::connect(st->gps.map, &pp::map::MapWidget::reverse_finished, st,
                     [st](const QString &address, const QString &error) {
                         const QString text = error.isEmpty() ? address : error;
                         st->gps.address_value->set_full_text(text.isEmpty() ? QStringLiteral("—")
                                                                             : text);
                         set_hook(st, hook::kGpsAddress, text);
                         st->gps.gps_status->setToolTip(text.isEmpty() ? QString() : text);
                         if (error.isEmpty() && !address.isEmpty()) {
                             st->gps.gps_status->setText(PageMeta::tr("已选点 · %1").arg(address));
                         }
                     });
    QObject::connect(st->gps.map, &pp::map::MapWidget::point_selected, st,
                     [st](double lat, double lon) {
                         apply_picked_point(st, lat, lon, PageMeta::tr("已从地图选择坐标"));
                         // UX-4①「失焦隐藏」：地图上取点 = 离开搜索浮层
                         st->gps.search_results->setVisible(false);
                         st->gps.suggest_list->setVisible(false);
                     });
    // 图层（M6-T8/§5.2）：三态 Seg → set_tile_style（基准不漂由 mapwidget 单源保证）
    QObject::connect(
        st->gps.layer_seg, &formkit::SegmentedControl::current_changed, st, [st](int index) {
            const int style = std::clamp(index, 0, 2);
            st->gps.map->set_tile_style(layer_style_of(style));
            refresh_layer_hint(st);
            set_hook(st, hook::kGpsLayer, QString::fromLatin1(kLayerStyleKeys[std::size_t(style)]));
        });
    // M6-T12（§5.3/§7-T12 体验件）：缩放钮 → map->zoom_in/zoom_out（视口中心锚点，中心不动）
    QObject::connect(st->overlay.zoom_in, &QToolButton::clicked, st,
                     [st] { st->gps.map->zoom_in(); });
    QObject::connect(st->overlay.zoom_out, &QToolButton::clicked, st,
                     [st] { st->gps.map->zoom_out(); });
    // M6-T13（§7-T13 R5）：展开钮 → 大地图对话框（同一 map 控件重挂载；模态，关闭即挂回）
    QObject::connect(st->overlay.map_expand, &QToolButton::clicked, st,
                     [st] { open_map_expanded_view(st); });
    // 光标坐标读数（接 map 的 cursor_moved，WGS-84、约 100ms 节流）：地图覆盖层 chip 显示
    // 「光标: lat, lon」；同一读数另挂坐标行 tooltip（§2.6：细节进 tooltip）—— 只读回显，
    // 不写规则框、不动 gps_status（状态行留给选点/搜索结果等语义消息）。
    QObject::connect(
        st->gps.map, &pp::map::MapWidget::cursor_moved, st, [st](double lat, double lon) {
            const QString text = PageMeta::tr("光标: %1, %2").arg(format_6(lat), format_6(lon));
            st->overlay.map_cursor->setText(text);
            if (st->gps.datum_row != nullptr) {
                st->gps.datum_row->setToolTip(text);
            }
        });
    // R-315：瓦片加载有 UI 反馈（tiles_pending_changed → 「加载中…」chip；0 = 隐藏）。
    // search_finished **不接**：它与 search_results_ready 同一批数据（页面只消费后者，
    // 双接会把状态行刷两遍）—— 登记不接理由于 handoff。
    QObject::connect(st->gps.map, &pp::map::MapWidget::tiles_pending_changed, st,
                     [st](int pending) { st->overlay.map_loading->setVisible(pending > 0); });
    // M6-T10（§4.5 R4）：显示档切换钮 + 复制链 + 坐标框右键菜单 —— 三条入口同一收口
    // （set_dms_mode / copy_coords）；右键菜单同时承担"复制"（只读框的默认菜单不可用）
    QObject::connect(st->gps.dms_toggle, &QAbstractButton::toggled, st,
                     [st](bool on) { set_dms_mode(st, on); });
    QObject::connect(st->gps.copy_coords, &QToolButton::clicked, st, [st] { copy_coords(st); });
    for (QLineEdit *edit : {st->gps.view_lat, st->gps.view_lon}) {
        edit->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(edit, &QLineEdit::customContextMenuRequested, st,
                         [st, edit](const QPoint &pos) {
                             QMenu menu(edit);
                             QAction *copy = menu.addAction(PageMeta::tr("复制坐标"));
                             QAction *dms = menu.addAction(PageMeta::tr("度分显示（DMS）"));
                             dms->setCheckable(true);
                             dms->setChecked(st->gps.dms_mode);
                             const QAction *chosen = menu.exec(edit->mapToGlobal(pos));
                             if (chosen == copy) {
                                 copy_coords(st);
                             } else if (chosen == dms) {
                                 set_dms_mode(st, dms->isChecked()); // 菜单已翻转勾选态
                             }
                         });
    }
    // UX-4④：gps_status 右键「复制地址」（地址行删除后的唯一复制入口；tooltip 已挂地址）
    st->gps.gps_status->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(st->gps.gps_status, &QLabel::customContextMenuRequested, st,
                     [st](const QPoint &pos) {
                         const QString address = st->gps.address_value->full_text();
                         if (address.isEmpty() || address == QStringLiteral("—")) {
                             return;
                         }
                         QMenu menu(st->gps.gps_status);
                         QAction *copy = menu.addAction(PageMeta::tr(kCopyAddressText));
                         if (menu.exec(st->gps.gps_status->mapToGlobal(pos)) == copy) {
                             QApplication::clipboard()->setText(address);
                             st->gps.gps_status->setText(PageMeta::tr("已复制地址"));
                         }
                     });
    // 初始态（Road = map 的默认图层）+ 自验钩子读数
    refresh_layer_hint(st);
    set_hook(st, hook::kGpsLayer, QString::fromLatin1(kLayerStyleKeys[0]));
    set_hook(st, hook::kGpsResults, 0);
    set_hook(st, hook::kGpsSuggest, 0);
    set_hook(st, hook::kGpsAddress, QString());
    set_hook(st, hook::kGpsDms, false); // M6-T10：显示档（默认十进制度）
    fill_result_rows(st);               // 空态 = 占位行（"搜索后在此选择"）+ 浮层隐藏
}

QWidget *build_gps_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    const CardHeader header =
        make_header(st, card, PageMeta::tr("GPS"), objname::kGpsPill, objname::kGpsHint,
                    objname::kPpMetaGpsEnable); // UX-5：卡头「启用」开关
    st->gps.gps_pill = header.pill;
    st->gps.gps_hint = header.hint;
    st->gps.gps_hint->set_full_text(PageMeta::tr("跟随选中 · WGS-84"));
    st->gps.gps_enable_switch = header.enable;
    v->addWidget(header.row);

    auto *body = new QWidget(card);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                           theme::Metrics::card_pad_bottom);
    bv->setSpacing(0);

    st->gps.gps_warn = new QLabel(PageMeta::tr("GPS 坐标无效"), card);
    st->gps.gps_warn->setObjectName(objname::kGpsWarning);
    st->gps.gps_warn->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    st->gps.gps_warn->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                                         4);
    st->gps.gps_warn->setVisible(false);
    v->addWidget(st->gps.gps_warn);

    build_gps_coord_section(st, body, bv);
    // UX-4④：地址行（pp-meta-gps-address-row）删除 —— 但 pp-meta-gps-address 与
    // pp_meta_gps_address 钩子保留：**隐藏承载 widget**（文本 = 逆地理回显，供 gps_status
    // tooltip 与右键「复制地址」取值；隐藏不占行节奏）。
    st->gps.address_value = new ElidedLabel(body);
    st->gps.address_value->setObjectName(objname::kGpsAddress);
    st->gps.address_value->setProperty(kFieldProperty, fieldprop::kAddress);
    st->gps.address_value->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
    st->gps.address_value->set_full_text(QStringLiteral("—"));
    st->gps.address_value->setVisible(false);
    bv->addWidget(st->gps.address_value);
    build_gps_ops_row(st, body, bv);
    build_gps_provider_row(st, body, bv);
    build_gps_search_row(st, body, bv);
    build_gps_map_section(st, body, bv);
    v->addWidget(body);
    st->gps.gps_status = new QLabel(card);
    st->gps.gps_status->setObjectName(objname::kGpsStatus);
    st->gps.gps_status->setProperty(kFieldProperty, fieldprop::kSub);
    st->gps.gps_status->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    st->gps.gps_status->setContentsMargins(theme::Metrics::card_pad_x, 4,
                                           theme::Metrics::card_pad_x, 4);
    v->addWidget(st->gps.gps_status);

    build_gps_rule_section(st, card, v);
    gps_wiring(st);
    return card;
}

contextmenu::GpsCardIntents gps_card_intents(MetaState *st) {
    contextmenu::GpsCardIntents in;
    in.running = st->core.page == nullptr || !st->core.page->isEnabled();
    in.enabled = st->gps.gps_enable_switch != nullptr && st->gps.gps_enable_switch->isChecked();
    in.enable_toggle = [st] {
        if (st->gps.gps_enable_switch != nullptr)
            st->gps.gps_enable_switch->toggle();
    };
    in.read_from_file = [st] {
        if (st->gps.read_gps != nullptr)
            st->gps.read_gps->click();
    };
    in.pick_on_map = [st] { open_map_expanded_view(st); };
    in.take_map_center = [st] {
        if (st->gps.pick_map != nullptr)
            st->gps.pick_map->click();
    };
    in.copy_coords = [st] {
        if (st->gps.copy_coords != nullptr)
            st->gps.copy_coords->click();
    };
    in.clear_gps = [st] {
        if (st->gps.clear_gps != nullptr)
            st->gps.clear_gps->click();
    };
    return in;
}

} // namespace pp::ui::meta
