// castalia-ejecutar — the Castalia Run dialog (Bible §7.7).
//
// "Run (default hotkey, e.g. the launch-key + R): type a command, a path, a
// URL, or an app name; history; runs native or routes .exe to Wine (§11)."
//
// The whole point of a Run box is that one field accepts four different kinds
// of thing, so the interesting part is the *routing*, and it is deliberately
// a pure function (`planFor`) with no side effects: it turns typed text into
// a Plan — what kind of thing this is, which command would run it, and a
// plain-Spanish explanation. The dialog shows that explanation live under the
// field as you type, so you always know what Enter is about to do, and
// `--print-plan` exposes exactly the same function head-lessly.
//
// Nothing is remembered but the history you typed, in your own config, and
// "Borrar historial" empties it (P7: local-only, clearable).
//
// Usage: castalia-ejecutar [--theme id] [--repo PATH] [--command TEXT]
//                          [--print-plan TEXT] [--screenshot out.png]

#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QKeyEvent>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "Theme.h"

namespace {

// What the typed text turned out to be.
enum class Kind { Empty, Url, Directory, File, Windows, Command, Unknown };

struct Plan {
    Kind kind = Kind::Empty;
    QStringList argv;      // what would actually be executed
    QString explain;       // one line of plain Spanish for the user
    bool runnable = false;
};

QString which(const QString &bin)
{
    return QStandardPaths::findExecutable(bin);
}

// Split a command line on spaces, honouring single and double quotes so
// `castalia-terminal --run 'echo hola'` survives intact. Deliberately simple:
// this is a Run box, not a shell — no globbing, no substitution, no pipes.
QStringList splitCommand(const QString &text)
{
    QStringList out;
    QString current;
    QChar quote;
    bool inWord = false;
    for (const QChar ch : text) {
        if (!quote.isNull()) {
            if (ch == quote)
                quote = QChar();
            else
                current.append(ch);
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
            inWord = true;
            continue;
        }
        if (ch.isSpace()) {
            if (inWord) {
                out.append(current);
                current.clear();
                inWord = false;
            }
            continue;
        }
        current.append(ch);
        inWord = true;
    }
    if (inWord)
        out.append(current);
    return out;
}

bool looksLikeUrl(const QString &text)
{
    static const QStringList schemes = {
        QStringLiteral("http://"),  QStringLiteral("https://"),
        QStringLiteral("ftp://"),   QStringLiteral("mailto:"),
        QStringLiteral("magnet:"),
    };
    for (const QString &s : schemes)
        if (text.startsWith(s, Qt::CaseInsensitive))
            return true;
    // "www.algo.com" without a scheme is what people actually type.
    return text.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)
           && !text.contains(QLatin1Char(' '));
}

bool isWindowsProgram(const QString &path)
{
    const QString lower = path.toLower();
    return lower.endsWith(QStringLiteral(".exe"))
           || lower.endsWith(QStringLiteral(".msi"))
           || lower.endsWith(QStringLiteral(".bat"))
           || lower.endsWith(QStringLiteral(".com"));
}

QString expandPath(const QString &text)
{
    QString path = text;
    if (path.startsWith(QLatin1Char('~')))
        path.replace(0, 1, QDir::homePath());
    return path;
}

