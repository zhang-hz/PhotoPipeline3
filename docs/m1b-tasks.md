# PhotoPipeline M1b 任务书 v1.0（UI 批次）

> **阶段目标**：Linux 上完成全部 UI（三页主窗口 / 文件列表+缩略图 / 参数表单引擎 / 元数据页+单文件编辑器 / 预设 / 运行页 / 设置 / 地图控件），并与 M1a 冻结引擎全链路集成。
> **文档地位**：M1b 唯一执行依据，由主对话（架构者）维护。执行者是 subagent，**只执行本文档明确写出的动作**。
> **继承**：`docs/m0-tasks.md` §1（16 条）与 `docs/m1-tasks.md` §1（16 条）开发纪律**全额继续生效**；本文档 §1 为 M1b 补充。UI 批次总体定义见 `docs/m1-tasks.md` §4.9（T9–T13/T14b），本文档是其**细化与冻结展开**，冲突时以本文档为准。
> **引擎基线**：M1a 8/8 PASS（`docs/m1-report.md`）；UI 只消费 m1-report §4 的 15 个冻结头文件，**一个签名都不许改**。

---

## 0. 批次范围与轮次模型（主对话控制）

M1b 按多轮设计-审查循环执行：

| 轮次 | 内容 | 出口 |
|---|---|---|
| **R1 实现** | W-A（U1/U2/U3 并行）→ W-B（U4–U9 六任务并行）→ W-C（U10 集成）。产出完整可运行 UI + 全页截图 | `--ui-smoke` 全绿 + 截图产出 |
| **R2..Rn 迭代** | 用户审查截图/实机 → 主对话把问题翻译成 U-FIX-\<n\> 任务规格（追加到 §9）→ subagent 执行 → 重新截图 → 再审查 | 用户判定达标 |
| **收口** | U-FIN（= T14b）：offscreen 冒烟进 ctest、走查清单记录、README UI 章节、`docs/m1b-report.md`（主对话落盘） | §7 出口准则全 PASS |

每轮 subagent 全部用 `deepseek-official/deepseek-flash`、reasoning max、**禁止再下发 subagent**。

---

## 1. 开发纪律（M1b 补充，在 M0/M1 纪律之上叠加）

