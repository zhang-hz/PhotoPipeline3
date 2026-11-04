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
// （用户手动改过后端 → user_backend_override）；预设落值后同样求值【M7-R2D2 修订（R-066）：
// 预设落值路径**跳过**预选（in_preset_apply 语义位）—— 预设记录的后端为权威值，不再静默改写
// svt-av1→libaom（裁定登记 build/m7r2-d2/handoff.md）】；select_format() 仍是冒烟/走查钩子。
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
#include <cmath>
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
#include <QFileInfo>
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
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStorageInfo>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "codecs/encoders.h"
#include "codecs/heif_profile.h" // UX-10.1：编码档位纯函数单源（WP2 I4）
#include "core/adaptive.h"       // M10-W3-T2：自适应策略解析/校验单源（只读摘要；C24 无编辑器）
#include "core/error_text.h"     // R-070/273：错误中文映射单源（M7-R2B；本包接线）
#include "core/format_preset.h" // M12：kFormatPresetDefaultName / is_default_format_preset_name（default 单源）
#include "core/fsops.h"
#include "core/params.h"
#include "ui/byte_text.h"              // R-254（M7-R2.1-F3）：字节自然语读数单源（原 human_bytes）
#include "ui/contextmenu.h"            // UX-1 ⑥⑦⑨：公共右键菜单件（actions 单源）
#include "ui/format_policy.h"          // R-191/B.3：位深交集/后端归一/avif 谓词纯核单源
#include "ui/format_selection_model.h" // R-189（M7-R2.1-F3）：「已选格式」唯一真相
#include "ui/formkit.h"                // C2：行构造器（R1–R4）+ 自绘控件（磁贴/开关/胶囊）
#include "ui/icons.h"                  // M5-T3：折叠插入符 /「添加格式…」的 ＋ 一律矢量图标（C3）
#include "ui/leading_indent.h"         // R-218（M7-R2E2）：逻辑边距（对轨缩进）单源
#include "ui/paramform.h"
#include "ui/theme.h"

// M8-B2：预检拒绝时切回输出页（`开始运行` 拒绝 → 聚焦第一错行 = 先让页可见再聚焦）。
// 方向说明：页面 → 主窗的唯一反向引用只用于"切页"，不读写主窗状态（加性，登记 A20）。
#include "ui/mainwindow.h"

#include "ui/page_output_impl.h" // R-189/B.3：Impl 共享私有头（TU 拆分）

namespace pp::ui {

namespace {

// —— 格式预设下拉的**可读性**（需求 3「长名/DSL 可读」的 GUI 面；Lead 口径）——
// 下拉固定 160px（= 同页参数行字段宽）⇒ 长预设名会被 Qt elide。两件套兜底：
//   ① 弹层按最长项自适应宽（实现单源 = paramform 的 combo_popup_autosize，与参数枚举下拉
//      同一份实现 —— 不在这里重写同构代码）；
//   ② combo 自身 tooltip = 当前预设**全名**（M12：无「未选择」占位项 ⇒ 无选中项 = 清空），
//      悬浮可见被省略的全文。
QString current_preset_full_name(const FormatState *st) {
    if (st == nullptr || st->preset_combo == nullptr)
        return {};
    const int index = st->preset_combo->currentIndex();
    return index >= 0 ? st->preset_combo->itemText(index) : QString();
}

// 0.4.2 / 需求 3（A13）：大预设来源的**如实**显示（替换 M12 的「接管（detached）」措辞）。
//   * 占位文本 = combo 无选中时的真实来源（不再假装高亮 default）；
//   * 注记 = 「值来自大预设「<名>」（基于格式预设 <stem> / 自定义参数）」。
QString global_source_placeholder(const QString &global_name) {
    return global_name.isEmpty() ? PageOutput::tr("来自大预设")
                                 : PageOutput::tr("来自大预设：%1").arg(global_name);
}

QString global_source_note(const QString &global_name, const QString &based_on) {
    QString text = global_name.isEmpty() ? PageOutput::tr("值来自大预设")
                                         : PageOutput::tr("值来自大预设「%1」").arg(global_name);
    text += based_on.isEmpty() ? PageOutput::tr("（自定义参数）")
                               : PageOutput::tr("（基于格式预设 %1）").arg(based_on);
    return text;
}

void sync_format_preset_combo_view(FormatState *st) {
    if (st == nullptr || st->preset_combo == nullptr)
        return;
    combo_popup_autosize(st->preset_combo);
    // 悬停全文：绑定态 = 预设名；大预设来源 = 来源说明（无选中项可显示）。
    st->preset_combo->setToolTip(
        st->preset_detached ? global_source_note(st->preset_global_name, st->preset_based_on)
                            : current_preset_full_name(st));
}

// M12 需求 4：下拉项的「default」判定单源 = core/format_preset.h 的
// is_default_format_preset_name（ASCII 大小写不敏感；Windows 文件系统大小写不敏感）。
// 用**文件名 stem** 判定而不是显示名：显示名可能带「（损坏）」后缀、JSON name 也可能与文件名不同。
QString preset_path_stem(const QString &path) {
    return path.isEmpty() ? QString() : QFileInfo(path).completeBaseName();
}

bool is_default_preset_path(const QString &path) {
    return !path.isEmpty() &&
           pp::is_default_format_preset_name(preset_path_stem(path).toStdString());
}

// 列表里 default 项的下标（无则 -1）。选中规则单源（set_format_preset_list /
// set_current_format_preset / detached 三处共用）。
int default_preset_item_index(const QComboBox *combo) {
    if (combo == nullptr)
        return -1;
    for (int i = 0; i < combo->count(); ++i) {
        if (is_default_preset_path(combo->itemData(i, Qt::UserRole).toString()))
            return i;
    }
    return -1;
}

// M10-W3-T2 会话态弹层：预设卡下拉条目的「基准显示名」数据角色（0.4.2 / 需求 3 加性）。
// 条目文本可能带「（已修改）」后缀；基准名单独存 role，避免往返拼接/剥离产生第二份真源。
constexpr int kPresetBaseNameRole = Qt::UserRole + 1;
const QString &dirty_suffix() {
    static const QString kDirtySuffix = PageOutput::tr("（已修改）");
    return kDirtySuffix;
}

// M12 需求 3 根因修复（实测：工厂 HEIF「Apple自适应」预设 x265:crf=20 → 控件显示 28）：
//   预设 JSON 里 `"x265:crf": 20` 是**整数字面量**；HEIF/AVIF 是**运行时内省**格式（静态表
//   `techs` 为空）⇒ IO 层找不到 ParamDef，按 JSON 类型推断保存成 int64；而
//   `ParamForm::set_values` 只接受与 ParamDef **同类型**的值（Float 参数收到 int64 会被
//   跳过）⇒ 控件停在内省默认值（x265 CRF 默认 28），载入的 20 被静默丢弃。
// 修复 = 落卡前按**生效后端**（`effective_backends` 含运行时内省）的参数表把数值归一为
// ParamDef 期望类型（int64↔double 仅在数值精确可表示时互转）；单点覆盖「格式预设载入」与
// 「全局预设载入」两条写卡路径（人工编辑的预设 JSON 也不再受字面量写法影响）。
void coerce_numeric_param_types(const pp::TechDef &tech, pp::ParamSet &params) {
    constexpr double kMaxExactInt = 9007199254740992.0; // 2^53（精确可表示整数上限）
    for (auto &[key, value] : params) {
        const pp::ParamDef *def = pp::find_param(tech, key);
        if (def == nullptr)
            continue; // 保留键 / 跨技术键 / 插件透传键：不归一（由校验链负责）
        if (def->type == pp::ParamType::Float) {
            if (const int64_t *i = std::get_if<int64_t>(&value))
                value = static_cast<double>(*i);
        } else if (def->type == pp::ParamType::Int) {
            if (const double *d = std::get_if<double>(&value)) {
                if (std::isfinite(*d) && *d == std::floor(*d) && std::abs(*d) <= kMaxExactInt)
                    value = static_cast<int64_t>(*d);
            }
        }
    }
}

} // namespace

FormatState *OutputPageImpl::state_of(const std::string &id) {
    const auto it = states.find(id);
    return it == states.end() ? nullptr : &it->second;
}

const FormatState *OutputPageImpl::state_of(const std::string &id) const {
    const auto it = states.find(id);
    return it == states.end() ? nullptr : &it->second;
}

bool OutputPageImpl::is_selected(const std::string &id) const { return sel.contains(id); }

std::string OutputPageImpl::backend_id(const std::string &id) const {
    const FormatState *st = state_of(id);
    return (st != nullptr && st->form != nullptr) ? st->form->selection().backend : std::string();
}

bool OutputPageImpl::lossless_of(const std::string &id) const {
    const FormatState *st = state_of(id);
    return (st != nullptr && st->form != nullptr) ? st->form->selection().lossless : false;
}

int OutputPageImpl::bitdepth_of(const std::string &id) const {
    const FormatState *st = state_of(id);
    return (st != nullptr && st->bitdepth != nullptr) ? st->bitdepth->currentData().toInt() : 0;
}

pp::ColorTarget OutputPageImpl::color_target() const {
    // R-190：值 = itemData（枚举本体）；无选中/无控件 → 构造默认「保持原样」
    if (color_combo == nullptr || color_combo->currentIndex() < 0)
        return pp::ColorTarget::KeepOriginal;
    return static_cast<pp::ColorTarget>(color_combo->currentData().toInt());
}

pp::ConflictPolicy OutputPageImpl::conflict() const {
    // R-190：值 = itemData（枚举本体）；无选中/无控件 → 构造默认「自动加序号」
    if (conflict_combo == nullptr || conflict_combo->currentIndex() < 0)
        return pp::ConflictPolicy::Rename;
    return static_cast<pp::ConflictPolicy>(conflict_combo->currentData().toInt());
}

void OutputPageImpl::toggle_format(const std::string &id, bool on, bool user) {
    if (pp::find_format(id) == nullptr)
        return;
    // R-189：集合/现役变更只走 FormatSelection（含 R-266 游离槽语义，逐字等价）；
    // no-op 早退同原「on == was」。投影统一在末尾 reproject_format_selection()。
    const bool closing_active = (!on && id == sel.active());
    if (!sel.toggle(id, on))
        return;
    if (on) {
        build_tab(id);
        // R-133：format_changed 已随孤儿信号删除（config_changed 在本函数末尾统一发射）
        (void)user;
    } else {
        snapshot_state(id);
        remove_tab(id);
        // M7R23G8-1（修 R-189 语义漂移，M7R22G1-6）：remove_tab → QTabWidget::removeTab 发出的
        // currentChanged 会把现役写成页签滑动邻接位（该 handler 的 set_active 是末写者），覆盖
        // FormatSelection::toggle 的 R-266「现役前移首位」。此处按契约重申 front（同
        // apply_preset 的既有末写者序；751f479 旧序语义归位，模型真值表 test_smoke 不动）。
        if (closing_active && !sel.empty())
            sel.set_active(sel.items().front());
    }
    reproject_format_selection();
    update_metadata_only_state();
    update_template_example();
    update_estimate();
    emit q->config_changed();
}

void OutputPageImpl::reproject_format_selection() {
    sync_tiles();
    reconcile_tab_order();
    sync_tab_current();
    update_format_count();
    refresh_tab_labels();
    refresh_changed_pill();
}

int OutputPageImpl::tile_index(const std::string &id) {
    for (int i = 0; i < kFormatCount; ++i)
        if (id == kFormats[i].id)
            return i;
    return kFormatCount;
}

void OutputPageImpl::sync_tiles() {
    for (int i = 0; i < kFormatCount; ++i) {
        const QSignalBlocker blocker(tiles[i]);
        tiles[i]->setChecked(is_selected(kFormats[i].id));
    }
}

void OutputPageImpl::sync_tab_current() {
    const QSignalBlocker blocker(tabs);
    const QString active = QString::fromStdString(sel.active());
    for (int i = 0; i < tabs->count(); ++i) {
        const QWidget *page = tabs->widget(i);
        if (page != nullptr && page->property(kPageFormatIdProperty).toString() == active) {
            tabs->setCurrentIndex(i);
            break;
        }
    }
    update_form_object_names();
}

void OutputPageImpl::reconcile_tab_order() {
    if (tabs == nullptr)
        return;
    const QSignalBlocker blocker(tabs);
    for (int want = 0; want < static_cast<int>(sel.size()); ++want) {
        FormatState *st = state_of(sel.at(want));
        if (st == nullptr || st->page == nullptr)
            continue;
        const int have = tabs->indexOf(st->page);
        if (have < 0 || have == want)
            continue;
        // removeTab 会把页面暂时从 QStackedWidget 摘出；insertTab 立即放回。信号已屏蔽，
        // 现役索引由紧随其后的 sync_tab_current() 统一恢复。
        const QString label = tabs->tabText(have);
        tabs->removeTab(have);
        tabs->insertTab(want, st->page, label);
    }
}

void OutputPageImpl::update_add_format_enabled() {
    if (add_format_button == nullptr)
        return;
    const bool room = sel.size() < static_cast<std::size_t>(kFormatCount);
    const bool usable = room && !metadata_only;
    add_format_button->setEnabled(usable);
    add_format_button->setToolTip(
        !room ? PageOutput::tr("8 个格式已全部添加")
              : (metadata_only ? PageOutput::tr("仅元数据模式：输出格式多选已禁用")
                               : PageOutput::tr("添加格式…（从尚未选中的格式中选择）")));
}

void OutputPageImpl::update_form_object_names() {
    for (const auto &[id, st] : states) {
        if (st.form == nullptr)
            continue;
        st.form->setObjectName(id == sel.active()
                                   ? QStringLiteral("pp-param-form")
                                   : QStringLiteral("pp-param-form-") + QString::fromStdString(id));
    }
}

void OutputPageImpl::build_tab(const std::string &id) {
    const pp::FormatDef *f = pp::find_format(id);
    if (f == nullptr)
        return;
    FormatState &st = states[id];
    if (st.page != nullptr)
        return; // 已存在（取消勾选后重新勾选 → 复用快照重建）
    st.page = new QWidget(tabs);
    st.page->setObjectName(QStringLiteral("pp-format-page-") + QString::fromStdString(id));
    // M7-R2.4：页签标题/现役/页序一律按页面自身 id 解析（不再依赖页签下标与 sel 同序）
    st.page->setProperty(kPageFormatIdProperty, QString::fromStdString(id));
    auto *v = new QVBoxLayout(st.page);
    // 页签内容与卡片其余行同轴（卡内边距 12；R1 行 pitch 连续）
    v->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                          theme::Metrics::card_pad_bottom);
    v->setSpacing(0);

