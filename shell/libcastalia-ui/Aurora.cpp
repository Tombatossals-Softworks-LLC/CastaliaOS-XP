#include "Aurora.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QtGlobal>

#include <cmath>

namespace castalia {

// ------------------------------------------------------------- Konami ----

const QVector<int> &KonamiDetector::sequence()
{
    // Qt::Key values, spelled numerically so this header stays free of
    // <QtGui> for the head-less consumers.
    static const QVector<int> keys = {
        0x01000013, 0x01000013,             // Up, Up
        0x01000015, 0x01000015,             // Down, Down
        0x01000012, 0x01000014,             // Left, Right
        0x01000012, 0x01000014,             // Left, Right
        0x42, 0x41,                         // B, A
    };
    return keys;
}

bool KonamiDetector::feed(int key)
{
    const QVector<int> &seq = sequence();
    if (key == seq.at(m_index)) {
        if (++m_index == seq.size()) {
            m_index = 0;
            return true;
        }
        return false;
    }
    // A wrong key restarts the run — but the key that broke it may itself be
    // a valid opening (↑ ↑ ↑ ↓ ↓ … must still work), so re-test it from zero
    // rather than throwing it away.
    m_index = (key == seq.first()) ? 1 : 0;
    return false;
}

// ------------------------------------------------------------- Aurora ----

namespace {

// A tiny deterministic generator: the star field must be identical every run
// (and in every screenshot) without shipping a table of coordinates.
struct Lcg {
    quint32 s;
    explicit Lcg(quint32 seed) : s(seed ? seed : 1u) {}
    qreal next()                    // → [0, 1)
    {
        s = s * 1664525u + 1013904223u;
        return (s >> 8) / qreal(1u << 24);
    }
};

// Blend two colours; t = 0 → a, t = 1 → b.
QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

} // namespace

void paintAurora(QPainter *p, const QRectF &rect, qreal phase, qreal opacity,
                 const QColor &accent)
{
    if (!p || rect.isEmpty() || opacity <= 0.0)
        return;
    opacity = qBound(qreal(0.0), opacity, qreal(1.0));
    const qreal w = rect.width(), h = rect.height();
    const QColor tint = accent.isValid() ? accent : QColor(0xF5, 0x79, 0x00);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setClipRect(rect);
    p->setOpacity(opacity);
    p->setPen(Qt::NoPen);

    // 1) the night sky — deep indigo at the zenith, warmer near the horizon
    QLinearGradient sky(rect.topLeft(), rect.bottomLeft());
    sky.setColorAt(0.0, QColor(0x06, 0x08, 0x18));
    sky.setColorAt(0.62, QColor(0x0C, 0x14, 0x2E));
    sky.setColorAt(1.0, mix(QColor(0x14, 0x1A, 0x2C), tint, 0.18));
    p->fillRect(rect, sky);

    // 2) stars — fixed positions, breathing out of phase with one another
    Lcg rng(0x0CA57A11u);
    for (int i = 0; i < 110; ++i) {
        const qreal x = rect.left() + rng.next() * w;
        const qreal y = rect.top() + rng.next() * h * 0.78;
        const qreal size = 0.6 + rng.next() * 1.5;
        const qreal twinklePhase = rng.next();
        const qreal tw = 0.45 + 0.55 * std::sin((phase * 2 + twinklePhase)
                                                * 2 * M_PI);
        QColor star(0xFF, 0xFF, 0xFF);
        star.setAlphaF(qBound(qreal(0.0), 0.25 + 0.6 * tw, qreal(1.0)));
        p->setBrush(star);
        p->drawEllipse(QPointF(x, y), size, size);
    }

    // 3) the curtains — four bands of sine-driven ribbon, each drifting at its
    //    own rate so they weave instead of marching in step. Every band is
    //    filled with a vertical gradient that fades to nothing top and bottom,
    //    which is what reads as "aurora" rather than "wavy stripe".
    const QColor hues[4] = {
        mix(QColor(0x5C, 0xE0, 0xB0), tint, 0.25),   // green
        mix(QColor(0x62, 0xC4, 0xFF), tint, 0.20),   // ice blue
        mix(QColor(0xC4, 0x8B, 0xFF), tint, 0.30),   // violet
        mix(QColor(0xFF, 0xD2, 0x7F), tint, 0.55),   // warm gold
    };
    for (int band = 0; band < 4; ++band) {
        const qreal baseY = rect.top() + h * (0.09 + 0.11 * band);
        const qreal amp = h * (0.040 + 0.015 * band);
        const qreal thickness = h * (0.20 + 0.04 * band);
        const qreal k = (2.0 + 0.7 * band) * 2 * M_PI / w;
        const qreal drift = phase * 2 * M_PI * (0.7 + 0.35 * band)
                            + band * 1.7;
        // The upper edge of the curtain, as a function of x.
        auto edge = [&](qreal x) {
            return baseY + amp * std::sin(k * x + drift)
                   + amp * 0.4 * std::sin(k * 1.9 * x - drift * 0.6);
        };

        QPainterPath path;
        const int steps = 64;
        for (int i = 0; i <= steps; ++i) {
            const qreal x = rect.left() + w * i / steps;
            if (i == 0)
                path.moveTo(x, edge(x));
            else
                path.lineTo(x, edge(x));
        }
        for (int i = steps; i >= 0; --i) {
            // The skirt runs past where the gradient has already faded to
            // nothing, so the path's own bottom edge is never a visible cut.
            const qreal x = rect.left() + w * i / steps;
            path.lineTo(x, edge(x) + thickness
                                         * (1.1 + 0.3
                                                * std::sin(k * 0.6 * x
                                                           + drift)));
        }
        path.closeSubpath();

        // Bright at the top edge, gone well before the hem — a curtain
        // hanging from its own crest, not a filled stripe.
        QLinearGradient g(0, baseY - amp, 0, baseY + amp + thickness);
        QColor c = hues[band];
        c.setAlphaF(0.0);
        g.setColorAt(0.0, c);
        c.setAlphaF(0.30 - 0.045 * band);
        g.setColorAt(0.14, c);
        c.setAlphaF(0.10 - 0.015 * band);
        g.setColorAt(0.42, c);
        c.setAlphaF(0.0);
        g.setColorAt(0.70, c);
        g.setColorAt(1.0, c);
        p->setBrush(g);
        p->drawPath(path);

        // The crest itself: a thin, bright filament along the top edge.
        QPainterPath crest;
        for (int i = 0; i <= steps; ++i) {
            const qreal x = rect.left() + w * i / steps;
            if (i == 0)
                crest.moveTo(x, edge(x));
            else
                crest.lineTo(x, edge(x));
        }
        QColor line = hues[band].lighter(135);
        line.setAlphaF(0.42 - 0.06 * band);
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(line, 1.6));
        p->drawPath(crest);
        p->setPen(Qt::NoPen);

        // Striations: the vertical rays that make an aurora read as an
        // aurora. Clipped to the curtain, so they cost nothing outside it.
        p->save();
        p->setClipPath(path, Qt::IntersectClip);
        const int rays = 74;
        // One gradient for the whole band rather than one per ray: the pen's
        // brush is in painter coordinates, so every ray fades along the same
        // vertical profile as the curtain it belongs to, and a frame costs
        // four gradients instead of three hundred.
        QLinearGradient rayFade(0, baseY - amp, 0, baseY + amp + thickness);
        QColor rayHue = hues[band].lighter(120);
        for (int i = 0; i < rays; ++i) {
            const qreal x = rect.left() + w * (i + 0.5) / rays;
            const qreal f = std::sin(x * 0.06 + drift * 1.7 + band)
                            * std::sin(x * 0.017 - drift);
            if (f <= 0.05)
                continue;
            const qreal peak = qBound(qreal(0.0), 0.20 * f, qreal(0.28));
            rayHue.setAlphaF(peak * 0.6);
            rayFade.setColorAt(0.0, rayHue);
            rayHue.setAlphaF(peak);
            rayFade.setColorAt(0.12, rayHue);
            rayHue.setAlphaF(0.0);
            rayFade.setColorAt(0.58, rayHue);
            rayFade.setColorAt(1.0, rayHue);
            p->setPen(QPen(QBrush(rayFade), w / rays * 0.55));
            p->drawLine(QPointF(x, edge(x)),
                        QPointF(x, edge(x) + thickness));
        }
        p->restore();
        p->setPen(Qt::NoPen);
    }

    // 4) the horizon glow the curtains stand on
    QRadialGradient glow(QPointF(rect.center().x(), rect.bottom()),
                         qMax(w, h) * 0.7);
    QColor warm = mix(tint, QColor(0xFF, 0xF3, 0xDC), 0.45);
    warm.setAlphaF(0.30);
    glow.setColorAt(0.0, warm);
    warm.setAlphaF(0.0);
    glow.setColorAt(1.0, warm);
    p->setBrush(glow);
    p->drawRect(rect);

    p->restore();
}

} // namespace castalia
