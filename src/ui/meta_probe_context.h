// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页探针访问层（R-182 测试钩子收口 + R-258 后半：加性 ProbeContext）
//
// Contract: docs/deep-review-0.3.2.md R-182（测试钩子入侵公开 API + 宿主 findChild 反向伸入
// 页面，objectName 三方事实契约）与 R-258（探针与产品共 TU/私有耦合的后半收口）。
// 口径（additive：既有行为零变化，钩子/词表值串逐字不变）：
//   * `hook::` = 自验钩子（动态属性）名**单源** —— set_hook 写入 / 探针读取 / page_meta.h
//     清单三方同一词表；
//   * `objname::` = pp-meta-* / tag_* 等 objectName **单源** —— 构建器 setObjectName 与
//     探针 findChild 同源（三方事实契约收口成一张表）；
//   * `ProbeContext` = 探针侧**类型化只读访问器**（替代宿主散写 property()/findChild 字符串）。
// 消费方：页面构建器（写侧，本包落地）+ --ui-smoke 探针（读侧，F4 波次按「探针需求」接入）。
#pragma once

#include "ui/page_meta.h"

#include <QList>
#include <QString>
#include <QVariant>
#include <QWidget>

namespace pp::ui::meta {

// —— 自验钩子（动态属性）名 ——
namespace hook {
inline constexpr const char *kFollowCount = "pp_meta_follow_count";
inline constexpr const char *kFollowName = "pp_meta_follow_name";
inline constexpr const char *kFollowPending = "pp_meta_follow_pending";
inline constexpr const char *kFollowStaleDropped = "pp_meta_follow_stale_dropped";
inline constexpr const char *kTimeNew = "pp_meta_time_new";
inline constexpr const char *kGpsEffective = "pp_meta_gps_effective";
inline constexpr const char *kMultiRows = "pp_meta_multi_rows";
inline constexpr const char *kGpsLayer = "pp_meta_gps_layer";
inline constexpr const char *kGpsResults = "pp_meta_gps_results";
inline constexpr const char *kGpsSuggest = "pp_meta_gps_suggest";
inline constexpr const char *kGpsAddress = "pp_meta_gps_address";
inline constexpr const char *kGpsDms = "pp_meta_gps_dms";
} // namespace hook

// —— objectName 词表（pp-meta-* 一族 + tag/gps/exception 规则面；值串逐字不变）——
namespace objname {
inline const QString kExceptionLabel = QStringLiteral("exception_label");
inline const QString kExceptionList = QStringLiteral("exception_list");
inline const QString kGpsAlt = QStringLiteral("gps_alt");
inline const QString kGpsDir = QStringLiteral("gps_dir");
inline const QString kGpsEnable = QStringLiteral("gps_enable");
inline const QString kGpsLat = QStringLiteral("gps_lat");
inline const QString kGpsLon = QStringLiteral("gps_lon");
inline const QString kGpsMoreToggle = QStringLiteral("gps_more_toggle");
inline const QString kGpsStatus = QStringLiteral("gps_status");
inline const QString kGpsTs = QStringLiteral("gps_ts");
inline const QString kGpsWarning = QStringLiteral("gps_warning");
inline const QString kPpCard = QStringLiteral("pp-card");
inline const QString kPpCardTitle = QStringLiteral("pp-card-title");
inline const QString kContent = QStringLiteral("pp-meta-content");
inline const QString kEffectRow = QStringLiteral("pp-meta-effect-row");
inline const QString kExceptionHost = QStringLiteral("pp-meta-exception-host");
inline const QString kGpsAddress = QStringLiteral("pp-meta-gps-address");
inline const QString kPpMetaGpsAlt = QStringLiteral("pp-meta-gps-alt");
inline const QString kGpsAltdirRow = QStringLiteral("pp-meta-gps-altdir-row");
inline const QString kGpsCategory1 = QStringLiteral("pp-meta-gps-category-%1");
inline const QString kGpsCategoryMenu = QStringLiteral("pp-meta-gps-category-menu");
inline const QString kGpsClear = QStringLiteral("pp-meta-gps-clear");
inline const QString kGpsCoordRow = QStringLiteral("pp-meta-gps-coord-row");
inline const QString kGpsCopy = QStringLiteral("pp-meta-gps-copy");
inline const QString kGpsDatum = QStringLiteral("pp-meta-gps-datum");
inline const QString kGpsDatumIcon = QStringLiteral("pp-meta-gps-datum-icon");
inline const QString kGpsDatumRow = QStringLiteral("pp-meta-gps-datum-row");
inline const QString kPpMetaGpsDir = QStringLiteral("pp-meta-gps-dir");
inline const QString kGpsDms = QStringLiteral("pp-meta-gps-dms");
inline const QString kPpMetaGpsEnable = QStringLiteral("pp-meta-gps-enable");
inline const QString kGpsHint = QStringLiteral("pp-meta-gps-hint");
inline const QString kPpMetaGpsLat = QStringLiteral("pp-meta-gps-lat");
inline const QString kGpsLayer = QStringLiteral("pp-meta-gps-layer");
inline const QString kGpsLayerHybrid = QStringLiteral("pp-meta-gps-layer-hybrid");
inline const QString kGpsLayerRoad = QStringLiteral("pp-meta-gps-layer-road");
inline const QString kGpsLayerSatellite = QStringLiteral("pp-meta-gps-layer-satellite");
inline const QString kPpMetaGpsLon = QStringLiteral("pp-meta-gps-lon");
inline const QString kGpsOpsRow = QStringLiteral("pp-meta-gps-ops-row");
inline const QString kGpsPick = QStringLiteral("pp-meta-gps-pick");
inline const QString kGpsPill = QStringLiteral("pp-meta-gps-pill");
inline const QString kGpsProvider = QStringLiteral("pp-meta-gps-provider");
inline const QString kGpsProviderRow = QStringLiteral("pp-meta-gps-provider-row");
inline const QString kGpsRead = QStringLiteral("pp-meta-gps-read");
inline const QString kGpsResult1 = QStringLiteral("pp-meta-gps-result-%1");
inline const QString kGpsResultAddress1 = QStringLiteral("pp-meta-gps-result-address-%1");
inline const QString kGpsResultName1 = QStringLiteral("pp-meta-gps-result-name-%1");
inline const QString kGpsResultsHost = QStringLiteral("pp-meta-gps-results-host");
inline const QString kGpsRule = QStringLiteral("pp-meta-gps-rule");
inline const QString kGpsRuleToggle = QStringLiteral("pp-meta-gps-rule-toggle");
inline const QString kGpsSearchButton = QStringLiteral("pp-meta-gps-search-button");
inline const QString kGpsSearchEdit = QStringLiteral("pp-meta-gps-search-edit");
inline const QString kGpsSearchResults = QStringLiteral("pp-meta-gps-search-results");
inline const QString kGpsSearchRow = QStringLiteral("pp-meta-gps-search-row");
inline const QString kGpsSuggest = QStringLiteral("pp-meta-gps-suggest");
inline const QString kInfoCamera = QStringLiteral("pp-meta-info-camera");
inline const QString kInfoColor = QStringLiteral("pp-meta-info-color");
inline const QString kInfoEmpty = QStringLiteral("pp-meta-info-empty");
inline const QString kInfoEmptyRow = QStringLiteral("pp-meta-info-empty-row");
inline const QString kInfoExposure = QStringLiteral("pp-meta-info-exposure");
inline const QString kInfoFile = QStringLiteral("pp-meta-info-file");
inline const QString kInfoGps = QStringLiteral("pp-meta-info-gps");
inline const QString kInfoGpsPill = QStringLiteral("pp-meta-info-gps-pill");
inline const QString kInfoHint = QStringLiteral("pp-meta-info-hint");
inline const QString kInfoLens = QStringLiteral("pp-meta-info-lens");
inline const QString kInfoPill = QStringLiteral("pp-meta-info-pill");
inline const QString kInfoRating = QStringLiteral("pp-meta-info-rating");
inline const QString kInfoRow1 = QStringLiteral("pp-meta-info-row-%1");
inline const QString kInfoSize = QStringLiteral("pp-meta-info-size");
inline const QString kInfoTime = QStringLiteral("pp-meta-info-time");
inline const QString kInfoWb = QStringLiteral("pp-meta-info-wb");
inline const QString kKeyword1 = QStringLiteral("pp-meta-keyword-%1");
inline const QString kKeywordMore = QStringLiteral("pp-meta-keyword-more");
inline const QString kKeywordNone = QStringLiteral("pp-meta-keyword-none");
inline const QString kKeywordsLink = QStringLiteral("pp-meta-keywords-link");
inline const QString kMap = QStringLiteral("pp-meta-map");
inline const QString kMapCursor = QStringLiteral("pp-meta-map-cursor");
inline const QString kMapExpand = QStringLiteral("pp-meta-map-expand");
inline const QString kMapLayerPlate = QStringLiteral("pp-meta-map-layer-plate");
inline const QString kMapLoading = QStringLiteral("pp-meta-map-loading");
inline const QString kMapZoomIn = QStringLiteral("pp-meta-map-zoom-in");
inline const QString kMapZoomOut = QStringLiteral("pp-meta-map-zoom-out");
inline const QString kMulti = QStringLiteral("pp-meta-multi");
inline const QString kMultiArrow = QStringLiteral("pp-meta-multi-arrow");
inline const QString kMultiMore = QStringLiteral("pp-meta-multi-more");
inline const QString kMultiName1 = QStringLiteral("pp-meta-multi-name-%1");
inline const QString kMultiNew1 = QStringLiteral("pp-meta-multi-new-%1");
inline const QString kMultiOld1 = QStringLiteral("pp-meta-multi-old-%1");
inline const QString kPrivacyEnable = QStringLiteral("pp-meta-privacy-enable");
inline const QString kPrivacyPill = QStringLiteral("pp-meta-privacy-pill");
inline const QString kSeg = QStringLiteral("pp-meta-seg");
inline const QString kSegDelta = QStringLiteral("pp-meta-seg-delta");
inline const QString kSegTz = QStringLiteral("pp-meta-seg-tz");
inline const QString kTagHost = QStringLiteral("pp-meta-tag-host");
inline const QString kTimeEmpty = QStringLiteral("pp-meta-time-empty");
inline const QString kTimeEmptyRow = QStringLiteral("pp-meta-time-empty-row");
inline const QString kTimeEnable = QStringLiteral("pp-meta-time-enable");
inline const QString kTimeHint = QStringLiteral("pp-meta-time-hint");
inline const QString kTimeNew0 = QStringLiteral("pp-meta-time-new-0");
inline const QString kTimeNote0 = QStringLiteral("pp-meta-time-note-0");
inline const QString kTimeOld0 = QStringLiteral("pp-meta-time-old-0");
inline const QString kTimePill = QStringLiteral("pp-meta-time-pill");
inline const QString kTimeSrc0 = QStringLiteral("pp-meta-time-src-0");
inline const QString kTzFrom = QStringLiteral("pp-meta-tz-from");
inline const QString kTzRow = QStringLiteral("pp-meta-tz-row");
inline const QString kTzTo = QStringLiteral("pp-meta-tz-to");
inline const QString kTagAddButton = QStringLiteral("tag_add_button");
inline const QString kTagKey = QStringLiteral("tag_key");
inline const QString kTagRemove = QStringLiteral("tag_remove");
inline const QString kTagRow = QStringLiteral("tag_row");
inline const QString kTagRowsHost = QStringLiteral("tag_rows_host");
inline const QString kTagValue = QStringLiteral("tag_value");
} // namespace objname

// R-180：常驻子控件**显式标记**（重复入列/误删 = 悬垂；原约定只活在注释里 → 值标记 + 断言）
inline constexpr const char *kResidentProperty = "pp_meta_resident";
inline void mark_resident(QWidget *w) {
    if (w != nullptr) {
        w->setProperty(kResidentProperty, true);
    }
}
inline bool is_resident(const QWidget *w) {
    return w != nullptr && w->property(kResidentProperty).toBool();
}

// —— 探针侧只读访问器（additive；不引 MetaState —— 只读页面动态属性与子控件）——
struct ProbeContext {
    const PageMeta *page = nullptr;

