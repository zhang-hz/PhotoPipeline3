// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M4-W3-T13）— 输出页：多格式磁贴 + 路径模板 + 预设 v2 + lossless 显式参数
//
// 规格 = docs/v0.3.0-design.md §3.2/§3.3/§4.2/§9.3 + docs/v0.3.0-consensus.md §3.3（需求 3 展开）
//        + 决策 D5/D6/D7 + docs/mockups/output-dark.html（**精确规格**）+
//        output-dark.png（视觉参照）：
//   ① 模式卡：转码 / 仅元数据 seg（D7：切仅元数据 → 格式多选区+格式参数卡禁用并出提示）；
//   ② 输出格式卡：8 磁贴复选 + 头部「已选 N」计数徽标（多选 = 1 输入 → N 输出）；
//   ③ 全局卡（**一行一字段 70px 统一轴**）：输出根目录 / 同名冲突 / 色彩目标 / 单行分文件夹开关 /
//      路径模板（中性输入框；符号表与示例收进悬浮提示）+ 单行示例路径（实时、超长省略、不折行）；
//   ④ 格式参数卡：QTabWidget 逐已选格式页签（页签内含位深 + ParamForm 原样嵌入；页签带改动计数
//      徽标；右端「＋ 添加格式…」入口）；
//   ⑤ 预设卡：预设下拉 + 保存 / 另存… / 删除（读取 = 下拉选中即载入；文件 I/O 与 rules 合并归
//      MainWindow —— 单一持有者）。
//
// 冻结契约（PP-FROZEN，见 page_output.h 的追加面说明）：既有方法签名/语义不变；本文件是
// M1b 单格式页的 0.3.0 多格式实现，既有"冻结行为"逐条保留：
//   * 位深可选集 = FormatDef.bitdepths ∩ probe_bitdepth_support()（heif/avif；探测按
//     (format, backend) 进程内缓存一次）；
//   * introspect_backends() 的结果按静态表顺序归一（avif 默认后端 = svt-av1）；
//   * avif + alpha 预选 = **触发条件求值**（进入 avif / 后端 / 位深 / alpha 变化时求值），
//     用户手动改过后端 → user_backend_override（set_batch_has_alpha 清除）；
//   * 预设落值后同样求值；`select_format()` 是冒烟/走查钩子（收敛为单选 + 激活该页签）。
//
// lossless（出口硬项）：无损 = **显式 schema 参数** `lossless`（src/core/params.h 的
// kLosslessParamKey；format_tables.cpp 为 jxl vardct/modular、webp lossy/lossless 声明；
// heif/avif 由 libheif 内省提供）+ 内部管道键 `__lossless`（谓词/编码器输入，0.3.0 保留）。
// 本页的「无损」复选框（ParamForm 顶部，objectName pp-lossless-check）是两个承载面的控制点；
// ParamForm/params 引擎保持两键同步 → 预设 v2 序列化含该键、config_base 下发的参数集含该键。
//
// 排版硬约束（§9.3）：表单行 = 70px 右对齐标签列 + 12px 列距 + 32px 行高（theme::Metrics）；
// 全页禁折行（ElidedLabel 省略号 + 悬浮提示）；卡片间距 10px；objectName 全部 pp-* 钩子。
//
// 落地口径（设计未逐字给出处，逐条记账）：
//   * **磁贴顺序**按 M4 原型（精确规格）：JPEG · WebP · JPEG XL · HEIF · AVIF · PNG · TIFF · BMP
//     —— 取代 M1b 的单选版顺序（v0.2 的 8 格式集合不变；本页为 0.3.0 多选实现）；
//   * 磁贴扩展名文本取自 `FormatDef.ext`（单源）：HEIF 显示 `.heic`（原型写 `.heif`）；
//   * 页签计数徽标 = 该格式参数表中「值 ≠ 表内默认」的项数（QTabWidget 的页签文案承载，
//     `label` 与数字间以 `·` 分隔；QSS 无法给页签内嵌药丸，故不做 .pill 造型）；
//   * 路径模板示例用固定样例上下文（`2024/05` + `IMG_2731`，与原型 tooltip 同例），
//     前缀 = 当前输出根目录（空 → `<根>`）；多格式时并排显示前两个格式的示例；
//   * 「另存…」复用既有 PresetsDialog（冻结的命名清洗与落盘路径归 MainWindow）。
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
#include <QApplication>
#include <QButtonGroup>
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
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "codecs/encoders.h"
#include "core/fsops.h"
#include "core/params.h"
#include "ui/paramform.h"
#include "ui/theme.h"

