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
    // 0.4.2 / 需求 3（A13）：导入…/导出… 两条文件管理动作（与格式预设行对齐；枚举末尾加性）。
    enum class Action { None, Load, SaveAs, Delete, Import, Export };
    // presets: (path, display-name) list from pp::ui::list_presets(presets_dir)
    explicit PresetsDialog(const std::vector<std::pair<QString, QString>> &presets,
                           const QString &suggested_name, QWidget *parent = nullptr);
    Action action() const;
    QString name() const; // load target / save-as name / delete target
    QString path() const; // Load/Delete/Export = preset file path; SaveAs/Import = empty
};

} // namespace pp::ui
