// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — run page: progress / throughput / cancel / summary (M1b U9)
//
// src/ui/page_run.h 是 PP-FROZEN 头文件，不含任何私有成员（§2.13 逐字节冻结），
// 因此页面状态保存在本翻译单元的注册表（键 = PageRun*），随 QObject::destroyed 释放。
// 行模型 PageRunRowModel 定义在本文件（§2.13：内部小模型不进头文件）。
#include "ui/page_run.h"

#include <QAbstractItemView>
#include <QAbstractListModel>
#include <QBrush>
#include <QColor>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLayout>
#include <QListView>
#include <QModelIndex>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QVector>
#include <cstdint>

namespace pp::ui {

namespace {

// ---------------------------------------------------------------- §3.1 12 态映射（唯一来源）
QString state_text(pp::FileState s) {
    switch (s) {
    case pp::FileState::Queued:
        return PageRun::tr("排队");
    case pp::FileState::Probing:
        return PageRun::tr("探测");
    case pp::FileState::Decoding:
        return PageRun::tr("解码");
    case pp::FileState::Orienting:
        return PageRun::tr("旋转");
    case pp::FileState::Coloring:
        return PageRun::tr("色彩");
    case pp::FileState::Flattening:
        return PageRun::tr("合成");
    case pp::FileState::Encoding:
        return PageRun::tr("编码");
    case pp::FileState::Writing:
        return PageRun::tr("写入");
    case pp::FileState::Done:
        return PageRun::tr("完成");
    case pp::FileState::Skipped:
        return PageRun::tr("跳过");
    case pp::FileState::Failed:
        return PageRun::tr("失败");
    case pp::FileState::Cancelled:
        return PageRun::tr("取消");
    }
    return QString();
}

QColor state_color(pp::FileState s) {
    switch (s) {
    case pp::FileState::Queued:
        return QColor(0x80, 0x80, 0x80); // #808080
    case pp::FileState::Probing:
    case pp::FileState::Decoding:
    case pp::FileState::Orienting:
    case pp::FileState::Coloring:
    case pp::FileState::Flattening:
    case pp::FileState::Encoding:
    case pp::FileState::Writing:
        return QColor(0x00, 0x88, 0xcc); // #0088cc
    case pp::FileState::Done:
        return QColor(0x22, 0xaa, 0x22); // #22aa22
    case pp::FileState::Skipped:
        return QColor(0x99, 0x99, 0x99); // #999999
    case pp::FileState::Failed:
        return QColor(0xdd, 0x33, 0x33); // #dd3333
    case pp::FileState::Cancelled:
        return QColor(0x99, 0x99, 0x99); // #999999
    }
    return QColor(0x80, 0x80, 0x80);
}

bool is_terminal(pp::FileState s) {
    return s == pp::FileState::Done || s == pp::FileState::Skipped || s == pp::FileState::Failed ||
           s == pp::FileState::Cancelled;
}

// 行名：只显示文件名（调用方可能传完整路径）
QString row_name(const QString &name) {
    const QString base = QFileInfo(name).fileName();
    return base.isEmpty() ? name : base;
}

// ---------------------------------------------------------------- 数值文案
QString format_mb_s(double v) { return PageRun::tr("%1 MB/s").arg(v, 0, 'f', 1); }
QString format_avg_ms(double v) { return PageRun::tr("%1 ms/文件").arg(v, 0, 'f', 0); }

QString format_duration(double ms) {
    if (ms >= 1000.0)
        return PageRun::tr("%1 s").arg(ms / 1000.0, 0, 'f', 1);
    return PageRun::tr("%1 ms").arg(ms, 0, 'f', 0);
}

QString format_throughput(double mb_s, double avg_ms) {
    return format_mb_s(mb_s) + QStringLiteral(" · ") + format_avg_ms(avg_ms);
}

} // namespace

namespace detail {

// 运行页行模型（§2.13：内部小模型，不进头文件）
class PageRunRowModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit PageRunRowModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : static_cast<int>(rows_.size());
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size())
            return QVariant();
        const Row &r = rows_.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            return r.text;
        case Qt::ForegroundRole:
            return QBrush(r.color);
        case Qt::ToolTipRole:
            return r.error.isEmpty() ? QVariant() : QVariant(r.error);
        default:
            return QVariant();
        }
    }

    void set_rows(const QStringList &names) {
        beginResetModel();
        rows_.clear();
        rows_.reserve(names.size());
        for (const QString &n : names) {
            Row r;
            r.name = row_name(n);
            r.text = r.name + QStringLiteral(" —— ") + state_text(pp::FileState::Queued);
            r.color = state_color(pp::FileState::Queued);
            rows_.append(r);
        }
        endResetModel();
    }

    void set_state(int row, pp::FileState state, const QString &error) {
        if (row < 0 || row >= rows_.size())
            return;
        Row &r = rows_[row];
        r.text = r.name + QStringLiteral(" —— ") + state_text(state);
        r.color = state_color(state);
        r.error = error;
        const QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::ForegroundRole, Qt::ToolTipRole});
    }

    QString error_at(int row) const {
        if (row < 0 || row >= rows_.size())
            return QString();
        return rows_.at(row).error;
    }

