// castalia-discos — the Castalia Disk Manager (Bible §9, §6.14,
// §10 XP-parity "Disk Management").
//
// §9 MVP: "view disks/partitions, mount/unmount, format removable", with
// "destructive ops gated by typed confirm". This app can erase a disk, so
// the interesting design is not the tree — it is everything that stands
// between a user and losing their photographs:
//
//   * the format button only ever lights up for a **removable** device that
//     is not mounted and is not the medium this session booted from;
//   * and even then it is disabled until the user types the device path
//     exactly, the same gate the installer uses (§14.5 #1). "Are you sure?"
//     is not a gate; typing /dev/sdb1 is.
//
// Everything it reads comes from `lsblk --json`, which is on every machine
// that has util-linux and gives a stable, parseable view of the block layer.
// Mount and unmount go through `udisksctl` when it is there (no root needed
// for removable media, the desktop way) and fall back to mount/umount.
//
// The parser and the safety rules are pure functions, so --selftest gates
// them head-lessly in CI where there is no removable disk to experiment on
// — and where you would not want an experiment anyway.
//
// Usage: castalia-discos [--theme id] [--repo PATH] [--demo] [--selftest]
//                        [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVector>

#include "Blocks.h"
#include "Theme.h"

namespace {

using castalia::blocks::Block;
using castalia::blocks::humanBytes;
using castalia::blocks::parseLsblk;

QString which(const QString &bin)
{
    return QStandardPaths::findExecutable(bin);
}

// Mount points that mean "this is the system you are running", so nothing
// here may be formatted however removable it claims to be. /run/live/medium
// is the ISO you booted; erasing it mid-session is a special kind of bad.
bool isSystemMount(const QString &mountpoint)
{
    if (mountpoint.isEmpty())
        return false;
    static const QStringList roots = {
        QStringLiteral("/"), QStringLiteral("/boot"),
        QStringLiteral("/boot/efi"), QStringLiteral("/home"),
        QStringLiteral("/usr"), QStringLiteral("/var"),
        QStringLiteral("/run/live/medium"), QStringLiteral("/run/live/rootfs"),
    };
    return roots.contains(mountpoint);
}

// Why a device may not be formatted — the empty string means it may. The
// message is what the user sees, so it says what to do about it.
QString formatRefusal(const Block &b)
{
    if (b.type == QStringLiteral("disk") && !b.children.isEmpty())
        return QStringLiteral(
            "Este disco tiene particiones. Selecciona una partición: dar "
            "formato al disco entero no está en esta versión.");
    if (b.type != QStringLiteral("part") && b.type != QStringLiteral("disk"))
        return QStringLiteral("Solo se puede dar formato a discos y "
                              "particiones.");
    if (b.readOnly)
        return QStringLiteral("El dispositivo está protegido contra "
                              "escritura.");
    if (isSystemMount(b.mountpoint))
        return QStringLiteral("Es parte del sistema en uso (%1).")
            .arg(b.mountpoint);
    if (!b.mountpoint.isEmpty())
        return QStringLiteral("Está montado en %1. Desmóntalo primero.")
            .arg(b.mountpoint);
    if (!b.removable)
        return QStringLiteral(
            "No es un dispositivo extraíble. Esta versión solo da formato a "
            "medios extraíbles (USB, tarjetas).");
    return QString();
}

bool canFormat(const Block &b)
{
    return formatRefusal(b).isEmpty();
}

// The typed gate (§14.5 #1). Nothing clever: the user types the path of the
// thing that is about to be erased, and it has to match exactly.
bool confirmationMatches(const QString &typed, const QString &path)
{
    return !path.isEmpty() && typed.trimmed() == path;
}

QString mountStatus(const Block &b)
{
    if (!b.mountpoint.isEmpty())
        return QStringLiteral("Montado en %1").arg(b.mountpoint);
    if (b.type == QStringLiteral("disk"))
        return QString();
    return b.fstype.isEmpty() ? QStringLiteral("Sin formato")
                              : QStringLiteral("Sin montar");
}

QString describe(const Block &b)
{
    if (b.type == QStringLiteral("disk")) {
        QString text = b.model.isEmpty() ? b.name : b.model;
        if (b.transport == QStringLiteral("usb"))
            text += QStringLiteral(" (USB)");
        if (b.removable)
            text += QStringLiteral(" · extraíble");
        return text;
    }
    if (!b.label.isEmpty())
        return QStringLiteral("%1 (%2)").arg(b.label, b.name);
    return b.name;
}

// The sample for --demo and the offscreen render gate: a real machine's
// disks differ every time, and a screenshot has to be the same twice.
QVector<Block> demoDisks()
{
    Block hd;
    hd.name = QStringLiteral("sda");
    hd.path = QStringLiteral("/dev/sda");
    hd.type = QStringLiteral("disk");
    hd.model = QStringLiteral("ST3160815AS");
    hd.transport = QStringLiteral("sata");
    hd.size = 160041885696LL;
    Block p1;
    p1.name = QStringLiteral("sda1");
    p1.path = QStringLiteral("/dev/sda1");
    p1.type = QStringLiteral("part");
    p1.fstype = QStringLiteral("ext4");
    p1.label = QStringLiteral("castalia");
    p1.mountpoint = QStringLiteral("/");
    p1.size = 155041885696LL;
    Block p2;
    p2.name = QStringLiteral("sda2");
    p2.path = QStringLiteral("/dev/sda2");
    p2.type = QStringLiteral("part");
    p2.fstype = QStringLiteral("swap");
    p2.size = 5000000000LL;
    hd.children = {p1, p2};

    Block usb;
    usb.name = QStringLiteral("sdb");
    usb.path = QStringLiteral("/dev/sdb");
    usb.type = QStringLiteral("disk");
    usb.model = QStringLiteral("SanDisk Cruzer");
    usb.transport = QStringLiteral("usb");
    usb.removable = true;
    usb.size = 8004304896LL;
    Block u1;
    u1.name = QStringLiteral("sdb1");
    u1.path = QStringLiteral("/dev/sdb1");
    u1.type = QStringLiteral("part");
    u1.fstype = QStringLiteral("vfat");
    u1.label = QStringLiteral("COPIAS");
    u1.removable = true;
    u1.size = 8004304896LL;
    usb.children = {u1};

    return {hd, usb};
}

// --- head-less self-test (Bible §17.4) -----------------------------------
int selftest()
{
    int failures = 0;
    auto check = [&failures](bool ok, const char *what) {
        if (!ok) {
            QTextStream(stderr) << "discos-selftest: FAIL " << what << '\n';
            ++failures;
        }
    };

    // A real `lsblk -J -b` reply, including the two shapes util-linux has
    // used for the same fields across releases.
    const QByteArray json = R"({
      "blockdevices": [
        {"name":"sda","path":"/dev/sda","size":160041885696,"type":"disk",
         "rm":false,"ro":false,"model":"ST3160815AS","tran":"sata",
         "children":[
           {"name":"sda1","path":"/dev/sda1","size":"155041885696",
            "type":"part","fstype":"ext4","label":"castalia",
            "mountpoint":"/","rm":"0"},
           {"name":"sda2","path":"/dev/sda2","size":5000000000,
            "type":"part","fstype":"swap","mountpoint":null}]},
        {"name":"sdb","path":"/dev/sdb","size":8004304896,"type":"disk",
         "rm":true,"tran":"usb","model":"SanDisk Cruzer","children":[
           {"name":"sdb1","path":"/dev/sdb1","size":8004304896,"type":"part",
            "fstype":"vfat","label":"COPIAS","rm":true}]}]
    })";
    // The parser itself is libcastalia-ui's contract and is gated by its own
    // self-test; what this fixture is for is the *policy* below.
    const QVector<Block> disks = parseLsblk(json);
    check(disks.size() == 2, "the fixture parses into two disks");

    // The safety rules — the part of this app that matters.
    check(isSystemMount(QStringLiteral("/")), "/ is the system");
    check(isSystemMount(QStringLiteral("/run/live/medium")),
          "the live medium is the system you are running from");
    check(!isSystemMount(QStringLiteral("/media/usb")),
          "an ordinary mount point is not the system");

    const Block root = disks.at(0).children.at(0);
    check(!canFormat(root), "the mounted root partition may not be formatted");
    check(formatRefusal(root).contains(QStringLiteral("sistema en uso")),
          "…and it says why");
    check(!canFormat(disks.at(0)),
          "a disk with partitions is not formatted whole");
    Block swap = disks.at(0).children.at(1);
    check(!canFormat(swap),
          "an unmounted internal partition is still not removable");
    check(formatRefusal(swap).contains(QStringLiteral("extraíble")),
          "…and it says that too");

    Block stick = disks.at(1).children.at(0);
    check(canFormat(stick), "an unmounted removable partition may be formatted");
    stick.mountpoint = QStringLiteral("/media/usb");
    check(!canFormat(stick), "…but not while it is mounted");
    check(formatRefusal(stick).contains(QStringLiteral("Desmóntalo")),
          "…and it says what to do about it");
    stick.mountpoint.clear();
    stick.readOnly = true;
    check(!canFormat(stick), "…nor when it is write-protected");

    // The typed gate.
    check(confirmationMatches(QStringLiteral("/dev/sdb1"),
                              QStringLiteral("/dev/sdb1")),
          "the exact path unlocks it");
    check(confirmationMatches(QStringLiteral("  /dev/sdb1  "),
                              QStringLiteral("/dev/sdb1")),
          "surrounding whitespace is forgiven");
    check(!confirmationMatches(QStringLiteral("/dev/sdb"),
                               QStringLiteral("/dev/sdb1")),
          "a prefix is not the path");
    check(!confirmationMatches(QStringLiteral("sdb1"),
                               QStringLiteral("/dev/sdb1")),
          "the name alone is not the path");
    check(!confirmationMatches(QStringLiteral("si"),
                               QStringLiteral("/dev/sdb1")),
          "\"yes\" is not a confirmation");
    check(!confirmationMatches(QString(), QString()),
          "an empty gate never unlocks");

    check(humanBytes(160041885696LL) == QStringLiteral("149.1 GiB"),
          "a 160 GB disk reads as its real capacity");
    check(humanBytes(0) == QStringLiteral("—"), "unknown size reads as a dash");

    check(mountStatus(root) == QStringLiteral("Montado en /"),
          "a mounted partition says where");
    Block blank;
    blank.type = QStringLiteral("part");
    check(mountStatus(blank) == QStringLiteral("Sin formato"),
          "an unformatted partition says so");

    check(describe(disks.at(1)).contains(QStringLiteral("USB")),
          "a USB disk is labelled as one");
    check(describe(disks.at(1)).contains(QStringLiteral("extraíble")),
          "…and as removable");
    check(describe(stick).startsWith(QStringLiteral("COPIAS")),
          "a labelled partition leads with its label");

    check(demoDisks().size() == 2, "the sample has an internal disk and a stick");

    QTextStream(stdout) << (failures == 0
        ? QStringLiteral("discos-selftest: OK\n")
        : QStringLiteral("discos-selftest: %1 failure(s)\n").arg(failures));
    return failures == 0 ? 0 : 1;
}

} // namespace

