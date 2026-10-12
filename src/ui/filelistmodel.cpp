// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — file list model + delegate (M1b-U4; M4-T11 左栏勾选/分组/分类)
//
// 规格落地（docs/m1b-tasks.md §2.8 + §3.1）：
//   - add_paths：文件与/或目录；目录递归自走；支持扩展名 = pp::input_extensions()
//     大小写不敏感；点文件/点目录跳过；不支持的普通文件只计数不入列（§2.8 v1.1：
//     按 lexically_normal 路径去重，重复 add 不重复累计；clear() 同时归零集合与计数）；
//     lexically_normal 去重；新增行入缩略图队列（endInsertRows 之后行号才有效）
//   - M7-R2D1 加性（R-025/148/149/026）：目录扫盘搬后台（GUI 不冻结；PP_UI_NO_STATE 下保持
//     同步供 smoke 走查确定性）；去重键 = 弱规范化 + 大小写折叠（别名归一）；枚举/读取失败
//     计入「N 项未能读取」；分组摘要回填走路径 → 行号哈希
//   - M2-T16b：每个 add_paths 批次内按路径字典序排序（跨批次仍为追加语义）
//   - StateTextRole 文本与徽标颜色共用本 TU 的 §3.1 十二态表（唯一来源）
//   - apply_thumb stale-guard：row 越界或 path 不匹配 → 丢弃（删行/清空后的在途结果）
//   - entries()：FileEntry 值语义深拷贝（src/base_dir/exception）供 Scheduler
//
// M4-T11（0.3.0）追加面（§3.6 裁定行 + §6.2；全为**加性**，既有实现逐字不动）：
//   * data/flags 增补：Qt::CheckStateRole（勾选 = 参与运行）、Qt::DecorationRole（行尾分类色点
//     .chip-dot 的色源）、ClassIdRole / GroupKeyRole（§3.6 追加的两枚 role）；
//   * 勾选集合（默认全勾 = v0.2 行为兼容）与整类圈选（check_class/invert_class/clear_checks）；
//   * 分类：单源 = `pp::ClassRegistry`（指针只读消费 + set_class 直写注册表，不复制第二份状态）；
//   * 分组：键 = `pp::group_key(mode, GroupSource)` 纯函数；源格式取自 probe 摘要（零额外 IO），
//     月份/相机型号由**独立单线程池**异步补扫 EXIF 摘要（stale-guard 按路径；失败/无字段归
//     「无分组」），就绪后发 group_source_changed()；
//   * `FileGroupProxyModel`（新增）：两列 + 分组节（顶层）= QTreeView 的层次层；
//   * `FileGroupDelegate`（新增）：勾选列 / 行内容列（转调冻结的 FileDelegate::paint）/
//     分组节头（mockup .ghead）三路分派 + 勾选自绘命中框。配色一律取 QPalette
//     （theme::palette(tokens) 已把 §9.2 tokens 映射进 Text/PlaceholderText/Highlight/
//     HighlightedText），故本 TU 零 tokens 依赖、随主题自动跟随。
//
// M5-T5（§7.5 文件行 + 裁定 A6/A3）追加面（**加性**；A6 明令的令牌收口即本段）：
//   * 行几何：58 高（§7.5）/ 缩略图 46 r4（R4 = theme::Metrics::thumb_px）/ 状态**胶囊**
//     （R4 16 r3 9.5/700，色 = theme::pill_paint 单源）；
//   * 信息 = 三行：文件名 12/600 + 「尺寸 · 大小 · 格式」+「日期 · 机型」（注释 10px = §4.2
//     辅助档，裁定 A3 作废 §7.5 的 10.5 表述）；`join_items` 保证**不截断**（放不下减行内项，
//     绝不产出省略号）；取数 = 追加 roles SizeRole / ShotDateRole / CameraRole（+ FileRow 加性
//     字段 size_bytes）；
//   * **A6 令牌收口**：`kStateStyle` 的 12 态硬编码色 → theme::Tokens（排队/跳过/取消 = text3、
//     进行中 = accent、完成 = ok、失败 = err；例外标记 #dd8800 → warn；第二行灰字 → text3）；
//     委托持 tokens 副本（FileDelegate::set_tokens，MainWindow::refresh_theme 重放）；
//   * **A6 废除自持副本**：`FileGroupDelegate::kCheckBox=15` 删除 → theme::Metrics::checkbox_px
//     （13；勾选框 = .cbx 13×13 r2，边框 1px --ctl-bd / 选中 accent 填充 + acc-on 勾形）；
//   * 选中面（修 3-17）：选中 = 整行 accent-dim 底 + 左缘 2px accent 条（与勾选框分属两个视觉
//     通道）；样式对 State_Selected 的填充被屏蔽（否则盖住淡底）。
#include "ui/filelistmodel.h"
#include "ui/icons.h"      // M5-T3：勾形/例外标记一律矢量图标（C3）—— 色取自 tokens（M5-T5/A6）
#include "ui/thumbnails.h" // 冻结的 filelistmodel.h 依赖 Thumbnailer 已声明（不自带该头）

#include <QAbstractItemView>
#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QMetaObject>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QRunnable>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QThreadPool>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

#include <exiv2/exiv2.hpp> // Exif.Image.Model（分组摘要；经 core/metadata.h 传递引入）

#include "core/fsops.h"
#include "core/metadata.h" // pp::read_metadata / pp::effective_datetime（分组摘要的来源）
#include "ui/theme.h"      // 令牌单源（排印/语义色/行内度量；M5-T5/A6 起本 TU 全面消费）

