#include "CastaliaPanel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QRadialGradient>
#include <QScreen>
#include <QStyle>
#include <QStyleOption>
#include <QStylePainter>
#include <QStyleOptionButton>
#include <QTime>
#include <QTimer>

#include "CastaliaMenu.h"
#include "Switcher.h"
#include "TrayHost.h"
#include "XEmbedTray.h"
#include "Mark.h"
#include "Theme.h"
#include "WindowList.h"

// The orb: the Castalia mark itself, painted at the full height of the launch
// key so the round badge reads as one — not a small logo dropped on a button.
// Hovering blooms a soft halo behind it (the mark is a disc, so the halo is a
// disc too); the QSS reserves the space with its left padding.
void LaunchButton::paintEvent(QPaintEvent *event)
{
    QPushButton::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal side = height() - 6.0;
    const QRectF orb(4.5, (height() - side) / 2.0, side, side);
    if (underMouse() && !castalia::reduceMotion()) {
        QRadialGradient halo(orb.center(), side * 0.78);
        halo.setColorAt(0.0, QColor(255, 255, 255, 120));
        halo.setColorAt(0.55, QColor(255, 255, 255, 46));
        halo.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(orb.center(), side * 0.78, side * 0.78);
    }
    castalia::drawMark(&p, orb);
}

void ClockLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked();
    QLabel::mousePressEvent(event);
}

