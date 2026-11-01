# 自适应质量策略（Adaptive Policy）用户手册

> **适用版本**：0.4.0（M10 需求 11 / 契约 **C24**）。**默认关闭**。
> **真源链**：设计 = [`docs/v0.4.0-design.md`](v0.4.0-design.md) §9（§9.1 语义 / §9.2 DSL / §9.4 键 / §9.5 成本 / §9.7 默认库）；
> 验收 = `build/m10/w3-core/report.md`（DSL/AST/42 条错误码/5 条默认策略/预置 JSON 承载，W3-T3）；
> 实现真源 = `src/core/adaptive.cpp`（`default_adaptive_policies()`、`adaptive_error_codes()`、
> `adaptive_forbidden_keys()`、`render_adaptive_policy()`）。
> **相关文档**：[格式预设用户手册](format-presets.md) · [参数清单](param-catalog.md) · [M10 收口报告](m10-report.md) ·
> [策略 AST JSON Schema](schemas/adaptive-policy.v1.schema.json) · [格式预设 JSON Schema](schemas/format-preset.v1.schema.json) ·
> [工作流任务卡](m10-workflow.md) · [M10 裁定登记](m10-adjudications.md)

---

## 1. 这是什么（以及它不是什么）

自适应策略是一段**跟着格式预设走**的文本：它让程序在编码时**多试几组参数**，每试一轮就用
SSIMULACRA 2 给产物打个分，分数达到目标就停；没达到就输出**最后一轮成功编码**的结果。

| 项 | 口径 |
|---|---|
| 默认状态 | **关闭**。未载入带 `adaptive.enabled=true` 的格式预设、或策略文本为空 ⇒ 行为与 0.3.x **逐字节相同** |
| 作用域 | **每 (文件 × 输出) 独立**；只改该输出的 `ParamSet` 副本，不碰其它输出、不碰全局项 |
| 策略存放 | **只存在预设文件里**（格式预设的 `adaptive.policy`，或全局预设 v2 `outputs[i].adaptive.policy`；见 §11、§11.1） |
| GUI | **没有策略编辑器**：只有只读摘要 +「启用」开关；写法 = 编辑预设 JSON（见 §12）。原话裁决见设计 §15 |
| 策略语言 | 行式 DSL **v1**（§4）；脚本可用 JSON AST 导入（§14），导出仍只有 DSL 文本（避免双真源） |
| 评分依赖 | 自适应**必须评分**：即使设置里关了质量评分，启用自适应的输出仍会计算评分（UI 摘要注明「自适应需要评分」） |
| 不做什么 | 不改编码器线程策略、不改色彩/位深/分辨率/元数据、不做任何图像处理（负面清单不扩义） |

---

## 2. 快速上手（4 步）

1. 打开（或新建）某个格式的预设文件，例如
   `%APPDATA%\PhotoPipeline\presets\formats\heif\我的 HEIF.json`
   （便携模式下是 `<exe 目录>\presets\formats\heif\我的 HEIF.json`；路径规则见 [格式预设手册](format-presets.md) §2）。
2. 在根对象里加 `adaptive` 对象，把策略文本写进 `policy`（字符串内的换行写成 `\n`），并把 `enabled` 设为 `true`。
3. 在 PageOutput 的该格式页签里选中这个预设 ⇒ 载入时**先校验策略**：不合法会给出「第 L 行第 C 列：原因；建议（错误码）」并**拒绝载入**（保留当前卡值，绝不静默套默认）。
4. 开始运行。启用自适应的输出行会显示「自适应 3/8」这类轮次进度；日志里出现 `adaptive round` / `adaptive summary`（§13）。

> 只想先看效果：把 `max_rounds` 设成 `2`、`target` 设成 `85`，跑一张小图。

---

## 3. 术语（读完 §4 前先扫一眼）

| 词 | 含义 |
|---|---|
| 轮（round） | 应用一组参数 → 编码到候选临时文件 → 评分 → 决策。一轮只产出一个候选 |
| stage（阶段） | 一组动作（`search` / `set`）；阶段内动作**按书写顺序**执行，一个阶段跑完（或达标）再进下一个 |
| `current` | 搜索起点 = **运行配置当前值**（即该格式卡上此刻的值），不是上一轮的值 ⇒ 每次执行都从同一起点出发（幂等） |
| 达标 | `score >= target`（默认 90，浮点按 ≥ 判定，SSIMULACRA 2 分数） |
| last | 未达标时输出**最后一轮成功编码**的结果（本版唯一支持） |
| 候选临时文件 | 每轮的编码产物，落在最终输出同目录（同目录唯一名），**不写元数据**（评分只看像素）；选定后才 commit 到最终路径 |

---

## 4. 语法（DSL v1，EBNF 逐字）

以下 EBNF 与设计 §9.2 **逐字一致**：

