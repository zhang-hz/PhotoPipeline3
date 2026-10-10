# PhotoPipeline M6 收口报告（GPS 地图升级 · W-M1–W-M6）

- **里程碑**：M6 = **GPS 地图升级**（设计/任务书 `docs/m6-map-upgrade.md` —— 该文件在库外
  （untracked），本文按节号引用；开发期裁定 `docs/m6-adjudications.md` A1–A3）。目标：把「内嵌
  地图选点」从能用升级到地图 App 水平 —— R1 搜得出 / R2 卫星图 / R3 搜索后地图内选点闭环 /
  R4 坐标基准透明可信 / R5 体验优化。
- **版本口径**：**折入 0.3.1 树，版本号不变** —— 单源 `CMakeLists.txt:19`
  `project(PhotoPipeline VERSION 0.3.1 LANGUAGES C CXX)`；`CHANGELOG.md` 的 [0.3.1] 段仍是
  「GUI 缺陷根治版」，**M6 段未写**（未决项 1，本任务面外）。
- **M6 起点提交**：`d049d22`（REL-0.3.1 收口）；下文「M6 起点..HEAD」= `d049d22..HEAD`
  （15 个提交：T1–T13 + 密钥 gitignore + T15；本报告提交 T16 为第 16 个）。
- **交付统计**：T15 收口时点（`d049d22..8aa3025`）= **25 文件，+4618 / −291**；T16 收口另含
  2 文件 +37 / −6（探针判据修正 + `ui_smoke.py` 文档段）与本文 `docs/m6-report.md`。
- **口径**：不圆场、逐字证据；凡未实测者标「未跑」。**T15 联网证据存 `build/t15-probe/`、
  T16 全量回归证据存 `build/t16-logs/`**（探针/日志按纪律放 gitignored 目录，不入库）。
  本报告的实跑证据全部来自本机（Windows / MSVC / x64-windows-avx2 / Qt 6.11.2）。

---

## 1. 交付内容（W-M1–W-M5 逐波）

