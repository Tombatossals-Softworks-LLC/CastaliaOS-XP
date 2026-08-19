// castalia-hardware — the Castalia Hardware Center (Bible §9, §6.15,
// §10 XP-parity "Device Manager").
//
// §9: "Show detected hardware, drivers, quirks. MVP: tree of devices +
// driver in use + status." §10 is careful about the parity: Linux loads its
// own drivers, so this is *inspect*, not *install a .inf*. What a person
// actually needs to know from it is "did my machine's parts get recognised,
// and is something driving them" — which is exactly what a 2000s PC owner
// opened Device Manager to find out.
//
// Everything it reads is plain text in sysfs and procfs:
//   /sys/bus/pci/devices/*   class, vendor, device, the driver symlink
//   /sys/bus/usb/devices/*   idVendor/idProduct, and product/manufacturer,
//                            which USB devices carry themselves
//   /proc/cpuinfo            the processor
//   /proc/meminfo            installed memory
// `lspci` is used only when it happens to be installed, and only to put
// human names on PCI ids. That is a deliberate inversion of the usual design:
// sysfs is always there, so the app works on a FLOOR-tier machine with no
// pciutils, and simply reads "Dispositivo 8086:0d57" instead of a marketing
// name. Nothing is ever hidden for want of a name.
//
// The parsers are pure functions so --selftest can gate them head-lessly in
// CI, where the build machine's hardware is not the user's.
//
// Usage: castalia-hardware [--theme id] [--repo PATH] [--demo] [--selftest]
//                          [--screenshot out.png]

#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>

#include "Theme.h"

namespace {

// One thing in the machine, however it was found.
struct Device {
    QString category;      // the Spanish group it lands in
    QString name;          // human name, or "Dispositivo 8086:0d57"
    QString driver;        // kernel module bound to it, or empty
    QString address;       // PCI slot / USB path — the stable identifier
    QString bus;           // "PCI" / "USB" / "" for the summary rows
};

QString which(const QString &bin)
{
    return QStandardPaths::findExecutable(bin);
}

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// The PCI class code's high byte is the base class. These are the groups a
// person recognises; anything we do not name still appears, under "Otros".
QString pciClassName(quint32 classCode)
{
    switch ((classCode >> 16) & 0xFF) {
    case 0x01: return QStringLiteral("Almacenamiento");
    case 0x02: return QStringLiteral("Red");
    case 0x03: return QStringLiteral("Gráficos");
    case 0x04: return QStringLiteral("Sonido y vídeo");
    case 0x06: return QStringLiteral("Puentes del sistema");
    case 0x07: return QStringLiteral("Puertos serie");
    case 0x08: return QStringLiteral("Controladores del sistema");
    case 0x09: return QStringLiteral("Entrada");
    case 0x0C: return QStringLiteral("Puertos USB y FireWire");
    case 0x0D: return QStringLiteral("Inalámbrico");
    default:   return QStringLiteral("Otros");
    }
}

// `lspci -mm -nn -k` puts a human name and the bound driver on one device.
// Only ever an enrichment: the device list itself comes from sysfs, so a
// machine with no pciutils loses names, not devices.
//
//   00:02.0 "VGA compatible controller [0300]" "Intel [8086]" "HD [0046]"
//        Driver=i915
//        Module=i915
struct PciName {
    QString name;
    QString driver;
};

QHash<QString, PciName> parseLspci(const QString &text)
{
    QHash<QString, PciName> out;
    QString slot;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QStringLiteral("Driver:"))) {
            if (!slot.isEmpty())
                out[slot].driver = line.section(QLatin1Char(':'), 1).trimmed();
            continue;
        }
        if (line.startsWith(QStringLiteral("Module:"))
            || line.startsWith(QStringLiteral("Slot:")))
            continue;                    // -k's other keys: nothing we need
        // A device line: the slot, then quoted fields. The vendor is the
        // second quoted field and the model the third; together they read
        // the way the box did.
        const int space = line.indexOf(QLatin1Char(' '));
        if (space <= 0)
            continue;
        const QString candidate = line.left(space);
        if (!candidate.contains(QLatin1Char(':')))
            continue;
        QStringList quoted;
        int i = space;
        while (true) {
            const int open = line.indexOf(QLatin1Char('"'), i);
            if (open < 0)
                break;
            const int close = line.indexOf(QLatin1Char('"'), open + 1);
            if (close < 0)
                break;
            quoted << line.mid(open + 1, close - open - 1);
            i = close + 1;
        }
        if (quoted.size() < 3)
            continue;
        slot = candidate;
        PciName entry;
        // Strip the "[8086]" id lspci -nn appends; the numbers are already
        // in the address column and repeating them just makes noise.
        auto clean = [](QString s) {
            const int bracket = s.lastIndexOf(QLatin1Char('['));
            if (bracket > 0)
                s = s.left(bracket).trimmed();
            return s;
        };
        entry.name = QStringLiteral("%1 %2").arg(clean(quoted.at(1)),
                                                 clean(quoted.at(2)))
                         .trimmed();
        out.insert(slot, entry);
    }
    return out;
}

