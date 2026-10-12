# m7r21-f2-gates —— R2.1 F2「R2E1 重做：page_meta + mapwidget RTL 与结构拆分」门禁读数

> 包：M7-R2.1-F2（提交 `b83f185`/`8683c1e`/`bb5290c`/`9bd01b6`/`b9fd350`/`6e1b3bf`/`a2d55ce`/
> `7971cbe`/`38af841`/`40c385e`/`6bd2c15`/`d6360f6`）｜日期：2026-10-13｜原始日志：
> `build/m7r21-f2-*.log`、`build/m7r21-f2-meta-anchor.txt`、`build/m7r21-f2/rtl-probe-run2.log`
> （gitignored）。摘录人：F5（2026-10-13，自 F2 handoff ⑤ 与原始日志逐字摘录）。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m7r21-f2 -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7r21-f2
python tools/env.py run -- ctest --test-dir build/m7r21-f2 --output-on-failure
python tools/env.py run -- ctest --test-dir build/m7r21-f2 --output-on-failure -R "dms|gcj02|geocoder|layers|providers"
python tools/env.py run -- bash -c "QT_QPA_PLATFORM=offscreen build/m7r21-f2/photopipeline --ui-smoke --inputs tests/golden/base --shots build/m7r21-f2/shots"
grep -E "UI-SMOKE (meta-geom|meta-loop|meta-geometry|rtl-mirror)" build/m7r21-f2-uis-*.log
```

## ② 读数/结果摘录

- 全量 ctest：**100% tests passed out of 35**（B.1 十步 + RTL 每步后各一轮；
  终态 `build/m7r21-f2-ctest12.log`；基线轮 `build/m7r21-f2-ctest-base.log` 同值）。
- 地图域单测 `-R "dms|gcj02|geocoder|layers|providers"`：`100% tests passed out of 5`
  （`build/m7r21-f2-maptests-final.log`；test_gcj02 含 map_view 新断言组）。
- ui_smoke 冻结行 `UI-SMOKE OK shots=12 pages=3` 逐字不变；meta 四行与基线
  `build/m7r21-f2-meta-anchor.txt` **逐字节相同**（每步比对记录于 `build/m7r21-f2-uis-*.log`）：
  `meta-geom: checked=22 bad=0` / `meta-loop: checked=11 bad=0` / `meta-geometry` /
  `rtl-mirror`。
- **B2 逐字验证**：meta_format 15 个函数体对 git HEAD 逐字节比对 **ALL IDENTICAL**（脚本取证）。
- **RTL 临时探针四查全绿**（`build/m7r21-f2/rtl-probe-run2.log`，offscreen，不入库）：
  `stars-mirror OK w=68 ltr=[3,8] rtl=[59,64] mirror=[59,64]`；
  `hud-mirror OK box ltr=[6,282,397,295] rtl=[2,282,393,295] mirror-x0=2 mirror-x1=393`
  （LTR 左缘锚定 6 / RTL 右缘锚定 393=400-1-6）；`logical-align OK checked=3 bad=0`；
  `geom-mirror OK checked=2 bad=0`。

## ③ 结论与判据

- B.1 page_meta 拆分**零行为变化**：函数体逐字迁移 + meta 四探针行与锚逐字节相同；
  `page_meta.h` 逐字节未动（拆分只动 .cpp 与新增 meta_*.{h,cpp}）。
- R-218 RTL（本面 = page_meta/mapwidget）：逻辑坐标 + 单点镜像（LTR 恒等）；对齐一律
  AlignLeading/AlignTrailing；瓦片/图钉等地理投影内容不镜像（坐标基准红线）。
- R-181 收口：`pp::read_metadata`/QtConcurrent 只在 meta_probe.cpp 出现（page_meta 与各卡
  TU 零 IO）。B.5 步① map_view（投影/基准纯核入 pp_core）后地图域 5/5 全绿。
- 未跑（按分工归波次收口）：ci-tidy 棘轮、tools/regression.py 全语料回归（见 F2 handoff ⑤）。
