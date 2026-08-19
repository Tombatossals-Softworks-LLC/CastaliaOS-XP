// castalia-desktop — the desktop layer (Bible §7.5).
// Wallpaper (rendered from the Azure Bay source; the production build pre-bakes
// rasters per §8.4/§8.6), selectable icons with shadowed labels, and a working
// right-click menu. The fixed system icons (Equipo, Documentos, Lugares de red,
// Papelera) each double-click to a real location, the user's ~/Escritorio
// contents appear as icons that open with the right app, and the menu creates
// folders, refreshes, and opens the Control Center to personalise.
#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QWidget>

#include "Aurora.h"
#include "ThemeTokens.h"

class QSvgRenderer;
class QFileSystemWatcher;
class QTimer;

// One desktop icon: 48 px art + label with a soft shadow (§7.5). Selection
// wears the active theme's accent; hovering breathes a soft halo in
// (120 ms, instant under reduce-motion), and launching sends one accent ring
// out from the icon so a double-click is visibly acknowledged even when the
// app it starts takes a moment to map its window.
class DesktopIcon : public QWidget {
    Q_OBJECT
public:
    DesktopIcon(const QIcon &icon, const QString &label,
                const QColor &accent, QWidget *parent = nullptr);

    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    // Filesystem path this icon opens on double-click (empty = no target).
    void setOpenPath(const QString &path) { m_openPath = path; }
    QString openPath() const { return m_openPath; }
    // The launch acknowledgement (also used when a rubber band picks it up).
    void pulse();

signals:
    void activated();
    void clicked(DesktopIcon *self);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void animateHover(bool over);

    QIcon m_icon;
    QString m_label;
    QString m_openPath;
    QColor m_accent;
    qreal m_hover = 0.0;     // 0 = rest, 1 = hovered
    qreal m_pulse = 0.0;     // 0 = idle, 1 = ring at full radius
    bool m_selected = false;
};

// The aurora: the desktop's hidden flourish (↑ ↑ ↓ ↓ ← → ← → B A, or
// `--easter-egg aurora`). A frameless child that covers the desktop, runs
// castalia::paintAurora at ~20 fps for a few seconds and fades itself out.
// Any click or key dismisses it early; when the run ends it deletes itself,
// so nothing survives the show (§16: no timer left behind).
class AuroraOverlay : public QWidget {
    Q_OBJECT
public:
    AuroraOverlay(const QColor &accent, int seconds,
                  const QString &repoRoot, QWidget *parent);

    void dismiss();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QColor m_accent;
    QString m_repo;          // so the mark finds the artwork
    QTimer *m_frames = nullptr;
    qreal m_phase = 0.0;
    qreal m_opacity = 0.0;
    int m_elapsedMs = 0;
    int m_runMs = 0;
};

class DesktopWindow : public QWidget {
    Q_OBJECT
public:
    DesktopWindow(const ThemeTokens &tokens, const QString &repoRoot,
                  const QSize &size, QWidget *parent = nullptr);
    ~DesktopWindow() override;

    // Show the aurora now (the `--easter-egg aurora` entry point).
    void showAurora(int seconds = 9);

    // Head-less gate for the desktop plane's interactive parts — the rubber
    // band, the wallpaper cache and the key sequence — driven with synthetic
    // events under QT_QPA_PLATFORM=offscreen (Bible §17.4). Returns 0 when
    // every check passes. `castalia-desktop --selftest`.
    int selfTest();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    DesktopIcon *addIcon(const QString &iconName, const QString &label,
                         const QString &openPath = QString());
    // An icon that launches a program rather than opening a location. Used
    // for the live session's "Instalar Castalia OS" (§14.5) — the one thing
    // a live desktop has to offer that an installed one must not.
    DesktopIcon *addAppIcon(const QString &iconName, const QString &label,
                            const QString &bin);
    void clearSelection();
    void relayout();
    void loadUserIcons();
    void refresh();
    void newFolder();
    // The wallpaper to show, resolving the user's override first
    // (~/.config/castalia/desktop.conf), then the theme's [assets].wallpaper,
    // then the Azure Bay default. Absolute paths are used as-is; other values
    // are repo-relative.
    QString resolveWallpaper() const;
    // Re-read the override and repaint (wired to the config watcher, so a
    // pick in the Control Center changes the live desktop with no re-login).
    void reloadWallpaper();
    // Rasterise the current SVG into m_wall at the window size. The desktop
    // repaints on every icon hover and selection; re-running the SVG renderer
    // each time would cost the whole wallpaper per frame, so it is baked once
    // per (source, size) and blitted afterwards (§16 FLOOR budget).
    void bakeWallpaper();
    // Attach the decoder that matches m_wallPath (SVG renderer or raster).
    void loadWallpaperSource();
    QString desktopDir() const;
    QString launchTheme() const;
    void launchPath(const QString &path);
    void launchApp(const QString &bin, const QStringList &args = {});
    // Select every icon the rubber band touches (and only those).
    void applyBand(const QRect &band);

    ThemeTokens m_tokens;
    QString m_repo;
    QSvgRenderer *m_wallpaper = nullptr;
    QFileSystemWatcher *m_wallWatcher = nullptr;
    QString m_wallPath;          // source of the baked pixmap
    QString m_rasterPath;        // set when that source is a JPEG/PNG, not SVG
    QPixmap m_wall;              // baked wallpaper, window-sized
    QPixmap m_wallOutgoing;      // the previous one, during a crossfade
    qreal m_wallFade = 1.0;      // 1 = settled, <1 = crossfading in
    QList<DesktopIcon *> m_icons;
    int m_systemIcons = 0;   // count of fixed system icons (kept on refresh)
    QPoint m_bandOrigin;
    QRect m_band;                // empty when no rubber band is being dragged
    bool m_banding = false;
    castalia::KonamiDetector m_konami;
    AuroraOverlay *m_aurora = nullptr;
};
