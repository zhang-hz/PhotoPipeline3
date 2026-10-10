# PhotoPipeline M5 收口报告（GUI 刷新 v6 · 折入 0.3.0）

- **里程碑**：M5 = **GUI 刷新 v6**（任务书 `docs/m5-tasks.md`；设计单源 `docs/qt-gui-design-spec.md`（v6）
  + 视觉终稿 `docs/mockups-v2/*.png`（4 页 × 亮暗 8 张）+ 问题台账 `docs/gui-review-0.3.0.md`）。
- **版本口径**（用户已确认）：**GUI 刷新折入 0.3.0 树，版本号不变** —— 单源仍为 `CMakeLists.txt:3`
  `project(PhotoPipeline VERSION 0.3.0 LANGUAGES C CXX)`（`vcpkg.json:3` 同步）；本机实测
  `PhotoPipeline 0.3.0`（打包产物内复核与 `--version`，§1.4）。
- **基线**：M4 收口（`docs/m4-report.md`，0.3.0 树：多格式工作台九需求 + CI 工业化 + AVX2 基线）。
- **提交锚点**：W-G1–W-R 的落地提交见 §2；**本报告不声明最终 HEAD**，当前状态以 `git log` 为准
  （本报告落盘时 HEAD = `e932abf` + 本任务（M5-T15）的文档/注释提交）。
- **发行纪律（本任务硬边界）**：本轮**不含** `git push` / `git tag` / `gh release`。tag `v0.3.0` →
  GitHub Release 属发行轮动作（§5 未决项 1 记录现状：**该 tag 已存在于 M4 提交 `80888ca`**，
  远端同 hash，且**未挂任何 Release 资产** ⇒ 按"M3 §7.1-11 tag 挂资产前可移动"仍可移动/重建）。
- **口径**：沿用 M3/M4 报告口径 —— **不圆场**、逐字证据、未决项如实。凡本机未实测者一律标
  「未跑」或「以 CI 为准」，不写推测值。**W-G4/CI 各轮的原始回报存于 dynamic workflow 运行记录，
  本会话不可读**（M4 §9.2 同源限制）⇒ 本报告的波次证据以**提交正文（逐字摘要）+ 本机实跑**为证，
  不编造未落盘的引文。

---

## 1. 总出口对照

### 1.1 M4 任务书总出口 8 条（`docs/m4-tasks.md:10-19`）在 M5 时点的状态

| # | 出口项 | M5 时点 | 依据 |
|---|---|---|---|
| 1 | 九项需求逐条验收全绿 | ✅ 维持（含 M4 如实记录的 250ms / flatten ≥2× 两处口径，未改判据） | `docs/m4-report.md` §2/§7；M5 未改引擎语义 |
| 2 | 金样 20/20 + ctest 全绿 + 冻结行逐字 | ✅ **本机实跑复现** | `SMOKE total=20 pass=20 fail=0`；ctest release **28/28**（收口复跑 11.31 s）；`UI-SMOKE OK shots=12 pages=3`（§1.4） |
| 3 | CI 四道全绿且双 ≤20min | ⚠️ **3/4 首跑全绿实测；release 道未触发**（`on: push: tags: ['v*']`，本波次未推 tag） | §1.4（run 号 + job 耗时 + 预算校准） |
| 4 | 依赖升级表 + `pp_linkprobe` | ✅ 维持（M5 零依赖变更） | `vcpkg.json`/`tools/versions.env` 本波次未动；M5 未改 `src/core`/`src/codecs`/`src/decode`（C5） |
| 5 | AVX2 基线 + SIMD 一致性 | ✅ 维持（M5 未触碰 `cmake/avx2.cmake` 与 `src/core/simd/`） | 同上 |
| 6 | 接口再冻结（PP-FROZEN） | ✅ 维持 + **新增三处登记**：`settings.h` 的 `ui_theme_mode`（A28/C6 追加面）、`scheduler.h` 的 `RunSummary::peak_bytes`（A29 窄幅解冻）、`src/mapwidget/mapwidget.cpp` 纯外观（A30） | §4.2（A28/A29/A30） |
| 7 | 双平台产物五门禁 + 烟测 | ✅ **Windows 本机打包轮全过**（§1.4 指纹逐字）；✅ **AppImage 走 CI appimage job 全过**（§1.4） | §1.4 |
| 8 | 文档收口 | ✅ 本文 + `CHANGELOG.md` [0.3.0] GUI v6 段 + `README.md` 增补 | 本报告 + 同批提交 |

### 1.2 v6 §12 十七条逐条（`docs/qt-gui-design-spec.md:242`；探针证据 = 本机实跑 `--ui-smoke` 输出）

> 取证命令：`python tools/env.py run -- python tests/ui_smoke.py build/release-dev`（exit 0，
> 全文 `build/m5t15-logs/ui-smoke-full.log`）。下表引文均为该次运行 stdout 的**逐字**行；
> 凡引 `…` 的行 = 该行原样省略中间段；少数判据名（如「字段 160 × 4 + 单位槽 16」「修 4-08 自动档」）
> 取自探针**断言的判据清单注释**（`tests/ui_smoke.py:212-307` 的前缀块），其真值由同行
> `checked=N bad=0` 汇总 —— 本报告不把注释文字冒充为 stdout 输出。

| # | §12 条目 | 探针/证据（逐字） |
|---|---|---|
| 1 | 行 pitch 全局 28 | `UI-SMOKE meta-geom: checked=17 bad=0` · `output-geom: checked=28 bad=0` · `run-geom: checked=14 bad=0` · `settings-geom: checked=42 bad=0` · `classify-grid: … pitch=28 bad=0` |
| 2 | 字段宽只取 88/160/flex | `output-geom: … spin-short=88 spin-std=160 groups=4`；`settings-geom: … 字段 160 × 4 + 单位槽 16`（行内 checked 计数见上行） |
| 3 | 行内只有一个标签 | 三页 geom 行同一 `checked=N bad=0` 判据内（T7/T8/T10 交付面；本报告未单列子断言） |
| 4 | 选中/悬停零宽度变化 | `filelist-selprobe: … sel=27,26,285x58 sel-after=27,26,285x58 check=0,26,27x58 focus=0 bar2=x1:#4cc2ff focus-frame-suppress[…]`；`filelist-selgrid: y28[#4cc2ff,…] y36[…]=y28`（条带**双点**取证）；输出页磁贴零形变在 `output-geom`（`tiles=8`） |
| 5 | caption 46×32 / 关闭悬停 #c42b1c / 图标全 SVG | `caption-geometry: w=46 h=32`；`caption-band: widget=46x32 hit=46x32 band-y=0..31 out-of-band-zone=Caption`；`icons: count=37 required=34 stroke=1.50 bad-path=0 bad-bounds=0 bad-svg=0 bad-svg-attr=0`；`icon-buttons: buttons=8 bad=0`；`icons-engine: bad=0`（T14 同坐标系修正后） |
| 6 | 步骤导航无 ✓；设置页签与主步骤同序 | `nav-text: segments=3 bad=0`；`settings-geom: checked=42 bad=0`（A21 序 = 通用·输出·运行·地图·关于） |
| 7 | 互斥=分段；开关/复选按 6.1 表；数值=spinbox（"自动"） | `output-geom: … tiles=8 rows=15 six-rows=6 spin-short=88 spin-std=160 groups=4 pills=4`；`settings-geom: … 修 4-08 自动档 = 下拉项` |
| 8 | 数值右对齐 tabular；范围/单位不占标签位 | `theme-equiv+nowrap: checked=3 bad=0 … nums=143`（§12-8 spin/数值列右对齐 + kv tnum，T13 补探针） |
| 9 | 状态色四档语义；空态不用红；0 字节=琥珀 | `status-tones: rules-bad=0 live=warn word=warn`；`status-selection: … ready="已选 16 / 16 · 就绪"(ok)`；`run-probe-rows: … f1=[失败\|0 字节\|└ 原因：16→8 位降档被禁止]`；`offline-banner-px: amber-dist=72.4 … ring-dist=72.4 … near=0/1980(0.00%)`（T14 换锚后判据） |
| 10 | 运行指标单源；日志尾=真实尾部（等宽）；失败行含原因 | `run-geom: checked=14 bad=0 pane=587x900 rows=10`；`run-log-tail: …`；`run-memory: 128 KB`（A29 真实峰值读数，非「—」） |
| 11 | 空态：画布引导+投放区+示例占位；重复文案收敛 | `preview-geom: … empty(icon=1 text="将照片拖到此处，或点击 ＋ 添加")`（§8 逐字，A16）；`filelist-geom: …`（空态投放区）；`run-idle: pill="就绪" summary="点击底栏「开始运行」开始批处理"` |
| 12 | 文件行=勾选+46px 缩略图+2–3 行不截断+右侧状态 | `filelist-geom: checked=17 bad=0 row=285x58 thumb=46 check=13 check_col=27 pill_h=16 btn=28x26 info_px=10 … join="4032×3024 · 3.21 MB · JPEG"` |
| 13 | 亮暗两主题布局逐像素等价 | `theme-equiv+nowrap: checked=3 bad=0 equiv-pp-widgets=988 hints=4 nums=143`（Dark↔Light 的 pp-* 控件 (objectName, 窗口几何, 可见性) 逐项一致）；`theme-tokens-v6: checked=52 bad=0` |
| 14 | 卡头 hint nowrap+省略号；无意外换行 | 同一行的 `hints=4`（T13 新增判据） |
| 15 | 图标钮 28×28 且有 tooltip；主按钮实心、主操作居右 | `icon-buttons: buttons=8 bad=0`；`preview-geom: … zoom-btn=28x28 n=4 nav=28x28`；`settings-geom: … 确定实心（QSS 规则 + accent 色）且居右 + 恢复默认居左` |
| 16 | 栏宽固定轴（左 330/右 587），切页列不错位 | `columns-1440: 330/487/587 (expect 330/487/587 ±4)` · `columns-grown: 330/747/587` · `columns-min: 330/227/587` · `columns-recalibrated: 330/487/587` |
| 17 | 文案：自然语、无公式、无括号堆砌、同一设置全界面同名 | `status-disk: text="剩余磁盘 54.8 GB" reading=1`；`run-overview: … budget="自动（16 核）"`（A39①措辞统一后）；`meta-follow-fallback: hint=exif_full.jpg (1/3)` |

