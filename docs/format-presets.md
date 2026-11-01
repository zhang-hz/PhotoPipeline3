# 格式预设（Format Presets）用户手册

> **适用版本**：0.4.0（M10 需求 10 / 契约 **C23**）。
> **真源链**：设计 = [`docs/v0.4.0-design.md`](v0.4.0-design.md) §8（§8.2 数据模型 / §8.3 存储 / §8.4 校验链 / §8.5 UI / §11 兼容）；
> 验收 = `build/m10/w3-core/report.md`（core+io、JSON 规则、校验链，W3-T1）；
> 实现真源 = `src/core/format_preset.cpp`（校验）+ `src/ui/format_preset_io.cpp`（JSON/原子写/列举/导入导出）。
> **相关文档**：[自适应策略用户手册](adaptive-policy.md) · [参数清单](param-catalog.md) · [M10 收口报告](m10-report.md) ·
> [格式预设 JSON Schema](schemas/format-preset.v1.schema.json) · [策略 AST Schema](schemas/adaptive-policy.v1.schema.json) ·
> [M10 工作流](m10-workflow.md) · [M10 裁定登记](m10-adjudications.md)

---

## 1. 一句话：每个格式一张卡，各存各的

格式预设 = **某一个格式卡的可运行快照**（后端 / 技术 / 位深 / 参数 / 保留键 `heif_profile` / 自适应策略），
存在一个独立 JSON 文件里，**只作用于该格式**。

| 维度 | 格式预设（本文） | 全局预设（0.3.x 既有） |
|---|---|---|
| 作用域 | **单个格式**（`format` 字段 = 唯一键） | 全部输出卡（`outputs[]`） |
| 内容 | 格式参数 + 位深 + 预留的 `heif_profile` + `adaptive` | 8 个输出配置 + 全局项（色彩/冲突/模板/规则/仅元数据） |
| 存储 | `<data_dir>/presets/formats/<format>/<name>.json` | `<data_dir>/presets/<name>.json`（根层） |
| 文件标识 | `"kind": "format-preset"`（必填） | 无 `kind`，含 `outputs` 数组 |
| 目录扫描 | 只扫 `presets/formats/**` | 只扫 `presets/` 根层 `*.json` |
| 叠加关系 | **无隐式叠加**（见 §6） | 载入会覆盖所有卡的值 |

> 为什么用子目录：全局预设列举只扫 `presets/` 根层，因此两类文件**互不污染**、互不触发写盘（设计 §8.3）。

---

## 2. 存在哪里

```
<data_dir>/presets/formats/<format>/<name>.json
```

- `<format>` = 8 个格式 id 之一：`jpeg` `jxl` `png` `tiff` `webp` `bmp` `heif` `avif`。
- `<name>` = 显示名（= 文件名去掉 `.json`）；保存时按"安全文件名词干"校验（见 §5.4）。
- `<data_dir>` = 便携优先落盘（`src/platform/paths.h`）：
  - 可执行目录可写 ⇒ **便携模式**：`<exe 目录>/presets/formats/…`（同目录还有 `settings.ini`、`logs/`）；
  - 只读 ⇒ 回退系统数据目录：Windows `%APPDATA%\PhotoPipeline\presets\formats\…`、
    Linux `$XDG_DATA_HOME/PhotoPipeline`（默认 `~/.local/share/PhotoPipeline`）。
- **目录不可用**（`data_dir` 为空）⇒ 显式中文错误「格式预设目录不可用」，**绝不回落到当前工作目录**写文件。
- 文件写入走**原子写**（`write_atomic_file`），不会出现半截 JSON。

---

## 3. JSON 结构与完整示例

### 3.1 完整示例（可直接复制成文件）

