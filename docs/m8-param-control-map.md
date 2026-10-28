# M8 控件形态与联动分级（补充设计）

> 状态：**补充设计 v1（2026-10-28）**，随设计 v4 一起评审；确认后冻结为 M8 契约的一部分。
> 关联：设计 `docs/m8-encoder-params-and-ssimulacra2-design.md` §4.6｜参数范围 `docs/m8-param-exposure-review.md`｜执行 `docs/m8-workflow.md`（A0/A1–A3/B2）
> 目的：回答“哪些参数是模式切换、哪些是数值、哪些是下拉”，并把“谁控制谁”落成**可机械求值**的联动矩阵（UI 实时 / 运行前预检 / 逐文件 / 编码器兜底）。

---

## 0. 结论摘要

1. **真正的路线锚点只有 6 类可编辑**：输出后端（AVIF libaom/SVT）、JXL 技术（VarDCT/Modular）、无损（lossless）、速率控制模式（RC 模式）、libaom realtime、JXL 码流输出模式（buffering/output_mode）；另有 5 类**档位/属性锚点**（preset、tune、位深、色度、profile/level/tier）。
2. 其余参数只有三种形态：**数值（N）**、**下拉（D）**、**开关（S）**；另有文本 1 项（`frame_name`）、只读 13 项（x265 4 + libaom 6 + SVT 3）。
3. **RC 模式采用显式模式下拉（已定，用户 2026-10-28 裁定）**：HEIF 质量/CRF/QP、libaom 质量/CQ Level、SVT 质量/QP；只显示当前路径数值行，其余隐藏并禁止下发；默认“质量”= 既有 `quality`（零行为变化），无损时整组隐藏。
4. **新增布尔行用三态下拉（已定）** `{跟随默认 / 开 / 关}`，避免“启用勾选 + 开关”双复选框；既有布尔行保持复选框不变。
5. **计数双口径（已定）**：评审稿 ≈40 / ≈50 / 10 / ≈42 是**组行**口径（用于范围评审与报告）；**键展开 170 行是实现/目录/测试/门禁的唯一真源**。范围不变，精确数以 Stage A4 的“评审清单 ↔ 目录行”对账表为准。

---

## 1. 分类口径

### 1.1 两个维度

| 维度 | 取值 | 用途 |
|---|---|---|
| **形态（Form）** | M 模式切换 / N 数值 / D 下拉 / S 开关 / T 文本 / R 只读 | 决定画什么控件、怎么校验 |
| **联动角色（Role）** | A 锚点（驱动别人）/ P 依赖（被别人驱动）/ I 独立 | 决定规则引擎里谁是 driver、谁是 dependent |

> 一个控件可以“形态是下拉、角色是锚点”（如 `preset`）；也可以“形态是开关、角色是锚点”（如 `lossless`）。两个维度都不省略。

### 1.2 形态定义

| 代号 | 形态 | 渲染 | 适用 |
|---|---|---|---|
| **M** | 模式切换 | 互斥单选/下拉，或开关 | 改变编码路线/参数可用集：后端、技术、无损、RC 模式、realtime、JXL 码流输出模式 |
| **N** | 数值 | SpinBox（Int/Float） | 连续/离散数值、阈值、强度、比例 |
| **D** | 下拉 | ComboBox（固定候选） | 枚举档位、level/tier、tile 对数、分区尺寸等 |
| **S** | 开关 | CheckBox（既有）或三态下拉（新增） | 单个编码工具的二元开关 |
| **T** | 文本 | LineEdit | `frame_name`（新 `ParamType::String`） |
| **R** | 只读 | 只读行/标签 | 上游/位深/色度固定值、derive 值 |

### 1.3 角色定义

| 代号 | 角色 | 含义 | 典型 |
|---|---|---|---|
| **A** | 锚点 | 切换后改变其他参数的可见性/可设置性/语义 | 后端、技术、`lossless`、RC 模式、`preset`、`tune`、`chroma`、`bitdepth` |
| **P** | 依赖 | 只在某锚点/前驱成立时可见、可设、有意义 | `psy-rdoq` 依赖 `rdoq-level>0`；`upsampling_mode` 依赖 `resampling>1` |
| **I** | 独立 | 不控制别人，也不被（除全局无损/位深/色度外）控制 | `no-deblock`、`allow-non-conformance`、`tune-content` |

### 1.4 与现有控件的关系（零回归约束）

- **既有 7（HEIF）/ 10（AVIF）/ 28（JXL）键的控件形态不变**：
  - HEIF/AVIF 由运行时内省决定：区间整数 → N、整数集合/字符串集合 → D、布尔 → S；
  - JXL 由 `format_tables.cpp` 静态表决定：现有 `resampling`/`group_order`/`color_transform` 是 D，其余 Int/Float 是 N、Bool 是 S。
