// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — main window (M1b-U10 integration).
//
// 冻结头（§2.14）只给出公开 API + 私有 build_ui/wire/lock_for_run/refresh_status/save_session
// + `struct Impl`，因此全部控件与状态承载在本 TU 的 MainWindow::Impl 内。两个无法用覆写实现的
// 行为（§2.14 要求"窗口整体 acceptDrops，dropAccept → add_paths" 与 closeEvent 保存会话）
// 通过装在窗口上的事件过滤器完成——头文件逐字节冻结，不添加虚函数覆写。
//
// 接线口径全部来自 W-B 落地口径（§2.8/§2.9/§2.10/§2.11/§2.12/§2.13）与 §4.3 接线备忘：
//   * FileListModel::set_thumbnailer 内部已 connect(ready→apply_thumb) 并自动 enqueue →
//   不重复连接；
//   * PageOutput 的格式/参数/位深/冲突状态只经冻结 API（config_base/collect_preset/apply_preset）；
//   * PageMeta 的 objectName 契约（gps_map 等）用于 set_offline_maps 与冒烟断言；
//   * ExifEditor::result() 遮蔽 QDialog::result() → 判 accept 必须写 dlg.QDialog::result()。

#include "ui/mainwindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/logger.h"
#include "mapwidget/mapwidget.h"
#include "platform/paths.h"
#include "ui/exif_editor.h"
#include "ui/filelistmodel.h"
#include "ui/page_meta.h"
#include "ui/page_output.h"
#include "ui/page_run.h"
#include "ui/paramform.h"
#include "ui/preset_io.h"
#include "ui/presets_dialog.h"
#include "ui/settings_dialog.h"
#include "ui/thumbnails.h"

namespace pp::ui {
namespace {

constexpr int kPollIntervalMs = 150; // §2.14：运行轮询间隔
constexpr int kThumbWaitMs = 10000;  // §4.3：缩略图队列空上限
constexpr int kRunWaitMs = 120000;   // U10 冻结：冒烟实跑上限
constexpr int kSmokeShotCount = 8;
constexpr qint64 kMinShotBytes = 10 * 1024;

// 冒烟截图清单（顺序与名称逐字节固定 = §4.3 冻结 8 张；2026-09-20 勘误以任务书为准）
const char *const kSmokeShots[kSmokeShotCount] = {
    "01-meta.png",      "02-output.png",   "02b-output-avif.png", "03-run.png",
    "03b-run-done.png", "04-settings.png", "05-exif-editor.png",  "06-presets.png",
};
constexpr int kShotMeta = 0;
constexpr int kShotOutput = 1;
constexpr int kShotAvif = 2;
constexpr int kShotRun = 3;
constexpr int kShotRunDone = 4;
constexpr int kShotSettings = 5;
constexpr int kShotExif = 6;
constexpr int kShotPresets = 7;

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
qint64 min_shot_bytes(int index) { return index >= kShotSettings ? 2 * 1024 : kMinShotBytes; }

QString T(const char *s) { return QCoreApplication::translate("MainWindow", s); }

bool parse_log_level_text(const std::string &s, pp::LogLevel &out) {
    if (s == "trace") {
        out = pp::LogLevel::Trace;
        return true;
    }
    if (s == "debug") {
        out = pp::LogLevel::Debug;
        return true;
    }
    if (s == "info") {
        out = pp::LogLevel::Info;
        return true;
    }
    if (s == "warn") {
        out = pp::LogLevel::Warn;
        return true;
    }
    if (s == "error") {
        out = pp::LogLevel::Error;
        return true;
    }
    return false;
}

QString format_label(const QString &id) {
    const pp::FormatDef *f = pp::find_format(id.toStdString());
    return f != nullptr ? QString::fromStdString(f->label) : id;
}

QString conflict_label(pp::ConflictPolicy p) {
    switch (p) {
    case pp::ConflictPolicy::Rename:
        return T("自动加序号");
    case pp::ConflictPolicy::Skip:
        return T("跳过");
    case pp::ConflictPolicy::Overwrite:
        return T("覆盖");
    }
    return T("自动加序号");
}

QString path_text(const std::filesystem::path &p) {
    return QFileInfo(QString::fromStdString(p.string())).fileName();
}

bool is_metadata_only_format(const QString &suffix) {
    static const QStringList ok{QStringLiteral("jpg"),  QStringLiteral("jpeg"),
                                QStringLiteral("png"),  QStringLiteral("tif"),
                                QStringLiteral("tiff"), QStringLiteral("webp")};
    return ok.contains(suffix);
}

} // namespace

// ---------------------------------------------------------------------------
// Impl：控件树 + 运行状态 + 冒烟断言
// ---------------------------------------------------------------------------

struct MainWindow::Impl {
    MainWindow *w = nullptr;
    pp::AppSettings settings;

    // ---- 左：文件面板 ----
    QWidget *panel = nullptr;
    FileListModel *model = nullptr;
    Thumbnailer *thumbs = nullptr;
    QSortFilterProxyModel *proxy = nullptr;
    QListView *view = nullptr;
    QLabel *file_count = nullptr;
    QLabel *unsupported = nullptr;
    QLineEdit *search = nullptr;
    QPushButton *add_files = nullptr;
    QPushButton *add_dir = nullptr;
    QPushButton *remove_sel = nullptr;
    QPushButton *clear_all = nullptr;