private:
    struct Row {
        QString name, text, error;
        QColor color;
    };
    QVector<Row> rows_;
};

} // namespace detail

namespace {

// ---------------------------------------------------------------- 页面状态（冻结头无成员）
struct RunState {
    QProgressBar *progress = nullptr;
    QLabel *status = nullptr;
    QLabel *throughput = nullptr;
    QPushButton *cancel = nullptr;
    QStackedWidget *center = nullptr;
    QLabel *guide = nullptr;
    QListView *view = nullptr;
    detail::PageRunRowModel *model = nullptr;
    QGroupBox *summary_box = nullptr;
    QLabel *summary_text = nullptr;
    QPushButton *open_out = nullptr;
    QPushButton *open_logs = nullptr;
    QTimer *tick = nullptr;

    std::size_t total = 0, terminal = 0;
    std::size_t ok = 0, failed = 0, skipped = 0, cancelled = 0;
    std::uint64_t out_bytes = 0;
    bool running = false, finished = false, cancel_requested = false;
    QElapsedTimer clock;
    QString out_root;
};

QHash<const PageRun *, RunState *> &registry() {
    static QHash<const PageRun *, RunState *> store;
    return store;
}

RunState *state_of(const PageRun *page) { return registry().value(page, nullptr); }

QString status_text(const RunState &s) {
    if (s.cancel_requested) {
        return PageRun::tr("已取消：完成 %1 · 取消 %2")
            .arg(static_cast<qulonglong>(s.ok))
            .arg(static_cast<qulonglong>(s.cancelled));
    }
    if (s.running) {
        return PageRun::tr("进行中 · 完成 %1 · 失败 %2 · 跳过 %3")
            .arg(static_cast<qulonglong>(s.ok))
            .arg(static_cast<qulonglong>(s.failed))
            .arg(static_cast<qulonglong>(s.skipped));
    }
    if (s.finished) {
        return PageRun::tr("已完成：成功 %1 · 失败 %2 · 跳过 %3")
            .arg(static_cast<qulonglong>(s.ok))
            .arg(static_cast<qulonglong>(s.failed))
            .arg(static_cast<qulonglong>(s.skipped));
    }
    return PageRun::tr("排队中…");
}

// ---------------------------------------------------------------- 文案写入（M2-T22）
// QLabel 默认不省略号：文字超出自身 rect 就被硬裁切（画到一半的字形直接切掉）。而
// setText() → updateGeometry() 只**投递** LayoutRequest，布局要到下一个事件循环回合才重算，
// 所以"改文案"与"抓帧"落在同一回合时，QLabel 仍是旧几何：M2-T22 像素终审在 03-run.png
// 实测 runStatus width=189 < sizeHint=195（同一行 spacer 还空着 464px，不是空间不足），
// 末尾 "0" 被切掉右半边（墨迹止于 x=830=641+189）。
// 这里在文案变化后同步激活本页根布局 —— 这是唯一能消除"陈旧几何"的手段：给 QLabel 设
// QSizePolicy/minimumWidth 无用，因为 QLabel（非 wordWrap）的 minimumSizeHint() 本来就
// 等于 sizeHint()（实测 minHint=sizeHint=195），布局并没有压它，只是还没来得及重排。
// 同步激活保证任意时刻（含同一回合内 grab）width() ≥ fontMetrics().horizontalAdvance(text())。
void set_label_text(QLabel *label, const QString &text) {
    if (label->text() == text)
        return; // 文案未变：不触发多余的布局回合（500ms tick 高频路径）
    label->setText(text);
    QWidget *page = label->parentWidget();
    QLayout *layout = page != nullptr ? page->layout() : nullptr;
    if (layout != nullptr)
        layout->activate();
}

// 运行中：累计 out_bytes / QElapsedTimer（ns 分辨率，避免毫秒截断成 0 的除零退化）
void refresh_labels(const RunState &s) {
    set_label_text(s.status, status_text(s));
    const double ms = s.clock.isValid() ? static_cast<double>(s.clock.nsecsElapsed()) / 1.0e6 : 0.0;
    const double mb_s = ms > 0.0 ? (static_cast<double>(s.out_bytes) / 1.0e6) / (ms / 1000.0) : 0.0;
    const double avg_ms = (s.terminal > 0 && ms > 0.0) ? ms / static_cast<double>(s.terminal) : 0.0;
    set_label_text(s.throughput, format_throughput(mb_s, avg_ms));
}

void build_ui(PageRun *page, RunState *s) {
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    // 顶部：总进度 + 取消
    auto *top = new QHBoxLayout;
    s->progress = new QProgressBar(page);
    s->progress->setObjectName(QStringLiteral("runProgress"));
    s->progress->setFormat(PageRun::tr("第 %v / %m 个"));
    s->progress->setRange(0, 1);
    s->progress->setValue(0);
    s->progress->setMinimumWidth(240);
    top->addWidget(s->progress, 1);
    s->cancel = new QPushButton(PageRun::tr("取消"), page);
    s->cancel->setObjectName(QStringLiteral("runCancel"));
    s->cancel->setEnabled(false);
    s->cancel->setVisible(false); // visible ∧ enabled ⇔ is_running()
    // 隐藏时保留槽位：顶栏其余控件不因按钮显隐而重排（进度条占 stretch 侧）
    QSizePolicy cancel_policy = s->cancel->sizePolicy();
    cancel_policy.setRetainSizeWhenHidden(true);
    s->cancel->setSizePolicy(cancel_policy);
    top->addWidget(s->cancel);
    root->addLayout(top);

    // 状态 + 吞吐
    auto *info = new QHBoxLayout;
    s->status = new QLabel(PageRun::tr("排队中…"), page);
    s->status->setObjectName(QStringLiteral("runStatus"));
    info->addWidget(s->status);
    info->addStretch(1);
    s->throughput = new QLabel(QString(), page);
    s->throughput->setObjectName(QStringLiteral("runThroughput"));
    info->addWidget(s->throughput);
    root->addLayout(info);

    // 中部：引导文案 / 行列表
    s->center = new QStackedWidget(page);
    s->center->setObjectName(QStringLiteral("runCenter"));
    s->guide = new QLabel(PageRun::tr("点击主界面\"开始\"运行批处理"), s->center);
    s->guide->setObjectName(QStringLiteral("runGuide"));
    s->guide->setAlignment(Qt::AlignCenter);
    s->guide->setWordWrap(true);
    QPalette guide_pal = s->guide->palette();
    guide_pal.setColor(QPalette::WindowText, QColor(0x80, 0x80, 0x80));
    s->guide->setPalette(guide_pal);
    s->center->addWidget(s->guide);

    s->view = new QListView(s->center);
    s->view->setObjectName(QStringLiteral("runList"));
    s->view->setUniformItemSizes(true);
    s->view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    s->view->setSelectionMode(QAbstractItemView::SingleSelection);
    s->model = new detail::PageRunRowModel(s->view);
    s->view->setModel(s->model);
    s->center->addWidget(s->view);
    s->center->setCurrentWidget(s->guide);
    root->addWidget(s->center, 1);

    // 结束：摘要 + 两个按钮
    s->summary_box = new QGroupBox(PageRun::tr("摘要"), page);
    s->summary_box->setObjectName(QStringLiteral("runSummary"));
    auto *box = new QVBoxLayout(s->summary_box);
    s->summary_text = new QLabel(s->summary_box);
    s->summary_text->setObjectName(QStringLiteral("runSummaryText"));
    s->summary_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    box->addWidget(s->summary_text);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    s->open_out = new QPushButton(PageRun::tr("打开输出目录"), s->summary_box);
    s->open_out->setObjectName(QStringLiteral("runOpenOutput"));
    s->open_out->setEnabled(false);
    s->open_logs = new QPushButton(PageRun::tr("查看日志"), s->summary_box);
    s->open_logs->setObjectName(QStringLiteral("runOpenLogs"));
    s->open_logs->setEnabled(false);
    buttons->addWidget(s->open_out);
    buttons->addWidget(s->open_logs);
    box->addLayout(buttons);
    s->summary_box->setVisible(false);
    root->addWidget(s->summary_box);

    // 运行中吞吐估算刷新
    s->tick = new QTimer(page);
    s->tick->setInterval(500);
    QObject::connect(s->tick, &QTimer::timeout, page, [page, s]() {
        if (s->running)
            refresh_labels(*s);
    });

    QObject::connect(s->cancel, &QPushButton::clicked, page, [page, s]() {
        s->cancel_requested = true;
        refresh_labels(*s); // 已取消：完成 N · 取消 C
        emit page->cancel_requested();
    });
    QObject::connect(s->open_out, &QPushButton::clicked, page,
                     [page, s]() { emit page->open_output_requested(s->out_root); });
    QObject::connect(s->open_logs, &QPushButton::clicked, page,
                     [page]() { emit page->open_logs_requested(); });

    // 运行中双击行 → tooltip 显示 error（error 同时经 Qt::ToolTipRole 悬停可见）
    QObject::connect(s->view, &QListView::doubleClicked, page, [page, s](const QModelIndex &idx) {
        if (!idx.isValid())
            return;
        const QString err = s->model->error_at(idx.row());
        if (err.isEmpty())
            return;
        const QRect rect = s->view->visualRect(idx);
        QToolTip::showText(s->view->viewport()->mapToGlobal(rect.center()), err, s->view);
    });
}

} // namespace

