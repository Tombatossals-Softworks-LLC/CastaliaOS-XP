#include "Mark.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QLinearGradient>
#include <QPainterPath>

namespace {

// A gradient placed the way SVG's default objectBoundingBox units place one:
// (x1,y1)-(x2,y2) are fractions of the shape's bounding box. Every ramp below
// mirrors branding/logo/castalia-mark.svg stop for stop, so the native mark
// and the SVG are the same picture — one is not a loose "impression" of the
// other.
QLinearGradient bboxGradient(const QRectF &b, qreal x1, qreal y1,
                             qreal x2, qreal y2)
{
    return QLinearGradient(b.x() + b.width() * x1, b.y() + b.height() * y1,
                           b.x() + b.width() * x2, b.y() + b.height() * y2);
}

const QColor kOutline(0x0E, 0x2C, 0x4E);       // the navy every edge wears
const QColor kHillOutline(0x20, 0x57, 0x1A);
const QColor kGlass(0xFF, 0xB0, 0x20);         // lit window
const QColor kDoor(0xF7, 0x93, 0x00);
const QColor kDoorGlow(0xFF, 0xD2, 0x7A);

QLinearGradient stone(const QRectF &b)
{
    QLinearGradient g = bboxGradient(b, 0, 0, 1, 0);
    g.setColorAt(0.00, QColor(0x87, 0xBE, 0xEC));
    g.setColorAt(0.45, QColor(0x5A, 0x98, 0xD6));
    g.setColorAt(1.00, QColor(0x3A, 0x72, 0xB4));
    return g;
}

QLinearGradient stoneDark(const QRectF &b)
{
    QLinearGradient g = bboxGradient(b, 0, 0, 1, 0);
    g.setColorAt(0.0, QColor(0x5A, 0x98, 0xD6));
    g.setColorAt(1.0, QColor(0x2F, 0x5F, 0x9B));
    return g;
}

QLinearGradient roof(const QRectF &b)
{
    QLinearGradient g = bboxGradient(b, 0, 0, 1, 1);
    g.setColorAt(0.0, QColor(0xF2, 0x6A, 0x52));
    g.setColorAt(1.0, QColor(0xC9, 0x28, 0x1C));
    return g;
}

// An arched opening: a flat-bottomed shape with a semicircular head — the
// window and door silhouette the whole mark repeats.
QPainterPath arched(qreal x, qreal top, qreal w, qreal bottom)
{
    const qreal r = w / 2.0;
    QPainterPath path(QPointF(x, bottom));
    path.lineTo(x, top);
    path.arcTo(QRectF(x, top - r, w, w), 180, -180);
    path.lineTo(x + w, bottom);
    path.closeSubpath();
    return path;
}

// One conical turret roof.
QPainterPath cone(qreal apexX, qreal apexY, qreal left, qreal right,
                  qreal base)
{
    QPainterPath path(QPointF(apexX, apexY));
    path.lineTo(right, base);
    path.lineTo(left, base);
    path.closeSubpath();
    return path;
}

} // namespace

namespace castalia {

// Geometry mirrors branding/logo/castalia-mark.svg on its 48-unit grid: the
// C monogram as a thick ring open to the right, with the keep standing in its
// bowl on two green hills. Centre (24,24), outer radius 22.6, inner 13.4,
// arms cut at ±35°. This is the fallback drawMark() uses when the artwork
// file is not reachable — same mark, no assets required.
void drawMarkVector(QPainter *p, const QRectF &rect)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->translate(rect.topLeft());
    const qreal s = qMin(rect.width(), rect.height()) / 48.0;
    p->scale(s, s);

    const QRectF outer(1.4, 1.4, 45.2, 45.2);       // r = 22.6 about (24,24)
    const QRectF inner(10.6, 10.6, 26.8, 26.8);     // r = 13.4

    // ---- the C ------------------------------------------------------------
    // Qt angles: 0° is 3 o'clock and positive sweeps anticlockwise, so the
    // ring runs from the upper arm (+35°) the long way round to the lower one.
    QPainterPath ring;
    ring.arcMoveTo(outer, 35);
    ring.arcTo(outer, 35, 290);
    ring.arcTo(inner, 325, -290);
    ring.closeSubpath();