### 1.3 M5 任务书 §5 收口清单（`docs/m5-tasks.md:93-99`）

| 清单项 | 状态 | 证据 |
|---|---|---|
| v6 §12 十七条逐条 PASS | ✅ | §1.2（探针逐字）；视觉面见 §3 |
| m4-tasks §6 七项走查自动化部分全绿 + 证据包留 `.cache/m5-review/` | ✅ 证据包在库 | `.cache/m5-review/wg4-t13-{dark,light}-*/` 各 **15 张**真实平台截图（§6 证据路径） |
| ctest 27/27 + release-dev 28/28 + 金样 20/20 + 冻结行 + regression zero diff | ✅ **数值随 M5 增测上移**：ctest release **28/28**、release-dev **29/29**（新增 `test_formkit` 段几何断言）；其余逐项见 §1.4 | §1.4 |
| CI 四道全绿 + 双 ≤20min 实测 | ✅ **4/4**（后补：release 道 run 38024895135 全绿，整轮 wall **17.7 min ≤ 20**（§1.5 e）；单 job 实测上限 16.7 min，各自 `timeout-minutes: 20` 未破）。**遗留**：`winzip` 16.7 min 超 §12.3 单 job 预算 15 min（承 §4.1 第 1 条——**超预算是报告事实、非门禁红**） | §1.4 / §1.5 |
| win64.zip 五门禁 + 许可审计 + AppImage 烟测 | ✅ Windows 本机全过；✅ AppImage 经 CI 实测（本机不可复跑，明示） | §1.4 |
| `docs/m5-report.md` + CHANGELOG 0.3.0 增补 + README 增补 | ✅ 本批落地 | 本文 + `CHANGELOG.md` + `README.md` |
| `tag v0.3.0` → GitHub Release（含前置探测） | ✅ **已发行**（M5-T16 发行轮）：tag 推进至 `50317d0`，release 道 run 38024895135 五 job 全绿，Release v0.3.0 四附件（含双探测绿） | §1.5（a–g） |

### 1.4 本波次 CI 与打包实测（全部本机执行，逐字）

**（a）本机测试闸门**（在最终树：HEAD `e932abf` + 本任务的 `src/main.cpp` 注释修文 + 文档）

| 命令（均经 `python tools/env.py run --`） | 结果（逐字） | 日志 |
|---|---|---|
| `cmake --build --preset release` | exit 0（增量 `[5/6] Linking CXX executable photopipeline.exe`） | `build/m5t15-logs/build-release.log` |
| `cmake --build --preset release-dev` | exit 0 | `build/m5t15-logs/build-release-dev.log` |
| `ctest --preset release --output-on-failure` | `100% tests passed out of 28`（11.31 s；更早一轮同树复跑 11.20 s） | `build/m5t15-logs/gate-ctest.log` |
| `ctest --preset release-dev --output-on-failure` | `100% tests passed out of 29`（35.55 s） | `build/m5t15-logs/ctest-release-dev.log` |
| `python tests/ui_smoke.py build/release-dev` | exit 0；末两行 `UI-SMOKE OK shots=12 pages=3` / `UI-SMOKE pass` | `build/m5t15-logs/ui-smoke-full.log` |
| `python tests/golden/smoke.py build/release-dev` | `SMOKE total=20 pass=20 fail=0` | `build/m5t15-logs/golden.log` |
| `python tools/regression.py build/release-dev` | `regression: zero diff (baseline reproduced)`（归一化 2406 行 vs 基线 2406 行） | `build/m5t15-logs/regression.log` |
| `bash tools/ci-format.sh` | `violations=0` / `全源 0 diff（clang-format version 23.1.1，118 files）` | `build/m5t15-logs/format.log`（stdout） |
| `python tools/collect_licenses.py --check` | `LICENSE-CHECK total=36 min=30 spdx=138/138 ok=1` | `build/m5t15-logs/license-check-final.log` |

**（b）Windows 打包轮（本机）** —— 命令与结果逐字：

```
$ python tools/env.py run -- python tools/make_winzip.py
make_winzip: staging: photopipeline.exe + 40 个 DLL（applocal 部署产物）
make_winzip: CRT app-local 部署: 10 个 ← …\VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT
make_winzip: 剔除冗余产物: vc_redist.x64.exe（18779992 bytes） / dxcompiler.dll（14317000 bytes） / dxil.dll（1509800 bytes）
make_winzip: 非传递闭包点: 1 个已随包（0 个为本次显式补拷）
make_winzip: 门禁 A（闭包）: 68 个 PE / 105 个导入名 → staging 51 · apiset 17 · system 37
make_winzip: 门禁 B（来源）: 5 个 Qt6*.dll 与 Qt 工具链 sha256 逐一一致（.toolchain/Qt/6.11.2/msvc2022_64/bin）
make_winzip: 断言 C（结构）: photopipeline.exe / platforms/qwindows.dll / platforms/qoffscreen.dll / imageformats/=4 dll / tls/=2 dll 齐备
make_winzip: Qt 6.11 DLL 面: 链接面 Qt6Core/Gui/Widgets/Network 全部随包；staging 内 Qt6*.dll = 5（含 Qt6Svg，插件依赖由门禁 A 覆盖）
make_winzip: 断言 D（许可，写盘前）: licenses/ = 35 个 copyright（≥30 ✓）+ PhotoPipeline/LICENSE ✓
make_winzip: 烟测 E-1: PhotoPipeline 0.3.0 (exit 0)
make_winzip: 产物内复核: 解压即用 --version = PhotoPipeline 0.3.0 (exit 0)；licenses/*/copyright = 35（≥30 ✓）
make_winzip: 产物: dist\PhotoPipeline-0.3.0-win64.zip (34602328 bytes,
             sha256=60c61339be713f854498134646589375740b3d50453d5e6c12036e2786bf34d4, 条目 104)
exit 0
```

烟测 E-2（CI 同形调用，追加 offscreen 走查）：

```
$ python tools/env.py run -- python tools/make_winzip.py dist --smoke-exe build/release-dev/photopipeline.exe
make_winzip: 烟测 E-1: PhotoPipeline 0.3.0 (exit 0)
make_winzip: 烟测 E-2: UI-SMOKE OK shots=12 pages=3 (exit 0, QT_QPA_PLATFORM=offscreen)
make_winzip: 烟测状态隔离: 已从 staging 清除走查运行态 3 项（portable data_dir=exe 目录）: classes.json, logs, presets
make_winzip: 产物: dist\PhotoPipeline-0.3.0-win64.zip (34602328 bytes,
             sha256=6cfc99da90f5a95cee1721460643af8050fccfef33da9bdd32687596aa23198e, 条目 104)
exit 0
```

