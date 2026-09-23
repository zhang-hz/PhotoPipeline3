// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — file list model + delegate (M1b-U4; M4-T11 左栏勾选/分组/分类)
//
// 规格落地（docs/m1b-tasks.md §2.8 + §3.1）：
//   - add_paths：文件与/或目录；目录递归自走；支持扩展名 = pp::input_extensions()
//     大小写不敏感；点文件/点目录跳过；不支持的普通文件只计数不入列（§2.8 v1.1：
//     按 lexically_normal 路径去重，重复 add 不重复累计；clear() 同时归零集合与计数）；
//     lexically_normal 去重；新增行入缩略图队列（endInsertRows 之后行号才有效）
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
#include "ui/filelistmodel.h"
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
#include "ui/theme.h"      // 排印单源（节头 10.5px/700；include 顺序须在 <QApplication> 之后）

namespace pp::ui {
namespace {

// ---- §3.1 FileState 12 态映射（文本 + 颜色）唯一表；顺序 = pp::FileState 枚举顺序 ----
struct StateStyle {
    const char *text;
    const char *color;
};
constexpr StateStyle kStateStyle[] = {
    {"排队", "#808080"}, // Queued
    {"探测", "#0088cc"}, // Probing
    {"解码", "#0088cc"}, // Decoding
    {"旋转", "#0088cc"}, // Orienting
    {"色彩", "#0088cc"}, // Coloring
    {"合成", "#0088cc"}, // Flattening
    {"编码", "#0088cc"}, // Encoding
    {"写入", "#0088cc"}, // Writing
    {"完成", "#22aa22"}, // Done
    {"跳过", "#999999"}, // Skipped
    {"失败", "#dd3333"}, // Failed
    {"取消", "#999999"}, // Cancelled
};
static_assert(std::size(kStateStyle) == 12, "§3.1 十二态映射必须齐全");

const StateStyle &state_style(pp::FileState st) {
    const std::size_t i = static_cast<std::size_t>(st);
    return kStateStyle[i < std::size(kStateStyle) ? i : 0];
}

QString state_text(pp::FileState st) { return QString::fromUtf8(state_style(st).text); }
QColor state_color(pp::FileState st) { return QColor(QLatin1String(state_style(st).color)); }

constexpr int kRowHeight = 56;                     // §2.8 行高
constexpr int kThumbBox = 48;                      // §2.8 缩略图 48×48
constexpr const char *kExceptionColor = "#dd8800"; // §3.1 例外徽标 ⚑
constexpr const char *kSubLineColor = "#808080";   // 第二行灰字

std::filesystem::path fs_path(const QString &s) {
    return std::filesystem::path(s.toStdString()).lexically_normal();
}
QString fs_string(const std::filesystem::path &p) { return QString::fromStdString(p.string()); }

// 目录自走/去重/stale-guard 全部走同一规范化形式（lexically_normal 字符串）
QString norm_string(const QString &s) { return fs_string(fs_path(s)); }

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

// 单个输入项 → 候选文件（目录递归；点文件/点目录跳过；不支持者按去重路径计数，v1.1）
void collect_from(const std::filesystem::path &input, std::vector<std::filesystem::path> &files,
                  std::set<QString> &unsupported_seen, std::size_t &unsupported) {
    // 不支持文件：仅首次遇到（自上次 clear 以来）才计数（§2.8 v1.1 裁定）
    const auto count_unsupported = [&](const std::filesystem::path &p) {
        if (unsupported_seen.insert(fs_string(p.lexically_normal())).second)
            ++unsupported;
    };
    std::error_code ec;
    if (std::filesystem::is_directory(input, ec)) {
        std::filesystem::recursive_directory_iterator it(
            input, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec)
            return; // 目录打不开（权限/竞态）→ 忽略该输入项
        const std::filesystem::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec)
                break; // 目录项枚举失败 → 停止该子树
            const std::filesystem::path &p = it->path();
            const std::string name = p.filename().string();
            if (!name.empty() && name.front() == '.') { // 点文件/点目录
                std::error_code dir_ec;
                if (it->is_directory(dir_ec))
                    it.disable_recursion_pending();
                continue;
            }
            std::error_code type_ec;
            if (!it->is_regular_file(type_ec))
                continue;
            if (is_supported_ext(p)) {
                files.push_back(p.lexically_normal());
            } else {
                count_unsupported(p); // 不支持：计数但不入列
            }
        }
        return;
    }
    if (std::filesystem::is_regular_file(input, ec)) {
        if (is_supported_ext(input)) {
            files.push_back(input.lexically_normal());
        } else {
            count_unsupported(input);
        }
    }
    // 不存在/非普通文件 → 忽略（冻结接口无错误通道；unsupported_count 只统计普通文件）
}

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
        return state_text(r.state);
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
    default:
        return {};
    }
}

