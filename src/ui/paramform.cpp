// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — schema-driven parameter form engine (M1b-U3)
//
// 行为规格落地（docs/m1b-tasks.md §2.6 条 1..8）：
//   1. 顶部选择行：后端（backends.size()>1）/ 技术（当前后端 techs.size()>1）/
//      无损（当前后端存在 lossless_capable 技术——条 2 的 jpeg 反例即隐藏）。
//   2. 无损 = 保留键 kLosslessKey（= pp::kLosslessKey，m1-tasks §3.4；键值单源，不在本文件
//      重抄字面量）：勾选写工作集并 pp::apply_locks；
//      当前技术不可无损 → 自动切首个 lossless_capable 技术（jxl→modular、webp→lossless）；
//      取消时若该切换是本表单自动做的（pre_lossless_tech 记忆）则切回原技术。
//      技术下拉在无损开启期间把非 lossless_capable 项置灰（disabled item）。
//   3. Int→QSpinBox / Float→QDoubleSpinBox(decimals=3) / Bool→QCheckBox / Enum→QComboBox；
//      tooltip = ParamDef.tooltip；核心直排，advanced 收进可折叠 "高级参数" 组
//      （起始折叠）+ 组内顶部搜索框（label/key 包含过滤，大小写不敏感）。
//   4. 任何值/选择变化 → 对当前技术每个参数重算 eval_visible（隐藏行）/ eval_lock
//      （控件禁用 + 强制值写入工作集）。
//   5. 值 ≠ def（同类型比较）→ 改值标记（theme::set_mod：强调边框 + 强调色字；裁定 A8：`●`
//      前缀字形已删）+ 标签右键菜单 "重置为默认"。
//   6. values() = 工作集去掉 "__" 前缀键；set_values() 同 key 同类型才应用。
//   7. 用户操作 → changed()；用户改后端/技术/无损 → 先 selection_changed 再 changed()；
//      set_selection/set_values 不发信号。
//   8. heif/avif：单一 "runtime" 技术 → 技术下拉隐藏；参数全部来自内省 TechDef。
//
// M5-T8（W-G3 · §7.2 格式参数卡 / §5 R1–R4；评审 2-18/2-19/2-20/2-31/2-32/2-33/2-34/2-35/
// 2-37/2-39/2-40/2-41/2-42）：
//   * 一切行经 formkit 构造器（R1 pitch 28 / R2 标签轨 80 / 字段宽 88·160 两档 / R3 范围入
//     10px 注释）—— 组内外同一标签轴与同一起点（2-32）；
//   * 技术 / 无损 = 普通表单行；布尔参数行语序「勾选框→标签」且文字紧邻勾选框（2-34/2-35）；
//   * 高级参数 = 现代折叠行（chevron + 摘要、**无** QGroupBox、无双层框线，2-19/2-31），
//     区内 = 搜索行（带标签与用途说明，2-40/2-41）+ 子分组（2-39）+ 限高内滚（2-42）；
//   * 数值控件只取短 88（组内小数值）/ 标准 160（核心独立数值）两档，不再拉满行宽（2-33）；
//   * 魔法值（范围下端 < 0 = 编码器自选）显示「自动」（specialValueText；枚举的 "auto" 档
//     同义显示为「自动」）—— 修 2-37。
#include "ui/paramform.h"

#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <utility>

#include "ui/contextmenu.h"    // UX-1 ⑧：格式参数卡右键菜单（actions 单源）
#include "ui/formkit.h"        // C2：行构造器（R1–R4）+ 自绘勾选框（M5-T8）
#include "ui/icons.h"          // 折叠插入符 = 矢量图标（C3；禁字形）
#include "ui/leading_indent.h" // R-218：逻辑边距（空态提示与表单同轴）
#include "ui/theme.h"

