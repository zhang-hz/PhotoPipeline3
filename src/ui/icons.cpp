// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M5-T3）— 矢量图标库实现（契约 C3；契约/线宽口径/单源规则见 icons.h）
//
// 结构：
//   1) kDefs 表 —— **图标形的唯一真源**（`d` 串与 assets/icons/<键>.svg 逐字同源）；
//   2) path_from_d —— SVG `d` 子集解析（同一份数据既供运行期绘制，也供 --ui-smoke 的同源校验）；
//   3) paint —— 24 网格 → 目标盒等比映射；笔宽换算回网格单位（线宽不随盒缩放，见 icons.h）；
//   4) VectorEngine —— QIcon 的矢量引擎（任意尺寸按需光栅化、DPI 感知，无 qrc/无 Qt6::Svg）。

#include "ui/icons.h"

// M7-WP5（R-271 测试接线）：apply(QAbstractButton*) 是本 TU 唯一的 QtWidgets 依赖面；
// 单测 target 只链 pp_core（Qt6::Core/Gui），故 tests/unit/test_icons.cpp 以
// PP_ICONS_TEST_NO_WIDGETS 编译本文件（只测解析/几何纯面）。生产构建不定义该宏 ——
// 代码路径与函数面零改动（apply() 在生产构建里照常编译）。
#ifndef PP_ICONS_TEST_NO_WIDGETS
#include <QAbstractButton>
#endif
#include <QColor>
#include <QIconEngine>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>

