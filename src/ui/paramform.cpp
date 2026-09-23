// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — schema-driven parameter form engine (M1b-U3)
//
// 行为规格落地（docs/m1b-tasks.md §2.6 条 1..8）：
//   1. 顶部选择器行：后端（backends.size()>1）/ 技术（当前后端 techs.size()>1）/
//      无损（当前后端存在 lossless_capable 技术——条 2 的 jpeg 反例即隐藏）。
//   2. 无损 = 保留键 "__lossless"（m1-tasks §3.4）：勾选写工作集并 pp::apply_locks；
//      当前技术不可无损 → 自动切首个 lossless_capable 技术（jxl→modular、webp→lossless）；
//      取消时若该切换是本表单自动做的（pre_lossless_tech 记忆）则切回原技术。
//      技术下拉在无损开启期间把非 lossless_capable 项置灰（disabled item）。
//   3. Int→QSpinBox / Float→QDoubleSpinBox(decimals=3) / Bool→QCheckBox / Enum→QComboBox；
//      tooltip = ParamDef.tooltip；核心直排，advanced 收进可折叠 "高级参数" 组
//      （起始折叠）+ 组内顶部搜索框（label/key 包含过滤，大小写不敏感）。
//   4. 任何值/选择变化 → 对当前技术每个参数重算 eval_visible（隐藏行）/ eval_lock
//      （控件禁用 + 强制值写入工作集）。
//   5. 值 ≠ def（同类型比较）→ 行标签前缀 "● "；标签右键菜单 "重置为默认"。
//   6. values() = 工作集去掉 "__" 前缀键；set_values() 同 key 同类型才应用。
//   7. 用户操作 → changed()；用户改后端/技术/无损 → 先 selection_changed 再 changed()；
//      set_selection/set_values 不发信号。
//   8. heif/avif：单一 "runtime" 技术 → 技术下拉隐藏；参数全部来自内省 TechDef。
#include "ui/paramform.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace pp::ui {
namespace {

// 保留键（m1-tasks §3.4）：随工作集传递，但不序列化、不显示、不出现在 values()
constexpr const char *kLosslessKey = "__lossless";
// §9.1 U3：参数表中名为 "lossless" 的参数（内省 heif/avif 暴露）不渲染为行——顶部"无损"
// 复选框独占该参数（值随复选框同步写入工作集，编码器仍从参数集读它）。
constexpr const char *kLosslessParamKey = "lossless";

bool is_reserved_key(const std::string &key) { return key.rfind("__", 0) == 0; }

// "同类型" 判定（条 6）：Int=int64、Float=double（int64/double 互不容忍）、
// Bool=bool、Enum=与某个 choice 的值完全相同（类型 + 取值一起匹配）
bool same_typed_value(const pp::ParamDef &p, const pp::ParamValue &v) {
    switch (p.type) {
    case pp::ParamType::Int:
        return std::holds_alternative<int64_t>(v);
    case pp::ParamType::Float:
        return std::holds_alternative<double>(v);
    case pp::ParamType::Bool:
        return std::holds_alternative<bool>(v);
    case pp::ParamType::Enum:
        return std::any_of(p.choices.begin(), p.choices.end(),
                           [&v](const auto &c) { return c.second == v; });
    }
    return false;
}

const pp::ParamDef *find_param(const pp::TechDef &t, const std::string &key) {
    for (const pp::ParamDef &p : t.params)
        if (p.key == key)
            return &p;
    return nullptr;
}

const pp::TechDef *find_tech_in(const pp::BackendDef *b, const std::string &id) {
    if (!b)
        return nullptr;
    for (const pp::TechDef &t : b->techs)
        if (t.id == id)
            return &t;
    return nullptr;
}

const pp::TechDef *first_lossless_tech_in(const pp::BackendDef *b) {
    if (!b)
        return nullptr;
    for (const pp::TechDef &t : b->techs)
        if (t.lossless_capable)
            return &t;
    return nullptr;
}

// 参数键集合一致（静态表路径与调用方传入的后端表是同一份定义时才走引擎默认值路径）
bool same_param_keys(const pp::TechDef &a, const pp::TechDef &b) {
    if (a.params.size() != b.params.size())
        return false;
    for (std::size_t i = 0; i < a.params.size(); ++i)
        if (a.params[i].key != b.params[i].key)
            return false;
    return true;
}

} // namespace