Qt::ItemFlags FileListModel::flags(const QModelIndex &idx) const {
    if (!idx.isValid())
        return Qt::NoItemFlags;
    // M4-T11：ItemIsUserCheckable = 勾选入口（§6.2/D2）；内容列由委托自绘命中框，行本身不可编辑
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
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

    std::vector<std::filesystem::path> files;
    std::size_t unsupported = 0;
    for (const QString &p : paths) {
        if (p.isEmpty())
            continue;
        collect_from(fs_path(p), files, unsupported_paths_, unsupported);
    }

    // M2-T16b：批次内按路径字典序排序后再追加 —— UI 行序不再依赖 readdir 顺序（可预测，并使
    // 01-meta.png 等截图 / T9 回归基线确定化）。跨批次仍是追加语义：多次 add 的先后顺序保留。
    std::sort(files.begin(), files.end(),
              [](const std::filesystem::path &a, const std::filesystem::path &b) {
                  return a.string() < b.string();
              });

    // 去重：已入列（known_paths_）与本批次内重复都跳过
    std::vector<std::filesystem::path> fresh;
    std::set<QString> batch_keys;
    for (const std::filesystem::path &f : files) {
        const QString key = fs_string(f);
        if (known_paths_.contains(key) || !batch_keys.insert(key).second)
            continue;
        fresh.push_back(f);
    }

    const std::size_t first_new = rows_.size();
    if (!fresh.empty()) {
        beginInsertRows(QModelIndex(), int(first_new), int(first_new + fresh.size() - 1));
        rows_.reserve(rows_.size() + fresh.size());
        for (const std::filesystem::path &f : fresh) {
            FileRow row;
            row.entry.src = f;
            row.entry.base_dir = f.parent_path(); // §2.8：base_dir = src.parent
            known_paths_.insert(fs_string(f));
            rows_.push_back(std::move(row));
        }
        endInsertRows();
    }
    unsupported_ += unsupported;

    // 新增行 → 缩略图入队（此时行号已生效）
    if (thumbs_) {
        for (std::size_t i = first_new; i < rows_.size(); ++i) {
            thumbs_->request(int(i), fs_string(rows_[i].entry.src));
        }
    }
    if (!fresh.empty())
        scan_group_sources(); // M4-T11：新增行的分组摘要（月份/相机型号）异步补扫
    if (!fresh.empty() || unsupported != 0)
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

    // 从大到小删除，避免行号漂移（任意输入顺序都正确）
    for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
        const int r = *it;
        beginRemoveRows(QModelIndex(), r, r);
        known_paths_.erase(fs_string(rows_[std::size_t(r)].entry.src));
        rows_.erase(rows_.begin() + r);
        endRemoveRows();
    }
    // 在途/排队结果由 apply_thumb stale-guard（行号漂移 → path 不匹配）丢弃；
    // 行号下移后重新入队仍缺探测信息的行（否则被删行之后的行会永久失去缩略图）
    enqueue_pending_thumbs();
    emit content_changed();
}

