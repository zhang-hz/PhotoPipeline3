// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — file list model + delegate (M1b frozen)
//
// PP-FROZEN(0.3.0) —— 解冻裁定表 §3.6 行 `ui/filelistmodel.h` **已落地**
//   落地任务 = W2-T11（文件列表勾选 + 正交搜索 + 自动分组 QTreeView + 分类面板）；原标注
//   PP-THAWED(0.3.0-M4-D20) 随落地再冻结为本标记（冻结头 SPDX 延续）。
//   裁定原文（本行为逐字抄录；行首 "// " 为注释包装）：
// clang-format off
// | `ui/filelistmodel.h` | 追加 roles：`CheckState`（Qt 标准）、`ClassId`、`GroupKey`；模型 QListView→QTreeView（分组节） |
// clang-format on
//   0.3.0 落地形态（逐条对应裁定面；T1 上报的"基类/节行落地形态由 T11 定"即此处置）：
//     * CheckState → **Qt 标准** `Qt::CheckStateRole`（不新增自定义 role；勾选 = 参与运行 =
//       批量规则作用范围，§6.2/D2）；
//     * ClassId    → `ClassIdRole`（QString；空串 = 无分类）。分类色（mockup 行尾 .chip-dot）
//       另经 **Qt 标准** `Qt::DecorationRole` 输出 QColor —— 仍不新增自定义 role；
//     * GroupKey   → `GroupKeyRole`（QString；空串 = 「无分组」，与 `pp::kNoGroupLabel` 同义）；
//     * 视图形态：`QListView` → `QTreeView`。层次结构由**新增**的 `FileGroupProxyModel` 提供
//       （顶层 = 分组节，叶子 = 文件行）；`FileListModel` 仍是**平坦源模型** —— M1b 冻结的
//       add_paths/remove_rows/entries/row(i)/行号口径与委托 `FileDelegate` 的绘制**逐字不变**
//       （树形只加在代理层，冻结面零重写；tokens 化的节头/勾选框由新增委托承担）。
//
//   联动语义（§6.2/§9.1 逐字摘录，本文件为其落地）：
// clang-format off
// - `ClassRegistry`：增删改名/颜色/热键（0-9）；"全部"为保留虚拟分类。默认模板：精选/待定/废片。
// - 归属：一文件一分类（可无）；打标即写 registry；持久化 `data_dir()/classes.json`（键=规范化绝对路径；文件不在列表时惰性清理）。
// - 自动分组视图：`GroupKey` role 提供 `月份(YYYY-MM) | 相机型号 | 源格式` 三选一 + 「无分组」；QTreeView 节头=组名+计数。
// - 圈选语义（D2）：复选框 `checked` = 参与运行 = 批量规则作用范围；分类行右键菜单「全选该类/反选该类/清空勾选」；搜索过滤与勾选正交（勾选状态不因过滤丢失）。
// clang-format on
//   落地口径（§6.2 未逐字给出处；逐条对拍见 tests/ui_smoke.py 的 T11 扩展段）：
//     * **勾选状态住在源模型**（FileRow::checked）→ 搜索过滤（QSortFilterProxyModel）只是
//       视图层的可见性，勾选/计数不因过滤丢失（正交性的实现依据）；默认 checked = true
//       （未动勾选时参与集 = 全部文件，与 v0.2 行为一致）。
//     * **分类单源 = `ClassRegistry`**：本模型只持有只读指针（`set_class_registry`），
//       ClassIdRole/DecorationRole 实时查表，`set_class()` 直接写注册表（不复制第二份状态）；
//       持久化（classes.json）与惰性清理（prune_missing）由消费端 mainwindow 负责。
//     * **分组键的数据源**：源格式 = probe 摘要（`ImageInfo::format`，零额外 IO）；月份/相机型号
//       需 EXIF 摘要，由本模型在**独立单线程池**里异步补齐（`scan_group_sources`；每行一次，
//       stale-guard 按路径比对），就绪后发 `group_source_changed()` → 分组视图重建。
//     * **节头计数 = 当前可见（过滤后）文件数**：树里只挂可见行，节头数字与树内容自洽；
//       分组键本身取自源模型（过滤不改变键）。
#pragma once
#include "core/classify.h"
#include "core/pipeline.h"
#include "ui/thumbnails.h" // M1b-U4: +1 line vs §2.8 frozen text (Thumbnailer must be declared for AUTOMOC; see report)
#include <QAbstractListModel>
#include <QAbstractProxyModel>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <optional>
#include <set>
#include <string>
#include <vector>

class QThreadPool;