// /proc/cpuinfo: the model name, and how many logical processors carry it.
struct Cpu {
    QString model;
    int threads = 0;
};

Cpu parseCpuinfo(const QString &text)
{
    Cpu cpu;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const int colon = raw.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = raw.left(colon).trimmed();
        const QString value = raw.mid(colon + 1).trimmed();
        if (key == QStringLiteral("model name")
            || key == QStringLiteral("Model")        // ARM/older kernels
            || key == QStringLiteral("cpu model")) {
            if (cpu.model.isEmpty())
                cpu.model = value;
            ++cpu.threads;
        } else if (key == QStringLiteral("processor")) {
            // A kernel that gives no model name still counts its processors.
            if (cpu.model.isEmpty() && cpu.threads == 0)
                continue;
        }
    }
    if (cpu.threads == 0)                            // count the hard way
        cpu.threads = text.count(QStringLiteral("processor\t"));
    return cpu;
}

// /proc/meminfo's MemTotal, in bytes (the file reports kB).
qint64 parseMemTotal(const QString &text)
{
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        if (!raw.startsWith(QStringLiteral("MemTotal")))
            continue;
        const QStringList parts = raw.split(QLatin1Char(' '),
                                            Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            bool ok = false;
            const qint64 kb = parts.at(1).toLongLong(&ok);
            if (ok)
                return kb * 1024;
        }
    }
    return 0;
}

QString humanBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("—");
    const char *unit[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = double(bytes);
    int i = 0;
    while (value >= 1024.0 && i < 4) {
        value /= 1024.0;
        ++i;
    }
    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', i >= 3 ? 1 : 0)
        .arg(QLatin1String(unit[i]));
}

// What the user is really asking: is something driving this?
QString driverStatus(const QString &driver)
{
    return driver.isEmpty() ? QStringLiteral("Sin controlador")
                            : QStringLiteral("En uso: %1").arg(driver);
}

// --- the real machine ----------------------------------------------------

QVector<Device> pciDevices()
{
    QVector<Device> out;
    QHash<QString, PciName> names;
    const QString lspci = which(QStringLiteral("lspci"));
    if (!lspci.isEmpty()) {
        QProcess p;
        p.start(lspci, {QStringLiteral("-mm"), QStringLiteral("-nn"),
                        QStringLiteral("-k")});
        if (p.waitForFinished(2000))
            names = parseLspci(QString::fromUtf8(p.readAllStandardOutput()));
    }

    QDir root(QStringLiteral("/sys/bus/pci/devices"));
    // Not `slots`: Qt's moc keywords make that an identifier you cannot use.
    const auto pciSlots = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                         QDir::Name);
    for (const QString &slot : pciSlots) {
        const QString dir = root.filePath(slot);
        Device d;
        d.bus = QStringLiteral("PCI");
        d.address = slot;
        d.category = pciClassName(
            readFile(dir + QStringLiteral("/class")).trimmed()
                .mid(2).toUInt(nullptr, 16));
        const QString vendor =
            readFile(dir + QStringLiteral("/vendor")).trimmed().mid(2);
        const QString device =
            readFile(dir + QStringLiteral("/device")).trimmed().mid(2);
        const QFileInfo driverLink(dir + QStringLiteral("/driver"));
        if (driverLink.isSymLink())
            d.driver = QFileInfo(driverLink.symLinkTarget()).fileName();

        // lspci addresses drop the domain ("0000:"); sysfs keeps it.
        const QString shortSlot = slot.section(QLatin1Char(':'), 1);
        const PciName named = names.value(shortSlot, names.value(slot));
        d.name = named.name.isEmpty()
            ? QStringLiteral("Dispositivo %1:%2").arg(vendor, device)
            : named.name;
        if (d.driver.isEmpty())
            d.driver = named.driver;
        out.append(d);
    }
    return out;
}

