# PhotoPipeline 设计文档 v1.2（含复审修订与 M0–M3 里程碑重排）

> 依据：`docs/brainstorm-consensus.md`（需求共识）+ 设计阶段 5 轮问答（骨架 / 参数系统 / 元数据 UX / 工程面 / 补充）
> 状态：**设计定稿，待实现**
> 五条铁律：禁止降级 · 内部全程 float32 · 全参数暴露 · 小而美 · 无额外图像处理

---

## 1. 系统架构

### 1.1 分层与目录结构

```
PhotoPipeline3/
├── CMakeLists.txt / CMakePresets.json
├── vcpkg.json                     # 图像库依赖清单
├── vcpkg-overlay/jpegli/          # 自建 port：jpegli 作为全程序唯一 libjpeg（替换 vcpkg 的 jpeg 依赖，复审 S2）
├── src/
│   ├── main.cpp
│   ├── core/                      # 引擎层：纯 C++23，零 Qt 依赖，可独立单测
│   │   ├── types.h                # ImageInfo / Warning / Timing / StageEvent
│   │   ├── params.h/.cpp          # ParamSchema 系统（§2.1）
│   │   ├── pipeline.h/.cpp        # 单文件处理序列（§3）
│   │   ├── scheduler.h/.cpp       # worker 池 + 像素预算（§2.5）
│   │   ├── colormanager.h/.cpp    # lcms2 封装（§4）
│   │   ├── metadata.h/.cpp        # 元数据模型与规则合成（§5）
│   │   ├── fsops.h/.cpp           # 镜像路径 / 冲突策略
│   │   └── logger.h/.cpp          # spdlog 封装（§7）
│   ├── decode/
│   │   └── oiio_reader.h/.cpp     # OIIO 解码（全部 10 种输入）
│   ├── codecs/                    # IEncoder 实现（§2.2）
│   │   ├── encoder.h
│   │   ├── enc_jpegli.cpp         # JPEG（google/jpegli，libjpeg62 ABI）
│   │   ├── enc_jxl.cpp            # JPEG XL（libjxl 直连）
│   │   ├── enc_heif.cpp           # HEIF（x265）+ AVIF（SVT-AV1/libaom）
│   │   ├── enc_webp.cpp           # WebP（libwebp）
│   │   └── enc_oiio.cpp           # PNG / TIFF / BMP（OIIO 输出）
│   ├── ui/                        # Qt Widgets（仅依赖 core 接口）
│   │   ├── mainwindow.cpp         # 三区单窗口（§6.1）
│   │   ├── filelistmodel.cpp      # 虚拟化列表 + 异步缩略图
│   │   ├── paramform.cpp          # schema→控件生成引擎（§6.3）
│   │   ├── page_meta.cpp / page_output.cpp / page_run.cpp  # 文件管理并入左侧常驻区（复审 S8）
│   │   ├── exif_editor.cpp        # 单文件元数据编辑器（§6.4）
│   │   └── presets.cpp            # 预设读写
│   ├── mapwidget/                 # 自绘 slippy 地图（§6.5）
│   │   ├── mapwidget.cpp          # 瓦片渲染 / 滚轮缩放 / 点击选点
│   │   ├── providers.cpp          # OSM / 高德 瓦片与搜索
│   │   └── coord.cpp              # GCJ-02 → WGS-84
│   └── platform/
│       ├── mica.cpp               # Win32 DWM SystemBackdrop
│       ├── displayprofile.cpp     # 显示器 ICC 探测（Win: GetICMProfileW）
│       └── paths.cpp              # 便携目录解析（§8.5）
├── tests/
│   ├── unit/                      # 单元测试
│   └── golden/                    # 金样回放（§8.4）
├── assets/
│   ├── icc/                       # 内置目标 ICC（P3 / AdobeRGB）
│   └── i18n/                      # .ts 翻译源
└── .github/workflows/             # CI（§8.3）
```

**依赖方向**：`ui → core ← codecs/decode`；UI 不直接接触任何图像库；core 不含 Qt。
**单一可执行文件**，无插件系统、无脚本扩展。

### 1.2 数据流（转码模式，单文件）

```
FileEntry
  → probe        头信息（尺寸/通道/位深/子图数）+ 像素预算 acquire
  → metadata读   Exiv2：EXIF/XMP/ICC/拍摄时间/GPS/Orientation
  → decode       OIIO → float32；通道保留 {1,2,3,4}（灰/灰+α/RGB/RGBA）；多页取首页+warning
  → orient       Orientation 旋转（启用时，90/180/270+镜像，内存转置）
  → color        lcms2 浮点变换（目标≠保持原样时）
  → flatten      目标格式不支持 alpha 时合成底色（默认白）+warning
  → encode       IEncoder；位深 = 用户显式选择；float→整型标准舍入（无抖动）
  → metawrite    混合路径写入元数据（§5.2）
  → mtime        同步拍摄时间（可开关）
  → release      像素预算释放 → 日志记录 → 状态经 queued signal 回 GUI
```

