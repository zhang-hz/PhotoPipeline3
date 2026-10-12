# M7 收口报告 —— 工程收口轮（深度评审 331 条 + UI 14 项整改）

日期：2026-10-12（R2.1 增章 2026-10-13）｜ 版本口径：`CMakeLists.txt:19` = **0.3.3**（`572a86e` M7-REL-0.3.3-prep 起）
范围：`docs/deep-review-0.3.2.md`（R-001～R-331）与 `docs/ux-review-14.md`（UX-1～UX-14）的批量整改、
结构重构与文档收口（M7-WP1…WP13）+ **R2 轮尾单与能力补强**（M7-R2A…R2F，方案真源 =
[docs/m7r2-plan.md](m7r2-plan.md)）+ **R2.1 轮收口与结构余量**（M7-R2.1-F1…F4，见 §7）。
**覆盖台账见附录 A/B（331+14 逐条 R2.1 终态，无一遗漏）**；裁定汇总见
[docs/m7-adjudications.md](m7-adjudications.md)；逐条用户可见变更见 [CHANGELOG.md](../CHANGELOG.md) `[0.3.3]`；
R2/R2.1 各包交付与门禁读数见「R2 轮交付」（§6）、「R2.1 轮交付」（§7）与 [docs/m7-evidence/](m7-evidence/)。

工作包与提交索引（提交号以 `git log` 实测为准，2026-10-13 复核：**R1 窗口 19 提交全覆盖**
（WP1–WP13/REL 去重）、**R2 轮 11 提交**（R2A `c540630`/R2B `5d19146`/R2C `210fce5`/R2-gate
`09b5340`/R2D1 `6e001da`/R2D2 `27c9f40`/R2E2 `52b9870`/R2F `8a541de`/R2F2 `2c50bfc`/R2G `6a6e043`/R2REL `01cbd74`）、
**R2.1 轮 33 提交**（F1×4/F2×12/F3×10/F4×7，见表末各行与 §7）。
**包名 ↔ git SCOPE 命名对照**（勿混读）：R2F = `M7-R2-F:`（`8a541de`）、R2F2 = `M7-R2-F2:`
（`2c50bfc`，独立校读第二轮）、R2G = `M7-R2-GATE:`（`6a6e043`）、R2-gate = `M7-R2-gate`
（`09b5340`，重播种包，**与 R2G 是两个包**）、R2REL = `M7-R2REL:`（`01cbd74`）、R2.1 各包 =
`M7-R2.1-F1…F4:`。**条目数不在此表计**——各包条目数随计数口径（唯一主责 / 表内出现次数 /
任务包分派数）而异，逐条归属以附录 A 提交列为准。**行序 = 包编号序，非 git 时间序**：WP3 `18fb355` 实为 M7 首提交
（早于 WP1/WP2），WP9 `594d93f` 早于 WP8 `40f3405` 一分钟，WP6-fix `e8b9bf1` 在 WP7 之后；
git 时间序对照见 R2C 对照表 `build/m7r2-c/handoff.md` 附录）：

