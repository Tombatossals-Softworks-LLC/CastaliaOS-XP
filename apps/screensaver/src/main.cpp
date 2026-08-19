// castalia-salvapantallas — original animated screensavers (Bible §9, §8).
//
// Three self-contained, original scenes painted with QPainter — no assets, no
// GL, no external engine — in the Castalia sea-and-sandstone palette:
//   * ondas    — flowing azure wave bands with drifting bokeh and the mark
//   * mystify  — bouncing polyline ribbons with fading echoes (a classic)
//   * estrellas— a warp starfield
//   * aurora   — the shared castalia::paintAurora curtains, tinted by the
//                active theme's accent (the same scene the desktop's hidden
//                flourish shows — one implementation, two homes)
// Runs full-screen and exits on any key or mouse move; --preview shows it in a
// window; --screenshot renders a representative frame to PNG. Motion is a pure
// function of a frame counter (deterministic, so screenshots reproduce).
//
// Usage: castalia-salvapantallas [--mode ondas|mystify|estrellas|aurora]
//        [--theme classic] [--repo P] [--preview] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <cmath>
#include <cstdint>

#include "Aurora.h"
#include "Mark.h"
#include "Theme.h"

namespace {
// Deterministic xorshift PRNG so a given frame always looks the same.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x2545F4914F6CDD1DULL) {}
    uint64_t next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    double unit() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
    double range(double a, double b) { return a + unit() * (b - a); }
};

const QColor kSea0(0x0A, 0x14, 0x1F);
const QColor kSea1(0x0D, 0x1D, 0x2E);
const QColor kAzure(0x3E, 0x82, 0xB6);
const QColor kAzureLt(0x7F, 0xB0, 0xD4);
const QColor kSand(0xD8, 0xC4, 0x9A);
} // namespace

class Saver : public QWidget {
public:
    enum Mode { Ondas, Mystify, Estrellas, Aurora };
    Saver(Mode mode, bool preview, const QColor &accent = QColor())
        : m_mode(mode), m_preview(preview), m_accent(accent)
    {
        setAutoFillBackground(true);
        setMouseTracking(true);
        setWindowTitle(QStringLiteral("Salvapantallas de Castalia"));
        if (!preview) {
            setCursor(Qt::BlankCursor);
            setWindowFlag(Qt::FramelessWindowHint, true);
        }
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this]() {
            ++m_frame;
            update();
        });
    }
    void start() { m_timer->start(1000 / 60); }
    void seekTo(int frame) { m_frame = frame; }  // for deterministic shots

protected:
    void keyPressEvent(QKeyEvent *) override { finish(); }
    void mousePressEvent(QMouseEvent *) override { finish(); }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_preview)
            return;
        if (m_lastMouse.isNull())
            m_lastMouse = e->pos();
        else if ((e->pos() - m_lastMouse).manhattanLength() > 8)
            finish();
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        switch (m_mode) {
        case Ondas: drawOndas(p); break;
        case Mystify: drawMystify(p); break;
        case Estrellas: drawStars(p); break;
        case Aurora: drawAurora(p); break;
        }
    }