class DiskManager : public QWidget {
    Q_OBJECT
public:
    DiskManager(const QString &repo, const ThemeTokens &tokens, bool demo)
        : m_repo(repo), m_tokens(tokens), m_demo(demo)
    {
        setWindowTitle(QStringLiteral("Administrador de discos — Castalia"));
        resize(840, 520);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("DkHeader"));
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "#DkHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *icon = new QLabel(head);
        icon->setPixmap(castalia::themeIcon(m_repo, QStringLiteral("disk"))
                            .pixmap(28, 28));
        hl->addWidget(icon);
        auto *title = new QLabel(QStringLiteral("Administrador de discos"),
                                 head);
        title->setStyleSheet(QStringLiteral("color:%1;font-size:15px;"
                                            "font-weight:bold;")
                                 .arg(colorTok("titlebar_text")));
        hl->addWidget(title);
        hl->addStretch(1);
        root->addWidget(head);

        m_tree = new QTreeWidget(this);
        m_tree->setColumnCount(5);
        m_tree->setHeaderLabels({QStringLiteral("Dispositivo"),
                                 QStringLiteral("Tamaño"),
                                 QStringLiteral("Sistema de archivos"),
                                 QStringLiteral("Estado"),
                                 QStringLiteral("Ruta")});
        // The device name is the wide column: a label plus its partition
        // name does not fit in whatever is left over after four narrow ones.
        m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
        m_tree->header()->resizeSection(0, 250);
        for (int c = 1; c < 4; ++c)
            m_tree->header()->setSectionResizeMode(
                c, QHeaderView::ResizeToContents);
        m_tree->header()->setStretchLastSection(true);
        m_tree->setAlternatingRowColors(true);
        m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
        connect(m_tree, &QTreeWidget::currentItemChanged, this,
                [this]() { updateButtons(); });
        auto *wrap = new QVBoxLayout;
        wrap->setContentsMargins(14, 12, 14, 6);
        wrap->addWidget(m_tree, 1);
        root->addLayout(wrap, 1);

