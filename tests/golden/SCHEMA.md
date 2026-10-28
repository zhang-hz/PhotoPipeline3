# expected.json 断言 schema（M0 冻结 v1；M2-T8 追加值规范化与 pixel 省略口径；M4-T5 追加 outputs[] v2；
# M8-C13 追加 sidecar `metric` 与 `assert.score` 区间断言）

- case: string（用例名）
- input: string（相对 tests/golden 的输入路径；多产物对的默认像素参照）
- output_format: string（格式 id；多产物用逗号连接，纯记录）
- assert.pixel.mode: "exact" | "psnr"
  - exact: 解码回读逐位相等（无损路径）
  - psnr: 回读 PSNR ≥ assert.pixel.threshold_db 才 PASS
  - **webp 产物的通道对齐（M4-T5）**：webp 仍由 libwebp 解码（M1 冻结口径：绝不用 OIIO 的 WebP
    reader，它对带 alpha 的文件返回预乘 RGB）。当**参照**为 3 通道（无 alpha 源）时，产物按
    `WebPDecodeRGB` 解码为 3 通道 —— 仅消除 RGBA 扩张造成的伪通道不匹配；参照为 4 通道（或
    灰度，此类对一律用 `"pixel": {}`）时保持 `WebPDecodeRGBA` 的既有行为。
  - **省略口径（M2-T8 ②）**：`"pixel": {}` 表示该对不做像素比较。仅用于像素通道数被设计改变的
    对（灰度源→RGB 编码器 `gray-webp`、alpha 拍平 `alpha-jpeg`：输入 1/4 通道 vs 输出 3 通道，
    `exact`/`psnr` 的几何前置条件 same_geometry 必然失败）。这类对的契约由 warnings_contain 与
    metadata 断言承担；像素正确性由既有单测（test_color/test_pipeline_contract）覆盖。
- assert.pixel.threshold_db: number（psnr 时必填）
- assert.metadata: [{ key: string, op: "eq"|"exists"|"absent", value: string }]
  - key 为 Exiv2 完整标签名（Exif.* / Xmp.*）
  - `value` 用**下方规范化文本**（M2-T8：#26 已销账）比较，与 Exiv2 的 `print()` 无关
- assert.warnings_contain: [string]（WarningKind 名，输出警告必须包含所列项）
  - 来源：`photopipeline --dev` 写出的 `<输出>.pp.json` sidecar（解码后的图像无法携带警告）
- assert.warnings_exact: [string]（M7-WP3 / R-040 **加性**扩展）：sidecar 的 WarningKind
  **全集**必须与所列**多集合**逐项相等（重复次数计入；顺序无关）。键存在即生效，
  `"warnings_exact": []` = 全集必须为空（与 `warnings_empty: true` 同义）。
- assert.warnings_empty: bool（M7-WP3 / R-040 加性扩展）：true = 该产物必须零告警。
- 三模式并存时各自独立判；**warnings_contain 语义逐字不变**（只判包含）。给出任一模式
  即要求 sidecar 存在（缺失 = FAIL）。三者都缺省 = 不做告警断言（v1 既有行为）。

## outputs[]（M4-T5 追加，v1 字段全部保留兼容读取）

多产物对（1 源 → N 输出）在顶层追加 `outputs` 数组；**携带 outputs[] 时 `pp_verify` 的第二参数
是用例输出根目录**（不带时是产物文件本身，v1 口径逐字不变）：

- outputs[i].rel: string（相对用例输出根目录的产物路径）
  - **路径断言**：该路径必须存在且是常规文件，否则 FAIL（`output '<rel>' missing …`）
- outputs[i].format: string（格式 id，记录用）
- outputs[i].input: string（可选）覆盖顶层 input —— 一个用例的多个产物对应不同源时使用
  （如 `multiformat-conflict`：产物分别来自 `base/rgb8.png` 与 `base/rgb8.tif`）
- outputs[i].assert: object（可选；缺省回落到顶层 `assert`）
  - 结构与顶层 `assert` 相同（pixel / metadata / warnings_contain / warnings_exact /
    warnings_empty），**逐产物独立断言**；warnings 从该产物自己的 `<rel>.pp.json` sidecar 读取。

