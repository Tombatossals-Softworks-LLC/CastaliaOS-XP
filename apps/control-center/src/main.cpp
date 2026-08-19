// castalia-control-center — the settings hub (Bible §9.1, §10).
//
// Usage:
//   castalia-control-center --theme classic [--repo PATH]
//                           [--page N] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QListWidget>
#include <QTimer>

#include <cstdio>

#include "ControlCenter.h"
#include "Theme.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(
        QStringLiteral("castalia-control-center"));

    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("page"),
                   QStringLiteral("Open category N (0-based)"),
                   QStringLiteral("n"), QStringLiteral("0")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString theme = cli.value(QStringLiteral("theme"));
    castalia::applyTheme(&app, repo, theme);

    ControlCenter cc(repo, theme);
    cc.show();

    const int page = cli.value(QStringLiteral("page")).toInt();
    if (page > 0) {
        if (auto *list = cc.findChild<QListWidget *>())
            list->setCurrentRow(page);
    }

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty()) {
        QTimer::singleShot(120, &app, [&]() {
            cc.grab().save(shot);
            std::printf("castalia-control-center: wrote %s\n",
                        qPrintable(shot));
            app.quit();
        });
    }
    return app.exec();
}
