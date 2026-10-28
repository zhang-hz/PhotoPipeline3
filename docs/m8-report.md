# M8 收口报告（进行中）

> 状态：**进行中**（2026-10-28 开工）。按 `docs/m8-workflow.md` 的 K/A/B/C/D 阶段追加；每阶段结束补“阶段验收”。
> 版本：M8 / 0.4.0（版本号届时按 `CMakeLists.txt:19` 单源更新）。
> 关联：设计 `docs/m8-encoder-params-and-ssimulacra2-design.md`（v4）｜参数范围 `docs/m8-param-exposure-review.md`｜控件形态 `docs/m8-param-control-map.md`｜纪律 `docs/m8-discipline.md`。

## 0. 阶段总览

| 阶段 | 状态 | 提交 | 关键证据 |
|---|---|---|---|
| K 启动 | 完成 | 并入 M8-A 提交 | `build/m8/k3-baseline/cmd.log` |
| A 参数事实层 | 完成 | 待提交 | `build/m8/a4-{build2,ctest,smoke,regression,format}.log`、`build/m8/a4/reconcile.txt` |
| B 引擎 + UI | 未开始 | 待定 | — |
| C SSIMULACRA 2 | 未开始 | 待定 | — |
| D 收口 | 未开始 | 待定 | — |

## 1. 交付内容（逐阶段追加）

### K 启动

- K1 `docs/m8-adjudications.md`（冻结面加性逐条登记，M8-A1…A16）
- K2 本报告骨架
- K3 基线门禁留档（Stage A 前状态）
- K4 任务板（task-1/2/3 数据卡 + 写作用域/依赖/验收）+ 构建树约定
- K5 `docs/m8-discipline.md`（任务卡自包含/禁止递归派发）已确认并生效

### A 参数事实层（完成）

- **A0**：新增 `src/codecs/param_catalog/catalog.{h,cpp}`（CatalogSpec/CatalogRow/查询/计数/合并辅助）；`tools/gen_param_catalog.py`（specs → `.inc` + anchors，`--validate-only/--check/--strict-counts`）；`tools/param_catalog_specs/README.md`（schema/字段/形态约束/行清单）。
- **A1**：`tools/param_catalog_specs/jxl.json` 16 行（switch 6 / number 5 / dropdown 1 / mode 2 / text 2；anchor 4 / dependent 9 / independent 3）。
- **A2**：`tools/param_catalog_specs/x265.json` 40 行（36 可设置 + 4 只读）。x265 spec 关键决策：`x265:no-deblock` engine 用规范名 `no-deblock`（通用 `no-` 前缀，B1 直接下发、不取反）；X7 按源码订正为 `tu-inter-depth` 并降级为 tooltip note。
- **A3**：`tools/param_catalog_specs/aom.json` 57 行（51 可设置 + 6 只读）、`svt.json` 3 行只读；aom 形态 switch 29 / dropdown 16 / number 6。
- **A4**：生成 `.inc`（rows=116：x265 40 / aom 57 / svt 3 / jxl 16）+ `anchors.inc`（x265 239 / aom 192）；新增 `tests/unit/test_param_catalog.cpp`（计数/形态/角色/分组/锚点/禁名/唯一性/合并辅助，37 项 ctest 通过）。Stage A 未把目录并入 `introspect_backends`/`static_formats` ⇒ **零用户可见行为变化**。

**A4 对账（`build/m8/a4/reconcile.txt`）**

| 后端 | 行数 | 可设置 | 只读 | anchor / dependent / independent |
|---|---:|---:|---:|---|
| heif/x265 | 40 | 36 | 4 | 4 / 25 / 11 |
| avif/libaom | 57 | 51 | 6 | 3 / 18 / 36 |
| avif/svt-av1 | 3 | 0 | 3 | 0 / 0 / 3 |
| jxl/libjxl | 16 | 16 | 0 | 4 / 9 / 3 |
| 合计 | **116** | **103** | **13** | 11 / 52 / 53 |

> 与评审稿“组行 ≈142”差异来自组行合并（8 帧内开关、tskip 对、qm 对、partition 上下限、tile 对、HDR 组、只读行等），**范围未扩大**；键展开 116 行为目录/测试/门禁真源。

**A 阶段偏差（如实登记）**

