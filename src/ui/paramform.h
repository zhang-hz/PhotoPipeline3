// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — schema-driven parameter form (M1b frozen)
#pragma once
#include <QWidget>
#include <string>
#include <vector>
#include "core/params.h"

namespace pp::ui {

struct FormSelection {
    std::string backend;     // current backend id ("" -> first)
    std::string tech;        // current tech id ("" -> first)
    bool lossless = false;   // lossless switch (only meaningful when tech lossless_capable)
};

class ParamForm : public QWidget {
    Q_OBJECT
public:
    // fmt: static format def (static_formats() entries are process-stable).
    // backends: effective backend list. For runtime-introspected formats (heif/avif)
    // pass pp::introspect_backends(fmt.id); for others pass fmt.backends.
    ParamForm(const pp::FormatDef& fmt, std::vector<pp::BackendDef> backends,
              QWidget* parent = nullptr);
    ~ParamForm();   // M1b-U3: +1 line vs §2.6 frozen text (moc needs a complete Impl; see report)

    void set_selection(const FormSelection& sel);   // programmatic (no signal)
    FormSelection selection() const;
    pp::ParamSet values() const;                    // current values, "__" keys excluded
    void set_values(const pp::ParamSet& s);         // apply by key, unknown keys ignored

    // test/inspection hook: is the row for `key` currently visible?
    bool is_param_visible(const std::string& key) const;

signals:
    void selection_changed(const pp::ui::FormSelection& sel);  // user action only
    void changed();                                            // any value/selection change

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pp::ui
