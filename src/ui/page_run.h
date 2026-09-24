// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — run page (M1b frozen; M4-T14 追加面)
//
// PP-FROZEN(0.3.0) —— 运行页「逐输出进度 + 总览 + 运行期锁定（G5）」（设计 §7/§8/§9.1/§9.3；
//   共识 §3.4；mockup run-dark）。落地任务 = W3-T14。本文件 0.3.0 的**追加面**（既有方法
//   签名/语义一字不动）：
//     * `struct RunPlan` + `set_plan()`：运行计划（逐源文件宽/高/字节 = 行数文案与压缩比的
//       输入；格式清单 = 逐输出子行的配置顺序；交错/线程预算/并行 = 总览卡只读读数；
//       §3.4/§8.1/§8.2）。**必须在 begin_run() 之前调用**（begin_run 用它建行）。
//     * `set_tokens(tokens)`：主题 tokens（QSS/配色单源 ui/theme.h；与 PageMeta/PageOutput/
//       PreviewPanel/ClassifyPanel 同一注入口径）。
//     * `set_log_tail(lines, log_name)`：日志尾卡内容（**单一持有者 = MainWindow**：页只渲染，
//       不读文件系统 —— 与预设卡 I/O 同口径）。
//     * 运行读数（底栏/走查消费）：`progress_percent()`（§7.1 运行级总进度）、`eta_seconds()`、
//       `outputs_done()/outputs_total()`。
//   既有方法（begin_run/on_event/end_run/reset/is_running 与三枚信号）签名不变；语义随
//   0.3.0 的逐输出进度收敛（详见 .cpp 头注释）。页内状态仍在 .cpp 的注册表里（§2.13：冻结头
//   不含私有成员）。
#pragma once
#include "core/scheduler.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <cstddef>
#include <cstdint>

namespace pp::ui {

// 前置声明（不拉 ui/theme.h：theme.h 依赖包含者先引入 <QApplication>，而本头会被 AUTOMOC 的
// mocs_compilation.cpp 直接包含 —— 与 ui/page_output.h / page_meta.h 同口径）
namespace theme {
struct Tokens;
}

class PageRun : public QWidget {
    Q_OBJECT
public:
    explicit PageRun(QWidget *parent = nullptr);

    // ---- PP-FROZEN(0.3.0) 追加面（M4-T14）----
    // 运行计划（§3.4/§7.2/§8.1/§8.2）：逐源文件的尺寸/字节 + 输出格式清单 + 调度读数。
    // 逐项与运行集合（勾选口径）一一对应、下标 = 提交序号（ev.index）。
    struct RunPlan {
        QStringList format_ids; // 配置顺序（= RunConfig::outputs 顺序）
        QVector<int> widths;    // 逐源文件宽（0 = 未知 → 组行只显示「N 个输出」）
        QVector<int> heights;   // 逐源文件高（0 = 未知 → 编码行文案退化为百分比）
        QVector<qint64> bytes;  // 逐源文件字节（0 = 未知 → 完成行不显示压缩比）
        int stagger_ms = 0;     // §8.1 交错步距（>0 → 排队行文案带「交错启动」）
        int workers = 0;        // 0 = 逻辑核（§8.2 的并行上界）
        int thread_budget = 0;  // 0 = 自动（§9.3 设置项「线程预算」）
        bool metadata_only = false;
    };

    void set_tokens(const theme::Tokens &tokens);
    void set_plan(const RunPlan &plan); // begin_run 之前调用（之后调用无效：行已建）
    // 日志尾卡：lines = 末尾若干行（页只渲染末 5 行）；log_name = 卡头 hint（空 → 不显示）
    void set_log_tail(const QStringList &lines, const QString &log_name);

    int progress_percent() const; // §7.1 运行级总进度（0..100；-1 = 未开始）
    int eta_seconds() const;      // 粗估剩余秒（< 0 = 不可估）
    int outputs_done() const;     // 已结算（终态）输出数
    int outputs_total() const;    // files × formats
    int files_started() const;    // 已取件的源文件数（§8.2 并行读数：取件 − 结算 = 进行中）
    int files_terminal() const;   // 已结算的源文件数

    // ---- M1b 冻结面（签名/语义不变）----
    void begin_run(std::size_t total, const QStringList &names); // reset + populate rows
    void on_event(const pp::FileEvent &ev); // GUI thread; ev.result already copied by caller
    void end_run(const pp::RunSummary &sum, const QString &out_root);
    void reset(); // idle state

    bool is_running() const;

signals:
    void cancel_requested();
    void open_output_requested(const QString &dir);
    void open_logs_requested();
};

} // namespace pp::ui
