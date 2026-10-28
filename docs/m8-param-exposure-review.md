# M8 参数暴露评审清单（逐库 · 重要性分级 · 待拍板）

> 状态：**评审稿 v1（2026-10-28）**，对应设计 `docs/m8-encoder-params-and-ssimulacra2-design.md`。
> 目的：回应「x265 参数太多了，选择重要的；把每个库要暴露的参数总结，展示出来进行评审，说明参数的重要性」。
> 口径：本清单 = **建议暴露的 GUI 参数全集**（每行 = 一个控件）；未列入的参数按类别在 §5 汇总并给出不暴露理由，不逐个占评审篇幅。
> 证据来源：本机 vcpkg buildtree 逐行核对（libheif 1.23.5 / x265 4.3 / libaom 3.15.1 / SVT-AV1 4.2.0 / libjxl 0.12.0）+ 官方文档（见 §7）。重要性判定标准见 §0。
> 注：表中“默认值”为 pinned 版本语境下的**参考值**（部分随 preset/tune/位深/插件强制而变化）；评审重点是**范围与重要性**，逐值冻结由目录生成脚本在开发期从源码抽取核对。

---

## 0. 评审口径与总量

**重要性分级**

| 级别 | 含义 | UI 位置 |
|---|---|---|
| ★ 核心 | 直接影响画质/体积/兼容性，绝大多数用户该看到 | 核心区（不折叠） |
| ● 重要 | 常用高级旋钮，调优/兼容性问题会用到 | 高级区，默认展开的第一组 |
| ○ 进阶 | 少数场景（极限质量/极限速度/特定内容/调试取证） | 高级区，折叠组 |
| — 只读 | 由上游/位深/色度自动决定，**显示生效值但不可改** | 高级区「只读（上游固定）」 |
| ✕ 不暴露 | 见 §5（视频向/有害/不可达/文件副作用） | 不出现（文档登记原因） |

**总量（建议 · 已获用户 2026-10-28 裁定“按评审稿精选清单定稿”）**

| family / 后端 | 现在 GUI 可见 | 建议新增 | 合计 | 说明 |
|---|---:|---:|---:|---|
| HEIF / x265 | 7（+ 顶部无损复选框/档位行/位深行） | **+33** | **≈40** | 从 220 个可达名中精选“对单帧静止图有意义”的 33 项；其余按 §5 归类不暴露 |
| AVIF / libaom | ≈10 可见（15 内省 − alpha 4 − threads 隐藏） | **+40** | **≈50** | 原生 15 全保留 + `aom:` 白名单 40 项 |
| AVIF / SVT-AV1 | 10 | **0** | **10** | **用户已裁定不打 overlay 补丁**；SVT 全参数面登记为库面限制（§3） |
| JPEG XL | 28（24 frame setting + 4 特例） | **+14** | **≈42** | 补齐 libjxl 0.12 对单图有意义的缺项 |
| 合计 | ≈55 | +87 | **≈142** | 对比“字面全量 ~500+”大幅收敛，覆盖画质/体积/兼容性主路径 |

> **线程池裁定（用户 2026-10-28）**：线程池不是编码参数，**不进入格式参数面**（`x265:pools`/`x265:frame-threads` 从清单移除；不新增“HEIF 线程池受预算约束”设置）；线程调度交给软件默认/调度器处理。R-078（x265 默认池不入账）继续如实登记为已知限制。
> 所有新增项默认 **不设置（跟随编码器默认）**，只有用户启用才下发 ⇒ 既有产物零变化、回归 zero diff。既有 7/10/28 项语义与默认值不变。
>
> **控件形态/联动分级（v4 补充，2026-10-28）**：逐键形态（模式切换/数值/下拉/开关/文本/只读）、联动锚点与计数口径见 `docs/m8-param-control-map.md`。本表的 ≈40 / ≈50 / 10 / ≈42 是**组行**计数；按控件/键展开约 155 个可编辑控件 + 2 禁用/隐藏 + 13 只读（170 行），差异全部来自组行合并，范围不变；精确数以 Stage A4 “评审清单↔目录行”对账表为准。

---

## 1. HEIF / x265（≈40 项：现有 7 + 新增 33）

### 1.1 现有 7 项（保持不变，列此仅供对照）

