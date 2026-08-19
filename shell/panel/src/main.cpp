// castalia-panel — Phase 0 proof of concept (Bible §18 Phase 0, §7.2).
//
// The first native pixels of Castalia OS: a Qt5 panel that
//   1. parses the REAL theme tokens (themes/<id>/theme.conf) in C++,
//   2. loads the QSS generated from those same tokens by
//      tools/theme_export.py,
//   3. derives panel-specific styling from the tokens at runtime,
//   4. reports the §16 budget numbers (startup ms, RSS KiB), and
//   5. can render itself headlessly (QT_QPA_PLATFORM=offscreen) to PNG for
//      CI screenshot verification: --screenshot panel.png --menu-shot m.png
//
// Usage:
//   castalia-panel --theme classic [--repo PATH] [--width 1024]
//                  [--screenshot out.png] [--menu-shot out.png]

#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>

#include <cstdio>

#include "Locale.h"
#include "AppRoster.h"
#include "CastaliaMenu.h"
#include "CastaliaPanel.h"
#include "Switcher.h"
#include "ThemeTokens.h"
#include "TrayHost.h"
#include "XEmbedTray.h"

namespace {

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// Linear blend of two token colours; t = 0 → a, t = 1 → b. The same operation
// tools/theme_export.py performs on the Python side, so the shell's derived
// tints match the generated stylesheet's.
QColor mix(const QColor &a, const QColor &b, qreal t)
{
    if (!a.isValid())
        return b;
    if (!b.isValid())
        return a;
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

// Panel/menu-specific styling derived from the tokens at runtime — the same
// values the QSS was generated from, applied to the shell's own object names.
//
// The gloss is derived, never hard-coded: every lit and shaded stop comes from
// the theme's own accent/surface, so the panel wears whichever of the seven
// themes is on. High Contrast gets flat fills instead — a specular band across
// a button is exactly the kind of decoration that costs contrast (§7.11).
QString panelQss(const ThemeTokens &t)
{
    const QString tbTop = t.str("colors", "titlebar_top");
    const QString tbBot = t.str("colors", "titlebar_bottom");
    const QString tbText = t.str("colors", "titlebar_text");
    const QString accent = t.str("colors", "accent");
    const QString selText = t.str("colors", "selection_text");
    const QString surface = t.str("colors", "surface");
    const QString surfaceAlt = t.str("colors", "surface_alt");
    const QString border = t.str("colors", "border");
    const QString text = t.str("colors", "text");
    const int rad = t.cornerRadius();
    const bool hc = t.highContrast();

    const QColor accentColor = t.color(QStringLiteral("accent"));
    auto hex = [](const QColor &c) { return c.name(QColor::HexRgb); };
    // The launch button's four stops: lit crown, body, the crease at the
    // half, and a soft return at the foot — the shape light actually makes on
    // a curved plastic key.
    const QString launchFill = hc
        ? accent
        : QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                         "stop:0 %1, stop:0.45 %2, stop:0.5 %3, stop:1 %4)")
              .arg(hex(accentColor.lighter(132)), accent,
                   hex(accentColor.darker(122)), hex(accentColor.darker(104)));
    // Task/quick-launch buttons are glass over the panel: they let the panel's
    // own gradient through and only add the highlight.
    const QString glass = hc
        ? QStringLiteral("rgba(255,255,255,40)")
        : QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                         "stop:0 rgba(255,255,255,64), "
                         "stop:0.48 rgba(255,255,255,30), "
                         "stop:0.52 rgba(255,255,255,12), "
                         "stop:1 rgba(255,255,255,34))");
    const QString glassHover = hc
        ? QStringLiteral("rgba(255,255,255,80)")
        : QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                         "stop:0 rgba(255,255,255,104), "
                         "stop:0.48 rgba(255,255,255,62), "
                         "stop:0.52 rgba(255,255,255,38), "
                         "stop:1 rgba(255,255,255,66))");
    // The focused window reads as pressed-in: shaded at the top, lifting at
    // the foot — the inverse of the glass above.
    const QString glassActive = hc
        ? QStringLiteral("rgba(255,255,255,110)")
        : QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                         "stop:0 rgba(0,0,0,58), "
                         "stop:0.5 rgba(255,255,255,26), "
                         "stop:1 rgba(255,255,255,74))");

    return QStringLiteral(R"(
#CastaliaPanel {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 %1, stop:1 %2);
    border-top: 1px solid %8;
}
#LaunchButton {
    /* the left padding is the orb's seat — LaunchButton::paintEvent draws the
       Castalia mark there at the full height of the key */
    background: %11; color: %4; font-weight: bold;
    border: 1px solid %2; border-radius: %9px;
    padding: 2px 12px 2px 32px;
}
#LaunchButton:hover { border-color: %4; }
#QuickLaunch, #TaskButton {
    color: %5; border: 1px solid rgba(255,255,255,70);
    background: %12; border-radius: %9px;
    padding: 2px 10px;
}
#QuickLaunch:hover, #TaskButton:hover {
    background: %13; border-color: rgba(255,255,255,150);
}
#QuickLaunch:pressed, #TaskButton:pressed { background: %14; }
#TaskButton[active="true"] {
    background: %14;
    border-color: rgba(255,255,255,140);
    border-left: 2px solid %3;
    font-weight: bold;
}
#PanelSep { color: rgba(255,255,255,90); }
#TrayFrame {
    background: rgba(0,0,0,52); border-radius: %9px;
    border: 1px solid rgba(255,255,255,34);
}
#ClockLabel { color: %5; background: transparent; }
#VolBtn {
    background: transparent; border: 1px solid transparent;
    border-radius: %9px;
}
#VolBtn:hover {
    background: rgba(255,255,255,58); border-color: rgba(255,255,255,90);
}
#TrayItem {
    background: transparent; border: 1px solid transparent;
    border-radius: %9px; padding: 0px; margin: 0px;
}
#TrayItem:hover {
    background: rgba(255,255,255,58); border-color: rgba(255,255,255,90);
}

