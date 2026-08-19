// castalia-salir — the Castalia session/power dialog (Bible §7.6).
//
// "A single modal with big, clear, original-iconed buttons: Shut Down ·
// Restart · Log Off · Lock · Switch User (+ Suspend where the hwprobe says
// it is safe). Confirmation on destructive actions; remembers nothing
// sensitive."
//
// Until now the Castalia Menu's power row was decorative — the buttons were
// laid out but never connected to anything, so there was no way to leave the
// session from the shell. This app is the missing piece, and the panel's
// power row now opens it.
//
// The honesty rule (P10) applies to every tile: each action is *resolved*
// against the tools actually installed on the machine (elogind/systemd,
// pkill, a real screen locker, LightDM) and a tile whose action cannot be
// performed is disabled and says why, instead of pretending and failing.
//
// Every glyph is painted natively (QPainter) — no per-action icon assets.
//
// Usage: castalia-salir [--theme id] [--repo PATH] [--focus ID]
//                       [--action ID] [--print-actions] [--screenshot out.png]
//
//   --focus ID       preselect a tile (apagar|reiniciar|suspender|
//                    cerrar-sesion|bloquear|cambiar-usuario)
//   --action ID      perform the action straight away, no window (used by the
//                    panel for "Bloquear", which needs no confirmation)
//   --print-actions  print the resolved action table and exit — how the
//                    machine's real capabilities are inspected without
//                    performing anything

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QFontMetrics>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QtGlobal>

#include <csignal>

#include "Theme.h"

namespace {

// The glyph painted on a tile. Original artwork, drawn in code (§8.4).
enum class Glyph { Power, Restart, Suspend, Logout, Lock, SwitchUser };

struct Action {
    QString id;           // stable id used by --focus / --action
    QString label;        // Spanish label (§ Spanish-first)
    QString hint;         // one short line under the label
    Glyph glyph = Glyph::Power;
    bool destructive = false;   // needs a confirmation step (§7.6)
    QString confirm;            // the confirmation question
    QStringList argv;           // resolved command; empty when internal/absent
    qint64 signalPid = 0;       // >0: signal this pid instead of spawning
    bool verifyExit = false;    // run it synchronously and require exit 0
    QString why;                // why it is unavailable (empty when it is)

    bool available() const { return signalPid > 0 || !argv.isEmpty(); }
};

QString which(const QString &bin)
{
    return QStandardPaths::findExecutable(bin);
}

// The first candidate command whose binary exists, or an empty list.
QStringList firstAvailable(const QVector<QStringList> &candidates)
{
    for (const QStringList &c : candidates) {
        if (c.isEmpty())
            continue;
        if (!which(c.first()).isEmpty())
            return c;
    }
    return QStringList();
}

// Does this raw /proc/<pid>/cmdline (NUL-separated) belong to a
// castalia-session? Pure, so the self-test can check it without a session.
//
// This deliberately reads cmdline and NOT comm. The kernel truncates
// /proc/<pid>/comm to 15 bytes (TASK_COMM_LEN), and "castalia-session" is
// 16, so a running session always reports "castalia-sessio" — a comm
// comparison against the real name can never match. cmdline is not
// truncated, so it is the one place the full name survives.
bool cmdlineIsSession(const QByteArray &rawCmdline)
{
    for (const QByteArray &arg : rawCmdline.split('\0')) {
        if (arg.isEmpty())
            continue;
        if (QFileInfo(QString::fromLocal8Bit(arg)).fileName()
            == QLatin1String("castalia-session"))
            return true;
    }
    return false;
}

// The pid of the castalia-session that owns this session, when it told us.
// castalia-session exports CASTALIA_SESSION_PID; we still verify the process
// exists and really is a castalia-session before we would ever signal it, so
// a stale or hostile value can never make us kill an unrelated process.
qint64 sessionPid()
{
    bool ok = false;
    const qint64 pid = qEnvironmentVariable("CASTALIA_SESSION_PID")
                           .toLongLong(&ok);
    if (!ok || pid <= 1)
        return 0;
    QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!cmdline.open(QIODevice::ReadOnly))
        return 0;
    return cmdlineIsSession(cmdline.readAll()) ? pid : 0;
}

