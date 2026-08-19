// castalia-buscaminas — Buscaminas, the classic mine-finding puzzle (Bible
// §9.3 "Classic Games", §9.4). A clean-room, original implementation: the
// rules of the game are not copyrightable, and every pixel here — the bevelled
// field, the LED counters, the little face, the mines and flags — is drawn
// natively with QPainter. No Microsoft assets, no image files, no external
// dependency beyond Qt5 + libcastalia-ui (§3.9).
//
// Rules: uncover every safe square without detonating a mine. Numbers tell how
// many mines touch a square. Right-click to flag a suspected mine. Click a
// revealed number with the right count of flags around it to "chord" (open the
// rest). The first click is always safe.
//
// Usage: castalia-buscaminas --theme classic [--repo P]
//        [--level principiante|intermedio|experto] [--seed N]
//        [--demo] [--screenshot out.png] [--selftest]

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFont>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Theme.h"

// ============================================================== board model ==
// Pure game logic, deliberately decoupled from any widget so it can be
// exercised head-less by --selftest.
namespace game {

struct Cell {
    bool mine = false;
    bool revealed = false;
    bool flagged = false;
    int adj = 0;
};

enum class State { Ready, Playing, Won, Lost };

// A small deterministic PRNG (xorshift32) so a --seed reproduces a layout
// exactly — used by screenshots and self-tests, and it keeps play honest
// without pulling in <random>'s heavier machinery.
class Rng {
public:
    explicit Rng(uint32_t seed) : m_s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next()
    {
        m_s ^= m_s << 13;
        m_s ^= m_s >> 17;
        m_s ^= m_s << 5;
        return m_s;
    }
    int below(int n) { return n > 0 ? int(next() % uint32_t(n)) : 0; }

private:
    uint32_t m_s;
};

class Board {
public:
    Board(int cols, int rows, int mines, uint32_t seed)
        : m_cols(cols), m_rows(rows), m_mines(mines), m_seed(seed)
    {
        m_cells.resize(size_t(cols) * size_t(rows));
    }

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }
    int mines() const { return m_mines; }
    State state() const { return m_state; }
    bool inBounds(int c, int r) const
    {
        return c >= 0 && c < m_cols && r >= 0 && r < m_rows;
    }
    const Cell &at(int c, int r) const { return m_cells[idx(c, r)]; }

    // The cell the player detonated (-1,-1 until a loss) so the view can flag
    // it red.
    int boomC() const { return m_boomC; }
    int boomR() const { return m_boomR; }

    int flagsPlaced() const
    {
        int n = 0;
        for (const Cell &cell : m_cells)
            if (cell.flagged)
                ++n;
        return n;
    }
    // May go negative if the player over-flags — the classic counter behaviour.
    int minesRemaining() const { return m_mines - flagsPlaced(); }

    // Left-click. First reveal lays the mines so the clicked cell (and, when
    // there is room, its neighbours) are guaranteed safe — no first-move death.
    void reveal(int c, int r)
    {
        if (m_state == State::Won || m_state == State::Lost)
            return;
        if (!inBounds(c, r))
            return;
        Cell &cell = m_cells[idx(c, r)];
        if (cell.flagged)
            return;
        if (cell.revealed) {
            chord(c, r);
            return;
        }
        if (!m_laid) {
            lay(c, r);
            m_state = State::Playing;
        }
        floodReveal(c, r);
        checkWin();
    }

    void toggleFlag(int c, int r)
    {
        if (m_state == State::Won || m_state == State::Lost)
            return;
        if (!inBounds(c, r))
            return;
        Cell &cell = m_cells[idx(c, r)];
        if (cell.revealed)
            return;
        cell.flagged = !cell.flagged;
    }

    // Open every un-flagged neighbour of a satisfied number (flags == number).
    void chord(int c, int r)
    {
        if (m_state != State::Playing)
            return;
        const Cell &cell = m_cells[idx(c, r)];
        if (!cell.revealed || cell.adj == 0)
            return;
        int flags = 0;
        forEachNeighbour(c, r, [&](int nc, int nr) {
            if (m_cells[idx(nc, nr)].flagged)
                ++flags;
        });
        if (flags != cell.adj)
            return;
        forEachNeighbour(c, r, [&](int nc, int nr) {
            Cell &n = m_cells[idx(nc, nr)];
            if (!n.flagged && !n.revealed)
                floodReveal(nc, nr);
        });
        checkWin();
    }

