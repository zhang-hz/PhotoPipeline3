# PhotoPipeline M6（GPS 地图升级）设计 + 开发计划任务书 v1.0

> 目标：把「内嵌地图选点」从**能用**升级到**地图 App 水平** —— 搜索能搜出东西、可切卫星图、
> 搜索后地图内选点闭环、坐标基准透明可信；并补一批体验优化。
> 现状基线：0.3.0（M5 GUI 刷新 v6 已收口）。地图链路见 `src/mapwidget/`（coord/providers/mapwidget）
> + `src/ui/page_meta.cpp`（GPS 卡接入）+ `src/core/settings.*`（持久档）。
> 开发组织（沿用 D21 + 用户新规）：**架构/设计/接口由主对话定稿**；开发任务经 dynamic workflow 下发；
> 子代理禁止再派发子代理；主对话零开发动作（编排/设计/评审/闸门裁定除外）；开发全程不问询、不停工。

---

## 0. 设计权威链

1. 本文 §3 架构契约（**主对话定稿，子代理不得擅自变更**）；
2. `docs/m1b-tasks.md` §2.3–§2.5（coord/providers/mapwidget 的 **PP-FROZEN** 头文件与 URL 事实表）——
   **冻结接口改动必须走 §3.C9「追认」流程**，不得直接改签名；
3. `docs/qt-gui-design-spec.md`（v6 单源规范）+ `docs/mockups-v2/`（视觉终稿）——新增控件沿用 v6 表单体系
   （formkit R1–R4、令牌单源 theme.h、矢量图标）；GPS 卡视觉只做**加性**扩展；
4. 本文 §7 验收清单 —— 每条落地为 `--ui-smoke` 可断言探针 + 截图证据。

---

## 1. 需求映射（用户 5 条 → 设计落点）

| # | 用户需求 | 落点（本文节） | 优先级 |
|---|---|---|---|
| R1 | 搜索达地图 App 水平（当前搜不出） | §4.2 地理解耦 + §4.3 搜索管线 + §5.1 搜索 UI | **P0** |
| R2 | 切换卫星地图 | §4.1 瓦片图层模型 + §5.2 图层切换 | **P0** |
| R3 | 搜索后地图内选点 | §4.4 多图钉 + §5.3 选点闭环 | **P1** |
| R4 | 确认选点准确、是否偏移加密 | §4.5 坐标基准透明化 + §6 | **P1** |
| R5 | 更多优化 | §8 | P2–P3 |

---

## 2. 现状诊断（结论已实测）

### 2.1 「搜不出来」三层根因（实测网络源）
```
Nominatim/OSM   → SSL 握手失败，不可达（大陆 OSM 系被墙）  ← 首要根因
高德道路/卫星瓦片 → http 200 ✓
高德联想/逆地理  → 端点存在，仅缺 Key（INVALID_USER_KEY）
```
1. **默认源 OSM，而 OSM 系大陆不可达**（`settings.h:75` 默认 `map_provider="osm"`；搜索走
   `nominatim.openstreetmap.org`，`providers.cpp:104`）→ `run_search` 报「搜索请求失败」。
2. **高德搜索可达但需 Key**，未填 → `mapwidget.cpp:682` 提示「使用高德搜索需要先在设置中填写 Key」。
3. **即便 Nominatim 通，质量不到地图 App**：单 `q=` 全文、`limit=6`、无 `accept-language=zh`、
   无联想/周边偏置/分类/逆地理；UA `PhotoPipeline/0.1 (...; dev)`（`providers.cpp:37`）偏泛易 403。

### 2.2 卫星图：完全缺位，但高德已支持（实测 200）
当前 `providers.cpp:88` 仅高德 `style=8`（矢量路网）。高德另有 **`style=6` 卫星影像 + `style=7` 路网标注叠加**，均实测可达。**纯增量改造。**

