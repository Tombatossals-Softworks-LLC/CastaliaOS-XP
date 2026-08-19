#include "DesktopWindow.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QImageReader>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTimer>
#include <QVariantAnimation>

#include "Aurora.h"
#include "Mark.h"
#include "Sound.h"
#include "Theme.h"

// ---------------------------------------------------------- DesktopIcon --

DesktopIcon::DesktopIcon(const QIcon &icon, const QString &label,
                         const QColor &accent, QWidget *parent)
    : QWidget(parent), m_icon(icon), m_label(label),
      m_accent(accent.isValid() ? accent : QColor(0x3E, 0x82, 0xB6))
{
    // 104 tall, not 96: two-line labels ("Lugares de red", "Instalar
    // Castalia OS") had their second line clipped at the old height.
    setFixedSize(110, 104);
    setCursor(Qt::PointingHandCursor);
}

void DesktopIcon::enterEvent(QEvent *)
{
    animateHover(true);
}

void DesktopIcon::leaveEvent(QEvent *)
{
    animateHover(false);
}

void DesktopIcon::animateHover(bool over)
{
    const qreal target = over ? 1.0 : 0.0;
    if (castalia::reduceMotion()) {
        m_hover = target;
        update();
        return;
    }
    auto *anim = new QVariantAnimation(this);
    anim->setDuration(120);
    anim->setStartValue(m_hover);
    anim->setEndValue(target);
    anim->setEasingCurve(QEasingCurve::OutQuad);
    connect(anim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &v) {
                m_hover = v.toReal();
                update();
            });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// One accent ring, out from the icon and gone in 420 ms: the launch
// acknowledgement. A double-click on the desktop starts a whole process, which
// on the 512 MB floor can take a second to put a window on screen — without
// this, the desktop looks like it ignored you.
void DesktopIcon::pulse()
{
    if (castalia::reduceMotion())
        return;
    auto *anim = new QVariantAnimation(this);
    anim->setDuration(420);
    anim->setStartValue(qreal(0.0));
    anim->setEndValue(qreal(1.0));
    anim->setEasingCurve(QEasingCurve::OutQuad);
    connect(anim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &v) {
                m_pulse = v.toReal();
                update();
            });
    connect(anim, &QVariantAnimation::finished, this, [this]() {
        m_pulse = 0.0;
        update();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void DesktopIcon::setSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    update();
}

void DesktopIcon::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    if (m_selected) {
        QColor fill = m_accent;
        fill.setAlpha(95);
        QColor ring = m_accent;
        ring.setAlpha(190);
        p.setPen(QPen(ring, 1));
        p.setBrush(fill);
        p.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 4, 4);
    } else if (m_hover > 0.0) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, int(34 * m_hover)));
        p.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 4, 4);
    }

    if (m_pulse > 0.0) {
        // an expanding, thinning ring centred on the artwork
        QColor ring = m_accent;
        ring.setAlphaF(qBound(qreal(0.0), (1.0 - m_pulse) * 0.75, qreal(1.0)));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(ring, 2.5 * (1.0 - m_pulse) + 0.5));
        const QPointF centre(width() / 2.0, 30.0);
        const qreal r = 16.0 + 26.0 * m_pulse;
        p.drawEllipse(centre, r, r);
    }

    const QPixmap pm = m_icon.pixmap(48, 48);
    p.drawPixmap((width() - 48) / 2, 6, pm);

    QRect textRect(0, 58, width(), 42);
    p.setPen(QColor(0, 0, 0, 180));                    // soft label shadow
    p.drawText(textRect.translated(1, 1),
               Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_label);
    p.setPen(Qt::white);
    p.drawText(textRect,
               Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_label);
}

void DesktopIcon::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked(this);
    QWidget::mousePressEvent(event);
}

void DesktopIcon::mouseDoubleClickEvent(QMouseEvent *)
{
    emit activated();
}

// -------------------------------------------------------- AuroraOverlay --

