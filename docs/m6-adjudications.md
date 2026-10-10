# M6 开发期裁定记录（主对话裁定，随波次滚动追加）

> 依据：`docs/m6-map-upgrade.md`（契约 C1-C10）。冻结接口（PP-FROZEN）的加性扩展走 §3.C9「加性 + 追认」，逐条登记于此。

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| A1 | **M6-T4 mapwidget.h 加性扩展(图层+结果图钉+pin_clicked)**：PP-FROZEN `mapwidget.h` 仅加性新增(3 处插入)，既有签名/行为零改动；加性面 = `Pin` + `set_tile_style`/`tile_style`/`set_result_pins`/`clear_result_pins` + 信号 `pin_clicked`；绘制/命中在 T5/T6；依据契约 C9 加性+追认（冻结头 diff 13 行全加、0 删） | 契约 C9 明文（加性 + 追认） | M6-T4 |
| A2 | **M6-T7 mapwidget.h 加性(suggest/reverse/search_results_ready + run_suggest/run_reverse)**：PP-FROZEN `mapwidget.h` 仅加性新增(3 处插入：include `geocoder.h` + public slots 两行 + signals 三行)，既有签名/行为零改动（diff 6 行全加、0 删）；**`run_search` 行为升级为 Geocoder 周边偏置**（6→20 条、`accept-language=zh`、视口中心 `near`、周边无果回退全文）= 预期 R1 特性（docs/m6-map-upgrade.md §4.3/§7-T7），走追认；冻结信号 `search_finished` 仍逐次发射且顺序 == 内部 results 顺序 | 契约 C9 明文（加性 + 追认）；R1 能力升级为设计预期行为 | M6-T7 |
| A3 | **M6-T12 mapwidget.h 加性(`set_sat_labels` + `zoom_in`/`zoom_out` + 信号 `cursor_moved`)**：PP-FROZEN `mapwidget.h` 仅加性新增（2 处插入：public 段三方法 + signals 段一信号；diff 6 行全加、0 删、既有行一字符未动），既有签名/行为零改动；加性面 = 混合图层「叠标注」开关（消费 T11 的 `map_sat_labels`）、缩放 ±1（clamp [kMinZoom,kMaxZoom]、中心不动，与滚轮同一内部字段）、光标世界坐标信号（WGS-84、约 100 ms 节流、仅移动时发）。**另：启用 `setMouseTracking`**（悬停即可回显光标坐标）——不动冻结的滚轮/拖拽/点击三件套（docs/m1b-tasks.md §2.5 line 225 逐字行为保持） | 契约 C9 明文（加性 + 追认）；R5 体验项（§5.3/§7-T12） | M6-T12 |