QVector<Device> usbDevices()
{
    QVector<Device> out;
    QDir root(QStringLiteral("/sys/bus/usb/devices"));
    const auto entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                        QDir::Name);
    for (const QString &entry : entries) {
        // Interfaces (1-1:1.0) are parts of a device, not devices.
        if (entry.contains(QLatin1Char(':')))
            continue;
        const QString dir = root.filePath(entry);
        const QString idVendor =
            readFile(dir + QStringLiteral("/idVendor")).trimmed();
        if (idVendor.isEmpty())
            continue;                    // a root hub stub, not a device
        const QString idProduct =
            readFile(dir + QStringLiteral("/idProduct")).trimmed();
        Device d;
        d.bus = QStringLiteral("USB");
        d.address = entry;
        d.category = QStringLiteral("USB");
        // USB devices carry their own strings — no id database needed.
        const QString product =
            readFile(dir + QStringLiteral("/product")).trimmed();
        const QString maker =
            readFile(dir + QStringLiteral("/manufacturer")).trimmed();
        d.name = product.isEmpty()
            ? QStringLiteral("Dispositivo USB %1:%2").arg(idVendor, idProduct)
            : (maker.isEmpty() ? product
                               : QStringLiteral("%1 %2").arg(maker, product));
        const QFileInfo driverLink(dir + QStringLiteral("/driver"));
        if (driverLink.isSymLink())
            d.driver = QFileInfo(driverLink.symLinkTarget()).fileName();
        out.append(d);
    }
    return out;
}

QVector<Device> summaryDevices()
{
    QVector<Device> out;
    const Cpu cpu = parseCpuinfo(readFile(QStringLiteral("/proc/cpuinfo")));
    if (!cpu.model.isEmpty() || cpu.threads > 0) {
        Device d;
        d.category = QStringLiteral("Equipo");
        d.name = cpu.model.isEmpty() ? QStringLiteral("Procesador")
                                     : cpu.model;
        d.driver = cpu.threads > 0
            ? QStringLiteral("%1 procesador(es) lógico(s)").arg(cpu.threads)
            : QString();
        out.append(d);
    }
    const qint64 mem = parseMemTotal(readFile(QStringLiteral("/proc/meminfo")));
    if (mem > 0) {
        Device d;
        d.category = QStringLiteral("Equipo");
        d.name = QStringLiteral("Memoria instalada");
        d.driver = humanBytes(mem);
        out.append(d);
    }
    return out;
}

// The sample shown by --demo and by the offscreen render gate: the real list
// is different on every machine, and a screenshot has to be the same twice.
QVector<Device> demoDevices()
{
    QVector<Device> out;
    const struct {
        const char *cat; const char *name; const char *driver;
        const char *addr; const char *bus;
    } rows[] = {
        {"Equipo", "Intel Pentium 4 3.00GHz", "2 procesador(es) lógico(s)",
         "", ""},
        {"Equipo", "Memoria instalada", "2 GiB", "", ""},
        {"Gráficos", "Intel 82945G Express", "i915", "00:02.0", "PCI"},
        {"Sonido y vídeo", "Intel 82801G AC'97 Audio", "snd_intel8x0",
         "00:1b.0", "PCI"},
        {"Red", "Realtek RTL-8139/8139C", "8139too", "02:00.0", "PCI"},
        {"Almacenamiento", "Intel 82801GB IDE", "ata_piix", "00:1f.1", "PCI"},
        {"Puertos USB y FireWire", "Intel 82801G USB UHCI", "uhci_hcd",
         "00:1d.0", "PCI"},
        {"Otros", "Dispositivo 1234:5678", "", "03:00.0", "PCI"},
        {"USB", "Logitech USB Optical Mouse", "usbhid", "1-2", "USB"},
    };
    for (const auto &r : rows) {
        Device d;
        d.category = QString::fromUtf8(r.cat);
        d.name = QString::fromUtf8(r.name);
        d.driver = QString::fromUtf8(r.driver);
        d.address = QString::fromUtf8(r.addr);
        d.bus = QString::fromUtf8(r.bus);
        out.append(d);
    }
    return out;
}

