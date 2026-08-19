// castalia-migrar — the Castalia Migration Assistant (Bible §9, §14.6).
//
// §9 MVP: "mount an NTFS disk, copy Documents/Pictures/Music/Desktop/
// Favorites to the new home", with "read-only source; never writes the old
// disk; clear progress". This is the app that makes an old machine's contents
// survive the change of operating system, and the person running it has
// exactly one copy of those photographs.
//
// So the guarantee is structural, not a promise in a dialog:
//
//   * the Windows disk is mounted **read-only** (`mount -o ro`), and if that
//     fails the app stops rather than retrying without the flag;
//   * every copy is one-way, into the user's home, and never the reverse;
//   * rsync is asked not to delete anything (no --delete, ever) and to skip
//     files that already exist and are newer.
//
// Windows moved the user's folders between releases — XP put them under
// "Documents and Settings\<user>\My Documents", Vista and later under
// "Users\<user>\Documents" — and Spanish installations translated the folder
// names. All of that is one pure lookup table, gated by --selftest, because
// the failure mode is silent: a table that misses "Mis documentos" copies
// nothing and reports success.
//
// Usage: castalia-migrar [--theme id] [--repo PATH] [--demo] [--selftest]
//                        [--screenshot out.png]

#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

#include "Blocks.h"
#include "Theme.h"

