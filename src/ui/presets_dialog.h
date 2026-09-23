// PP-FROZEN(file) — src/ui/presets_dialog.h
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — preset manager dialog (M1b frozen)
#pragma once
#include <QDialog>
#include <QString>
#include <utility>
#include <vector>

namespace pp::ui {

class PresetsDialog : public QDialog {
    Q_OBJECT
public:
    enum class Action { None, Load, SaveAs, Delete };
    // presets: (path, display-name) list from pp::ui::list_presets(presets_dir)
    explicit PresetsDialog(const std::vector<std::pair<QString, QString>> &presets,
                           const QString &suggested_name, QWidget *parent = nullptr);
    Action action() const;
    QString name() const; // load target / save-as name / delete target
    QString path() const; // corresponding file path (already sanitized for save-as)
};

} // namespace pp::ui
