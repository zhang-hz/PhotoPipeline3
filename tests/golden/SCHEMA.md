# expected.json 断言 schema（M0 冻结 v1；M2-T8 追加值规范化与 pixel 省略口径）

- case: string（用例名）
- input: string（相对 tests/golden 的输入路径）
- output_format: string（格式 id）
- assert.pixel.mode: "exact" | "psnr"
  - exact: 解码回读逐位相等（无损路径）
  - psnr: 回读 PSNR ≥ assert.pixel.threshold_db 才 PASS
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

## 元数据值文本规范化（M2-T8 冻结，docs/m2-tasks.md §4 T8 ①）

`pp_verify` 读取元数据后按类型渲染为文本，规则逐字节稳定（跨运行/跨机器/跨容器）：

| 类型 | 规则 | 样例（原始 → 规范化） |
|---|---|---|
| unsignedRational / signedRational | 约分到最简 `num/den`；`den == 1` → 整数形式；多元素以 `, ` 连接 | `28/10` → `14/5`；`4944/100` → `1236/25`；`31/1` → `31` |
| asciiString | 去尾部 NUL 与首尾空白（字节原样保留，UTF-8 不转码） | `"  padded\0\0"` → `padded`；`N` → `N` |
| 数组（count > 1） | 逐元素渲染后以 `, ` 连接 | `16 16 16` → `16, 16, 16`；`2.3.0.0`(GPSVersionID) → `2, 3, 0, 0` |
| undefined（二进制） | `0x` + 小写十六进制；超过 64 个十六进制字符截断并加 `...` | `48 50 51 50` → `0x30323332` |
| 其余（Short/Long/Signed/Date 等，标量） | 沿用 Exiv2 既有文本（`print()`） | `top, left`；`300`；`2024:01:01 00:00:00` |
| XMP | 同一渲染器（xmpText 视作文本；langAlt/xmpBag/xmpSeq 按元素） | `lang="x-default" …` |

退化守卫（规则未定义、语料无实例）：`den == 0` 原样输出 `num/0`，不做约分。
`--selftest` 覆盖上表每一类（含 40 字节 undefined 的截断样例）。

示例:
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