PageRun::PageRun(QWidget *parent) : QWidget(parent) {
    Q_ASSERT(!registry().contains(this));
    RunState *s = new RunState;
    registry().insert(this, s);
    connect(this, &QObject::destroyed, this, [this]() { delete registry().take(this); });
    build_ui(this, s);
    reset(); // idle
}

void PageRun::begin_run(std::size_t total, const QStringList &names) {
    RunState *s = state_of(this);
    if (!s)
        return;
    reset(); // 重置一切（时钟/计数/摘要/按钮）
    s->total = total;
    s->running = true;
    s->model->set_rows(names);
    if (static_cast<std::size_t>(names.size()) != total) {
        qWarning("PageRun::begin_run: total=%llu but names=%lld",
                 static_cast<unsigned long long>(total), static_cast<long long>(names.size()));
    }
    s->progress->setRange(0, static_cast<int>(total));
    s->progress->setValue(0);
    s->progress->setVisible(true); // idle 隐藏，运行中显示
    s->cancel->setEnabled(true);   // visible ∧ enabled ⇔ is_running()
    s->cancel->setVisible(true);
    s->center->setCurrentWidget(s->view);
    s->summary_box->setVisible(false);
    s->clock.start();
    s->tick->start();
    refresh_labels(*s);
}

void PageRun::on_event(const pp::FileEvent &ev) {
    RunState *s = state_of(this);
    if (!s)
        return;
    const int row = static_cast<int>(ev.index);
    const QString error = ev.result ? QString::fromStdString(ev.result->error) : QString();
    s->model->set_state(row, ev.state, error);
    if (!is_terminal(ev.state))
        return;

    if (ev.result)
        s->out_bytes += ev.result->out_bytes; // 终态计入吞吐累计（不保存指针）
    switch (ev.state) {
    case pp::FileState::Done:
        ++s->ok;
        break;
    case pp::FileState::Skipped:
        ++s->skipped;
        break;
    case pp::FileState::Failed:
        ++s->failed;
        break;
    case pp::FileState::Cancelled:
        ++s->cancelled;
        break;
    default:
        break;
    }
    ++s->terminal;
    s->progress->setValue(static_cast<int>(s->terminal)); // 仅终态推进进度条
    refresh_labels(*s);
}

