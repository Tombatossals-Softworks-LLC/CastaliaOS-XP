// castalia-recuperacion — the Recovery Center (Bible §9 / §1063, P8).
//
// A themed Qt front-end over the shared, unit-tested Restore Points backend
// (recovery/castalia_recovery): list the points, create a new one, and roll
// the system back to one — the same engine the recovery environment uses,
// surfaced from within a healthy system. Restores are guarded (typed
// confirmation) and run through a graphical privilege prompt. Pure Qt5 +
// libcastalia-ui theming; no third-party assets (§3.9).
//
// Usage: castalia-recuperacion --theme classic [--repo P] [--store DIR]
//        [--demo] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Mark.h"
#include "Theme.h"

namespace {

struct Point { QString id, reason, created, label; };

QString humanReason(const QString &r)
{
    if (r == QLatin1String("manual"))
        return QStringLiteral("Manual");
    if (r == QLatin1String("pre-restore"))
        return QStringLiteral("Antes de restaurar");
    if (r == QLatin1String("pre-update"))
        return QStringLiteral("Antes de actualizar");
    if (r == QLatin1String("scheduled"))
        return QStringLiteral("Programado");
    return r;
}

QVector<Point> demoPoints()
{
    return {
        {QStringLiteral("20260709-093015-000001"), QStringLiteral("manual"),
         QStringLiteral("2026-07-09 09:30"),
         QStringLiteral("Antes de instalar controladores")},
        {QStringLiteral("20260708-201142-000002"),
         QStringLiteral("pre-update"), QStringLiteral("2026-07-08 20:11"),
         QStringLiteral("Actualizaciones del sistema")},
        {QStringLiteral("20260706-112050-000003"),
         QStringLiteral("scheduled"), QStringLiteral("2026-07-06 11:20"),
         QStringLiteral("Punto semanal")},
    };
}

} // namespace

