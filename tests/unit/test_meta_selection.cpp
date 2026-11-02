// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M13（0.4.1）— 选择项级聚合 / 字段级合并纯函数单测（C2.7 / C6）
//
// Contract: docs/m13-tasks.md C2（4.2 聚合语义 / 4.4 提交语义 / 4.6 预设）+ C6（单测只增）。
// 证据面：
//   * 三态聚合 absent / uniform / mixed（含 labels 逐文件文本、caps）；
//   * 卡级 active（flag==true / 字段存在 / 半开）+ 选择集 on/off/partial 聚合；
//   * 字段级提交不串扰：只改 Δ days 不动 minutes/模式/时区；
//   * 按文件合并保留各自其它分量（两文件不同 Δ，各改各的 days）；
//   * 卡关闭清除该卡全部字段（时间/隐私/GPS）；GpsClear 语义；
//   * GPS lat/lon 两值都成功才落、保留 base 的 alt/dir/ts；无 GPS 点不创造点；
//   * tags 合并（逐键覆盖 + 删除键 + 其他文件项保留）；
//   * 空 override 归一 nullopt；预设聚合（uniform 入规则、全 active 才 *_enabled）；
//   * 只读值文本聚合（uniform/mixed/「…还有 N 个」）。
//
// 测试接线（test_meta_format 同款先例）：被测 TU = src/ui/meta_selection.cpp（纯 QtCore），
// 由 CMake 的 pp_test_meta_selection 目标单独编译；本文件只 include 头并断言。
// 每个失败打印 "FAIL <case>: <detail>"；main() 返回失败数（0 = 全绿）。
#include "ui/meta_selection.h"

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace meta = pp::ui::meta;
using meta::Aggregate;
using meta::EditField;
using pp::MetadataOverride;
using pp::TimeShift;

namespace {

// 本单测是**测试断言面**：先 `check(x.has_value())` 再取 `*x` 的结构化断言贯穿全文，
// clang-tidy 的 bugprone-unchecked-optional-access 看不到 check() 里的前提（跨函数），会逐点
// 误报。此处按文件级豁免并在报告/裁定登记（A17）：被测语义正确性由断言本身在运行时保证。
// NOLINTBEGIN(bugprone-unchecked-optional-access)

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
        std::fflush(stdout);
    }
}

meta::SelectionSnapshotList selection(std::vector<std::optional<MetadataOverride>> exceptions) {
    meta::SelectionSnapshotList out;
    for (std::size_t i = 0; i < exceptions.size(); ++i) {
        meta::SelectionSnapshot item;
        item.path = QStringLiteral("/photos/file%1.jpg").arg(i + 1);
        item.exception = std::move(exceptions[i]);
        out.push_back(std::move(item));
    }
    return out;
}

MetadataOverride time_override(std::optional<TimeShift> shift) {
    MetadataOverride ov;
    ov.time_shift = shift;
    return ov;
}

MetadataOverride gps_override(const pp::GpsData &gps) {
    MetadataOverride ov;
    ov.gps = gps;
    return ov;
}

meta::FieldInput input_int(int value) {
    meta::FieldInput in;
    in.value = value;
    return in;
}

