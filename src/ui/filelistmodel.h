// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — file list model + delegate (M1b frozen)
//
// PP-THAWED(0.3.0-M4-D20) —— 解冻裁定表 §3.6 行 `ui/filelistmodel.h`（依据 docs/v0.3.0-design.md §3.6）
//   裁定原文（本行为逐字抄录；行首 "// " 为注释包装）：
// clang-format off
// | `ui/filelistmodel.h` | 追加 roles：`CheckState`（Qt 标准）、`ClassId`、`GroupKey`；模型 QListView→QTreeView（分组节） |
// clang-format on
//   落地任务 = W2-T11（文件列表/分类：勾选 + 正交搜索 + 自动分组 QTreeView；出口含 UI 冒烟扩展段绿、
//   `classes.json` 往返）→ 落地后改标 PP-FROZEN(0.3.0)。
//   本文件其余既有 roles/声明不在本表行内 → 维持 PP-FROZEN(M1b) 只读（仅上面 3 个 role 与视图形态属解冻面）。
#pragma once
#include <QAbstractListModel>
#include <QImage>
#include <QStyledItemDelegate>
#include <QStringList>
#include <optional>
#include <set>
#include <vector>
#include "core/pipeline.h"
#include "ui/thumbnails.h"   // M1b-U4: +1 line vs §2.8 frozen text (Thumbnailer must be declared for AUTOMOC; see report)

namespace pp::ui {

struct FileRow {
    pp::FileEntry entry;              // src, base_dir (= src.parent), exception
    pp::ImageInfo info;               // after probe
    bool probe_ok = false;
    QString error;                    // probe error (Chinese prefix + English detail)
    pp::FileState state = pp::FileState::Queued;
    QImage thumb;                     // may be null
    bool has_exception() const { return entry.exception.has_value(); }
};

class FileListModel : public QAbstractListModel {
    Q_OBJECT
public:
// PP-THAWED(0.3.0-M4-D20) §3.6 · FileListModel::Roles 追加 + 模型/视图形态
//   —— 0.3.0 冻结形态（设计 §3.6 逐字抄录）：
// clang-format off
// 追加 roles：`CheckState`（Qt 标准）、`ClassId`、`GroupKey`；模型 QListView→QTreeView（分组节）
// clang-format on
//   建议形态（§3.6 未逐字给出 role 序号 → 追加在既有 roles 末尾，Qt 标准枚举直接用 `Qt::CheckStateRole`）：
// clang-format off
//   // CheckState → Qt::CheckStateRole（Qt 标准：checked/unchecked/partiallyChecked；勾选=参与运行）
//   // ClassId    → QString/int（pp::ClassRegistry 的 ClassDef.id；"全部"为保留虚拟分类）
//   // GroupKey   → QString（自动分组视图的节键：月份(YYYY-MM)|相机型号|源格式 三选一 + 「无分组」）
// clang-format on
//   联动语义（§6.2/§9.1 逐字摘录，供 T11 实现）：复选框 checked = 参与运行 = 批量规则作用范围；
//   分类行右键菜单「全选该类/反选该类/清空勾选」；搜索过滤与勾选正交（勾选状态不因过滤丢失）；
//   QTreeView 节头 = 组名 + 计数（左栏「勾选+分组」+ 分类面板，§9.1）。
//   注（落地待定，T11 处置）：模型现形基类为 `QAbstractListModel`（见本文件 `class FileListModel`
//   声明行；行号随注释变动，勿以行号定位），
//   "QListView→QTreeView（分组节）" 需层次结构 → 基类/节行的落地形态由 T11 定（M4-T1 已上报，W5 收口入 m4-report）。
    enum Roles {
        ThumbRole = Qt::UserRole + 1,  // QImage
        NameRole,                      // QString (file name)
        PathRole,                      // QString (full path)
        DimRole,                       // QString "1920×1080" ("" before probe)
        FormatRole,                    // QString "JPEG" ("" before probe)
        StateRole,                     // int (pp::FileState)
        StateTextRole,                 // QString (§3 mapping)
        ExceptionRole,                 // bool
        ErrorRole                      // QString
    };

    explicit FileListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& idx, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& idx) const override;

    // files and/or directories; directories walked recursively (UI-side walk:
    // supported = ext in pp::input_extensions() case-insensitive; dotfiles skipped;
    // unsupported regular files counted, not added; duplicates by lexically_normal
    // path skipped). Triggers thumbnail enqueue for added rows.
    void add_paths(const QStringList& paths);
    void remove_rows(const QList<int>& rows);   // any order; internally handled
    void clear();

    std::size_t size() const;
    bool empty() const;
    const FileRow& row(std::size_t i) const;
    std::size_t unsupported_count() const;
    std::size_t exception_count() const;
    std::vector<pp::FileEntry> entries() const;         // deep copy for Scheduler

    void set_state(std::size_t i, pp::FileState st);    // dataChanged
    void set_exception(std::size_t i, std::optional<pp::MetadataOverride> ex);
    void apply_thumb(int row, const QString& path, const pp::ImageInfo& info,
                     bool probe_ok, const QString& error, const QImage& thumb);
    // stale-guard: ignores when row out of range or path mismatch

    void set_thumbnailer(Thumbnailer* t);   // not owned; nullptr = no async thumbs
    void enqueue_pending_thumbs();          // (re)enqueue all rows lacking probe info

signals:
    void content_changed();                 // count/unsupported/anything summary-worthy
    void exception_changed(std::size_t i);

private:
    std::vector<FileRow> rows_;
    std::set<QString> known_paths_;         // lexically_normal strings
    std::set<QString> unsupported_paths_;   // M1b v1.1: dedup unsupported counts (2026-09-20 ruling)
    std::size_t unsupported_ = 0;
    Thumbnailer* thumbs_ = nullptr;
};

// Row painting: 56px high; 48x48 thumb left; name (bold) + "1920×1080 · JPEG" second line;
// right-aligned state badge (colored text §3) + "⚑" exception marker before badge.
class FileDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;   // {option.rect.width(), 56}
};

}  // namespace pp::ui