- M8 只对**新增目录行**引入“启用/跟随默认”三态与控件形态元数据；既有行的默认值、可见性谓词、锁定谓词**逐字不动**。
- 新增形态元数据放在 `CatalogRow`（`Control`/`Role` 字段），**不放 `ParamDef`**，避免冻结面扩大；UI 对新增行接收可选目录描述，既有行继续按 `ParamDef.type` 推导。

---

## 2. 跨格式模式锚点（最高优先级）

### 2.1 路线锚点（可编辑）

| # | 锚点 | 形态 | 取值 | 作用域 | 主导联动 |
|---|---|---|---|---|---|
| **G1** | 输出格式 / 后端 | M（已有控件） | HEIF=x265；AVIF=libaom/SVT；JXL=libjxl | 页面级 | 切换整个参数面；跨后端键（`svt:` 等）= 预检错误 |
| **G2** | JXL 技术 | M（已有控件） | VarDCT / Modular | JXL | 切换参数集：VarDCT-only 与 Modular-only 互斥（现有 `tech` 过滤） |
| **G3** | 无损 | M（开关） | `lossless` on/off | 每格式 | 见 G3.1–G3.4 |
| **G4** | RC 模式（已定显式下拉） | M（新增下拉） | 质量 / CRF / QP（libaom 为 质量 / CQ Level；SVT 为 质量 / QP） | HEIF / AVIF | 只显示当前路径的数值行；其余隐藏并禁止下发；无损时整组隐藏 |
| **G5** | realtime | M（开关） | on/off | libaom | 切换 usage → `speed`（cpu-used）域与并行行为变化 |
| **G6** | JXL 码流输出模式 | M | `buffering`（0/1/2）、`output_mode`（0/1/2） | JXL | 改变码流结构/缓冲；`output_mode=2` 加兼容性警告 |

### 2.2 档位/属性锚点

| # | 锚点 | 形态 | 取值 | 主导联动 |
|---|---|---|---|---|
| **G7** | preset | D（锚点） | x265 10 档 | 改变未显式覆盖参数的默认基线；显式参数仍覆盖 |
| **G8** | tune | D（锚点） | 各库枚举 | x265 psy/AQ 部分被插件覆盖；libaom `iq` 提示库自动改 QM/sharpness/chroma-deltaq；SVT `iq/ms-ssim` 提示 |
| **G9** | 位深 | M/D（页面级） | 8/10/12/16 | profile 推导、QP/CRF 域、JXL 16bit+level5 错误 |
| **G10** | 色度 | D（锚点） | 420/422/444 | profile 推导、x265 RExt、libaom identity 约束、SVT 固定 420 |
| **G11** | profile / level / tier | R / D / D | profile 只读；level/tier 可选 | 兼容性门槛；level 与 CRF/VBV 的警告 |

### 2.3 无损（G3）分后端展开

| 后端 | 联动动作（timing） |
|---|---|
| **HEIF/x265** | `quality` 隐藏（UI）；`chroma` 锁定 444（UI+ENC）；`keyint=1` 只读展示；RC 组（G4）隐藏；profile 变为 RExt 只读 + 警告（UI/PRE） |
| **AVIF/libaom** | `quality` 隐藏；`chroma` 锁定 444；`aom:aq-mode=0`、`aom:enable-chroma-deltaq=0` 锁定；`tune=iq` 禁用；警告“码流无损可能 <100”（UI/PRE） |
| **AVIF/SVT** | `lossless` 控件禁用 + tooltip 指路 libaom（产品当前硬失败，UI） |
| **JXL** | 强制技术 = Modular（UI）；`distance` 隐藏；`resampling` 锁定 1；`modular_lossy_palette` 锁定 false；VarDCT-only 参数隐藏；警告“真无损分数应为 100”（UI/ENC） |

---

## 3. HEIF / x265 参数形态表

> 新增行全部默认 **Unset**（不勾“启用”就不进 `ParamSet`）；RC 组由 G4 模式控件统一启用，不单独给每行勾选。联动列引用 §3.4 的 X 规则。

### 3.1 现有 7 项（形态不变）

| 键 | 形态 | 角色 | 类型/域 | 默认 | 联动 |
|---|---|---|---|---|---|
| `quality` | N | A（RC 默认路径） | int 0–100 | 50 | G4/X1；无损→隐藏（G3） |
| `lossless` | M | A | bool | false | G3 |
| `preset` | D | A | enum 10 档 | slow | G7/X14 |
| `tune` | D | A | enum psnr/ssim/grain/fastdecode | ssim | G8/X3（psy/AQ 被覆盖，警告） |
| `chroma` | D | A | enum 420/422/444 | 420 | G10/X13（444+psy 警告；profile 只读） |
| `tu-intra-depth` | N | P | int 1–4 | 2 | X7/X8（与 CTU 耦合，逐文件） |
| `complexity` | N | P | int 0–100 | 0（无默认） | X14（≥90 关 wpp） |