// Resolve every action against the tools this machine actually has (§7.6).
QVector<Action> resolveActions()
{
    QVector<Action> out;

    Action off;
    off.id = QStringLiteral("apagar");
    off.label = QCoreApplication::translate("Salir", "Apagar");
    off.hint = QCoreApplication::translate("Salir", "Cierra todo y apaga el equipo.");
    off.glyph = Glyph::Power;
    off.destructive = true;
    off.confirm = QStringLiteral(
        "¿Apagar el equipo? Se cerrarán todos los programas abiertos; "
        "guarda tu trabajo antes de continuar.");
    off.argv = firstAvailable({
        {QStringLiteral("loginctl"), QStringLiteral("poweroff")},
        {QStringLiteral("systemctl"), QStringLiteral("poweroff")},
        {QStringLiteral("poweroff")},
        {QStringLiteral("shutdown"), QStringLiteral("-h"),
         QStringLiteral("now")},
    });
    if (!off.available())
        off.why = QCoreApplication::translate("Salir", "sin orden de apagado en el sistema");
    out.append(off);

    Action reboot;
    reboot.id = QStringLiteral("reiniciar");
    reboot.label = QCoreApplication::translate("Salir", "Reiniciar");
    reboot.hint = QCoreApplication::translate("Salir", "Cierra todo y vuelve a arrancar.");
    reboot.glyph = Glyph::Restart;
    reboot.destructive = true;
    reboot.confirm = QStringLiteral(
        "¿Reiniciar el equipo? Se cerrarán todos los programas abiertos; "
        "guarda tu trabajo antes de continuar.");
    reboot.argv = firstAvailable({
        {QStringLiteral("loginctl"), QStringLiteral("reboot")},
        {QStringLiteral("systemctl"), QStringLiteral("reboot")},
        {QStringLiteral("reboot")},
        {QStringLiteral("shutdown"), QStringLiteral("-r"),
         QStringLiteral("now")},
    });
    if (!reboot.available())
        reboot.why = QCoreApplication::translate("Salir", "sin orden de reinicio en el sistema");
    out.append(reboot);

    Action suspend;
    suspend.id = QStringLiteral("suspender");
    suspend.label = QCoreApplication::translate("Salir", "Suspender");
    suspend.hint = QCoreApplication::translate("Salir", "Duerme el equipo sin cerrar la sesión.");
    suspend.glyph = Glyph::Suspend;
    suspend.argv = firstAvailable({
        {QStringLiteral("loginctl"), QStringLiteral("suspend")},
        {QStringLiteral("systemctl"), QStringLiteral("suspend")},
    });
    // Suspend is the one action old hardware genuinely fails at (§4.6), so we
    // only offer it when the kernel says this machine can do it *and* we have
    // a way to ask for it — never as a hopeful guess.
    if (suspend.available()) {
        QFile state(QStringLiteral("/sys/power/state"));
        const bool mem = state.open(QIODevice::ReadOnly | QIODevice::Text)
            && QString::fromUtf8(state.readAll()).simplified()
                   .split(QLatin1Char(' '), Qt::SkipEmptyParts)
                   .contains(QStringLiteral("mem"));
        if (!mem) {
            suspend.argv.clear();
            suspend.why = QCoreApplication::translate("Salir", "sin suspensión a RAM en este equipo");
        }
    } else {
        suspend.why = QCoreApplication::translate("Salir", "sin elogind/systemd para suspender");
    }
    out.append(suspend);

    Action logout;
    logout.id = QStringLiteral("cerrar-sesion");
    logout.label = QCoreApplication::translate("Salir", "Cerrar sesión");
    logout.hint = QCoreApplication::translate("Salir", "Vuelve a la pantalla de inicio de sesión.");
    logout.glyph = Glyph::Logout;
    logout.destructive = true;
    logout.confirm = QStringLiteral(
        "¿Cerrar la sesión? Se cerrarán todos tus programas abiertos.");
    // castalia-session traps SIGTERM and tears the session down cleanly
    // (plays the farewell sound, stops the supervised processes). Signalling
    // the session directly is exact; pkill is the fallback for a session we
    // did not start, and `openbox --exit` the last resort.
    logout.signalPid = sessionPid();
    if (logout.signalPid == 0) {
        // `pkill -x` matches against the process NAME, which the kernel
        // truncates to 15 bytes — it can never match a 16-character
        // "castalia-session" and silently matches nothing. Match the full
        // command line instead, and check the exit code so a fallback that
        // finds no session reports failure rather than closing the dialog
        // as if it had logged the user out.
        logout.argv = firstAvailable({
            {QStringLiteral("pkill"), QStringLiteral("-TERM"),
             QStringLiteral("-f"), QStringLiteral("castalia-session")},
            {QStringLiteral("openbox"), QStringLiteral("--exit")},
        });
        logout.verifyExit = true;
        if (!logout.available())
            logout.why = QCoreApplication::translate("Salir", "sin sesión Castalia que cerrar");
    }
    out.append(logout);

    Action lock;
    lock.id = QStringLiteral("bloquear");
    lock.label = QCoreApplication::translate("Salir", "Bloquear");
    lock.hint = QCoreApplication::translate("Salir", "Protege la pantalla sin cerrar nada.");
    lock.glyph = Glyph::Lock;
    // Only a locker that really asks for a password counts. Castalia's own
    // screensaver does not authenticate, so offering it here would be a lie.
    lock.argv = firstAvailable({
        {QStringLiteral("xsecurelock")},
        {QStringLiteral("slock")},
        {QStringLiteral("i3lock")},
        {QStringLiteral("xtrlock")},
        {QStringLiteral("xscreensaver-command"), QStringLiteral("-lock")},
        {QStringLiteral("light-locker-command"), QStringLiteral("-l")},
        {QStringLiteral("dm-tool"), QStringLiteral("lock")},
    });
    if (!lock.available())
        lock.why = QCoreApplication::translate("Salir", "sin bloqueador de pantalla instalado");
    out.append(lock);

    Action switchUser;
    switchUser.id = QStringLiteral("cambiar-usuario");
    switchUser.label = QCoreApplication::translate("Salir", "Cambiar de usuario");
    switchUser.hint = QCoreApplication::translate("Salir", "Deja tu sesión abierta y abre otra.");
    switchUser.glyph = Glyph::SwitchUser;
    switchUser.argv = firstAvailable({
        {QStringLiteral("dm-tool"), QStringLiteral("switch-to-greeter")},
    });
    if (!switchUser.available())
        switchUser.why = QCoreApplication::translate("Salir", "requiere LightDM (dm-tool)");
    out.append(switchUser);

    return out;
}

