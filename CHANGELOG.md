# 变更日志

## [0.3.3] — 2026-10-12

**M7 工程收口轮（M7-WP1…WP11）**：发布前深度评审（`docs/deep-review-0.3.2.md`，331 条 R-id）与
UI 14 项评审（`docs/ux-review-14.md`）的批量整改 + 结构重构。两条口径：① **数据安全双 P0 根治**
（输出路径不得命中源文件；元数据写回原子化、失败=失败不再报成功）；② 整改与重构以金样
**20/20** + 全语料回归 **zero diff** 背书（默认行为零变化；能力类新项一律 opt-in）。
331+14 逐条覆盖台账见 `docs/m7-report.md` 附录，开发期裁定汇总见 `docs/m7-adjudications.md`。
版本单源 = `CMakeLists.txt:19` = **0.3.3**。

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

### 变更

- 仅元数据模式 GUI 默认冲突策略 **Rename → Skip**（不再静默产 `(1)` 副本）；CLI/金样默认面不动
- 终态词表统一为「已完成 / 部分成功 / 跳过 / 失败 / 已取消」（列表胶囊「完成/取消」随之更名）
- 格式页签文案去「· N 位」、页签等宽（位深控制功能原样保留）
- 滚动条 10px 实心轨 → 6px 细浮层（含弹窗同皮）；按钮内距 11/14 → 12 单源
- 日志 `[file]` 标签随 TU 拆分迁移（`[main.cpp]` → `[dev_cli.cpp]` 等，机械）
- 回归基线两次重播种（逐行归因见提交 `d882fe6` / `59de09d`；heif 档位 info 行 46 行 +
  TU 标签迁移 180 行 / 消双记 4 行）
- `tests/golden/SCHEMA.md` 加性扩展告警断言字段（v1 字段全保留）；expected.json 仅增不改

### 已知

- **线程读数为估算口径**（R-078）：「总活跃 ≤ T」成立的是 `active ≤ W ≤ T` 与 `ΣE ≤ T`；
  worker 主线程 + 编码线程两族之和在 `W*2 > T` 分支可达 2W，且 **SynthPump 采样线程与
  x265 库内线程池不入账**。`RunSummary::threads_total_est` 是两族峰值之和的**估算**，
  不是硬上界；UI 读数未接线处显示「—」（不编数）
- **色彩可选项未实现**（色域映射 / 8bit 抖动 / 渲染意图 / 线性光重采样）：接口数据面
  （`RunConfig`/`ColorOptions` 四字段）已接线且默认值=现行为，ColorManager 侧消费**未入树**
  （该波次未交付）——界面不提供开关，行为与 0.3.2 逐字一致
- UX-14 A1 残余：P3 目标下码流 VUI CICP 实测落 unspecified(2)（以 ICC 为准、不矛盾）；
  精确 12 需给 libheif 传 `output_nclx_profile`（会改输出字节，待裁定）
- Windows 真机验收清单（拖拽/双击最大化/八向缩放/Snap/三钮/圆角/多显示器 DPI 等 10 项）
  须人工签收；offscreen `--ui-smoke` 无法替代
- Linux 回归基线（`tools/baseline/golden.log`）仅随版本 bump 定点替换 12 行
  `version {lib=app version=…}`（0.3.2 → 0.3.3，同 `6d98e2e` 先例做法）；M7 的行为行
  （TU 标签迁移 180 行 + heif profile 46 行等）**未移植** —— 须在 Linux 宿主全量重播种后
  确认（R-015 纪律）；发行后首个 nightly 必看
- 深度评审 P1 面余量（R-025/026/066/068/072 等）与结构性债（page_meta/page_output/mapwidget
  拆分、RTL、契约强类型化）逐条登记在 `docs/m7-report.md` 覆盖台账，未静默丢弃

### 发行

- Windows：`PhotoPipeline-0.3.3-win64.zip` —— **34,727,624 B**（本机 sha256 `4a3ec25d…`，105 条目；
  zip 不承诺字节可复现，CI 产物指纹另计），`make_winzip.py` 五项门禁（A 闭包 / B 来源 /
  C 结构 / D 许可 / E-1 启动版本断言）
- Linux：`PhotoPipeline-0.3.3-x86_64.AppImage` + `PhotoPipeline-0.3.3-deps.txt`
  （`tools/appimage-check-closure.sh` 闭包清单）—— 由 tag 触发的 `release.yml` `appimage` job
  在 Linux runner 产出（本机 Windows 不构建）
- 第 4 附件：`PhotoPipeline-0.3.3-SHA256SUMS.txt`（三个发布件的 sha256，逐字取自下载件）
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
- 「已发行」终态与 CI 产物指纹留待**发行后收口**提交登记（`d049d22` 先例）；2026-10-12 实况：
  release #13（tag `v0.3.3` → `b1af371`）winzip job 撞 `timeout-minutes: 20` 硬顶被取消
  （build 单步 19m11s 未完）、publish 跳过 ⇒ GitHub Release 未创建，处置待主对话裁决
  （全链见 [docs/m7-report.md](docs/m7-report.md) §3.8）

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