struct ParamForm::Impl {
    ParamForm *q = nullptr;
    const pp::FormatDef *fmt = nullptr;
    std::vector<pp::BackendDef> backends; // 生效后端表（静态表拷贝或运行时内省结果）
    FormSelection sel;                    // 已解析（非空）的当前选择
    pp::ParamSet values;                  // 工作集（含保留键 "__lossless"）
    std::string pre_lossless_tech;        // 本表单自动切走前的技术（取消无损时切回）

    QVBoxLayout *root = nullptr;
    QWidget *selector_row = nullptr;
    QLabel *backend_label = nullptr;
    QComboBox *backend_combo = nullptr;
    QLabel *tech_label = nullptr;
    QComboBox *tech_combo = nullptr;
    QCheckBox *lossless_check = nullptr;

    QWidget *core_area = nullptr;
    QFormLayout *core_form = nullptr;
    QGroupBox *adv_group = nullptr;
    QWidget *adv_area = nullptr;
    QFormLayout *adv_form = nullptr;
    QLineEdit *adv_search = nullptr;
    QLabel *cross_error = nullptr; // §2.7：交叉参数约束红字区（参数组底部）

    struct Row {
        const pp::ParamDef *def = nullptr; // 指向生效 TechDef 的 ParamDef（生命周期 >= 本控件）
        std::string key;
        QLabel *label = nullptr;
        QWidget *editor = nullptr;
        bool advanced = false;
        bool locked = false;
        bool shown = true; // 谓词可见 且 通过高级区搜索过滤（is_param_visible 口径）
    };
    std::vector<Row> rows;

    bool syncing = false; // 程序化写控件期间抑制 valueChanged 回写

    // ---------------------------------------------------------------- 构造

    Impl(ParamForm *owner, const pp::FormatDef &f, std::vector<pp::BackendDef> bs)
        : q(owner), fmt(&f), backends(std::move(bs)) {
        if (backends.empty())
            backends = fmt->backends; // 内省不可用 → 回退静态表（不崩、可显示）
        root = new QVBoxLayout(q);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(6);
        build_selectors();
        build_cross_error();

        sel.backend = backends.empty() ? std::string() : backends.front().id;
        const pp::TechDef *t = tech();
        sel.tech = t ? t->id : std::string();
        sel.lossless = false;
        populate_tech_combo();
        rebuild_params();
        refresh();
    }

    // ---------------------------------------------------------------- 查询

    const pp::BackendDef *backend() const {
        for (const pp::BackendDef &b : backends)
            if (b.id == sel.backend)
                return &b;
        return backends.empty() ? nullptr : &backends.front();
    }

    const pp::TechDef *tech() const {
        const pp::BackendDef *b = backend();
        if (!b)
            return nullptr;
        const pp::TechDef *t = find_tech_in(b, sel.tech);
        return t ? t : (b->techs.empty() ? nullptr : &b->techs.front());
    }

    const pp::TechDef *tech_by_id(const std::string &id) const {
        return find_tech_in(backend(), id);
    }

    // ---------------------------------------------------------------- 顶部选择器行

    void build_selectors() {
        selector_row = new QWidget(q);
        selector_row->setObjectName(QStringLiteral("pp-selector-row"));
        auto *h = new QHBoxLayout(selector_row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);

        backend_label = new QLabel(ParamForm::tr("后端"), selector_row);
        backend_combo = new QComboBox(selector_row);
        backend_combo->setObjectName(QStringLiteral("pp-backend-combo"));
        for (const pp::BackendDef &b : backends)
            backend_combo->addItem(QString::fromStdString(b.label.empty() ? b.id : b.label),
                                   QString::fromStdString(b.id));
        h->addWidget(backend_label);
        h->addWidget(backend_combo);

        tech_label = new QLabel(ParamForm::tr("技术"), selector_row);
        tech_combo = new QComboBox(selector_row);
        tech_combo->setObjectName(QStringLiteral("pp-tech-combo"));
        h->addWidget(tech_label);
        h->addWidget(tech_combo);

        lossless_check = new QCheckBox(ParamForm::tr("无损"), selector_row);
        lossless_check->setObjectName(QStringLiteral("pp-lossless-check"));
        lossless_check->setToolTip(
            ParamForm::tr("无损输出（写入保留键 __lossless，驱动参数谓词）"));
        h->addWidget(lossless_check);
        h->addStretch(1);

        root->addWidget(selector_row);

        QObject::connect(backend_combo, &QComboBox::currentIndexChanged, q, [this](int idx) {
            if (syncing || idx < 0 || idx >= static_cast<int>(backends.size()))
                return;
            user_set_backend(backends[static_cast<std::size_t>(idx)].id);
        });
        QObject::connect(tech_combo, &QComboBox::currentIndexChanged, q, [this](int idx) {
            if (syncing || idx < 0)
                return;
            const pp::BackendDef *b = backend();
            if (!b || idx >= static_cast<int>(b->techs.size()))
                return;
            user_set_tech(b->techs[static_cast<std::size_t>(idx)].id);
        });
        QObject::connect(lossless_check, &QCheckBox::toggled, q, [this](bool on) {
            if (syncing)
                return;
            user_set_lossless(on);
        });
    }

