// Castalia shell — theme token loader.
// Parses the flat TOML subset used by themes/<id>/theme.conf (sections,
// string / integer / boolean values, # comments). The tokens are the single
// source of truth (Bible §6.16); no full TOML dependency is needed on the
// FLOOR tier for this format.
#pragma once

#include <QColor>
#include <QHash>
#include <QString>

class ThemeTokens {
public:
    // Loads themes/<id>/theme.conf. Returns a default-constructed (invalid)
    // instance if the file is missing or a required key is absent.
    static ThemeTokens load(const QString &confPath);

    bool isValid() const { return m_valid; }

    QString str(const QString &section, const QString &key,
                const QString &fallback = QString()) const;
    int     num(const QString &section, const QString &key,
                int fallback = 0) const;
    QColor  color(const QString &key) const;   // from [colors]

    // convenience accessors used by the shell
    int panelHeight() const   { return num("metrics", "panel_height", 30); }
    int titlebarHeight() const{ return num("metrics", "titlebar_height", 26); }
    int cornerRadius() const  { return num("metrics", "corner_radius", 2); }
    QString themeName() const { return str("meta", "name"); }
    QString themeId() const   { return str("meta", "id"); }
    // The accessibility theme opts out of decoration that trades contrast for
    // gloss (§7.11): highlights, sheens and tints check this first.
    bool highContrast() const
    {
        return str("meta", "high_contrast") == QLatin1String("true");
    }

private:
    QHash<QString, QString> m_values;   // "section/key" -> raw value
    bool m_valid = false;
};
