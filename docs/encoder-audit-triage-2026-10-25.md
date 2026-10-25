# 编码器审计价值分诊 + Alpha / 多页 TIFF 系统方案

> **第二轮裁定（2026-10-25，以此为准）**：Alpha **明确不支持**（不输出 alpha，输入拍平到背景并告警）；TIFF **多页/动画不支持，但缩略图/预览图支持**；判断口径改为"是 bug / 不是 bug"二分，是 bug 就修。  
> 本文 §3.1、§6、§7 的 alpha/TIFF 方案已被 **[第二轮裁定](encoder-audit-decisions-r2-2026-10-25.md)** 取代；本文保留作为全量条目与"哪些不是 bug"的索引。

> 依据：`docs/encoder-config-audit-2026-10-25.md`（下称"全量报告"）的 84 条格式条目 + 92 条 `X-*` 跨面条目。
> 本文只做两件事：① 按"用户真实损失"重排 P0/P1/P2/P3，指出哪些是"审计完整性"而不是"该修"；② 给出 alpha 与多页 TIFF 两条系统方案（不是逐条打补丁）。
> 结论先行：**全量报告的事实取证可信，但严重度是"影响面"口径，不是"发生概率 × 默认路径 × 静默性"口径；按后者重排，真正值得先修的是 2 个 P0 + 约 10 个 P1/P2 系统项，其余大多数是边界、文案、取证面或重复条目。**

---

## 0. 一句话结论

- **P0 两条都值得，且 alpha 不是"一条 bug"，是一组同根缺陷**：解码端把 OIIO 的关联 alpha 当直通，随后 flatten 二次相乘、WebP/JXL/HEIF/AVIF 把预乘数据当未关联写入、PNG 反关联时低 alpha 量化并在 alpha=0 丢 RGB。只改 flatten 公式只能修 JPEG/BMP 一路，必须做解码边界归一。
- **多页 TIFF 是真正的容器结构损坏**：编码器只写 1 页（705 B），损坏发生在 Exiv2 元数据后写（最终 4622 B / 3 IFD），第 3 个 IFD 的 `StripOffsets` 数组偏移写成 0，读取器从 TIFF 头读出 `[2771273, 8]` 越界。根因不是"TIFF 编码器"，是"把源的多页 ExifData（`Exif.SubImage*` / `Exif.Thumbnail`）整份重放进单页产物"。
- **P1 17 条里约 8 条必修、8 条该修、1 条可延后；P2 37 条里约 10 条真正有价值；P3 28 条基本是打磨/文档；`X-*` 92 条大量与格式条目重复。**
- **报告里有 5 个判断需要修正**：alpha 的"局部修法"不成立；TIFF 只删 Thumbnail 不够；严重度缺"默认路径"权重（HEIF-03、PNG-02 应升，JXL-04/05、WEBP-02/03 应降）；若干净控件/上游怪癖被当成缺陷；`X-*` 重复条目抬高了"问题总数"。

---

## 1. 分诊口径：什么叫"真正有价值"

一条发现至少命中以下一项，才进入修复队列：

1. **默认路径就会发生**（不点高级参数、不改预设）。
2. **静默**（`ok=true`、无 UI 提示、产物却错/假）。
3. **破坏数据或破坏承诺**（像素错、文件不可解码、无损承诺为假、色彩标记丢失）。
4. **常见工作流**（手机 HEIC、网页 WebP、截图/Logo 的 alpha、扫描多页 TIFF）。
5. **修复成本低且能机械化验收**（一行顺序、删一个死枚举、加一条 lock/断言）。

反之，以下归为"审计完整性/打磨"，不占修复窗口：

- 只影响文档、tooltip、行号、注释、UI 文案；
- 只影响 `--dev` 取证链（不是用户入口）；
- 上游库的固有约束（libwebp 的 `partitions`、OIIO 的 `biSizeImage`），且已有明确 workaround；
- 同一事实在格式章 + `X-*` 章重复登记；
- 参数本身是"装饰控件"，正确处置是删/置灰，而不是"修生效"。

---

## 2. 全量严重度 vs 价值密度

| 报告级别 | 报告条数 | 真正值得修（本文判定） | 说明 |
|---|---:|---:|---|
| P0 | 2 | **2** | alpha 综合征 + 多页 TIFF；两条都会静默损坏数据/产物 |
| P1 | 17 | **8 必修 + 8 该修 + 1 可延后** | 见 §4；必修集中在默认路径/必失败/假无损 |
| P2 | 37 | **约 10** | 其中 HEIF-03、PNG-02、X-COMPLIANCE-08、X-METADATA-ONLY-01 比部分 P1 更值 |
| P3 | 28 | **3–5** | 删死控件、改标签、补文档；其余不影响产物 |
| `X-*` | 92 | 去重后约 20，高价值约 5 | alpha 重复 4 条、库面重复多条；`X-COMPLIANCE-08/05/01`、`X-METADATA-ONLY-01` 是新增高价值 |

> 这不是说 P2/P3 的取证是错的；它们对"全量审计"有价值，对"修复排序"价值低。报告把"审计完整性"和"修复优先级"混在同一个 P 级别里，是阅读时最大的噪音来源。

---

## 3. 两条 P0：都值得，但 alpha 被低估了"面"

### 3.1 P0-1 alpha：一条根因，五种表现

> **已被第二轮裁定取代**：alpha 明确不支持，不再保留/输出 alpha；正确做法是"解码后、色彩前拍平 + 警告"，当前真 bug 是 flatten 对预乘缓冲二次乘 alpha。见 [第二轮裁定](encoder-audit-decisions-r2-2026-10-25.md) §1。

全量报告已经拆得很细，但表述上仍像"若干条独立问题"。实际是**一个边界语义错误**：

```
OIIO 解码默认返回 associated（预乘）RGB
        │
        ├─ JPEG/BMP：flatten 再乘一次 alpha → 半透明区偏暗
        ├─ WebP：libwebp 按 unassociated 写入 → 文件里是预乘值，查看器再乘一次
        ├─ JXL：声明 alpha_premultiplied=false，写入预乘值
        ├─ HEIF/AVIF：不写 prem 引用，写入预乘值
        └─ PNG：OIIO 写端反预乘 → 低 alpha 量化误差，alpha=0 的 RGB 被归零
```

**为什么必须做系统方案**：

