# M6 开发期裁定记录（主对话裁定，随波次滚动追加）

> 依据：`docs/m6-map-upgrade.md`（契约 C1-C10）。冻结接口（PP-FROZEN）的加性扩展走 §3.C9「加性 + 追认」，逐条登记于此。

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| A1 | **M6-T4 mapwidget.h 加性扩展(图层+结果图钉+pin_clicked)**：PP-FROZEN `mapwidget.h` 仅加性新增(3 处插入)，既有签名/行为零改动；加性面 = `Pin` + `set_tile_style`/`tile_style`/`set_result_pins`/`clear_result_pins` + 信号 `pin_clicked`；绘制/命中在 T5/T6；依据契约 C9 加性+追认（冻结头 diff 13 行全加、0 删） | 契约 C9 明文（加性 + 追认） | M6-T4 |
| A2 | **M6-T7 mapwidget.h 加性(suggest/reverse/search_results_ready + run_suggest/run_reverse)**：PP-FROZEN `mapwidget.h` 仅加性新增(3 处插入：include `geocoder.h` + public slots 两行 + signals 三行)，既有签名/行为零改动（diff 6 行全加、0 删）；**`run_search` 行为升级为 Geocoder 周边偏置**（6→20 条、`accept-language=zh`、视口中心 `near`、周边无果回退全文）= 预期 R1 特性（docs/m6-map-upgrade.md §4.3/§7-T7），走追认；冻结信号 `search_finished` 仍逐次发射且顺序 == 内部 results 顺序 | 契约 C9 明文（加性 + 追认）；R1 能力升级为设计预期行为 | M6-T7 |