- **指纹（字节数 / 条目数）**：**34,602,328 B / 104 条目**（两次运行逐字同值）；sha256 两次不同
  （`60c61339…` / `6cfc99da…`）—— 差异经**逐条比对归因**：104 个条目的**路径与 CRC/未压缩大小完全一致**，
  仅 **36 条（`licenses/*`，由 `collect_licenses.py` 现写）的 zip `date_time` 不同** ⇒ 与 M2/M3/M4
  既有结论一致：**zip 不承诺字节可复现，字节数稳定**。
- **与 CI 产物的交叉对照**（下载 CI artifact 后逐条比对）：**条目路径集合逐一相同（104/104）**；
  51 个条目 CRC 不同（本机与 runner 的编译器/构建路径不同，二进制面自然不同）。
- E-2 的 offscreen 走查会向 staging 写入运行态（`classes.json` / `logs` / `presets`），打包器在
  两条烟测前取快照、之后强制还原（否则本机分类归属会随 zip 出货，M4-T18 缺陷）。

**（c）AppImage（走 CI main-full 的 appimage job；本机不可复跑，明示）**

- 运行：`main-full` run **38019708847** @ `e932abf`，`workflow_dispatch`，**success**，
  2026-10-10T03:11:54Z → 03:29:56Z（wall 18m2s）。
- 产物：`PhotoPipeline-0.3.0-x86_64.AppImage`，**51,837,432 B**，sha256
  `c79b6b8aacd700073beadf9d50bca48cdda19e82a2eee0daab498d0244bcfe12`。
  **本机下载后 `sha256sum` 逐字一致**（与 job env 里的 `APPIMAGE_SHA256` 同值）。
- `tools/appimage-check-closure.sh` 结果：以 artifact 内 `PhotoPipeline-0.3.0-deps.txt` 为凭
  （该文件 = `make_appimage.sh:471` 调用同一脚本的输出），逐字：

  ```
  == AppImage 依赖闭包检查: …/dist/PhotoPipeline.AppDir
  == 受检 ELF: 36 个（LD_LIBRARY_PATH=…/usr/lib）
  -- ① [FAIL-A] not found（目标机缺库，致命）: 无
  -- ② [FAIL-B] 「必须来自随包」族（libQt6*/libjxl*）解析到 AppDir 之外（…致命）: 无
  -- ③ [SYS] 目标机系统要求（解析到包外的库，按 soname 聚合）: 45 个
  ```

  CI job 日志同源三行：`make_appimage: 闭包门禁 PASS: 全部 ELF 无 not found…` /
  `烟测 ④: 产物内 usr/share/licenses/ = 35 个 copyright（≥30 ✓）+ PhotoPipeline/LICENSE ✓` /
  产物启动烟测 `== ① --version` → `PhotoPipeline 0.3.0`、`== ② offscreen GUI 启动存活性（6s）`
  → `OK: 启动 6s 后仍存活（随包 Qt 与平台插件闭包可加载）`、`§ 产物启动烟测 PASS`。
- **本机复跑该脚本：未跑**。理由（实测）：脚本依赖 Linux ELF 工具链（`ldd` 对 ELF 的解析、
  `mapfile`、`file`）；本机为 Windows，且 `wsl.exe -l -v` 实测**未安装任何 WSL 发行版**。
  ⇒ AppImage 闭包结论**以 CI 为准**，本机只做了 artifact 的 sha256 一致性与 `deps.txt` 原文核对。

**（d）CI 四道首跑（本机经 `gh` 查证 + `tools/ci-budget.py` 复算）**

| 道 | run | 结论 | head | wall | 备注 |
|---|---|---|---|---|---|
| pr-fast | 38019714454 | success | `e932abf` | 12m2s | fmt 9s / build+unit linux 3m51s / build+unit windows 9m54s / golden-subset 1m48s / gate 3s |
| main-full | 38019708847 | success | `e932abf` | 18m2s | 9 个 job 详见下 |
| nightly | 38019720990 | success | `e932abf` | 4m23s | asan/ubsan/tsan + regression + audit + gate |
| **release（首跑）** | 38023300103 | **failure**（双探测均误判；逐条归因并修复，见 §1.5） | `cef01fe` | 16m11s | appimage ✓3.8 · winzip ✓15.9 · **probe-appimage ✗7s**（下载件无 x 位 rc=126）· **probe-winzip ✗14s**（PS 捕获为空 + `$ver`/`$VER` 覆写）· publish skipped（未产生 Release ⇒ tag 仍可移动） |
| **release（发行）** | 38024895135 | **success** | `50317d0` | **17m47s**（job wall **17.7 min ≤ 20** ✅） | 五 job 全绿（4.5 / 16.7 / 0.5 / 0.2 / 0.7 min）→ **GitHub Release v0.3.0 已发布**（§1.5） |
| （前置）warm-cache | 38010575991 | success | — | 1h14m13s | 唯一豁免 20min 的播种道 |

main-full job 实测（`gh api …/jobs`）与**预算校准**（本机用 `tools/ci-budget.py` 复算，
输入 = 该 run 的 jobs.json，参数与 `.github/workflows/main-full.yml:1261-1262` 逐字相同）：

```
## main-full 预算校准（设计 §12.3）
| job | 预算 (min) | 实测 (min) | 结论 | 状态 |
| lint（clang-format 全源 0 diff） | 1 | 0.1 | ✅ 预算内 | success |
| linux（全 ctest+金样） | 12 | 5.7 | ✅ 预算内 | success |
| winzip | 15 | 17.1 | ⚠ 超预算 +2.1 | success |
| windows（全 ctest+金样） | 15 | 17.0 | ⚠ 超预算 +2.0 | success |
| appimage | 12 | 4.3 | ✅ 预算内 | success |
| audit（许可/SPDX） | 3 | 1.3 | ✅ 预算内 | success |
| tidy（clang-tidy 棘轮） | 12 | 8.8 | ✅ 预算内 | success |
| cache-gc | 1 | 0.1 | ✅ 预算内 | success |
| gate（总闸） | 1 | 0.1 | ✅ 预算内 | success |
**整轮 wall-clock** = 17.6 min（首 job 03:12:19 → 末 job 03:29:55）｜✅ ≤ 20min 硬顶
**超预算 job**（2 个）: `winzip`, `windows（全 ctest+金样）`
```

nightly 预算校准（同法复算）：tsan 4.2 / ubsan 3.5 / audit 1.9 / asan 2.8 / regression 2.8 /
gate 0.1 min，**整轮 4.3 min ≤ 20min**，全部 ✅ 预算内。

**（e）本机打包轮 vs CI 打包轮的指纹并列（不互相替代）**

| 轮次 | 产物 | 字节数 | 条目 | sha256 |
|---|---|---|---|---|
| 本机（M5 打包轮，本节 b） | `PhotoPipeline-0.3.0-win64.zip` | 34,602,328 | 104 | `60c61339…`（E-1 轮）/ `6cfc99da…`（E-1+E-2 轮） |
| CI（main-full 38019708847 的 winzip job） | 同上 | 33,538,488 | 104 | `b555ff7e2ffdd24710ec4f9a1dc79922fc478da6c8bcf0c1acc92040388cd402` |
| （对照）M4-T18 本机 | 同上 | 34,484,306 | 104 | —（`docs/m4-report.md` §8.7） |

CI fingerprint 行逐字：`make_winzip: 产物: …\dist\PhotoPipeline-0.3.0-win64.zip (33538488 bytes,
sha256=b555ff7e…, 条目 104)`；本机下载 CI artifact 后 `sha256sum` 与 `33,538,488 B` **逐字一致**。

### 1.5 发行结果（M5-T16 发行轮；`gh` 取证 + 本机实跑抽查）

**（a）前置核对（发行前，逐项实跑）**