// ---------------------------------------------------------------------------
// 1) 三态聚合（absent / uniform / mixed + labels）
// ---------------------------------------------------------------------------
void test_aggregation() {
    { // absent：全部文件都未设置
        const auto sel = selection({std::nullopt, std::nullopt});
        const auto agg = meta::time_component_aggregate(sel, 2);
        check(agg.state == Aggregate::Absent, "aggregate/absent",
              "state=" + std::to_string(int(agg.state)));
        check(agg.present == 0 && agg.total == 2, "aggregate/absent-count", "present!=0");
        check(meta::time_mode_aggregate(sel).state == Aggregate::Absent, "aggregate/absent-mode",
              "mode not absent");
    }
    { // uniform：两文件都 +1d（其它分量同为 0）
        TimeShift shift;
        shift.days = 1;
        const auto sel = selection({time_override(shift), time_override(shift)});
        const auto agg = meta::time_component_aggregate(sel, 2);
        check(agg.state == Aggregate::Uniform, "aggregate/uniform", "state != uniform");
        check(agg.value == 1, "aggregate/uniform-value", "value != 1");
        check(agg.labels.size() == 2, "aggregate/uniform-labels", "labels != 2");
        check(agg.labels.at(0) == QStringLiteral("file1.jpg: 1"), "aggregate/uniform-label0",
              ("label=" + agg.labels.at(0).toStdString()).c_str());
    }
    { // mixed：+1d 与 +2d（只 days 不同）
        TimeShift a;
        a.days = 1;
        TimeShift b;
        b.days = 2;
        const auto sel = selection({time_override(a), time_override(b)});
        const auto agg = meta::time_component_aggregate(sel, 2);
        check(agg.state == Aggregate::Mixed, "aggregate/mixed", "state != mixed");
        check(agg.present == 2 && agg.labels.size() == 2, "aggregate/mixed-labels", "labels");
        // 其它分量仍 uniform（mixed 只落在被编辑字段上）
        check(meta::time_component_aggregate(sel, 4).value == 0, "aggregate/mixed-scope",
              "minutes should stay uniform 0");
        check(meta::time_component_aggregate(sel, 4).state == Aggregate::Uniform,
              "aggregate/mixed-scope-state", "minutes state != uniform");
    }
    { // 部分文件设置（present>0 但未覆盖全部）→ mixed（C2.2「至少两值不同」）
        TimeShift shift;
        shift.days = 3;
        const auto sel = selection({time_override(shift), std::nullopt});
        check(meta::time_component_aggregate(sel, 2).state == Aggregate::Mixed,
              "aggregate/partial-mixed", "partial should be mixed");
    }
    { // 空选择 → absent（不进卡片装载）
        const auto sel = selection({});
        check(meta::time_mode_aggregate(sel).state == Aggregate::Absent, "aggregate/empty",
              "empty selection not absent");
    }
}

// ---------------------------------------------------------------------------
// 2) 卡级 active（flag==true / 字段存在 / 半开）与选择集三态
// ---------------------------------------------------------------------------
void test_card_active() {
    check(!meta::card_active(std::nullopt, EditField::TimeEnable), "card/absent",
          "nullopt should be off");
    { // 字段存在 = active（flag 未设）
        TimeShift shift;
        shift.hours = 2;
        const MetadataOverride ov = time_override(shift);
        check(meta::card_active(ov, EditField::TimeEnable), "card/field-active",
              "field should activate card");
    }
    { // flag==true 且无字段 = active（开关打开但暂无值）
        MetadataOverride ov;
        ov.time_enabled = true;
        check(meta::card_active(ov, EditField::TimeEnable), "card/flag-true",
              "flag true should activate");
    }
    { // flag==false 且无编辑 = off（半开）
        MetadataOverride ov;
        ov.time_enabled = false;
        check(!meta::card_active(ov, EditField::TimeEnable), "card/flag-false",
              "half-open should be off");
    }
    { // 选择集 on/off → partial 聚合
        TimeShift shift;
        shift.days = 1;
        MetadataOverride on = time_override(shift);
        MetadataOverride off;
        off.time_enabled = false;
        const auto sel = selection({on, off});
        const auto agg = meta::card_active_aggregate(sel, EditField::TimeEnable);
        check(agg.state == Aggregate::Mixed, "card/partial-mixed", "state != mixed");
        const auto all_on = selection({on, on});
        check(meta::card_active_aggregate(all_on, EditField::TimeEnable).state ==
                      Aggregate::Uniform &&
                  meta::card_active_aggregate(all_on, EditField::TimeEnable).value,
              "card/all-on", "not uniform true");
    }
}