    // ---- 顶栏 / 页面 ----
    QAction *nav_meta = nullptr;
    QAction *nav_output = nullptr;
    QAction *nav_run = nullptr;
    QAction *settings_action = nullptr;
    QActionGroup *nav_group = nullptr;
    QStackedWidget *stack = nullptr;
    PageMeta *page_meta = nullptr;
    PageOutput *page_output = nullptr;
    PageRun *page_run = nullptr;

    // ---- 底栏 ----
    QLabel *status = nullptr;
    QPushButton *start = nullptr;

    // ---- 运行状态 ----
    std::unique_ptr<pp::Scheduler> sched;
    QTimer *poll = nullptr;
    bool running = false;
    // 完成检测（§2.14 的"轮询 !running()"机制不可用：pp::Scheduler::running() 只在 wait() 内清零，
    // 见报告 api-deltas）→ 轮询改为"终态事件计数 == 本批文件数"，语义等价且不阻塞 GUI。
    std::size_t run_total = 0;
    std::size_t run_terminal = 0;
    std::size_t run_done = 0;
    std::size_t run_failed = 0;
    std::size_t run_skipped = 0;
    QString last_preset_path;
    QStringList cached_paths; // 行集合签名（避免每次状态变化都重扫时间预览）
    bool cached_alpha = false;

    // ---- 拖放 / closeEvent 事件过滤器（冻结头不允许覆写虚函数）----
    struct Filter : QObject {
        Impl *impl = nullptr;
        explicit Filter(Impl *i) : QObject(i->w), impl(i) {}
        bool eventFilter(QObject *watched, QEvent *event) override {
            return impl->handle_event(watched, event);
        }
    };
    Filter *filter = nullptr;

    // ---- 冒烟 ----
    QStringList smoke_failures;
    int smoke_shots = 0;

    Impl(MainWindow *owner, const pp::AppSettings &s);
    ~Impl();

    bool handle_event(QObject *watched, QEvent *event);

    // 联动 / 工具
    void on_content_changed();
    void sync_selection();
    void sync_exceptions();
    void clear_exception(const QString &path);
    void remove_selected();
    void restore_session();
    void open_logs();
    void handle_file_event(const pp::FileEvent &ev);

    // 冒烟工具
    void pump(int ms);
    bool wait_for(const std::function<bool()> &pred, int timeout_ms);
    void wait_thumbs(int timeout_ms);
    void smoke_fail(const QString &reason);
    void smoke_grab(const QString &dir, const char *name, QWidget *target = nullptr);
    void smoke_run(const QString &shots_dir);
    void smoke_amap_boundary(pp::map::MapWidget *map);
    static QString repo_root();
};

MainWindow::Impl::Impl(MainWindow *owner, const pp::AppSettings &s) : w(owner), settings(s) {
    model = new FileListModel(w);
    thumbs = new Thumbnailer(96, w);
    model->set_thumbnailer(thumbs); // U4 口径：内部已 connect(ready→apply_thumb) + 自动 enqueue

    proxy = new QSortFilterProxyModel(w);
    proxy->setSourceModel(model);
    proxy->setFilterRole(FileListModel::NameRole);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterFixedString(QString());

    poll = new QTimer(w);
    poll->setInterval(kPollIntervalMs);

    filter = new Filter(this);
    w->installEventFilter(filter);
}

MainWindow::Impl::~Impl() {
    if (filter != nullptr) {
        w->removeEventFilter(filter);
        delete filter;
        filter = nullptr;
    }
}

bool MainWindow::Impl::handle_event(QObject * /*watched*/, QEvent *event) {
    switch (event->type()) {
    case QEvent::DragEnter: {
        auto *e = static_cast<QDragEnterEvent *>(event);
        if (e->mimeData() != nullptr && e->mimeData()->hasUrls()) {
            e->acceptProposedAction();
            return true;
        }
        return false;
    }
    case QEvent::Drop: {
        auto *e = static_cast<QDropEvent *>(event);
        if (e->mimeData() == nullptr)
            return false;
        QStringList paths;
        for (const QUrl &url : e->mimeData()->urls()) {
            if (url.isLocalFile())
                paths << url.toLocalFile();
        }
        if (!paths.isEmpty()) {
            w->add_paths(paths);
            e->acceptProposedAction();
            return true;
        }
        return false;
    }
    case QEvent::Close: {
        if (running) {
            const QMessageBox::StandardButton answer =
                QMessageBox::question(w, T("退出"), T("批处理正在运行，确定退出吗？"),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                event->ignore();
                return true;
            }
            if (sched)
                sched->cancel();
        }
        w->save_session();
        return false; // 放行默认关闭路径
    }
    default:
        break;
    }
    return false; // 未处理的事件照常投递（QObject::eventFilter 的默认语义）
}

// ---------------------------------------------------------------------------
// MainWindow：构造 / 布局 / 接线
// ---------------------------------------------------------------------------

MainWindow::MainWindow(const pp::AppSettings &settings, QWidget *parent)
    : QMainWindow(parent), impl_(std::make_unique<Impl>(this, settings)) {
    build_ui();
    wire();
    impl_->restore_session();
    impl_->sync_exceptions();
    impl_->on_content_changed();
    set_current_page(1);
}

MainWindow::~MainWindow() = default;

void MainWindow::build_ui() {
    Impl &d = *impl_;
    setWindowTitle(QStringLiteral("PhotoPipeline"));
    resize(1280, 800);
    setMinimumSize(1024, 680);
    setAcceptDrops(true);

    // ---- 顶部 QToolBar（不可移动）：3 个互斥页签 + 右侧 设置… ----
    QToolBar *bar = addToolBar(tr("主导航"));
    bar->setObjectName(QStringLiteral("pp-toolbar"));
    bar->setMovable(false);
    bar->setFloatable(false);

    d.nav_group = new QActionGroup(this);
    d.nav_group->setExclusive(true);
    d.nav_meta = bar->addAction(tr("① 元数据"));
    d.nav_output = bar->addAction(tr("② 输出"));
    d.nav_run = bar->addAction(tr("③ 运行"));
    for (QAction *action : {d.nav_meta, d.nav_output, d.nav_run}) {
        action->setCheckable(true);
        d.nav_group->addAction(action);
    }
    d.nav_meta->setChecked(true);

    auto *spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    d.settings_action = bar->addAction(tr("设置…"));

    // ---- 中部：QSplitter(左文件面板 | 右 3 页) ----
    auto *central = new QWidget(this);
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(6);

    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false);
    outer->addWidget(splitter, 1);