产物顺序 = RunConfig.outputs 的**配置顺序**（编码顺序是内部实现细节，§4.3）。

## progress_trace（M4-T6 追加，v1/v2 皆可叠加）

`--dev` 在 `<--out 根目录>/progress-trace.jsonl` 落**进度事件流**（每行一个 JSON 对象 = 一个
`pp::FileEvent`；由 `photopipeline --dev` 的 `ProgressTraceSink` 写出，见 src/main.cpp）。
它不是断言产物，而是进度口径的**可复核证据**：金样 `progress-trace` 对用它锁住 §7 的单调性与
真实/合成口径（依据 docs/v0.3.0-design.md §7.1–§7.4）。

事件行字段（逐行 JSON Lines，UTF-8）：

| 字段 | 类型 | 说明 |
|---|---|---|
| `seq` | number | 全局单调序号（写入顺序 = 递达顺序） |
| `file` | number | 批内文件下标（= `FileEvent::index`，按输入顺序） |
| `state` | string | `queued`/`probing`/`decoding`/`orienting`/`coloring`/`flattening`/`encoding`/`writing`/`progress`/`done`/`skipped`/`failed`/`cancelled`（`FileState` 的小写名） |
| `state`（**加性追加** M7R22G3-1） | string | `done_with_errors`（`FileState::DoneWithErrors` 的小写名；多输出**部分成功**的源文件级终态）——上行 v1 词表行**逐字不动**，本值系加性补档（枚举 = `src/core/pipeline.h` 末尾加性值，M7-WP1/I7；`state_text` 分支 = `src/dev_cli.cpp`，M7-R2.1-F4），命名沿用「`FileState` 的小写名」词表口径 |
| `output_index` | number | `-1` = 源文件级（probe/decode/orient/color）；`≥0` = 输出级（`RunConfig.outputs` 的配置顺序下标） |
| `stage` | string | `Probe`/`Decode`/`Orient`/`Color`/`Flatten`/`Encode`/`MetaWrite`/`Done` |
| `stage_frac` | number | 阶段内 0..1 |
| `overall_frac` | number | 文件整体 0..1（§7.2 权重表：3%+27%+10%+50%+10%） |
| `synthetic` | boolean | `true` = 合成进度（编码器无回调，§7.3；UI 斜纹）。真实进度不得被合成覆盖（§3.1） |
| `t_ms` | number | 相对本进程侧车起点的毫秒（记录用，**不参与断言**） |

expected.json 的 `progress_trace` 段：

- `progress_trace.file`: string（可选，缺省 `progress-trace.jsonl`）——相对**用例输出根目录**
- `progress_trace.expect_outputs[]`: 逐输出下标的期望（下标 = `output_index`）
  - `index`: number（必填，≥0）
  - `synthetic`: boolean（必填）—— 该下标的**所有**进度事件必须等于此值
  - `reported`: boolean（可选）—— 断言该产物 sidecar（`outputs[index].rel` 或本项的 `rel` 的
    `<rel>.pp.json`）里的 `progress_reported`（§7.4 快照键）与此一致；并同向断言
    `progress_max_row`（真实面 > 0、合成面 = 0）
  - `rel`: string（可选）—— 显式指定 sidecar 定位（缺省取 `outputs[index].rel`）
  - `format`: string（可选，记录用）

断言（全部通过才 PASS；任一失败把原因拼进 detail）：

1. 侧车文件存在、逐行可解析（JSON Lines）、非空。
2. 逐文件（`file` 列）的 `overall_frac` **单调不倒退**（容差 1e-6）且 ∈ [0,1]。
3. `expect_outputs[]` 每个下标：至少一个事件，且该下标所有事件的 `synthetic` 与期望一致。
4. `reported` 给定时：sidecar 的 `progress_reported`/`progress_max_row` 与期望同向一致。
5. 终态为 `done` 的文件必须存在 `overall_frac == 1.0` 的进度事件（§7.2 权重合计=1 的可观测面）。