namespace {

using castalia::blocks::Block;
using castalia::blocks::humanBytes;

// One folder we know how to bring across: the names Windows used for it, and
// where it lands in a Castalia home.
struct FolderRule {
    const char *destination;      // Spanish home folder
    const char *names[6];         // the Windows names, English and Spanish
};

// XP and later, English and Spanish. A name that is missing here is a folder
// that silently does not migrate, which is why the table is tested.
const FolderRule kFolders[] = {
    {"Documentos", {"Documents", "My Documents", "Mis documentos",
                    "Documentos", nullptr, nullptr}},
    {"Imágenes",   {"Pictures", "My Pictures", "Mis imágenes",
                    "Imágenes", nullptr, nullptr}},
    {"Música",     {"Music", "My Music", "Mi música", "Música",
                    nullptr, nullptr}},
    {"Vídeos",     {"Videos", "My Videos", "Mis vídeos", "Vídeos",
                    nullptr, nullptr}},
    {"Escritorio", {"Desktop", "Escritorio", nullptr, nullptr, nullptr,
                    nullptr}},
    {"Favoritos",  {"Favorites", "Favoritos", nullptr, nullptr, nullptr,
                    nullptr}},
};

// One thing to copy: where it is now and where it will be.
struct Copy {
    QString label;       // "Documentos"
    QString source;      // /media/windows/Users/dave/Documents
    QString destination; // /home/dave/Documentos
};

QString which(const QString &bin)
{
    return QStandardPaths::findExecutable(bin);
}

// A partition worth offering: it holds a Windows filesystem. NTFS is the
// usual answer; a FAT32 partition on a 2000s machine is often the data drive,
// so it is offered too rather than quietly withheld.
bool looksLikeWindows(const Block &b)
{
    if (b.type != QStringLiteral("part"))
        return false;
    const QString fs = b.fstype.toLower();
    return fs == QStringLiteral("ntfs") || fs == QStringLiteral("vfat")
        || fs == QStringLiteral("exfat");
}

// Where a mounted Windows disk keeps its user profiles, in the order they
// appeared: XP first, because that is the machine this OS is aimed at.
QStringList profileRoots()
{
    return {QStringLiteral("Documents and Settings"), QStringLiteral("Users"),
            QStringLiteral("WINDOWS/Profiles")};
}

// The user profiles on a mounted Windows disk. Skips the accounts Windows
// creates for itself — offering "All Users" as a person to migrate is how a
// user ends up copying 4 GB of nothing.
QStringList windowsUsers(const QString &root, const QStringList &entries)
{
    Q_UNUSED(root);
    static const QStringList skip = {
        QStringLiteral("all users"), QStringLiteral("default user"),
        QStringLiteral("default"), QStringLiteral("public"),
        QStringLiteral("localservice"), QStringLiteral("networkservice"),
        QStringLiteral("systemprofile"), QStringLiteral("todos los usuarios"),
        QStringLiteral("usuario predeterminado"),
    };
    QStringList out;
    for (const QString &entry : entries) {
        if (entry.startsWith(QLatin1Char('.')))
            continue;
        if (skip.contains(entry.toLower()))
            continue;
        out.append(entry);
    }
    return out;
}

// What we would copy for one profile: only the folders that are actually
// there. Pure so the table above can be tested without a Windows disk.
QVector<Copy> planCopies(const QString &profileDir, const QString &home,
                         const QStringList &present)
{
    QVector<Copy> out;
    for (const FolderRule &rule : kFolders) {
        for (const char *name : rule.names) {
            if (!name)
                break;
            const QString candidate = QString::fromUtf8(name);
            // Windows filesystems are case-insensitive; the mount may not be.
            QString found;
            for (const QString &entry : present) {
                if (entry.compare(candidate, Qt::CaseInsensitive) == 0) {
                    found = entry;
                    break;
                }
            }
            if (found.isEmpty())
                continue;
            Copy c;
            c.label = QString::fromUtf8(rule.destination);
            c.source = QStringLiteral("%1/%2").arg(profileDir, found);
            c.destination = QStringLiteral("%1/%2").arg(home, c.label);
            out.append(c);
            break;                 // one source per destination, the first
        }
    }
    return out;
}

// The rsync arguments. Written as a pure function because the safety of this
// app is in this list: no --delete, and the source is only ever read.
QStringList rsyncArgs(const Copy &c)
{
    return {QStringLiteral("-a"),          // keep timestamps and structure
            QStringLiteral("--update"),    // never overwrite something newer
            QStringLiteral("--info=progress2"),
            QStringLiteral("--no-inc-recursive"),
            // Trailing slash: copy the *contents* into the destination, so
            // Documents/ does not become Documentos/Documents/.
            c.source + QStringLiteral("/"),
            c.destination};
}

// rsync --info=progress2 prints "   1,234,567  42%  1.23MB/s    0:00:03";
// the percentage is the second field. Returns -1 for a line that carries no
// progress, which is most of them.
int parseRsyncProgress(const QString &line)
{
    const QStringList fields = line.simplified().split(QLatin1Char(' '));
    for (const QString &field : fields) {
        if (!field.endsWith(QLatin1Char('%')))
            continue;
        bool ok = false;
        const int value = field.left(field.size() - 1).toInt(&ok);
        if (ok && value >= 0 && value <= 100)
            return value;
    }
    return -1;
}

QVector<Block> demoPartitions()
{
    Block c;
    c.name = QStringLiteral("sda1");
    c.path = QStringLiteral("/dev/sda1");
    c.type = QStringLiteral("part");
    c.fstype = QStringLiteral("ntfs");
    c.label = QStringLiteral("WINDOWS");
    c.size = 120034123776LL;
    Block d;
    d.name = QStringLiteral("sda2");
    d.path = QStringLiteral("/dev/sda2");
    d.type = QStringLiteral("part");
    d.fstype = QStringLiteral("ntfs");
    d.label = QStringLiteral("DATOS");
    d.size = 40034123776LL;
    return {c, d};
}

QString describe(const Block &b)
{
    const QString label = b.label.isEmpty() ? b.name : b.label;
    return QStringLiteral("%1 — %2 (%3, %4)")
        .arg(label, b.path, b.fstype.toUpper(), humanBytes(b.size));
}

// --- head-less self-test (Bible §17.4) -----------------------------------
int selftest()
{
    int failures = 0;
    auto check = [&failures](bool ok, const char *what) {
        if (!ok) {
            QTextStream(stderr) << "migrar-selftest: FAIL " << what << '\n';
            ++failures;
        }
    };

    Block ntfs;
    ntfs.type = QStringLiteral("part");
    ntfs.fstype = QStringLiteral("ntfs");
    check(looksLikeWindows(ntfs), "an NTFS partition is offered");
    ntfs.fstype = QStringLiteral("NTFS");
    check(looksLikeWindows(ntfs), "…whatever the case lsblk reports");
    ntfs.fstype = QStringLiteral("vfat");
    check(looksLikeWindows(ntfs), "a FAT32 data drive is offered too");
    ntfs.fstype = QStringLiteral("ext4");
    check(!looksLikeWindows(ntfs), "a Linux partition is not a Windows one");
    ntfs.fstype = QStringLiteral("ntfs");
    ntfs.type = QStringLiteral("disk");
    check(!looksLikeWindows(ntfs), "a whole disk is not a partition");

    // Profile discovery, and the accounts Windows creates for itself.
    const QStringList entries = {QStringLiteral("Dave"),
                                 QStringLiteral("All Users"),
                                 QStringLiteral("Default User"),
                                 QStringLiteral("Todos los usuarios"),
                                 QStringLiteral("Claudio"),
                                 QStringLiteral(".DS_Store")};
    const QStringList users =
        windowsUsers(QStringLiteral("/mnt/win"), entries);
    check(users.size() == 2, "only the real people are offered");
    check(users.contains(QStringLiteral("Dave"))
              && users.contains(QStringLiteral("Claudio")),
          "…and both of them are");
    check(!users.contains(QStringLiteral("All Users")),
          "All Users is not a person");
    check(!users.contains(QStringLiteral("Todos los usuarios")),
          "…in Spanish either");

    check(profileRoots().contains(QStringLiteral("Documents and Settings")),
          "XP's profile root is looked for");
    check(profileRoots().contains(QStringLiteral("Users")),
          "…and Vista's");
    check(profileRoots().indexOf(QStringLiteral("Documents and Settings"))
              < profileRoots().indexOf(QStringLiteral("Users")),
          "XP first: it is the machine this OS is for");

    // The folder table — the silent failure mode this app has.
    const QVector<Copy> xp = planCopies(
        QStringLiteral("/mnt/win/Documents and Settings/Dave"),
        QStringLiteral("/home/dave"),
        {QStringLiteral("My Documents"), QStringLiteral("My Pictures"),
         QStringLiteral("Desktop"), QStringLiteral("Favorites"),
         QStringLiteral("Application Data")});
    check(xp.size() == 4, "an XP profile brings four folders across");
    check(xp.at(0).label == QStringLiteral("Documentos"),
          "My Documents lands in Documentos");
    check(xp.at(0).source.endsWith(QStringLiteral("Dave/My Documents")),
          "…from where it actually is");
    check(xp.at(0).destination == QStringLiteral("/home/dave/Documentos"),
          "…into the Spanish home folder");
    bool copiesAppData = false;
    for (const Copy &c : xp)
        if (c.source.contains(QStringLiteral("Application Data")))
            copiesAppData = true;
    check(!copiesAppData, "Windows' own data folders are left behind");

    const QVector<Copy> spanish = planCopies(
        QStringLiteral("/mnt/win/Documents and Settings/Dave"),
        QStringLiteral("/home/dave"),
        {QStringLiteral("Mis documentos"), QStringLiteral("Mis imágenes"),
         QStringLiteral("Mi música")});
    check(spanish.size() == 3,
          "a Spanish Windows migrates just as well as an English one");
    check(spanish.at(0).destination == QStringLiteral("/home/dave/Documentos"),
          "Mis documentos lands in Documentos");
    check(spanish.at(2).label == QStringLiteral("Música"),
          "Mi música lands in Música");

    const QVector<Copy> modern = planCopies(
        QStringLiteral("/mnt/win/Users/Dave"), QStringLiteral("/home/dave"),
        {QStringLiteral("documents"), QStringLiteral("PICTURES")});
    check(modern.size() == 2,
          "a case-insensitive filesystem still matches");
    check(planCopies(QStringLiteral("/x"), QStringLiteral("/home/d"),
                     {}).isEmpty(),
          "an empty profile plans no copies, rather than copying blindly");

    // The safety of the whole app is in these arguments.
    Copy c;
    c.source = QStringLiteral("/mnt/win/Users/Dave/Documents");
    c.destination = QStringLiteral("/home/dave/Documentos");
    const QStringList args = rsyncArgs(c);
    check(!args.contains(QStringLiteral("--delete")),
          "rsync is NEVER asked to delete anything");
    check(args.contains(QStringLiteral("--update")),
          "a newer file at the destination is never overwritten");
    check(args.at(args.size() - 2)
              == QStringLiteral("/mnt/win/Users/Dave/Documents/"),
          "the source keeps its trailing slash (contents, not the folder)");
    check(args.last() == c.destination,
          "the destination is the home folder, and it is last");
    // The old disk is the source and only the source.
    check(!args.last().startsWith(QStringLiteral("/mnt/win")),
          "nothing is ever written back to the Windows disk");

    check(parseRsyncProgress(
              QStringLiteral("      1,234,567  42%    1.23MB/s    0:00:03"))
              == 42,
          "rsync's percentage is read");
    check(parseRsyncProgress(QStringLiteral("sending incremental file list"))
              == -1,
          "a line with no progress reports none");
    check(parseRsyncProgress(QStringLiteral("  0  0%    0.00kB/s")) == 0,
          "0% is progress, not absence of it");
    check(parseRsyncProgress(QString()) == -1, "an empty line is not progress");

    check(demoPartitions().size() == 2, "the sample offers two disks");
    check(describe(demoPartitions().at(0))
              .contains(QStringLiteral("WINDOWS")),
          "a partition is described by its label");
    check(describe(demoPartitions().at(0)).contains(QStringLiteral("NTFS")),
          "…and its filesystem");

    QTextStream(stdout) << (failures == 0
        ? QStringLiteral("migrar-selftest: OK\n")
        : QStringLiteral("migrar-selftest: %1 failure(s)\n").arg(failures));
    return failures == 0 ? 0 : 1;
}

} // namespace

