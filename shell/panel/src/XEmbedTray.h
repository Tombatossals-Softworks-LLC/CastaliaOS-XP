// Castalia shell — the XEmbed half of the system tray (Bible §7.4).
//
// TrayHost covers StatusNotifierItem, which is what modern applications speak.
// This covers everything older: the freedesktop System Tray Protocol, where an
// application asks the tray manager to *adopt its X window* and the panel
// reparents that window inside itself. GTK2-era icons, Wine, and Qt's own
// QSystemTrayIcon on a machine with no session bus all arrive this way.
//
// The two halves are independent on purpose: a bare X session with no D-Bus
// still gets a working tray through this one.
//
// The protocol, in the order it happens:
//   1. we own the X selection _NET_SYSTEM_TRAY_S<screen> on a window of ours;
//   2. we announce that with a MANAGER client message to the root window —
//      applications that started before us are watching for exactly this;
//   3. they send us _NET_SYSTEM_TRAY_OPCODE / SYSTEM_TRAY_REQUEST_DOCK
//      carrying their window id;
//   4. we reparent that window into a container of ours, tell it it has been
//      embedded (_XEMBED / XEMBED_EMBEDDED_NOTIFY) and map it.
#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

class XEmbedTray : public QWidget {
    Q_OBJECT
public:
    // `background` is the colour the panel shows behind the tray. It is not
    // decoration: an XEmbed client sets its own window background to
    // ParentRelative and inherits the *X* background of the container it is
    // embedded in — not whatever Qt paints there. Without a real background
    // pixel on the container, legacy icons come up as blank boxes.
    explicit XEmbedTray(const QColor &background, QWidget *parent = nullptr);
    ~XEmbedTray() override;

    // True when we actually own the tray selection. False means either no X
    // connection or another tray manager got there first — in which case we
    // host nothing rather than fighting over every icon.
    bool isManager() const;

    // How many icons are docked. Used by the panel's self-test.
    int itemCount() const;

    // The selection an X tray manager owns, for a screen number. Pure, and
    // pinned by the self-test: a wrong name here means every legacy tray icon
    // in the session silently goes nowhere.
    static QString selectionAtomName(int screen);

    // The width the row of icons needs. Pure, and pinned by the self-test:
    // the tray must collapse to nothing when empty so the clock does not sit
    // on a hole, and it must grow by exactly one icon plus one gap per item.
    static int widthFor(int items);

protected:
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    // Our containers live in the panel's top-level window, so their position
    // depends on every ancestor's geometry — and when the clock grows a digit
    // the whole tray frame slides left without us getting a move event of our
    // own. We watch the ancestors for that.
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onXcbEvent();

private:
    void dock(unsigned int client);
    void undock(unsigned int client);
    // Place every container in the panel's coordinates. Called whenever the
    // row changes or the tray itself moves — the clock changes width every
    // minute and drags us along with it.
    void relayout();
    // Repaint one container's X background from the panel's own painting, so
    // a ParentRelative icon sits on the tray well's gradient rather than on a
    // flat patch of it. Falls back to the flat colour if the widget cannot be
    // rendered (no window yet).
    void refreshBackground(int index);

    struct Priv;
    Priv *d;
};