| 波次 | 提交 | 交付物（文件面） | 出口（T16 复跑） |
|---|---|---|---|
| W-M1 地基：模型解耦 | `82935bf` T1、`2250e59` T2、`dfe5053` T3 | 新 `src/mapwidget/layers.{h,cpp}`：`TileStyle{Road,Satellite,Hybrid}` + `TileSource`（amap/osm/esri × 瓦片 URL 事实表 + Hybrid 标注层）；新 `src/mapwidget/geocoder.{h,cpp}`：amap/nominatim × search/suggest/reverse URL 事实表 + `parse_*`（出参一律 WGS-84，amap 分支内转 GCJ→WGS）；`src/mapwidget/providers.cpp` 降级为薄适配器（**冻结 `providers.h` 签名逐字节保留**，`search_url` 保持冻结字节）；新单测 `test_layers` / `test_geocoder` / `test_providers` | 双预设构建 exit 0；`test_layers`/`test_geocoder`/`test_providers` 全绿（§3②） |
| W-M2 地图控件能力 | `902535f` T4、`1ce6f30` T5、`c8e3f1f` T6 | `mapwidget.h` 加性（C9 追认 **A1**）：`Pin` + `set_tile_style`/`tile_style`/`set_result_pins`/`clear_result_pins` + 信号 `pin_clicked`；`mapwidget.cpp::on_paint` 瓦片管线接 `layers`（select_source 回退、Hybrid = 影像(style6)+标注(style7) 双层、缓存 key 加 layer、版权行随有效源）；结果钉绘制（实心圆+编号气泡，主钉置顶）+ 命中（最近钉 ≤13px 优先于取点） | `ui_smoke` 图层/图钉探针绿（§3②）；T15 ⑥⑦⑧⑩ 实测（§2） |
| W-M3 搜索管线与 UI | `3707d43` T7、`2d1fa6c` T8、`56e975e` T9 | `mapwidget.cpp`：`run_search` 迁 `Geocoder`（**周边偏置 around + 20 条 + `accept-language=zh` + 0 结果回退全文**）、`run_suggest`（联想）/`run_reverse`（逆地理），每类单在途/abort 旧/8s 超时/错误分类；`mapwidget.h` 加性（**A2**）：`run_suggest`/`run_reverse` + `suggest_finished`/`reverse_finished`/`search_results_ready`；版权单源 D1（layers.cpp 唯一定义处）；`page_meta.cpp` GPS 卡：图层 Seg 三态、联想框（200ms 防抖）、分类 chips（6 类目）、可点结果列表（名称/地址/距离）、地址回显行；**选点闭环**接线（搜索→多钉+列表→点钉/点行→主钉+规则框回填 WGS-84→微调→「地图选点」取中心→逆地理确认；换源清旧钉） | `meta-loop:`（选点值 = WGS-84 断言）绿；T15 ⑨⑩⑪ 实测（§2） |
| W-M4 坐标透明 + 设置 | `f2d43ed` T10、`33da0cf` T11、`8071a08`（gitignore） | 新 `src/mapwidget/dms.{h,cpp}`（`deg_to_dms`/`dms_to_deg`/`coord_precision_suspect`）+ `test_dms`；GPS 卡坐标显示行：「**WGS-84（标准 GPS）**」标签+Info 矢量图标+tooltip、DMS 切换（只显示/复制不改写入值）、复制、右键菜单、`out_of_china` 边缘带精度提示；`src/core/settings.*` 加性追加 `map_style`/`map_sat_labels`（契约 C6：默认=现行为、老 INI 缺键回落、`amap_key` 脱敏不变）+ 设置页「地图」页签；`.gitignore` 密钥防泄漏（`.zcode/secrets/` + `amap_key*` 等 14 行） | `test_dms`/`test_settings` 绿；`settings-dialog:`/`settings-geom:` 行绿（§3②） |
| W-M5 体验优化 | `f8f6b3c` T12、`34cf2c4` T13 | `mapwidget.h` 加性（**A3**）：`set_sat_labels`/`zoom_in`/`zoom_out`（clamp [kMinZoom,kMaxZoom]、中心不动）+ 信号 `cursor_moved`（WGS-84、约 100ms 节流）；`mapwidget.cpp` 左下比例尺自绘；`page_meta.cpp` 地图覆盖层：左上光标坐标 chip + 右下「－/＋」+ 右上「展开」钮；新 `src/ui/map_dialog.{h,cpp}`（「展开大地图」= **同一只 MapWidget 重挂载**（不新建第二只地图/不复制状态）+ 挂回复位 + 尺寸记忆） | `map-hud:`/`meta-map-expand:` 探针绿（14/14）；T15 ⑧ 切图层不漂（§2） |
| W-M6 验收收口 | `8aa3025` T15、**本提交 T16** | T15：真机联网验收探针（`build/t15-probe/probe.cpp` 同源编译 map chain + QtNetwork，Key 从 `.zcode/secrets/amap_key` 读入进程内存、输出全 redact）→ R1–R4 逐条 **28 PASS / FAIL 计数 = 0**；T16：全量回归（§3）+ 偏差修复轮（§4.3） | 见 §3 |

**未落地项**：T14（离线瓦片落盘持久化 / 多图 GPS 轨迹线，设计标「可选」）未做 —— 见 §5。

---

## 2. R1–R4 验收结论（依据：T15 联网实测 + T16 复跑）

**证据**：T15 探针日志 `build/t15-probe/t15-probe.log` / `run3.txt`（28 PASS，FAIL = 0）；
**本任务（T16）在收口树上用同一探针二进制复跑一次**（`build/t16-logs/live-probe.log` +
`live-probe-stdout.txt`）：**28 PASS，FAIL 计数 = 0，exit 0**；复跑前 `cmake --build
build/t15-probe/out` 报 `ninja: no work to do.` ⇒ 该二进制与当前树同源（T16 未触碰 map chain）。
另：`--ui-smoke` 离线探针（meta-loop / meta-geom / map-hud / meta-map-expand）在收口树上
全绿（§3②）。以下引文均为探针 stdout 逐字（`…` = 原样省略中间段）。

### R1 搜索（地图 App 水平）
- ① **默认源可搜出**：`[PASS] R1-① 全文搜「北京」≥15 条 | http=200 count=20 名称非空=20 地址非空=20
  类别非空=20 带距离=0 err=-`；控件侧 `count=20 名称非空=20 地址非空=20`。
  注：`place/text` 端点按设计不返回 distance（`geocoder.cpp:217`），距离面由 ② 覆盖。
- ② **周边偏置**：`[PASS] R1-② 周边偏置（近天安门搜「咖啡」）按距离 | count=20 带距离=20 升序=是
  最大距离=470m err=-`；且 `around location=116.413642,39.905603 ↔ wgs84_to_gcj02(视口中心)=
  (39.905603, 116.413642)`（逐位一致 ⇒ 请求基准正确）。