    void populate_tech_combo() {
        syncing = true;
        tech_combo->clear();
        const pp::BackendDef *b = backend();
        int current = -1;
        if (b) {
            for (std::size_t i = 0; i < b->techs.size(); ++i) {
                const pp::TechDef &t = b->techs[i];
                tech_combo->addItem(QString::fromStdString(t.label.empty() ? t.id : t.label),
                                    QString::fromStdString(t.id));
                if (t.id == sel.tech)
                    current = static_cast<int>(i);
            }
        }
        if (current >= 0)
            tech_combo->setCurrentIndex(current);
        update_tech_items_enabled();
        syncing = false;
    }

    // 无损开启期间：非 lossless_capable 技术置灰（条 2）
    void update_tech_items_enabled() {
        auto *model = qobject_cast<QStandardItemModel *>(tech_combo->model());
        const pp::BackendDef *b = backend();
        if (!model || !b)
            return;
        for (int i = 0; i < static_cast<int>(b->techs.size()) && i < model->rowCount(); ++i) {
            QStandardItem *item = model->item(i);
            if (!item)
                continue;
            item->setEnabled(!sel.lossless ||
                             b->techs[static_cast<std::size_t>(i)].lossless_capable);
        }
    }

    void update_selectors() {
        syncing = true;
        const pp::BackendDef *b = backend();
        const bool multi_backend = backends.size() > 1;
        backend_label->setVisible(multi_backend);
        backend_combo->setVisible(multi_backend);
        const bool multi_tech = b && b->techs.size() > 1;
        tech_label->setVisible(multi_tech);
        tech_combo->setVisible(multi_tech);
        lossless_check->setVisible(first_lossless_tech_in(b) != nullptr);
        lossless_check->setChecked(sel.lossless);
        for (int i = 0; i < backend_combo->count(); ++i) {
            if (backend_combo->itemData(i).toString() == QString::fromStdString(sel.backend)) {
                backend_combo->setCurrentIndex(i);
                break;
            }
        }
        for (int i = 0; i < tech_combo->count(); ++i) {
            if (tech_combo->itemData(i).toString() == QString::fromStdString(sel.tech)) {
                tech_combo->setCurrentIndex(i);
                break;
            }
        }
        update_tech_items_enabled();
        syncing = false;
    }

    // ---------------------------------------------------------------- 行构建

    // §2.7：参数组底部的红字交叉校验区（objectName pp-cross-error，#D02222）。
    // 只创建一次；build_rows() 每次重建控件后把它移回布局末尾。
    void build_cross_error() {
        cross_error = new QLabel(q);
        cross_error->setObjectName(QStringLiteral("pp-cross-error"));
        cross_error->setStyleSheet(QStringLiteral("color: #D02222;"));
        cross_error->setTextFormat(Qt::PlainText);
        cross_error->setWordWrap(true);
        cross_error->setVisible(false);
    }