| 项 | 结果 | 证据 |
|---|---|---|
| 工作树 | 干净（仅 M4/M5 既有未跟踪杂物：`nul` / `docs/mockups$n.png` / `docs/mockups-v2$pg-*.png` / `docs/gui-fix-mockups*` / `tests/__pycache__/`，未触碰、未提交） | `git status --porcelain` |
| 版本单源 | **0.3.0**（单源 = `CMakeLists.txt:19` `project(PhotoPipeline VERSION 0.3.0 …)`） | 产物 `--version` = `PhotoPipeline 0.3.0` |
| CI 三道（发行前树 `cef01fe`） | pr-fast **38022179465** success `13m23s` · main-full **38022184222** success `18m18s` · nightly **38022181606** success `3m57s`（三条 headSha 均 = `cef01fed…`） | `gh run view --json headSha` 逐条核对 |
| CI 三道 + release（**发行提交 `50317d0`**，发行后逐一复核） | pr-fast **38026021415** success `10m32s` · main-full **38024893020**（push）success `18m11s` · nightly **38026023524** success `3m36s` · release **38024895135** success | headSha 全部 = `50317d0e…` |
| 本机闸门（最终树） | release/release-dev 构建 exit 0 · `ctest --preset release` **28/28** · 金样 `SMOKE total=20 pass=20 fail=0` · `UI-SMOKE OK shots=12 pages=3` · regression `zero diff (baseline reproduced)` · `ci-format` `violations=0` | 本轮复跑，日志 `build/m5t16-logs/` |
| Release 查重 | `no release for v0.3.0`（发行前显式核对；与 publish ① 的 `查重 OK: tag v0.3.0 尚无 Release` 互证） | `gh release view v0.3.0` → `release not found` |

**（b）tag 推进（挂资产前可移动，M3 §7.1-11；共 4 次 `git push -f origin v0.3.0`）**

| 时点 | tag 对象 | 指向提交 | 说明 |
|---|---|---|---|
| 发行前（M4 遗留） | `97d4753` | `80888ca`（M4-CI-fix2，2026-09-25） | 远端同 hash；**未挂任何 Release 资产** ⇒ 可移动 |
| fix 轮 1 | `aaec299` | `cef01fe` | 发布说明改源 + SHA256SUMS 附件 |
| fix 轮 2 | `7b53717` | `8b84f93` | 下载件 `chmod +x` |
| fix 轮 3 | `97eadec` | `745d07e` | probe-winzip 捕获改 `cmd /c` |
| **发行（最终）** | **`84c9158`**（annotated，message `PhotoPipeline 0.3.0`） | **`50317d0`** | = 本报告记录的修复树；`git rev-parse v0.3.0^{}` = `origin/main` = `50317d0` |

**（c）release 道运行序列（4 跑；红/取消逐条归因，最终跑绿）**

| # | run | 树 | 结论 | 时长 | 归因与处置 |
|---|---|---|---|---|---|
| 1 | 38023300103 | `cef01fe` | **failure** | 16m11s | **双探测误判**：probe-appimage ✗7s（下载件无 x 位 → `env … Permission denied` / rc=126）；probe-winzip ✗14s（GUI 子系统 exe 的 PS `&` 捕获恒空 + `$ver`/`$VER` 大小写覆写 ⇒ 假「不一致」）。产物本体无问题（appimage ✓ / winzip ✓，E-1 与构建目录自测同绿）⇒ tag 仍可移动 |
| 2 | 38023713939 | `8b84f93` | cancelled | 11m55s | 与跑 1 的 probe-winzip 同码（彼时 fix3 未落地）⇒ **主动取消止血**，避免白等 8 min |
| 3 | 38024417697 | `745d07e` | cancelled | 7m25s | probe-appimage **二跳红**：过 chmod 后卡在 AppRun `ldd` 自检——runner 基础镜像缺 `libEGL.so.1` / `libOpenGL.so.0`（rc=3）。同因必红 ⇒ 主动取消 |
| 4 | **38024895135** | `50317d0` | **success** | **17m47s** | 五 job 全绿 → Release v0.3.0 发布（下表） |

**（d）修复提交（每项一提交、均只增；先本机验证再推 tag）**

| 提交 | 内容 | 本机验证（逐字） |
|---|---|---|
| `cef01fe` | 发布说明改源 `CHANGELOG [版本]` 节（止于 `### 发行`）+ 新增 `SHA256SUMS` 附件（原 `--generate-notes` 由本步骤产出） | YAML 解析 OK；④ 脚本以假 `dist-release` 实跑 rc=0（说明 217 行、末行 Full Changelog 链接；清单 3 行 sha256） |
| `8b84f93` | probe-appimage / publish ② 的**下载件补 `chmod +x`**；④ 补 `git fetch --tags` 兜底 | 归因 = 跑 1 日志（`Permission denied`、rc=126）；`ci-format` violations=0 |
| `745d07e` | probe-winzip 捕获改 `cmd /c`（GUI 子系统 exe 不走 PowerShell `&`） | step 原文整段实跑：正例 `下载件 --version -> [PhotoPipeline 0.3.0] (rc=0)`；负例（v9.9.9）报「不一致」并 exit 1 |
| `50317d0` | probe-appimage / publish **补运行期系统库**（`bash tools/ci-install-deps.sh`，与 appimage job 同源清单）；probe-winzip 捕获**落文件 + `Get-Content -TotalCount 1`**（去除对 PS 原生 stdout 捕获与数组语义的依赖） | 双侧实证同 `745d07e`；CI 实测 apt 步：probe-appimage 22s / publish 33s |

**（e）双 ≤20min 核对（run 38024895135；`tools/ci-budget.py` 本机复算，参数与 `release.yml` 逐字相同）**

```
## release 预算校准（设计 §12.3；关键路径 = winzip 15 + probe-winzip 3 + publish 2 = 20min 边界）

| job | 预算 (min) | 实测 (min) | 结论 | 状态 |
| --- | --- | --- | --- | --- |
| appimage | 12 | 4.5 | ✅ 预算内 | success |
| winzip | 15 | 16.7 | ⚠ 超预算 +1.7 | success |
| probe-appimage | 2 | 0.5 | ✅ 预算内 | success |
| probe-winzip | 3 | 0.2 | ✅ 预算内 | success |
| publish | 2 | 0.7 | ✅ 预算内 | success |

**整轮 wall-clock** = 17.7 min（首 job 04:40:40 → 末 job 04:58:24）｜✅ ≤ 20min 硬顶

**关键路径嫌疑** = `winzip`（16.7 min）——若整轮超顶，优先拆它（设计 §12.3）。

**超预算 job**（1 个）: `winzip`
```

**（f）Release 核对（`gh release view v0.3.0` 逐字）**

```
title:	PhotoPipeline 0.3.0
tag:	v0.3.0
draft:	false
prerelease:	false
immutable:	false
author:	github-actions[bot]
created:	2026-10-10T04:40:35Z
published:	2026-10-10T04:58:22Z
url:	https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.3.0
asset:	PhotoPipeline-0.3.0-deps.txt
asset:	PhotoPipeline-0.3.0-SHA256SUMS.txt
asset:	PhotoPipeline-0.3.0-win64.zip
asset:	PhotoPipeline-0.3.0-x86_64.AppImage
```

说明正文 = **CHANGELOG `[0.3.0]` 节**（217 行，止于 `### 发行`）+「完整变更日志 @v0.3.0」+
「**Full Changelog**: …/compare/v0.2.0...v0.3.0」；publish 日志逐字：
`发布说明 217 行（源: CHANGELOG.md [0.3.0] 节，止于 ### 发行；prev=v0.2.0）`。

附件逐字（`--json assets`，size / GitHub digest 与下载后本机 `sha256sum` 三方一致）：

| 资产 | 字节 | sha256 |
|---|---|---|
| `PhotoPipeline-0.3.0-x86_64.AppImage` | 51,837,432 | `3ac8e2bc8d4919da96feee28132d58c8fe192e785cfa9d5967fd7a163591a3f9` |
| `PhotoPipeline-0.3.0-deps.txt` | 2,439 | `e8c80b3d61a4e0d41cde50eb3c04577688d7428cc189129b3331cf221407854d` |
| `PhotoPipeline-0.3.0-win64.zip` | 33,538,488 | `acbd54a3b47fa2082c52ad69105ecef013c297223c80c330b816e35003639d40` |
| `PhotoPipeline-0.3.0-SHA256SUMS.txt` | 293 | `4b1b8551021be9a5fc7546bda1f06b563819665d76163f91603751836eebf4b3` |

**（g）产物抽查（全部下载自 Release 资产，本机实跑）**

- `gh release download v0.3.0` 三件 + 清单：本机 `sha256sum` 与 `SHA256SUMS.txt` **逐字一致**
  （`3ac8e2bc…` / `e8c80b3d…` / `acbd54a3…`）。
- **`photopipeline.exe --version` 抽查 = `PhotoPipeline 0.3.0`（rc=0）**（解包 `PhotoPipeline/photopipeline.exe`，
  zip 条目 **104**）——即任务书「产物 `--version` 抽查」。
- AppImage 侧：CI 双探测 + publish 复核（`probe-appimage OK: PhotoPipeline 0.3.0`；
  publish 内 `PhotoPipeline 0.3.0` + `tag=v0.3.0 版本一致: appimage=0.3.0 winzip=0.3.0`）；
  **本机不可复跑 AppImage**（Windows；无 WSL 发行版）——如实标「未跑」，仅做 sha256 核对。
