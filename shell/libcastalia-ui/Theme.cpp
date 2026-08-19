#include "Theme.h"

#include "Locale.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QHash>
#include <QPalette>

#include <cmath>

namespace {

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// WCAG relative luminance of a colour (0 = black, 1 = white).
double relLuminance(const QColor &c)
{
    auto lin = [](int v) {
        const double s = v / 255.0;
        return s <= 0.03928 ? s / 12.92
                            : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.red()) + 0.7152 * lin(c.green())
           + 0.0722 * lin(c.blue());
}

// Dark themes need a matching QPalette so widgets that aren't explicitly
// styled by the QSS (item views, line edits, tooltips) follow the theme
// instead of defaulting to a light palette. Light themes keep Qt's default
// palette (which already matches them), so this only touches dark themes and
// never changes the look — or the screenshots — of the light ones.
void applyDarkPalette(QApplication *app, const ThemeTokens &t)
{
    const QColor surface = t.color(QStringLiteral("surface"));
    if (!surface.isValid() || relLuminance(surface) >= 0.5)
        return;
    const QColor surfaceAlt = t.color(QStringLiteral("surface_alt"));
    const QColor text = t.color(QStringLiteral("text"));
    const QColor text2 = t.color(QStringLiteral("text_secondary"));
    const QColor selBg = t.color(QStringLiteral("selection_bg"));
    const QColor selText = t.color(QStringLiteral("selection_text"));

    QPalette p = app->palette();
    p.setColor(QPalette::Window, surface);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, surface);
    p.setColor(QPalette::AlternateBase,
               surfaceAlt.isValid() ? surfaceAlt : surface.lighter(115));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, surfaceAlt.isValid() ? surfaceAlt : surface);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::ToolTipBase,
               surfaceAlt.isValid() ? surfaceAlt : surface);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Highlight, selBg);
    p.setColor(QPalette::HighlightedText, selText);
    if (text2.isValid()) {
        p.setColor(QPalette::PlaceholderText, text2);
        p.setColor(QPalette::Disabled, QPalette::Text, text2);
        p.setColor(QPalette::Disabled, QPalette::WindowText, text2);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, text2);
    }
    app->setPalette(p);
}

// Display order matches the design system (Bible §8.2); the Human flagship
// leads, unknown ids sort last.
int themeRank(const QString &id)
{
    static const QStringList order = {
        QStringLiteral("human"),    QStringLiteral("classic"),
        QStringLiteral("azul"),     QStringLiteral("oliva"),
        QStringLiteral("plata"),    QStringLiteral("medianoche"),
        QStringLiteral("high-contrast")};
    const int i = order.indexOf(id);
    return i < 0 ? 99 : i;
}

} // namespace

namespace castalia {

QString themeConfPath(const QString &repoRoot, const QString &themeId)
{
    return QDir(repoRoot).filePath(
        QStringLiteral("themes/%1/theme.conf").arg(themeId));
}

QString themeQssPath(const QString &repoRoot, const QString &themeId)
{
    return QDir(repoRoot).filePath(
        QStringLiteral("build/out/themes/%1/castalia.qss").arg(themeId));
}

ThemeTokens applyTheme(QApplication *app, const QString &repoRoot,
                       const QString &themeId, const QString &extraQss)
{
    const ThemeTokens tokens =
        ThemeTokens::load(themeConfPath(repoRoot, themeId));
    const QString qss = readFile(themeQssPath(repoRoot, themeId));
    if (app) {
        // The interface language, before any widget is built: a translator
        // installed after a label exists does not retranslate it. Every app
        // calls applyTheme(), so this is how they are all localised without
        // one line of per-app code (§7.13).
        locale::applyConfigured(app, repoRoot);
        applyDarkPalette(app, tokens);
        app->setStyleSheet(qss + extraQss);
    }
    return tokens;
}

QString activeThemeId(const QString &fallback)
{
    // Same lookup order as castalia-session's theme_from(): the user's config,
    // then the system default. Parse the `id = "..."` line from [meta].
    const QString home = QDir::homePath();
    const QStringList candidates = {
        home + QStringLiteral("/.config/castalia/theme.conf"),
        QStringLiteral("/etc/castalia/theme.conf")};
    for (const QString &path : candidates) {
        const QString text = readFile(path);
        if (text.isEmpty())
            continue;
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            const QString t = line.trimmed();
            if (!t.startsWith(QStringLiteral("id")))
                continue;
            const int q1 = t.indexOf(QLatin1Char('"'));
            const int q2 = t.indexOf(QLatin1Char('"'), q1 + 1);
            if (q1 >= 0 && q2 > q1)
                return t.mid(q1 + 1, q2 - q1 - 1);
        }
    }
    return fallback;
}

bool reduceMotion()
{
    if (qEnvironmentVariable("CASTALIA_REDUCE_MOTION")
            == QLatin1String("1"))
        return true;
    // Deterministic renders (CI screenshots) must not depend on timing.
    return qEnvironmentVariable("QT_QPA_PLATFORM")
        .contains(QLatin1String("offscreen"));
}

bool isLiveSession()
{
    if (qEnvironmentVariable("CASTALIA_LIVE") == QLatin1String("1"))
        return true;
    // live-boot mounts the medium here; an installed system has no such path.
    return QFileInfo::exists(QStringLiteral("/run/live/medium"));
}

QIcon themeIcon(const QString &repoRoot, const QString &name)
{
    // Memoised per path. The Start Menu alone asks for ~50 icons, several of
    // them the same file (`clock`, `search`, `package`… are reused by design),
    // and every QIcon built from an SVG carries its own renderer + pixmap
    // cache. Handing back the same QIcon is free — QIcon is implicitly shared
    // and the whole shell is single-threaded GUI code — and it keeps the menu
    // build off the §16 FLOOR budget.
    static QHash<QString, QIcon> cache;
    const QString path = QDir(repoRoot).filePath(
        QStringLiteral("themes/icons/48/%1.svg").arg(name));
    const auto hit = cache.constFind(path);
    if (hit != cache.constEnd())
        return *hit;
    QIcon icon;
    if (QFile::exists(path))
        icon = QIcon(path);
    cache.insert(path, icon);
    return icon;
}

QStringList availableThemes(const QString &repoRoot)
{
    QDir dir(QDir(repoRoot).filePath(QStringLiteral("themes")));
    QStringList ids;
    for (const QString &sub :
         dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QFile::exists(dir.filePath(sub + QStringLiteral("/theme.conf"))))
            ids << sub;
    }
    std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) {
        return themeRank(a) < themeRank(b);
    });
    return ids;
}

} // namespace castalia
