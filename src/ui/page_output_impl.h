// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 输出页 Impl 共享私有头（B.3 余量 TU 拆分产物；M7-R2.1-F3）
//
// 契约（deep-review-0.3.2.md R-155/R-189 族 + build/m7r2-e2/handoff.md ⑥-1）：
// page_output.cpp 原 2300 行 god-Impl 单 TU —— 按所有权拆为
//   page_output.cpp（页公开面 + 注册表 + 状态/参数机）、
//   output_page_build.cpp（4 卡控件束：build_ui/make_*/build_*_card/wire/attach_card_menu）、
//   output_template_ui.cpp（模板段：template_*/show_vars_menu/sync_split_switch/
//   apply_split_to_template/update_template_example）。
// 拆分口径 = **行为零变化**（方法体逐字搬移；objectName/信号/公开面逐位不变）；
// 本头 = 原匿名命名空间共享件（补 inline）+ Impl 成员声明（mainwindow_impl.h 同款先例）。
// **不入 PP-FROZEN 面**（冻结面仍是 ui/page_output.h）。
#pragma once

#include "ui/page_output.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStorageInfo>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "codecs/encoders.h"
#include "codecs/heif_profile.h"
#include "core/error_text.h"
#include "core/fsops.h"
#include "core/params.h"
#include "ui/byte_text.h"
#include "ui/contextmenu.h"
#include "ui/format_policy.h"
#include "ui/format_selection_model.h"
#include "ui/formkit.h"
#include "ui/icons.h"
#include "ui/leading_indent.h"
#include "ui/paramform.h"
#include "ui/theme.h"