| 键 | 名称 | 类型/域 | 默认（插件 slow/ssim 语境） | 级别 | 重要性 |
|---|---|---|---|---|---|
| `quality` | 质量 | int 0–100（CRF=(100−q)/2） | 50 | ★ | 唯一质量主入口；无损下无效（现有 UI 已隐藏） |
| `lossless` | 无损 | bool | false | ★ | 码流无损（强制 4:4:4；非 RGB 逐位） |
| `preset` | 速度预设 | enum 10 档 | slow | ★ | 速度/压缩率主旋钮 |
| `tune` | 画质取向 | enum psnr/ssim/grain/fastdecode | ssim | ★ | 失真度量（注意 psy/AQ 部分会被 libheif 覆盖，tooltip 需说明） |
| `chroma` | 色度采样 | enum 420/422/444 | 420 | ★ | 兼容性/画质；444 触发 RExt 档 |
| `tu-intra-depth` | 帧内 TU 深度 | int 1–4 | 2 | ● | 帧内分割深度；与 CTU 有耦合 |
| `complexity` | 复杂度档 | int 0–100 | 0（无默认） | ● | 插件映射到 rd-refine/cu-lossless/wpp 的复合开关 |

### 1.2 新增 33 项（建议）

#### A. 质量 / 码率控制（13）

| 键（引擎名） | 名称 | 类型/域 | 默认 | 级别 | 为什么重要 / 与现有项的关系 |
|---|---|---|---|---|---|
| `x265:crf` | CRF 质量因子 | float，8bit 0–51（随位深上抬） | 28（RC=CRF 时） | ★ | 比 `quality` 更精确的 CRF 原值；**与 quality 二选一**（同写 rfConstant，后者赢） |
| `x265:qp` | 恒定 QP | int 0–51 | 32（RC=CRF 时不用） | ● | CQP 模式专家入口；与 quality/crf 三选一 |
| `x265:qpmin` | 最小 QP | int 0–69 | 0 | ● | 裁掉过高质量分配、控制体积 |
| `x265:qpmax` | 最大 QP | int 0–69 | 69 | ● | 防低质量块（照片暗部/渐变常见） |
| `x265:aq-mode` | 自适应量化模式 | int 0–4 | 2（插件强制 1） | ● | 静止图空间 AQ，影响暗部/纹理画质；tooltip 注明插件覆盖 |
| `x265:aq-strength` | AQ 强度 | float 0–3 | 1.0 | ● | 与 aq-mode 配合的主强度 |
| `x265:qcomp` | 码率压缩曲线 | float 0.5–1.0 | 0.6 | ○ | CRF 曲线形状；调体积/质量分布 |
| `x265:psy-rd` | 感知 RD | float 0–5 | 2.0（插件强制 1.0） | ● | JPEG XL/AVIF 之外最常用的“观感”旋钮；**4:4:4 且 >0 会触发色度 QP 偏移**（tooltip） |
| `x265:psy-rdoq` | 感知 RDOQ | float 0–50 | 0（插件强制 1.0） | ● | 纹理保留；rdoq=0 时被库清零（联动） |
| `x265:rdoq-level` | RDOQ 级别 | int 0–2 | 0（slow→2） | ● | 码率/画质核心；0 时 psy-rdoq 无效（联动） |
| `x265:rd` | RDO 深度 | int 1–6 | 3（slow→4） | ● | 速度↔质量主旋钮之一；<3 时 cu-lossless/tskip 被禁 |
| `x265:rd-refine` | RD 精化 | bool | 0 | ○ | 高 rd 下额外增益；需 rd≥5 且 aq-mode≠0（联动） |
| `x265:cu-lossless` | CU 级无损试探 | bool | 0 | ○ | 对细节/文字类内容常出奇迹；需 rd≥3 |
| `x265:ssim-rd` | SSIM 导向 RD | bool | 0 | ○ | 改变 RD 目标（会清 psy-rd，联动） |

#### B. 量化 / 分区 / 变换（11）

| 键 | 名称 | 类型/域 | 默认 | 级别 | 为什么重要 |
|---|---|---|---|---|---|
| `x265:qg-size` | 量化组尺寸 | int ≥8（≤CTU） | 32 | ○ | 量化自适应粒度；越界会被库静默夹逼 |
| `x265:cbqpoffs` | Cb QP 偏移 | int −12..12 | 0 | ○ | 色度质量分配；444+psy 下库自动改 6（tooltip） |
| `x265:crqpoffs` | Cr QP 偏移 | int −12..12 | 0 | ○ | 同上 |
| `x265:min-cu-size` | 最小 CU | enum 8/16/32 | 8 | ● | 速度/细节权衡；>8 且宽高不整除时 x265 自动 padding（运行期警告） |
| `x265:max-tu-size` | 最大 TU | enum 4/8/16/32 | 32 | ● | 变换尺寸上限；>CTU 会被夹逼 |
| `x265:limit-tu` | 限制 TU 递归 | int 0–4 | 0 | ○ | 需 tu-inter-depth>1（默认 1 ⇒ 单帧恒无效，tooltip 标注） |
| `x265:fast-intra` | 快速帧内 | bool | 0 | ● | rd≤4 时显著提速；单帧常用 |
| `x265:splitrd-skip` | 帧内 split RD 跳过 | bool | 0 | ● | **帧内专用**提速项 |
| `x265:signhide` | 符号位隐藏 | bool | 1 | ○ | 省 1 bit/系数，几乎无画质代价 |
| `x265:strong-intra-smoothing` | 强帧内平滑 | bool | 1 | ○ | 平滑/细节取向 |
| `x265:tskip` / `x265:tskip-fast` | 变换跳过 | bool ×2 | 0/0 | ○ | 需 rd≥3；4×4 帧内场景有效（两个独立行，或合并为一个组） |
| `x265:lowpass-dct` | 低通 DCT | bool | 0 | ○ | 小图/降噪后内容有时更省 |

