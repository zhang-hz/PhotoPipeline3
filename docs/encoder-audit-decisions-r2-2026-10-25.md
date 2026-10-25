# 审计第二轮裁定：Alpha 不支持、TIFF 缩略图支持、Bug 二分

> 用户裁定（2026-10-25）：
> 1. **Alpha 明确不支持**：不保留、不输出 alpha；给出"不支持"的正确行为。
> 2. **TIFF 多页/动画不支持**：只取主图；但**缩略图/预览图应支持**。
> 3. **判断标准改为"是 bug / 不是 bug"二分**：是 bug 就修，不是 bug 不改；不再按"价值高低"把真 bug 往后排。
>
> 本文取代 `docs/encoder-audit-triage-2026-10-25.md` 的 §3.1、§6、§7 以及"价值分诊"口径；全量事实仍以 `docs/encoder-config-audit-2026-10-25.md` 为准。

---

## 1. Alpha：明确不支持

### 1.1 正确行为（一句话）

**不输出 alpha；任何带 alpha 的输入在解码后、色彩管理前拍平到背景，并给出可见警告。** 不尝试保留 alpha，也不为 alpha 做直通/预乘的端到端语义。

这意味着：

- 8 个输出格式的 `FormatDef.supports_alpha` 全部改为 `false`；产物永远只有 1 或 3 通道。
- 删除/隐藏所有 alpha 专用参数：WebP 的 `alpha_compression` / `alpha_filtering` / `alpha_quality`，AVIF 的 alpha quality，JXL 的 alpha 相关项，TIFF/PNG 的 alpha 声明路径。
- 输入带 alpha 时：`AlphaFlattened` 警告 + 侧车记录 `alpha_flattened=true` + 背景值；不再有"保留 alpha"的承诺。
- 如果用户要严格拒绝而不是拍平，另加设置项 `alpha_policy = flatten | reject`，默认 `flatten`；本文按默认 `flatten` 设计。

为什么选"拍平"而不是"拒绝"：这是批量转码器，PNG/WebP 带 alpha 的输入很常见；拍平是确定性的、可解释的降级，配合警告不会静默。拒绝会让大量普通输入直接失败，除非用户明确要求严格模式。

### 1.2 当前真正的 bug：flatten 把关联缓冲当直通

alpha 不支持，并不等于 alpha 路径没有 bug。当前唯一的 alpha 真 bug 是：

- OIIO 解码默认返回 **associated（预乘）** RGB（PNG/WebP/TIFF/HEIF 都是；JXL 例外，见下）。
- `flatten` 公式是 `c*a + bg*(1-a)`，这是**直通**公式；对已预乘的 `c` 又乘了一次 alpha → 半透明区偏暗。
- 实测：RGB `(200,100,50)`、alpha=128、白底，BMP 输出 `(177,152,140)`，正确应为 `(228,178,153)`。

修复不是"保留 alpha"，而是让 flatten 对预乘缓冲正确：

```
对 associated 缓冲：dst[c] = src[c] + bg * (1 - a)
```

这是合成公式在预乘空间的正确形式，不需要反预乘，也不需要每格式 alpha 声明。

### 1.3 解码/管线的统一契约

**契约**：进入 `flatten` 的缓冲一律是 **associated（预乘）**；flatten 之后一律是不带 alpha 的 RGB/灰度。

- PNG / WebP / TIFF（默认）/ HEIF / AVIF：OIIO 默认已经返回 associated，无需额外处理。
- **JXL 是例外**：OIIO 的 JXL reader 不处理 `alpha_premultiplied`，返回的是码流里存的值。
  - 若 `alpha_premultiplied=false`（常见），缓冲是直通 → flatten 前先做一次 `RGB *= alpha`；
  - 若 `alpha_premultiplied=true`，缓冲已经是预乘 → 不再乘。
  - 用 libjxl 的 `JxlDecoderGetBasicInfo` 读这个 flag；读不到就按"直通"处理并告警，或直接失败，不能猜。
- 其他带 alpha 的输入格式：若无法判定关联性，**拒绝该文件**并给明确错误；不要默认假设。

**管线顺序**（关键）：

```
decode → orient → flatten（若有 alpha） → color → encode
```

- 必须 **在 color 之前** flatten：lcms2 的色彩变换是非线性的，对预乘值做 `T(C*a)` 不等于 `T(C)*a`；先拍平成不透明 RGB，色彩变换才正确。
- 因此删除当前"逐 target、在 color 之后 flatten"的逻辑；`supports_alpha` 的排序/分支也可以删掉。
- 背景值 `flatten_gray` 是**源空间**的灰度（0..1），会随图像一起做色彩变换；UI 文案要写"源空间拍平底色"，避免用户以为是目标空间。