- ③ **联想**：`[PASS] R1-③ 联想 suggest 返回建议 | count=10 标题非空=10 带坐标=9 err=-`。
- ④ **0 结果回退全文**：`[PASS] R1-④ 0 结果回退全文（荒野视口搜城市名仍出结果） | count=20 err=-
  around先发=是 text回退=是`。
- ⑤ **无 Key 指引**：`[PASS] R1-⑤ 无 Key 明确指引 | search=「使用高德搜索需要先在设置中填写 Key」
  suggest=「使用高德联想需要先在设置中填写 Key」 reverse=「使用高德逆地理需要先在设置中填写 Key」
  零请求=是`（离线/无 Key 零请求）。
- **结论：R1 通过。** 免 Key 兜底（Nominatim）保留但大陆不可达 → 错误分类文案「大陆不可达，请切高德」
  （`geocoder` 错误分类面，离线探针覆盖；联网面以 ⑤ 的反面即「可达源」实测为准）。

### R2 卫星图
- ⑥ **三图层可切、取图 200**：`[PASS] R2-⑥ 三图层 URL 取图 200（Road8/Sat6/Hybrid6+7）| URL事实=是
  road:200/19152B sat:200/34621B hyb-img:200/34621B hyb-label:200/19740B`；Hybrid 同帧 =
  影像(6)+标注(7) 双层、`grab=512x384 非灰底占比=99.9%（阈值 15%）`（实拍非空）。
- ⑦ **源不支持时回退**：`[PASS] R2-⑦ osm 卫星回退 esri | osm.supports(Sat)=否 esri.supports(Sat)=是
  控件 esri 请求=6 实测取图 http=200/12902字节 URL=https://server.arcgisonline.com/…`；回退两侧同
  WGS-84 栅格基准（`osm.gcj02=0 esri.gcj02=0 amap.gcj02=1`）。
- ⑧ **切图层不漂**：三态各 `[PASS] … 中心逐位不变=是 主钉逐位不变=是`，且
  `[PASS] R2-⑧ datum 往返一致（WGS→GCJ→WGS / center_on→center_lat）| coord 往返 Δ=(0, 0)° 控件往返 Δ=(0, 0)°`。
- **记忆跨进程**：`map_style`/`map_sat_labels` 写回/读回（`test_settings` + `settings-geom:` 行）；
  大地图尺寸记忆（`meta-map-expand` 二次展开 = 上次尺寸）。
- **结论：R2 通过。**

### R3 搜索后地图内选点闭环
- ⑨ **结果 = WGS-84（解析边界）**：`[PASS] R3-⑨ 解析输出 == gcj02_to_wgs84(线上 GCJ 原始坐标) |
  条目=20 逐条一致=是`；`[PASS] R3-⑨ 选点值 = WGS-84（非 GCJ-02 原值）| 线上首条 GCJ (39.903772,
  116.415057) → 解析 WGS (39.902369, 116.408816)（相差 554.8 m）`；`search_select(0) → point_selected
  = 结果点 WGS-84`（发射次数 = 1）。
- ⑩ **点钉/列表/微调/取中心**：`[PASS] R3-⑩ 取中心回填（center_lat/lon = 选中点 WGS-84）`；
  `[PASS] R3-⑩ 点图钉 → 主钉落该点（pin_clicked 带索引）| pin_clicked=1 … 同时发 point_selected=0
  （点钉不算取点）`；`[PASS] R3-⑩ 像素几何复算：点 +64px 命中右钉（索引/落点一致）`；
  `[PASS] R3-⑩ 钉外点击回退取点（屏→WGS 复算一致）… 误命钉=-1`。
- **离线面**：`UI-SMOKE meta-loop: checked=11 bad=0`（选点值 = WGS-84 且 ≠ GCJ-02 偏移值；
  换源清钉 = 不留「点不动」的旧钉）。
- **结论：R3 通过。**

### R4 坐标基准（是否偏移加密）
- **结论：真实 WGS-84，未被加密，可直接写 EXIF**（设计 §6 的核对链路 + 下列实测）。
- ⑪ **逆地理真实地址**：`[PASS] R4-⑪ 逆地理 → 真实地址（含「北京市」）| 地址=「北京市东城区东华门街道
  商务部老年大学台基厂头条10号院」err=- 边界基准转换=是`；`regeo location=116.413642,39.905603 ↔
  wgs84_to_gcj02(点)=(39.905603, 116.413642)`（请求按该源基准）。
