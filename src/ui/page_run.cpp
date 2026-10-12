// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M5-T9 / W-G3）— 运行页 v6 化：总览（合成条 + 指标单源 kv + 操作行）/
// 任务进度（表：分组行 + 逐输出缩进行） / 日志尾（等宽 4 行真实尾部）。
//
// 规格 = docs/qt-gui-design-spec.md §7.3（运行页卡表）+ §2.5（数据单源）+ §8（状态四档/空态/
//   tooltip）+ §5 R1–R4（行 pitch 28 / 标签轨 80 / 控件度量）+ §10（Qt 实现映射）+ §11（无障碍）；
//   问题台账 docs/gui-review-0.3.0.md 3-01…3-16 与 K4 裁定（要求改·满足）；
//   开发期裁定 docs/m5-adjudications.md A7（按钮高 26）/ A8（字形禁令：状态一律矢量图标）/
//   A9（RunMarkLabel 的屏幕/DPI 变化失效缓存）/ A29（RunSummary::peak_bytes 加性解冻 →
//   「峰值内存」kv 接**真实读数**，量级换档 B→KB→MB→GB；0 = 未采集 → 「—」）。
//
// v6 口径（逐条对应 K4「运行页数据真实性」）：
//   * **指标单源**（修 3-03/3-04）：总览 kv 只有 §7.3 的 8 项（总耗时/吞吐 · 平均/并行 ·
//     交错启动/线程预算 · 汇总/峰值内存），没有第二个"已用时"；"总耗时"运行中走本页时钟、
//     结束时切到引擎的 `RunSummary::total_ms`（同一指标的两个时刻，不是两个数）。未发生的量
//     显示 "—"（§2.5），数值一律正文对比度（3-04 的"整片浅灰像失效面板"）。
//   * **实现性文案出界面**（修 3-05）：任务卡 hint =「实心=已写盘 · 斜纹=合成中」（mockup 逐字），
//     "阶段权重 + 行级子进度"作废；"交错启动/线程预算/并行"的算法口径（§8.1 步距、§8.2 分配）
//     进 tooltip（§2.6「实现细节进 tooltip」）。
//   * **三级动作层级**（修 3-08 + A39 ②/W-R-T13）：导航（查看日志…/打开输出目录）= accent
//     文字链；破坏性（取消剩余）= **红文字链**（红色表达破坏语义，不再是描边危险钮）；结果
//     （再次运行）= 描边次钮 —— 三者高 26（R4 表 + A7），不再"同一层级两副面孔"。
//   * **0 字节 = 异常**（修 3-10）：完成的输出若 0 字节 → 进度条 = 琥珀（**禁绿色成功条**）、
//     状态标记 = 琥珀 Warn 矢量图标（不再是绿勾）、状态胶囊 = 琥珀「0 字节」、大小列显真实
//     「0 B」（不做 KB 四舍五入 —— 3-10 的"若为四舍五入应显示真实字节"）。
//
// 任务表（§7.3「表格：源文件分组行 + 逐输出缩进行 × 进度条(实心/斜纹) × 大小/速度/耗时(右对齐)
//   × 状态胶囊」）：列 = [状态标记 16][名称][进度 88][大小 60][速度 48][耗时 48][状态 132]，
//   行高 = R1 的 28（表头 24）；缩进（逐输出行）只作用于名称列（mockup padding-left:18px）。
//   速度列 = 压缩比（源 ÷ 输出），**基线**（相对源文件）进 tooltip（修 3-11）；完成态**唯一形态**
//   = formkit::PillBadge 绿胶囊「已完成」（修 3-12：文件行文字"完成"/面板头蓝胶囊/任务行"✓"
//   三套词汇收敛为 formkit 一处实现）；失败行带「└ 原因：…」行（§7.3 逐字）。
//   **计数单源**（修 3-13）：输出总数只出现在总览卡头 hint「N / M 个输出」，逐文件「N 个输出」
//   作废（逐输出行就在其下）；状态计数只在「汇总」kv。
//
// 日志尾卡（§7.3「等宽字体 4 行真实尾部（warn/err 着色）+ 查看完整日志…」）：等宽（Consolas）
//   4 行、warn/err 整行着色；**有日志才显示日志名**（修 3-14：面板头显示 run-*.log 而内容却是
//   "尚无日志"的自相矛盾）；空态文案去括号（修 3-15），卡体按 4 行信息量收紧（修 3-16）。
//
// 三级进度（§7.1）与事件折算口径**语义零改动**（M4-T14 落地，T9 只改呈现）：
//   * 运行级 = Σ 输出完成度 / (文件数 × 格式数)；输出完成度 = (0.5·enc + 0.1·meta) / 0.6
//     （= core/progress.cpp `ProgressMux::output_frac()` 的归一），终态输出按"已结算"计 1.0；
//   * 源文件级（分组行）= 共享段（probe/decode/orient/color，§7.2 只算一次）加权；
//   * 输出级（子行）= `shared + 0.5·enc_i + 0.1·meta_i`；合成进度（§7.3）= 斜纹（115°/6px/12px）。
//
// 合成条（总览「进度」）= `RunProgressBar`：QProgressBar 子类，**无障碍角色保持 progressbar、
//   值 = 显示的百分比**（修 3-07：不再把 0..N×1000 的内部量暴露给辅助技术），自绘分段
//   （完成=绿 / 0 字节=琥珀 / 失败=红 / 其余终态=中性 / 进行中=强调）——与 mockup
//   `.pbar i.done/i.warn/i.fail` 同语义。
//
// 节流（§7.4）：事件回调只写内存行状态并置脏位，控件写入由 60ms 定时器统一 flush（行控件对
//   同值早退）。运行期锁定（G5）：本页运行中只有「取消剩余」可交互（**唯一取消入口**不变）；
//   跳转入口（查看日志…/打开输出目录）保持可用；结束态取消钮消失。左栏/步骤钮/设置/预设/预览/
//   分类的置灰在 `MainWindow::lock_for_run()`（T9 零改动）。
//
// 页内状态保存在本翻译单元的注册表（键 = PageRun*，随 QObject::destroyed 释放）—— page_run.h
// 是 PP-FROZEN 头文件（0.3.0 追加面：T14 的 RunPlan/读数 + T9 的 `run_again_requested()`），
// 不含私有成员（§2.13）。本文件无 Q_OBJECT 类（行内控件不发信号），故不含 *.moc 包含
// （与 page_meta.cpp / page_output.cpp 同口径）。
#include "ui/page_run.h"
#include "core/error_text.h" // R-070/273：失败原因行中文映射单源（M7-R2B；本包接线）
#include "ui/contextmenu.h"  // UX-1：公共右键菜单件（一卡一 actions 构造函数）
#include "ui/term_words.h"   // R-139：终态词表 term_words 单源（列表/运行页/状态栏共用一张）

#include <algorithm>
#include <cmath>
#include <map> // R-185：注册表 pimpl（与 page_output 同款 std::map + unique_ptr）
#include <memory>
#include <thread>

#include <QApplication> // theme.h 的 style_mirrors_system_accent() 用 qApp->style()
#include <QClipboard>   // UX-1：「复制日志 / 复制失败原因」
#include <QColor>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu> // UX-1：卡片右键菜单
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

#include "core/progress.h"      // StageWeights（§7.2 单源）
#include "ui/formkit.h"         // M5-T9：行构造器（C2）+ 胶囊/进度条（C8）/令牌（C1）
#include "ui/icons.h"           // M5-T3：状态标记/取消钮 ■ 一律矢量图标（C3）
#include "ui/leading_indent.h"  // R-218（M7-R2E2）：逻辑边距（行首缩进）单源
#include "ui/run_rows_model.h"  // M7-WP9（B.2）：行模型纯语义（runpage）
#include "ui/run_status_text.h" // M7-WP9（B.2）：文案/数值格式纯函数（runpage）
#include "ui/theme.h"