private:
    void finish() { if (!m_preview) close(); }

    void drawBackground(QPainter &p, const QColor &a, const QColor &b)
    {
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, a);
        g.setColorAt(1, b);
        p.fillRect(rect(), g);
    }

    // ---- Ondas: flowing sine bands + bokeh + gently pulsing mark ----------
    void drawOndas(QPainter &p)
    {
        drawBackground(p, kSea1, kSea0);
        const double t = m_frame / 60.0;
        const int W = width(), H = height();
        for (int band = 0; band < 5; ++band) {
            QPainterPath path;
            const double baseY = H * (0.45 + band * 0.11);
            const double amp = 26 + band * 10;
            const double ph = t * (0.5 + band * 0.18) + band;
            path.moveTo(0, baseY);
            for (int x = 0; x <= W; x += 12) {
                const double y = baseY
                    + amp * std::sin(x * 0.006 + ph)
                    + amp * 0.4 * std::sin(x * 0.013 - ph * 1.3);
                path.lineTo(x, y);
            }
            path.lineTo(W, H);
            path.lineTo(0, H);
            path.closeSubpath();
            QColor c = (band % 2) ? kAzure : kAzureLt;
            c.setAlpha(30 + band * 8);
            p.fillPath(path, c);
        }
        // drifting bokeh
        Rng r(1234567);
        for (int i = 0; i < 34; ++i) {
            const double bx = r.unit();
            const double by = r.unit();
            const double sp = r.range(0.01, 0.05);
            const double rad = r.range(6, 34);
            double x = std::fmod(bx + t * sp, 1.2) * W - 0.1 * W;
            double y = (by * 0.8 + 0.05) * H
                       + 10 * std::sin(t * 0.6 + i);
            QRadialGradient rg(QPointF(x, y), rad);
            QColor cc = (i % 3 == 0) ? kSand : kAzureLt;
            cc.setAlpha(60);
            rg.setColorAt(0, cc);
            cc.setAlpha(0);
            rg.setColorAt(1, cc);
            p.fillRect(QRectF(x - rad, y - rad, 2 * rad, 2 * rad), rg);
        }
        // the mark, softly pulsing
        const double s = 120 + 8 * std::sin(t * 1.2);
        p.save();
        p.setOpacity(0.9);
        castalia::drawMark(&p, QRectF(W / 2.0 - s / 2, H * 0.30 - s / 2,
                                      s, s));
        p.restore();
        drawWordmark(p);
    }

    // ---- Mystify: bouncing polyline ribbons with fading echoes ------------
    struct Poly { QVector<QPointF> pts, vel; QColor color; };
    void ensureMystify()
    {
        if (!m_polys.isEmpty())
            return;
        Rng r(0xC0FFEE);
        const int W = width() ? width() : 1024, H = height() ? height() : 768;
        for (int g = 0; g < 2; ++g) {
            Poly poly;
            poly.color = g ? kSand : kAzureLt;
            for (int i = 0; i < 5; ++i) {
                poly.pts.push_back(QPointF(r.range(0, W), r.range(0, H)));
                poly.vel.push_back(QPointF(r.range(-4, 4), r.range(-4, 4)));
            }
            m_polys.push_back(poly);
        }
        m_history.clear();
    }
    void drawMystify(QPainter &p)
    {
        drawBackground(p, QColor(0x0B, 0x12, 0x1A), QColor(0x05, 0x09, 0x0F));
        ensureMystify();
        const int W = width(), H = height();
        for (auto &poly : m_polys)
            for (int i = 0; i < poly.pts.size(); ++i) {
                QPointF &pt = poly.pts[i];
                QPointF &v = poly.vel[i];
                pt += v;
                if (pt.x() < 0 || pt.x() > W) { v.setX(-v.x()); pt.setX(
                    qBound(0.0, pt.x(), double(W))); }
                if (pt.y() < 0 || pt.y() > H) { v.setY(-v.y()); pt.setY(
                    qBound(0.0, pt.y(), double(H))); }
            }
        // snapshot current polygons into the echo history
        QVector<QPair<QVector<QPointF>, QColor>> snap;
        for (const auto &poly : m_polys)
            snap.push_back({poly.pts, poly.color});
        m_history.push_back(snap);
        while (m_history.size() > 14)
            m_history.removeFirst();
        // draw echoes oldest→newest, brightening
        for (int h = 0; h < m_history.size(); ++h) {
            const double a = double(h + 1) / m_history.size();
            for (const auto &pc : m_history[h]) {
                QColor c = pc.second;
                c.setAlphaF(0.10 + 0.5 * a * a);
                p.setPen(QPen(c, 1.5));
                p.setBrush(Qt::NoBrush);
                QPolygonF poly(pc.first);
                p.drawPolygon(poly);
            }
        }
    }

    // ---- Estrellas: warp starfield ----------------------------------------
    struct Star { double x, y, z; };
    void ensureStars()
    {
        if (!m_stars.isEmpty())
            return;
        Rng r(999983);
        for (int i = 0; i < 320; ++i)
            m_stars.push_back({r.range(-1, 1), r.range(-1, 1),
                               r.range(0.02, 1.0)});
    }
    void drawStars(QPainter &p)
    {
        p.fillRect(rect(), QColor(0x04, 0x07, 0x0C));
        ensureStars();
        const double W = width(), H = height();
        const double cx = W / 2, cy = H / 2;
        for (auto &s : m_stars) {
            const double pz = s.z;
            s.z -= 0.012;
            if (s.z <= 0.02) {
                Rng r(uint64_t(std::llround((s.x + 2) * 1e6))
                      ^ (m_frame * 2654435761ULL));
                s.x = r.range(-1, 1);
                s.y = r.range(-1, 1);
                s.z = 1.0;
                continue;
            }
            auto proj = [&](double z) {
                return QPointF(cx + s.x / z * cx, cy + s.y / z * cy);
            };
            const QPointF now = proj(s.z);
            const QPointF was = proj(pz);
            if (!rect().contains(now.toPoint()))
                continue;
            const double b = qBound(0.0, (1.0 - s.z), 1.0);
            QColor c = (int(s.z * 97) % 5 == 0) ? kAzureLt
                                                : QColor(255, 255, 255);
            c.setAlphaF(0.25 + 0.75 * b);
            p.setPen(QPen(c, 0.6 + 2.4 * b, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(was, now);
        }
        drawWordmark(p);
    }

    // ---- Aurora: the shared curtains, plus the wordmark ------------------
    void drawAurora(QPainter &p)
    {
        // 7 s per full cycle at 60 fps, matching the desktop's flourish.
        castalia::paintAurora(&p, rect(), m_frame / 420.0, 1.0, m_accent);
        drawWordmark(p);
    }

    void drawWordmark(QPainter &p)
    {
        p.setPen(QColor(255, 255, 255, 150));
        QFont f = font();
        f.setPointSizeF(13);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 3);
        p.setFont(f);
        p.drawText(QRect(0, height() - 46, width(), 30), Qt::AlignHCenter,
                   QStringLiteral("CASTALIA  OS"));
    }

    Mode m_mode;
    bool m_preview;
    QColor m_accent;
    int m_frame = 0;
    QTimer *m_timer = nullptr;
    QPoint m_lastMouse;
    QVector<Poly> m_polys;
    QVector<QVector<QPair<QVector<QPointF>, QColor>>> m_history;
    QVector<Star> m_stars;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-salvapantallas"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("mode"),
                   QStringLiteral("ondas|mystify|estrellas|aurora"),
                   QStringLiteral("m"), QStringLiteral("ondas")});
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("preview"),
                   QStringLiteral("Windowed preview (does not exit on input)")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render one frame to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const ThemeTokens tokens =
        castalia::applyTheme(&app, repo, cli.value(QStringLiteral("theme")));

    const QString m = cli.value(QStringLiteral("mode"));
    Saver::Mode mode = Saver::Ondas;
    if (m == QStringLiteral("mystify")) mode = Saver::Mystify;
    else if (m == QStringLiteral("estrellas")) mode = Saver::Estrellas;
    else if (m == QStringLiteral("aurora")) mode = Saver::Aurora;

    const QString shot = cli.value(QStringLiteral("screenshot"));
    const bool preview = cli.isSet(QStringLiteral("preview")) || !shot.isEmpty();

    Saver saver(mode, preview, tokens.color(QStringLiteral("accent")));
    if (!shot.isEmpty()) {
        saver.resize(1024, 640);
        saver.show();
        // advance to a lively frame, then grab (deterministic).
        for (int i = 0; i < 90; ++i)
            saver.seekTo(i), saver.repaint();
        QTimer::singleShot(60, &app, [&]() {
            saver.grab().save(shot);
            app.quit();
        });
        return app.exec();
    }

    if (preview) {
        saver.resize(720, 460);
        saver.show();
    } else {
        saver.showFullScreen();
    }
    saver.start();
    return app.exec();
}
