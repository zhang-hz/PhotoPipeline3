// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/mainwindow.h"

namespace pp::ui {
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("PhotoPipeline M0"));
    resize(1200, 760);
}
}  // namespace pp::ui