// Perform an action. Returns false when it could not even be started.
bool perform(const Action &a)
{
    if (a.signalPid > 0)
        return ::kill(static_cast<pid_t>(a.signalPid), SIGTERM) == 0;
    if (a.argv.isEmpty())
        return false;
    // Most actions are fire-and-forget: the machine is going down and there
    // is nothing to report. The logout fallback is the exception — pkill
    // exits 1 when it matched nothing, and starting a process that does
    // nothing is not the same as logging out, so that one is run
    // synchronously (pkill takes milliseconds) and its status believed.
    if (a.verifyExit)
        return QProcess::execute(a.argv.first(), a.argv.mid(1)) == 0;
    return QProcess::startDetached(a.argv.first(), a.argv.mid(1));
}

void paintGlyph(QPainter &p, Glyph glyph, const QRectF &box,
                const QColor &ink, const QColor &accent)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal s = box.width();          // the box is square
    const QPointF c = box.center();
    QPen pen(ink, qMax<qreal>(2.0, s * 0.085));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (glyph) {
    case Glyph::Power: {
        // Ring with a gap at the top + the vertical stem: the IEC symbol
        // everyone reads as "power", drawn in our own weight.
        const qreal r = s * 0.34;
        QRectF ring(c.x() - r, c.y() - r + s * 0.04, r * 2, r * 2);
        p.drawArc(ring, -60 * 16, 300 * 16);
        QPen stem = pen;
        stem.setColor(accent);
        p.setPen(stem);
        p.drawLine(QPointF(c.x(), c.y() - s * 0.40),
                   QPointF(c.x(), c.y() - s * 0.02));
        break;
    }
    case Glyph::Restart: {
        const qreal r = s * 0.33;
        QRectF ring(c.x() - r, c.y() - r, r * 2, r * 2);
        p.drawArc(ring, 60 * 16, 285 * 16);
        // arrowhead at the open end (top-right), filled in the accent
        QPainterPath head;
        const QPointF tip(c.x() + r * 0.52, c.y() - r * 0.86);
        head.moveTo(tip);
        head.lineTo(tip + QPointF(s * 0.16, s * 0.05));
        head.lineTo(tip + QPointF(s * 0.02, s * 0.19));
        head.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawPath(head);
        break;
    }
    case Glyph::Suspend: {
        // Crescent: a filled disc minus an offset disc (even-odd), so it
        // stays crisp at any size without an asset.
        QPainterPath moon;
        moon.addEllipse(c + QPointF(s * 0.02, 0), s * 0.34, s * 0.34);
        QPainterPath bite;
        bite.addEllipse(c + QPointF(s * 0.20, -s * 0.10), s * 0.30, s * 0.30);
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawPath(moon.subtracted(bite));
        p.setBrush(accent);
        p.drawEllipse(c + QPointF(-s * 0.02, -s * 0.26), s * 0.035, s * 0.035);
        break;
    }
    case Glyph::Logout: {
        // A door with the frame open and an arrow stepping out to the right.
        const QRectF door(c.x() - s * 0.36, c.y() - s * 0.34,
                          s * 0.40, s * 0.68);
        QPainterPath frame;
        frame.moveTo(door.topRight());
        frame.lineTo(door.topLeft());
        frame.lineTo(door.bottomLeft());
        frame.lineTo(door.bottomRight());
        p.drawPath(frame);
        QPen arrow = pen;
        arrow.setColor(accent);
        p.setPen(arrow);
        p.drawLine(QPointF(c.x() - s * 0.02, c.y()),
                   QPointF(c.x() + s * 0.32, c.y()));
        QPainterPath head;
        head.moveTo(QPointF(c.x() + s * 0.40, c.y()));
        head.lineTo(QPointF(c.x() + s * 0.22, c.y() - s * 0.14));
        head.lineTo(QPointF(c.x() + s * 0.22, c.y() + s * 0.14));
        head.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawPath(head);
        break;
    }
    case Glyph::Lock: {
        const QRectF body(c.x() - s * 0.26, c.y() - s * 0.06,
                          s * 0.52, s * 0.40);
        QRectF shackle(c.x() - s * 0.16, c.y() - s * 0.34,
                       s * 0.32, s * 0.36);
        p.drawArc(shackle, 0, 180 * 16);
        p.drawLine(shackle.left(), shackle.center().y(),
                   shackle.left(), body.top());
        p.drawLine(shackle.right(), shackle.center().y(),
                   shackle.right(), body.top());
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(body, s * 0.06, s * 0.06);
        break;
    }
    case Glyph::SwitchUser: {
        // Two figures, the front one in the accent — the same reading as the
        // 48 px "users" icon, redrawn for this size.
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawEllipse(c + QPointF(s * 0.10, -s * 0.20), s * 0.13, s * 0.13);
        QPainterPath backBody;
        backBody.addRoundedRect(
            QRectF(c.x() - s * 0.05, c.y(), s * 0.32, s * 0.30),
            s * 0.10, s * 0.10);
        p.drawPath(backBody);
        p.setBrush(accent);
        p.drawEllipse(c + QPointF(-s * 0.12, -s * 0.14), s * 0.15, s * 0.15);
        QPainterPath frontBody;
        frontBody.addRoundedRect(
            QRectF(c.x() - s * 0.32, c.y() + s * 0.06, s * 0.40, s * 0.30),
            s * 0.11, s * 0.11);
        p.drawPath(frontBody);
        break;
    }
    }
    p.restore();
}