    explicit ProbeContext(const PageMeta *p) : page(p) {}

    QVariant prop(const char *name) const {
        return page != nullptr ? page->property(name) : QVariant();
    }
    int follow_count() const { return prop(hook::kFollowCount).toInt(); }
    QString follow_name() const { return prop(hook::kFollowName).toString(); }
    bool follow_pending() const { return prop(hook::kFollowPending).toBool(); }
    int follow_stale_dropped() const { return prop(hook::kFollowStaleDropped).toInt(); }
    QString time_new() const { return prop(hook::kTimeNew).toString(); }
    QString gps_effective() const { return prop(hook::kGpsEffective).toString(); }
    int multi_rows() const { return prop(hook::kMultiRows).toInt(); }
    QString gps_layer() const { return prop(hook::kGpsLayer).toString(); }
    int gps_results() const { return prop(hook::kGpsResults).toInt(); }
    int gps_suggest() const { return prop(hook::kGpsSuggest).toInt(); }
    QString gps_address() const { return prop(hook::kGpsAddress).toString(); }
    bool gps_dms() const { return prop(hook::kGpsDms).toBool(); }

    // objectName 查找（词表入参；宿主不再散写字符串）
    template <typename T> T *child(const QString &object_name) const {
        return page != nullptr ? page->findChild<T *>(object_name) : nullptr;
    }
    template <typename T> QList<T *> children(const QString &object_name) const {
        return page != nullptr ? page->findChildren<T *>(object_name) : QList<T *>();
    }
};

} // namespace pp::ui::meta