#### C. 环路滤波 / 心理视觉（5）

| 键 | 名称 | 类型/域 | 默认 | 级别 | 为什么重要 |
|---|---|---|---|---|---|
| `x265:sao` | SAO 滤波 | bool | 1 | ● | HEVC 主要环路滤波；开关影响体积/观感 |
| `x265:sao-non-deblock` | SAO 非去块边界 | bool | 0 | ○ | 细节保留；仅 sao=1 有意义 |
| `x265:limit-sao` | 限制 SAO | bool | 0 | ○ | 省比特/提速 |
| `x265:selective-sao` | 按 slice 类型选 SAO | int 0–4 | 0 | ○ | 单 I 帧收益小 |
| `x265:no-deblock` | 关闭去块滤波 | bool | false | ● | 全局去块开关（比逐参数 tC:beta 好用的专家面）；默认值来自 x265（loopfilter=1） |

> 说明：`deblock tC:beta` 的细粒度字符串（如 `2:3`）不进入 GUI（原始字符串语义 + 易写错），只提供 `no-deblock` 专家开关；需要细调走预设 JSON/`--param` 的受限通道（见 §6 问题 3）。

#### D. 码流 / 兼容性（4）

| 键 | 名称 | 类型/域 | 默认 | 级别 | 为什么重要 |
|---|---|---|---|---|---|
| `x265:level-idc` | 目标 Level | enum（10–72 常用档） | 0=自动 | ● | 解码器/硬件兼容门槛；与 CRF 同用会强制 VBV（联动警告，非确定性） |
| `x265:high-tier` | 高 Tier | bool | 1 | ○ | 仅配 level 时有效 |
| `x265:allow-non-conformance` | 允许不合规 | bool | 0 | ○ | 大 ref/超大图场景的解药；会让 profile/level 变 NONE |
| `x265:wpp` | 波前并行 | bool | 1 | ● | 并行形态；单图编码质量/速度微调；`slices>1` 依赖它 |

#### E. 只读信息行（3，展示上游固定值；不计入 40）

| 只读键 | 显示值来源 | 为什么显示 |
|---|---|---|
| `__x265:profile` | 由位深/chroma 推导（mainstillpicture / main10-intra / main12-intra） | 用户需知道实际档位（现有"编码档位"行已覆盖；此处可作为只读确认行，或复用之） |
| `__x265:keyint` | 1（`param_apply_profile` 侧效应） | 解释“为什么单帧还有 GOP 限制” |
| `__x265:input-csp` | 由 chroma 推导（420/422/444） | 解释平面布局与 chroma 的绑定 |
| `__x265:total-frames` | 1 | 解释为什么视频类参数不生效 |

> 计数核对：13 + 11 + 5 + 4 = **33 项新增**（另只读信息行 3 项不计入）；加上现有 7 ⇒ **≈40**，与总量表一致。
>
> **已剔除（用户裁定）**：线程池类参数（`x265:pools`/`x265:frame-threads`）不属于编码参数，不进入格式参数面；线程调度交给软件默认。`x265:slices`/`wpp` 属编码工具（影响码流），保留。

### 1.3 x265 不暴露清单（220 可达名中其余部分，按类）