namespace pp::ui {
namespace {

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

const StateStyle &state_style(pp::FileState st) {
    const std::size_t i = static_cast<std::size_t>(st);
    return kStateStyle[i < std::size(kStateStyle) ? i : 0];
}

QString state_text(pp::FileState st) { return QString::fromUtf8(state_style(st).text); }

const char *state_tone(pp::FileState st) { return state_style(st).tone; }

// ---- M7-WP6 / R-069：0 字节产物事实的行级判据（三处状态面同判的唯一实现）----
// 终态事件把"输出 0 字节"回传进 FileRow::zero_byte_output；完成档（Done / DoneWithErrors）
// 一旦带该事实，胶囊不再显绿色「已完成」，改显琥珀「0 字节」—— 与运行页的「异常/0 字节」同判。
bool row_zero_anomaly(const FileRow &r) {
    return r.zero_byte_output &&
           (r.state == pp::FileState::Done || r.state == pp::FileState::DoneWithErrors);
}

QString row_state_text(const FileRow &r) {
    return row_zero_anomaly(r) ? QString::fromUtf8(term_words::kZeroBytes) : state_text(r.state);
}

const char *row_state_tone(const FileRow &r) {
    return row_zero_anomaly(r) ? theme::kPillWarn : state_tone(r.state);
}

// §7.5 行内「大小」项的格式化：3 位有效数字（mockup 行内示例 "3.21 MB" / "64.2 MB" / "142 MB"）
QString format_size(qint64 bytes) {
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
QString format_shot_date(const std::string &datetime) {
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
int state_pill_width(const QString &text) {
    const QFontMetrics fm(
        theme::font(theme::Typography::badge_px, theme::Typography::badge_weight));
    return theme::Metrics::pill_pad_x * 2 + fm.horizontalAdvance(text);
}

// 状态胶囊绘制：色 = theme::pill_paint（与 formkit::PillBadge 同一取色函数/同一 R4 几何；
// 委托无法承载 QWidget，故按令牌复述形状 —— 半径/内距/字阶都取自主题单源，漂移即探针红）
void draw_state_pill(QPainter *painter, const QRect &box, const theme::Tokens &t, const char *tone,
                     const QString &text) {
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
void paint_selection(QPainter *painter, const QRect &item_rect, const theme::Tokens &t,
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
QRect mirror_x(const QRect &r, const QRect &bounds) {
    return QRect(bounds.left() + bounds.right() - r.right(), r.top(), r.width(), r.height());
}

bool rtl_of(const QStyleOptionViewItem &opt) { return opt.direction == Qt::RightToLeft; }

// 行内文本对齐：leading 对齐（LTR = 左 / RTL = 右；与镜像后的信息区配套）
Qt::Alignment text_align(bool rtl) {
    return rtl ? Qt::Alignment(Qt::AlignRight) : Qt::Alignment(Qt::AlignLeft);
}

constexpr int kGroupCountAlphaPct = 80; // .cnt{opacity:.8}（节头计数）
// R-280：行尾分类色点边长（原裸字面量 8）—— mockup `.chip-dot{width:8px;height:8px}`。
constexpr int kChipDotPx = 8;
// R-280：分组结构重建去抖（原裸字面量 30ms）—— 摘要/键逐行落定 → 合并为一次重建。
constexpr int kRebuildDebounceMs = 30;

std::filesystem::path fs_path(const QString &s) {
    return std::filesystem::path(s.toStdString()).lexically_normal();
}
QString fs_string(const std::filesystem::path &p) { return QString::fromStdString(p.string()); }

// 目录自走/去重/stale-guard 全部走同一规范化形式（lexically_normal 字符串）
QString norm_string(const QString &s) { return fs_string(fs_path(s)); }

// R-149：同文件别名去重键 —— **弱规范化 + 大小写折叠**（Windows 大小写不敏感；8.3 短名 /
// UNC 前缀 / 符号链接由 weakly_canonical 收敛到同一实体名）。弱规范化只解析存在的前缀、
// 不因尾段缺失而失败；取不到（盘符不存在等）→ 退化 lexically_normal。显示路径不改（行内
// 仍显示用户拖入的那条），只有**去重判据**走本键 —— 同文件以别名二次入列/二次输出由此挡掉。
QString dedup_key(const std::filesystem::path &p) {
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
bool is_supported_ext(const std::filesystem::path &p) {
    std::string ext = p.extension().string();
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    const std::vector<std::string> &exts = pp::input_extensions();
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

// 单个输入项 → 候选文件（目录递归；点文件/点目录跳过；不支持者按去重路径计数，v1.1）。
// R-148：枚举/读取失败的输入项**计数**（原实现静默忽略，用户不知道漏了什么）——目录打不开 /
// 枚举中断 / 输入项不存在或不可读，各计 1 项「未能读取」。
void collect_from(const std::filesystem::path &input, std::vector<std::filesystem::path> &files,
                  std::set<QString> &unsupported_seen, std::size_t &unsupported,
                  std::size_t &unreadable) {
    // 不支持文件：仅首次遇到（自上次 clear 以来）才计数（§2.8 v1.1 裁定）
    const auto count_unsupported = [&](const std::filesystem::path &p) {
        if (unsupported_seen.insert(fs_string(p.lexically_normal())).second)
            ++unsupported;
    };
    std::error_code ec;
    if (std::filesystem::is_directory(input, ec)) {
        std::filesystem::recursive_directory_iterator it(
            input, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) {
            ++unreadable; // R-148：目录打不开（权限/竞态）→ 计入「未能读取」
            return;
        }
        const std::filesystem::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) {
                ++unreadable; // R-148：枚举中断（该子树的剩余项读不到）
                break;
            }
            const std::filesystem::path &p = it->path();
            const std::string name = p.filename().string();
            if (!name.empty() && name.front() == '.') { // 点文件/点目录
                std::error_code dir_ec;
                if (it->is_directory(dir_ec))
                    it.disable_recursion_pending();
                continue;
            }
            std::error_code type_ec;
            if (!it->is_regular_file(type_ec)) {
                if (type_ec)
                    ++unreadable; // R-148：条目 stat 失败（被删/无权限）
                continue;
            }
            if (is_supported_ext(p)) {
                files.push_back(p.lexically_normal());
            } else {
                count_unsupported(p); // 不支持：计数但不入列
            }
        }
        return;
    }
    std::error_code file_ec;
    if (std::filesystem::is_regular_file(input, file_ec)) {
        if (is_supported_ext(input)) {
            files.push_back(input.lexically_normal());
        } else {
            count_unsupported(input);
        }
        return;
    }
    // R-148：不存在/非普通文件/不可读 → 计入「未能读取」（原为静默忽略）
    ++unreadable;
}

// R-025：一次 add_paths 的扫盘产物（worker 线程产出、GUI 线程消费；纯数据）。
// 批内排序与逐文件 stat/去重键都在 worker 完成 —— GUI 线程只剩成批插入。
struct ScanBatch {
    std::vector<ScannedPath> files;
    std::vector<QString> unsupported_keys;
    std::size_t unreadable = 0;
};

ScanBatch scan_inputs(const QStringList &paths) {
    ScanBatch batch;
    std::vector<std::filesystem::path> found;
    std::set<QString> unsupported_seen;
    std::size_t unsupported = 0;
    for (const QString &p : paths) {
        if (p.isEmpty())
            continue;
        collect_from(fs_path(p), found, unsupported_seen, unsupported, batch.unreadable);
    }
    // M2-T16b：批次内按路径字典序排序后再追加 —— UI 行序不再依赖 readdir 顺序。
    std::sort(found.begin(), found.end(),
              [](const std::filesystem::path &a, const std::filesystem::path &b) {
                  return a.string() < b.string();
              });
    batch.files.reserve(found.size());
    for (const std::filesystem::path &f : found) {
        ScannedPath item;
        item.path = fs_string(f);
        item.dedup_key = dedup_key(f); // R-149：别名归一（worker 内做，GUI 零 IO）
        std::error_code size_ec;
        const std::uintmax_t bytes = std::filesystem::file_size(f, size_ec);
        item.size_bytes = size_ec ? -1 : static_cast<qint64>(bytes); // M5-T5 行内「大小」
        batch.files.push_back(std::move(item));
    }
    batch.unsupported_keys.assign(unsupported_seen.begin(), unsupported_seen.end());
    return batch;
}

// R-025：后台扫盘任务（串行走 scan_pool_，与 EXIF 摘要同池 —— 都是只读 IO，不与缩略图/转码
// 抢核）。结果经 Qt 队列投递回 GUI 线程（context = 模型自身，模型析构则事件自动作废）。
class AddPathsRunnable : public QRunnable {
public:
    AddPathsRunnable(FileListModel *model, QStringList paths)
        : model_(model), paths_(std::move(paths)) {
        setAutoDelete(true);
    }
    void run() override {
        ScanBatch batch = scan_inputs(paths_);
        FileListModel *const model = model_;
        QMetaObject::invokeMethod(
            model,
            [model, batch] {
                model->apply_scanned_paths(batch.files, batch.unsupported_keys, batch.unreadable);
            },
            Qt::QueuedConnection);
    }

private:
    FileListModel *model_ = nullptr;
    QStringList paths_;
};

} // namespace

// ---------------------------------------------------------------- FileListModel

FileListModel::FileListModel(QObject *parent) : QAbstractListModel(parent) {}

int FileListModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(rows_.size());
}

QVariant FileListModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() < 0 || std::size_t(idx.row()) >= rows_.size())
        return {};
    const FileRow &r = rows_[std::size_t(idx.row())];
    switch (role) {
    case ThumbRole:
        return QVariant::fromValue(r.thumb);
    case NameRole:
        return fs_string(r.entry.src.filename());
    case PathRole:
        return fs_string(r.entry.src);
    case DimRole:
        return r.probe_ok ? QStringLiteral("%1×%2").arg(r.info.width).arg(r.info.height)
                          : QString();
    case FormatRole:
        return r.probe_ok ? QString::fromStdString(r.info.format).toUpper() : QString();
    case StateRole:
        return int(r.state);
    case StateTextRole:
        return row_state_text(r); // R-069：0 字节事实改判「0 字节」
    case StateToneRole:           // R-069：色调同源（原由委托按枚举自算）
        return QLatin1String(row_state_tone(r));
    case ExceptionRole:
        return r.has_exception();
    case ErrorRole:
        return r.error;
    // ---- M4-T11（§3.6 追加面）----
    case Qt::CheckStateRole:
        return r.checked ? int(Qt::Checked) : int(Qt::Unchecked);
    case Qt::DecorationRole: {
        // 行尾分类色点（mockup .chip-dot）：色源 = 注册表；无分类 → 无效 QVariant（不画点）
        const pp::ClassDef *def =
            classes_ != nullptr ? classes_->find(class_id_of(std::size_t(idx.row()))) : nullptr;
        if (def == nullptr)
            return {};
        return QColor(QRgb(def->rgb & 0xFFFFFFu));
    }
    case ClassIdRole:
        return QString::fromStdString(class_id_of(std::size_t(idx.row())));
    case GroupKeyRole:
        return group_key_of(std::size_t(idx.row()));
    // ---- M5-T5（§7.5 第二/三行信息）----
    case SizeRole:
        return format_size(r.size_bytes); // "" = 未知 → 行内不显示"大小"
    case ShotDateRole:
        return format_shot_date(r.scan_datetime); // EXIF 生效时间（异步摘要；"" = 无）
    case CameraRole:
        return QString::fromStdString(r.scan_camera); // Exif.Image.Model（"" = 无）
    default:
        return {};
    }
}

Qt::ItemFlags FileListModel::flags(const QModelIndex &idx) const {
    if (!idx.isValid())
        return Qt::NoItemFlags;
    // M4-T11：ItemIsUserCheckable = 勾选入口（§6.2/D2）；内容列由委托自绘命中框，行本身不可编辑。
    // GUI-R2 修「选中后出现两排选择框」：勾选语义**只属勾选列** —— 对内容列也给 UserCheckable
    // 会让 initStyleOption 置上 HasCheckIndicator，样式于是又在内容列左缘画一个原生勾选框
    // （实拍：缩略图左侧露出第二枚框，选中高亮后更显眼）。
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (idx.column() == FileGroupProxyModel::kCheckColumn)
        f |= Qt::ItemIsUserCheckable;
    return f;
}

bool FileListModel::setData(const QModelIndex &idx, const QVariant &value, int role) {
    if (role != Qt::CheckStateRole || !idx.isValid() || idx.row() < 0 ||
        std::size_t(idx.row()) >= rows_.size()) {
        return false;
    }
    set_checked(std::size_t(idx.row()), value.toInt() == int(Qt::Checked));
    return true;
}

void FileListModel::add_paths(const QStringList &paths) {
    if (paths.isEmpty())
        return;
    // R-025：目录递归扫盘（recursive_directory_iterator + 逐文件 stat + 别名归一）搬后台 ——
    // 大目录/网络盘拖入不再冻结 GUI（poll/取消不走）。回填见 apply_scanned_paths。
    // smoke/dev harness（PP_UI_NO_STATE）保持**同步**：dev_ui_smoke 在 add_paths 返回后立即
    // 走查（无等待点），异步会让探针读到空列表成为竞态 —— 判据确定性优先（裁定见 handoff）。
    if (qEnvironmentVariableIsSet("PP_UI_NO_STATE")) {
        const ScanBatch batch = scan_inputs(paths);
        apply_scanned_paths(batch.files, batch.unsupported_keys, batch.unreadable);
        return;
    }
    if (scan_pool_ == nullptr) {
        scan_pool_ = new QThreadPool(this);
        scan_pool_->setMaxThreadCount(1); // 串行：扫盘/EXIF 摘要都是只读 IO，不与转码抢核
    }
    scan_pool_->start(new AddPathsRunnable(this, paths));
}

void FileListModel::apply_scanned_paths(const std::vector<ScannedPath> &files,
                                        const std::vector<QString> &unsupported_keys,
                                        std::size_t unreadable) {
    // 去重：已入列（known_paths_）与本批次内重复都跳过（R-149：键 = 别名归一键）
    std::vector<const ScannedPath *> fresh;
    std::set<QString> batch_keys;
    for (const ScannedPath &f : files) {
        if (known_paths_.contains(f.dedup_key) || !batch_keys.insert(f.dedup_key).second)
            continue;
        fresh.push_back(&f);
    }

    const std::size_t first_new = rows_.size();
    if (!fresh.empty()) {
        beginInsertRows(QModelIndex(), int(first_new), int(first_new + fresh.size() - 1));
        rows_.reserve(rows_.size() + fresh.size());
        for (const ScannedPath *f : fresh) {
            FileRow row;
            row.entry.src = fs_path(f->path);
            row.entry.base_dir = row.entry.src.parent_path(); // §2.8：base_dir = src.parent
            row.size_bytes = f->size_bytes; // M5-T5：stat 已在 worker 做（-1 = 未知，不显 0）
            row.dedup_key = f->dedup_key;
            known_paths_.insert(f->dedup_key);
            row_by_key_[row.entry.src.string()] = rows_.size(); // R-026：路径 → 行号
            rows_.push_back(std::move(row));
        }
        endInsertRows();
        // R-024：增量计数（新行默认 checked = true、无例外）
        checked_ += fresh.size();
    }
    // R-148：枚举/读取失败计入「N 项未能读取」
    unreadable_ += unreadable;
    // 不支持计数：按去重键只记首次（v1.1 口径；键集合与旧行为同源）
    std::size_t unsupported_new = 0;
    for (const QString &key : unsupported_keys) {
        if (unsupported_paths_.insert(key).second)
            ++unsupported_new;
    }
    unsupported_ += unsupported_new;

    // 新增行 → 缩略图入队（此时行号已生效）
    if (thumbs_) {
        for (std::size_t i = first_new; i < rows_.size(); ++i) {
            thumbs_->request(int(i), fs_string(rows_[i].entry.src));
        }
    }
    if (!fresh.empty())
        scan_group_sources(); // M4-T11：新增行的分组摘要（月份/相机型号）异步补扫
    if (!fresh.empty() || unsupported_new != 0 || unreadable != 0)
        emit content_changed();
}

void FileListModel::remove_rows(const QList<int> &rows) {
    if (rows.isEmpty() || rows_.empty())
        return;

    std::vector<int> idx;
    idx.reserve(std::size_t(rows.size()));
    for (int r : rows) {
        if (r >= 0 && std::size_t(r) < rows_.size())
            idx.push_back(r);
    }
    if (idx.empty())
        return;
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());

