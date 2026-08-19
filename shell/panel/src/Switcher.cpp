#include "Switcher.h"

#include <QApplication>
#include <QFontMetrics>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QSocketNotifier>
#include <QVariantAnimation>

#include <cstring>

#include <xcb/xcb.h>

#include "AppRoster.h"
#include "Ewmh.h"
#include "Theme.h"

namespace {

// The card's geometry. A row is icon + title; the icon is drawn at 26 px from
// the 48 px family, which is the size it still reads cleanly at.
const int kRow = 34;
const int kIcon = 26;
const int kPad = 12;
const int kGap = 10;
const int kMinWidth = 300;
const int kMaxWidth = 560;

// X keysyms we bind. Spelled out rather than pulled from xcb-keysyms: three
// constants are not worth a library dependency in every ISO.
const uint32_t kSymTab = 0xff09;
const uint32_t kSymEscape = 0xff1b;
const uint32_t kSymGrave = 0x0060;
const uint32_t kSymReturn = 0xff0d;

// The lock modifiers we have to ignore. An X grab matches the modifier state
// exactly, so a session with Caps Lock or Num Lock on would simply not have
// an Alt+Tab unless we grab every combination.
const uint32_t kLockCombos[] = {
    0,
    XCB_MOD_MASK_LOCK,
    XCB_MOD_MASK_2,
    XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2,
};

// Find the keycode bound to a keysym, by walking the server's keyboard map.
xcb_keycode_t keycodeFor(xcb_connection_t *c, uint32_t keysym)
{
    const xcb_setup_t *setup = xcb_get_setup(c);
    const int first = setup->min_keycode;
    const int count = setup->max_keycode - setup->min_keycode + 1;
    if (count <= 0)
        return 0;
    xcb_get_keyboard_mapping_reply_t *r = xcb_get_keyboard_mapping_reply(
        c, xcb_get_keyboard_mapping(c, static_cast<xcb_keycode_t>(first),
                                    static_cast<uint8_t>(count)),
        nullptr);
    if (!r)
        return 0;
    const int per = r->keysyms_per_keycode;
    const xcb_keysym_t *syms = xcb_get_keyboard_mapping_keysyms(r);
    const int total = xcb_get_keyboard_mapping_keysyms_length(r);
    xcb_keycode_t found = 0;
    for (int i = 0; i < total && !found; ++i)
        if (syms[i] == keysym)
            found = static_cast<xcb_keycode_t>(first + i / per);
    free(r);
    return found;
}

// The keycodes that produce Mod1 (Alt). We need them by keycode because the
// switch commits when Alt comes *up*, and a KeyRelease carries no modifier of
// its own to test.
QVector<xcb_keycode_t> mod1Keycodes(xcb_connection_t *c)
{
    QVector<xcb_keycode_t> out;
    xcb_get_modifier_mapping_reply_t *r = xcb_get_modifier_mapping_reply(
        c, xcb_get_modifier_mapping(c), nullptr);
    if (!r)
        return out;
    const int per = r->keycodes_per_modifier;
    const xcb_keycode_t *codes = xcb_get_modifier_mapping_keycodes(r);
    for (int i = 0; i < per; ++i) {          // Mod1 is the fourth of the eight
        const xcb_keycode_t k = codes[3 * per + i];
        if (k != 0)
            out.append(k);
    }
    free(r);
    return out;
}

} // namespace

struct Switcher::Priv {
    ThemeTokens tokens;
    QString repo;

    xcb_connection_t *conn = nullptr;
    xcb_window_t root = 0;
    castalia::ewmh::Atoms atoms;
    QSocketNotifier *notifier = nullptr;
    bool bound = false;
    bool grabbed = false;             // the keyboard grab, while Alt is held

    xcb_keycode_t tab = 0;
    xcb_keycode_t grave = 0;
    xcb_keycode_t escape = 0;
    xcb_keycode_t enter = 0;
    QVector<xcb_keycode_t> altKeys;

    QVector<Entry> entries;
    int index = 0;
    QVector<uint32_t> mru;            // most-recently-focused first
    QHash<uint32_t, QIcon> iconCache;

    QVariantAnimation *slide = nullptr;   // the selection bar, between rows
    qreal barY = 0;
};

int Switcher::step(int count, int current, bool forward)
{
    if (count <= 0)
        return 0;
    const int next = (current + (forward ? 1 : -1)) % count;
    return next < 0 ? next + count : next;
}

QVector<uint32_t> Switcher::orderByMru(const QVector<uint32_t> &clients,
                                       const QVector<uint32_t> &mru)
{
    QVector<uint32_t> out;
    out.reserve(clients.size());
    for (uint32_t w : mru)
        if (clients.contains(w) && !out.contains(w))
            out.append(w);
    for (uint32_t w : clients)          // never focused yet: the WM's order
        if (!out.contains(w))
            out.append(w);
    return out;
}