CastaliaPanel::CastaliaPanel(const ThemeTokens &tokens, int width,
                             bool demoTasks, QWidget *parent)
    : QWidget(parent), m_tokens(tokens)
{
    setObjectName(QStringLiteral("CastaliaPanel"));
    // A real dock window (§7.1): the WM leaves it undecorated and reserves a
    // strut. width<=0 means "span the primary screen" (live session).
    setAttribute(Qt::WA_X11NetWmWindowTypeDock, true);
    setWindowFlag(Qt::FramelessWindowHint, true);
    if (width <= 0)
        width = screen() ? screen()->geometry().width() : 1024;
    setFixedSize(width, m_tokens.panelHeight());
    if (screen())
        move(0, screen()->geometry().height() - m_tokens.panelHeight());

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 2, 6, 2);
    lay->setSpacing(6);

    // launch button — the corner menu ergonomic (§7.3)
    auto *launch = new LaunchButton(QStringLiteral("Castalia"), this);
    launch->setObjectName(QStringLiteral("LaunchButton"));
    launch->setCursor(Qt::PointingHandCursor);
    connect(launch, &QPushButton::clicked, this, &CastaliaPanel::toggleMenu);
    lay->addWidget(launch);

    auto addSeparator = [this, lay]() {
        auto *sep = new QFrame(this);
        sep->setObjectName(QStringLiteral("PanelSep"));
        sep->setFrameShape(QFrame::VLine);
        lay->addWidget(sep);
    };
    addSeparator();

    // quick launch (§7.4) — real one-click launchers with the family icons.
    // Theme resolved at click time so they honour a Control Center change.
    const QString repo = qEnvironmentVariable("CASTALIA_REPO",
                                              QStringLiteral("."));
    const QString panelTheme = m_tokens.themeId().isEmpty()
        ? QStringLiteral("classic") : m_tokens.themeId();
    const struct {
        const char *icon; const char *tip; const char *bin;
    } quick[] = {
        {"folder", "Castalia Explorer", "castalia-explorer"},
        {"help", "Centro de ayuda", "castalia-bienvenida"},
    };
    for (const auto &q : quick) {
        auto *ql = new QPushButton(this);
        ql->setObjectName(QStringLiteral("QuickLaunch"));
        ql->setIcon(castalia::themeIcon(repo, QLatin1String(q.icon)));
        ql->setIconSize(QSize(18, 18));
        ql->setToolTip(QString::fromUtf8(q.tip));
        ql->setCursor(Qt::PointingHandCursor);
        ql->setFixedWidth(30);
        const QString bin = QLatin1String(q.bin);
        connect(ql, &QPushButton::clicked, this, [repo, panelTheme, bin]() {
            QProcess::startDetached(
                bin, {QStringLiteral("--repo"), repo,
                      QStringLiteral("--theme"),
                      castalia::activeThemeId(panelTheme)});
        });
        lay->addWidget(ql);
    }
    addSeparator();

    // window list — the REAL open windows via EWMH (§7.2). It grows to fill
    // the panel; in offscreen/screenshot renders it shows a demo set instead.
    m_tasks = new WindowList(demoTasks, this);
    lay->addWidget(m_tasks, 1);

    // tray + clock
    auto *tray = new QFrame(this);
    tray->setObjectName(QStringLiteral("TrayFrame"));
    auto *trayLay = new QHBoxLayout(tray);
    trayLay->setContentsMargins(10, 0, 10, 0);
    trayLay->setSpacing(8);
    // Third-party indicators (§7.4). The host claims
    // org.kde.StatusNotifierWatcher, so anything speaking SNI lands here; it
    // takes no space until something registers.
    m_tray = new TrayHost(repo, tray);
    trayLay->addWidget(m_tray);

    // …and everything older, through the X System Tray Protocol (§7.4). The
    // two halves are independent: a session with no D-Bus at all still gets a
    // working tray through this one.
    // A legacy icon paints itself ParentRelative, so the flat colour we hand
    // the embedder is literally what shows behind it — get it wrong and every
    // icon wears a visible patch. Reproduce what the eye sees at that spot:
    // the panel gradient half-way down, under the tray well's rgba(0,0,0,52).
    m_xembed = new XEmbedTray(
        trayWellColor(m_tokens.color(QStringLiteral("titlebar_top")),
                      m_tokens.color(QStringLiteral("titlebar_bottom"))),
        tray);
    trayLay->addWidget(m_xembed);

    // Our own tray item: the volume speaker opens the Control de volumen
    // (replacing the earlier placeholder dots). Theme resolved at click time.
    auto *vol = new QPushButton(tray);
    vol->setObjectName(QStringLiteral("VolBtn"));
    vol->setIcon(castalia::themeIcon(repo, QStringLiteral("speaker")));
    vol->setIconSize(QSize(18, 18));
    vol->setToolTip(tr("Control de volumen"));
    vol->setCursor(Qt::PointingHandCursor);
    vol->setFixedSize(24, 24);
    connect(vol, &QPushButton::clicked, this, [repo, panelTheme]() {
        QProcess::startDetached(
            QStringLiteral("castalia-volumen"),
            {QStringLiteral("--repo"), repo, QStringLiteral("--theme"),
             castalia::activeThemeId(panelTheme)});
    });
    trayLay->addWidget(vol);

    // The network light (§9 Network Center, "status tray"). It is a panel
    // button rather than a resident applet on purpose: the state it shows is
    // three reads of /sys/class/net, and a whole extra process to poll that
    // is not a trade the FLOOR tier should make.
    m_net = new QPushButton(tray);
    m_net->setObjectName(QStringLiteral("VolBtn"));   // same glassy treatment
    m_net->setIcon(castalia::themeIcon(repo, QStringLiteral("network")));
    m_net->setIconSize(QSize(18, 18));
    m_net->setCursor(Qt::PointingHandCursor);
    m_net->setFixedSize(24, 24);
    connect(m_net, &QPushButton::clicked, this, [repo, panelTheme]() {
        QProcess::startDetached(
            QStringLiteral("castalia-redes"),
            {QStringLiteral("--repo"), repo, QStringLiteral("--theme"),
             castalia::activeThemeId(panelTheme)});
    });
    trayLay->addWidget(m_net);
    updateNetwork();
    // Ten seconds, very coarse: a cable coming out is not an event anybody
    // needs to see within a frame, and the panel's wakeups are budgeted (§16).
    auto *netTimer = new QTimer(this);
    netTimer->setTimerType(Qt::VeryCoarseTimer);
    connect(netTimer, &QTimer::timeout, this, &CastaliaPanel::updateNetwork);
    netTimer->start(10000);

    m_clock = new ClockLabel(tray);
    m_clock->setObjectName(QStringLiteral("ClockLabel"));
    m_clock->setCursor(Qt::PointingHandCursor);
    m_clock->setToolTip(tr("Abrir el calendario"));
    // Click the time to open the Calendario (XP-era ergonomic); theme resolved
    // at click time so it honours a Control Center change.
    connect(m_clock, &ClockLabel::clicked, this, [repo, panelTheme]() {
        QProcess::startDetached(
            QStringLiteral("castalia-calendario"),
            {QStringLiteral("--repo"), repo, QStringLiteral("--theme"),
             castalia::activeThemeId(panelTheme)});
    });
    trayLay->addWidget(m_clock);
    lay->addWidget(tray);

    // The clock shows HH:mm, so it only needs to wake once a minute —
    // updateClock() re-arms the timer for the next minute boundary (1/60th
    // of the wakeups of a 1 s tick; §16 FLOOR budget).
    // Alt+Tab (§7.6). It is a window of its own, owned by the panel: the
    // switch has a ≤120 ms budget on FLOOR and spawning a process would spend
    // most of it before drawing a pixel. A screenshot render never binds — it
    // must not walk off with the real session's Alt+Tab.
    m_switcher = new Switcher(m_tokens, repo, this);
    if (!demoTasks)
        m_switcher->bind();

    m_clockTimer = new QTimer(this);
    m_clockTimer->setSingleShot(true);
    m_clockTimer->setTimerType(Qt::VeryCoarseTimer);
    connect(m_clockTimer, &QTimer::timeout, this,
            &CastaliaPanel::updateClock);
    updateClock();

    m_menu = new CastaliaMenu(m_tokens);
}

