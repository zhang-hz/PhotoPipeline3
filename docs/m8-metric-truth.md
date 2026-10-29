# M8-W4：评分真实性核验 + HEIF 444/420 深度诊断

> 状态：**v1（2026-10-29）**，W4 任务交付（Lead 可整段引用进 0.3.6.alpha1 发行说明/报告）。
> 用户原话：「HEIF 444 与 420 编码的评价结果基本一致，怀疑要么 444 没生效，要么评价系统有问题」+
> 「深度检查，确保编码和评价都是正确的，尤其是评价，禁止默认造假（比如用 420 量化后结果输入，这没有意义）」。
> 本文回答：**444 生效吗、评分真实吗、用户看到的「接近」是什么**，并给出可复跑的独立 oracle 证据。
> 相关契约：`docs/m8-encoder-params-and-ssimulacra2-design.md` §5（C9–C13）、`src/core/metric/metric.h`。
> 证据目录（gitignored，可复跑）：`build/evidence-w4/`。

---

## 1. 结论（发行说明口径）

### 1.1 对用户问题的直接回答

| 问题 | 结论 | 关键证据 |
|---|---|---|
| 4:4:4 真的生效了吗？ | **是，已写进码流并在解码像素上可见** | 产物 `hvcC` 的 `chromaFormat` 实测 444→**3**、422→**2**、420→**1**（逐一对上请求值）；解码后 444 vs 420 逐像素不同（dY=2.64、dCb=4.06、dCr=3.38 灰阶）；同源同 quality 下 444=71.55 / 420=57.41（彩色合成图，差 14.14 分） |
| 评价系统有问题吗？ | **没有。评分真实，参考=全分辨率编码器输入（不是 420 结果）** | 独立 oracle（`--score-pair`：源文件 vs 产物，文件级重新解码 + 同一 metric 核心）在 **11 个格式/配置上逐位复现 sidecar 分数，全部 delta=0.0000**；伪参考反证：把参考也做 4:2:0 后 420 产物从 57.41 被抬到 **75.18**（比真 444 还高）——真实管线没有发生这种抬高 |
| 为什么用户看到「444 与 420 基本一致」？ | **内容与质量档位使然，属指标真实敏感度** | ① 灰度内容（r=g=b）没有色度可损 → 两产物字节/分辨率不同但**分数逐位相同**（98.9212497104806）；② 自然照片（super/T1.tif，22.8MP）在 quality=90 时 444=77.2848 / 420=77.4966（差 0.21，420 略高——**编码器码率分配**，见 §4）；③ 色度极端内容差距才大（14 分） |

### 1.2 一句话（可直接进 CHANGELOG/发行说明）

> HEIF 4:4:4 编码确认真实生效（码流 `chromaFormat=3`），SSIMULACRA 2 评分经独立文件级 oracle 复核为真实（11 个配置与 sidecar 逐位一致）；用户观察到的「444/420 分数接近」是**指标对色度降采样的真实敏感度与内容相关**的结果（灰度内容必然相同；自然照片低色度细节下只有 ~0.2 分差且由 x265 码率分配造成），**不是 444 未生效、也不是评分造假**。

---

## 2. 独立 oracle：`--dev --score-pair`（新增 dev-only 工具）

### 2.1 它验证什么 / 不验证什么

- 验证链：**参考来源 + 两侧归一化 + 指标调用**。参考侧不再从管线工作缓冲派生，而是**从磁盘上的原始源文件重新解码**（`probe_file + decode_float`，与管线失真侧同一读入口），可选按目标位深量化（`--ref-bps`，与 C9 同一量化/反归一化口径，含 10/12-bit 位复制扩张），两侧用**同一份**色彩描述（默认取 REF 文件 ICC，`--icc` 可强制）经同一 `ColorManager` → sRGB + 精确 sRGB EOTF，送**同一** `pp::metric::compute()`。
- 不验证：指标算法本身（那由 C1 的 PyPI/libjxl 官方 oracle 门禁覆盖，本次未重跑）；也不覆盖 alpha 输入（管线评分前已拍平）与非 `keep` 彩色目标（P3/AdobeRGB 需 `--icc` 传入目标 ICC，未测试）。
- 实现：`src/dev_cli.cpp` 内新增（`PP_BUILD_DEV` 门控，**未改 CMakeLists.txt**、未改冻结接口）。
- 判据：若管线「参考被 420 化 / 参考取自产物」，oracle（真实源文件为参考）与 sidecar 会显著分歧；实测 11 个配置 delta 全 0 ⇒ 参考来源真实。

### 2.2 复跑

