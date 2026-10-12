# docs/m7-evidence/ — M7 波次证据留存目录

> **这个目录是什么**：M7 起各任务包（R2A–R2F、R2REL 等）把**关键门禁读数、失败日志摘录、
> 线上实况取证**写入本目录，命名 `m7r2-<pkg>-<主题>.md`，供**换机审计**——回应 M7 校读意见
> 「证据全在 `build/`（gitignored）路径，换机/换人即失证」。本目录**入库**；`build/` 下的
> 原始日志仍放各自 `build/<task-id>/`，此处只留**摘录 + 命令行 + 结论**，并在摘录里注明
> 原始日志路径（两层互补：本目录可长期复核，build/ 保全量原文）。

## 规矩（各包共同遵守）

1. **命名**：`m7r2-<pkg>-<主题>.md`。`<pkg>` = 任务包名（小写，如 `c`、`rel`、`f`）；
   `<主题>` = 证据内容（如 `winzip-gates`、`ci-budget-timings`）。跨包通用的文档
   （如 `release-runbook.md`）不带包前缀。
2. **内容三段式**：① **命令行**（逐字可复跑，含 `python tools/env.py run --` 包装）；
   ② **读数/结果摘录**（关键行逐字保留，长输出截断注明 `…`）；③ **结论与判据**（过/不过、
   差异归因）。失败也照实写——失败证据比成功证据更贵。
3. **不出新判据**：本文档只记录既有判据的实跑读数；判据本身的改动走裁定（adjudications）。
4. **不贴密钥/凭据**：任何摘录先过 `docs/m6-report.md` §6 的扫描口径（值形态 0 命中）。
5. **不覆盖旧证据**：同一主题有新读数就新开 `m7r2-<pkg>-<主题>-<日期或轮次>.md` 或在文内
   加「追记」节，保留历史读数供对比。

## 当前条目

| 文件 | 包 | 主题 |
|---|---|---|
| `release-runbook.md` | （通用） | 发布链操作手册：暖度验证、tag 前置条件、公共 API 轮询、失败即停 |
| `m7r2-c-winzip-gates.md` | R2C | make_winzip 五门禁本地全跑读数（含 E-2 offscreen 走查） |
| `m7r2-c-ci-budget-timings.md` | R2C | 发布链/主干道预算实况取证（公共 API job/step 耗时 + warm-cache 秒败 + 缓存链地图） |
| `m7r2-a-gates.md` | R2A | 色彩能力与解码语义门禁读数（ctest 35/35、金样 20/20、test_color 1351、test_decode 642、A1 位流 VUI、回归归因 heif/avif +20B×31） |
| `m7r2-b-gates.md` | R2B | 核心与参数域尾单门禁读数（7 目标子集、金样 20/20、regression --self-check 19 例、test_color 他包在途注记） |
| `m7r2-d1-gates.md` | R2D1 | 主窗/列表/预览/分类/page_meta 门禁读数（ctest 35/35、ui_smoke 冻结行、icons bad-svg=0、M7-R2-B 换锚） |
| `m7r2-d2-gates.md` | R2D2 | 输出/运行/表单/编辑器/设置 门禁读数（ctest 35/35、output-geom/settings-geom、M7-R2-A 换锚） |
| `m7r2-e2-gates.md` | R2E2 | RTL 与结构拆分门禁读数（ctest 35/35、第 3 波 5 探针逐行、金样 20/20） |
| `m7r2-gate-regression-reseed.md` | R2-gate | 回归基线重播种 132 行逐行归因（A1 nclx 精确信令）+ M7 窗口 4 次重播种上下文 |
| `m7r2-gate.md` | R2G | R2 终门禁全量判据取证（format/build/ctest/ui-smoke/金样/tidy/回归/密钥 八项 + tidy 棘轮 fallout 修复 23 条清单 + 首跑瞬态回归 diff 归因） |