namespace pp::ui::icons {
namespace {

// ---------------------------------------------------------------------------
// 1) 图标表：{枚举, 键, 描边 d, 填充 d}
//    d 串 = assets/icons/<键>.svg 的 path[@d] 逐字（同名同形；--ui-smoke 的 icons 探针逐条比对）。
//    取形来源：docs/mockups-v2/*.html 的内联 <svg>（顶栏 ☆/⚙、文件工具行、
//    翻页 ◀▶、勾选 ✓、品牌相机）逐字/等比转录，其余按 §3.3 的 24 网格 + 1.5 等线宽语言新绘。
//    caption 四字形例外（M7-WP5/UX-7④）：按 §3.3 的 WinUI3 10×10 字形盒重绘（见 kDefs 行内注）。
// ---------------------------------------------------------------------------
struct Def {
    Name name;
    const char *key;
    const char *stroke_d;
    const char *fill_d;
};

constexpr Def kDefs[] = {
    // M7-WP5（UX-7④，docs/ux-review-14.md §7「三钮视觉校准」）：caption 四字形按
    // qt-gui-design-spec.md §3.3 重画 —— WinUI3 的 **10×10 字形盒**（kCaptionIconPx=10 的
    // 图标盒 = 24 网格）。端点收 1.8 网格（= 半线宽 0.75px × 2.4 网格/px）→ 1.5px 圆帽描边的
    // 墨迹恰好铺满 0–24 网格 = 10×10 字形盒；旧形（2.4/21.6、3.6/20.4）只占 7–8px，偏细偏小。
    // 与 assets/icons/caption-*.svg 的 path[@d] 逐字同形（--ui-smoke icons 同源探针钉住）。
    {Name::CaptionMin, "caption-min", "M1.8 12H22.2", ""},
    {Name::CaptionMax, "caption-max", "M1.8 1.8H22.2V22.2H1.8Z", ""},
    {Name::CaptionRestore, "caption-restore", "M1.8 6.6H17.4V22.2H1.8Z M6.6 6.6V1.8H22.2V17.4H17.4",
     ""},
    {Name::CaptionClose, "caption-close", "M1.8 1.8L22.2 22.2M22.2 1.8L1.8 22.2", ""},
    {Name::Star, "star", "M12 3.5l2.6 5.3 5.9.9-4.3 4.2 1 5.8L12 17l-5.2 2.7 1-5.8L3.5 9.7l5.9-.9z",
     ""},
    {Name::Gear, "gear",
     "M15 12C15 13.657 13.657 15 12 15C10.343 15 9 13.657 9 12C9 10.343 10.343 9 12 9C13.657 9 15 "
     "10.343 15 12 M12 3v2.4M12 18.6V21M3 12h2.4M18.6 12H21M5.9 5.9l1.7 1.7M16.4 16.4l1.7 1.7M18.1 "
     "5.9l-1.7 1.7M7.6 16.4l-1.7 1.7",
     ""},
    {Name::Search, "search",
     "M16.4 10C16.4 13.535 13.535 16.4 10 16.4C6.465 16.4 3.6 13.535 3.6 10C3.6 6.465 6.465 3.6 10 "
     "3.6C13.535 3.6 16.4 6.465 16.4 10 M15 15L20 20",
     ""},
    {Name::Add, "add", "M12 4V20M4 12H20", ""},
    {Name::Remove, "remove", "M4 12H20", ""},
    {Name::Filter, "filter", "M4 6H20M7 12H17M10 18H14", ""},
    {Name::Group, "group",
     "M3 4.5L7 4.5L7 8.5L3 8.5Z M3 10L7 10L7 14L3 14Z M3 15.5L7 15.5L7 19.5L3 19.5Z M10.5 "
     "6.5H21M10.5 12H21M10.5 17.5H21",
     ""},
    {Name::ZoomIn, "zoom-in",
     "M16.4 10C16.4 13.535 13.535 16.4 10 16.4C6.465 16.4 3.6 13.535 3.6 10C3.6 6.465 6.465 3.6 10 "
     "3.6C13.535 3.6 16.4 6.465 16.4 10 M15 15L20 20 M10 6.6V13.4M6.6 10H13.4",
     ""},
    {Name::ZoomOut, "zoom-out",
     "M16.4 10C16.4 13.535 13.535 16.4 10 16.4C6.465 16.4 3.6 13.535 3.6 10C3.6 6.465 6.465 3.6 10 "
     "3.6C13.535 3.6 16.4 6.465 16.4 10 M15 15L20 20 M6.6 10H13.4",
     ""},
    {Name::ZoomFit, "zoom-fit",
     "M3.5 9.5V3.5H9.5M14.5 3.5H20.5V9.5M20.5 14.5V20.5H14.5M9.5 20.5H3.5V14.5", ""},
    {Name::ZoomActual, "zoom-actual",
     "M4.5 4.5L19.5 4.5L19.5 19.5L4.5 19.5Z M14.6 12C14.6 13.436 13.436 14.6 12 14.6C10.564 14.6 "
     "9.4 13.436 9.4 12C9.4 10.564 10.564 9.4 12 9.4C13.436 9.4 14.6 10.564 14.6 12",
     ""},
    {Name::Prev, "prev", "M15 4L8 12L15 20", ""},
    {Name::Next, "next", "M9 4L16 12L9 20", ""},
    {Name::Browse, "browse",
     "M3 19.5L3 7.5L9 7.5L11 10L21 10L21 19.5Z M12 17.0V12.5M9.8 14.7L12 12.5L14.2 14.7", ""},
    {Name::FolderOpen, "folder-open",
     "M3.5 18.5V7.5H9.5L11.5 10.0H19.5V12.2 M3.5 18.5L7.0 12.2H21.0L17.5 18.5Z", ""},
    {Name::MapPin, "map-pin",
     "M18 9C18 12.314 15.314 15 12 15C8.686 15 6 12.314 6 9C6 5.686 8.686 3 12 3C15.314 3 18 5.686 "
     "18 9 M7.76 13.24L12.0 21.3L16.24 13.24 M14.4 9C14.4 10.325 13.325 11.4 12 11.4C10.675 11.4 "
     "9.6 10.325 9.6 9C9.6 7.675 10.675 6.6 12 6.6C13.325 6.6 14.4 7.675 14.4 9",
     ""},
    {Name::MapSearch, "map-search",
     "M18 9.2C18 12.514 15.314 15.2 12 15.2C8.686 15.2 6 12.514 6 9.2C6 5.886 8.686 3.2 12 "
     "3.2C15.314 3.2 18 5.886 18 9.2 M7.76 13.44L12.0 21.3L16.24 13.44 M13.7 8.6C13.7 9.925 12.625 "
     "11 11.3 11C9.975 11 8.9 9.925 8.9 8.6C8.9 7.275 9.975 6.2 11.3 6.2C12.625 6.2 13.7 7.275 "
     "13.7 8.6 M13.1 10.4L15.2 12.5",
     ""},
    {Name::Log, "log",
     "M6.5 3.5L13.5 3.5L18 8L18 20.5L6.5 20.5Z M13.5 3.5V8.0H18.0 M9.5 12.0H15.0M9.5 15.5H15.0M9.5 "
     "19.0H12.5",
     ""},
    {Name::Play, "play", "", "M8.5 5.5L18.5 12L8.5 18.5Z"},
    {Name::Check, "check", "M4.8 12L10.08 17.28L19.2 7.2", ""},
    {Name::ChevronDown, "chevron-down", "M5.5 9.0L12 15.5L18.5 9.0", ""},
    {Name::ChevronRight, "chevron-right", "M9.0 5.5L15.5 12L9.0 18.5", ""},
    {Name::Clear, "clear", "M6.5 6.5L17.5 17.5M17.5 6.5L6.5 17.5", ""},
    {Name::Tag, "tag",
     "M4 4L12.5 4L20.5 12L12 20.5L4 12.5Z M8.9 7.6C8.9 8.318 8.318 8.9 7.6 8.9C6.882 8.9 6.3 8.318 "
     "6.3 7.6C6.3 6.882 6.882 6.3 7.6 6.3C8.318 6.3 8.9 6.882 8.9 7.6",
     ""},
    {Name::Image, "image",
     "M6 5H18C19.657 5 21 6.343 21 8V16C21 17.657 19.657 19 18 19H6C4.343 19 3 17.657 3 16V8C3 "
     "6.343 4.343 5 6 5Z M5 17l4.5-4.5 3 3L16 12l3 3",
     "M10.5 10C10.5 10.828 9.828 11.5 9 11.5C8.172 11.5 7.5 10.828 7.5 10C7.5 9.172 8.172 8.5 9 "
     "8.5C9.828 8.5 10.5 9.172 10.5 10"},
    {Name::Info, "info",
     "M20.5 12C20.5 16.694 16.694 20.5 12 20.5C7.306 20.5 3.5 16.694 3.5 12C3.5 7.306 7.306 3.5 12 "
     "3.5C16.694 3.5 20.5 7.306 20.5 12 M12 11.5V16.5",
     "M13.1 7.6C13.1 8.208 12.608 8.7 12 8.7C11.392 8.7 10.9 8.208 10.9 7.6C10.9 6.992 11.392 6.5 "
     "12 6.5C12.608 6.5 13.1 6.992 13.1 7.6"},
    {Name::Warn, "warn", "M12 4.2L21.3 20L2.7 20Z M12 10.0V14.8",
     "M13.1 17.6C13.1 18.208 12.608 18.7 12 18.7C11.392 18.7 10.9 18.208 10.9 17.6C10.9 16.992 "
     "11.392 16.5 12 16.5C12.608 16.5 13.1 16.992 13.1 17.6"},
    {Name::Error, "error",
     "M20.5 12C20.5 16.694 16.694 20.5 12 20.5C7.306 20.5 3.5 16.694 3.5 12C3.5 7.306 7.306 3.5 12 "
     "3.5C16.694 3.5 20.5 7.306 20.5 12 M9.4 9.4L14.6 14.6M14.6 9.4L9.4 14.6",
     ""},
    {Name::Reset, "reset",
     "M16.31 6.484C18.806 8.434 19.681 11.819 18.444 14.735C17.206 17.651 14.163 19.373 11.026 "
     "18.932C7.888 18.491 5.438 15.998 5.052 12.853C4.666 9.708 6.44 6.697 9.378 5.51 M16.31 "
     "6.484L12.788 7.232 M16.31 6.484L14.731 9.72",
     ""},
    {Name::Edit, "edit",
     "M7.2 12.9L15.9 4.2L19.8 8.1L11.1 16.8Z M4.2 19.8L7.2 12.9L11.1 16.8Z M13.4 6.7L17.3 10.6",
     ""},
    {Name::Stop, "stop", "",
     "M8.1 6.5H15.9C16.784 6.5 17.5 7.216 17.5 8.1V15.9C17.5 16.784 16.784 17.5 15.9 "
     "17.5H8.1C7.216 17.5 6.5 16.784 6.5 15.9V8.1C6.5 7.216 7.216 6.5 8.1 6.5Z"},
    {Name::Pending, "pending", "",
     "M8.5 12C8.5 12.828 7.828 13.5 7 13.5C6.172 13.5 5.5 12.828 5.5 12C5.5 11.172 6.172 10.5 7 "
     "10.5C7.828 10.5 8.5 11.172 8.5 12 M13.5 12C13.5 12.828 12.828 13.5 12 13.5C11.172 13.5 10.5 "
     "12.828 10.5 12C10.5 11.172 11.172 10.5 12 10.5C12.828 10.5 13.5 11.172 13.5 12 M18.5 12C18.5 "
     "12.828 17.828 13.5 17 13.5C16.172 13.5 15.5 12.828 15.5 12C15.5 11.172 16.172 10.5 17 "
     "10.5C17.828 10.5 18.5 11.172 18.5 12"},
    {Name::Active, "active",
     "M19.5 12C19.5 16.142 16.142 19.5 12 19.5C7.858 19.5 4.5 16.142 4.5 12C4.5 7.858 7.858 4.5 12 "
     "4.5C16.142 4.5 19.5 7.858 19.5 12",
     "M12 4.5C16.142 4.5 19.5 7.858 19.5 12C19.5 16.142 16.142 19.5 12 19.5Z"},
};

constexpr int kDefCount = int(sizeof(kDefs) / sizeof(kDefs[0]));
static_assert(kDefCount >= 34, "C3：图标清单必须覆盖任务书列出的 34 个名字");

int index_of(Name name) {
    for (int i = 0; i < kDefCount; ++i)
        if (kDefs[i].name == name)
            return i;
    return -1;
}

// ---------------------------------------------------------------------------
// 2) SVG `d` 子集解析（M/m L/l H/h V/v C/c S/s Z/z；隐式重复与逗号/空白分隔均支持）
// ---------------------------------------------------------------------------
class DLexer {
public:
    explicit DLexer(const QString &d) : text_(d) {}

