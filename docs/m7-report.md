# M7 收口报告 —— 工程收口轮（深度评审 331 条 + UI 14 项整改）

日期：2026-10-12 ｜ 版本口径：`CMakeLists.txt:19` = 0.3.2（下一发行段 0.3.3 由发行工作包定稿）
范围：`docs/deep-review-0.3.2.md`（R-001～R-331）与 `docs/ux-review-14.md`（UX-1～UX-14）的批量整改、
结构重构与文档收口。**覆盖台账见附录 A/B（331+14 逐条去向，无一遗漏）**；裁定汇总见
[docs/m7-adjudications.md](docs/m7-adjudications.md)；逐条用户可见变更见 [CHANGELOG.md](CHANGELOG.md) `[未发布]`。

工作包与提交索引：

| 包 | 提交 | 面 |
|---|---|---|
| WP1 数据安全与原子写盘 | `12ce994` | core 引擎数据面（R-001/002 双 P0 等 20 条） |
| WP2 codecs 与参数正确性 | `33d09a8` | codecs/params（R-010/011 等 + UX-14 A 级） |
| WP3 工程化/CI/供应链 | `18fb355` | tools/workflows（R-014～019 等 37 条） |
| WP4 主题/样式/单源收口 | `7472b97` | theme/formkit/UI 样式（UX-3/6/8/11/12 等） |
| WP5 平台壳层 | `25b60b3` | frameless/mica/secret（UX-7/13、R-094 等） |
| WP6 主流程与交互 | `d99df1b` + `e8b9bf1` | mainwindow/page_run/page_output（R-150～153 必修等） |
| WP7 地图与元数据卡 | `a6e29f6` | mapwidget/page_meta（UX-4/5、R-012/013 等） |
| WP8 引擎结构与契约 | `40f3405` | B.4/B.6/B.7/B.8 蓝图（R-156/324 等） |
| WP9 UI 结构拆分 | `594d93f` | 探针迁出 + mainwindow 五域（R-048/049 等） |
| M7-GATE | `d882fe6`、`59de09d` | 回归基线重播种（逐行归因，见提交正文） |
| WP10 色彩行为 | **未交付** | 见 §3.1（ColorManager 消费未入树） |
| WP11 长尾清扫与文档（本包） | 本提交 | docs/**、CHANGELOG、README + P3 注释/文案/常量小修 |

---

## 1. 交付内容

### 1.1 整改面（按评审优先级）

1. **P0 数据破坏双条根治**（R-001/002）：输出路径命中源文件即整文件拒绝；元数据写回原子化
   （tmp→rename）且失败=失败。另接 R-003/004/005/007/023/028/029/030/031 全部数据面 P1/P2。
2. **R3 必修四条**（R-150 版本徽标 / R-151 翻图打标错位 / R-152 过滤态双击写错文件 /
   R-153 分组重建吞多选）全部修复，同批收 R-006/008/069/076/139/140/141 等运行语义面。
3. **参数假接线**（R-010 PNG 键名、R-011 TIFF predictor【复核推翻评审结论：上游真实消费，
   键名对齐即可生效；float 档显式拒绝】、R-244/245/246 JPEG 三假控件锁定+禁用原因）。
4. **色彩正确性 A/B 级**（UX-14）：nclx 跟随 ICC（A1）、灰度/RGB ICC 降级不整文件失败（A2）、
   ΔE2000 护栏（B1）、ICC 嵌入回读扩面（B3）。
5. **地图链**（R-012 联想清算、R-013 key 脱敏、R-077 空 Key 预检、R-158/224 表驱动、
   R-219～223 契约收敛、R-161 等）+ UX-4 地图 App 化、UX-5 启用开关。
6. **UI 14 项**：已修/大部完成 11 项（UX-3/4/5/6 大部/7/8 前半/9/10/11 大部/12 主修/13），
   残余逐条登记（附录 B）。
7. **工程/CI/供应链**：actions 钉 SHA、回归归因护栏、UBSan 假绿封堵、语料 sha256 锁、
   tidy 棘轮收紧、工具链钉版、发行包 NOTICE 等 37 条（WP3 全清）。
8. **结构重构**（B.4/B.6/B.7/B.8 + R-048/049）：run_one_file 十阶段、enc_common 五连拷贝收编、
   main.cpp 拆四 TU、探针迁出发布二进制（PP_BUILD_DEV 门控）、mainwindow 五域拆 TU、
   OutputState/ErrorKind 契约枚举。金样 20/20 + 回归 zero diff 背书行为零变化。

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
| R-292 | 本报告附录 C | 13 组镜像对账：收敛 6 / 登记 7（不改逻辑） |

### 1.3 文档收口（WP11）

- `docs/m7-adjudications.md`：九个 handoff 的「裁定登记」合并（A1–A31 加性 / B1–B61 行为 /
  C1–C10 保留 / D1–D12 本轮 / E1–E8 遗留裁决点）。
- `docs/m7-report.md`（本文件）：体例五章 + 覆盖台账附录。
- `CHANGELOG.md` `[未发布]`：新增/修复/变更/已知四段累计本轮（发行段定稿归发行工作包）。
- `README.md`：M7 段落 + 计数口径 + 打包口径 + 线程估算口径。
- 评审文档本体（`deep-review-0.3.2.md`/`ux-review-14.md`）**只读未改**。

---

## 2. 验收结论（命令 + 结果逐条取证）

> 本节由 WP11 先落结构与本包实跑数据；**终门禁（全量 ctest / 金样 / 回归 / format / tidy）
> 由 WP12 在波次收口统一补数**，届时在此追加并注明命令与时间。以下为本包（WP11）实跑。

### 2.1 本包实跑（2026-10-12）

| 命令 | 结果 |
|---|---|
| `python tools/env.py run -- cmake --preset release-dev -B build/m7-wp11 -DVCPKG_MANIFEST_INSTALL=OFF` | 配置成功 |
| `python tools/env.py run -- ctest --test-dir build/m7-wp11 -N` | **Total Tests: 35**（release-dev） |
| `python tools/env.py run -- cmake --preset release -B build/m7-wp11-rel -DVCPKG_MANIFEST_INSTALL=OFF` + `ctest --test-dir build/m7-wp11-rel -N` | **Total Tests: 34**（release）；两清单 diff 仅 `ui_smoke` 一条（dev 门控） |
| `grep "物理核" src/ui/smoke_probes.cpp tests/` | 0 命中（R-065 文案改动不触发任何探针断言） |
| `python tools/env.py run -- cmake --build build/m7-wp11` | exit 0（329/329 目标，含 photopipeline） |
| `python tools/env.py run -- ctest --test-dir build/m7-wp11 --output-on-failure` | **100% tests passed out of 35**（43.8s，含 ui_smoke Passed） |
| `python tools/env.py run -- tools/bin/clang-format-win64.exe --dry-run -Werror <本包 10 个改动文件>` | 0 diff（钉版二进制，仅本包改动文件） |

### 2.2 前十个包的验证证据（引自各 handoff「验证命令与结果」节，均为实跑）

- WP1：8 个单测全绿 + 金样 `SMOKE total=20 pass=20 fail=0`（冻结行逐字不动）。
- WP2：ctest **33/33**（当时口径）+ 金样 20/20 + 新增断言 6 组全绿。
- WP3：`pp_verify --selftest` 39 探针 OK；语料 sha256 锁负对照拒跑；versions-check 10 键 OK。
- WP4：test_thumbs/test_formkit 新断言绿；ui_smoke 冻结行不变。
- WP5：ctest **35/35** + 金样 20/20 + `UI-SMOKE OK shots=12 pages=3` + R-312 命中序探针 17 断言 OK。
- WP6：ctest **35/35**（含 ui_smoke）+ 五处换锚断言实跑通过（`e8b9bf1`）。
- WP7：地图域 5 单测绿；meta-geom checked=22 bad=0 / meta-loop 11/11 / meta-map-expand 14/14。
- WP8：ctest **35/35** + 金样 20/20（行为零变化）+ 契约单测 3 组新增断言绿。
- WP9：ctest **35/35** + ui_smoke 冻结行 + 发布树探针串 0 命中（dev 树阳性对照 HIT）。
- M7-GATE：回归 zero diff（两次重播种均按 R-015 先归因后落盘，闭合证明见提交正文）+
  金样 20/20 + `ctest --preset release` + format violations=0。

### 2.3 待 WP12 补数（结构留位）

> WP11 已在自己的树 `build/m7-wp11` 实测 ctest 35/35（§2.1）；以下为**波次收口口径**
> （release-dev 门禁树 + 金样 + 回归 + 全仓 format/tidy）的终数，由 WP12 统一补。

- [ ] 全量 `ctest --test-dir build/release-dev --output-on-failure`（WP11 自树实测 35/35，门禁树待复核）
- [ ] 金样 `python tests/golden/smoke.py build/release-dev`（预期 `SMOKE total=20 pass=20 fail=0`）
- [ ] 回归 `python tools/regression.py`（预期 zero diff；基线已含两次 M7 重播种）
- [ ] `bash tools/ci-format.sh`（预期 violations=0）+ `python tools/ci-tidy.py build/release-dev`（棘轮只减不增）
- [ ] `QT_QPA_PLATFORM=offscreen … --ui-smoke`（预期 `UI-SMOKE OK shots=12 pages=3`）

### 2.4 真机验收（offscreen 无法替代，须人工签收）

WP5 清单 10 项：拖拽移动 / 双击标题栏最大化 / 八向缩放（含三钮顶部 6px）/ Win11 Snap（含
悬停最大化钮弹 Snap Layouts）/ 三钮悬停点击 / DWM 圆角与最大化切角 / 原生 chrome 消失 /
四弹窗同口径 / 多显示器 DPI / Aero 摸边。**未签收前 UX-7/UX-13 不算终验**（登记为已知缺口）。

---

## 3. 偏差与追认

### 3.1 WP10 未交付（本报告最大偏差，如实）

- **事实**：`build/m7-wp10/` 仅有 configure 产物（2026-10-12 05:47），**无 handoff、无提交**；
  代码面取证 `src/core/colormanager.cpp:630-632` = `(void)opts;`（I3 数据面接线但消费未入树），
  全 `src/ui` grep 无色域映射/抖动/渲染意图/线性光 UI 面；M7-GATE 提交正文亦明载
  「WP10 行为实现未入树」。
- **影响**：任务书 README 清单列的「色域映射/抖动/渲染意图/线性光重采样 opt-in 项」**未实现**。
  按「诚实显示」纪律，README/CHANGELOG **不将其列为新功能**，只在「已知」披露（裁定 D12）。
  UX-14 的 C1–C4 四条对应登记为未修（附录 B）。
- **残余**：`RunConfig/ColorOptions` 四字段已接线且默认值=现行为（零影响）；行为实现 + UI 开关
  待后续工作包。

### 3.2 R-115 计数口径偏差（任务书「33 条」→ 实测 35 条）

- 任务书写「ctest 计数口径改实（33 条）」；33 是评审日（2026-10-11，ux-review 基线）口径。
  M7 期间 `test_icons`/`test_secret` 入编后为 **35（release-dev）/ 34（release）**——本包
  2026-10-12 用两个全新配置树 `ctest -N` 实测（§2.1）。README 按实测改并加「以 ctest -N
  实测为准」的非计数口径（裁定 D8）。

### 3.3 评审结论被证据推翻的两处（先例：复核优于照抄）

- **R-011**：评审称 `tiff:predictor` 无人消费；WP2 复核 OIIO 3.1.17 `tiffoutput.cpp:1026-1029`
  证明 `tiff:Predictor`（大小写不敏感）真实消费、且在自动规则之后跑 ⇒ 用户值生效。改法从
  「标注不可控」改为「键名对齐 + float 档显式拒绝」（W2-A12）。
- **R-212**：`paint_spin_arrows` 被判死码，实为 Spin/DSpin::paintEvent 消费（W4-T5 判定修正）。
- 另 `go_text`（R-214 家族）非死值（探针消费），保留。

### 3.4 断言换锚汇总（全部同严格度或收紧，无一放宽）

R-171/R-002（write-warnings、meta-write-fail）、R-097（png_ok 硬断言）、UX-3（test_thumbs
两通道分叉 4 条）、UX-9（tab-label/bitdepth-lock 合取项）、T13（清空该类勾选）、W7-A12（地图
136→260 锚）、W7-A4/A6（geocoder/layers 语义换锚）、B28（settings 密文断言收紧）、B60
（fmt_duration 期望锚定既有行为）。明细见 adjudications 二节。

### 3.5 回归基线重播种（两次，均先归因）

- `d882fe6`：46 行 heif 档位 info 行（WP2 新增日志面，行为 0 于产物字节）。
- `59de09d`：184 行 = 机械标签迁移 180（main.cpp→dev_cli.cpp，行为 0）+ 真实行为 4
  （R-162 错误消双记）。闭合证明：transformed(baseline) == current 逐字节相等。

### 3.6 其它追认

- WP5-A10/A11（CMake 两行登记、pragma lib）：工程不可达的字面偏差，理由在案（adjudications B33/B34）。
- WP6-T15（页签等宽改案）、WP9-W9-8（死码接线）等实现改案已在 adjudications 二节逐条登记。
- WP11 只动注释/文案/常量命名级代码 + 独占文档面；`logic` 一律登记（红线 3）。

---

## 4. 未覆盖 / 未决项（如实）

### 4.1 跨界未决（等主对话裁决，adjudications 五节 E1–E8 同步）

1. **R-009 收尾一行**：`theme_replay_panels()`（mainwindow）未重放 `page_run->set_tokens`
   ——构造期已修（WP4），**主题切换时运行页令牌不刷新**仍在。P1 残余，一行接线属逻辑改动，
   红线 3 下本包登记不改（grep 取证：`src/ui/mainwindow.cpp` 的 theme_replay_panels 仅
   preview/classify/page_meta/page_output 四件 set_tokens）。
2. **R-231**：`validate_preset` 弱门禁（未知键/跨字段规则不查）——presets.cpp 跨面，WP1 转出后
   无包认领。
3. **R-070/273 UI 中文映射**：ErrorKind 单源已备（WP8），UI 侧按枚举对表的中文文案未接。
4. **R-076 引擎侧**：`scheduler.cpp` DoneWithErrors→Done 映射未改（UI 已按逐输出事实补判）。
5. **R-099 第三轨 / R-104 持久层词表回落 / R-296 CICP 表驱动 / R-211 强类型位深 /
   R-163 取消归类 / R-007 `\\?\` 归一**：见 adjudications E1–E5。
6. **R-226 潜伏越界**：`tile_index()` 对未知 id 返回 `kFormatCount`，`tiles[tile_index(id)]`
   即越界（两表分裂时 UB）——防御属逻辑改动，登记不改（page_output.cpp:1114、:2028）。
7. **WP5 真机验收 10 项**（§2.4）与 **Linux 基线重播种**（E7）。

### 4.2 登记为已知差异 / 结构性债（附录 A「登记」条目，择要）

- 大单元拆分余量：page_meta（B.1）/ page_output（B.3）/ mapwidget（B.5）/ enc_heif 四职责 /
  settings ctor 563 行 / filelistmodel 6 类 8 职责（R-155 余量、R-061/200 等）。
- UI 建造器余量：page_meta/page_run/mainwindow 卡壳-卡头-卡体与页内 make_button 未换 formkit
  单源（R-021/022/053/117/119）；exif_editor 行高 28 节奏（UX-6③）。
- 性能面：R-025/026（add_paths 后台化、分组摘要哈希）、R-024 部分（合并刷新已做，增量计数已做，
  合并残留）、R-228（磁盘空间查询逐键 stat）。
- 交互面：R-068/133/134/135/138/143/144/145/147/148/149/249/265/266/276/277/278/314/317/318/319。
- 契约面：R-177～205 常注释/同构债、R-325 强类型路线（C8）、R-326 不变量成文、R-330 两页骨架。
- R-292 七组镜像（附录 C）；R-118 四处注释「留后续」（mica.h:11、settings.h:38、simd.h:17
  三处未标，providers.cpp 已随 WP7 收口）。
- R-317 预估卡两项恒「—」：维持「未发生的量不显示」（M5-A24 口径），非缺陷。

### 4.3 未跑 / 无法跑

- 终门禁五项（§2.3）归 WP12；本包只跑了 §2.1 之列。
- TSan 坏捕获正对照（R-027）本机 Windows 无法执行，验证点=首轮 Linux nightly tsan job。
- R-025/026 等性能项无自动化判据（改动本身也未做，见附录 A）。

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

# 计数口径复现（R-115）
python tools/env.py run -- ctest --test-dir build/m7-wp11 -N      # 35
python tools/env.py run -- ctest --test-dir build/m7-wp11-rel -N  # 34（diff 仅 ui_smoke）
```

证据路径：各包 handoff = `build/m7-wpN/handoff.md`（验证命令与结果逐条在册）；
门禁取证 = `build/m7-gate/*.log`（回归归因/金样/ctest/format）；
回归归因 = `.cache/regression/attribution.log`（R-015 护栏产出）。

---

# 附录 A：覆盖台账 —— deep-review R-001～R-331（逐条去向）

去向口径：**已修**（含提交）/ **部分**（主修已落、余量登记）/ **登记**（已知差异，理由在案）/
**跨界**（等主对话裁决，问题在案）。评审本体只读未改；本表为唯一对账面。

## A.1 P0（2 条）

| R-id | 去向 | 依据 / 落点 |
|---|---|---|
| R-001 | **已修** `12ce994` | `same_output_as_source()` 拦截（pipeline + metaonly）；test_pipeline_contract r001/* |
| R-002 | **已修** `12ce994` | exiv2 原子写回 + 写失败=失败（B1）；test_metadata r002/* |

## A.2 P1（R-003～R-023、R-150～R-161，32 条）

| R-id | 去向 | 依据 / 落点 |
|---|---|---|
| R-003 | **已修** `12ce994`+`33d09a8` | fsops 原子写四件套 + 五编码器接线 |
| R-004 | **已修** `12ce994` | srcmeta.error 非空=文件失败 |
| R-005 | **已修** `12ce994` | reserved 仅记成功输出 + 软/硬保留（B3） |
| R-006 | **已修** `d99df1b` | on_run_again 补齐语义（B35） |
| R-007 | **部分** `12ce994` | 预检+manifest 已落；`\\?\` 归一**跨界**（E5） |
| R-008 | **已修** `d99df1b` | Progress 不落列表状态 |
| R-009 | **部分** `7472b97` | 构造期已修；refresh_theme 不重放 page_run **跨界未决**（§4.1-1） |
| R-010 | **已修** `33d09a8` | png:compressionLevel 键名 + 体积差断言 |
| R-011 | **已修** `33d09a8` | 复核反转评审结论（§3.3）；键名对齐 + float 显式拒绝 |
| R-012 | **已修** `a6e29f6` | 联想世代守卫 + abort |
| R-013 | **已修** `a6e29f6` | redact_secrets 单点脱敏 |
| R-014 | **已修** `18fb355` | actions 全钉 commit SHA |
| R-015 | **已修** `18fb355` | --update 归因护栏（M7-GATE 两次实证） |
| R-016 | **已修** `18fb355` | nightly regression-windows job |
| R-017 | **已修** `18fb355` | halt_on_error=1 + Temporary 扫描 |
| R-018 | **已修** `18fb355` | corpus.sha256 入库锁语料 |
| R-019 | **已修** `18fb355` | --require-baseline |
| R-020 | **已修** `7472b97` | ElidedLabel 7 份收敛 formkit（含 R-320） |
| R-021 | **部分** `7472b97` | card_header/make_card 单源 + page_output 已换；余三 TU **登记** |
| R-022 | **部分** `7472b97` | formkit::make_button + settings_dialog 已换；page_meta/page_output **登记** |
| R-023 | **已修** `12ce994` | classify::load 收紧（缺 classes 键=失败） |
| R-150 | **已修** `d99df1b` | PP_VERSION_STRING 徽标 |
| R-151 | **已修** `d99df1b` | PreviewPanel::navigated 同步选中 |
| R-152 | **已修** `d99df1b` | 双击 mapToSource 补全 |
| R-153 | **已修** `d99df1b` | 多选整组快照/恢复 |
| R-154 | **已修** `d99df1b` | 运行中拒收 drop |
| R-155 | **部分** `594d93f`/`40f3405`/`a6e29f6` | 9 处巨型拆 4 处（mainwindow/run_one_file/build_gps_card/main.cpp）；余 5 处蓝图**登记**（B.1/B.2/B.3/B.5） |
| R-156 | **已修** `40f3405` | enc_common 收编 500 行拷贝 |
| R-157 | **已修** `40f3405`+`594d93f` | log_level_from_text 单源 + UI 收口 |
| R-158 | **已修** `a6e29f6` | 三塔收敛（W7-A3/A5） |
| R-159 | **已修** `25b60b3` | icons 相对 s 反射修复 + 单测 |
| R-160 | **已修** `40f3405` | seq_++ 入锁 |
| R-161 | **已修** `a6e29f6` | insert_tile 返回 bool、成功才复位 |

## A.3 P2（R-024～R-077、R-162～R-251，128 条）

| R-id | 去向 | 依据 / 落点 |
|---|---|---|
| R-024 | **已修** `d99df1b` | 增量计数 + 150ms 合并刷新 |
| R-025 | **登记** | add_paths 后台化未做（大目录冻结；性能优化无判据面） |
| R-026 | **登记** | 分组摘要哈希回填未做（同上） |
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
| R-044 | **部分** `18fb355` | 口径如实化；C++ 探针像素容差**跨界**（C6） |
| R-045 | **已修** `a6e29f6` | clear_search_state 两路共用 |
| R-046 | **已修** `a6e29f6` | 精度存疑文案对齐几何 |
| R-047 | **已修** `a6e29f6` | 仅坐标变化才 center_on |
| R-048 | **已修** `594d93f` | 探针迁出 + PP_BUILD_DEV 门控（发布树 0 命中取证） |
| R-049 | **部分** `594d93f` | mainwindow 五域拆 TU；Impl 分舱**登记** |
| R-050 | **已修** `12ce994` | class_file 惰性默认（B4） |
| R-051 | **已修** `33d09a8` | anchor 轨删除 + 注册表期望键断言 |
| R-052 | **登记** | CMakeLists 源清单双维护未收口（WP5-A10 反按两行登记，B33） |
| R-053 | **部分** `7472b97` | 同 R-021 |
| R-054 | **已修** `7472b97` | 三轨钩子收敛 ppFk |
| R-055 | **已修** `d99df1b` | warn_cannot_start/warn_preset_op 抽助手 |
| R-056 | **已修** `7472b97` | card_body 死函数删除 |
| R-057 | **已修** `33d09a8` | pack_rgba8_to_rgb8 死 kernel 删除 |
| R-058 | **已修** `7472b97` | GUI-R2 头注释按实现改写 |
| R-059 | **已修** `7472b97` | #dd8800 → warn 令牌 |
| R-060 | **已修** `a6e29f6` | providers 保留+标注「冻结遗留仅测试可见」（W7-A5） |
| R-061 | **部分** `a6e29f6`+`40f3405` | build_gps_card / run_one_file 已拆；settings ctor 563 行**登记** |
| R-062 | **已修** `18fb355` | nightly/warm-cache 单源断言补齐 |
| R-063 | **已修** `18fb355` | Qt 版本字面量清零 |
| R-064 | **已修** `7472b97` | theme::replay_tokens 单点重放 |
| R-065 | **部分** `40f3405`+WP11 | 引擎注释 + settings 文案/tooltip 改实（本包）；「thread_budget 耦合提示」已入 tooltip |
| R-066 | **登记** | preset avif 后端往返丢失（svt→libaom 静默改写）未修 |
| R-067 | **已修** `d99df1b` | 运行前提示被过滤隐藏文件 |
| R-068 | **登记** | 热键打标作用域与勾选集承诺不一致未修 |
| R-069 | **已修** `d99df1b` | 0 字节琥珀呈现 + StateToneRole |
| R-070 | **部分** `40f3405` | ErrorKind 单源已备；UI 中文映射**跨界**（§4.1-3） |
| R-071 | **已修** `d99df1b` | 运行中拒收拖放（含 R-154） |
| R-072 | **登记** | settings 损坏/只读静默提示未修 |
| R-073 | **已修** `d99df1b` | 输出根预检 + 失败档文案（B42） |
| R-074 | **已修** `d99df1b` | metaonly GUI 默认 Skip（B35） |
| R-075 | **已修** `d99df1b` | GPS 规则预检确认 |
| R-076 | **部分** `d99df1b` | UI 按逐输出补判；引擎侧映射**跨界**（§4.1-4） |
| R-077 | **已修** `a6e29f6` | 空 Key 预检引导文案 |
| R-162 | **已修** `40f3405` | 错误收尾口径统一进 IEncoder 契约 |
| R-163 | **部分** `33d09a8` | 五编码器消费 cancelled；pipeline 归类**跨界**（E3） |
| R-164 | **部分** `40f3405` | warn_unknown_params 单实现；三源键表未合一**登记** |
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
| R-177 | **登记** | MetaState 上帝结构（B.1 未做） |
| R-178 | **已修** `a6e29f6` | commit_rule_change 四连刷收口 |
| R-179 | **已修** `7472b97` | apply_style QSS 收口进 theme（field_qss 等） |
| R-180 | **登记** | page_meta 隐含约定（loading 协议等）未类型化 |
| R-181 | **登记** | UI 内嵌 IO / exif_text 不可单测（B2 未做） |
| R-182 | **登记** | 测试钩子入侵公开 API（objectName 三方契约） |
| R-183 | **登记** | classify_panel 重建协议脆弱 |
| R-184 | **登记** | make_chip 复刻 ChipButton 未合并 |
| R-185 | **登记** | pimpl 两套解法未统一 |
| R-186 | **部分** `7472b97` | button padding 单源；卡构造双份随 R-021 登记 |
| R-187 | **部分** `594d93f` | run_rows_model 抽数据语义；OutRow/FileRow 双 struct **登记** |
| R-188 | **登记** | refresh_widgets 160 行未拆（B.2 后两行未做） |
| R-189 | **登记** | 「已选格式」四份表示（B.3 未做） |
| R-190 | **登记** | itemData 枚举只写不读 |
| R-191 | **登记** | avif 提示谓词两份 |
| R-192 | **登记** | 后端覆盖标记归属 |
| R-193 | **已修** `40f3405` | main.cpp 拆四 TU |
| R-194 | **部分** `12ce994` | 原子写收敛 fsops；UTF-8 校验/FILE* 读**登记** |
| R-195 | **登记** | ParamDef 查找双实现（first/last-wins 相反） |
| R-196 | **登记** | exif_editor 三对孪生 |
| R-197 | **登记** | 时间标签集合 4 份真相 |
| R-198 | **登记** | EXIF 类型系统触点 6 处 |
| R-199 | **登记** | 三态机 combo 魔法索引 |
| R-200 | **登记** | filelistmodel 6 类 8 职责 |
| R-201 | **登记** | SettingsDialog ctor 563 行 |
| R-202 | **登记** | 三套异步脚手架 |
| R-203 | **登记** | FileGroupProxyModel dataChanged 只转发列 0 |
| R-204 | **登记** | UI 直接 include exiv2.hpp |
| R-205 | **登记** | FileRow 状态矩阵无收口 |
| R-206 | **部分** `33d09a8` | cross_rules 单源规则表；全通道合并未做（B7） |
| R-207 | **已修** `33d09a8` | kKey* 绑键常量 |
| R-208 | **已修** `33d09a8` | select_tech/declares_lossless 合一 |
| R-209 | **已修** `33d09a8` | 镜像 ParamDef 工厂去重 + quality 一键两义注记 |
| R-210 | **部分** `12ce994` | JSON 栈标注边界 + 保留裁定（C3） |
| R-211 | **部分** `33d09a8` | bps/bpp 注释显式化；强类型**跨界**（E4） |
| R-212 | **已修** `7472b97` | formkit 死 API 删除（paint_spin_arrows 判定修正保留） |
| R-213 | **已修** `7472b97` | QSS 盒模型注释矛盾改写 |
| R-214 | **部分** `7472b97` | 死值清理（pill_radius_qss/bottom_bg/row_h/kFkCheck）；go_text 保留 |
| R-215 | **已修** `7472b97` | kFkCheck 死钩子+死 QSS 删除 |
| R-216 | **已修** `7472b97` | 双行高并存清零（真源 kRowPitch=28） |
| R-217 | **登记** | paramform 编辑器工厂未做 |
| R-218 | **登记** | RTL 全清未做（与行为零变化冲突，C10；建议独立包） |
| R-219 | **已修** `a6e29f6` | reverse_url 入参 WGS-84 单约定（B44） |
| R-220 | **已修** `a6e29f6` | 子域纯函数（B45） |
| R-221 | **已修** `a6e29f6` | paint 幂等（请求移出绘制） |
| R-222 | **已修** `a6e29f6` | 冻结叙事注释改对 |
| R-223 | **已修** `a6e29f6` | result_key 稳定键对齐（W7-A2） |
| R-224 | **已修** `a6e29f6` | layers kSources 表驱动 |
| R-225 | **已修** `d99df1b` | 同 R-141（防御收尾回写 file.state） |
| R-226 | **登记** | tile_index 越界潜伏（§4.1-6；防御=逻辑改动不改） |
| R-227 | **已修** `d99df1b` | apply_thumb 不覆盖流水线状态 |
| R-228 | **登记** | 磁盘空间逐键 stat 未节流 |
| R-229 | **已修** `d99df1b` | remove_tab 先快照（git -S 取证） |
| R-230 | **已修** `33d09a8` | heif 内省默认值取插件当前值 |
| R-231 | **跨界** | validate_preset 弱门禁（§4.1-2，无包认领） |
| R-232 | **已修** `12ce994` | 同 R-023（缺 classes 键=失败） |
| R-233 | **已修** `a6e29f6` | 时区具名表 + 动态补项 |
| R-234 | **已修** `12ce994` | OffsetTime 以墙钟实改为条件 |
| R-235 | **已修** `12ce994` | 全跳过早退发输出级进度 |
| R-236 | **已修** `40f3405` | conflict 打印生效值 |
| R-237 | **部分** `40f3405`+WP11 | dev usage + settings 文案改实；详见 R-065 |
| R-238 | **部分** `40f3405` | default_bitdepth_for 单源函数；消费面**登记** |
| R-239 | **登记** | parse_conflict 三套映射未收口 |
| R-240 | **部分** WP11 | 三处注释相反本包改实（日志尾/列宽尺规/set_plan）；warn_preset_op 接线 `594d93f`（W9-8） |
| R-241 | **已修** `d99df1b` | remove_tab 注释纠偏（git -S 取证） |
| R-242 | **已修** WP11（D5） | path() 注释改实 |
| R-243 | **已修** `40f3405` | bench-report.json 转义 |
| R-244 | **已修** `33d09a8` | arith_code 恒锁 + 禁用原因 |
| R-245 | **已修** `33d09a8` | xyb_mode 恒锁 + 禁用原因 |
| R-246 | **已修** `33d09a8` | dct_method 恒锁 0 + 禁用原因 |
| R-247 | **已修** `33d09a8` | psnr_target 联动提示 |
| R-248 | **已修** `d99df1b` | remove_rows 段合并 |
| R-249 | **登记** | 主题选后逃逸（取消不回滚）未修 |
| R-250 | **已修** `d99df1b` | 运行读数单账本 |
| R-251 | **登记** | build_ui 早期读空 tokens 未修（refresh_theme 重放掩盖） |

## A.4 P3（R-078～R-149、R-252～R-331，171 条）

| R-id | 去向 | 依据 / 落点 |
|---|---|---|
| R-078 | **部分** `40f3405`+WP11 | RunSummary 三字段 + 发行说明口径（D10）；UI 读数未接（不编数显「—」）**登记** |
| R-079 | **登记** | cancel 不唤醒 PixelBudget cv（1s 轮询余量） |
| R-080 | **登记** | 关窗等待框/语义告知未做 |
| R-081 | **登记** | GUI 线程同步 IO 两处小量（评审自述"保持观察"） |
| R-082 | **已修** `12ce994` | classes.json version 校验 |
| R-083 | **已修** `12ce994` | 模板末段文件名校验（11 例真值表） |
| R-084 | **已修** `12ce994` | 标签编辑被拒升格 Warning |
| R-085 | **部分** `12ce994` | 生效值数据面补全（XMP/OffsetTime）；UI 呈现**登记** |
| R-086 | **已修** `33d09a8` | 真实进度 0.95 封顶 |
| R-087 | **已修** `33d09a8` | jpegli longjmp 资源 + 回调 try/catch |
| R-088 | **已修** `33d09a8` | simd.h 权重公式注释改对（实现零改动） |
| R-089 | **已修** `33d09a8` | chroma 量化统一 quantize16（B9） |
| R-090 | **已修** `33d09a8` | BMP 位深 {8,24} 口径改实 |
| R-091 | **已修** `18fb355` | VCPKG_SHA 活锚断言 |
| R-092 | **已修** `18fb355` | 发行包 NOTICE（GPL §6） |
| R-093 | **登记** | 重定向策略显式化未做（依赖 Qt 默认） |
| R-094 | **已修** `25b60b3` | secret.{h,cpp} DPAPI/编码+0600（B28/B30） |
| R-095 | **已修** `18fb355` | mkfixtures 缺件 FAIL |
| R-096 | **已修** `18fb355` | ci-tidy 跨 TU 去重 |
| R-097 | **已修** `12ce994` | png_ok 硬断言（C4） |
| R-098 | **已修** `18fb355` | env.py --strict |
| R-099 | **部分** `a6e29f6` | page_meta 两轨统一 deg_to_dms；exif_editor 第三轨**登记** |
| R-100 | **已修** `a6e29f6` | parse_coord 回退 dms_to_deg |
| R-101 | **已修** `a6e29f6` | 瓦片失败延迟重抓 |
| R-102 | **已修** `a6e29f6` | !current 先于 offline 判定 |
| R-103 | **已修** `a6e29f6` | world_to_latlon clamp |
| R-104 | **部分** `a6e29f6` | UI 侧词表回落；settings 持久层**登记** |
| R-105 | **已修** `a6e29f6` | dms.h 注释改对 |
| R-106 | **已修** `40f3405` | pipeline.h include 清理 |
| R-107 | **已修** `d99df1b` | ~MainWindow 显式收尾序 |
| R-108 | **部分** `a6e29f6` | 双源收敛+标注；UA 仍 PhotoPipeline/0.1（网络头=行为）**登记** |
| R-109 | **部分** `594d93f` | run_rows_model/run_status_text 可测化；库化（pp_ui_logic）**登记** |
| R-110 | **已修** `33d09a8` | 注册宏失败分支不再静默 |
| R-111 | **已修** `33d09a8` | encode_threads 默认 1 |
| R-112 | **已修** `7472b97` | row_h 死常量删除 |
| R-113 | **已修** `7472b97` | mark_name 死访问器删除 |
| R-114 | **已修** `7472b97` | bottom_bg 删除 |
| R-115 | **已修** `18fb355`+WP11（D8） | CMakeLists 注释 + README 计数改实（§3.2） |
| R-116 | **已修** `594d93f` | 探针迁 smoke_probes.cpp |
| R-117 | **部分** `7472b97` | 卡头边距单源（card_header）；页内副本随 R-021 登记 |
| R-118 | **部分** `a6e29f6`+WP11 | providers 注释已收口；mica.h:11/settings.h:38/simd.h:17 三处「留后续」**登记**（本表 §4.2 为 M7 候选表落点） |
| R-119 | **部分** `7472b97` | kCardHeadPadTop/Bottom 常量化；同 R-021 余量 |
| R-120 | **已修** `18fb355` | ci-check-deps 从 versions.env 解析 |
| R-121 | **已修** `18fb355`+WP11（D9） | 默认目录统一 + README 设计取舍 |
| R-122 | **已修** `18fb355` | vcpkg.json 0.3.2 + 单源断言 |
| R-123 | **已修** `18fb355` | 显式 libjpeg-turbo/tiff 依赖 |
| R-124 | **已修** `18fb355` | ccache 仅 GNU\|Clang 挂载 |
| R-125 | **已修** `18fb355` | dev test preset |
| R-126 | **已修** `18fb355` | shots=7→12 注释 |
| R-127 | **部分** `18fb355` | 检查集单源；HeaderFilterRegex 扩面**登记**（C5，须重播种） |
| R-128 | **已修** `18fb355` | CLANG_FORMAT_VERSION 入 versions.env |
| R-129 | **已修** `18fb355` | .gitignore 补齐 + 垃圾清理 |
| R-130 | **已修** `18fb355` | .gitattributes eol=lf |
| R-131 | **已修** `18fb355` | versions_env.py 统一解析 |
| R-132 | **已修** `18fb355` | .clang-format c++23 |
| R-133 | **登记** | format_changed 孤儿信号未接/删 |
| R-134 | **登记** | checks_changed/zoom_changed/preview_loaded 孤儿信号未处理 |
| R-135 | **登记** | 预设未知键往返丢失未修 |
| R-136 | **部分** `40f3405` | template_has_format 单源 + dev_cli 回填；GUI 判据副本**登记**（同 R-321） |
| R-137 | **登记** | map_style 词表四处并存未单源 |
| R-138 | **登记** | 「分文件夹默认结构」生效时机未标注 |
| R-139 | **已修** `d99df1b` | term_words.h 终态词表（B36） |
| R-140 | **已修** `d99df1b` | 状态栏补三桶 + 单位标注 |
| R-141 | **已修** `d99df1b` | 防御收尾回写 file.state |
| R-142 | **已修** `d99df1b` | 清空该类勾选（B41） |
| R-143 | **登记** | classify chip 二义/零点击死端未修 |
| R-144 | **登记** | 失败原因下钻/落盘未做 |
| R-145 | **登记** | CLI/GUI 默认面分叉未对齐（dev 对齐需裁定） |
| R-146 | **已修** `d99df1b` | 再次运行含失败先确认 |
| R-147 | **登记** | 设置生效时机标注未做 |
| R-148 | **登记** | 目录枚举失败静默未提示 |
| R-149 | **登记** | Windows 大小写/短名去重未做 |
| R-252 | **已修** `594d93f` | 五条时序约定补 Q_ASSERT 锚点 |
| R-253 | **已修** `594d93f` | refresh_status/theme 分段 |
| R-254 | **登记** | remove_sel 双写/natural_bytes 双实现/T() 混用/Impl 分舱 |
| R-255 | **登记** | 探针函数超长（smoke_probe_meta 1647 行） |
| R-256 | **登记** | apply_map_style 名实不符 |
| R-257 | **部分** `594d93f` | mainwindow.h:45 注释修正（B57）；错误三态**登记** |
| R-258 | **已修** `594d93f` | 探针与产品分 TU |
| R-259 | **登记** | 析构期 join 无反馈 |
| R-260 | **登记** | page_meta.h 钩子清单寄生（B.1 未做） |
| R-261 | **已修** `a6e29f6` | MapDialog::release 复原双边化 |
| R-262 | **已修** `a6e29f6` | 「限扫 200」注释改对（≤8） |
| R-263 | **已修** `d99df1b` | 重命名预填显示名 |
| R-264 | **部分** `594d93f` | fmt 族收 run_status_text.h；natural_bytes 双实现（R-254）**登记** |
| R-265 | **登记** | page_output 死码三连 |
| R-266 | **登记** | active_id 指向已移除格式 |
| R-267 | **已修** `7472b97` | nav_num 引用 badge 单源 + ChipButton 注释纠偏 |
| R-268 | **部分** WP11（D6） | icons/paramform 两处注释改实；余见 R-269 |
| R-269 | **登记** | paramform " " note 占位 + 词根启发式（group 数据面已备，X1 未接） |
| R-270 | **部分** `7472b97` | QSS 魔法值部分令牌化；菜单/页签内距余量**登记** |
| R-271 | **已修** `25b60b3` | icons 解析器单测（+ PP_ICONS_TEST_NO_WIDGETS） |
| R-272 | **部分** `40f3405`+WP11（D4） | 死 include 随拆分消除 + bench 魔法数命名 + kMaxExactInt |
| R-273 | **登记** | 错误文案通道分裂（随 R-070 跨界） |
| R-274 | **已修** `40f3405` | --bench-cases 清单精确匹配 |
| R-275 | **登记** | settings_geom_probe 重复段/虚计数 |
| R-276 | **登记** | 几何函数两口径（裁剪/未裁剪） |
| R-277 | **登记** | 预览失败恒显「载入中」 |
| R-278 | **登记** | 死 API 面 + printf 绕过 logger |
| R-279 | **登记** | Caption 双实现未合并；thumbnails.h `<memory>` 跨界（include 级超出注释/文案/常量授权） |
| R-280 | **登记** | 魔法数残留（GroupCountRole 等 4 处；非 handoff 余量，收后续） |
| R-281 | **登记** | 几何探针副作用 |
| R-282 | **部分** `12ce994` | effective_datetime 收敛；BMP/with_extension 保留裁定（C1/C2） |
| R-283 | **部分** `40f3405`+WP11（D1） | scheduler/dev_cli 注释 + fsops.h/settings.h 注释改实 |
| R-284 | **已修** `40f3405`+`594d93f` | 纯函数单源 + run_rows_model 同源权重 |
| R-285 | **已修** `12ce994` | metadata-only 能力探测进程缓存 |
| R-286 | **部分** `40f3405` | include 清理+导航注释；metadata.cpp 反向 include **登记** |
| R-287 | **已修** `40f3405` | ProgressMux OutState 状态机 |
| R-288 | **已修** `40f3405` | 高水位单层 |
| R-289 | **已修** `12ce994` | flatten_ms 累加口径统一 |
| R-290 | **部分** `12ce994` | write_atomic×2 收敛；LRU×3/trim×2 **登记** |
| R-291 | **登记** | settings 键表半表驱动（写侧表 vs 读侧 18 分支） |
| R-292 | **对账登记** WP11（D11） | 13 组明细见附录 C：收敛 6 / 登记 7 |
| R-293 | **已修** `33d09a8` | format_tables 冻结头改枚举式口径 |
| R-294 | **已修** `33d09a8` | 边界值补出处（UI 范围标注） |
| R-295 | **部分** `33d09a8`+WP11（D3） | 裸键清零 + paramform 注释 |
| R-296 | **跨界** | CICP 映射表驱动（colormanager 面，无包认领） |
| R-297 | **已修** `33d09a8` | interleave 2→2 直通 + ref 越界修复 |
| R-298 | **已修** `33d09a8` | enc_heif Float 死分支显式拒绝 |
| R-299 | **已修** `33d09a8` | tie-break 显式化 + resolved_backend |
| R-300 | **已修** `33d09a8` | encoders.cpp 过期注释清理 |
| R-301 | **已修** `33d09a8` | trampoline atomic<bool> |
| R-302 | **部分** `33d09a8` | pragma FP_CONTRACT OFF；构建级收缩关闭**登记**（跨界 WP3 未接） |
| R-303 | **已修** `33d09a8` | set_force_scalar 脚手架 + 分测 |
| R-304 | **已修** `33d09a8` | seek_subimage 返回值检查 + info 消费面显式化 |
| R-305 | **已修** `33d09a8` | 内省枚举自增 + 硬上限；probe_cache 失效策略成文 |
| R-306 | **已修** `a6e29f6` | mapwidget 三处注释失实改对 |
| R-307 | **已修** `a6e29f6` | kChinaBBox 单源 |
| R-308 | **已修** `25b60b3` | rect_of 并入 to_physical + 钩子寄生面显式区 |
| R-309 | **已修** `25b60b3` | path_under 空目录→空路径（settings.h 同款病灶**跨界**，§4.1） |
| R-310 | **已修** `25b60b3` | QPointer |
| R-311 | **已修** `a6e29f6` | HUD 双轨注释 + kHudPointSize |
| R-312 | **已修** `25b60b3` | 三钮命中优先（同 UX-7②） |
| R-313 | **已修** `a6e29f6` | 点选联想停防抖 |
| R-314 | **登记** | 键盘路由吞方向键（RTL 同族结构性） |
| R-315 | **部分** `a6e29f6` | tiles_pending_changed 接加载 chip；search_finished 不接（B49 裁定） |
| R-316 | **已修** `33d09a8` | param-catalog 12 处漂移对齐 |
| R-317 | **登记** | 预估卡两项恒「—」——维持不编数（M5-A24 口径，非缺陷） |
| R-318 | **部分** `33d09a8` | 三假控件「已禁用：原因」；全量推广**登记** |
| R-319 | **登记** | CLI --base GUI 无入口 |
| R-320 | **已修** `7472b97` | 第 7 份 ElidedLabel（并入 R-020） |
| R-321 | **登记** | page_output template_has_format 副本/注释失实（core 单源已备，消费端未换） |
| R-322 | **已修** `40f3405` | pp::template_has_format 判据单源 |
| R-323 | **已修** `40f3405` | 拆分边界证据落地（B.6 四 TU） |
| R-324 | **已修** `40f3405` | run_one_file 十阶段（B.4） |
| R-325 | **部分** `40f3405` | 枚举/校验器收口；强类型路线**登记**（C8） |
| R-326 | **登记** | FileRow 不变量 15 条仍活在注释 |
| R-327 | **登记** | 扩展性短板（成本表在 adjudications/报告 §1） |
| R-328 | **已修** `40f3405` | 第 6 编码器成本表交付（handoff §⑥） |
| R-329 | **部分** | 数据驱动评级随 R-292 对账（附录 C） |
| R-330 | **登记** | 两页骨架不对称（B.2/B.3 蓝图） |
| R-331 | **结案** | 结论性条目：GUI 异常面无用户可触发崩溃；唯一实缺陷 R-152 已修 |

---

# 附录 B：UX-14 项台账

| UX | 去向 | 依据 / 落点 |
|---|---|---|
| UX-1 右键菜单 | **部分** `d99df1b` | contextmenu 公共件 + 12 类 actions + 文件列表/运行页 4 卡接线；⑥⑦⑧⑨ 四卡与任务卡行级动作**登记**（X3） |
| UX-2 全选/取消 | **部分** `d99df1b` | 右键三项（勾选全部/取消/反选，作用于可见行）；工具行 pp-check-menu + Ctrl+A 系快捷键 + icons check-all **登记** |
| UX-3 预览分辨率 | **已修** `7472b97` | 门槛拆通道 1600/64 + 缓存 4096/8 + test_thumbs 两通道断言（B19/B20） |
| UX-4 GPS 地图 App 化 | **已修** `a6e29f6` | 浮层结果/分类下拉/图层浮层/地址 tooltip/地图 260 + build_gps_card 拆函数；「stretch 吃满右栏」余量**登记**（W7 跨界 4） |
| UX-5 启用开关 | **已修** `a6e29f6` | 三卡 SwitchControl + build_rules 跳过 + gps_enable 双向同步（B47/B48） |
| UX-6 样式收口 | **部分** `7472b97`+WP11（D7） | 几何常量/骨架单源/QSS 收口/ppFk 三轨/写死色清零 + spec §5 R1 例外登记；exif_editor 行高（③）、classify 8/4→10/12 **登记** |
| UX-7 真无边框 | **已修** `25b60b3` | WM_NCCALCSIZE + 三钮命中优先 + 四弹窗同口径 + 字形重画（B24/B25/B32）；**真机 10 项待签收**（§2.4） |
| UX-8 高级参数 | **部分** `7472b97`+`d99df1b` | 224px 限高退役 + ParamDef.group 数据面 + test_smoke 断言；paramform UI 重做（子分组折叠/整行折叠头/行宽 160）**登记**（X1） |
| UX-9 格式卡 | **已修** `d99df1b`+`e8b9bf1` | 页签去位深 + 等宽（T15 改案）+ 磁贴去位深文本；位深控制功能逐字保留（B39/B43） |
| UX-10 编码档位 | **部分** `33d09a8`+`d99df1b` | 10.1 核心行+联动+落盘反查、10.2 speed/tune 提核心、10.3 白名单对表均落地；色度限幅（X6）**登记** |
| UX-11 滚动条 | **部分** `7472b97` | 6px 细浮层 + 弹窗同皮（B21）；lock_card_size 三页接线**登记** |
| UX-12 亮色页签 | **部分** `7472b97` | tokens 初值 + replay_tokens 补 tabs（B21）；settings-tabs-light 像素探针**登记** |
| UX-13 圆角 | **已修** `25b60b3` | DWMWA_WINDOW_CORNER_PREFERENCE（Win10 静默忽略）；同 UX-7 真机验收 |
| UX-14 色彩质量 | **部分** | A1 `33d09a8`（精确码残余 E1）/ A2 `12ce994` / A3 `33d09a8` / A4 `33d09a8` / A5 `12ce994` / B1 `12ce994` / B3 `33d09a8`；**B2 跨库对拍登记**；**C1–C4（色域映射/抖动/意图/线性光）未实现**（§3.1 WP10 未交付）；D1/D2/D3 解码侧**登记** |

---

# 附录 C：R-292 词表/枚举镜像副本 13 组对账（WP11，D11）

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
| `platform/mica.h:11` | 主题切换监听 | 未标「已废/已落」——**登记**（WP5 跨界 5：v1 口径维持，未扩展监听） |
| `core/settings.h:38` | map_tile_source/map_geocoder 键状态不明 | 未标——**登记**（键确已不存，属历史注释） |
| `core/simd/simd.h:17` | AVX-512 预留 | 未标——**登记**（预留语义仍成立） |
| `mapwidget/providers.cpp:11-18` | 「待迁移清理」已过期 | **已收口** `a6e29f6`（W7-A5：改标「冻结遗留、仅测试可见」） |

（本表即评审要求的「M7 候选表」落点；前三处如需注释标注属注释级修改，可随下一轮顺带。）

---

*报告完。终门禁数据由 WP12 在 §2.3 追加；本报告由 WP11 于 2026-10-12 收口。*
