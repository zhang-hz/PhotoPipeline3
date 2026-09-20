# 变更日志

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
- AppImage 打包（`tools/make_appimage.sh`，免安装、免网络、便携）+ 参数化生成的 9 色块应用图标
  + desktop 文件；打包器二进制入库并旁置 SHA512
- 金样断言级 **16 对**（像素 + 元数据值 + warnings 三面断言）；全语料回归基线
  （`tools/regression.sh` + `tools/baseline/golden.log`，规范化后双跑零 diff）
- sanitizer 清扫：ASan/UBSan 全语料矩阵零发现；TSan 并发路径 0 报告（抑制文件附 happens-before 论证
  与阳性对照）
- GitHub Actions 工作流：`linux`（构建 + ctest + 金样/回归）、`ui-smoke`（offscreen 无头冒烟）
  两个 job；AppImage 打包 job 尚未落盘（见 M2 报告）

### 修复

- UI 观感 5 项（M1b 审查遗留）：未选中格式按钮可点性、搜索框左边界、设置页留白、预设空态位置、
  XMP 页缺搜索框
- 时间预览扫描由 GUI 同步改为后台异步（200 文件上限；批变化时取消旧任务，避免乱序回填与冻结）
- 短批次运行不再输出无信息量的 budget 日志（`elapsed < 1s` 守卫）
- 未知编码参数、元数据写失败现在进入用户可见警告通道（此前仅落日志，UI 不可见）
- 非 UTF-8 locale 下预设保存/载入、设置 INI、日志目录的路径往返不一致（统一走 locale 安全层）
- `pp_verify` 元数据文本化不稳定（有理数定形 `a/b`、ASCII trim、数组 `, ` 连接）
- 链接自注册统一为 whole-archive 形态并删除冗余 anchor 符号；UI 冒烟断言改为顺序无关
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
- GitHub Actions 首次真实运行（R15）的结论以仓库 Actions 页面为准（托管运行结果无法本地验证）