QImage Switcher::decodeIcon(const QVector<uint32_t> &data, int preferred)
{
    int bestAt = -1;
    int bestSide = 0;
    for (int i = 0; i + 2 <= data.size();) {
        const int w = static_cast<int>(data.at(i));
        const int h = static_cast<int>(data.at(i + 1));
        // Guard hard: this array is written by other people's toolkits, and a
        // bad width would walk us off the end of it.
        if (w <= 0 || h <= 0 || w > 1024 || h > 1024)
            break;
        const int words = w * h;
        if (i + 2 + words > data.size())
            break;
        const int side = qMin(w, h);
        const bool better = bestAt < 0
            || (bestSide < preferred && side > bestSide)
            || (side >= preferred && (bestSide < preferred
                                      || side < bestSide));
        if (better) {
            bestAt = i;
            bestSide = side;
        }
        i += 2 + words;
    }
    if (bestAt < 0)
        return QImage();

    const int w = static_cast<int>(data.at(bestAt));
    const int h = static_cast<int>(data.at(bestAt + 1));
    QImage img(w, h, QImage::Format_ARGB32);
    // _NET_WM_ICON words are 0xAARRGGBB, which is exactly Format_ARGB32.
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x)
            line[x] = static_cast<QRgb>(data.at(bestAt + 2 + y * w + x));
    }
    return img;
}

Switcher::Switcher(const ThemeTokens &tokens, const QString &repo,
                   QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint
                          | Qt::X11BypassWindowManagerHint
                          | Qt::WindowStaysOnTopHint),
      d(new Priv)
{
    d->tokens = tokens;
    d->repo = repo;
    setObjectName(QStringLiteral("Switcher"));
    // Override-redirect and never focused: the WM must not manage it, or the
    // switcher would show up in its own list.
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);
    hide();

    d->slide = new QVariantAnimation(this);
    d->slide->setDuration(120);           // §8.6: ≤200 ms, and skippable
    d->slide->setEasingCurve(QEasingCurve::OutCubic);
    connect(d->slide, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &v) { d->barY = v.toReal(); update(); });

}