    void build_rows() {
        rows.clear();
        delete core_area;
        core_area = nullptr;
        core_form = nullptr;
        delete adv_group;
        adv_group = nullptr;
        adv_area = nullptr;
        adv_form = nullptr;
        adv_search = nullptr;

        core_area = new QWidget(q);
        core_area->setObjectName(QStringLiteral("pp-core-area"));
        core_form = new QFormLayout(core_area);
        core_form->setContentsMargins(0, 0, 0, 0);
        core_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        adv_group = new QGroupBox(ParamForm::tr("高级参数"), q);
        adv_group->setObjectName(QStringLiteral("pp-advanced-group"));
        adv_group->setCheckable(true);
        adv_group->setChecked(false); // collapsed 起始
        auto *adv_outer = new QVBoxLayout(adv_group);
        adv_outer->setContentsMargins(2, 0, 2, 2); // §9.1：标题↔搜索框间距收紧
        adv_outer->setSpacing(2);
        adv_area = new QWidget(adv_group);
        adv_area->setObjectName(QStringLiteral("pp-advanced-area"));
        adv_form = new QFormLayout(adv_area);
        adv_form->setContentsMargins(0, 0, 0, 0);
        adv_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        adv_search = new QLineEdit(adv_area);
        adv_search->setObjectName(QStringLiteral("pp-advanced-search"));
        adv_search->setPlaceholderText(ParamForm::tr("搜索参数…"));
        adv_search->setClearButtonEnabled(true);
        // §9.1：搜索框与相邻参数控件同列同宽（空标签占位 → 落在字段列，不拉满整行）
        // M2-T7 #28b（实测复核后裁定不改布局）：搜索框过滤的就是高级参数行，其左边界与
        // 这些行的编辑器字段列严格相等（自验实测 jxl 134/134、jpeg 115/115、webp 132/132、
        // tiff 79/79，d=0）；与上一层"核心参数"表单字段列的差值（jxl 60px）来自另一个
        // QFormLayout 的标签列宽，且随格式在 -79..+60px 浮动——按它对齐会让搜索框随格式
        // 漂移、并与所过滤的行错列。等式由 .cache/tmp/m2t7-selftest 断言锁死。
        adv_form->addRow(QString(), adv_search);
        adv_area->setVisible(false);
        adv_outer->addWidget(adv_area);
        QObject::connect(adv_group, &QGroupBox::toggled, adv_area, &QWidget::setVisible);
        QObject::connect(adv_search, &QLineEdit::textChanged, q, [this](const QString &) {
            if (syncing)
                return;
            update_row_states(); // 过滤只影响视图，不是值变化 → 不发信号
        });

        if (const pp::TechDef *t = tech()) {
            for (const pp::ParamDef &p : t->params) {
                if (p.key == kLosslessParamKey)
                    continue; // §9.1：由顶部"无损"复选框独占
                add_row(p);
            }
        }

        root->addWidget(core_area);
        root->addWidget(adv_group);
        // §2.7：跨格式/技术重建后，红字区恒为参数组最后一行
        root->removeWidget(cross_error);
        root->addWidget(cross_error);
    }