- 只把 `flatten` 公式改成 `dst = src + bg*(1-a)`（全量报告 X-COMPLIANCE-01 的备选①），只能修 JPEG/BMP；WebP/JXL/HEIF/AVIF 仍然把预乘值当直通写，PNG 仍然在反预乘时丢低 alpha 信息。
- 只让每个编码器各自"反预乘"是 N 个补丁，而且 PNG 写端还会再反一次；色彩管理（lcms2）在预乘值上做非线性 TRC 变换本身就不等价于"先变换再预乘"。
- 因此正确的系统不变量是：**引擎内部统一 straight（unassociated）alpha；premultiplied 只作为容器/编码器的表示，在边界一次性转换**。详见 §6。

### 3.2 P0-2 多页 TIFF：根因在元数据计划，不在编码器

证据链（全量报告 §4.4 + 本次复核）：

- `enc_oiio.cpp:379` 只调一次 `write_image`，编码阶段产物 **705 B**（1 页）。
- `metadata.cpp:820` `plan.exif = src.exif` 把源 TIFF 的全部 ExifData 复制进来；Exiv2 的 TIFF 模型把后续 IFD 映射成 `Exif.SubImage1..9`（`tiffimage_int.cpp:1272-1280`）或 `Exif.Thumbnail`。
- `metadata.cpp:380` `img->setExifData(*exif)` + `writeMetadata()` 把这些组重新展开成额外 IFD；第 2 页数据被真的复制（`strip` 载荷逐字节相同），第 3 页只有目录项、数据没搬。
- 产物 3 个 IFD；第 3 个 IFD 的 `StripOffsets` 数组偏移写成 **0**，读取器从 TIFF 头读出 `[2771273, 8]`（= `II*\0` + 偏移 8），而文件只有 4622 B；`oiiotool --subimage 2` 报 `Read error at scanline 4294967295`。
- `ok=true`，只有一条 `MultipageTruncated`；用户不会发现。

**为什么它是 P0**：不是"多一页"的审美问题，是"产物含不可解码目录 + 运行结果说成功"的结构损坏；任何枚举子图的工具（OIIO/libtiff/PDF 封装/扫描档批处理）都会在该页报错。

**修复方向**：在元数据计划层剥离 `Exif.SubImage*` / `Exif.Thumbnail.*` 与源结构标签，并在临时文件提交前做 IFD 结构探针（§7）。只删 Thumbnail 不够——本 case 的第 2/3 页是 `SubImage`，不是 Thumbnail。

---

## 4. P1 17 条：逐条价值判定

### 4.1 必修（8 条）

| ID | 问题 | 为什么必修 |
|---|---|---|
| AVIF-01 | GUI 默认 `qp=0`，SVT CQP 0，`quality` 失效，体积 7–32× | 默认路径 + 静默 + 画质/体积双输；内省 `has_default=false` 兜底成 0 是系统机制，不只 AVIF |
| AVIF-02 | SVT-AV1 勾"无损"必失败 | UI 可点、100% 失败、错误文案不指向"SVT 只支持 4:2:0" |
| BMP-02 | 16-bit 源默认 24 bpp 静默降到 8 bps，无降档告警 | 数据精度静默丢失；`src_bitdepth > out_bitdepth` 把 bps 与 bpp 直接比较，是明确 bug |
| HEIF-01 | "Main（8 位 · 兼容性最好）"是 100% 失败选项 | UI 第一项、兼容性承诺最强、必失败且只报"档位不一致" |
| JPEG-01 | 色度采样在量化表之后设置，444/422/440 实际用 420 的 DQT | **每个默认 JPEG** 都受影响；顺序一行修复，参考实现顺序明确 |
| JXL-01 | 无损 + `resampling>1` 静默降采样 | 假无损；虽然要用户改非默认参数，但修复是 lock + 规则 + 编码器兜底，成本低 |
| JXL-03 | 三个 Modular 参数用错 setter，一改就整文件失败 | 高级参数只要动一下就失败；改 float setter 即可，成本极低 |
| WEBP-04 | `near_lossless<100` 生效后非逐位无损，仍报 `lossless=true` | 假无损 + 无告警；锁 100 或发 warning 即可 |

### 4.2 该修（8 条）

| ID | 问题 | 判断 |
|---|---|---|
| BMP-01 | 位深下拉 8/24 对 RGB 源产同一字节，两个方向都与产物矛盾 | BMP 是边缘格式，但 UI 在说谎；建议改成"按通道数只读展示"而不是真去驱动 bpp |
| BMP-03 | keep + 非 sRGB 源 ICC 被 BMP 丢掉，无 ICC 告警 | 色彩语义静默丢失；BMP 无 ICC 槽是事实，正确做法是"转 sRGB 或明确告警" |
| JXL-02 | `color_transform` 三个值被 libjxl 覆写为 no-op；默认 YCoCg 让无损体积 2.8× | 死控件 + 默认体积问题；删三档/加 auto，或至少把默认交还库 |
| JXL-04 | `codestream_level` 默认 10，每个产物带 `jxll` | 兼容性无谓收窄；改成 auto（-1）即可。**可降为 P2**，因为主流解码器多支持 level 10 |
| TIFF-02 | `ccittrle` 是必然失败档 | 删枚举或运行前拦截；成本低，但只影响选该档的人 |
| WEBP-01 | `tech=lossy + lossless=true` 静默出有损，全链记无损 | GUI 主路径会自愈，CLI/预设可复现；仍应让编码器/校验层拒绝矛盾组合 |
| WEBP-02 | 选 Lossless 技术但不勾无损 → 产物无损、记录却说非无损 | 反向 UI 真值问题；正确做法是技术选择驱动复选框，单向同步。**可降为 P2** |
| WEBP-03 | `preset` 六档产物逐字节相同 | 死控件；删行最省事，或改成"默认值来源"并与手调联动。**可降为 P2** |

### 4.3 可延后（1 条）

| ID | 问题 | 判断 |
|---|---|---|
| JXL-05 | `distance=0` 无 ICC 时失败、有 ICC 时静默变无损 | 边界/怪异行为；UI 可把 VarDCT 的 0 禁用或写清"0=无损且需 ICC/Modular"，不必现在动库路径 |

### 4.4 P1 里的"严重度错位"