### 2.3 选点：单点有，闭环缺
现有：点击地图选点 ✓、「地图选点」取中心 ✓、搜索结果下拉 `search_select` 居中+钉 ✓。
缺：搜索后只钉 1 个结果，**多结果不能同时画成可点图钉**、无「搜→钉→微调」连贯流。

### 2.4 坐标基准：**已正确，未被加密**（详见 §6）
EXIF GPS 标准强制 WGS-84；公共 API 全程 WGS-84；GCJ-02 偏移仅在显示层被正确反向抵消。单测往返容差 `1e-6°`(≈0.1m)。唯一注意：`out_of_china` 粗包围盒在港澳台/边境边缘有几十米级局部误差。

---

## 3. 架构契约（主对话定稿，子代理不得擅自变更）

| # | 契约 | 内容 |
|---|---|---|
| **C1** | **瓦片源 / 地理解码解耦** | 拆 `TileProvider`（现把 tile+search+datum 捆一起）为两个正交维度：`TileSource`（路网/卫星图层、raster 基准）与 `Geocoder`（搜索/联想/逆地理）。二者可自由组合（如高德搜 + Esri 卫星）。**新文件承载，冻结的 `providers.h` 公共签名逐字节保留**（旧 `TileProvider` 降级为薄适配器，编译面零破坏）。 |
| **C2** | **图层模型** | `enum class TileStyle { Road, Satellite, Hybrid }`（Hybrid = 影像 + 标注叠加双层绘制）。`TileSource::tile_url(z,x,y,style)` 统一出口；缓存 key 加 `style` 维度，LRU/字节预算逻辑复用。 |
| **C3** | **坐标基准边界不变** | 公共坐标**一律 WGS-84**（EXIF 合规）；GCJ-02 只在 `mapwidget` 边界转换，规则与现状完全一致。新增能力（联想/逆地理/卫星）沿用同一转换边界。 |
| **C4** | **WGS-84 语义对外可见** | GPS 卡显式标注「WGS-84（标准 GPS）」；选点后回显**地址 + 坐标**供肉眼核对；`out_of_china` 边界地带给精度提示；坐标支持 十进制度 ↔ DMS 切换/复制。消除「是否被加密」的用户疑虑。 |
| **C5** | **搜索管线异步 + 可取消 + 防抖** | `run_search`（全文/周边）、`suggest`（联想，输入 200ms 防抖）、`reverse`（逆地理）三类请求；复用现有 `abort_search`/`QNetworkAccessManager`；请求去重、超时 8s、失败分类提示（被墙 / 无 Key / 无结果 / 解析失败）。 |
| **C6** | **设置加键规则**（沿用 M5-C6） | 新增 `map_tile_source` / `map_geocoder` / `map_style` / `map_sat_labels`；默认值=现行为；`test_settings` 随码扩展；老 INI 可读（缺键回落默认）；`amap_key` 脱敏规则不变。 |
| **C7** | **UI 只加性扩展** | GPS 卡新增控件全部走 formkit + theme 令牌 + 矢量图标，无字形图标；冻结 `pp-*` objectName 全保留；`ui_smoke` 冻结行 `UI-SMOKE OK shots=…` 逐字不动（新增截图同名单重渲）。 |
| **C8** | **引擎零改动** | `src/core`/`src/codecs`/`src/decode` 零改动。金样 20 对断言零改动。地图改造只在 `src/mapwidget/` + `src/ui/page_meta.*` + `src/core/settings.*` + 测试。 |
| **C9** | **冻结接口「追认」流程** | `coord.h`/`providers.h`/`mapwidget.h` 为 PP-FROZEN。**新增能力尽量走新文件**；确需给冻结头加**加性**声明（如 `MapWidget::set_tile_style`/`set_result_pins`）时，走「加性 + 追认」：表外新增行、不改既有签名，任务提交注明、入 `m6-adjudications.md`（先例：M4-T12 `center_lat/lon` 加性只读回读）。 |
| **C10** | **测试机械化** | 每项新能力在 `--ui-smoke` 内加几何/行为探针（图层可切、多图钉命中、联想防抖、逆地理回显、WGS-84 标注存在）；`tests/unit/` 补 geocoder 解析、图层 URL、DMS 换算纯函数单测。 |