namespace pp::ui {
namespace {

// ---------------------------------------------------------------- 格式按钮表

struct FormatEntry {
    const char *id;
    const char *label;
};

// M4 原型 output-dark.html 的磁贴网格顺序（4 列行优先，§9.4「原型 = 精确规格」）
const FormatEntry kFormats[] = {
    {"jpeg", "JPEG"}, {"webp", "WebP"}, {"jxl", "JPEG XL"}, {"heif", "HEIF"},
    {"avif", "AVIF"}, {"png", "PNG"},   {"tiff", "TIFF"},   {"bmp", "BMP"},
};
constexpr int kFormatCount = static_cast<int>(sizeof(kFormats) / sizeof(kFormats[0]));
constexpr int kFormatColumns = 4;

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

// ---------------------------------------------------------------- 小控件

// 单行省略标签（§9.3「全界面禁止文案折行（超长省略号）」；与 mainwindow/page_meta 同口径）
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) { setWordWrap(false); }
    void set_full_text(const QString &text) {
        full_text_ = text;
        setProperty("ppFullText", text); // 自验/走查读语义值（显示文本可能被省略成 "…"）
        setToolTip(text);
        updateGeometry();
        apply_elide();
    }
    QString full_text() const { return full_text_; }
    QSize sizeHint() const override {
        const QSize base = QLabel::sizeHint();
        if (full_text_.isEmpty())
            return base;
        return QSize(fontMetrics().horizontalAdvance(full_text_) + 2, base.height());
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        apply_elide();
    }

private:
    void apply_elide() {
        if (full_text_.isEmpty()) {
            QLabel::clear();
            return;
        }
        if (width() <= 0) {
            QLabel::setText(full_text_);
            return;
        }
        QLabel::setText(fontMetrics().elidedText(full_text_, Qt::ElideRight, std::max(0, width())));
    }
    QString full_text_;
};

// 格式复选磁贴（mockup .fmt）：15px 复选方框 + 名称（12px/700）+ 右端扩展名（9.5px --txt3）；
// 选中 = accent-dim 底 + accent-bd 边（.fmt.on）。自绘（QSS 无法表达方框+右对齐扩展名的组合）。
class FormatTile : public QAbstractButton {
public:
    explicit FormatTile(QWidget *parent = nullptr) : QAbstractButton(parent) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void set_tokens(const theme::Tokens &t) {
        tokens_ = t;
        update();
    }
    void set_labels(const QString &name, const QString &ext) {
        name_ = name;
        ext_ = ext;
        setToolTip(name + QLatin1Char(' ') + ext);
        updateGeometry();
        update();
    }
    QSize sizeHint() const override {
        const QFontMetrics name_fm(theme::font(12.0, QFont::Bold));
        const QFontMetrics ext_fm(theme::font(9.5));
        const int w = kPadX * 2 + theme::Metrics::checkbox_px + kGap +
                      name_fm.horizontalAdvance(name_) + kGap + ext_fm.horizontalAdvance(ext_);
        return QSize(w + 8, kTileHeight);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const theme::Tokens &t = tokens_;
        const bool on = isChecked();
        const bool dim = !isEnabled();
        QColor bg = on ? t.accent_dim : t.control;
        QColor bd = on ? t.accent_bd : t.control_bd;
        if (dim) {
            bg = theme::at_opacity(on ? t.accent_dim : t.control, t.window, kDimAlpha);
            bd = theme::at_opacity(on ? t.accent_bd : t.control_bd, t.window, kDimAlpha);
        } else if (!on && underMouse()) {
            bg = theme::composite(t.control, t.window);
            bd = t.control_bd;
        }
        QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(bd, 1.0));
        p.setBrush(bg);
        p.drawRoundedRect(box, kTileRadius, kTileRadius);

        const int cb = theme::Metrics::checkbox_px;
        const QRectF mark(kPadX, (height() - cb) / 2.0, cb, cb);
        if (on) {
            p.setPen(QPen(t.accent, 1.5));
            p.setBrush(t.accent);
        } else {
            p.setPen(QPen(dim ? theme::at_opacity(t.text3, t.window, kDimAlpha) : t.text3, 1.5));
            p.setBrush(Qt::NoBrush);
        }
        p.drawRoundedRect(mark, theme::Metrics::checkbox_radius, theme::Metrics::checkbox_radius);
        if (on) {
            p.setPen(dim ? theme::at_opacity(t.on_accent, t.window, kDimAlpha) : t.on_accent);
            p.setFont(theme::font(10.0, QFont::Bold));
            p.drawText(mark, Qt::AlignCenter, QStringLiteral("✓"));
        }
        const int name_x = static_cast<int>(mark.right()) + kGap;
        const QFontMetrics ext_fm(theme::font(9.5));
        const int ext_w = ext_fm.horizontalAdvance(ext_);
        const QRect name_rect(name_x, 0, std::max(0, width() - name_x - ext_w - kGap - kPadX),
                              height());
        p.setFont(theme::font(12.0, QFont::Bold));
        const QFontMetrics name_fm(theme::font(12.0, QFont::Bold));
        p.setPen(dim ? theme::at_opacity(t.text, t.window, kDimAlpha) : t.text);
        p.drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                   name_fm.elidedText(name_, Qt::ElideRight, name_rect.width()));
        p.setFont(theme::font(9.5));
        p.setPen(dim ? theme::at_opacity(t.text3, t.window, kDimAlpha) : t.text3);
        p.drawText(QRect(width() - kPadX - ext_w, 0, ext_w, height()),
                   Qt::AlignRight | Qt::AlignVCenter, ext_);
    }

private:
    static constexpr int kPadX = 10;
    static constexpr int kGap = 8;
    static constexpr int kTileHeight = 30;
    static constexpr int kTileRadius = 6;    // mockup .fmt{border-radius:6px}
    static constexpr double kDimAlpha = 0.4; // 禁用态（仅元数据）：.icon-btn.dim 同口径
    theme::Tokens tokens_{};
    QString name_, ext_;
};