### 3.2 新增 36 项

#### A. 质量/码率控制（14）

| 键 | 形态 | 角色 | 类型/域 | 联动 |
|---|---|---|---|---|
| `x265:crf` | N | A（RC 路径） | float，8bit 0–51（随位深上抬） | G4/X1；与 quality/qp 互斥 |
| `x265:qp` | N | A（RC 路径） | int 0–51 | G4/X1 |
| `x265:qpmin` | N | P | int 0–69 | X1b（≤qpmax） |
| `x265:qpmax` | N | P | int 0–69 | X1b（≥qpmin） |
| `x265:aq-mode` | D | P | int 0–4（库值） | X2（插件强制 1，tooltip）；驱动 aq-strength/rd-refine |
| `x265:aq-strength` | N | P | float 0–3 | X2（aq-mode=0 时禁用） |
| `x265:qcomp` | N | I | float 0.5–1.0 | — |
| `x265:psy-rd` | N | P | float 0–5 | X5（444 且 >0 警告）；X6（ssim-rd 会清） |
| `x265:psy-rdoq` | N | P | float 0–50 | X3（rdoq-level=0 时禁用） |
| `x265:rdoq-level` | D | P | int 0–2 | X3；驱动 psy-rdoq |
| `x265:rd` | N | P | int 1–6 | X4（<3 禁 cu-lossless/tskip；rd-refine 需 ≥5） |
| `x265:rd-refine` | S | P | bool | X4（rd≥5 且 aq-mode≠0） |
| `x265:cu-lossless` | S | P | bool | X4（rd≥3） |
| `x265:ssim-rd` | S | P | bool | X6（清 psy-rd，警告/联动） |

#### B. 量化/分区/变换（13）

| 键 | 形态 | 角色 | 类型/域 | 联动 |
|---|---|---|---|---|
| `x265:qg-size` | D | P | enum 8/16/32/64 | X8（≤CTU，逐文件） |
| `x265:cbqpoffs` | N | I | int −12..12 | X13（444+psy 可能自动改，tooltip） |
| `x265:crqpoffs` | N | I | int −12..12 | X13（同上） |
| `x265:min-cu-size` | D | P | enum 8/16/32 | X9（>8 且宽高不整除 → 警告，逐文件） |
| `x265:max-tu-size` | D | P | enum 4/8/16/32 | X8（≤CTU） |
| `x265:limit-tu` | N | P | int 0–4 | X7（tu-intra-depth>1 才有意义） |
| `x265:fast-intra` | S | I | bool | —（rd≤4 提速，tooltip） |
| `x265:splitrd-skip` | S | I | bool | — |
| `x265:signhide` | S | I | bool | — |
| `x265:strong-intra-smoothing` | S | I | bool | — |
| `x265:tskip` / `x265:tskip-fast` | S | P | bool ×2 | X4（rd≥3；tskip-fast 依赖 tskip） |
| `x265:lowpass-dct` | S | I | bool | — |

#### C. 环路滤波 / 心理视觉（5）

| 键 | 形态 | 角色 | 类型/域 | 联动 |
|---|---|---|---|---|
| `x265:sao` | S | A | bool | X10/X11（sao=0 时禁用其下 3 项） |
| `x265:sao-non-deblock` | S | P | bool | X10/X11 |
| `x265:limit-sao` | S | P | bool | X10（sao=1 才有意义） |
| `x265:selective-sao` | D | P | int 0–4 | X10 |
| `x265:no-deblock` | S | I | bool | —（deblock 字符串不暴露） |

#### D. 码流 / 兼容性（4）

| 键 | 形态 | 角色 | 类型/域 | 联动 |
|---|---|---|---|---|
| `x265:level-idc` | D | A | enum 0=auto / 1.0–7.2 常用档 | X12（high-tier 依赖；level+CRF 警告） |
| `x265:high-tier` | S | P | bool | X12（level-idc>0 才有效） |
| `x265:allow-non-conformance` | S | I | bool | —（profile/level 可能变 NONE，tooltip） |
| `x265:wpp` | S | P | bool | X14（complexity≥90 会关 wpp；slices 未暴露） |

### 3.3 只读信息行（4，不计入可编辑）

`__x265:profile`（由位深/chroma 推导）、`__x265:keyint`（=1）、`__x265:input-csp`（由 chroma 推导）、`__x265:total-frames`（=1）。

### 3.4 x265 联动规则

