// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — main window, three zones (M1b frozen)
//
// M4-T9：本文件的**公开 API/私有签名逐字不变**（无增删）；仅更新本注释以反映 0.3.0 骨架
// 重构（顶栏兼标题栏的无边框窗口 + 三栏 QSplitters），实现全部落在 mainwindow.cpp 的
// MainWindow::Impl 内（沿 M1b 口径：冻结头不承载控件与状态）。
#pragma once
#include "core/pipeline.h"
#include "core/scheduler.h"
#include "core/settings.h"
#include <QMainWindow>
#include <memory>

namespace pp::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const pp::AppSettings &settings, QWidget *parent = nullptr);
    ~MainWindow();

    void add_paths(const QStringList &paths); // files/dirs (drop / smoke / api)
    void set_current_page(int one_based);     // 1=meta 2=output 3=run
    void set_offline_maps(bool off);          // smoke / degraded

    // dev-only scripted walk (PP_BUILD_DEV callers): pages + dialogs, optional PNG grabs
    void ui_smoke_walk(const QString &shots_dir);

private slots:
    void on_start();
    void on_cancel();
    void on_scheduler_done(); // QTimer poll -> finish run
    void open_settings();
    void manage_presets();
    void open_exif_editor_row(int row);
    void open_exif_editor_path(const QString &path);

private:
    void build_ui();
    void wire();
    void lock_for_run(bool lock); // G5
    void refresh_status();        // bottom summary + start enable
    void save_session();          // closeEvent
    // zones/pages/models (unique_ptr<...> members; types not repeated here — keep private)
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pp::ui