    d.panel = new QWidget(splitter);
    auto *pv = new QVBoxLayout(d.panel);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(4);

    auto *title_row = new QHBoxLayout();
    auto *title = new QLabel(tr("文件"), d.panel);
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    d.file_count = new QLabel(QStringLiteral("0 个文件"), d.panel);
    d.unsupported = new QLabel(d.panel);
    d.unsupported->setStyleSheet(QStringLiteral("color:#dd8800"));
    d.unsupported->setVisible(false);
    title_row->addWidget(title);
    title_row->addWidget(d.file_count);
    title_row->addStretch(1);
    title_row->addWidget(d.unsupported);
    pv->addLayout(title_row);

    auto *buttons = new QGridLayout();
    buttons->setSpacing(4);
    d.add_files = new QPushButton(tr("添加文件…"), d.panel);
    d.add_dir = new QPushButton(tr("添加文件夹…"), d.panel);
    d.remove_sel = new QPushButton(tr("移除所选"), d.panel);
    d.clear_all = new QPushButton(tr("清空"), d.panel);
    d.remove_sel->setEnabled(false);
    buttons->addWidget(d.add_files, 0, 0);
    buttons->addWidget(d.add_dir, 0, 1);
    buttons->addWidget(d.remove_sel, 1, 0);
    buttons->addWidget(d.clear_all, 1, 1);
    pv->addLayout(buttons);

    d.search = new QLineEdit(d.panel);
    d.search->setObjectName(QStringLiteral("pp-search"));
    d.search->setPlaceholderText(tr("搜索文件名…"));
    d.search->setClearButtonEnabled(true);
    pv->addWidget(d.search);

    d.view = new QListView(d.panel);
    d.view->setObjectName(QStringLiteral("pp-file-view"));
    d.view->setModel(d.proxy);
    d.view->setItemDelegate(new FileDelegate(d.view));
    d.view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d.view->setSelectionBehavior(QAbstractItemView::SelectRows);
    d.view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d.view->setUniformItemSizes(true);
    pv->addWidget(d.view, 1);