| 类 | 约数 | 代表 | 不暴露理由 |
|---|---:|---|---|
| 视频/多帧向 | ~120 | bframes/b-adapt/b-pyramid/ref/me/subme/merange/max-merge/weightp/weightb/early-skip/rskip/limit-refs/limit-modes/rdpenalty/b-intra/cutree/ipratio/pbratio/vbv-*/cplxblur/qblur/zones/qpfile/interlace/open-gop/intra-refresh/analysis-*/pass/stats/csv/abr-ladder/multiview/scc/fovea/hme* 等 | 单帧仍图路径不走序列分支 ⇒ 无效果（部分会改 VPS/level 开销） |
| 有害/破坏自洽 | ~15 | `annexb`（NAL 起始码/长度前缀被 libheif 误剥）、`input-csp`（平面布局错位）、`total-frames`（≠1 破坏 still/profile）、VUI 组（range/colorprim/transfer/colormatrix/chromaloc/display-window/crop-rect/video-signal-type-preset：容器 nclx/pasp 与码流打架）、`min-cu-size>8`+非整除宽高（自动 padding/conformance window）、`slices>1`（兼容未验，已在暴露面以 ○ + 警示保留）、`bitrate/vbv-*`（把 CRF 改成 ABR）、`uhd-bd`/`dolby-vision-profile`/`intra-refresh`/`pic-struct` | 会产出不合规/不自洽码流或改变仍图语义 |
| 文件副作用 | ~12 | nalu-file/csv/stats/analysis-save/analysis-load/lambda-file/film-grain/dhdr10-info/fovea-gaze-file/zones/scaling-list 的文件形式 | GUI 不应触发文件读写 |
| 死参 / CLI-ONLY / 本构建不存在 | ~30 | `log-level`/`log`/`input-res`（被 libheif 事后覆盖，静默无效）、`preset`/`tune`/`profile`/`qpfile`/`zonefile`/`abr-ladder`…（不在 `x265_param_parse` 名字表，必失败）、`alpha`/`format`/`num-views`/`scc`/`svt*`/`input-depth`（#if 编译掉） | 不可达或必失败 |
| 线程池/并行风险 | 3 | `pools`/`frame-threads`（**用户裁定：线程池不是编码参数，不进入格式参数面，交给软件默认调度**）、`slices`（item 内多 VCL 的兼容性未验证、收益低） | 不属于“编码质量参数”或风险 > 收益 |

---

## 2. AVIF / libaom（≈50 项：现有 ≈10 + 新增 40）

### 2.1 现有项（保留，alpha 专用项按既有裁定不暴露）

`quality` ★、`lossless` ★、`chroma` ★、`tune` ★、`speed`（cpu-used）★、`min-q` ●、`max-q` ●、`realtime` ○（会切 REALTIME 模式、cpu-used 上限变化）、`auto-tiles` ●、`enable-intrabc` ●（插件原生；`threads` 仍由调度器覆盖，保持隐藏）。

### 2.2 新增 40 项（`aom:` 白名单；全部默认不设置）

