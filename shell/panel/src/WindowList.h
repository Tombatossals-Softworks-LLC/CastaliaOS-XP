// WindowList — the taskbar's real window strip (Bible §7.2).
//
// This is not a mock: it reflects the ACTUAL managed windows on the display,
// read straight from the EWMH hints the window manager publishes on the root
// window (_NET_CLIENT_LIST, _NET_ACTIVE_WINDOW, _NET_WM_NAME) via libxcb.
// Clicking a button activates (and un-minimizes) that window through a
// standard _NET_ACTIVE_WINDOW client message.
//
// It is EVENT-DRIVEN: the panel subscribes to PropertyNotify on the root
// window (and on each managed client for title changes), so open/close/
// focus/rename reach the taskbar the instant the WM publishes them — no
// per-second polling; a slow 5 s timer remains only as a safety net. Updates
// are diffed, so buttons only appear/disappear when windows actually open
// or close.
//
// When there is no usable X connection — offscreen CI renders, or a display
// with no EWMH-aware WM — it falls back to a small representative set of demo
// buttons so the panel still reads as a finished taskbar in screenshots.
#pragma once

#include <QWidget>

class WindowList : public QWidget {
    Q_OBJECT
public:
    // demo=true forces the fallback buttons (used for deterministic renders).
    explicit WindowList(bool demo, QWidget *parent = nullptr);
    ~WindowList() override;

private:
    void refresh();
    void buildDemo();
    void activate(unsigned int window);
    void onXcbEvent();
    void scheduleRefresh();

    struct Priv;
    Priv *d;
};
