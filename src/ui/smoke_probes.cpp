// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — --ui-smoke 走查探针域（M7-WP9 / R-048·R-116·R-258 迁出产物）。
//
// 契约（docs/deep-review-0.3.2.md §5R R-048/R-116/R-258）：原 mainwindow.cpp:4008-11729 的
// 探针域整体搬入本 TU，**PP_BUILD_DEV 门控**——发布构建（无 -DPP_BUILD_DEV=ON）不含任何
// 探针代码与常量（原「约 7600 行自检代码无条件编进发布二进制」缺口就此收口）。
// objectName / 钩子 / 断言行逐位不变；冻结行 `UI-SMOKE OK shots=12 pages=3` 不变。
// 探针实现为 MainWindow::Impl 成员函数（Impl 定义见 ui/mainwindow_impl.h），行为与
// 迁移前逐字节等价。
#include "ui/mainwindow_impl.h"

#include "ui/contextmenu.h"    // UX-1：卡片右键菜单面（词表单源）
#include "ui/formkit.h"        // R-识图1：CheckableCard/ElidedLabel 回读（layout-audit ①③）
#include "ui/formkit_layout.h" // R-识图1：磁贴/下拉可用宽口径单源（layout-audit ①）
#include "ui/leading_indent.h" // R-218：逻辑边距单源
#include "ui/meta_probe_context.h" // R-182/R-258 + M7-R2.3-G8：钩子/词表单源 + ProbeContext（读侧全面迁移）
#include "ui/meta_state.h" // M7R22G1-3：meta::RatingStars/DeltaSpin/unit_slot/note_label（rtl-mirror 四查）
#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QBoxLayout>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDirIterator>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QTabBar>
#include <QToolButton>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>

#ifdef PP_BUILD_DEV

namespace pp::ui {
namespace {
using theme::preferred_theme_mode;

constexpr int kThumbWaitMs = 10000; // §4.3：缩略图队列空上限
constexpr int kRunWaitMs = 120000;  // U10 冻结：冒烟实跑上限
constexpr int kSmokeShotCount = 12;
constexpr qint64 kMinShotBytes = 10LL * 1024; // LL 起手=64 位乘法（值不变 10240）

// 冒烟截图清单（顺序与名称逐字节固定；M4-T14 起 = 12 张：§4.3 冻结 8 张 + 运行页 4 张）
//   * 03-run / 03b-run-done：**真实运行**下的运行页（运行中锁定态 / 结束态）；
//   * 03c-run-idle：运行页空闲态（三卡空态 + 引导）；
//   * 03d-run-rows：运行页逐输出行探针态（真实/斜纹/完成产物/失败原因 四类同行）；
//   * 03e-run-cancel：运行页取消态（取消唯一入口 → 取消后定稿）；
//   * 07-run-light：浅色主题运行页（明暗双主题覆盖）。
const char *const kSmokeShots[kSmokeShotCount] = {
    "01-meta.png",      "02-output.png",      "02b-output-avif.png", "03-run.png",
    "03b-run-done.png", "03c-run-idle.png",   "03d-run-rows.png",    "03e-run-cancel.png",
    "04-settings.png",  "05-exif-editor.png", "06-presets.png",      "07-run-light.png",
};
constexpr int kShotMeta = 0;
constexpr int kShotOutput = 1;
constexpr int kShotAvif = 2;
constexpr int kShotRun = 3;
constexpr int kShotRunDone = 4;
constexpr int kShotRunIdle = 5;
constexpr int kShotRunRows = 6;
constexpr int kShotRunCancel = 7;
constexpr int kShotSettings = 8;
constexpr int kShotExif = 9;
constexpr int kShotPresets = 10;
constexpr int kShotRunLight = 11;

// ---------------------------------------------------------------------------
// §M2-T16：走查 EXIF 编辑器 fixture 的确定性选取（消除对目录枚举顺序的依赖）
// ---------------------------------------------------------------------------
// 走查原先用 model->row(0)（“首个输入文件”）构造 EXIF 编辑器，而输入列表来自
// FileListModel::add_paths → std::filesystem::recursive_directory_iterator（readdir 顺序
// 未定义）：仓库工作树首个是 gray16.png（含 IFD0）→ 通过；`git archive` 导出的干净树
// 首个是 targa.tga（无 EXIF）→ “IFD0 下找不到叶子标签”必红（CI 每次全新 checkout）。
// 现口径：entries() 的路径**先按字典序排序**（排序结果与 readdir 顺序无关；同名候选也因此
// 定序），再按下面的**固定候选优先级**取第一个存在者；候选全不存在 → UI-SMOKE FAIL，不静默跳过。
// 候选 = 实测含 IFD0 叶子（Exif.Image.*）的 tests/golden/base 语料（exiv2 探针逐文件实测，
// 见 M2-T16 报告；每个候选取首个 IFD0 叶子的值均非空）：
//   photo.jpg  Exif.Image.Software（1 个 IFD0 叶子，规范 JPEG EXIF 载体）
//   gray16.tif / multi.tif / rgb8.tif / rgb16.tif  Exif.Image.ImageWidth=64（各 15 个 IFD0 叶子）
//   rgb8.png / gray16.png / gray8.png / graya8.png / rgba8.png / rgb16.png / rgba16.png
//              Exif.Image.Software（各 1 个 IFD0 叶子）
//   jxl8.jxl   Exif.Image.Software（1 个 IFD0 叶子）
// 实测**无 EXIF**（0 个 Exif.Image.*）因而不作候选：anim.gif / bmp24.bmp / targa.tga。
const char *const kSmokeExifFixtures[] = {
    "photo.jpg", "gray16.tif", "multi.tif", "rgb8.tif",  "rgb16.tif",  "rgb8.png", "gray16.png",
    "gray8.png", "graya8.png", "rgba8.png", "rgb16.png", "rgba16.png", "jxl8.jxl",
};

// 候选清单文本（仅用于失败信息，便于定位语料不合规）
QString smoke_exif_fixture_names() {
    QStringList names;
    for (const char *f : kSmokeExifFixtures)
        names << QString::fromLatin1(f);
    return names.join(QLatin1String(" / "));
}

// 选取 EXIF fixture：entries 路径字典序排序 → 按 kSmokeExifFixtures 固定优先级取第一个存在者。
// 返回 nullopt = 一个候选都没有（调用方必须 UI-SMOKE FAIL）。
std::optional<pp::FileEntry> pick_smoke_exif_fixture(const std::vector<pp::FileEntry> &entries) {
    std::vector<pp::FileEntry> sorted = entries; // 值语义拷贝（src/base_dir/exception）
    std::sort(sorted.begin(), sorted.end(), [](const pp::FileEntry &a, const pp::FileEntry &b) {
        return a.src.string() < b.src.string(); // 字典序：与 readdir 顺序无关
    });
    for (const char *candidate : kSmokeExifFixtures) {
        const std::filesystem::path name(candidate);
        for (const pp::FileEntry &e : sorted) {
            if (e.src.filename() != name)
                continue;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(e.src, ec) || ec)
                continue; // 枚举后被移走
            return e;
        }
    }
    return std::nullopt;
}

// 诊断用路径标签：优先仓库根相对路径（CI 日志可核对），树外语料（如 /tmp 隔离树）退回文件名。
QString smoke_fixture_label(const std::filesystem::path &p, const QString &repo_root_path) {
    const QString abs = QDir::cleanPath(QString::fromStdString(p.string()));
    if (!repo_root_path.isEmpty()) {
        const QString rel = QDir(repo_root_path).relativeFilePath(abs);
        if (!rel.startsWith(QLatin1String("..")) && !QDir::isAbsolutePath(rel))
            return rel;
    }
    return QString::fromStdString(p.filename().string());
}

// 断言 e 的最小字节数（语义 = "offscreen 下非空渲染"）：1440×900 整窗截图 >10KB；
// 对话框截图 >2KB——空列表的预设对话框大面积空白，PNG 压缩后仅 ~7.6KB（实测），
// 仍是有内容的真实渲染，故按控件面积分档（数值随每次冒烟 stdout 打印备查）。
// M5-T1 机械阈值调整（C7 登记，语义不变）：运行页三张探针图（03c/03d/03e）自 v6 令牌起
// 下限由 10KB 下调到 4KB——(a) 暗色窗口底自 §4.1 起是**纯色 #22252b**（v4 三停渐变的抖动
// 噪声使 PNG 虚高 4–5×：346 种残色 → 94 种）、(b) 进度条高 6（R4，v4 8）。二者皆令牌取值
// 变化，产物仍是真实渲染（空白图 <2KB，照旧被拦下；实测 5679 / 10214 / 7186 字节）。
qint64 min_shot_bytes(int index) {
    if (index == kShotRunIdle || index == kShotRunRows || index == kShotRunCancel)
        return 4LL * 1024;
    return index >= kShotSettings ? 2LL * 1024 : kMinShotBytes;
}
} // namespace

// ---------------------------------------------------------------------------
// --ui-smoke 脚本化走查（§4.3 冻结断言）
// ---------------------------------------------------------------------------
void MainWindow::ui_smoke_walk(const QString &shots_dir) {
    // 探针输出改**非缓冲**：Windows GUI 子系统下 stdout 是全缓冲，走查途中崩溃（如 0xC0000005）
    // 会把已打印的探针行全部丢掉，只剩一个 exit code（实测：ui_smoke 崩溃时 smoke.log 0 字节，
    // 无法定位卡在哪条探针）。本行不改变任何输出内容，只保证"跑到哪儿看得见"。
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Impl &d = *impl_;
    setProperty("pp_ui_smoke_ran", true);
    setProperty("pp_ui_smoke_exit", 1);
    d.smoke_failures.clear();
    d.smoke_shots = 0;
    try {
        d.smoke_run(shots_dir);
    } catch (const std::exception &e) {
        d.smoke_fail(tr("未捕获异常：%1").arg(QString::fromUtf8(e.what())));
    } catch (...) {
        d.smoke_fail(tr("未捕获异常"));
    }
    if (d.smoke_failures.isEmpty()) {
        std::printf("UI-SMOKE OK shots=%d pages=3\n", d.smoke_shots);
        std::fflush(stdout);
        setProperty("pp_ui_smoke_exit", 0);
    } else {
        for (const QString &failure : d.smoke_failures) {
            std::fprintf(stderr, "UI-SMOKE FAIL %s\n", qUtf8Printable(failure));
        }
        std::fprintf(stderr, "UI-SMOKE FAILED %d\n", static_cast<int>(d.smoke_failures.size()));
        std::fflush(stderr);
    }
}

void MainWindow::Impl::pump(int ms) {
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    } while (timer.elapsed() < ms);
}

bool MainWindow::Impl::wait_for(const std::function<bool()> &pred, int timeout_ms) {
    QElapsedTimer timer;
    timer.start();
    while (!pred()) {
        if (timer.elapsed() >= timeout_ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return true;
}

void MainWindow::Impl::wait_thumbs(int timeout_ms) {
    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(thumbs, &Thumbnailer::queue_empty, &loop, &QEventLoop::quit);
    guard.start(timeout_ms);
    if (thumbs->pending() > 0)
        loop.exec();
    pump(250); // 让已排队的 ready 结果落到模型
}

// ---------------------------------------------------------------------------
// GUI-R2 回归探针（docs/gui-review-0.3.0-r2.md）——只增，四条自检行：
//   * combo-consistency：全界面 QComboBox 的样式钩子 / 高 24 / **实拍非空**（钉问题 1+5）
//   * format-card-align：页签带 ↔「＋ 添加格式…」corner widget 几何同带（钉问题 2）
//   * dialog-close：设置/元数据编辑器/预设 弹窗的 caption ✕ 必须真关窗（钉问题 3）
//   * run-guard：「开始运行」可点性与闸门一致 + 点击后不弹模态（钉问题 4）
// 判据一律硬判（smoke_fail）；行内 checked/bad 与取证值由本函数给出。
// ---------------------------------------------------------------------------
void MainWindow::Impl::smoke_probe_gui_r2() {
    // ---- (1) 下拉一致性 ----
    // 期望：每个 QComboBox 都挂**唯一**样式钩子（ppFk="combo"，theme::kFkCombo —— R-058 修：
    // 此前头注释写"ppField/ppMetaField 同样接受"与实现相反，实现只认 ppFk；三条钩子轨已按
    // UX-6⑤ 收敛到 ppFk），高度与 Metrics::combo_h 同档（±2 容
    // border-box），且**明暗两档实拍都有内容** （亮色下 ParamForm 的下拉曾整只不绘制 —— 非背景像素
    // 0/19604，见报告 §1.1 表）。
    {
        int checked = 0, hook_miss = 0, height_bad = 0, invisible = 0;
        QStringList detail, heights;
        const theme::ThemeMode restore_mode = theme_mode;
        // 全量扫描：**不按 isVisible 过滤** —— 病灶多藏在非当前页；QWidget::grab() 对隐藏
        // 控件同样能渲染，取证不受影响。structural=true 才查钩子/高度（与主题无关，只查一次）。
        const auto scan = [&](QWidget *root, const char *scope, bool structural) {
            if (root == nullptr)
                return;
            const QList<QComboBox *> combos = root->findChildren<QComboBox *>();
            for (QComboBox *c : combos) {
                if (c == nullptr)
                    continue;
                if (structural) {
                    ++checked;
                    heights << QString::number(c->height());
                    // 唯一样式钩子 = ppFk="combo"（theme::kFkCombo）。旧的 ppField="combo" /
                    // ppMetaField="combo" 两条迁移期路径随 GUI-R2 退役，这里不再接受。
                    const bool hooked = c->property(theme::kFkProperty).toString() ==
                                        QLatin1String(theme::kFkCombo);
                    if (!hooked) {
                        ++hook_miss;
                        detail << QStringLiteral("%1:%2/no-hook")
                                      .arg(QString::fromLatin1(scope), c->objectName());
                    }
                    if (std::abs(c->height() - theme::Metrics::combo_h) > 2) {
                        ++height_bad;
                        detail << QStringLiteral("%1:%2/h=%3")
                                      .arg(QString::fromLatin1(scope), c->objectName())
                                      .arg(c->height());
                    }
                }
                // 实拍可见性：grab 自身，统计与域众数色偏离 > 24 的像素；0 = 整只没画
                const QImage img = c->grab().toImage();
                int nonbg = 0;
                if (!img.isNull()) {
                    QHash<QRgb, int> hist;
                    for (int y = 0; y < img.height(); ++y)
                        for (int x = 0; x < img.width(); ++x)
                            ++hist[img.pixel(x, y)];
                    QRgb modal = 0;
                    int best = -1;
                    for (auto it = hist.constBegin(); it != hist.constEnd(); ++it)
                        if (it.value() > best) {
                            best = it.value();
                            modal = it.key();
                        }
                    const int mr = qRed(modal), mg = qGreen(modal), mb = qBlue(modal);
                    for (int y = 0; y < img.height(); ++y)
                        for (int x = 0; x < img.width(); ++x) {
                            const QRgb p = img.pixel(x, y);
                            if (std::abs(qRed(p) - mr) + std::abs(qGreen(p) - mg) +
                                    std::abs(qBlue(p) - mb) >
                                24)
                                ++nonbg;
                        }
                }
                if (nonbg <= 0) {
                    ++invisible;
                    detail << QStringLiteral("%1:%2/invisible")
                                  .arg(QString::fromLatin1(scope), c->objectName());
                }
            }
        };
        // 明暗各扫一遍（结构性判据只在暗档取，避免重复计数）
        for (int pass = 0; pass < 2; ++pass) {
            set_theme_mode(pass == 0 ? theme::ThemeMode::Dark : theme::ThemeMode::Light);
            pump(120);
            const bool structural = (pass == 0);
            scan(w, pass == 0 ? "win/dark" : "win/light", structural);
            { // 弹窗侧（dialogkit 口径）—— 同一功能控件的另一条样式路径
                SettingsDialog sd(settings, w);
                sd.show();
                pump(120);
                scan(&sd, pass == 0 ? "settings/dark" : "settings/light", structural);
                sd.close();
                const std::vector<std::pair<QString, QString>> empty_list;
                PresetsDialog pd(empty_list, QStringLiteral("sample"), w);
                pd.show();
                pump(120);
                scan(&pd, pass == 0 ? "presets/dark" : "presets/light", structural);
                pd.close();
                if (model->size() > 0) {
                    ExifEditor ed(QString::fromStdString(model->row(0).entry.src.string()),
                                  page_meta->rules(), model->row(0).entry.exception, w);
                    ed.setProperty("pp_exif_editor_suppress_modal", true);
                    ed.show();
                    pump(120);
                    scan(&ed, pass == 0 ? "exif/dark" : "exif/light", structural);
                    ed.close();
                }
            }
        }
        set_theme_mode(restore_mode); // 结束归位
        pump(80);
        heights.removeDuplicates();
        std::printf("UI-SMOKE combo-consistency: checked=%d no-hook=%d h-bad=%d invisible=%d "
                    "bad=%d heights=[%s] %s\n",
                    checked, hook_miss, height_bad, invisible, hook_miss + height_bad + invisible,
                    qUtf8Printable(heights.join(QLatin1Char(' '))),
                    qUtf8Printable(detail.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (hook_miss + height_bad + invisible > 0)
            smoke_fail(
                MainWindow::tr("下拉一致性（问题 1/5）：%1").arg(detail.join(QLatin1Char(' '))));
    }

    // ---- (2) 格式卡对齐 ----
    // 期望：「＋ 添加格式…」**垂直居中于页签带**且含于其中（实测 0.3.0：页签带 y=664–704、
    // 添加格式块 y=678–718 → 垂直错位 14px + 水平缝隙，双主题同现）。按钮高 26 是 A7 规格，
    // 判据据此取**中心线对齐 + 含于带内**，不要求与页签带同高（真机页签带高随字族 29）。
    {
        auto *tabs = w->findChild<QTabWidget *>(QStringLiteral("pp-format-tabs"));
        auto *add = w->findChild<QToolButton *>(QStringLiteral("pp-add-format"));
        int checked = 0, bad = 0;
        QStringList detail;
        if (tabs == nullptr || add == nullptr || tabs->tabBar() == nullptr) {
            ++checked;
            ++bad;
            detail << QStringLiteral("missing-tabs-or-add");
        } else {
            QTabBar *bar = tabs->tabBar();
            const QPoint bar_org = bar->mapTo(tabs, QPoint(0, 0));
            const QPoint add_org = add->mapTo(tabs, QPoint(0, 0));
            const int bar_mid = bar_org.y() + bar->height() / 2;
            const int add_mid = add_org.y() + add->height() / 2;
            const int dmid = add_mid - bar_mid;
            const bool inside = add_org.y() >= bar_org.y() &&
                                add_org.y() + add->height() <= bar_org.y() + bar->height() + 1;
            ++checked;
            detail << QStringLiteral("tab=%1+%2,%3x%4 add=%5+%6,%7x%8 dmid=%9 inside=%10")
                          .arg(bar_org.x())
                          .arg(bar_org.y())
                          .arg(bar->width())
                          .arg(bar->height())
                          .arg(add_org.x())
                          .arg(add_org.y())
                          .arg(add->width())
                          .arg(add->height())
                          .arg(dmid)
                          .arg(inside ? 1 : 0);
            if (std::abs(dmid) > 2 || !inside)
                ++bad;
        }
        std::printf("UI-SMOKE format-card-align: checked=%d bad=%d %s\n", checked, bad,
                    qUtf8Printable(detail.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (bad > 0)
            smoke_fail(MainWindow::tr("格式参数页签带与「添加格式」错位（问题 2）：%1")
                           .arg(detail.join(QLatin1Char(' '))));
    }

    // ---- (3) 弹窗 caption 关闭钮 ----
    // 期望：点 pp-dlg-close 必须关掉对话框。0.3.0 三个弹窗都只创建按钮、从未 connect，
    // 点了没反应（问题 3）。逐个构造 → show → click ✕ → 断言不可见。
    {
        int checked = 0, bad = 0;
        QStringList detail;
        const auto probe_close = [&](QDialog &dlg, const char *name) {
            ++checked;
            dlg.show();
            pump(200);
            auto *close_btn = dlg.findChild<QPushButton *>(QStringLiteral("pp-dlg-close"));
            if (close_btn == nullptr) {
                ++bad;
                detail << QStringLiteral("%1/no-close-btn").arg(QString::fromLatin1(name));
                dlg.close();
                pump(60);
                return;
            }
            close_btn->click();
            pump(200);
            const bool closed = !dlg.isVisible();
            if (!closed) {
                ++bad;
                detail << QStringLiteral("%1/close-noop").arg(QString::fromLatin1(name));
                dlg.close();
                pump(60);
            }
        };
        { // 设置
            SettingsDialog dlg(settings, w);
            probe_close(dlg, "settings");
        }
        { // 元数据编辑器（U8：模态短路属性保持）
            std::optional<pp::MetadataOverride> exc;
            std::string src;
            if (model->size() > 0) {
                const FileRow &fr = model->row(0);
                src = fr.entry.src.string();
                exc = fr.entry.exception;
            }
            if (!src.empty()) {
                ExifEditor dlg(QString::fromStdString(src), page_meta->rules(), exc, w);
                dlg.setProperty("pp_exif_editor_suppress_modal", true);
                probe_close(dlg, "exif-editor");
            } else {
                ++checked;
                detail << QStringLiteral("exif-editor/no-fixture");
            }
        }
        { // 预设管理
            const std::vector<std::pair<QString, QString>> empty_list;
            PresetsDialog dlg(empty_list, QStringLiteral("sample"), w);
            probe_close(dlg, "presets");
        }
        std::printf("UI-SMOKE dialog-close: checked=%d bad=%d %s\n", checked, bad,
                    qUtf8Printable(detail.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (bad > 0)
            smoke_fail(MainWindow::tr("弹窗 caption 关闭钮无效（问题 3）：%1")
                           .arg(detail.join(QLatin1Char(' '))));
    }

    // ---- (4) 运行重入守卫 ----
    // 期望：可点性与 on_start 的闸门同源；点击后进入运行态、**全程不弹模态框**；运行期按钮
    // 必须禁用（重入会弹「正在运行」= 用户看到的"概率性错误"）。模态看门狗一出现就记录并
    // 关闭，避免走查被 QMessageBox 卡死。
    {
        int checked = 0, bad = 0;
        QStringList detail, modals;
        QPushButton *start = w->findChild<QPushButton *>(QStringLiteral("pp-start"));
        QTimer watchdog;
        watchdog.setInterval(30);
        QObject::connect(&watchdog, &QTimer::timeout, w, [&modals] {
            if (QWidget *m = QApplication::activeModalWidget()) {
                modals << QStringLiteral("%1/%2").arg(
                    QString::fromLatin1(m->metaObject()->className()), m->objectName());
                m->close();
            }
        });
        watchdog.start();
        const QString reason = page_output != nullptr ? page_output->ready_to_start() : QString();
        if (start == nullptr) {
            ++checked;
            ++bad;
            detail << QStringLiteral("no-pp-start");
        } else if (!reason.isEmpty()) {
            // 闸门未过：按钮必须禁用，且点了不该弹模态（禁用钮点不动 = 无模态）
            ++checked;
            if (start->isEnabled()) {
                ++bad;
                detail << QStringLiteral("enabled-while-blocked(%1)").arg(reason);
            }
            start->click();
            pump(150);
            if (!modals.isEmpty()) {
                ++bad;
                detail << QStringLiteral("modal-on-disabled-click=[%1]")
                              .arg(modals.join(QLatin1Char(',')));
            }
            detail << QStringLiteral("blocked-by=\"%1\"").arg(reason);
        } else {
            // 闸门已过：点击应真跑起来且无模态；随后取消复位。两轮 = 抓"概率性"重入。
            for (int round = 0; round < 2 && bad == 0; ++round) {
                ++checked;
                modals.clear();
                start->click();
                pump(120);
                if (!modals.isEmpty()) {
                    ++bad;
                    detail << QStringLiteral("r%d/modal-on-start=[%2]")
                                  .arg(round)
                                  .arg(modals.join(QLatin1Char(',')));
                }
                if (!running) {
                    ++bad;
                    detail << QStringLiteral("r%d/not-running").arg(round);
                } else if (start->isEnabled()) {
                    ++bad;
                    detail << QStringLiteral("r%d/start-enabled-in-run").arg(round);
                }
                if (running)
                    w->on_cancel();
                wait_for([this] { return !running; }, kRunWaitMs);
                pump(120);
            }
            detail << QStringLiteral("rounds=2");
        }
        watchdog.stop();
        std::printf("UI-SMOKE run-guard: checked=%d bad=%d modals=%d %s\n", checked, bad,
                    modals.size(), qUtf8Printable(detail.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (bad > 0)
            smoke_fail(
                MainWindow::tr("运行重入守卫（问题 4）：%1").arg(detail.join(QLatin1Char(' '))));
    }

    // ---- (5) 渲染面取证（GUI-R2 判定口径修正）----
    // 上面 (1)(2) 查的是**控件几何**，挡不住"几何对了、画出来还是平台的复古件"——
    // 数值框的 ▲▼ 分格按钮、页签行的两条灰带都是这么漏过去的。本节一律以**渲染像素**判：
    //   * 箭头列内不得有贯穿的对比线（竖分隔 / 横分隔）—— 平台"独立描边按钮格"的签名；
    //   * 箭头列内必须有图形（矢量箭头）；
    //   * 页签行不得有平坦色带（设计稿这一行是白底 + 底部发丝线，无色带）。
    // 判据与主题无关：对比度取**相对域众数色**的偏差，不写死色值。
    {
        int checked = 0, bad = 0;
        QStringList detail;
        const auto luma = [](const QColor &c) {
            return 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
        };
        // 找出 img 里的域众数色（= 控件底），返回它
        const auto modal_of = [](const QImage &img) {
            QHash<QRgb, int> hist;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    ++hist[img.pixel(x, y)];
            QRgb best = 0;
            int n = -1;
            for (auto it = hist.constBegin(); it != hist.constEnd(); ++it)
                if (it.value() > n) {
                    n = it.value();
                    best = it.key();
                }
            return QColor(best);
        };
        // 箭头列检查：strip = 控件右端 col 宽的竖带
        const auto check_strip = [&](QWidget *wgt, int col, const char *name) {
            if (wgt == nullptr)
                return;
            ++checked;
            const QImage img = wgt->grab().toImage();
            if (img.isNull() || img.width() <= col) {
                ++bad;
                detail << QStringLiteral("%1/no-image").arg(QString::fromLatin1(name));
                return;
            }
            const QColor bg = modal_of(img);
            const double bg_l = luma(bg);
            auto contrast = [&](const QColor &c) { return std::abs(luma(c) - bg_l) > 40.0; };
            const int x0 = img.width() - col;
            // ① 竖分隔：任一列的对比像素占比 ≥ 50% 高
            int vline = -1;
            for (int x = x0; x < img.width(); ++x) {
                int n = 0;
                for (int y = 0; y < img.height(); ++y)
                    if (contrast(QColor(img.pixel(x, y))))
                        ++n;
                if (n * 2 >= img.height()) {
                    vline = x - x0;
                    break;
                }
            }
            // ② 横分隔：任一行的对比像素占比 ≥ 50% 带宽
            int hline = -1;
            for (int y = 0; y < img.height(); ++y) {
                int n = 0;
                for (int x = x0; x < img.width(); ++x)
                    if (contrast(QColor(img.pixel(x, y))))
                        ++n;
                if (n * 2 >= col) {
                    hline = y;
                    break;
                }
            }
            // ③ 箭头本体：带内对比像素总数须 > 0
            int glyph = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = x0; x < img.width(); ++x)
                    if (contrast(QColor(img.pixel(x, y))))
                        ++glyph;
            if (vline >= 0 || hline >= 0 || glyph == 0) {
                ++bad;
                detail << QStringLiteral("%1/vline=%2 hline=%3 glyph=%4")
                              .arg(QString::fromLatin1(name))
                              .arg(vline)
                              .arg(hline)
                              .arg(glyph);
            }
        };
        const QList<QComboBox *> combos = w->findChildren<QComboBox *>();
        for (QComboBox *c : combos)
            check_strip(c, theme::Metrics::combo_arrow_px * 2 + 8, "combo");
        const QList<QAbstractSpinBox *> spins = w->findChildren<QAbstractSpinBox *>();
        for (QAbstractSpinBox *s : spins)
            check_strip(s, theme::Metrics::spin_arrow_w, "spin");
        // 页签行色带：设计稿这一行是**卡底 + 底部发丝线**，不许出现 --ctl 平坦色块
        // （0.3.0 的两条 29px 灰带正是 QTabBar/corner 吃了 palette 的 --ctl 底）。
        // 取证域 = **页签条本身那一行**（不含页签内容区 —— 那里的下拉/spin 本来就是 --ctl 底，
        // 算进来必误报）。只扫 tab bar 高度内、corner widget 之外的区域。
        if (auto *tabs = w->findChild<QTabWidget *>(QStringLiteral("pp-format-tabs"))) {
            QTabBar *bar = tabs->tabBar();
            if (bar != nullptr && tabs->height() > 0) {
                ++checked;
                const QImage img = tabs->grab().toImage();
                if (!img.isNull()) {
                    const int rows = std::min(img.height(), img.height() * (bar->height() + 2) /
                                                                std::max(1, tabs->height()));
                    const int x_end = img.width() * bar->width() / std::max(1, tabs->width());
                    const QColor ctl = tokens.control;
                    int hit = 0;
                    for (int y = 0; y < rows; ++y)
                        for (int x = 0; x < x_end; ++x) {
                            const QColor p(img.pixel(x, y));
                            if (std::abs(p.red() - ctl.red()) + std::abs(p.green() - ctl.green()) +
                                    std::abs(p.blue() - ctl.blue()) <=
                                12)
                                ++hit;
                        }
                    const int total = rows * x_end;
                    if (total > 0 && hit * 100 > total * 3) {
                        ++bad;
                        detail << QStringLiteral("tabstrip/ctl-fill=%1/%2").arg(hit).arg(total);
                    }
                }
            }
        }
        // 树展开符：矢量 chevron 的笔画是**斜线**（横/竖连续段都很短）；平台的方框/加号
        // 展开符有 ≥8px 的横竖贯通描边。据此区分，不写死形状。
        if (auto *view = w->findChild<QWidget *>(QStringLiteral("pp-file-view"))) {
            ++checked;
            const QImage img = view->grab().toImage();
            if (img.isNull()) {
                ++bad;
                detail << QStringLiteral("branch/no-image");
            } else {
                // 取分组节头那一行左端 24px（分组节头才带展开符）
                const int side = 24 * img.width() / std::max(1, view->width());
                const QColor bg = modal_of(img);
                const double bg_l = luma(bg);
                auto contrast = [&](int x, int y) {
                    return std::abs(luma(QColor(img.pixel(x, y))) - bg_l) > 40.0;
                };
                int best_glyph = 0, best_h = 0, best_v = 0;
                for (int y0 = 0; y0 + side <= img.height(); y0 += side) {
                    int glyph = 0, hrun = 0, vrun = 0;
                    for (int y = y0; y < y0 + side; ++y) {
                        int run = 0;
                        for (int x = 0; x < side; ++x) {
                            if (contrast(x, y)) {
                                ++glyph;
                                ++run;
                            } else {
                                hrun = std::max(hrun, run);
                                run = 0;
                            }
                        }
                        hrun = std::max(hrun, run);
                    }
                    for (int x = 0; x < side; ++x) {
                        int run = 0;
                        for (int y = y0; y < y0 + side; ++y) {
                            if (contrast(x, y))
                                ++run;
                            else {
                                vrun = std::max(vrun, run);
                                run = 0;
                            }
                        }
                        vrun = std::max(vrun, run);
                    }
                    if (glyph > best_glyph) {
                        best_glyph = glyph;
                        best_h = hrun;
                        best_v = vrun;
                    }
                }
                if (best_glyph == 0 || best_h > side * 2 / 3 || best_v > side * 2 / 3) {
                    ++bad;
                    detail << QStringLiteral("branch/glyph=%1 hrun=%2 vrun=%3")
                                  .arg(best_glyph)
                                  .arg(best_h)
                                  .arg(best_v);
                }
            }
        }
        // 令牌 QSS 在场性（结构位）：四类平台默认皮的规则不得被回退掉
        {
            ++checked;
            const QString sheet = w->styleSheet();
            for (const char *sel :
                 {"QMenu", "QMessageBox", "QToolTip", "QGroupBox", "QToolButton::menu-indicator"}) {
                if (!sheet.contains(QLatin1String(sel))) {
                    ++bad;
                    detail << QStringLiteral("qss-missing/%1").arg(QString::fromLatin1(sel));
                }
            }
        }
        // 勾选语义只属勾选列（GUI-R2 修「两排选择框」）：内容列若仍带 UserCheckable，
        // initStyleOption 会置 HasCheckIndicator → 样式在内容列左缘再画一枚原生勾选框。
        {
            ++checked;
            const QAbstractItemModel *m = model;
            if (m == nullptr) {
                ++bad;
                detail << QStringLiteral("check-col/no-model");
            } else {
                int leaked = 0;
                const int rows = std::min<int>(m->rowCount(), 24);
                for (int r = 0; r < rows; ++r) {
                    const QModelIndex content = m->index(r, 1);
                    if (content.isValid() && (m->flags(content) & Qt::ItemIsUserCheckable) != 0)
                        ++leaked;
                }
                if (leaked > 0) {
                    ++bad;
                    detail << QStringLiteral("check-col/user-checkable-on-content=%1").arg(leaked);
                }
            }
        }
        std::printf("UI-SMOKE render-tokens: checked=%d bad=%d %s\n", checked, bad,
                    qUtf8Printable(detail.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (bad > 0)
            smoke_fail(MainWindow::tr("渲染面仍有平台代画痕迹（复古分格/色带）：%1")
                           .arg(detail.join(QLatin1Char(' '))));
    }
}

void MainWindow::Impl::smoke_fail(const QString &reason) {
    smoke_failures << reason;
    std::fprintf(stderr, "ui-smoke: %s\n", qUtf8Printable(reason));
    std::fflush(stderr);
}

QString MainWindow::Impl::repo_root() {
    static const QString cached = [] {
        QDir dir(QCoreApplication::applicationDirPath());
        for (int i = 0; i < 8; ++i) {
            if (dir.exists(QStringLiteral("CMakeLists.txt")) ||
                dir.exists(QStringLiteral(".git"))) {
                return dir.absolutePath();
            }
            if (!dir.cdUp())
                break;
        }
        return QString();
    }();
    return cached;
}

void MainWindow::Impl::smoke_grab(const QString &dir, const char *name, QWidget *target) {
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QString::fromLatin1(name));
    QWidget *source = target != nullptr ? target : static_cast<QWidget *>(w);
    const QPixmap shot = source->grab();
    if (shot.isNull() || !shot.save(path, "PNG")) {
        smoke_fail(MainWindow::tr("截图保存失败：%1").arg(path));
        return;
    }
    ++smoke_shots;
}

void MainWindow::Impl::smoke_amap_boundary(pp::map::MapWidget *map) {
    const QString prev_provider = QString::fromStdString(settings.map_provider);
    const QString prev_key = QString::fromStdString(settings.amap_key);

    map->set_amap_key(QStringLiteral("ui-smoke-key")); // key 任填；offline 下不发请求
    map->set_provider(QStringLiteral("amap"));
    map->center_on(39.9042, 116.4074, 10);
    pump(150);

    double lat = 0.0;
    double lon = 0.0;
    int hits = 0;
    const QMetaObject::Connection conn = QObject::connect(
        map, &pp::map::MapWidget::point_selected, w, [&lat, &lon, &hits](double la, double lo) {
            lat = la;
            lon = lo;
            ++hits;
        });

    const QPointF local(map->width() / 2.0, map->height() / 2.0);
    const QPointF global(map->mapToGlobal(local.toPoint()));
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(map, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(map, &release);
    QObject::disconnect(conn);

    if (hits != 1) {
        smoke_fail(
            MainWindow::tr("amap 边界：视口中心点击未产生 point_selected（hits=%1）").arg(hits));
    } else {
        const double dlat = lat - 39.9042;
        const double dlon = lon - 116.4074;
        if (std::hypot(dlat, dlon) >= 0.001) {
            smoke_fail(MainWindow::tr("amap 边界：选中点偏差 %1°（%2, %3）")
                           .arg(std::hypot(dlat, dlon), 0, 'f', 6)
                           .arg(lat, 0, 'f', 6)
                           .arg(lon, 0, 'f', 6));
        }
    }

    page_meta->set_map_provider(prev_provider, prev_key, settings.tile_cache_mb);
    map->clear_marker();
    pump(80);
}

// ---------------------------------------------------------------------------
// M4-T9 骨架自检：窗口行为矩阵的可自动化部分（手测项见任务书 W5 走查清单：
// Snap 布局悬停 / 双击最大化 / Aero 摸边 / 多显示器 DPI）
// ---------------------------------------------------------------------------

void MainWindow::Impl::smoke_probe_skeleton() {
    // ---- 无边框窗口标志 + 顶栏/底栏高度 + 最小窗口（§9.1 尺寸基准）----
    const bool frameless_flag = (w->windowFlags() & Qt::FramelessWindowHint) != 0;
    std::printf("UI-SMOKE skeleton: frameless=%d titlebar=%d bottom=%d min=%dx%d size=%dx%d\n",
                frameless_flag ? 1 : 0, titlebar != nullptr ? titlebar->height() : -1,
                bottombar != nullptr ? bottombar->height() : -1, w->minimumWidth(),
                w->minimumHeight(), w->width(), w->height());
    std::fflush(stdout);
    if (!frameless_flag)
        smoke_fail(MainWindow::tr("无边框窗口：未置 Qt::FramelessWindowHint"));
    if (titlebar == nullptr || titlebar->height() != theme::Metrics::toolbar_height)
        smoke_fail(MainWindow::tr("顶栏高度不是 %1px").arg(theme::Metrics::toolbar_height));
    if (bottombar == nullptr || bottombar->height() != theme::Metrics::bottom_height)
        smoke_fail(MainWindow::tr("底栏高度不是 %1px").arg(theme::Metrics::bottom_height));
    if (w->minimumWidth() != theme::Metrics::min_window_w ||
        w->minimumHeight() != theme::Metrics::min_window_h) {
        smoke_fail(MainWindow::tr("最小窗口不是 %1×%2")
                       .arg(theme::Metrics::min_window_w)
                       .arg(theme::Metrics::min_window_h));
    }
    if (splitter == nullptr) {
        smoke_fail(MainWindow::tr("三栏 splitter 缺失"));
        return;
    }

    // ---- 三栏尺寸：1440×900 校准（§3.2 栏宽固定轴 330 / 487 / 587）----
    // M5-T4 机械换锚：基线由 M4 的 258/537/601（J 口径）换到 v6 §3.2 的 330/587 + 中栏弹性
    // 落点 487（1440 − 2×10 − 330 − 587 − 2×8）；语义不变（三栏基线可复现）。
    const auto check_columns = [this](const char *tag, int left, int mid, int right, int tol) {
        std::printf("UI-SMOKE columns-%s: %d/%d/%d (expect %d/%d/%d ±%d)\n", tag, left, mid, right,
                    theme::Metrics::left_width, theme::Metrics::mid_width_1440,
                    theme::Metrics::right_width, tol);
        std::fflush(stdout);
        const bool ok = std::abs(left - theme::Metrics::left_width) <= tol &&
                        std::abs(mid - theme::Metrics::mid_width_1440) <= tol &&
                        std::abs(right - theme::Metrics::right_width) <= tol;
        if (!ok)
            smoke_fail(MainWindow::tr("三栏尺寸 %1 不符：%2/%3/%4")
                           .arg(QString::fromLatin1(tag))
                           .arg(left)
                           .arg(mid)
                           .arg(right));
    };
    {
        const QList<int> sizes = splitter->sizes();
        if (sizes.size() != 3) {
            smoke_fail(MainWindow::tr("splitter 不是三栏（size=%1）").arg(sizes.size()));
        } else {
            check_columns("1440", sizes.at(0), sizes.at(1), sizes.at(2), 4);
        }
    }

    // ---- 放大（1700）：**左右两条固定轴不动**，全部余量进中栏（§3.2 .col-l/.col-r `flex:0 0`
    //      + §12-16「栏宽固定轴，切页列不错位」）。M5-T4 机械换锚：M4 的中:右=100:112 弹性比
    //      随 v6 固定轴作废（右栏不再是弹性列），改为断言"左右恒 330/587、中栏 = 余量" ——
    //      比原比例断言更强（逐列钉住），语义不变（列宽可复现、内容不撑宽）。
    const auto grown_columns = [](int window_w) {
        return window_w - 2 * theme::Metrics::main_pad_x - 2 * theme::Metrics::gap; // 列区总宽
    };
    w->resize(1700, 1000);
    pump(150);
    {
        const QList<int> sizes = splitter->sizes();
        const int expect_mid =
            grown_columns(1700) - theme::Metrics::left_width - theme::Metrics::right_width;
        std::printf("UI-SMOKE columns-grown: %d/%d/%d (expect %d/%d/%d ±4, 左右固定轴)\n",
                    sizes.value(0), sizes.value(1), sizes.value(2), theme::Metrics::left_width,
                    expect_mid, theme::Metrics::right_width);
        std::fflush(stdout);
        if (sizes.size() == 3) {
            if (std::abs(sizes.at(0) - theme::Metrics::left_width) > 2)
                smoke_fail(MainWindow::tr("放大后左栏不再是固定 %1px（实为 %2）")
                               .arg(theme::Metrics::left_width)
                               .arg(sizes.at(0)));
            if (std::abs(sizes.at(2) - theme::Metrics::right_width) > 2)
                smoke_fail(MainWindow::tr("放大后右栏不再是固定 %1px（实为 %2）")
                               .arg(theme::Metrics::right_width)
                               .arg(sizes.at(2)));
            if (std::abs(sizes.at(1) - expect_mid) > 4)
                smoke_fail(MainWindow::tr("放大后中栏不是余量 %1px（实为 %2）")
                               .arg(expect_mid)
                               .arg(sizes.at(1)));
        }
    }

    // ---- 最小窗口：1180×720（§3.1）----
    w->resize(theme::Metrics::min_window_w, theme::Metrics::min_window_h);
    pump(150);
    {
        const QList<int> sizes = splitter->sizes();
        const int expect_mid = grown_columns(theme::Metrics::min_window_w) -
                               theme::Metrics::left_width - theme::Metrics::right_width;
        std::printf("UI-SMOKE columns-min: %d/%d/%d (expect %d/%d/%d ±4)\n", sizes.value(0),
                    sizes.value(1), sizes.value(2), theme::Metrics::left_width, expect_mid,
                    theme::Metrics::right_width);
        std::fflush(stdout);
        if (sizes.size() == 3) {
            if (std::abs(sizes.at(0) - theme::Metrics::left_width) > 2)
                smoke_fail(MainWindow::tr("最小窗口下左栏不是 %1px（实为 %2）")
                               .arg(theme::Metrics::left_width)
                               .arg(sizes.at(0)));
            if (std::abs(sizes.at(2) - theme::Metrics::right_width) > 2)
                smoke_fail(MainWindow::tr("最小窗口下右栏不是 %1px（实为 %2）")
                               .arg(theme::Metrics::right_width)
                               .arg(sizes.at(2)));
            if (std::abs(sizes.at(1) - expect_mid) > 4)
                smoke_fail(MainWindow::tr("最小窗口下中栏不是余量 %1px（实为 %2）")
                               .arg(expect_mid)
                               .arg(sizes.at(1)));
        }
    }

    // ---- 回到校准尺寸（后续 8 张冻结截图的基准）----
    w->resize(theme::Metrics::calibrated_w, theme::Metrics::calibrated_h);
    pump(200);
    {
        const QList<int> sizes = splitter->sizes();
        if (sizes.size() == 3)
            check_columns("recalibrated", sizes.at(0), sizes.at(1), sizes.at(2), 4);
    }

    // ---- 顶栏部件几何（mockup .steps/.step/.capbtns 逐条）----
    {
        const int cap_h = cap_close != nullptr ? cap_close->height() : -1;
        const int cap_w = cap_close != nullptr ? cap_close->width() : -1;
        std::printf("UI-SMOKE caption-geometry: w=%d h=%d (单轨 §3.3 46×32；命中带 = 可见面)\n",
                    cap_w, cap_h);
        std::fflush(stdout);
        // M5-T4 机械换锚（裁定 A1 单轨）：可见面 = 命中面 = 46×32（v4 的"46×顶栏满高"作废）。
        if (cap_w != theme::Metrics::caption_btn_w || cap_h != theme::Metrics::caption_btn_h ||
            cap_h != theme::Metrics::caption_btn_hit_h) {
            smoke_fail(MainWindow::tr("caption 三钮几何 %1×%2（期望 %3×%4 单轨）")
                           .arg(cap_w)
                           .arg(cap_h)
                           .arg(theme::Metrics::caption_btn_w)
                           .arg(theme::Metrics::caption_btn_h));
        }
        // 导航区（§6.2 一体式分段 92×30）：M5-T4 机械换锚 —— v4 的三个 .step 独立钮 +
        // 6px 间距 + 18px 编号徽标 → v6 单控件三段（段宽 92 等宽、无段间距、数字圆 15 内嵌）。
        if (nav == nullptr || nav->segment_count() != 3) {
            smoke_fail(MainWindow::tr("一体式分段导航缺失（段数 %1）")
                           .arg(nav != nullptr ? nav->segment_count() : -1));
        } else {
            const QRect first = nav->segment_rect(0);
            const QRect second = nav->segment_rect(1);
            const QRect third = nav->segment_rect(2);
            std::printf("UI-SMOKE nav: strip=%dx%d seg0=%d+%d seg1=%d+%d seg2=%d+%d seg=%dx%d\n",
                        nav->width(), nav->height(), first.x(), first.width(), second.x(),
                        second.width(), third.x(), third.width(), first.width(), first.height());
            std::fflush(stdout);
            const bool contiguous = second.x() - (first.x() + first.width()) == 0 &&
                                    third.x() - (second.x() + second.width()) == 0;
            if (first.width() != theme::Metrics::nav_seg_w ||
                first.height() != theme::Metrics::nav_h || !contiguous)
                smoke_fail(MainWindow::tr("导航段几何 %1×%2（期望 %3×%4、段间无 gap）")
                               .arg(first.width())
                               .arg(first.height())
                               .arg(theme::Metrics::nav_seg_w)
                               .arg(theme::Metrics::nav_h));
            if (nav->height() != theme::Metrics::nav_h ||
                nav->width() != 3 * theme::Metrics::nav_seg_w + 2 * theme::Metrics::nav_border)
                smoke_fail(MainWindow::tr("导航条几何 %1×%2（期望 %3×%4）")
                               .arg(nav->width())
                               .arg(nav->height())
                               .arg(3 * theme::Metrics::nav_seg_w + 2 * theme::Metrics::nav_border)
                               .arg(theme::Metrics::nav_h));
        }
        if (file_count == nullptr || !file_count->isVisible() || file_count->text().isEmpty()) {
            smoke_fail(MainWindow::tr("卡头计数徽标（.pill）异常：null=%1 visible=%2 text=\"%3\"")
                           .arg(file_count == nullptr ? 1 : 0)
                           .arg(file_count != nullptr && file_count->isVisible() ? 1 : 0)
                           .arg(file_count != nullptr ? file_count->text() : QString()));
        } else {
            std::printf("UI-SMOKE pill: count=%s objectName=%s\n",
                        qUtf8Printable(file_count->text()),
                        qUtf8Printable(file_count->objectName()));
            std::fflush(stdout);
        }
    }
    // ---- 左栏四个动作钮的 pp-* 测试钩子（M4-W2-fix 第 3 条：§9.3「objectName 沿 pp-*
    // 命名法」）---- 「＋ 添加文件…/添加文件夹…/清空列表」收在 ＋ 的弹出菜单里（T11 的 .fp-tools
    // 形态）， 故钩子挂在 QAction 上；「− 移除所选」是按钮。
    {
        const QAction *hooks[] = {add_files_action, add_dir_action, clear_action};
        const char *names[] = {"pp-add-files", "pp-add-dir", "pp-clear-all"};
        QStringList found;
        bool ok = true;
        for (int i = 0; i < 3; ++i) {
            const bool hit =
                hooks[i] != nullptr && hooks[i]->objectName() == QString::fromLatin1(names[i]);
            ok = ok && hit;
            found << QStringLiteral("%1=%2").arg(QString::fromLatin1(names[i]),
                                                 hooks[i] != nullptr ? hooks[i]->objectName()
                                                                     : QStringLiteral("(null)"));
        }
        const bool remove_ok =
            remove_sel != nullptr && remove_sel->objectName() == QStringLiteral("pp-remove-sel");
        ok = ok && remove_ok;
        found << QStringLiteral("pp-remove-sel=%1")
                     .arg(remove_sel != nullptr ? remove_sel->objectName()
                                                : QStringLiteral("(null)"));
        found << QStringLiteral("file-count=%1")
                     .arg(file_count != nullptr ? file_count->objectName()
                                                : QStringLiteral("(null)"));
        std::printf("UI-SMOKE hooks: %s\n", qUtf8Printable(found.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (!ok || file_count == nullptr ||
            file_count->objectName() != QStringLiteral("pp-file-count"))
            smoke_fail(MainWindow::tr("左栏 pp-* 测试钩子缺失或命名不符：%1")
                           .arg(found.join(QLatin1Char(' '))));
    }

    // ---- M5-T3（契约 C3）：矢量图标集自检 ----
    //   (a) 库面：§3.3 的 34 个必需名齐备 + 运行标记 3 名；每条路径非空、落在 24×24 设计网格内、
    //       等线宽 ∈ 1.4–1.6（§3.3「描边 1.5px 等线宽」）；
    //   (b) 同源面：assets/icons/<键>.svg 的 d 串与本库同名条目**逐元素同形**（同一 path_from_d
    //       解析后逐点比对；SVG 是设计源，不是运行期依赖 —— 不加 qrc/不依赖 Qt6::Svg）；
    //   (c) 控件面：骨架图标钮（caption 三钮 / ☆ / ⚙ / ＋ / − / ▶）挂的是矢量图标、无字形文本、
    //       且都有 accessibleName + tooltip（§11）；
    //   (d) 全局面：窗口内任何 QAbstractButton 的文案都不含禁用字形（§3.3 禁字形充当图标）。
    {
        const QVector<icons::Name> &names = icons::all_names();
        const char *const required[] = {
            "caption-min",  "caption-max",   "caption-restore", "caption-close", "star",  "gear",
            "search",       "add",           "remove",          "filter",        "group", "zoom-in",
            "zoom-out",     "zoom-fit",      "zoom-actual",     "prev",          "next",  "browse",
            "map-pin",      "map-search",    "folder-open",     "log",           "play",  "check",
            "chevron-down", "chevron-right", "clear",           "tag",           "image", "info",
            "warn",         "error",         "reset",           "edit"};
        int bad_required = 0;
        QStringList missing;
        for (const char *key : required) {
            icons::Name hit{};
            if (!icons::from_key(QLatin1String(key), &hit)) {
                ++bad_required;
                missing << QString::fromLatin1(key);
            }
        }
        // 解析器回归：C/S 的参数组耗尽后必须**回到命令循环**（继续解析后续子路径），不得截断。
        // 期望：M0 0 / L1 1 / M2 2 / C×3 / M6 6 / H7 / M8 8 = 9 个元素，末元素 = (8,8) 的 MoveTo。
        {
            const QPainterPath probe =
                icons::path_from_d(QStringLiteral("M0 0L1 1M2 2C3 3 4 4 5 5M6 6H7M8 8"));
            const QPainterPath::Element last = probe.elementCount() > 0
                                                   ? probe.elementAt(probe.elementCount() - 1)
                                                   : QPainterPath::Element{};
            if (probe.elementCount() != 9 ||
                !(last.x == 8.0 && last.y == 8.0 &&
                  last.type == QPainterPath::ElementType::MoveToElement)) {
                smoke_fail(MainWindow::tr("图标 d 串解析器截断（元素 %1，末元素 %2,%3）")
                               .arg(probe.elementCount())
                               .arg(last.x)
                               .arg(last.y));
            }
        }
        // 图标形的边界（含描边半径 0.75）：24 网格内不得越界（越界 = 形被裁）
        const qreal stroke_half = icons::kStrokePx / 2.0;
        int bad_path = 0, bad_bounds = 0, bad_svg = 0, bad_svg_attr = 0;
        QStringList bad_detail;
        const QDir assets(QDir(repo_root()).filePath(QStringLiteral("assets/icons")));
        for (icons::Name name : names) {
            const QPainterPath shape = icons::path(name);
            if (shape.isEmpty()) {
                ++bad_path;
                bad_detail << QStringLiteral("path:%1").arg(QString::fromLatin1(icons::key(name)));
                continue;
            }
            const QRectF bounds = shape.boundingRect();
            if (bounds.left() < -stroke_half || bounds.top() < -stroke_half ||
                bounds.right() > icons::kGrid + stroke_half ||
                bounds.bottom() > icons::kGrid + stroke_half) {
                ++bad_bounds;
                bad_detail
                    << QStringLiteral("bounds:%1").arg(QString::fromLatin1(icons::key(name)));
            }
            // (b) SVG 设计源同源：d 串集合解析后与 C++ 路径逐元素比对
            const QString path_file =
                assets.filePath(QString::fromLatin1(icons::key(name)) + QStringLiteral(".svg"));
            QFile fp(path_file);
            if (!fp.open(QIODevice::ReadOnly | QIODevice::Text)) {
                ++bad_svg;
                bad_detail
                    << QStringLiteral("svg-missing:%1").arg(QString::fromLatin1(icons::key(name)));
                continue;
            }
            const QString svg = QString::fromUtf8(fp.readAll());
            fp.close();
            if (!svg.contains(QStringLiteral("viewBox=\"0 0 24 24\"")) ||
                !svg.contains(QStringLiteral("stroke-width=\"1.5\"")))
                ++bad_svg_attr;
            QPainterPath from_svg;
            const QRegularExpression d_re(QStringLiteral("d=\"([^\"]*)\""));
            auto it = d_re.globalMatch(svg);
            while (it.hasNext())
                from_svg.addPath(icons::path_from_d(it.next().captured(1)));
            bool same = from_svg.elementCount() == shape.elementCount();
            for (int i = 0; same && i < shape.elementCount(); ++i) {
                const QPainterPath::Element a = shape.elementAt(i);
                const QPainterPath::Element b = from_svg.elementAt(i);
                same = a.type == b.type && std::abs(a.x - b.x) < 1e-6 && std::abs(a.y - b.y) < 1e-6;
            }
            if (!same) {
                ++bad_svg;
                bad_detail
                    << QStringLiteral("svg-mismatch:%1").arg(QString::fromLatin1(icons::key(name)));
            }
        }
        std::printf("UI-SMOKE icons: count=%d required=%d stroke=%.2f bad-path=%d bad-bounds=%d "
                    "bad-svg=%d bad-svg-attr=%d [%s]\n",
                    int(names.size()), int(sizeof(required) / sizeof(required[0])),
                    double(icons::kStrokePx), bad_path, bad_bounds, bad_svg, bad_svg_attr,
                    qUtf8Printable(bad_detail.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (names.size() < 34 || bad_required != 0 || bad_path != 0 || bad_bounds != 0 ||
            bad_svg != 0 || bad_svg_attr != 0 || icons::kStrokePx < icons::kStrokeMinPx ||
            icons::kStrokePx > icons::kStrokeMaxPx) {
            smoke_fail(
                MainWindow::tr("矢量图标集自检不符（清单 %1 缺 %2 / path=%3 bounds=%4 svg=%5 "
                               "attr=%6）： %7")
                    .arg(names.size())
                    .arg(missing.join(QLatin1Char(',')))
                    .arg(bad_path)
                    .arg(bad_bounds)
                    .arg(bad_svg)
                    .arg(bad_svg_attr)
                    .arg(bad_detail.join(QLatin1Char(' '))));
        }
    }
    {
        // (c) 骨架图标钮：矢量图标 + 无文本 + accessibleName/tooltip 齐（§11）。
        // 分两档：A = **纯图标钮**（可见面只有图标 → 必有 accessibleName + tooltip）；
        //        B = **带图标的文字钮**（文案本身即无障碍名 / 可见语义，如底栏 CTA）。
        const struct {
            QAbstractButton *button;
            const char *object_name;
            bool text_button;
        } icon_buttons[] = {
            {cap_min, "pp-cap-min", false},       {cap_max, "pp-cap-max", false},
            {cap_close, "pp-cap-close", false},   {presets_btn, "pp-presets", false},
            {settings_btn, "pp-settings", false}, {add_btn, "pp-add", false},
            {remove_sel, "pp-remove-sel", false}, {start, "pp-start", true},
        };
        QStringList bad;
        for (const auto &entry : icon_buttons) {
            const QAbstractButton *button = entry.button;
            const QString tag = QString::fromLatin1(entry.object_name);
            if (button == nullptr) {
                bad << tag + QStringLiteral("=null");
                continue;
            }
            const QString icon_key = button->property(icons::kIconNameProperty).toString();
            if (button->icon().isNull() || icon_key.isEmpty())
                bad << tag + QStringLiteral("=no-icon");
            if (entry.text_button) {
                if (button->text().isEmpty())
                    bad << tag + QStringLiteral("=no-text");
            } else {
                if (!button->text().isEmpty())
                    bad << tag + QStringLiteral("=glyph-text");
                if (button->accessibleName().isEmpty() || button->toolTip().isEmpty())
                    bad << tag + QStringLiteral("=a11y");
            }
        }
        // (d) 全局面：任何按钮文案都不含禁用字形（§3.3）。
        // 清单按**码点**构造（而非字面量）：否则本探针自身会命中"字面量字形"的全仓扫描。
        // ─ □ ✕ ☆ ⚙ ◀ ▶ ＋ − ✓ ❐ ✗ ■ ◐ » ⚑ ▾
        const char16_t banned_points[] = {0x2500, 0x25A1, 0x2715, 0x2606, 0x2699, 0x25C0,
                                          0x25B6, 0xFF0B, 0x2212, 0x2713, 0x2750, 0x2717,
                                          0x25A0, 0x25D0, 0x00BB, 0x2691, 0x25BE};
        QString banned;
        banned.reserve(int(std::size(banned_points)));
        for (char16_t point : banned_points)
            banned.append(QChar(point));
        for (QAbstractButton *button : w->findChildren<QAbstractButton *>()) {
            if (button == nullptr)
                continue;
            for (const QChar ch : button->text()) {
                if (banned.contains(ch)) {
                    bad << button->objectName() + QStringLiteral("=") + button->text();
                    break;
                }
            }
        }
        std::printf("UI-SMOKE icon-buttons: buttons=%d bad=%d [%s]\n",
                    int(sizeof(icon_buttons) / sizeof(icon_buttons[0])), int(bad.size()),
                    qUtf8Printable(bad.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (!bad.isEmpty())
            smoke_fail(MainWindow::tr("图标钮/字形残留：%1").arg(bad.join(QLatin1Char(' '))));
    }
    {
        // (e) QIcon 引擎路径（按钮实际走的那条）与直接绘制**等价**：同一图标两条路径的墨迹
        //     包围盒必须逐边相差 ≤1px（否则按钮上的图标会缺角/缩水 —— 引擎路径回归的守门断言）。
        //     M5-T14 口径修正（T13 真实平台红 7 例的**探针缺陷**）：旧引擎路径取
        //     `QIcon::pixmap(box)` 再回读像素 —— 该重载在 dpr≠1 平台上按**平台 dpr** 光栅化，
        //     返回的是**物理**像素图（dpr=2 → 24×24），与直接绘制的逻辑 12×12 跨坐标系比较，
        //     逐边差值恰为 2 倍（T13 取证行 icons-engine-space: via=24x24(dpr=2.0)）。修正 =
        //     引擎路径改走 `QIcon::paint()` 渲到 12×12/dpr=1 的同一目标 —— 即按钮经 QStyle
        //     实际走的路径。断言本体（逐边 ≤1px + 居中）零改动。
        const auto ink_box = [](const QImage &img) {
            int x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    if (qAlpha(img.pixel(x, y)) <= 8)
                        continue;
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x);
                    y1 = std::max(y1, y);
                }
            }
            return QRect(QPoint(x0, y0), QPoint(x1, y1));
        };
        const QSize box(12, 12);
        QStringList report;
        int bad = 0;
        // M5-T13 验收取证（只增；M5-T14 更正口径）：两条路径的**像素空间**采样 —— 修正后两条
        // 路径都在同一坐标系（逻辑 12×12 / dpr=1：direct = icons::paint，via = QIcon::paint）。
        // engine-raw = `QIcon::pixmap(box)` 的原始回读（真实平台上 = 物理像素 + 平台 dpr）：
        // 旧探针误把它当同空间是比较 bug 的现场证据，保留为诊断读数（不参与断言）。
        QSize direct_space(0, 0);
        QSize via_space(0, 0);
        qreal direct_dpr = 0.0;
        qreal via_dpr = 0.0;
        QSize raw_space(0, 0);
        qreal raw_dpr = 0.0;
        // 墨迹必须**居中**（|left+right-(side-1)| ≤ 2 且 |top+bottom-(side-1)| ≤ 2）：这一条抓
        // "网格忘了对中 → 图标偏移越界被裁"的回归（G1 实测踩过：整枚图标只剩右下角碎片）。
        const QStringList centered_icons{QStringLiteral("caption-max"), QStringLiteral("add"),
                                         QStringLiteral("remove"),      QStringLiteral("gear"),
                                         QStringLiteral("error"),       QStringLiteral("stop"),
                                         QStringLiteral("image"),       QStringLiteral("zoom-fit")};
        const auto ink_centered = [&box](const QRect &ink) {
            const int sum_x = ink.left() + ink.right();
            const int sum_y = ink.top() + ink.bottom();
            return std::abs(sum_x - (box.width() - 1)) <= 2 &&
                   std::abs(sum_y - (box.height() - 1)) <= 2;
        };
        for (icons::Name name :
             {icons::Name::CaptionMax, icons::Name::CaptionClose, icons::Name::Star,
              icons::Name::Gear, icons::Name::Add, icons::Name::Remove, icons::Name::Play}) {
            QImage direct(box, QImage::Format_ARGB32_Premultiplied);
            direct.fill(Qt::transparent);
            {
                QPainter p(&direct);
                icons::paint(p, name, QRectF(QPointF(0, 0), QSizeF(box)), Qt::white);
            }
            // 引擎路径 = QIcon::paint 渲到**同一 12×12/dpr=1 目标**（按钮经 QStyle 的路径；
            // 不再走 QIcon::pixmap(size) 的物理像素回读 —— 见本段开头 M5-T14 口径修正）
            QImage via_icon(box, QImage::Format_ARGB32_Premultiplied);
            via_icon.fill(Qt::transparent);
            {
                QPainter p(&via_icon);
                icons::icon(name, Qt::white).paint(&p, QRect(QPoint(0, 0), box));
            }
            if (direct_space.isEmpty()) { // M5-T13 取证：首个图标的空间样本即代表两条路径口径
                direct_space = direct.size();
                via_space = via_icon.size();
                direct_dpr = direct.devicePixelRatio();
                via_dpr = via_icon.devicePixelRatio();
                const QPixmap raw = icons::icon(name, Qt::white).pixmap(box); // 诊断：原始回读
                raw_space = raw.size();
                raw_dpr = raw.devicePixelRatio();
            }
            const QRect a = ink_box(direct);
            const QRect b = ink_box(via_icon);
            const bool same = a.isValid() && b.isValid() && std::abs(a.left() - b.left()) <= 1 &&
                              std::abs(a.top() - b.top()) <= 1 &&
                              std::abs(a.right() - b.right()) <= 1 &&
                              std::abs(a.bottom() - b.bottom()) <= 1;
            const bool must_center = centered_icons.contains(QString::fromLatin1(icons::key(name)));
            if (!same || (must_center && (!ink_centered(a) || !ink_centered(b))))
                ++bad;
            report << QStringLiteral("%1:%2/%3")
                          .arg(QString::fromLatin1(icons::key(name)),
                               QStringLiteral("%1,%2,%3,%4")
                                   .arg(a.left())
                                   .arg(a.top())
                                   .arg(a.right())
                                   .arg(a.bottom()),
                               QStringLiteral("%1,%2,%3,%4")
                                   .arg(b.left())
                                   .arg(b.top())
                                   .arg(b.right())
                                   .arg(b.bottom()));
        }
        std::printf("UI-SMOKE icons-engine: bad=%d [%s]\n", bad,
                    qUtf8Printable(report.join(QLatin1Char(' '))));
        // M5-T13 验收取证（只增；M5-T14 更正口径）：两条路径的实际像素空间 —— 修正后都在同一
        // 坐标系（逻辑 12×12/dpr=1）；engine-raw 为旧探针误用的 `QIcon::pixmap(box)` 原始回读
        // （真实平台上 = 物理像素 + 平台 dpr，与旧 via=24x24(dpr=2.0) 同源，供失败定位）。
        std::printf("UI-SMOKE icons-engine-space: box=%dx%d direct=%dx%d(dpr=%.1f) "
                    "via=%dx%d(dpr=%.1f) engine-raw=%dx%d(dpr=%.1f)\n",
                    box.width(), box.height(), direct_space.width(), direct_space.height(),
                    direct_dpr, via_space.width(), via_space.height(), via_dpr, raw_space.width(),
                    raw_space.height(), raw_dpr);
        std::fflush(stdout);
        if (bad != 0)
            smoke_fail(
                MainWindow::tr("QIcon 引擎路径与直接绘制的墨迹包围盒不符（%1 例）").arg(bad));
    }
}

void MainWindow::Impl::smoke_probe_frameless() {
    // ---- (a) 纯函数命中判定（合成盒：1440×900 窗口 / 44px 顶栏 / 三钮贴右 46×32 居中）----
    // M5-T4 机械换锚：几何改按 v6 §3（顶栏 44、caption 46×32 居中 —— 单轨下命中带 = 控件矩形），
    // 断言集合与语义不变（拖拽/排除区/三钮/八向缩放/最大化 1px 边）。
    const int tb = theme::Metrics::toolbar_height;
    const int cap_w = theme::Metrics::caption_btn_w;
    const int cap_h = theme::Metrics::caption_btn_hit_h;
    const int cap_y = (tb - cap_h) / 2; // 居中：44 顶栏里 32 高控件的 y = 6
    const int cap_mid_y = cap_y + cap_h / 2;
    plat::HitBox box;
    box.window = QRect(0, 0, 1440, 900);
    box.caption = QRect(0, 0, 1440, tb);
    box.min_button = QRect(1440 - 3 * cap_w, cap_y, cap_w, cap_h);
    box.max_button = QRect(1440 - 2 * cap_w, cap_y, cap_w, cap_h);
    box.close_button = QRect(1440 - cap_w, cap_y, cap_w, cap_h);
    // 一体式分段导航位（硬约束 1 的被排除区；宽 = 3×92 + 2×1）
    box.drag_excludes = {QRect(200, cap_y, 3 * theme::Metrics::nav_seg_w + 2, cap_h)};
    box.border = plat::kResizeBorderPx;
    struct HitCase {
        const char *name;
        QPoint point;
        plat::HitZone expect;
    };
    const HitCase cases[] = {
        {"caption", {700, cap_mid_y}, plat::HitZone::Caption},
        {"excluded", {250, cap_mid_y}, plat::HitZone::Client},
        {"min-btn", {1440 - 3 * cap_w + 10, cap_mid_y}, plat::HitZone::MinButton},
        {"max-btn", {1440 - 2 * cap_w + 10, cap_mid_y}, plat::HitZone::MaxButton},
        {"close-btn", {1440 - cap_w + 10, cap_mid_y}, plat::HitZone::CloseButton},
        {"client", {700, 450}, plat::HitZone::Client},
        {"outside", {1500, 450}, plat::HitZone::None},
        {"left", {2, 450}, plat::HitZone::Left},
        {"right", {1438, 450}, plat::HitZone::Right},
        {"top", {700, 2}, plat::HitZone::Top},
        {"bottom", {700, 898}, plat::HitZone::Bottom},
        {"top-left", {2, 2}, plat::HitZone::TopLeft},
        {"top-right", {1438, 2}, plat::HitZone::TopRight},
        {"bottom-left", {2, 898}, plat::HitZone::BottomLeft},
        {"bottom-right", {1438, 898}, plat::HitZone::BottomRight},
    };
    QStringList report;
    int bad = 0;
    for (const HitCase &item : cases) {
        const plat::HitZone got = plat::hit_test(box, item.point);
        report << QStringLiteral("%1=%2").arg(QString::fromLatin1(item.name),
                                              QString::fromLatin1(hit_zone_name(got)));
        if (got != item.expect) {
            ++bad;
            report.last() +=
                QStringLiteral("(≠%1)").arg(QString::fromLatin1(hit_zone_name(item.expect)));
        }
    }
    std::printf("UI-SMOKE hit: %s\n", qUtf8Printable(report.join(QLatin1Char(' '))));
    std::fflush(stdout);
    if (bad > 0)
        smoke_fail(MainWindow::tr("命中区判定不符 %1 项：%2").arg(bad).arg(report.join(' ')));

    // ---- (b) 最大化：仍保留 1px 缩放边命中，内侧立即回 Client（硬约束 2）----
    plat::HitBox max_box = box;
    max_box.maximized = true;
    const plat::HitZone edge = plat::hit_test(max_box, {0, 450});
    const plat::HitZone inner = plat::hit_test(max_box, {1, 450});
    const plat::HitZone corner = plat::hit_test(max_box, {1439, 899});
    std::printf("UI-SMOKE hit-maximized: edge=%s inner=%s corner=%s\n", hit_zone_name(edge),
                hit_zone_name(inner), hit_zone_name(corner));
    std::fflush(stdout);
    if (edge != plat::HitZone::Left || inner != plat::HitZone::Client ||
        corner != plat::HitZone::BottomRight)
        smoke_fail(MainWindow::tr("最大化 1px 缩放边命中不符（edge=%1 inner=%2 corner=%3）")
                       .arg(QString::fromLatin1(hit_zone_name(edge)),
                            QString::fromLatin1(hit_zone_name(inner)),
                            QString::fromLatin1(hit_zone_name(corner))));

    // ---- (c) Win32 命令常量（§9.1「三钮行为 SC_*」；数值由 frameless.cpp 的 static_assert 与 SDK
    // 核对）----
    std::printf("UI-SMOKE caption-sc: min=0x%lx max=0x%lx close=0x%lx\n",
                plat::syscommand_for(plat::CaptionAction::Minimize),
                plat::syscommand_for(plat::CaptionAction::Maximize),
                plat::syscommand_for(plat::CaptionAction::Close));
    std::fflush(stdout);
    if (plat::syscommand_for(plat::CaptionAction::Minimize) != 0xF020L ||
        plat::syscommand_for(plat::CaptionAction::Maximize) != 0xF030L ||
        plat::syscommand_for(plat::CaptionAction::Close) != 0xF060L)
        smoke_fail(MainWindow::tr("三钮命令常量不是 SC_MINIMIZE/SC_MAXIMIZE/SC_CLOSE"));

    if (frameless == nullptr) {
        smoke_fail(MainWindow::tr("无边框接线缺失（Frameless::attach 返回空）"));
        return;
    }

    // ---- (d) 真窗口命中：顶栏空白 = 拖拽；三钮 = 各自命中区；控件/列表 = 不拖拽（硬约束 1）----
    // M5-T4 机械换锚：三段式下顶栏中央已被**导航区**占据（导航居中），顶栏空白取样点随之移到
    // 导航区与工具区之间的空档；导航区的取样点是第 2 段中心。语义不变（空白=Caption 拖拽，
    // 导航/列表=Client 不拖拽）。
    const int drag_x = (nav->geometry().right() + 1 + presets_btn->x()) / 2;
    const QPoint caption_point = titlebar->mapToGlobal(QPoint(drag_x, titlebar->height() / 2));
    // 注意：mapToGlobal 吃**控件自身坐标**，故中心点取 rect().center()（geometry() 是父坐标系）
    const QPoint close_point = cap_close->mapToGlobal(cap_close->rect().center());
    const QPoint nav_point = nav->mapToGlobal(nav->segment_rect(1).center());
    const QPoint panel_point = view->mapToGlobal(QPoint(8, 8));
    const plat::HitZone live_caption = frameless->classify_logical(caption_point);
    const plat::HitZone live_close = frameless->classify_logical(close_point);
    const plat::HitZone live_nav = frameless->classify_logical(nav_point);
    const plat::HitZone live_panel = frameless->classify_logical(panel_point);
    std::printf("UI-SMOKE hit-live: caption=%s close=%s nav=%s panel=%s\n",
                hit_zone_name(live_caption), hit_zone_name(live_close), hit_zone_name(live_nav),
                hit_zone_name(live_panel));
    std::fflush(stdout);
    if (live_caption != plat::HitZone::Caption || live_close != plat::HitZone::CloseButton ||
        live_nav != plat::HitZone::Client || live_panel != plat::HitZone::Client) {
        smoke_fail(MainWindow::tr("真窗口命中不符：顶栏=%1 关闭钮=%2 导航区=%3 文件列表=%4")
                       .arg(QString::fromLatin1(hit_zone_name(live_caption)),
                            QString::fromLatin1(hit_zone_name(live_close)),
                            QString::fromLatin1(hit_zone_name(live_nav)),
                            QString::fromLatin1(hit_zone_name(live_panel))));
    }

    // ---- (e) 原生命中测试往返（Windows：SendMessage(WM_NCHITTEST) → HT* 实测）----
    // 这是能力表"拖拽移动/八向缩放/最大化钮 Snap/三钮"在**系统眼里**的端到端取证
    // （Snap 悬停与双击最大化是 OS 侧行为，落在 W5 手测清单）。
    bool native_supported = false;
    {
        const qreal dpr = w->devicePixelRatioF();
        const auto physical = [&](const QPoint &logical_global) {
            return plat::to_physical(logical_global, dpr);
        };
        struct NativeCase {
            const char *name;
            QPoint logical_global;
            plat::HitZone expect;
        };
        const QPoint right_edge(titlebar->width() - 1, titlebar->height() / 2);
        const QPoint bottom_edge(titlebar->width() / 2, w->height() - 1);
        const NativeCase native_cases[] = {
            {"caption", titlebar->mapToGlobal(QPoint(drag_x, titlebar->height() / 2)),
             plat::HitZone::Caption},
            // R-316：三钮是 Qt 客户区控件 → 原生命中必须是 HTCLIENT（否则平台插件吞点击）。
            // 几何命中（classify_logical）仍分别返回 CloseButton/MaxButton/MinButton，
            // 由上面的 hit-live 断言覆盖；这里验的是"系统交给谁"。
            {"close", cap_close->mapToGlobal(cap_close->rect().center()), plat::HitZone::Client},
            {"max", cap_max->mapToGlobal(cap_max->rect().center()), plat::HitZone::Client},
            {"min", cap_min->mapToGlobal(cap_min->rect().center()), plat::HitZone::Client},
            {"left", w->mapToGlobal(QPoint(0, w->height() / 2)), plat::HitZone::Left},
            {"right", w->mapToGlobal(right_edge), plat::HitZone::Right},
            {"bottom", w->mapToGlobal(bottom_edge), plat::HitZone::Bottom},
            {"client", view->mapToGlobal(QPoint(8, 8)), plat::HitZone::Client},
        };
        QStringList native_report;
        int native_bad = 0;
        for (const NativeCase &item : native_cases) {
            const plat::HitZone got =
                plat::probe_native_hit_test(w, physical(item.logical_global), &native_supported);
            native_report << QStringLiteral("%1=%2").arg(QString::fromLatin1(item.name),
                                                         QString::fromLatin1(hit_zone_name(got)));
            if (native_supported && got != item.expect) {
                ++native_bad;
                native_report.last() +=
                    QStringLiteral("(≠%1)").arg(QString::fromLatin1(hit_zone_name(item.expect)));
            }
        }
        std::printf("UI-SMOKE native-hit: supported=%d %s\n", native_supported ? 1 : 0,
                    qUtf8Printable(native_report.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (native_supported && native_bad > 0)
            smoke_fail(MainWindow::tr("原生命中测试不符 %1 项：%2")
                           .arg(native_bad)
                           .arg(native_report.join(' ')));
    }

    // ---- (f) 三钮命令路由（拦截模式：只取证不真的最小化/关闭）----
    frameless->set_intercept_actions(true);
    QStringList routed;
    const QMetaObject::Connection conn = QObject::connect(
        frameless, &plat::Frameless::caption_action, w, [&routed](plat::CaptionAction action) {
            routed << QString::fromLatin1(caption_action_name(action));
        });
    cap_min->click();
    cap_max->click();
    cap_close->click();
    QObject::disconnect(conn);
    frameless->set_intercept_actions(false);
    std::printf("UI-SMOKE caption-route: %s\n", qUtf8Printable(routed.join(QLatin1Char(','))));
    std::fflush(stdout);
    const QStringList expect{QStringLiteral("minimize"), QStringLiteral("maximize"),
                             QStringLiteral("close")};
    if (routed != expect)
        smoke_fail(MainWindow::tr("三钮命令路由不符：%1").arg(routed.join(',')));

    // ---- (g) caption **单轨**（M5-T4，裁定 A1；行名沿用 M4 的探测标签以便对账）----
    //   * 可见面 = 命中面 = 46×32（§3.3 逐字）→ 命中带恒等于控件矩形（band.top() = 0）；
    //   * 带外（控件矩形之外、仍落顶栏）按下不触发按钮，且窗口命中判定回落到 Caption（拖拽），
    //     不是"点了没反应"的死区 —— 语义与 M4 双轨探针一致，只有"带"的几何换了锚。
    {
        const QRect widget = cap_close->rect();
        const QRect band = CaptionButton::hit_band(widget);
        // 带外取样点：控件下缘之下 2px（相对控件坐标，故 y = 33；映射到顶栏仍在 44px 内）
        const int outside_y = widget.height() - 1 + 2;
        std::printf("UI-SMOKE caption-band: widget=%dx%d hit=%dx%d band-y=%d..%d "
                    "out-of-band-zone=%s\n",
                    widget.width(), widget.height(), band.width(), band.height(), band.top(),
                    band.bottom(),
                    hit_zone_name(frameless->classify_logical(
                        cap_close->mapToGlobal(QPoint(cap_close->width() / 2, outside_y)))));
        std::fflush(stdout);
        if (widget.height() != theme::Metrics::caption_btn_h ||
            widget.height() != theme::Metrics::caption_btn_hit_h ||
            band.height() != theme::Metrics::caption_btn_hit_h || band.top() != 0 || band != widget)
            smoke_fail(MainWindow::tr("caption 单轨几何不符：控件=%1×%2 命中带=%3×%4(top=%5)")
                           .arg(widget.width())
                           .arg(widget.height())
                           .arg(band.width())
                           .arg(band.height())
                           .arg(band.top()));
        // 带外（控件矩形外 2px）真发一次按下+释放：不得触发按钮
        frameless->set_intercept_actions(true);
        QStringList band_routed;
        const QMetaObject::Connection band_conn =
            QObject::connect(frameless, &plat::Frameless::caption_action, w,
                             [&band_routed](plat::CaptionAction action) {
                                 band_routed << QString::fromLatin1(caption_action_name(action));
                             });
        const auto send_click = [](QWidget *target, const QPoint &pos) {
            const QPointF global(target->mapToGlobal(pos));
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), global, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(target, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), global, Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(target, &release);
        };
        send_click(cap_close, QPoint(cap_close->width() / 2, outside_y)); // 带外
        const int routed_after_outside = static_cast<int>(band_routed.size());
        send_click(cap_close, QPoint(cap_close->width() / 2, widget.height() / 2)); // 带内
        QObject::disconnect(band_conn);
        frameless->set_intercept_actions(false);
        std::printf(
            "UI-SMOKE caption-dual-track: tracks=1 widget=%dx%d out-of-band=%d in-band=%s\n",
            widget.width(), widget.height(), routed_after_outside,
            band_routed.size() > 0 ? qUtf8Printable(band_routed.last()) : "(none)");
        std::fflush(stdout);
        if (routed_after_outside != 0 || band_routed.size() != 1 ||
            band_routed.last() != QStringLiteral("close"))
            smoke_fail(MainWindow::tr("caption 命中带失效：带外=%1 带内=%2")
                           .arg(routed_after_outside)
                           .arg(band_routed.join(',')));
        if (frameless->classify_logical(cap_close->mapToGlobal(
                QPoint(cap_close->width() / 2, outside_y))) != plat::HitZone::Caption)
            smoke_fail(MainWindow::tr("caption 命中区之外不是拖拽区（应回落 HTCAPTION/Caption）"));
    }

    std::printf("UI-SMOKE frameless-platform: native-hit-test=%d\n",
                plat::Frameless::native_hit_test_supported() ? 1 : 0);
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// M5-T4（C7）壳层几何值表探针：§3.1/§3.2/§3.3/§6.2 + R4 的壳层落点逐项 checked/bad。
// 口径 = "落在屏幕上的实际几何"（控件尺寸/栏宽切片/命中区），不是令牌读数 —— 令牌读数由
// smoke_probe_theme 的 v6 值表承担，两者互补（改了令牌但没落到布局 = 这里红）。
// ---------------------------------------------------------------------------
void MainWindow::Impl::smoke_probe_shell_geometry() {
    const QList<int> sizes = splitter != nullptr ? splitter->sizes() : QList<int>();
    const QRect caption = cap_close != nullptr ? cap_close->rect() : QRect();
    const QRect cap_band = cap_close != nullptr ? CaptionButton::hit_band(caption) : QRect();
    const QRect nav_first = nav != nullptr ? nav->segment_rect(0) : QRect();
    // 窗口内边距（§3.2 左右 10 / 上下 8）：以三栏区在窗口根容器里的落点反测（= 布局的实际留给
    // 内容区的边距），不是读令牌 —— 令牌与布局脱节时这里红。
    const int pad_x =
        splitter != nullptr && root != nullptr ? splitter->mapTo(root, QPoint(0, 0)).x() : -1;
    struct Expect {
        const char *name;
        int got;
        int want;
    };
    const Expect table[] = {
        // §3.1 窗口骨架
        {"toolbar_h", titlebar != nullptr ? titlebar->height() : -1,
         theme::Metrics::toolbar_height},
        {"statusbar_h", bottombar != nullptr ? bottombar->height() : -1,
         theme::Metrics::bottom_height},
        {"pad_x", pad_x, theme::Metrics::main_pad_x},
        // §3.3 顶栏三段
        {"brand_icon", app_icon != nullptr ? app_icon->width() : -1, theme::Metrics::brand_icon},
        {"icon_btn", presets_btn != nullptr ? presets_btn->width() : -1, theme::Metrics::icon_btn},
        // §6.2 一体式分段导航（段 92×30）
        {"nav_h", nav != nullptr ? nav->height() : -1, theme::Metrics::nav_h},
        {"nav_w", nav != nullptr ? nav->width() : -1,
         3 * theme::Metrics::nav_seg_w + 2 * theme::Metrics::nav_border},
        {"nav_seg_w", nav_first.width(), theme::Metrics::nav_seg_w},
        {"nav_seg_h", nav_first.height(), theme::Metrics::nav_h},
        // §3.3 caption 单轨 46×32（可见面 = 命中面）
        {"caption_w", caption.width(), theme::Metrics::caption_btn_w},
        {"caption_h", caption.height(), theme::Metrics::caption_btn_h},
        {"caption_hit_w", cap_band.width(), theme::Metrics::caption_btn_w},
        {"caption_hit_h", cap_band.height(), theme::Metrics::caption_btn_hit_h},
        // §3.2 栏宽固定轴（1440 校准切片）
        {"col_left", sizes.value(0, -1), theme::Metrics::left_width},
        {"col_mid", sizes.value(1, -1), theme::Metrics::mid_width_1440},
        {"col_right", sizes.value(2, -1), theme::Metrics::right_width},
        // R4 按钮高 26 / 主 CTA 22（A7：壳层内不得再挂 input_h）
        {"btn_h", add_btn != nullptr ? add_btn->height() : -1, theme::Metrics::btn_h},
        {"cta_h", start != nullptr ? start->height() : -1, theme::Metrics::cta_h},
        {"status_dot", status_dot != nullptr ? status_dot->width() : -1, theme::Metrics::dot_px},
    };
    const int count = int(sizeof(table) / sizeof(table[0]));
    QStringList bad;
    for (const Expect &e : table) {
        if (e.got != e.want)
            bad << QStringLiteral("%1=%2(期望 %3)")
                       .arg(QString::fromLatin1(e.name))
                       .arg(e.got)
                       .arg(e.want);
    }
    std::printf("UI-SMOKE shell-geom: checked=%d bad=%d %s\n", count, bad.size(),
                qUtf8Printable(bad.join(QLatin1Char(' '))));
    std::fflush(stdout);
    if (!bad.isEmpty())
        smoke_fail(
            MainWindow::tr("壳层几何（§3/§6.2/R4）不符：%1").arg(bad.join(QLatin1Char(' '))));

    // ---- §6.2 段序 + K1「不放 ✓ 等完成态装饰」----
    // 导航是**单控件**（段不是 QAbstractButton）→ M5-T3 的"全局面按钮文案字形扫描"覆盖不到这里，
    // 就地补一条同源断言：段序 = ① 元数据 → ② 输出 → ③ 运行（绑定既有三个 QAction，顺序即运行
    // 流程）、编号 1–3 内嵌、文案非空且不含完成态/字形钩子。
    {
        // 字形清单按码点构造（避免本探针自身命中"字面量字形"扫描）：✓ ✔ ✗ ● ▶ ◀ ▸ □
        const char16_t banned_points[] = {0x2713, 0x2714, 0x2717, 0x25CF,
                                          0x25B6, 0x25C0, 0x25B8, 0x25A1};
        QString banned;
        for (char16_t point : banned_points)
            banned.append(QChar(point));
        QStringList nav_bad;
        const QAction *expect_actions[] = {nav_meta, nav_output, nav_run};
        for (int i = 0; i < 3; ++i) {
            const QAction *action = nav != nullptr ? nav->segment_action(i) : nullptr;
            const QString text = action != nullptr ? action->text() : QString();
            if (nav == nullptr || action != expect_actions[i] || action == nullptr)
                nav_bad << QStringLiteral("seg%1=wrong-action").arg(i + 1);
            if (nav == nullptr || nav->segment_number(i) != i + 1)
                nav_bad << QStringLiteral("seg%1=wrong-number").arg(i + 1);
            if (text.isEmpty())
                nav_bad << QStringLiteral("seg%1=no-text").arg(i + 1);
            for (const QChar ch : text) {
                if (banned.contains(ch)) {
                    nav_bad << QStringLiteral("seg%1=glyph").arg(i + 1);
                    break;
                }
            }
        }
        std::printf("UI-SMOKE nav-text: segments=%d bad=%d\n",
                    nav != nullptr ? nav->segment_count() : -1, int(nav_bad.size()));
        std::fflush(stdout);
        if (!nav_bad.isEmpty())
            smoke_fail(MainWindow::tr("导航段序/文案不符（§6.2 + K1 无完成态装饰）：%1")
                           .arg(nav_bad.join(QLatin1Char(' '))));
    }

    // ---- A18（§11 无障碍）：可访问接口（tab/button group 语义）+ 自绘焦点环（accent 1px）+
    //      段间键盘导航按**视觉序** ----
    {
        QStringList a11y_bad;
        int role = -1;
        int child_count = -1;
        int child_role = -1;
        int child_selected = -1;
        int interface_ready = 0;
        QString interface_via = QStringLiteral("none");
        const int page_before = nav_meta != nullptr && nav_meta->isChecked()
                                    ? 1
                                    : (nav_output != nullptr && nav_output->isChecked() ? 2 : 3);
#if QT_CONFIG(accessibility)
        // (1) 接口来源：优先走 QAccessible 查询（= 无障碍栈的同一条路）；不可用（平台未激活
        //     无障碍）时退回**同类实例**直采语义 —— 两条路都过才算「接口在场」。
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(nav);
        ShellNavAccessible *owned = nullptr; // 直采实例（工厂路径的接口归 QAccessible 缓存所有）
        if (iface != nullptr) {
            interface_via = QStringLiteral("query");
        } else if (nav != nullptr) {
            owned = new ShellNavAccessible(nav);
            iface = owned;
            interface_via = QStringLiteral("direct");
        }
        if (iface != nullptr) {
            interface_ready = 1;
            role = int(iface->role());
            child_count = iface->childCount();
            if (QAccessibleInterface *child0 = iface->child(0)) {
                child_role = int(child0->role()); // 段 = PageTab（tab 语义）
                child_selected = child0->state().selected ? 1 : 0;
            }
        }
        delete owned;
        if (interface_ready == 0)
            a11y_bad << QStringLiteral("iface=none");
        if (role != int(QAccessible::PageTabList))
            a11y_bad
                << QStringLiteral("role=%1(期望 %2)").arg(role).arg(int(QAccessible::PageTabList));
        if (child_count != 3)
            a11y_bad << QStringLiteral("children=%1").arg(child_count);
        if (child_role != int(QAccessible::PageTab))
            a11y_bad << QStringLiteral("child-role=%1").arg(child_role);
        if (child_selected != 1) // 段 0（元数据）为当前选中段
            a11y_bad << QStringLiteral("child-selected=%1").arg(child_selected);
#else
        a11y_bad << QStringLiteral("accessibility-disabled");
#endif
        // (2) 控件可读名（§11：控件有可读名；非 QAccessible 路径也能取证）
        if (nav == nullptr || nav->accessibleName().isEmpty())
            a11y_bad << QStringLiteral("no-accessible-name");
        // (3) Tab 序：导航进入 Tab 链（A18 前 = NoFocus；单 Tab 停靠点 + 段间 ←/→ 视觉序）
        const bool tabbable = nav != nullptr && (nav->focusPolicy() & Qt::TabFocus) != 0;
        if (!tabbable)
            a11y_bad << QStringLiteral("no-tab-focus");
        // (4) 自绘焦点环：聚焦前/后同一控件实拍的差异像素（环 = accent 1px 圆角外框）。
        //     离屏平台窗口默认不激活（hasFocus() 需窗口 active）→ 先显式激活本窗口再聚焦。
        int ring_diff = -1;
        int ring_accent_hits = -1;
        bool ring_visible = false;
        if (nav != nullptr) {
            QApplication::setActiveWindow(w);
            pump(40);
            nav->clearFocus();
            pump(40);
            const QImage off_shot = nav->grab().toImage();
            nav->setFocus(Qt::TabFocusReason);
            pump(40);
            const QImage on_shot = nav->grab().toImage();
            ring_visible = nav->focus_ring_visible();
            int diff = 0;
            int accent_hits = 0;
            const QColor accent = tokens.accent;
            if (!off_shot.isNull() && off_shot.size() == on_shot.size()) {
                for (int y = 0; y < on_shot.height(); ++y) {
                    for (int x = 0; x < on_shot.width(); ++x) {
                        const QColor before = off_shot.pixelColor(x, y);
                        const QColor after = on_shot.pixelColor(x, y);
                        if (before == after)
                            continue;
                        ++diff;
                        const int dr = after.red() - accent.red();
                        const int dg = after.green() - accent.green();
                        const int db = after.blue() - accent.blue();
                        if (std::sqrt(double(dr * dr + dg * dg + db * db)) < 120.0)
                            ++accent_hits;
                    }
                }
            }
            ring_diff = diff;
            ring_accent_hits = accent_hits;
            if (!ring_visible || diff < 200 || accent_hits < 100)
                a11y_bad << QStringLiteral("focus-ring(visible=%1 diff=%2 accent=%3)")
                                .arg(ring_visible ? 1 : 0)
                                .arg(diff)
                                .arg(accent_hits);
        } else {
            a11y_bad << QStringLiteral("no-nav");
        }
        // (5) 段间键盘导航 = 视觉序（←/→；Home 到首段）+ 与鼠标同一条 QAction 路径
        int order_ok = 0;
        QString diag;
        if (nav != nullptr) {
            w->set_current_page(1);
            pump(60);
            diag = QStringLiteral("en=%1%2%3 seg=%4 preview=%5 focus=%6")
                       .arg(nav_meta != nullptr && nav_meta->isEnabled() ? 1 : 0)
                       .arg(nav_output != nullptr && nav_output->isEnabled() ? 1 : 0)
                       .arg(nav_run != nullptr && nav_run->isEnabled() ? 1 : 0)
                       .arg(nav->current_segment())
                       .arg(preview_panel != nullptr ? preview_panel->current_position_text()
                                                     : QStringLiteral("-"))
                       .arg(QApplication::focusWidget() != nullptr
                                ? QApplication::focusWidget()->objectName()
                                : QStringLiteral("-"));
            nav->setFocus(Qt::TabFocusReason);
            // 键面取 **Ctrl 变体**：预览面板装有应用级 Router（无修饰 ←/→ = 翻图，全窗口生效），
            // 带 Ctrl 的同一组键才是本应用里导航可达的一路（Router 只吃无修饰键）。
            const auto send_nav_key = [this](int key, Qt::KeyboardModifiers mods) {
                QKeyEvent press(QEvent::KeyPress, key, mods);
                QCoreApplication::sendEvent(nav, &press); // nav = Impl 成员（本块已判非空）
                pump(60);
            };
            const QString preview_before =
                preview_panel != nullptr ? preview_panel->current_position_text() : QString();
            send_nav_key(Qt::Key_Right, Qt::ControlModifier); // ① 元数据 → ② 输出（视觉序向右）
            const bool to_output = nav->current_segment() == 1 && nav_output != nullptr &&
                                   nav_output->isChecked() && stack != nullptr &&
                                   stack->currentIndex() == 1;
            send_nav_key(Qt::Key_Right, Qt::ControlModifier); // ② → ③ 运行
            const bool to_run =
                nav->current_segment() == 2 && stack != nullptr && stack->currentIndex() == 2;
            send_nav_key(Qt::Key_Home, Qt::ControlModifier); // 回到最左（视觉序首段）
            const bool to_home =
                nav->current_segment() == 0 && stack != nullptr && stack->currentIndex() == 0;
            const bool single_stop = nav->focusPolicy() == Qt::StrongFocus; // 单停靠点（内部导航）
            // 预览翻图不得被本控件抢走（Ctrl 键面 = 与预览热键正交的取证）
            const bool preview_kept = preview_panel == nullptr ||
                                      preview_panel->current_position_text() == preview_before;
            if (to_output && to_run && to_home && single_stop && preview_kept)
                order_ok = 1;
            else
                a11y_bad << QStringLiteral("order(out=%1 run=%2 home=%3 stop=%4 preview-kept=%5)")
                                .arg(to_output ? 1 : 0)
                                .arg(to_run ? 1 : 0)
                                .arg(to_home ? 1 : 0)
                                .arg(single_stop ? 1 : 0)
                                .arg(preview_kept ? 1 : 0);
            w->set_current_page(page_before); // 归位（后续探针/截图不受本块影响）
            nav->clearFocus();
            pump(60);
        }
        // (6) §11「禁用系统虚线框」：骨架 QSS 里导航的 outline 关闭（可见焦点指示 = 自绘环）
        const bool outline_off = w->styleSheet().contains(QStringLiteral("outline: none"));
        if (!outline_off)
            a11y_bad << QStringLiteral("outline-on");
        std::printf(
            "UI-SMOKE nav-a11y: rules=%d diag=[%s] iface=%s role=%d children=%d child-role=%d "
            "selected=%d name=%d tab=%d ring=%d diff=%d accent=%d order=%d outline-off=%d "
            "%s\n",
            int(a11y_bad.size()), qUtf8Printable(diag), qUtf8Printable(interface_via), role,
            child_count, child_role, child_selected,
            nav != nullptr && !nav->accessibleName().isEmpty() ? 1 : 0, tabbable ? 1 : 0,
            ring_visible ? 1 : 0, ring_diff, ring_accent_hits, order_ok, outline_off ? 1 : 0,
            qUtf8Printable(a11y_bad.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (!a11y_bad.isEmpty())
            smoke_fail(MainWindow::tr("导航无障碍（§11/A18）不符：%1")
                           .arg(a11y_bad.join(QLatin1Char(' '))));
    }

    // ---- 状态四档（§8）：QSS 侧四档规则齐备 + 当前落地档位（点/词同色、空/就绪不得为红）----
    {
        const QString sheet = w->styleSheet();
        QStringList tone_bad;
        for (const char *tone :
             {theme::kToneNeutral, theme::kToneOk, theme::kToneWarn, theme::kToneErr}) {
            const QString value = QString::fromLatin1(tone);
            for (const QString &selector :
                 {QStringLiteral("QLabel#pp-status"), QStringLiteral("QLabel#pp-status-dot")}) {
                const QString rule =
                    selector + QStringLiteral("[%1=\"%2\"]")
                                   .arg(QString::fromLatin1(theme::kStatusToneProperty), value);
                if (!sheet.contains(rule))
                    tone_bad << rule;
            }
        }
        const QString live = status_dot != nullptr
                                 ? status_dot->property(theme::kStatusToneProperty).toString()
                                 : QString();
        const QString live_word =
            status != nullptr ? status->property(theme::kStatusToneProperty).toString() : QString();
        std::printf("UI-SMOKE status-tones: rules-bad=%d live=%s word=%s\n", tone_bad.size(),
                    qUtf8Printable(live), qUtf8Printable(live_word));
        std::fflush(stdout);
        if (!tone_bad.isEmpty())
            smoke_fail(
                MainWindow::tr("状态四档 QSS 规则缺失：%1").arg(tone_bad.join(QLatin1Char(' '))));
        // 点与词同色（同一条属性值）；且**空态**（未勾选 = 中性档）绝不为红
        if (live.isEmpty() || live != live_word)
            smoke_fail(MainWindow::tr("状态点与状态词不同档（点=%1 词=%2）").arg(live, live_word));
        if (live == QString::fromLatin1(theme::kToneErr) && model->checked_count() == 0)
            smoke_fail(MainWindow::tr("空/未勾选态用了红档（§8：就绪/空不得用红）"));
    }

    // ---- W-R-T13（A39 ⑤）：**选中数不被预检警告吞掉** ----
    //   两态取证（在位改值后原样还原）：
    //     * 输出根目录未设 → 「已选 N / M · 未设置输出根目录」（琥珀档；N/M = 当前勾选/总数）；
    //     * 根目录就位   → 「已选 N / M · 就绪」语义不变（档位按例外数：§8 例外 = 琥珀）。
    {
        const QString keep_root = page_output->out_root();
        const int checked_now = int(model->checked_count());
        const int total_now = int(model->size());
        const std::size_t exceptions_now = model->exception_count();
        const QString prefix = MainWindow::tr("已选 %1 / %2 · ").arg(checked_now).arg(total_now);
        const auto tone_now = [this] {
            return status != nullptr ? status->property(theme::kStatusToneProperty).toString()
                                     : QString();
        };
        page_output->set_out_root(QString()); // 预检必拦（ready_to_start = 未设置输出根目录）
        pump(60);
        const QString unset_reason = page_output->ready_to_start();
        const QString unset_text = status != nullptr ? status->full_text() : QString();
        const QString unset_tone = tone_now();
        // 根目录就位档：给一个**绝对**临时目录（其余预检项照旧）→ 读数 + 档位按就地实况判
        page_output->set_out_root(QDir::tempPath());
        pump(60);
        const QString ready_reason = page_output->ready_to_start();
        const QString ready_text = status != nullptr ? status->full_text() : QString();
        const QString ready_tone = tone_now();
        page_output->set_out_root(keep_root); // 还原（探针不改被检对象的终态）
        pump(60);
        const bool unset_ok = checked_now > 0 && !unset_reason.isEmpty() &&
                              unset_text.startsWith(prefix) &&
                              unset_text.contains(QStringLiteral("未设置输出根目录")) &&
                              unset_tone == QString::fromLatin1(theme::kToneWarn);
        // 无阻塞 → 「已选 N / M · 就绪」（档位按例外数：§8 例外 = 琥珀）；若还有别的预检项没到位 →
        // 读数前缀仍须在场、警告接在其后（本探针要钉的正是"选中数不被警告吞掉"）。
        const bool ready_ok =
            ready_reason.isEmpty()
                ? (ready_text.startsWith(prefix) && ready_text.contains(MainWindow::tr("就绪")) &&
                   ready_tone ==
                       QString::fromLatin1(exceptions_now > 0 ? theme::kToneWarn : theme::kToneOk))
                : (ready_text.startsWith(prefix) && ready_text.contains(ready_reason) &&
                   ready_tone == QString::fromLatin1(theme::kToneWarn));
        std::printf("UI-SMOKE status-selection: checked=%d/%d unset=\"%s\"(%s) ready=\"%s\"(%s) "
                    "ready-reason=\"%s\" exceptions=%d\n",
                    checked_now, total_now, qUtf8Printable(unset_text), qUtf8Printable(unset_tone),
                    qUtf8Printable(ready_text), qUtf8Printable(ready_tone),
                    qUtf8Printable(ready_reason), int(exceptions_now));
        std::fflush(stdout);
        if (!unset_ok || !ready_ok)
            smoke_fail(MainWindow::tr("状态栏选中数被警告吞掉（unset=%1[%2] ready=%3[%4] 期望前缀 "
                                      "\"%5\"）")
                           .arg(unset_text, unset_tone, ready_text, ready_tone, prefix));
    }

    // ---- A19（T11，§3.2 骨架行）：底栏第三段「剩余磁盘」—— 自然语读数（值 + 单位）+ 分区 hairline
    // ----
    {
        const QString text = disk_status != nullptr ? disk_status->full_text() : QString();
        const QString prefix = QStringLiteral("剩余磁盘 ");
        const QString value = text.startsWith(prefix) ? text.mid(prefix.size()) : QString();
        static const QRegularExpression natural(
            QStringLiteral("^\\d+(\\.\\d)? (B|KB|MB|GB|TB|PB)$"));
        const bool reading = natural.match(value).hasMatch();
        const bool placeholder = value == QStringLiteral("—");
        // 分区位置：… [输出摘要] [hairline] [剩余磁盘]（段序逐字 = §3.2 骨架行）
        const auto cx = [this](const QWidget *widget) {
            return widget != nullptr ? widget->mapTo(root, QPoint(0, 0)).x() : -1;
        };
        const int out_right =
            output_status != nullptr ? cx(output_status) + output_status->width() : -1;
        const int sep_x = cx(disk_sep);
        const int label_x = cx(disk_status);
        const bool ordered = out_right >= 0 && sep_x > out_right && label_x > sep_x;
        const bool tip = disk_status != nullptr && !disk_status->toolTip().isEmpty();
        std::printf("UI-SMOKE status-disk: text=\"%s\" reading=%d placeholder=%d ordered=%d "
                    "tip=%d\n",
                    qUtf8Printable(text), reading ? 1 : 0, placeholder ? 1 : 0, ordered ? 1 : 0,
                    tip ? 1 : 0);
        std::fflush(stdout);
        if (disk_status == nullptr || disk_sep == nullptr || !ordered || !tip) {
            smoke_fail(MainWindow::tr("底栏「剩余磁盘」段不符（控件=%1/%2 段序=%3 tooltip=%4）")
                           .arg(disk_status != nullptr ? 1 : 0)
                           .arg(disk_sep != nullptr ? 1 : 0)
                           .arg(ordered ? 1 : 0)
                           .arg(tip ? 1 : 0));
        }
        // 自然语读数值（在真实卷上必须给真读数：占位「—」= 卷不可读）
        if (!reading && !placeholder) {
            smoke_fail(MainWindow::tr("「剩余磁盘」不是「值 + 单位」自然语：\"%1\"").arg(text));
        }
        if (placeholder) {
            smoke_fail(MainWindow::tr("「剩余磁盘」未取到卷可用空间（工作目录所在卷不可读？）"));
        }
    }
}

void MainWindow::Impl::smoke_probe_theme() {
    const auto dump = [](const char *label, const theme::Tokens &t) {
        std::printf(
            "UI-SMOKE theme-%s: window=%s card=%s card-bd=%s text=%s/%s/%s accent=%s "
            "accent-dim=%s on-accent=%s go-text=%s mod=%s/%s control=%s progress=%s/%s "
            "shadow=(dy=%d blur=%d) go-shadow=(dy=%d blur=%d)\n",
            label, qUtf8Printable(theme::css_color(t.window)),
            qUtf8Printable(theme::css_color(t.card)), qUtf8Printable(theme::css_color(t.card_bd)),
            qUtf8Printable(theme::css_color(t.text)), qUtf8Printable(theme::css_color(t.text2)),
            qUtf8Printable(theme::css_color(t.text3)), qUtf8Printable(theme::css_color(t.accent)),
            qUtf8Printable(theme::css_color(t.accent_dim)),
            qUtf8Printable(theme::css_color(t.on_accent)),
            qUtf8Printable(theme::css_color(t.go_text)), qUtf8Printable(theme::css_color(t.mod_bd)),
            qUtf8Printable(theme::css_color(t.mod_text)),
            qUtf8Printable(theme::css_color(t.control)),
            qUtf8Printable(theme::css_color(t.progress_fill_from)),
            qUtf8Printable(theme::css_color(t.progress_fill_to)), t.shadow_dy, t.shadow_blur,
            t.go_shadow_dy, t.go_shadow_blur);
        std::fflush(stdout);
    };
    // M5-T1：期望值换锚到设计说明书 v6 §4.1/§4.3（原 v4 mockup 口径的 card-bd .08 / r8 / r4
    // 随令牌换值机械对齐 —— 语义不变：tokens 必须与单源规范逐字一致）。α 改以**百分比**断言，
    // ±1pp 容差只吸收 8bit α 量化（0.09×255 = 22.95 → 23 → 回读 9.02%）。
    const auto alpha_pp = [](const QColor &c) {
        return static_cast<int>(std::lround(c.alphaF() * 100.0));
    };
    const auto near_pp = [&alpha_pp](const QColor &c, int pp) {
        return std::abs(alpha_pp(c) - pp) <= 1;
    };
    set_theme_mode(theme::ThemeMode::Dark);
    pump(80);
    dump("dark", tokens);
    const theme::Tokens dark = tokens;
    if (dark.window != QColor(0x22, 0x25, 0x2b) || dark.window_grad0 != QColor(0x22, 0x25, 0x2b) ||
        dark.window_grad1 != QColor(0x22, 0x25, 0x2b) ||
        dark.window_grad2 != QColor(0x22, 0x25, 0x2b) || // v6：暗色 win = 纯色（无渐变）
        dark.text != QColor(0xec, 0xec, 0xf0) || dark.text2 != QColor(0xa9, 0xaa, 0xb4) ||
        dark.text3 != QColor(0x7a, 0x7b, 0x86) || !near_pp(dark.card, 5) ||
        !near_pp(dark.card_bd, 10) || !near_pp(dark.control, 7) || !near_pp(dark.control_bd, 12) ||
        !near_pp(dark.accent_dim, 10) || !near_pp(dark.accent_bd, 34) ||
        dark.on_accent != QColor(0x0c, 0x1b, 0x24) || dark.go_text != QColor(0x0c, 0x1b, 0x24) ||
        dark.ok != QColor(0x4c, 0xc4, 0x7c) || dark.warn != QColor(0xe0, 0xa9, 0x4a) ||
        dark.err != QColor(0xff, 0x7a, 0x8a) || !near_pp(dark.ok_dim, 10) ||
        !near_pp(dark.ok_bd, 32) || !near_pp(dark.warn_dim, 10) || !near_pp(dark.warn_bd, 32) ||
        !near_pp(dark.err_dim, 10) || !near_pp(dark.err_bd, 32) ||
        dark.stage_bg != QColor(0x16, 0x18, 0x1d) || !near_pp(dark.shadow_color, 0) ||
        dark.shadow_blur != 0 || dark.go_shadow_blur != 10 || dark.go_shadow_dy != 2 ||
        dark.window_mid_stop != 0.45 || dark.radius_card != 6 || dark.radius_control != 3 ||
        dark.radius_stage != 4 || dark.radius_pill != 3 || dark.mod_bd != dark.accent_bd ||
        dark.mod_text != dark.accent || dark.progress_fill_from != QColor(0x3a, 0xa3, 0xdc) ||
        dark.progress_fill_to != QColor(0x4c, 0xc2, 0xff))
        smoke_fail(MainWindow::tr("深色 tokens 与 v6 §4.1 表不符"));

    set_theme_mode(theme::ThemeMode::Light);
    pump(80);
    dump("light", tokens);
    const theme::Tokens light = tokens;
    if (light.window != QColor(0xe3, 0xe5, 0xea) ||
        light.window_grad0 != QColor(0xe9, 0xec, 0xf2) ||
        light.window_grad2 != QColor(0xe7, 0xef, 0xe9) || // v6：亮色 win = 三停渐变
        light.card != QColor(0xff, 0xff, 0xff) || !near_pp(light.card_bd, 9) ||
        light.text != QColor(0x1b, 0x1c, 0x20) || light.text2 != QColor(0x5d, 0x5f, 0x68) ||
        light.text3 != QColor(0x8b, 0x8d, 0x96) || light.control != QColor(0xee, 0xf0, 0xf4) ||
        !near_pp(light.control_bd, 12) || !near_pp(light.accent_dim, 8) ||
        !near_pp(light.accent_bd, 32) || light.on_accent != QColor(0xff, 0xff, 0xff) ||
        light.go_text != QColor(0x0c, 0x1b, 0x24) || light.ok != QColor(0x1d, 0x7a, 0x3e) ||
        light.warn != QColor(0x9a, 0x6a, 0x00) || light.err != QColor(0xc4, 0x2b, 0x3a) ||
        !near_pp(light.ok_dim, 8) || !near_pp(light.ok_bd, 30) || !near_pp(light.warn_dim, 8) ||
        !near_pp(light.warn_bd, 30) || !near_pp(light.err_dim, 8) || !near_pp(light.err_bd, 30) ||
        light.stage_bg != QColor(0x16, 0x18, 0x1d) || light.shadow_blur != 4 ||
        light.shadow_dy != 1 || !near_pp(light.shadow_color, 7) || light.go_shadow_blur != 10 ||
        light.go_shadow_dy != 2 || light.window_mid_stop != 0.45 || light.radius_pill != 3 ||
        light.mod_bd != light.accent_bd || light.mod_text != light.accent)
        smoke_fail(MainWindow::tr("浅色 tokens 与 v6 §4.1 表不符"));

    // ---- M5-T1：§4.3 间距/圆角 + §4.2 字阶 + §5 R4 控件度量（逐行；token 单源取值）----
    // 值表驱动：任一不符即 FAIL 并把**全部**不符项连名带值打印（不靠猜）。
    {
        struct Expect {
            const char *name;
            int got;
            int want;
        };
        const Expect table[] = {
            // R4 控件度量
            {"spin_h", theme::Metrics::spin_h, 24},
            {"spin_arrow_w", theme::Metrics::spin_arrow_w, 17},
            {"spin_arrow_h", theme::Metrics::spin_arrow_h, 12},
            {"combo_h", theme::Metrics::combo_h, 24},
            {"combo_arrow_px", theme::Metrics::combo_arrow_px, 8},
            {"field_w", theme::Metrics::field_w, 160},
            {"input_h", theme::Metrics::input_h, 24},
            {"checkbox_px", theme::Metrics::checkbox_px, 13},
            {"checkbox_radius", theme::Metrics::checkbox_radius, 2},
            {"switch_w", theme::Metrics::switch_w, 30},
            {"switch_h", theme::Metrics::switch_h, 16},
            {"switch_knob", theme::Metrics::switch_knob, 12},
            {"seg_h", theme::Metrics::seg_h, 24},
            {"seg_w", theme::Metrics::seg_w, 88},
            {"seg_radius", theme::Metrics::seg_radius, 3},
            {"btn_h", theme::Metrics::btn_h, 26},
            {"cta_h", theme::Metrics::cta_h, 22},
            {"pill_h", theme::Metrics::pill_h, 16},
            {"progress_h", theme::Metrics::progress_h, 6},
            {"mini_progress_px", theme::Metrics::mini_progress_px, 6},
            {"big_progress_px", theme::Metrics::big_progress_px, 6},
            {"icon_btn", theme::Metrics::icon_btn, 28},
            {"icon_btn_radius", theme::Metrics::icon_btn_radius, 4},
            {"thumb_px", theme::Metrics::thumb_px, 46},
            {"thumb_radius", theme::Metrics::thumb_radius, 4},
            // §4.3 圆角 / 间距令牌
            {"card_radius", theme::Metrics::card_radius, 6},
            {"control_radius", theme::Metrics::control_radius, 3},
            {"btn_radius", theme::Metrics::btn_radius, 3},
            {"go_radius", theme::Metrics::go_radius, 4},
            {"stage_radius", theme::Metrics::stage_radius, 4},
            {"dialog_radius", theme::Metrics::dialog_radius, 6},
            {"pill_radius", theme::Metrics::pill_radius, 3},
            {"dot_px", theme::Metrics::dot_px, 8},
            {"dot_radius", theme::Metrics::dot_radius, 99},
            {"spacing_xs", theme::Metrics::spacing_xs, 4},
            {"spacing_s", theme::Metrics::spacing_s, 8},
            {"spacing_m", theme::Metrics::spacing_m, 12},
            {"spacing_l", theme::Metrics::spacing_l, 16},
            {"gap", theme::Metrics::gap, 8},
            {"group_gap", theme::Metrics::group_gap, 8},
            {"card_pad_top", theme::Metrics::card_pad_top, 10},
            {"card_pad_x", theme::Metrics::card_pad_x, 12},
            {"card_pad_bottom", theme::Metrics::card_pad_bottom, 12},
            {"form_col_gap", theme::Metrics::form_col_gap, 12},
            // §4.2 字阶（px 以 1/10px 整数编码比较，避开浮点 ==）
            {"card_title_px_x10", int(theme::Typography::card_title_px * 10), 120},
            {"card_title_weight", theme::Typography::card_title_weight, 700},
            {"body_px_x10", int(theme::Typography::body_px * 10), 115},
            {"small_px_x10", int(theme::Typography::small_px * 10), 110},
            {"hint_px_x10", int(theme::Typography::hint_px * 10), 100},
            {"badge_px_x10", int(theme::Typography::badge_px * 10), 95},
            {"badge_weight", theme::Typography::badge_weight, 700},
            {"base_px_x10", int(theme::Typography::base_px * 10), 115},
        };
        const int count = int(sizeof(table) / sizeof(table[0]));
        QStringList bad;
        for (const Expect &e : table) {
            if (e.got != e.want)
                bad << QStringLiteral("%1=%2(期望 %3)")
                           .arg(QString::fromLatin1(e.name))
                           .arg(e.got)
                           .arg(e.want);
        }
        std::printf("UI-SMOKE theme-tokens-v6: checked=%d bad=%d %s\n", count, bad.size(),
                    qUtf8Printable(bad.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (!bad.isEmpty())
            smoke_fail(MainWindow::tr("v6 令牌（§4.2/§4.3/R4）取值不符：%1")
                           .arg(bad.join(QLatin1Char(' '))));
    }

    // accent（§9.2：系统强调色，mockup 值兜底）：判据 = 调色板样式是否镜像系统（见 theme.h）。
    // 有系统强调色 → 明暗两侧同值；否则逐侧比对 mockup 兜底值
    const bool sys_accent = theme::system_accent_available();
    const QString style_name =
        QApplication::style() != nullptr ? QApplication::style()->objectName() : QString();
    std::printf("UI-SMOKE accent-source: %s (style=%s mirrors-system=%d)\n",
                sys_accent ? "system-accent" : "mockup-fallback", qUtf8Printable(style_name),
                theme::style_mirrors_system_accent() ? 1 : 0);
    std::fflush(stdout);
    if (sys_accent) {
        if (dark.accent != light.accent)
            smoke_fail(MainWindow::tr("系统强调色下明暗 accent 不一致（%1/%2）")
                           .arg(theme::css_color(dark.accent), theme::css_color(light.accent)));
    } else if (dark.accent != QColor(0x4c, 0xc2, 0xff) ||
               light.accent != QColor(0x00, 0x67, 0xc0)) {
        smoke_fail(MainWindow::tr("无系统强调色时 accent 未退回 mockup 值（%1/%2）")
                       .arg(theme::css_color(dark.accent), theme::css_color(light.accent)));
    }

    // ---- 明暗跟随系统（§9.2 末行）----
    // 处理路径 = system_theme_mode()（读 QStyleHints::colorScheme）→ set_theme_mode()，
    // 由 wire() 的 colorSchemeChanged 槽驱动。offscreen 平台不派发该信号（下面的
    // signal-fired 只打印不判死），故这里对**解析 + 落地**两段分别取证：
    set_theme_mode(theme::ThemeMode::Dark);
    pump(40);
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    pump(80);
    const Qt::ColorScheme hint = QGuiApplication::styleHints()->colorScheme();
    const theme::ThemeMode resolved = theme::system_theme_mode();
    const bool signal_fired = !tokens.dark(); // 槽被派发 → tokens 已翻到浅色
    std::printf("UI-SMOKE theme-follow: hint=%s resolved=%s signal-fired=%d applied=%s\n",
                hint == Qt::ColorScheme::Light
                    ? "light"
                    : (hint == Qt::ColorScheme::Dark ? "dark" : "unknown"),
                resolved == theme::ThemeMode::Light ? "light" : "dark", signal_fired ? 1 : 0,
                tokens.dark() ? "dark" : "light");
    std::fflush(stdout);
    if (hint == Qt::ColorScheme::Light && resolved != theme::ThemeMode::Light)
        smoke_fail(MainWindow::tr("system_theme_mode() 未跟随 QStyleHints::colorScheme"));
    set_theme_mode(resolved); // 与信号槽同一条处理路径
    pump(40);
    if (resolved == theme::ThemeMode::Light && tokens.dark())
        smoke_fail(MainWindow::tr("set_theme_mode(light) 未落地浅色 tokens"));
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    pump(80);
    set_theme_mode(theme::system_theme_mode());
    pump(40);
    if (tokens.dark() != true)
        smoke_fail(MainWindow::tr("色板回到深色后 tokens 未回深色"));

    // 截图基准 = 启动口径（PP_UI_THEME 强制档 / 跟随系统；原型基准是 meta-dark）
    set_theme_mode(preferred_theme_mode());
    pump(80);
    std::printf("UI-SMOKE theme-baseline: %s\n", tokens.dark() ? "dark" : "light");
    std::fflush(stdout);

    // ---- theme::set_mod 便捷函数（M4-W2-fix 第 9 条；T12 的 .mod 消费入口）----
    // 承诺的语义 = setProperty(kModProperty) + unpolish/polish 触发 QSS 重算；QSS 侧规则
    // （*[ppMod="true"] 的强调边框/字 + 700 字重）由 style_sheet 落地，这里两者都取证。
    {
        QLabel probe;
        theme::set_mod(&probe, true);
        const bool on = probe.property(theme::kModProperty).toBool();
        theme::set_mod(&probe, false);
        const bool off = !probe.property(theme::kModProperty).toBool();
        const bool rule = w->styleSheet().contains(QStringLiteral("ppMod"));
        std::printf("UI-SMOKE set-mod: on=%d off=%d qss-rule=%d\n", on ? 1 : 0, off ? 1 : 0,
                    rule ? 1 : 0);
        std::fflush(stdout);
        if (!on || !off || !rule)
            smoke_fail(MainWindow::tr("theme::set_mod 未落地（on=%1 off=%2 qss-rule=%3）")
                           .arg(on ? 1 : 0)
                           .arg(off ? 1 : 0)
                           .arg(rule ? 1 : 0));
    }

    // ---- M5-T1：徽章 pill 全色调 QSS（§4.1 末行：同色 8–10% 底 + 30–34% 描边 + 同色字）----
    // 选择器 + 三色都要在生成串里逐条落地（arg 链错位是 QSS 生成的典型失效模式：
    // 选择器会被吃成空串、或色调张冠李戴 —— 这里逐色调钉住，不留"看起来对"）。
    {
        const QString sheet = w->styleSheet();
        QStringList pill_bad;
        for (const char *tone : {"n", "acc", "ok", "warn", "err"}) {
            const theme::PillPaint paint = theme::pill_paint(tokens, tone);
            const QString sel =
                QStringLiteral("QLabel[ppPill=\"%1\"]").arg(QString::fromLatin1(tone));
            if (!sheet.contains(sel) || !sheet.contains(theme::css_color(paint.fg)) ||
                !sheet.contains(theme::css_color(paint.bg)) ||
                !sheet.contains(theme::css_color(paint.bd)))
                pill_bad << sel;
        }
        std::printf("UI-SMOKE pill-tones: tones=5 bad=%d mode=%s\n", pill_bad.size(),
                    tokens.dark() ? "dark" : "light");
        std::fflush(stdout);
        if (!pill_bad.isEmpty())
            smoke_fail(MainWindow::tr("徽章 pill 色调 QSS 缺失/错色：%1")
                           .arg(pill_bad.join(QLatin1Char(' '))));
    }
}

void MainWindow::Impl::smoke_probe_lifecycle() {
    // 窗口构造/销毁（第二实例）+ 最小尺寸约束（§9.1 1180×720）
    auto *second = new MainWindow(pp::AppSettings{}, nullptr);
    second->resize(600, 400); // 低于最小窗口 → 应被钳到 1180×720
    second->show();
    pump(150);
    const QSize size = second->size();
    const bool frameless_flag = (second->windowFlags() & Qt::FramelessWindowHint) != 0;
    std::printf("UI-SMOKE lifecycle: second-window=%dx%d frameless=%d\n", size.width(),
                size.height(), frameless_flag ? 1 : 0);
    std::fflush(stdout);
    if (size.width() != theme::Metrics::min_window_w ||
        size.height() != theme::Metrics::min_window_h)
        smoke_fail(MainWindow::tr("最小尺寸未生效：%1×%2").arg(size.width()).arg(size.height()));
    if (!frameless_flag)
        smoke_fail(MainWindow::tr("第二窗口未置 Qt::FramelessWindowHint"));
    delete second; // 不走 close()：避免 Close 过滤器写会话
    pump(80);
    std::printf("UI-SMOKE lifecycle: destroyed ok\n");
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// M4-T10 预览面板自检（§6.1 控件面 + 热键接线位）
//   自动化部分：徽标文案恒显、缩放 chip 两枚、热键提示表（随注册表生成）、位置读数、
//   翻图（next/previous → 索引/读数/出图）、缩放（适应/1:1 的绘制尺寸差异）、
//   热键路由（未锁定发出 class_hotkey、锁定后不发出）。
//   手测部分（W5 走查清单）：真实 48MP 点选的 250ms 主观手感、1:1 拖拽平移、快速翻图不堆积。
// ---------------------------------------------------------------------------

void MainWindow::Impl::smoke_probe_preview() {
    if (preview_panel == nullptr) {
        smoke_fail(MainWindow::tr("预览面板缺失（PreviewPanel 未建）"));
        return;
    }
    if (model->empty()) {
        smoke_fail(MainWindow::tr("预览自检：文件列表为空"));
        return;
    }
    // 1:1 断言要读 model 的 probe 摘要（源尺寸）→ 先等缩略图通道把 probe 结果落进模型
    wait_thumbs(kThumbWaitMs);

    // ---- (a) 控件面：徽标（M5-T6 修 1-17/1-18 文案人话）/ 缩放控件组的模式两钮 / 热键提示表
    //   （随 ClassRegistry 生成）----
    QLabel *badge = preview_panel->findChild<QLabel *>(QStringLiteral("pp-preview-badge"));
    QToolButton *zoom_fit =
        preview_panel->findChild<QToolButton *>(QStringLiteral("pp-preview-zoom-fit"));
    QToolButton *zoom_11 =
        preview_panel->findChild<QToolButton *>(QStringLiteral("pp-preview-zoom-1to1"));
    // A17（T11）机械换锚：打标 chip 的控件类型由「键帽 ClipLabel + 名称 ClipLabel」双件改为
    // formkit::ChipButton 单件（§6.1 唯一形态）→ 取值路径随控件换锚（键 = key_text()、
    // 名称 = text()）；chip 表语义（热键 1/2/3 与名称逐字）与断言本体不变。「0 清除」不再是
    // chip（降级为文字链，见下）→ 期望表相应缩短一条（规范强制变化，登记在任务报告）。
    QStringList key_chips;
    QStringList key_names;
    for (QAbstractButton *button :
         preview_panel->findChildren<QAbstractButton *>(QStringLiteral("pp-preview-key"))) {
        const auto *chip = static_cast<const formkit::ChipButton *>(button);
        key_chips << chip->key_text();
        key_names << chip->text();
    }
    // 修 1-20（A17）：清除 = 文字链（QToolButton，无键帽）+ 前置分隔线
    const auto *clear_link =
        preview_panel->findChild<QToolButton *>(QStringLiteral("pp-preview-clear"));
    const auto *clear_sep =
        preview_panel->findChild<QFrame *>(QStringLiteral("pp-preview-key-sep"));
    // −/＋ 两钮的读数 = 挂载的矢量图标名（修 1-19：图标钮无文案，控件面身份由图标名承载）
    const auto icon_key = [](const QToolButton *button) {
        return button != nullptr ? button->property(icons::kIconNameProperty).toString()
                                 : QStringLiteral("<missing>");
    };
    // W-R-T13（A39 ④）机械换锚：「适应 / 1:1」由矢量图标钮恢复为**文字形态**（§8 组件清单逐字
    // 「− / 100% / ＋ / 适应 / 1:1」；v2 .zoombar 的两颗模式钮本就是文字钮）→ 控件面身份随之从
    // 「图标名」换锚成「钮文案 + tooltip」；断言本体（模式钮两枚、非 checkbox、身份可读）不变。
    std::printf("UI-SMOKE preview-controls: badge=\"%s\" zoom-fit=\"%s\" zoom-1:1=\"%s\" "
                "keys=[%s] names=[%s] pos=\"%s\"\n",
                badge != nullptr ? qUtf8Printable(badge->text()) : "<missing>",
                zoom_fit != nullptr ? qUtf8Printable(zoom_fit->text()) : "<missing>",
                zoom_11 != nullptr ? qUtf8Printable(zoom_11->text()) : "<missing>",
                qUtf8Printable(key_chips.join(QLatin1Char(' '))),
                qUtf8Printable(key_names.join(QLatin1Char(' '))),
                qUtf8Printable(preview_panel->current_position_text()));
    std::fflush(stdout);
    if (badge == nullptr || badge->text() != QStringLiteral("原图 · 未应用修改")) {
        smoke_fail(MainWindow::tr("预览徽标不是「原图 · 未应用修改」（%1）")
                       .arg(badge != nullptr ? badge->text() : QStringLiteral("<missing>")));
    }
    // A39 ④：缩放模式两钮 = **文字钮**（「适应」/「1:1」逐字；非 checkbox；tooltip 保留；且不再
    // 挂矢量图标 —— C3 字形禁令针对「用字形充当图标」，这里是规范正文要求的文字形态）。
    if (zoom_fit == nullptr || zoom_11 == nullptr || zoom_fit->text() != QStringLiteral("适应") ||
        zoom_11->text() != QStringLiteral("1:1") ||
        zoom_fit->toolTip() != QStringLiteral("适应舞台") ||
        zoom_11->toolTip() != QStringLiteral("原始像素 (1:1)") || zoom_fit->isCheckable() ||
        zoom_11->isCheckable() ||
        !zoom_fit->property(icons::kIconNameProperty).toString().isEmpty() ||
        !zoom_11->property(icons::kIconNameProperty).toString().isEmpty()) {
        smoke_fail(MainWindow::tr("缩放「适应 | 1:1」模式钮不是文字钮（fit=%1/%2 1:1=%3/%4 "
                                  "checkable=%5/%6）")
                       .arg(zoom_fit != nullptr ? zoom_fit->text() : QStringLiteral("<missing>"),
                            zoom_fit != nullptr ? zoom_fit->toolTip() : QStringLiteral("<missing>"),
                            zoom_11 != nullptr ? zoom_11->text() : QStringLiteral("<missing>"),
                            zoom_11 != nullptr ? zoom_11->toolTip() : QStringLiteral("<missing>"))
                       .arg(zoom_fit != nullptr ? zoom_fit->isCheckable() : -1)
                       .arg(zoom_11 != nullptr ? zoom_11->isCheckable() : -1));
    }
    // A39 ④ 补强（断言只增）：−/＋ 仍是矢量图标钮（C3：图标全 SVG），图标名读出非空
    if (icon_key(preview_panel->findChild<QToolButton *>(QStringLiteral("pp-preview-zoom-out"))) !=
            QString::fromLatin1(icons::key(icons::Name::ZoomOut)) ||
        icon_key(preview_panel->findChild<QToolButton *>(QStringLiteral("pp-preview-zoom-in"))) !=
            QString::fromLatin1(icons::key(icons::Name::ZoomIn))) {
        smoke_fail(MainWindow::tr("缩放 −/＋ 钮不再是矢量图标钮"));
    }
    // 默认模板（§6.2）：精选/待定/废片（热键 1/2/3）—— A17/修 1-20：保留的「0 清除」不再以
    // chip 出现（降级为文字链；热键 '0' 的路由不变，见下）
    const QStringList expect_chips{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
    if (key_chips != expect_chips)
        smoke_fail(MainWindow::tr("热键提示 chip 表不符：%1").arg(key_chips.join(',')));
    if (key_names.size() != 3 || key_names.at(0) != QStringLiteral("精选") ||
        key_names.at(1) != QStringLiteral("待定") || key_names.at(2) != QStringLiteral("废片")) {
        smoke_fail(MainWindow::tr("热键提示名称表不符：%1").arg(key_names.join(',')));
    }
    if (clear_link == nullptr || clear_link->text() != QStringLiteral("清除") ||
        clear_sep == nullptr) {
        smoke_fail(
            MainWindow::tr("「清除」未降级为文字链/未分隔（link=%1 sep=%2）")
                .arg(clear_link != nullptr ? clear_link->text() : QStringLiteral("<missing>"))
                .arg(clear_sep != nullptr ? QStringLiteral("ok") : QStringLiteral("<missing>")));
    }
    // 修 1-20 的空间关系：… [最后一个 chip] [分隔线] [清除文字链]（破坏性动作与打标分列）
    {
        const QList<QAbstractButton *> chip_buttons =
            preview_panel->findChildren<QAbstractButton *>(QStringLiteral("pp-preview-key"));
        const QAbstractButton *last_chip = chip_buttons.isEmpty() ? nullptr : chip_buttons.last();
        const auto cx = [this](const QWidget *widget) {
            return widget != nullptr ? widget->mapTo(preview_panel, QPoint(0, 0)).x() : -1;
        };
        const int chip_right = last_chip != nullptr ? cx(last_chip) + last_chip->width() : -1;
        const int sep_x = cx(clear_sep);
        const int link_x = cx(clear_link);
        if (chip_right < 0 || sep_x <= chip_right || link_x <= sep_x) {
            smoke_fail(MainWindow::tr("「清除」文字链未与打标 chip 分隔"
                                      "（chip-right=%1 sep=%2 link=%3）")
                           .arg(chip_right)
                           .arg(sep_x)
                           .arg(link_x));
        }
    }
    if (preview_panel->current_position_text() != QStringLiteral("1 / %1").arg(model->size()))
        smoke_fail(
            MainWindow::tr("预览位置读数不符：%1").arg(preview_panel->current_position_text()));

    const int original_index = preview_panel->current_index();

    // ---- (b) 翻图：next/previous → 索引与读数跟随；出图在 5s 内落地 ----
    preview_panel->next();
    const int after_next = preview_panel->current_index();
    preview_panel->previous();
    const int after_prev = preview_panel->current_index();
    if (after_next != original_index + 1 || after_prev != original_index)
        smoke_fail(MainWindow::tr("翻图索引不符：%1→%2→%3")
                       .arg(original_index)
                       .arg(after_next)
                       .arg(after_prev));
    // 定位到第二张等出图（同时验证池的异步投递链路）
    preview_panel->next();
    const bool loaded = wait_for([this] { return preview_panel->image_visible(); }, 5000);
    const QSize shown = preview_panel->displayed_size();
    std::printf("UI-SMOKE preview-flip: index=%d pos=\"%s\" name=\"%s\" visible=%d "
                "displayed=%dx%d\n",
                preview_panel->current_index(),
                qUtf8Printable(preview_panel->current_position_text()),
                qUtf8Printable(preview_panel->current_file_name()), loaded ? 1 : 0, shown.width(),
                shown.height());
    std::fflush(stdout);
    if (!loaded)
        smoke_fail(
            MainWindow::tr("翻图后 5s 内未出图（index=%1）").arg(preview_panel->current_index()));
    if (preview_panel->current_info_text().isEmpty())
        smoke_fail(MainWindow::tr("预览信息行为空（尺寸/位深/色彩空间未落地）"));

    // ---- (c) 缩放：1:1 的绘制尺寸 = 源像素尺寸；适应 = 舞台内等比（两者对 64px 小图必须不同）----
    const pp::ImageInfo current_info =
        model->row(static_cast<std::size_t>(preview_panel->current_index())).info;
    preview_panel->set_zoom_fit(false);
    pump(60);
    const QSize at_1to1 = preview_panel->displayed_size();
    preview_panel->set_zoom_fit(true);
    pump(60);
    const QSize at_fit = preview_panel->displayed_size();
    std::printf("UI-SMOKE preview-zoom: 1:1=%dx%d fit=%dx%d (source %dx%d)\n", at_1to1.width(),
                at_1to1.height(), at_fit.width(), at_fit.height(), current_info.width,
                current_info.height);
    std::fflush(stdout);
    if (at_1to1.width() != current_info.width || at_1to1.height() != current_info.height)
        smoke_fail(MainWindow::tr("1:1 不是源像素尺寸：%1x%2 ≠ %3x%4")
                       .arg(at_1to1.width())
                       .arg(at_1to1.height())
                       .arg(current_info.width)
                       .arg(current_info.height));
    if (at_fit == at_1to1)
        smoke_fail(MainWindow::tr("适应与 1:1 绘制尺寸相同（%1x%2）——缩放未生效")
                       .arg(at_fit.width())
                       .arg(at_fit.height()));

    // ---- (d) 热键路由 + 打标接线位：未锁定发出、锁定（G5 分类只读）不发出 ----
    int hits = 0;
    char last_key = 0;
    const QMetaObject::Connection conn =
        connect(preview_panel, &PreviewPanel::class_hotkey, w, [&hits, &last_key](QChar key) {
            ++hits;
            last_key = key.toLatin1();
        });
    const auto send_key = [this](int key, const char *text) {
        QWidget *target =
            view != nullptr ? static_cast<QWidget *>(view) : static_cast<QWidget *>(w);
        target->setFocus();
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString::fromLatin1(text));
        QApplication::sendEvent(target, &press);
    };
    send_key(Qt::Key_1, "1");
    pump(40);
    const int unlocked_hits = hits;
    preview_panel->set_locked(true);
    send_key(Qt::Key_1, "1");
    pump(40);
    const int locked_hits = hits;
    preview_panel->set_locked(false);
    disconnect(conn);
    std::printf("UI-SMOKE preview-hotkey: unlocked=%d locked=%d last=\"%c\"\n", unlocked_hits,
                locked_hits, last_key != 0 ? last_key : '?');
    std::fflush(stdout);
    if (unlocked_hits != 1 || locked_hits != 1 || last_key != '1')
        smoke_fail(MainWindow::tr("热键路由不符：未锁定=%1 锁定=%2 末键=%3（期望 1/1/'1'）")
                       .arg(unlocked_hits)
                       .arg(locked_hits)
                       .arg(QChar(last_key != 0 ? last_key : '?')));

    // ---- (e) 快速翻图不堆积（§6.1）：连翻 8 张 → 池内队列深度恒 ≤1，且最终收敛到末张 ----
    int max_pending = 0;
    for (int i = 0; i < 8 && preview_panel->current_index() + 1 < static_cast<int>(model->size());
         ++i) {
        preview_panel->next();
        pump(2); // 只推进 2ms（远短于一次解码）
        max_pending = std::max(max_pending, preview_panel->pending_requests());
    }
    const int burst_index = preview_panel->current_index();
    const bool burst_loaded = wait_for([this] { return preview_panel->image_visible(); }, 5000);
    std::printf("UI-SMOKE preview-burst: index=%d queued_max=%d pending_now=%d visible=%d\n",
                burst_index, max_pending, preview_panel->pending_requests(), burst_loaded ? 1 : 0);
    std::fflush(stdout);
    if (max_pending > 1)
        smoke_fail(MainWindow::tr("快速翻图堆积：池队列深度 %1 > 1").arg(max_pending));
    if (!burst_loaded || preview_panel->pending_requests() != 0)
        smoke_fail(MainWindow::tr("快速翻图后未收敛：index=%1 pending=%2")
                       .arg(burst_index)
                       .arg(preview_panel->pending_requests()));

    // ---- 归位：截图基准 = 面板显示首个文件（§5.2 跟随口径），且恢复未锁定态 ----
    preview_panel->set_current(original_index);
    pump(120);
}

// ---------------------------------------------------------------------------
// M5-T6（C7）预览画布几何/状态自检（§8 画布/空态 + 修 1-16/1-17/1-18/1-19/1-21/1-22/3-20/3-21）
//   取证面 = "落在屏幕上的实际几何 + 实拍像素"：画布底 #16181d（明暗两档同值）、缩放条钮 28、
//   翻图钮 28、角标空态隐藏、空态矢量图标 + 引导语、照片衬垫与画布底可区分、小图最近邻。
//   令牌读数由 smoke_probe_theme 的 v6 值表承担，两者互补（改了令牌但没落到画布 = 这里红）。
//   空态取样前先把文件集合清空、取样后原样恢复（集合 + 当前项），不扰动后继探针/截图基线。
// ---------------------------------------------------------------------------
void MainWindow::Impl::smoke_probe_preview_geometry() {
    if (preview_panel == nullptr || model->empty()) {
        smoke_fail(MainWindow::tr("预览几何自检：面板或文件集合缺失"));
        return;
    }
    QFrame *stage = w->findChild<QFrame *>(QStringLiteral("pp-preview-stage"));
    QLabel *badge = w->findChild<QLabel *>(QStringLiteral("pp-preview-badge"));
    QWidget *zoombar = w->findChild<QWidget *>(QStringLiteral("pp-preview-zoombar"));
    QToolButton *zoom_out = w->findChild<QToolButton *>(QStringLiteral("pp-preview-zoom-out"));
    QToolButton *zoom_in_btn = w->findChild<QToolButton *>(QStringLiteral("pp-preview-zoom-in"));
    QToolButton *zoom_fit_btn = w->findChild<QToolButton *>(QStringLiteral("pp-preview-zoom-fit"));
    QToolButton *zoom_11_btn = w->findChild<QToolButton *>(QStringLiteral("pp-preview-zoom-1to1"));
    QLabel *zoom_level = w->findChild<QLabel *>(QStringLiteral("pp-preview-zoom-level"));
    QLabel *hint = w->findChild<QLabel *>(QStringLiteral("pp-preview-hint"));
    QWidget *empty_box = w->findChild<QWidget *>(QStringLiteral("pp-preview-empty"));
    QLabel *empty_icon = w->findChild<QLabel *>(QStringLiteral("pp-preview-empty-icon"));
    QLabel *empty_text = w->findChild<QLabel *>(QStringLiteral("pp-preview-empty-text"));
    QToolButton *nav_prev = w->findChild<QToolButton *>(QStringLiteral("pp-preview-prev"));
    QToolButton *nav_next = w->findChild<QToolButton *>(QStringLiteral("pp-preview-next"));
    // 热键 chip（修 1-22 的禁用面：chip 自身 isEnabled 跟随其容器）
    // A17（T11）机械换锚：chip 的控件类型 ClipLabel → formkit::ChipButton（QAbstractButton 子类，
    // 无 Q_OBJECT → 按 objectName 取 + 静态下转，与页面探针同一口径）；禁用面语义不变。
    auto *key_chip = static_cast<formkit::ChipButton *>(
        w->findChild<QAbstractButton *>(QStringLiteral("pp-preview-key")));
    if (stage == nullptr || badge == nullptr || zoombar == nullptr || zoom_out == nullptr ||
        zoom_in_btn == nullptr || zoom_fit_btn == nullptr || zoom_11_btn == nullptr ||
        zoom_level == nullptr || hint == nullptr || empty_box == nullptr || empty_icon == nullptr ||
        empty_text == nullptr || nav_prev == nullptr || nav_next == nullptr ||
        key_chip == nullptr) {
        smoke_fail(MainWindow::tr("预览画布构件缺失（舞台/角标/缩放条/提示/空态/翻图钮）"));
        return;
    }
    if (!wait_for([this] { return preview_panel->image_visible(); }, 5000)) {
        smoke_fail(MainWindow::tr("预览几何自检：当前项 5s 内未出图"));
        return;
    }
    // 实拍取样：逻辑坐标 → 抓图像素（dpr 换算与 filelist-geom 同一口径）
    const auto sample = [&stage](const QPoint &logical) {
        const QImage shot = stage->grab().toImage();
        const double scale =
            stage->width() > 0 ? double(shot.width()) / double(stage->width()) : 1.0;
        const int x = std::clamp(int(std::lround(logical.x() * scale)), 0, shot.width() - 1);
        const int y = std::clamp(int(std::lround(logical.y() * scale)), 0, shot.height() - 1);
        return shot.pixelColor(x, y);
    };
    const auto near = [](const QColor &a, const QColor &b, int tol) {
        return std::abs(a.red() - b.red()) <= tol && std::abs(a.green() - b.green()) <= tol &&
               std::abs(a.blue() - b.blue()) <= tol;
    };
    const QColor stage_token(0x16, 0x18, 0x1d); // §4.1 画布底 #16181d（不随主题变）
    int checked = 0;
    QStringList bad;
    const auto expect = [&checked, &bad](const char *name, bool ok, const QString &detail) {
        ++checked;
        if (!ok)
            bad << QStringLiteral("%1(%2)").arg(QString::fromLatin1(name), detail);
    };

    // ---- (a) 有图态：角标（修 1-17/1-18）/ 缩放控件组（修 1-19）/ 提示（§8 逐字）/ 翻图钮（修
    // 1-21）----
    const QSize nav_size(nav_prev->width(), nav_prev->height());
    const QSize zoom_btn_size(zoom_out->width(), zoom_out->height());
    const int zoom_buttons = int(zoombar->findChildren<QToolButton *>().size());
    // W-R-T13（A39 ④）机械换锚：断言本体（单一控件组 4 钮 / 非 checkbox / tooltip 逐字 / 高度
    // 同档）不变；「适应 / 1:1」的尺寸判据从「28×28 图标钮」换锚为「**文字形态**：高 28 + 宽随
    // 文本（放得下、≥ 图标钮档）」，并补文字逐字断言（断言只增）。−/＋ 仍钉 28×28。
    // 文字钮的形态判据 = 「文案放得下」（宽随文本 + 左右内距 v2 .zoombar 的 width:auto;padding:0
    // 6px），**不锚** 28 —— 「1:1」这类窄文案在真实字体下比图标钮窄是合法的（实测 22px）；
    // 与图标钮的分野由 icon() 空/非空判（形态本体），高度仍同为 28（同一条带不撑高）。
    const auto fits_text = [](const QToolButton *button) {
        if (button == nullptr)
            return false;
        const QFontMetrics fm(button->font());
        return button->width() >= fm.horizontalAdvance(button->text()) + 8;
    };
    const bool zoom_group_ok =
        zoom_btn_size == QSize(theme::Metrics::icon_btn, theme::Metrics::icon_btn) &&
        zoom_in_btn->size() == zoom_btn_size && zoom_buttons == 4 && !zoom_out->isCheckable() &&
        !zoom_in_btn->isCheckable() && !zoom_fit_btn->isCheckable() &&
        !zoom_11_btn->isCheckable() && zoom_out->toolTip() == QStringLiteral("缩小") &&
        zoom_in_btn->toolTip() == QStringLiteral("放大") &&
        zoom_fit_btn->toolTip() == QStringLiteral("适应舞台") &&
        zoom_11_btn->toolTip() == QStringLiteral("原始像素 (1:1)") &&
        zoom_fit_btn->text() == QStringLiteral("适应") &&
        zoom_11_btn->text() == QStringLiteral("1:1") &&
        zoom_fit_btn->height() == theme::Metrics::icon_btn &&
        zoom_11_btn->height() == theme::Metrics::icon_btn && fits_text(zoom_fit_btn) &&
        fits_text(zoom_11_btn) && zoom_fit_btn->icon().isNull() && zoom_11_btn->icon().isNull() &&
        !zoom_out->icon().isNull() && !zoom_in_btn->icon().isNull();
    expect("zoom-group", zoombar->isVisible() && zoom_group_ok,
           QStringLiteral("bar=%1 %2x%2 fit=%3x%4 act=%5x%6 n=%7")
               .arg(zoombar->isVisible() ? 1 : 0)
               .arg(zoom_btn_size.width())
               .arg(zoom_fit_btn->width())
               .arg(zoom_fit_btn->height())
               .arg(zoom_11_btn->width())
               .arg(zoom_11_btn->height())
               .arg(zoom_buttons));
    expect("nav-btn",
           nav_size == QSize(theme::Metrics::icon_btn, theme::Metrics::icon_btn) &&
               nav_next->size() == nav_size,
           QStringLiteral("%1x%2").arg(nav_size.width()).arg(nav_size.height()));
    expect("badge", badge->isVisible() && badge->text() == QStringLiteral("原图 · 未应用修改"),
           QStringLiteral("vis=%1 text=\"%2\"").arg(badge->isVisible() ? 1 : 0).arg(badge->text()));
    // 角标笔触：半透明底（实拍 ≠ 画布底）+ 1px 描边（活动 QSS 里成文）
    {
        const QColor badge_px = sample(QPoint(14, 14));
        const bool translucent = !near(badge_px, stage_token, 3);
        const QString sheet = preview_panel->styleSheet();
        const int badge_rule =
            static_cast<int>(sheet.indexOf(QStringLiteral("QLabel#pp-preview-badge")));
        const bool bordered =
            badge_rule >= 0 &&
            sheet.mid(badge_rule, 160).contains(QStringLiteral("border: 1px solid"));
        expect("badge-paint", translucent && bordered,
               QStringLiteral("px=%1 border=%2")
                   .arg(badge_px.name(QColor::HexRgb))
                   .arg(bordered ? 1 : 0));
    }
    expect("hint",
           hint->isVisible() && hint->text() == QStringLiteral("拖动平移 · 滚轮缩放 · 双击适应"),
           QStringLiteral("vis=%1 text=\"%2\"").arg(hint->isVisible() ? 1 : 0).arg(hint->text()));
    // 比例读数 = 面板的有效比例（适应档 = 实测；1:1/比例档 = 100%/档值）
    expect("level", zoom_level->text() == QStringLiteral("%1%").arg(preview_panel->zoom_percent()),
           QStringLiteral("\"%1\"(面板=%2)")
               .arg(zoom_level->text())
               .arg(preview_panel->zoom_percent()));

    // ---- (b) 修 3-21：照片衬垫与画布底可区分（letterbox 带 / 画布底 两点实拍）----
    const QSize shot_size = preview_panel->displayed_size();
    const int stage_w = stage->width();
    const int stage_h = stage->height();
    const int image_top = (stage_h - shot_size.height()) / 2;
    const int mat_top = image_top - 6; // kPhotoMatPad = 6（ui/preview_panel.cpp 单源）
    QColor band_px;                    // letterbox 带上的实拍（= 画布底；与下方的衬垫色对照）
    QColor mat_px;                     // 照片衬垫实拍（修 3-21 的取证点）
    if (mat_top > 18 && shot_size.width() == stage_w) {
        band_px = sample(QPoint(stage_w / 2, std::max(2, mat_top - 12)));
        mat_px = sample(QPoint(stage_w / 2, mat_top + 3));
        expect("letterbox", near(band_px, stage_token, 3) && !near(mat_px, band_px, 3),
               QStringLiteral("band=%1 mat=%2")
                   .arg(band_px.name(QColor::HexRgb), mat_px.name(QColor::HexRgb)));
    } else {
        expect("letterbox", false,
               QStringLiteral("no-band shot=%1x%2 stage=%3x%4")
                   .arg(shot_size.width())
                   .arg(shot_size.height())
                   .arg(stage_w)
                   .arg(stage_h));
    }

    // ---- (c) 修 3-20：小图（64px 源）放大档最近邻、缩小档平滑 ----
    const pp::ImageInfo info =
        model->row(static_cast<std::size_t>(preview_panel->current_index())).info;
    preview_panel->set_zoom_fit(true);
    pump(40);
    const bool nn_fit = preview_panel->nearest_neighbor();
    const int fit_percent = preview_panel->zoom_percent(); // 适应档实测比例（截图基线档）
    preview_panel->set_zoom_percent(25);                   // 16px < 源 64px → 缩小档（平滑）
    pump(40);
    const bool nn_small = preview_panel->nearest_neighbor();
    const bool small_size_ok =
        preview_panel->zoom_percent() == 25 &&
        preview_panel->displayed_size() == QSize(info.width / 4, info.height / 4) &&
        zoom_level->text() == QStringLiteral("25%");
    // 缩放钮实点（修 1-19 的控件语义落地）：＋ 步进（比例上走）、适应钮回到适应档
    zoom_in_btn->click();
    pump(20);
    const int up_percent = preview_panel->zoom_percent();
    zoom_fit_btn->click();
    pump(20);
    const bool click_ok = up_percent > 25 && preview_panel->zoom_fit() &&
                          preview_panel->zoom_percent() == fit_percent;
    // 修 3-20：放大档（适应/1:1）最近邻 = 1，缩小档（25% < 源）平滑 = 0
    expect("nearest", nn_fit && !nn_small && small_size_ok && click_ok,
           QStringLiteral("fit=%1 small=%2 size=%3 click=%4 up=%5 fit=%6")
               .arg(nn_fit ? 1 : 0)
               .arg(nn_small ? 1 : 0)
               .arg(small_size_ok ? 1 : 0)
               .arg(click_ok ? 1 : 0)
               .arg(up_percent)
               .arg(preview_panel->zoom_fit() ? 1 : 0));

    // ---- (c2) K5「拖动平移」：放大档超出舞台 → 左键拖动 → 画面真的位移（grab 实拍对比）----
    {
        preview_panel->set_zoom_percent(800); // 64px × 8 = 512 > 舞台宽 → 可平移
        pump(40);
        const QImage before = stage->grab().toImage();
        const QPoint start(stage->width() / 2, stage->height() / 2);
        const QPoint end = start + QPoint(20, 0); // 夹取范围（±23）内 → 位移可分辨
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(start), stage->mapToGlobal(start),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(stage, &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(end), stage->mapToGlobal(end), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(stage, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(end), stage->mapToGlobal(end),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(stage, &release);
        pump(40);
        const QImage after = stage->grab().toImage();
        expect("drag", before != after, QStringLiteral("moved=%1").arg(before != after ? 1 : 0));
        preview_panel->set_zoom_fit(true); // 归位（截图基线 = 适应档）
        pump(40);
    }

    // ---- (d) 空态（§8 + 修 1-16/1-17/1-22）：清空集合 → 取样 → 原样恢复 ----
    const int saved_index = preview_panel->current_index();
    const QStringList saved_files = cached_paths;
    preview_panel->set_files(QStringList());
    pump(80);
    const QString empty_text_seen = empty_text->text(); // 打印用（恢复后会被还原成占位空串）
    const bool empty_structure =
        empty_box->isVisible() && !badge->isVisible() && !zoombar->isVisible() &&
        !hint->isVisible() && !empty_icon->pixmap().isNull() &&
        empty_text->text() == QStringLiteral("将照片拖到此处，或点击 ＋ 添加") &&
        !nav_prev->isEnabled() && !nav_next->isEnabled() && !key_chip->isEnabled();
    expect("empty", empty_structure,
           QStringLiteral("box=%1 badge=%2 bar=%3 hint=%4 icon=%5 text=\"%6\" nav=%7/%8 chips=%9")
               .arg(empty_box->isVisible() ? 1 : 0)
               .arg(badge->isVisible() ? 1 : 0)
               .arg(zoombar->isVisible() ? 1 : 0)
               .arg(hint->isVisible() ? 1 : 0)
               .arg(empty_icon->pixmap().isNull() ? 0 : 1)
               .arg(empty_text->text())
               .arg(nav_prev->isEnabled() ? 1 : 0)
               .arg(nav_next->isEnabled() ? 1 : 0)
               .arg(key_chip->isEnabled() ? 1 : 0));
    // 画布底 = §4.1 #16181d，且**明暗两档同值**（§9「画布底不变」）
    const QColor canvas_dark = [&] {
        set_theme_mode(theme::ThemeMode::Dark);
        pump(40);
        return sample(QPoint(12, 12));
    }();
    const QColor canvas_light = [&] {
        set_theme_mode(theme::ThemeMode::Light);
        pump(40);
        return sample(QPoint(12, 12));
    }();
    set_theme_mode(preferred_theme_mode());
    pump(40);
    expect("canvas",
           near(canvas_dark, stage_token, 2) && near(canvas_light, stage_token, 2) &&
               theme::tokens(theme::ThemeMode::Dark).stage_bg == stage_token &&
               theme::tokens(theme::ThemeMode::Light).stage_bg == stage_token,
           QStringLiteral("dark=%1 light=%2")
               .arg(canvas_dark.name(QColor::HexRgb), canvas_light.name(QColor::HexRgb)));
    // 恢复（集合 + 当前项；LRU 命中 → 出图瞬时）
    preview_panel->set_files(saved_files);
    preview_panel->set_current(saved_index);
    pump(80);
    const bool restored = wait_for([this] { return preview_panel->image_visible(); }, 5000) &&
                          !empty_box->isVisible() && badge->isVisible() && zoombar->isVisible() &&
                          key_chip->isEnabled() && nav_prev->isEnabled() == (saved_index > 0);
    expect("restore", restored,
           QStringLiteral("index=%1/%2 visible=%3 chips=%4")
               .arg(preview_panel->current_index())
               .arg(saved_index)
               .arg(preview_panel->image_visible() ? 1 : 0)
               .arg(key_chip->isEnabled() ? 1 : 0));

    std::printf("UI-SMOKE preview-geom: checked=%d bad=%d canvas=%s(light=%s) band/mat=%s/%s "
                "zoom-btn=%dx%d n=%d nav=%dx%d level=\"%s\" badge(empty-hidden)=%d hint=%d "
                "empty(icon=%d text=\"%s\") chip-off=%d nn=%d/%d footer=%s\n",
                checked, int(bad.size()), qUtf8Printable(canvas_dark.name(QColor::HexRgb)),
                qUtf8Printable(canvas_light.name(QColor::HexRgb)),
                qUtf8Printable(band_px.name(QColor::HexRgb)),
                qUtf8Printable(mat_px.name(QColor::HexRgb)), zoom_btn_size.width(),
                zoom_btn_size.height(), zoom_buttons, nav_size.width(), nav_size.height(),
                qUtf8Printable(zoom_level->text()), badge->isVisible() ? 1 : 0,
                hint->isVisible() ? 1 : 0, empty_icon->pixmap().isNull() ? 0 : 1,
                qUtf8Printable(empty_text_seen), key_chip->isEnabled() ? 1 : 0,
                preview_panel->nearest_neighbor() ? 1 : 0, int(info.width),
                qUtf8Printable(bad.join(QLatin1Char(' '))));
    std::fflush(stdout);
    if (!bad.isEmpty())
        smoke_fail(MainWindow::tr("预览画布几何/状态不符（§8 + 修 1-16/1-17/1-19/1-21/1-22/"
                                  "3-20/3-21）：%1")
                       .arg(bad.join(QLatin1Char(' '))));
}

// ---------------------------------------------------------------------------
// M4-T11 文件列表自检（§3.6 roles + §6.2 勾选/正交搜索/自动分组节头）
//   自动化部分：roles 可读性与可勾选标志、真实鼠标点击勾选（自绘命中框）、内容列不误触勾选、
//   搜索过滤与勾选正交（过滤不丢勾选/不丢计数）、分组节 = 组名+计数（三态 + 无分组，节内计数
//   合计 = 可见行数，节内行序稳定）、节头跨列与展开态。
//   手测部分（W5 走查清单）：拖拽分栏后左栏下限、节头折叠手感、万级文件的滚动流畅度。
// ---------------------------------------------------------------------------

void MainWindow::Impl::smoke_probe_filelist() {
    if (model->empty()) {
        smoke_fail(MainWindow::tr("文件列表自检：列表为空"));
        return;
    }
    wait_thumbs(kThumbWaitMs);
    if (!wait_for([this] { return model->group_scan_idle(); }, 15000))
        smoke_fail(MainWindow::tr("分组摘要扫描未在 15s 内收敛"));

    // ---- (a) §3.6 追加 roles：CheckState（Qt 标准）/ ClassId / GroupKey ----
    const QModelIndex src0 = model->index(0, 0);
    const bool checkable = (model->flags(src0) & Qt::ItemIsUserCheckable) != 0;
    const QVariant check_state = src0.data(Qt::CheckStateRole);
    const QVariant class_id = src0.data(FileListModel::ClassIdRole);
    const QVariant group_key = src0.data(FileListModel::GroupKeyRole);
    std::printf("UI-SMOKE filelist-roles: checkable=%d check_state=%d class_id=\"%s\" "
                "group_key=\"%s\" scanned=%d\n",
                checkable ? 1 : 0, check_state.toInt(), qUtf8Printable(class_id.toString()),
                qUtf8Printable(group_key.toString()), model->group_source_scanned(0) ? 1 : 0);
    std::fflush(stdout);
    if (!checkable)
        smoke_fail(MainWindow::tr("文件行未置 Qt::ItemIsUserCheckable（勾选无入口）"));
    if (!check_state.isValid() || check_state.toInt() != int(Qt::Checked))
        smoke_fail(MainWindow::tr("默认勾选不是 Qt::Checked（实为 %1）").arg(check_state.toInt()));
    if (!class_id.isValid() || !group_key.isValid())
        smoke_fail(MainWindow::tr("ClassIdRole / GroupKeyRole 不可读"));
    for (std::size_t i = 0; i < model->size(); ++i) {
        if (!model->group_source_scanned(i)) {
            smoke_fail(MainWindow::tr("第 %1 行的分组摘要未回填").arg(static_cast<int>(i)));
            break;
        }
    }

    // ---- (b) 勾选：真实鼠标点击（列 0 自绘命中框）→ 取消/恢复；内容列不误触 ----
    const int checked_all = int(model->checked_count());
    if (checked_all != int(model->size()))
        smoke_fail(MainWindow::tr("默认并非全勾：%1 / %2").arg(checked_all).arg(model->size()));
    const auto click_at = [this](const QPoint &pos) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos),
                          view->viewport()->mapToGlobal(pos), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos),
                            view->viewport()->mapToGlobal(pos), Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &release);
        pump(30);
    };
    const QModelIndex group0 = groups->group_index(0);
    if (!group0.isValid()) {
        smoke_fail(MainWindow::tr("分组节缺失（groups->group_index(0) 无效）"));
        return;
    }
    const QModelIndex first_file = groups->index(0, FileGroupProxyModel::kCheckColumn, group0);
    if (!first_file.isValid()) {
        smoke_fail(MainWindow::tr("首文件行缺失"));
        return;
    }
    view->scrollTo(first_file);
    pump(40);
    const QRect check_hit = FileGroupDelegate::check_rect(view->visualRect(first_file));
    const bool hit_ok = view->indexAt(check_hit.center()) == first_file;
    click_at(check_hit.center());
    const bool after_click = model->is_checked(0);
    const int checked_after_click = int(model->checked_count());
    click_at(check_hit.center()); // 恢复
    // 内容列（文件名/缩略图区域）点击不得改勾选：命中框 = 列 1 左沿 + 40px
    const QModelIndex first_content = groups->index(0, FileGroupProxyModel::kRowColumn, group0);
    const QRect content_rect = view->visualRect(first_content);
    const QPoint content_pos(content_rect.left() + 40, content_rect.center().y());
    const bool content_hits = view->indexAt(content_pos) == first_content;
    const bool checked_before_content = model->is_checked(0);
    click_at(content_pos);
    const bool checked_after_content = model->is_checked(0);
    std::printf("UI-SMOKE filelist-check: hit=%d after_click=%d checked=%d/%d restore=%d "
                "content_hit=%d content_toggle=%d\n",
                hit_ok ? 1 : 0, after_click ? 1 : 0, checked_after_click, int(model->size()),
                model->is_checked(0) ? 1 : 0, content_hits ? 1 : 0,
                checked_before_content != checked_after_content ? 1 : 0);
    std::fflush(stdout);
    if (!hit_ok)
        smoke_fail(MainWindow::tr("勾选框命中失败（visualRect 中心不是该行）"));
    if (after_click)
        smoke_fail(MainWindow::tr("点击勾选框未取消勾选"));
    if (checked_after_click != checked_all - 1)
        smoke_fail(MainWindow::tr("勾选计数不符：%1（期望 %2）")
                       .arg(checked_after_click)
                       .arg(checked_all - 1));
    if (!model->is_checked(0))
        smoke_fail(MainWindow::tr("二次点击未恢复勾选"));
    if (content_hits && checked_before_content != checked_after_content)
        smoke_fail(MainWindow::tr("内容列点击误触了勾选"));

    // ---- (c) 搜索正交（§6.2/D2）：过滤只改可见性，勾选/计数/键都不丢 ----
    const std::size_t probe_row = model->size() > 1 ? model->size() / 2 : 0;
    const QString probe_name =
        QString::fromStdString(model->row(probe_row).entry.src.filename().string());
    const QString filter_text =
        probe_name.left(std::max(1, int(probe_name.size()) - 4)); // 名称前缀（至少 1 字符）
    const int checked_before_filter = int(model->checked_count());
    model->set_checked(probe_row, false); // 该行取消勾选
    const int checked_with_unchecked = int(model->checked_count());
    search->setText(filter_text);
    pump(60);
    const int filtered_rows = proxy->rowCount();
    const int checked_while_filtered = int(model->checked_count());
    const bool key_preserved = !model->is_checked(probe_row) &&
                               model->group_key_of(probe_row) ==
                                   model->group_key_of(probe_row); // 键仍可读（不随过滤变化）
    search->clear();
    pump(60);
    const int rows_restored = proxy->rowCount();
    const bool check_survived = !model->is_checked(probe_row);
    model->set_checked(probe_row, true);
    std::printf("UI-SMOKE filelist-search: filter=\"%s\" visible=%d/%d checked %d→%d→%d "
                "restored_rows=%d key_kept=%d check_survived=%d\n",
                qUtf8Printable(filter_text), filtered_rows, int(model->size()),
                checked_before_filter, checked_with_unchecked, checked_while_filtered,
                rows_restored, key_preserved ? 1 : 0, check_survived ? 1 : 0);
    std::fflush(stdout);
    if (filtered_rows >= int(model->size()))
        smoke_fail(MainWindow::tr("搜索未过滤任何行（filter=\"%1\" → %2 / %3）")
                       .arg(filter_text)
                       .arg(filtered_rows)
                       .arg(model->size()));
    if (checked_while_filtered != checked_with_unchecked)
        smoke_fail(MainWindow::tr("过滤改变了勾选计数：%1 ≠ %2")
                       .arg(checked_while_filtered)
                       .arg(checked_with_unchecked));
    if (!check_survived)
        smoke_fail(MainWindow::tr("过滤丢失了勾选状态（§6.2 正交性）"));
    if (rows_restored != int(model->size()))
        smoke_fail(
            MainWindow::tr("清空搜索后行数未复原：%1 ≠ %2").arg(rows_restored).arg(model->size()));

    // ---- (d) 分组节 = 组名 + 计数（§6.2）；三态 + 「无分组」----
    const auto dump_headers = [this](const char *mode_name) {
        QStringList headers;
        for (int g = 0; g < groups->group_count(); ++g)
            headers << groups->header_text_at(g);
        std::printf("UI-SMOKE filelist-group-%s: groups=%d [%s]\n", mode_name,
                    groups->group_count(), qUtf8Printable(headers.join(QStringLiteral(" | "))));
        std::fflush(stdout);
    };
    const auto check_structure = [this](const char *mode_name) -> bool {
        // 节数/节内行数 = 由模型的 GroupKeyRole 直接推导（代理分组 == 模型键）
        const int visible = proxy->rowCount();
        int sum = 0;
        for (int g = 0; g < groups->group_count(); ++g) {
            const int size = groups->group_size_at(g);
            sum += size;
            const QString label = groups->group_label_at(g);
            if (label.isEmpty() ||
                groups->header_text_at(g) != FileGroupProxyModel::header_text(label, size)) {
                smoke_fail(MainWindow::tr("节头文案不符（%1 的第 %2 节）")
                               .arg(QString::fromLatin1(mode_name))
                               .arg(g));
                return false;
            }
            if (size <= 0) {
                smoke_fail(MainWindow::tr("空节未被折叠（%1 第 %2 节）")
                               .arg(QString::fromLatin1(mode_name))
                               .arg(g));
                return false;
            }
        }
        if (sum != visible) {
            smoke_fail(MainWindow::tr("节内计数合计 %1 ≠ 可见行数 %2（%3）")
                           .arg(sum)
                           .arg(visible)
                           .arg(QString::fromLatin1(mode_name)));
            return false;
        }
        return true;
    };
    // 视图侧状态（跨列节头/展开态）在模型 reset 后**延迟一轮**落地（见 apply_group_view_state）
    // → 每次切模式后 pump 一次再断言
    set_group_mode(pp::GroupMode::SourceFormat);
    pump(40);
    dump_headers("format");
    check_structure("format");
    set_group_mode(pp::GroupMode::Camera);
    pump(40);
    dump_headers("camera");
    check_structure("camera");
    set_group_mode(pp::GroupMode::None);
    pump(40);
    dump_headers("none");
    check_structure("none");
    if (groups->group_count() != 1 ||
        groups->group_label_at(0) !=
            QString::fromUtf8(pp::kNoGroupLabel.data(), int(pp::kNoGroupLabel.size()))) {
        smoke_fail(
            MainWindow::tr("「无分组」模式不是单节「%1」")
                .arg(QString::fromUtf8(pp::kNoGroupLabel.data(), int(pp::kNoGroupLabel.size()))));
    }
    // 月份模式：标签形态 "YYYY 年 M 月"（mockup .ghead）；键形态由 core/classify 保证
    set_group_mode(pp::GroupMode::Month);
    pump(60);
    dump_headers("month");
    check_structure("month");
    for (int g = 0; g < groups->group_count(); ++g) {
        const QString key = groups->group_key_at(g);
        const QString label = groups->group_label_at(g);
        if (key.isEmpty())
            continue; // 「无分组」
        if (!(key.size() == 7 && key.at(4) == QLatin1Char('-')) ||
            !label.endsWith(QStringLiteral("月"))) {
            smoke_fail(
                MainWindow::tr("月份节键/节名形态不符：key=\"%1\" label=\"%2\"").arg(key, label));
            break;
        }
    }
    // 节头跨列（mockup .ghead 通栏）+ 展开态
    const QModelIndex g0 = groups->group_index(0);
    const QRect header_rect = view->visualRect(g0);
    std::printf("UI-SMOKE filelist-span: header=%dx%d viewport=%dx%d expanded=%d\n",
                header_rect.width(), header_rect.height(), view->viewport()->width(),
                view->viewport()->height(), view->isExpanded(g0) ? 1 : 0);
    {
        const QWidget *panel_w = view->parentWidget() != nullptr ? view->parentWidget() : view;
        const QWidget *card_w =
            panel_w->parentWidget() != nullptr ? panel_w->parentWidget() : panel_w;
        std::printf("UI-SMOKE filelist-geometry: view=%dx%d panel=%dx%d card=%dx%d col=%dx%d "
                    "colmin=%dx%d tree_min=%dx%d\n",
                    view->width(), view->height(), panel_w->width(), panel_w->height(),
                    card_w->width(), card_w->height(), left_col->width(), left_col->height(),
                    left_col->minimumWidth(), left_col->minimumHeight(), view->minimumWidth(),
                    view->minimumHeight());
    }
    std::fflush(stdout);
    if (!header_rect.isValid() || header_rect.width() < view->viewport()->width() * 9 / 10)
        smoke_fail(MainWindow::tr("节头未跨两列：%1 < 视口 %2")
                       .arg(header_rect.width())
                       .arg(view->viewport()->width()));
    if (!view->isExpanded(g0))
        smoke_fail(MainWindow::tr("分组节默认未展开"));
    if (view->isExpanded(g0)) { // 折叠记忆：用户折叠 → 重建 → 仍折叠（走真实信号路径）
        view->collapse(g0);
        pump(40);
        const bool collapsed_recorded = groups->is_collapsed(groups->group_key_at(0));
        groups->rebuild();
        pump(60);
        const bool still_collapsed = !view->isExpanded(groups->group_index(0)) &&
                                     groups->is_collapsed(groups->group_key_at(0));
        std::printf("UI-SMOKE filelist-collapse: key=\"%s\" recorded=%d expanded_after=%d "
                    "collapsed_flag=%d rebuilds=%d\n",
                    qUtf8Printable(groups->group_key_at(0)), collapsed_recorded ? 1 : 0,
                    view->isExpanded(groups->group_index(0)) ? 1 : 0,
                    groups->is_collapsed(groups->group_key_at(0)) ? 1 : 0, groups->rebuilds());
        std::fflush(stdout);
        if (!collapsed_recorded)
            smoke_fail(MainWindow::tr("用户折叠未被记为折叠态（§6.2 折叠记忆）"));
        if (!still_collapsed)
            smoke_fail(MainWindow::tr("折叠态未跨重建保持（§6.2 过滤/摘要变化不丢展开态）"));
        groups->set_collapsed(groups->group_key_at(0), false);
        groups->rebuild();
        pump(40);
    }

    // ---- 归位：截图基准 = 全部勾选 + 按月份分组 + 首个文件 + 列表回顶（mockup 左栏口径）----
    model->set_all_checked(true);
    set_group_mode(pp::GroupMode::Month);
    if (search->text() != QString())
        search->clear();
    if (proxy->filtering())
        proxy->clear_filters(); // M5-T5 筛选归位（避免污染后续探针/截图）
    if (view->selectionModel() != nullptr)
        view->selectionModel()->clearSelection();
    view->setCurrentIndex(QModelIndex()); // 当前项也会触发 scrollTo（会把节头滚出视口）→ 一并清掉
    view->scrollToTop();
    preview_panel->set_current(0);
    pump(150);
}

// ---------------------------------------------------------------------------
// M5-T5（C7）文件行/工具行几何值表 + 空态投放区 + 选中面像素取证（§7.5 / 修 3-17/1-06/1-07/1-08）
//   口径 = "落在屏幕上的实际几何"（视图里的行矩形 / 行内几何单源函数 / 控件尺寸 / 视口像素取样），
//   不是令牌读数 —— 令牌读数由 smoke_probe_theme 的 v6 值表承担，两者互补（改了令牌但没落到
//   布局/绘制 = 这里红）。行内几何与绘制共用 FileDelegate::row_rects/join_items（同一实现）。
// ---------------------------------------------------------------------------
void MainWindow::Impl::smoke_probe_filelist_geometry() {
    if (model->empty() || groups->group_count() == 0 || view->itemDelegate() == nullptr) {
        smoke_fail(MainWindow::tr("文件行几何自检：列表/委托缺失"));
        return;
    }
    wait_thumbs(kThumbWaitMs);
    const QModelIndex group0 = groups->group_index(0);
    const QModelIndex row_idx = groups->index(0, FileGroupProxyModel::kRowColumn, group0);
    const QModelIndex check_idx = groups->index(0, FileGroupProxyModel::kCheckColumn, group0);
    if (!row_idx.isValid() || !check_idx.isValid()) {
        smoke_fail(MainWindow::tr("文件行几何自检：首文件行缺失"));
        return;
    }
    const theme::Tokens &t = tokens;

    // ---- (a) 行内几何（单源函数 = 绘制的同一实现）----
    const QRect row_rect = view->visualRect(row_idx); // 屏幕上的实际行矩形
    QStyleOptionViewItem opt;
    opt.initFrom(view);
    opt.rect = QRect(0, 0, std::max(1, view->viewport()->width()), FileDelegate::kFileRowHeight);
    const QSize row_hint = view->itemDelegate()->sizeHint(opt, row_idx);
    const QSize check_hint = view->itemDelegate()->sizeHint(opt, check_idx);
    const QString state_text = row_idx.data(FileListModel::StateTextRole).toString();
    const QFontMetrics pill_fm(
        theme::font(theme::Typography::badge_px, theme::Typography::badge_weight));
    const int pill_w = theme::Metrics::pill_pad_x * 2 + pill_fm.horizontalAdvance(state_text);
    const bool has_exception = row_idx.data(FileListModel::ExceptionRole).toBool();
    const FileDelegate::RowRects rects = FileDelegate::row_rects(row_rect, pill_w, has_exception);
    const QRect check_box = FileGroupDelegate::check_rect(row_rect);
    const auto px_of = [](const QFont &f) {
        return int(std::lround(f.pointSizeF() * 96.0 / 72.0)); // pt → px（theme::px_to_pt 的逆）
    };
    struct Expect {
        const char *name;
        int got;
        int want;
    };
    const Expect table[] = {
        // §7.5 行高 58 / R4 缩略图 46 / R4 复选框 13（A6：单一来源）
        {"row_h", row_rect.height(), FileDelegate::kFileRowHeight},
        {"row_hint", row_hint.height(), FileDelegate::kFileRowHeight},
        {"check_hint", check_hint.height(), FileDelegate::kFileRowHeight},
        {"thumb", rects.thumb.width(), theme::Metrics::thumb_px},
        {"thumb_h", rects.thumb.height(), theme::Metrics::thumb_px},
        {"check", check_box.width(), theme::Metrics::checkbox_px},
        {"check_col", view->columnWidth(FileGroupProxyModel::kCheckColumn),
         FileGroupDelegate::check_column_width()},
        // R4 胶囊 16 / 工具行按钮（宽 28 / 高 26 —— A7）
        {"pill_h", rects.pill.height(), theme::Metrics::pill_h},
        {"btn_w", add_btn != nullptr ? add_btn->width() : -1, theme::Metrics::icon_btn},
        {"btn_h", add_btn != nullptr ? add_btn->height() : -1, theme::Metrics::btn_h},
        {"remove_h", remove_sel != nullptr ? remove_sel->height() : -1, theme::Metrics::btn_h},
        {"filter_h", filter_btn != nullptr ? filter_btn->height() : -1, theme::Metrics::btn_h},
        {"filter_w", filter_btn != nullptr ? filter_btn->width() : -1, theme::Metrics::icon_btn},
        {"search_h", search != nullptr ? search->height() : -1, theme::Metrics::input_h},
        {"group_h", group_mode != nullptr ? group_mode->height() : -1, theme::Metrics::combo_h},
        // §4.2/§7.5 字阶：文件名 12、信息行 10（裁定 A3）
        {"name_px", px_of(FileDelegate::name_font()),
         int(std::lround(theme::Typography::card_title_px))},
        {"info_px", px_of(FileDelegate::info_font()), int(std::lround(theme::Typography::hint_px))},
    };
    const int count = int(sizeof(table) / sizeof(table[0]));
    QStringList bad;
    for (const Expect &e : table) {
        if (e.got != e.want)
            bad << QStringLiteral("%1=%2(期望 %3)")
                       .arg(QString::fromLatin1(e.name))
                       .arg(e.got)
                       .arg(e.want);
    }

    // ---- (b) 工具行常驻（修 1-07/1-08）：五件可达；无选择时移除钮**隐藏**而非常驻禁用 ----
    // 起点归位：前序探针（T11 的分组重建会按路径恢复选中）可能留下选中 → 先清空再断言"无选择"
    if (view->selectionModel() != nullptr)
        view->selectionModel()->clearSelection();
    view->setCurrentIndex(QModelIndex());
    pump(60);
    const bool tools_visible = tools_row != nullptr && tools_row->isVisibleTo(panel) &&
                               search->isVisibleTo(tools_row) && add_btn->isVisibleTo(tools_row) &&
                               filter_btn->isVisibleTo(tools_row) &&
                               group_mode->isVisibleTo(tools_row);
    const bool remove_hidden_without_selection = !remove_sel->isVisibleTo(tools_row);
    view->setCurrentIndex(row_idx);
    if (view->selectionModel() != nullptr)
        view->selectionModel()->select(row_idx, QItemSelectionModel::ClearAndSelect |
                                                    QItemSelectionModel::Rows);
    pump(60);
    const bool remove_shown_with_selection =
        remove_sel->isVisibleTo(tools_row) && remove_sel->isEnabled();
    // 置灰必须仍是**隐藏**路径（1-08）：清掉选择 → 按钮消失（而不是变灰）
    if (view->selectionModel() != nullptr)
        view->selectionModel()->clearSelection();
    view->setCurrentIndex(QModelIndex());
    pump(60);
    const bool remove_hidden_again = !remove_sel->isVisibleTo(tools_row);

    // ---- (c) 信息行不截断（§7.5）：join_items = 绘制同一实现（放不下减行内项，禁省略号）----
    const QFontMetrics info_fm(FileDelegate::info_font());
    const QStringList info_items{QStringLiteral("4032×3024"), QStringLiteral("3.21 MB"),
                                 QStringLiteral("JPEG")};
    const QString wide = FileDelegate::join_items(info_items, info_fm, 400);
    const QString narrow = FileDelegate::join_items(info_items, info_fm, 90);
    const QString tiny = FileDelegate::join_items(info_items, info_fm, 10);
    const bool no_ellipsis = !wide.contains(QChar(0x2026)) && !narrow.contains(QChar(0x2026)) &&
                             !tiny.contains(QChar(0x2026));
    const bool dims_first = narrow.startsWith(QStringLiteral("4032×3024"));
    // 第三行（日期 · 机型）的装配同一实现（golden/base 夹具无 EXIF → 行内取数为空，
    // 故此处用合成项对装配口径取证；role 侧只断言可读 + 日期形态）
    const QString shot_line = FileDelegate::join_items(
        {QStringLiteral("2024-05-12"), QStringLiteral("Apple iPhone 15 Pro")}, info_fm, 400);
    const bool shot_line_ok = shot_line == QStringLiteral("2024-05-12 · Apple iPhone 15 Pro");
    // 行内取数（第二/三行信息）：三项 role 可读（尺寸/大小/格式 + 日期 · 机型）
    const QVariant size_var = row_idx.data(FileListModel::SizeRole);
    const QVariant date_var = row_idx.data(FileListModel::ShotDateRole);
    const QVariant camera_var = row_idx.data(FileListModel::CameraRole);
    const QString size_text = size_var.toString();
    const QString date_text = date_var.toString();
    const QString camera_text = camera_var.toString();
    const bool info_roles_ok = size_var.isValid() && date_var.isValid() && camera_var.isValid() &&
                               !size_text.isEmpty() &&
                               (date_text.isEmpty() || date_text.size() == 10);

    // ---- (d) 空态投放区（修 1-06 + A16）：结构 + 真值表 + 运行态落地 + 文案去重 ----
    // A16：文件列表空态 = 「拖放照片到此处」（§8 的逐字引导语「将照片拖到此处，或点击 ＋ 添加」
    // 归预览画布 —— 同屏两条同文冗余）；显式「添加文件…」钮留存（§8 逐字）。
    const QString drop_full = drop_text != nullptr ? drop_text->full_text() : QString();
    const bool empty_struct =
        empty_state != nullptr && drop_zone != nullptr && drop_icon != nullptr &&
        !drop_icon->pixmap().isNull() && drop_text != nullptr && empty_add != nullptr &&
        !empty_add->text().isEmpty() && empty_add->height() == theme::Metrics::btn_h &&
        !drop_zone->styleSheet().isEmpty() && drop_full == QStringLiteral("拖放照片到此处") &&
        empty_add->text() == QStringLiteral("添加文件…") &&
        drop_full != QStringLiteral("将照片拖到此处，或点击 ＋ 添加"); // A16：不得与预览同文
    const bool empty_truth = MainWindow::Impl::empty_state_visible(true, false) &&
                             !MainWindow::Impl::empty_state_visible(false, false) &&
                             !MainWindow::Impl::empty_state_visible(true, true);
    const bool empty_live = file_stack != nullptr && file_stack->currentIndex() == 0 &&
                            !empty_state->isVisible(); // 有文件 → 列表页（空态不占位）

    // ---- (e) 选中面像素取证（修 3-17）：选中 = 整行 accent-dim 底 + 左缘 2px accent 条 ----
    view->scrollToTop();
    pump(40);
    const QRect sel_rect = view->visualRect(row_idx);
    const QImage before = view->viewport()->grab().toImage();
    FileViewFocusFrameStyle::reset_counters(); // M5-T14 诊断：本次选中面取证的拦截计数
    const QImage after = [&] {
        view->setCurrentIndex(row_idx);
        view->selectionModel()->select(row_idx, QItemSelectionModel::ClearAndSelect |
                                                    QItemSelectionModel::Rows);
        pump(60);
        return view->viewport()->grab().toImage();
    }();
    const qreal shot_scale = view->viewport()->width() > 0
                                 ? double(before.width()) / double(view->viewport()->width())
                                 : 1.0;
    const int sample_x = int(std::lround(0.5 * shot_scale)); // 条带 2px 之内（x=0..1）
    const int sample_y = int(std::lround((sel_rect.top() + sel_rect.height() / 2.0) * shot_scale));
    const QColor bar_unsel = before.pixelColor(sample_x, sample_y);
    const QColor bar_sel = after.pixelColor(sample_x, sample_y);
    // M5-T14 断言补强（只增）：左缘 2px accent 条**整幅**取样 —— 0.5 与 1.5 逻辑像素两处都必须
    // 落在 accent 上（T13 真实平台红 = 系统焦点框「1px 白 + 1px 黑」恰好盖满这两列；修复后两处
    // 复原 → 本检查同时守住"条宽 = 2px"与"无系统焦点框覆盖条带"两条语义）。
    const int bar_last_x =
        std::max(0, int(std::lround(double(FileDelegate::kSelectionBarW) * shot_scale)) - 1);
    const int sample_x2 = std::clamp(int(std::lround(1.5 * shot_scale)), 0, bar_last_x);
    const QColor bar_sel2 = after.pixelColor(sample_x2, sample_y);
    // M5-T13 验收取证（只增）：选中条取样邻域 + 列 0 矩形 + 视图滚动量（真实平台失败定位）。
    {
        QStringList before_px;
        QStringList after_px;
        for (int x = 0; x <= 6; ++x) {
            before_px << before.pixelColor(x, sample_y).name(QColor::HexRgb);
            after_px << after.pixelColor(x, sample_y).name(QColor::HexRgb);
        }
        const QRect sel_rect_after = view->visualRect(row_idx);
        const QRect check_rect_v = view->visualRect(check_idx);
        QStringList grid;
        const int yt = int(std::lround(sel_rect.top() * shot_scale));
        const int yb = int(std::lround(sel_rect.bottom() * shot_scale));
        for (int y : {yt + 2, yt + 10, sample_y - 20, sample_y, sample_y + 20, yb - 10, yb - 2}) {
            const int yy = std::clamp(y, 0, after.height() - 1);
            QStringList row;
            for (int x = 0; x <= 8; x += 2)
                row << after.pixelColor(x, yy).name(QColor::HexRgb);
            grid << QStringLiteral("y%1[%2]").arg(yy).arg(row.join(QLatin1Char(',')));
        }
        std::printf(
            "UI-SMOKE filelist-selprobe: dpr=%.1f scale=%.2f x=%d y=%d sel=%d,%d,%dx%d "
            "sel-after=%d,%d,%dx%d check=%d,%d,%dx%d focus=%d curcol=%d bar2=x%d:%s "
            "focus-frame-suppress[opt=%d wid=%d dev=%d miss=%d] before=[%s] after=[%s]\n",
            view->viewport()->devicePixelRatioF(), shot_scale, sample_x, sample_y, sel_rect.x(),
            sel_rect.y(), sel_rect.width(), sel_rect.height(), sel_rect_after.x(),
            sel_rect_after.y(), sel_rect_after.width(), sel_rect_after.height(), check_rect_v.x(),
            check_rect_v.y(), check_rect_v.width(), check_rect_v.height(), view->hasFocus() ? 1 : 0,
            view->currentIndex().column(), sample_x2, qUtf8Printable(bar_sel2.name(QColor::HexRgb)),
            FileViewFocusFrameStyle::hits_option(), FileViewFocusFrameStyle::hits_widget(),
            FileViewFocusFrameStyle::hits_device(), FileViewFocusFrameStyle::misses(),
            qUtf8Printable(before_px.join(QLatin1Char(','))),
            qUtf8Printable(after_px.join(QLatin1Char(','))));
        std::printf("UI-SMOKE filelist-selgrid: %s\n", qUtf8Printable(grid.join(QLatin1Char(' '))));
        std::fflush(stdout);
    }
    const auto near = [](const QColor &a, const QColor &b, int tol) {
        return std::abs(a.red() - b.red()) <= tol && std::abs(a.green() - b.green()) <= tol &&
               std::abs(a.blue() - b.blue()) <= tol;
    };
    const bool bar_is_accent = near(bar_sel, t.accent, 32);
    // M5-T14 断言补强（只增）：条带第 2 逻辑像素同为 accent（守"条宽 = 2px"），
    // 且骨架 QSS 已对文件列表视图关闭系统焦点框（守"系统虚线框不再盖条带"的实现面）。
    const bool bar_is_accent2 = near(bar_sel2, t.accent, 32);
    const bool focus_frame_off = w->styleSheet().contains(QStringLiteral("outline: none")) &&
                                 w->styleSheet().contains(QStringLiteral("QTreeView#pp-file-view"));
    const bool selection_distinct = !near(bar_sel, bar_unsel, 24);
    const bool sel_token = FileDelegate::selection_fill(t) == t.accent_dim &&
                           FileDelegate::selection_bar(t) == t.accent &&
                           FileDelegate::kSelectionBarW == 2;
    // 归位（后续探针/截图不受选中面污染）
    if (view->selectionModel() != nullptr)
        view->selectionModel()->clearSelection();
    view->setCurrentIndex(QModelIndex());
    view->scrollToTop();
    pump(60);

    // ---- (f) 面板无残留（M5-T5 实测伪影回归）：分类面板可见行/chip 数 == 行数 ----
    // deleteLater 要等事件循环回主层才析构（冒烟的 processEvents 不派发 DeferredDelete）→
    // 重建时必须先 hide()，否则旧代会以旧几何在同槽位叠画（"双圆角框 + 双 ＋"伪影）。
    int visible_rows = 0;
    int visible_chips = 0;
    for (QWidget *w : classify_card->findChildren<QWidget *>(QStringLiteral("pp-class-row"))) {
        if (w->isVisible())
            ++visible_rows;
    }
    for (QWidget *w : classify_card->findChildren<QWidget *>(QStringLiteral("pp-class-chip"))) {
        if (w->isVisible())
            ++visible_chips;
    }
    const int classify_rows = classify_panel != nullptr ? classify_panel->row_count() : -1;
    const bool no_stale_rows = classify_panel != nullptr && visible_rows == classify_rows &&
                               visible_chips == classify_rows;

    std::printf(
        "UI-SMOKE filelist-geom: checked=%d bad=%d row=%dx%d thumb=%d check=%d check_col=%d "
        "pill_h=%d btn=%dx%d info_px=%d tools=%d remove-hidden=%d/%d/%d join=\"%s\"|\"%s\" "
        "info=size:\"%s\" date:\"%s\" camera:\"%s\" drop=\"%s\" empty=%d/%d/%d rows=%d/%d "
        "bar=%s→%s|%s sel_token=%d focus-frame-off=%d %s\n",
        count, int(bad.size()), row_rect.width(), row_rect.height(), rects.thumb.width(),
        check_box.width(), view->columnWidth(FileGroupProxyModel::kCheckColumn),
        rects.pill.height(), add_btn != nullptr ? add_btn->width() : -1,
        add_btn != nullptr ? add_btn->height() : -1, px_of(FileDelegate::info_font()),
        tools_visible ? 1 : 0, remove_hidden_without_selection ? 1 : 0,
        remove_shown_with_selection ? 1 : 0, remove_hidden_again ? 1 : 0, qUtf8Printable(wide),
        qUtf8Printable(narrow), qUtf8Printable(size_text), qUtf8Printable(date_text),
        qUtf8Printable(camera_text), qUtf8Printable(drop_full), empty_struct ? 1 : 0,
        empty_truth ? 1 : 0, empty_live ? 1 : 0, visible_rows, classify_rows,
        qUtf8Printable(bar_unsel.name(QColor::HexRgb)),
        qUtf8Printable(bar_sel.name(QColor::HexRgb)), qUtf8Printable(bar_sel2.name(QColor::HexRgb)),
        sel_token ? 1 : 0, focus_frame_off ? 1 : 0, qUtf8Printable(bad.join(QLatin1Char(' '))));
    std::fflush(stdout);
    if (!bad.isEmpty())
        smoke_fail(MainWindow::tr("文件行/工具行几何（§7.5/R4/A3/A7）不符：%1")
                       .arg(bad.join(QLatin1Char(' '))));
    if (!tools_visible || !remove_hidden_without_selection || !remove_shown_with_selection ||
        !remove_hidden_again) {
        smoke_fail(MainWindow::tr("工具行常驻/移除钮隐藏不符（tools=%1 无选择隐藏=%2 有选择显示=%3 "
                                  "复归隐藏=%4）")
                       .arg(tools_visible ? 1 : 0)
                       .arg(remove_hidden_without_selection ? 1 : 0)
                       .arg(remove_shown_with_selection ? 1 : 0)
                       .arg(remove_hidden_again ? 1 : 0));
    }
    if (!no_ellipsis || !dims_first || narrow != QStringLiteral("4032×3024"))
        smoke_fail(MainWindow::tr("信息行截断口径不符（宽=\"%1\" 窄=\"%2\" 极窄=\"%3\"）")
                       .arg(wide, narrow, tiny));
    if (wide != QStringLiteral("4032×3024 · 3.21 MB · JPEG") || !shot_line_ok)
        smoke_fail(MainWindow::tr("信息行装配不符：\"%1\" / \"%2\"").arg(wide, shot_line));
    if (!info_roles_ok)
        smoke_fail(MainWindow::tr("行内信息取数不符（大小=\"%1\" 日期=\"%2\" 机型=\"%3\"）")
                       .arg(size_text, date_text, camera_text));
    if (!empty_struct || !empty_truth || !empty_live)
        smoke_fail(MainWindow::tr("空态投放区不符（结构=%1 真值=%2 落地=%3 文案=\"%4\" 钮=\"%5\"）")
                       .arg(empty_struct ? 1 : 0)
                       .arg(empty_truth ? 1 : 0)
                       .arg(empty_live ? 1 : 0)
                       .arg(drop_full, empty_add != nullptr ? empty_add->text() : QString()));
    if (!no_stale_rows)
        smoke_fail(MainWindow::tr("分类面板残留部件（可见行 %1 / chip %2 ≠ 行数 %3）")
                       .arg(visible_rows)
                       .arg(visible_chips)
                       .arg(classify_rows));
    if (!bar_is_accent || !bar_is_accent2 || !selection_distinct || !sel_token || !focus_frame_off)
        smoke_fail(MainWindow::tr("选中行高亮（修 3-17）不符：像素 %1→%2 条带右列=%3（accent=%4）"
                                  "条带=%5 焦点框关闭=%6")
                       .arg(bar_unsel.name(QColor::HexRgb), bar_sel.name(QColor::HexRgb),
                            bar_sel2.name(QColor::HexRgb), theme::css_color(t.accent))
                       .arg(sel_token ? 1 : 0)
                       .arg(focus_frame_off ? 1 : 0));
}

// ---------------------------------------------------------------------------
// M4-T11 分类面板自检（§6.2 CRUD / 热键打标 / 圈选 / classes.json 往返）
//   自动化部分：面板行结构与「全部」虚拟分类、CRUD（增/改/色/热键唯一/删）、
//   热键打标（预览接线位 → registry）、右键菜单项集合与圈选三项语义、
//   classes.json 保存-重载往返、运行期只读。
//   手测部分（W5 走查清单）：颜色对话框取色、重命名输入体验、原生右键菜单外观。
// ---------------------------------------------------------------------------

void MainWindow::Impl::smoke_probe_classify() {
    if (classify_panel == nullptr || model->empty()) {
        smoke_fail(MainWindow::tr("分类自检：面板或列表缺失"));
        return;
    }
    // 冒烟不碰用户真实注册表：把 classes.json 指到仓库临时目录（结束前还原）
    const QString saved_file = classes_file;
    const pp::ClassRegistry backup = classes; // 快照（结束时还原）
    const QString tmp_dir = repo_root() + QStringLiteral("/.cache/tmp/ui-smoke-classes");
    QDir(tmp_dir).removeRecursively();
    if (!QDir().mkpath(tmp_dir))
        smoke_fail(MainWindow::tr("无法创建分类冒烟目录：%1").arg(tmp_dir));
    classes_file = tmp_dir + QStringLiteral("/classes.json");

    // ---- (a) 面板结构：1 + classes.size() 行；首行 = 「全部」（计数 = 文件总数）----
    const QModelIndex g0 = groups->group_index(0);
    const QModelIndex first_file = groups->index(0, FileGroupProxyModel::kRowColumn, g0);
    if (first_file.isValid() && view->selectionModel() != nullptr) {
        view->setCurrentIndex(first_file); // 选中首文件 → 面板 .on 跟随
        view->selectionModel()->select(first_file, QItemSelectionModel::ClearAndSelect |
                                                       QItemSelectionModel::Rows);
    }
    pump(60);
    const int expect_rows = 1 + int(classes.classes.size());
    std::printf("UI-SMOKE classify-panel: rows=%d expect=%d all_label=\"%s\" all_count=%d "
                "total=%d active=%d hotkeys=[%s]\n",
                classify_panel->row_count(), expect_rows,
                qUtf8Printable(classify_panel->row_label(0)), classify_panel->row_count_value(0),
                int(model->size()), classify_panel->row_active(0) ? 1 : 0,
                qUtf8Printable(classify_panel->hotkey_labels().join(QLatin1Char(' '))));
    std::fflush(stdout);
    if (classify_panel->row_count() != expect_rows)
        smoke_fail(MainWindow::tr("面板行数 %1 ≠ 1 + 类数 %2")
                       .arg(classify_panel->row_count())
                       .arg(expect_rows));
    if (classify_panel->row_label(0) != tr("全部") ||
        classify_panel->row_count_value(0) != int(model->size())) {
        smoke_fail(MainWindow::tr("「全部」行不符：label=\"%1\" count=%2（期望 %3）")
                       .arg(classify_panel->row_label(0))
                       .arg(classify_panel->row_count_value(0))
                       .arg(model->size()));
    }
    // 默认模板（§6.2）：精选/待定/废片 + 热键 1/2/3
    const QStringList hotkeys = classify_panel->hotkey_labels();
    if (hotkeys !=
        QStringList{QStringLiteral("1=精选"), QStringLiteral("2=待定"), QStringLiteral("3=废片")}) {
        smoke_fail(MainWindow::tr("默认类表/热键不符：[%1]").arg(hotkeys.join(QLatin1Char(' '))));
    }

    // ---- (a2) A15：面板 = **2 列 kv-grid**（mockups-v2 形态）----
    //   口径（逐条）：① 2 列 → 相邻两单元同行（同 y）、第三行起换行；② 行 pitch 28（R1）；
    //   ③ 两列**等宽**（1fr/1fr）且列距 = form_col_gap；④ 「＋ 新建分类…」占网格的**下一个
    //   空位**（与 mockup 的 `.kv-grid` 内 button 同格位：偶数行数 → 换行首格，奇数 → 紧随其后）。
    {
        QStringList grid_bad;
        const int rows_now = classify_panel->row_count();
        QVector<QWidget *> cells;
        for (int i = 0; i < rows_now; ++i)
            cells.append(classify_panel->row_widget(i));
        // 当前代的新建格（findChild 会命中尚未析构的旧代 → 走面板的**当前代**读数）
        QWidget *new_cell = classify_panel->new_class_cell();
        int pitch_bad = 0;
        for (QWidget *cell : cells) {
            if (cell == nullptr || !cell->isVisible() || cell->height() != formkit::kRowPitch)
                ++pitch_bad;
        }
        if (pitch_bad > 0)
            grid_bad << QStringLiteral("row-pitch=%1").arg(pitch_bad);
        int pair_bad = 0;
        int line_bad = 0;
        for (int i = 0; i + 1 < rows_now; i += 2) {
            const QWidget *left = cells.value(i);
            const QWidget *right = cells.value(i + 1);
            if (left == nullptr || right == nullptr) {
                ++pair_bad;
                continue;
            }
            // 同行（同 y）+ 右侧在后（视觉序）+ 两列等宽 + 列距 = form_col_gap
            if (left->y() != right->y() || right->x() <= left->x() ||
                std::abs(right->width() - left->width()) > 1 ||
                right->x() - left->x() != left->width() + theme::Metrics::form_col_gap) {
                ++pair_bad;
            }
        }
        // 列对齐：第 2 网格行（若有）与第 1 行同轴（同 x）；行距 = 1 行 pitch（28）
        if (rows_now > 2 && cells.at(0) != nullptr && cells.at(2) != nullptr) {
            if (cells.at(2)->x() != cells.at(0)->x() ||
                cells.at(2)->y() - cells.at(0)->y() != formkit::kRowPitch) {
                ++line_bad;
            }
        }
        if (pair_bad > 0)
            grid_bad << QStringLiteral("pairs=%1").arg(pair_bad);
        if (line_bad > 0)
            grid_bad << QStringLiteral("second-line-misaligned");
        // ④ 新建链的格位 = 下一个空位
        bool slot_ok = new_cell != nullptr && !cells.isEmpty();
        if (slot_ok) {
            const bool even = (rows_now % 2) == 0;
            QWidget *last = cells.last();
            QWidget *first = cells.first();
            QWidget *second = cells.value(1, first);
            slot_ok = even ? (new_cell->y() > last->y() && new_cell->x() == first->x())
                           : (new_cell->y() == last->y() && new_cell->x() == second->x());
        }
        if (!slot_ok)
            grid_bad << QStringLiteral("new-link-slot");
        const int cell_w =
            cells.isEmpty() || cells.first() == nullptr ? -1 : cells.first()->width();
        QStringList geom;
        for (QWidget *cell : cells) {
            geom << (cell != nullptr ? QStringLiteral("%1+%2,%3x%4")
                                           .arg(cell->x())
                                           .arg(cell->y())
                                           .arg(cell->width())
                                           .arg(cell->height())
                                     : QStringLiteral("<null>"));
        }
        if (new_cell != nullptr)
            geom << QStringLiteral("new=%1+%2,%3x%4")
                        .arg(new_cell->x())
                        .arg(new_cell->y())
                        .arg(new_cell->width())
                        .arg(new_cell->height());
        std::printf("UI-SMOKE classify-grid: rows=%d cell=%dx%d cols=2 gap=%d pitch=%d bad=%d "
                    "geom=[%s] %s\n",
                    rows_now, cell_w,
                    cells.isEmpty() || cells.first() == nullptr ? -1 : cells.first()->height(),
                    theme::Metrics::form_col_gap, formkit::kRowPitch, int(grid_bad.size()),
                    qUtf8Printable(geom.join(QLatin1Char(' '))),
                    qUtf8Printable(grid_bad.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (!grid_bad.isEmpty())
            smoke_fail(MainWindow::tr("分类面板 2 列网格（A15）不符：%1")
                           .arg(grid_bad.join(QLatin1Char(' '))));
    }

    // ---- (b) 打标：预览热键接线位（class_hotkey → 注册表）----
    const int target_row = preview_panel->current_index() >= 0 ? preview_panel->current_index() : 0;
    const QString target_path =
        QString::fromStdString(model->row(std::size_t(target_row)).entry.src.string());
    const auto send_hotkey = [this](int key, const char *text) {
        QWidget *target =
            view != nullptr ? static_cast<QWidget *>(view) : static_cast<QWidget *>(w);
        target->setFocus();
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString::fromLatin1(text));
        QApplication::sendEvent(target, &press);
        pump(40);
    };
    send_hotkey(Qt::Key_2, "2"); // → 待定（maybe）
    const std::optional<std::string> tagged =
        classes.class_of(std::filesystem::path(target_path.toStdString()));
    pump(60);
    const QString model_class = QString::fromStdString(model->class_id_of(std::size_t(target_row)));
    const QVariant dot = model->index(target_row, 0).data(Qt::DecorationRole);
    const int maybe_count = int(model->class_file_count("maybe"));
    std::printf("UI-SMOKE classify-tag: row=%d file=\"%s\" registry=%s model_class=\"%s\" "
                "dot_valid=%d maybe_count=%d panel_count=%d\n",
                target_row, qUtf8Printable(QFileInfo(target_path).fileName()),
                tagged.has_value() ? tagged->c_str() : "<none>", qUtf8Printable(model_class),
                dot.isValid() ? 1 : 0, maybe_count,
                classify_panel->row_count_value(2)); // 行 2 = 待定（默认模板顺序）
    std::fflush(stdout);
    if (tagged.value_or("") != "maybe")
        smoke_fail(MainWindow::tr("热键 '2' 未把文件标为「待定」（实为 %1）")
                       .arg(QString::fromStdString(tagged.value_or("<none>"))));
    if (model_class != QLatin1String("maybe"))
        smoke_fail(MainWindow::tr("ClassIdRole 未跟随打标：\"%1\"").arg(model_class));
    if (!dot.isValid())
        smoke_fail(MainWindow::tr("装饰角色（分类色点）未给出颜色"));
    if (classify_panel->row_count_value(2) != maybe_count)
        smoke_fail(MainWindow::tr("面板「待定」计数 %1 ≠ 模型计数 %2")
                       .arg(classify_panel->row_count_value(2))
                       .arg(maybe_count));

    // ---- (c) 右键菜单项集合 + 圈选三项语义 ----
    QMenu *class_menu = classify_panel->build_row_menu(1); // 行 1 = 精选
    QStringList class_actions;
    for (QAction *action : class_menu->actions()) {
        if (!action->isSeparator())
            class_actions << action->text();
    }
    delete class_menu;
    QMenu *all_menu = classify_panel->build_row_menu(0); // 行 0 = 「全部」
    QStringList all_actions;
    for (QAction *action : all_menu->actions()) {
        if (!action->isSeparator())
            all_actions << action->text();
    }
    delete all_menu;
    // R-142 机械换锚（语义不变；登记 build/m7-wp6/handoff.md T13）：第三项「清空勾选」→
    // 「清空该类勾选」（原为全局清空，与前两项的"该类"限定自相矛盾）。菜单项集合/顺序不变。
    const QStringList expect_class{QStringLiteral("全选该类"),     QStringLiteral("反选该类"),
                                   QStringLiteral("清空该类勾选"), QStringLiteral("重命名…"),
                                   QStringLiteral("颜色…"),        QStringLiteral("热键…"),
                                   QStringLiteral("删除")};
    const QStringList expect_all{QStringLiteral("全选该类"), QStringLiteral("反选该类"),
                                 QStringLiteral("清空该类勾选")};
    std::printf("UI-SMOKE classify-menu: class=[%s] all=[%s]\n",
                qUtf8Printable(class_actions.join(QLatin1Char(' '))),
                qUtf8Printable(all_actions.join(QLatin1Char(' '))));
    std::fflush(stdout);
    if (class_actions != expect_class)
        smoke_fail(MainWindow::tr("类行菜单不符：[%1]").arg(class_actions.join(QLatin1Char(' '))));
    if (all_actions != expect_all)
        smoke_fail(
            MainWindow::tr("「全部」行菜单不符：[%1]").arg(all_actions.join(QLatin1Char(' '))));
    // 圈选：该类全部取消 → 全选该类复原；反选该类；清空勾选
    classify_panel->invert_class(QStringLiteral("maybe")); // 只有 1 个待定文件 → 取消
    const int after_invert = int(model->checked_count());
    classify_panel->check_class(QStringLiteral("maybe")); // 全选该类 → +1（其余不动）
    const int after_check_class = int(model->checked_count());
    classify_panel->clear_checks();
    const int after_clear = int(model->checked_count());
    classify_panel->check_class(QString()); // 「全部」= 全部文件
    const int after_all = int(model->checked_count());
    std::printf("UI-SMOKE classify-bulk: invert=%d check_class=%d clear=%d all=%d total=%d\n",
                after_invert, after_check_class, after_clear, after_all, int(model->size()));
    std::fflush(stdout);
    if (after_invert != int(model->size()) - 1)
        smoke_fail(MainWindow::tr("反选该类计数不符：%1（期望 %2）")
                       .arg(after_invert)
                       .arg(int(model->size()) - 1));
    if (after_check_class != int(model->size()))
        smoke_fail(MainWindow::tr("全选该类计数不符：%1").arg(after_check_class));
    if (after_clear != 0)
        smoke_fail(MainWindow::tr("清空勾选后仍有 %1 个勾选").arg(after_clear));
    if (after_all != int(model->size()))
        smoke_fail(MainWindow::tr("「全部」全选计数不符：%1").arg(after_all));

    // ---- (d) CRUD（§6.2）：增 / 改名 / 颜色 / 热键唯一 / 删；「全部」不可改 ----
    // 中文名 → ASCII slug 不可用 → id 走 "class" 兜底（id 是持久化键，显示名可随时改）
    const bool created = classify_panel->new_class(QStringLiteral("旅行"), 0x1565C0u, '4');
    const QString new_id =
        classes.classes.empty() ? QString() : QString::fromStdString(classes.classes.back().id);
    const bool slug_fallback = new_id == QLatin1String("class");
    const bool dup_hotkey_rejected = !classify_panel->set_class_hotkey(new_id, '1');
    const bool renamed = classify_panel->rename_class(new_id, QStringLiteral("远行"));
    const bool recolored = classify_panel->set_class_color(new_id, 0xFF00FFu);
    const bool all_immutable =
        !classify_panel->rename_class(QString::fromUtf8(pp::ClassRegistry::kAllId.data(),
                                                        int(pp::ClassRegistry::kAllId.size())),
                                      QStringLiteral("x")) &&
        !classify_panel->delete_class(QString::fromUtf8(pp::ClassRegistry::kAllId.data(),
                                                        int(pp::ClassRegistry::kAllId.size())));
    const std::size_t rows_after_create = classes.classes.size();
    const pp::ClassDef *travel = classes.find(new_id.toStdString());
    std::printf(
        "UI-SMOKE classify-crud: created=%d id=\"%s\" slug_fallback=%d "
        "dup_hotkey_rejected=%d renamed=%d recolored=%d classes=%d name=\"%s\" rgb=%06X "
        "hotkey=%c all_immutable=%d\n",
        created ? 1 : 0, qUtf8Printable(new_id), slug_fallback ? 1 : 0, dup_hotkey_rejected ? 1 : 0,
        renamed ? 1 : 0, recolored ? 1 : 0, int(rows_after_create),
        travel != nullptr ? travel->name.c_str() : "<none>", travel != nullptr ? travel->rgb : 0u,
        travel != nullptr && travel->hotkey != 0 ? travel->hotkey : '-', all_immutable ? 1 : 0);
    std::fflush(stdout);
    if (!created || !dup_hotkey_rejected || !renamed || !recolored)
        smoke_fail(MainWindow::tr("CRUD 失败：created=%1 dup_hotkey=%2 rename=%3 color=%4")
                       .arg(created ? 1 : 0)
                       .arg(dup_hotkey_rejected ? 1 : 0)
                       .arg(renamed ? 1 : 0)
                       .arg(recolored ? 1 : 0));
    if (travel == nullptr || travel->name != "远行" || (travel->rgb & 0xFFFFFFu) != 0xFF00FFu)
        smoke_fail(MainWindow::tr("CRUD 结果未落注册表"));
    if (!all_immutable)
        smoke_fail(MainWindow::tr("保留虚拟分类「全部」被改名/删除成功（应被拒）"));
    if (classify_panel->row_count() != 1 + int(classes.classes.size()))
        smoke_fail(MainWindow::tr("CRUD 后面板行数未跟随"));

    // ---- (e) classes.json 往返（§6.2 持久化）----
    save_classes(); // 打标 + CRUD 均已触发；这里显式落盘取文件
    const std::filesystem::path file(classes_file.toStdString());
    pp::ClassRegistry reloaded;
    std::string load_err;
    const bool loaded = reloaded.load(file, &load_err);
    bool same = loaded && reloaded.classes.size() == classes.classes.size() &&
                reloaded.assignment.size() == classes.assignment.size();
    for (std::size_t i = 0; same && i < classes.classes.size(); ++i) {
        const pp::ClassDef &a = classes.classes[i];
        const pp::ClassDef &b = reloaded.classes[i];
        same = a.id == b.id && a.name == b.name && a.rgb == b.rgb && a.hotkey == b.hotkey;
    }
    const std::optional<std::string> reloaded_class =
        reloaded.class_of(std::filesystem::path(target_path.toStdString()));
    std::printf("UI-SMOKE classify-roundtrip: file=\"%s\" loaded=%d classes=%d/%d "
                "assignment=%d/%d same=%d tagged=%s err=\"%s\"\n",
                qUtf8Printable(QFileInfo(classes_file).fileName()), loaded ? 1 : 0,
                int(reloaded.classes.size()), int(classes.classes.size()),
                int(reloaded.assignment.size()), int(classes.assignment.size()), same ? 1 : 0,
                reloaded_class.has_value() ? reloaded_class->c_str() : "<none>", load_err.c_str());
    std::fflush(stdout);
    if (!loaded || !same)
        smoke_fail(MainWindow::tr("classes.json 往返不符（loaded=%1 same=%2 err=%3）")
                       .arg(loaded ? 1 : 0)
                       .arg(same ? 1 : 0)
                       .arg(QString::fromStdString(load_err)));
    if (reloaded_class.value_or("") != "maybe")
        smoke_fail(MainWindow::tr("往返后归属丢失：%1")
                       .arg(QString::fromStdString(reloaded_class.value_or("<none>"))));

    // ---- (f) 运行期只读（G5）：锁 → 行/新建钮置灰、菜单不弹 ----
    classify_panel->set_locked(true);
    const bool locked_rows_disabled =
        classify_panel->row_widget(0) != nullptr && !classify_panel->row_widget(0)->isEnabled();
    const int locked_checked = int(model->checked_count());
    classify_panel->invert_class(QStringLiteral("maybe")); // 锁定时为 no-op
    const bool lock_respected = int(model->checked_count()) == locked_checked;
    classify_panel->set_locked(false);
    std::printf("UI-SMOKE classify-lock: rows_disabled=%d bulk_noop=%d\n",
                locked_rows_disabled ? 1 : 0, lock_respected ? 1 : 0);
    std::fflush(stdout);
    if (!locked_rows_disabled || !lock_respected)
        smoke_fail(MainWindow::tr("运行期只读未生效（rows=%1 noop=%2）")
                       .arg(locked_rows_disabled ? 1 : 0)
                       .arg(lock_respected ? 1 : 0));

    // ---- 归位：还原注册表快照与 classes.json 路径（截图基准不受冒烟污染）----
    classes = backup;
    classes_file = saved_file;
    model->notify_class_changed();
    classify_panel->set_registry(&classes);
    preview_panel->set_class_registry(&classes);
    model->set_all_checked(true);
    if (view->selectionModel() != nullptr)
        view->selectionModel()->clearSelection();
    view->setCurrentIndex(QModelIndex());
    pump(120);
}

// ---------------------------------------------------------------------------
// M4-W3-T14 运行页自检（§3.4 + §7.1-7.4 + §9.3；mockup run-dark = 精确规格）
//   * 页实例独立（不动主窗口的运行页）：外壳容器挂主题骨架 QSS（窗口底渐变 + QFrame#pp-card
//     等只作用于 pp-* 钩子的规则），页面只吃 tokens —— 与 smoke_probe_meta 的独立页同口径；
//   * 探针覆盖：逐输出子行五态（真实进行中 / 斜纹合成 / 完成产物事实 / 失败原因 / 交错排队）·
//     **真实 vs 斜纹的像素级取证**（斜纹 = 双色带 #3aa3dc/#4cc2ff 同时出现；实心 = 只有 accent，
//     无 #3aa3dc）· 总览只读栅格（交错/线程预算/并行/当前分配）· 取消态 · 浅色主题。
// ---------------------------------------------------------------------------
namespace {

// 迷你进度条取样：统计左/右三色 token 的命中数（取样带 = 条内中部，避开 1px 边框与圆角）
struct BarPixels {
    int samples = 0, accent = 0, from = 0, to = 0;
    double accent_ratio() const { return samples > 0 ? double(accent) / samples : 0.0; }
    double from_ratio() const { return samples > 0 ? double(from) / samples : 0.0; }
    double to_ratio() const { return samples > 0 ? double(to) / samples : 0.0; }
};

BarPixels probe_bar_pixels(const QImage &img, const theme::Tokens &t, double frac) {
    BarPixels p;
    if (img.isNull() || img.width() < 24 || img.height() < 4)
        return p;
    const auto near = [](QRgb c, const QColor &ref) {
        return std::abs(qRed(c) - ref.red()) <= 16 && std::abs(qGreen(c) - ref.green()) <= 16 &&
               std::abs(qBlue(c) - ref.blue()) <= 16;
    };
    // 取样带 = **期望填充区的内部**：grab() 会用调色板窗底色预填 pixmap（实测轨道像素不透明），
    // 故不能靠 alpha 区分轨道/填充；改为按语义读数 frac（ppBarFrac）算右沿，再两侧各内缩 4px
    // 避开 1px 边框与圆角弧。
    const int inner_left = 2;
    const int inner_right = img.width() - 2;
    const int fill_right = inner_left + int(double(inner_right - inner_left) * frac);
    const int left = inner_left + 4;
    const int right = std::min(inner_right - 4, fill_right - 4);
    if (right <= left)
        return p;
    const int y = img.height() / 2;
    for (int x = left; x <= right; ++x) {
        const QRgb c = img.pixel(x, y);
        ++p.samples;
        if (near(c, t.accent))
            ++p.accent;
        if (near(c, t.progress_fill_from))
            ++p.from;
        if (near(c, t.progress_fill_to))
            ++p.to;
    }
    return p;
}

// 取样带色彩直方图（失败时的取证：把实际画出来的颜色列出来，不靠猜）
QString probe_bar_histogram(const QImage &img) {
    if (img.isNull())
        return QStringLiteral("(null)");
    QHash<QRgb, int> hist;
    const int y = img.height() / 2;
    for (int x = 0; x < img.width(); ++x) {
        const QRgb c = img.pixel(x, y);
        ++hist[static_cast<QRgb>(c | 0xff000000u)]; // 忽略 alpha 维度用于分组
    }
    QList<QPair<int, QRgb>> sorted;
    sorted.reserve(hist.size());
    for (auto it = hist.constBegin(); it != hist.constEnd(); ++it)
        sorted.append({it.value(), it.key()});
    std::sort(
        sorted.begin(), sorted.end(),
        [](const QPair<int, QRgb> &a, const QPair<int, QRgb> &b) { return a.first > b.first; });
    QStringList parts;
    for (int i = 0; i < sorted.size() && i < 4; ++i) {
        parts << QStringLiteral("%1x%2")
                     .arg(QColor(QRgb(sorted[i].second)).name(QColor::HexRgb))
                     .arg(sorted[i].first);
    }
    return QStringLiteral("%1x%2 [%3]")
        .arg(img.width())
        .arg(img.height())
        .arg(parts.join(QLatin1Char(' ')));
}

// 探针用的事件构造
// M7R21F4-0（NOLINT 处置翻案）：index/out_index = 文件行 + 输出序号，同为 int 易混 ——
// 按「同型形参对 → 参数组结构化」收拢成命名组 EventRow（基线内同类告警一并收口）。
struct EventRow {
    int index = 0;
    int out_index = -1;
};
pp::FileEvent probe_progress(const EventRow &row, pp::Stage stage, float frac, bool synth) {
    pp::FileEvent ev{};
    ev.index = static_cast<std::size_t>(row.index);
    ev.state = pp::FileState::Progress;
    ev.progress.output_index = row.out_index;
    ev.progress.stage = stage;
    ev.progress.stage_frac = frac;
    ev.progress.overall_frac = frac;
    ev.progress.synthetic = synth;
    return ev;
}

pp::FileEvent probe_terminal(int index, pp::FileState state, const pp::FileResult &result) {
    pp::FileEvent ev{};
    ev.index = static_cast<std::size_t>(index);
    ev.state = state;
    ev.result = &result;
    return ev;
}

QString probe_full_text(const QWidget *page, const QString &name) {
    const auto *label = page->findChild<QLabel *>(name);
    if (label == nullptr)
        return QString();
    const QVariant full = label->property("ppFullText");
    return full.isValid() ? full.toString() : label->text();
}

} // namespace

void MainWindow::Impl::smoke_probe_run(const QString &shots_dir) {
    theme::Tokens dark = tokens;
    if (dark.mode != theme::ThemeMode::Dark)
        dark = theme::tokens(theme::ThemeMode::Dark); // 探针基准恒深色（原型基准）

    QWidget shell;
    shell.setObjectName(QStringLiteral("pp-root")); // theme 骨架 QSS 的窗口底（渐变）
    shell.setStyleSheet(theme::style_sheet(dark));
    shell.resize(theme::Metrics::right_width, theme::Metrics::calibrated_h - 118);
    auto *shell_layout = new QVBoxLayout(&shell);
    shell_layout->setContentsMargins(0, 0, 0, 0);
    auto *page = new PageRun(&shell);
    shell_layout->addWidget(page);
    page->set_tokens(dark);
    shell.show();
    pump(200);

    // ---- 运行计划：4 个源文件 × 2 格式（mockup run-dark 的任务行组合）----
    PageRun::RunPlan plan;
    plan.format_ids = QStringList{QStringLiteral("jpeg"), QStringLiteral("webp")};
    plan.widths = QVector<int>{4032, 2480, 6000, 4032};
    plan.heights = QVector<int>{3024, 3508, 4000, 3024};
    plan.bytes = QVector<qint64>{5'000'000, 6'200'000, 7'400'000, 1'310'720};
    plan.stagger_ms = 150; // §8.1 默认（mockup「交错 150 ms」）
    plan.workers = 0;      // 0 = 逻辑核
    plan.thread_budget = 0;
    page->set_plan(plan);
    page->set_log_tail(
        QStringList{
            QStringLiteral("18:23:43.412 [info] [encode] IMG_2732.jpg → webp 228 KB ratio=5.6 "
                           "t=0.9s"),
            QStringLiteral("18:23:44.087 [warn] [depth] scan_007.png 16bit→8bit 降档被禁止，跳过"),
            QStringLiteral("18:23:44.560 [info] [sched] DSC_0412.ARW.tif 启动（交错偏移 150ms）"),
            QStringLiteral("18:23:45.004 [info] [encode] P1010888.RW2.jpg → jpeg 640 KB ratio=4.1 "
                           "t=1.2s"),
            QStringLiteral("18:23:45.510 [info] [run] 24 个输出已结算 · 成功 22 · 失败 1 · 跳过 1"),
        },
        QStringLiteral("run-20260923-182341.log"));

    // ---- 03c-run-idle.png：空闲态（就绪 + 空任务表 + 引导 + 日志尾）----
    page->reset();
    pump(120);
    {
        const QString pill = probe_full_text(page, QStringLiteral("pp-run-state-pill"));
        const QString summary = probe_full_text(page, QStringLiteral("pp-run-summary"));
        const QString empty = probe_full_text(page, QStringLiteral("pp-run-task-empty"));
        std::printf("UI-SMOKE run-idle: pill=\"%s\" summary=\"%s\" empty=\"%s\" bars=%d\n",
                    qUtf8Printable(pill), qUtf8Printable(summary), qUtf8Printable(empty),
                    int(page->findChildren<QWidget *>(
                                QRegularExpression(QStringLiteral("^pp-run-bar-\\d+-\\d+$")))
                            .size()));
        std::fflush(stdout);
        if (pill != QStringLiteral("就绪") || summary.isEmpty() || empty.isEmpty())
            smoke_fail(MainWindow::tr("运行页空闲态不符（pill=%1 summary=%2 empty=%3）")
                           .arg(pill, summary, empty));
    }
    smoke_grab(shots_dir, kSmokeShots[kShotRunIdle], &shell);

    // ---- 03d-run-rows.png：逐输出行探针态（真实 / 斜纹 / 完成 / 失败+原因 / 0 字节异常 /
    //      交错排队；M5-T9 起五文件 × 两格式 = 10 行，覆盖 §7.3 的四种终态与两种进行中）----
    page->begin_run(5, QStringList{QStringLiteral("IMG_2731.jpg"), QStringLiteral("scan_007.png"),
                                   QStringLiteral("P1010888.RW2.jpg"),
                                   QStringLiteral("IMG_2732.jpg"), QStringLiteral("scan_012.png")});
    pump(120);
    // 文件 0：共享段（probe/decode/color）→ JPEG 真实行级 78% + WebP 合成 52%
    page->on_event(probe_progress({0, -1}, pp::Stage::Probe, 1.0f, false));
    page->on_event(probe_progress({0, -1}, pp::Stage::Decode, 1.0f, false));
    page->on_event(probe_progress({0, -1}, pp::Stage::Color, 1.0f, false));
    page->on_event(probe_progress({0, 0}, pp::Stage::Encode, 0.78f, false));
    page->on_event(probe_progress({0, 1}, pp::Stage::Encode, 0.52f, true));
    // 文件 1：共享段 → JPEG 进行中 30% → 终态失败（逐输出原因 + 文件级聚合原因同文案）；
    //         WebP 侧 ok 但 0 字节 —— 3-10 的「异常不得落绿色完成态」样本
    {
        pp::FileResult failed;
        failed.info.width = 2480;
        failed.info.height = 3508;
        failed.error = QStringLiteral("16→8 位降档被禁止").toStdString();
        failed.outputs.resize(2);
        failed.outputs[0].format_id = "jpeg";
        failed.outputs[0].error = QStringLiteral("16→8 位降档被禁止").toStdString();
        failed.outputs[0].t.encode_ms = 640;
        failed.outputs[1].format_id = "webp";
        failed.outputs[1].ok = true; // ok 但 0 字节（修 3-10 的"PNG 0 KB"同款）
        page->on_event(probe_progress({1, -1}, pp::Stage::Decode, 1.0f, false));
        page->on_event(probe_progress({1, 0}, pp::Stage::Encode, 0.30f, false));
        page->on_event(probe_terminal(1, pp::FileState::Failed, failed));
    }
    // 文件 2：无事件 = 交错排队（stagger_ms=150）→ pill 收「排队」（A39 ③：交错启动语义
    // 移进 pill 的 tooltip）
    // 文件 3：终态完成（产物事实：字节 · 压缩比 · 耗时）
    {
        pp::FileResult done;
        done.info.width = 4032;
        done.info.height = 3024;
        done.out_bytes = 421888 + 233472;
        done.outputs.resize(2);
        done.outputs[0].format_id = "jpeg";
        done.outputs[0].ok = true;
        done.outputs[0].out_bytes = 421888; // 412 KB
        done.outputs[0].t.encode_ms = 2300;
        done.outputs[0].t.metawrite_ms = 100; // 2.4s；ratio = 1310720/421888 = 3.1×
        done.outputs[1].format_id = "webp";
        done.outputs[1].ok = true;
        done.outputs[1].out_bytes = 233472; // 228 KB
        done.outputs[1].t.encode_ms = 850;
        done.outputs[1].t.metawrite_ms = 50; // 0.9s；ratio = 5.6×
        page->on_event(probe_progress({3, -1}, pp::Stage::Decode, 1.0f, false));
        page->on_event(probe_terminal(3, pp::FileState::Done, done));
    }
    // 文件 4：终态完成但**一个输出 0 字节** → 组行「异常」+ 该行琥珀（修 3-10；绿色只给真成功）
    {
        pp::FileResult zero;
        zero.info.width = 2480;
        zero.info.height = 3508;
        zero.out_bytes = 655360;
        zero.outputs.resize(2);
        zero.outputs[0].format_id = "jpeg";
        zero.outputs[0].ok = true;
        zero.outputs[0].out_bytes = 655360; // 640 KB
        zero.outputs[0].t.encode_ms = 700;
        zero.outputs[1].format_id = "webp";
        zero.outputs[1].ok = true; // 0 字节（palette 无像素写入）
        zero.outputs[1].t.encode_ms = 0;
        page->on_event(probe_progress({4, -1}, pp::Stage::Decode, 1.0f, false));
        page->on_event(probe_terminal(4, pp::FileState::Done, zero));
    }
    pump(250); // 60ms flush + 布局

    // ---- (a) 行数与行文案（M5-T9：状态列 = 胶囊；大小/速度/耗时 = 三列；失败原因 = 独立行）----
    {
        const int bars = int(page->findChildren<QWidget *>(
                                     QRegularExpression(QStringLiteral("^pp-run-bar-\\d+-\\d+$")))
                                 .size());
        const auto pill = [page](const QString &name) {
            auto *label = page->findChild<QLabel *>(name);
            return label != nullptr ? label->text() : QString();
        };
        const auto bar_kind = [page](const QString &name) {
            auto *widget = page->findChild<QWidget *>(name);
            return widget != nullptr ? widget->property("ppBarKind").toString() : QString();
        };
        const QString f0_real = pill(QStringLiteral("pp-run-status-0-0"));
        const QString f0_synth = pill(QStringLiteral("pp-run-status-0-1"));
        const QString f1_status = pill(QStringLiteral("pp-run-status-1-0"));
        const QString f1_reason = probe_full_text(page, QStringLiteral("pp-run-reason-text-1-0"));
        const QString f1_zero = pill(QStringLiteral("pp-run-status-1-1"));
        const QString f2_row0 = pill(QStringLiteral("pp-run-status-2-0"));
        const QString f2_row1 = pill(QStringLiteral("pp-run-status-2-1"));
        const auto pill_tip = [page](const QString &name) {
            auto *label = page->findChild<QLabel *>(name);
            return label != nullptr ? label->toolTip() : QString();
        };
        const QString f2_row0_tip = pill_tip(QStringLiteral("pp-run-status-2-0"));
        const QString f2_row1_tip = pill_tip(QStringLiteral("pp-run-status-2-1"));
        const QString f2_group = pill(QStringLiteral("pp-run-gstatus-2"));
        const QString f2_group_tip = pill_tip(QStringLiteral("pp-run-gstatus-2"));
        const QString f3_size = probe_full_text(page, QStringLiteral("pp-run-size-3-0"));
        const QString f3_speed = probe_full_text(page, QStringLiteral("pp-run-speed-3-0"));
        const QString f3_time = probe_full_text(page, QStringLiteral("pp-run-time-3-0"));
        const QString f3_status = pill(QStringLiteral("pp-run-status-3-0"));
        const QString f4_zero = pill(QStringLiteral("pp-run-status-4-1"));
        const QString f4_zero_kind = bar_kind(QStringLiteral("pp-run-bar-4-1"));
        const QString f4_group = pill(QStringLiteral("pp-run-gstatus-4"));
        std::printf("UI-SMOKE run-probe-rows: bars=%d f0=[%s|%s] f1=[%s|%s|%s] f2=[%s|%s] "
                    "f2tip=\"%s\" f2group=%s/%s f3=[%s|%s|%s|%s] f4=[%s|%s|%s]\n",
                    bars, qUtf8Printable(f0_real), qUtf8Printable(f0_synth),
                    qUtf8Printable(f1_status), qUtf8Printable(f1_zero), qUtf8Printable(f1_reason),
                    qUtf8Printable(f2_row0), qUtf8Printable(f2_row1), qUtf8Printable(f2_row0_tip),
                    qUtf8Printable(f2_group), qUtf8Printable(f2_group_tip), qUtf8Printable(f3_size),
                    qUtf8Printable(f3_speed), qUtf8Printable(f3_time), qUtf8Printable(f3_status),
                    qUtf8Printable(f4_zero), qUtf8Printable(f4_zero_kind),
                    qUtf8Printable(f4_group));
        std::fflush(stdout);
        if (bars != 10)
            smoke_fail(
                MainWindow::tr("逐输出行数不符：bar=%1（期望 10 = 5 文件 × 2 格式）").arg(bars));
        if (!f0_real.contains(QStringLiteral("行")) || !f0_real.contains(QStringLiteral("/3024")))
            smoke_fail(MainWindow::tr("真实行级进度未按「编码 N/M 行」显示：%1").arg(f0_real));
        if (f0_synth != QStringLiteral("编码（合成进度）"))
            smoke_fail(MainWindow::tr("合成进度行文案不符：%1").arg(f0_synth));
        if (f1_status != QStringLiteral("失败") ||
            !f1_reason.contains(QStringLiteral("16→8 位降档被禁止")) ||
            !f1_reason.startsWith(QStringLiteral("└ 原因：")))
            smoke_fail(
                MainWindow::tr("失败行未直显原因（状态=%1 原因=%2）").arg(f1_status, f1_reason));
        // W-R-T13（A39 ③）机械换锚：pill 文案由「排队中（交错启动）/ 排队中」收成「排队」
        // （mockup run.html 的 .pill 逐字）；「交错启动」的实现语义移进 pill 的 tooltip ——
        // 断言本体（排队态可读 + 交错语义可达）不变并补强（tooltip 必须带步距 150 ms）。
        if (f2_row0 != QStringLiteral("排队") || f2_row1 != QStringLiteral("排队") ||
            f2_group != QStringLiteral("排队"))
            smoke_fail(MainWindow::tr("排队行文案不符：%1 / %2（组行 %3）")
                           .arg(f2_row0, f2_row1, f2_group));
        if (!f2_row0_tip.contains(QStringLiteral("交错启动")) ||
            !f2_row0_tip.contains(QStringLiteral("150")) ||
            !f2_group_tip.contains(QStringLiteral("交错启动")) || f2_row1_tip != f2_row0_tip)
            smoke_fail(
                MainWindow::tr("排队 pill 的 tooltip 未承载交错启动语义（行0=%1 行1=%2 组=%3）")
                    .arg(f2_row0_tip, f2_row1_tip, f2_group_tip));
        if (f3_size != QStringLiteral("412 KB") || f3_speed != QStringLiteral("3.1×") ||
            f3_time != QStringLiteral("2.4s") || f3_status != QStringLiteral("已完成"))
            smoke_fail(MainWindow::tr("完成行未显示产物事实（大小=%1 速度=%2 耗时=%3 状态=%4）")
                           .arg(f3_size, f3_speed, f3_time, f3_status));
        // 修 3-10：0 字节 = 琥珀异常（条 warn ≠ done；胶囊「0 字节」；组行「异常」）
        if (f1_zero != QStringLiteral("0 字节") || f4_zero != QStringLiteral("0 字节") ||
            f4_zero_kind != QStringLiteral("warn") || f4_group != QStringLiteral("异常"))
            smoke_fail(MainWindow::tr("0 字节输出未按异常呈现（f1=%1 f4=%2 kind=%3 组=%4）")
                           .arg(f1_zero, f4_zero, f4_zero_kind, f4_group));
    }

    // ---- (b) 真实 vs 斜纹：像素级取证（斜纹 = 双色带同时出现；实心 = 无 #3aa3dc）----
    {
        auto *real_bar = page->findChild<QWidget *>(QStringLiteral("pp-run-bar-0-0"));
        auto *synth_bar = page->findChild<QWidget *>(QStringLiteral("pp-run-bar-0-1"));
        if (real_bar == nullptr || synth_bar == nullptr) {
            smoke_fail(MainWindow::tr("运行页缺少逐输出迷你条钩子（pp-run-bar-0-*）"));
        } else {
            const BarPixels real_px = probe_bar_pixels(real_bar->grab().toImage(), dark,
                                                       real_bar->property("ppBarFrac").toDouble());
            const BarPixels synth_px = probe_bar_pixels(
                synth_bar->grab().toImage(), dark, synth_bar->property("ppBarFrac").toDouble());
            std::printf("UI-SMOKE run-bars: real{samples=%d accent=%.2f from=%.2f to=%.2f} "
                        "synth{samples=%d accent=%.2f from=%.2f to=%.2f} synth-flag=%d/%d\n",
                        real_px.samples, real_px.accent_ratio(), real_px.from_ratio(),
                        real_px.to_ratio(), synth_px.samples, synth_px.accent_ratio(),
                        synth_px.from_ratio(), synth_px.to_ratio(),
                        real_bar->property("ppBarSynthetic").toBool() ? 1 : 0,
                        synth_bar->property("ppBarSynthetic").toBool() ? 1 : 0);
            std::printf("UI-SMOKE run-bars-hist: real=%s synth=%s accent=%s from=%s to=%s\n",
                        qUtf8Printable(probe_bar_histogram(real_bar->grab().toImage())),
                        qUtf8Printable(probe_bar_histogram(synth_bar->grab().toImage())),
                        qUtf8Printable(theme::css_color(dark.accent)),
                        qUtf8Printable(theme::css_color(dark.progress_fill_from)),
                        qUtf8Printable(theme::css_color(dark.progress_fill_to)));
            std::fflush(stdout);
            const bool real_solid = real_px.samples > 40 && real_px.accent_ratio() >= 0.85 &&
                                    real_px.from_ratio() <= 0.05;
            const bool synth_striped =
                synth_px.samples > 40 && synth_px.from_ratio() >= 0.2 && synth_px.to_ratio() >= 0.2;
            const bool flags = !real_bar->property("ppBarSynthetic").toBool() &&
                               synth_bar->property("ppBarSynthetic").toBool();
            if (!real_solid || !synth_striped || !flags)
                smoke_fail(
                    MainWindow::tr("真实/斜纹渲染不符（real-solid=%1 synth-striped=%2 flags=%3）")
                        .arg(real_solid ? 1 : 0)
                        .arg(synth_striped ? 1 : 0)
                        .arg(flags ? 1 : 0));
        }
    }

    // ---- (c) 总览卡只读栅格（§7.3 kv 八项；M5-T9 机械换锚：并行 = 「N / M」，§8.2 分配读数
    //      收进并行读数的 tooltip —— 3-05「工程术语出界面」的同一处置）----
    {
        const int cores = [] {
            const unsigned hw = std::thread::hardware_concurrency();
            return hw > 0 ? static_cast<int>(hw) : 1;
        }();
        const QString stagger = probe_full_text(page, QStringLiteral("pp-run-stagger"));
        const QString budget = probe_full_text(page, QStringLiteral("pp-run-budget"));
        const QString workers = probe_full_text(page, QStringLiteral("pp-run-workers"));
        const QString elapsed = probe_full_text(page, QStringLiteral("pp-run-elapsed"));
        const QString tally = probe_full_text(page, QStringLiteral("pp-run-tally"));
        const QString hint = probe_full_text(page, QStringLiteral("pp-run-progress-hint"));
        const QString percent = probe_full_text(page, QStringLiteral("pp-run-percent"));
        auto *workers_label = page->findChild<QLabel *>(QStringLiteral("pp-run-workers"));
        const QString worker_tip = workers_label != nullptr ? workers_label->toolTip() : QString();
        std::printf("UI-SMOKE run-overview: stagger=\"%s\" budget=\"%s\" workers=\"%s\" "
                    "tip=\"%s\" hint=\"%s\" percent=\"%s\" elapsed=\"%s\" tally=\"%s\"\n",
                    qUtf8Printable(stagger), qUtf8Printable(budget), qUtf8Printable(workers),
                    qUtf8Printable(worker_tip), qUtf8Printable(hint), qUtf8Printable(percent),
                    qUtf8Printable(elapsed), qUtf8Printable(tally));
        std::fflush(stdout);
        if (stagger != QStringLiteral("150 ms"))
            smoke_fail(MainWindow::tr("交错读数不符：%1").arg(stagger));
        // W-R-T13（A39 ①）机械换锚：值「自适应（16 核）」→「自动（16 核）」（review 4-12 全局
        // 措辞统一：设置弹窗·运行页签的「线程预算」下拉首项与行注同为「自动」）。断言本体
        // （自动档 = 按本机逻辑核数报读）不变；另补：旧措辞「自适应」必须从本页消失（断言只增）。
        if (budget != MainWindow::tr("自动（%1 核）").arg(cores))
            smoke_fail(MainWindow::tr("线程预算读数不符：%1（期望 自动（%2 核））")
                           .arg(budget)
                           .arg(cores));
        {
            auto *budget_label = page->findChild<QLabel *>(QStringLiteral("pp-run-budget"));
            const bool tip_ok =
                budget_label != nullptr &&
                budget_label->toolTip().contains(QStringLiteral("自动 = 按本机核心数"));
            QStringList stale;
            for (QLabel *label : page->findChildren<QLabel *>()) {
                const QVariant full =
                    label->property("ppFullText"); // 省略标签的全文（同 probe_full_text 口径）
                const QString text = full.isValid() ? full.toString() : label->text();
                if (text.contains(QStringLiteral("自适应")) ||
                    label->toolTip().contains(QStringLiteral("自适应")))
                    stale << label->objectName();
            }
            if (!tip_ok || !stale.isEmpty())
                smoke_fail(MainWindow::tr("线程预算措辞未统一（tip=%1 残留自适应=%2）")
                               .arg(tip_ok ? 1 : 0)
                               .arg(stale.isEmpty() ? QStringLiteral("<无>")
                                                    : stale.join(QLatin1Char(','))));
        }
        if (!QRegularExpression(QStringLiteral("^\\d+ / \\d+$")).match(workers).hasMatch() ||
            !worker_tip.contains(QStringLiteral("文件并行")))
            smoke_fail(MainWindow::tr("并行读数/分配提示不符：%1 / %2").arg(workers, worker_tip));
        if (hint != QStringLiteral("6 / 10 个输出"))
            smoke_fail(MainWindow::tr("总览输出计数不符：%1（期望 6 / 10 个输出）").arg(hint));
        if (percent.isEmpty() || percent == QStringLiteral("0%"))
            smoke_fail(MainWindow::tr("总览百分比未推进：%1").arg(percent));
        // 终态事实：总耗时（唯一时间指标）+ 汇总（按输出计；本探针态 = 成功 3 · 异常 2 · 失败 1）
        if (elapsed == QStringLiteral("—"))
            smoke_fail(MainWindow::tr("总耗时未推进：%1").arg(elapsed));
        if (!tally.contains(QStringLiteral("成功 3")) ||
            !tally.contains(QStringLiteral("异常 2")) || !tally.contains(QStringLiteral("失败 1")))
            smoke_fail(
                MainWindow::tr("汇总读数不符：%1（期望 成功 3 · 异常 2 · 失败 1）").arg(tally));
    }
    smoke_grab(shots_dir, kSmokeShots[kShotRunRows], &shell);

    // ---- (d) 取消态（唯一取消入口 = 运行页；取消后定稿为「已取消」）----
    {
        int cancel_hits = 0;
        const QMetaObject::Connection conn = QObject::connect(
            page, &PageRun::cancel_requested, page, [&cancel_hits]() { ++cancel_hits; });
        page->begin_run(
            2, QStringList{QStringLiteral("IMG_2731.jpg"), QStringLiteral("IMG_2732.jpg")});
        page->on_event(probe_progress({0, -1}, pp::Stage::Decode, 1.0f, false));
        page->on_event(probe_progress({0, 0}, pp::Stage::Encode, 0.42f, false));
        pump(120);
        auto *cancel = page->findChild<QPushButton *>(QStringLiteral("pp-run-cancel"));
        if (cancel == nullptr || !cancel->isEnabled()) {
            smoke_fail(MainWindow::tr("运行页取消钮不可用（pp-run-cancel）"));
        } else {
            cancel->click();
            pump(120);
            pp::RunSummary sum;
            sum.total = 2;
            sum.cancelled = 2;
            sum.total_ms = 900.0;
            page->on_event(probe_terminal(0, pp::FileState::Cancelled, pp::FileResult{}));
            page->on_event(probe_terminal(1, pp::FileState::Cancelled, pp::FileResult{}));
            page->end_run(sum, QString());
            pump(150);
            const QString pill = probe_full_text(page, QStringLiteral("pp-run-state-pill"));
            // M5-T9：行状态词 = 状态胶囊（pp-run-status-i-j；旧 stage 列并入胶囊/原因行）
            const QString row0 = probe_full_text(page, QStringLiteral("pp-run-status-0-0"));
            std::printf("UI-SMOKE run-cancel: hits=%d pill=\"%s\" row0=\"%s\" cancel-visible=%d\n",
                        cancel_hits, qUtf8Printable(pill), qUtf8Printable(row0),
                        cancel->isVisible() ? 1 : 0);
            std::fflush(stdout);
            if (cancel_hits != 1 || pill != QStringLiteral("已取消") || cancel->isVisible() ||
                row0 != QStringLiteral("已取消"))
                smoke_fail(MainWindow::tr("取消态不符（hits=%1 pill=%2 row0=%3 cancel-visible=%4）")
                               .arg(cancel_hits)
                               .arg(pill, row0)
                               .arg(cancel->isVisible() ? 1 : 0));
            smoke_grab(shots_dir, kSmokeShots[kShotRunCancel], &shell);
        }
        QObject::disconnect(conn);
    }

    // ---- (e) 浅色主题（07-run-light.png）：同一页 tokens 重放 ----
    {
        const theme::Tokens light = theme::tokens(theme::ThemeMode::Light);
        shell.setStyleSheet(theme::style_sheet(light));
        page->set_tokens(light);
        page->begin_run(
            4, QStringList{QStringLiteral("IMG_2731.jpg"), QStringLiteral("scan_007.png"),
                           QStringLiteral("P1010888.RW2.jpg"), QStringLiteral("IMG_2732.jpg")});
        page->on_event(probe_progress({0, -1}, pp::Stage::Decode, 1.0f, false));
        page->on_event(probe_progress({0, 0}, pp::Stage::Encode, 0.78f, false));
        page->on_event(probe_progress({0, 1}, pp::Stage::Encode, 0.52f, true));
        pump(250);
        QWidget *light_bar = page->findChild<QWidget *>(QStringLiteral("pp-run-bar-0-1"));
        const BarPixels light_synth =
            light_bar != nullptr ? probe_bar_pixels(light_bar->grab().toImage(), light,
                                                    light_bar->property("ppBarFrac").toDouble())
                                 : BarPixels{};
        std::printf("UI-SMOKE run-light: synth{samples=%d from=%.2f to=%.2f} window=%dx%d\n",
                    light_synth.samples, light_synth.from_ratio(), light_synth.to_ratio(),
                    shell.width(), shell.height());
        std::fflush(stdout);
        if (light_synth.samples == 0 || light_synth.from_ratio() < 0.2 ||
            light_synth.to_ratio() < 0.2)
            smoke_fail(MainWindow::tr("浅色主题下斜纹渲染不符（samples=%1 from=%2 to=%3）")
                           .arg(light_synth.samples)
                           .arg(light_synth.from_ratio(), 0, 'f', 2)
                           .arg(light_synth.to_ratio(), 0, 'f', 2));
        smoke_grab(shots_dir, kSmokeShots[kShotRunLight], &shell);
    }
    page->reset();
    pump(80);
}

// M5-T9（C7）：运行页 v6 化几何/语义值表（§7.3 三卡组 + R1–R4 + K4 四条 + 裁定 A7/A8/A9）。
// 值表驱动（行内 `checked=N bad=M`），逐条对应：
//   a) 总览 kv 行 pitch 28 + 标签轨 80（R1/R2）
//   b) 表行高 28 / 表头 24（R1）
//   c) 进度条 6（R4：总览合成条 + 分组条 + 逐输出条）
//   d) 日志尾等宽字族（§7.3）+ 4 行真实尾部 + 查看完整日志… + 状态自洽（修 3-14）
//   e) 状态胶囊单形态（完成绿「已完成」/ 0 字节琥珀 / 失败红 + 「└ 原因：…」行；A8 字形禁令）
//   f) 指标单源（无「已用时」第二出处；摘要句无指标；修 3-03/3-04）
//   g) 实现性文案出界面（任务 hint 逐字 + 术语入 tooltip；修 3-05）
//   h) 三级动作层级（链/危险钮/次钮）+ 按钮高 26（修 3-08 + A7）
//   i) 计数单源（逐文件行不再重复「N 个输出」；修 3-13） + A9（屏幕/DPI 变化失效缓存）
//   m) A29：峰值内存 kv = 引擎真实读数（RunSummary::peak_bytes；量级换档非「—」）
void MainWindow::Impl::smoke_probe_run_geometry() {
    PageRun page;
    page.set_tokens(tokens);
    page.resize(theme::Metrics::right_width, theme::Metrics::calibrated_h);
    page.show();
    PageRun::RunPlan plan;
    plan.format_ids = QStringList{QStringLiteral("jpeg"), QStringLiteral("webp")};
    plan.widths = QVector<int>{4032, 2480};
    plan.heights = QVector<int>{3024, 3508};
    plan.bytes = QVector<qint64>{1'310'720, 2'621'440};
    plan.stagger_ms = 150; // §8.1 默认（§9.3 设置域 0–2000）
    page.set_plan(plan);
    page.set_log_tail(
        QStringList{
            QStringLiteral("23:41:57.01 [info] [run] 开始批处理 · 2 个文件 × 2 个输出"),
            QStringLiteral("23:41:58.21 [info] [encode] IMG_2731.jpg → jpeg 412 KB ok"),
            QStringLiteral("23:41:58.34 [info] [encode] IMG_2731.jpg → webp 0 B palette"),
            QStringLiteral("23:41:58.35 [warn] [writer] webp 输出 0 字节 → 标记异常"),
            QStringLiteral("23:41:59.02 [error] [decode] scan_007.png 16→8 位降档被禁止"),
            QStringLiteral("23:41:59.10 [info] [run] 2 个输出已结算 · 成功 1 · 异常 1 · 失败 1"),
        },
        QStringLiteral("run-20260923-234157.log"));
    page.begin_run(2, QStringList{QStringLiteral("IMG_2731.jpg"), QStringLiteral("scan_007.png")});
    {
        // 文件 0：完成 + 一个 0 字节输出（修 3-10 的样本）；文件 1：失败（原因行样本）
        pp::FileResult done;
        done.info.width = 4032;
        done.info.height = 3024;
        done.out_bytes = 421888;
        done.outputs.resize(2);
        done.outputs[0].format_id = "jpeg";
        done.outputs[0].ok = true;
        done.outputs[0].out_bytes = 421888; // 412 KB
        done.outputs[0].t.encode_ms = 890;
        done.outputs[1].format_id = "webp";
        done.outputs[1].ok = true; // 0 字节
        page.on_event(probe_progress({0, -1}, pp::Stage::Decode, 1.0f, false));
        page.on_event(probe_terminal(0, pp::FileState::Done, done));

        pp::FileResult failed;
        failed.error = QStringLiteral("16→8 位降档被禁止").toStdString();
        failed.outputs.resize(2);
        failed.outputs[0].format_id = "jpeg";
        failed.outputs[0].error = QStringLiteral("16→8 位降档被禁止").toStdString();
        failed.outputs[1].format_id = "webp";
        failed.outputs[1].error = QStringLiteral("16→8 位降档被禁止").toStdString();
        page.on_event(probe_progress({1, -1}, pp::Stage::Decode, 1.0f, false));
        page.on_event(probe_terminal(1, pp::FileState::Failed, failed));
    }
    {
        // 终态（§7.3 第三行）：结束态读数 = 引擎 RunSummary 单源（总耗时/吞吐/平均/汇总）
        pp::RunSummary sum;
        sum.total = 2;
        sum.ok = 1;
        sum.failed = 1;
        sum.out_bytes = 421888;
        sum.total_ms = 9800.0;
        sum.throughput_mb_s = 86.0;
        sum.avg_file_ms = 344.0;
        sum.peak_bytes = 2'254'857'830ull; // A29：≈2.1 GB（像素预算峰值；读数断言见 (g)）
        page.end_run(sum, QStringLiteral("D:/Photos/export"));
    }
    pump(250);

    int checked = 0;
    QStringList bad;
    const auto expect = [&checked, &bad](const char *name, bool ok, const QString &detail) {
        ++checked;
        if (!ok)
            bad << QStringLiteral("%1(%2)").arg(QString::fromLatin1(name), detail);
    };
    const auto row_of = [&page](const QString &name) { return page.findChild<QWidget *>(name); };
    const auto label_text = [](QLabel *label) {
        if (label == nullptr)
            return QString();
        const QVariant full = label->property("ppFullText");
        return full.isValid() ? full.toString() : label->text();
    };
    const auto text_of = [&page, &label_text](const QString &name) {
        return label_text(page.findChild<QLabel *>(name));
    };
    const auto pill_of = [&page](const QString &name) {
        auto *label = page.findChild<QLabel *>(name);
        return label != nullptr ? static_cast<formkit::PillBadge *>(label) : nullptr;
    };
    const auto bar_kind = [&page](const QString &name) {
        QWidget *bar = page.findChild<QWidget *>(name);
        return bar != nullptr ? bar->property("ppBarKind").toString() : QString();
    };

    // ---- (a) 总览 kv 行 pitch 28 + 标签轨 80（R1/R2）----
    {
        const QStringList kv_rows = {
            QStringLiteral("pp-run-elapsed-row"), QStringLiteral("pp-run-avg-row"),
            QStringLiteral("pp-run-stagger-row"), QStringLiteral("pp-run-tally-row")};
        int seen = 0;
        int pitch_bad = 0;
        for (const QString &name : kv_rows) {
            QWidget *row = row_of(name);
            if (row == nullptr || !row->isVisible())
                continue;
            ++seen;
            if (row->height() != formkit::kRowPitch)
                ++pitch_bad;
        }
        const QStringList progress_rows = {QStringLiteral("pp-run-progress-row"),
                                           QStringLiteral("pp-run-ops-row")};
        for (const QString &name : progress_rows) {
            QWidget *row = row_of(name);
            if (row == nullptr || !row->isVisible())
                continue;
            ++seen;
            if (row->height() != formkit::kRowPitch)
                ++pitch_bad;
        }
        const QString label_hook = QString::fromLatin1(theme::kFkLabel);
        int labels_seen = 0;
        int labels_bad = 0;
        for (QLabel *label : page.findChildren<QLabel *>()) {
            if (label->property(theme::kFkProperty).toString() != label_hook)
                continue;
            ++labels_seen;
            if (label->width() != formkit::kLabelW)
                ++labels_bad;
        }
        expect("overview-kv-pitch",
               seen >= 6 && pitch_bad == 0 && labels_seen >= 6 && labels_bad == 0,
               QStringLiteral("rows=%1 pitch-bad=%2 labels=%3 label-bad=%4")
                   .arg(seen)
                   .arg(pitch_bad)
                   .arg(labels_seen)
                   .arg(labels_bad));
    }

    // ---- (b) 表行高 28（R1）：分组行 / 逐输出行 / 失败原因行；表头 24 ----
    {
        int pitch_bad = 0;
        QStringList seen_names;
        for (const char *name : {"pp-run-group-0", "pp-run-group-1", "pp-run-row-0-0",
                                 "pp-run-row-0-1", "pp-run-row-1-0", "pp-run-reason-1-0"}) {
            QWidget *row = row_of(QString::fromLatin1(name));
            if (row == nullptr) {
                ++pitch_bad;
                continue;
            }
            seen_names << QString::fromLatin1(name);
            if (row->height() != formkit::kRowPitch)
                ++pitch_bad;
        }
        QWidget *head = row_of(QStringLiteral("pp-run-thead"));
        const bool head_ok = head != nullptr && head->height() == 24; // 表头行（非信息行）
        expect("table-row-pitch", pitch_bad == 0 && head_ok && seen_names.size() == 6,
               QStringLiteral("rows=%1 bad=%2 head=%3")
                   .arg(seen_names.size())
                   .arg(pitch_bad)
                   .arg(head != nullptr ? head->height() : -1));
    }

    // ---- (c) 进度条高 6（R4）：总览合成条 + 分组条 + 逐输出条 ----
    {
        int bad_count = 0;
        int seen = 0;
        for (const char *name :
             {"pp-run-progress", "pp-run-gbar-0", "pp-run-bar-0-0", "pp-run-bar-1-0"}) {
            QWidget *bar = row_of(QString::fromLatin1(name));
            if (bar == nullptr)
                ++bad_count;
            ++seen;
            if (bar != nullptr && bar->height() != formkit::kProgressH)
                ++bad_count;
        }
        expect("progress-h6", bad_count == 0 && seen == 4,
               QStringLiteral("seen=%1 bad=%2 want=%3")
                   .arg(seen)
                   .arg(bad_count)
                   .arg(formkit::kProgressH));
    }

    // ---- (d) 日志尾：等宽字族（§7.3）+ 4 行真实尾部 + 查看完整日志… ----
    {
        QLabel *lines = page.findChild<QLabel *>(QStringLiteral("pp-run-log-lines"));
        const QString family = lines != nullptr ? lines->font().family() : QString();
        const int shown_lines =
            lines != nullptr ? static_cast<int>(lines->text().split(QStringLiteral("<br>")).size())
                             : 0;
        auto *open_full = page.findChild<QPushButton *>(QStringLiteral("pp-run-open-full-log"));
        expect("log-mono",
               family == QString::fromLatin1(theme::Typography::family_mono) && shown_lines == 4 &&
                   open_full != nullptr && open_full->text() == QStringLiteral("查看完整日志…") &&
                   open_full->property(theme::kFkProperty).toString() == QStringLiteral("link"),
               QStringLiteral("family=\"%1\" lines=%2 link=%3")
                   .arg(family)
                   .arg(shown_lines)
                   .arg(open_full != nullptr ? open_full->text() : QString()));
        // 修 3-14：有日志才有日志名；清空日志 → 名与尾部行同进退（不再自相矛盾）
        const QString name_with_log = text_of(QStringLiteral("pp-run-log-name"));
        page.set_log_tail(QStringList(), QStringLiteral("run-20260923-234157.log"));
        pump(60);
        const QString name_without_log = text_of(QStringLiteral("pp-run-log-name"));
        QLabel *empty_lines = page.findChild<QLabel *>(QStringLiteral("pp-run-log-lines"));
        const QString empty_text = empty_lines != nullptr ? empty_lines->text() : QString();
        expect("log-no-contradiction",
               name_with_log.endsWith(QStringLiteral(".log")) && name_without_log.isEmpty() &&
                   !empty_text.contains(QStringLiteral("（")),
               QStringLiteral("with=\"%1\" without=\"%2\" empty-has-paren=%3")
                   .arg(name_with_log, name_without_log)
                   .arg(empty_text.contains(QStringLiteral("（")) ? 1 : 0));
    }

    // ---- (e) 状态胶囊单形态（3-12 完成态唯一形态 / 3-10 0 字节琥珀 / 失败红 + 原因行）----
    {
        auto *done_pill = pill_of(QStringLiteral("pp-run-status-0-0"));
        auto *zero_pill = pill_of(QStringLiteral("pp-run-status-0-1"));
        auto *failed_pill = pill_of(QStringLiteral("pp-run-status-1-0"));
        auto *group_zero_pill = pill_of(QStringLiteral("pp-run-gstatus-0"));
        QLabel *reason = page.findChild<QLabel *>(QStringLiteral("pp-run-reason-text-1-0"));
        const QString reason_text = label_text(reason);
        const bool done_ok =
            done_pill != nullptr && done_pill->text() == QStringLiteral("已完成") &&
            done_pill->tone() == formkit::PillBadge::Tone::Ok && done_pill->has_icon();
        const bool zero_ok =
            zero_pill != nullptr && zero_pill->text() == QStringLiteral("0 字节") &&
            zero_pill->tone() == formkit::PillBadge::Tone::Warn && zero_pill->has_icon() &&
            bar_kind(QStringLiteral("pp-run-bar-0-1")) == QLatin1String("warn") // 禁绿色成功条
            && text_of(QStringLiteral("pp-run-size-0-1")) == QStringLiteral("0 B");
        const bool failed_ok =
            failed_pill != nullptr && failed_pill->text() == QStringLiteral("失败") &&
            failed_pill->tone() == formkit::PillBadge::Tone::Err && reason != nullptr &&
            reason->isVisible() && reason_text.startsWith(QStringLiteral("└ 原因：")) &&
            reason_text.contains(QStringLiteral("16→8 位降档被禁止"));
        const bool group_ok = group_zero_pill != nullptr &&
                              group_zero_pill->text() == QStringLiteral("异常") &&
                              group_zero_pill->tone() == formkit::PillBadge::Tone::Warn;
        // 卡头状态胶囊：结束且无失败 → 完成态唯一形态（绿胶囊 + 矢量勾）
        auto *head_pill = pill_of(QStringLiteral("pp-run-state-pill"));
        const bool head_ok = head_pill != nullptr &&
                             head_pill->text() == QStringLiteral("已完成 · 失败 2") &&
                             head_pill->tone() == formkit::PillBadge::Tone::Err;
        expect("status-pills", done_ok && zero_ok && failed_ok && group_ok && head_ok,
               QStringLiteral("done=%1 zero=%2 failed=%3 group=%4 head=%5 bar=%6 size=\"%7\" "
                              "reason=\"%8\"")
                   .arg(done_ok ? 1 : 0)
                   .arg(zero_ok ? 1 : 0)
                   .arg(failed_ok ? 1 : 0)
                   .arg(group_ok ? 1 : 0)
                   .arg(head_ok ? 1 : 0)
                   .arg(bar_kind(QStringLiteral("pp-run-bar-0-1")))
                   .arg(text_of(QStringLiteral("pp-run-size-0-1")), reason_text));
    }

    // ---- (f) 指标单源（修 3-03/3-04）：无「已用时」第二出处；摘要句不含任何指标 ----
    {
        bool elapsed_dup = false;
        bool impl_words = false;
        for (QLabel *label : page.findChildren<QLabel *>()) {
            const QString text = label_text(label);
            if (text.contains(QStringLiteral("已用时")))
                elapsed_dup = true;
            if (text.contains(QStringLiteral("阶段权重")) ||
                text.contains(QStringLiteral("行级子进度")))
                impl_words = true;
        }
        const QString summary = text_of(QStringLiteral("pp-run-summary"));
        const QString tally = text_of(QStringLiteral("pp-run-tally"));
        const QString elapsed = text_of(QStringLiteral("pp-run-elapsed"));
        const bool summary_clean = !summary.contains(QStringLiteral("总耗时")) &&
                                   !summary.contains(QStringLiteral("吞吐")) &&
                                   !summary.contains(QStringLiteral("MB/s"));
        expect("single-source",
               !elapsed_dup && !impl_words && summary_clean && elapsed != QStringLiteral("—") &&
                   tally.contains(QStringLiteral("成功 1")) &&
                   tally.contains(QStringLiteral("异常 1")) &&
                   tally.contains(QStringLiteral("失败 2")) &&
                   summary == QStringLiteral("运行结束 · 可打开输出目录查看结果"),
               QStringLiteral("elapsed-dup=%1 impl=%2 summary=\"%3\" elapsed=\"%4\" tally=\"%5\"")
                   .arg(elapsed_dup ? 1 : 0)
                   .arg(impl_words ? 1 : 0)
                   .arg(summary, elapsed, tally));
    }

    // ---- (g) 实现性文案出界面（修 3-05）：任务卡 hint 逐字 + 术语进 tooltip ----
    {
        const QString hint = text_of(QStringLiteral("pp-run-tasks-hint"));
        const QString stagger = text_of(QStringLiteral("pp-run-stagger"));
        auto *workers = page.findChild<QLabel *>(QStringLiteral("pp-run-workers"));
        auto *budget = page.findChild<QLabel *>(QStringLiteral("pp-run-budget"));
        auto *memory = page.findChild<QLabel *>(QStringLiteral("pp-run-memory"));
        const bool tips_ok = workers != nullptr && !workers->toolTip().isEmpty() &&
                             budget != nullptr && !budget->toolTip().isEmpty() &&
                             memory != nullptr && !memory->toolTip().isEmpty();
        expect("plain-language",
               hint == QStringLiteral("实心=已写盘 · 斜纹=合成中") &&
                   stagger == QStringLiteral("150 ms") && tips_ok,
               QStringLiteral("hint=\"%1\" stagger=\"%2\" tips=%3")
                   .arg(hint, stagger)
                   .arg(tips_ok ? 1 : 0));
    }

    // ---- (m) A29：峰值内存 kv = 引擎**真实读数**（RunSummary::peak_bytes 加性解冻）----
    //   口径：非「—」+ 量级换档格式（B→KB→MB→GB）；本探针喂 2'254'857'830 B → 「2.10 GB」。
    {
        const QString memory = text_of(QStringLiteral("pp-run-memory"));
        const QRegularExpression shape(QStringLiteral("^\\d+(?:\\.\\d+)? (?:KB|MB|GB)$"));
        expect("memory-peak",
               memory != QStringLiteral("—") && shape.match(memory).hasMatch() &&
                   memory == QStringLiteral("2.10 GB"),
               QStringLiteral("memory=\"%1\"").arg(memory));
    }

    // ---- (h) 操作行三级动作（修 3-08）+ 按钮高 26（R4/A7）----
    {
        auto *open_logs = page.findChild<QPushButton *>(QStringLiteral("pp-run-open-logs"));
        auto *open_output = page.findChild<QPushButton *>(QStringLiteral("pp-run-open-output"));
        auto *cancel = page.findChild<QPushButton *>(QStringLiteral("pp-run-cancel"));
        auto *again = page.findChild<QPushButton *>(QStringLiteral("pp-run-again"));
        const auto is_field = [](QPushButton *button, const char *value) {
            return button != nullptr &&
                   button->property(theme::kFkProperty).toString() == QString::fromLatin1(value);
        };
        // W-R-T13（A39 ②）机械换锚：破坏性动作由「红描边钮（danger）」改**红文字链**
        // （link-danger：border:none + color = 令牌 err）。断言本体（三级动作各一副面孔 +
        // 高 26 + 文案逐字）不变；补强（断言只增）：页 QSS 里该档规则必须成文且取 err 令牌，
        // **旧 danger 描边档必须已退役**（否定断言——旧形态不得回潮）。
        const bool tiers = is_field(open_logs, "link") && is_field(open_output, "link") &&
                           is_field(cancel, "link-danger") && is_field(again, "button");
        // UX-6⑤ 换锚（语义不变）：钩子轨并入 ppFk 后选择器文本改 ppFk；破坏性文字链仍须
        // 无框 + err 色（拆两次 contains，容忍公共规则里的中间修饰），旧 danger 描边档与
        // ppField/ppMetaField 两条退役轨都必须**不出现**（否定断言——旧形态不得回潮）。
        const QString sheet = page.styleSheet();
        const int rule_at =
            static_cast<int>(sheet.indexOf(QStringLiteral("QPushButton[ppFk=\"link-danger\"]")));
        const QString link_rule = rule_at >= 0 ? sheet.mid(rule_at, 200) : QString();
        const bool link_qss =
            rule_at >= 0 && link_rule.contains(QStringLiteral("border: none")) &&
            link_rule.contains(QStringLiteral("color: ") + theme::css_color(tokens.err));
        const bool danger_retired = !sheet.contains(QStringLiteral("=\"danger\"")) &&
                                    !sheet.contains(QStringLiteral("ppField")) &&
                                    !sheet.contains(QStringLiteral("ppMetaField"));
        // 文字链本体：无框（QSS 已成文）+ **不挂图标**（与另两条文字链同形；旧 danger 钮的 ■ 退役）
        const bool link_icons_off = cancel != nullptr && open_logs != nullptr &&
                                    open_output != nullptr && cancel->icon().isNull() &&
                                    open_logs->icon().isNull() && open_output->icon().isNull();
        const bool heights = cancel != nullptr && cancel->height() == theme::Metrics::btn_h &&
                             again != nullptr && again->height() == theme::Metrics::btn_h;
        const bool labels =
            open_logs != nullptr && open_logs->text() == QStringLiteral("查看日志…") &&
            open_output != nullptr && open_output->text() == QStringLiteral("打开输出目录") &&
            cancel != nullptr && cancel->text() == QStringLiteral("取消剩余") && again != nullptr &&
            again->text() == QStringLiteral("再次运行");
        expect("action-tiers",
               tiers && heights && labels && link_qss && danger_retired && link_icons_off,
               QStringLiteral("tiers=%1 h=%2/%3 labels=%4 qss=%5 danger-retired=%6 icon-off=%7")
                   .arg(tiers ? 1 : 0)
                   .arg(cancel != nullptr ? cancel->height() : -1)
                   .arg(again != nullptr ? again->height() : -1)
                   .arg(labels ? 1 : 0)
                   .arg(link_qss ? 1 : 0)
                   .arg(danger_retired ? 1 : 0)
                   .arg(link_icons_off ? 1 : 0));
        // 接线：结束态「再次运行」可点且**只发意图**（页不含调度；回收 = MainWindow 的开始运行）
        int again_hits = 0;
        const QMetaObject::Connection again_conn = QObject::connect(
            &page, &PageRun::run_again_requested, &page, [&again_hits]() { ++again_hits; });
        const bool again_ready = again != nullptr && again->isEnabled();
        if (again_ready)
            again->click();
        pump(60);
        QObject::disconnect(again_conn);
        expect("again-wiring", again_ready && again_hits == 1,
               QStringLiteral("ready=%1 hits=%2").arg(again_ready ? 1 : 0).arg(again_hits));
    }

    // ---- (j) 单源计数（修 3-13）：逐文件行不再重复「N 个输出」----
    {
        int group_meta_dup = 0;
        for (const char *name : {"pp-run-gname-0", "pp-run-gname-1"}) {
            if (text_of(QString::fromLatin1(name)).contains(QStringLiteral("个输出")))
                ++group_meta_dup;
        }
        const QString hint = text_of(QStringLiteral("pp-run-progress-hint"));
        expect("count-single", group_meta_dup == 0 && hint == QStringLiteral("4 / 4 个输出"),
               QStringLiteral("dup=%1 hint=\"%2\"").arg(group_meta_dup).arg(hint));
    }

    // ---- (l) 0 字节条的像素取证（修 3-10「禁绿色成功条」）：琥珀在场、绿不得出现 ----
    {
        const theme::Tokens base = tokens;
        const QColor ok_color = base.ok;
        const QColor warn_color = base.warn;
        QWidget *zero_bar = row_of(QStringLiteral("pp-run-bar-0-1"));
        const auto min_distance = [&zero_bar](const QColor &want) {
            if (zero_bar == nullptr)
                return 1e9;
            const QImage img = zero_bar->grab().toImage();
            int best = 1 << 30;
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    const QColor px = img.pixelColor(x, y);
                    const int dr = px.red() - want.red();
                    const int dg = px.green() - want.green();
                    const int db = px.blue() - want.blue();
                    best = std::min(best, dr * dr + dg * dg + db * db);
                }
            }
            return best < (1 << 30) ? std::sqrt(double(best)) : 1e9;
        };
        const double ok_dist = min_distance(ok_color);     // 绿 = 真完成（本条不得出现）
        const double warn_dist = min_distance(warn_color); // 琥珀 = 异常（本条的填充色）
        expect("zero-bar-amber", ok_dist > 30.0 && warn_dist < 24.0,
               QStringLiteral("ok-dist=%.1f warn-dist=%.1f").arg(ok_dist).arg(warn_dist));
    }

    // ---- (k) A9：屏幕/DPI 变化失效缓存（RunMarkLabel 按新 dpr 重光栅化）----
    {
        auto *mark = static_cast<QLabel *>(row_of(QStringLiteral("pp-run-mark-0-0")));
        const qint64 before = mark != nullptr ? mark->pixmap().cacheKey() : 0;
        if (mark != nullptr) {
            QEvent screen_event(QEvent::ScreenChangeInternal); // A9 的 QEvent::ScreenChange 语义
            QCoreApplication::sendEvent(mark, &screen_event);
        }
        pump(60);
        const qint64 after = mark != nullptr ? mark->pixmap().cacheKey() : 0;
        expect("dpr-invalidate", before != 0 && after != 0 && after != before,
               QStringLiteral("before=%1 after=%2").arg(before).arg(after));
    }

    std::printf("UI-SMOKE run-geom: checked=%d bad=%d pane=%dx%d rows=%d %s\n", checked,
                int(bad.size()), page.width(), page.height(),
                int(page.findChildren<QWidget *>(
                            QRegularExpression(QStringLiteral("^pp-run-(group|row|reason)-\\d+")))
                        .size()),
                qUtf8Printable(bad.join(QLatin1Char(' '))));
    std::fflush(stdout);
    if (!bad.isEmpty()) {
        smoke_fail(MainWindow::tr("运行页几何/语义（§7.3/R1–R4/K4）不符：%1")
                       .arg(bad.join(QLatin1Char(' '))));
    }
    page.hide();
    pump(80);
}

// ---------------------------------------------------------------------------
// M5-T13（W-G4 验收，C7 探针只增）：§12-13 亮暗布局等价 + §12-14 卡头 hint 禁折行。
//  · theme-equiv：同一窗口状态下 Dark→Light 两次落地，全部 pp-* 控件的
//    (objectName, 窗口坐标几何, 可见性) 逐项一致 —— §9「布局零差异：两主题仅换 §4 令牌」。
//  · hint-nowrap：全部 *-hint（含 pp-card-hint）标签禁折行、高 = 单行；文本要么放得下，
//    要么以省略号结尾（§12-14「卡头 hint nowrap+省略号」= ElidedLabel 语义，无静默裁字）。
// 探针结束时按进入时的档位归位（其余探针/截图口径不变）。
// ---------------------------------------------------------------------------
void MainWindow::Impl::smoke_probe_verification() {
    struct Snap {
        QString key;
        QRect geom;
        bool visible = false;
    };
    const auto snapshot = [this]() {
        QList<Snap> out;
        for (QWidget *child : w->findChildren<QWidget *>()) {
            const QString name = child->objectName();
            if (!name.startsWith(QStringLiteral("pp-")))
                continue;
            Snap s;
            s.key = name;
            s.geom = QRect(child->mapTo(w, QPoint(0, 0)), child->size());
            s.visible = child->isVisible();
            out.append(s);
        }
        std::sort(out.begin(), out.end(), [](const Snap &a, const Snap &b) {
            if (a.key != b.key)
                return a.key < b.key;
            if (a.geom.x() != b.geom.x())
                return a.geom.x() < b.geom.x();
            if (a.geom.y() != b.geom.y())
                return a.geom.y() < b.geom.y();
            if (a.geom.width() != b.geom.width())
                return a.geom.width() < b.geom.width();
            if (a.geom.height() != b.geom.height())
                return a.geom.height() < b.geom.height();
            return int(a.visible) < int(b.visible);
        });
        return out;
    };
    const auto rect_text = [](const Snap &s) {
        return QStringLiteral("%1@%2,%3 %4x%5 v%6")
            .arg(s.key)
            .arg(s.geom.x())
            .arg(s.geom.y())
            .arg(s.geom.width())
            .arg(s.geom.height())
            .arg(s.visible ? 1 : 0);
    };

    int checked = 0;
    QStringList bad;
    const auto expect = [&checked, &bad](const char *name, bool ok, const QString &detail) {
        ++checked;
        if (!ok)
            bad << QStringLiteral("%1(%2)").arg(QString::fromLatin1(name), detail);
    };

    int equiv_n = 0; // M5-T13：比对到的 pp-* 控件数（随行打印，供取证非空）
    int hint_n = 0;  // M5-T13：比对到的可见 hint 数
    int nums_n = 0;  // M5-T13：比对到的数值面（spin/数值列/kv 值）数

    // ---- (a) 亮暗布局等价（§12-13）----
    {
        wait_thumbs(kThumbWaitMs);
        const theme::ThemeMode want = theme_mode;
        set_theme_mode(theme::ThemeMode::Dark);
        pump(250);
        const QList<Snap> dark = snapshot();
        set_theme_mode(theme::ThemeMode::Light);
        pump(250);
        const QList<Snap> light = snapshot();
        set_theme_mode(want); // 归位
        pump(250);
        equiv_n = int(dark.size());
        bool same = dark.size() == light.size();
        QString detail;
        if (!same) {
            detail = QStringLiteral("count=%1/%2").arg(dark.size()).arg(light.size());
        } else {
            for (int i = 0; i < dark.size(); ++i) {
                if (dark[i].key != light[i].key || dark[i].geom != light[i].geom ||
                    dark[i].visible != light[i].visible) {
                    same = false;
                    detail =
                        QStringLiteral("%1 vs %2").arg(rect_text(dark[i]), rect_text(light[i]));
                    break;
                }
            }
        }
        expect("theme-equiv-widgets", same && dark.size() >= 20,
               QStringLiteral("n=%1/%2 %3").arg(dark.size()).arg(light.size()).arg(detail));
    }

    // ---- (b) 卡头/字段 hint：禁折行 + 单行高 + 放不下必须省略号（§12-14）----
    {
        int hints = 0;
        int hints_bad = 0;
        QStringList detail;
        for (QLabel *label : w->findChildren<QLabel *>()) {
            const QString name = label->objectName();
            if (!(name.endsWith(QStringLiteral("-hint")) || name == QStringLiteral("pp-card-hint")))
                continue;
            if (!label->isVisible() || label->text().isEmpty())
                continue;
            ++hints;
            ++hint_n;
            const QFontMetrics fm(label->font());
            const bool single_line = label->height() <= fm.height() + 8;
            const bool no_wrap = !label->wordWrap();
            const bool fits_or_elided = fm.horizontalAdvance(label->text()) <= label->width() + 1 ||
                                        label->text().endsWith(QChar(0x2026));
            if (!(single_line && no_wrap && fits_or_elided)) {
                ++hints_bad;
                detail << QStringLiteral("%1 %2x%3 \"%4\"")
                              .arg(name)
                              .arg(label->width())
                              .arg(label->height())
                              .arg(label->text());
            }
        }
        expect("hint-nowrap", hints >= 2 && hints_bad == 0,
               QStringLiteral("hints=%1 bad=%2 [%3]")
                   .arg(hints)
                   .arg(hints_bad)
                   .arg(detail.join(QStringLiteral("; "))));
    }

    // ---- (c) §12-8 数值右对齐 + tabular（此前无独立探针）----
    //  判据按规范分层（§4.2「数值」行 + mockup tokens.css .kv-v）：
    //   * 右对齐 + tnum：全部 spinbox（R4「值区 flex 右对齐」，formkit 单源落地）与运行页
    //     数值列（§7.3「大小/速度/耗时（右对齐）」）；
    //   * 仅 tnum：全部 kv 值标签（ppFk=value）—— mockup .kv-v 只声明 tabular-nums、不设
    //     text-align（kv 值档不要求右对齐；formkit::value_label 的右对齐是数值 kv 的选择）。
    {
        int nums = 0;
        int nums_bad = 0;
        QStringList detail;
        const auto check_numeric = [&](Qt::Alignment alignment, const QFont &font,
                                       const QString &name, const QString &why,
                                       bool require_right) {
            ++nums;
            const bool right = (alignment & Qt::AlignRight) != 0;
            const bool tnum = font.featureValue(QFont::Tag("tnum")) != 0;
            if (!(right || !require_right) || !tnum) {
                ++nums_bad;
                detail << QStringLiteral("%1:%2(right=%3 tnum=%4)")
                              .arg(why, name)
                              .arg(right ? 1 : 0)
                              .arg(tnum ? 1 : 0);
            }
        };
        for (QAbstractSpinBox *spin : w->findChildren<QAbstractSpinBox *>())
            check_numeric(spin->alignment(), spin->font(), spin->objectName(),
                          QStringLiteral("spin"), /*require_right=*/true);
        for (QLabel *label : w->findChildren<QLabel *>()) {
            if (label->property(theme::kFkProperty).toString() == QLatin1String(theme::kFkValue))
                check_numeric(label->alignment(), label->font(), label->objectName(),
                              QStringLiteral("kv"), /*require_right=*/false);
        }
        for (QLabel *label : w->findChildren<QLabel *>()) {
            const QString name = label->objectName();
            if (name.startsWith(QStringLiteral("pp-run-size-")) ||
                name.startsWith(QStringLiteral("pp-run-speed-")) ||
                name.startsWith(QStringLiteral("pp-run-time-")))
                check_numeric(label->alignment(), label->font(), name, QStringLiteral("numcol"),
                              /*require_right=*/true);
        }
        nums_n = nums;
        expect("num-align", nums >= 12 && nums_bad == 0,
               QStringLiteral("nums=%1 bad=%2 [%3]")
                   .arg(nums)
                   .arg(nums_bad)
                   .arg(detail.join(QStringLiteral("; "))));
    }

    std::printf("UI-SMOKE theme-equiv+nowrap: checked=%d bad=%d equiv-pp-widgets=%d hints=%d "
                "nums=%d %s\n",
                checked, int(bad.size()), equiv_n, hint_n, nums_n,
                qUtf8Printable(bad.join(QLatin1Char(' '))));
    std::fflush(stdout);
    if (!bad.isEmpty())
        smoke_fail(MainWindow::tr("§12-13/§12-14 探针（亮暗布局等价 / hint 禁折行）不符：%1")
                       .arg(bad.join(QLatin1Char(' '))));
}

// ===========================================================================
// M7-R2E2 · 第 3 波常驻探针（只增；冻结断言面 / 截图清单 / 冻结行逐字不动）
// 需求来源：build/m7r2-d1/handoff.md ⑤（context-menu / check-all）、
// build/m7r2-d2/handoff.md ⑤（context-menu / R-070 / R-078 / UX-8）、UX-10.1、R-218。
// 口径（M5-A26）：逐探针一行自检 `UI-SMOKE <tag>:`；不新增截图（shots=12 冻结）。
// ===========================================================================

namespace {

// UX-1：向宿主发 ContextMenu 事件 → attach_card_menu 的槽构造菜单（父 = 宿主）→ 取回断言。
// 菜单 WA_DeleteOnClose：探针断言完 close() 即销毁，不留弹窗。
QMenu *popup_card_menu(QWidget *host) {
    if (host == nullptr)
        return nullptr;
    const auto last_menu = [host] {
        // 「按分类打标 →」子菜单也是 host 的 findChildren 结果 —— 取**顶层**菜单（父 == host）
        const QList<QMenu *> menus = host->findChildren<QMenu *>();
        for (QMenu *m : menus) {
            if (m->parentWidget() == host)
                return m;
        }
        return menus.isEmpty() ? nullptr : menus.first();
    };
    const auto send_context = [](QWidget *target) {
        QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(4, 4));
        QApplication::sendEvent(target, &ev);
    };
    send_context(host);
    QMenu *menu = last_menu();
    if (menu != nullptr)
        return menu;
    // QAbstractScrollArea（文件列表树）：ContextMenu 的自然落点是 viewport —— 补投一次
    if (auto *scroll = qobject_cast<QAbstractScrollArea *>(host)) {
        send_context(scroll->viewport());
        menu = last_menu();
    }
    return menu;
}

// attach_card_menu 的宿主 = 自身/祖先中 contextMenuPolicy == CustomContextMenu 的最近者
QWidget *menu_host_of(QWidget *from) {
    for (QWidget *w = from; w != nullptr; w = w->parentWidget()) {
        if (w->contextMenuPolicy() == Qt::CustomContextMenu)
            return w;
    }
    return nullptr;
}

// 卡菜单断言：非分隔项逐条对齐 contextmenu 构造面（词表单源）+ 首项文案钉字面量（防回归）
// M7R21F4-0（NOLINT 处置翻案，替代 R2G 的行级消音）：checked/bad = 断言计数出参对
// （核过数/坏数），同为 int& 易混 —— 按「同型形参对 → 参数组结构化」收拢成命名组 Tally，
// 探针输出行（UI-SMOKE … checked=/bad=）逐字不变。
struct Tally {
    int checked = 0;
    int bad = 0;
};
void expect_card_menu(const QString &tag, QWidget *host, const QString &first_text,
                      const std::vector<contextmenu::Item> &want,
                      const std::function<void(const QString &)> &fail, Tally &tally) {
    const auto expect = [&](bool ok, const QString &what) {
        ++tally.checked;
        if (!ok) {
            ++tally.bad;
            fail(QStringLiteral("context-menu：%1 %2").arg(tag, what));
        }
    };
    QMenu *menu = popup_card_menu(host);
    expect(menu != nullptr, QStringLiteral("菜单未弹出"));
    if (menu == nullptr)
        return;
    QStringList got;
    for (QAction *action : menu->actions()) {
        if (!action->isSeparator())
            got << action->text();
    }
    QStringList want_texts;
    for (const contextmenu::Item &item : want) {
        if (!item.separator)
            want_texts << item.text;
    }
    expect(got == want_texts,
           QStringLiteral("项集合 %1（期望 %2）")
               .arg(got.join(QLatin1Char('/')), want_texts.join(QLatin1Char('/'))));
    expect(!got.isEmpty() && got.first() == first_text,
           QStringLiteral("首项 %1（期望 %2）")
               .arg(got.isEmpty() ? QStringLiteral("<空>") : got.first(), first_text));
    // 补强（M7R22G1-8）：分隔线**全序**逐位（want 的 separator 旗标序 == 菜单实际）——
    // 原判据跳过分隔线只比文本序列；分隔线落位自此钉住。
    QStringList want_full;
    for (const contextmenu::Item &item : want)
        want_full << (item.separator ? QStringLiteral("<sep>") : item.text);
    QStringList got_full;
    for (QAction *action : menu->actions())
        got_full << (action->isSeparator() ? QStringLiteral("<sep>") : action->text());
    expect(got_full == want_full,
           QStringLiteral("含分隔线全序 %1（期望 %2）")
               .arg(got_full.join(QLatin1Char('/')), want_full.join(QLatin1Char('/'))));
    menu->close();
    expect(!menu->isVisible(), QStringLiteral("菜单已收（无残留弹层）"));
}

// —— G1 探针总集公共件（M7R22G1-3/-4）：渲染取样与墨迹盒 ——
// （源 = build/m7r21-f2/rtl_probe.cpp 临时探针同款常驻化；只渲染**局部实例**，不触主窗）
// 出参一律结构化（同型出参对会被 bugprone-easily-swappable-parameters 点名，F4 先例 Tally）。

// 自由函数版 pump（Impl::pump 同体；G1 探针是自由函数，不入 mainwindow_impl.h）
void smoke_pump(int ms) {
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    } while (timer.elapsed() < ms);
}

QImage smoke_render_widget(QWidget &w) {
    w.ensurePolished();
    QImage img(w.size(), QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter painter(&img);
    w.render(&painter);
    painter.end();
    return img;
}

struct SmokeSpan {
    int lo = -1;
    int hi = -1;
    bool found = false;
};

struct SmokeBox {
    int x0 = -1;
    int y0 = -1;
    int x1 = -1;
    int y1 = -1;
    bool found = false;
};

// 星形着色像素（accent ±40 通道带）的水平包围盒
void smoke_accent_bbox(const QImage &img, const QColor &accent, SmokeSpan *out) {
    SmokeSpan span;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (std::abs(c.red() - accent.red()) < 40 &&
                std::abs(c.green() - accent.green()) < 40 &&
                std::abs(c.blue() - accent.blue()) < 40 && c.alpha() > 200) {
                if (!span.found || x < span.lo)
                    span.lo = x;
                if (!span.found || x > span.hi)
                    span.hi = x;
                span.found = true;
            }
        }
    }
    if (out != nullptr)
        *out = span;
}

// 近白像素（HUD 比例尺盒）的包围盒
void smoke_white_bbox(const QImage &img, SmokeBox *out) {
    SmokeBox box;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() >= 235 && c.green() >= 235 && c.blue() >= 235) {
                if (!box.found || x < box.x0)
                    box.x0 = x;
                if (!box.found || y < box.y0)
                    box.y0 = y;
                if (!box.found || x > box.x1)
                    box.x1 = x;
                if (!box.found || y > box.y1)
                    box.y1 = y;
                box.found = true;
            }
        }
    }
    if (out != nullptr)
        *out = box;
}

// 文本墨迹盒（非白像素）的水平包围盒 —— rtl-deep 的渲染取样
void smoke_ink_span(const QImage &img, SmokeSpan *out) {
    SmokeSpan span;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() < 235 || c.green() < 235 || c.blue() < 235) {
                if (!span.found || x < span.lo)
                    span.lo = x;
                if (!span.found || x > span.hi)
                    span.hi = x;
                span.found = true;
            }
        }
    }
    if (out != nullptr)
        *out = span;
}

} // namespace

// —— R-218：RTL 逻辑坐标/镜像（rtl-mirror）：镜像关系 + 逻辑边距随向，LTR 逐字节不变 ——
void MainWindow::Impl::smoke_probe_rtl_mirror() {
    int checked = 0;
    int bad = 0;
    const auto expect = [&](bool ok, const QString &what) {
        ++checked;
        if (!ok) {
            ++bad;
            smoke_fail(MainWindow::tr("rtl-mirror：%1").arg(what));
        }
    };
    const QRect row(0, 0, 320, FileDelegate::kFileRowHeight);
    const auto mirror = [&row](const QRect &r) {
        return QRect(row.left() + row.right() - r.right(), r.top(), r.width(), r.height());
    };
    // 1) 行几何单源重载（FileDelegate::row_rects）：RTL 四矩形 = LTR 绕行矩形中线镜像
    const FileDelegate::RowRects ltr = FileDelegate::row_rects(row, 48, true);
    const FileDelegate::RowRects rtl = FileDelegate::row_rects(row, 48, true, true);
    expect(rtl.thumb == mirror(ltr.thumb), QStringLiteral("row_rects.thumb 镜像"));
    expect(rtl.pill == mirror(ltr.pill), QStringLiteral("row_rects.pill 镜像"));
    expect(rtl.text == mirror(ltr.text), QStringLiteral("row_rects.text 镜像"));
    expect(rtl.flag == mirror(ltr.flag), QStringLiteral("row_rects.flag 镜像"));
    const FileDelegate::RowRects same = FileDelegate::row_rects(row, 48, true, false);
    expect(same.thumb == ltr.thumb && same.pill == ltr.pill && same.text == ltr.text &&
               same.flag == ltr.flag,
           QStringLiteral("row_rects(rtl=false) 与三参版逐值相同"));
    // 2) 勾选框几何（绘制与命中同一条）
    const QRect check_ltr = FileGroupDelegate::check_rect(row);
    expect(FileGroupDelegate::check_rect(row, true) == mirror(check_ltr),
           QStringLiteral("check_rect 镜像"));
    expect(FileGroupDelegate::check_rect(row, false) == check_ltr,
           QStringLiteral("check_rect(rtl=false) 与单参版逐值相同"));
    // 3) 逻辑边距（ui/leading_indent.h 单源）：方向切换重放（LTR 值 = 原 setContentsMargins）
    {
        QWidget scratch;
        auto *lay = new QHBoxLayout(&scratch);
        set_logical_margins(&scratch, 8, 4);
        QMargins m = lay->contentsMargins();
        expect(m.left() == 8 && m.right() == 4 && m.top() == 0 && m.bottom() == 0,
               QStringLiteral("逻辑边距 LTR 落 leading"));
        scratch.setLayoutDirection(Qt::RightToLeft);
        m = lay->contentsMargins();
        expect(m.left() == 4 && m.right() == 8, QStringLiteral("逻辑边距 RTL 镜像重放"));
    }
    {
        QWidget scratch; // 无布局 → widget 路径（page_run 行缩进 / page_output 对轨缩进）
        set_leading_indent(&scratch, 18);
        QMargins m = scratch.contentsMargins();
        expect(m.left() == 18 && m.right() == 0, QStringLiteral("行首缩进 LTR"));
        scratch.setLayoutDirection(Qt::RightToLeft);
        m = scratch.contentsMargins();
        expect(m.left() == 0 && m.right() == 18, QStringLiteral("行首缩进 RTL 镜像"));
    }
    // —— 扩充四查（M7R22G1-3；F2⑤-1 常驻化：build/m7r21-f2/rtl_probe.cpp 临时探针的四查）——
    //   ① stars-mirror：RatingStars 星位（逻辑首端）LTR↔RTL 水平镜像；
    //   ② hud-mirror：MapWidget 自绘 HUD 比例尺盒镜像（盒宽随字体回退变化 ⇒ 判据全为
    //      关系式 + 左缘锚，不钉绝对宽；F2 实测注记「盒宽受缺失字体回退放大」）；
    //   ③ logical-align 读回：DeltaSpin/unit_slot/note_label 的逻辑对齐旗标；
    //   ④ geom-mirror：PageMeta 固定宽控件（DeltaSpin）行内 LTR↔RTL 镜像。
    //  R-044 / M7R22G1-3 容差：像素/几何镜像判据 ±2px（字体敏感几何档）；关系式仍钉语义。
    //  四查全部用**局部实例**（RatingStars/MapWidget/PageMeta/scratch），不触主窗布局方向。
    int sub_stars_bad = 0;
    int sub_hud_bad = 0;
    int sub_align_bad = 0;
    int sub_geom_bad = 0;
    const auto sub = [&](bool ok, int *bucket, const QString &what) {
        ++checked;
        if (!ok) {
            ++bad;
            ++*bucket;
            smoke_fail(MainWindow::tr("rtl-mirror：%1").arg(what));
        }
    };
    { // ① stars-mirror：rating=1 → 仅逻辑首星着色；着色盒 = LTR 盒的水平镜像
        meta::RatingStars ltr;
        meta::RatingStars rtl;
        ltr.setLayoutDirection(Qt::LeftToRight);
        rtl.setLayoutDirection(Qt::RightToLeft);
        ltr.set_tokens(tokens);
        rtl.set_tokens(tokens);
        ltr.set_rating(1);
        rtl.set_rating(1);
        ltr.resize(ltr.sizeHint());
        rtl.resize(rtl.sizeHint());
        const QImage a = smoke_render_widget(ltr);
        const QImage b = smoke_render_widget(rtl);
        SmokeSpan sa;
        SmokeSpan sb;
        smoke_accent_bbox(a, tokens.accent, &sa);
        smoke_accent_bbox(b, tokens.accent, &sb);
        const int ww = a.width();
        const bool ok = sa.found && sb.found && std::abs(sb.lo - (ww - 1 - sa.hi)) <= 2 &&
                        std::abs(sb.hi - (ww - 1 - sa.lo)) <= 2 && std::abs(ww - b.width()) <= 2;
        sub(ok, &sub_stars_bad,
            QStringLiteral("stars-mirror w=%1 ltr=[%2,%3] rtl=[%4,%5] mirror=[%6,%7]")
                .arg(ww)
                .arg(sa.lo)
                .arg(sa.hi)
                .arg(sb.lo)
                .arg(sb.hi)
                .arg(ww - 1 - sa.hi)
                .arg(ww - 1 - sa.lo));
    }
    { // ② hud-mirror：离线 MapWidget（provider 空 = 仅比例尺盒；banner 隐藏不判）
        pp::map::MapWidget ltr;
        pp::map::MapWidget rtl;
        ltr.set_offline(true);
        rtl.set_offline(true);
        for (QLabel *l : ltr.findChildren<QLabel *>())
            l->hide();
        for (QLabel *l : rtl.findChildren<QLabel *>())
            l->hide();
        ltr.resize(400, 300);
        rtl.resize(400, 300);
        ltr.setLayoutDirection(Qt::LeftToRight);
        rtl.setLayoutDirection(Qt::RightToLeft);
        const QImage a = smoke_render_widget(ltr);
        const QImage b = smoke_render_widget(rtl);
        SmokeBox ba;
        SmokeBox bb;
        smoke_white_bbox(a, &ba);
        smoke_white_bbox(b, &bb);
        const bool ok = ba.found && bb.found && std::abs(bb.x0 - (400 - 1 - ba.x1)) <= 2 &&
                        std::abs(bb.x1 - (400 - 1 - ba.x0)) <= 2 && std::abs(bb.y0 - ba.y0) <= 2 &&
                        std::abs(bb.y1 - ba.y1) <= 2 &&
                        std::abs((ba.x1 - ba.x0) - (bb.x1 - bb.x0)) <= 2 &&
                        std::abs(ba.x0 - 6) <= 2 && std::abs(bb.x1 - (400 - 1 - 6)) <= 2;
        sub(ok, &sub_hud_bad,
            QStringLiteral("hud-mirror box ltr=[%1,%2,%3,%4] rtl=[%5,%6,%7,%8]")
                .arg(ba.x0)
                .arg(ba.y0)
                .arg(ba.x1)
                .arg(ba.y1)
                .arg(bb.x0)
                .arg(bb.y0)
                .arg(bb.x1)
                .arg(bb.y1));
    }
    { // ③ logical-align 读回：对齐一律逻辑旗标（F2 对齐逻辑化的存量读回）
        meta::DeltaSpin spin(nullptr, QStringLiteral("probe-rtl-spin"));
        sub(spin.alignment() == (Qt::AlignTrailing | Qt::AlignVCenter), &sub_align_bad,
            QStringLiteral("DeltaSpin 对齐 = AlignTrailing（got 0x%1）")
                .arg(unsigned(spin.alignment()), 0, 16));
        QLabel *unit = meta::unit_slot(nullptr, QStringLiteral("u"));
        sub(unit->alignment() == (Qt::AlignLeading | Qt::AlignVCenter), &sub_align_bad,
            QStringLiteral("unit_slot 对齐 = AlignLeading（got 0x%1）")
                .arg(unsigned(unit->alignment()), 0, 16));
        meta::ElidedLabel *note = meta::note_label(nullptr);
        sub(note->alignment() == (Qt::AlignLeading | Qt::AlignVCenter), &sub_align_bad,
            QStringLiteral("note_label 对齐 = AlignLeading（got 0x%1）")
                .arg(unsigned(note->alignment()), 0, 16));
        delete unit;
        delete note;
    }
    { // ④ geom-mirror：PageMeta 固定宽控件（DeltaSpin）行内镜像（自相对关系，±2px）
        PageMeta ltr;
        PageMeta rtl;
        ltr.set_tokens(tokens);
        rtl.set_tokens(tokens);
        ltr.resize(800, 1200);
        rtl.resize(800, 1200);
        ltr.setLayoutDirection(Qt::LeftToRight);
        rtl.setLayoutDirection(Qt::RightToLeft);
        const auto activate_all = [](QWidget &root) {
            if (root.layout() != nullptr)
                root.layout()->activate();
            for (QWidget *child : root.findChildren<QWidget *>()) {
                if (child->layout() != nullptr)
                    child->layout()->activate();
            }
        };
        activate_all(ltr);
        activate_all(rtl);
        pump(60);
        activate_all(ltr);
        activate_all(rtl);
        const QString names[] = {meta::objname::kDeltaYears, meta::objname::kDeltaHours};
        for (const QString &name : names) {
            const QString qn = name;
            QWidget *wl = ltr.findChild<QWidget *>(qn);
            QWidget *wr = rtl.findChild<QWidget *>(qn);
            if (wl == nullptr || wr == nullptr) {
                sub(false, &sub_geom_bad, QStringLiteral("geom-mirror %1 缺失").arg(qn));
                continue;
            }
            const QRect gl = wl->geometry();
            const QRect gr = wr->geometry();
            const int row_w = wl->parentWidget() != nullptr ? wl->parentWidget()->width() : -1;
            const int row_wr = wr->parentWidget() != nullptr ? wr->parentWidget()->width() : -1;
            const int mirrored_x = row_w - gl.x() - gl.width();
            sub(row_w == row_wr && std::abs(gr.x() - mirrored_x) <= 2 && gr.width() == gl.width() &&
                    gr.y() == gl.y() && gr.height() == gl.height(),
                &sub_geom_bad,
                QStringLiteral("geom-mirror %1 ltr=(%2,%3 %4x%5 row=%6) rtl=(%7,%8 %9x%10 row=%11) "
                               "mirror-x=%12")
                    .arg(qn)
                    .arg(gl.x())
                    .arg(gl.y())
                    .arg(gl.width())
                    .arg(gl.height())
                    .arg(row_w)
                    .arg(gr.x())
                    .arg(gr.y())
                    .arg(gr.width())
                    .arg(gr.height())
                    .arg(row_wr)
                    .arg(mirrored_x));
        }
    }
    std::printf("UI-SMOKE rtl-mirror: checked=%d bad=%d stars=%d hud=%d align=%d geom=%d\n",
                checked, bad, sub_stars_bad, sub_hud_bad, sub_align_bad, sub_geom_bad);
    std::fflush(stdout);
}

// —— R-189/192：format_selection_model 往返（M7R22G1-5；m7r2-e2 ⑤-5 / F3 ⑤-1 常驻化）——
//   「4 sync 坍缩为 reproject」的 UI 级往返：任一 toggle 后四面一致 = ①磁贴勾选 ②页签序
//   （pp-format-page-<id> 序）③现役投影（PageOutput::current_format）④现役表单归位
//   （pp-param-form 挂在现役页）。自由函数（探针声明不入 mainwindow_impl.h，取数面 = 显式
//   参数）；操作走磁贴 click = 真实用户路径；结束归位初态，不留残态给后续探针。
namespace {

struct FmtSnap {
    QStringList checked; // 磁贴勾选集（磁贴规范序）
    QStringList tabs;    // 页签 id 序（pp-format-page-<id>）
    QString active;      // current_format()
    QString form_page;   // 现役表单所在页 id（"" = 未找到）
    QString tab_current; // 现役页签 id
};

FmtSnap fmt_snap(PageOutput *page_output) {
    FmtSnap s;
    if (page_output == nullptr)
        return s;
    const QWidget *host = page_output->findChild<QWidget *>(QStringLiteral("pp-tiles"));
    if (host != nullptr) {
        for (const QAbstractButton *tile : host->findChildren<QAbstractButton *>()) {
            const QString name = tile->objectName();
            if (!name.startsWith(QStringLiteral("pp-format-")))
                continue;
            if (tile->isChecked())
                s.checked << name.mid(10);
        }
    }
    const auto *tabs = page_output->findChild<QTabWidget *>(QStringLiteral("pp-format-tabs"));
    if (tabs != nullptr) {
        for (int i = 0; i < tabs->count(); ++i) {
            const QWidget *page = tabs->widget(i);
            const QString n = page != nullptr ? page->objectName() : QString();
            if (n.startsWith(QStringLiteral("pp-format-page-"))) {
                s.tabs << n.mid(15);
                if (i == tabs->currentIndex())
                    s.tab_current = n.mid(15);
            }
        }
    }
    s.active = page_output->current_format();
    const QWidget *form = page_output->findChild<QWidget *>(QStringLiteral("pp-param-form"));
    for (const QWidget *p = form != nullptr ? form->parentWidget() : nullptr; p != nullptr;
         p = p->parentWidget()) {
        const QString n = p->objectName();
        if (n.startsWith(QStringLiteral("pp-format-page-"))) {
            s.form_page = n.mid(15);
            break;
        }
    }
    return s;
}

} // namespace

void probe_format_roundtrip(PageOutput *page_output,
                            const std::function<void(const QString &)> &fail) {
    int checked = 0;
    int bad = 0;
    const auto expect = [&](bool ok, const QString &what) {
        ++checked;
        if (!ok) {
            ++bad;
            fail(QStringLiteral("format-roundtrip：%1").arg(what));
        }
    };
    if (page_output == nullptr) {
        fail(QStringLiteral("format-roundtrip：输出页未就绪"));
        std::printf("UI-SMOKE format-roundtrip: checked=1 bad=1\n");
        std::fflush(stdout);
        return;
    }
    const auto tile_of = [page_output](const QString &id) -> QAbstractButton * {
        QWidget *host = page_output->findChild<QWidget *>(QStringLiteral("pp-tiles"));
        return host != nullptr
                   ? host->findChild<QAbstractButton *>(QStringLiteral("pp-format-") + id)
                   : nullptr;
    };
    const auto first_unchecked = [page_output]() {
        QString id;
        const QWidget *host = page_output->findChild<QWidget *>(QStringLiteral("pp-tiles"));
        if (host != nullptr) {
            for (const QAbstractButton *tile : host->findChildren<QAbstractButton *>()) {
                const QString name = tile->objectName();
                if (name.startsWith(QStringLiteral("pp-format-")) && !tile->isChecked()) {
                    id = name.mid(10);
                    break;
                }
            }
        }
        return id;
    };
    const auto expect_consistent = [&](const FmtSnap &s, const QString &step) {
        expect(s.tabs == s.checked,
               QStringLiteral("%1 页签序==磁贴勾选集（tabs %2 vs checked %3）")
                   .arg(step, s.tabs.join(QLatin1Char(',')), s.checked.join(QLatin1Char(','))));
        expect(!s.checked.isEmpty() && s.tab_current == s.active && s.form_page == s.active,
               QStringLiteral("%1 现役投影一致（active %2 tab %3 form %4）")
                   .arg(step, s.active, s.tab_current, s.form_page));
    };
    const FmtSnap s0 = fmt_snap(page_output);
    expect_consistent(s0, QStringLiteral("初态"));
    const QString x = first_unchecked();
    expect(!x.isEmpty(), QStringLiteral("存在未勾选磁贴（可 toggle）"));
    if (x.isEmpty()) {
        std::printf("UI-SMOKE format-roundtrip: checked=%d bad=%d\n", checked, bad);
        std::fflush(stdout);
        return;
    }
    // 步骤 1：toggle X 开 → R-189（现役 = 新开项）+ 四面一致
    if (QAbstractButton *tile = tile_of(x))
        tile->click();
    smoke_pump(80);
    FmtSnap s1 = fmt_snap(page_output);
    expect(s1.checked.contains(x) && s1.checked.size() == s0.checked.size() + 1,
           QStringLiteral("toggle 开 %1 入集（%2）").arg(x, s1.checked.join(QLatin1Char(','))));
    expect(s1.active == x, QStringLiteral("R-189 开 %1 后现役=新开项（got %2）").arg(x, s1.active));
    expect_consistent(s1, QStringLiteral("开 %1 后").arg(x));
    // 步骤 2：再开 Y → 现役 = Y；步骤 3：关现役 Y → 现役落点取证 + 四面一致
    const QString y = first_unchecked();
    QString off_active;
    QString off_front;
    if (!y.isEmpty()) {
        if (QAbstractButton *tile = tile_of(y))
            tile->click();
        smoke_pump(80);
        const FmtSnap s2 = fmt_snap(page_output);
        expect(s2.active == y,
               QStringLiteral("R-189 开 %1 后现役=新开项（got %2）").arg(y, s2.active));
        expect_consistent(s2, QStringLiteral("开 %1 后").arg(y));
        if (QAbstractButton *tile = tile_of(y))
            tile->click();
        smoke_pump(80);
        const FmtSnap s3 = fmt_snap(page_output);
        expect(!s3.checked.contains(y) && !s3.tabs.contains(y),
               QStringLiteral("关 %1 出集（%2）").arg(y, s3.checked.join(QLatin1Char(','))));
        // 关现役后的现役落点：R-266 模型契约 =「现役前移首位（规范序首项）」（format_selection_
        // model.h 头注；模型真值表 tests/unit/test_smoke.cpp 钉）。M7R23G8-1 修复 R-189 语义
        // 漂移（M7R22G1-6 跨界发现）：原 remove_tab → currentChanged 末写者把现役写成页签滑动
        // 邻接位（实测 off-active=webp front=jpeg），现 toggle_format 在 remove_tab 后显式重申
        // front —— 两读数归位一致，此处按契约钉断言（断言只增）。
        off_active = s3.active;
        off_front = s3.tabs.isEmpty() ? QStringLiteral("<空>") : s3.tabs.first();
        expect(s3.tabs.contains(s3.active),
               QStringLiteral("关现役后现役 ∈ 剩余集合（got %1 tabs %2 front %3）")
                   .arg(s3.active, s3.tabs.join(QLatin1Char(',')), off_front));
        expect(s3.active == off_front,
               QStringLiteral("R-266 关现役后现役前移首位（got %1 front %2）")
                   .arg(s3.active, off_front));
        expect_consistent(s3, QStringLiteral("关 %1 后").arg(y));
    }
    // 归位：toggle X 关 → 集合回到初态；现役按初态回拨（页签点击 = 用户路径）
    if (QAbstractButton *tile = tile_of(x))
        tile->click();
    smoke_pump(80);
    FmtSnap fin = fmt_snap(page_output);
    if (fin.active != s0.active) {
        if (auto *tabs = page_output->findChild<QTabWidget *>(QStringLiteral("pp-format-tabs"))) {
            for (int i = 0; i < tabs->count(); ++i) {
                const QWidget *pg = tabs->widget(i);
                if (pg != nullptr &&
                    pg->objectName() == QStringLiteral("pp-format-page-") + s0.active) {
                    tabs->setCurrentIndex(i);
                    break;
                }
            }
        }
        smoke_pump(80);
        fin = fmt_snap(page_output);
    }
    expect(fin.checked == s0.checked && fin.tabs == s0.tabs && fin.active == s0.active,
           QStringLiteral("归位初态（checked %1 vs %2）")
               .arg(fin.checked.join(QLatin1Char(',')), s0.checked.join(QLatin1Char(','))));
    std::printf("UI-SMOKE format-roundtrip: checked=%d bad=%d x=%s y=%s off-active=%s front=%s\n",
                checked, bad, qUtf8Printable(x), qUtf8Printable(y), qUtf8Printable(off_active),
                qUtf8Printable(off_front));
    std::fflush(stdout);
}

// —— R-218 深水区（M7R22G1-4；m7r2-e2 ⑤-6 常驻化）：QLabel 数值列右对齐在 RTL 下的随向
//   行为**定论** —— Qt 标准控件 QLabel（及继承其 paintEvent 的 formkit::ElidedLabel）
//   渲染走 QStyle::visualAlignment ⇒ 左/右对齐**自动随向镜像**，无需显式 visualAlignment。
//   判据 = 同一对齐的 LTR/RTL 字形墨迹盒互为水平镜像（±2px，R-044 字体敏感几何容差）：
//   三档（QLabel AlignRight / QLabel AlignLeft / ElidedLabel AlignRight）逐档钉住 +
//   应用数值列（pp-run-size-*/speed-*/time-*）旗标读回（右对齐 + QLabel 血统 ⇒ 随定论镜像）。
void probe_rtl_deep(MainWindow *w, const std::function<void(const QString &)> &fail) {
    int checked = 0;
    int bad = 0;
    const auto expect = [&](bool ok, const QString &what) {
        ++checked;
        if (!ok) {
            ++bad;
            fail(QStringLiteral("rtl-deep：%1").arg(what));
        }
    };
    // 同一标签在 LTR/RTL 下渲染取样：墨迹盒互为水平镜像（±2px）
    const auto mirror_case = [&](const QString &tag, QLabel &label, Qt::Alignment align) {
        label.setAlignment(align | Qt::AlignVCenter);
        label.setFixedSize(200, 40);
        SmokeSpan ltr;
        SmokeSpan rtl;
        label.setLayoutDirection(Qt::LeftToRight);
        smoke_ink_span(smoke_render_widget(label), &ltr);
        label.setLayoutDirection(Qt::RightToLeft);
        smoke_ink_span(smoke_render_widget(label), &rtl);
        expect(ltr.found && rtl.found && std::abs(rtl.lo - (200 - 1 - ltr.hi)) <= 2 &&
                   std::abs(rtl.hi - (200 - 1 - ltr.lo)) <= 2,
               QStringLiteral("%1 墨迹盒 LTR↔RTL 镜像（ltr=[%2,%3] rtl=[%4,%5] mirror=[%6,%7]）")
                   .arg(tag)
                   .arg(ltr.lo)
                   .arg(ltr.hi)
                   .arg(rtl.lo)
                   .arg(rtl.hi)
                   .arg(200 - 1 - ltr.hi)
                   .arg(200 - 1 - ltr.lo));
    };
    {
        QLabel label;
        label.setText(QStringLiteral("8888"));
        mirror_case(QStringLiteral("QAlignRight"), label, Qt::AlignRight);
    }
    {
        QLabel label;
        label.setText(QStringLiteral("8888"));
        mirror_case(QStringLiteral("QAlignLeft"), label, Qt::AlignLeft);
    }
    {
        meta::ElidedLabel label;
        label.setText(QStringLiteral("8888"));
        mirror_case(QStringLiteral("EAlignRight"), label, Qt::AlignRight);
    }
    // 应用面读回：运行页数值列（右对齐）—— 结论（自动镜像）挂在同名控件族上
    int cols = 0;
    int cols_bad = 0;
    if (w != nullptr) {
        for (QLabel *label : w->findChildren<QLabel *>()) {
            const QString name = label->objectName();
            if (!(name.startsWith(QStringLiteral("pp-run-size-")) ||
                  name.startsWith(QStringLiteral("pp-run-speed-")) ||
                  name.startsWith(QStringLiteral("pp-run-time-"))))
                continue;
            ++cols;
            if ((label->alignment() & Qt::AlignRight) == 0)
                ++cols_bad;
        }
    }
    expect(cols >= 1 && cols_bad == 0,
           QStringLiteral("数值列旗标右对齐（cols=%1 bad=%2；QLabel 血统 ⇒ RTL 自动镜像，"
                          "无需显式 visualAlignment）")
               .arg(cols)
               .arg(cols_bad));
    std::printf("UI-SMOKE rtl-deep: checked=%d bad=%d cols=%d\n", checked, bad, cols);
    std::fflush(stdout);
}

// —— UX-14 C1–C4 色彩四选项档位探针（M7R22G1-7）：pp-intent-combo / pp-gamut-combo /
//   pp-dither8-switch / pp-linear-switch —— ① 默认值 = 现行为逐字（intent 0 / gamut 0 /
//   dither8 off / linear off；M7R21F4-4 接线口径「全 opt-in 默认关 = 现行为」）+ 档位数
//   （0..3 / 0..2）；② 切换后 config_base() 读回断言 + linear_resample() 同源读回；
//   ③ 归位默认并复读（不留残态）。自由函数取数面。
void probe_color_options(PageOutput *page_output,
                         const std::function<void(const QString &)> &fail) {
    int checked = 0;
    int bad = 0;
    const auto expect = [&](bool ok, const QString &what) {
        ++checked;
        if (!ok) {
            ++bad;
            fail(QStringLiteral("color-options：%1").arg(what));
        }
    };
    if (page_output == nullptr) {
        fail(QStringLiteral("color-options：输出页未就绪"));
        std::printf("UI-SMOKE color-options: checked=1 bad=1\n");
        std::fflush(stdout);
        return;
    }
    QComboBox *intent = page_output->findChild<QComboBox *>(QStringLiteral("pp-intent-combo"));
    QComboBox *gamut = page_output->findChild<QComboBox *>(QStringLiteral("pp-gamut-combo"));
    auto *dither8 = page_output->findChild<QAbstractButton *>(QStringLiteral("pp-dither8-switch"));
    auto *linear = page_output->findChild<QAbstractButton *>(QStringLiteral("pp-linear-switch"));
    const bool in_place =
        intent != nullptr && gamut != nullptr && dither8 != nullptr && linear != nullptr;
    expect(in_place, QStringLiteral("四控件在位（intent=%1 gamut=%2 dither8=%3 linear=%4）")
                         .arg(intent != nullptr ? 1 : 0)
                         .arg(gamut != nullptr ? 1 : 0)
                         .arg(dither8 != nullptr ? 1 : 0)
                         .arg(linear != nullptr ? 1 : 0));
    if (!in_place) {
        std::printf("UI-SMOKE color-options: checked=%d bad=%d\n", checked, bad);
        std::fflush(stdout);
        return;
    }
    const auto cfg4 = [page_output]() {
        const pp::RunConfig cfg = page_output->config_base();
        return QStringLiteral("intent %1 gamut %2 dither8 %3 linear %4")
            .arg(cfg.color_intent)
            .arg(cfg.gamut_mapping)
            .arg(cfg.color_dither8 ? 1 : 0)
            .arg(cfg.linear_resample ? 1 : 0);
    };
    // ① 档位数（0..3 / 0..2，M7R21F4-4）+ 默认值 = 现行为
    expect(intent->count() == 4 && gamut->count() == 3,
           QStringLiteral("档位数（intent %1=4 / gamut %2=3）")
               .arg(intent->count())
               .arg(gamut->count()));
    expect(intent->currentIndex() == 0 && gamut->currentIndex() == 0 && !dither8->isChecked() &&
               !linear->isChecked(),
           QStringLiteral("默认档 = 现行为（%1）").arg(cfg4()));
    {
        const pp::RunConfig cfg = page_output->config_base();
        expect(cfg.color_intent == 0 && cfg.gamut_mapping == 0 && !cfg.color_dither8 &&
                   !cfg.linear_resample,
               QStringLiteral("config_base 默认读回（%1）").arg(cfg4()));
    }
    // ② 切换（用户路径 click / setCurrentIndex）→ config_base 读回
    intent->setCurrentIndex(2);
    gamut->setCurrentIndex(1);
    dither8->click();
    linear->click();
    smoke_pump(60);
    {
        const pp::RunConfig cfg = page_output->config_base();
        expect(cfg.color_intent == 2 && cfg.gamut_mapping == 1 && cfg.color_dither8 &&
                   cfg.linear_resample && page_output->linear_resample(),
               QStringLiteral("切换后读回（%1; linear_resample()=%2）")
                   .arg(cfg4())
                   .arg(page_output->linear_resample() ? 1 : 0));
    }
    // ③ 归位默认 + 复读
    intent->setCurrentIndex(0);
    gamut->setCurrentIndex(0);
    dither8->click();
    linear->click();
    smoke_pump(60);
    {
        const pp::RunConfig cfg = page_output->config_base();
        expect(cfg.color_intent == 0 && cfg.gamut_mapping == 0 && !cfg.color_dither8 &&
                   !cfg.linear_resample,
               QStringLiteral("归位后 config_base 复读（%1）").arg(cfg4()));
    }
    std::printf("UI-SMOKE color-options: checked=%d bad=%d\n", checked, bad);
    std::fflush(stdout);
}

// —— UX-2：勾选批量三态计数 + 过滤作用面 + 快捷键可达（check-all）——
void MainWindow::Impl::smoke_probe_check_all() {
    int checked = 0;
    int bad = 0;
    const auto expect = [&](bool ok, const QString &what) {
        ++checked;
        if (!ok) {
            ++bad;
            smoke_fail(MainWindow::tr("check-all：%1").arg(what));
        }
    };
    if (model == nullptr || proxy == nullptr || view == nullptr) {
        smoke_fail(MainWindow::tr("check-all：文件列表未就绪"));
        std::printf("UI-SMOKE check-all: checked=1 bad=1\n");
        std::fflush(stdout);
        return;
    }
    const std::size_t rows = model->size();
    expect(rows > 0, QStringLiteral("列表非空（语料已入列）"));
    // 三态计数（UX-2 语义）：全选 → rows；取消 → 0；反选逐档往返
    model->set_all_checked(true);
    expect(model->checked_count() == rows, QStringLiteral("勾选全部 → checked==rows"));
    apply_visible_checks(CheckOp::None);
    expect(model->checked_count() == 0, QStringLiteral("取消全部 → 0"));
    apply_visible_checks(CheckOp::Invert);
    expect(model->checked_count() == rows, QStringLiteral("反选（空集）→ rows"));
    apply_visible_checks(CheckOp::Invert);
    expect(model->checked_count() == 0, QStringLiteral("反选（全集）→ 0"));
    // 过滤作用面 = 可见过滤行；勾选与过滤正交（过滤外行的勾选不因操作而动）
    const QString prev_search = search != nullptr ? search->text() : QString();
    if (search != nullptr) {
        model->set_all_checked(true);
        search->setText(QStringLiteral("png"));
        pump(150);
        std::size_t visible = 0;
        for (std::size_t i = 0; i < rows; ++i) {
            if (proxy->mapFromSource(model->index(static_cast<int>(i), 0)).isValid())
                ++visible;
        }
        expect(visible > 0 && visible < rows, QStringLiteral("过滤后可见行 0<visible<rows"));
        apply_visible_checks(CheckOp::None);
        std::size_t visible_checked_left = 0;
        std::size_t hidden_touched = 0;
        for (std::size_t i = 0; i < rows; ++i) {
            const bool is_visible =
                proxy->mapFromSource(model->index(static_cast<int>(i), 0)).isValid();
            if (is_visible && model->is_checked(i))
                ++visible_checked_left;
            if (!is_visible && !model->is_checked(i))
                ++hidden_touched;
        }
        expect(visible_checked_left == 0, QStringLiteral("取消全部只作用于可见行"));
        expect(hidden_touched == 0, QStringLiteral("过滤外行勾选不动（正交）"));
        search->setText(prev_search);
        pump(150);
        model->set_all_checked(true);
    }
    // 快捷键可达（Ctrl+A / Ctrl+Shift+A / Ctrl+D；WidgetWithChildrenShortcut 挂列表焦点域）
    QStringList seqs;
    for (QAction *action : view->actions())
        seqs << action->shortcut().toString();
    expect(seqs.contains(QKeySequence(QKeySequence::SelectAll).toString()),
           QStringLiteral("Ctrl+A 全选行可达"));
    expect(seqs.contains(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A).toString()),
           QStringLiteral("Ctrl+Shift+A 勾选全部可达"));
    expect(seqs.contains(QKeySequence(Qt::CTRL | Qt::Key_D).toString()),
           QStringLiteral("Ctrl+D 取消全部勾选可达"));
    // 补强（M7R22G1-8）：快捷键动作**执行语义**（不只注册面）——触发「勾选全部/取消全部」
    // 动作并断言勾选计数随之变化（Ctrl+A 是视图选择面，不在勾选计数判据内）。
    QAction *act_check_all = nullptr;
    QAction *act_uncheck_all = nullptr;
    for (QAction *action : view->actions()) {
        const QString seq = action->shortcut().toString();
        if (seq == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A).toString())
            act_check_all = action;
        if (seq == QKeySequence(Qt::CTRL | Qt::Key_D).toString())
            act_uncheck_all = action;
    }
    model->set_all_checked(false);
    if (act_check_all != nullptr)
        act_check_all->trigger();
    pump(60);
    expect(model->checked_count() == rows,
           QStringLiteral("「勾选全部」动作执行 → checked==rows（got %1）")
               .arg(model->checked_count()));
    if (act_uncheck_all != nullptr)
        act_uncheck_all->trigger();
    pump(60);
    expect(model->checked_count() == 0,
           QStringLiteral("「取消全部」动作执行 → 0（got %1）").arg(model->checked_count()));
    model->set_all_checked(true); // 归位（保持本探针原终态 = 全选）
    std::printf("UI-SMOKE check-all: checked=%d bad=%d rows=%llu\n", checked, bad,
                static_cast<unsigned long long>(rows));
    std::fflush(stdout);
}

// —— UX-1：卡片右键菜单（context-menu）：①②③④⑤⑥⑦⑧⑨⑩⑪⑫ 逐卡弹一次 ——
void MainWindow::Impl::smoke_probe_context_menus() {
    Tally tally;
    const std::function<void(const QString &)> fail = [this](const QString &reason) {
        smoke_fail(reason);
    };
    const auto host_of = [](QWidget *root, const QString &descendant) {
        return root != nullptr ? menu_host_of(root->findChild<QWidget *>(descendant)) : nullptr;
    };
    struct CardMenu {
        const char *tag;
        QWidget *host;
        QString first;
        std::vector<contextmenu::Item> want;
    };
    std::vector<CardMenu> cards;
    // ① 文件列表（宿主 = view）
    cards.push_back({"file-list", view, QString::fromUtf8("勾选全部"),
                     contextmenu::file_list_actions(contextmenu::FileListIntents{})});
    // ②③④⑤ 元数据页四卡（宿主 = 卡框；卡头 pill 为稳定锚）
    cards.push_back({"meta-info", host_of(page_meta, meta::objname::kInfoPill),
                     QString::fromUtf8("复制全部信息"),
                     contextmenu::info_card_actions(contextmenu::InfoCardIntents{})});
    cards.push_back({"meta-time", host_of(page_meta, meta::objname::kTimePill),
                     QString::fromUtf8("启用"),
                     contextmenu::time_card_actions(contextmenu::TimeCardIntents{})});
    cards.push_back({"meta-gps", host_of(page_meta, meta::objname::kGpsPill),
                     QString::fromUtf8("启用"),
                     contextmenu::gps_card_actions(contextmenu::GpsCardIntents{})});
    cards.push_back({"meta-privacy", host_of(page_meta, meta::objname::kPrivacyPill),
                     QString::fromUtf8("启用"),
                     contextmenu::privacy_card_actions(contextmenu::PrivacyCardIntents{})});
    // ⑥⑦⑨ 输出页三卡
    cards.push_back({"out-format", host_of(page_output, "pp-tiles"), QString::fromUtf8("全选格式"),
                     contextmenu::format_card_actions(contextmenu::FormatCardIntents{})});
    cards.push_back({"out-global", host_of(page_output, "pp-out-root-edit"),
                     QString::fromUtf8("重置为默认"),
                     contextmenu::global_card_actions(contextmenu::GlobalCardIntents{})});
    cards.push_back({"out-preset", host_of(page_output, "pp-preset-row"),
                     QString::fromUtf8("另存为预设…"),
                     contextmenu::preset_card_actions(contextmenu::PresetCardIntents{})});
    // ⑧ 格式参数卡（宿主 = ParamForm 自身；从核心区向上找）
    cards.push_back({"param-card", host_of(page_output, "pp-core-area"),
                     QString::fromUtf8("重置为默认"),
                     contextmenu::param_card_actions(contextmenu::ParamCardIntents{})});
    // ⑩⑪⑫ 运行页三卡
    cards.push_back({"run-overview", host_of(page_run, "pp-run-state-pill"),
                     QString::fromUtf8("查看完整日志…"),
                     contextmenu::run_overview_actions(contextmenu::RunOverviewIntents{})});
    cards.push_back({"run-task", host_of(page_run, "pp-run-tasks-hint"),
                     QString::fromUtf8("复制失败原因"),
                     contextmenu::task_row_actions(contextmenu::TaskRowIntents{})});
    cards.push_back({"run-logs", host_of(page_run, "pp-run-log-name"),
                     QString::fromUtf8("复制日志"),
                     contextmenu::log_tail_actions(contextmenu::LogTailIntents{})});
    for (const CardMenu &card : cards) {
        expect_card_menu(QString::fromLatin1(card.tag), card.host, card.first, card.want, fail,
                         tally);
    }
    std::printf("UI-SMOKE context-menu: cards=%d checked=%d bad=%d\n",
                static_cast<int>(cards.size()), tally.checked, tally.bad);
    std::fflush(stdout);
}

// —— R-070/R-078：失败原因行取样 + 峰值线程行（run-reason）——
void MainWindow::Impl::smoke_probe_run_reason() {
    int checked = 0;
    int bad = 0;
    const auto expect = [&](bool ok, const QString &what) {
        ++checked;
        if (!ok) {
            ++bad;
            smoke_fail(MainWindow::tr("run-reason：%1").arg(what));
        }
    };
    if (page_run == nullptr) {
        smoke_fail(MainWindow::tr("run-reason：运行页未就绪"));
        std::printf("UI-SMOKE run-reason: checked=1 bad=1\n");
        std::fflush(stdout);
        return;
    }
    // R-078：峰值线程行（结束态读数非「—」+ tooltip 写明估算口径）
    const auto *threads = page_run->findChild<QLabel *>(QStringLiteral("pp-run-threads"));
    const auto *threads_row = page_run->findChild<QWidget *>(QStringLiteral("pp-run-threads-row"));
    expect(threads != nullptr && threads_row != nullptr, QStringLiteral("峰值线程行存在"));
    if (threads != nullptr) {
        expect(threads->text() != QStringLiteral("—"),
               QStringLiteral("结束态峰值线程非「—」（got %1）").arg(threads->text()));
        expect(threads->toolTip().contains(QStringLiteral("估算")),
               QStringLiteral("tooltip 写明估算口径"));
    }
    // R-070：失败原因行「└ 原因：%1」—— 中文分类可见 + 原始引擎串进 tooltip
    int reason_seen = 0;
    QString sample_text;
    QString sample_tip;
    for (QLabel *label : page_run->findChildren<QLabel *>()) {
        if (!label->objectName().startsWith(QLatin1String("pp-run-reason-text-")))
            continue;
        ++reason_seen;
        if (sample_text.isEmpty()) {
            sample_text = label->text();
            sample_tip = label->toolTip();
        }
    }
    expect(reason_seen > 0, QStringLiteral("失败原因行存在（03d 探针态四类同行）"));
    if (!sample_text.isEmpty()) {
        expect(sample_text.contains(QStringLiteral("└ 原因：")),
               QStringLiteral("原因行含「└ 原因：%1」").arg(sample_text));
        const QChar head = sample_text.size() > 5 ? sample_text.at(5) : QChar();
        expect(head.isLetter() && head.unicode() > 127,
               QStringLiteral("原因行分类为中文（R-070 error_text）"));
        expect(!sample_tip.isEmpty(), QStringLiteral("原始引擎串进 tooltip"));
    }
    std::printf("UI-SMOKE run-reason: checked=%d bad=%d reasons=%d\n", checked, bad, reason_seen);
    std::fflush(stdout);
}

// —— UX-8/UX-10.1：高级二级折叠 + 编码档位限幅联动（folds）——
void MainWindow::Impl::smoke_probe_advanced_folds() {
    int checked = 0;
    int bad = 0;
    const auto expect = [&](bool ok, const QString &what) {
        ++checked;
        if (!ok) {
            ++bad;
            smoke_fail(MainWindow::tr("folds：%1").arg(what));
        }
    };
    if (page_output == nullptr) {
        smoke_fail(MainWindow::tr("folds：输出页未就绪"));
        std::printf("UI-SMOKE folds: checked=1 bad=1\n");
        std::fflush(stdout);
        return;
    }
    const QString prev = page_output->current_format();
    page_output->select_format(QStringLiteral("jxl")); // 静态表参数面：子分组最全
    // UX-8：子分组折叠头（pp-advanced-group-N）+ 默认只展开首组 + 「N 项」计数
    // （组数随格式参数面而定 —— 内省格式（heif/avif）无 group 回落单组属实况，见 R-2D2 UX-8）
    int groups_seen = 0;
    int expanded = 0;
    int counted = 0;
    for (int g = 0; g < 4; ++g) {
        QWidget *header =
            page_output->findChild<QWidget *>(QStringLiteral("pp-advanced-group-%1").arg(g));
        if (header == nullptr)
            continue;
        ++groups_seen;
        for (QToolButton *toggle : header->findChildren<QToolButton *>()) {
            if (toggle->isCheckable()) {
                if (toggle->isChecked())
                    ++expanded;
                break;
            }
        }
        for (QLabel *label : header->findChildren<QLabel *>()) {
            if (label->text().contains(QStringLiteral("项"))) {
                ++counted;
                break;
            }
        }
    }
    expect(groups_seen >= 1, QStringLiteral("子分组折叠头存在（got %1）").arg(groups_seen));
    expect(expanded == 1, QStringLiteral("默认只展开首组（got %1）").arg(expanded));
    expect(counted == groups_seen,
           QStringLiteral("折叠头「N 项」计数齐（got %1/%2）").arg(counted).arg(groups_seen));
    // UX-10.1：编码档位（pp-profile-combo）→ 档位说明/兼容性提示行
    page_output->select_format(QStringLiteral("avif"));
    auto *profile = page_output->findChild<QComboBox *>(QStringLiteral("pp-profile-combo"));
    expect(profile != nullptr, QStringLiteral("avif 页签有编码档位行"));
    if (profile != nullptr && profile->count() >= 3) {
        profile->setCurrentIndex(2); // High（4:4:4）—— 兼容性提示必非空（ux-review §10.1-4）
        pump(60);
        const auto *note = page_output->findChild<QLabel *>(QStringLiteral("pp-profile-note-avif"));
        expect(note != nullptr, QStringLiteral("档位说明行存在（pp-profile-note-*）"));
        if (note != nullptr) {
            expect(!note->text().isEmpty() || !note->toolTip().isEmpty(),
                   QStringLiteral("非 Main 档位给兼容性说明（%1）").arg(note->text()));
        }
    }
    if (!prev.isEmpty())
        page_output->select_format(prev);
    std::printf("UI-SMOKE folds: checked=%d bad=%d groups=%d\n", checked, bad, groups_seen);
    std::fflush(stdout);
}

// ===== M7-R2.1-F4 探针批（第 4 波；R2F ⑤ 未落地项）=============================
// 出处：R2D1 ⑤ / R2D2 ⑤ / R2E2 ⑤ / R2F ⑤ 的「探针需求」；已落地项（context-menu /
// check-all / folds / run-reason / rtl-mirror / UX-4 stretch map≥260）不重复。

// R-148「N 项未能读取」+ R-149 别名去重（intake）：
//   * 不存在的输入项 → unreadable_count +1 且卡头 hint 出「N 项未能读取」；
//   * 同一文件的冗余路径形态（dir/./file）→ 不重复入列（弱规范化去重键）。
void MainWindow::Impl::smoke_probe_intake() {
    int checked = 0;
    int bad = 0;
    const auto expect = [&](bool ok, const QString &what) {
        ++checked;
        if (!ok) {
            ++bad;
            smoke_fail(MainWindow::tr("intake：%1").arg(what));
        }
    };
    if (model == nullptr) {
        smoke_fail(MainWindow::tr("intake：文件列表未就绪"));
        std::printf("UI-SMOKE intake: checked=1 bad=1\n");
        std::fflush(stdout);
        return;
    }
    const std::size_t rows0 = model->size();
    const std::size_t unread0 = model->unreadable_count();
    model->add_paths(QStringList{QStringLiteral("/nonexistent/pp-smoke-missing.png")});
    expect(model->unreadable_count() == unread0 + 1,
           QStringLiteral("R-148 不存在输入计入未能读取（got %1）").arg(model->unreadable_count()));
    const QString hint_text = unsupported != nullptr ? unsupported->text() : QString();
    expect(hint_text.contains(QStringLiteral("项未能读取")),
           QStringLiteral("R-148 卡头 hint 同面出「N 项未能读取」（got \"%1\"）").arg(hint_text));
    if (rows0 > 0) {
        const std::string src0 = model->row(0).entry.src.string();
        const QFileInfo fi(QString::fromStdString(src0));
        const QString alias =
            fi.path() + QStringLiteral("/./") + fi.fileName(); // 冗余路径形态（两平台同实体）
        model->add_paths(QStringList{alias});
        expect(model->size() == rows0,
               QStringLiteral("R-149 别名不重复入列（rows %1→%2）").arg(rows0).arg(model->size()));
        if (model->size() > rows0) { // 失败自净（别名误入列时把新增行摘掉，后续探针不受污染）
            QList<int> extras;
            for (int i = static_cast<int>(rows0); i < static_cast<int>(model->size()); ++i)
                extras << i;
            model->remove_rows(extras);
        }
    }
    std::printf("UI-SMOKE intake: checked=%d bad=%d rows=%llu unread=%llu hint=%d\n", checked, bad,
                static_cast<unsigned long long>(model->size()),
                static_cast<unsigned long long>(model->unreadable_count()),
                hint_text.contains(QStringLiteral("项未能读取")) ? 1 : 0);
    std::fflush(stdout);
}

// R-143：chip = 过滤档开关（点击 → 列表按该类过滤；再点/点「全部」= 清除）+ `.on` 拆态单义
// （当前过滤档，不再与"无分类/无选中"共用通道）。
// 取第 n 个 chip（findChildren 序不稳定 → 按文本/键帽序不判据；n=1 = 「全部」外首个真类）
QAbstractButton *pick_chip(const QList<QAbstractButton *> &chips, int n) {
    int seen = 0;
    for (QAbstractButton *chip : chips) {
        if (seen == n)
            return chip;
        ++seen;
    }
    return nullptr;
}

void MainWindow::Impl::smoke_probe_class_filter() {
    int checked = 0;
    int bad = 0;
    const auto expect = [&](bool ok, const QString &what) {
        ++checked;
        if (!ok) {
            ++bad;
            smoke_fail(MainWindow::tr("classify-filter：%1").arg(what));
        }
    };
    const auto visible_rows = [this]() {
        std::size_t n = 0;
        for (std::size_t i = 0; i < model->size(); ++i)
            if (proxy->mapFromSource(model->index(static_cast<int>(i), 0)).isValid())
                ++n;
        return n;
    };
    if (model == nullptr || proxy == nullptr || classify_panel == nullptr) {
        smoke_fail(MainWindow::tr("classify-filter：列表/面板未就绪"));
        std::printf("UI-SMOKE classify-filter: checked=1 bad=1\n");
        std::fflush(stdout);
        return;
    }
    // ChipButton 无自身 Q_OBJECT（formkit 自绘件惯例）→ 按基类 QAbstractButton 查
    // R-183 探针纪律：**不跨 pump 缓存部件指针** —— 面板重建（refresh_timer → rebuild_rows）
    // 走 hide + deleteLater，事件循环回到主层即析构旧代；每次点击前重新取件。
    const auto chips_now = [this]() {
        return classify_panel->findChildren<QAbstractButton *>(QStringLiteral("pp-class-chip"));
    };
    expect(chips_now().size() >= 2,
           QStringLiteral("chip 面存在（got %1）").arg(chips_now().size()));
    if (chips_now().size() < 2)
        return;
    const std::size_t all_visible = visible_rows();
    if (QAbstractButton *chip = pick_chip(chips_now(), 1))
        chip->click(); // 首个真类（「全部」= chips_now().at(0)）
    pump(80);
    const QString filter = proxy != nullptr ? proxy->class_filter() : QString();
    expect(!filter.isEmpty(), QStringLiteral("点 chip 后过滤档生效（got \"%1\"）").arg(filter));
    std::size_t expect_n = 0;
    for (std::size_t i = 0; i < model->size(); ++i)
        if (model->data(model->index(static_cast<int>(i), 0), FileListModel::ClassIdRole)
                .toString() == filter)
            ++expect_n;
    expect(visible_rows() == expect_n,
           QStringLiteral("过滤后可见行 == 该类行（%1 vs %2）").arg(visible_rows()).arg(expect_n));
    int on_count = 0;
    for (QAbstractButton *chip : chips_now())
        if (chip->isChecked())
            ++on_count;
    expect(on_count == 1, QStringLiteral(".on 拆态单义（got %1）").arg(on_count));
    if (QAbstractButton *chip = pick_chip(chips_now(), 1))
        chip->click(); // 再点 = 清除
    pump(80);
    expect(visible_rows() == all_visible,
           QStringLiteral("再点清除过滤（%1 vs %2）").arg(visible_rows()).arg(all_visible));
    std::printf("UI-SMOKE classify-filter: checked=%d bad=%d filter=\"%s\" rows=%llu\n", checked,
                bad, qUtf8Printable(filter), static_cast<unsigned long long>(visible_rows()));
    std::fflush(stdout);
}

// R-152 过滤态双击编辑对象正确（view 行 → 源行映射 = 双击处理链，指向所见对象）
// + R-153 分组重建多选整组恢复（两选跨重建保留）。
void MainWindow::Impl::smoke_probe_edit_target() {
    int checked = 0;
    int bad = 0;
    const auto expect = [&](bool ok, const QString &what) {
        ++checked;
        if (!ok) {
            ++bad;
            smoke_fail(MainWindow::tr("edit-target：%1").arg(what));
        }
    };
    if (model == nullptr || groups == nullptr || view == nullptr || model->size() < 2) {
        smoke_fail(MainWindow::tr("edit-target：列表未就绪/行数不足"));
        std::printf("UI-SMOKE edit-target: checked=1 bad=1\n");
        std::fflush(stdout);
        return;
    }
    // R-152：过滤态（搜索缩集合）下取首可见 view 行，走双击处理链映射回源行，
    // 断言源行文件名与视图显示一致（对象不错位）。
    const QString prev_search = search != nullptr ? search->text() : QString();
    if (search != nullptr) {
        search->setText(QStringLiteral("png"));
        pump(150);
    }
    // R-152（M7R22G1-9 深度）：过滤态下**每个**可见文件行的双击映射链都落回同名源行
    // （原只查首行；「双击编辑错文件」类缺陷要求全可见面一致）。
    int vis_rows = 0;
    int map_bad = 0;
    QStringList map_diag;
    for (std::size_t i = 0; i < model->size(); ++i) {
        const QModelIndex pidx = proxy->mapFromSource(model->index(static_cast<int>(i), 0));
        const QModelIndex gidx = groups->mapFromSource(pidx);
        if (!gidx.isValid())
            continue;
        ++vis_rows;
        const QModelIndex src_idx = proxy->mapToSource(groups->mapToSource(gidx));
        const int src_row = src_idx.row();
        const QString shown = view->model()->data(gidx, FileListModel::NameRole).toString();
        if (src_row < 0 || src_row >= static_cast<int>(model->size())) {
            ++map_bad;
            map_diag << QStringLiteral("row%1:越界%2").arg(i).arg(src_row);
            continue;
        }
        const QString base =
            QFileInfo(QString::fromStdString(model->row(std::size_t(src_row)).entry.src.string()))
                .fileName();
        if (!shown.contains(base)) {
            ++map_bad;
            map_diag << QStringLiteral("row%1:\"%2\"↔\"%3\"").arg(i).arg(shown, base);
        }
    }
    expect(vis_rows > 0, QStringLiteral("过滤后应有可见行（got %1）").arg(vis_rows));
    expect(map_bad == 0, QStringLiteral("R-152 所见即所编（全 %1 可见行映射一致；%2）")
                             .arg(vis_rows)
                             .arg(map_diag.join(QLatin1Char(' '))));
    if (search != nullptr) {
        search->setText(prev_search);
        pump(150);
    }
    // R-153：多选两行 → **搜索过滤重建**（评审场景原文「多选 5 行后敲过滤字符不再塌缩成 1 行」）
    // → 两选保留（被过滤隐藏的行不越权恢复 —— 两行都命中过滤串，故都应保留）。
    if (view->selectionModel() != nullptr) {
        view->selectionModel()->clearSelection();
        QList<int> picked;
        QItemSelection sel;
        for (std::size_t i = 0; i < model->size() && picked.size() < 2; ++i) {
            const std::string src = model->row(i).entry.src.string();
            if (src.find(".png") == std::string::npos)
                continue; // 与下方过滤串同判据（隐藏即不恢复 → 选样须命中）
            const QModelIndex pidx = proxy->mapFromSource(model->index(static_cast<int>(i), 0));
            const QModelIndex gidx = groups->mapFromSource(pidx);
            if (gidx.isValid()) {
                sel.select(gidx, gidx);
                picked << static_cast<int>(i);
            }
        }
        if (!sel.isEmpty())
            view->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect |
                                                    QItemSelectionModel::Rows);
        if (search != nullptr && picked.size() == 2) {
            search->setText(QStringLiteral("png"));
            pump(150);
        }
        int still = 0;
        QStringList diag;
        for (int row : picked) {
            const QModelIndex pidx = proxy->mapFromSource(model->index(row, 0));
            const QModelIndex gidx = groups->mapFromSource(pidx);
            const bool ok = gidx.isValid() && view->selectionModel()->isSelected(gidx);
            if (ok)
                ++still;
            diag << QStringLiteral("%1:%2/%3")
                        .arg(row)
                        .arg(gidx.isValid() ? 1 : 0)
                        .arg(view->selectionModel()->isSelected(gidx) ? 1 : 0);
        }
        expect(still == picked.size() && picked.size() == 2,
               QStringLiteral("R-153 过滤重建多选保留（%1/%2 diag %3）")
                   .arg(still)
                   .arg(picked.size())
                   .arg(diag.join(QLatin1Char(' '))));
        // R-153 回归深度（M7R22G1-9）：第二轮重建（清过滤 → 再过滤）——恢复协议可重复，
        // 两选仍保留（「选中恢复先于 expandAll」顺序契约对二次重建同样成立）。
        if (search != nullptr && picked.size() == 2) {
            search->setText(QString());
            pump(150);
            search->setText(QStringLiteral("png"));
            pump(150);
        }
        int still2 = 0;
        for (int row : picked) {
            const QModelIndex pidx = proxy->mapFromSource(model->index(row, 0));
            const QModelIndex gidx = groups->mapFromSource(pidx);
            if (gidx.isValid() && view->selectionModel()->isSelected(gidx))
                ++still2;
        }
        expect(still2 == picked.size() && picked.size() == 2,
               QStringLiteral("R-153 二轮重建多选仍保留（%1/%2）").arg(still2).arg(picked.size()));
        if (search != nullptr) {
            search->setText(QString());
            pump(150);
        }
        view->selectionModel()->clearSelection();
    }
    std::printf("UI-SMOKE edit-target: checked=%d bad=%d\n", checked, bad);
    std::fflush(stdout);
}

// UX-12 像素档（D2⑤-2）：亮色档 UnderlineTabs 选中/未选中标题绘制像素 == 亮 text / 亮 text2，
// 基线 hairline == 亮 card_bd（R2D2 修的是重放链，像素档由本探针钉）。
void MainWindow::Impl::smoke_probe_settings_tabs_light() {
    int checked = 0;
    int bad = 0;
    const auto expect = [&](bool ok, const QString &what) {
        ++checked;
        if (!ok) {
            ++bad;
            smoke_fail(MainWindow::tr("settings-tabs-light：%1").arg(what));
        }
    };
    const theme::Tokens lt = theme::tokens(theme::ThemeMode::Light);
    SettingsDialog dlg(settings, w);
    auto *tabs = dlg.findChild<dialogkit::UnderlineTabs *>(QLatin1String("pp-dlg-tabs"));
    expect(tabs != nullptr, QStringLiteral("pp-dlg-tabs 存在"));
    if (tabs == nullptr) {
        std::printf("UI-SMOKE settings-tabs-light: checked=%d bad=%d\n", checked, bad);
        std::fflush(stdout);
        return;
    }
    tabs->set_tokens(lt);
    dlg.show();
    pump(120);
    // 标题绘制像素：抓单 tab 按钮 → 非透明像素直方图的众数 = 纯字色（抗锯齿内部像素）
    // 直方图众数；skip = 背景色（抓图含部件背后的对话框底，字形色 = 非背景里的众数）
    const auto mode_except = [](const QImage &img, const QColor &skip, bool *found) {
        QHash<QRgb, int> hist;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QRgb c = img.pixel(x, y);
                if (qAlpha(c) < 200)
                    continue;
                if (skip.isValid() && std::abs(qRed(c) - skip.red()) <= 3 &&
                    std::abs(qGreen(c) - skip.green()) <= 3 &&
                    std::abs(qBlue(c) - skip.blue()) <= 3)
                    continue;
                ++hist[c];
            }
        QRgb best = 0;
        int best_n = 0;
        for (auto it = hist.constBegin(); it != hist.constEnd(); ++it)
            if (it.value() > best_n) {
                best_n = it.value();
                best = it.key();
            }
        if (found != nullptr)
            *found = best_n > 0;
        return QColor(best);
    };
    const auto mode_of = [](const QImage &img) {
        QHash<QRgb, int> hist;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QRgb c = img.pixel(x, y);
                if (qAlpha(c) < 200)
                    continue;
                ++hist[c];
            }
        QRgb best = 0;
        int best_n = 0;
        for (auto it = hist.constBegin(); it != hist.constEnd(); ++it)
            if (it.value() > best_n) {
                best_n = it.value();
                best = it.key();
            }
        return QColor(best);
    };
    const auto near = [](const QColor &a, const QColor &b) {
        return std::abs(a.red() - b.red()) <= 6 && std::abs(a.green() - b.green()) <= 6 &&
               std::abs(a.blue() - b.blue()) <= 6;
    };
    // ── R-044 / M7R22G1-AA：像素档 AA 容差机制（仍钉语义）──────────────────────────
    // 众数取色在灰阶 AA（Linux 无 ClearType）下被大量半覆盖边缘像素拉向底色：main-full
    // 实测（run 38314701500 注解）选中 got #1e2028 / want #1b1c20、未选中 got #222638 /
    // want #5d5f68 —— 同一绘制色 + AA 混合的取色漂移，非语义差异。机制（断言只增不减）：
    //   ① 原众数口径 near(mode, want, ±6) **保留为充分条件**（ClearType/全覆盖路径不变）；
    //   ② 新增 AA 容差充分条件：字形内核（非背景像素按离底色距离取最深 15% 的逐通道
    //      中位数）沿 bg→want 线覆盖度 t ≥ 0.5 + 逐通道残差 ≤ 12（通道容差）；
    //   ③ 仍钉语义：内核须更接近 want 而非另一枚令牌（选中=text ↔ 未选中=text2 互斥）。
    const auto glyph_shot = [](QWidget *btn) {
        QImage img = btn->grab().toImage();
        // 选中段下缘 2px 是 accent 下划线（paintEvent）—— 标题字形取样需让开该带
        if (img.height() > 6)
            img = img.copy(0, 0, img.width(), img.height() - 4);
        return img;
    };
    const auto ink_core = [](const QImage &img, const QColor &bg) {
        struct Ink {
            double d;
            int r, g, b;
        };
        std::vector<Ink> ps;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const QRgb c = img.pixel(x, y);
                if (qAlpha(c) < 200)
                    continue;
                const int dr = qRed(c) - bg.red();
                const int dg = qGreen(c) - bg.green();
                const int db = qBlue(c) - bg.blue();
                const double d = std::sqrt(double(dr * dr + dg * dg + db * db));
                if (d <= 3.0)
                    continue; // 背景噪声带（与 mode_except 的 ±3 同口径）
                ps.push_back({d, qRed(c), qGreen(c), qBlue(c)});
            }
        }
        if (ps.empty())
            return QColor();
        std::sort(ps.begin(), ps.end(), [](const Ink &x, const Ink &y) { return x.d < y.d; });
        const std::size_t keep = std::max<std::size_t>(1, ps.size() * 15 / 100);
        std::vector<int> rs;
        std::vector<int> gs;
        std::vector<int> bs;
        for (std::size_t i = ps.size() - keep; i < ps.size(); ++i) {
            rs.push_back(ps[i].r);
            gs.push_back(ps[i].g);
            bs.push_back(ps[i].b);
        }
        const auto median = [](std::vector<int> v) {
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        return QColor(median(rs), median(gs), median(bs));
    };
    QStringList title_diag; // 取证：mode/est/t/resid 读数随自检行打印（不判据）
    struct TitleTint {      // 语义令牌对（want=本档标题色 / other=互斥对照档），结构化防同型形参对
        QColor want;
        QColor other;
    };
    const auto title_check = [&](const char *objname, const QString &tag, const TitleTint &tint) {
        QAbstractButton *btn = dlg.findChild<QAbstractButton *>(QLatin1String(objname));
        if (btn == nullptr) {
            expect(false, QStringLiteral("%1 缺失").arg(QString::fromLatin1(objname)));
            return;
        }
        const QImage img = glyph_shot(btn);
        const QColor bg = mode_of(img);
        bool found = false;
        const QColor mode_c = mode_except(img, bg, &found);
        const QColor est = ink_core(img, bg);
        // 判据②：内核沿 bg→want 线的覆盖度 t ≥ 0.5 + 逐通道残差 ≤ 12（通道容差）
        const double wr = tint.want.red() - bg.red();
        const double wg = tint.want.green() - bg.green();
        const double wb = tint.want.blue() - bg.blue();
        const double len2 = wr * wr + wg * wg + wb * wb;
        double t = -1.0;
        int resid = -1;
        bool seg_ok = false;
        if (est.isValid() && len2 >= 1.0) {
            const double er = est.red() - bg.red();
            const double eg = est.green() - bg.green();
            const double eb = est.blue() - bg.blue();
            t = (er * wr + eg * wg + eb * wb) / len2;
            const int ar = int(std::lround(bg.red() + t * wr));
            const int ag = int(std::lround(bg.green() + t * wg));
            const int ab = int(std::lround(bg.blue() + t * wb));
            resid = std::max(std::abs(est.red() - ar),
                             std::max(std::abs(est.green() - ag), std::abs(est.blue() - ab)));
            // 判据③：仍钉语义 —— 内核更接近 want 而非另一枚令牌（text ↔ text2 互斥）
            const auto dist2 = [](const QColor &x, const QColor &y) {
                const double dr = x.red() - y.red();
                const double dg = x.green() - y.green();
                const double db = x.blue() - y.blue();
                return dr * dr + dg * dg + db * db;
            };
            seg_ok = t >= 0.5 && resid <= 12 && dist2(est, tint.want) <= dist2(est, tint.other);
        }
        const bool ok = near(mode_c, tint.want) || seg_ok; // 判据①原口径保留（充分条件）
        title_diag << QStringLiteral("%1 mode=%2 est=%3 t=%4 resid=%5")
                          .arg(QString::fromLatin1(objname), mode_c.name(), est.name())
                          .arg(t, 0, 'f', 2)
                          .arg(resid);
        expect(ok, QStringLiteral("%1（mode %2 est %3 want %4 bg %5 t=%6 resid=%7）")
                       .arg(tag, mode_c.name(), est.name(), tint.want.name(), bg.name())
                       .arg(t, 0, 'f', 2)
                       .arg(resid));
    };
    title_check("pp-tab-general", QStringLiteral("选中标题像素 == 亮 text"),
                TitleTint{lt.text, lt.text2});
    title_check("pp-tab-output", QStringLiteral("未选中标题像素 == 亮 text2"),
                TitleTint{lt.text2, lt.text});
    // 基线 hairline（tab 件下缘）== 亮 card_bd
    {
        const QImage img = tabs->grab().toImage();
        const QColor bg = mode_of(img);
        QHash<QRgb, int> hist;
        const int y = img.height() - 1;
        for (int x = 0; x < img.width(); ++x) {
            const QRgb c = img.pixel(x, y);
            if (qAlpha(c) < 40)
                continue;
            ++hist[c];
        }
        QRgb best = 0;
        int best_n = 0;
        for (auto it = hist.constBegin(); it != hist.constEnd(); ++it)
            if (it.value() > best_n) {
                best_n = it.value();
                best = it.key();
            }
        const QColor got(best);
        // card_bd 带 alpha（亮档 = rgba(0,0,0,.09)）→ 期望值 = 与实测背景的合成
        // （抓图像素是合成结果；与 token 的绘制色相等即「hairline == 亮 card_bd」语义）
        const qreal a = lt.card_bd.alphaF();
        const QColor want(int(std::lround(bg.red() * (1.0 - a) + lt.card_bd.red() * a)),
                          int(std::lround(bg.green() * (1.0 - a) + lt.card_bd.green() * a)),
                          int(std::lround(bg.blue() * (1.0 - a) + lt.card_bd.blue() * a)));
        expect(near(got, want),
               QStringLiteral("基线 hairline == 亮 card_bd 合成（got %1 want %2 bg %3）")
                   .arg(got.name(), want.name(), bg.name()));
    }
    dlg.close();
    std::printf("UI-SMOKE settings-tabs-light: checked=%d bad=%d %s\n", checked, bad,
                qUtf8Printable(title_diag.join(QLatin1Char(' '))));
    std::fflush(stdout);
}

void MainWindow::Impl::smoke_run(const QString &shots_dir) {
    // ---- M4-T9 骨架自检（窗口行为矩阵可自动化部分；先跑，之后截图归位 1440×900）----
    smoke_probe_lifecycle();
    smoke_probe_skeleton();
    smoke_probe_shell_geometry(); // M5-T4（C7）：壳层几何值表 + 状态四档（§3/§6.2/R4/§8）
    smoke_probe_frameless();
    smoke_probe_theme();
    smoke_probe_preview();           // M4-T10：预览面板（翻图/缩放/徽标/热键接线位）
    smoke_probe_preview_geometry();  // M5-T6（C7）：画布底/空态/角标/缩放条/衬垫/最近邻（§8）
    smoke_probe_output_geometry();   // M5-T8（C7）：输出页四卡组几何（§7.2 逐行 / R1–R4）
    smoke_probe_filelist();          // M4-T11：勾选/正交搜索/分组节（§3.6 roles + §6.2）
    smoke_probe_filelist_geometry(); // M5-T5（C7）：行/工具行几何 + 空态 + 选中面（§7.5/修 3-17）
    smoke_probe_meta(shots_dir);     // M4-T12：生效值显示/写入一致性 + 多选小表 + 地图跟随（§5.2）

    // ---- 页 1（元数据）：等缩略图队列空 → 01-meta.png ----
    w->set_current_page(1);
    pump(200);
    wait_thumbs(kThumbWaitMs);
    smoke_grab(shots_dir, kSmokeShots[kShotMeta]);

    // ---- 断言 a：MapWidget 存在且 grab 非空 ----
    pp::map::MapWidget *map = w->findChild<pp::map::MapWidget *>();
    if (map == nullptr) {
        smoke_fail(MainWindow::tr("地图控件缺失（findChild<MapWidget*> 为空）"));
    } else {
        const QImage image = map->grab().toImage();
        if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
            smoke_fail(MainWindow::tr("地图 grab 返回空图"));
        }
        // ---- 断言 b：amap 基准面边界（GCJ-02 ↔ WGS-84）----
        smoke_amap_boundary(map);
    }

    // ---- 页 2（输出）：参数谓词 → 02-output.png ----
    w->set_current_page(2);
    pump(200);
    page_output->select_format(QStringLiteral("jxl"));
    pump(300);

    ParamForm *form = w->findChild<ParamForm *>(QStringLiteral("pp-param-form"));
    if (form == nullptr) {
        smoke_fail(MainWindow::tr("输出页 ParamForm 缺失（pp-param-form）"));
    } else {
        if (!form->is_param_visible("distance")) {
            smoke_fail(MainWindow::tr("jxl：distance 参数应可见"));
        }
        // jxl + 无损 → 自动 modular 且 distance 锁定 0.0（§4.3 谓词断言）
        form->set_selection(FormSelection{std::string(), std::string(), true});
        const FormSelection lossless_sel = form->selection();
        if (lossless_sel.tech != "modular") {
            smoke_fail(MainWindow::tr("jxl 无损：技术未切到 modular（当前 %1）")
                           .arg(QString::fromStdString(lossless_sel.tech)));
        }
        const pp::ParamSet values = form->values();
        const auto it = values.find("distance");
        const double *distance = it == values.end() ? nullptr : std::get_if<double>(&it->second);
        if (distance == nullptr || *distance != 0.0) {
            smoke_fail(MainWindow::tr("jxl 无损：distance 未锁定为 0.0"));
        }
        form->set_selection(FormSelection{}); // 还原首个后端/技术
        pump(120);
    }
    // 复位 jxl 默认参数（上面的无损断言把 distance 锁到 0.0；保留了截图就不是默认态，
    // 实跑前置也要求 "jxl 默认参"）。select_format 无条件重建 ParamForm → 回到表内默认值。
    page_output->select_format(QStringLiteral("jxl"));
    pump(200);
    smoke_grab(shots_dir, kSmokeShots[kShotOutput]);

    // ---- 02b-output-avif.png：avif 运行时内省 + 位深探测落定（§4.3）→ 还原 jxl ----
    page_output->select_format(QStringLiteral("avif"));
    pump(300);
    smoke_grab(shots_dir, kSmokeShots[kShotAvif]);
    page_output->select_format(QStringLiteral("jxl"));
    pump(200);

    // jpeg：quality_mode=quality → quality 可见且 distance 隐藏（§4.3）
    page_output->select_format(QStringLiteral("jpeg"));
    pump(300);
    form = w->findChild<ParamForm *>(QStringLiteral("pp-param-form"));
    if (form == nullptr) {
        smoke_fail(MainWindow::tr("jpeg：ParamForm 缺失"));
    } else {
        form->set_values(pp::ParamSet{{"quality_mode", std::string("quality")}});
        pump(80);
        if (!form->is_param_visible("quality")) {
            smoke_fail(MainWindow::tr("jpeg（quality 模式）：quality 应可见"));
        }
        if (form->is_param_visible("distance")) {
            smoke_fail(MainWindow::tr("jpeg（quality 模式）：distance 应隐藏"));
        }
    }

    // tiff：compression 恒可见；compression=none → deflate_level 隐藏（§4.3）
    page_output->select_format(QStringLiteral("tiff"));
    pump(300);
    form = w->findChild<ParamForm *>(QStringLiteral("pp-param-form"));
    if (form == nullptr) {
        smoke_fail(MainWindow::tr("tiff：ParamForm 缺失"));
    } else {
        if (!form->is_param_visible("compression")) {
            smoke_fail(MainWindow::tr("tiff：compression 应可见"));
        }
        form->set_values(pp::ParamSet{{"compression", std::string("none")}});
        pump(80);
        if (form->is_param_visible("deflate_level")) {
            smoke_fail(MainWindow::tr("tiff（none）：deflate_level 应隐藏"));
        }
    }

    // ---- 实跑前置：out_root=<仓库>/.cache/tmp/ui-smoke-out + conflict=overwrite ----
    page_output->select_format(QStringLiteral("jxl"));
    pump(250);
    QString root_dir = repo_root();
    if (root_dir.isEmpty()) {
        root_dir = QDir::currentPath();
        std::fprintf(stderr, "ui-smoke: repo root not found; out_root falls back to %s\n",
                     qUtf8Printable(root_dir));
    }
    const QString out_root = root_dir + QStringLiteral("/.cache/tmp/ui-smoke-out");
    QDir(out_root).removeRecursively();
    if (!QDir().mkpath(out_root)) {
        smoke_fail(MainWindow::tr("无法创建输出目录：%1").arg(out_root));
    }
    page_output->set_out_root(out_root);
    if (page_output->out_root() != out_root) {
        smoke_fail(MainWindow::tr("冒烟前置：输出根目录未生效（%1）").arg(page_output->out_root()));
    }
    // M4-T14 实跑前置：输出 = **JPEG + WebP**（mockup run-dark 任务行的两格式组合）——让
    // 03-run.png 的「逐输出子行」在真实运行下呈现 N=2 行（§3.4「多格式时一个源文件 N 行输出」），
    // 并顺带覆盖多输出管线的 UI 事件流与"真实/合成"两类进度。走冻结 API 的 collect/apply 往返
    // （select_format 是单选钩子，构造多选集合只能经预设面），同时把冲突策略置为覆盖。
    {
        page_output->select_format(QStringLiteral("jpeg"));
        pump(250);
        pp::PresetData preset = page_output->collect_preset(QStringLiteral("ui-smoke"));
        if (preset.outputs.size() != 1) {
            smoke_fail(MainWindow::tr("冒烟前置：jpeg 单选应收集到 1 个输出（实为 %1）")
                           .arg(preset.outputs.size()));
        }
        const pp::OutputFormatSpec jpeg_spec = preset.outputs.front();
        page_output->select_format(QStringLiteral("webp"));
        pump(250);
        preset = page_output->collect_preset(QStringLiteral("ui-smoke"));
        const pp::OutputFormatSpec webp_spec = preset.outputs.front();
        preset.outputs = {jpeg_spec, webp_spec};
        preset.conflict = pp::ConflictPolicy::Overwrite;
        preset.split_by_format = true; // 分格式子目录（$format/$dir/$file）
        preset.output_template = "$format/$dir/$file";
        page_output->apply_preset(preset);
        pump(250);
    }
    const std::vector<pp::OutputFormatSpec> run_outputs = page_output->config_base().outputs;
    if (run_outputs.size() != 2) {
        smoke_fail(
            MainWindow::tr("冒烟前置：输出格式不是 2 项（实为 %1）").arg(run_outputs.size()));
    }
    if (page_output->config_base().conflict != pp::ConflictPolicy::Overwrite) {
        smoke_fail(MainWindow::tr("冒烟前置：冲突策略不是覆盖"));
    }

    // ---- 页 3（运行）：点开始 → 03-run.png ----
    w->set_current_page(3);
    pump(200);
    start->click();
    // 尽量抓在"运行中"：本批 16 个小文件很短，故只推进到出现部分进度即抓，
    // 否则退化为结束态（与 03b 同图，仅记入 stdout 供审查判断）。
    QProgressBar *run_progress = w->findChild<QProgressBar *>(QStringLiteral("pp-run-progress"));
    {
        QElapsedTimer spin;
        spin.start();
        while (spin.elapsed() < 60) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            const int value = run_progress != nullptr ? run_progress->value() : 0;
            const int maximum = run_progress != nullptr ? run_progress->maximum() : 0;
            if (page_run->is_running() && maximum > 0 && value > 0 && value < maximum)
                break;
            QThread::msleep(1);
        }
    }
    std::printf("UI-SMOKE 03-run: running=%d progress=%d/%d outputs=%d/%d bottom=%s\n",
                page_run->is_running() ? 1 : 0,
                run_progress != nullptr ? run_progress->value() : -1,
                run_progress != nullptr ? run_progress->maximum() : -1, page_run->outputs_done(),
                page_run->outputs_total(), qUtf8Printable(start->text()));
    std::fflush(stdout);
    // G5 锁定视觉（M4-W2-fix 第 7 条 + T14 锁定接线）：运行中「设置/预设」置灰（QSS :disabled =
    // .icon-btn.dim opacity .4 的等效预合成色）、导航段 1/2 置灰（.step.dim opacity .45 口径，
    // 含数字圆；M5-T4：段的使能态 = QAction —— 单控件下不再有独立钮可问）、左栏文件面板/分组
    // 下拉置灰、预览与分类只读、运行页取消可点（唯一取消入口）。
    {
        const bool presets_off = presets_btn != nullptr && !presets_btn->isEnabled();
        const bool settings_off = settings_btn != nullptr && !settings_btn->isEnabled();
        const bool steps_off = nav != nullptr && nav->segment_count() == 3 &&
                               !nav_meta->isEnabled() && !nav_output->isEnabled();
        // 置灰**不得**把矢量图标退化成文本（§3.3「图标全 SVG、禁字形」）：按钮仍须挂矢量图标、
        // 无可见文案 —— 钉住 T4 修复的"QToolButton 被 action 空图标覆盖 → 回落文本"问题。
        const bool icons_kept =
            presets_btn != nullptr && !presets_btn->icon().isNull() &&
            !presets_btn->property(icons::kIconNameProperty).toString().isEmpty() &&
            presets_btn->text().isEmpty() && settings_btn != nullptr &&
            !settings_btn->icon().isNull() &&
            !settings_btn->property(icons::kIconNameProperty).toString().isEmpty() &&
            settings_btn->text().isEmpty();
        const bool panel_off = panel != nullptr && !panel->isEnabled();
        const bool group_off = group_mode != nullptr && !group_mode->isEnabled();
        const bool preview_locked =
            [&] { // 预览只读的**可观测面** = 打标热键表置灰（PreviewPanel::set_locked 的唯一落地）
                QLabel *keyhint =
                    w->findChild<QLabel *>(QStringLiteral("pp-preview-keyhint-label"));
                return keyhint != nullptr && !keyhint->isEnabled();
            }();
        QPushButton *run_cancel = w->findChild<QPushButton *>(QStringLiteral("pp-run-cancel"));
        const bool cancel_ready =
            run_cancel != nullptr && run_cancel->isVisible() && run_cancel->isEnabled();
        const bool new_page_locked = page_meta != nullptr && !page_meta->isEnabled() &&
                                     page_output != nullptr && !page_output->isEnabled();
        std::printf("UI-SMOKE lock-visual: presets=%d settings=%d steps1-2=%d panel=%d group=%d "
                    "preview-locked=%d page1-2=%d run-cancel=%d icons-kept=%d\n",
                    presets_off ? 1 : 0, settings_off ? 1 : 0, steps_off ? 1 : 0, panel_off ? 1 : 0,
                    group_off ? 1 : 0, preview_locked ? 1 : 0, new_page_locked ? 1 : 0,
                    cancel_ready ? 1 : 0, icons_kept ? 1 : 0);
        std::fflush(stdout);
        if (!presets_off || !settings_off || !steps_off || !panel_off || !group_off ||
            !preview_locked || !new_page_locked || !cancel_ready || !icons_kept) {
            smoke_fail(MainWindow::tr("运行期锁定未生效（预设=%1 设置=%2 步骤=%3 文件面板=%4 "
                                      "分组=%5 预览=%6 页1-2=%7 取消钮=%8 图标未退化=%9）")
                           .arg(presets_off ? 1 : 0)
                           .arg(settings_off ? 1 : 0)
                           .arg(steps_off ? 1 : 0)
                           .arg(panel_off ? 1 : 0)
                           .arg(group_off ? 1 : 0)
                           .arg(preview_locked ? 1 : 0)
                           .arg(new_page_locked ? 1 : 0)
                           .arg(cancel_ready ? 1 : 0)
                           .arg(icons_kept ? 1 : 0));
        }
    }
    // ---- M4-T14 断言：真实运行下的**逐输出行**（§3.4：行数 = 文件数 × 格式数；运行中的行有
    // 阶段文字/进度读数；锁定态的行不因运行而消失）----
    {
        const QList<QWidget *> bars = w->findChildren<QWidget *>(
            QRegularExpression(QStringLiteral("^pp-run-bar-\\d+-\\d+$")));
        const QList<QWidget *> groups =
            w->findChildren<QWidget *>(QRegularExpression(QStringLiteral("^pp-run-group-\\d+$")));
        const int expect_rows = int(model->checked_count()) * int(run_outputs.size());
        std::printf("UI-SMOKE run-rows: bars=%d expect=%d groups=%d\n", bars.size(), expect_rows,
                    groups.size());
        std::fflush(stdout);
        if (bars.size() != expect_rows || groups.size() != int(model->checked_count())) {
            smoke_fail(MainWindow::tr("逐输出行数不符：bar=%1 group=%2（期望 %3/%4）")
                           .arg(bars.size())
                           .arg(groups.size())
                           .arg(expect_rows)
                           .arg(model->checked_count()));
        }
    }
    // ---- M4-T14 断言：底栏运行态（mockup run-dark .bottom：状态计数 + ETA + .go 变状态读数）----
    {
        const auto full_text = [](const ElidedLabel *label) {
            if (label == nullptr)
                return QString();
            const QVariant full = label->property("ppFullText");
            return full.isValid() ? full.toString() : label->text();
        };
        const QString bottom = full_text(status);
        const QString eta = full_text(output_status);
        const QString go = start != nullptr ? start->text() : QString();
        std::printf("UI-SMOKE run-bottom: status=\"%s\" eta=\"%s\" go=\"%s\"\n",
                    qUtf8Printable(bottom), qUtf8Printable(eta), qUtf8Printable(go));
        std::fflush(stdout);
        if (!bottom.contains(QStringLiteral("进行中")) ||
            !bottom.contains(QStringLiteral("排队")) || !eta.startsWith(QStringLiteral("ETA")) ||
            !go.startsWith(QStringLiteral("运行中"))) {
            smoke_fail(
                MainWindow::tr("底栏运行态不符（status=%1 eta=%2 go=%3）").arg(bottom, eta, go));
        }
    }
    smoke_grab(shots_dir, kSmokeShots[kShotRun]);

    if (!wait_for([this] { return !page_run->is_running(); }, kRunWaitMs)) {
        smoke_fail(MainWindow::tr("运行未在 %1 ms 内结束").arg(kRunWaitMs));
        if (sched)
            sched->cancel();
        wait_for([this] { return !page_run->is_running(); }, 30000);
    }
    pump(300);
    smoke_grab(shots_dir, kSmokeShots[kShotRunDone]);

    // ---- 断言 d：结束态（未运行 / 进度满 / 摘要可见）----
    if (page_run->is_running()) {
        smoke_fail(MainWindow::tr("运行结束后 is_running() 仍为真"));
    }
    QProgressBar *progress = w->findChild<QProgressBar *>(QStringLiteral("pp-run-progress"));
    if (progress == nullptr) {
        smoke_fail(MainWindow::tr("运行页缺少 pp-run-progress"));
    } else if (progress->maximum() <= 0 || progress->value() != progress->maximum()) {
        smoke_fail(
            MainWindow::tr("进度条未满：%1 / %2").arg(progress->value()).arg(progress->maximum()));
    }
    // ---- M5-T12/A29：**实跑**结束态的「峰值内存」= 引擎真实读数（RunSummary::peak_bytes）----
    //   本批走真实 Scheduler → PixelBudget（每文件 acquire 2×frame）→ 结束态必须是非零读数
    //   （非「—」）；加性字段未接线/未取值时这里红。
    if (QLabel *memory = w->findChild<QLabel *>(QStringLiteral("pp-run-memory"))) {
        const QVariant full = memory->property("ppFullText");
        const QString text = full.isValid() ? full.toString() : memory->text();
        std::printf("UI-SMOKE run-memory: %s\n", qUtf8Printable(text));
        std::fflush(stdout);
        if (text == QStringLiteral("—") || !text.endsWith(QLatin1String("B")))
            smoke_fail(MainWindow::tr("结束态峰值内存非真实读数：\"%1\"").arg(text));
    } else {
        smoke_fail(MainWindow::tr("运行页缺少峰值内存 kv（pp-run-memory）"));
    }
    // M4-T14：摘要句（pp-run-summary）+ 结束态逐输出行定稿（产物事实/失败原因）
    if (QLabel *summary_line = w->findChild<QLabel *>(QStringLiteral("pp-run-summary"))) {
        const QString text = summary_line->property("ppFullText").toString();
        std::printf("UI-SMOKE summary: %s\n", qUtf8Printable(text));
        if (text.isEmpty())
            smoke_fail(MainWindow::tr("运行摘要为空（pp-run-summary）"));
    } else {
        smoke_fail(MainWindow::tr("运行页缺少摘要行（pp-run-summary）"));
    }
    if (QLabel *run_pill = w->findChild<QLabel *>(QStringLiteral("pp-run-state-pill"))) {
        // M5-T9：状态胶囊 = formkit::PillBadge（自绘；语义值在 text()，不挂 ppFullText）。
        // 全成功批次 → 完成态唯一形态（绿胶囊「已完成」+ 矢量勾，修 3-12）。
        auto *pill = static_cast<formkit::PillBadge *>(run_pill);
        std::printf("UI-SMOKE run state: %s tone=%d icon=%d\n", qUtf8Printable(pill->text()),
                    int(pill->tone()), pill->has_icon() ? 1 : 0);
        std::fflush(stdout);
        if (pill->text() != QStringLiteral("已完成") ||
            pill->tone() != formkit::PillBadge::Tone::Ok || !pill->has_icon()) {
            smoke_fail(MainWindow::tr("完成态状态胶囊不符（text=%1 tone=%2 icon=%3）")
                           .arg(pill->text())
                           .arg(int(pill->tone()))
                           .arg(pill->has_icon() ? 1 : 0));
        }
    }
    {
        // 终态逐输出行：每行 bar 已结算（done/warn/failed，均满格）；完成行给出产物事实
        // （大小列 = 字节 / 速度列 = 压缩比 × 并带基线 tooltip），失败行给出原因行
        // —— 数据源 = 引擎的 OutputResult（§3.4）。M5-T9 机械换锚：旧「pct 列 + stage 列」
        // 拆成 大小/速度/耗时 三列 + 状态胶囊 + 失败原因行（语义不变，取证更细）。
        const QList<QWidget *> bars = w->findChildren<QWidget *>(
            QRegularExpression(QStringLiteral("^pp-run-bar-\\d+-\\d+$")));
        int settled = 0, done_rows = 0, failed_rows = 0, with_bytes = 0, with_ratio = 0;
        QString first_failure;
        const auto label_text = [](QLabel *label) {
            if (label == nullptr)
                return QString();
            const QVariant full = label->property("ppFullText");
            return full.isValid() ? full.toString() : label->text();
        };
        for (QWidget *bar : bars) {
            const QString kind = bar->property("ppBarKind").toString();
            if (kind != QLatin1String("pending") && kind != QLatin1String("active"))
                ++settled;
            if (kind == QLatin1String("done"))
                ++done_rows;
            if (kind == QLatin1String("failed"))
                ++failed_rows;
            const QString name = bar->objectName();
            const QString suffix = name.mid(int(QStringLiteral("pp-run-bar-").size()));
            // 大小列（完成 = 字节；0 字节 = 异常档，仍给真实「0 B」而非四舍五入的 KB）
            const QString size_text =
                label_text(w->findChild<QLabel *>(QStringLiteral("pp-run-size-") + suffix));
            if (size_text.endsWith(QLatin1String("B")) && size_text != QLatin1String("—"))
                ++with_bytes;
            // 速度列（压缩比倍数 + 基线 tooltip）
            auto *speed = w->findChild<QLabel *>(QStringLiteral("pp-run-speed-") + suffix);
            if (speed != nullptr && label_text(speed).contains(QStringLiteral("×")) &&
                speed->toolTip().contains(QStringLiteral("相对源文件")))
                ++with_ratio;
            if (kind == QLatin1String("failed") && first_failure.isEmpty())
                first_failure = label_text(
                    w->findChild<QLabel *>(QStringLiteral("pp-run-reason-text-") + suffix));
        }
        std::printf("UI-SMOKE run-rows-final: total=%d settled=%d done=%d failed=%d bytes=%d "
                    "ratio=%d first-failure=\"%s\"\n",
                    bars.size(), settled, done_rows, failed_rows, with_bytes, with_ratio,
                    qUtf8Printable(first_failure));
        std::fflush(stdout);
        if (settled != bars.size() || bars.isEmpty())
            smoke_fail(
                MainWindow::tr("结束态仍有未结算输出行（%1/%2）").arg(settled).arg(bars.size()));
        if (done_rows > 0 && (with_bytes == 0 || with_ratio == 0))
            smoke_fail(MainWindow::tr("完成行未显示产物事实（字节=%1 压缩比=%2）")
                           .arg(with_bytes)
                           .arg(with_ratio));
        if (failed_rows > 0 && first_failure.isEmpty())
            smoke_fail(MainWindow::tr("失败行未直显原因"));
    }
    // 日志尾卡（§7.3）：单一持有者 = MainWindow 读 run-*.log 末尾、页只渲染。**--ui-smoke 分支
    // 不写磁盘日志**（main.cpp 冒烟分支只走 stderr，§4.3）→ 这里如实打印实时读到的内容（空 =
    // 页面空态文案「尚无日志 · 开始运行后在此显示最新记录」）；**填充态**由 smoke_probe_run 的
    // 独立页探针取证（03d）。
    if (QLabel *log_tail = w->findChild<QLabel *>(QStringLiteral("pp-run-log-lines"))) {
        const QString shown = log_tail->text();
        QString flat = shown.left(200);
        flat.replace(QLatin1Char('\n'), QLatin1String(" | "));
        std::printf("UI-SMOKE run-log-tail: %s\n",
                    flat.isEmpty() ? "(empty)" : qUtf8Printable(flat));
        std::fflush(stdout);
    } else {
        smoke_fail(MainWindow::tr("运行页缺少日志尾控件（pp-run-log-lines）"));
    }

    // ---- M4-T14：运行页自检（独立页实例：逐输出行五态 / 真实 vs 斜纹像素取证 / 取消态 /
    // 浅色）+ M5-T9 的 C7 几何探针（run-geom）----
    smoke_probe_run(shots_dir);
    smoke_probe_run_geometry();
    // M5-T13（W-G4 验收）：§12-13 亮暗布局等价 + §12-14 hint 禁折行（探针只增；结束归位）
    smoke_probe_verification();
    // ---- M7-R2E2：第 3 波常驻探针（R-218 / UX-1 / UX-2 / R-070 / R-078 / UX-8 / UX-10.1）----
    smoke_probe_rtl_mirror();
    smoke_probe_check_all();
    smoke_probe_context_menus();
    smoke_probe_run_reason();
    smoke_probe_advanced_folds();
    // ---- M7-R2.1-F4 第 4 波常驻探针（R-148/149、R-143、R-152/153、UX-12 像素档）----
    smoke_probe_intake();
    smoke_probe_class_filter();
    smoke_probe_edit_target();
    smoke_probe_settings_tabs_light();
    // ---- M7-R2.2-G1 探针总集（format_selection_model 往返 / R-218 深水区）----
    {
        const std::function<void(const QString &)> g1_fail = [this](const QString &reason) {
            smoke_fail(reason);
        };
        probe_format_roundtrip(page_output, g1_fail);
        probe_rtl_deep(w, g1_fail);
        probe_color_options(page_output, g1_fail);
    }

    // ---- M7-R2.2-G1：0 字节补齐双跑 + R-144 失败清单（真实编排；M7R22G1-10/-11）----
    //   语义面 = R-006 / D1⑤-3 / M7-R2D1：「再次运行」= 逐文件预检只提交缺件（0 字节按缺件）、
    //   跑前移除 0 字节产物（非空成品与源文件绝不触碰）、本轮冲突策略 Skip；
    //   R-144 = 真实失败运行（损坏样本）→ run_failure_lines「路径\t原因」行式落盘
    //   （write_failure_list）+ 状态栏「N 项失败」tooltip 出处/手型 + 点击下钻运行页。
    //   真实用户路径 = pp-run-again 钮（on_run_again → run_again_pending → on_start 补齐分支）。
    {
        int z_checked = 0;
        int z_bad = 0;
        int f_checked = 0;
        int f_bad = 0;
        const auto z_expect = [&](bool ok, const QString &what) {
            ++z_checked;
            if (!ok) {
                ++z_bad;
                smoke_fail(MainWindow::tr("zero-byte-redo：%1").arg(what));
            }
        };
        const auto f_expect = [&](bool ok, const QString &what) {
            ++f_checked;
            if (!ok) {
                ++f_bad;
                smoke_fail(MainWindow::tr("failure-list：%1").arg(what));
            }
        };
        struct OutFact {
            qint64 size = -1;
            qint64 mtime = -1;
        };
        const auto scan_outputs = [&out_root]() {
            QHash<QString, OutFact> map;
            QDir root(out_root);
            QDirIterator it(out_root, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString path = it.next();
                const QFileInfo info(path);
                map.insert(root.relativeFilePath(path),
                           OutFact{info.size(), info.lastModified().toMSecsSinceEpoch()});
            }
            return map;
        };
        const auto scan_sources = [this]() {
            QHash<QString, qint64> map;
            for (std::size_t i = 0; i < model->size(); ++i) {
                const QString src = QString::fromStdString(model->row(i).entry.src.string());
                map.insert(src, QFileInfo(src).size());
            }
            return map;
        };
        // ① 产物面台账 + 选 A（首个产物）截断 0 字节；B = 其余产物（同文件另一格式最强）
        const QHash<QString, OutFact> before = scan_outputs();
        z_expect(before.size() >= 2, QStringLiteral("实跑产物 ≥2（got %1）").arg(before.size()));
        QStringList keys = before.keys();
        keys.sort();
        const QString key_a = keys.isEmpty() ? QString() : keys.first();
        const QString path_a = out_root + QLatin1Char('/') + key_a;
        {
            QFile f(path_a);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.close();
        }
        z_expect(QFileInfo(path_a).size() == 0, QStringLiteral("A 已截断 0 字节"));
        // ② 损坏样本（真实失败源；探针自建自清，不碰任何既有源文件）
        const QString corrupt = QDir::tempPath() + QStringLiteral("/pp-ui-smoke-corrupt.jpg");
        {
            QFile f(corrupt);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(QByteArrayLiteral("\xff\xd8\xff\x00not-a-real-jpeg-payload"));
                f.close();
            }
        }
        const QHash<QString, qint64> src_before = scan_sources();
        model->add_paths(QStringList{corrupt});
        pump(80);
        // ③ 「再次运行」（真实用户路径；run_failed==0 ⇒ on_run_again 无确认框）
        auto *again = page_run->findChild<QAbstractButton *>(QStringLiteral("pp-run-again"));
        z_expect(again != nullptr, QStringLiteral("pp-run-again 在位"));
        if (again != nullptr)
            again->click();
        const bool finished = wait_for([this] { return !page_run->is_running(); }, kRunWaitMs);
        z_expect(finished, QStringLiteral("补齐运行在 %1ms 内收尾").arg(kRunWaitMs));
        pump(150);
        // ④ 0 字节重做 / 非空不动 / 源只读
        const QHash<QString, OutFact> after = scan_outputs();
        const qint64 a_size = QFileInfo(path_a).size();
        z_expect(a_size > 0, QStringLiteral("0 字节产物 A 重做非零（got %1）").arg(a_size));
        int touched = 0;
        for (auto it = before.constBegin(); it != before.constEnd(); ++it) {
            if (it.key() == key_a)
                continue;
            const OutFact now = after.value(it.key());
            if (now.size != it.value().size || now.mtime != it.value().mtime)
                ++touched;
        }
        z_expect(touched == 0,
                 QStringLiteral("非空成品不动（%1 个产物字节/mtime 逐位不变，A 除外）")
                     .arg(before.size() - 1));
        const QHash<QString, qint64> src_after = scan_sources();
        int src_touched = 0;
        for (auto it = src_before.constBegin(); it != src_before.constEnd(); ++it) {
            if (!src_after.contains(it.key()) || src_after.value(it.key()) != it.value())
                ++src_touched;
        }
        z_expect(src_touched == 0,
                 QStringLiteral("源面只读（%1 个源 size 不变且不消失）").arg(src_before.size()));
        // ⑤ R-144：失败清单行式 + 落盘逐行 + 状态栏下钻
        f_expect(!run_failure_lines.isEmpty(),
                 QStringLiteral("真实失败行入账（got %1）").arg(run_failure_lines.size()));
        bool has_corrupt = false;
        bool has_tab = false;
        const QString corrupt_name = QFileInfo(corrupt).fileName();
        for (const QString &line : run_failure_lines) {
            // 路径形态跨平台（fs::native 分隔符）⇒ 按文件名命中（语义 = 该样本的失败行入账）
            if (line.contains(corrupt_name))
                has_corrupt = true;
            if (line.contains(QLatin1Char('\t')))
                has_tab = true;
        }
        f_expect(has_corrupt && has_tab,
                 QStringLiteral("行式 = 路径\\t原因（corrupt 入账 %1 / tab %2）")
                     .arg(has_corrupt ? 1 : 0)
                     .arg(has_tab ? 1 : 0));
        f_expect(!failure_list_path.isEmpty() && QFileInfo::exists(failure_list_path),
                 QStringLiteral("失败清单已落盘（%1）").arg(failure_list_path));
        QStringList file_lines;
        {
            QFile f(failure_list_path);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                while (!f.atEnd())
                    file_lines << QString::fromUtf8(f.readLine()).chopped(1);
            }
        }
        f_expect(file_lines == run_failure_lines,
                 QStringLiteral("落盘逐行 == run_failure_lines（%1 vs %2 行）")
                     .arg(file_lines.size())
                     .arg(run_failure_lines.size()));
        if (status != nullptr) {
            // 瞬态状态备注（set_status_note + 定时器）会临时占用 tooltip/手型呈现位 ——
            // 先清备注（同等待时器到点的等效动作）再验 R-144 的失败段下钻呈现。
            set_status_note(QString());
            pump(80);
            f_expect(status->toolTip().contains(failure_list_path),
                     QStringLiteral("状态栏 tooltip 含落盘出处（%1）").arg(status->toolTip()));
            f_expect(status->cursor().shape() == Qt::PointingHandCursor,
                     QStringLiteral("失败段手型（下钻入口）"));
            // 下钻判定自适应页序（栈下标语法不入断言）：先测运行页栈位，再离开，再点击对比
            w->set_current_page(3);
            pump(60);
            const int run_idx = stack != nullptr ? stack->currentIndex() : -1;
            w->set_current_page(1);
            pump(60);
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(4, 4), QPointF(4, 4),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(status, &press);
            pump(60);
            f_expect(stack != nullptr && stack->currentIndex() == run_idx && run_idx >= 0,
                     QStringLiteral("点击下钻到运行页（got %1 want %2）")
                         .arg(stack != nullptr ? stack->currentIndex() : -1)
                         .arg(run_idx));
        } else {
            f_expect(false, QStringLiteral("pp-status 缺失"));
        }
        // ⑥ 自清 + 归位：移除损坏行、删探针自建样本（run_failure_lines/failure_list_path
        //    保留实况 = 下一轮 run-again 自己清算；不伪造归位）
        for (std::size_t i = 0; i < model->size(); ++i) {
            if (QString::fromStdString(model->row(i).entry.src.string()) == corrupt) {
                QList<int> drop;
                drop << int(i);
                model->remove_rows(drop);
                break;
            }
        }
        QFile::remove(corrupt);
        std::printf("UI-SMOKE zero-byte-redo: checked=%d bad=%d outputs=%d a=%lld\n", z_checked,
                    z_bad, int(before.size()), static_cast<long long>(a_size));
        std::fflush(stdout);
        std::printf("UI-SMOKE failure-list: checked=%d bad=%d lines=%d\n", f_checked, f_bad,
                    int(run_failure_lines.size()));
        std::fflush(stdout);
    }

    // ---- 对话框三连（构造 → show → processEvents → grab → close，绝不 exec）----
    { // 04-settings.png：设置对话框
        SettingsDialog dlg(settings, w);
        dlg.show();
        pump(250);
        smoke_grab(shots_dir, kSmokeShots[kShotSettings], &dlg);
        // ---- M5-T10（§7.4 重做）：C7 几何探针（值表驱动；判据实现与控件同 TU =
        // settings_dialog.cpp 的 dialogkit::settings_geom_probe，调用方只打印与 smoke_fail）----
        // 位置：截图之后、accept 之前（accept 会隐藏对话框 → 焦点环/可见面几何失效）。
        {
            const dialogkit::GeomProbeReport geom = dialogkit::settings_geom_probe(dlg);
            std::printf("UI-SMOKE settings-geom: checked=%d bad=%d %s\n", geom.checked,
                        static_cast<int>(geom.bad.size()),
                        qUtf8Printable(geom.bad.join(QLatin1Char(' '))));
            std::fflush(stdout);
            if (!geom.bad.isEmpty()) {
                smoke_fail(MainWindow::tr("设置弹窗几何（§7.4/A21）不符：%1")
                               .arg(geom.bad.join(QLatin1Char(' '))));
            }
        }
        // ---- M4-T15（§9.3 设置追加项）：控件 → AppSettings 读回自检 ----
        // 不新增截图、不改冻结末行。两侧都验：未 accept（非确定路径）= 构造快照原样返回
        // （§2.10）；accept 后 = 控件值，且"分文件夹默认结构"写回 output_template +
        // split_by_format 一对（§3.6 两字段）。
        {
            const pp::AppSettings snapshot = dlg.settings();
            auto *stagger = dlg.findChild<QSpinBox *>(QStringLiteral("pp-stagger-ms"));
            // M5-T10 机械换锚（修 4-08「禁 spinbox 塞文本魔法值」）：「线程预算」的控件类型由
            // spinbox 改为「自动/数值」下拉 → 这里的查找类型随之换锚；objectName 与
            // 「0 = 自动」的读回语义不变，断言本体（set 5 → 读回 5）逐字不变。
            auto *threads = dlg.findChild<QComboBox *>(QStringLiteral("pp-thread-budget"));
            auto *structure = dlg.findChild<QComboBox *>(QStringLiteral("pp-folder-structure"));
            if (stagger == nullptr || threads == nullptr || structure == nullptr) {
                smoke_fail(MainWindow::tr("设置对话框缺少 T15 追加项控件（pp-stagger-ms / "
                                          "pp-thread-budget / pp-folder-structure）"));
            } else {
                stagger->setValue(250);
                const int threads_idx = threads->findData(5);
                if (threads_idx >= 0) {
                    threads->setCurrentIndex(threads_idx);
                }
                const int idx = structure->findData(QStringLiteral("$dir/$format/$file"));
                if (idx >= 0) {
                    structure->setCurrentIndex(idx);
                }
                dlg.accept();
                const pp::AppSettings edited = dlg.settings();
                const bool ok = snapshot.stagger_ms == settings.stagger_ms &&
                                snapshot.thread_budget == settings.thread_budget &&
                                snapshot.output_template == settings.output_template &&
                                snapshot.split_by_format == settings.split_by_format &&
                                edited.stagger_ms == 250 && edited.thread_budget == 5 &&
                                edited.output_template == "$dir/$format/$file" &&
                                edited.split_by_format;
                std::printf("UI-SMOKE settings-dialog: stagger=%d threads=%d tmpl=%s split=%d "
                            "(snapshot stagger=%d threads=%d tmpl=%s)\n",
                            edited.stagger_ms, edited.thread_budget, edited.output_template.c_str(),
                            edited.split_by_format ? 1 : 0, snapshot.stagger_ms,
                            snapshot.thread_budget, snapshot.output_template.c_str());
                std::fflush(stdout);
                if (!ok) {
                    smoke_fail(MainWindow::tr("设置追加项读回不符（stagger=%1 threads=%2 tmpl=%3 "
                                              "split=%4）")
                                   .arg(edited.stagger_ms)
                                   .arg(edited.thread_budget)
                                   .arg(QString::fromStdString(edited.output_template))
                                   .arg(edited.split_by_format ? 1 : 0));
                }
            }
        }
        // ---- M5-T11（A28）：主题三档写回 + 持久化往返 + 启动读取点的解析链 ----
        // 位置：T15 读回之后、close 之前；**不改变任何终态**（三档逐一取值后复位到原档，
        // 持久化覆盖与临时文件都还原/清理）→ 后继截图与探针基线不受影响。
        {
            auto *mode =
                dlg.findChild<formkit::SegmentedControl *>(QStringLiteral("pp-theme-mode"));
            int bad = 0;
            QString detail;
            if (mode == nullptr || mode->count() != 3) {
                ++bad;
                detail = QStringLiteral("seg-missing");
            } else {
                const int keep = mode->current_index();
                // 三档写回：档位 → AppSettings.ui_theme_mode（system/light/dark 逐档一致）
                for (int i = 0; i < 3; ++i) {
                    mode->set_current_index(i);
                    const QString got = QString::fromStdString(dlg.settings().ui_theme_mode);
                    const QString want =
                        QString::fromLatin1(i == 0 ? "system" : (i == 1 ? "light" : "dark"));
                    if (got != want) {
                        ++bad;
                        detail += QStringLiteral("%1→%2(期望 %3) ").arg(i).arg(got, want);
                    }
                }
                mode->set_current_index(keep); // 复位（探针不改变被检对象的终态）
                // 即时生效链（A28「选后立即生效」）：点「亮色」段 → 全局档翻转 + 解析链同档；
                // 随后点回原档复位（同一进程环境与基线不受影响）
                if (QAbstractButton *light = qobject_cast<QAbstractButton *>(mode->segment(1))) {
                    light->click();
                    pump(40);
                    const bool instant = mode->current_index() == 1 &&
                                         theme::preferred_theme_mode() == theme::ThemeMode::Light;
                    if (!instant) {
                        ++bad;
                        detail += QStringLiteral("instant(click→%1) ")
                                      .arg(theme::theme_mode_id() == QByteArrayLiteral("light")
                                               ? QStringLiteral("light")
                                               : QStringLiteral("<none>"));
                    }
                    if (QAbstractButton *back =
                            qobject_cast<QAbstractButton *>(mode->segment(keep))) {
                        back->click(); // 复位到探针进入时的档
                        pump(40);
                    }
                }
                // 持久化往返：AppSettings 经真实存储层落盘再读回（临时文件，不碰 data_dir）
                pp::AppSettings round = dlg.settings();
                round.ui_theme_mode = "dark";
                const QString tmp =
                    QDir(QDir::tempPath()).filePath(QStringLiteral("pp-m5t11-theme.ini"));
                const std::string err = pp::save_settings(tmp.toStdString(), round);
                const pp::AppSettings back = pp::load_settings(tmp.toStdString());
                if (!err.empty() || back.ui_theme_mode != "dark") {
                    ++bad;
                    detail += QStringLiteral("roundtrip(%1) ").arg(QString::fromStdString(err));
                }
                QFile::remove(tmp);
                // 启动读取点：**与 MainWindow 构造期同一函数** apply_startup_theme()
                // （settings.ui_theme_mode → 进程级持久化档 → 落地模式）；PP_UI_THEME 强制档
                // 优先，故仅在未设强制档时取证（强制档下持久化档按设计让位）。
                const QByteArray keep_persisted = theme::persisted_theme_mode();
                const QByteArray forced = qgetenv("PP_UI_THEME").trimmed().toLower();
                if (forced.isEmpty()) {
                    const bool dark_ok = apply_startup_theme("dark") == theme::ThemeMode::Dark &&
                                         theme::theme_mode_id() == QByteArrayLiteral("dark") &&
                                         theme::persisted_theme_mode() == QByteArrayLiteral("dark");
                    const bool light_ok =
                        apply_startup_theme("light") == theme::ThemeMode::Light &&
                        theme::theme_mode_id() == QByteArrayLiteral("light") &&
                        theme::persisted_theme_mode() == QByteArrayLiteral("light");
                    const bool system_ok =
                        apply_startup_theme("system") == theme::system_theme_mode() &&
                        theme::persisted_theme_mode().isEmpty();
                    theme::set_persisted_theme_mode(keep_persisted); // 复位到探针进入时的档
                    if (!dark_ok || !light_ok || !system_ok) {
                        ++bad;
                        detail += QStringLiteral("startup(dark=%1 light=%2 system=%3) ")
                                      .arg(dark_ok ? 1 : 0)
                                      .arg(light_ok ? 1 : 0)
                                      .arg(system_ok ? 1 : 0);
                    }
                }
                // 端到端（A28「主窗启动读取点」的**构造路径**）：用 ui_theme_mode=light 的
                // AppSettings 造一个主窗 —— 这正是新进程启动时的同一构造序列（磁盘 → AppSettings
                // 的一段由 test_settings 的往返单测覆盖）→ 窗口必须落到浅色档。随后销毁并复位
                // 本窗主题与全局持久化档（后继截图/探针基线不受影响）。
                {
                    pp::AppSettings light_settings;
                    light_settings.ui_theme_mode = "light";
                    auto *probe_window = new MainWindow(light_settings, nullptr);
                    pump(120);
                    // 注入 + 落地：持久化档被写进进程覆盖；窗口落**单源解析**的档（未设
                    // PP_UI_THEME 强制档时即浅色；强制档在场时按设计落强制档 —— 判据随之取
                    // 同一函数的结果，不写死浅色）+ tokens 与档一致。
                    const theme::ThemeMode want = theme::preferred_theme_mode();
                    const bool landed =
                        theme::persisted_theme_mode() == QByteArrayLiteral("light") &&
                        probe_window->impl_->theme_mode == want &&
                        probe_window->impl_->tokens.dark() == (want == theme::ThemeMode::Dark);
                    delete probe_window;
                    pump(80);
                    theme::set_persisted_theme_mode(keep_persisted);
                    refresh_theme(); // 重放 QApplication 调色板/字体 + 本窗
                                     // QSS（探针窗口改过全局调色板）
                    pump(80);
                    if (!landed) {
                        ++bad;
                        detail += QStringLiteral("mainwindow-startup ");
                    }
                }
            }
            // 单行诊断（前缀 = tests/ui_smoke.py 的 T11 清单）：checked = 三档写回 3 +
            // 即时生效（点段）1 + 存储往返 1 + 启动读取点 3（强制档在场时启动链让位 → 少 3 项）
            // + 主窗构造路径端到端 1
            const int total = (mode != nullptr && mode->count() == 3)
                                  ? (qgetenv("PP_UI_THEME").trimmed().isEmpty() ? 9 : 6)
                                  : 0;
            std::printf("UI-SMOKE settings-theme: checked=%d bad=%d mode=\"%s\" %s\n", total, bad,
                        qUtf8Printable(mode != nullptr
                                           ? QString::fromStdString(dlg.settings().ui_theme_mode)
                                           : QString()),
                        qUtf8Printable(detail.trimmed()));
            std::fflush(stdout);
            if (bad != 0) {
                smoke_fail(MainWindow::tr("主题档持久化（A28）不符：%1").arg(detail));
            }
        }
        dlg.close();
        pump(80);
    }
    { // 05-exif-editor.png：确定性 fixture 的 EXIF 编辑器（reject：不产生编辑）
        // §M2-T16：不再取 model->row(0)——那依赖 readdir 顺序（CI 干净 checkout 必红）。
        const std::optional<pp::FileEntry> fixture = pick_smoke_exif_fixture(model->entries());
        if (model->empty()) {
            smoke_fail(MainWindow::tr("文件列表为空，无法打开 EXIF 编辑器"));
        } else if (!fixture.has_value()) {
            smoke_fail(MainWindow::tr("EXIF 编辑器：语料中没有含 IFD0 叶子的候选 fixture"
                                      "（已枚举 %1 个文件；候选：%2）")
                           .arg(static_cast<int>(model->size()))
                           .arg(smoke_exif_fixture_names()));
        } else {
            const pp::FileEntry &entry = *fixture;
            const bool fixture_had_exception = entry.exception.has_value();
            // 诊断行（新增，不改冻结末行）：CI 日志据此核对本次实际使用的 fixture
            std::printf("UI-SMOKE exif-fixture=%s\n",
                        qUtf8Printable(smoke_fixture_label(entry.src, repo_root())));
            std::fflush(stdout);
            ExifEditor dlg(QString::fromStdString(entry.src.string()), page_meta->rules(),
                           entry.exception, w);
            dlg.setProperty("pp_exif_editor_suppress_modal", true); // 模态短路（U8 口径）
            dlg.show();
            pump(400);
            // §9.1 U10-FIX：先程序化选中一个叶子标签（IFD0 下首个），使右值区有键名+值再 grab
            // （U8 挂在 selectionModel 的 currentChanged 上，setCurrentItem 同样触发）
            if (QTreeWidget *tree = dlg.findChild<QTreeWidget *>(QStringLiteral("exif_tree"))) {
                QTreeWidgetItem *group = nullptr;
                for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                    QTreeWidgetItem *node = tree->topLevelItem(i);
                    if (node != nullptr && node->text(0) == QStringLiteral("IFD0")) {
                        group = node;
                        break;
                    }
                }
                if (group == nullptr && tree->topLevelItemCount() > 0) {
                    group = tree->topLevelItem(0);
                }
                QTreeWidgetItem *leaf = nullptr;
                if (group != nullptr) {
                    tree->expandItem(group);
                    for (int i = 0; i < group->childCount(); ++i) {
                        QTreeWidgetItem *child = group->child(i);
                        if (child != nullptr && child->childCount() == 0) {
                            leaf = child;
                            break;
                        }
                    }
                }
                if (leaf == nullptr) {
                    smoke_fail(MainWindow::tr("EXIF 编辑器：IFD0 下找不到叶子标签，右值区将为空"));
                } else {
                    tree->setCurrentItem(leaf);
                    pump(200);
                    std::printf("UI-SMOKE 05-exif-editor selection: %s\n",
                                qUtf8Printable(leaf->text(0)));
                    std::fflush(stdout);
                }
            } else {
                smoke_fail(MainWindow::tr("EXIF 编辑器缺少 exif_tree 控件"));
            }
            pump(150);
            smoke_grab(shots_dir, kSmokeShots[kShotExif], &dlg);
            dlg.reject();
            pump(120);
            // §M2-T16b：断言绑定到本 fixture 行（不再引用 row(0)，与输入顺序彻底解耦）：
            // reject 只能不改变该行的例外状态。
            bool fixture_found = false;
            bool fixture_exception_now = false;
            for (std::size_t i = 0; i < model->size(); ++i) {
                if (model->row(i).entry.src == entry.src) {
                    fixture_exception_now = model->row(i).has_exception();
                    fixture_found = true;
                    break;
                }
            }
            if (!fixture_found) {
                smoke_fail(MainWindow::tr("EXIF 编辑器：reject 后 fixture 行从列表消失"));
            } else if (fixture_exception_now != fixture_had_exception) {
                smoke_fail(MainWindow::tr("EXIF 编辑器 reject 后产生了例外"));
            }
        }
    }
    { // 06-presets.png：预设对话框（§4.3 冻结：空列表 + suggested_name "sample"）
        // M2-T21c：示例名必须是能通过 sanitize_name 的 ASCII（U6 冻结口径 `[A-Za-z0-9_\- ]`）。
        // 旧值 "示例预设" 被清洗成空串 → has_name=false →"另存为"灰化，截图里"框内有名字却
        // 按不动"，与空态提示"输入名称后点\"另存为\"创建"自相矛盾。
        const std::vector<std::pair<QString, QString>> empty_list;
        PresetsDialog dlg(empty_list, QStringLiteral("sample"), w);
        dlg.show();
        pump(250);
        smoke_grab(shots_dir, kSmokeShots[kShotPresets], &dlg);
        dlg.close();
        pump(80);
    }

    // ---- M4-T11：分类面板自检（放在截图之后：自检会临时改写注册表并在结束时还原）----
    smoke_probe_classify();

    // ---- M4-T12：主窗口接线面的活体断言 —— 无选中 → 首个文件 + 「(1/N)」（§5.2）----
    //   （分类自检收尾已清空选中；此处只读页面钩子，不改列表/选中）
    if (!model->empty()) {
        sync_selection();
        pump(150);
        const meta::ProbeContext probe_meta(page_meta);
        const int follow_count = probe_meta.follow_count();
        const QString follow_name = probe_meta.follow_name();
        const auto *hint = page_meta->findChild<QLabel *>(meta::objname::kInfoHint);
        const QString hint_text = hint != nullptr && hint->property("ppFullText").isValid()
                                      ? hint->property("ppFullText").toString()
                                      : (hint != nullptr ? hint->text() : QString());
        const QString expected =
            QStringLiteral("%1 (1/%2)")
                .arg(QFileInfo(QString::fromStdString(model->row(0).entry.src.string())).fileName())
                .arg(model->size());
        std::printf("UI-SMOKE meta-follow-live: count=%d name=%s hint=%s\n", follow_count,
                    qUtf8Printable(QFileInfo(follow_name).fileName()), qUtf8Printable(hint_text));
        std::fflush(stdout);
        if (follow_count != 1 || hint_text != expected) {
            smoke_fail(MainWindow::tr("无选中跟随未回落到首个文件（count=%1 hint=%2 want=%3）")
                           .arg(follow_count)
                           .arg(hint_text, expected));
        }
    }

    // ---- GUI-R2 回归探针（docs/gui-review-0.3.0-r2.md）：下拉一致性 / 格式卡对齐 /
    //   弹窗 caption 关闭钮 / 运行重入守卫。放最末 —— run-guard 会真跑两轮并取消复位，
    //   不得插在任何依赖列表/选中/页面态的断言之前。
    smoke_probe_gui_r2();

    // ---- 断言 e：8 张 PNG 全部存在且 >10KB ----
    if (shots_dir.isEmpty()) {
        std::fprintf(stderr, "ui-smoke: --shots 未给：跳过截图与体积断言（CI 模式）\n");
        smoke_shots = 0;
    } else {
        for (int i = 0; i < kSmokeShotCount; ++i) {
            const char *name = kSmokeShots[i];
            const QFileInfo info(QDir(shots_dir).filePath(QString::fromLatin1(name)));
            std::printf("UI-SMOKE shot %s %lld bytes\n", name, static_cast<long long>(info.size()));
            if (!info.exists() || !info.isFile()) {
                smoke_fail(MainWindow::tr("截图缺失：%1").arg(name));
            } else if (info.size() <= min_shot_bytes(i)) {
                smoke_fail(MainWindow::tr("截图过小（%1 字节，阈值 %2）：%3")
                               .arg(info.size())
                               .arg(min_shot_bytes(i))
                               .arg(name));
            }
        }
        std::fflush(stdout);
        if (smoke_shots != kSmokeShotCount) {
            smoke_fail(MainWindow::tr("截图计数 %1 ≠ %2").arg(smoke_shots).arg(kSmokeShotCount));
        }
    }

    // ---- 布局审计（识图门禁）：放最末 —— 只读几何/实拍，不改任何终态 ----
    smoke_probe_layout_audit();
}

// ---------------------------------------------------------------------------
// 布局审计（识图门禁 · R-识图1，M7-R2.4-G2）——只增。
//
// 起因：debug/debug1.png + debug/debug2.png（2026-10-13，用户红框 7 处）——
// tests/ui_smoke.py 头口径明写「截图仅存档，不做像素比对」：渲染面的裁切 / 重叠 / 空按钮
// 在 format / tidy / ctest / gold smoke / regression / ui-smoke 全部门禁下无人看守。本探针
// 把「识图」机械化成几何 + 像素断言（12 张截图仍按原口径人工留档；冻结末行逐字不变）：
//   ① text-clip  控件画不下自己的文案：下拉 / 标签 / 按钮 / 磁贴 / 输入框占位符
//                → "JPEG XL"→"JPEG…"（磁贴）、「相对比色 + 黑点补偿」被裁半字（下拉）
//   ② overlap    两个叶子控件全局矩形相交 ≥8×8 → 技术行下拉压「无损编码」行
//   ③ blank-btn  QToolButton/QPushButton 无文案无图标；纯图标钮内区墨迹 < 12px（实拍）
//                → 预览「上一张/下一张」两块白方
//   ④ overflow   叶子控件越出父矩形 ≥3px → 行高 28 装不下更高的控件（② 同根因的几何面）
//   ⑤ ghost      无名·无文案·无图标的裸 QWidget/QFrame 空盒 ≥40×18 →「＋ 添加格式…」旁白框
//   ⑥ card-blank pp-card 高 > 可见子件高和 + 24；卡头子件高 > 44 →「关键信息」卡头大片空白
// 扫描口径：几何类判据（②④⑤⑥）只看**当前页可见**件（隐藏件几何是布局跳过后的陈值），
// 三页逐页各扫一遍；文案 / 图标类判据（①③）不看可见性 —— 病灶藏在非当前页（与
// combo-consistency 同源口径），明暗两档各一轮，重复命中按行文去重。
// ---------------------------------------------------------------------------
void MainWindow::Impl::smoke_probe_layout_audit() {
    int checked = 0;
    int bad = 0;
    QStringList detail;
    QSet<QString> seen;
    const auto hit = [&](const QString &item) {
        if (seen.contains(item))
            return; // 明暗 × 翻页重复命中只记一次
        seen.insert(item);
        ++bad;
        if (detail.size() < 8)
            detail << item;
        std::printf("UI-SMOKE layout-audit: %s\n", qUtf8Printable(item)); // 逐条取证（失败可定位）
    };
    const auto tag = [this](const QWidget *x) {
        const QPoint p = x->mapTo(w, QPoint(0, 0)); // 全局（主窗口坐标）—— 重叠定位用
        return QStringLiteral("%1#%2[%3x%4@%5,%6]")
            .arg(QString::fromLatin1(x->metaObject()->className()),
                 x->objectName().isEmpty() ? QStringLiteral("-") : x->objectName())
            .arg(x->width())
            .arg(x->height())
            .arg(p.x())
            .arg(p.y());
    };
    // 祖先链取证（overlap 定位用）：逐级 class#name[w×h@x,y]，直到主窗口
    const auto chain = [this, &tag](const QWidget *x) {
        QStringList parts;
        for (const QWidget *p = x; p != nullptr && parts.size() < 5; p = p->parentWidget())
            parts << (p == w ? QStringLiteral("WIN") : tag(p));
        return parts.join(QStringLiteral(" < "));
    };
    // 墨迹水平范围（逻辑像素）：与**逐行众数底色**偏离 > 24 的像素包围盒；无墨 → {−1,−1}。
    // 入参区域是**逻辑坐标**（控件坐标系），内部按 devicePixelRatio 换算到图像像素——
    // 实拍图 dpr=2 时按物理像素取窗会只取到半幅，把背景算成墨迹。
    // 逐行取底（不是全图众数）：禁用态控件与页面渐变底混合后底色非均匀，全图众数会把
    // 大半底色判成墨迹（chips / 分段钮的假阳即此因）。
    const auto ink_bounds = [](const QImage &img, const QRect &logical_region) {
        struct Span {
            double left = -1.0;
            double right = -1.0;
        };
        Span out;
        if (img.isNull() || img.width() < 2 || img.height() < 2)
            return out;
        const double dpr = img.devicePixelRatioF() > 0 ? img.devicePixelRatioF() : 1.0;
        QRect r =
            logical_region.isEmpty()
                ? img.rect()
                : QRect(QPoint(qRound(logical_region.x() * dpr), qRound(logical_region.y() * dpr)),
                        QSize(qRound(logical_region.width() * dpr),
                              qRound(logical_region.height() * dpr)));
        r = r.intersected(img.rect());
        if (r.isEmpty())
            return out;
        for (int yy = r.top(); yy <= r.bottom(); ++yy) {
            QHash<QRgb, int> hist;
            for (int xx = r.left(); xx <= r.right(); ++xx)
                ++hist[img.pixel(xx, yy)];
            QRgb modal = 0;
            int best = -1;
            for (auto it = hist.constBegin(); it != hist.constEnd(); ++it)
                if (it.value() > best) {
                    best = it.value();
                    modal = it.key();
                }
            const int mr = qRed(modal), mg = qGreen(modal), mb = qBlue(modal);
            for (int xx = r.left(); xx <= r.right(); ++xx) {
                const QRgb p = img.pixel(xx, yy);
                if (std::abs(qRed(p) - mr) + std::abs(qGreen(p) - mg) + std::abs(qBlue(p) - mb) <=
                    24)
                    continue;
                const double lx = double(xx) / dpr;
                if (out.left < 0.0 || lx < out.left)
                    out.left = lx;
                if (out.right < 0.0 || lx > out.right)
                    out.right = lx;
            }
        }
        return out;
    };
    const auto ink_span = [&ink_bounds](const QImage &img, const QRect &logical_region) {
        const auto b = ink_bounds(img, logical_region);
        return b.left < 0.0 ? 0.0 : b.right - b.left + 1.0;
    };
    const auto ink_right = [&ink_bounds](const QImage &img, const QRect &logical_region) {
        return ink_bounds(img, logical_region).right;
    };

    const int page_before = stack != nullptr ? stack->currentIndex() : -1;
    const theme::ThemeMode mode_before = theme_mode;
    for (int theme_pass = 0; theme_pass < 2; ++theme_pass) {
        set_theme_mode(theme_pass == 0 ? theme::ThemeMode::Dark : theme::ThemeMode::Light);
        const int pages = stack != nullptr ? stack->count() : 1;
        for (int page = 0; page < pages; ++page) {
            if (stack != nullptr) {
                stack->setCurrentIndex(page);
                pump(60);
            }
            const QList<QWidget *> all = w->findChildren<QWidget *>();
            QList<QWidget *> leaves;
            for (QWidget *x : all) {
                if (x == nullptr || x->window() != w)
                    continue; // 弹窗是独立 top-level（几何归 dialog-close / settings-dialog）
                if (x->width() < 4 || x->height() < 4)
                    continue;
                if (theme_pass == 0 && page == 0)
                    ++checked;
                // 下拉 / 微调框 / 行编辑 / 页签带 的内部件不判（Qt 自绘 chrome：清除钮、
                // 页签滚动钮等）——几何与文案都由外框承担
                bool internal = false;
                for (QWidget *p = x->parentWidget(); p != nullptr && p != w;
                     p = p->parentWidget()) {
                    if (qobject_cast<const QComboBox *>(p) != nullptr ||
                        qobject_cast<const QAbstractSpinBox *>(p) != nullptr ||
                        qobject_cast<const QLineEdit *>(p) != nullptr ||
                        qobject_cast<const QTabBar *>(p) != nullptr) {
                        internal = true;
                        break;
                    }
                }
                if (internal)
                    continue;
                // 几何/文案判据只看**当前页可见**件：隐藏件（折叠段、空态收起行）几何是布局
                // 跳过后的陈值，拿它比尺寸只会满屏假阳。三页逐页各扫一遍 ⇒ 非当前页照样过堂。
                if (!x->isVisibleTo(w))
                    continue;

                // ---- ① text-clip（识图口径：**实绘墨迹顶到文本区右缘** + 内容声明比）----
                // 判据两路，按控件分：
                //   * 标签 / 文字钮 / 磁贴 —— 布局按**自身 sizeHint** 给宽，「宽 ≥ sizeHint」
                //     即不裁；标签无内距、文字本就贴边，用"墨迹贴边"判会满屏假阳。
                //   * 下拉 / 行编辑占位符 —— sizeHint 按**最宽项**算（含未选项），改用同字体
                //     裸标签代量**当前文案**，加箭头/内距后与实占宽比。
                // 自绘磁贴（CheckableCard）按 paintEvent 口径单列：它用 elidedText，被省略时
                // 墨迹只是变短不会缺角（"JPEG XL"→"JPEG…"，debug2 红框 1）。
                // （曾试过"参考件渲一遍比墨迹宽"：§4.2 的 11.5px 档走 setPointSizeF，参考件与
                // 控件按各自 DPI 解析像素高不同 —— 实测同一文案 156 / 111，换算不可靠，弃。）
                QString text;
                int formula_need = -1;
                int formula_avail = -1;
                bool rich = false;
                if (const auto *tile = dynamic_cast<const formkit::CheckableCard *>(x)) {
                    text = tile->text();
                    const QFontMetrics name_fm(
                        theme::font(theme::Typography::card_title_px, QFont::Bold));
                    const QFontMetrics hint_fm(theme::font(theme::Typography::badge_px));
                    const int hint_w = tile->hint_text().isEmpty()
                                           ? 0
                                           : hint_fm.horizontalAdvance(tile->hint_text());
                    formula_need = name_fm.horizontalAdvance(text);
                    formula_avail = x->width() - 2 * formkit::kTilePadX - formkit::kCheckPx -
                                    formkit::kTileGap -
                                    (hint_w > 0 ? hint_w + formkit::kTileGap : 0);
                } else if (const auto *line = qobject_cast<const QLineEdit *>(x)) {
                    if (line->text().isEmpty())
                        text = line->placeholderText();
                } else if (dynamic_cast<const formkit::ElidedLabel *>(x) != nullptr) {
                    text.clear(); // 省略即设计（§9.3 挤不下才出省略号）→ 不判
                } else if (const auto *combo = qobject_cast<const QComboBox *>(x)) {
                    text = combo->currentText();
                } else if (const auto *btn = qobject_cast<const QAbstractButton *>(x)) {
                    if (btn->icon().isNull()) // 带图标钮的墨迹混进图标 → 交 ③ 的实拍判
                        text = btn->text();
                } else if (const auto *label = qobject_cast<const QLabel *>(x)) {
                    text = label->text();
                    rich = label->textFormat() != Qt::PlainText;
                }
                const bool clip_candidate =
                    !text.isEmpty() && !rich && formula_need < 0 &&
                    x->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed &&
                    qobject_cast<const QComboBox *>(x) != nullptr; // 判据只认下拉（见下）
                if (clip_candidate) {
                    // 硬裁的可见签名 = **实绘墨迹顶到内容区右缘**（contentsRect 让出 24px
                    // 箭头列）。判据**只认下拉**：①它是 D5 病灶类（文本轨 160 硬裁半字）；
                    // ②它的文本区边界明确（箭头列起点）。按钮/chip/分段钮自绘且自带描边，
                    // 墨迹法分不清"描边"与"被裁的字"，判了全是假阳；其宽度多随内容，结构上
                    // 不裁。为什么不用度量/QImage 参考：点值字号在"控件度量"与"实绘"间解析
                    // 不一致（实测同一文案 156 / 102，差 1.5×），offscreen 的 CJK 又以 tofu
                    // 满宽字形渲染（比真机宽约 10%）—— 任何换算都会假阳。
                    QRect area = x->contentsRect();
                    if (qobject_cast<const QComboBox *>(x) != nullptr)
                        area.setRight(area.right() - 24);
                    const QImage shot = x->grab().toImage();
                    const double span = ink_span(shot, area);
                    const double right = ink_right(shot, area);
                    const double left = right - span + 1.0;
                    if (area.width() > 8 && left <= area.left() + 1.0 && right >= area.right())
                        hit(QStringLiteral("text-clip:%1 墨迹=%2..%3 内容区=%4..%5 \"%6\"")
                                .arg(tag(x))
                                .arg(left, 0, 'f', 1)
                                .arg(right, 0, 'f', 1)
                                .arg(area.left())
                                .arg(area.right())
                                .arg(text.left(20)));
                }
                if (formula_need > formula_avail)
                    hit(QStringLiteral("text-clip:%1 磁贴主字 need=%2 avail=%3 \"%4\"")
                            .arg(tag(x))
                            .arg(formula_need)
                            .arg(formula_avail)
                            .arg(text.left(20)));

                // ---- ③ blank-btn / ink-btn（不看可见性）----
                if (auto *btn = qobject_cast<QAbstractButton *>(x)) {
                    const bool tool_like = qobject_cast<const QToolButton *>(x) != nullptr ||
                                           qobject_cast<const QPushButton *>(x) != nullptr;
                    if (tool_like) {
                        const bool has_text = !btn->text().isEmpty();
                        const bool has_icon =
                            !btn->icon().isNull() ||
                            !btn->property(icons::kIconNameProperty).toString().isEmpty();
                        if (!has_text && !has_icon) {
                            hit(QStringLiteral("blank-btn:%1 无文案无图标").arg(tag(x)));
                        } else if (!has_text && has_icon) {
                            // 像素取证：图标挂了但画不出来 = 同样是白方 → 内区（各缩 3px）
                            // 必须有 ≥12 个非背景像素（外框描边不算墨）
                            const QImage img = btn->grab().toImage();
                            int ink = 0;
                            QRgb modal = 0;
                            if (!img.isNull()) {
                                QHash<QRgb, int> hist;
                                for (int yy = 0; yy < img.height(); ++yy)
                                    for (int xx = 0; xx < img.width(); ++xx)
                                        ++hist[img.pixel(xx, yy)];
                                int best = -1;
                                for (auto it = hist.constBegin(); it != hist.constEnd(); ++it)
                                    if (it.value() > best) {
                                        best = it.value();
                                        modal = it.key();
                                    }
                                const int mr = qRed(modal), mg = qGreen(modal), mb = qBlue(modal);
                                for (int yy = 3; yy + 3 < img.height(); ++yy) {
                                    for (int xx = 3; xx + 3 < img.width(); ++xx) {
                                        const QRgb p = img.pixel(xx, yy);
                                        if (std::abs(qRed(p) - mr) + std::abs(qGreen(p) - mg) +
                                                std::abs(qBlue(p) - mb) >
                                            24)
                                            ++ink;
                                    }
                                }
                            }
                            if (ink < 12)
                                hit(QStringLiteral("ink-btn:%1 内区墨迹=%2").arg(tag(x)).arg(ink));
                        }
                    }
                }

                // ---- 几何类判据：只看当前页可见件（隐藏件几何是布局跳过后的陈值）----
                if (!x->isVisibleTo(w))
                    continue;
                const bool is_leaf = qobject_cast<const QComboBox *>(x) != nullptr ||
                                     qobject_cast<const QAbstractButton *>(x) != nullptr ||
                                     qobject_cast<const QLineEdit *>(x) != nullptr ||
                                     qobject_cast<const QAbstractSpinBox *>(x) != nullptr ||
                                     qobject_cast<const QLabel *>(x) != nullptr;
                bool under_tabbar = false;
                for (QWidget *p = x->parentWidget(); p != nullptr; p = p->parentWidget())
                    if (qobject_cast<const QTabBar *>(p) != nullptr) {
                        under_tabbar = true;
                        break;
                    }
                if (is_leaf && !under_tabbar) // 页签带上的角标定位件（AddFormatAligner）不判
                    leaves << x;

                // ---- ⑤ ghost ----
                const QString cls = QString::fromLatin1(x->metaObject()->className());
                if (x->children().isEmpty() && x->objectName().isEmpty() &&
                    (cls == QLatin1String("QWidget") || cls == QLatin1String("QFrame")) &&
                    x->width() >= 40 && x->height() >= 18)
                    hit(QStringLiteral("ghost:%1 裸控件无名无文案").arg(tag(x)));
            }

            // ---- ② overlap ----
            // 比**裁到滚动视口内**的矩形：卡片滚出视口下方时与底栏几何上相交，但用户看不见
            // （滚动语义），不算重叠。
            const auto visible_rect = [this](const QWidget *x) {
                QRect r(x->mapTo(w, QPoint(0, 0)), x->size());
                for (const QWidget *p = x->parentWidget(); p != nullptr && p != w;
                     p = p->parentWidget()) {
                    const auto *sa = qobject_cast<const QAbstractScrollArea *>(p);
                    if (sa == nullptr || sa->viewport() == nullptr)
                        continue;
                    const QRect vr(sa->viewport()->mapTo(w, QPoint(0, 0)), sa->viewport()->size());
                    r = r.intersected(vr);
                }
                return r;
            };
            for (int i = 0; i < leaves.size(); ++i) {
                for (int j = i + 1; j < leaves.size(); ++j) {
                    QWidget *a = leaves[i];
                    QWidget *b = leaves[j];
                    if (a->isAncestorOf(b) || b->isAncestorOf(a))
                        continue;
                    const QRect ra = visible_rect(a);
                    const QRect rb = visible_rect(b);
                    if (ra.isEmpty() || rb.isEmpty())
                        continue;
                    const QRect inter = ra.intersected(rb);
                    if (inter.width() >= 8 && inter.height() >= 8)
                        hit(QStringLiteral("overlap:%1 x %2 相交 %3x%4 (%5)")
                                .arg(tag(a), tag(b))
                                .arg(inter.width())
                                .arg(inter.height())
                                .arg(chain(a)));
                }
            }
            // ---- ④ overflow ----
            for (QWidget *x : leaves) {
                QWidget *par = x->parentWidget();
                if (par == nullptr || par == w)
                    continue;
                const auto *sa = qobject_cast<const QAbstractScrollArea *>(par->parentWidget());
                if (sa != nullptr && sa->viewport() == par)
                    continue; // 滚动视口内可超视口（滚动语义）
                const QRect pr(par->mapTo(w, QPoint(0, 0)), par->size());
                const QRect cr(x->mapTo(w, QPoint(0, 0)), x->size());
                if (cr.left() < pr.left() - 3 || cr.top() < pr.top() - 3 ||
                    cr.right() > pr.right() + 3 || cr.bottom() > pr.bottom() + 3)
                    hit(QStringLiteral("overflow:%1 越出 %2")
                            .arg(tag(x), QStringLiteral("%1#%2").arg(
                                             QString::fromLatin1(par->metaObject()->className()),
                                             par->objectName())));
            }
            // ---- ⑥ card-blank / head-tall / body-blank ----
            // 卡体留白两处都会渲染成"大片空白"：卡整体高于子件和（head-tall 的几何面）、
            // 卡内某层容器高于其**可见**子件和（空态收起 5 行 kv 后 body 仍按 5 行铺高，
            // debug/debug1.png 红框 2 的「关键信息」卡头大片空白就是这条）。
            for (QFrame *card : w->findChildren<QFrame *>()) {
                if (card == nullptr || card->objectName() != QLatin1String("pp-card") ||
                    !card->isVisibleTo(w))
                    continue;
                // 富余高载体（布局 stretch>0，如 UX-4 的 GPS 卡）不判 —— 空白是设计。
                // QBoxLayout 无 Q_OBJECT ⇒ qobject_cast 用不上（static_assert），走 dynamic_cast。
                QWidget *par = card->parentWidget();
                const auto *box =
                    par != nullptr ? dynamic_cast<const QBoxLayout *>(par->layout()) : nullptr;
                if (box != nullptr) {
                    const int idx = box->indexOf(card);
                    if (idx >= 0 && box->stretch(idx) > 0)
                        continue;
                }
                const std::function<void(QWidget *, int)> blank_walk = [&](QWidget *host,
                                                                           int depth) {
                    // 只查**布局托管**的容器：画布（pp-preview-stage 等）子件绝对定位、
                    // 高度由画布自身语义决定，不属"留白"
                    if (host == nullptr || depth > 2 || host->layout() == nullptr)
                        return;
                    int sum = 0;
                    QStringList kid_tags;
                    QList<QWidget *> kids;
                    for (QObject *ch : host->children()) {
                        auto *cw = qobject_cast<QWidget *>(ch);
                        if (cw == nullptr || !cw->isVisibleTo(w))
                            continue;
                        sum += cw->height();
                        kids << cw;
                        if (kid_tags.size() < 4)
                            kid_tags << tag(cw);
                    }
                    // 空白是设计的两种情形：布局给了 stretch / 放了 spacer
                    bool absorbing = false;
                    const QLayout *lay = host->layout();
                    if (const auto *b = dynamic_cast<const QBoxLayout *>(lay))
                        for (int i = 0; i < b->count(); ++i)
                            absorbing = absorbing || b->stretch(i) > 0;
                    if (lay != nullptr)
                        for (int i = 0; i < lay->count(); ++i)
                            absorbing = absorbing || lay->itemAt(i)->spacerItem() != nullptr;
                    if (!absorbing && !kids.isEmpty() && host->height() - sum > 24)
                        hit(QStringLiteral("card-blank:%1 高=%2 可见子件和=%3 [%4]")
                                .arg(tag(host))
                                .arg(host->height())
                                .arg(sum)
                                .arg(kid_tags.join(QLatin1Char(' '))));
                    for (QWidget *k : kids)
                        blank_walk(k, depth + 1);
                };
                blank_walk(card, 0);
                int head_h = -1;
                for (QObject *ch : card->children()) {
                    const auto *cw = qobject_cast<QWidget *>(ch);
                    if (cw == nullptr || !cw->isVisibleTo(w))
                        continue;
                    head_h = cw->height();
                    break;
                }
                if (head_h > 44)
                    hit(QStringLiteral("head-tall:%1 卡头高=%2").arg(tag(card)).arg(head_h));
            }
        }
    }
    if (stack != nullptr && page_before >= 0)
        stack->setCurrentIndex(page_before);
    set_theme_mode(mode_before);
    pump(60);
    std::printf("UI-SMOKE layout-audit: checked=%d bad=%d [%s]\n", checked, bad,
                qUtf8Printable(detail.join(QLatin1Char(' '))));
    std::fflush(stdout);
    if (bad > 0)
        smoke_fail(MainWindow::tr("布局审计（识图）命中 %1 处：%2")
                       .arg(bad)
                       .arg(detail.join(QLatin1Char(' '))));
}

// ---------------------------------------------------------------------------
// M4-T12 元数据页自检（§5.2 生效值显示 / D4 地图单钉 / §9.2 .mod / 多选小表 / 无选中提示）
//   确定性输入 = 仓库金样 `tests/golden/meta/exif_full.jpg`（与 tests/unit/test_effective.cpp
//   同一 fixture：DateTimeOriginal 2024:03:01 10:00:00、GPS 31.2304/121.4737；金值口径见
//   test_effective 的 delta/2h 与 tz/+08-to+09 用例）。
//   核心断言 = **显示值与写入值同源**（"两张皮"检测）：页面显示的生效时间/坐标必须等于
//   `pp::build_plan()`（写路径）在同一份规则下的落盘值。
//   页实例独立（不动主窗口的页面/列表/选中）；地图与本窗口同一条离线口径（探针不发网络请求）。
// ---------------------------------------------------------------------------
MetaSelectionItem MainWindow::Impl::meta_probe_item(const QString &path) {
    MetaSelectionItem item;
    item.path = path;
    // probe 摘要 = 与列表模型同一条 oiio probe 通道（本探针只补"已收集"的那份快照）
    const pp::ProbeOutcome probe = pp::probe_file(std::filesystem::path(path.toStdString()));
    item.info = probe.info;
    item.probe_ok = probe.error.empty();
    item.file_size = QFileInfo(path).size();
    return item;
}

// ---------------------------------------------------------------------------
// M5-T8（W-G3 · §7.2 输出页 / §5 R1–R4 / 评审 2-05…2-42 / 裁定 A6/A7/A8）—— C7 几何值表
//   磁贴 2×4 + 对勾位零形变（选中不改宽、不改 sizeHint）｜六行一义 pitch 28（位深/技术/无损/
//   编码努力/视觉距离/高级参数，行内只有一个标签）｜标签轨 80 且组内外同轴（2-32）｜
//   spin 只取 88·160 两档（2-33）｜范围入 10px 注释（2-18）｜魔法值「自动」（2-37）｜
//   高级参数折叠行默认折叠 + 子分组 + 搜索行（2-19/2-31/2-39/2-40/2-41）｜
//   互斥置灰（仅元数据：磁贴区 + 格式参数卡禁用，模式分段自身仍可用）｜
//   模板示例行单行且无 "<根>"（2-14/2-15）｜卡头计数 = 中性胶囊（2-07）｜按钮 26（A7）｜
//   页签 = 格式名 · 位深（2-21）｜预设 placeholder + 禁用原因（2-24/2-25）｜
//   根目录 placeholder（2-10）/色彩目标 tooltip（2-16）/模板 tooltip（2-13）。
// ---------------------------------------------------------------------------
void MainWindow::Impl::smoke_probe_output_geometry() {
    PageOutput page;
    page.set_tokens(tokens);
    page.resize(theme::Metrics::right_width, theme::Metrics::calibrated_h);
    page.show();
    page.select_format(QStringLiteral("jxl"));
    pump(150);

    int checked = 0;
    QStringList bad;
    const auto expect = [&checked, &bad](const char *name, bool ok, const QString &detail) {
        ++checked;
        if (!ok)
            bad << QStringLiteral("%1(%2)").arg(QString::fromLatin1(name), detail);
    };
    const auto row_of = [&page](const QString &name) { return page.findChild<QWidget *>(name); };
    const auto row_x = [&page](const QString &name) {
        QWidget *row = page.findChild<QWidget *>(name);
        return row != nullptr ? row->mapTo(&page, QPoint(0, 0)).x() : -1;
    };
    const auto row_label_x = [&page](const QString &name) {
        QWidget *row = page.findChild<QWidget *>(name);
        if (row == nullptr)
            return -1;
        const QString hook = QString::fromLatin1(theme::kFkLabel);
        for (QLabel *label : row->findChildren<QLabel *>()) {
            if (label->property(theme::kFkProperty).toString() == hook)
                return label->mapTo(&page, QPoint(0, 0)).x();
        }
        return -1;
    };
    const auto row_note = [&page](const QString &name) {
        QWidget *row = page.findChild<QWidget *>(name);
        if (row == nullptr)
            return QString();
        const QString hook = QString::fromLatin1(theme::kFkNote);
        for (QLabel *label : row->findChildren<QLabel *>()) {
            if (label->property(theme::kFkProperty).toString() == hook)
                return label->text();
        }
        return QString();
    };
    const auto full_text = [&page](const QString &name) {
        const auto *widget = page.findChild<QLabel *>(name);
        if (widget == nullptr)
            return QString();
        const QVariant value = widget->property("ppFullText");
        return value.isValid() ? value.toString() : widget->text();
    };

    // ---- (a) 磁贴 2×4 + 对勾位零形变 + 未选中 ≠ 禁用（修 2-05）----
    {
        // formkit 自绘控件无 Q_OBJECT（不走 moc）→ 按 objectName 取 + 静态下转（单元确定性）
        QList<formkit::CheckableCard *> tiles;
        for (const char *id : {"jpeg", "webp", "jxl", "heif", "avif", "png", "tiff", "bmp"}) {
            if (QWidget *widget = page.findChild<QWidget *>(QStringLiteral("pp-format-") +
                                                            QString::fromLatin1(id))) {
                tiles.append(static_cast<formkit::CheckableCard *>(widget));
            }
        }
        QList<int> xs;
        QList<int> ys;
        for (formkit::CheckableCard *tile : tiles) {
            const QPoint pos = tile->mapTo(&page, QPoint(0, 0));
            xs.append(pos.x());
            ys.append(pos.y());
        }
        std::sort(xs.begin(), xs.end());
        xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
        std::sort(ys.begin(), ys.end());
        ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
        int deform_ok = 0;
        int enabled_tiles = 0;
        QStringList deform_detail;
        for (formkit::CheckableCard *tile : tiles) {
            const QSize size_before = tile->size();
            const QSize hint_before = tile->sizeHint();
            const QPoint pos_before = tile->mapTo(&page, QPoint(0, 0));
            tile->setChecked(!tile->isChecked());
            pump(40);
            const QPoint pos_after = tile->mapTo(&page, QPoint(0, 0));
            if (tile->size() == size_before && tile->sizeHint() == hint_before &&
                pos_after == pos_before) {
                ++deform_ok;
            } else {
                deform_detail << QStringLiteral("%1 %2→%3 @%4,%5→%6,%7 hint %8→%9")
                                     .arg(tile->objectName())
                                     .arg(size_before.width())
                                     .arg(tile->size().width())
                                     .arg(pos_before.x())
                                     .arg(pos_before.y())
                                     .arg(pos_after.x())
                                     .arg(pos_after.y())
                                     .arg(hint_before.width())
                                     .arg(tile->sizeHint().width());
            }
            tile->setChecked(!tile->isChecked()); // 还原（jxl 单选）
            pump(40);
            if (tile->isEnabled())
                ++enabled_tiles;
        }
        expect("tiles-2x4", tiles.size() == 8 && ys.size() == 2 && xs.size() == 4,
               QStringLiteral("tiles=%1 rows=%2 cols=%3")
                   .arg(tiles.size())
                   .arg(ys.size())
                   .arg(xs.size()));
        expect("tile-zero-deform", deform_ok == tiles.size(),
               QStringLiteral("ok=%1/%2 %3")
                   .arg(deform_ok)
                   .arg(tiles.size())
                   .arg(deform_detail.join(QLatin1Char(' '))));
        expect("tile-normal", enabled_tiles == tiles.size(), // 未选中 = 正常对比度（非禁用态）
               QStringLiteral("enabled=%1/%2").arg(enabled_tiles).arg(tiles.size()));
    }
    // ---- (a2) A27（T11 缺陷根治受益面）：转换模式分段 —— 段自持高（= kSegH − 2）且可见可选 ----
    {
        const auto *seg =
            page.findChild<formkit::SegmentedControl *>(QStringLiteral("pp-output-seg"));
        QWidget *first = seg != nullptr ? seg->segment(0) : nullptr;
        QWidget *second = seg != nullptr ? seg->segment(1) : nullptr;
        const auto *convert = page.findChild<QWidget *>(QStringLiteral("pp-mode-convert"));
        const auto *metadata = page.findChild<QWidget *>(QStringLiteral("pp-mode-metadata"));
        // 段对象名 = 调用点为探针留的钩子（pp-mode-convert / pp-mode-metadata）
        const bool named = first != nullptr && second != nullptr && seg->segment(0) == convert &&
                           seg->segment(1) == metadata;
        expect("mode-seg-visible",
               seg != nullptr && seg->count() == 2 && seg->height() == formkit::kSegH &&
                   first != nullptr && first->height() == formkit::seg_inner_h() &&
                   first->width() == formkit::kSegW && second != nullptr &&
                   second->height() == formkit::seg_inner_h() &&
                   second->width() == formkit::kSegW && first->isVisible() && second->isVisible() &&
                   named && seg->current_index() >= 0 && seg->current_index() < seg->count(),
               QStringLiteral("seg=%1x%2 first=%3x%4(期望 %5x%6) visible=%7/%8 named=%9")
                   .arg(seg != nullptr ? seg->width() : -1)
                   .arg(seg != nullptr ? seg->height() : -1)
                   .arg(first != nullptr ? first->width() : -1)
                   .arg(first != nullptr ? first->height() : -1)
                   .arg(formkit::kSegW)
                   .arg(formkit::seg_inner_h())
                   .arg(first != nullptr && first->isVisible() ? 1 : 0)
                   .arg(second != nullptr && second->isVisible() ? 1 : 0)
                   .arg(named ? 1 : 0));
    }
    page.select_format(QStringLiteral("jxl"));
    pump(60);

    // ---- (b) 六行一义：pitch 28 + 行内单标签 + 顺序（§7.2 逐行）+ 分文件夹行语法（修 2-11）----
    const QStringList six_rows = {
        QStringLiteral("pp-bitdepth-row"),       QStringLiteral("pp-tech-row"),
        QStringLiteral("pp-lossless-row"),       QStringLiteral("pp-param-row-effort"),
        QStringLiteral("pp-param-row-distance"), QStringLiteral("pp-advanced-row")};
    {
        int seen = 0;
        int pitch_bad = 0;
        int multi_label = 0;
        bool ordered = true;
        int prev_y = -1;
        const QString label_hook = QString::fromLatin1(theme::kFkLabel);
        for (const QString &name : six_rows) {
            QWidget *row = row_of(name);
            if (row == nullptr || !row->isVisible())
                continue;
            ++seen;
            if (row->height() != formkit::kRowPitch)
                ++pitch_bad;
            int labels = 0;
            for (QLabel *label : row->findChildren<QLabel *>())
                if (label->property(theme::kFkProperty).toString() == label_hook)
                    ++labels;
            if (labels > 1)
                ++multi_label; // R3：一行只有一个标签
            const int y = row->mapTo(&page, QPoint(0, 0)).y();
            if (prev_y >= 0 && y <= prev_y)
                ordered = false;
            prev_y = y;
        }
        expect("six-rows", seen == 6 && pitch_bad == 0 && ordered && multi_label == 0,
               QStringLiteral("seen=%1 pitch-bad=%2 ordered=%3 multi-label=%4")
                   .arg(seen)
                   .arg(pitch_bad)
                   .arg(ordered ? 1 : 0)
                   .arg(multi_label));
        // 全部行（两卡 + 页签内）pitch 28
        QStringList all_rows = six_rows;
        all_rows << QStringLiteral("pp-mode-row") << QStringLiteral("pp-root-row")
                 << QStringLiteral("pp-conflict-row") << QStringLiteral("pp-color-row")
                 << QStringLiteral("pp-split-row") << QStringLiteral("pp-template-row")
                 << QStringLiteral("pp-example-row") << QStringLiteral("pp-preset-row");
        int rows_seen = 0;
        int rows_bad = 0;
        for (const QString &name : all_rows) {
            QWidget *row = row_of(name);
            if (row == nullptr || !row->isVisible())
                continue;
            ++rows_seen;
            if (row->height() != formkit::kRowPitch)
                ++rows_bad;
        }
        expect("row-pitch", rows_seen >= 14 && rows_bad == 0,
               QStringLiteral("rows=%1 bad=%2").arg(rows_seen).arg(rows_bad));
        // 标签轨 80（R2）
        int labels_seen = 0;
        int labels_bad = 0;
        for (QLabel *label : page.findChildren<QLabel *>()) {
            if (label->property(theme::kFkProperty).toString() != label_hook)
                continue;
            ++labels_seen;
            if (label->width() != formkit::kLabelW)
                ++labels_bad;
        }
        expect("label-track", labels_seen >= 12 && labels_bad == 0,
               QStringLiteral("labels=%1 bad=%2").arg(labels_seen).arg(labels_bad));
        // 分文件夹行：标签归左列、控件在字段轨（修 2-11）
        const int split_label_x = row_label_x(QStringLiteral("pp-split-row"));
        const auto *split_switch = page.findChild<QWidget *>(QStringLiteral("pp-split-switch"));
        const int split_x =
            split_switch != nullptr ? split_switch->mapTo(&page, QPoint(0, 0)).x() : -1;
        expect("split-row",
               split_label_x == row_x(QStringLiteral("pp-split-row")) &&
                   split_x >= split_label_x + formkit::kLabelW + formkit::kColGap,
               QStringLiteral("label-x=%1 switch-x=%2").arg(split_label_x).arg(split_x));
    }

    // ---- (c) 数值控件 88·160 两档（2-33）+ 范围 10px 注释（2-18）----
    {
        const auto *effort = page.findChild<QSpinBox *>(QStringLiteral("pp-param-effort"));
        const auto *distance =
            page.findChild<QDoubleSpinBox *>(QStringLiteral("pp-param-distance"));
        const QString effort_note = row_note(QStringLiteral("pp-param-row-effort"));
        const QString distance_note = row_note(QStringLiteral("pp-param-row-distance"));
        expect("spin-core",
               effort != nullptr && effort->width() == formkit::kFieldStd && distance != nullptr &&
                   distance->width() == formkit::kFieldStd,
               QStringLiteral("effort=%1 distance=%2 want=%3")
                   .arg(effort != nullptr ? effort->width() : -1)
                   .arg(distance != nullptr ? distance->width() : -1)
                   .arg(formkit::kFieldStd));
        expect("range-note",
               effort_note == QStringLiteral("1–10") && distance_note == QStringLiteral("0.0–25.0"),
               QStringLiteral("effort=\"%1\" distance=\"%2\"").arg(effort_note, distance_note));
    }

    int groups_jxl = 0; // 打印用：jxl 页签的高级区子分组数（jpeg 页签只有 1 组）
    // ---- (d) 高级参数：默认折叠 → 展开后 搜索行 + 子分组 + 88 档 spin + 同轴 + 魔法值 ----
    //      （2-19/2-31/2-32/2-37/2-39/2-40/2-41/2-42）
    {
        auto *toggle = page.findChild<QToolButton *>(QStringLiteral("pp-advanced-toggle"));
        auto *host = row_of(QStringLiteral("pp-advanced-area"));
        const bool folded =
            toggle != nullptr && host != nullptr && !host->isVisible() && !toggle->isChecked();
        if (toggle != nullptr)
            toggle->setChecked(true);
        pump(80);
        auto *search = page.findChild<QLineEdit *>(QStringLiteral("pp-advanced-search"));
        int groups = 0;
        if (host != nullptr) {
            for (QWidget *widget : host->findChildren<QWidget *>()) {
                if (widget->objectName().startsWith(QStringLiteral("pp-advanced-group-")))
                    ++groups;
            }
        }
        expect("advanced-fold",
               folded && host != nullptr && host->isVisible() && toggle != nullptr &&
                   toggle->text().contains(QStringLiteral("项")),
               QStringLiteral("folded=%1 expanded=%2 summary=\"%3\"")
                   .arg(folded ? 1 : 0)
                   .arg(host != nullptr && host->isVisible() ? 1 : 0)
                   .arg(toggle != nullptr ? toggle->text() : QString()));
        expect("advanced-search",
               search != nullptr && search->placeholderText().endsWith(QStringLiteral("…")) &&
                   !search->toolTip().isEmpty(),
               QStringLiteral("ph=\"%1\"")
                   .arg(search != nullptr ? search->placeholderText() : QString()));
        groups_jxl = groups;
        expect("advanced-groups", groups >= 3, QStringLiteral("groups=%1").arg(groups));
        // 组内数值 = 标准 160（UX-8 §8-3：折叠区数值行宽与核心区一致，「折叠 = 88」第二档废除；
        // M7-R2-A 换锚 kFieldShort→kFieldStd，断言语义不变 = "折叠区数值控件宽 == 约定字段宽档"）
        // ；组内外标签列同轴（2-32）
        int adv_spins = 0;
        int adv_bad = 0;
        if (host != nullptr) {
            for (QSpinBox *spin : host->findChildren<QSpinBox *>()) {
                ++adv_spins;
                if (spin->width() != formkit::kFieldStd)
                    ++adv_bad;
            }
            for (QDoubleSpinBox *spin : host->findChildren<QDoubleSpinBox *>()) {
                ++adv_spins;
                if (spin->width() != formkit::kFieldStd)
                    ++adv_bad;
            }
        }
        expect("spin-group", adv_spins >= 2 && adv_bad == 0,
               QStringLiteral("spins=%1 bad=%2 want=%3")
                   .arg(adv_spins)
                   .arg(adv_bad)
                   .arg(formkit::kFieldStd));
        expect("label-axis",
               row_label_x(QStringLiteral("pp-param-row-effort")) >= 0 &&
                   row_label_x(QStringLiteral("pp-param-row-effort")) ==
                       row_label_x(QStringLiteral("pp-param-row-epf")),
               QStringLiteral("core-x=%1 adv-x=%2")
                   .arg(row_label_x(QStringLiteral("pp-param-row-effort")))
                   .arg(row_label_x(QStringLiteral("pp-param-row-epf"))));
        // 魔法值「自动」（2-37）：epf（范围下端 = -1 = 编码器自选）
        auto *epf = page.findChild<QSpinBox *>(QStringLiteral("pp-param-epf"));
        const QString epf_default = epf != nullptr ? epf->text() : QString();
        if (epf != nullptr)
            epf->setValue(-1);
        pump(60);
        expect("magic-auto",
               epf != nullptr && epf->text() == QStringLiteral("自动") &&
                   epf_default != QStringLiteral("自动"),
               QStringLiteral("default=\"%1\" magic=\"%2\"")
                   .arg(epf_default)
                   .arg(epf != nullptr ? epf->text() : QString()));
        // 改值 → 卡头中性 pill 计数（A8：无 ● 字形；计数由 pill 承担）
        const auto *changed_label =
            page.findChild<QLabel *>(QStringLiteral("pp-format-changed-pill"));
        const auto *changed_pill = changed_label != nullptr
                                       ? static_cast<const formkit::PillBadge *>(changed_label)
                                       : nullptr;
        expect("changed-pill",
               changed_pill != nullptr && changed_pill->isVisible() &&
                   changed_pill->text() == QStringLiteral("已改 1"),
               QStringLiteral("text=\"%1\" visible=%2")
                   .arg(changed_pill != nullptr ? changed_pill->text() : QString())
                   .arg(changed_pill != nullptr && changed_pill->isVisible() ? 1 : 0));
        if (toggle != nullptr)
            toggle->setChecked(false);
        pump(60);
    }

    // ---- (e) 模板示例行：单行、统一 $ 语法（无 "<根>"）（2-13/2-14/2-15）----
    {
        const auto *edit = page.findChild<QLineEdit *>(QStringLiteral("pp-template-edit"));
        const auto *example = page.findChild<QLabel *>(QStringLiteral("pp-template-example"));
        const QString text = full_text(QStringLiteral("pp-template-example"));
        expect("template-example",
               edit != nullptr && example != nullptr && example->isVisible() &&
                   !text.contains(QLatin1Char('<')) && text.contains(QStringLiteral("/jxl/")) &&
                   example->height() <= formkit::kRowPitch - 8,
               QStringLiteral("text=\"%1\" h=%2")
                   .arg(text)
                   .arg(example != nullptr ? example->height() : -1));
        expect("template-tooltip",
               edit != nullptr && edit->toolTip().contains(QStringLiteral("$format")) &&
                   edit->toolTip().contains(QStringLiteral("$dir")),
               QStringLiteral("tip=%1").arg(edit != nullptr && !edit->toolTip().isEmpty() ? 1 : 0));
    }

    // ---- (f) 卡头中性计数胶囊（2-07）+ 页签 = 格式名（UX-9：去"· N 位"）+ 预设
    // placeholder（2-24/2-25）
    //      + 根目录 placeholder（2-10）+ 色彩目标 tooltip（2-16）+ 按钮 26（A7）----
    {
        const auto *format_label = page.findChild<QLabel *>(QStringLiteral("pp-format-pill"));
        const auto *format_pill = format_label != nullptr
                                      ? static_cast<const formkit::PillBadge *>(format_label)
                                      : nullptr;
        expect("card-pill",
               format_pill != nullptr && format_pill->tone() == formkit::PillBadge::Tone::Neutral &&
                   format_pill->text().startsWith(QStringLiteral("已选")),
               QStringLiteral("format=\"%1\"")
                   .arg(format_pill != nullptr ? format_pill->text() : QString()));
        const auto *tabs = page.findChild<QTabWidget *>(QStringLiteral("pp-format-tabs"));
        const QString tab0 = tabs != nullptr && tabs->count() > 0 ? tabs->tabText(0) : QString();
        // UX-9 机械换锚（语义不变；登记 build/m7-wp6/handoff.md 裁定登记）：页签文案**不再拼
        // 位深**（§16-1 口径 = 只在选项卡标题去掉"多少位"，位深控制功能原样保留 —— 见下面
        // bitdepth-lock 与 six_rows 两条**逐字保留**的断言）。新断言 = 不含「位」且非空。
        expect("tab-label", !tab0.isEmpty() && !tab0.contains(QStringLiteral("位")),
               QStringLiteral("tab0=\"%1\"").arg(tab0));
        // UX-9 加性断言（只增）：页签等宽（QTabBar::setExpanding 平摊宽度，见 page_output）
        {
            int bad = 0;
            if (tabs != nullptr && tabs->count() > 1 && tabs->tabBar() != nullptr) {
                const int w0 = tabs->tabBar()->tabRect(0).width();
                for (int i = 1; i < tabs->count(); ++i) {
                    if (std::abs(tabs->tabBar()->tabRect(i).width() - w0) > 1)
                        ++bad;
                }
            }
            expect("tab-equal", bad == 0, QStringLiteral("unequal=%1").arg(bad));
        }
        const auto *root_edit = page.findChild<QLineEdit *>(QStringLiteral("pp-out-root-edit"));
        const auto *color = page.findChild<QComboBox *>(QStringLiteral("pp-color-combo"));
        expect(
            "placeholders",
            root_edit != nullptr && !root_edit->placeholderText().isEmpty() && color != nullptr &&
                color->toolTip().contains(QStringLiteral("ICC")),
            QStringLiteral("root-ph=\"%1\" color-tip=%2")
                .arg(root_edit != nullptr ? root_edit->placeholderText() : QString())
                .arg(color != nullptr && color->toolTip().contains(QStringLiteral("ICC")) ? 1 : 0));
        const auto *preset = page.findChild<QComboBox *>(QStringLiteral("pp-preset-combo"));
        const auto *save = page.findChild<QPushButton *>(QStringLiteral("pp-preset-save"));
        const auto *remove = page.findChild<QPushButton *>(QStringLiteral("pp-preset-delete"));
        expect("preset-placeholder",
               preset != nullptr && preset->placeholderText() == QStringLiteral("未选择预设") &&
                   save != nullptr && !save->isEnabled() && !save->toolTip().isEmpty() &&
                   remove != nullptr && !remove->isEnabled(),
               QStringLiteral("ph=\"%1\" save=%2 del=%3")
                   .arg(preset != nullptr ? preset->placeholderText() : QString())
                   .arg(save != nullptr && save->isEnabled() ? 1 : 0)
                   .arg(remove != nullptr && remove->isEnabled() ? 1 : 0));
        int btn_bad = 0;
        for (const char *name : {"pp-browse-button", "pp-template-vars", "pp-preset-saveas",
                                 "pp-preset-save", "pp-preset-delete"}) {
            const auto *button = page.findChild<QPushButton *>(QString::fromLatin1(name));
            if (button == nullptr || button->height() != theme::Metrics::btn_h)
                ++btn_bad;
        }
        const auto *add_format = page.findChild<QToolButton *>(QStringLiteral("pp-add-format"));
        if (add_format == nullptr || add_format->height() != theme::Metrics::btn_h)
            ++btn_bad;
        expect("btn-26", btn_bad == 0,
               QStringLiteral("bad=%1 want=%2").arg(btn_bad).arg(theme::Metrics::btn_h));
    }

    // ---- (g) 预估 kv（④）：输入面 → 预计输出 = 勾选数 × 格式数 ----
    {
        page.set_input_count(12);
        pump(60);
        const QString one = full_text(QStringLiteral("pp-est-output"));
        QWidget *png_widget = page.findChild<QWidget *>(QStringLiteral("pp-format-png"));
        auto *png =
            png_widget != nullptr ? static_cast<formkit::CheckableCard *>(png_widget) : nullptr;
        if (png != nullptr)
            png->setChecked(true);
        pump(80);
        const QString two = full_text(QStringLiteral("pp-est-output"));
        const QString disk = full_text(QStringLiteral("pp-est-disk"));
        expect("estimate",
               one == QStringLiteral("12 个") && two == QStringLiteral("24 个") &&
                   !full_text(QStringLiteral("pp-est-size")).isEmpty() && !disk.isEmpty(),
               QStringLiteral("one=\"%1\" two=\"%2\" disk=\"%3\"").arg(one, two, disk));
        if (png != nullptr)
            png->setChecked(false);
        pump(80);
    }
    page.select_format(QStringLiteral("jxl"));
    pump(60);

    // ---- (h) 互斥置灰：仅元数据 → 磁贴区 + 格式参数卡禁用，模式分段仍可用（D7 语义零改动）----
    {
        auto *tiles_host = page.findChild<QWidget *>(QStringLiteral("pp-tiles"));
        QWidget *param_card = nullptr;
        for (QFrame *card : page.findChildren<QFrame *>(QStringLiteral("pp-card"))) {
            if (card->findChild<QTabWidget *>(QStringLiteral("pp-format-tabs")) != nullptr)
                param_card = card;
        }
        auto *mode_seg = page.findChild<QWidget *>(QStringLiteral("pp-output-seg"));
        auto *note = page.findChild<QLabel *>(QStringLiteral("pp-format-note"));
        page.set_metadata_only(true);
        pump(80);
        const bool on = page.metadata_only();
        const bool disabled = tiles_host != nullptr && !tiles_host->isEnabled() &&
                              param_card != nullptr && !param_card->isEnabled() &&
                              mode_seg != nullptr && mode_seg->isEnabled() && note != nullptr &&
                              note->isVisible();
        page.set_metadata_only(false);
        pump(80);
        const bool restored = !page.metadata_only() && tiles_host != nullptr &&
                              tiles_host->isEnabled() && param_card != nullptr &&
                              param_card->isEnabled() && note != nullptr && !note->isVisible();
        expect("metadata-only", on && disabled && restored,
               QStringLiteral("on=%1 off=%2 disabled=%3 restored=%4")
                   .arg(on ? 1 : 0)
                   .arg(page.metadata_only() ? 1 : 0)
                   .arg(disabled ? 1 : 0)
                   .arg(restored ? 1 : 0));
    }

    // ---- (j) 预设卡主次（修 2-25/2-26）：给一份预设并选中 → 保存/删除启用，且保存为**实心
    //      accent 主按钮**（R4 主按钮实心 acc + acc-on 字）----
    {
        auto *save = page.findChild<QPushButton *>(QStringLiteral("pp-preset-save"));
        auto *remove = page.findChild<QPushButton *>(QStringLiteral("pp-preset-delete"));
        page.set_preset_list(
            {{QStringLiteral("/tmp/m5t8-smoke.json"), QStringLiteral("m5t8 冒烟预设")}});
        page.set_current_preset(QStringLiteral("/tmp/m5t8-smoke.json"));
        pump(80);
        const auto color_distance = [](const QImage &image, const QColor &want) {
            if (image.isNull())
                return 1e9;
            int best = 1 << 30;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QColor px = image.pixelColor(x, y);
                    const int dr = px.red() - want.red();
                    const int dg = px.green() - want.green();
                    const int db = px.blue() - want.blue();
                    best = std::min(best, dr * dr + dg * dg + db * db);
                }
            }
            return best < (1 << 30) ? std::sqrt(double(best)) : 1e9;
        };
        const QImage shot = save != nullptr ? save->grab().toImage() : QImage();
        const double accent_dist = color_distance(shot, tokens.accent);
        const double ctl_dist = color_distance(shot, tokens.control_solid);
        expect("preset-primary",
               save != nullptr && save->isEnabled() && remove != nullptr && remove->isEnabled() &&
                   accent_dist < 24.0 && accent_dist < ctl_dist,
               QStringLiteral("save=%1 del=%2 accent=%.1f ctl=%.1f")
                   .arg(save != nullptr && save->isEnabled() ? 1 : 0)
                   .arg(remove != nullptr && remove->isEnabled() ? 1 : 0)
                   .arg(accent_dist)
                   .arg(ctl_dist));
        page.set_preset_list({});
        pump(40);
    }

    // ---- (i) 位深能力约束（JPEG 锁 8 位）：位深下拉 + 行内注释 ----
    {
        page.select_format(QStringLiteral("jpeg"));
        pump(120);
        const auto *bitdepth = page.findChild<QComboBox *>(QStringLiteral("pp-bitdepth-combo"));
        const auto *note = page.findChild<QLabel *>(QStringLiteral("pp-bitdepth-note-jpeg"));
        const auto *tabs = page.findChild<QTabWidget *>(QStringLiteral("pp-format-tabs"));
        const QString tab0 = tabs != nullptr && tabs->count() > 0 ? tabs->tabText(0) : QString();
        // UX-9 机械换锚（登记 build/m7-wp6/handoff.md T5）：本探针钉的是**位深控制功能**不变
        // （下拉锁 1 项 / 文案 "8 位" / 行内注释"仅支持 8 位"三项**逐字保留**）；原第四个合取项
        // `tab0.endsWith("8 位")` 钉的是**页签文案**里的位深 —— §16-1 口径已明令把它从页签去掉
        // （tab-label 探针改断言不含「位」），两者不可同存，故只摘掉该合取项。
        expect("bitdepth-lock",
               bitdepth != nullptr && bitdepth->count() == 1 &&
                   bitdepth->currentText() == QStringLiteral("8 位") && note != nullptr &&
                   note->text().contains(QStringLiteral("仅支持 8 位")) &&
                   !tab0.contains(QStringLiteral("位")),
               QStringLiteral("count=%1 text=\"%2\" note=\"%3\" tab=\"%4\"")
                   .arg(bitdepth != nullptr ? bitdepth->count() : -1)
                   .arg(bitdepth != nullptr ? bitdepth->currentText() : QString())
                   .arg(note != nullptr ? note->text() : QString())
                   .arg(tab0));
    }

    const auto count_named = [&page](const QString &prefix, const QString &suffix) {
        int n = 0;
        for (QWidget *widget : page.findChildren<QWidget *>()) {
            const QString name = widget->objectName();
            if (name.startsWith(prefix) && (suffix.isEmpty() || name.endsWith(suffix)))
                ++n;
        }
        return n;
    };
    const int rows_now = count_named(QStringLiteral("pp-"), QStringLiteral("-row"));
    int tiles_now = 0;
    for (const char *id : {"jpeg", "webp", "jxl", "heif", "avif", "png", "tiff", "bmp"}) {
        if (page.findChild<QWidget *>(QStringLiteral("pp-format-") + QString::fromLatin1(id)) !=
            nullptr) {
            ++tiles_now;
        }
    }
    const int groups_now = groups_jxl;
    std::printf("UI-SMOKE output-geom: checked=%d bad=%d tiles=%d rows=%d six-rows=%d "
                "spin-short=%d spin-std=%d groups=%d pills=%d %s\n",
                checked, int(bad.size()), tiles_now, rows_now, 6, formkit::kFieldShort,
                formkit::kFieldStd, groups_now,
                count_named(QStringLiteral("pp-"), QStringLiteral("-pill")),
                qUtf8Printable(bad.join(QLatin1Char(' '))));
    std::fflush(stdout);
    if (!bad.isEmpty()) {
        smoke_fail(MainWindow::tr("输出页几何（§7.2/R1–R4/修 2-05…2-42）不符：%1")
                       .arg(bad.join(QLatin1Char(' '))));
    }
    page.hide();
    pump(80);
}

// R-255（M7-R2.1-F3）：smoke_probe_meta 分节共享前置（fixture / 页面 / 取值 lambdas）。
// 原 1675 行单函数/深 10（deep-review R-255/E1-13）→ 12 个分节方法**逐字搬移**
// （断言/输出行与顺序不变；节体零文本改动，共享件以局部别名承接）。
struct MetaProbeCtx {
    PageMeta *page = nullptr;
    QString repo;
    QString fixture;
    std::function<QString(const QString &)> label;
    std::function<QString(const QString &)> full;
    std::function<QString(const QString &)> edit_text;
    std::function<QString(const QString &)> placeholder;
    std::function<bool()> wait_probe;
    std::function<QString(QString)> shown_form;
    std::function<QString(const pp::BatchRules &)> write_side_time;
    double pick_lat = 0.0;
    double pick_lon = 0.0;
    std::function<QString()> gps_badge;
    std::function<bool()> gps_badge_visible;
};

// R-255：smoke_probe_meta 分节 sec_a（自原函数逐字搬移；断言/输出行不变）
void MainWindow::Impl::probe_meta_sec_a(const MetaProbeCtx &c, const QString &shots_dir) {
    // R-255：共享前置局部别名（ctx 承接；节体逐字不变）
    PageMeta &page = *c.page;
    const QString &repo = c.repo;
    const QString &fixture = c.fixture;
    const auto &label = c.label;
    const auto &full = c.full;
    const auto &edit_text = c.edit_text;
    const auto &placeholder = c.placeholder;
    const auto &wait_probe = c.wait_probe;
    const auto &shown_form = c.shown_form;
    const auto &write_side_time = c.write_side_time;
    const double pick_lat = c.pick_lat;
    const double pick_lon = c.pick_lon;
    const auto &gps_badge = c.gps_badge;
    const auto &gps_badge_visible = c.gps_badge_visible;
    // ---- (a) 无规则：原值 = 生效值（未修改时两值相同也显示）+ 来源字段标注 ----
    {
        const QString old_shown = full(meta::objname::kTimeOld0);
        const QString new_shown = full(meta::objname::kTimeNew0);
        const QString src_shown = full(meta::objname::kTimeSrc0);
        const bool first_field_unmodified = !page.findChild<QLabel *>(meta::objname::kTimeNew0)
                                                 ->property(theme::kModProperty)
                                                 .toBool();
        std::printf("UI-SMOKE meta-geometry: page=%dx%d old-w=%d new-w=%d src-w=%d\n", page.width(),
                    page.height(), page.findChild<QLabel *>(meta::objname::kTimeOld0)->width(),
                    page.findChild<QLabel *>(meta::objname::kTimeNew0)->width(),
                    page.findChild<QLabel *>(meta::objname::kTimeSrc0)->width());
        std::fflush(stdout);
        std::printf("UI-SMOKE meta-baseline: old=%s new=%s src=%s unmodified=%d\n",
                    qUtf8Printable(old_shown), qUtf8Printable(new_shown), qUtf8Printable(src_shown),
                    first_field_unmodified ? 1 : 0);
        std::fflush(stdout);
        if (old_shown != QStringLiteral("2024-03-01 10:00:00") || new_shown != old_shown ||
            src_shown != QStringLiteral("DateTimeOriginal") || !first_field_unmodified) {
            smoke_fail(MainWindow::tr("生效值基线不符（old=%1 new=%2 src=%3）")
                           .arg(old_shown, new_shown, src_shown));
        }
        // 关键信息卡（只读）：8 个字段**逐格有显示**（空 = 缺陷；"—" = 源里确无该字段）。
        // fixture exif_full.jpg 只带 Artist/DateTimeOriginal/GPS（本文件头注释已列）→
        // 相机/镜头/曝光 三格**合法**为 "—"，其余五格必须是真值。
        const QStringList keys = {meta::objname::kInfoTime, meta::objname::kInfoCamera,
                                  meta::objname::kInfoLens, meta::objname::kInfoExposure,
                                  meta::objname::kInfoSize, meta::objname::kInfoColor,
                                  meta::objname::kInfoFile, meta::objname::kInfoGps};
        QStringList values;
        for (const QString &key : keys) {
            values << full(key);
        }
        std::printf("UI-SMOKE meta-info: %s\n", qUtf8Printable(values.join(QStringLiteral(" | "))));
        std::fflush(stdout);
        const std::array<int, 5> must_have = {0, 4, 5, 6, 7}; // 时间/尺寸/色彩/文件/GPS
        for (const int index : must_have) {
            const QString &value = values.at(index);
            if (value.isEmpty() || value == QStringLiteral("—")) {
                smoke_fail(MainWindow::tr("关键信息卡字段缺失（%1）：%2")
                               .arg(keys.at(index), values.join(QStringLiteral(" | "))));
            }
        }
        for (const QString &value : values) {
            if (value.isEmpty()) {
                smoke_fail(MainWindow::tr("关键信息卡有空字段：%1")
                               .arg(values.join(QStringLiteral(" | "))));
                break;
            }
        }
    }
}
// R-255：smoke_probe_meta 分节 sec_b（自原函数逐字搬移；断言/输出行不变）
void MainWindow::Impl::probe_meta_sec_b(const MetaProbeCtx &c, const QString &shots_dir) {
    // R-255：共享前置局部别名（ctx 承接；节体逐字不变）
    PageMeta &page = *c.page;
    const QString &repo = c.repo;
    const QString &fixture = c.fixture;
    const auto &label = c.label;
    const auto &full = c.full;
    const auto &edit_text = c.edit_text;
    const auto &placeholder = c.placeholder;
    const auto &wait_probe = c.wait_probe;
    const auto &shown_form = c.shown_form;
    const auto &write_side_time = c.write_side_time;
    const double pick_lat = c.pick_lat;
    const double pick_lon = c.pick_lon;
    const auto &gps_badge = c.gps_badge;
    const auto &gps_badge_visible = c.gps_badge_visible;
    // ---- (b) Δ+2h：显示生效值 == build_plan 写入值 == test_effective 金值 ----
    {
        pp::BatchRules rules;
        pp::TimeShift shift;
        shift.mode = pp::TimeShift::Mode::Delta;
        shift.hours = 2;
        rules.time_shift = shift;
        page.apply_rules(rules);
        pump(80);
        const QString shown = full(meta::objname::kTimeNew0);
        const QString written = shown_form(write_side_time(rules));
        const bool spin_mod = page.findChild<QSpinBox *>(meta::objname::kDeltaHours)
                                  ->property(theme::kModProperty)
                                  .toBool();
        const bool idle_spin_mod = page.findChild<QSpinBox *>(meta::objname::kDeltaYears)
                                       ->property(theme::kModProperty)
                                       .toBool();
        const bool new_mod = page.findChild<QLabel *>(meta::objname::kTimeNew0)
                                 ->property(theme::kModProperty)
                                 .toBool();
        std::printf("UI-SMOKE meta-effective: shown=%s write=%s golden=2024-03-01 12:00:00 "
                    "mod-hours=%d mod-years=%d mod-new=%d\n",
                    qUtf8Printable(shown), qUtf8Printable(written), spin_mod ? 1 : 0,
                    idle_spin_mod ? 1 : 0, new_mod ? 1 : 0);
        std::fflush(stdout);
        if (shown != QStringLiteral("2024-03-01 12:00:00") || shown != written || !spin_mod ||
            idle_spin_mod || !new_mod) {
            smoke_fail(MainWindow::tr("Δ+2h 生效值不符（shown=%1 write=%2 mod=%3/%4/%5）")
                           .arg(shown, written)
                           .arg(spin_mod ? 1 : 0)
                           .arg(idle_spin_mod ? 1 : 0)
                           .arg(new_mod ? 1 : 0));
        }
        if (!shots_dir.isEmpty()) { // 走查图：.mod（改动值 = 强调边框+强调色字+700）
            QDir().mkpath(shots_dir);
            const QString path = QDir(shots_dir).filePath(QStringLiteral("01c-meta-mod.png"));
            const bool saved = page.grab().save(path, "PNG");
            std::printf("UI-SMOKE meta-mod-shot: %s saved=%d\n", qUtf8Printable(path),
                        saved ? 1 : 0);
            std::fflush(stdout);
        }
    }
}
// R-255：smoke_probe_meta 分节 sec_c（自原函数逐字搬移；断言/输出行不变）
void MainWindow::Impl::probe_meta_sec_c(const MetaProbeCtx &c, const QString &shots_dir) {
    // R-255：共享前置局部别名（ctx 承接；节体逐字不变）
    PageMeta &page = *c.page;
    const QString &repo = c.repo;
    const QString &fixture = c.fixture;
    const auto &label = c.label;
    const auto &full = c.full;
    const auto &edit_text = c.edit_text;
    const auto &placeholder = c.placeholder;
    const auto &wait_probe = c.wait_probe;
    const auto &shown_form = c.shown_form;
    const auto &write_side_time = c.write_side_time;
    const double pick_lat = c.pick_lat;
    const double pick_lon = c.pick_lon;
    const auto &gps_badge = c.gps_badge;
    const auto &gps_badge_visible = c.gps_badge_visible;
    // ---- (c) 时区语义（+08:00 → +09:00）：墙钟 +1h，同样与写入值对拍 ----
    {
        pp::BatchRules rules;
        pp::TimeShift shift;
        shift.mode = pp::TimeShift::Mode::TimezoneSemantic;
        shift.from_offset_min = 480;
        shift.to_offset_min = 540;
        rules.time_shift = shift;
        page.apply_rules(rules);
        pump(80);
        const QString shown = full(meta::objname::kTimeNew0);
        const QString written = shown_form(write_side_time(rules));
        std::printf("UI-SMOKE meta-timezone: shown=%s write=%s golden=2024-03-01 11:00:00\n",
                    qUtf8Printable(shown), qUtf8Printable(written));
        std::fflush(stdout);
        if (shown != QStringLiteral("2024-03-01 11:00:00") || shown != written) {
            smoke_fail(
                MainWindow::tr("时区语义生效值不符（shown=%1 write=%2）").arg(shown, written));
        }
    }
}
// R-255：smoke_probe_meta 分节 sec_d（自原函数逐字搬移；断言/输出行不变）
void MainWindow::Impl::probe_meta_sec_d(const MetaProbeCtx &c, const QString &shots_dir) {
    // R-255：共享前置局部别名（ctx 承接；节体逐字不变）
    PageMeta &page = *c.page;
    const QString &repo = c.repo;
    const QString &fixture = c.fixture;
    const auto &label = c.label;
    const auto &full = c.full;
    const auto &edit_text = c.edit_text;
    const auto &placeholder = c.placeholder;
    const auto &wait_probe = c.wait_probe;
    const auto &shown_form = c.shown_form;
    const auto &write_side_time = c.write_side_time;
    const double pick_lat = c.pick_lat;
    const double pick_lon = c.pick_lon;
    const auto &gps_badge = c.gps_badge;
    const auto &gps_badge_visible = c.gps_badge_visible;
    // ---- (d) GPS 规则：生效坐标显示 + 地图 center_on 生效坐标 + 单钉（D4）----
    {
        pp::BatchRules rules;
        pp::GpsData gps;
        gps.lat = pick_lat;
        gps.lon = pick_lon;
        rules.gps = gps;
        page.apply_rules(rules);
        pump(120);
        auto *map = page.findChild<pp::map::MapWidget *>();
        const QString lat_text = edit_text(meta::objname::kPpMetaGpsLat);
        const QString lon_text = edit_text(meta::objname::kPpMetaGpsLon);
        const bool marker = map != nullptr && map->has_marker();
        const double center_lat = map != nullptr ? map->center_lat() : 0.0;
        const double center_lon = map != nullptr ? map->center_lon() : 0.0;
        const bool centered =
            std::fabs(center_lat - pick_lat) < 1e-3 && std::fabs(center_lon - pick_lon) < 1e-3;
        const bool mod = page.findChild<QLineEdit *>(meta::objname::kPpMetaGpsLat)
                             ->property(theme::kModProperty)
                             .toBool();
        std::printf("UI-SMOKE meta-gps: lat=%s lon=%s marker=%d center=%.6f,%.6f centered=%d "
                    "mod=%d\n",
                    qUtf8Printable(lat_text), qUtf8Printable(lon_text), marker ? 1 : 0, center_lat,
                    center_lon, centered ? 1 : 0, mod ? 1 : 0);
        std::fflush(stdout);
        if (lat_text != QStringLiteral("22.543100°") || lon_text != QStringLiteral("114.057900°") ||
            !marker || !centered || !mod) {
            smoke_fail(MainWindow::tr("GPS 生效值/地图跟随不符（lat=%1 lon=%2 marker=%3 "
                                      "centered=%4）")
                           .arg(lat_text, lon_text)
                           .arg(marker ? 1 : 0)
                           .arg(centered ? 1 : 0));
        }
    }
}
// R-255：smoke_probe_meta 分节 sec_e（自原函数逐字搬移；断言/输出行不变）
void MainWindow::Impl::probe_meta_sec_e(const MetaProbeCtx &c, const QString &shots_dir) {
    // R-255：共享前置局部别名（ctx 承接；节体逐字不变）
    PageMeta &page = *c.page;
    const QString &repo = c.repo;
    const QString &fixture = c.fixture;
    const auto &label = c.label;
    const auto &full = c.full;
    const auto &edit_text = c.edit_text;
    const auto &placeholder = c.placeholder;
    const auto &wait_probe = c.wait_probe;
    const auto &shown_form = c.shown_form;
    const auto &write_side_time = c.write_side_time;
    const double pick_lat = c.pick_lat;
    const double pick_lon = c.pick_lon;
    const auto &gps_badge = c.gps_badge;
    const auto &gps_badge_visible = c.gps_badge_visible;
    // ---- (e) 隐私剥除：时间/GPS 生效值 → 「将被移除」（§5.1；M5-T7：状态落卡头胶囊，
    //   占位符只留输入示例 → 生效值框 placeholder 恒空）----
    {
        pp::BatchRules rules;
        pp::TimeShift shift;
        shift.mode = pp::TimeShift::Mode::Delta;
        shift.hours = 2;
        rules.time_shift = shift;
        rules.strip_privacy = true;
        page.apply_rules(rules);
        pump(80);
        const QString time_text = full(meta::objname::kTimeNew0);
        const QString gps_lat = edit_text(meta::objname::kPpMetaGpsLat);
        const QString gps_hint = placeholder(meta::objname::kPpMetaGpsLat);
        const QString badge = gps_badge();
        std::printf("UI-SMOKE meta-strip: time=%s gps-value=%s gps-placeholder=%s badge=%s\n",
                    qUtf8Printable(time_text), qUtf8Printable(gps_lat), qUtf8Printable(gps_hint),
                    qUtf8Printable(badge));
        std::fflush(stdout);
        if (time_text != QStringLiteral("将被移除") || !gps_lat.isEmpty() || !gps_hint.isEmpty() ||
            badge != QStringLiteral("将被移除") || !gps_badge_visible()) {
            smoke_fail(MainWindow::tr("隐私剥除的生效值显示不符（time=%1 gps=%2/%3 badge=%4）")
                           .arg(time_text, gps_lat, gps_hint, badge));
        }
    }
}
// R-255：smoke_probe_meta 分节 sec_f（自原函数逐字搬移；断言/输出行不变）
void MainWindow::Impl::probe_meta_sec_f(const MetaProbeCtx &c, const QString &shots_dir) {
    // R-255：共享前置局部别名（ctx 承接；节体逐字不变）
    PageMeta &page = *c.page;
    const QString &repo = c.repo;
    const QString &fixture = c.fixture;
    const auto &label = c.label;
    const auto &full = c.full;
    const auto &edit_text = c.edit_text;
    const auto &placeholder = c.placeholder;
    const auto &wait_probe = c.wait_probe;
    const auto &shown_form = c.shown_form;
    const auto &write_side_time = c.write_side_time;
    const double pick_lat = c.pick_lat;
    const double pick_lon = c.pick_lon;
    const auto &gps_badge = c.gps_badge;
    const auto &gps_badge_visible = c.gps_badge_visible;
    // ---- (f) 清除 GPS：生效坐标空 + 「将清除 GPS」+ 钉清除，且**视口保持不动**（D4/§5.2）----
    {
        pp::BatchRules rules;
        rules.gps_clear = true;
        auto *map = page.findChild<pp::map::MapWidget *>();
        const double center_before_lat = map != nullptr ? map->center_lat() : 0.0;
        const double center_before_lon = map != nullptr ? map->center_lon() : 0.0;
        page.apply_rules(rules);
        pump(120);
        const QString gps_lat = edit_text(meta::objname::kPpMetaGpsLat);
        const QString gps_hint = placeholder(meta::objname::kPpMetaGpsLat);
        const QString badge = gps_badge();
        const bool marker = map != nullptr && map->has_marker();
        const bool kept = map != nullptr &&
                          std::fabs(map->center_lat() - center_before_lat) < 1e-9 &&
                          std::fabs(map->center_lon() - center_before_lon) < 1e-9;
        std::printf(
            "UI-SMOKE meta-gps-clear: value=%s placeholder=%s badge=%s marker=%d center-kept=%d\n",
            qUtf8Printable(gps_lat), qUtf8Printable(gps_hint), qUtf8Printable(badge),
            marker ? 1 : 0, kept ? 1 : 0);
        std::fflush(stdout);
        if (!gps_lat.isEmpty() || !gps_hint.isEmpty() || badge != QStringLiteral("将清除 GPS") ||
            marker || !kept) {
            smoke_fail(MainWindow::tr("清除 GPS 的生效值显示/地图语义不符（value=%1 ph=%2 "
                                      "badge=%3 marker=%4 kept=%5）")
                           .arg(gps_lat, gps_hint, badge)
                           .arg(marker ? 1 : 0)
                           .arg(kept ? 1 : 0));
        }
    }
}
// R-255：smoke_probe_meta 分节 sec_g（自原函数逐字搬移；断言/输出行不变）
void MainWindow::Impl::probe_meta_sec_g(const MetaProbeCtx &c, const QString &shots_dir) {
    // R-255：共享前置局部别名（ctx 承接；节体逐字不变）
    PageMeta &page = *c.page;
    const QString &repo = c.repo;
    const QString &fixture = c.fixture;
    const auto &label = c.label;
    const auto &full = c.full;
    const auto &edit_text = c.edit_text;
    const auto &placeholder = c.placeholder;
    const auto &wait_probe = c.wait_probe;
    const auto &shown_form = c.shown_form;
    const auto &write_side_time = c.write_side_time;
    const double pick_lat = c.pick_lat;
    const double pick_lon = c.pick_lon;
    const auto &gps_badge = c.gps_badge;
    const auto &gps_badge_visible = c.gps_badge_visible;
    // ---- (g) 多选（12 选）→ 逐行小表：上限 8 行 + 「…还有 4 个」（§5.2）----
    {
        QStringList paths;
        const QString meta_dir = repo + QStringLiteral("/tests/golden/meta");
        const QString base_dir = repo + QStringLiteral("/tests/golden/base");
        for (const QString &dir : {meta_dir, base_dir}) {
            const QDir d(dir);
            for (const QString &name : d.entryList(QDir::Files, QDir::Name)) {
                if (name.endsWith(QStringLiteral(".png")) ||
                    name.endsWith(QStringLiteral(".jpg")) ||
                    name.endsWith(QStringLiteral(".tif")) ||
                    name.endsWith(QStringLiteral(".webp")) ||
                    name.endsWith(QStringLiteral(".jxl")) ||
                    name.endsWith(QStringLiteral(".avif")) ||
                    name.endsWith(QStringLiteral(".heic"))) {
                    paths << d.filePath(name);
                }
            }
        }
        if (paths.size() < 12) {
            smoke_fail(MainWindow::tr("多选自检语料不足（%1 < 12）").arg(paths.size()));
        } else {
            const QStringList batch = paths.mid(0, 12);
            QList<MetaSelectionItem> items;
            for (const QString &path : batch) {
                items.append(meta_probe_item(path));
            }
            page.set_batch_files(batch);
            page.set_selection(items, /*from_selection=*/true);
            if (!wait_probe()) {
                smoke_fail(MainWindow::tr("元数据页自检：多选 probe 未在 10s 内回填"));
            }
            pump(120);
            const int rows = meta::ProbeContext(&page).multi_rows();
            const QString more = full(meta::objname::kMultiMore);
            const QString row0_name = full(meta::objname::kMultiName1.arg(0));
            const QString row0_old = full(meta::objname::kMultiOld1.arg(0));
            const QString row0_new = full(meta::objname::kMultiNew1.arg(0));
            const bool single_rows_hidden = full(meta::objname::kTimeOld0).isEmpty();
            std::printf("UI-SMOKE meta-multi: rows=%d more=%s row0=%s | %s -> %s "
                        "single-hidden=%d\n",
                        rows, qUtf8Printable(more), qUtf8Printable(row0_name),
                        qUtf8Printable(row0_old), qUtf8Printable(row0_new),
                        single_rows_hidden ? 1 : 0);
            std::fflush(stdout);
            if (rows != 8 || more != QStringLiteral("…还有 4 个") ||
                row0_name != QFileInfo(batch.first()).fileName()) {
                smoke_fail(MainWindow::tr("多选小表不符（rows=%1 more=%2 row0=%3）")
                               .arg(rows)
                               .arg(more, row0_name));
            }
            if (!shots_dir.isEmpty()) { // 走查图：多选小表（12 选 → 8 行 + 「…还有 4 个」）
                QDir().mkpath(shots_dir);
                const QString path = QDir(shots_dir).filePath(QStringLiteral("01b-meta-multi.png"));
                const bool saved = page.grab().save(path, "PNG");
                std::printf("UI-SMOKE meta-multi-shot: %s saved=%d\n", qUtf8Printable(path),
                            saved ? 1 : 0);
                std::fflush(stdout);
                if (!saved) {
                    smoke_fail(MainWindow::tr("多选小表走查图保存失败：%1").arg(path));
                }
            }
        }
    }
}
// R-255：smoke_probe_meta 分节 sec_g2（自原函数逐字搬移；断言/输出行不变）
void MainWindow::Impl::probe_meta_sec_g2(const MetaProbeCtx &c, const QString &shots_dir) {
    // R-255：共享前置局部别名（ctx 承接；节体逐字不变）
    PageMeta &page = *c.page;
    const QString &repo = c.repo;
    const QString &fixture = c.fixture;
    const auto &label = c.label;
    const auto &full = c.full;
    const auto &edit_text = c.edit_text;
    const auto &placeholder = c.placeholder;
    const auto &wait_probe = c.wait_probe;
    const auto &shown_form = c.shown_form;
    const auto &write_side_time = c.write_side_time;
    const double pick_lat = c.pick_lat;
    const double pick_lon = c.pick_lon;
    const auto &gps_badge = c.gps_badge;
    const auto &gps_badge_visible = c.gps_badge_visible;
    // ---- (g2) EXIF 编辑器：例外生效值回显 + .mod（§9.2；T12 step 8）----
    {
        pp::BatchRules rules;
        pp::TimeShift shift;
        shift.mode = pp::TimeShift::Mode::Delta;
        shift.hours = 2;
        rules.time_shift = shift;
        pp::GpsData gps;
        gps.lat = pick_lat;
        gps.lon = pick_lon;
        rules.gps = gps;
        ExifEditor editor(fixture, rules, std::nullopt, w);
        editor.setProperty("pp_exif_editor_suppress_modal", true);
        editor.show();
        pump(300);
        int tab_index = -1;
        if (auto *tabs = editor.findChild<QTabWidget *>(QStringLiteral("tabs"))) {
            tab_index = tabs->count() - 1; // 末页 = 时间 / GPS（与 make_time_gps_tab 同序）
            tabs->setCurrentIndex(tab_index);
        }
        pump(200);
        const auto *time_label = editor.findChild<QLabel *>(QStringLiteral("time_effective"));
        const auto *gps_label = editor.findChild<QLabel *>(meta::objname::kGpsEffectiveLabel);
        const QString time_text = time_label != nullptr ? time_label->text() : QString();
        const QString gps_text = gps_label != nullptr ? gps_label->text() : QString();
        const bool time_mod =
            time_label != nullptr && time_label->property(theme::kModProperty).toBool();
        const bool gps_mod =
            gps_label != nullptr && gps_label->property(theme::kModProperty).toBool();
        std::printf("UI-SMOKE meta-editor: tab=%d time=%s mod=%d gps=%s mod=%d\n", tab_index,
                    qUtf8Printable(time_text), time_mod ? 1 : 0, qUtf8Printable(gps_text),
                    gps_mod ? 1 : 0);
        std::fflush(stdout);
        if (time_text != QStringLiteral("生效：2024-03-01 12:00:00") ||
            gps_text != QStringLiteral("生效：22.543100, 114.057900") || !time_mod || !gps_mod) {
            smoke_fail(MainWindow::tr("EXIF 编辑器生效值回显不符（time=%1 gps=%2 mod=%3/%4）")
                           .arg(time_text, gps_text)
                           .arg(time_mod ? 1 : 0)
                           .arg(gps_mod ? 1 : 0));
        }
        if (!shots_dir.isEmpty()) { // 走查图（不占 8 张冻结截图的计数）
            QDir().mkpath(shots_dir);
            const QString path =
                QDir(shots_dir).filePath(QStringLiteral("05b-exif-editor-effective.png"));
            const bool saved = editor.grab().save(path, "PNG");
            std::printf("UI-SMOKE meta-editor-shot: %s saved=%d\n", qUtf8Printable(path),
                        saved ? 1 : 0);
            std::fflush(stdout);
            if (!saved) {
                smoke_fail(MainWindow::tr("EXIF 编辑器走查图保存失败：%1").arg(path));
            }
        }
        // ---- M5-T10（A8 零字形）：树行标记只存在于 item data ----
        // 探针：① 全树行文本里不得有 〔 〕 ● 字形；② 批量命中行 ≥ 1（本探针注入了 time_shift +
        // gps 规则）且该行的 text 里也没有「批」字形（标记由 delegate 画成中性 pill）；
        // ③ 注入一次改值 → 该行 changed=true 且文字不带 ●、字色 = 令牌 accent、字重 700。
        {
            int leaves = 0;
            int batch_rows = 0;
            int changed_rows = 0;
            int glyph_rows = 0;
            int changed_marked = 0;
            QWidget *tree_host = nullptr;
            auto *tree = editor.findChild<QTreeWidget *>(QStringLiteral("exif_tree"));
            QTreeWidgetItem *first_leaf = nullptr;
            if (tree != nullptr) {
                tree_host = tree;
                QList<QTreeWidgetItem *> stack;
                for (int i = 0; i < tree->topLevelItemCount(); ++i)
                    stack.push_back(tree->topLevelItem(i));
                // 字形清单按码点构造（避免本探针自身命中"字面量字形"扫描）：〔 〕 ●
                const char16_t banned[] = {0x3014, 0x3015, 0x25CF, 0x6279}; // 含「批」字
                while (!stack.isEmpty()) {
                    QTreeWidgetItem *item = stack.takeFirst();
                    for (int i = 0; i < item->childCount(); ++i)
                        stack.push_back(item->child(i));
                    // 叶子 = 非分组节点（分组节点的 node role = 1，且没有键）
                    if (item->childCount() != 0 ||
                        item->data(0, pp::ui::exif_roles::node).toInt() != 0)
                        continue;
                    ++leaves;
                    // 首叶子优先取可编辑行（MakerNote 归并/二进制块只读 → 换下一个）
                    if (first_leaf == nullptr ||
                        (first_leaf->text(0).contains(QStringLiteral("MakerNote")) &&
                         !item->text(0).contains(QStringLiteral("MakerNote"))))
                        first_leaf = item;
                    const QString text = item->text(0);
                    for (char16_t point : banned) {
                        if (text.contains(QChar(point))) {
                            ++glyph_rows;
                            break;
                        }
                    }
                    if (item->data(0, pp::ui::exif_roles::batch).toBool())
                        ++batch_rows;
                    if (item->data(0, pp::ui::exif_roles::changed).toBool())
                        ++changed_rows;
                }
                // ③ 注入改值：选中叶子 → 右值区改文本（textChanged → set_edit）
                if (first_leaf != nullptr) {
                    tree->setCurrentItem(first_leaf);
                    pump(120);
                    auto *pane_probe = editor.findChild<QLineEdit *>(QStringLiteral("exif_value"));
                    if (auto *pane = pane_probe) {
                        pane->setText(pane->text() + QStringLiteral("X"));
                        pump(120);
                        const QColor want = theme::tokens(theme::preferred_theme_mode()).accent;
                        const bool marked =
                            first_leaf->data(0, pp::ui::exif_roles::changed).toBool() &&
                            !first_leaf->text(0).contains(QChar(0x25CF)) &&
                            first_leaf->font(0).weight() == QFont::Bold &&
                            first_leaf->foreground(0).color() == want;
                        changed_marked = marked ? 1 : 0;
                    }
                }
            }
            std::printf("UI-SMOKE exif-rows: leaves=%d batch=%d changed-before=%d glyphs=%d "
                        "changed-marked=%d\n",
                        leaves, batch_rows, changed_rows, glyph_rows, changed_marked);
            std::fflush(stdout);
            if (tree_host == nullptr || leaves == 0 || glyph_rows != 0 || batch_rows == 0 ||
                !changed_marked) {
                smoke_fail(MainWindow::tr("EXIF 编辑器树行标记不符（leaves=%1 batch=%2 glyphs=%3 "
                                          "changed-marked=%4）")
                               .arg(leaves)
                               .arg(batch_rows)
                               .arg(glyph_rows)
                               .arg(changed_marked));
            }
        }
        editor.reject();
        pump(120);
    }
}
// R-255：smoke_probe_meta 分节 sec_h（自原函数逐字搬移；断言/输出行不变）
void MainWindow::Impl::probe_meta_sec_h(const MetaProbeCtx &c, const QString &shots_dir) {
    // R-255：共享前置局部别名（ctx 承接；节体逐字不变）
    PageMeta &page = *c.page;
    const QString &repo = c.repo;
    const QString &fixture = c.fixture;
    const auto &label = c.label;
    const auto &full = c.full;
    const auto &edit_text = c.edit_text;
    const auto &placeholder = c.placeholder;
    const auto &wait_probe = c.wait_probe;
    const auto &shown_form = c.shown_form;
    const auto &write_side_time = c.write_side_time;
    const double pick_lat = c.pick_lat;
    const double pick_lon = c.pick_lon;
    const auto &gps_badge = c.gps_badge;
    const auto &gps_badge_visible = c.gps_badge_visible;
    // ---- (h) 无选中 → 首个文件 + 「(1/N)」提示（§5.2）----
    {
        const QStringList batch{fixture, repo + QStringLiteral("/tests/golden/base/rgb8.png"),
                                repo + QStringLiteral("/tests/golden/base/photo.jpg")};
        page.set_batch_files(batch);
        page.set_selection(QList<MetaSelectionItem>{meta_probe_item(batch.first())},
                           /*from_selection=*/false);
        if (!wait_probe()) {
            smoke_fail(MainWindow::tr("元数据页自检：无选中回退的 probe 未回填"));
        }
        pump(80);
        const QString hint = full(meta::objname::kInfoHint);
        std::printf("UI-SMOKE meta-follow-fallback: hint=%s\n", qUtf8Printable(hint));
        std::fflush(stdout);
        const QString expected =
            QStringLiteral("%1 (1/%2)").arg(QFileInfo(fixture).fileName()).arg(batch.size());
        if (hint != expected) {
            smoke_fail(MainWindow::tr("无选中跟随提示不符（got=%1 want=%2）").arg(hint, expected));
        }
    }
}
// R-255：smoke_probe_meta 分节 sec_i（自原函数逐字搬移；断言/输出行不变）
void MainWindow::Impl::probe_meta_sec_i(const MetaProbeCtx &c, const QString &shots_dir) {
    // R-255：共享前置局部别名（ctx 承接；节体逐字不变）
    PageMeta &page = *c.page;
    const QString &repo = c.repo;
    const QString &fixture = c.fixture;
    const auto &label = c.label;
    const auto &full = c.full;
    const auto &edit_text = c.edit_text;
    const auto &placeholder = c.placeholder;
    const auto &wait_probe = c.wait_probe;
    const auto &shown_form = c.shown_form;
    const auto &write_side_time = c.write_side_time;
    const double pick_lat = c.pick_lat;
    const double pick_lon = c.pick_lon;
    const auto &gps_badge = c.gps_badge;
    const auto &gps_badge_visible = c.gps_badge_visible;
    // ---- (i) M5-T7 C7 几何探针（v6 §7.1 + R1–R4；值表驱动 checked/bad）----
    //   卡内行 pitch 28 / 标签轨 80 / 日期时间 spin 88 / 生效行 acc 值（像素取证）/ 空态收敛 /
    //   双列第二标签起点（R2：第二标签固定 x + 24px 间档）/ 操作行在地图上方（修 1-37）/
    //   卡头右置一律中性胶囊（1-24/1-25）/ 批量规则折叠行默认折叠 / 按钮 26（A7）/
    //   地图离线说明中性灰 + 括号进 tooltip（修 1-38/1-39）。
    {
        int meta_checked = 0;
        QStringList meta_bad;
        const auto meta_expect = [&meta_checked, &meta_bad](const char *name, bool ok,
                                                            const QString &detail) {
            ++meta_checked;
            if (!ok)
                meta_bad << QStringLiteral("%1(%2)").arg(QString::fromLatin1(name), detail);
        };

        // (i-1) 空态收敛（修 1-23/1-31）：未选择文件 → 关键信息卡/时间卡各收敛为一行说明
        {
            page.set_selection(QList<MetaSelectionItem>(), /*from_selection=*/false);
            pump(80);
            bool rows_hidden = true;
            for (int i = 0; i < 5; ++i) {
                const auto *row = page.findChild<QWidget *>(meta::objname::kInfoRow1.arg(i));
                if (row == nullptr || row->isVisible())
                    rows_hidden = false;
            }
            const auto *empty_row = page.findChild<QWidget *>(meta::objname::kInfoEmptyRow);
            const auto *empty_text = page.findChild<QLabel *>(meta::objname::kInfoEmpty);
            const auto *time_empty_row = page.findChild<QWidget *>(meta::objname::kTimeEmptyRow);
            const auto *view_lat = page.findChild<QLineEdit *>(meta::objname::kPpMetaGpsLat);
            const bool ok =
                rows_hidden && empty_row != nullptr && empty_row->isVisible() &&
                empty_text != nullptr &&
                empty_text->text() == QStringLiteral("未选择文件，请在左侧列表选择照片") &&
                time_empty_row != nullptr && time_empty_row->isVisible() && view_lat != nullptr &&
                view_lat->text().isEmpty() && !gps_badge_visible();
            meta_expect(
                "empty", ok,
                QStringLiteral("rows-hidden=%1 line=\"%2\" time-line=%3 gps-pill=%4")
                    .arg(rows_hidden ? 1 : 0)
                    .arg(empty_text != nullptr ? empty_text->text() : QStringLiteral("<none>"))
                    .arg(time_empty_row != nullptr && time_empty_row->isVisible() ? 1 : 0)
                    .arg(gps_badge_visible() ? 1 : 0));
        }

        // (i-2) 恢复跟随 + 生效行 acc 值（像素取证：修改态 = 强调色字；原值 = 非强调色）
        page.set_batch_files(QStringList{fixture});
        page.set_selection(QList<MetaSelectionItem>{meta_probe_item(fixture)},
                           /*from_selection=*/true);
        if (!wait_probe()) {
            smoke_fail(MainWindow::tr("元数据页自检：几何探针的 probe 未回填"));
        }
        pump(120);
        auto *time_new_label = page.findChild<QLabel *>(meta::objname::kTimeNew0);
        auto *time_old_label = page.findChild<QLabel *>(meta::objname::kTimeOld0);
        const auto color_distance = [](const QImage &image, const QColor &want) {
            int best = 1 << 30;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QColor px = image.pixelColor(x, y);
                    const int dr = px.red() - want.red();
                    const int dg = px.green() - want.green();
                    const int db = px.blue() - want.blue();
                    best = std::min(best, dr * dr + dg * dg + db * db);
                }
            }
            return best < (1 << 30) ? std::sqrt(double(best)) : 1e9;
        };
        if (time_new_label != nullptr && time_old_label != nullptr) {
            pp::BatchRules rules;
            pp::TimeShift shift;
            shift.mode = pp::TimeShift::Mode::Delta;
            shift.hours = 2;
            rules.time_shift = shift;
            page.apply_rules(rules);
            pump(120);
            const double accent_dist =
                color_distance(time_new_label->grab().toImage(), tokens.accent);
            const double plain_dist =
                color_distance(time_old_label->grab().toImage(), tokens.accent);
            const bool mod_flag = time_new_label->property(theme::kModProperty).toBool();
            meta_expect("acc-value", mod_flag && accent_dist <= 30.0 && plain_dist >= 60.0,
                        QStringLiteral("mod=%1 accent-dist=%.1f old-dist=%.1f")
                            .arg(mod_flag ? 1 : 0)
                            .arg(accent_dist)
                            .arg(plain_dist));
            page.apply_rules(pp::BatchRules{}); // 基线复位（无规则）
            pump(80);
        } else {
            meta_expect("acc-value", false, QStringLiteral("label missing"));
        }

        // (i-2b) §3.2：页内容不得反向撑大栏宽（内容宽 ≤ 页面宽；超出 = 右栏内横滚）
        QSize content_size;
        {
            const auto *content = page.findChild<QWidget *>(meta::objname::kContent);
            if (content != nullptr) {
                content_size = QSize(content->size().width(), content->minimumSizeHint().height());
            }
            QStringList mins;
            for (const auto *card : page.findChildren<QFrame *>(QStringLiteral("pp-card"))) {
                mins << QString::number(card->minimumSizeHint().width());
            }
            meta_expect("content-fit", content != nullptr && content->width() <= page.width(),
                        QStringLiteral("content=%1x%2 page=%3x%4 card-mins=[%5]")
                            .arg(content != nullptr ? content->width() : -1)
                            .arg(content != nullptr ? content->height() : -1)
                            .arg(page.width())
                            .arg(page.height())
                            .arg(mins.join(QLatin1Char(','))));
        }

        // (i-3) R1 行节奏 28 + R2 标签轨 80 + 日期时间 spin 88 / 高 24（A2：字段轨只 88/160/flex）
        {
            // 行容器 = formkit 未改名的行（pp-fk-row）∪ 本页命名的行（pp-meta-*-row）
            QList<QWidget *> rows = page.findChildren<QWidget *>(QStringLiteral("pp-fk-row"));
            for (QWidget *widget : page.findChildren<QWidget *>()) {
                const QString name = widget->objectName();
                if (name.startsWith(meta::objname::kPpMetaPrefix) &&
                    name.endsWith(QStringLiteral("-row")) && !rows.contains(widget)) {
                    rows.append(widget);
                }
            }
            int rows_seen = 0;
            int rows_bad = 0;
            for (QWidget *row : rows) {
                if (!row->isVisible())
                    continue;
                ++rows_seen;
                if (row->height() != formkit::kRowPitch)
                    ++rows_bad;
            }
            meta_expect("row-pitch", rows_seen >= 12 && rows_bad == 0,
                        QStringLiteral("rows=%1 bad=%2").arg(rows_seen).arg(rows_bad));
            int labels_seen = 0;
            int labels_bad = 0;
            const QString label_hook = QString::fromLatin1(theme::kFkLabel);
            for (QLabel *label : page.findChildren<QLabel *>()) {
                if (label->property(theme::kFkProperty).toString() != label_hook)
                    continue;
                ++labels_seen;
                if (label->width() != formkit::kLabelW)
                    ++labels_bad;
            }
            meta_expect("label-track", labels_seen >= 12 && labels_bad == 0,
                        QStringLiteral("labels=%1 bad=%2").arg(labels_seen).arg(labels_bad));
            int spin_bad = 0;
            for (const QString &name :
                 {meta::objname::kDeltaYears, meta::objname::kDeltaMonths,
                  meta::objname::kDeltaDays, meta::objname::kDeltaHours,
                  meta::objname::kDeltaMinutes, meta::objname::kDeltaSeconds}) {
                const auto *spin = page.findChild<QSpinBox *>(name);
                if (spin == nullptr || spin->width() != formkit::kFieldShort ||
                    spin->height() != formkit::kControlH)
                    ++spin_bad;
            }
            meta_expect("delta-spin", spin_bad == 0,
                        QStringLiteral("bad=%1 want=%2x%3")
                            .arg(spin_bad)
                            .arg(formkit::kFieldShort)
                            .arg(formkit::kControlH));
        }

        // (i-4) 双列第二标签起点（R2/A2）：160 档 = second_label_x(160)；kv 行 = 值轨右缘 + 24
        {
            const auto *tz_row = page.findChild<QWidget *>(meta::objname::kTzRow);
            int tz_label2_x = -1;
            if (tz_row != nullptr) {
                for (QLabel *label : tz_row->findChildren<QLabel *>()) {
                    if (label->property(theme::kFkProperty).toString() ==
                            QString::fromLatin1(theme::kFkLabel) &&
                        tz_label2_x < 0 && label->x() > 0) {
                        tz_label2_x = label->x();
                    }
                }
            }
            meta_expect("second-label-160",
                        tz_label2_x == formkit::second_label_x(formkit::kFieldStd),
                        QStringLiteral("x=%1 want=%2")
                            .arg(tz_label2_x)
                            .arg(formkit::second_label_x(formkit::kFieldStd)));
            // kv 双列行：第二标签 x 全行一致（不随内容漂移）+ = A2 的 [轨][24][80][12] 递推
            const auto *camera_value = page.findChild<QLabel *>(meta::objname::kInfoCamera);
            const auto *exposure_value = page.findChild<QLabel *>(meta::objname::kInfoExposure);
            const auto *color_value = page.findChild<QLabel *>(meta::objname::kInfoColor);
            const auto *gps_value = page.findChild<QLabel *>(meta::objname::kInfoGps);
            const auto page_x = [&page](const QWidget *widget) {
                return widget->mapTo(&page, QPoint(0, 0)).x();
            };
            const int col2 = camera_value != nullptr ? page_x(camera_value) : -1;
            const bool aligned = camera_value != nullptr && exposure_value != nullptr &&
                                 color_value != nullptr && gps_value != nullptr &&
                                 page_x(exposure_value) == col2 && page_x(color_value) == col2 &&
                                 page_x(gps_value) == col2;
            // kv 行 0 的列结构（A2 逐字）：[80 标签][12][轨][24][80 第二标签][12][轨]
            const auto *row0 = page.findChild<QWidget *>(meta::objname::kInfoRow0);
            int l1 = -1, l2 = -1, t1_x = -1, t1_w = -1, t2_x = -1, t2_w = -1;
            if (row0 != nullptr) {
                int label_seen = 0;
                for (QWidget *child : row0->findChildren<QWidget *>()) {
                    if (child->parentWidget() != row0)
                        continue;
                    const bool is_label = child->property(theme::kFkProperty).toString() ==
                                          QString::fromLatin1(theme::kFkLabel);
                    if (is_label) {
                        if (label_seen == 0)
                            l1 = child->x();
                        else if (label_seen == 1)
                            l2 = child->x();
                        ++label_seen;
                    } else if (child->objectName() == QStringLiteral("pp-fk-track")) {
                        if (t1_x < 0) {
                            t1_x = child->x();
                            t1_w = child->width();
                        } else {
                            t2_x = child->x();
                            t2_w = child->width();
                        }
                    }
                }
            }
            const bool grid_ok =
                l1 >= 0 && l2 > l1 && t1_x >= 0 && t2_x > t1_x &&
                l2 == l1 + formkit::kLabelW + formkit::kColGap + t1_w + formkit::kDoubleGap &&
                t2_x == l2 + formkit::kLabelW + formkit::kColGap &&
                std::abs(t1_w - t2_w) <= 1; // 同列同宽（两栏都是 flex → 平分富余宽）
            meta_expect("second-label-kv", aligned && grid_ok,
                        QStringLiteral("col2=%1 aligned=%2 l1=%3 l2=%4 t1=%5+%6 t2=%7+%8")
                            .arg(col2)
                            .arg(aligned ? 1 : 0)
                            .arg(l1)
                            .arg(l2)
                            .arg(t1_x)
                            .arg(t1_w)
                            .arg(t2_x)
                            .arg(t2_w));
        }

        // (i-5) 卡头右置一律中性胶囊（1-24/1-25）+ GPS 状态徽标（修 1-33/「已有 GPS」）
        {
            const auto tone_of = [&page](const QString &name) {
                const auto *label = page.findChild<QLabel *>(name);
                if (label == nullptr)
                    return -1;
                // 卡头胶囊由本页构造（PillBadge）→ 静态下转安全；取色调键
                return int(static_cast<const formkit::PillBadge *>(label)->tone());
            };
            const bool neutral =
                tone_of(meta::objname::kInfoPill) == int(formkit::PillBadge::Tone::Neutral) &&
                tone_of(meta::objname::kTimePill) == int(formkit::PillBadge::Tone::Neutral) &&
                tone_of(meta::objname::kGpsPill) == int(formkit::PillBadge::Tone::Neutral) &&
                tone_of(meta::objname::kPrivacyPill) == int(formkit::PillBadge::Tone::Neutral);
            meta_expect("header-pill-neutral", neutral,
                        QStringLiteral("info=%1 time=%2 gps=%3 privacy=%4")
                            .arg(tone_of(meta::objname::kInfoPill))
                            .arg(tone_of(meta::objname::kTimePill))
                            .arg(tone_of(meta::objname::kGpsPill))
                            .arg(tone_of(meta::objname::kPrivacyPill)));
            // 金样带 GPS 且无规则 → 状态胶囊 = 「已有 GPS」（独立徽标，非 placeholder）
            meta_expect("gps-state-badge",
                        gps_badge_visible() && gps_badge() == QStringLiteral("已有 GPS"),
                        QStringLiteral("visible=%1 text=%2")
                            .arg(gps_badge_visible() ? 1 : 0)
                            .arg(gps_badge()));
            // 隐私与文件时间（§7.1）：开关 30×16 + 说明 + 文字链（高 26）；关键字行带「编辑…」
            {
                int switch_rows = 0;
                for (const QString &name :
                     {meta::objname::kPrivacyStrip, meta::objname::kMtimeSync}) {
                    const auto *button = page.findChild<QAbstractButton *>(name);
                    if (button != nullptr && button->width() == formkit::kSwitchW &&
                        button->height() == formkit::kSwitchH && button->isCheckable()) {
                        ++switch_rows;
                    }
                }
                const auto *tags_link =
                    page.findChild<QToolButton *>(meta::objname::kPrivacyStripLink);
                const auto *exc_link = page.findChild<QToolButton *>(meta::objname::kMtimeSyncLink);
                const auto *keywords_link =
                    page.findChild<QToolButton *>(meta::objname::kKeywordsLink);
                const bool links_ok =
                    tags_link != nullptr && tags_link->text() == QStringLiteral("标签编辑…") &&
                    tags_link->height() == theme::Metrics::btn_h && exc_link != nullptr &&
                    exc_link->text() == QStringLiteral("例外…") && keywords_link != nullptr &&
                    keywords_link->text() == QStringLiteral("编辑…") &&
                    keywords_link->height() == theme::Metrics::btn_h;
                meta_expect(
                    "switch-rows", switch_rows == 2 && links_ok,
                    QStringLiteral("switches=%1 links=%2").arg(switch_rows).arg(links_ok ? 1 : 0));

                // (i-5b) W-R-T13（A39 ⑥ 缺陷根治回执）：隐私卡**卡体在卡内布局里**。
                //   病灶（缺陷复现时的取值）：卡体是"有父无布局"的孤儿 —— 几何停在默认 100×30、
                //   三行被压成 6px pitch 贴在卡左上角并盖住卡题（截图里的"圆形伪影"）。
                //   判据（姿态可判、不依赖缺陷形态）：卡高容得下 [卡头 28 + 3 行 × 28] + 三行
                //   pitch = 28 + 首行不压卡头（y ≥ 卡头高）。
                {
                    QWidget *card = nullptr;
                    const auto *strip_now =
                        page.findChild<QAbstractButton *>(meta::objname::kPrivacyStrip);
                    for (QWidget *p = strip_now != nullptr ? strip_now->parentWidget() : nullptr;
                         p != nullptr; p = p->parentWidget()) {
                        if (p->objectName() == QStringLiteral("pp-card")) {
                            card = p;
                            break;
                        }
                    }
                    QList<QWidget *> rows;
                    if (card != nullptr) {
                        rows = card->findChildren<QWidget *>(QStringLiteral("pp-fk-row"));
                    }
                    // 行在**卡坐标系**里的落点（行父 = 卡体；pitch 与"是否压住卡头"都按卡坐标判）
                    const auto row_y = [card](const QWidget *row) {
                        return row->mapTo(card, QPoint(0, 0)).y();
                    };
                    std::sort(rows.begin(), rows.end(),
                              [&row_y](const QWidget *a, const QWidget *b) {
                                  return row_y(a) < row_y(b);
                              });
                    int pitch_bad = 0;
                    for (int i = 1; i < rows.size(); ++i) {
                        if (row_y(rows.at(i)) - row_y(rows.at(i - 1)) != formkit::kRowPitch)
                            ++pitch_bad;
                    }
                    const int first_y = rows.isEmpty() ? -1 : row_y(rows.first());
                    const int want_h = formkit::kRowPitch * 4; // 卡头 1 行 + 卡体 3 行
                    const bool body_ok = card != nullptr && rows.size() == 3 && pitch_bad == 0 &&
                                         first_y >= formkit::kRowPitch && card->height() >= want_h;
                    meta_expect("privacy-card-body", body_ok,
                                QStringLiteral("card-h=%1 rows=%2 pitch-bad=%3 first-y=%4 want>=%5")
                                    .arg(card != nullptr ? card->height() : -1)
                                    .arg(rows.size())
                                    .arg(pitch_bad)
                                    .arg(first_y)
                                    .arg(want_h));
                }
            }
        }

        // (i-6) 修 1-37：地图操作行固定在地图上方（核心动作不落进内滚盲区）+ 折叠行默认折叠
        {
            const auto *ops_row = page.findChild<QWidget *>(meta::objname::kGpsOpsRow);
            const auto *map = page.findChild<pp::map::MapWidget *>(meta::objname::kMap);
            const auto *rule_host = page.findChild<QWidget *>(meta::objname::kGpsRule);
            const auto *rule_toggle = page.findChild<QToolButton *>(meta::objname::kGpsRuleToggle);
            const bool above =
                ops_row != nullptr && map != nullptr &&
                ops_row->mapTo(&page, QPoint(0, 0)).y() < map->mapTo(&page, QPoint(0, 0)).y();
            meta_expect("ops-above-map", above,
                        QStringLiteral("ops-y=%1 map-y=%2")
                            .arg(ops_row != nullptr ? ops_row->mapTo(&page, QPoint(0, 0)).y() : -1)
                            .arg(map != nullptr ? map->mapTo(&page, QPoint(0, 0)).y() : -1));
            meta_expect("rule-folded",
                        rule_toggle != nullptr && rule_host != nullptr && !rule_host->isVisible() &&
                            !rule_toggle->isChecked(),
                        QStringLiteral("toggle=%1 host-visible=%2")
                            .arg(rule_toggle != nullptr ? 1 : 0)
                            .arg(rule_host != nullptr && rule_host->isVisible() ? 1 : 0));
            // 操作行/搜索钮高 26（A7 按钮换锚）
            int btn_bad = 0;
            for (const QString &name :
                 {meta::objname::kGpsRead, meta::objname::kGpsPick, meta::objname::kGpsClear,
                  meta::objname::kGpsSearchButton}) {
                const auto *button = page.findChild<QPushButton *>(name);
                if (button == nullptr || button->height() != theme::Metrics::btn_h)
                    ++btn_bad;
            }
            meta_expect("btn-26", btn_bad == 0,
                        QStringLiteral("bad=%1 want=%2").arg(btn_bad).arg(theme::Metrics::btn_h));
        }

        // (i-6b) UX-4②（M7-WP7 只增）：分类 chips 行已收进搜索框右侧「分类 ▾」下拉 ——
        //   objectName pp-meta-gps-category-0..5 由菜单 action 继承；探针断言菜单项数 == 6
        //   且逐一在位（ux-review §4 改进方案 ② 的取证口径）。
        {
            const auto *cat_btn = page.findChild<QPushButton *>(meta::objname::kGpsCategoryMenu);
            const QMenu *menu = cat_btn != nullptr ? cat_btn->menu() : nullptr;
            int items = 0;
            bool names_ok = menu != nullptr;
            if (menu != nullptr) {
                const QList<QAction *> actions = menu->actions();
                items = int(actions.size());
                for (int i = 0; i < actions.size(); ++i) {
                    names_ok = names_ok &&
                               actions.at(i)->objectName() == meta::objname::kGpsCategory1.arg(i);
                }
            }
            meta_expect("category-menu", items == 6 && names_ok,
                        QStringLiteral("btn=%1 items=%2 names=%3")
                            .arg(cat_btn != nullptr ? 1 : 0)
                            .arg(items)
                            .arg(names_ok ? 1 : 0));
        }

        // (i-7) 修 1-38/1-39 + A30：地图离线说明 = 中性灰 + 括号进 tooltip。
        //   T12/A30 加强：banner 本体的**警示黄已去** —— mapwidget.cpp 不再自持硬编码语义色，
        //   故此处除文案/tooltip/令牌灰外，另做两路取证：(a) banner 调色板不得是原警示黄
        //   #fff1b8；(b) banner 实拍像素中不得出现原警示黄（阈值 40）。
        {
            const auto *map = page.findChild<pp::map::MapWidget *>(meta::objname::kMap);
            QString text;
            QString tip;
            QString sheet;
            QLabel *banner = nullptr;
            if (map != nullptr) {
                const QList<QLabel *> labels = map->findChildren<QLabel *>();
                if (!labels.isEmpty()) {
                    banner = labels.first();
                    text = banner->text();
                    tip = banner->toolTip();
                    sheet = banner->styleSheet();
                }
            }
            const bool neutral_gray = sheet.contains(theme::css_color(tokens.text3)) &&
                                      !sheet.contains(theme::css_color(tokens.warn));
            // (a) 调色板：原硬编码警示黄（底 #fff1b8 / 字 #6b4a00）不得残留
            const QColor legacy_bg(0xff, 0xf1, 0xb8);
            const QColor legacy_fg(0x6b, 0x4a, 0x00);
            bool palette_clean = banner != nullptr;
            if (banner != nullptr) {
                palette_clean = banner->palette().color(QPalette::Window) != legacy_bg &&
                                banner->palette().color(QPalette::WindowText) != legacy_fg;
            }
            // (b) 像素：banner 实拍里不得有**成片**接近警示黄的像素（banner 在离线态可见）。
            // M5-T13 验收取证（只增；M5-T14 口径换锚）：旧判据 = 全幅**最近**像素距离 > 40 ——
            // 属"点代理"，真实平台**亮色**下被文字 ClearType 子像素抗锯齿的零星暖色彩边触发
            // （实测 #f7e7c9 距 #fff1b8 = 21.3 < 40），而 banner 用色本身（调色板/QSS/文案）全部
            // 清白 → 语义（"警示黄不得上屏"）不动，动的是取样口径（登记为机械换锚）。新口径两条
            // 互补，判据只增不减：
            //   (b1) **内边距环带**（= 居中文字盒之外的一圈，天然避开文字行的抗锯齿彩边）严格
            //        口径：环带内最近警示黄像素距离 > 40 —— 旧实现（黄底 #fff1b8 / 黄框）在此
            //        必然命中（底色/描边铺满环带）；
            //   (b2) 全幅近警示黄像素**计数占比** < 5%（A30 口径换锚后的校准值）：实心黄底/黄字
            //        是成片像素（≥ 10% 量级），零星彩边只有几粒 —— 真实平台实测亮色 1.19%
            //        （107/8976，ClearType 彩边）、暗色 0.00% → 5% 为两侧各 ≥ 4× 余量的分界
            //        （见诊断行近警示黄计数/占比）。
            constexpr double kAmberDistPx = 40.0;  // A30 阈值（T12 口径，不改）
            constexpr double kAmberRatioPct = 5.0; // b2 占比上限（%）
            double amber_dist = 1e9;               // 全幅最近（诊断）
            double ring_dist = 1e9;                // 环带最近（b1 判据）
            int amber_x = -1;
            int amber_y = -1;
            int ring_x = -1;
            int ring_y = -1;
            QColor amber_px;
            QColor ring_px;
            int near_amber = 0;  // 全幅 dist <= kAmberDistPx 计数（b2 分子）
            int shot_pixels = 0; // b2 分母
            qreal shot_dpr = 1.0;
            QRect text_box;
            if (banner != nullptr && banner->isVisible()) {
                const QImage shot = banner->grab().toImage();
                shot_pixels = shot.width() * shot.height();
                shot_dpr = shot.devicePixelRatio() > 0.01 ? shot.devicePixelRatio() : 1.0;
                // 文字行盒：居中绘制的单行文本（§7.1 的 banner 只有一行）；外扩 3 逻辑像素吞掉
                // 抗锯齿外溢 → 盒**之外**即"避开文字行"的环带
                const QSizeF text_sz = QFontMetricsF(banner->font()).size(Qt::TextSingleLine, text);
                const QSize text_phys(int(std::ceil(text_sz.width() * shot_dpr)),
                                      int(std::ceil(text_sz.height() * shot_dpr)));
                const int pad = int(std::ceil(3.0 * shot_dpr));
                text_box = QRect(QPoint((shot.width() - text_phys.width()) / 2,
                                        (shot.height() - text_phys.height()) / 2),
                                 text_phys)
                               .adjusted(-pad, -pad, pad, pad);
                for (int y = 0; y < shot.height(); ++y) {
                    for (int x = 0; x < shot.width(); ++x) {
                        const QColor px = shot.pixelColor(x, y);
                        const int dr = px.red() - legacy_bg.red();
                        const int dg = px.green() - legacy_bg.green();
                        const int db = px.blue() - legacy_bg.blue();
                        const double d = std::sqrt(double(dr * dr + dg * dg + db * db));
                        if (d <= kAmberDistPx)
                            ++near_amber;
                        if (d < amber_dist) {
                            amber_dist = d;
                            amber_x = x;
                            amber_y = y;
                            amber_px = px;
                        }
                        if (!text_box.contains(x, y) && d < ring_dist) {
                            ring_dist = d;
                            ring_x = x;
                            ring_y = y;
                            ring_px = px;
                        }
                    }
                }
                const double ratio_pct =
                    shot_pixels > 0 ? 100.0 * double(near_amber) / double(shot_pixels) : 0.0;
                std::printf("UI-SMOKE offline-banner-px: amber-dist=%.1f at %d,%d #%02x%02x%02x "
                            "ring-dist=%.1f at %d,%d #%02x%02x%02x near=%d/%d(%.2f%%) dpr=%.1f "
                            "text-box=%d,%d,%dx%d shot=%dx%d\n",
                            amber_dist, amber_x, amber_y, amber_px.red(), amber_px.green(),
                            amber_px.blue(), ring_dist, ring_x, ring_y, ring_px.red(),
                            ring_px.green(), ring_px.blue(), near_amber, shot_pixels, ratio_pct,
                            shot_dpr, text_box.x(), text_box.y(), text_box.width(),
                            text_box.height(), shot.width(), shot.height());
                std::fflush(stdout);
            }
            const double amber_ratio_pct =
                shot_pixels > 0 ? 100.0 * double(near_amber) / double(shot_pixels) : 0.0;
            meta_expect(
                "offline-banner",
                banner != nullptr && banner->isVisible() && text == QStringLiteral("地图离线") &&
                    tip == QStringLiteral("可手动输入坐标") && neutral_gray && palette_clean &&
                    ring_dist > kAmberDistPx && amber_ratio_pct < kAmberRatioPct,
                QStringLiteral("text=\"%1\" tip=\"%2\" gray=%3 palette-clean=%4 "
                               "visible=%5 amber-dist=%6 at %7,%8 ring=%9 at %10,%11 "
                               "near=%12/%13(ratio=%14%%)")
                    .arg(text, tip)
                    .arg(neutral_gray ? 1 : 0)
                    .arg(palette_clean ? 1 : 0)
                    .arg(banner != nullptr && banner->isVisible() ? 1 : 0)
                    .arg(amber_dist)
                    .arg(amber_x)
                    .arg(amber_y)
                    .arg(ring_dist)
                    .arg(ring_x)
                    .arg(ring_y)
                    .arg(near_amber)
                    .arg(shot_pixels)
                    .arg(amber_ratio_pct, 0, 'f', 2));
        }

        // (i-8) A27（T11 缺陷根治受益面）：时间卡模式分段 —— 段自持高（= kSegH − 2）且可见可选
        {
            const auto *seg = page.findChild<formkit::SegmentedControl *>(meta::objname::kSeg);
            int seg_h = -1;
            int seg_w = -1;
            bool visible = false;
            bool uniform = seg != nullptr && seg->count() == 2;
            if (seg != nullptr) {
                for (int i = 0; i < seg->count(); ++i) {
                    QWidget *segment = seg->segment(i);
                    if (segment == nullptr) {
                        uniform = false;
                        continue;
                    }
                    if (i == 0) {
                        seg_h = segment->height();
                        seg_w = segment->width();
                    }
                    visible = visible || segment->isVisible();
                    uniform = uniform && segment->isVisible() && segment->height() == seg_h &&
                              segment->width() == seg_w;
                }
            }
            meta_expect("seg-visible",
                        seg != nullptr && seg->height() == formkit::kSegH && seg_h > 0 &&
                            seg_h == formkit::seg_inner_h() && seg_w == formkit::kSegW && visible &&
                            uniform && seg->current_index() >= 0 &&
                            seg->current_index() < seg->count(),
                        QStringLiteral("seg=%1x%2 seg-h=%3(期望 %4) visible=%5 uniform=%6")
                            .arg(seg != nullptr ? seg->width() : -1)
                            .arg(seg != nullptr ? seg->height() : -1)
                            .arg(seg_h)
                            .arg(formkit::seg_inner_h())
                            .arg(visible ? 1 : 0)
                            .arg(uniform ? 1 : 0));
        }

        // (i-9) M6-T12（§5.3/§7-T12 体验件）：地图覆盖层 —— 「－/＋」缩放钮（矢量图标钮；点按
        //   真的改档：同中心下 grab 逐像素变化，±1 往返复原）+ 光标坐标读数（接 cursor_moved，
        //   读数 = 信号值 = 6 位小数；坐标行 tooltip 同读数）+ 比例尺（mapwidget 自绘：左下白底
        //   α190 段的像素取证）。原样保留 §2.5 的滚轮/拖拽/点击三件套探针（(j) 段）。
        {
            auto *zoom_map = page.findChild<pp::map::MapWidget *>(meta::objname::kMap);
            auto *zoom_in = page.findChild<QToolButton *>(meta::objname::kMapZoomIn);
            auto *zoom_out = page.findChild<QToolButton *>(meta::objname::kMapZoomOut);
            auto *cursor_label = page.findChild<QLabel *>(meta::objname::kMapCursor);
            const auto zoom_button_ok = [](const QToolButton *button, const char *icon_key) {
                return button != nullptr && button->width() == 24 && button->height() == 24 &&
                       button->text().isEmpty() && !button->icon().isNull() &&
                       button->property(icons::kIconNameProperty).toString() ==
                           QString::fromLatin1(icon_key) &&
                       !button->toolTip().isEmpty() && !button->accessibleName().isEmpty();
            };
            meta_expect("map-zoom-buttons",
                        zoom_button_ok(zoom_in, "zoom-in") && zoom_button_ok(zoom_out, "zoom-out"),
                        QStringLiteral("in=%1x%2 out=%3x%4")
                            .arg(zoom_in != nullptr ? zoom_in->width() : -1)
                            .arg(zoom_in != nullptr ? zoom_in->height() : -1)
                            .arg(zoom_out != nullptr ? zoom_out->width() : -1)
                            .arg(zoom_out != nullptr ? zoom_out->height() : -1));
            if (zoom_map != nullptr && zoom_in != nullptr && zoom_out != nullptr &&
                cursor_label != nullptr) {
                const QImage before = zoom_map->grab().toImage();
                zoom_in->click();
                pump(60);
                const QImage zoomed = zoom_map->grab().toImage();
                zoom_out->click();
                pump(60);
                const QImage restored = zoom_map->grab().toImage();
                meta_expect("map-zoom-applied", before != zoomed && restored == before,
                            QStringLiteral("in=%1x%2 out=%3x%4 zoomed-diff=%5 restored-eq=%6")
                                .arg(before.width())
                                .arg(before.height())
                                .arg(zoomed.width())
                                .arg(zoomed.height())
                                .arg(before != zoomed ? 1 : 0)
                                .arg(restored == before ? 1 : 0));
                // ③ 光标读数：cursor_moved（公开信号）→ chip 文本 + 坐标行 tooltip（同读数）
                zoom_map->cursor_moved(31.230400, 121.473700);
                pump(40);
                const QString want_cursor = QStringLiteral("光标: %1, %2")
                                                .arg(QString::number(31.230400, 'f', 6),
                                                     QString::number(121.473700, 'f', 6));
                const auto *datum_row = page.findChild<QWidget *>(meta::objname::kGpsDatumRow);
                meta_expect("map-cursor-readout",
                            cursor_label->text() == want_cursor && datum_row != nullptr &&
                                datum_row->toolTip() == want_cursor,
                            QStringLiteral("text=\"%1\" tip=\"%2\"")
                                .arg(cursor_label->text())
                                .arg(datum_row != nullptr ? datum_row->toolTip() : QString()));
                // ④ 比例尺（左下）：白底 α190 段 —— 底部左侧 96×26 内近白像素计数（底 = 灰 #e9，
                //   网格 #d2 → 阈值 244 只有比例尺底能达标；banner 居中不落此区）
                const QImage shot = zoom_map->grab().toImage();
                const int roi_w = std::min(shot.width(), 96);
                const int roi_h = std::min(shot.height(), 26);
                int near_white = 0;
                for (int y = shot.height() - roi_h; y < shot.height(); ++y) {
                    for (int x = 0; x < roi_w; ++x) {
                        const QColor px = shot.pixelColor(x, y);
                        if (px.red() >= 244 && px.green() >= 244 && px.blue() >= 244) {
                            ++near_white;
                        }
                    }
                }
                meta_expect("map-scale-bar", near_white >= 20,
                            QStringLiteral("near-white=%1 roi=%2x%3 shot=%4x%5")
                                .arg(near_white)
                                .arg(roi_w)
                                .arg(roi_h)
                                .arg(shot.width())
                                .arg(shot.height()));
                std::printf("UI-SMOKE map-hud: zoom-btn=%dx%d cursor=\"%s\" scale-bar near=%d/%d "
                            "roi=%dx%d shot=%dx%d\n",
                            zoom_in->width(), zoom_in->height(),
                            qUtf8Printable(cursor_label->text()), near_white, roi_w * roi_h, roi_w,
                            roi_h, shot.width(), shot.height());
                std::fflush(stdout);
            } else {
                meta_expect("map-zoom-applied", false, QStringLiteral("controls missing"));
                meta_expect("map-cursor-readout", false, QStringLiteral("controls missing"));
                meta_expect("map-scale-bar", false, QStringLiteral("controls missing"));
            }
        }

        std::printf("UI-SMOKE meta-geom: checked=%d bad=%d content=%dx%d(pane=%dx%d) %s\n",
                    meta_checked, int(meta_bad.size()), content_size.width(), content_size.height(),
                    page.width(), page.height(), qUtf8Printable(meta_bad.join(QLatin1Char(' '))));
        // 注：content 高 = 布局最小高（右栏 4 卡组所需高度）；> 页面高 = 该栏内滚（§3.2 禁的是
        // 横向反撑，纵向滚动与 M4 同口径）—— 探针只报数，不设阈值。
        std::fflush(stdout);
        if (!meta_bad.isEmpty()) {
            smoke_fail(MainWindow::tr("元数据页几何（§7.1/R1–R4）不符：%1")
                           .arg(meta_bad.join(QLatin1Char(' '))));
        }
    }
}
// R-255：smoke_probe_meta 分节 sec_j（自原函数逐字搬移；断言/输出行不变）
void MainWindow::Impl::probe_meta_sec_j(const MetaProbeCtx &c, const QString &shots_dir) {
    // R-255：共享前置局部别名（ctx 承接；节体逐字不变）
    PageMeta &page = *c.page;
    const QString &repo = c.repo;
    const QString &fixture = c.fixture;
    const auto &label = c.label;
    const auto &full = c.full;
    const auto &edit_text = c.edit_text;
    const auto &placeholder = c.placeholder;
    const auto &wait_probe = c.wait_probe;
    const auto &shown_form = c.shown_form;
    const auto &write_side_time = c.write_side_time;
    const double pick_lat = c.pick_lat;
    const double pick_lon = c.pick_lon;
    const auto &gps_badge = c.gps_badge;
    const auto &gps_badge_visible = c.gps_badge_visible;
    // ---- (j) M6-T9 选点闭环（R3/§4.4）：搜索→多钉+可点列表→点钉/点行→主钉+规则框回填→
    //   地图微调→「地图选点」取中心→逆地理确认。全程离线（探针不发请求）：搜索成功路径的
    //   两步输出由探针等价地喂入 —— 图钉面 `set_result_pins`（公开加性 API），列表面直发
    //   `search_results_ready`（公开信号；页面结果面唯一入口，等价于搜索成功路径的第一步）。
    //   **选点值 = WGS-84 断言**：amap（GCJ-02）基准面下，规则框回填值仍逐位等于结果点的
    //   WGS-84，并显式排除 GCJ-02 偏移值（两者相差 > 1e-4°）。终点（真实逆地理地址）由
    //   M6-T15 验收的联网证据承担；此处断言离线分类文案（逆地理在离线态不谎报地址）。
    {
        int loop_checked = 0;
        QStringList loop_bad;
        const auto loop_expect = [&loop_checked, &loop_bad](const char *name, bool ok,
                                                            const QString &detail) {
            ++loop_checked;
            if (!ok)
                loop_bad << QStringLiteral("%1(%2)").arg(QString::fromLatin1(name), detail);
        };
        auto *map = page.findChild<pp::map::MapWidget *>(meta::objname::kMap);
        auto *combo = page.findChild<QComboBox *>(meta::objname::kGpsProvider);
        auto *list = page.findChild<QListWidget *>(meta::objname::kGpsSearchResults);
        auto *pick = page.findChild<QPushButton *>(meta::objname::kGpsPick);
        auto *status_label = page.findChild<QLabel *>(meta::objname::kGpsStatus);
        auto *lat_edit = page.findChild<QLineEdit *>(meta::objname::kGpsLat);
        auto *lon_edit = page.findChild<QLineEdit *>(meta::objname::kGpsLon);
        if (map == nullptr || combo == nullptr || list == nullptr || pick == nullptr ||
            status_label == nullptr || lat_edit == nullptr || lon_edit == nullptr) {
            loop_expect("controls", false, QStringLiteral("闭环控件缺失"));
        } else {
            // 规则清底（前序探针段留下的时间/GPS/剥除规则不参与本段）+ 跟随项回位
            // （生效值才有主钉）+ amap 基准面（GCJ-02 → 回填断言才有区分力）
            page.apply_rules(pp::BatchRules{});
            page.set_batch_files(QStringList{fixture});
            page.set_selection(QList<MetaSelectionItem>{meta_probe_item(fixture)},
                               /*from_selection=*/true);
            wait_probe();
            pump(80);
            combo->setCurrentIndex(combo->findData(QStringLiteral("amap")));
            map->set_offline(true);
            pump(80);

            // M7R21F4-0：lat/lon/distance_m 三同型 double 易混（tidy 基线内告警）——
            // 按「同型形参对 → 参数组结构化」收拢成命名组 GeoRow。
            struct GeoRow {
                double lat = 0.0;
                double lon = 0.0;
                double distance_m = -1.0;
            };
            const auto make_result = [](const QString &title, const QString &address,
                                        const GeoRow &pos) {
                pp::map::GeocodeResult result;
                result.title = title;
                result.address = address;
                result.lat = pos.lat;
                result.lon = pos.lon;
                result.distance_m = pos.distance_m;
                return result;
            };
            const std::vector<pp::map::GeocodeResult> results = {
                make_result(QStringLiteral("探针结果甲"), QStringLiteral("北京市东城区"),
                            {39.9087, 116.3975, -1.0}),
                make_result(QStringLiteral("探针结果乙"), QStringLiteral("上海市黄浦区"),
                            {31.2304, 121.4737, 250.0}),
                make_result(QStringLiteral("探针结果丙"), QStringLiteral("深圳市福田区"),
                            {22.5431, 114.0579, -1.0}),
            };
            std::vector<pp::map::MapWidget::Pin> pins;
            pins.reserve(results.size());
            for (std::size_t i = 0; i < results.size(); ++i) {
                pins.push_back(pp::map::MapWidget::Pin{results[i].lat, results[i].lon,
                                                       results[i].title, int(i)});
            }
            map->set_result_pins(pins);
            map->search_results_ready(results, QString());
            pump(80);

            // ① 搜索 → 多钉 + 结果列表（每行 = 名称/地址/距离，行件非空 = 可点列表而非占位行）
            loop_expect(
                "results-list",
                list->count() == int(results.size()) &&
                    (list->item(0) != nullptr && list->itemWidget(list->item(0)) != nullptr) &&
                    meta::ProbeContext(&page).gps_results() == int(results.size()),
                QStringLiteral("rows=%1 hook=%2")
                    .arg(list->count())
                    .arg(meta::ProbeContext(&page).gps_results()));
            loop_expect("results-status",
                        status_label->text() ==
                            QStringLiteral("搜索到 %1 个结果").arg(results.size()),
                        status_label->text());

            // 鼠标动作（与用户输入同路：press/release 事件直投地图/视口）
            int pin_hits = 0;
            int pin_index = -1;
            int points = 0;
            const QMetaObject::Connection pin_conn = QObject::connect(
                map, &pp::map::MapWidget::pin_clicked, w, [&pin_hits, &pin_index](int index) {
                    ++pin_hits;
                    pin_index = index;
                });
            const QMetaObject::Connection point_conn =
                QObject::connect(map, &pp::map::MapWidget::point_selected, w,
                                 [&points](double, double) { ++points; });
            const auto click_at = [](QWidget *target, const QPoint &pos) {
                QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), target->mapToGlobal(pos),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(target, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos),
                                    target->mapToGlobal(pos), Qt::LeftButton, Qt::NoButton,
                                    Qt::NoModifier);
                QApplication::sendEvent(target, &release);
            };

            // ② 点图钉（第 3 条：视口中心 = 钉位，绘制与命中同一坐标管线）→ 主钉 + 回填 WGS-84
            map->center_on(results[2].lat, results[2].lon, 14);
            pump(80);
            const QPoint view_center(map->width() / 2, map->height() / 2);
            click_at(map, view_center);
            pump(120);
            const std::pair<double, double> gcj =
                pp::map::wgs84_to_gcj02(results[2].lat, results[2].lon);
            loop_expect("pin-hit", pin_hits == 1 && pin_index == 2,
                        QStringLiteral("hits=%1 idx=%2").arg(pin_hits).arg(pin_index));
            loop_expect("pin-fill-wgs84",
                        lat_edit->text() == QString::number(results[2].lat, 'f', 6) &&
                            lon_edit->text() == QString::number(results[2].lon, 'f', 6) &&
                            (std::fabs(lat_edit->text().toDouble() - gcj.first) > 1e-4 ||
                             std::fabs(lon_edit->text().toDouble() - gcj.second) > 1e-4),
                        QStringLiteral("box=%1,%2 gcj=%3,%4")
                            .arg(lat_edit->text(), lon_edit->text())
                            .arg(gcj.first, 0, 'f', 6)
                            .arg(gcj.second, 0, 'f', 6));
            loop_expect("pin-marker",
                        map->has_marker() && std::fabs(map->marker_lat() - results[2].lat) < 1e-6 &&
                            std::fabs(map->marker_lon() - results[2].lon) < 1e-6,
                        QStringLiteral("marker=%1,%2")
                            .arg(map->marker_lat(), 0, 'f', 6)
                            .arg(map->marker_lon(), 0, 'f', 6));
            loop_expect("pin-list-follow",
                        list->currentRow() == 2 &&
                            status_label->text() ==
                                QStringLiteral("已选结果：%1").arg(results[2].title),
                        QStringLiteral("row=%1 status=%2")
                            .arg(list->currentRow())
                            .arg(status_label->text()));
            loop_expect("pin-reverse-offline",
                        meta::ProbeContext(&page).gps_address() ==
                            QStringLiteral("离线模式：逆地理不可用"),
                        meta::ProbeContext(&page).gps_address());

            // ③ 地图微调（自由取点）→ 回填 + 主钉随之 + 状态
            click_at(map, view_center + QPoint(-60, 40));
            pump(120);
            loop_expect("tune",
                        points == 1 &&
                            std::fabs(lat_edit->text().toDouble() - map->marker_lat()) < 1e-6 &&
                            std::fabs(lon_edit->text().toDouble() - map->marker_lon()) < 1e-6 &&
                            status_label->text() == QStringLiteral("已从地图选择坐标"),
                        QStringLiteral("points=%1 box=%2,%3")
                            .arg(points)
                            .arg(lat_edit->text(), lon_edit->text()));

            // ④ 「地图选点」= 视口中心（先拖拽平移，再取中心）
            {
                const QPoint drag_to = view_center + QPoint(50, -30);
                QMouseEvent press(QEvent::MouseButtonPress, QPointF(view_center),
                                  map->mapToGlobal(view_center), Qt::LeftButton, Qt::LeftButton,
                                  Qt::NoModifier);
                QApplication::sendEvent(map, &press);
                QMouseEvent move(QEvent::MouseMove, QPointF(drag_to), map->mapToGlobal(drag_to),
                                 Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(map, &move);
                QMouseEvent release(QEvent::MouseButtonRelease, QPointF(drag_to),
                                    map->mapToGlobal(drag_to), Qt::LeftButton, Qt::NoButton,
                                    Qt::NoModifier);
                QApplication::sendEvent(map, &release);
                pump(80);
                pick->click();
                pump(120);
                loop_expect("center-pick",
                            std::fabs(lat_edit->text().toDouble() - map->center_lat()) < 2e-6 &&
                                std::fabs(lon_edit->text().toDouble() - map->center_lon()) < 2e-6 &&
                                status_label->text() == QStringLiteral("已取地图中心为坐标"),
                            QStringLiteral("box=%1,%2 center=%3,%4")
                                .arg(lat_edit->text(), lon_edit->text())
                                .arg(map->center_lat(), 0, 'f', 6)
                                .arg(map->center_lon(), 0, 'f', 6));
            }

            // ⑤ 换地图源 = 换地理编码源 → 列表与结果钉一并收干净（不得留"点不动"的旧钉）
            {
                const int pins_before = pin_hits;
                const int points_before = points;
                combo->setCurrentIndex(combo->findData(QStringLiteral("osm")));
                pump(120);
                loop_expect(
                    "switch-clears-list",
                    meta::ProbeContext(&page).gps_results() == 0 && list->count() == 1 &&
                        (list->item(0) != nullptr && list->itemWidget(list->item(0)) == nullptr),
                    QStringLiteral("hook=%1 rows=%2")
                        .arg(meta::ProbeContext(&page).gps_results())
                        .arg(list->count()));
                map->center_on(results[2].lat, results[2].lon, 14);
                pump(80);
                click_at(map, view_center);
                pump(120);
                loop_expect("switch-clears-pins",
                            pin_hits == pins_before && points == points_before + 1,
                            QStringLiteral("pins=%1→%2 points=%3")
                                .arg(pins_before)
                                .arg(pin_hits)
                                .arg(points));
            }
            QObject::disconnect(pin_conn);
            QObject::disconnect(point_conn);
        }
        std::printf("UI-SMOKE meta-loop: checked=%d bad=%d %s\n", loop_checked,
                    int(loop_bad.size()), qUtf8Printable(loop_bad.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (!loop_bad.isEmpty()) {
            smoke_fail(
                MainWindow::tr("选点闭环（R3/§4.4）不符：%1").arg(loop_bad.join(QLatin1Char(' '))));
        }
    }
}
// R-255：smoke_probe_meta 分节 sec_k（自原函数逐字搬移；断言/输出行不变）
void MainWindow::Impl::probe_meta_sec_k(const MetaProbeCtx &c, const QString &shots_dir) {
    // R-255：共享前置局部别名（ctx 承接；节体逐字不变）
    PageMeta &page = *c.page;
    const QString &repo = c.repo;
    const QString &fixture = c.fixture;
    const auto &label = c.label;
    const auto &full = c.full;
    const auto &edit_text = c.edit_text;
    const auto &placeholder = c.placeholder;
    const auto &wait_probe = c.wait_probe;
    const auto &shown_form = c.shown_form;
    const auto &write_side_time = c.write_side_time;
    const double pick_lat = c.pick_lat;
    const double pick_lon = c.pick_lon;
    const auto &gps_badge = c.gps_badge;
    const auto &gps_badge_visible = c.gps_badge_visible;
    // ---- (k) M6-T13 大地图（§7-T13 R5）：覆盖层「展开」→ 模态对话框内是**同一只** MapWidget
    //   （重挂载：图层/图钉/视口/在途请求随控件走，零同步）→ 底栏「关闭」（真实用户路径）→
    //   挂回卡片并复位固定高（几何逐位复原、objectName 不变）。探针全程离线（(j) 段已
    //   set_offline(true)，大视口不发请求）。
    //   模态取证：点「展开」会进入 exec() 的嵌套事件循环 —— 预置 0ms 定时器在循环内完成取证
    //   并关窗（否则探针被模态阻塞）；两条路径都关窗（底栏关闭钮缺失 → reject 兜底）。
    {
        int expand_checked = 0;
        QStringList expand_bad;
        const auto expand_expect = [&expand_checked, &expand_bad](const char *name, bool ok,
                                                                  const QString &detail) {
            ++expand_checked;
            if (!ok) {
                expand_bad << QStringLiteral("%1(%2)").arg(QString::fromLatin1(name), detail);
            }
        };
        auto *expand_btn = page.findChild<QToolButton *>(meta::objname::kMapExpand);
        auto *map = page.findChild<pp::map::MapWidget *>(meta::objname::kMap);
        const bool expand_controls_ok =
            expand_btn != nullptr && map != nullptr && expand_btn->parentWidget() == map;
        expand_expect(
            "expand-button",
            expand_controls_ok && expand_btn->width() == 24 && expand_btn->height() == 24 &&
                expand_btn->text().isEmpty() && !expand_btn->icon().isNull() &&
                expand_btn->property(icons::kIconNameProperty).toString() ==
                    QStringLiteral("zoom-fit") &&
                !expand_btn->toolTip().isEmpty() && !expand_btn->accessibleName().isEmpty() &&
                expand_btn->isVisible(),
            QStringLiteral("btn=%1 icon=%2 tip=\"%3\"")
                .arg(
                    expand_controls_ok
                        ? QStringLiteral("%1x%2").arg(expand_btn->width()).arg(expand_btn->height())
                        : QStringLiteral("null"))
                .arg(expand_controls_ok ? expand_btn->property(icons::kIconNameProperty).toString()
                                        : QString())
                .arg(expand_controls_ok ? expand_btn->toolTip() : QString()));
        if (!expand_controls_ok) {
            expand_expect("dialog-modal", false, QStringLiteral("控件缺失"));
        } else {
            // 覆盖层位置：右上（不与右下缩放钮 / 左下比例尺抢位）
            const QRect btn_box = expand_btn->geometry();
            expand_expect("expand-position",
                          btn_box.right() <= map->width() - 2 && btn_box.top() <= 8,
                          QStringLiteral("btn=%1,%2 %3x%4 map=%5x%6")
                              .arg(btn_box.x())
                              .arg(btn_box.y())
                              .arg(btn_box.width())
                              .arg(btn_box.height())
                              .arg(map->width())
                              .arg(map->height()));
            // 基线：视口/主钉/图层设成可区分档 —— 展开后必须逐位保持（零同步证据）
            map->center_on(31.230400, 121.473700, 14);
            map->set_marker(31.230400, 121.473700);
            map->set_tile_style(pp::map::TileStyle::Hybrid);
            map->set_sat_labels(false);
            pump(80);
            const QRect card_geom = map->geometry();
            const QPoint card_pos = map->mapTo(&page, QPoint(0, 0));
            const double card_lat = map->center_lat();
            const double card_lon = map->center_lon();
            const double card_mlat = map->marker_lat();
            const double card_mlon = map->marker_lon();

            struct ExpandCycle {
                bool modal = false, title_ok = false, close_btn = false, caption_close = false;
                bool same_control = false, state_kept = false, painted = false;
                bool card_kept = false, placeholder_removed = false;
                QSize dialog_size;
                QSize map_size;
                // M6-T16：实拍诊断（失败时随详情打印；dpr≠1 平台归因用）
                qreal paint_dpr = 1.0;
                QSize paint_shot;
                int paint_near = 0;
            };
            const auto find_map_dialog = []() -> QDialog * {
                if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                    if (modal->objectName() == meta::objname::kMapDialog) {
                        return modal;
                    }
                }
                const QWidgetList tops = QApplication::topLevelWidgets();
                for (QWidget *top : tops) {
                    if (top != nullptr && top->objectName() == meta::objname::kMapDialog) {
                        return qobject_cast<QDialog *>(top);
                    }
                }
                return nullptr;
            };
            // 一次展开/关闭往返：0ms 定时器在 exec() 的嵌套循环内取证 → 关窗 → exec 返回
            const auto run_expand_cycle = [this, &page, expand_btn, map, &find_map_dialog,
                                           &card_lat, &card_lon, &card_mlat,
                                           &card_mlon](ExpandCycle *out, const QSize &resize_to) {
                QTimer::singleShot(
                    0, &page,
                    [out, map, &page, &find_map_dialog, &card_lat, &card_lon, &card_mlat,
                     &card_mlon, resize_to] {
                        QDialog *dialog = find_map_dialog();
                        if (dialog == nullptr) {
                            return; // out->modal 保持 false（= 取证失败，由调用侧记 bad）
                        }
                        out->modal = dialog->isVisible() && dialog->isModal();
                        out->title_ok = dialog->windowTitle() == QStringLiteral("地图");
                        out->close_btn =
                            dialog->findChild<QPushButton *>(meta::objname::kMapClose) != nullptr;
                        out->caption_close = dialog->findChild<QPushButton *>(
                                                 QStringLiteral("pp-dlg-close")) != nullptr;
                        auto *inside = dialog->findChild<pp::map::MapWidget *>(meta::objname::kMap);
                        out->same_control = inside != nullptr && inside == map;
                        if (inside != nullptr) {
                            out->map_size = inside->size();
                            out->state_kept = std::fabs(inside->center_lat() - card_lat) < 1e-9 &&
                                              std::fabs(inside->center_lon() - card_lon) < 1e-9 &&
                                              inside->has_marker() &&
                                              std::fabs(inside->marker_lat() - card_mlat) < 1e-9 &&
                                              std::fabs(inside->marker_lon() - card_mlon) < 1e-9 &&
                                              inside->tile_style() == pp::map::TileStyle::Hybrid;
                            const QImage shot = inside->grab().toImage();
                            // 实拍：大图非空且尺寸 = 控件尺寸；左上 96×26 内近白像素 ≥ 20 =
                            // 覆盖层（白底 chip/钮）随控件一同进对话框（子件随 reparent 走）。
                            // M6-T16 修（T15 提出、T16 裁定）：grab() 的图是**设备像素**
                            // （= 逻辑 × dpr），原判据拿设备尺寸比逻辑尺寸 —— dpr≠1 平台
                            // （200% 缩放）恒假。现按 dpr 换算两侧：尺寸比设备像素，ROI 的
                            // 96×26 保持**逻辑像素**口径（dpr=1 时与旧判据逐位等价）。
                            const qreal shot_dpr = shot.isNull() || shot.devicePixelRatio() <= 0.0
                                                       ? 1.0
                                                       : shot.devicePixelRatio();
                            int near_white = 0;
                            if (!shot.isNull()) {
                                const int roi_w =
                                    int(std::min(qreal(shot.width()), 96.0 * shot_dpr));
                                const int roi_h =
                                    int(std::min(qreal(shot.height()), 26.0 * shot_dpr));
                                for (int y = 0; y < roi_h; ++y) {
                                    for (int x = 0; x < roi_w; ++x) {
                                        const QColor px = shot.pixelColor(x, y);
                                        if (px.red() >= 244 && px.green() >= 244 &&
                                            px.blue() >= 244) {
                                            ++near_white;
                                        }
                                    }
                                }
                            }
                            const QSize shot_expected(
                                int(std::lround(inside->width() * shot_dpr)),
                                int(std::lround(inside->height() * shot_dpr)));
                            out->paint_dpr = shot_dpr;
                            out->paint_shot = shot.size();
                            out->paint_near = near_white;
                            out->painted =
                                !shot.isNull() && shot.size() == shot_expected && near_white >= 20;
                        }
                        if (resize_to.isValid()) {
                            dialog->resize(resize_to); // 模拟用户拉伸（尺寸记忆的取证前提）
                        }
                        out->dialog_size = dialog->size();
                        // 展开期卡片位 = 同高空占位（模态期卡片不抖动）
                        const auto *placeholder = page.findChild<QWidget *>(
                            QString::fromLatin1(meta::kMapPlaceholderName));
                        out->card_kept =
                            placeholder != nullptr &&
                            placeholder->height() == 260; // UX-4⑤ 换锚（kMapHeight 136→260）
                        if (auto *close =
                                dialog->findChild<QPushButton *>(meta::objname::kMapClose)) {
                            close->click(); // 真实用户路径：底栏「关闭」
                        } else {
                            dialog->reject(); // 兜底：绝不让探针卡在模态循环里
                        }
                    });
                expand_btn->click(); // 同步：内部 exec() 直到对话框关闭才返回
                pump(80);
                out->placeholder_removed =
                    page.findChild<QWidget *>(QString::fromLatin1(meta::kMapPlaceholderName)) ==
                    nullptr;
            };

            ExpandCycle first;
            run_expand_cycle(&first, QSize(900, 720));
            expand_expect("dialog-modal", first.modal,
                          QStringLiteral("modal=%1").arg(first.modal ? 1 : 0));
            expand_expect("dialog-chrome", first.title_ok && first.close_btn && first.caption_close,
                          QStringLiteral("title=%1 close=%2 caption=%3")
                              .arg(first.title_ok ? 1 : 0)
                              .arg(first.close_btn ? 1 : 0)
                              .arg(first.caption_close ? 1 : 0));
            expand_expect("dialog-same-control", first.same_control,
                          QStringLiteral("same=%1").arg(first.same_control ? 1 : 0));
            expand_expect("dialog-map-size",
                          first.map_size.width() >= 640 && first.map_size.height() >= 480,
                          QStringLiteral("map=%1x%2(期望 ≥640x480) dialog=%3x%4")
                              .arg(first.map_size.width())
                              .arg(first.map_size.height())
                              .arg(first.dialog_size.width())
                              .arg(first.dialog_size.height()));
            expand_expect("dialog-state-kept", first.state_kept && first.painted,
                          QStringLiteral("state=%1 painted=%2 shot=%3x%4 dpr=%5 near=%6")
                              .arg(first.state_kept ? 1 : 0)
                              .arg(first.painted ? 1 : 0)
                              .arg(first.paint_shot.width())
                              .arg(first.paint_shot.height())
                              .arg(first.paint_dpr)
                              .arg(first.paint_near));
            expand_expect("dialog-card-kept", first.card_kept && first.placeholder_removed,
                          QStringLiteral("占位高 260=%1 关闭后已移除=%2") // UX-4⑤ 换锚
                              .arg(first.card_kept ? 1 : 0)
                              .arg(first.placeholder_removed ? 1 : 0));
            // 关闭后：挂回卡片（同实例 + 同宿主 + 高度 ≥260 + 几何/落点逐位复原 + 状态保持）
            // UX-4⑤ 换锚：kMapHeight 136 → 260（ux-review §16-2 定档）
            // M7-R2-B 换锚（主对话授权）：`map->height() == 260` → `>= 260` —— UX-4 stretch 后地图
            // 260px 起步、随富余空间拉伸（smoke 视口 587×900 下实测 274）；字面量 ==260 钉的是旧
            // 固定高实现，布局真不变量 = geometry 逐位复原（下一行，逐字保留）。
            expand_expect("restored-card",
                          page.isAncestorOf(map) && map->isVisible() && map->height() >= 260 &&
                              map->geometry() == card_geom &&
                              map->mapTo(&page, QPoint(0, 0)) == card_pos,
                          QStringLiteral("h=%1 vis=%2 geom=(%3,%4 %5x%6) 基线=(%7,%8 %9x%10)")
                              .arg(map->height())
                              .arg(map->isVisible() ? 1 : 0)
                              .arg(map->geometry().x())
                              .arg(map->geometry().y())
                              .arg(map->geometry().width())
                              .arg(map->geometry().height())
                              .arg(card_geom.x())
                              .arg(card_geom.y())
                              .arg(card_geom.width())
                              .arg(card_geom.height()));
            expand_expect("restored-state",
                          std::fabs(map->center_lat() - card_lat) < 1e-9 &&
                              std::fabs(map->center_lon() - card_lon) < 1e-9 && map->has_marker() &&
                              std::fabs(map->marker_lat() - card_mlat) < 1e-9 &&
                              map->tile_style() == pp::map::TileStyle::Hybrid,
                          QStringLiteral("center=%1,%2 marker=%3,%4")
                              .arg(map->center_lat(), 0, 'f', 6)
                              .arg(map->center_lon(), 0, 'f', 6)
                              .arg(map->marker_lat(), 0, 'f', 6)
                              .arg(map->marker_lon(), 0, 'f', 6));
            expand_expect("restored-objectname",
                          map->objectName() == meta::objname::kMap &&
                              page.findChild<pp::map::MapWidget *>(meta::objname::kMap) == map &&
                              expand_btn->isVisible() && expand_btn->parentWidget() == map,
                          QStringLiteral("name=\"%1\" expand-vis=%2")
                              .arg(map->objectName())
                              .arg(expand_btn->isVisible() ? 1 : 0));
            // 第二次展开：可重复 + 尺寸记忆（沿用上次尺寸）+ 大视图随对话框尺寸
            ExpandCycle second;
            run_expand_cycle(&second, QSize());
            expand_expect("dialog-reopen",
                          second.modal && second.same_control && second.state_kept &&
                              second.map_size.width() >= 640 && second.map_size.height() >= 480,
                          QStringLiteral("modal=%1 same=%2 map=%3x%4")
                              .arg(second.modal ? 1 : 0)
                              .arg(second.same_control ? 1 : 0)
                              .arg(second.map_size.width())
                              .arg(second.map_size.height()));
            expand_expect("dialog-size-memory",
                          second.dialog_size == first.dialog_size &&
                              second.dialog_size == QSize(900, 720) &&
                              second.map_size.width() > first.map_size.width(),
                          QStringLiteral("1st=%1x%2 2nd=%3x%4")
                              .arg(first.dialog_size.width())
                              .arg(first.dialog_size.height())
                              .arg(second.dialog_size.width())
                              .arg(second.dialog_size.height()));
            expand_expect("restored-card-2",
                          page.isAncestorOf(map) &&
                              map->height() >= 260 && // UX-4⑤ 换锚
                                                      // M7-R2-B：==260 → ≥260（stretch 后
                                                      // 260 是下限；geometry 逐位不变）
                              map->geometry() == card_geom && map->isVisible(),
                          QStringLiteral("h=%1 geom=(%2,%3 %4x%5)")
                              .arg(map->height())
                              .arg(map->geometry().x())
                              .arg(map->geometry().y())
                              .arg(map->geometry().width())
                              .arg(map->geometry().height()));
        }
        std::printf("UI-SMOKE meta-map-expand: checked=%d bad=%d %s\n", expand_checked,
                    int(expand_bad.size()), qUtf8Printable(expand_bad.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (!expand_bad.isEmpty()) {
            smoke_fail(MainWindow::tr("大地图弹窗（§7-T13 R5）不符：%1")
                           .arg(expand_bad.join(QLatin1Char(' '))));
        }
    }
}

void MainWindow::Impl::smoke_probe_meta(const QString &shots_dir) {
    const QString repo = repo_root();
    const QString fixture =
        repo.isEmpty() ? QString() : repo + QStringLiteral("/tests/golden/meta/exif_full.jpg");
    if (fixture.isEmpty() || !QFileInfo::exists(fixture)) {
        smoke_fail(MainWindow::tr("元数据页自检：金样 fixture 缺失（%1）").arg(fixture));
        return;
    }
    PageMeta page;
    page.set_tokens(tokens);
    page.resize(theme::Metrics::right_width, theme::Metrics::calibrated_h);
    page.show();
    pump(150);
    if (auto *map = page.findChild<pp::map::MapWidget *>()) {
        map->set_offline(true); // 与本窗口 set_offline_maps(true) 同口径（探针不发请求）
    }
    const auto label = [&page](const QString &name) {
        const auto *widget = page.findChild<QLabel *>(name);
        return widget != nullptr ? widget->text() : QString();
    };
    // 语义值：ElidedLabel 把全文挂在 ppFullText 上（显示文本按宽度省略，见 page_meta.cpp）
    const auto full = [&page](const QString &name) {
        const auto *widget = page.findChild<QLabel *>(name);
        if (widget == nullptr) {
            return QString();
        }
        const QVariant value = widget->property("ppFullText");
        return value.isValid() ? value.toString() : widget->text();
    };
    const auto edit_text = [&page](const QString &name) {
        const auto *widget = page.findChild<QLineEdit *>(name);
        return widget != nullptr ? widget->text() : QString();
    };
    const auto placeholder = [&page](const QString &name) {
        const auto *widget = page.findChild<QLineEdit *>(name);
        return widget != nullptr ? widget->placeholderText() : QString();
    };
    const auto wait_probe = [this, &page] {
        return wait_for([&page] { return !meta::ProbeContext(&page).follow_pending(); }, 10000);
    };
    // "YYYY:MM:DD hh:mm:ss" → "YYYY-MM-DD hh:mm:ss"（页面的显示口径；与 page_meta.cpp 同式）
    const auto shown_form = [](QString text) {
        if (text.size() >= 10) {
            QString date = text.left(10);
            date.replace(QLatin1Char(':'), QLatin1Char('-'));
            text = date + text.mid(10);
        }
        return text;
    };
    const auto write_side_time = [&fixture](const pp::BatchRules &rules) {
        const pp::SourceMeta src = pp::read_metadata(std::filesystem::path(fixture.toStdString()));
        const pp::MetadataPlan plan = pp::build_plan(src, rules, std::nullopt);
        return QString::fromStdString(pp::effective_datetime(plan.exif, plan.xmp));
    };

    page.set_batch_files(QStringList{fixture});
    page.set_selection(QList<MetaSelectionItem>{meta_probe_item(fixture)}, /*from_selection=*/true);
    if (!wait_probe()) {
        smoke_fail(MainWindow::tr("元数据页自检：跟随项 probe 未在 10s 内回填"));
        return;
    }
    pump(80);

    // R-255：跨节共享定义（原散在 (d)/(e) 间）——随 ctx 分发给各分节
    const double pick_lat = 22.5431;
    const double pick_lon = 114.0579;
    // 卡头 GPS 状态胶囊（M5-T7/修 1-33：状态 = 独立徽标，不再是 placeholder 文案）
    const auto gps_badge = [&page] {
        const auto *label = page.findChild<QLabel *>(meta::objname::kGpsPill);
        return label != nullptr ? label->text() : QString();
    };
    const auto gps_badge_visible = [&page] {
        const auto *label = page.findChild<QLabel *>(meta::objname::kGpsPill);
        return label != nullptr && label->isVisible();
    };
    // R-255：共享前置入 ctx；分节调用顺序与原行序一致（输出逐字不变）
    MetaProbeCtx c;
    c.page = &page;
    c.repo = repo;
    c.fixture = fixture;
    c.label = label;
    c.full = full;
    c.edit_text = edit_text;
    c.placeholder = placeholder;
    c.wait_probe = wait_probe;
    c.shown_form = shown_form;
    c.write_side_time = write_side_time;
    c.pick_lat = pick_lat;
    c.pick_lon = pick_lon;
    c.gps_badge = gps_badge;
    c.gps_badge_visible = gps_badge_visible;
    probe_meta_sec_a(c, shots_dir);
    probe_meta_sec_b(c, shots_dir);
    probe_meta_sec_c(c, shots_dir);
    probe_meta_sec_d(c, shots_dir);
    probe_meta_sec_e(c, shots_dir);
    probe_meta_sec_f(c, shots_dir);
    probe_meta_sec_g(c, shots_dir);
    probe_meta_sec_g2(c, shots_dir);
    probe_meta_sec_h(c, shots_dir);
    probe_meta_sec_i(c, shots_dir);
    probe_meta_sec_j(c, shots_dir);
    probe_meta_sec_k(c, shots_dir);

    page.hide();
    pump(80);
}

} // namespace pp::ui

#else // !PP_BUILD_DEV

namespace pp::ui {
// 发布构建：冻结头声明的走查入口留空壳（无调用方 —— main.cpp 的 run_ui_smoke 同样被
// PP_BUILD_DEV 门控），探针面零字节进入发布二进制（R-048）。
void MainWindow::ui_smoke_walk(const QString &shots_dir) { Q_UNUSED(shots_dir); }
} // namespace pp::ui

#endif // PP_BUILD_DEV