    QLinearGradient ringFill(8.18, 1.4, 28.52, 46.6);
    ringFill.setColorAt(0.00, QColor(0x5A, 0xA0, 0xDC));
    ringFill.setColorAt(0.55, QColor(0x3B, 0x7C, 0xC4));
    ringFill.setColorAt(1.00, QColor(0x2A, 0x5F, 0xA0));
    QPen edge(kOutline, 1.7);
    edge.setJoinStyle(Qt::RoundJoin);
    p->setPen(edge);
    p->setBrush(ringFill);
    p->drawPath(ring);

    // ---- the keep ---------------------------------------------------------
    p->save();
    p->translate(2.9, 1.7);
    p->scale(0.9, 0.9);

    QPen keepEdge(kOutline, 1.5);
    keepEdge.setJoinStyle(Qt::RoundJoin);
    QPen fineEdge(kOutline, 1.0);

    // left turret
    const QRectF leftBody(9.6, 15, 6.8, 21);
    p->setPen(keepEdge);
    p->setBrush(stoneDark(leftBody));
    p->drawRect(leftBody);
    const QPainterPath leftRoof = cone(13, 5.6, 8.2, 17.8, 15.6);
    p->setBrush(roof(leftRoof.boundingRect()));
    p->drawPath(leftRoof);
    p->setPen(fineEdge);
    p->setBrush(kGlass);
    p->drawPath(arched(11.4, 21.8, 3.2, 26.6));

    // main keep: body and three merlons
    const QRectF body(15.2, 10, 11.2, 26);
    p->setPen(keepEdge);
    p->setBrush(stone(body));
    p->drawRect(body);
    for (const qreal mx : {15.2, 19.3, 23.4}) {
        const QRectF merlon(mx, 6.4, 3, 4.4);
        p->setBrush(stone(merlon));
        p->drawRect(merlon);
    }
    p->setPen(fineEdge);
    p->setBrush(kGlass);
    p->drawPath(arched(18.7, 14, 4.2, 19.4));
    p->setPen(QPen(kOutline, 1.3));
    p->setBrush(kDoor);
    p->drawPath(arched(17.8, 27.4, 6, 36));
    p->setPen(Qt::NoPen);
    QColor glow = kDoorGlow;
    glow.setAlphaF(0.85);
    p->setBrush(glow);
    p->drawPath(arched(19.3, 28, 3, 36));

    // right turret
    const QRectF rightBody(26.2, 22, 5.8, 14);
    p->setPen(keepEdge);
    p->setBrush(stoneDark(rightBody));
    p->drawRect(rightBody);
    const QPainterPath rightRoof = cone(29.1, 14, 24.8, 33.4, 22.6);
    p->setBrush(roof(rightRoof.boundingRect()));
    p->drawPath(rightRoof);
    p->setPen(fineEdge);
    p->setBrush(kGlass);
    p->drawPath(arched(27.7, 27.4, 2.8, 31.6));

    // the lit edge
    QColor lit(0xD6, 0xEA, 0xFF);
    lit.setAlphaF(0.5);
    QPen litPen(lit, 1.1);
    litPen.setCapStyle(Qt::RoundCap);
    p->setPen(litPen);
    p->drawLine(QPointF(16.5, 11), QPointF(16.5, 35));
    p->restore();

    // ---- the hills --------------------------------------------------------
    // Each wave starts and ends ON the badge circle and closes along it, so
    // the mark needs no clip to stay inside its own rim.
    auto hill = [&outer](qreal lx, qreal ly, qreal c1x, qreal c1y,
                         qreal mx, qreal my, qreal c2x, qreal c2y,
                         qreal rx, qreal ry, qreal from, qreal sweep) {
        QPainterPath path(QPointF(lx, ly));
        path.quadTo(c1x, c1y, mx, my);
        path.quadTo(c2x, c2y, rx, ry);
        path.arcTo(outer, from, sweep);
        path.closeSubpath();
        return path;
    };

    QPen hillEdge(kHillOutline, 1.3);
    hillEdge.setJoinStyle(Qt::RoundJoin);
    p->setPen(hillEdge);

