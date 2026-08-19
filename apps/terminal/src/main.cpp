// castalia-terminal — an ORIGINAL terminal emulator (Bible §9.3).
//
// Written from scratch: a real PTY (forkpty) runs the login shell, and a
// hand-written VT100/ANSI parser drives a cell-grid model that QWidget paints.
// No qtermwidget, no VTE — just Qt5 + POSIX. It handles UTF-8, 16/256/24-bit
// colour, bold/underline/inverse, cursor movement, erase, scroll regions,
// insert/delete, an alternate screen (so vim/less/htop behave), scrollback,
// and OSC window titles. Themed via libcastalia-ui (default fg/bg from tokens).
//
// Usage: castalia-terminal --theme classic [--repo P] [--run "cmd"]
//        [--screenshot out.png]

#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QDir>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QSocketNotifier>
#include <QTimer>
#include <QVector>
#include <QWheelEvent>
#include <QWidget>

#include <cstdlib>
#include <cstring>

#include <pty.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "Theme.h"

namespace {
constexpr int kDefault = -1;  // "use the theme default" fg/bg sentinel

// 256-colour xterm palette resolved to 0xRRGGBB.
int palette256(int i)
{
    static const int base16[16] = {
        0x111318, 0xB3372E, 0x6E9E3A, 0xC9A227, 0x3E82B6, 0x8E4A9E,
        0x2E8B8B, 0xC9C5BE, 0x5A564E, 0xE8887E, 0x9FD06A, 0xF3E09A,
        0x7FB0D4, 0xCBA6D6, 0x86C7C7, 0xFFFFFF};
    if (i < 16)
        return base16[i];
    if (i < 232) {
        i -= 16;
        const int r = i / 36, g = (i / 6) % 6, b = i % 6;
        auto c = [](int v) { return v ? v * 40 + 55 : 0; };
        return (c(r) << 16) | (c(g) << 8) | c(b);
    }
    const int v = (i - 232) * 10 + 8;
    return (v << 16) | (v << 8) | v;
}
} // namespace

struct Cell {
    uint charUcs = ' ';   // UCS-4 code point
    int fg = kDefault;
    int bg = kDefault;
    quint8 attrs = 0;     // 1 bold, 2 underline, 4 inverse
};
enum { A_BOLD = 1, A_UNDER = 2, A_INV = 4 };
using Line = QVector<Cell>;

class Terminal : public QWidget {
public:
    Terminal(const QColor &fg, const QColor &bg, QWidget *parent = nullptr)
        : QWidget(parent), m_fg(fg), m_bg(bg)
    {
        setFocusPolicy(Qt::StrongFocus);
        QFont f(QStringLiteral("DejaVu Sans Mono"));
        f.setStyleHint(QFont::Monospace);
        f.setPixelSize(15);
        setFont(f);
        QFontMetricsF fm(f);
        m_cw = fm.horizontalAdvance(QLatin1Char('M'));
        m_ch = fm.height();
        m_ascent = fm.ascent();
        resize(int(m_cw * 80) + 8, int(m_ch * 24) + 8);
        reshape(80, 24);

        m_blink = new QTimer(this);
        connect(m_blink, &QTimer::timeout, this, [this]() {
            m_cursorOn = !m_cursorOn;
            update();
        });
        m_blink->start(530);
    }

    void startShell()
    {
        struct winsize ws = {quint16(m_rows), quint16(m_cols), 0, 0};
        m_pid = forkpty(&m_master, nullptr, nullptr, &ws);
        if (m_pid == 0) {
            setenv("TERM", "xterm-256color", 1);
            setenv("COLORTERM", "truecolor", 1);
            const char *sh = getenv("SHELL");
            if (!sh || !*sh)
                sh = "/bin/bash";
            execlp(sh, sh, "-l", static_cast<char *>(nullptr));
            _exit(127);
        }
        if (m_pid < 0)
            return;
        m_notifier = new QSocketNotifier(m_master, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this,
                [this]() { readPty(); });
    }