namespace pp::ui {
namespace {

// 无损的两个承载面（T13 复核项 5：单源化 —— 取值改由 src/core/params.h
// 提供，本文件不再自持字面量）：
//   * `kLosslessKey`（= pp::kLosslessKey，内部管道键 `__lossless`）：随工作集传递，但不序列化、
//     不显示、不出现在 values()（m1-tasks §3.4）；
//   * `kLosslessParamKey`（= pp::kLosslessParamKey，显式 schema 参数 `lossless`）：§9.1 U3 起
//     不渲染为行，由"无损"复选框独占（值随复选框同步写入工作集，编码器/预设 v2 从参数集读它）。
const std::string kLosslessKey(pp::kLosslessKey);
const std::string kLosslessParamKey(pp::kLosslessParamKey);

bool is_reserved_key(const std::string &key) { return key.rfind("__", 0) == 0; }

// "同类型" 判定（条 6）：Int=int64、Float=double（int64/double 互不容忍）、
// Bool=bool、Enum=与某个 choice 的值完全相同（类型 + 取值一起匹配）。
// R-217：与 make_editor 的构造分支同一张类型表（分发点收敛进编辑器工厂段）。
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

// R-195（M7-R2B 单源 + 本包转发）：ParamDef 查找走 pp::find_param（first-wins 单源，
// params.h 契约）—— 本文件旧自持首命中实现删除，决胜规则与 preset_io/dev_cli 同一条。

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

QString tr_c(const char *text) { return QCoreApplication::translate("pp::ui::ParamForm", text); }

// 高级区子分组（M5-T8 · 修 2-39「组内 13 项平铺无内部结构 → 按语义分子组」）。
// UX-8（M7-R2D2）：分组判据改读 **ParamDef.group 单源**（WP6 数据面；词表 = pp::kGroup*），
// R-269 点名的 key 词根启发式（progressive*/noise*/speed|budget）**删除** —— 同一参数的归组
// 只由 format_tables 显式声明决定。内省参数（heif/avif）暂无 group 声明 → 回落「编码工具」
// （= 旧启发式的"其余"档；跨面需求：enc_heif param_def_from 补 group，见 handoff）。
enum class SubGroup { Progressive, Noise, Decode, Tools };

SubGroup subgroup_of(const pp::ParamDef &p) {
    const std::string &g = p.group;
    if (g == pp::kGroupProgressive)
        return SubGroup::Progressive;
    if (g == pp::kGroupNoise)
        return SubGroup::Noise;
    if (g == pp::kGroupDecodeBudget)
        return SubGroup::Decode;
    return SubGroup::Tools; // kGroupEncodeTools / kGroupCore（核心不入折叠）/ 空（内省参数）
}

QString subgroup_title(SubGroup g) {
    switch (g) {
    case SubGroup::Progressive:
        return tr_c("渐进");
    case SubGroup::Noise:
        return tr_c("噪声");
    case SubGroup::Decode:
        return tr_c("解码预算");
    case SubGroup::Tools:
        break;
    }
    return tr_c("编码工具");
}

// 子分组先后（= 折叠行摘要里的列出顺序，与 mockup「渐进 / 噪声 / 解码预算」同序）
constexpr SubGroup kSubGroupOrder[] = {SubGroup::Progressive, SubGroup::Noise, SubGroup::Decode,
                                       SubGroup::Tools};

// R3 范围注释（10px；"1–10" / "0.0–25.0" / 魔法值档 "自动…3"）。
// 小数位数随步长：步长 < 1 → 1 位小数（视觉距离 0.0–25.0），否则整数（感光噪声 0–6400）。
QString range_note_text(const pp::ParamDef &p) {
    if (p.type != pp::ParamType::Int && p.type != pp::ParamType::Float)
        return QString();
    const bool fractional = p.step > 0.0 && p.step < 1.0;
    const auto num = [fractional](double v) {
        return fractional ? QString::number(v, 'f', 1)
                          : QString::number(static_cast<qint64>(std::llround(v)));
    };
    if (p.lo < 0.0) // 范围下端 < 0 = 编码器自选（"自动"），注释按同一语义写
        return tr_c("%1…%2").arg(tr_c("自动"), num(p.hi));
    return QStringLiteral("%1–%2").arg(num(p.lo), num(p.hi));
}

// 标签轨文字超宽 → 省略号（R2 标签轨 80px；全文进 tooltip —— 与页面口径一致）
void elide_track_label(QLabel *label) {
    if (label == nullptr)
        return;
    const QString full = label->text();
    const QFontMetrics fm(label->font());
    if (fm.horizontalAdvance(full) > formkit::kLabelW)
        label->setText(fm.elidedText(full, Qt::ElideRight, formkit::kLabelW));
}

// ---------------------------------------------------------------------------
// R-217（M7-R2D2）：**编辑器工厂** —— ParamType 分发的唯一落点。
// 此前构造（add_row）/ 回写（sync_widgets）两处平行 switch 各自分派类型；现收敛为：
//   make_editor(...)  —— 构造控件 + formkit 行 + 信号接线（唯一 switch）；
//   Editor::apply(v)  —— 工作集值 → 控件（各分支闭包生成，回写路径零 switch）；
//   same_typed_value  —— 值 ↔ 定义同类型判定（同一张类型表，见上）。
// ---------------------------------------------------------------------------
struct Editor {
    QWidget *row = nullptr;
    QLabel *label = nullptr;
    QWidget *widget = nullptr;
    std::function<void(const pp::ParamValue &)> apply; // 工作集值 → 控件（不发信号）
};

// on_change(key, value)：用户改值回调（构造期接线；程序化 apply 不触发）。
Editor
make_editor(const pp::ParamDef &p, QWidget *area, QObject *context, formkit::FieldWidth width,
            const std::function<void(const std::string &, const pp::ParamValue &)> &on_change) {
    Editor ed;
    const QString tip = QString::fromStdString(p.tooltip);
    const QString label_text = QString::fromStdString(p.label);
    switch (p.type) {
    case pp::ParamType::Int: {
        formkit::SpinSpec spec;
        spec.min = static_cast<int>(std::lround(p.lo));
        spec.max = static_cast<int>(std::lround(p.hi));
        if (spec.max < spec.min)
            std::swap(spec.min, spec.max);
        spec.value = spec.min;
        spec.auto_magic = spec.min < 0; // 2-37：魔法值（编码器自选）显示「自动」
        spec.note = range_note_text(p);
        spec.object_name = QStringLiteral("pp-param-") + QString::fromStdString(p.key);
        auto *spin = formkit::spin_box(area, spec, width);
        spin->setSingleStep(std::max(1, static_cast<int>(std::lround(p.step))));
        spin->setToolTip(tip);
        ed.widget = spin;
        ed.apply = [spin](const pp::ParamValue &v) {
            if (const int64_t *i = std::get_if<int64_t>(&v)) {
                const int64_t clamped = std::clamp<int64_t>(*i, std::numeric_limits<int>::min(),
                                                            std::numeric_limits<int>::max());
                spin->setValue(static_cast<int>(clamped));
            }
        };
        QObject::connect(spin, &QSpinBox::valueChanged, context, [on_change, key = p.key](int v) {
            on_change(key, static_cast<int64_t>(v));
        });
        const formkit::Row row = formkit::frow(area, label_text, spin, width, QString(), spec.note);
        ed.row = row.row;
        ed.label = row.label;
        break;
    }
    case pp::ParamType::Float: {
        formkit::DoubleSpinSpec spec;
        spec.min = p.lo;
        spec.max = p.hi;
        if (spec.max < spec.min)
            std::swap(spec.min, spec.max);
        spec.value = spec.min;
        spec.decimals = 3;                // 条 3：decimals=3
        spec.auto_magic = spec.min < 0.0; // 2-37：范围下端 < 0 = 编码器自选 → 「自动」
        spec.note = range_note_text(p);
        spec.object_name = QStringLiteral("pp-param-") + QString::fromStdString(p.key);
        auto *spin = formkit::dspin_box(area, spec, width);
        spin->setSingleStep(p.step > 0.0 ? p.step : 1.0);
        spin->setToolTip(tip);
        ed.widget = spin;
        ed.apply = [spin](const pp::ParamValue &v) {
            if (const double *d = std::get_if<double>(&v))
                spin->setValue(*d);
        };
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, context,
                         [on_change, key = p.key](double v) { on_change(key, v); });
        const formkit::Row row = formkit::frow(area, label_text, spin, width, QString(), spec.note);
        ed.row = row.row;
        ed.label = row.label;
        break;
    }
    case pp::ParamType::Bool: {
        // 2-34/2-35：布尔行 = 「勾选框→标签」紧凑同行（标签轨留空，网格不错位）
        const formkit::CheckRow row =
            formkit::check_row(area, QString(), label_text,
                               QStringLiteral("pp-param-") + QString::fromStdString(p.key));
        row.box->setToolTip(tip);
        ed.row = row.row;
        ed.label = row.label;
        ed.widget = row.box;
        ed.apply = [box = row.box](const pp::ParamValue &v) {
            if (const bool *b = std::get_if<bool>(&v))
                box->setChecked(*b);
        };
        QObject::connect(row.box, &formkit::CheckBox::toggled, context,
                         [on_change, key = p.key](bool on) { on_change(key, on); });
        break;
    }
    case pp::ParamType::Enum: {
        formkit::ComboSpec spec;
        spec.object_name = QStringLiteral("pp-param-") + QString::fromStdString(p.key);
        spec.accessible = label_text;
        spec.tooltip = tip;
        auto *combo = formkit::combo_box(area, spec);
        for (const auto &choice : p.choices) {
            // 修 2-37：枚举的 "auto" 档同义显示为「自动」（显示面；值仍走 choices 的原值）
            const QString name =
                choice.first == "auto" ? tr_c("自动") : QString::fromStdString(choice.first);
            combo->addItem(name);
        }
        ed.widget = combo;
        ed.apply = [combo, def = &p](const pp::ParamValue &v) {
            int idx = -1;
            for (std::size_t i = 0; i < def->choices.size(); ++i) {
                if (def->choices[i].second == v) {
                    idx = static_cast<int>(i);
                    break;
                }
            }
            combo->setCurrentIndex(idx);
        };
        const pp::ParamDef *def = &p;
        QObject::connect(combo, &QComboBox::currentIndexChanged, context,
                         [on_change, key = p.key, def](int idx) {
                             if (idx < 0 || idx >= static_cast<int>(def->choices.size()))
                                 return;
                             on_change(key, def->choices[static_cast<std::size_t>(idx)].second);
                         });
        const formkit::Row row = formkit::frow(area, label_text, combo, width);
        ed.row = row.row;
        ed.label = row.label;
        break;
    }
    }
    return ed;
}

// UX-8：折叠头**整行可点**（点击行内任意处展开/收起）——事件转发到折叠钮；钮自身事件不转发
// （防一次点击双触发）。只挂在"纯展示 + 一枚折叠钮"的行上（参数行不挂）。
class FoldRowClick : public QObject {
public:
    FoldRowClick(QWidget *row, QToolButton *toggle) : QObject(row), toggle_(toggle) {
        if (row == nullptr)
            return;
        row->installEventFilter(this);
        const QList<QWidget *> kids = row->findChildren<QWidget *>();
        for (QWidget *kid : kids) {
            if (kid != toggle_)
                kid->installEventFilter(this);
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched != toggle_ && event->type() == QEvent::MouseButtonRelease &&
            toggle_ != nullptr) {
            toggle_->click();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QToolButton *toggle_ = nullptr;
};

} // namespace

struct ParamForm::Impl {
    ParamForm *q = nullptr;
    const pp::FormatDef *fmt = nullptr;
    std::vector<pp::BackendDef> backends; // 生效后端表（静态表拷贝或运行时内省结果）
    FormSelection sel;                    // 已解析（非空）的当前选择
    pp::ParamSet values;                  // 工作集（含保留键 "__lossless"）
    std::string pre_lossless_tech;        // 本表单自动切走前的技术（取消无损时切回）
    pp::ParamSet pre_lossless_values;     // 开无损前的用户值（取消时恢复，避免 444 残留）
    // R-009 同族（M7-WP4）：构造期 tokens 取当前生效档（原硬编码 Dark）
    theme::Tokens tokens = theme::tokens(theme::preferred_theme_mode());

