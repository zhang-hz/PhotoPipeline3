// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M4-W2-T11；M5-T5 控件面换锚）— 分类面板实现（设计 §6.2 / mockup 左栏）
//
// 见 classify_panel.h 的规格与落地口径。本 TU 的实现要点：
//   * A15（M5-T12）：单列行 → **2 列 kv-grid**（`grid-auto-rows:28px` + 列距 12 + 卡体内边距
//     12/8/12/4；两列等宽、单元按视觉序填格、「＋ 新建分类…」占下一个空位）—— 形态照 mockups-v2
//     的 meta.html 左栏；**chip 语义不变**（仍是展示件：无点击语义、键帽=热键、宽度只由类名决定）。
//   * 单元 = 自绘色点（DotLabel，零 QSS）+ 打标 chip（formkit::ChipButton：键帽+类名，固定宽）+
//     中性计数胶囊（formkit::PillBadge::count）—— M5-T5（1-11/1-13/1-14/A7）：
//       - chip 鼠标事件穿透（WA_TransparentForMouseEvents + NoFocus）：展示件，无点击语义，
//         右键菜单仍由**行**承载（唯一交互入口，语义零改动）；
//       - 计数 = 中性胶囊（禁蓝色实心）；chip 选中态 = `.on`（当前选中项的归属，§5.2 跟随口径）；
//       - 「＋ 新建分类…」= 文字链（无框 + accent 字；R4），高 = btn_h（A7），＋ 为矢量图标；
//       - 常量宽度只由（键帽 + 类名）决定（ChipButton.sizeHint），长名再按**单元可用宽**上限
//         截断（A15 的 2 列网格更窄；上限随栏宽重算，见 apply_cell_metrics）
//         —— 悬停/选中/锁定零宽度变化（§12-4）。
//   * 单元部件 contextMenuPolicy=CustomContextMenu → build_row_menu() + exec（不新增点击语义）；
//   * 「全部」= 保留虚拟分类（core/classify 的 kAllId）：计数 = 模型行数，网格首格；
//   * id 生成（新建）：名称 → ASCII  slug（[a-z0-9_-]）；无可用字符（中文名等）→ "class"；
//     再按注册表去重（-2/-3…）—— id 是持久化键，显示名可随时改；
//   * 颜色/热键对话框只作用于**已存在**的类；被注册表拒绝（热键冲突/保留 id）→ 无改动 +
//     状态提示（QMessageBox），绝不静默。
#include "ui/classify_panel.h"

#include <QAction>
#include <QApplication> // theme.h 依赖（qApp->style()）→ 必须先于 ui/theme.h
#include <QColor>
#include <QColorDialog>
#include <QCursor>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include "ui/filelistmodel.h"
#include "ui/formkit.h" // M5-T5：打标 chip = formkit::ChipButton；计数 = formkit::PillBadge
#include "ui/icons.h"   // M5-T3：「＋ 新建分类…」的 ＋ 一律矢量图标（C3）
#include "ui/theme.h"

namespace pp::ui {
namespace {

// 色点（mockup .cls-dot 9px 圆）；自绘，无 QSS，颜色来自类定义/保留色
class DotLabel : public QWidget {
public:
    explicit DotLabel(QWidget *parent = nullptr) : QWidget(parent) { setFixedSize(kDot, kDot); }
    void set_color(const QColor &c) {
        color_ = c;
        update();
    }
    static constexpr int kDot = 9;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color_);
        painter.drawEllipse(rect());
    }

private:
    QColor color_;
};