---

## 4. 详细设计

### 4.1 瓦片图层模型（R2 卫星图）

**新文件 `src/mapwidget/layers.h`（非冻结）：**
```cpp
namespace pp::map {

enum class TileStyle { Road, Satellite, Hybrid }; // Hybrid=影像+标注叠加

struct TileSource {
    QString id;          // "amap" | "osm" | "esri"
    QString label;       // "高德地图" | "OpenStreetMap" | "Esri 影像"
    bool gcj02;          // 栅格基准：amap=true(GCJ-02)；osm/esri=false(WGS-84)
    QString attribution; // 版权行
    bool supports(TileStyle) const;
    QUrl tile_url(int z, int x, int y, TileStyle) const;
};

const TileSource *tile_source_by_id(const QString &id);
QStringList tile_source_ids();
QStringList styles_for(const QString &source_id);  // 该源支持的图层
}
```

**瓦片 URL 事实表（新增，随实现冻结）：**

| 源 | 图层 | URL | 基准 |
|---|---|---|---|
| amap | Road | `https://webrd0{n}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}`（n∈1..4 轮换） | GCJ-02 |
| amap | Satellite | `https://webst0{n}.is.autonavi.com/appmaptile?style=6&x={x}&y={y}&z={z}` | GCJ-02 |
| amap | Hybrid 标注层 | `https://webst0{n}.is.autonavi.com/appmaptile?style=7&x={x}&y={y}&z={z}`（叠在 style=6 上） | GCJ-02 |
| osm | Road | `https://tile.openstreetmap.org/{z}/{x}/{y}.png`（带 UA） | WGS-84 |
| esri | Satellite/Hybrid | `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}`（**注意 y/x 顺序**；Hybrid 用其 reference 层） | WGS-84 |

**绘制（`mapwidget.cpp::on_paint` 改造）**：`Hybrid` 先画影像 tile 再画标注 tile（同 key、`style` 不同）；缓存 key = `{source, style, z, x, y}`。卫星底图偏暗，版权行保留白底 α190 可读。

### 4.2 地理解码解耦（R1 根因）

**新文件 `src/mapwidget/geocoder.h`（非冻结）：**
```cpp
namespace pp::map {

struct GeocodeResult {
    QString title;      // POI 名
    QString address;    // 结构化地址（省市区…）
    double lat = 0, lon = 0; // WGS-84（解析边界已转换）
    QString type;       // POI 类别
    double distance_m = -1;  // 周边搜距离，-1=未知
};
struct SuggestItem { QString title; QString address; double lat, lon; bool has_point; };

struct Geocoder {
    QString id;          // "amap" | "nominatim"
    QString label;
    bool requires_key;   // amap=true；nominatim=false
    bool gcj02;          // 返回值基准：amap=true；nominatim=false（转换在解析边界）
    QUrl search_url(const QString &q, const QString &key,
                    std::optional<std::pair<double,double>> near /*WGS-84*/) const;
    QUrl suggest_url(const QString &q, const QString &key) const;
    QUrl reverse_url(double lat, double lon, const QString &key) const; // lat/lon=该源基准
};
const Geocoder *geocoder_by_id(const QString &id);
QStringList geocoder_ids();

// 解析：出参 WGS-84；amap 分支内部 gcj02_to_wgs84（同现状 providers.cpp:166）
QString parse_search (const Geocoder&, const QByteArray&, std::vector<GeocodeResult> &out);
QString parse_suggest(const Geocoder&, const QByteArray&, std::vector<SuggestItem> &out);
QString parse_reverse(const Geocoder&, const QByteArray&, QString &address_out);
}
```

**端点事实表（新增，随实现冻结）：**