namespace pp::ui {

struct FileRow {
    pp::FileEntry entry; // src, base_dir (= src.parent), exception
    pp::ImageInfo info;  // after probe
    bool probe_ok = false;
    QString error; // probe error (Chinese prefix + English detail)
    pp::FileState state = pp::FileState::Queued;
    QImage thumb; // may be null
    // ---- M4-T11（§3.6 解冻面）**加性字段**（既有字段与语义一字不动）----
    bool checked = true;        // 勾选 = 参与运行集合（§6.2/D2；默认参与 = v0.2 行为）
    std::string scan_datetime;  // EXIF 生效时间 "YYYY:MM:DD HH:MM:SS"（异步摘要；可空）
    std::string scan_camera;    // Exif.Image.Model（异步摘要；可空）
    bool group_scanned = false; // 摘要扫描已回填（无字段/失败同样置位，避免重复扫）
    bool has_exception() const { return entry.exception.has_value(); }
};

class FileListModel : public QAbstractListModel {
    Q_OBJECT
public:
    // PP-FROZEN(0.3.0) §3.6 · FileListModel::Roles（0.3.0 冻结形态）
    //   裁定面 = 「追加 roles：CheckState（Qt 标准）、ClassId、GroupKey」→ CheckState 直接用
    //   Qt 标准枚举 `Qt::CheckStateRole`；下列两枚追加在既有 roles **末尾**（既有 9 枚不重排）。
    enum Roles {
        ThumbRole = Qt::UserRole + 1, // QImage
        NameRole,                     // QString (file name)
        PathRole,                     // QString (full path)
        DimRole,                      // QString "1920×1080" ("" before probe)
        FormatRole,                   // QString "JPEG" ("" before probe)
        StateRole,                    // int (pp::FileState)
        StateTextRole,                // QString (§3 mapping)
        ExceptionRole,                // bool
        ErrorRole,                    // QString
        // ---- 0.3.0 追加 ----
        ClassIdRole, // QString：所属分类 id（"" = 无分类；"all" 是虚拟「全部」，不作归属）
        GroupKeyRole // QString：分组节键（"" = 「无分组」；形态 = YYYY-MM / 相机型号 / 源格式）
    };

    explicit FileListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &idx, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &idx) const override;
    bool setData(const QModelIndex &idx, const QVariant &value, int role) override;

    // files and/or directories; directories walked recursively (UI-side walk:
    // supported = ext in pp::input_extensions() case-insensitive; dotfiles skipped;
    // unsupported regular files counted, not added; duplicates by lexically_normal
    // path skipped). Triggers thumbnail enqueue for added rows.
    void add_paths(const QStringList &paths);
    void remove_rows(const QList<int> &rows); // any order; internally handled
    void clear();

    std::size_t size() const;
    bool empty() const;
    const FileRow &row(std::size_t i) const;
    std::size_t unsupported_count() const;
    std::size_t exception_count() const;
    std::vector<pp::FileEntry> entries() const; // deep copy for Scheduler

    void set_state(std::size_t i, pp::FileState st); // dataChanged
    void set_exception(std::size_t i, std::optional<pp::MetadataOverride> ex);
    void apply_thumb(int row, const QString &path, const pp::ImageInfo &info, bool probe_ok,
                     const QString &error, const QImage &thumb);
    // stale-guard: ignores when row out of range or path mismatch

    void set_thumbnailer(Thumbnailer *t); // not owned; nullptr = no async thumbs
    void enqueue_pending_thumbs();        // (re)enqueue all rows lacking probe info

    // ==========================================================================================
    // M4-T11 · 勾选（§6.2/D2：复选框 checked = 参与运行 = 批量规则作用范围）
    // ==========================================================================================
    bool is_checked(std::size_t i) const;
    void set_checked(std::size_t i, bool on);
    void set_all_checked(bool on);
    std::size_t checked_count() const;
    std::vector<std::size_t> checked_rows() const;
    // 参与运行集合（深拷贝；顺序 = 列表行序）。未勾选 = 不参与，与 v0.2 的"全部参与"差一个勾选面。
    std::vector<pp::FileEntry> checked_entries() const;

    // 整类圈选（§6.2 分类行右键菜单：「全选该类 / 反选该类 / 清空勾选」）
    //   id = "all" 或空串 → 命中全部文件（「全部」是保留虚拟分类，见 core/classify.h）
    void check_class(const std::string &class_id);  // 全选该类：该类文件全部勾选（其余不动）
    void invert_class(const std::string &class_id); // 反选该类：该类已勾选者取消、未勾选者勾上
    void clear_checks();                            // 清空勾选：全部取消

