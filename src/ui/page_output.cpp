// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 输出页（0.3.0 多格式 + 路径模板 + 预设 v2；M5-T8 v6 化）
//
// 规格 = docs/qt-gui-design-spec.md（v6）§7.2 输出页卡表 + §5 表单体系 R1–R4 + §6 控件语义决策表
//        + §8 状态/空态/反馈；视觉终稿 = docs/mockups-v2/output-{dark,light}.png + output.html；
//        评审台账 = docs/gui-review-0.3.0.md（问题 2-01～2-43）；开发期裁定 =
//        docs/m5-adjudications.md。
//
// 四卡组（§7.2 逐行）：
//   ① 输出格式：转换模式（分段：转码 | 仅元数据 + 说明行）｜ 8 格式磁贴
//   2×4（formkit::CheckableCard：
//      整卡可点 + 对勾位固定 → 选中零形变；未选中 = 正常对比度，禁用另设样式 —— 修 2-05）｜
//      卡头计数 = **中性** pill「已选 N」（修 2-07；1-24/1-25 卡头右置一律中性胶囊）；
//   ② 全局：根目录（flex + 浏览…，placeholder 说明留空行为 —— 修 2-10）｜ 同名冲突（下拉）｜
//      色彩目标（下拉 + tooltip 解释专业词 —— 修 2-16）｜ 分文件夹（开关 = 立即生效布尔，
//      标签归左列 —— 修 2-11；与路径模板的 $format 段联动）｜ 路径模板（flex + 变量表钮；
//      变量表/示例进 tooltip —— 修 2-13；统一 $var 语法、不再混用 <根> —— 修 2-14）｜
//      输出示例（注释行；自然语单行防溢出 —— 修 2-15）；
//   ③ 格式参数：页签（格式名 · 位深 ｜ 右端「＋ 添加格式…」可用态 —— 修 2-21）+ 每页签一行一义
//      六行（位深 ｜ 技术 ｜ 无损 ｜ 编码努力 ｜ 视觉距离 ｜ 高级参数）—— 行本体归
//      formkit/ParamForm； 页签归属由结构本身表达（删常驻补丁文案 —— 修 2-22），卡头中性 pill =
//      该格式「已改 N」计数；
//   ④ 预设与预估：当前预设（下拉 placeholder「未选择预设」—— 修 2-24；另存… / 删除 / 保存，
//      统一按钮规格 + 主操作居右 + 禁用原因进 tooltip —— 修 2-25/2-26）｜ 预估 kv 2 行。
//
// 冻结契约（PP-FROZEN，见 page_output.h 的追加面说明）：既有方法签名/语义不变；既有"冻结行为"逐条
// 保留：位深可选集 = FormatDef.bitdepths ∩ probe_bitdepth_support()（heif/avif，按 (format,backend)
// 进程内缓存）；introspect_backends() 结果按静态表顺序归一；avif + alpha 预选 = 触发条件求值
// （用户手动改过后端 → user_backend_override）；预设落值后同样求值；select_format()
// 仍是冒烟/走查钩子。
//
// 依赖 M5-T8 追加面：`set_input_count(files)`（预估卡的输入面；MainWindow 在 refresh_status 里喂入
// 勾选数，页面据此算「预计输出 = 勾选数 × 已选格式数」；**不发信号**，其余预估项无实测依据 →
// 空态「—」+ tooltip 说明，见 M5-T8 报告的偏差登记）。
//
// 排版硬约束（R1–R4）：行 pitch 28（formkit 行容器）；标签轨 80 右对齐；字段宽只取 88/160/flex；
// 单位入 16px 槽、范围入 10px 注释；按钮高 26（A7：浏览/变量表/另存…/保存/删除/添加格式）；
// 全页禁折行（ElidedLabel 省略号 + 悬浮提示）；objectName 全部 pp-* 钩子。
#include "ui/page_output.h"

#include <algorithm>
#include <array>
#include <charconv>
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
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "codecs/encoders.h"
#include "core/fsops.h"
#include "core/params.h"
#include "ui/formkit.h" // C2：行构造器（R1–R4）+ 自绘控件（磁贴/开关/胶囊）
#include "ui/icons.h"   // M5-T3：折叠插入符 /「添加格式…」的 ＋ 一律矢量图标（C3）
#include "ui/paramform.h"
#include "ui/theme.h"

