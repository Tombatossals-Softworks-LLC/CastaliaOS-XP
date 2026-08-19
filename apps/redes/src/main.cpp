// castalia-redes — the Castalia Network Center (Bible §9, §6.9,
// §10 XP-parity "Network Connections").
//
// §9 MVP: "wired DHCP/static, Wi-Fi connect, status tray". It started as an
// honest read-only status view over iproute2; this is the half that changes
// things, and it is a [Wrap] over **NetworkManager** through `nmcli` (§6.9).
//
// Three tabs, in the order a person needs them:
//   Estado          what the machine's interfaces are doing right now — the
//                   iproute2 view, which needs no NetworkManager at all;
//   Wi-Fi           the networks in range, in plain language ("Excelente",
//                   "Abierta (sin contraseña)"), and connect;
//   Configuración   automática (DHCP) or manual address/gateway/DNS per
//                   connection.
//
// When NetworkManager is not installed the last two tabs say so and disable
// themselves rather than offering buttons that cannot work — the Estado tab
// keeps working, because reading the truth about the network never needed NM.
//
// The parsers and the argument builders are pure functions gated by
// --selftest. Two of them are worth the trouble specifically:
//   * nmcli's terse output is colon-separated with backslash escapes, and
//     SSIDs contain colons. A naive split renames the user's network.
//   * a wrong `nmcli connection modify` argument list does not error — it
//     applies, and the machine loses its network.
//
// Usage: castalia-redes [--theme id] [--repo PATH] [--demo] [--selftest]
//                       [--screenshot out.png]

#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "Theme.h"

namespace {

struct Iface {
    QString name, type, mac, ipv4, ipv6;
    bool up = false;
};

// One Wi-Fi network in range.
struct Network {
    QString ssid;
    int signal = 0;          // 0..100 as NetworkManager reports it
    QString security;        // raw ("WPA2", "WPA1 WPA2", "" for open)
    bool inUse = false;
};

// One NetworkManager connection profile.
struct Connection {
    QString name;
    QString type;            // 802-3-ethernet / 802-11-wireless / …
    QString device;          // empty when the profile is not active
};

QString run(const QString &bin, const QStringList &args, int timeoutMs = 1500)
{
    QProcess p;
    p.start(bin, args);
    if (!p.waitForFinished(timeoutMs))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput());
}

QString which(const QString &bin)
{
    return QStandardPaths::findExecutable(bin);
}

bool haveIp()
{
    return !which(QStringLiteral("ip")).isEmpty();
}

QString typeFor(const QString &name)
{
    if (name == QStringLiteral("lo"))
        return QStringLiteral("bucle local");
    if (name.startsWith(QLatin1Char('e')))   // enp*, eth*
        return QStringLiteral("Ethernet");
    if (name.startsWith(QStringLiteral("wl")))  // wlp*, wlan*
        return QStringLiteral("Wi-Fi");
    if (name.startsWith(QStringLiteral("tun"))
        || name.startsWith(QStringLiteral("wg")))
        return QStringLiteral("VPN");
    return QStringLiteral("red");
}

// --- nmcli, parsed --------------------------------------------------------

// Split one line of `nmcli -t` output. The separator is ':' and a literal
// colon inside a value is escaped as "\:" — which matters more than it
// sounds, because SSIDs may contain colons and a naive split would rename
// somebody's network and then fail to connect to it.
QStringList splitNmcliRow(const QString &line)
{
    QStringList fields;
    QString current;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('\\') && i + 1 < line.size()) {
            current.append(line.at(++i));      // the escaped character, as is
            continue;
        }
        if (c == QLatin1Char(':')) {
            fields.append(current);
            current.clear();
            continue;
        }
        current.append(c);
    }
    fields.append(current);
    return fields;
}

// `nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY device wifi list`
QVector<Network> parseWifiList(const QString &text)
{
    QVector<Network> out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty())
            continue;
        const QStringList f = splitNmcliRow(line);
        if (f.size() < 4)
            continue;
        Network n;
        n.inUse = f.at(0).trimmed() == QStringLiteral("*");
        n.ssid = f.at(1);
        n.signal = f.at(2).toInt();
        n.security = f.at(3).trimmed();
        if (n.ssid.isEmpty())
            continue;                 // a hidden network we cannot offer
        // The same SSID appears once per access point; keep the strongest.
        bool merged = false;
        for (Network &existing : out) {
            if (existing.ssid != n.ssid)
                continue;
            merged = true;
            if (n.signal > existing.signal)
                existing.signal = n.signal;
            existing.inUse = existing.inUse || n.inUse;
            break;
        }
        if (!merged)
            out.append(n);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Network &a, const Network &b) {
                         if (a.inUse != b.inUse)
                             return a.inUse;      // what you are on, first
                         return a.signal > b.signal;
                     });
    return out;
}

