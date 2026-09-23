# PhotoPipeline M4（0.3.0）任务书 v1.0

> 依据：`docs/v0.3.0-consensus.md`（21 项决策）+ `docs/v0.3.0-design.md`（接口/实现定稿）
> 里程碑：**M4 = 0.3.0**（多格式输出 + 生效值元数据 + 预览分类 + 逐文件进度 + 自适应并行 + GUI 重构 + CI 工业化 + 依赖全升 + AVX2）
> 开发组织（D21）：**subagent 开发制**——开发任务经 dynamic workflow 由 `deepseek/deepseek-flash`（思考强度 max）执行；主对话负责编排、评审、金样把关、文档收口。
> 纪律基石：PP-FROZEN 解冻/再冻结（D20）· 鼓点纪律 · 单测随码走 · 金样把关 · 单实现跨平台 · 逐任务提交锚点。

---

## 1. 总出口准则（0.3.0 发行门槛）

1. 九项需求逐条验收（共识 §1 表 + 设计 §1 验收目标）全绿。
2. 金样 **20/20**（16 旧对不回退 + 4 新对）；ctest 全绿（含新增单测）；UI 冒烟新冻结行 `UI-SMOKE OK shots=12 pages=3`。
3. CI 四道（pr-fast / main-full / nightly / release）全绿且满足双 ≤20min（warm-cache 豁免口径见设计 §12.6）。
4. 依赖升级表（设计 §10）落地：全升项到位、停留项零漂移；`pp_linkprobe` 扩展断言全绿。
5. AVX2：全产物 `-march=x86-64-v3`/`/arch:AVX2`（构建日志可查证）；SIMD 5 对一致性单测全绿；基准无回退 + 多格式解码共享达 1.35× 目标。
6. 接口再冻结完成：§3 裁定表全部落地并重新标注 PP-FROZEN。
7. 双平台产物（AppImage + win64.zip）五门禁 + 烟测通过；tag → GitHub Release 自动化通过含前置探测。
8. 文档收口：本任务书执行记录 + `docs/m4-report.md` + CHANGELOG 0.3.0 + README 增补。

---

## 2. 前置门槛（进入 W2 前置条件）

| 门槛 | 责任 | 说明 |
|---|---|---|
| G-A | 用户 | **GUI 原型确认**（`docs/mockups/*.png` 四张）——已产出待确认；如需调整先改原型再开发 |
| G-B | 用户 | **解冻裁定表批准**（设计 §3 全表）——批准即解冻 |
| G-C | 主对话 | W0 金样基线存档（升级前 `tools/baseline/` 双份归档） |

---

## 3. 波次与任务

### W0 · 解冻与地基（依赖升级一把全升 + AVX2 + CI 骨架）

| # | 任务 | 内容 | 出口 |
|---|---|---|---|
| T1 | 解冻落地 | 设计 §3 裁定表逐条落为代码头注释（`PP-THAWED`→落地后 `PP-FROZEN(0.3.0)`）；`vcpkg.json` version 字段修正 | 裁定表 100% 对应到文件 |
| T2 | 依赖一把全升 | overlay bump：OIIO 3.1.17（8 patch 重验）、libjxl 0.12、libheif 1.23.5、exiv2 0.28.9（**删 enableBMFF 调用**）、x265 4.3（multilib 保留）、SVT-AV1 4.2、aom 3.15.1、libde265 1.1.3；其余滚动到 vcpkg master 可得最新；**Qt 6.8.3→6.11.2**（versions.env）；CMake 兼容线 `3.28...4.4` | 双平台构建绿；linkprobe（含内省面断言）绿；金样 16/16 无回退（R21/R25/R27 在此引爆） |
| T3 | AVX2 地基 | 自定义 triplet（`x64-windows-avx2`/`x64-linux-avx2`）+ `cmake/avx2.cmake`（flags + raptorlake 回退探测）；OIIO `USE_SIMD=avx2`；`src/core/simd/` 骨架（`cpu_has_avx2`） | 构建日志可见 AVX2 flags；依赖二进制基线抽查（dumpbin/objdump） |
| T4 | CI 骨架先行 | `pr-fast` 道落地（fmt 门禁 + 双平台 build/unit + 金样子集）；缓存键追加 triplet 哈希；`clang-format` 全仓对齐 | PR 道全绿且 wall ≤20min（目标 ≤12） |

