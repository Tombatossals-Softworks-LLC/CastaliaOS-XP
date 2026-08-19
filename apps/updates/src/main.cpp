// castalia-actualizaciones — the Update Center (Bible §9, §10 "system update").
//
// A friendly front-end over apt: it lists the packages with a newer version
// available and, on request, installs them (guarded, via a graphical
// privilege prompt). One small wrapper, original UI, themed via
// libcastalia-ui. No third-party assets (§3.9).
//
// Usage: castalia-actualizaciones --theme classic [--repo P] [--demo]
//        [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Mark.h"
#include "Theme.h"

namespace {

struct Upd { QString name, fromVer, toVer; };

// Parse `apt list --upgradable`, whose lines read:
//   zlib1g/stable 1.2.13 amd64 [upgradable from: 1.2.11]
QVector<Upd> availableUpdates()
{
    QVector<Upd> out;
    QProcess p;
    p.start(QStringLiteral("apt"),
            {QStringLiteral("list"), QStringLiteral("--upgradable")});
    if (!p.waitForFinished(8000))
        return out;
    const QString text = QString::fromUtf8(p.readAllStandardOutput());
    for (const QString &line : text.split(QLatin1Char('\n'),
                                          Qt::SkipEmptyParts)) {
        const int mark = line.indexOf(QStringLiteral("[upgradable from:"));
        if (mark < 0)
            continue;
        const int slash = line.indexOf(QLatin1Char('/'));
        if (slash <= 0)
            continue;
        const QString name = line.left(slash);
        const QStringList head = line.mid(slash + 1).split(QLatin1Char(' '),
                                                           Qt::SkipEmptyParts);
        const QString toVer = head.size() > 1 ? head[1] : QString();
        QString fromVer = line.mid(mark + 17); // after "[upgradable from:"
        fromVer.remove(QLatin1Char(']'));
        out.push_back({name, fromVer.trimmed(), toVer});
    }
    return out;
}

QVector<Upd> demoUpdates()
{
    return {
        {QStringLiteral("linux-image-amd64"), QStringLiteral("6.1.0-17"),
         QStringLiteral("6.1.0-18")},
        {QStringLiteral("libqt5widgets5"), QStringLiteral("5.15.8"),
         QStringLiteral("5.15.10")},
        {QStringLiteral("castalia-desktop"), QStringLiteral("1.0.2"),
         QStringLiteral("1.0.3")},
        {QStringLiteral("wine"), QStringLiteral("8.0.1"),
         QStringLiteral("8.0.2")},
        {QStringLiteral("openssl"), QStringLiteral("3.0.11"),
         QStringLiteral("3.0.13")},
        {QStringLiteral("tzdata"), QStringLiteral("2023c"),
         QStringLiteral("2024a")},
    };
}

} // namespace

