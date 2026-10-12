# m7r2-d1-gates —— R2D1「主窗/列表/预览/分类/page_meta 行为尾单」关键门禁读数

> 包：R2D1（提交 `6e001da`）｜原始日志：`build/m7r2-d1-*.log`、`build/m7r2-d1/handoff.md`（gitignored）。
> 摘录人：R2F（2026-10-12，自 R2D1 handoff ④ 节逐字摘录）。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m7r2-d1 -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7r2-d1
python tools/env.py run -- ctest --test-dir build/m7r2-d1 --output-on-failure
python tools/env.py run -- python tests/ui_smoke.py build/m7r2-d1
python tools/env.py run -- python tests/golden/smoke.py build/m7r2-d1
```

## ② 读数/结果摘录

| 命令 | 读数（逐字） |
|---|---|
| 构建 | Linking photopipeline.exe OK |
| 全量 ctest | `100% tests passed out of 35`（build/m7r2-d1-ctest1.log） |
| UI 冒烟 | `UI-SMOKE OK shots=12 pages=3`（exit 0，冻结行逐字）；icons 探针 `count=38 required=34 bad-svg=0`（check-all 同源过） |
| 金样 | `SMOKE total=20 pass=20 fail=0`（引擎零改动面） |
| 格式（钉版二进制，本包 19 个改动文件） | 0 diff 后入库 |

## ③ 结论与判据

- 引擎面零改动（src/core、src/codecs、src/decode 无 diff）→ 金样/回归基线零影响。
- 断言换锚一处（主对话授权，裁定 M7-R2-B）：`smoke_probes.cpp` 两处 `map->height() == 260`
  → `>= 260`（UX-4 stretch 后 260 是下限）；`map->geometry() == card_geom` 逐字保留。
- 施工期并行包 WIP 两度挡全量构建（他包文件 `findChild<formkit::ElidedLabel*>` 静态断言），
  收尾读数均取自全量链接成功之后。
