// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — run page (M1b frozen)
#pragma once
#include "core/scheduler.h"
#include <QWidget>

namespace pp::ui {

class PageRun : public QWidget {
    Q_OBJECT
public:
    explicit PageRun(QWidget *parent = nullptr);

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
