# 格式预设（Format Presets）用户手册

> **适用版本**：0.4.0（M10 需求 10 / 契约 **C23**；**M12 修订** = 工厂预设外部化为仓库 JSON 数据文件 + 每格式出厂 `default` + 删除「未选择」态）。
> **真源链**：设计 = [`docs/v0.4.0-design.md`](v0.4.0-design.md) §8（§8.2 数据模型 / §8.3 存储与生命周期 / §8.4 校验链 / §8.5 UI / §11 兼容；
> §8.3 与 §8.5 带 **0.4.0（M12）修订**节）；
> **工厂数据文件 = 仓库 `presets/formats/**`（11 个，单一真源；随发行包分发、人类可直接编辑/复制）**；
> 实现真源 = `src/core/format_preset.{h,cpp}`（校验 + `default` 名单源）+ `src/ui/format_preset_io.{h,cpp}`（JSON/工厂目录/seed 迁移/原子写/列举/导入导出）；
> 验收 = [CHANGELOG `[0.4.0]`](../CHANGELOG.md)、[docs/0.4.0-report.md](0.4.0-report.md)、[docs/m12-adjudications.md](m12-adjudications.md)。
> **相关文档**：[自适应策略用户手册](adaptive-policy.md) · [参数清单](param-catalog.md) · [M10 收口报告](m10-report.md) ·
> [格式预设 JSON Schema](schemas/format-preset.v1.schema.json) · [策略 AST Schema](schemas/adaptive-policy.v1.schema.json) ·
> [M10 工作流](m10-workflow.md) · [M10 裁定登记](m10-adjudications.md) · [M11 收口报告](m11-report.md)（历史）

---

## 1. 一句话：每个格式一张卡，各存各的

格式预设 = **某一个格式卡的可运行快照**（后端 / 技术 / 位深 / 参数 / 保留键 `heif_profile` / 自适应策略），
存在一个独立 JSON 文件里，**只作用于该格式**。

0.4.0 起，**每个格式都带一份随程序分发的出厂预设 `default`**（工厂 JSON 文件，见 §7）：程序第一次运行会把它
复制进你的数据目录；格式卡打开时**自动选中并载入 `default`**，没有「未选择」这个状态。

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

## 2. 存在哪里（工厂目录 + 数据目录）

从 0.4.0 起要区分**两层目录**：

```
工厂（出厂，只读来源；随发行包分发）
  <exe 目录>/presets/formats/<format>/<stem>.json     ← 发行包里就是这一份（仓库 presets/formats/**）

数据（运行时读写；你改的就是这一层）
  <data_dir>/presets/formats/<format>/<stem>.json
```

| 层 | 解析规则 | 说明 |
|---|---|---|
| 工厂目录 | `factory_format_presets_root()` = `platform::executable_dir() + "presets/formats"`；空路径 ⇒ 返回空 | 发行包里 exe 旁带 `presets/`：Windows zip = `PhotoPipeline/presets/formats/**`；AppImage = `usr/bin/presets/formats/**` |
| 数据目录 | `format_presets_root()` = `data_dir() + "presets/formats"`（既有，不变） | 便携优先落盘（`src/platform/paths.h`） |
| 便携模式 | exe 目录可写 ⇒ `data_dir == exe 目录` | 此时**工厂目录 == 数据目录**：seed 只做旧文件清理、不复制（发行包里那份就是你在用的那份，可直接改） |
| 只读安装 | exe 目录不可写 ⇒ `data_dir` = 系统目录（Windows `%APPDATA%\PhotoPipeline` / Linux `$XDG_DATA_HOME/PhotoPipeline`） | 首启把工厂文件**按字节复制**到数据目录；工厂件保持原样 |

