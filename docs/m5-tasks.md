# PhotoPipeline M5（GUI 刷新 v6 + 0.3.0 发行收口）任务书 v1.0

> 依据：`docs/qt-gui-design-spec.md`（**v6 单源规范**，违反即回归）+ `docs/mockups-v2/`（视觉终稿 4 页 × 亮暗 8 张 PNG + HTML/tokens.css）
> + `docs/gui-review-0.3.0.md`（159 条问题台账，结论已被 v6 吸收）+ `docs/m4-tasks.md`/`docs/m4-report.md`（0.3.0 现状与未决项）。
> 版本口径（用户已确认）：**GUI 刷新折入 0.3.0 树**，版本号不变；全量验收后 `tag v0.3.0` → GitHub Release（发行动作已获授权）。
> 开发组织（D21 延续 + 用户新规）：开发任务经 dynamic workflow 由 `deepseek/deepseek-flash`（思考强度 max）执行；
> **架构/设计/接口由主对话定稿**；子代理**禁止再派发子代理**；主对话零开发动作（编排/设计/评审/闸门裁定/发行动作除外）；
> 开发全程不问询用户、不停工。

---

## 0. 设计权威链

1. `docs/qt-gui-design-spec.md`（v6）——表单体系 R1–R4、字阶、字段轨宽、控件语义、页面规格、验收清单 17 条，**以本文为准**；
2. `docs/mockups-v2/*.png|html`——视觉终稿；实现期布局允许 ±4px 微调，**控件集合与语义不得偏离**；偏离先改原型（HTML→PNG 重渲）再开发；
3. `docs/v0.3.0-design.md` §9——骨架与平台接线沿用（与 v6 冲突处 v6 优先：标签轴 70→**80px**、行高 32→**28px**、栏宽 左 330/右 587、卡圆角 8→**6**、字阶 12.5→**v6 阶梯**）；
4. `docs/gui-review-0.3.0.md`——问题背景（K1–K6 裁定结论均已入 v6：无 ✓ 状态装饰、数值保留 spinbox、选中零形变、指标单源、预览画布功能、设置弹窗重做）。

## 1. 架构契约（主对话定稿，子代理不得擅自变更）

| # | 契约 | 内容 |
|---|---|---|
| C1 | 令牌单源 | `src/ui/theme.h` 是唯一令牌源（颜色/字阶/间距/圆角/控件度量 + QSS 模板生成）；明暗=换令牌重建 QSS；暗色卡片无投影、画布底 `#16181d` 不随主题变 |
| C2 | 表单唯一入口 | 新建 `src/ui/formkit.{h,cpp}`：行构造器（frow 单列/kv 双列/check 行）**在构造层强制 R1–R4**——pitch 28、标签轨 80 右对齐、列距 12、字段宽仅 88/160/flex、双列 80/轨/24/80/轨、单位 16px 槽、范围 10px 注释；页面不得绕过 formkit 手摆栅格 |
| C3 | 图标矢量化 | 新建 `src/ui/icons.{h,cpp}`：**QPainterPath 矢量图标**（24×24 网格、描边 1.5px 等线宽、圆角端点）；`assets/icons/*.svg` 为设计源入库，路径数据转录进 C++（constexpr）；**不引入 Qt6::Svg、不加 qrc**（打包闭包零漂移——实现裁定，登记为机械性偏差）；全界面**禁止 Unicode 字形充当图标** |
| C4 | 测试钩子冻结 | `pp-*` objectName 全保留；`tests/ui_smoke.py` 冻结行 `UI-SMOKE OK shots=12 pages=3` **逐字不动**（12 张截图同名单重渲）；既有 T11/T14/T15 自检行前缀全保留 |
| C5 | 引擎零改动 | `src/core` `src/codecs` `src/decode` `src/mapwidget/providers.*` **零改动**（mapwidget 外观仅随令牌）；PP-FROZEN 表外动一行即停手升报；金样 20 对断言零改动 |
| C6 | 设置加键规则 | 新设置项（主题手动选择等）走 `AppSettings` 追加键，默认值=现行为，`test_settings` 随码扩展；预设 schema 不动（v2 + lossless 键） |
| C7 | 探针机械化 | 每项视觉改造必须在 `--ui-smoke` 进程内断言追加**几何探针**（pitch/轨宽/控件高/caption 46×32/选中零形变/明暗等价…对齐 v6 §12 逐条），把"看起来对"变成可断言；探针阈值仅可随令牌值机械调整，逐条登记 |
| C8 | 进度语义不变 | 进度条：实心=已写盘、斜纹=合成、绿=完成、琥珀=异常（0 字节）、红=失败；`run-bars` 像素取证（实心 vs 双色带）必须继续可判 |