- **JXL-04、JXL-05、WEBP-02、WEBP-03 更像 P2**：触发面窄（高级参数/CLI/预设）、无数据损坏、修复是删/锁/改文案。
- **AVIF-02、HEIF-01 是"必失败"而不是"数据损坏"**：P1 合理，但优先级应低于 JPEG-01/AVIF-01 这类"默认路径静默错"。
- **BMP-01/02/03 应打包成一条"BMP 是二等格式"的决策**：要么把 BMP 的高级控件收成只读 + 明确告警，要么承认 BMP 只做"8/24 自动按通道"，不要三条分开修。

---

## 5. P2/P3：哪些其实比 P1 更值，哪些是噪音

### 5.1 被低估的系统项（建议优先于普通 P1）

**已经是 P1、但报告把它们排在"普通 P1"里，实际应更靠前：**

| ID | 问题 | 为什么 |
|---|---|---|
| PNG-02 / X-COMPLIANCE-05 | PNG 同时写 iCCP 与 sRGB/gAMA/cHRM；非 sRGB ICC 时色彩声明自相矛盾 | **所有带 ICC 的 PNG**；偏好 sRGB chunk 的读取器会按 sRGB 解释 P3/AdobeRGB → 过饱和。不是边角 |
| X-COMPLIANCE-08 | `WarningKind` 在 `src/ui` 零消费，所有降档/丢元数据/拍平/丢 ICC 告警只进日志/侧车 | **系统性**：它让上面一半问题的"修法"对用户不可见；没有它，加 warning 等于没加 |
| X-METADATA-ONLY-01 | `out_root` 在输入树内 → 输出被下一轮收集为输入 → 时间偏移叠加 | 仅元数据模式的**静默数据错误**；修 `collect_inputs` 排除集即可，成本低 |
| X-WIRING-05/06 | 预设不保存 HEIF/AVIF 档位与色彩四全局项；载入后产物不一致 | 预设是"可复现交付"的核心；不修则"保存的配置"不等于"产出的配置" |

**报告标为 P2、但实际应升 P1 的：**

| ID | 问题 | 为什么 |
|---|---|---|
| HEIF-03 | 默认位深 10 → 默认产物是 RExt（`profile_idc=4`）、`ftyp=heix`，8 位才是 Main Still | **默认路径 + 兼容性承诺**；手机/播放器生态对 RExt 的支持远窄于 Main Still。默认改 8 或 UI 明确"10 位=RExt" |
| TIFF-07 | 编码器不设 `DateTime`，OIIO 写编码时刻并派生非 ISO 8601 XMP | 产物不可复现、元数据"加料"；与"元数据手术台"定位冲突 |
| AVIF-04 | libaom `alpha-quality` 被内省默认 50 钉死，`quality` 提高时 alpha 不跟随 | 带 alpha 的 AVIF 质量不一致；修默认/继承逻辑 |
| AVIF-06 | 选 Main 档位时位深被静默 10→8（AV1 Main 本含 10 位） | UI 承诺与产物不符；修 `profile_allowed_bitdepths` 的 AVIF 分支 |
| TIFF-04 | `deflate_level` 在 `predictor=none` 时被完全忽略 | 合法组合下 31× 体积差且调级别无效；加可见性/提示即可，不必改 libtiff |
| WEBP-09 | 跨技术参数被接受、写进侧车/预设，却永远不被读取 | 用户以为设置了；应在 `validate_params` 拒绝或切换技术时清理 |
| X-PARAM-ENGINE-08 | 预设不覆盖色彩四全局项与"仅元数据"模式 | 同 X-WIRING-05/06，是"配置可复现"的系统缺口 |

### 5.2 P2 里"可以做，但不急"

- **HEIF-05**（无损下 quality 仍可调但零效果）：加锁/置灰即可，属于 UI 真值。
- **JXL-07**（Modular 未勾无损 + 默认 distance=0 其实无损）：UI 文案/默认值问题。
- **JXL-08**（`keep_invisible=true` 让带 alpha 有损产物 3.1×）：默认值问题，改 auto/false 或文档。
- **HEIF-06 / AVIF-03**（`threads` 被调度器覆盖）：性能面，不是正确性。
- **AVIF-07**（speed 10–13 映射到 9，默认 12 落在死区）：性能面。
- **AVIF-08**（`tune=iq` + 无损必失败）：加交叉规则。
- **JPEG-03**（`psnr_target` 覆盖 quality 且达不到目标）：高级参数，加锁/提示；库的 PSNR 估计口径不是我们能改的。
- **JPEG-04**（`cicp_transfer_function` 只改量化表、不写色彩标记）：这是"伪色彩参数"，建议删除或改名为量化缩放。
- **PNG-05**（灰度 PNG 可带 RGB ICC）：边缘组合，修 ICC 匹配。
- **X-METADATA-ONLY-02**（GUI 仅元数据静默改 Skip）：行为不一致，修 UI 文案/策略。
- **X-METADATA-ONLY-03**（隐私剥除只清 EXIF+XMP）：隐私承诺不完整；修或明确文案。
- **X-METADATA-ONLY-07**（仅元数据仍强制 OIIO probe）：CMYK TIFF/截断 JPEG 被拒；对"纯元数据"场景是能力缺口。

### 5.3 基本是噪音/只该顺手做（P3 + 部分 P2）

- **文档/标签/行号**：JPEG-05/06/07、JXL-11/12/13、PNG-07/08/09、TIFF-08/09/10、WEBP-10/11/12/13、BMP-04/05/06/07、HEIF-07..13、AVIF-11..13。
- **上游怪癖**：BMP-07 `biSizeImage` 是 OIIO 的写法，不是本仓 bug；PNG-07 空 `eXIf`/`oFFs` 无害；`WEBP-06 partitions`、`WEBP-07 preprocessing` 是 libwebp 的约束，文档化即可。
- **默认值/策略**：PNG-04（8 位源默认 16 位）是"禁止降级"的产品选择，不是缺陷；需要的是 UI 说明/体积提示，不是改默认。
- **格式固有**：PNG/TIFF/BMP 的"无损编码"复选框恒无效果；正确处置是隐藏/勾选+禁用，不是"让它生效"。
- **`--dev` 取证链**：HEIF-02、AVIF-10、X-WIRING-02/04/17 等只影响 `--param` 字符串化入口；用户 GUI 不受影响。修它是为了"证据可信"，优先级低于用户面。
- **装饰控件**：JXL-09 `brotli_effort`、`WEBP-05 near_lossless 默认空转`（默认 100=关，不是 bug）、`WEBP-08 lossy 透明区 RGB`（格式限制）；删/文档即可。
- **重复条目**：`X-COMPLIANCE-01/02` 与 PNG-01、`X-LIBRARY-01` 与 AVIF-01、`X-LIBRARY-03` 与 WEBP-03、`X-PARAM-ENGINE-01` 与 JXL-01、`X-PARAM-ENGINE-02` 与 WEBP-04、`X-WIRING-01` 与 WEBP-01 等，都是同一事实的第二次登记。