    // 行 −1（M10-W3-T2 / C23 §8.5）：**格式预设行**（每格式页签顶部一行；在「编码档位/位深」
    // 行之上 —— 共享卡头是跨格式的，不能承载格式级下拉）。行内含只读自适应面（C24）。
    build_format_preset_row(id, v);
    // 列表请求在**本函数末尾**发出（M12）：页签建立即请 MainWindow 喂列表，并在「还没有已载入
    // 来源」时静默载入该格式的 default 预设 —— 自动载入必须等 ParamForm/位深/档位等全部
    // 子控件建好（apply_format_preset_to 对 form==nullptr 会静默跳过，早发会让「来源已载入」
    // 与卡值不一致）。页签激活时另一方（tabs currentChanged）会再发一次，多格式不串档。

    // 行 0（仅 HEIF/AVIF）：编码档位（Profile）—— UX-10.1 核心行，置于位深行**之上**
    // （档位是因、位深是果）。标签带兼容性说明见 heif_profile_options 的选项文案。
    if (is_heif_family(id)) {
        formkit::ComboSpec prof_spec;
        prof_spec.object_name = QStringLiteral("pp-profile-combo");
        prof_spec.accessible = PageOutput::tr("编码档位");
        st.profile = formkit::combo_box(st.page, prof_spec);
        const bool avif = (id == "avif");
        // 选项表 / 文案 / 可用性 = 单源（page_output_impl.h 定顺序，codecs/heif_profile.h 定
        // 标签与 tooltip）。W2-T2（C18）：追加 16..21 六个 still 变体；Main10Still 实测不可用
        // → **置灰 + 原因**（不静默隐藏，用户要能看到为什么选不了）。
        for (const ProfileOption &opt : heif_profile_options(avif)) {
            st.profile->addItem(profile_option_label(avif, opt.profile),
                                static_cast<int>(opt.profile));
            const int index = st.profile->count() - 1;
            const QString disabled_reason = profile_option_disabled_reason(avif, opt.profile);
            QString tip = profile_option_tooltip(avif, opt.profile);
            if (!disabled_reason.isEmpty()) {
                tip = PageOutput::tr("【不可用】%1\n%2").arg(disabled_reason, tip);
                if (auto *model = qobject_cast<QStandardItemModel *>(st.profile->model())) {
                    if (QStandardItem *item = model->item(index))
                        item->setEnabled(false);
                }
            }
            st.profile->setItemData(index, tip, Qt::ToolTipRole);
        }
        // C19（§6.2.3）：档位下拉的弹出层按最长项自适应宽度（受屏宽 80% 约束）+ item 尾省略
        // + tooltip 全文。实现单源 = src/ui/paramform.cpp 的 pp::ui::combo_popup_autosize。
        combo_popup_autosize(st.profile);
        const formkit::Row row =
            formkit::frow(st.page, PageOutput::tr("编码档位"), st.profile, FieldWidth::Standard,
                          QString(), formkit::kNotePlaceholder);
        row.row->setObjectName(QStringLiteral("pp-profile-row"));
        st.profile_note = row.note;
        st.profile_note->setObjectName(QStringLiteral("pp-profile-note-") +
                                       QString::fromStdString(id));
        st.profile_note->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        st.profile_note->setText(QString());
        v->addWidget(row.row);
        QObject::connect(st.profile, &QComboBox::currentIndexChanged, q,
                         [this, id](int) { on_profile_changed(id); });
    }

    // 行 1：位深（格式能力约束：如 JPEG 锁 8 位；约束说明入 10px 注释位）
    {
        formkit::ComboSpec bit_spec;
        bit_spec.object_name = QStringLiteral("pp-bitdepth-combo");
        bit_spec.accessible = PageOutput::tr("位深");
        // R-211（M7-R2D2）：位深单位标签消费**单源** pp::bitdepth_unit_label（"bps"/"bpp"，
        // UI 侧不再按格式自翻单位）。控件文案仍为「N 位」（bitdepth-lock 探针逐字钉住），
        // 单位语义进 tooltip（R-070 口径：实现细节进 tooltip）。
        bit_spec.tooltip =
            PageOutput::tr("输出位深（单位：%1 = %2）")
                .arg(QString::fromStdString(std::string(pp::bitdepth_unit_label(id))))
                .arg(pp::bitdepth_unit(id) == pp::BitdepthUnit::BitsPerPixel
                         ? PageOutput::tr("每像素位数")
                         : PageOutput::tr("每采样位数"));
        st.bitdepth = formkit::combo_box(st.page, bit_spec);
        // 行内 10px 注释位 = 格式能力约束说明（R3：提示不占标签位；占位符 " " 保证注释位存在）
        const formkit::Row row =
            formkit::frow(st.page, PageOutput::tr("位深"), st.bitdepth, FieldWidth::Standard,
                          QString(), formkit::kNotePlaceholder);
        row.row->setObjectName(QStringLiteral("pp-bitdepth-row"));
        st.bitdepth_note = row.note;
        st.bitdepth_note->setObjectName(QStringLiteral("pp-bitdepth-note-") +
                                        QString::fromStdString(id));
        st.bitdepth_note->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        st.bitdepth_note->setText(QString());
        v->addWidget(row.row);
    }

    // 格式级元数据承载能力（如实显示）：BMP 容器没有 EXIF/XMP 槽位（design.md §5.8），
    // 编码后元数据必然丢弃。此前只在运行结果的 warning 里出现（"成功后才发现"），
    // 这里在选择 BMP 页签时就把事实写出来，且给出不可绕过的原因。
    if (id == "bmp") {
        st.meta_hint = new ElidedLabel(st.page);
        st.meta_hint->setObjectName(QStringLiteral("pp-meta-hint-bmp"));
        st.meta_hint->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkWarn));
        st.meta_hint->setFont(theme::font(theme::Typography::hint_px));
        st.meta_hint->setMinimumHeight(formkit::kRowPitch);
        set_leading_indent(st.meta_hint, formkit::kLabelW + formkit::kColGap);
        st.meta_hint->set_full_text(
            PageOutput::tr("BMP 容器不承载 EXIF/XMP：转码后元数据丢弃（仅保留像素）"));
        st.meta_hint->setToolTip(
            PageOutput::tr("BMP 没有 EXIF/XMP 容器（Windows 位图规范只有 V5 头的 ICC 槽，且本"
                           "写入器不落 ICC）。需要元数据请改用 PNG/TIFF/WebP/JPEG/JXL/HEIF/AVIF；"
                           "「仅元数据」模式也不支持 BMP。"));
        v->addWidget(st.meta_hint);
    }

    // 行 2..6：技术 / 无损 / 核心参数 / 高级参数（ParamForm 一行一义）
    st.form = new ParamForm(*f, effective_backends(*f), st.page);
    st.form->setObjectName(QStringLiteral("pp-param-form-") + QString::fromStdString(id));
    st.form->set_tokens(tokens);
    v->addWidget(st.form);

    // M8-B2：运行前预检错误清单（每个格式 tab 底部；规则引擎同源，错误非空才显示）。
    // 布局：与卡内行同轴，逐行中文说明 + 定位提示；`开始运行` 拒绝时聚焦第一错行。
    {
        st.rule_error_box = new QWidget(st.page);
        st.rule_error_box->setObjectName(QStringLiteral("pp-rule-error-box-") +
                                         QString::fromStdString(id));
        auto *ev = new QVBoxLayout(st.rule_error_box);
        ev->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x, 0);
        ev->setSpacing(0);
        st.rule_error_list = new QLabel(st.rule_error_box);
        st.rule_error_list->setObjectName(QStringLiteral("pp-rule-error-list-") +
                                          QString::fromStdString(id));
        st.rule_error_list->setWordWrap(true);
        st.rule_error_list->setTextFormat(Qt::PlainText);
        st.rule_error_list->setProperty("ppInvalid", true); // theme::field_qss 的红字轨（err）
        st.rule_error_list->setFont(theme::font(theme::Typography::hint_px));
        ev->addWidget(st.rule_error_list);
        st.rule_error_box->setVisible(false);
        v->addWidget(st.rule_error_box);
    }

    // avif 10bit+alpha 预选提示（原 pp-alpha-hint 语义，位置随格式页签）
    st.alpha_hint = new ElidedLabel(st.page);
    st.alpha_hint->setObjectName(QStringLiteral("pp-alpha-hint"));
    st.alpha_hint->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkHint));
    st.alpha_hint->setFont(theme::font(theme::Typography::hint_px));
    st.alpha_hint->setMinimumHeight(formkit::kRowPitch);
    set_leading_indent(st.alpha_hint, formkit::kLabelW + formkit::kColGap); // R-218：逻辑边距
    st.alpha_hint->set_full_text(
        PageOutput::tr("10 位 + 含 alpha：已选择 libaom 后端（SVT-AV1 不支持该组合）"));
    st.alpha_hint->setVisible(false);
    v->addWidget(st.alpha_hint);

    QObject::connect(st.form, &ParamForm::selection_changed, q,
                     [this, id](const FormSelection &) { on_param_selection_changed(id); });
    QObject::connect(st.form, &ParamForm::changed, q, [this, id] {
        update_rule_errors(id);  // M8-B2：联动规则实时态 → 预检错误清单/tab 徽标同步
        update_profile_note(id); // UX-10.1 反向联动：改色度 → 档位行推导/校验重算
        refresh_changed_pill();
        refresh_tab_labels();
        emit q->config_changed();
    });
    QObject::connect(st.bitdepth, &QComboBox::currentIndexChanged, q,
                     [this, id](int) { on_bitdepth_changed(id); });

    // 初值：优先快照（取消勾选后重选），否则表内默认
    apply_state_to_form(id, st.spec);
    // M8-B2：预检上下文（位深）+ 首轮错误清单
    sync_rule_context(id);
    update_rule_errors(id);
    // M7-R2.4：**按规范序插入**（不是 addTab 追加）。sel 的顺序 = 磁贴序，页签必须同序，
    // 否则先选 JPEG XL 再勾 JPEG 时页签是 [JXL, JPEG] 而 sel 是 [JPEG, JXL] —— 标题
    // 按 sel 写、内容按插入序摆，立刻出现"JPEG 标题下是 JXL 参数"。
    int insert_at = static_cast<int>(sel.size()) - 1;
    for (int i = 0; i < static_cast<int>(sel.size()); ++i) {
        if (sel.at(i) == id) {
            insert_at = i;
            break;
        }
    }
    tabs->insertTab(insert_at, st.page, format_tab_label(id));
    apply_tab_width(); // UX-9：等宽
    sync_tab_current();
    // M12：页签（含全部子控件）建好后才请列表 —— MainWindow 可能据此**自动载入 default**，
    // 载入链需要 st.form/st.bitdepth/st.profile 已就位（见函数头部注释）。
    emit q->format_preset_list_requested(QString::fromStdString(id));
}

void OutputPageImpl::remove_tab(const std::string &id) {
    FormatState *st = state_of(id);
    if (st == nullptr || st->page == nullptr)
        return;
    // R-229：删页签前**先快照**当前控件状态 —— 否则重勾选时还原的是几个会话前的陈旧配置
    // （states[id].spec 停留在上次 snapshot_state 的那一刻）。
    snapshot_state(id);
    const int index = tabs->indexOf(st->page);
    if (index >= 0)
        tabs->removeTab(index); // R-241：页面本体**会**被删（见下三行 deleteLater）；留下的
                                // 是 states[id].spec 这份参数快照（重选可复用）—— 原注释
                                // "不删除页面"与实现相反，已纠偏。
    st->page->hide();
    st->page->setParent(nullptr);
    st->page->deleteLater();
    st->page = nullptr;
    st->form = nullptr;
    st->bitdepth = nullptr;
    st->bitdepth_note = nullptr;
    st->alpha_hint = nullptr;
    st->meta_hint = nullptr;
    st->profile = nullptr; // UX-10.1
    st->profile_note = nullptr;
    // M8-B2：预检错误清单随页签一起销毁（错误状态由下次建页签重算）
    st->rule_error_box = nullptr;
    st->rule_error_list = nullptr;
    st->rule_errors.clear();
    // M10-W3-T2：格式预设行的控件随页签销毁（**会话态字段保留**：format_preset_path /
    // adaptive_policy / adaptive_enabled / adaptive_parse_ok —— 勾掉再勾回仍记得来源与策略）
    st->preset_combo = nullptr;
    st->preset_save = nullptr;
    st->preset_saveas = nullptr;
    st->preset_delete = nullptr;
    st->preset_import = nullptr;
    st->preset_export = nullptr;
    st->preset_note = nullptr;
    st->adaptive_row = nullptr;
    st->adaptive_switch = nullptr;
    st->adaptive_summary = nullptr;
    apply_tab_width(); // UX-9：等宽
}