// A big power-dialog tile: glyph, label, hint. Focusable and keyboard-
// operable like any button (§7.11 keyboard-only).
class PowerTile : public QAbstractButton {
    Q_OBJECT
public:
    PowerTile(const Action &action, const ThemeTokens &tokens,
              QWidget *parent = nullptr)
        : QAbstractButton(parent), m_action(action), m_tokens(tokens)
    {
        setObjectName(QStringLiteral("PowerTile"));
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(190, 132);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setCursor(action.available() ? Qt::PointingHandCursor
                                     : Qt::ForbiddenCursor);
        setEnabled(action.available());
        setToolTip(action.available()
                       ? action.hint
                       : QStringLiteral("No disponible: %1").arg(action.why));
        setAccessibleName(action.label);
        setAccessibleDescription(action.hint);
    }

    const Action &action() const { return m_action; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const bool on = isEnabled();
        const bool hot = on && (underMouse() || hasFocus());
        const QColor surface(tok("surface", QStringLiteral("#F2F2F2")));
        const QColor alt(tok("surface_alt", QStringLiteral("#E6E6E6")));
        const QColor border(tok("border", QStringLiteral("#B0B0B0")));
        const QColor accent(tok("accent", QStringLiteral("#F57900")));
        QColor ink(tok("text", QStringLiteral("#202020")));
        QColor sub(tok("text_secondary", QStringLiteral("#666666")));
        if (!on) {
            ink.setAlpha(110);
            sub.setAlpha(110);
        }

        const QRectF card = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        QLinearGradient g(card.topLeft(), card.bottomLeft());
        g.setColorAt(0.0, hot ? surface.lighter(104) : surface);
        g.setColorAt(1.0, hot ? surface : alt);
        p.setBrush(g);
        p.setPen(QPen(hot ? accent : border, hot ? 2.0 : 1.0));
        p.drawRoundedRect(card, 4, 4);
        if (isDown() && on) {
            p.setBrush(QColor(0, 0, 0, 18));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(card, 4, 4);
        }

        const qreal side = 44;
        paintGlyph(p, m_action.glyph,
                   QRectF(card.center().x() - side / 2, card.top() + 12,
                          side, side),
                   ink, on ? accent : ink);

        QFont f = font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() + 0.5);
        p.setFont(f);
        p.setPen(ink);
        const QRectF labelBox(card.left() + 6, card.top() + 62,
                              card.width() - 12, 20);
        // Elide rather than clip: "Cambiar de usuario" must stay readable
        // even in the 800×600 layout (§7.11).
        p.drawText(labelBox, Qt::AlignHCenter | Qt::AlignVCenter,
                   QFontMetrics(f).elidedText(m_action.label, Qt::ElideRight,
                                              qRound(labelBox.width())));