```bash
# 1) 构建（dev 树；只动 dev_cli.cpp）
python tools/env.py run -- cmake --build build/w4-metric --target photopipeline

# 2) 单对：源文件 vs 产物；--ref-bps 8 = 参考侧量化到 8-bit 网格（=编码器输入）
build/w4-metric/photopipeline.exe --dev --score-pair build/tmp-m8fix/color.png \
    build/evidence-w4/oracle/heif-444/color.heic --ref-bps 8
# → SCORE_PAIR state=ok score=71.5536339092 ref_bps=8 width=256 height=256 ... icc=ref-file

# 3) 全量对照表 + HEIF/T1/灰度取证（脚本 = build/evidence-w4/metric_oracle.py，gitignored）
C:\Python314\python.exe build/evidence-w4/metric_oracle.py oracle   # 8 格式 11 配置对照
C:\Python314\python.exe build/evidence-w4/metric_oracle.py heif     # 444/422/420 码流+像素+伪参考反证
C:\Python314\python.exe build/evidence-w4/metric_oracle.py gray     # 灰度内容
C:\Python314\python.exe build/evidence-w4/metric_oracle.py t1 --fast# 真实照片
```

---

## 3. 证据表 A：11 个格式/配置「解码产物 vs 编码器输入」独立 oracle 对照

输入 `build/tmp-m8fix/color.png`（256×256 RGB 8-bit，无 ICC，红蓝高频棋盘+渐变）；参考=原始源文件。

| case | format | out_bitdepth | sidecar（管线） | oracle（独立重算） | delta | 产物字节 |
|---|---|---|---|---|---|---|
| jpeg | jpeg | 8 | 87.15738955957548 | 87.1573895596 | +0.0000 | 30293 |
| jxl | jxl | 16 | 91.76845917108632 | 91.7684591711 | +0.0000 | 28958 |
| jxl-lossless | jxl `--lossless` | 16 | 100 | 100.0000000000 | +0.0000 | 9998 |
| webp | webp | 8 | 66.12693836226777 | 66.1269383623 | +0.0000 | 23046 |
| webp-lossless | webp `--lossless` | 8 | 100 | 100.0000000000 | +0.0000 | 2562 |
| heif-444 | heif `chroma=444` | 8 | 71.5536339091692 | 71.5536339092 | +0.0000 | 12603 |
| heif-420 | heif `chroma=420` | 8 | 57.409420161845254 | 57.4094201618 | −0.0000 | 13432 |
| avif | avif（SVT-AV1，10-bit） | 10 | 54.777478830840636 | 54.7774788308 | −0.0000 | 12205 |
| png | png | 16 | 100 | 100.0000000000 | +0.0000 | 8287 |
| tiff | tiff | 16 | 100 | 100.0000000000 | +0.0000 | 93104 |
| bmp | bmp | 24 (=8bps) | 100 | 100.0000000000 | +0.0000 | 196662 |

判据：无损组（jxl-lossless/webp-lossless/png/tiff/bmp）oracle 与 100.0 **逐位相等**；有损组 oracle 与 sidecar **最大 |Δ| = 0.0000**（≤1e-10，print 精度内）。
附注：oracle 首版曾因「参考侧 dequant 用除法而非管线用的乘 `1/255f`」自比 98.716（1 ULP 放大），修正后同图自比 = **100.0000000000**——oracle 自身可信度前提已钉住。

---

## 4. 证据表 B：HEIF 444/422/420 深诊断（彩色合成图）

| 请求 chroma | hvcC chromaFormat | 产物字节 | sidecar | oracle | luma MAE | Cb MAE | Cr MAE |
|---|---|---|---|---|---|---|---|
| 444 | **3**（4:4:4） | 12603 | 71.5536339091692 | 71.5536339092 | 2.5851 | 3.1691 | 2.9406 |
| 422 | **2**（4:2:2） | 12816 | 58.49550363756342 | 58.4955036376 | 2.2108 | 2.5504 | 2.3668 |
| 420 | **1**（4:2:0） | 13432 | 57.409420161845254 | 57.4094201618 | 2.2144 | 3.2756 | 2.7986 |

- **444 真写入码流**：`hvcC`（HEVCDecoderConfigurationRecord）中 `chromaFormat` 逐一对上 3/2/1；444 与 420 解码产物逐像素不同（rgb MAE 5.5261；dY=2.6435、dCb=4.0567、dCr=3.3788）。编码器侧参数生效（另见 W3 报告 R-306：Enum 字符串分派修复后 chroma 才真正到达 libheif/x265）。
- **评分随产物变化**：三个请求产生三个不同分数且 oracle 全部精确复现 → 评分不是常量/不随产物变化。
- **伪参考反证（用户担心的造假路径被排除）**：把「参考」也做 4:2:0 抽样往返（RGB→YCbCr→CbCr 2×2 盒下采样+最近邻上采样→RGB）后再打分：