void OutputPageImpl::snapshot_state(const std::string &id) {
    FormatState *st = state_of(id);
    if (st == nullptr || st->form == nullptr)
        return;
    st->spec.format_id = id;
    const FormSelection fsel = st->form->selection();
    st->spec.backend_id = fsel.backend;
    st->spec.tech_id = fsel.tech;
    st->spec.params = st->extra; // R-135：先回填表单不认的键，再叠 form values（同名后者胜）
    for (const auto &[k, v] : st->form->values())
        st->spec.params[k] = v;
    st->spec.params[std::string(pp::kLosslessKey)] = fsel.lossless;
    st->spec.out_bitdepth = bitdepth_of(id);
    // UX-10.1：编码档位随快照落盘（保留键 __heif_profile = HeifProfile 序号；
    // 缺省 Auto = 0。enc_heif 编码前用同一规则源反查，见 codecs/heif_profile.h）
    if (st->profile != nullptr)
        st->spec.params[std::string(pp::kHeifProfileParamKey)] =
            static_cast<int64_t>(st->profile->currentData().toInt());
}

QString OutputPageImpl::format_tab_label(const std::string &id) const {
    const pp::FormatDef *f = pp::find_format(id);
    return f != nullptr ? QString::fromStdString(f->label) : QString::fromStdString(id);
}

void OutputPageImpl::refresh_changed_pill() {
    if (changed_pill == nullptr)
        return;
    const int changed = sel.active_in_set() ? changed_param_count(sel.active()) : 0;
    changed_pill->setText(PageOutput::tr("已改 %1").arg(changed));
    changed_pill->setVisible(changed > 0);
}

int OutputPageImpl::changed_param_count(const std::string &id) const {
    const FormatState *st = state_of(id);
    if (st == nullptr || st->form == nullptr)
        return 0;
    const pp::FormatDef *f = pp::find_format(id);
    if (f == nullptr)
        return 0;
    const FormSelection fsel = st->form->selection();
    // M4-T13b（D-W3-1 根因）：tech_by_id 返回的是**入参容器内**的指针，
    // 而 effective_backends 按值返回 → 必须先把临时绑定到具名局部再取指针，
    // 否则 t 在语句结束即悬垂（ASAN: heap-use-after-free @ page_output.cpp）。
    const std::vector<pp::BackendDef> backends = effective_backends(*f);
    const pp::TechDef *t = tech_by_id(backends, fsel.backend, fsel.tech);
    if (t == nullptr)
        return 0;
    const pp::ParamSet values = st->form->values();
    // M7-R2.5：基准 = **生效默认值**（default_params + 锁定强制值），不是裸 ParamDef.def。
    // 旧实现拿 p.def 直接比：PNG/TIFF 的锁定项、AVIF 内省默认值与插件实际默认不同、
    // 无损开关的锁定值都被算成"用户已改" → 全新页签也出现「已改 2 / 已改 3 / 已改 8」。
    // 这里按与 ParamForm::refresh() 同一顺序铺默认值+锁，锁出来的值不算用户改动；
    // 无损复选框与编码档位是用户面独立项，单独计数。
    pp::ParamSet base;
    base[std::string(pp::kLosslessKey)] = fsel.lossless;
    for (const pp::ParamDef &p : t->params) {
        if (p.has_default)
            base[p.key] = p.def;
    }
    pp::apply_locks(*f, fsel.backend, fsel.tech, fsel.lossless, base);
    if (pp::find_param(*t, std::string(pp::kLosslessParamKey)) != nullptr)
        base[std::string(pp::kLosslessParamKey)] = fsel.lossless;
    for (int pass = 0; pass < 2; ++pass) {
        for (const pp::ParamDef &p : t->params) {
            if (const std::optional<pp::ParamValue> forced = pp::eval_lock(p, base))
                base[p.key] = *forced;
        }
    }
    int count = 0;
    for (const pp::ParamDef &p : t->params) {
        if (p.key.rfind("__", 0) == 0 || p.key == std::string(pp::kLosslessParamKey))
            continue;
        const auto it = values.find(p.key);
        if (it == values.end())
            continue;
        const auto bt = base.find(p.key);
        if (bt == base.end()) {
            ++count; // 可选覆盖参数（无默认）被用户显式设置
            continue;
        }
        if (it->second.index() != bt->second.index() || !(it->second == bt->second))
            ++count;
    }
    // 无损复选框本身（默认 false）是用户面改动；锁定参数的联动值已由 base 吸收。
    if (fsel.lossless)
        ++count;
    // M7-R2.4：编码档位是保留键（__heif_profile）不进 t->params 循环，但它是用户可改的
    // 格式参数 —— 旧实现改档位后卡头「已改 N」不变，计数漏项。非「自动」即算一处改动。
    if (st->profile != nullptr && st->profile->currentIndex() > 0)
        ++count;
    return count;
}

void OutputPageImpl::refresh_tab_labels() {
    // M7-R2.4：标题由**页面自身 id** 决定（不再 `sel.at(i)` 按下标取）—— 即使页序因
    // 增删/预设重建短暂不同步，标题也永远不会串到别的格式上。
    // M8-B2：有预检错误的格式在标题追加「· N 错误」（无错时逐字不变 = 既有探针零影响）。
    for (int i = 0; i < tabs->count(); ++i) {
        const QWidget *page = tabs->widget(i);
        if (page == nullptr)
            continue;
        const QString page_id = page->property(kPageFormatIdProperty).toString();
        if (page_id.isEmpty())
            continue;
        const std::string pid = page_id.toStdString();
        QString label = format_tab_label(pid);
        const auto it = states.find(pid);
        if (it != states.end() && !it->second.rule_errors.empty())
            label +=
                PageOutput::tr(" · %1 错误").arg(static_cast<int>(it->second.rule_errors.size()));
        tabs->setTabText(i, label);
    }
    apply_tab_width(); // UX-9：等宽随格式集合重算（文案已不含位深 → 宽度不随位深漂移）
}

void OutputPageImpl::apply_tab_width() {
    if (tabs == nullptr || tabs->tabBar() == nullptr)
        return;
    QTabBar *bar = tabs->tabBar();
    // M7-R2.4（选项卡布局回归）：回到**标准 QTabBar 行为** —— 页签按内容定宽、左对齐；
    // 放不下时由 QTabBar 自己的滚动钮处理，当前页签由 Qt 滚入可视区。
    // 旧实现无条件 setExpanding(true)（UX-9 的"等宽"自研口径）带来三类用户可见 bug：
    //   * 单个页签被拉满整行、文字居中，看起来像标题而不是选项卡（与 mockup 的左对齐不符）；
    //   * 8 个格式时 QTabBar 的 minimumSizeHint（8×自然宽）撑破卡片 → 整个输出页横向滚动、
    //     格式磁贴第 3/4 列被裁、现役页签（BMP）留在可视区外；
    //   * 空出的页签条区域被平台画成 --ctl 平色带（tabstrip/ctl-fill 探针抓到的 382px）。
    // 现：自然宽 + 滚动钮 + 不画平台 base（底部发丝线由 ::pane 的 border-top 提供）；
    // tabs 的水平 SizePolicy=Ignored，保证页签条再宽也不会把页面撑出横向滚动。
    bar->setExpanding(false);
    bar->setUsesScrollButtons(true);
    bar->setElideMode(Qt::ElideRight);
    bar->setDrawBase(false);
    tabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    tabs->setMinimumWidth(0);
}

void OutputPageImpl::update_format_count() {
    if (format_pill != nullptr) {
        format_pill->setText(PageOutput::tr("已选 %1").arg(sel.size()));
        format_pill->setVisible(true);
    }
    if (empty_hint != nullptr)
        empty_hint->setVisible(sel.empty() && !metadata_only);
    update_add_format_enabled();
}

void OutputPageImpl::on_param_selection_changed(const std::string &id) {
    FormatState *st = state_of(id);
    if (st == nullptr || st->form == nullptr)
        return;
    const std::string now = st->form->selection().backend;
    // R-192（M7-R2.1-F3）：后端覆盖判定 per-format —— 基准 = **本格式**的 last_backend
    // （原全局单槽跨格式共享，换格式即误判"用户改过"）。
    if (now != st->last_backend) {
        st->backend_user_set = true; // §9.1：用户手动改后端 → 本格式不再自动抢占
        st->last_backend = now;
    }
    set_bitdepth_options(id, -1, /*keep_if_valid=*/true);
    evaluate_avif_alpha_preselect(id); // 后端/位深变化可能使预选条件成立
    update_alpha_hint(id);
    sync_rule_context(id); // M8-B2：后端/技术/无损入规则上下文
    update_rule_errors(id);
    refresh_tab_labels();
    refresh_changed_pill();
}

void OutputPageImpl::on_bitdepth_changed(const std::string &id) {
    evaluate_avif_alpha_preselect(id);
    update_alpha_hint(id);
    sync_rule_context(id); // M8-B2：位深进规则上下文（J18 等尺寸/位深相关规则）
    update_rule_errors(id);
    refresh_tab_labels();    // 位深入页签文案（UX-9 起只重写格式名，保留入口）
    update_profile_note(id); // UX-10.1：反向联动 —— 位深变了，「自动」档的推导结果跟着变
    emit q->config_changed();
}

// ---------------------------------------------------------------- M8-B2：运行前预检
// 规则上下文（位深；format/backend/tech/lossless 由 ParamForm 自身覆盖 —— 单一真相）。
void OutputPageImpl::sync_rule_context(const std::string &id) {
    FormatState *st = state_of(id);
    if (st == nullptr || st->form == nullptr)
        return;
    pp::ParamContext ctx;
    ctx.format_id = id;
    ctx.bitdepth = bitdepth_of(id);
    st->form->set_param_context(ctx);
}

// 求值该格式的规则错误 → 底部错误清单 + 页签计数徽标（"每格式 tab 错误数"）。
void OutputPageImpl::update_rule_errors(const std::string &id) {
    FormatState *st = state_of(id);
    if (st == nullptr || st->form == nullptr)
        return;
    st->rule_errors = st->form->rule_errors();
    const int n = static_cast<int>(st->rule_errors.size());
    if (st->rule_error_box != nullptr) {
        QStringList lines;
        for (const std::string &msg : st->rule_errors)
            lines << QStringLiteral("· ") + QString::fromStdString(msg);
        st->rule_error_list->setText(
            lines.isEmpty() ? QString()
                            : PageOutput::tr("参数预检未通过（%1 项）——修正后才能开始运行：\n%2")
                                  .arg(n)
                                  .arg(lines.join(QLatin1Char('\n'))));
        st->rule_error_box->setVisible(n > 0);
    }
    // 页签徽标：文案 = 格式名（无错时逐字不变 → 既有探针断言不受影响）；有错追加计数。
    if (st->page != nullptr) {
        const int index = tabs != nullptr ? tabs->indexOf(st->page) : -1;
        if (tabs != nullptr && index >= 0) {
            tabs->setTabText(index, format_tab_label(id));
            tabs->setTabToolTip(index,
                                n > 0 ? PageOutput::tr("%1 个参数预检错误\n%2")
                                            .arg(n)
                                            .arg(QString::fromStdString(st->rule_errors.front()))
                                      : QString());
        }
    }
}

void OutputPageImpl::update_all_rule_errors() {
    for (const std::string &id : sel.items())
        update_rule_errors(id);
}

// `开始运行` 被拒时：聚焦第一个有错误的格式页签 + 该格式第一个错误行（批量只报一次）。
void OutputPageImpl::focus_first_error() {
    if (auto *window = qobject_cast<MainWindow *>(q->window()))
        window->set_current_page(2); // 输出页（1=meta 2=output 3=run）
    for (const std::string &id : sel.items()) {
        FormatState *st = state_of(id);
        if (st == nullptr || st->form == nullptr || st->rule_errors.empty())
            continue;
        if (st->page != nullptr && tabs != nullptr) {
            const int index = tabs->indexOf(st->page);
            if (index >= 0)
                tabs->setCurrentIndex(index);
        }
        st->form->focus_first_rule_error();
        return;
    }
}

pp::HeifProfile OutputPageImpl::current_profile(const std::string &id) const {
    const FormatState *st = state_of(id);
    if (st == nullptr || st->profile == nullptr)
        return pp::HeifProfile::Auto;
    return static_cast<pp::HeifProfile>(st->profile->currentData().toInt());
}

int OutputPageImpl::current_chroma(const std::string &id) const {
    const FormatState *st = state_of(id);
    if (st == nullptr || st->form == nullptr)
        return 1;
    // M7-R2.4：libheif 的 chroma 是**字符串枚举**（"420"/"422"/"444"），旧实现用
    // pp::param_int() 读 → 永远拿不到值而回落 1（4:2:0）。后果：档位注释/推导永远按
    // 4:2:0 计算，选了 4:4:4 仍显示 Main；无损的 444 警告也永不出现。这里按真实类型读。
    const pp::ParamSet values = st->form->values();
    const auto it = values.find(std::string("chroma"));
    if (it == values.end())
        return 1;
    if (const int64_t *v = std::get_if<int64_t>(&it->second))
        return (*v >= 0 && *v <= 3) ? static_cast<int>(*v) : 1;
    if (const std::string *s = std::get_if<std::string>(&it->second)) {
        if (*s == "444")
            return 3;
        if (*s == "422")
            return 2;
        if (*s == "420")
            return 1;
        if (*s == "400" || *s == "monochrome")
            return 0;
    }
    return 1;
}

void OutputPageImpl::on_profile_changed(const std::string &id) {
    FormatState *st = state_of(id);
    if (st == nullptr || st->profile == nullptr)
        return;
    st->profile_user_set = st->profile->currentIndex() != 0; // 0 = 「自动」
    set_bitdepth_options(id, -1, /*keep_if_valid=*/true);
    update_profile_note(id);
    refresh_changed_pill(); // M7-R2.4：档位计入「已改 N」（见 changed_param_count）
    emit q->config_changed();
}