调用：`pp_verify tests/golden/smoke/progress-trace.json <用例输出根目录>`（携带 `progress_trace`
时第二参数**必须**是目录，否则 FAIL）。`--selftest` 覆盖三条探针（通过 / 单调性违例 / synthetic
口径违例）。

## sidecar `metric`（M8-C13 追加；仅 `--dev` 评分开启时写出）

`photopipeline --dev --score` 在 `<产物>.pp.json` sidecar **顶层追加** `metric` 对象（字段单源 =
`src/core/metric/metric.h` 的 `pp::metric`；契约 = docs/m8-encoder-params-and-ssimulacra2-design.md
C9–C13）。评分关闭（`Off`）/仅元数据运行：**不写**该键，sidecar 与既有口径逐字不变。

| 字段 | 类型 | 取值 / 语义 |
|---|---|---|
| `name` | string | `"ssimulacra2"`（`pp::metric::metric_name()` 单源） |
| `version` | number | 算法版本整数，当前 `1`（= SSIMULACRA 2 算法 2.1） |
| `state` | string | `"ok"`（分数有效）/ `"unavailable"`（回读失败、尺寸不符、取消等；不算产物失败）/ `"failed"`（指标内部错误；不算产物失败）—— `MetricState` 小写名；`Off` 不写 `metric` |
| `score` | number | 仅 `state == "ok"` 时有效：∈ (−inf, 100]，逐样本相同 = 100.0；负分合法、不 clamp |
| `ms` | number | 评分耗时毫秒（记录用，**不参与断言**） |
| `reference` | string | `"encoder-input"`：参考 = 编码器实际收到的量化后样本（不是源文件；C9） |
| `colorspace` | string | `"sRGB"`：参考/失真两侧统一归一到 sRGB 线性后评价（C9） |
| `note` | string | `unavailable`/`failed` 的中文短原因；`ok` 时为空串 |

分数语义 = **编码/量化保真度**（失真 = 解码回读的产物像素；参考 = 编码器输入），不是
“与源文件相似度”。真无损（逐位一致）应得 100.0；HEIF/AVIF 的“码流无损”因 RGB↔YCbCr 舍入
如实 <100。

## `assert.score`（M8-C13 追加；**区间断言，绝不逐值**）

`assert.score` 是对象，读取**该产物自己的** `<产物>.pp.json` sidecar 的 `metric` 对象
（`outputs[i].assert.score` 逐产物独立；顶层 `assert.score` 用于单产物对，语义相同）：

- 键存在即要求 sidecar 存在且含 `metric` 对象；sidecar 缺失 / JSON 无效 / 无 `metric` = FAIL。
- 支持且仅支持以下键（**未知键 = FAIL**，不允许静默忽略）：
  - `"state": string`：`metric.state` 必须**逐字相等**（如 `"ok"`；`"unavailable"` 可作状态反例锁定）。
    当要求的状态是 `"ok"` 时，额外要求 `metric.score` 是 number（`ok` 的 sidecar 契约）。
  - `"min": number`：要求 `metric.state == "ok"` 且 `metric.score >= min`。
  - `"max": number`：要求 `metric.state == "ok"` 且 `metric.score <= max`。
- `min` 与 `max` 同时给出 = **闭区间** `[min, max]`（端点含）；`state` 可与区间叠加（先判状态再判区间）。
- 空对象 `{}` = 只要求 `metric` 对象存在（不判值）。
- **区间断言，绝不逐值**：分数受编码器/线程/库版本影响，只锁语义区间（无损 min 99.0；有损
  min 50 / max 100；金样 4 个既有 case 覆盖，见 `tests/golden/smoke/*.json`）。
- 失败文案点名：sidecar 缺失 / JSON 无效 / `metric` 缺失 / `state` 不符 / `score` 非 number /
  越界（含实测值）。
- 调用/`--selftest`：`pp_verify` 的 `--selftest` 覆盖 `state` 正反例、`min`/`max`/闭区间
  正反例、sidecar/metric 缺失、`score` 非 number、未知键；回归（`tools/regression.py`）不带
  `--score` ⇒ 无 `metric` 键、既有断言零变化（C13）。