// `nmcli -t -f NAME,TYPE,DEVICE connection show`
QVector<Connection> parseConnections(const QString &text)
{
    QVector<Connection> out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty())
            continue;
        const QStringList f = splitNmcliRow(line);
        if (f.size() < 3)
            continue;
        Connection c;
        c.name = f.at(0);
        c.type = f.at(1);
        c.device = f.at(2) == QStringLiteral("--") ? QString() : f.at(2);
        if (c.name.isEmpty())
            continue;
        out.append(c);
    }
    return out;
}

// Signal strength as a person would say it, not as a number.
QString signalWords(int signal)
{
    if (signal >= 75)
        return QStringLiteral("Excelente");
    if (signal >= 50)
        return QStringLiteral("Buena");
    if (signal >= 25)
        return QStringLiteral("Regular");
    return QStringLiteral("Débil");
}

// …and the security, including the one that matters: an open network is not
// "--", it is a network anyone nearby can read.
QString securityWords(const QString &security)
{
    const QString s = security.trimmed();
    if (s.isEmpty() || s == QStringLiteral("--"))
        return QStringLiteral("Abierta (sin contraseña)");
    if (s.contains(QStringLiteral("WPA3")))
        return QStringLiteral("WPA3");
    if (s.contains(QStringLiteral("WPA2")))
        return QStringLiteral("WPA2");
    if (s.contains(QStringLiteral("WPA")))
        return QStringLiteral("WPA");
    if (s.contains(QStringLiteral("WEP")))
        return QStringLiteral("WEP (insegura)");
    return s;
}

bool needsPassword(const QString &security)
{
    const QString s = security.trimmed();
    return !(s.isEmpty() || s == QStringLiteral("--"));
}

// --- what we ask nmcli to change -----------------------------------------

bool isValidIpv4(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char('.'));
    if (parts.size() != 4)
        return false;
    for (const QString &part : parts) {
        if (part.isEmpty() || part.size() > 3)
            return false;
        for (const QChar &c : part)
            if (!c.isDigit())
                return false;
        bool ok = false;
        const int value = part.toInt(&ok);
        if (!ok || value < 0 || value > 255)
            return false;
    }
    return true;
}

// "192.168.1.42/24" — NetworkManager wants the prefix, and a user who types
// a bare address would otherwise get a /32 and no network at all.
bool isValidCidr(const QString &text)
{
    const int slash = text.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return false;
    if (!isValidIpv4(text.left(slash)))
        return false;
    bool ok = false;
    const int prefix = text.mid(slash + 1).toInt(&ok);
    return ok && prefix >= 1 && prefix <= 32;
}

// The argument list for "automática": hand everything back to DHCP, and
// clear the manual fields so a previous static setup cannot linger.
QStringList dhcpArgs(const QString &connection)
{
    return {QStringLiteral("connection"), QStringLiteral("modify"), connection,
            QStringLiteral("ipv4.method"), QStringLiteral("auto"),
            QStringLiteral("ipv4.addresses"), QString(),
            QStringLiteral("ipv4.gateway"), QString(),
            QStringLiteral("ipv4.dns"), QString()};
}

// …and for a manual address. Written as a pure function because a wrong
// list here does not error: it applies, and the machine loses its network.
QStringList staticArgs(const QString &connection, const QString &cidr,
                       const QString &gateway, const QString &dns)
{
    QStringList args = {QStringLiteral("connection"),
                        QStringLiteral("modify"), connection,
                        QStringLiteral("ipv4.method"),
                        QStringLiteral("manual"),
                        QStringLiteral("ipv4.addresses"), cidr};
    // An empty gateway or DNS is passed as an empty value, which clears the
    // field — never omitted, or the old value survives a change the user
    // believes they made.
    args << QStringLiteral("ipv4.gateway") << gateway;
    args << QStringLiteral("ipv4.dns") << dns;
    return args;
}

// Why a manual configuration cannot be applied — empty means it can.
QString staticRefusal(const QString &cidr, const QString &gateway,
                      const QString &dns)
{
    if (!isValidCidr(cidr))
        return QStringLiteral(
            "La dirección debe incluir la máscara, por ejemplo "
            "192.168.1.42/24.");
    if (!gateway.isEmpty() && !isValidIpv4(gateway))
        return QStringLiteral("La puerta de enlace no es una dirección IPv4 "
                              "válida.");
    for (const QString &server : dns.split(QLatin1Char(','),
                                           Qt::SkipEmptyParts))
        if (!isValidIpv4(server.trimmed()))
            return QStringLiteral("«%1» no es un servidor DNS válido.")
                .arg(server.trimmed());
    return QString();
}

QVector<Network> demoNetworks()
{
    return {
        {QStringLiteral("Castalia-Casa"), 92, QStringLiteral("WPA2"), true},
        {QStringLiteral("MOVISTAR_A1B2"), 71, QStringLiteral("WPA2"), false},
        {QStringLiteral("Cafetería Wi-Fi"), 48, QString(), false},
        {QStringLiteral("vecino-2.4G"), 22, QStringLiteral("WPA1 WPA2"),
         false},
    };
}

QVector<Connection> demoConnections()
{
    return {{QStringLiteral("Cableada 1"), QStringLiteral("802-3-ethernet"),
             QStringLiteral("eth0")},
            {QStringLiteral("Castalia-Casa"),
             QStringLiteral("802-11-wireless"), QStringLiteral("wlan0")}};
}

