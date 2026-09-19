// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — file list model + delegate (M1b frozen)
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
