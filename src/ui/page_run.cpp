// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M4-W3-T14）— 运行页：逐输出进度 + 总览 + 日志尾 + 运行期锁定（G5）
//
// 规格 = docs/v0.3.0-design.md §7（7.1 三级进度模型 / 7.2 阶段权重表 / 7.3 合成进度 /
//        7.4 节流与日志）+ §8.1（交错读数）+ §8.2（线程预算读数） + §9.1 骨架 +
//        §9.3「运行页：总览卡（大进度条 + 吞吐/并行/交错/线程预算只读 + 取消）→ 任务进度卡
//        （源文件分组行 + 逐输出子行）→ 日志尾卡（5 行滚动 + 跳转）。运行期锁定（G5）」+
//        §9.3 排版硬约束（70px 右对齐标签列 + 12px 列距 + 32px 行高；全页禁折行）+
//        docs/v0.3.0-consensus.md §3.4（逐文件进度）+ docs/mockups/run-dark.html（**精确
//        规格**：CSS 逐条）+ run-dark.png（视觉参照）。
//
// 三级进度（§7.1）在本页的落地口径：
//   * 运行级（大进度条 + 百分比）= Σ 输出完成度 / (文件数 × 格式数)；**输出完成度 = 该输出的
//     (0.5·enc + 0.1·meta) / 0.6**（= core/progress.cpp `ProgressMux::output_frac()` 的归一
//     口径），终态输出（成功/失败/跳过/取消都是终态）按"已结算"计 1.0 —— 与引擎同一份算术，
//     且与底栏「N 个输出」计数同源；
//   * 源文件级（分组行）= 状态标 + 文件名 + 「W×H · N 个输出」；
//   * 输出级（子行）= §7.2 权重表加权：`row_frac = shared + 0.5·enc_i + 0.1·meta_i`
//     （shared = Σ 四个共享段的 `StageWeights::stage_weight(s) × frac` ≤ 0.40）。故"编码中"
//     的行填色 = 0.4 + 0.5·enc，与 mockup 的数值自洽（0.4 + 0.5×0.773 ≈ 0.79）。
//
// 行文案（§3.4「进度条 + 百分比 + 阶段文字（解码/色彩/编码+行数/写入/完成/失败）」）：
//   * 未启动且**尚无输出启动**（源文件处于共享段）→ 首个未完成输出行承载共享段进度与阶段
//     文字（mockup：`解码 → 色彩` + 18%；0.03+0.27×0.67≈0.21 量级），其余行 `排队中`
//     （交错等待时首行 `排队中（交错启动）`）；
//   * 编码段 → `编码 1980/2560 行`（行数 = round(stage_frac×height)，与引擎
//     `progress_max_row` 同一折算基数）；合成进度（§7.3）→ `编码（合成进度）` + **斜纹**；
//     高度未知 → `编码 NN%`；
//   * 完成行 = 产物事实（`412 KB` + `3.1× · 2.4s`）；失败行 = **直显原因**
//     （`OutputResult::error`；文件级失败回落到 `FileResult::error`）；跳过/取消各自成态。
//
// 合成进度斜纹（§7.3）：`ProgressInfo::synthetic == true` → 8px 迷你条画 115°/6px 周期 12px
//   双色带（#3aa3dc → #4cc2ff；theme tokens `progress_fill_from/to` + `Metrics::synth_stripe_*`），
//   与 mockup `.obar i.synth{repeating-linear-gradient(115deg,…)}` 同几何。
//
// 节流（§7.4「GUI 节流 60ms 刷新」）：事件回调只写内存行状态并置脏位，控件写入由 60ms 定时器
//   统一 flush（含"已用时/吞吐/当前分配"这类时钟读数）。行控件仅在语义变化时 setText/
//   setStyleSheet（ElidedLabel/MiniBar 内部对同值早退）。
//
// 运行期锁定（G5）：本页运行中只有「取消运行」可交互（**唯一取消入口** = M1b R1 裁定沿用）；
//   跳转入口（查看日志…/打开输出目录）保持可用；结束态取消钮消失。左栏/步骤钮/设置/预设/预览/
//   分类的置灰在 `MainWindow::lock_for_run()`（T14 接锁定逻辑 + 探针）。
//
// 页内状态保存在本翻译单元的注册表（键 = PageRun*，随 QObject::destroyed 释放）—— page_run.h
// 是 PP-FROZEN 头文件，不含私有成员（§2.13）；行控件亦然（不进头文件）。本文件无 Q_OBJECT 类
// （控件类不发信号），故不含 *.moc 包含（与 page_meta.cpp / page_output.cpp 同口径）。
#include "ui/page_run.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include <QApplication> // theme.h 的 style_mirrors_system_accent() 用 qApp->style()
#include <QColor>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QtGlobal>
#include <cstdint>

#include "core/progress.h" // StageWeights（§7.2 单源）
#include "ui/theme.h"