// --- head-less self-test (Bible §17.4) -----------------------------------
int selftest()
{
    int failures = 0;
    auto check = [&failures](bool ok, const char *what) {
        if (!ok) {
            QTextStream(stderr) << "redes-selftest: FAIL " << what << '\n';
            ++failures;
        }
    };

    // nmcli's terse rows, escapes and all.
    check(splitNmcliRow(QStringLiteral("a:b:c")).size() == 3,
          "a plain row splits on colons");
    const QStringList escaped =
        splitNmcliRow(QStringLiteral("*:Mi\\:Red:75:WPA2"));
    check(escaped.size() == 4, "an escaped colon does not split the row");
    check(escaped.at(1) == QStringLiteral("Mi:Red"),
          "…and the SSID keeps its colon");
    check(splitNmcliRow(QStringLiteral("::")).size() == 3,
          "empty fields are still fields");

    const QVector<Network> nets = parseWifiList(QStringLiteral(
        "*:Castalia-Casa:92:WPA2\n"
        " :MOVISTAR_A1B2:71:WPA2\n"
        " :Castalia-Casa:64:WPA2\n"          // the same SSID, second AP
        " :Cafetería Wi-Fi:48:\n"
        " ::40:WPA2\n"                        // hidden: no SSID to offer
        "malformed line\n"));
    check(nets.size() == 3, "access points merge into networks");
    check(nets.at(0).ssid == QStringLiteral("Castalia-Casa"),
          "the network in use comes first");
    check(nets.at(0).inUse, "…and is marked as in use");
    check(nets.at(0).signal == 92,
          "a merged network keeps the strongest signal");
    check(nets.at(1).signal >= nets.at(2).signal,
          "the rest sort by signal");
    check(nets.at(2).security.isEmpty(), "an open network has no security");
    check(parseWifiList(QString()).isEmpty(), "no output is no networks");

    const QVector<Connection> conns = parseConnections(QStringLiteral(
        "Cableada 1:802-3-ethernet:eth0\n"
        "Casa\\:Wi-Fi:802-11-wireless:--\n"
        "\n"));
    check(conns.size() == 2, "both profiles are read");
    check(conns.at(0).device == QStringLiteral("eth0"),
          "an active profile names its device");
    check(conns.at(1).device.isEmpty(),
          "\"--\" means the profile is not active, not a device called --");
    check(conns.at(1).name == QStringLiteral("Casa:Wi-Fi"),
          "a profile name keeps its colon");

    // Plain language (§9: "plain-language states").
    check(signalWords(92) == QStringLiteral("Excelente"), "92% is excellent");
    check(signalWords(50) == QStringLiteral("Buena"), "50% is good");
    check(signalWords(30) == QStringLiteral("Regular"), "30% is fair");
    check(signalWords(5) == QStringLiteral("Débil"), "5% is weak");
    check(securityWords(QString())
              == QStringLiteral("Abierta (sin contraseña)"),
          "an open network says what that means");
    check(securityWords(QStringLiteral("--"))
              == QStringLiteral("Abierta (sin contraseña)"),
          "…however nmcli spells 'none'");
    check(securityWords(QStringLiteral("WPA1 WPA2"))
              == QStringLiteral("WPA2"),
          "a mixed-mode network reads as its best");
    check(securityWords(QStringLiteral("WEP"))
              == QStringLiteral("WEP (insegura)"),
          "WEP is named as insecure");
    check(!needsPassword(QString()), "an open network asks for no password");
    check(needsPassword(QStringLiteral("WPA2")),
          "a secured one does");

    // Addresses.
    check(isValidIpv4(QStringLiteral("192.168.1.42")), "a normal address");
    check(isValidIpv4(QStringLiteral("0.0.0.0")), "…and the edges");
    check(isValidIpv4(QStringLiteral("255.255.255.255")), "…both of them");
    check(!isValidIpv4(QStringLiteral("192.168.1")), "three octets is not one");
    check(!isValidIpv4(QStringLiteral("192.168.1.256")), "256 is not an octet");
    check(!isValidIpv4(QStringLiteral("192.168.1.a")), "letters are not");
    check(!isValidIpv4(QString()), "nor is nothing");
    check(isValidCidr(QStringLiteral("192.168.1.42/24")), "address plus mask");
    check(!isValidCidr(QStringLiteral("192.168.1.42")),
          "a bare address is refused: NM would make it a /32");
    check(!isValidCidr(QStringLiteral("192.168.1.42/0")), "/0 is not a mask");
    check(!isValidCidr(QStringLiteral("192.168.1.42/33")), "nor is /33");

    // The argument lists — a wrong one here does not error, it applies.
    const QStringList dhcp = dhcpArgs(QStringLiteral("Cableada 1"));
    check(dhcp.contains(QStringLiteral("ipv4.method")), "the method is set");
    check(dhcp.at(dhcp.indexOf(QStringLiteral("ipv4.method")) + 1)
              == QStringLiteral("auto"),
          "…to auto");
    check(dhcp.at(dhcp.indexOf(QStringLiteral("ipv4.addresses")) + 1)
              .isEmpty(),
          "a previous static address is cleared, not left behind");
    check(dhcp.at(dhcp.indexOf(QStringLiteral("ipv4.gateway")) + 1).isEmpty(),
          "…and so is the gateway");
    check(dhcp.at(2) == QStringLiteral("Cableada 1"),
          "the connection name survives its space");

    const QStringList manual = staticArgs(
        QStringLiteral("Cableada 1"), QStringLiteral("192.168.1.42/24"),
        QStringLiteral("192.168.1.1"), QStringLiteral("1.1.1.1,9.9.9.9"));
    check(manual.at(manual.indexOf(QStringLiteral("ipv4.method")) + 1)
              == QStringLiteral("manual"),
          "manual is manual");
    check(manual.at(manual.indexOf(QStringLiteral("ipv4.addresses")) + 1)
              == QStringLiteral("192.168.1.42/24"),
          "the address goes in with its mask");
    check(manual.contains(QStringLiteral("ipv4.dns")),
          "DNS is always passed, even when empty, so it can be cleared");
    const QStringList noGateway = staticArgs(
        QStringLiteral("c"), QStringLiteral("10.0.0.2/8"), QString(),
        QString());
    check(noGateway.at(noGateway.indexOf(QStringLiteral("ipv4.gateway")) + 1)
              .isEmpty(),
          "an omitted gateway clears the old one rather than keeping it");

    check(staticRefusal(QStringLiteral("192.168.1.42/24"),
                        QStringLiteral("192.168.1.1"),
                        QStringLiteral("1.1.1.1")).isEmpty(),
          "a good configuration is accepted");
    check(staticRefusal(QStringLiteral("192.168.1.42"), QString(), QString())
              .contains(QStringLiteral("máscara")),
          "a maskless address is refused, and says why");
    check(staticRefusal(QStringLiteral("192.168.1.42/24"),
                        QStringLiteral("casa"), QString())
              .contains(QStringLiteral("puerta de enlace")),
          "a bad gateway is refused");
    check(staticRefusal(QStringLiteral("192.168.1.42/24"), QString(),
                        QStringLiteral("1.1.1.1,nope"))
              .contains(QStringLiteral("nope")),
          "a bad DNS server is named in the refusal");
    check(staticRefusal(QStringLiteral("192.168.1.42/24"), QString(),
                        QString()).isEmpty(),
          "gateway and DNS are optional");

    check(typeFor(QStringLiteral("wlan0")) == QStringLiteral("Wi-Fi"),
          "wl* is Wi-Fi");
    check(typeFor(QStringLiteral("enp3s0")) == QStringLiteral("Ethernet"),
          "en* is Ethernet");
    check(typeFor(QStringLiteral("lo")) == QStringLiteral("bucle local"),
          "lo is the loopback");

    check(demoNetworks().size() == 4, "the sample has networks to show");
    check(demoConnections().size() == 2, "…and profiles to configure");

    QTextStream(stdout) << (failures == 0
        ? QStringLiteral("redes-selftest: OK\n")
        : QStringLiteral("redes-selftest: %1 failure(s)\n").arg(failures));
    return failures == 0 ? 0 : 1;
}

} // namespace

