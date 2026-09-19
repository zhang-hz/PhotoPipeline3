// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — metadata page (M1b frozen)
#pragma once
#include <QWidget>
#include <QStringList>
#include "core/metadata.h"

namespace pp::ui {

class PageMeta : public QWidget {
    Q_OBJECT
public:
    explicit PageMeta(QWidget* parent = nullptr);

    pp::BatchRules rules() const;                 // built from current UI state
    void apply_rules(const pp::BatchRules& r);    // preset load (no signal)

    void set_batch_files(const QStringList& paths);        // first-file time preview refresh
    void set_selected_files(const QStringList& paths);     // "read GPS from selection" enable
    void set_exception_summary(const QStringList& paths);  // exception card list
    void set_map_provider(const QString& provider_id, const QString& amap_key, int cache_mb);

signals:
    void rules_changed();
    void open_editor_requested(const QString& path);
    void clear_exception_requested(const QString& path);
};

}  // namespace pp::ui