// The routing. Pure: it inspects the filesystem and PATH but changes nothing,
// so the dialog can call it on every keystroke and `--print-plan` can expose
// it verbatim.
Plan planFor(const QString &raw)
{
    Plan plan;
    const QString text = raw.trimmed();
    if (text.isEmpty()) {
        // Not a repeat of the blurb above the field — the line under the
        // field always says what Enter would do, and with nothing typed the
        // useful thing to say is how to get here without the mouse (§7.7).
        plan.explain = QStringLiteral(
            "Consejo: esta ventana se abre en cualquier momento con "
            "⊞ + R.");
        return plan;
    }

    if (looksLikeUrl(text)) {
        plan.kind = Kind::Url;
        const QString url = text.startsWith(QStringLiteral("www."),
                                            Qt::CaseInsensitive)
            ? QStringLiteral("http://") + text : text;
        const QString opener = which(QStringLiteral("xdg-open"));
        if (opener.isEmpty()) {
            plan.explain = QStringLiteral(
                "Dirección de Internet, pero falta xdg-utils para abrirla.");
            return plan;
        }
        plan.argv = QStringList{QStringLiteral("xdg-open"), url};
        plan.explain = QCoreApplication::translate("Ejecutar", "Se abrirá la dirección %1.").arg(url);
        plan.runnable = true;
        return plan;
    }

    // A path — absolute, relative to home, or relative to the current dir.
    const QString candidate = expandPath(text);
    const QFileInfo info(candidate);
    if (info.exists()) {
        if (info.isDir()) {
            plan.kind = Kind::Directory;
            // Through castalia-open, so Explorer inherits the session's asset
            // tree and active theme. Launching it bare would hand it the
            // compiled-in defaults (--repo . --theme classic) and open an
            // unthemed window that cannot find the packaged icons.
            plan.argv = QStringList{
                QStringLiteral("castalia-explorer"),
                QStringLiteral("--path"),
                QDir::cleanPath(info.absoluteFilePath())};
            if (!which(QStringLiteral("castalia-open")).isEmpty())
                plan.argv.prepend(QStringLiteral("castalia-open"));
            plan.explain = QStringLiteral(
                "Se abrirá la carpeta %1 en Castalia Explorer.")
                .arg(QDir::cleanPath(info.absoluteFilePath()));
            plan.runnable = !which(QStringLiteral("castalia-explorer"))
                                 .isEmpty();
            if (!plan.runnable)
                plan.explain = QStringLiteral(
                    "Es una carpeta, pero Castalia Explorer no está "
                    "instalado.");
            return plan;
        }
        if (isWindowsProgram(info.absoluteFilePath())) {
            plan.kind = Kind::Windows;
            if (which(QStringLiteral("wine")).isEmpty()) {
                plan.explain = QStringLiteral(
                    "Es un programa de Windows®; instala la capa de "
                    "compatibilidad (Wine) para ejecutarlo.");
                return plan;
            }
            plan.argv = QStringList{QStringLiteral("wine"),
                                    info.absoluteFilePath()};
            plan.explain = QStringLiteral(
                "Programa de Windows®: se ejecutará con la capa de "
                "compatibilidad.");
            plan.runnable = true;
            return plan;
        }
        if (info.isExecutable()) {
            plan.kind = Kind::Command;
            plan.argv = QStringList{info.absoluteFilePath()};
            plan.explain = QCoreApplication::translate("Ejecutar", "Se ejecutará %1.")
                               .arg(info.absoluteFilePath());
            plan.runnable = true;
            return plan;
        }
        plan.kind = Kind::File;
        if (which(QStringLiteral("xdg-open")).isEmpty()) {
            plan.explain = QStringLiteral(
                "Es un documento, pero falta xdg-utils para abrirlo.");
            return plan;
        }
        plan.argv = QStringList{QStringLiteral("xdg-open"),
                                info.absoluteFilePath()};
        plan.explain = QStringLiteral(
            "Se abrirá %1 con el programa predeterminado.")
            .arg(info.fileName());
        plan.runnable = true;
        return plan;
    }

    // Otherwise: a command line. Only the program name is resolved; the rest
    // are passed through as arguments.
    const QStringList parts = splitCommand(text);
    if (parts.isEmpty()) {
        plan.explain = QCoreApplication::translate("Ejecutar", "No hay nada que ejecutar.");
        return plan;
    }
    const QString program = parts.first();
    if (isWindowsProgram(program)) {
        plan.kind = Kind::Windows;
        if (which(QStringLiteral("wine")).isEmpty()) {
            plan.explain = QStringLiteral(
                "Es un programa de Windows®, pero no se encontró «%1» ni la "
                "capa de compatibilidad.").arg(program);
            return plan;
        }
        plan.argv = QStringList{QStringLiteral("wine")} + parts;
        plan.explain = QStringLiteral(
            "Se intentará ejecutar %1 con la capa de compatibilidad.")
            .arg(program);
        plan.runnable = true;
        return plan;
    }
    const QString resolved = which(program);
    if (resolved.isEmpty()) {
        plan.kind = Kind::Unknown;
        plan.explain = QStringLiteral(
            "No se encontró «%1». Comprueba el nombre o escribe una ruta "
            "completa.").arg(program);
        return plan;
    }
    plan.kind = Kind::Command;
    plan.argv = parts;
    plan.explain = parts.size() > 1
        ? QCoreApplication::translate("Ejecutar", "Se ejecutará %1 con %2 argumento(s).")
              .arg(resolved).arg(parts.size() - 1)
        : QCoreApplication::translate("Ejecutar", "Se ejecutará %1.").arg(resolved);
    plan.runnable = true;
    return plan;
}

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::Empty:     return QStringLiteral("vacio");
    case Kind::Url:       return QStringLiteral("url");
    case Kind::Directory: return QStringLiteral("carpeta");
    case Kind::File:      return QStringLiteral("documento");
    case Kind::Windows:   return QStringLiteral("windows");
    case Kind::Command:   return QStringLiteral("comando");
    case Kind::Unknown:   return QStringLiteral("desconocido");
    }
    return QStringLiteral("desconocido");
}