| 组 | 键 | 名称 | 类型/域 | 默认 | 级别 | 为什么重要 / 约束 |
|---|---|---|---|---|---|---|
| 质量/码率 | `aom:cq-level` | 恒定质量 QP | int 0–63 | 10 | ● | **与 quality 二选一**（直通最后应用会覆盖 quality）；精确压体积用 |
| | `aom:aq-mode` | 自适应量化 | int 0–3 | 0 | ● | AV1 静止图画质关键；lossless 时必须 0（联动） |
| | `aom:deltaq-mode` | Delta-Q 模式 | int 0–6 | 1 | ○ | 分区 QP；mode 1 依赖 TPL（默认开） |
| | `aom:deltaq-strength` | Delta-Q 强度 | int 0–1000 | 100 | ○ | 只对 mode 4/6 有意义 |
| | `aom:enable-chroma-deltaq` | 色度 Delta-Q | bool | 0 | ○ | 色度单独量化；lossless 时必须 0 |
| | `aom:enable-qm` | 量化矩阵 | bool | 0 | ○ | 静止图收益明显；`tune=iq` 会自动打开 |
| | `aom:qm-min` / `qm-max` | QM 级别上下限 | int 0–15 ×2 | 库默认 | ○ | 与 enable-qm 联动 |
| | `aom:sharpness` | 锐度偏好 | int 0–7 | 0 | ● | 观感向，静止图常用 |
| | `aom:enable-adaptive-sharpness` | 自适应锐度 | bool | 0 | ○ | — |
| | `aom:dist-metric` | RD 度量 | enum psnr/qm-psnr | psnr | ○ | 调优取向 |
| | `aom:disable-trellis-quant` | 关 Trellis | int 0–3 | 3 | ○ | 速度/质量权衡 |
| | `aom:quant-b-adapt` | 自适应 quant_b | bool | 0 | ○ | — |
| | `aom:min-cr` | 最小压缩比 | int | 库默认 | ○ | 防“过度压缩”边界 |
| | `aom:sb-qp-sweep` | SB QP 扫描 | bool | 0 | ○ | 慢速细节优化 |
| | `aom:auto-intra-tools-off` | 自动关帧内工具 | bool | 0 | ○ | 速度档辅助 |
| 环路滤波 | `aom:enable-cdef` | CDEF | int 0–3 | 1 | ● | AV1 核心环路滤波 |
| | `aom:enable-restoration` | 恢复滤波 | bool | 1 | ● | 静止图画质关键 |
| | `aom:loopfilter-control` | 环路滤波策略 | int 0–3 | 1 | ○ | — |
| | `aom:enable-dual-filter` | 双滤波 | bool | 0 | ○ | — |
| 分区/变换 | `aom:enable-rect-partitions` | 矩形分区 | bool | 1 | ● | 影响压缩效率 |
| | `aom:enable-ab-partitions` | AB 分区 | bool | 1 | ● | — |
| | `aom:enable-1to4-partitions` | 1:4 分区 | bool | 1 | ○ | — |
| | `aom:min-partition-size` / `max-partition-size` | 分区尺寸上下限 | int 4–128?（库域） | 库默认 | ● | 必须 min≤max（规则拦截） |
| | `aom:sb-size` | Superblock 尺寸 | enum dynamic/64/128 | dynamic | ● | 速度/质量 |
| | `aom:enable-intra-edge-filter` | 帧内边缘滤波 | bool | 1 | ○ | — |
| | `aom:enable-tx64` | 64 点变换 | bool | 1 | ○ | — |
| | `aom:enable-flip-idtx` | 翻转 IDTX | bool | 库默认 | ○ | — |
| | `aom:enable-rect-tx` | 矩形变换 | bool | 1 | ○ | — |
| | `aom:reduced-tx-type-set` | 精简变换集 | bool | 0 | ○ | 速度 |
| | `aom:use-intra-dct-only` / `use-intra-default-tx-only` | 仅帧内 DCT / 默认变换 | bool ×2 | 0/0 | ○ | 提速 |
| 帧内工具 | `aom:enable-palette` / `enable-cfl-intra` / `enable-angle-delta` / `enable-filter-intra` / `enable-smooth-intra` / `enable-paeth-intra` / `enable-directional-intra` / `enable-diagonal-intra` | 8 个帧内工具开关 | bool ×8 | 全 1 | ○ | 关工具换速度；`iq` 调优会改部分 |
| 线程/分块 | `aom:tile-columns` / `tile-rows` | 分块列/行 | int log2 0–6 ×2 | 0/0 | ● | 并行/解码行为；与 `auto-tiles` 互斥 |
| | `aom:auto-tiles`（现有） | 自动分块 | bool | 0 | ● | 与 tile-* 互斥 |
| | `aom:row-mt` | 行级多线程 | bool | 1 | ● | 线程预算相关；`threads>1` 时插件自动开 |
| 码流/兼容 | `aom:target-seq-level-idx` | 目标 Level | enum | keep | ● | 解码器/硬件门槛 |
| | `aom:strict-level-conformance` | 严格 Level | bool | 0 | ○ | — |
| | `aom:set-tier-mask` | Tier | uint 位图 | 0 | ○ | — |
| | `aom:force-video-mode` | 强制视频模式 | bool | 0 | ○ | 唯一能改 still 序列头形态的可达名（放弃 reduced still header） |
| | `aom:timing-info` | Timing info | enum | unspecified | ○ | 兼容性元数据 |
| | `aom:cdf-update-mode` | CDF 更新 | int 0–2 | 1 | ○ | 解码/并行折衷 |
| | `aom:tune-content` | 内容取向 | enum default/screen | default | ○ | 屏幕内容/图形 |

> 计数：约 40 项新增（部分行是“组”合并，如 8 个帧内工具开关、qm-min/max、partition 上下限）；开发时按最终目录实数登记。
> **不暴露/禁止**（`aom:` 层面）：`input-chroma-subsampling-x/y`（只改码流声明不改平面布局 ⇒ 不合规）、`denoise-*`/`enable-dnl-denoising`（违反“无额外图像处理”）、`*-path`/`passes`/`film-grain-*`/`tune=vmaf*`（构建未开/文件副作用/改像素）、全部 cfg 级字段（`end-usage/usage/profile/bit-depth/lag/kf-*/full_still_picture_hdr/resize/superres`，**不是 `aom_codec_set_option` 名字**，不可达——libheif 固定 `AOM_Q + ALL_INTRA + g_limit=1`）。

---

## 3. AVIF / SVT-AV1（10 项，无补丁；用户已裁定）

