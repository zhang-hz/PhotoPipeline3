// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — output page (M1b frozen; M4-T13 追加面)
//
// PP-FROZEN(0.3.0) —— 输出页「多格式 + 路径模板 + 预设 v2」（设计 §3.3/§4.2/§9.3；mockup
//   output-dark）。落地任务 = W3-T13。本文件 0.3.0 的**追加面**（既有方法签名/语义一字不动）：
//     * `set_tokens(tokens)`：主题 tokens（QSS/配色单源 ui/theme.h；与 PageMeta/PreviewPanel/
//       ClassifyPanel 同一注入口径）；
//     * `set_preset_list(items)`：预设卡的列表喂入（(path, name) 对，来自
//       `pp::ui::list_presets(presets_dir)`）；
//     * `output_template()/set_output_template()`、`split_by_format()/set_split_by_format()`：
//       路径模板与分文件夹开关的读写面（§4.2；设置持久化 / 走查用；setter 不发信号）；
//     * 三枚预设卡信号（保存/载入/删除）——文件 I/O 与 rules 合并仍归 MainWindow（单一持有者）。
//   既有方法（config_base/metadata_only/set_metadata_only/out_root/set_out_root/
//   set_batch_has_alpha/apply_preset/collect_preset/restore_last/ready_to_start/
//   current_format/select_format 与四枚信号）签名不变；语义随多格式/多选收敛（详见 .cpp 头注释）。
#pragma once
#include "core/pipeline.h"
#include "core/presets.h"
#include <QString>
#include <QWidget>
#include <memory>
#include <utility>
#include <vector>

namespace pp::ui {

class ParamForm;

// 前置声明（不拉 ui/theme.h：只为减头重/依赖面 —— theme.h 自 M5-A5 起已自含 <QApplication>，
// 旧理由「依赖包含者先引入 <QApplication>」已失实（R-268 同病灶改实）；本头又会被 AUTOMOC 的
// mocs_compilation.cpp 直接包含，做法与 ui/paramform.h / page_meta.h 同口径）
namespace theme {
struct Tokens;
}

class PageOutput : public QWidget {
    Q_OBJECT
public:
    explicit PageOutput(QWidget *parent = nullptr);
    ~PageOutput();

    // RunConfig skeleton: out_root/outputs[]/color/conflict/output_template/split_by_format/
    // metadata_only filled; rules/workers/budget/rotate/flatten left at defaults (MainWindow
    // fills). 0.3.0：outputs[] = **全部已选格式**（配置顺序 = 磁贴顺序）；仅元数据模式恒 1 项（§3.2
    // 硬校验）。
    pp::RunConfig config_base() const;

    bool metadata_only() const;
    void set_metadata_only(bool on); // 禁用格式多选区/格式参数卡并出提示（D7 互斥）
    QString out_root() const;
    void set_out_root(const QString &dir);

    void set_batch_has_alpha(bool has); // avif 10bit+alpha -> libaom preselect

    void apply_preset(const pp::PresetData &p); // 全量落值（v2：多输出/模板/分文件夹）
    pp::PresetData collect_preset(const QString &name) const; // rules 由 MainWindow 合并

    // session restore (no signals); invalid values ignored
    void restore_last(const QString &format_id, const QString &out_root);

    // "" = ready; else human-readable reason (Start button gating)
    QString ready_to_start() const;

    // ---- PP-FROZEN 加性追加（M8-B2；追认登记 docs/m8-adjudications.md A20）----
    // 运行前参数预检（C6）：`开始运行` 被 ready_to_start() 拒绝后调用一次 —— 切到输出页、
    // 选中第一个有错误的格式页签、聚焦该格式第一个错误行。无错误 = 仅切页/滚动，无副作用。
    void focus_first_rule_error();

    // smoke/test hooks
    QString current_format() const;               // 当前页签的格式（激活项）
    void select_format(const QString &format_id); // 程序化：已选集合收敛为 {format_id} 并激活