// ---------------------------------------------------------------------------
// 3) 字段级合并不串扰（只改 days：minutes/mode/tz 保持各文件原值）
// ---------------------------------------------------------------------------
void test_merge_no_crosstalk() {
    TimeShift a;
    a.days = 1;
    a.minutes = 30;
    a.mode = TimeShift::Mode::Delta;
    const std::optional<MetadataOverride> current = time_override(a);
    const auto merged = meta::merge_field_edit(current, EditField::TimeDays, input_int(3));
    check(merged.has_value(), "merge/days-present", "merged is nullopt");
    check(merged->time_shift->days == 3, "merge/days-value", "days != 3");
    check(merged->time_shift->minutes == 30, "merge/days-keeps-minutes", "minutes lost");
    check(merged->time_shift->mode == TimeShift::Mode::Delta, "merge/days-keeps-mode", "mode lost");
    check(!merged->time_enabled.has_value(), "merge/days-no-flag", "flag should stay unset");
    // 时区分量同样不被 Δ 提交串扰
    TimeShift tz;
    tz.mode = TimeShift::Mode::TimezoneSemantic;
    tz.from_offset_min = 480;
    tz.to_offset_min = 60;
    std::optional<MetadataOverride> tz_current = time_override(tz);
    const auto merged_tz = meta::merge_field_edit(tz_current, EditField::TimeHours, input_int(-2));
    check(merged_tz->time_shift->mode == TimeShift::Mode::TimezoneSemantic, "merge/tz-mode-kept",
          "mode changed");
    check(merged_tz->time_shift->from_offset_min == 480 &&
              merged_tz->time_shift->to_offset_min == 60,
          "merge/tz-offsets-kept", "tz offsets changed");
    check(merged_tz->time_shift->hours == -2, "merge/tz-hours-set", "hours != -2");
}

// ---------------------------------------------------------------------------
// 4) 按文件合并：两文件各自的其它分量保留，只被编辑字段统一覆盖
// ---------------------------------------------------------------------------
void test_per_file_merge() {
    TimeShift a;
    a.days = 1;
    a.minutes = 5;
    TimeShift b;
    b.days = 2;
    b.minutes = 50;
    std::optional<MetadataOverride> ov_a = time_override(a);
    std::optional<MetadataOverride> ov_b = time_override(b);
    const auto merged_a = meta::merge_field_edit(ov_a, EditField::TimeDays, input_int(7));
    const auto merged_b = meta::merge_field_edit(ov_b, EditField::TimeDays, input_int(7));
    check(merged_a->time_shift->days == 7 && merged_b->time_shift->days == 7, "per-file/days-both",
          "days not both 7");
    check(merged_a->time_shift->minutes == 5, "per-file/a-minutes-kept", "a minutes lost");
    check(merged_b->time_shift->minutes == 50, "per-file/b-minutes-kept", "b minutes lost");
}