| 包 | 提交 | 面（代表条目） |
|---|---|---|
| WP1 数据安全与原子写盘 | `12ce994` | core 引擎数据面（R-001/002 双 P0 等） |
| WP2 codecs 与参数正确性 | `33d09a8` | codecs/params（R-010/011 等 + UX-14 A 级） |
| WP3 工程化/CI/供应链 | `18fb355` | tools/workflows（R-014～019 等） |
| WP4 主题/样式/单源收口 | `7472b97` | theme/formkit/UI 样式（UX-3/6/8/11/12 等） |
| WP5 平台壳层 | `25b60b3` | frameless/mica/secret（UX-7/13、R-094 等） |
| WP6 主流程与交互 | `d99df1b` + `e8b9bf1` | mainwindow/page_run/page_output（R-150～153 必修等） |
| WP7 地图与元数据卡 | `a6e29f6` | mapwidget/page_meta（UX-4/5、R-012/013 等） |
| WP8 引擎结构与契约 | `40f3405` | B.4/B.6/B.7/B.8 蓝图（R-156/324 等） |
| WP9 UI 结构拆分 | `594d93f` | 探针迁出 + mainwindow 五域（R-048/049 等） |
| M7-GATE 门禁与基线 | `d882fe6`、`59de09d`、`751f479` | **三提交**：回归重播种两次（本 scope；全窗口实为 4 次，§3.5）+ 终门禁收口（兼 WP12 交付内容，git SCOPE = `M7-GATE:`） |
| WP10 色彩行为 | **未交付**（→ R2A `c540630` 补交付） | 见 §3.1（ColorManager 消费未入树；R2 轮 C1–C4 已落地） |
| WP11 长尾清扫与文档 | `f3e1903` + `dec7a1d` | docs/**、CHANGELOG、README + P3 小修；`dec7a1d` = 校读修订（覆盖失实补列） |
| WP12 终门禁复核 | `751f479`（同一提交，git SCOPE `M7-GATE:`） | 全量判据取证（§2.3）+ tidy 棘轮收口（M7-ADJ-tidy-reseed，§3.7）+ fallout 修复 38 条 |
| REL-0.3.3 发行准备 | `572a86e` + `97cd9da` + `b1af371` | 版本 0.3.3 + CHANGELOG 发行段；`97cd9da` = 第三次基线重播种（版本行 12/行为 0）；`b1af371` = vcpkg.json 版本同改（R-122） |
| WP13 发行收口 | `a4cf0fc` | release #13 winzip 超时实况 + M7-ADJ-tag-move 登记（报告类，不入 tag） |
| R2A 色彩能力与解码语义 | `c540630` | 14 项技术设计实施（C1–C4/D1–D3/A1/UX-10.2/R-296/302/163/B3/ΔE，plan §2） |
| R2B 核心与参数域尾单 | `5d19146` | R-076/231/206/195/211/007/104 + error_text.h + regression E8 |
| R2C CI 与发布链 | `210fce5` | warm-cache YAML 修复 + 发布链取证 + 证据留存机制（docs/m7-evidence/） |
| R2-gate 回归重播种 | `09b5340` | 132 行 = **66 对**逐行归因（A1 nclx：31 文件 output-done 与 in-place 两类行各 +20B 共 62 对 + 汇总级联 4 对），见 m7-evidence。**勿与 R2G 混**：R2-gate（git 主题 `M7-R2-gate`）= 重播种包 |
| R2D1 主窗/列表/预览/分类/page_meta | `6e001da` | R-009/025/026/143/144/148/149/184/251/276/277/278/280/314 + UX-1①-⑤/UX-2/UX-4 stretch（276/277/278/280 为逐号列举；**R-279 不在列**，勿读作 276–280 区间——R-279 归 R2.1-F4 `93001a9`，见附录 A） |
| R2D2 输出/运行/表单/编辑器/设置 | `27c9f40` | R-066/070/072/077/133/135/136/137/138/226/228/238/249/265/266/273(UI 接线)/318 + UX-1⑥–⑨（UX-1⑩–⑫ 运行页三卡 = WP6 `d99df1b`，全文统一此口径）/8/10.1/11/12/6③ + X5（定义源 = plan 附 A） |
| R2E2 RTL 与结构拆分 | `52b9870` | R-218 RTL 镜像 + 第 3 波常驻探针 5 条 + R-190/191/203/275/281/326 |
| R2F 台账终稿与文档 | `8a541de` | 上轮校读 18 条归并 10 主题闭环 + 覆盖台账 R2 终态 + 证据摘录 + plan/CHANGELOG/README |
| R2F2 独立校读第二轮修订 | `2c50bfc` | 台账三行纠偏（R-184/195/273）+ NOLINT 追认链 + 18→10 归并映射 + 过度归因降准（git SCOPE `M7-R2-F2:`） |
| R2G 终门禁复核 | `6a6e043` | 八项判据全绿 + tidy fallout 23 条修复（修码 12 + 消音 11）+ 回归瞬态锁归因（D-1 登记，R2.1-F1 已根治）；git 主题 `M7-R2-GATE`，与 R2-gate 是两个包 |
| R2REL 发行包 | `01cbd74`（本报告基线提交） | 发布 0.3.3：预检/本机门禁全绿 → push 后 main-full #44 **红（linux ctest 8 条，R2 波次引入）** ⇒ 按任务书分支停止上报，tag 未移动、Release 未建；M7-ADJ-tag-move-2 授权在案未消耗（§3.9） |
| R2.1-F1 Linux 回归定位与修复 | `3339492`/`b673ee4`/`d4099bd`/`0555947` | 失败注解遥测二版（永久保留）+ test_fsops 平台样例 + `commit_temp_file` rename 有界重试（D-1 根治）；main-full #47 linux 绿（§7.1） |
| R2.1-F2 R2E1 重做（page_meta + mapwidget） | 12 提交 `b83f185`…`d6360f6` | B.1 page_meta 拆分十步 + R-218 RTL（本面）+ ProbeContext/词表（R-180/182/258）+ B.5① map_view（§7.2） |
| R2.1-F3 R2E2 结构余量 | 10 提交 `6250b3b`…`0ca8c62` | R-254/256/259/185/187/188/196–201/205/255 + B.3 format_selection_model（R-189/192）+ filelistmodel/settings TU 拆分（§7.3） |
| R2.1-F4 跨面包收口 | 7 提交 `dcedf4c`…`81d0b34` | NOLINT 处置翻案（M7R21F4-0）+ 跨界小项 14 条 + 色彩四选项 GUI 接线 + 第 4 波探针批 + 评审本体入库（§7.4） |

**编号命名空间（多套并存，勿混读）**：① `R-nnn` = deep-review 条目（`docs/deep-review-0.3.2.md`）；
② `UX-n` = ux-review 条目（`docs/ux-review-14.md`），其内部另有**色彩分级子项 A1–D3**（§14，
如 A1=nclx、B1=ΔE 护栏、C1–C4=色域映射等）——本报告引用时一律写「UX-14 A1」式全称；
③ `A1–A31/B1–B61/C1–C10/D1–D12/E1–E9/M7-ADJ-*/WP12-B*/WP13-B*`（R1 轮）、`R2A-A*`/`M7R2B-*`/`R2C-W*`/`R2D1-A*`/`M7-R2-A`/`M7-R2-B`/`M7R2D2-*`/`M7R2E2-*`/`R2G-G*`/`R2F-X*`/`R2F-C*`（R2 轮）、`M7R21F1-*`/`M7R21F2-*`/`M7R21F3-*`/`M7R21F4-*`（R2.1 轮）及 `D-n`（AGENTS §9 实现期发现号，如 D-1） = [docs/m7-adjudications.md](m7-adjudications.md)
裁定表编号；④ `B.1–B.8` = **deep-review 附录 B 的拆解蓝图**（B.1 page_meta / B.2 page_run /
B.3 page_output / B.4 run_one_file / B.5 mapwidget / B.6 main.cpp / B.7 enc_common / B.8 契约收口，
见 `docs/deep-review-0.3.2.md` §附录 B）；⑤ `X1/X3/X5/X6` = R2 会话内任务书实现改案编号
（如 X5 = `theme.h` 公共 warn 钩子 `kFkWarn`，R2D2 `27c9f40`）与 `WPn 跨界需求 k` = 各 WP
handoff ③「跨界需求」节条序——**定义真源 = [docs/m7r2-plan.md](m7r2-plan.md) 附 A** 末两行
（X 系与 WPn 跨界对照表，含实施归属）。`WPn` 本身 = R1 轮工作包编号（WP1…WP13），
`R2An`/`R2.1-Fn` = R2/R2.1 轮任务包编号（本报告索引表）。「任务书 / WP 分派清单」引述出处 =
[docs/m7r2-plan.md](m7r2-plan.md)（R2 轮方案真源；附 A「任务书引述出处对照」留存 M7-WP 系
分派书摘录，换机可独立复核——原始分派书未入 docs/；R2.1 各包任务书 = 会话内分派书，
逐条内容见 §7 与 `build/m7r21-fN/handoff.md`）。

---

## 1. 交付内容

### 1.1 整改面（按评审优先级）

1. **P0 数据破坏双条根治**（R-001/002）：输出路径命中源文件即整文件拒绝；元数据写回原子化
   （tmp→rename）且失败=失败。另接 R-003/004/005/023/028/029/030/031 全部数据面 P1/P2，
   R-007 主体已接（预检+manifest；`\\?\` 归一余量见附录 A / E5）。
2. **R3 必修四条**（R-150 版本徽标 / R-151 翻图打标错位 / R-152 过滤态双击写错文件 /
   R-153 分组重建吞多选）全部修复，同批收 R-006/008/069/076/139/140/141 等运行语义面。
3. **参数假接线**（R-010 PNG 键名、R-011 TIFF predictor【复核推翻评审结论：上游真实消费，
   键名对齐即可生效；float 档显式拒绝】、R-244/245/246 JPEG 三假控件锁定+禁用原因）。
4. **色彩正确性 A/B 级**（UX-14 §14 分级）：nclx 跟随 ICC（UX-14 A1）、灰度/RGB ICC 降级不
   整文件失败（UX-14 A2）、ΔE2000 护栏（UX-14 B1）、ICC 嵌入回读扩面（UX-14 B3）。
5. **地图链**（R-012 联想清算、R-013 key 脱敏、R-077 空 Key 预检、R-158/224 表驱动、
   R-219～223 契约收敛、R-161 等）+ UX-4 地图 App 化、UX-5 启用开关。
6. **UI 14 项**：R1 收口时点口径 = 已修/大部完成 11 项（UX-3/4/5/6 大部/7/8 前半/9/10/11 大部/
   12 主修/13），残余逐条登记（附录 B）——**此「11 项」是 R1 期快照，勿当终态**。**R2 轮追记**：
   R2D1/R2D2/R2E2 补齐后 14/14 全部**已修**（终态见附录 B，余量以格内注记）。**R2.1 追记**：
   格内登记的三处余量收口——UX-11 page_meta 侧锁卡接线（`93001a9`，GPS 卡不锁注记）、
   UX-12 像素档探针（`d737cb4`）、UX-14 C1–C4 界面开关接线（`6ff43f9`）；UX-8 余量
   （组折叠头「点击整行可点」）与 UX-6 classify 间距维持登记。
7. **工程/CI/供应链**：actions 钉 SHA、回归归因护栏、UBSan 假绿封堵、语料 sha256 锁、
   tidy 棘轮收紧、工具链钉版、发行包 NOTICE 等（WP3 全清，逐条见附录 A 提交列 `18fb355`）。
8. **结构重构**（B.4/B.6/B.7/B.8 蓝图 + R-048/049）：run_one_file 十阶段、enc_common 五连
   拷贝收编、main.cpp 拆四 TU、探针迁出发布二进制（PP_BUILD_DEV 门控）、mainwindow 五域拆 TU、
   OutputState/ErrorKind 契约枚举。**成功/默认路径行为零变化**由金样 20/20 背书；错误面与
   校验顺序的**授权行为变化**（错误消双记 / 校验前置 / 文案）逐条登记 adjudications B54–B56，
   其中 B54 即 §3.5 重播种中「真实行为 4 行」的来源——回归 zero diff 是重播种**之后**的读数，
   不构成「行为零变化」的背书。

### 1.2 长尾清扫（WP11，本包）

仅动注释/文案/常量命名级（红线 3：逻辑改动一律登记不改）：

| 项 | 落点 | 改法 |
|---|---|---|
| R-283 余量 | `src/core/fsops.h:75` | 「10 种输入」→「13 种扩展名」（注释失实） |
| R-283 余量 | `src/core/settings.h:83` | `workers` 注释「0=物理核数」→「0=自动（线程预算或逻辑核数）」 |
| R-065/237 文案 | `src/ui/settings_dialog.cpp:1211-1217` | tooltip/注释「自动 = 物理核数」改实（含联动提示；探针无钉该串，grep 取证） |
| R-157 尾巴 | `src/ui/settings_dialog.cpp:159-160` | 注释指向单源 `pp::log_level_from_text`（旧指已删除的 main.cpp 函数） |
| R-295 余量 | `src/ui/paramform.cpp:7` | 头注释 `"__lossless"` 字面量改引 `pp::kLosslessKey` 单源 |
| R-272 余量 | `src/ui/preset_io.cpp:269` | 魔法数 `9.007199254740992e15` 命名 `kMaxExactInt`（2^53） |
| R-242 | `src/ui/presets_dialog.h:21` | `path()` 注释改实（SaveAs=空串，旧注释与实现相反） |
| R-268 | `src/ui/icons.cpp:404` | 「跳过」→「截断返回已解析部分」（注释与实现相反） |
| R-268 | `src/ui/paramform.h:21` | 前置声明理由按 M5-A5 现状改写（theme.h 已自含 QApplication） |
| R-240 | `src/ui/page_run.h:60-61`、`src/ui/page_run.cpp:28` | 三处注释与实现相反：日志尾 5→4 行、列宽尺规 64/116→60/132、set_plan「之后无效」→运行期实时被读 |
| R-115 | `README.md`（4 处） | ctest 计数改实 + 「以 ctest -N 实测为准」非计数口径（实测见 §2.3） |
| R-121 | `README.md` 打包节 | 默认构建目录口径修实（build/m2-release→build/release）+ 设计取舍登记 |
| R-078 | `README.md` + `CHANGELOG.md` | 线程读数估算口径（SynthPump/x265 不入账）入发行说明；README 旧句「总活跃线程 ≤ T」改实 |
| UX-6① 余量 | `docs/qt-gui-design-spec.md` §5 R1 | 列表行 36 / 表头 24 两档例外显式登记（WP4 跨界需求 1） |
| R-292 | 本报告附录 C | 13 组镜像对账：收敛 5 / 大部收敛 1 / 部分收敛 1 / 登记 6（不改逻辑；二分口径收敛 6 / 登记 7，见附录 C 折算说明） |

### 1.3 文档收口（WP11）

- `docs/m7-adjudications.md`：九个 handoff 的「裁定登记」合并（A1–A31 加性 / B1–B61 行为 /
  C1–C10 保留 / D1–D12 本轮 / E1–E9 遗留裁决点；R2 轮续表 = 八节 R2A–R2E2 裁定 + 九节
  误识别关闭与受约束登记）。
- `docs/m7-report.md`（本文件）：体例五章 + 覆盖台账附录。
- `CHANGELOG.md` `[未发布]`：新增/修复/变更/已知四段累计本轮（发行段定稿归发行工作包）。
- `README.md`：M7 段落 + 计数口径 + 打包口径 + 线程估算口径。
- 评审文档本体（`deep-review-0.3.2.md`/`ux-review-14.md`）**只读未改**。

---

## 2. 验收结论（命令 + 结果逐条取证）

> 本节由 WP11 先落结构与本包实跑数据；**终门禁（全量 ctest / 金样 / 回归 / format / tidy）
> 由 WP12 在波次收口统一补数**，已于 2026-10-12 补齐（§2.3）。以下为 WP11 本包实跑。

### 2.1 本包实跑（2026-10-12）

| 命令 | 结果 |
|---|---|
| `python tools/env.py run -- cmake --preset release-dev -B build/m7-wp11 -DVCPKG_MANIFEST_INSTALL=OFF` | 配置成功 |
| `python tools/env.py run -- ctest --test-dir build/m7-wp11 -N` | **Total Tests: 35**（release-dev） |
| `python tools/env.py run -- cmake --preset release -B build/m7-wp11-rel -DVCPKG_MANIFEST_INSTALL=OFF` + `ctest --test-dir build/m7-wp11-rel -N` | **Total Tests: 34**（release）；两清单 diff 仅 `ui_smoke` 一条（dev 门控） |
| `grep "物理核" src/ui/smoke_probes.cpp tests/` | 0 命中（R-065 文案改动不触发任何探针断言） |
| `python tools/env.py run -- cmake --build build/m7-wp11` | exit 0（329/329 目标，含 photopipeline） |
| `python tools/env.py run -- ctest --test-dir build/m7-wp11 --output-on-failure` | **100% tests passed out of 35**（43.8s，含 ui_smoke Passed） |
| `python tools/env.py run -- tools/bin/clang-format-win64.exe --dry-run -Werror src/core/fsops.h src/core/settings.h src/ui/settings_dialog.cpp src/ui/paramform.cpp src/ui/paramform.h src/ui/preset_io.cpp src/ui/presets_dialog.h src/ui/icons.cpp src/ui/page_run.h src/ui/page_run.cpp` | 0 diff（钉版二进制；**覆盖面 = WP11 的 10 个改动文件**，与 §2.3 #6 的全源 152 文件 ci-format.sh 是两次不同覆盖面的检查） |

### 2.2 前十个包的验证证据（引自各 handoff「验证命令与结果」节，均为实跑）

> 各包取证原件在 `build/m7-wpN/handoff.md`（gitignored，按 AGENTS §6.3 留证）；关键读数
> 已摘录入 §2.5 使本报告可独立审计。探针名注：`meta-geom` / `meta-loop` / `meta-map-expand`
> 等为 `--ui-smoke` 走查输出的探针行前缀（`tests/ui_smoke.py` 前缀清单），`checked=N bad=M`
> = 该探针断言 N 条、失败 M 条。

- WP1：8 个单测全绿（= `pp_test_metadata` / `pp_test_pipeline_contract` / `pp_test_fsops` /
  `pp_test_classify` / `pp_test_settings` / `pp_test_effective` / `pp_test_color` /
  `pp_test_pixelbudget`）+ 金样 `SMOKE total=20 pass=20 fail=0`（冻结行逐字不动）。
- WP2：ctest **33/33**（当时口径）+ 金样 20/20 + 新增断言 6 组全绿。
- WP3：`pp_verify --selftest` 39 探针 OK；语料 sha256 锁负对照拒跑；versions-check 10 键 OK。
- WP4：test_thumbs/test_formkit 新断言绿；ui_smoke 冻结行不变。
- WP5：ctest **35/35** + 金样 20/20 + `UI-SMOKE OK shots=12 pages=3` + R-312 命中序探针 17 断言 OK。
- WP6：ctest **35/35**（含 ui_smoke）+ 五处换锚断言实跑通过（`e8b9bf1`）。
- WP7：地图域 5 单测绿；meta-geom checked=22 bad=0 / meta-loop 11/11 / meta-map-expand 14/14
  （探针行含义见本节引注）。
- WP8：ctest **35/35** + 金样 20/20（**成功/默认路径**行为零变化；错误面授权变化见 B54–B56）
  + 契约单测 3 组新增断言绿。
- WP9：ctest **35/35** + ui_smoke 冻结行 + 发布树探针串 0 命中（dev 树阳性对照 HIT）。
- M7-GATE：回归 zero diff（两次重播种均按 R-015 先归因后落盘，闭合证明见提交正文）+
  金样 20/20 + `ctest --preset release` + format violations=0。

### 2.3 终门禁（WP12，2026-10-12，门禁树 build/release-dev，取证日志 build/m7-wp12/*.log）

逐条命令与实测结果（全部实跑；fix 后复验一组，两组结论一致）：

| # | 命令 | 结果 |
|---|---|---|
| 1 | `python tools/env.py run -- cmake --preset release-dev -B build/release-dev -DVCPKG_MANIFEST_INSTALL=OFF && python tools/env.py run -- cmake --build build/release-dev` | exit 0（gate-build.log；fallout 修复后重建 gate-build3.log 同绿） |
| 2 | `python tools/env.py run -- ctest --test-dir build/release-dev --output-on-failure` | **100% tests passed out of 35**（44.2s；fallout 修复后复跑 35/35，`ui_smoke` Passed 29.9s）——「33 条」为评审日旧口径（`docs/ux-review-14.md:5` 基线 33/33），实测 35 详见 §3.2 |
| 3 | `python tools/env.py run -- ctest --test-dir build/release-dev -R ui_smoke -V` | 冻结行逐字 `UI-SMOKE OK shots=12 pages=3`（12 张截图全列、末行 `UI-SMOKE pass`） |
| 4 | `python tools/env.py run -- python tests/golden/smoke.py build/release-dev` | **`SMOKE total=20 pass=20 fail=0`** 逐字（20/20 VERIFY OK；fix 后复跑同值） |
| 5 | `python tools/env.py run -- python tools/regression.py` | **zero diff**（`normalized 2448 lines vs baseline 2448 lines`，baseline reproduced；无需重播种。fix 后复跑同结论） |
| 6 | `bash tools/ci-format.sh`（Git bash） | **violations=0**（钉版 clang-format 23.1.1，152 文件全源 0 diff；fix 后复跑同值） |
| 7 | `python tools/env.py run -- python tools/ci-tidy.py build/release-dev` | **`TIDY total=264 baseline=264 files=71 violations=0 seed=0`**（棘轮通过）。首跑红 24 组/302 告警 → 修复 38 条（302→264）→ 余 10 组 17 条经主对话裁定重播种（M7-ADJ-tidy-reseed，§3.7） |
| 8 | 密钥扫描（`git log --all -p` 全历史 240,346 行） | 值形态模式 **0 命中**：`sk-[A-Za-z0-9]{16,}`=0、`amap_key\s*[=:]\s*<16+字符值>`=0、`\b[0-9a-f]{32}\b`=0；`amap_key` 名字面 214 命中全为键名/字段名/文档（与 m6-report §6 口径一致，样例见 build/m7-wp12/leak_scan.py 输出） |

证据路径：`build/m7-wp12/gate-*.log`（configure/build/ctest/uis-verbose/golden/regression/format/tidy 各轮日志）、
`build/m7-wp12/blame{1,2}.txt`（43 条告警逐行 blame 归因）、`build/m7-wp12/handoff.md`（落点与归因台账）。

### 2.4 真机验收（offscreen 无法替代，须人工签收）

WP5 清单 10 项：拖拽移动 / 双击标题栏最大化 / 八向缩放（含三钮顶部 6px）/ Win11 Snap（含
悬停最大化钮弹 Snap Layouts）/ 三钮悬停点击 / DWM 圆角与最大化切角 / 原生 chrome 消失 /
四弹窗同口径 / 多显示器 DPI / Aero 摸边。**未签收前 UX-7/UX-13 不算终验**（登记为已知缺口）。

### 2.5 关键读数摘录（自包含；原件在 gitignored 路径，摘录后本报告可独立审计）

按 AGENTS §6.3 取证产物留在 `build/`（gitignored）并给路径；为免换机/清 build 后不可复核，
关键输出行摘录于此（逐字引自对应日志/handoff）。**R2 轮起证据摘录改落
[docs/m7-evidence/](m7-evidence/)（入库，三段式：命令行/读数/结论，规矩见其 README）**——
下表 R2 各包行的出处即该目录文件；R1 各 WP 行保留 build/ 原件路径（当时尚无证据目录）：

| 读数 | 逐字摘录 | 出处（原件） |
|---|---|---|
| 金样（WP12 终门禁） | `SMOKE total=20 pass=20 fail=0`（20/20 VERIFY OK） | `build/m7-wp12/gate-golden2.log` |
| UI 冻结行（WP12） | `UI-SMOKE OK shots=12 pages=3` | `build/m7-wp12/gate-ctest2.log` |
| 回归（WP12） | `regression: zero diff (baseline reproduced)`（normalized 2448 vs baseline 2448） | `build/m7-wp12/gate-regression2.log` |
| 全源格式（WP12） | `violations=0`（钉版 clang-format 23.1.1，152 文件） | `build/m7-wp12/gate-format2.log` |
| tidy 棘轮（WP12） | `TIDY total=264 baseline=264 files=71 violations=0 seed=0` | `build/m7-wp12/gate-tidy2.log` |
| 密钥扫描（WP12） | 值形态 `sk-[A-Za-z0-9]{16,}`=0、`amap_key\s*[=:]\s*<16+字符值>`=0、`\b[0-9a-f]{32}\b`=0（`amap_key` 名字面 214 命中全为键名/字段名/文档） | `build/m7-wp12/leak_scan.py` 输出 |
| WP1 八单测 | `test_metadata: all cases passed` / `test_pipeline_contract: all checks passed` / `test_fsops: OK` / `test_classify: OK` / `test_settings: OK` / `test_effective: OK` / `test_color: 546 checks, 0 failed` / `test_pixelbudget: OK` | 各 WP handoff「④ 验证命令与结果」节逐字；WP1 原件 = `build/m7-wp1/handoff.md` ④（八单测行逐条） |
| WP7 meta 探针 | `meta-geom: checked=22 bad=0` / `meta-loop: checked=11 bad=0` / `meta-map-expand: checked=14 bad=0` | `build/m7-wp7/handoff.md` ④ 节 |
| WP11 ctest | `100% tests passed out of 35`（43.8s）+ `Total Tests: 35/34`（两树 -N） | 本包实跑（§2.1） |
| R2A 金样/单测 | `SMOKE total=20 pass=20 fail=0`；`test_color: 1351 checks, 0 failed`；`test_decode: 642 checks, 0 failure(s)`；`bitstream VUI primaries=12` | [docs/m7-evidence/m7r2-a-gates.md](m7-evidence/m7r2-a-gates.md) |
| R2B 单测/自检 | `100% tests passed out of 7`；`regression self-check: cases=19 fail=0` | [docs/m7-evidence/m7r2-b-gates.md](m7-evidence/m7r2-b-gates.md) |
| R2D1 ctest/图标 | `100% tests passed out of 35`；icons `count=38 required=34 bad-svg=0` | [docs/m7-evidence/m7r2-d1-gates.md](m7-evidence/m7r2-d1-gates.md) |
| R2D2 ctest/几何 | `100% tests passed out of 35`；`output-geom: checked=29 bad=0`；`settings-geom: checked=42 bad=0` | [docs/m7-evidence/m7r2-d2-gates.md](m7-evidence/m7r2-d2-gates.md) |
| R2E2 ctest/探针 | `UI-SMOKE OK shots=12 pages=3`；`rtl-mirror: checked=11 bad=0`；`context-menu: cards=12 checked=36 bad=0`；`check-all: checked=11 bad=0 rows=16`；`run-reason: checked=4 bad=0 reasons=32`；`folds: checked=6 bad=0 groups=4`（五探针全摘） | [docs/m7-evidence/m7r2-e2-gates.md](m7-evidence/m7r2-e2-gates.md) |
| R2-gate 重播种归因 | `version-line changes=0 behavior changes=66 changed-lines=132`（A1 nclx：四类 66 对 = ①output-done ×31 对 + ②in-place row ×31 对 + ③run-summary ×2 对 + ④in-place summary ×2 对，即行级 62 对 + 汇总级联 4 对 = 66 对/132 行） | [docs/m7-evidence/m7r2-gate-regression-reseed.md](m7-evidence/m7r2-gate-regression-reseed.md) |
| R2.1-F1 CI 判据 | `linux（全 ctest+金样）: success`（run #47 @ `0555947`，check-run 114873710214）；本机 `100% tests passed out of 35` + `SMOKE total=20 pass=20 fail=0` | [docs/m7-evidence/m7r21-f1-gates.md](m7-evidence/m7r21-f1-gates.md) |
| R2.1-F2 拆分/RTL | `100% tests passed out of 35`；地图域 `out of 5` 全绿；meta 四探针行与锚**逐字节相同**；RTL 临时探针四查 OK | [docs/m7-evidence/m7r21-f2-gates.md](m7-evidence/m7r21-f2-gates.md) |
| R2.1-F3 结构余量 | `100% tests passed out of 35`；`UI-SMOKE OK shots=12 pages=3`；test_smoke 第 7/8/9 节 OK；`SMOKE total=20 pass=20 fail=0` | [docs/m7-evidence/m7r21-f3-gates.md](m7-evidence/m7r21-f3-gates.md) |
| R2.1-F4 收口批 | `UI-SMOKE OK shots=12 pages=3` + 新探针 `intake checked=3` / `classify-filter checked=5` / `edit-target checked=3` / `settings-tabs-light checked=4`（均 bad=0）；金样 20/20；`TIDY total=263 baseline=263 files=75 violations=0 seed=0`；NOLINT 指令 grep = 0 | [docs/m7-evidence/m7r21-f4-gates.md](m7-evidence/m7r21-f4-gates.md) |

---

## 3. 偏差与追认

### 3.1 WP10 未交付（本报告最大偏差，如实）

- **事实**：`build/m7-wp10/` 仅有 configure 产物（2026-10-12 05:47），**无 handoff、无提交**；
  代码面取证 `src/core/colormanager.cpp:630-632` = `(void)opts;`（I3 数据面接线但消费未入树），
  全 `src/ui` grep 无色域映射/抖动/渲染意图/线性光 UI 面；M7-GATE 提交正文亦明载
  「WP10 行为实现未入树」。
- **影响**：WP11 任务书的 README 清单列了「色域映射/抖动/渲染意图/线性光重采样 opt-in 项」，
  实际**未实现**。按「诚实显示」纪律，README/CHANGELOG **不将其列为新功能**，只在「已知」
  披露（裁定 D12）。UX-14 §14 的 C1–C4 四条对应登记为未修（附录 B）。
- **残余**：`RunConfig/ColorOptions` 四字段已接线且默认值=现行为（零影响）；行为实现 + UI 开关
  待后续工作包。
- **R2 轮追记（2026-10-12）**：**C1–C4 行为实现已由 R2A（`c540630`）交付**（14 项技术设计定稿
  = plan §2：色域映射三档 / 8bit TPDF 抖动 / 渲染意图四档 / 线性光重采样）。**等价口径（如实
  限定）**：四选项默认关下与 **R2A 入树时点的既有行为**逐字等价（mode0 / 抖动 false / 意图 0 /
  线性光 false 的等价断言背书）——**不是**与 0.3.2 发行面的字节等价：M7/R2 默认路径本身另有
  两处已归因字节变化（UX-14 A1 nclx +20B/文件、B9 chroma ±1 LSB，见 §6.2-1）；金样 20/20 是
  断言级判据、非字节比对。本节「未交付」结论对 R1 轮成立、已被 R2 取代。**R2.1 追记**：
  **GUI 开关已由 F4 `6ff43f9` 接线**（page_output 全局卡四控件，opt-in 默认关 = 现行为）——
  README/CHANGELOG 口径随之更新（§7.4）。

### 3.2 R-115 计数口径偏差（评审日「33 条」→ 实测 35 条）

- 「33 条」来源可核：`docs/ux-review-14.md:5`（评审日 2026-10-11 基线 `ctest 33/33 全绿`）
  与 WP11 任务书沿用该数（引述出处 `docs/m7r2-plan.md` 附 A）。M7 期间 `test_icons`/`test_secret` 入编后为 **35（release-dev）/
  34（release）**——WP11 2026-10-12 用两个全新配置树 `ctest -N` 实测（§2.1）。README 按实测
  改并加「以 ctest -N 实测为准」的非计数口径（裁定 D8）。

### 3.3 评审结论被证据推翻/修正的条目（先例：复核优于照抄）

- **R-011（推翻）**：评审称 `tiff:predictor` 无人消费；WP2 复核 OIIO 3.1.17 `tiffoutput.cpp:1026-1029`
  证明 `tiff:Predictor`（大小写不敏感）真实消费、且在自动规则之后跑 ⇒ 用户值生效。改法从
  「标注不可控」改为「键名对齐 + float 档显式拒绝」（W2-A12）。
- **R-212（推翻）**：`paint_spin_arrows` 被判死码，实为 Spin/DSpin::paintEvent 消费（W4-T5 判定修正）。
- **R-214 家族（判定修正，非推翻）**：`go_text` 非死值（探针消费），保留。

### 3.4 断言换锚汇总（全部同严格度或收紧，无一放宽）

R-171/R-002（write-warnings、meta-write-fail）、R-097（png_ok 硬断言）、UX-3（test_thumbs
两通道分叉 4 条）、UX-9（tab-label/bitdepth-lock 合取项）、T13（清空该类勾选）、W7-A12（地图
136→260 锚）、W7-A4/A6（geocoder/layers 语义换锚）、B28（settings 密文断言收紧）、B60
（fmt_duration 期望锚定既有行为）。明细见 adjudications 二节。

### 3.5 回归基线重播种（M7-GATE scope 两次，均先归因；全窗口实为 4 次）

- `d882fe6`：46 行 heif 档位 info 行——**日志面 46 / 产物字节 0**（WP2 新增 info 日志；
  提交正文归因标签「version-line 0 / 行为 46」的「行为」指日志面行为，两说法测量面不同，
  本节统一为「日志面 46 / 产物字节 0」，R2C 对照表口径差异项）。
- `59de09d`：184 行 = 机械标签迁移 180（main.cpp→dev_cli.cpp，行为 0）+ 真实行为 4
  （R-162 错误消双记）。闭合证明：transformed(baseline) == current 逐字节相等。
- 全窗口（git log 实测）另有两次：`97cd9da`（0.3.3 版本 bump，版本行 12 / 行为 0）、
  `09b5340`（R2-gate，132 行 = A1 nclx 精确信令）。**归因算术（单一口径，勿拆读）**：
  四类共 **66 对** = ① `output-done bytes=+20` × 31 对（heif 17 + avif 14 文件）+ ② `in-place
  row bytes=+20` × 31 对（同源级联）+ ③ `run-summary` +340/+280 × 2 对 + ④ `in-place summary`
  × 2 对——即**行级 62 对（31 文件 × 两类行）+ 汇总级联 4 对 = 66 对 / 132 行**；「无第五类」
  指无第五个**对类**（逐行归因见 `docs/m7-evidence/m7r2-gate-regression-reseed.md`）。
  **勿与 R2G 瞬态 diff 的「级联 4 类计数」混读**：那是单文件 rename 失败引起的四类行计数
  变化（§6.4 #6），与本处「汇总级联 4 对」不是同一对象。「基线两次重播种」**仅在 M7-GATE
  scope（d882fe6/59de09d）内成立**，与全量 git log 对齐须按四次计。

### 3.6 其它追认

- WP5-A10/A11（CMake 两行登记、pragma lib）：工程不可达的字面偏差，理由在案（adjudications B33/B34）。
- WP6-T15（页签等宽改案）、WP9-W9-8（死码接线）等实现改案已在 adjudications 二节逐条登记。
- WP11 只动注释/文案/常量命名级代码 + 独占文档面；`logic` 一律登记（红线 3）。

### 3.7 tidy 棘轮收口（WP12，M7-ADJ-tidy-reseed，主对话裁决）

- **事实**：M7 首跑 `ci-tidy.py build/release-dev` 红 24 组违规（总 302 > 基线 283）；逐行 blame 归因
  （`build/m7-wp12/blame{1,2}.txt`）后，38 条属**可零行为修复**（显式转换/未用变量/枚举基型
  `std::uint8_t`/循环变量按值/字符串拼接改 `+=`/拖拽排除表改 const&/4 个新单测 main 包 try-catch/
  enc_heif 舍入式拆命名临时量保持算子序/enc_jpegli 有意吞掉显式化），已全修（302→264）。
  唯一行为差异（登记 WP12-B2，见 adjudications 六节）：4 个新单测 main 的**异常逃逸路径**
  由 terminate→`FAILED`+返回 1（仅测试代码、仅崩溃路径；成功与断言路径逐字不动，ctest 判定不变）。
- **残余 17 条（10 文件）不可零行为修复**（逐条归因见 handoff ②-残余表，两套等价分解）：
  按告警类型 = 8× easily-swappable（内部同型参数对，修法=签名重构）+ 2× exception-escape
  （`~ErrSync`/事件回调 λ，修法=异常路径吞掉=行为变化，发行门禁期禁改）+ 5× Win32 int↔ptr
  （`reinterpret_cast<HWND>(winId())`/`NCCALCSIZE_PARAMS*` 平台互操作惯用式，frameless）+
  1× memcmp-float（dev_bench 逐位比对=bit-exact 判据本体，改值比较会漏 ±0/NaN）+ 1× 白盒
  include（test_icons 的 M3 铁律六单实现测试接线，有意为之）；按 blame 归属 = **M7 拆 TU
  机械迁移 6**（dev_bench×2/smoke_probes×2/dev_cli×1/mainwindow_status×1）+ **M6 未入基线
  存量 2**（dms/geocoder）+ **发行期禁改的异常路径 2**（colormanager `~ErrSync`、mainwindow_run
  事件回调 λ）+ **平台惯用式与有意为之 7**（frameless×6、test_icons×1）。机械迁移闭合对账：
  mainwindow.cpp 14→1、main.cpp 8→1，迁出计数与新载体承载同源。
- **裁定 M7-ADJ-tidy-reseed**（主对话授权，条件五条全落实）：`--update` 重播种
  `tools/baseline/clang-tidy.tsv` **283/62 → 264/71**——① 总告警只减不增（283→264 严格收紧）；
  ② **frameless.cpp 5→6 是唯一一处每文件级放宽**（Win32 惯用式 + 发行期不做签名重构，登记于
  adjudications 六节）；③ 逐条归因随提交正文与 handoff 在册——**17 条 = 非 frameless 11 条
  （机械迁移 6 + M6 存量 2 + 异常路径 2 + 有意为之 1）+ frameless 6 条（条件②承载）**，
  早稿「11 条（三类目）」即非 frameless 子集且漏列「发行期禁改的异常路径 2」类目，本节按
  四类明列统一口径；④ 复跑 `TIDY total=264 baseline=264 files=71 violations=0 seed=0`；
  ⑤ CI 判据、.clang-tidy 检查集零改动、零 NOLINT 抑制。（**R2 轮追记**：「零 NOLINT」自 R2G 起
  一度不再成立——R2G 终门禁 tidy fallout 采行级 NOLINT 消音 11 条，见 §6.4 与 adjudications
  八节 R2G-G2；本行为 WP12 期裁定原状。**R2.1 追记（现行口径）**：R2G 的 11 处行级消音已由
  F4 **全部撤销**（M7R21F4-0 翻案：参数组结构化 9 + Win32 显式转换封装 5 + 无零行为修法 7 处
  按本节口径重播种），`grep -rE "NOLINT(BEGIN|END|NEXTLINE)?\(" src/ tools/ tests/` = **0 命中**，
  「仓库零抑制 / 否决 NOLINT 案」重新为现行口径；tidy 基线 264/71 → 264/76 → **263/75**
  （总量只减，终读数 `TIDY total=263 baseline=263 files=75 violations=0 seed=0`，
  见 §7.4 与 adjudications 十一节）。）**NOLINT 案否决**（仓库零先例、抑制
  注释会埋回归盲区）。

### 3.8 发行偏差（WP13，M7-ADJ-tag-move，主对话裁决）

- **链路（如实）**：R-122 断言钉住 vcpkg.json 版本漂移 → 失败 tag 移动一次（97cd9da → b1af371）→ 重发布。逐步证据：
  1. WP13 版本 bump 只改 `CMakeLists.txt:19`，`vcpkg.json "version"` 漏同改（当时 grep 复核面未含 `*.json`）；
     `tools/ci-versions-check.py` 第 3 项（R-122，M7-WP3 引入）在 release.yml `appimage` job 6s 断言红
     （release #12 = run 38197049543 @ 97cd9da；报错逐字 `版本不一致: CMakeLists project VERSION=0.3.3
     vs vcpkg.json version=0.3.2 （R-122）`，本机复跑同文）。
  2. `M7-REL-0.3.3-gate-fix2`（b1af371）按 R-122 契约同改 `vcpkg.json` → 0.3.3；阴阳性对照：
     临时 worktree（97cd9da 树）`ci-versions-check` **FAIL**（同 CI 报错逐字）/ 主树（b1af371）**OK (10 keys, expect=2)**。
  3. 移动前取证（裁决条件 1）：`gh release view` gh CLI 未认证（如实记录，同 d049d22 先例状态）；
     匿名核对 releases 列表**无 v0.3.3**（最新 v0.3.2）、`/releases/tag/v0.3.3` 仅 2 个自动源码包资产
     （真发布 = 6 资产，对照 v0.3.2）、release #12 **publish 未执行** ⇒ 零 Release、零资产。
  4. 主对话裁决（escalate）：批准**一次**移动，条件七条（含「再红于任何新原因即停上报、禁止二次移动」）。
  5. 移动（裁决条件 3）：`git tag -d v0.3.3 && git tag -a v0.3.3 -F … b1af371` +
     `git push --force origin refs/tags/v0.3.3`（单 ref 原子更新）；`git ls-remote --tags origin` 核对
     `refs/tags/v0.3.3` = tag 对象 `cb181ee`，`^{}` = `b1af371`。
  6. 重推触发 release #13（run 38198290373 @ b1af371）→ 发布结果见下文「发布结果」段（本节末）。
- **影响面**：产物二进制零影响（`vcpkg.json` 版本字段不进产物；两棵树 `--version` 同为 `PhotoPipeline 0.3.3`）；
  金样 20/20 + 回归 zero diff + ctest 35/35 双背书不受影响（在 gate-fix 一并实跑）。
- **发布结果**：（2026-10-12 实况）appimage 6m56s 绿（AppImage artifact 49 MB；probe-appimage 32s 绿）；
  winzip job **20m16s 撞 `timeout-minutes: 20` 硬顶被 cancelled** —— 步骤级取证
  （API `actions/jobs/114646130378`）：Qt/vcpkg 缓存 restore 均 success、暖度断言（≥20 archive）过、
  语料断言过；step 12「configure + build release」跑 19m11s（03:06:42 → 03:25:53）未完被取消，
  step 13–16（release-dev 子集 / make_winzip / 指纹 / 上传）全 skipped ⇒ probe-winzip、publish
  **skipped** ⇒ **GitHub Release v0.3.3 未创建**（releases 列表复核仍止于 v0.3.2）。
  按裁决条件 5（再红于新原因即停、禁止二次移动 tag）已停止发布动作，残余上报等裁决；
  可选处置（需主对话授权）：同 tag 重跑 release.yml（不涉 tag 移动；winzip 预算 15 min、
  本次 build 单步 19m+，或需调整预算/缓存后再跑）。

### 3.9 发行终态（R2REL，2026-10-12）：**发行暂停于 main-full 红灯，tag 未移动**

> 任务书 = 「修复后发布 0.3.3」（M7-R2REL 包）；实况证据真源 =
> [docs/m7-evidence/m7r2-release.md](m7-evidence/m7r2-release.md)（预检/本机门禁/main-full 取证/
> 停止决策逐步可复跑）。**GitHub Release URL：不存在**（`/releases/tags/v0.3.3` 预检 404 且本轮未发布）。

- **预检（移动前，全过）**：`/releases/tags/v0.3.3` = **404**（零 Release）；release #13（run
  38198290373 @ b1af371）winzip cancelled（20m16s 撞顶）、probe-winzip/publish **skipped**、
  artifacts 仅 `PhotoPipeline-AppImage`（51,382,515 B，Actions 工件非 Release 资产）⇒ 零 Release 零资产；
  `git ls-remote --tags` 核对 v0.3.3 → `cb181ee`/`^{}`=`b1af371`。
- **本机门禁（全绿）**：`ci-format` **violations=0**（155 文件）· `ci-versions-check.py` **OK (10 keys,
  expect=0)** · `make_winzip.py --smoke-exe` 五门禁 + E-2 **rc=0**（E-2 冻结行 `UI-SMOKE OK shots=12
  pages=3` 逐字；E-2 的 dev exe 出自本包专属树 `build/m7r2-rel`，**未触碰 build/release-dev**）；
  打包面 `build/release` 增量重建 202 步至 HEAD。
- **推送与 main-full**：`git push origin main`（`a4cf0fc..2c50bfc`，HEAD `2c50bfc86fa6b53d0c6629e75aa60d40ab2e6ecc`）
  → main-full **#44（run 38255712342）红**：唯一失败 = **linux（全 ctest+金样）step 13
  `ctest --preset release` 8 条失败**（退出码 8 = 失败测试数；构建成功；其余 job 中
  lint/appimage/tidy/audit/winzip/windows/cache-gc 全绿，gate 随 linux 红）。
  **二分归因**：main-full #41 @ b1af371 同 runner「linux（全 ctest+金样）」**success**
  ⇒ 8 条失败为 **M7-R2 波次引入的 Linux 侧回归**（Windows 同套件全绿；非 R2F-C3 的基线移植债——
  那是 nightly 回归 golden.log 的登记）。逐条测试名需 Linux 宿主/鉴权日志（匿名 API 只到 job/step 级，
  本机无 WSL），未定位到具体断言。
- **缓存播种面（流程 3 目的）已达成**：winzip 19m45s / windows 19m50s / tidy 14m49s 全绿且 save 步完整
  （vcpkgwin-/Qt(msvc2022_64) 播种；Windows 侧无 ccache 为查证事实）——若后续发行，winzip 的 20min
  硬顶有暖度支撑。
- **停止决策（ask 流程 3 明文分支）**：main-full 红 ⇒ 修复推送（允许）或停止上报。修复须改
  `src/**`/`tests/**` = 本包独占文件面外（「不得改 src/ 与 tools/」），⇒ **停止上报**：
  tag `v0.3.3` **未移动**（保持 `b1af371`，零 Release 零资产状态不变）、release.yml 未触发、
  GitHub Release 未创建。授权登记 `M7-ADJ-tag-move-2`（adjudications 十节）：**授权在案未消耗**，
  待 8 条 Linux 失败修复、main-full 绿后由后续发行包按同一条件执行。
- **残余（如实）**：① 8 条 Linux ctest 失败未定位/未修（跨界，需 Linux 宿主或 CI 日志权限）；
  ② 0.3.3 未发行，`### 发行` 小节「已发行 + CI 产物指纹」终态收口留待真发布后补
  （`d049d22` 先例口径不变）；③ warm-cache 通道 #9–#13 全解析期秒败（R2C 修复后**无触发轮**，
  gh CLI 未认证无法 dispatch，runbook §5-1 未复核——本轮以 main-full 播种替代，已在证据 §4.4 注明）。

---

## 4. 未覆盖 / 未决项（如实）

### 4.1 跨界未决（与 adjudications 五节 E1–E9 逐条对应；R2 轮终态已注记）

**（a）遗留裁决点 E1–E9**（表内编号即 adjudications 五节编号；R2 终态与其表内「R2 终态」列一致）：

| E | 事项 | 对应条目 | R2 终态 |
|---|---|---|---|
| E1 | UX-14 A1 的 VUI CICP 精确码：P3 目标下实测 unspecified(2)（期望 12）；非 R-296 | UX-14 A1 | **已修**（R2A `c540630` 显式传 `output_nclx_profile`，位流直读 P3=12） |
| E2 | BMP 不承载 ICC（OIIO writer 事实，测试钉现状） | UX-14 B3 余量 | **受约束口径闭环**（R2A B3 警示口径，adjudications 九节 R2F-C1） |
| E3 | R-163 取消错误归类（编码器 cancel 返回 error，pipeline 归 Failed） | R-163 | **已修**（R2A `c540630` 取消归类单点） |
| E4 | R-211 强类型位深（bps/bpp 拆分） | R-211 | **登记**（R2B 已落单位单源；拆字段待解冻，M7R2B-5） |
| E5 | R-007 长路径 `\\?\` 归一（写路径无归一，259 硬线） | R-007 | **已修**（R2B `5d19146` 归一 + 硬线解除；codecs fopen/exists 余量登记） |
| E6 | WP5 真机验收 10 项（§2.4） | UX-7/UX-13 | **登记**（外部条件未达） |
| E7 | Linux 回归基线重播种 | R-015 余量 | **受约束口径闭环**（宿主约束；R2 132 行同未移植，R2F-C3） |
| E8 | regression.py:335-337 行型表钉 `[main.cpp]`（TU 迁移后误分类） | R-015 余量 | **已修**（R2B `5d19146` 锚点扩新 TU + `--self-check` 19 例） |
| E9 | R-296 CICP 映射表驱动（colormanager 分支驱动收敛） | R-296 | **已修**（R2A `c540630` kCicpTable 单源） |

**（b）其它跨面未决**（不在 E 表：无裁定条目编号，问题在此立档；R2 轮全部闭环）：

1. **R-009 收尾一行**：`theme_replay_panels()`（mainwindow）未重放 `page_run->set_tokens`
   ——构造期已修（WP4），**主题切换时运行页令牌不刷新**仍在。P1 残余，一行接线属逻辑改动，
   红线 3 下本包登记不改（grep 取证：`src/ui/mainwindow.cpp` 的 theme_replay_panels 仅
   preview/classify/page_meta/page_output 四件 set_tokens）。
   → **R2 终态：已修**（R2D1 `6e001da` 补 `page_run->set_tokens` 一行，theme_replay_panels 五件齐）。
2. **R-231**：`validate_preset` 弱门禁（未知键/跨字段规则不查）——presets.cpp 跨面，WP1 转出后
   无包认领。→ **R2 终态：已修**（R2B `5d19146` 补 cross_validate 通道 + 未知键显式拒绝）。
3. **R-070/273 UI 中文映射**：ErrorKind 单源已备（WP8），UI 侧按枚举对表的中文文案未接。
   → **R2 终态：已修**（R2B 新头 `core/error_text.h` + R2D2 UI 接线：模板预检/失败原因行
   「└ 原因：%1」走 `pp::error_text_zh`，原文进 tooltip；`run-reason` 探针钉）。
4. **R-076 引擎侧**：`scheduler.cpp` DoneWithErrors→Done 映射未改（UI 已按逐输出事实补判）。
   → **R2 终态：已修**（R2B `5d19146` `file_state_of(Verdict)` 单源映射，五值一一配对）。
5. **R-226 潜伏越界**：`tile_index()` 对未知 id 返回 `kFormatCount`，`tiles[tile_index(id)]`
   即越界（两表分裂时 UB）——防御属逻辑改动，登记不改（page_output.cpp:1114、:2028）。
   → **R2 终态：已修**（R2D2 `27c9f40` `select_format` 不再按下标取磁贴 + `Q_ASSERT` 锚点）。

### 4.2 登记为已知差异 / 结构性债（附录 A「登记」条目，择要；括注 R2 轮变化）

- 大单元拆分余量：page_meta（B.1）**已由 R2.1-F2 消**（`38af841` 等十步）；page_output（B.3）
  **大部已消**（format_selection_model + output_page_build/output_template_ui，`edfc6e4`）；
  mapwidget（B.5）**步①已落**（`d6360f6`，步②–⑥登记）；settings ctor 563 行 **已拆**
  （`334c8c1`）；filelistmodel 6 类 **已拆 TU**（`355e1f2`）；enc_heif 四职责维持登记。
- UI 建造器余量：page_meta/mainwindow 卡壳-卡头-卡体未换 formkit 单源（R-021/022/053/117/119
  余量）——**R2D2 已收 page_output（make_button 委托）与 page_run（三件套删除、直构
  formkit::make_card）**；exif_editor 行高 28 节奏（UX-6③）**已由 R2D2 完成**。
- 性能面：R-025/026（add_paths 后台化、分组摘要哈希）**已由 R2D1 完成**（`6e001da`；性能收益
  无机器判据面，不作数、不虚构读数）；R-024 部分（合并刷新已做，增量计数已做，合并残留）；
  R-228（磁盘空间查询逐键 stat）**已由 R2D2 完成（refresh_disk + 400ms 去抖）**。
- 交互面：R-068/134/147/317 维持登记；R-137/145/239/319 **已由 R2.1-F4 收口**（`93001a9`）；
  R-133/135/138/143/144/148/149/249/265/266/276/277/278/314/318 **已由 R2D1/R2D2 完成**
  （R-133 孤儿信号已由 R2.1-F4 删除，消费端不存在 = 终态闭环）。
- 契约面：R-177～205 常注释/同构债——R2E2 已消 R-190/191/203、R2D2 已消 R-217，**R2.1 已消
  R-177/180/181/182/185/187/188/196–201/204/205**（F2/F3/F4）；R-202 三套异步脚手架维持登记；
  R-325 强类型路线（C8，受约束登记 R2F-C4）、R-330 两页骨架维持登记。
- 余量登记（原属跨面讨论，**R2 轮全部闭环**）：R-099 exif_editor DMS 第三轨 → R2D2 统一
  `pp::map::deg_to_dms`；R-104 settings 持久层词表回落 → R2B `5d19146`；R-309 同款病灶
  （`default_class_file()` 空 data_dir）→ R2B 走 `platform::path_under` 空串口径。
- R-292 未收敛组（附录 C：登记 6 组 + 部分收敛 1 组机制余量；13 组镜像本体 R2 轮未动，
  另消 R-190/191 的 B.3 副本组，见「R2 轮交付」）；R-118 四处注释「留后续」
  （`src/platform/mica.h:11`、`src/core/settings.h:38`、`src/core/simd/simd.h:17` 三处未标，
  `src/mapwidget/providers.cpp` 已随 WP7 收口）。
- R-317 预估卡两项恒「—」：维持「未发生的量不显示」（M5-A24 口径），非缺陷。

### 4.3 未跑 / 无法跑

- ~~终门禁五项（§2.3）归 WP12~~ → **已由 WP12 于 2026-10-12 全部实跑**（§2.3 八项全绿）。
- ~~R-025/026 性能项~~ → **已由 R2D1 完成**（`6e001da`）：本条旧结论（「无自动化判据」）不再
  代表未做——性能收益本身无机器判据面，该口径已移入 §4.2 性能面行（口径说明，非未做项）。
- TSan 坏捕获正对照（R-027）本机 Windows 无法执行，验证点=首轮 Linux nightly tsan job。
- Linux AppImage 构建、Windows 真机验收（§2.4 十项）：本机不可替代，待人工签收（同 E6）。
- **R2.1 未跑（如实）**：① ~~全语料回归 `tools/regression.py` 在 R2.1 四包窗口未跑~~ →
  **已由 F6 终门禁（2026-10-13）补跑**：`regression: zero diff (baseline reproduced)`
  （首轮，未重播种；§7.5 判据 6）；② F1 的 8 条 Linux ctest 失败未能逐名归因（匿名日志 403
  + 遥测一版截断；可代码级归因的 2 条 = test_fsops 平台样例已修，其余 ≤7 条成因无法从现有
  证据判定——遥测二版 summary 通道下次红灯可直接点名，F1 handoff ⑥-1）。

---

## 5. 复现命令与证据路径

```bash
# 构建（各包各自的 build/m7-wpN；门禁用 build/release-dev）
python tools/env.py run -- cmake --preset release-dev -B build/m7-wp11 -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7-wp11

# 测试与金样（终门禁口径，WP12 补数时逐条贴结果）
python tools/env.py run -- ctest --test-dir build/release-dev --output-on-failure
python tools/env.py run -- python tests/golden/smoke.py build/release-dev
python tools/env.py run -- python tools/regression.py
bash tools/ci-format.sh
python tools/env.py run -- python tools/ci-tidy.py build/release-dev

# UI 冻结行
QT_QPA_PLATFORM=offscreen build/release-dev/photopipeline \
    --ui-smoke --inputs tests/golden/base --shots .cache/ui-review

# 格式（WP11 改动文件面；全源门禁见上文 ci-format.sh）
python tools/env.py run -- tools/bin/clang-format-win64.exe --dry-run -Werror \
    src/core/fsops.h src/core/settings.h src/ui/settings_dialog.cpp src/ui/paramform.cpp \
    src/ui/paramform.h src/ui/preset_io.cpp src/ui/presets_dialog.h src/ui/icons.cpp \
    src/ui/page_run.h src/ui/page_run.cpp

# 计数口径复现（R-115）
python tools/env.py run -- ctest --test-dir build/m7-wp11 -N      # 35
python tools/env.py run -- ctest --test-dir build/m7-wp11-rel -N  # 34（diff 仅 ui_smoke）
```

证据路径（原件在 gitignored 目录，关键读数已摘录 §2.5 供独立审计）：各包 handoff =
`build/m7-wpN/handoff.md`（验证命令与结果逐条在册）；门禁取证 = `build/m7-gate/*.log`（回归
归因/金样/ctest/format）；终门禁取证 = `build/m7-wp12/gate-*.log`（八项判据逐条）+
`build/m7-wp12/blame{1,2}.txt`（tidy 43 条归因）；回归归因 = `.cache/regression/attribution.log`
（R-015 护栏产出）。

---

## 6. R2 轮交付（M7-R2A…R2G，2026-10-12；方案真源 = [docs/m7r2-plan.md](m7r2-plan.md)）

> R2 轮 = 收口后尾单清扫 + 能力补强 + 台账终稿。逐条终态见附录 A/B（本章只给各包交付清单与
> 验证证据）；裁定见 adjudications 八/九节；证据摘录见 [docs/m7-evidence/](m7-evidence/)。

### 6.1 各包交付清单（fixed）

| 包 | 提交 | 交付（条目详见附录 A/B 对应行） | 验证证据 |
|---|---|---|---|
| **R2A** 色彩能力与解码语义 | `c540630` | 14 项技术设计（plan §2）：C1 色域映射三档 / C2 8bit TPDF 抖动（**仅 jpegli/webp/jxl/oiio 的 to_u8 量化段**；heif/avif 走 quantize16 面不接——裁定落点 = adjudications 八节 R2A-A11）/ C3 渲染意图四档 / C4 线性光重采样（默认关 = 与 R2A 入树时点既有行为逐字等价，非 0.3.2 字节等价，见 §6.2-1）；D1 CMYK/YCCK 一致拒收 / D2 EXIF ColorSpace / D3 HDR 诚实口径；UX-14 A1 nclx 精确码（E1 闭环）；UX-10.2 逐插件白名单；R-296 CICP 表驱动（E9）；R-302 构建级 FP 收缩关闭；R-163 取消归类（E3）；B3 BMP ICC 警示（E2 口径）；ΔE 护栏扩强 + 跨库对拍（UX-14 B1/B2） | `docs/m7-evidence/m7r2-a-gates.md` |
| **R2B** 核心与参数域尾单 | `5d19146` | R-076 终态映射单源 `file_state_of`；R-231 validate_preset 强化；R-206 双通道共用规则；R-195 查找 first-wins 单源；R-211 位深单位单源；R-007 `\\?\` 归一 + 259/255 硬线解除（E5）；R-104 持久层词表回落；WP5 跨界 1（default_class_file 空目录）；error_text.h 中文单源（R-070/273 core 面）；regression E8 行型表 + `--self-check`；R-325 13 项契约核对 | `docs/m7-evidence/m7r2-b-gates.md` |
| **R2C** CI 与发布链 | `210fce5` | warm-cache YAML 非法修复（空 `env:` 键）；发布链预算取证 + `release-runbook.md`；make_winzip 五门禁 + E-2 本地全跑；`docs/m7-evidence/` 证据留存机制；M7-GATE 提交索引对照表（本报告索引已按其修正） | `docs/m7-evidence/m7r2-c-*.md`、`release-runbook.md` |
| **R2-gate** | `09b5340` | 回归基线重播种 132 行 = 66 对逐行归因（A1 nclx：31 文件 output-done/in-place 两类行各 +20B 共 62 对 + 汇总级联 4 对，无第五类） | `docs/m7-evidence/m7r2-gate-regression-reseed.md` |
| **R2D1** 主窗/列表/预览/分类/page_meta | `6e001da` | R-009 令牌重放收尾（§4.1-1 闭环）；R-025/026 性能；R-143/144/148/149；R-184/251/276/277/278/280/314；WP5 跨界 2（预设路径守卫）；0 字节补齐语义；UX-1①-⑤ / UX-2 全选入口 / UX-4 stretch | `docs/m7-evidence/m7r2-d1-gates.md` |
| **R2D2** 输出/运行/表单/编辑器/设置 | `27c9f40` | R-066/070/072/077/133/135/136/137/138/226/228/238/249/265/266/273（UI 接线随 R-070 同批）/318；R-217 编辑器工厂；R-270 余量令牌化；R-099 第三轨；R-211 UI 消费；R-195 paramform 转发（本包半：preset_io/dev_cli 余量已由 R2.1-F4 `93001a9` 全收口）；UX-1⑥-⑨（**UX-1⑩–⑫ 运行页三卡 = WP6 `d99df1b`**）/ UX-8 UI 半 / UX-10.1 / UX-11 / UX-12 重放链 / UX-6③；X5 warn 钩子 | `docs/m7-evidence/m7r2-d2-gates.md` |
| **R2E2** RTL 与结构拆分 | `52b9870` | R-218 RTL 镜像（LTR 逐字节不变）；第 3 波常驻探针 5 条（rtl-mirror/check-all/context-menu/run-reason/folds）；R-190/191/203/275/281/326；B.3 纯核 `format_policy.h` | `docs/m7-evidence/m7r2-e2-gates.md` |
| **R2F** 台账终稿与文档 | `8a541de` | 上轮校读 18 条归并 10 主题闭环（§6.3）；覆盖台账 331+14 四档终态；`docs/m7r2-plan.md` 出处收口；证据摘录入 `docs/m7-evidence/`；CHANGELOG [0.3.3] 补记 + README 用户口径；误识别关闭 3 项 + 受约束 4 项入 adjudications 九节（台账主标签分别为 3/1，成员对照见附录 A 表头） | 本章 + `build/m7r2-f/handoff.md` |
| **R2G** 终门禁复核 | `6a6e043` | 八项判据全绿 + tidy 棘轮 fallout 23 条修复（修码 12 + 消音 11；裁定 R2G-G1…G4 汇入 adjudications 八节）+ 回归瞬态锁归因（**D-1** 登记）；git 主题 `M7-R2-GATE`，与 R2-gate `09b5340`（重播种包）勿混 | `docs/m7-evidence/m7r2-gate.md` |

### 6.2 口径变化（用户可见/文档可见）

1. **色彩能力四选项入树**（C1–C4）：引擎行为实现 + 护栏断言，**默认关**——各选项未启用时
   与 **R2A 入树时点的既有行为逐字等价**（mode0 / 抖动 false / 意图 0 / 线性光 false 的等价
   断言背书）。**口径限定（勿过度归因，如实改写）**：此「逐字等价」的比较基线是 **R2A 实施前
   的树**（其上已含 M7 的 B8 nclx 与 B9 quantize16），**不是 0.3.2 发行面**——金样 20/20 是
   断言级判据、**不是与 0.3.2 的字节比对**；默认路径相对 0.3.2 实际有两处已归因字节变化：
   **B9** chroma 量化统一 quantize16 的 ±1 LSB 级差异（adjudications B9，R-089）与 **UX-14 A1**
   nclx 精确信令使无 ICC 的 heif/avif **+20B/文件**（§3.5、R2-gate 归因）——与 §1.1-8
   「zero diff 是重播种之后的读数，不构成零变化背书」口径一致。C2 抖动仅接
   **jpegli/webp/jxl/oiio 的 to_u8 量化段**，heif/avif 的 8bit 量化路径（quantize16 面）
   **不接**（裁定落点 = adjudications 八节 **R2A-A11**：扩面需 `quantize16_dithered`，另裁；
   勿读作全部格式生效）。**GUI 开关 R2 期未接线、R2.1 已接线**（F4 `6ff43f9`：page_output
   全局卡 pp-intent/pp-gamut/pp-dither8/pp-linear 四控件，opt-in 默认关 = 现行为，见 §7.4）。
   原 §3.1「WP10 未交付」结论随之取代。
2. **HDR / CMYK 口径**：PQ/HLG 输入不重解释为 sRGB（miss 文案明示「暂不支持」，不建 HDR 管线）；
   JPEG CMYK/YCCK 一致拒收（错误串逐字沿用）。
3. **RTL 支持**（R-218）：自绘几何/内距/QSS 非对称值随布局方向镜像，LTR 逐字节不变。
4. **右键菜单/全选**（UX-1/UX-2）：12 卡全接线（①–⑤ `6e001da`、⑥–⑨ `27c9f40`、**UX-1⑩–⑫
   运行页三卡 = WP6 `d99df1b`**）+ 工具行 check-all + 快捷键三态。**归属统一口径**：UX-1
   ⑩–⑫ 一律记 WP6 `d99df1b`（git 主题 `M7-WP6:`），不记 R2D2——全文（索引表 / §6.1 /
   附录 B）三处已同此口径。
5. **结构拆分**：`format_policy.h`/`leading_indent.h`/`error_text.h` 三个 header-only 单源；
   回归行型表扩新 TU 锚点。
6. **断言换锚登记**：R2 轮共 **6 条**换锚类登记（R2A-A6 HDR miss 文案换锚 7 改 3 增、
   R2A-A10 跨库对拍测量路径换锚、M7R2B-3 test_fsops 长度硬线断言换锚、M7-R2-A、M7-R2-B、
   M7R2E2-5 settings_geom QSS 换锚）。**「主对话授权」严格限定为其中 2 处**——即期望值被
   重新定值且经主对话批准的两处：M7-R2-A `spin-group` 88→160、M7-R2-B map 高度 260 锚
   `==`→`>=`；其余 4 条为同严/测量路径换锚（严格度不降，不涉授权）。**R2.1 轮**另 1 条
   样例拼写调整（M7R21F1-3：test_fsops 长路径断言样例按平台拼写，两侧断言逐条相同、判据
   不变）——既非换锚亦非放宽，单列。金样/夹具零改动，冻结行逐字不变。

### 6.3 上轮校读 18 条 → 归并 10 主题终态（逐主题闭环；归并映射见表后）

> 上轮校读共 **18 条**细分意见（含「四小项」），按主题归并为下表 **10 行**。**映射口径
> （如实声明，防「18 条 vs 10 行」误读）**：① 原始 18 条为会话内校读文本、**未入 docs/**，
> 逐号对照不可复核（登记为残余）；② 表后「归并映射」列出的是**落点动作清单**（共 30 条
> 动作），**不是 18 条意见的逐号切分**——1 条意见可落多个动作、多条意见可并入同一主题，
> 意见条数（18）≠ 主题行数（10）≠ 动作条数（30），三者无需相等；③ 每条意见的去向以
> 主题行「终态/落点」为准，每条动作均已在表内闭环。机械核对（行数/四档计数/grep）为 R2F
> 会话 2026-10-12 实跑，命令与输出见 `build/m7r2-f/handoff.md` ④；deep-review 汇总行笔误的
> 核对对象 `docs/deep-review-0.3.2.md` 当前 untracked（残余）。

| # | 意见主题 | 终态 | 落点 |
|---|---|---|---|
| 1 | 计数口径 | **闭环**：附录分节 2/33/144/152=331 与 deep-review 逐条标签一致（其汇总行 P1=32/P2=145 系评审本体笔误，已注明）；终态四档计数 345=294+47+3+1 入表头 | 附录 A 表头 |
| 2 | E 映射 | **闭环**：E1–E9 两文互见；§1.3 早稿「E1–E8」改 E1–E9；R2 终态列补入（E1/E3/E5/E8/E9 已修、E2/E7 受约束、E4/E6 登记） | §4.1（a）、adjudications 五节 |
| 3 | 悬空引用 D34/WP12-B2 | **闭环**：`D34` 全文 0 命中（A3 已改指 D12/§3.1）；WP12-B2 已入 adjudications 六节并在 §3.7 引用 | 两文 grep 复核 |
| 4 | 去向标签四档化 | **闭环**：六标签收敛为终态四档（已修/误识别关闭/受约束口径闭环/登记），345 行全部落档，R1 折算口径入表头 | 附录 A/B 表头 |
| 5 | 附录 C 折算口径声明 | **闭环**：逐行 5/1/1/6 与二分摘要 6/7 的折算声明在附录 C 头（大部收敛→收敛、部分收敛→登记） | 附录 C |
| 6 | D11 交叉引用 | **闭环**：D11 ↔ 附录 C 双向互指（D11 明细见附录 C；附录 C 标 D11） | adjudications 四节 / 附录 C |
| 7 | 工作包条数口径 | **闭环**：索引表「条目数不在此表计」+ 三口径说明（唯一主责/表内出现次数/分派数），逐条归属以附录 A 提交列为准 | 文首索引表注 |
| 8 | 路径风格统一 | **闭环**：全文 src/ 前缀（R-118 行 `src/platform/mica.h:11`/`src/core/settings.h:38`/`src/core/simd/simd.h:17` 等已改） | 附录 A R-118 行 |
| 9 | M7-GATE 索引按 R2C 对照表修正 | **闭环**：751f479 归 M7-GATE（三提交）+ WP12 同提交双角色注明；补 dec7a1d/572a86e/97cd9da/b1af371/a4cf0fc 五行；「两次重播种」限定 M7-GATE scope、全窗口 4 次（§3.5）；行序注明非 git 时间序；d882fe6 口径统一「日志面 46 / 产物字节 0」 | 文首索引表、§3.5 |
| 10 | 格式命令占位符补实 | **闭环**：§2.1 与 §5 格式命令均列全 10 文件清单（无占位符；`<本包 10 个改动文件>` 由 dec7a1d 补实后本轮 grep 复核 0 残留）；R2 证据摘录命令同样逐字可复跑 | §2.1/§5、docs/m7-evidence/* |

**归并映射（18 条意见 → 10 主题 → 30 条落点动作；动作非意见的 1:1 切分）**：

| 主题 | 落点动作清单（每条动作均已在上表终态/落点内闭环） |
|---|---|
| #1 计数口径 | ①附录 A 分节计数 2/33/144/152；②「33 条」旧口径出处注记（ux-review-14.md:5）；③§3.7「17 条 / 11 条」两套分解调和为四类明列；④deep-review §1 汇总行 32/145 笔误注记（逐条标签 33/144）；⑤终态四档 345 行计数自检 |
| #2 E 映射 | ①§4.1（a）↔ adjudications 五节逐条对齐；②E1≠R-296 甄别（VUI 精确码 vs 表驱动）；③E2/E8 补入；④R-099/R-104 按附录 A 标签移出跨界清单 |
| #3 悬空引用 | ①A3 的「见 D34」改指 D12/§3.1（全文 0 活引用）；②WP12-B2 落进 adjudications 六节并被 §3.7 引用 |
| #4 去向标签四档化 | ①标签口径补全（对账登记/结案入定义）→ 收敛为终态四档；②R-279/302/309 混用标签改单标签+注记；③331+14=345 行全部落档（本轮含 R-184/195/273 三行纠偏） |
| #5 附录 C 折算口径声明 | ①逐行 5/1/1/6 明示；②二分摘要 6/7 的折算规则（大部收敛→收敛、部分收敛→登记）成文 |
| #6 D11 交叉引用 | ①§1.2/附录 C 头标注 D11；②D11 明细指回附录 C（双向互指） |
| #7 工作包条数口径 | ①索引表去「等 N 条」计数、注明三口径（唯一主责/表内出现次数/分派数）；②WP12 提交号 `751f479` 补列 |
| #8 路径风格统一 | ①附录 D 四处补 `src/` 前缀；②R-118 行路径前缀；③docs 内链 href 改文件相对路径 |
| #9 M7-GATE 索引 | ①`751f479` 归属 M7-GATE（三提交）+ WP12 双角色注明；②补 dec7a1d/572a86e/97cd9da/b1af371/a4cf0fc 五行；③「两次重播种」限定 M7-GATE scope（全窗口 4 次）；④行序注明非 git 时间序；⑤`d882fe6` 口径统一「日志面 46 / 产物字节 0」 |
| #10 格式命令占位符 | ①§2.1 格式命令补实 10 文件清单；②§5 复现节同补（并注明与全源 152 文件是两次不同覆盖面检查） |

**R2.1 增补（2026-10-13）**：索引表再补 **R2F2 `2c50bfc`** 与 **R2REL `01cbd74`** 两行及
R2.1 四包行（F1–F4，§7）；包名 ↔ git SCOPE 命名对照（R2F/R2F2/R2G/R2-gate/R2REL ↔
`M7-R2-F:`/`M7-R2-F2:`/`M7-R2-GATE:`/`M7-R2-gate`/`M7-R2REL:`）入索引注；「18 条 vs 10 行」
映射口径按本节表头三条如实声明（意见 18 / 主题 10 / 动作 30 三数不互等、非逐号切分）。

### 6.4 R2 验收结论（终门禁 M7-R2-GATE，2026-10-12，门禁树 build/release-dev）

> 本节 = R2 轮（R2A…R2F + R2-gate）收口后的**终门禁实跑**；逐条命令与输出摘录真源 =
> [docs/m7-evidence/m7r2-gate.md](m7-evidence/m7r2-gate.md)（三段式，原件在 `build/m7r2-g/*.log`）。
> 本轮口径：**判据零改动、基线零改动、冻结行逐字不动**；唯一代码改动 = tidy 棘轮 fallout 修复
> （23 条新增告警，行为零改动，金样 20/20 + 回归 zero diff 双背书）。

逐条判据与实测结果（全部实跑；修码后全量复验一组，两组结论一致）：

| # | 命令 | 结果 |
|---|---|---|
| 1 | `bash tools/ci-format.sh`（Git bash） | **`violations=0`**（钉版 clang-format 23.1.1，155 文件全源 0 diff；修码后复验同值。与 §2.3 #6 的 152 文件是两轮读数，差额 3 = R2 新增 `src/core/error_text.h`/`src/ui/format_policy.h`/`src/ui/leading_indent.h`。**R2.1 注**：F2/F3 拆 TU 又新增 20+ 个源文件，全源收集数再变——文件数为**各轮实跑读数**、非固定口径，以当轮 `ci-format.sh` 输出为准） |
| 2 | `python tools/env.py run -- cmake --preset release-dev -B build/release-dev -DVCPKG_MANIFEST_INSTALL=OFF && … cmake --build build/release-dev` | exit 0（修码后重建 build2.log 同绿） |
| 3 | `python tools/env.py run -- ctest --test-dir build/release-dev --output-on-failure` | **100% tests passed out of 35**（45.27s / 修码后 45.89s） |
| 3b | `python tools/env.py run -- ctest --test-dir build/release-dev -R ui_smoke -V` | 冻结行逐字 **`UI-SMOKE OK shots=12 pages=3`**（末行 `UI-SMOKE pass`；两轮逐字一致） |
| 4 | `python tools/env.py run -- python tests/golden/smoke.py build/release-dev` | **`SMOKE total=20 pass=20 fail=0`** 逐字（20/20 VERIFY OK；两轮同值，金样与夹具零改动） |
| 5 | `python tools/env.py run -- python tools/ci-tidy.py build/release-dev` | 首跑红 `TIDY total=287 baseline=264 files=72 violations=13 seed=0`（violations=13 = 违规文件组数：12 文件超基线 + 1 基线外新文件 contextmenu.cpp；告警增量 **+23 = 287−264**，逐文件分解见 `m7r2-gate.md` §②）（R2 各包漏跑 tidy 的 fallout）→ 修复后 **`TIDY total=264 baseline=264 files=71 violations=0 seed=0`**（棘轮只减不增：基线文件零改动、未用 `--update`）。**R2.1 追记**：R2G 的 11 处行级 NOLINT 已撤销、基线随 F4 两次重播种至 **`TIDY total=263 baseline=263 files=75 violations=0 seed=0`**（§7.4、adjudications 十一节） |
| 6 | `python tools/env.py run -- python tools/regression.py` | **`regression: zero diff (baseline reproduced)`**（2448 vs 2448；修码前后各一轮）。首跑曾 `DIFF FOUND (54 lines)`，逐行归因 = `full-jpeg` 节 `rgb16.tif` 收尾 rename 遇 OS 瞬态锁（`atomic commit: cannot replace …: Access is denied.`，`fsops.cpp:541`）**级联 4 类行计数**（row state=ok 23→22 / failed 4→5 / row-detail error 4→5 / run-summary bytes 14236→13723，与该文件 513B 产物逐字对上——**与 R2-gate 重播种的「汇总级联 4 对」不是同一对象**）；同二进制复跑 ×2 均 zero diff ⇒ **瞬态环境噪声，0 行真实变化，不重播种**。**R2.1 追记**：该瞬态锁缺口已由 F1 `commit_temp_file` rename 有界重试根治（M7R21F1-2） |
| 7 | 密钥扫描（`docs/m6-report.md` §6 口径：`git log --all -p` 全历史 254,927 行 + 工作树 `git grep`） | 值形态 **0 命中**：`sk-[A-Za-z0-9]{16,}`=0、`amap_key\s*[=:]\s*<16+字符值>`=0、`\b[0-9a-f]{32}\b`=0；`amap_key` 名字面 228（历史）/104（工作树）命中全为键名/字段名/文档 |

tidy fallout 修复口径（详单见证据文件 §③）：**修码 12 条**（机械修正：显式乘法提升 / qsizetype
收窄 / const 引用 / 死代码删除 / 形参序以 scoped enum 分隔）+ **消音 11 条**（NOLINT + 契约注释：
6×AVX2 内核 intrinsics 结构性误报 + 2×内核形参组同文件先例口径 + 3×出参对/探针签名/QtConcurrent
任务面）；消音只覆盖 R2 新增行，存量告警计数面原样（kernels 31 条保留）⇒ 总数恰回 264 = 基线。
**R2.1 追记（现行口径）**：上述 11 处消音已被 F4 **全部撤销**（M7R21F4-0 处置翻案：替代方案
a「形参组结构化」为主 + b「授权重播种 + 归因」兜底），仓库恢复零 NOLINT 抑制；tidy 基线
264/71 → 264/76 → 263/75（§7.4、§3.7 R2.1 追记、adjudications 十一节）。

#### 未覆盖 / 未决（如实）

1. **回归首跑瞬态锁**（已归因、当时登记不修）：输出名碰撞（`base/rgb16.png` 与 `base/rgb16.tif`
   的 `$dir/$file` 模板同产 `base/rgb16.jpg`，conflict=overwrite）的 rename 替换路径对瞬态文件锁
   敏感，与并发负载同现一次。判定为环境噪声（同二进制 ×2 zero diff）；R2G 期登记为已知差异
   **D-1**（AGENTS §9 实现期发现号，本仓首例；裁定登记 = adjudications 八节 R2G-G5）。
   **R2.1 终态：已根治**——F1 `0555947` `commit_temp_file` rename 有界重试（仅 Windows 共享
   违规类 5/32/33、≤3 次、退避 25/50/75ms），M7R21F1-2（adjudications 十二节）。
2. **tidy 消音 11 条的口径裁定**（已汇入 `docs/m7-adjudications.md` 八节 R2G-G1/G2——R2G 各包裁定
   按 `m7r2-plan.md` §4-9 汇入 adjudications，不再只留 gitignored handoff；原始登记仍存
   `build/m7r2-g/handoff.md`）：NOLINT 为**零语义面**
   处置（注释级，基线/判据零改动），亦可在主对话改判为「形参组结构化 / `--update` 重播种 +
   归因」任一方案；两方案的替代成本已写入 handoff，供复核翻案。**R2.1 终态：翻案已执行**
   （M7R21F4-0）——11 处消音全撤销、改结构化 + 授权重播种，仓库零抑制恢复（§7.4）。
3. **真机验收（§2.4）不在本门禁面**：offscreen 判据无法替代的 10 项仍待人工签收（UX-7/UX-13）。
4. **CI 远端面未跑**：`main-full`/`nightly` 的 tidy 棘轮与 20min 预算读数以 R2C 取证
   （`m7r2-c-ci-budget-timings.md`）为准；本包判据为本地门禁树实跑，与 CI 同判据同工具。
5. **发布链**（R2REL）不在本包 scope：v0.3.3 winzip 预算超时的处置仍按 §3.8 停在
   「等裁决」状态，本门禁不动。

---

## 7. R2.1 轮交付（M7-R2.1-F1…F6，2026-10-12～13）

> R2.1 轮 = R2 发行暂停后的收口窗：Linux 回归修复（F1）、R2E1 重做（F2）、R2E2 结构余量（F3）、
> 跨界面收口批（F4）、文档与台账终稿（F5）、终门禁复核（F6）。逐条终态已并入附录 A/B（**R2.1
> 终态四档**）；裁定见 adjudications 十一/十二节；验证证据三段式摘录见
> [docs/m7-evidence/m7r21-f{1,2,3,4}-gates.md](m7-evidence/) 与终门禁
> [docs/m7-evidence/m7r21-gate.md](m7-evidence/m7r21-gate.md)。
> **本章只给各包交付清单与验证证据，逐条 R-id 去向以附录 A/B 为准。**

### 7.1 F1「Linux 回归定位与修复」（4 提交：`3339492`/`b673ee4`/`d4099bd`/`0555947`）

- **失败注解遥测（M7R21F1-1/-1b，永久保留）**：main-full linux ctest 失败明细 → `::error`
  注解（失败测试名 + 失败断言行）+ `$GITHUB_STEP_SUMMARY` 全量明细；二版只登记真失败、
  注解封顶 10 且首条 = 名单汇总（一版实证泛滥 35 条 + 截断真失败名）。
- **test_fsops 长路径断言样例按平台拼写**（M7R21F1-3）：POSIX fs::path 不切分 `\` 导致
  `C:\out\…` 整串被非法字符防线误拒 ⇒ Linux 必红；#ifdef 明确分支、两侧断言逐条相同、
  严格度不变（断言不放宽）。
- **`commit_temp_file` rename 有界重试**（M7R21F1-2）：仅 Windows 共享违规类 5/32/33、≤3 次、
  退避 25/50/75ms——**D-1 根治**（R2G-G5 登记的瞬态锁缺口）。
- **验证**：本机 ctest **35/35** + 金样 **20/20** + 本包两文件 tidy 与基线逐项相等；**最终判据
  = main-full run #47 @ `0555947` linux（全 ctest+金样）job = success**（八条红的可代码级
  归因 2 条已修，其余 ≤7 条成因不可判定——如实登记 §4.3）。证据：
  [docs/m7-evidence/m7r21-f1-gates.md](m7-evidence/m7r21-f1-gates.md)。

### 7.2 F2「R2E1 重做：page_meta + mapwidget RTL 与结构拆分」（12 提交：`b83f185`…`d6360f6`）

- **B.1 page_meta 拆分十步**（deep-review 附录 B.1；`page_meta.h` 逐字节未动）：B2 meta_format
  （15 纯函数逐字迁出）→ C2 meta_state（MetaState/常量/构造器 + meta 命名空间外部链接）→
  B4 meta_probe（ProbeScanner，**R-181 UI 内嵌 IO 剥离收口**）→ B3 meta_rules（RuleInputs
  纯核 + tag 行显式清单，R-180）→ B5 四卡（meta_card_{privacy,info,time,gps}）→ B6
  meta_map_overlay（MapOverlayController）→ B7 meta_style（R-179 banner 拼串收口）→
  B1 收尾（**R-177** MetaState 六域子聚合 + 925 处访问点单遍改写，page_meta.cpp 收敛 291 行
  薄壳）。
- **R-218 RTL（本面）**：page_meta/mapwidget 自绘几何/对齐逻辑坐标化 + 单点镜像（LTR 恒等）；
  RatingStars 星序逻辑化；mapwidget HUD 镜像只限界面构件（瓦片/图钉等地理投影不镜像）。
- **R-180 余量 + R-182/R-258 后半**：`meta_probe_context.h` 加性新头——`hook::` 钩子名单源
  （12 名）+ `objname::` objectName 词表（111 名）+ `ProbeContext` 类型化访问器；LoadingGuard
  RAII、InfoSlot 具名索引、常驻子控件显式标记。
- **B.5① map_view**：投影/基准纯核入 pp_core（附录 B.5 步①；步②–⑥未做，§4.2 登记）。
- **验证**：全量 ctest **35/35**（每步一轮）+ 地图域单测 **5/5** + ui_smoke 冻结行 +
  meta 四探针行与锚**逐字节相同** + B2 函数体 15/15 逐字 + RTL 临时探针四查 OK。证据：
  [docs/m7-evidence/m7r21-f2-gates.md](m7-evidence/m7r21-f2-gates.md)。

### 7.3 F3「R2E2 结构余量」（10 提交：`6250b3b`…`0ca8c62`）

- **R-254** byte_text 字节读数单源 + remove_sel 判据/选中映射单点；**R-256** apply_map_style
  名实拆三职；**R-259** 析构期 join 带反馈同步（唯一明文授权行为变化，M7R21F3-10）；
  **R-185** page_run 注册表 unique_ptr RAII（page_run.h 加性 `~PageRun()`）；**R-187/188**
  RowBundle 控件束 + out_kind_of 状态机互译单点 + refresh_widgets 7 段；
  **R-196/197/198/199/205** exif_editor 孪生收敛 + time_tag_keys 9 键单表 + 三态去魔法索引 +
  状态写入权成文；**R-061/R-201** SettingsDialog ctor 563 行拆 5 个 build_*_page；
  **R-200** filelistmodel 一 TU 6 类按所有权拆 TU；**B.3 余量** format_selection_model
  （R-189/192）+ output_page_build/output_template_ui TU 拆分；**R-255** smoke_probe_meta
  1675 行/深 10 → 12 分节方法（节体零文本改动）。
- **验证**：全量 ctest **35/35** + ui_smoke 冻结行（分项 filelist-geom 17 / settings-geom 39 /
  output-geom 29 / folds 6 / context-menu 36 全绿，meta-* 逐字）+ test_smoke 第 7/8/9 节 +
  金样 **20/20**。证据：[docs/m7-evidence/m7r21-f3-gates.md](m7-evidence/m7r21-f3-gates.md)。

### 7.4 F4「跨面包收口」（7 提交：`dcedf4c`…`81d0b34`）

- **NOLINT 处置翻案（M7R21F4-0，主对话裁定）**：撤销 R2G 11 处行级消音 → 零行为结构修复
  14 处（参数组 struct 9 + Win32 显式转换封装 5）+ 无零行为修法 7 处按 M7-ADJ-tidy-reseed
  口径重播种 + 逐条归因；仓库零抑制恢复。tidy 基线 264/71 → 264/76 → **263/75**（终读数
  `TIDY total=263 baseline=263 files=75 violations=0 seed=0`）。
- **跨界小项 14 条全清**（`93001a9`）：R-195 三调用点转发（first-wins 单源，边界语义披露
  M7R21F4-9）、R-007 余量归一（normalize_extended_path）、R-145 --dev usage 醒目标注、
  R-239 冲突策略映射 core 单源、R-319 --base GUI 入口端到端、R-137 map_style 词表落
  core/settings.h、R-133 孤儿信号 format_changed 删除（冻结面删除追认）、R-065 README 措辞、
  R-269 note 占位收口、R-264 三套字节格式化收敛 byte_text、R-279 Caption 双实现合并、
  R-081 构造期 read_metadata 后台化、R-204 camera_model 加性访问器、R-260 注释改实 +
  UX-11 page_meta 侧锁卡（GPS 卡不锁注记 M7R21F4-5）+ enc_heif 内省参数 group 显式声明 +
  dev_cli state_text 补 DoneWithErrors。
- **色彩四选项 GUI 接线**（`6ff43f9`，M7R21F4-4）：page_output 全局卡 pp-intent/pp-gamut/
  pp-dither8/pp-linear 四控件（**opt-in 默认 0/false = 现行为**）+ config_base 读入 RunConfig
  + PreviewCache/PreviewPanel linear_resample 加性透传（切换清缓存）。
- **第 4 波常驻探针批**（`d737cb4`）：intake（R-148/149）/ classify-filter（R-143）/
  edit-target（R-152/153）/ settings-tabs-light（UX-12 像素档）四节，冻结行
  `UI-SMOKE OK shots=12 pages=3` 逐字不变；并修复探针抓出的两个真实缺陷——R-153 尾单
  （restore_selection 塌缩一行，M7R21F4-6）与 R-183 前半（rebuild_rows teardown 断信号面）。
- **评审本体入库**（`dcedf4c`）：deep-review/ux-review/m6-map-upgrade/gui-fix-mockups 四件
  入库 + UX-7 证据图移入 docs/m7-evidence/（评审与改动分离）。
- **验证**：ctest **35/35** + ui_smoke 冻结行 + 新探针 4 节（3/5/3/4 断言全绿）+ 金样 **20/20**
  + tidy 终读数 263/75 + NOLINT 指令 grep = 0 + clang-format 收口 0 diff。证据：
  [docs/m7-evidence/m7r21-f4-gates.md](m7-evidence/m7r21-f4-gates.md)。

### 7.5 R2.1 验收结论与残余（如实）

**六包判据全绿**（F1–F4 各包专属树实跑，读数摘录见上；F5 纯文档；**F6 终门禁于 2026-10-13
在门禁树 `build/release-dev` 全量实跑**）。冻结行 `SMOKE total=20 pass=20 fail=0`、
`UI-SMOKE OK shots=12 pages=3`、`--version` 单行、`pp_verify` VERIFY 行式**逐字不动**；
金样/夹具/回归基线零改动。

**R2.1 终门禁验收证据（M7-R2.1-GATE / F6，2026-10-13，门禁树 `build/release-dev`；
逐条取证三段式见 [docs/m7-evidence/m7r21-gate.md](m7-evidence/m7r21-gate.md)）**：

| # | 判据 | 命令 | 终值 | 过/不过 |
|---|---|---|---|---|
| 1 | 格式 | `"C:/Program Files/Git/bin/bash.exe" tools/ci-format.sh` | `violations=0`（187 文件，钉版 clang-format 23.1.1） | **过** |
| 2 | 门禁树构建 | `python tools/env.py run -- cmake --build build/release-dev` | exit 0；复跑 `ninja: no work to do.` | **过** |
| 3 | ctest 全绿 | `python tools/env.py run -- ctest --test-dir build/release-dev --output-on-failure` | `100% tests passed out of 35`（46.10s） | **过** |
| 3b | ui_smoke 冻结行 | `… ctest --test-dir build/release-dev -R ui_smoke -V` | `UI-SMOKE OK shots=12 pages=3` 逐字 | **过** |
| 4 | 金样 20 对 | `python tools/env.py run -- python tests/golden/smoke.py build/release-dev` | `SMOKE total=20 pass=20 fail=0` 逐字 | **过** |
| 5 | tidy 棘轮只减不增 + 零 NOLINT 新增 | `python tools/env.py run -- python tools/ci-tidy.py build/release-dev` | `TIDY total=263 baseline=263 files=75 violations=0 seed=0`（基线文件零改动）；NOLINT 指令 grep = 0 命中 | **过** |
| 6 | 回归 zero diff | `python tools/env.py run -- python tools/regression.py` | `regression: zero diff (baseline reproduced)`（2448 vs 2448，**首轮**；未 `--update`） | **过** |
| 7 | 密钥 0 命中 | `python build/m7r21-f6/leak_scan.py` + `git grep` 工作树 | 值形态 sk / amap-value / hex32 两面 = 0（名字面仅键名/字段名/文档） | **过** |

- **fallout = 0**：八项首轮全绿，无修复、无换锚裁定、无基线重播种。回归失败槽位（full-*
  `failed=4` = 2×cmyk + 2×corrupt、full-avif 另 +4 = svt-av1 十位 alpha 边界、meta-*
  `failed=4` = Exiv2 只读面）逐槽与基线一致，归因见 gate 文档 §②-6。
- **残余（如实）**：① ~~全语料回归本窗口未跑~~ → **已由 F6 终门禁补跑闭合**（zero diff）；
  ② F1 八条 Linux 失败中 ≤7 条成因不可判定（§4.3、F1 handoff ⑥-1）；③ 结构余量未尽项——
  B.5 mapwidget 步②–⑥、R-254 的 T()/tr() 与 Impl 分舱、R-256 后半 ev.index、R-201 后半
  对象图成员化、R-183 后半行部件复用级解耦、UX-8 组折叠头整行可点（§4.2 / 附录 A 格内注记）；
  ④ 探针需求未落项——0 字节双跑、R-144 失败清单、format_selection_model 往返、RTL 深水区、
  ProbeContext 全面接入、色彩四选项档位探针等（F4 handoff ⑤，9 条）。
- **发行状态不变**：0.3.3 仍未发行（tag `v0.3.3` 停在 `b1af371` 零 Release 零资产）；
  M7-ADJ-tag-move-2 授权在案未消耗（§3.9）。

---

# 附录 A：覆盖台账 —— deep-review R-001～R-331（逐条去向）

去向口径（每行**只标一个**主标签；余量以格内注记说明）。**R2.1 终态四档**（2026-10-13 终态，
331+14 全部条目落档）：**已修**（含提交哈希，R1/R2/R2.1 代次可并列；余量以格内「余量登记」
注记）/ **误识别关闭**（评审前提不成立、被证据证伪，不为关闭而改码）/ **受约束口径闭环**
（平台、依赖、冻结面等外部约束下按约束收口并登记，裁定表九节 R2F-C*）/ **登记**（已知差异/
结构性债，理由在案）。R1 轮历史标签到四档的折算（本表已全部改写为终态标签，此注供旧版本
对照）：「部分」→ 主修已落且余量注记者并入**已修**、主修未尽者并入**登记**；「跨界」→
R2/R2.1 认领者**已修**、未认领**登记**；「对账登记」（R-292）→ **登记**（注记对账动作）；
「结案」（R-331）→ **误识别关闭**（结论性条目随唯一实缺陷闭合）。评审本体只读未改；本表为
唯一对账面。行数自检：A.1=2 + A.2=33 + A.3=144 + A.4=152 = **331**（python 正则逐行核过；
与 deep-review 逐条优先级标签一致——其 §1 汇总行「P1=32、P2=145」系评审本体笔误，逐条标签
实为 P1=33、P2=144，只读不改、此处注明）。终态四档计数自检（F5 2026-10-13，python 正则
核过）：331+14 = 345 行 = **已修 326 + 登记 15 + 误识别关闭 3 + 受约束口径闭环 1**
（R2 终态为 297/44/3/1；R2.1 认领 28 条「登记→已修」+ R-183 1 条，见 §7）。
**成员对照（防「3↔3」「4↔1」误读；逐成员列全）**：
- 「误识别关闭 3」（台账主标签）= **R-011 / R-282 / R-331**；
- 九节 R2F-X1–X3（三条误识别登记）= **X1=R-282 死码前提 / X2=R-011 证伪 / X3=WP5-A10
  任务书笔误（落在 R-052 行）**。
  两组各 3 条、**成员并不重合**：R-331（结论条目，随 R-152 闭合结案，无九节条目、依据在
  附录 A 行内）只在台账组；R-052 只在九节组且其主标签 = **登记**（笔误面以 R2F-X3 行内注记）。
  即：R-011↔X2、R-282↔X1 一一对应，R-331（台账）与 R-052（X3）是两条不同的记录。
- 「受约束口径闭环」：台账主标签 **1 条 = R-325**（R2F-C4）；九节 R2F-C1–C4 **共 4 项**——
  其余三项（B3 BMP ICC→UX-14 行注记、非 Windows 密钥→R-094 行注记、Linux golden.log→
  R-015 行注记）以注记并入对应「已修」行（4 项受约束登记 vs 1 个受约束主标签）。

## A.1 P0（2 条）

| R-id | 去向 | 依据 / 落点 |
|---|---|---|
| R-001 | **已修** `12ce994` | `same_output_as_source()` 拦截（pipeline + metaonly）；test_pipeline_contract r001/* |
| R-002 | **已修** `12ce994` | exiv2 原子写回 + 写失败=失败（B1）；test_metadata r002/* |

## A.2 P1（R-003～R-023、R-150～R-161，33 条 = 21 + 12）

| R-id | 去向 | 依据 / 落点 |
|---|---|---|
| R-003 | **已修** `12ce994`+`33d09a8` | fsops 原子写四件套 + 五编码器接线 |
| R-004 | **已修** `12ce994` | srcmeta.error 非空=文件失败 |
| R-005 | **已修** `12ce994` | reserved 仅记成功输出 + 软/硬保留（B3） |
| R-006 | **已修** `d99df1b` | on_run_again 补齐语义（B35） |
| R-007 | **已修** `12ce994`+`5d19146`+`93001a9` | 预检+manifest（`12ce994`）+ \\?\ 归一与 259/255 硬线解除（`5d19146`）+ R2.1 余量收口：enc_heif/enc_oiio 写盘点与 resolve_conflict exists 探测归一 `normalize_extended_path`（`93001a9`，E5 余量清零） |
| R-008 | **已修** `d99df1b` | Progress 不落列表状态 |
| R-009 | **已修** `7472b97`+`6e001da` | 构造期 `7472b97`；theme_replay_panels 补 page_run 令牌重放 `6e001da`（§4.1-1 闭环） |
| R-010 | **已修** `33d09a8` | png:compressionLevel 键名 + 体积差断言 |
| R-011 | **误识别关闭**（诊断被证伪；键名对齐 `33d09a8`） | 评审「无人消费」诊断被 OIIO `tiffoutput.cpp:1026-1029` 证伪（§3.3）；实际病灶=键名大小写，对齐后生效 + float 显式拒绝（`33d09a8`） |
| R-012 | **已修** `a6e29f6` | 联想世代守卫 + abort |
| R-013 | **已修** `a6e29f6` | redact_secrets 单点脱敏 |
| R-014 | **已修** `18fb355` | actions 全钉 commit SHA |
| R-015 | **已修** `18fb355` | --update 归因护栏（M7-GATE 两次实证）；Linux 基线移植**受约束登记**（R2F-C3） |
| R-016 | **已修** `18fb355` | nightly regression-windows job |
| R-017 | **已修** `18fb355` | halt_on_error=1 + Temporary 扫描 |
| R-018 | **已修** `18fb355` | corpus.sha256 入库锁语料 |
| R-019 | **已修** `18fb355` | --require-baseline |
| R-020 | **已修** `7472b97` | ElidedLabel 7 份收敛 formkit（含 R-320） |
| R-021 | **已修** `7472b97`+`27c9f40`（余量登记） | card_header/make_card 单源（`7472b97`）+ page_output/page_run 已换（`27c9f40`）；余 page_meta/mainwindow 卡壳**登记** |
| R-022 | **已修** `7472b97`+`27c9f40`（余量登记） | formkit::make_button（`7472b97`）+ page_output/page_run 已换（`27c9f40`）；余 page_meta **登记** |
| R-023 | **已修** `12ce994` | classify::load 收紧（缺 classes 键=失败） |
| R-150 | **已修** `d99df1b` | PP_VERSION_STRING 徽标 |
| R-151 | **已修** `d99df1b` | PreviewPanel::navigated 同步选中 |
| R-152 | **已修** `d99df1b` | 双击 mapToSource 补全 |
| R-153 | **已修** `d99df1b` | 多选整组快照/恢复 |
| R-154 | **已修** `d99df1b` | 运行中拒收 drop |
| R-155 | **已修** `594d93f`/`40f3405`/`a6e29f6`/`38af841`/`edfc6e4`/`d6360f6`（余量登记） | 9 处巨型拆 4 处（mainwindow/run_one_file/build_gps_card/main.cpp）；R2.1 再消 3 半项——B.1 page_meta（`38af841` 收尾）、B.3 format_selection_model + output TU 拆分（`edfc6e4`）、B.5 步① map_view（`d6360f6`）；余量=B.2/B.3 尾/B.5 步②–⑥ **登记** |
| R-156 | **已修** `40f3405` | enc_common 收编 500 行拷贝 |
| R-157 | **已修** `40f3405`+`594d93f` | log_level_from_text 单源 + UI 收口 |
| R-158 | **已修** `a6e29f6` | 三塔收敛（W7-A3/A5） |
| R-159 | **已修** `25b60b3` | icons 相对 s 反射修复 + 单测 |
| R-160 | **已修** `40f3405` | seq_++ 入锁 |
| R-161 | **已修** `a6e29f6` | insert_tile 返回 bool、成功才复位 |

## A.3 P2（R-024～R-077、R-162～R-251，144 条 = 54 + 90）

| R-id | 去向 | 依据 / 落点 |
|---|---|---|
| R-024 | **已修** `d99df1b` | 增量计数 + 150ms 合并刷新 |
| R-025 | **已修** `6e001da` | 扫盘后台化 + 批内有序入列（`6e001da`；PP_UI_NO_STATE 下保持同步保判据确定性） |
| R-026 | **已修** `6e001da` | path→row 哈希回填 O(1)（`6e001da`） |
| R-027 | **已修** `18fb355` | tsan.supp 收窄（正对照配方在册；Linux 首轮验证） |
| R-028 | **已修** `12ce994` | 三份持久化 tmp 名带 PID/随机 |
| R-029 | **已修** `12ce994` | GPS ts 不再擦除 + Warning（B2） |
| R-030 | **已修** `12ce994` | metaonly 失败删本次输出 |
| R-031 | **已修** `12ce994` | collect_inputs 排除 out_root 子树 |
| R-032 | **已修** `12ce994` | 灰度/RGB ICC 降级 + Warning（UX-14 A2） |
| R-033 | **已修** `33d09a8` | downscale sc<4 掩码载入 + 最小宽度测试 |
| R-034 | **登记** | pixelbudget 饱和乘法未做（grep 取证：无饱和代码；极端宽高仍走通用异常） |
| R-035 | **已修** `12ce994` | GrayToRgbEncoded 升维告警（同 kind 去重） |
| R-036 | **已修** `18fb355` | clang-format sha512 执行前校验 |
| R-037 | **已修** `18fb355` | ci-seed-qt --sha256 必填 |
| R-038 | **已修** `18fb355` | 解包穿越补 Windows 面 |
| R-039 | **已修** `18fb355` | Python 工具链钉版 |
| R-040 | **已修** `18fb355` | warnings_exact/empty 三模式 + 20 对补断言 |
| R-041 | **已修** `18fb355` | sanitizer 夜间轮转 |
| R-042 | **已修** `18fb355` | sanitizer env 下沉 job 级 |
| R-043 | **已修** `18fb355` | pr-fast 补 ui-smoke 步骤 |
| R-044 | **已修** `18fb355` | 口径如实化（`18fb355`）；像素容差本体在 C++ 探针承载（C6） |
| R-045 | **已修** `a6e29f6` | clear_search_state 两路共用 |
| R-046 | **已修** `a6e29f6` | 精度存疑文案对齐几何 |
| R-047 | **已修** `a6e29f6` | 仅坐标变化才 center_on |
| R-048 | **已修** `594d93f` | 探针迁出 + PP_BUILD_DEV 门控（发布树 0 命中取证） |
| R-049 | **已修** `594d93f`（余量登记） | mainwindow 五域拆 TU；Impl 分舱**登记**（R2.1 另拆 output_page_build/output_template_ui/filelistmodel 六类，不涉 mainwindow Impl） |
| R-050 | **已修** `12ce994` | class_file 惰性默认（B4） |
| R-051 | **已修** `33d09a8` | anchor 轨删除 + 注册表期望键断言 |
| R-052 | **登记**（两行对偶=可达做法；任务书「一行」笔误误识别关闭 R2F-X3） | CMakeLists 源清单双维护未收口（两行对偶登记 B33）；任务书「一行」字面=笔误，误识别关闭 R2F-X3 |
| R-053 | **已修** `7472b97`+`27c9f40`（余量登记） | 同 R-021 |
| R-054 | **已修** `7472b97` | 三轨钩子收敛 ppFk |
| R-055 | **已修** `d99df1b` | warn_cannot_start/warn_preset_op 抽助手 |
| R-056 | **已修** `7472b97` | card_body 死函数删除 |
| R-057 | **已修** `33d09a8` | pack_rgba8_to_rgb8 死 kernel 删除 |
| R-058 | **已修** `7472b97` | GUI-R2 头注释按实现改写 |
| R-059 | **已修** `7472b97` | #dd8800 → warn 令牌 |
| R-060 | **已修** `a6e29f6` | providers 保留+标注「冻结遗留仅测试可见」（W7-A5） |
| R-061 | **已修** `a6e29f6`+`40f3405`+`334c8c1` | build_gps_card / run_one_file 已拆；settings ctor 563 行按 5 页签拆 build_*_page（`334c8c1`，R2.1-F3）——余量清零 |
| R-062 | **已修** `18fb355` | nightly/warm-cache 单源断言补齐 |
| R-063 | **已修** `18fb355` | Qt 版本字面量清零 |
| R-064 | **已修** `7472b97` | theme::replay_tokens 单点重放 |
| R-065 | **已修** `40f3405`+`f3e1903`+`27c9f40`+`93001a9` | 引擎注释 + settings 文案/tooltip 改实（`40f3405`+`f3e1903`）；R2D2 补运行页 tooltip 耦合提示（`27c9f40`）；R2.1 README「默认物理核数」措辞改实（`93001a9`） |
| R-066 | **已修** `27c9f40` | 预设落值跳过 avif+alpha 预选 + alpha 提示两态如实（`27c9f40`） |
| R-067 | **已修** `d99df1b` | 运行前提示被过滤隐藏文件 |
| R-068 | **登记** | 热键打标作用域与勾选集承诺不一致未修 |
| R-069 | **已修** `d99df1b` | 0 字节琥珀呈现 + StateToneRole |
| R-070 | **已修** `40f3405`+`5d19146`+`27c9f40` | ErrorKind 单源（`40f3405`）+ `core/error_text.h`（`5d19146`）+ UI 接线（`27c9f40`，run-reason 探针钉） |
| R-071 | **已修** `d99df1b` | 运行中拒收拖放（含 R-154） |
| R-072 | **已修** `27c9f40` | 打开设置自检 settings.ini + 非阻塞 warn 提示条（`27c9f40`） |
| R-073 | **已修** `d99df1b` | 输出根预检 + 失败档文案（B42） |
| R-074 | **已修** `d99df1b` | metaonly GUI 默认 Skip（B35） |
| R-075 | **已修** `d99df1b` | GPS 规则预检确认 |
| R-076 | **已修** `d99df1b`+`5d19146` | UI 逐输出补判（`d99df1b`）+ 引擎 `file_state_of(Verdict)` 单源映射（`5d19146`） |
| R-077 | **已修** `a6e29f6` | 空 Key 预检引导文案 |
| R-162 | **已修** `40f3405` | 错误收尾口径统一进 IEncoder 契约 |
| R-163 | **已修** `33d09a8`+`c540630` | 五编码器消费 cancelled（`33d09a8`）+ pipeline 归类单点（`c540630`，E3 闭环） |
| R-164 | **已修** `40f3405`（三源键表登记） | warn_unknown_params 单实现；三源键表未合一**登记** |
| R-165 | **已修** `40f3405` | img.roi() 取样口径统一 |
| R-166 | **已修** `40f3405` | chroma 行组滚动缓冲（384MB→128KB） |
| R-167 | **已修** `40f3405` | enc::write_file + RAII 统一落盘 |
| R-168 | **已修** `40f3405` | finalize_aggregate/fail_fill 单源 |
| R-169 | **已修** `12ce994` | plan_targets 死分支删除 |
| R-170 | **已修** `40f3405` | ProgressMux 事件序列锁口径 |
| R-171 | **已修** `12ce994` | const_cast 消灭、双通道 warning 拆分（B18） |
| R-172 | **已修** `40f3405` | OutputState 显式枚举 + set_output_state |
| R-173 | **已修** `40f3405` | validate_run_config 统一校验器 |
| R-174 | **已修** `40f3405` | cancel_exit 单点 + D1 死锁序成文 |
| R-175 | **已修** `40f3405` | path_ctx_for 组装单源 |
| R-176 | **已修** `40f3405` | ErrorKind + error_kind_of 前缀判据单源 |
| R-177 | **已修** `38af841` | MetaState 拆 Core/FollowModel/四卡子聚合（`38af841`，R2.1-F2 B1 收尾）；925 处访问点单遍改写，state_of 改 qobject_cast + Q_ASSERT |
| R-178 | **已修** `a6e29f6` | commit_rule_change 四连刷收口 |
| R-179 | **已修** `7472b97` | apply_style QSS 收口进 theme（field_qss 等） |
| R-180 | **已修** `9bd01b6`+`6bd2c15` | RuleInputs 值聚合 + tag 行显式清单（`9bd01b6`）+ LoadingGuard RAII / InfoSlot 具名索引 / 常驻子控件标记（`6bd2c15`，R2.1-F2） |
| R-181 | **已修** `b83f185`+`bb5290c` | 显示格式化纯函数 meta_format 可单测（`b83f185`）+ UI 内嵌 IO 剥离进 meta_probe（`bb5290c`，page_meta 与各卡 TU 零 IO） |
| R-182 | **已修** `6bd2c15` | objectName 三方事实契约收口 `meta_probe_context.h` 词表单源（111 名）+ ProbeContext 类型化访问器（`6bd2c15`） |
| R-183 | **已修** `d737cb4`（余量登记） | rebuild_rows 旧代 teardown 与 deleteLater 解耦（断信号面）+ 探针不跨 pump 缓存部件指针（`d737cb4`，M7R21F4-6）；余量=「行部件复用」级解耦**登记**（结构性重构） |
| R-184 | **已修** `6e001da` | make_chip 收编 formkit::ChipButton 唯一渲染路径（R2D1；实现随后随 B.1 拆分迁至 `src/ui/meta_state.cpp:152`，R2.1-F2）；deep-review 病灶记在 classify_panel，实际双实现落点在 page_meta |
| R-185 | **已修** `60742b0` | page_run 注册表收敛 `std::map + unique_ptr` RAII（`60742b0`，R2.1-F3；page_run.h 加性 `~PageRun()`，M7R21F3-2） |
| R-186 | **已修** `7472b97`+`27c9f40` | button padding 单源；卡构造双份随 R-021 登记 |
| R-187 | **已修** `594d93f`+`0a6655d` | run_rows_model 抽数据语义（`594d93f`）+ OutRow/FileRow 收敛 RowBundle 唯一控件束（`0a6655d`，R2.1-F3）——双 struct 余量清零 |
| R-188 | **已修** `0a6655d` | refresh_widgets 拆 7 段 + FileState↔OutKind 互译 `out_kind_of_file_state()` 单点（`0a6655d`，R2.1-F3） |
| R-189 | **已修** `edfc6e4` | 「已选格式」收敛 `format_selection_model.h` FormatSelection 唯一真相（`edfc6e4`，R2.1-F3 B.3；test_smoke 第 9 节真值表钉住） |
| R-190 | **已修** `52b9870` | currentData/findData 读回、四件 index 映射函数删除（`52b9870`） |
| R-191 | **已修** `52b9870` | format_policy.h 单源 alpha_combo/avif_preselect_needed（`52b9870`） |
| R-192 | **已修** `edfc6e4` | 后端覆盖标记 per-format（FormatState::backend_user_set + last_backend，全局两槽退役；set_batch_has_alpha 逐格式清，`edfc6e4`） |
| R-193 | **已修** `40f3405` | main.cpp 拆四 TU |
| R-194 | **已修** `12ce994`（UTF-8/FILE* 登记） | 原子写收敛 fsops；UTF-8 校验/FILE* 读**登记** |
| R-195 | **已修** `5d19146`+`27c9f40`+`93001a9` | first-wins 单源 `pp::find_param/lookup_param_def`（`5d19146`）+ paramform 转发（`27c9f40`）+ preset_io/dev_cli 两调用点转发、last-wins 循环退役（`93001a9`，M7R21F4-9 边界语义披露）——三调用点全收口 |
| R-196 | **已修** `c900f39` | exif_editor 三对孪生收敛（过滤 `apply_tree_filter` / 时间清除 `upsert_remove_edit` / Row 填充 `add_source_row`+`finalize_row`，`c900f39`） |
| R-197 | **已修** `c900f39`（行为裁定登记） | 时间标签集合 4 份真相 → `time_tag_keys.h` 9 键单表（wall_clock 清除集 5 键与引擎写路径同集，test_smoke 第 8 节钉）；「清除是否连带 OffsetTime*」行为语义维持现状**登记**（F3 ③-4，待裁定） |
| R-198 | **已修** `c900f39` | EXIF 类型触点收敛 finalize_row；exif_type 下拉参数更名 fallback_type 并如实注释（`c900f39`） |
| R-199 | **已修** `c900f39` | TriMode/TimeMethod/PrivacyMode 枚举 + itemData 单真相，currentIndex()==1/2 魔法索引退役（`c900f39`） |
| R-200 | **已修** `355e1f2` | filelistmodel 一 TU 6 类按所有权拆 TU（filedelegate/filegroup_proxy/filegroup_delegate/file_collect/group_scan，`355e1f2`）；跨 TU 共享纯件收 `filelistmodel_util.h` |
| R-201 | **已修** `334c8c1`（余量登记） | SettingsDialog ctor 拆 build_general/output/run/map/about 五页（`334c8c1`）；余量=settings()/恢复默认 lambda 的 findChild 字符串对象图成员化**登记** |
| R-202 | **登记** | 三套异步脚手架 |
| R-203 | **已修** `52b9870` | dataChanged 按行发 [列0,列1] 区间（`52b9870`） |
| R-204 | **已修** `93001a9` | core/metadata.h 加性访问器 `camera_model()`（`93001a9`，M7R21F4-7）；group_scan 转发、filelistmodel 双残留 include 退场 |
| R-205 | **已修** `c900f39` | FileRow 状态矩阵 7 维×14 态收口：filelistmodel.h 契约块补「状态写入权」条目（`c900f39`，M7R21F3-4 注释级） |
| R-206 | **已修** `33d09a8`+`5d19146` | 通道归属退场、双通道共用规则（`5d19146`，M7R2B-2） |
| R-207 | **已修** `33d09a8` | kKey* 绑键常量 |
| R-208 | **已修** `33d09a8` | select_tech/declares_lossless 合一 |
| R-209 | **已修** `33d09a8` | 镜像 ParamDef 工厂去重 + quality 一键两义注记 |
| R-210 | **已修** `12ce994` | JSON 栈标注边界 + 保留裁定（C3） |
| R-211 | **已修** `33d09a8`+`5d19146`+`27c9f40`（强类型受约束登记 R2F-C4） | 单位判定/换算/标签单源（`5d19146`）+ UI tooltip 消费（`27c9f40`）；强类型拆字段=受约束登记（R2F-C4） |
| R-212 | **已修** `7472b97` | formkit 死 API 删除（paint_spin_arrows 判定修正保留） |
| R-213 | **已修** `7472b97` | QSS 盒模型注释矛盾改写 |
| R-214 | **已修** `7472b97` | 死值清理（pill_radius_qss/bottom_bg/row_h/kFkCheck）；go_text 保留 |
| R-215 | **已修** `7472b97` | kFkCheck 死钩子+死 QSS 删除 |
| R-216 | **已修** `7472b97` | 双行高并存清零（真源 kRowPitch=28） |
| R-217 | **已修** `27c9f40` | make_editor 工厂收敛三处平行 switch（`27c9f40`） |
| R-218 | **已修** `52b9870`+`40c385e` | R2E2（`52b9870`）：逻辑坐标+单点镜像，LTR 逐字节不变，rtl-mirror 探针钉关系；R2.1-F2（`40c385e`）：page_meta/mapwidget 本面——对齐逻辑化 11 处 + RatingStars 星序 + HUD 单点镜像（地理投影不镜像，M7R21F2-7） |
| R-219 | **已修** `a6e29f6` | reverse_url 入参 WGS-84 单约定（B44） |
| R-220 | **已修** `a6e29f6` | 子域纯函数（B45） |
| R-221 | **已修** `a6e29f6` | paint 幂等（请求移出绘制） |
| R-222 | **已修** `a6e29f6` | 冻结叙事注释改对 |
| R-223 | **已修** `a6e29f6` | result_key 稳定键对齐（W7-A2） |
| R-224 | **已修** `a6e29f6` | layers kSources 表驱动 |
| R-225 | **已修** `d99df1b` | 同 R-141（防御收尾回写 file.state） |
| R-226 | **已修** `27c9f40` | select_format 去下标取磁贴 + Q_ASSERT（`27c9f40`，§4.1-5 闭环） |
| R-227 | **已修** `d99df1b` | apply_thumb 不覆盖流水线状态 |
| R-228 | **已修** `27c9f40` | refresh_disk 拆出 + 400ms 去抖（`27c9f40`） |
| R-229 | **已修** `d99df1b` | remove_tab 先快照（git -S 取证） |
| R-230 | **已修** `33d09a8` | heif 内省默认值取插件当前值 |
| R-231 | **已修** `5d19146` | cross_validate 通道 + 未知键显式拒绝（`5d19146`，§4.1-2 闭环） |
| R-232 | **已修** `12ce994` | 同 R-023（缺 classes 键=失败） |
| R-233 | **已修** `a6e29f6` | 时区具名表 + 动态补项 |
| R-234 | **已修** `12ce994` | OffsetTime 以墙钟实改为条件 |
| R-235 | **已修** `12ce994` | 全跳过早退发输出级进度 |
| R-236 | **已修** `40f3405` | conflict 打印生效值 |
| R-237 | **已修** `40f3405`+`f3e1903`+`27c9f40` | dev usage + settings 文案改实；详见 R-065 |
| R-238 | **已修** `40f3405`+`27c9f40` | 本页两处消费改调 `pp::default_bitdepth_for`（`27c9f40`） |
| R-239 | **已修** `93001a9` | ConflictPolicy↔文本映射上收 core 单源（fsops.h 加性 `conflict_policy_name/parse_conflict_policy`，`93001a9`，M7R21F4-7）；dev_cli/preset_io 四副本全转发 |
| R-240 | **已修** `f3e1903`+`594d93f` | 三处注释相反本包改实（日志尾/列宽尺规/set_plan）；warn_preset_op 接线 `594d93f`（W9-8） |
| R-241 | **已修** `d99df1b` | remove_tab 注释纠偏（git -S 取证） |
| R-242 | **已修** WP11（D5） | path() 注释改实 |
| R-243 | **已修** `40f3405` | bench-report.json 转义 |
| R-244 | **已修** `33d09a8` | arith_code 恒锁 + 禁用原因 |
| R-245 | **已修** `33d09a8` | xyb_mode 恒锁 + 禁用原因 |
| R-246 | **已修** `33d09a8` | dct_method 恒锁 0 + 禁用原因 |
| R-247 | **已修** `33d09a8` | psnr_target 联动提示 |
| R-248 | **已修** `d99df1b` | remove_rows 段合并 |
| R-249 | **已修** `27c9f40` | finished 钩子回滚 env/持久化档到构造快照（`27c9f40`） |
| R-250 | **已修** `d99df1b` | 运行读数单账本 |
| R-251 | **已修** `6e001da` | build_ui 开头先填 d.tokens（`6e001da`） |

## A.4 P3（R-078～R-149、R-252～R-331，152 条 = 72 + 80）

| R-id | 去向 | 依据 / 落点 |
|---|---|---|
| R-078 | **已修** `40f3405`+`f3e1903`+`27c9f40` | RunSummary 三字段（`40f3405`）+ 发行口径（D10）+ 峰值线程读数行（`27c9f40`，未采集显「—」） |
| R-079 | **登记** | cancel 不唤醒 PixelBudget cv（1s 轮询余量） |
| R-080 | **登记** | 关窗等待框/语义告知未做 |
| R-081 | **已修** `93001a9`（余量登记） | exif_editor 构造期 read_metadata 后台化（QtConcurrent + 回填，`93001a9`）；余量=「读取中」无进度态（仅 accept 守卫）**登记** |
| R-082 | **已修** `12ce994` | classes.json version 校验 |
| R-083 | **已修** `12ce994` | 模板末段文件名校验（11 例真值表） |
| R-084 | **已修** `12ce994` | 标签编辑被拒升格 Warning |
| R-085 | **已修** `12ce994`（余量登记） | 生效值数据面补全（XMP/OffsetTime）；UI 呈现**登记** |
| R-086 | **已修** `33d09a8` | 真实进度 0.95 封顶 |
| R-087 | **已修** `33d09a8` | jpegli longjmp 资源 + 回调 try/catch |
| R-088 | **已修** `33d09a8` | simd.h 权重公式注释改对（实现零改动） |
| R-089 | **已修** `33d09a8` | chroma 量化统一 quantize16（B9） |
| R-090 | **已修** `33d09a8` | BMP 位深 {8,24} 口径改实 |
| R-091 | **已修** `18fb355` | VCPKG_SHA 活锚断言 |
| R-092 | **已修** `18fb355` | 发行包 NOTICE（GPL §6） |
| R-093 | **登记** | 重定向策略显式化未做（依赖 Qt 默认） |
| R-094 | **已修** `25b60b3` | secret.{h,cpp} DPAPI/编码+0600（B28/B30）；非 Windows=编码档**受约束登记**（R2F-C2） |
| R-095 | **已修** `18fb355` | mkfixtures 缺件 FAIL |
| R-096 | **已修** `18fb355` | ci-tidy 跨 TU 去重 |
| R-097 | **已修** `12ce994` | png_ok 硬断言（C4） |
| R-098 | **已修** `18fb355` | env.py --strict |
| R-099 | **已修** `a6e29f6`+`27c9f40` | page_meta 两轨统一 deg_to_dms（`a6e29f6`）+ exif_editor 第三轨统一 `pp::map::deg_to_dms`（`27c9f40`） |
| R-100 | **已修** `a6e29f6` | parse_coord 回退 dms_to_deg |
| R-101 | **已修** `a6e29f6` | 瓦片失败延迟重抓 |
| R-102 | **已修** `a6e29f6` | !current 先于 offline 判定 |
| R-103 | **已修** `a6e29f6` | world_to_latlon clamp |
| R-104 | **已修** `a6e29f6`+`5d19146` | UI 侧词表回落（`a6e29f6`）+ settings 持久层回落（`5d19146`） |
| R-105 | **已修** `a6e29f6` | dms.h 注释改对 |
| R-106 | **已修** `40f3405` | pipeline.h include 清理 |
| R-107 | **已修** `d99df1b` | ~MainWindow 显式收尾序 |
| R-108 | **已修** `a6e29f6`（UA 余量登记） | 双源收敛+标注；UA 仍 PhotoPipeline/0.1（网络头=行为）**登记** |
| R-109 | **已修** `594d93f`+`52b9870`（库化登记） | run_rows_model/run_status_text 可测化；库化（pp_ui_logic）**登记** |
| R-110 | **已修** `33d09a8` | 注册宏失败分支不再静默 |
| R-111 | **已修** `33d09a8` | encode_threads 默认 1 |
| R-112 | **已修** `7472b97` | row_h 死常量删除 |
| R-113 | **已修** `7472b97` | mark_name 死访问器删除 |
| R-114 | **已修** `7472b97` | bottom_bg 删除 |
| R-115 | **已修** `18fb355`+WP11（D8） | CMakeLists 注释 + README 计数改实（§3.2） |
| R-116 | **已修** `594d93f` | 探针迁 smoke_probes.cpp |
| R-117 | **已修** `7472b97`+`27c9f40`（余量登记） | 卡头边距单源（card_header）；页内副本随 R-021 登记 |
| R-118 | **已修** `a6e29f6`+`f3e1903`（三处注释标注登记） | providers 注释已收口；`src/platform/mica.h:11`/`src/core/settings.h:38`/`src/core/simd/simd.h:17` 三处「留后续」**登记**（§4.2 为 M7 候选表落点） |
| R-119 | **已修** `7472b97`+`27c9f40`（余量登记） | kCardHeadPadTop/Bottom 常量化；同 R-021 余量 |
| R-120 | **已修** `18fb355` | ci-check-deps 从 versions.env 解析 |
| R-121 | **已修** `18fb355`+WP11（D9） | 默认目录统一 + README 设计取舍 |
| R-122 | **已修** `18fb355` | vcpkg.json 0.3.2 + 单源断言 |
| R-123 | **已修** `18fb355` | 显式 libjpeg-turbo/tiff 依赖 |
| R-124 | **已修** `18fb355` | ccache 仅 GNU\|Clang 挂载 |
| R-125 | **已修** `18fb355` | dev test preset |
| R-126 | **已修** `18fb355` | shots=7→12 注释 |
| R-127 | **已修** `18fb355`（C5 登记） | 检查集单源；HeaderFilterRegex 扩面**登记**（C5，须重播种） |
| R-128 | **已修** `18fb355` | CLANG_FORMAT_VERSION 入 versions.env |
| R-129 | **已修** `18fb355` | .gitignore 补齐 + 垃圾清理 |
| R-130 | **已修** `18fb355` | .gitattributes eol=lf |
| R-131 | **已修** `18fb355` | versions_env.py 统一解析 |
| R-132 | **已修** `18fb355` | .clang-format c++23 |
| R-133 | **已修** `27c9f40`+`93001a9` | 发射按「若变」收口（`27c9f40`）；孤儿信号 format_changed **删除**（`93001a9`，声明 + 3 处发射全链退役，config_changed 承载通知；冻结面删除追认 M7R21F4-1） |
| R-134 | **登记** | checks_changed/zoom_changed/preview_loaded 孤儿信号未处理 |
| R-135 | **已修** `27c9f40` | FormatState::extra 记存未知键、load→save 不丢（`27c9f40`） |
| R-136 | **已修** `40f3405`+`27c9f40` | core 单源（`40f3405`）+ GUI 两处判据退役改调（`27c9f40`，同 R-321） |
| R-137 | **已修** `27c9f40`+`93001a9` | settings_dialog 侧改调 layers 单源（`27c9f40`）；map_style 词表与序落 `core/settings.h kMapStyleIds` 单源、settings.cpp/layers.cpp/mainwindow 三处副本全转发（`93001a9`，M7R21F4-3：词表在 core、映射在 layers） |
| R-138 | **已修** `27c9f40` | 行注「下次启动生效」+ tooltip（`27c9f40`） |
| R-139 | **已修** `d99df1b` | term_words.h 终态词表（B36） |
| R-140 | **已修** `d99df1b` | 状态栏补三桶 + 单位标注 |
| R-141 | **已修** `d99df1b` | 防御收尾回写 file.state |
| R-142 | **已修** `d99df1b` | 清空该类勾选（B41） |
| R-143 | **已修** `6e001da` | chip=过滤档开关 + .on 单义（`6e001da`，R2D1-A1） |
| R-144 | **已修** `6e001da` | 失败清单落盘 `logs/failed-*.txt` + 状态栏可点下钻（`6e001da`） |
| R-145 | **已修** `93001a9` | --dev usage 醒目标注 dev/GUI 默认面分歧（conflict=overwrite vs rename、单输出模板 $dir/$file vs $format/$dir/$file）+ README 中文面同步（M7R21F4-8；修法取「--help 标注」不改默认值） |
| R-146 | **已修** `d99df1b` | 再次运行含失败先确认 |
| R-147 | **登记** | 设置生效时机标注未做 |
| R-148 | **已修** `6e001da` | unreadable_ 计数 + 卡头 hint「N 项未能读取」（`6e001da`） |
| R-149 | **已修** `6e001da` | dedup_key 弱规范化 + Windows 大小写折叠（`6e001da`） |
| R-252 | **已修** `594d93f` | 五条时序约定补 Q_ASSERT 锚点 |
| R-253 | **已修** `594d93f` | refresh_status/theme 分段 |
| R-254 | **已修** `6250b3b`（余量登记） | remove_sel 判据/选中映射单点 + natural_bytes/human_bytes 收敛 `byte_text.h` 单源（`6250b3b`，输出逐字节不变）；余量=T()/tr() 混用、Impl 120 成员分舱**登记** |
| R-255 | **已修** `0ca8c62`（余量登记） | smoke_probe_meta 1675 行/深 10 → 12 个分节方法（`0ca8c62`，节体零文本改动）；余量=sec_i 几何分节 ~650 行（值表驱动）**登记**不阻塞 |
| R-256 | **已修** `5cf7ddd`（余量登记） | apply_map_style 三职拆名（bind_map_layer_writeback/on_map_layer_changed/apply_map_style，`5cf7ddd`）；余量=ev.index→行号「行序不变」脆弱契约**登记**（后半） |
| R-257 | **已修** `594d93f`（错误三态登记） | mainwindow.h:45 注释修正（B57）；错误三态**登记** |
| R-258 | **已修** `594d93f`+`6bd2c15` | 探针与产品分 TU（`594d93f`）+ ProbeContext 访问器层收口 objectName 散写（`6bd2c15`，后半） |
| R-259 | **已修** `69c08e1` | 关闭路径带反馈同步 join（状态行 + 等待光标；阻塞期从析构挪到关闭路径，`69c08e1`，M7R21F3-10 明文授权行为变化） |
| R-260 | **已修** `93001a9`（复核） | classify_panel.h 头注释「职责两说」改实（ClassRegistry 实体在 core/classify）；page_meta.h 钩子清单寄生随 F2 meta_probe_context 收口（`93001a9` 复核） |
| R-261 | **已修** `a6e29f6` | MapDialog::release 复原双边化 |
| R-262 | **已修** `a6e29f6` | 「限扫 200」注释改对（≤8） |
| R-263 | **已修** `d99df1b` | 重命名预填显示名 |
| R-264 | **已修** `594d93f`+`6250b3b`+`93001a9` | fmt 族收 run_status_text.h（`594d93f`）+ natural_bytes/human_bytes 收敛 byte_text 单源（`6250b3b`）+ file_bytes/peak_mem_bytes 并入三套合一（`93001a9`，精度/档位逐字节不变） |
| R-265 | **已修** `27c9f40` | 三连删除（`27c9f40`：二次 set_values / QSignalBlocker / 第二样本） |
| R-266 | **已修** `27c9f40` | 仅元数据回退槽语义收口 + is_selected 判现役（`27c9f40`） |
| R-267 | **已修** `7472b97` | nav_num 引用 badge 单源 + ChipButton 注释纠偏 |
| R-268 | **已修** `f3e1903` | icons/paramform 两处注释改实；余见 R-269 |
| R-269 | **已修** `27c9f40`+`93001a9` | 词根启发式删除、子分组改读 ParamDef.group（`27c9f40`）；note 占位魔法空格收口 `formkit::kNotePlaceholder`（`93001a9`，3 处） |
| R-270 | **已修** `7472b97`+`27c9f40` | Metrics 补 menu_*/tab_* 令牌化（`27c9f40`） |
| R-271 | **已修** `25b60b3` | icons 解析器单测（+ PP_ICONS_TEST_NO_WIDGETS） |
| R-272 | **已修** `40f3405`+`f3e1903` | 死 include 随拆分消除 + bench 魔法数命名 + kMaxExactInt |
| R-273 | **已修** `5d19146`+`27c9f40` | 错误文案通道随 R-070 同批收口：`core/error_text.h` 中文单源（`5d19146`）+ UI 失败原因行/模板预检接线（原文进 tooltip，`27c9f40`，run-reason 探针钉） |
| R-274 | **已修** `40f3405` | --bench-cases 清单精确匹配 |
| R-275 | **已修** `52b9870` | 重复段删除、checked 42→39 虚计数修正（`52b9870`） |
| R-276 | **已修** `6e001da` | visible_row_rect 单源两路统一（`6e001da`） |
| R-277 | **已修** `6e001da` | failed_path/failed_error + 「无法预览：…」（`6e001da`） |
| R-278 | **已修** `6e001da` | 4 死 API 移除（queue_empty 保留）+ printf 收 logger（`6e001da`） |
| R-279 | **已修** `6e001da`+`93001a9` | thumbnails.h `<memory>` 已补（`6e001da`）；Caption 双实现合并——CaptionButton 迁 `ui/caption_button.h` 唯一实现、settings_dialog 手拷删除（`93001a9`，图标 12px 漂移归位 10px） |
| R-280 | **已修** `6e001da` | kGroupRoleBase/kChipDotPx/kRebuildDebounceMs 命名（`6e001da`）；「焦点环 +2」= kFocusRingPad 本已命名 |
| R-281 | **已修** `52b9870`（alpha 取值取证保留） | 去 processEvents + 正则换锚 + alpha 取值契约成文（`52b9870`）；两端取值=行为取证保留 |
| R-282 | **误识别关闭**（R2F-X1；effective_datetime 收敛已修 `12ce994`） | 死码前提不成立（R2F-X1）：BMP=文档化 API+测试钉住、with_extension=冻结+4 例（C1/C2 保留）；effective_datetime 收敛已修（`12ce994`） |
| R-283 | **已修** `40f3405`+`f3e1903` | scheduler/dev_cli 注释 + fsops.h/settings.h 注释改实 |
| R-284 | **已修** `40f3405`+`594d93f` | 纯函数单源 + run_rows_model 同源权重 |
| R-285 | **已修** `12ce994` | metadata-only 能力探测进程缓存 |
| R-286 | **已修** `40f3405`（反向 include 登记） | include 清理+导航注释；metadata.cpp 反向 include **登记** |
| R-287 | **已修** `40f3405` | ProgressMux OutState 状态机 |
| R-288 | **已修** `40f3405` | 高水位单层 |
| R-289 | **已修** `12ce994` | flatten_ms 累加口径统一 |
| R-290 | **已修** `12ce994`（LRU/trim 登记） | write_atomic×2 收敛；LRU×3/trim×2 **登记** |
| R-291 | **登记** | settings 键表半表驱动（写侧表 vs 读侧 18 分支） |
| R-292 | **登记**（D11 对账注记；二分口径收敛 6/登记 7，折算见附录 C） | 13 组明细见附录 C：收敛 5 / 大部收敛 1 / 部分收敛 1 / 登记 6（二分口径收敛 6 / 登记 7，折算见附录 C）；R2 轮另消 R-190/191 的 B.3 副本组（`52b9870`），13 组镜像本体未动 |
| R-293 | **已修** `33d09a8` | format_tables 冻结头改枚举式口径 |
| R-294 | **已修** `33d09a8` | 边界值补出处（UI 范围标注） |
| R-295 | **已修** `33d09a8`+`f3e1903` | 裸键清零 + paramform 注释 |
| R-296 | **已修** `c540630` | kCicpTable 表驱动单源（`c540630`，E9 闭环） |
| R-297 | **已修** `33d09a8` | interleave 2→2 直通 + ref 越界修复 |
| R-298 | **已修** `33d09a8` | enc_heif Float 死分支显式拒绝 |
| R-299 | **已修** `33d09a8` | tie-break 显式化 + resolved_backend |
| R-300 | **已修** `33d09a8` | encoders.cpp 过期注释清理 |
| R-301 | **已修** `33d09a8` | trampoline atomic<bool> |
| R-302 | **已修** `33d09a8`+`c540630` | 构建级 `/fp:precise` + `-ffp-contract=off` 落 kernels/resample 两 TU（`c540630`） |
| R-303 | **已修** `33d09a8` | set_force_scalar 脚手架 + 分测 |
| R-304 | **已修** `33d09a8` | seek_subimage 返回值检查 + info 消费面显式化 |
| R-305 | **已修** `33d09a8` | 内省枚举自增 + 硬上限；probe_cache 失效策略成文 |
| R-306 | **已修** `a6e29f6` | mapwidget 三处注释失实改对 |
| R-307 | **已修** `a6e29f6` | kChinaBBox 单源 |
| R-308 | **已修** `25b60b3` | rect_of 并入 to_physical + 钩子寄生面显式区 |
| R-309 | **已修** `25b60b3`+`5d19146` | path_under 空目录→空路径（`25b60b3`）；settings.h `default_class_file` 同款病灶已收口（`5d19146`） |
| R-310 | **已修** `25b60b3` | QPointer |
| R-311 | **已修** `a6e29f6` | HUD 双轨注释 + kHudPointSize |
| R-312 | **已修** `25b60b3` | 三钮命中优先（同 UX-7②） |
| R-313 | **已修** `a6e29f6` | 点选联想停防抖 |
| R-314 | **已修** `6e001da` | QAbstractItemView 焦点下 ←/→ 让给视图（`6e001da`） |
| R-315 | **已修** `a6e29f6`（B49 注记） | tiles_pending_changed 接加载 chip；search_finished 不接（B49 裁定） |
| R-316 | **已修** `33d09a8` | param-catalog 12 处漂移对齐 |
| R-317 | **登记** | 预估卡两项恒「—」——维持不编数（M5-A24 口径，非缺陷） |
| R-318 | **已修** `33d09a8`+`27c9f40` | 三面全量推广（`27c9f40`：paramform 锁定行/限幅项 + page_output 置灰区） |
| R-319 | **已修** `93001a9` | --base GUI 入口端到端（全局卡「基准目录」行 pp-base-edit + PageOutput mirror_base 面 + FileListModel::set_mirror_base 收集/重算两触点，derive_base_dir 单点；opt-in 空=现行为、会话级不持久化，M7R21F4-2） |
| R-320 | **已修** `7472b97` | 第 7 份 ElidedLabel（并入 R-020） |
| R-321 | **已修** `27c9f40` | GUI 两处判据退役改调 core 单源（`27c9f40`） |
| R-322 | **已修** `40f3405` | pp::template_has_format 判据单源 |
| R-323 | **已修** `40f3405` | 拆分边界证据落地（B.6 四 TU） |
| R-324 | **已修** `40f3405` | run_one_file 十阶段（B.4） |
| R-325 | **受约束口径闭环**（R2F-C4） | 校验器/单点收口 + 13 项契约核对（`5d19146`）；强类型=冻结面约束，受约束登记（R2F-C4/C8） |
| R-326 | **已修** `52b9870` | FileListModel 不变量 1–8 单点成文 + 状态矩阵收敛（`52b9870`） |
| R-327 | **登记** | 扩展性短板（成本表在 adjudications/报告 §1） |
| R-328 | **已修** `40f3405` | 第 6 编码器成本表交付（handoff §⑥） |
| R-329 | **登记**（评级未做；对账随 R-292） | 对账随 R-292（附录 C）；数据驱动评级本体未做 |
| R-330 | **登记** | 两页骨架不对称（B.2/B.3 蓝图） |
| R-331 | **误识别关闭**（结论条目随 R-152 闭合结案） | 结论性条目：GUI 异常面无用户可触发崩溃；唯一实缺陷 R-152 已修（随其闭合结案） |