class Migrar : public QWidget {
    Q_OBJECT
public:
    Migrar(const QString &repo, const ThemeTokens &tokens, bool demo)
        : m_repo(repo), m_tokens(tokens), m_demo(demo)
    {
        setWindowTitle(QStringLiteral("Asistente de migración — Castalia"));
        resize(760, 520);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("MgHeader"));
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "#MgHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *icon = new QLabel(head);
        icon->setPixmap(castalia::themeIcon(m_repo, QStringLiteral("home"))
                            .pixmap(28, 28));
        hl->addWidget(icon);
        auto *title = new QLabel(QStringLiteral("Asistente de migración"),
                                 head);
        title->setStyleSheet(QStringLiteral("color:%1;font-size:15px;"
                                            "font-weight:bold;")
                                 .arg(colorTok("titlebar_text")));
        hl->addWidget(title);
        hl->addStretch(1);
        root->addWidget(head);

        auto *body = new QVBoxLayout;
        body->setContentsMargins(16, 14, 16, 8);
        body->setSpacing(10);

        auto *intro = new QLabel(QStringLiteral(
            "Trae tus documentos, imágenes, música y favoritos desde el disco "
            "de Windows de este equipo. El disco antiguo se monta en "
            "<b>solo lectura</b>: nunca se escribe nada en él."), this);
        intro->setWordWrap(true);
        body->addWidget(intro);

