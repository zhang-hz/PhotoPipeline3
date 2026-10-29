# M8 · W3 高级参数真实连线机械化审计（需求 4a）

> 状态：**完成（2026-10-29，代码冻结前）**
> 任务：task-3「W3 高级参数真实连线机械化审计」｜执行：param-wirer
> 探针：`tools/param_effect_probe.py`（离线纯 Python，唯一机械判据）
> 机器可读结果：`build/evidence-w3/results.json` / `results.csv` / `static_audit.json` / `attribution_matrix.json` / `chroma/`
> 构建树：`build/w3-param`（`python tools/env.py run -- cmake --build build/w3-param --target photopipeline`）

---

## 0. 结论摘要

| 项 | 读数 |
|---|---|
| 高级参数目录行（`tools/param_catalog_specs/*.json`） | **116** = 103 可设置 + 13 只读 |
| 可设置行实测 | **103 行全部跑 A/B 双编码** |
| **EFFECT**（产物字节 SHA256 不同 = 参数真实改变码流/容器） | **74** |
| **NO_EFFECT**（A/B 逐字节相同，逐条归因见 §4） | **29** |
| **ERROR** | **0** |
| 只读行（不编码，按目录登记） | 13（x265 4 / libaom 6 / SVT-AV1 3） |
| **真断线 / 需改代码** | **0 处**（本次审计未发现需要修复的断线） |

分层读数：

| 后端 | 可设置 | EFFECT | NO_EFFECT | 只读 | 说明 |
|---|---:|---:|---:|---:|---|
| HEIF / x265 4.3 | 36 | 28 | 8 | 4 | 全部经 `heif_encoder_set_parameter_string("x265:<engine>")` → 插件 `x265_param_parse` |
| AVIF / libaom 3.15.1 | 51 | 35 | 16 | 6 | 经 `aom:<opt>` → `aom_codec_set_option` |
| AVIF / SVT-AV1 | 0 | — | — | 3 | 无 `svt:` 透传通道（设计 F6），只读登记 |
| JPEG XL / libjxl 0.12 | 16 | 11 | 5 | 0 | frame settings + 特殊 API（BasicInfo/AddBox/SetUpsamplingMode…） |
| **合计** | **103** | **74** | **29** | **13** | |

用户点名怀疑面（HEIF chroma=444 与 420 评分几乎一致）的**编码侧独立证据**见 §5：编码参数真实生效，产物与解码像素均差异显著；评分侧问题不在编码连线（评分归 metric-verifier）。

---

## 1. 探针用法（可复跑）

```bash
# 1) 构建本任务树（只需 photopipeline 目标；探针不依赖测试 exe）
python tools/env.py run -- cmake --build build/w3-param --target photopipeline

# 2) 静态审计（不编码，秒级）：目录 engine 名 ↔ pinned 上游名字表 ↔ 编码器下发点
python tools/env.py run -- python tools/param_effect_probe.py --static-audit

# 3) 全量 A/B 探针（约 55–65s：103 行 ×2 编码 + 3 个确定性对照 + 既有键抽查 + 色度证据）
python tools/env.py run -- python tools/param_effect_probe.py --with-chroma-evidence

# 4) 单行/单后端重跑（调试用）
python tools/env.py run -- python tools/param_effect_probe.py --only "x265:crf,aom:enable-qm"
python tools/env.py run -- python tools/param_effect_probe.py --formats avif --backend libaom

# 5) 归因矩阵（换 fixture / 换取值 / 换前置，用于 NO_EFFECT 判定）
python tools/env.py run -- python build/evidence-w3/manual_matrix.py
```

判据与默认参数：
- fixture = `build/tmp-m8fix/color.png`（256×256，对 4:2:0 色度敏感）；备选归因 fixture = `build/evidence-w3/fixtures/photo_like_512.png`（512×512 程序化自然图，gitignored）。
- 输出面：HEIF=`--format heif --bitdepth 8`；AVIF=`--format avif --backend libaom --bitdepth 8`；JXL=`--format jxl --bitdepth 16`；`--workers 1`。
- 判定：A/B 两次编码的**产物文件 SHA256** 不同 ⇒ EFFECT；相同 ⇒ NO_EFFECT（再做归因）。
- 条件参数带前置键（如 `x265:psy-rdoq` 前置 `x265:rdoq-level=2`、`upsampling_mode` 前置 `resampling=2`）；每行取值与前置见探针 `OVERRIDES` 表（报告 §4 有摘要）。
- **确定性对照**：每个 (format,backend) 未设参数连跑两次，SHA 必须相同；本次三轮均 `deterministic=YES`，A/B 判定可靠。