    // Reveal every remaining safe cell — used by --demo to build a rich shot.
    void revealAllSafe()
    {
        if (!m_laid)
            lay(m_cols / 2, m_rows / 2);
        m_state = State::Playing;
        for (int r = 0; r < m_rows; ++r)
            for (int c = 0; c < m_cols; ++c)
                if (!m_cells[idx(c, r)].mine)
                    m_cells[idx(c, r)].revealed = true;
        checkWin();
    }

    // Flag the first `n` real mines — for a tidy demo/screenshot only.
    void flagSomeMines(int n)
    {
        if (!m_laid)
            lay(m_cols / 2, m_rows / 2);
        for (size_t i = 0; i < m_cells.size() && n > 0; ++i)
            if (m_cells[i].mine && !m_cells[i].flagged) {
                m_cells[i].flagged = true;
                --n;
            }
    }

private:
    int idx(int c, int r) const { return r * m_cols + c; }

    template <typename F>
    void forEachNeighbour(int c, int r, F fn) const
    {
        for (int dr = -1; dr <= 1; ++dr)
            for (int dc = -1; dc <= 1; ++dc) {
                if (dc == 0 && dr == 0)
                    continue;
                const int nc = c + dc, nr = r + dr;
                if (inBounds(nc, nr))
                    fn(nc, nr);
            }
    }

    void lay(int safeC, int safeR)
    {
        // Cells forbidden from holding a mine: the first click and its ring,
        // as long as the board is big enough to still fit every mine.
        std::vector<char> forbidden(m_cells.size(), 0);
        forbidden[idx(safeC, safeR)] = 1;
        int safeCount = 1;
        forEachNeighbour(safeC, safeR, [&](int nc, int nr) {
            forbidden[idx(nc, nr)] = 1;
            ++safeCount;
        });
        const int free = int(m_cells.size()) - safeCount;
        if (free < m_mines) {
            // Tiny board: only protect the clicked cell itself.
            std::fill(forbidden.begin(), forbidden.end(), 0);
            forbidden[idx(safeC, safeR)] = 1;
        }

        Rng rng(m_seed);
        int placed = 0;
        const int total = int(m_cells.size());
        while (placed < m_mines) {
            const int p = rng.below(total);
            if (forbidden[p] || m_cells[p].mine)
                continue;
            m_cells[p].mine = true;
            ++placed;
        }
        for (int r = 0; r < m_rows; ++r)
            for (int c = 0; c < m_cols; ++c) {
                if (m_cells[idx(c, r)].mine)
                    continue;
                int n = 0;
                forEachNeighbour(c, r, [&](int nc, int nr) {
                    if (m_cells[idx(nc, nr)].mine)
                        ++n;
                });
                m_cells[idx(c, r)].adj = n;
            }
        m_laid = true;
    }

    void floodReveal(int c, int r)
    {
        Cell &cell = m_cells[idx(c, r)];
        if (cell.revealed || cell.flagged)
            return;
        cell.revealed = true;
        if (cell.mine) {
            m_state = State::Lost;
            m_boomC = c;
            m_boomR = r;
            for (Cell &k : m_cells)
                if (k.mine)
                    k.revealed = true;
            return;
        }
        if (cell.adj == 0)
            forEachNeighbour(c, r, [&](int nc, int nr) { floodReveal(nc, nr); });
    }

    void checkWin()
    {
        if (m_state != State::Playing)
            return;
        for (const Cell &cell : m_cells)
            if (!cell.mine && !cell.revealed)
                return;
        m_state = State::Won;
        for (Cell &cell : m_cells)
            if (cell.mine)
                cell.flagged = true;
    }

    int m_cols, m_rows, m_mines;
    uint32_t m_seed;
    bool m_laid = false;
    int m_boomC = -1, m_boomR = -1;
    State m_state = State::Ready;
    std::vector<Cell> m_cells;
};

} // namespace game

