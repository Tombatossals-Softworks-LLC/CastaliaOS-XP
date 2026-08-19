// Castalia shell — the EWMH reads the panel makes of the window manager.
//
// The taskbar (§7.2) and the Alt+Tab switcher (§7.6) look at exactly the same
// things: which windows are managed, which one is focused, what each is
// called, which program owns it, and which of them belong in a window list at
// all. This is that vocabulary in one place, so the two never disagree about
// what counts as a window.
//
// Everything here is a plain read over an existing xcb connection — no state,
// no ownership. Callers keep their own connection and their own Atoms bundle
// (atom ids are per-display, interned once).
#pragma once

#include <QString>
#include <QVector>

#include <xcb/xcb.h>

namespace castalia {
namespace ewmh {

// Intern (create/lookup) an X atom by name, synchronously.
xcb_atom_t atom(xcb_connection_t *c, const char *name);

// Every atom the panel needs, interned in one pass at startup.
struct Atoms {
    xcb_atom_t clientList = 0;      // _NET_CLIENT_LIST
    xcb_atom_t activeWindow = 0;    // _NET_ACTIVE_WINDOW
    xcb_atom_t wmName = 0;          // _NET_WM_NAME
    xcb_atom_t utf8 = 0;            // UTF8_STRING
    xcb_atom_t winType = 0;         // _NET_WM_WINDOW_TYPE
    xcb_atom_t typeDock = 0;
    xcb_atom_t typeDesktop = 0;
    xcb_atom_t state = 0;           // _NET_WM_STATE
    xcb_atom_t skipTaskbar = 0;     // _NET_WM_STATE_SKIP_TASKBAR
    xcb_atom_t icon = 0;            // _NET_WM_ICON

    static Atoms intern(xcb_connection_t *c);
};

// Read a 32-bit array property (WINDOW/ATOM/CARDINAL) from a window.
QVector<uint32_t> cardinals(xcb_connection_t *c, xcb_window_t win,
                            xcb_atom_t prop, xcb_atom_t type,
                            uint32_t maxWords = 1024);

// The window's name: _NET_WM_NAME (UTF-8) first, legacy WM_NAME after,
// "(sin título)" when a window offers neither.
QString title(xcb_connection_t *c, const Atoms &a, xcb_window_t win);

// The instance name from WM_CLASS — for our own programs that is the binary
// ("castalia-calc"), which is how the switcher finds a window's icon.
QString wmClassInstance(xcb_connection_t *c, xcb_window_t win);

// A window belongs in a window list unless it is a dock/desktop panel or has
// asked to be skipped (_NET_WM_STATE_SKIP_TASKBAR).
bool listable(xcb_connection_t *c, const Atoms &a, xcb_window_t win);

// Ask the WM to raise, focus and un-minimize a window (_NET_ACTIVE_WINDOW).
void activate(xcb_connection_t *c, xcb_window_t root, const Atoms &a,
              xcb_window_t win);

} // namespace ewmh
} // namespace castalia