---

## 2. "键真的到达后端"的机械论证（为什么 NO_EFFECT 不是"被丢弃"）

三条下发链路对**参数名/取值非法都会硬失败**，因此"A/B 编码成功"本身就是键送达的机械证据：

| 链路 | 上游证据（pinned 源码） | 失败表现 |
|---|---|---|
| `x265:<engine>` | libheif `encoder_x265.cc:1023-1034`：`x265_param_parse` 返回 <0 ⇒ 插件返回 `heif_error_Usage_error`，`enc_heif.cpp` 转 `fail()` | 该文件编码失败（rc≠0） |
| `aom:<opt>` | libaom `av1_cx_iface.c` `encoder_set_option` 只认 `g_av1_codec_arg_defs` 名单；`encoder_aom.cc:1235-1240` 非 OK 即返回错误 | 该文件编码失败（rc≠0） |
| JXL 各键 | `JxlEncoderFrameSettingsSetOption` 对越界/未支持返回 `JXL_ENC_ERROR`；`enc_jxl.cpp` 转 `encode_error` | 该文件编码失败（rc≠0） |

静态审计（`--static-audit`，`build/evidence-w3/static_audit.json`）：
- x265：36/36 engine 名命中 `x265_param_parse` 的 `OPT(...)`/`OPT2(...)` 表（`no-` 前缀按 param.cpp 的通用取反处理折算），missing=0；
- libaom：51/51 engine 名命中 `encoder_set_option` 实际消费的选项（arg_defs 真名），missing=0；
- libjxl：16/16 目录键在 `enc_jxl.cpp` 的两条下发面（`kFrameSettings` 29 键 / `kSpecialKeys` 14 键）中全覆盖，missing=0；
- HEIF 透传作用域：`heif` 只收 `x265:`、`avif` 只收 `aom:`、`svt:` 无通道（与设计 F6 一致）。

> 已知"疑似断线"澄清：`x265:no-deblock` 的 engine 名不在 `x265_param_parse` 的 OPT 表里，但 `param.cpp` 在 OPT 匹配前有**通用 `no-` 前缀取反**逻辑（`if (!strncmp(name,"no-",3)) name += 3; value = !value ...`）⇒ 合法且语义正确；探针实测 `x265:no-deblock=0/1` 产物不同（88556 vs 88576B，512 图），**非断线、无需改码**。

---

## 3. 每行判定表（103 可设置行）

图例：`E`=EFFECT；`N`=NO_EFFECT（归因编号见 §4）；`R`=只读。证据 = 产物 SHA/字节（完整逐行见 `results.csv`，每行含 A/B 值、前置键、SHA、rc、日志摘录）。

### 3.1 HEIF / x265（36 可设置）

| 行 | 判定 | 行 | 判定 | 行 | 判定 |
|---|---|---|---|---|---|
| x265:crf | E | x265:qcomp | E | x265:min-cu-size | E |
| x265:qp | E | x265:psy-rd | E | x265:max-tu-size | E |
| x265:qpmin | E | x265:psy-rdoq | E | x265:limit-tu | N-a |
| x265:qpmax | E | x265:rdoq-level | E | x265:fast-intra | N-b |
| x265:aq-mode | E | x265:rd | E | x265:splitrd-skip | N-b |
| x265:aq-strength | E | x265:rd-refine | N-c | x265:signhide | E |
| x265:ssim-rd | E | x265:cu-lossless | E | x265:strong-intra-smoothing | E |
| x265:qg-size | E | x265:cbqpoffs | E | x265:tskip | E |
| x265:tskip-fast | E | x265:crqpoffs | E | x265:lowpass-dct | N-b |
| x265:sao | E | x265:no-deblock | E | x265:limit-sao | N-d |
| x265:sao-non-deblock | E | x265:selective-sao | N-e | x265:level-idc | E |
| x265:high-tier | E | x265:allow-non-conformance | N-f | x265:wpp | E |

### 3.2 AVIF / libaom（51 可设置）