| # | 规则 | 动作 | 时机 |
|---|---|---|---|
| X1 | RC 三选一：quality / crf / qp 只能有一个路径 | 其余 H/D；模式由 G4 控制 | UI + PRE（预设冲突报错） |
| X1b | `qpmin ≤ qpmax` | 违反即 E | PRE |
| X2 | `aq-strength` 仅 `aq-mode≠0`；`rd-refine` 需 `aq-mode≠0` | 依赖行 D | UI |
| X3 | `psy-rdoq` 仅 `rdoq-level>0` | D + 原因 | UI |
| X4 | `rd<3` ⇒ `cu-lossless`/`tskip`/`tskip-fast` D；`rd-refine` 需 `rd≥5`；`tskip-fast` 依赖 `tskip` | D/H | UI |
| X5 | `psy-rd>0` 且 `chroma=444` ⇒ 色度 QP 偏移（库行为） | W（tooltip） | UI |
| X6 | `ssim-rd=1` 会清 `psy-rd` | W + 联动 D（二选一） | UI |
| X7 | `tu-intra-depth>1` 才允许 `limit-tu`；二者与自适应 CTU 相关 | D；FILE 校验 | UI + FILE |
| X8 | `qg-size` / `max-tu-size` ≤ 自适应 CTU | 越界 W（库会夹逼）；FILE 校验 | FILE |
| X9 | `min-cu-size>8` 且宽高不整除 ⇒ padding/conformance window | W | FILE |
| X10 | `sao=0` ⇒ `sao-non-deblock`/`limit-sao`/`selective-sao` D | D | UI |
| X11 | `sao-non-deblock` 仅 `sao=1` | D | UI |
| X12 | `high-tier` 仅 `level-idc>0` 有效；`level-idc` + CRF ⇒ 强制 VBV | D；W | UI |
| X13 | `cbqpoffs/crqpoffs` 在 444+psy 下库可能自动改 | W（tooltip） | UI |
| X14 | `complexity≥90` 会关 wpp；`wpp` 与 `slices>1` 组合需一致 | W；预设/`--param` 中出现 `slices>1` 且不满足 ⇒ E | UI + PRE |
| X15 | `psy-rd` 等 psy 参数的部分效果被 `tune`/插件固定项覆盖 | W | UI |
| X16 | 清单外/禁止键（`annexb`/`input-csp`/`total-frames`/VUI/`ctu`/日志/CLI-ONLY/文件副作用/有害组） | E（中文定位） | PRE |
| X17 | 线程池 `pools`/`frame-threads` 不出现；预设/`--param` 出现 ⇒ E | E | PRE |

---

## 4. AVIF / libaom 参数形态表

### 4.1 现有 10 项（形态不变；`threads` 仍隐藏）

| 键 | 形态 | 角色 | 类型/域 | 联动 |
|---|---|---|---|---|
| `quality` | N | A（RC 默认） | int 0–100 | G4/A1；无损→隐藏 |
| `lossless` | M | A | bool | G3/A2 |
| `chroma` | D | A | enum 420/422/444 | G10/A6（identity 需 444） |
| `tune` | D | A | enum psnr/ssim/iq（vmaf 未构建） | G8/A3 |
| `speed` | N | P | int 0–8（realtime 下域变化） | G5/A10 |
| `min-q` | N | P | int 0–63 | A5（≤max-q） |
| `max-q` | N | P | int 0–63 | A5 |
| `realtime` | M | A | bool | G5/A10 |
| `auto-tiles` | S | A | bool | A4（与 tile-* 互斥） |
| `enable-intrabc` | S | I | bool | — |

> `threads` 继续隐藏（调度器 E3 覆盖），不进入可选面。

### 4.2 新增 51 项（`aom:`，全部默认 Unset）

#### 质量/码率（16）

| 键 | 形态 | 角色 | 类型/域 | 联动 |
|---|---|---|---|---|
| `aom:cq-level` | N | A（RC 路径） | int 0–63 | G4/A1 |
| `aom:aq-mode` | D | P | int 0–3 | A2（无损锁 0） |
| `aom:deltaq-mode` | D | P | int 0–6 | A7（mode 1 依赖 TPL） |
| `aom:deltaq-strength` | N | P | int 0–1000 | A7（仅 mode 4/6 有意义） |
| `aom:enable-chroma-deltaq` | S | P | bool | A2（无损锁 0）；A3（iq 提示） |
| `aom:enable-qm` | S | A | bool | A8（驱动 qm-min/max）；A3 |
| `aom:qm-min` / `aom:qm-max` | N | P | int 0–15 ×2 | A5（min≤max）；A8（依赖 enable-qm） |
| `aom:sharpness` | N | P | int 0–7 | A3（iq 提示） |
| `aom:enable-adaptive-sharpness` | S | I | bool | — |
| `aom:dist-metric` | D | I | enum psnr/qm-psnr | — |
| `aom:disable-trellis-quant` | D | I | int 0–3 | — |
| `aom:quant-b-adapt` | S | I | bool | — |
| `aom:min-cr` | N | I | int（域以生成脚本为准） | — |
| `aom:sb-qp-sweep` | S | I | bool | — |
| `aom:auto-intra-tools-off` | S | I | bool | — |