    // ==========================================================================================
    // M4-T11 · 分类（消费 W1-T8 `core/classify`；单源 = 注册表，本模型不复制状态）
    // ==========================================================================================
    void set_class_registry(pp::ClassRegistry *registry); // not owned; nullptr = 无分类
    pp::ClassRegistry *class_registry() const;
    std::string class_id_of(std::size_t i) const;               // "" = 无分类（含未知/陈旧 id）
    void set_class(std::size_t i, const std::string &class_id); // 写 registry + dataChanged
    // 注册表被外部整体改动（如批量恢复快照）→ 全量刷新归属/色点（不改注册表本身）
    void notify_class_changed();
    // 该类当前文件数（"all" 或空串 = 全部行数）—— 分类面板的 .cnt 读数
    std::size_t class_file_count(const std::string &class_id) const;

    // ==========================================================================================
    // M4-T11 · 自动分组（§6.2；键由 core/classify 的纯函数 group_key() 出，零 IO）
    // ==========================================================================================
    pp::GroupMode group_mode() const;
    void set_group_mode(pp::GroupMode mode);
    // 分组摘要（probe 结果 + 异步 EXIF 摘要；本函数只读）
    pp::GroupSource group_source(std::size_t i) const;
    QString group_key_of(std::size_t i) const;      // "" = 「无分组」
    bool group_source_scanned(std::size_t i) const; // 摘要已回填（冒烟等待用）
    // 触发异步 EXIF 摘要扫描（新增行 / 探测完成后调用；每行只扫一次，stale-guard 按路径）
    void scan_group_sources();
    bool group_scan_idle() const; // 无在途摘要请求（冒烟等待用）
    // 摘要回填入口（异步扫描任务的回调；行号仅作快路径，回填按路径 stale-guard）
    void apply_group_scan(int row, const QString &path, const std::string &datetime,
                          const std::string &camera);

signals:
    void content_changed(); // count/unsupported/anything summary-worthy
    void exception_changed(std::size_t i);
    void checks_changed();       // 勾选集合变化（参与运行集合）
    void class_changed();        // 归属/注册表变化（打标、CRUD）
    void group_source_changed(); // 分组摘要批次就绪 → 分组视图重建

private:
    void emit_row_data_changed(std::size_t i, const QVector<int> &roles);

    std::vector<FileRow> rows_;
    std::set<QString> known_paths_;       // lexically_normal strings
    std::set<QString> unsupported_paths_; // M1b v1.1: dedup unsupported counts (2026-09-20 ruling)
    std::size_t unsupported_ = 0;
    Thumbnailer *thumbs_ = nullptr;
    // ---- M4-T11 追加 ----
    pp::ClassRegistry *classes_ = nullptr;            // 不持有（单源在 MainWindow）
    pp::GroupMode group_mode_ = pp::GroupMode::Month; // 默认「按月份」（mockup .combo）
    QThreadPool *scan_pool_ = nullptr;                // 惰性创建：1 线程串行摘要扫描
    int scan_pending_ = 0;                            // 在途摘要请求数（诊断/冒烟等待）
    std::set<QString> scan_inflight_;                 // 在途摘要（键 = 路径；去重 + 结果回填守卫）
};

// Row painting: 56px high; 48x48 thumb left; name (bold) + "1920×1080 · JPEG" second line;
// right-aligned state badge (colored text §3) + "⚑" exception marker before badge.
class FileDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override; // {option.rect.width(), 56}
};

// ============================================================================================
// PP-FROZEN(0.3.0) §3.6 · 自动分组视图（"QListView→QTreeView（分组节）" 的层次结构层）
// ============================================================================================
// 两列口径（mockup .frow 的 cbox 15px + gap 8px 的等价实现，避免改动冻结委托的绘制几何）：
//   * 列 0 = 勾选列（固定 29px = 6 内距 + 15 框 + 8 间隙）：只画 .cbox；
//   * 列 1 = 行内容列（弹性）：文件行交给冻结的 FileDelegate::paint（一字不改）；
//   * 分组节 = 顶层节点，`setFirstColumnSpanned` 跨两列，节头 = "组名 · 计数"（§6.2）。
class FileGroupProxyModel : public QAbstractProxyModel {
    Q_OBJECT
public:
    // 节内可见文件数（节头 "· N" 的数值来源；int）。属**新增代理**的 role，非 FileListModel 面。
    enum { GroupCountRole = Qt::UserRole + 300 };

    static constexpr int kCheckColumn = 0; // 勾选列
    static constexpr int kRowColumn = 1;   // 行内容列
    static constexpr int kColumnCount = 2;

    explicit FileGroupProxyModel(QObject *parent = nullptr);

    void setSourceModel(QAbstractItemModel *source) override;

