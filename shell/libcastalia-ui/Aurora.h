// libcastalia-ui — the Aurora: Castalia's one piece of pure spectacle.
//
// Two things live here, deliberately kept free of widgets so both can be
// exercised head-lessly (Bible §17.4):
//
//   * KonamiDetector — the classic ↑ ↑ ↓ ↓ ← → ← → B A sequence, as a pure
//     state machine over key codes. The desktop plane feeds it key presses;
//     the self-test feeds it arrays.
//   * paintAurora()  — draws one frame of the aurora curtain at a given
//     phase. A *pure function of its arguments*: the same phase paints the
//     same pixels, so a screenshot of it is reproducible and a screensaver,
//     an easter egg and a press-kit still can all share one implementation.
//
// Nothing here allocates a timer or touches global state.
#pragma once

#include <QColor>
#include <QRectF>
#include <QVector>

class QPainter;

namespace castalia {

// The ↑ ↑ ↓ ↓ ← → ← → B A cheat code, as a stream matcher over Qt::Key values.
class KonamiDetector {
public:
    // Feed one key press. Returns true exactly on the key that completes the
    // sequence (and rearms itself for the next run).
    bool feed(int key);

    // Number of keys matched so far (0 … sequence().size() - 1).
    int progress() const { return m_index; }

    void reset() { m_index = 0; }

    // The sequence itself, in order.
    static const QVector<int> &sequence();

private:
    int m_index = 0;
};

// Paint one frame of the aurora into `rect`.
//
//   phase    — animation position; any real number, wraps at 1.0.
//   opacity  — 0 … 1 master fade (the whole frame scales by it).
//   accent   — the active theme's accent; the curtains are tinted towards it
//              so the aurora belongs to whichever theme is on.
//
// The frame is opaque at opacity = 1: it paints its own night sky first, so
// it can be laid over a wallpaper without any preparation.
void paintAurora(QPainter *painter, const QRectF &rect, qreal phase,
                 qreal opacity, const QColor &accent);

} // namespace castalia
