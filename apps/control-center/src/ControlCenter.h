// Castalia Control Center — the single settings hub (Bible §9.1, §10).
// A category list on the left, a stacked panel on the right. The Appearance
// panel previews any of the bundled themes live and persists the choice to
// ~/.config/castalia/theme.conf — the exact `id = "..."` line castalia-session
// reads (§6.6/§6.16), so the pick reaches the whole desktop. About shows the
// original identity + Microsoft non-affiliation.
#pragma once

#include <QMainWindow>
#include <QVector>

#include "ThemeTokens.h"

class QLabel;
class QListWidget;
class QStackedWidget;

class ControlCenter : public QMainWindow {
    Q_OBJECT
public:
    ControlCenter(const QString &repoRoot, const QString &themeId,
                  QWidget *parent = nullptr);

signals:
    void themeChanged(const QString &themeId);

private:
    // Pages are built the first time they are shown, not up front. Nine
    // panels' worth of widgets — including the Appearance page, which paints
    // a live preview swatch for every bundled theme — used to be constructed
    // in the ctor, so opening the hub to change one setting paid for all of
    // them. See showPage(); the §16.2 memory budget is what caught it.
    void showPage(int index);

    QWidget *buildAppearance();
    QWidget *buildDisplay();
    QWidget *buildScreensaver();
    QWidget *buildRecovery();
    QWidget *buildLanguage();
    QWidget *buildSound();
    QWidget *buildNetwork();
    QWidget *buildPower();
    QWidget *buildAbout();

    void applyTheme(const QString &themeId);
    bool persistTheme(const QString &themeId);
    // Persist the chosen desktop wallpaper (repo-relative or absolute path, or
    // empty to follow the theme) to ~/.config/castalia/desktop.conf, which the
    // desktop watches and reloads live.
    bool persistWallpaper(const QString &value);
    // Persist the interface language to ~/.config/castalia/locale.conf, the
    // file castalia::locale reads before any app builds a widget (§7.13).
    // "auto" means "follow the system"; anything else is an explicit choice.
    bool persistLanguage(const QString &code);

    QString m_repo;
    QString m_theme;
    ThemeTokens m_tokens;
    QListWidget *m_categories = nullptr;
    QStackedWidget *m_pages = nullptr;
    //: One entry per category, in the same order; null until first shown.
    QVector<QWidget *> m_built;
    QLabel *m_appearanceStatus = nullptr;
    QLabel *m_wallpaperStatus = nullptr;
    QLabel *m_languageStatus = nullptr;
};
