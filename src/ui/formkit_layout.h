// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M5-T2）— formkit 纯几何：表单栅格与控件度量的**唯一取值点**
// （设计说明书 v6 §5 表单布局体系 R1–R4 / §6 控件语义决策表 / §10 Qt 实现映射；契约 C2/C7）
//
// 本头**零 Qt 依赖**（只有常量与 constexpr 计算函数）—— 供 src/ui/formkit.{h,cpp} 的行构造器
// 与自绘控件、tests/unit/test_formkit.cpp 共用同一份取值：R1/R2 的几何口径只写在这里一次，
// 构造器与单测不可能各算一套。
//
// 与 theme.h 的关系（C1 令牌单源）：颜色/字阶/间距圆角/控件度量的真源仍是 theme.h（§4.1/§4.2/
// §4.3 + R4）；本头按 §5 的栅格口径**复述**其中与表单相关的数值，并在 formkit.h 里以
// static_assert 逐条对齐 —— 两处漂移即编译失败。唯二例外见文件末「M4 遗留」注释
// （theme::Metrics::form_label_w / form_row_h：仍被 W-G2/W-G3 待迁移页面消费，T2 不动其值）。
//
// 单位口径：px = 逻辑像素（Qt 设备无关像素，与 mockup CSS px 同口径）。

#pragma once