    QVBoxLayout *root = nullptr;
    // 选择行（M5-T8：一律 formkit 行栅格 —— 后端行 / 技术行 / 无损行）
    QComboBox *backend_combo = nullptr; // 多后端格式（heif/avif）才可见
    QWidget *backend_row = nullptr;
    formkit::Row tech_row; // [技术 80][12][combo 160][注：依赖说明]
    QComboBox *tech_combo = nullptr;
    formkit::CheckRow lossless_row; // [空标签轨][12][13×13 勾选框 → "无损编码"]
    formkit::CheckBox *lossless_check = nullptr;

    QWidget *core_host = nullptr; // 核心参数行容器（pitch 28）
    QVBoxLayout *core_box = nullptr;
    formkit::Row adv_row; // 高级参数折叠行：[高级参数][12][摘要 …… chevron + N 项]（UX-8 整行可点）
    QToolButton *adv_toggle = nullptr;
    QWidget *adv_host = nullptr; // 折叠内容（默认隐藏）
    QWidget *adv_body = nullptr;
    QVBoxLayout *adv_box = nullptr;
    QLineEdit *adv_search = nullptr;
    QFrame *adv_rule = nullptr; // 折叠区顶缘 hairline（替代组框：无双层框线）
    // UX-8：子分组**二级折叠**（渐进/噪声/解码预算/编码工具）——(SubGroup 序号, 折叠钮)
    QList<QPair<int, QToolButton *>> subgroup_headers;
    QList<QPair<int, QLabel *>> subgroup_counts;          // 组内「N 项」标签（随谓词/过滤动态刷新）
    formkit::ElidedLabel *adv_empty = nullptr;            // 搜索无匹配时的空态说明
    bool group_expanded[4] = {true, false, false, false}; // 默认只展开第一组（kSubGroupOrder 序）
    // UX-10.1：枚举限幅（key → 禁用原因；空 = 无限幅）—— 由 limit_enum_choices 写入
    std::map<std::string, QString> enum_limits;
    QLabel *cross_error = nullptr; // §2.7：交叉参数约束红字区（参数组底部；不折行）

    struct Row {
        const pp::ParamDef *def = nullptr; // 指向生效 TechDef 的 ParamDef（生命周期 >= 本控件）
        std::string key;
        QWidget *row = nullptr;  // 行容器（formkit 行，固定高 28）
        QLabel *label = nullptr; // 标签轨（80 右对齐；布尔行留空）
        QWidget *editor = nullptr;
        std::function<void(const pp::ParamValue &)> apply; // R-217：工作集值 → 控件
        bool advanced = false;
        SubGroup subgroup = SubGroup::Tools;
        bool locked = false;
        bool eligible = true; // 谓词可见 且 通过高级区搜索过滤（**不含**子分组折叠态）
        bool shown = true;    // eligible + 所在子分组已展开（is_param_visible 口径）
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
        root->setSpacing(0); // R1：行 pitch 由行容器自身承担，行间无额外 gap
        build_cross_error();
        build_selectors();

        sel.backend = backends.empty() ? std::string() : backends.front().id;
        const pp::TechDef *t = tech();
        sel.tech = t ? t->id : std::string();
        sel.lossless = false;
        populate_tech_combo();
        rebuild_params(); // 工作集 = 表内默认（缺失则整表为空 —— 见 M5-T8 修复记录）
        refresh();
        attach_card_menu(); // UX-1 ⑧：格式参数卡右键
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

    // ---------------------------------------------------------------- 选择行

