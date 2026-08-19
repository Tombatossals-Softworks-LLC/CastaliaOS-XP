// castalia-servicios — the Castalia Services Manager (Bible §9.2, §6.4,
// §10 XP-parity "services.msc").
//
// §9.2: "Start/stop/enable runit services (plain language). MVP: list
// services w/ friendly names + status, start/stop. Plain-language
// names/descriptions, not raw unit files."
//
// The plain language is not invented here — `services/README.md` already
// defines the contract: every service ships a `service.conf` next to its
// runit `run` script, with a display name, a description, a category and an
// `essential` flag. This app is the reader that contract was written for.
// A service without one still appears, honestly labelled by its directory
// name and marked "sin descripción" — never hidden.
//
// Everything it knows comes from two places, both plain text:
//   * /etc/sv/<name>/          the service definitions (+ service.conf)
//   * the runsvdir directory   a symlink here means "enabled at boot"
// and one command, `sv status`, for whether it is actually running.
//
// The two parsers (`parseServiceConf`, `parseSvStatus`) are pure functions so
// `--selftest` can gate them head-lessly in CI, where there is no runit.
//
// Usage: castalia-servicios [--theme id] [--repo PATH] [--demo] [--selftest]
//                           [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVector>

#include <unistd.h>

#include "Theme.h"

namespace {

struct Meta {
    QString name, description, category;
    bool essential = false;
};

struct Status {
    bool running = false;
    bool known = false;      // false when `sv status` said nothing usable
    qint64 seconds = 0;
    QString raw;
};

struct Service {
    QString id;              // the directory name under /etc/sv
    Meta meta;
    Status status;
    bool enabled = false;    // symlinked into the runsvdir
};

QString which(const QString &bin)
{
    return QStandardPaths::findExecutable(bin);
}

// Parse a service.conf (`services/README.md`). Deliberately tolerant: an
// unknown key, a missing section header or a stray blank line must not lose
// the keys we do understand — a service is not going to be hidden from the
// user because someone wrote a comment oddly.
Meta parseServiceConf(const QString &text)
{
    Meta meta;
    for (const QString &rawLine : text.split(QLatin1Char('\n'))) {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char('[')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed().toLower();
        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
            && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);
        if (key == QStringLiteral("name"))
            meta.name = value;
        else if (key == QStringLiteral("description"))
            meta.description = value;
        else if (key == QStringLiteral("category"))
            meta.category = value.toLower();
        else if (key == QStringLiteral("essential"))
            meta.essential = value.compare(QStringLiteral("true"),
                                           Qt::CaseInsensitive) == 0
                             || value == QStringLiteral("1")
                             || value.compare(QStringLiteral("yes"),
                                              Qt::CaseInsensitive) == 0;
    }
    return meta;
}

// Parse one line of `sv status`:
//   run: dbus: (pid 1234) 4523s
//   down: cups: 12s, normally up
//   fail: foo: unable to change to service directory: file does not exist
Status parseSvStatus(const QString &line)
{
    Status status;
    status.raw = line.trimmed();
    if (status.raw.isEmpty())
        return status;
    const QString head = status.raw.section(QLatin1Char(':'), 0, 0).trimmed();
    if (head == QStringLiteral("run")) {
        status.running = true;
        status.known = true;
    } else if (head == QStringLiteral("down")) {
        status.running = false;
        status.known = true;
    } else {
        return status;   // fail/warning/anything else: we do not pretend
    }
    // The uptime is the last bare "<digits>s" token on the line. Punctuation
    // is stripped first: a stopped service reads "down: cups: 12s, normally
    // up", and the comma is part of the sentence, not of the number.
    for (const QString &token : status.raw.split(QLatin1Char(' '))) {
        QString t = token.trimmed();
        while (!t.isEmpty()
               && (t.endsWith(QLatin1Char(',')) || t.endsWith(QLatin1Char('.'))
                   || t.endsWith(QLatin1Char(';'))))
            t.chop(1);
        if (t.size() < 2 || !t.endsWith(QLatin1Char('s')))
            continue;
        bool ok = false;
        const qint64 value = t.left(t.size() - 1).toLongLong(&ok);
        if (ok)
            status.seconds = value;
    }
    return status;
}