        auto *diskBox = new QGroupBox(QStringLiteral("1. Disco de Windows"),
                                      this);
        auto *dl = new QVBoxLayout(diskBox);
        m_disks = new QListWidget(diskBox);
        m_disks->setMaximumHeight(96);
        connect(m_disks, &QListWidget::currentRowChanged, this,
                [this]() { scanProfiles(); });
        dl->addWidget(m_disks);
        body->addWidget(diskBox);

        auto *userBox = new QGroupBox(QStringLiteral("2. Cuenta a migrar"),
                                      this);
        auto *ul = new QVBoxLayout(userBox);
        m_users = new QListWidget(userBox);
        m_users->setMaximumHeight(96);
        connect(m_users, &QListWidget::currentRowChanged, this,
                [this]() { updatePlan(); });
        ul->addWidget(m_users);
        body->addWidget(userBox);

        auto *planBox = new QGroupBox(QStringLiteral("3. Qué se copiará"),
                                      this);
        auto *pl = new QVBoxLayout(planBox);
        m_plan = new QListWidget(planBox);
        pl->addWidget(m_plan);
        body->addWidget(planBox, 1);

        m_progress = new QProgressBar(this);
        m_progress->setRange(0, 100);
        m_progress->setVisible(false);
        body->addWidget(m_progress);
        root->addLayout(body, 1);