| 用途 | amap（GCJ-02，需 Key） | nominatim（WGS-84，免 Key，大陆被墙） |
|---|---|---|
| 全文搜 | `/v3/place/text?key=&keywords=&city=&offset=20&page=1&extensions=all` | `/search?format=json&limit=20&accept-language=zh&q=` |
| 周边搜 | `/v3/place/around?key=&location=lng,lat&radius=&keywords=&offset=20&extensions=all` | （Nominatim 无 around；用 `viewbox`+`bounded` 偏置） |
| 联想 | `/v3/assistant/inputtips?key=&keywords=&city=&datatype=poi` | 无（用本地最近 N 条 + 前缀） |
| 逆地理 | `/v3/geocode/regeo?key=&location=lng,lat&extensions=all` | `/reverse?format=json&lat=&lon=&accept-language=zh` |

**默认源策略**：`map_geocoder` 默认 **amap**（大陆可达）；`map_tile_source` 默认 **amap**；OSM/Esri 作国际/免 Key 兜底。Nominatim 保留但标注「大陆不可达」。

**UA 修正**：`PhotoPipeline/{ver} (batch transcoder; +<contact>)`，满足 Nominatim 政策并体现产品身份。

### 4.3 搜索管线（R1 地图 App 水平）

- **联想** `suggest(q)`：输入 200ms 防抖触发 → `suggest_url` → 下拉建议（POI+地址）；选建议=直接定位。
- **搜索** `run_search(q, near)`：优先 **周边偏置**（视口中心 `near`，radius 随 zoom），回退全文；`offset=20`；
  结果含 名称/地址/类别/距离；按距离或相关度排序。
- **分类 POI**：预置 chips（餐饮/酒店/加油站/景点/商场/地铁…）→ 传 `keywords/typecode` 一键搜。
- **逆地理** `reverse(lat,lon)`：选点后回显地址到 GPS 卡。
- **健壮性**：请求去重、`abort_search` 取消旧的、超时 8s、错误分类文案（被墙→「该源大陆不可达，请切高德」；无 Key→「请在设置填高德 Key」；0 结果→「无匹配」；解析失败→详情）。

### 4.4 多图钉选点闭环（R3）

**`MapWidget` 加性扩展（C9 追认）：**
```cpp
// 加性（表外新增，不改既有签名）
struct Pin { double lat, lon; /*WGS-84*/ QString label; int index; };
void set_result_pins(const std::vector<Pin>&); // 搜索结果多钉
void clear_result_pins();
void set_tile_style(TileStyle);
TileStyle tile_style() const;
// signals（加性）
void pin_clicked(int index);
void suggest_finished(const std::vector<SuggestItem>&, const QString &error);
void reverse_finished(const QString &address, const QString &error);
```
- **多钉绘制**：`on_paint` 在主图钉之外画结果钉（编号气泡）；点击命中检测 → `pin_clicked(i)` → 选中该结果。
- **闭环**：搜索 → 结果钉铺地图 + 列表（带地址/距离）→ 点图钉或列表项选中 → 主图钉落到该点 →
  允许继续**点地图微调** → 「地图选点」取中心/最终点击点 → 回显地址确认。
- **命中检测**：pimpl 里对最近结果钉做距离阈值判定（复用 `EventFilter`，不碰冻结事件声明）。

### 4.5 坐标基准透明化（R4）

见 §3.C4 + §6。落地 UI：
- GPS 卡坐标行旁固定小标签「**WGS-84（标准 GPS）**」（矢量图标 + tooltip 说明火星偏移已在内部抵消）。
- 选点后 gps_status 显示「已选点 · 逆地理地址」。
- `out_of_china` 边界地带（港澳台/边境）显示「该区域坐标精度存疑」提示。
- 坐标框支持 右键菜单/按钮：十进制度 ↔ DMS ↔ 复制；显示不写入。

---

## 5. UI 设计（page_meta.cpp GPS 卡，v6 加性）

现有布局自上而下：`操作行(地图选点/清除/…)` → `地图源下拉` → `地点搜索` → `搜索结果下拉` → `地图(高196)` → `批量GPS规则`。改造后：

