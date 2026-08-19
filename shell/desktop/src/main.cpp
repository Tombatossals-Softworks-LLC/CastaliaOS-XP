// castalia-desktop — Phase 0 proof of concept (Bible §7.5, §18).
//
// The desktop layer: Azure Bay wallpaper, selectable desktop icons with
// shadowed labels, right-click menu, and per-process Explorer launch.
// --panel-png composes a castalia-panel render onto the bottom edge so a
// single PNG shows the complete native Castalia desktop.
//
// Usage:
//   castalia-desktop --theme classic [--repo PATH] [--size 1024x768]
//                    [--screenshot out.png [--panel-png panel.png]]

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTimer>

#include <cstdio>

#include "Locale.h"
#include "DesktopWindow.h"
#include "ThemeTokens.h"

namespace {

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
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

} // namespace

int main(int argc, char **argv)
{
    QElapsedTimer startup;
    startup.start();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-desktop"));

    QCommandLineParser cli;
    cli.setApplicationDescription(
        QStringLiteral("Castalia desktop layer — Phase 0 PoC"));
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("size"), QStringLiteral("WxH (default: "
                   "full primary screen)"), QStringLiteral("wxh")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.addOption({QStringLiteral("panel-png"),
                   QStringLiteral("Compose this panel render at the bottom"),
                   QStringLiteral("file")});
    // The desktop's hidden flourish, on demand: the same overlay the key
    // sequence wakes. It exists as a flag so the render gate and the press kit
    // can capture it without pretending to type a cheat code.
    cli.addOption({QStringLiteral("easter-egg"),
                   QStringLiteral("Show a flourish at startup (aurora)"),
                   QStringLiteral("name")});
    cli.addOption({QStringLiteral("selftest"),
                   QStringLiteral("Run the head-less desktop gate and exit")});
    cli.process(app);

    const QDir repo(cli.value(QStringLiteral("repo")));
    const QString themeId = cli.value(QStringLiteral("theme"));
    const ThemeTokens tokens = ThemeTokens::load(
        repo.filePath(QStringLiteral("themes/%1/theme.conf").arg(themeId)));
    if (!tokens.isValid()) {
        std::fprintf(stderr, "castalia-desktop: cannot read tokens for %s\n",
                     qPrintable(themeId));
        return 2;
    }

    const QString qss = readFile(repo.filePath(
        QStringLiteral("build/out/themes/%1/castalia.qss").arg(themeId)));
        // The interface language, before a single widget exists: Qt cannot
    // retranslate a label that has already been built (§7.13). The apps get
    // this from castalia::applyTheme(); the three shell planes build their
    // own stylesheet, so they ask for it here.
    castalia::locale::applyConfigured(&app, repo.path());

    app.setStyleSheet(qss);   // the right-click menu is QSS-styled

    QSize size;
    if (cli.isSet(QStringLiteral("size"))) {
        const QStringList wh =
            cli.value(QStringLiteral("size")).split(QLatin1Char('x'));
        size = QSize(wh.value(0).toInt(), wh.value(1).toInt());
    } else if (QGuiApplication::primaryScreen()) {
        size = QGuiApplication::primaryScreen()->geometry().size();
    }
    if (size.isEmpty())
        size = QSize(1024, 768);

    DesktopWindow desktop(tokens, repo.absolutePath(), size);
    desktop.show();

    if (cli.isSet(QStringLiteral("selftest")))
        return desktop.selfTest();

    const QString egg = cli.value(QStringLiteral("easter-egg"));
    if (!egg.isEmpty()) {
        if (egg != QLatin1String("aurora")) {
            std::fprintf(stderr, "castalia-desktop: unknown flourish '%s' "
                                 "(known: aurora)\n", qPrintable(egg));
            return 2;
        }
        desktop.showAurora();
    }

    std::printf("castalia-desktop: theme=%s size=%dx%d startup=%lldms "
                "rss=%ldKiB\n",
                qPrintable(tokens.themeId()), desktop.width(),
                desktop.height(),
                static_cast<long long>(startup.elapsed()), rssKiB());

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty()) {
        const QString panelPng = cli.value(QStringLiteral("panel-png"));
        QTimer::singleShot(80, &app, [&]() {
            QImage frame = desktop.grab().toImage();
            if (!panelPng.isEmpty()) {
                const QImage panel(panelPng);
                if (!panel.isNull()) {
                    QPainter p(&frame);
                    p.drawImage(0, frame.height() - panel.height(), panel);
                }
            }
            frame.save(shot);
            std::printf("castalia-desktop: wrote %s\n", qPrintable(shot));
            app.quit();
        });
    }

    return app.exec();
}