    // Type a command into the shell (used by --run to prove the pipeline).
    void feed(const QString &cmd)
    {
        const QByteArray b = (cmd + QLatin1Char('\n')).toUtf8();
        if (m_master >= 0)
            (void)!::write(m_master, b.constData(), b.size());
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        const int cols = qMax(2, int((width() - 8) / m_cw));
        const int rows = qMax(2, int((height() - 8) / m_ch));
        if (cols == m_cols && rows == m_rows)
            return;
        reshape(cols, rows);
        if (m_master >= 0) {
            struct winsize ws = {quint16(rows), quint16(cols), 0, 0};
            ioctl(m_master, TIOCSWINSZ, &ws);
        }
    }

    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->modifiers().testFlag(Qt::ControlModifier)
            && e->modifiers().testFlag(Qt::ShiftModifier)) {
            if (e->key() == Qt::Key_C) {
                QApplication::clipboard()->setText(selectedText());
                return;
            }
            if (e->key() == Qt::Key_V) {
                writePty(QApplication::clipboard()->text().toUtf8());
                return;
            }
        }
        QByteArray out;
        switch (e->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter: out = "\r"; break;
        case Qt::Key_Backspace: out = "\x7f"; break;
        case Qt::Key_Tab: out = "\t"; break;
        case Qt::Key_Escape: out = "\x1b"; break;
        case Qt::Key_Up: out = "\x1b[A"; break;
        case Qt::Key_Down: out = "\x1b[B"; break;
        case Qt::Key_Right: out = "\x1b[C"; break;
        case Qt::Key_Left: out = "\x1b[D"; break;
        case Qt::Key_Home: out = "\x1b[H"; break;
        case Qt::Key_End: out = "\x1b[F"; break;
        case Qt::Key_PageUp: out = "\x1b[5~"; break;
        case Qt::Key_PageDown: out = "\x1b[6~"; break;
        case Qt::Key_Delete: out = "\x1b[3~"; break;
        default:
            if (e->modifiers().testFlag(Qt::ControlModifier)
                && !e->text().isEmpty()) {
                const char c = e->text().at(0).toUpper().toLatin1();
                if (c >= '@' && c <= '_')
                    out = QByteArray(1, char(c - '@'));
                else if (c == ' ')
                    out = QByteArray(1, '\0');
            }
            if (out.isEmpty() && !e->text().isEmpty())
                out = e->text().toUtf8();
        }
        if (!out.isEmpty()) {
            m_scroll = 0;  // any key jumps to the live view
            writePty(out);
        }
    }

    void wheelEvent(QWheelEvent *e) override
    {
        const int step = e->angleDelta().y() > 0 ? 3 : -3;
        m_scroll = qBound(0, m_scroll + step, m_scrollback.size());
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), m_bg);
        p.setFont(font());

        // Compose the visible window: tail of scrollback (per m_scroll) then
        // the live grid.
        const int total = m_scrollback.size() + m_rows;
        const int firstVis = qMax(0, total - m_rows - m_scroll);
        for (int r = 0; r < m_rows; ++r) {
            const int gi = firstVis + r;
            const Line *ln = nullptr;
            if (gi < m_scrollback.size())
                ln = &m_scrollback[gi];
            else if (gi - m_scrollback.size() < m_grid.size())
                ln = &m_grid[gi - m_scrollback.size()];
            if (!ln)
                continue;
            const qreal y = 4 + r * m_ch;
            for (int c = 0; c < ln->size() && c < m_cols; ++c) {
                const Cell &cell = ln->at(c);
                drawCell(p, c, y, cell);
            }
        }
        // cursor (only in the live view)
        if (m_cursorVisible && m_scroll == 0 && m_cursorOn && hasFocus()) {
            const qreal x = 4 + m_cx * m_cw;
            const qreal y = 4 + m_cy * m_ch;
            p.fillRect(QRectF(x, y, m_cw, m_ch),
                       QColor(m_fg.red(), m_fg.green(), m_fg.blue(), 180));
        }
    }

