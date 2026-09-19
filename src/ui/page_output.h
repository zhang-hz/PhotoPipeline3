// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — output page (M1b frozen)
#pragma once
#include <QWidget>
#include <memory>
#include "core/pipeline.h"
#include "core/presets.h"

namespace pp::ui {

class ParamForm;

class PageOutput : public QWidget {
    Q_OBJECT
public:
    explicit PageOutput(QWidget* parent = nullptr);
    ~PageOutput();

    // RunConfig skeleton: out_root/format/backend/tech/lossless/params/bitdepth/
    // color_target/conflict/metadata_only filled; rules/workers/budget/rotate/flatten
    // left at defaults (MainWindow fills them).
    pp::RunConfig config_base() const;

    bool metadata_only() const;
    void set_metadata_only(bool on);        // disables format-specific groups (see spec)
    QString out_root() const;
    void set_out_root(const QString& dir);

    void set_batch_has_alpha(bool has);     // avif 10bit+alpha -> libaom preselect

    void apply_preset(const pp::PresetData& p);   // format/selection/values/bitdepth/color/conflict
    pp::PresetData collect_preset(const QString& name) const;   // rules merged by MainWindow

    // session restore (no signals); invalid values ignored
    void restore_last(const QString& format_id, const QString& out_root);

    // "" = ready; else human-readable reason (Start button gating)
    QString ready_to_start() const;

    // smoke/test hooks
    QString current_format() const;
    void select_format(const QString& format_id);   // programmatic, no signal

signals:
    void format_changed(const QString& format_id);
    void config_changed();                        // anything summary-worthy
    void manage_presets_requested();
    void open_settings_requested();
};

}  // namespace pp::ui
