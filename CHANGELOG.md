# 变更日志

## [0.3.5.alpha3] — 2026-10-25

**预发布（alpha）**：全格式编码器配置审计后的修复批。审计覆盖 8 个输出格式的每个参数：
是否暴露、是否真正生效、默认值是否合理、联动是否缺失、是否存在静默降级/假桩/不生效；
并逐格式检查输出兼容性与「仅元数据」通路的字节保真。版本单源 = `CMakeLists.txt:19-24` =
**0.3.5.alpha3**（数字段 0.3.5 + 预发布标签 ".alpha3"）。全量审计报告见
[docs/encoder-config-audit-2026-10-25.md](docs/encoder-config-audit-2026-10-25.md)，修复
状态与门禁证据见 [docs/encoder-audit-fix-status-2026-10-25.md](docs/encoder-audit-fix-status-2026-10-25.md)。

### 变更（先读）

- **Alpha 明确不支持**：8 个输出格式 `supports_alpha=false`，不再产出任何带 alpha 的文件；
  带 alpha 的输入在**色彩管理之前**统一拍平到背景（预乘空间 `src + bg*(1-a)`，与
  「先乘 alpha 再合成」的旧公式不同），每个输出行给出 `AlphaFlattened` 警示。JXL 直通
  alpha 在解码边界按 `alpha_premultiplied` 补乘，读不到 basic info 时明确失败。
- **TIFF 多页/动画不支持**：只取第一页并给 `MultipageTruncated` 警示；转码路径在元数据写入前
  剥掉 `Exif.Image2/Image3`、`Exif.SubImage*` 与 IFD0 源结构标签。**真正的低分辨率缩略图
  IFD1（`Exif.Thumbnail`）保留**；写盘前用 `probe_tiff_structure` 校验 IFD 链/偏移，
  结构不合法不覆盖有效编码产物。仅元数据通路保持原 IFD 数与载荷。
- **HEIF 默认位深 8**（Main Still / `heic`），AVIF 保持 10；AVIF 默认不再下发 `qp=0`
  （默认产物 38,214 B → 1,975 B），`ParamDef::has_default` 区分「库有默认值」与
  「可选覆盖参数」，不再把范围下限当默认值。
- **预设补齐**：`color_intent` / `color_dither8` / `gamut_mapping` / `linear_resample` /
  `metadata_only` 五个全局字段与输出级 `heif_profile` 现在随预设保存/加载（此前丢回默认）。

### 修复

- **JPEG**：色度采样因子改到 `jpegli_set_defaults` 之后、quality/distance 之前设置——
  444/420 的量化表现在真正不同（此前两档 DQT 相同，`chroma` 形同虚设）；`psnr_target>0`
  时隐藏互斥的 quality 行；`psnr_target` 因库系统性达不到目标而隐藏控件（CLI/预设仍可设）；
  `cicp_transfer_function` 文案改为「仅量化表，不是色彩标记」；全局 lossless 打到 JPEG 腿时
  发 `LossyFromLossless` 警示（不再静默有损）。
- **JPEG XL**：无损锁定 `resampling=1`（含交叉规则）；三个 Modular 参数改用
  `SetFloatOption`（此前按整数下发，小数被截断）；`color_transform` 增加 `auto` 默认
  （不再默认 YCoCg，避免无损/近无损被隐式改色）；`codestream_level` 默认 auto；
  `keep_invisible` 默认 false；隐藏 `brotli_effort`；VarDCT `distance<=0` 明确拒绝。
- **WebP**：删除无效的 `preset` 与 alpha 专用参数；技术选择与 `lossless` 单一真源
  （`tech=lossy` + `--lossless` 明确报错，`tech=lossless` 即无损）；无损下
  `near_lossless<100` 强制 100 并记日志；侧车不再把 `lossless` 写成与产物相反的值。
- **PNG / BMP**：有 ICC 时只写 `iCCP`（不再同写 sRGB/gAMA/cHRM 造成自相矛盾）；灰度/RGB
  ICC 与输出通道不匹配时丢弃并记日志；新增 `compression`（zlib 策略）与 `filter`
  （libpng 行滤波）参数并接线。BMP 深度降档告警改用 `bitdepth_bps`（16-bit 源现在会报
  `DepthDowngrade`）；keep ICC 时告警；位深下拉改为只读「自动（按通道）」（不再给
  8/24 假选择）；TIFF/PNG/BMP 隐藏恒无效的「无损编码」复选框。
- **HEIF / AVIF**：`Main` 与 `MainStill` 视为同一可用族（8 位静止图不再 100% 失败）；
  SVT-AV1 + 无损给出明确「改用 libaom」错误（不再产出坏件）；all-intra 下 speed>9 钳到 9；
  无损时隐藏 quality；`alpha-quality` 不再暴露；`threads` 从 UI 隐藏（AVIF 由 E3 强制写、
  x265 无该参数）；AVIF `Main` 允许 8/10 位（不再静默 10→8）。
- **TIFF**：缺 `Exif.Image.DateTime` 时从 `Exif.Photo.DateTimeOriginal` 补写，编码器同时
  写 OIIO `DateTime`（此前写的是编码时刻）；实测 `exif_full.jpg → TIFF` tag 306 =
  `2024:03:01 10:00:00`。
- **仅元数据**：worker 层跳过 OIIO probe（CMYK TIFF 等纯元数据源可处理）；probe 失败只降级为
  「无尺寸信息」；隐私剥除同时清 IPTC(APP13) 与 COM（此前只清 EXIF+XMP）；GUI 不再把冲突
  策略静默改成 Skip。
- **入口一致性**：GUI 与 `--dev` 都排除 `out_root` 子树中的输入；`--dev --param` 对
  HEIF/AVIF 等运行时内省后端按真实类型下发（quality 40/70/90 产物已不同）。

### 新增

- 运行页独立告警行（`pp-run-warn-*`）：降档/丢元数据/alpha 拍平/丢 ICC 等非失败告警可见，
  失败原因行保持原口径。

### 测试

- `test_pipeline_contract` alpha 矩阵改为全格式「拍平 + 3 通道 + 每行 `AlphaFlattened`」；
  `test_simd` flatten 真值改为预乘公式；`test_params` 无损 JXL `resampling` 期望 1；
  `test_presets` 覆盖五个新全局字段 + `__heif_profile` 的 save→load 零丢失；
  `test_metadata` 新增 IPTC/COM 隐私剥除与 WebP 隐私重写断言。
- 金样 `webp-lossless`、`multiformat-split` 输入改用 `rgb8.png`（alpha 已不支持）；
  三个 multiformat 的 JPEG 腿新增 `LossyFromLossless` 断言；夹具零改动。

### 已知

- HEIF/AVIF 的「无损」是 YCbCr 平面无损，不是 RGB 逐位无损（tooltip 已写明）；
  x265 默认线程池无法从 libheif 参数面收紧；JPEG `psnr_target` 精度受库限制（控件已隐藏）；
  TIFF `deflate_level` 在 `predictor=none` 时受 OIIO/libtiff 上游限制；PNG 8 位源默认
  16 位属「禁止降级」策略。详见审计修复状态文档 §4。

## [0.3.5.alpha2] — 2026-10-25

**预发布（alpha）**：七项用户实测问题逐条根治——① 高德搜索失败；② Adobe RGB 不能正确
处理/显示；③ 色域「保持原样」后 ICC 看起来丢失；④ HEIF 勾选无损后的产物异常；
⑤ HEIF 选 10bit 后文件仍显示 Main；⑥ 无边框窗口右上角三钮点不动；⑦「仅元数据 + 改 GPS」
的图像逐字节保真缺少机械化测试。版本单源 = `CMakeLists.txt:19-24` = **0.3.5.alpha2**
（数字段 0.3.5 + 预发布标签 ".alpha2"）。

### 修复

- **高德搜索失败**：搜索源不再被瓦片源绑死——配置了高德 Key 时搜索/联想/逆地理一律走
  高德（此前默认 OSM 底图永远走 Nominatim，大陆不可达即「搜索失败」，高德 Key 形同虚设）；
  Key 录入自动 trim，并兼容整行粘贴的 `AMAP_KEY=xxxx` 与引号包裹形态；高德错误码追加
  可执行提示（Key 类型/数字签名/配额/权限），原始 `info` 保留可检索。
- **Adobe RGB 显示**：预览与缩略图不再把 Adobe RGB/P3 源按 sRGB 直出（发灰）——给
  `QImage` 附源 ICC 的 `QColorSpace`，由 Qt 绘制期转换到屏幕 sRGB，像素字节不变。
- **Adobe RGB 处理**：无 ICC、仅 `Exif.Photo.ColorSpace=2` 的文件，OIIO spec 读不到该
  属性时回落到 Exiv2 已读出的值并合成 Adobe RGB profile，避免误判 sRGB 造成输出色偏。
- **输出 ICC「消失」**：ICC 一直在文件里（T1→HEIF 的 `colr/prof` 为 560B ADBE）；根因是
  OIIO 的 HEIF/AVIF reader 不把容器 ICC 抬进 `ImageSpec`，本软件 probe 因此报
  `has_icc=false`。现在 probe 边界用 libheif 补读 `colr/prof` 并注入 spec，预览/信息卡/
  keep 分支拿到同一份字节。
- **HEIF 无损开关无效**：部分路径只写内部键 `__lossless`，`enc_heif` 只认显式
  `lossless` → `--lossless` 产物与有损逐字节相同。现在统一走 `lossless_flag()` 并直接
  调 `heif_encoder_set_lossless()`；无损产物已实测与有损不同且可重新打开。
- **HEIF 字符串参数被丢弃**：`ParamType::Enum` 一律当整数派发，`chroma=444/422`、
  `preset`、`tune` 全部报 "expected integer" 后忽略。现按值类型分派整数/字符串 setter，
  4:4:4 等参数真正落到 x265。
- **HEIF 10bit 档位**：新增**落盘后位深回读校验**（libheif 重新打开 tmp 文件读主图位深），
  后端若把 10 位静默降成 8 位直接失败；实测 SPS `bit_depth_luma_minus8=2`、回读
  `lumabits=10`。`general_profile_idc=4` 是 x265 静止图的 Main10 Intra 信号（与参考
  `super/T2.heic` 相同），不是 8 位 Main。
- **无边框三钮点不动**：三钮是 Qt 客户区控件，却对 `WM_NCHITTEST` 返回
  `HTMINBUTTON/HTMAXBUTTON/HTCLOSE`，Qt 的 FramelessWindowHint 链路吞掉非客户区点击。
  改为 `HTCLIENT`，由 `QPushButton` 处理 press/release/hover；保留
  `WS_MINIMIZEBOX/WS_MAXIMIZEBOX/WS_SYSMENU`。真机鼠标实测关闭/最大化/最小化三钮均生效。

### 新增

- **仅元数据图像字节保真测试**：走真实入口 `run_metadata_only`，写入 GPS（lat/lon/
  altitude/direction）后解析 TIFF `StripOffsets/StripByteCounts`（或 Tile）并比较压缩图像
  负载**逐字节**；覆盖 `tests/golden/base/rgb16.tif` 与可选 `super/T1.tif`
  （137,053,848 B 全等，GPS 已写入）。
- **ICC 探针**：`_DSF3043.heic` 的 HEIF 容器 ICC 在 probe 面可见（`has_icc=1`，560B
  ADBE）；`T1.tif`/`_DSF3043.heic` 预览 `QImage` 均携带 `Adobe RGB (1998)` 色彩空间。

### 变更

- HEIF/AVIF 编码档位文案：**「Main10（10 位）」→「Main10（10 位 · Intra）」**，并在
  兼容性提示中注明 x265 静止图写 Main10 Intra、部分工具显示为 Main（位深确实是 10 位）。

### 已知

- `super/T2.heic` 仍不可解码：其 HEVC RExt 工具体系超出 libde265 1.1.3 支持面，属解码器
  上游限制；本版输出（含 10bit/4:4:4/无损）用同一解码器可正常 reopen。
- 高德线上搜索只做了 URL/解析/错误分类与本机逻辑验证；本机 TLS 环境无法发真实请求，
  最终以用户账号实测为准。

### 发行

- tag `v0.3.5.alpha2`（annotated `3bf8a96` → commit `46fcb5c`）；`main-full` #79 全绿
  （10/10 job），`release.yml` #18 全绿（7/7 job）。
- GitHub Release `v0.3.5.alpha2`（published 2026-10-25T01:04:17+08:00，target=main）
  4 资产，API digest ↔ 下载件 `SHA256SUMS.txt` 三方逐字一致：
  - `PhotoPipeline-0.3.5.alpha2-win64.zip` **33,744,760 B**
    sha256 `a4dc4a68eb6788a5b76deb2b045c601f3487fed9deb2dce8f9b71cc97e868768`
  - `PhotoPipeline-0.3.5.alpha2-x86_64.AppImage` **52,058,616 B**
    sha256 `eb6b18b3dbbdd223ef27de0094f992ef84d0e6191d45d63be3fc1c41f1e9b98e`
  - `PhotoPipeline-0.3.5.alpha2-deps.txt` **2,439 B**
    sha256 `e8c80b3d61a4e0d41cde50eb3c04577688d7428cc189129b3331cf221407854d`
  - `PhotoPipeline-0.3.5.alpha2-SHA256SUMS.txt` **314 B**
    sha256 `e52340554ae80a6516e8fbcda1f623375fcc5187cccf87597b2a7e83573610ff`
- 解压 win64.zip 后 `PhotoPipeline/photopipeline.exe --version` =
  `PhotoPipeline 0.3.5.alpha2`（exit 0）；AppImage 由 release 链 `probe-appimage` 断言通过。
- 门禁（本机实测）：格式 violations=0 ｜ clang-tidy 262=基线 ｜ ctest release 35/35 ｜
  ctest release-dev 36/36 ｜ 金样 `SMOKE total=20 pass=20 fail=0` ｜ 回归 zero diff ｜
  `--ui-smoke` 冻结行 `UI-SMOKE OK shots=12 pages=3` + `layout-audit checked=1027 bad=0`。
- 收口报告：[docs/0.3.5.alpha2-report.md](docs/0.3.5.alpha2-report.md)（tag 后补记，
  报告类提交不入 tag）。

## [0.3.5.alpha1] — 2026-10-14

**预发布（alpha）**：GUI 渲染面缺陷根治 + **识图门禁**（用户两张红框截图 7 处缺陷 + 「没有识图步骤吗？」
追问的正面回应）。根因一句话：`tests/ui_smoke.py` 头明写「**截图仅存档，不做像素比对**」——
GUI 有测试但全是几何/语义断言，**渲染面（字形裁切 / 空图标 / 行错位 / 空盒）零机械判据**，
全靠人眼，于是 0.3.3/0.3.4 全部门禁绿灯放行。版本单源 = `CMakeLists.txt:19-21` = **0.3.5.alpha1**
（数字段 0.3.5 + 预发布标签 ".alpha1"；CMake `project(VERSION)` 只收数字段，标签拼在字符串侧）。

### 新增