- CI 打包指纹逐字：`make_winzip: 产物: …\dist\PhotoPipeline-0.3.0-win64.zip (33538488 bytes,
  sha256=acbd54a3b47fa2082c52ad69105ecef013c297223c80c330b816e35003639d40, 条目 104)`；
  `make_appimage: 产物: …/dist/PhotoPipeline-0.3.0-x86_64.AppImage (51837432 bytes)`；
  `烟测 E-1: PhotoPipeline 0.3.0 (exit 0)`；`烟测 E-2: UI-SMOKE OK shots=12 pages=3 (exit 0, QT_QPA_PLATFORM=offscreen)`。
- 发布动作末行逐字：`§ Release v0.3.0 已发布（附件：AppImage + deps.txt + win64.zip + SHA256SUMS.txt）`。

---

## 2. 波次执行记录 W-G1–W-R

> 「出口证据」列 = **当轮提交正文的逐字摘要**（时点读数，不可在后世复跑复现）；
> 本报告 §1.4 的读数才是**最终树**的复跑结果。任务号↔提交标签的错位（T11/T12 与
> W-G4 验收/收尾的交叉）按下表**按实际提交**登记，不强行对齐任务书编号。

| 波次 | 任务（任务书） | 落地提交 | 出口证据（摘要） |
|---|---|---|---|
| 预备 | T0 GUI 刷新 v6 设计说明书 / 视觉终稿 mockups-v2 / 审查台账入库 + M5 任务书 | `357f143` | `docs/qt-gui-design-spec.md`（v6 单源）、`docs/mockups-v2/`（8 张 PNG + HTML）、`docs/gui-review-0.3.0.md`、`docs/m5-tasks.md` |
| **W-G1** 地基 | T1 令牌层 v6（§4 颜色/字阶/间距圆角 + R4 控件度量 + QSS 生成） | `fbaa6da` | 双预设 build 绿 + 冻结行逐字（当轮） |
| | T2 formkit 表单框架（C2 契约：几何单源 + 行构造器 R1–R4 + 控制件语义） | `b5136e8` | `tests/unit/test_formkit.cpp` 新建并绿 |
| | T3 矢量图标集 `icons.{h,cpp}` + `assets/icons` 设计源 + 全 UI 字形清零（C3） | `24b034e` | 全仓 grep 无字形图标残留（当轮）；今日探针 `icons: count=37 required=34 bad-path=0 bad-bounds=0 bad-svg=0` |
| | （开发期裁定 A1–A11） | `5bc90a3` | `docs/m5-adjudications.md` A1–A11 |
| **W-G2** 壳层 | T4 壳层 v6 化（顶栏 44 三段式 / 导航 92×30 / **caption 单轨 46×32** / 状态栏 30 / 栏宽 330·587） | `8ccbb21` | 今日探针 `skeleton: frameless=1 titlebar=44 bottom=30`、`caption-geometry: w=46 h=32`、`columns-*: 330/…/587` |
| | T5 文件列表行 58px + 工具行常驻 + 空态投放区 + 分类面板 chip/中性计数 | `1c90523` | 今日探针 `filelist-geom: checked=17 bad=0 row=285x58 thumb=46 check=13 …` |
| | T6 预览画布 v6 化（`#16181d` 舞台 / 缩放条 / 角标空态隐藏 / 最近邻 / letterbox 衬垫） | `38e65fe` | 今日探针 `preview-geom: checked=12 bad=0 canvas=#16181d(light=#16181d) zoom-btn=28x28 nav=28x28` |
| | （开发期裁定 A12–A21，含**追认 T4 跨波次缺陷** QAction 空图标覆盖 / 筛选开关 A14） | `ed40eb0` | A12–A21 |
| **W-G3** 三页与弹窗 | T7 元数据页 §7.1 四卡组 | `2aaa8a1` | 今日探针 `meta-geom: checked=17 bad=0`、`meta-gps`/`meta-strip` 在场 |
| | T8 输出页 §7.2 四卡组（磁贴 2×4 / 一行一义六行 / 预设与预估） | `eb13c15` | 今日探针 `output-geom: checked=28 bad=0 tiles=8 six-rows=6` |
| | T9 运行页 §7.3 三卡组（总览合成条 / 指标单源 kv 八项 / 日志尾等宽 4 行）+ A9 RunMarkLabel DPI | `951514d` | 今日探针 `run-geom: checked=14 bad=0`、`run-bars`/`run-overview`/`run-log-tail` 在场 |
| | T10 设置弹窗 §7.4 重做 + dialogkit + EXIF/预设同语言 | `69e4538` | 今日探针 `settings-geom: checked=42 bad=0`、`exif-rows: leaves=9 batch=6 glyphs=0` |
| | （开发期裁定 A22–A32，含 **A27 段高 0 根治令** / A29 core 窄幅解冻） | `2a68020` | A22–A32 |
| **W-G4** 验收 | T11 W-G4 首项：缺陷根治与壳层收尾（A27 段高 0 / A28 主题持久化 / A13 分栏键 v6 / A16 空态去重 / A17 chip 单实现 / A19 剩余磁盘 / A10 双源退役） | `0ccf27a` | 提交正文闸门：`ctest release 28/28 + release-dev 29/29 + ui_smoke 冻结行 + regression zero diff + ci-format 0 diff` |
| | T12 W-G4：峰值内存接线（A29）+ 分类面板 2 列网格（A15）+ ShellNav 无障碍（A18）+ 地图 banner（A30） | `6f9fedc` | 今日探针 `classify-grid: … cols=2 gap=12 pitch=28 bad=0`、`run-memory: 128 KB`、`nav-a11y: … ring=1 order=1` |
| | T13 W-G4 验收：v6 §12 探针补齐（theme-equiv / hint 禁折行 / num-align）+ 真实平台失败取证行 | `ae1269f` | 今日探针 `theme-equiv+nowrap: checked=3 bad=0 equiv-pp-widgets=988 nums=143` |
| | T14 W-G4 收尾：T13 FAIL 三处根治（icons-engine 同坐标系 / 文件列表系统焦点框抑制 / offline-banner 判据换锚） | `2cd650b` | 修复后 `icons-engine: bad=0`、`filelist-selprobe: … focus-frame-suppress[…]`、`offline-banner-px: … near=0.00%` |
| | （开发期裁定 A33–A40） | `3d1c122` | A33–A40（含 **A39 W-R 视觉收尾修正清单**） |
| **W-R** 发行收口 | T13 W-R 视觉收尾修正：A39 六项逐条根治 + A40 核对 | `32f8d64` | 提交正文：`ctest release 28/28；金样 20/20；regression zero diff；ui_smoke（offscreen 与真实平台）rc=0；ci-format 0 diff`；今日探针 `run-overview: … budget="自动（16 核）"`、`status-selection`、`meta-geom: … privacy-card-body` |
| | T14 CI 首跑前置：pr-fast 手动补跑通道 + nightly Linux 基线重播种通道 | `d8fcc05` | workflow_dispatch 通道落地 |
| | T14 ci-tidy 棘轮违规时补写候选基线（只增诊断） | `46e6f1f` | — |
| | T14 tidy 棘轮首跑红修复：modmap 根因根治 + 基线重播种（283/62，CI 实测同值） | `014575d` | main-full `tidy` job 8.8 min success |
| | T14 nightly Linux 基线重播种：`golden.log` 更新至 0.3.0 归一化运行日志 | `0951888` | `tools/baseline/golden.log` 现含 12 处 `version=0.3.0`（本机复核）；nightly `regression` job success |
| | T14 ci-budget 字段修正：REST API 现行字段是 `completed_at`（原读 `finished_at`） | `64a664d` | 本文 §1.4(d) 的校准表即由修正后的脚本产出（非「不可得」） |
| | T14 回归基线 N10：svt-av1 宿主 CPU 特性横幅归一化（首跑 flake 根治） | `e932abf` | main-full/nightly 在 `e932abf` 上全绿 |
| | **T15（本任务）**：Windows 打包五门禁 + 文档收口（本文 / CHANGELOG / README / main.cpp 注释修文） | 见提交 | §1.4 |

**执行顺序说明（据 `git log`）**：T11/T12 的落地提交（`0ccf27a`/`6f9fedc`）与 T13/T14 标签的
W-G4 验收/收尾提交（`ae1269f`/`2cd650b`）在时间轴上交错；W-R 的视觉收尾再次使用 `M5-T13` 标签
（`32f8d64`）、CI 首跑修复统一使用 `M5-T14` 标签（6 个提交）。**未 rebase / 未 amend**，逐轮独立提交。