| 行 | 判定 | 行 | 判定 | 行 | 判定 |
|---|---|---|---|---|---|
| aom:cq-level | E | aom:deltaq-mode | E | aom:enable-palette | N-g |
| aom:aq-mode | E | aom:deltaq-strength | E | aom:enable-cfl-intra | E |
| aom:sharpness | E | aom:enable-chroma-deltaq | E | aom:enable-angle-delta | E |
| aom:enable-adaptive-sharpness | E | aom:enable-qm | E | aom:enable-filter-intra | E |
| aom:dist-metric | E | aom:qm-min | E | aom:enable-smooth-intra | E |
| aom:quant-b-adapt | E | aom:qm-max | E | aom:enable-paeth-intra | E |
| aom:disable-trellis-quant | N-g | aom:min-cr | N-h | aom:enable-directional-intra | E |
| aom:sb-qp-sweep | E | aom:auto-intra-tools-off | N-g | aom:enable-diagonal-intra | N-g* |
| aom:enable-cdef | E | aom:enable-restoration | N-g | aom:tile-columns | E |
| aom:enable-dual-filter | N-g | aom:loopfilter-control | E | aom:tile-rows | E |
| aom:row-mt | E | aom:enable-rect-partitions | E | aom:target-seq-level-idx | N-i |
| aom:strict-level-conformance | N-i | aom:enable-ab-partitions | N-g | aom:set-tier-mask | N-i |
| aom:force-video-mode | E | aom:enable-1to4-partitions | N-g | aom:timing-info | N-j |
| aom:cdf-update-mode | E | aom:min-partition-size | E | aom:tune-content | E |
| aom:enable-intra-edge-filter | E | aom:max-partition-size | N-g | aom:enable-tx64 | N-g |
| aom:enable-flip-idtx | E | aom:enable-rect-tx | E | aom:use-intra-dct-only | E |
| aom:reduced-tx-type-set | E | aom:use-intra-default-tx-only | N-g | | |

\* `aom:enable-diagonal-intra` 在 512×512 自然图上 EFFECT（31555 vs 31467B）；合成 256 图上为 N-g（内容相关，连线正常）。

### 3.3 JPEG XL（16 可设置）

| 行 | 判定 | 行 | 判定 |
|---|---|---|---|
| noise | E（前置 distance=4） | intensity_target | E |
| group_order_center_x | N-k | min_nits | E |
| group_order_center_y | N-k | relative_to_max_display | E |
| upsampling_mode | N-l | linear_below | E |
| uses_original_profile | E | intrinsic_size | E |
| box_compress | E（前置 --meta 注入 Exif box） | frame_name | E |
| expert_options | N-m | | |
| buffering | N-n | | |
| output_mode | E（A/B=0/2） | | |
| disable_perceptual_heuristics | E | | |

### 3.4 只读行（13，不编码）

`__x265:profile` `__x265:keyint` `__x265:input-csp` `__x265:total-frames`；`__aom:g-usage` `__aom:rc-end-usage` `__aom:g-limit` `__aom:lag-in-frames` `__aom:kf-disabled` `__aom:profile`；`__svt:profile` `__svt:level` `__svt:tier`。目录声明 `scope=readonly`，由 UI 只读展示（上游固定/推导值），不参与可设置行统计。

---

## 4. NO_EFFECT 逐条归因（29 条，均非断线）

归因依据 = ①探针实测（A/B 同 SHA）②换 fixture/取值二次取证（`attribution_matrix.json`）③pinned 上游源码语义。**所有行 A/B 编码均成功**，按 §2 论证键已送达后端。