- **识图门禁 `layout-audit`**（`smoke_probe_layout_audit`，`PP_BUILD_DEV` 门控；`tests/ui_smoke.py`
  追加 `UI-SMOKE layout-audit:` 齐备行）：六类**几何 + 像素**断言 —— ① text-clip（下拉文本区墨迹
  顶到切断线 / 磁贴主字被省略）② overlap（叶子控件全局矩形相交，裁到滚动视口内再比）
  ③ blank-btn / ink-btn（纯图标钮无文案无图标、实拍内区墨迹 < 12px）④ overflow（越出父矩形）
  ⑤ ghost（无名·无文案·无图标的裸空盒）⑥ card-blank / head-tall（卡体高于可见子件和）。
  每一处用户红框都有阳性对照（修复前 bad>0 逐条复现，修复后 `checked=1027 bad=0`）。
  12 张截图仍按原口径人工留档（不改"截图不比对"的既定取证口径）；**冻结末行逐字未动**。

### 修复

- **工具栏勾选操作钮白底**（debug1 ①）：`mini` QSS 选择器漏 `pp-check-menu` → 补进选择器，四钮同脸。
- **「关键信息」卡头大片空白**（debug1 ②）：`formkit::lock_card_size` 用**一次性**
  `setMinimumHeight(sizeHint)` 钉下限，隐藏行也计入 → 空态收起 5 行 kv 后卡体整片留白。
- **预览「上一张/下一张」两块白方**（debug1 ③）：`make_nav` 只挂探针钩子、**从不 `icons::apply`**
  → 随令牌重放矢量图标。
- **格式磁贴「JPEG XL」→「JPEG…」**（debug2 ①）：磁贴 `paintEvent` 无后缀时也减 6px 列距 → 与
  `sizeHint` 同式（后缀只在有后缀时让位）。
- **「渲染意图」下拉硬裁成「相对比色 + 黑点补M」**（debug2 ②）：项文案 150px 塞进 160 标准轨的
  132px 文本区 → 项文案改「相对比色+黑点补偿」，「（默认）」移入 R3 行尾注释槽随选择显隐。
- **三行开关不同列**（debug2 ③）：`make_flex_field` 给首个控件 stretch 1，单开关被居中到字段中段
  → 控件一律贴字段轨起点、富余宽归行尾。
- **格式参数行互相重叠 + 杂散空盒**（debug2 ③/④）：锁高陈值反向侧（建卡时表单还空只钉 84px，
  行建好后被压到 6px 节奏再被固定高弹回）→ 同卡头修复，重叠簇 20+ 条全消。
- **左栏搜索框挤成「···」**（debug1 ① 旁）：306px 工具行装不下 4 钮 + 160 下拉 + 搜索框 →
  分组下拉收短档 88（mockup 工具行口径），最长项「按相机型号」→「按机型」。

### 变更

- **`formkit::lock_card_size` 语义**：下限从"一次性钉数"改"实时 sizeHint 当下限"
  （`QSizePolicy::Minimum`，无 ShrinkFlag ⇒ 父布局把 sizeHint 当下限）—— 下限随内容实时变化，
  消灭"钉数陈值"整类缺陷（留白 / 压行两侧都出过）。
- **三页页级滚动条常驻**（`page_meta` / `page_output` / `page_run`）：内容高随选中/运行态变化会把
  竖滚动条"顶"出来/收回去，内容宽随之 ±15px，磁贴 2×4 栅格整体横移（用户可见的整卡回流 +
  既有断言 `tile-zero-deform` 漂移）⇒ 常驻滚动条钉住内容宽。
- **文案 2 处**（均有裁定登记 `docs/m7-adjudications.md` M7R25-3/4）：渲染意图项、分组项
  「按相机型号」→「按机型」（节头仍用「相机型号」，`pp::GroupKey` 语义不变）。

### 已知

- **整图基线比对仍未做**（12 张截图仍只存档）：上感知哈希/逐像素比对需先解决跨平台字体渲染差异
  （offscreen 的 CJK 以 tofu 满宽字形渲染，比真机墨迹宽约 10%），否则 CI 会红在字体上。
- **text-clip 判据只覆盖下拉**：按钮/chip/分段钮自绘带描边，墨迹法分不清"描边"与"被裁的字"，
  且宽度多随内容（结构上不裁）；其文本裁切暂无机械判据。
- 弹窗（设置 / EXIF 编辑器 / 预设）不在 layout-audit 扫描面内（归既有 `dialog-close` /
  `settings-dialog` 探针）。

### 发行

- 产物：Linux AppImage（x86_64）+ Windows 免安装 zip；**预发布（alpha）**，仅建议尝鲜/验证用。
- Release `v0.3.5.alpha1`（2026-10-14，target=main）四件资产，发行指纹（SHA256SUMS.txt 原文）：
  * `PhotoPipeline-0.3.5.alpha1-x86_64.AppImage` 52054520 bytes
    `23d8121c32454de845e73568de610235d7e9e569d290fc693969f491d3a36b7c`
  * `PhotoPipeline-0.3.5.alpha1-win64.zip` 33733574 bytes
    `80d76c21a935eab93d62e7a063e7dcad757d065f4bd73a6b2d4cdb3818231305`
  * `PhotoPipeline-0.3.5.alpha1-deps.txt` 2439 bytes
    `e8c80b3d61a4e0d41cde50eb3c04577688d7428cc189129b3331cf221407854d`
  * `PhotoPipeline-0.3.5.alpha1-SHA256SUMS.txt` 314 bytes
- CI `release` 运行（run #17，sha 61eade0）七 job 全绿：appimage / probe-appimage / winzip-build /
  winzip-build-dev / winzip-pack / probe-winzip / publish；`main-full`（main @61eade0）同步全绿。
- 门禁（本地实测）：格式 violations=0 ｜ clang-tidy 262=基线 ｜ ctest 35/35 ｜ 金样
  `SMOKE total=20 pass=20 fail=0` ｜ 回归 zero diff ｜ `--ui-smoke`
  `layout-audit checked=1027 bad=0` + `UI-SMOKE OK shots=12 pages=3` 冻结行逐字不变。
- 回归基线重播种 **0.3.5.alpha1**（归因：**版本行 12 / 行为 0**，逐文件 `golden.log` /
  `golden.windows.log` 各 12 行均为版本串）。
- 根因/证据/门禁归因见 [docs/m7r25-report.md](docs/m7r25-report.md)（含测试/门禁体系深查）。

## [0.3.4] — 2026-10-13

**M7-R2.4 热修轮**：0.3.3 Windows 发行件「双击打不开」P0 热修 + 该类缺陷的机械化防线。
根因一句话：M7-WP5 无边框收口把句柄获取从 `winId()`（会隐式建窗）换成在
`nativeEventFilter` 里对**每条**原生消息调用 `winId()`，窗口创建期（`CreateWindowEx` 同步投递
`WM_NCCALCSIZE`）因 `data.winid` 尚未赋值而**重入 `createWinId()` 递归建窗直至栈崩**（约 277 层
耗尽栈；每层泄漏一对隐藏顶层窗 + Qt 6.11 `_q_titlebar` 伴生窗），窗口全程不可见 → 用户侧 =
双击无反应、数秒后进程死亡（ntdll `0xc000041d` / `0xc0000005`）。offscreen 判据（金样 /
`--ui-smoke` / ctest）不触达真实建窗路径，0.3.3 全线门禁绿灯放行；真机验收清单（UX-7/UX-13）
当时标注"待人工签收"未签收，是流程侧直接原因。版本单源 = `CMakeLists.txt:19` = **0.3.4**。

### 修复

- **Windows 启动即挂（0.3.3 打不开，P0）**：`Frameless::nativeEventFilter` 与 `trigger()` 的
  句柄来源改为 `internalWinId()`（只读当前句柄、**永不建窗**；`qwidget.h` 取值语义），未建窗 = 0
  → 消息放行 Qt —— 消息处理期不再有任何隐式建窗面。触达面仅 `src/platform/frameless.cpp`
  （引擎 `src/core`/`src/codecs`/`src/decode` 零改动）；金样 **20/20** + 全语料回归 **zero diff** +
  `--ui-smoke` 冻结行逐字不变背书行为零变化。

### 变更

- **winzip 门禁 5 → 6**：新增 **E-3 真机启动探针** `tools/win_launch_probe.py`（冻结行前缀
  `WIN-LAUNCH OK`，与 SMOKE/UI-SMOKE 同一冻结纪律）——staging exe 走**真实 qwindows 平台**，
  断言「可见主窗出现 + 顶层窗口数不超阈（递归建窗风暴守卫）+ 进程存活」。阳性对照实测：
  0.3.3 坏包被拦（`WIN-LAUNCH FAIL … reason=window-storm windows=47>32`），修复后
  `WIN-LAUNCH OK windows=6 visible=1`。本类缺陷 offscreen 永不可见，此探针补上判据面的结构性
  盲区（本地与 CI 同一条判据）。

### 已知

- **0.3.3 的 Windows zip 为坏件（启动即挂）**：请改用 0.3.4；按发行纪律 v0.3.3 tag 落点与
  Release 记录不再改动，只在 README/CHANGELOG 标注口径。
- 门禁归因与"为何漏过"的完整分析见 `docs/0.3.4-report.md`（§3 门禁盲区 / §4 开发期盲区）。

### 发行

- 发行授权 = 用户明文「发布 0.3.4」（2026-10-13）；tag `v0.3.4` → `d1f878b`（发行树，含 P0 修复 +
  E-3 探针门禁 + 版本单源 0.3.4），报告类提交不入 tag。
- **发行终态（2026-10-13T11:37:39Z 已发布）**：tag 推送触发 `release.yml` run 38389821897
  **七 job 全绿**（appimage / winzip-build / winzip-build-dev / probe-appimage / winzip-pack /
  probe-winzip / publish；winzip-pack 内即本版新增的 E-3 真机启动探针，随发行链首跑即绿）；
  GitHub Release v0.3.4 四资产，API digest ↔ 下载件 `SHA256SUMS.txt` 逐字交叉一致：
  - `PhotoPipeline-0.3.4-win64.zip` **33,732,633 B**（sha256 `86e17914…`）—— make_winzip 六项
    门禁（A 闭包 / B 来源 / C 结构 / D 许可 / E-1 启动版本 / E-2 offscreen ui-smoke / E-3 真机启动）
  - `PhotoPipeline-0.3.4-x86_64.AppImage` **52,062,712 B**（sha256 `da6f7ba5…`）+
    `PhotoPipeline-0.3.4-deps.txt` **2,439 B**（sha256 `e8c80b3d…`）
  - `PhotoPipeline-0.3.4-SHA256SUMS.txt` **293 B**（sha256 `2e04b874…`）
- 发布说明取本节**之前**的 `[0.3.4]` 正文（止于本小节）；收口报告 = `docs/0.3.4-report.md`
  （tag 后补记，含门禁/开发期双归因）。

## [0.3.3] — 2026-10-12

**M7 工程收口轮（M7-WP1…WP13）+ R2 轮（M7-R2A…R2F）+ R2.1 轮（M7-R2.1-F1…F4）+
R2.2 轮（M7-R2.2-G1…G6）+ R2.3 轮（M7-R2.3-G7…G9）**：发布前
深度评审（`docs/deep-review-0.3.2.md`，331 条 R-id）与 UI 14 项评审（`docs/ux-review-14.md`）
的批量整改 + 结构重构 + 尾单清扫与能力补强。两条口径：① **数据安全双 P0 根治**（输出路径
不得命中源文件；元数据写回原子化、失败=失败不再报成功）；② 整改与重构以金样 **20/20** +
全语料回归 **zero diff** 背书（默认行为零变化；能力类新项一律 opt-in、默认关）。331+14 逐条
覆盖台账（R2.3 终态四档）见 `docs/m7-report.md` 附录，开发期裁定汇总见
`docs/m7-adjudications.md`，R2 方案真源见 `docs/m7r2-plan.md`，门禁读数摘录见
`docs/m7-evidence/`。版本单源 = `CMakeLists.txt:19` = **0.3.3**。

### 新增

**数据安全与原子写盘（R-001/002/003 双 P0 + 全家）**

- **输出路径命中源文件即整文件拒绝**：渲染输出路径后与源做 `fs::equivalent`/词法等价检查，
  `out == src` 直接报错（含仅元数据模式）——误配「输出根=输入目录+覆盖」不再毁原图
- **原子写盘**：5 个编码器与元数据写回统一「同目录临时文件 → 字节校验 → rename 提交，
  失败清场」（`fsops::write_atomic_file` 族）；Overwrite 不再先截断旧好文件，失败不留半成品
- **元数据写失败=失败**：Exiv2 就地截断重写改为临时文件+rename；写失败输出记 `ok=false`，
  不再吞成 warning 报成功；仅元数据模式源读取失败不再按"空元数据"清空副本
- **密钥落盘保护**（R-094）：`amap_key` 以 DPAPI 封存（Windows，`ppsec1:` 密文）落 settings，
  非 Windows = 编码 + 0600（口径如实：编码不是加密）；明文老格式读入兼容、首次保存升级
- 文件名预检（保留名/非法字符/长度）+ 输出根试建试写探针；长路径清单 `longPathAware`

**编码档位核心行（UX-10）**

- HEIF/AVIF 新增核心行「编码档位 (Profile)」（`pp-profile-row`）：HEVC `Main/Main10/Main12/
  Main Still Picture`、AV1 `Main/High/Professional` + 自动档；选定档位→位深下拉限幅，
  改位深/色度→档位自动重算；档位与当前设置不一致给中文错误（不静默改写），非 Main 档给兼容性提示
- `speed`/`tune` 提核心（SVT 无 `preset` 的白名单错位修正）；落盘前按插件推导规则反查档位
  并记 info 日志

**交互与版面（UX 评审）**

- **三卡「启用」开关**（UX-5）：时间偏移/GPS/隐私卡头右置开关，关=整卡不参与规则（卡内控件
  置灰），与"参数是否为空"解耦；GPS 折叠面 `gps_enable` 保留为同一状态第二入口
- **右键菜单**（UX-1）：公共件 `src/ui/contextmenu.{h,cpp}` + 12 类卡片菜单构造；本轮接线
  文件列表（含勾选全部/取消/反选、按分类打标、复制路径）与运行页三卡
- **输入预览分辨率提升**（UX-3）：内嵌预览门槛拆两通道——缩略图仍 64px，预览要 ≥1600px
  才采用（否则整图 float32 解码）；预览缓存上限 2048→4096px
- **GPS 卡地图 App 化**（UX-4）：搜索结果/联想改地图浮层（不选自动消失）、6 类目并入「分类」
  下拉、图层浮到地图左上、地址行并入已选点 tooltip、地图 136→260px
- **窗口真无边框 + Win11 圆角**（UX-7/UX-13）：`WM_NCCALCSIZE` 收口（原生标题栏/复古三钮
  消失）、三钮命中优先于缩放带（含顶部 6px 死区修复 R-312）、`DWMWA_WINDOW_CORNER_PREFERENCE`
  圆角（Win10 静默忽略）；主窗+四弹窗同一收口。**真机验收清单待人工逐项签收**
- **滚动条细浮层**（UX-11）：6px 半透明滑块、轨道透明、悬停加宽，主窗与弹窗同一套皮
- 高级参数区去掉 224px 硬限（UX-8 前半：完全展开、滚动只剩整页一层）
- 格式页签去「· N 位」+ 页签等宽、磁贴去位深文本（UX-9，位深控制功能原样保留）