// ================================================================ painting ===
namespace paint {

// A raised or sunken 3D bevel, the visual grammar of the classic field.
void bevel(QPainter &p, const QRectF &r, bool raised, qreal t = 2.0)
{
    const QColor light = raised ? QColor(255, 255, 255) : QColor(128, 128, 128);
    const QColor dark = raised ? QColor(128, 128, 128) : QColor(255, 255, 255);
    p.fillRect(r, QColor(0xC0, 0xC0, 0xC0));
    QPainterPath tl;
    tl.moveTo(r.topLeft());
    tl.lineTo(r.topRight());
    tl.lineTo(r.topRight() + QPointF(-t, t));
    tl.lineTo(r.topLeft() + QPointF(t, t));
    tl.lineTo(r.bottomLeft() + QPointF(t, -t));
    tl.lineTo(r.bottomLeft());
    p.fillPath(tl, light);
    QPainterPath br;
    br.moveTo(r.bottomRight());
    br.lineTo(r.bottomLeft());
    br.lineTo(r.bottomLeft() + QPointF(t, -t));
    br.lineTo(r.bottomRight() + QPointF(-t, -t));
    br.lineTo(r.topRight() + QPointF(-t, t));
    br.lineTo(r.topRight());
    p.fillPath(br, dark);
}

// Classic per-number colours (1 blue … 8 grey).
QColor numberColor(int n)
{
    static const QColor c[9] = {
        QColor(0, 0, 0),        QColor(0x00, 0x00, 0xFF),
        QColor(0x00, 0x7B, 0x00), QColor(0xFF, 0x00, 0x00),
        QColor(0x00, 0x00, 0x7B), QColor(0x7B, 0x00, 0x00),
        QColor(0x00, 0x7B, 0x7B), QColor(0x00, 0x00, 0x00),
        QColor(0x80, 0x80, 0x80)};
    return c[std::clamp(n, 0, 8)];
}

void drawMine(QPainter &p, const QRectF &r)
{
    p.save();
    const QPointF ctr = r.center();
    const qreal rad = std::min(r.width(), r.height()) * 0.30;
    p.setPen(QPen(Qt::black, std::max<qreal>(1.0, rad * 0.28)));
    for (int i = 0; i < 4; ++i) {
        const double a = i * M_PI / 4.0;
        const QPointF d(std::cos(a) * rad * 1.5, std::sin(a) * rad * 1.5);
        p.drawLine(ctr - d, ctr + d);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.drawEllipse(ctr, rad, rad);
    p.setBrush(Qt::white);
    p.drawEllipse(ctr + QPointF(-rad * 0.35, -rad * 0.35), rad * 0.25,
                  rad * 0.25);
    p.restore();
}

void drawFlag(QPainter &p, const QRectF &r)
{
    p.save();
    const qreal w = r.width(), h = r.height();
    const QPointF base(r.left() + w * 0.5, r.top() + h * 0.78);
    // pole
    p.setPen(QPen(Qt::black, std::max<qreal>(1.0, w * 0.06)));
    p.drawLine(base, QPointF(base.x(), r.top() + h * 0.24));
    // stand
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.drawRect(QRectF(r.left() + w * 0.28, r.top() + h * 0.74, w * 0.44,
                      h * 0.10));
    // pennant
    QPainterPath flag;
    flag.moveTo(base.x(), r.top() + h * 0.24);
    flag.lineTo(r.left() + w * 0.24, r.top() + h * 0.36);
    flag.lineTo(base.x(), r.top() + h * 0.48);
    flag.closeSubpath();
    p.setBrush(QColor(0xD8, 0x00, 0x00));
    p.drawPath(flag);
    p.restore();
}

// Seven-segment digit for the LED counters. value -1 draws a minus sign;
// -2 draws a blank.
void drawDigit(QPainter &p, const QRectF &r, int value)
{
    static const uint8_t seg[10] = {
        /*0*/ 0b0111111, /*1*/ 0b0000110, /*2*/ 0b1011011, /*3*/ 0b1001111,
        /*4*/ 0b1100110, /*5*/ 0b1101101, /*6*/ 0b1111101, /*7*/ 0b0000111,
        /*8*/ 0b1111111, /*9*/ 0b1101111};
    uint8_t on = 0;
    if (value >= 0 && value <= 9)
        on = seg[value];
    else if (value == -1)
        on = 0b1000000; // minus = segment g only

    const qreal m = r.width() * 0.16;      // horizontal inset
    const qreal t = r.width() * 0.16;      // stroke thickness
    const qreal x0 = r.left() + m, x1 = r.right() - m;
    const qreal y0 = r.top() + m, ym = r.center().y(), y1 = r.bottom() - m;

    p.setBrush(QColor(0xFF, 0x30, 0x00));
    p.setPen(Qt::NoPen);
    auto hbar = [&](qreal y) {
        QPainterPath s;
        s.moveTo(x0, y);
        s.lineTo(x0 + t, y - t);
        s.lineTo(x1 - t, y - t);
        s.lineTo(x1, y);
        s.lineTo(x1 - t, y + t);
        s.lineTo(x0 + t, y + t);
        s.closeSubpath();
        p.drawPath(s);
    };
    auto vbar = [&](qreal x, qreal ya, qreal yb) {
        QPainterPath s;
        s.moveTo(x, ya);
        s.lineTo(x + t, ya + t);
        s.lineTo(x + t, yb - t);
        s.lineTo(x, yb);
        s.lineTo(x - t, yb - t);
        s.lineTo(x - t, ya + t);
        s.closeSubpath();
        p.drawPath(s);
    };
    // a=top f=topleft b=topright g=mid e=botleft c=botright d=bottom
    if (on & 0b0000001) hbar(y0);        // a
    if (on & 0b0100000) vbar(x0, y0, ym);// f
    if (on & 0b0000010) vbar(x1, y0, ym);// b
    if (on & 0b1000000) hbar(ym);        // g
    if (on & 0b0010000) vbar(x0, ym, y1);// e
    if (on & 0b0000100) vbar(x1, ym, y1);// c
    if (on & 0b0001000) hbar(y1);        // d
}

// A 3-digit LED panel (mine counter / timer), clamped and sign-aware.
void drawCounter(QPainter &p, const QRectF &r, int value)
{
    bevel(p, r, false, 1.5);
    QRectF inner = r.adjusted(3, 3, -3, -3);
    p.fillRect(inner, Qt::black);
    const bool neg = value < 0;
    int v = std::clamp(std::abs(value), 0, neg ? 99 : 999);
    int d[3] = {(v / 100) % 10, (v / 10) % 10, v % 10};
    const qreal dw = inner.width() / 3.0;
    for (int i = 0; i < 3; ++i) {
        int digit = d[i];
        if (neg && i == 0)
            digit = -1; // leading minus
        QRectF dr(inner.left() + i * dw, inner.top(), dw, inner.height());
        drawDigit(p, dr.adjusted(dw * 0.10, 2, -dw * 0.10, -2), digit);
    }
}

} // namespace paint

// ============================================================== face button ==
// The reset button with its little mood: happy while you play, tense on a
// press, cool on a win, K.O. on a loss. Original art, no assets.
class FaceButton : public QWidget {
    Q_OBJECT
public:
    enum Mood { Happy, Tense, Won, Lost };
    explicit FaceButton(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(34, 34);
        setCursor(Qt::PointingHandCursor);
    }
    void setMood(Mood m)
    {
        if (m_mood != m) {
            m_mood = m;
            update();
        }
    }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *) override
    {
        m_down = true;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        m_down = false;
        update();
        if (rect().contains(e->pos()))
            emit clicked();
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        paint::bevel(p, QRectF(rect()), !m_down, 2.0);
        const QRectF f = QRectF(rect()).adjusted(6, 6, -6, -6);
        p.setBrush(QColor(0xFF, 0xE0, 0x4A));
        p.setPen(QPen(QColor(0x30, 0x30, 0x00), 1.4));
        p.drawEllipse(f);
        p.setBrush(Qt::black);
        const qreal ex = f.width() * 0.20, ey = f.height() * 0.16;
        const QPointF le = f.center() + QPointF(-f.width() * 0.20, -ey);
        const QPointF re = f.center() + QPointF(f.width() * 0.20, -ey);
        if (m_mood == Won) {
            // sunglasses
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRectF(le.x() - ex, le.y() - ey * 0.4, ex * 1.8,
                                     ey * 1.4), 2, 2);
            p.drawRoundedRect(QRectF(re.x() - ex * 0.8, re.y() - ey * 0.4,
                                     ex * 1.8, ey * 1.4), 2, 2);
            p.setPen(QPen(Qt::black, 1.6));
            p.drawLine(le, re);
        } else if (m_mood == Lost) {
            p.setPen(QPen(Qt::black, 1.8));
            auto cross = [&](QPointF c) {
                p.drawLine(c + QPointF(-ex, -ey), c + QPointF(ex, ey));
                p.drawLine(c + QPointF(ex, -ey), c + QPointF(-ex, ey));
            };
            cross(le);
            cross(re);
        } else {
            p.setPen(Qt::NoPen);
            p.drawEllipse(le, ex * 0.6, ey);
            p.drawEllipse(re, ex * 0.6, ey);
        }
        // mouth
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::black, 1.6));
        const QPointF mc = f.center() + QPointF(0, f.height() * 0.22);
        const qreal mw = f.width() * 0.34;
        if (m_mood == Lost) {
            QRectF mr(mc.x() - mw, mc.y(), mw * 2, f.height() * 0.22);
            p.drawArc(mr, 0, 180 * 16); // frown
        } else if (m_mood == Tense) {
            p.drawEllipse(mc, mw * 0.5, f.height() * 0.11); // "oh"
        } else {
            QRectF mr(mc.x() - mw, mc.y() - f.height() * 0.18, mw * 2,
                      f.height() * 0.22);
            p.drawArc(mr, 180 * 16, 180 * 16); // smile
        }
    }

