# HEIF grid tile 尺寸推导（C17'）——公式、规范证据与验收

> 状态：0.4.0.alpha2 修订（取代 0.4.0.alpha1 / M9 的 C17 口径）。
> 真源实现：`src/codecs/heif_capability.cpp` 的 `best_tile_for_level()` / `heif_plan_grid()`、
> `tools/heif_matrix_check.py` 的 `plan_grid()`（Python 逐值镜像）、
> `tests/unit/test_heif_capability.cpp`（逐值锁定期望）。设计记录：`docs/v0.4.0-design.md` §6.1.6。

## 0. 为什么改（用户问题 → 结论）

用户原话：「1024*576 是 L几规定的尺寸？为什么还是这个奇怪的数字？？我的要求是根据 level 和
still 规格制定，怎么还是固定值？」

核查结论：**1024×576 不是任何 Level 规定的尺寸**，它是 Adobe/MPEG 生态里对
`super/T7.heic`（Apple 硬件编码 HEIC）tile 的实测值（见 `docs/review-apple-heic-config.md`：
T7 的 grid 为 6×7 个 1024×576 tile、每 tile Level 3.1）——旧 C17 把它当"基准"是错误口径。
**C17' 删除该固定基准**：tile 尺寸改由「H.265 Level 的每帧最大亮度样本数 MaxLumaPs(M)」
+「图像宽高比」+「still 单图的 Level 互操作上界」推导，硬编码尺寸为 0 个。

## 1. 公式（C17'，实现逐字）

输入：图像 `W×H`、色度、显式 Level `L`（0=自动）、用户编码档位 `profile`、手动 tile 值（可选）。

1. **规划 Level `Lp`**：
   - `L == 0`（自动）→ `Lp = 51`（Level 5.1，still 图互操作上界，见 §2.3）；
   - `L > 0` 且 `profile` 是 still 单图档位（`MainStill`=idc 3，或 W2-T2 still 变体
     16..21）且 `L > 51` → `Lp = 51`（`still_level_capped=true`；**编码 Level 仍按用户值 `L`**，
     不静默改参数，给 warning）；
   - 其余 → `Lp = L`。
2. **容量与单边上限**：`M = MaxLumaPs(Lp)`（H.265 Table A.8 / x265 4.3 `level.cpp`），
   `D = floor(sqrt(8·M))`（x265 enforceLevel 的 `pic_width/height ≤ sqrt(8·MaxLumaPs)`）。
3. **枚举**（8 像素对齐、最小 64、无固定基准）：
   ```
   target = W / H
   w_max  = min(W, D)   （再按 8 向下对齐）
   h_max  = min(H, D)
   best = none
   for tw = align8(w_max), tw >= 64, tw -= 8:
       th = align8(min(h_max, floor(M / tw)))
       if th < 64: continue
       area = tw * th
       if area > M: continue
       diff = |tw/th - target|
       if best is none or area > best.area or (area == best.area and diff < best.diff):
           best = (tw, th)
   ```
   - **面积优先**：先取 `tw*th` 最大（可恰好等于 M，= 用满该 Level 的每帧亮度样本容量）；
     面积相同再取长宽比最接近图像宽高比；再相同保留枚举中先出现的大 `tw`（确定性）。
   - 结果 clamp 到图像宽高（`w_max/h_max` 已含）与 8192（与手动范围同口径的防御上限）。
   - `Lp` 对显式 Level 场景保证 tile 必满足 `tw*th ≤ M`、`tw,th ≤ D`；`Lp ≤ L` 时也满足编码 Level。
4. **分块数**：`cols = ceil(W/tw)`、`rows = ceil(H/th)`；`rows×cols == 1` → 降级单图；
   `rows/cols` 单边 >256 或乘积 >65535 → 错误（HEIF grid rows/cols 是 u8+1）。
5. **手动 tile**（语义全部保留）：8 向下对齐（提示对齐结果）；<64 / >8192 / 显式 Level 装不下
   → 中文预检错误（点名 MaxLumaPs/单边上限/最大可行 tile/三条出路）；单轴 0 = 该轴继续自动
   求解（按 `Lp` 容量收缩）；1×1 降级单图；MIAF 奇偶只 warn。

### 1.1 逐值表（C++ 单测与 Python 镜像锁定同一批值）