    void build_selectors() {
        // 后端行（仅多后端格式：heif/avif 内省出多个后端；单后端不占位）
        if (backends.size() > 1) {
            formkit::ComboSpec spec;
            spec.accessible = ParamForm::tr("后端");
            auto *combo = formkit::combo_box(q, spec);
            backend_combo = combo;
            for (const pp::BackendDef &b : backends)
                combo->addItem(QString::fromStdString(b.label.empty() ? b.id : b.label),
                               QString::fromStdString(b.id));
            const formkit::Row row =
                formkit::frow(q, ParamForm::tr("后端"), combo, formkit::FieldWidth::Standard);
            row.row->setObjectName(QStringLiteral("pp-backend-row"));
            backend_row = row.row;
            root->addWidget(row.row);
            QObject::connect(combo, &QComboBox::currentIndexChanged, q, [this](int idx) {
                if (syncing || idx < 0 || idx >= static_cast<int>(backends.size()))
                    return;
                user_set_backend(backends[static_cast<std::size_t>(idx)].id);
            });
        }

        // 技术行（[技术][combo 160][注：无损开启时的依赖说明]）
        formkit::ComboSpec tech_spec;
        tech_spec.object_name = QStringLiteral("pp-tech-combo");
        tech_spec.accessible = ParamForm::tr("技术");
        tech_spec.tooltip =
            ParamForm::tr("编码技术（各技术参数不同）；勾选「无损编码」后仅无损技术可选");
        tech_combo = formkit::combo_box(q, tech_spec);
        {
            const formkit::Row row =
                formkit::frow(q, ParamForm::tr("技术"), tech_combo, formkit::FieldWidth::Standard,
                              QString(), formkit::kNotePlaceholder);
            row.row->setObjectName(QStringLiteral("pp-tech-row"));
            tech_row = row;
            root->addWidget(row.row);
        }
        QObject::connect(tech_combo, &QComboBox::currentIndexChanged, q, [this](int idx) {
            if (syncing || idx < 0)
                return;
            const pp::BackendDef *b = backend();
            if (!b || idx >= static_cast<int>(b->techs.size()))
                return;
            user_set_tech(b->techs[static_cast<std::size_t>(idx)].id);
        });

        // 无损行（表单布尔 = 勾选框；语序固定「勾选框→标签」，2-35）
        {
            const formkit::CheckRow row = formkit::check_row(
                q, QString(), ParamForm::tr("无损编码"), QStringLiteral("pp-lossless-check"));
            row.row->setObjectName(QStringLiteral("pp-lossless-row"));
            lossless_row = row;
            lossless_check = row.box;
            // T13 复核项 5：tooltip 同时点明两个承载面（显式 schema 参数 + 内部管道键），
            // 不再只说保留键 —— 用户面看到的是"无损输出"这一件事，落盘/下发/编码各自由两键承担。
            lossless_check->setToolTip(
                ParamForm::tr(
                    "无损输出（显式 schema 参数 lossless：可落盘进预设、可校验；同时同步内部管道键 "
                    "__lossless 供参数谓词与编码器读取）；开启后视觉距离按 0 "
                    "处理（忽略视觉距离）") +
                ((fmt->id == "heif" || fmt->id == "avif")
                     ? ParamForm::tr("。本格式无损强制 4:4:4（4:2:0/4:2:2 会抽样色度，不是"
                                     "无损）；HEVC/AV1 的无损是 YCbCr 平面无损，RGB↔YCbCr 往返"
                                     "有舍入（不是逐位）；HEIF 超过 16 MP 会自动用标准 grid 分块"
                                     "以避开 libde265 的单图解码上限")
                     : QString()));
            root->addWidget(row.row);
        }
        QObject::connect(lossless_check, &formkit::CheckBox::toggled, q, [this](bool on) {
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
    // R-318（M7-R2.3-G8）：置灰条目补**条目级原因 tooltip**（「不静默灰」口径）——悬停即见
    // 「为何不可选」；恢复可选时清空，不留陈旧原因。
    void update_tech_items_enabled() {
        auto *model = qobject_cast<QStandardItemModel *>(tech_combo->model());
        const pp::BackendDef *b = backend();
        if (!model || !b)
            return;
        for (int i = 0; i < static_cast<int>(b->techs.size()) && i < model->rowCount(); ++i) {
            QStandardItem *item = model->item(i);
            if (!item)
                continue;
            const bool enabled =
                !sel.lossless || b->techs[static_cast<std::size_t>(i)].lossless_capable;
            item->setEnabled(enabled);
            item->setToolTip(enabled
                                 ? QString()
                                 : ParamForm::tr("已开启无损输出：该技术不支持无损编码，不可选"));
        }
    }

    // 选择行状态同步（可见性 + 依赖说明）：无损开启 → 技术行出"仅无损技术可选"注释（修 2-20）
    void update_selectors() {
        syncing = true;
        const pp::BackendDef *b = backend();
        const bool multi_backend = backends.size() > 1;
        if (backend_combo != nullptr && backend_row != nullptr) {
            backend_combo->setVisible(multi_backend);
            backend_row->setVisible(multi_backend);
            for (int i = 0; i < backend_combo->count(); ++i) {
                if (backend_combo->itemData(i).toString() == QString::fromStdString(sel.backend)) {
                    backend_combo->setCurrentIndex(i);
                    break;
                }
            }
        }
        const bool multi_tech = b && b->techs.size() > 1;
        tech_combo->setVisible(multi_tech);
        tech_row.row->setVisible(multi_tech);
        if (tech_row.note != nullptr)
            tech_row.note->setText(multi_tech && sel.lossless ? ParamForm::tr("仅无损技术可选")
                                                              : QString());
        for (int i = 0; i < tech_combo->count(); ++i) {
            if (tech_combo->itemData(i).toString() == QString::fromStdString(sel.tech)) {
                tech_combo->setCurrentIndex(i);
                break;
            }
        }
        update_tech_items_enabled();
        // 注意：isVisible() 会被祖先的可见性污染（构造期整页未 show → 恒 false）→ 用语义判据
        bool has_lossless = first_lossless_tech_in(b) != nullptr;
        // TIFF/PNG/BMP 的压缩方案恒无损且不声明 lossless 参数：勾选框恒无效 →
        // 不再显示"未勾选"的假控件（TIFF-06/PNG-08/BMP-06）。
        if (has_lossless && !declares_lossless(*fmt, b->id, sel.tech) &&
            (fmt->id == "tiff" || fmt->id == "png" || fmt->id == "bmp"))
            has_lossless = false;
        // AVIF + SVT-AV1 无无损通路：勾选框必须不可选（否则“可点但必失败”）。
        if (has_lossless && fmt->id == "avif" && sel.backend == "svt-av1") {
            has_lossless = false;
            sel.lossless = false;
        }
        lossless_check->setVisible(has_lossless);
        lossless_check->setChecked(sel.lossless);
        lossless_row.row->setVisible(has_lossless);
        syncing = false;
    }

    // ---------------------------------------------------------------- 折叠行 / 行构建

    void set_fold_icon(QToolButton *button, bool expanded) {
        if (button == nullptr)
            return;
        icons::apply(button, expanded ? icons::Name::ChevronDown : icons::Name::ChevronRight,
                     tokens.text2, tokens.text3, 12);
    }

    int count_advanced() const {
        int total = 0;
        for (const Row &r : rows)
            if (r.advanced && r.eligible)
                ++total;
        return total;
    }

    // 高级区摘要（折叠头左侧 hint 行；mockup 同序）—— UX-8 起折叠头右侧只显「N 项」计数 + chevron。
    // 只统计 **当前谓词/搜索过滤后仍可选** 的行：JXL 无损下 vardct-only 行全隐，若仍按表内
    // 总数报「12 项」，用户点开只看到 3 行，会被当成"选项显示不全"。
    QString advanced_summary() const {
        if (count_advanced() == 0)
            return QString();
        QStringList names;
        for (const SubGroup g : kSubGroupOrder) {
            const bool any = std::any_of(rows.begin(), rows.end(), [g](const Row &r) {
                return r.advanced && r.eligible && r.subgroup == g;
            });
            if (any)
                names << subgroup_title(g);
        }
        // 单组时组栏就在下面一行，摘要再写一次「编码工具」是重复标签（深走查 065/066/072/073）。
        return names.size() > 1 ? names.join(QStringLiteral(" / ")) : QString();
    }

    int count_group(SubGroup g) const {
        int n = 0;
        for (const Row &r : rows)
            if (r.advanced && r.subgroup == g)
                ++n;
        return n;
    }

    // 把折叠区内某一行滚入视野（外层无 QScrollArea 时 no-op；延迟到本轮布局完成）。
    void ensure_in_scroll(QWidget *target) {
        if (target == nullptr)
            return;
        QWidget *parent = q->parentWidget();
        while (parent != nullptr && qobject_cast<QScrollArea *>(parent) == nullptr)
            parent = parent->parentWidget();
        if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
            QTimer::singleShot(0, q, [scroll, target] {
                if (target != nullptr)
                    scroll->ensureWidgetVisible(target, 0, 12);
            });
        }
    }

    void build_rows() {
        rows.clear();
        subgroup_headers.clear();
        subgroup_counts.clear();
        adv_empty = nullptr; // 随 adv_host 一起 delete
        delete core_host;
        core_host = nullptr;
        core_box = nullptr;
        if (adv_row.row != nullptr) {
            root->removeWidget(adv_row.row);
            delete adv_row.row;
        }
        adv_row = formkit::Row{};
        adv_toggle = nullptr;
        delete adv_host;
        adv_host = nullptr;
        adv_body = nullptr;
        adv_box = nullptr;
        adv_search = nullptr;
        adv_rule = nullptr;

        // ---- 核心参数区（一行一义；formkit 行 × n）----
        core_host = new QWidget(q);
        core_host->setObjectName(QStringLiteral("pp-core-area"));
        core_box = new QVBoxLayout(core_host);
        core_box->setContentsMargins(0, 0, 0, 0);
        core_box->setSpacing(0);
        root->addWidget(core_host);

        // ---- 高级参数折叠行（UX-8 重设计：折叠头**整行可点**；左侧组名摘要、
        //      右侧「N 项」计数 + chevron；无 QGroupBox → 无双层框线）----
        adv_toggle = new QToolButton(q);
        adv_toggle->setObjectName(QStringLiteral("pp-advanced-toggle"));
        adv_toggle->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkFold));
        adv_toggle->setCheckable(true);
        adv_toggle->setChecked(false); // 起始折叠（§7.2「高级参数(展开)」）
        adv_toggle->setAutoRaise(true);
        adv_toggle->setCursor(Qt::PointingHandCursor);
        adv_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        adv_toggle->setFixedHeight(theme::Metrics::btn_h); // R4 按钮高 26
        adv_toggle->setFont(theme::font(theme::Typography::body_px, QFont::DemiBold));
        adv_toggle->setToolTip(ParamForm::tr("展开或收起高级参数（低频编码器选项）"));
        set_fold_icon(adv_toggle, false);
        {
            auto *host = new QWidget(q);
            auto *hl = new QHBoxLayout(host);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(theme::Metrics::group_gap);
            auto *summary = new formkit::ElidedLabel(host);
            summary->setObjectName(QStringLiteral("pp-advanced-summary"));
            summary->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkHint));
            summary->setFont(theme::font(theme::Typography::hint_px));
            hl->addWidget(summary, 1);
            hl->addWidget(adv_toggle, 0);
            const formkit::Row row =
                formkit::frow(q, ParamForm::tr("高级参数"), host, formkit::FieldWidth::Flex);
            row.row->setObjectName(QStringLiteral("pp-advanced-row"));
            adv_row = row;
            root->addWidget(row.row);
            new FoldRowClick(row.row, adv_toggle); // UX-8：整行可点（点击任意处展开/收起）
        }