private:
    Mood m_mood = Happy;
    bool m_down = false;
};

// ================================================================ minefield ==
class MineField : public QWidget {
    Q_OBJECT
public:
    MineField(int cols, int rows, int mines, uint32_t seed, QWidget *parent)
        : QWidget(parent), m_board(cols, rows, mines, seed), m_seed(seed)
    {
        setMouseTracking(false);
        setFixedSize(cols * kCell + 2 * kBorder, rows * kCell + 2 * kBorder);
    }

    game::Board &board() { return m_board; }
    static constexpr int kCell = 24;
    static constexpr int kBorder = 6;

signals:
    void changed();     // counters/face should refresh
    void firstMove();   // start the clock

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (m_board.state() == game::State::Won
            || m_board.state() == game::State::Lost)
            return;
        if (e->button() == Qt::LeftButton)
            m_tense = true;
        emit changed();
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        m_tense = false;
        int c, r;
        if (!cellAt(e->pos(), &c, &r)) {
            emit changed();
            return;
        }
        const bool wasReady = m_board.state() == game::State::Ready;
        const bool both = e->buttons() != Qt::NoButton; // one still held
        if (e->button() == Qt::RightButton && !both) {
            m_board.toggleFlag(c, r);
        } else if (e->button() == Qt::MiddleButton) {
            m_board.chord(c, r);
        } else if (e->button() == Qt::LeftButton) {
            m_board.reveal(c, r);
        }
        if (wasReady && m_board.state() == game::State::Playing)
            emit firstMove();
        update();
        emit changed();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        paint::bevel(p, QRectF(rect()), false, kBorder);
        for (int r = 0; r < m_board.rows(); ++r)
            for (int c = 0; c < m_board.cols(); ++c)
                drawCell(p, c, r);
    }