| 键 | 名称 | 类型/域 | 默认 | 级别 | 说明 |
|---|---|---|---|---|---|
| `speed` | 速度档 | int 0–13（all-intra 下应用钳到 9） | 12 | ★ | 主旋钮；>9 无意义（已钳 + 日志） |
| `quality` | 质量 | int 0–100（映射 QP） | 50 | ★ | 与 `qp` 互斥（qp 优先） |
| `qp` | 精确 QP | int 0–63，**无默认** | 未设置 | ● | 勾选启用才下发（三态）；覆盖 quality |
| `min-q` / `max-q` | QP 上下限 | int 0–63 ×2 | 0/63 | ● | min≤max |
| `tune` | 调优 | enum vq/psnr/ssim/iq/ms-ssim | psnr | ● | `iq`/`ms-ssim` 偏静止图 |
| `threads` | 线程数 | int 1–16 | 4 | ● | 受全局线程预算影响（调度器覆盖；UI 按现有口径隐藏或只读） |
| `tile-rows` / `tile-cols` | 分块（**真实条数**） | enum 1/2/4/8/16/32/64 ×2 | 4/4 | ● | 与 aom 的 log2 语义不同；Annex-A 约束 tiles≤128、cols≤16 |
| `lossless` | 无损 | bool | false | ✕ | **产品当前硬失败**（SVT 插件只 4:2:0 且无 chroma 参数）；UI 已隐藏，保持 + tooltip 指路 libaom |
| — 只读 | profile/level/tier | 由插件按位深/色度强制 | — | — | 显示实际值不可改 |

> **裁定的后果（写入设计/文档）**：SVT 的 121 个 `svt_av1_enc_parse_parameter` 名字在本产品中**不可达**（libheif 1.23.5 与 master 都没有 `svt:` 透传；用户裁定不打 patch）。因此“AVIF 全参数暴露”对 SVT 后端**不成立**，仅在 libaom 后端成立；`pp_linkprobe` 增加“SVT 参数面 = 10 键”的断言，防止将来误以为已补全。

---

## 4. JPEG XL（≈42 项：现有 28 + 新增 14）

### 4.1 现有 28 项（保持）

`effort`、`decoding_speed`、`codestream_level`、`distance`、`photon_noise`、`epf`、`keep_invisible`、`dots`、`patches`、`gaborish`、`progressive_ac`、`qprogressive_ac`、`progressive_dc`、`resampling`、`group_order`、`color_transform`、`modular_group_size`、`modular_predictor`、`modular_palette_colors`、`modular_lossy_palette`、`brotli_effort`（现隐藏）、`responsive`、`channel_colors_global_percent`、`channel_colors_group_percent`、`modular_ma_tree_learning_percent`、`modular_nb_prev_channels`、`lossless` + 档位/位深行。**语义/默认值不动**（改变默认值=产物漂移，另案）。

### 4.2 新增 14 项

| 键 | 名称 | 类型/域 | 默认 | 级别 | 为什么重要 / 约束 |
|---|---|---|---|---|---|
| `noise` | 旧式自适应噪声 | tri-state −1/0/1 | −1(auto) | ○ | libjxl 头注释不推荐、已被 photon_noise 取代；为“全参数”补一行，默认不设 |
| `group_order_center_x` | 渐进中心 X | int −1..width | −1 | ○ | 仅 `group_order=1(center)` 有意义（联动；边界运行期预检） |
| `group_order_center_y` | 渐进中心 Y | int −1..height | −1 | ○ | 同上 |
| `upsampling_mode` | 解码升采样核 | enum auto/nearest/… | −1 | ○ | 仅 `resampling>1` 有意义（联动） |
| `uses_original_profile` | 保留原色度配置 | bool（默认由规则推导） | 当前：有 ICC 即 true | ● | 决定 XYB：有损时强制 true 会显著变差；给专家显式开关 + 中文解释（当前是隐式推导） |
| `box_compress` | 压缩元数据 box | bool | 0（现状不压缩） | ● | Exif/XMP box 体积；打开后 `brotli_effort` 才有意义（联动） |
| `expert_options` | 允许专家选项 | bool | off | ○ | 唯一作用是允许 `effort=11` |
| `effort`（扩到 11） | 编码努力 11 | int（需 expert_options） | 7 | ○ | 极限压缩档；依赖专家开关（联动） |
| `buffering` | 缓冲模式 | tri-state −1/0/1/2 | −1 | ○ | 内存/流式权衡；3 已弃用 |
| `output_mode` | 输出流模式 | tri-state 0/1/2 | −1 | ○ | 2=乱序 jxlp（ftyp v1，老解码器不兼容；警示） |
| `disable_perceptual_heuristics` | 关闭感知优化 | bool | 0 | ○ | 需 `uses_original_profile=true`（联动，否则库报错） |
| `intensity_target` | 峰值亮度 | float nits | 0=自动 | ○ | HDR/色调映射提示；非 HDR 工作流可忽略 |
| `min_nits` / `relative_to_max_display` / `linear_below` | 黑位/相对显示/线性阈 | float/bool/float | 0/0/0 | ○ | 同上（HDR 元数据组，建议合并为“HDR 提示参数”组） |
| `intrinsic_size` | 建议显示尺寸 | int x/y | 0=不写 | ○ | 容器元数据；极少数场景 |
| `frame_name` | 帧名 | string | "" | ○ | 会写入码流且**禁用 fast-lossless**（tooltip） |

