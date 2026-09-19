// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMainWindow>

namespace pp::ui {
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
};
}  // namespace pp::ui
