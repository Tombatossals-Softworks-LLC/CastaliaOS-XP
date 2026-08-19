// castalia-archivador — the Archive Manager (Bible §9.3).
//
// Open zip / tar / tar.gz / tar.bz2 / tar.xz / 7z and browse them as a folder
// tree; extract all or a selection; create a new archive. It drives bsdtar
// (libarchive) — one tool, every common format — so the app stays small and
// self-contained. Pure Qt5 + libcastalia-ui theming.
//
// Usage: castalia-archivador [ARCHIVE] --theme classic [--repo P]
//        [--list ARCHIVE] [--demo] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cstdio>

#include "Mark.h"
#include "Notify.h"
#include "Theme.h"

namespace {

// List an archive's entries (paths) via bsdtar. Empty on failure.
QStringList listArchive(const QString &path)
{
    QProcess p;
    p.start(QStringLiteral("bsdtar"), {QStringLiteral("-tf"), path});
    QStringList out;
    if (p.waitForFinished(10000))
        for (const QString &l : QString::fromUtf8(p.readAllStandardOutput())
                                    .split(QLatin1Char('\n'),
                                           Qt::SkipEmptyParts))
            out << l;
    return out;
}

// A believable demo tree so an offscreen render shows a populated archive.
QStringList demoEntries()
{
    return {QStringLiteral("Documentos/"),
            QStringLiteral("Documentos/Factura-2007.pdf"),
            QStringLiteral("Documentos/Carta.rtf"),
            QStringLiteral("Imágenes/"),
            QStringLiteral("Imágenes/vacaciones.jpg"),
            QStringLiteral("Imágenes/perfil.png"),
            QStringLiteral("Música/"),
            QStringLiteral("Música/cancion.ogg"),
            QStringLiteral("notas.txt"),
            QStringLiteral("LÉEME.txt")};
}

} // namespace

class Archiver : public QWidget {
public:
    Archiver(const QString &repo, const QColor &accent)
        : m_repo(repo), m_accent(accent)
    {
        setWindowTitle(QStringLiteral("Gestor de archivos comprimidos — "
                                      "Castalia"));
        resize(640, 500);
        m_folderIcon = QIcon(QDir(m_repo).filePath(
            QStringLiteral("themes/icons/48/folder.svg")));
        m_fileIcon = QIcon(QDir(m_repo).filePath(
            QStringLiteral("themes/icons/48/documents.svg")));

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->addWidget(buildHeader());

        auto *bar = new QToolBar(this);
        bar->setMovable(false);
        bar->addAction(QStringLiteral("Abrir…"), this, &Archiver::openDialog);
        m_extractAll = bar->addAction(QStringLiteral("Extraer todo…"), this,
                                      [this]() { extract(false); });
        m_extractSel = bar->addAction(QStringLiteral("Extraer selección…"),
                                      this, [this]() { extract(true); });
        bar->addSeparator();
        bar->addAction(QStringLiteral("Nuevo archivo…"), this,
                       &Archiver::createArchive);
        root->addWidget(bar);

        m_tree = new QTreeWidget(this);
        m_tree->setHeaderLabels({QStringLiteral("Nombre")});
        m_tree->header()->setStretchLastSection(true);
        m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_tree->setAlternatingRowColors(true);
        root->addWidget(m_tree, 1);

        m_status = new QLabel(QStringLiteral("Abre un archivo comprimido para "
                                             "ver su contenido."), this);
        m_status->setContentsMargins(12, 6, 12, 8);
        m_status->setProperty("secondary", true);
        root->addWidget(m_status);
        setEnabledActions(false);
    }

    void openArchive(const QString &path)
    {
        const QStringList entries = listArchive(path);
        if (entries.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Abrir"),
                                 QStringLiteral("No se pudo leer el archivo o "
                                                "está vacío."));
            return;
        }
        m_path = path;
        populate(entries);
        setWindowTitle(QFileInfo(path).fileName()
                       + QStringLiteral(" — Gestor de archivos"));
    }
    void showDemo() { m_path.clear(); populate(demoEntries()); }