    // R-248：一次移除只发**一段连续区间**的 beginRemoveRows（原实现逐行发 N 次 →
    // 每次都触发分组代理的结构重建）。连续段合并后按段删除，N 个散行退化为「段数」次信号。
    // 从大到小删段，行号不漂移。
    std::vector<std::pair<int, int>> runs; // [first, last] 闭区间
    for (std::size_t k = 0; k < idx.size();) {
        int first = idx[k];
        int last = first;
        std::size_t j = k + 1;
        while (j < idx.size() && idx[j] == last + 1) {
            last = idx[j];
            ++j;
        }
        runs.emplace_back(first, last);
        k = j;
    }
    for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
        const int first = it->first;
        const int last = it->second;
        beginRemoveRows(QModelIndex(), first, last);
        for (int r = last; r >= first; --r) {
            known_paths_.erase(rows_[std::size_t(r)].dedup_key); // R-149：别名归一键
            if (rows_[std::size_t(r)].checked && checked_ > 0)
                --checked_; // R-024：增量计数
            if (rows_[std::size_t(r)].has_exception() && exceptions_ > 0)
                --exceptions_;
            rows_.erase(rows_.begin() + r);
        }
        endRemoveRows();
    }
    // R-026：行号漂移 → 路径 → 行号表整体重建（删除是用户动作尺度，O(n) 可接受）
    row_by_key_.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i)
        row_by_key_[rows_[i].entry.src.string()] = i;
    // 在途/排队结果由 apply_thumb stale-guard（行号漂移 → path 不匹配）丢弃；
    // 行号下移后重新入队仍缺探测信息的行（否则被删行之后的行会永久失去缩略图）
    enqueue_pending_thumbs();
    emit content_changed();
}

void FileListModel::clear() {
    if (thumbs_)
        thumbs_->clear_pending(); // 在途结果 → stale（apply_thumb 亦按越界丢弃）
    const bool changed = !rows_.empty() || unsupported_ != 0 || unreadable_ != 0;
    beginResetModel();
    rows_.clear();
    known_paths_.clear();
    unsupported_paths_.clear(); // v1.1：去重集合与计数一起归零
    unsupported_ = 0;
    unreadable_ = 0;     // R-148：与其它计数一起归零
    row_by_key_.clear(); // R-026
    checked_ = 0;        // R-024：增量计数归零
    exceptions_ = 0;
    endResetModel();
    if (changed)
        emit content_changed();
}

std::size_t FileListModel::size() const { return rows_.size(); }
bool FileListModel::empty() const { return rows_.empty(); }

const FileRow &FileListModel::row(std::size_t i) const {
    static const FileRow kEmpty; // 越界返回空行（内部不变量：调用方先查 size()）
    Q_ASSERT(i < rows_.size());
    return i < rows_.size() ? rows_[i] : kEmpty;
}

std::size_t FileListModel::unsupported_count() const { return unsupported_; }

std::size_t FileListModel::unreadable_count() const { return unreadable_; } // R-148

std::size_t FileListModel::exception_count() const {
    // R-024：增量计数（原 O(n) 全表扫；运行期逐文件事件 × refresh_status 会反复压 GUI 线程）
    return exceptions_;
}

std::vector<pp::FileEntry> FileListModel::entries() const {
    std::vector<pp::FileEntry> out;
    out.reserve(rows_.size());
    for (const FileRow &r : rows_)
        out.push_back(r.entry); // 值语义深拷贝（含 exception）
    return out;
}

void FileListModel::set_state(std::size_t i, pp::FileState st) {
    if (i >= rows_.size() || rows_[i].state == st)
        return;
    rows_[i].state = st;
    const QModelIndex idx = index(int(i));
    emit dataChanged(idx, idx, {StateRole, StateTextRole, StateToneRole});
    emit content_changed(); // 各态计数属"摘要相关"
}

// R-069：0 字节产物事实（终态事件回传）
void FileListModel::set_zero_byte_output(std::size_t i, bool on) {
    if (i >= rows_.size() || rows_[i].zero_byte_output == on)
        return;
    rows_[i].zero_byte_output = on;
    const QModelIndex idx = index(int(i));
    emit dataChanged(idx, idx, {StateTextRole, StateToneRole});
}

void FileListModel::set_exception(std::size_t i, std::optional<pp::MetadataOverride> ex) {
    if (i >= rows_.size())
        return;
    FileRow &r = rows_[i];
    const bool had = r.has_exception();
    r.entry.exception = std::move(ex);
    if (had == r.has_exception() && !had)
        return; // 无变化（都为空）
    if (had && !r.has_exception() && exceptions_ > 0)
        --exceptions_; // R-024：增量计数
    else if (!had && r.has_exception())
        ++exceptions_;
    const QModelIndex idx = index(int(i));
    emit dataChanged(idx, idx, {ExceptionRole});
    emit exception_changed(i);
}