void OutputPageImpl::apply_chroma_limits(const std::string &id) {
    FormatState *st = state_of(id);
    if (st == nullptr || st->form == nullptr)
        return;
    // M15：无损**不再**把色度限幅成仅 4:4:4（用户面要求 #2：无损下色度采样仍可配置）。
    // 限幅只来自「编码档位 → 色度兼容性」这一既有联动；无损 + 非 444 的"色度抽样有损"
    // 由 update_profile_note 的如实警告 + 编码器日志承担，不静默假装像素无损。
    const pp::HeifProfile want = current_profile(id);
    std::vector<int64_t> allowed;
    QString reason = PageOutput::tr("【已禁用：当前编码档位不支持该色度】");
    if (want != pp::HeifProfile::Auto) {
        std::vector<int> depths = profile_allowed_bitdepths(want, id == "avif");
        if (depths.empty())
            depths = {8, 10, 12};
        for (int c = 0; c <= 3; ++c) {
            for (const int d : depths) {
                if (pp::heif_profile_compatible_for(id == "avif", want, d, c, /*still=*/true,
                                                    nullptr)) {
                    allowed.push_back(c);
                    break;
                }
            }
        }
    }
    std::vector<std::string> allowed_text;
    for (const int64_t c : allowed) {
        if (c == 1)
            allowed_text.emplace_back("420");
        else if (c == 2)
            allowed_text.emplace_back("422");
        else if (c == 3)
            allowed_text.emplace_back("444");
    }
    // chroma 是**字符串**枚举（"420"/"422"/"444"）；整数版限幅对 string 选择匹配不到，
    // 会把整列都置灰。这里走字符串重载；allowed 非空但无可映射项（如只允许 4:0:0）→
    // 用一个不存在的哨兵值让三项全禁用，而不是误当成"清除限幅"。
    if (!allowed.empty() && allowed_text.empty())
        allowed_text.emplace_back("__none__");
    st->form->limit_enum_choices_text(std::string("chroma"), allowed_text, reason);
}

void OutputPageImpl::update_profile_note(const std::string &id) {
    FormatState *st = state_of(id);
    if (st == nullptr || st->profile == nullptr || st->profile_note == nullptr)
        return;
    // M15：无损**不再改写 chroma=444**（用户面要求 #2：无损下 chroma/档位/level 仍可配置）。
    // 无损 + 非 444 的"色度抽样有损"在本函数末尾给如实警告（不静默假装像素无损）。
    const bool lossless = st->form != nullptr && st->form->selection().lossless;
    const int depth = st->bitdepth != nullptr ? st->bitdepth->currentData().toInt() : 8;
    const pp::HeifProfile want = current_profile(id);
    const int chroma = current_chroma(id);
    // 本输出恒为静止图（与 enc_heif.cpp 的反查同口径：still = true）
    const pp::HeifProfile derived =
        pp::heif_derive_signal_profile(id == "avif", depth, chroma, true);
    const QString derived_label =
        QString::fromStdString(pp::heif_profile_label_for(id == "avif", derived));
    const QString chroma_text = chroma == 3   ? QStringLiteral("4:4:4")
                                : chroma == 2 ? QStringLiteral("4:2:2")
                                : chroma == 0 ? QStringLiteral("4:0:0")
                                              : QStringLiteral("4:2:0");
    QString note;
    QString tooltip;
    NoteTone tone = NoteTone::Hint;
    const bool avif = (id == "avif");
    // C19（§6.2.3）：下拉闭合时也要能看到完整信号说明 → 档位控件自身 tooltip = 全文。
    st->profile->setToolTip(profile_option_tooltip(avif, want));
    if (want == pp::HeifProfile::Auto) {
        // 固定短句：档位推导结果放进 tooltip。若把 derived 拼进正文，8 位默认（Main Still）
        // 会比 10 位默认多一段，足以触发页面滚动条出现，冻结的磁贴零形变探针会因布局横移失败。
        note = PageOutput::tr("插件自选");
        tooltip = PageOutput::tr("插件按 %1 位 / %2 自选 → %3")
                      .arg(depth)
                      .arg(chroma_text, derived_label);
        if (chroma == 3 || chroma == 2 ||
            (derived != pp::HeifProfile::Main && derived != pp::HeifProfile::MainStill))
            tone = NoteTone::Warn;
    } else if (const QString disabled = profile_option_disabled_reason(avif, want);
               !disabled.isEmpty()) {
        // C18：实测不可用的 still 变体（Main10Still）——预设/CLI 带进来时也要显式说明，
        // 不静默当成可用档位（编码后 SPS 断言会给同一份原因）。
        note = disabled;
        tooltip = profile_option_tooltip(avif, want);
        tone = NoteTone::Error;
    } else {
        std::string err_zh;
        if (!pp::heif_profile_compatible_for(avif, want, depth, chroma, true, &err_zh)) {
            note = QString::fromStdString(err_zh);
            tone = NoteTone::Error;
        } else {
            note = profile_compat_hint(want);
            tooltip = profile_option_tooltip(avif, want);
            if (!note.isEmpty() || chroma == 3)
                tone = NoteTone::Warn;
        }
    }
    if (lossless) {
        // M15：无损不再强制 4:4:4 —— 短句按真实色度如实呈现；HEIF 只对非 444 给"色度抽样
        // 有损"警告（不静默假装像素无损）。AVIF 的色度在规则层仍锁 444（G3/A2）。
        const bool chroma_is_444 = (chroma == 3);
        if (id != "avif" && !chroma_is_444) {
            if (note.isEmpty())
                note = PageOutput::tr("无损 + %1：色度有损").arg(chroma_text);
            tooltip = PageOutput::tr("无损模式下选择 4:2:0/4:2:2：色度在编码前被下采样，YCbCr 码流"
                                     "无损，但不等于像素级无损（需要像素级无损请把色度采样设为 "
                                     "4:4:4）。HEVC 的无损是 YCbCr 平面无损，RGB↔YCbCr 往返有舍入"
                                     "（不是逐位）；超过 16 MP 时自动改用标准 HEIF grid 分块，"
                                     "保证 libde265/OIIO 能打开。");
            if (tone == NoteTone::Hint)
                tone = NoteTone::Warn;
        } else {
            if (note.isEmpty())
                note = PageOutput::tr("无损（YCbCr 平面无损）");
            tooltip = PageOutput::tr(
                "无损 = 编码器无损模式（HEIF 也可用「速率控制」下拉的「无损」档，二者同源）。"
                "HEVC/AV1 的无损是 YCbCr 平面无损，RGB↔YCbCr 往返有舍入（不是逐位）；"
                "超过 16 MP 时自动改用标准 HEIF grid 分块，保证 libde265/OIIO 能打开。"
                "4:4:4 的查看器兼容性弱于 4:2:0。");
        }
    }
    // M7-R2.4：tooltip 单点落盘 —— 旧实现先在「自动」分支写推导 tooltip、末尾非无损分支
    // 又无条件 `setToolTip(QString())` 把它清掉，用户悬浮只看到空提示（推导结果永远不可见）。
    st->profile_note->setToolTip(tooltip);
    st->profile_note->setText(note);
    st->profile_note->setProperty("ppInvalid",
                                  tone == NoteTone::Error ? QStringLiteral("true") : QString());
    st->profile_note->setProperty(theme::kFkProperty,
                                  tone == NoteTone::Hint   ? QLatin1String(theme::kFkHint)
                                  : tone == NoteTone::Warn ? QLatin1String(theme::kFkWarn)
                                                           : QString());
    theme::repolish(st->profile_note);
    // UX-10.1 残余：档位 → 色度限幅联动（本注释位刷新点即联动收敛点 —— 档位/位深/色度
    // 任一变化都会走到这里）
    apply_chroma_limits(id);
}

