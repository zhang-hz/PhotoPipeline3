# 0.4.2 修复批（M15）收口报告 — 2026-11-04

> 性质：0.4.2 正式版发行后用户反馈 5 项缺陷的修复批。**不升版本号**（`--version` 仍
> `PhotoPipeline 0.4.2`），不移动 `v0.4.2` tag / 不重发 GitHub Release；交付 = 修复后的源码
> 工作树 + 本地发行构建 `dist/PhotoPipeline-0.4.2-win64.zip`。
> 契约 = 本批用户反馈（5 条，见 §1 映射）；裁定 = `docs/m15-adjudications.md`（A1–A13、U1–U6）。
> 回归口径：证据只增不减；冻结行/冻结截图集逐字不变；金样与夹具零改动。

## 1. 用户反馈 → 根因 → 修复（逐条）

| # | 用户原文（摘要） | 根因 | 修复 | 证据 |
|---|---|---|---|---|
| 1 | HEIC 提供无损选项（编码无损，作为质量/CRF/QP 之外的第四种模式），同时 profile、色度采样、level 等仍可配置 | HEIF 无损只有独立勾选框入口；G3 规则 Lock chroma=444 + enc_heif 无条件强制 444 + page_output 强制改写/仅 444 可选 ⇒ 无损下色度/档位不可配；RC 下拉无第 4 档 | RC 下拉加「无损」第四档（与勾选框同源，唯一真值 `sel.lossless`）；删除 chroma Lock/强制改写；无损 grid 按所选 chroma 推导色度面；非 444 如实中文警告；profile/level 保持可配（level 因 x265 硬约束改为「可配但不下发 + 如实告警」，见 A4） | `test_param_rules`/`test_heif_*`/`test_enc_oiio`/`test_format_presets` 6/6 PASS；444 hvcC chroma=3 可解码；420 + 无损 → 成功 + chroma=1 + 中文 warning + 可解码；10bit/420/Main10 → hvcC `profile_idc=2`（用户档位真正下发）；无损+level31 → hvcC `level_idc=255` + 两条 warning |
| 2 | 多选后右侧显示多个值，「多个值」框界面异常 | 多选 mixed 时时间卡「时区语义」两个 QComboBox 只加 ppMixed 虚线边框，**文本仍显示首值**（0.4.2 无 mixed 文本显示面） | 新增 `meta::MixedComboBox`（paint 级覆盖 `initStyleOption.currentText` = 「多个值」，零 item 模型/索引改动；常态委托基类逐像素不变）；`apply_mixed_combo` 统一驱动 | 探针 `mixed-combo-display`/`mixed-combo-activate` bad=0；真实平台截图 12 张齐 |
| 3 | 单击「多个值」框后软件闪退 | **本机未复现**（25+ 交互点 × 3 轮真实事件链全程存活、无 WER 事件）。同批复核发现并修复：mixed 提交统一到全部选中文件（修复探针焦点投递后验证）；3 处防御性收口（`refresh_gps_warning` 空指针守卫 / 时间多选小表行数夹在 items 长度内 / info 单文件 probes 空表守卫） | 15 条机械交互探针（spin 文本区/上下箭头、seg 同档、时区 popup+activated、focusOut、partial 开关、pending 窗口、连续点击、GPS/Tags/信息卡） | `meta-selection: checked=34 bad=0`；未复现条件如实登记 A6/U1 |
| 4 | JXL 编码效率严重退化（600s+），检查编译与运行时调度 | 0.4.2 规划器在多输出 P>1 时按 `e=floor(每文件预算/P)` **对所有 target 名义均分**；jpegli/OIIO 无内部线程、heif/x265 用固定池，只有 jxl/avif 真吃 E ⇒ JXL 从 16 被压到 8/4/2；再乘 JXL default 预设自适应（最多 30 轮重编码）放大成数量级退化。enc_jxl/vcpkg/编译链零 diff | 规划器按编码器能力**逐输出分配**（heavy 编码器分走余下预算，flat 保底 1；`RunScope` 末尾加性 `encode_threads_per_output`）；W×P×E≤T 旧不变式零放宽；单输出/P==1/深队列路径与 0.4.2 逐字一致 | 单轮 A/B（1 文件 T1.tif，jxl+png+jpeg，T=16）：旧口径 plan `5/5/5`（JXL E=5）→ 修复后 `14/1/1`；JXL 单轮 encode_ms 交错中位 **12320ms → 5096ms ≈ 2.4x**；多输出+自适应 Σ轮耗时 23.1s→17.7s（1.31x）；产物 SHA256 修复前后逐对相同（jxl `3EB4780D…` / `6751E358…`、png `5FF0ABEC…`、jpeg `8BC76472…`）；`test_thread_plan` matrix 26400 / contract 6560 fail=0 |
| 5 | 0.4.2 在部分 Windows 版本上，打开后鼠标转圈、无响应；提高 GUI 版本兼容性 | 0.4.2 唯一新增启动期阻塞系统调用 = `PreviewPanel` 构造（MainWindow 构造期、show 前、GUI 线程）同步调用 `system_display_profile()`（GetDC+GetICMProfileW → 读 ICC → lcms2）；部分 Windows 环境该调用长时间阻塞 → GUI 卡死。0.4.1 无此路径 | 构造期立即注入 sRGB 回退、系统 profile 后台线程获取 + QTimer 轮询（generation 丢弃过期、5s 软超时/30s 硬上限、析构不 join、off 平台不启线程）；ICC 读取 stat + 8MB 上限；屏幕变更同走异步；mica DWM 背景按 Win11 22H2 门控 + 启动日志记录 OS build/Qt/平台；启动期 preset 分项计时仅诊断模式输出 | `PP_DISPLAY_ICC_DELAY_MS=5000` 模拟：`shown=1399ms < 2s`、延迟期间 `WM_NULL` 响应 0ms、后台 5011ms 完成换档日志；`delay=8000` 走 5s 软超时且窗口可响应；`test_color` 1542 checks/0 failed；真实/offscreen ui-smoke 均通过冻结行 |