**R2 轮补记 · 新增（M7-R2A/R2E2/R2D1/R2D2）**

- **色彩能力四选项（引擎面，默认关）**：色域映射（无操作 / 软膝 / 感知压缩三档）、8bit TPDF
  抖动（±0.5 LSB，确定性；**仅 JPEG/WebP/JXL 及 OIIO 系四家 8bit 量化段**，HEIF/AVIF 的 8bit  路径不接）、渲染意图（相对/感知/绝对/饱和四档）、线性光重采样（预览降采样）。
  **默认关 = 各选项未启用时行为不变**（等价断言背书；金样 20/20 是断言级判据，非与 0.3.2 的字节比对——0.3.x 默认路径另有已归因字节变化，见「变更」）；R2 时点**界面开关尚未接线**
  （引擎面 `RunConfig` 四字段可配）——**R2.1 已接线**，见下方「R2.1 轮补记 · 新增」
- **HDR / CMYK 解码口径**：PQ/HLG（BT.2100）输入明确「暂不支持、保留合成 ICC 处理」不再
  静默按 sRGB 重解释（不建 HDR 管线）；JPEG CMYK/YCCK 一致拒收（probe/decode 双面）
- **EXIF ColorSpace(0xA001) 消费**：无 ICC 且声明 AdobeRGB 时按声明合成 ICC 入管线
- **RTL 布局镜像**（R-218）：自绘几何/内距/非对称 QSS 随布局方向镜像，LTR 逐字节不变
- **右键菜单 12 卡全接线 + 全选入口**（UX-1/UX-2）：工具行「勾选」钮 + Ctrl+A / Ctrl+Shift+A /
  Ctrl+D 快捷键（作用于可见行，过滤态有作用域提示）
- **失败原因下钻**：失败清单落盘 `logs/failed-<时间>.txt`，状态栏「N 项失败」可点跳运行页；
  失败原因行中文分类（原文进 tooltip）
- **运行页「峰值线程」读数行**（估算口径，未采集显「—」不编数）；参数区子分组二级折叠
  （默认只展开首组、「N 项」计数）；HEIF/AVIF 档位↔色度限幅联动 + 「已禁用：原因」全量推广

**R2.1 轮补记 · 新增（M7-R2.1-F4）**

- **色彩能力四选项的界面入口**（UX-14 C1–C4 的 GUI 面）：输出页全局卡新增渲染意图
  （相对/感知/绝对/饱和）、色域映射（无操作/软膝/感知压缩）、8bit 抖动、线性光重采样四控件
  ——**全部 opt-in，默认值 = 现行为**；线性光选项同步预览通道（切换即重解当前图）。
  引擎行为实现见上方 R2A 条目；本条只是把它接到界面上（R2.1 之前只能经引擎面 `RunConfig`
  启用）。
- **输出页「基准目录」行**（CLI `--base` 的 GUI 入口，R-319）：收集与重算共用同一派生单点；
  留空 = 现行为（源文件所在目录），会话内生效、不写设置。

### 修复

**数据安全与流程（P0/P1）**

- R-004 仅元数据模式源元数据读取失败 → 该文件失败（不再清空副本 EXIF/XMP）
- R-005 失败输出不再登记 reserved（Skip 策略下不再"幽灵跳过"后续同名源）
- R-006「再次运行」改为**补齐语义**：已存在且非空的输出整份不提交（conflict=Skip），
  不再产 `photo (1).jpg` 式重复件；仅元数据模式 GUI 默认冲突 = Skip（R-074）
- R-007 Windows 长路径/保留名/非法字符预检（模板渲染后统一校验，CON/PRN/NUL 等拒绝）
- R-023/232 classes.json 损坏/缺键不再被默认值覆写（显式失败、注册表不动；version>1 拒绝）
- R-028 settings/classes/预设三份持久化 tmp 名带 PID/随机后缀（双开不互踩）
- R-029 GPS 时间戳非法不再擦除既有 GPSDateStamp/GPSTimeStamp（保留源值 + Warning）
- R-030 仅元数据失败删本次创建输出；R-031 输出根在输入树内时收集自动排除
- R-083 模板纯字面段结尾（"jpeg/"）在校验层拒绝，不再渲染"目录名当文件名"

**交互正确性（R3 必修 4 条 + 主流程）**

- R-150 版本徽标改取 `PP_VERSION_STRING`（不再硬编码 0.3.0）
- R-151 翻图（←/→）同步左栏选中，打标不再打错对象
- R-152 过滤态双击打开 EXIF 编辑器补 `mapToSource` 映射（不再编辑到别的文件）
- R-153 分组重建整组快照/恢复多选；「清除筛选」合并为一次过滤
- R-008 进度事件不再覆写文件行状态（状态胶囊不再全程「排队」）
- R-069/R-076 0 字节产物琥珀「0 字节」呈现；多输出部分成功独立「部分成功」档
- R-139/140 终态词表统一（已完成/部分成功/跳过/失败/已取消）+ 状态栏补跳过/取消/部分三桶
- R-071/154 运行中拒收拖放（不再混入下一批）；R-146「再次运行」含失败先确认
- R-073/075 输出根可写性预检、GPS 规则非法预检确认；R-067 运行前提示被过滤隐藏的参与文件
- R-107 窗口析构显式 cancel→wait→置空回调；R-024/248 事件洪峰合并刷新、批量移除合并信号
- R-142 分类行右键「清空该类勾选」（不再误清他类）；R-263 重命名预填显示名
- UX-2 勾选全部/取消/反选（右键菜单，作用于可见行）；R-227 缩略图探测不再覆盖流水线状态

**参数假接线与色彩正确性**

- R-010 PNG `compressionLevel` 键名修正（`png:compressionLevel`，调 0–9 真实生效）
- R-011 TIFF predictor 控制面复核**与评审结论相反**：上游真实消费（键名对齐 `tiff:Predictor`，
  none/horizontal 生效）；`float` 档整型输出不可用 → 显式报错
- R-244/245/246 JPEG「算术编码 / XYB 量化 / DCT 方法」三个假控件锁定 + 「已禁用：原因」
- R-032（UX-14 A2）灰度源带 RGB ICC（或反向）不再整文件失败——忽略 ICC 降级 + Warning
- UX-14 A1 HEIF/AVIF nclx 不再写死 sRGB，按嵌入 ICC 反推 CICP（残余见「已知」）
- UX-14 B1/B3 `test_color` 补 ΔE2000 往返护栏（≤1.0）；ICC 嵌入回读断言扩 heif/avif/tiff
- R-033 `downscale_avx2` 极窄行越界读修复（掩码载入）；R-089 HEIF 各平面量化口径统一
- R-230 HEIF 内省默认值取插件当前值；R-247 `psnr_target` 联动提示

**地图（M6 遗留）**

- R-012 联想请求清算（清空/搜索后陈旧建议不再复活可点进写坐标闭环）
- R-013 amap key 脱敏（错误文案/截图不再泄 `key=`）
- R-077 空 Key 预检直接给「请到设置›地图填写」；R-045 换源清理两路共用
- R-046 精度存疑文案与几何对齐；R-047 生效值刷新不再把地图拽回（仅坐标变化才居中）
- R-101 瓦片失败延迟重抓；R-102 离线判序（在途请求不再误吐「离线模式」清列表）
- R-103 世界方块外点击不再取 |纬度|→90；R-158/224 适配映射单源 + 图层表驱动
- R-161 坏瓦片不再抵消失败计数（横幅阈值可达）；R-220 瓦片子域改纯函数（同瓦片恒同）
- R-221 paint 幂等（网络请求移出绘制）；R-223 行/钉按稳定键对齐（点 A 不再弹 B 行）
- R-233 时区偏移具名表（+5:45/+8:45 等不再被改写成 UTC+8）；R-313 点选联想停防抖定时器

**工程 / CI / 供应链**

- R-014 GitHub Actions 全部钉完整 commit SHA（含发布链）
- R-015 回归基线重播种强制先归因（版本行 vs 行为变化逐行计数，写失败不动基线）
- R-016 nightly 补 Windows 基线 diff job（双平台回归不再单腿）
- R-017 UBSan 假绿封堵（`halt_on_error=1` + `Testing/Temporary` 扫描）
- R-018 语料 sha256 锁（`tools/baseline/corpus.sha256` 入库，漂移拒跑）
- R-019 clang-tidy 棘轮 `--require-baseline`（未播种不再静默下线）
- R-036/037/038/039 工具链钉版与校验：clang-format sha512 校验、Qt 归档 sha256 必填、
  解包穿越防护补 Windows 面、Python 工具链钉 `==x.y.z`
- R-040 金样告警断言三模式（`warnings_exact`/`warnings_empty`），20 对告警面全覆盖
- R-041/042 sanitizer 覆盖夜间轮转 + 选项下沉 job 级；R-043 pr-fast 补 ui-smoke 步骤
- R-091 VCPKG_SHA 变活锚；R-092 发行包内 NOTICE（GPL §6 源码 offer）
- R-095 fixtures 缺件计 FAIL；R-096 ci-tidy 头文件告警跨 TU 去重；R-098 env.py `--strict`
- R-120–132 工程化清扫（Qt 字面量清零/单源断言补齐/vcpkg 显式依赖/dev test preset/
  .gitignore+.gitattributes 补齐/versions.env 解析统一/.clang-format c++23 等）

**结构与契约（B.4/B.6/B.7/B.8 蓝图落地）**

- R-156 编码器族 500 行五连拷贝收编 `codecs/enc_common`（新增第 6 编码器成本 600→~300 行）
- R-061/324 `run_one_file` 拆十阶段（单一上下文）；R-168/174/175 双入口/取消出口/PathCtx 单源
- R-172/176 `OutputState`/`ErrorKind` 显式枚举 + 唯一迁移点/判据单源
- R-193/323 main.cpp 拆四 TU（GUI/dev_cli/dev_bench/dev_ui_smoke，发布构建零 dev 路径）
- R-157 日志级别解析单源 `pp::log_level_from_text`；R-160 trace 序号竞争修复
- R-048/116/258 `--ui-smoke` 探针整体迁出 mainwindow 并 `PP_BUILD_DEV` 门控
  （发布二进制不再含探针）；R-049 mainwindow 按数据所有权拆五域
- R-284/287/288 进度算术纯函数单源 / 状态机收口 / 高水位单层；R-166 HEIF chroma 滚动缓冲
  （48MP 峰值 ~+384MB → ~+128KB，产物字节不变）
- R-208/209 技术判定与镜像 ParamDef 收敛单源；R-158 provider 三塔收敛（冻结面加性）
- R-159 icons 相对 `s` 反射点双重偏移修复；R-271 icons 解析器补单测
- R-308/309/310/312 frameless 取整单轨 / 空目录不回退 CWD / QPointer / 三钮命中面

**R2 轮补记 · 修复（全部尾单，M7-R2B/R2D1/R2D2/R2E2）**