        auto *bar = new QHBoxLayout;
        bar->setContentsMargins(16, 0, 16, 12);
        bar->setSpacing(8);
        m_note = new QLabel(this);
        m_note->setProperty("secondary", true);
        m_note->setWordWrap(true);
        bar->addWidget(m_note, 1);
        auto *rescan = new QPushButton(QStringLiteral("Buscar discos"), this);
        m_copy = new QPushButton(QStringLiteral("Copiar ahora"), this);
        for (QPushButton *b : {rescan, m_copy}) {
            b->setCursor(Qt::PointingHandCursor);
            bar->addWidget(b);
        }
        connect(rescan, &QPushButton::clicked, this, &Migrar::scanDisks);
        connect(m_copy, &QPushButton::clicked, this, &Migrar::copyNow);
        root->addLayout(bar);

        scanDisks();
    }

private slots:
    void scanDisks()
    {
        m_disks->clear();
        m_found.clear();
        const QVector<Block> parts = m_demo
            ? demoPartitions()
            : castalia::blocks::partitions(castalia::blocks::list());
        for (const Block &b : parts) {
            if (!looksLikeWindows(b))
                continue;
            m_found.append(b);
            m_disks->addItem(describe(b));
        }
        if (m_found.isEmpty()) {
            m_note->setText(QStringLiteral(
                "No se ha encontrado ningún disco de Windows (NTFS o FAT32) "
                "en este equipo."));
        } else {
            m_note->setText(QStringLiteral(
                "%1 partición(es) de Windows encontrada(s).")
                .arg(m_found.size()));
            m_disks->setCurrentRow(0);
        }
        updatePlan();
    }

    void scanProfiles()
    {
        m_users->clear();
        m_profileDir.clear();
        const int row = m_disks->currentRow();
        if (row < 0 || row >= m_found.size()) {
            updatePlan();
            return;
        }
        if (m_demo) {
            for (const QString &u : {QStringLiteral("Dave"),
                                     QStringLiteral("Claudio")})
                m_users->addItem(u);
            m_users->setCurrentRow(0);
            updatePlan();
            return;
        }
        const QString mount = mountReadOnly(m_found.at(row).path);
        if (mount.isEmpty()) {
            updatePlan();
            return;
        }
        for (const QString &rootName : profileRoots()) {
            QDir dir(QStringLiteral("%1/%2").arg(mount, rootName));
            if (!dir.exists())
                continue;
            const QStringList users = windowsUsers(
                dir.absolutePath(),
                dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name));
            if (users.isEmpty())
                continue;
            m_profileDir = dir.absolutePath();
            m_users->addItems(users);
            m_users->setCurrentRow(0);
            break;
        }
        if (m_users->count() == 0)
            m_note->setText(QStringLiteral(
                "El disco se ha montado, pero no se han encontrado perfiles "
                "de usuario de Windows en él."));
        updatePlan();
    }

    void updatePlan()
    {
        m_plan->clear();
        m_copies.clear();
        const QString home = QDir::homePath();
        if (m_demo && m_users->currentRow() >= 0) {
            m_copies = planCopies(
                QStringLiteral("/media/windows/Documents and Settings/%1")
                    .arg(m_users->currentItem()->text()),
                home,
                {QStringLiteral("My Documents"), QStringLiteral("My Pictures"),
                 QStringLiteral("My Music"), QStringLiteral("Desktop"),
                 QStringLiteral("Favorites")});
        } else if (!m_profileDir.isEmpty() && m_users->currentRow() >= 0) {
            const QString profile = QStringLiteral("%1/%2")
                .arg(m_profileDir, m_users->currentItem()->text());
            m_copies = planCopies(
                profile, home,
                QDir(profile).entryList(QDir::Dirs | QDir::NoDotAndDotDot));
        }
        for (const Copy &c : m_copies)
            m_plan->addItem(QStringLiteral("%1  →  %2")
                                .arg(c.source, c.destination));
        m_copy->setEnabled(!m_copies.isEmpty());
        if (m_copies.isEmpty() && !m_found.isEmpty()
            && m_users->currentRow() >= 0)
            m_plan->addItem(QStringLiteral(
                "(esta cuenta no tiene ninguna de las carpetas conocidas)"));
    }

    void copyNow()
    {
        if (m_copies.isEmpty())
            return;
        const QString rsync = which(QStringLiteral("rsync"));
        if (rsync.isEmpty()) {
            m_note->setText(QStringLiteral(
                "Falta rsync: no se puede copiar de forma segura."));
            return;
        }
        m_copy->setEnabled(false);
        m_progress->setVisible(true);
        int done = 0;
        for (const Copy &c : m_copies) {
            QDir().mkpath(c.destination);
            m_note->setText(QStringLiteral("Copiando %1…").arg(c.label));
            QApplication::processEvents();
            QProcess p;
            p.start(rsync, rsyncArgs(c));
            while (p.state() != QProcess::NotRunning) {
                p.waitForReadyRead(200);
                const QString chunk =
                    QString::fromUtf8(p.readAllStandardOutput());
                for (const QString &line : chunk.split(QLatin1Char('\r'))) {
                    const int pct = parseRsyncProgress(line);
                    if (pct >= 0)
                        m_progress->setValue(
                            (done * 100 + pct) / m_copies.size());
                }
                QApplication::processEvents();
            }
            ++done;
            m_progress->setValue(done * 100 / m_copies.size());
        }
        m_progress->setValue(100);
        m_note->setText(QStringLiteral(
            "Listo: %1 carpeta(s) copiadas. El disco de Windows no se ha "
            "modificado.").arg(m_copies.size()));
        m_copy->setEnabled(true);
    }