namespace pp::ui {

using formkit::FieldWidth;

// ---------------------------------------------------------------- 格式按钮表

struct FormatEntry {
    const char *id;
    const char *label;
};

// v6 原型（mockups-v2/output-dark.html）的磁贴网格顺序（4 列行优先）
const FormatEntry kFormats[] = {
    {"jpeg", "JPEG"}, {"webp", "WebP"}, {"jxl", "JPEG XL"}, {"heif", "HEIF"},
    {"avif", "AVIF"}, {"png", "PNG"},   {"tiff", "TIFF"},   {"bmp", "BMP"},
};
constexpr int kFormatCount = static_cast<int>(sizeof(kFormats) / sizeof(kFormats[0]));
constexpr int kFormatColumns = 4; // 2×4（§7.2）

// §2.9.2：逐格式高质量档默认位深 —— R-238（M7-R2D2）：三套口径收敛，本页消费**单源**
// `pp::default_bitdepth_for`（core/pipeline.h；--dev / bench / UI 装配链同一份）。
// 原 12 行逐字段复制删除（值域：jpeg 8 / jxl 16 / png 16 / tiff 16 / webp 8 / bmp 24(bpp) /
// heif·avif 10 / 其余 8）。

inline bool contains(const std::vector<int> &v, int x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

inline bool contains_str(const std::vector<std::string> &v, const std::string &s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// R-191（M7-R2E2）：parse_csv_ints / 位深交集 / 后端归一抽入 ui/format_policy.h 纯核单源

// §2.9.2：可选位深 = 静态允许集 ∩ 运行期探测（非 libheif 格式即静态集）。
// §2.9.7：探测结果按 (format, backend) 进程内缓存一次（GUI 线程独占，无需锁）。
inline std::vector<int> allowed_bitdepths(const pp::FormatDef &f, const std::string &backend) {
    if (f.id != "heif" && f.id != "avif")
        return f.bitdepths;
    static std::map<std::pair<std::string, std::string>, std::vector<int>> cache;
    const auto key = std::make_pair(f.id, backend);
    auto it = cache.find(key);
    if (it == cache.end()) {
        const std::vector<int> probed =
            fmtpolicy::parse_csv_ints(pp::probe_bitdepth_support(f.id, backend));
        std::vector<int> allowed = fmtpolicy::intersect_bitdepths(f.bitdepths, probed);
        it = cache.emplace(key, std::move(allowed)).first;
    }
    // 探测无可用结果（异常环境）→ 回退静态集，UI 至少可用（引擎侧仍会明确报错）
    return it->second.empty() ? f.bitdepths : it->second;
}

// heif/avif：静态表后端带 runtime_introspected 标志（FormatDef 本身无该字段）
inline bool runtime_introspected_format(const pp::FormatDef &f) {
    return std::any_of(f.backends.begin(), f.backends.end(),
                       [](const pp::BackendDef &b) { return b.runtime_introspected; });
}

// §2.9.7：heif/avif 内省在首次选中时执行并缓存；顺序归一为静态表顺序，
// 使默认后端与引擎默认（make_encoder(fmt,"") → svt-av1）一致。
inline std::vector<pp::BackendDef> effective_backends(const pp::FormatDef &f) {
    if (!runtime_introspected_format(f))
        return f.backends;
    static std::map<std::string, std::vector<pp::BackendDef>> cache;
    auto it = cache.find(f.id);
    if (it == cache.end()) {
        const std::vector<pp::BackendDef> live = pp::introspect_backends(f.id);
        std::vector<pp::BackendDef> ordered = fmtpolicy::merge_backend_order(f.backends, live);
        it = cache.emplace(f.id, std::move(ordered)).first;
    }
    return it->second.empty() ? f.backends : it->second;
}

inline bool has_backend(const std::vector<pp::BackendDef> &list, const std::string &id) {
    return std::any_of(list.begin(), list.end(),
                       [&](const pp::BackendDef &b) { return b.id == id; });
}

// 注意（M4-T13b / D-W3-1）：返回的是 **list 容器内**的指针 —— 调用方必须保证 list 的
// 生存期覆盖该指针的全部使用（effective_backends 按值返回，故须先绑定到具名局部）。
inline const pp::TechDef *tech_by_id(const std::vector<pp::BackendDef> &list,
                                     const std::string &backend, const std::string &tech) {
    for (const pp::BackendDef &b : list) {
        if (b.id != backend)
            continue;
        for (const pp::TechDef &t : b.techs) {
            if (t.id == tech)
                return &t;
        }
        return b.techs.empty() ? nullptr : &b.techs.front();
    }
    return nullptr;
}

// §3.2 组合框映射（顺序即冻结文案顺序）
// R-190（M7-R2E2）：冲突/色彩下拉的**唯一真相 = itemData**（构造时 addItem 文本 + 枚举值）。
// 原「index 映射表」（conflict_from_index/to_index、color_from_index/to_index 四件）退役 ——
// 读回走 currentData()、落值走 findData()，调序不再静默串档（deep-review R-190/E3-11）。

// R-254（M7-R2.1-F3）：原 human_bytes（与 mainwindow_status 的 natural_bytes 逐字同款的第二份
// 实现）删除 —— 字节自然语读数单源 = ui/byte_text.h（输出逐字节不变；真值表入 test_smoke）。

// ---------------------------------------------------------------- 小控件

// 单行省略标签（§9.3「全界面禁止文案折行（超长省略号）」；与 mainwindow/page_meta 同口径）
// R-020/R-320（M7-WP4）：ElidedLabel 的 7 份逐字拷贝收敛到 formkit 唯一实现
// （本文件原 30+ 行自拷类删除；行为逐位等价 —— 全文 + ppFullText + tooltip + 省略号）。
using ElidedLabel = formkit::ElidedLabel;

// ---------------------------------------------------------------- 页面样式
// tokens 单源 = ui/theme.h；本页只把 tokens 铺到自己的控件上（与 PageMeta/PreviewPanel 同口径）。
// 选择器一律以 pp-* objectName / ppField 动态属性为锚，避免误伤其它页面控件。
// 注：磁贴（formkit::CheckableCard）、开关（formkit::SwitchControl）、分段（SegmentedControl）、
// 胶囊（PillBadge）与全部行内标签/值/注释（ppFk 钩子）都不在此处 —— 它们自带令牌副本或走
// theme.h 的 formkit_qss 全局规则（C1 单源；A6：本页不再有任何硬编码色值）。
constexpr const char *kFieldProperty =
    theme::kFkProperty; // UX-6⑤/R-054：三条钩子轨并入 ppFk（旧 ppField 轨退役）

inline QString page_qss(const theme::Tokens &t) {
    // UX-6⑥（M7-WP4）：跨页同形规则 = theme::field_qss（输入/按钮/文字链/折叠/提示/ppInvalid），
    // 页专规则（格式页签/「＋ 添加格式…」/容器透明）= theme::page_output_qss —— 页内不再拼 QSS。
    return theme::field_qss(t) + theme::page_output_qss(t);
}

// ---------------------------------------------------------------- 页面状态

// ---------------------------------------------------------------------------
// UX-10.1：HEIF/AVIF「编码档位 (Profile)」核心行（docs/ux-review-14.md §10.1 口径）
//   profile = **编码档位**（HEVC 的 Main/Main10/Main12/Main Still Picture、AV1 的
//   Main/High/Professional），决定解码兼容性；**不是**色彩 ICC profile。
//   libheif 的两个插件都不暴露 profile 参数 —— 档位由插件按位深/色度/静止图自动推导
//   （见 codecs/heif_profile.h 的规则复刻，同一份真相）。本行把该推导**显式呈现**。
//   选定点落盘键 = pp::kHeifProfileParamKey（"__heif_profile"；保留键：不落预设、不进
//   未知参数校验、不进日志快照 —— params.h §3.4 约定）。
// ---------------------------------------------------------------------------
inline bool is_heif_family(const std::string &id) { return id == "heif" || id == "avif"; }

// 页签页面的格式 id 动态属性（M7-R2.4）：页签文案 / 现役解析 / 序对齐都按**页面自身 id**
// 取，不再用 "页签下标 == sel.at(下标)" 的隐式同序假设（该假设在「先选 jxl 再选 jpeg」时
// 立即破裂：sel 按磁贴序排序，QTabWidget 按插入序追加 → JPEG 标题下显示 JXL 内容）。
constexpr const char *kPageFormatIdProperty = "ppFormatId";

struct ProfileOption {
    pp::HeifProfile profile;
    const char *label; // 中文标签（含兼容性说明，§10.1 逐字口径）
};

// 选项表（docs/ux-review-14.md §10.1）：HEIF 五档 / AVIF 四档，首项恒为「自动」
inline const ProfileOption *heif_profile_options(bool avif, int &count) {
    static const ProfileOption kHeif[] = {
        {pp::HeifProfile::Auto, "自动"},
        // HEIF-01：8 位静止图在 x265 里恒为 Main Still Picture；此选项值仍叫 Main，
        // 但 enc_heif/heif_profile_compatible 把 Main 与 MainStill 视为同一可用族，
        // 不再出现“选了必失败”。（标签的中文修正在后续 UI 文案批处理中统一。）
        {pp::HeifProfile::Main, "Main（8 位 · 兼容性最好）"},
        {pp::HeifProfile::Main10, "Main10（10 位 · Intra）"},
        {pp::HeifProfile::Main12, "Main12（12 位）"},
        {pp::HeifProfile::MainStill, "Main Still Picture（8 位 · 静止图）"},
    };
    static const ProfileOption kAvif[] = {
        {pp::HeifProfile::Auto, "自动"},
        {pp::HeifProfile::Main, "Main（8/10 位 · 4:2:0/4:0:0）"},
        {pp::HeifProfile::High, "High（8/10 位 · 4:4:4）"},
        {pp::HeifProfile::Professional, "Professional（12 位 / 4:2:2）"},
    };
    if (avif) {
        count = int(std::size(kAvif));
        return kAvif;
    }
    count = int(std::size(kHeif));
    return kHeif;
}

// 档位 → 允许位深（联动限幅；空 = 不限）。口径与 heif_profile.h 的 derive 规则一致：
// HEVC 只看位深（Main/MainStill→8、Main10→10、Main12→12）；AV1 的 Professional 还可以由
// 4:2:2 命中（不限位深），故放宽到 {8,10,12}，最终判定交给 heif_profile_compatible。
inline std::vector<int> profile_allowed_bitdepths(pp::HeifProfile p, bool avif = false) {
    switch (p) {
    case pp::HeifProfile::Auto:
        return {};
    case pp::HeifProfile::Main:
        // AV1 Main 本含 8/10 位；HEVC Main 只对应 8 位（Main Still 也是 8 位）。
        return avif ? std::vector<int>{8, 10} : std::vector<int>{8};
    case pp::HeifProfile::MainStill:
        return {8};
    case pp::HeifProfile::Main10:
        return {10};
    case pp::HeifProfile::Main12:
        return {12};
    case pp::HeifProfile::High:
        return {8, 10};
    case pp::HeifProfile::Professional:
        return {8, 10, 12};
    }
    return {};
}

// 兼容性提示（§8「禁用不静默灰」/ §10.1-4）：非 Main 档位给 warn 色说明
inline QString profile_compat_hint(pp::HeifProfile p) {
    switch (p) {
    case pp::HeifProfile::Main:
    case pp::HeifProfile::Auto:
        return QString();
    case pp::HeifProfile::MainStill:
        return PageOutput::tr("Main Still Picture：8 位静止图档，需较新解码器");
    case pp::HeifProfile::Main10:
        return PageOutput::tr(
            "Main10：10 位，x265 静止图写 Main10 Intra 档（部分工具显示为 Main）");
    case pp::HeifProfile::Main12:
        return PageOutput::tr("Main12 需较新解码器（12 位）");
    case pp::HeifProfile::High:
        return PageOutput::tr("High 档（4:4:4）兼容性有限，需较新解码器");
    case pp::HeifProfile::Professional:
        return PageOutput::tr("Professional 档（12 位 / 4:2:2）兼容性最差，老解码器不可用");
    }
    return QString();
}

struct FormatState {
    pp::OutputFormatSpec spec; // 首次建页签的初值（无则表内默认）
    QWidget *page = nullptr;
    ParamForm *form = nullptr;
    QComboBox *bitdepth = nullptr;
    QLabel *bitdepth_note = nullptr; // 格式能力约束说明（行内 10px 注释位；如 "JPEG 仅支持 8 位"）
    ElidedLabel *alpha_hint = nullptr;
    ElidedLabel *meta_hint = nullptr; // 格式级元数据承载能力警示（BMP：容器不承载 EXIF/XMP）
    // ---- UX-10.1：HEIF/AVIF 编码档位核心行（位深行之上；仅这两格有）----
    QComboBox *profile = nullptr;   // pp-profile-combo（首项「自动」）
    QLabel *profile_note = nullptr; // 推导结果 / 兼容性提示（warn 色）
    bool profile_user_set = false;  // 用户手选过档位（反向联动改位深时不覆盖手选项）
    // R-192（M7-R2.1-F3）：后端覆盖标记 **per-format**（原全局单槽 user_backend_override +
    // last_backend 跨格式共享 —— 换格式即误判"用户改过"，deep-review R-192/E3-14 假阳假阴）。
    bool backend_user_set = false; // 用户手动改过**本格式**后端（set_batch_has_alpha 一并清除）
    std::string last_backend;      // 本格式最近一次已知后端（区分用户手动改动的基准）
    // R-135（M7-R2D2）：表单不认的键（预设带入的未知键 / 跨技术键）—— 落值时记存，
    // snapshot/spec_of 原样带回，load→save 往返不丢（form values 同名键覆盖之）。
    pp::ParamSet extra;
    // ---- M8-B2：运行前预检错误清单（每格式 tab；规则引擎与 ParamForm 同一份求值）----
    // 错误非空 ⇒ ready_to_start() 拒绝开始（中文、逐格式前缀；批量只报一次）并聚焦第一错行。
    QWidget *rule_error_box = nullptr;    // pp-rule-error-box-<id>
    QLabel *rule_error_list = nullptr;    // pp-rule-error-list-<id>
    std::vector<std::string> rule_errors; // 当前错误（中文）
};

// R-189：FormatSelection 的规范顺序 = 磁贴顺序（kFormats 表序；与原 tile_index 排序同键）
inline std::vector<std::string> tile_order() {
    std::vector<std::string> order;
    order.reserve(std::size_t(kFormatCount));
    for (const FormatEntry &e : kFormats)
        order.push_back(e.id);
    return order;
}

// R-189/B.3（M7-R2.1-F3）：原匿名命名空间的 Impl 提升为具名共享（跨 TU 拆分所需；
// 数据成员/方法签名逐字不变，方法体分落三个 TU）。
struct OutputPageImpl {
    PageOutput *q = nullptr;
    theme::Tokens tokens{};

    bool metadata_only = false;
    bool batch_has_alpha = false;
    // R-192（M7-R2.1-F3）：后端覆盖标记 per-format（FormatState::backend_user_set /
    // last_backend；原全局两槽 user_backend_override/last_backend 退役 —— 跨格式共享
    // 使"用户改过"判定假阳假阴，deep-review R-192/E3-14）。
    // R-066（M7-R2D2）：预设应用路径**跳过** avif+alpha 预选 —— 预设里记录的后端是权威值，
    // 落值后被 evaluate_avif_alpha_preselect 静默改写 svt-av1→libaom 会让 load→save 往返丢字段
    // （deep-review R-066）。置位期间预选求值直接返回 false（override 语义位）。
    bool in_preset_apply = false;
    // R-189（M7-R2.1-F3）：「已选格式」四份表示（顺序表 / 磁贴勾选 / 页签序 / 现役）收敛为
    // fmtsel::FormatSelection 唯一真相（原 active_id + selected 双成员退役）；变更后经
    // reproject_format_selection() 单次投影（4 手写 sync 坍缩为 reproject）。
    fmtsel::FormatSelection sel{tile_order()};
    std::map<std::string, FormatState> states;
    int input_count = 0; // 预估卡输入面（勾选文件数；MainWindow 喂入）

    // 卡 ① 输出格式
    QFrame *format_card = nullptr;
    formkit::SegmentedControl *mode_seg = nullptr;
    ElidedLabel *mode_note = nullptr;
    QWidget *tiles_host = nullptr;
    formkit::CheckableCard *tiles[kFormatCount] = {};
    formkit::PillBadge *format_pill = nullptr;
    ElidedLabel *format_hint = nullptr;

    // 卡 ② 全局
    QFrame *global_card = nullptr;
    QLineEdit *out_root_edit = nullptr;
    QLineEdit *base_edit = nullptr; // R-319：镜像基准目录（可选；空 = 现行为）
    // M7-R2.1-F4 色彩四选项（UX-14 C1–C4）：opt-in 控件，默认值 = 现行为（0/false）
    QComboBox *intent_combo = nullptr; // C3 渲染意图（0=相对比色+黑点补偿…3=饱和度）
    QComboBox *gamut_combo = nullptr;  // C1 色域映射（0=无（硬裁）/1=软膝/2=Lab 明度收敛）
    formkit::SwitchControl *dither8_switch = nullptr; // C2 8bit 量化抖动
    formkit::SwitchControl *linear_switch = nullptr;  // C4 线性光重采样（仅显示面）
    QPushButton *browse_button = nullptr;
    QComboBox *conflict_combo = nullptr;
    QComboBox *color_combo = nullptr;
    formkit::SwitchControl *split_switch = nullptr;
    QLineEdit *template_edit = nullptr;
    QPushButton *vars_button = nullptr;
    ElidedLabel *template_example = nullptr;

    // 卡 ③ 格式参数
    QFrame *param_card = nullptr;
    QTabWidget *tabs = nullptr;
    QToolButton *add_format_button = nullptr;
    formkit::PillBadge *changed_pill = nullptr;
    ElidedLabel *empty_hint = nullptr; // 未选任何格式时的空态提示（M7-R2.5）

    // 卡 ④ 预设与预估
    QFrame *preset_card = nullptr;
    QComboBox *preset_combo = nullptr;
    QPushButton *preset_save = nullptr;
    QPushButton *preset_saveas = nullptr;
    QPushButton *preset_delete = nullptr;
    ElidedLabel *preset_hint = nullptr;
    ElidedLabel *est_output = nullptr;
    ElidedLabel *est_size = nullptr;
    ElidedLabel *est_time = nullptr;
    ElidedLabel *est_disk = nullptr;
    QTimer *disk_timer = nullptr; // R-228：剩余磁盘查询去抖（statvfs 不挂逐键 textChanged）
    QString color_tip_base;       // R-318：色彩目标 tooltip 基线（禁用态换原因、启用态恢复）

    // ------------------------------------------------------------ 查询

    FormatState *state_of(const std::string &id);

    const FormatState *state_of(const std::string &id) const;

    bool is_selected(const std::string &id) const;

    std::string backend_id(const std::string &id) const;

    bool lossless_of(const std::string &id) const;

    int bitdepth_of(const std::string &id) const;

    pp::ColorTarget color_target() const;

    pp::ConflictPolicy conflict() const;

    QString template_text() const;

    // ------------------------------------------------------------ 界面构建

    void build_ui();

    // 卡壳（§4.3 r6 + --card-bd 描边/阴影走 theme QSS；卡头 = 标题 12/700 + 中性 pill + hint）
    QFrame *make_card(QWidget *parent, QVBoxLayout **body_out);

    struct CardHeader {
        QWidget *row = nullptr;
        QLabel *title = nullptr;
        formkit::PillBadge *pill = nullptr;
        ElidedLabel *hint = nullptr;
    };

    CardHeader make_header(QWidget *card, const QString &title, const QString &pill_name,
                           const QString &hint_name);

    // 卡体（R1：上内边距 10、下 12；行 pitch 由 formkit 行容器承担 → 行间无额外 gap）
    struct Body {
        QWidget *host = nullptr;
        QVBoxLayout *box = nullptr;
    };

    Body make_body(QWidget *card);

    QPushButton *make_button(QWidget *parent, const QString &text, const QString &object_name,
                             bool primary);

    // 行内复合字段（flex 轨）：[本体 flex][附件…] —— 复合字段容器撑满轨，无浮空死区（R2）
    QWidget *make_flex_field(QWidget *parent, const QList<QWidget *> &widgets);

    // 卡 ①：输出格式（转换模式分段 + 说明行 ｜ 8 格式磁贴 2×4）
    QWidget *build_format_card(QWidget *parent);

    // 卡 ②：全局（一行一义 6 行）
    QWidget *build_global_card(QWidget *parent);

    // 卡 ③：格式参数（页签 + 「＋ 添加格式…」；每页签一行一义六行）
    QWidget *build_param_card(QWidget *parent);

    // 卡 ④：预设与预估（当前预设 + 另存…/删除/保存 ｜ 预估 kv 2 行）
    QWidget *build_preset_card(QWidget *parent);

    static QString template_tooltip();

    // ------------------------------------------------------------ 信号接线

    void wire();

    // UX-1（M7-R2D2）：卡片右键菜单 —— 公共件 ui/contextmenu（与 page_run 同一口径；
    // items 惰性构造，右键时才求值）。
    void attach_card_menu(QWidget *host,
                          const std::function<std::vector<contextmenu::Item>()> &items);

    // 变量表菜单（点击变量即插入到模板光标处；§8：变量语法必有
    // tooltip，本菜单是同一份说明的可点形态）
    void show_vars_menu();

    // ------------------------------------------------------------ 磁贴 / 页签

    void toggle_format(const std::string &id, bool on, bool user);

    // R-189（M7-R2.1-F3）：「已选格式」四份表示的**投影单点** —— 原 4 个手写 sync
    // （sync_tiles / sync_tab_current / update_format_count / refresh_changed_pill，另加
    // 页签切换路径独享的 refresh_tab_labels）在每处变更后手工配平；现真相 = sel 单源，
    // 变更后统一本函数一次投影（同值幂等重放，严格度不变）。
    void reproject_format_selection();

    static int tile_index(const std::string &id);

    void sync_tiles();

    void sync_tab_current();

    // 页签序对齐（M7-R2.4）：把每个已选格式的页面按 sel.items() 的规范序重排 —— 修正
    // 「磁贴序 vs 插入序」两套顺序，保证页签标题/内容/现役三者永远同源。add/remove/
    // preset 重建后都经 reproject 调用一次（同值幂等）。
    void reconcile_tab_order();

    // 「＋ 添加格式…」可用态：8 个全选时光标悬停给出原因（不静默无响应）。
    void update_add_format_enabled();

    // 页签内的 ParamForm：只有**当前**页签的表单命名为 pp-param-form（冒烟/走查钩子），
    // 其余为 pp-param-form-<id> —— findChild 命中当前格式那一个。
    void update_form_object_names();

    void build_tab(const std::string &id);

    void remove_tab(const std::string &id);

    // 取消勾选前把当前控件状态快照进 spec（重选时还原用户编辑）
    void snapshot_state(const std::string &id);

    // 页签文案 = 格式名（§7.2 页签）。
    // UX-9（§16-1 口径）：**只在选项卡标题里去掉"多少位"** —— 位深控制功能原样保留
    // （pp-bitdepth-row / -combo / -note、预设往返、evaluate_avif_alpha_preselect 联动全不动）。
    // 位深变化仍会调 refresh_tab_labels()，但它只重写格式名（保留入口以便将来换文案）。
    QString format_tab_label(const std::string &id) const;

    // 卡头中性 pill：当前页签格式「已改 N」（值 ≠ 表内默认的参数项数；修 2-07 计数用中性档）
    // R-266：现役游离（集合空）时不显示陈旧计数 —— 现役判据 = sel.active_in_set()（R-189）。
    void refresh_changed_pill();

    // 页签计数：值 ≠ 表内默认 的参数项数
    int changed_param_count(const std::string &id) const;

    void refresh_tab_labels();

    // UX-9：页签等宽的落地点（addTab/removeTab/改文案后都要走一次）。
    // 口径 = `QTabBar::setExpanding(true)`：页签**平摊**页签条宽度 —— 天然等宽，且不留
    // 右侧空白带（自绘 tabSizeHint 定宽的方案会让页签条右端露出平坦色块，gui_r2 的
    // tabstrip/ctl-fill 探针判红；且 sizeHint 依赖自身 width() 会成布局反馈环）。
    // 实测（build/m7-wp6）：本口径下 `tab-equal` 与 `tabstrip/ctl-fill` 两条探针同绿。
    void apply_tab_width();

    void update_format_count();

    // ------------------------------------------------------------ 参数状态

    void on_param_selection_changed(const std::string &id);

    void on_bitdepth_changed(const std::string &id);

    // ------------------------------------------------------------ M8-B2 运行前预检
    // 上下文（位深/无损/后端/技术）→ ParamForm 规则引擎；错误清单 = 每格式 tab 底部 +
    // tab 计数徽标；`开始运行` 拒绝与「聚焦第一错行」都走这一份求值（C5/C6 同源）。
    void sync_rule_context(const std::string &id);
    void update_rule_errors(const std::string &id);
    void update_all_rule_errors();
    void focus_first_error();

    // ------------------------------------------------------------ UX-10.1 编码档位

    pp::HeifProfile current_profile(const std::string &id) const;

    // 色度（与 enc_heif 同键 "chroma"；0=4:0:0 1=4:2:0 2=4:2:2 3=4:4:4 —— heif_profile.h 口径）
    int current_chroma(const std::string &id) const;

    // 档位选定 → 位深/色度限幅（位深限幅在 set_bitdepth_options 里与格式能力求交；
    // 色度限幅落 ParamForm 的枚举行视图（UX-10.1 残余，M7-R2D2）：置灰 + 禁用原因，不静默
    // 改写用户的色度选择 —— 不一致由 heif_profile_compatible 的中文错误承担（一控件一义）。
    void on_profile_changed(const std::string &id);

    // UX-10.1 残余：档位 → 色度限幅联动（选定 Main 不给 4:4:4 等）。允许集 = 存在某个允许位深
    // 使 heif_profile_compatible(want, d, c, still=true) 通过 —— 与校验同一份规则源
    // （codecs/heif_profile.h）；「自动」不限幅（空集 = 全可选）。
    void apply_chroma_limits(const std::string &id);

    // 档位注释位：「自动」显示插件自选的**推导结果**；手选档位与当前设置不一致时给中文错误；
    // 非 Main 档位给 warn 色兼容性提示（§8「禁用不静默灰」）。
    // 色调三档（色值一律取自 tokens，本页不写色值字面量）：
    //   * 非法组合 → ppInvalid="true"（theme::field_qss 的红字轨，已有钩子）；
    //   * 兼容性提示 → X5 公共 warn 钩子 theme::kFkWarn（M7-R2D2 落地；原 QPalette 自拼被
    //     kFkHint 的 --txt3 规则覆盖，两轨抢色 —— 现统一走 QSS 钩子轨）；
    //   * 其余 → kFkHint（--txt3）。
    // 基类型显式 uint8_t（三值枚举；performance-enum-size，值域/语义零变化）。
    enum class NoteTone : std::uint8_t { Hint, Warn, Error };
    void update_profile_note(const std::string &id);

    // ------------------------------------------------------------ 位深

    void repopulate_bitdepths(FormatState &st, const std::vector<int> &allowed, int wanted);

    // forced > 0：apply_preset 指定值（不在交集则退回默认规则）；
    // keep_if_valid：后端切换时当前位深仍有效则保留。返回是否发生变化。
    bool set_bitdepth_options(const std::string &id, int forced, bool keep_if_valid);

    // ------------------------------------------------------------ 落值 / 读取

    // 把 OutputFormatSpec 落到该格式的页签控件（无信号：ParamForm 的 set_* 是程序化接口）
    void apply_state_to_form(const std::string &id, const pp::OutputFormatSpec &spec);

    pp::OutputFormatSpec spec_of(const std::string &id) const;

    pp::ParamSet default_params_for(const std::string &id, const std::string &backend,
                                    const std::string &tech, bool lossless) const;

    // ------------------------------------------------------------ avif + alpha / 仅元数据

    void update_alpha_hint(const std::string &id);

    // §9.1 [高]：**触发条件求值**（不依赖 set_batch_has_alpha 单点触发）。
    // 条件：avif ∧ batch_has_alpha ∧ 位深==10 ∧ 后端==svt-av1 ∧ 用户未手动改过后端。
    // R-066：预设落值路径（in_preset_apply）不预选 —— 预设后端为权威值，不静默改写。
    bool evaluate_avif_alpha_preselect(const std::string &id);

    void update_metadata_only_state();

    // ------------------------------------------------------------ 模板 / 示例

    // R-136/R-321/R-322（M7-R2D2）：split_by_format 的派生判据**单源** = pp::template_has_format
    // （core/pipeline.h；含 $format 段即 true）—— 本页原「逐段比对」副本与 settings_dialog 的
    // 复制件同批退役，CLI/GUI/持久化三处判据合一（R-145 根源之一）。
    static bool template_has_format(const QString &tmpl);

    void sync_split_switch(const QString &tmpl);

    void apply_split_to_template(bool on);

    // 输出示例（注释行）：自然语、单行、防溢出（修 2-14 统一 $var 语法/ 2-15 不再 "+..." 堆叠）
    void update_template_example();

    // ------------------------------------------------------------ 预估（④）

    // 预估卡：预计输出 = 勾选文件数 × 已选格式数（仅元数据恒 1 个格式 —— §3.2 硬校验）；
    // 体积/耗时无实测依据 → 中性空态「—」（§8：未发生的量不显示；tooltip 说明）。
    void update_estimate();

    // R-228（M7-R2D2）：剩余磁盘 = QStorageInfo 查询（statvfs 级系统调用）**脱离逐键 textChanged**
    // —— 打字时每字符 stat 会在网络盘上卡 GUI。输入停 400ms 后由 singleShot 定时器统一查一次；
    // 程序化换根（set_out_root / 初值）同步查一次（单次调用）。
    void refresh_disk();

    void schedule_disk_refresh();

    // 修 2-25：保存/删除的可用态随"是否有当前预设"走，禁用原因写进 tooltip（不静默灰）
    void sync_preset_buttons();
};

} // namespace pp::ui
