// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — filelistmodel 族共享件（私有头，**不入 PP-FROZEN 面**）
//
// 契约（deep-review-0.3.2.md R-200/E6-01 + M7-R2.1-F3）：filelistmodel.cpp 原一 TU 6 类
// 8 职责，按所有权拆为 filelistmodel.cpp（模型）/ filedelegate.cpp（行委托）/
// filegroup_proxy.cpp（分组代理）/ filegroup_delegate.cpp（分组委托 + 树视图）/
// file_collect.cpp（纯收集 + 后台扫盘）/ group_scan.cpp（异步 EXIF 摘要）——
// 拆分口径 = **行为零变化**（函数体逐字搬移；冻结头 filelistmodel.h 逐字不动）。
// 本头只承载跨 TU 共享的**纯函数/常量/载荷**（原 filelistmodel.cpp 匿名命名空间件，
// 逐字搬入并补 inline —— 语义与链接形态不变），以及两个 Runnable 的薄工厂声明
// （实现分别在 file_collect.cpp / group_scan.cpp）。
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include <QApplication> // theme.h 的 style_mirrors_system_accent() 用 qApp（include 序要求）
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QRect>
#include <QRunnable>
#include <QString>
#include <QStringList>
#include <QStyleOptionViewItem>

#include "core/fsops.h"       // pp::input_extensions（扩展名白名单）
#include "ui/filelistmodel.h" // FileRow / ScannedPath / FileListModel
#include "ui/term_words.h"
#include "ui/theme.h"