QString humanUptime(qint64 seconds)
{
    if (seconds < 60)
        return QStringLiteral("%1 s").arg(seconds);
    if (seconds < 3600)
        return QStringLiteral("%1 min").arg(seconds / 60);
    if (seconds < 86400)
        return QStringLiteral("%1 h %2 min")
            .arg(seconds / 3600).arg((seconds % 3600) / 60);
    return QStringLiteral("%1 días").arg(seconds / 86400);
}

QString categoryName(const QString &category)
{
    if (category == QStringLiteral("network"))
        return QStringLiteral("Red");
    if (category == QStringLiteral("hardware"))
        return QStringLiteral("Hardware");
    if (category == QStringLiteral("optional"))
        return QStringLiteral("Opcional");
    if (category == QStringLiteral("system"))
        return QStringLiteral("Sistema");
    return QStringLiteral("Otros");
}

// Where runit keeps the "enabled" symlinks, in the order runit itself looks.
QString runsvdir()
{
    for (const char *candidate : {"/etc/service", "/var/service", "/service",
                                  "/etc/runit/runsvdir/current"}) {
        const QFileInfo info{QLatin1String(candidate)};
        if (info.isDir())
            return info.absoluteFilePath();
    }
    return QString();
}

const char *kServiceRoot = "/etc/sv";

QVector<Service> demoServices()
{
    QVector<Service> out;
    const struct {
        const char *id; const char *name; const char *desc;
        const char *cat; bool essential; bool enabled; bool running;
        qint64 up;
    } rows[] = {
        {"lightdm", "LightDM", "Pantalla de acceso (greeter)",
         "system", true, true, true, 5233},
        {"dbus", "Bus de mensajes", "Comunicación entre programas del sistema",
         "system", true, true, true, 5240},
        {"eudev", "Detección de dispositivos",
         "Reconoce el hardware conectado", "hardware", true, true, true, 5241},
        {"connman", "Conexiones de red",
         "Gestiona la red cableada y la Wi-Fi", "network", false, true, true,
         5202},
        {"cups", "Impresión", "Cola de impresión y detección de impresoras",
         "optional", false, true, false, 0},
        {"sshd", "Acceso remoto (SSH)",
         "Permite conectarse a este equipo desde otro", "network", false,
         false, false, 0},
    };
    for (const auto &r : rows) {
        Service s;
        s.id = QString::fromUtf8(r.id);
        s.meta.name = QString::fromUtf8(r.name);
        s.meta.description = QString::fromUtf8(r.desc);
        s.meta.category = QString::fromUtf8(r.cat);
        s.meta.essential = r.essential;
        s.enabled = r.enabled;
        s.status.known = true;
        s.status.running = r.running;
        s.status.seconds = r.up;
        out.append(s);
    }
    return out;
}