### W1 · 引擎（多格式 + 进度 + E3 调度）

| # | 任务 | 内容 | 出口 |
|---|---|---|---|
| T5 | 多格式管线 | `pipeline.h`/`encoder.h` 重排（§3.1/§3.2）；`fsops::output_path` 真值表（§4.2）；解码共享+alpha flatten 编排（§4.3）；逐输出冲突登记（§4.4）；`run_metadata_only` 互斥校验 | `--dev` 1→N 输出全语料；金样 3 新对（multiformat-split/mirror/conflict）绿 |
| T6 | 进度系统 | `core/progress`（权重表/ProgressSynth/ProgressMux）；编码器 progress 映射（jpegli 行、jxl 回调、OIIO 行；webp/heif/avif 合成）；sidecar 进度快照 | `test_progress` 绿；金样 `progress-trace` 绿 |
| T7 | E3 调度器 | `alloc_threads` 纯函数 + 并发闸；交错限速器（stagger_ms）；取消路径；线程映射义务（§3.1） | `test_scheduler` 扩展绿；TSan 批压测 0 报告；运行日志含交错偏移/线程分配 |
| T8 | classify + 生效值 | `core/classify`（注册表/持久化/分组键）；`metadata::preview_effective`（与 build_plan 同源纯函数） | `test_classify`/`test_effective` 绿 |

### W2 · GUI 骨架（前提：G-A + G-B）

| # | 任务 | 内容 | 出口 |
|---|---|---|---|
| T9 | 主题与骨架 | `ui/theme` tokens 双主题；**无边框窗口 + 自绘 caption 三钮**（`platform/frameless`：HTCAPTION 拖拽/八向缩放/HTMAXBUTTON Snap）；三栏骨架（QSplitters 持久化）；底栏/顶栏；Mica/深色标题栏回归 | 明暗两主题截图与原型逐区对齐（±4px）；窗口行为矩阵（拖拽/双击最大化/Snap/边缘缩放）手测通过 |
| T10 | 预览面板 | `decode_preview` + 异步池 + LRU；缩放适应/1:1；翻图/徽标/热键提示 | 点选 250ms 内出图（48MP 实测）；内存受控 |
| T11 | 文件列表/分类 | 勾选（CheckState role）+ 搜索正交 + 自动分组 QTreeView；分类面板（CRUD/热键/整类圈选） | UI 冒烟扩展段绿；`classes.json` 往返 |

### W3 · GUI 三页（并行于 W2 后半可开工）

| # | 任务 | 内容 | 出口 |
|---|---|---|---|
| T12 | 元数据页 | 关键信息卡 + 时间「原→生效」+ GPS 生效值 + 地图跟随 + 多选小表；.mod 高亮 | 手动走查清单 + 生效值显示与写入一致性抽查 |
| T13 | 输出页 | 模式互斥 + 8 磁贴多选 + 格式页签（位深+ParamForm）+ 单行分文件夹开关 + 路径模板（$ 符号解析/校验）+ 单行示例 + 预设 v2（迁移） | 手动走查；`test_presets` v2 迁移绿；`test_fsops` 模板解析绿 |
| T14 | 运行页 | 逐输出进度行（真实/斜纹）+ 总览（线程预算读数/交错）+ 失败原因 + 锁定态 | UI 冒烟新冻结行 `shots=12 pages=3` 逐字 |
| T15 | 设置/对话框 | 设置追加项（stagger/thread_budget/分文件夹默认）；EXIF 编辑器适配生效值样式 | 设置持久化往返 |

### W4 · 工程（CI 工业化收口）