void OutputPageImpl::repopulate_bitdepths(FormatState &st, const std::vector<int> &allowed,
                                          int wanted) {
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

bool OutputPageImpl::set_bitdepth_options(const std::string &id, int forced, bool keep_if_valid) {
    const pp::FormatDef *f = pp::find_format(id);
    FormatState *st = state_of(id);
    if (f == nullptr || st == nullptr || st->bitdepth == nullptr)
        return false;
    // BMP 的“位深”实际由通道数决定（灰度 8bpp / 彩色 24bpp），8/24 下拉不驱动 bpp。
    // 不再暴露假选择：固定为“自动（按通道）”，disabled 展示。
    if (id == "bmp") {
        st->bitdepth->clear();
        st->bitdepth->addItem(PageOutput::tr("自动（按通道）"), 24);
        st->bitdepth->setCurrentIndex(0);
        st->bitdepth->setEnabled(false);
        if (st->bitdepth_note != nullptr) {
            // 窄卡片下 24bpp 全句会被 QLabel 硬裁（无省略号）；短句 + 全文 tooltip。
            st->bitdepth_note->setText(PageOutput::tr("灰度 8bpp / 彩色 24bpp"));
            st->bitdepth_note->setToolTip(
                PageOutput::tr("灰度 → 8bpp，彩色 → 24bpp（由通道数决定）"));
        }
        return false;
    }
    std::vector<int> allowed =
        allowed_bitdepths(*f, st->form != nullptr ? st->form->selection().backend : "");
    if (allowed.empty())
        return false;
    // UX-10.1：档位 → 位深限幅（与格式能力求交；「自动」不限幅）。
    // 限幅只收窄下拉选项，不静默改写用户已选的位深（keep_if_valid 语义不变）。
    {
        const std::vector<int> cap = profile_allowed_bitdepths(current_profile(id), id == "avif");
        if (!cap.empty()) {
            std::vector<int> narrowed;
            narrowed.reserve(allowed.size());
            for (int d : allowed) {
                if (contains(cap, d))
                    narrowed.push_back(d);
            }
            if (!narrowed.empty())
                allowed.swap(narrowed);
        }
    }
    const int current = st->bitdepth->currentData().toInt();
    int resolved = 0;
    if (forced > 0 && contains(allowed, forced)) {
        resolved = forced;
    } else if (keep_if_valid && contains(allowed, current)) {
        resolved = current;
    } else {
        resolved = pp::default_bitdepth_for(f->id); // R-238：默认位深单源
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

void OutputPageImpl::apply_state_to_form(const std::string &id, const pp::OutputFormatSpec &spec) {
    FormatState *st = state_of(id);
    const pp::FormatDef *f = pp::find_format(id);
    if (st == nullptr || st->form == nullptr || f == nullptr)
        return;
    const bool have_spec = !spec.format_id.empty();
    const bool lossless = have_spec ? pp::lossless_flag(spec.params) : false;
    if (have_spec) {
        FormSelection fsel;
        fsel.backend = spec.backend_id;
        fsel.tech = spec.tech_id;
        fsel.lossless = lossless;
        st->form->set_selection(fsel);
        // M12 需求 3 根因修复：数值字面量按生效参数表归一类型后再落值（详见
        // coerce_numeric_param_types 注释 —— 修复 HEIF Apple 自适应 `x265:crf=20` 显示 28）。
        pp::ParamSet applied = spec.params;
        {
            const std::vector<pp::BackendDef> eff = effective_backends(*f);
            if (const pp::TechDef *coerce_tech = tech_by_id(eff, fsel.backend, fsel.tech))
                coerce_numeric_param_types(*coerce_tech, applied);
        }
        st->form->set_values(applied); // R-265：原"二次落值"死码删除 —— set_values 幂等，
                                       // 第二遍零增量（魔法依赖：靠重复调用补齐切技术后的
                                       // 同键项，实际一次已覆盖；见 build/m7r2-d2/handoff）
        // R-135：表单不认的键（未知键/跨技术键）记存 —— snapshot/spec_of 原样带回，
        // load→save 往返不再丢键（保留键 "__" 另有承载面，不入本表）。
        st->extra.clear();
        {
            const pp::ParamSet owned = st->form->values();
            for (const auto &[k, v] : spec.params) {
                if (k.rfind("__", 0) == 0)
                    continue;
                if (owned.find(k) == owned.end())
                    st->extra[k] = v;
            }
        }
        // UX-10.1：还原编码档位（保留键 __heif_profile；缺省 = Auto）。
        // 旧预设可能带已被新表淘汰的兼容值（如 Main12=3）→ 找不到时显式回落到「自动」，
        // 不能沿用上一个页签/上一次落值留下的档位（旧实现只 break，索引保持不变）。
        // W2-T2（C18）备注：不可用档位（Main10Still=16）在模型里是 disabled；若预设带 16，
        // Qt 对 disabled item 的 setCurrentIndex 行为未在本任务实测（可能保留上一档位）——
        // 属已知边界，见 build/m10/w2-profile/report.md §9。
        if (st->profile != nullptr) {
            const int want =
                int(pp::param_int(spec.params, std::string(pp::kHeifProfileParamKey), 0));
            const QSignalBlocker blocker(st->profile);
            bool found = false;
            for (int i = 0; i < st->profile->count(); ++i) {
                if (st->profile->itemData(i).toInt() == want) {
                    st->profile->setCurrentIndex(i);
                    found = true;
                    break;
                }
            }
            if (!found)
                st->profile->setCurrentIndex(0);
            st->profile_user_set = st->profile->currentIndex() != 0;
        }
        // M10 / W3-T5（A20）：自适应会话态随 OutputFormatSpec 恢复 —— 全局预设载入（apply_preset）
        // 与格式预设载入（apply_format_preset_to）共用本落卡函数；空策略 = 关闭 → 整行隐藏，
        // 不残留上一个预设的策略（与「全局预设值覆盖全卡」口径一致；无隐式叠加）。
        st->adaptive_policy = spec.adaptive_policy;
        st->adaptive_enabled = !spec.adaptive_policy.empty() && spec.adaptive_enabled;
        update_adaptive_row(id);
    }
    set_bitdepth_options(id, have_spec ? spec.out_bitdepth : -1, /*keep_if_valid=*/false);
    evaluate_avif_alpha_preselect(id);
    update_alpha_hint(id);
    update_profile_note(id); // UX-10.1：档位注释位随落值刷新
    refresh_changed_pill();
}

pp::OutputFormatSpec OutputPageImpl::spec_of(const std::string &id) const {
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
            spec.out_bitdepth = pp::default_bitdepth_for(id); // R-238：默认位深单源
        }
        return spec;
    }
    const FormSelection fsel = st->form->selection();
    spec.backend_id = fsel.backend;
    spec.tech_id = fsel.tech;
    spec.params = st->extra; // R-135：表单不认的键原样带回（同名键由 form values 覆盖）
    for (const auto &[k, v] : st->form->values())
        spec.params[k] = v;
    spec.params[std::string(pp::kLosslessKey)] = fsel.lossless; // 内部管道键（引擎侧）
    spec.out_bitdepth = bitdepth_of(id);
    // UX-10.1：编码档位随 spec 下发（保留键 __heif_profile）
    if (st->profile != nullptr)
        spec.params[std::string(pp::kHeifProfileParamKey)] =
            static_cast<int64_t>(st->profile->currentData().toInt());
    // M10 / W3-T5（A20）：自适应开关 + 策略原文随卡快照采集（全局预设输出级 `adaptive` 的
    // 写盘面；格式预设 collect 另有同名两字段）。**采集面忠实**：开关关掉也保留策略文本
    // （用户重新打开不丢策略）；执行面门控在 `config_base()`（未启用 ⇒ 不下发 → 旧路径）。
    spec.adaptive_enabled =
        !st->adaptive_policy.empty() && st->adaptive_parse_ok && st->adaptive_enabled;
    spec.adaptive_policy = st->adaptive_policy;
    return spec;
}

pp::ParamSet OutputPageImpl::default_params_for(const std::string &id, const std::string &backend,
                                                const std::string &tech, bool lossless) const {
    const pp::FormatDef *f = pp::find_format(id);
    return f != nullptr ? pp::default_params(*f, backend, tech, lossless) : pp::ParamSet{};
}

void OutputPageImpl::update_alpha_hint(const std::string &id) {
    FormatState *st = state_of(id);
    if (st == nullptr || st->alpha_hint == nullptr)
        return;
    // R-066：提示分两态如实显示 —— 文案自称「已选择 libaom 后端」只在后端确为 libaom 时
    // 出现（预设路径跳过预选后可能仍是 svt-av1，此时不能宣称已改后端，改为指向改后端）。
    // R-191（M7-R2E2）：组合谓词单源 = fmtpolicy::alpha_combo（与 update_metadata_only_state
    // 的内联副本收敛；两态文案语义不动）
    const bool combo = fmtpolicy::alpha_combo(id, metadata_only, batch_has_alpha, bitdepth_of(id));
    const std::string backend = backend_id(id);
    if (combo && backend == "libaom") {
        st->alpha_hint->set_full_text(
            PageOutput::tr("10 位 + 含 alpha：已选择 libaom 后端（SVT-AV1 不支持该组合）"));
        st->alpha_hint->setVisible(true);
    } else if (combo && backend == "svt-av1") {
        st->alpha_hint->set_full_text(
            PageOutput::tr("10 位 + 含 alpha：SVT-AV1 不支持该组合（请改用 libaom 后端）"));
        st->alpha_hint->setVisible(true);
    } else {
        st->alpha_hint->setVisible(false);
    }
}

bool OutputPageImpl::evaluate_avif_alpha_preselect(const std::string &id) {
    FormatState *st = state_of(id);
    if (st == nullptr || st->form == nullptr)
        return false;
    const FormSelection fsel = st->form->selection();
    const pp::FormatDef *f = pp::find_format(id);
    const bool libaom_available = f != nullptr && has_backend(effective_backends(*f), "libaom");
    // R-191（M7-R2E2）：触发条件单源 = fmtpolicy::avif_preselect_needed（谓词真值表入
    // tests/unit/test_smoke.cpp；求值成功后的改写动作留本处）
    if (!fmtpolicy::avif_preselect_needed(id, metadata_only, batch_has_alpha, bitdepth_of(id),
                                          fsel.backend, st->backend_user_set, in_preset_apply,
                                          libaom_available))
        return false;
    FormSelection next = fsel;
    next.backend = "libaom";
    st->form->set_selection(next);   // 程序化：不发信号
    st->last_backend = next.backend; // R-192 per-format
    set_bitdepth_options(id, -1, /*keep_if_valid=*/true);
    return true;
}

void OutputPageImpl::update_metadata_only_state() {
    const bool enabled = !metadata_only;
    // 互斥置灰：磁贴多选区 + 格式参数卡（模式分段自身保持可用 —— 否则无法切回）
    tiles_host->setEnabled(enabled);
    param_card->setEnabled(enabled);
    // v0.2 语义（M1b §2.9）：仅元数据不做色彩转换 → 色彩目标一并禁用
    color_combo->setEnabled(enabled);
    // R-318：禁用态给原因（"不静默灰"口径全量推广）—— 置灰区 tooltip 标明禁用原因，
    // 复用既有说明行（pp-format-note）的语义，不静默灰。
    {
        const QString reason =
            PageOutput::tr("【已禁用：仅元数据模式不做色彩转换、不改输出格式（保持原格式）】");
        tiles_host->setToolTip(enabled ? QString() : reason);
        param_card->setToolTip(enabled ? QString() : reason);
        color_combo->setToolTip(enabled ? color_tip_base : reason);
    }
    mode_note->setVisible(metadata_only);
    {
        const QSignalBlocker blocker(mode_seg);
        mode_seg->set_current_index(metadata_only ? 1 : 0);
    }
    for (const auto &[id, st] : states) {
        if (st.alpha_hint != nullptr)
            st.alpha_hint->setVisible(
                fmtpolicy::alpha_combo(id, metadata_only, batch_has_alpha, bitdepth_of(id)));
    }
    if (empty_hint != nullptr)
        empty_hint->setVisible(sel.empty() && !metadata_only);
    update_add_format_enabled();
}

void OutputPageImpl::update_estimate() {
    if (est_output == nullptr)
        return;
    const int formats = metadata_only ? 1 : static_cast<int>(sel.size());
    const int outputs = input_count > 0 && formats > 0 ? input_count * formats : 0;
    est_output->set_full_text(outputs > 0 ? PageOutput::tr("%1 个").arg(outputs)
                                          : PageOutput::tr("—"));
    est_output->setToolTip(PageOutput::tr("每个已选文件都会为每个已选格式生成一个输出文件"));
    est_size->set_full_text(PageOutput::tr("—"));
    est_time->set_full_text(PageOutput::tr("—"));
    if (preset_hint != nullptr)
        preset_hint->set_full_text(
            input_count > 0 ? PageOutput::tr("基于选中 %1 个文件").arg(input_count) : QString());
}

void OutputPageImpl::refresh_disk() {
    if (est_disk == nullptr)
        return;
    const QString root = out_root_edit != nullptr ? out_root_edit->text().trimmed() : QString();
    QString disk = PageOutput::tr("未设置根目录");
    QString disk_tip = PageOutput::tr("填写输出根目录后显示可用空间");
    if (!root.isEmpty()) {
        const QStorageInfo info(root);
        if (info.isValid() && info.isReady()) {
            disk = PageOutput::tr("%1 可用").arg(byte_text::natural_bytes(info.bytesAvailable()));
            disk_tip = PageOutput::tr("输出根目录所在卷的可用空间：%1").arg(root);
        } else {
            disk_tip = PageOutput::tr("无法读取输出根目录所在卷的可用空间：%1").arg(root);
        }
    }
    est_disk->set_full_text(disk);
    est_disk->setToolTip(disk_tip);
}

void OutputPageImpl::schedule_disk_refresh() {
    if (disk_timer != nullptr)
        disk_timer->start(); // singleShot 400ms：输入停顿后统一查一次
}

// 指针键注册表：析构擦除 → 无悬挂/别名；GUI 线程独占，无需锁。
// R-185（M7-R2.1-F3）：注册表 pimpl 两套解法统一 —— page_run 已收敛到本同款
// （std::map + unique_ptr + 析构 erase + *_of() 访问器），不再有裸 new/手动 delete。
std::map<const PageOutput *, std::unique_ptr<OutputPageImpl>> &registry() {
    static std::map<const PageOutput *, std::unique_ptr<OutputPageImpl>> store;
    return store;
}

OutputPageImpl *impl_of(const PageOutput *page) {
    const auto it = registry().find(page);
    return it == registry().end() ? nullptr : it->second.get();
}

// ---------------------------------------------------------------- PageOutput

PageOutput::PageOutput(QWidget *parent) : QWidget(parent) {
    auto impl = std::make_unique<OutputPageImpl>();
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
    OutputPageImpl *impl_ = impl_of(this);
    pp::RunConfig cfg;
    const QString root = impl_->out_root_edit->text().trimmed();
    if (!root.isEmpty())
        cfg.out_root = std::filesystem::path(root.toStdString());
    // 多格式（§3.2）：已选集合逐项落成 OutputFormatSpec；仅元数据模式恒 1 项（§3.2 硬校验）
    if (impl_->metadata_only) {
        const std::string id =
            impl_->sel.active_in_set()
                ? impl_->sel.active()
                : (impl_->sel.empty() ? impl_->sel.active() : impl_->sel.items().front());
        cfg.outputs.push_back(impl_->spec_of(id));
    } else {
        for (const std::string &id : impl_->sel.items())
            cfg.outputs.push_back(impl_->spec_of(id));
    }
    // M10 / W3-T5（A20）：自适应**执行面门控** —— 未启用（开关关 / 策略解析失败 / 策略为空）
    // ⇒ 不下发策略，引擎走既有单次编码路径（逐字节不变，C25）。采集面（spec_of/collect_preset）
    // 仍保留策略原文，保证「关掉开关再保存预设」不丢文本。
    for (pp::OutputFormatSpec &spec : cfg.outputs) {
        if (!spec.adaptive_enabled)
            spec.adaptive_policy.clear();
    }
    cfg.color = impl_->color_target();
    // M7-R2.1-F4：色彩四选项（UX-14 C1–C4）——默认值 = 现行为（0/false），能力类 opt-in
    cfg.color_intent =
        impl_->intent_combo != nullptr ? impl_->intent_combo->currentData().toInt() : 0;
    cfg.color_dither8 = impl_->dither8_switch != nullptr && impl_->dither8_switch->isChecked();
    cfg.gamut_mapping =
        impl_->gamut_combo != nullptr ? impl_->gamut_combo->currentData().toInt() : 0;
    cfg.linear_resample = impl_->linear_switch != nullptr && impl_->linear_switch->isChecked();
    cfg.conflict = impl_->conflict();
    cfg.metadata_only = impl_->metadata_only;
    cfg.output_template = impl_->template_text().toStdString();
    cfg.split_by_format = OutputPageImpl::template_has_format(impl_->template_text());
    // rules / workers / budget_bytes / rotate_orientation / flatten_gray / stagger / thread_budget
    // 由 MainWindow 填充
    return cfg;
}

bool PageOutput::metadata_only() const {
    OutputPageImpl *impl_ = impl_of(this);
    return impl_->metadata_only;
}

void PageOutput::set_metadata_only(bool on) {
    OutputPageImpl *impl_ = impl_of(this);
    if (impl_->metadata_only == on)
        return;
    impl_->metadata_only = on;
    impl_->update_metadata_only_state();
    impl_->update_estimate();
    emit config_changed();
}

QString PageOutput::out_root() const {
    OutputPageImpl *impl_ = impl_of(this);
    return impl_->out_root_edit->text();
}

void PageOutput::set_out_root(const QString &dir) {
    OutputPageImpl *impl_ = impl_of(this);
    if (impl_->out_root_edit->text() == dir)
        return;
    const QSignalBlocker blocker(impl_->out_root_edit);
    impl_->out_root_edit->setText(dir);
    impl_->update_template_example();
    impl_->update_estimate();
    impl_->refresh_disk(); // R-228：程序化换根同步查一次（非逐键路径）
    emit config_changed();
}

void PageOutput::set_batch_has_alpha(bool has) {
    OutputPageImpl *impl_ = impl_of(this);
    // §9.1：本入口清除用户手动覆盖（R-192 per-format：逐格式清 backend_user_set）
    for (auto &[sid, sstate] : impl_->states) {
        (void)sid;
        sstate.backend_user_set = false;
    }
    const bool state_changed = (impl_->batch_has_alpha != has);
    impl_->batch_has_alpha = has;
    bool switched = false;
    for (const std::string &id : impl_->sel.items())
        switched = impl_->evaluate_avif_alpha_preselect(id) || switched;
    for (const std::string &id : impl_->sel.items())
        impl_->update_alpha_hint(id);
    if (state_changed || switched)
        emit config_changed();
}

void PageOutput::apply_preset(const pp::PresetData &p) {
    OutputPageImpl *impl_ = impl_of(this);
    pp::PresetData preset = p;
    pp::normalize_preset(preset); // 先用当前格式表补齐
    if (!pp::validate_preset(preset).empty())
        return; // 校验失败 → 不应用，保持原状（加载失败显式报错由调用方负责，R31）
    const std::string prev_active = impl_->sel.active(); // R-133：发射按「若变」约定

    // 0.4.2 / 需求 3（A13）：**整页原子替换** —— 先清**所有**格式的来源会话态（含未选中格式的
    // 旧绑定/旧大预设注记；否则重新勾选格式时会显示上一份配置的来源注记 = 来源撒谎）。
    for (auto &[sid, sstate] : impl_->states) {
        (void)sid;
        sstate.preset_detached = false;
        sstate.preset_global_name.clear();
        sstate.preset_based_on.clear();
        sstate.format_preset_path.clear();
    }
    // 已选集合 = 预设的 outputs（规范顺序）；逐输出采集来源（与 outputs 同序；
    // output_sources 可空/可短 —— 缺项 = 自定义参数；同格式重复项取首个 = 与集合去重同键）。
    struct SourceInfo {
        QString name;
        QString based_on;
    };
    std::vector<std::string> preset_ids;
    std::map<std::string, SourceInfo> source_of;
    for (std::size_t i = 0; i < preset.outputs.size(); ++i) {
        const pp::OutputFormatSpec &spec = preset.outputs[i];
        if (pp::find_format(spec.format_id) == nullptr)
            continue;
        const bool first =
            std::find(preset_ids.begin(), preset_ids.end(), spec.format_id) == preset_ids.end();
        if (first)
            preset_ids.push_back(spec.format_id);
        if (!first && source_of.find(spec.format_id) != source_of.end())
            continue;
        SourceInfo info;
        info.name = QString::fromStdString(preset.name);
        if (i < preset.output_sources.size())
            info.based_on = QString::fromStdString(preset.output_sources[i].format_preset);
        source_of[spec.format_id] = std::move(info);
    }
    impl_->sel.reset_items(std::move(preset_ids));
    // 在重建页签之前写来源态：build_tab 末尾会请 MainWindow 喂列表并尝试**自动载入 default**
    // （refresh_format_preset_row）；detached 抑制它（否则会把大预设值冲掉）。来源名同批写入，
    // 使列表回填时占位/注记已是真实来源（不再出现"先闪一下 default"）。
    for (const std::string &id : impl_->sel.items()) {
        const auto it = source_of.find(id);
        if (it == source_of.end())
            continue;
        impl_->states[id].preset_detached = true;
        impl_->states[id].preset_global_name = it->second.name;
        impl_->states[id].preset_based_on = it->second.based_on;
    }
    // 清掉不再选中的页签，建/复用所选页签
    for (const FormatEntry &e : kFormats) {
        if (!impl_->is_selected(e.id))
            impl_->remove_tab(e.id);
    }
    for (const std::string &id : impl_->sel.items())
        impl_->build_tab(id);
    if (!impl_->sel.empty())
        impl_->sel.set_active(impl_->sel.items().front());
    // R-066：预设落值期间抑制 avif+alpha 预选（预设后端 = 权威值，不静默改写）
    impl_->in_preset_apply = true;
    for (const pp::OutputFormatSpec &spec : preset.outputs) {
        if (!impl_->is_selected(spec.format_id))
            continue;
        impl_->states[spec.format_id].spec = spec;
        impl_->apply_state_to_form(spec.format_id, spec);
    }
    impl_->in_preset_apply = false;
    impl_->sync_tiles();
    impl_->reconcile_tab_order(); // M7-R2.4：预设可把页签建在任意顺序 → 按规范序对齐
    impl_->sync_tab_current();
    impl_->update_format_count();
    impl_->update_metadata_only_state();

    // 全局字段
    {
        const QSignalBlocker blocker(impl_->color_combo);
        impl_->color_combo->setCurrentIndex(
            impl_->color_combo->findData(static_cast<int>(preset.color_target))); // R-190
    }
    {
        const QSignalBlocker blocker(impl_->conflict_combo);
        impl_->conflict_combo->setCurrentIndex(
            impl_->conflict_combo->findData(static_cast<int>(preset.conflict))); // R-190
    }
    {
        const QSignalBlocker blocker(impl_->template_edit);
        impl_->template_edit->setText(QString::fromStdString(preset.output_template));
    }
    // 第二轮：色彩四全局项 + 仅元数据模式恢复（缺省值 = 现行为，旧预设零漂移）。
    if (impl_->intent_combo != nullptr) {
        const QSignalBlocker blocker(impl_->intent_combo);
        impl_->intent_combo->setCurrentIndex(impl_->intent_combo->findData(preset.color_intent));
    }
    if (impl_->gamut_combo != nullptr) {
        const QSignalBlocker blocker(impl_->gamut_combo);
        impl_->gamut_combo->setCurrentIndex(impl_->gamut_combo->findData(preset.gamut_mapping));
    }
    if (impl_->dither8_switch != nullptr) {
        const QSignalBlocker blocker(impl_->dither8_switch);
        impl_->dither8_switch->setChecked(preset.color_dither8);
    }
    if (impl_->linear_switch != nullptr) {
        const QSignalBlocker blocker(impl_->linear_switch);
        impl_->linear_switch->setChecked(preset.linear_resample);
    }
    if (p.metadata_only != impl_->metadata_only) {
        const QSignalBlocker blocker(this);
        set_metadata_only(p.metadata_only);
    }
    impl_->sync_split_switch(QString::fromStdString(preset.output_template));
    impl_->update_template_example();
    impl_->update_estimate();
    impl_->refresh_tab_labels();
    impl_->refresh_changed_pill();
    // ---- 0.4.2 / 需求 3（A13）：来源 = 大预设的**如实**显示 ----
    // 占位「来自大预设：<名>」+ 注记「值来自大预设「<名>」（基于格式预设 <stem> / 自定义参数）」，
    // combo **无选中项**（不再假装高亮 default）；保存/删除禁用（无绑定文件），另存/导入/导出可用。
    // 显式载入任一格式预设成功后由 format_preset_load_into 解除并恢复真绑定。
    for (const std::string &id : impl_->sel.items())
        impl_->sync_format_preset_source_display(id);
    // R-133（M7-R2.1-F4）：format_changed 已删除（孤儿信号零 connect；任务明文「接线或
    // 删除」取删除，R2D1-A3 零消费声明删除先例）。现役变更通知走 config_changed（消费方 =
    // mainwindow refresh_status，行为逐字不变）。
    (void)prev_active;
    emit config_changed();
}

pp::PresetData PageOutput::collect_preset(const QString &name) const {
    OutputPageImpl *impl_ = impl_of(this);
    pp::PresetData p;
    p.version = 2;
    p.name = name.toStdString();
    for (const std::string &id : impl_->sel.items())
        p.outputs.push_back(impl_->spec_of(id));
    // 0.4.2 / 需求 3（A13）：逐输出采集来源格式预设（文件名 stem）——
    //   * 绑定格式预设文件 → 用该文件名 stem；
    //   * 来源 = 大预设（detached）→ 继承其记录的 based_on（溯源链扁平化：保存出的新大预设
    //     仍标明"基于哪个格式预设"，不丢来源）；
    //   * 其余（自定义参数）→ 空串。
    // 空 vector 规范形态：全部为空时不写（旧文件 load→save 逐字节不变）。
    {
        std::vector<pp::PresetOutputSource> sources;
        bool any = false;
        sources.reserve(p.outputs.size());
        for (const std::string &id : impl_->sel.items()) {
            pp::PresetOutputSource source;
            const FormatState *st = impl_->state_of(id);
            if (st != nullptr) {
                if (!st->format_preset_path.isEmpty()) {
                    source.format_preset =
                        QFileInfo(st->format_preset_path).completeBaseName().toStdString();
                } else if (st->preset_detached) {
                    source.format_preset = st->preset_based_on.toStdString();
                }
            }
            any = any || !source.format_preset.empty();
            sources.push_back(std::move(source));
        }
        if (any)
            p.output_sources = std::move(sources);
    }
    p.output_template = impl_->template_text().toStdString();
    p.split_by_format = OutputPageImpl::template_has_format(impl_->template_text());
    p.color_target = impl_->color_target();
    p.conflict = impl_->conflict();
    // 第二轮：色彩四全局项 + 仅元数据模式也必须进预设（否则 save→load 产物不一致）。
    const pp::RunConfig cfg = config_base();
    p.color_intent = cfg.color_intent;
    p.color_dither8 = cfg.color_dither8;
    p.gamut_mapping = cfg.gamut_mapping;
    p.linear_resample = cfg.linear_resample;
    p.metadata_only = cfg.metadata_only;
    // rules 由 MainWindow 合并（§2.9）
    return p;
}

void PageOutput::restore_last(const QString &format_id, const QString &out_root) {
    OutputPageImpl *impl_ = impl_of(this);
    // 冻结契约（page_output.h「session restore (no signals); invalid values ignored」）：
    // 会话恢复全程**不发信号** —— 0.3.0 的多格式路径（select_format / set_out_root）内部会
    // emit config_changed，故在此对整页屏蔽（内部更新全是直接调用，不依赖信号）。
    const QSignalBlocker blocker(this);
    if (!format_id.isEmpty() && pp::find_format(format_id.toStdString()) != nullptr)
        select_format(format_id);
    if (!out_root.isEmpty())
        set_out_root(out_root);
    // 原 0.2 实现的收尾动作（等价物）：全部已选格式重算 alpha 提示
    for (const std::string &id : impl_->sel.items())
        impl_->update_alpha_hint(id);
}

QString PageOutput::ready_to_start() const {
    OutputPageImpl *impl_ = impl_of(this);
    const QString root = impl_->out_root_edit->text().trimmed();
    if (root.isEmpty())
        return tr("未设置输出根目录");
    if (!QDir::isAbsolutePath(root))
        return tr("输出根目录必须是绝对路径");
    if (!impl_->metadata_only && impl_->sel.empty())
        return tr("未选择输出格式");
    // §4.2（复核项 10）：非法路径模板必须在 ready_to_start 硬阻断（未知符号 / .. / 绝对路径）
    {
        std::string tmpl_err;
        if (!pp::validate_output_template(impl_->template_text().toStdString(), &tmpl_err)) {
            // R-070/273：模板预检错误走中文映射单源（未命中回退引擎原文，不编造文案）
            const std::string zh = pp::error_text_zh(tmpl_err);
            return QString::fromStdString(zh.empty() ? tmpl_err : zh);
        }
        if (impl_->template_text().trimmed().isEmpty())
            return tr("路径模板不能为空");
    }
    // M8-B2（C6）：运行前参数预检 —— 规则引擎错误逐格式汇总（**批量只报一次**：单条中文
    // 消息含所有格式的错误；拒绝后由 MainWindow 调 focus_first_rule_error() 定位第一错行）。
    if (!impl_->metadata_only) {
        QStringList lines;
        for (const std::string &id : impl_->sel.items()) {
            impl_->update_rule_errors(id); // 现算，保证与 UI 实时态同一份求值
            const FormatState *st = impl_->state_of(id);
            if (st == nullptr || st->rule_errors.empty())
                continue;
            const pp::FormatDef *fdef = pp::find_format(id);
            const QString label =
                fdef != nullptr ? QString::fromStdString(fdef->label) : QString::fromStdString(id);
            for (const std::string &msg : st->rule_errors)
                lines << tr("【%1】%2").arg(label, QString::fromStdString(msg));
        }
        if (!lines.isEmpty()) {
            impl_->refresh_tab_labels(); // 页签徽标与拒绝同一帧可见
            return tr("参数预检未通过（%1 项）——修正后才能开始运行：\n%2")
                .arg(lines.size())
                .arg(lines.join(QLatin1Char('\n')));
        }
    }
    return QString();
}

// M8-B2：预检拒绝后定位（MainWindow::on_start 在 warn_cannot_start 之后调用一次）：
// 切到输出页 → 第一个有错格式的页签 → 该格式第一个错误行。
void PageOutput::focus_first_rule_error() {
    OutputPageImpl *impl_ = impl_of(this);
    impl_->update_all_rule_errors();
    impl_->focus_first_error();
}

QString PageOutput::current_format() const {
    OutputPageImpl *impl_ = impl_of(this);
    return QString::fromStdString(impl_->sel.active());
}

void PageOutput::select_format(const QString &format_id) {
    OutputPageImpl *impl_ = impl_of(this);
    if (format_id.isEmpty())
        return;
    const std::string id = format_id.toStdString();
    if (pp::find_format(id) == nullptr)
        return;
    // 冒烟/走查钩子：收敛为单选并激活该页签（多选走磁贴点击）
    // R-226：tiles[] 潜伏越界防御 —— tile_index() 对 UI 表外 id 返回 kFormatCount，旧代码
    // tiles[tile_index(id)] 直接越界读（kFormats 与 core 格式表两表分裂即 UB）。现 tiles[]
    // 不再按下标取（R-265：原 QSignalBlocker 本就冗余 —— 置值只发生在 sync_tiles 的自阻断内），
    // 表外 id 继续走通用路径；断言锚点：UI 表外 id 不得进入磁贴下标路径。
    {
        const int tile = OutputPageImpl::tile_index(id);
        Q_ASSERT(tile >= 0 && tile < kFormatCount);
        (void)tile;
    }
    for (const FormatEntry &e : kFormats) {
        if (e.id != id && impl_->is_selected(e.id))
            impl_->toggle_format(e.id, false, /*user=*/false);
    }
    impl_->toggle_format(id, true, /*user=*/false);
    impl_->sel.set_active(id); // R-189：toggle 已置现役（幂等保序）
    impl_->reproject_format_selection();
    impl_->update_template_example();
}

// ---------------------------------------------------------------- 追加面（M4-T13 / M5-T8）

void PageOutput::set_input_count(int files) {
    OutputPageImpl *impl_ = impl_of(this);
    const int next = files > 0 ? files : 0;
    if (impl_->input_count == next)
        return;
    impl_->input_count = next;
    impl_->update_estimate(); // 只刷新预估读数（不发信号 → 不触发 refresh_status 回环）
}

void PageOutput::set_tokens(const theme::Tokens &tokens) {
    OutputPageImpl *impl_ = impl_of(this);
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

// 0.4.2 / 需求 3（A13）：预设卡条目文本 = 基准显示名 + （当前绑定且已修改时的「（已修改）」）。
// 基准名存 kPresetBaseNameRole（不靠往返拼接/剥离字符串），dirty 态由主窗比对后喂入。
void OutputPageImpl::refresh_preset_combo_texts() {
    if (preset_combo == nullptr)
        return;
    const int current = preset_combo->currentIndex();
    const QString current_path =
        current >= 0 ? preset_combo->itemData(current, Qt::UserRole).toString() : QString();
    for (int i = 0; i < preset_combo->count(); ++i) {
        QString base = preset_combo->itemData(i, kPresetBaseNameRole).toString();
        if (base.isEmpty())
            base = preset_combo->itemText(i);
        const bool dirty_here = preset_dirty && !current_path.isEmpty() &&
                                preset_combo->itemData(i, Qt::UserRole).toString() == current_path;
        preset_combo->setItemText(i, dirty_here ? base + dirty_suffix() : base);
        preset_combo->setItemData(i, base, Qt::ToolTipRole);
    }
}

void OutputPageImpl::sync_preset_buttons() {
    if (preset_combo == nullptr)
        return;
    const int index = preset_combo->currentIndex();
    const QString path =
        index >= 0 ? preset_combo->itemData(index, Qt::UserRole).toString() : QString();
    const bool has = !path.isEmpty();
    preset_save->setEnabled(has);
    preset_delete->setEnabled(has);
    preset_save->setToolTip(
        has ? (preset_dirty ? PageOutput::tr("保存到当前预设（覆盖同名文件；当前配置已修改）")
                            : PageOutput::tr("保存到当前预设（覆盖同名文件）"))
            : PageOutput::tr("未绑定预设：请先「另存…」保存一份新预设"));
    preset_delete->setToolTip(has ? PageOutput::tr("删除当前预设（会二次确认）")
                                  : PageOutput::tr("未绑定预设：无可删除项"));
}

void PageOutput::set_preset_list(const std::vector<std::pair<QString, QString>> &presets) {
    OutputPageImpl *impl_ = impl_of(this);
    const QSignalBlocker blocker(impl_->preset_combo);
    const QString current =
        impl_->preset_combo->currentIndex() >= 0
            ? impl_->preset_combo->itemData(impl_->preset_combo->currentIndex(), Qt::UserRole)
                  .toString()
            : QString();
    impl_->preset_combo->clear();
    // 0.4.2 / 需求 3（A13）：条目只 = 真实预设文件；**不设「未选择预设」幽灵占位项**。
    // 无绑定时 combo 无选中项，占位文本 = 「当前配置未保存为预设」（诚实状态说明，不是可选态）。
    for (const auto &[path, name] : presets) {
        impl_->preset_combo->addItem(name, path);
        impl_->preset_combo->setItemData(impl_->preset_combo->count() - 1, name,
                                         kPresetBaseNameRole);
    }
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
    impl_->refresh_preset_combo_texts();
    impl_->sync_preset_buttons();
}

void PageOutput::set_current_preset(const QString &path) {
    OutputPageImpl *impl_ = impl_of(this);
    if (impl_->preset_combo == nullptr)
        return;
    {
        const QSignalBlocker blocker(impl_->preset_combo);
        int index = -1;
        for (int i = 0; i < impl_->preset_combo->count(); ++i) {
            if (impl_->preset_combo->itemData(i, Qt::UserRole).toString() == path) {
                index = i;
                break;
            }
        }
        impl_->preset_combo->setCurrentIndex(index);
        impl_->refresh_preset_combo_texts();
    }
    impl_->sync_preset_buttons();
}

void PageOutput::set_current_preset_dirty(bool dirty) {
    OutputPageImpl *impl_ = impl_of(this);
    if (impl_->preset_dirty == dirty)
        return;
    impl_->preset_dirty = dirty;
    if (impl_->preset_combo != nullptr) {
        const QSignalBlocker blocker(impl_->preset_combo);
        impl_->refresh_preset_combo_texts();
    }
    impl_->sync_preset_buttons();
}

bool PageOutput::current_preset_dirty() const {
    OutputPageImpl *impl_ = impl_of(this);
    return impl_->preset_dirty;
}

void PageOutput::set_preset_note(const QString &text) {
    OutputPageImpl *impl_ = impl_of(this);
    if (impl_->preset_note == nullptr)
        return;
    impl_->preset_note->set_full_text(text);
    impl_->preset_note->setVisible(!text.isEmpty());
}

// M7-R2.1-F4：线性光重采样开关读回（预览通道透传用；默认 false = 现行为）
bool PageOutput::linear_resample() const {
    OutputPageImpl *impl_ = impl_of(this);
    return impl_->linear_switch != nullptr && impl_->linear_switch->isChecked();
}

// R-319（M7-R2.1-F4）：镜像基准目录（opt-in；空 = 现行为）
QString PageOutput::mirror_base() const {
    OutputPageImpl *impl_ = impl_of(this);
    return impl_->base_edit != nullptr ? impl_->base_edit->text().trimmed() : QString();
}

void PageOutput::set_mirror_base(const QString &base) {
    OutputPageImpl *impl_ = impl_of(this);
    if (impl_->base_edit == nullptr || impl_->base_edit->text().trimmed() == base.trimmed())
        return;
    const QSignalBlocker blocker(impl_->base_edit); // 程序化不发信号（同组 set_* 口径）
    impl_->base_edit->setText(base);
}

QString PageOutput::output_template() const {
    OutputPageImpl *impl_ = impl_of(this);
    return impl_->template_text();
}

void PageOutput::set_output_template(const QString &tmpl) {
    OutputPageImpl *impl_ = impl_of(this);
    if (impl_->template_text() == tmpl)
        return;
    const QSignalBlocker blocker(impl_->template_edit);
    impl_->template_edit->setText(tmpl);
    impl_->sync_split_switch(tmpl);
    impl_->update_template_example();
}

bool PageOutput::split_by_format() const {
    OutputPageImpl *impl_ = impl_of(this);
    return OutputPageImpl::template_has_format(impl_->template_text());
}

void PageOutput::set_split_by_format(bool on) {
    OutputPageImpl *impl_ = impl_of(this);
    if (split_by_format() == on)
        return;
    const QSignalBlocker blocker(impl_->split_switch);
    impl_->split_switch->setChecked(on);
    impl_->apply_split_to_template(on);
}

// ===========================================================================
// M10-W3-T2：格式预设行 + 只读自适应面（契约 docs/v0.4.0-design.md §8.5「C23」/ §9「C24」；
// 追认 docs/m10-adjudications.md A9）。本段全部为**加性**实现（既有方法体零改动）。
//   * 文件 I/O / 校验 / 归一 / 二次确认归 MainWindow（单一持有者）—— 页面只收列表、发意图、
//     落卡、显示只读摘要；
//   * 卡值真源仍是 spec/form->values()（预设只是"写入器"，不引入第二份真源）；
//   * 会话态 = FormatState::format_preset_path / adaptive_*（不落 settings；设计 §8.5）。
// ===========================================================================
void OutputPageImpl::sync_format_preset_buttons(const std::string &id) {
    FormatState *st = state_of(id);
    if (st == nullptr)
        return;
    const bool has = !st->format_preset_path.isEmpty();
    const bool detached = st->preset_detached;
    // M12 需求 4（用户口径）：每个格式的出厂预设叫 default —— **不可删除**（UI 置灰 + tooltip；
    // I/O 层的同判定守卫是第二道），但**可「保存」直接覆盖**（= 直接修改 default.json）。
    // 「未选择」态已不存在：列表为空才会没有选中项（错误态，保留既有禁用语义）。
    const bool is_default = has && is_default_preset_path(st->format_preset_path);
    if (st->preset_save != nullptr) {
        // 来源 = 大预设（0.4.2 / 需求 3 / A13）：卡值没有绑定的格式预设文件 ⇒ 保存禁用
        // （否则会用大预设值覆盖 default.json / 旧来源文件），原因进 tooltip（不静默灰），
        // 指路「另存」= 唯一不会误覆盖别处的落盘路径。
        st->preset_save->setEnabled(!detached);
        if (detached) {
            const QString name = st->preset_global_name.isEmpty()
                                     ? PageOutput::tr("（未命名）")
                                     : PageOutput::tr("「%1」").arg(st->preset_global_name);
            st->preset_save->setToolTip(
                PageOutput::tr("当前卡值来自大预设%1：没有绑定的格式预设文件可覆盖；"
                               "要保存为格式预设请用「另存」")
                    .arg(name));
        } else if (has) {
            st->preset_save->setToolTip(
                is_default
                    ? PageOutput::tr("保存到出厂默认预设 default（直接修改该格式的 default.json；"
                                     "保存前先校验，不过不写盘）")
                    : PageOutput::tr(
                          "保存到当前格式预设（覆盖同名文件；保存前先校验，不过不写盘）"));
        } else {
            st->preset_save->setToolTip(
                PageOutput::tr("未选中格式预设：「保存」= 另存为新的格式预设"));
        }
    }
    if (st->preset_delete != nullptr) {
        st->preset_delete->setEnabled(has && !is_default);
        if (detached) {
            st->preset_delete->setToolTip(
                PageOutput::tr("当前卡值来自大预设：没有绑定的格式预设文件可删除"));
        } else if (!has) {
            st->preset_delete->setToolTip(
                PageOutput::tr("未选中格式预设：无可删除项（预设列表为空或数据目录不可用）"));
        } else if (is_default) {
            st->preset_delete->setToolTip(
                PageOutput::tr("出厂默认预设不可删除：可直接「保存」修改它，或用「另存」复制一份"));
        } else {
            st->preset_delete->setToolTip(PageOutput::tr("删除当前格式预设（会二次确认）"));
        }
    }
    if (st->preset_export != nullptr) {
        st->preset_export->setEnabled(true); // 导出 = 当前格式卡快照（无选中预设也可导出）
        st->preset_export->setToolTip(
            PageOutput::tr("把当前格式卡快照导出到任意路径（同名二次确认；导出前先校验）"));
    }
    if (st->preset_saveas != nullptr) {
        st->preset_saveas->setToolTip(
            PageOutput::tr("把当前格式卡快照另存为新的格式预设（命名后写入预设目录）"));
    }
    // 任何按钮态刷新都顺带同步下拉的可读面（弹层自适应宽 + tooltip 当前全名）。
    sync_format_preset_combo_view(st);
}

// 0.4.2 / 需求 3（A13）：来源显示重放（占位文本 / 注记 / 按钮态）——**不改会话态**。
// 只在「来源 = 大预设」时写占位与注记；绑定态/空态交给 set_format_preset_list /
// set_current_format_preset / 载入链（它们各自知道该写什么注记）。
void OutputPageImpl::sync_format_preset_source_display(const std::string &id) {
    FormatState *st = state_of(id);
    if (st == nullptr)
        return;
    if (st->preset_combo != nullptr) {
        const QSignalBlocker blocker(st->preset_combo);
        if (st->preset_detached) {
            st->preset_combo->setPlaceholderText(global_source_placeholder(st->preset_global_name));
            st->preset_combo->setCurrentIndex(-1); // 不再假装高亮 default
        } else {
            st->preset_combo->setPlaceholderText(QString());
        }
    }
    if (st->preset_detached) {
        update_format_preset_note(id,
                                  global_source_note(st->preset_global_name, st->preset_based_on));
    }
    sync_format_preset_buttons(id);
}

// 来源 = 大预设的唯一迁移点（进入）：清绑定 + 写显示字段 + 重放显示/按钮态。
// 注意：**不写 format_preset_path**（来源不是文件绑定），也不触发自动 default 载入
// （MainWindow 的 refresh_format_preset_row 看到 detached 即返回）。
void OutputPageImpl::set_format_preset_global_source_for(const std::string &id,
                                                         const QString &global_name,
                                                         const QString &based_on) {
    FormatState *st = state_of(id);
    if (st == nullptr)
        return;
    st->preset_detached = true;
    st->format_preset_path.clear();
    st->preset_global_name = global_name;
    st->preset_based_on = based_on;
    sync_format_preset_source_display(id);
}

void OutputPageImpl::apply_format_preset_to(const std::string &id,
                                            const pp::OutputFormatSpec &spec) {
    FormatState *st = state_of(id);
    if (st == nullptr || st->form == nullptr)
        return;
    st->spec = spec;
    st->spec.format_id = id;
    // 落卡走与全局预设同一条链（apply_state_to_form：后端/技术/位深/参数/保留键 __heif_profile）。
    // UX-10.1 的档位还原、位深限幅、avif+alpha 预选（R-066 口径：预设后端为权威值）一并复用。
    apply_state_to_form(id, st->spec);
    // M8-B2 规则上下文（位深）随预设位深重算 → 预检错误清单/页签徽标同步（不静默留旧错误）
    sync_rule_context(id);
    update_rule_errors(id);
    update_profile_note(id);
    refresh_tab_labels();
    // C23 §8.6：载入后卡头「已改 N」按**新值**重算（预设值 ≠ 表内默认 ⇒ 计数非零是如实的）
    refresh_changed_pill();
    emit q->config_changed();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：参数语义由名字固定
void OutputPageImpl::set_adaptive_policy_for(const std::string &id, const std::string &policy,
                                             bool enabled) {
    FormatState *st = state_of(id);
    if (st == nullptr)
        return;
    st->adaptive_policy = policy;
    // 无策略 ⇒ 不可能启用（避免「enabled=true + policy 空」的无效态落进预设/运行配置）。
    st->adaptive_enabled = enabled && !policy.empty();
    update_adaptive_row(id);
}

// 只读自适应面（C24：GUI **无编辑器**）。摘要三要素 = 目标分 / 最多轮数 / 阶段数，解析单源 =
// core/adaptive（与执行器/校验同一份 AST）。解析或校验失败 → 行内中文错误 + 开关禁用
// （不静默套默认、不静默关闭）；策略为空 → 整行不显示（默认 = 关闭，不占版面）。
void OutputPageImpl::update_adaptive_row(const std::string &id) {
    FormatState *st = state_of(id);
    if (st == nullptr || st->adaptive_row == nullptr)
        return;
    if (st->adaptive_policy.empty()) {
        st->adaptive_parse_ok = true;
        st->adaptive_error.clear();
        if (st->adaptive_summary != nullptr) {
            st->adaptive_summary->set_full_text(QString());
            st->adaptive_summary->setToolTip(QString());
        }
        if (st->adaptive_switch != nullptr) {
            const QSignalBlocker blocker(st->adaptive_switch);
            st->adaptive_switch->setChecked(false);
        }
        st->adaptive_row->setVisible(false);
        return;
    }
    pp::AdaptivePolicy ast;
    pp::AdaptiveError err;
    const bool ok = pp::parse_and_validate_adaptive_policy(st->adaptive_policy, id, ast, &err);
    st->adaptive_parse_ok = ok;
    QString text;
    QString tip;
    if (ok) {
        QStringList stages;
        stages.reserve(static_cast<qsizetype>(ast.stages.size()));
        for (const pp::AdaptiveStage &stage : ast.stages)
            stages << QString::fromStdString(stage.id);
        const QString target = QString::number(ast.target, 'g', 6);
        text = PageOutput::tr("目标 %1 分 / 最多 %2 轮 / %3 个阶段")
                   .arg(target)
                   .arg(ast.max_rounds)
                   .arg(static_cast<int>(ast.stages.size()));
        tip = PageOutput::tr(
                  "只读摘要（策略只存在格式预设文件里，GUI 不提供编辑器）：目标分 %1 / 最多 %2 轮 "
                  "/ %3 个阶段（%4）；未达标的输出按策略输出最后一轮。自适应需要评分 —— 即使设置里"
                  "关闭评分，携带策略的输出仍会计算评分。关闭开关 = 本次不启用（预设文件不改）")
                  .arg(target)
                  .arg(ast.max_rounds)
                  .arg(static_cast<int>(ast.stages.size()))
                  .arg(stages.isEmpty() ? PageOutput::tr("—") : stages.join(PageOutput::tr("、")));
        st->adaptive_error.clear();
    } else {
        text = PageOutput::tr("自适应策略无法解析（第 %1 行第 %2 列）：%3")
                   .arg(err.line)
                   .arg(err.col)
                   .arg(QString::fromStdString(err.message));
        tip = text;
        if (!err.hint.empty())
            tip += QLatin1Char('\n') + QString::fromStdString(err.hint);
        if (!err.code.empty())
            tip += QLatin1Char('\n') +
                   PageOutput::tr("错误码：%1").arg(QString::fromStdString(err.code));
        tip += QLatin1Char('\n') +
               PageOutput::tr("修正后重新载入预设；「启用自适应」已禁用（不会静默按默认策略运行）");
        st->adaptive_error = text;
    }
    // 需求 3「DSL 可读」的 GUI 只读呈现（Lead 口径）：摘要 tooltip 末尾追加**策略原文**
    // （真实换行；策略是行式 DSL、可含中文注释与多个 stage，QToolTip 多行显示）——
    // 悬停即可读到完整策略文本，不必去翻预设文件。成功/错误两条分支都追加：
    // 坏策略时用户正需要对照原文定位（错误行号来自同一份解析）。
    if (!st->adaptive_policy.empty()) {
        tip += PageOutput::tr("\n\n—— 策略原文（只读，来自格式预设文件）——\n") +
               QString::fromStdString(st->adaptive_policy).trimmed();
    }
    if (st->adaptive_summary != nullptr) {
        // 错误态走 ppInvalid 红字轨（theme::field_qss）；动态属性变更需重新 polish 才生效
        st->adaptive_summary->setProperty("ppInvalid", !ok);
        if (QStyle *style = st->adaptive_summary->style()) {
            style->unpolish(st->adaptive_summary);
            style->polish(st->adaptive_summary);
        }
        st->adaptive_summary->set_full_text(text);
        st->adaptive_summary->setToolTip(tip);
    }
    if (st->adaptive_switch != nullptr) {
        const QSignalBlocker blocker(st->adaptive_switch);
        st->adaptive_switch->setChecked(ok && st->adaptive_enabled);
    }
    if (st->adaptive_switch != nullptr) {
        st->adaptive_switch->setEnabled(ok);
        st->adaptive_switch->setToolTip(
            ok ? PageOutput::tr("开 = 本次运行按该策略迭代参数；关 = 本次不启用（预设文件里的 "
                                "enabled 不改；下次载入仍按预设值）")
               : PageOutput::tr("策略无法解析：已禁用「启用自适应」（修正策略后重新载入预设）"));
    }
    st->adaptive_row->setVisible(true);
}

void OutputPageImpl::update_format_preset_note(const std::string &id, const QString &text) {
    FormatState *st = state_of(id);
    if (st == nullptr || st->preset_note == nullptr)
        return;
    st->preset_note->set_full_text(text);
    st->preset_note->setVisible(!text.isEmpty());
}

void PageOutput::set_format_preset_list(const QString &format_id,
                                        const std::vector<std::pair<QString, QString>> &presets) {
    OutputPageImpl *impl_ = impl_of(this);
    const std::string id = format_id.toStdString();
    FormatState *st = impl_->state_of(id);
    if (st == nullptr || st->preset_combo == nullptr)
        return;
    const QString session = st->format_preset_path;
    const bool detached = st->preset_detached;
    {
        const QSignalBlocker blocker(st->preset_combo);
        st->preset_combo->clear();
        // M12 需求 4：**不再有「未选择」占位项** —— 下拉只列真实预设文件（列表由 MainWindow
        // 喂入；列表空 = 数据目录不可用/无预设，属错误态，而不是一种可选的"未选择"状态）。
        for (const auto &[path, name] : presets)
            st->preset_combo->addItem(name, path);
        // 选中规则：
        //   ⓪ 来源 = 大预设（0.4.2 / 需求 3）：**无选中项** + 占位「来自大预设：<名>」——
        //      绝不假装高亮 default（卡值不是 default 的内容，M12 的视觉锚在语义上是撒谎）；
        //   ① 会话路径仍在列表里 → 保留选中（用户上次的选择不丢）；
        //   ② 否则（首启 / 新建格式卡 / 来源文件被外部删除）→ 选 default（文件名 stem 判定）；
        //   ③ 异常数据目录（无 default）→ 选第一项；列表空 → 无选中项。
        // ②③ 的选中项 = **待自动载入**的状态：会话路径必须清空，由
        // MainWindow::Impl::refresh_format_preset_row 静默 load（见 mainwindow_presets.cpp）。
        if (detached) {
            st->preset_combo->setCurrentIndex(-1);
            st->preset_combo->setPlaceholderText(global_source_placeholder(st->preset_global_name));
        } else {
            int index = -1;
            bool session_found = false;
            if (!session.isEmpty()) {
                for (int i = 0; i < st->preset_combo->count(); ++i) {
                    if (st->preset_combo->itemData(i, Qt::UserRole).toString() == session) {
                        index = i;
                        session_found = true;
                        break;
                    }
                }
            }
            if (!session_found) {
                index = default_preset_item_index(st->preset_combo);
                if (index < 0 && st->preset_combo->count() > 0)
                    index = 0;
                // 来源失效（被删/改名）或首启：清会话来源 → MainWindow 侧自动载入 default 并自愈。
                st->format_preset_path.clear();
            }
            st->preset_combo->setCurrentIndex(index);
            st->preset_combo->setPlaceholderText(QString());
        }
        // 需求 3：弹层自适应宽 + tooltip 全文（长名在 160px 里被 elide 时悬浮可读）
        sync_format_preset_combo_view(st);
    }
    // 注释位：
    //   * 来源 = 大预设 → 如实来源注记（刷新可能发生在导出等临时注记之后，需重放）；
    //   * 会话来源仍在 → 保留「来自格式预设」（自动载入成功后会写新注记）；
    //   * 列表空且无来源 → 清空（错误态）。
    if (detached) {
        impl_->update_format_preset_note(
            id, global_source_note(st->preset_global_name, st->preset_based_on));
    } else if (st->format_preset_path.isEmpty()) {
        impl_->update_format_preset_note(id, QString());
    }
    impl_->sync_format_preset_buttons(id);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：参数语义由名字固定
void PageOutput::set_current_format_preset(const QString &format_id, const QString &path) {
    OutputPageImpl *impl_ = impl_of(this);
    const std::string id = format_id.toStdString();
    FormatState *st = impl_->state_of(id);
    if (st == nullptr)
        return;
    st->format_preset_path = path;
    if (st->preset_combo != nullptr) {
        const QSignalBlocker blocker(st->preset_combo);
        // 0.4.2：无「未选择」占位项；空路径/找不到 = **无选中项**（不再高亮 default ——
        // 绑定失效时由 MainWindow 的刷新链自动载入 default 后写回真实路径，短暂无选中是诚实的）。
        int index = -1;
        if (!path.isEmpty()) {
            for (int i = 0; i < st->preset_combo->count(); ++i) {
                if (st->preset_combo->itemData(i, Qt::UserRole).toString() == path) {
                    index = i;
                    break;
                }
            }
        }
        st->preset_combo->setCurrentIndex(index);
        st->preset_combo->setPlaceholderText(
            st->preset_detached ? global_source_placeholder(st->preset_global_name) : QString());
    }
    // 需求 3：弹层自适应宽 + tooltip 全文（程序化高亮同样生效）
    sync_format_preset_combo_view(st);
    impl_->sync_format_preset_buttons(id);
}

void OutputPageImpl::set_format_preset_detached_for(const std::string &id, bool detached) {
    FormatState *st = state_of(id);
    if (st == nullptr)
        return;
    st->preset_detached = detached;
    if (detached) {
        // 兼容入口（M12 调用方）：只给「来源 = 大预设」标志而不给来源名 —— 占位/注记走
        // 「来自大预设 / 自定义参数」的通用措辞；主窗新路径改用
        // set_format_preset_global_source_for(id, 名, 基于) 写真实来源。
        st->format_preset_path.clear();
        sync_format_preset_source_display(id);
        return;
    }
    // 解除：来源名/基于信息随绑定链重建（载入成功由 format_preset_load_into 写注记）。
    st->preset_global_name.clear();
    st->preset_based_on.clear();
    if (st->preset_combo != nullptr) {
        const QSignalBlocker blocker(st->preset_combo);
        st->preset_combo->setPlaceholderText(QString());
    }
    sync_format_preset_buttons(id);
}

void PageOutput::set_format_preset_detached(const QString &format_id, bool detached) {
    OutputPageImpl *impl_ = impl_of(this);
    impl_->set_format_preset_detached_for(format_id.toStdString(), detached);
}

bool PageOutput::format_preset_detached(const QString &format_id) const {
    OutputPageImpl *impl_ = impl_of(this);
    const FormatState *st = impl_->state_of(format_id.toStdString());
    return st != nullptr && st->preset_detached;
}

// 0.4.2 / 需求 3（A13）：来源 = 大预设（真实来源名 + 基于的格式预设 stem）。
void PageOutput::set_format_preset_global_source(const QString &format_id,
                                                 const QString &global_name,
                                                 const QString &based_on) {
    OutputPageImpl *impl_ = impl_of(this);
    impl_->set_format_preset_global_source_for(format_id.toStdString(), global_name, based_on);
}

QString PageOutput::format_preset_global_source(const QString &format_id) const {
    OutputPageImpl *impl_ = impl_of(this);
    const FormatState *st = impl_->state_of(format_id.toStdString());
    return st != nullptr ? st->preset_global_name : QString();
}

QString PageOutput::format_preset_based_on(const QString &format_id) const {
    OutputPageImpl *impl_ = impl_of(this);
    const FormatState *st = impl_->state_of(format_id.toStdString());
    return st != nullptr ? st->preset_based_on : QString();
}

QString PageOutput::current_format_preset(const QString &format_id) const {
    OutputPageImpl *impl_ = impl_of(this);
    const FormatState *st = impl_->state_of(format_id.toStdString());
    return st != nullptr ? st->format_preset_path : QString();
}

void PageOutput::apply_format_preset(const pp::OutputFormatSpec &spec) {
    OutputPageImpl *impl_ = impl_of(this);
    // 目标格式 = spec 自带的 format_id（MainWindow 载入后已按当前页签校验）；空则取现役
    const std::string id = !spec.format_id.empty() ? spec.format_id : impl_->sel.active();
    if (id.empty())
        return;
    impl_->apply_format_preset_to(id, spec);
}

void PageOutput::set_format_preset_adaptive(const QString &format_id, const QString &policy,
                                            bool enabled) {
    OutputPageImpl *impl_ = impl_of(this);
    impl_->set_adaptive_policy_for(format_id.toStdString(), policy.toStdString(), enabled);
}

void PageOutput::set_format_preset_note(const QString &format_id, const QString &note) {
    OutputPageImpl *impl_ = impl_of(this);
    impl_->update_format_preset_note(format_id.toStdString(), note);
}

bool PageOutput::format_preset_adaptive_enabled(const QString &format_id) const {
    OutputPageImpl *impl_ = impl_of(this);
    const FormatState *st = impl_->state_of(format_id.toStdString());
    // 生效态：解析失败一律「不启用」（开关已禁用，不让坏策略静默参与运行）
    return st != nullptr && st->adaptive_parse_ok && st->adaptive_enabled;
}

QString PageOutput::format_preset_adaptive_policy(const QString &format_id) const {
    OutputPageImpl *impl_ = impl_of(this);
    const FormatState *st = impl_->state_of(format_id.toStdString());
    return st != nullptr ? QString::fromStdString(st->adaptive_policy) : QString();
}

} // namespace pp::ui