---

## 3. 视觉比对结论

> **本任务未重做目检**（如实声明）：本节结论 = ① W-G4 各轮**已记录**的目检差异（裁定册逐字）
> + ② 本机实跑的**机械化等价探针**。四组（元数据 / 输出 / 运行 / 设置 页 vs `mockups-v2` 同名 PNG）
> 的人工逐像素判读原始记录存于 workflow 运行记录（本会话不可读）⇒ 只登记**已落盘**的部分。

### 3.1 四组目检差异（W-G4 记录 → 处置 → 今日探针状态）

| 组 | 目检差异（裁定册逐字摘要） | 处置 | 现状（本机探针） |
|---|---|---|---|
| 运行页 | A39① 线程预算值「自适应（16 核）」措辞与 review 4-12 全局措辞不一致；③ 排队 pill 文案；④ 缩放条形态 | `32f8d64`：①→「自动（16 核）」（tooltip 同步 + 旧措辞否定断言）；③ pill 收「排队」+ 交错语义进 tooltip；④「适应 / 1:1」恢复文字形态 | `run-overview: … budget="自动（16 核）"`、`run-probe-rows: f2=[排队\|排队] f2tip="等待启动：交错启动，每 150 ms 才启动下一个文件"`、`preview-geom: zoom-fit="适应" zoom-1:1="1:1"` |
| 元数据页 | A40 GPS 卡操作行位置维持现状（修 1-37 优先于 mockup 行序）；A39⑥ 隐私卡标题「圆形伪影」 | `32f8d64`：⑥ **定性为实现缺陷**（卡体未加进卡布局 → 停在默认 100×30、三行压成 6px pitch 盖住卡题），`page_meta.cpp` 根治 + `privacy-card-body` 判据 | `meta-geom: checked=17 bad=0`（含 privacy-card-body）；`meta-geom` 的 `ops-above-map` 探针在场（A40 零改动） |
| 输出页 | A36 分类单元 chip 形态 vs mockup 纯文本（接受现状）；输出页磁贴/六行一义已按 §7.2 落地 | A36 登记为**已知视觉差异**（冻结断言 `visible_chips == classify_rows` 优先） | `output-geom: checked=28 bad=0 tiles=8 six-rows=6`；分类侧 `classify-panel: rows=4 … all_count=16 total=16` |
| 设置弹窗 | A21 页签序按裁定取「通用｜输出｜运行｜地图｜关于」；A39② 取消动作层级 | `69e4538`/`32f8d64`：② 「取消剩余」改红文字链（旧 danger 钮 + ■ 图标一并退役） | `settings-geom: checked=42 bad=0`；`run-cancel: hits=1 pill="已取消" row0="已取消" cancel-visible=0` |

视图级残留（A38 焦点环可见条件 = 窗口 active + 控件持焦）与 A33（chip 键帽形态保留）
同为**已裁定接受**项，不属差异未闭环。

### 3.2 机械化等价证据（本机实跑，逐字）

- **亮暗布局逐像素等价**：`UI-SMOKE theme-equiv+nowrap: checked=3 bad=0 equiv-pp-widgets=988
  hints=4 nums=143` —— 即在 Dark / Light 两次构建后比对到 **988 个 `pp-*` 控件**的
  (objectName, 窗口几何, 可见性) 三要素**逐项一致**，同时给出 4 条 hint 的禁折行判据与
  143 个数值面的右对齐/tnum 判据（三者同一行内以 `checked=3 bad=0` 汇总）。
- **令牌层等价**：`theme-tokens-v6: checked=52 bad=0`；`theme-dark` / `theme-light` 两行打印
  窗口/卡片/文本/强调/go 文本等令牌值（`go-text=#0c1b24` 双主题同值，M4 勘误 j）。
- **真实平台双主题截图**：`.cache/m5-review/wg4-t13-{dark,light}-*/` 各 **15 张**（13 张冻结集 +
  `01b-meta-multi` / `01c-meta-mod` / `05b-exif-editor-effective`）—— 证据包在库，供用户抽样目检
  （`docs/m5-tasks.md:94` 的清单项）。

---

## 4. 偏差与例外

### 4.1 本波次偏差归并（T 波次 devations + 本任务新发现）

| # | 偏差 | 证据 | 类别与处置 |
|---|---|---|---|
| 1 | **首次实测：`winzip`（17.1 min）与 `windows`（17.0 min）两个 job 超 §12.3 单 job 预算（各 15 min）约 2 min**；整轮 wall 17.6 min 仍在 20min 硬顶内，两 job 亦在各自 `timeout-minutes: 20` 内 ⇒ 门禁未红，但**预算表失真** | §1.4(d)（本机用 `tools/ci-budget.py` 对该 run 的 jobs API 输出复算，参数与 workflow 逐字相同） | **【机械性/待裁定】**按 §12.3 末行「超预算处置：单 job 超 20min → 拆 job 或砍步骤（**禁止放宽时限**）」——本波次 20min 未超，故**未改任何时限**；建议发行轮前把 `windows`/`winzip` 的 15min 预算重标为 18min 或按同一处置条款拆分。**本文不擅自改预算表**（属设计文档口径） |
| 2 | **`ctest` 条目数随 M5 上移**：release 27→**28**、release-dev 28→**29**（新增 `test_formkit`） | §1.4(a) 实跑 | **【机械性】**M4 报告与 README 的「27/28」为历史读数；本任务按实测订正 README 两处 |
| 3 | **v6 §12 第 3 条（行内只有一个标签）在本文档中未单列子断言引文** | §1.2 第 3 行 | **【口径】**三页 geom 探针的 `checked` 计数已覆盖该判据，但本报告未逐字引其子断言；不冒充逐条复现 |
| 4 | **AppImage 闭包脚本本机不可复跑** | §1.4(c)（`ldd`/`file` 的 Linux ELF 语义 + `wsl.exe -l -v` 未安装发行版） | **【机械性】**以 CI 的 `deps.txt` 原文 + artifact sha256 一致性为证，明示「未跑」 |
| 5 | **`tag v0.3.0` 已存在且指向 M4 提交 `80888ca`**（远端同 hash），**未挂 Release 资产** | `git log -1 v0.3.0^{}` → `80888ca 2026-09-25`；`gh release list` 无 v0.3.0 | **【机械性】**发行轮需决定：就地移动 tag（未挂资产可行，M3 §7.1-11）或删后重建；**不在本任务边界**（§5 未决项 1） |
| 6 | **zip 内部 mtime 浮动 ⇒ sha256 不可复现**（字节数稳定） | §1.4(b)：104 条目路径 + CRC/size 逐一相同，仅 36 条 `licenses/*` 的 `date_time` 不同 | **【机械性】**承 M2/M3/M4 既有结论（"不承诺字节可复现"）；指纹以**字节数 + 条目数**为稳定口径，sha 逐次登记 |
| 7 | **本机与 CI 的 winzip 指纹不同**（34,602,328 vs 33,538,488 B，条目同为 104；51 个条目 CRC 不同） | §1.4(e) | **【机械性】**本机与 runner 的编译环境不同（二进制面自然不同）；两者**互为独立轮次**，不互相替代，亦不覆盖 M4-T18 的 34,484,306 B 历史读数 |
| 8 | **`src/main.cpp` 主题注释修文（1953 行附近）**：原文只写「PP_UI_THEME 强制档优先，未设置才跟随系统」，缺 A28 持久化档 | 本任务改动（纯注释，零语义） | **【机械性】**注释修文；`bash tools/ci-format.sh` = `violations=0`；构建增量仅 `main.cpp` 重编 + 链接 |
| 9 | **README 陈旧事实订正**（ctest 27→28 / 许可数 39→35 / AppImage 40→35 / `PP_UI_THEME` 两档→三档 / deps.txt 条目） | 本任务改动，逐条以本机或 CI 实测为准（§1.4） | **【机械性】**文档与实测对齐；改动均在 README.md 文件面内 |
| 10 | **W-G4/CI 各轮的 workflow 回报不可读** | M4 §9.2 同源限制（workflow introspection 不可用） | **【机械性】**波次证据以**提交正文 + 本机实跑**为凭，不编造未落盘的引文 |
| 11 | **release 道探测链三处环境假设缺陷（首发即红，全部为"从未实跑过"的路径）**：① 下载件无 unix x 位（`download-artifact` 不还原权限）⇒ rc=126；② GUI 子系统 exe 不走 PowerShell `&` 捕获（且 `$ver`/`$VER` 覆写造成假"不一致"）；③ 探测 job 未装运行期系统库 ⇒ AppRun `ldd` 自检 rc=3 | §1.5(c)（跑 1 / 跑 3 日志逐字） | **【机械性/管道缺陷】**已修：`8b84f93`（chmod）/ `745d07e`（capture）/ `50317d0`（apt 运行期库）。三项均"产物本体无问题、是探测环境假设错"——即 M5-T16 任务书第 5 点的「探测脚本误判」情形；修复**只增**（未动断言、未删探测、未放宽时限） |
| 12 | **发布说明源与附件集变更（M5-T16-fix）**：`--generate-notes` → CHANGELOG `[版本]` 节（止于 `### 发行`）+ links；新增 `SHA256SUMS` 第 4 附件 | `cef01fe`；发行清单要求「说明 = CHANGELOG 0.3.0 节摘要」「资产含 checksums」 | **【口径/只增】**原 §12.7 三件套与五 job 拓扑原样保留；本项属发行物形态，逐字登记 |
| 13 | **`M5-T16-fix2` 提交正文笔误**：首跑 run 写作 `258bfef`（不存在的 hash），实际 = `cef01fe` | `git show 8b84f93` | **【机械性】**历史未改写（未 force-push main）；以本报告为准 |
| 14 | **`CHANGELOG.md` 的 `[0.3.0] ### 发行` 末条在发行后即过时**（原文「本轮仍不含 push / tag / Release…留待发行轮」） | `CHANGELOG.md`（M5-T15 边界陈述） | **【机械性】**`CHANGELOG.md` **不在本任务文件面**，按纪律未改；发布说明已按「止于 `### 发行`」生成以避开该条。建议主对话授权一次文案订正（§6 未决项升报第 3 条） |