- `<format>` = 8 个格式 id 之一：`jpeg` `jxl` `png` `tiff` `webp` `bmp` `heif` `avif`。
- `<stem>` / 文件名 = 显示名（= 文件名去掉 `.json`）；保存时按"安全文件名词干"校验（见 §5.4）。
- **数据目录不可用**（`data_dir` 为空）⇒ 显式中文错误「格式预设目录不可用」，**绝不回落到当前工作目录**写文件。
- 文件写入走**原子写**（`write_atomic_file`），不会出现半截 JSON。

### 2.1 首启 seed（幂等，一次）

程序启动时（主窗口初始化，进程内只执行一次，失败只记日志、不弹框）：

| 步骤 | 行为 |
|---|---|
| ① 迁移清理 | 删除数据目录各格式子目录下**所有** `_builtin-*.json`（0.4.0.alpha1/alpha2-dev 时代的只读出厂模板，见 §7.4）；数量记 `removed_legacy` |
| ② 跳过条件 | 工厂目录 == 数据目录（便携模式）或工厂目录为空 ⇒ **跳过复制** |
| ③ 逐文件复制 | 目标不存在 → 先用 `load_format_preset` 校验工厂文件，合法则**原字节复制**（原子写）⇒ `written++`；目标已存在 ⇒ `skipped++`（**一个字节都不动**，你改过的 `default` 永不被覆盖）；工厂文件非法 ⇒ `failed++` + 中文原因（不写坏文件） |

- **不重新格式化**：seed 是字节复制，工厂文件里的缩进/键序原样落到数据目录（§7 的「人类可直接改/复制」由此成立）。
- 日志字段：`written / skipped / removed_legacy / failed`（info 级）。
- 幂等：**非便携模式**首启 `written=11 / skipped=0`，同版本重复启动 `written=0 / skipped=11`；**便携模式**（工厂目录 == 数据目录）
  首启即 `written=0`（只清理旧文件、不复制）。升级到新版本再启动也只补缺、不动已存在的文件（**升级不自动更新你改过的 default**，如何拿到新出厂内容见 §7.2）。

---

## 3. JSON 结构与完整示例

### 3.1 完整示例（工厂文件 `presets/formats/heif/default.json` 逐字）

```json
{
    "adaptive": {
        "enabled": true,
        "policy": [
            "# HEIF default：quality 从当前值向上每 4 分试到 90；到 90 仍不达标则输出最后一轮。",
            "version 1",
            "target 90",
            "max_rounds 30",
            "output last",
            "",
            "stage quality-up",
            "  search quality from current up to 90 step 4 max_rounds 30"
        ]
    },
    "backend": "x265",
    "bitdepth": 8,
    "format": "heif",
    "kind": "format-preset",
    "name": "default",
    "params": {
        "chroma": "420",
        "lossless": false,
        "preset": "slow",
        "quality": 50,
        "tu-intra-depth": 2,
        "tune": "ssim"
    },
    "tech": "runtime",
    "version": 1
}
```