// Categories in the order a person looks for them: what the machine *is*
// first, then the parts they care about, then the plumbing.
int categoryRank(const QString &category)
{
    static const QStringList order = {
        QStringLiteral("Equipo"),
        QStringLiteral("Gráficos"),
        QStringLiteral("Sonido y vídeo"),
        QStringLiteral("Red"),
        QStringLiteral("Inalámbrico"),
        QStringLiteral("Almacenamiento"),
        QStringLiteral("USB"),
        QStringLiteral("Entrada"),
        QStringLiteral("Puertos USB y FireWire"),
        QStringLiteral("Puertos serie"),
        QStringLiteral("Controladores del sistema"),
        QStringLiteral("Puentes del sistema"),
    };
    const int i = order.indexOf(category);
    return i < 0 ? order.size() : i;      // "Otros" and anything new: last
}

// A plain-text report, for pasting into a forum post or an email when
// someone is helping you with a machine you cannot show them (§20).
QString report(const QVector<Device> &devices)
{
    QString out = QStringLiteral("Centro de hardware — Castalia OS\n");
    QString current;
    for (const Device &d : devices) {
        if (d.category != current) {
            current = d.category;
            out += QStringLiteral("\n[%1]\n").arg(current);
        }
        out += QStringLiteral("  %1").arg(d.name);
        if (!d.driver.isEmpty())
            out += QStringLiteral("  —  %1").arg(d.driver);
        if (!d.address.isEmpty())
            out += QStringLiteral("  (%1 %2)").arg(d.bus, d.address);
        out += QLatin1Char('\n');
    }
    return out;
}