#CastaliaMenu { background: %6; border: 1px solid %8; }
#MenuHeader {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 %1, stop:1 %2);
    border-bottom: 2px solid %3;
}
#MenuUser { color: %5; font-weight: bold; background: transparent; }
#MenuLeft { background: %6; }
#MenuRight { background: %7; border-left: 1px solid %8; }
#MenuFoot { background: %7; border-top: 1px solid %8; }
#MenuApp, #MenuPlace, #MenuAllApps {
    /* 3 px border + 7 px padding = the 10 px the entries had before the
       hover bar existed, so no label lost a pixel of width to it. */
    color: %10; text-align: left; padding: 5px 10px 5px 7px;
    border: none; border-left: 3px solid transparent;
    border-radius: %9px; background: transparent;
}
/* Hovering an entry washes it with the accent and lights a bar down its
   leading edge. The bar is in the resting rule too (transparent), so the
   label never shifts when it appears. */
#MenuApp:hover, #MenuPlace:hover, #MenuAllApps:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 %15, stop:1 %16);
    border-left-color: %3;
}
#MenuApp:pressed, #MenuPlace:pressed, #MenuAllApps:pressed {
    background: %17; color: %18;
}
#MenuApp:focus, #MenuPlace:focus, #MenuAllApps:focus {
    border-left-color: %3;
}
#MenuAllApps { font-weight: bold; border-top: 1px solid %8; }
#MenuSection {
    color: %3; font-size: 10px; font-weight: bold;
    background: transparent; border-bottom: 1px solid %8;
    padding: 2px 6px 3px 6px;
}
#MenuSearch {
    border: 1px solid %8; border-radius: %9px;
    padding: 3px 6px; background: %6; color: %10;
}
#MenuSearch:focus { border: 2px solid %3; padding: 2px 5px; }
#PowerBtn, #PowerBtnSolid {
    color: %10; border: 1px solid %8; border-radius: %9px;
    padding: 4px 12px; background: %6;
}
#PowerBtn:hover { background: %15; border-color: %3; }
#PowerBtnSolid { background: %11; color: %4; font-weight: bold; }
#PowerBtnSolid:hover { border-color: %4; }
)")
        .arg(tbTop, tbBot, accent, selText, tbText, surface, surfaceAlt,
             border)
        .arg(rad)
        .arg(text)
        .arg(launchFill, glass, glassHover, glassActive)
        // the hover wash: accent at 18 % on the leading edge, fading out
        .arg(hex(mix(t.color(QStringLiteral("surface")), accentColor, 0.18)),
             hex(mix(t.color(QStringLiteral("surface")), accentColor, 0.05)),
             t.str("colors", "selection_bg"),
             t.str("colors", "selection_text"));
}