```ebnf
policy      = { option } , { stage } ;
option      = "version" INT                 (* 省略=1 *)
            | "target" NUMBER               (* (0,100]，默认 90 *)
            | "max_rounds" INT              (* 1..32，默认 8 *)
            | "output" ( "last" | "best" )  (* 默认 last；best=P1 *)
            | "on_metric_unavailable" ( "stop" | "fail" )  (* 默认 stop *)
            | "plateau" NUMBER ;            (* 0=关；>0=连续 2 轮改善<阈值即止 *)
stage       = "stage" IDENT NL { action } ;
action      = "search" KEY "from" VALUE "to" VALUE "step" NUMBER
              [ "method" ( "linear" | "bisect" | "exp" | "greedy" ) ] [ "max_rounds" INT ]
            | "set" KEY VALUE ;
KEY         = 目录内参数键 | "profile" | "chroma" | "tech"  (* 伪参数 *)
VALUE       = NUMBER | true | false | ENUM_NAME | "字符串" ;
IDENT       = [a-z][a-z0-9_-]{0,31} ;
comment     = "#" 到行尾 ;
```

**落地口径（与上面摘要的差异，均已在实现与单测钉住）**：

- `search` 的 `from` 实际只接受 **`current` 或数值**（`to` / `step` 必须数值）；EBNF 里写成 `VALUE` 是摘要写法。
- `search` 子句顺序固定：`from … to … step …` → 可选 `method …` → 可选 `max_rounds N`；顺序颠倒或多写标记 = `stray_token` / `search_syntax`。
- `KEY` 的 `tech` 伪参数**默认禁止**（`tech_not_supported`，设计 §9.4 归为 P2）；`chroma` 按结构键处理（只能 `set`）。
- `set` 的 `VALUE`：整数 → `int64`，带小数点 → `double`，`true`/`false` → 布尔，裸词 → 枚举名（也接受中文显示名 / `…（N）` 标签），`"字符串"` → 字符串（转义 `\\` `\"` `\n` `\t` `\r`）。
- **注释**：`#` 到行尾；字符串里的 `#` 不算注释。**缩进不是语义**（`stage` 行开始新阶段），空行自由。
- **大小写**：关键字一律小写；枚举值按参数目录的 choices 匹配（引擎值或中文显示名均可）。
- **错误定位**：1-based `line` / `col`（来自 JSON AST 导入的错误，`line` / `col` 记 0，按字段名定位）。
- **规范化渲染**：固定写全 6 个选项 + 空行 + `stage` + 两空格缩进的动作；`method` 只在非 `linear` 时写，动作级 `max_rounds` 只在非 0 时写；满足 `render(parse(render(p))) == render(p)`。日志/侧车里的策略 SHA 用规范化文本复算。

---

## 5. 六个全局选项（必须写在所有 `stage` 之前）

| 选项 | 取值 | 默认 | 语义 |
|---|---|---|---|
| `version` | `1` | `1` | 策略版本；`>1` = `unsupported_version`（不静默按旧语义执行），`<1` 同样拒绝 |
| `target` | `(0, 100]` 浮点 | `90` | 目标分（SSIMULACRA 2）；90 ≈ 视觉透明档 |
| `max_rounds` | `1..32` 整数 | `8` | **全局轮数上界**（三层终止保证之一）；预设文件里可配 |
| `output` | `last` / `best` | `last` | 未达标时输出哪一轮；**`best` 在本版校验期显式拒绝**（`output_best_unsupported`，属 P1，不静默当 `last`） |
| `on_metric_unavailable` | `stop` / `fail` | `stop` | 评分不可用（回读失败/尺寸不符/取消）时：`stop` = 停止迭代、输出最后成功轮、metric 记 Unavailable；`fail` = 该输出失败（`error="adaptive: metric unavailable"`） |
| `plateau` | `>= 0` 浮点 | `0`（关） | `>0`：**连续 2 轮**改善 < 阈值即提前终止（失败终止，输出最后一轮） |

> 省略即默认。重复选项 = `duplicate_option`；写在 `stage` 之后 = `option_after_stage`。

---

## 6. 两类动作

### 6.1 `search`（数值搜索）

```
search <KEY> from (current|<数值>) to <数值> step <数值> [method linear|bisect|exp|greedy] [max_rounds N]
```

- 方向由 `from` / `to` 的相对大小决定（`dir = sign(to - from)`）；端点含 `to`，并按参数域钳位。
- `from == to`（两者都是显式数值）= `search_noop`；`step <= 0` = `step_range`。
- `max_rounds`（动作级，`1..32`）= 该动作的轮数上限；省略/0 = 继承全局。
- 只有 **6 个数值质量键**可被 `search`（白名单见 §8.1）；其余一律 `not_searchable`。