| # | 任务 | 内容 | 出口 |
|---|---|---|---|
| T16 | CI 全量 | main-full / nightly（asan/ubsan/tsan 三路并行）/ release 前置探测（查重+产物断言）；tidy 棘轮基线入库；预算表实测校准 | 四道全绿 + 双 ≤20min 实测数据入报告 |
| T17 | Windows 回归基线 | `tools/baseline/golden.windows.log` + regression.py 双平台口径 | Windows 基线 diff 绿（M3 遗留清零） |
| T18 | 打包/发布 | make_winzip/make_appimage 适配新依赖闭包（Qt 6.11 DLL 面）；许可 39+ 清点 | 五门禁 + 产物烟测绿 |

### W5 · 性能与收口

| # | 任务 | 内容 | 出口 |
|---|---|---|---|
| T19 | SIMD 热路径 | 5 对 `*_avx2/ref` + 一致性单测；`--dev bench` 基准场景 | 单测绿；flatten ≥2× 标量；多格式 ≤1.35× 单格式×N |
| T20 | 金样/UI 收口 | 金样 20 对全量；UI 冒烟全量；TODO 标签清零 | 全绿 |
| T21 | 发行 | 版本 0.3.0 单源提升；m4-report.md；CHANGELOG；README；tag `v0.3.0` → Release | §1 总出口 8 条全勾 |

---

## 4. subagent 开发协议（D21）

1. **执行体**：每个波次 = 一次 dynamic workflow 运行（`CreateWorkflow`），`subagent_model: "deepseek/deepseek-flash"`、思考强度 **max**（`$max`）；波次内任务按依赖序编排（T5→T6→T7 可部分并行，T9→T10→T11 串行）。
2. **世界闸门**：每个任务完成后 workflow 脚本必须 `world.run` 闸：`build → ctest → 金样子集` 全绿才放行下一任务；红则该任务 subagent 返工（同 run 内循环），再红则 escalate 主对话。
3. **改动边界**：subagent 只允许改动其任务声明的文件面；碰 §3 裁定表外的冻结接口、或需要表外解冻 → 立即停手 escalate。
4. **提交纪律**：每任务一个提交（`M4-T<n>: …`），带 SPDX 头；主对话评审后合入；鼓点=提交前 `--dev` 全语料跑一遍（引擎/管线类任务）。
5. **GUI 任务**：以 `docs/mockups/*.png+html` 为实现依据，偏离控件集合/语义必须先改原型。
6. **文档**：执行期事实（裁定/实测/偏差）随手记入任务节，W5 由主对话统一成 `docs/m4-report.md`（M3 报告口径：不圆场、逐字证据）。

---

## 5. 纪律与偏差处理

| # | 条款 |
|---|---|
| 1 | 铁律五条 + M3 铁律六（单实现/机制分派）全程有效 |
| 2 | 冻结纪律：D20 裁定表外零改动；再冻结后同 v0.2 口径 |
| 3 | 鼓点纪律：模块级 `--dev` 全语料；禁止大爆炸首次通电 |
| 4 | 金样只增不减：16 旧对断言不得放宽容差 |
| 5 | CI 预算是约束不是目标：超 20min = 设计失败，拆 job/砍步骤，禁放宽时限 |
| 6 | 偏差处理：执行与规格偏差 → 逐条记 m4-report「偏差与例外」，机械性偏差主对话批准后追认，语义性偏差必须回报裁定 |
| 7 | 缓存纪律（M3 教训）：冷构建在飞期间不得推送 |
| 8 | 发行纪律：tag 未发布资产前可移动，发布后不可移动（M3 §7.1-11 延续） |

---

## 6. 验收走查清单（W5 手动项，GUI）

- [ ] 分类热键打标/改名/删除/整类圈选；重启后分类仍在
- [ ] 预览翻图/缩放/徽标；运行期只读
- [ ] 时间/GPS/地图生效值：修改前两值相同、修改后高亮、多选小表、隐私剥除提示
- [ ] 输出页：多选→分文件夹默认开→取消→单选恢复；路径模板编辑/校验/示例正确；仅元数据互斥置灰
- [ ] 运行页：行级进度（JPEG）/斜纹（WebP）/失败原因/交错与线程预算读数/取消
- [ ] 明暗双主题 + Mica + 深色标题栏
- [ ] 预设 v1 迁移加载 + v2 保存再加载
