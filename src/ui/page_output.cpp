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
#include "codecs/heif_profile.h" // UX-10.1：编码档位纯函数单源（WP2 I4）
#include "core/error_text.h"     // R-070/273：错误中文映射单源（M7-R2B；本包接线）
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

    // 行 0（仅 HEIF/AVIF）：编码档位（Profile）—— UX-10.1 核心行，置于位深行**之上**
    // （档位是因、位深是果）。标签带兼容性说明见 heif_profile_options 的选项文案。
    if (is_heif_family(id)) {
        formkit::ComboSpec prof_spec;
        prof_spec.object_name = QStringLiteral("pp-profile-combo");
        prof_spec.accessible = PageOutput::tr("编码档位");
        st.profile = formkit::combo_box(st.page, prof_spec);
        const bool avif = (id == "avif");
        int option_count = 0;
        const ProfileOption *options = heif_profile_options(avif, option_count);
        for (int i = 0; i < option_count; ++i)
            st.profile->addItem(PageOutput::tr(options[i].label),
                                static_cast<int>(options[i].profile));
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
    const bool lossless = st->form->selection().lossless;
    const pp::HeifProfile want = current_profile(id);
    std::vector<int64_t> allowed;
    QString reason = PageOutput::tr("【已禁用：当前编码档位不支持该色度】");
    if (lossless) {
        // 无损只有 4:4:4 成立：4:2:0/4:2:2 在编码前就丢掉色度，x265 的 bLossless 只保
        // YCbCr 平面，对 RGB 源仍是有损。限幅是**硬联动**（enc_heif 同样强制 444，预设/CLI
        // 也绕不过去）；强制改写当前值在 update_profile_note 里做（本函数只动可选性）。
        allowed.push_back(3);
        reason = PageOutput::tr("【已禁用：无损输出要求 4:4:4（色度不抽样）】");
    } else if (want != pp::HeifProfile::Auto) {
        std::vector<int> depths = profile_allowed_bitdepths(want, id == "avif");
        if (depths.empty())
            depths = {8, 10, 12};
        for (int c = 0; c <= 3; ++c) {
            for (const int d : depths) {
                if (pp::heif_profile_compatible(want, d, c, /*still=*/true, nullptr)) {
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
    // 无损 ⇒ 4:4:4（硬联动，与 enc_heif 同口径）：先把工作集里的 chroma 改写为 444，再让
    // 下面的档位推导/色度限幅按真实值求值。set_values 是程序化接口（不发 changed 信号），
    // 因此不会形成刷新环；改写后的值随 spec_of() 下发/落预设。
    const bool lossless = st->form != nullptr && st->form->selection().lossless;
    if (lossless && st->form != nullptr) {
        pp::ParamSet patch;
        patch[std::string("chroma")] = std::string("444");
        st->form->set_values(patch);
        // 4:4:4 需要 HEVC Main 4:4:4 / AV1 High·Professional，本档位下拉没有这些项；
        // 用户先前手选的 Main/Main10/... 与无损联动后必然不一致 → 回到「自动」（唯一能
        // 正确表达 4:4:4 的选择），避免开跑时才被编码器拒绝。用 signal blocker：这是联动
        // 归一，不是用户改档；随后本函数按 Auto 重新推导说明。
        if (st->profile != nullptr && st->profile->currentIndex() != 0) {
            const QSignalBlocker blocker(st->profile);
            st->profile->setCurrentIndex(0);
            st->profile_user_set = false;
        }
    }
    const pp::HeifProfile want = current_profile(id);
    const int depth = st->bitdepth != nullptr ? st->bitdepth->currentData().toInt() : 8;
    const int chroma = current_chroma(id);
    // 本输出恒为静止图（与 enc_heif.cpp 的反查同口径：still = true）
    const pp::HeifProfile derived = pp::derive_heif_profile(id == "avif", depth, chroma, true);
    QString derived_label = QString::fromStdString(pp::heif_profile_label(derived));
    const QString depth_tag = depth == 8 ? QString() : QString::number(depth);
    if (id == "heif") {
        // HEVC 的 4:4:4 / 4:2:2 / 4:0:0 分别是 Main 4:4:4 / Main 4:2:2 / Main 4:0:0 族；
        // derive_heif_profile 只按位深分档（x265 源码规则），这里按真实色度补正显示。
        // 10/12 位时档位名本身带位深（Main10 / Main12），不能一律写成 "Main 4:4:4"。
        if (chroma == 3)
            derived_label = PageOutput::tr("Main%1 4:4:4（HEVC 4:4:4）").arg(depth_tag);
        else if (chroma == 2)
            derived_label = PageOutput::tr("Main%1 4:2:2（HEVC 4:2:2）").arg(depth_tag);
        else if (chroma == 0)
            derived_label = PageOutput::tr("Main%1 4:0:0（HEVC 4:0:0）").arg(depth_tag);
    }
    const QString chroma_text = chroma == 3   ? QStringLiteral("4:4:4")
                                : chroma == 2 ? QStringLiteral("4:2:2")
                                : chroma == 0 ? QStringLiteral("4:0:0")
                                              : QStringLiteral("4:2:0");
    QString note;
    QString tooltip;
    NoteTone tone = NoteTone::Hint;
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
    } else {
        std::string err_zh;
        if (!pp::heif_profile_compatible(want, depth, chroma, true, &err_zh)) {
            note = QString::fromStdString(err_zh);
            tone = NoteTone::Error;
        } else {
            note = profile_compat_hint(want);
            if (!note.isEmpty() || chroma == 3)
                tone = NoteTone::Warn;
        }
    }
    if (lossless) {
        // 折叠行内只放短句；完整口径（YCbCr 域无损、大图 grid 分块、4:4:4 兼容性）进 tooltip。
        if (note.isEmpty())
            note = PageOutput::tr("无损 → 4:4:4");
        tooltip =
            PageOutput::tr("HEIF/AVIF 无损：强制 4:4:4（4:2:0/4:2:2 会抽样色度，不是无损）。"
                           "HEVC 的无损是 YCbCr 平面无损，RGB↔YCbCr 往返有舍入（不是逐位）；"
                           "超过 16 MP 时自动改用标准 HEIF grid 分块，保证 libde265/OIIO 能打开。"
                           "4:4:4 的查看器兼容性弱于 4:2:0。");
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
        st->form->set_values(spec.params); // R-265：原"二次落值"死码删除 —— set_values 幂等，
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
        // UX-10.1：还原编码档位（保留键 __heif_profile；缺省 = Auto）
        if (st->profile != nullptr) {
            const int want =
                int(pp::param_int(spec.params, std::string(pp::kHeifProfileParamKey), 0));
            const QSignalBlocker blocker(st->profile);
            for (int i = 0; i < st->profile->count(); ++i) {
                if (st->profile->itemData(i).toInt() == want) {
                    st->profile->setCurrentIndex(i);
                    break;
                }
            }
            st->profile_user_set = st->profile->currentIndex() != 0;
        }
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

void OutputPageImpl::sync_preset_buttons() {
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

    // 已选集合 = 预设的 outputs（规范顺序）；逐项落值
    // R-189：集合重建走 FormatSelection::reset_items（原 clear+push+sort 逐字等价；
    // 去重判据 = 新集合自身 —— 与原实现同键）
    std::vector<std::string> preset_ids;
    for (const pp::OutputFormatSpec &spec : preset.outputs) {
        if (pp::find_format(spec.format_id) == nullptr)
            continue;
        if (std::find(preset_ids.begin(), preset_ids.end(), spec.format_id) == preset_ids.end())
            preset_ids.push_back(spec.format_id);
    }
    impl_->sel.reset_items(std::move(preset_ids));
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

void PageOutput::set_preset_list(const std::vector<std::pair<QString, QString>> &presets) {
    OutputPageImpl *impl_ = impl_of(this);
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
    OutputPageImpl *impl_ = impl_of(this);
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

} // namespace pp::ui