| method | 候选序列 | 每阶段上界（设计 §9.5） |
|---|---|---|
| `linear`（默认） | `start + dir*k*step`，钳到参数域与端点 | ≤16 |
| `bisect` | 端点"差/好"已知时按分数二分，直到区间 `< step` 或达标 | ≤18 |
| `exp` | 步长 × factor（默认 1.5，上限 4） | 受 `max_rounds` |
| `greedy` | 朝目标步进；无改善则步长减半，低于最小步长停止 | 受 `max_rounds` |

### 6.2 `set`（一次性赋值）

```
set <KEY> <VALUE>
```

- 支持两类键：该格式参数目录里**可设的键**（减去 §8.3 禁止表），以及伪参数 `profile`（仅 HEIF/AVIF）。
- 每个 `set` = **1 轮**；一个 `stage` 里多个 `set` 会各占 1 轮、按顺序串起来（`set` 阶段的参数是累积的：后一轮继承前一轮）。
- `set` 值按目标参数的类型/枚举/范围校验（`set_value_type` / `set_value_range` / `enum_value`）。

---

## 7. 三个完整示例

### 示例 1 · HEIF 用户场景（先提质量，再上 4:4:4）

```
# 我的 HEIF 照片策略：先提质量，再上 4:4:4
version 1
target 90
max_rounds 8
output last
on_metric_unavailable stop

stage quality-up
  search quality from current to 98 step 2

stage chroma-444
  set chroma 444
  set profile Main444_10Still
```

读法：先把 `quality` 从当前值按 +2 往上试，直到达标或到 98；没达标就进 `chroma-444` 阶段，
把色度切 4:4:4、档位切 `Main444_10Still`（会各花 1 轮）；全部跑完仍未达标 ⇒ 输出最后一轮。
（与设计 §9.2「完整示例 1」逐字一致；也是 HEIF 出厂默认策略的主体，见 §10。）

### 示例 2 · JXL 距离扫描（线性 → 二分）

```
version 1
target 90
max_rounds 10
output last
plateau 0.05

stage distance-scan
  search distance from current to 0.05 step 0.1

stage distance-bisect
  search distance from current to 0.05 step 0.02 method bisect
```

读法：JXL 的 `distance` 越小质量越高、体积越大。第一段用 0.1 的步长粗扫，第二段用 0.02 步长
带二分逼近 0.05；`plateau 0.05` 表示连续 2 轮改善都小于 0.05 分就提前收手（避免磨时间）。

### 示例 3 · 一次性兜底配置（只有 1 个阶段，最多 3 轮）

```
target 90
stage fallback
  set chroma 444
  set x265:psy-rd 2.0
  set x265:aq-mode 2
```

读法：不写选项 ⇒ 用默认（`version 1` / `target 90` / `max_rounds 8` / `output last` / `on_metric_unavailable stop` / `plateau 0`）。
只做一次 4:4:4 + 两个 x265 透传键的兜底尝试；达标即停，最多 3 轮（3 个 `set`）。
注意 `x265:psy-rd` / `x265:aq-mode` 不在 §9.4 的**示例表**里，但它们在 HEIF 插件的透传面/目录面上是**合法键**，
因此可用（这条已由单测钉住：示例 3 合法，`effort` 不可 search）。

---

## 8. 允许 / 禁止的目标键（正交性核心）

### 8.1 `search` 白名单（闭表，6 键）

| 键 | 允许的格式 | 典型区间（目录静态域；无条目时用兜底域） |
|---|---|---|
| `quality` | heif / avif / webp / jpeg | 0–100 |
| `distance` | jxl / jpeg | 0–25 |
| `x265:crf` | heif | 0–51（10 位 −12..51、12 位 −24..51 由执行期按位深复核） |
| `x265:qp` | heif | 0–51（同上） |
| `aom:cq-level` | avif | 0–63 |
| `qp` | avif | 0–63 |

> 白名单之外的键做 `search` ⇒ `not_searchable`。理由：搜索语义要求候选是单调数值；枚举/布尔/文本只能 `set`。

### 8.2 `set` 的允许面（数据驱动）

**允许面 = 该格式参数目录（全部后端×技术并集；heif/avif 含运行时内省）中可设的键 − 禁止表**，
外加显式类别键：`chroma` / `preset` / `tune` / `epf` / `gaborish` / `dots` / `patches` /
`modular_predictor` / `modular_group_size` / `color_transform`，以及伪参数 `profile`（仅 heif/avif）。

| 格式 | 可 `search` | 可 `set`（举例） | 伪参数 `profile` |
|---|---|---|---|
| heif | `quality`、`x265:crf`、`x265:qp` | `chroma`、`preset`、`tune`、`x265:*` 透传键 | ✔（HEVC 档位名/编号） |
| avif | `quality`、`aom:cq-level`、`qp` | `chroma`、`preset`、`tune`、`aom:*` 透传键 | ✔（AV1：Main/High/Professional） |
| jxl | `distance` | `epf`、`gaborish`、`dots`、`patches`、`modular_predictor`、`modular_group_size`、`color_transform` | ✘（`profile_key_format`） |
| webp | `quality` | `method`、`preset`、`sns_strength` … | ✘ |
| jpeg | `quality`、`distance` | `chroma` … | ✘ |
| png / tiff / bmp | 无（无损，评分≈100） | 目录内可设键 − 禁止表 | ✘ |

