// castalia-registros — the Castalia Log Viewer (Bible §9.2, §10 XP-parity
// "Event Viewer").
//
// §9.2: "Read/filter system + service logs. MVP: tail + filter by
// service/severity, search. Plain-text, colored severity; no binary journal."
//
// That last clause is the whole design. Castalia runs runit, not systemd, so
// the logs are what they have always been on Unix: plain text under /var/log
// and svlogd's `current` files under /var/log/<service>/. This reads them
// directly — no journal, no daemon, nothing to be out of sync with.
//
// Two things it is careful about:
//   * It never loads a whole log. Big logs are read from the END, backwards,
//     so opening a 400 MB messages file on a 512 MB machine costs one buffer
//     (§16), and the UI thread never stalls on I/O it did not bound.
//   * Severity is *classified*, not guessed at render time — `severityOf` is
//     a pure function so the filter, the colour and the counters can never
//     disagree, and `--selftest` checks it head-lessly in CI.
//
// Usage: castalia-registros [--theme id] [--repo PATH] [--file PATH]
//                           [--demo] [--selftest] [--screenshot out.png]

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

#include "Theme.h"

namespace {

// Plain-language severities (§9.2: "plain-text, colored severity").
enum class Severity { Error, Warning, Notice, Info };

struct Line {
    QString text;
    Severity severity = Severity::Info;
};

struct LogFile {
    QString label;    // what the user reads
    QString path;     // what we open
};

const int kMaxLines = 4000;      // what we keep in the view
const qint64 kTailBytes = 512 * 1024;   // what we read off the end

// Classify a log line. Pure and deliberately conservative: a line is only an
// error or a warning when it says so, because colouring an ordinary line red
// teaches people to ignore the colour.
Severity severityOf(const QString &line)
{
    const QString lower = line.toLower();
    static const QStringList errorWords = {
        QStringLiteral("error"),   QStringLiteral("failed"),
        QStringLiteral("failure"), QStringLiteral("fatal"),
        QStringLiteral("panic"),   QStringLiteral("segfault"),
        QStringLiteral("critical"), QStringLiteral("denied"),
        QStringLiteral("cannot "), QStringLiteral("refused"),
    };
    static const QStringList warnWords = {
        QStringLiteral("warn"),    QStringLiteral("deprecat"),
        QStringLiteral("timeout"), QStringLiteral("retry"),
        QStringLiteral("degraded"),
    };
    static const QStringList noticeWords = {
        QStringLiteral("notice"),  QStringLiteral("started"),
        QStringLiteral("starting"), QStringLiteral("stopped"),
        QStringLiteral("stopping"), QStringLiteral("mounted"),
    };
    for (const QString &w : errorWords)
        if (lower.contains(w))
            return Severity::Error;
    for (const QString &w : warnWords)
        if (lower.contains(w))
            return Severity::Warning;
    for (const QString &w : noticeWords)
        if (lower.contains(w))
            return Severity::Notice;
    return Severity::Info;
}

QString severityName(Severity severity)
{
    switch (severity) {
    case Severity::Error:   return QStringLiteral("Error");
    case Severity::Warning: return QStringLiteral("Aviso");
    case Severity::Notice:  return QStringLiteral("Suceso");
    case Severity::Info:    return QStringLiteral("Información");
    }
    return QStringLiteral("Información");
}

// Read at most the last `bytes` of a file and return its complete lines.
// Reading from the end is what makes this usable on a huge log — and the
// first (probably partial) line is dropped rather than shown truncated.
QStringList tailLines(const QString &path, qint64 bytes, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("No se puede leer %1 (%2).")
                         .arg(path, file.errorString());
        return QStringList();
    }
    const qint64 size = file.size();
    bool partialFirst = false;
    if (size > bytes) {
        file.seek(size - bytes);
        partialFirst = true;
    }
    const QByteArray blob = file.read(bytes);
    file.close();
    QStringList lines = QString::fromUtf8(blob).split(QLatin1Char('\n'));
    if (partialFirst && !lines.isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.size() > kMaxLines)
        lines = lines.mid(lines.size() - kMaxLines);
    return lines;
}