// --- head-less self-test (Bible §17.4) -----------------------------------
// The routing is the whole app, and it is pure, so it can be checked without
// a display — the same gate the games' --selftest gives their model.
int selftest()
{
    int failures = 0;
    auto check = [&failures](bool ok, const char *what) {
        if (!ok) {
            QTextStream(stderr) << "ejecutar-selftest: FAIL " << what << '\n';
            ++failures;
        }
    };

    // quoting
    const QStringList split = splitCommand(
        QStringLiteral("castalia-terminal --run 'echo hola mundo' -x"));
    check(split.size() == 4, "splitCommand keeps a quoted argument whole");
    check(split.value(2) == QStringLiteral("echo hola mundo"),
          "splitCommand strips the quotes but not the spaces");
    check(splitCommand(QStringLiteral("   ")).isEmpty(),
          "splitCommand of blanks is empty");

    // empty
    check(planFor(QString()).kind == Kind::Empty, "empty text is Empty");
    check(!planFor(QString()).runnable, "empty text is not runnable");

    // urls
    check(planFor(QStringLiteral("https://example.org")).kind == Kind::Url,
          "https is a URL");
    const Plan bare = planFor(QStringLiteral("www.example.org"));
    check(bare.kind == Kind::Url, "www. without a scheme is a URL");
    check(bare.argv.isEmpty()
              || bare.argv.last().startsWith(QStringLiteral("http://")),
          "a bare www. host gets a scheme before it is opened");
    check(planFor(QStringLiteral("www.example.org and more")).kind
              != Kind::Url,
          "a sentence starting with www. is not a URL");

    // real paths, in a temp dir so the test owns everything it touches
    QTemporaryDir dir;
    check(dir.isValid(), "temp dir for the path cases");
    if (dir.isValid()) {
        const QString folder = dir.path();
        const Plan dir = planFor(folder);
        check(dir.kind == Kind::Directory,
              "an existing directory routes to the file manager");
        // Whichever way it is launched, Explorer must be told where it is.
        check(dir.argv.contains(QStringLiteral("--path")),
              "the directory is passed to Explorer");
        check(!which(QStringLiteral("castalia-open")).isEmpty()
                  ? dir.argv.first() == QStringLiteral("castalia-open")
                  : dir.argv.first() == QStringLiteral("castalia-explorer"),
              "Explorer is launched through castalia-open when it exists, "
              "so it inherits the session repo and theme");

        const QString doc = folder + QStringLiteral("/nota.txt");
        QFile f(doc);
        check(f.open(QIODevice::WriteOnly), "write the sample document");
        f.write("hola");
        f.close();
        check(planFor(doc).kind == Kind::File,
              "an existing non-executable file is a document");

        const QString exe = folder + QStringLiteral("/juego.exe");
        QFile w(exe);
        w.open(QIODevice::WriteOnly);
        w.write("MZ");
        w.close();
        check(planFor(exe).kind == Kind::Windows,
              "an .exe routes to the compatibility layer");
    }

    // commands
    const Plan sh = planFor(QStringLiteral("/bin/sh"));
    check(sh.kind == Kind::Command && sh.runnable,
          "an absolute executable is a runnable command");
    check(sh.argv == QStringList{QStringLiteral("/bin/sh")},
          "an absolute executable runs itself, with no wrapper");
    const Plan missing =
        planFor(QStringLiteral("castalia-no-existe-de-verdad"));
    check(missing.kind == Kind::Unknown, "an unknown name is Unknown");
    check(!missing.runnable, "an unknown name is never runnable");
    check(missing.explain.contains(QCoreApplication::translate("Ejecutar", "No se encontró")),
          "an unknown name says so in Spanish");

    // the invariant that matters: nothing is ever runnable without a command
    for (const QString &probe : {QStringLiteral(""), QStringLiteral("  "),
                                 QStringLiteral("https://example.org"),
                                 QStringLiteral("/bin/sh"),
                                 QStringLiteral("castalia-no-existe")}) {
        const Plan p = planFor(probe);
        check(!p.runnable || !p.argv.isEmpty(),
              "a runnable plan always carries a command");
        check(!p.explain.isEmpty(), "every plan explains itself");
    }

    QTextStream(stdout) << (failures == 0
        ? QStringLiteral("ejecutar-selftest: OK\n")
        : QStringLiteral("ejecutar-selftest: %1 failure(s)\n").arg(failures));
    return failures == 0 ? 0 : 1;
}