// —— 2 列 kv-grid 几何（A15：mockups-v2 形态，值逐条照 tokens.css `.kv-grid` 与 meta.html）——
//   `.kv-grid{grid-template-columns:1fr auto 1fr auto;grid-auto-rows:28px;gap:0 12px}` +
//   卡体 `padding:8px 12px 10px`；单元 = [色点 9][6px][名][弹性][计数胶囊]（`.kv-v` + `.dot`）。
// UX-6④（M7-WP4）：自拷字面量改引用公共常量（同值 → 几何零变化；单源 = formkit_layout /
// theme::Metrics）。kPadTop/kPadBottom 8/4 是 mockup card-b 的偏离值（评审改进方案建议改
// 10/12 对齐 R1），换值需同步重锚 classify 几何探针 —— 登记 build/m7-wp4/handoff.md 待裁。
constexpr int kRowHeight = formkit::kRowPitch;    // R1 行 pitch 28（= grid-auto-rows:28px）
constexpr int kGridColumns = 2;                   // 2 列 kv-grid（A15）
constexpr int kColGap = formkit::kColGap;         // 列距 12（tokens.css `gap:0 12px`）
constexpr int kCellGap = formkit::kInlineGap;     // 单元内间距 6（`.kv-v .dot{margin-right:6px}`）
constexpr int kPadX = theme::Metrics::card_pad_x; // 卡体内边距左右 12（mockup card-b）
constexpr int kPadTop = 8;
constexpr int kPadBottom = 4; // + 卡布局自身下边距 6 = 10（与 mockup 的 card-b 底边距对齐）
// 计数胶囊预留宽（3 位数字量级）：chip 宽上限按"单元 − 色点 − 间距 − 预留"算 → 计数从 9 涨到
// 100 也不改 chip 宽（§12-4 零形变；宽度只由类名决定）
constexpr int kCountReserve = 30;
// chip 宽下限（保证键帽 13 + 至少一个字形可见）
constexpr int kChipMinWidth = 44;

// 「全部」行的保留色点（mockup #c9c9d2，主题无关的灰）
const QColor kAllDot(0xc9, 0xc9, 0xd2);

// 新建分类的默认色轮（首色与 mockup 类色一致：精选绿 / 待定琥珀 / 废片红 / 扫描件紫）
const std::uint32_t kDefaultColors[] = {0x2E7D32u, 0xF9A825u, 0xC62828u, 0x7C4DFFu,
                                        0x0277BDu, 0x00897Bu, 0x6D4C41u, 0xAD1457u};

QString slugify(const QString &name) {
    QString out;
    for (const QChar &ch : name) {
        if (ch.isLetterOrNumber() && ch.unicode() < 128) {
            out.append(ch.toLower());
        } else if (ch == QLatin1Char('-') || ch == QLatin1Char('_') || ch == QLatin1Char(' ')) {
            if (!out.isEmpty() && !out.endsWith(QLatin1Char('-')))
                out.append(QLatin1Char('-'));
        }
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out;
}

} // namespace

struct ClassifyPanel::Impl {
    ClassifyPanel *panel = nullptr;
    pp::ClassRegistry *registry = nullptr;
    FileListModel *model = nullptr;
    theme::Tokens tokens;
    bool has_tokens = false;
    // 首次 set_tokens 之前的绘制档（= formkit 默认令牌；窗口未显示即会被 refresh_theme 覆盖）
    theme::Tokens fallback = formkit::default_tokens();
    const theme::Tokens &paint_tokens() const { return has_tokens ? tokens : fallback; }
    bool locked = false;
    QString filter_class_id; // R-143：当前过滤档（空 = 「全部」= 无过滤；`.on` 单义）
    // 单元部件（M5-T5：色点 + 打标 chip + 中性计数胶囊；A15：2 列 kv-grid 排布）；index 0 =
    // 「全部」
    struct Row {
        QWidget *widget = nullptr;
        QString id; // "" = 「全部」
        formkit::ChipButton *chip = nullptr;
        formkit::PillBadge *count = nullptr;
    };
    std::vector<Row> rows;
    QGridLayout *grid = nullptr; // 2 列 kv-grid（A15；单元行 28 / 列距 12）
    QPushButton *new_btn = nullptr;
    QTimer *refresh_timer = nullptr;

    void rebuild_rows();
    void apply_row_styles();
    void apply_row_style(int row);
    void schedule_refresh(); // 打标/文件变化 → 0ms 合并刷新（批量打标不做 N 次面板重建）
    // A15：按面板当前宽重算单元宽/chip 宽上限（resize 与重建后各一次；宽度只由类名决定，
    // 悬停/选中/锁定不改 → §12-4 在本面成立）
    void apply_cell_metrics();
    int cell_width() const {
        const int content = panel != nullptr ? panel->width() - 2 * kPadX : 0;
        return std::max(0, (content - kColGap * (kGridColumns - 1)) / kGridColumns);
    }
    int chip_max_width() const {
        return std::max(kChipMinWidth,
                        cell_width() - DotLabel::kDot - 3 * kCellGap - kCountReserve);
    }
    std::string id_at(int row) const {
        if (row <= 0)
            return {};
        if (registry == nullptr || std::size_t(row) > registry->classes.size())
            return {};
        return registry->classes[std::size_t(row) - 1].id;
    }
};