> **工厂文件写法**：默认 **4 空格缩进、键名排序（与运行时写盘同一份 JSON 归一规则）**、UTF-8；
> `adaptive.policy` 一律写成 **JSON 字符串数组、一行一个元素**（沉盘零 `\n` 转义、可读、可 git diff）。
> read 旧形态（单个字符串，`\n`/`\r\n` 分隔）仍兼容，再次保存即升级为数组。内存/引擎表示不变
> （仍是单个 `\n` 连接字符串）。详见 [自适应策略手册](adaptive-policy.md) §11。
>
> 全部 11 个工厂文件的位置/内容/策略全文见 §7 与 [自适应策略手册](adaptive-policy.md) §10/§10.1。

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
| `name` | string | **是** | — | 显示名；保存时决定文件名 `<name>.json`；长度 ≤120 字节（按字节，非字符）；中文可用。**每格式出厂预设的 `name` 恒为小写 `default`** |
| `format` | enum | **是** | — | 8 格式之一；**作用域键**：只作用于该格式卡，与目标卡不符 ⇒ 拒绝 |
| `backend` | string | 否 | `""` | 空 = 载入时解析为**当前首选后端**；非空必须存在于该格式 |
| `tech` | string | 否 | `""` | 空 = 当前首选技术；libheif 系（heif/avif）只接受空或 `"runtime"`（运行时内省技术） |
| `bitdepth` | int enum | 否 | `0` | `0` = **不覆盖**（载入后按格式默认补齐：jpeg 8 / jxl 16 / png 16 / tiff 16 / webp 8 / bmp 24 / heif 8 / avif 10）；其余必须在该格式能力列表内 |
| `params` | object | 否 | `{}` | 该格式参数目录的键/值快照（**三态**：无键 = 未设置）；值只能是标量（数字/布尔/字符串）；`__` 前缀键**不落盘**（由 `reserved` 重建） |
| `reserved.heif_profile` | int `0..21` | 否 | 无 | HEIF/AVIF 编码档位序号（0=Auto…20=`Main444_10Still`、21=`Main444_12Still`）。HEIF 拒绝 AVIF 档（High/Professional），AVIF 拒绝 HEVC 档；16（`Main10Still`）实测不可用 ⇒ 拒绝 |
| `adaptive.enabled` | bool | 否 | `false` | `true` 时 `policy` 必须非空 |
| `adaptive.policy` | string[]（旧 string 兼容读） | 否 | 空 | 行式 DSL v1 文本，**一行一个数组元素**（可读、零 `\n` 转义）；总长 ≤16 KiB；详见 [自适应策略手册](adaptive-policy.md) §11 |

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
[格式预设] [下拉 160px ▼] [保存] [另存] [删除] [导入] [导出]      ← 行内间距 6
```

**0.4.0 起下拉里不再有「未选择」项**，也不存在「未选择」状态：

- 格式卡创建 / 首次进入时，程序**自动选中并静默载入**该格式的 `default`（载入失败只记日志 + 注释位中文错误，不弹窗）；
- 自适应摘要因此**始终**按当前预设的策略显示（`default` 有策略就显示，无策略就隐藏——不是"未选择所以隐藏"）；
- 列表为空只可能出现在数据目录不可用的错误态（此时无选中项，按钮按既有禁用语义）。

| 动作 | 行为 |
|---|---|
| 打开格式卡 | **自动载入 `default`**（无「未选择」中间态） |
| 选中下拉项 | 立即载入：先 `load`（含完整校验）→ 格式匹配检查 → `normalize` 补默认 → 应用到卡片；补齐默认项时提示「已载入（已补齐 N 个默认项）」 |
| 保存 | 用当前卡片快照覆盖**选中的文件**。`default` **可以保存**（= 直接修改 `default.json`，从此该格式默认值就是你的值）；保存前先校验，不通过只提示、**不写盘**（避免"先污染后报错"） |
| 另存 | 打开命名对话框 → 输入新名字 → 校验 → 写入 `<format>/<新名>.json`（复制当前值，原预设不动）。当前是 `default` 时建议名 = 「default 副本」（避免一次回车就覆盖出厂默认文件） |
| 删除 | 二次确认；只接受绝对路径的普通文件（拒绝目录/不存在的路径）。**`default` 不可删除**（按钮置灰 + 原因 tooltip；I/O 层同样拒绝并给中文原因）——要"恢复默认"就重新「保存」覆盖它，或「另存」复制一份再改 |
| 导入 | 打开文件对话框，选择任意位置的 JSON → 载入 + 校验 → 规范化后写入本格式目录（可顺带改名）。**不允许用导入顶替 `default`**（中文拒绝：改名后再导入；要改 default 请选中它后用「保存」） |
| 导出 | 打开文件对话框，把当前卡片快照校验后写到用户指定路径（不改变本格式目录） |

- **预设文件被外部删除** ⇒ 自动重新选回 `default` 并载入（自愈；不再有"未选择"分支）。
- **载入失败** ⇒ **保留当前卡值**并显示错误（坏文件绝不会静默套默认值，`classify.json` 的 P1 教训）。
- 卡值的真源仍是格式卡的控件/参数集；预设只是一个"写入器/读取器"。
- `保存 → 载入 → 保存` **字节稳定**（`QJsonDocument::Indented` + 键序/类型归一稳定）：可以用 git 管理这些文件。
- **全局预设载入后的来源显示（0.4.2 起；取代 0.4.0/0.4.1 的 detached 态）**：大预设会覆盖全部卡值，
  且**不再假装高亮 `default`** —— 格式预设下拉**无选中项**，占位显示「来自大预设：<大预设名>」；
  注释位写「值来自大预设「<名>」（基于格式预设 `<stem>` / 自定义参数）」（`<stem>` = 大预设文件里该输出
  的 `source_preset` 注记，可空 = 自定义参数）；「保存」「删除」禁用（没有绑定文件；tooltip 指路
  「另存」），另存 / 导入 / 导出可用；重新选中任一格式预设（含 `default`）即恢复绑定、`保存` 恢复可用
  （防止把大预设值误写回格式预设文件）。

> **0.4.0.alpha2 用户修订（保持）**：格式预设行 = `[格式预设][下拉 160px][保存][另存][删除][导入][导出]`
> —— 下拉宽 = 同页参数行字段宽 **160px**；各按钮用固定 `sizeHint` **不截断**；行内间距 6。
> 按钮文案**不再带省略号**（`另存` / `导入` / `导出`；用户的反馈是省略号被当成"文字被截断"）。
> 打开命名/文件对话框的语义不变（见上表）。

> dev harness：`photopipeline --dev … --preset <预设文件>` 也接受**格式预设**（按根 `kind` 分派；施加到输出 #0，
> 含参数/位深/`heif_profile`/`adaptive`），全局预设文件在同一条命令里同样可用（A14；见
> [自适应手册](adaptive-policy.md) §11.1）。**注意**：dev/CLI 默认路径**不自动载入** `default`（默认预设是 GUI 行为），
> 未给 `--preset` 时行为与 0.3.x 单次编码一致——这是回归 zero diff 的根基（设计 §8.3 M12 修订）。

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

全部通过才应用到卡片。`default` 名单源 = `core/format_preset.h` 的
`kFormatPresetDefaultName = "default"`（UI 删除保护/自动选中都用它；文件 stem 与 JSON `name` 都为 `default`）。

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
> 想让文件名与显示名一致，存一次即可。出厂 `default` 的文件名与 `name` 都是小写 `default`。

---

## 6. 与全局预设的关系：无隐式叠加

- **不叠加**：格式预设载入只写**当前格式卡**；全局预设载入覆盖**全部**卡。
- 全局预设载入后，格式预设行**显示真实来源**（下拉无选中 + 占位「来自大预设：<名>」+ 注释位标注
  「基于格式预设 `<stem>` / 自定义参数」，保存 / 删除禁用；见 §4 与 §6.1）；重新选中任一格式预设即恢复绑定。
- 两类目录**互不扫描**、互不触发写盘：格式预设不会出现在全局预设下拉里，反之亦然。
- 优先级不存在"谁覆盖谁"的隐式规则：都是显式动作生效，最后一次载入/编辑决定当前卡值。

### 6.1 大预设里的来源注记（0.4.2 加性字段）

> 一句话：大预设 `outputs[i]` 可带可选字符串 **`source_preset`**，只用于显示 / 溯源，**不参与产物**。

| 项 | 口径 |
|---|---|
| 语义 | 该输出最近一次显式绑定的**格式预设文件名 stem**（不含路径/扩展名）；空 / 缺省 = 自定义参数 |
| 自包含 | 大预设的落值快照仍完整自包含：格式预设文件在本机不存在也能载入（注记只影响来源显示） |
| 写出 | 空**不写**该字段 ⇒ 0.4.1 及更早写出的旧文件 `load→save` **逐字节不变**；`version` 仍为 2（**不 bump v3**，先例 = M10-A20 `adaptive` 加性块） |
| 读取 | 旧版本读新文件仅忽略该字段（只丢注记不丢值）；新版本读旧文件 = 无来源注记 |
| 校验 | `source_preset` 非 string → 显式报错（不静默忽略）；未知根/输出字段 → 忽略 + `log_warn`（不静默吞） |

---

## 7. 工厂预设（出厂文件）

> **一句话**：出厂预设 = **仓库里的 11 个 JSON 数据文件**（`presets/formats/<format>/<stem>.json`），
> 是格式预设的**单一真源**；随发行包分发、首启 seed 到数据目录。它们就是普通 JSON ——
> **人类可直接打开改、可整个文件复制成自己的预设**。

| 格式 | 工厂文件 | JSON `name` | 后端 · 技术 | 位深 | 参数（快照） | 自适应策略 |
|---|---|---|---|---|---|---|
| HEIF | `heif/apple-adaptive.json` | `Apple自适应` | `x265` · `runtime` | 10 | `chroma=420`、`preset=slow`、`tune=ssim`、`x265:aq-mode=3`、`x265:crf=20`、`x265:level-idc=31`（L3.1，CRF 路径不写 quality）；档位 `reserved.heif_profile=2`（Main10） | 先降 CRF（20 起、步长 2 到 8），再切 4:4:4 + Main 4:4:4 10 Still 兜底（全文见 [自适应手册](adaptive-policy.md) §10） |
| HEIF | `heif/default.json` | `default` | `x265` · `runtime` | 8 | **运行时内省默认值的静态快照**（`chroma=420`、`lossless=false`、`preset=slow`、`quality=50`、`tu-intra-depth=2`、`tune=ssim`；快照冻结在工厂文件里，不依赖用户机器上的 libheif 版本） | quality 从当前值 +4 升到 90（max 30） |
| JXL | `jxl/lossless-archive.json` | `无损归档` | `libjxl` · `modular` | 16 | `lossless=true`、`effort=7` | 无（无损，评分≈100） |
| JXL | `jxl/adaptive-compress.json` | `自适应压缩` | `libjxl` · `vardct` | 16 | `distance=1.0`、`effort=4` | distance 每 −0.1 降到 0.1（max 20） |
| JXL | `jxl/default.json` | `default` | `libjxl` · `vardct` | 16 | 静态表默认全量快照（9 键：`brotli_effort=-1`、`codestream_level=-1`、`decoding_speed=0`、`distance=1.0`、`effort=7`、`epf=-1`、`lossless=false`、`photon_noise=0`、`resampling=-1`） | distance 断点降档（1 以下 0.1 / 1–10 步长 2 / >10 步长 5；max 30） |
| AVIF | `avif/default.json` | `default` | `svt-av1` · `runtime` | 10 | `chroma=420`、`quality=85` | quality 升到 95 → chroma 444 → tune psnr（max 8） |
| WebP | `webp/default.json` | `default` | `libwebp` · `lossy` | 8 | 静态默认全量（`quality=85` …） | quality 升到 95 → method 6（max 8） |
| JPEG | `jpeg/default.json` | `default` | `jpegli` · `dct` | 8 | 静态默认全量；`quality_mode=distance`、`distance=3.0`（`quality=90` 仅在 quality 模式生效） | **JPEG 新策略**：distance 3.0 起 1–3 段步长 1 粗扫、1 以下步长 0.1 精扫到 0.1（max 20；全文见 §7.1） |
| PNG | `png/default.json` | `default` | `oiio` · `deflate` | 16 | `compression="default"`、`compressionLevel=6`、`filter=0` | 无（无损） |
| TIFF | `tiff/default.json` | `default` | `oiio` · `codec` | 16 | `compression="lzw"`、`deflate_level=6`、`predictor=2`、`tiff_tile_*=0` | 无（无损） |
| BMP | `bmp/default.json` | `default` | `oiio` · `raw` | 24 | 无参数 | 无（无损） |

> **清单口径**：11 个 = **8 个格式各一条 `default`** + HEIF「Apple自适应」+ JXL「无损归档」+ JXL「自适应压缩」。
> `default` 一律**小写**（文件 stem 与 JSON `name` 一致）。
> **需求 2 简化**：HEIF/JXL 的历史内建（「内置示例 · HEIF/AVIF/JXL/WebP/JPEG」与 `_builtin-heif-default`/`_builtin-jxl-default`
> 等）**不再保留**——新 `default` + Apple自适应/无损归档/自适应压缩取代；AVIF/WebP/JPEG 历史内建内容即新 `default`；
> PNG/TIFF/BMP 新增 `default`。

### 7.1 JPEG `default` 策略全文（逐字）

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

- 起点 `current` = 载入后的卡值 `distance=3.0`；候选序列（逐点单测 `pp_test_adaptive_exec` 钉住）：
  `2, 1, 0.9, 0.8, …, 0.1`，共 **11 个**候选（跨断点先钳到断点值 `1`，再从 1 以下按 0.1 走）。
- 语义与参数清单同步：JPEG `distance` 的 **UI 范围 0.3–3.0 → 0.1–3.0**（R-294 修订；否则 0.1 段不可达），
  见 [参数清单](param-catalog.md) §1。
- 为什么默认开自适应：JPEG 的 `distance` 直接决定体积/画质，用"从粗到细"的降档把"刚好 90 分"的那一档找出来，
  比固定一个距离更省体积（代价 = 多轮编码 + 评分，见 [自适应手册](adaptive-policy.md) §15 成本模型）。

### 7.2 `default` 语义：不可删除，但可以「保存」覆盖

| 动作 | 对 `default` 的表现 |
|---|---|
| 删除 | **拒绝**：UI 按钮置灰 + tooltip「出厂默认预设不可删除：可直接「保存」修改它，或用「另存」复制一份」；I/O 层防御性拒绝（中文原因） |
| 保存 | **可用**：直接覆盖数据目录里的 `default.json` ⇒ 该格式从此以你的值为默认（下次启动/换格式自动载入的就是它） |
| 另存 | 可用：复制成新名字，原 `default` 不变（当前是 `default` 时建议名 =「default 副本」） |
| 导入 | 可用，但**不能用导入顶替 `default`**（中文拒绝：改名后再导入；要改 default 请选中它后用「保存」） |
| 导出 | 可用 |
| 首启 seed | **不会**覆盖已存在的 `default.json`（哪怕你改过）。恢复出厂内容：**非便携**模式可删掉数据目录的 `default.json` 再启动（seed 会从工厂目录重写）；**便携**模式（数据目录 == exe 旁）没有独立工厂副本，直接从安装包/仓库把 `presets/formats/<format>/default.json` 复制回来 |

> **升级提示（便携模式）**：便携模式下数据目录 == exe 目录，新的发行包解压覆盖同名文件时**会覆盖你改过的
> `presets/formats/**`。升级前请先备份 `presets/`（或把改动另存成别的名字，`default` 之外的预设不会被新包覆盖）。

### 7.3 与自适应策略的关系

- 11 条工厂预设里：**无自适应** = JXL「无损归档」、PNG/TIFF/BMP 的 `default`（无损，评分≈100，策略无意义）；
  其余 **7 条**自带 `adaptive.enabled=true` + 策略原文（HEIF 2 条 / JXL 2 条 / AVIF / WebP / JPEG），
  载入后格式卡显示只读摘要，开关默认**开**。
- 关掉开关 = **本次运行**不启用（预设文件里的 `enabled` 不改；下次载入仍按文件）。
- 策略只在预设文件里：GUI 没有编辑器。要改策略就改工厂/数据目录的 JSON（`adaptive.policy` 是**数组，
  一行一个元素**；写法见 [自适应策略手册](adaptive-policy.md)），再重新选中载入；或「另存」一份改副本。
- 与全局预设的关系：全局预设 v2 的输出级也可带同样的 `adaptive` 块（见 §8.1），两者互不叠加。

### 7.4 旧 `_builtin-*` 迁移（破坏性，登记）

0.4.0 首次启动会**删除**数据目录各格式子目录下所有以 `_builtin-` 开头的 `*.json`（`removed_legacy` 计数）。
这些文件是 0.4.0.alpha1/alpha2-dev 的**只读出厂模板**（C++ `builtin_format_presets()` 幂等 seed 的产物），
0.4.0 起已被 11 条工厂 `default`/Apple自适应/无损归档/自适应压缩取代：

- **保留不受影响**：用户用「另存」存出的普通名字预设（不含 `_builtin-` 前缀）不被删除；
- **删除即不可恢复**（数据目录里的那一份）；历史内容可由 0.4.0.alpha1/alpha2-dev 源码与发布档案回溯；
- 只在数据目录清理，发行包与仓库 `presets/` 目录里不存在 `_builtin-*` 文件（单测断言"无 `_builtin-` 文件"）。
- 登记：[docs/m12-adjudications.md](m12-adjudications.md) A2。

---

## 8. 与自适应策略的关系

- `adaptive` 是预设的**可选块**：策略只随预设持久化，不单独存文件、不进设置项。
- 载入预设后：格式卡显示**只读摘要**（`含自适应策略：目标 90 / 最多 8 轮 / 3 个阶段`）+ **启用开关**；
  **没有策略编辑器**（用户裁决，设计 §9 引言）。
- **0.4.0 默认行为（重要）**：格式卡自动载入出厂 `default`，其中 JPEG/HEIF/AVIF/WebP/JXL 的 `default`
  自带策略 ⇒ 这些格式**默认就是启用自适应**的（可以随时用开关关掉，或换一份无策略的预设）；
  PNG/TIFF/BMP 的 `default` 无策略 ⇒ 自适应行隐藏、行为与 0.3.x 一致。
- **引擎/CLI 默认不变**：`--dev`、金样、回归等不带预设的路径**没有策略** ⇒ 产物与 0.3.x 逐字节相同（C25）。
- 策略为空 / `enabled=false` ⇒ 自适应关闭。
- 出厂预设自带策略，可「另存为」后改文本（写法见 [自适应策略手册](adaptive-policy.md)）。

### 8.1 全局预设也携带同一个 `adaptive` 块（0.4.0 / A20）

格式预设与全局预设 v2 的 **`outputs[i]`** 都支持同一个可选块（**加性**，旧文件逐字节不变）：

```json
"outputs": [
  {
    "format": "jpeg", "backend": "jpegli", "tech": "dct", "bitdepth": 8,
    "params": { "quality_mode": "quality", "quality": 90 },
    "adaptive": {
      "enabled": true,
      "policy": ["version 1", "target 90", "max_rounds 4", "",
                 "stage q-up", "  search quality from current up to 95 step 2"]
    }
  }
]
```

| 面 | 行为 |
|---|---|
| 写盘 | 当前格式卡的自适应会话态 = 开关 + 策略原文（**policy 数组，一行一个元素**，与格式预设同一编码）；**关掉开关也保留策略文本**（下次打开不丢） |
| 缺省 | `enabled=false` 且 `policy` 空 ⇒ **不写** `adaptive` 字段（旧全局预设零变化，C25） |
| 读旧文件 | 没有 `adaptive` 字段 ⇒ `enabled=false` / `policy` 空（自适应关闭，行为与 0.3.x 相同）；旧字符串形态 `policy` 仍兼容读 |
| 载入全局预设 | 覆盖全卡（含自适应）：格式卡只读摘要/开关同步为该值；格式预设行显示真实来源（无选中占位「来自大预设：<名>」+ 保存/删除禁用，§4/§6.1），值以全局预设为准 |
| 运行门控 | 只有 `enabled=true`（且策略非空、解析通过）才把策略交给引擎；否则**不下发策略**，走既有单次编码路径（产物逐字节不变） |
| dev harness | `--dev --preset <全局预设>` 的全局预设分支**也消费** `outputs[0].adaptive`（A14 收口补丁加性 3 行；无 `adaptive` 的旧预设行为逐字节不变）；与 `--adaptive-policy` 互斥 |
| 结构错误 | `adaptive` 不是对象 / `enabled` 非布尔 / `policy` 非字符串或字符串数组 ⇒ 载入**显式报错**（不静默当关闭） |

> 一句话：`adaptive` 是「输出级」属性，全局预设与格式预设各存一份、**互不叠加**；
> 最后一次显式载入（或开关动作）决定当前生效状态。

---

## 9. 常见问题

**Q：为什么下拉里没有「未选择」了？**
A：0.4.0 起每个格式都有一份出厂 `default`，格式卡打开即自动载入——"没选预设"不再是一个有意义的状态。
只有数据目录不可用（错误态）才会没有选中项。删除 `default` 也不允许（§7.2）。

**Q：我改的 `default` 会影响别人/下次升级吗？**
A：「保存」只改**你的数据目录**里的那份（便携模式下 = exe 旁，仅本机）；它不会进发行包、也不发包给他人。
升级到新版本时，seed **不会覆盖**已存在的 `default`（新出厂内容需要手动复制回数据目录，§7.2）。

**Q：升级后 `_builtin-*.json` 不见了？**
A：0.4.0 首次启动会迁移删除旧只读模板（§7.4，登记为破坏性变更）。你现在用的是新的 11 条工厂预设。

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
仓库里的 `presets/formats/**` 本身就是进版本库的出厂数据（也是改出厂值的正确位置——改完提交，随下一版随包分发）。

**Q：JPEG 为什么默认开自适应？会不会变慢？**
A：为了在"刚好 90 分"处省体积（§7.1）。代价是每个 JPEG 输出最多跑十几轮编码+评分（每轮成本见
[自适应手册](adaptive-policy.md) §15）；不需要就在格式卡上关掉「启用自适应」，或把 JPEG `default` 的
`adaptive.enabled` 改成 `false`。

---

## 10. 相关文档

- 设计：[`v0.4.0-design.md`](v0.4.0-design.md) §8（格式预设；**§8.3/§8.5 含 M12 修订**）、§9（自适应）、§11（迁移与兼容）、§12.5（验收探针）
- 工厂数据文件：仓库 `presets/formats/**`（11 个；单一真源）
- 验收：`build/m10/w3-core/report.md`（W3-T1：core+io、JSON 规则、校验链、API 清单）·
  `build/m10/w3-preset/report.md`（W3-T5：全局预设 `adaptive` 往返、内置预设 seed/只读、GUI 接线、探针与验收数字）·
  `build/m10/final-gate/report.md`（M10 最终树门禁）· M12 门禁读数见 [docs/0.4.0-report.md](0.4.0-report.md) §2
- 用户手册：[自适应策略](adaptive-policy.md) · [参数清单](param-catalog.md)
- Schema：[`format-preset.v1.schema.json`](schemas/format-preset.v1.schema.json) · [`adaptive-policy.v1.schema.json`](schemas/adaptive-policy.v1.schema.json)
- 登记：[M10 裁定（A8/A9/A13/A14/A20）](m10-adjudications.md) · [M11 裁定](m11-adjudications.md) · [M12 裁定（A1–A9）](m12-adjudications.md) ·
  [M10 工作流（W3-T1..T7）](m10-workflow.md) · [M10 收口报告](m10-report.md) · [M11 收口报告](m11-report.md)（历史）