        auto *bar = new QHBoxLayout;
        bar->setContentsMargins(14, 0, 14, 12);
        bar->setSpacing(8);
        m_note = new QLabel(this);
        m_note->setProperty("secondary", true);
        m_note->setWordWrap(true);
        bar->addWidget(m_note, 1);
        m_mount = new QPushButton(QStringLiteral("Montar"), this);
        m_unmount = new QPushButton(QStringLiteral("Desmontar"), this);
        m_format = new QPushButton(QStringLiteral("Dar formato…"), this);
        auto *refresh = new QPushButton(QStringLiteral("Actualizar"), this);
        for (QPushButton *b : {m_mount, m_unmount, m_format, refresh}) {
            b->setCursor(Qt::PointingHandCursor);
            bar->addWidget(b);
        }
        connect(m_mount, &QPushButton::clicked, this,
                [this]() { mountAction(true); });
        connect(m_unmount, &QPushButton::clicked, this,
                [this]() { mountAction(false); });
        connect(m_format, &QPushButton::clicked, this,
                &DiskManager::formatAction);
        connect(refresh, &QPushButton::clicked, this, &DiskManager::reload);
        root->addLayout(bar);

        reload();
    }

private slots:
    void reload()
    {
        m_disks = m_demo ? demoDisks() : readDisks();
        m_tree->clear();
        for (const Block &disk : m_disks) {
            auto *item = addRow(nullptr, disk);
            item->setExpanded(true);
            for (const Block &part : disk.children)
                addRow(item, part);
        }
        m_note->setText(m_disks.isEmpty()
            ? QStringLiteral("No se ha podido leer la lista de discos "
                             "(¿falta lsblk?).")
            : QStringLiteral("%1 disco(s). El formato solo se ofrece para "
                             "medios extraíbles sin montar.")
                  .arg(m_disks.size()));
        updateButtons();
    }

    void updateButtons()
    {
        const Block *b = current();
        const bool mounted = b && !b->mountpoint.isEmpty();
        m_mount->setEnabled(b && !mounted
                            && b->type == QStringLiteral("part"));
        m_unmount->setEnabled(b && mounted && !isSystemMount(b->mountpoint));
        m_format->setEnabled(b && canFormat(*b));
        if (b && !canFormat(*b) && b->type != QStringLiteral("disk"))
            m_format->setToolTip(formatRefusal(*b));
        else
            m_format->setToolTip(QString());
    }

    void mountAction(bool mount)
    {
        const Block *b = current();
        if (!b)
            return;
        const QString udisks = which(QStringLiteral("udisksctl"));
        QString bin;
        QStringList args;
        if (!udisks.isEmpty()) {
            bin = udisks;
            args = QStringList{
                mount ? QStringLiteral("mount") : QStringLiteral("unmount"),
                QStringLiteral("-b"), b->path};
        } else {
            bin = which(mount ? QStringLiteral("mount")
                              : QStringLiteral("umount"));
            args = QStringList{b->path};
        }
        if (bin.isEmpty()) {
            m_note->setText(QStringLiteral(
                "No hay ninguna herramienta para montar en este sistema."));
            return;
        }
        QProcess p;
        p.start(bin, args);
        p.waitForFinished(15000);
        const QString err = QString::fromUtf8(p.readAllStandardError())
                                .trimmed();
        m_note->setText(p.exitCode() == 0
            ? QStringLiteral("%1: %2").arg(b->path,
                  mount ? QStringLiteral("montado") : QStringLiteral("desmontado"))
            : QStringLiteral("No se pudo: %1").arg(
                  err.isEmpty() ? QStringLiteral("error desconocido") : err));
        reload();
    }

    void formatAction()
    {
        const Block *b = current();
        if (!b || !canFormat(*b))
            return;
        const QString path = b->path;      // copied: reload() invalidates b
        const QString size = humanBytes(b->size);

        QStringList kinds = {QStringLiteral("FAT32 (compatible con todo)"),
                             QStringLiteral("ext4 (Linux)")};
        bool ok = false;
        const QString kind = QInputDialog::getItem(
            this, QStringLiteral("Dar formato"),
            QStringLiteral("Sistema de archivos para %1 (%2):")
                .arg(path, size),
            kinds, 0, false, &ok);
        if (!ok)
            return;

        // The gate: not "are you sure", but "type what is about to be
        // erased" (§14.5 #1). Everything on the device goes; the dialog says
        // so in those words.
        const QString typed = QInputDialog::getText(
            this, QStringLiteral("Confirmar el formato"),
            QStringLiteral(
                "Se borrará TODO el contenido de %1 (%2).\n"
                "Esto no se puede deshacer.\n\n"
                "Escribe la ruta exacta para confirmar:")
                .arg(path, size),
            QLineEdit::Normal, QString(), &ok);
        if (!ok || !confirmationMatches(typed, path)) {
            m_note->setText(QStringLiteral(
                "Formato cancelado: la ruta escrita no coincide con %1.")
                .arg(path));
            return;
        }

        const bool fat = kind.startsWith(QStringLiteral("FAT"));
        const QString bin = which(fat ? QStringLiteral("mkfs.vfat")
                                      : QStringLiteral("mkfs.ext4"));
        if (bin.isEmpty()) {
            m_note->setText(QStringLiteral(
                "Falta la herramienta de formato (%1).")
                .arg(fat ? QStringLiteral("mkfs.vfat")
                         : QStringLiteral("mkfs.ext4")));
            return;
        }
        QProcess p;
        p.start(bin, {path});
        p.waitForFinished(120000);
        m_note->setText(p.exitCode() == 0
            ? QStringLiteral("%1 formateado.").arg(path)
            : QStringLiteral("El formato de %1 falló: %2").arg(path,
                  QString::fromUtf8(p.readAllStandardError()).trimmed()));
        reload();
    }