namespace pp::ui {
namespace {

using pp::ProgressInfo;
using pp::Stage;
using pp::StageWeights;

constexpr int kFlushMs = 60;     // §7.4：GUI 节流 60ms 刷新
constexpr int kLogTailLines = 5; // §9.3：日志尾 5 行滚动

// ===========================================================================
// 小控件
// ===========================================================================

// 单行省略标签（§9.3「全界面禁止文案折行（超长省略号）」；与 page_meta/page_output 同口径）
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) { setWordWrap(false); }
    void set_full_text(const QString &text) {
        if (full_text_ == text)
            return; // 同值早退（60ms flush 的高频路径）
        full_text_ = text;
        setProperty("ppFullText", text); // 探针/走查读语义值（显示文本可能被省略成 "…"）
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

// 迷你进度条（§9.2：8px r=99；实心=真实、斜纹=合成、绿=完成、红=失败）
// 自绘：QSS 无法表达 repeating-linear-gradient 斜纹，且像素级取证需要可 grab 的独立控件。
// 语义读数同时挂动态属性（ppBarKind/ppBarSynthetic/ppBarFrac）供探针/走查读取。
class MiniBar : public QWidget {
public:
    enum class Kind { Pending, Active, Done, Failed, Skipped };

    explicit MiniBar(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedHeight(theme::Metrics::mini_progress_px);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void set_tokens(const theme::Tokens &t) {
        tokens_ = t;
        update();
    }

    void set_state(Kind kind, float frac, bool synthetic) {
        const float clamped = std::clamp(frac, 0.f, 1.f);
        const bool synth = synthetic && kind == Kind::Active; // 终态不画斜纹（§7.3 只描述进行中）
        if (kind_ == kind && frac_ == clamped && synthetic_ == synth)
            return; // 同值早退
        kind_ = kind;
        frac_ = clamped;
        synthetic_ = synth;
        setProperty("ppBarKind", QString::fromLatin1(kind_name(kind_)));
        setProperty("ppBarSynthetic", synthetic_);
        setProperty("ppBarFrac", static_cast<double>(frac_));
        update();
    }

    Kind kind() const { return kind_; }
    float frac() const { return frac_; }
    bool synthetic() const { return synthetic_; }

    static const char *kind_name(Kind k) {
        switch (k) {
        case Kind::Pending:
            return "pending";
        case Kind::Active:
            return "active";
        case Kind::Done:
            return "done";
        case Kind::Failed:
            return "failed";
        case Kind::Skipped:
            return "skipped";
        }
        return "pending";
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const theme::Tokens &t = tokens_;
        const qreal r = height() / 2.0;
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(t.control_bd, theme::Metrics::progress_border_px));
        p.setBrush(t.control);
        p.drawRoundedRect(box, r, r);

        const float shown = settled_kind() ? 1.f : frac_; // 终态恒满格（mockup i.done/i.fail=100%）
        if (shown <= 0.f)
            return;
        const qreal inset = theme::Metrics::progress_border_px;
        const QRectF track = box.adjusted(inset, inset, -inset, -inset);
        const QRectF fill(track.left(), track.top(), track.width() * shown, track.height());
        if (fill.width() <= 0.0)
            return;
        p.setPen(Qt::NoPen);
        if (synthetic_)
            p.setBrush(stripe_brush(fill));
        else
            p.setBrush(fill_color());
        p.drawRoundedRect(fill, fill.height() / 2.0, fill.height() / 2.0);
    }

private:
    bool settled_kind() const {
        return kind_ == Kind::Done || kind_ == Kind::Failed || kind_ == Kind::Skipped;
    }

    QColor fill_color() const {
        const theme::Tokens &t = tokens_;
        switch (kind_) {
        case Kind::Active:
            return t.accent;
        case Kind::Done:
            return t.ok;
        case Kind::Failed:
            return t.err;
        case Kind::Skipped:
        case Kind::Pending:
            return t.text3;
        }
        return t.accent;
    }

    // `.obar i.synth{repeating-linear-gradient(115deg,#3aa3dc,#3aa3dc 6px,#4cc2ff 6px,#4cc2ff
    // 12px)}` CSS 角 = 自正上起顺时针 → 方向向量 (sinθ, −cosθ)；一个周期 = 2×band，RepeatSpread
    // 重复。
    QBrush stripe_brush(const QRectF &fill) const {
        const theme::Tokens &t = tokens_;
        constexpr double kPi = 3.14159265358979323846;
        const double rad = static_cast<double>(theme::Metrics::synth_stripe_deg) * kPi / 180.0;
        const QPointF dir(std::sin(rad), -std::cos(rad));
        const double period =
            theme::Metrics::synth_stripe_period_px; // 12px（= 2×band；theme token 单源）
        const QPointF center = fill.center();
        QLinearGradient g(center - dir * (period / 2.0), center + dir * (period / 2.0));
        g.setSpread(QGradient::RepeatSpread);
        g.setColorAt(0.0, t.progress_fill_from);
        g.setColorAt(0.5, t.progress_fill_from);
        g.setColorAt(0.5001, t.progress_fill_to);
        g.setColorAt(1.0, t.progress_fill_to);
        return QBrush(g);
    }

    theme::Tokens tokens_{};
    Kind kind_ = Kind::Pending;
    float frac_ = 0.f;
    bool synthetic_ = false;
};

// 日志尾（§9.3：5 行）：时间戳 --txt3、级别标签 --acc（mockup `.loglines .ts/.lv`）；富文本着色 +
// 逐行省略号（§9.3 不折行）；宽度变化后重渲染以保持省略点正确。
class LogLines : public QLabel {
public:
    explicit LogLines(QWidget *parent = nullptr) : QLabel(parent) {
        setTextFormat(Qt::RichText);
        setWordWrap(false);
        setAlignment(Qt::AlignLeft | Qt::AlignTop);
        setFont(theme::font(10.0, QFont::Normal, true));
    }
    void set_tokens(const theme::Tokens &t) {
        tokens_ = t;
        render();
    }
    void set_lines(const QStringList &lines) {
        if (lines_ == lines)
            return;
        lines_ = lines;
        render();
    }
    QStringList lines() const { return lines_; }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        render();
    }

private:
    void render() {
        if (lines_.isEmpty()) {
            setText(
                QStringLiteral("<span style=\"color:%1\">%2</span>")
                    .arg(theme::css_color(tokens_.text3), tr("（尚无运行日志）").toHtmlEscaped()));
            return;
        }
        QStringList html;
        html.reserve(lines_.size());
        for (const QString &raw : lines_) {
            const QString shown =
                width() > 0 ? fontMetrics().elidedText(raw, Qt::ElideRight, std::max(0, width()))
                            : raw;
            html << colorize(shown);
        }
        setText(html.join(QStringLiteral("<br>")));
    }

    QString colorize(const QString &line) const {
        const int space = line.indexOf(QLatin1Char(' '));
        if (space <= 0)
            return line.toHtmlEscaped();
        const QString stamp = line.left(space);
        QString rest = line.mid(space);
        QString head = QStringLiteral("<span style=\"color:%1\">%2</span>")
                           .arg(theme::css_color(tokens_.text3), stamp.toHtmlEscaped());
        const int bracket = rest.indexOf(QStringLiteral("] "));
        if (rest.startsWith(QLatin1Char('[')) && bracket > 0) {
            const QString level = rest.left(bracket + 1);
            head += QStringLiteral(" <span style=\"color:%1\">%2</span>")
                        .arg(theme::css_color(tokens_.accent), level.toHtmlEscaped());
            rest = rest.mid(bracket + 1);
        }
        return head + rest.toHtmlEscaped();
    }

    theme::Tokens tokens_{};
    QStringList lines_;
};

// ===========================================================================
// §7.2 阶段算术 / 文案词汇表
// ===========================================================================

// 共享段（源文件级，一次）的单阶段权重；非共享阶段 → 0（§7.2 前三行）
float shared_weight(Stage s) {
    switch (s) {
    case Stage::Probe:
        return StageWeights::probe_acquire;
    case Stage::Decode:
        return StageWeights::decode;
    case Stage::Orient:
        return StageWeights::orient;
    case Stage::Color:
        return StageWeights::color;
    default:
        return 0.f;
    }
}

bool is_shared_stage(Stage s) { return shared_weight(s) > 0.f; }

// 阶段文字（§3.4 词汇表 = 解码/色彩/编码+行数/写入/完成/失败；探测/旋转/合成取自十二态表）
QString shared_stage_text(Stage s) {
    switch (s) {
    case Stage::Probe:
        return PageRun::tr("探测");
    case Stage::Decode:
        return PageRun::tr("解码 → 色彩"); // mockup：共享段的前后串联提示
    case Stage::Orient:
        return PageRun::tr("旋转");
    case Stage::Color:
        return PageRun::tr("色彩");
    default:
        return PageRun::tr("处理中");
    }
}

QString write_stage_text(Stage s) {
    return s == Stage::Flatten ? PageRun::tr("合成") : PageRun::tr("写入");
}

// ===========================================================================
// 数值/杂项
// ===========================================================================

QString fmt_mmss(qint64 ms) {
    const qint64 total = std::max<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

QString fmt_mb_s(double v) { return PageRun::tr("%1 MB/s").arg(v, 0, 'f', 1); }

QString fmt_bytes(quint64 bytes) {
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb >= 1024.0)
        return PageRun::tr("%1 MB").arg(kb / 1024.0, 0, 'f', 2);
    return PageRun::tr("%1 KB").arg(kb, 0, 'f', 0);
}

QString fmt_secs(double ms) { return QStringLiteral("%1s").arg(ms / 1000.0, 0, 'f', 1); }

// 行名：只显示文件名（调用方可能传完整路径）
QString row_name(const QString &name) {
    const QString base = QFileInfo(name).fileName();
    return base.isEmpty() ? name : base;
}

int hardware_threads() {
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 0 ? static_cast<int>(hw) : 1;
}

bool is_terminal_state(pp::FileState s) {
    return s == pp::FileState::Done || s == pp::FileState::Skipped || s == pp::FileState::Failed ||
           s == pp::FileState::Cancelled;
}

// 格式 chip 文案（mockup .ofmt；未知 id 取大写）
QString format_chip_text(const QString &format_id) {
    if (format_id.isEmpty())
        return QStringLiteral("—");
    if (format_id.compare(QStringLiteral("jpeg"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("JPEG");
    if (format_id.compare(QStringLiteral("webp"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("WebP");
    if (format_id.compare(QStringLiteral("jxl"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("JXL");
    return format_id.toUpper();
}

// ===========================================================================
// 行状态
// ===========================================================================

enum class OutKind { Queued, Active, Done, Failed, Skipped, Cancelled };

MiniBar::Kind bar_kind(OutKind k) {
    switch (k) {
    case OutKind::Active:
        return MiniBar::Kind::Active;
    case OutKind::Done:
        return MiniBar::Kind::Done;
    case OutKind::Failed:
        return MiniBar::Kind::Failed;
    case OutKind::Skipped:
    case OutKind::Cancelled:
        return MiniBar::Kind::Skipped;
    case OutKind::Queued:
        break;
    }
    return MiniBar::Kind::Pending;
}

bool settled_kind(OutKind k) { return k != OutKind::Queued && k != OutKind::Active; }

struct OutRow {
    QWidget *row = nullptr;
    ElidedLabel *fmt = nullptr;
    MiniBar *bar = nullptr;
    ElidedLabel *pct = nullptr;
    ElidedLabel *stage = nullptr;
    // 进度（§7.2 段内 frac）
    float enc = 0.f, meta = 0.f;
    bool started = false, synthetic = false;
    Stage last_stage = Stage::Probe;
    bool has_stage = false;
    int rows_now = 0; // 行级回调折算的当前行（progress_max_row 同口径）
    // 终态事实（§3.4：字节 · 压缩比 · 耗时；失败原因）
    OutKind kind = OutKind::Queued;
    qint64 bytes = 0;
    double ratio = 0.0, ms = 0.0;
    QString error;
    // 已应用缓存（避免同值重写触发 style/repaint）
    QString applied_pct, applied_stage;
    QColor applied_pct_color, applied_stage_color;
};

struct FileRow {
    QWidget *group = nullptr;
    ElidedLabel *mark = nullptr;
    ElidedLabel *name = nullptr;
    ElidedLabel *meta = nullptr;
    QVector<OutRow> outs;
    // 共享段（§7.2 前三行；一次）
    float shared_probe = 0.f, shared_decode = 0.f, shared_orient = 0.f, shared_color = 0.f;
    Stage shared_stage = Stage::Probe;
    bool started = false, terminal = false;
    pp::FileState state = pp::FileState::Queued;
    int width = 0, height = 0;
    qint64 src_bytes = 0;
    QColor applied_mark_color;
};

struct RunState {
    PageRun *page = nullptr;
    theme::Tokens tokens{};
    PageRun::RunPlan plan;

    // 总览卡
    QFrame *overview = nullptr;
    ElidedLabel *state_pill = nullptr;
    ElidedLabel *progress_hint = nullptr;
    QProgressBar *progress = nullptr;
    ElidedLabel *percent = nullptr;
    ElidedLabel *elapsed = nullptr;
    ElidedLabel *throughput = nullptr;
    ElidedLabel *workers = nullptr;
    ElidedLabel *stagger = nullptr;
    ElidedLabel *budget = nullptr;
    ElidedLabel *alloc = nullptr;
    QPushButton *cancel = nullptr;
    QPushButton *open_logs = nullptr;
    QPushButton *open_output = nullptr;
    ElidedLabel *summary = nullptr;

    // 任务进度卡
    QFrame *tasks = nullptr;
    ElidedLabel *tasks_hint = nullptr;
    ElidedLabel *task_empty = nullptr;
    QWidget *list = nullptr;
    QVBoxLayout *list_layout = nullptr;

    // 日志尾卡
    QFrame *logs = nullptr;
    ElidedLabel *log_name = nullptr;
    LogLines *log_lines = nullptr;
    QStringList log_tail;

    // 行
    QVector<FileRow> files;

    // 运行态
    bool running = false, finished = false, cancel_requested = false;
    bool dirty = false;
    int formats = 1;
    std::size_t terminal_files = 0, started_files = 0;
    std::uint64_t out_bytes = 0;
    QString out_root;
    QElapsedTimer clock;
    QTimer *flush = nullptr;
};

QHash<const PageRun *, RunState *> &registry() {
    static QHash<const PageRun *, RunState *> store;
    return store;
}

RunState *state_of(const PageRun *page) { return registry().value(page, nullptr); }

// 颜色缓存写法（同值不重写 QSS，避免无谓 polish）
void set_color(QLabel *label, const QColor &color, QColor &cache) {
    if (cache.isValid() && cache == color)
        return;
    cache = color;
    label->setStyleSheet(QStringLiteral("color:%1").arg(theme::css_color(color)));
}

// ===========================================================================
// 进度读数（§7.1/§7.2）
// ===========================================================================

float shared_frac(const FileRow &file) {
    return std::clamp(file.shared_probe + file.shared_decode + file.shared_orient +
                          file.shared_color,
                      0.f, StageWeights::shared);
}

// 输出行填色（§7.2：共享段一次 + 该输出 50% encode + 10% metawrite）
float out_row_frac(const FileRow &file, const OutRow &out) {
    return std::clamp(shared_frac(file) + StageWeights::encode * out.enc +
                          StageWeights::metawrite * out.meta,
                      0.f, 1.f);
}

// §7.1 运行级：Σ 输出完成度 / 总输出数；终态输出按已结算计 1.0
double out_completion(const OutRow &out) {
    if (settled_kind(out.kind))
        return 1.0;
    const double own = (StageWeights::encode * out.enc + StageWeights::metawrite * out.meta) /
                       (StageWeights::encode + StageWeights::metawrite);
    return std::clamp(own, 0.0, 1.0);
}

double run_frac(const RunState &s) {
    const int total = static_cast<int>(s.files.size()) * s.formats;
    if (total <= 0)
        return 0.0;
    double sum = 0.0;
    for (const FileRow &file : s.files) {
        for (const OutRow &out : file.outs)
            sum += out_completion(out);
    }
    return std::clamp(sum / static_cast<double>(total), 0.0, 1.0);
}

int settled_outputs(const RunState &s) {
    int n = 0;
    for (const FileRow &file : s.files) {
        for (const OutRow &out : file.outs) {
            if (settled_kind(out.kind))
                ++n;
        }
    }
    return n;
}

bool any_output_started(const FileRow &file) {
    for (const OutRow &out : file.outs) {
        if (out.started)
            return true;
    }
    return false;
}

int first_unstarted(const FileRow &file) {
    for (int j = 0; j < file.outs.size(); ++j) {
        if (!file.outs.at(j).started)
            return j;
    }
    return -1;
}

QString file_meta_text(const FileRow &file) {
    QStringList parts;
    if (file.width > 0 && file.height > 0)
        parts << QStringLiteral("%1×%2").arg(file.width).arg(file.height);
    parts << PageRun::tr("%1 个输出").arg(file.outs.size());
    return parts.join(QStringLiteral(" · "));
}

QString out_pct_text(const FileRow &file, const OutRow &out) {
    switch (out.kind) {
    case OutKind::Done:
        return out.bytes > 0 ? fmt_bytes(static_cast<quint64>(out.bytes)) : QStringLiteral("100%");
    case OutKind::Failed:
        return PageRun::tr("失败");
    case OutKind::Skipped:
    case OutKind::Cancelled:
        return QStringLiteral("—"); // 原因/状态在阶段列直显，百分比列不重复一遍
    case OutKind::Queued:
        return QStringLiteral("—");
    case OutKind::Active:
        break;
    }
    return QStringLiteral("%1%").arg(std::lround(out_row_frac(file, out) * 100.0));
}

QString out_stage_text(const FileRow &file, const RunState &s, int index) {
    const OutRow &out = file.outs.at(index);
    switch (out.kind) {
    case OutKind::Failed:
        return out.error.isEmpty() ? PageRun::tr("失败") : out.error;
    case OutKind::Skipped:
        return PageRun::tr("跳过（冲突策略）");
    case OutKind::Cancelled:
        return PageRun::tr("取消");
    case OutKind::Done: {
        QStringList parts;
        if (out.ratio > 0.0)
            parts << QStringLiteral("%1×").arg(out.ratio, 0, 'f', 1);
        if (out.ms > 0.0)
            parts << fmt_secs(out.ms);
        return parts.isEmpty() ? PageRun::tr("完成") : parts.join(QStringLiteral(" · "));
    }
    case OutKind::Queued:
    case OutKind::Active:
        break;
    }
    if (!out.started) {
        // 尚未启动：文件处于共享段且**无输出已启动** → 首个未完成行承载共享段（§7.2 一次）
        if (index == first_unstarted(file) && !any_output_started(file) && !file.terminal) {
            if (file.started)
                return shared_stage_text(file.shared_stage);
            return s.plan.stagger_ms > 0 ? PageRun::tr("排队中（交错启动）")
                                         : PageRun::tr("排队中");
        }
        return PageRun::tr("排队中");
    }
    if (out.has_stage && out.last_stage == Stage::MetaWrite)
        return write_stage_text(Stage::MetaWrite);
    if (out.has_stage && out.last_stage == Stage::Flatten)
        return write_stage_text(Stage::Flatten);
    if (s.plan.metadata_only)
        return PageRun::tr("写入"); // 仅元数据模式：同容器零重编码（§3.2/D7）→ 无编码段
    if (out.synthetic)
        return PageRun::tr("编码（合成进度）"); // §7.3
    if (file.height > 0)
        return PageRun::tr("编码 %1/%2 行").arg(out.rows_now).arg(file.height);
    return PageRun::tr("编码 %1%").arg(std::lround(out_row_frac(file, out) * 100.0));
}

QColor out_stage_color(const RunState &s, const OutRow &out) {
    switch (out.kind) {
    case OutKind::Done:
        return s.tokens.ok;
    case OutKind::Failed:
        return s.tokens.err;
    case OutKind::Skipped:
    case OutKind::Cancelled:
        return s.tokens.warn;
    case OutKind::Active:
        return s.tokens.text2;
    case OutKind::Queued:
        break;
    }
    return s.tokens.text3;
}

QString mark_text(const FileRow &file) {
    switch (file.state) {
    case pp::FileState::Done:
        return QStringLiteral("✓");
    case pp::FileState::Failed:
        return QStringLiteral("✗");
    case pp::FileState::Skipped:
        return QStringLiteral("»");
    case pp::FileState::Cancelled:
        return QStringLiteral("■");
    case pp::FileState::Queued:
        return QStringLiteral("…");
    default:
        return file.terminal ? QStringLiteral("…") : QStringLiteral("◐");
    }
}

QColor mark_color(const RunState &s, const FileRow &file) {
    switch (file.state) {
    case pp::FileState::Done:
        return s.tokens.ok;
    case pp::FileState::Failed:
        return s.tokens.err;
    case pp::FileState::Skipped:
    case pp::FileState::Cancelled:
        return s.tokens.warn;
    case pp::FileState::Queued:
        return s.tokens.text3;
    default:
        return file.terminal ? s.tokens.text3 : s.tokens.accent;
    }
}

// ===========================================================================
// 事件 → 行状态（§7.2/§7.3）
// ===========================================================================

void apply_progress(RunState &s, FileRow &file, const ProgressInfo &pi) {
    if (pi.output_index >= 0 && pi.output_index < file.outs.size()) {
        OutRow &out = file.outs[pi.output_index];
        out.started = true;
        if (!settled_kind(out.kind))
            out.kind = OutKind::Active; // 进行中（终态由 apply_terminal 定稿，不被进度回退）
        out.synthetic = pi.synthetic;   // §7.3：true → 斜纹
        out.last_stage = pi.stage;
        out.has_stage = true;
        if (pi.stage == Stage::Encode) {
            out.enc = std::max(out.enc, pi.stage_frac);
            if (file.height > 0) {
                out.rows_now = std::max(out.rows_now,
                                        static_cast<int>(std::lround(pi.stage_frac * file.height)));
            }
        } else if (pi.stage == Stage::MetaWrite) {
            out.meta = std::max(out.meta, pi.stage_frac);
        }
        if (!file.started) {
            file.started = true;
            ++s.started_files;
        }
        return;
    }
    if (pi.output_index < 0) {
        const float weight = shared_weight(pi.stage);
        if (weight <= 0.f)
            return;
        const float contribution = weight * pi.stage_frac;
        switch (pi.stage) {
        case Stage::Probe:
            file.shared_probe = std::max(file.shared_probe, contribution);
            break;
        case Stage::Decode:
            file.shared_decode = std::max(file.shared_decode, contribution);
            break;
        case Stage::Orient:
            file.shared_orient = std::max(file.shared_orient, contribution);
            break;
        case Stage::Color:
            file.shared_color = std::max(file.shared_color, contribution);
            break;
        default:
            break;
        }
        file.shared_stage = pi.stage;
    }
}

void apply_terminal(RunState &s, FileRow &file, const pp::FileEvent &ev) {
    file.terminal = true;
    file.state = ev.state;
    ++s.terminal_files;
    if (!file.started) {
        file.started = true;
        ++s.started_files;
    }
    const pp::FileResult *result = ev.result;
    const QString file_error =
        result != nullptr ? QString::fromStdString(result->error) : QString();
    if (result != nullptr) {
        s.out_bytes += result->out_bytes;
        if (file.height == 0 && result->info.height > 0)
            file.height = result->info.height; // 兜底：plan 未给高度时用终态信息补
        if (file.width == 0 && result->info.width > 0)
            file.width = result->info.width;
    }
    for (int j = 0; j < file.outs.size(); ++j) {
        OutRow &out = file.outs[j];
        const pp::OutputResult *row =
            (result != nullptr && j < static_cast<int>(result->outputs.size()))
                ? &result->outputs[static_cast<std::size_t>(j)]
                : nullptr;
        if (row != nullptr) {
            out.bytes = static_cast<qint64>(row->out_bytes);
            out.ratio = (out.bytes > 0 && file.src_bytes > 0)
                            ? static_cast<double>(file.src_bytes) / static_cast<double>(out.bytes)
                            : 0.0;
            out.ms = row->t.encode_ms + row->t.metawrite_ms;
            out.error = QString::fromStdString(row->error);
            if (out.error.isEmpty() && row->skipped)
                out.kind = OutKind::Skipped;
            else if (out.error.isEmpty() && row->ok)
                out.kind = OutKind::Done;
            else if (!out.error.isEmpty())
                out.kind = OutKind::Failed;
        }
        if (!settled_kind(out.kind)) {
            switch (ev.state) {
            case pp::FileState::Done:
                out.kind = OutKind::Done;
                break;
            case pp::FileState::Skipped:
                out.kind = OutKind::Skipped;
                break;
            case pp::FileState::Cancelled:
                out.kind = OutKind::Cancelled;
                break;
            default:
                out.kind = OutKind::Failed;
                break;
            }
        }
        if (out.kind == OutKind::Failed && out.error.isEmpty())
            out.error = file_error; // 文件级失败（plan/decode）→ 逐输出行回落到聚合原因
    }
}

// ===========================================================================
// 渲染（60ms flush 的统一入口）
// ===========================================================================

void apply_out_row(RunState &s, const FileRow &file, OutRow &out, int index) {
    // 条色：终态按终态色；进行中（含"首个未完成行承载共享段进度"的那一行，mockup 的 18% 行）
    // = accent 实心；未启动 = 轨道（无填充）
    const bool carries_shared = !out.started && file.started && !file.terminal &&
                                !any_output_started(file) && index == first_unstarted(file);
    const MiniBar::Kind kind =
        settled_kind(out.kind)
            ? bar_kind(out.kind)
            : (out.started || carries_shared ? MiniBar::Kind::Active : MiniBar::Kind::Pending);
    out.bar->set_state(kind, out_row_frac(file, out), out.synthetic);
    const QString pct = out_pct_text(file, out);
    if (pct != out.applied_pct) {
        out.applied_pct = pct;
        out.pct->set_full_text(pct);
    }
    set_color(out.pct, out.kind == OutKind::Done ? s.tokens.text2 : s.tokens.text3,
              out.applied_pct_color);
    const QString stage = out_stage_text(file, s, index);
    if (stage != out.applied_stage) {
        out.applied_stage = stage;
        out.stage->set_full_text(stage);
    }
    set_color(out.stage, out_stage_color(s, out), out.applied_stage_color);
}

void refresh_widgets(RunState &s) {
    // ---- 总览卡（§3.4：总百分比 · 吞吐 · 并行 workers · 交错间隔 · 线程预算模式 · 取消）----
    const int total_outputs = static_cast<int>(s.files.size()) * s.formats;
    const double frac = s.finished ? 1.0 : run_frac(s);
    if (s.progress->maximum() > 0) {
        if (s.finished)
            s.progress->setValue(s.progress->maximum()); // 终态恒满（含失败；口径 = 已结算）
        else
            s.progress->setValue(static_cast<int>(std::lround(frac * s.progress->maximum())));
    }
    s.percent->set_full_text(QStringLiteral("%1%").arg(std::lround(frac * 100.0)));
    s.progress_hint->set_full_text(
        total_outputs > 0 ? PageRun::tr("%1 / %2 个输出").arg(settled_outputs(s)).arg(total_outputs)
                          : QString());

    const qint64 ms = s.clock.isValid() ? s.clock.elapsed() : 0;
    s.elapsed->set_full_text(fmt_mmss(ms));
    const double mb_s =
        ms > 0 ? (static_cast<double>(s.out_bytes) / 1.0e6) / (static_cast<double>(ms) / 1000.0)
               : 0.0;
    s.throughput->set_full_text(fmt_mb_s(mb_s));

    const int budget = s.plan.thread_budget > 0 ? s.plan.thread_budget : hardware_threads();
    const int pool = s.plan.workers > 0 ? s.plan.workers : budget;
    const int inflight =
        std::max(0, static_cast<int>(s.started_files) - static_cast<int>(s.terminal_files));
    const int remaining =
        std::max(0, static_cast<int>(s.files.size()) - static_cast<int>(s.started_files));
    s.workers->set_full_text(PageRun::tr("%1 / %2 workers").arg(inflight).arg(pool));
    s.stagger->set_full_text(PageRun::tr("%1 ms").arg(s.plan.stagger_ms));
    s.budget->set_full_text(s.plan.thread_budget > 0
                                ? PageRun::tr("固定 %1 核").arg(s.plan.thread_budget)
                                : PageRun::tr("自适应（%1 核）").arg(hardware_threads()));
    if (!s.running || s.files.isEmpty()) {
        s.alloc->set_full_text(QStringLiteral("—")); // §8.2 分配只在运行期有意义
    } else {
        // §8.2 的分配读数 = 同一纯函数的同参求值（remaining/inflight 由事件流折算）
        const pp::Alloc alloc = pp::alloc_threads(budget, remaining, inflight);
        s.alloc->set_full_text(
            PageRun::tr("%1 文件并行 + %2 编码多线程").arg(alloc.files).arg(alloc.encode_threads));
    }
    if (s.running && s.cancel_requested) {
        s.cancel->setEnabled(false);
        s.cancel->setVisible(true);
        s.cancel->setText(PageRun::tr("取消中…"));
    } else if (s.running) {
        s.cancel->setEnabled(true);
        s.cancel->setVisible(true);
        s.cancel->setText(PageRun::tr("■ 取消运行"));
    }

    // ---- 任务进度卡（源文件分组行 + 逐输出子行）----
    for (int i = 0; i < s.files.size(); ++i) {
        FileRow &file = s.files[i];
        file.mark->set_full_text(mark_text(file));
        set_color(file.mark, mark_color(s, file), file.applied_mark_color);
        file.meta->set_full_text(file_meta_text(file));
        for (int j = 0; j < file.outs.size(); ++j)
            apply_out_row(s, file, file.outs[j], j);
    }
    // ---- 日志尾 ----
    s.log_lines->set_lines(s.log_tail);
}

// ===========================================================================
// 页面 QSS（tokens 单源 = ui/theme.h；选择器只锚 pp-run-* / ppField，避免误伤它页）
// ===========================================================================

constexpr const char *kFieldProperty = "ppField";

QString page_qss(const theme::Tokens &t) {
    const auto css = [](const QColor &c) { return theme::css_color(c); };
    const QString field = QString::fromLatin1(kFieldProperty);
    QString qss;
    // 只读字段栅格（mockup .gl/.gv：70px 右对齐标签列 + 12px 列距 + 32px 行高）
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"label\"] { color: ") +
           css(t.text3) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"value\"] { color: ") +
           css(t.text) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"value-dim\"] { color: ") +
           css(t.text2) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"stage\"] { color: ") +
           css(t.text3) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"group-st\"] { color: ") +
           css(t.text3) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"empty\"] { color: ") +
           css(t.text3) + QStringLiteral("; }\n");
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"mark\"] { color: ") +
           css(t.text3) + QStringLiteral("; }\n");
    // 格式 chip（mockup .ofmt：--ctl 底 + --ctl-bd 边 + r=3 + 10px/700 --txt2 + 44px 定宽）
    qss += QStringLiteral("QLabel[") + field + QStringLiteral("=\"chip\"] { background: ") +
           css(t.control) + QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: 3px; color: ") + css(t.text2) + QStringLiteral("; }\n");
    // 大进度条（run-dark .bigbar：10px + r=99 + 1px 边框；填充 = 双色线性渐变 .bigbar i）
    qss += QStringLiteral("QProgressBar#pp-run-progress { background: ") + css(t.control) +
           QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: ") +
           QString::number(theme::Metrics::big_progress_px / 2) + QStringLiteral("px; }\n");
    qss += QStringLiteral("QProgressBar#pp-run-progress::chunk { background: qlineargradient("
                          "x1:0, y1:0, x2:1, y2:0, stop:0 ") +
           css(t.progress_fill_from) + QStringLiteral(", stop:1 ") + css(t.progress_fill_to) +
           QStringLiteral("); border-radius: ") +
           QString::number(theme::Metrics::big_progress_px / 2 - 1) + QStringLiteral("px; }\n");
    // 次按钮（mockup .btn：--ctl 底 + --ctl-bd 边 + r=4 + 600）/ 危险钮（.btn.danger：--err 字 +
    // err 边）
    qss += QStringLiteral("QPushButton[") + field + QStringLiteral("=\"button\"] { background: ") +
           css(t.control) + QStringLiteral("; border: 1px solid ") + css(t.control_bd) +
           QStringLiteral("; border-radius: ") + QString::number(theme::Metrics::control_radius) +
           QStringLiteral("px; color: ") + css(t.text2) +
           QStringLiteral("; font-weight: 600; padding: 0 14px; }\n");
    qss += QStringLiteral("QPushButton[") + field + QStringLiteral("=\"button\"]:hover { color: ") +
           css(t.text) + QStringLiteral("; }\n");
    QColor danger_bd = t.err;
    danger_bd.setAlpha(102); // rgba(241,112,123,.4)
    qss += QStringLiteral("QPushButton[") + field + QStringLiteral("=\"danger\"] { background: ") +
           css(t.control) + QStringLiteral("; border: 1px solid ") + css(danger_bd) +
           QStringLiteral("; border-radius: ") + QString::number(theme::Metrics::control_radius) +
           QStringLiteral("px; color: ") + css(t.err) +
           QStringLiteral("; font-weight: 600; padding: 0 14px; }\n");
    qss += QStringLiteral("QPushButton[") + field +
           QStringLiteral("=\"danger\"]:disabled { color: ") + css(t.text3) +
           QStringLiteral("; border-color: ") + css(t.control_bd) + QStringLiteral("; }\n");
    // 链接（mockup .link：--acc + 600）
    qss += QStringLiteral("QPushButton[") + field +
           QStringLiteral("=\"link\"] { background: "
                          "transparent; border: none; color: ") +
           css(t.accent) + QStringLiteral("; font-weight: 600; padding: 0 2px; }\n");
    qss += QStringLiteral("QPushButton[") + field +
           QStringLiteral("=\"link\"]:disabled { color: ") + css(t.text3) + QStringLiteral("; }\n");
    // 日志尾框（mockup .loglines：--card-hi 底 + --card-bd 边 + r=6 + monospace 10px）
    qss += QStringLiteral("QFrame#pp-run-log-box { background: ") + css(t.card_hi) +
           QStringLiteral("; border: 1px solid ") + css(t.card_bd) +
           QStringLiteral("; "
                          "border-radius: ") +
           QString::number(theme::Metrics::stage_radius) + QStringLiteral("px; }\n");
    // 摘要行（mockup .hint 同级：--txt2）
    qss +=
        QStringLiteral("QLabel#pp-run-summary { color: ") + css(t.text2) + QStringLiteral("; }\n");
    // 容器透明（窗口底渐变透出）
    qss += QStringLiteral("QWidget#pp-run-content { background: transparent; }\n");
    qss += QStringLiteral("QScrollArea#pp-run-scroll { background: transparent; }\n");
    qss += QStringLiteral("QScrollArea#pp-run-scroll > QWidget > QWidget { background: "
                          "transparent; }\n");
    return qss;
}

// ===========================================================================
// 界面构建
// ===========================================================================

QFrame *make_card(QWidget *parent, const QString &title, ElidedLabel **pill_out,
                  ElidedLabel **hint_out) {
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("pp-card")); // theme 骨架 QSS：QFrame#pp-card
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
        pill->setFont(theme::font(theme::Typography::badge_px, theme::Typography::badge_weight));
        pill->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        h->addWidget(pill);
        *pill_out = pill;
    }
    h->addStretch(1);
    if (hint_out != nullptr) {
        auto *hint = new ElidedLabel(head);
        hint->setObjectName(QStringLiteral("pp-card-hint"));
        hint->setFont(theme::font(theme::Typography::hint_px, 500));
        hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        h->addWidget(hint, 1);
        *hint_out = hint;
    }
    v->addWidget(head);
    return card;
}