> 键在**别的格式**目录里有、本格式没有 ⇒ `key_not_in_format`；哪个目录都没有 ⇒ `unknown_key`。
> `tech` 伪参数默认禁止（`tech_not_supported`）：改编码技术会让"评分参考"与"参数面"同时漂移，P2 才考虑。

### 8.3 禁止表（硬拒绝，与格式无关；`forbidden_key`）

共 **40 个键**（任一出现在 `search`/`set` 目标上即失败），另有通配规则：**一切 `__` 前缀保留键**同样硬拒绝
（`profile` 用伪键 `profile` 表达，不要写 `__heif_profile`）。

| 类别 | 键（逐字） | 为什么禁止 |
|---|---|---|
| 无损 / 重采样 / 缩放 | `lossless`、`resampling`、`upsampling_mode` | 改评分参考维度（分辨率/是否有损），"优化"不再正交 |
| HDR / 亮度口径 | `intensity_target`、`min_nits`、`relative_to_max_display`、`linear_below` | 直接改像素与评分口径 |
| Alpha / 噪声 / 感知启发 | `keep_invisible`、`noise`、`photon_noise`、`disable_perceptual_heuristics` | 改参考像素或绕过感知模型 |
| 专家门 / 容器语义 | `expert_options`、`codestream_level`、`buffering`、`output_mode`、`frame_name`、`intrinsic_size` | 容器/码流结构，非画质旋钮 |
| 位深（格式级结构键） | `bitdepth`、`out_bitdepth`、`bit_depth` | 位深由格式预设字段承载，迭代中固定 |
| 全局项 | `color_target`、`color_intent`、`color_dither8`、`gamut_mapping`、`linear_resample`、`metadata_only`、`conflict`、`rules`、`output_template`、`split_by_format`、`outputs` | 属全局预设职责；策略不得改色彩/冲突/模板/元数据 |
| 预设结构字段 | `kind`、`version`、`name`、`format`、`backend`、`reserved`、`adaptive`、`policy`、`params` | 不是参数，是文件/预设结构 |

（上表合计 40 键；实现在 `src/core/adaptive.cpp` 的 `adaptive_forbidden_keys()`，单测对**全表**逐项硬拒绝。
设计 §9.4 的"禁止表"一行是同一集合的摘要。）

---

## 9. 执行语义（逐条可测试）

1. **启用**：`adaptive.enabled = true` **且** `policy` 非空。载入预设后格式卡显示只读摘要
   （`含自适应策略：目标 90 / 最多 8 轮 / 3 个阶段` + 启用开关）；未载入预设 / 策略为空 = 关闭。
2. **作用域**：每 (文件 × 输出) 独立；只改本输出的 `ParamSet` 副本；策略**不得**改全局项与位深。
3. **一轮** = 应用参数 → 编码到候选临时文件 → 评分 → 决策；编码走既有预检/规则；候选文件不写元数据。
4. **终止**（三种成功/失败出口）：
   - **达标即停**：`score >= target` ⇒ 立即终止**后续所有阶段**（包括同阶段剩余动作）；
   - **阶段耗尽**：当前 stage 动作跑完未达标 ⇒ 进下一个 stage；**没有下一个 stage ⇒ 失败终止**；
   - **轮数触顶**：累计轮数达到 `max_rounds` ⇒ 失败终止；
   - **plateau**：连续 2 轮改善 < 阈值 ⇒ 失败终止；
   - 取消 / 编码失败 / 预检失败 / 评分不可用 ⇒ 见下条与 §9.7。
5. **输出（last）**：失败终止输出**最后一轮成功编码**；选定候选 `commit_temp_file` 到最终路径，然后才走
   既有 metawrite/mtime 段（`encode → score → metawrite` 只对最终文件一次，与现状一致）。
6. **取消丢弃**：轮间检查取消 ⇒ 删除**全部**候选、该输出 `Cancelled`、**不落盘**（与现状取消语义一致）。
7. **评分不可用**：默认 `stop`（停止迭代、输出当前最后成功轮、metric 记 `Unavailable`）；`fail` ⇒ 该输出失败。
8. **评分开关**：自适应必须评分（见 §1）；非自适应输出不受影响。
9. **确定性**：不改编码器线程策略；同输入 + 同策略 + 同配置 ⇒ 同结果；策略文本 SHA256 前 16 位进日志便于比对。
10. **并行**：自适应在 worker 线程内**串行**执行（每轮串行）；不同文件 / 不同输出之间照常并行（与多格式并行正交）。
11. **即时成功边界**：无损格式或评分≈100 ⇒ 立即成功（0 轮）；<8×8 或回读失败 ⇒ 走评分不可用路径。