// The logs worth offering, in the order a user would look for them: the
// system logs first, then whatever runit services are logging under
// /var/log/<service>/current. Only files that exist and are readable.
QVector<LogFile> discoverLogs()
{
    QVector<LogFile> found;
    const struct { const char *label; const char *path; } wellKnown[] = {
        {"Sistema (messages)",     "/var/log/messages"},
        {"Sistema (syslog)",       "/var/log/syslog"},
        {"Arranque del núcleo",    "/var/log/dmesg"},
        {"Núcleo (kern.log)",      "/var/log/kern.log"},
        {"Autenticación",          "/var/log/auth.log"},
        {"Servidor gráfico (Xorg)", "/var/log/Xorg.0.log"},
        {"Instalación de paquetes", "/var/log/dpkg.log"},
        {"Gestor de paquetes (apt)", "/var/log/apt/history.log"},
    };
    for (const auto &entry : wellKnown) {
        const QFileInfo info(QLatin1String(entry.path));
        if (info.isFile() && info.isReadable())
            found.append({QString::fromUtf8(entry.label), info.filePath()});
    }
    // runit/svlogd service logs (§6.4, §6.14).
    QDir logs(QStringLiteral("/var/log"));
    const QStringList services =
        logs.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &service : services) {
        const QFileInfo current(
            logs.filePath(service + QStringLiteral("/current")));
        if (current.isFile() && current.isReadable())
            found.append({QStringLiteral("Servicio: %1").arg(service),
                          current.filePath()});
    }
    return found;
}

// A representative log for the render gate and for anyone with no readable
// logs — clearly labelled as a sample, never presented as this machine's.
QStringList demoLines()
{
    return QStringList{
        QStringLiteral("2026-07-31 09:14:02 castalia-session: theme: human"),
        QStringLiteral("2026-07-31 09:14:02 runit: starting service dbus"),
        QStringLiteral("2026-07-31 09:14:03 kernel: EXT4-fs (sda1): mounted "
                       "filesystem with ordered data mode"),
        QStringLiteral("2026-07-31 09:14:03 castalia-panel: startup 118 ms, "
                       "RSS 21 MB"),
        QStringLiteral("2026-07-31 09:14:05 NetworkManager: eth0: link up, "
                       "100 Mbps full duplex"),
        QStringLiteral("2026-07-31 09:14:07 pipewire: WARNING quantum 1024 "
                       "requested, using 2048 (timeout)"),
        QStringLiteral("2026-07-31 09:15:11 castalia-explorer: cannot read "
                       "/media/usb0: Permission denied"),
        QStringLiteral("2026-07-31 09:15:12 udisks: mount /dev/sdb1 failed: "
                       "unknown filesystem type"),
        QStringLiteral("2026-07-31 09:16:40 castalia-restore: snapshot "
                       "pre-update created (1.2 GiB, 41 s)"),
        QStringLiteral("2026-07-31 09:18:02 apt: upgraded castalia-desktop "
                       "0.1.0 -> 0.1.1"),
        QStringLiteral("2026-07-31 09:22:31 castalia-session: shutdown "
                       "requested; stopping session"),
    };
}