        f.setBold(false);
        f.setPointSizeF(qMax<qreal>(7.0, font().pointSizeF() - 1.0));
        p.setFont(f);
        p.setPen(sub);
        const QString hint = on ? m_action.hint : m_action.why;
        p.drawText(QRectF(card.left() + 8, card.top() + 82,
                          card.width() - 16, card.height() - 88),
                   Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, hint);
    }

    // QAbstractButton only activates on Space; a tile this size reads as a
    // card, and users press Enter on it. The dialog must be fully operable
    // from the keyboard (§7.11), so accept both.
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (isEnabled())
                click();
            return;
        }
        QAbstractButton::keyPressEvent(event);
    }

    void enterEvent(QEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent *e) override { update(); QAbstractButton::leaveEvent(e); }
    void focusInEvent(QFocusEvent *e) override { update(); QAbstractButton::focusInEvent(e); }
    void focusOutEvent(QFocusEvent *e) override { update(); QAbstractButton::focusOutEvent(e); }

private:
    QString tok(const char *key, const QString &fallback) const
    {
        const QString v = m_tokens.str(QStringLiteral("colors"),
                                       QString::fromLatin1(key));
        return v.isEmpty() ? fallback : v;
    }

    Action m_action;
    ThemeTokens m_tokens;
};

} // namespace