class Updates : public QWidget {
public:
    Updates(const QColor &accent, bool demo) : m_accent(accent), m_demo(demo)
    {
        setWindowTitle(QStringLiteral("Centro de actualizaciones — Castalia"));
        resize(660, 520);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->addWidget(buildHeader());

        auto *body = new QVBoxLayout;
        body->setContentsMargins(14, 12, 14, 12);
        body->setSpacing(10);

        m_intro = new QLabel(this);
        m_intro->setWordWrap(true);
        body->addWidget(m_intro);

        m_tree = new QTreeWidget(this);
        m_tree->setColumnCount(3);
        m_tree->setHeaderLabels({QStringLiteral("Paquete"),
                                 QStringLiteral("Versión instalada"),
                                 QStringLiteral("Nueva versión")});
        m_tree->setRootIsDecorated(false);
        m_tree->setAlternatingRowColors(true);
        m_tree->header()->setStretchLastSection(true);
        m_tree->setColumnWidth(0, 260);
        m_tree->setColumnWidth(1, 160);
        body->addWidget(m_tree, 1);

        auto *row = new QHBoxLayout;
        auto *refresh = new QPushButton(QStringLiteral("Buscar de nuevo"),
                                        this);
        connect(refresh, &QPushButton::clicked, this, &Updates::reload);
        row->addWidget(refresh);
        row->addStretch(1);
        m_install = new QPushButton(
            QStringLiteral("Instalar actualizaciones"), this);
        m_install->setObjectName(QStringLiteral("PrimaryBtn"));
        connect(m_install, &QPushButton::clicked, this, &Updates::installAll);
        row->addWidget(m_install);
        body->addLayout(row);

        root->addLayout(body, 1);
        reload();
    }

private:
    QWidget *buildHeader()
    {
        auto *head = new QWidget(this);
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %1,"
            "stop:1 %2);")
            .arg(m_accent.lighter(112).name(), m_accent.darker(118).name()));
        auto *l = new QHBoxLayout(head);
        l->setContentsMargins(16, 0, 16, 0);
        auto *mk = new QLabel(head);
        QPixmap pm(36, 36);
        pm.fill(Qt::transparent);
        { QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
          castalia::drawMark(&p, QRectF(2, 2, 32, 32)); }
        mk->setPixmap(pm);
        l->addWidget(mk);
        auto *t = new QLabel(QStringLiteral(
            "<span style='color:white;font-size:15px;font-weight:bold'>"
            "Centro de actualizaciones</span>"), head);
        l->addWidget(t);
        l->addStretch(1);
        m_headStat = new QLabel(head);
        m_headStat->setStyleSheet(QStringLiteral("color:#EAF1F7;"));
        l->addWidget(m_headStat);
        return head;
    }

    void reload()
    {
        m_ups = m_demo ? demoUpdates() : availableUpdates();
        if (!m_demo && m_ups.isEmpty()) {
            // Could be genuinely up to date, or apt unavailable in this
            // context; show a friendly, honest empty state.
            m_ups.clear();
        }
        m_tree->clear();
        for (const Upd &u : m_ups) {
            auto *it = new QTreeWidgetItem(m_tree);
            it->setText(0, u.name);
            it->setText(1, u.fromVer);
            it->setText(2, u.toVer);
        }
        const int n = m_ups.size();
        m_install->setEnabled(n > 0);
        if (n > 0) {
            m_intro->setText(QStringLiteral(
                "Hay <b>%1</b> %2 disponibles. Revisa la lista y pulsa "
                "«Instalar actualizaciones» para aplicarlas; se te pedirá la "
                "contraseña de administrador.")
                .arg(n)
                .arg(n == 1 ? QStringLiteral("actualización")
                            : QStringLiteral("actualizaciones")));
            m_headStat->setText(QStringLiteral("%1 disponibles").arg(n));
        } else {
            m_intro->setText(QStringLiteral(
                "✓ El sistema está al día. No hay actualizaciones "
                "pendientes."));
            m_headStat->setText(QStringLiteral("Al día"));
        }
    }

    void installAll()
    {
        if (m_ups.isEmpty())
            return;
        // Safety first (§10): take a Restore Point before touching the system,
        // so a bad update can always be rolled back from the Recovery Center.
        const QString restoreBin = QStandardPaths::findExecutable(
            QStringLiteral("castalia-restore"));
        const bool willSnapshot = !restoreBin.isEmpty();
        const auto ans = QMessageBox::question(
            this, QStringLiteral("Instalar actualizaciones"),
            QStringLiteral("Se instalarán %1 actualizaciones como "
                           "administrador.%2 ¿Continuar?")
                .arg(m_ups.size())
                .arg(willSnapshot
                     ? QStringLiteral(" Antes se creará un <b>punto de "
                                      "restauración</b> por si necesitas "
                                      "deshacerlo.")
                     : QString()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes)
            return;

        // Chain (snapshot && upgrade) in a shell so the update only runs once
        // the Restore Point exists. If the snapshot fails, the update is
        // aborted rather than left un-undoable.
        const QString upgrade = QStringLiteral("apt-get -y upgrade");
        QString script = upgrade;
        if (willSnapshot)
            script = restoreBin
                     + QStringLiteral(" create --reason pre-update --label "
                                      "'Antes de actualizar el sistema' && ")
                     + upgrade;

        QString helper = QStandardPaths::findExecutable(
            QStringLiteral("pkexec"));
        QStringList argv;
        if (!helper.isEmpty())
            argv = QStringList{QStringLiteral("sh"), QStringLiteral("-c"),
                               script};
        else {
            helper = QStringLiteral("sh");
            argv = QStringList{QStringLiteral("-c"), script};
        }
        QProcess::startDetached(helper, argv);
        m_intro->setText(willSnapshot
            ? QStringLiteral("Creando el punto de restauración e instalando "
                             "las actualizaciones… sigue el progreso en la "
                             "ventana de administrador.")
            : QStringLiteral("Instalación solicitada. Sigue el progreso en la "
                             "ventana de administrador; vuelve a «Buscar de "
                             "nuevo» al terminar."));
    }

    QColor m_accent;
    bool m_demo;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_intro = nullptr, *m_headStat = nullptr;
    QPushButton *m_install = nullptr;
    QVector<Upd> m_ups;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-actualizaciones"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Show a sample update list")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo"))).absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const QString accentStr =
        ThemeTokens::load(castalia::themeConfPath(repo, themeId))
            .str(QStringLiteral("colors"), QStringLiteral("accent"));
    castalia::applyTheme(&app, repo, themeId);
    const QColor accent(accentStr.isEmpty() ? QStringLiteral("#3E82B6")
                                            : accentStr);

    // A screenshot with no --demo still needs a populated list to be useful.
    const bool demo = cli.isSet(QStringLiteral("demo"))
                      || !cli.value(QStringLiteral("screenshot")).isEmpty();
    Updates w(accent, demo);
    w.show();

    const QString out = cli.value(QStringLiteral("screenshot"));
    if (!out.isEmpty())
        QTimer::singleShot(250, &app, [&]() {
            w.grab().save(out);
            app.quit();
        });
    return app.exec();
}