### 4.2 开发期裁定引用表（`docs/m5-adjudications.md` A1–A40）

| # | 裁定一句话 | 实施归属 | 状态 |
|---|---|---|---|
| A1 | caption 单轨 46×32（可见面=命中区；探针允许机械换锚） | T4（`8ccbb21`） | 已实施（今日探针 46×32） |
| A2 | 双列行栅格以 §5 R2 为准；tokens.css 的 80/152/80/152 **作废**（PNG 不重渲，差异登记） | T2 / T11 对账 | 已实施 |
| A3 | 文件行注释字阶取 §4.2 辅助 10px（§7.5 的 10.5 作废） | T5 | 已实施（`info_px=10`） |
| A4 | 徽标/导航数字 9.5px/700（tokens.css 的 9px 作废） | T4 | 已实施 |
| A5 | `style_mirrors_system_accent()` 显式引 QtWidgets | T4 顺手 | 已实施 |
| A6 | `kCheckBox=15` 副本废除 → `theme::Metrics::checkbox_px=13`；硬编码语义色逐文件收口 | T5/T7/T8/T10 | 已实施（`check=13`） |
| A7 | 按钮高 26 换锚（CTA 22 除外） | T4/T5/T8/T9/T10 | 已实施（`btn=28x26`） |
| A8 | 改值/状态标记零字形（`.mod` 无 `●`；`〔批〕`→中性 pill；PillBadge = 矢量勾+文字） | T8/T10，T9 复核 | 已实施（`exif-rows: glyphs=0`） |
| A9 | RunMarkLabel 补 DPI 失效缓存；icons 探针维持 `assets/icons/` 硬断言 | T9 | 已实施（A31 措辞修正） |
| A10 | `form_label_w(70)` / `form_row_h(32)` 迁移期双源**退役** | T11（`0ccf27a`） | 已实施，全仓无消费者残留 |
| A11 | §4/§5 与 tokens.css/HTML 量化值冲突一律以 §4/§5 文字为准 | 已实施；T11 台账核对 | 已实施 |
| A12 | 追认 T4 跨波次缺陷：QAction 空图标覆盖工具钮图标 → `lock_for_run` 重放 chrome 图标 + `icons-kept` 探针 | `8ccbb21` | 已实施 |
| A13 | 分栏持久化键 bump 至 `ui/splitter-v6`（旧几何一次性丢弃；基线 330/587） | T11 | 已实施 |
| A14 | 追认 T5 筛选可见性开关（属 D2「过滤列表」，不动勾选/输出/分类数据） | `1c90523` 保留 | 已实施 |
| A15 | 分类面板改 mockups-v2 形态（2 列 kv-grid：色点+名+计数胶囊） | T12（`6f9fedc`） | 已实施（`classify-grid cols=2`） |
| A16 | 空态文案去重（预览保留 §8 逐字；文件列表改「拖放照片到此处」+ 显式钮） | T11 | 已实施 |
| A17 | 预览打标 chip 统一 `formkit::ChipButton`（修 1-20 键帽化简 / 「0 清除」降文字链） | T11 | 已实施 |
| A18 | §11 无障碍补齐（ShellNav 接口语义 + 自绘焦点环 + Tab 序） | T12 | 已实施（`nav-a11y`） |
| A19 | 状态栏补「剩余磁盘」段（数据源 = 输出根目录可用空间） | T11 | 已实施（`status-disk: reading=1`） |
| A20 | **夹具不补 EXIF**（金样零改动；文件行第三行空 = 行为正确） | 不实施 | 维持 |
| A21 | 设置页签序 = 通用｜输出｜运行｜地图｜关于 | T10 | 已实施 |
| A22 | GPS 纬度/经度维持只读生效值框（不换 spin） | T7 已按此；T13 核验 | 已实施 |
| A23 | 追认 T7 信息面整理（GPS 时间戳行进 tooltip；生效时间三行→单行） | `2aaa8a1` | 已实施 |
| A24 | 预估体积/耗时无算法 → 维持「—」+ tooltip；真实估算记 **M6 候选** | 已实施 | 维持（§5） |
| A25 | 「视觉距离」范围取 schema 真值 0.0–25.0（mockup 0.0–10.0 记台账） | 已实施 | 已实施 |
| A26 | C7 探针落点惯例：`mainwindow.cpp` 的 `smoke_probe_*` 段 + `tests/ui_smoke.py` 前缀清单 | 惯例生效 | 生效 |
| A27 | **【高优缺陷根治】** formkit SegmentedControl 段高 0 → sizeHint/fixedHeight + 单测 + 撤销调用点补丁 | T11（`0ccf27a`） | 已实施 |
| A28 | 主题选择持久化：`AppSettings.ui_theme_mode`（system/light/dark，默认 system）+ 启动读取点 + `test_settings` 往返 | T11 | 已实施（`settings-theme: checked=9 bad=0`） |
| A29 | **批准 core 窄幅加性扩展**：`RunSummary` 末尾追加 `peak_bytes`（PP-FROZEN 追加面登记） | T12 | 已实施（`run-memory: 128 KB`） |
| A30 | `mapwidget.cpp` 允许**纯外观改**（离线 banner 中性化）；`providers.*` 零改动 | T12 | 已实施 |
| A31 | A9 措辞修正：Qt 6.11 无 `QEvent::ScreenChange` → `ScreenChangeInternal` + `DevicePixelRatioChange` | `951514d` | 已实施 |
| A32 | 右栏页内 ~60px 纵向滚动判为**可接受**（验收基线 = 单层滚动） | T13 按此验收 | 接受 |
| A33 | chip **键帽形态保留**（spec §6 > review 1-20） | 已实施，登记 | 接受 |
| A34 | 无修饰 ←/→ 归**预览翻图**（导航段用 Ctrl+←/→）；Router 焦点路由化 = **M6 候选** | 保持现状 | 维持（§5） |
| A35 | 峰值内存**分母**（"2.1 / 4 GB"）0.3.0 **不补**（需再解冻或单源下行）= **M6 候选** | M6 | 维持（§5） |
| A36 | 分类单元 chip 形态 vs mockup 纯文本：**接受现状**（冻结断言优先），登记为已知视觉差异 | 登记 | 接受（§3.1） |
| A37 | 追认 T14 `FileViewFocusFrameStyle` 代理样式（QTBUG-4281 路径）；同类焦点框风险面登记为本文未覆盖 | `2cd650b` | 已实施 + 风险登记（§5） |
| A38 | 焦点环可见条件 = 窗口 active + 控件持焦 | 接受 | 接受 |
| A39 | **W-R 视觉收尾修正清单**六项 | `32f8d64` | ①–⑥ 全部落地（§3.1） |
| A40 | GPS 卡操作行**维持现状**（修 1-37 优先于 mockup 行序）；差异登记 | 零改动 | 登记 |