// --- head-less self-test (Bible §17.4) -----------------------------------
int selftest()
{
    int failures = 0;
    auto check = [&failures](bool ok, const char *what) {
        if (!ok) {
            QTextStream(stderr) << "hardware-selftest: FAIL " << what << '\n';
            ++failures;
        }
    };

    // PCI class codes → the groups a person recognises.
    check(pciClassName(0x030000) == QStringLiteral("Gráficos"),
          "0x03 is the graphics card");
    check(pciClassName(0x020000) == QStringLiteral("Red"), "0x02 is network");
    check(pciClassName(0x010601) == QStringLiteral("Almacenamiento"),
          "the sub-class does not change the group");
    check(pciClassName(0xFFFF00) == QStringLiteral("Otros"),
          "an unknown class is grouped, never dropped");

    // lspci enrichment
    const QString sample =
        QStringLiteral("00:02.0 \"VGA compatible controller [0300]\" "
                       "\"Intel Corporation [8086]\" "
                       "\"82945G Express [2772]\" -r02 \"Dell [1028]\"\n"
                       "\tDriver: i915\n"
                       "\tModule: i915\n"
                       "02:00.0 \"Ethernet controller [0200]\" "
                       "\"Realtek [10ec]\" \"RTL-8139 [8139]\"\n");
    const QHash<QString, PciName> named = parseLspci(sample);
    check(named.size() == 2, "both devices are read");
    check(named.value(QStringLiteral("00:02.0")).name
              == QStringLiteral("Intel Corporation 82945G Express"),
          "vendor and model read as one name");
    check(named.value(QStringLiteral("00:02.0")).driver
              == QStringLiteral("i915"),
          "the bound driver is read");
    check(named.value(QStringLiteral("02:00.0")).driver.isEmpty(),
          "a device with no Driver: line claims none");
    check(parseLspci(QString()).isEmpty(),
          "no lspci output is no names, not a crash");
    // A machine with no pciutils must still list its devices.
    check(!parseLspci(QStringLiteral("bash: lspci: not found")).size(),
          "garbage in, nothing out");

    // /proc/cpuinfo
    const Cpu cpu = parseCpuinfo(QStringLiteral(
        "processor\t: 0\nvendor_id\t: GenuineIntel\n"
        "model name\t: Intel(R) Pentium(R) 4 CPU 3.00GHz\n\n"
        "processor\t: 1\nmodel name\t: Intel(R) Pentium(R) 4 CPU 3.00GHz\n"));
    check(cpu.model == QStringLiteral("Intel(R) Pentium(R) 4 CPU 3.00GHz"),
          "the processor names itself");
    check(cpu.threads == 2, "both logical processors are counted");
    check(parseCpuinfo(QString()).threads == 0, "an empty file is empty");

    // /proc/meminfo
    check(parseMemTotal(QStringLiteral("MemTotal:        2048000 kB\n"))
              == qint64(2048000) * 1024,
          "MemTotal is read in kB and reported in bytes");
    check(parseMemTotal(QStringLiteral("SwapTotal: 1 kB\n")) == 0,
          "no MemTotal line means unknown, not zero-by-accident");

    check(humanBytes(0) == QStringLiteral("—"), "unknown size reads as a dash");
    check(humanBytes(2LL * 1024 * 1024 * 1024)
              == QStringLiteral("2.0 GiB"), "gibibytes get a decimal");
    check(humanBytes(512) == QStringLiteral("512 B"), "small sizes stay exact");

    check(driverStatus(QString()) == QStringLiteral("Sin controlador"),
          "an undriven device says so plainly");
    check(driverStatus(QStringLiteral("i915"))
              == QStringLiteral("En uso: i915"),
          "a driven device names its driver");

    // Ordering: what the machine is, before the plumbing it is built from.
    check(categoryRank(QStringLiteral("Equipo"))
              < categoryRank(QStringLiteral("Gráficos")),
          "the machine itself comes first");
    check(categoryRank(QStringLiteral("Gráficos"))
              < categoryRank(QStringLiteral("Puentes del sistema")),
          "the graphics card outranks a bridge");
    check(categoryRank(QStringLiteral("Otros"))
              >= categoryRank(QStringLiteral("Puentes del sistema")),
          "unknown groups sort last");

    const QVector<Device> demo = demoDevices();
    check(demo.size() >= 8, "the sample has a machine's worth of parts");
    for (const Device &d : demo)
        check(!d.name.isEmpty() && !d.category.isEmpty(),
              "every sample device is named and grouped");
    const QString text = report(demo);
    check(text.contains(QStringLiteral("[Gráficos]")),
          "the report is grouped");
    check(text.contains(QStringLiteral("i915")),
          "the report names drivers");
    check(text.contains(QStringLiteral("Dispositivo 1234:5678")),
          "a device with no name still appears in the report");

    QTextStream(stdout) << (failures == 0
        ? QStringLiteral("hardware-selftest: OK\n")
        : QStringLiteral("hardware-selftest: %1 failure(s)\n").arg(failures));
    return failures == 0 ? 0 : 1;
}

} // namespace

class HardwareCenter : public QWidget {
    Q_OBJECT
public:
    HardwareCenter(const QString &repo, const ThemeTokens &tokens, bool demo)
        : m_repo(repo), m_tokens(tokens), m_demo(demo)
    {
        setWindowTitle(QStringLiteral("Centro de hardware — Castalia"));
        resize(860, 520);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("HwHeader"));
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "#HwHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *icon = new QLabel(head);
        icon->setPixmap(castalia::themeIcon(m_repo,
                                            QStringLiteral("computer"))
                            .pixmap(28, 28));
        hl->addWidget(icon);
        m_title = new QLabel(head);
        m_title->setStyleSheet(QStringLiteral("color:%1;font-size:15px;"
                                              "font-weight:bold;")
                                   .arg(colorTok("titlebar_text")));
        hl->addWidget(m_title);
        hl->addStretch(1);
        root->addWidget(head);

