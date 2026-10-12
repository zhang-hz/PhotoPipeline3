# m7r2-d2-gates —— R2D2「输出/运行/表单/编辑器/设置 行为尾单」关键门禁读数

> 包：R2D2（提交 `27c9f40`）｜原始日志：`build/m7r2-d2-*.log`、`build/m7r2-d2/handoff.md`（gitignored）。
> 摘录人：R2F（2026-10-12，自 R2D2 handoff ④ 节逐字摘录）。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m7r2-d2 -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7r2-d2
python tools/env.py run -- ctest --test-dir build/m7r2-d2 --output-on-failure
QT_QPA_PLATFORM=offscreen python tools/env.py run -- build/m7r2-d2/photopipeline.exe \
    --ui-smoke --inputs tests/golden/base --shots build/m7r2-d2/shots
python tools/env.py run -- python tests/golden/smoke.py build/m7r2-d2
```

## ② 读数/结果摘录

| 命令 | 读数（逐字） |
|---|---|
| 全量 ctest | `100% tests passed out of 35`（45.7s，含 test_smoke/ui_smoke；格式化后复跑同绿 44.9s） |
| ui-smoke 冻结行 | `UI-SMOKE OK shots=12 pages=3`（逐字不变） |
| output-geom 探针 | `output-geom: checked=29 bad=0 tiles=8 rows=15 six-rows=6 spin-short=88 spin-std=160 groups=4 pills=4` |
| settings-geom 探针 | `settings-geom: checked=42 bad=0`；run-overview 各读数断言绿 |
| 金样 | `SMOKE total=20 pass=20 fail=0`（冻结行逐字不动；引擎零改动面复核） |
| 格式（钉版，本包 9 文件） | exit 0；格式化后重建 + 全量 ctest 复跑同绿 |

## ③ 结论与判据

- 全绿；引擎零改动（金样复核）。
- 断言换锚一处（主对话授权，裁定 M7-R2-A）：ui_smoke `spin-group` 期望宽
  `kFieldShort(88)` → `kFieldStd(160)`（UX-8「折叠=88 第二档废除」定稿；语义等价、严格度不降）。
- 读数注记：`spin-short=88 spin-std=160` 为两档字段宽常量并存（kFieldShort 保留给
  spec §5「组内小数值」用途），非折叠态换档残余。
