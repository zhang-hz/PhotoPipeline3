# M6 开发期裁定记录（主对话裁定，随波次滚动追加）

> 依据：`docs/m6-map-upgrade.md`（契约 C1-C10）。冻结接口（PP-FROZEN）的加性扩展走 §3.C9「加性 + 追认」，逐条登记于此。

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| A1 | **M6-T4 mapwidget.h 加性扩展(图层+结果图钉+pin_clicked)**：PP-FROZEN `mapwidget.h` 仅加性新增(3 处插入)，既有签名/行为零改动；加性面 = `Pin` + `set_tile_style`/`tile_style`/`set_result_pins`/`clear_result_pins` + 信号 `pin_clicked`；绘制/命中在 T5/T6；依据契约 C9 加性+追认（冻结头 diff 13 行全加、0 删） | 契约 C9 明文（加性 + 追认） | M6-T4 |
