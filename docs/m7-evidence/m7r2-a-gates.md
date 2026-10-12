# m7r2-a-gates —— R2A「色彩能力与解码语义」关键门禁读数

> 包：R2A（提交 `c540630`）｜原始日志：`build/m7r2-a-*.log`、`build/m7r2-a/handoff.md`（gitignored）。
> 摘录人：R2F（2026-10-12，自 R2A handoff ④ 节逐字摘录）。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m7r2-a -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7r2-a -j 8
python tools/env.py run -- ctest --test-dir build/m7r2-a --output-on-failure
python tools/env.py run -- python tests/golden/smoke.py build/m7r2-a
python tools/env.py run -- python tools/regression.py build/m7r2-a --attrib-lines 4
python tools/env.py run -- python tools/regression.py          # release-dev 树自证
grep "/fp:precise" build/m7r2-a/build.ninja                    # R-302 自证
```

## ② 读数/结果摘录

| 命令 | 读数（逐字） |
|---|---|
| ctest 全量 | `100% tests passed out of 35`（build/m7r2-a-ctest-final.log） |
| 金样 | `SMOKE total=20 pass=20 fail=0`（冻结行逐字） |
| `pp_test_color.exe` | `1351 checks, 0 failed`（C1 护栏/C3/ΔE 扩样/跨库对拍/A1 位流 VUI/B3 警示） |
| `pp_test_simd.exe` | OK（`test_quantize8_dithered`：ref/avx2/分派/强制标量逐位、确定性、±0.5 LSB） |
| `pp_test_decode.exe` | `642 checks, 0 failure(s)`（D1 CMYK/YCCK 合成夹具拒收、D2 spec/文件/端到端、R-163 归类） |
| `pp_test_thumbs.exe` | OK（C4 线性光判别 + 默认路径逐字） |
| 回归归因（对 build/m7r2-a） | 仅 heif/avif 输出行 **bytes+20**（31 行），无增删行、其余格式零漂移 —— A1 nclx 精确信令 |
| 回归（release-dev 树） | zero diff（基线复现） |
| R-302 构建自证 | `grep "/fp:precise" build/m7r2-a/build.ninja` 命中（kernels/resample TU） |
| C1 护栏实测 | `gamut ramp=full-sat |dH'|(max clip=17.29 knee=13.30 conv=0.013 …)`；`gamut ramp=moderate (max clip=13.15 knee=7.34 conv=0.006; mean clip=3.40 knee=1.16 conv=0.002)` |
| A1 位流判据 | `bitstream VUI primaries=12 transfer=13 matrix=6`（P3=12、sRGB=1；解码侧 nclx 回填 P3=2 仅对照，不作判据） |

## ③ 结论与判据

- 全部判据过：ctest 35/35、金样 20/20（冻结行逐字）、两组单测 1351+642 checks 零失败。
- **唯一预期字节变化** = UX-14 A1 nclx 精确信令带来的 heif/avif +20B/文件（31 行归因，
  任务明示接受）；已由 R2-gate 逐行归因重播种（`m7r2-gate-regression-reseed.md`）。
- 如实记录：C1 mode1 在满饱和极远越界点为 0.77×/0.82×（非 0.5×，knee=0.85 公式定稿所致，
  断言按实测定标）；C3 各意图在矩阵类 profile 上像素差异 0（构造同数学，判据取缓存键分立）；
  heif/avif 8bit 量化路径（quantize16 面）未接抖动（任务面只接四家 to_u8）。
