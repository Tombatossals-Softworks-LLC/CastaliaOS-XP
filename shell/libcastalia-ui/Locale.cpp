#include "Locale.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace castalia {
namespace locale {

namespace {

// The catalogues that ship. Adding a language means adding it here, adding
// i18n/castalia_<code>.ts, and translating it — tools/tests/test_i18n.py
// fails the build if any of those three is missing.
const struct {
    const char *code;
    const char *name;      // in that language, as a picker must show it
} kLanguages[] = {
    {"es", "Español"},
    {"en", "English"},
};

// "en_GB.UTF-8", "en_GB", "en.UTF-8" and "en" all mean "en".
QString languagePart(const QString &value)
{
    QString v = value.trimmed();
    const int dot = v.indexOf(QLatin1Char('.'));
    if (dot > 0)
        v = v.left(dot);
    const int at = v.indexOf(QLatin1Char('@'));
    if (at > 0)
        v = v.left(at);
    const int underscore = v.indexOf(QLatin1Char('_'));
    if (underscore > 0)
        v = v.left(underscore);
    return v.toLower();
}

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// The same flat-TOML shape the theme's config uses (§6.6):
//   [locale]
//   language = "en"
QString parseLocaleConf(const QString &text)
{
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char('[')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        if (line.left(eq).trimmed().toLower() != QLatin1String("language"))
            continue;
        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
            && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);
        return value.trimmed();
    }
    return QString();
}

} // namespace

QStringList available()
{
    QStringList out;
    for (const auto &l : kLanguages)
        out << QString::fromLatin1(l.code);
    return out;
}

QString displayName(const QString &code)
{
    for (const auto &l : kLanguages)
        if (code == QLatin1String(l.code))
            return QString::fromUtf8(l.name);
    return code;
}

QString resolve(const QString &configured, const QString &environment)
{
    const QString wanted = configured.trimmed().toLower();
    if (wanted.isEmpty())
        return QStringLiteral("es");           // Spanish-first, by default
    if (wanted == QLatin1String("auto")) {
        const QString fromEnv = languagePart(environment);
        return available().contains(fromEnv) ? fromEnv
                                             : QStringLiteral("es");
    }
    const QString code = languagePart(wanted);
    return available().contains(code) ? code : QStringLiteral("es");
}

QString configured()
{
    const QStringList candidates = {
        QDir::homePath() + QStringLiteral("/.config/castalia/locale.conf"),
        QStringLiteral("/etc/castalia/locale.conf")};
    for (const QString &path : candidates) {
        const QString value = parseLocaleConf(readFile(path));
        if (!value.isEmpty())
            return value;
    }
    return QString();
}

bool apply(QCoreApplication *app, const QString &repoRoot,
           const QString &code)
{
    if (!app || code.isEmpty() || code == QLatin1String("es"))
        return false;               // the source language needs no catalogue

    // Where a .qm can be: the repo/asset tree the app was pointed at, the
    // installed asset tree, and the build output (a developer running from
    // the tree without installing).
    const QStringList roots = {
        QDir(repoRoot).filePath(QStringLiteral("build/out/i18n")),
        QDir(repoRoot).filePath(QStringLiteral("i18n")),
        QStringLiteral("/usr/share/castalia/i18n"),
        QStringLiteral("/opt/castalia/share/castalia/i18n"),
    };
    for (const QString &root : roots) {
        const QString path =
            QDir(root).filePath(QStringLiteral("castalia_%1.qm").arg(code));
        if (!QFile::exists(path))
            continue;
        auto *translator = new QTranslator(app);
        if (!translator->load(path)) {
            delete translator;
            continue;
        }
        app->installTranslator(translator);
        // Qt's own strings too, so a standard dialog's buttons are not left
        // in English inside an otherwise translated interface.
        auto *qt = new QTranslator(app);
        if (qt->load(QStringLiteral("qt_%1").arg(code),
                     QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
            app->installTranslator(qt);
        else
            delete qt;
        return true;
    }
    return false;
}

// The property that records "this application already has its language", so
// the startup hook below and the later applyTheme() call do not install two
// translators for the same catalogue.
const char kApplied[] = "castaliaLocaleApplied";

QString applyConfigured(QCoreApplication *app, const QString &repoRoot)
{
    if (!app)
        return QStringLiteral("es");
    const QString already = app->property(kApplied).toString();
    if (!already.isEmpty())
        return already;

    // CASTALIA_LANG wins over the config file: it is how a screenshot, a
    // render gate or a bug report pins a language for one run.
    QString wanted = qEnvironmentVariable("CASTALIA_LANG");
    if (wanted.isEmpty())
        wanted = configured();
    QString environment = qEnvironmentVariable("LANGUAGE");
    if (environment.isEmpty())
        environment = qEnvironmentVariable("LC_ALL");
    if (environment.isEmpty())
        environment = qEnvironmentVariable("LC_MESSAGES");
    if (environment.isEmpty())
        environment = qEnvironmentVariable("LANG");
    if (environment.isEmpty())
        environment = QLocale::system().name();

    const QString code = resolve(wanted, environment);
    const bool installed = apply(app, repoRoot, code);
    // Spanish is settled the moment it is resolved — there is nothing to
    // load. Any other language is only settled once its catalogue is really
    // installed: if the first attempt guessed the wrong asset root, a later
    // call with the right one has to be allowed to try again.
    if (installed || code == QLatin1String("es"))
        app->setProperty(kApplied, code);
    return code;
}

namespace {

// The `--repo PATH` every Castalia binary accepts. Available here because
// QCoreApplication has already parsed argv by the time the startup hook
// below runs — the app's own QCommandLineParser has not run yet, and must
// not have to: a language installed after main() starts is a language that
// arrives too late for anything main() computes.
QString repoFromArguments()
{
    const QStringList args = QCoreApplication::arguments();
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--repo") && i + 1 < args.size())
            return args.at(i + 1);
        if (args.at(i).startsWith(QLatin1String("--repo=")))
            return args.at(i).mid(7);
    }
    return QString();
}

// Run by Qt immediately after the QCoreApplication constructor, before a
// single line of anybody's main() — which is the only placement that is
// actually safe. Installing the translator from main() means every binary
// has to remember to do it *first*, and the one that resolves its labels
// into a table on the line above ends up half translated with no error
// anywhere. Being early is not an optimisation here; it is the whole
// mechanism (§7.13).
void installLanguageAtStartup()
{
    auto *app = QCoreApplication::instance();
    if (!app)
        return;
    QString repo = qEnvironmentVariable("CASTALIA_REPO");
    if (repo.isEmpty())
        repo = repoFromArguments();
    if (repo.isEmpty())
        repo = QDir::currentPath();
    applyConfigured(app, repo);
}

} // namespace

// The macro pastes the name into a function definition, so it has to sit in
// the namespace that declares the hook.
Q_COREAPP_STARTUP_FUNCTION(installLanguageAtStartup)

} // namespace locale
} // namespace castalia