QLabel *make_field_label(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setFont(theme::font(theme::Typography::small_px));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setMinimumHeight(theme::Metrics::form_row_h);
    label->setMinimumWidth(theme::Metrics::form_label_w);
    label->setMaximumWidth(theme::Metrics::form_label_w);
    label->setProperty(kFieldProperty, QStringLiteral("label"));
    return label;
}

ElidedLabel *make_value_label(QWidget *parent, const QString &object_name) {
    auto *label = new ElidedLabel(parent);
    label->setObjectName(object_name);
    label->setFont(theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
    label->setProperty(kFieldProperty, QStringLiteral("value"));
    label->setMinimumHeight(theme::Metrics::form_row_h);
    return label;
}

// 总览卡（§9.3：大进度条 + 吞吐/并行/交错/线程预算只读 + 取消 + 跳转）
void build_overview_card(QWidget *parent, RunState *s) {
    s->overview = make_card(parent, PageRun::tr("运行总览"), &s->state_pill, &s->progress_hint);
    auto *v = qobject_cast<QVBoxLayout *>(s->overview->layout());
    s->state_pill->setObjectName(QStringLiteral("pp-run-state-pill"));
    s->progress_hint->setObjectName(QStringLiteral("pp-run-progress-hint"));
    s->state_pill->set_full_text(PageRun::tr("就绪"));

    // 只读字段栅格：4 列（70px 标签 / 值 / 70px 标签 / 值）+ 12px 列距（§9.3 硬约束）
    auto *grid = new QGridLayout();
    grid->setContentsMargins(14, 2, 14, 0);
    grid->setHorizontalSpacing(theme::Metrics::form_col_gap);
    grid->setVerticalSpacing(0);

    // 行 1：进度（大进度条 + 百分比）
    grid->addWidget(make_field_label(s->overview, PageRun::tr("进度")), 0, 0);
    auto *bar_row = new QWidget(s->overview);
    auto *bar_h = new QHBoxLayout(bar_row);
    bar_h->setContentsMargins(0, 0, 0, 0);
    bar_h->setSpacing(8);
    s->progress = new QProgressBar(bar_row);
    s->progress->setObjectName(QStringLiteral("pp-run-progress"));
    s->progress->setTextVisible(false);
    s->progress->setFixedHeight(theme::Metrics::big_progress_px); // .bigbar{height:10px}
    s->progress->setRange(0, 1);
    s->progress->setValue(0);
    s->progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    bar_h->addWidget(s->progress, 1);
    s->percent = new ElidedLabel(bar_row);
    s->percent->setObjectName(QStringLiteral("pp-run-percent"));
    s->percent->setFont(theme::font(theme::Typography::body_px, QFont::Normal, true));
    s->percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    s->percent->setFixedWidth(38); // mockup：<div class="num" style="width:38px">
    bar_h->addWidget(s->percent);
    grid->addWidget(bar_row, 0, 1);

    // 行 2：已用时 / 吞吐
    grid->addWidget(make_field_label(s->overview, PageRun::tr("已用时")), 1, 0);
    s->elapsed = make_value_label(s->overview, QStringLiteral("pp-run-elapsed"));
    grid->addWidget(s->elapsed, 1, 1);
    grid->addWidget(make_field_label(s->overview, PageRun::tr("吞吐")), 1, 2);
    s->throughput = make_value_label(s->overview, QStringLiteral("pp-run-throughput"));
    grid->addWidget(s->throughput, 1, 3);

    // 行 3：并行 / 交错
    grid->addWidget(make_field_label(s->overview, PageRun::tr("并行")), 2, 0);
    s->workers = make_value_label(s->overview, QStringLiteral("pp-run-workers"));
    grid->addWidget(s->workers, 2, 1);
    grid->addWidget(make_field_label(s->overview, PageRun::tr("交错")), 2, 2);
    s->stagger = make_value_label(s->overview, QStringLiteral("pp-run-stagger"));
    grid->addWidget(s->stagger, 2, 3);

    // 行 4：线程预算 / 当前分配（§8.2 E3 读数）
    grid->addWidget(make_field_label(s->overview, PageRun::tr("线程预算")), 3, 0);
    s->budget = make_value_label(s->overview, QStringLiteral("pp-run-budget"));
    grid->addWidget(s->budget, 3, 1);
    grid->addWidget(make_field_label(s->overview, PageRun::tr("当前分配")), 3, 2);
    s->alloc = make_value_label(s->overview, QStringLiteral("pp-run-alloc"));
    grid->addWidget(s->alloc, 3, 3);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    v->addLayout(grid);

    // 运行控制行（mockup .runctl：取消运行 + 查看日志… + 打开输出目录）
    auto *ctl = new QWidget(s->overview);
    auto *ctl_h = new QHBoxLayout(ctl);
    ctl_h->setContentsMargins(14, 8, 14, 10);
    ctl_h->setSpacing(14);
    s->cancel = new QPushButton(PageRun::tr("■ 取消运行"), ctl);
    s->cancel->setObjectName(QStringLiteral("pp-run-cancel"));
    s->cancel->setProperty(kFieldProperty, QStringLiteral("danger"));
    s->cancel->setFixedHeight(theme::Metrics::input_h); // .btn{height:28px}
    s->cancel->setFont(theme::font(theme::Typography::body_px, 600));
    s->cancel->setCursor(Qt::PointingHandCursor);
    ctl_h->addWidget(s->cancel);
    s->open_logs = new QPushButton(PageRun::tr("查看日志…"), ctl);
    s->open_logs->setObjectName(QStringLiteral("pp-run-open-logs"));
    s->open_logs->setProperty(kFieldProperty, QStringLiteral("link"));
    s->open_logs->setFont(theme::font(theme::Typography::body_px, 600));
    s->open_logs->setCursor(Qt::PointingHandCursor);
    ctl_h->addWidget(s->open_logs);
    ctl_h->addStretch(1);
    s->open_output = new QPushButton(PageRun::tr("打开输出目录"), ctl);
    s->open_output->setObjectName(QStringLiteral("pp-run-open-output"));
    s->open_output->setProperty(kFieldProperty, QStringLiteral("link"));
    s->open_output->setFont(theme::font(theme::Typography::body_px, 600));
    s->open_output->setCursor(Qt::PointingHandCursor);
    ctl_h->addWidget(s->open_output);
    v->addWidget(ctl);

    // 摘要行（空闲 = 引导文案；完成 = 结果汇总；运行中清空）
    s->summary = new ElidedLabel(s->overview);
    s->summary->setObjectName(QStringLiteral("pp-run-summary"));
    s->summary->setFont(theme::font(theme::Typography::small_px));
    s->summary->setContentsMargins(14, 0, 14, 10);
    v->addWidget(s->summary);

    QObject::connect(s->cancel, &QPushButton::clicked, s->page, [s]() {
        s->cancel_requested = true;
        s->dirty = true;
        refresh_widgets(*s); // 立即反馈（不等 60ms flush）
        emit s->page->cancel_requested();
    });
    QObject::connect(s->open_logs, &QPushButton::clicked, s->page,
                     [s]() { emit s->page->open_logs_requested(); });
    QObject::connect(s->open_output, &QPushButton::clicked, s->page,
                     [s]() { emit s->page->open_output_requested(s->out_root); });
}

// 任务进度卡（§3.4：源文件分组行 + 逐输出子行 + 阶段权重提示）
void build_tasks_card(QWidget *parent, RunState *s) {
    s->tasks = make_card(parent, PageRun::tr("任务进度"), nullptr, &s->tasks_hint);
    s->tasks_hint->set_full_text(PageRun::tr("阶段权重 + 行级子进度"));
    auto *v = qobject_cast<QVBoxLayout *>(s->tasks->layout());
    auto *body = new QWidget(s->tasks);
    auto *body_v = new QVBoxLayout(body);
    body_v->setContentsMargins(2, 2, 12, 10);
    body_v->setSpacing(0);
    s->task_empty = new ElidedLabel(body);
    s->task_empty->setObjectName(QStringLiteral("pp-run-task-empty"));
    s->task_empty->setFont(theme::font(theme::Typography::body_px));
    s->task_empty->setProperty(kFieldProperty, QStringLiteral("empty"));
    s->task_empty->setContentsMargins(12, 6, 0, 12);
    body_v->addWidget(s->task_empty);
    s->list = new QWidget(body);
    s->list->setObjectName(QStringLiteral("pp-run-list"));
    s->list_layout = new QVBoxLayout(s->list);
    s->list_layout->setContentsMargins(0, 0, 0, 0);
    s->list_layout->setSpacing(0);
    body_v->addWidget(s->list);
    v->addWidget(body);
}

// 日志尾卡（§9.3：5 行滚动 + 跳转入口）
void build_logs_card(QWidget *parent, RunState *s) {
    s->logs = make_card(parent, PageRun::tr("日志尾"), nullptr, &s->log_name);
    s->log_name->setObjectName(QStringLiteral("pp-run-log-name"));
    auto *v = qobject_cast<QVBoxLayout *>(s->logs->layout());
    auto *box = new QFrame(s->logs);
    box->setObjectName(QStringLiteral("pp-run-log-box"));
    auto *box_v = new QVBoxLayout(box);
    box_v->setContentsMargins(10, 8, 10, 8);
    s->log_lines = new LogLines(box);
    s->log_lines->setObjectName(QStringLiteral("pp-run-log-lines"));
    s->log_lines->set_tokens(s->tokens);
    box_v->addWidget(s->log_lines);
    v->addWidget(box);
}

// 行控件（分组行 + 逐输出子行）：先建后填（N 格式 N 行；§3.4/§6.2）
void build_rows(RunState *s, const QStringList &names) {
    const int formats = s->formats;
    for (int i = 0; i < names.size(); ++i) {
        FileRow file;
        file.group = new QWidget(s->list);
        file.group->setObjectName(QStringLiteral("pp-run-group-%1").arg(i));
        auto *g = new QHBoxLayout(file.group);
        g->setContentsMargins(12, 0, 0, 0); // .grp{padding:7px 4px 3px} → 行高 24
        g->setSpacing(8);
        file.mark = new ElidedLabel(file.group);
        file.mark->setObjectName(QStringLiteral("pp-run-gmark-%1").arg(i));
        file.mark->setFont(theme::font(theme::Typography::body_px, QFont::Bold));
        file.mark->setProperty(kFieldProperty, QStringLiteral("mark"));
        file.mark->setFixedWidth(16); // .mark{width:16px}
        file.mark->setAlignment(Qt::AlignCenter);
        file.mark->set_full_text(QStringLiteral("…"));
        g->addWidget(file.mark);
        file.name = new ElidedLabel(file.group);
        file.name->setObjectName(QStringLiteral("pp-run-gname-%1").arg(i));
        file.name->setFont(theme::font(theme::Typography::body_px, QFont::Bold));
        file.name->setProperty(kFieldProperty, QStringLiteral("value"));
        file.name->set_full_text(row_name(names.at(i)));
        g->addWidget(file.name);
        g->addStretch(1);
        file.meta = new ElidedLabel(file.group);
        file.meta->setObjectName(QStringLiteral("pp-run-gmeta-%1").arg(i));
        file.meta->setFont(theme::font(10.0));
        file.meta->setProperty(kFieldProperty, QStringLiteral("group-st"));
        file.meta->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        g->addWidget(file.meta, 1);
        file.group->setMinimumHeight(24);
        s->list_layout->addWidget(file.group);

        if (i < s->plan.heights.size())
            file.height = s->plan.heights.at(i);
        if (i < s->plan.widths.size())
            file.width = s->plan.widths.at(i);
        if (i < s->plan.bytes.size())
            file.src_bytes = s->plan.bytes.at(i);

        file.outs.reserve(formats);
        for (int j = 0; j < formats; ++j) {
            OutRow out;
            out.row = new QWidget(s->list);
            out.row->setObjectName(QStringLiteral("pp-run-row-%1-%2").arg(i).arg(j));
            auto *r = new QHBoxLayout(out.row);
            r->setContentsMargins(22, 0, 0, 0); // .orow{padding:3px 4px 3px 22px}
            r->setSpacing(8);
            const QString fmt_id =
                j < s->plan.format_ids.size() ? s->plan.format_ids.at(j) : QString();
            out.fmt = new ElidedLabel(out.row);
            out.fmt->setObjectName(QStringLiteral("pp-run-fmt-%1-%2").arg(i).arg(j));
            out.fmt->setProperty(kFieldProperty, QStringLiteral("chip"));
            out.fmt->setFont(theme::font(10.0, QFont::Bold));
            out.fmt->setAlignment(Qt::AlignCenter);
            out.fmt->setFixedWidth(44); // .ofmt{width:44px}
            out.fmt->setFixedHeight(16);
            out.fmt->set_full_text(format_chip_text(fmt_id));
            r->addWidget(out.fmt);
            out.bar = new MiniBar(out.row);
            out.bar->setObjectName(QStringLiteral("pp-run-bar-%1-%2").arg(i).arg(j));
            out.bar->set_tokens(s->tokens);
            r->addWidget(out.bar, 1);
            out.pct = new ElidedLabel(out.row);
            out.pct->setObjectName(QStringLiteral("pp-run-pct-%1-%2").arg(i).arg(j));
            out.pct->setFont(theme::font(10.0, QFont::Normal, true));
            out.pct->setProperty(kFieldProperty, QStringLiteral("value-dim"));
            out.pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            out.pct->setFixedWidth(74); // .opct{width:74px}
            r->addWidget(out.pct);
            out.stage = new ElidedLabel(out.row);
            out.stage->setObjectName(QStringLiteral("pp-run-stage-%1-%2").arg(i).arg(j));
            out.stage->setFont(theme::font(10.0));
            out.stage->setProperty(kFieldProperty, QStringLiteral("stage"));
            out.stage->setFixedWidth(150); // .ost{width:150px}
            r->addWidget(out.stage);
            out.row->setMinimumHeight(22);
            s->list_layout->addWidget(out.row);
            file.outs.append(out);
        }
        file.meta->set_full_text(file_meta_text(file)); // 输出行建齐后再写「N 个输出」
        s->files.append(file);
    }
}

} // namespace