void PageRun::end_run(const pp::RunSummary &sum, const QString &out_root) {
    RunState *s = state_of(this);
    if (!s)
        return;
    s->tick->stop();
    s->running = false;
    s->finished = true;
    s->cancel->setEnabled(false);
    s->cancel->setVisible(false); // 完成态不残留「可取消」观感
    s->total = sum.total;
    s->ok = sum.ok;
    s->failed = sum.failed;
    s->skipped = sum.skipped;
    s->cancelled = sum.cancelled;
    s->terminal = sum.ok + sum.failed + sum.skipped + sum.cancelled;
    s->out_bytes = sum.out_bytes;
    s->out_root = out_root;
    if (sum.cancelled > 0)
        s->cancel_requested = true;

    s->progress->setRange(0, static_cast<int>(sum.total));
    s->progress->setValue(static_cast<int>(s->terminal));
    set_label_text(s->status, status_text(*s));
    set_label_text(s->throughput, format_throughput(sum.throughput_mb_s, sum.avg_file_ms));

    QStringList lines;
    lines << PageRun::tr("成功：%1").arg(static_cast<qulonglong>(sum.ok));
    lines << PageRun::tr("失败：%1").arg(static_cast<qulonglong>(sum.failed));
    lines << PageRun::tr("跳过：%1").arg(static_cast<qulonglong>(sum.skipped));
    lines << PageRun::tr("取消：%1").arg(static_cast<qulonglong>(sum.cancelled));
    lines << PageRun::tr("总耗时：%1").arg(format_duration(sum.total_ms));
    lines << PageRun::tr("吞吐：%1").arg(format_mb_s(sum.throughput_mb_s));
    lines << PageRun::tr("平均：%1").arg(format_avg_ms(sum.avg_file_ms));
    s->summary_text->setText(lines.join(QLatin1Char('\n')));
    s->summary_box->setVisible(true);
    s->open_out->setEnabled(true);
    s->open_logs->setEnabled(true);
}

void PageRun::reset() {
    RunState *s = state_of(this);
    if (!s)
        return;
    s->tick->stop();
    s->clock.invalidate();
    s->running = false;
    s->finished = false;
    s->cancel_requested = false;
    s->total = s->terminal = 0;
    s->ok = s->failed = s->skipped = s->cancelled = 0;
    s->out_bytes = 0;
    s->out_root.clear();
    s->model->set_rows(QStringList());
    s->progress->setRange(0, 1);
    s->progress->setValue(0);
    s->progress->setVisible(false); // idle：不显示误导性的「第 0 / 1 个」
    s->cancel->setEnabled(false);
    s->cancel->setVisible(false); // visible ∧ enabled ⇔ is_running()
    set_label_text(s->throughput, QString());
    s->summary_text->clear();
    s->summary_box->setVisible(false);
    s->open_out->setEnabled(false);
    s->open_logs->setEnabled(false);
    s->center->setCurrentWidget(s->guide);
    set_label_text(s->status, status_text(*s)); // 排队中…
}

bool PageRun::is_running() const {
    const RunState *s = state_of(this);
    return s != nullptr && s->running;
}

} // namespace pp::ui

#include "page_run.moc"