// --- head-less self-test (Bible §17.4) -----------------------------------
int selftest()
{
    int failures = 0;
    auto check = [&failures](bool ok, const char *what) {
        if (!ok) {
            QTextStream(stderr) << "servicios-selftest: FAIL " << what << '\n';
            ++failures;
        }
    };

    // The real file that ships in services/lightdm/.
    const Meta lightdm = parseServiceConf(QStringLiteral(
        "[service]\nname = LightDM\n"
        "description = Pantalla de acceso (greeter)\n"
        "category = system\nessential = true\n"));
    check(lightdm.name == QStringLiteral("LightDM"), "name is read");
    check(lightdm.description.startsWith(QStringLiteral("Pantalla")),
          "description is read");
    check(lightdm.category == QStringLiteral("system"), "category is read");
    check(lightdm.essential, "essential = true is read");

    const Meta quoted = parseServiceConf(QStringLiteral(
        "# comentario\nname = \"Bus de mensajes\"\nessential = no\n"
        "clave-desconocida = da igual\n"));
    check(quoted.name == QStringLiteral("Bus de mensajes"),
          "quotes are stripped");
    check(!quoted.essential, "essential = no is false");
    check(parseServiceConf(QString()).name.isEmpty(),
          "an empty conf yields an empty meta, not a crash");

    // sv status
    const Status run = parseSvStatus(
        QStringLiteral("run: dbus: (pid 1234) 4523s"));
    check(run.known && run.running, "a run: line is running");
    check(run.seconds == 4523, "the uptime is read");
    const Status down = parseSvStatus(
        QStringLiteral("down: cups: 12s, normally up"));
    check(down.known && !down.running, "a down: line is stopped");
    check(down.seconds == 12, "a stopped service reports how long");
    const Status broken = parseSvStatus(QStringLiteral(
        "fail: foo: unable to change to service directory"));
    check(!broken.known, "a fail: line is not claimed as known");
    check(!broken.running, "a fail: line is never reported as running");
    check(!parseSvStatus(QString()).known, "an empty line is unknown");
    // A pid must never be mistaken for an uptime.
    check(parseSvStatus(QStringLiteral("run: x: (pid 99) 7s")).seconds == 7,
          "the uptime wins over the pid");

    check(humanUptime(45) == QStringLiteral("45 s"), "seconds stay seconds");
    check(humanUptime(3600).startsWith(QStringLiteral("1 h")),
          "an hour reads as an hour");
    check(categoryName(QStringLiteral("network")) == QStringLiteral("Red"),
          "categories are Spanish");
    check(categoryName(QStringLiteral("inventada"))
              == QStringLiteral("Otros"),
          "an unknown category falls back rather than disappearing");

    check(demoServices().size() >= 5, "the sample has something to show");
    for (const Service &s : demoServices()) {
        check(!s.id.isEmpty(), "every sample service has an id");
        check(!s.meta.name.isEmpty(), "every sample service has a name");
    }

    QTextStream(stdout) << (failures == 0
        ? QStringLiteral("servicios-selftest: OK\n")
        : QStringLiteral("servicios-selftest: %1 failure(s)\n").arg(failures));
    return failures == 0 ? 0 : 1;
}

} // namespace

class Servicios : public QWidget {
    Q_OBJECT
public:
    Servicios(const QString &repo, const ThemeTokens &tokens, bool demo)
        : m_repo(repo), m_tokens(tokens), m_demo(demo),
          m_sv(which(QStringLiteral("sv"))), m_runsvdir(runsvdir())
    {
        setWindowTitle(QStringLiteral("Servicios del sistema — Castalia"));
        resize(780, 470);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("SvHeader"));
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "#SvHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *icon = new QLabel(head);
        icon->setPixmap(castalia::themeIcon(m_repo,
                                            QStringLiteral("services"))
                            .pixmap(28, 28));
        hl->addWidget(icon);
        m_title = new QLabel(head);
        hl->addWidget(m_title);
        hl->addStretch(1);
        root->addWidget(head);

        m_tree = new QTreeWidget(this);
        m_tree->setRootIsDecorated(false);
        m_tree->setAlternatingRowColors(true);
        m_tree->setColumnCount(4);
        m_tree->setHeaderLabels({QStringLiteral("Servicio"),
                                 QStringLiteral("Estado"),
                                 QStringLiteral("Al arrancar"),
                                 QStringLiteral("Categoría")});
        // Four narrow columns, so the table still reads at 800x600 (§7.11).
        // The plain-language description (§9.2) is the wide thing, so it goes
        // in the detail line under the table where it has room to be a
        // sentence instead of a clipped fragment.
        for (int c = 0; c < 3; ++c)
            m_tree->header()->setSectionResizeMode(
                c, QHeaderView::ResizeToContents);
        m_tree->header()->setStretchLastSection(true);
        m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
        connect(m_tree, &QTreeWidget::currentItemChanged, this,
                [this](QTreeWidgetItem *, QTreeWidgetItem *) {
                    updateButtons();
                });
        auto *wrap = new QVBoxLayout;
        wrap->setContentsMargins(14, 12, 14, 6);
        wrap->addWidget(m_tree, 1);
        m_detail = new QLabel(this);
        m_detail->setWordWrap(true);
        m_detail->setMinimumHeight(34);
        m_detail->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
        wrap->addWidget(m_detail);
        root->addLayout(wrap, 1);