### 1.3 线程模型

| 项 | 设计 |
|---|---|
| GUI 线程 | Qt 事件循环；worker 只通过 queued signal 回报 |
| Worker 池 | `std::jthread` × N，N 默认=物理核数，设置可调；文件级并行（worker 内串行执行全部阶段） |
| 像素预算 | 全局令牌池：容量 = min(可用RAM×50%, 8GB) ÷ float32 每像素字节数；probe 后按 **2× 帧大小** acquire（旋转/合成需要源+目标双帧峰值，G2），编码完 release；设置里可手动改 GB 数 |
| 取消 | `std::atomic<bool>`，阶段边界检查；当前文件的编码一次性完成（不中断编码器内部） |
| 缩略图 | 独立低并发（≤2）后台生成，不与转码 worker 抢核 |
| 编码器内部线程 | **全部关闭**（libjxl/x265/SVT 均设串行），只用文件级并行——消除超订阅；单文件大图牺牲部分速度（复审 S7） |

---

## 2. 核心抽象

### 2.1 ParamSchema（参数系统——全参数 × 小而美的解法）

四层模型：**Format → Backend → Tech → Param**，每个参数带两个谓词。

```cpp
struct ParamDef {
    std::string key;                 // "distance"
    QString     label;               // "视觉距离 (distance)"
    enum class Type { Int, Float, Bool, Enum } type;
    QVariant    def;                 // 默认值
    double      lo = 0, hi = 0, step = 1;          // 数值范围
    std::vector<std::pair<QString, QVariant>> choices; // 枚举选项
    bool        advanced = false;    // 收进"高级参数"折叠区
    QString     tooltip;             // 中文说明 + 英文术语
    // 谓词——由表单引擎在每次参数变化后重新求值：
    std::function<bool(const ParamSet&)> visible;  // 仅当所属技术/后端/模式选中时显示
    std::function<std::optional<QVariant>(const ParamSet&)> locked; // 无损锁定：返回强制值→控件显示但禁用
};
struct TechDef    { QString id, label; bool losslessCapable; std::vector<ParamDef> params; };
struct BackendDef { QString id, label; std::vector<TechDef> techs; };   // 如 SVT-AV1 / libaom
struct FormatDef  {
    QString id, label, ext;
    std::vector<BackendDef> backends;
    std::vector<int> bitDepths;      // JPEG{8}; PNG{8,16}; HEIF{8,10,12}...
    bool supportsAlpha, supportsGray;
    MetadataPath metaPath;           // §5.2 写入路径
};
```

**规则**：
- 表单引擎按当前 `格式→后端→技术→无损开关` 求值谓词，动态增删/锁定控件——Modular 与 VarDCT 参数互斥显示，勾选无损后相关参数锁定为无损档位值（如 JXL `distance` 锁 0）。
- **参数来源双轨**：jpegli / libjxl / libwebp / OIIO 用静态 C++ 表（类型安全）；**HEIF/AVIF 用 libheif 运行时内省**（`heif_encoder_list_parameters`）转成同一 ParamDef 结构——参数随 libheif 版本自动扩展，升级零改码。
- 一处定义，四处消费：UI 控件生成、预设序列化（JSON）、日志参数快照、编码器调用参数集。
- 高级区带搜索框（一个 QLineEdit 的事）；被改过默认值的参数标 `●`；右键"重置为默认"。
- 目录见 `docs/param-catalog.md`（草案，实现期逐 API 核对定稿）。

### 2.2 IEncoder

```cpp
struct EncodeRequest {
    OIIO::ImageBuf& img;         // float32, 通道 1/3/4
    const ParamSet& params;      // 已按 schema 校验
    int outBitDepth;             // 用户显式选择（UI 已按格式能力约束）
    const MetadataPayloads& meta;// exif/xmp blob + 目标/原样 ICC
    const std::filesystem::path& outPath;
    std::function<bool()> cancelled; // 阶段边界检查
};
struct EncodeResult { size_t bytes; std::vector<Warning> warnings; Timing t; };
class IEncoder {
    virtual const FormatDef& format() const = 0;
    virtual EncodeResult encode(const EncodeRequest&) = 0;
    virtual ~IEncoder() = default;
};
```

### 2.3 元数据模型

```cpp
struct FileEntry {
    std::filesystem::path src;
    SourceProbe probe;                          // 格式/尺寸/通道/位深/多页/时间/GPS/ICC有无
    std::optional<MetadataOverride> exception;  // 单文件例外（§5.1）
};
struct BatchRules {                             // 批量规则（元数据页配置）
    std::optional<TimeShift> timeShift;         // Δ偏移 或 时区语义（§5.3）
    std::optional<GpsData>   gps;               // 手动/地图选点（§5.4）
    std::vector<TagEdit>     exifEdits, xmpEdits; // set/del 任意 key
    bool stripPrivacy = false;                  // 隐私剥除
    bool syncMtime    = false;
};
// 运行时合成：effective(file) = 源元数据 ⊕ BatchRules ⊕ exception
// exception 可逐项覆盖批量规则，或整单"忽略批量规则"
```

