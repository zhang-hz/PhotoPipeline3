# 编码器审计修复状态（2026-10-25 快速修复批）

> 基线：0.3.5.alpha2 + 工作树既有 0.3.6-alpha1 未提交改动。
> 判定口径：按第二轮裁定「是 bug 就修，不是 bug 不改」；Alpha 不支持、TIFF 多页不支持但缩略图支持。
> 本轮**未提交**（工作树改动 + 新文档）；`src/` 的改动叠加在既有未提交改动之上。

## 1. 门禁结果

| 门禁 | 命令 | 结果 |
|---|---|---|
| 构建 | `python tools/env.py run -- cmake --build build/release-dev` | 通过 |
| 单测 | `python tools/env.py run -- ctest --test-dir build/release-dev` | **36/36 通过** |
| 金样 | `python tools/env.py run -- python tests/golden/smoke.py build/release-dev` | **20/20 通过** |
| 格式 | `bash tools/ci-format.sh`（Git Bash） | **0 violation** |
| 回归 | `python tools/env.py run -- python tools/regression.py` | **有 diff，未重播种**（见 §5） |

证据日志：`build/audit-fix-ctest-final9.log`、`build/audit-fix-smoke-final8.log`、`build/audit-fix-format.log`、`build/audit-fix-regression-final3.log`。

## 2. 已修复（按面）

### Alpha：明确不支持 → 拍平

- `format_tables.cpp`：8 个格式 `supports_alpha=false`；删除 WebP 的 `alpha_compression/alpha_filtering/alpha_quality`；libheif 内省过滤 `alpha-quality`。
- `pipeline.cpp`：`stage_color` 在**色彩变换之前**统一拍平；删除逐 target flatten / supports_alpha 排序；每个输出行写 `AlphaFlattened`。
- `simd/kernels.cpp` + `simd.h`：flatten 公式改为预乘空间 `dst = src + bg*(1-a)`；ref/AVX2 同步。
- `decode/oiio_reader.cpp`：JXL 读 basic info；`alpha_premultiplied=false` 时在 decode 边界补乘 alpha；读不到 basic info 明确失败。
- `page_run.cpp`：warning 行（`pp-run-warn-*`）显示 `AlphaFlattened` 等非失败告警；失败原因行保持 `└ 原因：` 原口径。

验证：
- 256×2 RGBA ramp（RGB 200/100/50，alpha 0..255）→ BMP 24bpp，alpha=128 像素 BGR=`152,177,227`（RGB=`227,177,152`），命中白底合成 `200*0.5+255*0.5=228`（±1）。
- 同输入 → PNG 8bit：`colortype=2`（无 alpha），`AlphaFlattened` 告警。

### TIFF：多页不支持，缩略图支持

- `metadata.cpp`：
  - `sanitize_transcode_metadata()`：删 `Exif.Image2/Image3`、`Exif.SubImage*`、IFD0 源结构标签；`Exif.Thumbnail` 仅在 reduced-resolution/尺寸更小时保留整组。
  - `probe_tiff_structure()`：校验 IFD 链、目录数、`SubIFDs`、strip/tile/JPEG 偏移；`exiv2_apply_atomic` 在 **commit 前** 检查，失败不覆盖有效编码产物。
  - `rewrite_metadata_only`：要求目录数不变、偏移在界内；仅元数据仍保留多页。
- `pipeline.cpp::stage_meta`：转码路径调用净化；仅元数据不调用。
- 验证：
  - `multi3.tif`（3 页 32/48/64）→ TIFF：**1 IFD**，`ok`，无 `MetadataDropped`。
  - `tests/golden/base/multi.tif`（主图 64×64 + 32×32 缩略图）→ TIFF：**2 IFD**，缩略图保留。
  - 3 页 metadata-only 保留 3 IFD（单测/探针路径）。

### JPEG

- `enc_jpegli.cpp`：色度采样因子移到 `jpegli_set_defaults` 之后、`quality/distance` 之前；444/420 的 DQT 现已不同（实测 444 `4c8261a8/59cd490e/07a03af8`，420 `77fb2860/3c7adf54/43ca24de`）。
- `format_tables.cpp`：`psnr_target>0` 时隐藏 quality_mode/distance/quality；`psnr_target` 本身因实测系统性达不到目标而整体隐藏（CLI/预设仍可设）；`cicp_transfer_function` 改名/文案改为“仅量化表，不是色彩标记”。
- `pipeline.cpp::plan_targets`：JPEG + lossless 标志 → `LossyFromLossless` warning（不再静默）。

### JPEG XL

- `format_tables.cpp`：`resampling` 无损时隐藏 + `lock_one_when_lossless`；`color_transform` 增加 `auto` 默认（不再默认 YCoCg 2.8×）；`codestream_level` 默认 auto；`keep_invisible` 默认 false；`brotli_effort` 隐藏；MA 树比例上限 100。
- `enc_jxl.cpp`：三个 Modular 参数改 `SetFloatOption`；无损分支兜底 `RESAMPLING=1`；`color_transform=auto` 不设置；VarDCT `distance<=0` 明确拒绝。
- `params.cpp`：`lossless && resampling>1` 交叉规则。