```json
{
  "kind": "format-preset",
  "version": 1,
  "name": "HEIF 高质量 444",
  "format": "heif",
  "backend": "",
  "tech": "",
  "bitdepth": 10,
  "params": {
    "quality": 90,
    "chroma": "444",
    "heif_grid": 1,
    "heif_tile_w": 0,
    "heif_tile_h": 0,
    "tune": "animation",
    "x265:level-idc": 0
  },
  "reserved": { "heif_profile": 20 },
  "adaptive": {
    "enabled": true,
    "policy": "version 1\ntarget 90\nmax_rounds 8\noutput last\non_metric_unavailable stop\n\nstage quality-up\n  search quality from current to 98 step 2\n\nstage chroma-444\n  set chroma 444\n  set profile Main444_10Still\n"
  }
}
```

最小可用文件只需要三个字段：`kind`、`name`、`format`（其余按当前卡值/默认补齐）。

```json
{
  "kind": "format-preset",
  "version": 1,
  "name": "JPEG 高质量",
  "format": "jpeg",
  "params": { "quality": 92 }
}
```

### 3.2 字段逐条

| 字段 | 类型 | 必填 | 缺省 | 说明 |
|---|---|---|---|---|
| `kind` | string | **是** | — | 必须 `"format-preset"`。缺失 ⇒ 拒绝并提示「这不是格式预设文件…」；若同时含 `outputs` 数组会额外提示"像全局预设" |
| `version` | int | 否 | `1` | 缺省按 1 读；`>1` ⇒ 「不支持的格式预设版本 N（当前支持 1…）」；`<1` 拒绝 |
| `name` | string | **是** | — | 显示名；保存时决定文件名 `<name>.json`；长度 ≤120 字节（按字节，非字符）；中文可用 |
| `format` | enum | **是** | — | 8 格式之一；**作用域键**：只作用于该格式卡，与目标卡不符 ⇒ 拒绝 |
| `backend` | string | 否 | `""` | 空 = 载入时解析为**当前首选后端**；非空必须存在于该格式 |
| `tech` | string | 否 | `""` | 空 = 当前首选技术；libheif 系（heif/avif）只接受空或 `"runtime"`（运行时内省技术） |
| `bitdepth` | int enum | 否 | `0` | `0` = **不覆盖**（载入后按格式默认补齐：jpeg 8 / jxl 16 / png 16 / tiff 16 / webp 8 / bmp 24 / heif 8 / avif 10）；其余必须在该格式能力列表内 |
| `params` | object | 否 | `{}` | 该格式参数目录的键/值快照（**三态**：无键 = 未设置）；值只能是标量（数字/布尔/字符串）；`__` 前缀键**不落盘**（由 `reserved` 重建） |
| `reserved.heif_profile` | int `0..21` | 否 | 无 | HEIF/AVIF 编码档位序号（0=Auto…20=`Main444_10Still`、21=`Main444_12Still`）。HEIF 拒绝 AVIF 档（High/Professional），AVIF 拒绝 HEVC 档；16（`Main10Still`）实测不可用 ⇒ 拒绝 |
| `adaptive.enabled` | bool | 否 | `false` | `true` 时 `policy` 必须非空 |
| `adaptive.policy` | string | 否 | 空 | 行式 DSL v1 文本，≤16 KiB；详见 [自适应策略手册](adaptive-policy.md) |

**三个容易踩的点**：

1. `params` 里的 `__` 前缀键（如 `__heif_profile`、`__heif_grid`）**不会**被写盘；档位请写 `reserved.heif_profile`。
   手工写 `params.__heif_profile` 会被忽略并由 `reserved` 重建（与全局预设 `preset_io.cpp` 同口径）。
2. `heif_grid` 是 **int 枚举** `0=关闭 / 1=自动 / 2=总是`（默认 `1` 自动；目录类型是整数）。
   写字符串 `"auto"` 会被逐键校验拒绝（提示可选值「关闭/自动/总是」），**不会静默当 0**。
3. **跨技术残留键**会被拒绝：例如 JXL 预设里带 Modular-only 键（`modular_*`）却把 `tech` 钉成 VarDCT，
   会点名"该键属于哪个技术"并报错（设计 §8.4 第 4 步）。

---

## 4. 保存 / 加载 / 导入 / 导出 / 删除

UI 位置：**每个格式页签顶部一行**（在"编码档位/位深"行之上）：