ClassifyPanel::ClassifyPanel(QWidget *parent) : QWidget(parent), impl_(std::make_unique<Impl>()) {
    Impl &d = *impl_;
    d.panel = this;
    setObjectName(QStringLiteral("pp-class-panel"));
    // A15：卡体 = 2 列 kv-grid（行 pitch 28、列距 12；卡体内边距 12/8/12/4 —— 与 mockup 的
    // `card-b{padding:8px 12px 10px}` 对齐，余下 6px 由分类卡布局承担）
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kPadX, kPadTop, kPadX, kPadBottom);
    layout->setSpacing(0);
    d.refresh_timer = new QTimer(this);
    d.refresh_timer->setSingleShot(true);
    d.refresh_timer->setInterval(0); // 合并同一轮事件里的多次变化（批量打标 = 1 次刷新）
    connect(d.refresh_timer, &QTimer::timeout, this, [this] { impl_->rebuild_rows(); });
    d.grid = new QGridLayout();
    d.grid->setContentsMargins(0, 0, 0, 0);
    d.grid->setHorizontalSpacing(kColGap);
    d.grid->setVerticalSpacing(0); // R1：行间无额外 gap（行 pitch 28 = grid-auto-rows）
    d.grid->setColumnStretch(0, 1);
    d.grid->setColumnStretch(1, 1);
    layout->addLayout(d.grid);
    d.rebuild_rows();
}

ClassifyPanel::~ClassifyPanel() = default;

void ClassifyPanel::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    impl_->apply_cell_metrics(); // A15：栏宽变化 → 单元/chip 重定尺（网格仍 2 列等宽）
}

void ClassifyPanel::set_tokens(const theme::Tokens &tokens) {
    impl_->tokens = tokens;
    impl_->has_tokens = true;
    impl_->apply_row_styles();
    // M5-T3：矢量图标随 tokens 重放（QIcon 取色不吃 QSS 的 color）；M5-T5：文字链取 accent
    if (auto *btn = findChild<QPushButton *>(QStringLiteral("pp-class-new")))
        icons::apply(btn, icons::Name::Add, tokens.accent, tokens.text3, 12);
}

void ClassifyPanel::set_registry(pp::ClassRegistry *registry) {
    impl_->registry = registry;
    impl_->rebuild_rows();
}

void ClassifyPanel::set_model(FileListModel *model) {
    Impl &d = *impl_;
    if (d.model == model)
        return;
    if (d.model != nullptr)
        disconnect(d.model, nullptr, this, nullptr);
    d.model = model;
    if (d.model != nullptr) {
        // 文件集合/归属变化 → 计数与高亮跟随（打标/删行/清空都经这两条信号）
        // 批量打标会对每行发一次 class_changed → 走 0ms 合并刷新，避免 N 次面板重建
        connect(d.model, &FileListModel::content_changed, this,
                [this] { impl_->schedule_refresh(); });
        connect(d.model, &FileListModel::class_changed, this,
                [this] { impl_->schedule_refresh(); });
    }
    impl_->rebuild_rows();
}

void ClassifyPanel::set_locked(bool locked) {
    impl_->locked = locked;
    for (Impl::Row &row : impl_->rows) {
        if (row.widget != nullptr)
            row.widget->setEnabled(!locked);
    }
    if (impl_->new_btn != nullptr)
        impl_->new_btn->setEnabled(!locked);
    if (!locked)
        impl_->apply_row_styles(); // 解锁后补一次计数/高亮（锁定期不跟随 content_changed）
}

bool ClassifyPanel::locked() const { return impl_->locked; }

void ClassifyPanel::set_filter_class_id(const QString &class_id) {
    if (impl_->filter_class_id == class_id)
        return;
    impl_->filter_class_id = class_id;
    impl_->apply_row_styles();
}

QString ClassifyPanel::filter_class_id() const { return impl_->filter_class_id; }

// ---------------------------------------------------------------------------
// 行构建 / 样式
// ---------------------------------------------------------------------------