- **数据/路径面**：R-007 长路径 `\\?\` 归一 + 259/255 长度硬线解除（保留名/非法字符/尾随点空格
  防线不动）；「再次运行」预检先删自家 0 字节残次品（源文件绝不触碰）；预设保存/删除对空目录
  路径显式拒绝（不再拼 CWD 相对名）
- **运行语义**：R-076 多输出部分成功 → 独立「部分成功」档引擎侧单源映射；R-163 取消不再误归
  失败（Cancelled 单点归类）；R-226 磁贴下标越界潜伏点消除
- **校验与预设**：R-231 预设全量校验（未知键显式拒绝 + 跨字段复核）；R-206 跨字段规则双通道
  共用；R-135 预设未知键 load→save 往返不丢；R-066 预设后端往返不再被 alpha 预选静默改写；
  R-249 设置主题「取消」回滚（不再永久写入未确认选择）
- **列表/预览/分类**：R-009 主题切换后运行页令牌刷新；R-025/026 大目录扫盘后台化 + 分组回填
  O(1)（GUI 不冻结）；R-143 分类 chip 给过滤语义（`.on` 单义）；R-148「N 项未能读取」计数提示；
  R-149 同文件别名（大小写/8.3 短名）去重；R-251 主窗早期取色读空令牌；R-276 滚动后色点与胶囊
  错位；R-277 预览失败显「无法预览：…」不再恒「载入中」；R-314 列表焦点下 ←/→ 不再被翻图吞掉
- **输出/表单/编辑器/设置**：R-136/321 模板 `$format` 判据 GUI 两处副本退役改调单源；R-137
  图层词表设置侧改调 layers 单源；R-138「分文件夹」标注生效时机；R-217 参数编辑器工厂收敛；
  R-228 磁盘空间查询去抖（不再逐键 stat）；R-265/266 page_output 死码三连 + 游离格式陈旧徽标；
  R-270 菜单/页签内距令牌化；R-099 EXIF 编辑器 DMS 三轨统一单源；R-318「已禁用：原因」全量推广
- **结构拆分（B.3 局部）**：R-190 下拉 itemData 单真相（调序不再静默串档）；R-191 avif 提示
  谓词单源；R-203 分组代理转发两列；R-326 FileListModel 不变量成文；R-275/281 设置几何探针
  去重/去副作用（断言换锚同严）

**R2.1 轮补记 · 修复（M7-R2.1-F1…F4）**

- **原子提交瞬态锁根治**（D-1）：rename 提交遇 Windows 瞬态文件锁（杀毒/索引器短锁）不再
  直接报失败——共享违规类错误有界重试（≤3 次、退避 25/50/75ms），其余错误与 Linux 行为不变
- **多选恢复塌缩**（R-153 尾单）：「多选后敲过滤字符」场景恢复整组选择时不再塌缩成一行
  （R2D1 修复未覆盖的默认选择命令路径，回归探针抓出后修复）
- **分类面板重建闪烁/误连**（R-183 前半）：重建行时旧代控件与延迟删除解耦（同步断信号面），
  不再偶发误触发旧控件信号
- **Linux 测试修复**：test_fsops 长路径断言样例按平台拼写（Windows 路径样例在 POSIX 路径语义
  下被误判非法字符），两侧断言逐条相同、严格度不变；main-full linux ctest 失败注解遥测
  （失败测试名 + 断言行 → CI 注解 + Step Summary 全量明细，判据可见性改进，永久保留）
- **参数查找口径统一**（R-195）：预设导入/`--dev` set_param 三处调用点全部走同一查找单源
  （first-wins）；`--dev` 的 `--help` 醒目标注 CLI/GUI 默认面分歧（R-145：conflict 默认
  overwrite vs rename、单输出模板差异）
- **冲突策略词表单源**（R-239）、**地图图层词表单源**（R-137：词表在 core、映射在 layers）、
  **Caption 双实现合并**（R-279）、**EXIF 编辑器构造期读元数据后台化**（R-081，打开编辑器
  不再卡界面）、**相机型号读取下放引擎访问器**（R-204）、**孤儿信号 format_changed 删除**
  （R-133，通知统一走 config_changed）、字节读数三套格式化收敛单源（R-264）、note 占位
  魔法空格收口（R-269）、分类面板头注释改实（R-260）
- **滚动条锁卡补完**（UX-11 余量）：元数据页信息/时间/开关三卡加高防压缩（GPS 卡维持动态
  高度，与地图 stretch 互斥——如实登记）；亮色页签像素档探针（UX-12 余量）入常驻走查

**R2.2 轮补记 · 修复（M7-R2.2）**

- **分类面板重建闪烁/误连根除**（R-183 后半）：rebuild_rows 改「规格表 + 就地改配」，稳态下
  行部件零销毁重建（部件身份跨重建稳定，deleteLater 时序退出常规路径）
- **运行页行映射鲁棒性**（R-256 后半）：进度事件按行号落位由「行序不变」隐式约定改为显式
  校验 + 路径稳定键兜底（行序漂移时按源文件路径重定位，找不到即不落行、不落错行）
- **Linux/多字体环境 UI 走查误红修复**（R-044 容差机制）：亮色页签像素档断言加字形内核判据
  （灰阶 AA 漂移不再误判主题错误；语义仍钉、断言只增不减）
- **EXIF 编辑器时间清除语义换锚**（R-197，**行为变化**见「变更」）：「清除」现连带
  OffsetTime* 三键与 XMP MetadataDate，与批量规则命中面/时间偏移触达面同集
- **失败原因清单与 0 字节补齐的机械回归**：R-144 失败清单行式落盘 + 状态栏下钻、R-006
  「再次运行」0 字节补齐语义入常驻 ui_smoke 探针（真实损坏样本编排，回归防线前移）

**R2.3 轮补记 · 修复（M7-R2.3）**

- **关现役后现役格式页签落点归位**（R-266，修 R-189 语义漂移）：关闭现役格式后现役**前移到
  首位**（与格式选择契约一致），不再落在页签滑动邻接位；机械探针断言钉住（断言只增）
- **禁用态原因提示补齐**（R-318 两处余量）：预设对话框三按钮（载入/另存为/删除）禁用时补
  原因 tooltip（与输出页预设按钮同款双态）；参数区技术条目置灰补条目级原因 tooltip
  （恢复可选即清空）

### 变更

- 仅元数据模式 GUI 默认冲突策略 **Rename → Skip**（不再静默产 `(1)` 副本）；CLI/金样默认面不动
- 终态词表统一为「已完成 / 部分成功 / 跳过 / 失败 / 已取消」（列表胶囊「完成/取消」随之更名）
- 格式页签文案去「· N 位」、页签等宽（位深控制功能原样保留）
- 滚动条 10px 实心轨 → 6px 细浮层（含弹窗同皮）；按钮内距 11/14 → 12 单源
- 日志 `[file]` 标签随 TU 拆分迁移（`[main.cpp]` → `[dev_cli.cpp]` 等，机械）
- 回归基线两次重播种（逐行归因见提交 `d882fe6` / `59de09d`；heif 档位 info 行 46 行 +
  TU 标签迁移 180 行 / 消双记 4 行）
- `tests/golden/SCHEMA.md` 加性扩展告警断言字段（v1 字段全保留）；expected.json 仅增不改

**R2 轮补记 · 变更**

- **heif/avif 输出字节 +20B/文件**（无 ICC 输出新增 colr/nclx 盒）：UX-14 A1 nclx 精确信令
  （P3=12、sRGB=1，码流位流直读）的直接推论——回归基线已按此逐行归因重播种（132 行，
  `docs/m7-evidence/m7r2-gate-regression-reseed.md`）；带 ICC 的输出零变化
- 参数区折叠行数值控件宽 88 → 160（UX-8 定稿「废除折叠=88 第二档」）；子分组默认只展开第一组
- 分类 chip 语义改为**过滤档开关**（左键=按类过滤，`.on` 单义=当前过滤档；选中项归属不再由
  chip 高亮、由行内色点承担）
- `format_changed` 发射按「若变」约定（apply_preset 仅现役格式真变才发）
- 色彩能力四选项**默认关**（能力面 opt-in；未启用时与 **R2A 入树时点的既有行为**逐字等价
  ——**不是**与 0.3.2 的字节等价：0.3.x 默认路径另有两处已归因字节变化：B9 chroma 量化统一
  ±1 LSB 级、A1 nclx 使无 ICC 的 heif/avif +20B/文件）；RTL 镜像仅作用于
  RTL 布局方向（LTR 逐字节不变）
- 回归基线在 0.3.3 窗口共 4 次重播种（`d882fe6`/`59de09d`/`97cd9da`/`09b5340`，均先逐行归因）

**R2.1 轮补记 · 变更（M7-R2.1）**

- **内部结构拆分**（行为零变化，金样 20/20 + 探针行逐字背书）：元数据页按蓝图 B.1 拆十个
  内部单元（页面头逐字节冻结）；EXIF 编辑器孪生函数收敛 + 时间键单表；设置对话框构造函数
  按 5 页签拆分；文件列表一 TU 六类按所有权拆分；运行页刷新分 7 段、控件束单源
- **RTL 支持扩面**（R-218 续）：元数据页与地图自绘几何/对齐逻辑坐标化（LTR 逐字节不变；
  地图瓦片/图钉等地理投影内容不随语言翻转）
- **静态分析基线重播种**（工程面）：撤销上一轮 11 处行级 NOLINT 抑制、改零行为结构修复
  （参数组结构化 / 显式转换封装）；无零行为修法的 7 条按归因重播种，基线总量只减不增
  （264→263）；仓库恢复零抑制

**R2.2 轮补记 · 变更（M7-R2.2）**

- **EXIF 编辑器时间「清除」语义换锚**（R-197，主对话裁定 M7R22G2-0，**行为变化**）：清除
  现在连带 touched 全部 9 键（`OffsetTime`/`OffsetTimeOriginal`/`OffsetTimeDigitized` 三键
  + XMP `MetadataDate` 一并移除），与批量规则命中面、时间偏移触达面同集；原「只清墙钟
  5 键」口径废止。缺键清除仍为 no-op，无新错误面
- **内部结构拆分**（行为零变化，金样 20/20 + 全语料回归 zero diff + ui_smoke 冻结行背书）：
  mapwidget 按蓝图 B.5 六步全落（瓦片缓存/交互状态机/绘制/抓取/地理编码拆独立单元，
  mapwidget.cpp 1444→760 行）；mainwindow Impl 成员五舱分域；EXIF 编辑器时间清除谓词、
  设置对话框控件对象图、字段样式词表单源化
- **RTL 支持续**（R-218）：共享 QSS 非对称几何改逻辑侧（RTL 下对称镜像，LTR 逐字节不变）
- **`tests/golden/SCHEMA.md` 加性补档**：progress_trace `state` 值表补 `done_with_errors`
  （多输出部分成功终态；既有值表行逐字不动，裁定 M7R22G3-1）
- 无新增用户可见功能（R2.2 交付面 = 判据加固 / 结构余量 / 文档收口）

**R2.3 轮补记 · 变更（M7-R2.3）**

- **内部结构拆分/收口**（行为零变化，金样 20/20 + ctest 36/36 + ui_smoke 冻结行背书）：
  mainwindow 两个巨型装配函数 build_ui/wire 语义分段（R-253 四装配函数至此全落）；dialogkit
  标题栏 icon 成员化（findChild 反向伸入退役，M7R23G8-2 加性追认）；探针词表单源化
  （meta_probe_context.h 补 13 键 + smoke_probes 字面量清零，M7R23G8-3）
- **注释改实**（R-268/R-260 余量）：page_output/classify_panel/preview_panel 三处前置声明
  旧理由与 page_meta 钩子清单引用口径对齐现状（纯注释，零行为）
- **CI 发布链工程面**（用户无感，判据零改动）：winzip 预算工程解——打包 job 改为消费已验证
  构建产物（双 artifact）+ release-dev 子集段拆独立 job；五门禁/E-2 走查/冻结行断言/暖度断言
  逐步逐字保留，`timeout-minutes` 不动（M7R23G7-0）；main-full #58/#59 全绿实证 per-job
  ≤20min（winzip job 97s/89s）
- 无新增用户可见功能（R2.3 交付面 = 两处交互修复 / 结构收口 / 发布链工程解）

### 已知

- **线程读数为估算口径**（R-078）：「总活跃 ≤ T」成立的是 `active ≤ W ≤ T` 与 `ΣE ≤ T`；
  worker 主线程 + 编码线程两族之和在 `W*2 > T` 分支可达 2W，且 **SynthPump 采样线程与
  x265 库内线程池不入账**。`RunSummary::threads_total_est` 是两族峰值之和的**估算**，
  不是硬上界；UI 读数未接线处显示「—」（不编数）
- **色彩能力四选项已入引擎、默认关**（色域映射 / 8bit 抖动 / 渲染意图 / 线性光重采样，
  R2 轮 R2A 交付行为实现 + 护栏，R2.1 交付界面入口）：默认关=未启用时行为不变；界面开关
  在**输出页全局卡**（渲染意图/色域映射/8bit 抖动/线性光重采样四控件，opt-in）；8bit 抖动
  仅作用于 JPEG/WebP/JXL 及 OIIO 系四家的 8bit 量化段，**HEIF/AVIF 的 8bit 路径不接**
  （扩面需另行开发）
- ~~UX-14 A1 残余~~ **已闭环**（R2A）：显式传 `output_nclx_profile` 后码流 VUI 精确落
  P3=12 / sRGB=1（位流直读断言）；代价=无 ICC 的 heif/avif 输出 +20B/文件（见「变更」）
- Windows 真机验收清单（拖拽/双击最大化/八向缩放/Snap/三钮/圆角/多显示器 DPI 等 10 项）
  须人工签收；offscreen `--ui-smoke` 无法替代
- Linux 回归基线（`tools/baseline/golden.log`）仅随版本 bump 定点替换 12 行
  `version {lib=app version=…}`（0.3.2 → 0.3.3，同 `6d98e2e` 先例做法）；M7/R2 的行为行
  （TU 标签迁移 180 行 + heif profile 46 行 + R2 nclx 132 行等）**未移植** —— 须在 Linux
  宿主全量重播种后确认（R-015 纪律，受约束登记 R2F-C3）；发行后首个 nightly 必看
- 深度评审余量与结构性债逐条登记在 `docs/m7-report.md` 覆盖台账（终态四档），未静默丢弃
  ——R2 轮已收 R-025/026/066/072/RTL 等，R2.1 轮再收 page_meta 拆分、settings ctor 拆分、
  filelistmodel 拆分、R-068/137/145/239/319 交互族等，R2.2 轮再收 mapwidget 拆分（B.5
  步②–⑥）、R-254/256/201/183 各余量与探针总集，R2.3 轮再收 R-266 语义归位 / R-318 两处
  tooltip / R-253 分段 / ProbeContext 迁移（详见该报告 §6–§9）；仍在册的结构债
  择要：契约强类型化 R-325、三套异步脚手架 R-202、两页骨架 R-330、组折叠头整行可点
  （UX-8 余量）——~~R-318 两个未覆盖面~~ / ~~R-266 UI 分歧~~ / ~~ProbeContext 词表缺口~~
  **已由 R2.3-G8 消**（`af7687f` / `4c9d7db` / `9b37f75`）
- **色温显示从不出现（受约束登记）**：EXIF 编辑器白平衡行的色温拼接（`Exif.Photo.
  ColorTemperature`）在当前 Exiv2（vcpkg 钉版）下**不可构造**（键表不接受该 tag）⇒ 该段
  恒不显示；防御分支保留、不升依赖、不加假数据（裁定 M7R23G9-1）
- **R2.2 判据面残余（如实）**：~~R-044 容差机制与 R2.2 探针批的 Linux 侧实证读数未取得~~
  ——**已取得**（main-full #54 linux 含 `ui_smoke` 绿起，#58/#59 全绿复证）；探针批与容差
  机制的本机读数为 Windows 侧，平台敏感判据已按 ±2px/通道容差收敛

### 发行

- Windows：`PhotoPipeline-0.3.3-win64.zip` —— **33,732,900 B**（sha256 `0074d2f9…`，Release
  附件逐字；zip 不承诺字节可复现，本机打包 34,801,903 B / `3f41297e…` 仅供参考），
  `make_winzip.py` 五项门禁（A 闭包 / B 来源 / C 结构 / D 许可 / E-1 启动版本断言）
- Linux：`PhotoPipeline-0.3.3-x86_64.AppImage` **52,062,712 B**（sha256 `3e8bb822…`）+
  `PhotoPipeline-0.3.3-deps.txt` **2,439 B**（sha256 `e8c80b3d…`，`tools/appimage-check-closure.sh`
  闭包清单）—— 由 tag 触发的 `release.yml` `appimage` job 在 Linux runner 产出（本机 Windows 不构建）
- 第 4 附件：`PhotoPipeline-0.3.3-SHA256SUMS.txt` **293 B**（sha256 `0e7ded56…`；三个发布件的
  sha256，逐字取自下载件）
- **发布链路**：tag `v0.3.3` 推送触发 `release.yml`（appimage → probe-appimage /
  winzip → probe-winzip → publish）；publish 前置探测 = Release 查重 + tag↔版本断言 +
  产物 `--version` 逐字断言；发布说明取本节**之前**的 `[0.3.3]` 正文（止于本小节）
- 发行前门禁（本机实跑，2026-10-12）：`ci-format` violations=0（152 文件）·
  `ctest --test-dir build/release-dev` **35/35**（含 `ui_smoke` 冻结行）· 金样
  `SMOKE total=20 pass=20 fail=0` · 全语料回归 **zero diff**（Windows 基线重播种 12 行
  version 行，逐行归因 = 版本行 12 / 行为 0）· `make_winzip` 五门禁全过（E-2 offscreen
  走查未带 `--smoke-exe` 未跑；`release.yml` winzip job 带 `--smoke-exe` 复验）·
  `tools/ci-versions-check.py`（R-122 单源一致性）通过 · 密钥扫描 0 命中
- `vcpkg.json` `version` 与 `CMakeLists.txt:19` **同改** 0.3.3（R-122 契约「版本 bump 必须
  同改」；`ci-versions-check.py` 第 3 项在 CI 钉住，首推 tag 时该项红灯后补齐）
- 「已发行」终态与 CI 产物指纹于**发行后收口提交**登记（`d049d22` 先例，落地见本节末
  「发行终态」条）；2026-10-12 实况（历史）：release #13（tag `v0.3.3` → `b1af371`）winzip
  job 撞 `timeout-minutes: 20` 硬顶被取消（build 单步 19m11s 未完）、publish 跳过 ⇒
  GitHub Release 未创建，处置待主对话裁决（全链见 [docs/m7-report.md](docs/m7-report.md) §3.8）
- **R2REL 发行包实况（2026-10-12 晚）**：预检（零 Release 零资产）与本机门禁（ci-format 0 diff /
  ci-versions-check OK / make_winzip 五门禁 + E-2 rc=0，冻结行逐字）全绿后推送 main
  （`a4cf0fc..2c50bfc`）；main-full #44 **红**——linux（全 ctest+金样）`ctest --preset release`
  **8 条失败**（二分：#41 @ b1af371 同 runner 绿 ⇒ M7-R2 波次引入的 Linux 侧回归）⇒ 按任务书
  「main-full 红 ⇒ 修复推送或停止上报」分支**停止发行**：tag 未移动（保持 b1af371）、release.yml
  未触发、**GitHub Release 仍未创建**；二次移动授权（M7-ADJ-tag-move-2）在案未消耗，待 Linux
  失败修复且 main-full 绿后执行（全链见 [docs/m7-report.md](docs/m7-report.md) §3.9 与
  [docs/m7-evidence/m7r2-release.md](docs/m7-evidence/m7r2-release.md)）
- **R2.1-F7 发行第二班实况（2026-10-13）**：F1 已修上轮 8 条 Linux ctest（main-full #47 linux 绿）
  且 F1–F6 门禁全绿后重走发行链——预检（零 Release 零资产、tag `b1af371` 未动）/ 推送 main
  （`0555947..e1017fe`）/ 本机复核（ci-format `violations=0` 187 文件 + make_winzip 五门禁
  rc=0 含 E-2 冻结行逐字）全绿；但 main-full **#48 红**——linux job 唯一失败 = release-dev 树
  **`ui_smoke` 一条**（二分：#47 同 job 绿 ⇒ R2.1 F2–F4 波次引入的 Linux 侧回归，候选面为
  F4 第 4 波常驻探针/F2-F3 几何改动，推断未实证）⇒ **再次停止发行**：tag 未移动（仍 b1af371）、
  release.yml 未触发、**GitHub Release 仍未创建**；`M7-ADJ-tag-move-2` 授权继续未消耗，待
  ui_smoke 修复且 main-full 全绿后执行（全链见 [docs/m7-report.md](docs/m7-report.md) §3.10 与
  [docs/m7-evidence/m7r21-release.md](docs/m7-evidence/m7r21-release.md)）
- **M7-R2.2-G6 发行第三班实况（2026-10-13）**：G1 已修 Linux `ui_smoke` 且 R2.2 八项门禁
  全绿后重走发行链——预检（零 Release 零资产、tag `b1af371` 未动）/ 推送 main
  （`4addc51..35c4410`）/ 本机复核（ci-format `violations=0` 196 文件 + make_winzip 五门禁
  rc=0 含 E-2 冻结行逐字）全绿；**linux job 全绿达成**（main-full #54，含 release-dev
  `ui_smoke`，11m40s）——但 main-full **#54 总闸红**：唯一失败 = **winzip job 撞
  `timeout-minutes: 20` 硬顶被取消**（build release 14m20s + dev 子集 4m42s 在建时撞顶；
  归因 = 代码量增长：对照 #48 同 job 19m59s 绿、build 步 13m05s → 14m20s）⇒ runbook §6
  「任一 job 撞 20min 被取消 ⇒ 停止发布链」命中，**第三次停止发行**：tag 未移动（仍
  `b1af371`）、release.yml 未触发、**GitHub Release 仍未创建**；`M7-ADJ-tag-move-2` 授权
  继续未消耗。附实测推算：release.yml winzip 与 main-full winzip 构建步逐字同命令，本树
  预计 ≈21–22min > 20min 硬顶 ⇒ 即便放行 tag 移动，publish 亦大概率 skipped（release #13
  同型先例：winzip 20m16s 撞顶、publish skipped 零 Release），移动 = 烧掉唯一一次授权且
  0.3.3 卡死「tag 已动零 Release 无第三次机会」死角（全链见
  [docs/m7-report.md](docs/m7-report.md) §3.11 与
  [docs/m7-evidence/m7r22-release.md](docs/m7-evidence/m7r22-release.md)）
- **发行状态（G6 第三班后，2026-10-13）：待发布**。0.3.3 仍未发行：tag `v0.3.3` 停在
  `b1af371`（零 Release 零资产）、release.yml 未触发、GitHub Release 未创建；`M7-ADJ-tag-move-2`
  二次移动授权在案未消耗。上轮发布条件 ①（main-full linux 含 ui_smoke 绿）**已达成**（#54
  实证）、②（本机门禁全绿）已达成；**新增前置**：③ **release 链 winzip job 预算处置**
  ——该 job 在当前树实测/推算 ≈21–22min > 20min 硬顶（`.github/` 构建步与 main-full 逐字
  同命令），须先经裁定处置（预算重标定 `docs/v0.3.0-design.md` §12.3/§12.6 口径，或 winzip
  job 减负，二者均在本包文件面外）才可动 tag；④ tag 移动按 `M7-ADJ-tag-move-2` 条件执行
  （预检零 Release 零资产 / 原子操作 + `ls-remote` 核对 / **移动后不得再动**，故 ③ 未处置
  前推 tag = 赌 ~15% 成功率烧授权，不做）。「已发行 + CI 产物指纹」终态收口留待真发布后
  （`d049d22` 先例口径不变）
- **R2.3-G7 预算处置后状态（2026-10-13）：待发布**。前置③ **已处置**——winzip 预算工程解
  （M7R23G7-0：产物复用/拆 job，**CI 判据零改动**、`timeout-minutes` 不动、五门禁+E-2+冻结行
  逐步保留）落地，main-full **#58/#59 全绿**实证 per-job ≤20min（winzip job 97s/89s，
  `docs/m7-evidence/m7r23-g7-gates.md`）；**release 链三段（winzip-build / winzip-build-dev /
  winzip-pack）无 tag 轮实测**，预算值 15/7/8 待发行轮首跑按 `docs/m7-evidence/
  release-runbook.md` §4 补读数（不编数）。前置④ tag 移动条件不变（`M7-ADJ-tag-move-2`：
  预检零 Release 零资产 / 原子操作 + `ls-remote` 核对 / 移动后不得再动）。发行状态仍 =
  **待发布**
- **发行终态（M7-R2.3-G10 第四班，2026-10-13）：已发行**（`d049d22` 先例的「已发行 +
  CI 产物指纹」收口即本条；取代上方两条「待发布」状态快照，历史实况条目不变）：
  **`v0.3.3` tag → 发行树 `6b571cb`**（tag 对象 `0aba959`；`M7-ADJ-tag-move-2` 二次移动
  授权就此消耗，条件全满足：预检零 Release 零资产 / 原子 `git push --force origin
  refs/tags/v0.3.3` + `ls-remote` 核对 / 此后不再移动）；GitHub Release **已创建**
  （<https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.3.3>，
  published 2026-10-13T07:19:43Z，4 资产指纹见本节首三条，API `digest` ↔ 下载件
  `SHA256SUMS.txt` 逐字交叉一致）；发布说明 = 本节**之前**的 `[0.3.3]` 正文（release.yml ④
  口径）。发行前核对（实跑）：**main-full #60 十 job 含总闸全绿** @ `6b571cb`（lint /
  linux 全 ctest+金样 / appimage / audit / windows-dev / windows / tidy / winzip / cache-gc /
  gate，整轮 17m11s）+ 本机 `ci-format` violations=0（196 文件）+ `make_winzip --smoke-exe`
  五门禁 rc=0（E-2 冻结行 `UI-SMOKE OK shots=12 pages=3` 逐字）；release 道七 job 全绿
  （appimage 4.6 / winzip-build **14.8** / winzip-build-dev **6.3** / winzip-pack **1.4** /
  probe-appimage 0.7 / probe-winzip 0.2 / publish 0.7 min），前置探测（Release 查重 +
  tag↔版本 + 下载件 `--version` 逐字）全过，关键路径 winzip-build+pack+probe-winzip+publish
  = **17.1min ≤ 20min 硬顶**（winzip-build 14.8 贴近 15min 预算——G7 拆段三段实测读数
  就此补齐 runbook §7.4 挂账项）（全链见 [docs/m7-report.md](docs/m7-report.md) §3.12 与
  [docs/m7-evidence/m7r23-release.md](docs/m7-evidence/m7r23-release.md)）

## [0.3.2] — 2026-10-11

**GPS 地图升级版（M6）**：把「内嵌地图选点」从**能用**升级到**地图 App 水平** —— R1 搜索搜得出、
R2 可切卫星/混合图层、R3 搜索后地图内选点闭环、R4 坐标基准透明可信，另补一批体验优化（R5）。
两条关键口径：① **选点坐标 = WGS-84** —— 高德火星偏移（GCJ-02）已在地理编码边界内部抵消，
**未加密**、可直接写 EXIF（实测量级 ~555 m 的偏移只存在于显示层，写回值经往返断言零漂移）；
② **引擎零改动** —— `src/codecs` / `src/decode` 零 diff，`src/core` 仅 `settings.{h,cpp}` 纯加性
追加 `map_style` / `map_sat_labels` 两键（0 删、老 INI 缺键回落默认、行为零变化），由金样
**20/20** 与全语料 regression **zero diff** 双重背书。版本单源 = `CMakeLists.txt:19` = **0.3.2**。
验收证据与偏差登记见 [docs/m6-report.md](docs/m6-report.md)。

### 新增

**瓦片图层模型 + 地理编码（T1–T3，模型解耦）**

- **图层模型**（`src/mapwidget/layers.{h,cpp}`）：`TileStyle{Road, Satellite, Hybrid}` +
  `TileSource`（**amap / osm / esri** × 瓦片 URL 事实表）；Hybrid = 影像（amap style=6）+
  标注（style=7）**双层同帧**绘制；缓存 key 加图层维度、版权行随有效源单源（D1）
- **地理编码**（`src/mapwidget/geocoder.{h,cpp}`）：amap / nominatim ×
  search / suggest / reverse URL 事实表 + `parse_*`（**出参一律 WGS-84**，amap 分支内部
  GCJ→WGS）；`providers` 降级为薄适配层（**冻结 `providers.h` 签名与 `search_url` 逐字节保留**）；
  新单测 `test_layers` / `test_geocoder` / `test_providers`（URL 事实逐字断言）

**地图控件（T4–T6）**

- 图层切换「矢量 / 卫星 / 混合」（缓存 key 加 layer、`select_source` 回退、版权行随有效源）
- 搜索结果**编号气泡图钉**（实心圆 + 编号，主钉置顶）；**点钉命中**（最近钉 ≤13px 优先于取点）
  + `pin_clicked` 信号（冻结头加性追认 A1）

**GPS 卡搜索与选点闭环（T7–T9）**

- **搜索（App 级）**：联想（输入 200ms 防抖 → 下拉建议）、周边偏置 **`around` 20 条**
  （`accept-language=zh`）、**6 类目分类 chips** 一键搜、**可点结果列表**（名称 / 地址 / 距离）、
  地址回显行（逆地理）；`run_suggest` / `run_reverse` + `suggest_finished` /
  `reverse_finished` / `search_results_ready`（追认 A2）；每类单在途、abort 旧请求、8s 超时、
  错误分类提示（被墙 / 无 Key / 无结果 / 解析失败）
- **选点闭环**：搜索 → 多钉 + 列表 → 点钉 / 点行 → 主钉 + 规则框**回填 WGS-84** → 微调 →
  「地图选点」取中心 → 逆地理确认

**坐标基准透明（T10，R4）**

- GPS 卡坐标显示行「**WGS-84（标准 GPS）**」标签 + Info 矢量图标 + tooltip；十进制 ↔ **DMS**
  切换（只显示 / 复制，**不改写入值**）、复制到剪贴板、坐标框右键菜单；`out_of_china` 盒边缘
  0.5° 带内**精度提示**（tooltip + 状态行）；新 `src/mapwidget/dms.{h,cpp}` +
  `test_dms`（往返 / 格式逐字 / 粗盒边界带）

**图层记忆与地图 UX（T11–T13，R5）**

- **图层记忆**：设置页新「地图」页签；`map_style` / `map_sat_labels` 跨进程持久化
  （`settings.{h,cpp}` 加性追加：默认 = 现行为、老 INI 可读；`amap_key` 脱敏不变）
- 缩放「－ / ＋」、左下**比例尺**、光标 **WGS-84 坐标 chip**（约 100ms 节流）；**大地图对话框**
  （「展开」= **同一只 MapWidget 重挂载**，不新建第二只地图 / 不复制状态；挂回复位 + 尺寸记忆）
  （追认 A3）；`src/ui/map_dialog.{h,cpp}`
- **回归探针**：离线 `--ui-smoke` 新增 `meta-loop`（**选点值 = WGS-84 断言**）/ `map-hud` /
  `meta-map-expand`（14/14）等；冻结末行与截图 12 张不变

**密钥防泄漏纪律（`8071a08`）**

- `.gitignore` 增补 14 行（`.zcode/secrets/`、`amap_key*` 等）—— 高德 Key **不入库**（用户自填，
  设置页密文录入）；全历史 blob 泄漏扫描 0 命中（[docs/m6-report.md](docs/m6-report.md) §6）

### 修复

**「搜索搜不出来」根治（R1）**

- 实测三层根因（[docs/m6-map-upgrade.md](docs/m6-map-upgrade.md) §2.1）：① 默认源 OSM 系大陆
  **不可达**（Nominatim SSL 握手失败）→ 搜索恒报「请求失败」；② 高德服务可达但需 Key；③ 即便
  Nominatim 通，旧事实表（单 `q=` 全文 / `limit=6` / 无中文参数）也不到地图 App 水平
- 搜索管线迁 `Geocoder`：**换用可达的高德 Web 服务 + 参数/编码收口** —— `place/text` 事实表
  （`keywords` 百分号编码、`city=`、`offset=20`、`page=1`、`extensions=all`、`accept-language=zh`，
  URL 事实逐字入测试）；**周边偏置 `around`**（视口中心、半径随 zoom，请求按该源基准转换）→
  **20 条按距离升序**；**0 结果回退全文**搜索；未填 Key 时搜索 / 联想 / 逆地理三路**明确指引、
  零请求**

**残留图钉（选点闭环）**

- **换地图源清残留图钉**：换源即清结果钉与主钉，不留「点不动」的旧钉（旧钉仍画在屏上却不能
  选中 / 回填）；行选与点钉同句反馈

**工具链（`tools/env.py`，发行门禁取证修复）**

- `env.py run` 只靠句柄继承 ⇒ **GUI 子系统**子进程（`photopipeline.exe` 为 `WIN32_EXECUTABLE`）
  的标准句柄槽位不被继承赋值（实测 `GetStdHandle` 全为 NULL）—— `env.py run -- …photopipeline.exe
  --version` 这类**捕获**调用静默 0 字节（应用按既有设计转去挂接父控制台，输出落在控制台）。
  修复 = `run` 显式把本进程 stdin/stdout/stderr 交给子进程（`forward_std_streams()`）：
  管道 / 重定向捕获恢复，控制台直跑所见不变，`--version` 逐字断言（`PhotoPipeline 0.3.2`）复通

### 已知

- **T14 可选项未做**：离线瓦片落盘持久化（现为内存 LRU，断网只保留已浏览区）与多图 GPS 轨迹线
- 免 Key 兜底 Nominatim/OSM 保留但大陆**不可达**（客观被墙）→ 错误分类文案「大陆不可达，请切
  高德」；**BD-09（百度）未引入**（现无百度源）
- 高德 Key 仍为「用户自填」口径（不入库）；免费额度内自用，**超限表现未做压力实测**
- 自动化止于像素 / 几何 / 明暗等价层；真机手感与观感的人工目检未做

## [0.3.1] — 2026-10-10

**GUI 缺陷根治版**：对 0.3.0 做可视化排查（真实平台渲染双主题实拍 + 像素级取证），根治四类
用户可见缺陷，并按「设计要求自绘/令牌化、实现却让平台画」把同类**平台代画存量**一次收口；
同时修正**验收判定口径**（控件几何 → 渲染像素）。**引擎侧零改动**（`src/core` / `src/codecs` /
`src/decode` 未触碰）。排查报告与证据见 [docs/gui-review-0.3.0-r2.md](docs/gui-review-0.3.0-r2.md)。

### 新增

- **下拉唯一构造点**（`src/ui/formkit.{h,cpp}`）：`ComboSpec` / `combo_box()` / `combo_row()`，
  配套自绘 `formkit::Combo`（`icons::ChevronDown` 矢量箭头 8px 居右、**无分格按钮**）。QComboBox
  从 6 个建造点收敛到这一处（`grep "new QComboBox" src/` 归零），样式钩子唯一 = `ppFk="combo"`
- **数值控件自绘**（`formkit::Spin` / `DSpin`）：箭头列改为**盒内一列**（竖发丝线 + 横发丝线 +
  矢量 ▲▼），对齐设计稿 `.spin .ar`，取代平台的独立描边按钮格
- **平台指示器矢量化**（收在应用级 `QProxyStyle`，形只有一份走 `icons` 库）：树展开符
  （`QTreeView::branch`）/ 菜单勾选（`QMenu::indicator`）/ 菜单钮下拉三角（`PE_IndicatorArrowDown`）
- **GUI-R2 五条回归探针**（`smoke_probe_gui_r2()` + `tests/ui_smoke.py` 的 `GUI_R2_LINE_PREFIXES`，
  只增、冻结末行不变、截图仍 12 张）：`combo-consistency` · `format-card-align` · `dialog-close` ·
  `run-guard` · `render-tokens`（**渲染像素**口径：箭头列无贯穿对比线、树展开符形态、
  勾选语义只属勾选列、令牌 QSS 在场性）

### 修复

**下拉（问题 1 + 5，同一根因）**

- 亮色下 `ParamForm` 建造的下拉（技术 / 后端 / 枚举参数）**整只不绘制** —— 未挂样式钩子 →
  不匹配任何 QSS 规则；实测非背景像素 `0/19604` → 修复后 `6331/19604`
- 同一功能控件**四种高度**（24 / 30 / 18 / 22）与三套属性名（`ppField` / `ppMetaField` / 无）→
  统一为 `ppFk="combo"` + 高 24 单源；退役 `page_qss` / `page_meta` / `dialogkit` 三份重复规则
- 下拉箭头是 **Win32 经典分格按钮**（竖分隔 + 描边）→ 改矢量 chevron（`::drop-down` 只留箭头位、
  `::down-arrow` 置空）；`Metrics::combo_arrow_px = 8` 终于有了消费点

**格式参数（问题 2）**

- 「＋ 添加格式…」与页签带**同高同色错位 14px、中间有缝** —— 两条 29px 灰带是 `QTabBar` /
  corner 未透明、吃了 palette 的 `--ctl` 底（设计稿该行是白底 + 底部发丝线 + 文字链，无色带）。
  透明化后色带消除；按钮**垂直居中**到页签带（按钮高 26 是 A7，不随带拉伸）

**弹窗（问题 3）**

- 设置 / 元数据编辑器 / 预设管理 三个弹窗标题栏 **✕ 点了没反应** —— caption 关闭钮只创建、
  从未 `connect`（`make_title_bar` 只建按钮；主窗那枚是显式接的，弹窗三处漏接）。
  新增共享接线 `dialogkit::bind_close_button`（幂等），三处各调一次

**文件列表（用户复审新增）**

- 选中文件后**出现第二枚勾选框**（大半被缩略图挡住、选中高亮后更显眼）—— `flags()` 对所有列返回
  `ItemIsUserCheckable`，而内容列的绘制没清 `HasCheckIndicator`（勾选列分支清了、内容列漏了）。
  勾选语义改为**只属勾选列** + 委托任何列都不画样式勾选指示
- ＋/−/筛选 工具钮多出**下拉箭头**（设计稿是纯图标钮，只有分组下拉带箭头）—— 箭头是 ＋/筛选
  挂菜单后平台自己画的。`QToolButton::menu-indicator` 置空（菜单功能保留、语义由 tooltip 承担）

**运行（问题 4，防御）**

- 「开始运行」在运行期存在**可点窗口期**（只靠 `refresh_status` 的 `setEnabled` 落地），
  重入会弹「正在运行」模态 → `lock_for_run` 开头立即禁用

**平台代画存量（举一反三，10 项一次收口）**

- 滚动条（评审 1-50 点过名，此前零 QSS 规则）→ 令牌化细轨 + 圆角滑块 + 隐藏两端箭头钮
- 下拉**弹出列表** → 令牌化（`--card` 底 + `--acc-dim` 选中）
- 右键/上下文菜单（18 处）→ 令牌化（底/边/圆角/选中/分隔线）
- 报错/询问框（38 处，含 `on_start` 六闸门）→ 令牌化（底/标签/按钮）
- 提示框（`QToolTip`）→ 令牌化（`--card-hi` 底 + `--txt` 字）
- 分组盒（EXIF 编辑器 3 处）→ 扁平化，消除双层框线

### 已知问题

- **「点击运行概率性错误」未复现**：脚本化走查 5/5 全绿、`run-guard` 两轮重入 0 模态 ⇒ 不在脚本化
  路径上，需真实交互时序复现后定位。本轮只做了防御（立即禁用开始钮）+ 值守探针（模态看门狗）
- `QMessageBox` 只做了令牌化（颜色/字体/按钮），**未换 dialogkit 自绘标题栏** —— 38 处调用面换壳
  属功能改动，待裁定
- 工具钮去箭头后，带菜单的 ＋/筛选 不再有可点提示，语义全靠 tooltip（与设计稿一致；若要严格按稿
  把 ＋ 改成「添加文件」单一动作，属功能改动）
- `QFileDialog` 仍为原生对话框（平台惯例，设计上允许）；树展开符不画连接线（设计稿的树没有）

### 发行

- Windows：`PhotoPipeline-0.3.1-win64.zip` —— **33,551,147 B**（sha256 `e372f5a8…`），
  `make_winzip.py` 五项门禁（A 闭包 / B 来源 / C 结构 / D 许可 / E 启动含 offscreen `--ui-smoke`）
- Linux：`PhotoPipeline-0.3.1-x86_64.AppImage` **51,853,816 B**（sha256 `245645f8…`）+
  `PhotoPipeline-0.3.1-deps.txt` **2,439 B**（sha256 `e8c80b3d…`，`tools/appimage-check-closure.sh`
  闭包清单）
- 第 4 附件：`PhotoPipeline-0.3.1-SHA256SUMS.txt` **293 B**（三个发布件的 sha256，逐字取自下载件）
- **已发行**（2026-10-10）：`v0.3.1` tag 指向发行树 `16f0a08`，GitHub Release 含 4 资产；
  发布说明 = 本节**之前**的 `[0.3.1]` 正文（release.yml ④ 口径，止于 `### 发行`）。
  发行前核对：`main-full` 九 job 含总闸全绿 @ `16f0a08`（lint / appimage / **linux 全 ctest+金样** /
  winzip / **windows 全 ctest+金样** / audit / tidy / cache-gc / gate）；本机门禁七项全绿。
  release 道五 job 全绿（appimage 3.2 / winzip **17.7** / probe-appimage 0.6 / probe-winzip 0.2 /
  publish 0.8 min），前置探测（Release 查重 + tag↔版本 + 产物 `--version` 逐字）全过；
  关键路径 winzip+probe-winzip+publish = **18.7 min ≤ 20min 硬顶**（winzip 17.7 仍超 §12.3 的
  15 min job 预算 —— 硬顶未破，与 0.3.0 同一已登记偏差，重标预算属设计文档口径，待裁定）

