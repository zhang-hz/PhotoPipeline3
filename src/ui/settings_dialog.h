// PP-FROZEN(file) — src/ui/settings_dialog.h
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — settings dialog (M1b frozen)
#pragma once
#include <QDialog>
#include "core/settings.h"

namespace pp::ui {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const pp::AppSettings& current, QWidget* parent = nullptr);
    pp::AppSettings settings() const;    // edited values (OK or Apply)
};

}  // namespace pp::ui
