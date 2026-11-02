// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — single-file metadata editor (M1b frozen)
//
// M14（0.4.2）/ C2 语义说明（**接口零改动**，仅注释更新；冻结面只加性 + 追认）：
//   * 单文件元数据编辑 = 该文件 pp::MetadataOverride（唯一可写状态）；UI 已无全局批量规则。
//   * 树行标记：exif_roles::changed 仍有效（已改值 → 强调色字 + 700）；
//     exif_roles::batch（批量规则命中）随批量规则概念退役 —— 常量保留（冻结头只增不删），
//     exif_editor.cpp 不再设置它（--ui-smoke 由 meta 区段探针统一换锚）。
//   * 构造参数 batch 为历史兼容面：宿主恒传空 BatchRules{}；对话框不表达批量规则。
//     OK 返回的 override 与旧 override 合并（六个卡级 enable 字段与历史字段不丢失）。
#pragma once
#include "core/metadata.h"
#include <QDialog>
#include <optional>

namespace pp::ui {

// M5-T10（A8 零字形）：树行标记的 item data role —— 行文本里不再有 〔批〕/● 字形，
// 「已改值」标记只以 item data 承载，由行 delegate 画成强调色字（+700）；--ui-smoke 按同一
// role 读回（单源，避免探针再抄一份 magic number）。key/node 一并导出：探针需要区分
// 「标签叶子」与「分组节点」（分组节点不是可编辑行）。
namespace exif_roles {
inline constexpr int key = Qt::UserRole;      // QString：Exiv2 全键（叶子行）
inline constexpr int node = Qt::UserRole + 1; // int：0 = 标签叶子，1 = 分组节点
// M14/0.4.2：批量规则概念已退役 → 本 role 保留仅为冻结面兼容，产品代码不再写入（恒 false）。
inline constexpr int batch = Qt::UserRole + 2;   // bool：批量规则会命中该标签（已退役，恒 false）
inline constexpr int changed = Qt::UserRole + 3; // bool：该标签已改值（例外里的 set/remove）
} // namespace exif_roles

class ExifEditor : public QDialog {
    Q_OBJECT
public:
    // src: source file (read-only!). batch: 历史参数（宿主恒传空 BatchRules{}；对话框不再消费
    // 批量规则，仅为冻结签名兼容保留）。existing: current override to edit (nullopt = fresh)；
    // OK 结果 = 本页编辑覆盖到 existing 上（不丢失对话框不表达的字段）。
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