// ---------------------------------------------------------------------------
// 5) 卡开关：false = 清除该卡全部字段；true = 只置 flag（值不变）
// ---------------------------------------------------------------------------
void test_card_switch() {
    meta::FieldInput off;
    off.flag = false;
    meta::FieldInput on;
    on.flag = true;
    { // 时间卡关闭：time_shift 清除 + time_enabled=false；其它卡字段不动
        TimeShift shift;
        shift.days = 4;
        MetadataOverride ov = time_override(shift);
        ov.gps_enabled = true;
        const auto merged =
            meta::merge_field_edit(std::optional<MetadataOverride>(ov), EditField::TimeEnable, off);
        check(merged.has_value(), "card-off/kept-gps-flag", "override vanished");
        check(!merged->time_shift.has_value(), "card-off/time-cleared", "time_shift not cleared");
        check(merged->time_enabled.value_or(true) == false, "card-off/flag-false",
              "flag not false");
        check(merged->gps_enabled.value_or(false) == true, "card-off/gps-untouched",
              "gps flag changed");
    }
    { // 时间卡打开：flag=true，已有值保留
        TimeShift shift;
        shift.days = 4;
        const auto merged = meta::merge_field_edit(
            std::optional<MetadataOverride>(time_override(shift)), EditField::TimeEnable, on);
        check(merged->time_enabled.value_or(false) == true, "card-on/flag-true", "flag not true");
        check(merged->time_shift.has_value() && merged->time_shift->days == 4, "card-on/value-kept",
              "value lost");
    }
    { // 隐私卡关闭：隐私字段 + tag 编辑全清（C1.1 注：UI 关闭卡时主动清 tag）
        MetadataOverride ov;
        ov.strip_privacy = true;
        ov.sync_mtime = true;
        ov.modernize_exif = true;
        ov.strip_creator_tool = true;
        ov.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("x"), false});
        ov.privacy_enabled = true;
        const auto merged = meta::merge_field_edit(std::optional<MetadataOverride>(ov),
                                                   EditField::PrivacyEnable, off);
        check(merged.has_value() && !merged->exif_edits.empty() == false, "card-off/tags-cleared",
              "tag edits not cleared");
        check(!merged->strip_privacy.has_value() && !merged->sync_mtime.has_value() &&
                  !merged->modernize_exif.has_value() && !merged->strip_creator_tool.has_value(),
              "card-off/privacy-cleared", "privacy fields not cleared");
        check(merged->privacy_enabled.value_or(true) == false, "card-off/privacy-flag",
              "privacy flag not false");
    }
    { // GPS 卡关闭：gps + gps_clear 清除
        pp::GpsData gps;
        gps.lat = 31.2;
        gps.lon = 121.4;
        MetadataOverride ov = gps_override(gps);
        ov.gps_clear = true;
        const auto merged =
            meta::merge_field_edit(std::optional<MetadataOverride>(ov), EditField::GpsEnable, off);
        check(merged.has_value() && !merged->gps.has_value() && !merged->gps_clear,
              "card-off/gps-cleared", "gps not cleared");
    }
}

// ---------------------------------------------------------------------------
// 5b) 新建 time_shift 的种子 = 控件显示值（C2.4 显示=写入）：只改一个 Δ 分量时，
//     其余分量/模式取 seed（界面所见），已有 shift 的文件仍保持各自其它分量。
// ---------------------------------------------------------------------------
void test_time_seed() {
    meta::FieldInput in;
    in.value = 4;
    pp::TimeShift seed;
    seed.days = 1;
    seed.minutes = 30;
    seed.mode = pp::TimeShift::Mode::Delta;
    in.seed_time = seed;
    const auto merged = meta::merge_field_edit(std::nullopt, EditField::TimeHours, in);
    check(merged.has_value() && merged->time_shift.has_value(), "seed/present", "shift missing");
    check(merged->time_shift->hours == 4, "seed/edited-field", "hours not set");
    check(merged->time_shift->days == 1 && merged->time_shift->minutes == 30, "seed/display-values",
          "seed components lost");
    // 已有 shift 的文件：seed 不覆盖其它分量（只改被编辑字段）
    TimeShift existing;
    existing.hours = 2;
    existing.minutes = 50;
    const auto kept = meta::merge_field_edit(
        std::optional<MetadataOverride>(time_override(existing)), EditField::TimeHours, in);
    check(kept->time_shift->minutes == 50, "seed/existing-kept", "existing minutes changed");
    // 时区偏移首次落值：另一偏移取 seed（否则界面 UTC+8 而 override 落 0）
    meta::FieldInput tz_in;
    tz_in.value = 60;
    pp::TimeShift tz_seed;
    tz_seed.from_offset_min = 480;
    tz_seed.mode = pp::TimeShift::Mode::TimezoneSemantic;
    tz_in.seed_time = tz_seed;
    const auto tz_merged = meta::merge_field_edit(std::nullopt, EditField::TimeTzTo, tz_in);
    check(tz_merged->time_shift->to_offset_min == 60 &&
              tz_merged->time_shift->from_offset_min == 480 &&
              tz_merged->time_shift->mode == pp::TimeShift::Mode::TimezoneSemantic,
          "seed/tz-from", "tz seed not applied");
}