AuroraOverlay::AuroraOverlay(const QColor &accent, int seconds,
                             const QString &repoRoot, QWidget *parent)
    : QWidget(parent), m_accent(accent), m_repo(repoRoot),
      m_runMs(qMax(1, seconds) * 1000)
{
    setGeometry(parent->rect());
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::BlankCursor);
    setAttribute(Qt::WA_DeleteOnClose, true);
    raise();
    show();
    setFocus(Qt::OtherFocusReason);

    if (castalia::reduceMotion()) {
        // Deterministic renders (the CI screenshot gate, a reduce-motion
        // desktop) get one held frame instead of an animation: same picture
        // every time, and it waits for a key or a click rather than a timer.
        m_phase = 0.35;
        m_opacity = 1.0;
        return;
    }
    m_frames = new QTimer(this);
    m_frames->setInterval(50);            // 20 fps — kind to the FLOOR tier
    connect(m_frames, &QTimer::timeout, this, [this]() {
        m_elapsedMs += m_frames->interval();
        m_phase = m_elapsedMs / 7000.0;
        const qreal in = qMin(qreal(1.0), m_elapsedMs / 400.0);
        const qreal out = qMin(qreal(1.0), (m_runMs - m_elapsedMs) / 700.0);
        m_opacity = qBound(qreal(0.0), qMin(in, out), qreal(1.0));
        if (m_elapsedMs >= m_runMs) {
            m_frames->stop();
            close();                       // WA_DeleteOnClose: nothing lingers
            return;
        }
        update();
    });
    m_frames->start();
}

// Bring the show to an end gracefully — the fade-out is the last 700 ms of the
// run, so shortening the run *is* the exit animation.
void AuroraOverlay::dismiss()
{
    if (!m_frames) {
        close();
        return;
    }
    m_runMs = qMin(m_runMs, m_elapsedMs + 300);
}

void AuroraOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    castalia::paintAurora(&p, rect(), m_phase, m_opacity, m_accent);

    // the mark and the wordmark, riding the curtains
    const qreal side = qMin(width(), height()) * 0.16;
    const QRectF markRect((width() - side) / 2.0, height() * 0.52, side, side);
    p.setOpacity(m_opacity);
    castalia::drawMark(&p, markRect, m_repo);

    QFont f = font();
    f.setPointSizeF(qMax(qreal(11.0), side * 0.22));
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0xFF, 0xF6, 0xE8));
    p.drawText(QRectF(0, markRect.bottom() + side * 0.16, width(), side * 0.5),
               Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("Castalia OS"));
    f.setPointSizeF(qMax(qreal(8.0), side * 0.13));
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor(0xE0, 0xD6, 0xC8));
    p.drawText(QRectF(0, markRect.bottom() + side * 0.72, width(), side * 0.5),
               Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("Tombatossals Softworks"));
    p.setOpacity(1.0);
}

void AuroraOverlay::mousePressEvent(QMouseEvent *)
{
    dismiss();
}

void AuroraOverlay::keyPressEvent(QKeyEvent *)
{
    dismiss();
}

// -------------------------------------------------------- DesktopWindow --

DesktopWindow::DesktopWindow(const ThemeTokens &tokens,
                             const QString &repoRoot, const QSize &size,
                             QWidget *parent)
    : QWidget(parent), m_tokens(tokens), m_repo(repoRoot)
{
    setWindowTitle(QStringLiteral("Castalia Desktop"));
    // A real desktop-layer window (§7.5): the WM keeps it at the bottom,
    // undecorated, covering the screen — this is the wallpaper + icon plane.
    setAttribute(Qt::WA_X11NetWmWindowTypeDesktop, true);
    setWindowFlag(Qt::FramelessWindowHint, true);
    setFixedSize(size);
    // The desktop takes the keyboard when clicked: the rubber band, and the
    // key sequence that wakes the aurora, both need it.
    setFocusPolicy(Qt::StrongFocus);

    // Wallpaper: the user's Control Center pick first, then the theme's
    // [assets].wallpaper, then the Azure Bay default (§6.16). A watcher on the
    // config makes a pick change the live desktop with no re-login.
    m_wallPath = resolveWallpaper();
    loadWallpaperSource();
    m_wallWatcher = new QFileSystemWatcher(this);
    const QString cfgDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/castalia");
    QDir().mkpath(cfgDir);
    m_wallWatcher->addPath(cfgDir);
    const QString cfg = cfgDir + QStringLiteral("/desktop.conf");
    if (QFile::exists(cfg))
        m_wallWatcher->addPath(cfg);
    connect(m_wallWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &) { reloadWallpaper(); });
    connect(m_wallWatcher, &QFileSystemWatcher::directoryChanged, this,
            [this, cfg]() {
                // The config may have just been created/replaced; (re)watch it.
                if (QFile::exists(cfg)
                    && !m_wallWatcher->files().contains(cfg))
                    m_wallWatcher->addPath(cfg);
                reloadWallpaper();
            });

    // The fixed system icons (§7.5); each double-clicks to a real location.
    const QString home =
        qEnvironmentVariable("HOME", QStringLiteral("/root"));
    addIcon(QStringLiteral("computer"), tr("Equipo"),
            QStringLiteral("/"));
    addIcon(QStringLiteral("documents"), tr("Documentos"),
            home + QStringLiteral("/Documentos"));
    addIcon(QStringLiteral("network"), tr("Lugares de red"),
            QStringLiteral("/mnt"));
    addIcon(QStringLiteral("trash"), tr("Papelera"),
            home + QStringLiteral("/.local/share/Trash/files"));
    // A live session gets one more, and it is the point of a live session:
    // try the OS, then install it without hunting through a menu for how
    // (§14.5). An installed desktop never shows it.
    if (castalia::isLiveSession())
        addAppIcon(QStringLiteral("disk"),
                   tr("Instalar Castalia OS"),
                   QStringLiteral("castalia-instalador"));
    m_systemIcons = m_icons.size();

    loadUserIcons();
    relayout();
}