---

## 5. 未决项与 M6 候选

| # | 事项 | 现状 | 处置建议 |
|---|---|---|---|
| 1 | **`tag v0.3.0` → GitHub Release（发行轮）** | ✅ **已闭合（M5-T16）**：tag 推进 4 次至 `50317d0`（tag 对象 `84c9158`），Release v0.3.0 已发布（4 附件、双探测绿、查重前置探测在位），见 §1.5 | — |
| 2 | **§12.3 单 job 预算表校准**（winzip 17.1 / windows 17.0 vs 15 min） | **发行轮实测补全**：release 道 `winzip` = **16.7 min**（首跑 15.9），仍超 15 min 预算 +1.7；**整轮 wall 17.7 min ≤ 20min 硬顶**、各 job `timeout-minutes: 20` 未破（§1.5 e） | 建议主对话按 §12.3 处置：把 `winzip`/`windows` 15min 预算**重标 18min**，或按同一处置条款**拆 job/砍冗余**（「禁止放宽时限」= `timeout-minutes` 不动；预算表属设计文档口径，本报告不擅自改） |
| 3 | **release 道首跑** | ✅ **已闭合**：首跑 38023300103 双探测红 → 三处管道缺陷逐条归因修复（§4.1 第 11 条）→ 发行跑 38024895135 五 job 全绿（含 `gh release view` 查重、`probe-*` 双探测、publish 复核） | — |
| 4 | **A37 未覆盖的同类焦点框风险面** | T14 只对**文件列表视图**吞掉 `PE_FrameFocusRect`；其它视图（树/表）未逐一大排查 | 记 M6 候选：全仓 `QTreeView`/`QTableView` 焦点框面排查 |
| 5 | **40 分钟级 TODO：M6 候选清单**（用户可见面） | ① 预估体积/耗时**真实算法**（A24，现为「—」+ tooltip）；② 峰值内存**分母**（A35，现 "2.1 / 4 GB" 缺 4 GB 来源）；③ **Router 焦点路由化**（A34，现 Ctrl+←/→ 变体）；④ 分类面板**严格 mockup 形态**（A36，chip vs 纯文本）；⑤ **mockup PNG 重渲**（A2 作废的 tokens.css 量化值 + M4 未决 #10） | 逐项须先改规范/原型再开发（§0 权威链） |
| 6 | **Linux 基线重生成** | ✅ **本波次已做**：`tools/baseline/golden.log` 由 `0951888` 重播种（现含 12 处 `version=0.3.0`，2377 行）；nightly `regression` job 在 `e932abf` 上 success。**残留**：`tools/regression.py` 的 `HEADER` 两行仍引用 `tools/regression.sh`（M3 §8-9 口径：改它必须先重生成基线） | 如需文案同步：Linux 侧 `--update` 后再改 HEADER（独立口径动作） |
| 7 | **M4 遗留未决项的现状态** | 250ms 判据（无内嵌档 535.1 ms）· flatten ≥2× 仅 ≤1024² 成立 · 批吞吐与 v0.2 对照未跑 —— **本轮未复核、未改判据**（M5 只做视觉与排版，引擎零改动 C5） | 维持 M4 §10 的登记 |
| 8 | **AppImage 本机不可复跑** | §1.4(c)（无 Linux ELF 工具链 / 无 WSL 发行版） | 以 CI 为准；如需本机复核，装 WSL 发行版后可直接对解包 AppDir 跑 `tools/appimage-check-closure.sh` |
| 9 | **工作树杂物**（`nul`、`docs/mockups$n.png`、`docs/mockups-v2$pg-*.png`、`docs/gui-fix-mockups*`、`tests/__pycache__/`） | 自 M4/M5 会话开始即存在（**非本任务产出**）；按纪律**未触碰、未提交** | 需清理请主对话裁定 |

---

## 6. 复现命令与证据路径

```powershell
# ---- 测试闸门（§1.4a；全部经 python tools/env.py run -- 包装） ----
python tools\env.py run -- cmake --build --preset release
python tools\env.py run -- cmake --build --preset release-dev
python tools\env.py run -- ctest --preset release --output-on-failure              # 28/28
python tools\env.py run -- ctest --preset release-dev --output-on-failure          # 29/29
python tools\env.py run -- python tests\ui_smoke.py build\release-dev              # 冻结行 shots=12 pages=3
python tools\env.py run -- python tests\golden\smoke.py build\release-dev          # 20/20
python tools\env.py run -- python tools\regression.py build\release-dev            # zero diff
bash tools/ci-format.sh                                                            # violations=0

# ---- Windows 打包轮（§1.4b） ----
python tools\env.py run -- python tools\make_winzip.py                            # 五门禁（E-1）
python tools\env.py run -- python tools\make_winzip.py dist --smoke-exe build\release-dev\photopipeline.exe   # + E-2
python tools\env.py run -- python tools\collect_licenses.py --check               # total=36 min=30 spdx=138/138

# ---- CI 取证（§1.4d；gh 需登录） ----
gh run list --limit 20
gh run view 38019708847 --log > .cache\m5t15-ci\main-full-38019708847.log
gh run download 38019708847 -n PhotoPipeline-AppImage -D .cache\m5t15-ci\appimage
gh run download 38019708847 -n PhotoPipeline-win64    -D .cache\m5t15-ci\winzip
gh api "repos/zhang-hz/PhotoPipeline3/actions/runs/38019708847/jobs?per_page=100" > .cache\m5t15-ci\jobs-38019708847.json
python tools\ci-budget.py .cache\m5t15-ci\jobs-38019708847.json `
  --budget "lint=1,tidy=12,linux=12,windows=15,appimage=12,winzip=15,audit=3,cache-gc=1,gate=1" `
  --wall-limit 20 --title "main-full 预算校准（设计 §12.3）"

# ---- 发行轮（§1.5：tag 推进 + release 道取证 + Release 抽查） ----
git tag -f -a v0.3.0 -m "PhotoPipeline 0.3.0" 50317d0 ; git push -f origin v0.3.0   # 仅「未挂资产」期可做（M3 §7.1-11）
gh run list --workflow=release.yml --limit 5
gh api "repos/zhang-hz/PhotoPipeline3/actions/runs/38024895135/jobs?per_page=100" > .cache\m5t16-jobs-38024895135.json
python tools\ci-budget.py .cache\m5t16-jobs-38024895135.json `
  --budget "appimage=12,winzip=15,probe-appimage=2,probe-winzip=3,publish=2" `
  --wall-limit 20 --title "release 预算校准（设计 §12.3）"
gh release view v0.3.0
gh release download v0.3.0 -D .cache\m5t16-release --pattern "PhotoPipeline-0.3.0-*"
sha256sum .cache\m5t16-release\PhotoPipeline-0.3.0-*      # 与 SHA256SUMS.txt 对拍（逐字一致）
.cache\m5t16-release\stage\PhotoPipeline\photopipeline.exe --version   # 抽查 = PhotoPipeline 0.3.0
```

证据留档（本机，未入库）：

- 闸门与打包日志：`build/m5t15-logs/`（`build-release.log` / `build-release-dev.log` /
  `gate-ctest.log` / `ctest-release-dev.log` / `ui-smoke-full.log` / `golden.log` / `regression.log` /
  `winzip-final-e1.log` / `winzip-final-e1e2.log` / `license-check-final.log`）；
- CI 取证：`.cache/m5t15-ci/`（`main-full-38019708847.log` · `jobs-38019708847/38019714454/38019720990.json` ·
  `appimage/`（AppImage 51,837,432 B + deps.txt）· `winzip/`（CI zip 33,538,488 B）·
  `local-e1only.zip` / `local-e1e2.zip`（本机两轮产物副本，CRC 比对用））；
- 视觉证据包：`.cache/m5-review/wg4-t13-{dark,light}-*/`（各 15 张真实平台截图）；
- 蒙版/对比留证（W-G4 期）：`.cache/m5-review/rb/`、`.cache/m5-review/rb2/`；
- **发行轮（§1.5）**：`.cache/m5t16-jobs-38024895135.json`（release 跑 jobs API 原文）·
  `.cache/m5t16-release/`（Release 四附件本机副本 + `stage/` 解包）· 本机最终树闸门日志
  `build/m5t16-logs/`（`build-release.log` / `build-release-dev.log` / `ctest-release.log` /
  `golden.log` / `ui-smoke.log` / `regression.log`）。