// ===========================================================================
// PageRun（公开面）
// ===========================================================================

PageRun::PageRun(QWidget *parent) : QWidget(parent) {
    Q_ASSERT(!registry().contains(this));
    RunState *s = new RunState;
    s->page = this;
    registry().insert(this, s);
    connect(this, &QObject::destroyed, this, [this]() { delete registry().take(this); });

    s->tokens = theme::tokens(theme::ThemeMode::Dark);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("pp-run-scroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);
    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("pp-run-content"));
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(theme::Metrics::gap); // mockup .scroll{gap:10px}
    scroll->setWidget(content);

    build_overview_card(content, s);
    build_tasks_card(content, s);
    build_logs_card(content, s);
    v->addWidget(s->overview);
    v->addWidget(s->tasks);
    v->addWidget(s->logs);
    v->addStretch(1);

    setStyleSheet(page_qss(s->tokens));

    s->flush = new QTimer(this);
    s->flush->setInterval(kFlushMs); // §7.4：GUI 节流 60ms
    connect(s->flush, &QTimer::timeout, this, [s]() {
        if (!s->dirty)
            return;
        refresh_widgets(*s);
        s->dirty = false;
    });
    reset(); // 空闲态（就绪 + 空任务表 + 引导文案）
}

void PageRun::set_plan(const RunPlan &plan) {
    RunState *s = state_of(this);
    if (!s)
        return;
    s->plan = plan;
    if (s->files.isEmpty())
        s->formats = plan.format_ids.isEmpty() ? 1 : plan.format_ids.size();
}