### WebP

- `format_tables.cpp`：删除 `preset` 与 alpha 专用参数。
- `enc_webp.cpp`：技术选择与 `lossless` 单一真源（tech=lossy + flag 明确失败；tech=lossless 即无损）；`near_lossless<100` 在无损下强制 100 并日志；删除 `WebPConfigPreset`。
- `pipeline.cpp`：tech=lossless 时把 `lossless/__lossless` 写回参数集，侧车不再与产物相反。

### PNG / BMP

- `enc_oiio.cpp`：
  - 有 ICC 时设 `oiio:ColorSpace="ICC"`，不再同时写 iCCP + sRGB/gAMA/cHRM（实测 P3 产物 chunks 仅 `IHDR iCCP eXIf oFFs IDAT IEND`）。
  - 灰度输出遇到 RGB ICC（或反向）丢弃并记日志，不再写不匹配 profile。
  - 新增 `compression`（zlib 策略）与 `filter`（libpng 行滤波）参数并接线。
- `pipeline.cpp`：BMP 深度告警改用 `bitdepth_bps`（16-bit 源 → BMP 24bpp 现在报 `DepthDowngrade`）；BMP keep + 任意 ICC 都报 `EmbedIccUnsupported`。
- `page_output.cpp`：BMP 位深下拉改为只读「自动（按通道）」——灰度 8bpp / 彩色 24bpp 由编码器按通道决定，不再提供 8/24 假选择。
- `paramform.cpp`：TIFF/PNG/BMP 不再显示恒无效的“无损编码”复选框；AVIF + svt-av1 时无损复选框隐藏（切到 libaom 才出现）。

### HEIF / AVIF

- `params.h` + `enc_heif.cpp`：`ParamDef::has_default` 加性字段；libheif `has_default=false` 的可选参数（SVT `qp`）不再被填成范围下限。默认 AVIF 不再下发 `qp=0`（实测默认 1975 B，而非 38214 B）。
- `enc_heif.cpp`：HEIF 8 位静止图的 `Main` 与 `MainStill` 视为同一可用族（不再 100% 失败）；SVT + 无损给出明确 `choose libaom` 错误；speed>9 在 all-intra 下钳到 9 并记日志；无损时 quality 行隐藏；`alpha-quality` 不再暴露。
- `page_output_impl.h`：AVIF Main 允许 8/10 位（不再静默 10→8）。
- `pipeline.cpp`：HEIF 默认位深改 8（Main Still / `heic`），AVIF 保持 10。

### 跨面

- `page_run.cpp`：warning 行（降档/丢元数据/拍平/丢 ICC）在运行页可见。
- `dev_cli.cpp`：输入收集排除 `out_root` 子树；`mainwindow_run.cpp` 同步过滤 GUI 勾选集合（`is_inside(src,out_root)`），两入口一致。
- `presets.h` / `preset_io.cpp` / `page_output.cpp`：`PresetData` 加性追加 `color_intent/color_dither8/gamut_mapping/linear_resample/metadata_only`；JSON 读写 + collect/apply 全通，save→load 可复现产物；`validate_preset` 校验范围。HEIF/AVIF 档位（`__heif_profile`）改用输出级 `heif_profile` 字段持久化，不再丢回自动。
- `paramform.cpp`：AVIF + svt-av1 的无损复选框隐藏（避免“可点但必失败”）。
- `enc_heif.cpp`：`threads` 参数从 UI 隐藏（AVIF 由 E3 强制写、x265 无该参数）；运行日志仍记录实际 E 分配，不再给“可调但被覆盖/不存在”的旋钮。
- `scheduler.cpp` / `pipeline.cpp`：仅元数据模式跳过 worker 层 OIIO probe；`run_metadata_only` 内 probe 失败只降级为“无尺寸信息”，CMYK TIFF 等纯元数据源可处理。实测 CMYK TIFF → metadata-only：`ok`，`codestream_same=true`（strip 载荷 SHA 不变）。
- `dev_cli.cpp` + `params.h`：`--param` 对运行时内省后端回退到 live `introspect_backends()` 类型表；AVIF/HEIF `quality=40/70/90` 实测产物 2303/4894/10264 B、2409/6987/18925 B，不再固定回退 90。
- `mainwindow_run.cpp`：仅元数据不再静默把冲突策略改成 Skip，UI 选什么就执行什么。
- `metadata.cpp`：隐私剥除时对 IPTC(APP13) 与 COM 执行 set-empty（`setIptcData({})` / `setComment("")`），不再只清 EXIF+XMP；`test_metadata` 新增 IPTC/COM 清除断言。
- `metadata.cpp` / `enc_oiio.cpp`：转码计划在缺少 `Exif.Image.DateTime` 时从 `Exif.Photo.DateTimeOriginal` 补一份，编码器同时写 OIIO `DateTime`；实测 `exif_full.jpg → TIFF` 最终 tag 306 = `2024:03:01 10:00:00`，不再写编码时刻。

