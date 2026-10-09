// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M5-T2 — formkit 纯几何单测（**零 Qt 依赖**）。
//
// 契约：docs/m5-tasks.md §1 C2「表单唯一入口」（pitch 28 / 标签轨 80 右对齐 / 列距 12 /
//   字段宽仅 88·160·flex / 双列 80·12·轨·24·80·12·轨 / 单位槽 16 / 范围注释 10px）
//   + docs/qt-gui-design-spec.md §5 R1–R4 与 §12 验收清单 1–4 条（行节奏、字段轨、
//   一行一义、选中零形变）。
// 只测 src/ui/formkit_layout.h 的常量与 constexpr 计算函数 —— 该头零 Qt 依赖，故本用例不建
//   QApplication、不 include 任何 widget 头；自绘控件的取值必须**复述**同一份常量（formkit.h
//   里的 static_assert 把复述值与 theme.h 令牌锁死，本用例把取值与规范逐条锁死）。
// 每个失败打印 "FAIL <case>: <detail>"；main() 返回失败数（0 = 全绿）。

#include <cstdio>
#include <string>

#include "ui/formkit_layout.h"

namespace fk = pp::ui::formkit;

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string num(long long v) { return std::to_string(v); }

// 期望值比对（失败时打印 got/期望，避免"只看失败行猜数值"）
void check_eq(long long got, long long want, const std::string &case_name) {
    check(got == want, case_name, "got " + num(got) + ", want " + num(want));
}

} // namespace