**P3 28 条的整体判断**：没有一条影响产物正确性。约 18 条是文案/标签/行号/文档（JPEG-05..07、JXL-11..13、PNG-07..09、TIFF-08..10、WEBP-10..13、HEIF-07..13、AVIF-11..13、BMP-04..06）；约 5 条是死控件/空块（空 `eXIf`/`oFFs`、恒无效复选框）；其余是上游口径或取证面。只有"删死控件/改标签"值得顺手做。

### 5.3.1 P2 37 条全量索引（按处置分组）

| 处置 | ID | 说明 |
|---|---|---|
| **升 P1（7）** | PNG-02、HEIF-03、TIFF-07、AVIF-04、AVIF-06、TIFF-04、WEBP-09 | 默认路径/色彩承诺/可复现/31× 体积；见 §5.1 |
| **该修（10）** | AVIF-03、AVIF-05、AVIF-08、HEIF-05、HEIF-06、JPEG-03、JPEG-04、JXL-07、JXL-08、PNG-05 | 性能旋钮/UI 真值/交叉规则/默认值；不阻塞发布 |
| **可延后或只做文档（10）** | AVIF-07、AVIF-09、AVIF-10、HEIF-02、JXL-06、JXL-09、JXL-10、PNG-03、PNG-06、TIFF-05 | 边界/取证链/暴露缺口；`dev --param` 类只影响证据 |
| **噪音/合并/不改（10）** | BMP-04、BMP-05、HEIF-04、JPEG-02、PNG-04、TIFF-06、WEBP-05、WEBP-06、WEBP-07、WEBP-08 | 文案/策略/格式固有/库约束；PNG-04 是产品选择，TIFF-06 是 UI 打磨 |

> 计数：7 + 10 + 10 + 10 = 37。分组是"处置"判断，不是报告严重度；同一 ID 不重复计数。

### 5.4 "不是 bug"清单（避免误修）

1. **PNG 默认 16 位**：产品原则是"禁止降级"，16 位是保守选择；要修的是"体积提示/默认策略讨论"，不是把它当缺陷。
2. **TIFF/PNG/BMP 的"无损编码"复选框**：格式本身恒无损；控件应隐藏或"勾选+禁用+原因"，不应真的去改变编码。
3. **`partitions` / `preprocessing` 在默认参数下无效**：libwebp 的 `method>=3`/`sharp_yuv` 约束；加 tooltip 或锁，不是编码器漏读。
4. **`biSizeImage`**：OIIO 的 BMP writer 口径；本仓改不了，报上游即可。
5. **`--dev --param` 对 HEIF/AVIF 的类型问题**：dev harness 的限制；GUI 内省路径不受影响。修它是为了取证链，不是用户 P1。
6. **`svt:` 私有透传不存在**：白名单放行了一个库不支持的通道；删白名单项即可，不是"功能缺失"。

---

## 6. Alpha 系统方案：内部统一 straight alpha

> **已被第二轮裁定取代**：alpha 明确不支持，不需要 straight 内部表示；正确方案是"拍平到背景、不输出 alpha"。本节保留为历史分析，不要按此实现。见 [第二轮裁定](encoder-audit-decisions-r2-2026-10-25.md) §1。

### 6.1 先定契约（比任何代码都重要）

**契约 A（内部）**：`DecodeOutcome` / `WorkImage` / `EncodeRequest` 的像素缓冲一律是 **straight（unassociated）alpha**，float32，RGB 不乘 alpha；alpha 在 `[0,1]`。

**契约 B（边界）**：
- 解码层负责把任意源容器归一成 straight；
- 编码层按各容器原生语义写出：PNG/WebP/TIFF(tag 2)/JXL(`alpha_premultiplied=false`)/HEIF/AVIF(no `prem`) 全部写 straight；
- 只有 BMP/JPEG 等不支持 alpha 的目标才 flatten。

**契约 C（测试）**：所有 alpha 比较都在 straight 空间做；`pp_verify` 必须用同一语义读两侧；原生字节探针（PNG IDAT/WebP VP8L/TIFF strip）作为第二证据。

### 6.2 为什么选 straight，而不是"保持预乘、改 flatten 公式"

| 方案 | 做法 | 优点 | 致命缺点 |
|---|---|---|---|
| A. 内部 straight（推荐） | 解码归一；编码器全按直通写 | 一个语义；色彩管理正确；flatten 公式已是直通；PNG 不丢 alpha=0 RGB；WebP/JXL/HEIF 原生就是直通 | 需要处理"源本身是预乘"的少量格式（TIFF tag1/HEIF prem/JXL prem） |
| B. 内部预乘 | 改 flatten 为 `src + bg*(1-a)`；PNG/WebP 编码前反预乘；JXL/HEIF 声明 prem | 不动解码；保留源预乘值 | 色彩管理在预乘值上做非线性 TRC 变换**数学不对**；PNG 低 alpha 仍丢；每个编码器语义分裂；alpha=0 RGB 仍归零 |
| C. 双表示 + 标志 | 每帧带 `alpha_associated`，所有消费者分支 | 最"保真" | 复杂度翻倍；色彩/量化/缩略图/预览全要分支；违背"小而美" |

**关键理由（报告没展开）**：`ColorManager` 在 `colormanager.cpp:979-1009` 把 RGB 送 lcms2、alpha 原样复制。lcms2 的 ICC 变换包含非线性 TRC；对预乘值 `C*a` 做 `T(C*a)` 不等于 `T(C)*a`。只有 straight 缓冲才能保证"先色彩变换、后 alpha 合成/编码"的语义正确。这也是为什么"只改 flatten"不能算系统修复。

### 6.3 解码边界：唯一归一化点

现状 `src/decode/oiio_reader.cpp:234`：