### 2.4 ColorManager（§4 详述）

单例；缓存 lcms2 transform（源profile×目标profile×通道数 LRU）。

### 2.5 Scheduler

```cpp
class Scheduler {
    // 提交 Job 列表；worker 从队列取 FileEntry
    // probe → acquireBudget(mp) 阻塞 → pipeline.run() → releaseBudget
    // 事件（queued signal）：Queued/Decoding/Coloring/Encoding/Writing/Done/Failed/Cancelled
};
```

---

## 3. 管线各阶段细则

| 阶段 | 细节 |
|---|---|
| probe | OIIO `ImageInput::open` 读 spec（不解码像素）；Exiv2 读元数据摘要。多页 TIFF / 动图：记录 `isMultiPage`，处理时取首页/首帧 + warning |
| decode | `ImageBuf` 读为 `TypeFloat`；通道保留 {1,2,3,4}；TIFF CMYK：跳过该文件报错（共识：不做 CMYK 输入） |
| orient | 启用时（默认开）按 Orientation 1–8 旋转/镜像，清除标签；关闭时纯像素转码、标签保留 |
| color | 见 §4；**色彩变换统一"任意源→RGB(A)目标"**（复审 S1）：灰度转彩色目标 = lcms2 单调用升维变换（精度无损），无独立灰度色彩管线；源无 ICC 且格式无原生色彩描述 → 假定 sRGB + 日志 info |
| flatten | 目标格式 `supportsAlpha=false`（JPEG/BMP）→ 合成到可配置底色（默认白）+ warning |
| encode | float→目标位深标准舍入（**无抖动**——抖动是处理）；`cancelled()` 在 encode 前最后检查一次 |
| metawrite | §5.2 矩阵 |
| 失败处理 | 每阶段 try/catch；单文件失败标记原因后继续其余文件；不做自动重试；处理顺序 = 添加顺序（显示按文件名排序，G14） |
| 同名冲突 | 同批次不同源映射到同一输出名（`a/x.jpg`+`b/x.jpg`→`out/x.jxl`）同样走冲突策略（G4）；输出根目录位于源树内时添加阶段即警告（G12） |

**已砍的旁路**：JPEG→JXL 码流级无损转封装（`JxlEncoderAddJPEGFrame`）→ v2 再议，v1 管线单一。

---

## 4. 色彩系统

| 项 | 设计 |
|---|---|
| 引擎 | lcms2，float32 缓冲（`TYPE_RGBA_FLT / RGB_FLT / GRAY_FLT`） |
| 意图/黑点 | 固定 `INTENT_RELATIVE_COLORIMETRIC` + `cmsFLAGS_BLACKPOINTCOMPENSATION` |
| 源 profile | 嵌入 ICC（优先）→ 格式原生色彩描述（如 JXL color encoding，经 OIIO 属性核对）→ 假定 sRGB（日志记录） |
| 目标空间 | sRGB / Display P3 / Adobe RGB (1998) / **保持原样**；前三者由 `assets/icc` 打包可再分发 ICC（sRGB 可用 lcms2 内建），来源在文件头注明 |
| 输出嵌入 | 转换时嵌目标 ICC；保持原样且源有 ICC → 原样拷贝；源无 ICC → 不嵌（不无谓增大文件） |
| 灰度 | "保持原样"时原生保留（JPEG/PNG/TIFF/JXL 原生灰度；WebP/HEIF/AVIF 以 RGB 编码 + 日志）；转彩色目标 → lcms2 单调用升维（复审 S1，无独立灰度管线） |
| 缩略图 | ~256px；优先嵌入缩略图（Exiv2 preview / OIIO），否则降采样解码；v1 全平台按 sRGB 渲染（显示器 ICC 探测留 v1.1，复审 S3）；仅内存 LRU |
| 性能 | lcms2 transform 按 (源,目标,通道) 缓存复用 |

---

## 5. 元数据系统

### 5.1 合成规则

`effective(file) = 源元数据 ⊕ BatchRules ⊕ exception`。例外编辑器中每个字段三态：`继承批量规则 / 覆盖 / 清除`；文件列表用徽标标记带例外的文件。所有修改**只写输出文件**，源文件永远只读。

### 5.2 写入路径矩阵（查证事实：Exiv2 对 HEIF/AVIF/JXL 只读）

| 输出格式 | EXIF/XMP 写入 | ICC | 说明 |
|---|---|---|---|
| JPEG / PNG / TIFF / WebP | **Exiv2 后写**（编码完成后对输出文件读写） | Exiv2 | ⚠️ PNG 的 eXIf chunk 支持为验证项 R1：若不支持 → EXIF 关键字段镜像到 XMP + 日志 warning |
| HEIF / AVIF | **libheif 注入**：编码时 `heif_context_add_exif_metadata` / `add_XMP_metadata` | libheif API | 我们是编码方，天然可行 |
| JXL | **box API**：编码时 `JxlEncoderAddBox("Exif"/"xml ")` | box | 始终写容器格式（裸码流无元数据） |
| BMP | 无容器元数据 | 不支持 | 丢弃 + 日志 info |