    void add_row(const pp::ParamDef &p) {
        const QString tip = QString::fromStdString(p.tooltip);
        QWidget *parent =
            p.advanced ? static_cast<QWidget *>(adv_area) : static_cast<QWidget *>(core_area);

        Row r;
        r.def = &p;
        r.key = p.key;
        r.advanced = p.advanced;
        r.label = new QLabel(QString::fromStdString(p.label), parent);
        r.label->setObjectName(QStringLiteral("pp-label-") + QString::fromStdString(p.key));
        r.label->setContextMenuPolicy(Qt::CustomContextMenu);
        r.label->setToolTip(tip);

        switch (p.type) {
        case pp::ParamType::Int: {
            auto *spin = new QSpinBox(parent);
            int lo = static_cast<int>(std::lround(p.lo));
            int hi = static_cast<int>(std::lround(p.hi));
            if (hi < lo)
                std::swap(lo, hi);
            spin->setRange(lo, hi);
            spin->setSingleStep(std::max(1, static_cast<int>(std::lround(p.step))));
            r.editor = spin;
            QObject::connect(spin, &QSpinBox::valueChanged, q, [this, key = p.key](int v) {
                if (syncing)
                    return;
                values[key] = static_cast<int64_t>(v);
                refresh();
                emit q->changed();
            });
            break;
        }
        case pp::ParamType::Float: {
            auto *spin = new QDoubleSpinBox(parent);
            double lo = p.lo, hi = p.hi;
            if (hi < lo)
                std::swap(lo, hi);
            spin->setRange(lo, hi);
            spin->setDecimals(3); // 条 3：decimals=3
            spin->setSingleStep(p.step > 0.0 ? p.step : 1.0);
            r.editor = spin;
            QObject::connect(spin, &QDoubleSpinBox::valueChanged, q, [this, key = p.key](double v) {
                if (syncing)
                    return;
                values[key] = v;
                refresh();
                emit q->changed();
            });
            break;
        }
        case pp::ParamType::Bool: {
            auto *box = new QCheckBox(parent);
            r.editor = box;
            QObject::connect(box, &QCheckBox::toggled, q, [this, key = p.key](bool on) {
                if (syncing)
                    return;
                values[key] = on;
                refresh();
                emit q->changed();
            });
            break;
        }
        case pp::ParamType::Enum: {
            auto *combo = new QComboBox(parent);
            for (const auto &choice : p.choices)
                combo->addItem(QString::fromStdString(choice.first));
            r.editor = combo;
            const pp::ParamDef *def = &p;
            QObject::connect(
                combo, &QComboBox::currentIndexChanged, q, [this, key = p.key, def](int idx) {
                    if (syncing || idx < 0 || idx >= static_cast<int>(def->choices.size()))
                        return;
                    values[key] = def->choices[static_cast<std::size_t>(idx)].second;
                    refresh();
                    emit q->changed();
                });
            break;
        }
        }
        r.editor->setObjectName(QStringLiteral("pp-param-") + QString::fromStdString(p.key));
        r.editor->setToolTip(tip);

        QFormLayout *form = p.advanced ? adv_form : core_form;
        form->addRow(r.label, r.editor);
        rows.push_back(r);

        // 条 5：标签右键菜单 → 重置为默认
        QObject::connect(r.label, &QWidget::customContextMenuRequested, q,
                         [this, label = r.label, key = r.key](const QPoint &pos) {
                             QMenu menu;
                             QAction *reset = menu.addAction(ParamForm::tr("重置为默认"));
                             if (menu.exec(label->mapToGlobal(pos)) == reset)
                                 reset_to_default(key);
                         });
    }

    // ---------------------------------------------------------------- 值 / 谓词

    // 默认值全集：静态表携带该 backend/tech（键集合一致）时走 pp::default_params，
    // 运行时内省后端（heif/avif，techs 只在构造传入的表里）自行取 ParamDef::def。
    pp::ParamSet make_defaults() const {
        const pp::TechDef *effective = tech();
        const pp::BackendDef *sb = pp::find_backend(*fmt, sel.backend);
        const pp::TechDef *st = sb ? pp::find_tech(*sb, sel.tech) : nullptr;
        if (st && effective && same_param_keys(*st, *effective))
            return pp::default_params(*fmt, sel.backend, sel.tech, sel.lossless);
        pp::ParamSet s;
        s[kLosslessKey] = sel.lossless;
        if (effective)
            for (const pp::ParamDef &p : effective->params)
                s[p.key] = p.def;
        return s;
    }

    // 技术/后端变化 → 重建工作集（默认值 + 同 key 同类型沿用旧值）与行控件
    void rebuild_params() {
        const pp::ParamSet old = values;
        values = make_defaults();
        if (const pp::TechDef *t = tech()) {
            for (const auto &[k, v] : old) {
                if (is_reserved_key(k))
                    continue;
                const pp::ParamDef *p = find_param(*t, k);
                if (p && same_typed_value(*p, v))
                    values[k] = v;
            }
        }
        values[kLosslessKey] = sel.lossless;
        build_rows();
    }

    // 条 4：谓词重算 —— apply_locks → 强制值写回 → 控件同步 → 可见性/●
    void refresh() {
        values[kLosslessKey] = sel.lossless;
        // §9.1：名为 "lossless" 的参数没有行，其值由顶部"无损"复选框独占（heif/avif 内省参数）
        if (const pp::TechDef *t = tech(); t && find_param(*t, kLosslessParamKey))
            values[kLosslessParamKey] = sel.lossless;
        pp::apply_locks(*fmt, sel.backend, sel.tech, sel.lossless, values);
        for (Row &r : rows) {
            const std::optional<pp::ParamValue> forced = pp::eval_lock(*r.def, values);
            r.locked = forced.has_value();
            if (forced)
                values[r.key] = *forced; // 锁定 → 值显示为强制值（写入工作集）
        }
        sync_widgets();
        update_row_states();
        update_selectors();
        update_cross_error();
    }