DesktopWindow::~DesktopWindow() = default;

// Precedence: user override (~/.config/castalia/desktop.conf, key
// `wallpaper = "..."`) → theme [assets].wallpaper → Azure Bay default.
QString DesktopWindow::resolveWallpaper() const
{
    const QString fallback = m_repo
        + QStringLiteral("/branding/wallpapers/valle-de-castalia.jpg");

    auto resolve = [this](const QString &value) -> QString {
        if (value.isEmpty())
            return QString();
        // Absolute path used as-is; anything else is repo-relative.
        const QString path = QDir::isAbsolutePath(value)
            ? value : m_repo + QLatin1Char('/') + value;
        return QFile::exists(path) ? path : QString();
    };

    // 1) the user's Control Center pick
    const QString cfg =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/castalia/desktop.conf");
    QFile f(cfg);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const QByteArray &raw : f.readAll().split('\n')) {
            const QString line = QString::fromUtf8(raw).trimmed();
            if (!line.startsWith(QStringLiteral("wallpaper")))
                continue;
            const int q1 = line.indexOf(QLatin1Char('"'));
            const int q2 = line.indexOf(QLatin1Char('"'), q1 + 1);
            if (q1 >= 0 && q2 > q1) {
                const QString got = resolve(line.mid(q1 + 1, q2 - q1 - 1));
                if (!got.isEmpty())
                    return got;
            }
            break;   // an empty/invalid override means "follow the theme"
        }
    }

    // 2) the theme's wallpaper token
    const QString themed =
        resolve(m_tokens.str(QStringLiteral("assets"),
                             QStringLiteral("wallpaper")));
    if (!themed.isEmpty())
        return themed;

    // 3) the default
    return fallback;
}

// Point the right decoder at m_wallPath. Vector wallpapers get a renderer;
// photographic ones are left to bakeWallpaper(), which reads them scaled.
void DesktopWindow::loadWallpaperSource()
{
    delete m_wallpaper;
    m_wallpaper = nullptr;
    m_rasterPath.clear();
    if (m_wallPath.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        m_wallpaper = new QSvgRenderer(m_wallPath, this);
        return;
    }
    m_rasterPath = m_wallPath;
}

// Rasterise the wallpaper once, at the window size, into m_wall. Everything else
// that repaints the desktop — a hover halo, a selection, a rubber band —
// then costs one blit instead of a full re-render of the wallpaper artwork.
void DesktopWindow::bakeWallpaper()
{
    m_wall = QPixmap(size());
    if (m_wallpaper && m_wallpaper->isValid()) {
        m_wall.fill(Qt::transparent);
        QPainter p(&m_wall);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        // cover: scale the 4:3 source to fill, centred (slice)
        const QSizeF src(1024, 768);
        const QSizeF scaled = src.scaled(size(), Qt::KeepAspectRatioByExpanding);
        m_wallpaper->render(&p,
                            QRectF(QPointF((width() - scaled.width()) / 2.0,
                                           (height() - scaled.height()) / 2.0),
                                   scaled));
        return;
    }

    // A photographic wallpaper (JPEG/PNG). QImageReader is asked for the
    // covering size *before* decoding: the shipped default is 2560x1664, and
    // decoding that in full costs ~17 MB of RAM on a machine whose whole
    // budget is 512 MB (§16 FLOOR). The JPEG handler scales during decode, so
    // this reads roughly the screen's worth of pixels instead.
    if (!m_rasterPath.isEmpty()) {
        QImageReader reader(m_rasterPath);
        reader.setAutoTransform(true);
        const QSize src = reader.size();
        if (src.isValid() && !src.isEmpty()) {
            const QSize cover =
                src.scaled(size(), Qt::KeepAspectRatioByExpanding);
            reader.setScaledSize(cover);
        }
        const QImage img = reader.read();
        if (!img.isNull()) {
            m_wall.fill(Qt::black);
            QPainter p(&m_wall);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(QPointF((width() - img.width()) / 2.0,
                                (height() - img.height()) / 2.0),
                        img);
            return;
        }
    }

    m_wall.fill(QColor(0x2C, 0x66, 0x99));          // azure fallback
}

