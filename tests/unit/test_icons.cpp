// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M7-WP5 — icons 解析器单测（R-271：path_from_d 对全部 kDefs 逐条解析断言）
// + R-159 专项（相对 s/S 的反射控制点不得双重偏移）。
//
// 证据面（docs/deep-review-0.3.2.md）：
//   * R-271「icons 解析器零单元测试（E4-02 的测试盲区）」→ 本文件逐条补齐；
//   * R-159「path_from_d 相对 s 命令反射控制点双重偏移」→ c1=2*cur-last_c2 后 rel 分支
//     又加 cur（变 3*cur-last_c2）；修法 = rel 只偏移 c2/end。现网 37 条 d 串无 s/S 不触发，
//     故用显式 s/S 用例钉住反射点坐标（含"前段非曲线 → 反射基准=当前点"的规范分支）。
//
// 测试接线：src/ui/icons.cpp 属 UI 目录（不编入 pp_core），单测 target 只链 pp_core
// （Qt6::Core/Gui）→ 以 PP_ICONS_TEST_NO_WIDGETS 编译该 TU（收窄掉 apply(QAbstractButton*)
// 这个唯一 QtWidgets 依赖面）。本用例不建 QGuiApplication、不触 QIcon/QPixmap 路径。
// 每个失败打印 "FAIL <case>: <detail>"；main() 返回失败数（0 = 全绿）。

#define PP_ICONS_TEST_NO_WIDGETS
#include "ui/icons.cpp" // 单实现（M3 铁律六）：解析器/kDefs 与生产同一份代码

#include <cstdio>
#include <string>

namespace icons = pp::ui::icons;

namespace {

int g_failed = 0;

using ElementT = QPainterPath::ElementType;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

// QPainterPath 元素（type,x,y）逐点比对（容差 1e-6，与 --ui-smoke 的 icons 同源探针同口径）
bool element_same(const QPainterPath &p, int i, QPainterPath::ElementType type, qreal x, qreal y) {
    if (i < 0 || i >= p.elementCount())
        return false;
    const QPainterPath::Element e = p.elementAt(i);
    return e.type == type && std::abs(e.x - x) < 1e-6 && std::abs(e.y - y) < 1e-6;
}

std::string show_element(const QPainterPath &p, int i) {
    if (i < 0 || i >= p.elementCount())
        return "(oob)";
    const QPainterPath::Element e = p.elementAt(i);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "#%d type=%d (%.4f,%.4f)", i, int(e.type), double(e.x),
                  double(e.y));
    return buf;
}

} // namespace