- ⑫ **DMS 往返/格式**：`[PASS] R4-⑫ 十进制度 ↔ DMS 换算往返（含线上选点）| 3 点 × 经纬两向，量化刻度
  0.001′≈1.85 m 内往返一致`；`[PASS] R4-⑫ DMS 格式（N 39°54.246′ 形态）| N 39°54.252' | E 39°54.252'`。
- ⑬ **边界精度提示**：`[PASS] R4-⑬ coord_precision_suspect 边界带判定 | 北京（盒内深处）=正常;
  盒北缘带内…=存疑; 盒西缘带内…=存疑; 东京（盒外）=正常; 纽约（盒外）=正常; 香港（盒内，远离边缘）=正常`
  （UI 接线 `page_meta.cpp:1333/1777` → gps_status 文案 + tooltip）。
- **WGS-84 标签**：GPS 卡坐标行前「WGS-84（标准 GPS）」标签（Info 矢量图标 + tooltip「EXIF GPS 采用
  WGS-84 标准；高德火星偏移（GCJ-02）已在内部抵消…」）——离线探针 `meta-geom: checked=21 bad=0` 覆盖。
- **结论：R4 通过。**「偏移 300–700m 只存在于显示层、被正确抵消」由 ⑨ 的 554.8 m 差值实测
  （线上 GCJ 原值 ≠ 写回值）与 `meta-loop` 的回填断言共同坐实。

---

## 3. T16 全量回归（逐条取证；命令 + 结果）

> 全部在**收口树**（本提交的工作副本，HEAD = `8aa3025` + T16 改动）上本机实跑；日志逐份存
> `build/t16-logs/`（gitignored）。构建/测试一律经 `python tools/env.py run -- ` 包装（注入
> vcvars + 仓库内 Qt/CMake/Ninja）。

| # | 检查 | 命令（逐字） | 结果 | 日志 |
|---|---|---|---|---|
| ① | 双预设构建 | `python tools/env.py run -- cmake --build --preset release` / `--preset release-dev` | **exit 0 ×2**（T16 改动后重建，见 `build-release-final.log` / `build-release-dev-final.log`） | `build/t16-logs/build-*-final.log` |
| ② | ctest 双预设 | `python tools/env.py run -- ctest --preset release --output-on-failure`；`… --preset release-dev …` | **release 32/32 = 100%**（11.44 s）；**release-dev 33/33 = 100%**（41.29 s，含 `ui_smoke` 30.12 s Passed）；含 `test_layers`/`test_geocoder`/`test_providers`/`test_dms`/`test_settings` 各 Passed | `ctest-release-final.log` / `ctest-release-dev-final.log` |
| ③ | 语料 + 金样 20 对 | `OIIOTOOL=$PWD/vcpkg_installed/x64-windows-avx2/tools/openimageio/oiiotool.exe PP_MKFIXTURES=build/release/pp_mkfixtures python tools/env.py run -- python tools/gen_corpus.py`；`python tools/env.py run -- python tests/golden/smoke.py build/release-dev` | gen_corpus **OK - 27 fixtures**（CHECKSUMS written）；`SMOKE total=20 pass=20 fail=0`（20 对断言级全过） | `gen-corpus.log` / `golden-smoke-final.log` |
| ④ | 全语料回归 | `python tools/env.py run -- python tools/regression.py`（BUILD_DIR 默认 `build/release-dev`） | `regression: zero diff (baseline reproduced)`（2406 行归一化对 2406 行基线）；exit 0 | `regression-final.log` |
| ⑤ | 格式门禁 | `bash tools/ci-format.sh` | `violations=0`（130 文件，clang-format 23.1.1 钉版）；exit 0 | `ci-format2.log` |
| ⑥ | 引擎零改动 | `git diff --stat d049d22..HEAD -- src/core src/codecs src/decode` | **按字面 = 非空（见下方裁定）**：`src/codecs`、`src/decode` 0 diff；`src/core` = `settings.cpp` +23/0、`settings.h` +23/0（**纯加性追加**） | 本报告 §4.2 |
| ⑦ | 冻结头 | `git diff --numstat d049d22..HEAD -- src/mapwidget/<h>` | `coord.h` **0/0**；`providers.h` **0/0**；`mapwidget.h` **25/0**（= A1 13 + A2 6 + A3 6，全加 0 删）；追认 A1–A3 已在 `docs/m6-adjudications.md` | 本报告 §4.1 |