void PageRun::set_tokens(const theme::Tokens &tokens) {
    RunState *s = state_of(this);
    if (!s)
        return;
    s->tokens = tokens;
    setStyleSheet(page_qss(tokens));
    for (FileRow &file : s->files) {
        for (OutRow &out : file.outs)
            out.bar->set_tokens(tokens);
    }
    s->log_lines->set_tokens(tokens);
    // 浅色卡阴影（§9.2 0 1px 4px rgba(16,24,40,.06)；深色无阴影）
    const qreal dpr = devicePixelRatioF() > 0.01 ? devicePixelRatioF() : 1.0;
    QFrame *const cards[] = {s->overview, s->tasks, s->logs};
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
            card->setGraphicsEffect(nullptr); // 删旧效果（含析构）
        }
    }
    refresh_widgets(*s);
}

void PageRun::set_log_tail(const QStringList &lines, const QString &log_name) {
    RunState *s = state_of(this);
    if (!s)
        return;
    s->log_tail = lines.mid(std::max(0, static_cast<int>(lines.size()) - kLogTailLines));
    s->log_lines->set_lines(s->log_tail);
    s->log_name->set_full_text(log_name);
}

int PageRun::outputs_total() const {
    const RunState *s = state_of(this);
    return s != nullptr ? static_cast<int>(s->files.size()) * s->formats : 0;
}