private:
    QVector<Block> readDisks()
    {
        // Loop devices and empty optical drives are noise on a disk manager.
        QVector<Block> out;
        for (const Block &b : castalia::blocks::list())
            if (b.type == QStringLiteral("disk") && b.size > 0)
                out.append(b);
        return out;
    }

    QTreeWidgetItem *addRow(QTreeWidgetItem *parent, const Block &b)
    {
        auto *item = parent ? new QTreeWidgetItem(parent)
                            : new QTreeWidgetItem(m_tree);
        item->setText(0, describe(b));
        item->setText(1, humanBytes(b.size));
        item->setText(2, b.fstype);
        item->setText(3, mountStatus(b));
        item->setText(4, b.path);
        item->setData(0, Qt::UserRole, b.path);
        if (!parent) {
            item->setIcon(0, castalia::themeIcon(m_repo,
                                                 QStringLiteral("disk")));
            QFont bold = item->font(0);
            bold.setBold(true);
            item->setFont(0, bold);
        }
        return item;
    }

    // The block behind the selected row, looked up by path so the pointer is
    // always into the current list rather than a stale copy.
    const Block *current() const
    {
        QTreeWidgetItem *item = m_tree->currentItem();
        if (!item)
            return nullptr;
        const QString path = item->data(0, Qt::UserRole).toString();
        for (const Block &disk : m_disks) {
            if (disk.path == path)
                return &disk;
            for (const Block &part : disk.children)
                if (part.path == path)
                    return &part;
        }
        return nullptr;
    }

    QString colorTok(const char *key) const
    {
        const QColor c = m_tokens.color(QLatin1String(key));
        return c.isValid() ? c.name() : QStringLiteral("#2C6699");
    }

    QString m_repo;
    ThemeTokens m_tokens;
    bool m_demo = false;
    QVector<Block> m_disks;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_note = nullptr;
    QPushButton *m_mount = nullptr, *m_unmount = nullptr, *m_format = nullptr;
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
                   QStringLiteral("Show a representative sample disk set")});
    cli.addOption({QStringLiteral("selftest"),
                   QStringLiteral("Run the head-less parser/safety gate")});
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
    QApplication::setApplicationName(QStringLiteral("castalia-discos"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    addOptions(cli);
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const ThemeTokens tokens = castalia::applyTheme(
        &app, repo, cli.value(QStringLiteral("theme")));

    DiskManager w(repo, tokens, cli.isSet(QStringLiteral("demo")));
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
