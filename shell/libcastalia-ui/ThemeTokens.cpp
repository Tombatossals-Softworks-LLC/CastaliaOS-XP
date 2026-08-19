#include "ThemeTokens.h"

#include <QFile>
#include <QTextStream>

ThemeTokens ThemeTokens::load(const QString &confPath)
{
    ThemeTokens t;
    QFile f(confPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return t;

    QTextStream in(&f);
    QString section;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            section = line.mid(1, line.size() - 2).trimmed();
            continue;
        }
        const int eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        QString key = line.left(eq).trimmed();
        QString value = line.mid(eq + 1).trimmed();
        // strip a trailing comment that is outside quotes
        if (!value.startsWith('"')) {
            const int hash = value.indexOf('#');
            if (hash >= 0)
                value = value.left(hash).trimmed();
        } else {
            const int closing = value.indexOf('"', 1);
            if (closing > 0)
                value = value.mid(1, closing - 1);
        }
        if (key.startsWith('"') && key.endsWith('"'))
            key = key.mid(1, key.size() - 2);
        t.m_values.insert(section + QLatin1Char('/') + key, value);
    }

    // minimum viability: a name and one color prove we read the right file
    t.m_valid = t.m_values.contains(QStringLiteral("meta/id"))
             && t.m_values.contains(QStringLiteral("colors/accent"));
    return t;
}

QString ThemeTokens::str(const QString &section, const QString &key,
                         const QString &fallback) const
{
    return m_values.value(section + QLatin1Char('/') + key, fallback);
}

int ThemeTokens::num(const QString &section, const QString &key,
                     int fallback) const
{
    bool ok = false;
    const int v = str(section, key).toInt(&ok);
    return ok ? v : fallback;
}

QColor ThemeTokens::color(const QString &key) const
{
    return QColor(str(QStringLiteral("colors"), key, QStringLiteral("#FF00FF")));
}