class Recovery : public QWidget {
public:
    Recovery(const QColor &accent, const QString &repo, const QString &store,
             bool demo)
        : m_accent(accent), m_repo(repo), m_store(store), m_demo(demo)
    {
        setWindowTitle(QStringLiteral("Centro de recuperación — Castalia"));
        resize(680, 520);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->addWidget(buildHeader());

        auto *body = new QVBoxLayout;
        body->setContentsMargins(14, 12, 14, 12);
        body->setSpacing(10);

        auto *intro = new QLabel(
            QStringLiteral(
                "Un <b>punto de restauración</b> guarda una instantánea del "
                "sistema para poder volver a él si algo va mal (los datos de "
                "tu carpeta personal no se tocan). Crea uno antes de cambios "
                "importantes."),
            this);
        intro->setWordWrap(true);
        body->addWidget(intro);

        m_tree = new QTreeWidget(this);
        m_tree->setColumnCount(3);
        m_tree->setHeaderLabels({QStringLiteral("Fecha"),
                                 QStringLiteral("Motivo"),
                                 QStringLiteral("Descripción")});
        m_tree->setRootIsDecorated(false);
        m_tree->setAlternatingRowColors(true);
        m_tree->header()->setStretchLastSection(true);
        m_tree->setColumnWidth(0, 175);
        m_tree->setColumnWidth(1, 170);
        body->addWidget(m_tree, 1);

        auto *row = new QHBoxLayout;
        auto *create = new QPushButton(
            QStringLiteral("Crear punto de restauración…"), this);
        create->setObjectName(QStringLiteral("PrimaryBtn"));
        connect(create, &QPushButton::clicked, this, &Recovery::createPoint);
        auto *refresh = new QPushButton(QStringLiteral("Actualizar"), this);
        connect(refresh, &QPushButton::clicked, this, &Recovery::reload);
        row->addWidget(create);
        row->addWidget(refresh);
        row->addStretch(1);
        m_restore = new QPushButton(
            QStringLiteral("Restaurar el seleccionado…"), this);
        connect(m_restore, &QPushButton::clicked, this,
                &Recovery::restoreSelected);
        row->addWidget(m_restore);
        body->addLayout(row);

        m_status = new QLabel(this);
        m_status->setProperty("secondary", true);
        body->addWidget(m_status);

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
            "Centro de recuperación</span>"), head);
        l->addWidget(t);
        l->addStretch(1);
        m_headStat = new QLabel(head);
        m_headStat->setStyleSheet(QStringLiteral("color:#EAF1F7;"));
        l->addWidget(m_headStat);
        return head;
    }

    QProcessEnvironment backendEnv() const
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PYTHONPATH"),
                   m_repo + QStringLiteral("/recovery"));
        return env;
    }

    QStringList baseArgs() const
    {
        return {QStringLiteral("-m"), QStringLiteral("castalia_recovery"),
                QStringLiteral("--store"), m_store};
    }

    // Parse a `list` line: "<id>  <reason>  <YYYY-MM-DD HH:MM>  <label...>".
    static bool parseLine(const QString &line, Point *out)
    {
        const QStringList t = line.split(QLatin1Char(' '),
                                         Qt::SkipEmptyParts);
        if (t.size() < 4)
            return false;
        out->id = t[0];
        out->reason = t[1];
        out->created = t[2] + QLatin1Char(' ') + t[3];
        out->label = QStringList(t.mid(4)).join(QLatin1Char(' '));
        return true;
    }

    QVector<Point> livePoints()
    {
        QVector<Point> pts;
        QProcess p;
        p.setProcessEnvironment(backendEnv());
        QStringList a = baseArgs();
        a << QStringLiteral("list");
        p.start(QStringLiteral("python3"), a);
        if (!p.waitForFinished(6000))
            return pts;
        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        for (const QString &line : out.split(QLatin1Char('\n'),
                                             Qt::SkipEmptyParts)) {
            if (line.startsWith(QLatin1Char('(')))
                continue; // "(sin puntos…)"
            Point pt;
            if (parseLine(line, &pt))
                pts.push_back(pt);
        }
        return pts;
    }

    void reload()
    {
        m_points = m_demo ? demoPoints() : livePoints();
        m_tree->clear();
        for (const Point &pt : m_points) {
            auto *it = new QTreeWidgetItem(m_tree);
            it->setText(0, pt.created);
            it->setText(1, humanReason(pt.reason));
            it->setText(2, pt.label.isEmpty() ? QStringLiteral("—")
                                              : pt.label);
            it->setData(0, Qt::UserRole, pt.id);
            it->setToolTip(0, pt.id);
        }
        const int n = m_points.size();
        m_restore->setEnabled(n > 0);
        m_headStat->setText(n == 1 ? QStringLiteral("1 punto")
                                   : QStringLiteral("%1 puntos").arg(n));
        if (n == 0)
            m_status->setText(QStringLiteral(
                "No hay puntos de restauración todavía. Crea el primero antes "
                "de un cambio importante."));
        else
            m_status->setText(QString());
    }

    // Run a backend subcommand (create/restore) under pkexec, non-blocking.
    void runPrivileged(const QStringList &extra, const QString &note)
    {
        QStringList a = baseArgs();
        a += extra;
        QString helper = QStandardPaths::findExecutable(
            QStringLiteral("pkexec"));
        QStringList argv;
        // pkexec drops the environment, so pass PYTHONPATH via env -.
        const QString pyPath = m_repo + QStringLiteral("/recovery");
        if (!helper.isEmpty()) {
            argv = QStringList{QStringLiteral("env"),
                               QStringLiteral("PYTHONPATH=") + pyPath,
                               QStringLiteral("python3")};
            argv += a;
        } else {
            helper = QStringLiteral("python3");
            argv = a;
        }
        auto *proc = new QProcess(this);
        if (helper == QLatin1String("python3"))
            proc->setProcessEnvironment(backendEnv());
        connect(proc,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, proc, note](int code, QProcess::ExitStatus) {
                    m_status->setText(code == 0
                        ? note + QStringLiteral(" ✓")
                        : QStringLiteral("La operación no se completó "
                                         "(código %1).").arg(code));
                    reload();
                    proc->deleteLater();
                });
        proc->start(helper, argv);
        m_status->setText(note + QStringLiteral("…"));
    }

    void createPoint()
    {
        if (m_demo) {
            m_status->setText(QStringLiteral(
                "(modo demostración: no se crean puntos reales)"));
            return;
        }
        bool ok = false;
        const QString label = QInputDialog::getText(
            this, QStringLiteral("Crear punto de restauración"),
            QStringLiteral("Descripción (opcional):"), QLineEdit::Normal,
            QString(), &ok);
        if (!ok)
            return;
        QStringList extra{QStringLiteral("create"),
                          QStringLiteral("--reason"),
                          QStringLiteral("manual")};
        if (!label.isEmpty())
            extra << QStringLiteral("--label") << label;
        runPrivileged(extra, QStringLiteral("Creando punto de restauración"));
    }

    void restoreSelected()
    {
        auto *it = m_tree->currentItem();
        if (!it)
            return;
        const QString id = it->data(0, Qt::UserRole).toString();
        const QString when = it->text(0);
        if (m_demo) {
            m_status->setText(QStringLiteral(
                "(modo demostración: no se restaura nada)"));
            return;
        }
        const auto ans = QMessageBox::warning(
            this, QStringLiteral("Restaurar el sistema"),
            QStringLiteral("Se devolverá el sistema al estado del punto del "
                           "<b>%1</b>. Antes se creará automáticamente un "
                           "punto con el estado actual, por si quieres "
                           "deshacerlo.<br><br>¿Continuar?").arg(when),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes)
            return;
        runPrivileged({QStringLiteral("restore"), id,
                       QStringLiteral("--confirm")},
                      QStringLiteral("Restaurando el sistema"));
    }

    QColor m_accent;
    QString m_repo, m_store;
    bool m_demo;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_headStat = nullptr, *m_status = nullptr;
    QPushButton *m_restore = nullptr;
    QVector<Point> m_points;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-recuperacion"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("store"),
                   QStringLiteral("Restore Points store dir"),
                   QStringLiteral("dir"),
                   QStringLiteral("/var/lib/castalia/restore")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Show sample restore points")});
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

    // A bare screenshot against the default store still needs a populated list
    // to be useful, so fall back to demo there; but an explicit --store renders
    // live (so a real store can be captured/verified), and --demo always demos.
    const QString store = cli.value(QStringLiteral("store"));
    const bool defaultStore =
        store == QStringLiteral("/var/lib/castalia/restore");
    const bool shot = !cli.value(QStringLiteral("screenshot")).isEmpty();
    const bool demo = cli.isSet(QStringLiteral("demo"))
                      || (shot && defaultStore);
    Recovery w(accent, repo, store, demo);
    w.show();

    const QString out = cli.value(QStringLiteral("screenshot"));
    if (!out.isEmpty())
        QTimer::singleShot(250, &app, [&]() {
            w.grab().save(out);
            app.quit();
        });
    return app.exec();
}