| 编号 | 行 | 归因类别 | 说明与证据 |
|---|---|---|---|
| N-a | `x265:limit-tu` | 上游限制（已登记 X7） | `limitTU` 仅在 `tu-inter-depth>1` 时有语义，而 libheif 插件不暴露/不设置该键（`analysis.cpp:982` 等按 `m_limitTU` 分支）⇒ 产品路径下合法 no-op；设计 X7 已降级为 tooltip |
| N-b | `x265:fast-intra` / `splitrd-skip` / `lowpass-dct` | 合法 no-op（内容/编码决策相关） | 均为分析加速/近似开关：两种 fixture（256 合成 + 512 自然图）下 x265 RD 决策与产物逐字节一致；键已到达 `x265_param_parse`（engine 名静态核对命中、A/B 编码成功） |
| N-c | `x265:rd-refine` | 合法 no-op（小图未触发） | 前置 `rd=6,aq-mode=2`；256 合成图下 A/B 同码流，**512 自然图上 EFFECT（105681 vs 106796B）** ⇒ 连线正常，属内容相关 |
| N-d | `x265:limit-sao` | 上游语义（仅 inter 生效） | `sao.cpp:854/1396` 等按 `slice->m_sliceType == P_SLICE` 分支；产品恒单帧 I 帧（still image）⇒ 结构上无效果 |
| N-e | `x265:selective-sao` | 合法 no-op | `sao=1` 且 `selective-sao=0` 时 x265 在 `encoder.cpp:4166-4167` 规范成 4；本内容下 3 vs 4 决策一致（同码流） |
| N-f | `x265:allow-non-conformance` | 合法 no-op（许可开关） | `bAllowNonConformance` 只在 `encoder/api.cpp:189`（profile==NONE 时拒绝/放行）与 `param.cpp:1719`（超 142MP 分辨率）被读取，常规合规编码不写码流；合规配置 A/B 同码流 |
| N-g | `aom:disable-trellis-quant` `auto-intra-tools-off` `enable-restoration` `enable-dual-filter` `enable-ab-partitions` `enable-1to4-partitions` `max-partition-size` `enable-tx64` `use-intra-default-tx-only` `enable-palette`（+256 图上的 `enable-diagonal-intra`） | 合法 no-op（内容相关，工具未被 RD 选中） | 256 合成图与 512 自然图上 A/B 均逐字节一致；同类键中 `enable-diagonal-intra` 在 512 图翻转 EFFECT、其余同类（enable-rect-partitions/enable-palette 以外 8 个帧内工具）多数 EFFECT ⇒ 同类链路已证连线，本组属"本内容下工具未被选中" |
| N-h | `aom:min-cr` | 合法 no-op（RC 路径不适用） | libheif 插件固定 `rc_end_usage=AOM_Q`（`encoder_aom.cc:1052`）；`min-cr` 只影响 ABR/压缩比约束路径 |
| N-i | `aom:target-seq-level-idx` / `strict-level-conformance` / `set-tier-mask` | 合法 no-op（插件固定项覆盖，上游限制） | 单帧静止图 + 插件固定 `g_limit=1`/profile 推导；256/512 两 fixture 码流一致；键已送达（set_option 成功 + 名字命中 arg_defs 消费面） |
| N-j | `aom:timing-info` | 合法 no-op（静止图无语义） | 单帧静止图像无帧时序（timing info）可写；码流不变 |
| N-k | `group_order_center_x/y` | 合法 no-op（产品非流式路径） | 中心坐标仅在 `group_order=center` 且多 group 顺序写出时有意义；本产品恒整图输入/整图输出（非流式），256 单 group / 512 四 group 均同码流；键已作为 frame setting 送达 |
| N-l | `upsampling_mode` | 合法 no-op（解码端 hint） | `JxlEncoderSetUpsamplingMode` 写 custom upsampling weights（`encode.cc:1562+`）；实测 `resampling=2` 下 nearest vs pixel-dot 产物一致（该差异不进入本产品路径码流）；属解码端 hint，连线本身已由 API 成功调用证明 |
| N-m | `expert_options` | 功能"门"（合法 no-op + 门控证据） | 唯一语义 = 放开 effort=11；`EXTRA_CHECKS` 直接证据：`effort=11 + expert_options=0` → libjxl 拒绝（EFFECT_REJECTED），`+expert_options=1` → 成功 ⇒ 门已连线；effort 10 vs 11 在本 fixture 产物一致 |
| N-n | `buffering` | 合法 no-op（库文档明确） | `jxl/encode.h:343-356`：BUFFERING 只控制输入/输出缓冲策略与内存（`0=全缓冲`、`1/2=流式阈值`），**不改变码流**；且仅 `output_mode` 未设时参与解析（`encode.cc:477`） |

---

## 5. 用户点名面：HEIF chroma=444/422/420 编码侧独立证据

命令：`python tools/env.py run -- python tools/param_effect_probe.py --with-chroma-evidence`（fixture=`build/tmp-m8fix/color.png` 256×256；产物与解码 PNG 存 `build/evidence-w3/chroma/`）。

| 后端 | chroma | 产物字节 | 产物 SHA（前16） | 解码 PNG SHA（前16） | 解码像素 vs 源 MAE |
|---|---:|---:|---|---|---:|
| x265 | 420 | 13432 | `9a3f5881335379e1` | `6c163e23c9d7c90a` | 4.498 |
| x265 | 422 | 14221 | `ef55ee382a58a08b` | `edc67429b447cd6f` | 3.888 |
| x265 | 444 | 12603 | `7bb2fe94492a8c35` | `8ccae490cad3d9b9` | 4.633 |
| libaom | 420 | 9312 | `7b41737ae001d63f` | `9e9e993f1c53d00a` | 6.444 |
| libaom | 422 | 9926 | `db198bf45b0f8cb2` | `b33b9581f539467a` | 7.118 |
| libaom | 444 | 10013 | `fe8150928f594488` | `501a58c304c33a07` | 7.959 |