---

# 附录 B：UX-14 项台账

| UX | 去向 | 依据 / 落点 |
|---|---|---|
| UX-1 右键菜单 | **已修** `d99df1b`+`6e001da`+`27c9f40`+`52b9870` | 12 卡全接线（①-⑤ `6e001da`、⑥-⑨ `27c9f40`、**⑩-⑫ 运行页三卡 = `d99df1b`**）+ context-menu 探针 36 断言（`52b9870`） |
| UX-2 全选/取消 | **已修** `d99df1b`+`6e001da`+`52b9870` | 工具行 pp-check-menu + Ctrl+A 系快捷键 + check-all 图标（`6e001da`）+ check-all 探针（`52b9870`） |
| UX-3 预览分辨率 | **已修** `7472b97` | 门槛拆通道 1600/64 + 缓存 4096/8 + test_thumbs 两通道断言（B19/B20） |
| UX-4 GPS 地图 App 化 | **已修** `a6e29f6`+`6e001da` | 浮层/下拉/图层/tooltip/260 已落（`a6e29f6`）+ stretch 吃满（`6e001da`，min 260 + Expanding；探针 ≥260 换锚 M7-R2-B） |
| UX-5 启用开关 | **已修** `a6e29f6` | 三卡 SwitchControl + build_rules 跳过 + gps_enable 双向同步（B47/B48） |
| UX-6 样式收口 | **已修** `7472b97`+`f3e1903`+`27c9f40`（classify 间距登记） | 几何常量/骨架单源/QSS 收口/ppFk（`7472b97`+`f3e1903`）+ exif_editor 行高 28（`27c9f40`）；classify 8/4→10/12 **登记** |
| UX-7 真无边框 | **已修** `25b60b3` | WM_NCCALCSIZE + 三钮命中优先 + 四弹窗同口径 + 字形重画（B24/B25/B32）；**真机 10 项待签收**（§2.4） |
| UX-8 高级参数 | **已修** `7472b97`+`d99df1b`+`27c9f40`+`52b9870`（余量登记） | 224px 退役 + group 数据面（`7472b97`+`d99df1b`）+ UI 重做（`27c9f40`：二级折叠/整行折叠头/行宽 160）+ folds 探针（`52b9870`）；余量=组折叠头「点击整行可点」**登记**（F4 handoff ⑤-8） |
| UX-9 格式卡 | **已修** `d99df1b`+`e8b9bf1` | 页签去位深 + 等宽（T15 改案）+ 磁贴去位深文本；位深控制功能逐字保留（B39/B43） |
| UX-10 编码档位 | **已修** `33d09a8`+`d99df1b`+`27c9f40`+`c540630` | 10.1 核心行+联动+落盘反查（`33d09a8`+`d99df1b`）+ 色度限幅联动（`27c9f40`）+ 10.2 逐插件白名单（`c540630`） |
| UX-11 滚动条 | **已修** `7472b97`+`27c9f40`+`93001a9` | 6px 细浮层+弹窗同皮（`7472b97`）+ lock_card_size 接线 page_output 四卡/page_run 三卡（`27c9f40`）+ page_meta 侧 info/time/switch 三卡（`93001a9`；GPS 卡不锁 = UX-4 stretch 富余高载体，M7R21F4-5 取舍注记） |
| UX-12 亮色页签 | **已修** `7472b97`+`27c9f40`+`d737cb4` | tokens 初值+replay 补 tabs（`7472b97`）+ UnderlineTabs/FocusRing 重放链（`27c9f40`）+ settings-tabs-light 像素档探针 4 断言（`d737cb4`，第 4 波） |
| UX-13 圆角 | **已修** `25b60b3` | DWMWA_WINDOW_CORNER_PREFERENCE（Win10 静默忽略）；同 UX-7 真机验收 |
| UX-14 色彩质量 | **已修** `33d09a8`+`12ce994`+`c540630`（E1/E2 口径闭环，详见 R2 轮交付） | UX-14 §14 分级子项（全称引注）：**UX-14 A1** nclx 与 ICC 矛盾 `33d09a8`+`c540630`（闭环 E1）/ **UX-14 A2** 灰度-RGB ICC 整文件失败 `12ce994` / **UX-14 A3** PNG compressionLevel 键名 `33d09a8`（=R-010）/ **UX-14 A4** TIFF predictor `33d09a8`（=R-011，评审诊断被证伪）/ **UX-14 A5** CICP 日志口径 `12ce994` / **UX-14 B1** ΔE 护栏 `12ce994`+`c540630`（扩强）/ **UX-14 B2** 跨库对拍 `c540630` / **UX-14 B3** ICC 回读扩面+BMP 警示 `33d09a8`+`c540630`（BMP 受约束 R2F-C1）/ **UX-14 C1–C4** 色域映射/8bit 抖动/渲染意图/线性光 `c540630`（引擎面，默认关）+ GUI 四控件接线 `6ff43f9`（opt-in 默认关 = 现行为；C2 仅 to_u8 四家，裁定落点 R2A-A11）/ **UX-14 D1–D3** CMYK 拒收/EXIF ColorSpace/HDR 诚实口径 `c540630` |