## 元数据值文本规范化（M2-T8 冻结，docs/m2-tasks.md §4 T8 ①）

`pp_verify` 读取元数据后按类型渲染为文本，规则逐字节稳定（跨运行/跨机器/跨容器）：

| 类型 | 规则 | 样例（原始 → 规范化） |
|---|---|---|
| unsignedRational / signedRational | 约分到最简 `num/den`；`den == 1` → 整数形式；多元素以 `, ` 连接 | `28/10` → `14/5`；`4944/100` → `1236/25`；`31/1` → `31` |
| asciiString | 去尾部 NUL 与首尾空白（字节原样保留，UTF-8 不转码） | `"  padded\0\0"` → `padded`；`N` → `N` |
| 数组（元素型类型 count > 1） | 逐元素渲染后以 `, ` 连接；**仅限数值型与 XMP 集合型**（xmpBag/xmpSeq/xmpAlt/langAlt）——字符串型（asciiString/IPTC string/comment/xmpText/date/time）的 `count()` 是字节长度，按标量处理 | `16 16 16` → `16, 16, 16`；`2.3.0.0`(GPSVersionID) → `2, 3, 0, 0`；xmpText `PhotoPipeline-M0` 保持单值（不重复 16 次） |
| undefined（二进制） | `0x` + 小写十六进制；超过 64 个十六进制字符截断并加 `...` | `48 50 51 50` → `0x30323332` |
| 其余（Short/Long/Signed/Date 等，标量） | 沿用 Exiv2 既有文本（`print()`） | `top, left`；`300`；`2024:01:01 00:00:00` |
| XMP | 同一渲染器：xmpText 等字符串型是标量，xmpBag/xmpSeq/xmpAlt/langAlt 按元素 | `PhotoPipeline-M0`；`alpha, beta`；`lang="x-default" …` |

退化守卫（规则未定义、语料无实例）：`den == 0` 原样输出 `num/0`，不做约分。
`--selftest` 覆盖上表每一类（含 40 字节 undefined 的截断样例）**以及 outputs[] 三条探针**
（逐产物定位/路径断言/断言按产物绑定）。

示例（v1 单产物）:
```json
{
  "case": "exif-roundtrip",
  "input": "meta/exif_full.jpg",
  "output_format": "jpeg",
  "assert": {
    "pixel": { "mode": "psnr", "threshold_db": 35 },
    "metadata": [
      { "key": "Exif.Image.Artist", "op": "eq", "value": "M0" },
      { "key": "Exif.GPSInfo.GPSLatitude", "op": "eq", "value": "31, 13, 1236/25" },
      { "key": "Exif.GPSInfo.GPSVersionID", "op": "eq", "value": "2, 3, 0, 0" }
    ],
    "warnings_contain": []
  }
}
```

示例（v2 多产物；`multiformat-conflict` 的命名 + 逐产物像素口径）:
```json
{
  "case": "multiformat-conflict",
  "input": "base/rgb8.png",
  "output_format": "jpeg,webp",
  "outputs": [
    { "rel": "jpeg/rgb8.jpg", "format": "jpeg", "input": "base/rgb8.png",
      "assert": { "pixel": { "mode": "psnr", "threshold_db": 35 },
                  "metadata": [], "warnings_contain": [] } },
    { "rel": "jpeg/rgb8 (1).jpg", "format": "jpeg", "input": "base/rgb8.tif",
      "assert": { "pixel": { "mode": "psnr", "threshold_db": 35 },
                  "metadata": [], "warnings_contain": [] } },
    { "rel": "webp/rgb8.webp", "format": "webp", "input": "base/rgb8.png",
      "assert": { "pixel": { "mode": "exact" }, "metadata": [], "warnings_contain": [] } },
    { "rel": "webp/rgb8 (1).webp", "format": "webp", "input": "base/rgb8.tif",
      "assert": { "pixel": { "mode": "exact" }, "metadata": [], "warnings_contain": [] } }
  ]
}
```
调用：`pp_verify tests/golden/smoke/multiformat-conflict.json <用例输出根目录>`。

