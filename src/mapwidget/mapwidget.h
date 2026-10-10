// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — self-drawn slippy map (M1b frozen)
#pragma once
#include "mapwidget/coord.h"
#include "mapwidget/geocoder.h"
#include "mapwidget/layers.h"
#include <QWidget>
#include <memory>
#include <vector>

namespace pp::map {

class MapWidget : public QWidget {
    Q_OBJECT
public:
    explicit MapWidget(QWidget *parent = nullptr);
    ~MapWidget();

    void set_provider(const QString &id); // "osm" | "amap"
    void set_amap_key(const QString &key);
    void set_cache_mb(int mb);  // tile LRU byte budget
    void set_offline(bool off); // true: no network; cached tiles still drawn

    // All public coordinates are WGS-84. Internal tile math uses the provider
    // datum (GCJ-02 for amap); conversion happens at the boundary.
    void set_marker(double lat, double lon);
    void clear_marker();
    bool has_marker() const;
    double marker_lat() const;
    double marker_lon() const;
    void center_on(double lat, double lon, int zoom = -1); // zoom -1 = keep current
    // M4-T12（加性只读回读；§5.2「地图 center_on 生效坐标」的取证/接线面，行为零改动）：
    // 视口中心（WGS-84）。元数据页的「地图选点」取中心为坐标、自检断言 center_on 落点用。
    double center_lat() const;
    double center_lon() const;
    // M6 加性：结果图钉 + 图层切换（追认见 docs/m6-adjudications.md）
    struct Pin {
        double lat = 0, lon = 0; // WGS-84
        QString label;
        int index = 0;
    };
    void set_tile_style(TileStyle style);
    TileStyle tile_style() const;
    void set_result_pins(const std::vector<Pin> &pins);
    void clear_result_pins();
    // M6-T12 加性（追认见 docs/m6-adjudications.md A3）—— 地图体验件（§5.3 / §7-T12）：
    // Hybrid 的「叠标注」开关（false = 只画影像层）与缩放 ±1（clamp 到既有档域，中心不动）。
    void set_sat_labels(bool on);
    void zoom_in();
    void zoom_out();

    QSize minimumSizeHint() const override { return {240, 180}; }

signals:
    void point_selected(double lat, double lon); // click (not drag) selection, WGS-84
    void search_finished(const QStringList &titles, const QString &error);
    void tiles_pending_changed(int pending); // fetch queue depth (for spinners/tests)
    void pin_clicked(int index);             // M6 加性：点击结果图钉
    void suggest_finished(const std::vector<SuggestItem> &items, const QString &error);
    void reverse_finished(const QString &address, const QString &error);
    void search_results_ready(const std::vector<GeocodeResult> &results, const QString &error);
    void cursor_moved(double lat, double lon); // M6-T12 加性：光标世界坐标（WGS-84；约 100ms 节流）

public slots:
    void run_search(const QString &query); // async; -> search_finished
    void search_select(int index);         // pick result -> center+marker+point_selected
    void run_suggest(const QString &query);
    void run_reverse(double lat, double lon);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pp::map