#### 环路滤波（4）

| 键 | 形态 | 角色 | 类型/域 | 联动 |
|---|---|---|---|---|
| `aom:enable-cdef` | D | P | int 0–3 | — |
| `aom:enable-restoration` | S | I | bool | — |
| `aom:loopfilter-control` | D | I | int 0–3 | — |
| `aom:enable-dual-filter` | S | I | bool | — |

#### 分区/变换（13）

| 键 | 形态 | 角色 | 类型/域 | 联动 |
|---|---|---|---|---|
| `aom:enable-rect-partitions` / `enable-ab-partitions` / `enable-1to4-partitions` | S | I | bool ×3 | — |
| `aom:min-partition-size` / `max-partition-size` | D | P | enum 4/8/16/32/64/128 ×2 | A5（min≤max） |
| `aom:sb-size` | D | P | enum dynamic/64/128 | — |
| `aom:enable-intra-edge-filter` | S | I | bool | — |
| `aom:enable-tx64` | S | I | bool | — |
| `aom:enable-flip-idtx` | S | I | bool | — |
| `aom:enable-rect-tx` | S | I | bool | — |
| `aom:reduced-tx-type-set` | S | I | bool | — |
| `aom:use-intra-dct-only` / `use-intra-default-tx-only` | S | P | bool ×2 | A9（互斥，待源码核实） |

#### 帧内工具（8，均 S / I）

`aom:enable-palette`、`enable-cfl-intra`、`enable-angle-delta`、`enable-filter-intra`、`enable-smooth-intra`、`enable-paeth-intra`、`enable-directional-intra`、`enable-diagonal-intra`。

#### 线程/分块（3）

| 键 | 形态 | 角色 | 类型/域 | 联动 |
|---|---|---|---|---|
| `aom:tile-columns` / `tile-rows` | D | P | int log2 0–6 ×2 | A4（与 auto-tiles 互斥） |
| `aom:row-mt` | S | P | bool | A13（与调度器线程预算交互；**建议默认不设或只读**） |

#### 码流/兼容（7）

| 键 | 形态 | 角色 | 类型/域 | 联动 |
|---|---|---|---|---|
| `aom:target-seq-level-idx` | D | A | enum level 档 | G11/A16 |
| `aom:strict-level-conformance` | S | P | bool | A16 |
| `aom:set-tier-mask` | D | P | 0=自动 / 1=Main / 2=High（域以生成脚本核对） | A16 |
| `aom:force-video-mode` | S | I | bool | A14（放弃 reduced still header，警告） |
| `aom:timing-info` | D | I | enum unspecified/equal/... | — |
| `aom:cdf-update-mode` | D | I | int 0–2 | — |
| `aom:tune-content` | D | I | enum default/screen | — |

### 4.3 只读固定项（6 行）

libheif 固定 `g_usage=ALL_INTRA`、`rc_end_usage=AOM_Q`、`g_limit=1`、`lag=0`、kf disabled、profile 由位深/色度推导；显示为 R 行，不建可编辑行、不进预设。

### 4.4 libaom 联动规则

| # | 规则 | 动作 | 时机 |
|---|---|---|---|
| A1 | `quality` 与 `aom:cq-level` 二选一 | 其余 H/D | UI + PRE |
| A2 | `lossless` ⇒ 444 + `aq-mode=0` + `enable-chroma-deltaq=0` | Lock/Force | UI + ENC |
| A3 | `tune=iq` ⇒ 库自动改 QM/sharpness/chroma-deltaq | W（不静默覆盖用户值） | UI |
| A4 | `auto-tiles` 与 `tile-columns/rows` 互斥 | H/D | UI + PRE |
| A5 | `min-q≤max-q`；`qm-min≤qm-max`；`min-partition-size≤max-partition-size`；`cq-level≤63` | E | PRE |
| A6 | `aom:matrix-coefficients=identity` 需 4:4:4 | E/W | PRE |
| A7 | `deltaq-strength` 仅 mode 4/6 有意义；`deltaq-mode=1` 依赖 TPL | W/D | UI |
| A8 | `enable-qm=0` ⇒ `qm-min/max` 禁用 | D | UI |
| A9 | `use-intra-dct-only` 与 `use-intra-default-tx-only` 互斥 | E | PRE（待源码核实） |
| A10 | `realtime` 切换 usage/cpu-used 域 | 动态域 + W | UI |
| A11 | 禁止 `input-chroma-subsampling-*`/`denoise-*`/`passes`/`film-grain-*`/`tune=vmaf*`/`*-path` | E | PRE |
| A12 | `lossless` 与 `tune=iq` 互斥 | D | UI |
| A13 | `row-mt` 与调度器线程模型交互，显式关闭可能与插件自动开矛盾 | **默认不设/只读**（未决，见 §11） | UI |
| A14 | `force-video-mode` 放弃 reduced still header | W | UI |
| A15 | `aom:` 透传最后应用，覆盖内置控制（quality 等） | 由 A1 收口；其余 W | PRE/ENC |
| A16 | level/tier 与 `target-seq-level-idx`/`set-tier-mask` 一致性 | E/W | PRE |