---

# 附录 C：R-292 词表/枚举镜像副本 13 组对账（WP11，D11）

逐行标签：收敛 5（#1/2/3/4/6）+ 大部收敛 1（#5）+ 部分收敛 1（#7）+ 登记 6（#8–13）。
**二分折算口径**（汇总处「收敛 6 / 登记 7」的由来）：大部收敛并入「收敛」、部分收敛并入
「登记」；逐行标签为准，二分数仅用于一行摘要。

| # | 镜像组 | 现状 | 去向 |
|---|---|---|---|
| 1 | pick_tech ≡ select_tech | 合一为 `pp::select_tech`（presets 副本删除） | **收敛** `33d09a8`（R-208） |
| 2 | jxl 五参数 ×2 | 工厂函数单点声明 | **收敛** `33d09a8`（R-209） |
| 3 | webp method/quality ×2 | 工厂 + 一键两义注记 | **收敛** `33d09a8`（R-209） |
| 4 | declares_lossless 分叉对 | 合一（tech_declares_lossless 内核） | **收敛** `33d09a8`（R-208） |
| 5 | `__lossless` 字面量 3:3 | format_tables/enc_jxl/enc_webp 改单源读取；paramform 注释 WP11 改引单源；paramform.cpp:196/338 注释/tooltip 仍有字面量（仅文案） | **大部收敛** `33d09a8`+WP11 |
| 6 | write_atomic ×2（settings/classify/preset_io） | 收敛 fsops I1 单实现 | **收敛** `12ce994`（R-194/290） |
| 7 | param-catalog ↔ format_tables | 12 处漂移已对齐（R-316）；三方一致性无机器保障 | **部分收敛** `33d09a8`；机制缺口**登记** |
| 8 | 谓词回退值 ↔ ParamDef.def | 无一致性断言 | **登记**（逻辑一致性约束，非注释/文案/常量级） |
| 9 | kJpegliCommit ↔ vcpkg REF | 双事实需人肉同步 | **登记**（跨文件事实对） |
| 10 | LRU ×3（colormanager/thumbs/mapwidget） | 三份实现 | **登记**（结构性，B.5 蓝图可顺带） |
| 11 | trim/ascii_lower ×2 | 两份实现 | **登记**（工具函数归位属逻辑改动） |
| 12 | ui_theme_mode ×3 | settings/theme/对话框三处词表 | **登记**（收敛涉行为面） |
| 13 | ColorTarget 互逆对 | to_string/parse 手写互逆 | **登记**（强类型/表驱动属逻辑改动） |

---

# 附录 D：R-118 注释级「留后续」4 处现状

| 位置 | 内容 | 现状 |
|---|---|---|
| `src/platform/mica.h:11` | 主题切换监听 | 未标「已废/已落」——**登记**（WP5 跨界 5：v1 口径维持，未扩展监听） |
| `src/core/settings.h:38` | map_tile_source/map_geocoder 键状态不明 | 未标——**登记**（键确已不存，属历史注释） |
| `src/core/simd/simd.h:17` | AVX-512 预留 | 未标——**登记**（预留语义仍成立） |
| `src/mapwidget/providers.cpp:11-18` | 「待迁移清理」已过期 | **已收口** `a6e29f6`（W7-A5：改标「冻结遗留、仅测试可见」） |

（本表即评审要求的「M7 候选表」落点；前三处如需注释标注属注释级修改，可随下一轮顺带。）

---

*报告完。终门禁数据已由 WP12 于 2026-10-12 补入 §2.3（八项全绿）；本报告 WP11 收口、WP12 终验、
R2F/R2F2 台账终稿（R2 轮交付 §6）、R2.1 增章与台账 R2.1 终态（§7 + 331+14 四档 326/15/3/1 +
证据四件入 docs/m7-evidence/，F5 2026-10-13）。*

