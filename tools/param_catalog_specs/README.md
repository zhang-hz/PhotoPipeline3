# M8 参数目录 specs（人工维护 → 生成 .inc）

本目录的 4 个 JSON 是 `src/codecs/param_catalog/*_catalog.inc` 的唯一人工输入。
生成器：`python tools/gen_param_catalog.py`（`--check` 校验生成物最新；`--validate-only` 仅校验 JSON；`--strict-counts` 断言最终行数）。
契约：`docs/m8-encoder-params-and-ssimulacra2-design.md` C1/C2；形态/角色：`docs/m8-param-control-map.md`。

> **不要手改 `.inc`**；只改本目录 JSON，然后由 Lead 运行生成器。

## 1. 顶层结构

```json
{
  "format": "heif",
  "backend": "x265",
  "rows": [ { ...row... } ]
}
```

| 文件 | format | backend | 行数（Stage A 定稿） |
|---|---|---|---:|
| `x265.json` | `heif` | `x265` | 40（36 可设置 + 4 只读） |
| `aom.json` | `avif` | `libaom` | 57（51 可设置 + 6 只读） |
| `svt.json` | `avif` | `svt-av1` | 3（只读） |
| `jxl.json` | `jxl` | `libjxl` | 16（可设置） |
| 合计 | | | **116** |

## 2. row 字段

| 字段 | 必填 | 说明 |
|---|---|---|
| `key` | 是 | GUI/ParamSet 键；见 §3 前缀规则 |
| `engine` | 可设置行必填 | 透传引擎名（`x265:`/`aom:` 后的名字）；只读行留空 |
| `label` | 是 | 中文标签（≤10 汉字优先） |
| `type` | 是 | `int` / `float` / `bool` / `enum` / `text` |
| `lo` / `hi` / `step` | int/float 必填 | 数值域；`lo<=hi`，`step>0` |
| `choices` | enum 必填 | `[["显示名", 值], ...]`；值可为 string/int/float/bool；显示名唯一 |
| `def` | 否 | 默认显示值：数值为数字串、bool 为 `"0"/"1"`、enum 为某个 choices 显示名、text 为空串。**新增行不参与默认下发**（生成器固定 `has_default=false`） |
| `form` | 是 | `mode` / `number` / `dropdown` / `switch` / `text` / `readonly` |
| `role` | 是 | `anchor` / `dependent` / `independent` |
| `scope` | 是 | Stage A 只允许 `settable` / `readonly` |
| `tech` | 否 | `both`（默认）/ `vardct` / `modular`；仅 JXL |
| `group` | 是 | 九个词表之一（见 §4） |
| `rules` | 否 | 规则 ID 逗号分隔（如 `"X1,X5"`）；B1 消费 |
| `reason` | 是 | 一句话“为什么这样映射/为什么重要” |
| `tooltip` | 可设置行必填 | 中文解释 + 英文/库常量；写清联动后果 |

### 形态 ↔ 类型一致性（生成器强制）

- `form=number` ⇔ `type=int|float`
- `form=dropdown|mode` ⇔ `type=enum`（mode 行通常是 `buffering`/`output_mode` 这类码流模式）
- `form=switch` ⇔ `type=bool`
- `form=text` ⇔ `type=text`（仅 `frame_name`/`intrinsic_size`）
- `form=readonly` ⇔ `scope=readonly` 且 `type=enum`（choices 单条，显示当前生效值/推导说明）

## 3. key / engine 规则

- HEIF/x265 可设置：`x265:<engine>`；只读：`__x265:<name>`；engine 必须出现在 x265 4.3 `x265_param_parse` 名字表（生成器抽取锚点校验）。
- AVIF/libaom 可设置：`aom:<engine>`；只读：`__aom:<name>`；engine 必须在 libaom 3.15.1 `aom_codec_set_option` 名字表（锚点校验）。
- AVIF/SVT 只读：`__svt:<name>`；**任何 `svt:` 可设置键都不允许**（无透传通道）。
- JXL：无前缀小写 snake（`noise` 等）；无 engine。
- 生成器拒绝 x265 禁设名：`annexb/input-csp/total-frames/ctu/log-level/log/input-res/pools/frame-threads/preset/tune/profile/qpfile/zonefile/abr-ladder/...`（完整集合见脚本）。
- 生成器拒绝 aom 禁设名：`input-chroma-subsampling-*/denoise-*/passes/film-grain*/tune/end-usage/usage/profile/bit-depth/lag-in-frames/kf-*/resize-*/superres-*` 等（见脚本）。

### 锚点来源（pinned 源码，只读）