```
格式预设 [ 未选择 ▼ ] [保存] [另存…] [删除] [导入…] [导出…]
```

| 动作 | 行为 |
|---|---|
| 选中下拉项 | 立即载入：先 `load`（含完整校验）→ 格式匹配检查 → `normalize` 补默认 → 应用到卡片；补齐默认项时提示「已载入（已补齐 N 个默认项）」 |
| 保存 | 用当前卡片快照覆盖**选中的文件**；未选中 ⇒ 等同「另存…」。**保存前先校验**，不通过只提示、**不写盘**（避免"先污染后报错"） |
| 另存… | 输入新名字 → 校验 → 写入 `<format>/<新名>.json` |
| 删除 | 二次确认；只接受绝对路径的普通文件（拒绝目录/不存在的路径） |
| 导入… | 选择任意位置的 JSON → 载入 + 校验 → 规范化后写入本格式目录（可顺带改名） |
| 导出… | 把当前卡片快照校验后写到用户指定路径（不改变本格式目录） |

- 载入失败 ⇒ **保留当前卡值**并显示错误（坏文件绝不会静默套默认值，`classify.json` 的 P1 教训）。
- 卡值的真源仍是格式卡的控件/参数集；预设只是一个"写入器/读取器"。
- `保存 → 载入 → 保存` **字节稳定**（`QJsonDocument::Indented` + 键序/类型归一稳定）：可以用 git 管理这些文件。

> UI 行已随 **W3-T2** 落地（每格式页签顶部一行；`--ui-smoke` 探针 `pp-format-preset-*` /
> `pp-format-preset-adaptive-*`，`format-preset` 组 `checked=23 bad=0`，见 `build/m10/w3-ui/report.md`）。
> 没有该行的旧构建也可用"手改 JSON + 重新选中"的方式（§5）。
>
> dev harness：`photopipeline --dev … --preset <预设文件>` 也接受**格式预设**（按根 `kind` 分派；施加到输出 #0，
> 含参数/位深/`heif_profile`/`adaptive`），全局预设文件在同一条命令里同样可用（A14；见
> [自适应手册](adaptive-policy.md) §11.1）。

---

## 5. 校验链、损坏与版本行为

### 5.1 同一套校验（保存 / 载入 / 开始运行三处）

1. **结构/版本/kind/字段类型**：`kind` 必须匹配、`version` ≤1、`params` 只收标量；
2. **格式匹配**：`format` == 目标卡（`check_format_preset_applies_to`）；
3. **normalize**：`backend` 空 → 首选；`bitdepth` 0 → 格式默认；`tech` 空 → `select_tech`；再 `fill_defaults` + `apply_locks`；
   返回被补齐的键列表；**幂等**（第二次调用返回空）；
4. **逐键校验**：类型 → 范围 → 枚举（与 `ParamDef` 同源；libheif 系先走运行时内省，内省不可用则放行不误报）；
   **未知键拒绝**、**跨技术残留键拒绝**（点名 owner 技术）；
5. **`heif_profile` 信号门控**：仅 heif/avif；`0..21`；HEIF/AVIF 档位不互认；`Main10Still`(16) 不可用；
6. **自适应**：`enabled=true` 且策略空 ⇒ 拒绝；策略非空 ⇒ 走 DSL `parse + validate`（失败即阻断保存/载入）；
7. **跨字段规则**：复用既有 `cross_validate`（尺寸未知 ⇒ 尺寸相关规则自动跳过）。

全部通过才应用到卡片。

### 5.2 损坏与版本