void DesktopWindow::reloadWallpaper()
{
    const QString path = resolveWallpaper();
    // The watcher fires for every write in ~/.config/castalia — a theme
    // change, a Run-dialog history save. Only a *different* wallpaper is
    // worth re-rasterising.
    if (path == m_wallPath && !m_wall.isNull())
        return;
    m_wallPath = path;
    const QPixmap previous = m_wall;

    loadWallpaperSource();
    bakeWallpaper();

    // Picking a wallpaper in the Control Center dissolves into it (200 ms)
    // instead of snapping — the change is live, so it deserves to look it.
    if (!previous.isNull() && !castalia::reduceMotion()) {
        m_wallOutgoing = previous;
        m_wallFade = 0.0;
        auto *fade = new QVariantAnimation(this);
        fade->setDuration(200);
        fade->setStartValue(qreal(0.0));
        fade->setEndValue(qreal(1.0));
        fade->setEasingCurve(QEasingCurve::InOutQuad);
        connect(fade, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &v) {
                    m_wallFade = v.toReal();
                    update();
                });
        connect(fade, &QVariantAnimation::finished, this, [this]() {
            m_wallFade = 1.0;
            m_wallOutgoing = QPixmap();     // free the old raster
            update();
        });
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    }
    update();
}

DesktopIcon *DesktopWindow::addIcon(const QString &iconName,
                                    const QString &label,
                                    const QString &openPath)
{
    const QString path = m_repo + QStringLiteral("/themes/icons/48/")
        + iconName + QStringLiteral(".svg");
    auto *icon = new DesktopIcon(
        QIcon(path), label, m_tokens.color(QStringLiteral("accent")), this);
    icon->setOpenPath(openPath);
    connect(icon, &DesktopIcon::clicked, this, [this](DesktopIcon *self) {
        clearSelection();
        self->setSelected(true);
    });
    connect(icon, &DesktopIcon::activated, this, [this, icon]() {
        if (icon->openPath().isEmpty())
            return;
        icon->pulse();                 // acknowledge the launch immediately
        launchPath(icon->openPath());
    });
    icon->show();
    m_icons.append(icon);
    return icon;
}

DesktopIcon *DesktopWindow::addAppIcon(const QString &iconName,
                                       const QString &label,
                                       const QString &bin)
{
    DesktopIcon *icon = addIcon(iconName, label);
    connect(icon, &DesktopIcon::activated, this, [this, icon, bin]() {
        icon->pulse();                 // acknowledge the launch immediately
        launchApp(bin, {QStringLiteral("--repo"), m_repo,
                        QStringLiteral("--theme"), launchTheme()});
    });
    return icon;
}

// The user's desktop folder (Spanish "Escritorio", or the XDG default).
QString DesktopWindow::desktopDir() const
{
    const QString home =
        qEnvironmentVariable("HOME", QStringLiteral("/root"));
    for (const QString &name :
         {QStringLiteral("/Escritorio"), QStringLiteral("/Desktop")}) {
        if (QFileInfo::exists(home + name))
            return home + name;
    }
    return home + QStringLiteral("/Escritorio");
}

// Show the contents of the desktop folder as icons alongside the system ones.
void DesktopWindow::loadUserIcons()
{
    QDir dir(desktopDir());
    if (!dir.exists())
        return;
    const auto entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name | QDir::DirsFirst);
    for (const QFileInfo &fi : entries) {
        addIcon(fi.isDir() ? QStringLiteral("folder")
                           : QStringLiteral("documents"),
                fi.fileName(), fi.absoluteFilePath());
    }
}