> 计数：expert_options+effort 11 算 2 行、HDR 组算 4 行 ⇒ 约 14 行；`ec_resampling`/`alpha_distance`/`frame_bit_depth`/JPEG 重压缩 5 项/动画/预览等按既有裁定不暴露（alpha 不支持、无 JPEG 重压缩路径）。

### 4.3 JXL 不暴露清单（按类）

JPEG 重压缩族 5（`jpeg_recon_cfl`/`jpeg_compress_boxes`/`jpeg_keep_exif/xmp/jumbf`）、`already_downsampled`、`frame_index_box`、`store_jpeg_metadata`、动画/预览（`have_animation`/`have_preview`）、`alpha_distance`/`ec_resampling`（无 alpha 输出）、`frame_bit_depth`（输入语义，本管线恒一致）、`use_full_image_heuristics`（测试向）。理由：本产品只做“解码→float32→重编码”，不走 JPEG 重压缩/动图/预降采样。

---

## 5. 不暴露项总账（给“全暴露”一个可审计的定义）

| 库 | 可达/可编程面 | 本清单暴露 | 不暴露（分类） |
|---|---:|---:|---|
| x265 4.3 | 220 个 `x265_param_parse` 可达名 | ≈40 | 视频向 ~120（单帧无效果）、有害 ~15、文件副作用 ~12、死参/CLI-ONLY/构建不存在 ~30、线程池/并行风险 3（用户裁定不暴露线程池） |
| libaom 3.15.1 | 15 原生 + 125 个 `aom:` 名 | ≈50 | cfg 级不可达 ~55、视频向 ~21、调试/路径 ~17、禁止 ~6 |
| SVT-AV1 4.2.0 | 10 原生（121 名需补丁，**已裁定不打**） | 10 | 121 个 parse 名全部不可达（记录为库面限制） |
| libjxl 0.12.0 | 41 frame setting + 2 编码器函数 + BasicInfo/EC 面 | ≈42 | JPEG 重压缩 5、动画/预览/预降采样/测试向等 ~8、alpha 3 |

> 设计文档会把此表落成契约 C1/C2：“**全暴露 = 评审清单内 100% 参数化 + 未列入者逐类登记理由**”。任何后续“能否再加 X”都按此表增补，不再以“CLI 全量”为口径。

---

## 6. 裁定记录与仍待确认

**已裁定（用户，2026-10-28 交互问答）**

1. **参数范围**：按本评审稿精选清单定稿（x265 ≈40 / libaom ≈50 / SVT 10 / JXL ≈42）。
2. **清单外参数**：**完全封死**——未收录的 `x265:`/`aom:` 键 = 预检错误；老预设里手写冷门键会被拒绝并给中文定位。
3. **SVT-AV1**：不打 overlay 补丁；仅 10 原生键，121 个 parse 名登记为库面限制。
4. **SSIMULACRA 2 接入**：方案 **A 官方源码 + 兼容层**（BSD-3，算法/权重逐字保留）；B 仅作数值门禁失败时的 fallback。
5. **评分默认**：GUI **默认开，8 种输出全部评分**；参考=**量化后的编码器输入**（真无损=100）。
6. **线程池**：不是编码参数，不进入格式参数面（`x265:pools`/`frame-threads` 移除；不新增 HEIF 线程池设置）；交给软件默认调度，R-078 如实登记。
7. **工作流**：三阶段串行——①参数事实与目录 → ②引擎+UI → ③SSIMULACRA 2；阶段内并行；每阶段一个可验收增量。
8. **交付节奏**：每阶段提交/验收，最后统一收口。

**仍待确认**：无（等你 review 本清单后给“开工”即可；若对清单个别条目有增删，直接指出键名）。

---

## 7. 依据来源（摘要）