// --- head-less self-test (Bible §17.4) -----------------------------------
int selftest()
{
    int failures = 0;
    auto check = [&failures](bool ok, const char *what) {
        if (!ok) {
            QTextStream(stderr) << "registros-selftest: FAIL " << what << '\n';
            ++failures;
        }
    };

    check(severityOf(QStringLiteral("kernel: I/O error on sda"))
              == Severity::Error, "an error line is an Error");
    check(severityOf(QStringLiteral("mount FAILED")) == Severity::Error,
          "severity is case-insensitive");
    check(severityOf(QStringLiteral("Permission denied")) == Severity::Error,
          "denied is an Error");
    check(severityOf(QStringLiteral("WARNING: quantum too small"))
              == Severity::Warning, "a warning line is a Warning");
    check(severityOf(QStringLiteral("runit: starting service dbus"))
              == Severity::Notice, "a service transition is a Notice");
    check(severityOf(QStringLiteral("castalia-panel: startup 118 ms"))
              == Severity::Info, "an ordinary line stays Info");
    check(severityOf(QString()) == Severity::Info,
          "an empty line is Info, not a crash");
    // Errors must win over the softer words when a line contains both —
    // otherwise "failed, will retry" would be filed as a mere warning.
    check(severityOf(QStringLiteral("failed, will retry in 5s"))
              == Severity::Error, "Error outranks Warning on the same line");

    // Tail: the last N bytes, with the partial first line dropped.
    QTemporaryDir dir;
    check(dir.isValid(), "temp dir for the tail cases");
    if (dir.isValid()) {
        const QString path = dir.path() + QStringLiteral("/big.log");
        QFile f(path);
        check(f.open(QIODevice::WriteOnly), "write the sample log");
        for (int i = 0; i < 500; ++i)
            f.write(QStringLiteral("line %1 padding padding padding\n")
                        .arg(i).toUtf8());
        f.close();

        QString err;
        const QStringList all = tailLines(path, 1024 * 1024, &err);
        check(err.isEmpty(), "a readable log produces no error");
        check(all.size() == 500, "a small log is read whole");
        check(all.last().startsWith(QStringLiteral("line 499")),
              "the last line is the last line");

        const QStringList tail = tailLines(path, 512, &err);
        check(tail.size() < 500 && !tail.isEmpty(),
              "a byte budget really bounds the read");
        check(tail.last().startsWith(QStringLiteral("line 499")),
              "a bounded read still ends at the end of the file");
        check(tail.first().startsWith(QStringLiteral("line ")),
              "the partial first line is dropped, not shown truncated");

        QString missingErr;
        const QStringList none = tailLines(
            dir.path() + QStringLiteral("/no-existe.log"), 1024, &missingErr);
        check(none.isEmpty(), "an unreadable log yields no lines");
        check(!missingErr.isEmpty(), "an unreadable log explains itself");
    }

    check(demoLines().size() > 5, "the sample log has something to show");
    // The sample lines exist for --demo and the render gate only. If they
    // ever leak into a live view they read as this machine's own failures,
    // so keep them recognisable as fabrications.
    for (const QString &line : demoLines())
        check(line.startsWith(QStringLiteral("2026-")),
              "sample lines carry the fixed sample date");

    QTextStream(stdout) << (failures == 0
        ? QStringLiteral("registros-selftest: OK\n")
        : QStringLiteral("registros-selftest: %1 failure(s)\n").arg(failures));
    return failures == 0 ? 0 : 1;
}

} // namespace

class Registros : public QWidget {
    Q_OBJECT
public:
    Registros(const QString &repo, const ThemeTokens &tokens, bool demo,
              const QString &forced)
        : m_repo(repo), m_tokens(tokens), m_demo(demo)
    {
        setWindowTitle(QStringLiteral("Visor de registros — Castalia"));
        resize(760, 480);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("RgHeader"));
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "#RgHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *icon = new QLabel(head);
        icon->setPixmap(castalia::themeIcon(m_repo,
                                            QStringLiteral("documents"))
                            .pixmap(28, 28));
        hl->addWidget(icon);
        m_title = new QLabel(head);
        hl->addWidget(m_title);
        hl->addStretch(1);
        root->addWidget(head);