namespace pp::ui {

// ---- §3.1 FileState 12 态映射（文本 + **色调**）唯一表；顺序 = pp::FileState 枚举顺序 ----
// M5-T5（A6 令牌收口）：原表里的硬编码色（#808080/#0088cc/#22aa22/#999999/#dd3333）作废 ——
// 色调键交给 theme::pill_paint（§4.1 末行「同色 8–10% 底 + 30–34% 描边 + 同色字」单源）：
//   排队/跳过/取消 = 中性（n）、进行中（探测→写入）= accent、完成 = ok、失败 = err（§8 四档语义）。
struct StateStyle {
    const char *text;
    const char *tone;
};
constexpr StateStyle kStateStyle[] = {
    {term_words::kQueued, theme::kPillNeutral},    // Queued
    {"探测", theme::kPillAccent},                  // Probing
    {"解码", theme::kPillAccent},                  // Decoding
    {"旋转", theme::kPillAccent},                  // Orienting
    {"色彩", theme::kPillAccent},                  // Coloring
    {"合成", theme::kPillAccent},                  // Flattening
    {"编码", theme::kPillAccent},                  // Encoding
    {"写入", theme::kPillAccent},                  // Writing
    {term_words::kDone, theme::kPillOk},           // Done（R-139 词表：与运行页同词）
    {term_words::kSkipped, theme::kPillNeutral},   // Skipped
    {term_words::kFailed, theme::kPillErr},        // Failed
    {term_words::kCancelled, theme::kPillNeutral}, // Cancelled（R-139 词表）
    // ---- M7-WP6 加性两档（枚举尾部 I7/R-008；既有 12 行语义逐字不动）----
    // R-008：Progress 是**过程**态，不再落列表模型（只喂 page_run）；此行只作兜底映射
    // （万一落表也不再误报"排队"）。
    {term_words::kRunning, theme::kPillAccent}, // Progress
    {term_words::kPartial, theme::kPillWarn},   // DoneWithErrors（R-076：多输出部分成功档）
};
// 映射表必须覆盖 FileState **全部**枚举值（原 12 行断言扩到 14 行 = 断言面只增不减：
// 语义仍是"逐枚举恰好一项"，换锚说明见 build/m7-wp6/handoff.md 裁定登记）。
static_assert(std::size(kStateStyle) == 14, "§3.1 状态映射必须覆盖 FileState 全部枚举");

inline const StateStyle &state_style(pp::FileState st) {
    const std::size_t i = static_cast<std::size_t>(st);
    return kStateStyle[i < std::size(kStateStyle) ? i : 0];
}

inline QString state_text(pp::FileState st) { return QString::fromUtf8(state_style(st).text); }

inline const char *state_tone(pp::FileState st) { return state_style(st).tone; }

// ---- M7-WP6 / R-069：0 字节产物事实的行级判据（三处状态面同判的唯一实现）----
// 终态事件把"输出 0 字节"回传进 FileRow::zero_byte_output；完成档（Done / DoneWithErrors）
// 一旦带该事实，胶囊不再显绿色「已完成」，改显琥珀「0 字节」—— 与运行页的「异常/0 字节」同判。
inline bool row_zero_anomaly(const FileRow &r) {
    return r.zero_byte_output &&
           (r.state == pp::FileState::Done || r.state == pp::FileState::DoneWithErrors);
}

inline QString row_state_text(const FileRow &r) {
    return row_zero_anomaly(r) ? QString::fromUtf8(term_words::kZeroBytes) : state_text(r.state);
}

inline const char *row_state_tone(const FileRow &r) {
    return row_zero_anomaly(r) ? theme::kPillWarn : state_tone(r.state);
}

// §7.5 行内「大小」项的格式化：3 位有效数字（mockup 行内示例 "3.21 MB" / "64.2 MB" / "142 MB"）
inline QString format_size(qint64 bytes) {
    if (bytes < 0)
        return {};
    static const char *const kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    double value = double(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(kUnits)) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return QStringLiteral("%1 B").arg(bytes); // 字节档不用小数
    const int digits = value >= 100.0 ? 0 : (value >= 10.0 ? 1 : 2);
    return QStringLiteral("%1 %2").arg(value, 0, 'f', digits).arg(QLatin1String(kUnits[unit]));
}

// EXIF 生效时间 "YYYY:MM:DD HH:MM:SS" → 行内日期段 "YYYY-MM-DD"（形态不符 → 原样直出）
inline QString format_shot_date(const std::string &datetime) {
    QString text = QString::fromStdString(datetime).trimmed();
    if (text.size() < 10)
        return text;
    text.truncate(10);
    if (text.at(4) == QLatin1Char(':') && text.at(7) == QLatin1Char(':'))
        return text.replace(4, 1, QLatin1Char('-')).replace(7, 1, QLatin1Char('-'));
    return text;
}

constexpr const char *kExceptionTone = theme::kPillWarn; // 例外标记（矢量 tag 图标）色调

// 状态胶囊宽度（= formkit::PillBadge::sizeHint 的口径：内距 ×2 + 文字；边框画在盒内）
inline int state_pill_width(const QString &text) {
    const QFontMetrics fm(
        theme::font(theme::Typography::badge_px, theme::Typography::badge_weight));
    return theme::Metrics::pill_pad_x * 2 + fm.horizontalAdvance(text);
}

// 状态胶囊绘制：色 = theme::pill_paint（与 formkit::PillBadge 同一取色函数/同一 R4 几何；
// 委托无法承载 QWidget，故按令牌复述形状 —— 半径/内距/字阶都取自主题单源，漂移即探针红）
inline void draw_state_pill(QPainter *painter, const QRect &box, const theme::Tokens &t,
                            const char *tone, const QString &text) {
    const theme::PillPaint paint = theme::pill_paint(t, tone);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF rect = QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5);
    painter->setPen(QPen(paint.bd, 1.0));
    painter->setBrush(paint.bg);
    painter->drawRoundedRect(rect, theme::Metrics::pill_radius, theme::Metrics::pill_radius);
    painter->setFont(theme::font(theme::Typography::badge_px, theme::Typography::badge_weight));
    painter->setPen(paint.fg);
    painter->drawText(box, Qt::AlignCenter, text);
    painter->restore();
}

// 选中面（修 3-17）：两列各画自己那半；行首（leading）2px 强调条只画在行首列（列 0）。
// R-218（M7-R2E2）：强调条贴**行首**（RTL = 物理右缘）；LTR 路径逐字节不变。
inline void paint_selection(QPainter *painter, const QRect &item_rect, const theme::Tokens &t,
                            bool first_column, bool rtl) {
    painter->save();
    painter->fillRect(item_rect, FileDelegate::selection_fill(t));
    if (first_column) {
        const int bar_x =
            rtl ? item_rect.right() - FileDelegate::kSelectionBarW + 1 : item_rect.left();
        painter->fillRect(
            QRect(bar_x, item_rect.top(), FileDelegate::kSelectionBarW, item_rect.height()),
            FileDelegate::selection_bar(t));
    }
    painter->restore();
}