**附加（本任务新增取证）**：
- **dpr≠1 复跑**：`QT_SCALE_FACTOR=2 python tools/env.py run -- python tests/ui_smoke.py
  build/release-dev` → `UI-SMOKE OK shots=12 pages=3`，exit 0（修复前该组合确定性红，见 §4.3）。
- **真机（非 offscreen）复跑**：`python tools/env.py run -- python build/t16-logs/run_real_smoke.py`
  （真实 Windows QPA，实测 dpr=2.0，`--shots .cache/ui-review`）→ `UI-SMOKE OK shots=12 pages=3`，
  exit 0；`theme-equiv+nowrap: checked=3 bad=0 equiv-pp-widgets=991`（明暗布局等价取证非空）。
  日志 `build/t16-logs/smoke-real-platform.log`。
- **冻结行逐字**：全部 ui_smoke 运行末行 = `UI-SMOKE OK shots=12 pages=3`（0.3.0 起冻结，未动）。
- **联网探针复跑**：§2 所述 28 PASS / FAIL=0（`build/t16-logs/live-probe.log`）。

**⑥ 的口径裁定（如实记录）**：设计 C8 单元格内自相矛盾 —— 同格既写「`src/core`/`src/codecs`/
`src/decode` 零改动」，又写「地图改造只在 `src/mapwidget/` + `src/ui/page_meta.*` +
**`src/core/settings.*`** + 测试」；且 §7-T11 明确把 `src/core/settings.*` 列为任务面（C6 设置加键）。
**按任务书自身口径判**：`src/codecs`/`src/decode` 零 diff；`src/core` 仅 `settings.{h,cpp}` 纯加性
追加 `map_style`/`map_sat_labels`（0 删、既有键零改动、老 INI 兼容），且 `settings.h` 内已按
M5 A28/A29 先例做「PP-FROZEN(0.3.0)【追加面登记】」注释登记。引擎行为零变化有金样 20/20 +
regression zero diff 双重背书。**字面「src/core 为空」不成立，属任务书句内矛盾，非实现偏差。**

---

## 4. 偏差与追认

### 4.1 冻结头加性追认（`docs/m6-adjudications.md` A1–A3，均已登记）
| 编号 | 内容 | diff（本机复核） |
|---|---|---|
| A1（T4） | `mapwidget.h`：`Pin`/`set_tile_style`/`tile_style`/`set_result_pins`/`clear_result_pins` + 信号 `pin_clicked` | `13/0` |
| A2（T7） | `mapwidget.h`：`run_suggest`/`run_reverse` + `suggest_finished`/`reverse_finished`/`search_results_ready`（`run_search` 行为升级为周边偏置 = R1 设计预期） | `6/0` |
| A3（T12） | `mapwidget.h`：`set_sat_labels`/`zoom_in`/`zoom_out` + 信号 `cursor_moved`（另启用 `setMouseTracking`） | `6/0` |
| 合计 | | `25/0`（**全加 0 删**；`coord.h`/`providers.h` 在整个 M6 期间 0/0） |

### 4.2 其它加性/登记面
- **`src/core/settings.{h,cpp}`（T11，C6）**：`map_style`/`map_sat_labels` 末尾追加 + `settings_pairs`/
  `apply_pair` 各两行；`settings.h` 内联「PP-FROZEN(0.3.0)【追加面登记】」注释（与 M5 A28 同口径），
  **未占 A 编号**（A1–A3 均属 mapwidget.h）。见 §3⑥ 裁定。
- **D1 版权单源（T7）**：`layers.cpp` 为版权文案唯一定义处（amap「© 高德地图」/ esri「© Esri, Maxar,
  Earthstar Geographics」），`on_paint` 硬编码删除。

### 4.3 T15 提出、T16 裁定并修复：`dialog-state-kept` 在 dpr≠1 平台恒假（探针缺陷，非产品缺陷）
- **现象（T15）**：`--ui-smoke` 真机复跑两连红：`UI-SMOKE meta-map-expand: checked=14 bad=1
  dialog-state-kept(state=1 painted=0)`（T13 自身 smoke 记录曾绿 ⇒ 首次归因未定）。