1. A3 def 取 pinned 源码 **all-intra** 口径：`enable-cdef=0`（插件 `encoder_init:3091` 覆写；评审稿 1 为普通模式默认）、`qm-min=4`/`qm-max=10`（:3100-3101）、`disable-trellis-quant=3`；`cq-level` def 留空（libheif 总先按 quality 映射并 AOME_SET_CQ_LEVEL，库内 10 不可观察）；tune=iq 的 handle_tuning 改 9 个值已逐行 tooltip 注明。
2. A2 def 口径 = `x265_param_default()`；preset slow/tune ssim 与插件强制项（aq-mode=1、psy-rd/psy-rdoq=1.0、rd=4、rdoq-level=2 等）写在 tooltip/reason，不写 def；crf/qp 动态域（随位深）与 qg-size/max-tu-size（随自适应 CTU）留给 B1 预检。
3. A3 `min-cr` 域 [0,1000] 为实用域（源码无上界校验），标“待 B1 源码核对”；`target-seq-level-idx` 仅列合法 level 索引 + 31 不约束；`set-tier-mask` 按源码位掩码；`tune-content` 含 `film`。
4. A2 只读 4 行 role 按语义取（profile/keyint/input-csp=dependent，total-frames=independent；control-map 未规定，登记待 B1 统一）。
5. 生成器在 Stage A 修复 4 处（登记 M8-A14/A15）：KEY_RE 允许 `__` 只读键；AOM anchors 含 `ARG_DEF_ENUM`；x265 `no-` 别名锚点；JXL text 行 `expected_format` 修正；另修正 anchors 计数字符串。
6. A1 `intrinsic_size` 以 `form=text` 承载 x/y 对（“宽x高”），B1/B2 负责解析/校验；HDR 三值上界 10000 为 UI 约定（libjxl 不校验）。

## 2. 验收结论（命令 + 结果逐条取证）

### K3 基线（Stage A 前）

命令与日志：`build/m8/k3-baseline/cmd.log`。结果：

- `ctest --test-dir build/release-dev`：**36/36 passed**（60.88 s）。
- 金样：**`SMOKE total=20 pass=20 fail=0`**（20 个 case 全 PASS）。
- UI：**`UI-SMOKE OK shots=12 pages=3`**；`layout-audit checked=1069 bad=0`；`format-card checked=81 bad=0`。
- `regression.py`：本机 **DIFF**，归因为**已知环境形态差异**（非代码回归）：基线 runner 4 vCPU `thread_budget=4` vs 本机 16 核 `thread_budget=16`，以及 libjpeg-turbo buildtree 路径截断差异（`vendor-stdout`）；AGENTS §2 已登记“基线为 runner-shaped，本机直跑出 thread-shaped diff”，CI 为权威。Stage 门禁复核时沿用此归因，不重播种。
- `ci-format.sh`：首次因 pwsh PATH 无 `bash` 未执行；用 `C:\Program Files\Git\bin\bash.exe` 复跑（`build/m8/k3-baseline/format.log`）。

> 说明：本机回归不能 zero diff 是**已知基线形态**，不是 M8 引入；Stage A/B/C 的“零回归”以“新增参数默认不设、既有信息行/预算行不变、CI zero diff”为判据，本地 diff 必须逐条归因为 thread/path 形状。

### A 阶段门禁（Stage A 出口）

| 门禁 | 命令 | 结果 |
|---|---|---|
| 生成器 | `python tools/gen_param_catalog.py --strict-counts` → `--check` | 均 exit 0：`rows=116 x265_anchors=239 aom_anchors=192`；`catalog check OK` |
| 构建 | `python tools/env.py run -- cmake --build build/release-dev` | exit 0（`build/m8/a4-build2.log`） |
| 单测 | `ctest --test-dir build/release-dev` | **37/37 passed**（60.30 s；新增 `test_param_catalog`，`build/m8/a4-ctest.log`） |
| 金样 | `python tests/golden/smoke.py build/release-dev` | **`SMOKE total=20 pass=20 fail=0`**（`build/m8/a4-smoke.log`） |
| 回归 | `python tools/regression.py` | 本机 DIFF，归因同 K3（thread_budget 16 vs baseline 4 + buildtree 路径截断）；**无任何 M8 键/目录行进入日志**（目录尚未接入），`build/m8/a4-regression.log` |
| 格式 | `bash tools/ci-format.sh`（Git Bash 路径） | `violations=0`（197 files） |
| 零行为变化 | 目录 API 未被 `introspect_backends`/`static_formats`/UI 调用 | 代码检索 + 金样/回归一致 |

## 3. 偏差与追认

（逐条）

## 4. 未覆盖 / 未决项（如实）

（逐条）

## 5. 复现命令与证据路径

```bash
python tools/env.py run -- cmake --build build/release-dev
python tools/env.py run -- ctest --test-dir build/release-dev
python tools/env.py run -- python tests/golden/smoke.py build/release-dev
python tools/env.py run -- python tools/regression.py
python tools/env.py run -- python tests/ui_smoke.py build/release-dev
bash tools/ci-format.sh
```

证据：`build/m8/<task-id>/`（gitignored）；报告引用路径 + 关键输出行。