private:
    QWidget *buildHeader()
    {
        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("ArchHeader"));
        head->setFixedHeight(58);
        head->setStyleSheet(QStringLiteral(
            "#ArchHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(m_accent.lighter(112).name(), m_accent.darker(118).name()));
        auto *l = new QHBoxLayout(head);
        l->setContentsMargins(16, 0, 16, 0);
        m_title = new QLabel(head);
        m_title->setText(QStringLiteral(
            "<span style='color:white;font-size:15px;font-weight:bold'>"
            "Archivos comprimidos</span><br>"
            "<span style='color:#EAF1F7'>zip · tar · gz · bz2 · xz · 7z</span>"));
        l->addWidget(m_title);
        l->addStretch(1);
        return head;
    }

    void setEnabledActions(bool on)
    {
        if (m_extractAll) m_extractAll->setEnabled(on);
        if (m_extractSel) m_extractSel->setEnabled(on);
    }

    void populate(const QStringList &entries)
    {
        m_tree->clear();
        QHash<QString, QTreeWidgetItem *> dirs;  // path -> item
        long files = 0;
        for (const QString &raw : entries) {
            QString e = raw;
            const bool isDir = e.endsWith(QLatin1Char('/'));
            if (isDir)
                e.chop(1);
            const QStringList parts =
                e.split(QLatin1Char('/'), Qt::SkipEmptyParts);
            if (parts.isEmpty())
                continue;
            QString accum;
            QTreeWidgetItem *parent = nullptr;
            for (int i = 0; i < parts.size(); ++i) {
                accum += (i ? QStringLiteral("/") : QString()) + parts[i];
                const bool leaf = (i == parts.size() - 1);
                const bool leafIsDir = isDir || !leaf;
                QTreeWidgetItem *item = dirs.value(accum);
                if (!item) {
                    item = parent ? new QTreeWidgetItem(parent)
                                  : new QTreeWidgetItem(m_tree);
                    item->setText(0, parts[i]);
                    item->setIcon(0, leafIsDir ? m_folderIcon : m_fileIcon);
                    item->setData(0, Qt::UserRole, raw);  // full stored path
                    dirs.insert(accum, item);
                    if (leaf && !leafIsDir)
                        ++files;
                }
                parent = item;
            }
        }
        m_tree->expandToDepth(0);
        setEnabledActions(true);
        const QString size = m_path.isEmpty()
            ? QStringLiteral("demostración")
            : QStringLiteral("%1 KiB")
                  .arg(QFileInfo(m_path).size() / 1024);
        m_status->setText(QStringLiteral("%1 archivos · %2")
                              .arg(files).arg(size));
    }

    void openDialog()
    {
        const QString p = QFileDialog::getOpenFileName(
            this, QStringLiteral("Abrir archivo comprimido"),
            QDir::homePath(),
            QStringLiteral("Comprimidos (*.zip *.tar *.tar.gz *.tgz *.tar.bz2 "
                           "*.tar.xz *.7z *.gz *.bz2 *.xz)"));
        if (!p.isEmpty())
            openArchive(p);
    }

    void extract(bool selectionOnly)
    {
        if (m_path.isEmpty()) {
            QMessageBox::information(
                this, QStringLiteral("Extraer"),
                QStringLiteral("Esta es una vista de demostración. Abre un "
                               "archivo real para extraer."));
            return;
        }
        const QString dest = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Extraer en…"), QDir::homePath());
        if (dest.isEmpty())
            return;
        QStringList args{QStringLiteral("-xf"), m_path,
                         QStringLiteral("-C"), dest};
        if (selectionOnly) {
            for (QTreeWidgetItem *it : m_tree->selectedItems()) {
                const QString stored =
                    it->data(0, Qt::UserRole).toString();
                if (!stored.isEmpty())
                    args << stored;
            }
            if (args.size() == 4) {  // nothing selected
                QMessageBox::information(
                    this, QStringLiteral("Extraer"),
                    QStringLiteral("Selecciona al menos un elemento."));
                return;
            }
        }
        QProcess p;
        p.start(QStringLiteral("bsdtar"), args);
        const bool ok = p.waitForFinished(60000) && p.exitCode() == 0;
        m_status->setText(ok
            ? QStringLiteral("Extraído en %1").arg(dest)
            : QStringLiteral("La extracción falló."));
        // Extraction can take a minute; the user is rarely still watching.
        castalia::notify(QStringLiteral("Archivos comprimidos"),
                         ok ? QStringLiteral("Extracción terminada")
                            : QStringLiteral("La extracción falló"),
                         dest, QStringLiteral("archive"));
        if (!ok)
            QMessageBox::warning(this, QStringLiteral("Extraer"),
                                 QString::fromUtf8(p.readAllStandardError()));
    }

    void createArchive()
    {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("Elegir archivos para comprimir"),
            QDir::homePath());
        if (files.isEmpty())
            return;
        QString out = QFileDialog::getSaveFileName(
            this, QStringLiteral("Guardar como"),
            QDir::homePath() + QStringLiteral("/archivo.zip"),
            QStringLiteral("Zip (*.zip);;Tar.gz (*.tar.gz)"));
        if (out.isEmpty())
            return;
        if (!QFileInfo(out).fileName().contains(QLatin1Char('.')))
            out += QStringLiteral(".zip");
        // store basenames, relative to each file's own directory
        const QString cwd = QFileInfo(files.first()).absolutePath();
        QStringList args{QStringLiteral("-a"), QStringLiteral("-c"),
                         QStringLiteral("-f"), out, QStringLiteral("-C"), cwd};
        for (const QString &f : files)
            args << QFileInfo(f).fileName();
        QProcess p;
        p.start(QStringLiteral("bsdtar"), args);
        const bool ok = p.waitForFinished(120000) && p.exitCode() == 0;
        if (ok)
            openArchive(out);
        else
            QMessageBox::warning(this, QStringLiteral("Nuevo archivo"),
                                 QString::fromUtf8(p.readAllStandardError()));
    }

    QString m_repo;
    QColor m_accent;
    QString m_path;
    QIcon m_folderIcon, m_fileIcon;
    QLabel *m_title = nullptr, *m_status = nullptr;
    QTreeWidget *m_tree = nullptr;
    QAction *m_extractAll = nullptr, *m_extractSel = nullptr;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-archivador"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addPositionalArgument(QStringLiteral("archive"),
                              QStringLiteral("Archive to open"));
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("list"),
                   QStringLiteral("List an archive's entries and exit"),
                   QStringLiteral("file")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Show a demo archive tree")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString listArg = cli.value(QStringLiteral("list"));
    if (!listArg.isEmpty()) {
        for (const QString &e : listArchive(listArg))
            std::printf("%s\n", qPrintable(e));
        return 0;
    }

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const QString accentStr =
        ThemeTokens::load(castalia::themeConfPath(repo, themeId))
            .str(QStringLiteral("colors"), QStringLiteral("accent"));
    castalia::applyTheme(&app, repo, themeId);
    const QColor accent(accentStr.isEmpty() ? QStringLiteral("#3E82B6")
                                            : accentStr);

    Archiver w(repo, accent);
    const bool shot = !cli.value(QStringLiteral("screenshot")).isEmpty();
    if (!cli.positionalArguments().isEmpty())
        w.openArchive(cli.positionalArguments().first());
    else if (cli.isSet(QStringLiteral("demo")) || shot)
        w.showDemo();
    w.show();

    const QString out = cli.value(QStringLiteral("screenshot"));
    if (!out.isEmpty())
        QTimer::singleShot(200, &app, [&]() {
            w.grab().save(out);
            app.quit();
        });
    return app.exec();
}