### 1.4 代码改动清单

| 位置 | 改动 |
|---|---|
| `core/format_tables.cpp` | 8 个格式 `supports_alpha=false`；删除 alpha 专用 ParamDef（WebP 3 项、AVIF alpha quality 等） |
| `core/pipeline.cpp` | 在 orient 之后、color 之前统一 flatten；删除逐 target flatten 与 `supports_alpha` 排序；`AlphaFlattened` 警告写到每个输出行/侧车 |
| `core/simd/kernels.cpp` + `simd.h` | `flatten_ref/avx2` 公式改为 `src + bg*(1-a)`；更新契约注释与单测期望 |
| `decode/oiio_reader.cpp` | 增加 JXL alpha 关联检测；必要时在 decode 后做 `RGB *= a`（或标记给 flatten 层）；其他格式无法判定时失败 |
| `codecs/enc_webp.cpp` / `enc_jxl.cpp` / `enc_heif.cpp` / `enc_oiio.cpp` | 增加入口断言：`nch==2/4` 视为 pipeline bug，直接失败；删除 alpha 分支/参数读取（或保留为不可达防御） |
| `ui/paramform.cpp` / `page_output.cpp` | 隐藏 alpha 专用参数；`flatten_gray` 文案改"源空间拍平底色"；`AlphaFlattened` 在运行页可见 |
| `docs/*` | 口径改为"不支持 alpha；带 alpha 输入拍平"；不再宣称 alpha 保留 |

### 1.5 测试

- RGBA8 ramp（RGB 恒 200/100/50，alpha 0..255）→ JPEG/BMP/PNG/WebP/JXL/TIFF/HEIF/AVIF：
  - 所有产物 `nchannels` 为 1 或 3；
  - alpha=128、白底 → `(228,178,153)`（允许 ±1 量化）；
  - alpha=0 → 纯白（或配置的背景色）；
  - PNG 无 `tRNS`/alpha 通道；WebP 无 `ALPH`；TIFF `SamplesPerPixel` 为 1/3 且无 `ExtraSamples`；JXL `num_extra_channels=0`；HEIF 无 alpha aux。
- JXL 直通 alpha 源：先 `RGB*=a` 再 flatten，结果与 PNG 源一致；JXL `alpha_premultiplied=true` 源不重复乘。
- 现有 `alpha-jpeg`/`multiformat-split` 的 JPEG 腿补像素断言（合成值），不要再用 `pixel: {}`。
- `pp_verify`：比较前把带 alpha 的参照按同一背景拍平（新增 `--flatten-alpha <bg>` 或 expected 里声明背景）；否则通道数/像素都不匹配。

### 1.6 哪些 alpha findings 直接关闭

- `PNG-01`、`X-COMPLIANCE-01..04`、`X-COMPLIANCE-10`、`X-COMPLIANCE-15`：不再保留/输出 alpha，这些"保留 alpha 时的语义错误"不再是产品问题；只保留"flatten 必须正确"这一条。
- `WEBP-01` 中"有损产物被标 lossless"仍要修（见 §3），但它不再是 alpha 问题。
- `TIFF-03` 已撤回；TIFF 不再输出 alpha，`ExtraSamples` 问题消失。
- `supports_alpha` 相关 UI/文档全部收口；`AlphaFlattened` 是唯一用户可见的 alpha 行为。

---

## 2. TIFF：多页/动画不支持，缩略图/预览支持

### 2.1 正确行为

- **多页/动画 TIFF（额外全分辨率页）不支持**：只解主图（IFD0），发 `MultipageTruncated` 警告；**绝不把额外页写进产物**。
- **缩略图/预览图（reduced-resolution IFD1）支持**：如果源 TIFF 的 IFD1 是真正的缩略图/预览（`NewSubfileType` bit0=1 或尺寸小于主图），在 TIFF 输出中保留为 IFD1；其他容器（JPEG/PNG/WebP）能表达 `Exif.Thumbnail` 的也保留，不能表达的丢弃并告警。
- 不把"多页"误判为"缩略图"，也不把"缩略图"误删。

### 2.2 Exiv2 的真实分组（根因的关键）

Exiv2 不是把所有额外 IFD 都叫 `SubImage`：

| 源 IFD | Exiv2 组 | 含义 |
|---|---|---|
| IFD0 | `Exif.Image.*` | 主图 |
| IFD1（`next`） | **`Exif.Thumbnail.*`** | 缩略图/第二目录；Exiv2 用 `newTiffThumbData` 处理 |
| IFD2（`next`） | **`Exif.Image2.*`** | 第二页 |
| IFD3（`next`） | **`Exif.Image3.*`** | 第三页 |
| `SubIFDs`（0x014A） | `Exif.SubImage1..9.*` | 子图/预览目录 |