void ClassifyPanel::Impl::rebuild_rows() {
    // 清旧格（含「＋ 新建分类…」的活动格）：**先 hide 再 deleteLater** —— deleteLater 要等事件
    // 循环回到主层才会真正析构，而本面板的重建由 0ms 定时器/信号驱动、冒烟走 processEvents
    // （不派发 DeferredDelete）→ 旧代部件会以**旧几何**继续可见并在同槽位叠画（实测伪影：
    // 分类首行的"双圆角框 + 双 ＋"）。hide() 让旧代立即退出可见面，析构仍走 deleteLater
    // （不在这里直接 delete：重建可能发生在行部件自己的信号发射栈内，立即析构 = 悬垂发送者）。
    while (QLayoutItem *item = grid->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            // R-183（M7-R2.1-F4，E2-11 修法前半）：重建协议与 deleteLater 解耦 —— 旧代退出
            // 可见面的**同时**断开其信号面（悬垂点击不再触发旧 handler）；析构仍走 deleteLater
            // （立即 delete 会在行部件自身信号发射栈内留下悬垂发送者，见上注）。
            QObject::disconnect(w, nullptr, panel, nullptr);
            w->deleteLater();
        }
        delete item;
    }
    rows.clear();

    // A15（mockups-v2 形态）：单元按**视觉序**填格（左→右、上→下），2 列 / 行 pitch 28
    int slot = 0;
    const auto place = [&](QWidget *cell) {
        grid->addWidget(cell, slot / kGridColumns,
                        slot % kGridColumns); // 宽度 = apply_cell_metrics
        ++slot;
    };

    const auto add_row = [&](const QString &id, const QString &label, const QString &keycap,
                             int count, const QColor &dot) {
        auto *row = new QWidget(panel);
        row->setObjectName(QStringLiteral("pp-class-row"));
        row->setFixedHeight(kRowHeight); // R1：行 pitch 28（= grid-auto-rows:28px）
        row->setContextMenuPolicy(Qt::CustomContextMenu);
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0); // 卡体内边距/列距由面板布局与网格承担
        lay->setSpacing(kCellGap);
        auto *dot_label = new DotLabel(row);
        dot_label->setObjectName(QStringLiteral("pp-class-dot"));
        dot_label->set_color(dot);
        // 打标 chip（formkit 唯一形态）：键帽 = 热键位（无热键 → 不画键帽）、文字 = 类名；
        // R-143：chip = **过滤档开关**（左键 = 按该类过滤/再点清除；右键菜单仍归**单元** ——
        // 按钮对 ContextMenu 事件不消费，沿父链落到行）；宽度只由类名决定（§12-4 零形变）；
        // 宽上限 = 单元可用宽（长名截断；全名/热键进 tooltip + accessibleName，§8/§11）
        auto *chip = new formkit::ChipButton(row);
        chip->setObjectName(QStringLiteral("pp-class-chip"));
        chip->set_key_text(keycap);
        chip->setText(label);
        chip->setFocusPolicy(Qt::NoFocus);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFixedWidth(std::min(chip->sizeHint().width(), chip_max_width()));
        chip->setToolTip(
            keycap.isEmpty()
                ? ClassifyPanel::tr("%1（点击按此类筛选）").arg(label)
                : ClassifyPanel::tr("%1 · 热键 %2（点击按此类筛选）").arg(label, keycap));
        chip->setAccessibleName(label); // §11：可读名
        chip->set_tokens(paint_tokens());
        {
            // R-143：点击 = 过滤档切换（当前档再点 / 「全部」= 清除过滤）；G5 运行中只读
            const QString &row_id = id;
            QObject::connect(chip, &QAbstractButton::clicked, panel, [this, row_id] {
                if (locked)
                    return;
                const QString next = (row_id == filter_class_id) ? QString() : row_id;
                filter_class_id = next;
                apply_row_styles();
                emit panel->class_filter_requested(next);
            });
        }
        // 计数 = 中性胶囊（§2.7：计数不表状态、禁蓝色实心；1-11/1-13）
        auto *cnt = formkit::PillBadge::count(row, count);
        cnt->setObjectName(QStringLiteral("pp-class-count"));
        cnt->set_tokens(paint_tokens());
        lay->addWidget(dot_label);
        lay->addWidget(chip);
        lay->addStretch(1); // 计数贴单元右缘（mockup 的 `1fr auto` 列对）
        lay->addWidget(cnt);
        const int index = int(rows.size());
        connect(row, &QWidget::customContextMenuRequested, panel,
                [this, index, row](const QPoint &pos) {
                    if (locked)
                        return; // G5：运行中只读（不弹菜单）
                    QMenu *menu = panel->build_row_menu(index);
                    menu->setAttribute(Qt::WA_DeleteOnClose, true);
                    menu->exec(row->mapToGlobal(pos));
                });
        place(row);
        rows.push_back(Row{row, id, chip, cnt});
    };

    // 首格 = 「全部」（保留虚拟分类；计数 = 模型行数；无热键 → 无键帽）
    const int total = model != nullptr ? int(model->size()) : 0;
    add_row(QString(), ClassifyPanel::tr("全部"), QString(), total, kAllDot);
    if (registry != nullptr) {
        for (const pp::ClassDef &c : registry->classes) {
            add_row(QString::fromStdString(c.id), QString::fromStdString(c.name),
                    c.hotkey != 0 ? QString(QChar(c.hotkey)) : QString(),
                    int(model != nullptr ? model->class_file_count(c.id) : 0),
                    QColor(QRgb(c.rgb & 0xFFFFFFu)));
        }
    }

    // 「＋ 新建分类…」= 文字链（R4「文字链无框」+ mockup `.btn.lnk`；高 = btn_h，裁定 A7）；
    // M5-T3 起 ＋ = 矢量图标。A15：位置 = 网格的**下一个空位**（与 mockup 的
    // `<button class="btn lnk">…</button>` 同格位，不再自占整行）。
    auto *btn = new QPushButton(ClassifyPanel::tr("新建分类…"), panel);
    btn->setObjectName(QStringLiteral("pp-class-new"));
    btn->setToolTip(ClassifyPanel::tr("新建分类…"));
    btn->setAccessibleName(ClassifyPanel::tr("新建分类…")); // §11
    icons::apply(btn, icons::Name::Add, paint_tokens().accent, paint_tokens().text3, 12);
    btn->setFixedHeight(theme::Metrics::btn_h); // 裁定 A7（R4 按钮高 26）
    btn->setEnabled(!locked);
    connect(btn, &QPushButton::clicked, panel, [this] {
        if (locked)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(panel, ClassifyPanel::tr("新建分类"),
                                                   ClassifyPanel::tr("分类名称："),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty())
            panel->new_class(name.trimmed());
    });
    auto *btn_cell = new QWidget(panel);
    btn_cell->setObjectName(QStringLiteral("pp-class-new-cell"));
    auto *btn_lay = new QHBoxLayout(btn_cell);
    btn_lay->setContentsMargins(0, 0, 0, 0);
    btn_lay->setSpacing(0);
    btn_lay->addWidget(btn);
    btn_lay->addStretch(1); // 文字链左对齐（mockup `style="text-align:left"`）
    place(btn_cell);
    new_btn = btn; // 供 set_locked/apply_row_styles 复用（成员持有，不额外父子关系）
    new_btn->setVisible(true);

    apply_cell_metrics(); // 宽度定尺（面板宽已知时；后续 resize 走 resizeEvent）
    // R-143：过滤档指向的类被删除 → 过滤档清算（否则无 chip 点亮、列表却只剩空集）
    if (!filter_class_id.isEmpty()) {
        bool found = false;
        for (const Row &entry : rows) {
            if (entry.id == filter_class_id) {
                found = true;
                break;
            }
        }
        if (!found) {
            filter_class_id.clear();
            emit panel->class_filter_requested(QString());
        }
    }
    apply_row_styles();
}