private:
    void drawCell(QPainter &p, int c, qreal y, const Cell &cell)
    {
        int fgv = cell.fg, bgv = cell.bg;
        QColor fg = fgv == kDefault ? m_fg : QColor(fgv >> 16 & 255,
                                                    fgv >> 8 & 255, fgv & 255);
        QColor bg = bgv == kDefault ? m_bg : QColor(bgv >> 16 & 255,
                                                    bgv >> 8 & 255, bgv & 255);
        if (cell.attrs & A_INV)
            std::swap(fg, bg);
        const qreal x = 4 + c * m_cw;
        if (bg != m_bg)
            p.fillRect(QRectF(x, y, m_cw + 0.5, m_ch), bg);
        if (cell.charUcs == ' ' && !(cell.attrs & A_UNDER))
            return;
        QFont f = font();
        f.setBold(cell.attrs & A_BOLD);
        f.setUnderline(cell.attrs & A_UNDER);
        p.setFont(f);
        p.setPen(fg);
        p.drawText(QPointF(x, y + m_ascent),
                   QString(QChar(cell.charUcs)));
    }

    QString selectedText() const { return QString(); }  // 1.0: mouse selection

    void reshape(int cols, int rows)
    {
        m_cols = cols;
        m_rows = rows;
        m_scrollTop = 0;
        m_scrollBot = rows - 1;
        auto fresh = [&](QVector<Line> &g) {
            g.clear();
            for (int r = 0; r < rows; ++r)
                g.push_back(Line(cols));
        };
        fresh(m_grid);
        fresh(m_alt);
        m_cx = qMin(m_cx, cols - 1);
        m_cy = qMin(m_cy, rows - 1);
        update();
    }

    void writePty(const QByteArray &b)
    {
        if (m_master >= 0)
            (void)!::write(m_master, b.constData(), b.size());
    }

    void readPty()
    {
        char buf[8192];
        const ssize_t n = ::read(m_master, buf, sizeof(buf));
        if (n <= 0) {  // shell exited
            if (m_notifier)
                m_notifier->setEnabled(false);
            setWindowTitle(QStringLiteral("Terminal — [proceso terminado]"));
            return;
        }
        for (ssize_t i = 0; i < n; ++i)
            consume(static_cast<unsigned char>(buf[i]));
        update();
    }

    Line &curGrid(int r) { return m_grid[r]; }

    void putUcs(uint u)
    {
        if (m_cx >= m_cols) {
            m_cx = 0;
            lineFeed();
        }
        Cell &cell = m_grid[m_cy][m_cx];
        cell.charUcs = u;
        cell.fg = m_fg_sgr;
        cell.bg = m_bg_sgr;
        cell.attrs = m_attrs;
        ++m_cx;
    }

    void lineFeed()
    {
        if (m_cy == m_scrollBot)
            scrollUp(1);
        else
            ++m_cy;
    }
    void reverseIndex()
    {
        if (m_cy == m_scrollTop)
            scrollDown(1);
        else
            --m_cy;
    }

    void scrollUp(int n)
    {
        for (int k = 0; k < n; ++k) {
            if (m_scrollTop == 0 && !m_altActive) {
                m_scrollback.push_back(m_grid[m_scrollTop]);
                if (m_scrollback.size() > 4000)
                    m_scrollback.removeFirst();
            }
            m_grid.remove(m_scrollTop);
            m_grid.insert(m_scrollBot, Line(m_cols));
        }
    }
    void scrollDown(int n)
    {
        for (int k = 0; k < n; ++k) {
            m_grid.remove(m_scrollBot);
            m_grid.insert(m_scrollTop, Line(m_cols));
        }
    }

    void eraseInLine(int mode)
    {
        Line &ln = m_grid[m_cy];
        const int from = (mode == 1) ? 0 : m_cx;
        const int to = (mode == 0) ? m_cols - 1 : (mode == 1 ? m_cx : m_cols - 1);
        for (int c = from; c <= to && c < m_cols; ++c)
            ln[c] = blank();
    }
    void eraseInDisplay(int mode)
    {
        if (mode == 2 || mode == 3) {
            for (int r = 0; r < m_rows; ++r)
                m_grid[r] = Line(m_cols);
            if (mode == 3)
                m_scrollback.clear();
            return;
        }
        const int r0 = (mode == 1) ? 0 : m_cy;
        const int r1 = (mode == 0) ? m_rows - 1 : m_cy;
        for (int r = r0; r <= r1; ++r) {
            if (r == m_cy)
                eraseInLine(mode);
            else
                m_grid[r] = Line(m_cols);
        }
    }
    Cell blank() const
    {
        Cell c;
        c.bg = m_bg_sgr;
        return c;
    }

    // ---- the VT parser -----------------------------------------------------
    void consume(unsigned char b)
    {
        switch (m_state) {
        case Normal: normalByte(b); break;
        case Esc: escByte(b); break;
        case Csi: csiByte(b); break;
        case Osc: oscByte(b); break;
        case Charset: m_state = Normal; break;  // consume + ignore
        }
    }

    void normalByte(unsigned char b)
    {
        if (m_utf8Left > 0) {
            m_utf8 = (m_utf8 << 6) | (b & 0x3F);
            if (--m_utf8Left == 0)
                putUcs(m_utf8);
            return;
        }
        if (b == 0x1b) { m_state = Esc; return; }
        if (b == 0x0d) { m_cx = 0; return; }
        if (b == 0x0a || b == 0x0b || b == 0x0c) { lineFeed(); return; }
        if (b == 0x08) { if (m_cx > 0) --m_cx; return; }
        if (b == 0x09) { m_cx = qMin(m_cols - 1, (m_cx / 8 + 1) * 8); return; }
        if (b == 0x07) return;  // bell
        if (b < 0x20) return;
        if (b < 0x80) { putUcs(b); return; }
        // UTF-8 lead byte
        if ((b & 0xE0) == 0xC0) { m_utf8 = b & 0x1F; m_utf8Left = 1; }
        else if ((b & 0xF0) == 0xE0) { m_utf8 = b & 0x0F; m_utf8Left = 2; }
        else if ((b & 0xF8) == 0xF0) { m_utf8 = b & 0x07; m_utf8Left = 3; }
        else putUcs('?');
    }

    void escByte(unsigned char b)
    {
        m_state = Normal;
        switch (b) {
        case '[': m_state = Csi; m_params.clear(); m_priv = false;
            m_pcur.clear(); break;
        case ']': m_state = Osc; m_osc.clear(); break;
        case '(': case ')': case '*': case '+': m_state = Charset; break;
        case '7': m_sx = m_cx; m_sy = m_cy; m_sAttrs = m_attrs;
            m_sFg = m_fg_sgr; m_sBg = m_bg_sgr; break;
        case '8': m_cx = m_sx; m_cy = m_sy; m_attrs = m_sAttrs;
            m_fg_sgr = m_sFg; m_bg_sgr = m_sBg; break;
        case 'M': reverseIndex(); break;
        case 'D': lineFeed(); break;
        case 'E': m_cx = 0; lineFeed(); break;
        case 'c': hardReset(); break;
        default: break;
        }
    }

    void csiByte(unsigned char b)
    {
        if (b == '?') { m_priv = true; return; }
        if (b >= '0' && b <= '9') { m_pcur.append(char(b)); return; }
        if (b == ';') { pushParam(); return; }
        if (b == '>' || b == '=' || b == '!') return;  // ignore intermediates
        pushParam();
        dispatchCsi(b);
        m_state = Normal;
    }
    void pushParam()
    {
        m_params.push_back(m_pcur.isEmpty() ? -1 : m_pcur.toInt());
        m_pcur.clear();
    }
    int param(int i, int def) const
    {
        return (i < m_params.size() && m_params[i] > 0) ? m_params[i] : def;
    }

    void dispatchCsi(unsigned char f)
    {
        switch (f) {
        case 'A': m_cy = qMax(m_scrollTop, m_cy - param(0, 1)); break;
        case 'B': m_cy = qMin(m_scrollBot, m_cy + param(0, 1)); break;
        case 'C': m_cx = qMin(m_cols - 1, m_cx + param(0, 1)); break;
        case 'D': m_cx = qMax(0, m_cx - param(0, 1)); break;
        case 'E': m_cx = 0; m_cy = qMin(m_scrollBot, m_cy + param(0, 1)); break;
        case 'F': m_cx = 0; m_cy = qMax(m_scrollTop, m_cy - param(0, 1)); break;
        case 'G': m_cx = qBound(0, param(0, 1) - 1, m_cols - 1); break;
        case 'd': m_cy = qBound(0, param(0, 1) - 1, m_rows - 1); break;
        case 'H': case 'f':
            m_cy = qBound(0, param(0, 1) - 1, m_rows - 1);
            m_cx = qBound(0, param(1, 1) - 1, m_cols - 1);
            break;
        case 'J': eraseInDisplay(param(0, 0)); break;
        case 'K': eraseInLine(param(0, 0)); break;
        case 'L': insertLines(param(0, 1)); break;
        case 'M': deleteLines(param(0, 1)); break;
        case '@': insertChars(param(0, 1)); break;
        case 'P': deleteChars(param(0, 1)); break;
        case 'X': eraseChars(param(0, 1)); break;
        case 'S': scrollUp(param(0, 1)); break;
        case 'T': scrollDown(param(0, 1)); break;
        case 'r':
            m_scrollTop = qBound(0, param(0, 1) - 1, m_rows - 1);
            m_scrollBot = qBound(0, param(1, m_rows) - 1, m_rows - 1);
            m_cx = 0; m_cy = m_scrollTop;
            break;
        case 's': m_sx = m_cx; m_sy = m_cy; break;
        case 'u': m_cx = m_sx; m_cy = m_sy; break;
        case 'm': applySgr(); break;
        case 'h': case 'l': setMode(f == 'h'); break;
        default: break;
        }
    }

    void insertLines(int n)
    {
        if (m_cy < m_scrollTop || m_cy > m_scrollBot)
            return;
        for (int k = 0; k < n; ++k) {
            m_grid.remove(m_scrollBot);
            m_grid.insert(m_cy, Line(m_cols));
        }
    }
    void deleteLines(int n)
    {
        if (m_cy < m_scrollTop || m_cy > m_scrollBot)
            return;
        for (int k = 0; k < n; ++k) {
            m_grid.remove(m_cy);
            m_grid.insert(m_scrollBot, Line(m_cols));
        }
    }
    void insertChars(int n)
    {
        Line &ln = m_grid[m_cy];
        for (int k = 0; k < n; ++k) {
            ln.insert(m_cx, blank());
            if (ln.size() > m_cols)
                ln.removeLast();
        }
    }
    void deleteChars(int n)
    {
        Line &ln = m_grid[m_cy];
        for (int k = 0; k < n && m_cx < ln.size(); ++k) {
            ln.remove(m_cx);
            ln.push_back(blank());
        }
    }
    void eraseChars(int n)
    {
        for (int c = m_cx; c < m_cx + n && c < m_cols; ++c)
            m_grid[m_cy][c] = blank();
    }

    void setMode(bool set)
    {
        if (!m_priv)
            return;
        for (int p : m_params) {
            if (p == 25)
                m_cursorVisible = set;
            else if (p == 1049 || p == 1047 || p == 47)
                setAltScreen(set);
        }
    }
    void setAltScreen(bool on)
    {
        if (on == m_altActive)
            return;
        m_altActive = on;
        std::swap(m_grid, m_alt);
        if (on) {
            for (int r = 0; r < m_rows; ++r)
                m_grid[r] = Line(m_cols);
            m_cx = 0; m_cy = 0;
        }
    }

    void applySgr()
    {
        if (m_params.isEmpty())
            m_params.push_back(0);
        for (int i = 0; i < m_params.size(); ++i) {
            int p = m_params[i] < 0 ? 0 : m_params[i];
            if (p == 0) { m_attrs = 0; m_fg_sgr = kDefault; m_bg_sgr = kDefault; }
            else if (p == 1) m_attrs |= A_BOLD;
            else if (p == 4) m_attrs |= A_UNDER;
            else if (p == 7) m_attrs |= A_INV;
            else if (p == 22) m_attrs &= ~A_BOLD;
            else if (p == 24) m_attrs &= ~A_UNDER;
            else if (p == 27) m_attrs &= ~A_INV;
            else if (p >= 30 && p <= 37) m_fg_sgr = palette256(p - 30);
            else if (p >= 40 && p <= 47) m_bg_sgr = palette256(p - 40);
            else if (p >= 90 && p <= 97) m_fg_sgr = palette256(p - 90 + 8);
            else if (p >= 100 && p <= 107) m_bg_sgr = palette256(p - 100 + 8);
            else if (p == 39) m_fg_sgr = kDefault;
            else if (p == 49) m_bg_sgr = kDefault;
            else if (p == 38 || p == 48) {
                int *dst = (p == 38) ? &m_fg_sgr : &m_bg_sgr;
                if (i + 1 < m_params.size() && m_params[i + 1] == 5) {
                    *dst = palette256(param(i + 2, 0));
                    i += 2;
                } else if (i + 1 < m_params.size() && m_params[i + 1] == 2) {
                    const int r = param(i + 2, 0), g = param(i + 3, 0),
                              bl = param(i + 4, 0);
                    *dst = (r << 16) | (g << 8) | bl;
                    i += 4;
                }
            }
        }
    }

    void oscByte(unsigned char b)
    {
        if (b == 0x07 || b == 0x1b) {  // BEL or ST
            const int semi = m_osc.indexOf(';');
            if (semi > 0) {
                const QString code = m_osc.left(semi);
                if (code == QLatin1String("0") || code == QLatin1String("2"))
                    setWindowTitle(m_osc.mid(semi + 1)
                                   + QStringLiteral(" — Terminal"));
            }
            m_state = Normal;
            return;
        }
        m_osc.append(QChar(b));
    }

    void hardReset()
    {
        reshape(m_cols, m_rows);
        m_cx = m_cy = 0;
        m_attrs = 0; m_fg_sgr = m_bg_sgr = kDefault;
        m_altActive = false;
        m_scrollback.clear();
    }

    // geometry / fonts
    qreal m_cw = 8, m_ch = 16, m_ascent = 12;
    int m_cols = 80, m_rows = 24;
    QColor m_fg, m_bg;

    // grids
    QVector<Line> m_grid, m_alt;
    QVector<Line> m_scrollback;
    bool m_altActive = false;
    int m_scroll = 0;  // scrollback offset (0 = live)

    // cursor + SGR state
    int m_cx = 0, m_cy = 0;
    int m_sx = 0, m_sy = 0;
    quint8 m_attrs = 0, m_sAttrs = 0;
    int m_fg_sgr = kDefault, m_bg_sgr = kDefault, m_sFg = kDefault,
        m_sBg = kDefault;
    int m_scrollTop = 0, m_scrollBot = 23;
    bool m_cursorVisible = true, m_cursorOn = true;

    // parser state
    enum State { Normal, Esc, Csi, Osc, Charset } m_state = Normal;
    QVector<int> m_params;
    QString m_pcur;
    bool m_priv = false;
    QString m_osc;
    uint m_utf8 = 0;
    int m_utf8Left = 0;

    // pty
    int m_master = -1;
    pid_t m_pid = -1;
    QSocketNotifier *m_notifier = nullptr;
    QTimer *m_blink = nullptr;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-terminal"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("run"),
                   QStringLiteral("Type a command on start (proof/demo)"),
                   QStringLiteral("cmd")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const ThemeTokens t =
        ThemeTokens::load(castalia::themeConfPath(
            repo, cli.value(QStringLiteral("theme"))));
    // Terminals read best on a deep ink background with warm-white text.
    const QColor bg = QColor(0x12, 0x16, 0x1C);
    const QColor fg = QColor(0xEC, 0xE9, 0xE4);
    castalia::applyTheme(&app, repo, cli.value(QStringLiteral("theme")));

    Terminal term(fg, bg);
    term.setWindowTitle(QStringLiteral("Terminal — Castalia"));
    term.show();
    term.startShell();

    const QString run = cli.value(QStringLiteral("run"));
    if (!run.isEmpty())
        QTimer::singleShot(350, &term, [&term, run]() { term.feed(run); });

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(run.isEmpty() ? 400 : 1300, &app, [&]() {
            term.grab().save(shot);
            app.quit();
        });
    return app.exec();
}