// 药丸开关（§9.2 控件规约：34×18 药丸 + 14px 圆钮；mockup .sw / .sw.off）
class PillSwitch : public QAbstractButton {
public:
    explicit PillSwitch(QWidget *parent = nullptr) : QAbstractButton(parent) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    void set_tokens(const theme::Tokens &t) {
        tokens_ = t;
        update();
    }
    QSize sizeHint() const override {
        return QSize(theme::Metrics::switch_w, theme::Metrics::switch_h);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const theme::Tokens &t = tokens_;
        const QRectF track(0, 0, theme::Metrics::switch_w, theme::Metrics::switch_h);
        p.setPen(Qt::NoPen);
        p.setBrush(isChecked() ? t.accent : t.control_bd);
        p.drawRoundedRect(track, theme::Metrics::switch_h / 2.0, theme::Metrics::switch_h / 2.0);
        const double knob = 14.0;
        const double y = (theme::Metrics::switch_h - knob) / 2.0;
        const double x = isChecked() ? theme::Metrics::switch_w - knob - 2.0 : 2.0;
        p.setBrush(isChecked() ? t.on_accent : QColor(0xc9, 0xc9, 0xd2));
        p.drawEllipse(QRectF(x, y, knob, knob));
    }

private:
    theme::Tokens tokens_{};
};

// ---------------------------------------------------------------- 页面样式
// tokens 单源 = ui/theme.h；本页只把 tokens 铺到自己的控件上（与 PageMeta/PreviewPanel 同口径）。
// 选择器一律以 pp-* objectName / ppField 动态属性为锚，避免误伤其它页面控件。
constexpr const char *kFieldProperty = "ppField";

QString page_qss(const theme::Tokens &t) {
    const auto css = [](const QColor &c) { return theme::css_color(c); };
    const QString mod = QString::fromLatin1(kFieldProperty);
    QString qss;
    // 输入类控件（mockup .path-in / .combo：--ctl 底 + --ctl-bd 边 + r=4 + --txt 字）
    const auto control_rule = [&](const QString &selector) {
        return selector + QStringLiteral(" { background: ") + css(t.control) +
               QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
               QStringLiteral("; border-radius: ") +
               QString::number(theme::Metrics::control_radius) + QStringLiteral("px; color: ") +
               css(t.text) + QStringLiteral("; padding: 0 8px; }\n");
    };
    qss += control_rule(QStringLiteral("QLineEdit[") + mod + QStringLiteral("=\"input\"]"));
    qss += control_rule(QStringLiteral("QLineEdit[") + mod + QStringLiteral("=\"mono\"]"));
    qss += control_rule(QStringLiteral("QComboBox[") + mod + QStringLiteral("=\"combo\"]"));
    // 次按钮（mockup .mini-btn：--ctl 底 + --ctl-bd 边 + r=4 + --txt2 字 + 600）
    qss += QStringLiteral("QPushButton[") + mod + QStringLiteral("=\"button\"] { background: ") +
           css(t.control) + QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: ") + QString::number(theme::Metrics::control_radius) +
           QStringLiteral("px; color: ") + css(t.text2) + QStringLiteral("; padding: 0 10px; }\n");
    qss += QStringLiteral("QPushButton[") + mod + QStringLiteral("=\"button\"]:hover { color: ") +
           css(t.text) + QStringLiteral("; }\n");
    // 表单标签（--txt3，右对齐 70px 轴）/ 提示（--txt3）/ 失效提示（--err）
    qss += QStringLiteral("QLabel[") + mod + QStringLiteral("=\"label\"] { color: ") +
           css(t.text3) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + mod + QStringLiteral("=\"hint\"] { color: ") + css(t.text3) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + mod +
           QStringLiteral("=\"hint\"][ppInvalid=\"true\"] { color: ") + css(t.err) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[ppInvalid=\"true\"] { color: ") + css(t.err) +
           QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel#pp-card-hint { color: ") + css(t.text3) + QStringLiteral("; }\n");
    qss +=
        QStringLiteral("QLabel#pp-split-label { color: ") + css(t.text) + QStringLiteral("; }\n");
    // 模式分段（mockup .seg：--ctl 底 + r=6 + 内 2px；选中格 = 亮底 + --txt）
    qss += QStringLiteral("QWidget#pp-output-seg { background: ") + css(t.control) +
           QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: 6px; }\n");
    qss += QStringLiteral("QToolButton[ppSeg=\"seg\"] { background: transparent; border: none; "
                          "border-radius: 4px; color: ") +
           css(t.text2) + QStringLiteral("; padding: 4px 16px; font-weight: 600; }\n");
    qss += QStringLiteral("QToolButton[ppSeg=\"seg\"]:checked { background: ") +
           css(t.dark() ? QColor(255, 255, 255, 26) : QColor(255, 255, 255)) +
           QStringLiteral("; color: ") + css(t.text) + QStringLiteral("; }\n");
    // 格式页签（mockup .tabs/.tab：--txt2 600；选中 = accent-dim 底 + accent-bd 边 + --txt 字）
    qss +=
        QStringLiteral("QTabWidget#pp-format-tabs::pane { border: none; border-top: 1px solid ") +
        css(t.line) + QStringLiteral("; }\n");
    qss += QStringLiteral("QTabWidget#pp-format-tabs QTabBar::tab { background: transparent; "
                          "color: ") +
           css(t.text2) +
           QStringLiteral("; padding: 6px 14px 7px; margin-right: 4px; "
                          "border: 1px solid transparent; border-bottom: none; "
                          "border-top-left-radius: 6px; border-top-right-radius: 6px;"
                          " font-weight: 700; }\n");
    qss += QStringLiteral("QTabWidget#pp-format-tabs QTabBar::tab:selected { background: ") +
           css(t.accent_dim) + QStringLiteral("; border-color: ") + css(t.accent_bd) +
           QStringLiteral("; color: ") + css(t.text) + QStringLiteral("; }\n");
    // 「＋ 添加格式…」（mockup：页签行右端，--txt3 500）
    qss += QStringLiteral("QToolButton#pp-add-format { border: none; background: transparent; "
                          "color: ") +
           css(t.text3) + QStringLiteral("; padding: 4px 10px; }\n");
    qss += QStringLiteral("QToolButton#pp-add-format:hover { color: ") + css(t.accent) +
           QStringLiteral("; }\n");
    // 容器透明（窗口底渐变透出）
    qss += QStringLiteral("QWidget#pp-output-content { background: transparent; }\n");
    qss += QStringLiteral("QScrollArea#pp-output-scroll { background: transparent; }\n");
    qss += QStringLiteral("QScrollArea#pp-output-scroll > QWidget > QWidget { background: "
                          "transparent; }\n");
    return qss;
}