    // ---- PP-FROZEN(0.3.0) 追加面（M4-T13）----
    void set_tokens(const theme::Tokens &tokens);
    // ---- PP-FROZEN(0.3.0) 追加面（M5-T8 · §7.2 预设与预估卡）----
    // 预估卡的**输入面**：勾选文件数（MainWindow 在 refresh_status 里喂入；页面据此算
    // 「预计输出 = 勾选数 × 已选格式数」，其余预估项无实测依据 → 中性空态）。不发信号
    // （只在页面内部刷新读数），故不会与 refresh_status 形成回环。
    void set_input_count(int files);
    void set_preset_list(const std::vector<std::pair<QString, QString>> &presets);
    void set_current_preset(const QString &path); // 高亮当前预设（不发信号）
    QString output_template() const;
    void set_output_template(const QString &tmpl); // programmatic (no signal)
    bool split_by_format() const;
    void set_split_by_format(bool on); // programmatic (no signal)：开 → 模板插 $format 段
    // —— M7-R2.1-F4 / R-319 加性（既有签名逐字不变；裁定登记 docs/m7-adjudications.md
    // 十一节）：镜像基准目录（CLI `--base` 的 GUI 面，opt-in）。空 = 现行为（各输入文件
    // 所在目录）；非空 = 基准目录包含的输入按其折算相对输出树。改动即发
    // mirror_base_changed；set_ 为程序化不发信号（同组既有 set_* 口径）。
    QString mirror_base() const;
    void set_mirror_base(const QString &base); // programmatic (no signal)
    // —— M7-R2.1-F4 / UX-14 C1–C4 加性：色彩四选项读回（线性光重采样同时透传预览通道）。
    // 其余三档经 config_base() 进 RunConfig；本读回供主窗同步预览缓存。
    bool linear_resample() const;

signals:
    // R-133（M7-R2.1-F4）：format_changed 孤儿信号**删除**（任务明文「接线或删除」+ R2D1-A3
    // 零消费声明删除先例，裁定追认 docs/m7-adjudications.md 十一节）—— 声明 + 2 处发射 +
    // 零 connect 全链退役；config_changed 承载原「变化」通知（toggle/现役变更都发），既有
    // 消费面（mainwindow refresh_status）行为逐字不变。
    void config_changed(); // anything summary-worthy
    void manage_presets_requested();
    void open_settings_requested();
    // ---- PP-FROZEN(0.3.0) 追加面（M4-T13）预设卡（mockup output-dark 的「预设」卡）----
    void preset_load_requested(const QString &path);
    void preset_save_requested(const QString &name); // 保存到 <presets_dir>/<name>.json
    void preset_saveas_requested();                  // 另存…（MainWindow 走 PresetsDialog 命名）
    void preset_delete_requested(const QString &path);
    void output_template_changed(const QString &tmpl); // 模板变化（设置持久化/走查）
    void mirror_base_changed(const QString &base);     // R-319：镜像基准目录变化（空 = 清除）

    // ---- PP-FROZEN 加性追加（M10 / W3-T2；契约 docs/v0.4.0-design.md §8.5「C23」/§9「C24」；
    //      追认登记 docs/m10-adjudications.md A9）----
    // 格式预设行（每格式页签顶部一行）的意图信号：**文件 I/O、完整性校验、归一、二次确认、
    // 绝对路径守卫一律归 MainWindow（单一持有者，mainwindow_presets.cpp）** —— 与全局预设卡
    // （preset_load_requested/…）同一分工。页面只发意图 + 落卡 + 只读自适应面。
    // 未选中预设时「保存」走 save_as（另存…）；「删除」只在有选中项时发；「导出」带当前
    // 会话态路径（可能为空 = 未选中，MainWindow 据此决定默认文件名）。
    void format_preset_list_requested(const QString &format_id); // 页签建立/激活：请喂列表
    void format_preset_load_requested(const QString &path);      // 下拉选中即载入
    void format_preset_save_requested(const QString &path);      // 覆盖当前预设文件
    void format_preset_save_as_requested();                      // 另存…（MainWindow 命名）
    void format_preset_delete_requested(const QString &path);    // 删除（二次确认在 MainWindow）
    void format_preset_import_requested();                       // 导入…（文件对话框）
    void format_preset_export_requested(const QString &path);    // 导出…（文件对话框）

public:
    // ---- PP-FROZEN 加性追加（M10 / W3-T2）：格式预设行的**视图面**（末尾追加，既有方法/
    //      信号签名与语义一字不动）----
    // 分工（设计 §8.5「与全局预设无隐式叠加」）：卡值真源仍是 spec/form->values()，预设只是
    // "写入器"；本组方法只做「列表喂入 / 会话态高亮 / 落卡 / 只读自适应面 / 注释位」。
    //   * set_format_preset_list：列表项 = (绝对路径, 显示名)；显示名已含「（损坏）」标记
    //     （由 MainWindow 组装）。首项恒为「未选择」（itemData 为空）。同值幂等，保留当前选中。
    //   * set_current_format_preset：程序化高亮（不发信号、不载入）；空路径 = 回到「未选择」。
    //   * apply_format_preset：把已 normalize/validate 的输出快照落到**该格式卡**（后端/技术/
    //     位深/核心+高级参数/保留键 __heif_profile），并重算卡头「已改 N」（无信号）。
    //   * set_format_preset_adaptive：只读自适应面（C24：GUI **不提供文本编辑**）。策略非空 →
    //     摘要「目标 X / 最多 N 轮 / K 个阶段」+「启用自适应」开关（初值 = 预设 enabled；关闭 =
    //     本次不启用）；解析/校验失败 → 行内中文错误 + 开关禁用；空策略 → 整行不显示。
    //   * set_format_preset_note：注释位（「来自格式预设：X」/「已载入（已补齐 N 个默认项）」）；
    //     空串 = 清空并隐藏注释位（不占行高；同一行控件始终在位）。
    //   * format_preset_adaptive_enabled / format_preset_adaptive_policy：探针/采集读回。
    void set_format_preset_list(const QString &format_id,
                                const std::vector<std::pair<QString, QString>> &presets);
    void set_current_format_preset(const QString &format_id, const QString &path);
    QString current_format_preset(const QString &format_id) const;
    void apply_format_preset(const pp::OutputFormatSpec &spec);
    void set_format_preset_adaptive(const QString &format_id, const QString &policy, bool enabled);
    void set_format_preset_note(const QString &format_id, const QString &note);
    bool format_preset_adaptive_enabled(const QString &format_id) const;
    QString format_preset_adaptive_policy(const QString &format_id) const;
};

} // namespace pp::ui
