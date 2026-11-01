# 自适应质量策略（Adaptive Policy）用户手册

> **适用版本**：0.4.0（M10 需求 11 / 契约 **C24**）。**引擎默认关闭；GUI 出厂 `default` 预设可自带策略**（见 §1/§10）。
> **真源链**：设计 = [`docs/v0.4.0-design.md`](v0.4.0-design.md) §9（§9.1 语义 / §9.2 DSL / §9.4 键 / §9.5 成本 / §9.7 默认库；
> §9.7 带 **0.4.0（M12）修订**节）；
> **策略文本的发行真源 = 仓库工厂数据文件 `presets/formats/**`（11 个，单一真源；M12 起不再是 C++ 内置）**；
> 验收 = `build/m10/w3-core/report.md`（DSL/AST/错误码/默认策略/预置 JSON 承载，W3-T3）+ 0.4.0 迭代
> 「DSL 可读化 / 断点搜索 / 5 条新增内置预设 / M12 工厂文件外部化与 JPEG 新策略」（本手册 §4/§6.1/§10.1/§16 同步：错误码 42→45）；
> 实现真源 = `src/core/adaptive.cpp`（`adaptive_error_codes()`、`adaptive_forbidden_keys()`、
> `render_adaptive_policy()`）+ [`docs/format-presets.md`](format-presets.md) §7（工厂文件清单/seed）。
> **相关文档**：[格式预设用户手册](format-presets.md) · [参数清单](param-catalog.md) · [M10 收口报告](m10-report.md) ·
> [策略 AST JSON Schema](schemas/adaptive-policy.v1.schema.json) · [格式预设 JSON Schema](schemas/format-preset.v1.schema.json) ·
> [工作流任务卡](m10-workflow.md) · [M10 裁定登记](m10-adjudications.md)

---

## 1. 这是什么（以及它不是什么）

自适应策略是一段**跟着格式预设走**的文本：它让程序在编码时**多试几组参数**，每试一轮就用
SSIMULACRA 2 给产物打个分，分数达到目标就停；没达到就输出**最后一轮成功编码**的结果。

| 项 | 口径 |
|---|---|
| 默认状态 | **引擎默认关闭**：不带预设的路径（`--dev`、金样、回归）没有策略 ⇒ 行为与 0.3.x **逐字节相同**。**GUI 例外**：每个格式卡自动载入出厂 `default`，其中 JPEG / HEIF / AVIF / WebP / JXL 的 `default` 自带策略 ⇒ 这些格式在 GUI 里**默认启用**（可随时关掉开关或换无策略预设；PNG/TIFF/BMP 的 `default` 无策略）。见 §10 |
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
2. 在根对象里加 `adaptive` 对象，把策略文本写进 `policy` —— **推荐写成字符串数组，一行一个元素**
   （落盘零 `\n` 转义、可读、可 diff；旧版单字符串写法仍兼容读），并把 `enabled` 设为 `true`：

   ```json
   "adaptive": {
     "enabled": true,
     "policy": [
       "version 1",
       "target 90",
       "max_rounds 8",
       "output last",
       "",
       "stage quality-up",
       "  search quality from current to 98 step 2"
     ]
   }
   ```

3. 在 PageOutput 的该格式页签里选中这个预设 ⇒ 载入时**先校验策略**：不合法会给出「第 L 行第 C 列：原因；建议（错误码）」并**拒绝载入**（保留当前卡值，绝不静默套默认）。数组形态下 L = 数组下标 + 1（空行也占一行）。
4. 开始运行。启用自适应的输出行会显示「自适应 3/8」这类轮次进度；日志里出现 `adaptive plan` / `adaptive stage` / `adaptive round` / `adaptive summary`（§13）。

> 只想先看效果：把 `max_rounds` 设成 `2`、`target` 设成 `85`，跑一张小图。

---

## 3. 术语（读完 §4 前先扫一眼）

| 词 | 含义 |
|---|---|
| 轮（round） | 应用一组参数 → 编码到候选临时文件 → 评分 → 决策。一轮只产出一个候选 |
| stage（阶段） | 一组动作（`search` / `set`）；阶段内动作**按书写顺序**执行，一个阶段跑完（或达标）再进下一个。**全 `set` 阶段 = 所有 `set` 落在同一轮参数上、只编一轮**（不是每个 set 各占一轮；见 §6.2） |
| `current` | 搜索起点 = **运行配置当前值**（即该格式卡上此刻的值），不是上一轮的值 ⇒ 每次执行都从同一起点出发（幂等） |
| 方向 | `search` 的移动方向：旧语法由 `from`/`to` 的相对大小推断；DSL v1.1 可显式写 `down`/`up`（只降/只升，反向时该动作 0 轮、不报错） |
| 步长断点（breakpoint） | DSL v1.1：把搜索区间按「开区间上界」分段的步长排程，如 `breakpoint 1 step 2 breakpoint 10 step 5`（详见 §6.1） |
| 达标 | `score >= target`（默认 90，浮点按 ≥ 判定，SSIMULACRA 2 分数） |
| last | 未达标时输出**最后一轮成功编码**的结果（本版唯一支持） |
| 候选临时文件 | 每轮的编码产物，落在最终输出同目录（同目录唯一名），**不写元数据**（评分只看像素）；选定后才 commit 到最终路径 |

