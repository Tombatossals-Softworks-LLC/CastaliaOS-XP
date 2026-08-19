// castalia-notificaciones — the notification server (Bible §7.4).
//
// "A light org.freedesktop.Notifications server (our own, Qt) — corner toasts,
// a small history, per-app mute. No cloud, no account."
//
// Until now nothing in the shell could tell the user anything: an update
// finished, a disk was plugged in, a background copy failed — all silent. This
// is the piece that speaks, and it speaks the freedesktop protocol, so
// *anything* on the system (apt hooks, a third-party app, `notify-send`) can
// reach the user through it.
//
// Toasts stack in the corner above the panel, slide in (≤200 ms, and not at
// all under reduce-motion), expire on their own, and dismiss on click. Every
// one is appended to a small on-disk history, and an app the user has muted
// goes to the history *without* a toast — muting hides the interruption, never
// the record.
//
// Usage:
//   castalia-notificaciones                       # run the server
//   castalia-notificaciones --send "Título" --body "…" [--app NOMBRE]
//   castalia-notificaciones --historial           # the history window
//   castalia-notificaciones --mute NOMBRE | --unmute NOMBRE
//   castalia-notificaciones --demo                # two sample toasts
//   castalia-notificaciones --selftest            # head-less gate

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusAbstractAdaptor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <cstdio>

#include "Mark.h"
#include "Sound.h"
#include "Theme.h"
#include "ThemeTokens.h"