void FileListModel::apply_thumb(int row, const QString &path, const pp::ImageInfo &info,
                                bool probe_ok, const QString &error, const QImage &thumb) {
    if (row < 0 || std::size_t(row) >= rows_.size())
        return; // stale：行越界（删行/清空）
    FileRow &r = rows_[std::size_t(row)];
    if (norm_string(path) != fs_string(r.entry.src))
        return; // stale：path 不匹配（行漂移）

    r.info = info;
    r.probe_ok = probe_ok;
    r.thumb = thumb;
    const bool was_failed = (r.state == pp::FileState::Failed);
    bool state_written = false;
    if (probe_ok) {
        r.error.clear();
    } else {
        r.error = error.isEmpty() ? tr("探测失败") : tr("探测失败：%1").arg(error);
        // R-227：探测结果**不得**覆写流水线状态 —— 缩略图探测是旁路（只补 info/thumb），
        // 迟到的失败结果若把 Done/Failed/Cancelled 等终态盖成"失败"，列表胶囊就与运行页矛盾。
        // 只有"什么都还没发生"（Queued）的行才由探测失败定为失败态。
        if (r.state == pp::FileState::Queued) {
            r.state = pp::FileState::Failed; // 探测失败 = 该行错误态（thumbs.h：error 非空）
            state_written = true;
        }
    }
    const QModelIndex idx = index(row);
    // M4-T11：+GroupKeyRole（源格式模式的分组键随 probe 摘要落定）
    emit dataChanged(
        idx, idx,
        {ThumbRole, DimRole, FormatRole, StateRole, StateTextRole, ErrorRole, GroupKeyRole});
    if (!probe_ok && !was_failed && state_written)
        emit content_changed();
}

void FileListModel::set_thumbnailer(Thumbnailer *t) {
    if (thumbs_ == t)
        return;
    if (thumbs_)
        disconnect(thumbs_, nullptr, this, nullptr);
    thumbs_ = t;
    if (!thumbs_)
        return;
    // 结果回填：ready 已在 GUI 线程发出（Thumbnailer 队列投递）→ 直接连接
    connect(thumbs_, &Thumbnailer::ready, this, &FileListModel::apply_thumb);
    enqueue_pending_thumbs();
}

void FileListModel::enqueue_pending_thumbs() {
    if (!thumbs_)
        return;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const FileRow &r = rows_[i];
        if (r.probe_ok || !r.error.isEmpty())
            continue; // 已有结果 / 已判失败
        thumbs_->request(int(i), fs_string(r.entry.src));
    }
}

// ============================================================================================
// M4-T11 · 勾选（§6.2/D2：复选框 checked = 参与运行 = 批量规则作用范围）
// ============================================================================================

bool FileListModel::is_checked(std::size_t i) const {
    return i < rows_.size() ? rows_[i].checked : false;
}

void FileListModel::set_checked(std::size_t i, bool on) {
    if (i >= rows_.size() || rows_[i].checked == on)
        return;
    rows_[i].checked = on;
    // R-024：增量计数
    if (on)
        ++checked_;
    else if (checked_ > 0)
        --checked_;
    const QModelIndex idx = index(int(i));
    emit dataChanged(idx, idx, {Qt::CheckStateRole});
    emit checks_changed();
    emit content_changed(); // 底栏"已选 N / M"摘要相关
}

void FileListModel::set_all_checked(bool on) {
    bool any = false;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].checked != on) {
            rows_[i].checked = on;
            any = true;
        }
    }
    if (!any)
        return;
    checked_ = on ? rows_.size() : 0; // R-024：增量计数
    if (!rows_.empty())
        emit dataChanged(index(0), index(int(rows_.size()) - 1), {Qt::CheckStateRole});
    emit checks_changed();
    emit content_changed();
}

std::size_t FileListModel::checked_count() const {
    // R-024：增量计数（原 O(n) 全表扫；见 filelistmodel.h 的 checked_/exceptions_）
    return checked_;
}

std::vector<std::size_t> FileListModel::checked_rows() const {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].checked)
            out.push_back(i);
    }
    return out;
}

std::vector<pp::FileEntry> FileListModel::checked_entries() const {
    std::vector<pp::FileEntry> out;
    for (const FileRow &r : rows_) {
        if (r.checked)
            out.push_back(r.entry); // 值语义深拷贝（含 exception）
    }
    return out;
}

void FileListModel::check_class(const std::string &class_id) {
    const bool all = class_id.empty() || class_id == std::string(pp::ClassRegistry::kAllId);
    bool any = false;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (!all && class_id_of(i) != class_id)
            continue;
        if (!rows_[i].checked) {
            rows_[i].checked = true;
            ++checked_; // R-024：增量计数
            any = true;
        }
    }
    if (!any)
        return;
    if (!rows_.empty())
        emit dataChanged(index(0), index(int(rows_.size()) - 1), {Qt::CheckStateRole});
    emit checks_changed();
    emit content_changed();
}

void FileListModel::invert_class(const std::string &class_id) {
    const bool all = class_id.empty() || class_id == std::string(pp::ClassRegistry::kAllId);
    bool any = false;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (!all && class_id_of(i) != class_id)
            continue;
        rows_[i].checked = !rows_[i].checked;
        // R-024：增量计数
        if (rows_[i].checked)
            ++checked_;
        else if (checked_ > 0)
            --checked_;
        any = true;
    }
    if (!any)
        return;
    if (!rows_.empty())
        emit dataChanged(index(0), index(int(rows_.size()) - 1), {Qt::CheckStateRole});
    emit checks_changed();
    emit content_changed();
}

void FileListModel::clear_checks() { set_all_checked(false); }

// R-142：只清该类的勾（id 空/"all" = 全部 → 转发 clear_checks）
void FileListModel::clear_class_checks(const std::string &class_id) {
    const bool all = class_id.empty() || class_id == std::string(pp::ClassRegistry::kAllId);
    if (all) {
        clear_checks();
        return;
    }
    bool any = false;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (class_id_of(i) != class_id || !rows_[i].checked)
            continue;
        rows_[i].checked = false;
        if (checked_ > 0)
            --checked_; // R-024：增量计数
        any = true;
    }
    if (!any)
        return;
    if (!rows_.empty())
        emit dataChanged(index(0), index(int(rows_.size()) - 1), {Qt::CheckStateRole});
    emit checks_changed();
    emit content_changed();
}

// ============================================================================================
// M4-T11 · 分类（单源 = pp::ClassRegistry；本模型只读写它，不复制状态）
// ============================================================================================

void FileListModel::set_class_registry(pp::ClassRegistry *registry) {
    if (classes_ == registry)
        return;
    classes_ = registry;
    emit class_changed(); // 色点/归属全量刷新
}

pp::ClassRegistry *FileListModel::class_registry() const { return classes_; }

std::string FileListModel::class_id_of(std::size_t i) const {
    if (classes_ == nullptr || i >= rows_.size())
        return {};
    return classes_->class_of(rows_[i].entry.src).value_or(std::string());
}

void FileListModel::set_class(std::size_t i, const std::string &class_id) {
    if (classes_ == nullptr || i >= rows_.size())
        return;
    // assign() 自带口径（core/classify.h）：空 id / 保留 id「全部」/ 未知 id → 清除归属
    classes_->assign(rows_[i].entry.src, class_id);
    emit_row_data_changed(i, {ClassIdRole, Qt::DecorationRole});
    emit class_changed();
}

void FileListModel::notify_class_changed() {
    if (!rows_.empty()) {
        emit dataChanged(index(0), index(int(rows_.size()) - 1), {ClassIdRole, Qt::DecorationRole});
    }
    emit class_changed();
}

std::size_t FileListModel::class_file_count(const std::string &class_id) const {
    if (class_id.empty() || class_id == std::string(pp::ClassRegistry::kAllId))
        return rows_.size();
    std::size_t n = 0;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (class_id_of(i) == class_id)
            ++n;
    }
    return n;
}

// ============================================================================================
// M4-T11 · 自动分组（键 = core/classify 的 group_key() 纯函数；EXIF 摘要异步补扫）
// ============================================================================================

namespace {

// 分组摘要补扫（独立单线程池 = 串行，不与缩略图/转码抢核；只读 EXIF，不碰像素）
class GroupScanRunnable : public QRunnable {
public:
    GroupScanRunnable(FileListModel *model, QString path) : model_(model), path_(std::move(path)) {
        setAutoDelete(true);
    }
    void run() override {
        std::string datetime;
        std::string camera;
        try {
            const pp::SourceMeta meta =
                pp::read_metadata(std::filesystem::path(path_.toStdString()));
            datetime = pp::effective_datetime(meta.exif, meta.xmp);
            const auto it = meta.exif.findKey(Exiv2::ExifKey("Exif.Image.Model"));
            if (it != meta.exif.end()) {
                camera = it->toString();
                const std::size_t b = camera.find_first_not_of(" \t\r\n");
                const std::size_t e = camera.find_last_not_of(" \t\r\n");
                camera = (b == std::string::npos) ? std::string() : camera.substr(b, e - b + 1);
            }
        } catch (...) {
            // 摘要失败 = 无字段（归入「无分组」；分组绝不因读失败而抛错/阻断）
            datetime.clear();
            camera.clear();
        }
        FileListModel *const model = model_;
        const QString path = path_;
        // 回填在 GUI 线程（context = 模型自身）执行：Qt 队列投递，模型析构则事件自动作废
        QMetaObject::invokeMethod(
            model,
            [model, path, datetime, camera] {
                model->apply_group_scan(-1, path, datetime, camera);
            },
            Qt::QueuedConnection);
    }

private:
    FileListModel *model_ = nullptr;
    QString path_;
};

} // namespace