---

## 4. 语法（DSL v1.1，EBNF 逐字）

```ebnf
policy      = { option } , { stage } ;
option      = "version" INT                 (* 省略=1 *)
            | "target" NUMBER               (* (0,100]，默认 90 *)
            | "max_rounds" INT              (* 1..32，默认 8 *)
            | "output" ( "last" | "best" )  (* 默认 last；best=P1 *)
            | "on_metric_unavailable" ( "stop" | "fail" )  (* 默认 stop *)
            | "plateau" NUMBER ;            (* 0=关；>0=连续 2 轮改善<阈值即止 *)
stage       = "stage" IDENT NL { action } ;
action      = "search" KEY "from" VALUE [ ( "down" | "up" ) ] "to" VALUE "step" NUMBER
              { "breakpoint" NUMBER "step" NUMBER }
              [ "method" ( "linear" | "bisect" | "exp" | "greedy" ) ] [ "max_rounds" INT ]
            | "set" KEY VALUE ;
KEY         = 目录内参数键 | "profile" | "chroma" | "tech"  (* 伪参数 *)
VALUE       = NUMBER | true | false | ENUM_NAME | "字符串" ;
IDENT       = [a-z][a-z0-9_-]{0,31} ;
comment     = "#" 到行尾 ;
```

**v1.1 加性扩展（需求 3/7；旧语法逐字兼容）**：

- 可选**显式方向**：`from current down|up to …`（`down` 位于 from 取值之后、`to` 之前，且必须跟 `to`）；
  省略 = 旧语义（方向由 `to − from` 的相对大小在运行期推断）。
- 可选**步长断点**：`breakpoint <数值> step <数值>`，可重复、位于基础 `step` 之后、`method` 之前；
  断点必须**严格升序**、与 `to` 同侧（down 时 > to、up 时 < to），断点 step > 0，且**必须与显式方向
  + `method linear` 一起使用**（其余组合在校验期显式报错，不静默忽略）。

**落地口径（与上面摘要的差异，均已在实现与单测钉住）**：

- `search` 的 `from` 实际只接受 **`current` 或数值**（`to` / `step` 必须数值）；EBNF 里写成 `VALUE` 是摘要写法。
- `search` 子句顺序固定：`from … [down|up] to … step …` → 零或多个 `breakpoint … step …` →
  可选 `method …` → 可选 `max_rounds N`；顺序颠倒或多写标记 = `stray_token` / `search_syntax` /
  `breakpoint_syntax`。
- `KEY` 的 `tech` 伪参数**默认禁止**（`tech_not_supported`，设计 §9.4 归为 P2）；`chroma` 按结构键处理（只能 `set`）。
- `set` 的 `VALUE`：整数 → `int64`，带小数点 → `double`，`true`/`false` → 布尔，裸词 → 枚举名（也接受中文显示名 / `…（N）` 标签），`"字符串"` → 字符串（转义 `\\` `\"` `\n` `\t` `\r`）。
- **注释**：`#` 到行尾；字符串里的 `#` 不算注释。**缩进不是语义**（`stage` 行开始新阶段），空行自由。
- **大小写**：关键字一律小写；枚举值按参数目录的 choices 匹配（引擎值或中文显示名均可）。
- **错误定位**：1-based `line` / `col`（来自 JSON AST 导入的错误，`line` / `col` 记 0，按字段名定位）。
- **规范化渲染**：固定写全 6 个选项 + 空行 + `stage` + 两空格缩进的动作；显式方向按语义写
  `down` / `up`（旧语法无方向时**不写**方向词，因为 `from current` 无法静态推断方向）；断点按
  数值升序逐组写 `breakpoint V step S`；`method` 只在非 `linear` 时写，动作级 `max_rounds` 只在
  非 0 时写；满足 `render(parse(render(p))) == render(p)`。日志/侧车里的策略 SHA 用规范化文本复算。

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
search <KEY> from (current|<数值>) [down|up] to <数值> step <数值>
       [breakpoint <数值> step <数值>]... [method linear|bisect|exp|greedy] [max_rounds N]