## 2. 波次与任务（串行推进；每任务一提交 `M5-T<n>: …`，SPDX 头，clang-format 过）

### W-G1 地基（串行）

| # | 任务 | 文件面 | 出口 |
|---|---|---|---|
| T1 | 令牌层 v6：theme.h 重写（§4 颜色/字阶/间距圆角 + R4 控件度量 + QSS 生成）；`set_mod`/`.pill` 等既有 API 保持可编译（调用点最小适配逐文件登记） | `src/ui/theme.h`（+最小调用点） | build 双预设 + ctest 27/27 + 冻结行逐字 |
| T2 | formkit 表单框架 + 自绘控件：行构造器/Seg/Switch/Tile/Chip/Pill/ProgressStrip/IconButton（几何常量纯函数可单测） | `src/ui/formkit.*` 新建、`tests/unit/test_formkit.cpp` 新建 | 同上 + 新单测绿 |
| T3 | 矢量图标集 + 全局换图标：caption 三钮（WinUI3 46×32/关悬停 #c42b1c 白字）/☆ ⚙/搜索/加减/筛选/分组/缩放/翻页/浏览/图钉/日志/▶/chevron/check 等；accessibleName+tooltip 齐 | `src/ui/icons.*` 新建、`assets/icons/*.svg`、各调用点字形替换（逐文件登记） | 同上 + 证据：全仓 grep 无字形图标残留 |

### W-G2 壳层（串行）

| # | 任务 | 文件面 | 出口 |
|---|---|---|---|
| T4 | 顶栏 44px 三段式（品牌/一体式分段导航 92×30 无 ✓/工具+caption）+ 状态栏 30px（自然语摘要+22px 实心 CTA）+ 无边框合规复核（HTCAPTION/八向 6px/HTMAXBUTTON，frameless 命中几何不动）+ 栏宽固定轴 330/587 | `mainwindow.*` 壳区、`platform/frameless.*` 仅核对 | 闸门 + caption/顶栏几何探针 |
| T5 | 文件列表行 58px（勾选+46px 缩略图+2 行信息不截断+状态胶囊）+ 工具行常驻（搜索/添加/移除/筛选/分组）+ 空态投放区 + 分类面板（chip 打标/计数中性胶囊） | `filelistmodel.*`、`classify_panel.*`、mainwindow 列表区 | 闸门 + T11 自检行全保留 |
| T6 | 预览画布：`#16181d` 舞台、空态图标+引导语、缩放条悬浮右下（−/100%/＋/适应/1:1）、左下拖动提示、角标空态隐藏、小图最近邻、◀▶ ≥28px | `preview_panel.*`、`mainwindow` 预览区 | 闸门 + 预览几何探针 |

### W-G3 三页与弹窗（串行）

| # | 任务 | 文件面 | 出口 |
|---|---|---|---|
| T7 | 元数据页 §7.1：关键信息 kv 双列 5 行/时间偏移（分段+3×spin88+生效对照行 acc）/GPS（双列轨+地图+操作行）/隐私与文件时间；.mod 高亮沿用 | `page_meta.*` | 闸门 + 生效值探针 + meta-strip/meta-gps 探针保留 |
| T8 | 输出页 §7.2：模式分段+注释/8 磁贴 2×4（预留对勾位零形变）/全局卡（模板 $var+变量表 tooltip+示例行+分文件夹开关）/格式页签（**一行一义六行**：位深/技术/无损/编码努力/视觉距离/高级参数折叠+搜索，其余参数入折叠区）/预设与预估 | `page_output.*`、`paramform.*` | 闸门 + 模板/互斥探针保留 |
| T9 | 运行页 §7.3：总览（指标单源 kv/操作行）/任务进度表（缩进行/实心斜纹/0 字节琥珀/失败原因行/右对齐数值）/日志尾（等宽真实尾部） | `page_run.*` | 闸门 + T14 自检行全保留（run-bars 可判） |
| T10 | 设置弹窗**重做** §7.4：660px、标题栏 40px、下划线页签 通用\|运行\|输出\|地图\|关于（与主步骤同序）、行 28/标签 80/字段 160、魔法值"自动"、alpha 底色行重排（端标+预览色块一致）、底栏 48px 恢复默认左/取消+实心确定右；EXIF 编辑器/预设弹窗同语言 | `settings_dialog.*`、`exif_editor.*`、`presets_dialog.*` | 闸门 + settings-dialog 自检行保留 + 设置往返绿 |

