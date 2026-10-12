# m7r2-e2-gates —— R2E2「RTL 与结构拆分 + 第 3 波常驻探针」关键门禁读数

> 包：R2E2（提交 `52b9870`）｜原始日志：`build/m7r2-e2-*.log`、`build/m7r2-e2/handoff.md`（gitignored）。
> 摘录人：R2F（2026-10-12，自 R2E2 handoff ④ 节逐字摘录）。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m7r2-e2 -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7r2-e2
python tools/env.py run -- ctest --test-dir build/m7r2-e2 --output-on-failure
python tools/env.py run -- python tests/ui_smoke.py build/m7r2-e2
python tools/env.py run -- python tests/golden/smoke.py build/m7r2-e2
```

## ② 读数/结果摘录

| 命令 | 读数（逐字） |
|---|---|
| 全量 ctest | `100% tests passed out of 35`（格式化前后各一轮均绿） |
| ui-smoke 冻结行 | `UI-SMOKE OK shots=12 pages=3`（冻结行逐字不变、零新截图） |
| 第 3 波常驻探针 | `rtl-mirror: checked=11 bad=0` / `check-all: checked=11 bad=0 rows=16` / `context-menu: cards=12 checked=36 bad=0` / `run-reason: checked=4 bad=0 reasons=32` / `folds: checked=6 bad=0 groups=4` |
| 既有几何探针 | `output-geom: checked=29 bad=0` / `settings-geom: checked=39 bad=0` |
| 金样 | `SMOKE total=20 pass=20 fail=0`（引擎零改动面） |
| test_smoke 第 6 节（B.3 format_policy 真值表） | 随 ctest `test_smoke` 全绿 |

## ③ 结论与判据

- 全绿；冻结行与 12 张截图零变化（新增探针不加截图）；引擎零改动。
- LTR 逐字节不变由 `rtl-mirror` 的 `rtl=false 恒等` 断言 + 金样 20/20 背书；RTL 镜像关系
  （row_rects/check_rect/逻辑边距随向）由同探针钉死。
- **未跑**（按分工归波次收口）：`tools/ci-tidy.py` 棘轮、`tools/regression.py` 全语料回归。
  实况：R2 轮收口门禁由 R2-gate（回归重播种 + 复跑 zero diff，`m7r2-gate-regression-reseed.md`）
  与后续 R2F/R2REL 波次统一把关。