// ---------------------------------------------------------------- 页面状态

struct FormatState {
    pp::OutputFormatSpec spec; // 首次建页签的初值（无则表内默认）
    QWidget *page = nullptr;
    ParamForm *form = nullptr;
    QComboBox *bitdepth = nullptr;
    ElidedLabel *bitdepth_note = nullptr;
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

    // 模式
    QToolButton *mode_convert = nullptr;
    QToolButton *mode_meta = nullptr;
    ElidedLabel *mode_note = nullptr;

    // 输出格式卡
    QFrame *format_card = nullptr;
    FormatTile *tiles[kFormatCount] = {};
    ElidedLabel *format_pill = nullptr;
    ElidedLabel *format_hint = nullptr;

    // 全局卡
    QFrame *global_card = nullptr;
    QLineEdit *out_root_edit = nullptr;
    QPushButton *browse_button = nullptr;
    QComboBox *conflict_combo = nullptr;
    QComboBox *color_combo = nullptr;
    PillSwitch *split_switch = nullptr;
    QLineEdit *template_edit = nullptr;
    ElidedLabel *template_example = nullptr;

    // 格式参数卡
    QFrame *param_card = nullptr;
    QTabWidget *tabs = nullptr;
    QToolButton *add_format_button = nullptr;
    ElidedLabel *tabs_hint = nullptr;

    // 预设卡
    QFrame *preset_card = nullptr;
    QComboBox *preset_combo = nullptr;
    QPushButton *preset_save = nullptr;
    QPushButton *preset_saveas = nullptr;
    QPushButton *preset_delete = nullptr;

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
        v->setSpacing(theme::Metrics::gap); // mockup .scroll{gap:10px}
        scroll->setWidget(content);

