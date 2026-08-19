// castalia-acerca — "Acerca de Castalia" (About this computer) — Bible §9,
// §10 XP-parity.
//
// The XP-era "Acerca de" / System Information box: the Castalia mark, the
// product name and version, and an honest read of the machine — kernel,
// architecture, processor, memory, the Qt/shell it runs on, and the host name.
// Everything comes from QSysInfo and /proc, so it renders anywhere (including
// the offscreen CI gate) and never guesses. The version is compiled in from
// the project's canonical VERSION (CMake PROJECT_VERSION), so it can't drift.
//
// Usage: castalia-acerca --theme human [--repo PATH] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>

#include "Mark.h"
#include "Theme.h"

#ifndef CASTALIA_VERSION
#define CASTALIA_VERSION "0.1.0"
#endif

namespace {

// First "model name" from /proc/cpuinfo (the CPU as the kernel sees it).
QString cpuModel()
{
    QFile f(QStringLiteral("/proc/cpuinfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const auto lines = QString::fromUtf8(f.readAll())
                           .split(QLatin1Char('\n'));
    for (const QString &ln : lines)
        if (ln.startsWith(QStringLiteral("model name")))
            return ln.section(QLatin1Char(':'), 1).trimmed();
    return QString();
}

// Total RAM from /proc/meminfo (MemTotal is in kB).
QString memTotal()
{
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const auto lines = QString::fromUtf8(f.readAll())
                           .split(QLatin1Char('\n'));
    for (const QString &ln : lines) {
        if (!ln.startsWith(QStringLiteral("MemTotal")))
            continue;
        const qint64 kb = ln.section(QLatin1Char(':'), 1).trimmed()
                              .section(QLatin1Char(' '), 0, 0).toLongLong();
        if (kb > 0)
            return QLocale().formattedDataSize(kb * 1024, 1,
                QLocale::DataSizeTraditionalFormat);
    }
    return QString();
}

// The Castalia mark for the header. libcastalia-ui owns which of the two
// expressions to use at a given size (the artwork above 32 px, the vector
// re-drawing below), so the About box does not get to have its own opinion.
QPixmap markPixmap(const QString &repo, int px)
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    castalia::drawMark(&p, QRectF(0, 0, px, px), repo);
    return pm;
}

} // namespace

class Acerca : public QWidget {
    Q_OBJECT
public:
    Acerca(const QString &repo, const ThemeTokens &tokens)
        : m_repo(repo), m_tokens(tokens)
    {
        setWindowTitle(QStringLiteral("Acerca de Castalia"));
        resize(500, 420);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // Header: the mark + the product name on the titlebar gradient.
        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("AbHeader"));
        head->setFixedHeight(76);
        head->setStyleSheet(QStringLiteral(
            "#AbHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(18, 0, 18, 0);
        hl->setSpacing(14);
        const QPixmap mark = markPixmap(m_repo, 48);
        if (!mark.isNull()) {
            auto *logo = new QLabel(head);
            logo->setPixmap(mark);
            hl->addWidget(logo);
        }
        auto *name = new QLabel(head);
        name->setText(QStringLiteral(
            "<span style='color:%1;font-size:22px;font-weight:bold'>Castalia "
            "OS</span><br><span style='color:%1'>Edición Classic · vista "
            "previa para desarrolladores</span>")
            .arg(colorTok("titlebar_text")));
        hl->addWidget(name);
        hl->addStretch(1);
        root->addWidget(head);

        auto *body = new QVBoxLayout;
        body->setContentsMargins(22, 18, 22, 16);
        body->setSpacing(10);

        auto *form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(8);
        addRow(form, QStringLiteral("Versión:"),
               QString::fromLatin1(CASTALIA_VERSION));
        addRow(form, QStringLiteral("Núcleo:"),
               QStringLiteral("%1 %2").arg(QSysInfo::kernelType(),
                                           QSysInfo::kernelVersion()));
        addRow(form, QStringLiteral("Arquitectura:"),
               QSysInfo::currentCpuArchitecture());
        const QString cpu = cpuModel();
        if (!cpu.isEmpty())
            addRow(form, QStringLiteral("Procesador:"), cpu);
        const QString mem = memTotal();
        if (!mem.isEmpty())
            addRow(form, QStringLiteral("Memoria:"), mem);
        addRow(form, QStringLiteral("Entorno:"),
               QStringLiteral("Castalia Shell · Qt %1")
                   .arg(QString::fromLatin1(qVersion())));
        addRow(form, QStringLiteral("Equipo:"), QSysInfo::machineHostName());
        body->addLayout(form);
        body->addStretch(1);

        auto *studio = new QLabel(this);
        studio->setText(QStringLiteral(
            "<b>Tombatossals Softworks</b><br>"
            "Dave Abellán · Claudio di Castello<br>"
            "hello@tombatossalssoftworks.com · tombatossalssoftworks.com"));
        studio->setProperty("secondary", true);
        studio->setTextInteractionFlags(Qt::TextSelectableByMouse);
        body->addWidget(studio);

        auto *legal = new QLabel(
            QStringLiteral("Software original. Sin código, marcas ni recursos "
                           "de Microsoft."), this);
        legal->setProperty("secondary", true);
        legal->setWordWrap(true);
        body->addWidget(legal);
        root->addLayout(body, 1);
    }

private:
    void addRow(QFormLayout *form, const QString &k, const QString &v)
    {
        auto *val = new QLabel(v.isEmpty() ? QStringLiteral("—") : v, this);
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(k, val);
    }
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }

    QString m_repo;
    ThemeTokens m_tokens;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-acerca"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const ThemeTokens tokens = castalia::applyTheme(&app, repo, themeId);

    Acerca w(repo, tokens);
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