// Re-flow every icon into top-to-bottom columns that wrap across the screen.
void DesktopWindow::relayout()
{
    const int cellW = 116, cellH = 114, marginX = 14, marginY = 16;
    // Keep the bottom row clear of the panel/taskbar strut (§7.2).
    const int bottomReserve = m_tokens.panelHeight() + 14;
    const int usable = height() - marginY - bottomReserve;
    const int perCol = qMax(1, usable / cellH);
    for (int i = 0; i < m_icons.size(); ++i) {
        const int col = i / perCol, row = i % perCol;
        m_icons[i]->move(marginX + col * cellW, marginY + row * cellH);
    }
}

void DesktopWindow::refresh()
{
    // Drop the user icons (keep the fixed system ones), then reload.
    while (m_icons.size() > m_systemIcons) {
        DesktopIcon *icon = m_icons.takeLast();
        icon->deleteLater();
    }
    loadUserIcons();
    relayout();
    update();
}

void DesktopWindow::newFolder()
{
    QDir dir(desktopDir());
    dir.mkpath(QStringLiteral("."));
    QString name = tr("Nueva carpeta");
    for (int n = 2; dir.exists(name); ++n)
        name = tr("Nueva carpeta %1").arg(n);
    if (dir.mkdir(name))
        refresh();
}

void DesktopWindow::clearSelection()
{
    for (DesktopIcon *icon : m_icons)
        icon->setSelected(false);
}

void DesktopWindow::launchPath(const QString &path)
{
    // Folders open in Explorer; other files open with the system handler.
    // §7.1: each runs as its own process.
    if (QFileInfo(path).isDir()
        || path == QLatin1String("/")) {
        launchApp(QStringLiteral("castalia-explorer"),
                  {QStringLiteral("--repo"), m_repo,
                   QStringLiteral("--theme"), launchTheme(),
                   QStringLiteral("--path"), path});
    } else {
        QProcess::startDetached(QStringLiteral("xdg-open"), {path});
    }
}

// The theme to launch first-party apps with — the active one (honouring a
// Control Center change), falling back to the desktop's own launch theme.
QString DesktopWindow::launchTheme() const
{
    return castalia::activeThemeId(
        m_tokens.themeId().isEmpty() ? QStringLiteral("classic")
                                     : m_tokens.themeId());
}

void DesktopWindow::launchApp(const QString &bin, const QStringList &args)
{
    QProcess::startDetached(bin, args);
}

// ------------------------------------------------------------- selftest --