- **T16 复现与根因**：`QT_SCALE_FACTOR=2` 下离线复跑确定性复现同一失败（`smoke-dpi2-via-py.log`）。
  根因 = T13 探针判据 `shot.size() == inside->size()` 拿 `QWidget::grab()` 的**设备像素**尺寸（= 逻辑
  × dpr）去比**逻辑**尺寸 —— dpr=2（200% 缩放，本机真机态实测 dpr=2.0）时恒假、误报 `painted=0`；
  渲染本体正常（near-white 覆盖层素达标）。
- **修复（T16，`src/ui/mainwindow.cpp` 11130–11163）**：按图的 `devicePixelRatio` 换算两侧
  （尺寸比设备像素、ROI 的 96×26 保持**逻辑像素**口径），dpr=1 时与旧判据**逐位等价**；失败详情
  追加 `shot/dpr/near` 诊断字段。**探针（测试面）修正，产品行为零改动**；`tests/ui_smoke.py`
  docstring 增补 M6-T16 段（前缀清单与冻结行零改动）。
- **验证三连**：dpr=1（`smoke-fix-dpr1.log`）/ dpr=2（`smoke-fix-dpr2-final.log`）/ 真机 dpr=2
  （`smoke-real-platform.log`）—— 三者 `meta-map-expand: checked=14 bad=0` + 冻结行逐字。

### 4.4 T15 其余发现（逐条处置）
1. `.zcode/secrets/amap_key` 实际形态 = `AMAP_KEY=<32hex>`：探针已剥 `NAME=` 前缀取值（T16 复跑
   沿用同路径成功）。**运行器按纪律只读进进程内存**。
2. `notCovered`（T15 列）：真机屏幕/DPI 渲染与明暗双主题观感 —— **T16 已补**：真机非 offscreen
   `--ui-smoke` 全绿（dpr=2.0 实测）+ `theme-equiv+nowrap checked=3 bad=0`。手感的**人工目检**仍属
   人机界面复核（自动化只到像素/几何/等价层）。
3. EXIF 写入端一致性：由 `meta-loop` 离线探针承担（本次绿）；T15 探针另盖 live 数据 → 结果值 →
   回填值链路。两侧合拢 = R3⑨/R4⑪ 与 §2 结论。

### 4.5 T16 新发现（如实记录，未动作）
- **`gen_corpus.py` 重生成与入库语料存在容器字节漂移**：本机重跑（OIIOTOOL 3.1.17，avx2 工具树）
  17/27 fixture 字节与入库版不同（如 `base/rgb8.tif` 470→865 B、`meta/jxl_exif.jxl` 3193→3246 B）；
  **像素面逐份等价**（抽 8 份 `oiiotool --diff` 全 `PASS`）。与设计 R16/D2 口径一致（「同机字节稳定、
  跨机器不保证；CHECKSUMS 本地生成不入库」）——差异来自 M4 起的工具链升级。**处置 = 不把重生成字节
  入库**（仓库自 M0 起未重播种语料；REL-0.3.1 亦未动），跑完断言后已 `git checkout -- tests/golden`
  还原，工作树与 HEAD 逐字节一致；20 对金样在**入库语料**上复跑全过（§3③）。
- **⑥ 字面口径**（§3 裁定的由来）：设计 C8 句内矛盾，报告按「任务书 §7-T11 明确任务面」判。

---

## 5. 未覆盖 / 未决项（如实）

| # | 项 | 状态与理由 |
|---|---|---|
| 1 | **CHANGELOG / README 的 M6 段** | 未写（本任务面外；版本口径仍 0.3.1「GUI 缺陷根治版」）。如需发行/口径收口，属后续轮。 |
| 2 | **离线瓦片落盘持久化**（T14 可选） | **未做** —— 当前仅内存 LRU；断网只保留已浏览区（进程内）。 |
| 3 | **多图 GPS 轨迹线**（T14 可选） | **未做**（设计标 P3/可选）。 |
| 4 | **BD-09（百度）** | **未做且按设计不引入**（设计 §6「未处理但不紧急」：现无百度源；未来若加需另加 `bd09↔wgs84`）。 |
| 5 | **Nominatim/OSM 大陆可达性** | 不可达为客观事实（被墙），保留作国际/免 Key 兜底 + 「大陆不可达，请切高德」分类文案。 |
| 6 | **高德 Key 的分发** | 仍为「用户自填」（设计 X1 建议口径）：Key 不入库（§6），无 Key 时三路明确指引（R1⑤）。 |
| 7 | **真机手感/观感人工复核** | 自动化止于像素/几何/等价（§4.4-2）；人工目检未做。 |
| 8 | **高德 Web 服务 Key 配额** | 免费额度内自用；超限表现（QPS/日限）未做压力实测。 |