    bool next_command(QChar &command) {
        skip();
        if (pos_ >= text_.size())
            return false;
        const QChar ch = text_.at(pos_);
        if (!ch.isLetter())
            return false;
        command = ch;
        ++pos_;
        return true;
    }

    // 下一枚 token 是否是数字起始（参数组之间用：组尽了 → 回到命令循环，而不是截断整条路径）
    bool has_number() {
        skip();
        if (pos_ >= text_.size())
            return false;
        const QChar ch = text_.at(pos_);
        return ch.isDigit() || ch == u'.' || ch == u'+' || ch == u'-';
    }

    bool next_number(qreal &out) {
        skip();
        const int start = pos_;
        if (pos_ < text_.size() && (text_.at(pos_) == u'+' || text_.at(pos_) == u'-'))
            ++pos_;
        bool digits = false;
        while (pos_ < text_.size() && text_.at(pos_).isDigit()) {
            ++pos_;
            digits = true;
        }
        if (pos_ < text_.size() && text_.at(pos_) == u'.') {
            ++pos_;
            while (pos_ < text_.size() && text_.at(pos_).isDigit()) {
                ++pos_;
                digits = true;
            }
        }
        if (!digits) {
            pos_ = start;
            return false;
        }
        // "5.9.9" 这类无分隔写法：小数位读数在第二个 '.' 处停下（下一枚数字从该 '.' 起）—— 见 §表
        bool ok = false;
        out = text_.mid(start, pos_ - start).toDouble(&ok);
        return ok;
    }

private:
    void skip() {
        while (pos_ < text_.size() && (text_.at(pos_).isSpace() || text_.at(pos_) == u','))
            ++pos_;
    }

