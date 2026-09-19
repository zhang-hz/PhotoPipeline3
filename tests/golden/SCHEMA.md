# expected.json 断言 schema（M0 冻结 v1）

- case: string（用例名）
- input: string（相对 tests/golden 的输入路径）
- output_format: string（格式 id）
- assert.pixel.mode: "exact" | "psnr"
  - exact: 解码回读逐位相等（无损路径）
  - psnr: 回读 PSNR ≥ assert.pixel.threshold_db 才 PASS
- assert.pixel.threshold_db: number（psnr 时必填）
- assert.metadata: [{ key: string, op: "eq"|"exists"|"absent", value: string }]
  - key 为 Exiv2 完整标签名（Exif.* / Xmp.*）
- assert.warnings_contain: [string]（WarningKind 名，输出警告必须包含所列项）

示例:
{
  "case": "smoke-jxl-lossless",
  "input": "base/rgb8.png",
  "output_format": "jxl",
  "assert": {
    "pixel": { "mode": "exact" },
    "metadata": [],
    "warnings_contain": []
  }
}