long rssKiB()
{
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1;
    for (const QByteArray &line : status.readAll().split('\n'))
        if (line.startsWith("VmRSS:"))
            return line.mid(6).trimmed().split(' ').first().toLong();
    return -1;
}

// The panel's head-less gate. Only the pure parts: what an application sends
// to RegisterStatusNotifierItem, and the words that reveal the hidden menu
// entry. Getting the first one wrong means a third-party indicator silently
// never appears — the exact failure nobody notices until a user reports that
// "the tray does not work with X".
int selftest()
{
    int fail = 0;
    auto check = [&fail](bool ok, const char *what) {
        if (!ok) {
            ++fail;
            qWarning("panel selftest FAIL: %s", what);
        }
    };
    const QString sender = QStringLiteral(":1.42");

    // a plain bus name → the spec's default object path
    auto a = TrayHost::splitItemService(
        QStringLiteral("org.kde.StatusNotifierItem-1234-1"), sender);
    check(a.first == QStringLiteral("org.kde.StatusNotifierItem-1234-1"),
          "tray: a bus name is the service");
    check(a.second == QStringLiteral("/StatusNotifierItem"),
          "tray: a bus name implies the default path");

    // a bare object path → the service is whoever called
    auto b = TrayHost::splitItemService(
        QStringLiteral("/org/ayatana/NotificationItem/nm_applet"), sender);
    check(b.first == sender, "tray: a path means the sender's own name");
    check(b.second == QStringLiteral("/org/ayatana/NotificationItem/nm_applet"),
          "tray: the path is kept verbatim");

    // some applications send both at once
    auto c = TrayHost::splitItemService(
        QStringLiteral("org.example.App/StatusNotifierItem"), sender);
    check(c.first == QStringLiteral("org.example.App"),
          "tray: service/path in one string is split");
    check(c.second == QStringLiteral("/StatusNotifierItem"),
          "tray: …and its path survives");

    // nothing at all is refused rather than registered as an empty item
    check(TrayHost::splitItemService(QString(), sender).first.isEmpty(),
          "tray: an empty registration is refused");
    check(TrayHost::splitItemService(QStringLiteral("   "), sender)
              .first.isEmpty(),
          "tray: whitespace is refused too");

    check(TrayHost::hostServiceName(4321)
              == QStringLiteral("org.kde.StatusNotifierHost-4321"),
          "tray: the host name carries the pid");

    // The X tray manager's selection name. An application looks for exactly
    // this string; a typo here and every legacy tray icon in the session goes
    // nowhere, with no error anywhere.
    check(XEmbedTray::selectionAtomName(0)
              == QStringLiteral("_NET_SYSTEM_TRAY_S0"),
          "xembed: the selection name for screen 0");
    check(XEmbedTray::selectionAtomName(2)
              == QStringLiteral("_NET_SYSTEM_TRAY_S2"),
          "xembed: …and it carries the screen number");

    // The row of docked icons. Empty means *no* width: the tray well must
    // close up rather than leave a hole next to the clock.
    check(XEmbedTray::widthFor(0) == 0, "xembed: an empty tray takes no room");
    check(XEmbedTray::widthFor(1) == 22, "xembed: one icon is one icon wide");
    check(XEmbedTray::widthFor(3) == 22 * 3 + 4 * 2,
          "xembed: three icons carry two gaps");

    // The colour behind a ParentRelative icon: the panel gradient's midpoint
    // dimmed by the tray well. Checked on a token set we control, because a
    // wrong value here is invisible in code and glaring on screen.
    check(CastaliaPanel::trayWellColor(QColor(100, 140, 200),
                                      QColor(60, 60, 100))
              == QColor(64, 80, 119),
          "xembed: the well is the gradient midpoint, dimmed");
    check(CastaliaPanel::trayWellColor(QColor(), QColor(30, 30, 30))
              == QColor(30, 30, 30),
          "xembed: a half-defined theme still gives a real colour");

    // Alt+Tab (§7.6). The cycle wraps in both directions; getting this wrong
    // means the switcher skips the window the user was reaching for.
    check(Switcher::step(3, 0, true) == 1, "switcher: forward from the first");
    check(Switcher::step(3, 2, true) == 0, "switcher: forward wraps round");
    check(Switcher::step(3, 0, false) == 2, "switcher: back wraps round");
    check(Switcher::step(0, 0, true) == 0, "switcher: no windows, no crash");

    // Most-recently-used order — the half of Alt+Tab people actually feel.
    {
        const QVector<uint32_t> clients = {10, 20, 30};
        // 30 focused last, then 20; 10 has never been focused. A window that
        // has since closed (99) must not resurrect itself into the list.
        const QVector<uint32_t> order =
            Switcher::orderByMru(clients, {30, 99, 20});
        check(order == QVector<uint32_t>({30, 20, 10}),
              "switcher: MRU first, then the WM's order, closed ones dropped");
        check(Switcher::orderByMru(clients, {})
                  == QVector<uint32_t>({10, 20, 30}),
              "switcher: a fresh session falls back to the WM's order");
    }

    // _NET_WM_ICON comes from other people's programs, so it is parsed
    // defensively: two images here, and the one at least 26 px wins.
    {
        QVector<uint32_t> data;
        data << 2 << 2 << 0xFF112233u << 0xFF112233u << 0xFF112233u
             << 0xFF112233u;                      // a 2x2 image
        data << 32 << 32;                          // …and a 32x32 one
        for (int i = 0; i < 32 * 32; ++i)
            data << 0xFF445566u;
        const QImage best = Switcher::decodeIcon(data, 26);
        check(best.width() == 32 && best.height() == 32,
              "switcher: the icon at least as big as the row wins");
        check(best.pixel(0, 0) == 0xFF445566u,
              "switcher: ARGB words land as ARGB pixels");
        check(Switcher::decodeIcon({}, 26).isNull(),
              "switcher: no icon property is not an icon");
        // A width that lies about the data that follows must not walk off it.
        check(Switcher::decodeIcon({64, 64, 0xFFFFFFFFu}, 26).isNull(),
              "switcher: a truncated icon is refused, not read past");
    }

    // The roster the menu and the switcher share (§7.3, §7.6).
    check(castalia::apps::iconForBinary(QStringLiteral("castalia-calc"))
              == QStringLiteral("calculator"),
          "roster: a binary maps to its icon");
    check(castalia::apps::iconForBinary(QStringLiteral("firefox")).isEmpty(),
          "roster: a program that is not ours has no roster icon");

    // The network light in the tray (§9). The loopback is always up, so a
    // machine with the cable out would otherwise claim a connection.
    {
        using NetLink = CastaliaPanel::NetLink;
        const QVector<NetLink> loopbackOnly = {{QStringLiteral("lo"),
                                                QStringLiteral("up"), false}};
        check(!CastaliaPanel::anyLinkUp(loopbackOnly),
              "net: the loopback is not a network connection");
        check(CastaliaPanel::networkTooltip(loopbackOnly)
                  == QStringLiteral("Sin conexión de red"),
              "net: …and the tooltip says so plainly");

        const QVector<NetLink> wired = {
            {QStringLiteral("lo"), QStringLiteral("up"), false},
            {QStringLiteral("enp3s0"), QStringLiteral("up"), false},
            {QStringLiteral("wlp2s0"), QStringLiteral("down"), true}};
        check(CastaliaPanel::anyLinkUp(wired), "net: a live cable counts");
        check(CastaliaPanel::networkTooltip(wired)
                  == QStringLiteral("Conectado por cable (enp3s0)"),
              "net: …and the tooltip names the interface");

        const QVector<NetLink> wifi = {
            {QStringLiteral("lo"), QStringLiteral("up"), false},
            {QStringLiteral("enp3s0"), QStringLiteral("down"), false},
            {QStringLiteral("wlp2s0"), QStringLiteral("up"), true}};
        check(CastaliaPanel::networkTooltip(wifi)
                  == QStringLiteral("Conectado por Wi-Fi (wlp2s0)"),
              "net: a wireless link is named as Wi-Fi");

        const QVector<NetLink> unknown = {
            {QStringLiteral("enp3s0"), QStringLiteral("unknown"), false}};
        check(!CastaliaPanel::anyLinkUp(unknown),
              "net: \"unknown\" is not \"up\" — we do not guess");
        check(!CastaliaPanel::anyLinkUp({}),
              "net: a machine with no interfaces is not connected");
    }

    // the hidden menu entry's words (§ docs/EASTER-EGGS.md)
    check(!CastaliaMenu::isSecretWord(QStringLiteral("notas")),
          "menu: an ordinary word reveals nothing");

    if (fail == 0)
        qInfo("castalia-panel selftest: all checks passed");
    return fail == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    // The self-test runs before any QApplication: it touches no widget, and a
    // gate that needs a display is a gate CI cannot run on a build machine.
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--selftest")
            return selftest();

    QElapsedTimer startup;
    startup.start();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-panel"));

    QCommandLineParser cli;
    cli.setApplicationDescription(
        QStringLiteral("Castalia panel — Phase 0 PoC"));
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("width"), QStringLiteral("Panel width px"),
                   QStringLiteral("px"), QStringLiteral("1024")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render panel to PNG and exit"),
                   QStringLiteral("file")});
    cli.addOption({QStringLiteral("menu-shot"),
                   QStringLiteral("Also render the open menu to PNG"),
                   QStringLiteral("file")});
    cli.addOption({QStringLiteral("selftest"),
                   QStringLiteral("Run the head-less panel gate and exit")});
    cli.addOption({QStringLiteral("switcher-shot"),
                   QStringLiteral("Also render the Alt+Tab switcher to PNG"),
                   QStringLiteral("file")});
    cli.addOption({QStringLiteral("menu-query"),
                   QStringLiteral("Pre-fill the menu search box before "
                                  "--menu-shot"),
                   QStringLiteral("text")});
    cli.process(app);

    const QDir repo(cli.value(QStringLiteral("repo")));
    const QString themeId = cli.value(QStringLiteral("theme"));
    const QString conf =
        repo.filePath(QStringLiteral("themes/%1/theme.conf").arg(themeId));

    const ThemeTokens tokens = ThemeTokens::load(conf);
    if (!tokens.isValid()) {
        std::fprintf(stderr, "castalia-panel: cannot read tokens: %s\n",
                     qPrintable(conf));
        return 2;
    }

    // the generated stylesheet + token-derived shell styling (§6.16)
    const QString qssPath = repo.filePath(
        QStringLiteral("build/out/themes/%1/castalia.qss").arg(themeId));
    const QString qss = readFile(qssPath);
    if (qss.isEmpty())
        std::fprintf(stderr,
                     "castalia-panel: warning: no generated QSS at %s "
                     "(run tools/theme_export.py)\n",
                     qPrintable(qssPath));
        // The interface language, before a single widget exists: Qt cannot
    // retranslate a label that has already been built (§7.13). The apps get
    // this from castalia::applyTheme(); the three shell planes build their
    // own stylesheet, so they ask for it here.
    castalia::locale::applyConfigured(&app, repo.path());

    app.setStyleSheet(qss + panelQss(tokens));

    // Screenshot/offscreen renders show the deterministic demo taskbar; a
    // live session (no --screenshot) shows the real EWMH window list.
    const bool demoTasks = !cli.value(QStringLiteral("screenshot")).isEmpty();
    CastaliaPanel panel(tokens, cli.value(QStringLiteral("width")).toInt(),
                        demoTasks);
    panel.show();

    std::printf("castalia-panel: theme=%s panel=%dpx startup=%lldms rss=%ldKiB\n",
                qPrintable(tokens.themeId()), tokens.panelHeight(),
                static_cast<long long>(startup.elapsed()), rssKiB());

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty()) {
        const QString menuShot = cli.value(QStringLiteral("menu-shot"));
        const QString switcherShot =
            cli.value(QStringLiteral("switcher-shot"));
        QTimer::singleShot(60, &app, [&]() {
            panel.grab().save(shot);
            std::printf("castalia-panel: wrote %s\n", qPrintable(shot));
            if (!menuShot.isEmpty()) {
                panel.menu()->show();
                // after show(): showEvent() clears the box on every open
                panel.menu()->setQuery(cli.value(
                    QStringLiteral("menu-query")));
                panel.menu()->grab().save(menuShot);
                std::printf("castalia-panel: wrote %s\n",
                            qPrintable(menuShot));
            }
            if (!switcherShot.isEmpty()) {
                panel.switcher()->showDemo();
                panel.switcher()->grab().save(switcherShot);
                std::printf("castalia-panel: wrote %s\n",
                            qPrintable(switcherShot));
            }
            app.quit();
        });
    }

    return app.exec();
}