QString historyPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
           + QStringLiteral("/castalia/ejecutar.conf");
}

const int kHistoryMax = 20;

} // namespace

class Ejecutar : public QWidget {
    Q_OBJECT
public:
    Ejecutar(const QString &repo, const ThemeTokens &tokens,
             const QString &preset)
        : m_repo(repo), m_tokens(tokens)
    {
        setWindowTitle(tr("Ejecutar — Castalia"));
        resize(520, 252);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("EjHeader"));
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "#EjHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *icon = new QLabel(head);
        icon->setPixmap(castalia::themeIcon(m_repo, QStringLiteral("terminal"))
                            .pixmap(28, 28));
        hl->addWidget(icon);
        auto *title = new QLabel(head);
        title->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>Ejecutar"
            "</span>")
            .arg(colorTok("titlebar_text")));
        hl->addWidget(title);
        hl->addStretch(1);
        root->addWidget(head);

        auto *body = new QVBoxLayout;
        body->setContentsMargins(16, 14, 16, 10);
        body->setSpacing(8);

        auto *blurb = new QLabel(QStringLiteral(
            "Escribe el nombre de un programa, una carpeta, un documento o "
            "una dirección de Internet y Castalia lo abrirá."), this);
        blurb->setWordWrap(true);
        blurb->setProperty("secondary", true);
        body->addWidget(blurb);

        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        row->addWidget(new QLabel(tr("Abrir:"), this));
        m_input = new QComboBox(this);
        m_input->setEditable(true);
        m_input->setInsertPolicy(QComboBox::NoInsert);
        m_input->lineEdit()->setPlaceholderText(
            tr("castalia-notas, ~/Documentos, www.ejemplo.org…"));
        m_input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_input->addItems(loadHistory());
        m_input->setCurrentText(preset);
        connect(m_input, &QComboBox::currentTextChanged, this,
                &Ejecutar::refreshPlan);
        connect(m_input->lineEdit(), &QLineEdit::returnPressed, this,
                &Ejecutar::execute);
        row->addWidget(m_input, 1);
        body->addLayout(row);

        m_explain = new QLabel(this);
        m_explain->setWordWrap(true);
        m_explain->setProperty("secondary", true);
        m_explain->setMinimumHeight(38);
        m_explain->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        body->addWidget(m_explain);
        body->addStretch(1);
        root->addLayout(body, 1);

        auto *bar = new QHBoxLayout;
        bar->setContentsMargins(16, 0, 16, 14);
        bar->setSpacing(8);
        m_clear = new QPushButton(tr("Borrar historial"), this);
        m_clear->setCursor(Qt::PointingHandCursor);
        connect(m_clear, &QPushButton::clicked, this, &Ejecutar::clearHistory);
        bar->addWidget(m_clear);
        bar->addStretch(1);
        auto *browse = new QPushButton(tr("Examinar…"), this);
        browse->setCursor(Qt::PointingHandCursor);
        connect(browse, &QPushButton::clicked, this, &Ejecutar::browse);
        bar->addWidget(browse);
        auto *cancel = new QPushButton(tr("Cancelar"), this);
        cancel->setCursor(Qt::PointingHandCursor);
        connect(cancel, &QPushButton::clicked, this, &QWidget::close);
        bar->addWidget(cancel);
        m_ok = new QPushButton(tr("Aceptar"), this);
        m_ok->setCursor(Qt::PointingHandCursor);
        m_ok->setDefault(true);
        connect(m_ok, &QPushButton::clicked, this, &Ejecutar::execute);
        bar->addWidget(m_ok);
        root->addLayout(bar);

        refreshPlan(m_input->currentText());
        m_input->setFocus();
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private slots:
    void refreshPlan(const QString &text)
    {
        m_plan = planFor(text);
        m_explain->setText(m_plan.explain);
        m_ok->setEnabled(m_plan.runnable);
        m_clear->setEnabled(m_input->count() > 0);
    }

    void execute()
    {
        if (!m_plan.runnable)
            return;
        if (!QProcess::startDetached(m_plan.argv.first(),
                                     m_plan.argv.mid(1))) {
            m_explain->setText(QStringLiteral(
                "No se pudo iniciar %1.").arg(m_plan.argv.first()));
            return;
        }
        rememberHistory(m_input->currentText().trimmed());
        close();
    }

    void browse()
    {
        const QString picked = QFileDialog::getOpenFileName(
            this, tr("Elegir un programa o documento"),
            QDir::homePath());
        if (!picked.isEmpty())
            m_input->setCurrentText(picked);
    }

    void clearHistory()
    {
        QSettings settings(historyPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("history"));
        settings.sync();
        const QString current = m_input->currentText();
        m_input->clear();
        m_input->setCurrentText(current);
        m_clear->setEnabled(false);
    }