int PageRun::outputs_done() const {
    const RunState *s = state_of(this);
    return s != nullptr ? settled_outputs(*s) : 0;
}

int PageRun::files_started() const {
    const RunState *s = state_of(this);
    return s != nullptr ? static_cast<int>(s->started_files) : 0;
}

int PageRun::files_terminal() const {
    const RunState *s = state_of(this);
    return s != nullptr ? static_cast<int>(s->terminal_files) : 0;
}

int PageRun::progress_percent() const {
    const RunState *s = state_of(this);
    if (s == nullptr || s->files.isEmpty())
        return -1;
    return static_cast<int>(std::lround(run_frac(*s) * 100.0));
}

int PageRun::eta_seconds() const {
    const RunState *s = state_of(this);
    if (s == nullptr || !s->running)
        return -1;
    const double frac = run_frac(*s);
    if (frac <= 0.001)
        return -1;
    const double elapsed_s = static_cast<double>(s->clock.elapsed()) / 1000.0;
    return static_cast<int>(std::lround(elapsed_s * (1.0 - frac) / frac));
}

void PageRun::begin_run(std::size_t total, const QStringList &names) {
    RunState *s = state_of(this);
    if (!s)
        return;
    reset();
    s->formats = s->plan.format_ids.isEmpty() ? 1 : s->plan.format_ids.size();
    s->running = true;
    build_rows(s, names);
    if (static_cast<std::size_t>(names.size()) != total) {
        qWarning("PageRun::begin_run: total=%llu but names=%lld",
                 static_cast<unsigned long long>(total), static_cast<long long>(names.size()));
    }
    s->task_empty->setVisible(false);
    s->progress->setRange(0, std::max(1, outputs_total()) * 1000);
    s->progress->setValue(0);
    s->state_pill->set_full_text(tr("进行中"));
    s->summary->set_full_text(QString());
    s->open_logs->setEnabled(true);    // 运行期可跳日志（§9.3：跳转入口保持可用）
    s->open_output->setEnabled(false); // 输出目录在运行结束后开放（避免半成品目录误导）
    s->clock.start();
    s->flush->start();
    refresh_widgets(*s);
    s->dirty = false;
}