## [0.3.0] — 2026-09-24

**多格式批处理工作台**：在 0.2.0（Linux + Windows 双平台）基础上完成 M4 的九项需求——
输入预览/分类/勾选、生效值元数据、多格式输出、逐文件进度、自适应并行、GUI 重构（无边框三栏）、
CI 工业化（快慢道四流水线）、依赖一把全升、AVX2 性能基线。工具链 = MSVC（VS 2022/2026 Build Tools）
+ vcpkg（`x64-windows-avx2` 自定义 triplet，继承 `x64-windows` 动态链接形态）+ **Qt 6.11.2**
（`msvc2022_64`）；ISA 基线 = **AVX2 / x86-64-v3**（调优目标 i7-14700K）。

> **M5 折入（2026-10-10 收口）**：本版同时折入 **M5「GUI 刷新 v6」**——视觉体系按
> [docs/qt-gui-design-spec.md](docs/qt-gui-design-spec.md)（v6 单源规范）与视觉终稿
> [docs/mockups-v2/](docs/mockups-v2/) 全量重做（**版本号不变**，用户已确认口径）。引擎侧零改动
> （`src/core`/`src/codecs`/`src/decode` 未触碰），只做视觉与排版；用户可见变化见下方
> 「GUI 刷新 v6（M5 折入）」节，验收证据与偏差登记见 [docs/m5-report.md](docs/m5-report.md)。