namespace pp::ui {
namespace {

using pp::ProgressInfo;
using pp::Stage;
using pp::StageWeights;

namespace fk = formkit;

constexpr int kFlushMs = 60;     // §7.4：GUI 节流 60ms 刷新
constexpr int kLogTailLines = 4; // §7.3：日志尾 4 行（warn/err 着色）
constexpr int kMarkW = 16;       // 状态标记盒宽（矢量图标 12 居中）
constexpr int kMarkPx = 12;      // 标记图标边长
constexpr int kCellGap =
    fk::kInlineGap; // 表内列距（UX-6④：= formkit kInlineGap；探针 run-geom 按同一张表取证）
constexpr int kRowPadX = theme::Metrics::card_pad_x; // 表左右内距（UX-6④：= 卡内边距 12）
constexpr int kOutIndent = 18;                       // 逐输出行名称缩进（mockup padding-left:18px）
constexpr int kReasonIndent = 36;                    // 失败原因行缩进（缩进行之下再一级）
constexpr int kBarColW = 88;                         // 进度列
constexpr int kSizeColW = 60;                        // 大小列
constexpr int kSpeedColW = 48;                       // 速度列（压缩比）
constexpr int kTimeColW = 48;                        // 耗时列
constexpr int kStatusColW = 132;             // 状态列（胶囊右对齐；容下「编码 16000/16000 行」）
constexpr int kTableHeadH = fk::kTableHeadH; // 表头行高（UX-6①：§5 R1 唯一例外，表头不是信息行）

// 页内动态属性（页面 QSS 的锚；只锚本页控件，不误伤它页）
constexpr const char *kRunLabelProperty = "ppRun";
constexpr const char *kRunHead = "thead";      // 表头/空态
constexpr const char *kRunGName = "gname";     // 分组行名称
constexpr const char *kRunOName = "oname";     // 逐输出行名称
constexpr const char *kRunReason = "reason";   // 失败原因行
constexpr const char *kRunSummary = "summary"; // 卡底摘要句

// ===========================================================================
// 小控件（页内；不进头文件）
// ===========================================================================

// 单行省略标签（§9.3「全界面禁止文案折行」；与 page_meta/page_output 同口径）
// R-020/R-320（M7-WP4）：ElidedLabel 的 7 份逐字拷贝收敛到 formkit 唯一实现
// （本文件原 30+ 行自拷类删除；行为逐位等价 —— 全文 + ppFullText + tooltip + 省略号）。
using ElidedLabel = formkit::ElidedLabel;

// R-218（M7-R2E2）：行首缩进 = 逻辑边距（leading 在左）—— 单源在 ui/leading_indent.h
// （LTR 落左 / RTL 落右 + LayoutDirectionChange 重放；LTR 初值逐字节不变）。

// 总览合成条（§7.3「进度(合成条)」）：QProgressBar 子类 —— **无障碍角色仍是 progressbar、
// 暴露值 = 显示的百分比**（修 3-07），自绘分段（mockup `.pbar i.done|i.warn|i.fail|i.comp`）。
// 段占比按"每输出一格"折算（与运行级完成度同一分母），进行中段 = 剩余部分。
class RunProgressBar : public QProgressBar {
public:
    explicit RunProgressBar(QWidget *parent = nullptr) : QProgressBar(parent) {
        setTextVisible(false);
        setFixedHeight(theme::Metrics::progress_h); // R4 进度条高 6
        setRange(0, 100);                           // 值域 = 百分比（与显示字面量一致）
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void set_tokens(const theme::Tokens &t) {
        tokens_ = t;
        update();
    }
    // 五段占比（0..1；和 ≤ 1，余量 = 轨道）：完成 / 0 字节异常 / 失败 / 其余终态 / 进行中
    void set_segments(double done, double warn, double failed, double other, double active) {
        if (done == done_ && warn == warn_ && failed == failed_ && other == other_ &&
            active == active_) {
            return; // 同值早退
        }
        done_ = done;
        warn_ = warn;
        failed_ = failed;
        other_ = other;
        active_ = active;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const theme::Tokens &t = tokens_;
        const qreal r = height() / 2.0; // R4 r99 → 半高
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(t.control_bd, theme::Metrics::progress_border_px));
        p.setBrush(t.control);
        p.drawRoundedRect(box, r, r);
        const qreal inset = theme::Metrics::progress_border_px;
        const QRectF track = box.adjusted(inset, inset, -inset, -inset);
        if (track.width() <= 0.0 || track.height() <= 0.0)
            return;
        p.setPen(Qt::NoPen);
        const double seg[5] = {done_, warn_, failed_, other_, active_};
        const QColor colors[5] = {t.ok, t.warn, t.err, t.text3, t.accent};
        // R-218（M7-R2E2）：段从行首（leading）铺起 —— LTR=左缘、RTL=右缘；LTR 路径逐字节不变。
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        qreal x = rtl ? track.right() : track.left();
        for (int i = 0; i < 5; ++i) {
            const qreal w = track.width() * std::clamp(seg[i], 0.0, 1.0);
            if (w <= 0.0)
                continue;
            p.setBrush(colors[i]);
            p.drawRect(QRectF(rtl ? x - w : x, track.top(), w, track.height()));
            x += rtl ? -w : w;
        }
    }

private:
    theme::Tokens tokens_{};
    double done_ = 0.0, warn_ = 0.0, failed_ = 0.0, other_ = 0.0, active_ = 0.0;
};

// 状态标记位点（16px 盒，固定尺寸无省略需求 → 普通 QLabel + 矢量 pixmap）：
// ElidedLabel 的 resizeEvent 会 setText（清掉 pixmap），故标记不能用它；同 (图标, 色) 早退，
// 避免 60ms flush 的高频路径反复光栅化。
// A9：屏幕/DPI 变化（跨屏拖动、缩放变更）后缓存 pixmap 的 devicePixelRatio 已过期 →
// 事件当拍起失效，下一拍（dpr 已是新屏的值）按新 dpr 重光栅化（否则标记发虚/糊）。
class RunMarkLabel : public QLabel {
public:
    explicit RunMarkLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setFixedWidth(kMarkW);
        setAlignment(Qt::AlignCenter);
    }
    void set_mark(icons::Name name, const QColor &color) {
        if (name == name_ && color == color_ && !pixmap().isNull())
            return;
        name_ = name;
        color_ = color;
        refresh();
    }
    // R-113（M7-WP4）：死访问器 mark_name() 已删（全仓零调用）

protected:
    bool event(QEvent *e) override {
        // Qt 6 里"屏幕变化"事件 = ScreenChangeInternal（A9 的 QEvent::ScreenChange 即其公开语义）；
        // DevicePixelRatioChange 覆盖不换屏也改变 dpr 的场景（系统缩放在另一显示器上变更）。
        const QEvent::Type type = e->type();
        if (type == QEvent::ScreenChangeInternal || type == QEvent::DevicePixelRatioChange) {
            QTimer::singleShot(0, this, [this]() { refresh(); }); // 下一拍：dpr 已更新
        }
        return QLabel::event(e);
    }
    void paintEvent(QPaintEvent *event) override {
        // 兜底（A9）：绘制前自查 dpr —— 即便平台未投递 ScreenChange 类事件，也保证缓存 pixmap
        // 始终按当前屏的 dpr 光栅化（跨屏拖动/系统缩放变更后不残留旧 dpr 的模糊图标）。
        if (!pixmap().isNull() && dpr_ != devicePixelRatioF())
            refresh();
        QLabel::paintEvent(event);
    }

private:
    void refresh() {
        dpr_ = devicePixelRatioF();
        setPixmap(icons::pixmap(name_, kMarkPx, color_, dpr_));
    }
    icons::Name name_ = icons::Name::Pending;
    QColor color_;
    qreal dpr_ = 0.0; // 缓存 pixmap 的光栅化 dpr（A9 的失效判据）
};

// 日志尾（§7.3「等宽字体 4 行真实尾部（warn/err 着色）」）：等宽字族（Consolas，令牌单源）+
// warn/err 整行着色 + 逐行省略号（§9.3 不折行）；宽度变化后重渲染以保持省略点正确。
class LogLines : public QLabel {
public:
    explicit LogLines(QWidget *parent = nullptr) : QLabel(parent) {
        setTextFormat(Qt::RichText);
        setWordWrap(false);
        setAlignment(Qt::AlignLeft | Qt::AlignTop);
        setFont(mono_font(theme::Typography::mono_px));
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
    // 日志尾等宽字族（§7.3；族名 = Typography::family_mono，本页不写死）
    static QFont mono_font(double px) {
        QFont f = theme::font(px);
        f.setFamilies({QString::fromLatin1(theme::Typography::family_mono), QStringLiteral("Menlo"),
                       QStringLiteral("Courier New")});
        return f;
    }

    void render() {
        if (lines_.isEmpty()) {
            // 空态：去括号的自然语（修 3-15；此时面板头也不显示日志名 —— 见 set_log_tail）
            setText(span(tokens_.text3, PageRun::tr("尚无日志 · 开始运行后在此显示最新记录")));
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

    // 行形态（core/logger.cpp 的 `%H:%M:%S.%e [%l] [%t] %v`）：
    //   * warn 行 → 整行琥珀；err/critical 行 → 整行红（§7.3「warn/err 着色」）；
    //   * 其余 → 时间戳 txt3 + 级别 acc + 正文 txt2。
    QString colorize(const QString &line) const {
        const int space = line.indexOf(QLatin1Char(' '));
        if (space <= 0)
            return span(tokens_.text2, line);
        const QString stamp = line.left(space);
        QString rest = line.mid(space + 1);
        QString level;
        if (rest.startsWith(QLatin1Char('['))) {
            const int close = rest.indexOf(QStringLiteral("] "));
            if (close > 0) {
                level = rest.mid(1, close - 1);
                rest = rest.mid(close + 2);
            }
        }
        const QString lower = level.toLower();
        if (lower.contains(QStringLiteral("warn")))
            return span(tokens_.warn, line);
        if (lower.contains(QStringLiteral("err")) || lower.contains(QStringLiteral("crit")))
            return span(tokens_.err, line);
        if (level.isEmpty())
            return span(tokens_.text3, stamp) + span(tokens_.text2, rest);
        return span(tokens_.text3, stamp) + QStringLiteral(" ") +
               span(tokens_.accent, QStringLiteral("[") + level + QStringLiteral("] ")) +
               span(tokens_.text2, rest);
    }

    static QString span(const QColor &color, const QString &text) {
        return QStringLiteral("<span style=\"color:%1\">%2</span>")
            .arg(theme::css_color(color), text.toHtmlEscaped());
    }

    theme::Tokens tokens_{};
    QStringList lines_;
};

// ===========================================================================
// §7.2 阶段算术 / 文案词汇表（M4-T14 语义零改动）
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

// M7-WP9（B.2 run_status_text / run_rows_model）：阶段文案与数值格式族、终态判定抽入
// ui/run_status_text.h + ui/run_rows_model.h（可单测单元，R-109）；此处 using/转发，
// 调用点零改动（转发 = 冻结面「加性 + 追认」的旧签名保留形态）。
using runpage::fmt_bytes;
using runpage::fmt_duration;
using runpage::fmt_mb_s;
using runpage::fmt_mem_bytes;
using runpage::fmt_secs;
using runpage::format_name;
using runpage::is_terminal_state;
using runpage::row_name;
using runpage::shared_stage_text;
using runpage::write_stage_text;

int hardware_threads() {
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 0 ? static_cast<int>(hw) : 1;
}

// is_terminal_state / format_name 已抽入 ui/run_status_text.h（见上方 using）

// 行状态（结算判定单源 = runpage::OutKind / runpage::settled_kind；ui/run_rows_model.h）
using runpage::OutKind;
using runpage::settled_kind;

// 数值列缓存（文本 + 色；§4.2 数值档 = 右对齐 + tabular）
struct CellCache {
    QString text;
    QColor color;
    void apply(ElidedLabel *label, const QString &value, const QColor &tint) {
        if (text != value) {
            text = value;
            label->set_full_text(value);
        }
        if (!(color.isValid() && color == tint)) {
            color = tint;
            label->setStyleSheet(QStringLiteral("color:%1").arg(theme::css_color(tint)));
        }
    }
};

// R-187（M7-R2.1-F3）：行控件束单源 —— 组行（FileRow）与逐输出行（OutRow）原各带一份逐字
// 同款的「7 控件 + 数值缓存 + 应用缓存」束（deep-review R-187/E3-08「双 struct 双份控件
// 束」）。收敛为唯一 RowBundle，两 struct 嵌入（既有成员名逐字可用，消费点零改动；仅组行
// 数值缓存命名归一 size_cell → size_cache 3 处）。行容器不入束（组行叫 group、输出行叫
// row），失败原因行是输出行专属。
struct RowBundle {
    RunMarkLabel *mark = nullptr;
    ElidedLabel *name = nullptr;
    fk::ProgressStrip *bar = nullptr;
    ElidedLabel *size = nullptr;
    ElidedLabel *speed = nullptr;
    ElidedLabel *time = nullptr;
    fk::PillBadge *status = nullptr;
    // 数值列缓存（文本 + 色；§4.2 数值档 = 右对齐 + tabular）
    CellCache size_cache, speed_cache, time_cache;
    // 已应用缓存（避免同值重写触发 style/repaint）
    QString applied_status;
    fk::PillBadge::Tone applied_tone = fk::PillBadge::Tone::Neutral;
    bool applied_icon_set = false;
    icons::Name applied_icon = icons::Name::Pending;
};

struct OutRow : RowBundle {
    QWidget *row = nullptr;
    QWidget *reason_row = nullptr; // 失败行的「└ 原因：…」行（默认隐藏）
    ElidedLabel *reason = nullptr;
    // 进度（§7.2 段内 frac）
    float enc = 0.f, meta = 0.f;
    bool started = false, synthetic = false;
    Stage last_stage = Stage::Probe;
    bool has_stage = false;
    int rows_now = 0; // 行级回调折算的当前行（progress_max_row 同口径）
    // 终态事实（字节 · 压缩比 · 耗时；失败原因）
    OutKind kind = OutKind::Queued;
    qint64 bytes = 0;
    double ratio = 0.0, ms = 0.0;
    QString error;
    QString applied_reason;
    bool reason_shown = false;
};

struct FileRow : RowBundle {
    QWidget *group = nullptr;
    QVector<OutRow> outs;
    // 共享段（§7.2 前三行；一次）
    float shared_probe = 0.f, shared_decode = 0.f, shared_orient = 0.f, shared_color = 0.f;
    Stage shared_stage = Stage::Probe;
    bool started = false, terminal = false;
    pp::FileState state = pp::FileState::Queued;
    int width = 0, height = 0;
    qint64 src_bytes = 0;
};

// R-187（M7-R2.1-F3）：OutKind（逐输出）/ pp::FileState（逐文件）平行状态机的**互译单点**
// （原 apply_terminal 内联 switch；deep-review R-187/E3-08）。映射真值表：
//   FileState::Done → Done｜Skipped → Skipped｜Cancelled → Cancelled｜
//   其余（Failed/DoneWithErrors/中途异常等一切未列值）→ Failed。
OutKind out_kind_of_file_state(pp::FileState state) {
    switch (state) {
    case pp::FileState::Done:
        return OutKind::Done;
    case pp::FileState::Skipped:
        return OutKind::Skipped;
    case pp::FileState::Cancelled:
        return OutKind::Cancelled;
    default:
        return OutKind::Failed;
    }
}

struct RunState {
    PageRun *page = nullptr;
    theme::Tokens tokens{};
    PageRun::RunPlan plan;

    // 总览卡（§7.3 第一行：进度合成条 + 百分比；kv 4 行；操作行；摘要句）
    QFrame *overview = nullptr;
    fk::PillBadge *state_pill = nullptr;
    ElidedLabel *progress_hint = nullptr;
    RunProgressBar *progress = nullptr;
    ElidedLabel *percent = nullptr;
    ElidedLabel *elapsed = nullptr;    // 总耗时（**唯一**时间指标；无「已用时」第二出处）
    ElidedLabel *throughput = nullptr; // 吞吐
    ElidedLabel *average = nullptr;    // 平均
    ElidedLabel *workers = nullptr;    // 并行（进行中 / 可并行）
    ElidedLabel *stagger = nullptr;    // 交错启动
    ElidedLabel *budget = nullptr;     // 线程预算
    ElidedLabel *tally = nullptr;      // 汇总（成功/异常/失败/跳过/取消）
    ElidedLabel *memory = nullptr;     // 峰值内存
    ElidedLabel *threads = nullptr;    // 峰值线程（R-078：threads_total_est 估算口径）
    QPushButton *open_logs = nullptr;
    QPushButton *open_output = nullptr;
    QPushButton *cancel = nullptr;
    QPushButton *run_again = nullptr;
    ElidedLabel *summary = nullptr;
    QString applied_state_text;
    fk::PillBadge::Tone applied_state_tone = fk::PillBadge::Tone::Neutral;
    bool applied_state_icon_set = false;
    icons::Name applied_state_icon = icons::Name::Pending;
    bool state_pill_icon = false; // 当前状态胶囊是否已带图标（PillBadge 的图标不可撤）

    // 任务进度卡（表头 + 分组行/逐输出行）
    QFrame *tasks = nullptr;
    ElidedLabel *tasks_hint = nullptr;
    ElidedLabel *task_empty = nullptr;
    QWidget *table_head = nullptr;
    QWidget *list = nullptr;
    QVBoxLayout *list_layout = nullptr;

    // 日志尾卡
    QFrame *logs = nullptr;
    ElidedLabel *log_name = nullptr;
    QPushButton *open_full_log = nullptr;
    LogLines *log_lines = nullptr;
    QStringList log_tail;
    QString log_name_text;

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
    // 终态读数（end_run 的引擎汇总；未结束 → 显示走实时折算）
    bool has_summary = false;
    double summary_total_ms = 0.0, summary_mb_s = 0.0, summary_avg_ms = 0.0;
    quint64 summary_peak_bytes = 0; // A29：RunSummary::peak_bytes（像素预算峰值，单源）
    int summary_threads_est = 0;    // R-078：RunSummary::threads_total_est（峰值活跃线程估算）
};

// R-185（M7-R2.1-F3）：注册表 pimpl 两套解法统一为 RAII —— 原 page_run 走 QHash<裸指针> +
// 裸 new + destroyed 信号手动 delete/take，page_output 走 unique_ptr（deep-review R-185/E3-06
// 「同一注册表 pimpl 两套解法」）。现两页同款：std::map<页指针, std::unique_ptr<State>> +
// 访问器 *_of() + **析构 erase**（所有权归注册表，无手动 delete）。
// 指针键注册表：析构擦除 → 无悬挂/别名；GUI 线程独占，无需锁。
std::map<const PageRun *, std::unique_ptr<RunState>> &registry() {
    static std::map<const PageRun *, std::unique_ptr<RunState>> store;
    return store;
}

RunState *state_of(const PageRun *page) {
    const auto it = registry().find(page);
    return it == registry().end() ? nullptr : it->second.get();
}

// 0 字节完成的输出 = **异常**（§8/§7.3；修 3-10）：条/标记/胶囊一律琥珀，绝不落绿色成功态
// —— 判定单源 = runpage::out_zero_bytes（ui/run_rows_model.h）。
using runpage::out_zero_bytes;

bool out_zero_bytes(const OutRow &out) { return runpage::out_zero_bytes(out.kind, out.bytes); }

// 状态四档（§8）：标记色与状态词同档（"状态点与状态词同色"）
QColor tone_color(const RunState &s, fk::PillBadge::Tone tone) {
    switch (tone) {
    case fk::PillBadge::Tone::Ok:
        return s.tokens.ok;
    case fk::PillBadge::Tone::Warn:
        return s.tokens.warn;
    case fk::PillBadge::Tone::Err:
        return s.tokens.err;
    case fk::PillBadge::Tone::Accent:
        return s.tokens.accent;
    case fk::PillBadge::Tone::Neutral:
        break;
    }
    return s.tokens.text3;
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

// §7.1 运行级完成度：Σ 输出完成度 / (文件数 × 格式数)。折算单源 =
// runpage::out_completion / runpage::run_frac（ui/run_rows_model.h，可单测）。
double out_completion(const OutRow &out) {
    return runpage::out_completion(settled_kind(out.kind), out.enc, out.meta);
}

double run_frac(const RunState &s) {
    double sum = 0.0;
    for (const FileRow &file : s.files) {
        for (const OutRow &out : file.outs)
            sum += out_completion(out);
    }
    return runpage::run_frac(static_cast<int>(s.files.size()), s.formats, sum);
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

// ---- 状态文案（§8 四档；完成态唯一形态 = 绿胶囊「已完成」，修 3-12）----

struct StatusText {
    QString text;
    fk::PillBadge::Tone tone = fk::PillBadge::Tone::Neutral;
    icons::Name icon = icons::Name::Pending;
    bool has_icon = false;
    QString tip; // A39 ③：排队 pill 的「交错启动」语义（§2.6：实现细节进 tooltip）
};

// 排队 pill 的 tooltip（W-R-T13 / A39 ③）：pill 文案收「排队」（mockup run.html 的 .pill 逐字），
// 「交错启动」的实现语义移到这里（§2.6 实现细节进 tooltip）；文案随本批次的交错步距。
QString queue_tip(const RunState &s) {
    return s.plan.stagger_ms > 0
               ? PageRun::tr("等待启动：交错启动，每 %1 ms 才启动下一个文件").arg(s.plan.stagger_ms)
               : PageRun::tr("等待启动：前序输出完成后开始");
}

// 输出行阶段文字（未启动/进行中：排队、共享段承载、编码行数、合成、写入）
QString out_stage_text(const FileRow &file, const RunState &s, int index) {
    const OutRow &out = file.outs.at(index);
    if (!out.started) {
        // 尚未启动：文件处于共享段且**无输出已启动** → 首个未完成行承载共享段（§7.2 一次）
        if (index == first_unstarted(file) && !any_output_started(file) && !file.terminal) {
            if (file.started)
                return shared_stage_text(file.shared_stage);
            return PageRun::tr("排队"); // A39 ③：pill 收「排队」（交错语义进 tooltip）
        }
        return PageRun::tr("排队"); // A39 ③
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

StatusText out_status(const FileRow &file, const RunState &s, int index) {
    const OutRow &out = file.outs.at(index);
    switch (out.kind) {
    case OutKind::Done:
        if (out_zero_bytes(out))
            return {PageRun::tr(pp::ui::term_words::kZeroBytes), fk::PillBadge::Tone::Warn,
                    icons::Name::Warn, true}; // R-069/R-139：词表单源
        return {PageRun::tr(pp::ui::term_words::kDone), fk::PillBadge::Tone::Ok, icons::Name::Check,
                true};
    case OutKind::Failed:
        return {PageRun::tr(pp::ui::term_words::kFailed), fk::PillBadge::Tone::Err,
                icons::Name::Error, true};
    case OutKind::Skipped:
        return {PageRun::tr(pp::ui::term_words::kSkipped), fk::PillBadge::Tone::Warn,
                icons::Name::Pending, false};
    case OutKind::Cancelled:
        return {PageRun::tr(pp::ui::term_words::kCancelled), fk::PillBadge::Tone::Neutral,
                icons::Name::Stop, false};
    case OutKind::Active:
        return {out_stage_text(file, s, index), fk::PillBadge::Tone::Accent, icons::Name::Active,
                false};
    case OutKind::Queued:
        break;
    }
    // A39 ③：未启动的 pill 文案统一收「排队」（交错语义进 tooltip）；「首个未完成行承载共享段」
    // 的行不是排队态（文本 = 共享段阶段词）→ 不带排队 tooltip。
    const QString text = out_stage_text(file, s, index);
    const bool queued = !out.started && text == PageRun::tr("排队");
    return {text, fk::PillBadge::Tone::Neutral, icons::Name::Pending, false,
            queued ? queue_tip(s) : QString()};
}

// 分组行状态（文件级）
StatusText file_status(const FileRow &file, const RunState &s) {
    bool zero = false;
    bool any_ok = false, any_bad = false;
    for (const OutRow &out : file.outs) {
        zero = zero || out_zero_bytes(out);
        if (out.kind == OutKind::Done)
            any_ok = true;
        if (out.kind == OutKind::Failed)
            any_bad = true;
    }
    // R-076：多输出"有成有败" = 部分成功（不聚合成 Done）—— 调动器侧仍映射 Done，这里按
    // 逐输出事实补判（与 mainwindow::handle_file_event 的补判同口径）。
    const bool partial = file.state == pp::FileState::Done && any_ok && any_bad;
    switch (file.state) {
    case pp::FileState::DoneWithErrors: // R-076：独立档
        return {PageRun::tr(pp::ui::term_words::kPartial), fk::PillBadge::Tone::Warn,
                icons::Name::Warn, true};
    case pp::FileState::Done:
        if (zero)
            return {PageRun::tr("异常"), fk::PillBadge::Tone::Warn, icons::Name::Warn, true};
        if (partial) // R-076
            return {PageRun::tr(pp::ui::term_words::kPartial), fk::PillBadge::Tone::Warn,
                    icons::Name::Warn, true};
        return {PageRun::tr(pp::ui::term_words::kDone), fk::PillBadge::Tone::Ok, icons::Name::Check,
                true};
    case pp::FileState::Failed:
        return {PageRun::tr(pp::ui::term_words::kFailed), fk::PillBadge::Tone::Err,
                icons::Name::Error, true};
    case pp::FileState::Skipped:
        return {PageRun::tr(pp::ui::term_words::kSkipped), fk::PillBadge::Tone::Warn,
                icons::Name::Pending, false};
    case pp::FileState::Cancelled:
        return {PageRun::tr(pp::ui::term_words::kCancelled), fk::PillBadge::Tone::Neutral,
                icons::Name::Stop, false};
    case pp::FileState::Queued:
        return {PageRun::tr(pp::ui::term_words::kQueued), fk::PillBadge::Tone::Neutral,
                icons::Name::Pending, false,
                queue_tip(s)}; // A39 ③：组行 pill 同收「排队」+ tooltip
    default:
        break;
    }
    return {shared_stage_text(file.shared_stage), fk::PillBadge::Tone::Accent, icons::Name::Active,
            false};
}

// ---- 数值列（§4.2 数值档 = 右对齐 + tabular）----

QString out_size_text(const OutRow &out) {
    if (out.kind == OutKind::Done)
        return fmt_bytes(static_cast<quint64>(std::max<qint64>(0, out.bytes)));
    return QStringLiteral("—");
}

QString ratio_text(double ratio) {
    return ratio > 0.0 ? QStringLiteral("%1×").arg(ratio, 0, 'f', 1) : QStringLiteral("—");
}

// 速度列 tooltip：**标基线**（修 3-11：「0.6×」是相对源文件的压缩倍数，不是一个"速度"）
QString speed_tip(qint64 src_bytes, qint64 out_bytes, double ratio) {
    if (ratio <= 0.0 || out_bytes <= 0)
        return PageRun::tr("压缩比 · 相对源文件（源文件字节 ÷ 输出字节）");
    return PageRun::tr("压缩比 · 相对源文件：%1 ÷ %2 = %3×")
        .arg(fmt_bytes(static_cast<quint64>(std::max<qint64>(0, src_bytes))),
             fmt_bytes(static_cast<quint64>(std::max<qint64>(0, out_bytes))))
        .arg(ratio, 0, 'f', 2);
}

// 分组读数（Σ 字节 / Σ 耗时 / 源 ÷ Σ 输出）
struct GroupFacts {
    qint64 bytes = 0;
    double ms = 0.0;
    double ratio = 0.0;
};

GroupFacts group_facts(const FileRow &file) {
    GroupFacts facts;
    for (const OutRow &out : file.outs) {
        facts.bytes += std::max<qint64>(0, out.bytes);
        facts.ms += std::max(0.0, out.ms);
    }
    if (facts.bytes > 0 && file.src_bytes > 0)
        facts.ratio = static_cast<double>(file.src_bytes) / static_cast<double>(facts.bytes);
    return facts;
}

// 汇总（kv「汇总」；按**输出**计 —— 与卡头「N / M 个输出」同一分母）
struct Tally {
    int ok = 0, warn = 0, failed = 0, skipped = 0, cancelled = 0;

    int settled() const { return ok + warn + failed + skipped + cancelled; }
};

Tally run_tally(const RunState &s) {
    Tally t;
    for (const FileRow &file : s.files) {
        for (const OutRow &out : file.outs) {
            switch (out.kind) {
            case OutKind::Done:
                (out_zero_bytes(out) ? t.warn : t.ok) += 1;
                break;
            case OutKind::Failed:
                t.failed += 1;
                break;
            case OutKind::Skipped:
                t.skipped += 1;
                break;
            case OutKind::Cancelled:
                t.cancelled += 1;
                break;
            case OutKind::Queued:
            case OutKind::Active:
                break;
            }
        }
    }
    return t;
}

QString tally_text(const Tally &t) {
    if (t.settled() == 0)
        return QStringLiteral("—"); // §2.5：未发生的量不显示
    QStringList parts;
    if (t.ok > 0)
        parts << PageRun::tr("成功 %1").arg(t.ok);
    if (t.warn > 0)
        parts << PageRun::tr("异常 %1").arg(t.warn);
    if (t.failed > 0)
        parts << PageRun::tr("失败 %1").arg(t.failed);
    if (t.skipped > 0)
        parts << PageRun::tr("跳过 %1").arg(t.skipped);
    if (t.cancelled > 0)
        parts << PageRun::tr("取消 %1").arg(t.cancelled);
    return parts.join(QStringLiteral(" · "));
}

// ===========================================================================
// 事件 → 行状态（§7.2/§7.3；M4-T14 语义零改动）
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
            // R-187：平行状态机互译单点（原内联 switch 抽为 out_kind_of_file_state）
            out.kind = out_kind_of_file_state(ev.state);
        }
        if (out.kind == OutKind::Failed && out.error.isEmpty())
            out.error = file_error; // 文件级失败（plan/decode）→ 逐输出行回落到聚合原因
    }
}

// ===========================================================================
// 渲染（60ms flush 的统一入口）
// ===========================================================================

void apply_pill(fk::PillBadge *pill, const StatusText &st, QString &text_cache,
                fk::PillBadge::Tone &tone_cache, bool &icon_set, icons::Name &icon_cache) {
    if (text_cache != st.text) {
        text_cache = st.text;
        pill->setText(st.text);
    }
    if (tone_cache != st.tone) {
        tone_cache = st.tone;
        pill->set_tone(st.tone);
    }
    // PillBadge 的图标"一经设置不可清除"（A8 契约）；行程内状态单调 → 首次挂上即可，
    // 仅图标本身变化时重设。需要"有图标 ↔ 无图标"往返的场景走 swap_pill（见状态胶囊）。
    if (st.has_icon && (!icon_set || icon_cache != st.icon)) {
        icon_set = true;
        icon_cache = st.icon;
        pill->set_icon(st.icon);
    }
}

// 状态胶囊的"有图标 ↔ 无图标"往返：PillBadge 没有清图标接口（formkit 是 W-G2 地基，不改），
// 故换一枚同 objectName 的新胶囊（就绪 → 进行中 → 已完成 → 再运行 的真实路径）。
fk::PillBadge *swap_pill(fk::PillBadge *old_pill, const theme::Tokens &tokens) {
    QWidget *host = old_pill->parentWidget();
    auto *layout = host != nullptr ? qobject_cast<QHBoxLayout *>(host->layout()) : nullptr;
    auto *fresh = new fk::PillBadge(host);
    fresh->setObjectName(old_pill->objectName());
    fresh->setText(old_pill->text());
    fresh->set_tone(old_pill->tone());
    fresh->set_tokens(tokens);
    if (layout != nullptr) {
        layout->insertWidget(layout->indexOf(old_pill), fresh);
        layout->removeWidget(old_pill);
    }
    old_pill->hide();
    old_pill->deleteLater();
    return fresh;
}

void render_out_row(RunState &s, FileRow &file, int index) {
    OutRow &out = file.outs[index];
    // 条色：终态按终态色；进行中（含"首个未完成行承载共享段进度"那一行）= accent 实心；
    // 未启动 = 轨道（无填充）。0 字节 = 琥珀（修 3-10：禁绿色成功条）。
    const bool carries_shared = !out.started && file.started && !file.terminal &&
                                !any_output_started(file) && index == first_unstarted(file);
    fk::ProgressStrip::Kind kind = fk::ProgressStrip::Kind::Pending;
    if (settled_kind(out.kind)) {
        switch (out.kind) {
        case OutKind::Done:
            kind =
                out_zero_bytes(out) ? fk::ProgressStrip::Kind::Warn : fk::ProgressStrip::Kind::Done;
            break;
        case OutKind::Failed:
            kind = fk::ProgressStrip::Kind::Failed;
            break;
        case OutKind::Skipped:
        case OutKind::Cancelled:
            kind = fk::ProgressStrip::Kind::Warn; // C8 无"跳过"档：无产出/未完成 → 琥珀
            break;
        case OutKind::Active:
        case OutKind::Queued:
            break;
        }
    } else if (out.started || carries_shared) {
        kind = fk::ProgressStrip::Kind::Active;
    }
    out.bar->set_state(kind, out_row_frac(file, out), out.synthetic);

    const StatusText st = out_status(file, s, index);
    out.mark->set_mark(st.icon, tone_color(s, st.tone));
    apply_pill(out.status, st, out.applied_status, out.applied_tone, out.applied_icon_set,
               out.applied_icon);
    out.status->setToolTip(st.tip); // A39 ③：排队 pill 的 tooltip（其余状态清空）

    // 数值列（完成 = 正文对比度；未发生 = 注释档；0 字节 = 琥珀，修 3-10）
    out.size_cache.apply(out.size, out_size_text(out),
                         out_zero_bytes(out)
                             ? s.tokens.warn
                             : (out.kind == OutKind::Done ? s.tokens.text : s.tokens.text3));
    out.speed_cache.apply(out.speed, ratio_text(out.ratio),
                          out.ratio > 0 ? s.tokens.text : s.tokens.text3);
    out.speed->setToolTip(speed_tip(file.src_bytes, out.bytes, out.ratio));
    out.time_cache.apply(out.time, out.ms > 0.0 ? fmt_secs(out.ms) : QStringLiteral("—"),
                         out.ms > 0.0 ? s.tokens.text : s.tokens.text3);

    // 失败行：下方「└ 原因：…」行（§7.3 逐字）
    const bool failed = out.kind == OutKind::Failed;
    if (failed) {
        // R-070/273：错误分类映射中文（core/error_text.h 单源，R2B 备表）；未命中回退引擎原文
        // （不编造文案），原始串进 tooltip（分类中文 + 原文进 tooltip/日志口径）。
        const std::string err = out.error.toStdString();
        const std::string zh = err.empty() ? std::string() : pp::error_text_zh(err);
        const QString shown = QString::fromStdString(zh.empty() ? err : zh);
        const QString text =
            PageRun::tr("└ 原因：%1").arg(shown.isEmpty() ? PageRun::tr("未报告") : shown);
        if (out.applied_reason != text) {
            out.applied_reason = text;
            out.reason->set_full_text(text);
            out.reason->setToolTip(err.empty() ? text : QString::fromStdString(err));
        }
    }
    if (out.reason_shown != failed) {
        out.reason_shown = failed;
        out.reason_row->setVisible(failed);
    }
}

void render_file_row(RunState &s, FileRow &file) {
    const StatusText st = file_status(file, s);
    file.mark->set_mark(st.icon, tone_color(s, st.tone));
    apply_pill(file.status, st, file.applied_status, file.applied_tone, file.applied_icon_set,
               file.applied_icon);
    file.status->setToolTip(st.tip); // A39 ③：组行排队 pill 的 tooltip（其余状态清空）

    const GroupFacts facts = group_facts(file);
    file.size_cache.apply(file.size,
                          facts.bytes > 0 ? fmt_bytes(static_cast<quint64>(facts.bytes))
                                          : QStringLiteral("—"),
                          facts.bytes > 0 ? s.tokens.text : s.tokens.text3);
    file.speed_cache.apply(file.speed, ratio_text(facts.ratio),
                           facts.ratio > 0 ? s.tokens.text : s.tokens.text3);
    file.speed->setToolTip(speed_tip(file.src_bytes, facts.bytes, facts.ratio));
    file.time_cache.apply(file.time, facts.ms > 0.0 ? fmt_secs(facts.ms) : QStringLiteral("—"),
                          facts.ms > 0.0 ? s.tokens.text : s.tokens.text3);

    // 分组条：失败 > 0 字节异常 > 终态 > 进行中/排队（C8 档位）
    bool any_failed = false;
    bool any_zero = false;
    for (const OutRow &out : file.outs) {
        any_failed = any_failed || out.kind == OutKind::Failed;
        any_zero = any_zero || out_zero_bytes(out);
    }
    fk::ProgressStrip::Kind kind = fk::ProgressStrip::Kind::Pending;
    if (any_failed) {
        kind = fk::ProgressStrip::Kind::Failed;
    } else if (file.terminal) {
        if (any_zero || file.state == pp::FileState::Skipped ||
            file.state == pp::FileState::Cancelled) {
            kind = fk::ProgressStrip::Kind::Warn;
        } else if (file.state == pp::FileState::Done) {
            kind = fk::ProgressStrip::Kind::Done;
        }
    } else if (file.started) {
        kind = fk::ProgressStrip::Kind::Active;
    }
    double frac = 0.0;
    if (!file.outs.isEmpty()) {
        for (const OutRow &out : file.outs)
            frac += out_row_frac(file, out);
        frac /= static_cast<double>(file.outs.size());
    }
    file.bar->set_state(kind, frac, false);
}

// R-188（M7-R2.1-F3）：refresh_widgets 原 160 行单函数吃 7 个语义段（deep-review R-188/E3-09
// 「60ms 节流核心无法局部推理」）→ 按语义拆 7 个段函数（同 R-253 mainwindow 语义分段先例）：
// ① 进度合成条 ② kv 时间/吞吐/平均 ③ 调度读数（并行/交错/线程预算）④ 汇总/峰值
// ⑤ 状态胶囊 ⑥ 操作行 ⑦ 任务表 + 日志尾。共享读数一次算好传入、段调用顺序与原文逐字
// 一致 —— 行为零变化。入口 refresh_widgets 在段后定义（60ms flush 的统一入口不变）。

// 段①（R-188）：总览 —— 进度合成条 + 百分比 + 计数提示
void refresh_overview_progress(RunState &s, int total_outputs, double frac, const Tally &tally,
                               int settled) {
    const int percent_now = s.finished ? 100 : static_cast<int>(std::lround(frac * 100.0));
    if (s.progress->value() != percent_now)
        s.progress->setValue(percent_now);
    const double denom = total_outputs > 0 ? static_cast<double>(total_outputs) : 1.0;
    s.progress->set_segments(static_cast<double>(tally.ok) / denom,
                             static_cast<double>(tally.warn) / denom,
                             static_cast<double>(tally.failed) / denom,
                             static_cast<double>(tally.skipped + tally.cancelled) / denom,
                             std::max(0.0, frac - static_cast<double>(settled) / denom));
    s.percent->set_full_text(QStringLiteral("%1%").arg(percent_now));
    // 计数单源（修 3-13）：输出总数只出现在这里；逐文件「N 个输出」已作废
    s.progress_hint->set_full_text(
        total_outputs > 0 ? PageRun::tr("%1 / %2 个输出").arg(settled).arg(total_outputs)
                          : QString());
}

// 段②（R-188）：总览 kv —— 总耗时 / 吞吐 / 平均（指标单源 + 未发生的量显示 "—"）
void refresh_overview_metrics(RunState &s) {
    const qint64 live_ms = s.clock.isValid() ? s.clock.elapsed() : 0;
    const qint64 total_ms = (s.finished && s.has_summary) ? static_cast<qint64>(s.summary_total_ms)
                                                          : (live_ms > 0 ? live_ms : 0);
    s.elapsed->set_full_text(total_ms > 0 ? fmt_duration(total_ms) : QStringLiteral("—"));
    s.elapsed->setToolTip(PageRun::tr("本次运行的墙钟总耗时（运行中实时累计，结束时取引擎汇总）"));
    const double mb_s = (s.finished && s.has_summary)
                            ? s.summary_mb_s
                            : (live_ms > 0 ? (static_cast<double>(s.out_bytes) / 1.0e6) /
                                                 (static_cast<double>(live_ms) / 1000.0)
                                           : 0.0);
    s.throughput->set_full_text(mb_s > 0 ? fmt_mb_s(mb_s) : QStringLiteral("—"));
    s.throughput->setToolTip(PageRun::tr("已写盘字节 ÷ 用时"));
    const double avg_ms = (s.finished && s.has_summary)
                              ? s.summary_avg_ms
                              : (s.terminal_files > 0 ? static_cast<double>(live_ms) /
                                                            static_cast<double>(s.terminal_files)
                                                      : 0.0);
    s.average->set_full_text(avg_ms > 0 ? PageRun::tr("%1 ms/文件").arg(avg_ms, 0, 'f', 0)
                                        : QStringLiteral("—"));
    s.average->setToolTip(PageRun::tr("每个文件的平均处理时间"));
}

// 段③（R-188）：总览 kv —— 并行 / 交错启动 / 线程预算（§8.2 分配读数进 tooltip）
void refresh_overview_sched(RunState &s) {
    const int budget = s.plan.thread_budget > 0 ? s.plan.thread_budget : hardware_threads();
    const int pool = s.plan.workers > 0 ? s.plan.workers : budget;
    const int inflight =
        std::max(0, static_cast<int>(s.started_files) - static_cast<int>(s.terminal_files));
    const int remaining =
        std::max(0, static_cast<int>(s.files.size()) - static_cast<int>(s.started_files));
    // 并行 = 进行中 / 可并行（读数与引擎同源：进行中 = 已取件 − 已结算）
    s.workers->set_full_text(s.running || s.finished
                                 ? PageRun::tr("%1 / %2").arg(inflight).arg(pool)
                                 : QStringLiteral("—"));
    // §8.2 的分配读数（实现细节）进 tooltip（修 3-05；§2.6：实现细节进 tooltip）。
    // R-065/R-237（M7-R2D2 文案统一）：并行上限与「线程预算」的耦合如实写明 —— workers=自动时
    // 并行数按线程预算与逻辑核数自适应，不是独立常量。
    if (!s.running || s.files.isEmpty()) {
        s.workers->setToolTip(
            PageRun::tr("正在处理的文件数 / 可并行上限\n"
                        "并行上限随「线程预算」联动（并行=自动时按线程预算与逻辑核数折算）"));
    } else {
        const pp::Alloc alloc = pp::alloc_threads(budget, remaining, inflight);
        s.workers->setToolTip(
            PageRun::tr("正在处理的文件数 / 可并行上限\n当前分配：%1 文件并行 + %2 编码多线程"
                        "\n并行上限随「线程预算」联动（并行=自动时按线程预算与逻辑核数折算）")
                .arg(alloc.files)
                .arg(alloc.encode_threads));
    }
    s.stagger->set_full_text(s.plan.stagger_ms > 0 ? PageRun::tr("%1 ms").arg(s.plan.stagger_ms)
                                                   : PageRun::tr("关闭"));
    s.stagger->setToolTip(
        s.plan.stagger_ms > 0
            ? PageRun::tr("每 %1 ms 才启动下一个文件，避免所有文件同时解码占满处理器")
                  .arg(s.plan.stagger_ms)
            : PageRun::tr("不交错：所有文件立即启动"));
    // 线程预算读数（W-R-T13 / A39 ①；修 4-12「同一设置跨界面同名同词」）：自动档的值 = 设置
    // 弹窗同词「自动」（settings·运行页签的「线程预算」下拉首项 + 行注「0 = 自动」）→ 运行页
    // 值形如「自动（16 核）」；旧「自适应（16 核）」措辞作废（review 3-04 的同值不同说）。
    // R-065/R-237（M7-R2D2）：语义改实 —— T 是**总活跃线程上限**（含编码器内部线程，§8.2），
    // 不是"核心数上限"；值文案与「自动 = 按本机核心数」字样为 ui_smoke run-overview 探针
    // 逐字钉住（不动），语义句并入 tooltip。
    s.budget->set_full_text(s.plan.thread_budget > 0
                                ? PageRun::tr("固定 %1 核").arg(s.plan.thread_budget)
                                : PageRun::tr("自动（%1 核）").arg(hardware_threads()));
    s.budget->setToolTip(
        PageRun::tr("总活跃线程上限 T（§8.2，含编码器内部线程；worker+编码线程峰值可到 2W）；"
                    "自动 = 按本机核心数"));
}

// 段④（R-188）：总览 kv —— 汇总 / 峰值内存 / 峰值线程
void refresh_overview_counts(RunState &s, const Tally &tally) {
    s.tally->set_full_text(tally_text(tally));
    s.tally->setToolTip(PageRun::tr("按输出计：成功 / 0 字节异常 / 失败 / 跳过 / 取消"));
    // 峰值内存（A29：解冻加性字段 RunSummary::peak_bytes → 接**真实读数**；与引擎
    // `budget report` 行同一统计点，单源）。运行中未采集（终态汇总才有该读数）→ 「—」（§2.5）
    s.memory->set_full_text(s.has_summary ? fmt_mem_bytes(s.summary_peak_bytes)
                                          : QStringLiteral("—"));
    s.memory->setToolTip(PageRun::tr("本次运行的像素预算峰值（解码/合成期的最大并发占用）"));
    // 峰值线程（R-078）：RunSummary::threads_total_est = peak_active_files +
    // peak_sum_encode_threads（估算口径；SynthPump/x265 库内池不入账）；未采集显「—」
    if (s.threads != nullptr) {
        s.threads->set_full_text(s.has_summary ? QString::number(s.summary_threads_est)
                                               : QStringLiteral("—"));
        s.threads->setToolTip(
            PageRun::tr("本次运行的峰值活跃线程估算（worker 主线程 + 编码器内部线程的实测峰值之和；"
                        "SynthPump 采样线程与 x265 库内线程池不入账 —— 估算口径）"));
    }
}

// 段⑤（R-188）：总览 —— 状态胶囊（§8 四档；完成态唯一形态 = 绿胶囊「已完成」）
void refresh_state_pill(RunState &s, const Tally &tally) {
    StatusText pill;
    if (s.running) {
        pill.text = PageRun::tr("进行中");
        if (tally.failed > 0)
            pill.text += PageRun::tr(" · 失败 %1").arg(tally.failed);
        else if (tally.warn > 0)
            pill.text += PageRun::tr(" · 异常 %1").arg(tally.warn);
        pill.tone = tally.failed > 0
                        ? fk::PillBadge::Tone::Err
                        : (tally.warn > 0 ? fk::PillBadge::Tone::Warn : fk::PillBadge::Tone::Ok);
    } else if (s.finished && s.cancel_requested) {
        pill.text = PageRun::tr("已取消");
        pill.tone = fk::PillBadge::Tone::Neutral;
    } else if (s.finished) {
        pill.text = PageRun::tr("已完成");
        if (tally.failed > 0)
            pill.text += PageRun::tr(" · 失败 %1").arg(tally.failed);
        else if (tally.warn > 0)
            pill.text += PageRun::tr(" · 异常 %1").arg(tally.warn);
        pill.tone = tally.failed > 0
                        ? fk::PillBadge::Tone::Err
                        : (tally.warn > 0 ? fk::PillBadge::Tone::Warn : fk::PillBadge::Tone::Ok);
        if (tally.failed == 0 && tally.warn == 0) {
            pill.has_icon = true; // 完成态唯一形态（A8：勾形 = 矢量图标）
            pill.icon = icons::Name::Check;
        }
    } else {
        pill.text = PageRun::tr("就绪");
        pill.tone = fk::PillBadge::Tone::Neutral;
    }
    if (pill.has_icon != s.state_pill_icon) {
        s.state_pill = swap_pill(s.state_pill, s.tokens);
        s.state_pill_icon = pill.has_icon;
        s.applied_state_text.clear();
        s.applied_state_tone = fk::PillBadge::Tone::Neutral;
        s.applied_state_icon_set = false;
        s.applied_state_icon = icons::Name::Pending;
    }
    apply_pill(s.state_pill, pill, s.applied_state_text, s.applied_state_tone,
               s.applied_state_icon_set, s.applied_state_icon);
}

// 段⑥（R-188）：总览 —— 操作行（三级动作 + 运行期语义）
void refresh_action_row(RunState &s) {
    if (s.running && s.cancel_requested) {
        s.cancel->setEnabled(false);
        s.cancel->setVisible(true);
        s.cancel->setText(PageRun::tr("取消中…"));
    } else if (s.running) {
        s.cancel->setEnabled(true);
        s.cancel->setVisible(true);
        s.cancel->setText(PageRun::tr("取消剩余"));
    } else {
        s.cancel->setEnabled(false);
        s.cancel->setVisible(false); // visible ∧ enabled ⇔ is_running()
    }
    s.run_again->setEnabled(!s.running);
    s.run_again->setToolTip(s.running ? PageRun::tr("运行中：结束后可用")
                                      : PageRun::tr("按当前设置再运行一批"));
}

// 段⑦（R-188）：任务表（组行 + 逐输出行）与日志尾卡
void refresh_task_table(RunState &s) {
    for (FileRow &file : s.files) {
        render_file_row(s, file);
        for (int j = 0; j < file.outs.size(); ++j)
            render_out_row(s, file, j);
    }
    const bool has_rows = !s.files.isEmpty();
    s.table_head->setVisible(has_rows);
    s.task_empty->setVisible(!has_rows);

    // ---- 日志尾（有日志才有名字：修 3-14 的自相矛盾状态）----
    s.log_lines->set_lines(s.log_tail);
    s.log_name->set_full_text(s.log_tail.isEmpty() ? QString() : s.log_name_text);
    s.open_full_log->setEnabled(!s.log_tail.isEmpty() || !s.log_name_text.isEmpty());
}

// R-188：60ms flush 的统一入口 —— 共享读数一次计算 + 段①…⑦ 按原序调用（行为零变化）。
void refresh_widgets(RunState &s) {
    const int total_outputs = static_cast<int>(s.files.size()) * s.formats;
    const double frac = s.finished ? 1.0 : run_frac(s);
    const Tally tally = run_tally(s);
    const int settled = settled_outputs(s);
    refresh_overview_progress(s, total_outputs, frac, tally, settled); // 段①
    refresh_overview_metrics(s);                                       // 段②
    refresh_overview_sched(s);                                         // 段③
    refresh_overview_counts(s, tally);                                 // 段④
    refresh_state_pill(s, tally);                                      // 段⑤
    refresh_action_row(s);                                             // 段⑥
    refresh_task_table(s);                                             // 段⑦
}

// ===========================================================================
// 页面 QSS（tokens 单源 = ui/theme.h；选择器只锚 ppRun/ppField/本页 objectName）
// ===========================================================================

constexpr const char *kFieldProperty =
    theme::kFkProperty; // UX-6⑤/R-054：三条钩子轨并入 ppFk（旧 ppField 轨退役）

QString page_qss(const theme::Tokens &t) {
    // UX-6⑥（M7-WP4）：跨页同形规则 = theme::field_qss（按钮/文字链三级动作），页专规则
    // （ppRun 五档语义色/表头/日志尾框/容器透明）= theme::page_run_qss —— 页内不再拼 QSS。
    return theme::field_qss(t) + theme::page_run_qss(t);
}

// ===========================================================================
// 界面构建
// ===========================================================================

// ---------------------------------------------------------------------------
// UX-1（M7-WP6）：卡片右键菜单 —— 公共件 ui/contextmenu（单一构造点）。
// items **惰性**构造（右键时才求值）→ 运行中置灰实时反映 lock_for_run 状态。
// 一卡一 actions 构造函数：运行总览 = run_overview_actions、任务进度 = task_row_actions、
// 日志尾 = log_tail_actions（docs/ux-review-14.md §1 表 ⑩⑪⑫ 行）。
// ---------------------------------------------------------------------------
void attach_card_menu(QWidget *host, const std::function<std::vector<contextmenu::Item>()> &items) {
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

// R-021/R-053 余量（M7-R2D2）：卡壳/卡头/卡体**单源** = formkit::make_card（本文件原
// make_card / card_head / make_card_body 三件套自拷删除；几何逐值等价 —— 卡头 12,9,12,7 +
// 间距 8、卡体 12,10,12,12，均走 formkit_layout 常量）。调用点各持一份 CardParts。

// 数值列（§4.2 数值档：右对齐 + tabular）
ElidedLabel *make_num_cell(QWidget *parent, const QString &object_name, int width) {
    auto *label = new ElidedLabel(parent);
    label->setObjectName(object_name);
    label->setFont(theme::font(theme::Typography::small_px, QFont::Normal, /*tabular=*/true));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setFixedWidth(width);
    return label;
}

// 状态列（胶囊右对齐；列宽固定 → 各列不错位）
struct StatusCell {
    QWidget *cell = nullptr;
    fk::PillBadge *pill = nullptr;
};

StatusCell make_status_cell(QWidget *parent, const QString &object_name,
                            const theme::Tokens &tokens) {
    StatusCell out;
    out.cell = new QWidget(parent);
    out.cell->setFixedWidth(kStatusColW);
    auto *line = new QHBoxLayout(out.cell);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(0);
    line->addStretch(1);
    out.pill = new fk::PillBadge(out.cell);
    out.pill->setObjectName(object_name);
    out.pill->set_tokens(tokens);
    line->addWidget(out.pill);
    return out;
}

// 表行（R1：定高 28）
QWidget *make_table_row(QWidget *parent, const QString &object_name, int height) {
    auto *row = new QWidget(parent);
    row->setObjectName(object_name);
    row->setFixedHeight(height);
    auto *line = new QHBoxLayout(row);
    line->setContentsMargins(kRowPadX, 0, kRowPadX, 0);
    line->setSpacing(kCellGap);
    return row;
}

// 行命名（探针钩子；test/ui_smoke.py 的 run-geom 与 mainwindow 的 T14 探针按这些名字取证）
struct RowNames {
    QString row, mark, name, bar, size, speed, time, status;
};

RowNames out_names(int i, int j) {
    const QString tag = QStringLiteral("%1-%2").arg(i).arg(j);
    const auto n = [&tag](const char *prefix) { return QString::fromLatin1(prefix) + tag; };
    return {n("pp-run-row-"),  n("pp-run-mark-"),  n("pp-run-oname-"), n("pp-run-bar-"),
            n("pp-run-size-"), n("pp-run-speed-"), n("pp-run-time-"),  n("pp-run-status-")};
}

RowNames group_names(int i) {
    const QString tag = QString::number(i);
    const auto n = [&tag](const char *prefix) { return QString::fromLatin1(prefix) + tag; };
    return {n("pp-run-group-"), n("pp-run-gmark-"),  n("pp-run-gname-"), n("pp-run-gbar-"),
            n("pp-run-gsize-"), n("pp-run-gspeed-"), n("pp-run-gtime-"), n("pp-run-gstatus-")};
}

// 行骨架（名称列 = 弹性列，其余定宽；缩进只作用于名称列）
struct RowSkeleton {
    QWidget *row = nullptr;
    RunMarkLabel *mark = nullptr;
    ElidedLabel *name = nullptr;
    fk::ProgressStrip *bar = nullptr;
    ElidedLabel *size = nullptr;
    ElidedLabel *speed = nullptr;
    ElidedLabel *time = nullptr;
    fk::PillBadge *status = nullptr;
};

RowSkeleton make_row_skeleton(QWidget *parent, const RowNames &names, const theme::Tokens &tokens,
                              bool output_row) {
    RowSkeleton out;
    out.row = make_table_row(parent, names.row, fk::kRowPitch);
    auto *line = qobject_cast<QHBoxLayout *>(out.row->layout());
    out.mark = new RunMarkLabel(out.row);
    out.mark->setObjectName(names.mark);
    line->addWidget(out.mark);
    out.name = new ElidedLabel(out.row);
    out.name->setObjectName(names.name);
    out.name->setFont(theme::font(theme::Typography::small_px, output_row ? QFont::Normal : 600));
    out.name->setProperty(kRunLabelProperty,
                          QString::fromLatin1(output_row ? kRunOName : kRunGName));
    if (output_row)
        set_leading_indent(out.name, kOutIndent); // mockup：只缩名称列（R-218：逻辑边距）
    line->addWidget(out.name, 1);
    out.bar = new fk::ProgressStrip(out.row);
    out.bar->setObjectName(names.bar);
    out.bar->setFixedWidth(kBarColW);
    out.bar->set_tokens(tokens);
    line->addWidget(out.bar);
    out.size = make_num_cell(out.row, names.size, kSizeColW);
    line->addWidget(out.size);
    out.speed = make_num_cell(out.row, names.speed, kSpeedColW);
    line->addWidget(out.speed);
    out.time = make_num_cell(out.row, names.time, kTimeColW);
    line->addWidget(out.time);
    const StatusCell status = make_status_cell(out.row, names.status, tokens);
    out.status = status.pill;
    line->addWidget(status.cell);
    return out;
}

// 表头行（§7.3 表格列题；mockup 逐字：文件 / 输出 · 进度 · 大小 · 速度 · 耗时 · 状态）
QWidget *make_table_head(QWidget *parent) {
    auto *head = new QFrame(parent);
    head->setObjectName(QStringLiteral("pp-run-thead"));
    head->setFixedHeight(kTableHeadH);
    auto *line = new QHBoxLayout(head);
    line->setContentsMargins(kRowPadX, 0, kRowPadX, 0);
    line->setSpacing(kCellGap);
    const auto label = [head](const QString &text, Qt::Alignment align, int width) {
        auto *l = new QLabel(text, head);
        l->setProperty(kRunLabelProperty, QString::fromLatin1(kRunHead));
        l->setFont(theme::font(theme::Typography::badge_px, 600));
        l->setAlignment(align | Qt::AlignVCenter);
        l->setWordWrap(false);
        if (width > 0)
            l->setFixedWidth(width);
        return l;
    };
    line->addSpacing(kMarkW);
    line->addWidget(label(PageRun::tr("文件 / 输出"), Qt::AlignLeft, 0), 1);
    line->addWidget(label(PageRun::tr("进度"), Qt::AlignLeft, kBarColW));
    line->addWidget(label(PageRun::tr("大小"), Qt::AlignRight, kSizeColW));
    line->addWidget(label(PageRun::tr("速度"), Qt::AlignRight, kSpeedColW));
    line->addWidget(label(PageRun::tr("耗时"), Qt::AlignRight, kTimeColW));
    line->addWidget(label(PageRun::tr("状态"), Qt::AlignRight, kStatusColW));
    return head;
}

// 总览卡（§7.3 第一行：进度合成条 ｜ kv 八项 ｜ 操作行 + 摘要句）
void build_overview_card(QWidget *parent, RunState *s) {
    const formkit::CardParts parts =
        formkit::make_card(parent, PageRun::tr("运行总览"), QStringLiteral("pp-run-state-pill"),
                           QStringLiteral("pp-run-progress-hint"));
    s->overview = parts.frame;
    s->state_pill = parts.pill_badge();
    s->state_pill->setVisible(true); // card_header 初值隐藏（有内容才画）；状态胶囊常驻
    s->state_pill->set_tokens(s->tokens);
    s->state_pill->setText(PageRun::tr("就绪"));
    s->progress_hint = parts.hint_label();

    QWidget *body = parts.body;
    auto *body_v = qobject_cast<QVBoxLayout *>(body->layout());

    // 行 1：进度（R2 单列行：标签轨 + 轨内 = 合成条 + 百分比读数）
    {
        auto *host = new QWidget(body);
        auto *hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(8);
        s->progress = new RunProgressBar(host);
        s->progress->setObjectName(QStringLiteral("pp-run-progress"));
        s->progress->set_tokens(s->tokens);
        s->progress->setAccessibleName(PageRun::tr("运行进度")); // §11：progressbar 语义
        hl->addWidget(s->progress, 1);
        s->percent = new ElidedLabel(host);
        s->percent->setObjectName(QStringLiteral("pp-run-percent"));
        s->percent->setFont(theme::font(theme::Typography::body_px, QFont::Normal, true));
        s->percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        s->percent->setFixedWidth(38); // mockup：<span class="kv-v" style="width:38px">
        hl->addWidget(s->percent);
        const fk::Row row = fk::frow(body, PageRun::tr("进度"), host, fk::FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-run-progress-row"));
        body_v->addWidget(row.row);
    }

    // kv 4 行（§7.3 逐字：总耗时/吞吐 · 平均/并行 · 交错启动/线程预算 · 汇总/峰值内存）
    {
        struct KvCell {
            QString key;
            QString object_name;
            ElidedLabel **out;
        };
        struct KvLine {
            KvCell left;
            KvCell right;
        };
        const KvLine lines[4] = {
            {{PageRun::tr("总耗时"), QStringLiteral("pp-run-elapsed"), &s->elapsed},
             {PageRun::tr("吞吐"), QStringLiteral("pp-run-throughput"), &s->throughput}},
            {{PageRun::tr("平均"), QStringLiteral("pp-run-avg"), &s->average},
             {PageRun::tr("并行"), QStringLiteral("pp-run-workers"), &s->workers}},
            {{PageRun::tr("交错启动"), QStringLiteral("pp-run-stagger"), &s->stagger},
             {PageRun::tr("线程预算"), QStringLiteral("pp-run-budget"), &s->budget}},
            {{PageRun::tr("汇总"), QStringLiteral("pp-run-tally"), &s->tally},
             {PageRun::tr("峰值内存"), QStringLiteral("pp-run-memory"), &s->memory}},
        };
        for (const KvLine &line : lines) {
            const auto value_cell = [body](const QString &object_name) {
                auto *value = new ElidedLabel(body);
                value->setObjectName(object_name);
                value->setFont(theme::font(theme::Typography::body_px, QFont::Normal, true));
                value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                value->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkValue));
                value->setText(QStringLiteral("—"));
                return value;
            };
            fk::FieldCell left;
            left.label = line.left.key;
            left.width = fk::FieldWidth::Flex;
            left.field = value_cell(line.left.object_name);
            fk::FieldCell right;
            right.label = line.right.key;
            right.width = fk::FieldWidth::Flex;
            right.field = value_cell(line.right.object_name);
            const fk::DoubleRow row = fk::double_row(body, left, right);
            row.row->setObjectName(line.left.object_name + QStringLiteral("-row"));
            body_v->addWidget(row.row);
            *line.left.out = static_cast<ElidedLabel *>(left.field);
            *line.right.out = static_cast<ElidedLabel *>(right.field);
        }
        // R-078（M7-R2D2）：峰值线程读数行 —— 消费 RunSummary::threads_total_est（WP8 加性
        // 字段）；未结束/未采集显「—」不编数（M5-A24 口径），估算口径进 tooltip。
        {
            auto *value = new ElidedLabel(body);
            value->setObjectName(QStringLiteral("pp-run-threads"));
            value->setFont(
                theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            value->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkValue));
            value->setText(QStringLiteral("—"));
            s->threads = value;
            const fk::Row row =
                fk::frow(body, PageRun::tr("峰值线程"), value, fk::FieldWidth::Flex);
            row.row->setObjectName(QStringLiteral("pp-run-threads-row"));
            body_v->addWidget(row.row);
        }
    }

    // 操作行（§7.3：查看日志…/打开输出目录/取消剩余/再次运行；三级动作层级，R4 高 26）
    {
        auto *host = new QWidget(body);
        auto *hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(8);
        const auto link = [host](const QString &text, const QString &object_name) {
            auto *button = new QPushButton(text, host);
            button->setObjectName(object_name);
            button->setProperty(kFieldProperty, QStringLiteral("link"));
            button->setFont(theme::font(theme::Typography::body_px, 600));
            button->setFixedHeight(theme::Metrics::btn_h);
            button->setCursor(Qt::PointingHandCursor);
            button->setAccessibleName(text); // §11
            button->setToolTip(text);
            return button;
        };
        s->open_logs = link(PageRun::tr("查看日志…"), QStringLiteral("pp-run-open-logs"));
        hl->addWidget(s->open_logs);
        s->open_output = link(PageRun::tr("打开输出目录"), QStringLiteral("pp-run-open-output"));
        hl->addWidget(s->open_output);
        s->cancel = new QPushButton(PageRun::tr("取消剩余"), host);
        s->cancel->setObjectName(QStringLiteral("pp-run-cancel"));
        // W-R-T13（A39 ②）：破坏性动作 = **红文字链**（视觉终稿动作层级：导航链 / 破坏性红链 /
        // 结果次钮三副面孔）。红描边钮形态退役（旧 "danger" 属性值不再出现在本页 QSS）。
        s->cancel->setProperty(kFieldProperty, QStringLiteral("link-danger"));
        s->cancel->setFont(theme::font(theme::Typography::body_px, 600));
        s->cancel->setFixedHeight(theme::Metrics::btn_h); // R4/A7 按钮高 26
        s->cancel->setCursor(Qt::PointingHandCursor);
        s->cancel->setAccessibleName(PageRun::tr("取消剩余")); // §11
        s->cancel->setToolTip(PageRun::tr("取消尚未开始的输出，已完成的输出保留"));
        hl->addWidget(s->cancel);
        hl->addStretch(1);
        s->run_again = new QPushButton(PageRun::tr("再次运行"), host);
        s->run_again->setObjectName(QStringLiteral("pp-run-again"));
        s->run_again->setProperty(kFieldProperty, QStringLiteral("button"));
        s->run_again->setFont(theme::font(theme::Typography::body_px, 600));
        s->run_again->setFixedHeight(theme::Metrics::btn_h);
        s->run_again->setCursor(Qt::PointingHandCursor);
        s->run_again->setAccessibleName(PageRun::tr("再次运行")); // §11
        hl->addWidget(s->run_again);
        const fk::Row row = fk::frow(body, PageRun::tr("操作"), host, fk::FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-run-ops-row"));
        body_v->addWidget(row.row);
    }

    // 摘要句（空态 = 引导；运行中空；结束 = 结果去向 —— **不含任何指标重复**，§2.5）
    s->summary = new ElidedLabel(s->overview);
    s->summary->setObjectName(QStringLiteral("pp-run-summary"));
    s->summary->setFont(theme::font(theme::Typography::small_px));
    s->summary->setProperty(kRunLabelProperty, QString::fromLatin1(kRunSummary));
    s->summary->setContentsMargins(kRowPadX, 6, kRowPadX, 0);
    body_v->addWidget(s->summary);

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
    QObject::connect(s->run_again, &QPushButton::clicked, s->page,
                     [s]() { emit s->page->run_again_requested(); });
}

// 任务进度卡（§7.3 第二行：表头 + 分组行 + 逐输出行 + 失败原因行）
void build_tasks_card(QWidget *parent, RunState *s) {
    const formkit::CardParts parts = formkit::make_card(parent, PageRun::tr("任务进度"), QString(),
                                                        QStringLiteral("pp-run-tasks-hint"));
    s->tasks = parts.frame;
    s->tasks_hint = parts.hint_label();
    s->tasks_hint->set_full_text(PageRun::tr("实心=已写盘 · 斜纹=合成中")); // mockup 逐字

    QWidget *body = parts.body;
    auto *body_v = qobject_cast<QVBoxLayout *>(body->layout());
    s->table_head = make_table_head(body);
    body_v->addWidget(s->table_head);
    s->list = new QWidget(body);
    s->list->setObjectName(QStringLiteral("pp-run-list"));
    s->list_layout = new QVBoxLayout(s->list);
    s->list_layout->setContentsMargins(0, 0, 0, 0);
    s->list_layout->setSpacing(0);
    body_v->addWidget(s->list);
    s->task_empty = new ElidedLabel(body);
    s->task_empty->setObjectName(QStringLiteral("pp-run-task-empty"));
    s->task_empty->setFont(theme::font(theme::Typography::body_px));
    s->task_empty->setProperty(kRunLabelProperty, QString::fromLatin1(kRunHead));
    s->task_empty->setContentsMargins(kRowPadX, 6, kRowPadX, 0);
    body_v->addWidget(s->task_empty);
}

// 日志尾卡（§7.3 第三行：等宽 4 行 + 查看完整日志…；卡头右置 = 日志名 + 文字链）
void build_logs_card(QWidget *parent, RunState *s) {
    const formkit::CardParts parts = formkit::make_card(parent, PageRun::tr("日志尾"), QString(),
                                                        QStringLiteral("pp-run-log-name"));
    s->logs = parts.frame;
    s->log_name = parts.hint_label();
    s->log_name->setProperty(kRunLabelProperty, QString::fromLatin1(kRunHead));
    QHBoxLayout *head = qobject_cast<QHBoxLayout *>(parts.head->layout());
    s->open_full_log = new QPushButton(PageRun::tr("查看完整日志…"), s->logs);
    s->open_full_log->setObjectName(QStringLiteral("pp-run-open-full-log"));
    s->open_full_log->setProperty(kFieldProperty, QStringLiteral("link"));
    s->open_full_log->setFont(theme::font(theme::Typography::body_px, 600));
    s->open_full_log->setFixedHeight(theme::Metrics::btn_h);
    s->open_full_log->setCursor(Qt::PointingHandCursor);
    s->open_full_log->setAccessibleName(PageRun::tr("查看完整日志…")); // §11
    s->open_full_log->setToolTip(PageRun::tr("在文件管理器中打开日志文件"));
    head->addWidget(s->open_full_log);

    QWidget *body = parts.body;
    auto *body_v = qobject_cast<QVBoxLayout *>(body->layout());
    auto *box = new QFrame(body);
    box->setObjectName(QStringLiteral("pp-run-log-box"));
    auto *box_v = new QVBoxLayout(box);
    box_v->setContentsMargins(10, 8, 10, 8);
    box_v->setSpacing(0);
    s->log_lines = new LogLines(box);
    s->log_lines->setObjectName(QStringLiteral("pp-run-log-lines"));
    s->log_lines->set_tokens(s->tokens);
    box_v->addWidget(s->log_lines);
    body_v->addWidget(box);

    QObject::connect(s->open_full_log, &QPushButton::clicked, s->page,
                     [s]() { emit s->page->open_logs_requested(); });
}

// 行控件（分组行 + 逐输出子行 + 失败原因行）：先建后填（N 格式 N 行；§7.3）
void build_rows(RunState *s, const QStringList &names) {
    const int formats = s->formats;
    for (int i = 0; i < names.size(); ++i) {
        const RowSkeleton skel =
            make_row_skeleton(s->list, group_names(i), s->tokens, /*output_row=*/false);
        FileRow file;
        file.group = skel.row;
        file.mark = skel.mark;
        file.name = skel.name;
        file.bar = skel.bar;
        file.size = skel.size;
        file.speed = skel.speed;
        file.time = skel.time;
        file.status = skel.status;
        file.name->set_full_text(row_name(names.at(i))); // 修 3-13：不再重复「N 个输出」
        s->list_layout->addWidget(file.group);

        if (i < s->plan.heights.size())
            file.height = s->plan.heights.at(i);
        if (i < s->plan.widths.size())
            file.width = s->plan.widths.at(i);
        if (i < s->plan.bytes.size())
            file.src_bytes = s->plan.bytes.at(i);

        file.outs.reserve(formats);
        for (int j = 0; j < formats; ++j) {
            const RowSkeleton orow =
                make_row_skeleton(s->list, out_names(i, j), s->tokens, /*output_row=*/true);
            OutRow out;
            out.row = orow.row;
            out.mark = orow.mark;
            out.name = orow.name;
            out.bar = orow.bar;
            out.size = orow.size;
            out.speed = orow.speed;
            out.time = orow.time;
            out.status = orow.status;
            const QString fmt_id =
                j < s->plan.format_ids.size() ? s->plan.format_ids.at(j) : QString();
            out.name->set_full_text(QStringLiteral("└ %1").arg(format_name(fmt_id)));
            s->list_layout->addWidget(out.row);

            // 失败行原因行（默认隐藏；§7.3「失败行含 └ 原因：… 行」）
            out.reason_row = make_table_row(
                s->list, QStringLiteral("pp-run-reason-%1-%2").arg(i).arg(j), fk::kRowPitch);
            out.reason = new ElidedLabel(out.reason_row);
            out.reason->setObjectName(QStringLiteral("pp-run-reason-text-%1-%2").arg(i).arg(j));
            out.reason->setFont(theme::font(theme::Typography::small_px));
            out.reason->setProperty(kRunLabelProperty, QString::fromLatin1(kRunReason));
            set_leading_indent(out.reason, kReasonIndent); // R-218：逻辑边距（LTR=左/RTL=右）
            auto *reason_line = qobject_cast<QHBoxLayout *>(out.reason_row->layout());
            reason_line->addWidget(out.reason, 1);
            out.reason_row->setVisible(false);
            s->list_layout->addWidget(out.reason_row);

            file.outs.append(out);
        }
        s->files.append(file);
    }
}

} // namespace

// ===========================================================================
// PageRun（公开面）
// ===========================================================================

// R-185：析构擦除（与 PageOutput::~PageOutput 同款）——unique_ptr 自动释放 RunState；
// 释放时点在 ~PageRun 体内（原 destroyed 信号 delete/take 在 ~QObject 段；RunState 析构
// 无副作用、全部访问点对 state_of 判空，两时点等价）。
PageRun::~PageRun() { registry().erase(this); }

PageRun::PageRun(QWidget *parent) : QWidget(parent) {
    Q_ASSERT(registry().find(this) == registry().end());
    // R-185：RAII 注册表 —— unique_ptr 所有权入表（原裸 new + destroyed 信号 delete/take；
    // 释放点收敛到 ~PageRun 的 registry().erase，与 PageOutput 同款）
    auto owned = std::make_unique<RunState>();
    RunState *s = owned.get();
    s->page = this;
    registry().emplace(this, std::move(owned));

    // R-009（M7-WP4）：构造期 tokens 取**当前生效档**（原硬编码 Dark → 浅色主题整页深色绘制）
    s->tokens = theme::tokens(theme::preferred_theme_mode());
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
    v->setSpacing(theme::Metrics::gap); // 卡间距 8（§4.3）
    scroll->setWidget(content);

    build_overview_card(content, s);
    build_tasks_card(content, s);
    build_logs_card(content, s);
    v->addWidget(s->overview);
    v->addWidget(s->tasks);
    v->addWidget(s->logs);
    v->addStretch(1);
    // UX-11（M7-R2D2）：卡片防压缩 —— 滚动区(widgetResizable) 把 Preferred 件压向
    // minimumSizeHint；三卡按当前内容 sizeHint 钉下限，滚动条只在卡片整体超出时出现。
    formkit::lock_card_size(s->overview);
    formkit::lock_card_size(s->tasks);
    formkit::lock_card_size(s->logs);

    // ---- UX-1（M7-WP6）：三张卡各一个右键菜单（公共件 = ui/contextmenu）----
    attach_card_menu(s->overview, [this, s] {
        contextmenu::RunOverviewIntents in;
        in.running = s->running;
        in.open_log = [this] { emit open_logs_requested(); };
        in.open_output_dir = [this, s] { emit open_output_requested(s->out_root); };
        in.cancel_remaining = [this] { emit cancel_requested(); };
        in.run_again = [this] { emit run_again_requested(); };
        return contextmenu::run_overview_actions(in);
    });
    attach_card_menu(s->tasks, [s] {
        contextmenu::TaskRowIntents in;
        in.running = s->running;
        // 复制失败原因 = 当前可见的全部「└ 原因：…」行（R-144 的下钻替代面：失败清单可带走）
        in.copy_failure = [s] {
            QStringList reasons;
            for (const FileRow &file : s->files) {
                for (const OutRow &out : file.outs) {
                    if (out.reason == nullptr || !out.reason->isVisible())
                        continue;
                    // ElidedLabel 的完整文本在 ppFullText 动态属性（省略号版在 text()）
                    const QVariant full = out.reason->property("ppFullText");
                    const QString text = full.isValid() ? full.toString() : out.reason->text();
                    if (!text.isEmpty())
                        reasons << text;
                }
            }
            if (QClipboard *clip = QGuiApplication::clipboard())
                clip->setText(reasons.join(QLatin1Char('\n')));
        };
        // 「跳到该文件 / 重试该文件」需要行级上下文（逐输出行的右键）；本轮未接线 → 置灰，
        // 见 build/m7-wp6/handoff.md 残余项。
        return contextmenu::task_row_actions(in);
    });
    attach_card_menu(s->logs, [s] {
        contextmenu::LogTailIntents in;
        in.running = s->running;
        in.copy_log = [s] {
            if (QClipboard *clip = QGuiApplication::clipboard())
                clip->setText(s->log_lines != nullptr
                                  ? s->log_lines->lines().join(QLatin1Char('\n'))
                                  : QString());
        };
        in.open_log_file = [s] {
            if (s->page != nullptr)
                emit s->page->open_logs_requested();
        };
        in.clear_display = [s] {
            if (s->log_lines != nullptr)
                s->log_lines->set_lines({});
        };
        return contextmenu::log_tail_actions(in);
    });

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
    // W-R-T13（A39 ②）：取消剩余已是**文字链**（mockup run.html 的 .btn.lnk 逐字：与「查看日志…」
    // 「打开输出目录」同形、仅换色）→ 钮上不再挂矢量图标（旧 danger 钮时代的 ■ 已随形态一并退役；
    // 取色交给 QSS 的 link-danger 档）。
    s->progress->set_tokens(tokens);
    s->state_pill->set_tokens(tokens);
    for (FileRow &file : s->files) {
        file.bar->set_tokens(tokens);
        file.status->set_tokens(tokens);
        for (OutRow &out : file.outs) {
            out.bar->set_tokens(tokens);
            out.status->set_tokens(tokens);
        }
    }
    s->log_lines->set_tokens(tokens);
    // 浅色卡阴影（§4.1 shadow 0 1px 4px rgba(16,24,40,.07)；深色无阴影）
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
    // §7.3：只渲染末 4 行（真实尾部）；**有日志才有日志名**（修 3-14 的自相矛盾状态）
    s->log_tail = lines.mid(std::max(0, static_cast<int>(lines.size()) - kLogTailLines));
    s->log_name_text = s->log_tail.isEmpty() ? QString() : log_name;
    refresh_widgets(*s);
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
    s->progress->setRange(0, 100); // 值 = 百分比（无障碍暴露值与显示一致，修 3-07）
    s->progress->setValue(0);
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
    s->has_summary = true;
    s->summary_total_ms = sum.total_ms;
    s->summary_mb_s = sum.throughput_mb_s;
    s->summary_avg_ms = sum.avg_file_ms;
    s->summary_peak_bytes = sum.peak_bytes;         // A29（加性字段；M4 前恒 0 → 读数「—」）
    s->summary_threads_est = sum.threads_total_est; // R-078（WP8 加性字段；估算口径见 tooltip）
    if (sum.cancelled > 0)
        s->cancel_requested = true;
    // 防御性收尾：未收到终态事件的行按结果定稿（正常路径调度器对每文件恰好发一次终态）
    // R-141/R-225：**同时回写 file.state** —— 原实现只定稿 out.kind，组行胶囊仍停在"排队/处理中"，
    // 与其下输出行"失败/已取消"同屏矛盾。
    for (FileRow &file : s->files) {
        if (file.terminal)
            continue;
        file.terminal = true;
        for (OutRow &out : file.outs) {
            if (!settled_kind(out.kind))
                out.kind = sum.cancelled > 0 ? OutKind::Cancelled : OutKind::Failed;
        }
        // 组行终态与输出行同判：全部取消 → Cancelled；否则按"有失败 = Failed、否则 Done"。
        bool any_failed = false, any_cancelled = false, any_ok = false;
        for (const OutRow &out : file.outs) {
            any_failed = any_failed || out.kind == OutKind::Failed;
            any_cancelled = any_cancelled || out.kind == OutKind::Cancelled;
            any_ok = any_ok || out.kind == OutKind::Done;
        }
        file.state = any_cancelled && !any_ok && !any_failed ? pp::FileState::Cancelled
                     : any_failed && any_ok                  ? pp::FileState::DoneWithErrors
                     : any_failed                            ? pp::FileState::Failed
                                                             : pp::FileState::Done;
    }
    s->progress->setRange(0, 100);
    s->open_output->setEnabled(!out_root.isEmpty());
    s->open_logs->setEnabled(true);
    // R-006：取消文案与**实际语义**一致 —— 「再次运行」现在是补齐语义（只提交缺件、
    // 已存在且非空的输出跳过），故这句不再是空头支票。
    // R-073：结束语按结果分档，但**只在全批失败**时换失败档 —— 原句"可打开输出目录查看结果"
    // 对"有产出"的批次依然成立（ui_smoke 的 single-source 探针逐字钉住该句，见 T14）。
    if (sum.cancelled > 0)
        s->summary->set_full_text(tr("已取消 · 剩余文件未处理（可再次运行补齐缺件）"));
    else if (sum.failed > 0 && sum.ok == 0 && sum.skipped == 0)
        s->summary->set_full_text(
            tr("运行结束 · 全部 %1 个失败，无可查看的输出（可再次运行补齐缺件）").arg(sum.failed));
    else
        s->summary->set_full_text(tr("运行结束 · 可打开输出目录查看结果"));
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
    s->has_summary = false;
    s->summary_total_ms = 0.0;
    s->summary_mb_s = 0.0;
    s->summary_avg_ms = 0.0;
    s->summary_peak_bytes = 0;
    s->summary_threads_est = 0;
    for (FileRow &file : s->files) {
        delete file.group;
        for (OutRow &out : file.outs) {
            delete out.row;
            delete out.reason_row;
        }
    }
    s->files.clear();
    s->progress->setRange(0, 100);
    s->progress->setValue(0);
    s->progress->set_segments(0.0, 0.0, 0.0, 0.0, 0.0);
    s->percent->set_full_text(QStringLiteral("0%"));
    s->summary->set_full_text(tr("点击底栏「开始运行」开始批处理"));
    s->task_empty->set_full_text(tr("尚未开始 · 运行中逐输出行在此展开"));
    s->open_output->setEnabled(false);
    s->open_logs->setEnabled(false);
    refresh_widgets(*s);
}

bool PageRun::is_running() const {
    const RunState *s = state_of(this);
    return s != nullptr && s->running;
}

} // namespace pp::ui