---

## 5. AVIF / SVT-AV1 参数形态表（10 键，无补丁）

| 键 | 形态 | 角色 | 类型/域 | 默认 | 联动 |
|---|---|---|---|---|---|
| `speed` | N | P | int 0–13（all-intra 钳到 9） | 12 | S3（>9 值域动态警告） |
| `quality` | N | A（RC 默认） | int 0–100 | 50 | G4/S1（qp 优先） |
| `qp` | N | A（RC 路径） | int 0–63，**无默认** | 未设置 | G4/S1（三态：启用才下发） |
| `min-q` / `max-q` | N | P | int 0–63 ×2 | 0/63 | S1b（min≤max） |
| `tune` | D | A | enum vq/psnr/ssim/iq/ms-ssim | psnr | G8 |
| `threads` | R（隐藏） | — | int 1–16 | 4 | S5（调度器覆盖，不暴露） |
| `tile-rows` / `tile-cols` | D | P | enum 1/2/4/…/64 ×2 | 4/4 | S4（真实条数；Annex-A） |
| `lossless` | M | A | bool | false | S2（产品硬失败，禁用 + 指路 libaom） |

只读展示：profile / level / tier（由插件按位深/色度强制）。

| # | 规则 | 动作 | 时机 |
|---|---|---|---|
| S1 | `qp` 启用 ⇒ `quality` 隐藏/禁用（qp 优先） | H/D | UI + PRE |
| S1b | `min-q≤max-q` | E | PRE |
| S2 | `lossless` 禁用（SVT 插件只 4:2:0 且无 chroma 参数） | D + tooltip | UI |
| S3 | `speed>9` 在 all-intra 下被钳到 9 | W（值域可显示 0–13） | UI |
| S4 | `tile-rows/cols` 为真实条数；tiles≤128、cols≤16 | E | PRE |
| S5 | `threads` 不暴露；profile/level/tier 只读 | R | UI |
| S6 | 任何 `svt:` 前缀键 | E | PRE |

---

## 6. JPEG XL 参数形态表

### 6.1 现有 27 键（设计口径 28；形态不变）

| 形态 | 键 | 说明 |
|---|---|---|
| **N（15）** | `effort`、`decoding_speed`、`codestream_level`、`distance`、`photon_noise`、`epf`、`progressive_dc`、`modular_group_size`、`modular_predictor`、`modular_palette_colors`、`brotli_effort`、`channel_colors_global_percent`、`channel_colors_group_percent`、`modular_ma_tree_learning_percent`、`modular_nb_prev_channels` | 既有 -1=自动哨兵不变；`effort` 上限扩到 11（见 J6） |
| **D（3）** | `resampling`、`group_order`、`color_transform` | `resampling`/`group_order` 已是下拉 |
| **S（8）** | `keep_invisible`、`dots`、`patches`、`gaborish`、`progressive_ac`、`qprogressive_ac`、`modular_lossy_palette`、`responsive` | 现有复选框 |
| **M（1）** | `lossless` | 模式锚点 |

> 计数核对：15+3+8+1 = 27；设计/评审口径 28（含档位/位深行或一个历史计数差），Stage A4 以 `format_tables.cpp` 实际键为准对账，不改变“既有键语义不动”的约束。

### 6.2 新增 16 项与 effort 扩档

| 键 | 形态 | 角色 | 类型/域 | 联动 |
|---|---|---|---|---|
| `noise` | S | I | 三态（跟随默认/开/关） | J16（旧式噪声，已被 photon_noise 取代，tooltip） |
| `group_order_center_x` | N | P | int（0..width-1） | J4（仅 group_order=center；<width 逐文件） |
| `group_order_center_y` | N | P | int（0..height-1） | J4（仅 group_order=center；<height 逐文件） |
| `upsampling_mode` | D | P | enum auto/nearest/… | J3（仅 resampling>1） |
| `uses_original_profile` | S | P | 三态 | J7（disable_perceptual 依赖）；J8（有损 + true 质量变差警告） |
| `box_compress` | S | A | 三态 | J5（驱动 brotli_effort） |
| `expert_options` | S | A | 三态 | J6（驱动 effort=11） |
| `effort`（扩到 11） | N | P | 现有 1–10 → 1–11 | J6（11 需 expert_options） |
| `buffering` | D | M | 三态 0/1/2（3 已弃用） | G6（码流缓冲模式） |
| `output_mode` | D | M | 三态 0/1/2 | G6/J12（2=乱序 jxlp，兼容警告） |
| `disable_perceptual_heuristics` | S | P | 三态 | J7（需 uses_original_profile=true） |
| `intensity_target` | N | P | float nits，0=自动 | J17（HDR 组） |
| `min_nits` | N | P | float，0=自动 | J17 |
| `relative_to_max_display` | S | P | 三态 | J17 |
| `linear_below` | N | P | float，0=自动 | J17 |
| `intrinsic_size` | N | I | int x/y，0=不写 | J14（容器元数据） |
| `frame_name` | T | I | string | J13（禁用 fast-lossless，tooltip） |