int main() {
    // ---- R1 行节奏（§5 R1）：卡内任何信息行 pitch 恒 28，行间无额外 gap ----
    check_eq(fk::kRowPitch, 28, "r1/pitch-28");
    check_eq(fk::kCardPadTop, 10, "r1/card-pad-top-10");
    check_eq(fk::kCardPadBottom, 12, "r1/card-pad-bottom-12");
    check_eq(fk::rows_height(0), 0, "r1/rows-height-0");
    check_eq(fk::rows_height(4), 112, "r1/rows-height-4x28");
    // 卡体高 = 上 10 + n×28 + 下 12（R1 逐字）
    check_eq(fk::card_body_h(5), 10 + 5 * 28 + 12, "r1/card-body-5-rows");
    check(fk::card_body_h(5) == fk::kCardPadTop + fk::rows_height(5) + fk::kCardPadBottom,
          "r1/card-body-composition", "card_body_h(n) != pad_top + rows + pad_bottom");

    // ---- R2 字段轨（§5 R2）：标签轨 80 右对齐 / 列距 12 / 字段轨起点 92 ----
    check_eq(fk::kLabelW, 80, "r2/label-track-80");
    check_eq(fk::kColGap, 12, "r2/col-gap-12");
    check_eq(fk::field_track_x(), 92, "r2/field-track-x-92");
    check_eq(fk::field_track_x() - fk::kLabelW, fk::kColGap, "r2/track-x-is-label-plus-gap");

    // ---- R2 字段宽三档：短 88 / 标准 160 / 长 flex（撑满轨）----
    check_eq(fk::kFieldShort, 88, "r2/field-short-88");
    check_eq(fk::kFieldStd, 160, "r2/field-std-160");
    check_eq(fk::field_w(fk::FieldWidth::Short), 88, "r2/field-w-short");
    check_eq(fk::field_w(fk::FieldWidth::Standard), 160, "r2/field-w-standard");
    // 定宽档"控件即轨道宽"（撑满轨 = 无浮空死区）：短/标准在任意轨内都取定值
    check_eq(fk::field_w_in_track(fk::FieldWidth::Short, 160), 88,
             "r2/short-in-wide-track-stays-88");
    check_eq(fk::field_w_in_track(fk::FieldWidth::Standard, 160), 160, "r2/std-fills-160-track");
    check_eq(fk::field_w_in_track(fk::FieldWidth::Standard, 88), 160,
             "r2/std-keeps-160-in-88-track");
    // 长档（flex）= 轨宽 − 单位槽/注释占位（每槽 16 + 行内距 6）
    check_eq(fk::kUnitSlotW, 16, "r3/unit-slot-16");
    check_eq(fk::kInlineGap, 6, "r3/inline-gap-6");
    check_eq(fk::flex_field_w(200), 200, "r2/flex-no-aux-equals-track");
    check_eq(fk::flex_field_w(200, 1), 200 - 22, "r2/flex-one-unit-slot");
    check_eq(fk::flex_field_w(200, 3), 200 - 3 * 22, "r2/flex-three-unit-slots");
    check_eq(fk::flex_field_w(200, 0, 30), 200 - 36, "r2/flex-with-range-note");
    check_eq(fk::field_w_in_track(fk::FieldWidth::Flex, 200), 200, "r2/flex-fills-track");
    check(fk::field_w_in_track(fk::FieldWidth::Flex, 200) != fk::field_w(fk::FieldWidth::Short) &&
              fk::field_w_in_track(fk::FieldWidth::Flex, 200) !=
                  fk::field_w(fk::FieldWidth::Standard),
          "r2/no-fourth-grade", "flex 档被当成了定宽档（88/160 之外的第四档）");

    // ---- R2 双列行：80 / 12 / 轨 / 24 / 80 / 12 / 轨（第二标签固定 x）----
    check_eq(fk::kDoubleGap, 24, "r2/double-gap-24");
    check_eq(fk::second_label_x(160), 80 + 12 + 160 + 24, "r2/second-label-x-std");
    check_eq(fk::second_label_x(88), 80 + 12 + 88 + 24, "r2/second-label-x-short");
    check_eq(fk::second_track_x(160), 80 + 12 + 160 + 24 + 80 + 12, "r2/second-track-x-std");
    check_eq(fk::double_row_width(160, 160), 80 + 12 + 160 + 24 + 80 + 12 + 160,
             "r2/double-row-total-std");
    check_eq(fk::double_row_width(88, 88), 80 + 12 + 88 + 24 + 80 + 12 + 88,
             "r2/double-row-total-short");
    // 逐档复核"第二标签起点 = 第一轨右缘 + 24"（第二标签不随内容漂移的结构保证）
    for (int track : {fk::kFieldShort, fk::kFieldStd, 200, 300}) {
        check_eq(fk::second_label_x(track) - (fk::field_track_x() + track), fk::kDoubleGap,
                 "r2/second-label-gap-24@" + num(track));
        check_eq(fk::second_track_x(track) - fk::second_label_x(track), fk::kLabelW + fk::kColGap,
                 "r2/second-track-follows-label@" + num(track));
    }
    // 双列标准轨整行宽 ≤ 右栏卡内可用宽（§3.2 右栏 587 − 2×1 卡边 − 2×12 卡内边距 = 561）
    check(fk::double_row_width(160, 160) <= 587 - 2 * 1 - 2 * 12, "r2/double-row-fits-right-col",
          "双列 160/160 未装进右栏卡内宽：" + num(fk::double_row_width(160, 160)));

    // ---- R3 提示位：单位入 16px 槽、范围入 10px 注释（都不占标签位）----
    check_eq(fk::kNotePx, 10, "r3/note-10px");
    // 单位槽起点 = 控件右缘 + 行内距（槽在字段轨内，不越到标签轨）
    check_eq(fk::unit_slot_x(fk::field_track_x(), 160), 92 + 160 + 6, "r3/unit-slot-x-std");
    check_eq(fk::unit_slot_x(fk::field_track_x(), 88), 92 + 88 + 6, "r3/unit-slot-x-short");
    check(fk::unit_slot_x(fk::field_track_x(), 160) > fk::field_track_x(),
          "r3/unit-slot-after-field", "单位槽落到了字段轨之前");

    // ---- R4 控件度量（§5 R4 表逐行；与 theme.h 令牌的一致性由 formkit.h 的 static_assert 锁）----
    check_eq(fk::kControlH, 24, "r4/control-h-24");
    check_eq(fk::kSpinArrowW, 17, "r4/spin-arrow-col-17");
    check_eq(fk::kSpinArrowH, 12, "r4/spin-arrow-12");
    check_eq(fk::kSpinPadX, 7, "r4/spin-pad-x-7");
    check_eq(fk::kComboArrowPx, 8, "r4/combo-arrow-8");
    check_eq(fk::kSegH, 24, "r4/seg-h-24");
    check_eq(fk::kSegW, 88, "r4/seg-w-88");
    check_eq(fk::kSegRadius, 3, "r4/seg-r3");
    // A27（T11 缺陷根治）：段本体高 = 容器高 − 上下各 1px 描边（22），**不得为 0** ——
    // 段（QAbstractButton 子类）无基类 sizeHint，布局曾把段算成 0 高 → 全仓分段控件不可见。
    // 段高自持 = sizeHint/fixedHeight 取同一算式（formkit.cpp 的 Segment）→ 取值单源在此。
    check_eq(fk::kSegBorder, 1, "r4/seg-border-1");
    check_eq(fk::seg_inner_h(), 22, "r4/seg-inner-h-22");
    check_eq(fk::seg_inner_h(), fk::kSegH - 2 * fk::kSegBorder, "r4/seg-inner-h-from-border");
    check(fk::seg_inner_h() > 0, "r4/seg-inner-h-positive",
          "段本体高被算成 0（A27 缺陷回归）：" + num(fk::seg_inner_h()));
    check(fk::seg_inner_h() < fk::kSegH, "r4/seg-inner-h-inside-container",
          "段本体高越出容器高：" + num(fk::seg_inner_h()));
    check_eq(fk::kSwitchW, 30, "r4/switch-w-30");
    check_eq(fk::kSwitchH, 16, "r4/switch-h-16");
    check_eq(fk::kSwitchKnob, 12, "r4/switch-knob-12");
    check_eq(fk::kCheckPx, 13, "r4/checkbox-13");
    check_eq(fk::kCheckRadius, 2, "r4/checkbox-r2");
    check_eq(fk::kCheckGap, 8, "r4/checkbox-gap-8");
    check_eq(fk::kBtnH, 26, "r4/button-h-26");
    check_eq(fk::kPillH, 16, "r4/pill-h-16");
    check_eq(fk::kPillRadius, 3, "r4/pill-r3");
    check_eq(fk::kPillPx10, 95, "r4/pill-px-9.5");
    check_eq(fk::kPillWeight, 700, "r4/pill-weight-700");
    check_eq(fk::kProgressH, 6, "r4/progress-h-6");
    check_eq(fk::kProgressRadius, 99, "r4/progress-r99");
    check_eq(fk::kIconBtn, 28, "r4/icon-button-28");
    check_eq(fk::kIconBtnRadius, 4, "r4/icon-button-r4");
    check_eq(fk::kTileH, 30, "r4/tile-h-30");
    check_eq(fk::kTileRadius, 3, "r4/tile-r3");
    check_eq(fk::kTilePadX, 9, "r4/tile-pad-x-9");
    check_eq(fk::kTileGap, 6, "r4/tile-gap-6");
    check_eq(fk::kChipH, 22, "r4/chip-h-22");
    check_eq(fk::kChipRadius, 3, "r4/chip-r3");
    check_eq(fk::kChipKeyPx, 13, "r4/chip-keycap-13");
    check_eq(fk::kChipPadX, 8, "r4/chip-pad-x-8");
    check_eq(fk::kChipGap, 4, "r4/chip-gap-4");

    // control_h 表（构造器与探针共用的同一张表）
    check_eq(fk::control_h(fk::Control::Spin), 24, "r4/control-h/spin");
    check_eq(fk::control_h(fk::Control::Combo), 24, "r4/control-h/combo");
    check_eq(fk::control_h(fk::Control::Input), 24, "r4/control-h/input");
    check_eq(fk::control_h(fk::Control::Segmented), 24, "r4/control-h/segmented");
    check_eq(fk::control_h(fk::Control::Button), 26, "r4/control-h/button");
    check_eq(fk::control_h(fk::Control::Switch), 16, "r4/control-h/switch");
    check_eq(fk::control_h(fk::Control::Checkbox), 13, "r4/control-h/checkbox");
    check_eq(fk::control_h(fk::Control::Pill), 16, "r4/control-h/pill");
    check_eq(fk::control_h(fk::Control::Chip), 22, "r4/control-h/chip");
    check_eq(fk::control_h(fk::Control::IconButton), 28, "r4/control-h/icon-button");
    check_eq(fk::control_h(fk::Control::Progress), 6, "r4/control-h/progress");
    check_eq(fk::control_h(fk::Control::Tile), 30, "r4/control-h/tile");

    // ---- §12-4 选中/悬停零形变的结构前提：等宽段与"勾选位固定"的取值 ----
    // 分段控件整宽 = 段数 × 88（等宽 → 选中不改宽）
    check_eq(fk::kSegW * 2, 176, "r12-4/seg-two-segments-width");
    check_eq(fk::kSegW * 3, 264, "r12-4/seg-three-segments-width");
    // A27：段组总高 = 容器高 = 段高 + 上下描边（段与容器同高口径，选中不改高也不改宽）
    check_eq(fk::seg_inner_h() + 2 * fk::kSegBorder, fk::kSegH, "r12-4/seg-inner-plus-border-is-h");
    // 磁贴对勾位 = 勾选框同口径 13×13（在固定宽里始终占位 → 勾选不改宽）
    check_eq(fk::kCheckPx, 13, "r12-4/tile-check-slot-reserved-13");
    check(fk::kTileH > fk::kCheckPx && fk::kTilePadX * 2 + fk::kCheckPx < fk::kTileH + 100,
          "r12-4/tile-slot-fits", "对勾位与磁贴高不自洽");

    // ---- 行内不越界：标签轨 + 列距 + 标准轨 ≤ 右栏卡内宽（R2 的可用性边界）----
    check(fk::field_track_x() + fk::kFieldStd <= 587 - 2 * 1 - 2 * 12, "r2/std-row-fits-right-col",
          "标准档单列行未装进右栏卡内宽");

    if (g_failed == 0) {
        std::printf("test_formkit: all checks passed (pitch=28 label=80 gap=12 grades=88/160/flex "
                    "double=%d)\n",
                    fk::second_label_x(fk::kFieldStd));
        return 0;
    }
    std::printf("test_formkit: FAILED (%d)\n", g_failed);
    return g_failed;
}