## 3. 测试/金样改动（语义变更，非放宽）

- `tests/unit/test_simd.cpp`：flatten 真值改成预乘公式（`src + bg*(1-a)`）。
- `tests/unit/test_pipeline_contract.cpp`：alpha 矩阵改为“所有输出拍平 + 3 通道 + 每行 AlphaFlattened”；RGBA→sRGB 改为白底合成断言。
- `tests/unit/test_params.cpp`：无损 JXL 的 `resampling` 期望 1（lock）。
- `tests/unit/test_presets.cpp`：`diff_preset` 增加五个新全局字段 + `__heif_profile`，`make_delta_preset` 取非默认值，JSON 键/值逐项断言（save→load 零丢失）。
- `tests/unit/test_metadata.cpp`：新增 IPTC（`Iptc.Application2.Caption`）与 COM 写入 → 隐私剥除 → 回读为空的断言；另加 WebP 隐私剥除不因 Exiv2 不支持 IPTC/COM 而整体失败的断言。
- 金样 `webp-lossless`、`multiformat-split` 输入从 `rgba8.png` 改为 `rgb8.png`（alpha 已不支持）；三个 multiformat 的 JPEG 腿新增 `LossyFromLossless` 断言。
- 未改动任何夹具文件；未更新 `regression.py` 基线。

## 4. 剩余限制（非本轮可修代码 bug，如实）

以下不是“还能在本仓改一行”的代码 bug，而是后端能力 / 上游库 / 产品策略 / 格式语义边界；对应的 UI 假承诺已尽量收口，仍可复现的边界在此明示：

1. **HEIF/AVIF 实际并发仍不能完全受 E3 约束**：UI 的 `threads` 已隐藏（AVIF 由 E3 强制写，x265 无该参数），但 x265 默认线程池无法从 libheif 参数面收紧；属后端能力边界，需日志/文档披露。
2. **JPEG `psnr_target` 本身达不到目标**：UI 已隐藏整个控件（不再给用户“填 40 得 35”的陷阱），库的距离搜索口径未改；CLI/预设仍可设置。
3. **TIFF `deflate_level` 在 `predictor=none` 时无效**（OIIO/lib 侧；未加上游 issue）。
4. **PNG 8 位源默认 16 位**（“禁止降级”策略；未改默认）。
5. **HEIF/AVIF 无损是 YCbCr 平面无损，不是 RGB 逐位无损**：无损复选框 tooltip 已明确写出“YCbCr 平面无损、RGB↔YCbCr 有舍入（不是逐位）”，属格式语义边界，不再当作可修 bug。
6. 其余 P2/P3：PNG ancillary 元数据、WebP partitions/preprocessing 文档、AVIF grid 兼容性、JXL level5+16bit 边界等。

## 5. 回归基线

`tools/regression.py` 仍有 diff，归因明确且**预期**：

- `AlphaFlattened` 出现在 `graya8.png`、`anim.gif` 等带 alpha 输入的行；
- 带 alpha 的 WebP/HEIF/AVIF 输出字节变小/像素变亮（拍平后的正确值）；
- JPEG 行新增 `LossyFromLossless`（`--lossless` 全局开关打到 JPEG 腿）。

按 AGENTS §5.3/§6.4，重播种基线需要先复核归因并登记；本轮**没有**执行 `--update`，避免把语义变更静默写进基线。建议在提交正文写明上述归因后再重播种。

## 6. 复现命令

```powershell
python tools/env.py run -- cmake --build build/release-dev --parallel
python tools/env.py run -- ctest --test-dir build/release-dev
python tools/env.py run -- python tests/golden/smoke.py build/release-dev
& 'C:\Program Files\Git\bin\bash.exe' tools/ci-format.sh
python tools/env.py run -- python tools/regression.py
```

关键手测：

```powershell
# alpha：RGBA ramp → BMP，alpha=128 应为白底合成 (227,177,152)（BGR 存 152,177,227）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/audit-fix-out/alpha_ramp.png `
  --out build/audit-fix-out/alpha-bmp2 --format bmp --bitdepth 24 --workers 1

# TIFF 多页（期望 1 IFD）与缩略图（期望 2 IFD）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/tiff/inputs/multi3.tif `
  --out build/audit-fix-out/tiffmulti6 --format tiff --workers 1
python tools/env.py run -- build/release-dev/photopipeline.exe --dev tests/golden/base/multi.tif `
  --out build/audit-fix-out/tiffthumb --format tiff --workers 1

# JPEG chroma DQT 必须不同
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/avif-rt/in/grad_rgb8.png `
  --out build/audit-fix-out/jpeg-444 --format jpeg --workers 1 --param chroma=444
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/avif-rt/in/grad_rgb8.png `
  --out build/audit-fix-out/jpeg-420 --format jpeg --workers 1 --param chroma=420
python build/encoder-audit/jparse.py build/audit-fix-out/jpeg-444/*.jpg build/audit-fix-out/jpeg-420/*.jpg
```