void CastaliaPanel::toggleMenu()
{
    if (m_menu->isVisible()) {
        m_menu->hide();
        return;
    }
    const QPoint anchor = mapToGlobal(QPoint(4, 0));
    m_menu->move(anchor.x(), anchor.y() - m_menu->height() - 4);
    m_menu->show();
}

// The glass. The QSS owns the panel's base gradient (it is token-derived and
// must stay that way); everything here is lighting on top of it, painted
// natively because a stylesheet cannot express a specular band that stops
// halfway down a widget. High Contrast opts out entirely: gloss is exactly
// the kind of decoration that eats contrast (§7.11).
bool CastaliaPanel::anyLinkUp(const QVector<NetLink> &links)
{
    for (const NetLink &l : links) {
        if (l.name == QLatin1String("lo"))
            continue;                  // always up, never a connection
        if (l.operstate == QLatin1String("up"))
            return true;
    }
    return false;
}

QString CastaliaPanel::networkTooltip(const QVector<NetLink> &links)
{
    for (const NetLink &l : links) {
        if (l.name == QLatin1String("lo")
            || l.operstate != QLatin1String("up"))
            continue;
        return l.wireless
            ? tr("Conectado por Wi-Fi (%1)").arg(l.name)
            : tr("Conectado por cable (%1)").arg(l.name);
    }
    return tr("Sin conexión de red");
}

QColor CastaliaPanel::trayWellColor(const QColor &top, const QColor &bottom)
{
    if (!top.isValid() || !bottom.isValid())
        return bottom.isValid() ? bottom : top;
    // The panel gradient runs top→bottom; the tray icons are centred, so the
    // colour under them is the midpoint…
    const qreal dim = 1.0 - 52.0 / 255.0;          // …dimmed by the tray well
    return QColor(qRound((top.red() + bottom.red()) / 2.0 * dim),
                  qRound((top.green() + bottom.green()) / 2.0 * dim),
                  qRound((top.blue() + bottom.blue()) / 2.0 * dim));
}

void CastaliaPanel::updateNetwork()
{
    if (!m_net)
        return;
    QVector<NetLink> links;
    QDir sys(QStringLiteral("/sys/class/net"));
    for (const QString &name :
         sys.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        NetLink link;
        link.name = name;
        QFile state(sys.filePath(name + QStringLiteral("/operstate")));
        if (state.open(QIODevice::ReadOnly | QIODevice::Text))
            link.operstate = QString::fromLatin1(state.readAll()).trimmed();
        // A wireless interface is the one the kernel gives a wireless/ dir.
        link.wireless =
            QFileInfo::exists(sys.filePath(name + QStringLiteral("/wireless")));
        links.append(link);
    }
    const bool up = anyLinkUp(links);
    m_net->setToolTip(networkTooltip(links));
    // Disconnected reads as a dimmed icon rather than a second asset: the
    // family has one network glyph, and a greyed one says the same thing.
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(m_net->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(m_net);
        m_net->setGraphicsEffect(effect);
    }
    effect->setOpacity(up ? 1.0 : 0.45);
}

void CastaliaPanel::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    QWidget::paintEvent(event);
    if (m_tokens.highContrast())
        return;

    const qreal h = height();
    p.setPen(Qt::NoPen);

    // 1) the specular band: strongest at the very top, gone by mid-height
    QLinearGradient sheen(0, 0, 0, h * 0.52);
    sheen.setColorAt(0.0, QColor(255, 255, 255, 66));
    sheen.setColorAt(0.55, QColor(255, 255, 255, 22));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillRect(QRectF(0, 0, width(), h * 0.52), sheen);

    // 2) the accent bloom the launch corner sits in
    QColor accent = m_tokens.color(QStringLiteral("accent"));
    if (accent.isValid()) {
        QRadialGradient bloom(QPointF(h * 0.9, h), h * 2.6);
        QColor warm = accent;
        warm.setAlpha(58);
        bloom.setColorAt(0.0, warm);
        warm.setAlpha(0);
        bloom.setColorAt(1.0, warm);
        p.fillRect(QRectF(0, 0, h * 3.4, h), bloom);
    }

    // 3) hairlines: a lit top edge, a shadow just above the border
    p.setPen(QPen(QColor(255, 255, 255, 92), 1));
    p.drawLine(QPointF(0, 0.5), QPointF(width(), 0.5));
    p.setPen(QPen(QColor(0, 0, 0, 46), 1));
    p.drawLine(QPointF(0, h - 1.5), QPointF(width(), h - 1.5));
}

void CastaliaPanel::updateClock()
{
    m_clock->setText(QDateTime::currentDateTime()
                         .toString(QStringLiteral("HH:mm")));
    // wake shortly after the next minute boundary
    const int ms = 60000 - QTime::currentTime().msecsSinceStartOfDay() % 60000;
    m_clockTimer->start(ms + 100);
}