private:
    bool cellAt(const QPoint &pos, int *c, int *r) const
    {
        const int x = pos.x() - kBorder, y = pos.y() - kBorder;
        if (x < 0 || y < 0)
            return false;
        *c = x / kCell;
        *r = y / kCell;
        return m_board.inBounds(*c, *r);
    }

    void drawCell(QPainter &p, int c, int r)
    {
        const QRectF cell(kBorder + c * kCell, kBorder + r * kCell, kCell,
                          kCell);
        const game::Cell &k = m_board.at(c, r);
        const bool lost = m_board.state() == game::State::Lost;
        if (!k.revealed) {
            paint::bevel(p, cell, true, 2.4);
            if (k.flagged)
                paint::drawFlag(p, cell.adjusted(4, 4, -4, -4));
            // A wrongly-flagged cell is crossed out once the game is lost.
            if (lost && k.flagged && !k.mine) {
                p.setPen(QPen(QColor(0x7B, 0, 0), 1.8));
                p.drawLine(cell.topLeft() + QPointF(4, 4),
                           cell.bottomRight() - QPointF(4, 4));
            }
            return;
        }
        // revealed: flat sunken tile
        p.fillRect(cell, QColor(0xC0, 0xC0, 0xC0));
        p.setPen(QColor(0x80, 0x80, 0x80));
        p.drawLine(cell.topLeft(), cell.topRight());
        p.drawLine(cell.topLeft(), cell.bottomLeft());
        if (k.mine) {
            if (m_board.boomC() == c && m_board.boomR() == r)
                p.fillRect(cell, QColor(0xE0, 0x30, 0x30)); // the fatal one
            paint::drawMine(p, cell.adjusted(3, 3, -3, -3));
        } else if (k.adj > 0) {
            p.setPen(paint::numberColor(k.adj));
            QFont f = p.font();
            f.setBold(true);
            f.setPixelSize(int(kCell * 0.62));
            p.setFont(f);
            p.drawText(cell, Qt::AlignCenter, QString::number(k.adj));
        }
    }

    game::Board m_board;
    uint32_t m_seed;
    bool m_tense = false;

