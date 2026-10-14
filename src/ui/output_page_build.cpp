// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 输出页 4 卡控件束（output_page_build）（B.3 余量 TU 拆分产物；M7-R2.1-F3）
// 拆分口径 = 行为零变化（方法体自 page_output.cpp 逐字搬移；公开面逐位不变）。
#include "ui/page_output_impl.h"

namespace pp::ui {

void OutputPageImpl::build_ui() {
    auto *outer = new QVBoxLayout(q);
    outer->setContentsMargins(0, 0, 0, 0);

    // R-228：剩余磁盘查询去抖（输入停 400ms 后查一次；见 refresh_disk）
    disk_timer = new QTimer(q);
    disk_timer->setSingleShot(true);
    disk_timer->setInterval(400);
    QObject::connect(disk_timer, &QTimer::timeout, q, [this] { refresh_disk(); });

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
    // 滚动条常驻：内容高随所选格式变化会把竖滚动条"顶"出来/收回去，内容宽随之 ±15px，
    // 磁贴 2×4 栅格（列 stretch 等分）整体横移 —— 与 tile-zero-deform「勾选零形变」冲突，
    // 也是用户可见的整卡回流。常驻滚动条 ⇒ 内容宽恒定（与 page_meta / page_run 同口径）。
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    scroll->setWidget(content);

    v->addWidget(build_format_card(content));
    v->addWidget(build_global_card(content));
    v->addWidget(build_param_card(content));
    v->addWidget(build_preset_card(content));
    v->addStretch(1);
    // UX-11（M7-R2D2）：卡片防压缩 —— QScrollArea(widgetResizable) 会把 Preferred 件压向
    // minimumSizeHint，卡片按当前内容 sizeHint 钉下限；滚动条只在卡片整体超出时出现。
    for (QFrame *card : {format_card, global_card, param_card, preset_card})
        formkit::lock_card_size(card);
}

QFrame *OutputPageImpl::make_card(QWidget *parent, QVBoxLayout **body_out) {
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("pp-card"));
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    *body_out = v;
    return card;
}

OutputPageImpl::CardHeader OutputPageImpl::make_header(QWidget *card, const QString &title,
                                                       const QString &pill_name,
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

OutputPageImpl::Body OutputPageImpl::make_body(QWidget *card) {
    Body out;
    out.host = new QWidget(card);
    out.box = new QVBoxLayout(out.host);
    out.box->setContentsMargins(theme::Metrics::card_pad_x, theme::Metrics::card_pad_top,
                                theme::Metrics::card_pad_x, theme::Metrics::card_pad_bottom);
    out.box->setSpacing(0);
    return out;
}

QPushButton *OutputPageImpl::make_button(QWidget *parent, const QString &text,
                                         const QString &object_name, bool primary) {
    // R-022 余量（M7-R2D2）：按钮唯一构造点 = formkit::make_button（R4 高 26 + 指针手 +
    // accessibleName + ppFk button/primary 钩子）；本页只剩 objectName 与字号补挂。
    auto *button = static_cast<QPushButton *>(formkit::make_button(parent, text, primary));
    button->setObjectName(object_name);
    button->setFont(theme::font(theme::Typography::body_px, QFont::DemiBold));
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return button;
}

QWidget *OutputPageImpl::make_flex_field(QWidget *parent, const QList<QWidget *> &widgets) {
    auto *host = new QWidget(parent);
    auto *h = new QHBoxLayout(host);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(theme::Metrics::group_gap);
    // 控件一律**贴字段轨起点**（与其它行同轴），富余宽归行尾 stretch —— 旧写法给首个控件
    // stretch 1，单开关行的 30×16 开关被居中到字段中段，与「分文件夹」行的开关不同列
    // （debug/debug2.png 红框 3；layout-audit ② 的列错位）。
    for (QWidget *widget : widgets)
        h->addWidget(widget, 0);
    h->addStretch(1);
    return host;
}

QWidget *OutputPageImpl::build_format_card(QWidget *parent) {
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
        note->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkHint));
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
    mode_note->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkHint));
    mode_note->setFont(theme::font(theme::Typography::hint_px));
    mode_note->setMinimumHeight(formkit::kRowPitch);
    set_leading_indent(mode_note,
                       formkit::kLabelW + formkit::kColGap); // R-218：逻辑边距（对轨缩进）
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
            auto *tile = new formkit::CheckableCard(tiles_host);
            tile->setObjectName(QStringLiteral("pp-format-") + QLatin1String(kFormats[i].id));
            // UX-9（§16-1 口径）：磁贴只留「☑ 格式名」—— 原第二行文本（".jpg" 等扩展名）
            // 随内容变宽；去掉后宽度才能统一固定。
            tile->set_labels(PageOutput::tr(kFormats[i].label));
            tile->set_tokens(tokens);
            tile->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            grid->addWidget(tile, i / kFormatColumns, i % kFormatColumns);
            tiles[i] = tile;
        }
        // UX-9：磁贴固定宽 = 8 格里最宽的那个（"☑ 格式名" 宽度，与 CheckableCard::sizeHint
        // 同一算法 → 不会出现"名被裁"或"格子宽窄不一"）
        {
            int tile_w = 0;
            for (int i = 0; i < kFormatCount; ++i) {
                if (tiles[i] != nullptr)
                    tile_w = std::max(tile_w, tiles[i]->sizeHint().width());
            }
            for (int i = 0; i < kFormatCount; ++i) {
                if (tiles[i] != nullptr)
                    tiles[i]->setFixedWidth(tile_w);
            }
        }
        for (int c = 0; c < kFormatColumns; ++c)
            grid->setColumnStretch(c, 1);
        body.box->addWidget(tiles_host);
    }
    v->addWidget(body.host);
    update_format_count();
    return format_card;
}