```cpp
out.buf.read(0, 0, 0, spec.nchannels, true, OIIO::TypeDesc::FLOAT);
```

OIIO 的原生约定是 **associated**；PNG/WebP/TIFF/HEIF 的 reader 在没有配置时都会转成 associated（JXL reader 例外，它不处理 `alpha_premultiplied`）。改法：

```cpp
// 1) 用 ImageBuf 的 config 面把 "请返回 unassociated" 传给 reader。
OIIO::ImageSpec config;
config.attribute("oiio:UnassociatedAlpha", 1);
out.buf.reset(file, 0, 0, nullptr, &config);   // 打开并读 spec
if (!out.buf.initialized()) { ... }
// 2) 读像素（仍用 force=true + FLOAT）。
out.buf.read(0, 0, 0, out.buf.spec().nchannels, true, OIIO::TypeDesc::FLOAT);

// 3) 对"reader 无法给 straight"的源做一次归一化。
if (source_alpha_is_associated(out.buf.spec(), file)) {
    if (!OIIO::ImageBufAlgo::unpremult(out.buf, out.buf)) { ... }
    // 可选：warning "source alpha was premultiplied; converted to straight"
}
```

`ImageBuf::reset(name, ..., config)` 会把 config 传给 `ImageInput::open`（OIIO `imagebuf.cpp:1258,1478`），所以这是受支持的路径；`ImageBufAlgo::unpremult` 在 `imagebufalgo.h:2285-2286` 有 bool 重载。

**逐格式的源关联检测**：

| 源格式 | reader 在 `oiio:UnassociatedAlpha=1` 下的行为 | 检测/归一 |
|---|---|---|
| PNG | `pnginput.cpp:226-227,365-374` 保留 unassociated | 已是 straight，无需处理 |
| WebP | `webpinput.cpp:254-257,363-369` 保留 unassociated | 已是 straight |
| TIFF tag 2（unassociated） | `tiffinput.cpp:903-904,1362-1367` 保留 | 已是 straight |
| TIFF tag 1（associated） | reader **不会**反预乘（只处理 tag2） | 检测：`alpha_channel>=0 && !oiio:UnassociatedAlpha && !tiff:UnassociatedAlpha` → `unpremult` |
| HEIF/AVIF，无 `prem` | `heifinput.cpp:389-404` 不预乘 | 已是 straight |
| HEIF/AVIF，有 `prem` | reader 返回预乘（`m_associated_alpha=true`，不会反） | 用 `heif_image_handle_is_premultiplied_alpha()` 检测 → `unpremult` |
| JXL，`alpha_premultiplied=false` | OIIO JXL reader 不处理该 flag，但数据本身是 straight | 已是 straight |
| JXL，`alpha_premultiplied=true` | reader 原样返回预乘 | 用 libjxl `JxlDecoderGetBasicInfo` 的 `alpha_premultiplied` 检测 → `unpremult` |
| JPEG/BMP | 无 alpha | 不适用 |

> 对无法可靠判定关联性的格式（不在上表、且带 alpha），**失败并给出明确错误**，不要默认假设。静默假设是这类 bug 的温床。

### 6.4 编码边界：逐格式写什么

| 目标 | 写入前数据 | 声明/API | 需要的改动 |
|---|---|---|---|
| PNG | straight | OIIO `ImageSpec` 加 `oiio:UnassociatedAlpha=1`；否则 `pngoutput.cpp:240-241` 会 `unpremult` | `enc_oiio.cpp` 在 `nch==2/4` 时设 attr |
| TIFF | straight | 同一 attr；`tiffoutput.cpp:828-840` 据此写 `ExtraSamples=2`（unassociated） | 同上；当前默认写 tag 1 |
| WebP | straight | `WebPPictureImportRGBA` 要求 unassociated（`webpoutput.cpp:169-176` 同款注释） | 无代码改动；加注释/断言，防未来有人"优化"成预乘 |
| JXL | straight | `info.alpha_premultiplied=JXL_FALSE`，`ec.alpha_premultiplied=JXL_FALSE` | 已正确；保持，并加测试 |
| HEIF/AVIF | straight | `heif_image_set_premultiplied_alpha(img, 0)`（默认即 0）；不写 `prem` | 显式调用一次，防默认变化；加容器扫描断言无 `prem` |
| JPEG/BMP | flatten 后的 RGB | 无 alpha | 无改动；flatten 公式保持 `src*a + bg*(1-a)` |

**flatten**：`simd/kernels.cpp:78-92` 的 `flatten_ref` 已经是 straight 公式，**不要按全量报告 X-COMPLIANCE-01 的备选①去改**；只要解码归一，公式就正确。建议在 `flatten_alpha_in_place` 加一条 debug 断言/契约注释："输入必须是 straight；由 decode 边界保证"。

**色彩/量化**：
- 色彩变换在 straight 上做（现状 `colormanager` 只动 RGB、alpha 原样，正好符合）。
- 量化在 straight 上做；PNG 写端不再反预乘，低 alpha 的 RGB 不会被放大/截断。
- `alpha=0` 的 RGB 在 straight PNG/WebP/JXL/TIFF 中可以保留源值；若某容器要求归零，必须显式 warning，不能静默。

### 6.5 测试与门禁：修 alpha 必须同时修裁判

现状最致命的是"裁判和编码器共享同一个错误语义"：

- `pp_verify` 的 `load_oiio` 用无 config 的 `ImageInput::open`（`tools/pp_verify.cpp:79`），PNG/TIFF 读回是 associated；WebP 走 libwebp 是 straight。两侧恰好抵消，`webp-lossless` 的 exact 才能过。
- 金样 alpha 夹具只有 0/255（关联恒等点），拍平数学错误无裁判。

**必须一起做的四件事**：

1. `pp_verify` 的 OIIO 读取加 `oiio:UnassociatedAlpha=1`；对 TIFF tag1 源做同样的 `unpremult` 归一；WebP 继续用 libwebp（straight）。
2. 新增 `tests/unit/test_alpha_semantics.cpp`：
   - 构造 256×2 RGBA8：RGB 恒 `(200,100,50)`，alpha 从 0..255；
   - `decode_float` 后断言 alpha=128 的 RGB ≈ 200/255（straight），不是 100/255；
   - 经 `enc_oiio` 写 PNG，用 OIIO unassociated 读回：alpha=0/1/128 的 RGB 必须是 200/100/50（允许量化 ±1）；
   - 经 `enc_webp` 写 WebP，用 libwebp 解码断言同样值；
   - 经 `enc_jxl`/`enc_heif` 写，用对应解码器断言；
   - flatten 到 RGB 后断言 `(228,178,153)`（白底、alpha=128）。