void ClassifyPanel::Impl::apply_cell_metrics() {
    if (panel == nullptr)
        return;
    const int cell = cell_width();
    if (cell <= 0)
        return; // 宽度未定（构造期）→ 保持自然布局，待 resize 后再定尺
    const int chip_cap = chip_max_width();
    // 单元（行 + 新建格）等宽重排（1fr/1fr 的 Qt 等效：两列同宽 → 跨行同轴）
    for (int i = 0; i < grid->count(); ++i) {
        if (QWidget *cell_widget = grid->itemAt(i)->widget()) {
            if (cell_widget->width() != cell)
                cell_widget->setFixedWidth(cell);
        }
    }
    // chip 只按类名重取上限（宽度仍只由类名决定 → 悬停/选中/计数变化零形变，§12-4）
    for (Row &entry : rows) {
        if (entry.chip == nullptr)
            continue;
        const int want = std::min(entry.chip->sizeHint().width(), chip_cap);
        if (entry.chip->width() != want)
            entry.chip->setFixedWidth(want);
    }
}

void ClassifyPanel::Impl::apply_row_style(int row) {
    // R-143：`.on` = **过滤档**（「全部」.on ⇔ 无分类过滤）—— 单义，不再与"无分类/无选中"共用
    const bool active =
        row == 0 ? filter_class_id.isEmpty() : rows[std::size_t(row)].id == filter_class_id;
    Row &entry = rows[std::size_t(row)];
    if (entry.chip == nullptr)
        return;
    // chip 选中态换底/换字色，**不改宽**（§12-4）
    entry.chip->setChecked(active);
}