        m_tree = new QTreeWidget(this);
        m_tree->setColumnCount(3);
        m_tree->setHeaderLabels({QStringLiteral("Dispositivo"),
                                 QStringLiteral("Estado"),
                                 QStringLiteral("Dirección")});
        // Device names are long ("Intel 82801G AC'97 Audio Controller"), so
        // the first column gets real width instead of whatever is left.
        m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
        m_tree->header()->resizeSection(0, 340);
        m_tree->header()->setSectionResizeMode(1,
                                               QHeaderView::ResizeToContents);
        m_tree->header()->setStretchLastSection(true);
        m_tree->setAlternatingRowColors(true);
        m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
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
        auto *copy = new QPushButton(QStringLiteral("Copiar informe"), this);
        auto *refresh = new QPushButton(QStringLiteral("Actualizar"), this);
        for (QPushButton *b : {copy, refresh}) {
            b->setCursor(Qt::PointingHandCursor);
            bar->addWidget(b);
        }
        connect(copy, &QPushButton::clicked, this, [this]() {
            QApplication::clipboard()->setText(report(m_devices));
            m_note->setText(QStringLiteral(
                "Informe copiado al portapapeles."));
        });
        connect(refresh, &QPushButton::clicked, this,
                &HardwareCenter::reload);
        root->addLayout(bar);

        reload();
    }

private slots:
    void reload()
    {
        m_devices.clear();
        if (m_demo) {
            m_devices = demoDevices();
        } else {
            m_devices = summaryDevices();
            m_devices += pciDevices();
            m_devices += usbDevices();
        }
        std::stable_sort(m_devices.begin(), m_devices.end(),
                         [](const Device &a, const Device &b) {
                             return categoryRank(a.category)
                                 < categoryRank(b.category);
                         });

        m_tree->clear();
        QTreeWidgetItem *group = nullptr;
        QString current;
        int undriven = 0;
        for (const Device &d : m_devices) {
            if (d.category != current) {
                current = d.category;
                group = new QTreeWidgetItem(m_tree);
                group->setText(0, current);
                QFont bold = group->font(0);
                bold.setBold(true);
                group->setFont(0, bold);
                group->setExpanded(true);
                group->setFirstColumnSpanned(true);
            }
            auto *item = new QTreeWidgetItem(group);
            item->setText(0, d.name);
            // The summary rows carry a fact, not a driver: show it as is.
            item->setText(1, d.bus.isEmpty() ? d.driver
                                             : driverStatus(d.driver));
            item->setText(2, d.address.isEmpty()
                                 ? QString()
                                 : QStringLiteral("%1 %2").arg(d.bus,
                                                               d.address));
            if (!d.bus.isEmpty() && d.driver.isEmpty()) {
                ++undriven;
                item->setToolTip(1, QStringLiteral(
                    "El núcleo reconoce el dispositivo pero no hay ningún "
                    "módulo asociado. Puede ser normal (un puente del "
                    "sistema) o faltar firmware."));
            }
        }

        const int devices = m_devices.size();
        m_title->setText(QStringLiteral("Centro de hardware"));
        m_note->setText(
            undriven == 0
                ? QStringLiteral("%1 elementos detectados. Todos tienen un "
                                 "controlador asignado.").arg(devices)
                : QStringLiteral("%1 elementos detectados; %2 sin controlador "
                                 "(a menudo es normal: los puentes del "
                                 "sistema no necesitan uno).")
                      .arg(devices).arg(undriven));
    }

private:
    QString colorTok(const char *key) const
    {
        const QColor c = m_tokens.color(QLatin1String(key));
        return c.isValid() ? c.name() : QStringLiteral("#2C6699");
    }

    QString m_repo;
    ThemeTokens m_tokens;
    bool m_demo = false;
    QVector<Device> m_devices;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_title = nullptr, *m_note = nullptr;
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
                   QStringLiteral("Show a representative sample machine")});
    cli.addOption({QStringLiteral("selftest"),
                   QStringLiteral("Run the head-less parser self-test")});
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
    QApplication::setApplicationName(QStringLiteral("castalia-hardware"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    addOptions(cli);
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const ThemeTokens tokens = castalia::applyTheme(
        &app, repo, cli.value(QStringLiteral("theme")));

    HardwareCenter w(repo, tokens, cli.isSet(QStringLiteral("demo")));
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