3. 金样补 alpha 像素断言：`alpha-jpeg`、`multiformat-split` 的 jpeg 腿目前 `"pixel": {}`；加"合成后采样点"断言。断言只增不减，不改夹具。
4. 原生字节探针：PNG IDAT 解码、WebP `WebPDecodeRGBA`、TIFF strip 解析；至少对 PNG/WebP 各一条，作为第二证据，防 OIIO 读端再次"对称抵消"。

### 6.6 迁移与风险

- **现有金样会变**：PNG 低 alpha 像素、WebP/JXL/HEIF 半透明区颜色会从"错的"变成"对的"；TIFF 的 `ExtraSamples` 从 1 变 2（像素值也变 straight）。这是预期行为，不是回归；提交正文必须写清"旧断言锁的是错误语义"。
- **`webp-lossless` exact**：修完 `pp_verify` 后应仍然通过（两侧都 straight）；如果失败，先查 `pp_verify` 的 WebP 分支，不要回退编码器。
- **性能**：请求 unassociated 省掉 reader 的 premultiply；PNG 写端省掉 unpremultiply；只有"源本身预乘"的少数文件多一次 `unpremult`。总体不亏。
- **冻结面**：`oiio_reader.cpp`、`enc_*.cpp`、`pp_verify.cpp` 都是内部实现；`EncodeRequest`/`ImageInfo` 不需要加字段。若加 `WarningKind::AlphaNormalized`，属于 `types.h` 的加性扩展，需按 AGENTS §5.1 登记裁定；也可以先只 `log_info`，不扩枚举。
- **第三方预乘源**：TIFF tag1、HEIF `prem`、JXL `alpha_premultiplied=true` 是真实存在的输入；`unpremult` 会把 8-bit 预乘值的量化误差放大（低 alpha 尤其明显），这是"源本身已经损失"的不可避免结果。建议在 sidecar 记 `alpha_normalized=true`，让用户知道。

### 6.7 落地顺序

1. **P0-a**：`oiio_reader` config + 归一化 + `pp_verify` config；先让 decode/verify 语义一致。
2. **P0-b**：`enc_oiio` PNG/TIFF attr；`enc_heif` 显式 `set_premultiplied_alpha(0)`；`enc_webp`/`enc_jxl` 注释 + 断言。
3. **P0-c**：单测（decode/encode/flatten）+ 金样 alpha 像素断言 + 原生字节探针。
4. **P0-d**：跑 `ctest`、`smoke`、`regression`；对 `alpha-jpeg`/`multiformat-split` 的产物做人工/脚本复核。
5. **后续**：JXL prem 检测、HEIF prem 检测（如果第 1 步只做了 PNG/WebP/TIFF）。

---

## 7. 多页 TIFF 系统方案：单页不变量 + 结构探针

> **已被第二轮裁定取代**：多页/动画 TIFF 不支持（只取首页 + 警告，不写入额外页）；**缩略图/预览图支持**（保留合法 IFD1 缩略图）。本节"剥离全部 Thumbnail"的写法是错的，会误删缩略图。见 [第二轮裁定](encoder-audit-decisions-r2-2026-10-25.md) §2。

### 7.1 先定不变量

- **转码输出 = 单帧**：多页/动画输入只取第 0 页（已有 `MultipageTruncated`）。
- **转码 TIFF 产物必须恰好 1 个图像目录（IFD0）**，且不含 `SubIFDs`、`Exif.SubImage*`、`Exif.Thumbnail` 目录。
- **仅元数据模式 = 同容器原样**：必须保留原 IFD 数量、strip/tile 载荷与偏移语义；已实测 2 页 pass-through 逐字节相同。
- **元数据计划分层**：`TagMetadata`（EXIF/XMP/GPS 等标量）与 `ImageStructure`（IFD 链、SubIFDs、缩略图、strip/tile offsets）分离；转码只消费前者，仅元数据消费后者（由 Exiv2 原样重写）。

### 7.2 根因（已复核到字节）

1. `build_plan()` 把源 `ExifData` 全量复制（`metadata.cpp:820`）。
2. Exiv2 的 TIFF reader 把后续 IFD 映射成 `Exif.SubImage1..9`（`tiffimage_int.cpp:1272-1280` 的 `subImage1Id..subImage9Id` 组）或 `Exif.Thumbnail`。
3. `exiv2_apply()` 的 `setExifData(*exif)` + `writeMetadata()`（`metadata.cpp:376-387`）按这些组重建 TIFF：第 2 页的数据被复制，第 3 页只有目录项、没有数据；`StripOffsets` 数组偏移写成 0。
4. 产物 3 IFD、4622 B；第 3 IFD 的 `StripOffsets` 读到 `[2771273, 8]`（TIFF 头），越界；`ok=true`。

> 关键：这不是"Exiv2 故意保留多页"，而是"我们把源的多页结构当成普通标签交给 Exiv2 重放"。修复点必须在**计划层**，不能指望编码器或 Exiv2。

### 7.3 主修：转码计划剥离结构组

新增内部函数（`metadata.cpp`，不进冻结头）：

```cpp
// 只用于转码输出；metadata-only 不调用。
void sanitize_transcode_metadata(MetadataPlan &plan, bool keep_thumbnail);
```

删除规则：

- **所有转码输出**：`Exif.SubImage1..9.*` 全部删除（这些是页/子图目录，不是标量标签）。
- **TIFF 输出**（或任何 `keep_thumbnail=false` 的运行）：`Exif.Thumbnail.*` 全部删除。
- **所有转码输出**：删除源结构标签，避免旧结构覆盖新产物：
  `Exif.Image.ImageWidth`、`ImageLength`、`BitsPerSample`、`Compression`、`PhotometricInterpretation`、`StripOffsets`、`RowsPerStrip`、`StripByteCounts`、`PlanarConfiguration`、`Predictor`、`TileWidth`、`TileLength`、`TileOffsets`、`TileByteCounts`、`SamplesPerPixel`、`ExtraSamples`、`SampleFormat`、`NewSubfileType`、`SubfileType`、`FillOrder`、`MinSampleValue`、`MaxSampleValue`、`YCbCrSubSampling`、`YCbCrPositioning`、`ReferenceBlackWhite`、`JPEGInterchangeFormat`、`JPEGInterchangeFormatLength`。