void FileListModel::scan_group_sources() {
    if (rows_.empty())
        return;
    if (scan_pool_ == nullptr) {
        scan_pool_ = new QThreadPool(this);
        scan_pool_->setMaxThreadCount(1); // 串行：EXIF 摘要轻量，避免与转码抢核
    }
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].group_scanned)
            continue;
        const QString path = fs_string(rows_[i].entry.src);
        if (!scan_inflight_.insert(path).second)
            continue; // 已在途（路径键去重）
        ++scan_pending_;
        scan_pool_->start(new GroupScanRunnable(this, path));
    }
}

void FileListModel::apply_group_scan(int row, const QString &path, const std::string &datetime,
                                     const std::string &camera) {
    if (scan_pending_ > 0)
        --scan_pending_;
    scan_inflight_.erase(norm_string(path));
    // stale-guard：按**路径**回填（行号会因删行漂移；路径唯一 → 行号无关）。
    // R-026：路径 → 行号哈希 O(1)（原全表扫 + 逐行 std::string→QString 构造，1 万行 ≈ 亿次）
    int target = -1;
    if (row >= 0 && std::size_t(row) < rows_.size() &&
        fs_string(rows_[std::size_t(row)].entry.src) == norm_string(path)) {
        target = row; // 快路径（无删行时的常态）
    } else {
        const auto it = row_by_key_.find(norm_string(path).toStdString());
        if (it != row_by_key_.end() && it->second < rows_.size())
            target = int(it->second);
    }
    if (target < 0)
        return; // 行已不在列表（删行/清空）→ 丢弃
    FileRow &r = rows_[std::size_t(target)];
    r.scan_datetime = datetime;
    r.scan_camera = camera;
    r.group_scanned = true;
    // M5-T5：摘要同时喂「日期 · 机型」信息行 → 一并声明变化角色（行重绘即见）
    emit_row_data_changed(std::size_t(target), {GroupKeyRole, ShotDateRole, CameraRole});
    emit group_source_changed();
}

bool FileListModel::group_scan_idle() const { return scan_pending_ == 0; }

pp::GroupMode FileListModel::group_mode() const { return group_mode_; }

void FileListModel::set_group_mode(pp::GroupMode mode) {
    if (group_mode_ == mode)
        return;
    group_mode_ = mode;
    if (!rows_.empty())
        emit dataChanged(index(0), index(int(rows_.size()) - 1), {GroupKeyRole});
    emit group_source_changed();
}

pp::GroupSource FileListModel::group_source(std::size_t i) const {
    pp::GroupSource src;
    if (i >= rows_.size())
        return src;
    const FileRow &r = rows_[i];
    src.datetime_original = r.scan_datetime;
    src.camera_model = r.scan_camera;
    src.source_format = r.probe_ok ? r.info.format : std::string();
    return src;
}

QString FileListModel::group_key_of(std::size_t i) const {
    return QString::fromStdString(pp::group_key(group_mode_, group_source(i)));
}

bool FileListModel::group_source_scanned(std::size_t i) const {
    return i < rows_.size() && rows_[i].group_scanned;
}

void FileListModel::emit_row_data_changed(std::size_t i, const QVector<int> &roles) {
    if (i >= rows_.size())
        return;
    const QModelIndex idx = index(int(i));
    emit dataChanged(idx, idx, roles);
}

// ------------------------------------------------------------------ FileDelegate

void FileDelegate::set_tokens(const theme::Tokens &t) { tokens_ = t; }

QFont FileDelegate::name_font() {
    return theme::font(theme::Typography::card_title_px, QFont::DemiBold); // §7.5 文件名 12/600
}

QFont FileDelegate::info_font() {
    return theme::font(theme::Typography::hint_px); // §4.2 辅助 10/400（裁定 A3）
}

QColor FileDelegate::selection_fill(const theme::Tokens &t) { return t.accent_dim; }
QColor FileDelegate::selection_bar(const theme::Tokens &t) { return t.accent; }

QString FileDelegate::join_items(const QStringList &items, const QFontMetrics &fm, int avail_w) {
    QString out;
    for (const QString &item : items) {
        if (item.isEmpty())
            continue;
        const int item_w = fm.horizontalAdvance(item);
        if (out.isEmpty()) {
            // 首项就放不下 → 本行留空：宁可少一行信息，也不用省略号吞掉尺寸/大小（§7.5 逐字）
            if (item_w > avail_w)
                break;
            out = item;
            continue;
        }
        const QString candidate = out + QStringLiteral(" · ") + item;
        if (fm.horizontalAdvance(candidate) > avail_w)
            break; // 放不下 → 减行内项（书写序保持，不截断、不省略号）
        out = candidate;
    }
    return out;
}

// R-276：行几何的**唯一输入口径** —— 视口尾缘收口（竖滚动条出现后 item 比可见视口宽 ~14px，
// 尾对齐胶囊/色点会被裁或错位）。两路绘制（FileDelegate::paint / FileGroupDelegate 色点）
// 一律先经本函数，再进 row_rects（收口规则只写这一份）。
// R-218（M7-R2E2）：收口边随方向镜像 —— LTR 收右缘（竖滚动条在右）/ RTL 收左缘（滚动条在左）；
// LTR 分支逐字节不变（旧式只收右缘）。
QRect FileDelegate::visible_row_rect(const QRect &item_rect, const QAbstractItemView *view) {
    QRect row_rect = item_rect;
    if (view != nullptr) {
        const bool rtl = view->layoutDirection() == Qt::RightToLeft;
        const int visible_right =
            view->horizontalScrollBar()->value() + view->viewport()->width() - 1;
        if (!rtl) {
            if (row_rect.right() > visible_right)
                row_rect.setRight(visible_right);
        } else {
            const int visible_left = view->horizontalScrollBar()->value();
            if (row_rect.left() < visible_left)
                row_rect.setLeft(visible_left);
        }
    }
    return row_rect;
}

FileDelegate::RowRects FileDelegate::row_rects(const QRect &item_rect, int pill_w, bool has_flag) {
    RowRects rects;
    const int thumb_px = theme::Metrics::thumb_px; // R4 缩略图 46（§7.5）
    rects.thumb = QRect(item_rect.left() + kRowInsetL,
                        item_rect.top() + (item_rect.height() - thumb_px) / 2, thumb_px, thumb_px);
    int right = item_rect.left() + item_rect.width() - kRowInsetR; // 右开边界
    const int pill_h = theme::Metrics::pill_h;                     // R4 胶囊高 16
    rects.pill =
        QRect(right - pill_w, item_rect.top() + (item_rect.height() - pill_h) / 2, pill_w, pill_h);
    right = rects.pill.left() - kPillGap;
    if (has_flag) {
        rects.flag = QRect(right - kExceptionMarkPx,
                           item_rect.top() + (item_rect.height() - kExceptionMarkPx) / 2,
                           kExceptionMarkPx, kExceptionMarkPx);
        right = rects.flag.left() - kPillGap;
    }
    const int text_x = rects.thumb.left() + thumb_px + kThumbTextGap;
    rects.text = QRect(text_x, item_rect.top() + kRowInsetY, std::max(0, right - text_x),
                       item_rect.height() - 2 * kRowInsetY);
    return rects;
}

// R-218 / M7-R2E2 加性重载：rtl=false 与三参版逐值相同；rtl=true 四矩形绕 item_rect 中线镜像
// （逻辑坐标算好再单点镜像；ui_smoke 的 rtl-mirror 探针钉死镜像关系）。
FileDelegate::RowRects FileDelegate::row_rects(const QRect &item_rect, int pill_w, bool has_flag,
                                               bool rtl) {
    RowRects rects = row_rects(item_rect, pill_w, has_flag);
    if (rtl) {
        rects.thumb = mirror_x(rects.thumb, item_rect);
        rects.pill = mirror_x(rects.pill, item_rect);
        rects.text = mirror_x(rects.text, item_rect);
        if (!rects.flag.isEmpty())
            rects.flag = mirror_x(rects.flag, item_rect);
    }
    return rects;
}

void FileDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    const QWidget *const widget = opt.widget;
    QStyle *const style = widget ? widget->style() : QApplication::style();
    const bool selected = (opt.state & QStyle::State_Selected) != 0;
    const theme::Tokens &t = tokens_;

    // 行背景：悬停/交替底交给样式；**选中底由本委托自绘**（修 3-17 的高亮强化）→ 先屏蔽样式
    // 的选中填充，否则样式蓝底会盖住 accent-dim 淡底。
    {
        QStyleOptionViewItem bg_opt = opt;
        bg_opt.state &= ~QStyle::State_Selected;
        bg_opt.text.clear();
        bg_opt.icon = QIcon();
        // GUI-R2：勾选框一律自绘（FileGroupDelegate 的勾选列），样式指示器在**任何列**都不得
        // 出现 —— 否则内容列左缘会多画一枚原生勾选框（"两排选择框"）。
        bg_opt.features &= ~QStyleOptionViewItem::HasCheckIndicator;
        style->drawControl(QStyle::CE_ItemViewItem, &bg_opt, painter, widget);
    }

    // 事实（U4 实测）：QListView/QTreeView 用"未含竖滚动条"的视口宽度调 sizeHint，竖滚动条出现
    // 后 item 比可见视口宽 ~14px → 右对齐胶囊会被裁掉。收口规则单源 = visible_row_rect（R-276）。
    const QRect row_rect =
        visible_row_rect(opt.rect, qobject_cast<const QAbstractItemView *>(widget));
    if (row_rect.width() <= 0 || row_rect.height() <= 0)
        return;

    // 选中面（修 3-17）：整行淡强调底 + 左缘 2px 强调条（行首由列 0 承担，见 FileGroupDelegate）
    if (selected)
        painter->fillRect(row_rect, selection_fill(t));

    const QString state_label = index.data(FileListModel::StateTextRole).toString();
    const bool has_exception = index.data(FileListModel::ExceptionRole).toBool();
    // R-218：几何走逻辑坐标 + 单点镜像（RTL 时缩略图/信息区/胶囊/例外标记整体镜像）
    const bool rtl = rtl_of(opt);
    const RowRects rects = row_rects(row_rect, state_pill_width(state_label), has_exception, rtl);

    // 左：46×46 缩略图（r4 圆角；无缩略图 → --ctl 占位块，mockup .thumb{background:var(--ctl)}）
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QPainterPath thumb_clip;
    thumb_clip.addRoundedRect(QRectF(rects.thumb), theme::Metrics::thumb_radius,
                              theme::Metrics::thumb_radius);
    painter->setClipPath(thumb_clip);
    painter->fillRect(rects.thumb, t.control);
    const QImage thumb = index.data(FileListModel::ThumbRole).value<QImage>();
    if (!thumb.isNull()) {
        const QImage scaled = thumb.scaled(theme::Metrics::thumb_px, theme::Metrics::thumb_px,
                                           Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QRect target(rects.thumb.left() + (rects.thumb.width() - scaled.width()) / 2,
                           rects.thumb.top() + (rects.thumb.height() - scaled.height()) / 2,
                           scaled.width(), scaled.height());
        painter->drawImage(target, scaled);
    }
    painter->restore();

    // 中：三行信息 —— 文件名（12/600）+「尺寸 · 大小 · 格式」+「日期 · 机型」（10px，--txt3）。
    // 信息行**禁止截断**：放不下减行内项（join_items），绝不产出省略号；失败行的原因在
    // 第三行（无 EXIF 信息时）以 err 色兜底，否则由右侧"失败"胶囊表达。
    const QString name = index.data(FileListModel::NameRole).toString();
    QStringList meta1;
    const QString dims = index.data(FileListModel::DimRole).toString();
    const QString size = index.data(FileListModel::SizeRole).toString();
    const QString format = index.data(FileListModel::FormatRole).toString();
    if (!dims.isEmpty())
        meta1 << dims;
    if (!size.isEmpty())
        meta1 << size;
    if (!format.isEmpty())
        meta1 << format;
    QStringList meta2;
    const QString date = index.data(FileListModel::ShotDateRole).toString();
    const QString camera = index.data(FileListModel::CameraRole).toString();
    if (!date.isEmpty())
        meta2 << date;
    if (!camera.isEmpty())
        meta2 << camera;
    const QString error = index.data(FileListModel::ErrorRole).toString();
    bool line3_is_error = false;
    if (meta2.isEmpty() && !error.isEmpty()) {
        meta2 << error; // 失败行的原因（行内唯一可见处；行内文案与 §7.5 的字段优先级无关）
        line3_is_error = true;
    }

    const QFont name_draw_font = name_font();
    const QFont info_draw_font = info_font();
    const QFontMetrics name_fm(name_draw_font);
    const QFontMetrics info_fm(info_draw_font);
    const QString line2 = join_items(meta1, info_fm, rects.text.width());
    const QString line3 = join_items(meta2, info_fm, rects.text.width());
    if (rects.text.width() > 0) {
        const int name_h = name_fm.height();
        const int info_h = info_fm.height();
        const int block = name_h + (line2.isEmpty() ? 0 : info_h) + (line3.isEmpty() ? 0 : info_h);
        int y = rects.text.top() + std::max(0, (rects.text.height() - block) / 2);
        painter->save();
        painter->setFont(name_draw_font);
        painter->setPen(t.text);
        painter->drawText(QRect(rects.text.left(), y, rects.text.width(), name_h),
                          text_align(rtl) | Qt::AlignVCenter,
                          name_fm.elidedText(name, Qt::ElideMiddle, rects.text.width()));
        y += name_h;
        painter->setFont(info_draw_font);
        painter->setPen(t.text3);
        if (!line2.isEmpty()) {
            painter->drawText(QRect(rects.text.left(), y, rects.text.width(), info_h),
                              text_align(rtl) | Qt::AlignVCenter, line2);
            y += info_h;
        }
        if (!line3.isEmpty()) {
            painter->setPen(line3_is_error ? t.err : t.text3);
            painter->drawText(QRect(rects.text.left(), y, rects.text.width(), info_h),
                              text_align(rtl) | Qt::AlignVCenter, line3);
        }
        painter->restore();
    }

    // 例外标记（矢量 tag 图标；§3.1 例外 = 警告档色 —— A6：原 #dd8800 收口为令牌 warn 档）
    if (has_exception && !rects.flag.isEmpty()) {
        painter->save();
        icons::paint(*painter, icons::Name::Tag, QRectF(rects.flag),
                     theme::pill_paint(t, kExceptionTone).fg);
        painter->restore();
    }

    // 右：状态胶囊（R4 16 高 r3 9.5/700；色 = theme::pill_paint 单源 —— §8 四档语义）
    // R-069：色调改由模型出（StateToneRole）—— 0 字节产物事实（行级）才能把"已完成"
    // 改判成琥珀；原实现按 FileState 枚举自算，行级事实进不来。
    const QByteArray tone = index.data(FileListModel::StateToneRole).toString().toLatin1();
    draw_state_pill(painter, rects.pill, t, tone.constData(), state_label);
}

QSize FileDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(index);
    return QSize(option.rect.width(), kFileRowHeight); // §7.5：行高 58
}

// ============================================================================================
// M4-T11 · 节名格式化（键 → 显示名；mockup .ghead 文案）
// ============================================================================================

QString group_label(pp::GroupMode mode, const QString &key) {
    if (key.isEmpty())
        return QString::fromUtf8(pp::kNoGroupLabel.data(), int(pp::kNoGroupLabel.size()));
    switch (mode) {
    case pp::GroupMode::Month: {
        // "YYYY-MM" → "2024 年 1 月"（mockup .ghead："2024 年 5 月"）；形态不符 → 原样直出
        if (key.size() == 7 && key.at(4) == QLatin1Char('-')) {
            bool ok_year = false;
            bool ok_month = false;
            const int year = key.left(4).toInt(&ok_year);
            const int month = key.mid(5, 2).toInt(&ok_month);
            if (ok_year && ok_month && month >= 1 && month <= 12)
                return QStringLiteral("%1 年 %2 月").arg(year).arg(month);
        }
        return key;
    }
    case pp::GroupMode::Camera:
        return key;
    case pp::GroupMode::SourceFormat:
        return key.toUpper(); // 与行内 FormatRole（"JPEG"）同口径
    case pp::GroupMode::None:
        return key;
    }
    return key;
}

// ============================================================================================
// M4-T11 · FileGroupProxyModel（分组节 + 两列的层次层）
// ============================================================================================

FileGroupProxyModel::FileGroupProxyModel(QObject *parent) : QAbstractProxyModel(parent) {
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(
        kRebuildDebounceMs); // 摘要/键逐行落定 → 去抖合并为一次重建（R-280 命名）
    connect(debounce_, &QTimer::timeout, this, [this] { rebuild(); });
}