private:
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }

    QStringList loadHistory() const
    {
        QSettings settings(historyPath(), QSettings::IniFormat);
        return settings.value(QStringLiteral("history/entries"))
                   .toStringList();
    }

    // Most recent first, de-duplicated, capped. Local file, user-owned,
    // clearable from the dialog (P7).
    void rememberHistory(const QString &entry)
    {
        if (entry.isEmpty())
            return;
        QStringList entries = loadHistory();
        entries.removeAll(entry);
        entries.prepend(entry);
        while (entries.size() > kHistoryMax)
            entries.removeLast();
        QDir().mkpath(QFileInfo(historyPath()).absolutePath());
        QSettings settings(historyPath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("history/entries"), entries);
        settings.sync();
    }

    QString m_repo;
    ThemeTokens m_tokens;
    Plan m_plan;
    QComboBox *m_input = nullptr;
    QLabel *m_explain = nullptr;
    QPushButton *m_ok = nullptr, *m_clear = nullptr;
};

namespace {

void addOptions(QCommandLineParser &cli)
{
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("command"),
                   QStringLiteral("Prefill the field with TEXT"),
                   QStringLiteral("text")});
    cli.addOption({QStringLiteral("selftest"),
                   QStringLiteral("Run the head-less routing self-test")});
    cli.addOption({QStringLiteral("print-plan"),
                   QStringLiteral("Print how TEXT would be routed, and exit"),
                   QStringLiteral("text")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
}

} // namespace

int main(int argc, char **argv)
{
    // --print-plan and --selftest are pure QtCore, so they work with no
    // display (a QApplication would abort on a tty or a bare CI runner).
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        const bool plan = a == "--print-plan" || a.startsWith("--print-plan=");
        if (!plan && a != "--selftest")
            continue;
        QCoreApplication app(argc, argv);
        QCommandLineParser cli;
        addOptions(cli);
        cli.process(app);
        if (cli.isSet(QStringLiteral("selftest")))
            return selftest();
        const Plan p = planFor(cli.value(QStringLiteral("print-plan")));
        QTextStream(stdout)
            << kindName(p.kind) << '|'
            << (p.runnable ? QStringLiteral("si") : QStringLiteral("no"))
            << '|' << p.argv.join(QLatin1Char(' ')) << '|'
            << p.explain << '\n';
        return p.runnable ? 0 : 1;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-ejecutar"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    addOptions(cli);
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const ThemeTokens tokens = castalia::applyTheme(&app, repo, themeId);

    Ejecutar w(repo, tokens, cli.value(QStringLiteral("command")));
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