- **保留**：`Exif.Image.Orientation`（旋转后统一置 1）、`Make/Model/Software/Artist/Copyright/DateTime`、`Exif.Photo.*`、`Exif.GPSInfo.*`、XMP。
- **ICC**：`Exif.Image.InterColorProfile` 建议删除，ICC 由编码器独立写入，避免"元数据里的 ICC"和"编码器 ICC"打架。

调用点：

```cpp
// pipeline.cpp 转码路径（约 :967）
ctx.meta_plan = build_plan(ctx.srcmeta, ctx.cfg.rules, ctx.fe.exception);
if (ctx.rotated) ctx.meta_plan.exif["Exif.Image.Orientation"] = uint16_t(1);
// 新增：任一 target 是 TIFF/JXL/HEIF/AVIF 时，不保留缩略图；SubImage/结构标签恒删。
sanitize_transcode_metadata(ctx.meta_plan, /*keep_thumbnail=*/!needs_structural_strip(ctx.targets));
ctx.payloads = make_payloads(ctx.meta_plan);   // JXL/HEIF 的 EXIF blob 也必须是净化后的
```

`run_metadata_only`（约 :1421）**不要**调用；它需要完整源结构。

### 7.4 防线：提交前做 TIFF 结构探针

只做剥离还不够：Exiv2 版本变化、其他容器写路径、未来新增字段都可能再引入 IFD。需要一条"提交前断言"。

新增内部 `tiff_probe.cpp`（或 `metadata.cpp` 静态函数）：

```cpp
struct TiffProbe {
    bool ok = false;
    int directories = 0;       // IFD 链上的图像目录数
    int sub_ifds = 0;          // 0x014A 指向的子图数
    bool offsets_in_bounds = true;
    std::string error;
};
TiffProbe probe_tiff_structure(const std::filesystem::path &p);
```

检查项：

- 头：`II*\0` / `MM\0*`；BigTIFF `II+\0` / `MM\0+`（64 位偏移）。
- IFD 链：从 offset 走 `next`，检测环、上限（如 1024）、每个目录 entry 数上限。
- 每个 entry 的值区：`offset + type_size*count <= file_size`。
- 图像数据：`StripOffsets/StripByteCounts`、`TileOffsets/TileByteCounts`、`JPEGInterchangeFormat/Length`；每个 `offset+count <= file_size`，且 `count>0` 时 `offset != 0`。
- `SubIFDs`（0x014A）：转码输出要求 `sub_ifds == 0`；metadata-only 允许，但要求与源一致。
- `ExifIFD`/`GPSIFD`：offset 在文件内即可，不计入 `directories`。

接入点：

- `exiv2_apply_atomic()`（`metadata.cpp:401-426`）当前流程是 `copy_file → exiv2_apply(tmp) → commit_temp_file`。在 `exiv2_apply(tmp)` 之后、`commit_temp_file` **之前**插探针：
  - 转码 TIFF：`directories == 1 && sub_ifds == 0 && offsets_in_bounds`；
  - metadata-only TIFF：`directories == source_directories && offsets_in_bounds`；
  - 失败：`discard_temp_file(tmp)`，返回明确错误；目标文件保持编码产物原样（不会落地坏文件）。
- `write_metadata_exiv2()` 对 `.tif/.tiff` 传 `expected_directories=1`；`rewrite_metadata_only()` 先 probe 源，传源目录数。
- 由于 `exiv2_apply_atomic` 是 `metadata.cpp` 内部函数（不在 `metadata.h`），加参数不触碰冻结面；对外签名不变。

**为什么必须在 commit 前**：如果先 commit 再 probe，坏文件已经覆盖了原本有效的编码产物；用户拿到的是坏文件 + `ok=false`，仍然算数据损坏。

### 7.5 仅元数据路径：保留多页，加"不变"断言

- `rewrite_metadata_only()` 不解码、不重编码；Exiv2 会保留原 IFD 链（已实测 2 页 pass-through 整文件相同）。
- 加探针：`before.directories == after.directories`，且 `after.offsets_in_bounds`；strip/tile 载荷的逐字节相等仍由测试/审计脚本保证。
- 不要对 metadata-only 调用 `sanitize_transcode_metadata`；否则会把用户的多页 TIFF 改成单页，违背"仅元数据"承诺。

### 7.6 测试

1. **单元**：`test_tiff_probe.cpp`
   - 1 IFD / 2 IFD / 3 IFD 的正常文件；
   - `StripOffsets` 越界、`SubIFDs` 非空、环、截断；
   - BigTIFF 最小样本（可选）。
2. **单元**：`test_metadata_sanitize.cpp`
   - 源 ExifData 含 `Exif.SubImage1.ImageWidth`、`Exif.Thumbnail.JPEGInterchangeFormat`、`Exif.Image.StripOffsets`；
   - 净化后这些键不存在，`Exif.Photo.*`/`GPSInfo`/XMP 保留；
   - `keep_thumbnail=true` 时 Thumbnail 保留、SubImage 仍删。
3. **端到端**：
   - 3 页 TIFF → TIFF 输出：产物 `directories==1`，首帧 `pp_verify exact OK`；无越界 offset；
   - 2 页 TIFF → TIFF 输出：`directories==1`（回归当前 P0）；
   - 3 页 TIFF → metadata-only：`directories==3`，三页都可解码，strip 载荷 sha 不变；
   - 带 EXIF 缩略图的 JPEG → TIFF：`directories==1`（Thumbnail 被剥离）；
   - 多页 TIFF → JPEG/PNG/WebP：不产生额外 IFD/异常 EXIF（`SubImage` 已剥离）。
4. **门禁**：把"3 页 TIFF → 1 IFD"加进 `tests/golden` 或 `--dev` 集成用例；现有金样只有 2 页 `multi.tif`，且只断言首页。

### 7.7 备选方案与取舍