class Redes : public QWidget {
    Q_OBJECT
public:
    // `tab` selects which page opens: the render gate and the press kit
    // want the Wi-Fi list, not the status page, and a screenshot cannot
    // click.
    Redes(const QString &repo, const ThemeTokens &tokens, bool demo,
          const QString &tab = QString())
        : m_repo(repo), m_tokens(tokens), m_demo(demo), m_ip(haveIp()),
          m_nmcli(which(QStringLiteral("nmcli")))
    {
        setWindowTitle(QStringLiteral("Centro de redes — Castalia"));
        resize(720, 470);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("NtHeader"));
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "#NtHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *icon = new QLabel(head);
        icon->setPixmap(castalia::themeIcon(m_repo, QStringLiteral("network"))
                            .pixmap(28, 28));
        hl->addWidget(icon);
        m_title = new QLabel(head);
        hl->addWidget(m_title);
        hl->addStretch(1);
        root->addWidget(head);

        m_tabs = new QTabWidget(this);
        m_tabs->addTab(buildStatusTab(), QStringLiteral("Estado"));
        m_tabs->addTab(buildWifiTab(), QStringLiteral("Wi-Fi"));
        m_tabs->addTab(buildConfigTab(), QStringLiteral("Configuración"));
        auto *wrap = new QVBoxLayout;
        wrap->setContentsMargins(14, 12, 14, 6);
        wrap->addWidget(m_tabs, 1);
        root->addLayout(wrap, 1);