当前 bug 的机制：

- `build_plan()` 全量复制源 `ExifData`（含 `Exif.Thumbnail`、`Exif.Image2`、`Exif.Image3`）。
- `setExifData()` + `writeMetadata()` 把这些组重新展开成额外 IFD：
  - IFD1 被当 Thumbnail 写出（即使它其实是全分辨率第 2 页）；
  - IFD2/IFD3 被当 Image2/Image3 写出；
  - 第 3 页的数据没被搬，`StripOffsets` 数组偏移写成 0，读取器从 TIFF 头读出 `[2771273, 8]`，越界。
- 所以修复必须**按组分类**：保留真正的 Thumbnail，删除页组 `Image2/Image3` 与 `SubImage*`。

### 2.3 分类与净化

在转码路径（`build_plan` 之后、`make_payloads` 之前）做一次：

```cpp
void sanitize_transcode_metadata(MetadataPlan &plan, bool keep_thumbnail);
```

规则：

1. 计算主图尺寸 `Exif.Image.ImageWidth/ImageLength` 与 `Exif.Thumbnail.ImageWidth/ImageLength` / `NewSubfileType`。
2. `keep_thumbnail = thumb_is_reduced_resolution()`：
   - `Exif.Thumbnail.NewSubfileType & 1`（reduced-resolution）**或**缩略图宽/高小于主图；
   - 且 `Exif.Thumbnail` 有可搬运的数据（`JPEGInterchangeFormat`+`Length` 或 `StripOffsets`+`ByteCounts`）。
3. 若 `keep_thumbnail=false`：删除整个 `Exif.Thumbnail.*`（它是全分辨率页，不是缩略图）。
4. 若 `keep_thumbnail=true`：**保留 `Exif.Thumbnail.*` 整组**，包括它的 `StripOffsets`/`JPEGInterchangeFormat` 等结构标签（否则 Exiv2 无法写缩略图数据）。
5. 无条件删除：
   - `Exif.Image2.*`、`Exif.Image3.*`（页）；
   - `Exif.SubImage1..9.*`（SubIFD 页/预览；本版不保留 SubIFD 预览）；
   - `Exif.Image` 组里的源结构标签：`ImageWidth/ImageLength/BitsPerSample/Compression/PhotometricInterpretation/StripOffsets/RowsPerStrip/StripByteCounts/PlanarConfiguration/Predictor/TileWidth/TileLength/TileOffsets/TileByteCounts/SamplesPerPixel/ExtraSamples/SampleFormat/NewSubfileType/SubfileType/FillOrder/JPEGInterchangeFormat/JPEGInterchangeFormatLength`。
6. 保留 `Exif.Image.Orientation=1`（旋转后统一置 1）、`Exif.Photo.*`、`Exif.GPSInfo.*`、XMP、ICC（ICC 由编码器独立写；如担心冲突，删除 `Exif.Image.InterColorProfile`）。

> 关键：不要把 `Exif.Thumbnail` 的 `StripOffsets`/`JPEGInterchangeFormat` 当"源结构标签"删掉——那是缩略图数据指针，删了缩略图就没了。

### 2.4 输出断言

在 `exiv2_apply(tmp)` 之后、`commit_temp_file` 之前做 TIFF 结构探针：

- 合法形态只有两种：
  1. **1 个 IFD**：主图，无缩略图；
  2. **2 个 IFD**：IFD0 主图 + IFD1 合法缩略图（reduced-resolution、尺寸小于主图、数据/偏移在界内）。
- 拒绝：`>2` 个 IFD、IFD1 为全分辨率、`SubIFDs` 非空、strip/tile/JPEG 数据偏移越界。
- 多页源只取首页：输出 1 IFD（若源还有合法缩略图，可 2 IFD）。
- metadata-only：保留原 IFD 数量与载荷，断言"目录数不变 + 偏移在界内"；不调用净化。

探针要能处理 BigTIFF（64 位偏移）；不能解析就失败关闭，不要放行。

### 2.5 测试