---

## 6. 密钥未入库确认（高德 Key）

- **`.gitignore`（提交 `8071a08`，14 行）**：`.zcode/secrets/`、`amap_key*`、`*.amap_key`、`*.amapkey`、
  `.secrets/`、`.env*`、`*secret*.key` 等；`git check-ignore -v .zcode/secrets/amap_key` →
  `.gitignore:17:.zcode/secrets/`（命中）。
- **泄漏扫描（T16 实跑，`build/t16-logs/leak-scan.txt`）**：以 `.zcode/secrets/amap_key` 的**真实值**
  （32 字符，扫描过程不落盘不打印）为针——
  - git 跟踪的全部工作树文件：**0 命中**；
  - **全部 git 历史对象（2055 个 blob/commit，`rev-list --all` + `cat-file --batch`）：0 命中**；
  - 探针/日志等未跟踪目录（`build/t15-probe`、`build/t16-logs`、`.cache`）：**0 命中**（探针输出
    全 redact，URL 里 `key=***`）。
- **名字面核对**：`git grep amap_key` 命中的全部是**键名/字段名**（`settings.*`、`settings_dialog`、
  `mapwidget.cpp`、文档），无字面值；`git grep -E "[0-9a-f]{32}"` 在 `src/tests/tools` 的命中均为无关
  常量（`versions.env` 的 VCPKG_SHA、`tools/bin/*.sha512`、`pp_verify` 自检数据）。
- **结论：高德 Key 未入库（工作树 / 历史 / 运行产物三面 0 命中；secrets 目录已被 gitignore）。**

---

## 7. 复现命令与证据路径

```bash
# 构建（①）
python tools/env.py run -- cmake --build --preset release-dev      # exit 0
python tools/env.py run -- cmake --build --preset release          # exit 0

# ctest（②；ui_smoke 仅在 release-dev 树注册）
python tools/env.py run -- ctest --preset release      --output-on-failure   # 32/32 100%
python tools/env.py run -- ctest --preset release-dev  --output-on-failure   # 33/33 100%（含 ui_smoke）

# 语料 + 金样 20 对（③；OIIOTOOL 需指向本仓 avx2 工具树，见 §4.5）
OIIOTOOL=$PWD/vcpkg_installed/x64-windows-avx2/tools/openimageio/oiiotool.exe \
  PP_MKFIXTURES=build/release/pp_mkfixtures python tools/env.py run -- python tools/gen_corpus.py
python tools/env.py run -- python tests/golden/smoke.py build/release-dev    # SMOKE total=20 pass=20 fail=0

# 全语料回归（④）
python tools/env.py run -- python tools/regression.py                        # zero diff

# 格式门禁（⑤）
bash tools/ci-format.sh                                                      # violations=0

# 引擎/冻结头（⑥⑦）
git diff --numstat d049d22..HEAD -- src/core src/codecs src/decode           # 仅 settings.{h,cpp} 各 23/0
git diff --numstat d049d22..HEAD -- src/mapwidget/coord.h \
    src/mapwidget/providers.h src/mapwidget/mapwidget.h                      # 0/0、0/0、25/0

# 探针复跑（§2 / §4.3）
python tools/env.py run -- python build/t16-logs/run_live_probe.py            # 28 PASS / FAIL=0
QT_SCALE_FACTOR=2 python tools/env.py run -- python tests/ui_smoke.py build/release-dev   # dpr=2 绿
python tools/env.py run -- python build/t16-logs/run_real_smoke.py            # 真机 dpr=2 绿

# 密钥扫描（§6）
python tools/env.py run -- python <上文 leak-scan 脚本>                        # 0 命中（日志 build/t16-logs/leak-scan.txt）
```

**证据路径**：`build/t16-logs/`（T16 全部日志：构建/ctest/金样/regression/format/泄漏扫描/三路
ui_smoke/联网探针复跑）、`build/t15-probe/`（T15 联网验收原始探针与日志）。二者均 gitignored
（探针纪律），不入库；本文引文与之一一对应。

> 落盘时 HEAD = `8aa3025` + 本任务（M6-T16）提交。