public:
    bool tense() const { return m_tense; }
};

// =================================================================== window ==
struct Level {
    const char *name;
    int cols, rows, mines;
};
static const Level kLevels[] = {
    {"principiante", 9, 9, 10},
    {"intermedio", 16, 16, 40},
    {"experto", 30, 16, 99},
};

class Buscaminas : public QMainWindow {
    Q_OBJECT
public:
    Buscaminas(const Level &lvl, const QColor &accent, uint32_t seed)
        : m_accent(accent), m_seed(seed), m_level(lvl)
    {
        setWindowTitle(QStringLiteral("Buscaminas — Castalia"));
        buildMenu();
        buildBoard();
        m_clock = new QTimer(this);
        m_clock->setInterval(1000);
        connect(m_clock, &QTimer::timeout, this, [this]() {
            if (m_secs < 999) {
                ++m_secs;
                refresh();
            }
        });
    }

    MineField *field() { return m_field; }
    // Repaint the face/counters after the board was changed out-of-band (--demo).
    void sync() { refresh(); }

private:
    void buildMenu()
    {
        auto *game = menuBar()->addMenu(QStringLiteral("&Juego"));
        auto *nueva = game->addAction(QStringLiteral("Nueva partida"), this,
                                      [this]() { newGame(); });
        nueva->setShortcut(QKeySequence(Qt::Key_F2));
        game->addSeparator();
        for (const Level &l : kLevels) {
            const QString label = QString::fromUtf8(l.name);
            auto *a = game->addAction(
                label.left(1).toUpper() + label.mid(1)
                    + QStringLiteral("  (%1×%2, %3)")
                          .arg(l.cols).arg(l.rows).arg(l.mines),
                [this, l]() {
                    m_level = l;
                    newGame();
                });
            a->setCheckable(false);
        }
        game->addSeparator();
        auto *quit = game->addAction(QStringLiteral("Salir"), this,
                                     [this]() { close(); });
        quit->setShortcut(QKeySequence::Quit);

        auto *help = menuBar()->addMenu(QStringLiteral("A&yuda"));
        help->addAction(QStringLiteral("Cómo se juega"), this, [this]() {
            QMessageBox::information(
                this, QStringLiteral("Cómo se juega"),
                QStringLiteral(
                    "Descubre todas las casillas sin mina.\n\n"
                    "• Clic izquierdo: destapar una casilla.\n"
                    "• Clic derecho: poner o quitar una bandera.\n"
                    "• Los números indican cuántas minas tocan la casilla.\n"
                    "• Clic sobre un número con sus banderas puestas: destapa "
                    "el resto.\n\nLa primera casilla siempre es segura."));
        });
        help->addAction(QStringLiteral("Acerca de Buscaminas"), this, [this]() {
            QMessageBox::about(
                this, QStringLiteral("Acerca de Buscaminas"),
                QStringLiteral(
                    "Buscaminas de Castalia OS.\n\nUna versión propia y "
                    "original del clásico juego de lógica: reglas de dominio "
                    "público, todo dibujado con Qt, sin recursos de terceros."));
        });
    }

    void buildBoard()
    {
        auto *central = new QWidget(this);
        central->setObjectName(QStringLiteral("BuscaBg"));
        central->setStyleSheet(QStringLiteral(
            "#BuscaBg{background:#C0C0C0;}"));
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(10, 10, 10, 10);
        root->setSpacing(8);

        // Status panel: mine counter | face | timer, on a sunken bevel with a
        // thin themed accent frame so it belongs to the active Castalia theme.
        auto *panel = new QWidget(central);
        panel->setObjectName(QStringLiteral("BuscaPanel"));
        panel->setStyleSheet(QStringLiteral(
            "#BuscaPanel{background:#C0C0C0;border:2px solid %1;"
            "border-radius:3px;}")
            .arg(m_accent.darker(115).name()));
        auto *pl = new QHBoxLayout(panel);
        pl->setContentsMargins(10, 6, 10, 6);
        m_mineCounter = new CounterLabel(panel);
        m_face = new FaceButton(panel);
        m_timerCounter = new CounterLabel(panel);
        connect(m_face, &FaceButton::clicked, this, [this]() { newGame(); });
        pl->addWidget(m_mineCounter);
        pl->addStretch(1);
        pl->addWidget(m_face);
        pl->addStretch(1);
        pl->addWidget(m_timerCounter);
        root->addWidget(panel);

        m_fieldWrap = new QHBoxLayout;
        m_fieldWrap->addStretch(1);
        installField(central);
        m_fieldWrap->addStretch(1);
        root->addLayout(m_fieldWrap);

        setCentralWidget(central);
        refresh();
    }