void ClassifyPanel::Impl::schedule_refresh() {
    if (locked)
        return; // 运行期只读：分类与文件集合都不变 → 计数无需跟随（避免运行期逐事件重扫）
    if (refresh_timer != nullptr)
        refresh_timer->start();
}

void ClassifyPanel::Impl::apply_row_styles() {
    if (!has_tokens)
        return;
    for (int i = 0; i < int(rows.size()); ++i) {
        Row &entry = rows[std::size_t(i)];
        // 计数实时重取（文件增删/打标后跟随）
        if (entry.count != nullptr) {
            const std::string id = id_at(i);
            const int value = id.empty() ? int(model != nullptr ? model->size() : 0)
                                         : int(model != nullptr ? model->class_file_count(id) : 0);
            entry.count->setText(QString::number(value));
            entry.count->set_tokens(tokens);
        }
        if (entry.chip != nullptr)
            entry.chip->set_tokens(tokens);
        apply_row_style(i);
    }
    if (new_btn != nullptr) {
        new_btn->setFont(theme::font(theme::Typography::body_px, 600));
        // 文字链（R4：无框 + accent 字；禁用 = --txt3）
        new_btn->setStyleSheet(
            QStringLiteral("QPushButton#pp-class-new { background:transparent; border:none; "
                           "color:%1; text-align:left; padding:0 %2px; }"
                           "QPushButton#pp-class-new:disabled { color:%3; }")
                .arg(theme::css_color(tokens.accent))
                .arg(theme::Metrics::pill_pad_x)
                .arg(theme::css_color(tokens.text3)));
    }
}

// ---------------------------------------------------------------------------
// 圈选（§6.2/D2）
// ---------------------------------------------------------------------------

void ClassifyPanel::check_class(const QString &class_id) {
    if (impl_->locked || impl_->model == nullptr)
        return;
    impl_->model->check_class(class_id.toStdString());
}

void ClassifyPanel::invert_class(const QString &class_id) {
    if (impl_->locked || impl_->model == nullptr)
        return;
    impl_->model->invert_class(class_id.toStdString());
}

void ClassifyPanel::clear_checks() {
    if (impl_->locked || impl_->model == nullptr)
        return;
    impl_->model->clear_checks();
}

// R-142：右键「清空该类勾选」—— 只清**该类**文件的勾（原第三项是全局清空，右键"废片"
// 会把"精选"的勾也清掉；前两项都带"该类"限定、第三项没有，语义自相矛盾）。
void ClassifyPanel::clear_class_checks(const QString &class_id) {
    if (impl_->locked || impl_->model == nullptr)
        return;
    impl_->model->clear_class_checks(class_id.toStdString());
}

// ---------------------------------------------------------------------------
// CRUD（§6.2）
// ---------------------------------------------------------------------------

bool ClassifyPanel::new_class(const QString &name, std::uint32_t rgb, char hotkey) {
    Impl &d = *impl_;
    if (d.locked || d.registry == nullptr || name.trimmed().isEmpty())
        return false;
    QString id = slugify(name);
    if (id.isEmpty())
        id = QStringLiteral("class");
    if (d.registry->find(id.toStdString()) != nullptr ||
        id == QString::fromUtf8(pp::ClassRegistry::kAllId.data(),
                                int(pp::ClassRegistry::kAllId.size()))) {
        QString candidate;
        for (int n = 2; n < 1000; ++n) {
            candidate = QStringLiteral("%1-%2").arg(id).arg(n);
            if (d.registry->find(candidate.toStdString()) == nullptr)
                break;
            candidate.clear();
        }
        id = candidate.isEmpty() ? id + QStringLiteral("-x") : candidate;
    }
    const std::uint32_t color =
        rgb != 0 ? rgb : kDefaultColors[d.registry->classes.size() % std::size(kDefaultColors)];
    if (!d.registry->add(pp::ClassDef{id.toStdString(), name.toStdString(), color, hotkey})) {
        emit error_occurred(tr("无法创建分类「%1」（名称/热键被占用）").arg(name));
        return false;
    }
    d.rebuild_rows();
    emit registry_changed();
    return true;
}