| 参考来源 | vs 444 产物 | vs 420 产物 |
|---|---|---|
| 真实参考（原始源文件，全色度） | **71.55**（sidecar 71.5536） | **57.41**（sidecar 57.4094） |
| 伪参考（参考被 420 化） | 57.67（−13.88） | **75.18（+17.77，反而比真 444 还高）** |

⇒ 若管线真用「420 量化后的结果」当参考，用户会看到 420 打分被抬高到 75.18 这种**假高分**；实测管线没有此现象（oracle 用真实源文件逐位复现）。这正是「禁止默认造假」的机械化反证，已固化为单测 `fake-ref/*`。

---

## 5. 证据表 C：真实照片 super/T1.tif（3902×5854 16-bit + 内嵌 ICC）

quality=90（默认）HEIF 8-bit；参考=源文件（量化到 8-bit，`--icc super/T1.tif`）。

| 请求 chroma | 产物字节 | sidecar | oracle | delta | luma MAE | Cb MAE | Cr MAE | 色度高通能量比 |
|---|---|---|---|---|---|---|---|---|
| 444 | 1,911,896 | **77.28477194879144** | 77.2847719488 | +0.0000 | 1.9067 | 1.4245 | 1.2267 | 0.764 |
| 420 | 1,941,390 | **77.49658709625251** | 77.4965870963 | +0.0000 | **1.8880** | 1.5602 | 1.2644 | 0.683 |

**解读（如实）**：这张自然照片上 420 分数略高 **0.21 分**（复现 Lead 发现 77.28 vs 77.50），且 oracle 对两个分数都逐位复现 ⇒ **不是评分造假**，是真实产物差异：
- **420 的亮度误差更小**（luma MAE 1.8880 < 444 的 1.9067）：同 quality/CRF 下 x265 对 4:2:0 的色度平面用更少码率，省下的比特回到亮度平面 ⇒ 亮度质量略优；
- 420 的色度误差略大（Cb 1.5602 > 1.4245、色度高通能量保留率 0.683 < 0.764）；
- SSIMULACRA 2 的感知加权下，这张照片「亮度细节多、色度细节少」⇒ 亮度小幅受益 > 色度小幅受损，净效应 +0.21。**属编码器码率分配的合理结果，不是缺陷**。

quality 85/95 交叉（部分完成，如实登记）：
| 配置 | 分数 | 备注 |
|---|---|---|
| q90 444 | 77.2848 | 正常 |
| q90 420 | 77.4966 | 正常 |
| q95 420 | 91.0438 | 正常 |
| q95 444 | **unavailable（产物回读失败）** | 见 §7 未覆盖项：本机 OIIO/libheif 无法解码该 18.6MB 4:4:4 8-bit 产物（`Decoding the input data did not give a decompressed image`）；评分如实报 Unavailable（不伪造分数） |

⇒ 高 quality 时两者都逼近视觉无损（420=91.04），差距的来源从「编码损失」转为「编码器实现细节」，这也是用户在高质量档看到「基本一致」的另一原因。

---

## 6. 证据表 D：灰度内容（用户最可能看到「完全一致」的场景）

输入 `tests/golden/base/rgb8.png`（64×64，三通道逐像素相同，`is_gray=True`）：

| 请求 chroma | 产物字节 | 产物 SHA256[:16] | sidecar | oracle |
|---|---|---|---|---|
| 444 | 716 | 93fe1bad31488dbf | 98.9212497104806 | 98.9212497105 |
| 420 | 717 | 314b907422e606bc | 98.9212497104806 | 98.9212497105 |

- 产物字节/容器不同（716 vs 717）、SHA 不同，但**解码像素与分数逐位相同**（r=g=b 内容没有色度可抽样，420 与 444 的色度平面是常量）⇒ 444/420「完全一致」在此内容上是**正确且唯一可能**的结果，不是评分失效。
- 分数 98.92 而非 100：8-bit 有损 HEVC 的亮度量化/回读误差，属正常（C9 口径：参考=量化后输入，失真=解码产物）。

---

## 7. 机械化回归（新增单测，ctest 自动收编）

`tests/unit/test_metric_truth.cpp`（新文件，`tests/unit/test_*.cpp` glob 自动进 ctest，**未改 CMakeLists.txt**；断言只增不减）：