namespace {

// ---------------------------------------------------------------- model ---

struct Note {
    uint id = 0;
    QString app;
    QString summary;
    QString body;
    QString icon;
    QDateTime when;
};

// The spec's expire_timeout: -1 means "server decides", 0 means "until the
// user dismisses it". Anything else is milliseconds, clamped to something a
// human can actually read but not so long it becomes furniture.
int resolveTimeout(int requested)
{
    if (requested == 0)
        return 0;                          // sticky
    if (requested < 0)
        return 6000;                       // our default
    return qBound(2000, requested, 30000);
}

QString configPath()
{
    return QStandardPaths::writableLocation(
               QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/castalia/notificaciones.conf");
}

QString historyPath()
{
    return QStandardPaths::writableLocation(
               QStandardPaths::GenericDataLocation)
           + QStringLiteral("/castalia/notificaciones.tsv");
}

// The muted-app list, one `mute = "app"` line per app (the same flat-TOML
// shape every other Castalia config uses).
QStringList mutedApps()
{
    QStringList out;
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    for (const QByteArray &raw : f.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (!line.startsWith(QStringLiteral("mute")))
            continue;
        const int q1 = line.indexOf(QLatin1Char('"'));
        const int q2 = line.indexOf(QLatin1Char('"'), q1 + 1);
        if (q1 >= 0 && q2 > q1)
            out << line.mid(q1 + 1, q2 - q1 - 1);
    }
    return out;
}

bool isMuted(const QString &app, const QStringList &muted)
{
    for (const QString &m : muted)
        if (QString::compare(m, app, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

bool setMuted(const QString &app, bool mute)
{
    QStringList list = mutedApps();
    const bool already = isMuted(app, list);
    if (mute == already)
        return true;
    if (mute) {
        list << app;
    } else {
        QStringList kept;
        for (const QString &m : list)
            if (QString::compare(m, app, Qt::CaseInsensitive) != 0)
                kept << m;
        list = kept;
    }
    QDir().mkpath(QFileInfo(configPath()).absolutePath());
    QFile f(configPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    QTextStream out(&f);
    out << "# Castalia — notificaciones (Bible §7.4).\n"
        << "# Una línea `mute = \"app\"` por aplicación silenciada: sus avisos\n"
        << "# van al historial sin mostrar burbuja.\n";
    for (const QString &m : list)
        out << "mute = \"" << m << "\"\n";
    return true;
}

// The history is a tab-separated log — greppable from a terminal, trivially
// parseable here, and capped so it can never grow without bound.
const int kHistoryCap = 200;

QString flatten(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('\t'), QLatin1Char(' '));
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return out.trimmed();
}

void appendHistory(const Note &note)
{
    const QString path = historyPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QStringList lines;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lines = QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'),
                                                      Qt::SkipEmptyParts);
        in.close();
    }
    lines << QStringLiteral("%1\t%2\t%3\t%4")
                 .arg(note.when.toString(Qt::ISODate), flatten(note.app),
                      flatten(note.summary), flatten(note.body));
    while (lines.size() > kHistoryCap)
        lines.removeFirst();
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return;
    QTextStream ts(&out);
    for (const QString &l : lines)
        ts << l << '\n';
}

// Newest first — the order a person reads a history in.
QVector<Note> readHistory()
{
    QVector<Note> out;
    QFile f(historyPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    const QStringList lines = QString::fromUtf8(f.readAll())
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QStringList f4 = lines.at(i).split(QLatin1Char('\t'));
        if (f4.size() < 3)
            continue;
        Note n;
        n.when = QDateTime::fromString(f4.at(0), Qt::ISODate);
        n.app = f4.at(1);
        n.summary = f4.at(2);
        n.body = f4.value(3);
        out.append(n);
    }
    return out;
}

// ---------------------------------------------------------------- toast ---

// One width for every toast: a stack of ragged-width cards reads as clutter,
// and a fixed width is what lets the body text wrap predictably.
const int kToastWidth = 340;

class Toast : public QWidget {
    Q_OBJECT
public:
    Toast(const Note &note, const ThemeTokens &tokens, const QString &repo,
          QWidget *parent = nullptr)
        : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint),
          m_id(note.id), m_tokens(tokens)
    {
        // A real notification window: the WM keeps it above, undecorated, and
        // — crucially — it never takes focus away from what you were doing.
        setAttribute(Qt::WA_X11NetWmWindowTypeNotification, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setAttribute(Qt::WA_DeleteOnClose, true);
        setCursor(Qt::PointingHandCursor);
        setFixedWidth(kToastWidth);

        auto *root = new QHBoxLayout(this);
        root->setContentsMargins(14, 10, 12, 10);
        root->setSpacing(10);

        auto *art = new QLabel(this);
        art->setFixedSize(32, 32);
        QPixmap pm = castalia::themeIcon(repo, note.icon).pixmap(32, 32);
        if (pm.isNull()) {
            pm = QPixmap(32, 32);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            castalia::drawMark(&p, QRectF(0, 0, 32, 32), repo);
        }
        art->setPixmap(pm);
        root->addWidget(art, 0, Qt::AlignTop);

        auto *text = new QVBoxLayout;
        text->setSpacing(2);
        auto *sum = new QLabel(note.summary, this);
        sum->setObjectName(QStringLiteral("ToastSummary"));
        sum->setWordWrap(true);
        text->addWidget(sum);
        if (!note.body.isEmpty()) {
            auto *bod = new QLabel(note.body, this);
            bod->setObjectName(QStringLiteral("ToastBody"));
            bod->setWordWrap(true);
            bod->setProperty("secondary", true);
            text->addWidget(bod);
        }
        auto *from = new QLabel(note.app, this);
        from->setObjectName(QStringLiteral("ToastApp"));
        from->setProperty("secondary", true);
        text->addWidget(from);
        root->addLayout(text, 1);

        // Word-wrapped labels only know their height once the layout has run
        // at the final width, so activate it first and then ask. adjustSize()
        // on a widget that has never been shown answers with nonsense.
        root->activate();
        setFixedHeight(qMax(72, sizeHint().height()));
    }

    uint noteId() const { return m_id; }

    // Slide in from the right edge and fade up. `target` is where the toast
    // ends up; the animation only ever moves it horizontally, so a re-stack
    // while one is arriving cannot fight the animation vertically.
    void appearAt(const QPoint &target)
    {
        move(target);
        if (castalia::reduceMotion()) {
            show();
            return;
        }
        const QPoint from = target + QPoint(40, 0);
        move(from);
        setWindowOpacity(0.0);
        show();
        auto *slide = new QPropertyAnimation(this, "pos", this);
        slide->setDuration(180);
        slide->setStartValue(from);
        slide->setEndValue(target);
        slide->setEasingCurve(QEasingCurve::OutCubic);
        slide->start(QAbstractAnimation::DeleteWhenStopped);
        auto *fade = new QPropertyAnimation(this, "windowOpacity", this);
        fade->setDuration(180);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    }

    void slideTo(const QPoint &target)
    {
        if (castalia::reduceMotion() || !isVisible()) {
            move(target);
            return;
        }
        auto *anim = new QPropertyAnimation(this, "pos", this);
        anim->setDuration(150);
        anim->setStartValue(pos());
        anim->setEndValue(target);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

signals:
    void dismissed(uint id);

protected:
    void mousePressEvent(QMouseEvent *) override { emit dismissed(m_id); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal rad = qMax(2, m_tokens.cornerRadius());
        QColor surface = m_tokens.color(QStringLiteral("surface"));
        QColor border = m_tokens.color(QStringLiteral("border"));
        QColor accent = m_tokens.color(QStringLiteral("accent"));
        const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);

        p.setPen(QPen(border, 1));
        p.setBrush(surface);
        p.drawRoundedRect(r, rad, rad);
        // the accent bar down the leading edge — the same language the Start
        // Menu's hover and the taskbar's active window already speak
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(QRectF(r.left() + 1, r.top() + 1, 4,
                                 r.height() - 2), 2, 2);
    }

private:
    uint m_id;
    ThemeTokens m_tokens;
};

// ---------------------------------------------------------------- server ---

class Server : public QObject {
    Q_OBJECT
public:
    Server(const ThemeTokens &tokens, const QString &repo, QObject *parent)
        : QObject(parent), m_tokens(tokens), m_repo(repo) {}

    // The whole point, in one function: record it, decide whether to show it,
    // and show it if so. Returns the id the caller can close it by.
    uint post(const Note &incoming, int timeoutMs)
    {
        Note note = incoming;
        if (note.id == 0)
            note.id = ++m_lastId;
        else
            m_lastId = qMax(m_lastId, note.id);
        note.when = QDateTime::currentDateTime();
        if (note.summary.isEmpty())
            note.summary = QStringLiteral("(sin título)");
        appendHistory(note);
        if (isMuted(note.app, mutedApps()))
            return note.id;               // recorded, not shown

        auto *toast = new Toast(note, m_tokens, m_repo);
        connect(toast, &Toast::dismissed, this, [this](uint id) { close(id); });
        m_toasts.append(toast);
        restack(toast);            // the new one slides in, the rest shuffle up
        castalia::playSound(m_repo, castalia::Sound::Notify);

        const int ms = resolveTimeout(timeoutMs);
        if (ms > 0) {
            const uint id = note.id;
            QTimer::singleShot(ms, this, [this, id]() { close(id); });
        }
        return note.id;
    }

    void close(uint id)
    {
        for (int i = 0; i < m_toasts.size(); ++i) {
            if (m_toasts.at(i)->noteId() != id)
                continue;
            Toast *t = m_toasts.takeAt(i);
            t->close();
            restack();
            emit closed(id, 2);            // 2 = dismissed by the user/expiry
            return;
        }
    }

    int visibleCount() const { return m_toasts.size(); }

    // Where each toast sits, oldest first — stacked upward from the corner,
    // clear of the panel's strut. A pure function of the heights (toasts are
    // not all the same height: a two-line body is taller than a one-line one),
    // so the self-test checks the real algorithm rather than a simplified
    // cousin of it.
    QVector<QPoint> layoutFor(const QVector<int> &heights) const
    {
        const QRect screen = m_screen.isValid()
            ? m_screen
            : (QGuiApplication::primaryScreen()
                   ? QGuiApplication::primaryScreen()->availableGeometry()
                   : QRect(0, 0, 1024, 768));
        const int margin = 12, gap = 8;
        const int x = screen.right() - kToastWidth - margin + 1;
        int y = screen.bottom() - m_tokens.panelHeight() - margin + 1;
        QVector<QPoint> out(heights.size());
        // Walk newest → oldest, bottom → top: the newest arrival is the one
        // nearest the corner, and older ones are pushed up by its height.
        for (int i = heights.size() - 1; i >= 0; --i) {
            y -= heights.at(i);
            out[i] = QPoint(x, y);
            y -= gap;
        }
        return out;
    }

    void setScreenForTest(const QRect &r) { m_screen = r; }

signals:
    void closed(uint id, uint reason);

private:
    void restack(Toast *entering = nullptr)
    {
        QVector<int> heights;
        heights.reserve(m_toasts.size());
        for (Toast *t : m_toasts)
            heights.append(t->height());
        const QVector<QPoint> places = layoutFor(heights);
        for (int i = 0; i < m_toasts.size(); ++i) {
            if (m_toasts.at(i) == entering)
                m_toasts.at(i)->appearAt(places.at(i));
            else
                m_toasts.at(i)->slideTo(places.at(i));
        }
    }

    ThemeTokens m_tokens;
    QString m_repo;
    QVector<Toast *> m_toasts;
    QRect m_screen;
    uint m_lastId = 0;
};

// The freedesktop interface, hand-written (no codegen step in the build).
class NotificationsAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    explicit NotificationsAdaptor(Server *server)
        : QDBusAbstractAdaptor(server), m_server(server)
    {
        connect(server, &Server::closed, this,
                &NotificationsAdaptor::NotificationClosed);
    }

public slots:
    uint Notify(const QString &app_name, uint replaces_id,
                const QString &app_icon, const QString &summary,
                const QString &body, const QStringList &actions,
                const QVariantMap &hints, int expire_timeout)
    {
        Q_UNUSED(actions);
        Q_UNUSED(hints);
        Note n;
        n.id = replaces_id;
        n.app = app_name.isEmpty() ? QStringLiteral("Sistema") : app_name;
        n.icon = app_icon;
        n.summary = summary;
        n.body = body;
        return m_server->post(n, expire_timeout);
    }

    void CloseNotification(uint id) { m_server->close(id); }

    QStringList GetCapabilities()
    {
        // Only what we really do. Claiming "actions" without buttons would
        // make callers hide their own fallback UI and lose the interaction.
        return {QStringLiteral("body"), QStringLiteral("icon-static"),
                QStringLiteral("persistence")};
    }

    QString GetServerInformation(QString &vendor, QString &version,
                                 QString &spec_version)
    {
        vendor = QStringLiteral("Tombatossals Softworks");
        version = QStringLiteral(CASTALIA_VERSION);
        spec_version = QStringLiteral("1.2");
        return QStringLiteral("Castalia Notificaciones");
    }

signals:
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString &action_key);

private:
    Server *m_server;
};

// --------------------------------------------------------------- history ---

class Historial : public QWidget {
    Q_OBJECT
public:
    Historial(const ThemeTokens &tokens, const QString &repo,
              const QVector<Note> &notes)
        : m_repo(repo)
    {
        Q_UNUSED(tokens);
        setWindowTitle(QStringLiteral("Historial de notificaciones"));
        resize(520, 420);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(14, 14, 14, 14);
        root->setSpacing(8);

        auto *hint = new QLabel(
            QStringLiteral("Los últimos avisos del sistema. Silenciar una "
                           "aplicación oculta su burbuja, no su registro."),
            this);
        hint->setProperty("secondary", true);
        hint->setWordWrap(true);
        root->addWidget(hint);

        m_list = new QListWidget(this);
        root->addWidget(m_list, 1);
        const QStringList muted = mutedApps();
        for (const Note &n : notes) {
            const QString when =
                n.when.isValid()
                    ? n.when.toString(QStringLiteral("dd/MM HH:mm"))
                    : QString();
            QString line = QStringLiteral("%1  ·  %2").arg(when, n.summary);
            if (!n.body.isEmpty())
                line += QStringLiteral(" — %1").arg(n.body);
            line += QStringLiteral("   [%1]").arg(n.app);
            if (isMuted(n.app, muted))
                line += QStringLiteral(" (silenciada)");
            auto *item = new QListWidgetItem(
                castalia::themeIcon(repo, n.icon), line, m_list);
            item->setData(Qt::UserRole, n.app);
        }
        if (notes.isEmpty())
            m_list->addItem(QStringLiteral("Todavía no hay notificaciones."));

        auto *row = new QHBoxLayout;
        m_mute = new QPushButton(QStringLiteral("Silenciar esta aplicación"),
                                 this);
        m_mute->setEnabled(false);
        connect(m_list, &QListWidget::currentItemChanged, this,
                [this](QListWidgetItem *cur) { updateMuteButton(cur); });
        connect(m_mute, &QPushButton::clicked, this, [this]() { toggleMute(); });
        row->addWidget(m_mute);
        row->addStretch(1);
        auto *close = new QPushButton(QStringLiteral("Cerrar"), this);
        close->setDefault(true);
        connect(close, &QPushButton::clicked, this, &QWidget::close);
        row->addWidget(close);
        root->addLayout(row);

        if (m_list->count() > 0)
            m_list->setCurrentRow(0);
    }

private:
    void updateMuteButton(QListWidgetItem *cur)
    {
        const QString app = cur ? cur->data(Qt::UserRole).toString() : QString();
        m_mute->setEnabled(!app.isEmpty());
        m_mute->setText(isMuted(app, mutedApps())
                            ? QStringLiteral("Reactivar «%1»").arg(app)
                            : QStringLiteral("Silenciar «%1»").arg(app));
    }