- `multi3.tif`（3 页，32/48/64 递增）：IFD1/IFD2 都是全分辨率页 → 输出 **1 IFD**；第 3 页不再越界。
- 2 页全分辨率 TIFF → **1 IFD**（回归当前 P0）。
- 真正的缩略图 TIFF（主图 4000×3000，IFD1 160×120，`NewSubfileType=1`）→ **2 IFD**；IFD1 可解码、尺寸正确、偏移在界内。
- JPEG（带 EXIF 缩略图）→ TIFF：若 Exiv2 能搬运，2 IFD；不能搬运则丢弃缩略图并告警（单独测试记录行为）。
- 3 页 TIFF → metadata-only：**3 IFD**，三页可解码，strip/tile 载荷逐字节不变。
- `test_tiff_probe`：1/2/3 IFD、全分辨率 IFD1、合法缩略图、越界、环、SubIFDs、BigTIFF。

### 2.6 哪些 TIFF findings 关闭/保留

- `TIFF-01`：保留为真 bug（页泄漏/越界），修法改为"页组删除 + 缩略图保留 + 探针"。
- `TIFF-06`（无损复选框空转）：是真 UI bug；隐藏/勾选+禁用，顺手修。
- `TIFF-08/09/10`：文案/文档，不算 bug；改文档即可。
- `TIFF-03`：已撤回，且 alpha 不再输出，永久关闭。
- "多页保留"不是 bug，是明确不支持；不要再为它设计多帧管线。

---

## 3. Bug / 不是 bug 二分（替代价值分诊）

### 3.1 判定规则

**是 bug（修）**：代码行为与声明/UI/格式承诺矛盾，或静默产出错误/不可解码结果。典型：

- 参数存在但完全无效（死控件）；
- 同一输入两种结果/同一 UI 值两种语义；
- 无损标志为真但产物有损；
- 产物结构越界/不可解码；
- 声明的默认值/范围与库行为不符；
- 错误被吞成 `ok=true`；
- 必失败/必无效的枚举项仍暴露；
- UI 显示"已生效"但编码器不读。

**不是 bug（不改代码）**：产品策略、上游库约束、纯文案：

- PNG 默认 16 位（"禁止降级"策略）；
- `partitions`/`preprocessing` 在 libwebp 默认参数下的约束；
- `biSizeImage` 的上游写法；
- 空 `eXIf`/`oFFs`；
- 纯标签/行号/文档漂移；
- "多页保留"（明确不支持）。

**边界**：如果"不是 bug"但 UI 在说谎（如 PNG/TIFF/BMP 的"无损编码"复选框恒无效），按 **UI bug** 修：隐藏/勾选+禁用，不改编码语义。

### 3.2 真 bug 清单（全修，不再按价值排序）

**alpha / flatten**
- flatten 公式对 associated 缓冲二次乘 alpha；改为 `src + bg*(1-a)`。
- flatten 位置在 color 之后；移到 color 之前。
- JXL 直通 alpha 未预乘；检测 `alpha_premultiplied` 后补乘。
- alpha 专用参数/能力未删除，UI 仍在暗示支持；`supports_alpha=false` + 隐藏参数。
- `AlphaFlattened` 警告无 UI 出口。

**TIFF**
- 多页页组 `Exif.Image2/Image3` / `Exif.SubImage*` 被重放进产物；删除 + 探针。
- 全分辨率 IFD1 被当缩略图保留；按 reduced-resolution 分类。
- 真正缩略图被误删；保留 `Exif.Thumbnail` 整组并验证。
- `ccittrle` 必失败；删/拦截。
- `predictor=float` 必失败；删/拦截。
- `deflate_level` 在 `predictor=none` 时无效且无提示；加锁/提示（或修 OIIO 顺序，若可）。
- `DateTime` 写编码时刻；改源生效时间/固定值。
- 无损复选框空转；UI 修正。

**JPEG**
- 色度采样在量化表之后设置；移到之前。
- `psnr_target` 静默覆盖三项且达不到目标；锁/提示/禁用。
- `cicp_transfer_function` 只改量化表、不写色彩标记；删除或改名。
- `lossless` 标志对 JPEG 无拦截；加 warning/拒绝。

**JXL**
- 无损 + `resampling>1` 静默降采样；锁 1 + 规则 + 编码器兜底。
- 三个 Modular 参数用错 setter；改 float。
- `color_transform` 三个值 no-op、默认 YCoCg 2.8×；加 auto/交还库默认。
- `codestream_level` 默认 10 收窄兼容；默认改 auto。
- `distance=0` 两种结局；VarDCT 禁用 0 或明确路由。
- `keep_invisible` 默认 true 导致 3.1×；改 auto/false。
- `brotli_effort` 死参数；隐藏/删除。

