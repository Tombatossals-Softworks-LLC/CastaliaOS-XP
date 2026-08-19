// Castalia shell — the Alt+Tab window switcher (Bible §7.6).
//
// Openbox has a switcher of its own, but it is Openbox's: a flat list in
// Openbox's chrome, themable only as far as its themerc goes. This is
// Castalia's — the icon family, the accent, the card, the same hover language
// as the launch menu — and it is **most-recently-used ordered**, which is the
// half of Alt+Tab people actually feel: one press goes back to the window you
// just came from.
//
// It lives in the panel process rather than in one of its own. The §16 budget
// gives the switch ≤120 ms on FLOOR, and a resident window list that already
// speaks EWMH beats spawning a process (~70 ms before it draws a pixel) on
// every press.
//
// It owns the binding itself: an X passive grab on Alt+Tab, Alt+Shift+Tab and
// Alt+` — so `openbox-rc.xml` must NOT bind them, or Openbox takes the grab
// first and ours silently fails.
#pragma once

#include <QIcon>
#include <QString>
#include <QVector>
#include <QWidget>

#include "ThemeTokens.h"

class QImage;

class Switcher : public QWidget {
    Q_OBJECT
public:
    Switcher(const ThemeTokens &tokens, const QString &repo,
             QWidget *parent = nullptr);
    ~Switcher() override;

    // Take the X grabs and start following the focus. Separate from the
    // constructor on purpose: a screenshot render builds a panel too, and it
    // must not walk off with the session's Alt+Tab.
    void bind();

    // True when we hold the X grabs — i.e. Alt+Tab reaches us at all.
    bool isBound() const;

    // Fill the list with a fixed, representative set and show it, for the
    // deterministic offscreen render (--switcher-shot). Never used live.
    void showDemo();

    // Where the next press lands: wraps in both directions. Pure and pinned
    // by the self-test, because an off-by-one here means Alt+Tab skips the
    // window you were reaching for.
    static int step(int count, int current, bool forward);

    // The switcher's order: most-recently-used first, then any window we have
    // never seen focused, in the WM's own order. Windows in `mru` that are no
    // longer open drop out. Pure — this is the behaviour people judge Alt+Tab
    // by, so it is tested rather than trusted.
    static QVector<uint32_t> orderByMru(const QVector<uint32_t> &clients,
                                        const QVector<uint32_t> &mru);

    // Decode _NET_WM_ICON: a run of [width, height, w*h ARGB pixels] images.
    // Picks the smallest image at least `preferred` px (the largest one if
    // none reaches it) — scaling a 16 px icon up to 26 looks like a mistake.
    // Returns a null image on malformed data, which is not hypothetical:
    // this array comes from other people's programs.
    static QImage decodeIcon(const QVector<uint32_t> &data, int preferred);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onXcbEvent();

private:
    struct Entry {
        uint32_t window = 0;
        QString title;
        QIcon icon;
    };

    void begin(bool forward, bool sameApp);
    void advance(bool forward);
    void commit();
    void cancel();
    void layoutCard();
    QIcon iconFor(uint32_t window);

    struct Priv;
    Priv *d;
};