bool ClassifyPanel::rename_class(const QString &id, const QString &name) {
    Impl &d = *impl_;
    if (d.locked || d.registry == nullptr || name.trimmed().isEmpty())
        return false;
    if (!d.registry->rename(id.toStdString(), name.toStdString())) {
        emit error_occurred(tr("无法重命名（分类不存在或名称为空）"));
        return false;
    }
    d.rebuild_rows();
    emit registry_changed();
    return true;
}

bool ClassifyPanel::set_class_color(const QString &id, std::uint32_t rgb) {
    Impl &d = *impl_;
    if (d.locked || d.registry == nullptr)
        return false;
    if (!d.registry->set_color(id.toStdString(), rgb)) {
        emit error_occurred(tr("无法设置颜色（分类不存在）"));
        return false;
    }
    d.rebuild_rows();
    emit registry_changed();
    return true;
}

bool ClassifyPanel::set_class_hotkey(const QString &id, char key) {
    Impl &d = *impl_;
    if (d.locked || d.registry == nullptr)
        return false;
    if (!d.registry->set_hotkey(id.toStdString(), key)) {
        // 热键唯一（core/classify 口径）：冲突/非法（'0' 保留作清除）一律拒绝
        emit error_occurred(tr("热键不可用（已被其它分类占用或非法）"));
        return false;
    }
    d.rebuild_rows();
    emit registry_changed();
    return true;
}

bool ClassifyPanel::delete_class(const QString &id) {
    Impl &d = *impl_;
    if (d.locked || d.registry == nullptr)
        return false;
    if (!d.registry->erase(id.toStdString())) {
        emit error_occurred(tr("无法删除（分类不存在）"));
        return false;
    }
    d.rebuild_rows();
    emit registry_changed();
    return true;
}

// ---------------------------------------------------------------------------
// 右键菜单（唯一交互入口）
// ---------------------------------------------------------------------------

QMenu *ClassifyPanel::build_row_menu(int row) {
    Impl &d = *impl_;
    auto *menu = new QMenu(this);
    const std::string id = d.id_at(row);
    const QString class_id = QString::fromStdString(id);
    const bool is_all = id.empty();

    // §6.2/D2 圈选三项（「全部」行同样可用：「全部」= 全部文件）
    QAction *a_all = menu->addAction(tr("全选该类"));
    QAction *a_inv = menu->addAction(tr("反选该类"));
    // R-142：第三项改「清空该类勾选」—— 与前两项同一条"该类"限定（原为全局清空）。
    QAction *a_clear = menu->addAction(tr("清空该类勾选"));
    connect(a_all, &QAction::triggered, this, [this, class_id] { check_class(class_id); });
    connect(a_inv, &QAction::triggered, this, [this, class_id] { invert_class(class_id); });
    connect(a_clear, &QAction::triggered, this, [this, class_id] { clear_class_checks(class_id); });

    if (!is_all) {
        menu->addSeparator();
        QAction *a_rename = menu->addAction(tr("重命名…"));
        QAction *a_color = menu->addAction(tr("颜色…"));
        QAction *a_hotkey = menu->addAction(tr("热键…"));
        QAction *a_del = menu->addAction(tr("删除"));
        connect(a_rename, &QAction::triggered, this, [this, class_id] {
            // R-263：预填**显示名**（原预填 id —— 用户看到的是 "pick" 而不是"精选"）。
            const pp::ClassDef *def = impl_->registry != nullptr
                                          ? impl_->registry->find(class_id.toStdString())
                                          : nullptr;
            const QString current = def != nullptr ? QString::fromStdString(def->name) : class_id;
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("重命名分类"), tr("新名称："),
                                                       QLineEdit::Normal, current, &ok);
            if (ok && !name.trimmed().isEmpty())
                rename_class(class_id, name.trimmed());
        });
        connect(a_color, &QAction::triggered, this, [this, class_id] {
            const pp::ClassDef *def = impl_->registry != nullptr
                                          ? impl_->registry->find(class_id.toStdString())
                                          : nullptr;
            const QColor initial =
                def != nullptr ? QColor(QRgb(def->rgb & 0xFFFFFFu)) : QColor(Qt::gray);
            const QColor chosen = QColorDialog::getColor(initial, this, tr("分类颜色"));
            if (chosen.isValid())
                set_class_color(class_id, std::uint32_t(chosen.rgb() & 0xFFFFFFu));
        });
        connect(a_hotkey, &QAction::triggered, this, [this, class_id] {
            // 热键候选 = 1–9 + 无；已被他类占用者置灰（唯一性由注册表强制）
            QMenu submenu;
            QStringList labels{QStringLiteral("无")};
            QList<char> keys{0};
            for (char k = '1'; k <= '9'; ++k) {
                keys << k;
                QString owner;
                if (impl_->registry != nullptr) {
                    for (const pp::ClassDef &c : impl_->registry->classes) {
                        if (c.hotkey == k) {
                            owner = QString::fromStdString(c.name);
                            break;
                        }
                    }
                }
                labels << (owner.isEmpty() ? QString(QChar(k))
                                           : QStringLiteral("%1（%2）").arg(QChar(k), owner));
            }
            QList<QAction *> actions;
            for (const QString &label : labels)
                actions << submenu.addAction(label);
            for (int i = 0; i < actions.size(); ++i) {
                const char key = keys.at(i);
                if (key == 0 || impl_->registry == nullptr)
                    continue;
                for (const pp::ClassDef &c : impl_->registry->classes) {
                    if (c.hotkey == key && c.id != class_id.toStdString()) {
                        actions.at(i)->setEnabled(false);
                        break;
                    }
                }
            }
            QAction *chosen = submenu.exec(QCursor::pos());
            if (chosen == nullptr)
                return;
            const int index = actions.indexOf(chosen);
            if (index >= 0)
                set_class_hotkey(class_id, keys.at(index));
        });
        connect(a_del, &QAction::triggered, this, [this, class_id] {
            const pp::ClassDef *def = impl_->registry != nullptr
                                          ? impl_->registry->find(class_id.toStdString())
                                          : nullptr;
            const QString name = def != nullptr ? QString::fromStdString(def->name) : class_id;
            if (QMessageBox::question(
                    this, tr("删除分类"), tr("删除分类「%1」？该分类的归属将一并清除。").arg(name),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
                delete_class(class_id);
            }
        });
    }
    return menu;
}