namespace pp::ui::formkit {

// ---------------------------------------------------------------------------
// R1 行节奏（§5 R1）：卡内**任何**信息行 pitch 恒 28px，行间无额外 gap
// （表单行 / kv 行 / checkbox 行同节奏）。卡体上内边距 10、下 12。
// ---------------------------------------------------------------------------
inline constexpr int kRowPitch = 28;
inline constexpr int kCardPadTop = 10;
inline constexpr int kCardPadBottom = 12;

// ---------------------------------------------------------------------------
// R2 字段轨（§5 R2）：固定轨道网格，控件**必须撑满其轨**
//   单列行：[ 标签 80px 右对齐 ][12px][ 字段轨 ]
//   双列行：[ 80 ][12][ 轨 ][24px][ 80 ][12][ 轨 ]   ← 第二标签在固定 x，不随内容漂移
// 字段宽只有三档：短 88 / 标准 160 / 长 flex（路径、搜索框）。
// ---------------------------------------------------------------------------
inline constexpr int kLabelW = 80;     // 标签轨（右对齐）
inline constexpr int kColGap = 12;     // 列距
inline constexpr int kFieldShort = 88; // 字段宽·短档（组内小数值）
inline constexpr int kFieldStd = 160;  // 字段宽·标准档（下拉、独立数值、短文本）
inline constexpr int kDoubleGap = 24;  // 双列行的两栏间距 [轨][24px][第二标签]

// ---------------------------------------------------------------------------
// R3 提示位（§5 R3）：范围/单位是提示不是标签 —— 单位入 16px 槽，范围入 10px 注释。
// 槽与注释都排在字段控件右侧（行内），不占标签位（§12-8）。
// ---------------------------------------------------------------------------
inline constexpr int kUnitSlotW = 16; // 单位槽宽
inline constexpr int kNotePx = 10;    // 范围注释字号（§4.2 辅助档 10/400，整数 px 编码）
inline constexpr int kInlineGap = 6;  // 控件 / 单位槽 / 注释之间的行内距

// ---------------------------------------------------------------------------
// R4 控件度量（§5 R4 表逐行；formkit.h static_assert 与 theme.h 对齐）
// ---------------------------------------------------------------------------
inline constexpr int kControlH = 24;   // spin / 下拉 / 文本 / 分段 高 24
inline constexpr int kSpinArrowW = 17; // spinbox 箭头列 17px（▲▼ 各 12）
inline constexpr int kSpinArrowH = 12;
inline constexpr int kSpinPadX = 7;     // spinbox 值区内距（R4 表未列；mockup `.spin .v` 0 7px）
inline constexpr int kComboArrowPx = 8; // 下拉箭头 8px 居右
inline constexpr int kSegH = 24;        // 分段控件高 24
inline constexpr int kSegW = 88;        // 分段段宽 88（等宽 → 选中/悬停零宽度变化）
inline constexpr int kSegRadius = 3;    // 分段 r3
inline constexpr int kSwitchW = 30;     // 开关 30×16 药丸
inline constexpr int kSwitchH = 16;
inline constexpr int kSwitchKnob = 12;     // 开关钮 12
inline constexpr int kCheckPx = 13;        // 复选框 13×13
inline constexpr int kCheckRadius = 2;     // 复选框 r2
inline constexpr int kCheckGap = 8;        // 勾选框 → 文字（语序固定）
inline constexpr int kBtnH = 26;           // 按钮高 26
inline constexpr int kPillH = 16;          // 徽章 pill 高 16
inline constexpr int kPillRadius = 3;      // 徽章 pill r3
inline constexpr int kPillPx10 = 95;       // 徽章 9.5px（1/10px 整数编码，避开浮点比较）
inline constexpr int kPillWeight = 700;    // 徽章 700
inline constexpr int kProgressH = 6;       // 进度条高 6
inline constexpr int kProgressRadius = 99; // 进度条 r99（规格值，自绘按半高落地）
inline constexpr int kIconBtn = 28;        // 图标钮 28×28
inline constexpr int kIconBtnRadius = 4;   // 图标钮 r4
// 磁贴卡（§6.1 多选集合唯一形态；§7.2 输出格式 2×4）：固定宽 + 预留对勾位（选中零形变）
inline constexpr int kTileH = 30;
inline constexpr int kTileRadius = 3;
inline constexpr int kTilePadX = 9;
inline constexpr int kTileGap = 6;
// 打标 chip（§6.1 批量快捷打标）：键帽 + 文字，固定宽。
// 文字字号取 §4.2 标签档 11/600（mockup .tagchip 为 10.5/600 —— §4.2 无 10.5 档，差 0.5px
// 属 §0.2「实现期布局 ±4px 微调」；令牌单源优先，登记见 M5-T2 报告）。
inline constexpr int kChipH = 22;
inline constexpr int kChipRadius = 3;
inline constexpr int kChipKeyPx = 13; // 键帽 13×13 r2
inline constexpr int kChipPadX = 8;
inline constexpr int kChipGap = 4;

// ---------------------------------------------------------------------------
// 计算函数（行构造器与单测共用；全部 constexpr → 可在 static_assert 与单测里直接取值）
// ---------------------------------------------------------------------------

// 字段轨起点 x = [标签 80][列距 12] —— 单列行与双列行第一栏同一起点
constexpr int field_track_x() { return kLabelW + kColGap; }

// 双列行第二标签起点 x = 80 / 12 / 轨 / 24（与内容无关 → 第二标签固定 x）
constexpr int second_label_x(int track_w) { return field_track_x() + track_w + kDoubleGap; }

// 双列行第二字段轨起点 x = 第二标签 x + [80][12]
constexpr int second_track_x(int track_w) { return second_label_x(track_w) + kLabelW + kColGap; }

// 双列行总占宽（两栏各按给定轨宽）—— 供布局选档与探针复核
constexpr int double_row_width(int track0, int track1) { return second_track_x(track0) + track1; }

// 单位槽起点 x = 控件右缘 + 行内距
constexpr int unit_slot_x(int control_x, int control_w) {
    return control_x + control_w + kInlineGap;
}

// 字段宽三档（R2）：短 / 标准为定值；长（flex）由轨宽减扣除项给出
enum class FieldWidth { Short, Standard, Flex };

constexpr int field_w(FieldWidth width) {
    return width == FieldWidth::Short ? kFieldShort : kFieldStd;
}

// flex 控件在 track_w 轨内的可用宽：扣每单位槽（16 + 行内距）与注释占宽（+ 行内距）
constexpr int flex_field_w(int track_w, int unit_slots = 0, int note_w = 0) {
    return track_w - unit_slots * (kUnitSlotW + kInlineGap) -
           (note_w > 0 ? note_w + kInlineGap : 0);
}

// 控件在轨内的实际宽：短/标准 = 定值（轨即控件宽，无浮空死区）；长 = 撑满轨
constexpr int field_w_in_track(FieldWidth width, int track_w, int unit_slots = 0, int note_w = 0) {
    return width == FieldWidth::Flex ? flex_field_w(track_w, unit_slots, note_w) : field_w(width);
}

// 卡内 n 行的行组高（R1；不含卡体上下内边距）
constexpr int rows_height(int rows) { return rows * kRowPitch; }

// 卡体高（R1：上 10 + n×28 + 下 12）
constexpr int card_body_h(int rows) { return kCardPadTop + rows * kRowPitch + kCardPadBottom; }

// 控件高度（R4 表逐行；构造器与探针共用同一张表）
enum class Control {
    Spin,       // 24
    Combo,      // 24
    Input,      // 24
    Segmented,  // 24
    Button,     // 26
    Switch,     // 16
    Checkbox,   // 13
    Pill,       // 16
    Chip,       // 22
    IconButton, // 28
    Progress,   // 6
    Tile,       // 30
};

constexpr int control_h(Control kind) {
    switch (kind) {
    case Control::Spin:
    case Control::Combo:
    case Control::Input:
    case Control::Segmented:
        return kControlH;
    case Control::Button:
        return kBtnH;
    case Control::Switch:
        return kSwitchH;
    case Control::Checkbox:
        return kCheckPx;
    case Control::Pill:
        return kPillH;
    case Control::Chip:
        return kChipH;
    case Control::IconButton:
        return kIconBtn;
    case Control::Progress:
        return kProgressH;
    case Control::Tile:
        return kTileH;
    }
    return kControlH;
}

} // namespace pp::ui::formkit