- x265 4.3：`vcpkg/buildtrees/x265/src/4.3-*/source/common/param.cpp`（`x265_param_parse` 内 `OPT("name")` / `OPT2("a","b")`）
- libaom 3.15.1：`vcpkg/buildtrees/aom/src/66f4d0fa1d-*/av1/arg_defs.c`（`ARG_DEF(NULL, "name", ...)`）；`av1/av1_cx_iface.c` 的 `encoder_set_option` 消费
- libjxl 0.12：`vcpkg/buildtrees/libjxl/src/v0.12.0-*/lib/include/jxl/encode.h`

> 域/默认值以 pinned 源码 + `docs/m8-param-exposure-review.md` 为准；不确定的域在 `reason` 写明“待 B1 源码核对”。

## 4. 分组词表（九选一）

`质量/码率` · `分析工具` · `心理视觉` · `量化/变换` · `环路滤波` · `分块/线程` · `码流/兼容性` · `调试/实验` · `只读（上游固定）`

## 5. 行清单（Stage A 必须一行不少）

### x265.json（40）

**可设置（36）**

- 质量/码率（14）：`x265:crf`(★anchor) `x265:qp`(anchor) `x265:qpmin` `x265:qpmax` `x265:aq-mode`(dropdown) `x265:aq-strength` `x265:qcomp` `x265:psy-rd` `x265:psy-rdoq` `x265:rdoq-level`(dropdown) `x265:rd` `x265:rd-refine`(switch) `x265:cu-lossless`(switch) `x265:ssim-rd`(switch)
- 量化/变换（13）：`x265:qg-size`(dropdown) `x265:cbqpoffs` `x265:crqpoffs` `x265:min-cu-size`(dropdown) `x265:max-tu-size`(dropdown) `x265:limit-tu` `x265:fast-intra` `x265:splitrd-skip` `x265:signhide` `x265:strong-intra-smoothing` `x265:tskip` `x265:tskip-fast` `x265:lowpass-dct`
- 环路滤波/心理视觉（5）：`x265:sao`(switch anchor) `x265:sao-non-deblock` `x265:limit-sao` `x265:selective-sao`(dropdown) `x265:no-deblock`
- 码流/兼容（4）：`x265:level-idc`(dropdown anchor) `x265:high-tier` `x265:allow-non-conformance` `x265:wpp`

**只读（4）**：`__x265:profile` `__x265:keyint` `__x265:input-csp` `__x265:total-frames`

### aom.json（57）

**可设置（51）**

- 质量/码率（16）：`aom:cq-level` `aom:aq-mode` `aom:deltaq-mode` `aom:deltaq-strength` `aom:enable-chroma-deltaq` `aom:enable-qm` `aom:qm-min` `aom:qm-max` `aom:sharpness` `aom:enable-adaptive-sharpness` `aom:dist-metric` `aom:disable-trellis-quant` `aom:quant-b-adapt` `aom:min-cr` `aom:sb-qp-sweep` `aom:auto-intra-tools-off`
- 环路滤波（4）：`aom:enable-cdef` `aom:enable-restoration` `aom:loopfilter-control` `aom:enable-dual-filter`
- 分区/变换（13）：`aom:enable-rect-partitions` `aom:enable-ab-partitions` `aom:enable-1to4-partitions` `aom:min-partition-size` `aom:max-partition-size` `aom:sb-size` `aom:enable-intra-edge-filter` `aom:enable-tx64` `aom:enable-flip-idtx` `aom:enable-rect-tx` `aom:reduced-tx-type-set` `aom:use-intra-dct-only` `aom:use-intra-default-tx-only`
- 帧内工具（8）：`aom:enable-palette` `aom:enable-cfl-intra` `aom:enable-angle-delta` `aom:enable-filter-intra` `aom:enable-smooth-intra` `aom:enable-paeth-intra` `aom:enable-directional-intra` `aom:enable-diagonal-intra`
- 分块/线程（3）：`aom:tile-columns` `aom:tile-rows` `aom:row-mt`
- 码流/兼容（7）：`aom:target-seq-level-idx` `aom:strict-level-conformance` `aom:set-tier-mask` `aom:force-video-mode` `aom:timing-info` `aom:cdf-update-mode` `aom:tune-content`

**只读（6）**：`__aom:g-usage` `__aom:rc-end-usage` `__aom:g-limit` `__aom:lag-in-frames` `__aom:kf-disabled` `__aom:profile`

### svt.json（3，只读）

`__svt:profile` `__svt:level` `__svt:tier`

### jxl.json（16）

`noise` `group_order_center_x` `group_order_center_y` `upsampling_mode` `uses_original_profile` `box_compress` `expert_options` `buffering` `output_mode` `disable_perceptual_heuristics` `intensity_target` `min_nits` `relative_to_max_display` `linear_below` `intrinsic_size` `frame_name`

## 6. 验证

```bash
python tools/gen_param_catalog.py --validate-only
```

不写文件、只校验 JSON + 锚点；失败会给出 `文件[行] 原因`。