void PageRun::on_event(const pp::FileEvent &ev) {
    RunState *s = state_of(this);
    if (!s)
        return;
    const int index = static_cast<int>(ev.index);
    if (index < 0 || index >= s->files.size())
        return;
    FileRow &file = s->files[index];
    apply_progress(*s, file, ev.progress);
    if (ev.state == pp::FileState::Queued)
        return;
    if (!is_terminal_state(ev.state)) {
        if (!file.started) {
            file.started = true;
            ++s->started_files;
        }
        file.state = ev.state;
        s->dirty = true;
        return;
    }
    apply_terminal(*s, file, ev);
    s->dirty = true;
}

void PageRun::end_run(const pp::RunSummary &sum, const QString &out_root) {
    RunState *s = state_of(this);
    if (!s)
        return;
    s->flush->stop();
    s->running = false;
    s->finished = true;
    s->out_root = out_root;
    s->out_bytes = sum.out_bytes;
    s->cancel->setEnabled(false);
    s->cancel->setVisible(false); // 完成态不残留「可取消」观感
    if (sum.cancelled > 0)
        s->cancel_requested = true;
    // 防御性收尾：未收到终态事件的行按结果定稿（正常路径调度器对每文件恰好发一次终态）
    for (FileRow &file : s->files) {
        if (file.terminal)
            continue;
        file.terminal = true;
        for (OutRow &out : file.outs) {
            if (!settled_kind(out.kind))
                out.kind = sum.cancelled > 0 ? OutKind::Cancelled : OutKind::Failed;
        }
    }
    s->progress->setRange(0, std::max(1, outputs_total()) * 1000);
    s->state_pill->set_full_text(sum.cancelled > 0 ? tr("已取消") : tr("已完成"));
    QStringList parts;
    parts << tr("成功 %1").arg(static_cast<qulonglong>(sum.ok))
          << tr("失败 %1").arg(static_cast<qulonglong>(sum.failed))
          << tr("跳过 %1").arg(static_cast<qulonglong>(sum.skipped))
          << tr("取消 %1").arg(static_cast<qulonglong>(sum.cancelled))
          << tr("总耗时 %1 s").arg(sum.total_ms / 1000.0, 0, 'f', 1)
          << tr("吞吐 %1").arg(fmt_mb_s(sum.throughput_mb_s))
          << tr("平均 %1 ms/文件").arg(sum.avg_file_ms, 0, 'f', 0);
    s->summary->set_full_text(parts.join(QStringLiteral(" · ")));
    s->open_output->setEnabled(!out_root.isEmpty());
    s->open_logs->setEnabled(true);
    refresh_widgets(*s); // 终态恒满（含失败；口径 = 全部输出已结算）
    s->dirty = false;
}

