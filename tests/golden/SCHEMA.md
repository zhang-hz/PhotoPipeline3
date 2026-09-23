# expected.json 断言 schema（M0 冻结 v1；M2-T8 追加值规范化与 pixel 省略口径；M4-T5 追加 outputs[] v2）

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

## outputs[]（M4-T5 追加，v1 字段全部保留兼容读取）

多产物对（1 源 → N 输出）在顶层追加 `outputs` 数组；**携带 outputs[] 时 `pp_verify` 的第二参数
是用例输出根目录**（不带时是产物文件本身，v1 口径逐字不变）：

- outputs[i].rel: string（相对用例输出根目录的产物路径）
  - **路径断言**：该路径必须存在且是常规文件，否则 FAIL（`output '<rel>' missing …`）
- outputs[i].format: string（格式 id，记录用）
- outputs[i].input: string（可选）覆盖顶层 input —— 一个用例的多个产物对应不同源时使用
  （如 `multiformat-conflict`：产物分别来自 `base/rgb8.png` 与 `base/rgb8.tif`）
- outputs[i].assert: object（可选；缺省回落到顶层 `assert`）
  - 结构与顶层 `assert` 相同（pixel / metadata / warnings_contain），**逐产物独立断言**；
    warnings 从该产物自己的 `<rel>.pp.json` sidecar 读取。

产物顺序 = RunConfig.outputs 的**配置顺序**（编码顺序是内部实现细节，§4.3）。

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