| 情况 | 行为 |
|---|---|
| JSON 语法错 | 下拉里仍**列出**该文件（名字用文件名，标「（损坏）」），悬停显示错误；选中 ⇒ 报错且保留当前卡值 |
| `kind` 缺失/不符 | 拒绝并提示；若像全局预设（含 `outputs`）会额外指路「请在全局预设区载入该文件」 |
| `version` > 1 | 显式报错（不静默丢字段）；`version` 缺失按 1 |
| 未知根字段 | **忽略 + 记日志**（`field=…`，前向兼容）；未知 `reserved` / `adaptive` 子字段同样忽略 + 日志 |
| `params` 值不是标量 | 显式拒绝（嵌套数组/对象/`null` 一律不接受） |
| 未知参数键 | 拒绝：「未知参数：`key`（格式 X 的参数表中没有该键）」 |
| 空 `adaptive` | `enabled=false` 且 `policy` 为空 ⇒ **不写** `adaptive` 字段（旧文件零变化，C25） |
| 目录/路径 | 读/写目标是目录、路径为空、目录不可用 ⇒ 各自中文错误 |

### 5.3 列举与排序

- 只列 `*.json`、跳过隐藏文件（`.` 开头）、只认普通文件；
- 排序 = 显示名（大小写敏感）→ 路径；
- 坏文件也列出（`broken=true` + 原因），**不隐藏**。

### 5.4 名字规则（保存/导入时）

拒绝：空名、`.`/`..`、>120 字节、首尾空格、尾随点、路径分隔符与 `\ / < > : " | ? *`、控制字符、
Windows 保留设备名（`CON`/`PRN`/`AUX`/`NUL`/`COM1..9`/`LPT1..9`，大小写不敏感，带 `.json` 同样保留）。

> 手改 JSON 时 `name` 不必等于当前文件名（载入以 `name` 为准），但**下次保存会按 `name` 命名**；
> 想让文件名与显示名一致，存一次即可。

---

## 6. 与全局预设的关系：无隐式叠加

- **不叠加**：格式预设载入只写**当前格式卡**；全局预设载入覆盖**全部**卡。
- 全局预设载入后，格式预设的**选择状态保留**，但卡值以全局预设为准（之后想回到格式预设，重新选中它即可）。
- 两类目录**互不扫描**、互不触发写盘：格式预设不会出现在全局预设下拉里，反之亦然。
- 优先级不存在"谁覆盖谁"的隐式规则：都是显式动作生效，最后一次载入/编辑决定当前卡值。

---

## 7. 内置预设（出厂文件）

内置预设 = **随程序编译**的 5 条示例（HEIF / AVIF / JXL / WebP / JPEG；PNG/TIFF/BMP 不提供 ——
无损格式评分 ≈100，策略无意义）。它们**不是**安装目录里的数据文件 —— **发行包不含 `share/presets/**`
出厂文件、也没有安装拷贝规则（D-8）**；内容来源 = `src/core/format_preset.cpp` 的 `builtin_format_presets()`：
该格式安全默认（后端/技术/位深/参数初值）+ §9.7 默认策略逐字。

### 7.1 首次运行自动落地（幂等 seed）

| 项 | 实际行为 |
|---|---|
| 时机 | 主窗口初始化（`refresh_preset_card()` 首调，进程内只执行一次）——不阻塞 UI |
| 路径 | `<data_dir>/presets/formats/<format>/_builtin-<format>.json`（例：`…/jpeg/_builtin-jpeg.json`） |
| 写入条件 | **仅当目标文件不存在**时写入；已存在（上次已 seed / 用户改过 / 用户改名占坑）一律**跳过**，一个字节都不动 |
| 失败语义 | 校验不过或目录不可写 ⇒ 只记日志（`builtin format preset seed failed`），不弹对话框、不影响格式预设功能 |
| 内容 | `name` = 「内置示例 · <格式>」；文件内 `kind`/`version` 与普通格式预设完全一致（可直接手改） |
| 逐字节稳定 | 规范缩进 JSON（与保存同一条写盘函数）⇒ 同一构建两次 seed 到不同目录，字节相同（单测钉住） |

5 条内置预设的安全初值（策略的第一/二阶段会在其基础上搜索更高值）：