### 新增

**GUI 刷新 v6（M5 折入）**

- **视觉体系 v6（令牌单源）**：全界面颜色/字阶/间距/圆角与控件度量收口到 `src/ui/theme.h`
  唯一令牌源（明暗 = 换令牌重建 QSS；暗色卡片无投影、画布底 `#16181d` 不随主题变）；页内行节奏
  统一 **pitch 28px**
- **表单体系（`src/ui/formkit.{h,cpp}`）**：行构造器在构造层强制规格 —— 标签轨 80px 右对齐、
  列距 12、**字段宽只有 88 / 160 / flex 三档**、单位 16px 槽、范围 10px 注释；页面不再手摆栅格
- **图标矢量化（`src/ui/icons.{h,cpp}`）**：24×24 网格、1.5px 等线宽、圆角端点的 QPainterPath
  矢量图标（`assets/icons/*.svg` 为设计源入库）；**全界面 Unicode 字形充当图标已清零**（含改值标记、
  状态徽标、弹窗套件），每枚图标带 tooltip 与 accessibleName
- **无边框窗口收口**：caption 三钮 **46×32 单轨**（可见面 = 命中区），关闭悬停 `#c42b1c`，
  最大化钮保留 `HTMAXBUTTON`（Win11 Snap），八向 6px 缩放命中几何不变
- **壳层**：顶栏 44px 三段式（品牌 / 一体式分段导航 92×30 / 工具 + caption）、状态栏 30px 四档
  （新增「剩余磁盘」读数段）；**栏宽固定轴 左 330 / 右 587**（切页列不错位）；步骤导航**无 ✓ 等完成态装饰**
- **明暗主题**：亮暗两主题**布局等价**（`--ui-smoke` 逐一比对 **988 个 `pp-*` 控件**的几何与可见性
  逐项一致，仅令牌不同）；**主题可手动切换并跨进程持久化** —— 设置·通用 三档
  「跟随系统｜浅色｜深色」（`AppSettings.ui_theme_mode`），优先级 = `PP_UI_THEME` 强制档 >
  持久化档 > 跟随系统
- **文件列表**：行高 **58px**（勾选 13px + 46px 缩略图 + 2–3 行信息不截断 + 右侧状态胶囊）；
  工具行常驻（搜索 / 添加 / 移除 / 筛选 / 分组）+ 空态投放区；**分类面板改 2 列网格**
  （色点 + 名 + 计数胶囊）；分类筛选新增「仅显示已勾选 / 仅显示有例外 + 清除筛选」
  （只影响列表可见性，不动勾选 / 输出 / 分类数据）
- **预览画布**：`#16181d` 舞台（明暗同值）、矢量图标空态 + 引导语、缩放控件组
  「− / 100% / ＋ / 适应 / 1:1」、翻图钮 28px、角标空态隐藏、小图最近邻、letterbox 照片衬垫
- **三页 v6 化**：① 元数据页四卡组（关键信息 kv 双列 5 行 / 时间偏移 3×spin88 + 生效对照 /
  GPS 双列轨 + 地图 + 操作行 / 隐私与文件时间）；② 输出页四卡组（格式磁贴 2×4 零形变 /
  全局一行一义 / 格式参数「一行一义」六行 + 折叠区 / 预设与预估）；③ 运行页三卡组
  （总览合成条 + 指标单源 kv 八项 + 操作行三级动作 / 任务表分组行 + 逐输出行 tabular /
  日志尾等宽真实尾部 4 行）
- **设置弹窗重做**：660px、标题栏 40px、下划线页签 **通用｜输出｜运行｜地图｜关于**、
  行 pitch 28 / 标签 80 / 字段 160、底栏 48px（恢复默认居左 / 实心确定居右）；EXIF 编辑器与
  预设弹窗同语言（零字形改值标记）
- **语义与措辞统一**：线程预算值全局同词「**自动（N 核）**」；状态栏「已选 N / M」不被预检警告
  吞掉；运行页「峰值内存」显示**真实读数**（core 末尾追加 `RunSummary::peak_bytes`，
  日志行与金样零改动）

**预览、分类与勾选（需求 1）**

- **常驻输入预览面板**（中栏）：点选即出图（`decode_preview(path, 2048)`，内嵌预览优先、无内嵌
  则全解码后 LANCZOS3 降采样），独立低并发后台池（≤2） + 16 张 LRU；缩放「适应 | 1:1」、
  ←/→ 翻图、徽标「输入 · 未修改像素」；**只预览输入，不预览输出**（负面清单松绑范围）
- **手动分类**：`ClassRegistry` 命名分类（颜色点 + 热键 0–9）、计数、预览底部热键提示动态生成；
  持久化 `data_dir()/classes.json`（键 = 规范化绝对路径；Windows 大小写折叠）
- **自动分组视图**：QTreeView 节头（月份 / 相机型号 / 源格式 + 无分组），节头带计数
- **文件列表勾选**：`CheckState` role ⇒ 勾选 = 参与运行 = 批量规则作用范围；搜索过滤与勾选正交；
  分类行右键「全选该类/反选该类/清空勾选」

**生效值元数据（需求 2）**

- `metadata::preview_effective()`：**与写路径 `build_plan()` 同源的纯函数**（杜绝显示/写入两张皮）
- 元数据页新增**关键信息卡**（拍摄时间/相机/镜头/曝光/尺寸·位深·通道/色彩空间/文件/ GPS）与
  **时间「原 → 生效」对照行**（删除线原值 + 强调色生效值 + 来源字段标注；未修改也常显）；
  多选（≥2）→ 逐行小表（上限 8 行 + 「…还有 N 个」）
- GPS 卡显示**生效坐标**、地图跟随选中文件 `center_on` 生效值单钉（D4：不画新旧对比）；
  改动值统一 `.mod` 样式（强调边框 + 强调色字）

**多格式输出（需求 3）**

- **1 输入 → N 格式输出，解码仅一次**：`OutputTarget` 多目标化、`RunConfig.outputs[]`、
  色彩变换与元数据 plan 全局共享一次；BMP 丢元数据与灰度/alpha 告警语义沿用 0.2
- **输出路径模板**（`$format`/`$dir`/`$file`/`$name`/`$ext` 可混排；未知符号/`..`/绝对路径
  = 校验错误并阻断开始运行；`$dir` 空段坍缩）；「按文件格式分文件夹」单行开关与模板 `$format` 段联动