---

## 10. 内置默认策略库（5 条全文）

`default_adaptive_policies()` 编译进程序（`src/core/adaptive.cpp`）+ `src/core/format_preset.cpp` 的
`builtin_format_presets()` 把策略逐字装进 5 条内置格式预设（设计 §9.7）；**出厂文件不在安装目录**
（无 `share/presets/**`，见 D-8），由首次运行的**幂等 seed** 落到数据目录（见本节末注）。
**PNG / TIFF / BMP 不提供**策略（无损，评分≈100，策略无意义；预设里 `adaptive` 永远为空）。

**HEIF**

```
# HEIF 出厂策略（设计 §9.7 / §9.2 示例 1）：先把质量拉到 98，再切 4:4:4 / still 档位。
# 目标 90 = SSIMULACRA 2 视觉透明档；未达标输出最后一轮。
version 1
target 90
max_rounds 8
output last
on_metric_unavailable stop

stage quality-up
  search quality from current to 98 step 2

stage chroma-444
  set chroma 444
  set profile Main444_10Still
```

**AVIF**

```
# AVIF 出厂策略（设计 §9.7）：quality 搜索 → 4:4:4 → tune psnr 兜底。
version 1
target 90
max_rounds 8
output last
on_metric_unavailable stop

stage quality-up
  search quality from current to 95 step 2

stage chroma-444
  set chroma 444

stage tune-psnr
  set tune psnr
```

**JXL**

```
# JXL 出厂策略（设计 §9.2 示例 2）：distance 线性扫描 → 二分逼近。
version 1
target 90
max_rounds 10
output last
plateau 0.05

stage distance-scan
  search distance from current to 0.05 step 0.1

stage distance-bisect
  search distance from current to 0.05 step 0.02 method bisect
```

**WebP**

```
# WebP 出厂策略（设计 §9.7）：quality 搜索 → method（压缩努力）提升。
version 1
target 90
max_rounds 8
output last
on_metric_unavailable stop

stage quality-up
  search quality from current to 95 step 2

stage method-up
  set method 6
```

**JPEG**

```
# JPEG 出厂策略（设计 §9.7）：jpegli quality 搜索。
version 1
target 90
max_rounds 8
output last
on_metric_unavailable stop

stage quality-up
  search quality from current to 95 step 2
```

> 出厂文件 = **只读模板**（`builtin=true`）：要改就「另存为」到自己的名字再编辑。
> 首次运行由**运行时幂等 seed** 把 5 条内置预设写到 `<data_dir>/presets/formats/<format>/_builtin-<format>.json`
> （只补不存在的那条，已存在一个字节都不动；D-8 —— 发行包**不含** `share/presets/` 出厂文件与安装规则）。
> 详见 [格式预设手册](format-presets.md) §7。

---

## 11. 在格式预设 JSON 里携带策略

`adaptive` 是预设根对象里的一个可选对象（两个字段，`enabled` 与 `policy`）：

```json
"adaptive": {
  "enabled": true,
  "policy": "version 1\ntarget 90\nmax_rounds 8\noutput last\non_metric_unavailable stop\n\nstage quality-up\n  search quality from current to 98 step 2\n\nstage chroma-444\n  set chroma 444\n  set profile Main444_10Still\n"
}
```

| 字段 | 类型 | 缺省 | 规则 |
|---|---|---|---|
| `adaptive.enabled` | bool | `false` | `true` 时 **必须**有非空 `policy`（否则保存/载入被阻断） |
| `adaptive.policy` | string | 空 | 行式 DSL v1 文本；**≤16 KiB**（`policy_too_large`）；保存/载入/开始运行三处同一套 parse+validate |

- 完整预设示例见 [格式预设手册](format-presets.md) §3（含 `kind`/`version`/`params`/`reserved` 的整份文件）。
- `enabled=false` 且 `policy` 为空 ⇒ **不写** `adaptive` 字段（旧文件零变化，C25）。

### 11.1 全局预设也携带同一个块（A20）+ dev CLI 两条路径（A14）

格式预设与**全局预设 v2 的 `outputs[i]`** 支持**同一个** `adaptive` 块（加性；两者**互不叠加**，
最后一次显式载入/开关决定生效态）：

