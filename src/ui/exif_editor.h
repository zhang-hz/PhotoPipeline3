// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — single-file metadata editor (M1b frozen)
#pragma once
#include "core/metadata.h"
#include <QDialog>
#include <optional>

namespace pp::ui {

class ExifEditor : public QDialog {
    Q_OBJECT
public:
    // src: source file (read-only!). batch: current batch rules (for markers).
    // existing: current override to edit (nullopt = fresh).
    ExifEditor(const QString &src, const pp::BatchRules &batch,
               const std::optional<pp::MetadataOverride> &existing, QWidget *parent = nullptr);
    ~ExifEditor();

    // valid after accept()
    pp::MetadataOverride result() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pp::ui