// ---------------------------------------------------------------------------
// 冒烟/诊断读数
// ---------------------------------------------------------------------------

int ClassifyPanel::row_count() const { return int(impl_->rows.size()); }

QString ClassifyPanel::row_id(int row) const {
    if (row < 0 || row >= int(impl_->rows.size()))
        return {};
    return impl_->rows[std::size_t(row)].id;
}

QString ClassifyPanel::row_label(int row) const {
    if (row < 0 || row >= int(impl_->rows.size()))
        return {};
    // M5-T5：行名 = 打标 chip 的文字（唯一显示面）
    const formkit::ChipButton *chip = impl_->rows[std::size_t(row)].chip;
    return chip != nullptr ? chip->text() : QString();
}

int ClassifyPanel::row_count_value(int row) const {
    if (row < 0 || row >= int(impl_->rows.size()))
        return -1;
    const formkit::PillBadge *cnt = impl_->rows[std::size_t(row)].count;
    return cnt != nullptr ? cnt->text().toInt() : -1;
}

bool ClassifyPanel::row_active(int row) const {
    if (row < 0 || row >= int(impl_->rows.size()))
        return false;
    const formkit::ChipButton *chip = impl_->rows[std::size_t(row)].chip;
    return chip != nullptr && chip->isChecked(); // `.on` = chip 选中态（§5.2 跟随口径）
}

QStringList ClassifyPanel::hotkey_labels() const {
    QStringList out;
    if (impl_->registry == nullptr)
        return out;
    for (const pp::ClassDef &c : impl_->registry->classes) {
        if (c.hotkey == 0)
            continue;
        out << QStringLiteral("%1=%2").arg(QChar(c.hotkey)).arg(QString::fromStdString(c.name));
    }
    return out;
}

QWidget *ClassifyPanel::row_widget(int row) const {
    if (row < 0 || row >= int(impl_->rows.size()))
        return nullptr;
    return impl_->rows[std::size_t(row)].widget;
}

QWidget *ClassifyPanel::new_class_cell() const {
    QPushButton *btn = impl_->new_btn;
    return btn != nullptr ? btn->parentWidget() : nullptr;
}

} // namespace pp::ui