- x265 4.3：`source/common/param.cpp`（名字表/默认值/parse 范围）、`source/x265cli.h` / `doc/reST/cli.rst`（258 基名/27 CLI-ONLY/文档分类）；官方 CLI 文档 https://x265.readthedocs.io/en/master/cli.html
- libheif 1.23.5 插件：`libheif/plugins/encoder_x265.cc`（7 参数/透传/强制顺序/不可覆盖项）、`encoder_aom.cc`（15 参数/`aom:` 直通/最后应用）、`encoder_svt.cc`（10 参数/**无 `svt:`**）；https://github.com/strukturag/libheif
- libaom 3.15.1：`av1/av1_cx_iface.c`（`encoder_set_option` 125 名/`validate_config` 非法组合）、`av1/arg_defs.c`、`aom/aomcx.h`；https://aomedia.googlesource.com/aom
- SVT-AV1 4.2.0：`Source/API/EbSvtAv1Enc.h`、`Source/Lib/Globals/enc_settings.c`（`svt_av1_enc_parse_parameter` 121 名）；https://gitlab.com/AOMediaCodec/SVT-AV1
- libjxl 0.12.0：`lib/include/jxl/encode.h`（41 frame settings）、`lib/jxl/encode.cc`/`enc_*.cc`（约束/静默行为）；https://github.com/libjxl/libjxl/tree/v0.12.0
- 既有审计：`docs/encoder-config-audit-2026-10-25.md`（逐键实测矩阵）、`docs/encoder-audit-decisions-r2-2026-10-25.md`（alpha/TIFF 裁定）

---

## 8. 附：SSIMULACRA 2 为什么不是“直接拿 GitHub C++ 源码链接”

**事实**：GitHub 上确实有 C++ 源码（libjxl 0.12.0 `tools/ssimulacra2.cc`、`cloudinary/ssimulacra2`），但都**不是可链接的库**：

1. libjxl 的 `tools/ssimulacra2.{h,cc}` 依赖 `lib/jxl` **内部**头（`image_bundle.h`/`enc_xyb.h`/`gauss_blur`/`StatusOr`…），这些内部头**不在安装包**里；且它挂在 `JPEGXL_ENABLE_DEVTOOLS` 下、`install(TARGETS ${TOOL_BINARIES})` 不包含它 ⇒ vcpkg 的 libjxl 从不构建/安装它（安装树只有 `jxl/` 公共头 + 3 个 DLL）。
2. `cloudinary/ssimulacra2` 的公开 API 同样是 `jxl::ImageBundle`（内部类型），仓库把 **libjxl 0.8 源码整体 vendored** 进 `src/lib/`，没有 `find_package`/导出目标/`install()`；想用就要再编一份 libjxl 0.8，与本项目 libjxl 0.12 **符号/版本冲突**。
3. pinned vcpkg（2026.07.29）**没有 `ssimulacra2` port**；libjxl 的 devtools 也不被启用。

**因此“接入官方源码”只有三种工程形态**：

| 方案 | 做法 | 优点 | 缺点/风险 |
|---|---|---|---|
| **A（推荐）官方源码 + 兼容层** | 把 libjxl `tools/ssimulacra2.{h,cc}` + `gauss_blur.cc`（BSD-3，**逐字保留算法/权重/常量**）纳入 repo，写约 200–300 行兼容层替换它依赖的内部件（Image3F/ImageF、状态、Gaussian blur、ToXYB、lcms 线性化），换成我们的缓冲/流式实现 | 用的是官方 C++ 源码本体（非重写算法）；不新增依赖；可控内存（流式，参考实现 130–160 B/px→24MP 3.5GB 的问题消失）；可 AVX2；可复用参考准备 | 兼容层需数值校验（PyPI 独立实现 + 可选 libjxl 参考 CLI 做 oracle，容差 ≤0.1/≤0.01） |
| **B** overlay 编译官方内部子集 | 从 pinned libjxl 源码编 `ssimulacra2.cc + gauss_blur.cc + DEC 内部 TU + enc_xyb` 成静态库 + C ABI shim | 最贴近官方二进制 | 内部头不安装、子集编译（MSVC + hwy 1.4）**未验证**；参考实现峰值内存 ≈3.5GB/24MP、单线程 3–6s/24MP，仍要改源码才能进当前预算；每次 libjxl 升级要 rebase |
| **C** vendored 全 libjxl 0.8 | 用 cloudinary repo 自带源码编 | 源码现成 | 双 libjxl 版本冲突/体积暴涨，**否决** |

> 结论：**A 不是“自研算法”，而是“官方源码 + 适配/打包层”**；只在无法满足数值门禁时才升级到 B。无论 A/B，都会用 **PyPI `ssimulacra2`（BSD-3，独立实现）** 与（可选）dev-only 构建的 libjxl 参考 CLI 做数值对照，并把证据写进报告。
