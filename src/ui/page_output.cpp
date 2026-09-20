// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — output page (M1b-U5)
//
// Frozen contract: docs/m1b-tasks.md §2.9 (header, byte-exact) + §2.9 behavior
// items 1..8 + §3.2 wording table. Landing notes:
//  * bit-depth options = FormatDef.bitdepths ∩ probe_bitdepth_support() for
//    heif/avif; the probe result is cached process-wide per (format, backend)
//    (§2.9.2 / §2.9.7).
//  * introspect_backends() enumerates libaom before svt-av1 (measured); the
//    introspected lists are re-ordered to the static table order so avif's
//    default backend stays svt-av1 (engine default: registry order / static table).
//  * ParamForm is embedded verbatim (U3 landing canon): set_selection/set_values
//    are signal-free, only user actions emit selection_changed/changed.
//  * run path / rules assembly is out of scope here (§2.9 RunConfig skeleton).
//  * §9.1 [高]：avif+alpha 预选改为**触发条件求值**（进入 avif / alpha / 位深 / 后端变化时求值），
//    用户手动改后端 → user_backend_override（set_batch_has_alpha 清除），不再抢占用户选择。

#include "ui/page_output.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <QButtonGroup>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "codecs/encoders.h"
#include "core/params.h"
#include "ui/paramform.h"