// ============================================================================================
// R-218（M7-R2E2）：RTL 逻辑坐标/镜像（docs/deep-review-0.3.2.md R-218/E4-16）
//   自绘几何一律先在**逻辑坐标**（leading 在左）算好，再经 mirror_x 单点镜像到物理坐标；
//   rtl=false 时全部原样返回 —— LTR 路径逐字节不变（golden/ui_smoke 冻结面背书）。
//   镜像关系由 ui_smoke 的 rtl-mirror 探针钉死。
// ============================================================================================

// 物理 x' = bounds.left() + bounds.right() - r.right()（绕 bounds 中线镜像；LTR 恒等）
inline QRect mirror_x(const QRect &r, const QRect &bounds) {
    return QRect(bounds.left() + bounds.right() - r.right(), r.top(), r.width(), r.height());
}

inline bool rtl_of(const QStyleOptionViewItem &opt) { return opt.direction == Qt::RightToLeft; }

// 行内文本对齐：leading 对齐（LTR = 左 / RTL = 右；与镜像后的信息区配套）
inline Qt::Alignment text_align(bool rtl) {
    return rtl ? Qt::Alignment(Qt::AlignRight) : Qt::Alignment(Qt::AlignLeft);
}

constexpr int kGroupCountAlphaPct = 80; // .cnt{opacity:.8}（节头计数）
// R-280：行尾分类色点边长（原裸字面量 8）—— mockup `.chip-dot{width:8px;height:8px}`。
constexpr int kChipDotPx = 8;
// R-280：分组结构重建去抖（原裸字面量 30ms）—— 摘要/键逐行落定 → 合并为一次重建。
constexpr int kRebuildDebounceMs = 30;

inline std::filesystem::path fs_path(const QString &s) {
    return std::filesystem::path(s.toStdString()).lexically_normal();
}
inline QString fs_string(const std::filesystem::path &p) {
    return QString::fromStdString(p.string());
}

// 目录自走/去重/stale-guard 全部走同一规范化形式（lexically_normal 字符串）
inline QString norm_string(const QString &s) { return fs_string(fs_path(s)); }

// R-149：同文件别名去重键 —— **弱规范化 + 大小写折叠**（Windows 大小写不敏感；8.3 短名 /
// UNC 前缀 / 符号链接由 weakly_canonical 收敛到同一实体名）。弱规范化只解析存在的前缀、
// 不因尾段缺失而失败；取不到（盘符不存在等）→ 退化 lexically_normal。显示路径不改（行内
// 仍显示用户拖入的那条），只有**去重判据**走本键 —— 同文件以别名二次入列/二次输出由此挡掉。
inline QString dedup_key(const std::filesystem::path &p) {
    std::error_code ec;
    std::filesystem::path canon = std::filesystem::weakly_canonical(p, ec);
    if (ec || canon.empty())
        canon = p.lexically_normal();
    QString key = fs_string(canon);
#ifdef _WIN32
    key = key.toLower(); // Windows 路径大小写折叠（Linux/macOS 大小写敏感，不折叠）
#endif
    return key;
}

// 扩展名白名单（大小写不敏感，pp::input_extensions() 的项不含点）
inline bool is_supported_ext(const std::filesystem::path &p) {
    std::string ext = p.extension().string();
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    const std::vector<std::string> &exts = pp::input_extensions();
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

// R-025：一次 add_paths 的扫盘产物（worker 线程产出、GUI 线程消费；纯数据）。
// 批内排序与逐文件 stat/去重键都在 worker 完成 —— GUI 线程只剩成批插入。
struct ScanBatch {
    std::vector<ScannedPath> files;
    std::vector<QString> unsupported_keys;
    std::size_t unreadable = 0;
};

// 纯收集（实现 = file_collect.cpp）：目录递归 + 逐文件 stat + 去重键 + 批内排序。
ScanBatch scan_inputs(const QStringList &paths);

// R-200 跨 TU 衔接的薄工厂（Runnable 本体分别在 file_collect.cpp / group_scan.cpp；
// 都是 setAutoDelete 的 QRunnable，经 FileListModel 的串行 scan_pool_ 启动）。
QRunnable *make_add_paths_task(FileListModel *model, QStringList paths);
QRunnable *make_group_scan_task(FileListModel *model, const QString &path);

} // namespace pp::ui
