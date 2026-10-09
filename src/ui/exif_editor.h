// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — single-file metadata editor (M1b frozen)
#pragma once
#include "core/metadata.h"
#include <QDialog>
#include <optional>

namespace pp::ui {

// M5-T10（A8 零字形）：树行标记的 item data role —— 行文本里不再有 〔批〕/● 字形，
// 「批量规则命中」与「已改值」两个标记只以 item data 承载，由行 delegate 画成中性 pill「批」
// 与强调色字（+700）；--ui-smoke 按同一对 role 读回（单源，避免探针再抄一份 magic number）。
// key/node 一并导出：探针需要区分「标签叶子」与「分组节点」（分组节点不是可编辑行）。
namespace exif_roles {
inline constexpr int key = Qt::UserRole;         // QString：Exiv2 全键（叶子行）
inline constexpr int node = Qt::UserRole + 1;    // int：0 = 标签叶子，1 = 分组节点
inline constexpr int batch = Qt::UserRole + 2;   // bool：批量规则会命中该标签
inline constexpr int changed = Qt::UserRole + 3; // bool：该标签已改值（例外里的 set/remove）
} // namespace exif_roles

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