    void toggleMute()
    {
        QListWidgetItem *cur = m_list->currentItem();
        if (!cur)
            return;
        const QString app = cur->data(Qt::UserRole).toString();
        if (app.isEmpty())
            return;
        setMuted(app, !isMuted(app, mutedApps()));
        updateMuteButton(cur);
    }

    QString m_repo;
    QListWidget *m_list = nullptr;
    QPushButton *m_mute = nullptr;
};

// -------------------------------------------------------------- selftest ---

int g_fail = 0;
void check(bool ok, const char *what)
{
    if (!ok) {
        ++g_fail;
        qWarning("notificaciones selftest FAIL: %s", what);
    }
}

int selftest()
{
    // 1) the spec's timeout rules
    check(resolveTimeout(-1) == 6000, "default timeout");
    check(resolveTimeout(0) == 0, "zero means sticky");
    check(resolveTimeout(4000) == 4000, "explicit timeout is honoured");
    check(resolveTimeout(50) == 2000, "absurdly short is clamped up");
    check(resolveTimeout(999999) == 30000, "absurdly long is clamped down");

    // 2) mute matching is by name, case-insensitively
    const QStringList muted = {QStringLiteral("Centro de software")};
    check(isMuted(QStringLiteral("centro de software"), muted),
          "mute ignores case");
    check(!isMuted(QStringLiteral("Centro de control"), muted),
          "mute does not match a different app");
    check(!isMuted(QString(), muted), "an unnamed app is not muted");

    // 3) history: append, cap, read newest-first
    QTemporaryDir home;
    check(home.isValid(), "temp HOME");
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/.local/share").toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
    for (int i = 0; i < kHistoryCap + 5; ++i) {
        Note n;
        n.app = QStringLiteral("Prueba");
        n.summary = QStringLiteral("aviso %1").arg(i);
        n.body = QStringLiteral("línea\ncon salto\ty tab");
        n.when = QDateTime::currentDateTime();
        appendHistory(n);
    }
    const QVector<Note> hist = readHistory();
    check(hist.size() == kHistoryCap, "history is capped");
    check(hist.first().summary
              == QStringLiteral("aviso %1").arg(kHistoryCap + 4),
          "history reads newest first");
    check(!hist.first().body.contains(QLatin1Char('\t'))
              && !hist.first().body.contains(QLatin1Char('\n')),
          "tabs and newlines cannot break a record");

    // 4) mute round-trips through the config file
    check(setMuted(QStringLiteral("Prueba"), true), "mute writes");
    check(isMuted(QStringLiteral("Prueba"), mutedApps()), "mute persists");
    check(setMuted(QStringLiteral("Prueba"), false), "unmute writes");
    check(!isMuted(QStringLiteral("Prueba"), mutedApps()), "unmute persists");

    // 5) the stack geometry: clear of the panel, no overlaps, newest lowest —
    //    including the case that first shipped broken, toasts of unequal height
    ThemeTokens tokens;
    Server server(tokens, QStringLiteral("."), nullptr);
    server.setScreenForTest(QRect(0, 0, 1024, 768));
    const int panel = tokens.panelHeight();
    const QVector<int> heights = {110, 72, 96};        // oldest → newest
    const QVector<QPoint> places = server.layoutFor(heights);
    check(places.size() == 3, "one place per toast");
    check(places.at(2).y() + heights.at(2) <= 768 - panel,
          "the newest toast clears the panel");
    for (int i = 0; i < 2; ++i)
        check(places.at(i).y() + heights.at(i) < places.at(i + 1).y(),
              "each toast sits clear above the next");
    check(places.at(0).x() + kToastWidth <= 1024, "toasts stay on screen");
    check(places.at(0).x() == places.at(2).x(), "the stack shares one edge");
    check(server.layoutFor({}).isEmpty(), "an empty stack lays out to nothing");

    if (g_fail == 0)
        qInfo("castalia-notificaciones selftest: all checks passed");
    return g_fail == 0 ? 0 : 1;
}

void addOptions(QCommandLineParser &cli)
{
    cli.setApplicationDescription(
        QStringLiteral("Servidor de notificaciones de Castalia"));
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("human")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("send"),
                   QStringLiteral("Post a notification and exit"),
                   QStringLiteral("summary")});
    cli.addOption({QStringLiteral("body"),
                   QStringLiteral("Body text for --send"),
                   QStringLiteral("text")});
    cli.addOption({QStringLiteral("app"),
                   QStringLiteral("Application name for --send"),
                   QStringLiteral("name"), QStringLiteral("Castalia")});
    cli.addOption({QStringLiteral("icon"),
                   QStringLiteral("Icon name from the 48 px family"),
                   QStringLiteral("name")});
    cli.addOption({QStringLiteral("historial"),
                   QStringLiteral("Show the notification history")});
    cli.addOption({QStringLiteral("mute"),
                   QStringLiteral("Silence an application"),
                   QStringLiteral("name")});
    cli.addOption({QStringLiteral("unmute"),
                   QStringLiteral("Un-silence an application"),
                   QStringLiteral("name")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Show two sample toasts (no bus needed)")});
    cli.addOption({QStringLiteral("selftest"),
                   QStringLiteral("Run the head-less gate")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
}

} // namespace

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) != "--selftest")
            continue;
        QCoreApplication app(argc, argv);
        QCommandLineParser cli;
        addOptions(cli);
        cli.process(app);
        return selftest();
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(
        QStringLiteral("castalia-notificaciones"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    addOptions(cli);
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo"))).absolutePath();
    const ThemeTokens tokens =
        castalia::applyTheme(&app, repo, cli.value(QStringLiteral("theme")),
                             QStringLiteral(R"(
#ToastSummary { font-weight: bold; }
#ToastApp { font-size: 10px; }
)"));

    // --mute / --unmute: a one-shot config edit, no window.
    for (const auto &pair : {std::make_pair("mute", true),
                             std::make_pair("unmute", false)}) {
        const QString name = cli.value(QLatin1String(pair.first));
        if (name.isEmpty())
            continue;
        const bool ok = setMuted(name, pair.second);
        std::printf("castalia-notificaciones: %s %s\n",
                    pair.second ? "silenciada" : "reactivada",
                    qPrintable(name));
        return ok ? 0 : 1;
    }

    if (cli.isSet(QStringLiteral("historial"))) {
        auto *win = new Historial(tokens, repo, readHistory());
        win->show();
        const QString shot = cli.value(QStringLiteral("screenshot"));
        if (!shot.isEmpty())
            QTimer::singleShot(150, &app, [&, win]() {
                win->grab().save(shot);
                app.quit();
            });
        return app.exec();
    }

    // --send: hand it to the running server over the bus. Falling back to
    // "show it ourselves" would put a toast on screen that no server knows
    // about, so instead we say plainly that nobody is listening.
    const QString summary = cli.value(QStringLiteral("send"));
    if (!summary.isEmpty()) {
        QDBusInterface iface(QStringLiteral("org.freedesktop.Notifications"),
                             QStringLiteral("/org/freedesktop/Notifications"),
                             QStringLiteral("org.freedesktop.Notifications"),
                             QDBusConnection::sessionBus());
        if (!iface.isValid()) {
            std::fprintf(stderr, "castalia-notificaciones: no hay servidor de "
                                 "notificaciones en el bus de sesión\n");
            return 1;
        }
        const QDBusReply<uint> reply = iface.call(
            QStringLiteral("Notify"), cli.value(QStringLiteral("app")), 0u,
            cli.value(QStringLiteral("icon")), summary,
            cli.value(QStringLiteral("body")), QStringList(), QVariantMap(),
            -1);
        if (!reply.isValid()) {
            std::fprintf(stderr, "castalia-notificaciones: %s\n",
                         qPrintable(reply.error().message()));
            return 1;
        }
        std::printf("castalia-notificaciones: enviada (id %u)\n", reply.value());
        return 0;
    }

    auto *server = new Server(tokens, repo, &app);
    new NotificationsAdaptor(server);

    const bool demo = cli.isSet(QStringLiteral("demo"));
    if (!demo) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        const bool object = bus.registerObject(
            QStringLiteral("/org/freedesktop/Notifications"), server);
        const bool name = bus.registerService(
            QStringLiteral("org.freedesktop.Notifications"));
        if (!object || !name) {
            // Another server owns the name, or there is no session bus at all
            // (a bare X session, the CI renderer). Say which, and stop —
            // two notification servers is worse than none.
            std::fprintf(stderr, "castalia-notificaciones: no se pudo tomar "
                                 "org.freedesktop.Notifications (%s)\n",
                         qPrintable(bus.lastError().message().isEmpty()
                                        ? QStringLiteral("¿ya hay otro "
                                                         "servidor?")
                                        : bus.lastError().message()));
            return 2;
        }
        std::printf("castalia-notificaciones: escuchando en "
                    "org.freedesktop.Notifications\n");
    }

    if (demo) {
        Note a;
        a.app = QStringLiteral("Centro de actualizaciones");
        a.icon = QStringLiteral("update");
        a.summary = QStringLiteral("Actualizaciones listas");
        a.body = QStringLiteral("7 paquetes, 24 MB. Se instalarán al apagar.");
        Note b;
        b.app = QStringLiteral("Papelera de reciclaje");
        b.icon = QStringLiteral("trash");
        b.summary = QStringLiteral("Papelera vaciada");
        b.body = QStringLiteral("Se liberaron 412 MB.");
        server->post(a, 0);
        server->post(b, 0);
    }

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty()) {
        // Compose the toasts onto a screen-sized frame: they are separate
        // top-level windows, so grabbing one would only ever show one.
        QTimer::singleShot(200, &app, [&]() {
            // The frame is the screen, not a guess: the toasts anchor to the
            // real corner, so a fixed-size canvas would crop or float them.
            const QSize screen = QGuiApplication::primaryScreen()
                ? QGuiApplication::primaryScreen()->geometry().size()
                : QSize(1024, 768);
            QImage frame(screen, QImage::Format_ARGB32_Premultiplied);
            frame.fill(QColor(0x2C, 0x66, 0x99));
            QPainter p(&frame);
            const auto windows = QApplication::topLevelWidgets();
            for (QWidget *w : windows) {
                if (!w->isVisible())
                    continue;
                p.drawPixmap(w->pos(), w->grab());
            }
            p.end();
            frame.save(shot);
            app.quit();
        });
    }

    return app.exec();
}

#include "main.moc"