### 5.1 搜索区（R1）
```
[ 图层: (矢量|卫星|混合) seg ]  [ 地图源下拉(可选) ]
[ 地点搜索: ________________ [搜索🔍] ]
[ 分类 chips: 🍽餐饮 🏨酒店 ⛽加油 🏞景点 🚇地铁 … ]
[ 搜索结果(可点列表，带地址/距离/类别，滚动) ]   ← 替换纯文本下拉
```
- 搜索框带**联想下拉**（防抖）；分类 chips 单选即时搜；结果列表可点选、悬停高亮对应图钉。

### 5.2 图层切换（R2）
- `Seg`（formkit）三态：矢量 / 卫星 / 混合；切换即 `set_tile_style`；记忆跨进程（`map_style`）。
- 卫星/混合下若源不支持 → 自动回退并 tooltip 说明（如 OSM 无卫星→Esri）。

### 5.3 地图与选点（R3）
```
[ 地图(高≥196，可加比例尺/±缩放) ]   ← 多结果图钉 + 主图钉
[ 状态行: 已选点 · <逆地理地址> · WGS-84 ]
```
- 「地图选点」按钮语义不变（取中心）；点击地图取点不变；新增点图钉取结果点。
- 比例尺 + 缩放 `±`（右下悬浮，沿用预览缩放条样式）。

### 5.4 坐标显示（R4）
- 经纬度框保持 6 位小数；右侧小标签「WGS-84」；右键菜单 DMS/复制。

---

## 6. 坐标基准与精度（R4 事实核对，回应「是否偏移加密」）

**结论：选点/搜索结果是真实 WGS-84，未被加密，可直接写 EXIF。** 核对链路：

| 路径 | 基准流 | 写 EXIF 前 |
|---|---|---|
| 点击地图 | 屏幕→world→datum(GCJ if amap)→**datum_to_wgs**→`point_selected` | WGS-84 ✓ |
| 搜索选点 | amap 返回 GCJ→**parse 内 gcj02_to_wgs84**→`search_select` | WGS-84 ✓ |
| 「地图选点」取中心 | `center_lat/lon`（内部 datum→WGS）| WGS-84 ✓ |
| OSM/Nominatim | 本就 WGS-84，无偏移 | WGS-84 ✓ |

- **EXIF GPS 依标准强制 WGS-84**（`metadata.h:185 write_gps` 写 `GPSLatitude/Ref`）；公共 API 全程 WGS-84（`mapwidget.h:22`）。
- **精度**：GCJ-02 逆变换不动点迭代收敛 `1e-13°`（<0.1mm）；单测往返容差 `1e-6°`(≈0.1m)。原始火星偏移 300–700m **仅存在于显示层，被正确抵消，不入 EXIF**。
- **唯一边界**：`coord.cpp:28 out_of_china` 粗矩形（72–137.8°E, 0.8–55.8°N）；港澳台/边境边缘偏移归零/应用不一致，可致几十米级误差 → UI 提示（§4.5）。
- **未处理但不紧急**：BD-09（百度）第三套偏移未支持；当前无百度源，不引入。若未来加百度源，需另加 `bd09↔wgs84`。

---

## 7. 波次与任务（串行；每任务一提交 `M6-T<n>: …`，SPDX 头，clang-format 过）

### W-M1 地基：模型解耦（不动 UI）
| # | 任务 | 文件面 | 出口 |
|---|---|---|---|
| T1 | `layers.h` 瓦片图层模型 + `TileSource`（amap/osm/esri，Road/Satellite/Hybrid URL 事实表）+ `layers.cpp`；单测：URL 逐字节、style 能力矩阵 | `src/mapwidget/layers.{h,cpp}` 新建、`tests/unit/test_layers.cpp` 新建 | build 双预设 + 新单测绿 + 旧编译面零破坏 |
| T2 | `geocoder.h` 地理解码模型 + amap/nominatim `search/suggest/reverse` URL + `parse_*`（WGS-84 出参、amap 内转 GCJ）；单测：解析回退、GCJ→WGS、错误分类 | `src/mapwidget/geocoder.{h,cpp}` 新建、`tests/unit/test_geocoder.cpp` 新建 | 同上 + parse 单测绿 |
| T3 | `providers.*` 降级为薄适配器（`TileProvider`/`parse_search`/`search_url` 委托到新模型），**冻结 `providers.h` 签名逐字节不变**；老单测/调用点全绿 | `src/mapwidget/providers.cpp`（h 不动） | 冻结头 diff=空 + ctest 全绿 |