    // Create the minefield for the current level/seed and wire its signals.
    void installField(QWidget *parent)
    {
        m_field = new MineField(m_level.cols, m_level.rows, m_level.mines,
                                m_seed, parent);
        connect(m_field, &MineField::changed, this, &Buscaminas::refresh);
        connect(m_field, &MineField::firstMove, this,
                [this]() { m_clock->start(); });
        m_fieldWrap->insertWidget(1, m_field);
    }

    void newGame()
    {
        m_clock->stop();
        m_secs = 0;
        // A fresh non-repeating seed each round while play is real; --seed only
        // fixes the very first board (for reproducible shots/tests).
        m_seed = m_seed * 1664525u + 1013904223u;
        delete m_fieldWrap->takeAt(1)->widget(); // remove old field
        installField(centralWidget());
        adjustSize();
        setFixedSize(sizeHint());
        refresh();
    }

    void refresh()
    {
        using S = game::State;
        const S st = m_field->board().state();
        if (st == S::Won || st == S::Lost)
            m_clock->stop();
        FaceButton::Mood mood = FaceButton::Happy;
        if (st == S::Won)
            mood = FaceButton::Won;
        else if (st == S::Lost)
            mood = FaceButton::Lost;
        else if (m_field->tense())
            mood = FaceButton::Tense;
        m_face->setMood(mood);
        m_mineCounter->setValue(m_field->board().minesRemaining());
        m_timerCounter->setValue(m_secs);
    }

    // A fixed-size widget wrapping the LED counter painter.
    class CounterLabel : public QWidget {
    public:
        explicit CounterLabel(QWidget *parent) : QWidget(parent)
        {
            setFixedSize(52, 30);
        }
        void setValue(int v)
        {
            if (v != m_v) {
                m_v = v;
                update();
            }
        }

    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter p(this);
            paint::drawCounter(p, QRectF(rect()), m_v);
        }

    private:
        int m_v = 0;
    };

    QColor m_accent;
    uint32_t m_seed;
    Level m_level;
    MineField *m_field = nullptr;
    QHBoxLayout *m_fieldWrap = nullptr;
    FaceButton *m_face = nullptr;
    CounterLabel *m_mineCounter = nullptr;
    CounterLabel *m_timerCounter = nullptr;
    QTimer *m_clock = nullptr;
    int m_secs = 0;
};

