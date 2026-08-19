#include "Ewmh.h"

#include <cstring>

namespace castalia {
namespace ewmh {

xcb_atom_t atom(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_cookie_t ck =
        xcb_intern_atom(c, 0, static_cast<uint16_t>(std::strlen(name)), name);
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, ck, nullptr);
    const xcb_atom_t a = r ? r->atom : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
    free(r);
    return a;
}

Atoms Atoms::intern(xcb_connection_t *c)
{
    Atoms a;
    a.clientList = atom(c, "_NET_CLIENT_LIST");
    a.activeWindow = atom(c, "_NET_ACTIVE_WINDOW");
    a.wmName = atom(c, "_NET_WM_NAME");
    a.utf8 = atom(c, "UTF8_STRING");
    a.winType = atom(c, "_NET_WM_WINDOW_TYPE");
    a.typeDock = atom(c, "_NET_WM_WINDOW_TYPE_DOCK");
    a.typeDesktop = atom(c, "_NET_WM_WINDOW_TYPE_DESKTOP");
    a.state = atom(c, "_NET_WM_STATE");
    a.skipTaskbar = atom(c, "_NET_WM_STATE_SKIP_TASKBAR");
    a.icon = atom(c, "_NET_WM_ICON");
    return a;
}

QVector<uint32_t> cardinals(xcb_connection_t *c, xcb_window_t win,
                            xcb_atom_t prop, xcb_atom_t type,
                            uint32_t maxWords)
{
    QVector<uint32_t> out;
    xcb_get_property_cookie_t ck =
        xcb_get_property(c, 0, win, prop, type, 0, maxWords);
    xcb_get_property_reply_t *r = xcb_get_property_reply(c, ck, nullptr);
    if (r) {
        const int n = xcb_get_property_value_length(r)
            / static_cast<int>(sizeof(uint32_t));
        const auto *v = static_cast<uint32_t *>(xcb_get_property_value(r));
        out.reserve(n);
        for (int i = 0; i < n; ++i)
            out.append(v[i]);
        free(r);
    }
    return out;
}

QString title(xcb_connection_t *c, const Atoms &a, xcb_window_t win)
{
    xcb_get_property_cookie_t ck =
        xcb_get_property(c, 0, win, a.wmName, a.utf8, 0, 512);
    xcb_get_property_reply_t *r = xcb_get_property_reply(c, ck, nullptr);
    QString t;
    if (r) {
        const int len = xcb_get_property_value_length(r);
        if (len > 0)
            t = QString::fromUtf8(
                static_cast<const char *>(xcb_get_property_value(r)), len);
        free(r);
    }
    if (t.isEmpty()) {
        ck = xcb_get_property(c, 0, win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING,
                              0, 512);
        r = xcb_get_property_reply(c, ck, nullptr);
        if (r) {
            const int len = xcb_get_property_value_length(r);
            if (len > 0)
                t = QString::fromLatin1(
                    static_cast<const char *>(xcb_get_property_value(r)), len);
            free(r);
        }
    }
    return t.isEmpty() ? QStringLiteral("(sin título)") : t;
}

QString wmClassInstance(xcb_connection_t *c, xcb_window_t win)
{
    // WM_CLASS is two NUL-terminated strings: instance, then class. The
    // instance is the one that carries the program name.
    xcb_get_property_cookie_t ck = xcb_get_property(
        c, 0, win, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 128);
    xcb_get_property_reply_t *r = xcb_get_property_reply(c, ck, nullptr);
    QString instance;
    if (r) {
        const int len = xcb_get_property_value_length(r);
        const char *v = static_cast<const char *>(xcb_get_property_value(r));
        if (len > 0)
            instance = QString::fromLatin1(v, static_cast<int>(strnlen(
                v, static_cast<size_t>(len))));
        free(r);
    }
    return instance;
}

bool listable(xcb_connection_t *c, const Atoms &a, xcb_window_t win)
{
    for (uint32_t t : cardinals(c, win, a.winType, XCB_ATOM_ATOM))
        if (t == a.typeDock || t == a.typeDesktop)
            return false;
    for (uint32_t s : cardinals(c, win, a.state, XCB_ATOM_ATOM))
        if (s == a.skipTaskbar)
            return false;
    return true;
}

void activate(xcb_connection_t *c, xcb_window_t root, const Atoms &a,
              xcb_window_t win)
{
    xcb_client_message_event_t ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = win;
    ev.type = a.activeWindow;
    ev.data.data32[0] = 2;              // source indication: a pager/taskbar
    ev.data.data32[1] = XCB_CURRENT_TIME;
    ev.data.data32[2] = 0;
    xcb_send_event(c, 0, root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
                       | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   reinterpret_cast<const char *>(&ev));
    xcb_flush(c);
}

} // namespace ewmh
} // namespace castalia