**载荷生成（G1）**：注入用 EXIF/XMP 载荷由 Exiv2 内存 `.exv`（`ExvMemIO`）产生——合成后的元数据写入内存映像，导出 EXIF TIFF blob；XMP 经 `XmpParser::encode` 序列化为 RDF XML。JXL 的 Exif box 内容 = 4 字节偏移头 + TIFF 数据（libheif 的 `add_exif_metadata` 直接接受 TIFF blob）。

**细节规则**：GPS 有理数编码 = 正值 Rational + 半球 ref 标签 N/S/E/W（G7）；MakerNote 跨容器原样字节拷贝 + 日志说明（G8）；XMP LangAlt v1 只编辑 `x-default` 单值（G15）。

### 5.3 时间偏移

- **Δ 模式**：±年/月/日/时/分/秒（时钟拨错修正）
- **时区语义模式**：`原值按时区 A 解释 → 重写为时区 B 墙钟时间`（旅行照片修正），同步写 `OffsetTime / OffsetTimeOriginal / OffsetTimeDigitized`（EXIF 2.31）
- 作用字段：`Exif.Image.DateTime`、`Exif.Photo.DateTimeOriginal`、`Exif.Photo.DateTimeDigitized` + 已存在的 XMP 对应（`xmp:CreateDate` 等）
- 规则页显示**首个文件前后对照预览**；某文件无时间字段 → 跳过该项 + 日志 warning

### 5.4 GPS

- 字段：经纬度（DMS / 十进制互输）必填；海拔 / 方位 / 时间戳可选（默认折叠）
- EXIF 有理数编码（`GPSVersionID` 等 tag 族齐全写入）
- 地图选点 → 坐标回填；**高德底图选点自动 GCJ-02→WGS-84**（公开迭代逆变换，精度 1–2m，满足照片 GPS 米级精度）；一键清除 GPS

### 5.5 仅元数据模式（输出页顶部模式切换：`转码` / `仅元数据`）

同格式、**零重编码**，只替换容器内元数据，输出到镜像目录（同名文件）：

| 格式 | 实现 |
|---|---|
| JPEG / PNG / TIFF / WebP | Exiv2 无损重写（压缩数据原样保留） |
| JXL | box 级替换（码流 box 原样拷贝，重写 Exif/xml box） |
| HEIF / AVIF | **v1 不支持**：界面置灰 + 提示走转码路径（BMFF iloc 偏移手术风险过高） |

### 5.6 隐私剥除 / mtime

- 剥除 = 删全部 EXIF + XMP，保留 ICC 与像素；`PrivacyMode` 作为 BatchRules 优先级最高的开关
- mtime 同步 = 输出文件修改时间 = effective DateTimeOriginal（无则不动）

---

## 6. UI 设计

### 6.1 主窗口（三区单窗口）

```
┌──────────────────────────────────────────────────────┐
│  ① 元数据  ② 输出  ③ 运行             ← 顶部步骤导航 │
├──────────────┬───────────────────────────────────────┤
│              │                                       │
│  文件列表     │           步骤内容区                    │
│  拖放/递归    │   （元数据页 / 输出页 / 运行页）           │
│  列表+缩略图  │                                       │
│  徽标/计数    │                                       │
├──────────────┴───────────────────────────────────────┤
│  状态摘要              [  开始  ]  ← 底部操作栏          │
└──────────────────────────────────────────────────────┘
```

- **文件管理并入左侧常驻区**（复审 S8）：拖放目标、递归收集、移除/清空、不支持扩展名计数提示全在列表区，无独立文件页，步骤导航 3 步
- 文件列表行：缩略图 + 文件名 + 尺寸 + 格式 + 状态徽标（含"带元数据例外"标记）；搜索过滤（QSortFilterProxyModel）；双击 → 元数据编辑器
- 任意跳步；`开始` 按钮在配置有效时才启用（校验：非空文件集 + 输出目录有效）
- **运行期间锁定**（G5）：转换进行中步骤区禁用，仅运行页与取消可用

### 6.2 三个步骤页

- **元数据页**：批量规则卡片（时间偏移 / GPS / 标签修改 / 隐私剥除 / mtime 同步）+ 时间偏移前后对照 + 例外文件摘要；GPS 卡片内嵌地图选点
- **输出页**：模式切换（转码/仅元数据）→ 格式选择（8 格）→ 全局参数（输出根目录 / 冲突策略 skip·overwrite·rename，默认 rename / 色彩目标 / 位深——位深可选值随格式能力联动）→ 格式参数区（§6.3）→ 预设管理（保存/加载/删除，JSON）
- **运行页**：总进度 + 每文件阶段状态（排队/解码/色彩/编码/写入/完成/失败）+ 实时吞吐统计 + 取消；完成后"打开输出目录 / 查看日志"
- **设置对话框**（工具栏入口，G6）：worker 数 / 内存预算 GB / alpha 合成底色 / 日志级别 / 底图提供方 + 高德 key / 瓦片内存缓存上限 / 关于（版本与许可清单）