    QString text_;
    int pos_ = 0;
};

// ---------------------------------------------------------------------------
// 3) 路径缓存（首用构建；调用方只在 GUI 线程消费 —— 与 Qt 绘制路径同线程约定）
// ---------------------------------------------------------------------------
struct PathCache {
    std::array<QPainterPath, kDefCount> stroke;
    std::array<QPainterPath, kDefCount> fill;
};

const PathCache &cache() {
    static const PathCache built = [] {
        PathCache out;
        for (int i = 0; i < kDefCount; ++i) {
            if (kDefs[i].stroke_d[0] != '\0')
                out.stroke[i] = path_from_d(QString::fromUtf8(kDefs[i].stroke_d));
            if (kDefs[i].fill_d[0] != '\0')
                out.fill[i] = path_from_d(QString::fromUtf8(kDefs[i].fill_d));
        }
        return out;
    }();
    return built;
}

QColor faded(const QColor &color) {
    QColor out = color;
    out.setAlphaF(std::clamp(double(color.alphaF()) * kDisabledAlpha, 0.0, 1.0));
    return out;
}

// ---------------------------------------------------------------------------
// 4) QIcon 矢量引擎（任意尺寸按需光栅化；不做位图缓存 —— QIcon 自身按 key 缓存）
// ---------------------------------------------------------------------------
class VectorEngine : public QIconEngine {
public:
    VectorEngine(Name name, const QColor &color, const QColor &disabled, qreal dpr)
        : name_(name), color_(color), disabled_(disabled.isValid() ? disabled : faded(color)),
          dpr_(dpr > 0.01 ? dpr : 1.0) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        if (painter == nullptr)
            return;
        icons::paint(*painter, name_, QRectF(rect), color_for(mode));
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap out(size);
        out.setDevicePixelRatio(dpr_);
        out.fill(Qt::transparent);
        QPainter painter(&out);
        // 先标 dpr 再画：QPainter 在该 pixmap 上按**逻辑**坐标工作（与控件路径同一坐标系）
        const QRectF logical(0.0, 0.0, size.width() / dpr_, size.height() / dpr_);
        paint(&painter, logical.toRect(), mode, state);
        return out;
    }