```

- **方向**：省略方向词 = 旧语法（`dir = sign(to − from)`，在运行期按 `current` 实值推断）；
  显式 `down` = 只降不升、`up` = 只升不降。显式方向下**起点已在端点错侧 ⇒ 该动作 0 轮，不是错误**
  （例如 `from current down to 8` 而当前值已 ≤ 8：不产生候选，阶段直接耗尽/进下一 stage）。
- 端点含 `to`，并按参数域钳位；`from == to`（两者都是显式数值、且**无**方向词）= `search_noop`；
  `step <= 0` = `step_range`。
- **`max_rounds`（动作级，`1..32`）**：覆盖该动作的阶段上界（linear 默认阶段上界 16），仍受全局
  `max_rounds` 与三层终止约束。需要「一路降到 0.1 / 升到 90」这类长序列时**必须显式写**
  （例如 `… down to 0.1 step 0.1 max_rounds 30`），否则候选数在 16 处被阶段上界截断。
- 只有 **6 个数值质量键**可被 `search`（白名单见 §8.1）；其余一律 `not_searchable`。

| method | 候选序列 | 每阶段上界（设计 §9.5） |
|---|---|---|
| `linear`（默认） | `v ← v + dir·step(v)`，钳到参数域/断点值/端点；`step(v)` 见下方断点语义 | 默认 16（动作级 `max_rounds` 可放宽），另受全局 `max_rounds` |
| `bisect` | 端点"差/好"已知时按分数二分，直到区间 `< step` 或达标 | ≤18 |
| `exp` | 步长 × factor（默认 1.5，上限 4） | 受 `max_rounds` |
| `greedy` | 朝目标步进；无改善则步长减半，低于最小步长停止 | 受 `max_rounds` |

**步长断点（DSL v1.1 / 需求 7；仅 `linear`）**：`breakpoint <值> step <步长>` 把搜索区间按
**开区间上界**分段 ——

- `step(v)` = 基础 `step` 若 `v <= 第一个断点`；否则 = 「**v 严格大于的最大断点**」的 step。
  例：`… down to 0.1 step 0.1 breakpoint 1 step 2 breakpoint 10 step 5` ⇒ `v <= 1` 用 0.1、
  `1 < v <= 10` 用 2、`v > 10` 用 5（区间口径与用户需求「1–10 步长 2、10–25 步长 5、
  1–0.1 步长 0.1」一致，1.0 归 1–0.1 段）。
- **跨断点不得跳过断点值**：若下一步会越过某个断点 `b`（down: `next < b < v`；up 对称），
  先把候选钳到 `b` 本身（取沿方向最近的断点），再继续用该断点下方区间的步长；
  最后若越过 `to` 则钳到 `to`（端点必被尝试）。
- 断点必须**严格升序**（`breakpoint_order`）、与 `to` 同侧（down: > to；up: < to），
  给出显式起点时还必须在起点内侧（`breakpoint_value`）；断点 step 必须 > 0；
  断点**必须配显式方向**且 `method linear`（否则 `breakpoint_syntax`）。
- 期望候选序列（`JXL Default` 策略，逐点单测钉住）：

  | 当前 distance | 候选序列 | 轮数 |
  |---|---|---|
  | `1.0` | `0.9, 0.8, …, 0.1` | 9 |
  | `4.0` | `2, 1, 0.9, …, 0.1` | 11 |
  | `12.0` | `10, 8, 6, 4, 2, 1, 0.9, …, 0.1` | 15 |

- **JPEG `default` 策略（0.4.0 / M12 新增）**：`search distance from current down to 0.1 step 0.1
  breakpoint 1 step 1 max_rounds 20`，起点 = 卡值 `distance=3.0`、参数域 `[0.1, 3.0]`——
  `v > 1` 时用断点 `1` 的步长 **1**（1–3 段粗扫），`v <= 1` 用基础步长 **0.1**（0.1–1 段精扫）。

  | 当前 distance | 候选序列 | 轮数 |
  |---|---|---|
  | `3.0` | `2, 1, 0.9, 0.8, …, 0.1` | **11** |
  | `2.0` | `1, 0.9, 0.8, …, 0.1` | 10 |
  | `0.6` | `0.5, 0.4, …, 0.1` | 5 |

  读法：从 3.0 出发先按整数跳（2、1），到断点 1 后每次降 0.1，直到 0.1；达标即停，未达标输出最后一轮
  （本轮即 0.1 档，体积最大）。这条策略同时是「JPEG distance UI 域 0.3–3.0 → **0.1–3.0**」的原因——
  0.3 下限会让 0.1–0.3 段不可达（R-294 修订，见 [参数清单](param-catalog.md) §1）。

### 6.2 `set`（一次性赋值）

```
set <KEY> <VALUE>
```

- 支持两类键：该格式参数目录里**可设的键**（减去 §8.3 禁止表），以及伪参数 `profile`（仅 HEIF/AVIF）。
- **`set` 阶段语义（勘误）**：一个 `stage` 内的**全部 `set` 落在同一轮参数上、只编一轮**
  （同轮的多个键一起生效），不是「每个 set 各占一轮」。只有多个 stage 才会各占一轮。
  （本文早期版本写成「每个 set = 1 轮」、与实现不符，现按实现修正；单测 `pp_test_adaptive_exec`
  的「set 阶段 = 1 轮 / 多键合一」用例钉住。）
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
把色度切 4:4:4、档位切 `Main444_10Still`（**同一轮同时生效、只编一轮**）；全部跑完仍未达标 ⇒ 输出最后一轮。
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

### 示例 3 · 一次性兜底配置（只有 1 个阶段，1 轮）

```
target 90
stage fallback
  set chroma 444
  set x265:psy-rd 2.0
  set x265:aq-mode 2