## 2. 验收读数（命令 + 结果）

### 2.1 单测 / 功能门禁（集成树 `build/m15-integration`，release-dev）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m15-integration -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m15-integration -j 10
python tools/env.py run -- ctest --test-dir build/m15-integration --output-on-failure
```

- **ctest 52/52 passed**（最终全量轮 Total 222.37s；其中 `ui_smoke` 通过、`test_enc_oiio` 通过）。
- **金样 20/20**：`SMOKE total=20 pass=20 fail=0`（`tests/golden/smoke.py build/m15-integration`）。
- **回归 zero diff**：`regression: normalized 2567 lines vs baseline 2567 lines` / `zero diff (baseline reproduced)`。
- **`--ui-smoke` 冻结行逐字**：`UI-SMOKE OK shots=12 pages=3`；`folds: checked=11 bad=0`、
  `m8-params: checked=107 bad=0`、`meta-selection: checked=34 bad=0`、`layout-audit 1124/0`。
- **格式门禁**：`bash tools/ci-format.sh` → `check 246 files … violations=0`（钉版 clang-format 23.1.1）。
- **静态分析棘轮**：`python tools/env.py run -- python tools/ci-tidy.py build/m15-integration`
  → **TIDY total=253 baseline=262 files=76 violations=0 seed=0（未 --update）**。
- **发行树 ctest（release，非 dev）**：**51/51 passed**（最终重建后 Total 25.73s）。

### 2.2 发行构建与打包

```bash
python tools/env.py run -- cmake --build build/release -j 6          # release 预设（PP_BUILD_DEV=OFF）
python tools/env.py run -- python tools/make_winzip.py dist --smoke-exe build/m15-integration/photopipeline.exe
```

- `build/release/photopipeline.exe` `--version` = **`PhotoPipeline 0.4.2`**。
- **打包五门禁全绿**：
  - 闭包门禁 A：68 PE / 105 导入名 → staging 51 · apiset 17 · system 37（无 missing）；
  - 来源门禁 B：5 个 `Qt6*.dll` 与 Qt 工具链 sha256 逐一一致；
  - 结构断言 C：exe / qwindows / qoffscreen / imageformats=4 / tls=2 齐备；
  - 许可断言 D：`licenses/` 35 个 copyright（≥30）+ `PhotoPipeline/LICENSE`；
  - 工厂预设门禁：8 格式 default.json 齐备 · JSON 总数 11 · 无 `_builtin-` 残留（写盘前 + zip 内各一次）；
  - 烟测 E-1 `PhotoPipeline 0.4.2`(exit 0) · E-2 `UI-SMOKE OK shots=12 pages=3`(offscreen) ·
    E-3 `WIN-LAUNCH OK windows=6 visible=1`（真实 qwindows 平台）；
  - 产物内复核：解压即用 `--version` OK、licenses 35。
- **产物**：`dist/PhotoPipeline-0.4.2-win64.zip`
  **35,397,625 bytes**，`sha256 = 585cabde64e2aa2aaa7000a65a052c77c94ef0fe650d7773827fe8147fa234b0`，条目 118。

### 2.3 逐问题专项证据

- **HEIC 无损（task-1）**：`build/heic-lossless/evidence/{444,420,profile-main10,level31}`
  （产物 + 日志 + hvcC 读数）；444 无损 `hvcC chroma=3` 且解码 exit 0；420 无损成功 +
  `chroma=1` + 日志原文 `[warning] 无损模式 + 色度 420：色度在编码前被下采样，YCbCr 码流无损但
  不等于像素级无损（需要像素级无损请把色度采样设为 4:4:4）`；Main10 档位 `profile_idc=2`；
  `level-idc=31` → `level_idc=255`（无损固定 8.5）+ 两条如实 warning。
- **元数据 mixed（task-2）**：`build/meta-ui/{repro3.log, real-smoke.log, ctest-meta2.log, shots/}`；
  `PP_MIXED_REPRO=1` 快路径 20 步交互全过；真实平台 12 张截图齐、`meta-selection checked=34 bad=0`；
  闪退未复现（3 轮 × 25+ 交互点、无 WER 事件），如实登记未决。
- **JXL 调度（task-3）**：`build/jxl-perf/ab_run.ps1` + `.cache/jxlperf/ab/*`；A/B 读数表见 §1 #4；
  `PP_THREAD_PLAN_LEGACY=1` 默认关闭（同二进制 A/B 诊断）；dev_cli 的临时 A/B 放宽已回退（`git diff` 无该文件）。
- **启动去阻塞（task-4）**：`build/win-startup/verify-startup-async.ps1`
  （`-SkipUiSmoke` 秒级跑启动段）；读数见 §1 #5；非诊断模式无新增日志行（回归零漂移实测）。

## 3. 偏差与追认

逐条见 `docs/m15-adjudications.md`：A1（`RunScope` 末尾加性）、A3/A4（HEIC 无损语义与 Level 诚实降级）、
A5（MixedComboBox 显示面）、A6（启动异步化语义变化）、A7–A9（探针换锚，只增不减）、A10（日志面零新增）、
A11（Win 版本门控）、A12（不升版本）、A13（dist 口径）。**无冻结面重排、无断言放宽、无金样/夹具改动。**

## 4. 未覆盖 / 未决（如实）

- **「单击多个值框闪退」本机未复现**（任务 2）：已覆盖 25+ 交互点 × 3 轮（真实事件链），未覆盖
  真实 OS 级拖选/双击选词/滚轮/IME/触摸板/键盘 Tab 全链/右键菜单；建议用户侧开 WER LocalDumps
  拿 Qt6Widgets 偏移或 nightly ASan 跑该交互段。防御性收口不等于根因证明。
- **「部分 Windows 版本」启动卡死**：本机 Win11 26100 无法复现（GetICMProfileW ~2ms）；根因归属为
  排除法 + 异步化后「即使系统调用慢/挂也不阻塞 GUI」的构造性保证（5s/8s 模拟实测）。具体
  Windows 版本/驱动/网络 profile 路径未做真机取证。
- **HEIC 无损 Level**：x265 无损=CQP，`enforceLevel` 对显式 decoder level 硬失败（`level.cpp:443-445`）
  ⇒ 实现为「可配但无损下不下发 + 如实告警」，产物 PTL Level 固定 8.5。登记为唯一需求偏差（A4/U3）。
- **AVIF**：本轮只按用户需求改 HEIF；AVIF/libaom 的 G3 chroma Lock=444 维持 0.4.2 语义，AVIF 矩阵未复跑。
- **JXL A/B wall**：修复期 4 agent 并发构建，wall 噪声大；JXL 单轮 encode_ms 交错中位 2.4x 为可引用
  读数，静机 wall 复测建议用 `build/jxl-perf/ab_run.ps1 -Scenario n2/n3 -Legacy 0|1`。
- **M14 既有未决项不变**（nightly asan/tsan/Linux regression 待事后背书、HEIF/AVIF 矩阵、iPhone 真机等）。
- **一次性 `theme-equiv` offscreen 卡宽（651 vs 661）**：meta-ui 轮次出现一次、真实平台不复现；集成树
  全量 ctest/ui-smoke 未复现；按既有 A30 去抖口径登记为环境时序观察项，未改判据。

## 5. 复现命令与证据路径

```bash
# 集成门禁（本机 Windows）
python tools/env.py run -- cmake --preset release-dev -B build/m15-integration -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m15-integration -j 10
python tools/env.py run -- ctest --test-dir build/m15-integration --output-on-failure      # 52/52
python tools/env.py run -- python tests/golden/smoke.py build/m15-integration              # 20/20
python tools/env.py run -- python tools/regression.py build/m15-integration               # zero diff
python tools/env.py run -- python tools/ci-tidy.py build/m15-integration                  # ratchet
& "C:\Program Files\Git\bin\bash.exe" tools/ci-format.sh                                  # 0 diff
# 发行构建 + 打包（Windows）
python tools/env.py run -- cmake --build build/release -j 6
python tools/env.py run -- python tools/make_winzip.py dist --smoke-exe build/m15-integration/photopipeline.exe
```

证据路径（gitignored）：`build/m15-{ui-smoke,ctest,golden,regression,tidy,release-ctest,winzip}.log`、
`build/heic-lossless/evidence/**`、`build/meta-ui/**`、`build/jxl-perf/**`、`build/win-startup/**`。