### 6.3 参数表单引擎

`paramform.cpp`：输入 ParamSchema，输出 QFormLayout。行为：

1. 顶部：后端选择器（如有多）→ 技术选择器（如有多）→ 无损开关（如技术支持）
2. 核心参数直接展开；"高级参数"折叠区（带搜索框）
3. 谓词求值：任何参数变化 → 重算全部 visible/locked → 增删/启停控件（锁定控件显示强制值）
4. 偏离默认值的参数标 `●`，右键重置
5. 位深/色彩目标等格式级参数放全局区（不随技术切换重置）

### 6.4 单文件元数据编辑器（对话框）

- 左：EXIF 分组树（IFD0 / Exif / GPS + 搜索框；MakerNotes 只读展示）；右：值编辑（按 Exiv2 类型校验）
- 底部："按编号添加任意标签"（hex → Exiv2 key，类型下拉）
- XMP 页签：dc / photoshop / xmpRights 常用字段 + 自定义 `Xmp.xxx.yyy` 路径入口
- 时间 / GPS 页签：该文件的时间偏移与 GPS 例外覆盖（三态）

### 6.5 地图控件

- 自绘 slippy 地图：在线瓦片（内存缓存，会话级）、滚轮缩放、点击选点（十字标）、地名搜索
- 底图提供方下拉：**OSM（内置，Nominatim 搜索，免 key，默认）** / **高德（Web 服务 key 自备，坐标自动 GCJ-02→WGS-84）**
- 断网时地图区降级为手动坐标输入；转码流程本身完全离线

### 6.6 平台视觉

- Qt 6.8 LTS（Win11 Fluent 风格内建于 6.7+）+ **Mica**：`DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE, DWMSBT_MAINWINDOW)`，Win11 25H2+ 恒可用
- 深浅色跟随系统 + `DWMWA_USE_IMMERSIVE_DARK_MODE` 标题栏同步；HiDPI 由 Qt 处理
- 界面中文（全量 `tr()` + `.ts`，预留 i18n）

---

## 7. 日志设计

- spdlog → `数据目录/logs/run-YYYYMMDD-HHMMSS.log`，保留最近 20 次，英文，结构化 `k=v`；warning 及以上即时 flush（崩溃不丢关键日志，G13）；不做 minidump
- 行格式：`HH:MM:SS.mmm [lvl] [tid] [stage] [file] message {k=v ...}`

**点位清单（埋点枚举）**：

| 类别 | 点位 |
|---|---|
| 运行头 | app/Qt/OIIO/jpegli/libjxl/libheif/x265/SVT-AV1/libaom/Exiv2/lcms2 版本；全部参数快照（格式/后端/技术/无损/全部参数值/位深/色彩目标/冲突策略/批量规则） |
| 每文件·probe | 源路径、格式、尺寸、通道、源位深、多页标记、ICC 有无、拍摄时间、GPS 有无 |
| 每文件·分段耗时 | decode / orient / color / flatten / encode / metawrite 各自 ms |
| 每文件·输出 | 输出路径、字节数、压缩比、位深、色彩变换记录（源profile→目标profile）、实际参数覆盖快照 |
| 每文件·警告 | 位深降档、无损→有损、多页截断、alpha 合成、无 ICC 假定 sRGB、元数据丢弃（BMP）、时间字段缺失跳过 |
| 运行尾 | 成功/失败/跳过计数、总耗时、总吞吐 MB/s、平均单文件耗时 |

---

## 8. 工程与构建

### 8.1 工具链（按用户实际节奏：**先 Linux 跑通，再切 Windows**）

| 平台 | 编译器 | Qt | 图像库 |
|---|---|---|---|
| Linux（M0–M2） | GCC 14+ / Clang 18+ | **aqt 统一拉取官方 6.8 LTS 预编译**（两平台/CI 同源，消除 apt 6.4 与 Win 6.8 的 API 偏差雷区） | vcpkg（baseline 钉死） |
| Windows（M3） | MSVC 2022（最新） | **aqt 统一拉取官方 6.8 LTS 预编译** | vcpkg（同 baseline） |

工程工具链（M0 第 1 天就位）：clang-format + clang-tidy（bugprone/performance 基础集）+ ccache + PCH；dev preset 默认 ASan/UBSan，另有 tsan 档；vcpkg binary cache（依赖 40 分钟编译只发生一次）。

C++23；CMake ≥3.28 + presets。

### 8.2 vcpkg 依赖与验证项

