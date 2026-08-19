#include "XEmbedTray.h"

#include <QEvent>
#include <QImage>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QSocketNotifier>
#include <QVector>

#include <cstring>

#include <xcb/xcb.h>

namespace {

// The icon size the panel gives each docked window, and the gap between two of
// them. The spec lets the manager choose; 22 px matches our own tray buttons.
const int kIconSize = 22;
const int kSpacing = 4;

// System Tray Protocol opcodes (data32[1] of _NET_SYSTEM_TRAY_OPCODE).
const uint32_t kRequestDock = 0;

// XEmbed messages (data32[1] of _XEMBED).
const uint32_t kEmbeddedNotify = 0;

xcb_atom_t internAtom(xcb_connection_t *c, const QString &name)
{
    const QByteArray latin = name.toLatin1();
    xcb_intern_atom_cookie_t ck =
        xcb_intern_atom(c, 0, static_cast<uint16_t>(latin.size()),
                        latin.constData());
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, ck, nullptr);
    const xcb_atom_t a = r ? r->atom : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
    free(r);
    return a;
}

} // namespace

struct XEmbedTray::Priv {
    xcb_connection_t *conn = nullptr;
    xcb_window_t root = 0;
    xcb_window_t manager = 0;         // the window that owns the selection
    int screen = 0;
    bool owner = false;

    xcb_atom_t selection = 0;
    xcb_atom_t managerAtom = 0;
    xcb_atom_t opcode = 0;
    xcb_atom_t orientation = 0;
    xcb_atom_t xembed = 0;

    QSocketNotifier *notifier = nullptr;
    uint32_t background = 0;             // the flat fallback colour
    uint8_t depth = 24;
    xcb_gcontext_t gc = 0;               // for uploading container backgrounds

    // One docked icon: the application's window and the container of ours it
    // lives in. Ordered, because the tray is a row and icons keep their place.
    struct Item {
        xcb_window_t client = 0;
        xcb_window_t container = 0;
        xcb_pixmap_t back = 0;        // the slice of panel behind this icon
    };
    QVector<Item> items;

    int indexOf(xcb_window_t client) const
    {
        for (int i = 0; i < items.size(); ++i)
            if (items.at(i).client == client)
                return i;
        return -1;
    }
};

QString XEmbedTray::selectionAtomName(int screen)
{
    return QStringLiteral("_NET_SYSTEM_TRAY_S%1").arg(screen);
}

int XEmbedTray::widthFor(int items)
{
    return items <= 0 ? 0 : items * kIconSize + (items - 1) * kSpacing;
}

XEmbedTray::XEmbedTray(const QColor &background, QWidget *parent)
    : QWidget(parent), d(new Priv)
{
    d->background = background.isValid()
        ? (background.rgb() & 0xFFFFFFu) : 0x000000u;
    setObjectName(QStringLiteral("XEmbedTray"));
    setFixedWidth(0);                 // no icons yet: take no room at all
    setMinimumHeight(kIconSize);

    d->conn = xcb_connect(nullptr, &d->screen);
    if (!d->conn || xcb_connection_has_error(d->conn)) {
        if (d->conn)
            xcb_disconnect(d->conn);
        d->conn = nullptr;
        return;                       // offscreen render, no X: host nothing
    }

    const xcb_setup_t *setup = xcb_get_setup(d->conn);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < d->screen && it.rem; ++i)
        xcb_screen_next(&it);
    if (!it.data) {
        xcb_disconnect(d->conn);
        d->conn = nullptr;
        return;
    }
    d->root = it.data->root;
    d->depth = it.data->root_depth;

    d->selection = internAtom(d->conn, selectionAtomName(d->screen));
    d->managerAtom = internAtom(d->conn, QStringLiteral("MANAGER"));
    d->opcode = internAtom(d->conn, QStringLiteral("_NET_SYSTEM_TRAY_OPCODE"));
    d->orientation =
        internAtom(d->conn, QStringLiteral("_NET_SYSTEM_TRAY_ORIENTATION"));
    d->xembed = internAtom(d->conn, QStringLiteral("_XEMBED"));

    // Somebody else may already be the tray manager (another panel, a stray
    // session). Two managers would each adopt half the icons, so we check
    // first and simply stand down.
    xcb_get_selection_owner_reply_t *own = xcb_get_selection_owner_reply(
        d->conn, xcb_get_selection_owner(d->conn, d->selection), nullptr);
    const bool taken = own && own->owner != XCB_NONE;
    free(own);
    if (taken)
        return;

    // The manager window never shows anything; it exists to hold the selection
    // and to receive the dock requests.
    d->manager = xcb_generate_id(d->conn);
    const uint32_t values[] = {1, XCB_EVENT_MASK_STRUCTURE_NOTIFY
                                      | XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_create_window(d->conn, XCB_COPY_FROM_PARENT, d->manager, d->root,
                      -1, -1, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_COPY_FROM_PARENT,
                      XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, values);

    // Horizontal, because the panel is.
    const uint32_t horizontal = 0;
    xcb_change_property(d->conn, XCB_PROP_MODE_REPLACE, d->manager,
                        d->orientation, XCB_ATOM_CARDINAL, 32, 1,
                        &horizontal);

    xcb_set_selection_owner(d->conn, d->manager, d->selection,
                            XCB_CURRENT_TIME);
    own = xcb_get_selection_owner_reply(
        d->conn, xcb_get_selection_owner(d->conn, d->selection), nullptr);
    d->owner = own && own->owner == d->manager;
    free(own);
    if (!d->owner)
        return;                       // lost a race; host nothing

    // Announce it. Applications that started before the panel are sitting on
    // the root window waiting for exactly this message — without it they never
    // ask to dock, and the tray stays empty for the whole session.
    xcb_client_message_event_t ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = d->root;
    ev.type = d->managerAtom;
    ev.data.data32[0] = XCB_CURRENT_TIME;
    ev.data.data32[1] = d->selection;
    ev.data.data32[2] = d->manager;
    xcb_send_event(d->conn, 0, d->root, XCB_EVENT_MASK_STRUCTURE_NOTIFY,
                   reinterpret_cast<const char *>(&ev));
    xcb_flush(d->conn);

    d->gc = xcb_generate_id(d->conn);
    xcb_create_gc(d->conn, d->gc, d->root, 0, nullptr);

    d->notifier = new QSocketNotifier(xcb_get_file_descriptor(d->conn),
                                      QSocketNotifier::Read, this);
    connect(d->notifier, &QSocketNotifier::activated, this,
            [this]() { onXcbEvent(); });
}