// ---------------------------------------------------------------------------
// 6) 隐私四开关逐字段提交
// ---------------------------------------------------------------------------
void test_privacy_flags() {
    meta::FieldInput on;
    on.flag = true;
    const auto merged = meta::merge_field_edit(std::nullopt, EditField::PrivacyCreatorTool, on);
    check(merged.has_value() && merged->strip_creator_tool.value_or(false) == true,
          "privacy/creator-tool", "strip_creator_tool not set");
    const auto merged2 = meta::merge_field_edit(merged, EditField::PrivacyStrip, on);
    check(merged2->strip_privacy.value_or(false) == true, "privacy/strip", "strip not set");
    check(merged2->strip_creator_tool.value_or(false) == true, "privacy/prior-kept",
          "previous flag lost");
}

// ---------------------------------------------------------------------------
// 7) GPS 分量：lat/lon 两值成功才落、保留 base 的 alt/dir/ts；无点不创造点
// ---------------------------------------------------------------------------
void test_gps_merge() {
    pp::GpsData source;
    source.lat = 10.0;
    source.lon = 20.0;
    source.altitude = 12.5;
    source.direction = 90.0;
    source.timestamp = std::string("2024:03:01 10:00:00");
    { // 只给 lat -> 不提交（has_lat_lon=false）
        meta::FieldInput in;
        in.has_lat_lon = false;
        in.lat = 31.0;
        const auto merged = meta::merge_gps_field_edit(
            std::optional<MetadataOverride>(gps_override(source)), source, EditField::GpsLat, in);
        check(merged->gps.has_value() && merged->gps->lat == 10.0, "gps/lat-not-both",
              "lat should be unchanged");
    }
    { // 两值成功：lat/lon 覆盖，alt/dir/ts 保留（base = 生效 GPS）
        meta::FieldInput in;
        in.has_lat_lon = true;
        in.lat = 31.2304;
        in.lon = 121.4737;
        const auto merged = meta::merge_gps_field_edit(
            std::optional<MetadataOverride>(gps_override(source)), source, EditField::GpsLat, in);
        check(merged->gps->lat == 31.2304 && merged->gps->lon == 121.4737, "gps/latlon-set",
              "lat/lon not set");
        check(merged->gps->altitude.has_value() && *merged->gps->altitude == 12.5,
              "gps/alt-preserved", "altitude lost");
        check(merged->gps->direction.has_value() && *merged->gps->direction == 90.0,
              "gps/dir-preserved", "direction lost");
        check(merged->gps->timestamp.has_value(), "gps/ts-preserved", "timestamp lost");
    }
    { // GpsAlt：无 GPS 点（current/base 都空）→ 忽略、不创造点
        meta::FieldInput in;
        in.altitude = 100.0;
        const auto merged =
            meta::merge_gps_field_edit(std::nullopt, std::nullopt, EditField::GpsAlt, in);
        check(!merged.has_value(), "gps/alt-no-point", "should not create override");
        // 有 base 点 → 写 altitude
        const auto merged2 =
            meta::merge_gps_field_edit(std::nullopt, source, EditField::GpsAlt, in);
        check(merged2.has_value() && merged2->gps->altitude.has_value() &&
                  *merged2->gps->altitude == 100.0,
              "gps/alt-set", "altitude not set");
    }
    { // GpsClear：true = gps_clear + gps.reset；false = 只清 flag
        meta::FieldInput clear;
        clear.flag = true;
        const auto merged = meta::merge_field_edit(
            std::optional<MetadataOverride>(gps_override(source)), EditField::GpsClear, clear);
        check(merged->gps_clear && !merged->gps.has_value(), "gps/clear", "clear semantics wrong");
        const auto unset = meta::merge_field_edit(merged, EditField::GpsClear, meta::FieldInput{});
        // gps_clear 清掉后该 override 已空 → 归一 nullopt（C2.1），两种形态都算"已清除"
        check(!unset.has_value() || !unset->gps_clear, "gps/clear-unset", "gps_clear not cleared");
    }
}

