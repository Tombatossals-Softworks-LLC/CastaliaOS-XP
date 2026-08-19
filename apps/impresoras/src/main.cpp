// castalia-impresoras — the Castalia Printers panel (Bible §9, §10 XP-parity).
//
// The XP-era "Impresoras y faxes", done honestly over CUPS. It lists the
// configured printers with their status and which one is the default, shows
// the print queue, and lets you set your default printer (user-scoped, via
// `lpoptions -d` → ~/.cups/lpoptions), cancel a job, or refresh. It speaks the
// standard CUPS command-line (`lpstat`, `cancel`, `lpoptions`); on a machine
// with no print system it says so instead of pretending. Pure Qt5.
//
// `--demo` shows a representative, read-only setup (no CUPS calls) so the
// offscreen render gate and the live suite have something to display.
//
// Usage: castalia-impresoras --theme human [--repo PATH] [--demo]
//                            [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include "Theme.h"

namespace {

struct Printer {
    QString name, status;
    bool isDefault = false;
};

// Run a command, return trimmed stdout (empty on failure/timeout).
QString run(const QString &bin, const QStringList &args)
{
    QProcess p;
    p.start(bin, args);
    if (!p.waitForFinished(1500))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput());
}

bool haveCups()
{
    return !QStandardPaths::findExecutable(QStringLiteral("lpstat")).isEmpty();
}

} // namespace

class Impresoras : public QWidget {
    Q_OBJECT
public:
    Impresoras(const QString &repo, const ThemeTokens &tokens, bool demo)
        : m_repo(repo), m_tokens(tokens), m_demo(demo), m_cups(haveCups())
    {
        setWindowTitle(QStringLiteral("Impresoras — Castalia"));
        resize(620, 400);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("PrHeader"));
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "#PrHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *icon = new QLabel(head);
        icon->setPixmap(castalia::themeIcon(m_repo, QStringLiteral("printer"))
                            .pixmap(28, 28));
        hl->addWidget(icon);
        m_title = new QLabel(head);
        const QString sub = m_demo ? QStringLiteral("vista de ejemplo")
            : (m_cups ? QStringLiteral("CUPS") : QStringLiteral("sin CUPS"));
        m_title->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>Impresoras"
            "</span>&nbsp;&nbsp;<span style='color:%1'>%2</span>")
            .arg(colorTok("titlebar_text"), sub));
        hl->addWidget(m_title);
        hl->addStretch(1);
        root->addWidget(head);

        auto *body = new QHBoxLayout;
        body->setContentsMargins(14, 12, 14, 8);
        body->setSpacing(14);

        auto *left = new QVBoxLayout;
        auto *plabel = new QLabel(QStringLiteral("Impresoras"), this);
        plabel->setProperty("secondary", true);
        left->addWidget(plabel);
        m_printers = new QListWidget(this);
        connect(m_printers, &QListWidget::currentRowChanged, this,
                &Impresoras::onPrinterChanged);
        left->addWidget(m_printers, 1);
        body->addLayout(left, 2);

        auto *right = new QVBoxLayout;
        auto *qlabel = new QLabel(QStringLiteral("Cola de impresión"), this);
        qlabel->setProperty("secondary", true);
        right->addWidget(qlabel);
        m_jobs = new QListWidget(this);
        connect(m_jobs, &QListWidget::currentRowChanged, this,
                &Impresoras::syncButtons);
        right->addWidget(m_jobs, 1);
        m_note = new QLabel(this);
        m_note->setWordWrap(true);
        m_note->setProperty("secondary", true);
        right->addWidget(m_note);
        body->addLayout(right, 3);
        root->addLayout(body, 1);

        auto *bar = new QHBoxLayout;
        bar->setContentsMargins(14, 0, 14, 14);
        m_refresh = mkButton(QStringLiteral("Actualizar"), QString());
        connect(m_refresh, &QPushButton::clicked, this, &Impresoras::reload);
        bar->addWidget(m_refresh);
        bar->addStretch(1);
        m_setDefault = mkButton(QStringLiteral("Predeterminada"), QString());
        connect(m_setDefault, &QPushButton::clicked, this,
                &Impresoras::setDefault);
        bar->addWidget(m_setDefault);
        m_cancel = mkButton(QStringLiteral("Cancelar trabajo"),
                            QStringLiteral("PrCancel"));
        connect(m_cancel, &QPushButton::clicked, this, &Impresoras::cancelJob);
        bar->addWidget(m_cancel);
        root->addLayout(bar);

        reload();
    }