namespace pp::ui {
namespace {

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

// §2.9.2：逐格式高质量档默认位深
int default_bitdepth(const std::string &id) {
    if (id == "jpeg")
        return 8;
    if (id == "jxl")
        return 16;
    if (id == "png")
        return 16;
    if (id == "tiff")
        return 16;
    if (id == "webp")
        return 8;
    if (id == "bmp")
        return 24;
    if (id == "heif")
        return 10;
    if (id == "avif")
        return 10;
    return 8;
}

bool contains(const std::vector<int> &v, int x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

bool contains_str(const std::vector<std::string> &v, const std::string &s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

std::vector<int> parse_csv_ints(const std::string &csv) {
    std::vector<int> out;
    std::size_t pos = 0;
    while (pos <= csv.size()) {
        const std::size_t comma = csv.find(',', pos);
        const std::size_t len = (comma == std::string::npos) ? std::string::npos : comma - pos;
        const std::string tok = csv.substr(pos, len);
        if (!tok.empty()) {
            int value = 0;
            const char *first = tok.data();
            const char *last = tok.data() + tok.size();
            const std::from_chars_result res = std::from_chars(first, last, value);
            if (res.ec == std::errc() && res.ptr == last)
                out.push_back(value);
        }
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    return out;
}

// §2.9.2：可选位深 = 静态允许集 ∩ 运行期探测（非 libheif 格式即静态集）。
// §2.9.7：探测结果按 (format, backend) 进程内缓存一次（GUI 线程独占，无需锁）。
std::vector<int> allowed_bitdepths(const pp::FormatDef &f, const std::string &backend) {
    if (f.id != "heif" && f.id != "avif")
        return f.bitdepths;
    static std::map<std::pair<std::string, std::string>, std::vector<int>> cache;
    const auto key = std::make_pair(f.id, backend);
    auto it = cache.find(key);
    if (it == cache.end()) {
        const std::vector<int> probed = parse_csv_ints(pp::probe_bitdepth_support(f.id, backend));
        std::vector<int> allowed;
        for (const int d : f.bitdepths)
            if (contains(probed, d))
                allowed.push_back(d);
        it = cache.emplace(key, std::move(allowed)).first;
    }
    // 探测无可用结果（异常环境）→ 回退静态集，UI 至少可用（引擎侧仍会明确报错）
    return it->second.empty() ? f.bitdepths : it->second;
}

// heif/avif：静态表后端带 runtime_introspected 标志（FormatDef 本身无该字段）
bool runtime_introspected_format(const pp::FormatDef &f) {
    return std::any_of(f.backends.begin(), f.backends.end(),
                       [](const pp::BackendDef &b) { return b.runtime_introspected; });
}

// §2.9.7：heif/avif 内省在首次选中时执行并缓存；顺序归一为静态表顺序，
// 使默认后端与引擎默认（make_encoder(fmt,"") → svt-av1）一致。
std::vector<pp::BackendDef> effective_backends(const pp::FormatDef &f) {
    if (!runtime_introspected_format(f))
        return f.backends;
    static std::map<std::string, std::vector<pp::BackendDef>> cache;
    auto it = cache.find(f.id);
    if (it == cache.end()) {
        const std::vector<pp::BackendDef> live = pp::introspect_backends(f.id);
        std::vector<pp::BackendDef> ordered;
        for (const pp::BackendDef &sb : f.backends)
            for (const pp::BackendDef &lb : live)
                if (lb.id == sb.id)
                    ordered.push_back(lb);
        for (const pp::BackendDef &lb : live)
            if (std::none_of(ordered.begin(), ordered.end(),
                             [&](const pp::BackendDef &b) { return b.id == lb.id; }))
                ordered.push_back(lb);
        it = cache.emplace(f.id, std::move(ordered)).first;
    }
    return it->second.empty() ? f.backends : it->second;
}

bool has_backend(const std::vector<pp::BackendDef> &list, const std::string &id) {
    return std::any_of(list.begin(), list.end(),
                       [&](const pp::BackendDef &b) { return b.id == id; });
}

// 注意（M4-T13b / D-W3-1）：返回的是 **list 容器内**的指针 —— 调用方必须保证 list 的
// 生存期覆盖该指针的全部使用（effective_backends 按值返回，故须先绑定到具名局部）。
const pp::TechDef *tech_by_id(const std::vector<pp::BackendDef> &list, const std::string &backend,
                              const std::string &tech) {
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
pp::ConflictPolicy conflict_from_index(int idx) {
    switch (idx) {
    case 1:
        return pp::ConflictPolicy::Skip;
    case 2:
        return pp::ConflictPolicy::Overwrite;
    default:
        return pp::ConflictPolicy::Rename; // 默认 自动加序号
    }
}

int conflict_to_index(pp::ConflictPolicy p) {
    switch (p) {
    case pp::ConflictPolicy::Skip:
        return 1;
    case pp::ConflictPolicy::Overwrite:
        return 2;
    case pp::ConflictPolicy::Rename:
    default:
        return 0;
    }
}

pp::ColorTarget color_from_index(int idx) {
    switch (idx) {
    case 1:
        return pp::ColorTarget::SRGB;
    case 2:
        return pp::ColorTarget::DisplayP3;
    case 3:
        return pp::ColorTarget::AdobeRGB;
    default:
        return pp::ColorTarget::KeepOriginal; // 默认 保持原样
    }
}

int color_to_index(pp::ColorTarget t) {
    switch (t) {
    case pp::ColorTarget::SRGB:
        return 1;
    case pp::ColorTarget::DisplayP3:
        return 2;
    case pp::ColorTarget::AdobeRGB:
        return 3;
    case pp::ColorTarget::KeepOriginal:
    default:
        return 0;
    }
}

// 人类可读容量（剩余磁盘读数；自然语，不显示裸字节）
QString human_bytes(qint64 bytes) {
    constexpr double kKiB = 1024.0;
    const char *const units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= kKiB && unit < 5) {
        value /= kKiB;
        ++unit;
    }
    return unit <= 1
               ? QStringLiteral("%1 %2").arg(qint64(value)).arg(QLatin1String(units[unit]))
               : QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(QLatin1String(units[unit]));
}

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

QString page_qss(const theme::Tokens &t) {
    // UX-6⑥（M7-WP4）：跨页同形规则 = theme::field_qss（输入/按钮/文字链/折叠/提示/ppInvalid），
    // 页专规则（格式页签/「＋ 添加格式…」/容器透明）= theme::page_output_qss —— 页内不再拼 QSS。
    return theme::field_qss(t) + theme::page_output_qss(t);
}

// ---------------------------------------------------------------- 页面状态

struct FormatState {
    pp::OutputFormatSpec spec; // 首次建页签的初值（无则表内默认）
    QWidget *page = nullptr;
    ParamForm *form = nullptr;
    QComboBox *bitdepth = nullptr;
    QLabel *bitdepth_note = nullptr; // 格式能力约束说明（行内 10px 注释位；如 "JPEG 仅支持 8 位"）
    ElidedLabel *alpha_hint = nullptr;
};

struct Impl {
    PageOutput *q = nullptr;
    theme::Tokens tokens{};

    bool metadata_only = false;
    bool batch_has_alpha = false;
    bool user_backend_override = false; // §9.1：用户手动改过后端（set_batch_has_alpha 清除）
    std::string last_backend;           // 最近一次已知后端（区分用户手动改动）
    std::string active_id = "jxl";      // 当前页签 / 仅元数据模式的单一格式
    std::vector<std::string> selected;  // 已选格式（规范顺序 = 磁贴顺序）
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

    // ------------------------------------------------------------ 查询

    FormatState *state_of(const std::string &id) {
        const auto it = states.find(id);
        return it == states.end() ? nullptr : &it->second;
    }

    const FormatState *state_of(const std::string &id) const {
        const auto it = states.find(id);
        return it == states.end() ? nullptr : &it->second;
    }

    bool is_selected(const std::string &id) const { return contains_str(selected, id); }

    std::string backend_id(const std::string &id) const {
        const FormatState *st = state_of(id);
        return (st != nullptr && st->form != nullptr) ? st->form->selection().backend
                                                      : std::string();
    }

    bool lossless_of(const std::string &id) const {
        const FormatState *st = state_of(id);
        return (st != nullptr && st->form != nullptr) ? st->form->selection().lossless : false;
    }

    int bitdepth_of(const std::string &id) const {
        const FormatState *st = state_of(id);
        return (st != nullptr && st->bitdepth != nullptr) ? st->bitdepth->currentData().toInt() : 0;
    }

    pp::ColorTarget color_target() const {
        return color_from_index(color_combo != nullptr ? color_combo->currentIndex() : 0);
    }

    pp::ConflictPolicy conflict() const {
        return conflict_from_index(conflict_combo != nullptr ? conflict_combo->currentIndex() : 0);
    }

    QString template_text() const {
        return template_edit != nullptr ? template_edit->text() : QString();
    }

    // ------------------------------------------------------------ 界面构建

    void build_ui() {
        auto *outer = new QVBoxLayout(q);
        outer->setContentsMargins(0, 0, 0, 0);

        auto *scroll = new QScrollArea(q);
        scroll->setObjectName(QStringLiteral("pp-output-scroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        outer->addWidget(scroll);

        auto *content = new QWidget(scroll);
        content->setObjectName(QStringLiteral("pp-output-content"));
        auto *v = new QVBoxLayout(content);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(theme::Metrics::gap); // §4.3 卡间距 8
        scroll->setWidget(content);

        v->addWidget(build_format_card(content));
        v->addWidget(build_global_card(content));
        v->addWidget(build_param_card(content));
        v->addWidget(build_preset_card(content));
        v->addStretch(1);
    }

    // 卡壳（§4.3 r6 + --card-bd 描边/阴影走 theme QSS；卡头 = 标题 12/700 + 中性 pill + hint）
    QFrame *make_card(QWidget *parent, QVBoxLayout **body_out) {
        auto *card = new QFrame(parent);
        card->setObjectName(QStringLiteral("pp-card"));
        auto *v = new QVBoxLayout(card);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        *body_out = v;
        return card;
    }

    struct CardHeader {
        QWidget *row = nullptr;
        QLabel *title = nullptr;
        formkit::PillBadge *pill = nullptr;
        ElidedLabel *hint = nullptr;
    };

    CardHeader make_header(QWidget *card, const QString &title, const QString &pill_name,
                           const QString &hint_name) {
        // R-021/R-053（M7-WP4）：卡头几何单源 = formkit::card_header（原 30 行自拷建造器退役；
        // pill 单走 PillBadge、hint 单走 ElidedLabel；卡头内边距 9/7 与间距 8 = formkit_layout
        // 常量）
        const formkit::CardParts parts = formkit::card_header(card, title, pill_name, hint_name);
        CardHeader out;
        out.row = parts.head;
        out.title = parts.title;
        out.pill = parts.pill_badge();
        out.hint = parts.hint_label();
        return out;
    }

    // 卡体（R1：上内边距 10、下 12；行 pitch 由 formkit 行容器承担 → 行间无额外 gap）
    struct Body {
        QWidget *host = nullptr;
        QVBoxLayout *box = nullptr;
    };

    Body make_body(QWidget *card) {
        Body out;
        out.host = new QWidget(card);
        out.box = new QVBoxLayout(out.host);
        out.box->setContentsMargins(theme::Metrics::card_pad_x, theme::Metrics::card_pad_top,
                                    theme::Metrics::card_pad_x, theme::Metrics::card_pad_bottom);
        out.box->setSpacing(0);
        return out;
    }

    QPushButton *make_button(QWidget *parent, const QString &text, const QString &object_name,
                             bool primary) {
        auto *button = new QPushButton(text, parent);
        button->setObjectName(object_name);
        button->setFixedHeight(theme::Metrics::btn_h); // R4 按钮高 26（A7）
        button->setCursor(Qt::PointingHandCursor);
        button->setFont(theme::font(theme::Typography::body_px, QFont::DemiBold));
        button->setProperty(kFieldProperty,
                            primary ? QStringLiteral("primary") : QStringLiteral("button"));
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return button;
    }

    // 行内复合字段（flex 轨）：[本体 flex][附件…] —— 复合字段容器撑满轨，无浮空死区（R2）
    QWidget *make_flex_field(QWidget *parent, QList<QWidget *> widgets) {
        auto *host = new QWidget(parent);
        auto *h = new QHBoxLayout(host);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(theme::Metrics::group_gap);
        for (int i = 0; i < widgets.size(); ++i)
            h->addWidget(widgets[i], i == 0 ? 1 : 0);
        return host;
    }

    // 卡 ①：输出格式（转换模式分段 + 说明行 ｜ 8 格式磁贴 2×4）
    QWidget *build_format_card(QWidget *parent) {
        QVBoxLayout *v = nullptr;
        format_card = make_card(parent, &v);
        const CardHeader header =
            make_header(format_card, PageOutput::tr("输出格式"), QStringLiteral("pp-format-pill"),
                        QStringLiteral("pp-format-hint"));
        // 修 2-08：公式式文案（"每个输入输出 N 个文件"）不再常驻 —— 自然语 hint + tooltip 说明
        header.hint->set_full_text(PageOutput::tr("可多选 · 对勾位固定"));
        header.hint->setToolTip(PageOutput::tr("每个已选格式都会为每个输入文件生成一个输出文件"));
        format_pill = header.pill;
        format_hint = header.hint;
        v->addWidget(header.row);

        const Body body = make_body(format_card);
        // 转换模式（互斥 = 分段控件唯一形态；段宽 88 等宽 → 选中零形变）
        {
            mode_seg = new formkit::SegmentedControl(format_card);
            mode_seg->setObjectName(QStringLiteral("pp-output-seg"));
            mode_seg->add_segment(PageOutput::tr("转码"), QStringLiteral("pp-mode-convert"));
            mode_seg->add_segment(PageOutput::tr("仅元数据"), QStringLiteral("pp-mode-metadata"));
            auto *note =
                new QLabel(PageOutput::tr("转码 = 生成新文件 · 仅元数据 = 原地写入"), format_card);
            note->setObjectName(QStringLiteral("pp-mode-hint"));
            note->setProperty(kFieldProperty, QStringLiteral("hint"));
            note->setFont(theme::font(theme::Typography::hint_px));
            QWidget *field = make_flex_field(format_card, {mode_seg, note});
            const formkit::Row row =
                formkit::frow(format_card, PageOutput::tr("转换模式"), field, FieldWidth::Flex);
            row.row->setObjectName(QStringLiteral("pp-mode-row"));
            body.box->addWidget(row.row);
        }
        // 仅元数据说明行（互斥语义零改动：格式多选/格式参数卡置灰并出说明）
        mode_note = new ElidedLabel(format_card);
        mode_note->setObjectName(QStringLiteral("pp-format-note"));
        mode_note->setProperty(kFieldProperty, QStringLiteral("hint"));
        mode_note->setFont(theme::font(theme::Typography::hint_px));
        mode_note->setMinimumHeight(formkit::kRowPitch);
        mode_note->setContentsMargins(formkit::kLabelW + formkit::kColGap, 0, 0, 0);
        mode_note->set_full_text(
            PageOutput::tr("仅元数据：同容器零重编码，输出格式多选已禁用（保持原格式）"));
        mode_note->setVisible(false);
        body.box->addWidget(mode_note);
        // 8 格式磁贴 2×4（多选集合唯一形态 = formkit::CheckableCard：整卡可点 + 对勾位固定）
        {
            tiles_host = new QWidget(format_card);
            tiles_host->setObjectName(QStringLiteral("pp-tiles"));
            auto *grid = new QGridLayout(tiles_host);
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setHorizontalSpacing(theme::Metrics::group_gap); // §4.3 组内 8
            grid->setVerticalSpacing(theme::Metrics::group_gap);
            for (int i = 0; i < kFormatCount; ++i) {
                const pp::FormatDef *f = pp::find_format(kFormats[i].id);
                auto *tile = new formkit::CheckableCard(tiles_host);
                tile->setObjectName(QStringLiteral("pp-format-") + QLatin1String(kFormats[i].id));
                tile->set_labels(PageOutput::tr(kFormats[i].label),
                                 QStringLiteral(".") + QString::fromStdString(f ? f->ext : ""));
                tile->set_tokens(tokens);
                tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                grid->addWidget(tile, i / kFormatColumns, i % kFormatColumns);
                tiles[i] = tile;
            }
            for (int c = 0; c < kFormatColumns; ++c)
                grid->setColumnStretch(c, 1);
            body.box->addWidget(tiles_host);
        }
        v->addWidget(body.host);
        update_format_count();
        return format_card;
    }

    // 卡 ②：全局（一行一义 6 行）
    QWidget *build_global_card(QWidget *parent) {
        QVBoxLayout *v = nullptr;
        global_card = make_card(parent, &v);
        const CardHeader header =
            make_header(global_card, PageOutput::tr("全局"), QString(), QString());
        v->addWidget(header.row);
        const Body body = make_body(global_card);

        // 根目录（flex + 浏览…）；修 2-10：placeholder 说明留空行为
        {
            out_root_edit = new QLineEdit(global_card);
            out_root_edit->setObjectName(QStringLiteral("pp-out-root-edit"));
            out_root_edit->setFixedHeight(theme::Metrics::input_h);
            out_root_edit->setProperty(kFieldProperty, QStringLiteral("input"));
            out_root_edit->setPlaceholderText(PageOutput::tr("留空无法开始运行：请选择输出根目录"));
            out_root_edit->setToolTip(PageOutput::tr("所有输出都落在这个目录下（按路径模板分层）"));
            browse_button = make_button(global_card, PageOutput::tr("浏览…"),
                                        QStringLiteral("pp-browse-button"), false);
            browse_button->setToolTip(PageOutput::tr("选择输出根目录"));
            QWidget *field = make_flex_field(global_card, {out_root_edit, browse_button});
            const formkit::Row row =
                formkit::frow(global_card, PageOutput::tr("根目录"), field, FieldWidth::Flex);
            row.row->setObjectName(QStringLiteral("pp-root-row"));
            body.box->addWidget(row.row);
        }
        // 同名冲突（枚举 = 下拉）
        {
            formkit::ComboSpec conflict_spec;
            conflict_spec.object_name = QStringLiteral("pp-conflict-combo");
            conflict_spec.accessible = PageOutput::tr("同名冲突");
            conflict_spec.tooltip = PageOutput::tr("输出文件与已有文件同名时的处理方式");
            conflict_combo = formkit::combo_box(global_card, conflict_spec);
            conflict_combo->addItem(PageOutput::tr("自动加序号"),
                                    static_cast<int>(pp::ConflictPolicy::Rename));
            conflict_combo->addItem(PageOutput::tr("跳过"),
                                    static_cast<int>(pp::ConflictPolicy::Skip));
            conflict_combo->addItem(PageOutput::tr("覆盖"),
                                    static_cast<int>(pp::ConflictPolicy::Overwrite));
            conflict_combo->setCurrentIndex(0);
            conflict_combo->setFixedHeight(theme::Metrics::combo_h);
            conflict_combo->setProperty(kFieldProperty, QStringLiteral("combo"));
            conflict_combo->setToolTip(
                PageOutput::tr("目标文件已存在时：自动加序号（IMG_1.jpg）/ 跳过 / 覆盖"));
            const formkit::Row row = formkit::frow(global_card, PageOutput::tr("同名冲突"),
                                                   conflict_combo, FieldWidth::Standard);
            row.row->setObjectName(QStringLiteral("pp-conflict-row"));
            body.box->addWidget(row.row);
        }
        // 色彩目标（下拉 + tooltip 解释专业词 —— 修 2-16）
        {
            formkit::ComboSpec color_spec;
            color_spec.object_name = QStringLiteral("pp-color-combo");
            color_spec.accessible = PageOutput::tr("色彩目标");
            color_combo = formkit::combo_box(global_card, color_spec);
            color_combo->addItem(PageOutput::tr("保持原样"),
                                 static_cast<int>(pp::ColorTarget::KeepOriginal));
            color_combo->addItem(PageOutput::tr("sRGB"), static_cast<int>(pp::ColorTarget::SRGB));
            color_combo->addItem(PageOutput::tr("Display P3"),
                                 static_cast<int>(pp::ColorTarget::DisplayP3));
            color_combo->addItem(PageOutput::tr("Adobe RGB (1998)"),
                                 static_cast<int>(pp::ColorTarget::AdobeRGB));
            color_combo->setCurrentIndex(0);
            color_combo->setFixedHeight(theme::Metrics::combo_h);
            color_combo->setProperty(kFieldProperty, QStringLiteral("combo"));
            color_combo->setToolTip(PageOutput::tr(
                "保持原样 = 不改动嵌入的 ICC 色彩描述；其余档位按目标色域转换并写入新的 ICC。"
                "仅元数据模式下不生效（不做色彩转换）"));
            const formkit::Row row = formkit::frow(global_card, PageOutput::tr("色彩目标"),
                                                   color_combo, FieldWidth::Standard);
            row.row->setObjectName(QStringLiteral("pp-color-row"));
            body.box->addWidget(row.row);
        }
        // 分文件夹（独立布尔（立即生效）= 开关；标签归左列 —— 修 2-11；与 $format 段联动）
        {
            split_switch = new formkit::SwitchControl(global_card);
            split_switch->setObjectName(QStringLiteral("pp-split-switch"));
            split_switch->set_tokens(tokens);
            split_switch->setToolTip(
                PageOutput::tr("开 = 路径模板带上 $format 段（每个格式一个子目录）；关 = 去掉该段，"
                               "多个格式会写进同一目录"));
            auto *text = new QLabel(PageOutput::tr("按文件格式分文件夹"), global_card);
            text->setObjectName(QStringLiteral("pp-split-label"));
            text->setProperty(kFieldProperty, QStringLiteral("sub"));
            text->setFont(theme::font(theme::Typography::body_px));
            QWidget *field = make_flex_field(global_card, {split_switch, text});
            const formkit::Row row =
                formkit::frow(global_card, PageOutput::tr("分文件夹"), field, FieldWidth::Flex);
            row.row->setObjectName(QStringLiteral("pp-split-row"));
            body.box->addWidget(row.row);
        }
        // 路径模板（flex + 变量表钮；变量表/示例进 tooltip —— 修 2-13；统一 $var 语法 —— 修 2-14）
        {
            template_edit = new QLineEdit(global_card);
            template_edit->setObjectName(QStringLiteral("pp-template-edit"));
            template_edit->setFixedHeight(theme::Metrics::input_h);
            template_edit->setProperty(kFieldProperty, QStringLiteral("input"));
            template_edit->setPlaceholderText(QStringLiteral("$format/$dir/$file"));
            template_edit->setToolTip(template_tooltip());
            template_edit->setText(QStringLiteral("$format/$dir/$file"));
            vars_button = make_button(global_card, PageOutput::tr("变量表"),
                                      QStringLiteral("pp-template-vars"), false);
            vars_button->setToolTip(template_tooltip());
            QWidget *field = make_flex_field(global_card, {template_edit, vars_button});
            const formkit::Row row =
                formkit::frow(global_card, PageOutput::tr("路径模板"), field, FieldWidth::Flex);
            row.row->setObjectName(QStringLiteral("pp-template-row"));
            body.box->addWidget(row.row);
        }
        // 输出示例（注释行；自然语单行 —— 修 2-14/2-15）
        {
            template_example = new ElidedLabel(global_card);
            template_example->setObjectName(QStringLiteral("pp-template-example"));
            template_example->setProperty(kFieldProperty, QStringLiteral("sub"));
            template_example->setFont(theme::font(theme::Typography::hint_px));
            const formkit::Row row = formkit::frow(global_card, PageOutput::tr("输出示例"),
                                                   template_example, FieldWidth::Flex);
            row.row->setObjectName(QStringLiteral("pp-example-row"));
            body.box->addWidget(row.row);
        }
        v->addWidget(body.host);
        update_template_example();
        return global_card;
    }

    // 卡 ③：格式参数（页签 + 「＋ 添加格式…」；每页签一行一义六行）
    QWidget *build_param_card(QWidget *parent) {
        QVBoxLayout *v = nullptr;
        param_card = make_card(parent, &v);
        // 修 2-22：页签内参数的归属由**结构**表达（位深/技术/无损/编码努力/视觉距离都在页签内），
        // 不再挂"页签内含位深与编码参数"这类常驻补丁文案。
        const CardHeader header = make_header(param_card, PageOutput::tr("格式参数"),
                                              QStringLiteral("pp-format-changed-pill"), QString());
        changed_pill = header.pill;
        v->addWidget(header.row);

        tabs = new QTabWidget(param_card);
        tabs->setObjectName(QStringLiteral("pp-format-tabs"));
        tabs->setDocumentMode(true);
        add_format_button = new QToolButton(tabs);
        add_format_button->setObjectName(QStringLiteral("pp-add-format"));
        add_format_button->setText(PageOutput::tr("添加格式…"));
        add_format_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        add_format_button->setToolTip(PageOutput::tr("添加格式…（开启尚未选中的格式）"));
        add_format_button->setAccessibleName(PageOutput::tr("添加格式…")); // §11
        add_format_button->setProperty(icons::kIconNameProperty,
                                       QString::fromLatin1(icons::key(icons::Name::Add)));
        add_format_button->setAutoRaise(true);
        add_format_button->setCursor(Qt::PointingHandCursor);
        add_format_button->setFixedHeight(theme::Metrics::btn_h); // R4 按钮高 26（A7）
        add_format_button->setFont(theme::font(theme::Typography::body_px, 500));
        tabs->setCornerWidget(add_format_button, Qt::TopRightCorner);
        // GUI-R2 修 2：QTabWidget 的 corner widget 由它自己的布局摆位，与页签带**不是同一绘制
        // 单元**（实测 dy=10 / 实拍色带差 14px，两块圆角块中间还有缝）。这里在每次布局后把按钮
        // **垂直居中**到页签带上（按钮高 26 是 A7 规格，不随页签带拉伸），保证「＋ 添加格式…」
        // 与页签带同带；只在几何真变时写，避免布局环。
        class AddFormatAligner : public QObject {
        public:
            AddFormatAligner(QTabWidget *tabs_, QToolButton *btn_)
                : QObject(tabs_), tabs_(tabs_), btn_(btn_) {
                tabs_->installEventFilter(this);
            }

        protected:
            bool eventFilter(QObject *watched, QEvent *event) override {
                const QEvent::Type t = event->type();
                if (watched == tabs_ &&
                    (t == QEvent::Resize || t == QEvent::LayoutRequest || t == QEvent::Show)) {
                    QTabBar *bar = tabs_->tabBar();
                    if (bar != nullptr && btn_ != nullptr) {
                        const QPoint org = bar->mapTo(tabs_, QPoint(0, 0));
                        const int y = org.y() + (bar->height() - btn_->height()) / 2;
                        const QRect want(org.x() + bar->width() - btn_->width(), y, btn_->width(),
                                         btn_->height());
                        if (btn_->geometry() != want)
                            btn_->setGeometry(want);
                    }
                }
                return QObject::eventFilter(watched, event);
            }

        private:
            QTabWidget *tabs_;
            QToolButton *btn_;
        };
        new AddFormatAligner(tabs, add_format_button);
        tabs->setContentsMargins(0, 0, 0, 0);
        v->addWidget(tabs);
        refresh_changed_pill();
        return param_card;
    }

    // 卡 ④：预设与预估（当前预设 + 另存…/删除/保存 ｜ 预估 kv 2 行）
    QWidget *build_preset_card(QWidget *parent) {
        QVBoxLayout *v = nullptr;
        preset_card = make_card(parent, &v);
        const CardHeader header = make_header(preset_card, PageOutput::tr("预设与预估"), QString(),
                                              QStringLiteral("pp-preset-hint"));
        preset_hint = header.hint;
        v->addWidget(header.row);
        const Body body = make_body(preset_card);

        // 当前预设（下拉 + 三个按钮；主操作居右 —— R4）
        {
            formkit::ComboSpec preset_spec;
            preset_spec.object_name = QStringLiteral("pp-preset-combo");
            preset_spec.accessible = PageOutput::tr("当前预设");
            preset_spec.tooltip = PageOutput::tr("选择预设即载入（不改变左侧文件的勾选）");
            preset_combo = formkit::combo_box(preset_card, preset_spec);
            preset_combo->setPlaceholderText(PageOutput::tr("未选择预设")); // 修 2-24
            preset_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            preset_saveas = make_button(preset_card, PageOutput::tr("另存…"),
                                        QStringLiteral("pp-preset-saveas"), false);
            preset_saveas->setToolTip(PageOutput::tr("另存为新预设…（命名后写入预设目录）"));
            preset_delete = make_button(preset_card, PageOutput::tr("删除"),
                                        QStringLiteral("pp-preset-delete"), false);
            preset_save = make_button(preset_card, PageOutput::tr("保存"),
                                      QStringLiteral("pp-preset-save"), true);
            QWidget *field = make_flex_field(
                preset_card, {preset_combo, preset_saveas, preset_delete, preset_save});
            const formkit::Row row =
                formkit::frow(preset_card, PageOutput::tr("当前预设"), field, FieldWidth::Flex);
            row.row->setObjectName(QStringLiteral("pp-preset-row"));
            body.box->addWidget(row.row);
        }
        // 预估 kv 2 行（预计输出/体积 ｜ 预计耗时/剩余磁盘）—— 无实测依据的两项走中性空态
        {
            const auto make_value = [&](const QString &object_name) {
                auto *value = new ElidedLabel(preset_card);
                value->setObjectName(object_name);
                value->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkValue));
                value->setFont(theme::font(theme::Typography::body_px, QFont::Normal, true));
                value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                return value;
            };
            est_output = make_value(QStringLiteral("pp-est-output"));
            est_size = make_value(QStringLiteral("pp-est-size"));
            est_time = make_value(QStringLiteral("pp-est-time"));
            est_disk = make_value(QStringLiteral("pp-est-disk"));
            est_size->setToolTip(
                PageOutput::tr("未运行前不给估值（避免假读数）：完成后体积见运行页"));
            est_time->setToolTip(
                PageOutput::tr("未运行前不给估值（避免假读数）：完成后吞吐见运行页"));
            const auto make_row = [&](const QString &key1, ElidedLabel *value1, const QString &key2,
                                      ElidedLabel *value2) {
                formkit::FieldCell left;
                left.label = key1;
                left.field = value1;
                left.width = FieldWidth::Flex;
                formkit::FieldCell right;
                right.label = key2;
                right.field = value2;
                right.width = FieldWidth::Flex;
                return formkit::double_row(preset_card, left, right).row;
            };
            body.box->addWidget(make_row(PageOutput::tr("预计输出"), est_output,
                                         PageOutput::tr("预计体积"), est_size));
            body.box->addWidget(make_row(PageOutput::tr("预计耗时"), est_time,
                                         PageOutput::tr("剩余磁盘"), est_disk));
        }
        v->addWidget(body.host);
        update_estimate();
        sync_preset_buttons(); // 初始无当前预设 → 保存/删除禁用并给出原因（修 2-25）
        return preset_card;
    }

    static QString template_tooltip() {
        return PageOutput::tr(
            "变量：$format=格式目录 · $dir=源相对目录 · $file=文件名.扩展名 · $name=主名 · "
            "$ext=扩展名\n"
            "示例：$format/$dir/$file → …/jxl/2024-05/IMG_2731.jxl\n"
            "段间以 / 分隔；未知 $ 符号、.. 与绝对路径会被拒绝（开始运行前阻断）");
    }

    // ------------------------------------------------------------ 信号接线

    void wire() {
        for (int i = 0; i < kFormatCount; ++i) {
            formkit::CheckableCard *tile = tiles[i];
            QObject::connect(tile, &QAbstractButton::toggled, q, [this, i](bool on) {
                toggle_format(kFormats[i].id, on, /*user=*/true);
            });
        }
        QObject::connect(mode_seg, &formkit::SegmentedControl::current_changed, q,
                         [this](int index) { q->set_metadata_only(index == 1); });

        QObject::connect(browse_button, &QPushButton::clicked, q, [this] {
            const QString start = out_root_edit->text().trimmed();
            const QString dir =
                QFileDialog::getExistingDirectory(q, PageOutput::tr("选择输出根目录"), start);
            if (!dir.isEmpty())
                q->set_out_root(dir);
        });
        QObject::connect(out_root_edit, &QLineEdit::textChanged, q, [this](const QString &) {
            update_template_example();
            update_estimate();
            emit q->config_changed();
        });
        QObject::connect(conflict_combo, &QComboBox::currentIndexChanged, q,
                         [this](int) { emit q->config_changed(); });
        QObject::connect(color_combo, &QComboBox::currentIndexChanged, q,
                         [this](int) { emit q->config_changed(); });
        QObject::connect(template_edit, &QLineEdit::textChanged, q, [this](const QString &text) {
            sync_split_switch(text);
            update_template_example();
            emit q->output_template_changed(text);
            emit q->config_changed();
        });
        QObject::connect(vars_button, &QPushButton::clicked, q, [this] { show_vars_menu(); });
        QObject::connect(split_switch, &formkit::SwitchControl::toggled, q,
                         [this](bool on) { apply_split_to_template(on); });
        QObject::connect(tabs, &QTabWidget::currentChanged, q, [this](int index) {
            if (index < 0 || index >= static_cast<int>(selected.size()))
                return;
            const std::string id = selected[static_cast<std::size_t>(index)];
            if (id == active_id)
                return;
            active_id = id;
            update_form_object_names();
            update_template_example();
            refresh_changed_pill();
            refresh_tab_labels();
            emit q->format_changed(QString::fromStdString(id));
            emit q->config_changed();
        });
        QObject::connect(add_format_button, &QToolButton::clicked, q, [this] {
            // 「＋ 添加格式…」= 打开尚未选中的首个格式（磁贴顺序）；全部已选 → 无可用项
            for (const FormatEntry &e : kFormats) {
                if (!is_selected(e.id)) {
                    toggle_format(e.id, true, /*user=*/true);
                    return;
                }
            }
        });
        QObject::connect(preset_combo, &QComboBox::activated, q, [this](int index) {
            if (index < 0)
                return;
            const QString path = preset_combo->itemData(index, Qt::UserRole).toString();
            if (!path.isEmpty())
                emit q->preset_load_requested(path);
        });
        QObject::connect(preset_save, &QPushButton::clicked, q, [this] {
            const int index = preset_combo->currentIndex();
            const QString path =
                index >= 0 ? preset_combo->itemData(index, Qt::UserRole).toString() : QString();
            if (path.isEmpty()) {
                emit q->preset_saveas_requested(); // 未选预设 → 走命名保存
                return;
            }
            emit q->preset_save_requested(preset_combo->itemText(index));
        });
        QObject::connect(preset_saveas, &QPushButton::clicked, q,
                         [this] { emit q->preset_saveas_requested(); });
        QObject::connect(preset_delete, &QPushButton::clicked, q, [this] {
            const int index = preset_combo->currentIndex();
            const QString path =
                index >= 0 ? preset_combo->itemData(index, Qt::UserRole).toString() : QString();
            if (!path.isEmpty())
                emit q->preset_delete_requested(path);
        });
    }

    // 变量表菜单（点击变量即插入到模板光标处；§8：变量语法必有
    // tooltip，本菜单是同一份说明的可点形态）
    void show_vars_menu() {
        struct Var {
            const char *token;
            const char *desc;
        };
        static const Var vars[] = {
            {"$format", "格式目录（jxl / png / …）"},
            {"$dir", "源相对目录（如 2024/05）"},
            {"$file", "文件名.扩展名"},
            {"$name", "主名（不含扩展名）"},
            {"$ext", "扩展名"},
        };
        QMenu menu(q);
        for (const Var &v : vars) {
            QAction *action = menu.addAction(QString::fromLatin1(v.token) + QStringLiteral(" · ") +
                                             PageOutput::tr(v.desc));
            QObject::connect(action, &QAction::triggered, q, [this, token = v.token] {
                const int pos = template_edit->cursorPosition();
                QString text = template_edit->text();
                text.insert(pos, QString::fromLatin1(token));
                template_edit->setText(text); // textChanged → 示例/信号
                template_edit->setCursorPosition(pos + QString::fromLatin1(token).size());
            });
        }
        menu.exec(vars_button->mapToGlobal(QPoint(0, vars_button->height())));
    }

    // ------------------------------------------------------------ 磁贴 / 页签

    void toggle_format(const std::string &id, bool on, bool user) {
        if (pp::find_format(id) == nullptr)
            return;
        const bool was = is_selected(id);
        if (on == was)
            return;
        if (on) {
            selected.push_back(id);
            std::sort(selected.begin(), selected.end(),
                      [](const std::string &a, const std::string &b) {
                          return tile_index(a) < tile_index(b); // 规范顺序 = 磁贴顺序
                      });
            build_tab(id);
            active_id = id;
            if (user)
                emit q->format_changed(QString::fromStdString(id));
        } else {
            snapshot_state(id);
            remove_tab(id);
            selected.erase(std::remove(selected.begin(), selected.end(), id), selected.end());
            if (active_id == id)
                active_id = selected.empty() ? id : selected.front();
        }
        sync_tiles();
        sync_tab_current();
        update_format_count();
        update_metadata_only_state();
        update_template_example();
        update_estimate();
        refresh_changed_pill();
        emit q->config_changed();
    }

    static int tile_index(const std::string &id) {
        for (int i = 0; i < kFormatCount; ++i)
            if (id == kFormats[i].id)
                return i;
        return kFormatCount;
    }

    void sync_tiles() {
        for (int i = 0; i < kFormatCount; ++i) {
            const QSignalBlocker blocker(tiles[i]);
            tiles[i]->setChecked(is_selected(kFormats[i].id));
        }
    }

    void sync_tab_current() {
        const QSignalBlocker blocker(tabs);
        for (int i = 0; i < static_cast<int>(selected.size()); ++i) {
            if (selected[static_cast<std::size_t>(i)] == active_id) {
                tabs->setCurrentIndex(i);
                break;
            }
        }
        update_form_object_names();
    }

    // 页签内的 ParamForm：只有**当前**页签的表单命名为 pp-param-form（冒烟/走查钩子），
    // 其余为 pp-param-form-<id> —— findChild 命中当前格式那一个。
    void update_form_object_names() {
        for (const auto &[id, st] : states) {
            if (st.form == nullptr)
                continue;
            st.form->setObjectName(id == active_id ? QStringLiteral("pp-param-form")
                                                   : QStringLiteral("pp-param-form-") +
                                                         QString::fromStdString(id));
        }
    }

    void build_tab(const std::string &id) {
        const pp::FormatDef *f = pp::find_format(id);
        if (f == nullptr)
            return;
        FormatState &st = states[id];
        if (st.page != nullptr)
            return; // 已存在（取消勾选后重新勾选 → 复用快照重建）
        st.page = new QWidget(tabs);
        st.page->setObjectName(QStringLiteral("pp-format-page-") + QString::fromStdString(id));
        auto *v = new QVBoxLayout(st.page);
        // 页签内容与卡片其余行同轴（卡内边距 12；R1 行 pitch 连续）
        v->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                              theme::Metrics::card_pad_bottom);
        v->setSpacing(0);

        // 行 1：位深（格式能力约束：如 JPEG 锁 8 位；约束说明入 10px 注释位）
        {
            formkit::ComboSpec bit_spec;
            bit_spec.object_name = QStringLiteral("pp-bitdepth-combo");
            bit_spec.accessible = PageOutput::tr("位深");
            st.bitdepth = formkit::combo_box(st.page, bit_spec);
            // 行内 10px 注释位 = 格式能力约束说明（R3：提示不占标签位；占位符 " " 保证注释位存在）
            const formkit::Row row =
                formkit::frow(st.page, PageOutput::tr("位深"), st.bitdepth, FieldWidth::Standard,
                              QString(), QStringLiteral(" "));
            row.row->setObjectName(QStringLiteral("pp-bitdepth-row"));
            st.bitdepth_note = row.note;
            st.bitdepth_note->setObjectName(QStringLiteral("pp-bitdepth-note-") +
                                            QString::fromStdString(id));
            st.bitdepth_note->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            st.bitdepth_note->setText(QString());
            v->addWidget(row.row);
        }

        // 行 2..6：技术 / 无损 / 核心参数 / 高级参数（ParamForm 一行一义）
        st.form = new ParamForm(*f, effective_backends(*f), st.page);
        st.form->setObjectName(QStringLiteral("pp-param-form-") + QString::fromStdString(id));
        st.form->set_tokens(tokens);
        v->addWidget(st.form);

        // avif 10bit+alpha 预选提示（原 pp-alpha-hint 语义，位置随格式页签）
        st.alpha_hint = new ElidedLabel(st.page);
        st.alpha_hint->setObjectName(QStringLiteral("pp-alpha-hint"));
        st.alpha_hint->setProperty(kFieldProperty, QStringLiteral("hint"));
        st.alpha_hint->setFont(theme::font(theme::Typography::hint_px));
        st.alpha_hint->setMinimumHeight(formkit::kRowPitch);
        st.alpha_hint->setContentsMargins(formkit::kLabelW + formkit::kColGap, 0, 0, 0);
        st.alpha_hint->set_full_text(
            PageOutput::tr("10 位 + 含 alpha：已选择 libaom 后端（SVT-AV1 不支持该组合）"));
        st.alpha_hint->setVisible(false);
        v->addWidget(st.alpha_hint);

        QObject::connect(st.form, &ParamForm::selection_changed, q,
                         [this, id](const FormSelection &) { on_param_selection_changed(id); });
        QObject::connect(st.form, &ParamForm::changed, q, [this, id] {
            refresh_changed_pill();
            refresh_tab_labels();
            Q_UNUSED(id);
            emit q->config_changed();
        });
        QObject::connect(st.bitdepth, &QComboBox::currentIndexChanged, q,
                         [this, id](int) { on_bitdepth_changed(id); });

        // 初值：优先快照（取消勾选后重选），否则表内默认
        apply_state_to_form(id, st.spec);
        tabs->addTab(st.page, format_tab_label(id));
        sync_tab_current();
    }

    void remove_tab(const std::string &id) {
        FormatState *st = state_of(id);
        if (st == nullptr || st->page == nullptr)
            return;
        const int index = tabs->indexOf(st->page);
        if (index >= 0)
            tabs->removeTab(index); // 不删除页面（快照保留在 states 里，重选可复用）
        st->page->hide();
        st->page->setParent(nullptr);
        st->page->deleteLater();
        st->page = nullptr;
        st->form = nullptr;
        st->bitdepth = nullptr;
        st->bitdepth_note = nullptr;
        st->alpha_hint = nullptr;
    }

    // 取消勾选前把当前控件状态快照进 spec（重选时还原用户编辑）
    void snapshot_state(const std::string &id) {
        FormatState *st = state_of(id);
        if (st == nullptr || st->form == nullptr)
            return;
        st->spec.format_id = id;
        const FormSelection sel = st->form->selection();
        st->spec.backend_id = sel.backend;
        st->spec.tech_id = sel.tech;
        st->spec.params = st->form->values();
        st->spec.params[std::string(pp::kLosslessKey)] = sel.lossless;
        st->spec.out_bitdepth = bitdepth_of(id);
    }

    // 页签文案 = 格式名 · 位深（§7.2 页签；改动计数改由卡头中性 pill 承担）
    QString format_tab_label(const std::string &id) const {
        const pp::FormatDef *f = pp::find_format(id);
        const QString name =
            f != nullptr ? QString::fromStdString(f->label) : QString::fromStdString(id);
        const int depth = bitdepth_of(id);
        return depth > 0 ? name + QStringLiteral(" · ") + PageOutput::tr("%1 位").arg(depth) : name;
    }

    // 卡头中性 pill：当前页签格式「已改 N」（值 ≠ 表内默认的参数项数；修 2-07 计数用中性档）
    void refresh_changed_pill() {
        if (changed_pill == nullptr)
            return;
        const int changed = changed_param_count(active_id);
        changed_pill->setText(PageOutput::tr("已改 %1").arg(changed));
        changed_pill->setVisible(changed > 0);
    }

    // 页签计数：值 ≠ 表内默认 的参数项数
    int changed_param_count(const std::string &id) const {
        const FormatState *st = state_of(id);
        if (st == nullptr || st->form == nullptr)
            return 0;
        const pp::FormatDef *f = pp::find_format(id);
        if (f == nullptr)
            return 0;
        const FormSelection sel = st->form->selection();
        // M4-T13b（D-W3-1 根因）：tech_by_id 返回的是**入参容器内**的指针，
        // 而 effective_backends 按值返回 → 必须先把临时绑定到具名局部再取指针，
        // 否则 t 在语句结束即悬垂（ASAN: heap-use-after-free @ page_output.cpp）。
        const std::vector<pp::BackendDef> backends = effective_backends(*f);
        const pp::TechDef *t = tech_by_id(backends, sel.backend, sel.tech);
        if (t == nullptr)
            return 0;
        const pp::ParamSet values = st->form->values();
        int count = 0;
        for (const pp::ParamDef &p : t->params) {
            if (p.key.rfind("__", 0) == 0)
                continue;
            const auto it = values.find(p.key);
            if (it == values.end())
                continue;
            const bool same_type = it->second.index() == p.def.index();
            if (!same_type || !(it->second == p.def))
                ++count;
        }
        return count;
    }

    void refresh_tab_labels() {
        for (int i = 0; i < static_cast<int>(selected.size()); ++i)
            tabs->setTabText(i, format_tab_label(selected[static_cast<std::size_t>(i)]));
    }

    void update_format_count() {
        if (format_pill == nullptr)
            return;
        format_pill->setText(PageOutput::tr("已选 %1").arg(selected.size()));
        format_pill->setVisible(true);
    }

    // ------------------------------------------------------------ 参数状态

    void on_param_selection_changed(const std::string &id) {
        const FormatState *st = state_of(id);
        if (st == nullptr || st->form == nullptr)
            return;
        const std::string now = st->form->selection().backend;
        if (now != last_backend) {
            user_backend_override = true; // §9.1：用户手动改后端 → 之后不再自动抢占
            last_backend = now;
        }
        set_bitdepth_options(id, -1, /*keep_if_valid=*/true);
        evaluate_avif_alpha_preselect(id); // 后端/位深变化可能使预选条件成立
        update_alpha_hint(id);
        refresh_tab_labels();
        refresh_changed_pill();
    }

    void on_bitdepth_changed(const std::string &id) {
        evaluate_avif_alpha_preselect(id);
        update_alpha_hint(id);
        refresh_tab_labels(); // 位深入页签文案
        emit q->config_changed();
    }

    // ------------------------------------------------------------ 位深

    void repopulate_bitdepths(FormatState &st, const std::vector<int> &allowed, int wanted) {
        const QSignalBlocker blocker(st.bitdepth);
        st.bitdepth->clear();
        int index = -1;
        for (std::size_t i = 0; i < allowed.size(); ++i) {
            st.bitdepth->addItem(PageOutput::tr("%1 位").arg(allowed[i]), allowed[i]);
            if (allowed[i] == wanted)
                index = static_cast<int>(i);
        }
        if (index >= 0)
            st.bitdepth->setCurrentIndex(index);
    }

    // forced > 0：apply_preset 指定值（不在交集则退回默认规则）；
    // keep_if_valid：后端切换时当前位深仍有效则保留。返回是否发生变化。
    bool set_bitdepth_options(const std::string &id, int forced, bool keep_if_valid) {
        const pp::FormatDef *f = pp::find_format(id);
        FormatState *st = state_of(id);
        if (f == nullptr || st == nullptr || st->bitdepth == nullptr)
            return false;
        const std::vector<int> allowed =
            allowed_bitdepths(*f, st->form != nullptr ? st->form->selection().backend : "");
        if (allowed.empty())
            return false;
        const int current = st->bitdepth->currentData().toInt();
        int resolved = 0;
        if (forced > 0 && contains(allowed, forced)) {
            resolved = forced;
        } else if (keep_if_valid && contains(allowed, current)) {
            resolved = current;
        } else {
            resolved = default_bitdepth(f->id);
            if (!contains(allowed, resolved))
                resolved = contains(allowed, 8) ? 8 : allowed.front(); // §2.9.2
        }
        repopulate_bitdepths(*st, allowed, resolved);
        // 格式能力约束说明（如 JPEG 仅 8 位）：排在同行控件右侧（行内提示不占标签位）
        if (st->bitdepth_note != nullptr) {
            const bool locked = allowed.size() == 1;
            st->bitdepth_note->setText(locked ? PageOutput::tr("%1 仅支持 %2 位输出")
                                                    .arg(QString::fromStdString(f->label))
                                                    .arg(allowed.front())
                                              : QString());
        }
        return resolved != current;
    }

    // ------------------------------------------------------------ 落值 / 读取

    // 把 OutputFormatSpec 落到该格式的页签控件（无信号：ParamForm 的 set_* 是程序化接口）
    void apply_state_to_form(const std::string &id, const pp::OutputFormatSpec &spec) {
        FormatState *st = state_of(id);
        const pp::FormatDef *f = pp::find_format(id);
        if (st == nullptr || st->form == nullptr || f == nullptr)
            return;
        const bool have_spec = !spec.format_id.empty();
        const bool lossless = have_spec ? pp::lossless_flag(spec.params) : false;
        if (have_spec) {
            FormSelection sel;
            sel.backend = spec.backend_id;
            sel.tech = spec.tech_id;
            sel.lossless = lossless;
            st->form->set_selection(sel);
            st->form->set_values(spec.params);
            st->form->set_values(spec.params); // 二次落值：切技术后补齐同键项（幂等）
        }
        set_bitdepth_options(id, have_spec ? spec.out_bitdepth : -1, /*keep_if_valid=*/false);
        evaluate_avif_alpha_preselect(id);
        update_alpha_hint(id);
        refresh_changed_pill();
    }

    pp::OutputFormatSpec spec_of(const std::string &id) const {
        pp::OutputFormatSpec spec;
        spec.format_id = id;
        const FormatState *st = state_of(id);
        if (st == nullptr || st->form == nullptr) {
            // 无页签（理论上不出现：选中即建页签）→ 回退表内默认
            const pp::FormatDef *f = pp::find_format(id);
            if (f != nullptr && !f->backends.empty()) {
                spec.backend_id = f->backends.front().id;
                // 同上：effective_backends 按值返回，指针必须落在具名局部上（否则下一语句即悬垂）
                const std::vector<pp::BackendDef> backends = effective_backends(*f);
                const pp::TechDef *t = tech_by_id(backends, spec.backend_id, "");
                spec.tech_id = t != nullptr ? t->id : std::string();
                spec.params = default_params_for(id, spec.backend_id, spec.tech_id, false);
                spec.out_bitdepth = default_bitdepth(id);
            }
            return spec;
        }
        const FormSelection sel = st->form->selection();
        spec.backend_id = sel.backend;
        spec.tech_id = sel.tech;
        spec.params = st->form->values();
        spec.params[std::string(pp::kLosslessKey)] = sel.lossless; // 内部管道键（引擎侧）
        spec.out_bitdepth = bitdepth_of(id);
        return spec;
    }

    pp::ParamSet default_params_for(const std::string &id, const std::string &backend,
                                    const std::string &tech, bool lossless) const {
        const pp::FormatDef *f = pp::find_format(id);
        return f != nullptr ? pp::default_params(*f, backend, tech, lossless) : pp::ParamSet{};
    }

    // ------------------------------------------------------------ avif + alpha / 仅元数据

    void update_alpha_hint(const std::string &id) {
        FormatState *st = state_of(id);
        if (st == nullptr || st->alpha_hint == nullptr)
            return;
        const bool show =
            !metadata_only && batch_has_alpha && id == "avif" && bitdepth_of(id) == 10;
        st->alpha_hint->setVisible(show);
    }

    // §9.1 [高]：**触发条件求值**（不依赖 set_batch_has_alpha 单点触发）。
    // 条件：avif ∧ batch_has_alpha ∧ 位深==10 ∧ 后端==svt-av1 ∧ 用户未手动改过后端。
    bool evaluate_avif_alpha_preselect(const std::string &id) {
        FormatState *st = state_of(id);
        if (st == nullptr || st->form == nullptr || user_backend_override)
            return false;
        if (!batch_has_alpha || id != "avif")
            return false;
        if (bitdepth_of(id) != 10)
            return false;
        const FormSelection sel = st->form->selection();
        if (sel.backend != "svt-av1")
            return false;
        const pp::FormatDef *f = pp::find_format(id);
        if (f == nullptr || !has_backend(effective_backends(*f), "libaom"))
            return false;
        FormSelection next = sel;
        next.backend = "libaom";
        st->form->set_selection(next); // 程序化：不发信号
        last_backend = next.backend;
        set_bitdepth_options(id, -1, /*keep_if_valid=*/true);
        return true;
    }

    void update_metadata_only_state() {
        const bool enabled = !metadata_only;
        // 互斥置灰：磁贴多选区 + 格式参数卡（模式分段自身保持可用 —— 否则无法切回）
        tiles_host->setEnabled(enabled);
        param_card->setEnabled(enabled);
        // v0.2 语义（M1b §2.9）：仅元数据不做色彩转换 → 色彩目标一并禁用
        color_combo->setEnabled(enabled);
        mode_note->setVisible(metadata_only);
        {
            const QSignalBlocker blocker(mode_seg);
            mode_seg->set_current_index(metadata_only ? 1 : 0);
        }
        for (const auto &[id, st] : states) {
            if (st.alpha_hint != nullptr)
                st.alpha_hint->setVisible(!metadata_only && batch_has_alpha && id == "avif" &&
                                          bitdepth_of(id) == 10);
        }
    }

    // ------------------------------------------------------------ 模板 / 示例

    static bool template_has_format(const QString &tmpl) {
        const QStringList segs = tmpl.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString &seg : segs)
            if (seg == QLatin1String("$format"))
                return true;
        return false;
    }

    void sync_split_switch(const QString &tmpl) {
        if (split_switch == nullptr)
            return;
        const QSignalBlocker blocker(split_switch);
        split_switch->setChecked(template_has_format(tmpl));
    }

    void apply_split_to_template(bool on) {
        if (template_edit == nullptr)
            return;
        QString tmpl = template_edit->text();
        QStringList segs = tmpl.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const bool has = template_has_format(tmpl);
        if (on && !has) {
            segs.prepend(QStringLiteral("$format"));
        } else if (!on && has) {
            segs.removeAll(QStringLiteral("$format"));
            if (segs.isEmpty())
                segs << QStringLiteral("$dir") << QStringLiteral("$file");
        } else {
            return;
        }
        template_edit->setText(segs.join(QLatin1Char('/'))); // textChanged → 示例/信号
    }

    // 输出示例（注释行）：自然语、单行、防溢出（修 2-14 统一 $var 语法/ 2-15 不再 "+..." 堆叠）
    void update_template_example() {
        if (template_example == nullptr || template_edit == nullptr)
            return;
        const QString tmpl = template_edit->text();
        std::string err;
        if (!pp::validate_output_template(tmpl.toStdString(), &err)) {
            template_example->setProperty("ppInvalid", true);
            theme::repolish(template_example);
            template_example->set_full_text(
                PageOutput::tr("模板无效：%1").arg(QString::fromStdString(err)));
            return;
        }
        template_example->setProperty("ppInvalid", false);
        theme::repolish(template_example);
        QStringList samples;
        for (const std::string &id : selected) {
            const pp::FormatDef *f = pp::find_format(id);
            if (f == nullptr)
                continue;
            pp::PathCtx ctx;
            ctx.format_dir = f->id;
            ctx.rel_dir = std::filesystem::path("2024") / "05";
            ctx.stem = "IMG_2731";
            ctx.ext = f->ext;
            const std::filesystem::path out =
                pp::render_output_path(tmpl.toStdString(), ctx, std::filesystem::path("."));
            if (out.empty())
                continue;
            QString rel = QString::fromStdString(out.generic_string());
            if (rel.startsWith(QStringLiteral("./")))
                rel.remove(0, 2);
            samples << QStringLiteral("…/") +
                           rel; // 根目录前缀以 "…" 表示（与模板语法同族，无 <根>）
            if (samples.size() == 2)
                break;
        }
        if (selected.empty()) {
            template_example->set_full_text(PageOutput::tr("选择输出格式后显示输出路径示例"));
        } else if (samples.isEmpty()) {
            template_example->set_full_text(PageOutput::tr("模板为空的段（无可展开的路径）"));
        } else if (selected.size() > 1) {
            template_example->set_full_text(
                samples.first() + QLatin1Char(' ') +
                PageOutput::tr("等 %1 个输出路径").arg(selected.size()));
        } else {
            template_example->set_full_text(samples.first());
        }
        template_example->setToolTip(template_tooltip() + QLatin1Char('\n') +
                                     template_example->full_text());
    }

    // ------------------------------------------------------------ 预估（④）

    // 预估卡：预计输出 = 勾选文件数 × 已选格式数（仅元数据恒 1 个格式 —— §3.2 硬校验）；
    // 体积/耗时无实测依据 → 中性空态「—」（§8：未发生的量不显示；tooltip 说明）。
    void update_estimate() {
        if (est_output == nullptr)
            return;
        const int formats = metadata_only ? 1 : static_cast<int>(selected.size());
        const int outputs = input_count > 0 && formats > 0 ? input_count * formats : 0;
        est_output->set_full_text(outputs > 0 ? PageOutput::tr("%1 个").arg(outputs)
                                              : PageOutput::tr("—"));
        est_output->setToolTip(PageOutput::tr("每个已选文件都会为每个已选格式生成一个输出文件"));
        est_size->set_full_text(PageOutput::tr("—"));
        est_time->set_full_text(PageOutput::tr("—"));
        const QString root = out_root_edit != nullptr ? out_root_edit->text().trimmed() : QString();
        QString disk = PageOutput::tr("—");
        QString disk_tip = PageOutput::tr("填写输出根目录后显示可用空间");
        if (!root.isEmpty()) {
            const QStorageInfo info(root);
            if (info.isValid() && info.isReady()) {
                disk = PageOutput::tr("%1 可用").arg(human_bytes(info.bytesAvailable()));
                disk_tip = PageOutput::tr("输出根目录所在卷的可用空间：%1").arg(root);
            } else {
                disk_tip = PageOutput::tr("无法读取输出根目录所在卷的可用空间：%1").arg(root);
            }
        }
        est_disk->set_full_text(disk);
        est_disk->setToolTip(disk_tip);
        if (preset_hint != nullptr)
            preset_hint->set_full_text(input_count > 0
                                           ? PageOutput::tr("基于选中 %1 个文件").arg(input_count)
                                           : QString());
    }

    // 修 2-25：保存/删除的可用态随"是否有当前预设"走，禁用原因写进 tooltip（不静默灰）
    void sync_preset_buttons() {
        if (preset_combo == nullptr)
            return;
        const int index = preset_combo->currentIndex();
        const QString path =
            index >= 0 ? preset_combo->itemData(index, Qt::UserRole).toString() : QString();
        const bool has = !path.isEmpty();
        preset_save->setEnabled(has);
        preset_delete->setEnabled(has);
        preset_save->setToolTip(has ? PageOutput::tr("保存到当前预设（覆盖同名文件）")
                                    : PageOutput::tr("未选择预设：请先「另存…」保存一份新预设"));
        preset_delete->setToolTip(has ? PageOutput::tr("删除当前预设（会二次确认）")
                                      : PageOutput::tr("未选择预设：无可删除项"));
    }
};

// 指针键注册表：析构擦除 → 无悬挂/别名；GUI 线程独占，无需锁。
std::map<const PageOutput *, std::unique_ptr<Impl>> &registry() {
    static std::map<const PageOutput *, std::unique_ptr<Impl>> store;
    return store;
}

Impl *impl_of(const PageOutput *page) {
    const auto it = registry().find(page);
    return it == registry().end() ? nullptr : it->second.get();
}

} // namespace

// ---------------------------------------------------------------- PageOutput

PageOutput::PageOutput(QWidget *parent) : QWidget(parent) {
    auto impl = std::make_unique<Impl>();
    impl->q = this;
    impl->tokens = theme::tokens(theme::ThemeMode::Dark);
    impl->build_ui();
    impl->wire();
    impl->toggle_format("jxl", true, /*user=*/false); // §2.9.1 默认 jxl（0.3.0：默认单选）
    registry().emplace(this, std::move(impl));
    set_tokens(theme::tokens(theme::ThemeMode::Dark)); // 统一走注入口（QSS + 自绘控件）
}

PageOutput::~PageOutput() { registry().erase(this); }

pp::RunConfig PageOutput::config_base() const {
    Impl *impl_ = impl_of(this);
    pp::RunConfig cfg;
    const QString root = impl_->out_root_edit->text().trimmed();
    if (!root.isEmpty())
        cfg.out_root = std::filesystem::path(root.toStdString());
    // 多格式（§3.2）：已选集合逐项落成 OutputFormatSpec；仅元数据模式恒 1 项（§3.2 硬校验）
    if (impl_->metadata_only) {
        const std::string id =
            impl_->is_selected(impl_->active_id)
                ? impl_->active_id
                : (impl_->selected.empty() ? impl_->active_id : impl_->selected.front());
        cfg.outputs.push_back(impl_->spec_of(id));
    } else {
        for (const std::string &id : impl_->selected)
            cfg.outputs.push_back(impl_->spec_of(id));
    }
    cfg.color = impl_->color_target();
    cfg.conflict = impl_->conflict();
    cfg.metadata_only = impl_->metadata_only;
    cfg.output_template = impl_->template_text().toStdString();
    cfg.split_by_format = Impl::template_has_format(impl_->template_text());
    // rules / workers / budget_bytes / rotate_orientation / flatten_gray / stagger / thread_budget
    // 由 MainWindow 填充
    return cfg;
}

bool PageOutput::metadata_only() const {
    Impl *impl_ = impl_of(this);
    return impl_->metadata_only;
}

void PageOutput::set_metadata_only(bool on) {
    Impl *impl_ = impl_of(this);
    if (impl_->metadata_only == on)
        return;
    impl_->metadata_only = on;
    impl_->update_metadata_only_state();
    impl_->update_estimate();
    emit config_changed();
}

QString PageOutput::out_root() const {
    Impl *impl_ = impl_of(this);
    return impl_->out_root_edit->text();
}

void PageOutput::set_out_root(const QString &dir) {
    Impl *impl_ = impl_of(this);
    if (impl_->out_root_edit->text() == dir)
        return;
    const QSignalBlocker blocker(impl_->out_root_edit);
    impl_->out_root_edit->setText(dir);
    impl_->update_template_example();
    impl_->update_estimate();
    emit config_changed();
}

void PageOutput::set_batch_has_alpha(bool has) {
    Impl *impl_ = impl_of(this);
    impl_->user_backend_override = false; // §9.1：本入口清除用户手动覆盖
    const bool state_changed = (impl_->batch_has_alpha != has);
    impl_->batch_has_alpha = has;
    bool switched = false;
    for (const std::string &id : impl_->selected)
        switched = impl_->evaluate_avif_alpha_preselect(id) || switched;
    for (const std::string &id : impl_->selected)
        impl_->update_alpha_hint(id);
    if (state_changed || switched)
        emit config_changed();
}

void PageOutput::apply_preset(const pp::PresetData &p) {
    Impl *impl_ = impl_of(this);
    pp::PresetData preset = p;
    pp::normalize_preset(preset); // 先用当前格式表补齐
    if (!pp::validate_preset(preset).empty())
        return; // 校验失败 → 不应用，保持原状（加载失败显式报错由调用方负责，R31）

    // 已选集合 = 预设的 outputs（规范顺序）；逐项落值
    impl_->selected.clear();
    for (const pp::OutputFormatSpec &spec : preset.outputs) {
        if (pp::find_format(spec.format_id) == nullptr)
            continue;
        if (!impl_->is_selected(spec.format_id))
            impl_->selected.push_back(spec.format_id);
    }
    std::sort(impl_->selected.begin(), impl_->selected.end(),
              [](const std::string &a, const std::string &b) {
                  return Impl::tile_index(a) < Impl::tile_index(b);
              });
    // 清掉不再选中的页签，建/复用所选页签
    for (const FormatEntry &e : kFormats) {
        if (!impl_->is_selected(e.id))
            impl_->remove_tab(e.id);
    }
    for (const std::string &id : impl_->selected)
        impl_->build_tab(id);
    if (!impl_->selected.empty())
        impl_->active_id = impl_->selected.front();
    for (const pp::OutputFormatSpec &spec : preset.outputs) {
        if (!impl_->is_selected(spec.format_id))
            continue;
        impl_->states[spec.format_id].spec = spec;
        impl_->apply_state_to_form(spec.format_id, spec);
    }
    impl_->sync_tiles();
    impl_->sync_tab_current();
    impl_->update_format_count();
    impl_->update_metadata_only_state();

    // 全局字段
    {
        const QSignalBlocker blocker(impl_->color_combo);
        impl_->color_combo->setCurrentIndex(color_to_index(preset.color_target));
    }
    {
        const QSignalBlocker blocker(impl_->conflict_combo);
        impl_->conflict_combo->setCurrentIndex(conflict_to_index(preset.conflict));
    }
    {
        const QSignalBlocker blocker(impl_->template_edit);
        impl_->template_edit->setText(QString::fromStdString(preset.output_template));
    }
    impl_->sync_split_switch(QString::fromStdString(preset.output_template));
    impl_->update_template_example();
    impl_->update_estimate();
    impl_->refresh_tab_labels();
    impl_->refresh_changed_pill();
    if (!impl_->selected.empty())
        emit format_changed(QString::fromStdString(impl_->active_id));
    emit config_changed();
}

pp::PresetData PageOutput::collect_preset(const QString &name) const {
    Impl *impl_ = impl_of(this);
    pp::PresetData p;
    p.version = 2;
    p.name = name.toStdString();
    for (const std::string &id : impl_->selected)
        p.outputs.push_back(impl_->spec_of(id));
    p.output_template = impl_->template_text().toStdString();
    p.split_by_format = Impl::template_has_format(impl_->template_text());
    p.color_target = impl_->color_target();
    p.conflict = impl_->conflict();
    // rules 由 MainWindow 合并（§2.9）
    return p;
}

void PageOutput::restore_last(const QString &format_id, const QString &out_root) {
    Impl *impl_ = impl_of(this);
    // 冻结契约（page_output.h「session restore (no signals); invalid values ignored」）：
    // 会话恢复全程**不发信号** —— 0.3.0 的多格式路径（select_format / set_out_root）内部会
    // emit config_changed，故在此对整页屏蔽（内部更新全是直接调用，不依赖信号）。
    const QSignalBlocker blocker(this);
    if (!format_id.isEmpty() && pp::find_format(format_id.toStdString()) != nullptr)
        select_format(format_id);
    if (!out_root.isEmpty())
        set_out_root(out_root);
    // 原 0.2 实现的收尾动作（等价物）：全部已选格式重算 alpha 提示
    for (const std::string &id : impl_->selected)
        impl_->update_alpha_hint(id);
}

QString PageOutput::ready_to_start() const {
    Impl *impl_ = impl_of(this);
    const QString root = impl_->out_root_edit->text().trimmed();
    if (root.isEmpty())
        return tr("未设置输出根目录");
    if (!QDir::isAbsolutePath(root))
        return tr("输出根目录必须是绝对路径");
    if (!impl_->metadata_only && impl_->selected.empty())
        return tr("未选择输出格式");
    // §4.2（复核项 10）：非法路径模板必须在 ready_to_start 硬阻断（未知符号 / .. / 绝对路径）
    {
        std::string tmpl_err;
        if (!pp::validate_output_template(impl_->template_text().toStdString(), &tmpl_err))
            return QString::fromStdString(tmpl_err);
        if (impl_->template_text().trimmed().isEmpty())
            return tr("路径模板不能为空");
    }
    return QString();
}

QString PageOutput::current_format() const {
    Impl *impl_ = impl_of(this);
    return QString::fromStdString(impl_->active_id);
}

void PageOutput::select_format(const QString &format_id) {
    Impl *impl_ = impl_of(this);
    if (format_id.isEmpty())
        return;
    const std::string id = format_id.toStdString();
    if (pp::find_format(id) == nullptr)
        return;
    // 冒烟/走查钩子：收敛为单选并激活该页签（多选走磁贴点击）
    QSignalBlocker blocker(impl_->tiles[static_cast<std::size_t>(Impl::tile_index(id))]);
    for (const FormatEntry &e : kFormats) {
        if (e.id != id && impl_->is_selected(e.id))
            impl_->toggle_format(e.id, false, /*user=*/false);
    }
    impl_->toggle_format(id, true, /*user=*/false);
    impl_->active_id = id;
    impl_->sync_tab_current();
    impl_->update_template_example();
    impl_->refresh_tab_labels();
    impl_->refresh_changed_pill();
}

// ---------------------------------------------------------------- 追加面（M4-T13 / M5-T8）

void PageOutput::set_input_count(int files) {
    Impl *impl_ = impl_of(this);
    const int next = files > 0 ? files : 0;
    if (impl_->input_count == next)
        return;
    impl_->input_count = next;
    impl_->update_estimate(); // 只刷新预估读数（不发信号 → 不触发 refresh_status 回环）
}

void PageOutput::set_tokens(const theme::Tokens &tokens) {
    Impl *impl_ = impl_of(this);
    impl_->tokens = tokens;
    for (formkit::CheckableCard *tile : impl_->tiles) {
        if (tile != nullptr)
            tile->set_tokens(tokens);
    }
    if (impl_->split_switch != nullptr)
        impl_->split_switch->set_tokens(tokens);
    if (impl_->mode_seg != nullptr)
        impl_->mode_seg->set_tokens(tokens);
    if (impl_->format_pill != nullptr)
        impl_->format_pill->set_tokens(tokens);
    if (impl_->changed_pill != nullptr)
        impl_->changed_pill->set_tokens(tokens);
    for (auto &[id, st] : impl_->states) {
        Q_UNUSED(id);
        if (st.form != nullptr)
            st.form->set_tokens(tokens);
    }
    // 页面 QSS（卡片/徽标/输入类控件/页签；与 PageMeta 同一注入口径）
    impl_->q->setStyleSheet(page_qss(tokens));
    // M5-T3：矢量图标随 tokens 重放（QIcon 取色不吃 QSS 的 color）—— 「添加格式…」的 ＋
    if (impl_->add_format_button != nullptr)
        icons::apply(impl_->add_format_button, icons::Name::Add, tokens.accent, tokens.text3, 12);
    // 浅色卡阴影（§4.1 shadow：亮 0 1px 4px；深色无投影）
    const qreal dpr = impl_->q->devicePixelRatioF() > 0.01 ? impl_->q->devicePixelRatioF() : 1.0;
    const std::array<QFrame *, 4> cards = {impl_->format_card, impl_->global_card,
                                           impl_->param_card, impl_->preset_card};
    for (QFrame *card : cards) {
        if (card == nullptr)
            continue;
        if (tokens.shadow_blur > 0) {
            auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(card->graphicsEffect());
            if (shadow == nullptr) {
                shadow = new QGraphicsDropShadowEffect(card);
                card->setGraphicsEffect(shadow);
            }
            shadow->setColor(tokens.shadow_color);
            shadow->setBlurRadius(double(tokens.shadow_blur) * dpr);
            shadow->setXOffset(0);
            shadow->setYOffset(double(tokens.shadow_dy) * dpr);
        } else if (card->graphicsEffect() != nullptr) {
            card->setGraphicsEffect(nullptr); // 删旧效果（setGraphicsEffect(nullptr) 会析构它）
        }
    }
}

void PageOutput::set_preset_list(const std::vector<std::pair<QString, QString>> &presets) {
    Impl *impl_ = impl_of(this);
    const QSignalBlocker blocker(impl_->preset_combo);
    const QString current =
        impl_->preset_combo->currentIndex() >= 0
            ? impl_->preset_combo->itemData(impl_->preset_combo->currentIndex(), Qt::UserRole)
                  .toString()
            : QString();
    impl_->preset_combo->clear();
    for (const auto &[path, name] : presets)
        impl_->preset_combo->addItem(name, path);
    // 修 2-24：无当前预设 → 保持未选中（显示 placeholder「未选择预设」），不再默认指向首项
    int index = -1;
    if (!current.isEmpty()) {
        for (int i = 0; i < impl_->preset_combo->count(); ++i) {
            if (impl_->preset_combo->itemData(i, Qt::UserRole).toString() == current) {
                index = i;
                break;
            }
        }
    }
    impl_->preset_combo->setCurrentIndex(index);
    impl_->sync_preset_buttons();
}

void PageOutput::set_current_preset(const QString &path) {
    Impl *impl_ = impl_of(this);
    if (impl_->preset_combo == nullptr)
        return;
    const QSignalBlocker blocker(impl_->preset_combo);
    for (int i = 0; i < impl_->preset_combo->count(); ++i) {
        if (impl_->preset_combo->itemData(i, Qt::UserRole).toString() == path) {
            impl_->preset_combo->setCurrentIndex(i);
            break;
        }
    }
    impl_->sync_preset_buttons();
}

QString PageOutput::output_template() const {
    Impl *impl_ = impl_of(this);
    return impl_->template_text();
}

void PageOutput::set_output_template(const QString &tmpl) {
    Impl *impl_ = impl_of(this);
    if (impl_->template_text() == tmpl)
        return;
    const QSignalBlocker blocker(impl_->template_edit);
    impl_->template_edit->setText(tmpl);
    impl_->sync_split_switch(tmpl);
    impl_->update_template_example();
}

bool PageOutput::split_by_format() const {
    Impl *impl_ = impl_of(this);
    return Impl::template_has_format(impl_->template_text());
}

void PageOutput::set_split_by_format(bool on) {
    Impl *impl_ = impl_of(this);
    if (split_by_format() == on)
        return;
    const QSignalBlocker blocker(impl_->split_switch);
    impl_->split_switch->setChecked(on);
    impl_->apply_split_to_template(on);
}

} // namespace pp::ui