// ---------------------------------------------------------------------------
// 8) Tags 合并（逐键覆盖 + 删除键 + 保留其它既有项）
// ---------------------------------------------------------------------------
void test_tags_merge() {
    MetadataOverride ov;
    ov.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("old"), false});
    ov.exif_edits.push_back(pp::TagEdit{"Exif.Image.Copyright", std::string("keep"), false});
    std::vector<pp::TagEdit> tags;
    tags.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("new"), false});
    tags.push_back(pp::TagEdit{"Xmp.dc.title", std::string("title"), false});
    const auto merged =
        meta::apply_tags_edit(std::optional<MetadataOverride>(ov), tags, {"Exif.Image.Copyright"});
    check(merged.has_value(), "tags/present", "merged nullopt");
    check(merged->exif_edits.size() == 1, "tags/exif-count", "exif_edits size != 1");
    check(merged->exif_edits[0].key == "Exif.Image.Artist" &&
              merged->exif_edits[0].value.has_value() && *merged->exif_edits[0].value == "new",
          "tags/overwrite", "artist not overwritten");
    check(merged->xmp_edits.size() == 1 && merged->xmp_edits[0].key == "Xmp.dc.title",
          "tags/xmp-add", "xmp edit missing");
    // 空编辑表 + 无删除 → 保持原样
    const auto kept = meta::apply_tags_edit(std::optional<MetadataOverride>(ov), {}, {});
    check(kept->exif_edits.size() == 2, "tags/keep-others", "existing edits lost");
}

// ---------------------------------------------------------------------------
// 9) 空 override 归一 nullopt（C2.1）
// ---------------------------------------------------------------------------
void test_normalize() {
    MetadataOverride empty;
    check(meta::override_is_empty(empty), "normalize/empty", "default override not empty");
    check(!meta::normalize_override(empty).has_value(), "normalize/nullopt", "not normalized");
    MetadataOverride flag_only;
    flag_only.time_enabled = false;
    check(!meta::override_is_empty(flag_only), "normalize/flag-not-empty",
          "flag-only considered empty");
    check(meta::normalize_override(flag_only).has_value(), "normalize/keep-flag", "flag lost");
    // 卡关闭清空后 → 归一 nullopt（除 flag=false 本身，故仍非空）
    MetadataOverride off_only;
    off_only.time_enabled = false;
    check(meta::normalize_override(off_only).has_value(), "normalize/half-open-kept",
          "flag=false should persist");
}

// ---------------------------------------------------------------------------
// 10) 预设聚合（uniform 入规则；*_enabled 全 active 才 true；mixed 留空）
// ---------------------------------------------------------------------------
void test_preset_aggregation() {
    { // 两文件同值 + 同 active → 规则全量
        TimeShift shift;
        shift.days = 1;
        MetadataOverride ov = time_override(shift);
        ov.strip_privacy = true;
        ov.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("a"), false});
        const auto rules = meta::batch_rules_from_selection(selection({ov, ov}));
        check(rules.time_enabled, "preset/time-enabled", "time_enabled should be true");
        check(rules.time_shift.has_value() && rules.time_shift->days == 1, "preset/time-value",
              "time_shift missing");
        check(rules.privacy_enabled, "preset/privacy-enabled", "privacy_enabled should be true");
        check(rules.strip_privacy, "preset/strip", "strip_privacy missing");
        check(rules.exif_edits.size() == 1, "preset/tags", "tag edits missing");
    }
    { // mixed 值 → 留空；非全 active → *_enabled=false
        TimeShift a;
        a.days = 1;
        TimeShift b;
        b.days = 2;
        MetadataOverride ov_a = time_override(a);
        MetadataOverride ov_b = time_override(b);
        ov_b.time_enabled = false;
        const auto rules = meta::batch_rules_from_selection(selection({ov_a, ov_b}));
        check(!rules.time_enabled, "preset/not-all-active", "time_enabled should be false");
        check(!rules.time_shift.has_value(), "preset/mixed-empty", "mixed value entered rule");
    }
    { // 空选择 → 全部卡关闭（无规则可存）
        const auto rules = meta::batch_rules_from_selection(selection({}));
        check(!rules.time_enabled && !rules.gps_enabled && !rules.privacy_enabled,
              "preset/empty-selection", "cards should be off");
    }
}