- **逐输出独立冲突解析**：`reserved` 按每个 `out_path` 登记；一源多输出撞同一 desired 时第 2..N 个
  走 rename，不再互相覆盖
- flatten 编排：alpha-preserving 目标先编码，之后在工作缓冲**原地**拍平（峰值内存仍是 2×frame）

**逐文件进度（需求 4）**

- 三级进度模型（运行级 / 源文件级聚合 / 输出级）+ 阶段权重表（probe 3% · decode 27% ·
  orient+color 10% · encode 50% · 落盘 10%）
- **真实行级进度**：JPEG（jpegli 写扫描行）/ JXL（编码回调）/ PNG / TIFF（OIIO 写行）；
  **合成进度**：WebP / HEIF / AVIF（编码器无回调）→ 按尺寸估算时长的缓出推进 + **斜纹条标识**
  （`synthetic=true`，完成跳 1.0，不倒退）
- 运行页：源文件分组行 + 逐输出子行（进度条 + 百分比 + 阶段文字 + 产物事实/失败原因）、
  总览（吞吐/并行/交错/线程预算读数 + 取消唯一入口）、日志尾卡；运行期锁定（步骤 1/2 只读）
- `--dev` 新增进度侧车（`progress-trace.jsonl`）供金样断言事件流

**自适应并行（需求 5）**

- **交错启动**：全局限速器 `next_start = max(now, last_start + stagger_ms)`（0–2000ms 可配置，
  0=立即）；取消路径立即唤醒等待者；日志记每文件启动偏移
- **E3 自适应线程预算**：纯函数 `alloc_threads(T, remaining, inflight)` —— 队列深则多文件并行 +
  编码器单线程，队列浅则收缩并行、放大 `encode_threads`（≤16）；契约 = 任一时刻「活跃编码线程 +
  活跃 worker 主线程 ≤ T」；线程映射义务落到各库 API（jxl runner / heif·avif 插件 threads /
  webp thread_level；jpegli·OIIO 无内部线程，如实记录）

**GUI 重构（需求 6）**

- **无边框窗口 + 自绘 caption 三钮**（`platform/frameless`：`WM_NCHITTEST` HTCAPTION 拖拽 /
  八向边缘缩放 / 最大化钮 HTMAXBUTTON 触发 Win11 Snap；命中区 46×32、hover 面满顶栏 52px、
  关闭悬停 `#c42b1c`；Linux 走 `startSystemMove/Resize`）；Mica + 深色标题栏随系统主题
- 三栏骨架（左 258px / 中 1.0 / 右 1.119，QSplitters 可持久化）+ 三步内容区（元数据/输出/运行）、
  底栏摘要 + 开始运行；`ui/theme` tokens 单源，明暗双主题
- 输出页：8 格式复选磁贴 + 计数、逐格式页签（位深 + ParamForm）、一行一字段统一标签轴、
  全界面禁折行（省略号 + 悬浮全文）；设置对话框追加交错启动/线程预算/分文件夹默认

**CI 工业化（需求 7）**

- 四道流水线：**pr-fast**（fmt 门禁 + 双平台 build/unit + 金样子集）· **main-full**（lint + tidy 棘轮
  → 双平台全量 → appimage/winzip → 五门禁 + 烟测 → audit/cache-gc → gate）·
  **nightly**（asan/ubsan/tsan 三路并行 + 回归基线 diff（含 Windows 基线）+ 许可/SPDX/依赖审计）·
  **release**（tag v*：appimage ∥ winzip → **probe-appimage / probe-winzip 双探测** → publish）
- 硬约束「每 job 与整轮 wall-clock 双 ≤20min」写入每 job `timeout-minutes: 20`（唯一豁免 =
  `warm-cache.yml` 播种）；发布链路前置探测 = `gh release view` 查重 + 产物 `--version` 断言
- clang-format 硬门禁（全源 0 diff）+ clang-tidy 棘轮（基线入库，告警数 ≤ 基线且新增代码 0 告警）

**依赖全升（需求 8）**

| 库 | 0.2.0 | 0.3.0 |
|---|---|---|
| Qt (aqt) | 6.8.3 | **6.11.2** |
| OpenImageIO | 3.1.14.0 | **3.1.17.0** |
| libjxl | 0.11.2 | **0.12.0**（修 box 追加的上游 bug，正落我们的 JXL 元数据路径） |
| libheif | 1.23.1 | **1.23.5** |
| exiv2 | 0.28.8 | **0.28.9**（顺删已弃用且无替代的 `enableBMFF()` 调用） |
| x265 | 4.2 | **4.3** |
| SVT-AV1 | 4.1.0 | **4.2.0** |
| aom | 3.13.3 | **3.15.1** |
| libde265 | 1.1.1 | **1.1.3**（CVE 累积必升） |
| libwebp / lcms / spdlog / zstd / libpng / giflib / openexr / highway 等 | 现状 | 对齐 vcpkg master 可得最新（实测：libwebp 1.6.0 · lcms 2.19.1 · spdlog 1.17.0 · zstd 1.5.7 · libpng 1.6.58 · giflib 6.1.3 · openexr 3.4.13 · highway 1.4.0） |
| CMake | 3.28 | 兼容线 **3.28...4.4**（CI 装 4.4.x） |

- **停留（白纸黑字）**：OIIO 3.2（未 GA）· exiv2 main/0.29-dev · jpegli 保持钉 commit `031a0077` ·
  SVT-AV1 不跨 4.x · Qt 不回 6.8；vcpkg baseline tag 保持 `2026.07.29`（超前版本走 overlay port）

**性能与 SIMD（需求 9）**

- **AVX2 / x86-64-v3 基线**：自研 targets `/arch:AVX2 /GL /LTCG`（MSVC）或
  `-march=x86-64-v3 -mtune=raptorlake|haswell` + LTO（GCC/Clang）；依赖侧**不注入 ISA/LTO flags**
  （见"修复"）；OIIO overlay 显式 `-DUSE_SIMD=avx2`、libwebp `WEBP_ENABLE_SIMD=ON`
- **5 对自研热路径**（`src/core/simd/`，各带 `*_avx2` / `*_ref` 同签名 + 一致性单测）：
  flatten（alpha 合成）/ quantize8·16（float→int 舍入）/ transpose8（8×8 分块转置）/
  downscale（LANCZOS3 水平+垂直两遍）/ interleave（通道展开）
- 基准 `--dev bench` 三场景实测（详见 `docs/m4-report.md` §7）：多格式（jpeg+webp）总耗时
  **0.932×** 两次单格式之和（解码共享生效，判据 ≤1.35×）；flatten 微基准 4.08x@256² → 1.54x@4096²

### 破坏性变更

- **接口一次性解冻 + 再冻结（D20）**：`EncodeRequest`（`target` 取代 `params/outBitDepth/outPath`）、
  `RunConfig`（`format/backend/tech/params/out_bitdepth` → `outputs[]` + 模板/交错/线程预算）、
  `Scheduler` 事件（`FileEvent.progress`）、`PresetData`（schema v1 → v2）均为**结构重排**，
  不再保证聚合初始化兼容；相关冻结头已重新标注 `PP-FROZEN(0.3.0)`
- **预设格式 v1 → v2**：v2 = `outputs: [OutputFormatSpec]` 数组 + `output_template` /
  `split_by_format`；0.2 的单格式 JSON 由 `migrate_preset_v1()` **零丢失**迁移（R31：含
  `outputs[i].lossless` 显式键，避免 png/tiff/bmp 等"技术本身无损"的格式经
  load→save→load 静默回退 false）
- **线程契约 E2 → E3**：0.2 的「编码器内部线程全部关闭」解冻为「按自适应预算分配，总活跃线程 ≤ T」
- **`--dev` CLI**：`--format/--backend/--tech` 单输出参数保留，新增 `--outputs`（多目标）、
  `--template`、`--stagger`、`--thread-budget` 与 `bench` 子场景
- **AVX2 为硬基线**：不再要求 pre-AVX2 CPU 兼容（x86-64-v3 产物）

### 修复

- **色彩回拷黑图（静默数据损失）**：非 `keep` 色彩目标下，3/4 通道源经 `ColorManager` **原地**写回
  工作缓冲后又被"回拷"一次 —— 同址读写把刚变换好的像素清零，产出**全黑图且 `ok=true`、无告警**。
  修复判据 = "当前缓冲的本地像素地址是否仍是工作缓冲的存储"（同址不回拷，灰度升维换缓冲才回拷）；
  配套像素级回归断言（`test_pipeline_contract`）。证据与逐条根因见 `docs/m4-report.md` §5.1
- **UI 偶发访问违例（D-W3-1 悬垂指针）**：`page_output.cpp` 的 `tech_by_id()` 返回**入参容器内**的
  指针，而 `effective_backends()` 按值返回 → 临时对象在语句结束即亡，指针悬垂
  （ASAN: `heap-use-after-free @ page_output.cpp:1211`）。修复 = 先绑定具名局部再取指针
  （同类点 3 处一并收敛）。证据见 `docs/m4-report.md` §5.2
- **依赖侧全局 ISA flag 的真回归**：`/arch:AVX2` 经 triplet 注入会让 **libwebp 1.6.0 无损解码崩溃**
  （`0xC0000005`；最小复现 = 仅链 libwebp 的探针，40 个依赖 DLL 回填扫描唯一触发者 = `libwebp.dll`）
  —— 根因是该库的 AVX2 内核必须**逐文件**加 flag，全局 flag 破坏其运行时 CPU 派发模型。裁定：
  依赖侧零 ISA/LTO 注入，AVX2 走各库自身机制（等价物一个不丢）
- **`/GL` 与依赖端口构建的机制冲突**：aom 端口 `WINDOWS_EXPORT_ALL_SYMBOLS` 走 `cmake -E __create_def`
  读不了 IL 对象 → LTO 只作用于自研 targets
- **HEIF/AVIF 4 通道入参错位（T19 引入，全语料回归暴露）**：逐行取像素后一律按 `x*3+c` 取 RGB，
  而步长是 `nch` ⇒ RGBA 源（或灰度升维）逐像素错位；修复 = 展开条件 `nch < 3` → `nch != 3`
  （1/2/4 通道统一经 `pp::simd::interleave` 展成 3 通道，alpha 按原 stride 取）；修复后 heif 产物
  逐字节复现基线、解码回源逐位一致
- **打包产物缺陷（T18 发现）**：便携模式把 `classes.json` / `presets` / `logs` 写进 staging →
  zip 内会带上**本机**的分类归属；改为 E-1/E-2 烟测前后 staging 快照比对 + 还原 + 硬断言
- **测试/工具二进制崩溃与断言失真的历史坑（M3 延续）**：日志 CRLF、`capture_stderr` Windows 空存根、
  缺 UTF-8 清单导致 ACP 不匹配等，均在 M3 修复，0.3.0 沿用其回归防线

### 已知问题

- **CI 四道首跑（M5 实测）**：`pr-fast` / `main-full` / `nightly` 三道士 `e932abf` 全绿
  （wall **12m2s / 18m2s / 4m23s**，均 ≤20min 硬顶；main-full 的 `appimage` 与 `winzip` 两个
  打包 job 亦绿）；**`release` 道未触发**（`on: push: tags: ['v*']`，本版 tag 尚未推进）。
  首次实测发现 **`windows`（17.0 min）与 `winzip`（17.1 min）两个 job 超出 §12.3 单 job 预算
  15min 约 2min**（整轮 17.6 min ≤ 20min 硬顶未破，故未改任何时限）—— 处置见
  [docs/m5-report.md](docs/m5-report.md) §4.1 第 1 条与 §5 未决项 2
- **预览 250ms 判据**：内嵌预览命中路径满足；**无内嵌预览的 48MP 输入**实测 535.1 ms
  （含一次全解码，`--dev bench` 场景 `preview48`）——为如实记录，不改判据也不改口径
- **Windows 回归基线随版本号漂移**：`tools/baseline/golden.windows.log` 含 `version=app` 行
  （归一化规则不覆盖版本串），版本提升后需按 `regression.py --update` 重生成（本版已重生成，
  见 `docs/m4-report.md` §8.5）
- **macOS 未构建/未发行**（承 0.2.0）；Linux 侧 0.3.0 的完整验证（ctest/金样/ui_smoke/appimage）
  由 CI 与 Linux 机执行——本机为 Windows
- **zip / AppImage 不承诺字节可复现**（条目 mtime / squashfs 超级块时间戳参与，承 M2/M3 结论）
- `AVIF 10-bit + alpha 不可用`（SVT-AV1 上游缺陷，编码前明确报错并建议 libaom）、
  非 ASCII 交互控制台代码页、第三方 sRGB→sRGB ≤1.8e-4 暗部偏差等 0.2.0 遗留项**语义未变**

### 发行

- Windows：`PhotoPipeline-0.3.0-win64.zip` —— **M5 打包轮本机五门禁全过**（A 闭包 / B 来源 /
  C 结构 / D 许可 / E 启动含 offscreen `--ui-smoke`），指纹 **34,602,328 B / 104 条目**
  （sha256 随条目 mtime 浮动，逐次登记）；CI `winzip` job 同轮产物 **33,538,488 B / 104 条目**
  （sha256 `b555ff7e…`）。两者**条目面逐一相同**，二进制面因本机与 runner 编译环境不同而不同
- Linux：`PhotoPipeline-0.3.0-x86_64.AppImage` **51,837,432 B**（sha256 `c79b6b8a…`），由
  `main-full` 的 `appimage` job 构建；`tools/appimage-check-closure.sh` 结果 = 受检 **36 个 ELF**、
  FAIL-A/FAIL-B 均无、目标机系统 soname **45 条**（清单 = 产物旁 `PhotoPipeline-0.3.0-deps.txt`）
- **已发行**（M5-T16，2026-10-10）：`v0.3.0` tag 指向发行树 `50317d0`，GitHub Release
  含 4 资产（win64.zip / AppImage / deps.txt / SHA256SUMS.txt），前置探测（Release 查重 +
  产物 `--version` 断言）全过，release 道整轮 17.7 min ≤ 20min —— 口径与证据见
  [docs/m5-report.md](docs/m5-report.md) §1.5

## [0.2.0] — 2026-09-23

首个 **Windows 版本（x64）**：在 0.1.0（Linux x86_64 / AppImage）基础上完成 M3 的 Windows 移植与
打包自动化，形成 **Linux + Windows 双平台**产物。工具链 = MSVC（VS 2022/2026 Build Tools）+ vcpkg
（`x64-windows` 三元组，**动态**链接）+ Qt 6.8.3（`msvc2022_64`）。

### 新增

**平台与移植**

- Windows 支持（x64，**Windows 10 1903+ / Windows 11**；1903 是 UTF-8 代码页应用清单的下限）：
  三页主窗口、内嵌地图、缩略图、设置/预设、`--dev` harness 与 `--ui-smoke` 脚本化走查全部可用
- **全局 UTF-8**：可执行文件嵌入应用清单（`activeCodePage=UTF-8`）+ 全量 `/utf-8` 编译 ⇒
  进程 ACP = UTF-8，与 Linux 的 UTF-8 字节语义对齐（此前 ACP 跟随系统区域，非 ASCII 语料
  `tests/golden/base/edge/测试📸unicode.png` 经宽→窄转换失败会 abort）