    splitter->addWidget(d.panel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({320, 960});

    d.stack = new QStackedWidget(splitter);
    d.stack->setObjectName(QStringLiteral("pp-stack"));
    d.page_meta = new PageMeta(d.stack);
    d.page_output = new PageOutput(d.stack);
    d.page_run = new PageRun(d.stack);
    d.stack->addWidget(d.page_meta);   // 页 1 元数据
    d.stack->addWidget(d.page_output); // 页 2 输出
    d.stack->addWidget(d.page_run);    // 页 3 运行
    splitter->addWidget(d.stack);

    // ---- 底部：状态摘要 + stretch + 开始（运行中禁用；取消只在运行页）----
    auto *bottom = new QHBoxLayout();
    d.status = new QLabel(tr("没有文件"), central);
    d.status->setObjectName(QStringLiteral("pp-status"));
    bottom->addWidget(d.status, 1);
    d.start = new QPushButton(tr("开始"), central);
    d.start->setObjectName(QStringLiteral("pp-start"));
    d.start->setDefault(true);
    bottom->addWidget(d.start);
    outer->addLayout(bottom);

    setCentralWidget(central);
}

void MainWindow::wire() {
    Impl &d = *impl_;

    connect(d.nav_meta, &QAction::triggered, this, [this] { set_current_page(1); });
    connect(d.nav_output, &QAction::triggered, this, [this] { set_current_page(2); });
    connect(d.nav_run, &QAction::triggered, this, [this] { set_current_page(3); });
    connect(d.settings_action, &QAction::triggered, this, &MainWindow::open_settings);

    connect(d.add_files, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, tr("添加文件"), QString(),
            tr("图片 (*.jpg *.jpeg *.png *.tif *.tiff *.webp *.jxl *.heic *.heif *.avif *.bmp "
               "*.gif *.tga);;所有文件 (*)"));
        if (!files.isEmpty())
            add_paths(files);
    });
    connect(d.add_dir, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("添加文件夹"));
        if (!dir.isEmpty())
            add_paths(QStringList{dir});
    });
    connect(d.remove_sel, &QPushButton::clicked, this, [this] { impl_->remove_selected(); });
    connect(d.clear_all, &QPushButton::clicked, this, [this] { impl_->model->clear(); });
    connect(d.search, &QLineEdit::textChanged, this,
            [this](const QString &text) { impl_->proxy->setFilterFixedString(text); });
    connect(d.view, &QListView::doubleClicked, this, [this](const QModelIndex &idx) {
        if (idx.isValid())
            open_exif_editor_row(impl_->proxy->mapToSource(idx).row());
    });
    connect(d.view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { impl_->sync_selection(); });
    // G5（2026-09-20 R1 修订）：底栏开始按钮运行中保持"开始"且禁用 → 取消唯一入口 = 运行页
    connect(d.start, &QPushButton::clicked, this, &MainWindow::on_start);

    connect(d.model, &FileListModel::content_changed, this,
            [this] { impl_->on_content_changed(); });
    connect(d.model, &FileListModel::exception_changed, this,
            [this](std::size_t) { impl_->sync_exceptions(); });
    // 探测完成 → alpha 预检。注意：这不是 U4 的 ready→apply_thumb 连接（那个由模型内部自理），
    // 只是消费方对同一信号只读旁路，不与模型竞争写入。
    connect(d.thumbs, &Thumbnailer::ready, this,
            [this](int, const QString &, const pp::ImageInfo &info, bool ok, const QString &,
                   const QImage &) {
                if (!ok || !info.has_alpha || impl_->running || impl_->cached_alpha)
                    return;
                impl_->cached_alpha = true;
                impl_->page_output->set_batch_has_alpha(true);
            });

    connect(d.poll, &QTimer::timeout, this, [this] {
        if (impl_->sched && impl_->run_total > 0 && impl_->run_terminal >= impl_->run_total) {
            on_scheduler_done();
        }
    });

    connect(d.page_meta, &PageMeta::rules_changed, this, [this] { refresh_status(); });
    connect(d.page_meta, &PageMeta::open_editor_requested, this,
            &MainWindow::open_exif_editor_path);
    connect(d.page_meta, &PageMeta::clear_exception_requested, this,
            [this](const QString &path) { impl_->clear_exception(path); });

    connect(d.page_output, &PageOutput::config_changed, this, [this] { refresh_status(); });
    connect(d.page_output, &PageOutput::manage_presets_requested, this,
            &MainWindow::manage_presets);
    connect(d.page_output, &PageOutput::open_settings_requested, this, &MainWindow::open_settings);

    connect(d.page_run, &PageRun::cancel_requested, this, &MainWindow::on_cancel);
    connect(d.page_run, &PageRun::open_output_requested, this, [](const QString &dir) {
        if (!dir.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(d.page_run, &PageRun::open_logs_requested, this, [this] { impl_->open_logs(); });
}

// ---------------------------------------------------------------------------
// 公开 API
// ---------------------------------------------------------------------------

void MainWindow::add_paths(const QStringList &paths) {
    if (paths.isEmpty())
        return;
    impl_->model->add_paths(paths);
}

void MainWindow::set_current_page(int one_based) {
    Impl &d = *impl_;
    const int index = std::clamp(one_based, 1, 3) - 1;
    d.stack->setCurrentIndex(index);
    QAction *action = index == 0 ? d.nav_meta : (index == 1 ? d.nav_output : d.nav_run);
    if (action != nullptr && !action->isChecked())
        action->setChecked(true);
}

void MainWindow::set_offline_maps(bool off) {
    const QList<pp::map::MapWidget *> maps = findChildren<pp::map::MapWidget *>();
    for (pp::map::MapWidget *map : maps)
        map->set_offline(off);
}

// ---------------------------------------------------------------------------
// 运行：开始 / 取消 / 结束 / G5 锁定
// ---------------------------------------------------------------------------

void MainWindow::on_start() {
    Impl &d = *impl_;
    if (d.running) {
        QMessageBox::warning(this, tr("无法开始"), tr("正在运行"));
        return;
    }
    if (d.model->empty()) {
        QMessageBox::warning(this, tr("无法开始"), tr("没有文件"));
        return;
    }
    const QString reason = d.page_output->ready_to_start();
    if (!reason.isEmpty()) {
        QMessageBox::warning(this, tr("无法开始"), reason);
        return;
    }

    pp::RunConfig cfg = d.page_output->config_base();
    cfg.rules = d.page_meta->rules();
    cfg.workers = d.settings.workers;
    cfg.budget_bytes = static_cast<uint64_t>(d.settings.budget_gb) << 30; // 0 = 自动
    cfg.flatten_gray = d.settings.flatten_gray;
    cfg.rotate_orientation = d.settings.rotate_orientation;

    // M2-T5 §2.7：交叉参数约束的最后闸门。ParamForm 已实时红字提示，这里对真正要下发的
    // 配置再校验一次；非空 → 列出全部消息并阻止开始（不进入 lock_for_run/Scheduler）。
    // 0.3.0 多输出：逐输出求值（T13 起 outputs 可能 >1，UI 现在恒 1 项）。
    {
        std::vector<std::string> cross;
        for (const pp::OutputFormatSpec &spec : cfg.outputs) {
            const std::vector<std::string> msgs =
                pp::cross_validate(spec.params, spec.format_id, spec.tech_id);
            cross.insert(cross.end(), msgs.begin(), msgs.end());
        }
        if (!cross.empty()) {
            QStringList lines;
            lines.reserve(static_cast<int>(cross.size()));
            for (const std::string &msg : cross)
                lines << QString::fromStdString(msg);
            QMessageBox::warning(this, tr("无法开始"), lines.join(QLatin1Char('\n')));
            return;
        }
    }

    if (cfg.metadata_only) {
        QStringList bad;
        for (std::size_t i = 0; i < d.model->size(); ++i) {
            const QString name = path_text(d.model->row(i).entry.src);
            const QString suffix = QFileInfo(name).suffix().toLower();
            if (!is_metadata_only_format(suffix))
                bad << name;
        }
        if (!bad.isEmpty()) {
            const QString head = bad.mid(0, 20).join(QLatin1Char('\n'));
            const QString more =
                bad.size() > 20 ? tr("\n…（其余 %1 个省略）").arg(bad.size() - 20) : QString();
            const QMessageBox::StandardButton answer = QMessageBox::warning(
                this, tr("仅元数据预检"),
                tr("以下 %1 个文件的格式不支持仅元数据（输出将保持源格式：JPEG / PNG / TIFF / "
                   "WebP）：\n%2%3\n\n仍要继续吗？")
                    .arg(bad.size())
                    .arg(head, more),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
        }
    }

    QStringList names;
    names.reserve(static_cast<int>(d.model->size()));
    for (std::size_t i = 0; i < d.model->size(); ++i) {
        names << path_text(d.model->row(i).entry.src);
    }
    std::vector<pp::FileEntry> entries = d.model->entries(); // 含单文件例外（引擎侧生效）
    const std::size_t total = entries.size();
    for (std::size_t i = 0; i < total; ++i)
        d.model->set_state(i, pp::FileState::Queued);
    d.run_total = total;
    d.run_terminal = 0;
    d.run_done = 0;
    d.run_failed = 0;
    d.run_skipped = 0;

    d.sched = std::make_unique<pp::Scheduler>(cfg, std::move(entries));
    d.sched->set_event_callback([this](const pp::FileEvent &ev) {
        // 回调可能来自任意 worker 线程：立即按值拷贝 FileResult（指针仅发射瞬间有效）
        const bool has_result = ev.result != nullptr;
        const pp::FileResult copy = has_result ? *ev.result : pp::FileResult{};
        QMetaObject::invokeMethod(
            this,
            [this, ev, has_result, copy]() {
                pp::FileEvent forwarded = ev;
                forwarded.result = has_result ? &copy : nullptr;
                impl_->handle_file_event(forwarded);
            },
            Qt::QueuedConnection);
    });
    d.sched->start();
    lock_for_run(true); // G5
    d.page_run->begin_run(total, names);
    d.poll->start();
    refresh_status();
}

void MainWindow::on_cancel() {
    if (impl_->sched)
        impl_->sched->cancel(); // 幂等；进行中的文件跑完
}

void MainWindow::on_scheduler_done() {
    Impl &d = *impl_;
    if (!d.sched)
        return;
    d.poll->stop();
    d.sched->wait();
    // 排空在途 queued 事件后再定稿，避免 end_run 之后又被迟到事件改写计数
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    const pp::RunSummary sum = d.sched->summary();
    d.page_run->end_run(sum, d.page_output->out_root());
    d.sched.reset();
    lock_for_run(false);
    impl_->on_content_changed();
    refresh_status();
}

void MainWindow::lock_for_run(bool lock) {
    Impl &d = *impl_;
    d.running = lock;
    d.nav_meta->setEnabled(!lock);
    d.nav_output->setEnabled(!lock);
    d.settings_action->setEnabled(!lock);
    d.panel->setEnabled(!lock);
    d.page_meta->setEnabled(!lock);
    d.page_output->setEnabled(!lock);
    if (lock)
        set_current_page(3); // stack 锁到运行页
    refresh_status();
}

void MainWindow::refresh_status() {
    Impl &d = *impl_;
    const std::size_t count = d.model->size();
    const std::size_t unsupported = d.model->unsupported_count();
    const std::size_t exceptions = d.model->exception_count();
    const QString reason = d.page_output->ready_to_start();

    QString text;
    bool invalid = false;
    if (d.running) {
        // 运行中：跳过/失败计数同步状态栏（增量计数，避免每个事件 O(n) 重扫）
        text = tr("运行中 · 完成 %1 · 失败 %2 · 跳过 %3")
                   .arg(d.run_done)
                   .arg(d.run_failed)
                   .arg(d.run_skipped);
    } else if (count == 0) {
        text = tr("没有文件");
        invalid = true;
    } else if (!reason.isEmpty()) {
        text = reason;
        invalid = true;
    } else {
        text = tr("%1 个文件 · %2 → %3 · 冲突：%4")
                   .arg(count)
                   .arg(format_label(d.page_output->current_format()), d.page_output->out_root(),
                        conflict_label(d.page_output->config_base().conflict));
    }
    if (exceptions > 0)
        text += tr(" · %1 个例外").arg(exceptions);
    d.status->setText(text);
    d.status->setStyleSheet(invalid ? QStringLiteral("color:#cc0000") : QString());

    d.file_count->setText(tr("%1 个文件").arg(count));
    d.unsupported->setText(unsupported > 0 ? tr("（%1 个不支持）").arg(unsupported) : QString());
    d.unsupported->setVisible(unsupported > 0);

    // G5（2026-09-20 R1 修订）：底栏按钮恒为"开始"；运行中禁用（取消只在运行页）
    const bool can_start = !d.running && count > 0 && reason.isEmpty();
    d.start->setText(tr("开始"));
    d.start->setEnabled(can_start);
    const bool has_selection = d.view->selectionModel() != nullptr &&
                               !d.view->selectionModel()->selectedIndexes().isEmpty();
    d.remove_sel->setEnabled(!d.running && has_selection);
}

void MainWindow::save_session() {
    Impl &d = *impl_;
    d.settings.last_format = d.page_output->current_format().toStdString();
    d.settings.last_out_root = d.page_output->out_root().toStdString();
    d.settings.last_preset = d.last_preset_path.toStdString();
    pp::save_settings(pp::platform::settings_file(), d.settings);
}

// ---------------------------------------------------------------------------
// 设置 / 预设 / EXIF 编辑器
// ---------------------------------------------------------------------------

void MainWindow::open_settings() {
    Impl &d = *impl_;
    if (d.running)
        return;
    SettingsDialog dlg(d.settings, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    d.settings = dlg.settings();
    pp::LogLevel level = pp::LogLevel::Info;
    if (!parse_log_level_text(d.settings.log_level, level))
        level = pp::LogLevel::Info;
    pp::log_set_level(level);
    d.page_meta->set_map_provider(QString::fromStdString(d.settings.map_provider),
                                  QString::fromStdString(d.settings.amap_key),
                                  d.settings.tile_cache_mb);
    pp::save_settings(pp::platform::settings_file(), d.settings); // 设置持久化：立即落盘
    refresh_status();
}

void MainWindow::manage_presets() {
    Impl &d = *impl_;
    if (d.running)
        return;
    const std::filesystem::path dir = pp::platform::presets_dir();
    std::vector<std::pair<QString, QString>> items;
    for (const auto &[path, name] : pp::ui::list_presets(dir)) {
        items.emplace_back(QString::fromStdString(path.string()), QString::fromStdString(name));
    }
    PresetsDialog dlg(
        items,
        d.last_preset_path.isEmpty() ? QString() : QFileInfo(d.last_preset_path).completeBaseName(),
        this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    switch (dlg.action()) {
    case PresetsDialog::Action::Load: {
        pp::PresetData preset;
        const std::string err = pp::ui::load_preset(dlg.path().toStdString(), preset);
        if (!err.empty()) {
            QMessageBox::warning(this, tr("载入预设失败"), QString::fromStdString(err));
            return;
        }
        pp::normalize_preset(preset);
        d.page_output->apply_preset(preset);
        d.page_meta->apply_rules(preset.rules);
        d.last_preset_path = dlg.path();
        refresh_status();
        return;
    }
    case PresetsDialog::Action::SaveAs: {
        const QString name = dlg.name(); // U6 已在对话框内清洗
        if (name.isEmpty())
            return;
        pp::PresetData preset = d.page_output->collect_preset(name);
        preset.rules = d.page_meta->rules(); // rules 归 PageMeta（MainWindow 合并）
        const std::filesystem::path path = dir / (name.toStdString() + ".json");
        const std::string err = pp::ui::save_preset(path, preset);
        if (!err.empty()) {
            QMessageBox::warning(this, tr("保存预设失败"), QString::fromStdString(err));
            return;
        }
        d.last_preset_path = QString::fromStdString(path.string());
        return;
    }
    case PresetsDialog::Action::Delete: {
        const QString path = dlg.path();
        if (path.isEmpty())
            return;
        if (!QFile::remove(path)) {
            QMessageBox::warning(this, tr("删除预设失败"), path);
            return;
        }
        if (d.last_preset_path == path)
            d.last_preset_path.clear();
        return;
    }
    case PresetsDialog::Action::None:
    default:
        return;
    }
}

void MainWindow::open_exif_editor_row(int row) {
    Impl &d = *impl_;
    if (d.running)
        return;
    if (row < 0 || static_cast<std::size_t>(row) >= d.model->size())
        return;
    const FileRow &file_row = d.model->row(static_cast<std::size_t>(row));
    ExifEditor dlg(QString::fromStdString(file_row.entry.src.string()), d.page_meta->rules(),
                   file_row.entry.exception, this);
    dlg.exec();
    // ExifEditor::result() 遮蔽 QDialog::result()（U8 口径）→ 必须显式限定基类
    if (dlg.QDialog::result() == QDialog::Accepted) {
        d.model->set_exception(static_cast<std::size_t>(row), dlg.result());
        refresh_status();
    }
}

void MainWindow::open_exif_editor_path(const QString &path) {
    Impl &d = *impl_;
    for (std::size_t i = 0; i < d.model->size(); ++i) {
        if (QString::fromStdString(d.model->row(i).entry.src.string()) == path) {
            open_exif_editor_row(static_cast<int>(i));
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Impl：联动 / 工具
// ---------------------------------------------------------------------------

void MainWindow::Impl::restore_session() {
    page_output->restore_last(QString::fromStdString(settings.last_format),
                              QString::fromStdString(settings.last_out_root));
    page_meta->set_map_provider(QString::fromStdString(settings.map_provider),
                                QString::fromStdString(settings.amap_key), settings.tile_cache_mb);
    // 预设优先于 last_format（§2.14）：仅当文件仍存在
    if (settings.last_preset.empty())
        return;
    std::error_code ec;
    if (!std::filesystem::exists(settings.last_preset, ec) || ec)
        return;
    pp::PresetData preset;
    if (!pp::ui::load_preset(settings.last_preset, preset).empty())
        return;
    pp::normalize_preset(preset);
    page_output->apply_preset(preset);
    page_meta->apply_rules(preset.rules);
    last_preset_path = QString::fromStdString(settings.last_preset);
}

void MainWindow::Impl::on_content_changed() {
    if (!running) {
        QStringList paths;
        paths.reserve(static_cast<int>(model->size()));
        for (std::size_t i = 0; i < model->size(); ++i) {
            paths << QString::fromStdString(model->row(i).entry.src.string());
        }
        if (paths != cached_paths) {
            cached_paths = paths;
            page_meta->set_batch_files(paths); // 首文件时间预览（PageMeta 内部限扫 200）
            bool any_alpha = false;
            for (std::size_t i = 0; i < model->size(); ++i) {
                const FileRow &row = model->row(i);
                if (row.probe_ok && row.info.has_alpha)
                    any_alpha = true;
            }
            if (any_alpha != cached_alpha) {
                cached_alpha = any_alpha;
                page_output->set_batch_has_alpha(any_alpha); // 无探测信息时保守 false
            }
        }
        // 例外编辑只在非运行期发生（编辑器/清除入口在运行中关闭）→ 运行中跳过 O(n) 重扫
        sync_exceptions();
    }
    w->refresh_status();
}

void MainWindow::Impl::sync_selection() {
    QStringList paths;
    bool has = false;
    if (view->selectionModel() != nullptr) {
        for (const QModelIndex &idx : view->selectionModel()->selectedIndexes()) {
            const int row = proxy->mapToSource(idx).row();
            if (row < 0 || static_cast<std::size_t>(row) >= model->size())
                continue;
            paths << QString::fromStdString(
                model->row(static_cast<std::size_t>(row)).entry.src.string());
            has = true;
        }
    }
    page_meta->set_selected_files(paths);
    remove_sel->setEnabled(!running && has);
}

void MainWindow::Impl::sync_exceptions() {
    QStringList paths;
    for (std::size_t i = 0; i < model->size(); ++i) {
        if (model->row(i).has_exception()) {
            paths << QString::fromStdString(model->row(i).entry.src.string());
        }
    }
    page_meta->set_exception_summary(paths);
}

void MainWindow::Impl::clear_exception(const QString &path) {
    for (std::size_t i = 0; i < model->size(); ++i) {
        if (QString::fromStdString(model->row(i).entry.src.string()) == path) {
            model->set_exception(i, std::nullopt);
            return;
        }
    }
}

void MainWindow::Impl::remove_selected() {
    if (view->selectionModel() == nullptr)
        return;
    QList<int> rows;
    for (const QModelIndex &idx : view->selectionModel()->selectedIndexes()) {
        const int row = proxy->mapToSource(idx).row();
        if (row >= 0 && !rows.contains(row))
            rows << row;
    }
    if (rows.isEmpty())
        return;
    std::sort(rows.begin(), rows.end());
    model->remove_rows(rows);
}

namespace {

// §M2-T16b(c')：logs_dir() 来自平台字节层，可能含**非 UTF-8 字节**；Qt6 的 QString 恒为
// UTF-8 语义，QString::fromStdString(dir.string()) 会把非法序列替换成 U+FFFD →
// QDir::entryInfoList 直接丢掉全部条目，QUrl 也会指向被改写后的假路径（T14 报告实测）。故：
//   * 枚举一律走 std::filesystem::directory_iterator（目录项名保留原始字节）；
//   * 只有交给 QDesktopServices 时才需要 QString：先做 UTF-8 往返校验，合法 → fromLocalFile；
//     非法 → stderr 明确提示（不静默失败）。
bool bytes_are_valid_utf8(const std::string &bytes) {
    const QByteArray raw(bytes.data(), static_cast<int>(bytes.size()));
    return QString::fromUtf8(raw).toUtf8() == raw;
}

void open_local_path_bytes(const std::filesystem::path &p, const char *what) {
    const std::string bytes = p.string();
    if (!bytes_are_valid_utf8(bytes)) {
        std::fprintf(stderr,
                     "ui: cannot open %s: path contains non-UTF-8 bytes; open it from a shell "
                     "instead (raw: %s)\n",
                     what, bytes.c_str());
        std::fflush(stderr);
        return;
    }
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QString::fromUtf8(bytes.data(), static_cast<int>(bytes.size()))));
}

// 最新 run-*.log：mtime 新者优先；同 mtime 取名字字典序小者（与 QDir::Time 同口径）。
// 无匹配 → 空 path（调用方退回目录本身）。字节级匹配：非 UTF-8 文件名同样命中。
std::filesystem::path newest_run_log(const std::filesystem::path &dir) {
    std::filesystem::path newest;
    std::filesystem::file_time_type newest_time{};
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec) || fec)
            continue;
        const std::string name = it->path().filename().string();
        if (name.size() < 8 || name.compare(0, 4, "run-") != 0 ||
            name.compare(name.size() - 4, 4, ".log") != 0) {
            continue;
        }
        const std::filesystem::file_time_type t = it->last_write_time(fec);
        if (fec)
            continue;
        if (newest.empty() || t > newest_time ||
            (t == newest_time && name < newest.filename().string())) {
            newest = it->path();
            newest_time = t;
        }
    }
    return newest;
}

} // namespace

void MainWindow::Impl::open_logs() {
    const std::filesystem::path dir = pp::platform::logs_dir();
    if (dir.empty()) {
        std::fprintf(stderr, "ui: cannot open logs: data_dir()/logs is unavailable (empty path)\n");
        std::fflush(stderr);
        return;
    }
    const std::filesystem::path newest = newest_run_log(dir);
    open_local_path_bytes(newest.empty() ? dir : newest, "logs");
}

void MainWindow::Impl::handle_file_event(const pp::FileEvent &ev) {
    if (ev.index >= model->size())
        return;
    switch (ev.state) { // 计数先于 set_state：set_state 同步触发 content_changed → refresh_status
    case pp::FileState::Done:
        ++run_terminal;
        ++run_done;
        break;
    case pp::FileState::Skipped:
        ++run_terminal;
        ++run_skipped;
        break;
    case pp::FileState::Failed:
        ++run_terminal;
        ++run_failed;
        break;
    case pp::FileState::Cancelled:
        ++run_terminal; // Scheduler::finish() 对每个文件恰好发一次终态事件
        break;
    default:
        break;
    }
    model->set_state(ev.index, ev.state);
    page_run->on_event(ev);
    w->refresh_status();
}

// ---------------------------------------------------------------------------
// --ui-smoke 脚本化走查（§4.3 冻结断言）
// ---------------------------------------------------------------------------

void MainWindow::ui_smoke_walk(const QString &shots_dir) {
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

void MainWindow::Impl::smoke_run(const QString &shots_dir) {
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

    // ---- 实跑前置：jxl + out_root=<仓库>/.cache/tmp/ui-smoke-out + conflict=overwrite ----
    page_output->select_format(QStringLiteral("jxl"));
    pump(250);
    QString root = repo_root();
    if (root.isEmpty()) {
        root = QDir::currentPath();
        std::fprintf(stderr, "ui-smoke: repo root not found; out_root falls back to %s\n",
                     qUtf8Printable(root));
    }
    const QString out_root = root + QStringLiteral("/.cache/tmp/ui-smoke-out");
    QDir(out_root).removeRecursively();
    if (!QDir().mkpath(out_root)) {
        smoke_fail(MainWindow::tr("无法创建输出目录：%1").arg(out_root));
    }
    page_output->set_out_root(out_root);
    // conflict 无独立 setter → 用冻结 API 走一次 collect/apply 往返（其余字段保持现状）
    pp::PresetData preset = page_output->collect_preset(QStringLiteral("ui-smoke"));
    preset.conflict = pp::ConflictPolicy::Overwrite;
    page_output->apply_preset(preset);
    pump(200);
    if (page_output->current_format() != QLatin1String("jxl")) {
        smoke_fail(
            MainWindow::tr("冒烟前置：格式不是 jxl（%1）").arg(page_output->current_format()));
    }
    if (page_output->out_root() != out_root) {
        smoke_fail(MainWindow::tr("冒烟前置：输出根目录未生效（%1）").arg(page_output->out_root()));
    }
    if (page_output->config_base().conflict != pp::ConflictPolicy::Overwrite) {
        smoke_fail(MainWindow::tr("冒烟前置：冲突策略不是覆盖"));
    }

    // ---- 页 3（运行）：点开始 → 03-run.png ----
    w->set_current_page(3);
    pump(200);
    start->click();
    // 尽量抓在"运行中"：本批 16 个小文件 ~150ms 就跑完，故只推进到出现部分进度即抓，
    // 否则退化为结束态（与 03b 同图，仅记入 stdout 供审查判断）。
    QProgressBar *run_progress = w->findChild<QProgressBar *>(QStringLiteral("runProgress"));
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
    std::printf("UI-SMOKE 03-run: running=%d progress=%d/%d\n", page_run->is_running() ? 1 : 0,
                run_progress != nullptr ? run_progress->value() : -1,
                run_progress != nullptr ? run_progress->maximum() : -1);
    std::fflush(stdout);
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
    QProgressBar *progress = w->findChild<QProgressBar *>(QStringLiteral("runProgress"));
    if (progress == nullptr) {
        smoke_fail(MainWindow::tr("运行页缺少 runProgress"));
    } else if (progress->maximum() <= 0 || progress->value() != progress->maximum()) {
        smoke_fail(
            MainWindow::tr("进度条未满：%1 / %2").arg(progress->value()).arg(progress->maximum()));
    }
    QGroupBox *summary = w->findChild<QGroupBox *>(QStringLiteral("runSummary"));
    if (summary == nullptr || !summary->isVisible()) {
        smoke_fail(MainWindow::tr("运行摘要不可见"));
    }
    if (QLabel *run_status = w->findChild<QLabel *>(QStringLiteral("runStatus"))) {
        std::printf("UI-SMOKE run status: %s\n", qUtf8Printable(run_status->text()));
    }
    if (QLabel *summary_text = w->findChild<QLabel *>(QStringLiteral("runSummaryText"))) {
        const QString text =
            QString(summary_text->text()).replace(QLatin1Char('\n'), QLatin1String(" / "));
        std::printf("UI-SMOKE summary: %s\n", qUtf8Printable(text));
    }
    std::fflush(stdout);

    // ---- 对话框三连（构造 → show → processEvents → grab → close，绝不 exec）----
    { // 04-settings.png：设置对话框
        SettingsDialog dlg(settings, w);
        dlg.show();
        pump(250);
        smoke_grab(shots_dir, kSmokeShots[kShotSettings], &dlg);
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
}

} // namespace pp::ui