        // ---- 折叠内容（UX-8：**废除卡内嵌滚动** —— 原 224px 硬限 + QScrollArea 内滚已退役，
        //      高级区高度 = 内容自然高，滚动只剩整页一层，§8 统一原则）----
        adv_host = new QWidget(q);
        adv_host->setObjectName(QStringLiteral("pp-advanced-area"));
        auto *hv = new QVBoxLayout(adv_host);
        hv->setContentsMargins(0, 0, 0, 0);
        hv->setSpacing(0);
        adv_rule = new QFrame(adv_host);
        adv_rule->setObjectName(QStringLiteral("pp-adv-rule"));
        adv_rule->setFixedHeight(1);
        hv->addWidget(adv_rule);
        // 搜索行（带标签 + 用途说明；省略号统一 "…" —— 修 2-40/2-41；与参数行同轴 → 2-32）
        {
            adv_search = new QLineEdit(adv_host);
            adv_search->setObjectName(QStringLiteral("pp-advanced-search"));
            adv_search->setPlaceholderText(ParamForm::tr("在高级参数中搜索…"));
            adv_search->setClearButtonEnabled(true);
            adv_search->setToolTip(ParamForm::tr("按参数名或键过滤高级参数（大小写不敏感）"));
            const formkit::Row row = formkit::frow(adv_host, ParamForm::tr("搜索"), adv_search,
                                                   formkit::FieldWidth::Flex);
            row.row->setObjectName(QStringLiteral("pp-advanced-search-row"));
            hv->addWidget(row.row);
            QObject::connect(adv_search, &QLineEdit::textChanged, q, [this](const QString &) {
                if (syncing)
                    return;
                update_row_states(); // 过滤只影响视图，不是值变化 → 不发信号
            });
        }
        adv_body = new QWidget(adv_host);
        adv_body->setObjectName(QStringLiteral("pp-advanced-body"));
        adv_box = new QVBoxLayout(adv_body);
        adv_box->setContentsMargins(0, 0, 0, 0);
        adv_box->setSpacing(0);
        hv->addWidget(adv_body);
        // 搜索无匹配时的空态（旧实现只剩空白；且高级头被整体隐藏，连收起入口都消失）。
        adv_empty = new formkit::ElidedLabel(adv_host);
        adv_empty->setObjectName(QStringLiteral("pp-advanced-empty"));
        adv_empty->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkHint));
        adv_empty->setFont(theme::font(theme::Typography::hint_px));
        adv_empty->setMinimumHeight(formkit::kRowPitch);
        adv_empty->set_full_text(ParamForm::tr("没有匹配的高级参数：清空搜索框可查看全部"));
        set_leading_indent(adv_empty, formkit::kLabelW + formkit::kColGap);
        adv_empty->setVisible(false);
        hv->addWidget(adv_empty);

