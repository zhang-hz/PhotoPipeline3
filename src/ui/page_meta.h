// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — metadata page (M1b frozen; M4-T12 追加面)
//
// PP-FROZEN(0.3.0) —— 元数据页「生效值显示」（设计 §5.2 / §9.3；mockup meta-dark|meta-light）
//   落地任务 = W3-T12。本文件 0.3.0 的**追加面**（既有方法签名/语义一字不动）：
//     * `MetaSelectionItem`：跟随选中项的纯数据快照（路径 + 单文件例外 + probe 摘要）；
//     * `set_selection(items, from_selection)`：§5.2「一切显示跟随文件列表选中项」的喂入口
//       （无选中 → 调用方填首个文件且 from_selection=false，页面出「(1/N)」提示）；
//     * `set_tokens(tokens)`：主题 tokens（QSS/配色单源 ui/theme.h；与 PreviewPanel/
//       ClassifyPanel 同一注入口径）。
//   既有方法（rules/apply_rules/set_batch_files/set_selected_files/set_exception_summary/
//   set_map_provider 与三枚信号）签名不变；语义按 §5.2 收敛（详见 page_meta.cpp 头注释）。
#pragma once
#include "core/metadata.h"
#include <QList>
#include <QStringList>
#include <QWidget>
#include <optional>

namespace pp::ui {

// 前置声明（不拉 ui/theme.h：theme.h 依赖包含者先引入 <QApplication>，而本头会被 AUTOMOC 的
// mocs_compilation.cpp 直接包含 —— 与 ui/classify_panel.h / preview_panel.h 同口径）
namespace theme {
struct Tokens;
}

// M4-T12（§5.2）：跟随项（选中项 / 无选中时回退的首个文件）的只读快照。
//   * `exception` = 该文件的单文件例外（三态覆盖；nullopt = 无例外）→ 喂 preview_effective；
//   * `info/probe_ok/file_size` = 列表模型在添加文件时收集的 probe 摘要（thumbs 通道，
//     §5.2「异步性：probe 摘要已在文件添加时收集」）→ 关键信息卡的尺寸/位深/通道/格式/大小。
struct MetaSelectionItem {
    QString path;
    std::optional<pp::MetadataOverride> exception;
    pp::ImageInfo info;
    bool probe_ok = false;
    qint64 file_size = 0; // 字节
};

class PageMeta : public QWidget {
    Q_OBJECT
public:
    explicit PageMeta(QWidget *parent = nullptr);

    pp::BatchRules rules() const;              // built from current UI state
    void apply_rules(const pp::BatchRules &r); // preset load (no signal)

    void set_batch_files(const QStringList &paths);       // 批内文件（「影响 N / N」与 (i/N) 提示）
    void set_selected_files(const QStringList &paths);    // 仅路径的选中面（无 probe 摘要/例外）
    void set_exception_summary(const QStringList &paths); // exception card list
    void set_map_provider(const QString &provider_id, const QString &amap_key, int cache_mb);

    // ---- PP-FROZEN(0.3.0) 追加面（M4-T12）----
    void set_selection(const QList<MetaSelectionItem> &items, bool from_selection);
    void set_tokens(const theme::Tokens &tokens);

signals:
    void rules_changed();
    void open_editor_requested(const QString &path);
    void clear_exception_requested(const QString &path);
};

} // namespace pp::ui