QWidget *OutputPageImpl::build_global_card(QWidget *parent) {
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
        out_root_edit->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkInput));
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
    // R-319（M7-R2.1-F4）：镜像基准目录（opt-in；CLI --base 的 GUI 面）—— 留空 = 现行为
    // （各输入文件所在目录），非空 = 基准目录包含的输入按其折算 $dir 相对输出树。
    {
        base_edit = new QLineEdit(global_card);
        base_edit->setObjectName(QStringLiteral("pp-base-edit"));
        base_edit->setFixedHeight(theme::Metrics::input_h);
        base_edit->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkInput));
        base_edit->setPlaceholderText(PageOutput::tr("留空 = 各输入文件所在目录"));
        base_edit->setToolTip(
            PageOutput::tr("镜像路径基准目录（可选）：基准目录内的输入按相对路径镜像输出树；"
                           "留空 = 每个输入文件所在目录（现行为）。与 --dev 的 --base 同语义"));
        QWidget *field = make_flex_field(global_card, {base_edit});
        const formkit::Row row =
            formkit::frow(global_card, PageOutput::tr("基准目录"), field, FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-base-row"));
        body.box->addWidget(row.row);
        QObject::connect(base_edit, &QLineEdit::textChanged, q, [this](const QString &text) {
            emit q->mirror_base_changed(text.trimmed());
        });
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
        conflict_combo->addItem(PageOutput::tr("跳过"), static_cast<int>(pp::ConflictPolicy::Skip));
        conflict_combo->addItem(PageOutput::tr("覆盖"),
                                static_cast<int>(pp::ConflictPolicy::Overwrite));
        conflict_combo->setCurrentIndex(0);
        conflict_combo->setFixedHeight(theme::Metrics::combo_h);
        conflict_combo->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkCombo));
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
        color_combo->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkCombo));
        color_combo->setToolTip(PageOutput::tr(
            "保持原样 = 不改动嵌入的 ICC 色彩描述；其余档位按目标色域转换并写入新的 ICC。"
            "仅元数据模式下不生效（不做色彩转换）"));
        color_tip_base = color_combo->toolTip(); // R-318：禁用/启用切换时按基线恢复
        const formkit::Row row = formkit::frow(global_card, PageOutput::tr("色彩目标"), color_combo,
                                               FieldWidth::Standard);
        row.row->setObjectName(QStringLiteral("pp-color-row"));
        body.box->addWidget(row.row);
    }
    // M7-R2.1-F4：色彩四选项（UX-14 C1–C4 的 UI 面；能力类 opt-in，默认值 = 现行为）。
    // 口径与默认值见 docs/m7r2-plan.md §2（C1 色域映射 / C2 抖动 / C3 渲染意图 / C4 线性光）。
    {
        formkit::ComboSpec intent_spec;
        intent_spec.object_name = QStringLiteral("pp-intent-combo");
        intent_spec.accessible = PageOutput::tr("渲染意图");
        intent_combo = formkit::combo_box(global_card, intent_spec);
        // 文案进 160 标准轨（R2 同列同宽不许拉满）：「相对比色+黑点补偿」102px 墨迹可容，
        // 旧串「相对比色 + 黑点补偿（默认）」150px 被硬裁成「相对比色 + 黑点补M」
        // （debug/debug2.png 红框 2）。默认档标记移到行尾 10px 注释槽（R3：提示不占标签位），
        // 随选择同步显示/隐藏 —— 未发生的量不显示（§诚实显示）。
        intent_combo->addItem(PageOutput::tr("相对比色+黑点补偿"), 0);
        intent_combo->addItem(PageOutput::tr("感知"), 1);
        intent_combo->addItem(PageOutput::tr("绝对比色"), 2);
        intent_combo->addItem(PageOutput::tr("饱和度"), 3);
        intent_combo->setCurrentIndex(0);
        intent_combo->setFixedHeight(theme::Metrics::combo_h);
        intent_combo->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkCombo));
        intent_combo->setToolTip(PageOutput::tr(
            "色彩转换的渲染意图（UX-14 C3）：默认档 = 现行为（相对比色 + 黑点补偿）；"
            "感知/绝对/饱和为可选能力档。带 A2B/B2A 表的 ICC 才有可见差异"));
        const formkit::Row row =
            formkit::frow(global_card, PageOutput::tr("渲染意图"), intent_combo,
                          FieldWidth::Standard, QString(), PageOutput::tr("（默认）"));
        row.row->setObjectName(QStringLiteral("pp-intent-row"));
        if (row.note != nullptr) {
            QObject::connect(intent_combo, &QComboBox::currentIndexChanged, q,
                             [note = row.note](int index) {
                                 note->setText(index == 0 ? PageOutput::tr("（默认）") : QString());
                             });
        }
        body.box->addWidget(row.row);
    }
    {
        formkit::ComboSpec gamut_spec;
        gamut_spec.object_name = QStringLiteral("pp-gamut-combo");
        gamut_spec.accessible = PageOutput::tr("色域映射");
        gamut_combo = formkit::combo_box(global_card, gamut_spec);
        gamut_combo->addItem(PageOutput::tr("无（硬裁；默认）"), 0);
        gamut_combo->addItem(PageOutput::tr("软膝"), 1);
        gamut_combo->addItem(PageOutput::tr("Lab 明度收敛"), 2);
        gamut_combo->setCurrentIndex(0);
        gamut_combo->setFixedHeight(theme::Metrics::combo_h);
        gamut_combo->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkCombo));
        gamut_combo->setToolTip(PageOutput::tr(
            "目标色域外像素的映射策略（UX-14 C1）：默认「无」= 现行为（硬裁）；软膝/明度收敛为"
            "可选能力档，只处理越界像素、不动 alpha"));
        const formkit::Row row = formkit::frow(global_card, PageOutput::tr("色域映射"), gamut_combo,
                                               FieldWidth::Standard);
        row.row->setObjectName(QStringLiteral("pp-gamut-row"));
        body.box->addWidget(row.row);
    }
    {
        dither8_switch = new formkit::SwitchControl(global_card);
        dither8_switch->setObjectName(QStringLiteral("pp-dither8-switch"));
        dither8_switch->set_tokens(tokens);
        dither8_switch->setToolTip(PageOutput::tr(
            "8bit 输出的 TPDF 量化抖动（UX-14 C2，±0.5 LSB，确定性）：默认关 = 量化路径逐字"
            "不变；开启仅作用于 8bit 量化段，10/12/16bit 不受影响"));
        QWidget *field = make_flex_field(global_card, {dither8_switch});
        const formkit::Row row =
            formkit::frow(global_card, PageOutput::tr("8bit 抖动"), field, FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-dither8-row"));
        body.box->addWidget(row.row);
    }
    {
        linear_switch = new formkit::SwitchControl(global_card);
        linear_switch->setObjectName(QStringLiteral("pp-linear-switch"));
        linear_switch->set_tokens(tokens);
        linear_switch->setToolTip(PageOutput::tr(
            "预览/缩略图的线性光重采样（UX-14 C4，仅显示面）：默认关 = 预览重采样路径逐字不变；"
            "开启后降采样在线性光中进行（sRGB EOTF → 线性 → LANCZOS3 → 逆 "
            "EOTF），输出像素不受影响"));
        QWidget *field = make_flex_field(global_card, {linear_switch});
        const formkit::Row row =
            formkit::frow(global_card, PageOutput::tr("线性光重采样"), field, FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-linear-row"));
        body.box->addWidget(row.row);
    }
    // 色彩四选项变更 → config_changed（主窗据此同步预览通道 + 状态行；同组既有口径）
    QObject::connect(dither8_switch, &formkit::SwitchControl::toggled, q,
                     [this](bool) { emit q->config_changed(); });
    QObject::connect(linear_switch, &formkit::SwitchControl::toggled, q,
                     [this](bool) { emit q->config_changed(); });
    QObject::connect(intent_combo, &QComboBox::currentIndexChanged, q,
                     [this](int) { emit q->config_changed(); });
    QObject::connect(gamut_combo, &QComboBox::currentIndexChanged, q,
                     [this](int) { emit q->config_changed(); });
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
        text->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkSub));
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
        template_edit->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkInput));
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
        template_example->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkSub));
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