        if (const pp::TechDef *t = tech()) {
            // UX-8：子分组**二级折叠**（渐进 / 噪声 / 解码预算 / 编码工具），分组判据 =
            // ParamDef.group 单源；默认只展开第一组（重建 = 新参数集，展开态随之复位）。
            bool first_group = true;
            for (const SubGroup g : kSubGroupOrder) {
                std::vector<const pp::ParamDef *> in_group;
                for (const pp::ParamDef &p : t->params) {
                    if (p.advanced && p.key != kLosslessParamKey && subgroup_of(p) == g)
                        in_group.push_back(&p);
                }
                if (in_group.empty())
                    continue;
                const int gi = static_cast<int>(g);
                group_expanded[gi] = first_group;
                first_group = false;
                // 组折叠头：[chevron + 组名] …… [N 项]（整行可点；12px 缩进 = 内容缩进，
                // 行本体仍走 formkit 全局轴 → 组内外同轴，修 2-32）
                auto *header = new QWidget(adv_body);
                header->setObjectName(QStringLiteral("pp-advanced-group-") + QString::number(gi));
                header->setContentsMargins(formkit::kColGap, 0, 0, 0);
                header->setFixedHeight(formkit::kRowPitch);
                auto *hh = new QHBoxLayout(header);
                hh->setContentsMargins(0, 0, 0, 0);
                hh->setSpacing(formkit::kInlineGap);
                auto *toggle = new QToolButton(header);
                toggle->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkFold));
                toggle->setCheckable(true);
                toggle->setChecked(group_expanded[gi]);
                toggle->setAutoRaise(true);
                toggle->setCursor(Qt::PointingHandCursor);
                toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                toggle->setFixedHeight(theme::Metrics::btn_h);
                toggle->setFont(theme::font(theme::Typography::hint_px, QFont::Bold));
                toggle->setText(subgroup_title(g));
                toggle->setToolTip(ParamForm::tr("展开或收起该子分组"));
                set_fold_icon(toggle, group_expanded[gi]);
                hh->addWidget(toggle);
                hh->addStretch(1);
                auto *count = new QLabel(ParamForm::tr("%1 项").arg(int(in_group.size())), header);
                count->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkNote));
                count->setFont(theme::font(theme::Typography::hint_px));
                hh->addWidget(count);
                subgroup_headers.append({gi, toggle});
                subgroup_counts.append({gi, count});
                adv_box->addWidget(header);
                new FoldRowClick(header, toggle); // 组折叠头同样整行可点
                QObject::connect(toggle, &QToolButton::toggled, q, [this, gi, toggle](bool on) {
                    if (gi >= 0 && gi < 4)
                        group_expanded[gi] = on;
                    set_fold_icon(toggle, on);
                    update_row_states();
                    if (!on)
                        return;
                    // 子组展开后把该组**最后一行**滚入视野：8 行的编码工具组一次全露出来，
                    // 而不是只露前 4 行（多组全开时底部行不会无声落在视口外）。
                    QWidget *last = nullptr;
                    for (const Row &r : rows)
                        if (r.advanced && static_cast<int>(r.subgroup) == gi && r.shown)
                            last = r.row;
                    ensure_in_scroll(last);
                });
                for (const pp::ParamDef *p : in_group)
                    add_row(*p);
            }
            // 核心参数（表内顺序）
            for (const pp::ParamDef &p : t->params) {
                if (p.advanced || p.key == kLosslessParamKey)
                    continue; // §9.1：lossless 由"无损"复选框独占
                add_row(p);
            }
        }
        // UX-8：折叠头右侧「N 项」计数（advanced-fold 探针锚 = 文案含「项」）+ 左侧组名摘要
        adv_toggle->setText(ParamForm::tr("%1 项").arg(count_advanced()));
        if (adv_row.row != nullptr) {
            // findChild 走 QLabel（ElidedLabel 无 Q_OBJECT，静态下转）
            if (auto *summary =
                    adv_row.row->findChild<QLabel *>(QStringLiteral("pp-advanced-summary")))
                static_cast<formkit::ElidedLabel *>(summary)->set_full_text(advanced_summary());
        }
        adv_row.row->setVisible(count_advanced() > 0);
        adv_toggle->setEnabled(count_advanced() > 0);

        root->addWidget(adv_host);
        adv_host->setVisible(false);
        QObject::connect(adv_toggle, &QToolButton::toggled, adv_host, &QWidget::setVisible);
        QObject::connect(adv_toggle, &QToolButton::toggled, q, [this](bool on) {
            set_fold_icon(adv_toggle, on);
            adv_toggle->setToolTip(on ? ParamForm::tr("收起高级参数")
                                      : ParamForm::tr("展开或收起高级参数（低频编码器选项）"));
            if (!on)
                return;
            // 折叠区在参数卡底部，展开后内容往往落在视口外；滚到折叠区起点。
            ensure_in_scroll(adv_host);
        });
        // §2.7：跨格式/技术重建后，红字区恒为参数组最后一行
        root->removeWidget(cross_error);
        root->addWidget(cross_error);
    }

    void add_row(const pp::ParamDef &p) {
        const QString tip = QString::fromStdString(p.tooltip);
        QWidget *area =
            p.advanced ? static_cast<QWidget *>(adv_body) : static_cast<QWidget *>(core_host);
        QVBoxLayout *box = p.advanced ? adv_box : core_box;
        // UX-8（§8-3）：折叠区数值行宽与核心区一致（160）——「折叠 = 88」第二档废除
        // （ui_smoke `spin-group` 断言随 M7-R2-A 换锚 kFieldShort→kFieldStd，见 handoff
        // 裁定登记）。
        const formkit::FieldWidth width = formkit::FieldWidth::Standard;

        Row r;
        r.def = &p;
        r.key = p.key;
        r.advanced = p.advanced;
        r.subgroup = subgroup_of(p);

        // R-217：类型分发唯一落点 = make_editor（构造 + 行 + 接线 + 回写闭包）
        Editor ed =
            make_editor(p, area, q, width, [this](const std::string &key, const pp::ParamValue &v) {
                if (syncing)
                    return;
                values[key] = v;
                refresh();
                emit q->changed();
            });
        r.row = ed.row;
        r.label = ed.label;
        r.editor = ed.widget;
        r.apply = std::move(ed.apply);

        r.row->setObjectName(QStringLiteral("pp-param-row-") + QString::fromStdString(p.key));
        elide_track_label(r.label);
        if (r.label != nullptr) {
            r.label->setObjectName(QStringLiteral("pp-label-") + QString::fromStdString(p.key));
            r.label->setToolTip(tip);
            r.label->setContextMenuPolicy(Qt::CustomContextMenu);
            // 条 5：标签右键菜单 → 重置为默认
            QObject::connect(r.label, &QWidget::customContextMenuRequested, q,
                             [this, label = r.label, key = r.key](const QPoint &pos) {
                                 QMenu menu;
                                 QAction *reset = menu.addAction(ParamForm::tr("重置为默认"));
                                 if (menu.exec(label->mapToGlobal(pos)) == reset)
                                     reset_to_default(key);
                             });
        }
        box->addWidget(r.row);
        rows.push_back(r);
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
                if (p.has_default) // AVIF-01：可选覆盖参数不写默认值
                    s[p.key] = p.def;
        return s;
    }

    // 技术/后端变化 → 重建工作集（默认值 + 同 key 同类型沿用旧值）与行控件
    // M7-R2.4（格式卡「关无损后距离变 0」）：old_tech 提供**旧技术**的锁定谓词 —— 旧值若
    // 来自锁定（如 JXL 无损把 distance 强制为 0.0），切换后不得当成用户选择沿用。
    // 旧实现无条件沿用同 key 同类型值：关掉无损 → 技术回 VarDCT，distance 仍是 0.0 →
    // 编码器报 "VarDCT distance=0 is ambiguous"，页面上看着"恢复默认"实际已不可编码。
    // M7-R2.5（AVIF 自动后端切换串参数）：carry_values=false 用于**跨后端**重建 ——
    // SVT-AV1 与 libaom 的同名键（speed/threads/tune…）默认值与值域都不同，按 key 沿用
    // 会把 SVT 的 speed=12 带进 libaom（值域 0–9），UI 显示被钳到 9、工作集里仍是 12，
    // 既造成「已改 3」假计数，也可能把越界值下发给编码器。
    void rebuild_params(const pp::TechDef *old_tech = nullptr, bool carry_values = true) {
        const pp::ParamSet old = values;
        values = make_defaults();
        if (carry_values) {
            if (const pp::TechDef *t = tech()) {
                for (const auto &[k, v] : old) {
                    if (is_reserved_key(k))
                        continue;
                    const pp::ParamDef *p = find_param(*t, k);
                    if (!p || !same_typed_value(*p, v))
                        continue;
                    if (old_tech != nullptr) {
                        const pp::ParamDef *op = find_param(*old_tech, k);
                        if (op != nullptr && pp::eval_lock(*op, old).has_value())
                            continue; // 旧值是锁定强制值，不是用户选择 → 回新表默认
                    }
                    values[k] = v;
                }
            }
        }
        values[kLosslessKey] = sel.lossless;
        build_rows();
    }

    // 条 4：谓词重算 —— apply_locks → 强制值写回 → 控件同步 → 可见性/改值标记
    void refresh() {
        values[kLosslessKey] = sel.lossless;
        // §9.1：名为 "lossless" 的参数没有行，其值由"无损"复选框独占（heif/avif 内省参数）
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
    // §9.3：行内不折行；全文同时进悬浮提示（超长行按宽度硬裁，不换行、不撑宽布局）。
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
        cross_error->setToolTip(text);
        cross_error->setVisible(!msgs.empty());
    }

    // R-318：禁用态必须给原因（"不静默灰"口径全量推广）——tooltip 组装单点：
    // 基础 tooltip（ParamDef）+ 锁定前缀（eval_lock 强制）+ 枚举限幅原因（UX-10.1）。
    QString tooltip_for(const Row &r) const {
        QString tip = QString::fromStdString(r.def->tooltip);
        if (r.locked)
            tip = ParamForm::tr("【已禁用：被同组参数约束锁定】") + tip;
        const auto it = enum_limits.find(r.key);
        if (it != enum_limits.end())
            tip = tip + QLatin1Char('\n') + it->second;
        return tip;
    }

    void sync_widgets() {
        syncing = true;
        for (Row &r : rows) {
            const auto it = values.find(r.key);
            if (it != values.end() && r.apply)
                r.apply(it->second); // R-217：回写走工厂闭包（零第二处 switch）
            r.editor->setEnabled(!r.locked);
            if (r.editor != nullptr)
                r.editor->setToolTip(tooltip_for(r));
        }
        syncing = false;
    }

    void update_row_states() {
        const QString filter = adv_search ? adv_search->text() : QString();
        const bool filter_active = !filter.isEmpty();
        int advanced_total = 0; // 谓词可见的高级行（**不含**搜索过滤）—— 决定折叠头是否保留
        std::map<int, int> group_visible;
        for (Row &r : rows) {
            const bool predicate_visible = pp::eval_visible(*r.def, values); // 条 4
            bool eligible = predicate_visible;
            if (eligible && r.advanced && filter_active) {
                const QString label = QString::fromStdString(r.def->label);
                const QString key = QString::fromStdString(r.key);
                eligible = label.contains(filter, Qt::CaseInsensitive) ||
                           key.contains(filter, Qt::CaseInsensitive); // 条 3：包含过滤
            }
            r.eligible = eligible;
            if (r.advanced && predicate_visible)
                ++advanced_total;
            if (r.advanced && eligible)
                ++group_visible[static_cast<int>(r.subgroup)];
            // 折叠只决定"行是否显示"，不参与"该子分组是否存在可见项"的判定 —— 否则收起
            // 一个子分组会把它的折叠头也隐藏掉，用户再也点不开（原实现即此病灶）。
            bool shown = eligible;
            if (shown && r.advanced && !filter_active) {
                const int gi = static_cast<int>(r.subgroup);
                shown = gi >= 0 && gi < 4 && group_expanded[gi];
            }
            r.shown = shown;
            if (r.row != nullptr)
                r.row->setVisible(shown);

            const auto it = values.find(r.key);
            const bool deviates = it != values.end() && it->second.index() == r.def->def.index() &&
                                  !(it->second == r.def->def); // 条 5：同类型比较
            // 条 5 / 裁定 A8：改值标记 = 强调边框 + 强调色字（theme::set_mod），无 `●` 字形。
            // 布尔行（自绘勾选框不吃 QSS，且无值文本可着色）与锁定项不打标记：该格式的改值
            // 总量由格式参数卡头的「已改 N」中性胶囊承担（§7.2/1-24）。
            if (r.def->type != pp::ParamType::Bool && r.editor != nullptr)
                theme::set_mod(r.editor, deviates && !r.locked);
        }
        // UX-8：子分组折叠头 —— 本组无**可选**行（谓词/搜索过滤后）→ 连头隐藏（过滤后不留
        // 空标题）；折叠态不再影响折叠头的可见性（否则收起后无法再展开，见上）。
        for (const auto &[group, toggle] : subgroup_headers) {
            const bool any = std::any_of(rows.begin(), rows.end(), [group](const Row &r) {
                return r.advanced && r.eligible && static_cast<int>(r.subgroup) == group;
            });
            if (toggle == nullptr || toggle->parentWidget() == nullptr)
                continue;
            toggle->parentWidget()->setVisible(any);
            const bool open = (group >= 0 && group < 4 && group_expanded[group]) || filter_active;
            if (toggle->isChecked() != open) {
                const QSignalBlocker blocker(toggle);
                toggle->setChecked(open);
            }
            set_fold_icon(toggle, open);
        }
        // 组内「N 项」跟随谓词/过滤刷新（旧实现建组时写死 → 头「3 项」/组「4 项」自相矛盾）。
        for (const auto &[group, label] : subgroup_counts) {
            if (label == nullptr)
                continue;
            label->setText(ParamForm::tr("%1 项").arg(group_visible[group]));
        }
        // 折叠头计数/摘要随谓词/搜索过滤实时更新（只报"点开真能看到"的行数）。
        if (adv_toggle != nullptr) {
            const int n = count_advanced();
            adv_toggle->setText(ParamForm::tr("%1 项").arg(n));
            if (n == 0 && !filter_active) {
                // 当前技术/无损档下没有任何可见高级参数：折叠头与空折叠区一起收掉
                // （旧实现留下「高级参数 0 项」的空折叠面，点了也什么都没有）。
                if (adv_toggle->isChecked()) {
                    const QSignalBlocker blocker(adv_toggle);
                    adv_toggle->setChecked(false);
                }
                adv_toggle->setVisible(false);
                set_fold_icon(adv_toggle, false);
                if (adv_host != nullptr)
                    adv_host->setVisible(false);
                if (adv_row.row != nullptr)
                    adv_row.row->setVisible(false);
                if (adv_empty != nullptr)
                    adv_empty->setVisible(false);
            } else {
                // M7-R2.5：折叠头**不因搜索 0 命中而消失** —— 它承载收起/再展开入口；
                // 只要还有谓词可见的高级行（或当前已展开）就保留，计数写 0。
                adv_toggle->setVisible(true);
                adv_toggle->setEnabled(advanced_total > 0 || adv_toggle->isChecked());
                if (adv_row.row != nullptr) {
                    adv_row.row->setVisible(advanced_total > 0 || adv_toggle->isChecked());
                    if (auto *summary =
                            adv_row.row->findChild<QLabel *>(QStringLiteral("pp-advanced-summary")))
                        static_cast<formkit::ElidedLabel *>(summary)->set_full_text(
                            advanced_summary());
                }
                if (adv_empty != nullptr)
                    adv_empty->setVisible(filter_active && n == 0);
            }
        }
    }

    // ---------------------------------------------------------------- UX-10.1 枚举限幅
    // 档位 → 色度限幅（docs/ux-review-14.md §10.1-2）：把枚举行可选项收窄到 allowed；
    // 空 = 清除限幅。只动视图（置灰 + 禁用原因），不改工作集、不发信号 —— 不一致由调用方按
    // heif_profile_compatible 报中文错误（一控件一义，不静默改写）。
    void apply_enum_limits(const std::string &key, const std::vector<int64_t> &allowed,
                           const QString &reason) {
        if (allowed.empty())
            enum_limits.erase(key);
        else
            enum_limits[key] = reason;
        for (Row &r : rows) {
            if (r.key != key || r.def->type != pp::ParamType::Enum || r.editor == nullptr)
                continue;
            auto *combo = qobject_cast<QComboBox *>(r.editor);
            if (combo == nullptr)
                continue;
            for (int i = 0; i < combo->count() && i < static_cast<int>(r.def->choices.size());
                 ++i) {
                const pp::ParamValue &choice = r.def->choices[static_cast<std::size_t>(i)].second;
                bool ok = true;
                if (!allowed.empty()) {
                    const int64_t *v = std::get_if<int64_t>(&choice);
                    ok = v != nullptr &&
                         std::find(allowed.begin(), allowed.end(), *v) != allowed.end();
                }
                if (auto *model = qobject_cast<QStandardItemModel *>(combo->model())) {
                    if (QStandardItem *item = model->item(i))
                        item->setEnabled(ok);
                }
                combo->setItemData(i, ok ? QVariant() : reason, Qt::ToolTipRole);
            }
            r.editor->setToolTip(tooltip_for(r));
        }
    }

    // 字符串枚举（chroma 等）：按 choices 的显示名/字符串值匹配。整数版对 string 选择
    // 永远匹配不到（std::get_if<int64_t> 为空），会把整列都置灰 —— HEIF/AVIF 的
    // 档位→色度联动与无损→4:4:4 联动都走本重载。
    void apply_enum_limits_text(const std::string &key, const std::vector<std::string> &allowed,
                                const QString &reason) {
        if (allowed.empty())
            enum_limits.erase(key);
        else
            enum_limits[key] = reason;
        for (Row &r : rows) {
            if (r.key != key || r.def->type != pp::ParamType::Enum || r.editor == nullptr)
                continue;
            auto *combo = qobject_cast<QComboBox *>(r.editor);
            if (combo == nullptr)
                continue;
            for (int i = 0; i < combo->count() && i < static_cast<int>(r.def->choices.size());
                 ++i) {
                const auto &choice = r.def->choices[static_cast<std::size_t>(i)];
                bool ok = allowed.empty();
                if (!ok && std::find(allowed.begin(), allowed.end(), choice.first) != allowed.end())
                    ok = true;
                if (!ok) {
                    if (const std::string *sv = std::get_if<std::string>(&choice.second))
                        ok = std::find(allowed.begin(), allowed.end(), *sv) != allowed.end();
                }
                if (auto *model = qobject_cast<QStandardItemModel *>(combo->model())) {
                    if (QStandardItem *item = model->item(i))
                        item->setEnabled(ok);
                }
                combo->setItemData(i, ok ? QVariant() : reason, Qt::ToolTipRole);
            }
            r.editor->setToolTip(tooltip_for(r));
        }
    }

    // actions 单源 = contextmenu::param_card_actions（docs/ux-review-14.md §1 表第 ⑧ 行）。
    // 运行中置灰由调用方按 lock_for_run 语义给（这里以自身 enable 态折算；整卡在运行期被
    // lock_for_run 连页禁用时菜单本就不可达）。
    void reset_all_to_default() {
        if (const pp::TechDef *t = tech()) {
            for (const pp::ParamDef &p : t->params) {
                if (is_reserved_key(p.key))
                    continue;
                if (p.has_default)
                    values[p.key] = p.def;
                else
                    values.erase(p.key); // 可选覆盖参数恢复为“未设置”
            }
        }
        refresh();
        emit q->changed();
    }

    static QString value_text(const pp::ParamValue &v) {
        if (const bool *b = std::get_if<bool>(&v))
            return *b ? QStringLiteral("true") : QStringLiteral("false");
        if (const int64_t *i = std::get_if<int64_t>(&v))
            return QString::number(static_cast<qint64>(*i));
        if (const double *d = std::get_if<double>(&v))
            return QString::number(*d, 'g', 12);
        if (const std::string *s = std::get_if<std::string>(&v))
            return QString::fromStdString(*s);
        return QString();
    }

    void copy_params_to_clipboard() {
        QStringList lines;
        for (const auto &[key, value] : public_values())
            lines << QString::fromStdString(key) + QLatin1Char('=') + value_text(value);
        if (QClipboard *clip = QApplication::clipboard())
            clip->setText(lines.join(QLatin1Char('\n')));
    }

    void paste_params_from_clipboard() {
        QClipboard *clip = QApplication::clipboard();
        if (clip == nullptr)
            return;
        const QStringList lines = clip->text().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        pp::ParamSet incoming;
        const pp::TechDef *t = tech();
        if (t == nullptr)
            return;
        for (const QString &line : lines) {
            const qsizetype eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            const std::string key = line.left(eq).trimmed().toStdString();
            const QString text = line.mid(eq + 1).trimmed();
            const pp::ParamDef *p = find_param(*t, key);
            if (p == nullptr)
                continue; // 未知 key 忽略（与 set_values 同口径）
            pp::ParamValue v;
            switch (p->type) {
            case pp::ParamType::Bool:
                v = (text == QLatin1String("true") || text == QLatin1String("1"));
                break;
            case pp::ParamType::Int:
                v = static_cast<int64_t>(text.toLongLong());
                break;
            case pp::ParamType::Float:
                v = text.toDouble();
                break;
            case pp::ParamType::Enum: {
                // 与 choices 的值同型匹配（优先数值/布尔文本，其余按字符串值）
                bool matched = false;
                for (const auto &choice : p->choices) {
                    if (value_text(choice.second) == text) {
                        v = choice.second;
                        matched = true;
                        break;
                    }
                }
                if (!matched)
                    continue;
                break;
            }
            }
            if (same_typed_value(*p, v))
                incoming[key] = v;
        }
        if (incoming.empty())
            return;
        set_values(
            incoming); // 同 key 同类型才应用；不发信号语义同 set_values（用户动作在下一行补发）
        emit q->changed();
    }

    void expand_all_advanced() {
        if (adv_toggle != nullptr && !adv_toggle->isChecked())
            adv_toggle->setChecked(true);
        for (int i = 0; i < 4; ++i)
            group_expanded[i] = true;
        update_row_states();
    }

    void attach_card_menu() {
        q->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(q, &QWidget::customContextMenuRequested, q, [this](const QPoint &pos) {
            contextmenu::ParamCardIntents in;
            in.reset_defaults = [this] { reset_all_to_default(); };
            in.copy_params = [this] { copy_params_to_clipboard(); };
            in.paste_params = [this] { paste_params_from_clipboard(); };
            in.expand_advanced = [this] { expand_all_advanced(); };
            in.running = !q->isEnabled();
            QMenu *m = contextmenu::menu(q, contextmenu::param_card_actions(in));
            m->setAttribute(Qt::WA_DeleteOnClose);
            m->popup(q->mapToGlobal(pos));
        });
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
        if (p->has_default)
            values[key] = p->def;
        else
            values.erase(key); // 可选覆盖参数恢复为“未设置”
        refresh();
        emit q->changed();
    }

    // ---------------------------------------------------------------- 选择变化

    void apply_state(const std::string &backend_id, const std::string &tech_id, bool lossless) {
        const bool backend_changed = (backend_id != sel.backend);
        const bool rebuild = backend_changed || (tech_id != sel.tech);
        const pp::TechDef *old_tech = tech(); // 必须在改 sel 前取（锁定谓词按旧技术求值）
        sel.backend = backend_id;
        sel.tech = tech_id;
        sel.lossless = lossless;
        if (rebuild) {
            populate_tech_combo();
            // 跨后端：同名键的值域/语义不同，禁止沿用旧值；仅同后端换技术时沿用非锁定值。
            rebuild_params(old_tech, /*carry_values=*/!backend_changed);
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
        pre_lossless_values.clear();
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
        pre_lossless_values.clear();
        apply_state(sel.backend, id, sel.lossless);
        emit q->selection_changed(sel);
        emit q->changed();
    }

    void user_set_lossless(bool on) {
        if (on == sel.lossless)
            return;
        std::string tech_id = sel.tech;
        std::string memo = pre_lossless_tech;
        bool restore_values = false;
        if (on) {
            // 先记开无损前的用户值：HEIF/AVIF 的运行时技术自带 lossless_capable，
            // 不触发换技术，但 chroma 仍会被强制 444；关无损时必须恢复。
            pre_lossless_values = values;
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
            restore_values = !pre_lossless_values.empty();
        } else {
            memo.clear();
            restore_values = !pre_lossless_values.empty();
        }
        pre_lossless_tech = memo;
        apply_state(sel.backend, tech_id, on);
        if (restore_values) {
            set_values(pre_lossless_values); // HEIF 的强制 chroma=444 等不应残留
            pre_lossless_values.clear();
        }
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
        if (backend_id != sel.backend) {
            pre_lossless_tech.clear();
            pre_lossless_values.clear();
        }
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

    // ---------------------------------------------------------------- 主题

    void set_tokens(const theme::Tokens &t) {
        tokens = t;
        // R-064（M7-WP4）：自绘件重放收口 theme::replay_tokens 单点 —— 原逐件遍历只覆盖
        // 勾选框、漏 formkit Combo/Spin/DSpin 的箭头（主题切换后箭头残留旧主题色）。
        theme::replay_tokens(q, t);
        set_fold_icon(adv_toggle, adv_toggle != nullptr && adv_toggle->isChecked());
        for (const auto &[group, toggle] : subgroup_headers) {
            Q_UNUSED(group);
            if (toggle != nullptr)
                set_fold_icon(toggle, toggle->isChecked());
        }
        if (adv_rule != nullptr)
            adv_rule->setStyleSheet(QStringLiteral("background: ") + theme::css_color(t.line) +
                                    QStringLiteral("; border: none;"));
        if (adv_search != nullptr)
            adv_search->setStyleSheet(
                QStringLiteral("background: ") + theme::css_color(t.control) +
                QStringLiteral("; border: 1px solid ") + theme::css_color(t.control_bd) +
                QStringLiteral("; border-radius: ") +
                QString::number(theme::Metrics::control_radius) + QStringLiteral("px; color: ") +
                theme::css_color(t.text) + QStringLiteral("; padding: 0 8px; min-height: ") +
                QString::number(theme::Metrics::input_h - 2) + QStringLiteral("px;"));
        if (cross_error != nullptr)
            cross_error->setStyleSheet(QStringLiteral("color: ") + theme::css_color(t.err) +
                                       QStringLiteral(";"));
    }

    void build_cross_error() {
        cross_error = new QLabel(q);
        cross_error->setObjectName(QStringLiteral("pp-cross-error"));
        cross_error->setTextFormat(Qt::PlainText);
        cross_error->setWordWrap(false);
        cross_error->setVisible(false);
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

void ParamForm::set_tokens(const theme::Tokens &tokens) { impl_->set_tokens(tokens); }

void ParamForm::limit_enum_choices(const std::string &key, const std::vector<int64_t> &allowed,
                                   const QString &disabled_reason) {
    impl_->apply_enum_limits(key, allowed, disabled_reason);
}

void ParamForm::limit_enum_choices_text(const std::string &key,
                                        const std::vector<std::string> &allowed,
                                        const QString &disabled_reason) {
    impl_->apply_enum_limits_text(key, allowed, disabled_reason);
}

} // namespace pp::ui