    QIconEngine *clone() const override { return new VectorEngine(name_, color_, disabled_, dpr_); }

    QString key() const override {
        return QStringLiteral("pp-icon/%1/%2/%3/%4")
            .arg(QString::fromLatin1(icons::key(name_)), color_.name(QColor::HexArgb),
                 disabled_.name(QColor::HexArgb), QString::number(dpr_));
    }

private:
    QColor color_for(QIcon::Mode mode) const {
        switch (mode) {
        case QIcon::Disabled:
            return disabled_;
        case QIcon::Active:
        case QIcon::Selected:
            return color_;
        case QIcon::Normal:
            break;
        }
        return color_;
    }

    Name name_;
    QColor color_;
    QColor disabled_;
    qreal dpr_ = 1.0;
};

} // namespace

// ---------------------------------------------------------------------------
// 对外 API
// ---------------------------------------------------------------------------
QPainterPath path_from_d(const QString &d) {
    QPainterPath path;
    DLexer lex(d);
    QChar command;
    QPointF cur(0.0, 0.0);
    QPointF start(0.0, 0.0);
    QPointF last_c2(0.0, 0.0); // S/s 的反射基准（上一三次段的第二控制点）
    while (lex.next_command(command)) {
        const QChar up = command.toUpper();
        const bool rel = command.isLower();
        if (up == u'Z') {
            path.closeSubpath();
            cur = start;
            last_c2 = cur;
            continue;
        }
        if (up == u'M' || up == u'L') {
            const bool move = up == u'M';
            bool first = true;
            qreal x = 0.0;
            qreal y = 0.0;
            while (lex.next_number(x) && lex.next_number(y)) {
                if (rel) {
                    x += cur.x();
                    y += cur.y();
                }
                if (move && first) {
                    path.moveTo(x, y);
                    start = QPointF(x, y);
                } else {
                    path.lineTo(x, y);
                }
                cur = QPointF(x, y);
                last_c2 = cur;
                first = false;
            }
            continue;
        }
        if (up == u'H' || up == u'V') {
            qreal v = 0.0;
            while (lex.next_number(v)) {
                if (up == u'H') {
                    const qreal x = rel ? cur.x() + v : v;
                    cur = QPointF(x, cur.y());
                } else {
                    const qreal y = rel ? cur.y() + v : v;
                    cur = QPointF(cur.x(), y);
                }
                path.lineTo(cur);
            }
            last_c2 = cur;
            continue;
        }
        if (up == u'C' || up == u'S') {
            const int args = up == u'C' ? 6 : 4;
            std::array<qreal, 6> a{};
            while (lex.has_number()) { // 参数组耗尽 → 回到命令循环（**不得**截断后续子路径）
                for (int i = 0; i < args; ++i) {
                    if (!lex.next_number(a[i]))
                        return path; // 组内残缺 = 流损坏：截断保护（返回已解析部分）
                }
                // S/s 的首控制点 = 上一三次段的第二控制点关于当前点的反射（无前段 → 取当前点）
                QPointF c1 = (up == u'C') ? QPointF(a[0], a[1]) : (2.0 * cur - last_c2);
                QPointF c2(a[args - 4], a[args - 3]);
                QPointF end(a[args - 2], a[args - 1]);
                if (rel) {
                    if (up == u'C')
                        c1 += cur; // C/c：两控制点都是相对量
                    // M7-WP5（R-159）：S/s 的反射点是**绝对量**（SVG 规范 = 上一控制点关于
                    // 当前点的镜像）—— 旧实现在 rel 分支把 c1 也加了 cur，反射点变
                    // 3*cur-last_c2（双重偏移）。rel 只偏移 c2/end。
                    c2 += cur;
                    end += cur;
                }
                path.cubicTo(c1, c2, end);
                last_c2 = c2;
                cur = end;
            }
            continue;
        }
        // 未支持的命令字母：**截断**——停止解析并返回已解析部分（不是"跳过该字母继续"；
        // 不应出现 —— kDefs 的表只用到上述子集）
        return path;
    }
    return path;
}