class SalirDialog : public QWidget {
    Q_OBJECT
public:
    SalirDialog(const QString &repo, const ThemeTokens &tokens,
                const QVector<Action> &actions, const QString &focusId)
        : m_repo(repo), m_tokens(tokens), m_actions(actions)
    {
        setWindowTitle(tr("Salir de Castalia"));
        resize(660, 424);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("SlHeader"));
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "#SlHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *icon = new QLabel(head);
        icon->setPixmap(castalia::themeIcon(m_repo, QStringLiteral("power"))
                            .pixmap(28, 28));
        hl->addWidget(icon);
        auto *title = new QLabel(head);
        // The name comes from the window title rather than being spelled a
        // second time inside the markup: two copies of the same sentence are
        // two strings to translate, and only one of them ever gets noticed
        // when it is missed (§7.13).
        title->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>%2</span>"
            "&nbsp;&nbsp;<span style='color:%1'>%3</span>")
            .arg(colorTok("titlebar_text"), windowTitle(),
                 tr("¿qué quieres hacer?")));
        hl->addWidget(title);
        hl->addStretch(1);
        root->addWidget(head);

        m_stack = new QStackedWidget(this);
        root->addWidget(m_stack, 1);

        // --- page 0: the tiles ------------------------------------------
        auto *grid = new QWidget(m_stack);
        auto *gl = new QGridLayout(grid);
        gl->setContentsMargins(14, 14, 14, 8);
        gl->setSpacing(10);
        int col = 0, row = 0;
        PowerTile *focusTile = nullptr;
        for (const Action &a : m_actions) {
            auto *tile = new PowerTile(a, m_tokens, grid);
            connect(tile, &QAbstractButton::clicked, this,
                    [this, a]() { request(a); });
            gl->addWidget(tile, row, col);
            if (a.id == focusId && tile->isEnabled())
                focusTile = tile;
            if (!focusTile && !m_firstEnabled && tile->isEnabled())
                m_firstEnabled = tile;
            if (++col == 3) { col = 0; ++row; }
        }
        m_stack->addWidget(grid);

        // --- page 1: the confirmation (§7.6) ----------------------------
        auto *confirm = new QWidget(m_stack);
        auto *cl = new QVBoxLayout(confirm);
        cl->setContentsMargins(28, 24, 28, 16);
        cl->setSpacing(12);
        cl->addStretch(1);
        m_confirmTitle = new QLabel(confirm);
        QFont bigFont = font();
        bigFont.setBold(true);
        bigFont.setPointSizeF(bigFont.pointSizeF() + 3);
        m_confirmTitle->setFont(bigFont);
        m_confirmTitle->setAlignment(Qt::AlignHCenter);
        cl->addWidget(m_confirmTitle);
        m_confirmText = new QLabel(confirm);
        m_confirmText->setWordWrap(true);
        m_confirmText->setAlignment(Qt::AlignHCenter);
        m_confirmText->setProperty("secondary", true);
        cl->addWidget(m_confirmText);
        cl->addStretch(1);
        auto *cbar = new QHBoxLayout;
        cbar->addStretch(1);
        m_back = new QPushButton(tr("Cancelar"), confirm);
        m_back->setCursor(Qt::PointingHandCursor);
        connect(m_back, &QPushButton::clicked, this, [this]() {
            m_pending = Action();
            m_stack->setCurrentIndex(0);
            if (m_firstEnabled)
                m_firstEnabled->setFocus();
        });
        cbar->addWidget(m_back);
        m_go = new QPushButton(confirm);
        m_go->setObjectName(QStringLiteral("Primary"));
        m_go->setCursor(Qt::PointingHandCursor);
        m_go->setDefault(true);
        connect(m_go, &QPushButton::clicked, this, [this]() {
            run(m_pending);
        });
        cbar->addWidget(m_go);
        cbar->addStretch(1);
        cl->addLayout(cbar);
        m_stack->addWidget(confirm);

        // --- footer ------------------------------------------------------
        // Only on the tile page: the confirmation page carries its own
        // Cancelar, and two of them side by side is a needless choice.
        m_foot = new QWidget(this);
        auto *bar = new QHBoxLayout(m_foot);
        bar->setContentsMargins(14, 0, 14, 12);
        m_note = new QLabel(m_foot);
        m_note->setProperty("secondary", true);
        m_note->setWordWrap(true);
        bar->addWidget(m_note, 1);
        auto *cancel = new QPushButton(tr("Cancelar"), m_foot);
        cancel->setCursor(Qt::PointingHandCursor);
        connect(cancel, &QPushButton::clicked, this, &QWidget::close);
        bar->addWidget(cancel);
        root->addWidget(m_foot);
        connect(m_stack, &QStackedWidget::currentChanged, m_foot,
                [this](int index) { m_foot->setVisible(index == 0); });

        QStringList missing;
        for (const Action &a : m_actions)
            if (!a.available())
                missing << a.label.toLower();
        m_note->setText(missing.isEmpty()
            ? tr("Pulsa Esc para volver al escritorio.")
            : tr("No disponible en este equipo: %1. "
                 "Pulsa Esc para volver al escritorio.")
                  .arg(missing.join(QStringLiteral(", "))));

        if (focusTile)
            focusTile->setFocus();
        else if (m_firstEnabled)
            m_firstEnabled->setFocus();
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            if (m_stack->currentIndex() == 1) {
                m_pending = Action();
                m_stack->setCurrentIndex(0);
                if (m_firstEnabled)
                    m_firstEnabled->setFocus();
            } else {
                close();
            }
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    // A destructive action goes through the confirmation page; the rest run
    // straight away (locking or suspending loses no work).
    void request(const Action &a)
    {
        if (!a.available())
            return;
        if (!a.destructive) {
            run(a);
            return;
        }
        m_pending = a;
        m_confirmTitle->setText(a.label);
        m_confirmText->setText(a.confirm);
        m_go->setText(QStringLiteral("Sí, %1").arg(a.label.toLower()));
        m_stack->setCurrentIndex(1);
        m_go->setFocus();
    }

    void run(const Action &a)
    {
        if (!a.available())
            return;
        if (!perform(a)) {
            m_stack->setCurrentIndex(0);
            m_note->setText(QStringLiteral(
                "No se pudo %1: la orden del sistema falló. "
                "Puedes intentarlo desde el Terminal.").arg(a.label.toLower()));
            return;
        }
        // The action is under way; the session is going down (or the locker
        // is up) — get out of the way immediately.
        close();
    }

    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }

    QString m_repo;
    ThemeTokens m_tokens;
    QVector<Action> m_actions;
    Action m_pending;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_foot = nullptr;
    QLabel *m_confirmTitle = nullptr, *m_confirmText = nullptr,
           *m_note = nullptr;
    QPushButton *m_go = nullptr, *m_back = nullptr;
    PowerTile *m_firstEnabled = nullptr;
};