        auto *bar = new QHBoxLayout;
        bar->setContentsMargins(14, 0, 14, 12);
        m_note = new QLabel(this);
        m_note->setProperty("secondary", true);
        m_note->setWordWrap(true);
        bar->addWidget(m_note, 1);
        auto *refresh = new QPushButton(QStringLiteral("Actualizar"), this);
        refresh->setCursor(Qt::PointingHandCursor);
        connect(refresh, &QPushButton::clicked, this, &Redes::reload);
        bar->addWidget(refresh);
        root->addLayout(bar);

        // NetworkManager is what makes the last two tabs possible. Without
        // it they are disabled with an explanation — not populated with
        // buttons that cannot work.
        const bool manageable = m_demo || !m_nmcli.isEmpty();
        m_tabs->setTabEnabled(1, manageable);
        m_tabs->setTabEnabled(2, manageable);

        reload();

        if (tab == QStringLiteral("wifi"))
            m_tabs->setCurrentIndex(1);
        else if (tab == QStringLiteral("config"))
            m_tabs->setCurrentIndex(2);
    }

private slots:
    void reload()
    {
        reloadStatus();
        reloadWifi();
        reloadConnections();

        const QString sub = m_demo
            ? QStringLiteral("vista de ejemplo")
            : (m_nmcli.isEmpty()
                   ? (m_ip ? QStringLiteral("solo lectura (sin NetworkManager)")
                           : QStringLiteral("sin herramientas de red"))
                   : QStringLiteral("NetworkManager"));
        m_title->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>Centro "
            "de redes</span>&nbsp;&nbsp;<span style='color:%1'>%2</span>")
            .arg(colorTok("titlebar_text"), sub));

        if (m_demo)
            m_note->setText(QStringLiteral(
                "Vista de ejemplo: no se ha consultado la red real."));
        else if (m_nmcli.isEmpty())
            m_note->setText(QStringLiteral(
                "NetworkManager no está instalado: se muestra el estado, pero "
                "no se puede cambiar la configuración desde aquí."));
        else
            m_note->clear();
    }

    void showIface(int row)
    {
        const bool ok = row >= 0 && row < m_ifaces.size();
        const Iface f = ok ? m_ifaces[row] : Iface{};
        m_fType->setText(ok ? f.type : QStringLiteral("—"));
        m_fState->setText(ok ? (f.up ? QStringLiteral("activa")
                                     : QStringLiteral("inactiva"))
                             : QStringLiteral("—"));
        m_fV4->setText(ok && !f.ipv4.isEmpty() ? f.ipv4 : QStringLiteral("—"));
        m_fV6->setText(ok && !f.ipv6.isEmpty() ? f.ipv6 : QStringLiteral("—"));
        m_fMac->setText(ok && !f.mac.isEmpty() ? f.mac : QStringLiteral("—"));
    }

    void connectWifi()
    {
        const int row = m_wifi->currentIndex().row();
        if (row < 0 || row >= m_networks.size())
            return;
        const Network net = m_networks.at(row);
        if (m_demo) {
            m_note->setText(QStringLiteral(
                "Vista de ejemplo: no se conecta a «%1».").arg(net.ssid));
            return;
        }
        QStringList args = {QStringLiteral("device"), QStringLiteral("wifi"),
                            QStringLiteral("connect"), net.ssid};
        if (needsPassword(net.security)) {
            bool ok = false;
            const QString password = QInputDialog::getText(
                this, QStringLiteral("Conectar a %1").arg(net.ssid),
                QStringLiteral("Contraseña de la red (%1):")
                    .arg(securityWords(net.security)),
                QLineEdit::Password, QString(), &ok);
            if (!ok || password.isEmpty())
                return;
            args << QStringLiteral("password") << password;
        }
        QProcess p;
        p.start(m_nmcli, args);
        // Associating and getting a lease takes real seconds on old Wi-Fi.
        p.waitForFinished(45000);
        const QString err = QString::fromUtf8(p.readAllStandardError())
                                .trimmed();
        m_note->setText(p.exitCode() == 0
            ? QStringLiteral("Conectado a «%1».").arg(net.ssid)
            : QStringLiteral("No se pudo conectar a «%1»: %2")
                  .arg(net.ssid, err.isEmpty()
                       ? QStringLiteral("error desconocido") : err));
        reload();
    }

    void applyConfig()
    {
        const int row = m_conns->currentIndex();
        if (row < 0 || row >= m_connections.size())
            return;
        const QString name = m_connections.at(row).name;
        QStringList args;
        if (m_dhcp->isChecked()) {
            args = dhcpArgs(name);
        } else {
            const QString refusal = staticRefusal(m_addr->text().trimmed(),
                                                  m_gw->text().trimmed(),
                                                  m_dns->text().trimmed());
            if (!refusal.isEmpty()) {
                m_note->setText(refusal);
                return;
            }
            args = staticArgs(name, m_addr->text().trimmed(),
                              m_gw->text().trimmed(), m_dns->text().trimmed());
        }
        if (m_demo) {
            m_note->setText(QStringLiteral(
                "Vista de ejemplo: no se ha cambiado «%1».").arg(name));
            return;
        }
        QProcess p;
        p.start(m_nmcli, args);
        p.waitForFinished(15000);
        if (p.exitCode() != 0) {
            m_note->setText(QStringLiteral("No se pudo aplicar: %1")
                .arg(QString::fromUtf8(p.readAllStandardError()).trimmed()));
            return;
        }
        // A changed profile does nothing until it is brought up again.
        QProcess up;
        up.start(m_nmcli, {QStringLiteral("connection"),
                           QStringLiteral("up"), name});
        up.waitForFinished(30000);
        m_note->setText(up.exitCode() == 0
            ? QStringLiteral("«%1» actualizada y reconectada.").arg(name)
            : QStringLiteral("«%1» guardada, pero no se pudo reconectar: %2")
                  .arg(name, QString::fromUtf8(up.readAllStandardError())
                                 .trimmed()));
        reload();
    }