private:
    // Mount the Windows partition read-only. If the read-only mount fails we
    // stop: retrying without `ro` is exactly the mistake this app exists to
    // not make.
    QString mountReadOnly(const QString &device)
    {
        if (m_mounts.contains(device))
            return m_mounts.value(device);
        const QString mount = which(QStringLiteral("mount"));
        if (mount.isEmpty()) {
            m_note->setText(QStringLiteral("No hay «mount» en este sistema."));
            return QString();
        }
        auto *dir = new QTemporaryDir;
        if (!dir->isValid()) {
            delete dir;
            return QString();
        }
        QProcess p;
        p.start(mount, {QStringLiteral("-o"), QStringLiteral("ro"),
                        device, dir->path()});
        p.waitForFinished(15000);
        if (p.exitCode() != 0) {
            m_note->setText(QStringLiteral(
                "No se pudo montar %1 en solo lectura: %2")
                .arg(device,
                     QString::fromUtf8(p.readAllStandardError()).trimmed()));
            delete dir;
            return QString();
        }
        m_temporaries.append(dir);
        m_mounts.insert(device, dir->path());
        return dir->path();
    }

    QString colorTok(const char *key) const
    {
        const QColor c = m_tokens.color(QLatin1String(key));
        return c.isValid() ? c.name() : QStringLiteral("#2C6699");
    }

    QString m_repo;
    ThemeTokens m_tokens;
    bool m_demo = false;
    QVector<Block> m_found;
    QVector<Copy> m_copies;
    QString m_profileDir;
    QHash<QString, QString> m_mounts;
    QVector<QTemporaryDir *> m_temporaries;
    QListWidget *m_disks = nullptr, *m_users = nullptr, *m_plan = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_note = nullptr;
    QPushButton *m_copy = nullptr;
};

namespace {

void addOptions(QCommandLineParser &cli)
{
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Show a representative sample migration")});
    cli.addOption({QStringLiteral("selftest"),
                   QStringLiteral("Run the head-less folder-map self-test")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
}

} // namespace

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) != "--selftest")
            continue;
        QCoreApplication app(argc, argv);
        QCommandLineParser cli;
        addOptions(cli);
        cli.process(app);
        return selftest();
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-migrar"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    addOptions(cli);
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const ThemeTokens tokens = castalia::applyTheme(
        &app, repo, cli.value(QStringLiteral("theme")));

    Migrar w(repo, tokens, cli.isSet(QStringLiteral("demo")));
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