namespace {

// --- head-less self-test (Bible §17.4) -----------------------------------
// The action table is the contract this dialog rests on: every tile must be
// performable or explain itself, and destructive tiles must ask first. That
// is checkable with no display, so CI checks it on every build.
int selftest()
{
    int failures = 0;
    auto check = [&failures](bool ok, const char *what) {
        if (!ok) {
            QTextStream(stderr) << "salir-selftest: FAIL " << what << '\n';
            ++failures;
        }
    };

    const QVector<Action> actions = resolveActions();
    const QStringList expected = {
        QStringLiteral("apagar"),         QStringLiteral("reiniciar"),
        QStringLiteral("suspender"),      QStringLiteral("cerrar-sesion"),
        QStringLiteral("bloquear"),       QStringLiteral("cambiar-usuario"),
    };
    QStringList ids;
    for (const Action &a : actions)
        ids << a.id;
    check(ids == expected, "the six \u00a7 7.6 actions, in order");

    for (const Action &a : actions) {
        check(!a.label.isEmpty(), "every action has a label");
        check(!a.hint.isEmpty(), "every action has a hint");
        // The honesty invariant: a tile is either performable, or it is
        // disabled *and* says why. Never neither, never both.
        check(a.available() != !a.why.isEmpty(),
              "available xor explained");
        if (a.available())
            check(a.signalPid > 0 || !a.argv.isEmpty(),
                  "an available action carries a way to perform it");
        // Nothing that closes your programs may fire without asking.
        if (a.destructive)
            check(!a.confirm.isEmpty(),
                  "a destructive action carries its confirmation text");
    }

    // The three that lose work must be the three that confirm.
    QStringList destructive;
    for (const Action &a : actions)
        if (a.destructive)
            destructive << a.id;
    check(destructive == QStringList({QStringLiteral("apagar"),
                                      QStringLiteral("reiniciar"),
                                      QStringLiteral("cerrar-sesion")}),
          "apagar, reiniciar and cerrar-sesion are the confirmed ones");

    // The comm-truncation trap: a running "castalia-session" reports comm
    // "castalia-sessio" (15 bytes), so identification must come from
    // cmdline. These are the exact byte strings /proc hands back.
    check(cmdlineIsSession(QByteArray("/opt/castalia/bin/castalia-session\0",
                                      34)),
          "an installed session is recognised from its cmdline");
    check(cmdlineIsSession(QByteArray("/bin/sh\0./castalia-session\0", 27)),
          "a session run through its interpreter is recognised");
    check(!cmdlineIsSession(QByteArray("/usr/bin/castalia-panel\0", 24)),
          "another Castalia process is not mistaken for the session");
    check(!cmdlineIsSession(QByteArray("castalia-sessio\0", 16)),
          "the truncated comm name alone is not accepted as the cmdline");
    check(!cmdlineIsSession(QByteArray()), "an empty cmdline is refused");

    // The fallback must match the full command line: `pkill -x` compares
    // against the 15-byte process name and can never match this one.
    for (const Action &a : actions) {
        if (a.id != QStringLiteral("cerrar-sesion") || a.argv.isEmpty())
            continue;
        if (a.argv.first().endsWith(QStringLiteral("pkill"))) {
            check(a.argv.contains(QStringLiteral("-f")),
                  "the pkill fallback matches the full command line");
            check(!a.argv.contains(QStringLiteral("-x")),
                  "the pkill fallback does not use -x (name is truncated)");
            check(a.verifyExit,
                  "the pkill fallback checks whether it matched anything");
        }
    }

    // A bogus CASTALIA_SESSION_PID must never be trusted into a signal.
    const QByteArray saved = qgetenv("CASTALIA_SESSION_PID");
    qputenv("CASTALIA_SESSION_PID", "999999999");
    check(sessionPid() == 0, "an unverifiable session pid is refused");
    qputenv("CASTALIA_SESSION_PID", "0");
    check(sessionPid() == 0, "pid 0 is refused");
    qputenv("CASTALIA_SESSION_PID", "no-soy-un-numero");
    check(sessionPid() == 0, "a non-numeric session pid is refused");
    if (saved.isEmpty())
        qunsetenv("CASTALIA_SESSION_PID");
    else
        qputenv("CASTALIA_SESSION_PID", saved);

    QTextStream(stdout) << (failures == 0
        ? QStringLiteral("salir-selftest: OK\n")
        : QStringLiteral("salir-selftest: %1 failure(s)\n").arg(failures));
    return failures == 0 ? 0 : 1;
}

void addOptions(QCommandLineParser &cli)
{
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("focus"),
                   QStringLiteral("Preselect an action tile"),
                   QStringLiteral("id")});
    cli.addOption({QStringLiteral("action"),
                   QStringLiteral("Perform an action without showing the "
                                  "dialog"),
                   QStringLiteral("id")});
    cli.addOption({QStringLiteral("selftest"),
                   QStringLiteral("Run the head-less action-table self-test")});
    cli.addOption({QStringLiteral("print-actions"),
                   QStringLiteral("Print the resolved action table and exit")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
}

// True when the invocation needs no GUI at all. Resolving and performing an
// action is pure QtCore, so these modes must work from a tty or a service
// with no display — constructing a QApplication first would abort there.
bool wantsHeadless(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        if (a == "--print-actions" || a == "--selftest"
            || a == "--action" || a.startsWith("--action="))
            return true;
    }
    return false;
}