void FileGroupProxyModel::setSourceModel(QAbstractItemModel *source) {
    if (sourceModel() != nullptr)
        disconnect(sourceModel(), nullptr, this, nullptr);
    QAbstractProxyModel::setSourceModel(source);
    if (source == nullptr) {
        rebuild();
        return;
    }
    connect(source, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { rebuild(); });
    connect(source, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &, int, int) { rebuild(); });
    connect(source, &QAbstractItemModel::modelReset, this, [this] { rebuild(); });
    connect(source, &QAbstractItemModel::layoutChanged, this, [this] { rebuild(); });
    connect(source, &QAbstractItemModel::dataChanged, this,
            &FileGroupProxyModel::on_source_data_changed);
    // 摘要批次就绪（源自根 FileListModel；本代理的 source 可能是过滤代理 → 沿 sourceModel 链
    // 走到根模型，避免对中间层的类型假设）
    QAbstractItemModel *root = source;
    while (auto *upstream = qobject_cast<QAbstractProxyModel *>(root))
        root = upstream->sourceModel();
    if (auto *flm = qobject_cast<FileListModel *>(root)) {
        connect(flm, &FileListModel::group_source_changed, this,
                &FileGroupProxyModel::schedule_rebuild);
    }
    rebuild();
}

void FileGroupProxyModel::set_group_mode(pp::GroupMode mode) {
    if (mode_ == mode)
        return;
    mode_ = mode;
    rebuild(); // 只关系节名文案；键由源模型给出
}

pp::GroupMode FileGroupProxyModel::group_mode() const { return mode_; }

void FileGroupProxyModel::schedule_rebuild() {
    if (debounce_ != nullptr)
        debounce_->start();
}

void FileGroupProxyModel::on_source_data_changed(const QModelIndex &top_left,
                                                 const QModelIndex &bottom_right,
                                                 const QVector<int> &roles) {
    // 文件行数据变化 → 原样转发给视图（勾选/缩略图/状态都靠它刷新）。
    // R-203（M7-R2E2）：代理是两列结构（勾选列 + 行内容列），转发不得只出列 0 —— 此前只发
    // mapFromSource 的单格，列 1 靠视图整行重绘续命（deep-review R-203/E6-08）。现按行发
    // [列 0, 列 1] 的 dataChanged 区间（行数/roles 语义逐字不变）。
    if (sourceModel() == nullptr)
        return;
    for (int r = top_left.row(); r <= bottom_right.row(); ++r) {
        const QModelIndex src = sourceModel()->index(r, 0);
        const QModelIndex proxy = mapFromSource(src);
        if (proxy.isValid()) {
            const QModelIndex last = index(proxy.row(), kColumnCount - 1, proxy.parent());
            emit dataChanged(proxy, last.isValid() ? last : proxy, roles);
        }
    }
    // 分组键变化 → 结构可能要变（去抖合并；不做逐行重建）
    for (int role : roles) {
        if (role == FileListModel::GroupKeyRole) {
            schedule_rebuild();
            break;
        }
    }
}

void FileGroupProxyModel::rebuild() {
    QAbstractItemModel *src = sourceModel();
    const int n = src != nullptr ? src->rowCount() : 0;
    beginResetModel();
    groups_.clear();
    row_group_.assign(std::size_t(n), -1);
    row_pos_.assign(std::size_t(n), -1);
    if (src != nullptr) {
        QHash<QString, int> by_key;
        for (int r = 0; r < n; ++r) {
            const QModelIndex si = src->index(r, 0);
            const QString key = si.data(FileListModel::GroupKeyRole).toString();
            auto it = by_key.find(key);
            int g = 0;
            if (it == by_key.end()) {
                groups_.push_back(Group{key, group_label(mode_, key), {}});
                g = int(groups_.size()) - 1;
                by_key.insert(key, g);
            } else {
                g = it.value();
            }
            groups_[std::size_t(g)].rows.push_back(r);
            row_group_[std::size_t(r)] = g;
            row_pos_[std::size_t(r)] = int(groups_[std::size_t(g)].rows.size()) - 1;
        }
    }
    ++rebuilds_;
    endResetModel();
}

QModelIndex FileGroupProxyModel::index(int row, int column, const QModelIndex &parent) const {
    if (row < 0 || column < 0 || column >= kColumnCount)
        return {};
    if (!parent.isValid()) {
        if (std::size_t(row) >= groups_.size())
            return {};
        return createIndex(row, column, quintptr(0)); // 分组节
    }
    if (parent.internalId() != 0)
        return {}; // 节的子节点不再有子节点
    const int g = parent.row();
    if (g < 0 || std::size_t(g) >= groups_.size())
        return {};
    if (std::size_t(row) >= groups_[std::size_t(g)].rows.size())
        return {};
    return createIndex(row, column, quintptr(g + 1)); // 文件行（父节点 = 节）
}

QModelIndex FileGroupProxyModel::parent(const QModelIndex &child) const {
    if (!child.isValid() || child.internalId() == 0)
        return {};
    const int g = int(child.internalId()) - 1;
    if (g < 0 || std::size_t(g) >= groups_.size())
        return {};
    return createIndex(g, 0, quintptr(0));
}

int FileGroupProxyModel::rowCount(const QModelIndex &parent) const {
    if (!parent.isValid())
        return int(groups_.size());
    if (parent.internalId() != 0)
        return 0;
    const int g = parent.row();
    if (g < 0 || std::size_t(g) >= groups_.size())
        return 0;
    return int(groups_[std::size_t(g)].rows.size());
}

int FileGroupProxyModel::columnCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);
    return kColumnCount;
}

bool FileGroupProxyModel::is_group(const QModelIndex &proxy_index) const {
    return proxy_index.isValid() && proxy_index.internalId() == 0;
}

QVariant FileGroupProxyModel::data(const QModelIndex &proxy_index, int role) const {
    if (!proxy_index.isValid())
        return {};
    if (is_group(proxy_index)) {
        const int g = proxy_index.row();
        if (g < 0 || std::size_t(g) >= groups_.size())
            return {};
        const Group &group = groups_[std::size_t(g)];
        switch (role) {
        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
            return header_text(group.label, int(group.rows.size()));
        case GroupCountRole:
            return int(group.rows.size());
        case FileListModel::GroupKeyRole:
            return group.key;
        default:
            return {};
        }
    }
    const QModelIndex src = mapToSource(proxy_index);
    return src.isValid() ? src.data(role) : QVariant();
}

Qt::ItemFlags FileGroupProxyModel::flags(const QModelIndex &proxy_index) const {
    if (!proxy_index.isValid())
        return Qt::NoItemFlags;
    if (is_group(proxy_index))
        return Qt::ItemIsEnabled; // 节头不可选/不可勾（控件集合与 mockup 一致）
    const QModelIndex src = mapToSource(proxy_index);
    return src.isValid() ? src.flags() : Qt::NoItemFlags;
}

bool FileGroupProxyModel::setData(const QModelIndex &proxy_index, const QVariant &value, int role) {
    if (!proxy_index.isValid() || is_group(proxy_index) || sourceModel() == nullptr)
        return false;
    const QModelIndex src = mapToSource(proxy_index);
    return src.isValid() ? sourceModel()->setData(src, value, role) : false;
}

QModelIndex FileGroupProxyModel::mapToSource(const QModelIndex &proxy_index) const {
    if (!proxy_index.isValid() || sourceModel() == nullptr)
        return {};
    if (is_group(proxy_index))
        return {}; // 节头无源索引
    const int g = int(proxy_index.internalId()) - 1;
    if (g < 0 || std::size_t(g) >= groups_.size())
        return {};
    const std::size_t pos = std::size_t(proxy_index.row());
    const std::vector<int> &rows = groups_[std::size_t(g)].rows;
    if (pos >= rows.size())
        return {};
    return sourceModel()->index(rows[pos], 0); // 源（过滤代理）单列
}

QModelIndex FileGroupProxyModel::mapFromSource(const QModelIndex &source_index) const {
    if (!source_index.isValid())
        return {};
    const int r = source_index.row();
    if (r < 0 || std::size_t(r) >= row_group_.size())
        return {};
    const int g = row_group_[std::size_t(r)];
    if (g < 0)
        return {};
    return createIndex(row_pos_[std::size_t(r)], 0, quintptr(g + 1));
}

int FileGroupProxyModel::group_count() const { return int(groups_.size()); }

QModelIndex FileGroupProxyModel::group_index(int group_row) const {
    return index(group_row, 0, QModelIndex());
}

QString FileGroupProxyModel::group_key_at(int group_row) const {
    if (group_row < 0 || std::size_t(group_row) >= groups_.size())
        return {};
    return groups_[std::size_t(group_row)].key;
}

QString FileGroupProxyModel::group_label_at(int group_row) const {
    if (group_row < 0 || std::size_t(group_row) >= groups_.size())
        return {};
    return groups_[std::size_t(group_row)].label;
}

int FileGroupProxyModel::group_size_at(int group_row) const {
    if (group_row < 0 || std::size_t(group_row) >= groups_.size())
        return 0;
    return int(groups_[std::size_t(group_row)].rows.size());
}

QString FileGroupProxyModel::header_text(const QString &label, int count) {
    return QStringLiteral("%1 · %2").arg(label).arg(count); // mockup .ghead：组名 + "· N"
}

QString FileGroupProxyModel::header_text_at(int group_row) const {
    return header_text(group_label_at(group_row), group_size_at(group_row));
}

int FileGroupProxyModel::rebuilds() const { return rebuilds_; }