| 格式 | 后端 · 技术 | 位深 | 参数初值 | 自适应策略 |
|---|---|---|---|---|
| HEIF | `x265` · `runtime` | 10 | `quality=85`、`chroma=420` | §9.7 HEIF（quality → chroma 444 + Main444_10Still） |
| AVIF | `svt-av1` · `runtime` | 10 | `quality=85`、`chroma=420` | §9.7 AVIF（quality → chroma 444 → tune psnr） |
| JXL | `libjxl` · `vardct` | 16 | `distance=1.0` | §9.7 JXL（distance 线性 → 二分） |
| WebP | `libwebp` · `lossy` | 8 | `quality=85` | §9.7 WebP（quality → method 6） |
| JPEG | `jpegli` · `dct` | 8 | `quality_mode=quality`、`quality=90` | §9.7 JPEG（quality） |

> 两个口径说明：
> 1. **JPEG 显式钉 `quality_mode=quality`**：默认 `distance` 模式下策略搜索 `quality` 不改变产物（`distance` 才是生效参数）。
> 2. **HEIF/AVIF 只写安全初值**：运行时内省参数随 libheif 插件版本漂移，且格式级参数并集按后端
>    first-wins 可能与具体后端的取值范围冲突（实测 `avif` 的 `speed`）——写进预设会被逐键校验拒绝。
>    未写的参数 = 不覆盖，由运行时默认值承担。

### 7.2 只读：`_builtin-` 前缀

文件名以 `_builtin-` 开头的预设是**只读模板**（前缀单源 = `core/format_preset.h` 的
`kFormatPresetBuiltinPrefix`）：

| 动作 | 行为 |
|---|---|
| 下拉选中 | 正常载入（卡值/只读自适应摘要/开关都按文件内容生效） |
| **保存**（覆盖） | **拒绝**：中文提示「内置预设只读（_builtin- 前缀）：不能覆盖；请用「另存…」保存为新的预设」；文件零改动 |
| **删除** | UI 按钮**置灰** + 原因 tooltip；I/O 层同样拒绝删除（中文错误） |
| **另存…** | **可用**：换一个普通名字（不带前缀）即可保存副本，之后随便改 |
| **导出…** | 可用（写到用户选择的路径，不受前缀约束） |
| 手工删改文件 | 文件系统层面当然可以；下次启动 seed **不会**把你删掉/改过的文件写回（只补不存在的那条） |

> 想“改内置模板”：选中它 → 「另存…」起个新名字 → 改参数/策略 → 保存。原来的 `_builtin-*` 永远保持出厂样本。

### 7.3 与自适应策略的关系

- 5 条内置预设都**自带 `adaptive.enabled=true`** + §9.7 策略原文（含注释；文本逐字见
  [自适应策略手册](adaptive-policy.md) §10），载入后格式卡显示只读摘要，开关默认**开**。
- 关掉开关 = **本次运行**不启用（预设文件里的 `enabled` 不改；下次载入仍按文件）。
- 策略只在预设文件里：GUI 没有编辑器。要改策略就「另存…」后手改副本 JSON 的
  `adaptive.policy` 文本（写法见 [自适应策略手册](adaptive-policy.md)），再重新选中载入。
- 与全局预设的关系：全局预设 v2 的输出级也可带同样的 `adaptive` 块（见 §8.1），两者互不叠加。

---

## 8. 与自适应策略的关系

- `adaptive` 是预设的**可选块**：策略只随预设持久化，不单独存文件、不进设置项。
- 载入预设后：格式卡显示**只读摘要**（`含自适应策略：目标 90 / 最多 8 轮 / 3 个阶段`）+ **启用开关**；
  **没有策略编辑器**（用户裁决，设计 §9 引言）。
- 未选中预设 / 策略为空 / `enabled=false` ⇒ 自适应关闭，输出与 0.3.x 逐字节相同（C25）。
- 出厂预设自带 1 条示例策略，可「另存为」后改文本（写法见 [自适应策略手册](adaptive-policy.md)）。

### 8.1 全局预设也携带同一个 `adaptive` 块（0.4.0 / A20）

格式预设与全局预设 v2 的 **`outputs[i]`** 都支持同一个可选块（**加性**，旧文件逐字节不变）：

```json
"outputs": [
  {
    "format": "jpeg", "backend": "jpegli", "tech": "dct", "bitdepth": 8,
    "params": { "quality_mode": "quality", "quality": 90 },
    "adaptive": { "enabled": true, "policy": "version 1\ntarget 90\n…\n" }
  }
]
```