```

读法：不写选项 ⇒ 用默认（`version 1` / `target 90` / `max_rounds 8` / `output last` / `on_metric_unavailable stop` / `plateau 0`）。
只做**一轮**兜底尝试：`chroma 444` + 两个 x265 透传键**在同一轮同一次编码里一起生效**（1 个 stage 的全部
`set` 合并为一轮，见 §6.2）；达标即停，否则阶段耗尽、输出这一轮。
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

## 10. 出厂策略库（11 条工厂文件；策略全文）

> **0.4.0 / M12 起，出厂预设与策略文本的单一真源 = 仓库工厂数据文件 `presets/formats/**`**
> （11 个 JSON：8 格式各一条 `default` + HEIF「Apple自适应」+ JXL「无损归档」+ JXL「自适应压缩」）。
> 工厂文件随发行包分发（Windows zip 顶层 `PhotoPipeline/presets/formats/**`、AppImage `usr/bin/presets/formats/**`），
> 首启**按字节复制** seed 到 `<data_dir>/presets/formats/`；人类可直接编辑/复制，见
> [格式预设手册](format-presets.md) §2/§7。
>
> 历史：0.4.0.alpha1/alpha2-dev 的出厂预设是 **C++ 编译内置**（`src/core/format_preset.cpp` 的
> `builtin_format_presets()` + 首启幂等 seed 成 `_builtin-*.json` 只读模板；HEIF `default` 由 libheif
> 运行时内省生成快照，以免写死随插件版本漂移的值，D-8）。该机制已删除（M12-A1/A2/A3）：人类不可直接改/复制、
> 出厂更新不随包走、C++ 数据与 JSON 双源。旧 `_builtin-*.json` 由首启迁移清理（[format-presets.md](format-presets.md) §7.4）。
> HEIF `default` 的参数快照现在**冻结在工厂 JSON 里**（静态数据，不再依赖用户机器的 libheif 版本）。

**策略全文按工厂文件逐条列出**（`default` 名 = 小写；每条策略顶部的 `#` 注释行也逐字在文件里）。
PNG / TIFF / BMP 的 `default` **不提供策略**（无损，评分≈100，策略无意义；`adaptive` 永远为空）；
`JXL 无损归档` 同样无自适应。

**HEIF · `presets/formats/heif/apple-adaptive.json`（name `Apple自适应`）**

```text
# HEIF Apple 自适应：先降 CRF（步长 2，到 8），仍不达标再切 4:4:4 + Main 4:4:4 10 Still。
version 1
target 90
max_rounds 10
output last
on_metric_unavailable stop

stage crf-down
  search x265:crf from current down to 8 step 2 max_rounds 10

stage chroma-444
  set chroma 444
  set profile Main444_10Still
```

语义：优先降 CRF（步长 2；CRF 到 8 仍不达标）→ 切 4:4:4 + Main 4:4:4 10 Still（同一轮、只编一轮）→
仍不达标结束（输出最后一轮）。参数：`x265`/`runtime`/10bit、档位 `reserved.heif_profile=2`（Main10）、
`chroma=420`/`preset=slow`/`tune=ssim`/`x265:aq-mode=3`/`x265:crf=20`/`x265:level-idc=31`（CRF 路径不写 quality；
**CRF 默认 20**，不是 28——用户需求 3，`--ui-smoke` 探针 `heif-apple-crf=20` 钉住）。

**HEIF · `presets/formats/heif/default.json`**

```text
# HEIF default：quality 从当前值向上每 4 分试到 90；到 90 仍不达标则输出最后一轮。
version 1
target 90
max_rounds 30
output last

stage quality-up
  search quality from current up to 90 step 4 max_rounds 30
```

参数 = **运行时内省默认值的静态快照**（`chroma=420`/`lossless=false`/`preset=slow`/`quality=50`/
`tu-intra-depth=2`/`tune=ssim`；`x265`/`runtime`/8bit）。快照冻结在工厂文件里，随包分发。

**JXL · `presets/formats/jxl/adaptive-compress.json`（name `自适应压缩`）**

```text
# JXL 自适应压缩：distance 从当前值每 0.1 往下降，直到达标或到 0.1（越小越清晰）。
version 1
target 90
max_rounds 20
output last

stage distance-down
  search distance from current down to 0.1 step 0.1 max_rounds 20
```

参数：`libjxl`/`vardct`/16bit、`distance=1.0`、`effort=4`。

**JXL · `presets/formats/jxl/default.json`**

```text
# JXL default：distance 断点步长降档（1 以下 0.1 精扫 / 1–10 步长 2 / 10 以上步长 5）。
version 1
target 90
max_rounds 30
output last

stage distance-down
  search distance from current down to 0.1 step 0.1 breakpoint 1 step 2 breakpoint 10 step 5 max_rounds 30
```

参数：`libjxl`/`vardct`/16bit、静态表默认全量快照（9 键：`brotli_effort=-1`、`codestream_level=-1`、
`decoding_speed=0`、`distance=1.0`、`effort=7`、`epf=-1`、`lossless=false`、`photon_noise=0`、`resampling=-1`）。
断点语义与期望候选序列见 §6.1。

**JXL · `presets/formats/jxl/lossless-archive.json`（name `无损归档`）**：`libjxl`/`modular`/16bit、
`lossless=true`、`effort=7`；**无自适应**（无损，评分≈100）。

**AVIF · `presets/formats/avif/default.json`**

```text
# AVIF default（历史内建 §9.7）：quality 搜索 → 4:4:4 → tune psnr 兜底。
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

参数：`svt-av1`/`runtime`/10bit、`quality=85`、`chroma=420`（沿用旧口径：只写安全初值；运行期内省参数不写，
避免随插件版本漂移被逐键校验拒绝）。

**WebP · `presets/formats/webp/default.json`**

```text
# WebP default（历史内建 §9.7）：quality 搜索 → method（压缩努力）提升。
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

参数：`libwebp`/`lossy`/8bit、静态默认全量（`quality=85`、`method=4` …）。

**JPEG · `presets/formats/jpeg/default.json`（0.4.0 / M12 新策略）**

```text
# JPEG default：视觉距离从 3.0 起步（1–3 步长 1 粗扫），1 以下按 0.1 精扫到 0.1；取达到目标 90 分的最小体积（= 最大 distance）一轮。
version 1
target 90
max_rounds 20
output last
on_metric_unavailable stop

stage distance-down
  search distance from current down to 0.1 step 0.1 breakpoint 1 step 1 max_rounds 20
```

参数：`jpegli`/`dct`/8bit、静态默认全量快照（`quality_mode=distance`、`distance=3.0`、`quality=90`（quality
模式下才生效）、`chroma=444`、`progressive=true`、`optimize_coding=true` …）。
语义：从 3.0 起步，**1–3 段步长 1 粗扫、1 以下步长 0.1 精扫到 0.1**，取「达到目标 90 分的最小体积
（最大 distance）」的一轮；候选序列与跨断点钳位见 §6.1。
**JPEG distance 的 UI 范围同步放宽为 0.1–3.0**（R-294 修订；否则 0.1–0.3 段不可达，见 [参数清单](param-catalog.md) §1）。

### 10.1 11 条工厂预设清单（路径 / 参数 / 策略总表）

| 格式 | 工厂文件 | JSON `name` | 后端 · 技术 | 位深 | 参数（快照） | 自适应策略 |
|---|---|---|---|---|---|---|
| HEIF | `heif/apple-adaptive.json` | `Apple自适应` | `x265` · `runtime` | 10 | `chroma=420`、`preset=slow`、`tune=ssim`、`x265:aq-mode=3`、`x265:crf=20`、`x265:level-idc=31`；档位 `reserved.heif_profile=2`（Main10） | CRF 20→8 步长 2，再切 444 + Main444_10Still（max 10） |
| HEIF | `heif/default.json` | `default` | `x265` · `runtime` | 8 | **运行时内省默认值的静态快照**（`chroma=420`/`lossless=false`/`preset=slow`/`quality=50`/`tu-intra-depth=2`/`tune=ssim`） | quality 从当前值 +4 升到 90（max 30） |
| JXL | `jxl/lossless-archive.json` | `无损归档` | `libjxl` · `modular` | 16 | `lossless=true`、`effort=7` | **无**（无损，评分≈100） |
| JXL | `jxl/adaptive-compress.json` | `自适应压缩` | `libjxl` · `vardct` | 16 | `distance=1.0`、`effort=4` | distance 每 −0.1 降到 0.1（max 20） |
| JXL | `jxl/default.json` | `default` | `libjxl` · `vardct` | 16 | 静态表默认全量快照（9 键） | distance 断点降档（1 以下 0.1 / 1–10 步长 2 / >10 步长 5；max 30） |
| AVIF | `avif/default.json` | `default` | `svt-av1` · `runtime` | 10 | `quality=85`、`chroma=420` | quality→444→tune psnr（max 8） |
| WebP | `webp/default.json` | `default` | `libwebp` · `lossy` | 8 | 静态默认全量（`quality=85`、`method=4` …） | quality→method 6（max 8） |
| JPEG | `jpeg/default.json` | `default` | `jpegli` · `dct` | 8 | 静态默认全量；`quality_mode=distance`、`distance=3.0`、`quality=90` | **distance 3.0→0.1，断点 1 步长 1**（max 20；§6.1/§10） |
| PNG | `png/default.json` | `default` | `oiio` · `deflate` | 16 | `compression="default"`、`compressionLevel=6`、`filter=0` | **无**（无损） |
| TIFF | `tiff/default.json` | `default` | `oiio` · `codec` | 16 | `compression="lzw"`、`deflate_level=6`、`predictor=2`、`tiff_tile_*=0` | **无**（无损） |
| BMP | `bmp/default.json` | `default` | `oiio` · `raw` | 24 | 无参数 | **无**（无损） |

> 全部 11 条都必须过 `load_format_preset` + `validate_format_preset`（单测 `pp_test_format_presets` 钉住：
> 8 格式、11 文件、每格式恰有 `default.json` 且 `name=="default"`、HEIF 恰 2 / JXL 恰 3、无 `_builtin-` 文件、
> 无「内置示例 ·」名）。HEIF 系键在运行时内省不可用的构建里由既有校验链放行（不误报）。
>
> 带 `search` 的策略**都显式写了动作级 `max_rounds`**（Apple自适应 10 / HEIF default 30 / JXL 自适应压缩 20 /
> JXL default 30 / JPEG default 20）：linear 默认阶段上界 16，不写会被截断（见 §6.1）。
>
> seed 幂等（[format-presets.md](format-presets.md) §2.1）：**非便携**模式首启 `written=11 / skipped=0`、
> 二次 `written=0 / skipped=11`；**便携**模式（工厂目录 == 数据目录）只清理旧文件、不复制（`written=0`）；
> 逐字节复制、已存在不覆盖。

> **`policy` 的落盘形态（需求 3）**：JSON 字符串数组，一行一个元素（含 `#` 注释行与空行）；示例见
> [format-presets.md](format-presets.md) §3.1 与 `presets/formats/jpeg/default.json`。

---

## 11. 在格式预设 JSON 里携带策略（**数组：一行一条**）

`adaptive` 是预设根对象里的一个可选对象（两个字段，`enabled` 与 `policy`）。**需求 3 起
`policy` 一律写成 JSON 字符串数组、一行一个元素**（零 `\n` 转义、人类可读、可 git diff）：

```json
"adaptive": {
  "enabled": true,
  "policy": [
    "version 1",
    "target 90",
    "max_rounds 8",
    "output last",
    "on_metric_unavailable stop",
    "",
    "stage quality-up",
    "  search quality from current to 98 step 2",
    "",
    "stage chroma-444",
    "  set chroma 444",
    "  set profile Main444_10Still"
  ]
}
```

| 字段 | 类型 | 缺省 | 规则 |
|---|---|---|---|
| `adaptive.enabled` | bool | `false` | `true` 时 **必须**有非空 `policy`（否则保存/载入被阻断） |
| `adaptive.policy` | **string[]**（旧 `string` 兼容读） | 空 | 行式 DSL v1 文本，一行一个元素；每个元素**不得内嵌换行**（否则数组下标 ↔ 行号对不上，显式报错）。总长 **≤16 KiB**（按各行字节和 + 换行计；超限 = `policy_too_large`）。保存/载入/开始运行三处同一套 parse+validate |

- **内存表示不变**：`FormatPresetData::adaptive_policy` / `OutputFormatSpec::adaptive_policy` 仍是
  单个 `\n` 连接的 `std::string`（内部/引擎/日志/SHA 读取路径零改动）；只有 JSON I/O 边界在
  「数组 ⇄ 文本」之间转换（`core/adaptive.h` 的 `adaptive_policy_to_lines` / `adaptive_policy_from_lines`）。
- **旧字符串兼容读**：读入旧文件（`"policy": "…\n…"`）时按 `\n` / `\r\n` 拆行后走同一条规范化
  路径；再次保存即升级为数组形态。
- **字节稳定**：`保存 → 载入 → 保存` 逐字节一致（数组元素顺序 = 文本行序；末尾空行无语义、
  写出时丢弃）；空白行/注释行都保留。
- **错误定位**：DSL 解析/校验错误的 `line` = 数组下标 + 1（数组形态下与文件行号同源的语义坐标）。
- `enabled=false` 且 `policy` 为空 ⇒ **不写** `adaptive` 字段（旧文件零变化，C25）。
- **测试证据（0.4.0 迭代）**：`pp_test_format_presets` 的「adaptive.policy 数组持久化 + 旧字符串
  兼容读」用例（写出数组/无 `\n` 转义/逐行内容/`保存→载入→保存` 字节稳定/内嵌换行报错/数组下标+1
  行号）、`pp_test_presets` 的全局预设 adaptive 组（JSON 数组逐行 + 旧字符串兼容读 + 字节稳定）、
  `--ui-smoke` 的 format-preset 探针 (g)/(h)（seed 后文件 policy 为数组、全局预设写盘数组逐行）。
- 完整预设示例见 [格式预设手册](format-presets.md) §3（含 `kind`/`version`/`params`/`reserved` 的整份文件）。

### 11.1 全局预设也携带同一个块（A20）+ dev CLI 两条路径（A14）

格式预设与**全局预设 v2 的 `outputs[i]`** 支持**同一个** `adaptive` 块（加性；两者**互不叠加**，
最后一次显式载入/开关决定生效态）：

| 面 | 实际行为 |
|---|---|
| 全局预设写盘 | `outputs[i].adaptive = {enabled, policy:[...]}`（数组一行一条，与格式预设同一编码）；空块**不写**字段 ⇒ 旧全局预设逐字节不变（C25） |
| 全局预设读盘 | 缺省 = `enabled=false` / 空策略（自适应关闭，行为与 0.3.x 相同）；`adaptive` 非对象 / `enabled` 非布尔 / `policy` 非字符串或字符串数组 ⇒ **显式报错**（不静默当关闭）；旧字符串形态兼容读 |
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
| 改策略 | 直接编辑 `adaptive.policy` 数组（一行一个元素，含 `#` 注释行/空行；旧单字符串形态仍兼容读）；保存后重新选中预设触发校验 |
| 校验失败 | 载入被拒绝并给出「第 L 行第 C 列：原因；建议（错误码）」；**当前卡值保持不变**（不静默套默认） |
| 合法性自查 | 对照 §9 错误码表；或先用 JSON AST 导入通道（§14）让脚本生成、再由程序渲染成 DSL |
| 想比对版本 | 日志里的 `policy_sha` 是**规范化文本**的 SHA256 前 16 位（注释/缩进不同不影响比对） |

> 为什么没有编辑器：策略是"配置质量目标"的可审计文本，用户裁决把编辑面留在文件里（设计 §9 引言、§15），
> GUI 只做只读摘要 + 开关，避免再造一套会与 DSL 语义漂移的表单。

---

## 13. 日志、进度与侧车（可复算）

启用自适应后，`--dev` / GUI 运行日志新增（info 级，仅启用时）：

```
[info] [pipeline] adaptive plan {src=… out=… output_index=0 target=90 max_rounds=30
        stages=1 stage_ids=quality-up policy_sha=…}
[info] [pipeline] adaptive stage {… stage=quality-up round_before=0}
[info] [pipeline] adaptive round {src=… out=… output_index=0 round=2 stage=quality-up
        key=quality value=96 effective=quality=96 score=91.2 state=ok bytes=…
        encode_ms=… metric_ms=… decision=met candidate=…}
[info] [pipeline] adaptive summary {src=… rounds=3 target=90 selected=round2
        selected_params=quality=96 policy_sha=… final_score=91.2 overhead_ms=… reason=met
        reason_detail="达标：分数 91.20 ≥ 目标 90（第 2 轮）"}
```

- **hooks（需求 1 的迭代日志接口，`core/adaptive_exec.h`）**：`on_plan(AdaptivePlanInfo)`
  （run_policy 入口一次）、`on_stage_start(stage_id, round_before)`（每个 stage 一次）、
  `on_round(AdaptiveRoundRecord, max_rounds)`（每轮一次，含新增 `effective_params` /
  `candidate_path`）、`on_scoring(round, max_rounds)`（评分期心跳）。
  `AdaptiveRunResult` 末尾新增 `selected_params_text` / `reason_detail`（中文终止原因），
  `reason` 仍是稳定英文码。
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
  "max_rounds": 30,
  "output": "last",
  "on_metric_unavailable": "stop",
  "plateau": 0,
  "stages": [
    { "id": "distance-down",
      "actions": [
        { "kind": "search", "key": "distance", "from": "current",
          "direction": "down", "to": 0.1, "step": 0.1,
          "breakpoints": [
            { "value": 1, "step": 2 },
            { "value": 10, "step": 5 }
          ],
          "max_rounds": 30 }
      ] }
  ]
}
```

- 根：`{version?, target?, max_rounds?, plateau?, output?, on_metric_unavailable?, stages[]}`（`stages` 必填 ≥1）。
- `search` 动作：`{kind:"search", key, from, to, step, direction?, breakpoints?, method?, max_rounds?}`；
  `from` = 数值或 `"current"`；`direction` = `"down"` / `"up"`（省略 = 旧语法，由端点推断）；
  `breakpoints` = `[{value, step}...]`（严格升序；仅 direction 存在且 method=linear 时合法）。
- `set` 动作：`{kind:"set", key, value}`；`value` = 数字 / 布尔 / 字符串。
- 导入后**仍走同一套 `validate`**（§9、§8），AST 里的非法键不会比 DSL 更宽松；错误码前缀为 `json_syntax` / `json_field`。

---

## 15. 成本模型（每轮 encode + score）

| 图像量级 | 每轮 encode | 每轮 score | 每轮合计（实测口径，设计 §9.5） |
|---|---|---|---|
| 2 MP | 0.5–1.0 s | 0.63–1.03 s | **1.3–1.8 s** |
| 22.8 MP（23 MP） | 3–6 s | 1.5–3 s（流式后） | **5–9 s** |

- **最坏耗时 ≈ `max_rounds × 单轮`**：`max_rounds=8` 时最坏 ≈ 8 轮；`plateau` 与「达标即停」通常远早于触顶。
- 三层终止保证：全局 `max_rounds`（≤32）+ 每 stage 上界（linear 默认 ≤16、bisect ≤18；动作级
  `max_rounds` 可放宽 linear 阶段上界）+ `set` 阶段恒 1 轮；`plateau` 额外提前终止。
- 0.4.0 实测参考（见 §16）：23 MP 单次评分 **1.572 s**（W1-T4 流式化），评分色彩归一 8.49 s → **0.42 s**（W1-T2c），
  因此 22.8 MP 的每轮成本比设计估值更低；具体以本机日志 `encode_ms` / `metric_ms` 为准。
- UI/日志披露原则：**不显示的耗时 = 未发生的量**；无法估算时显示「—」而不是编造。

**启用前请自问**：这批图真的需要"≥90 分"吗？默认关闭是有意的——自适应把一张图的成本乘以轮数。

---

## 16. 错误码清单（45 条，按类别归并）

错误对象 = `{line, col, code, message, hint}`；GUI/预设保存显示为
`第 L 行第 C 列：message；建议 hint（错误码 code）`。

### 16.1 词法 / 语法（21 条，`parse_adaptive_policy`）

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
| `breakpoint_syntax` | `breakpoint` 子句结构错：缺数值 / 缺 `step` / `step` 非数值；**无显式方向却写 breakpoint**；**method≠linear 却写 breakpoint** | `breakpoint 1` → `breakpoint 1 step 2`；旧语法请补 `down`/`up`；bisect/exp/greedy 不支持断点 |

### 16.2 语义 / 注册表（22 条，`validate_adaptive_policy`）

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
| `breakpoint_order` | `breakpoint` 值未**严格升序**（含重复） | `breakpoint 10 step 5 breakpoint 1 step 2` → 升序重排 |
| `breakpoint_value` | 断点值不在搜索区间内（down 须 > to、up 须 < to；显式起点时还须在起点内侧）/ 超出参数域；或断点 `step <= 0` | `to 0.1` 时 `breakpoint 0.05` → 改到 `(0.1, 起点]` 内 |
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

> 全表 = `adaptive_error_codes()` 的 45 项（21 + 22 + 2；40 条既有码 + 断点 3 条
> `breakpoint_syntax` / `breakpoint_order` / `breakpoint_value`，0.4.0 迭代需求 7 新增）；
> 单测对每条至少一个非法样例钉住 code（含行号/列号）。

---

## 17. 已知边界与未决（如实）

1. **`output best` 未交付**（P1）：需要保留候选文件集，当前只有 `last`。
2. **10/12 位动态域只在执行期复核**：解析期用目录静态域（如 `0..51`），实际每轮由预检按位深复核（`−12..51` / `−24..51`）；错误信息会点名位深。
3. **`tech` 切换默认禁止**：P2；若将来放开，必须"切换后重跑整套校验"。
4. **`heif_grid` 的类型**：目录里是 **int 枚举 0/1/2**（关闭/自动/总是），设计 §8.2 早期 JSON 示例写成 `"auto"` 已被修正；
   预设里写字符串会被逐键校验拒绝（不静默当 0）。
5. **无损格式不配策略**：PNG/TIFF/BMP 的评分≈100，策略无意义（工厂 `default` 不提供 `adaptive`；JXL「无损归档」同样无自适应）。
6. **执行器与出厂文件的最终形态（0.4.0 定稿）**：执行器（轮循环/候选 commit/侧车）随 W3-T4 落地；出厂预设 =
   **仓库工厂数据文件 `presets/formats/**`（11 条 JSON，随包分发 + 首启字节复制 seed；M12-A1/A3 取代 0.4.0.alpha1
   的 C++ 内置库 + `_builtin-` 只读模板/D-8）**。端到端读数（格式预设四格式 / 全局预设 adaptive /
   `--adaptive-policy`）见 [M10 收口报告](m10-report.md) §1.3 与 §2-14、`build/m10/w3-preset/e2e/`、
   `build/m10/final-gate/a14/`（M12 复跑读数见 [docs/0.4.0-report.md](0.4.0-report.md) §2）。
7. **断点只支持 `method linear`**（DSL v1.1 的显式边界）：bisect/exp/greedy 没有「按段步长排程」的
   确定语义，写 breakpoint 会被 `breakpoint_syntax` 拒绝（不静默忽略）。断点还必须配显式 `down`/`up`
   （`from current` 的旧语法方向是运行期推断的，断点分段无法静态定义）。
8. **显式方向下的「反向」不报错**：`from current down to 8` 而当前值已 ≤ 8 ⇒ 该动作 0 轮（阶段耗尽/
   进下一 stage），这是需求 7 明确的口径；只有**旧语法**的显式 `from == to` 仍报 `search_noop`。
9. **HEIF `default` 快照已静态化（M12）**：参数 = 运行时内省默认值的静态快照，冻结在
   `presets/formats/heif/default.json`，**不再在用户机器上生成**（0.4.0.alpha1 的运行时 seed 口径已取消）。
   libheif/x265 升级后如需刷新快照，属"出厂数据更新"（改工厂文件，随下一版分发；见
   [format-presets.md](format-presets.md) §7）。
10. **`set` 阶段语义按实现修正**（§6.2）：一个 stage 内全部 `set` 同轮生效、只编一轮；本文早期版本
    写「每个 set = 1 轮」与实现矛盾，已勘误。

---

## 18. 相关文档

- 设计：[`v0.4.0-design.md`](v0.4.0-design.md) §9（语义/DSL/键/成本/默认库；**§9.7 含 M12 修订**）、§8（格式预设承载）、§11（兼容）
- 工厂数据文件（**出厂预设与策略文本的单一真源**）：仓库 `presets/formats/**`（11 个，M12 起）
- 验收：`build/m10/w3-core/report.md`（DSL/AST/错误码/默认库/预设 JSON 承载）、`build/m10/w3-exec/report.md`
  （执行器/pipeline 集成/侧车）、`build/m10/w3-preset/report.md`（全局预设 `adaptive`/内置 seed）、
  `build/m10/final-gate/report.md`（最终树门禁与 E2E）· M12 门禁读数见 [docs/0.4.0-report.md](0.4.0-report.md) §2
- 用户手册：[格式预设](format-presets.md) · [参数清单](param-catalog.md)
- 登记：[M10 裁定（A3/A9/A13/A14/A20）](m10-adjudications.md) · [M11 裁定](m11-adjudications.md) ·
  [M12 裁定（A1–A9）](m12-adjudications.md) · [M10 工作流（W3 任务卡）](m10-workflow.md) · [M10 收口报告](m10-report.md)
- Schema：[`adaptive-policy.v1.schema.json`](schemas/adaptive-policy.v1.schema.json)（AST 导入）· [`format-preset.v1.schema.json`](schemas/format-preset.v1.schema.json)（预设容器）