// ================================================================= selftest ==
// A head-less check of the model invariants — a deterministic gate the CI can
// run without a display (Bible §17.4 / §19.3).
static int selftest()
{
    using namespace game;
    int fail = 0;
    auto check = [&](bool ok, const char *what) {
        if (!ok) {
            fail++;
            qWarning("selftest FAIL: %s", what);
        }
    };

    // 1) First click is always safe and starts play.
    {
        Board b(9, 9, 10, 42);
        b.reveal(4, 4);
        check(!b.at(4, 4).mine, "first click not a mine");
        check(b.at(4, 4).revealed, "first click revealed");
        check(b.state() == State::Playing || b.state() == State::Won,
              "state advances on first click");
    }
    // 2) Mine count is exactly as requested and neighbour math is consistent.
    {
        Board b(16, 16, 40, 7);
        b.reveal(0, 0);
        int mines = 0, adjSum = 0, touch = 0;
        for (int r = 0; r < 16; ++r)
            for (int c = 0; c < 16; ++c) {
                if (b.at(c, r).mine)
                    ++mines;
            }
        check(mines == 40, "exactly 40 mines laid");
        // recompute adjacency independently for a revealed 0-cell region
        for (int r = 0; r < 16; ++r)
            for (int c = 0; c < 16; ++c)
                if (!b.at(c, r).mine) {
                    int n = 0;
                    for (int dr = -1; dr <= 1; ++dr)
                        for (int dc = -1; dc <= 1; ++dc) {
                            if (!dc && !dr)
                                continue;
                            if (b.inBounds(c + dc, r + dr)
                                && b.at(c + dc, r + dr).mine)
                                ++n;
                        }
                    if (b.at(c, r).adj != n)
                        ++touch;
                    adjSum += n;
                }
        check(touch == 0, "adjacency counts are correct");
        Q_UNUSED(adjSum);
    }
    // 3) Revealing all safe cells wins; hitting a mine loses.
    {
        Board b(9, 9, 10, 99);
        b.revealAllSafe();
        check(b.state() == State::Won, "reveal-all-safe wins");
    }
    {
        Board b(9, 9, 10, 5);
        b.reveal(4, 4);
        // find a mine and step on it
        bool stepped = false;
        for (int r = 0; r < 9 && !stepped; ++r)
            for (int c = 0; c < 9 && !stepped; ++c)
                if (b.at(c, r).mine) {
                    b.reveal(c, r);
                    stepped = true;
                }
        check(b.state() == State::Lost, "stepping on a mine loses");
    }
    // 4) Flag toggling adjusts the remaining counter.
    {
        Board b(9, 9, 10, 3);
        b.reveal(4, 4);
        const int before = b.minesRemaining();
        b.toggleFlag(0, 0);
        check(b.minesRemaining() == before - 1, "flag decrements counter");
        b.toggleFlag(0, 0);
        check(b.minesRemaining() == before, "unflag restores counter");
    }

    if (fail == 0)
        qInfo("buscaminas selftest: all checks passed");
    return fail == 0 ? 0 : 1;
}

// ===================================================================== main ==
int main(int argc, char **argv)
{
    // --selftest exercises the pure model with no display, so handle it before
    // a QApplication (and its platform plugin) is ever needed.
    for (int i = 1; i < argc; ++i)
        if (QLatin1String(argv[i]) == QLatin1String("--selftest"))
            return selftest();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-buscaminas"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("level"),
                   QStringLiteral("principiante|intermedio|experto"),
                   QStringLiteral("name"), QStringLiteral("principiante")});
    cli.addOption({QStringLiteral("seed"),
                   QStringLiteral("Fixed mine layout seed"),
                   QStringLiteral("n"), QStringLiteral("0")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Reveal a played board for a screenshot")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.addOption({QStringLiteral("selftest"),
                   QStringLiteral("Run head-less model checks and exit")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo"))).absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const QString accentStr =
        ThemeTokens::load(castalia::themeConfPath(repo, themeId))
            .str(QStringLiteral("colors"), QStringLiteral("accent"));
    castalia::applyTheme(&app, repo, themeId);
    const QColor accent(accentStr.isEmpty() ? QStringLiteral("#3E82B6")
                                            : accentStr);

    Level lvl = kLevels[0];
    const QString ln = cli.value(QStringLiteral("level"));
    for (const Level &l : kLevels)
        if (ln == QLatin1String(l.name))
            lvl = l;

    bool seedOk = false;
    uint32_t seed = cli.value(QStringLiteral("seed")).toUInt(&seedOk);
    const bool demo = cli.isSet(QStringLiteral("demo"));
    if (demo && !seedOk)
        seed = 12345; // a pleasant, reproducible demo layout

    Buscaminas w(lvl, accent, seed);

    if (demo) {
        // Build a mid-game picture: open a safe region, flag a few real mines,
        // then step on one so numbers, flags and mines all show at once.
        game::Board &b = w.field()->board();
        b.reveal(lvl.cols / 2, lvl.rows / 2);
        b.flagSomeMines(3);
        for (int r = 0; r < b.rows(); ++r)
            for (int c = 0; c < b.cols(); ++c)
                if (b.at(c, r).mine && !b.at(c, r).flagged) {
                    b.reveal(c, r);
                    r = b.rows();
                    break;
                }
        w.field()->update();
        w.sync();
    }

    w.show();

    const QString out = cli.value(QStringLiteral("screenshot"));
    if (!out.isEmpty())
        QTimer::singleShot(300, &app, [&]() {
            w.grab().save(out);
            app.quit();
        });
    return app.exec();
}

#include "main.moc"