int runHeadless(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("castalia-salir"));
    QCommandLineParser cli;
    addOptions(cli);
    cli.process(app);

    if (cli.isSet(QStringLiteral("selftest")))
        return selftest();

    const QVector<Action> actions = resolveActions();

    // Introspection: what can this machine actually do, and with what?
    if (cli.isSet(QStringLiteral("print-actions"))) {
        QTextStream out(stdout);
        for (const Action &a : actions) {
            QString how = a.signalPid > 0
                ? QStringLiteral("SIGTERM %1").arg(a.signalPid)
                : a.argv.join(QLatin1Char(' '));
            if (!a.available())
                how = QStringLiteral("— %1").arg(a.why);
            out << a.id << '|'
                << (a.available() ? QStringLiteral("si")
                                  : QStringLiteral("no"))
                << '|' << how << '\n';
        }
        return 0;
    }

    // Direct execution (the panel's "Bloquear" uses this): no window at all.
    const QString id = cli.value(QStringLiteral("action"));
    for (const Action &a : actions) {
        if (a.id != id)
            continue;
        if (!a.available()) {
            QTextStream(stderr)
                << "castalia-salir: " << a.id << " no disponible: "
                << a.why << '\n';
            return 2;
        }
        return perform(a) ? 0 : 1;
    }
    QTextStream(stderr) << "castalia-salir: acción desconocida: " << id << '\n';
    return 2;
}

} // namespace

int main(int argc, char **argv)
{
    if (wantsHeadless(argc, argv))
        return runHeadless(argc, argv);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-salir"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    addOptions(cli);
    cli.process(app);

    const QVector<Action> actions = resolveActions();
    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const ThemeTokens tokens = castalia::applyTheme(&app, repo, themeId);

    SalirDialog w(repo, tokens, actions, cli.value(QStringLiteral("focus")));
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
