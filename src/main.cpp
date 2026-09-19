// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M0 skeleton
#include <QApplication>
#include <QTimer>
#include "ui/mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    pp::ui::MainWindow w;
    w.show();
#ifdef PP_M0_SMOKE
    QTimer::singleShot(2000, &app, &QApplication::quit);  // M1 removes this
#endif
    return app.exec();
}
