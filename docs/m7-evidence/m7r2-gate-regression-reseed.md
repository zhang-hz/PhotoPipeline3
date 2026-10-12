# m7r2-gate-regression-reseed —— R2 轮回归基线重播种逐行归因

> 包：R2-gate（提交 `09b5340`）｜原始日志：`build/m7-gate/g-regression*.log`、
> `.cache/regression/attribution.log`（gitignored）。摘录人：R2F（2026-10-12，自提交正文
> 与 R2A handoff ④ 归因行逐字摘录）。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- python tools/regression.py                    # → DIFF FOUND（132 行）
python tools/env.py run -- python tools/regression.py --update           # 归因确认后重播种
python tools/env.py run -- python tools/regression.py                    # → zero diff（复跑）
```

## ② 读数/结果摘录

- 归因（12 sections paired；removed/added by kind 均 0，纯 in-place transition）：
  共 66 对 = 132 行，四类，全部可归 **UX-14 A1 nclx 精确信令**（`c540630`）：
  1. `output-done bytes=+20` × 31 对（heif 17 文件 / avif 14 文件）—— `enc_heif.cpp:745-751`
     显式传 `output_nclx_profile` 后无 ICC 输出新增 colr/nclx 盒。产物取证（`nclxscan.py` 实跑）：
     `rgb8.heic` box 树 ipco 内 `colr off=328 size=19`，payload `0001 000d 0006 8000`
     （CICP prim=1 trc=13 mat=6 full=1 = sRGB）；+19B 盒 +1B ipma 属性关联项 = 20B，
     与日志 bytes 增量逐字吻合。
  2. `in-place row bytes=+20` × 31 对（同源级联）。
  3. `run-summary bytes=+340`（full-heif = 17×20）/ `+280`（full-avif = 14×20）× 2 对。
  4. `in-place summary` 同源级联 × 2 对。
- 未变侧互证：11 个 ICC 嵌入输出字节不变（heif: gray8/gray16/gray16.tif/graya8/jxl8/jxl_exif；
  avif: gray8/gray16/gray16.tif/jxl8/jxl_exif），其产物是 `colr:prof(+588/504B)` 且无 nclx。
  其余 10 sections（full-jpeg/jxl/png/tiff/webp/bmp + meta-jpeg/png/tiff/webp）零漂移。
- 已知预期中的另两类实测 **0 行**：D1/D3 新告警与错误文案（`cmyk.tif` 仍逐字
  `probe failed: CMYK input is not supported`；语料无 HDR transfer 输入）；R-076 状态呈现。
- 版本行变更 0：`--update attrib: version-line changes=0 behavior changes=66 changed-lines=132`。
- 重播种后复跑：`regression: zero diff (baseline reproduced)`（2448 → 2448 lines，132 changed）。

## ③ 结论与判据

- 重播种符合 R-015 纪律（先逐行归因、后落盘）；132 行全部归 A1 nclx 精确信令（任务明示接受的
  字节变化），无第五类。
- 上下文：M7 窗口内回归基线共 **4 次**重播种——`d882fe6`（heif profile info 日志面 46 行 /
  产物字节 0）、`59de09d`（机械标签迁移 180 + R-162 消双记 4）、`97cd9da`（0.3.3 版本行 12 /
  行为 0）、`09b5340`（本条 132 行）。「两次重播种」仅在 M7-GATE scope 内成立。