QWidget *OutputPageImpl::build_param_card(QWidget *parent) {
    QVBoxLayout *v = nullptr;
    param_card = make_card(parent, &v);
    // 修 2-22：页签内参数的归属由**结构**表达（位深/技术/无损/编码努力/视觉距离都在页签内），
    // 不再挂"页签内含位深与编码参数"这类常驻补丁文案。
    const CardHeader header = make_header(param_card, PageOutput::tr("格式参数"),
                                          QStringLiteral("pp-format-changed-pill"), QString());
    changed_pill = header.pill;
    v->addWidget(header.row);

    // UX-9（§16-1 口径）：页签**等宽** —— 宽度不随"· N 位"文案漂移（见 apply_tab_width）
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
            if (watched == tabs_ && (t == QEvent::Resize || t == QEvent::LayoutRequest ||
                                     t == QEvent::Show || t == QEvent::LayoutDirectionChange)) {
                QTabBar *bar = tabs_->tabBar();
                if (bar != nullptr && btn_ != nullptr) {
                    const QPoint org = bar->mapTo(tabs_, QPoint(0, 0));
                    const int y = org.y() + (bar->height() - btn_->height()) / 2;
                    // R-218（M7-R2E2）：按钮贴页签带的 leading 端（LTR=右 / RTL=左）；
                    // LTR 路径逐字节不变。
                    const bool rtl = tabs_->layoutDirection() == Qt::RightToLeft;
                    const int x = rtl ? org.x() : org.x() + bar->width() - btn_->width();
                    const QRect want(x, y, btn_->width(), btn_->height());
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

QWidget *OutputPageImpl::build_preset_card(QWidget *parent) {
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
        QWidget *field =
            make_flex_field(preset_card, {preset_combo, preset_saveas, preset_delete, preset_save});
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
        est_size->setToolTip(PageOutput::tr("未运行前不给估值（避免假读数）：完成后体积见运行页"));
        est_time->setToolTip(PageOutput::tr("未运行前不给估值（避免假读数）：完成后吞吐见运行页"));
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
        body.box->addWidget(
            make_row(PageOutput::tr("预计输出"), est_output, PageOutput::tr("预计体积"), est_size));
        body.box->addWidget(
            make_row(PageOutput::tr("预计耗时"), est_time, PageOutput::tr("剩余磁盘"), est_disk));
    }
    v->addWidget(body.host);
    update_estimate();
    refresh_disk();        // R-228：磁盘读数独立通道（初值一次）
    sync_preset_buttons(); // 初始无当前预设 → 保存/删除禁用并给出原因（修 2-25）
    return preset_card;
}

void OutputPageImpl::wire() {
    for (int i = 0; i < kFormatCount; ++i) {
        formkit::CheckableCard *tile = tiles[i];
        QObject::connect(tile, &QAbstractButton::toggled, q,
                         [this, i](bool on) { toggle_format(kFormats[i].id, on, /*user=*/true); });
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
        schedule_disk_refresh(); // R-228：statvfs 去抖，不逐键查询
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
        // R-189：现役切换只走 FormatSelection（越界/同值早退逐字等价）；投影单点。
        const std::string id = sel.at(index);
        if (id.empty() || !sel.set_active(id))
            return;
        reproject_format_selection();
        update_template_example();
        // R-133：format_changed 已随孤儿信号删除（现役切换通知走 config_changed，消费方
        // = mainwindow refresh_status，行为逐字不变）。
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

    // ---- UX-1 卡⑥⑦⑨（docs/ux-review-14.md §1 表）：一卡一 actions 构造函数（⑧在
    //      paramform 自身）；items 惰性构造 → 运行中置灰实时反映 lock_for_run 语义 ----
    attach_card_menu(format_card, [this] {
        contextmenu::FormatCardIntents in;
        in.check_all_formats = [this] {
            for (const FormatEntry &e : kFormats)
                if (!is_selected(e.id))
                    toggle_format(e.id, true, /*user=*/true);
        };
        in.clear_formats = [this] {
            for (const FormatEntry &e : kFormats)
                if (is_selected(e.id))
                    toggle_format(e.id, false, /*user=*/true);
        };
        in.restore_defaults = [this] {
            // 恢复默认格式 = §2.9.1 默认单选 jxl（0.3.0 默认面）
            for (const FormatEntry &e : kFormats) {
                const bool want = std::string(e.id) == "jxl";
                if (is_selected(e.id) != want)
                    toggle_format(e.id, want, /*user=*/true);
            }
        };
        in.running = !q->isEnabled();
        return contextmenu::format_card_actions(in);
    });
    attach_card_menu(global_card, [this] {
        contextmenu::GlobalCardIntents in;
        in.reset_defaults = [this] {
            {
                const QSignalBlocker blocker(conflict_combo);
                conflict_combo->setCurrentIndex(0); // 默认 自动加序号（§3.2）
            }
            {
                const QSignalBlocker blocker(color_combo);
                color_combo->setCurrentIndex(0); // 默认 保持原样
            }
            template_edit->setText(QStringLiteral("$format/$dir/$file")); // → 联动/信号
            emit q->config_changed();
        };
        in.browse_out_root = [this] { browse_button->click(); };
        in.running = !q->isEnabled();
        return contextmenu::global_card_actions(in);
    });
    attach_card_menu(preset_card, [this] {
        contextmenu::PresetCardIntents in;
        in.save_as = [this] { emit q->preset_saveas_requested(); };
        in.overwrite_save = [this] {
            const int index = preset_combo->currentIndex();
            const QString path =
                index >= 0 ? preset_combo->itemData(index, Qt::UserRole).toString() : QString();
            if (path.isEmpty())
                emit q->preset_saveas_requested(); // 未选预设 → 走命名保存
            else
                emit q->preset_save_requested(preset_combo->itemText(index));
        };
        in.delete_preset = [this] {
            const int index = preset_combo->currentIndex();
            const QString path =
                index >= 0 ? preset_combo->itemData(index, Qt::UserRole).toString() : QString();
            if (!path.isEmpty())
                emit q->preset_delete_requested(path);
        };
        in.running = !q->isEnabled();
        return contextmenu::preset_card_actions(in);
    });
}

void OutputPageImpl::attach_card_menu(
    QWidget *host, const std::function<std::vector<contextmenu::Item>()> &items) {
    if (host == nullptr)
        return;
    host->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(host, &QWidget::customContextMenuRequested, host,
                     [host, items](const QPoint &pos) {
                         QMenu *m = contextmenu::menu(host, items());
                         m->setAttribute(Qt::WA_DeleteOnClose);
                         m->popup(host->mapToGlobal(pos));
                     });
}

} // namespace pp::ui