| 方案 | 做法 | 取舍 |
|---|---|---|
| **A. 单页 + 计划剥离 + 探针（推荐）** | 转码只输出首帧；元数据计划不含结构组；提交前断言 1 IFD | 符合产品"多页取首页"；改动集中在 metadata/pipeline；风险低 |
| B. 保留多页 | 解码所有页、逐页重编码，或复制压缩 strip 重写 IFD | 需要多帧管线/每页参数/进度模型；与"解码一次、编码 N 次"架构冲突；当前不做 |
| C. 只删 Thumbnail | 报告原建议的最小版 | **不够**：本 case 的第 2/3 页是 `SubImage`，仍会写出 3 IFD；必须同时删 SubImage |
| D. 不用 Exiv2 写 TIFF | 用 libtiff 只改 IFD0 标签 | 需要自己处理 EXIF/GPS/XMP 写入；工作量大，收益是彻底避免结构重写；可作为长期替代 |

**建议**：先做 A；如果未来要支持"保留多页"，再评估 B（每页独立编码）或 D（容器级复制）。

---

## 8. 建议的实际修复顺序

### 第 0 批（数据完整性，必须先做）

1. **Alpha 解码归一 + 编码声明 + 裁判修正**（§6）。
2. **TIFF 计划剥离 + 结构探针**（§7）。

### 第 1 批（默认路径/必失败/假无损，8 条）

3. AVIF-01 `qp=0`（内省三态默认 + GUI 只下发"有默认/用户改过"的键）。
4. JPEG-01 色度因子移到量化表之前。
5. HEIF-01 删 Main 死档位；**HEIF-03 默认位深改 8**（打包成"HEIF 默认兼容性"一项）。
6. JXL-01 无损锁 `resampling=1`；JXL-03 三个 float setter。
7. WEBP-04 `near_lossless<100` 发 warning 或锁 100；WEBP-01/02 技术/复选框单一真源。
8. AVIF-02 SVT 无损自动切 libaom 或禁用复选框；BMP-02 用 `bitdepth_bps` 比较 + 16→8 告警。

### 第 2 批（系统性可见性/可复现，5 条）

9. `WarningKind` 的 UI 出口（运行行/文件列表/汇总）。
10. `out_root` 排除集（metadata-only 与转码收集都传）。
11. 预设补齐色彩四全局项 + HEIF/AVIF 档位；修 `bitdepth` 缺省。
12. PNG `iCCP` 与 `sRGB/gAMA/cHRM` 二选一（非 sRGB ICC 时只写 ICC）。
13. TIFF `DateTime` 用源生效时间/固定值；HEIF/AVIF 无损语义文案。

### 第 3 批（该修但不阻塞）

14. BMP-01/03（位深只读 + ICC 告警）。
15. TIFF-02/05（删 `ccittrle`/`predictor=float` 或运行前拦截）。
16. JXL-02/04（color_transform 加 auto；codestream_level 默认 auto）。
17. AVIF-04/06/07/08（alpha quality、Main 位深、speed 档位、tune+无损规则）。
18. TIFF-04/07、WEBP-09、HEIF-05、JXL-07/08、PNG-05。
19. `--dev --param` 对运行时内省后端的类型解析（取证链）。

### 第 4 批（打磨/文档，不占修复窗口）

20. P3 文案/标签/行号、空 `eXIf`/`oFFs`、`biSizeImage`（报上游）、死控件统一隐藏。
21. `docs/param-catalog.md` / `design.md` 的"全参数暴露/全部 TIFF 参数"口径改成"裁剪清单"。
22. 把 `X-*` 重复条目合并到格式条目，报告只保留唯一 ID。

---

## 9. 验收清单（最小但足够）

### Alpha

- [ ] `test_alpha_semantics`：alpha=128 时 PNG/WebP/JXL/TIFF 的 RGB 都是 `200,100,50`（不乘 0.5）；alpha=0 的 RGB 不被静默归零（PNG 保留源值）。
- [ ] JPEG/BMP flatten：alpha=128 的合成值 `(228,178,153)`，不是 `(177,152,140)`。
- [ ] `pp_verify` 对 `alpha-jpeg`/`multiformat-split` 的 jpeg 腿有像素断言。
- [ ] `pp_verify` 的 OIIO 读取使用 `oiio:UnassociatedAlpha=1`；TIFF tag1 源做 `unpremult`。
- [ ] 原生字节：PNG IDAT 解码、WebP `WebPDecodeRGBA`、TIFF strip 各一条独立断言。

### 多页 TIFF

- [ ] 3 页 TIFF → TIFF：`directories==1`，无 `SubIFDs`，所有 `StripOffsets+ByteCounts` 在文件内。
- [ ] 2 页 TIFF → TIFF：`directories==1`（现有 P0 回归）。
- [ ] 3 页 TIFF → metadata-only：`directories==3`，三页可解码，strip 载荷 sha 不变。
- [ ] 带 EXIF 缩略图的 JPEG → TIFF：`directories==1`。
- [ ] `test_tiff_probe` 覆盖越界/环/SubIFDs/BigTIFF。

### 全局

- [ ] `ctest --preset release` 全绿。
- [ ] `tests/golden/smoke.py` 20 对全绿（若新增 alpha 断言，只增不减）。
- [ ] `tools/regression.py` zero diff（或按归因登记）。
- [ ] `--ui-smoke` 冻结末行不变（若改 UI 文案/控件，需按 UI 冻结规则更新截图/断言）。

---

## 10. 附：本文引用的关键代码/证据

- Alpha 根因：`src/decode/oiio_reader.cpp:234`；`src/core/simd/kernels.cpp:78-92`；`src/core/colormanager.cpp:979-1009`。
- OIIO 行为：`pnginput.cpp:226-227,365-374`；`pngoutput.cpp:240-241`；`tiffoutput.cpp:828-840`；`tiffinput.cpp:903-904,1359-1367`；`webpinput.cpp:254-257,363-369`；`webpoutput.cpp:132-176`；`heifinput.cpp:389-404`；`imagebuf.cpp:1258,1478`；`imagebufalgo.h:2285-2286`。
- TIFF 根因：`src/core/metadata.cpp:820,376-387,401-426`；`tiffimage_int.cpp:1272-1280`；`tiffimage.cpp:79-82`；产物证据 `build/encoder-audit/tiff/cases/multi3/multi3.tif`（4622 B / 3 IFD）。
- 全量报告：`docs/encoder-config-audit-2026-10-25.md` 的 §4.4、§5.7、§6.4、§6.5、§7.0、§9.7。
