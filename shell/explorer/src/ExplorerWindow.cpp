#include "ExplorerWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QProcess>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>

CastaliaIconProvider::CastaliaIconProvider(const QString &iconDir)
    : m_folder(iconDir + QStringLiteral("/folder.svg")),
      m_file(iconDir + QStringLiteral("/documents.svg")),
      m_disk(iconDir + QStringLiteral("/disk.svg"))
{
}

QIcon CastaliaIconProvider::icon(IconType type) const
{
    switch (type) {
    case Folder:  return m_folder;
    case Drive:   return m_disk;
    default:      return m_file;
    }
}

QIcon CastaliaIconProvider::icon(const QFileInfo &info) const
{
    return info.isDir() ? m_folder : m_file;
}

ExplorerWindow::ExplorerWindow(const ThemeTokens &tokens,
                               const QString &iconDir,
                               const QString &startPath, QWidget *parent)
    : QMainWindow(parent), m_tokens(tokens), m_iconDir(iconDir)
{
    setWindowIcon(themedIcon(QStringLiteral("folder")));
    resize(780, 520);

    // ---- menu bar (§9.1 MVP surface) ----
    QMenu *file = menuBar()->addMenu(QStringLiteral("&Archivo"));
    file->addAction(QStringLiteral("Nueva carpeta"));
    file->addSeparator();
    file->addAction(QStringLiteral("Cerrar"), this, &QWidget::close);
    menuBar()->addMenu(QStringLiteral("&Edición"));
    QMenu *view = menuBar()->addMenu(QStringLiteral("&Ver"));
    menuBar()->addMenu(QStringLiteral("Ay&uda"));

    // ---- toolbar: back / forward / up + address ----
    auto *bar = addToolBar(QStringLiteral("Navegación"));
    bar->setMovable(false);
    QAction *back = bar->addAction(QStringLiteral("← Atrás"));
    QAction *fwd = bar->addAction(QStringLiteral("→"));
    QAction *up = bar->addAction(QStringLiteral("↑"));
    connect(back, &QAction::triggered, this, &ExplorerWindow::goBack);
    connect(fwd, &QAction::triggered, this, &ExplorerWindow::goForward);
    connect(up, &QAction::triggered, this, &ExplorerWindow::goUp);
    m_address = new QLineEdit(this);
    m_address->setObjectName(QStringLiteral("AddressBar"));
    connect(m_address, &QLineEdit::returnPressed, this, [this]() {
        navigateTo(m_address->text());
    });
    bar->addWidget(m_address);

    // ---- places sidebar (§7.10) ----
    m_places = new QListWidget(this);
    m_places->setObjectName(QStringLiteral("PlacesList"));
    m_places->setIconSize(QSize(16, 16));
    m_places->setFixedWidth(185);
    m_places->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const struct { const char *icon; const char *label; } places[] = {
        {"home", "Carpeta personal"}, {"documents", "Documentos"},
        {"computer", "Equipo"},       {"network", "Lugares de red"},
        {"trash", "Papelera"},
    };
    for (const auto &p : places) {
        auto *item = new QListWidgetItem(themedIcon(QLatin1String(p.icon)),
                                         QLatin1String(p.label), m_places);
        Q_UNUSED(item);
    }
    connect(m_places, &QListWidget::itemActivated, this,
            [this, startPath](QListWidgetItem *item) {
                // PoC mapping: Documentos → start dir, personal → its parent
                if (item->text() == QLatin1String("Documentos"))
                    navigateTo(startPath);
                else if (item->text() == QLatin1String("Carpeta personal"))
                    navigateTo(QFileInfo(startPath).dir().absolutePath());
            });

    // ---- file view over the real filesystem ----
    m_model = new QFileSystemModel(this);
    m_model->setIconProvider(new CastaliaIconProvider(iconDir));
    m_model->setRootPath(QString());
    connect(m_model, &QFileSystemModel::directoryLoaded, this,
            &ExplorerWindow::updateStatus);

    m_view = new QListView(this);
    m_view->setObjectName(QStringLiteral("FileView"));
    m_view->setModel(m_model);
    m_view->setViewMode(QListView::IconMode);
    m_view->setIconSize(QSize(48, 48));
    m_view->setGridSize(QSize(96, 84));
    m_view->setResizeMode(QListView::Adjust);
    m_view->setWordWrap(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_view, &QListView::activated, this,
            &ExplorerWindow::onActivated);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QListView::customContextMenuRequested, this,
            &ExplorerWindow::showContextMenu);

    auto *iconMode = view->addAction(QStringLiteral("Iconos"));
    auto *listMode = view->addAction(QStringLiteral("Lista"));
    iconMode->setCheckable(true);
    listMode->setCheckable(true);
    iconMode->setChecked(true);
    auto *group = new QActionGroup(this);
    group->addAction(iconMode);
    group->addAction(listMode);
    connect(iconMode, &QAction::triggered, this, [this]() {
        m_view->setViewMode(QListView::IconMode);
        m_view->setIconSize(QSize(48, 48));
        m_view->setGridSize(QSize(96, 84));
    });
    connect(listMode, &QAction::triggered, this, [this]() {
        m_view->setViewMode(QListView::ListMode);
        m_view->setIconSize(QSize(16, 16));
        m_view->setGridSize(QSize());
    });

    auto *split = new QSplitter(this);
    split->setHandleWidth(1);
    split->setChildrenCollapsible(false);
    split->addWidget(m_places);
    split->addWidget(m_view);
    split->setStretchFactor(1, 1);
    split->setSizes({m_places->width(), 1000});
    setCentralWidget(split);

    m_status = new QLabel(this);
    statusBar()->addWidget(m_status);

    navigateTo(startPath, false);
}