namespace pp::ui {
namespace {

// ---------------------------------------------------------------- 格式按钮表

struct FormatEntry {
    const char* id;
    const char* label;
};

// §2.9.1：顺序与文案冻结（4×2，行优先）
const FormatEntry kFormats[] = {
    {"jpeg", "JPEG"}, {"jxl", "JPEG XL"}, {"png", "PNG"},   {"tiff", "TIFF"},
    {"webp", "WebP"}, {"heif", "HEIF"},   {"avif", "AVIF"}, {"bmp", "BMP"},
};
constexpr int kFormatCount = static_cast<int>(sizeof(kFormats) / sizeof(kFormats[0]));
constexpr int kFormatColumns = 4;

// §2.9.2：逐格式高质量档默认位深
int default_bitdepth(const std::string& id) {
    if (id == "jpeg") return 8;
    if (id == "jxl") return 16;
    if (id == "png") return 16;
    if (id == "tiff") return 16;
    if (id == "webp") return 8;
    if (id == "bmp") return 24;
    if (id == "heif") return 10;
    if (id == "avif") return 10;
    return 8;
}

bool contains(const std::vector<int>& v, int x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

std::vector<int> parse_csv_ints(const std::string& csv) {
    std::vector<int> out;
    std::size_t pos = 0;
    while (pos <= csv.size()) {
        const std::size_t comma = csv.find(',', pos);
        const std::size_t len = (comma == std::string::npos) ? std::string::npos : comma - pos;
        const std::string tok = csv.substr(pos, len);
        if (!tok.empty()) {
            int value = 0;
            const char* first = tok.data();
            const char* last = tok.data() + tok.size();
            const std::from_chars_result res = std::from_chars(first, last, value);
            if (res.ec == std::errc() && res.ptr == last) out.push_back(value);
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

// §2.9.2：可选位深 = 静态允许集 ∩ 运行期探测（非 libheif 格式即静态集）。
// §2.9.7：探测结果按 (format, backend) 进程内缓存一次（GUI 线程独占，无需锁）。
std::vector<int> allowed_bitdepths(const pp::FormatDef& f, const std::string& backend) {
    if (f.id != "heif" && f.id != "avif") return f.bitdepths;
    static std::map<std::pair<std::string, std::string>, std::vector<int>> cache;
    const auto key = std::make_pair(f.id, backend);
    auto it = cache.find(key);
    if (it == cache.end()) {
        const std::vector<int> probed = parse_csv_ints(pp::probe_bitdepth_support(f.id, backend));
        std::vector<int> allowed;
        for (const int d : f.bitdepths)
            if (contains(probed, d)) allowed.push_back(d);
        it = cache.emplace(key, std::move(allowed)).first;
    }
    // 探测无可用结果（异常环境）→ 回退静态集，UI 至少可用（引擎侧仍会明确报错）
    return it->second.empty() ? f.bitdepths : it->second;
}

// heif/avif：静态表后端带 runtime_introspected 标志（FormatDef 本身无该字段）
bool runtime_introspected_format(const pp::FormatDef& f) {
    return std::any_of(f.backends.begin(), f.backends.end(),
                       [](const pp::BackendDef& b) { return b.runtime_introspected; });
}

// §2.9.7：heif/avif 内省在首次选中时执行并缓存；顺序归一为静态表顺序，
// 使默认后端与引擎默认（make_encoder(fmt,"") → svt-av1）一致。
std::vector<pp::BackendDef> effective_backends(const pp::FormatDef& f) {
    if (!runtime_introspected_format(f)) return f.backends;
    static std::map<std::string, std::vector<pp::BackendDef>> cache;
    auto it = cache.find(f.id);
    if (it == cache.end()) {
        const std::vector<pp::BackendDef> live = pp::introspect_backends(f.id);
        std::vector<pp::BackendDef> ordered;
        for (const pp::BackendDef& sb : f.backends)
            for (const pp::BackendDef& lb : live)
                if (lb.id == sb.id) ordered.push_back(lb);
        for (const pp::BackendDef& lb : live)
            if (std::none_of(ordered.begin(), ordered.end(),
                             [&](const pp::BackendDef& b) { return b.id == lb.id; }))
                ordered.push_back(lb);
        it = cache.emplace(f.id, std::move(ordered)).first;
    }
    return it->second.empty() ? f.backends : it->second;
}

bool has_backend(const std::vector<pp::BackendDef>& list, const std::string& id) {
    return std::any_of(list.begin(), list.end(),
                       [&](const pp::BackendDef& b) { return b.id == id; });
}

// 灰色说明 / 黄色提示（QPalette，与 §1.10「Qt 内建样式」一致；同批 page_meta 口径）
QLabel* grey_note(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, QColor(0x80, 0x80, 0x80));
    label->setPalette(pal);
    return label;
}

QLabel* yellow_hint(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, QColor(0xcc, 0x88, 0x00));
    label->setPalette(pal);
    return label;
}

// §3.2 组合框映射（顺序即冻结文案顺序）
pp::ConflictPolicy conflict_from_index(int idx) {
    switch (idx) {
        case 1: return pp::ConflictPolicy::Skip;
        case 2: return pp::ConflictPolicy::Overwrite;
        default: return pp::ConflictPolicy::Rename;   // 默认 自动加序号
    }
}

int conflict_to_index(pp::ConflictPolicy p) {
    switch (p) {
        case pp::ConflictPolicy::Skip: return 1;
        case pp::ConflictPolicy::Overwrite: return 2;
        case pp::ConflictPolicy::Rename: default: return 0;
    }
}

pp::ColorTarget color_from_index(int idx) {
    switch (idx) {
        case 1: return pp::ColorTarget::SRGB;
        case 2: return pp::ColorTarget::DisplayP3;
        case 3: return pp::ColorTarget::AdobeRGB;
        default: return pp::ColorTarget::KeepOriginal;   // 默认 保持原样
    }
}

int color_to_index(pp::ColorTarget t) {
    switch (t) {
        case pp::ColorTarget::SRGB: return 1;
        case pp::ColorTarget::DisplayP3: return 2;
        case pp::ColorTarget::AdobeRGB: return 3;
        case pp::ColorTarget::KeepOriginal: default: return 0;
    }
}

// ---------------------------------------------------------------- 页面状态
// §2.9 冻结头未声明任何成员（逐字节不动，§1.3）→ 状态放文件内注册表，
// 与同批 page_run/page_meta 的落地口径一致。

struct Impl {
    PageOutput* q = nullptr;

    QString current_format_id = QStringLiteral("jxl");
    bool metadata_only = false;
    bool batch_has_alpha = false;
    // §9.1 [高]：avif+alpha 预选状态
    bool user_backend_override = false;   // 用户手动改过后端（set_batch_has_alpha 时清除）
    std::string last_backend;             // 最近一次已知后端（区分用户手动改动）

    // 模式
    QRadioButton* mode_convert = nullptr;
    QRadioButton* mode_meta = nullptr;

    // 输出格式
    QGroupBox* format_group = nullptr;
    QButtonGroup* format_buttons = nullptr;
    QToolButton* format_button[kFormatCount] = {};
    QLabel* format_note = nullptr;

    // 全局
    QGroupBox* global_group = nullptr;
    QLineEdit* out_root_edit = nullptr;
    QPushButton* browse_button = nullptr;
    QComboBox* conflict_combo = nullptr;
    QComboBox* color_combo = nullptr;
    QComboBox* bitdepth_combo = nullptr;
    QLabel* global_note = nullptr;

    // 格式参数
    QGroupBox* param_group = nullptr;
    QVBoxLayout* param_layout = nullptr;
    ParamForm* param_form = nullptr;
    QLabel* alpha_hint = nullptr;
    QLabel* param_note = nullptr;

    // 其它
    QPushButton* presets_button = nullptr;
    QPushButton* settings_button = nullptr;

    // ------------------------------------------------------------ 查询

    const pp::FormatDef* format() const {
        return pp::find_format(current_format_id.toStdString());
    }

    std::string backend_id() const {
        return param_form ? param_form->selection().backend : std::string();
    }

    void remember_backend() { last_backend = backend_id(); }

    int bitdepth() const {
        return bitdepth_combo ? bitdepth_combo->currentData().toInt() : 0;
    }

    pp::ColorTarget color_target() const {
        return color_from_index(color_combo ? color_combo->currentIndex() : 0);
    }

    pp::ConflictPolicy conflict() const {
        return conflict_from_index(conflict_combo ? conflict_combo->currentIndex() : 0);
    }

    // ------------------------------------------------------------ 界面构建

    void build_ui() {
        auto* outer = new QVBoxLayout(q);
        outer->setContentsMargins(0, 0, 0, 0);

        auto* scroll = new QScrollArea(q);
        scroll->setObjectName(QStringLiteral("pp-output-scroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        outer->addWidget(scroll);

        auto* content = new QWidget(scroll);
        auto* v = new QVBoxLayout(content);
        v->setContentsMargins(8, 8, 8, 8);
        v->setSpacing(8);
        scroll->setWidget(content);

        build_mode_group(content, v);
        build_format_group(content, v);
        build_global_group(content, v);
        build_param_group(content, v);
        build_button_row(content, v);
        v->addStretch(1);
    }

    void build_mode_group(QWidget* parent, QVBoxLayout* v) {
        auto* group = new QGroupBox(PageOutput::tr("模式"), parent);
        group->setObjectName(QStringLiteral("pp-mode-group"));
        auto* row = new QHBoxLayout(group);
        mode_convert = new QRadioButton(PageOutput::tr("转码"), group);
        mode_convert->setObjectName(QStringLiteral("pp-mode-convert"));
        mode_meta = new QRadioButton(PageOutput::tr("仅元数据"), group);
        mode_meta->setObjectName(QStringLiteral("pp-mode-metadata"));
        mode_convert->setChecked(true);   // 默认转码
        row->addWidget(mode_convert);
        row->addWidget(mode_meta);
        row->addStretch(1);
        v->addWidget(group);
    }

    void build_format_group(QWidget* parent, QVBoxLayout* v) {
        format_group = new QGroupBox(PageOutput::tr("输出格式"), parent);
        format_group->setObjectName(QStringLiteral("pp-format-group"));
        // 与 模式/全局/格式参数 同构：QGroupBox + 内嵌内容，按钮与组框留白一致
        auto* box = new QVBoxLayout(format_group);
        box->setContentsMargins(9, 9, 9, 9);
        box->setSpacing(6);
        auto* grid = new QGridLayout();
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(6);
        format_buttons = new QButtonGroup(q);
        format_buttons->setExclusive(true);
        for (int i = 0; i < kFormatCount; ++i) {
            auto* button = new QToolButton(format_group);
            button->setObjectName(QStringLiteral("pp-format-") + QLatin1String(kFormats[i].id));
            button->setText(PageOutput::tr(kFormats[i].label));
            button->setCheckable(true);
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            button->setMinimumHeight(24);
            format_buttons->addButton(button, i);
            grid->addWidget(button, i / kFormatColumns, i % kFormatColumns);
            format_button[i] = button;
        }
        box->addLayout(grid);
        format_note = grey_note(metadata_note_text(), format_group);
        format_note->setObjectName(QStringLiteral("pp-format-note"));
        format_note->setWordWrap(true);
        format_note->setVisible(false);
        box->addWidget(format_note);
        v->addWidget(format_group);
    }

    void build_global_group(QWidget* parent, QVBoxLayout* v) {
        global_group = new QGroupBox(PageOutput::tr("全局"), parent);
        global_group->setObjectName(QStringLiteral("pp-global-group"));
        auto* form = new QFormLayout(global_group);

        auto* root_row = new QWidget(global_group);
        auto* root_h = new QHBoxLayout(root_row);
        root_h->setContentsMargins(0, 0, 0, 0);
        out_root_edit = new QLineEdit(root_row);
        out_root_edit->setObjectName(QStringLiteral("pp-out-root-edit"));
        browse_button = new QPushButton(PageOutput::tr("浏览…"), root_row);
        browse_button->setObjectName(QStringLiteral("pp-browse-button"));
        root_h->addWidget(out_root_edit, 1);
        root_h->addWidget(browse_button);
        form->addRow(PageOutput::tr("输出根目录"), root_row);

        conflict_combo = new QComboBox(global_group);
        conflict_combo->setObjectName(QStringLiteral("pp-conflict-combo"));
        conflict_combo->addItem(PageOutput::tr("自动加序号"),
                                static_cast<int>(pp::ConflictPolicy::Rename));
        conflict_combo->addItem(PageOutput::tr("跳过"), static_cast<int>(pp::ConflictPolicy::Skip));
        conflict_combo->addItem(PageOutput::tr("覆盖"),
                                static_cast<int>(pp::ConflictPolicy::Overwrite));
        conflict_combo->setCurrentIndex(0);
        form->addRow(PageOutput::tr("同名冲突"), conflict_combo);

        color_combo = new QComboBox(global_group);
        color_combo->setObjectName(QStringLiteral("pp-color-combo"));
        color_combo->addItem(PageOutput::tr("保持原样"),
                             static_cast<int>(pp::ColorTarget::KeepOriginal));
        color_combo->addItem(PageOutput::tr("sRGB"), static_cast<int>(pp::ColorTarget::SRGB));
        color_combo->addItem(PageOutput::tr("Display P3"),
                             static_cast<int>(pp::ColorTarget::DisplayP3));
        color_combo->addItem(PageOutput::tr("Adobe RGB (1998)"),
                             static_cast<int>(pp::ColorTarget::AdobeRGB));
        color_combo->setCurrentIndex(0);
        form->addRow(PageOutput::tr("色彩目标"), color_combo);

        bitdepth_combo = new QComboBox(global_group);
        bitdepth_combo->setObjectName(QStringLiteral("pp-bitdepth-combo"));
        form->addRow(PageOutput::tr("输出位深"), bitdepth_combo);

        global_note = grey_note(metadata_note_text(), global_group);
        global_note->setObjectName(QStringLiteral("pp-global-note"));
        global_note->setWordWrap(true);
        global_note->setVisible(false);
        form->addRow(global_note);

        v->addWidget(global_group);
    }

    void build_param_group(QWidget* parent, QVBoxLayout* v) {
        param_group = new QGroupBox(PageOutput::tr("格式参数"), parent);
        param_group->setObjectName(QStringLiteral("pp-param-group"));
        param_layout = new QVBoxLayout(param_group);

        alpha_hint = yellow_hint(
            PageOutput::tr("10 位 + 含 alpha：已选择 libaom 后端（SVT-AV1 不支持该组合）"),
            param_group);
        alpha_hint->setObjectName(QStringLiteral("pp-alpha-hint"));
        alpha_hint->setWordWrap(true);
        alpha_hint->setVisible(false);
        param_layout->addWidget(alpha_hint);
        // index 1 预留给 ParamForm（rebuild_param_form 用 insertWidget(1, ...)）

        param_note = grey_note(metadata_note_text(), param_group);
        param_note->setObjectName(QStringLiteral("pp-param-note"));
        param_note->setWordWrap(true);
        param_note->setVisible(false);
        param_layout->addWidget(param_note);

        v->addWidget(param_group);
    }

    void build_button_row(QWidget* parent, QVBoxLayout* v) {
        auto* row = new QHBoxLayout();
        presets_button = new QPushButton(PageOutput::tr("预设管理…"), parent);
        presets_button->setObjectName(QStringLiteral("pp-presets-button"));
        settings_button = new QPushButton(PageOutput::tr("设置…"), parent);
        settings_button->setObjectName(QStringLiteral("pp-settings-button"));
        row->addWidget(presets_button);
        row->addWidget(settings_button);
        row->addStretch(1);
        v->addLayout(row);
    }

    static QString metadata_note_text() {
        return PageOutput::tr("仅元数据：输出保持源格式（JPEG / PNG / TIFF / WebP）；其余参数不适用");
    }

    // ------------------------------------------------------------ 信号接线

    void wire() {
        QObject::connect(format_buttons, &QButtonGroup::idClicked, q, [this](int id) {
            if (id < 0 || id >= kFormatCount) return;
            const std::string fid = kFormats[id].id;
            if (fid == current_format_id.toStdString()) return;
            apply_format(fid, true);
        });
        QObject::connect(browse_button, &QPushButton::clicked, q, [this] {
            const QString start = out_root_edit->text().trimmed();
            const QString dir = QFileDialog::getExistingDirectory(
                q, PageOutput::tr("选择输出根目录"), start);
            if (!dir.isEmpty()) q->set_out_root(dir);
        });
        QObject::connect(out_root_edit, &QLineEdit::textChanged, q,
                         [this](const QString&) { emit q->config_changed(); });
        QObject::connect(conflict_combo, &QComboBox::currentIndexChanged, q,
                         [this](int) { emit q->config_changed(); });
        QObject::connect(color_combo, &QComboBox::currentIndexChanged, q,
                         [this](int) { emit q->config_changed(); });
        QObject::connect(bitdepth_combo, &QComboBox::currentIndexChanged, q, [this](int) {
            evaluate_avif_alpha_preselect();   // §9.1：位深变化使条件成立 → 求值
            update_alpha_hint();
            emit q->config_changed();
        });
        QObject::connect(mode_meta, &QRadioButton::toggled, q, [this](bool on) {
            if (on) q->set_metadata_only(true);
        });
        QObject::connect(mode_convert, &QRadioButton::toggled, q, [this](bool on) {
            if (on) q->set_metadata_only(false);
        });
        QObject::connect(presets_button, &QPushButton::clicked, q,
                         [this] { emit q->manage_presets_requested(); });
        QObject::connect(settings_button, &QPushButton::clicked, q,
                         [this] { emit q->open_settings_requested(); });
    }

    // ------------------------------------------------------------ 格式 / 参数表单

    void sync_format_buttons() {
        const QSignalBlocker blocker(format_buttons);
        for (int i = 0; i < kFormatCount; ++i) {
            const bool on = (QLatin1String(kFormats[i].id) == current_format_id);
            format_button[i]->setChecked(on);
            // §9.1 [低]：选中态加强（边框 + 加粗 + 底色）；仅本控件样式，非主题文件
            // M2-T7 #28a：未选中态加浅边框（可点性提示）。R1 用的 palette(mid) 在默认
            // 主题下实测 #B8B8B8，与按钮底色 #EFEFEF 对比偏弱（Δ55）；这里取
            // palette(dark)（同一族、随主题走、实测 #9F9F9F，Δ80）作"浅边框"，仍是浅灰，
            // 选中态字符串逐字节不变。
            format_button[i]->setStyleSheet(
                on ? QStringLiteral("QToolButton { border: 2px solid palette(highlight);"
                                    " border-radius: 3px; padding: 2px 6px; font-weight: bold;"
                                    " background: palette(alternate-base); }")
                   : QStringLiteral("QToolButton { border: 1px solid palette(dark);"
                                    " border-radius: 3px; padding: 3px 7px;"
                                    " background: palette(button); }"));
        }
    }

    void rebuild_param_form() {
        const pp::FormatDef* f = format();
        if (!f || !param_layout) return;
        if (param_form) {
            // 立即脱离控件树：延迟析构的旧表单在事件循环跑起来前仍是 findChild 的
            // 第一个命中；setParent(nullptr) 同时保证不在其信号栈内自我析构。
            ParamForm* old = param_form;
            param_form = nullptr;
            param_layout->removeWidget(old);
            old->hide();
            old->setParent(nullptr);
            old->deleteLater();
        }
        param_form = new ParamForm(*f, effective_backends(*f), param_group);
        param_form->setObjectName(QStringLiteral("pp-param-form"));
        param_layout->insertWidget(1, param_form);
        QObject::connect(param_form, &ParamForm::selection_changed, q,
                         [this](const FormSelection&) { on_param_selection_changed(); });
        QObject::connect(param_form, &ParamForm::changed, q,
                         [this] { emit q->config_changed(); });
        param_form->setEnabled(!metadata_only);   // 组已置灰；此处保证重建后状态一致
        remember_backend();
    }

    // 用户改后端/技术/无损（avif 后端变化 → 位深集合重算）；config_changed 由紧随
    // 其后的 ParamForm::changed 发出（§2.6 条 7 顺序保证）。
    void on_param_selection_changed() {
        const std::string now = backend_id();
        if (now != last_backend) {
            user_backend_override = true;   // §9.1：用户手动改后端 → 之后不再自动抢占
            last_backend = now;
        }
        set_bitdepth_options(-1, /*keep_if_valid=*/true);
        evaluate_avif_alpha_preselect();    // 后端/位深变化可能使预选条件成立
        update_alpha_hint();
    }

    void apply_format(const std::string& id, bool emit_signals) {
        const pp::FormatDef* f = pp::find_format(id);
        if (!f) return;   // 非法值忽略
        const bool changed = (id != current_format_id.toStdString());
        current_format_id = QString::fromStdString(id);
        sync_format_buttons();
        rebuild_param_form();
        set_bitdepth_options(-1, /*keep_if_valid=*/false);   // 逐格式默认值
        remember_backend();
        evaluate_avif_alpha_preselect();   // §9.1：进入 avif 即求值
        update_alpha_hint();
        if (emit_signals && changed) {
            emit q->format_changed(current_format_id);   // 先 format_changed
            emit q->config_changed();                    // 再 config_changed
        }
    }

    // ------------------------------------------------------------ 位深

    void repopulate_bitdepths(const std::vector<int>& allowed, int selected) {
        const QSignalBlocker blocker(bitdepth_combo);
        bitdepth_combo->clear();
        int index = -1;
        for (std::size_t i = 0; i < allowed.size(); ++i) {
            bitdepth_combo->addItem(PageOutput::tr("%1 位").arg(allowed[i]), allowed[i]);
            if (allowed[i] == selected) index = static_cast<int>(i);
        }
        if (index >= 0) bitdepth_combo->setCurrentIndex(index);
    }

    // forced > 0：apply_preset 指定值（不在交集则退回默认规则）；
    // keep_if_valid：后端切换时当前位深仍有效则保留。返回是否发生变化。
    bool set_bitdepth_options(int forced, bool keep_if_valid) {
        const pp::FormatDef* f = format();
        if (!f) return false;
        const std::vector<int> allowed = allowed_bitdepths(*f, backend_id());
        if (allowed.empty()) return false;
        const int current = bitdepth();
        int resolved = 0;
        if (forced > 0 && contains(allowed, forced)) {
            resolved = forced;
        } else if (keep_if_valid && contains(allowed, current)) {
            resolved = current;
        } else {
            resolved = default_bitdepth(f->id);
            if (!contains(allowed, resolved))
                resolved = contains(allowed, 8) ? 8 : allowed.front();   // §2.9.2
        }
        repopulate_bitdepths(allowed, resolved);
        return resolved != current;
    }

    // ------------------------------------------------------------ avif + alpha / 仅元数据

    void update_alpha_hint() {
        const bool show = !metadata_only && batch_has_alpha &&
                          current_format_id == QLatin1String("avif") && bitdepth() == 10;
        alpha_hint->setVisible(show);
    }

    // §9.1 [高]：**触发条件求值**（不再依赖 set_batch_has_alpha 单点触发）。
    // 条件：avif ∧ batch_has_alpha ∧ 位深==10 ∧ 后端==svt-av1 ∧ 用户未手动改过后端。
    // 调用点：进入 avif、后端/位深变化、alpha 变化、apply_preset 落值后。
    bool evaluate_avif_alpha_preselect() {
        if (!param_form || user_backend_override) return false;
        if (!batch_has_alpha || current_format_id != QLatin1String("avif")) return false;
        if (bitdepth() != 10) return false;
        const FormSelection sel = param_form->selection();
        if (sel.backend != "svt-av1") return false;
        const pp::FormatDef* f = format();
        if (!f || !has_backend(effective_backends(*f), "libaom")) return false;
        FormSelection next = sel;
        next.backend = "libaom";
        param_form->set_selection(next);   // 程序化：不发信号（U3 落地口径）
        remember_backend();
        set_bitdepth_options(-1, /*keep_if_valid=*/true);
        return true;
    }

    void update_metadata_only_state() {
        format_group->setEnabled(!metadata_only);
        color_combo->setEnabled(!metadata_only);
        bitdepth_combo->setEnabled(!metadata_only);
        param_group->setEnabled(!metadata_only);
        format_note->setVisible(metadata_only);
        global_note->setVisible(metadata_only);
        param_note->setVisible(metadata_only);
        update_alpha_hint();
    }

    // ------------------------------------------------------------ 程序化设值（无信号）

    void set_out_root_value(const QString& dir, bool emit_signal) {
        if (out_root_edit->text() == dir) return;
        const QSignalBlocker blocker(out_root_edit);
        out_root_edit->setText(dir);
        if (emit_signal) emit q->config_changed();
    }

    void set_color_target_value(pp::ColorTarget target) {
        const QSignalBlocker blocker(color_combo);
        color_combo->setCurrentIndex(color_to_index(target));
    }

    void set_conflict_value(pp::ConflictPolicy policy) {
        const QSignalBlocker blocker(conflict_combo);
        conflict_combo->setCurrentIndex(conflict_to_index(policy));
    }
};

// 指针键注册表：析构擦除 → 无悬挂/别名；GUI 线程独占，无需锁。
std::map<const PageOutput*, std::unique_ptr<Impl>>& registry() {
    static std::map<const PageOutput*, std::unique_ptr<Impl>> store;
    return store;
}

Impl* impl_of(const PageOutput* page) {
    const auto it = registry().find(page);
    return it == registry().end() ? nullptr : it->second.get();
}

}  // namespace

// ---------------------------------------------------------------- PageOutput

PageOutput::PageOutput(QWidget* parent) : QWidget(parent) {
    auto impl = std::make_unique<Impl>();
    impl->q = this;
    impl->build_ui();
    impl->wire();
    impl->apply_format("jxl", /*emit_signals=*/false);   // §2.9.1 默认 jxl
    impl->update_metadata_only_state();
    registry().emplace(this, std::move(impl));
}

PageOutput::~PageOutput() { registry().erase(this); }

pp::RunConfig PageOutput::config_base() const {
    Impl* impl_ = impl_of(this);
    pp::RunConfig cfg;
    const QString root = impl_->out_root_edit->text().trimmed();
    if (!root.isEmpty()) cfg.out_root = std::filesystem::path(root.toStdString());
    cfg.format_id = impl_->current_format_id.toStdString();
    if (impl_->param_form) {
        const FormSelection sel = impl_->param_form->selection();
        cfg.backend_id = sel.backend;
        cfg.tech_id = sel.tech;
        cfg.lossless = sel.lossless;
        cfg.params = impl_->param_form->values();
    }
    cfg.out_bitdepth = impl_->bitdepth();
    cfg.color_target = impl_->color_target();
    cfg.conflict = impl_->conflict();
    cfg.metadata_only = impl_->metadata_only;
    // rules / workers / budget_bytes / rotate_orientation / flatten_gray 由 MainWindow 填充
    return cfg;
}

bool PageOutput::metadata_only() const {
    Impl* impl_ = impl_of(this);
    return impl_->metadata_only;
}

void PageOutput::set_metadata_only(bool on) {
    Impl* impl_ = impl_of(this);
    if (impl_->metadata_only == on) return;
    impl_->metadata_only = on;
    {
        const QSignalBlocker block_convert(impl_->mode_convert);
        const QSignalBlocker block_meta(impl_->mode_meta);
        impl_->mode_convert->setChecked(!on);
        impl_->mode_meta->setChecked(on);
    }
    impl_->update_metadata_only_state();
    emit config_changed();
}

QString PageOutput::out_root() const {
    Impl* impl_ = impl_of(this);
    return impl_->out_root_edit->text();
}

void PageOutput::set_out_root(const QString& dir) {
    Impl* impl_ = impl_of(this);
    impl_->set_out_root_value(dir, /*emit_signal=*/true);
}

void PageOutput::set_batch_has_alpha(bool has) {
    Impl* impl_ = impl_of(this);
    impl_->user_backend_override = false;   // §9.1：本入口清除用户手动覆盖
    const bool state_changed = (impl_->batch_has_alpha != has);
    impl_->batch_has_alpha = has;
    const bool switched = impl_->evaluate_avif_alpha_preselect();
    impl_->update_alpha_hint();
    if (state_changed || switched) emit config_changed();
}

void PageOutput::apply_preset(const pp::PresetData& p) {
    Impl* impl_ = impl_of(this);
    pp::PresetData preset = p;
    pp::normalize_preset(preset);   // 先用当前格式表补齐
    if (!pp::validate_preset(preset).empty()) return;   // 校验失败 → 不应用，保持原状

    const bool format_changed_now =
        (preset.format_id != impl_->current_format_id.toStdString());
    impl_->apply_format(preset.format_id, /*emit_signals=*/false);
    if (impl_->param_form) {
        FormSelection sel;
        sel.backend = preset.backend_id;
        sel.tech = preset.tech_id;
        sel.lossless = preset.lossless;
        impl_->param_form->set_selection(sel);     // 程序化：不发信号
        impl_->param_form->set_values(preset.params);
    }
    impl_->set_bitdepth_options(preset.out_bitdepth, /*keep_if_valid=*/false);
    impl_->set_color_target_value(preset.color_target);
    impl_->set_conflict_value(preset.conflict);
    impl_->remember_backend();
    impl_->evaluate_avif_alpha_preselect();   // §9.1：预设落值后同样求值
    impl_->update_alpha_hint();
    if (format_changed_now) emit format_changed(impl_->current_format_id);
    emit config_changed();
}

pp::PresetData PageOutput::collect_preset(const QString& name) const {
    Impl* impl_ = impl_of(this);
    pp::PresetData p;
    p.version = 1;
    p.name = name.toStdString();
    p.format_id = impl_->current_format_id.toStdString();
    if (impl_->param_form) {
        const FormSelection sel = impl_->param_form->selection();
        p.backend_id = sel.backend;
        p.tech_id = sel.tech;
        p.lossless = sel.lossless;
        p.params = impl_->param_form->values();
    }
    p.out_bitdepth = impl_->bitdepth();
    p.color_target = impl_->color_target();
    p.conflict = impl_->conflict();
    // rules 由 MainWindow 合并（§2.9）
    return p;
}

void PageOutput::restore_last(const QString& format_id, const QString& out_root) {
    Impl* impl_ = impl_of(this);
    if (!format_id.isEmpty() && pp::find_format(format_id.toStdString()) != nullptr)
        impl_->apply_format(format_id.toStdString(), /*emit_signals=*/false);
    if (!out_root.isEmpty()) impl_->set_out_root_value(out_root, /*emit_signal=*/false);
    impl_->update_alpha_hint();
}

QString PageOutput::ready_to_start() const {
    Impl* impl_ = impl_of(this);
    const QString root = impl_->out_root_edit->text().trimmed();
    if (root.isEmpty()) return tr("未设置输出根目录");
    if (!QDir::isAbsolutePath(root)) return tr("输出根目录必须是绝对路径");
    return QString();
}

QString PageOutput::current_format() const {
    Impl* impl_ = impl_of(this);
    return impl_->current_format_id;
}

void PageOutput::select_format(const QString& format_id) {
    Impl* impl_ = impl_of(this);
    if (format_id.isEmpty()) return;
    impl_->apply_format(format_id.toStdString(), /*emit_signals=*/false);
}

}  // namespace pp::ui