```bash
python tools/env.py run -- cmake --build build/w4-metric --target pp_test_metric_truth
python tools/env.py run -- ctest --test-dir build/w4-metric -R "metric|quality" --output-on-failure
# 2/2 Passed（test_metric_truth 0.29s / test_quality_metric 0.19s）
```

实测读数（build/w4-metric，2026-10-29）：

| 用例 | 断言 | 实测 |
|---|---|---|
| identity | 恒等 = 100.0（8×8/64×48/129×127） | 100.0 |
| ladder | 量化级 2→128 分数严格递增且均 <100 | −26.229151 → 9.523834 → 52.512604 → 82.771505 → 91.993234 → 95.520780 → 97.209874 |
| chroma420 | 色度极端图 + 4:2:0 复合色度失真 ⇒ <90 | **33.260807** |
| fake-ref | 真参考 420 失真 <90；假参考（参考也 420 化）=100.0；差 ≥5 | true=33.260807 / fake=100.0 / **inflation=66.739193** |
| direction | 参考/失真互换分数不等（官方 artifact/blur 项不对称） | forward=−35.104374 / reverse=−38.836412（Δ=3.732039） |
| determinism | 同输入两次分数逐位相同 | 相同 |

说明（如实，防止误解）：**纯色度降采样**在 SSIMULACRA 2 里扣分有限（本仓官方实现 C1 已证与 libjxl 官方 6/6 |Δ|=0，**不得为凑阈值改 metric**）；因此单测用「色度极端内容 + 4:2:0 抽样与色度粗量化」的复合失真来钉语义，真实 444/420 的分差（合成图 14 分）主要来自 420 码流里色度平面的量化+块效应。

全量 ctest 读数（build/w4-metric，40 项）：**38/40 passed**；失败 2 项均非 W4 文件：
`test_fsops`（W1 自己修，本轮 W1 已处理 `sddl.h` include 顺序后编译恢复，测试断言仍在 W1 手中）、
`ui_smoke`（UI 门禁按团队纪律只允许 ui-polisher/Lead 单点运行，且 W2 的 UI 改动不在本树的最新态，失败与本卡无关；
本卡相关两项 `test_metric_truth`（0.08s）与 `test_quality_metric` 均 **Passed**）。

---

## 8. 代码改动

| 文件 | 改动 | 性质 |
|---|---|---|
| `src/dev_cli.cpp` | 新增 dev-only `--score-pair REF DIST [--ref-bps N] [--icc FILE]`（独立 oracle：文件级 probe/decode → 参考位深量化 → 同一 ICC/sRGB 归一 → `pp::metric::compute`；输出单行 `SCORE_PAIR state=...`） | 加性；`PP_BUILD_DEV` 门控；不改 CMakeLists/冻结接口；默认路径零行为变化 |
| `tests/unit/test_metric_truth.cpp` | 新增（恒等/质量单调/色度抽样扣分/假参考反证/方向性/确定性） | 加性；ctest 自动收编 |
| `docs/m8-metric-truth.md` | 本文 | 新增 |
| `src/core/pipeline.cpp`、`src/core/metric/*`、`tools/pp_verify.cpp` | **零改动**（核验未发现评分/参考来源缺陷） | — |

---

## 9. 未覆盖 / 未决项（如实）

1. **q95 `chroma=444` 产物本机不可解码**（OIIO/libheif 报 `Decoding the input data did not give a decompressed image`；同 quality 的 420 可解、q90 的 444 可解）。评分侧行为正确（`unavailable`，不伪造分数）；是否为 x265/libheif/本机解码器（libde265 4:4:4 限制）缺陷需 W3/Lead 判定，本轮未修（`enc_heif.cpp` 属 W3）。
2. **固定 QP / tune 交叉验证未跑**（任务卡示例项）：本轮用 quality 90/95 + luma/chroma 分通道替代；如需更严格的「同亮度质量下 444≥420」结论，建议补 `--param x265:qp=` 或 `x265:tune=psnr` 交叉（属可选强化）。
3. **oracle 覆盖面**：仅 `color=keep`（无 ICC 源 + T1 有 ICC 源）；非 keep 彩色目标（P3/AdobeRGB）需 `--icc` 且未测；alpha 输入不支持（管线评分前已拍平）；10/12-bit 由 avif 10-bit 覆盖，HEIF 10/12-bit 未单独跑。
4. **平台**：仅 Windows 本机实测；Linux 行为未验证（同一 metric 核心/同一代码路径，风险低）。
5. **T1 quality=95 444 与 420 的对照不完整**（444 不可解码），高 quality 档的趋势只由 420 单点支撑。
