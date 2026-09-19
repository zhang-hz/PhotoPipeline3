// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — self-drawn slippy map (M1b frozen)
#pragma once
#include <QWidget>
#include <memory>
#include "mapwidget/coord.h"

namespace pp::map {

class MapWidget : public QWidget {
    Q_OBJECT
public:
    explicit MapWidget(QWidget* parent = nullptr);
    ~MapWidget();

    void set_provider(const QString& id);          // "osm" | "amap"
    void set_amap_key(const QString& key);
    void set_cache_mb(int mb);                     // tile LRU byte budget
    void set_offline(bool off);                    // true: no network; cached tiles still drawn

    // All public coordinates are WGS-84. Internal tile math uses the provider
    // datum (GCJ-02 for amap); conversion happens at the boundary.
    void set_marker(double lat, double lon);
    void clear_marker();
    bool has_marker() const;
    double marker_lat() const;
    double marker_lon() const;
    void center_on(double lat, double lon, int zoom = -1);   // zoom -1 = keep current

    QSize minimumSizeHint() const override { return {240, 180}; }

signals:
    void point_selected(double lat, double lon);   // click (not drag) selection, WGS-84
    void search_finished(const QStringList& titles, const QString& error);
    void tiles_pending_changed(int pending);       // fetch queue depth (for spinners/tests)

public slots:
    void run_search(const QString& query);         // async; -> search_finished
    void search_select(int index);                 // pick result -> center+marker+point_selected

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pp::map