### W-G4 验收（主对话参与评审）

| # | 任务 | 出口 |
|---|---|---|
| T11 | v6 §12 十七条逐条核验（探针+截图证据）；明暗成对截图 vs mockups-v2 对照（截图纪律见 §3.4） | 十七条逐条 PASS 行 + 偏差清单 |
| T12 | 偏差修复轮（T11 清单逐条） | 全量回归：ctest 27/27 + release-dev 28/28 + 金样 20/20 + 冻结行 + regression zero diff |

### W-R 发行收口

| # | 任务 | 出口 |
|---|---|---|
| T13 | CI 四道首跑：先 `warm-cache.yml` 播种，再 workflow_dispatch 四道；红则修（预算双 ≤20min 硬约束，禁放宽时限；tidy 首跑红→`tools/ci-tidy.py --update` 重播种=裁定 r2） | 四道全绿实测数据入报告 |
| T14 | 0.3.0 打包轮：win64.zip 五门禁（闭包/来源/结构/许可/启动）+ `collect_licenses.py --check`；AppImage 走 CI job | 五门禁 + 烟测绿、指纹入报告 |
| T15 | 发行：CHANGELOG/README/m5-report 收口 → `git push` → `tag v0.3.0` → push tag → GitHub Release（release.yml 前置探测：查重+产物 `--version` 断言） | M4 总出口 8 条 + v6 十七条全勾 |

## 3. subagent 协议（D21 + 用户新规）

1. **执行体**：每波次一次 `CreateWorkflow`；`subagent_model: "deepseek/deepseek-flash$max"`；提示词=任务全要素（目标/文件边界/设计引用章节/接口契约/约束/出口/回报格式/停手条件），由主对话逐任务写全。
2. **禁止再派发**：子代理不得派发任何子代理/工作流；不得提问等待；遇裁定性问题（语义偏离/表外接口/原型冲突）**立即停手**，在回报中如实陈述。
3. **世界闸门**：每任务完成 → `world.run` 闸 `build release → build release-dev → ctest release → tests/ui_smoke.py`；红则同轮返工（附失败输出全文），再红升报主对话。
4. **提交纪律**：闸门绿后每任务一个提交 `M5-T<n>: …`（SPDX；`bash tools/ci-format.sh` 过）；**工作树既有未跟踪杂物（`nul`、`docs/mockups$n.png`、`docs/gui-fix-mockups*` 等）不许触碰、不许纳入提交**。
5. **截图与识图纪律（用户明令）**：截图对比只信**当次新产**文件——绝对路径、文件名含时间戳+随机数、读前 `ls` 核实存在；送人眼/识图前加 **LSB 不可见噪声**（每张唯一）防缓存；识图报错一律按路径错误/历史文件名/缓存规避缺失处理，**禁止浪费时间排查"识图功能"**。
6. **回报格式**（每任务末）：改动文件清单 / 提交 hash / 闸门结果行 / 新增探针行 / 偏差与登记 / 建议。不贴大段代码。

## 4. 纪律

1. 铁律五条 + M3 铁律六（单实现、机制分派、零平台分叉）全程有效；负面清单（共识 §5）继续有效——GUI 刷新只做视觉与排版，不新增功能语义。
2. PP-FROZEN：本轮 UI 层改造，冻结面零改动；金样只增不减、冻结断言零放水。
3. 鼓点纪律：闸门全绿才放行，禁止大爆炸；偏差处理=机械性记账后追认、语义性立即升报。
4. 不圆场：未实测如实标"未跑"；证据逐字引用（日志行/探针行）。
5. 发行纪律：tag 挂 Release 资产前可移动、挂上后不可移动；推送遵守缓存纪律（冷构建在飞不推送）。

## 5. 验收与发行清单（收口勾选）

- [ ] v6 §12 十七条逐条 PASS（探针+截图证据）
- [ ] m4-tasks §6 七项走查的自动化部分全绿；证据包留 `.cache/m5-review/`（供用户抽样目检）
- [ ] ctest 27/27 + release-dev 28/28 + 金样 20/20 + 冻结行逐字 + regression zero diff
- [ ] CI 四道全绿 + 双 ≤20min 实测
- [ ] win64.zip 五门禁 + 许可审计 + AppImage 烟测
- [ ] `docs/m5-report.md` + CHANGELOG 0.3.0 增补 + README 增补
- [ ] `tag v0.3.0` → GitHub Release（含前置探测）