| 面 | 实际行为 |
|---|---|
| 全局预设写盘 | `outputs[i].adaptive = {enabled, policy}`；空块**不写**字段 ⇒ 旧全局预设逐字节不变（C25） |
| 全局预设读盘 | 缺省 = `enabled=false` / 空策略（自适应关闭，行为与 0.3.x 相同）；`adaptive` 非对象 / `enabled` 非布尔 / `policy` 非字符串 ⇒ **显式报错**（不静默当关闭） |
| 引擎判据 | 仍是 **`adaptive_policy` 非空**；`adaptive_enabled` 只是启用状态/持久化面（`OutputFormatSpec` 末尾加性，默认 `false`） |
| GUI 门控 | `PageOutput::config_base()` 按 `adaptive_enabled && 策略非空 && 解析通过` 才把策略交给引擎；关 = 走既有单次编码路径（产物逐字节不变） |
| `--dev --preset FILE` | 按根 `kind` 分派：`kind="format-preset"` ⇒ 格式预设（施加到输出 #0）；含 `outputs` ⇒ 全局预设 v2。**两条路径都消费 `adaptive`**（全局预设分支由 A14 收口补丁加性 3 行补齐；无 `adaptive` 的旧预设行为逐字节不变） |
| `--dev --adaptive-policy FILE` | 直接给策略 DSL 文件；与预设内策略**互斥**（同时给出 ⇒ exit 2；报错文案仍写 "format preset's adaptive policy" —— 行为正确，dev-only 措辞未区分来源） |
| 输出数量约束 | 策略要求**恰好 1 个输出**（`--dev` 多输出 + 策略 ⇒ 显式错误；自适应按 (文件 × 输出) 作用域，不在本版做多输出分发） |

---

## 12. 没有 GUI 编辑器：怎么写、怎么改

| 场景 | 做法 |
|---|---|
| 新建策略 | 复制一个已有预设 JSON → 改文件名（= 显示名）→ 改 `name` 与 `adaptive` → 重新载入该预设 |
| 改策略 | 直接编辑 `adaptive.policy` 字符串（注意 `\n` 是换行、`\"` 是引号）；保存后重新选中预设触发校验 |
| 校验失败 | 载入被拒绝并给出「第 L 行第 C 列：原因；建议（错误码）」；**当前卡值保持不变**（不静默套默认） |
| 合法性自查 | 对照 §9 错误码表；或先用 JSON AST 导入通道（§14）让脚本生成、再由程序渲染成 DSL |
| 想比对版本 | 日志里的 `policy_sha` 是**规范化文本**的 SHA256 前 16 位（注释/缩进不同不影响比对） |

> 为什么没有编辑器：策略是"配置质量目标"的可审计文本，用户裁决把编辑面留在文件里（设计 §9 引言、§15），
> GUI 只做只读摘要 + 开关，避免再造一套会与 DSL 语义漂移的表单。

---

## 13. 日志、进度与侧车（可复算）

启用自适应后，`--dev` / GUI 运行日志新增（info 级，仅启用时）：

```
[info] [pipeline] adaptive round {src=… out=… output_index=0 round=2 stage=quality-up
        key=quality value=96 score=91.2 state=ok bytes=… encode_ms=… metric_ms=… decision=met}
[info] [pipeline] adaptive summary {src=… rounds=3 target=90 selected=round2
        policy_sha=… final_score=91.2 overhead_ms=…}
```

- **进度**：`ProgressInfo` 加性字段 `adaptive_round` / `adaptive_max`，运行页显示「自适应 3/8」；overall 不虚增。
- **侧车**：`<产物>.pp.json` 加性对象
  `"adaptive": {policy_version, target, max_rounds, output, rounds:[…], selected_round, final_score, reason}`。
- 逐轮记录含参数值 / 分数 / 体积 / 耗时 / 决策，**可离线复算**"为什么选了这一轮"。

---

## 14. JSON AST 导入（脚本生成用；不落盘）

`docs/schemas/adaptive-policy.v1.schema.json` 是**导入面**：脚本可以生成 JSON AST，程序转成 `AdaptivePolicy`
后再渲染成 DSL；**导出/落盘仍然只有 DSL 文本**（避免双真源）。

```json
{
  "version": 1,
  "target": 90,
  "max_rounds": 8,
  "output": "last",
  "on_metric_unavailable": "stop",
  "plateau": 0,
  "stages": [
    { "id": "quality-up",
      "actions": [
        { "kind": "search", "key": "quality", "from": "current", "to": 98, "step": 2 }
      ] },
    { "id": "chroma-444",
      "actions": [
        { "kind": "set", "key": "chroma", "value": "444" },
        { "kind": "set", "key": "profile", "value": "Main444_10Still" }
      ] }
  ]
}
```

- 根：`{version?, target?, max_rounds?, plateau?, output?, on_metric_unavailable?, stages[]}`（`stages` 必填 ≥1）。
- `search` 动作：`{kind:"search", key, from, to, step, method?, max_rounds?}`；`from` = 数值或 `"current"`。
- `set` 动作：`{kind:"set", key, value}`；`value` = 数字 / 布尔 / 字符串。
- 导入后**仍走同一套 `validate`**（§9、§8），AST 里的非法键不会比 DSL 更宽松；错误码前缀为 `json_syntax` / `json_field`。

---

## 15. 成本模型（每轮 encode + score）