int DesktopWindow::selfTest()
{
    int fail = 0;
    auto check = [&fail](bool ok, const char *what) {
        if (!ok) {
            ++fail;
            qWarning("desktop selftest FAIL: %s", what);
        }
    };

    // 1) the wallpaper cache
    bakeWallpaper();
    check(!m_wall.isNull(), "the wallpaper bakes to a pixmap");
    check(m_wall.size() == size(), "the baked wallpaper is window-sized");
    const QString resolved = m_wallPath;
    reloadWallpaper();
    check(m_wallPath == resolved, "a reload re-resolves to the same source");
    check(m_wallOutgoing.isNull(),
          "an unchanged wallpaper starts no crossfade");

    // 2) the rubber band
    // Four fixed icons, and a fifth — "Instalar Castalia OS" — only when
    // this is a live session. On an installed desktop that icon must not
    // exist at all (§14.5).
    const int expected = castalia::isLiveSession() ? 5 : 4;
    check(m_systemIcons == expected,
          castalia::isLiveSession()
              ? "the live desktop offers to install itself"
              : "an installed desktop shows only the four system icons");
    check(m_icons.size() >= 4, "the fixed system icons are present");
    clearSelection();
    applyBand(m_icons.first()->geometry().adjusted(2, 2, -2, -2));
    int selected = 0;
    for (DesktopIcon *icon : m_icons)
        selected += icon->isSelected() ? 1 : 0;
    check(selected == 1, "a band over one icon selects exactly that one");
    check(m_icons.first()->isSelected(), "…and it is the right one");

    applyBand(QRect(width() - 4, height() - 4, 2, 2));
    selected = 0;
    for (DesktopIcon *icon : m_icons)
        selected += icon->isSelected() ? 1 : 0;
    check(selected == 0, "a band over bare desktop selects nothing");

    applyBand(rect());
    selected = 0;
    for (DesktopIcon *icon : m_icons)
        selected += icon->isSelected() ? 1 : 0;
    check(selected == m_icons.size(), "a full-screen band takes every icon");
    clearSelection();

    // 3) the key sequence
    auto sendKey = [this](int key) {
        QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(this, &ev);
    };
    const QVector<int> seq = castalia::KonamiDetector::sequence();
    check(m_aurora == nullptr, "no aurora at rest");
    for (int i = 0; i < seq.size() - 1; ++i)
        sendKey(seq.at(i));
    check(m_aurora == nullptr, "an incomplete sequence shows nothing");
    sendKey(0x58 /* X */);
    for (int i = 0; i < seq.size() - 1; ++i)
        sendKey(seq.at(i));
    check(m_aurora == nullptr, "a broken run does not fire");
    sendKey(seq.last());
    check(m_aurora != nullptr, "the completed sequence wakes the aurora");

    // 4) it ends, and leaves nothing behind
    if (m_aurora) {
        m_aurora->dismiss();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    check(m_aurora == nullptr, "dismissing the aurora deletes it");
    check(findChildren<AuroraOverlay *>().isEmpty(),
          "no overlay survives the show");

    if (fail == 0)
        qInfo("castalia-desktop selftest: all checks passed");
    return fail == 0 ? 0 : 1;
}

void DesktopWindow::paintEvent(QPaintEvent *)
{
    if (m_wall.isNull() || m_wall.size() != size())
        bakeWallpaper();

    QPainter p(this);
    if (!m_wallOutgoing.isNull() && m_wallFade < 1.0) {
        p.drawPixmap(0, 0, m_wallOutgoing);
        p.setOpacity(m_wallFade);
    }
    p.drawPixmap(0, 0, m_wall);
    p.setOpacity(1.0);

    if (!m_band.isNull()) {
        // The selection rectangle: accent wash inside, solid accent edge —
        // the same colour language the icons' own selection speaks.
        p.setRenderHint(QPainter::Antialiasing, false);
        QColor accent = m_tokens.color(QStringLiteral("accent"));
        if (!accent.isValid())
            accent = QColor(0x3E, 0x82, 0xB6);
        QColor fill = accent;
        fill.setAlpha(56);
        p.setBrush(fill);
        accent.setAlpha(210);
        p.setPen(QPen(accent, 1));
        p.drawRect(m_band.adjusted(0, 0, -1, -1));
    }
}

void DesktopWindow::mousePressEvent(QMouseEvent *event)
{
    clearSelection();
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton) {
        // Drag on empty desktop = rubber band (§7.5's "select several").
        m_banding = true;
        m_bandOrigin = event->pos();
        m_band = QRect();
    }
    QWidget::mousePressEvent(event);
}

void DesktopWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_banding) {
        m_band = QRect(m_bandOrigin, event->pos()).normalized();
        applyBand(m_band);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void DesktopWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_banding) {
        m_banding = false;
        m_band = QRect();
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void DesktopWindow::applyBand(const QRect &band)
{
    for (DesktopIcon *icon : m_icons)
        icon->setSelected(band.intersects(icon->geometry()));
}

void DesktopWindow::keyPressEvent(QKeyEvent *event)
{
    if (m_konami.feed(event->key())) {
        showAurora();
        return;
    }
    QWidget::keyPressEvent(event);
}

// The aurora (↑ ↑ ↓ ↓ ← → ← → B A, or --easter-egg aurora). One at a time,
// and it takes the wallpaper's own accent so it belongs to the active theme.
void DesktopWindow::showAurora(int seconds)
{
    if (m_aurora)
        return;
    QColor accent = m_tokens.color(QStringLiteral("accent"));
    if (!accent.isValid())
        accent = QColor(0xF5, 0x79, 0x00);
    m_aurora = new AuroraOverlay(accent, seconds, m_repo, this);
    connect(m_aurora, &QObject::destroyed, this,
            [this]() { m_aurora = nullptr; });
    castalia::playSound(m_repo, castalia::Sound::Startup);
}

void DesktopWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);                                   // styled by the QSS
    menu.addAction(tr("Nueva carpeta"), this,
                   &DesktopWindow::newFolder);
    menu.addAction(tr("Pegar"))->setEnabled(false);
    menu.addSeparator();
    menu.addAction(tr("Actualizar"), this,
                   &DesktopWindow::refresh);
    menu.addAction(tr("Abrir carpeta del escritorio"), this,
                   [this]() { launchPath(desktopDir()); });
    menu.addSeparator();
    // Personalize → the Control Center (appearance/theme picker, §9).
    menu.addAction(tr("Personalizar…"), this, [this]() {
        launchApp(QStringLiteral("castalia-control-center"),
                  {QStringLiteral("--repo"), m_repo,
                   QStringLiteral("--theme"), launchTheme()});
    });
    menu.exec(event->globalPos());
}