> 新增=16 个键（`effort` 是既有键扩域，不另计行）；评审稿“+14”按组行/合并计数。

### 6.3 JXL 联动规则

| # | 规则 | 动作 | 时机 |
|---|---|---|---|
| J1 | 技术（VarDCT/Modular）切换参数集；VarDCT-only 与 Modular-only 互斥 | H | UI（现有 tech 过滤） |
| J2 | `lossless` ⇒ 强制 Modular、隐藏 distance、锁 resampling=1、锁 modular_lossy_palette=false | H/L/Force | UI + ENC |
| J3 | `resampling>1` 才显示 `upsampling_mode` | H/D | UI |
| J4 | `group_order=center` 才显示 center_x/y；center 坐标 < width/height | H；E | UI + FILE |
| J5 | `box_compress=0` ⇒ `brotli_effort` 隐藏 | H/D | UI |
| J6 | `expert_options=0` ⇒ `effort` 上限 10；=1 才允许 11 | 动态域 + E | UI + PRE |
| J7 | `disable_perceptual_heuristics` 需 `uses_original_profile=true` | D；E | UI + PRE |
| J8 | 有损 + `uses_original_profile=true` ⇒ 质量可能显著变差 | W | UI |
| J9 | 有损 Modular ⇒ 隐藏 `palette_colors`/`lossy_palette`/`channel_colors_*`；`modular_ma_tree_learning_percent=0` + 有损 Modular ⇒ 硬错误 | H；E | UI + PRE |
| J10 | `modular_color_space>0` 需 `color_transform≠XYB` | D/E | UI + PRE |
| J11 | 有损 Modular 下 `modular_predictor=3..6` 会被库替换 | W | UI |
| J12 | `output_mode=2` ⇒ 老解码器兼容性警告 | W | UI |
| J13 | `frame_name` 非空会禁用 fast-lossless | W | UI |
| J14 | `intrinsic_size=0` = 不写；x/y 需同时给 | E | PRE |
| J15 | `buffering=3` 已弃用，候选只给 0/1/2 | 域 | 目录 |
| J16 | `noise` 旧式、已被 `photon_noise` 取代 | ○ + tooltip | 目录 |
| J17 | HDR 组（`intensity_target`/`min_nits`/`relative_to_max_display`/`linear_below`）一起出现；非 HDR 可忽略 | W | UI |
| J18 | `codestream_level=5` + 16bit 无损 ⇒ 需 level 10 | E | PRE |

---

## 7. 联动求值时机（实现口径）

| 时机 | 动作 | 负责 | 失败表现 |
|---|---|---|---|
| **UI 实时** | 隐藏 H / 置灰 D / 锁定 L / 警告 W | `paramform` + `param_rules` | 行内 hint、tooltip；不弹窗 |
| **运行前预检** | 错误 E（汇总） | `validate_run_config` | 拒绝运行、中文定位到格式 tab + 行；批量只报一次 |
| **逐文件** | 尺寸/位深相关检查 | worker 内编码前 | 该文件失败并给明确原因（不是逐文件重复同一错误） |
| **编码器兜底** | 最后防线 | `enc_heif`/`enc_jxl` | 现有 `fail()` 文案中文化；包含键/值 |

**求值顺序**：模式锚点（G1–G6）→ 档位锚点（G7–G11）→ 依赖行（P）→ 独立行（I）。锚点切换**不静默覆盖用户值**；必须强制的值（如无损锁 444）以只读/锁定态显示，并在预检摘要里说明原因。

---

## 8. 控件渲染规则

1. **三态覆盖**（新增可选行）：
   - N/D 行：前导“启用”勾选；未勾 = 不下发（`ParamSet` 无键），值控件置灰并显示“跟随编码器默认”；
   - S 行：三态下拉 `{跟随默认, 开, 关}`（避免双复选框）；
   - 既有行：保持今日控件与默认值，无“启用”勾选。