        auto *bar = new QHBoxLayout;
        bar->setContentsMargins(14, 0, 14, 12);
        bar->setSpacing(8);
        m_note = new QLabel(this);
        m_note->setProperty("secondary", true);
        m_note->setWordWrap(true);
        bar->addWidget(m_note, 1);
        m_start = new QPushButton(QStringLiteral("Iniciar"), this);
        m_stop = new QPushButton(QStringLiteral("Detener"), this);
        m_restart = new QPushButton(QStringLiteral("Reiniciar"), this);
        auto *refresh = new QPushButton(QStringLiteral("Actualizar"), this);
        for (QPushButton *b : {m_start, m_stop, m_restart, refresh}) {
            b->setCursor(Qt::PointingHandCursor);
            bar->addWidget(b);
        }
        connect(m_start, &QPushButton::clicked, this,
                [this]() { act(QStringLiteral("up")); });
        connect(m_stop, &QPushButton::clicked, this,
                [this]() { act(QStringLiteral("down")); });
        connect(m_restart, &QPushButton::clicked, this,
                [this]() { act(QStringLiteral("restart")); });
        connect(refresh, &QPushButton::clicked, this, &Servicios::reload);
        root->addLayout(bar);

        reload();
    }

private slots:
    void reload()
    {
        m_services = m_demo ? demoServices() : readServices();
        m_tree->clear();
        for (const Service &s : m_services) {
            auto *item = new QTreeWidgetItem(m_tree);
            item->setText(0, s.meta.name.isEmpty() ? s.id : s.meta.name);
            item->setToolTip(0, QStringLiteral("%1/%2")
                                    .arg(QLatin1String(kServiceRoot), s.id));
            item->setText(1, stateText(s));
            item->setText(2, s.enabled ? QStringLiteral("Sí")
                                       : QStringLiteral("No"));
            item->setText(3, categoryName(s.meta.category));
            if (s.meta.essential) {
                QFont f = item->font(0);
                f.setBold(true);
                item->setFont(0, f);
            }
        }
        if (m_tree->topLevelItemCount() > 0)
            m_tree->setCurrentItem(m_tree->topLevelItem(0));

        const QString mode = m_demo
            ? QStringLiteral("vista de ejemplo")
            : (m_sv.isEmpty() ? QStringLiteral("sin runit")
                              : QStringLiteral("runit"));
        m_title->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>Servicios "
            "del sistema</span>&nbsp;&nbsp;<span style='color:%1'>%2 · %3 "
            "servicios</span>")
            .arg(colorTok("titlebar_text"), mode)
            .arg(m_services.size()));
        updateButtons();
    }