private slots:
    void reload()
    {
        m_printers->clear();
        m_list = m_demo ? demoPrinters() : readPrinters();
        for (const Printer &p : m_list) {
            const QString star = p.isDefault ? QStringLiteral("  ★") : QString();
            auto *it = new QListWidgetItem(
                castalia::themeIcon(m_repo, QStringLiteral("printer")),
                QStringLiteral("%1 — %2%3").arg(p.name, p.status, star),
                m_printers);
            it->setData(Qt::UserRole, p.name);
        }
        if (!m_list.isEmpty())
            m_printers->setCurrentRow(0);
        else
            onPrinterChanged(-1);

        if (m_demo)
            m_note->setText(QStringLiteral(
                "Vista de ejemplo: no se han consultado impresoras reales."));
        else if (!m_cups)
            m_note->setText(QStringLiteral(
                "No se detecta CUPS (lpstat). Las impresoras aparecerán "
                "cuando el sistema de impresión esté disponible."));
        else if (m_list.isEmpty())
            m_note->setText(QStringLiteral(
                "No hay impresoras configuradas en CUPS."));
        else
            m_note->clear();
    }

    void onPrinterChanged(int)
    {
        m_jobs->clear();
        const QString name = currentPrinter();
        if (!name.isEmpty()) {
            const QStringList jobs = m_demo ? demoJobs(name) : readJobs(name);
            for (const QString &j : jobs)
                new QListWidgetItem(j, m_jobs);
        }
        syncButtons();
    }

    void setDefault()
    {
        const QString name = currentPrinter();
        if (m_demo || !m_cups || name.isEmpty())
            return;
        // User-scoped default (~/.cups/lpoptions) — no privilege needed.
        QProcess::execute(QStringLiteral("lpoptions"),
                          {QStringLiteral("-d"), name});
        reload();
    }

    void cancelJob()
    {
        if (m_demo || !m_cups)
            return;
        auto *it = m_jobs->currentItem();
        if (!it)
            return;
        // The job id is the first token of the queue line.
        const QString job = it->text().section(QLatin1Char(' '), 0, 0);
        if (job.isEmpty())
            return;
        QProcess::execute(QStringLiteral("cancel"), {job});
        onPrinterChanged(m_printers->currentRow());
    }

    void syncButtons()
    {
        const bool live = m_cups && !m_demo;
        m_setDefault->setEnabled(live && !currentPrinter().isEmpty());
        m_cancel->setEnabled(live && m_jobs->currentItem() != nullptr);
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
    QString currentPrinter() const
    {
        auto *it = m_printers->currentItem();
        return it ? it->data(Qt::UserRole).toString() : QString();
    }

    // Parse `lpstat -p` (status) + `lpstat -d` (default) into printers.
    QList<Printer> readPrinters()
    {
        QList<Printer> out;
        if (!m_cups)
            return out;
        QString def = run(QStringLiteral("lpstat"), {QStringLiteral("-d")});
        QString defName;
        QRegularExpression dre(QStringLiteral("destination:\\s*(\\S+)"));
        const auto dm = dre.match(def);
        if (dm.hasMatch())
            defName = dm.captured(1);
        const QString ps = run(QStringLiteral("lpstat"), {QStringLiteral("-p")});
        const auto lines = ps.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QRegularExpression re(
            QStringLiteral("^printer\\s+(\\S+)\\s+is\\s+([^.]+)"));
        for (const QString &ln : lines) {
            const auto m = re.match(ln);
            if (!m.hasMatch())
                continue;
            Printer p;
            p.name = m.captured(1);
            p.status = translateStatus(m.captured(2).trimmed());
            p.isDefault = p.name == defName;
            out.append(p);
        }
        return out;
    }

    QStringList readJobs(const QString &name)
    {
        QStringList out;
        const QString o = run(QStringLiteral("lpstat"),
                              {QStringLiteral("-o"), name});
        for (const QString &ln : o.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            out << ln.simplified();
        if (out.isEmpty())
            out << QStringLiteral("(sin trabajos en cola)");
        return out;
    }

    static QString translateStatus(const QString &en)
    {
        if (en.startsWith(QStringLiteral("idle")))
            return QStringLiteral("inactiva");
        if (en.startsWith(QStringLiteral("printing")))
            return QStringLiteral("imprimiendo");
        if (en.contains(QStringLiteral("disabled")))
            return QStringLiteral("deshabilitada");
        return en;
    }

    QList<Printer> demoPrinters()
    {
        return {
            {QStringLiteral("Oficina-LaserJet"),
             QStringLiteral("inactiva"), true},
            {QStringLiteral("Fotos-InkJet"),
             QStringLiteral("imprimiendo"), false},
        };
    }
    QStringList demoJobs(const QString &name)
    {
        if (name == QStringLiteral("Fotos-InkJet"))
            return {QStringLiteral("Fotos-InkJet-318  dave  vacaciones.jpg  "
                                   "1,2 MB  imprimiendo")};
        return {QStringLiteral("(sin trabajos en cola)")};
    }

    QString m_repo;
    ThemeTokens m_tokens;
    bool m_demo = false, m_cups = false;
    QList<Printer> m_list;
    QLabel *m_title = nullptr, *m_note = nullptr;
    QListWidget *m_printers = nullptr, *m_jobs = nullptr;
    QPushButton *m_refresh = nullptr, *m_setDefault = nullptr,
                *m_cancel = nullptr;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-impresoras"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Show a representative read-only setup")});
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
        QStringLiteral("#PrCancel:enabled{font-weight:bold;border-color:%1;}")
            .arg(accent));

    Impresoras w(repo, tokens, cli.isSet(QStringLiteral("demo")));
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