int main() {
    // ---- R-159：相对 s 的反射控制点（双重偏移回归钉）----
    // "M0 0s1 1 2 0s3 -1 4 0"：
    //   s#1：c1 = 2*(0,0)-(0,0) = (0,0)；c2 = (1,1)；end = (2,0)
    //   s#2：c1 = 2*(2,0)-(1,1) = (3,-1)   ← 反射点是绝对量；旧实现在 rel 分支 +cur 得 (5,-1)
    //        c2 = (3,-1)+(2,0) = (5,-1)；end = (4,0)+(2,0) = (6,0)
    {
        const QPainterPath p = icons::path_from_d(QStringLiteral("M0 0s1 1 2 0s3 -1 4 0"));
        check(p.elementCount() == 7, "r159/rel-s/element-count",
              "got " + std::to_string(p.elementCount()));
        check(element_same(p, 0, ElementT::MoveToElement, 0.0, 0.0), "r159/rel-s/move",
              show_element(p, 0));
        check(element_same(p, 1, ElementT::CurveToElement, 0.0, 0.0), "r159/rel-s/s1-c1",
              show_element(p, 1));
        check(element_same(p, 2, ElementT::CurveToDataElement, 1.0, 1.0), "r159/rel-s/s1-c2",
              show_element(p, 2));
        check(element_same(p, 3, ElementT::CurveToDataElement, 2.0, 0.0), "r159/rel-s/s1-end",
              show_element(p, 3));
        // 钉点：反射点 = 2*cur-last_c2 = (3,-1)，不是 (5,-1)
        check(element_same(p, 4, ElementT::CurveToElement, 3.0, -1.0), "r159/rel-s/s2-c1",
              show_element(p, 4) + " —— 反射点必须是绝对量 2*cur-last_c2（R-159 双重偏移回归钉）");
        check(element_same(p, 5, ElementT::CurveToDataElement, 5.0, -1.0), "r159/rel-s/s2-c2",
              show_element(p, 5));
        check(element_same(p, 6, ElementT::CurveToDataElement, 6.0, 0.0), "r159/rel-s/s2-end",
              show_element(p, 6));
    }

    // ---- R-159：绝对 S（C 后）反射基准 = 上一三次段的第二控制点 ----
    // "M0 0C1 1 2 1 3 0S5 -1 6 0"：S#1 c1 = 2*(3,0)-(2,1) = (4,-1)
    {
        const QPainterPath p = icons::path_from_d(QStringLiteral("M0 0C1 1 2 1 3 0S5 -1 6 0"));
        check(p.elementCount() == 7, "r159/abs-s/element-count",
              "got " + std::to_string(p.elementCount()));
        check(element_same(p, 4, ElementT::CurveToElement, 4.0, -1.0), "r159/abs-s/s-c1",
              show_element(p, 4));
        check(element_same(p, 5, ElementT::CurveToDataElement, 5.0, -1.0), "r159/abs-s/s-c2",
              show_element(p, 5));
        check(element_same(p, 6, ElementT::CurveToDataElement, 6.0, 0.0), "r159/abs-s/s-end",
              show_element(p, 6));
    }

    // ---- R-159：前段非曲线 → 反射基准 = 当前点（S 的 c1 = cur，SVG 规范分支）----
    // "M5 5L10 5s2 2 4 0"：L 后 last_c2 = cur = (10,5) → s c1 = 2*(10,5)-(10,5) = (10,5)
    {
        const QPainterPath p = icons::path_from_d(QStringLiteral("M5 5L10 5s2 2 4 0"));
        check(p.elementCount() == 5, "r159/after-line/element-count",
              "got " + std::to_string(p.elementCount()));
        check(element_same(p, 2, ElementT::CurveToElement, 10.0, 5.0), "r159/after-line/s-c1",
              show_element(p, 2));
        check(element_same(p, 3, ElementT::CurveToDataElement, 12.0, 7.0), "r159/after-line/s-c2",
              show_element(p, 3));
        check(element_same(p, 4, ElementT::CurveToDataElement, 14.0, 5.0), "r159/after-line/s-end",
              show_element(p, 4));
    }

    // ---- R-159：相对 c 的控制点仍是相对量（修法不得把 C 一起改错）----
    // "M1 1c1 2 3 4 5 6"：c1 = (2,3)；c2 = (4,5)；end = (6,7)
    {
        const QPainterPath p = icons::path_from_d(QStringLiteral("M1 1c1 2 3 4 5 6"));
        check(p.elementCount() == 4, "r159/rel-c/element-count",
              "got " + std::to_string(p.elementCount()));
        check(element_same(p, 1, ElementT::CurveToElement, 2.0, 3.0), "r159/rel-c/c1",
              show_element(p, 1));
        check(element_same(p, 2, ElementT::CurveToDataElement, 4.0, 5.0), "r159/rel-c/c2",
              show_element(p, 2));
        check(element_same(p, 3, ElementT::CurveToDataElement, 6.0, 7.0), "r159/rel-c/end",
              show_element(p, 3));
    }

    // ---- 解析器契约回归（与 --ui-smoke 的 icons 解析探针同口径断言）：C/S 参数组耗尽后
    //      必须回到命令循环继续解析后续子路径，不得截断 ----
    {
        const QPainterPath probe =
            icons::path_from_d(QStringLiteral("M0 0L1 1M2 2C3 3 4 4 5 5M6 6H7M8 8"));
        const QPainterPath::Element last = probe.elementCount() > 0
                                               ? probe.elementAt(probe.elementCount() - 1)
                                               : QPainterPath::Element{};
        check(probe.elementCount() == 9 && last.type == ElementT::MoveToElement && last.x == 8.0 &&
                  last.y == 8.0,
              "parser/no-truncation",
              "elements=" + std::to_string(probe.elementCount()) +
                  " last=" + show_element(probe, probe.elementCount() - 1));
    }

    // ---- R-271：kDefs 全表逐条解析断言 ----
    // 每条：stroke/fill 的 d 串各自解析非空、与 path() 合成一致、全部元素落在 24 网格内
    // （含描边半径 0.75 —— 与 ui_smoke 的 icons bounds 探针同一条边界口径）、键名往返一致。
    const QVector<icons::Name> &names = icons::all_names();
    check(names.size() == icons::kDefCount, "r271/def-count",
          "all_names=" + std::to_string(names.size()) +
              " kDefs=" + std::to_string(icons::kDefCount));
    const qreal margin = icons::kStrokePx / 2.0; // 0.75：描边半宽允许的越界量
    for (int i = 0; i < icons::kDefCount; ++i) {
        const icons::Def &def = icons::kDefs[i];
        const std::string key_name = def.key;
        const std::string case_prefix = "r271/" + key_name;

        QPainterPath stroke;
        QPainterPath fill;
        if (def.stroke_d[0] != '\0') {
            stroke = icons::path_from_d(QString::fromUtf8(def.stroke_d));
            check(!stroke.isEmpty(), case_prefix + "/stroke-parses", std::string(def.stroke_d));
        }
        if (def.fill_d[0] != '\0') {
            fill = icons::path_from_d(QString::fromUtf8(def.fill_d));
            check(!fill.isEmpty(), case_prefix + "/fill-parses", std::string(def.fill_d));
        }
        check(def.stroke_d[0] != '\0' || def.fill_d[0] != '\0', case_prefix + "/has-shape",
              "both d strings empty");

        // path() = stroke ∪ fill（同源合成）：元素数一致、逐点一致
        const QPainterPath shape = icons::path(def.name);
        const QPainterPath expect = [&] {
            QPainterPath out = stroke;
            out.addPath(fill);
            return out;
        }();
        bool same = shape.elementCount() == expect.elementCount();
        for (int e = 0; same && e < shape.elementCount(); ++e) {
            const QPainterPath::Element a = shape.elementAt(e);
            const QPainterPath::Element b = expect.elementAt(e);
            same = a.type == b.type && std::abs(a.x - b.x) < 1e-6 && std::abs(a.y - b.y) < 1e-6;
        }
        check(same, case_prefix + "/path-matches-table", "path() != kDefs 的 d 串解析结果");

        // 形不越界（含描边半宽 0.75 —— 越界 = 形被裁）
        const QRectF bounds = shape.boundingRect();
        check(!shape.isEmpty() && bounds.left() >= -margin && bounds.top() >= -margin &&
                  bounds.right() <= icons::kGrid + margin &&
                  bounds.bottom() <= icons::kGrid + margin,
              case_prefix + "/bounds",
              "rect=" + std::to_string(double(bounds.left())) + "," +
                  std::to_string(double(bounds.top())) + "," +
                  std::to_string(double(bounds.right())) + "," +
                  std::to_string(double(bounds.bottom())));

        // 键名往返（from_key(key(name)) == name）
        icons::Name back{};
        check(icons::from_key(QLatin1String(def.key), &back) && back == def.name,
              case_prefix + "/key-roundtrip", std::string(def.key));

        // 风格口径：无描边只有填充 = Fill，其余 = Stroke
        const icons::Style want = (def.stroke_d[0] == '\0' && def.fill_d[0] != '\0')
                                      ? icons::Style::Fill
                                      : icons::Style::Stroke;
        check(icons::style(def.name) == want, case_prefix + "/style", "");
    }

    // ---- 线宽口径锁（§3.3「1.4–1.6px」）：常量本身是 ui_smoke 探针的同一判据 ----
    check(icons::kStrokePx >= icons::kStrokeMinPx && icons::kStrokePx <= icons::kStrokeMaxPx,
          "stroke/range", "kStrokePx=" + std::to_string(double(icons::kStrokePx)));
    check(icons::kGrid == 24.0, "grid/24", std::to_string(double(icons::kGrid)));

    if (g_failed == 0) {
        std::printf("test_icons: OK\n");
        return 0;
    }
    std::printf("test_icons: FAILED (%d)\n", g_failed);
    return g_failed;
}
