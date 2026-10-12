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

// R-204（M7-R2.1-F4）：分组摘要已随 R-200 拆分移入 group_scan.cpp（读取走 pp::camera_model
// 单源访问器），本 TU 不再触碰 Exiv2/metadata 面 —— 原 <exiv2/exiv2.hpp> 与 core/metadata.h
// 两个残留 include 一并退场（R-204「UI 直接操作 Exiv2 类型」收口）。
#include "core/fsops.h"
#include "ui/theme.h" // 令牌单源（排印/语义色/行内度量；M5-T5/A6 起本 TU 全面消费）

#include "ui/filelistmodel_util.h" // R-200：共享纯件 + Runnable 工厂（私有头）

namespace pp::ui {

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
    scan_pool_->start(make_add_paths_task(this, paths)); // R-200：工厂跨 TU
}

namespace {

// R-319（M7-R2.1-F4）：行 base_dir 推导单点 —— 基准目录**包含**该文件 → 基准目录，否则
// src.parent（同 dev_cli choose_base 的单基准语义；空基准 = 现行为逐字不变）。
std::filesystem::path derive_base_dir(const std::filesystem::path &src, const QString &mirror_base) {
    if (mirror_base.isEmpty())
        return src.parent_path();
    const std::filesystem::path base = fs_path(mirror_base);
    return pp::is_inside(src, base) ? base : src.parent_path();
}

} // namespace

// R-319：镜像基准目录（opt-in；设置即重算既有行，收集面沿用同一推导）
void FileListModel::set_mirror_base(const QString &base) {
    if (mirror_base_ == base)
        return;
    mirror_base_ = base;
    for (FileRow &row : rows_)
        row.entry.base_dir = derive_base_dir(row.entry.src, mirror_base_);
    if (!rows_.empty())
        emit dataChanged(index(0, 0), index(int(rows_.size() - 1), 0));
}

QString FileListModel::mirror_base() const { return mirror_base_; }

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
            // §2.8：base_dir = src.parent；R-319 镜像基准（含则基准、不含回退 parent）
            row.entry.base_dir = derive_base_dir(row.entry.src, mirror_base_);
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
        scan_pool_->start(make_group_scan_task(this, path)); // R-200：工厂跨 TU
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

} // namespace pp::ui