### W-M2 地图控件能力（R2/R3 核心）
| # | 任务 | 文件面 | 出口 |
|---|---|---|---|
| T4 | `mapwidget.h` 加性扩展（C9 追认）：`set_tile_style`/`set_result_pins`/`clear_result_pins`/`pin_clicked`/`suggest_finished`/`reverse_finished`；入 `m6-adjudications.md` | `mapwidget.h`（表外加性）、`mapwidget.cpp` | 追认登记 + build 绿 |
| T5 | 图层绘制：Hybrid 双层（影像+标注）、缓存 key 加 style、卫星版权行、图层切换 datum 往返不漂 | `mapwidget.cpp::on_paint` 等 | ui-smoke 图层探针（三态各 grab 非空）+ 截图 |
| T6 | 多图钉绘制 + 命中检测（pimpl `EventFilter`）→ `pin_clicked`；主图钉共存 | `mapwidget.cpp` | 探针：N 钉渲染 + 点选命中 |

### W-M3 搜索管线与 UI（R1/R3）
| # | 任务 | 文件面 | 出口 |
|---|---|---|---|
| T7 | `MapWidget` 搜索升级：`suggest`（防抖）/ `run_search(near)` 周边偏置 / `reverse`；请求去重/取消/错误分类 | `mapwidget.cpp` + `geocoder` 接线 | 探针：联想/周边/逆地理可触发（offline mock） |
| T8 | GPS 卡 UI：图层 Seg + 搜索联想框 + 分类 chips + 可点结果列表 + 地址回显行；formkit/令牌/矢量图标，冻结 objectName 保留 | `src/ui/page_meta.cpp` | 闸门 + gps 卡探针 + 冻结行逐字 |
| T9 | 选点闭环接线：搜索→多钉+列表→点钉/列表选中→主钉→微调→取中心→逆地理确认 | `page_meta.cpp` + `mapwidget` 信号 | 探针：闭环走通（选点值=WGS-84 断言） |

### W-M4 坐标透明 + 设置（R4）
| # | 任务 | 文件面 | 出口 |
|---|---|---|---|
| T10 | WGS-84 标签 + DMS 换算/复制 + 边界精度提示；`dms` 纯函数单测 | `page_meta.cpp`、`tests/unit/test_dms.cpp` | 探针：WGS-84 标签存在 + DMS 往返绿 |
| T11 | 设置加键（C6）：`map_tile_source/map_geocoder/map_style/map_sat_labels`，默认=现行为，老 INI 兼容，脱敏不变，设置页「地图」页签加图层/源/Key | `src/core/settings.*`、`settings_dialog.cpp`、`test_settings.cpp` | 设置往返绿 + test_settings 扩展绿 |

### W-M5 体验优化（R5，P2–P3）
| # | 任务 | 文件面 | 出口 |
|---|---|---|---|
| T12 | 缩放 `±` + 比例尺 + 光标坐标实时回显 | `mapwidget`/`page_meta` | 探针：缩放/比例尺存在 |
| T13 | 「展开大地图」弹窗模式（同控件复用）+ 图层/源跨进程记忆 | `page_meta`/`mainwindow` | 探针 + 记忆往返 |
| T14 | 离线瓦片落盘持久化（可选，突破内存 LRU）+ 多图 GPS 轨迹线（可选） | `mapwidget` | 探针 + 断网可看已浏览区 |