| 库 | port | 状态/动作 |
|---|---|---|
| OIIO | `openimageio` | ⚠️ 验证 features：libjxl/libheif/webp/gif/targa 插件是否默认启用；TIFF 的 zstd 压缩依赖 libzstd feature（R2/G11） |
| jpegli | **自建 overlay port**（google/jpegli，CMake） | **作为全程序唯一 libjpeg**：overlay 让 `jpeg` 依赖指向 jpegli（替换 libjpeg-turbo），OIIO 亦链接之——消除双 libjpeg 同名符号冲突（复审 S2，两者导出符号完全同名，静态共存必炸）；OIIO 的 JPEG 解码同步升级为 jpegli 浮点解码 |
| libheif | `libheif[x265,aom]` | ⚠️ 查 svt-av1 feature 是否存在，缺则 overlay（R2） |
| Exiv2 | `exiv2` | ⚠️ 确认 `enable_bmff=1`（HEIF/AVIF/JXL 读取的前提）（R2） |
| libjxl / libwebp / lcms2 | 直接 | ✅ |
| 链接策略 | 图像库尽量静态；**libheif 因 LGPLv3 动态链接**（Windows zip 携带 DLL，AppImage 打包） | R8 |

### 8.3 CI（GitHub Actions 双矩阵）

- `build-test`：**M0 起仅 ubuntu-24.04**（Linux 先行）；Windows 矩阵在 M3 加入；vcpkg binary cache；Link Probe + 构建 + 单元 + 金样回放 + offscreen UI 冒烟（M2 起）
- `release`：M2 出 `PhotoPipeline-x86_64.AppImage`（linuxdeploy + qt 插件，G10）；M3 起追加 `PhotoPipeline-win64.zip`（windeployqt）；打 tag 自动产出

### 8.4 测试

- **单元**：ParamSchema 谓词引擎、时间偏移计算（含跨月/跨年/时区语义）、GCJ-02↔WGS-84、镜像路径与冲突策略、色彩变换数值（已知 ICC 对的金值）、元数据合成（规则⊕例外）
- **金样回放（分级，复审 S5）**：`tests/golden/<format>/{in.*, expected.json}`；**M1 冒烟级**（8 对：转码→OIIO 回读→PSNR 容差 / 无损路径逐位相等）；**M2 断言级**（追加元数据字段值、警告清单、灰度/alpha/多页边界样，共 ~11 对）
- **语料库**（M0 生成，checksum 入库）：oiiotool 合成 ~25 fixture——16bit TIFF / 灰+α PNG / P3 JPEG / 渐进 JPEG / 多页 TIFF / 动图 GIF/WebP / CMYK 负例 / **截断损坏负例×2 / 中文 emoji 文件名**；`tests/golden/real/` 放用户提供的真实样本（iPhone HEIF、扫描 TIFF 等）
- **回归基线**：全语料 `--dev` 日志存档，debug 期间每修一 bug 全量 diff——"改了什么坏了什么"一眼可见

### 8.5 落盘（便携优先）

```
exe 目录可写 → 便携模式: ./settings.ini  ./presets/*.json  ./logs/
exe 目录只读 → 回退:      %APPDATA%/PhotoPipeline 或 XDG ~/.local/share/PhotoPipeline
```

### 8.6 dev-only 测试入口

CMake 选项 `PP_BUILD_DEV`（Release 关闭）。开启时 `photopipeline --dev <输入> <输出目录> [预设.json]` 跑完整管线 + 全量日志——M1 验收与日常调试入口；发布构建不含此代码路径，与"不做产品 CLI"共识不冲突。附加：`PP_LOG_LEVEL` 环境变量切换日志级别（免重编）；debug 模式默认 overwrite 冲突策略，保证幂等重跑。

---

## 9. 里程碑与工程纪律（重排：M0–M3，大爆炸式开发）

> 开发模式：M0 把**所有外部不确定性**炸干净 → M1 **一次性完成全部编码** → M2 快速 debug 收口 → M3 Windows 独立 bring-up。
> 大爆炸式 M1 成立的唯一前提：**鼓点纪律**——每落一个模块，`--dev` 全语料跑一遍，绝不允许 7k 行代码"首次通电"。

### 9.1 阶段定义与出口准则

**M0 · 环境 + 库 + 编译准备**

| 内容 | 出口准则 |
|---|---|
| vcpkg baseline 钉死 + binary cache + jpegli overlay 拦截 `libjpeg-turbo` port | 全新 Linux 机一条命令 `cmake --workflow --preset fresh` 全绿；cache 命中后重建 < 5 min |
| Link Probe 二进制（每库调一个函数） | 全绿；作为永久 CI 哨兵 |
| **六 Spike**（见 9.2） | 全绿 |
| 测试语料库生成（§8.4：~25 合成 fixture + 负例 + unicode 文件名 + real/） | checksum 入库 |
| 参数表对照真实 API 头文件定稿，C++ 表编译进库（未使用） | catalog 与头文件一致 |
| `expected.json` 断言 schema 冻结 | — |
| clang-format / clang-tidy / ccache / PCH / CI(Linux) / aqt Qt 6.8 | CI 从第一个 commit 全绿 |
| 空 Qt 窗口骨架 + dev harness 空壳 | 可运行 |

