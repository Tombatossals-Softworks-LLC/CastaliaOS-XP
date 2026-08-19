// castalia-papelera — the Castalia Recycle Bin (Bible §9, §10 XP-parity).
//
// The desktop's Papelera de reciclaje, implemented against the freedesktop.org
// Trash specification: it reads the user's trash at
// $XDG_DATA_HOME/Trash/{files,info} (default ~/.local/share/Trash), lists what
// is there with each item's original location, deletion date and size, and can
// restore an item to where it came from, delete one for good, or empty the
// whole bin. Being a real spec means files sent to the trash by the file
// manager (or any compliant app) show up here and vice-versa. Pure Qt5; the
// only writes are inside the user's own trash and the restore targets.
//
// `--demo` shows a representative, read-only bin (no disk touched) so the
// offscreen render and the live-suite have something to display.
//
// Usage: castalia-papelera --theme human [--repo PATH] [--demo]
//                          [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "Notify.h"
#include "Sound.h"
#include "Theme.h"

namespace {

struct TrashItem {
    QString name;        // basename in the trash
    QString origPath;    // absolute path it was deleted from
    QDateTime deleted;
    qint64 size = 0;
    QString filePath;    // Trash/files/<name>
    QString infoPath;    // Trash/info/<name>.trashinfo
};

QString trashRoot()
{
    const QString xdg = qEnvironmentVariable("XDG_DATA_HOME");
    const QString base = !xdg.isEmpty()
        ? xdg
        : qEnvironmentVariable("HOME", QStringLiteral("/root"))
              + QStringLiteral("/.local/share");
    return base + QStringLiteral("/Trash");
}

qint64 pathSize(const QString &path)
{
    QFileInfo fi(path);
    if (!fi.isDir())
        return fi.size();
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QString humanSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

// Parse the real trash into items (best effort; skips malformed entries).
QList<TrashItem> readTrash()
{
    QList<TrashItem> out;
    const QString root = trashRoot();
    QDir infoDir(root + QStringLiteral("/info"));
    const QString filesDir = root + QStringLiteral("/files");
    const auto infos = infoDir.entryList({QStringLiteral("*.trashinfo")},
                                         QDir::Files);
    for (const QString &info : infos) {
        const QString base = info.left(info.size() - 10);  // strip .trashinfo
        const QString filePath = filesDir + QLatin1Char('/') + base;
        if (!QFileInfo::exists(filePath))
            continue;
        QSettings s(infoDir.filePath(info), QSettings::IniFormat);
        s.beginGroup(QStringLiteral("Trash Info"));
        const QString rawPath = s.value(QStringLiteral("Path")).toString();
        const QString del = s.value(QStringLiteral("DeletionDate")).toString();
        s.endGroup();
        if (rawPath.isEmpty())
            continue;
        TrashItem it;
        it.name = base;
        // Paths are percent-encoded per the spec (RFC 2396).
        it.origPath = QUrl::fromPercentEncoding(rawPath.toUtf8());
        it.deleted = QDateTime::fromString(del, Qt::ISODate);
        it.size = pathSize(filePath);
        it.filePath = filePath;
        it.infoPath = infoDir.filePath(info);
        out.append(it);
    }
    return out;
}

QList<TrashItem> demoTrash()
{
    const QString home = qEnvironmentVariable("HOME", QStringLiteral("/root"));
    auto mk = [](const QString &name, const QString &path, qint64 size,
                 const QDateTime &when) {
        TrashItem it;
        it.name = name;
        it.origPath = path;
        it.size = size;
        it.deleted = when;
        return it;
    };
    const QDateTime base(QDate(2026, 7, 24), QTime(9, 15));
    return {
        mk(QStringLiteral("presupuesto-2026.ods"),
           home + QStringLiteral("/Documentos/presupuesto-2026.ods"),
           48213, base),
        mk(QStringLiteral("captura-antigua.png"),
           home + QStringLiteral("/Escritorio/captura-antigua.png"),
           1503002, base.addSecs(-3600)),
        mk(QStringLiteral("notas-viejas"),
           home + QStringLiteral("/Documentos/notas-viejas"),
           12040, base.addDays(-1)),
    };
}

bool removePath(const QString &path)
{
    QFileInfo fi(path);
    if (fi.isDir())
        return QDir(path).removeRecursively();
    return QFile::remove(path);
}

} // namespace

class Papelera : public QWidget {
    Q_OBJECT
public:
    Papelera(const QString &repo, const ThemeTokens &tokens, bool demo)
        : m_repo(repo), m_tokens(tokens), m_demo(demo)
    {
        setWindowTitle(QStringLiteral("Papelera de reciclaje — Castalia"));
        resize(640, 420);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("PpHeader"));
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "#PpHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *icon = new QLabel(head);
        icon->setPixmap(castalia::themeIcon(m_repo, QStringLiteral("trash"))
                            .pixmap(28, 28));
        hl->addWidget(icon);
        m_title = new QLabel(head);
        hl->addWidget(m_title);
        hl->addStretch(1);
        root->addWidget(head);