        v->addWidget(build_mode_card(content));
        v->addWidget(build_format_card(content));
        v->addWidget(build_global_card(content));
        v->addWidget(build_param_card(content));
        v->addWidget(build_preset_card(content));
        v->addStretch(1);
    }

    QFrame *make_card(QWidget *parent, const QString &title, const QString &hint,
                      ElidedLabel **pill_out, ElidedLabel **hint_out) {
        auto *card = new QFrame(parent);
        card->setObjectName(QStringLiteral("pp-card"));
        auto *v = new QVBoxLayout(card);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        auto *head = new QWidget(card);
        auto *h = new QHBoxLayout(head);
        h->setContentsMargins(12, 9, 12, 7);
        h->setSpacing(8);
        auto *title_label = new QLabel(title, head);
        title_label->setObjectName(QStringLiteral("pp-card-title"));
        title_label->setFont(
            theme::font(theme::Typography::card_title_px, theme::Typography::card_title_weight));
        h->addWidget(title_label);
        if (pill_out != nullptr) {
            auto *pill = new ElidedLabel(head);
            pill->setProperty(theme::kPillProperty, true);
            pill->setFont(
                theme::font(theme::Typography::badge_px, theme::Typography::badge_weight));
            pill->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
            pill->setVisible(false);
            h->addWidget(pill);
            *pill_out = pill;
        }
        h->addStretch(1);
        if (hint_out != nullptr) {
            auto *hint_label = new ElidedLabel(head);
            hint_label->setObjectName(QStringLiteral("pp-card-hint"));
            hint_label->setFont(theme::font(theme::Typography::hint_px, 500));
            hint_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            hint_label->set_full_text(hint);
            h->addWidget(hint_label, 1);
            *hint_out = hint_label;
        }
        v->addWidget(head);
        return card;
    }

    // 模式卡：seg（转码 | 仅元数据）+ 互斥提示
    QWidget *build_mode_card(QWidget *parent) {
        auto *card = make_card(parent, PageOutput::tr("模式"), QString(), nullptr, nullptr);
        auto *v = qobject_cast<QVBoxLayout *>(card->layout());

        auto *seg_row = new QWidget(card);
        auto *seg_h = new QHBoxLayout(seg_row);
        seg_h->setContentsMargins(14, 0, 14, 8);
        seg_h->setSpacing(0);
        auto *seg = new QWidget(seg_row);
        seg->setObjectName(QStringLiteral("pp-output-seg"));
        auto *seg_layout = new QHBoxLayout(seg);
        seg_layout->setContentsMargins(2, 2, 2, 2);
        seg_layout->setSpacing(0);
        mode_convert = new QToolButton(seg);
        mode_convert->setObjectName(QStringLiteral("pp-mode-convert"));
        mode_convert->setText(PageOutput::tr("转码"));
        mode_meta = new QToolButton(seg);
        mode_meta->setObjectName(QStringLiteral("pp-mode-metadata"));
        mode_meta->setText(PageOutput::tr("仅元数据"));
        for (QToolButton *b : {mode_convert, mode_meta}) {
            b->setCheckable(true);
            b->setAutoRaise(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setProperty("ppSeg", QStringLiteral("seg"));
            b->setFont(theme::font(theme::Typography::body_px, 600));
            b->setMinimumHeight(24);
            seg_layout->addWidget(b);
        }
        mode_convert->setChecked(true);
        seg_h->addWidget(seg);
        seg_h->addStretch(1);
        v->addWidget(seg_row);

        mode_note = new ElidedLabel(card);
        mode_note->setObjectName(QStringLiteral("pp-format-note"));
        mode_note->setFont(theme::font(theme::Typography::hint_px));
        mode_note->setContentsMargins(14, 0, 14, 8);
        mode_note->set_full_text(
            PageOutput::tr("仅元数据：同容器零重编码；此时输出格式多选自动禁用（保持原格式）"));
        mode_note->setVisible(false);
        v->addWidget(mode_note);
        return card;
    }

    // 输出格式卡：8 磁贴复选 + 「已选 N」计数徽标
    QWidget *build_format_card(QWidget *parent) {
        format_card =
            make_card(parent, PageOutput::tr("输出格式"),
                      PageOutput::tr("可多选 · 每个输入输出 N 个文件"), &format_pill, &format_hint);
        auto *v = qobject_cast<QVBoxLayout *>(format_card->layout());
        auto *grid = new QGridLayout();
        grid->setContentsMargins(14, 2, 14, 12);
        grid->setHorizontalSpacing(7); // mockup .fmt-grid{gap:7px}
        grid->setVerticalSpacing(7);
        for (int i = 0; i < kFormatCount; ++i) {
            const pp::FormatDef *f = pp::find_format(kFormats[i].id);
            auto *tile = new FormatTile(format_card);
            tile->setObjectName(QStringLiteral("pp-format-") + QLatin1String(kFormats[i].id));
            tile->set_labels(PageOutput::tr(kFormats[i].label),
                             QStringLiteral(".") + QString::fromStdString(f ? f->ext : ""));
            tile->set_tokens(tokens);
            grid->addWidget(tile, i / kFormatColumns, i % kFormatColumns);
            tiles[i] = tile;
        }
        for (int c = 0; c < kFormatColumns; ++c)
            grid->setColumnStretch(c, 1);
        v->addLayout(grid);
        update_format_count();
        return format_card;
    }

    // 全局卡：一行一字段（70px 右对齐标签轴）
    QWidget *build_global_card(QWidget *parent) {
        global_card = make_card(parent, PageOutput::tr("全局"), QString(), nullptr, nullptr);
        auto *v = qobject_cast<QVBoxLayout *>(global_card->layout());
        auto *grid = new QGridLayout();
        grid->setContentsMargins(14, 2, 14, 12);
        grid->setHorizontalSpacing(theme::Metrics::form_col_gap);
        grid->setVerticalSpacing(0);
        int row = 0;
        const auto add_label = [&](const QString &text, int r) {
            auto *label = new QLabel(text, global_card);
            label->setObjectName(QStringLiteral("pp-global-label"));
            label->setFont(theme::font(theme::Typography::small_px));
            label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            label->setMinimumHeight(theme::Metrics::form_row_h);
            label->setMinimumWidth(theme::Metrics::form_label_w);
            label->setMaximumWidth(theme::Metrics::form_label_w);
            label->setProperty("ppField", QStringLiteral("label"));
            grid->addWidget(label, r, 0);
            return label;
        };

        // 输出根目录（输入框 + 浏览钮）
        add_label(PageOutput::tr("输出根目录"), row);
        auto *root_row = new QWidget(global_card);
        auto *root_h = new QHBoxLayout(root_row);
        root_h->setContentsMargins(0, 0, 0, 0);
        root_h->setSpacing(8);
        out_root_edit = new QLineEdit(root_row);
        out_root_edit->setObjectName(QStringLiteral("pp-out-root-edit"));
        out_root_edit->setMinimumHeight(theme::Metrics::input_h);
        out_root_edit->setProperty("ppField", QStringLiteral("input"));
        browse_button = new QPushButton(PageOutput::tr("浏览…"), root_row);
        browse_button->setObjectName(QStringLiteral("pp-browse-button"));
        browse_button->setMinimumHeight(theme::Metrics::input_h);
        browse_button->setProperty("ppField", QStringLiteral("button"));
        root_h->addWidget(out_root_edit, 1);
        root_h->addWidget(browse_button);
        grid->addWidget(root_row, row++, 1);

        // 同名冲突
        add_label(PageOutput::tr("同名冲突"), row);
        conflict_combo = new QComboBox(global_card);
        conflict_combo->setObjectName(QStringLiteral("pp-conflict-combo"));
        conflict_combo->addItem(PageOutput::tr("自动加序号"),
                                static_cast<int>(pp::ConflictPolicy::Rename));
        conflict_combo->addItem(PageOutput::tr("跳过"), static_cast<int>(pp::ConflictPolicy::Skip));
        conflict_combo->addItem(PageOutput::tr("覆盖"),
                                static_cast<int>(pp::ConflictPolicy::Overwrite));
        conflict_combo->setCurrentIndex(0);
        conflict_combo->setMinimumHeight(theme::Metrics::input_h);
        conflict_combo->setProperty("ppField", QStringLiteral("combo"));
        grid->addWidget(conflict_combo, row++, 1, Qt::AlignLeft);

        // 色彩目标
        add_label(PageOutput::tr("色彩目标"), row);
        color_combo = new QComboBox(global_card);
        color_combo->setObjectName(QStringLiteral("pp-color-combo"));
        color_combo->addItem(PageOutput::tr("保持原样"),
                             static_cast<int>(pp::ColorTarget::KeepOriginal));
        color_combo->addItem(PageOutput::tr("sRGB"), static_cast<int>(pp::ColorTarget::SRGB));
        color_combo->addItem(PageOutput::tr("Display P3"),
                             static_cast<int>(pp::ColorTarget::DisplayP3));
        color_combo->addItem(PageOutput::tr("Adobe RGB (1998)"),
                             static_cast<int>(pp::ColorTarget::AdobeRGB));
        color_combo->setCurrentIndex(0);
        color_combo->setMinimumHeight(theme::Metrics::input_h);
        color_combo->setProperty("ppField", QStringLiteral("combo"));
        grid->addWidget(color_combo, row++, 1, Qt::AlignLeft);

        // 按文件格式分文件夹（单行开关；不含解释文字 —— 共识 §4 修订 ②）
        add_label(QString(), row);
        auto *switch_row = new QWidget(global_card);
        auto *switch_h = new QHBoxLayout(switch_row);
        switch_h->setContentsMargins(0, 0, 0, 0);
        switch_h->setSpacing(10);
        split_switch = new PillSwitch(switch_row);
        split_switch->setObjectName(QStringLiteral("pp-split-switch"));
        split_switch->set_tokens(tokens);
        auto *switch_label = new QLabel(PageOutput::tr("按文件格式分文件夹"), switch_row);
        switch_label->setObjectName(QStringLiteral("pp-split-label"));
        switch_label->setFont(theme::font(theme::Typography::body_px));
        switch_h->addWidget(split_switch);
        switch_h->addWidget(switch_label);
        switch_h->addStretch(1);
        grid->addWidget(switch_row, row++, 1);

        // 路径模板（中性输入框 + 悬浮提示里的符号表/示例）
        add_label(PageOutput::tr("路径模板"), row);
        template_edit = new QLineEdit(global_card);
        template_edit->setObjectName(QStringLiteral("pp-template-edit"));
        template_edit->setMinimumHeight(theme::Metrics::input_h);
        template_edit->setProperty("ppField", QStringLiteral("mono"));
        template_edit->setToolTip(template_tooltip());
        template_edit->setText(QStringLiteral("$format/$dir/$file"));
        grid->addWidget(template_edit, row++, 1);

        // 单行示例路径（实时、超长省略、不折行）
        add_label(QString(), row);
        template_example = new ElidedLabel(global_card);
        template_example->setObjectName(QStringLiteral("pp-template-example"));
        template_example->setFont(theme::font(theme::Typography::hint_px, QFont::Normal, true));
        template_example->setProperty("ppField", QStringLiteral("hint"));
        template_example->setToolTip(template_tooltip());
        template_example->setMinimumHeight(theme::Metrics::form_row_h - 8);
        grid->addWidget(template_example, row++, 1);
        grid->setColumnStretch(1, 1);
        v->addLayout(grid);
        update_template_example();
        return global_card;
    }

    static QString template_tooltip() {
        return PageOutput::tr(
            "$format=格式目录 · $dir=镜像目录 · $file=文件名.扩展名 · $name=主名 · $ext=扩展名\n"
            "例：<根>/jpeg/2024/05/IMG_2731.jpg ＋ <根>/webp/2024/05/IMG_2731.webp\n"
            "段间以 / 分隔；未知 $ 符号、.. 与绝对路径会被拒绝（开始运行前阻断）");
    }

    // 格式参数卡：QTabWidget 逐已选格式（位深 + ParamForm）
    QWidget *build_param_card(QWidget *parent) {
        param_card = make_card(parent, PageOutput::tr("格式参数"),
                               PageOutput::tr("页签内含位深与编码参数"), nullptr, &tabs_hint);
        auto *v = qobject_cast<QVBoxLayout *>(param_card->layout());
        tabs = new QTabWidget(param_card);
        tabs->setObjectName(QStringLiteral("pp-format-tabs"));
        tabs->setDocumentMode(true);
        add_format_button = new QToolButton(tabs);
        add_format_button->setObjectName(QStringLiteral("pp-add-format"));
        add_format_button->setText(PageOutput::tr("＋ 添加格式…"));
        add_format_button->setAutoRaise(true);
        add_format_button->setCursor(Qt::PointingHandCursor);
        add_format_button->setFont(theme::font(theme::Typography::body_px, 500));
        tabs->setCornerWidget(add_format_button, Qt::TopRightCorner);
        v->addWidget(tabs);
        return param_card;
    }

    // 预设卡：下拉 + 保存 / 另存… / 删除
    QWidget *build_preset_card(QWidget *parent) {
        preset_card = make_card(parent, PageOutput::tr("预设"), QString(), nullptr, nullptr);
        auto *v = qobject_cast<QVBoxLayout *>(preset_card->layout());
        auto *row = new QWidget(preset_card);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(14, 2, 14, 12);
        h->setSpacing(8);
        preset_combo = new QComboBox(row);
        preset_combo->setObjectName(QStringLiteral("pp-preset-combo"));
        preset_combo->setMinimumHeight(theme::Metrics::input_h);
        preset_combo->setProperty("ppField", QStringLiteral("combo"));
        preset_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        preset_save = new QPushButton(PageOutput::tr("保存"), row);
        preset_save->setObjectName(QStringLiteral("pp-preset-save"));
        preset_saveas = new QPushButton(PageOutput::tr("另存…"), row);
        preset_saveas->setObjectName(QStringLiteral("pp-preset-saveas"));
        preset_delete = new QPushButton(PageOutput::tr("删除"), row);
        preset_delete->setObjectName(QStringLiteral("pp-preset-delete"));
        for (QPushButton *b : {preset_save, preset_saveas, preset_delete}) {
            b->setMinimumHeight(theme::Metrics::input_h);
            b->setProperty("ppField", QStringLiteral("button"));
            b->setFont(theme::font(theme::Typography::body_px, 600));
        }
        h->addWidget(preset_combo, 1);
        h->addWidget(preset_save);
        h->addWidget(preset_saveas);
        h->addWidget(preset_delete);
        v->addWidget(row);
        return preset_card;
    }

    // ------------------------------------------------------------ 信号接线

    void wire() {
        for (int i = 0; i < kFormatCount; ++i) {
            QObject::connect(tiles[i], &QAbstractButton::toggled, q, [this, i](bool on) {
                toggle_format(kFormats[i].id, on, /*user=*/true);
            });
        }
        QObject::connect(mode_convert, &QToolButton::clicked, q,
                         [this] { q->set_metadata_only(false); });
        QObject::connect(mode_meta, &QToolButton::clicked, q,
                         [this] { q->set_metadata_only(true); });

        QObject::connect(browse_button, &QPushButton::clicked, q, [this] {
            const QString start = out_root_edit->text().trimmed();
            const QString dir =
                QFileDialog::getExistingDirectory(q, PageOutput::tr("选择输出根目录"), start);
            if (!dir.isEmpty())
                q->set_out_root(dir);
        });
        QObject::connect(out_root_edit, &QLineEdit::textChanged, q, [this](const QString &) {
            update_template_example();
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
        QObject::connect(split_switch, &QAbstractButton::toggled, q,
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
            emit q->format_changed(QString::fromStdString(id));
            emit q->config_changed();
        });
        QObject::connect(add_format_button, &QToolButton::clicked, q, [this] {
            // 「＋ 添加格式…」= 打开尚未选中的首个格式（磁贴顺序）；全部已选 → 提示
            for (const FormatEntry &e : kFormats) {
                if (!is_selected(e.id)) {
                    toggle_format(e.id, true, /*user=*/true);
                    return;
                }
            }
            tabs_hint->set_full_text(PageOutput::tr("8 个格式已全部选中"));
        });
        QObject::connect(preset_combo, &QComboBox::activated, q, [this](int index) {
            if (index < 0)
                return;
            const QString path = preset_combo->itemData(index, Qt::UserRole).toString();
            if (!path.isEmpty())
                emit q->preset_load_requested(path);
        });
        QObject::connect(preset_save, &QPushButton::clicked, q, [this] {
            const QString name =
                preset_combo->currentIndex() >= 0 ? preset_combo->currentText() : QString();
            if (name.isEmpty()) {
                emit q->preset_saveas_requested();
                return;
            }
            emit q->preset_save_requested(name);
        });
        QObject::connect(preset_saveas, &QPushButton::clicked, q,
                         [this] { emit q->preset_saveas_requested(); });
        QObject::connect(preset_delete, &QPushButton::clicked, q, [this] {
            const QString path =
                preset_combo->currentIndex() >= 0
                    ? preset_combo->itemData(preset_combo->currentIndex(), Qt::UserRole).toString()
                    : QString();
            if (!path.isEmpty())
                emit q->preset_delete_requested(path);
        });
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
        v->setContentsMargins(14, 8, 14, 10);
        v->setSpacing(6);

        // 位深行（70px 轴；格式能力约束：如 JPEG 锁 8 位）
        auto *bd_row = new QWidget(st.page);
        auto *bd_h = new QHBoxLayout(bd_row);
        bd_h->setContentsMargins(0, 0, 0, 0);
        bd_h->setSpacing(theme::Metrics::form_col_gap);
        auto *bd_label = new QLabel(PageOutput::tr("位深"), bd_row);
        bd_label->setObjectName(QStringLiteral("pp-bitdepth-label-") + QString::fromStdString(id));
        bd_label->setFont(theme::font(theme::Typography::small_px));
        bd_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bd_label->setMinimumWidth(theme::Metrics::form_label_w);
        bd_label->setMaximumWidth(theme::Metrics::form_label_w);
        st.bitdepth = new QComboBox(bd_row);
        st.bitdepth->setObjectName(QStringLiteral("pp-bitdepth-combo"));
        st.bitdepth->setMinimumHeight(theme::Metrics::input_h);
        st.bitdepth->setProperty("ppField", QStringLiteral("combo"));
        st.bitdepth_note = new ElidedLabel(bd_row);
        st.bitdepth_note->setObjectName(QStringLiteral("pp-bitdepth-note-") +
                                        QString::fromStdString(id));
        st.bitdepth_note->setFont(theme::font(theme::Typography::hint_px));
        st.bitdepth_note->setProperty("ppField", QStringLiteral("hint"));
        bd_h->addWidget(bd_label);
        bd_h->addWidget(st.bitdepth);
        bd_h->addWidget(st.bitdepth_note, 1);
        bd_h->addStretch(0);
        v->addWidget(bd_row);

        // ParamForm（原样嵌入；无损复选框在其顶部选择器行）
        st.form = new ParamForm(*f, effective_backends(*f), st.page);
        st.form->setObjectName(QStringLiteral("pp-param-form-") + QString::fromStdString(id));
        v->addWidget(st.form);

        // avif 10bit+alpha 预选提示（原 pp-alpha-hint 语义，位置随格式页签）
        st.alpha_hint = new ElidedLabel(st.page);
        st.alpha_hint->setObjectName(QStringLiteral("pp-alpha-hint"));
        st.alpha_hint->setFont(theme::font(theme::Typography::hint_px));
        st.alpha_hint->set_full_text(
            PageOutput::tr("10 位 + 含 alpha：已选择 libaom 后端（SVT-AV1 不支持该组合）"));
        st.alpha_hint->setVisible(false);
        v->addWidget(st.alpha_hint);

        QObject::connect(st.form, &ParamForm::selection_changed, q,
                         [this, id](const FormSelection &) { on_param_selection_changed(id); });
        QObject::connect(st.form, &ParamForm::changed, q, [this, id] {
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

    QString format_tab_label(const std::string &id) const {
        const pp::FormatDef *f = pp::find_format(id);
        const QString name =
            f != nullptr ? QString::fromStdString(f->label) : QString::fromStdString(id);
        const int changed = changed_param_count(id);
        return changed > 0 ? name + QStringLiteral("  · ") + QString::number(changed) : name;
    }

    // 页签计数徽标：值 ≠ 表内默认 的参数项数
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
        // 否则 t 在语句结束即悬垂（ASAN: heap-use-after-free @ page_output.cpp:1211）。
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
        format_pill->setVisible(true);
        format_pill->set_full_text(PageOutput::tr("已选 %1").arg(selected.size()));
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
    }

    void on_bitdepth_changed(const std::string &id) {
        evaluate_avif_alpha_preselect(id);
        update_alpha_hint(id);
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
        // 格式能力约束提示（如 JPEG 仅 8 位）
        if (st->bitdepth_note != nullptr) {
            const bool locked = allowed.size() == 1;
            st->bitdepth_note->set_full_text(locked ? PageOutput::tr("%1 仅支持 %2 位输出")
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
        refresh_tab_labels();
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
        format_card->setEnabled(enabled);
        param_card->setEnabled(enabled);
        // v0.2 语义（M1b §2.9）：仅元数据不做色彩转换 → 色彩目标一并禁用
        color_combo->setEnabled(enabled);
        mode_note->setVisible(metadata_only);
        if (mode_convert != nullptr) {
            const QSignalBlocker b1(mode_convert);
            const QSignalBlocker b2(mode_meta);
            mode_convert->setChecked(!metadata_only);
            mode_meta->setChecked(metadata_only);
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

    void update_template_example() {
        if (template_example == nullptr || template_edit == nullptr)
            return;
        const QString tmpl = template_edit->text();
        std::string err;
        if (!pp::validate_output_template(tmpl.toStdString(), &err)) {
            template_example->set_full_text(
                PageOutput::tr("模板无效：%1").arg(QString::fromStdString(err)));
            template_example->setProperty("ppInvalid", true);
            return;
        }
        template_example->setProperty("ppInvalid", false);
        const QString root = out_root_edit->text().trimmed();
        const QString prefix = root.isEmpty() ? QStringLiteral("<根>") : root;
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
            samples << prefix + QStringLiteral("/") + rel;
            if (samples.size() == 2)
                break;
        }
        if (selected.size() > 2)
            samples << QStringLiteral("…");
        template_example->set_full_text(samples.join(QStringLiteral("  + ")));
        template_example->setToolTip(template_tooltip() + QLatin1Char('\n') +
                                     template_example->full_text());
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
}

// ---------------------------------------------------------------- 追加面（M4-T13）

void PageOutput::set_tokens(const theme::Tokens &tokens) {
    Impl *impl_ = impl_of(this);
    impl_->tokens = tokens;
    for (FormatTile *tile : impl_->tiles) {
        if (tile != nullptr)
            tile->set_tokens(tokens);
    }
    if (impl_->split_switch != nullptr)
        impl_->split_switch->set_tokens(tokens);
    // 页面 QSS（卡片/徽标/输入类控件/页签；与 PageMeta 同一注入口径）
    impl_->q->setStyleSheet(page_qss(tokens));
    // 浅色卡阴影（§9.2 0 1px 4px rgba(16,24,40,.06)；深色无阴影）
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
    if (!current.isEmpty()) {
        for (int i = 0; i < impl_->preset_combo->count(); ++i) {
            if (impl_->preset_combo->itemData(i, Qt::UserRole).toString() == current) {
                impl_->preset_combo->setCurrentIndex(i);
                break;
            }
        }
    }
    const bool has = impl_->preset_combo->count() > 0;
    impl_->preset_save->setEnabled(has);
    impl_->preset_delete->setEnabled(has);
}

void PageOutput::set_current_preset(const QString &path) {
    Impl *impl_ = impl_of(this);
    if (impl_->preset_combo == nullptr)
        return;
    const QSignalBlocker blocker(impl_->preset_combo);
    for (int i = 0; i < impl_->preset_combo->count(); ++i) {
        if (impl_->preset_combo->itemData(i, Qt::UserRole).toString() == path) {
            impl_->preset_combo->setCurrentIndex(i);
            return;
        }
    }
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