> **M0 已完成（SUB-G 独立审计 9/9 PASS）**：六 Spike 全绿、语料 27 fixture 字节稳定、参数表 78 条编译进库、linkprobe 9/9（双 AVIF 后端运行时在列）、fresh 链路 8.12s（cache 命中）、冷编译 3.86s、ctest 6/6、冻结接口逐字节一致。工程事实与全部 api-deltas 见 docs/m0-tasks.md §17。

**M1 · 全部编码（Linux，底向上 + 鼓点）**

- 编码顺序：types → params → fsops/logger → decode → color → **编码器一天一个** → metadata → pipeline → scheduler → harness → UI（地图控件最后写、带手动坐标降级路径，卡住不阻塞收口）
- **鼓点纪律（强制）**：每落一个模块，`--dev` 全语料跑一遍
- **单测随码走**（是 M1 的组成部分，不是 M2 的作业）；接口（`types.h`/`encoder.h`/`params.h`）第一天冻结，此后不改签名
- UI 只用成熟模式（QStackedWidget / QAbstractListModel / QDialog）；断言策略：内部不变量全 assert（通道∈{1,2,3,4}、位深∈集合、预算非负）
- `TODO(M2)` 标签纪律：写码时觉得糙的地方全打标，M2 开工即有 debug 命中清单
- 取消语义 checklist：像素预算 acquire 处必须检查取消标志（防死锁）
- **出口准则**：全部模块落库；全语料 `--dev` 零崩溃、8 格式全部出图、日志完整；UI 手动走查通过；`TODO(M2)` 清单归档

**M2 · Linux debug 收口**

- dev preset 默认 ASan/UBSan；harness 跑 TSan；`PP_LOG_LEVEL` 免重编切级
- 回归基线 diff（§8.4）；`QT_QPA_PLATFORM=offscreen` UI 冒烟进 CI；AppImage 打包烟测进 CI
- 预置 bug 分类学探针：通道/位深断言、色彩金值、元数据往返、unicode 路径、并发、model/view 六类各有现成测试
- **出口准则**：金样断言级全绿（~11 对）；ASan/UBSan/TSan 干净；offscreen 冒烟绿；AppImage 烟测绿；`TODO(M2)` 清零

**M3 · Windows bring-up + 发布**

- MSVC 工具链 + vcpkg（同 baseline）+ aqt Qt 6.8 + Windows CI 矩阵加入
- windeployqt + zip 打包（libheif 动态 DLL 携带）；Mica / 深色标题栏实装；Win11 25H2 实机冒烟
- **出口准则**：Win CI 全绿；干净 Win11 25H2 上 zip 解压即用；双平台 release tag 产物齐

### 9.2 M0 六 Spike（风险前置引爆）

| Spike | 内容 | 退役风险 |
|---|---|---|
| A | OIIO 解码全部 10 种输入样本 | R2（插件 features） |
| B | jpegli 编码 JPEG → OIIO 回读成功 | R12 运行时证明（libjpeg 统一） |
| C | libheif 编码 HEIF/AVIF + EXIF blob 注入 → Exiv2 读回 | G1 载荷管线 / R1 尾巴 |
| D | libjxl 编码 + Exif box → Exiv2 读回 | G1 / R13 |
| E | lcms2 浮点变换数值金值校验 | 色彩管线地基 |
| F | Exiv2 无损重写 JPEG 副本，压缩字节逐位不变 | R10（仅元数据模式地基） |

### 9.3 Big-bang 风险登记（BB 系）

| # | 风险 | 缓解 |
|---|---|---|
| BB1 | M1 零集成点 → M2 爆炸 | 鼓点纪律（强制，§9.1） |
| BB2 | 依赖版本中途漂移 | baseline 钉死；M1/M2 禁止升级窗口 |
| BB3 | MSVC 编译问题（jpegli/OIIO on MSVC） | 独立 M3 隔离（用户决策） |
| BB4 | EXIF/XMP blob 格式不匹配（字节序/TIFF 头） | Spike C/D 在 M0 引爆 |
| BB5 | Qt 双版本 API 偏差 | aqt 统一 6.8 LTS（用户决策） |
| BB6 | 参数表与实际 API 不符导致返工 | M0 对照头文件冻结参数表 |
| BB7 | 取消/像素预算死锁 | acquire 处检查取消；M2 TSan 复核 |
| BB8 | UI model/view 契约 bug | 简单成熟模式 + offscreen CI 冒烟 |
| BB9 | 色彩数值跨版本漂移 | lcms2 随 baseline 钉版本；金样容差断言 |
| BB10 | Windows 打包 DLL 地狱（libheif 动态链接） | M3 打包烟测（干净系统解压即用） |