    QModelIndex mapToSource(const QModelIndex &proxy_index) const override;
    QModelIndex mapFromSource(const QModelIndex &source_index) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &proxy_index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &proxy_index) const override;
    bool setData(const QModelIndex &proxy_index, const QVariant &value, int role) override;

    // 结构只读视图（冒烟/诊断）
    bool is_group(const QModelIndex &proxy_index) const;
    int group_count() const;
    QModelIndex group_index(int group_row) const;
    QString group_key_at(int group_row) const;   // 节键（"" = 「无分组」）
    QString group_label_at(int group_row) const; // 节名（"2024 年 1 月" / "JPEG" / "无分组"）
    int group_size_at(int group_row) const;      // 节内可见文件数
    QString header_text_at(int group_row) const; // 节头文案 = 组名 + " · " + 计数（单源）
    static QString header_text(const QString &label, int count);
    int rebuilds() const; // 重建次数（冒烟读数：分组视图随过滤/键变化的可观测证据）

    // 折叠记忆（键 = 组键；重建后由 view 侧复位）：过滤/摘要变化不丢用户的折叠操作
    void set_collapsed(const QString &group_key, bool collapsed);
    bool is_collapsed(const QString &group_key) const;
    QStringList collapsed_keys() const;

    // 节名格式化用的分组模式（键由源模型出；本代理只负责节名文案）。二者由 mainwindow 同点设置。
    void set_group_mode(pp::GroupMode mode);
    pp::GroupMode group_mode() const;

public slots:
    void rebuild(); // 立即重建（结构变化；摘要/键变化走 30ms 去抖）

private:
    void schedule_rebuild();
    void on_source_data_changed(const QModelIndex &top_left, const QModelIndex &bottom_right,
                                const QVector<int> &roles);

    struct Group {
        QString key; // 原始键（"" = 无分组）
        QString label;
        std::vector<int> rows; // 源（过滤后模型）行号
    };
    std::vector<Group> groups_;
    std::vector<int> row_group_; // 源行 → 组下标（-1 = 不可见/未分组）
    std::vector<int> row_pos_;   // 源行 → 组内位置
    std::set<QString> collapsed_;
    pp::GroupMode mode_ = pp::GroupMode::Month;
    class QTimer *debounce_ = nullptr;
    int rebuilds_ = 0;
};

// 节名格式化：键 → 显示名（月份 "YYYY-MM" → "2024 年 1 月"；其余去空白直出；空键 = 「无分组」）
QString group_label(pp::GroupMode mode, const QString &key);

// ============================================================================================
// PP-FROZEN(0.3.0) §3.6 · 左栏树视图（勾选命中 + 节头展开；视图侧行为的两条显式实现）
// ============================================================================================
// 视图侧只做两件事，且都走**显式命中**（不依赖 Qt 的委托自动勾选路径 —— 样式/基类对勾选指示器
// 的几何假设与本面板的自绘 .cbox 不一致，见 FileGroupDelegate::editorEvent 的说明）：
//   * 列 0 勾选框命中（命中框 = FileGroupDelegate::check_rect）→ setData(CheckStateRole)；
//   * 分组节头点击 → 展开/折叠（rootIsDecorated=false 无展开箭头，单击节头即切换）。
class FileTreeView : public QTreeView {
    Q_OBJECT
public:
    explicit FileTreeView(QWidget *parent = nullptr);

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
};

// ============================================================================================
// PP-FROZEN(0.3.0) §3.6 · 左栏委托（勾选列 + 行内容列 + 分组节头）
// ============================================================================================
// 分派（唯一入口，view 只设这一枚委托）：
//   * 分组节（GroupCountRole 有效）→ 节头（mockup .ghead：10.5px/700 + letter-spacing + txt3）；
//   * 文件行 + 列 0 → .cbox（15px r=3；未勾选 = 1.5px 边框，勾选 = accent 填充 + ✓）；
//   * 文件行 + 列 1 → FileDelegate::paint（冻结绘制）+ 行尾分类色点（mockup .chip-dot）。
// 勾选交互走 editorEvent 的**自绘命中框**（不依赖样式 SE_ItemViewItemCheckIndicator 的几何，
// 文件行内容列一律不响应勾选 —— 点缩略图只选中，不误触勾选）。
class FileGroupDelegate : public FileDelegate {
    Q_OBJECT
public:
    explicit FileGroupDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;

    // 勾选框几何（mockup .cbox：15px r=3；左内距 6px、纵向居中）——冒烟点击命中用同一函数
    static QRect check_rect(const QRect &item_rect);
    static constexpr int kGroupHeaderHeight = 26; // .ghead{padding:7px 6px 4px} + 10.5px 行高
    static constexpr int kCheckBox = 15;          // .cbox 15px（= theme::Metrics::checkbox_px）
    static constexpr int kCheckInset = 6;         // .frow{padding:4px 6px}
};

} // namespace pp::ui