1. **只做任务书明列的动作**；未覆盖 = 停该项写入报告 `next-needed`，禁止自行决策。设计/结构/接口/文案疑问一律报告，不实现、不绕过。
2. **禁止修改 docs/**（只读，含本文档）。报告放最终消息。
3. **PP-FROZEN 逐字节**：本文档 §2 全部头文件按给出的文本**逐字节复制**（含注释与空行；`…` 省略号标注处除外——那里写你自己的私有成员）。M1a 的 15 个冻结头文件一个字符不许动。
4. **禁止再下发 subagent**。
5. **禁止改依赖与构建基础设施**；CMake/CMakePresets 改动**仅限** §6 明确分配给本任务的行。
6. **Qt 依赖面**：只准用 Qt6::Core / Gui / Widgets / Network 四个模块的 API；禁用 QtCharts/QtQml/QtSvg 等；**禁止新增第三方库**（无 nlohmann、无 quazip…）。Qt API 事实必须来自 `.toolchain/Qt/6.8.3/gcc_64/include` 实际头文件（"事实来自命令输出"纪律）。
7. **UI 图像库隔离**：`src/ui/**`、`src/mapwidget/**` 内**禁止 include** OpenImageIO / lcms2 / libheif / libjxl / libwebp / jpegli 头文件；像素/编码工作一律走 core 接口（`pp::`）。**唯一例外**：`ui/exif_editor.cpp` 与 `ui/page_meta.cpp` 可直接调用 Exiv2 API（元数据模型即 Exiv2 类型，经 `core/metadata.h` 传递；读 GPS/预览/类型校验需要）。
8. **线程纪律**：worker 线程 → GUI 只允许 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 或跨线程信号（队列连接）；GUI 线程禁止任何 >50ms 的阻塞操作（探测/缩略图必须异步）。
9. **i18n**：所有用户可见文本一律 `tr()` 包裹，源语言中文；英文术语按 §3 文案表。
10. **视觉**：Qt 内建样式；`QApplication::setStyle("Fluent")` 尝试设置、失败静默回退（qInfo 记录）；**不自绘非地图控件、不用 QSS 主题文件、不引入图标文件**（文本徽标/Unicode 符号即可）；Mica 属 M3。
11. **文件归属**：只创建/修改本任务 §4 明列的文件；跨域需求写报告。并行波次**先写完全部源文件再构建**；构建偶发失败（glob 竞态）重试一次再报告。
12. **构建隔离**：每任务用 `source tools/env.sh && cmake --preset release -B build/m1b-u<n> -DVCPKG_MANIFEST_INSTALL=OFF && cmake --build build/m1b-u<n> -j && ctest --test-dir build/m1b-u<n> --output-on-failure`；`build/release*` 归 U10。
13. **Git**：`M1b-U<n>: <摘要>`；红构建不提交；只 add 本任务文件；禁 push/rebase/reset。
14. **`TODO(M2)` 标签纪律**照旧；内部不变量 assert，用户输入走错误返回。
15. **报告 ≤150 行**，按 §7 格式。

---

## 2. 冻结接口全集（M1b 新增；PP-FROZEN，逐字节落地）

命名空间：`pp`（core/platform）、`pp::ui`（ui/）、`pp::map`（mapwidget/）。已有 15 个 M1a 冻结头不在本文重复（见 m1-report §4）。

### 2.1 `src/core/thumbs.h`（U1；Qt-free，编入 pp_core）

```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — probe + thumbnail (M1b frozen; UI-facing, core layer, no Qt)
#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "core/types.h"

namespace pp {

struct ThumbImage {
    int width = 0, height = 0;       // returned size (longest edge <= target)
    std::vector<uint8_t> rgba;       // width*height*4, 8-bit, sRGB-assumed, alpha on white
    bool from_embedded = false;      // true = embedded preview used
};

struct ThumbOutcome {
    pp::ImageInfo info;              // probe result (meaningful when probe_ok)
    bool probe_ok = false;
    std::string error;               // non-empty = probe failure (row error state)
    pp::ThumbImage thumb;            // empty = thumbnail unavailable (probe still ok)
};

// Thread-safe, stateless: probe + thumbnail in one call.
//  1) probe_file(src): failure -> {probe_ok=false, error}
//  2) embedded preview: Exiv2 PreviewManager, largest preview with long edge >= 64;
//     decode its bytes via OIIO (memory reader), resample to target
//  3) no/failed preview -> full float32 decode + resample to target
//  4) thumb failure with probe_ok -> thumb empty, error stays empty
//  5) never upscale (scale = min(target/w, target/h, 1.0)); gray -> RGB; alpha -> white
ThumbOutcome make_thumbnail(const std::filesystem::path& src, int target_long_edge);

}  // namespace pp
```

### 2.2 `src/core/settings.h`（U1）与 `src/platform/paths.h`（U1）

**逐字节复制 `docs/m1-tasks.md` §3.14（settings.h）与 §3.13（paths.h）的冻结代码块**（本文不重复）。落地约束：

- `settings.cpp`：极简 INI，`key=value`、`#` 注释、未知键**原样写回**；缺文件 → 全默认值不报错；保存原子（先写 `<file>.tmp` 再 rename）。
- `paths.cpp`：`executable_dir()` 用 `/proc/self/exe`（Linux）；可写探测 = 在 exe 目录尝试创建 `.pp-write-test` 文件后删除；结果**进程内缓存**；data_dir/prestes_dir/logs_dir 首次调用时 `create_directories`（失败回退 XDG，再失败返回空 path 由调用方处理）。

### 2.3 `src/mapwidget/coord.h`（U2）

```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — WGS-84 <-> GCJ-02 conversion (M1b frozen)
#pragma once
#include <utility>

namespace pp::map {

// Public iterative algorithm. GCJ->WGS is an iterative approximation (~1m).
// Out of China (rough bbox test) -> identity (returns input unchanged).
std::pair<double, double> wgs84_to_gcj02(double lat, double lon);
std::pair<double, double> gcj02_to_wgs84(double lat, double lon);

}  // namespace pp::map
```

### 2.4 `src/mapwidget/providers.h`（U2）

```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — tile & geocoding providers (M1b frozen)
#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <vector>

namespace pp::map {

struct SearchResult {
    QString title;
    double lat = 0, lon = 0;   // WGS-84
};

class TileProvider {
public:
    QString id;      // "osm" | "amap"
    QString label;   // "OpenStreetMap" | "高德地图"
    bool gcj02 = false;

    QUrl tile_url(int z, int x, int y) const;   // amap rotates webrd01..04
    // Search URL. OSM: Nominatim (no key). Amap: REST place API; empty key -> returns
    // url with key placeholder empty (caller must check amap_key first).
    QUrl search_url(const QString& query, const QString& amap_key) const;
    QString user_agent() const;                 // "PhotoPipeline/0.1 (batch transcoder; dev)"
};

// nullptr if unknown id
const TileProvider* provider_by_id(const QString& id);
// {"osm","amap"}
QStringList provider_ids();
// Parse search JSON -> results (WGS-84; amap converts GCJ->WGS internally).
// Returns "" on success, else human-readable error (Chinese).
QString parse_search(const QString& provider_id, const QByteArray& body,
                     std::vector<SearchResult>& out);

}  // namespace pp::map
```

URL 事实（冻结，不得改动）：
- OSM 瓦片：`https://tile.openstreetmap.org/{z}/{x}/{y}.png`（**必须带 User-Agent**，遵守 OSM 瓦片政策；不用子域轮换）。
- 高德瓦片：`https://webrd0{n}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}`，n∈1..4 轮换；坐标系 GCJ-02。
- Nominatim：`https://nominatim.openstreetmap.org/search?format=json&limit=6&q={query}`（**必须带 User-Agent**）；响应 `[{"display_name","lat","lon"}...]`。
- 高德搜索：`https://restapi.amap.com/v3/place/text?key={key}&keywords={query}&offset=6&page=1`；响应 `pois[].name` + `pois[].location`（"lng,lat"，GCJ-02 → 转 WGS-84）；`status!="1"` → 错误串。

### 2.5 `src/mapwidget/mapwidget.h`（U2）

```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — self-drawn slippy map (M1b frozen)
#pragma once
#include <QWidget>
#include <memory>
#include "mapwidget/coord.h"

namespace pp::map {

class MapWidget : public QWidget {
    Q_OBJECT
public:
    explicit MapWidget(QWidget* parent = nullptr);
    ~MapWidget();

    void set_provider(const QString& id);          // "osm" | "amap"
    void set_amap_key(const QString& key);
    void set_cache_mb(int mb);                     // tile LRU byte budget
    void set_offline(bool off);                    // true: no network; cached tiles still drawn

    // All public coordinates are WGS-84. Internal tile math uses the provider
    // datum (GCJ-02 for amap); conversion happens at the boundary.
    void set_marker(double lat, double lon);
    void clear_marker();
    bool has_marker() const;
    double marker_lat() const;
    double marker_lon() const;
    void center_on(double lat, double lon, int zoom = -1);   // zoom -1 = keep current

    QSize minimumSizeHint() const override { return {240, 180}; }

signals:
    void point_selected(double lat, double lon);   // click (not drag) selection, WGS-84
    void search_finished(const QStringList& titles, const QString& error);
    void tiles_pending_changed(int pending);       // fetch queue depth (for spinners/tests)

public slots:
    void run_search(const QString& query);         // async; -> search_finished
    void search_select(int index);                 // pick result -> center+marker+point_selected

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pp::map
```

行为规格（冻结）：
- 状态：中心（datum 坐标）+ zoom(int 2..18) + marker（WGS-84 存储，绘制时转 datum）。初始 center=(35.0,105.0) zoom=4。
- 交互：滚轮 = 以光标为锚 ±1 级；左键拖拽平移；**点击（按下/抬起位移 <4px）= 选点**（十字标 + `point_selected`，datum→WGS-84 后发射）。
- 绘制：可见瓦片范围 → 缓存命中绘制；未命中画浅灰底 + 细网格线；经 `QNetworkAccessManager` 拉取（**并发 ≤4**，每 provider 一个 UA），到包 → 入缓存 → `update()`；瓦片 256×256，`z/x/y` 按标准 slippy 公式（`(lon+180)/360·2^z·256`、Mercator y）。marker 最后画（十字线 + 圆圈）；右下角 6pt 版权标签（"© OpenStreetMap contributors" / "© 高德地图"）。
- 缓存：`(provider,z,x,y) -> QByteArray` LRU，字节预算 `set_cache_mb`（默认 64MB）；超限逐最旧淘汰。
- offline：不发任何网络请求；`run_search` → `search_finished({}, tr("离线模式：搜索不可用"))`。
- 搜索：`run_search` → GET（amap 无 key → `search_finished({}, tr("使用高德搜索需要先在设置中填写 Key"))`）→ `parse_search` → `search_finished(标题列表, err)`；`search_select(i)` → 定位 + set_marker + `point_selected`。搜索结果列表 UI **不在本控件内**（由嵌入方 PageMeta 承载）。
- 断网（请求失败/超时 8s）：对应瓦片保留灰底；连续失败 ≥3 → 底部提示条 "地图离线（可手动输入坐标）"（QLabel overlay，不弹窗）。

### 2.6 `src/ui/paramform.h`（U3）

```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — schema-driven parameter form (M1b frozen)
#pragma once
#include <QWidget>
#include <string>
#include <vector>
#include "core/params.h"

namespace pp::ui {

struct FormSelection {
    std::string backend;     // current backend id ("" -> first)
    std::string tech;        // current tech id ("" -> first)
    bool lossless = false;   // lossless switch (only meaningful when tech lossless_capable)
};

class ParamForm : public QWidget {
    Q_OBJECT
public:
    // fmt: static format def (static_formats() entries are process-stable).
    // backends: effective backend list. For runtime-introspected formats (heif/avif)
    // pass pp::introspect_backends(fmt.id); for others pass fmt.backends.
    ParamForm(const pp::FormatDef& fmt, std::vector<pp::BackendDef> backends,
              QWidget* parent = nullptr);

    void set_selection(const FormSelection& sel);   // programmatic (no signal)
    FormSelection selection() const;
    pp::ParamSet values() const;                    // current values, "__" keys excluded
    void set_values(const pp::ParamSet& s);         // apply by key, unknown keys ignored

    // test/inspection hook: is the row for `key` currently visible?
    bool is_param_visible(const std::string& key) const;

signals:
    void selection_changed(const pp::ui::FormSelection& sel);  // user action only
    void changed();                                            // any value/selection change

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pp::ui
```

行为规格（冻结；这是 §6.3 设计的落地）：
1. 顶部选择器行（QFormLayout 或 HBox）：`后端` QComboBox——仅当 backends.size()>1；`技术` QComboBox——仅当当前后端 techs.size()>1；`无损` QCheckBox——仅当当前技术 `lossless_capable`。
2. **无损语义**（对应保留键 `__lossless`）：勾选 → 内部工作集写入 `__lossless=true`，并调用 `pp::apply_locks(fmt, backend, tech, true, set)`；若当前技术**不** lossless_capable → 自动切换到首个 lossless_capable 技术（jxl→modular、webp→lossless），无任何 lossless_capable 技术则复选框隐藏（jpeg）。取消勾选 → `__lossless=false`，重新 `apply_locks`。技术下拉在无损开启期间把非 lossless_capable 项置灰（disabled item）。
3. 控件映射：Int→QSpinBox(lo..hi, step)；Float→QDoubleSpinBox(lo..hi, step，decimals=3)；Bool→QCheckBox；Enum→QComboBox(choices)。tooltip = ParamDef.tooltip。核心参数（advanced=false）直接排；advanced=true 收进 `高级参数` QGroupBox（collapsed 起始），组内顶部 `QLineEdit` 搜索框按 label/key 包含过滤（大小写不敏感）。
4. **谓词重算**：任何值/选择变化 → 对当前技术每个参数求 `eval_visible`/`eval_lock`：不可见 → 行隐藏；锁定 → 控件 `setEnabled(false)` 且值显示为强制值（写入工作集）。之后 `apply_locks` 已在 2/4 中执行。
5. **`●` 偏离标记**：当前值 ≠ def（同类型比较）→ 行标签前缀 `"● "`；恢复默认则去掉。右键菜单（label 上）：`重置为默认`。
6. `values()`：工作集去掉 `__` 前缀键的拷贝。`set_values()`：同 key 同类型才应用（int64/double 互不容忍，枚举按 choice value 匹配）；随后重算谓词 + `●`。
7. 信号：用户操作（下拉/勾选/spin 改值）→ `changed()`；后端/技术/无损被用户改变 → 先 `selection_changed` 再 `changed()`。`set_selection/set_values` 不发信号。
8. heif/avif（运行时内省后端）：单一 "runtime" 技术 → 技术下拉隐藏；参数全部来自内省 TechDef；`lossless` 复选框按描述符 `lossless_capable` 出现。

### 2.7 `src/ui/thumbnails.h`（U4）

```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — async probe/thumbnail provider (M1b frozen)
#pragma once
#include <QImage>
#include <QObject>
#include <QString>
#include "core/types.h"

namespace pp::ui {

// <=2 worker threads (design §1.3), FIFO queue, results on the GUI thread.
class Thumbnailer : public QObject {
    Q_OBJECT
public:
    explicit Thumbnailer(int target_long_edge = 96, QObject* parent = nullptr);
    ~Thumbnailer();                  // stops workers immediately (detaches pending)

    void request(int row, const QString& path);
    void clear_pending();
    int pending() const;

signals:
    void ready(int row, const QString& path, const pp::ImageInfo& info, bool probe_ok,
               const QString& error, const QImage& thumb);
    void queue_empty();              // pending reached 0 (after >=1 request)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pp::ui
```

实现约束：`qRegisterMetaType<pp::ImageInfo>()` 在构造函数里执行（队列信号需要）；worker 用 `std::jthread`×2，队列 mutex+condvar；结果经 `QMetaObject::invokeMethod(this, [...]{ emit ready(...); }, Qt::QueuedConnection)` 回 GUI 线程；重复 request 同一 row 未决时丢弃旧的；析构 stop_token 立即退出。

### 2.8 `src/ui/filelistmodel.h`（U4）

```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — file list model + delegate (M1b frozen)
#pragma once
#include <QAbstractListModel>
#include <QImage>
#include <QStyledItemDelegate>
#include <QStringList>
#include <optional>
#include <set>
#include <vector>
#include "core/pipeline.h"

namespace pp::ui {

struct FileRow {
    pp::FileEntry entry;              // src, base_dir (= src.parent), exception
    pp::ImageInfo info;               // after probe
    bool probe_ok = false;
    QString error;                    // probe error (Chinese prefix + English detail)
    pp::FileState state = pp::FileState::Queued;
    QImage thumb;                     // may be null
    bool has_exception() const { return entry.exception.has_value(); }
};

class FileListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        ThumbRole = Qt::UserRole + 1,  // QImage
        NameRole,                      // QString (file name)
        PathRole,                      // QString (full path)
        DimRole,                       // QString "1920×1080" ("" before probe)
        FormatRole,                    // QString "JPEG" ("" before probe)
        StateRole,                     // int (pp::FileState)
        StateTextRole,                 // QString (§3 mapping)
        ExceptionRole,                 // bool
        ErrorRole                      // QString
    };

    explicit FileListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& idx, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& idx) const override;

    // files and/or directories; directories walked recursively (UI-side walk:
    // supported = ext in pp::input_extensions() case-insensitive; dotfiles skipped;
    // unsupported regular files counted, not added; duplicates by lexically_normal
    // path skipped). Triggers thumbnail enqueue for added rows.
    void add_paths(const QStringList& paths);
    void remove_rows(const QList<int>& rows);   // any order; internally handled
    void clear();

    std::size_t size() const;
    bool empty() const;
    const FileRow& row(std::size_t i) const;
    std::size_t unsupported_count() const;
    std::size_t exception_count() const;
    std::vector<pp::FileEntry> entries() const;         // deep copy for Scheduler

    void set_state(std::size_t i, pp::FileState st);    // dataChanged
    void set_exception(std::size_t i, std::optional<pp::MetadataOverride> ex);
    void apply_thumb(int row, const QString& path, const pp::ImageInfo& info,
                     bool probe_ok, const QString& error, const QImage& thumb);
    // stale-guard: ignores when row out of range or path mismatch

    void set_thumbnailer(Thumbnailer* t);   // not owned; nullptr = no async thumbs
    void enqueue_pending_thumbs();          // (re)enqueue all rows lacking probe info

signals:
    void content_changed();                 // count/unsupported/anything summary-worthy
    void exception_changed(std::size_t i);

private:
    std::vector<FileRow> rows_;
    std::set<QString> known_paths_;         // lexically_normal strings
    std::size_t unsupported_ = 0;
    Thumbnailer* thumbs_ = nullptr;
};

// Row painting: 56px high; 48x48 thumb left; name (bold) + "1920×1080 · JPEG" second line;
// right-aligned state badge (colored text §3) + "⚑" exception marker before badge.
class FileDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;   // {option.rect.width(), 56}
};

}  // namespace pp::ui
```

### 2.9 `src/ui/page_output.h`（U5）

```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — output page (M1b frozen)
#pragma once
#include <QWidget>
#include <memory>
#include "core/pipeline.h"
#include "core/presets.h"

namespace pp::ui {

class ParamForm;

class PageOutput : public QWidget {
    Q_OBJECT
public:
    explicit PageOutput(QWidget* parent = nullptr);
    ~PageOutput();

    // RunConfig skeleton: out_root/format/backend/tech/lossless/params/bitdepth/
    // color_target/conflict/metadata_only filled; rules/workers/budget/rotate/flatten
    // left at defaults (MainWindow fills them).
    pp::RunConfig config_base() const;

    bool metadata_only() const;
    void set_metadata_only(bool on);        // disables format-specific groups (see spec)
    QString out_root() const;
    void set_out_root(const QString& dir);

    void set_batch_has_alpha(bool has);     // avif 10bit+alpha -> libaom preselect

    void apply_preset(const pp::PresetData& p);   // format/selection/values/bitdepth/color/conflict
    pp::PresetData collect_preset(const QString& name) const;   // rules merged by MainWindow

    // session restore (no signals); invalid values ignored
    void restore_last(const QString& format_id, const QString& out_root);

    // "" = ready; else human-readable reason (Start button gating)
    QString ready_to_start() const;

    // smoke/test hooks
    QString current_format() const;
    void select_format(const QString& format_id);   // programmatic, no signal

signals:
    void format_changed(const QString& format_id);
    void config_changed();                        // anything summary-worthy
    void manage_presets_requested();
    void open_settings_requested();
};

}  // namespace pp::ui
```

行为规格（冻结）：
1. 整页在 QScrollArea 内。自上而下：
   - **模式** QGroupBox（2 个 QRadioButton）：`转码` / `仅元数据`，默认转码。
   - **输出格式** QGroupBox：8 个 checkable QToolButton 排 4×2（QGridLayout），顺序与文案：`JPEG`、`JPEG XL`、`PNG`、`TIFF`、`WebP`、`HEIF`、`AVIF`、`BMP`；QButtonGroup 互斥；默认按 restore_last/apply_preset，否则 `jxl`。
   - **全局** QGroupBox（QFormLayout）：`输出根目录` = QLineEdit + `浏览…` QPushButton（QFileDialog::getExistingDirectory）；`同名冲突` QComboBox（`自动加序号`/`跳过`/`覆盖`，默认 自动加序号 → Rename/Skip/Overwrite）；`色彩目标` QComboBox（`保持原样`/`sRGB`/`Display P3`/`Adobe RGB (1998)` → KeepOriginal/SRGB/DisplayP3/AdobeRGB）；`输出位深` QComboBox（动态，见下）。
   - **格式参数** QGroupBox：内嵌 ParamForm（格式切换时重建）。
   - 按钮行：`预设管理…`、`设置…`（发 manage_presets_requested / open_settings_requested）。
2. **位深规则**（M1a 冻结口径）：可选值 = 静态允许集 ∩ 运行期探测。静态：jpeg{8}、jxl{8,16}、png{8,16}、tiff{8,16}、webp{8}、bmp{24}、heif/avif{8,10,12}。heif/avif 调 `pp::probe_bitdepth_support(format, backend)` 解析 "8,10,12" 取交集（结果按 (format,backend) **缓存一次**）；默认值逐格式：jpeg 8 / jxl 16 / png 16 / tiff 16 / webp 8 / bmp 24 / heif 10 / avif 10（默认不在交集 → 8 并发 config_changed）。后端切换时重算：当前位深仍有效则保留。
3. **avif + alpha 预选**：`set_batch_has_alpha(true)` 且 format==avif 且当前位深==10 且后端==svt-av1 → 自动切 libaom + 显示黄色提示 QLabel `10bit + 源含 alpha：已选择 libaom 后端（SVT-AV1 不支持该组合）`（用户可手动改回，仅提示不阻止）。
4. **仅元数据模式**：格式组/位深/色彩目标/格式参数全部 `setEnabled(false)` + 组内追加灰色说明 QLabel `仅元数据：输出保持源格式（JPEG / PNG / TIFF / WebP）；其余参数不适用`；模式/输出根目录/冲突策略保持可用。
5. `apply_preset`：validate 通过才应用（`pp::normalize_preset` 补齐后逐字段设置）；格式变化 → 重建 ParamForm + 发 format_changed。
6. `ready_to_start()`：输出根目录为空 → `未设置输出根目录`；非绝对路径 → `输出根目录必须是绝对路径`；否则空串。
7. heif/avif 内省与位深探测在**格式首次选中时**执行并缓存（进程内静态缓存即可），避免每次重建重复探测。
8. 所有变化 → `config_changed()`。

### 2.10 `src/ui/settings_dialog.h` 与 `src/ui/presets_dialog.h`（U6）

```cpp
// PP-FROZEN(file) — src/ui/settings_dialog.h
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — settings dialog (M1b frozen)
#pragma once
#include <QDialog>
#include "core/settings.h"

namespace pp::ui {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const pp::AppSettings& current, QWidget* parent = nullptr);
    pp::AppSettings settings() const;    // edited values (OK or Apply)
};

}  // namespace pp::ui
```

```cpp
// PP-FROZEN(file) — src/ui/presets_dialog.h
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — preset manager dialog (M1b frozen)
#pragma once
#include <QDialog>
#include <QString>
#include <utility>
#include <vector>

namespace pp::ui {

class PresetsDialog : public QDialog {
    Q_OBJECT
public:
    enum class Action { None, Load, SaveAs, Delete };
    // presets: (path, display-name) list from pp::ui::list_presets(presets_dir)
    explicit PresetsDialog(const std::vector<std::pair<QString, QString>>& presets,
                           const QString& suggested_name, QWidget* parent = nullptr);
    Action action() const;
    QString name() const;      // load target / save-as name / delete target
    QString path() const;      // corresponding file path (already sanitized for save-as)
};

}  // namespace pp::ui
```

SettingsDialog 规格：QTabWidget 三页。
- **运行**：`并发 worker 数` QSpinBox(0..64，特殊值显示 `自动（物理核数）`)；`内存预算 (GB)` QSpinBox(0..64，0 显示 `自动`）；`alpha 合成底色` QSlider(0..100) + 实时色块 QLabel（背景灰度 = 值）+ 文案 `黑 ←→ 白`；`按 EXIF 方向旋转` QCheckBox（rotate_orientation，默认开）。
- **地图**：`底图提供方` QComboBox（`OpenStreetMap（内置）`/`高德地图`）；`高德 Web 服务 Key` QLineEdit（password echo）；`瓦片缓存 (MB)` QSpinBox(16..512)；`日志级别` QComboBox（trace/debug/info/warn/error）。
- **关于**：`PhotoPipeline 0.1.0` + `pp::library_versions()` 逐行清单（QPlainTextOutput 只读）+ 许可说明静态文本：`本程序 GPL-3.0-or-later。所用库：Exiv2/x265 (GPLv2+)、libheif (LGPLv3)、Qt (LGPLv3)、OIIO (Apache-2.0)、libjxl/libwebp/SVT-AV1/libaom (BSD)、lcms2/spdlog (MIT)。`
- 按钮：`确定`/`取消`；`settings()` 返回编辑值（仅确定路径）。

PresetsDialog 规格：QListWidget（预设名，单选）+ `名称` QLineEdit（保存用，选中列表项时带出）+ 按钮行 `载入`/`另存为`/`删除`/`关闭`。删除需 QMessageBox::question 确认。保存名清洗：仅保留 `[A-Za-z0-9_\- ]`，其余剔除，空 → 载入/另存为按钮禁用。文件名 = `<名>.json`（路径由 MainWindow 用 presets_dir 拼接；对话框只回名字与列表路径）。

### 2.11 `src/ui/page_meta.h`（U7）

```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — metadata page (M1b frozen)
#pragma once
#include <QWidget>
#include <QStringList>
#include "core/metadata.h"

namespace pp::ui {

class PageMeta : public QWidget {
    Q_OBJECT
public:
    explicit PageMeta(QWidget* parent = nullptr);

    pp::BatchRules rules() const;                 // built from current UI state
    void apply_rules(const pp::BatchRules& r);    // preset load (no signal)

    void set_batch_files(const QStringList& paths);        // first-file time preview refresh
    void set_selected_files(const QStringList& paths);     // "read GPS from selection" enable
    void set_exception_summary(const QStringList& paths);  // exception card list
    void set_map_provider(const QString& provider_id, const QString& amap_key, int cache_mb);

signals:
    void rules_changed();
    void open_editor_requested(const QString& path);
    void clear_exception_requested(const QString& path);
};

}  // namespace pp::ui
```

行为规格（冻结）：整页 QScrollArea；卡片自上而下：

1. **时间偏移** QGroupBox + 启用 QCheckBox（默认关）：`方式` QComboBox（`Δ 偏移（时钟拨错）`/`时区语义（旅行照片）`）。Δ 模式：`年 月 日 时 分 秒` 6×QSpinBox(-9999..9999)；时区模式：`从`/`到` 两个 QComboBox（UTC-12:00..UTC+14:00 全表，含整半时区；显示 `UTC+08:00`，数据 = 分钟数）。底部两行 QLabel 前后对照预览：`首文件：2024:01:01 10:00:00 → 2024:01:01 11:00:00`；数据源 = set_batch_files 的第一个含时间文件（`pp::read_metadata` → `pp::effective_datetime`；无时间 → `首文件无时间字段`）。任一控件变化即时刷新。
2. **GPS** QGroupBox + 启用 QCheckBox（默认关）：`纬度`/`经度` QLineEdit（QDoubleValidator，-90..90 / -180..180，6 位小数）+ 实时 DMS 只读 QLabel（`31°13'49.4"N 121°28'25.3"E`）；`更多字段` 可折叠：`海拔 (m)`/`方位角 (°)`/`时间戳`（`YYYY:MM:DD HH:MM:SS`）QLineEdit 可空；内嵌 MapWidget（固定高 260，宽随卡片）；地图上方一行：`搜索` QLineEdit + `搜索` QPushButton + 结果 QComboBox（search_finished 填充标题，search_select 联动）+ `从选中文件读取坐标` QPushButton（set_selected_files 为空时禁用；读首个选中文件 GPS → 回填字段 + set_marker；无 GPS → 状态提示）。`清除 GPS` QCheckBox（勾选 → 坐标输入禁用；rules() 输出 gps_clear=true、无 gps）。
3. **标签修改** QGroupBox：行列表（QVBoxLayout of rows：`键` QLineEdit placeholder `Exif.Image.Artist 或 Xmp.dc.title`、`值` QLineEdit、`✕` 删除按钮）+ `添加标签` QPushButton。**值留空 = 删除该标签**（tooltip 说明）。rules()：键 `Xmp.` 前缀 → xmp_edits，否则 exif_edits；值空 → remove=true。
4. **隐私剥除** QGroupBox：QCheckBox `剥除全部 EXIF / XMP（保留 ICC 与像素）` + 灰字说明 `优先级最高的规则`。
5. **文件时间** QGroupBox：QCheckBox `输出文件修改时间同步拍摄时间 (mtime)`。
6. **例外** QGroupBox：`N 个文件带元数据例外` QLabel（0 → `无例外文件`）+ QListWidget（路径，双击 → open_editor_requested；右键菜单 `清除例外` → clear_exception_requested）。

rules() 装配（冻结）：启用且字段合法才填 time_shift/gps；GPS 勾选但坐标非法 → 忽略 gps 且卡片顶部显示黄色 `GPS 坐标无效` 提示。规则变化 → rules_changed()。

### 2.12 `src/ui/exif_editor.h`（U8）

```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — single-file metadata editor (M1b frozen)
#pragma once
#include <QDialog>
#include <optional>
#include "core/metadata.h"

namespace pp::ui {

class ExifEditor : public QDialog {
    Q_OBJECT
public:
    // src: source file (read-only!). batch: current batch rules (for markers).
    // existing: current override to edit (nullopt = fresh).
    ExifEditor(const QString& src, const pp::BatchRules& batch,
               const std::optional<pp::MetadataOverride>& existing, QWidget* parent = nullptr);
    ~ExifEditor();

    // valid after accept()
    pp::MetadataOverride result() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pp::ui
```

行为规格（冻结）：窗口 900×640，模态。顶部：文件名 QLabel + `忽略全部批量规则` QCheckBox（ignore_batch）。中部 QTabWidget：

1. **EXIF**：左半 QTreeWidget（搜索 QLineEdit 过滤）：分组 `IFD0`（Exif.Image.*）/`Exif`（Exif.Photo.*）/`GPS`（Exif.GPSInfo.*）/`MakerNote（只读）`（其余分组全部归并此处，子节点按原分组名，禁编辑）。子节点 = 标签名 + 当前值（toString）。批量规则会影响的标签（时间字段/GPS 字段/strip_privacy 全部/edits 命中键）文本前缀 `〔批〕`。右半值编辑区：选中项 → 按 Exiv2 类型给 QLineEdit（多数类型）或多行 QPlainTextEdit（UserComment/长 ASCII>60）；MakerNote 组只读显示。已改值项前缀 `● `。底部行：分组 QComboBox（IFD0/Exif/GPS）+ `编号 (hex)` QLineEdit（如 `0x0132`）+ 类型 QComboBox（Byte/Ascii/Short/Long/Rational/SRational/Undefined）+ `添加标签` 按钮（Exiv2 key 拼接 + 默认空值进树）。
2. **XMP**：同构（命名空间分组树 + 值编辑 + `Xmp.xxx.yyy 自定义路径` 添加行）。LangAlt 只编辑 `x-default`（值后缀标注）。
3. **时间 / GPS**：两组三态（`继承批量规则`/`覆盖`/`清除` QComboBox）：时间覆盖 → 同批量页的 Δ/时区字段；GPS 覆盖 → 纬经度 + 可选字段，或选 `清除 GPS`（gps_clear）；另有 `隐私剥除` 三态 QComboBox（继承/强制开/强制关 → strip_privacy optional）。

确定校验：把全部编辑经 `pp::apply_edits` 打到 `read_metadata` 的副本上，errors 非空 → QMessageBox 列出（保留对话框打开）。源文件**永远只读**——编辑只进 MetadataOverride。result()：exif_edits/xmp_edits = 树中 set/remove 记录；时间/GPS/隐私三态与 ignore_batch。取消 → 不修改。

### 2.13 `src/ui/page_run.h`（U9）

```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — run page (M1b frozen)
#pragma once
#include <QWidget>
#include "core/scheduler.h"

namespace pp::ui {

class PageRun : public QWidget {
    Q_OBJECT
public:
    explicit PageRun(QWidget* parent = nullptr);

    void begin_run(std::size_t total, const QStringList& names);   // reset + populate rows
    void on_event(const pp::FileEvent& ev);     // GUI thread; ev.result already copied by caller
    void end_run(const pp::RunSummary& sum, const QString& out_root);
    void reset();                               // idle state

    bool is_running() const;

signals:
    void cancel_requested();
    void open_output_requested(const QString& dir);
    void open_logs_requested();
};

}  // namespace pp::ui
```

行为规格（冻结）：顶部：总进度 QProgressBar（format `第 %v / %m 个`）+ 状态 QLabel（`排队中…` / `进行中 · 完成 N · 失败 M · 跳过 K` / `已取消：完成 N · 取消 C`）+ 吞吐 QLabel（`8.3 MB/s · 平均 120 ms/文件`；运行中由累计 out_bytes / QElapsedTimer 估算，结束用 summary）+ `取消` QPushButton（运行中才启用）。中部 QListView（内部小模型，文件名 + 状态文本着色 §3）。运行中双击行 → tooltip 显示 error。结束：摘要 QGroupBox（成功/失败/跳过/取消/总耗时/吞吐/平均）+ `打开输出目录` + `查看日志` 按钮（发信号）。begin_run 前页面显示引导文案 `点击主界面"开始"运行批处理`。on_event 终态时把 `*ev.result` 的 out_bytes 计入吞吐。

### 2.14 `src/ui/mainwindow.h`（U10）

```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — main window, three zones (M1b frozen)
#pragma once
#include <QMainWindow>
#include <memory>
#include "core/pipeline.h"
#include "core/scheduler.h"
#include "core/settings.h"

namespace pp::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const pp::AppSettings& settings, QWidget* parent = nullptr);
    ~MainWindow();

    void add_paths(const QStringList& paths);      // files/dirs (drop / smoke / api)
    void set_current_page(int one_based);          // 1=meta 2=output 3=run
    void set_offline_maps(bool off);               // smoke / degraded

    // dev-only scripted walk (PP_BUILD_DEV callers): pages + dialogs, optional PNG grabs
    void ui_smoke_walk(const QString& shots_dir);

private slots:
    void on_start();
    void on_cancel();
    void on_scheduler_done();                      // QTimer poll -> finish run
    void open_settings();
    void manage_presets();
    void open_exif_editor_row(int row);
    void open_exif_editor_path(const QString& path);

private:
    void build_ui();
    void wire();
    void lock_for_run(bool lock);                  // G5
    void refresh_status();                         // bottom summary + start enable
    void save_session();                           // closeEvent
    // zones/pages/models (unique_ptr<...> members; types not repeated here — keep private)
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pp::ui
```

行为规格（冻结）：
- **布局**：默认 1280×800，min 1024×680，标题 `PhotoPipeline`。顶部 QToolBar（非可移动）：左侧 3 个 checkable QAction（`① 元数据` `② 输出` `③ 运行`，QActionGroup 互斥）→ 切 QStackedWidget 页；右侧 `设置…` QAction。中部 QSplitter(Horizontal)：左 = 文件面板（见下），右 = QStackedWidget(3 页)。底部 QHBox：状态摘要 QLabel（`16 个文件 · JPEG XL → /home/x/out · 冲突：自动加序号`，配置无效时显示原因并红字）+ stretch + `开始` QPushButton（default，运行中变 `取消`）。
- **文件面板**（左，初始宽 320，Splitter 可调）：标题行 `文件` + 计数 QLabel；按钮行 `添加文件…`（QFileDialog 多选）/`添加文件夹…`/`移除所选`/`清空`；`搜索` QLineEdit（placeholder `搜索文件名…`）过滤 QListView；QListView(FileListModel + QSortFilterProxyModel(NameRole contains) + FileDelegate)。窗口整体 acceptDrops，dropAccept → add_paths。双击行 → open_exif_editor_row。`unsupported_count>0` → 标题行右侧黄字 `（N 个不支持）`。
- **开始**（on_start）：校验 = 文件非空 + PageOutput::ready_to_start 空 + 未运行中。构造 RunConfig：PageOutput::config_base() + PageMeta::rules() + settings(workers/budget_bytes=GB*2^30 或 0/flatten_gray/rotate_orientation)。仅元数据：按扩展名预检（.jpg/.jpeg/.png/.tif/.tiff/.webp 之外 → QMessageBox::warning 列出并确认继续）。Scheduler(cfg, model->entries())；event 回调：`QMetaObject::invokeMethod(this, [拷贝]{ ... }, Qt::QueuedConnection)` —— **在回调内立即按值拷贝 FileResult**（指针仅发射瞬间有效，slots 收 FileEvent 拷贝 + FileResult 拷贝）。start() 后 lock_for_run(true)（nav/左面板/第 1、2 页禁用，stack 锁到运行页，开始→取消），PageRun::begin_run；QTimer(150ms) 轮询 `!running()` → on_scheduler_done → wait() → summary → PageRun::end_run → 恢复 UI + refresh_status。
- **取消**：sched->cancel()（进行中文件跑完）；剩余文件收 Cancelled 事件。
- **事件分发**：状态事件 → FileListModel::set_state + PageRun::on_event；终态 → 计数。
- **设置对话框**：SettingsDialog(current) → OK → 保存 settings_、log_set_level、PageMeta::set_map_provider；`设置持久化`：立即 save_settings。
- **预设管理**：list_presets(platform::presets_dir()) → PresetsDialog；载入 → load_preset + normalize_preset + PageOutput::apply_preset + PageMeta::apply_rules；另存为 → PageOutput::collect_preset + rules 合并 → save_preset；删除 → 文件删除 + 刷新。
- **例外联动**：model exception_changed/batch 变化 → PageMeta::set_exception_summary(带例外路径列表)；PageMeta::open_editor_requested → 按 path 找 row → 编辑器；清除例外 → model->set_exception(row, nullopt)。
- **会话恢复**（构造时）：restore_last(last_format, last_out_root)；last_preset 非空且文件存在 → load+normalize+apply（preset 优先于 last_format）。closeEvent → save_session（last_format/last_preset/last_out_root + save_settings）。
- **alpha 预检**：模型任一行 `info.has_alpha` → PageOutput::set_batch_has_alpha(true)（无 probe 信息时保守 false）。
- set_current_page / set_offline_maps / add_paths 公开（smoke 与外部调用）。

---

## 3. 全局设计规格（文案/映射/颜色——冻结）

### 3.1 FileState 12 态映射（FileListModel::StateTextRole / PageRun 行文本）

| 态 | 文本 | 颜色 (QColor) |
|---|---|---|
| Queued | `排队` | #808080 |
| Probing | `探测` | #0088cc |
| Decoding | `解码` | #0088cc |
| Orienting | `旋转` | #0088cc |
| Coloring | `色彩` | #0088cc |
| Flattening | `合成` | #0088cc |
| Encoding | `编码` | #0088cc |
| Writing | `写入` | #0088cc |
| Done | `完成` | #22aa22 |
| Skipped | `跳过` | #999999 |
| Failed | `失败` | #dd3333 |
| Cancelled | `取消` | #999999 |

例外徽标：`⚑` 颜色 #dd8800。运行页行文本 = `文件名 —— 状态文本`（同色）。

### 3.2 其它冻结文案

- 冲突策略：`自动加序号`/`跳过`/`覆盖`；色彩目标：`保持原样`/`sRGB`/`Display P3`/`Adobe RGB (1998)`；模式：`转码`/`仅元数据`。
- 位深显示：`8 位`/`10 位`/`12 位`/`16 位`/`24 位`。
- Start 校验失败文案：`没有文件`、`未设置输出根目录`、`输出根目录必须是绝对路径`、`正在运行`。
- 顶部导航：`① 元数据`、`② 输出`、`③ 运行`；设置入口文案 `设置…`。
- GPS DMS 格式：`31°13'49.4"N 121°28'25.3"E`（度分秒一位小数）。

### 3.3 桌面/平台事实（实现必读）

- Qt 6.8.3 头文件在 `.toolchain/Qt/6.8.3/gcc_64/include`；插件已含 offscreen/xcb/wayland。
- 构建前必须 `source tools/env.sh`（QT_DIR/VCPKG_ROOT/CCACHE_DIR，否则 ccache 只读 R18）。
- 语料已入库：`tests/golden/base/` 16 文件、`edge/`、`meta/`（含损坏负例与 exif_full.jpg）。
- `pp_core` 消费者必须 whole-archive（CMake 已接线，新 target 不许漏）。
- **UI 层类进不了 ctest 单测**（U2 实测）：pp_test_* target 只链 pp_core（Qt6::Core 的 include/link），`src/ui/**` 与 mapwidget 的 Qt 部分无法在测试 target 中引用。UI 类的验证通道 = U10 `--ui-smoke`（含冻结断言）+ 各任务 `.cache/tmp` 临时自验程序（不入库）。**禁止**为此改 CMake（名字特例/glob 分叉均不授权）。
- OIIO 内存读：`ImageInput::open(name_hint, &spec, IOProxy*)` + `Filesystem::IOMemReader`（以实际头文件为准核对，事实纪律）。

---

## 4. 任务规格

> 通用：每任务开工先读 §1 + §2 相关节 + §3 + 本节；构建命令见 §1.12；报告 §7。文件头一律 `// SPDX-License-Identifier: GPL-3.0-or-later`。

### 4.1 W-A（并行 ×3）

**U1 core/thumbs + core/settings + platform/paths**
- 文件：`src/core/thumbs.{h,cpp}`、`src/core/settings.{h,cpp}`、`src/platform/paths.{h,cpp}`、`tests/unit/test_thumbs.cpp`、`tests/unit/test_settings.cpp`、`tests/unit/test_paths.cpp`。
- CMake：**无需编辑**（主对话已预置 paths.cpp 编入 pp_core 并从 UI glob 剔除；构建时验证生效即可）。
- 单测（ctest 名 thumbs/settings/paths）：thumbs——语料 `base/rgb8.png`（96px、rgba 尺寸正确、from_embedded=false、长边≤96、不放大）、`base/rgb16.tif`、`edge/` 损坏负例（probe_ok=false、error 非空）、`meta/exif_full.jpg`（probe_ok=true；thumb 非空即过，from_embedded 不做硬断言）；settings——默认值、写读往返全字段、未知键保留、注释行、原子写（不存在目录 → error 非空）；paths——`executable_dir()` 非空且存在、`settings_file().parent == data_dir()`、data_dir 存在且可写、`presets_dir()`/`logs_dir()` 在 data_dir 之下。
- 提示：embedded preview 经 Exiv2 `PreviewManager`（`ImageFactory::open` 只读打开）；OIIO 解码用内存 reader；重采样 `ImageBufAlgo::resample`（box）足够；灰→RGB、alpha→白合成。

**U2 mapwidget 模块（coord + providers + mapwidget）**
- 文件：`src/mapwidget/coord.{h,cpp}`、`src/mapwidget/providers.{h,cpp}`、`src/mapwidget/mapwidget.{h,cpp}`、`tests/unit/test_gcj02.cpp`。
- CMake：**无需编辑**（主对话已预置 coord.cpp 编入 pp_core 并从 UI glob 剔除；构建时验证生效即可）。
- MapWidget 本体不写单测（**U2 实测裁定，主对话 2026-09-19**：pp_test_* 只链 pp_core/Qt6::Core，`#include <QWidget>` 均不可编译——UI 层类进不了 ctest 单测）。U2 的 widget 侧证明 = `photopipeline` target 构建通过（编译/链接/moc）；**运行时断言（offline 构造 + grab 非空）归 U10 --ui-smoke**（§4.3 冻结）。
- 单测 test_gcj02（**仅依赖 coord.h，链 pp_core；纯数学，无 QApplication/无 widget include**）：北京(39.9042,116.4074)/上海(31.2304,121.4737)/广州(23.1291,113.2644)：wgs→gcj 偏移按**地面真实距离**断言（dlat×111320、dlon×111320×cos(lat 弧度)，hypot）∈ **300–700 m**（主对话修订 2026-09-19：U2 实测 555.5/481.8/621.7 m；原"300–600m 度距×111320"上限过紧，且该原始度量把经度度当全长米、物理失真；共识文档口径为"百米级非线性偏移"）；gcj(wgs(p)) 往返误差 <1e-6 度；中国外坐标恒等（如 (40.7128,-74.0060) 纽约）。

**U3 ui/paramform**
- 文件：`src/ui/paramform.{h,cpp}`。
- 无独立单测（谓词行为断言进 U10 的 --ui-smoke）；自验：构建 `photopipeline` target 通过 + 逐格式（8 个）在本地临时 main/测试小程序里实例化不崩（放 `.cache/tmp`，不入库，报告记录结论）。
- 必读：§2.6 全部行为条目 + m1-tasks §3.4 保留键约定（`__lossless` 由本表单写入，不外泄 values()）。

### 4.2 W-B（并行 ×6；前置 = W-A 全部完成）

**U4 ui/thumbnails + ui/filelistmodel**
- 文件：`src/ui/thumbnails.{h,cpp}`、`src/ui/filelistmodel.{h,cpp}`。
- 依赖：core/thumbs.h（U1）。规格 §2.7/§2.8。
- 自验：构建 photopipeline target 编译通过；用 `.cache/tmp` 临时程序（不入库）实例化模型 + Thumbnailer，灌 `tests/golden/base`，事件循环等 queue_empty（≤10s），断言 16 行 probe_ok 且 thumb 非空数 ≥14（损坏负例可失败）。

**U5 ui/page_output**
- 文件：`src/ui/page_output.{h,cpp}`。
- 依赖：paramform（U3）、encoders.h、presets.h、settings.h。规格 §2.9 全部。
- 自验（`.cache/tmp` 临时程序，报告记录）：默认态 config_base()==jxl/16bit；metadata_only 切换禁用正确；avif 选中 → 位深 {8,10}（svt-av1）/ {8,10,12}（libaom）；set_batch_has_alpha(true)+avif+10 位 → 后端自动 libaom；apply_preset/collect_preset 往返一致。

**U6 ui/settings_dialog + ui/presets_dialog**
- 文件：`src/ui/settings_dialog.{h,cpp}`、`src/ui/presets_dialog.{h,cpp}`。
- 依赖：core/settings.h（U1）、preset_io.h、library_versions()。规格 §2.10。

**U7 ui/page_meta**
- 文件：`src/ui/page_meta.{h,cpp}`。
- 依赖：mapwidget（U2）、metadata.h。规格 §2.11。
- 自验（临时程序）：rules() 装配矩阵（时间 Δ/时区/GPS/清除/标签/剥除/mtime 全组合）；`meta/exif_full.jpg` 时间预览非空；"从选中文件读取坐标"读出 exif_full.jpg 的 GPS（若无 GPS 则显示提示，两者皆可，报告记录事实）。

**U8 ui/exif_editor**
- 文件：`src/ui/exif_editor.{h,cpp}`。
- 依赖：metadata.h。规格 §2.12。允许直接调 Exiv2 API（§1.7 例外）。
- 自验（临时程序，offscreen）：构造于 `meta/exif_full.jpg`，树含 IFD0/Exif 节点 ≥5 项；模拟改一个值 → result().exif_edits 含该键；确定校验 errors=0。

**U9 ui/page_run**
- 文件：`src/ui/page_run.{h,cpp}`。
- 依赖：scheduler.h。规格 §2.13。
- 自验（临时程序）：灌 3 个假事件序列（Queued→Encoding→Done）→ 进度 1/3、吞吐>0、行状态更新；end_run 后按钮可用。

### 4.3 W-C（单任务；前置 = W-B 全部完成）

**U10 集成：mainwindow + main.cpp + ui_smoke + CMake 收口**
- 文件：`src/ui/mainwindow.{h,cpp}`（重写 M0 桩）、`src/main.cpp`（改）、`tests/ui_smoke.sh`（新）。
- CMake/CMakePresets（本任务独家授权）：
  1. 删除 `target_compile_definitions(photopipeline PRIVATE PP_M0_SMOKE)` 行；
  2. 删除 `add_test(NAME offscreen …)` 两行（旧桩冒烟，由 ui_smoke 取代）；
  3. 新增：`if(PP_BUILD_DEV) add_test(NAME ui_smoke COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen bash ${CMAKE_SOURCE_DIR}/tests/ui_smoke.sh ${CMAKE_BINARY_DIR}) endif()` + `set_tests_properties(ui_smoke PROPERTIES TIMEOUT 180)`；
  4. `CMakePresets.json`：增 `release-dev`（inherits release，`PP_BUILD_DEV: ON`，binaryDir 走默认 `${sourceDir}/build/release-dev`）+ 对应 build/test preset。
- main.cpp 改动（冻结顺序）：GUI 路径 = `set_qt_version_string` → QApplication → setStyle("Fluent") 尝试（失败 qInfo）→ `pp::platform::data_dir()` → `load_settings(settings_file())` → 日志级别解析（非法回退 info）→ `log_init(logs_dir(), level)` → `MainWindow w(settings)` → show → exec → `log_shutdown()`。删除 `#ifdef PP_M0_SMOKE` 块。`PP_BUILD_DEV` 下增 `--ui-smoke` 分支（须在 QApplication 之前拦截参数，同 --dev 模式）。
- **--ui-smoke 规格（冻结）**：`photopipeline --ui-smoke [--inputs DIR] [--shots DIR]`（缺省 inputs = `<仓库根>/tests/golden/base`，从可执行文件向上找 `.git`/`CMakeLists.txt` 定位仓库根；找不到 → stderr 报错退出 1）。流程：MainWindow(settings 默认) → resize(1440,900) → show → set_offline_maps(true) → add_paths(inputs) → 等缩略图队列空（QTimer 轮询 queue_empty，上限 10s）→ grab `01-meta.png` → 切页2 → `02-output.png` → select_format("avif") 等 300ms → `02b-output-avif.png` → 还原 jxl → 切页3 → `03-run.png` → **实跑**：out_root=`.cache/tmp/ui-smoke-out`（jxl，全量输入文件，conflict=overwrite）开始 → 轮询完成（上限 60s）→ `03b-run-done.png` → 对话框三连（构造→show→processEvents→grab→close，不 exec）：SettingsDialog `04-settings.png`、ExifEditor(首行文件) `05-exif-editor.png`、PresetsDialog(空列表) `06-presets.png` → stdout 末行 `UI-SMOKE OK shots=N pages=3` → 退出码 0。任何异常 → `UI-SMOKE FAIL <原因>` stderr + 退出码 1。**谓词断言**（顺带，失败即 FAIL）：jxl+无损 → selection.tech=="modular" 且 values() 含 distance==0.0（double）；jpeg quality_mode=="quality" → is_param_visible("quality") 且 !is_param_visible("distance")；tiff compression=="none" → !is_param_visible("deflate_level")。**地图断言**（失败即 FAIL）：切页 1 后 `MainWindow::findChild<pp::map::MapWidget*>()` 非空且 `grab()` 返回非空 QImage（offline 模式，全流程不得发出任何网络请求）。--shots 未给 → 只跑不存图（CI 模式）。
- `tests/ui_smoke.sh`：`#!/usr/bin/env bash; set -euo pipefail`；参数1=BUILD_DIR（默认 `build/release-dev`）；`QT_QPA_PLATFORM=offscreen "$BUILD_DIR/photopipeline" --ui-smoke --inputs "$(dirname "$0")/../golden/base"`；tail 校验输出含 `UI-SMOKE OK`；echo `UI-SMOKE pass`。
- 构建/验证（本任务专属）：`cmake --preset release-dev -DVCPKG_MANIFEST_INSTALL=OFF && cmake --build --preset release-dev -j` → `ctest --test-dir build/release-dev --output-on-failure` 全绿（旧 20 条 - offscreen + 新 thumbs/settings/paths/gcj02/ui_smoke）→ `--ui-smoke --shots .cache/ui-review` 产出 7 张 PNG 并报告路径。
- Git：分两次提交 `M1b-U10: integration` / `M1b-U10: ui smoke + presets`。

### 4.4 迭代与收口（R2..Rn / U-FIN）

- **U-FIX-\<n\>**：主对话根据用户审查意见编写（规格追加于 §9），格式 = 问题清单 + 每问题精确改动指令（文件/行为/文案），subagent 只执行清单。回归要求：--ui-smoke 全绿 + 受影响页截图重出。
- **U-FIN**（= T14b）：① `ctest --test-dir build/release-dev` 全绿；② 手动走查清单逐项执行并记录（§8 清单）；③ README UI 章节补写（构建/运行/截图说明）；④ 报告 → 主对话落盘 `docs/m1b-report.md`。

---

## 5. 构建增量所有者序列（M1b 全部）

| 顺序 | 任务 | 改动 | 状态 |
|---|---|---|---|
| 1 | U1 | pp_core glob + `src/platform/paths.cpp`；UI glob REMOVE_ITEM 剔除 | **已由主对话预置**（U1 只验证不编辑） |
| 2 | U2 | pp_core glob + `src/mapwidget/coord.cpp`；UI glob REMOVE_ITEM 剔除 | **已由主对话预置**（U2 只验证不编辑） |
| 3 | U10 | 删 PP_M0_SMOKE 定义；offscreen ctest → ui_smoke ctest（dev-gated）；CMakePresets `release-dev` | 待 U10 |

其余任务**一律不得改 CMake/CMakePresets**（源文件 glob 自动收编）。预置原因：U1/U2 并行编辑同一 CMakeLists 有覆盖竞态，由主对话一次落盘。

---

## 6. 依赖与数据流（UI 视角，冻结）

```
MainWindow ──owns──> FileListModel + Thumbnailer ──calls──> pp::make_thumbnail (core)
          ──owns──> PageMeta / PageOutput / PageRun / dialogs
PageOutput ──owns──> ParamForm ──reads──> pp::static_formats / introspect_backends / probe_bitdepth_support
PageMeta  ──embeds─> MapWidget (mapwidget/)
PageRun   <──events── MainWindow (queued copies) <── Scheduler::EventCb (worker threads)
start: PageOutput::config_base + PageMeta::rules + settings -> RunConfig -> Scheduler
```

禁止：Page 之间直接互指（一律经 MainWindow 转发/装配）；UI 持有 IEncoder；UI 调 run_one_file（Scheduler 是唯一入口）。

---

## 7. 验收与报告格式

**出口准则（U-FIN 核验）**＝ m1-tasks §6 批次 2 表（9–12 四项）：
| # | 准则 | 证据 |
|---|---|---|
| 9 | offscreen UI 冒烟绿 | ctest ui_smoke / tests/ui_smoke.sh |
| 10 | 手动走查清单全勾（§8） | 走查记录 |
| 11 | 参数表单谓词联动正确（无损锁定/技术互斥/后端切换） | --ui-smoke 谓词断言 + 走查 |
| 12 | 运行期锁定与取消可用（G5） | 走查 + 03b 截图 |

**报告格式（每 subagent 最终输出，≤150 行）**：
```
## REPORT
status: SUCCESS|PARTIAL|FAILED|BLOCKED
task: M1b-U<n> <名称>
tasks-done: [...]
tasks-skipped: [... (原因)]
api-deltas:
- <Qt/库/文件>: <任务书假设> → <事实> (来源: <命令/文件:行>)
artifacts:
- <路径> (一句话；标注 PP-FROZEN 复制)
frozen-check: <是否逐字节复制 §2 头文件>
build: <命令> → <退出码>
selftest: <临时自验程序结论>
commits: <hash + message>
next-needed:
- <需主对话决策事项>
```

---

## 8. 手动走查清单（U-FIN 逐项，R 轮参考）

1. 拖放目录 → 递归收集 + 缩略图渐入 + 不支持计数；搜索过滤；移除/清空。
2. 双击文件 → 编辑器：EXIF 树/XMP/三态时间 GPS；保存 → 列表 ⚑ 徽标；例外摘要联动。
3. 元数据页：时间偏移 Δ/时区预览正确；GPS 地图选点回填（联网）/离线降级提示；读取选中坐标。
4. 输出页：8 格式切换参数区重建；jxl 无损 → 自动 modular + distance 锁 0；jpeg quality_mode 切换；tiff 压缩联动；avif 位深随后端变化；10bit+alpha 自动 libaom；仅元数据置灰项。
5. 预设：保存/载入/删除/另存为；重启后上次会话恢复（格式/输出目录/预设）。
6. 设置：全部字段持久化；worker/预算/底色/日志级别生效（日志文件验证）；地图提供方切换。
7. 运行：开始 → 锁定（G5）+ 文件徽标逐态变化 + 吞吐；取消 → 剩余标取消；结束摘要 + 打开输出目录/日志。
8. 转码实跑抽验：输出文件可被 OIIO 读回（--dev 或文件管理器）。

## 9. 迭代任务记录（主对话维护，R2 起追加）

（空——首轮未开始）
