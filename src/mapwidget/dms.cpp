// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M6-T10 — 坐标显示格式化：度分输出 / 逆解析 / 粗盒边界带判定（纯函数）。
//
// 与 docs/m6-map-upgrade.md §4.5（坐标基准透明化 R4）+ §6（事实核对表）对应：mapwidget 的
// 公共 API 契约已保证坐标一律 WGS-84，本文件只做**文本互转**与"显示存疑"判定，不碰基准面。
// 编译进 pp_core（CMake 的 PP_CORE_SOURCES 显式收编，UI 侧 REMOVE_ITEM 剔除）→ 单测可链接。
//
// 口径三条（写清即契约）：
//   1) deg_to_dms：输出「N/S/E/W 前缀 + 度 + 十进制分（3 位小数）」，逐字对齐 dms.h 的示例
//      "N 39°54.246'"。1 千分之一分 ≈ 1.67e-5°（≈1.85 m）为显示最小刻度 —— 分/度的进位
//      （59.9995' → 下一度的 0.000'）由「千分之一分」整数量化天然处理，不做浮点进位特判。
//   2) dms_to_deg：兼容 "N 39°54.246'" / "39°54.246'N" / 秒形态 "N 39°54'14.8\"" / 纯十进制度
//      （"39.9042" / "-74.006"）；度分秒三段任一可缺，分、秒必须 ≥0 且 <60，尾随垃圾即拒绝。
//   3) coord_precision_suspect：与 coord.cpp 的 `out_of_china` 共用**同一粗矩形**（常量
//      单源 = coord.h 的 kChinaBBox*，M7-WP7/R-307 收口，不再双份维护）。
//      判定落法（dms.h 头注的展开；R-105 修正过"外→true"的失实头注）：
//        - 盒内深处        → false（偏移两向一致，正常显示/写入）；
//        - 盒外（境外）    → false（coord.cpp 两向 identity，无"界内界外不一致"可提示）；
//        - 盒内贴近任一边界 → true（**盒边缘一带** = 距任一边 ≤ kEdgeBandDeg）。
//      该带只说明「靠近粗包围盒边界」（R-046：不是真实国境线/港澳台边界判定）：GCJ-02
//      偏移"归零/应用"在地图源与逆变换之间不一致，可致几十米级误差 —— UI 据此提示
//      「该区域坐标精度存疑」（§4.5 边界精度提示；文案同 R-046 口径）。

#include "mapwidget/dms.h"

#include "mapwidget/coord.h" // R-307：粗包围盒常量单源（kChinaBBox*）

#include <QChar>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <utility>