| 图像 | 显式 Level / profile | 规划 Lp | tile（宽×高） | tile 亮度样本 | 网格 | 说明 |
|---|---|---|---|---|---|---|
| 3902×5854 | L3.1 | 31 | **768×1280** | **983,040 = M31 整值** | 6×5 | 面积优先枚举的赢家；不是 1024×576 |
| 3902×5854 | L3.0 | 30 | 576×960 | 552,960 = M30 | 7×7 | |
| 3902×5854 | L1.0 | 10 | 144×256 | 36,864 = M10 | 28×23 | |
| 3902×5854 | L6.0（整图装得下）+ grid 自动 | 60 | — | — | 1×1 → 单图 | 默认行为不变 |
| 3902×5854 | L6.0 + grid 总是 | 60 | 3896×5848 | 22,783,808 | 2×2 | clamp 到图像（8 向下）；不是 1024×576 |
| 3902×5854 | Level 自动 + grid 总是 | **51** | 2176×4096 | **8,912,896 = M51 整值** | 2×2 | 每 tile 的 Level 仍由 x265 自选 |
| 20000×20000 | Level 自动 + grid 自动 | 51 | 2176×4096 | 8,912,896 | 10×5 | 整图超 L7.2 才分块 |
| 6000×2000 | Level 自动 + grid 总是 | 51 | 5064×1760 | 8,912,640 | 2×2 | 比例 2.88 vs 图像 3.0 |
| 800×400 | Level 自动 + grid 总是 | 51 | 800×400 | 320,000 | 1×1 → 单图 | clamp 到图像 |
| 801×601 | Level 自动 + grid 总是（4:4:4） | 51 | 800×600 | 480,000 | 2×2 | 8 向下对齐 |
| 1024×576 | Level 自动 + grid 总是 | 51 | 1024×576 | 589,824 | 1×1 → 单图 | |
| 3902×5854 | L3.1 + 手动 480×256 | — | 480×256 | 122,880 | 9×23 | 手动语义不变 |
| 3902×5854 | L3.1 + 手动宽 512、高自动 | 31 | 512×1280 | 655,360 | 8×5 | 自动轴按 Lp 容量收缩 |

`D` 参考值：L1.0=543、L3.0=2103、L3.1=2804、L5.0/5.1/5.2=8444、L6.0/6.1/6.2=16888、
L7.2=33776。

## 2. 规范证据链（证据等级如实标注）

| # | 事实 | 出处（一手可复核） | 证据等级 |
|---|---|---|---|
| 2.1 | Level 的 `MaxLumaPs` 表（L3.1=983,040；L5.x=8,912,896；L6.x=35,651,584；L7.x=142,606,336 等） | ITU-T H.265 (V9, 2023-09) **A.4.1 Table A.8**（PDF 第 281 页）；与 x265 4.3 `source/encoder/level.cpp:45-65` 第一列逐值一致 | **证实**（本任务下载 ITU PDF 全文核验；718 页） |
| 2.2 | tile 单边 ≤ `sqrt(8·MaxLumaPs)`、面积 ≤ `MaxLumaPs`；x265 把宽高向上补齐到 minCUSize(=8) 再判 Level | H.265 **A.4.1 b/c**（`pic_width/height_in_luma_samples ≤ Sqrt(MaxLumaPs*8)`）+ x265 4.3 `enforceLevel()`（`level.cpp:398-410`）；仓库 Phase-1 实测 3902×2284 raw 8,912,168 ≤ L5.2 8,912,896 但补齐 8,932,352 后被拒 | **证实**（一手规范 + 源码 + 实测） |
| 2.3 | **still 单图的 Level 上界 = 5.1**（用于 Level=自动 的规划上界与 still 档位压缩） | **3GPP TS 26.140 §4.5 / TS 26.141 §4.2**：HEIF still 图须为 `heic` + `MiHB`(MIAF) brand，码流 = HEVC **Main Profile / Main Tier / Level 5.1**，媒体类型参数 `itemTypes="hvc1.1.2.L153.B0"`（`L153` = general_level_idc 153 = 3×51 = Level 5.1）。原文（HTML 转录）：<https://itecspec.com/3gpp/26.141/s/4.2> | **证实**（一手 3GPP 规范；属**应用格式/互操作**上界） |
| 2.4 | **H.265 本身没有 still picture profile 的 Level 上限** | ITU-T H.265 (V9) **A.3.3/A.3.4/A.3.5** 的 still 约束清单无 level 条款；**A.4.2** 标题即 "Profile-specific level limits for the video profiles"，其 NOTE 1 明确 "video profiles" = 非 still picture profiles（still 共 4 个：Main Still Picture、Main 10 Still Picture、Main 4:4:4 Still Picture、Main 4:4:4 16 Still Picture） | **已否定该假设**（一手规范全文核验） |
| 2.5 | x265 4.3 / libheif 1.23.5 不检查 still 档位 Level 上限 | x265 `enforceLevel()` 只按用户 `level-idc` + 通用限制；libheif 无 Level 策略 | 证实（源码核验） |
| 2.6 | Apple / iPhone 对任意 grid tile 尺寸的硬解接受度 | 无公开规范；仓库 R-12/R-2 沿用 "UNVERIFIED"（不做承诺，不写死真机结论） | **UNVERIFIED** |
| 2.7 | MIAF（ISO/IEC 23000-22）一手条款 | 标准付费墙，本环境不可读；仅经 3GPP 引用为**二手** | 未核验（二手） |