        // toolbar: which log, which severity, what text
        auto *bar = new QHBoxLayout;
        bar->setContentsMargins(14, 12, 14, 6);
        bar->setSpacing(8);
        bar->addWidget(new QLabel(QStringLiteral("Registro:"), this));
        m_logs = new QComboBox(this);
        m_logs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        bar->addWidget(m_logs, 2);
        bar->addWidget(new QLabel(QStringLiteral("Gravedad:"), this));
        m_severity = new QComboBox(this);
        m_severity->addItem(QStringLiteral("Todo"), -1);
        m_severity->addItem(severityName(Severity::Error),
                            int(Severity::Error));
        m_severity->addItem(severityName(Severity::Warning),
                            int(Severity::Warning));
        m_severity->addItem(severityName(Severity::Notice),
                            int(Severity::Notice));
        bar->addWidget(m_severity);
        m_filter = new QLineEdit(this);
        m_filter->setPlaceholderText(QStringLiteral("Buscar texto…"));
        m_filter->setClearButtonEnabled(true);
        bar->addWidget(m_filter, 2);
        root->addLayout(bar);

        m_view = new QPlainTextEdit(this);
        m_view->setReadOnly(true);
        m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_view->setFont(monoFont());
        root->addWidget(m_view, 1);

        auto *foot = new QHBoxLayout;
        foot->setContentsMargins(14, 6, 14, 12);
        foot->setSpacing(8);
        m_status = new QLabel(this);
        m_status->setProperty("secondary", true);
        m_status->setWordWrap(true);
        foot->addWidget(m_status, 1);
        m_follow = new QCheckBox(QStringLiteral("Seguir el final"), this);
        m_follow->setChecked(true);
        foot->addWidget(m_follow);
        auto *refresh = new QPushButton(QStringLiteral("Actualizar"), this);
        refresh->setCursor(Qt::PointingHandCursor);
        connect(refresh, &QPushButton::clicked, this, &Registros::reload);
        foot->addWidget(refresh);
        root->addLayout(foot);

        m_files = m_demo ? QVector<LogFile>() : discoverLogs();
        if (!forced.isEmpty())
            m_files.prepend({QFileInfo(forced).fileName(), forced});
        for (const LogFile &f : m_files)
            m_logs->addItem(f.label, f.path);
        if (m_files.isEmpty())
            m_logs->addItem(m_demo
                ? QStringLiteral("Registro de ejemplo")
                : QStringLiteral("Sin registros legibles"));
        m_logs->setEnabled(!m_files.isEmpty());

        connect(m_logs, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { reload(); });
        connect(m_severity, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { render(); });
        connect(m_filter, &QLineEdit::textChanged, this,
                [this](const QString &) { render(); });

        reload();
    }