private:
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }

    static QString stateText(const Service &s)
    {
        if (!s.status.known)
            return QStringLiteral("desconocido");
        return s.status.running
            ? QStringLiteral("activo · %1").arg(humanUptime(s.status.seconds))
            : QStringLiteral("detenido");
    }

    const Service *current() const
    {
        const int row = m_tree->indexOfTopLevelItem(m_tree->currentItem());
        if (row < 0 || row >= m_services.size())
            return nullptr;
        return &m_services[row];
    }

    // The buttons tell the truth about what this session can do: without
    // runit there is nothing to drive, and in the sample view nothing to
    // change.
    void updateButtons()
    {
        const Service *s = current();
        if (s) {
            const QString description = s->meta.description.isEmpty()
                ? QStringLiteral("<i>sin descripción en service.conf</i>")
                : s->meta.description.toHtmlEscaped();
            m_detail->setText(QStringLiteral(
                "<b>%1</b> — %2<br><span style='color:%3'>%4/%1%5</span>")
                .arg(s->id.toHtmlEscaped(), description,
                     colorTok("text_secondary"),
                     QLatin1String(kServiceRoot),
                     s->meta.essential
                         ? QStringLiteral("  ·  servicio esencial")
                         : QString()));
        } else {
            m_detail->clear();
        }
        const bool live = !m_demo && !m_sv.isEmpty() && s;
        m_start->setEnabled(live && s->status.known && !s->status.running);
        m_stop->setEnabled(live && s->status.known && s->status.running);
        m_restart->setEnabled(live && s->status.known && s->status.running);

        if (m_demo) {
            m_note->setText(QStringLiteral(
                "Vista de ejemplo: no se han consultado los servicios reales "
                "de este equipo."));
        } else if (m_sv.isEmpty()) {
            m_note->setText(QStringLiteral(
                "No se detecta runit (sv). Los servicios aparecerán aquí "
                "cuando el sistema los supervise."));
        } else if (m_services.isEmpty()) {
            m_note->setText(QStringLiteral(
                "No hay servicios definidos en %1.")
                .arg(QLatin1String(kServiceRoot)));
        } else if (s && s->meta.essential) {
            m_note->setText(QStringLiteral(
                "%1 es un servicio esencial: detenerlo puede dejar el "
                "escritorio sin funcionar.").arg(
                    s->meta.name.isEmpty() ? s->id : s->meta.name));
        } else {
            m_note->setText(QStringLiteral(
                "Los cambios se aplican ahora; «Al arrancar» se configura al "
                "instalar el servicio."));
        }
    }

    // Read /etc/sv/* — the definitions — and decorate each with its metadata,
    // whether it is symlinked into the runsvdir, and what `sv status` says.
    QVector<Service> readServices() const
    {
        QVector<Service> out;
        QDir root{QLatin1String(kServiceRoot)};
        if (!root.exists())
            return out;
        const QStringList names =
            root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &id : names) {
            Service s;
            s.id = id;
            QFile conf(root.filePath(id + QStringLiteral("/service.conf")));
            if (conf.open(QIODevice::ReadOnly | QIODevice::Text))
                s.meta = parseServiceConf(
                    QString::fromUtf8(conf.readAll()));
            if (s.meta.name.isEmpty())
                s.meta.name = id;
            s.enabled = !m_runsvdir.isEmpty()
                        && QFileInfo::exists(m_runsvdir + QLatin1Char('/')
                                             + id);
            if (!m_sv.isEmpty()) {
                QProcess p;
                p.start(m_sv, {QStringLiteral("status"), id});
                if (p.waitForFinished(1500))
                    s.status = parseSvStatus(
                        QString::fromUtf8(p.readAllStandardOutput()));
            }
            out.append(s);
        }
        return out;
    }

    // Start / stop / restart. Supervising services is root's job, so this
    // hands off to a graphical privilege prompt exactly like the Software and
    // Update centres do — never a silent privileged action.
    void act(const QString &verb)
    {
        const Service *s = current();
        if (!s || m_demo || m_sv.isEmpty())
            return;
        if (s->meta.essential && verb != QStringLiteral("up")) {
            const auto answer = QMessageBox::question(
                this, QStringLiteral("Servicio esencial"),
                QStringLiteral("%1 es esencial para el escritorio.\n\n"
                               "¿Seguro que quieres %2?")
                    .arg(s->meta.name,
                         verb == QStringLiteral("down")
                             ? QStringLiteral("detenerlo")
                             : QStringLiteral("reiniciarlo")),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
        }
        QString helper = which(QStringLiteral("pkexec"));
        QStringList argv{m_sv, verb, s->id};
        if (helper.isEmpty()) {
            if (::geteuid() != 0) {
                m_note->setText(QStringLiteral(
                    "Hace falta pkexec (o ejecutar como administrador) para "
                    "cambiar un servicio."));
                return;
            }
            helper = m_sv;
            argv = QStringList{verb, s->id};
        }
        QProcess::startDetached(helper, argv);
        m_note->setText(QStringLiteral(
            "Solicitado: %1 %2. Pulsa «Actualizar» en unos segundos.")
            .arg(verb, s->id));
    }

    QString m_repo;
    ThemeTokens m_tokens;
    bool m_demo = false;
    QString m_sv, m_runsvdir;
    QVector<Service> m_services;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_title = nullptr, *m_note = nullptr, *m_detail = nullptr;
    QPushButton *m_start = nullptr, *m_stop = nullptr, *m_restart = nullptr;
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
                   QStringLiteral("Show a representative sample service set")});
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
    QApplication::setApplicationName(QStringLiteral("castalia-servicios"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    addOptions(cli);
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const ThemeTokens tokens = castalia::applyTheme(&app, repo, themeId);

    Servicios w(repo, tokens, cli.isSet(QStringLiteral("demo")));
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