QIcon ExplorerWindow::themedIcon(const QString &name) const
{
    return QIcon(m_iconDir + QLatin1Char('/') + name
                 + QStringLiteral(".svg"));
}

void ExplorerWindow::navigateTo(const QString &path, bool remember)
{
    QDir dir(path);
    if (!dir.exists())
        return;
    if (remember && !m_current.isEmpty()) {
        m_backStack.push_back(m_current);
        m_forwardStack.clear();
    }
    m_current = dir.absolutePath();
    m_view->setRootIndex(m_model->index(m_current));
    m_address->setText(m_current);
    setWindowTitle(dir.dirName().isEmpty()
                       ? QStringLiteral("Castalia Explorer")
                       : dir.dirName()
                             + QStringLiteral(" — Castalia Explorer"));
    updateStatus();
}

void ExplorerWindow::goBack()
{
    if (m_backStack.isEmpty())
        return;
    m_forwardStack.push_back(m_current);
    const QString target = m_backStack.takeLast();
    navigateTo(target, false);
}

void ExplorerWindow::goForward()
{
    if (m_forwardStack.isEmpty())
        return;
    m_backStack.push_back(m_current);
    const QString target = m_forwardStack.takeLast();
    navigateTo(target, false);
}

void ExplorerWindow::goUp()
{
    QDir dir(m_current);
    if (dir.cdUp())
        navigateTo(dir.absolutePath());
}

void ExplorerWindow::onActivated(const QModelIndex &index)
{
    const QString path = m_model->filePath(index);
    if (m_model->isDir(index))
        navigateTo(path);
    else
        openPath(path);
}

QString ExplorerWindow::repoRoot() const
{
    return qEnvironmentVariable("CASTALIA_REPO", QStringLiteral("."));
}

QString ExplorerWindow::themeId() const
{
    const QString t = m_tokens.themeId();
    return t.isEmpty() ? QStringLiteral("classic") : t;
}

// Open a file in the matching first-party app; fall back to the system
// default for anything we don't have an app for.
void ExplorerWindow::openPath(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.isDir()) {
        navigateTo(path);
        return;
    }
    const QString ext = fi.suffix().toLower();
    static const QStringList kImg = {
        "png", "jpg", "jpeg", "gif", "bmp", "svg", "webp", "xpm", "ico"};
    static const QStringList kTxt = {
        "txt", "md", "log", "conf", "cfg", "ini", "csv", "xml", "json",
        "sh", "py", "c", "h", "cpp", "desktop", "srt", "yml", "yaml"};
    static const QStringList kArc = {
        "zip", "tar", "gz", "tgz", "bz2", "xz", "7z"};
    QString bin;
    if (kImg.contains(ext))
        bin = QStringLiteral("castalia-visor");
    else if (kTxt.contains(ext))
        bin = QStringLiteral("castalia-notas");
    else if (kArc.contains(ext))
        bin = QStringLiteral("castalia-archivador");
    if (!bin.isEmpty())
        QProcess::startDetached(
            bin, {path, QStringLiteral("--repo"), repoRoot(),
                  QStringLiteral("--theme"), themeId()});
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void ExplorerWindow::extractHere(const QString &archive)
{
    const QString dir = QFileInfo(archive).absolutePath();
    QProcess::startDetached(QStringLiteral("bsdtar"),
                            {QStringLiteral("-xf"), archive,
                             QStringLiteral("-C"), dir});
    if (m_status)
        m_status->setText(QStringLiteral("Extrayendo %1…")
                              .arg(QFileInfo(archive).fileName()));
}

void ExplorerWindow::showContextMenu(const QPoint &pos)
{
    const QModelIndex idx = m_view->indexAt(pos);
    QMenu menu(this);
    if (idx.isValid()) {
        const QString path = m_model->filePath(idx);
        const QString ext = QFileInfo(path).suffix().toLower();
        QAction *open = menu.addAction(QStringLiteral("Abrir"));
        connect(open, &QAction::triggered, this,
                [this, path]() { openPath(path); });
        static const QStringList kArc = {
            "zip", "tar", "gz", "tgz", "bz2", "xz", "7z"};
        if (kArc.contains(ext)) {
            QAction *ex = menu.addAction(QStringLiteral("Extraer aquí"));
            connect(ex, &QAction::triggered, this,
                    [this, path]() { extractHere(path); });
        }
        menu.addSeparator();
        QAction *copy = menu.addAction(QStringLiteral("Copiar ruta"));
        connect(copy, &QAction::triggered, this, [path]() {
            QApplication::clipboard()->setText(path);
        });
    } else {
        QAction *refresh = menu.addAction(QStringLiteral("Actualizar"));
        connect(refresh, &QAction::triggered, this,
                [this]() { navigateTo(m_current, false); });
    }
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

void ExplorerWindow::updateStatus()
{
    const QModelIndex root = m_view->rootIndex();
    const int count = m_model->rowCount(root);
    m_status->setText(count == 1
                          ? QStringLiteral("1 elemento")
                          : QStringLiteral("%1 elementos").arg(count));
}
