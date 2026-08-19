// Castalia shell — the taskbar panel (Phase 0 proof of concept).
// Layout per Bible §7.2: launch button → quick launch → window list
// (grows) → tray → clock. All colors arrive via the generated QSS plus the
// token-derived panel stylesheet; heights come from theme.conf metrics.
#pragma once

#include <QColor>
#include <QVector>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include "ThemeTokens.h"

class QTimer;
class Switcher;

class CastaliaMenu;
class TrayHost;
class XEmbedTray;
class WindowList;

// The tray clock: shows HH:mm and opens the Calendario on click (the XP-era
// ergonomic). Emits clicked(); the panel wires it to launch the app.
class ClockLabel : public QLabel {
    Q_OBJECT
public:
    using QLabel::QLabel;

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
};

class CastaliaPanel : public QWidget {
    Q_OBJECT
public:
    // demoTasks=true shows the fallback taskbar buttons instead of live
    // EWMH windows (deterministic offscreen/screenshot renders).
    explicit CastaliaPanel(const ThemeTokens &tokens, int width,
                           bool demoTasks = false, QWidget *parent = nullptr);

    CastaliaMenu *menu() const { return m_menu; }
    TrayHost *tray() const { return m_tray; }
    XEmbedTray *xembedTray() const { return m_xembed; }
    Switcher *switcher() const { return m_switcher; }

    // One network interface as the kernel reports it in /sys/class/net.
    struct NetLink {
        QString name;
        QString operstate;      // "up" / "down" / "unknown"
        bool wireless = false;
    };

    // Is this machine on a network? Pure, and pinned by the self-test: the
    // loopback is always up and must never count, or the tray would claim a
    // connection on a machine with the cable out (§9, the Network Center's
    // status indicator).
    static bool anyLinkUp(const QVector<NetLink> &links);

    // What the tray icon says when you hover it — the interface you are
    // actually connected through, or a plain statement that you are not.
    static QString networkTooltip(const QVector<NetLink> &links);

    // The flat colour a legacy tray icon ends up sitting on: the panel's
    // titlebar gradient half-way down (the icons are vertically centred),
    // under the tray well's rgba(0,0,0,52). Pure, and pinned by the
    // self-test — an XEmbed client paints ParentRelative, so this value *is*
    // the icon's background and a wrong one shows as a patch on the panel.
    static QColor trayWellColor(const QColor &top,
                                const QColor &bottom);

protected:
    // The QSS paints the panel's gradient; this lays the glass on top —
    // a specular band across the upper half, bright and dark hairlines at the
    // edges, and an accent bloom under the launch corner (§8.3).
    void paintEvent(QPaintEvent *event) override;

private slots:
    void toggleMenu();
    void updateClock();
    // Re-read /sys/class/net and set the tray light's tooltip and opacity.
    void updateNetwork();

private:
    ThemeTokens m_tokens;
    CastaliaMenu *m_menu = nullptr;
    ClockLabel *m_clock = nullptr;
    QTimer *m_clockTimer = nullptr;
    WindowList *m_tasks = nullptr;
    TrayHost *m_tray = nullptr;
    XEmbedTray *m_xembed = nullptr;
    Switcher *m_switcher = nullptr;
    QPushButton *m_net = nullptr;      // the network tray light
};

// The launch button paints the keep mark natively next to its text.
class LaunchButton : public QPushButton {
    Q_OBJECT
public:
    using QPushButton::QPushButton;

protected:
    void paintEvent(QPaintEvent *event) override;
};
