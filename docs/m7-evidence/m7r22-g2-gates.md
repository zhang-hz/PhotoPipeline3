# m7r22-g2-gates —— R2.2 G2「结构余量与语义裁定落地」门禁读数

> 包：M7-R2.2-G2（15 提交 `12234f1`/`0208651`/`8c90c54`/`9f01cb1`/`6d5d07e`/`fa60b38`/
> `77e3c90`/`d677c52`/`05181ac`/`847a83d`/`4c81bd0`/`0552023`/`ec1e3ba`/`7541683`/`4dee5c8`）｜
> 日期：2026-10-13｜原始日志：`build/m7r22-g2-build*.log`、`build/m7r22-g2-ctest*.log`、
> `build/m7r22-g2-uis*.log`、`build/m7r22-g2-golden.log`、`build/m7r22-g2-regression.log`
> （gitignored）。摘录人：G4（2026-10-13，自 G2 handoff ④ 逐字摘录；G4 另于合流 HEAD 复跑
  ui_smoke/金样/回归，见 `m7r22-g4-gates.md`）。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m7r22-g2 -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7r22-g2
python tools/env.py run -- ctest --test-dir build/m7r22-g2 --output-on-failure
python tools/env.py run -- python tests/ui_smoke.py build/m7r22-g2
python tools/env.py run -- python tests/golden/smoke.py build/m7r22-g2
python tools/env.py run -- python tools/regression.py build/m7r22-g2
bash tools/ci-format.sh
```

## ② 读数/结果摘录

- ctest（终态 ctest11）：**`100% tests passed out of 36`**（35 既有 + test_meta_format）；
  `ctest -R test_gcj02`（新断言 29：tile_cache LRU/预算 8 + 交互状态机 21）与
  `-R test_meta_format`（38）均 Passed。
- ui_smoke（终态 uis11）：`UI-SMOKE OK shots=12 pages=3`（冻结行逐字不变）；分项 classify
  六探针（panel/grid/tag/filter/menu/bulk）逐值绿、settings-geom 39、output-geom 29、
  filelist 族、map 族全绿。
- 金样（golden.log）：`SMOKE total=20 pass=20 fail=0`（冻结行逐字）。
- 回归（regression.log）：**`regression: zero diff (baseline reproduced)`**（normalized
  2448 lines vs baseline 2448；meta-* `failed=4` 槽位与基线一致 = Exiv2 只读面既有失败）。
  **R-197 回归归因**：时间清除 = GUI 例外编辑面（TagEdit remove），`--dev` 语料回归不经过
  exif_editor 路径——实测 zero diff 印证，无需重播种。
- 格式（`4dee5c8` 收口后）：`ci-format: OK —— 全源 0 diff`（钉版 clang-format 23.1.1）。
- **B.5 拆分收口读数**：mapwidget.cpp **1444 → 759 行**（G2 handoff 记数）/ **760 行**
  （G4 `wc -l` 复核，差 1 系计数口径）；MapWidget::Impl 165 行；拆出 6 件（tile_cache.h /
  map_interact.h / tile_cache / tile_fetcher / map_renderer / geocode_client）；CMakeLists
  零改动。
- 中途噪声归因（如实）：`847a83d` 期间 ui_smoke 曾 4 红（failure-list 探针）——归因 = G1
  在途未提交探针（比对面 QDir::tempPath() 正斜杠 vs Win 反斜杠路径）；G1 `4addc51` 修好入仓
  后全绿，终态复跑 36/36 + 冻结行。

## ③ 结论与判据

- **本包判据全绿**（Windows 侧，每单元 build + ctest 后提交）：36/36、金样 20/20、ui_smoke
  冻结行、回归 zero diff、全源格式 0 diff。
- **R-197 行为裁定（M7R22G2-0）换锚有据**：test_smoke 第 8 节换锚 3 条 + 新增 2 条（断言
  只增不减）；回归 zero diff 证明该行为面不经 `--dev` 语料路径，基线无需重播种。
- **行为零变化面锚定**：R-256 稳态逐事件同值 / R-201 null 守卫同义 / R-183 稳态值表逐值同旧
  / B.5 全五步绘制·交互·网络逐位（M7R22G2-6）；金样 20/20 + 回归 zero diff 双重背书。