void FileListModel::clear() {
    if (thumbs_)
        thumbs_->clear_pending(); // 在途结果 → stale（apply_thumb 亦按越界丢弃）
    const bool changed = !rows_.empty() || unsupported_ != 0;
    beginResetModel();
    rows_.clear();
    known_paths_.clear();
    unsupported_paths_.clear(); // v1.1：去重集合与计数一起归零
    unsupported_ = 0;
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

std::size_t FileListModel::exception_count() const {
    return std::size_t(std::count_if(rows_.begin(), rows_.end(),
                                     [](const FileRow &r) { return r.has_exception(); }));
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
    emit dataChanged(idx, idx, {StateRole, StateTextRole});
    emit content_changed(); // 各态计数属"摘要相关"
}

void FileListModel::set_exception(std::size_t i, std::optional<pp::MetadataOverride> ex) {
    if (i >= rows_.size())
        return;
    FileRow &r = rows_[i];
    const bool had = r.has_exception();
    r.entry.exception = std::move(ex);
    if (had == r.has_exception() && !had)
        return; // 无变化（都为空）
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
    if (probe_ok) {
        r.error.clear();
    } else {
        r.error = error.isEmpty() ? tr("探测失败") : tr("探测失败：%1").arg(error);
        r.state = pp::FileState::Failed; // 探测失败 = 该行错误态（thumbs.h：error 非空）
    }
    const QModelIndex idx = index(row);
    // M4-T11：+GroupKeyRole（源格式模式的分组键随 probe 摘要落定）
    emit dataChanged(
        idx, idx,
        {ThumbRole, DimRole, FormatRole, StateRole, StateTextRole, ErrorRole, GroupKeyRole});
    if (!probe_ok && !was_failed)
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
    if (!rows_.empty())
        emit dataChanged(index(0), index(int(rows_.size()) - 1), {Qt::CheckStateRole});
    emit checks_changed();
    emit content_changed();
}

std::size_t FileListModel::checked_count() const {
    return std::size_t(
        std::count_if(rows_.begin(), rows_.end(), [](const FileRow &r) { return r.checked; }));
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
    // stale-guard：按**路径**回填（行号会因删行漂移；路径唯一 → 行号无关）
    int target = -1;
    if (row >= 0 && std::size_t(row) < rows_.size() &&
        fs_string(rows_[std::size_t(row)].entry.src) == norm_string(path)) {
        target = row; // 快路径（无删行时的常态）
    } else {
        const QString key = norm_string(path);
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (fs_string(rows_[i].entry.src) == key) {
                target = int(i);
                break;
            }
        }
    }
    if (target < 0)
        return; // 行已不在列表（删行/清空）→ 丢弃
    FileRow &r = rows_[std::size_t(target)];
    r.scan_datetime = datetime;
    r.scan_camera = camera;
    r.group_scanned = true;
    emit_row_data_changed(std::size_t(target), {GroupKeyRole});
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

void FileDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // 背景/选中/焦点框交给样式；文字与图标自绘（清空避免默认绘制）
    const QWidget *const widget = opt.widget;
    QStyle *const style = widget ? widget->style() : QApplication::style();
    opt.text.clear();
    opt.icon = QIcon();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

    QRect area = opt.rect.adjusted(6, 4, -6, -4);
    // 事实（U4 实测）：QListView 用"未含竖滚动条"的视口宽度调 sizeHint（§2.8 冻结公式），
    // 竖滚动条出现后 item 比可见视口宽 ~14px → 右对齐徽标会被裁掉。以可见视口右边界收口。
    if (const QAbstractItemView *view = qobject_cast<const QAbstractItemView *>(widget)) {
        const int visible_right =
            view->horizontalScrollBar()->value() + view->viewport()->width() - 1;
        if (area.right() > visible_right)
            area.setRight(visible_right);
    }
    if (area.width() <= 0 || area.height() <= 0)
        return;

    const bool selected = opt.state & QStyle::State_Selected;
    const QColor primary = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    const QColor secondary = selected ? opt.palette.color(QPalette::HighlightedText)
                                      : QColor(QLatin1String(kSubLineColor));

    // 左：48×48 缩略图（等比缩放居中；无缩略图 → 空白占位框）
    const QRect thumb_box(area.left(), area.top() + (area.height() - kThumbBox) / 2, kThumbBox,
                          kThumbBox);
    const QImage thumb = index.data(FileListModel::ThumbRole).value<QImage>();
    painter->save();
    if (!thumb.isNull()) {
        const QImage scaled =
            thumb.scaled(kThumbBox, kThumbBox, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QRect target(thumb_box.left() + (kThumbBox - scaled.width()) / 2,
                           thumb_box.top() + (kThumbBox - scaled.height()) / 2, scaled.width(),
                           scaled.height());
        painter->drawImage(target, scaled);
    } else {
        painter->setPen(QPen(opt.palette.color(QPalette::Mid)));
        painter->drawRect(thumb_box.adjusted(0, 0, -1, -1));
    }
    painter->restore();

    // 右：状态徽标（§3.1 颜色，右对齐），例外时其左侧 "⚑"（#dd8800）
    const QString state_label = index.data(FileListModel::StateTextRole).toString();
    const int state_int = index.data(FileListModel::StateRole).toInt();
    const bool has_exception = index.data(FileListModel::ExceptionRole).toBool();
    QFont badge_font = opt.font;
    badge_font.setBold(true);
    const QFontMetrics badge_fm(badge_font);
    const QString flag = QString::fromUtf8("⚑");
    const int badge_w = badge_fm.horizontalAdvance(state_label);
    const int flag_adv = has_exception ? badge_fm.horizontalAdvance(flag) : 0;
    const int badge_left = area.right() - badge_w;
    const int flag_left = badge_left - (has_exception ? flag_adv + 6 : 0);

    painter->save();
    painter->setFont(badge_font);
    painter->setPen(state_color(pp::FileState(state_int)));
    painter->drawText(QRect(badge_left, area.top(), badge_w, area.height()),
                      Qt::AlignRight | Qt::AlignVCenter, state_label);
    if (has_exception) {
        painter->setPen(QColor(QLatin1String(kExceptionColor)));
        painter->drawText(QRect(flag_left, area.top(), flag_adv, area.height()),
                          Qt::AlignRight | Qt::AlignVCenter, flag);
    }
    painter->restore();

    // 中：第一行文件名（粗体），第二行 "1920×1080 · JPEG"（灰）；
    // 探测失败且无尺寸信息时第二行显示 ErrorRole 文本（失败色），避免只看到徽标无原因
    const QString name = index.data(FileListModel::NameRole).toString();
    QString sub = index.data(FileListModel::DimRole).toString();
    const QString format = index.data(FileListModel::FormatRole).toString();
    if (!format.isEmpty())
        sub = sub.isEmpty() ? format : sub + QStringLiteral(" · ") + format;
    bool sub_is_error = false;
    if (sub.isEmpty()) {
        const QString error = index.data(FileListModel::ErrorRole).toString();
        if (!error.isEmpty()) {
            sub = error;
            sub_is_error = true;
        }
    }

    const int text_left = thumb_box.right() + 10;
    const int text_width = flag_left - 8 - text_left;
    if (text_width > 0) {
        QFont name_font = opt.font;
        name_font.setBold(true);
        const QFontMetrics name_fm(name_font);
        const QFontMetrics sub_fm(opt.font);
        const int line_h = area.height() / 2;
        painter->save();
        painter->setFont(name_font);
        painter->setPen(primary);
        painter->drawText(QRect(text_left, area.top(), text_width, line_h),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          name_fm.elidedText(name, Qt::ElideMiddle, text_width));
        if (!sub.isEmpty()) {
            painter->setFont(opt.font);
            painter->setPen(sub_is_error ? state_color(pp::FileState::Failed) : secondary);
            painter->drawText(
                QRect(text_left, area.top() + line_h, text_width, area.height() - line_h),
                Qt::AlignLeft | Qt::AlignVCenter,
                sub_fm.elidedText(sub, Qt::ElideRight, text_width));
        }
        painter->restore();
    }
}

QSize FileDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(index);
    return QSize(option.rect.width(), kRowHeight); // §2.8：行高 56
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
    debounce_->setInterval(30); // 摘要/键逐行落定 → 30ms 去抖合并为一次重建
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
    // 文件行数据变化 → 原样转发给视图（勾选/缩略图/状态都靠它刷新）
    if (sourceModel() == nullptr)
        return;
    for (int r = top_left.row(); r <= bottom_right.row(); ++r) {
        const QModelIndex src = sourceModel()->index(r, 0);
        const QModelIndex proxy = mapFromSource(src);
        if (proxy.isValid())
            emit dataChanged(proxy, proxy, roles);
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

QStringList FileGroupProxyModel::collapsed_keys() const {
    QStringList out;
    for (const QString &key : collapsed_)
        out << key;
    return out;
}

// ============================================================================================
// M4-T11 · FileGroupDelegate（勾选列 + 行内容列 + 分组节头）
// ============================================================================================

FileGroupDelegate::FileGroupDelegate(QObject *parent) : FileDelegate(parent) {}

QRect FileGroupDelegate::check_rect(const QRect &item_rect) {
    return QRect(item_rect.left() + kCheckInset,
                 item_rect.top() + (item_rect.height() - kCheckBox) / 2, kCheckBox, kCheckBox);
}

void FileGroupDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const {
    if (index.data(FileGroupProxyModel::GroupCountRole).isValid()) {
        // ---- 分组节头（mockup .ghead：10.5px/700 + letter-spacing .4 + --txt3；"组名 · N"）----
        const QString text = index.data(Qt::DisplayRole).toString();
        const int count = index.data(FileGroupProxyModel::GroupCountRole).toInt();
        painter->save();
        painter->setClipRect(option.rect);
        QFont font = theme::font(theme::Typography::hint_px, 700);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 0.4f);
        painter->setFont(font);
        const QFontMetrics fm(font);
        const int left = option.rect.left() + kCheckInset;
        const int baseline_y = option.rect.top() + 7; // .ghead{padding:7px 6px 4px}
        QColor label_color = option.palette.color(QPalette::PlaceholderText); // = --txt3
        painter->setPen(label_color);
        painter->drawText(QPoint(left, baseline_y + fm.ascent()), text);
        // 计数部分（.cnt{opacity:.8}）：覆盖重绘 " · N"（尾部），色更淡
        if (count >= 0) {
            const QString suffix = FileGroupProxyModel::header_text(QString(), count).mid(1);
            const int cut = std::max(0, int(text.size() - suffix.size()));
            const QString prefix = text.left(cut);
            const int prefix_w = fm.horizontalAdvance(prefix);
            label_color.setAlphaF(0.8f);
            painter->setPen(label_color);
            painter->drawText(QPoint(left + prefix_w, baseline_y + fm.ascent()), suffix);
        }
        painter->restore();
        return;
    }
    if (index.column() == FileGroupProxyModel::kCheckColumn) {
        // ---- 勾选列：样式画行背景/选中/焦点（屏蔽样式自带指示器）→ 自绘 .cbox ----
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.icon = QIcon();
        opt.features &= ~QStyleOptionViewItem::HasCheckIndicator; // 自绘，不用样式指示器
        QStyle *style = opt.widget != nullptr ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        const QRect box = check_rect(opt.rect);
        const bool on = index.data(Qt::CheckStateRole).toInt() == int(Qt::Checked);
        const QColor border = opt.palette.color(QPalette::PlaceholderText);
        const QColor fill = opt.palette.color(QPalette::Highlight);
        const QColor mark = opt.palette.color(QPalette::HighlightedText);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPen pen(on ? fill : border);
        pen.setWidthF(on ? 1.0 : 1.5); // .cbox{border:1.5px}
        painter->setPen(pen);
        painter->setBrush(on ? QBrush(fill) : Qt::NoBrush);
        const qreal radius = 3.0; // .cbox{r=3}
        painter->drawRoundedRect(QRectF(box).adjusted(0.0, 0.0, -1.0, -1.0), radius, radius);
        if (on) {
            QFont mark_font = theme::font(theme::Typography::badge_px, 800);
            painter->setFont(mark_font);
            painter->setPen(mark);
            painter->drawText(box, Qt::AlignCenter, QStringLiteral("✓"));
        }
        painter->restore();
        return;
    }
    // ---- 行内容列：冻结绘制（一字不改）+ 行尾分类色点（mockup .chip-dot）----
    FileDelegate::paint(painter, option, index);
    const QVariant deco = index.data(Qt::DecorationRole);
    if (!deco.isValid())
        return;
    const QColor dot_color = deco.value<QColor>();
    if (!dot_color.isValid())
        return;
    QRect area = option.rect.adjusted(6, 4, -6, -4);
    if (area.width() <= 0 || area.height() <= 0)
        return;
    QFont badge_font = option.font;
    badge_font.setBold(true);
    const QFontMetrics fm(badge_font);
    const int badge_w = fm.horizontalAdvance(index.data(FileListModel::StateTextRole).toString());
    const bool has_exception = index.data(FileListModel::ExceptionRole).toBool();
    const int flag_w = has_exception ? fm.horizontalAdvance(QString::fromUtf8("⚑")) + 6 : 0;
    const int dot_size = 8; // .chip-dot{width:8px;height:8px}
    const int dot_right = area.right() - badge_w - flag_w - 8;
    const QRect dot(dot_right - dot_size + 1, area.center().y() - dot_size / 2, dot_size, dot_size);
    if (dot.left() < area.left())
        return; // 空间不足（极窄列）→ 不画，避免压到文件名
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
        return QSize(option.rect.width(), kRowHeight); // 与内容列同高（冻结行高 56）
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
                       FileGroupDelegate::check_rect(visualRect(index))
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