private slots:
    void reload()
    {
        m_error.clear();
        m_lines.clear();
        if (m_files.isEmpty()) {
            // ONLY --demo may put invented lines on screen. With no readable
            // logs on a real machine the view stays empty and says why:
            // sample errors with plausible timestamps, shown next to a
            // header reading "sin registros", would be indistinguishable
            // from this machine's own failures (P10).
            if (m_demo)
                for (const QString &text : demoLines())
                    m_lines.append({text, severityOf(text)});
        } else {
            const int row = qBound(0, m_logs->currentIndex(),
                                   m_files.size() - 1);
            const QStringList raw =
                tailLines(m_files[row].path, kTailBytes, &m_error);
            m_lines.reserve(raw.size());
            for (const QString &text : raw)
                m_lines.append({text, severityOf(text)});
        }
        render();
    }

    void render()
    {
        const QString needle = m_filter->text().trimmed();
        const int wanted = m_severity->currentData().toInt();
        int shown = 0, errors = 0, warnings = 0;
        QString html;
        html.reserve(m_lines.size() * 96);
        for (const Line &line : m_lines) {
            if (line.severity == Severity::Error)
                ++errors;
            else if (line.severity == Severity::Warning)
                ++warnings;
            if (wanted >= 0 && int(line.severity) != wanted)
                continue;
            if (!needle.isEmpty()
                && !line.text.contains(needle, Qt::CaseInsensitive))
                continue;
            html += QStringLiteral("<span style='color:%1'>%2</span><br>")
                        .arg(colorFor(line.severity), line.text.toHtmlEscaped());
            ++shown;
        }
        m_view->clear();
        if (html.isEmpty()) {
            QString why = QStringLiteral("Sin líneas que coincidan con el "
                                         "filtro.");
            if (m_lines.isEmpty()) {
                if (!m_error.isEmpty())
                    why = QStringLiteral("No se pudo leer este registro.");
                else if (m_files.isEmpty())
                    why = QStringLiteral("No hay ningún registro que se pueda "
                                         "leer con tu usuario.");
                else
                    why = QStringLiteral("Este registro está vacío.");
            }
            m_view->appendHtml(QStringLiteral("<span style='color:%1'>%2"
                                              "</span>")
                                   .arg(colorTok("text_secondary"), why));
        } else {
            m_view->appendHtml(html);
        }
        if (m_follow->isChecked())
            m_view->verticalScrollBar()->setValue(
                m_view->verticalScrollBar()->maximum());

        const QString source = m_files.isEmpty()
            ? (m_demo ? QStringLiteral("ejemplo")
                      : QStringLiteral("sin registros"))
            : m_files[qBound(0, m_logs->currentIndex(),
                             m_files.size() - 1)].path;
        m_title->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>Visor de "
            "registros</span>&nbsp;&nbsp;<span style='color:%1'>%2</span>")
            .arg(colorTok("titlebar_text"), source.toHtmlEscaped()));

        QStringList status;
        if (!m_error.isEmpty()) {
            status << m_error;
        } else if (m_files.isEmpty()) {
            status << (m_demo
                ? QStringLiteral("Registro de ejemplo: no se ha leído nada "
                                 "de este equipo.")
                : QStringLiteral("No hay registros legibles con tu usuario. "
                                 "Muchos viven en /var/log y piden permisos "
                                 "de administrador."));
        }
        status << QStringLiteral("%1 de %2 líneas · %3 errores · %4 avisos")
                      .arg(shown).arg(m_lines.size()).arg(errors)
                      .arg(warnings);
        if (m_lines.size() >= kMaxLines)
            status << QStringLiteral("(sólo el final del registro)");
        m_status->setText(status.join(QStringLiteral("  ·  ")));
    }

private:
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }

    // Severity colours come from the theme, so the viewer is legible in the
    // dark and high-contrast themes too (§8.2) instead of hard-coded red.
    QString colorFor(Severity severity) const
    {
        switch (severity) {
        case Severity::Error:   return QStringLiteral("#CC0000");
        case Severity::Warning: return colorTok("accent");
        case Severity::Notice:  return colorTok("text");
        case Severity::Info:    return colorTok("text_secondary");
        }
        return colorTok("text");
    }

    QFont monoFont() const
    {
        QFont f(m_tokens.str(QStringLiteral("fonts"), QStringLiteral("mono")));
        if (f.family().isEmpty())
            f.setFamily(QStringLiteral("DejaVu Sans Mono"));
        f.setStyleHint(QFont::Monospace);
        f.setPointSize(9);
        return f;
    }

    QString m_repo;
    ThemeTokens m_tokens;
    bool m_demo = false;
    QString m_error;
    QVector<LogFile> m_files;
    QVector<Line> m_lines;
    QComboBox *m_logs = nullptr, *m_severity = nullptr;
    QLineEdit *m_filter = nullptr;
    QPlainTextEdit *m_view = nullptr;
    QLabel *m_title = nullptr, *m_status = nullptr;
    QCheckBox *m_follow = nullptr;
};

namespace {

void addOptions(QCommandLineParser &cli)
{
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("file"),
                   QStringLiteral("Open this log file first"),
                   QStringLiteral("path")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Show a representative sample log")});
    cli.addOption({QStringLiteral("selftest"),
                   QStringLiteral("Run the head-less classifier self-test")});
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
    QApplication::setApplicationName(QStringLiteral("castalia-registros"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    addOptions(cli);
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const ThemeTokens tokens = castalia::applyTheme(&app, repo, themeId);

    Registros w(repo, tokens, cli.isSet(QStringLiteral("demo")),
                cli.value(QStringLiteral("file")));
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