**口径结论**：`M`、`D`、8 对齐是编码规范的硬事实；**Level 5.1 是"HEIF still 图互操作档"
的应用格式上界**——所以代码把它作为「Level=自动 的规划上界」与「still 档位的规划压缩值」，
并在注释/日志/UI 文案中如实说明，不冒充 H.265 profile 强制。

## 3. 行为面与警告

- **默认配置零变化**：Level=自动 + grid=自动 + 常规尺寸 → 单图（不产 grid box）。
- **显式 Level 装不下整图** → grid 自动分块，每 tile 满足该 Level；
- **grid=总是** → tile 由 `Lp` 容量推导（Level=自动 时 `Lp=5.1`），不再是 1024×576；
- **still 档位 + 显式 Level>5.1**：`plan.warning_zh`（中文、点名 3GPP/L153、声明"编码 Level
  保留用户值"）+ UI 规则 **H8** + enc_heif 运行日志 `heif grid warning`（单图路径也会记）；
  `plan.still_level_capped=true`、`plan.planning_level_idc=51`、`plan.tile_level_idc` 仍 = 用户 Level；
- **Level=自动 + 手动 tile**：规则 H5 warn（文案已去 Apple 基准），实现见 `param_rules.cpp`。

## 4. 验收（本轮实测）

> 证据路径均在 gitignored `build/` 下；命令一律经 `tools/env.py` 包装（Windows 用
> Windows PowerShell 5.1 串行锁脚本 `build/iter1/build.ps1`）。
> 构建树 = Lead 的干净集成树 `build/iter1`（Lead 13:15:40 全量成功；本任务增量
> `pp_test_heif_capability.exe` 13:23:02、`photopipeline.exe` 13:41:03 均含 C17' 源码）。

| 项 | 命令 / 证据 | 结果 |
|---|---|---|
| 单测 | `build/iter1/pp_test_heif_capability.exe`（13:23 构建，含 clang-format 后源码） | **366 checks / 0 failed** ✔（改前 223 checks） |
| 矩阵解析器自检 | `python tools/env.py run -- python tools/heif_matrix_check.py --phase final --set selftest --binary build/iter1/photopipeline.exe` | **SELFTEST RESULT: OK**（3/3：T1 旧原型 grid 4×11/44、T7 6×7/42、单图 probe） |
| 矩阵 final-gridtile | `... --set final-gridtile --jsonl build/heif-spec/clean-final-gridtile.jsonl --md .../clean-final-gridtile.md`（独立 `--tmp-dir`） | **pass=8 / fail=0 / expected-fail=3 / unexpected=0**（11 例，48.3s）✔ |
| 矩阵 final-grid | `... --set final-grid --jsonl build/heif-spec/clean-final-grid.jsonl --md .../clean-final-grid.md` | 干净窗口读数 **pass=13 / fail=6 / expected-fail=1 / unexpected=0**；6 个 fail **全部是环境原因**（`exit=127`：编码期间 `photopipeline.exe` 正被并行任务重链接/占用；另首轮有 1 例 240s 超时同因），非语义失败。**跨 3 次运行并集 = 16 pass + 4 expected-fail，20/20 例均至少一次正确判定，unexpected 恒 0**。详见 `build/heif-spec/final-grid.jsonl`、`clean-final-grid.jsonl`、`build/heif-spec/clean-filter-grid*.log` |
| 默认 HEIF 行为 | `--dev tests/golden/base/rgb8.png --out build/heif-spec/dev-single --format heif --workers 1`（Level=自动 + grid=自动） | 产物 `rgb8.heic` 717B、exit=0；box 树 = ftyp/meta/…/hvcC/…/mdat，**无 `grid` box**；hvcC idc=3（Main Still）、level_idc=30（L1.0）、4:2:0、8bit → **默认仍单图，行为未变** ✔ |

