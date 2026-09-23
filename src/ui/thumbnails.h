// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — async probe/thumbnail provider (M1b frozen)
#pragma once
#include "core/types.h"
#include <QImage>
#include <QObject>
#include <QString>

namespace pp::ui {

// <=2 worker threads (design §1.3), FIFO queue, results on the GUI thread.
class Thumbnailer : public QObject {
    Q_OBJECT
public:
    explicit Thumbnailer(int target_long_edge = 96, QObject *parent = nullptr);
    ~Thumbnailer(); // stops workers immediately (detaches pending)

    void request(int row, const QString &path);
    void clear_pending();
    int pending() const;

signals:
    void ready(int row, const QString &path, const pp::ImageInfo &info, bool probe_ok,
               const QString &error, const QImage &thumb);
    void queue_empty(); // pending reached 0 (after >=1 request)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pp::ui