void FileGroupProxyModel::set_collapsed(const QString &group_key, bool collapsed) {
    if (collapsed)
        collapsed_.insert(group_key);
    else
        collapsed_.erase(group_key);
}

bool FileGroupProxyModel::is_collapsed(const QString &group_key) const {
    return collapsed_.contains(group_key);
}

// M7-R2D1 / R-278：collapsed_keys() 已随死 API 清理移除（零消费；折叠记忆只有 is_collapsed
// 一个消费面）

// ============================================================================================
// M4-T11 · FileGroupDelegate（勾选列 + 行内容列 + 分组节头）
// ============================================================================================

FileGroupDelegate::FileGroupDelegate(QObject *parent) : FileDelegate(parent) {}

QRect FileGroupDelegate::check_rect(const QRect &item_rect) {
    const int box = theme::Metrics::checkbox_px; // R4 复选框 13×13（A6：自持 15 副本已废除）
    return QRect(item_rect.left() + kCheckInset, item_rect.top() + (item_rect.height() - box) / 2,
                 box, box);
}

// R-218 / M7-R2E2 加性重载：rtl=false 与单参版逐值相同；rtl=true 镜像到列内另一侧。
QRect FileGroupDelegate::check_rect(const QRect &item_rect, bool rtl) {
    const QRect box = check_rect(item_rect);
    return rtl ? mirror_x(box, item_rect) : box;
}

int FileGroupDelegate::check_column_width() {
    return kCheckInset + theme::Metrics::checkbox_px + kCheckGap;
}

void FileGroupDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const {
    const theme::Tokens &t = tokens();
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    if (index.data(FileGroupProxyModel::GroupCountRole).isValid()) {
        // ---- 分组节头（mockup .ghead：10px/700 + letter-spacing .4 + --txt3；"组名 · N"）----
        const QString text = index.data(Qt::DisplayRole).toString();
        const int count = index.data(FileGroupProxyModel::GroupCountRole).toInt();
        painter->save();
        painter->setClipRect(option.rect);
        if (selected) // 节头不可选（flags 只给 ItemIsEnabled），此处仅为不变量兜底
            painter->fillRect(option.rect, selection_fill(t));
        QFont font = theme::font(theme::Typography::hint_px, 700);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 0.4f);
        painter->setFont(font);
        const QFontMetrics fm(font);
        // R-218：文本锚点走逻辑坐标 + 单点镜像（RTL 时右起）；LTR 路径逐字节不变。
        const bool rtl = rtl_of(option);
        const int left = option.rect.left() + kCheckInset;
        const int baseline_y = option.rect.top() + 7; // .ghead{padding:7px 6px 4px}
        const auto draw_at = [&](int logical_x, const QString &s, const QColor &c) {
            const int w = fm.horizontalAdvance(s);
            const int x =
                rtl ? (option.rect.left() + option.rect.right() - logical_x - w + 1) : logical_x;
            painter->setPen(c);
            painter->drawText(QPoint(x, baseline_y + fm.ascent()), s);
        };
        QColor label_color = t.text3; // = --txt3
        draw_at(left, text, label_color);
        // 计数部分（.cnt{opacity:.8}）：覆盖重绘 " · N"（尾部），色更淡
        if (count >= 0) {
            const QString suffix = FileGroupProxyModel::header_text(QString(), count).mid(1);
            const int cut = std::max(0, int(text.size() - suffix.size()));
            const QString prefix = text.left(cut);
            const int prefix_w = fm.horizontalAdvance(prefix);
            label_color.setAlphaF(float(kGroupCountAlphaPct) / 100.0f);
            draw_at(left + prefix_w, suffix, label_color);
        }
        painter->restore();
        return;
    }
    if (index.column() == FileGroupProxyModel::kCheckColumn) {
        // ---- 勾选列：样式画悬停/交替底（屏蔽其选中填充，选中面由本委托落地）→ 自绘 .cbx ----
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.icon = QIcon();
        opt.features &= ~QStyleOptionViewItem::HasCheckIndicator; // 自绘，不用样式指示器
        opt.state &= ~QStyle::State_Selected;
        QStyle *style = opt.widget != nullptr ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        if (selected)
            paint_selection(painter, option.rect, t, /*first_column=*/true, rtl_of(option));

        // .cbx 13×13 r2（mockup：未勾选 = --ctl-bd 边框 + --card 底；勾选 = accent 填充 + 勾形）
        // R-218：勾选框贴行首（RTL = 镜像到列内另一侧）；命中判定同一条几何（check_rect 重载）
        const QRect box = check_rect(option.rect, rtl_of(option));
        const bool on = index.data(Qt::CheckStateRole).toInt() == int(Qt::Checked);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(on ? t.accent : t.control_bd, 1.0));
        painter->setBrush(on ? QBrush(t.accent) : QBrush(t.card));
        painter->drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5),
                                 theme::Metrics::checkbox_radius, theme::Metrics::checkbox_radius);
        if (on) {
            // M5-T3：勾形 = 矢量路径（icons::check）；色 = acc-on（强调底上的字色，§4.1）
            icons::paint(*painter, icons::Name::Check, QRectF(box), t.on_accent);
        }
        painter->restore();
        return;
    }
    // ---- 行内容列：§7.5 三行布局（FileDelegate::paint）+ 行尾分类色点（mockup .chip-dot）----
    FileDelegate::paint(painter, option, index);
    const QVariant deco = index.data(Qt::DecorationRole);
    if (!deco.isValid())
        return;
    const QColor dot_color = deco.value<QColor>();
    if (!dot_color.isValid())
        return;
    const QString state_label = index.data(FileListModel::StateTextRole).toString();
    const bool has_exception = index.data(FileListModel::ExceptionRole).toBool();
    // R-276：与 FileDelegate::paint 同一条输入口径（视口尾缘收口后的行矩形）—— 原先这里用
    // 未收口的 option.rect，竖滚动条出现后色点与胶囊错位。
    const QRect row_rect = FileDelegate::visible_row_rect(
        option.rect, qobject_cast<const QAbstractItemView *>(option.widget));
    if (row_rect.width() <= 0 || row_rect.height() <= 0)
        return;
    // R-218：色点贴胶囊之前 —— 逻辑坐标算好再单点镜像（空间不足判据同一条，镜像前后等价）
    const bool rtl = rtl_of(option);
    const FileDelegate::RowRects rects =
        row_rects(row_rect, state_pill_width(state_label), has_exception);
    QRect dot(rects.pill.left() - FileDelegate::kPillGap - kChipDotPx,
              row_rect.center().y() - kChipDotPx / 2, kChipDotPx, kChipDotPx);
    if (dot.left() < rects.thumb.right() + 1)
        return; // 空间不足（极窄列）→ 不画，避免压到文件名
    if (rtl)
        dot = mirror_x(dot, row_rect);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(dot_color);
    painter->drawEllipse(dot);
    painter->restore();
}

QSize FileGroupDelegate::sizeHint(const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const {
    if (index.data(FileGroupProxyModel::GroupCountRole).isValid())
        return QSize(option.rect.width(), kGroupHeaderHeight);
    if (index.column() == FileGroupProxyModel::kCheckColumn)
        return QSize(option.rect.width(), kFileRowHeight); // 与内容列同高（§7.5 行高 58）
    return FileDelegate::sizeHint(option, index);
}

bool FileGroupDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                    const QStyleOptionViewItem &option, const QModelIndex &index) {
    // 勾选一律由 FileTreeView 的显式命中处理（见其头注）：本委托**恒不响应** Qt 的自动勾选路径，
    // 避免「样式指示器几何 / 委托 editorEvent / 视图命中」三处各自判定同一枚 .cbox 而重复触发。
    Q_UNUSED(event);
    Q_UNUSED(model);
    Q_UNUSED(option);
    Q_UNUSED(index);
    return false;
}

// ============================================================================================
// M4-T11 · FileTreeView（视图侧两条显式行为）
// ============================================================================================

FileTreeView::FileTreeView(QWidget *parent) : QTreeView(parent) {}

void FileTreeView::mouseReleaseEvent(QMouseEvent *event) {
    if (event != nullptr && event->button() == Qt::LeftButton) {
        const QModelIndex index = indexAt(event->position().toPoint());
        if (index.isValid()) {
            if (index.data(FileGroupProxyModel::GroupCountRole).isValid()) {
                setExpanded(index, !isExpanded(index)); // 节头：单击即展开/折叠
            } else if (index.column() == FileGroupProxyModel::kCheckColumn &&
                       FileGroupDelegate::check_rect(visualRect(index),
                                                     layoutDirection() == Qt::RightToLeft)
                           .contains(event->position().toPoint())) {
                const bool on = index.data(Qt::CheckStateRole).toInt() == int(Qt::Checked);
                if (QAbstractItemModel *m = model()) {
                    m->setData(index, on ? int(Qt::Unchecked) : int(Qt::Checked),
                               Qt::CheckStateRole);
                }
                // 不 return：基类继续处理 current/selection（勾选与选中互不干扰）
            }
        }
    }
    QTreeView::mouseReleaseEvent(event);
}

} // namespace pp::ui