- **Mica 深色标题栏**与系统主题跟随（`src/platform/mica`；不可用时优雅降级为常规标题栏）

**打包与发行**

- **Windows 打包后端 `tools/make_winzip.py`**（stdlib-only，与 AppImage 后端四项烟测同口径）：
  顶层单一目录 `PhotoPipeline/` 的 `PhotoPipeline-<版本>-win64.zip`（解压即用）；app-local
  **VC 运行时**（`Microsoft.VC*.CRT` 整目录，不依赖目标机装 VC redist）；五项门禁 ——
  A 闭包（PE 导入表逐名三判据 + `MUST_BUNDLE` 前缀，见下）· B 来源（`Qt6*.dll` 与 Qt 工具链
  sha256 逐一一致）· C 结构（exe + `platforms/{qwindows,qoffscreen}.dll` + `imageformats/` + `tls/`）·
  D 许可（`licenses/*/copyright` ≥30，写盘前与 zip 内各一次）· E 启动（staging `--version` 冻结行；
  `--smoke-exe` 时追加 offscreen `--ui-smoke` 走查）；并剔除 `vc_redist.x64.exe` 与
  `dxcompiler.dll`/`dxil.dll`（D3D12 RHI 着色器编译器，本应用 QWidget/QPainter 路径不用）
- 第三方许可随附：`tools/collect_licenses.py` 汇总 vcpkg port 许可文本（新增 `--dest` 以适配
  Windows 布局 `licenses/`；缺省 `<APPDIR>/usr/share/licenses` 逐字未变）
- CI（`build-test.yml`）新增 **`windows`**（release ctest 23 + release-dev 子集金样 16）与
  **`winzip`**（打包 + `PhotoPipeline-win64` artifact）两个 job；Windows 缓存键前缀 `vcpkgwin-`、
  Qt 键 `qt-6.8.3-win64_msvc2022_64-*`，与 Linux 侧严格分离；`warm-cache.yml` 新增 `warm-windows` 播种
- 新增 **tag 触发发行自动化** `.github/workflows/release.yml`（`on: push: tags: ['v*']`）：
  Linux AppImage + Windows zip 并行构建并上传 artifact，`publish` job 做 **tag↔版本一致性断言**
  后 `gh release create`（tag 已存在同名 release 时失败，不静默覆盖）；日常 CI 不再被 tag 重复触发

**质量设施**

- 金样 driver / UI 冒烟 / 许可汇总 / 环境注入 / 语料生成 / 回归 / 打包等**共享脚本统一为 Python
  单实现**（删除 bash 版），冻结行与退出码逐字不变
- 测试与工具二进制同样嵌入 UTF-8 应用清单（与主程序一致），并补 SPDX 标注（14 个 PP-FROZEN 头文件）

### 修复

- **控制台输出可见性**：GUI 子系统进程的 std 句柄判据由"句柄是否有效"改为"**是否捕获型**
  （`GetFileType ∈ {PIPE, DISK}`）"——捕获型（ctest / `subprocess` / shell 重定向）原样保留，
  不捕获时挂接父控制台；控制台输出代码页只在**自己**挂接成功时改写（不污染父 shell 共享控制台）
- **日志文件行尾统一 LF**（spdlog formatter 显式 `eol="\n"`）；`stdout`/`stderr` 仍为平台文本模式
  （Windows CRLF），脚本匹配器一律 `rstrip('\r\n')`
- `capture_stderr` 的 Windows 实现（`_dup`/`_dup2`/`_close` + 二进制模式捕获文件），此前 Windows
  分支是空存根（恒返回空串，4 项 stderr 断言失去意义）
- `file_clock` 转换改为**编译期探测**（`src/core/filetime.h`：`requires` 在实例化期择
  `from_sys` / `from_utc`，避免绑定某个 STL 版本）；本机 MSVC 14.51 走 `from_utc`
- 预设读取改用 `_wfopen`（`fs::path::c_str()` 在 Windows 为 `wchar_t*`）；正确性不再依赖 ANSI 代码页
- WebP 的 ICCP mux 符号显式接线（`PkgConfig::WEBPMUX`）：`pp_core` 经 whole-archive 自注册，
  mux 符号对所有消费者必达，而 Windows 动态 pkg-config 不携带该传递依赖
- POSIX-only 判据平台化：`noread` fixture（Windows 下 `fs::perms::none` 不禁止读取）、
  `test_presets` 的**非法 UTF-8 原始字节路径**用例（Windows 路径为 UTF-16，不可表示）
- 测试侧 POSIX 垫层 `tests/unit/env_compat.h`（`setenv`/`unsetenv`/`getpid`）；空值环境变量语义
  经探针实证（UCRT 下"存在但为空"不可表达）
- 三个测试二进制启动即 abort（`0xc0000409`）的根因 = 缺 UTF-8 清单导致 ACP 不匹配（见"新增"）

### 已知问题

- **Windows 产物未字节可复现**：zip 条目内嵌文件 mtime ⇒ 同源两轮 sha256 必然不同
  （与 0.1.0 的 AppImage 同类，已接受）；仅承诺**结构一致 + 依赖清单一致**
- **无 Windows 回归基线**：`tools/baseline/golden.log` 是 Linux 侧基线（其内 `version=0.1.0`），
  Windows 侧尚无 `golden.windows.log`；`tools/regression.py` 未在 Windows 上接入 CI
- **非 ASCII 交互控制台**：中文控制台代码页下 `--version` 等输出仍按平台文本模式，未强制 UTF-8
  控制台代码页（管道/重定向场景为 UTF-8 字节）
- **AVIF 10-bit + alpha 不可用**（承 0.1.0，SVT-AV1 上游缺陷，编码前明确报错并建议 libaom）
- 部分文档/工具仍含 `0.1.0` 字面量：`tools/baseline/golden.log`（Linux 基线，历史值）、
  `tools/appimage-gui-smoke.sh` 默认文件名、`tools/tls_probe.cpp` 的 User-Agent 字符串、
  `README.md` 的 v0.1.0 AppImage 发行引用（历史发行件，如实保留）
- Linux 侧行为与 0.1.0 一致（本版对 Linux 的影响仅限共享脚本 Python 化与日志行尾/句柄判据的
  等价重构；ctest 23 + 金样 16 在 Windows 侧复跑通过，Linux CI 判定由发行轮执行）

### 发行

- Windows：`PhotoPipeline-0.2.0-win64.zip`（本机打包件；**release 上传由主对话执行**）
- Linux：`PhotoPipeline-0.2.0-x86_64.AppImage`（由 `release.yml` 的 `appimage` job 构建）

## [0.1.0] — 2026-09-20

首个发行版本：**Linux x86_64 单平台**，单一产物 AppImage。M0（骨架/冻结接口/语料）→ M1a（引擎）
→ M1b（界面）→ M2（debug 收口 + 发行状态）后达到发行状态。

### 新增

**引擎**

- 8 种编码格式：JPEG（jpegli）、JXL（libjxl）、PNG（libpng）、TIFF（libtiff）、WebP（libwebp）、
  BMP、HEIF（libheif / x265）、AVIF（SVT-AV1 / libaom）；位深可选值 = 静态能力集 ∩ 运行期探测，
  越界请求给出明确错误（不静默降档）
- 色彩管理（lcms2）：色彩目标 keep / srgb / p3 / adobergb；M2 新增无 ICC 的 JXL 源的 CICP 最小映射
  （sRGB / Display P3 / BT.2020，未支持组合维持 sRGB 并记日志）
- 元数据手术（Exiv2）：EXIF / XMP / GPS 读写与无损重写；"仅元数据"模式对 JPEG / PNG / TIFF / WebP
  保持字节级保真（零重编码）
- 调度器：像素预算记账、可取消（取消幂等）、多 worker、同名冲突策略 skip / overwrite / rename、
  逐文件状态与运行摘要（成功/失败/跳过/取消、耗时、吞吐）
- `photopipeline --dev` 命令行 harness（仅 dev 构建）：全语料矩阵、日志与回归基线入口

**界面**

- 三页主窗口（元数据规则 / 输出配置 / 运行监控）：拖放与目录递归收集、异步缩略图、文件名搜索、
  十二态状态徽标、不支持文件计数
- 参数表单引擎：8 格式 × 后端 × 技术的参数联动（谓词显隐、无损锁定、内省默认值）；M2 新增
  跨字段约束（webp lossy `qmin > qmax`、jpeg `progressive + optimize_coding`）红字反馈与启动阻断
- 内嵌地图选点：OSM（WGS-84）与高德（GCJ-02 自动边界转换）；断网自动降级，高德需在设置填 Key
- 单文件编辑器：EXIF 树（IFD0/Exif/GPS/只读 MakerNote）、XMP、时间/GPS/隐私三态覆盖，源文件永远只读
- 设置与预设（JSON）持久化；M2 新增 XMP 页搜索框、时间预览异步化、未选中格式按钮可点性边框、
  搜索框列对齐、设置页按当前页收缩、预设空态提示移入列表区

**发行与质量设施**

- `photopipeline --version` → `PhotoPipeline 0.1.0`；版本单源（`cmake/version.h.in` 由 CMake 生成，
  唯一改动点 = 顶层 `project(PhotoPipeline VERSION 0.1.0)`），关于页与 `--version` 同源
- `PP_LOG_LEVEL` 环境变量启动期一次性覆盖日志级别（非法值 stderr 提示并忽略）；单日志文件
  16 MiB 上限（截断后保留尾部 8 MiB，轮转规则不变）
- AppImage 打包（`tools/make_appimage.sh`，免安装、便携）+ 参数化生成的 9 色块应用图标
  + desktop 文件；打包器与 type2 runtime 二进制均入库并旁置 SHA512（T11d：`--runtime-file` 指向
  入库 runtime，**全程离线**；缺件或校验失败即 exit 2 硬失败，不回退到在线下载）
- 第三方许可随附（T11c）：`tools/collect_licenses.sh` 汇总 **40 个 port** 的许可文本 →
  `usr/share/licenses/<port>/copyright`，本项目 `LICENSE` → `usr/share/licenses/PhotoPipeline/LICENSE`；
  打包烟测从产物内解包逐项校验
- AppImage 依赖闭包修复（T18）：插件依赖解析到**系统 Qt 6.10** 导致 xcb 插件加载失败（双击无响应）
  → 闭包输入扩为「主二进制 + 全部插件」、随包 26 个 `.so`、`libQt6*` 溯源硬门禁（逐字节 `cmp`）、
  覆盖全部 **36 个 ELF** 的 A/B 两类闭包门禁，并新增真实显示 GUI 烟测（`tools/appimage-gui-smoke.sh`）
- 金样断言级 **16 对**（像素 + 元数据值 + warnings 三面断言）；全语料回归基线
  （`tools/regression.sh` + `tools/baseline/golden.log`，规范化后双跑零 diff）
- sanitizer 清扫：ASan/UBSan 全语料矩阵零发现；TSan 并发路径 0 报告（抑制文件附 happens-before 论证
  与阳性对照）
- GitHub Actions 工作流（`build-test.yml`）**四个 job**：`linux`（release 构建 + ctest 23 + 16 对金样）
  · `ui-smoke`（release-dev + offscreen 无头冒烟）· `appimage`（打包 + 四项内置烟测 + 产物启动烟测 +
  `PhotoPipeline-AppImage` artifact）· `cache-gc`（缓存清理）；三类缓存（Qt 工具链 / ccache /
  vcpkg 二进制缓存）由 `warm-cache.yml` 播种（`workflow_dispatch` + 每周 cron）

### 修复

- UI 观感 5 项（M1b 审查遗留）：未选中格式按钮可点性、搜索框左边界、设置页留白、预设空态位置、
  XMP 页缺搜索框
- 时间预览扫描由 GUI 同步改为后台异步（200 文件上限；批变化时取消旧任务，避免乱序回填与冻结）
- 短批次运行不再输出无信息量的 budget 日志（`elapsed < 1s` 守卫）
- 未知编码参数、元数据写失败现在进入用户可见警告通道（此前仅落日志，UI 不可见）
- 非 UTF-8 locale 下预设保存/载入、设置 INI、日志目录的路径往返不一致（统一走 locale 安全层）
- `pp_verify` 元数据文本化不稳定（有理数定形 `a/b`、ASCII trim、数组 `, ` 连接）
- 链接自注册统一为 whole-archive 形态并删除冗余 anchor 符号；UI 冒烟断言改为顺序无关
- 运行页顶部状态行因布局激活推迟被裁切（末位数值缺失）→ 同步 `layout()->activate()`（T22）
- AppRun 失败弹窗改为**有界**：`zenity` / `xmessage` 各自带 60 秒超时（自动消失，不再无界阻塞
  自动化调用者），并新增 `PP_NO_GUI_POPUP=1` 开关（置 1 时完全不弹窗，stderr 与日志照旧写）（T24）
- AppRun 在**缺少 `~/.cache`（或 HOME 不可写）的账户**上会静默退出（无窗口、无日志）——根因是
  `: >"$LOG"` 里的 `:` 属 POSIX 特殊内建，重定向失败会直接终止 shell（退出码 2），`|| LOG=/dev/null`
  兜底永不执行；现改为「先建日志目录、再用非特殊内建探测可写、失败即回退 `/dev/null`」，双击启动
  不再受该目录影响（T27）
- CI / 干净环境构建修复：nasm 缺失、jpegli pkgconfig、libjpeg `jerror.h`、vcpkg 二进制缓存键

### 已知问题

- **仅 Linux x86_64 发行**：Windows / macOS 未构建（内存探测与数据目录的 Windows 分支顺延 M3）
- **AVIF 10-bit + alpha 不可用**：SVT-AV1 后端的上游缺陷（会破坏堆），程序在编码前明确报错并建议
  改用 libaom 后端；HEIF/x265 后端则不提供线程数参数，无法强制单线程（保留后端默认线程池，已实测）
- 第三方 sRGB ICC 源转到 sRGB 目标存在 ≤1.8e-4 的暗部偏差（lcms2 优化器精度；处于数值契约内，
  已记录为事实）
- 输入目录收集不跟随符号链接；批次内输出名冲突判定为词法近似（Linux 文件系统大小写敏感，不构成
  实际问题）
- 超大输入生成缩略图时仍执行一次全解码（内存峰值处于缩略图预算内）
- 在线地图瓦片与经纬度反查依赖网络与高德 Web 服务 Key；离线时自动降级（本版地图精度已接受）
- AppImage 不承诺字节可复现（squashfs 超级块时间戳参与打包）；运行依赖目标机提供 `libssl3`
  （Qt TLS 后端 dlopen 系统 OpenSSL，缺失时在线地图失效）与 xcb 相关系统库（见 README 发行章节）
- GitHub Actions 为托管运行（ubuntu-24.04 runner），本地不可完全复现该环境；本版收口轮次的 CI
  结果（四 job 全绿）见 M2 报告 —— 终轮 run `35521800777`（head `d1b5fb7`）：`linux` 1m32s /
  `ui-smoke` 1m45s / `appimage` 1m37s / `cache-gc` 9s

### 发行

- Release: <https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.1.0> —— 附件
  `PhotoPipeline-0.1.0-x86_64.AppImage`（**50,940,408 B**，sha256 `31f75c48052be34a…`，前 16 位；
  完整校验值以 Release 页为准）