void Switcher::bind()
{
    d->conn = xcb_connect(nullptr, nullptr);
    if (!d->conn || xcb_connection_has_error(d->conn)) {
        if (d->conn)
            xcb_disconnect(d->conn);
        d->conn = nullptr;
        return;                           // offscreen render: no binding
    }
    d->root = xcb_setup_roots_iterator(xcb_get_setup(d->conn)).data->root;
    d->atoms = castalia::ewmh::Atoms::intern(d->conn);

    d->tab = keycodeFor(d->conn, kSymTab);
    d->grave = keycodeFor(d->conn, kSymGrave);
    d->escape = keycodeFor(d->conn, kSymEscape);
    d->enter = keycodeFor(d->conn, kSymReturn);
    d->altKeys = mod1Keycodes(d->conn);
    if (!d->tab || d->altKeys.isEmpty())
        return;                           // a keyboard we cannot bind

    auto grab = [this](xcb_keycode_t code, uint32_t mods) {
        for (uint32_t lock : kLockCombos)
            xcb_grab_key(d->conn, 0, d->root,
                         static_cast<uint16_t>(mods | lock), code,
                         XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    };
    grab(d->tab, XCB_MOD_MASK_1);
    grab(d->tab, XCB_MOD_MASK_1 | XCB_MOD_MASK_SHIFT);
    if (d->grave) {                       // Alt+` — same-app windows (§7.6)
        grab(d->grave, XCB_MOD_MASK_1);
        grab(d->grave, XCB_MOD_MASK_1 | XCB_MOD_MASK_SHIFT);
    }

    // Follow the focus so the list is most-recently-used ordered.
    const uint32_t rootMask[] = {XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_change_window_attributes(d->conn, d->root, XCB_CW_EVENT_MASK,
                                 rootMask);
    xcb_flush(d->conn);
    d->bound = true;

    d->notifier = new QSocketNotifier(xcb_get_file_descriptor(d->conn),
                                      QSocketNotifier::Read, this);
    connect(d->notifier, &QSocketNotifier::activated, this,
            [this]() { onXcbEvent(); });
}

Switcher::~Switcher()
{
    if (d->conn) {
        if (d->grabbed)
            xcb_ungrab_keyboard(d->conn, XCB_CURRENT_TIME);
        xcb_ungrab_key(d->conn, XCB_GRAB_ANY, d->root, XCB_MOD_MASK_ANY);
        xcb_flush(d->conn);
        xcb_disconnect(d->conn);
    }
    delete d;
}

bool Switcher::isBound() const
{
    return d->bound;
}

void Switcher::onXcbEvent()
{
    if (!d->conn)
        return;
    while (xcb_generic_event_t *ev = xcb_poll_for_event(d->conn)) {
        switch (ev->response_type & 0x7F) {
        case XCB_PROPERTY_NOTIFY: {
            const auto *pn =
                reinterpret_cast<xcb_property_notify_event_t *>(ev);
            if (pn->atom == d->atoms.activeWindow) {
                const QVector<uint32_t> act = castalia::ewmh::cardinals(
                    d->conn, d->root, d->atoms.activeWindow, XCB_ATOM_WINDOW);
                if (!act.isEmpty() && act.first() != 0) {
                    d->mru.removeAll(act.first());
                    d->mru.prepend(act.first());
                }
            }
            break;
        }
        case XCB_KEY_PRESS: {
            const auto *kp = reinterpret_cast<xcb_key_press_event_t *>(ev);
            const bool shift = (kp->state & XCB_MOD_MASK_SHIFT) != 0;
            if (kp->detail == d->tab)
                isVisible() ? advance(!shift) : begin(!shift, false);
            else if (d->grave && kp->detail == d->grave)
                isVisible() ? advance(!shift) : begin(!shift, true);
            else if (isVisible() && kp->detail == d->escape)
                cancel();
            else if (isVisible() && kp->detail == d->enter)
                commit();
            break;
        }
        case XCB_KEY_RELEASE: {
            const auto *kr = reinterpret_cast<xcb_key_release_event_t *>(ev);
            // Letting go of Alt is the commit — that is the whole gesture.
            if (isVisible() && d->altKeys.contains(kr->detail))
                commit();
            break;
        }
        default:
            break;
        }
        free(ev);
    }
    if (xcb_connection_has_error(d->conn) && d->notifier)
        d->notifier->setEnabled(false);
}

QIcon Switcher::iconFor(uint32_t window)
{
    const auto cached = d->iconCache.constFind(window);
    if (cached != d->iconCache.constEnd())
        return cached.value();

    QIcon icon;
    // Our own programs come first, by WM_CLASS's instance — which for them is
    // the binary name. This order is deliberate: Qt publishes a _NET_WM_ICON
    // of its own for any application that never set one, so trusting the
    // property first dressed the whole roster in Qt's placeholder instead of
    // the family icons.
    const QString name = castalia::apps::iconForBinary(
        castalia::ewmh::wmClassInstance(d->conn, window));
    if (!name.isEmpty())
        icon = castalia::themeIcon(d->repo, name);

    if (icon.isNull()) {
        // Everyone else: what the program publishes is the only thing that
        // works for Wine, DOSBox and anything we did not write.
        const QImage own = decodeIcon(
            castalia::ewmh::cardinals(d->conn, window, d->atoms.icon,
                                      XCB_ATOM_CARDINAL, 64 * 1024),
            kIcon);
        if (!own.isNull())
            icon = QIcon(QPixmap::fromImage(
                own.scaled(kIcon, kIcon, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation)));
    }
    if (icon.isNull())
        icon = castalia::themeIcon(d->repo, QStringLiteral("window"));
    d->iconCache.insert(window, icon);
    return icon;
}

void Switcher::begin(bool forward, bool sameApp)
{
    if (!d->conn)
        return;
    QVector<uint32_t> clients;
    for (uint32_t w : castalia::ewmh::cardinals(d->conn, d->root,
                                                d->atoms.clientList,
                                                XCB_ATOM_WINDOW))
        if (castalia::ewmh::listable(d->conn, d->atoms, w))
            clients.append(w);

    QVector<uint32_t> ordered = orderByMru(clients, d->mru);
    if (sameApp && !ordered.isEmpty()) {
        // Alt+` cycles the windows of the program you are already in.
        const QString mine =
            castalia::ewmh::wmClassInstance(d->conn, ordered.first());
        QVector<uint32_t> same;
        for (uint32_t w : ordered)
            if (castalia::ewmh::wmClassInstance(d->conn, w) == mine)
                same.append(w);
        ordered = same;
    }
    if (ordered.size() < 2)
        return;                     // nothing to switch between

    d->entries.clear();
    for (uint32_t w : ordered)
        d->entries.append({w, castalia::ewmh::title(d->conn, d->atoms, w),
                           iconFor(w)});
    d->index = step(d->entries.size(), 0, forward);

    // Hold the keyboard for the rest of the gesture: the next Tab and the
    // Alt release both have to reach us and not the focused window.
    xcb_grab_keyboard_reply_t *g = xcb_grab_keyboard_reply(
        d->conn,
        xcb_grab_keyboard(d->conn, 0, d->root, XCB_CURRENT_TIME,
                          XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC),
        nullptr);
    d->grabbed = g && g->status == XCB_GRAB_STATUS_SUCCESS;
    free(g);

    layoutCard();
    d->barY = kPad + d->index * kRow;
    show();
    raise();
}

void Switcher::advance(bool forward)
{
    if (d->entries.isEmpty())
        return;
    d->index = step(d->entries.size(), d->index, forward);
    const qreal target = kPad + d->index * kRow;
    if (castalia::reduceMotion()) {
        d->barY = target;
        update();
        return;
    }
    d->slide->stop();
    d->slide->setStartValue(d->barY);
    d->slide->setEndValue(target);
    d->slide->start();
}

void Switcher::commit()
{
    const bool had = !d->entries.isEmpty();
    const uint32_t chosen = had ? d->entries.at(d->index).window : 0;
    cancel();
    if (chosen)
        castalia::ewmh::activate(d->conn, d->root, d->atoms, chosen);
}

void Switcher::cancel()
{
    d->slide->stop();
    hide();
    d->entries.clear();
    if (d->conn && d->grabbed) {
        xcb_ungrab_keyboard(d->conn, XCB_CURRENT_TIME);
        xcb_flush(d->conn);
        d->grabbed = false;
    }
}

void Switcher::layoutCard()
{
    const QFontMetrics fm(font());
    int textWidth = 0;
    for (const Entry &e : qAsConst(d->entries))
        textWidth = qMax(textWidth, fm.horizontalAdvance(e.title));
    const int w = qBound(kMinWidth,
                         kPad * 2 + kIcon + kGap + textWidth + kPad,
                         kMaxWidth);
    const int h = kPad * 2 + d->entries.size() * kRow;
    resize(w, h);

    QRect area;
    if (QScreen *s = QApplication::primaryScreen())
        area = s->availableGeometry();
    if (area.isEmpty())
        area = QRect(0, 0, 1024, 768);
    move(area.center() - QPoint(w / 2, h / 2));
}

void Switcher::showDemo()
{
    // Stand-in window titles for the screenshot gates (§7.13 — an English
    // render must not show a Spanish switcher).
    const struct { const char *title; const char *icon; } demo[] = {
        {QT_TR_NOOP("Documentos — Castalia Explorer"), "folder"},
        {QT_TR_NOOP("Centro de control"), "settings"},
        {QT_TR_NOOP("Notas — sin título"), "text-editor"},
    };
    d->entries.clear();
    for (const auto &e : demo)
        d->entries.append({0, tr(e.title),
                           castalia::themeIcon(d->repo,
                                               QLatin1String(e.icon))});
    d->index = 1;                       // where one Alt+Tab press lands
    layoutCard();
    d->barY = kPad + d->index * kRow;
    show();
}

void Switcher::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool hc = d->tokens.highContrast();
    const QColor surface = d->tokens.color(QStringLiteral("surface"));
    const QColor border = d->tokens.color(QStringLiteral("border"));
    const QColor text = d->tokens.color(QStringLiteral("text"));
    const QColor accent = d->tokens.color(QStringLiteral("accent"));
    const qreal radius = qMax(4, d->tokens.cornerRadius() * 2);

    // the card
    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(border, 1));
    p.setBrush(surface);
    p.drawRoundedRect(card, radius, radius);
    if (!hc) {                          // the shell's glass hairline (§8.3)
        p.setPen(QPen(QColor(255, 255, 255, 130), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(card.adjusted(1, 1, -1, -1), radius - 1,
                          radius - 1);
    }

    // the selection, drawn under the rows so the text stays crisp
    if (!d->entries.isEmpty()) {
        const QRectF sel(kPad - 4, d->barY, width() - 2 * (kPad - 4), kRow);
        if (hc) {
            p.fillRect(sel, accent);
        } else {
            QLinearGradient wash(sel.topLeft(), sel.topRight());
            QColor a = accent;
            a.setAlpha(110);
            wash.setColorAt(0.0, a);
            a.setAlpha(18);
            wash.setColorAt(1.0, a);
            p.setPen(Qt::NoPen);
            p.setBrush(wash);
            p.drawRoundedRect(sel, radius - 1, radius - 1);
            p.fillRect(QRectF(sel.left(), sel.top(), 3, sel.height()), accent);
        }
    }

    const QFontMetrics fm(font());
    const int textLeft = kPad + kIcon + kGap;
    const int textWidth = width() - textLeft - kPad;
    for (int i = 0; i < d->entries.size(); ++i) {
        const Entry &e = d->entries.at(i);
        const int top = kPad + i * kRow;
        e.icon.paint(&p, QRect(kPad, top + (kRow - kIcon) / 2, kIcon, kIcon));
        QFont f = font();
        f.setBold(i == d->index);
        p.setFont(f);
        p.setPen(hc && i == d->index
                     ? d->tokens.color(QStringLiteral("selection_text"))
                     : text);
        p.drawText(QRect(textLeft, top, textWidth, kRow),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(e.title, Qt::ElideRight, textWidth));
    }
}