const char *key(Name name) {
    const int i = index_of(name);
    return i >= 0 ? kDefs[i].key : "";
}

bool from_key(QLatin1String key_text, Name *out) {
    for (int i = 0; i < kDefCount; ++i) {
        if (QLatin1String(kDefs[i].key) == key_text) {
            if (out != nullptr)
                *out = kDefs[i].name;
            return true;
        }
    }
    return false;
}

const QVector<Name> &all_names() {
    static const QVector<Name> names = [] {
        QVector<Name> out;
        out.reserve(kDefCount);
        for (int i = 0; i < kDefCount; ++i)
            out.push_back(kDefs[i].name);
        return out;
    }();
    return names;
}

Style style(Name name) {
    const int i = index_of(name);
    if (i < 0)
        return Style::Stroke;
    return (kDefs[i].stroke_d[0] == '\0' && kDefs[i].fill_d[0] != '\0') ? Style::Fill
                                                                        : Style::Stroke;
}

QPainterPath path(Name name) {
    const int i = index_of(name);
    if (i < 0)
        return {};
    const PathCache &c = cache();
    QPainterPath out = c.stroke[i]; // 拷贝（隐式共享）—— 调用方可安全再加工
    out.addPath(c.fill[i]);
    return out;
}

void paint(QPainter &painter, Name name, const QRectF &rect, const QColor &color, qreal stroke_px) {
    const int i = index_of(name);
    if (i < 0)
        return;
    const PathCache &c = cache();
    const QPainterPath &stroke = c.stroke[i];
    const QPainterPath &fill = c.fill[i];
    if (stroke.isEmpty() && fill.isEmpty())
        return;
    const qreal side = std::min(rect.width(), rect.height());
    if (side <= 0.0 || !color.isValid())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // 24 网格 → 目标盒：形等比缩放到 side，且**网格中心 (kGrid/2, kGrid/2) 落在 rect 中心**。
    // 少最后一步 translate(-kGrid/2)：网格原点会对到盒中心 → 图标整体偏移 12 网格、越界被裁
    // （--ui-smoke 的 icons-engine 探针按"墨迹居中"守这条回归）。
    painter.translate(rect.center());
    painter.scale(side / kGrid, side / kGrid);
    painter.translate(-kGrid / 2.0, -kGrid / 2.0);
    if (!stroke.isEmpty()) {
        // 笔宽换算回网格单位：形随盒缩放、线宽**不**随盒缩放（icons.h 的线宽口径）
        const qreal pen_px = std::clamp(stroke_px, 0.01, 64.0);
        QPen pen(color, pen_px * kGrid / side, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(stroke);
    }
    if (!fill.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(fill);
    }
    painter.restore();
}

QIcon icon(Name name, const QColor &color, const QColor &disabled, qreal dpr) {
    return QIcon(new VectorEngine(name, color, disabled, dpr));
}

#ifndef PP_ICONS_TEST_NO_WIDGETS
void apply(QAbstractButton *button, Name name, const QColor &color, const QColor &disabled,
           int px) {
    if (button == nullptr)
        return;
    button->setIcon(icon(name, color, disabled, button->devicePixelRatioF()));
    if (px > 0)
        button->setIconSize(QSize(px, px));
    button->setProperty(kIconNameProperty, QString::fromLatin1(key(name)));
}
#endif

QPixmap pixmap(Name name, int px, const QColor &color, qreal dpr) {
    const qreal ratio = dpr > 0.01 ? dpr : 1.0;
    const int side = std::max(1, px);
    QPixmap out(qRound(side * ratio), qRound(side * ratio));
    out.setDevicePixelRatio(ratio);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    paint(painter, name, QRectF(0.0, 0.0, side, side), color);
    return out;
}

} // namespace pp::ui::icons