**WebP**
- `tech=lossy + lossless=true` 产出有损却全链记无损；编码器/校验层拒绝。
- `tech=lossless` 未勾无损却产出无损、记录 false；技术/复选框单向同步。
- `preset` 六档同字节；删行或改成默认值来源。
- `near_lossless<100` 生效后非逐位无损仍报 true；锁 100/warning。
- 跨技术参数被接受但永不读取；校验/清理。
- `partitions`/`preprocessing` 的条件不生效：不是 bug，tooltip 说明即可。

**PNG**
- iCCP 与 sRGB/gAMA/cHRM 同写；非 sRGB ICC 只写 ICC。
- 灰度 PNG 携带 RGB ICC；按色彩类型匹配。
- `compression`/`png:filter` 未暴露（"全参数暴露"缺口）；补 schema。
- 8 位源默认 16 位：不是 bug（策略）；加 UI 提示。
- 空 `eXIf`/`oFFs`：不是 bug；不改。
- `pHYs/tIME/tEXt` 丢失：产品元数据范围外；文档说明，不算 bug。

**BMP**
- 位深 8/24 不驱动 bpp；改只读/按通道自动。
- 16bit 源降 8bit 无告警；`bitdepth_bps` 比较 + 警告。
- keep + 非 sRGB ICC 丢 ICC 无告警；转 sRGB 或警告。
- "无损"复选框空转；UI 修正。
- `biSizeImage`：上游，不改。

**HEIF/AVIF**
- HEIF Main 死档位；删/修判据。
- HEIF 默认 10 位 → RExt；默认 8 或明确提示。
- HEIF 无损下 quality 仍可调但无效；锁/置灰。
- HEIF `threads` 不存在、E3 未约束；修调度或说明。
- AVIF SVT 无损必失败；自动切 libaom/禁用。
- AVIF `qp=0` 默认；内省三态 + GUI 只下发有默认/改过的键。
- AVIF `threads` 被覆盖；修 E3 映射。
- AVIF `alpha-quality` 钉死 50；默认/继承修正。
- AVIF `chroma` 用 int 读 string；修类型。
- AVIF Main 10→8；修 `profile_allowed_bitdepths`。
- AVIF speed 10–13 死档；映射/提示。
- AVIF `tune=iq`+无损必失败；交叉规则。
- AVIF grid >16MP：明确不支持或文档，不静默。
- `--dev --param` 类型：dev 取证 bug；修。
- `svt:` 透传：白名单删掉。

**跨面**
- 预设不保存色彩全局项/档位；加字段。
- `out_root` 在输入树内二次施加；收集排除。
- 仅元数据冲突策略 GUI/CLI 不一致；修。
- 隐私剥除只清 EXIF+XMP；修或明确文案。
- warning UI 零消费；修（系统性）。
- 未知键/锁定/默认值在 GUI 与 CLI 不同源；修。
- 计划/侧车统计（`out_bytes` 不含后写元数据）；修口径。
- 仅元数据仍强制 OIIO probe；修/文档。

### 3.3 不是 bug（关闭，不改代码）

- PNG 默认 16 位（策略；加提示）。
- `partitions` / `preprocessing` 在 libwebp 默认参数下的行为（库约束；文档）。
- BMP `biSizeImage`（上游 OIIO；报 issue）。
- 空 `eXIf` / `oFFs`（无害；不改）。
- PNG `pHYs/tIME/tEXt` 丢失（元数据范围外；文档）。
- 多页/动画 TIFF 保留（明确不支持）。
- TIFF/PNG/BMP 的"无损"语义本身（格式恒无损；只修 UI 控件）。
- P3 的标签/行号/文档漂移（改文档，不改代码）。
- `WEBP-05 near_lossless 默认 100`（默认=关，不是 bug）。

---

## 4. 建议执行顺序（按依赖，不按"价值"）

1. **alpha flatten 修正**：公式 + 位置 + JXL 预乘 + `supports_alpha=false` + 测试。
2. **TIFF 页/缩略图**：分类净化 + 探针 + 2/3 页/缩略图测试。
3. **warning UI**：alpha/TIFF/降档/丢元数据都依赖它。
4. **参数必失败/死控件批量修**：TIFF `ccittrle`/`float`；JXL setter/resampling；WebP preset/tech；HEIF Main；AVIF qp/SVT 无损；BMP 位深；PNG ICC。
5. **默认值/联动批量修**：HEIF 默认位深、JXL level/keep_invisible、AVIF alpha quality/Main 位深、JPEG chroma/psnr、TIFF DateTime。
6. **跨面一致性**：预设、`out_root`、冲突策略、隐私、未知键、`--dev --param`。
7. **文档/UI 文案**：P3、口径、tooltip、死控件隐藏。

每批都按"实现 + 单测/探针 + 门禁"交付；不再为"值不值得"争论，只判断"是不是 bug"。