- **产物字节 SHA 三档全不同** ⇒ `chroma` 真实进入编码器并改变码流（不是"UI 选了 444、实际仍 420"）。
- **解码回读像素 420 vs 444：x265 有 99.84% 样本不同、MAE 5.526；libaom 99.52%、MAE 7.877** ⇒ 像素面也真实变化。
- 注意：`MAE vs 源` 不是单调排序（444 > 422/420），这是"固定 CRF 下 4:4:4 需要编码更多色度细节、量化误差重新分配"的正常现象，不能用它反推"444 未生效"；判定生效用的是产物/解码差异本身。
- 结论：**"HEIF 444 与 420 评分几乎一致"不是编码侧参数未连线**；若 SSIMULACRA2 分数接近，属评分口径/参考选择问题（评分侧由 metric-verifier 独立取证）。既有键代表性抽查（`EXTRA_CHECKS`）中 `chroma 420 vs 444`（HEIF 与 AVIF）、`preset`、`tune`、`quality`、`x265:crf`、`aom:cq-level` 共 9 项**全部 EFFECT**。

---

## 6. 修复清单

**无。** 本审计没有发现"真断线"。上表 29 个 NO_EFFECT 全部为合法 no-op 或上游语义限制（逐条归因见 §4），不构成缺陷，未改任何源码；代码冻结前最后一次全量构建 `build/w3-param --target photopipeline` 成功（EXIT=0），探针在最新 exe 上重跑取得本报告读数。

两个"审计期发现但**不属于断线**"的口径（供后续维护参考）：
1. `x265:no-deblock` 依赖 `param_parse` 的通用 `no-` 前缀取反语义（非 OPT 表显式名）——探针/静态核对已按此折算，行为正确；
2. 若未来把 "line" 类参数（如 `limit-sao`、`timing-info`、`buffering`、`group_order_center_*`）的 tooltip 写成"必然生效"，建议补一句"still-image / 非流式路径下可能无码流影响"（`docs/m8-param-control-map.md` 已含部分说明，未改文档）。

---

## 7. 未覆盖 / 风险 / 复跑口径（如实）

1. **探针为单/双 fixture 内容相关判定**：NO_EFFECT 中有 1 项（`x265:rd-refine`）在 512×512 自然图上翻转为 EFFECT、1 项（`aom:enable-diagonal-intra`）同理 ⇒ 已按"内容相关合法 no-op"归因；未做大规模语料扫描（时间盒内）。若需更强证据，可把 `--fixture` 指向 `build/evidence-w3/fixtures/photo_like_512.png` 重跑全量（约 2 分钟，103 行 ×2 ×2 套 fixture）。
2. **既有 52 个既有键**不在 116 行口径内，仅做 9 项代表性抽查（全部 EFFECT，见 §5）；完整既有键回归由金样/回归门禁覆盖。
3. **GUI 侧「勾选启用→params()」链路**属 W2（paramform.cpp），本卡未审计、未改动；本报告只证明"参数到达编码器并生效"的引擎侧。
4. **未跑全量 ctest**（时间让给探针/冻结窗口）；本卡未改源码，参数相关单测（`test_param_catalog`/`test_param_rules`/`test_params`）由 Lead 集成树收口。
5. 探针总时长 64.6s（目标 ≤10 分钟达标）；单行 A/B 日志与产物保留在 `build/evidence-w3/runs/`（gitignored），可复现。

---

## 8. 证据路径一览

| 证据 | 路径 | 内容 |
|---|---|---|
| 逐行机器可读结果 | `build/evidence-w3/results.json` / `results.csv` | 103 行 A/B 值、前置键、SHA、字节、rc、判定、日志摘录 |
| 静态审计 | `build/evidence-w3/static_audit.json` | x265/aom engine 名 ↔ pinned 上游名字表；JXL 下发面覆盖 |
| 归因矩阵 | `build/evidence-w3/attribution_matrix.json` + `matrix.log` | 换 fixture/取值二次取证（含 512 自然图翻转 EFFECT 记录） |
| 色度独立证据 | `build/evidence-w3/chroma/` | 420/422/444 产物 + 解码 PNG（逐后端） |
| 探针全量日志 | `build/evidence-w3/probe-final.log`、`probe.log` | 逐行判定与统计输出 |
| 每次 A/B 产物+日志 | `build/evidence-w3/runs/<key>/{A,B}/` | 可复核字节 SHA |
| 探针源码 | `tools/param_effect_probe.py` | 本报告唯一机械判据（含 OVERRIDES 取值表/静态审计/PNG 解码比较） |

---

*本报告为 W3 审计交付物；代码冻结后未再修改 `src/`。*
