// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M5-T3）— 矢量图标库（契约 C3；设计说明书 v6 §3.3 顶栏 / §5 R4 图标钮 /
// §11 无障碍 / §10 Qt 实现映射）
//
// 契约（docs/m5-tasks.md §1 C3「图标矢量化」）：
//   * **24×24 设计网格**、**描边 1.5px 等线宽**（§3.3「1.4–1.6px」区间）、圆帽圆角连接、纯几何
//     线性图标；QPainterPath + QPainter 绘制（抗锯齿、DPI 感知）；
//   * `assets/icons/*.svg` 是**设计源**（同名同形：本文件同名条目逐条给出同一个 `d` 串）；
//     实现**不依赖 Qt6::Svg、不加 qrc**（打包闭包零漂移 —— 实现裁定，登记为机械性偏差），
//     故 SVG 不参与运行期，仅作设计源与人工走查对照；
//   * 颜色一律由调用方以**令牌色**传入（本文件不写任何色值字面量，C1 单源）。
//
// 单源规则（M3 铁律六「单实现」）：图标形只有一份 —— 本 .cpp 的 `kDefs` 表（`d` 串）+ 本文件的
// 解析/绘制函数。调用方不得自绘图标路径、不得再引入第二套字形图标（Unicode 字形一律禁止，§3.3）。
//
// 线宽口径：1.5px 是**屏幕逻辑像素**常量，不随图标盒缩放 —— 24 网格是形，1.5px 是线宽。缩到
// 10px 的 caption 钮时若按比例缩笔宽（1.5×10/24 = 0.63px）会因抗锯齿变淡（实测灰化），与 §3.3
// 的「等线宽 1.4–1.6px」不符；故笔宽恒定，形随盒等比缩放。DPI 感知 = 逻辑坐标绘制（dpr 由
// QPainter 设备承担）→ 1x/2x 下分别是 1.5/3.0 设备像素，边缘由抗锯齿落在半像素上。

#pragma once

#include <QColor>
#include <QIcon>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QAbstractButton;
class QPainter;
QT_END_NAMESPACE

namespace pp::ui::icons {

// 设计网格边长（px 逻辑；§3.3「24×24 设计网格」）
inline constexpr qreal kGrid = 24.0;
// 描边等线宽（屏幕逻辑像素；§3.3「1.4–1.6px」区间 → 取中值 1.5）
inline constexpr qreal kStrokePx = 1.5;
inline constexpr qreal kStrokeMinPx = 1.4;
inline constexpr qreal kStrokeMaxPx = 1.6;
// 图标兜底色：调用方未给禁用档色时，禁用态 = 本色 × 该系数（= .icon-btn.dim{opacity:.4} 口径）
inline constexpr double kDisabledAlpha = 0.4;

// 图标清单（C3：名称可微调但覆盖全）。前 34 个 = 任务书逐条列出的名字；后 3 个（Stop/Pending/
// Active）是运行页任务表状态列的标记（M4 的 ■/…/◐ 字形 → 矢量），同属"不得再用字形当图标"。
enum class Name {
    CaptionMin,     // 最小化（─）
    CaptionMax,     // 最大化（□）
    CaptionRestore, // 向下还原（❐）
    CaptionClose,   // 关闭（✕）
    Star,           // 收藏 ☆
    Gear,           // 设置 ⚙
    Search,         // 搜索（放大镜）
    Add,            // 添加（＋）
    Remove,         // 移除（−）
    Filter,         // 筛选
    Group,          // 分组
    ZoomIn,         // 放大（＋）
    ZoomOut,        // 缩小（−）
    ZoomFit,        // 适应
    ZoomActual,     // 1:1
    Prev,           // 上一张（◀）
    Next,           // 下一张（▶）
    Browse,         // 浏览…
    MapPin,         // 地图图钉
    MapSearch,      // 地点搜索
    FolderOpen,     // 打开文件夹
    Log,            // 日志
    Play,           // 播放/开始（▶）
    Check,          // 勾选（✓）
    ChevronDown,    // 展开（▾）
    ChevronRight,   // 折叠（▸）
    Clear,          // 清除（✕）
    Tag,            // 打标/标签（⚑）
    Image,          // 图片/品牌（相机）
    Info,           // 信息
    Warn,           // 警告
    Error,          // 错误
    Reset,          // 重置/还原
    Edit,           // 编辑
    Stop,           // 已取消（■）
    Pending,        // 排队（…）
    Active,         // 进行中（◐）
    // —— M7-R2D1 / UX-2 加性追加（既有 37 枚不重排）——
    CheckAll, // 勾选全部（两行勾 + 两行槽；工具行 pp-check-menu）
};

// 填充口径：Stroke = 只描边（默认）；Fill = 只填充（实心图标：▶ 播放、■ 停止、… 排队点）
enum class Style { Stroke, Fill };

// 名字 ↔ 字符串键（"caption-min"…；键 = assets/icons/<key>.svg 文件名，探针/调试用）
const char *key(Name name);
bool from_key(QLatin1String key, Name *out);
const QVector<Name> &all_names();

// 图标风格（Fill 条目在 paint 时以刷子填充、不描边）
Style style(Name name);

// 路径：24×24 网格坐标系（圆帽圆角连接在 paint 时施加）。
QPainterPath path(Name name);
// SVG `d` 子集解析（M/m、L/l、H/h、V/v、C/c、S/s、Z/z；隐式重复与逗号分隔均支持）——
// 图标表与 assets/icons/*.svg 的同源校验（--ui-smoke 的 icons 探针）走同一条解析。
QPainterPath path_from_d(const QString &d);

// 把图标画到 rect（等比居中；rect 内**逻辑**尺寸最小边即图标边长）。抗锯齿由本函数开，
// 调用方的 painter 状态（笔/刷/变换）在返回后原样恢复。stroke_px = 逻辑像素（缺省 kStrokePx）。
void paint(QPainter &painter, Name name, const QRectF &rect, const QColor &color,
           qreal stroke_px = kStrokePx);

// QIcon（按钮用；矢量 QIconEngine：任意尺寸按需光栅化、DPI 感知）。
//   * color    = Normal/Active/Selected 态色；
//   * disabled = Disabled 态色（无效 → color 按 kDisabledAlpha 淡出）；
//   * dpr      = 目标控件 devicePixelRatioF()（仅影响 QIcon::pixmap 路径的光栅分辨率）。
QIcon icon(Name name, const QColor &color, const QColor &disabled = QColor(), qreal dpr = 1.0);

// QPixmap（无法自绘的位点，如 QLabel 承载的标记；按 dpr 光栅化并标注 devicePixelRatio）。
QPixmap pixmap(Name name, int px, const QColor &color, qreal dpr = 1.0);

// 图标名的动态属性钩子（`ppIcon="caption-close"`…）：无人眼条件下断言"这枚钮挂的是哪枚图标"
// 与"界面里已无字形图标"（--ui-smoke 的 icons 探针逐钮读回）。
inline constexpr const char *kIconNameProperty = "ppIcon";

// 把矢量图标挂到按钮上（**控件侧唯一入口**）：setIcon（矢量引擎，按控件 dpr 光栅化）+
// setIconSize（px <= 0 保持原值）+ 图标名属性钩子。颜色一律由调用方以令牌色传入（C1 单源）。
void apply(QAbstractButton *button, Name name, const QColor &color,
           const QColor &disabled = QColor(), int px = 0);

} // namespace pp::ui::icons