    // §2.7：值变化时求值交叉约束；非空 → 红字逐条换行，空 → 隐藏。
    // 输入是 refresh() 已应用锁定后的工作集（= 真正会下发给编码器的值）。
    void update_cross_error() {
        if (!cross_error)
            return;
        const std::vector<std::string> msgs = pp::cross_validate(values, fmt->id, sel.tech);
        QString text;
        for (const std::string &m : msgs) {
            if (!text.isEmpty())
                text += QLatin1Char('\n');
            text += QString::fromStdString(m);
        }
        cross_error->setText(text);
        cross_error->setVisible(!msgs.empty());
    }

    void sync_widgets() {
        syncing = true;
        for (Row &r : rows) {
            const auto it = values.find(r.key);
            if (it != values.end()) {
                const pp::ParamValue &v = it->second;
                switch (r.def->type) {
                case pp::ParamType::Int:
                    if (const int64_t *i = std::get_if<int64_t>(&v)) {
                        const int64_t clamped = std::clamp<int64_t>(
                            *i, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
                        static_cast<QSpinBox *>(r.editor)->setValue(static_cast<int>(clamped));
                    }
                    break;
                case pp::ParamType::Float:
                    if (const double *d = std::get_if<double>(&v))
                        static_cast<QDoubleSpinBox *>(r.editor)->setValue(*d);
                    break;
                case pp::ParamType::Bool:
                    if (const bool *b = std::get_if<bool>(&v))
                        static_cast<QCheckBox *>(r.editor)->setChecked(*b);
                    break;
                case pp::ParamType::Enum: {
                    int idx = -1;
                    for (std::size_t i = 0; i < r.def->choices.size(); ++i) {
                        if (r.def->choices[i].second == v) {
                            idx = static_cast<int>(i);
                            break;
                        }
                    }
                    static_cast<QComboBox *>(r.editor)->setCurrentIndex(idx);
                    break;
                }
                }
            }
            r.editor->setEnabled(!r.locked);
        }
        syncing = false;
    }

    void update_row_states() {
        const QString filter = adv_search ? adv_search->text() : QString();
        for (Row &r : rows) {
            bool shown = pp::eval_visible(*r.def, values); // 条 4：不可见 → 行隐藏
            if (shown && r.advanced && !filter.isEmpty()) {
                const QString label = QString::fromStdString(r.def->label);
                const QString key = QString::fromStdString(r.key);
                shown = label.contains(filter, Qt::CaseInsensitive) ||
                        key.contains(filter, Qt::CaseInsensitive); // 条 3：包含过滤
            }
            r.shown = shown;
            QFormLayout *form = r.advanced ? adv_form : core_form;
            if (form)
                form->setRowVisible(r.label, shown);

            const auto it = values.find(r.key);
            const bool deviates = it != values.end() && it->second.index() == r.def->def.index() &&
                                  !(it->second == r.def->def); // 条 5：同类型比较
            r.label->setText((deviates ? QStringLiteral("● ") : QString()) +
                             QString::fromStdString(r.def->label));
        }
    }

    void reset_to_default(const std::string &key) {
        const pp::ParamDef *p = nullptr;
        for (const Row &r : rows)
            if (r.key == key) {
                p = r.def;
                break;
            }
        if (!p)
            return;
        values[key] = p->def;
        refresh();
        emit q->changed();
    }

    // ---------------------------------------------------------------- 选择变化

    void apply_state(const std::string &backend_id, const std::string &tech_id, bool lossless) {
        const bool rebuild = (backend_id != sel.backend) || (tech_id != sel.tech);
        sel.backend = backend_id;
        sel.tech = tech_id;
        sel.lossless = lossless;
        if (rebuild) {
            populate_tech_combo();
            rebuild_params();
        }
        refresh();
    }

    void user_set_backend(const std::string &id) {
        if (id == sel.backend)
            return;
        const pp::BackendDef *b = nullptr;
        for (const pp::BackendDef &x : backends)
            if (x.id == id)
                b = &x;
        if (!b)
            return;
        pre_lossless_tech.clear();
        std::string tech_id = b->techs.empty() ? std::string() : b->techs.front().id;
        bool lossless = sel.lossless;
        if (lossless) {
            const pp::TechDef *lt = first_lossless_tech_in(b);
            if (lt)
                tech_id = lt->id;
            else
                lossless = false; // 新后端无 lossless 技术 → 复选框消失，标志归零
        }
        apply_state(id, tech_id, lossless);
        emit q->selection_changed(sel); // 条 7：先 selection_changed
        emit q->changed();              //        再 changed
    }

    void user_set_tech(const std::string &id) {
        if (id == sel.tech)
            return;
        const pp::TechDef *t = tech_by_id(id);
        if (!t)
            return;
        if (sel.lossless && !t->lossless_capable) { // 置灰项：拒绝并回到当前项
            update_selectors();
            return;
        }
        pre_lossless_tech.clear(); // 手动改技术 → 不再记忆自动切换
        apply_state(sel.backend, id, sel.lossless);
        emit q->selection_changed(sel);
        emit q->changed();
    }

    void user_set_lossless(bool on) {
        if (on == sel.lossless)
            return;
        std::string tech_id = sel.tech;
        std::string memo = pre_lossless_tech;
        if (on) {
            const pp::TechDef *ct = tech();
            if (!ct || !ct->lossless_capable) {
                const pp::TechDef *lt = first_lossless_tech_in(backend());
                if (!lt) { // 无 lossless_capable 技术（复选框此时本应隐藏）
                    update_selectors();
                    return;
                }
                memo = sel.tech;
                tech_id = lt->id; // jxl→modular、webp→lossless
            } else {
                memo.clear();
            }
        } else if (!memo.empty() && memo != sel.tech && tech_by_id(memo)) {
            tech_id = memo; // 撤销本表单造成的那次自动切换
            memo.clear();
        } else {
            memo.clear();
        }
        pre_lossless_tech = memo;
        apply_state(sel.backend, tech_id, on);
        emit q->selection_changed(sel);
        emit q->changed();
    }

    void set_selection(const FormSelection &s) {
        std::string backend_id = s.backend;
        const pp::BackendDef *b = nullptr;
        for (const pp::BackendDef &x : backends)
            if (x.id == backend_id)
                b = &x;
        if (!b) {
            b = backends.empty() ? nullptr : &backends.front(); // "" 或未知 → 首个
            backend_id = b ? b->id : std::string();
        }
        std::string tech_id = s.tech;
        if (!find_tech_in(b, tech_id))
            tech_id = (b && !b->techs.empty()) ? b->techs.front().id : std::string();
        bool lossless = s.lossless;
        if (lossless) {
            const pp::TechDef *ct = find_tech_in(b, tech_id);
            if (!ct || !ct->lossless_capable) {
                const pp::TechDef *lt = first_lossless_tech_in(b);
                if (lt)
                    tech_id = lt->id; // 维持 "lossless ⇒ 技术可无损" 不变式
                else
                    lossless = false;
            }
        }
        if (backend_id != sel.backend)
            pre_lossless_tech.clear();
        apply_state(backend_id, tech_id, lossless); // 不发信号（条 7）
    }

    // ---------------------------------------------------------------- 值接口

    void set_values(const pp::ParamSet &s) {
        const pp::TechDef *t = tech();
        if (!t)
            return;
        for (const auto &[k, v] : s) {
            if (is_reserved_key(k))
                continue; // 保留键不从外部注入
            const pp::ParamDef *p = find_param(*t, k);
            if (!p)
                continue; // 未知 key 忽略
            if (!same_typed_value(*p, v))
                continue; // 同类型才应用
            values[k] = v;
        }
        refresh();
    }

    pp::ParamSet public_values() const {
        pp::ParamSet out;
        for (const auto &[k, v] : values)
            if (!is_reserved_key(k))
                out.emplace(k, v);
        return out;
    }

    bool is_visible(const std::string &key) const {
        for (const Row &r : rows)
            if (r.key == key)
                return r.shown;
        return false;
    }
};

// ---------------------------------------------------------------- ParamForm

ParamForm::ParamForm(const pp::FormatDef &fmt, std::vector<pp::BackendDef> backends,
                     QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(this, fmt, std::move(backends))) {}

ParamForm::~ParamForm() = default;

void ParamForm::set_selection(const FormSelection &sel) { impl_->set_selection(sel); }

FormSelection ParamForm::selection() const { return impl_->sel; }

pp::ParamSet ParamForm::values() const { return impl_->public_values(); }

void ParamForm::set_values(const pp::ParamSet &s) { impl_->set_values(s); }

bool ParamForm::is_param_visible(const std::string &key) const { return impl_->is_visible(key); }

} // namespace pp::ui