void PageRun::reset() {
    RunState *s = state_of(this);
    if (!s)
        return;
    s->flush->stop();
    s->clock.invalidate();
    s->running = false;
    s->finished = false;
    s->cancel_requested = false;
    s->dirty = false;
    s->started_files = 0;
    s->terminal_files = 0;
    s->out_bytes = 0;
    s->out_root.clear();
    for (FileRow &file : s->files) {
        delete file.group;
        for (OutRow &out : file.outs)
            delete out.row;
    }
    s->files.clear();
    s->progress->setRange(0, 1);
    s->progress->setValue(0);
    s->percent->set_full_text(QStringLiteral("0%"));
    s->state_pill->set_full_text(tr("就绪"));
    s->summary->set_full_text(tr("点击底栏「开始运行」开始批处理"));
    s->task_empty->set_full_text(tr("尚未开始 · 运行中逐输出行在此展开"));
    s->task_empty->setVisible(true);
    s->cancel->setEnabled(false);
    s->cancel->setVisible(false); // visible ∧ enabled ⇔ is_running()
    s->open_output->setEnabled(false);
    s->open_logs->setEnabled(false);
    refresh_widgets(*s);
}

bool PageRun::is_running() const {
    const RunState *s = state_of(this);
    return s != nullptr && s->running;
}

} // namespace pp::ui