private:
    QWidget *buildStatusTab()
    {
        auto *tab = new QWidget(this);
        auto *body = new QHBoxLayout(tab);
        body->setContentsMargins(10, 10, 10, 10);
        body->setSpacing(14);

        auto *left = new QVBoxLayout;
        auto *ilabel = new QLabel(QStringLiteral("Interfaces"), tab);
        ilabel->setProperty("secondary", true);
        left->addWidget(ilabel);
        m_list = new QListWidget(tab);
        connect(m_list, &QListWidget::currentRowChanged, this,
                &Redes::showIface);
        left->addWidget(m_list, 1);
        body->addLayout(left, 2);

        auto *right = new QVBoxLayout;
        auto *form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setHorizontalSpacing(12);
        m_fType = new QLabel(tab);
        m_fState = new QLabel(tab);
        m_fV4 = new QLabel(tab);
        m_fV6 = new QLabel(tab);
        m_fMac = new QLabel(tab);
        for (QLabel *l : {m_fV4, m_fV6, m_fMac})
            l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(QStringLiteral("Tipo:"), m_fType);
        form->addRow(QStringLiteral("Estado:"), m_fState);
        form->addRow(QStringLiteral("IPv4:"), m_fV4);
        form->addRow(QStringLiteral("IPv6:"), m_fV6);
        form->addRow(QStringLiteral("MAC:"), m_fMac);
        right->addLayout(form);
        right->addStretch(1);
        m_host = new QLabel(tab);
        m_host->setProperty("secondary", true);
        m_host->setWordWrap(true);
        right->addWidget(m_host);
        body->addLayout(right, 3);
        return tab;
    }

    QWidget *buildWifiTab()
    {
        auto *tab = new QWidget(this);
        auto *lay = new QVBoxLayout(tab);
        lay->setContentsMargins(10, 10, 10, 10);
        m_wifi = new QTreeWidget(tab);
        m_wifi->setRootIsDecorated(false);
        m_wifi->setAlternatingRowColors(true);
        m_wifi->setColumnCount(3);
        m_wifi->setHeaderLabels({QStringLiteral("Red"),
                                 QStringLiteral("Señal"),
                                 QStringLiteral("Seguridad")});
        m_wifi->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_wifi->header()->setSectionResizeMode(1,
                                               QHeaderView::ResizeToContents);
        m_wifi->header()->setSectionResizeMode(2,
                                               QHeaderView::ResizeToContents);
        m_wifi->setSelectionBehavior(QAbstractItemView::SelectRows);
        connect(m_wifi, &QTreeWidget::itemDoubleClicked, this,
                [this]() { connectWifi(); });
        lay->addWidget(m_wifi, 1);

        auto *bar = new QHBoxLayout;
        bar->addStretch(1);
        auto *scan = new QPushButton(QStringLiteral("Buscar redes"), tab);
        auto *join = new QPushButton(QStringLiteral("Conectar"), tab);
        for (QPushButton *b : {scan, join}) {
            b->setCursor(Qt::PointingHandCursor);
            bar->addWidget(b);
        }
        connect(scan, &QPushButton::clicked, this, [this]() {
            if (!m_demo && !m_nmcli.isEmpty())
                run(m_nmcli, {QStringLiteral("device"), QStringLiteral("wifi"),
                              QStringLiteral("rescan")}, 8000);
            reloadWifi();
        });
        connect(join, &QPushButton::clicked, this, &Redes::connectWifi);
        lay->addLayout(bar);
        return tab;
    }

    QWidget *buildConfigTab()
    {
        auto *tab = new QWidget(this);
        auto *lay = new QVBoxLayout(tab);
        lay->setContentsMargins(10, 10, 10, 10);
        lay->setSpacing(8);

        auto *pick = new QHBoxLayout;
        pick->addWidget(new QLabel(QStringLiteral("Conexión:"), tab));
        m_conns = new QComboBox(tab);
        pick->addWidget(m_conns, 1);
        lay->addLayout(pick);

        m_dhcp = new QRadioButton(
            QStringLiteral("Automática (DHCP) — lo normal"), tab);
        m_dhcp->setChecked(true);
        auto *manual = new QRadioButton(
            QStringLiteral("Manual (dirección fija)"), tab);
        lay->addWidget(m_dhcp);
        lay->addWidget(manual);

        auto *form = new QFormLayout;
        m_addr = new QLineEdit(tab);
        m_addr->setPlaceholderText(QStringLiteral("192.168.1.42/24"));
        m_gw = new QLineEdit(tab);
        m_gw->setPlaceholderText(QStringLiteral("192.168.1.1"));
        m_dns = new QLineEdit(tab);
        m_dns->setPlaceholderText(QStringLiteral("1.1.1.1, 9.9.9.9"));
        form->addRow(QStringLiteral("Dirección IP y máscara:"), m_addr);
        form->addRow(QStringLiteral("Puerta de enlace:"), m_gw);
        form->addRow(QStringLiteral("Servidores DNS:"), m_dns);
        lay->addLayout(form);
        for (QLineEdit *e : {m_addr, m_gw, m_dns})
            e->setEnabled(false);
        connect(manual, &QRadioButton::toggled, this,
                [this](bool on) {
                    for (QLineEdit *e : {m_addr, m_gw, m_dns})
                        e->setEnabled(on);
                });

        lay->addStretch(1);
        auto *bar = new QHBoxLayout;
        bar->addStretch(1);
        auto *apply = new QPushButton(QStringLiteral("Aplicar"), tab);
        apply->setCursor(Qt::PointingHandCursor);
        connect(apply, &QPushButton::clicked, this, &Redes::applyConfig);
        bar->addWidget(apply);
        lay->addLayout(bar);
        return tab;
    }

    void reloadStatus()
    {
        m_list->clear();
        m_ifaces = m_demo ? demoIfaces() : readIfaces();
        for (const Iface &f : m_ifaces) {
            const QString addr = f.ipv4.isEmpty()
                ? (f.up ? QStringLiteral("sin IPv4")
                        : QStringLiteral("inactiva"))
                : f.ipv4;
            new QListWidgetItem(
                castalia::themeIcon(m_repo, QStringLiteral("network")),
                QStringLiteral("%1 — %2").arg(f.name, addr), m_list);
        }
        if (!m_ifaces.isEmpty())
            m_list->setCurrentRow(0);
        else
            showIface(-1);

        const QString host = m_demo ? QStringLiteral("pc-castalia")
                                    : QSysInfo::machineHostName();
        const QString gw = m_demo ? QStringLiteral("192.168.1.1 (eth0)")
                                  : defaultGateway();
        m_host->setText(QStringLiteral("Equipo: <b>%1</b>%2")
            .arg(host, gw.isEmpty() ? QString()
                : QStringLiteral(
                      "&nbsp;&nbsp;·&nbsp;&nbsp;Puerta de enlace: %1")
                      .arg(gw)));
    }

    void reloadWifi()
    {
        m_wifi->clear();
        m_networks = m_demo ? demoNetworks() : readNetworks();
        for (const Network &n : m_networks) {
            auto *item = new QTreeWidgetItem(m_wifi);
            item->setText(0, n.inUse ? QStringLiteral("%1  (conectada)")
                                           .arg(n.ssid)
                                     : n.ssid);
            item->setText(1, QStringLiteral("%1 (%2%)")
                                 .arg(signalWords(n.signal))
                                 .arg(n.signal));
            item->setText(2, securityWords(n.security));
            if (n.inUse) {
                QFont bold = item->font(0);
                bold.setBold(true);
                item->setFont(0, bold);
            }
        }
        if (m_wifi->topLevelItemCount() > 0)
            m_wifi->setCurrentItem(m_wifi->topLevelItem(0));
    }

    void reloadConnections()
    {
        m_conns->clear();
        m_connections = m_demo ? demoConnections() : readConnections();
        for (const Connection &c : m_connections)
            m_conns->addItem(c.device.isEmpty()
                                 ? c.name
                                 : QStringLiteral("%1 (%2)").arg(c.name,
                                                                 c.device));
    }

    QVector<Network> readNetworks()
    {
        if (m_nmcli.isEmpty())
            return {};
        return parseWifiList(run(m_nmcli,
                                 {QStringLiteral("-t"), QStringLiteral("-f"),
                                  QStringLiteral("IN-USE,SSID,SIGNAL,SECURITY"),
                                  QStringLiteral("device"),
                                  QStringLiteral("wifi"),
                                  QStringLiteral("list")}, 5000));
    }

    QVector<Connection> readConnections()
    {
        if (m_nmcli.isEmpty())
            return {};
        return parseConnections(run(m_nmcli,
                                    {QStringLiteral("-t"),
                                     QStringLiteral("-f"),
                                     QStringLiteral("NAME,TYPE,DEVICE"),
                                     QStringLiteral("connection"),
                                     QStringLiteral("show")}, 5000));
    }

    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }

    // Parse `ip -o link` (state + mac) and `ip -o addr` (v4/v6) into ifaces.
    QList<Iface> readIfaces()
    {
        QList<Iface> out;
        if (!m_ip)
            return out;
        const QString links = run(QStringLiteral("ip"),
                                  {QStringLiteral("-o"),
                                   QStringLiteral("link"),
                                   QStringLiteral("show")});
        for (const QString &ln : links.split(QLatin1Char('\n'),
                                             Qt::SkipEmptyParts)) {
            // "2: eth0: <...,UP,...> mtu ... link/ether aa:bb:.. brd ..."
            const auto head = ln.section(QLatin1Char(':'), 1, 1).trimmed();
            if (head.isEmpty())
                continue;
            Iface f;
            f.name = head.section(QLatin1Char('@'), 0, 0);  // strip vlan@parent
            f.type = typeFor(f.name);
            f.up = ln.contains(QStringLiteral(",UP,"))
                   || ln.contains(QStringLiteral("state UP"))
                   || ln.contains(QStringLiteral("<UP,"));
            const int li = ln.indexOf(QStringLiteral("link/"));
            if (li >= 0) {
                const QString after = ln.mid(li).section(QLatin1Char(' '), 1, 1);
                if (after.contains(QLatin1Char(':')))
                    f.mac = after;
            }
            out.append(f);
        }
        // Attach addresses.
        const QString addrs = run(QStringLiteral("ip"),
                                  {QStringLiteral("-o"),
                                   QStringLiteral("addr"),
                                   QStringLiteral("show")});
        for (const QString &ln : addrs.split(QLatin1Char('\n'),
                                             Qt::SkipEmptyParts)) {
            const QString name = ln.section(QLatin1Char(' '), 1, 1,
                                            QString::SectionSkipEmpty);
            const bool v4 = ln.contains(QStringLiteral(" inet "));
            const bool v6 = ln.contains(QStringLiteral(" inet6 "));
            QString addr;
            if (v4)
                addr = ln.section(QStringLiteral(" inet "), 1, 1)
                           .section(QLatin1Char(' '), 0, 0);
            else if (v6)
                addr = ln.section(QStringLiteral(" inet6 "), 1, 1)
                           .section(QLatin1Char(' '), 0, 0);
            if (addr.isEmpty())
                continue;
            for (Iface &f : out) {
                if (f.name != name)
                    continue;
                if (v4 && f.ipv4.isEmpty())
                    f.ipv4 = addr;
                else if (v6 && f.ipv6.isEmpty())
                    f.ipv6 = addr;
                break;
            }
        }
        return out;
    }

    QString defaultGateway()
    {
        if (!m_ip)
            return QString();
        const QString r = run(QStringLiteral("ip"),
                              {QStringLiteral("route"), QStringLiteral("show"),
                               QStringLiteral("default")});
        // "default via 192.168.1.1 dev eth0 ..."
        const QString via = r.section(QStringLiteral("via "), 1, 1)
                                .section(QLatin1Char(' '), 0, 0);
        const QString dev = r.section(QStringLiteral("dev "), 1, 1)
                                .section(QLatin1Char(' '), 0, 0);
        if (via.isEmpty())
            return QString();
        return dev.isEmpty() ? via : QStringLiteral("%1 (%2)").arg(via, dev);
    }

    QList<Iface> demoIfaces()
    {
        return {
            {QStringLiteral("eth0"), QStringLiteral("Ethernet"),
             QStringLiteral("de:ad:be:ef:00:11"),
             QStringLiteral("192.168.1.42/24"),
             QStringLiteral("fe80::dead:beff:feef:11/64"), true},
            {QStringLiteral("wlan0"), QStringLiteral("Wi-Fi"),
             QStringLiteral("a4:5e:60:1c:2d:3e"),
             QStringLiteral("10.0.0.7/24"), QString(), true},
            {QStringLiteral("lo"), QStringLiteral("bucle local"),
             QString(), QStringLiteral("127.0.0.1/8"),
             QStringLiteral("::1/128"), true},
        };
    }

    QString m_repo;
    ThemeTokens m_tokens;
    bool m_demo = false, m_ip = false;
    QString m_nmcli;
    QList<Iface> m_ifaces;
    QVector<Network> m_networks;
    QVector<Connection> m_connections;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_title = nullptr, *m_host = nullptr, *m_note = nullptr;
    QLabel *m_fType = nullptr, *m_fState = nullptr, *m_fV4 = nullptr,
           *m_fV6 = nullptr, *m_fMac = nullptr;
    QListWidget *m_list = nullptr;
    QTreeWidget *m_wifi = nullptr;
    QComboBox *m_conns = nullptr;
    QRadioButton *m_dhcp = nullptr;
    QLineEdit *m_addr = nullptr, *m_gw = nullptr, *m_dns = nullptr;
};

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) != "--selftest")
            continue;
        QCoreApplication app(argc, argv);
        Q_UNUSED(app);
        return selftest();
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-redes"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Show a representative sample network")});
    cli.addOption({QStringLiteral("selftest"),
                   QStringLiteral("Run the head-less parser/validator gate")});
    cli.addOption({QStringLiteral("tab"),
                   QStringLiteral("Open on a tab: estado|wifi|config"),
                   QStringLiteral("name")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const ThemeTokens tokens = castalia::applyTheme(&app, repo, themeId);

    Redes w(repo, tokens, cli.isSet(QStringLiteral("demo")),
            cli.value(QStringLiteral("tab")));
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