| 图像量级 | 每轮 encode | 每轮 score | 每轮合计（实测口径，设计 §9.5） |
|---|---|---|---|
| 2 MP | 0.5–1.0 s | 0.63–1.03 s | **1.3–1.8 s** |
| 22.8 MP（23 MP） | 3–6 s | 1.5–3 s（流式后） | **5–9 s** |

- **最坏耗时 ≈ `max_rounds × 单轮`**：`max_rounds=8` 时最坏 ≈ 8 轮；`plateau` 与「达标即停」通常远早于触顶。
- 三层终止保证：全局 `max_rounds`（≤32）+ 每 stage 上界（≤18）+ `set` 每键 1 轮；`plateau` 额外提前终止。
- 0.4.0 实测参考（见 §16）：23 MP 单次评分 **1.572 s**（W1-T4 流式化），评分色彩归一 8.49 s → **0.42 s**（W1-T2c），
  因此 22.8 MP 的每轮成本比设计估值更低；具体以本机日志 `encode_ms` / `metric_ms` 为准。
- UI/日志披露原则：**不显示的耗时 = 未发生的量**；无法估算时显示「—」而不是编造。

**启用前请自问**：这批图真的需要"≥90 分"吗？默认关闭是有意的——自适应把一张图的成本乘以轮数。

---

## 16. 错误码清单（42 条，按类别归并）

错误对象 = `{line, col, code, message, hint}`；GUI/预设保存显示为
`第 L 行第 C 列：message；建议 hint（错误码 code）`。

### 16.1 词法 / 语法（20 条，`parse_adaptive_policy`）

| code | 什么时候出现 | 例子 / 修法 |
|---|---|---|
| `empty_policy` | 文本为空或只有注释/空行 | 策略必须有内容；或把 `enabled` 设为 `false` |
| `policy_too_large` | 策略 > 16 KiB | 删注释/合并阶段；超过上限一律拒绝 |
| `unknown_statement` | 行首不是已知关键字（含行首是字符串） | `targt 90` → `target 90` |
| `option_after_stage` | 全局选项写在某个 `stage` 之后 | 把 6 个选项移到第一个 `stage` 之前 |
| `duplicate_option` | 同一选项写两次 | `target 90` 只写一次 |
| `missing_value` | 选项/`stage`/`search`/`set` 缺取值 | `target` → `target 90`；`stage` → `stage quality-up` |
| `invalid_option_value` | 选项取值类型/词表不符 | `output average`、`max_rounds 8.5`、`version x` |
| `unsupported_version` | `version` > 1 | 本版只支持 `version 1` |
| `bad_stage_id` | 阶段名不匹配 `[a-z][a-z0-9_-]{0,31}` | `stage Quality` → `stage quality` |
| `duplicate_stage` | 两个间隔中的阶段同名 | 阶段名用于日志归因，必须唯一 |
| `action_outside_stage` | 动作写在第一个 `stage` 之前 | 先 `stage` 再 `search`/`set` |
| `empty_stage` | 阶段没有任何动作（含最后一个阶段） | 每个阶段至少 1 个动作，或删掉该阶段 |
| `bad_number` | `search` 的 `from`/`to`/`step` 不是合法数值（或加了引号） | `from 90x` → `from 90`；起点可写 `current` |
| `bad_integer` | 动作级 `max_rounds` 不是整数 | `max_rounds 8.5` → `max_rounds 8` |
| `unterminated_string` | 字符串少右引号 | `set … "abc` → `"abc"` |
| `bad_escape` | 字符串里非法转义 | 只支持 `\\` `\"` `\n` `\t` `\r` |
| `search_syntax` | `search` 子句缺失/顺序错（缺 `from`/`to`/`step`，键加引号） | 写法见 §6.1 |
| `bad_method` | `method` 不是 `linear/bisect/exp/greedy` | `method quadratic` → `method bisect` |
| `set_syntax` | `set` 的键加了引号 | `set "chroma" 444` → `set chroma 444` |
| `stray_token` | 一行里有多余标记（含选项/stage/动作后的额外词） | `target 90 95`、`step 2 3` |

### 16.2 语义 / 注册表（20 条，`validate_adaptive_policy`）