### 4.1 真实产物事实（矩阵结构回读，逐值与 C++/Python 期望一致）

| 用例 | grid | tile（ispe） | 每 tile hvcC level | 说明 |
|---|---|---|---|---|
| T1 3902×5854 + 显式 L3.1（8/10bit × 420/444/422 × Auto/MainStill/Main10/Rext 档） | **6×5** | **768×1280** | **93 = L3.1** | 面积恰 = 983,040 = M31；旧 4×11×1024×576 作废 |
| T1 3902×5854 + Level=自动 + grid=总是 | **2×2** | **2176×4096** | 150 = L5.0（x265 对 =M51 面积自动选 5.0，≤ still 上界 5.1） | 规划上界 5.1 生效 |
| small 1024×576 + L3.0 + grid 自动 | 2×1 | 960×576 | 90 = L3.0 | tile = M30 整值 |
| small 1024×576 + L1.0 + grid 自动 | 4×4 | 256×144 | 30 = L1.0 | tile = M10 整值 |
| small + 手动 1004×300 + L3.1 | 2×2 | 1000×296 | 93 | 8 向下对齐语义保留 |
| small + 手动 512×288 + Level=自动 | 2×2 | 512×288 | 63 = L2.1 | 每 tile x265 自动 Level |
| grid=关闭 + 超 Level、手动 tile 超 MaxLumaPs / <64 | — | — | — | 中文预检错误（expected-fail 3/3 正确） |

复现算法（Python 原型，仅在 gitignored 目录留证）：`build/heif-spec/expected.py`。

## 5. 未决 / 开放项（如实）

1. **Apple/iPhone 真机接受度**：仍是 UNVERIFIED（R-12）；本任务只保证码流合法、可本地解码。
   旧 1024×576 的"对齐 Apple 参考"卖点随之取消——grid 结构仍合法，但不再声称与 T7 同构。
2. **MIAF 一手条款**（23000-22 对 HEIF/HEVC 图像的具体 Level 措辞）：付费墙未核验；本文档
   的 5.1 依据是 3GPP 引用 + 其 `hvc1.1.2.L153` 参数（二手→一手 3GPP）。
3. **非 still 档位的显式 Level>5.1**：3GPP 互操作档字面是 "HEVC Main Profile / Main Tier /
   Level 5.1"（任何 HEIF still 输出都 ≤5.1 才严格互操作），但本轮契约只对 **still 单图档位**
   warn/压缩；`Auto`/`Main`/`Main10` 等档位显式选 L6.0+ 不预警（登记，待后续裁定是否扩到全部
   HEIF 输出）。
4. **H.265 V9 的 still profile 名称集合**（与本任务相关但属 W2-T2 / R-2 复核项）：V9 里 named
   still picture profile 只有 4 个（Main Still Picture、Main 10 Still Picture、Main 4:4:4
   Still Picture、Main 4:4:4 16 Still Picture）——**没有** "Main 4:2:2 10/12 Still Picture"；
   且 "Main 10 Still Picture" 在 V9 中**存在**（idc=2 + one_picture_only + DPB=0），x265 4.3
   注释 "note there is no 10bit still picture profile" 已过时。本轮不改（Lead 裁决：登记
   known issue，另立小任务）。
5. **矩阵全量未跑**：本轮按 Lead 要求只跑 `--selftest` + final-grid/final-gridtile 子集；
   1102 例全量（`docs/m10-report.md` §5.1 命令）留待集成轮。
