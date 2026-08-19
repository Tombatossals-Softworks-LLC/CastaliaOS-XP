// castalia-explorer — Phase 0 proof of concept (Bible §9.1, §18).
//
// A REAL file-manager window over QFileSystemModel: places sidebar, working
// back/forward/up history, address bar, icon/list views, the Castalia icon
// family (via the runtime SVG icon engine in the PoC; production pre-bakes
// PNG per §8.4), the generated QSS, and §16 budget reporting.
//
// Usage:
//   castalia-explorer --theme classic [--repo PATH] [--path DIR]
//                     [--demo] [--screenshot out.png]
//
// --demo creates a small demo home (Documentos/Facturas etc.) and starts
// there, so CI screenshots always show meaningful content.

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>

#include <cstdio>

#include "Locale.h"
#include "ExplorerWindow.h"
#include "ThemeTokens.h"

namespace {

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// Explorer-specific chrome derived from the tokens (sidebar, address bar).
QString explorerQss(const ThemeTokens &t)
{
    return QStringLiteral(R"(
#PlacesList {
    background: %1; border: none;
    border-right: 1px solid %2; padding-top: 6px;
}
#PlacesList::item { padding: 5px 8px; }
#PlacesList::item:selected { background: %3; color: %4; }
#AddressBar { margin-left: 6px; }
#FileView { border: none; }
)")
        .arg(t.str("colors", "surface_alt"), t.str("colors", "border"),
             t.str("colors", "selection_bg"),
             t.str("colors", "selection_text"));
}

QString makeDemoTree()
{
    const QString home =
        QDir::temp().filePath(QStringLiteral("castalia-demo-home"));
    QDir dir(home);
    dir.removeRecursively();
    for (const QString &sub :
         {QStringLiteral("Documentos/Facturas"),
          QStringLiteral("Documentos/Fotos verano"),
          QStringLiteral("Documentos/Música"),
          QStringLiteral("Imágenes"), QStringLiteral("Descargas")})
        QDir().mkpath(home + QLatin1Char('/') + sub);
    for (const QString &f :
         {QStringLiteral("Documentos/notas.txt"),
          QStringLiteral("Documentos/presupuesto.ods"),
          QStringLiteral("Documentos/recetas de la abuela.txt")}) {
        QFile file(home + QLatin1Char('/') + f);
        file.open(QIODevice::WriteOnly);
        file.write("castalia demo\n");
    }
    return home + QStringLiteral("/Documentos");
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
    QApplication::setApplicationName(QStringLiteral("castalia-explorer"));

    QCommandLineParser cli;
    cli.setApplicationDescription(
        QStringLiteral("Castalia Explorer — Phase 0 PoC"));
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("path"), QStringLiteral("Start directory"),
                   QStringLiteral("dir"), QDir::homePath()});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Browse a generated demo home")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QDir repo(cli.value(QStringLiteral("repo")));
    const QString themeId = cli.value(QStringLiteral("theme"));
    const ThemeTokens tokens = ThemeTokens::load(
        repo.filePath(QStringLiteral("themes/%1/theme.conf").arg(themeId)));
    if (!tokens.isValid()) {
        std::fprintf(stderr, "castalia-explorer: cannot read tokens for %s\n",
                     qPrintable(themeId));
        return 2;
    }

    const QString qss = readFile(repo.filePath(
        QStringLiteral("build/out/themes/%1/castalia.qss").arg(themeId)));
    if (qss.isEmpty())
        std::fprintf(stderr, "castalia-explorer: warning: no generated QSS "
                             "(run tools/theme_export.py)\n");
        // The interface language, before a single widget exists: Qt cannot
    // retranslate a label that has already been built (§7.13). The apps get
    // this from castalia::applyTheme(); the three shell planes build their
    // own stylesheet, so they ask for it here.
    castalia::locale::applyConfigured(&app, repo.path());

    app.setStyleSheet(qss + explorerQss(tokens));

    const QString start = cli.isSet(QStringLiteral("demo"))
                              ? makeDemoTree()
                              : cli.value(QStringLiteral("path"));

    ExplorerWindow win(tokens,
                       repo.filePath(QStringLiteral("themes/icons/48")),
                       start);
    win.show();

    std::printf("castalia-explorer: theme=%s startup=%lldms rss=%ldKiB\n",
                qPrintable(tokens.themeId()),
                static_cast<long long>(startup.elapsed()), rssKiB());

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty()) {
        // give QFileSystemModel a beat to populate before grabbing
        QTimer::singleShot(350, &app, [&]() {
            win.grab().save(shot);
            std::printf("castalia-explorer: wrote %s (rss=%ldKiB)\n",
                        qPrintable(shot), rssKiB());
            app.quit();
        });
    }

    return app.exec();
}