    const QPainterPath back = hill(2.58, 31.2, 13, 26.6, 24, 30.4,
                                   35, 34.2, 45.79, 30, 344.31, -145.37);
    QLinearGradient backFill = bboxGradient(back.boundingRect(), 0, 0, 0, 1);
    backFill.setColorAt(0.0, QColor(0x8E, 0xCC, 0x57));
    backFill.setColorAt(1.0, QColor(0x5D, 0xA6, 0x34));
    p->setBrush(backFill);
    p->drawPath(back);

    const QPainterPath front = hill(4.15, 34.8, 14, 30.4, 26, 34.4,
                                    38, 38.4, 44.17, 34.2, 333.87, -127.62);
    QLinearGradient frontFill = bboxGradient(front.boundingRect(), 0, 0, 0, 1);
    frontFill.setColorAt(0.0, QColor(0x6F, 0xBC, 0x41));
    frontFill.setColorAt(1.0, QColor(0x3D, 0x87, 0x24));
    p->setBrush(frontFill);
    p->drawPath(front);

    p->restore();
}

// --------------------------------------------------------------- artwork ---

QString assetRoot(const QString &repoRoot)
{
    auto usable = [](const QString &root) {
        return !root.isEmpty()
               && QFile::exists(QDir(root).filePath(
                      QStringLiteral("branding/logo")));
    };
    if (usable(repoRoot))
        return repoRoot;
    const QString env = qEnvironmentVariable("CASTALIA_REPO");
    if (usable(env))
        return env;
    const QString installed = QStringLiteral("/usr/share/castalia");
    if (usable(installed))
        return installed;
    return QStringLiteral(".");
}

QPixmap markPixmap(const QSize &size, const QString &repoRoot)
{
    if (size.isEmpty())
        return QPixmap();
    // Keyed by root *and* size: one process can legitimately ask for several
    // sizes (a 24 px orb and a 96 px header), and a rescue shell can point at
    // a different tree than the session's.
    static QHash<QString, QPixmap> cache;
    const QString root = assetRoot(repoRoot);
    const QString key = QStringLiteral("%1|%2x%3")
                            .arg(root).arg(size.width()).arg(size.height());
    const auto hit = cache.constFind(key);
    if (hit != cache.constEnd())
        return *hit;

    QPixmap art(QDir(root).filePath(
        QStringLiteral("branding/logo/castalia-logo.png")));
    if (!art.isNull()) {
        // Halve repeatedly before the final step. One 256 → 24 smooth scale
        // samples far too sparsely for artwork with a 3 px outline — the rim
        // goes to mush and the windows disappear. Successive halving keeps
        // each step within the filter's reach, and it happens once per size
        // thanks to the cache above.
        QSize target = size;
        while (art.width() > target.width() * 2
               && art.height() > target.height() * 2) {
            art = art.scaled(art.size() / 2, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
        }
        art = art.scaled(target, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
    }
    cache.insert(key, art);
    return art;
}

void drawMark(QPainter *p, const QRectF &rect, const QString &repoRoot)
{
    if (!p || rect.isEmpty())
        return;
    // Small sizes get the vector re-drawing, not the artwork. The logo is a
    // 256 px painting with a 3 px outline, arrow-slit windows and two turret
    // cones; below ~32 px no resampling filter can keep those — the rim goes
    // soft and the windows vanish. The vector draws the same mark crisply at
    // any size, which is exactly what a hand-tuned small icon is for.
    // Compared side by side at 16/24/32/48 px before this line was written.
    if (qMin(rect.width(), rect.height()) < 32.0) {
        drawMarkVector(p, rect);
        return;
    }
    const QPixmap art = markPixmap(rect.size().toSize(), repoRoot);
    if (art.isNull()) {
        drawMarkVector(p, rect);          // no asset tree — draw the geometry
        return;
    }
    const bool wasSmooth =
        p->testRenderHint(QPainter::SmoothPixmapTransform);
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);
    p->drawPixmap(QPointF(rect.x() + (rect.width() - art.width()) / 2.0,
                          rect.y() + (rect.height() - art.height()) / 2.0),
                  art);
    p->setRenderHint(QPainter::SmoothPixmapTransform, wasSmooth);
}

} // namespace castalia