| code | 什么时候出现 | 例子 / 修法 |
|---|---|---|
| `target_range` | `target` 不在 `(0, 100]` | `target 0` / `target 120` |
| `max_rounds_range` | 全局 `max_rounds` 不在 `1..32` | `max_rounds 40` → `8` |
| `plateau_range` | `plateau < 0` 或非有限 | `plateau 0` = 关闭 |
| `no_stage` | 只有选项、没有 `stage` | 至少 1 个阶段 |
| `unknown_key` | 键既不在该格式目录、也不是伪参数 | `search qualityy …`（拼写） |
| `forbidden_key` | 命中 40 键禁止表或 `__` 前缀 | `set lossless true`、`set __heif_profile 20`（用 `set profile …`） |
| `key_not_in_format` | 键存在于其它格式目录，本格式没有 | jxl 里 `set chroma 444` |
| `not_searchable` | `search` 用了白名单（6 键）之外的键 | `search effort from current to 11 step 1` → 改 `set` |
| `search_range` | `from`/`to` 超出该键数值域 | `quality` 的 `to 120` 越界 |
| `search_noop` | 显式 `from == to`（区间为空） | 改端点或删该动作 |
| `step_range` | `step <= 0` | `step 0` → `step 2` |
| `action_max_rounds` | 动作级 `max_rounds` 不在 `1..32` | 删掉该子句 = 继承全局 |
| `set_value_type` | `set` 值类型与目标参数不符（含缺值） | `set chroma true` → `set chroma 444` |
| `set_value_range` | `set` 数值越界 | `set quality 120` |
| `enum_value` | `set` 枚举值不在 choices（引擎值/中文名/`…（N）`标签） | `set tune nonsense` |
| `profile_key_format` | `profile` 用在非 HEIF/AVIF | jxl 里 `set profile …` |
| `profile_value` | 档位名/编号非法 | `set profile Main444_99` |
| `profile_unavailable` | 档位存在但本后端不可用 | `set profile Main10Still`（16 号，实测不可用，见 [参数清单](param-catalog.md) §3） |
| `tech_not_supported` | 使用 `tech` 伪参数（P2 未交付） | 删掉该动作；技术由格式卡/`lossless` 决定 |
| `output_best_unsupported` | `output best`（P1 未交付） | 改成 `output last` |

### 16.3 JSON AST 导入（2 条，`parse_adaptive_policy_json` / `load_adaptive_policy_json`）

| code | 什么时候出现 | 修法 |
|---|---|---|
| `json_syntax` | JSON 解析失败 | 对照 [`adaptive-policy.v1.schema.json`](schemas/adaptive-policy.v1.schema.json) 检查括号/引号 |
| `json_field` | 字段缺失/类型错/枚举非法（`stages` 非数组、`kind` 非 search/set、`from` 既非数值也非 `"current"` 等） | 按 §14 的字段表补齐 |

> 全表 = `adaptive_error_codes()` 的 42 项（20 + 20 + 2，与 `build/m10/w3-core/report.md` §2.5 逐字一致）；
> 单测对每条至少一个非法样例钉住 code（含行号/列号）。

---

## 17. 已知边界与未决（如实）

1. **`output best` 未交付**（P1）：需要保留候选文件集，当前只有 `last`。
2. **10/12 位动态域只在执行期复核**：解析期用目录静态域（如 `0..51`），实际每轮由预检按位深复核（`−12..51` / `−24..51`）；错误信息会点名位深。
3. **`tech` 切换默认禁止**：P2；若将来放开，必须"切换后重跑整套校验"。
4. **`heif_grid` 的类型**：目录里是 **int 枚举 0/1/2**（关闭/自动/总是），设计 §8.2 早期 JSON 示例写成 `"auto"` 已被修正；
   预设里写字符串会被逐键校验拒绝（不静默当 0）。
5. **无损格式不配策略**：PNG/TIFF/BMP 的评分≈100，策略无意义（出厂库不提供）。
6. **执行器与出厂文件的最终形态（已定稿）**：执行器（轮循环/候选 commit/侧车）随 W3-T4 落地、内置库随 W3-T5
   （运行时幂等 seed，D-8）；端到端读数（格式预设四格式 / 全局预设 adaptive / `--adaptive-policy`）见
   [M10 收口报告](m10-report.md) §1.3 与 §2-14，证据 `build/m10/w3-preset/e2e/`、`build/m10/final-gate/a14/`。

---

## 18. 相关文档

- 设计：[`v0.4.0-design.md`](v0.4.0-design.md) §9（语义/DSL/键/成本/默认库）、§8（格式预设承载）、§11（兼容）
- 验收：`build/m10/w3-core/report.md`（DSL/AST/错误码/默认库/预设 JSON 承载）、`build/m10/w3-exec/report.md`
  （执行器/pipeline 集成/侧车）、`build/m10/w3-preset/report.md`（全局预设 `adaptive`/内置 seed）、
  `build/m10/final-gate/report.md`（最终树门禁与 E2E）
- 用户手册：[格式预设](format-presets.md) · [参数清单](param-catalog.md)
- 登记：[M10 裁定（A3/A9/A13/A14/A20）](m10-adjudications.md) · [M10 工作流（W3 任务卡）](m10-workflow.md) · [M10 收口报告](m10-report.md)
- Schema：[`adaptive-policy.v1.schema.json`](schemas/adaptive-policy.v1.schema.json)（AST 导入）· [`format-preset.v1.schema.json`](schemas/format-preset.v1.schema.json)（预设容器）