        auto *bar = new QHBoxLayout;
        bar->setContentsMargins(14, 10, 14, 0);
        bar->setSpacing(8);
        m_restore = mkButton(QStringLiteral("Restaurar"), QString());
        m_delete = mkButton(QStringLiteral("Eliminar"), QString());
        bar->addWidget(m_restore);
        bar->addWidget(m_delete);
        bar->addStretch(1);
        m_empty = mkButton(QStringLiteral("Vaciar papelera"),
                           QStringLiteral("PpEmpty"));
        bar->addWidget(m_empty);
        root->addLayout(bar);
        connect(m_restore, &QPushButton::clicked, this, &Papelera::restoreSel);
        connect(m_delete, &QPushButton::clicked, this, &Papelera::deleteSel);
        connect(m_empty, &QPushButton::clicked, this, &Papelera::emptyAll);

        m_stack = new QStackedWidget(this);
        m_table = new QTableWidget(0, 4, this);
        m_table->setObjectName(QStringLiteral("PpTable"));
        m_table->setHorizontalHeaderLabels({
            QStringLiteral("Nombre"), QStringLiteral("Ubicación original"),
            QStringLiteral("Eliminado"), QStringLiteral("Tamaño")});
        m_table->verticalHeader()->setVisible(false);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->horizontalHeader()->setStretchLastSection(false);
        m_table->horizontalHeader()->setSectionResizeMode(
            1, QHeaderView::Stretch);
        connect(m_table, &QTableWidget::itemSelectionChanged, this,
                &Papelera::syncButtons);
        m_stack->addWidget(m_table);

        m_emptyView = new QLabel(
            QStringLiteral("La papelera está vacía."), this);
        m_emptyView->setAlignment(Qt::AlignCenter);
        m_emptyView->setProperty("secondary", true);
        m_stack->addWidget(m_emptyView);

        auto *wrap = new QVBoxLayout;
        wrap->setContentsMargins(14, 10, 14, 14);
        wrap->addWidget(m_stack);
        root->addLayout(wrap, 1);

        reload();
    }

private slots:
    void restoreSel()
    {
        const int row = currentRow();
        if (m_demo || row < 0)
            return;
        const TrashItem it = m_items[row];
        const QString target = it.origPath;
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (QFileInfo::exists(target)) {
            castalia::playSound(m_repo, castalia::Sound::Error);
            QMessageBox::warning(this, QStringLiteral("Restaurar"),
                QStringLiteral("Ya existe un archivo en:\n%1").arg(target));
            return;
        }
        if (QFile::rename(it.filePath, target)) {
            QFile::remove(it.infoPath);
            reload();
        } else {
            castalia::playSound(m_repo, castalia::Sound::Error);
            QMessageBox::warning(this, QStringLiteral("Restaurar"),
                QStringLiteral("No se pudo restaurar «%1».").arg(it.name));
        }
    }

    void deleteSel()
    {
        const int row = currentRow();
        if (m_demo || row < 0)
            return;
        const TrashItem it = m_items[row];
        if (QMessageBox::question(this, QStringLiteral("Eliminar"),
                QStringLiteral("¿Eliminar «%1» de forma permanente?")
                    .arg(it.name)) != QMessageBox::Yes)
            return;
        removePath(it.filePath);
        QFile::remove(it.infoPath);
        reload();
    }