XEmbedTray::~XEmbedTray()
{
    if (d->conn) {
        // Hand every icon back to the root window rather than destroying it
        // with us: a panel restart should not kill the application's icon.
        for (const Priv::Item &item : qAsConst(d->items)) {
            xcb_reparent_window(d->conn, item.client, d->root, 0, 0);
            xcb_destroy_window(d->conn, item.container);
            if (item.back)
                xcb_free_pixmap(d->conn, item.back);
        }
        if (d->manager)
            xcb_destroy_window(d->conn, d->manager);
        xcb_flush(d->conn);
        xcb_disconnect(d->conn);
    }
    delete d;
}

bool XEmbedTray::isManager() const
{
    return d->owner;
}

int XEmbedTray::itemCount() const
{
    return d->items.size();
}

void XEmbedTray::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    relayout();                       // the clock resizes, we slide with it
}

void XEmbedTray::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void XEmbedTray::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    for (QWidget *w = parentWidget(); w; w = w->parentWidget())
        w->installEventFilter(this);
    relayout();
}

bool XEmbedTray::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Move || event->type() == QEvent::Resize)
        relayout();
    return QWidget::eventFilter(watched, event);
}

void XEmbedTray::onXcbEvent()
{
    if (!d->conn)
        return;
    while (xcb_generic_event_t *ev = xcb_poll_for_event(d->conn)) {
        switch (ev->response_type & 0x7F) {
        case XCB_CLIENT_MESSAGE: {
            const auto *cm =
                reinterpret_cast<xcb_client_message_event_t *>(ev);
            if (cm->type == d->opcode
                && cm->data.data32[1] == kRequestDock
                && cm->data.data32[2] != 0)
                dock(cm->data.data32[2]);
            break;
        }
        case XCB_DESTROY_NOTIFY: {
            const auto *dn =
                reinterpret_cast<xcb_destroy_notify_event_t *>(ev);
            undock(dn->window);
            break;
        }
        case XCB_UNMAP_NOTIFY: {
            // An icon that unmaps itself is asking to be taken out of the
            // tray (it may map again later and re-request docking).
            const auto *un = reinterpret_cast<xcb_unmap_notify_event_t *>(ev);
            undock(un->window);
            break;
        }
        case XCB_REPARENT_NOTIFY: {
            // Somebody took the window away from us.
            const auto *rn =
                reinterpret_cast<xcb_reparent_notify_event_t *>(ev);
            const int i = d->indexOf(rn->window);
            if (i >= 0 && rn->parent != d->items.at(i).container)
                undock(rn->window);
            break;
        }
        default:
            break;
        }
        free(ev);
    }
    if (xcb_connection_has_error(d->conn) && d->notifier)
        d->notifier->setEnabled(false);   // do not spin on a dead socket
}