2. **RC 模式控件**（G4）：每个格式一个模式下拉/单选；默认“质量”= 既有 `quality` 行；切到 CRF/QP 时只显示对应数值行，其余隐藏并禁止下发；预设载入时按“存在的键”推断模式，出现冲突（如同时有 crf 与 qp）⇒ 预检错误。
3. **动态域**：SVT `speed` 显示 0–13，>9 警告；JXL `effort` 上限随 `expert_options` 10/11；libaom `speed` 域随 `realtime`；`codestream_level` 保持现有数值控件。
4. **只读行**：`profile`/`keyint`/`input-csp`/`total-frames`/SVT profile/level/tier/libaom 固定项，显示“只读（上游固定/推导）”，不可编辑、不参与预设。
5. **错误定位**：`开始运行` 拒绝时聚焦第一个错误行；每个格式 tab 显示错误数徽标；文案含“键 + 当前值 + 原因 + 建议”。
6. **搜索/过滤**：现有搜索扩到“键/中文标签/引擎名”；保留“只看已启用/只看可设置”过滤；控件形态参与过滤（如“只看模式切换”可作为后续增强，不列入本轮验收）。
7. **预设**：只序列化键/值，不序列化控件形态；载入时未知/跨后端/未收录键 ⇒ 预检错误；三态未启用键不写入。

---

## 9. 计数与对账口径

| 后端 | 现有可编辑 | 新增可编辑 | 禁用/隐藏 | 只读 | 合计行 | 评审稿组行 |
|---|---:|---:|---:|---:|---:|---:|
| HEIF/x265 | 7 | 36 | 0 | 4 | 47 | ≈40 |
| AVIF/libaom | 10 | 51 | 0 | 6 | 67 | ≈50 |
| AVIF/SVT | 8 | 0 | 2（`lossless` 禁用、`threads` 隐藏） | 3 | 13 | 10 |
| JPEG XL | 27 | 16（+`effort` 扩域） | 0 | 0 | 43 | ≈42 |
| **合计** | **52** | **103** | **2** | **13** | **170** | **≈142** |

> **双口径（已定）**：≈142 组行用于范围评审/报告叙述；**170 键展开行是目录、测试、门禁、UI 验收的唯一真源**（`test_param_control_map` 按此断言）。差异全部来自**组行合并**（8 个帧内开关、`tskip`/`tskip-fast`、`qm-min/max`、partition 上下限、tile 对、HDR 组、只读行等），**不是范围扩大**。Stage A4 输出“评审清单条目 ↔ 目录行”逐条对账表，任何多/少必须书面说明。

---

## 10. 契约与任务落点

| 位置 | 补充内容 |
|---|---|
| 设计 §4.6（新增） | 本文件摘要；C1/C2 口径引用 |
| 设计 C1 | `CatalogRow` 增加 `Control`（M/N/D/S/T/R）与 `Role`（A/P/I）；`.inc` 行格式由 A0 冻结 |
| 设计 C2 | 新增行三态；新增布尔行三态下拉；RC 模式显式化（G4） |
| 设计 §4.4 | UI 引用 §4.6 / 本文件；不改变既有布局与冻结行 |
| workflow A0 | 冻结 `.inc` 行格式（含 control/role） |
| workflow A1–A3 | 每行必须填 form/role/联动规则引用；与评审稿逐条对账 |
| workflow B1 | `param_rules` 消费规则表；模式锚点/三态/动态域/预检 |
| workflow B2 | 渲染 M/N/D/S/T/R；三态、错误定位、预设推断 |
| 单测 | `test_param_control_map`：每行 form/role 非空；规则引用的键存在；锚点闭合；计数与对账表一致 |
| 开发纪律 | `docs/m8-discipline.md`：任务卡必填字段/清晰性自检/子任务禁止再派发；开工前置条件 |
| 报告 | `m8-report.md` 记录“控件形态计数表 + 对账差异说明” |

---

## 11. 未决与风险

| # | 事项 | 处理 |
|---|---|---|
| C1 | RC 模式控件 | **已定：显式模式下拉/单选**（用户 2026-10-28 裁定），见 §8 规则 2；不再走三个互斥勾选（已关闭） |
| C2 | `aom:row-mt` 与调度器线程预算的交互 | 建议默认不设/只读；不改变“线程池不暴露”的裁定 | 待 Stage B 定 |
| C3 | `aom:set-tier-mask`、`min-cr`、`enable-cdef` 等少数域/语义以源码抽取为准 | 生成脚本 A4 定稿；本表只给语义档位 | 待 A4 |
| C4 | `use-intra-dct-only` 与 `use-intra-default-tx-only` 是否真互斥 | A4/B1 前用本地 libaom 源码核实；不互斥则降为 W | 待 A4 |
| C5 | 既有 JXL 键数 27 vs 设计口径 28 | A4 对账修正口径（不改“既有键语义不动”） | 待 A4 |
| C6 | 组行 vs 键数计数差（≈142 vs 170 行） | **已定双口径**：报告 ≈142；测试/门禁 170；A4 对账表逐条登记 | 已关闭 |

---

*本文件是补充设计稿，确认后随设计 v3→v4 一起冻结；实现期变更走 `docs/m8-adjudications.md`。*