### W-M6 验收收口
| # | 任务 | 出口 |
|---|---|---|
| T15 | §8 验收清单逐条核验（探针+截图，明暗成对） | 逐条 PASS 行 + 偏差清单 |
| T16 | 偏差修复轮 + 全量回归 | ctest 全绿 + 金样 20/20 + 冻结行 + regression zero diff |

---

## 8. 验收清单（每条落地为探针/断言）

**R1 搜索**
- [ ] 默认源可搜出结果（amap 可达）；Nominatim 不可达时给「大陆不可达，请切高德」分类提示。
- [ ] 联想：输入防抖出建议，选建议即定位。
- [ ] 周边偏置：视口中心附近结果按距离排序。
- [ ] 分类 chips 一键搜。
- [ ] 结果含名称/地址/类别/距离；`offset≥15`。
- [ ] 无 Key 时明确指引（设置·地图·填高德 Key）。

**R2 卫星**
- [ ] 矢量/卫星/混合三态可切，各 grab 非空。
- [ ] 混合 = 影像 + 标注叠加。
- [ ] 图层切换后地理点不漂（datum 往返一致）。
- [ ] 记忆跨进程。

**R3 选点闭环**
- [ ] 搜索后全部结果画成编号图钉。
- [ ] 点图钉/列表项 → 主钉落到该点。
- [ ] 微调后「地图选点」取中心正确。
- [ ] 点选值 = WGS-84（断言 `point_selected` 与 EXIF 写入一致）。

**R4 坐标基准**
- [ ] GPS 卡显示「WGS-84（标准 GPS）」标签。
- [ ] 选点后回显逆地理地址。
- [ ] 边界地带精度提示出现。
- [ ] 十进制度 ↔ DMS 换算/复制往返正确。
- [ ] 现状核对结论入报告：EXIF 写入 WGS-84，未加密（§6 表）。

**回归**
- [ ] 冻结接口：`coord.h/providers.h/mapwidget.h` 既有签名逐字节不变（加性行入追认登记）。
- [ ] 金样 20 对断言零改动；引擎零改动。
- [ ] ctest 全绿；`ui_smoke` 冻结行逐字；明暗双主题等价。

---

## 9. 风险与未决项

| # | 类型 | 说明 | 处置 |
|---|---|---|---|
| X1 | **设计取舍（需用户拍板）** | 是否允许默认启用高德 Web 服务 Key（需注册，免费额度通常够用）？ | P0 依赖；建议「引导填 Key」+ 免 Key 兜底（Nominatim/Esri） |
| X2 | **设计取舍（需用户拍板）** | 卫星图是否只用高德，还是叠 Esri/Bing 国际兜底（OSM 无官方卫星）？ | 建议 amap 为主 + esri 兜底 |
| X3 | 冻结接口 | `mapwidget.h` 加性扩展需「追认」 | 走 C9，入 `m6-adjudications.md` |
| X4 | 精度 | `out_of_china` 粗包围盒在港澳台/边境几十米误差 | UI 提示；可选换更精细国界（后续） |
| X5 | 合规 | 高德/Esri 瓦片与 Key 的 ToS、版权行、UA | 版权行已画；UA 补联系方式；Key 本地存储脱敏 |
| X6 | 政策 | Nominatim 大陆不可达 + UA 政策 | UA 加联系方式；默认源切 amap |

---

## 10. 建议落地顺序（对外承诺口径）

1. **P0（W-M1→W-M2）**：模型解耦 + 默认源切可达 + 卫星图层 → 直接解决「搜不出来」「可看卫星」。
2. **P1（W-M3→W-M4）**：联想/周边/逆地理 + 多钉选点闭环 + 坐标透明化。
3. **P2（W-M5）**：缩放/比例尺/大地图/离线持久化等体验项。
4. **P3**：轨迹线、精细国界、百度 BD-09 支持（按需）。

> **两个待你拍板的取舍**（§9 X1/X2）定下后即可下发开发。开发经 dynamic workflow 由子代理执行，
> 主对话定设计/接口/闸门；每任务一提交 `M6-T<n>`，全量回归后进入收口。