void XEmbedTray::dock(unsigned int client)
{
    if (!d->conn || d->indexOf(client) >= 0)
        return;

    // The container is ours, created with xcb, never handed to Qt. The first
    // version let Qt own it as a native QWidget, and Qt quietly reset its X
    // background whenever the panel re-laid itself out — which turned already
    // docked icons into blank boxes the moment a second one arrived. An
    // XEmbed client paints ParentRelative: the container's *X* background is
    // the only thing behind it, so nothing else may touch it.
    const auto host = static_cast<xcb_window_t>(window()->winId());
    const xcb_window_t container = xcb_generate_id(d->conn);
    // The flat colour is only the first instant's background: relayout()
    // replaces it with the actual slice of panel this icon covers.
    const uint32_t values[] = {d->background};
    xcb_create_window(d->conn, XCB_COPY_FROM_PARENT, container, host,
                      0, 0, kIconSize, kIconSize, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT,
                      XCB_CW_BACK_PIXEL, values);

    // Watch the client so we notice it going away.
    const uint32_t mask[] = {XCB_EVENT_MASK_STRUCTURE_NOTIFY
                             | XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_change_window_attributes(d->conn, client, XCB_CW_EVENT_MASK, mask);

    xcb_reparent_window(d->conn, client, container, 0, 0);
    const uint32_t geometry[] = {kIconSize, kIconSize};
    xcb_configure_window(d->conn, client,
                         XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                         geometry);

    // Tell the client it is embedded. Without this an XEmbed-aware toolkit
    // keeps waiting and never draws.
    xcb_client_message_event_t ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = client;
    ev.type = d->xembed;
    ev.data.data32[0] = XCB_CURRENT_TIME;
    ev.data.data32[1] = kEmbeddedNotify;
    ev.data.data32[2] = 0;
    ev.data.data32[3] = container;
    ev.data.data32[4] = 0;            // XEmbed protocol version
    xcb_send_event(d->conn, 0, client, XCB_EVENT_MASK_NO_EVENT,
                   reinterpret_cast<const char *>(&ev));

    xcb_map_window(d->conn, client);
    xcb_map_window(d->conn, container);

    d->items.append({static_cast<xcb_window_t>(client), container});
    setFixedWidth(widthFor(d->items.size()));
    relayout();
}

void XEmbedTray::undock(unsigned int client)
{
    const int i = d->indexOf(client);
    if (i < 0)
        return;
    xcb_destroy_window(d->conn, d->items.at(i).container);
    if (d->items.at(i).back)
        xcb_free_pixmap(d->conn, d->items.at(i).back);
    d->items.remove(i);
    setFixedWidth(widthFor(d->items.size()));
    relayout();
}

void XEmbedTray::relayout()
{
    if (!d->conn || d->items.isEmpty())
        return;
    // Our containers hang off the panel's own top-level window, so they move
    // in its coordinates — not ours.
    const QPoint origin = mapTo(window(), QPoint(0, 0));
    const int y = origin.y() + (height() - kIconSize) / 2;
    for (int i = 0; i < d->items.size(); ++i) {
        const uint32_t place[] = {
            static_cast<uint32_t>(origin.x() + i * (kIconSize + kSpacing)),
            static_cast<uint32_t>(y)};
        xcb_configure_window(d->conn, d->items.at(i).container,
                             XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, place);
        refreshBackground(i);
    }
    xcb_flush(d->conn);
}

void XEmbedTray::refreshBackground(int index)
{
    QWidget *top = window();
    if (!d->conn || !top || !top->isVisible())
        return;
    Priv::Item &item = d->items[index];

    // Ask the panel to paint the exact square this icon covers. Its own
    // gradient, the tray well and the glass on top all come along, so the
    // icon lands on the background it would have had if it were ours.
    const QPoint origin = mapTo(top, QPoint(0, 0));
    const QRect slice(origin.x() + index * (kIconSize + kSpacing),
                      origin.y() + (height() - kIconSize) / 2,
                      kIconSize, kIconSize);
    QImage shot(kIconSize, kIconSize, QImage::Format_RGB32);
    shot.fill(QColor(QRgb(d->background)));
    top->render(&shot, QPoint(0, 0), QRegion(slice),
                QWidget::DrawWindowBackground | QWidget::DrawChildren);

    if (!item.back) {
        item.back = xcb_generate_id(d->conn);
        xcb_create_pixmap(d->conn, d->depth, item.back, d->root,
                          kIconSize, kIconSize);
    }
    // Format_RGB32 is 0xffRRGGBB in native order, which is what a 24-bit
    // TrueColor X visual wants in a 32-bpp ZPixmap.
    xcb_put_image(d->conn, XCB_IMAGE_FORMAT_Z_PIXMAP, item.back, d->gc,
                  kIconSize, kIconSize, 0, 0, 0, d->depth,
                  static_cast<uint32_t>(shot.sizeInBytes()), shot.constBits());
    xcb_change_window_attributes(d->conn, item.container, XCB_CW_BACK_PIXMAP,
                                 &item.back);
    xcb_clear_area(d->conn, 0, item.container, 0, 0, 0, 0);
    // The client paints ParentRelative, so it has to redraw over the new
    // background — an expose on it is what makes that happen.
    xcb_clear_area(d->conn, 1, item.client, 0, 0, 0, 0);
}