| 面 | 行为 |
|---|---|
| 写盘 | 当前格式卡的自适应会话态 = 开关 + 策略原文；**关掉开关也保留策略文本**（下次打开不丢） |
| 缺省 | `enabled=false` 且 `policy` 空 ⇒ **不写** `adaptive` 字段（旧全局预设零变化，C25） |
| 读旧文件 | 没有 `adaptive` 字段 ⇒ `enabled=false` / `policy` 空（自适应关闭，行为与 0.3.x 相同） |
| 载入全局预设 | 覆盖全卡（含自适应）：格式卡只读摘要/开关同步为该值；原先的格式预设选择状态保留但值以全局预设为准 |
| 运行门控 | 只有 `enabled=true`（且策略非空、解析通过）才把策略交给引擎；否则**不下发策略**，走既有单次编码路径（产物逐字节不变） |
| dev harness | `--dev --preset <全局预设>` 的全局预设分支**也消费** `outputs[0].adaptive`（A14 收口补丁加性 3 行；无 `adaptive` 的旧预设行为逐字节不变）；与 `--adaptive-policy` 互斥 |
| 结构错误 | `adaptive` 不是对象 / `enabled` 非布尔 / `policy` 非字符串 ⇒ 载入**显式报错**（不静默当关闭） |

> 一句话：`adaptive` 是「输出级」属性，全局预设与格式预设各存一份、**互不叠加**；
> 最后一次显式载入（或开关动作）决定当前生效状态。

---

## 9. 常见问题

**Q：为什么我选了预设，参数没变？**
A：可能载入失败（看错误提示，卡值会保留原样）；或该预设 `format` 与当前页签不符（会明确报错）。

**Q：能让一个预设同时管 HEIF 和 AVIF 吗？**
A：不能。格式预设的作用域就是单格式（`format` 是唯一键）；跨格式请用全局预设，或各存一份。

**Q：旧版（0.3.x）能读 0.4.0 的格式预设吗？**
A：格式预设是**新文件类型**；旧版本打开会按"不是格式预设"拒绝。反向：0.4.0 读旧的全局预设文件时，
`kind` 缺失会被识别为全局预设并提示去全局预设区载入；旧全局预设里的 `__heif_grid` 等别名仍兼容读。

**Q：`heif_profile` 16–21 的编号/语义？**
A：编号只追加、不重排（0–15 语义零变化）；16–21 是 still 变体，其中 16（`Main10Still`）实测不可用。
逐档表格与 Apple 兼容性说明见 [参数清单](param-catalog.md) §3。

**Q：文件可以进版本库吗？**
A：可以。保存是确定性的（原子写 + 缩进 JSON），`保存→载入→保存` 字节稳定；但**不要**把含真实路径/密钥的内容提交入库。

---

## 10. 相关文档

- 设计：[`v0.4.0-design.md`](v0.4.0-design.md) §8（格式预设）、§9（自适应）、§11（迁移与兼容）、§12.5（验收探针）
- 验收：`build/m10/w3-core/report.md`（W3-T1：core+io、JSON 规则、校验链、API 清单）·
  `build/m10/w3-preset/report.md`（W3-T5：全局预设 `adaptive` 往返、内置预设 seed/只读、GUI 接线、探针与验收数字）·
  `build/m10/final-gate/report.md`（最终树门禁：48/48、金样 20/20、`UI-SMOKE OK shots=12 pages=3`、默认产物 SHA 不变）
- 用户手册：[自适应策略](adaptive-policy.md) · [参数清单](param-catalog.md)
- Schema：[`format-preset.v1.schema.json`](schemas/format-preset.v1.schema.json) · [`adaptive-policy.v1.schema.json`](schemas/adaptive-policy.v1.schema.json)
- 登记：[M10 裁定（A8/A9/A13/A14/A20）](m10-adjudications.md) · [M10 工作流（W3-T1..T7）](m10-workflow.md) · [M10 收口报告](m10-report.md)
