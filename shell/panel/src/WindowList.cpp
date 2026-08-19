#include "WindowList.h"

#include "Ewmh.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHash>
#include <QLayoutItem>
#include <QPushButton>
#include <QSocketNotifier>
#include <QStyle>
#include <QTimer>
#include <QVariant>
#include <QVector>

#include <xcb/xcb.h>

struct WindowList::Priv {
    QHBoxLayout *lay = nullptr;
    xcb_connection_t *conn = nullptr;
    xcb_window_t root = 0;
    QSocketNotifier *notifier = nullptr;   // X fd → instant updates
    QTimer *coalesce = nullptr;            // merges event bursts into one refresh

    castalia::ewmh::Atoms a;

    QHash<uint32_t, QPushButton *> buttons;
    QVector<uint32_t> order;
};

WindowList::WindowList(bool demo, QWidget *parent)
    : QWidget(parent), d(new Priv)
{
    setObjectName(QStringLiteral("WindowList"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    d->lay = new QHBoxLayout(this);
    d->lay->setContentsMargins(0, 0, 0, 0);
    d->lay->setSpacing(4);

    if (!demo) {
        d->conn = xcb_connect(nullptr, nullptr);
        if (d->conn && xcb_connection_has_error(d->conn)) {
            xcb_disconnect(d->conn);
            d->conn = nullptr;
        }
    }

    if (!d->conn) { // offscreen / no EWMH WM — keep the panel looking finished
        buildDemo();
        return;
    }

    d->root = xcb_setup_roots_iterator(xcb_get_setup(d->conn)).data->root;
    d->a = castalia::ewmh::Atoms::intern(d->conn);

    // Subscribe to the WM's EWMH publications on the root window; from here
    // on the X server pushes changes to us the moment they happen.
    const uint32_t rootMask[] = {XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_change_window_attributes(d->conn, d->root, XCB_CW_EVENT_MASK,
                                 rootMask);
    xcb_flush(d->conn);

    d->coalesce = new QTimer(this);
    d->coalesce->setSingleShot(true);
    d->coalesce->setInterval(30);
    connect(d->coalesce, &QTimer::timeout, this, [this]() { refresh(); });

    d->notifier = new QSocketNotifier(xcb_get_file_descriptor(d->conn),
                                      QSocketNotifier::Read, this);
    // activated() changed signature across Qt 5 (int → QSocketDescriptor in
    // 5.15); a zero-argument lambda binds to either without naming the
    // overload, and we ignore the argument anyway — we drain the fd directly.
    connect(d->notifier, &QSocketNotifier::activated, this,
            [this]() { onXcbEvent(); });

    refresh();
    // Safety net only (a missed event, a WM restart): 5 s, not the old 1 s
    // poll — the event path above carries the real-time updates.
    auto *timer = new QTimer(this);
    timer->setTimerType(Qt::VeryCoarseTimer);
    connect(timer, &QTimer::timeout, this, [this]() { refresh(); });
    timer->start(5000);
}

// Drain every pending X event; a relevant PropertyNotify schedules ONE
// coalesced refresh (event bursts — e.g. a window opening — cost one pass).
void WindowList::onXcbEvent()
{
    if (!d->conn)
        return;
    bool dirty = false;
    while (xcb_generic_event_t *ev = xcb_poll_for_event(d->conn)) {
        if ((ev->response_type & 0x7F) == XCB_PROPERTY_NOTIFY) {
            const auto *pn =
                reinterpret_cast<xcb_property_notify_event_t *>(ev);
            if (pn->atom == d->a.clientList || pn->atom == d->a.activeWindow
                || pn->atom == d->a.wmName || pn->atom == XCB_ATOM_WM_NAME)
                dirty = true;
        }
        free(ev);
    }
    if (xcb_connection_has_error(d->conn)) {
        d->notifier->setEnabled(false);   // don't spin on a dead socket
        return;
    }
    if (dirty)
        scheduleRefresh();
}

void WindowList::scheduleRefresh()
{
    if (d->coalesce && !d->coalesce->isActive())
        d->coalesce->start();
}

WindowList::~WindowList()
{
    if (d->conn)
        xcb_disconnect(d->conn);
    delete d;
}

void WindowList::refresh()
{
    if (!d->conn)
        return;

    // active window (for the pressed-in highlight)
    uint32_t active = 0;
    const QVector<uint32_t> act = castalia::ewmh::cardinals(
        d->conn, d->root, d->a.activeWindow, XCB_ATOM_WINDOW);
    if (!act.isEmpty())
        active = act.first();

    // the managed clients, in creation order, minus panels/skip-taskbar ones
    QVector<uint32_t> clients;
    for (uint32_t w : castalia::ewmh::cardinals(d->conn, d->root,
                                                d->a.clientList,
                                                XCB_ATOM_WINDOW))
        if (castalia::ewmh::listable(d->conn, d->a, w))
            clients.append(w);

    // Only rebuild the button row when the SET of windows changed; otherwise
    // just refresh titles and the active highlight in place (no flicker).
    if (clients != d->order) {
        QLayoutItem *item;
        while ((item = d->lay->takeAt(0)) != nullptr) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
        d->buttons.clear();
        for (uint32_t w : clients) {
            auto *b = new QPushButton(this);
            b->setObjectName(QStringLiteral("TaskButton"));
            b->setCursor(Qt::PointingHandCursor);
            b->setMaximumWidth(220);
            connect(b, &QPushButton::clicked, this,
                    [this, w]() { activate(w); });
            d->lay->addWidget(b);
            d->buttons.insert(w, b);
            // Watch this client's title changes too (masks are per-listener
            // in X11, so this never disturbs the app's own event selection).
            const uint32_t mask[] = {XCB_EVENT_MASK_PROPERTY_CHANGE};
            xcb_change_window_attributes(d->conn, w, XCB_CW_EVENT_MASK,
                                         mask);
        }
        xcb_flush(d->conn);
        d->lay->addStretch(1);
        d->order = clients;
    }

    for (uint32_t w : clients) {
        QPushButton *b = d->buttons.value(w);
        if (!b)
            continue;
        const QString full = castalia::ewmh::title(d->conn, d->a, w);
        b->setText(QFontMetrics(b->font()).elidedText(full, Qt::ElideRight,
                                                       196));
        b->setToolTip(full);
        const bool on = (w == active);
        if (b->property("active").toBool() != on) {
            b->setProperty("active", on);
            b->style()->unpolish(b);
            b->style()->polish(b);
        }
    }
}

// Ask the WM to raise, focus and un-minimize the window (EWMH §_NET_ACTIVE).
void WindowList::activate(unsigned int window)
{
    if (!d->conn)
        return;
    castalia::ewmh::activate(d->conn, d->root, d->a, window);
}

// The fallback strip — the two windows the live demo opens first, so a
// screenshot without a live WM still shows a believable, populated taskbar.
void WindowList::buildDemo()
{
    auto add = [this](const QString &text, bool active) {
        auto *b = new QPushButton(text, this);
        b->setObjectName(QStringLiteral("TaskButton"));
        if (active)
            b->setProperty("active", true);
        d->lay->addWidget(b);
    };
    // Stand-in window titles for the screenshot gates. Translated like
    // any other string, so an English render does not show a Spanish
    // taskbar under an English menu (§7.13).
    add(tr("Documentos — Castalia Explorer"), true);
    add(tr("Centro de control"), false);
    d->lay->addStretch(1);
}