namespace pp::map {
namespace {

// 「盒边缘一带」的带宽（度）：盒内距任一边 ≤ 该值即判存疑（≈55 km；任务书 §4.5 授权 0.5°）
constexpr double kEdgeBandDeg = 0.5;

constexpr double kMinutesPerDegree = 60.0;
constexpr int kMilliPerMinute = 1000; // 「千分之一分」量化刻度

bool inside_rough_box(double lat, double lon) {
    return !(lon < kChinaBBoxLonMin || lon > kChinaBBoxLonMax || lat < kChinaBBoxLatMin ||
             lat > kChinaBBoxLatMax);
}

// 度符号 / 分 / 秒标记（ASCII 一律收；排印撇也兼容 —— 只认符号不认写法）
bool is_degree_mark(QChar c) { return c.unicode() == 0x00B0; } // °
bool is_minute_mark(QChar c) { return c == QLatin1Char('\'') || c.unicode() == 0x2032; }
bool is_second_mark(QChar c) { return c == QLatin1Char('"') || c.unicode() == 0x2033; }

// 半球字母 → 码：N=1 / S=-1 / E=2 / W=-2；非字母 → 0
int hemisphere_code(QChar c) {
    switch (c.toUpper().unicode()) {
    case 'N':
        return 1;
    case 'S':
        return -1;
    case 'E':
        return 2;
    case 'W':
        return -2;
    default:
        return 0;
    }
}

// 自 pos 起读一个浮点字面量（可选符号 + 数字 + 可选小数）；无数字 → false（pos 不动）
bool read_number(const QString &text, int &pos, double &out) {
    const int start = pos;
    int i = pos;
    if (i < text.size() && (text.at(i) == QLatin1Char('+') || text.at(i) == QLatin1Char('-'))) {
        ++i;
    }
    int digits = 0;
    while (i < text.size() && text.at(i).isDigit()) {
        ++i;
        ++digits;
    }
    if (i < text.size() && text.at(i) == QLatin1Char('.')) {
        ++i;
        while (i < text.size() && text.at(i).isDigit()) {
            ++i;
            ++digits;
        }
    }
    if (digits == 0) {
        return false;
    }
    bool ok = false;
    const double value = text.mid(start, i - start).toDouble(&ok);
    if (!ok) {
        return false;
    }
    out = value;
    pos = i;
    return true;
}

} // namespace

QString deg_to_dms(double deg, bool is_lat) {
    if (!std::isfinite(deg)) {
        return QString(); // 非有限值不成坐标（调用方另有有效性校验）
    }
    const bool negative = deg < 0.0; // -0.0 → N/E（与坐标语义一致）
    // 「千分之一分」整数量化：分/度的进位（59.9995' → 下一度 0.000'）由取模天然完成
    const long long milli =
        std::llround(std::fabs(deg) * kMinutesPerDegree * double(kMilliPerMinute));
    const long long degree = milli / (kMilliPerMinute * 60);
    const long long minute_milli = milli % (kMilliPerMinute * 60);
    const QChar hemi = is_lat ? (negative ? QLatin1Char('S') : QLatin1Char('N'))
                              : (negative ? QLatin1Char('W') : QLatin1Char('E'));
    return QStringLiteral("%1 %2°%3'")
        .arg(hemi)
        .arg(degree)
        .arg(QString::number(double(minute_milli) / double(kMilliPerMinute), 'f', 3));
}

std::optional<double> dms_to_deg(const QString &s) {
    const QString text = s.trimmed();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    int i = 0;
    const auto skip_spaces = [&text, &i] {
        while (i < text.size() && text.at(i).isSpace()) {
            ++i;
        }
    };

    skip_spaces();
    int prefix = 0; // 前缀半球字母
    if (i < text.size()) {
        const int code = hemisphere_code(text.at(i));
        if (code != 0) {
            prefix = code;
            ++i;
        }
    }
    skip_spaces();

    double degrees = 0.0;
    if (!read_number(text, i, degrees)) {
        return std::nullopt; // 无度数（纯垃圾/空）
    }
    double minutes = 0.0;
    double seconds = 0.0;
    if (i < text.size() && is_degree_mark(text.at(i))) {
        ++i;
        skip_spaces();
        const int after_degree = i;
        double value = 0.0;
        if (read_number(text, i, value)) {
            minutes = value;
            skip_spaces();
            if (i >= text.size() || !is_minute_mark(text.at(i))) {
                return std::nullopt; // 度后有数字但无分标记（"39°54.5"）→ 拒绝
            }
            ++i;
            skip_spaces();
            const int after_minute = i;
            double sec = 0.0;
            if (read_number(text, i, sec)) {
                seconds = sec;
                skip_spaces();
                if (i >= text.size() || !is_second_mark(text.at(i))) {
                    return std::nullopt; // 有秒数但无秒标记 → 拒绝（防吞后随字符）
                }
                ++i;
            } else {
                i = after_minute; // 度的「分」后直接结束/接字母（"39°54.246'"）
            }
        } else {
            i = after_degree; // 度符号后无分（"39°"）
        }
    }

    skip_spaces();
    int suffix = 0; // 后缀半球字母（"39°54.246'N"）
    if (i < text.size()) {
        const int code = hemisphere_code(text.at(i));
        if (code != 0) {
            suffix = code;
            ++i;
        }
    }
    skip_spaces();
    if (i != text.size()) {
        return std::nullopt; // 尾随垃圾
    }
    if (prefix != 0 && suffix != 0 && prefix != suffix) {
        return std::nullopt; // 前后半球自相矛盾
    }
    if (minutes < 0.0 || minutes >= kMinutesPerDegree || seconds < 0.0 ||
        seconds >= kMinutesPerDegree) {
        return std::nullopt; // 分/秒必须落在 [0, 60)
    }
    const int letter = prefix != 0 ? prefix : suffix;
    if (degrees < 0.0 && letter != 0) {
        return std::nullopt; // 显式负号与半球字母同时出现（含 S/W 双负）→ 拒绝
    }
    const bool negative = degrees < 0.0 || letter < 0;
    const double value = std::fabs(degrees) + minutes / kMinutesPerDegree +
                         seconds / (kMinutesPerDegree * kMinutesPerDegree);
    const double limit = (letter == 1 || letter == -1) ? 90.0 : 180.0; // N/S:90；E/W 与无字母:180
    if (value > limit) {
        return std::nullopt; // 长度方向越界（"N 95°0.000'"）
    }
    return negative ? -value : value;
}

bool coord_precision_suspect(double lat, double lon) {
    if (!std::isfinite(lat) || !std::isfinite(lon)) {
        return false; // 非有限值不成坐标，提示不适用
    }
    if (!inside_rough_box(lat, lon)) {
        return false; // 粗盒外（境外）：coord.cpp 两向 identity，无"界内界外不一致"
    }
    const double edge_dist = std::min({lat - kChinaBBoxLatMin, kChinaBBoxLatMax - lat,
                                       lon - kChinaBBoxLonMin, kChinaBBoxLonMax - lon});
    return edge_dist <= kEdgeBandDeg; // 盒边缘一带（含边界本身）
}

} // namespace pp::map