// ---------------------------------------------------------------------------
// 11) 只读值文本聚合（C2.5：uniform → 值；mixed → 多个值 + tooltip）
// ---------------------------------------------------------------------------
void test_text_aggregate() {
    const auto sel = selection({std::nullopt, std::nullopt, std::nullopt});
    { // uniform
        const auto agg = meta::aggregate_texts(
            sel, {QStringLiteral("Canon"), QStringLiteral("Canon"), QStringLiteral("Canon")});
        check(!agg.empty && agg.uniform, "text/uniform", "uniform flag wrong");
        check(meta::aggregate_display(agg, QStringLiteral("多个值")) == QStringLiteral("Canon"),
              "text/uniform-value", "display != Canon");
        check(meta::aggregate_tooltip(agg).isEmpty(), "text/uniform-tooltip",
              "uniform tooltip should be empty");
    }
    { // mixed
        const auto agg = meta::aggregate_texts(
            sel, {QStringLiteral("Canon"), QStringLiteral("Nikon"), QStringLiteral("Canon")});
        check(!agg.uniform, "text/mixed", "should be mixed");
        check(meta::aggregate_display(agg, QStringLiteral("多个值")) == QStringLiteral("多个值"),
              "text/mixed-display", "display != 多个值");
        check(meta::aggregate_tooltip(agg).contains(QStringLiteral("file2.jpg")),
              "text/mixed-tooltip", "tooltip missing file2");
    }
    { // 截断：cap=2，3 文件 → 「…还有 1 个」计入 omitted
        const auto agg = meta::aggregate_texts(
            sel, {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, 2);
        check(agg.omitted == 1, "text/omitted", "omitted != 1");
        check(meta::aggregate_tooltip(agg).contains(QStringLiteral("还有 1 个")),
              "text/tooltip-omitted", "omitted line missing");
    }
    { // 空选择
        const meta::TextAggregate empty;
        check(empty.empty && meta::aggregate_display(empty, QStringLiteral("多个值")).isEmpty(),
              "text/empty", "empty aggregate display not empty");
    }
}

// ---------------------------------------------------------------------------
// 12) 0.4.2（需求 1/2）：选择项**规模**不影响聚合判定（>8 的选择：同值/异值判定是纯函数
//     语义，不受多选小表 8 行截断影响；只有 tooltip labels 截断 + 「…还有 N 个」）
// ---------------------------------------------------------------------------
void test_selection_scale() {
    // 12 个文件、同值 → Uniform（不是 mixed、不是 absent）
    std::vector<std::optional<MetadataOverride>> all_same;
    for (int i = 0; i < 12; ++i) {
        TimeShift shift;
        shift.hours = 2;
        all_same.push_back(time_override(shift));
    }
    const auto sel_same = selection(all_same);
    const auto same = meta::time_component_aggregate(sel_same, 3);
    check(same.state == Aggregate::Uniform && same.value == 2, "scale/uniform-12",
          "12 uniform not detected");
    check(same.total == 12 && same.present == 12, "scale/uniform-12-counts", "counts wrong");
    check(same.labels.size() == meta::kAggregateLabelCap && same.omitted == 12 - 8,
          "scale/label-cap", "labels cap/omitted wrong");
    // 12 个文件：11 个 +2h、1 个 +3h → mixed（异值不因截断被漏）
    auto mixed = all_same;
    TimeShift other;
    other.hours = 3;
    mixed[11] = time_override(other);
    const auto sel_mixed = selection(mixed);
    check(meta::time_component_aggregate(sel_mixed, 3).state == Aggregate::Mixed, "scale/mixed-12",
          "12th differing value missed");
    // 只读文本聚合同判据（关键信息卡多选 >8 的判定面）
    QStringList texts;
    for (int i = 0; i < 12; ++i) {
        texts << QStringLiteral("Canon");
    }
    const auto agg_same = meta::aggregate_texts(sel_same, texts);
    check(agg_same.uniform && meta::aggregate_display(agg_same, QStringLiteral("多个值")) ==
                                  QStringLiteral("Canon"),
          "scale/text-uniform-12", "12-item text uniform failed");
    texts[11] = QStringLiteral("Nikon");
    const auto agg_mixed = meta::aggregate_texts(sel_mixed, texts);
    check(!agg_mixed.uniform && meta::aggregate_display(agg_mixed, QStringLiteral("多个值")) ==
                                    QStringLiteral("多个值"),
          "scale/text-mixed-12", "12-item text mixed failed");
    check(meta::aggregate_tooltip(agg_mixed).contains(QStringLiteral("还有 4 个")),
          "scale/text-mixed-12-omitted", "omitted line wrong");
    // 空选择 = Absent（所有字段）
    const auto empty = selection({});
    check(meta::time_component_aggregate(empty, 0).state == Aggregate::Absent &&
              meta::gps_clear_aggregate(empty).state == Aggregate::Absent &&
              meta::tags_aggregate(empty).state == Aggregate::Absent &&
              meta::card_active_aggregate(empty, EditField::TimeEnable).state == Aggregate::Absent,
          "scale/empty-absent", "empty selection not absent");
}

// ---------------------------------------------------------------------------
// 13) 0.4.2（需求 1/2）：卡头「启用」开关三态（全开 on / 全关 off / 部分 partial）
// ---------------------------------------------------------------------------
void test_card_tristate() {
    TimeShift shift;
    shift.days = 1;
    const MetadataOverride on = time_override(shift); // 有编辑 = active
    MetadataOverride off;                             // 无编辑 = off（nullopt 同）
    const auto sel_on = selection({on, on});
    const auto agg_on = meta::card_active_aggregate(sel_on, EditField::TimeEnable);
    check(agg_on.state == Aggregate::Uniform && agg_on.value, "tristate/all-on",
          "all-on not uniform true");
    const auto sel_off = selection({std::nullopt, off});
    const auto agg_off = meta::card_active_aggregate(sel_off, EditField::TimeEnable);
    check(agg_off.state == Aggregate::Uniform && !agg_off.value, "tristate/all-off",
          "all-off not uniform false");
    const auto sel_part = selection({on, std::nullopt});
    const auto agg_part = meta::card_active_aggregate(sel_part, EditField::TimeEnable);
    check(agg_part.state == Aggregate::Mixed, "tristate/partial", "partial not mixed");
    // 点开 = 对全部选中启用（不凭空造值）：只置 flag，不产生 time_shift
    meta::FieldInput enable;
    enable.flag = true;
    const auto enabled = meta::merge_field_edit(std::nullopt, EditField::TimeEnable, enable);
    check(enabled.has_value() && enabled->time_enabled.value_or(false) &&
              !enabled->time_shift.has_value(),
          "tristate/enable-no-value", "enable must not invent values");
}

} // namespace
// NOLINTEND(bugprone-unchecked-optional-access)

int main() { // NOLINT(bugprone-exception-escape) — 测试入口允许异常终止（非产品路径）
    test_aggregation();
    test_card_active();
    test_merge_no_crosstalk();
    test_per_file_merge();
    test_card_switch();
    test_time_seed();
    test_privacy_flags();
    test_gps_merge();
    test_tags_merge();
    test_normalize();
    test_preset_aggregation();
    test_text_aggregate();
    test_selection_scale(); // 0.4.2 需求 1/2：>8 选择聚合规模 / 空选择三态 Absent
    test_card_tristate();   // 0.4.2 需求 1/2：卡头启用三态 + 点开不造值
    if (g_failed == 0) {
        std::printf("test_meta_selection OK\n");
    }
    return g_failed;
}