    void emptyAll()
    {
        if (m_demo || m_items.isEmpty())
            return;
        if (QMessageBox::question(this, QStringLiteral("Vaciar papelera"),
                QStringLiteral("¿Vaciar la papelera? Se eliminarán %1 "
                               "elemento(s) de forma permanente.")
                    .arg(m_items.size())) != QMessageBox::Yes)
            return;
        qint64 freed = 0;
        for (const TrashItem &it : m_items) {
            freed += it.size;
            removePath(it.filePath);
            QFile::remove(it.infoPath);
        }
        const int count = m_items.size();
        // The palette has a sound for exactly this moment (§21.4)…
        castalia::playSound(m_repo, castalia::Sound::EmptyTrash);
        // …and §7.4 has a way to say it out loud, for the case the user
        // started the emptying and went to do something else.
        castalia::notify(
            QStringLiteral("Papelera de reciclaje"),
            QStringLiteral("Papelera vaciada"),
            QStringLiteral("%1 elemento(s) · %2 liberados")
                .arg(count)
                .arg(QLocale().formattedDataSize(
                    freed, 1, QLocale::DataSizeTraditionalFormat)),
            QStringLiteral("trash"));
        reload();
    }

    void syncButtons()
    {
        const bool sel = currentRow() >= 0 && !m_demo;
        m_restore->setEnabled(sel);
        m_delete->setEnabled(sel);
    }

private:
    QPushButton *mkButton(const QString &text, const QString &objName)
    {
        auto *b = new QPushButton(text, this);
        if (!objName.isEmpty())
            b->setObjectName(objName);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    }
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }
    int currentRow() const
    {
        const auto sel = m_table->selectionModel()->selectedRows();
        return sel.isEmpty() ? -1 : sel.first().row();
    }

    void reload()
    {
        m_items = m_demo ? demoTrash() : readTrash();
        m_table->setRowCount(0);
        qint64 total = 0;
        const QLocale loc;
        for (const TrashItem &it : m_items) {
            const int r = m_table->rowCount();
            m_table->insertRow(r);
            m_table->setItem(r, 0, new QTableWidgetItem(
                castalia::themeIcon(m_repo, QStringLiteral("trash")), it.name));
            m_table->setItem(r, 1, new QTableWidgetItem(it.origPath));
            m_table->setItem(r, 2, new QTableWidgetItem(
                it.deleted.isValid()
                    ? loc.toString(it.deleted, QLocale::ShortFormat)
                    : QStringLiteral("—")));
            auto *sz = new QTableWidgetItem(humanSize(it.size));
            sz->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(r, 3, sz);
            total += it.size;
        }
        m_table->setColumnWidth(0, 180);
        m_table->setColumnWidth(2, 130);
        m_table->setColumnWidth(3, 90);

        const bool has = !m_items.isEmpty();
        m_stack->setCurrentWidget(has ? static_cast<QWidget *>(m_table)
                                      : m_emptyView);
        m_empty->setEnabled(has && !m_demo);
        syncButtons();

        const QString demoTag = m_demo
            ? QStringLiteral(" · vista de ejemplo") : QString();
        m_title->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>Papelera "
            "de reciclaje</span>&nbsp;&nbsp;<span style='color:%1'>%2%3</span>")
            .arg(colorTok("titlebar_text"),
                 has ? QStringLiteral("%1 elemento(s) · %2")
                           .arg(m_items.size()).arg(humanSize(total))
                     : QStringLiteral("vacía"),
                 demoTag));
    }

    QString m_repo;
    ThemeTokens m_tokens;
    bool m_demo = false;
    QList<TrashItem> m_items;
    QLabel *m_title = nullptr, *m_emptyView = nullptr;
    QStackedWidget *m_stack = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_restore = nullptr, *m_delete = nullptr, *m_empty = nullptr;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-papelera"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Show a representative read-only bin")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const QString accent =
        ThemeTokens::load(castalia::themeConfPath(repo, themeId))
            .str(QStringLiteral("colors"), QStringLiteral("accent"));
    const ThemeTokens tokens = castalia::applyTheme(
        &app, repo, themeId,
        QStringLiteral("#PpEmpty:enabled{font-weight:bold;border-color:%1;}")
            .arg(accent));

    Papelera w(repo, tokens, cli.isSet(QStringLiteral("demo")));
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