Windows 构建切换点：**M3**（前三个阶段纯 Linux，用户在 M3 切换 Windows 实机）。

---

## 10. 风险登记册（实现期持续更新）

| # | 风险 | 状态 | 缓解 |
|---|---|---|---|
| R1 | PNG 的 eXIf chunk 写入（Exiv2 官方矩阵 PNG EXIF="-"） | **开放** | 实现期验证；备选：EXIF 关键字段镜像 XMP + warning |
| R2 | vcpkg feature 缺口：jpegli 必自建 port；libheif svt-av1 feature、OIIO 插件 features、Exiv2 bmff/xmp | **退役（M0 实测）** | 四处缺口全部落地：jpegli 拦截 port（含 libjpegli.a 静态直调层）、libheif[hevc,aom,svt-av1]（overlay 补 feature + WITH_SvtEnc 内置）、OIIO[jpegxl,libheif,webp,gif,tools]（overlay 恢复 FindJXL.cmake + 补静态闭包）、exiv2[bmff,xmp]；linkprobe 9/9 |
| R3 | jpeg-li 12 位 JPEG | **已关闭** | 查证：仅 8 位输出；16 位源→JPEG 降档警示 |
| R4 | libheif 插件（x265/SVT/libaom）实际暴露参数面 | **已接受-库面（M0 退役）** | 运行时内省通路实测可用（3 编码器在列）；参数表 heif/avif 走 runtime_introspected |
| R5 | OIIO PNG/TIFF 输出参数覆盖面 | **已接受** | OIIO 即该两格式的"库"，暴露其全部输出参数（M0 定稿：PNG 1 / TIFF 5 条真实参数，无后端项已剔除） |
| R6 | 高德瓦片直连 ToS 灰色 | 接受 | OSM 内置兜底 |
| R7 | GCJ-02 逆变换精度 1–2m | 接受 | 照片 GPS 本身米级 |
| R8 | libheif 动态链接部署（Windows zip / AppImage） | **降级：Linux 段退役（M0 实测）** | x64-linux triplet 实为 static（53 个 .a，唯一 .so=libjpeg.so.62 兼容层）→ Linux 近全静态；仅 Windows 打包段保留本风险 |
| R9 | 缩略图显示器 ICC | **已决策（复审 S3）** | v1 全平台假定 sRGB；`GetICMProfileW` 留 v1.1；影响面仅缩略图，与转码精度无关 |
| R10 | Exiv2 无损重写 PNG/TIFF 时压缩数据保真 | **JPEG 段退役（Spike F 双判定 PASS）；PNG/TIFF 段开放** | spike f：SOS 尾字节逐位一致 + 像素 hash 相等；PNG/TIFF 金样断言留 M2 |
| R11 | OIIO→libheif 编码的 float→int 双重转换损耗 | 低 | 统一在 codecs 层做一次 float→目标位深转换 |
| R12 | 双 libjpeg 符号冲突（OIIO ↔ jpegli 导出符号同名） | **已解决（复审 S2）+ M0 实测补证** | jpegli 为唯一 libjpeg：解码实现即 jpegli（损坏 JPG 报错出自 lib/jpegli/decode_marker.cc）；双库并存形态——libjpeg.so.62 兼容层（OIIO/tiff）+ libjpegli.a 直调层（本项目编码器，CMake target libjpeg-turbo::jpegli-static 带 hwy 闭包） |
| R13 | 无 ICC 的 JXL 源：色彩描述经 OIIO 的暴露方式未验证（G9） | **缩窄（M0）** | JXL box（Exif+xml）写读通路实测 OK（UseBoxes→4 字节 offset 前缀→CloseBoxes）；仅剩"OIIO 色彩编码属性暴露"M1 核对 |
| R14 | jpegli 上游无 tag/release（新增，M0 事实） | 已接受 | overlay 钉 main HEAD commit SHA（031a0077）+ SHA512；升级需人工重钉并回归 spike B |
| R15 | CI 未实跑：ubuntu-24.04/gcc-13 与本机 26.04/gcc-15 组合未验证（新增，D5） | 开放 | GitHub 首跑验证；binary cache 不跨编译器共享；D1 修复后 gen_corpus 行已对齐 |
| R16 | 语料 TIFF fixture 字节稳定性（新增，D2） | 已解决 | 固定 DateTime=2024:01:01 + 同名同参 → 同机字节稳定；跨机器不保证 → CHECKSUMS 本地生成、不入库 |
| R17 | exiv2 0.28.8 enableBMFF 已 [[deprecated]]（新增） | 低 | 仅 2 条编译告警，运行时功能正常；M1 升级 Exiv2 时换新 API |

---

## 11. 附录：决策溯源

- 需求层决策 → `docs/brainstorm-consensus.md`（4 轮需求问答）
- 设计层决策 → 本文档（5 轮设计问答：骨架 / 参数系统 / 元数据 UX / 工程面 / 补充）
- 参数清单草案 → `docs/param-catalog.md`
