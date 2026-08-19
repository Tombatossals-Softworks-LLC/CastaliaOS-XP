#include "TrayHost.h"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusMetaType>
#include <QDBusServiceWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>

#include "Mark.h"
#include "Theme.h"

namespace {

const char *kWatcherService = "org.kde.StatusNotifierWatcher";
const char *kWatcherPath = "/StatusNotifierWatcher";
const char *kItemInterface = "org.kde.StatusNotifierItem";

// The a(iiay) an SNI item hands over for IconPixmap: width, height and ARGB32
// bytes in network byte order. Plenty of applications ship no named icon at
// all, so without this the tray shows them as blanks.
struct RawPixmap {
    int width = 0;
    int height = 0;
    QByteArray bytes;
};

} // namespace

Q_DECLARE_METATYPE(RawPixmap)
Q_DECLARE_METATYPE(QVector<RawPixmap>)

namespace {

const QDBusArgument &operator>>(const QDBusArgument &arg, RawPixmap &pm)
{
    arg.beginStructure();
    arg >> pm.width >> pm.height >> pm.bytes;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const RawPixmap &pm)
{
    arg.beginStructure();
    arg << pm.width << pm.height << pm.bytes;
    arg.endStructure();
    return arg;
}

// Pick the largest pixmap offered and turn it into something paintable.
QPixmap fromRawPixmaps(const QVariant &value)
{
    QVector<RawPixmap> all;
    const QDBusArgument arg = value.value<QDBusArgument>();
    if (arg.currentType() != QDBusArgument::ArrayType)
        return QPixmap();
    arg.beginArray();
    while (!arg.atEnd()) {
        RawPixmap pm;
        arg >> pm;
        all.append(pm);
    }
    arg.endArray();

    RawPixmap best;
    for (const RawPixmap &pm : all)
        if (pm.width * pm.height > best.width * best.height)
            best = pm;
    if (best.width <= 0 || best.height <= 0
        || best.bytes.size() < best.width * best.height * 4)
        return QPixmap();

    QImage img(best.width, best.height, QImage::Format_ARGB32);
    const uchar *src = reinterpret_cast<const uchar *>(best.bytes.constData());
    for (int y = 0; y < best.height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < best.width; ++x) {
            const uchar *p = src + (y * best.width + x) * 4;
            // network byte order: A R G B
            line[x] = qRgba(p[1], p[2], p[3], p[0]);
        }
    }
    return QPixmap::fromImage(img);
}

} // namespace

// One indicator: the button in the panel plus the D-Bus handle behind it.
struct TrayHost::Item {
    QString service;
    QString path;
    QToolButton *button = nullptr;
    QDBusInterface *iface = nullptr;
};

// ------------------------------------------------------------- watcher ---

// org.kde.StatusNotifierWatcher, hand-written. Applications look for this name
// on the bus and refuse to show an indicator when nobody owns it.
class StatusNotifierWatcherAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems
                   READ registeredStatusNotifierItems)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered
                   READ isStatusNotifierHostRegistered)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion)
public:
    explicit StatusNotifierWatcherAdaptor(TrayHost *host)
        : QDBusAbstractAdaptor(host), m_host(host)
    {
        setAutoRelaySignals(false);
    }

    QStringList registeredStatusNotifierItems() const { return m_registered; }
    bool isStatusNotifierHostRegistered() const { return m_hostRegistered; }
    int protocolVersion() const { return 0; }

public slots:
    void RegisterStatusNotifierItem(const QString &serviceOrPath)
    {
        const QString sender = m_host->currentCallerService();
        const auto split = TrayHost::splitItemService(serviceOrPath, sender);
        if (split.first.isEmpty())
            return;
        const QString key = split.first + split.second;
        if (m_registered.contains(key))
            return;
        m_registered << key;
        // Queued, never direct. Everything addItem() does — AddMatch for the
        // item's change signals, watching its bus name — is itself a blocking
        // round trip, and doing that *inside* this dispatch keeps the calling
        // application (which is blocked on our reply) waiting, and every other
        // application queued behind it. Measured: registrations arriving
        // seconds apart instead of instantly. Reply first, wire up after.
        QMetaObject::invokeMethod(m_host, "addItem", Qt::QueuedConnection,
                                  Q_ARG(QString, split.first),
                                  Q_ARG(QString, split.second));
        emit StatusNotifierItemRegistered(key);
    }

    void RegisterStatusNotifierHost(const QString &service)
    {
        Q_UNUSED(service);
        m_hostRegistered = true;
        emit StatusNotifierHostRegistered();
    }

signals:
    void StatusNotifierItemRegistered(const QString &service);
    void StatusNotifierItemUnregistered(const QString &service);
    void StatusNotifierHostRegistered();

public:
    void forget(const QString &key)
    {
        if (m_registered.removeAll(key) > 0)
            emit StatusNotifierItemUnregistered(key);
    }

private:
    TrayHost *m_host;
    QStringList m_registered;
    bool m_hostRegistered = false;
};

// ---------------------------------------------------------------- host ---

QPair<QString, QString> TrayHost::splitItemService(const QString &argument,
                                                   const QString &sender)
{
    const QString arg = argument.trimmed();
    if (arg.isEmpty())
        return {QString(), QString()};
    if (arg.startsWith(QLatin1Char('/'))) {
        // An object path: the service is whoever called us.
        return {sender, arg};
    }
    // A bus name, with the spec's default object path. Some applications pass
    // "service/path" in one string; honour that too rather than losing them.
    const int slash = arg.indexOf(QLatin1Char('/'));
    if (slash > 0)
        return {arg.left(slash), arg.mid(slash)};
    return {arg, QStringLiteral("/StatusNotifierItem")};
}

QString TrayHost::currentCallerService() const
{
    return calledFromDBus() ? message().service() : QString();
}

QString TrayHost::hostServiceName(qint64 pid)
{
    return QStringLiteral("org.kde.StatusNotifierHost-%1").arg(pid);
}

TrayHost::TrayHost(const QString &repoRoot, QWidget *parent)
    : QWidget(parent), m_repo(repoRoot)
{
    setObjectName(QStringLiteral("TrayHost"));
    m_lay = new QHBoxLayout(this);
    m_lay->setContentsMargins(0, 0, 0, 0);
    m_lay->setSpacing(4);

    qDBusRegisterMetaType<RawPixmap>();
    qDBusRegisterMetaType<QVector<RawPixmap>>();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;                       // a bare X session: no tray, no error

    auto *watcher = new StatusNotifierWatcherAdaptor(this);
    if (!bus.registerObject(QLatin1String(kWatcherPath), this)
        || !bus.registerService(QLatin1String(kWatcherService))) {
        // Another panel or desktop already hosts the tray. Two watchers would
        // fight over every indicator, so we simply do not host one.
        delete watcher;
        return;
    }
    // Claim a host name and tell ourselves about it: applications wait for
    // IsStatusNotifierHostRegistered before they publish an item.
    bus.registerService(hostServiceName(QCoreApplication::applicationPid()));
    watcher->RegisterStatusNotifierHost(
        hostServiceName(QCoreApplication::applicationPid()));

    // Drop an indicator when its application leaves the bus — otherwise the
    // panel keeps a button that does nothing for the rest of the session.
    m_owners = new QDBusServiceWatcher(this);
    m_owners->setConnection(bus);
    m_owners->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_owners, &QDBusServiceWatcher::serviceUnregistered, this,
            [this, watcher](const QString &service) {
                for (const QString &key : m_items.keys()) {
                    if (!key.startsWith(service))
                        continue;
                    watcher->forget(key);
                    removeItem(key);
                }
            });
}

int TrayHost::itemCount() const
{
    return m_items.size();
}

void TrayHost::addItem(const QString &service, const QString &path)
{
    const QString key = service + path;
    if (m_items.contains(key))
        return;

    auto *item = new Item;
    item->service = service;
    item->path = path;
    item->iface = new QDBusInterface(service, path,
                                     QLatin1String(kItemInterface),
                                     QDBusConnection::sessionBus(), this);
    item->button = new QToolButton(this);
    item->button->setObjectName(QStringLiteral("TrayItem"));
    item->button->setAutoRaise(true);
    item->button->setCursor(Qt::PointingHandCursor);
    item->button->setFixedSize(24, 24);
    item->button->setIconSize(QSize(18, 18));
    // Left click activates, right click asks the application for its menu.
    // The coordinates are the button's on screen, which is what an app needs
    // to place a menu next to its own indicator.
    connect(item->button, &QToolButton::clicked, this, [this, key]() {
        Item *it = m_items.value(key);
        if (!it)
            return;
        const QPoint g = it->button->mapToGlobal(QPoint(0, 0));
        it->iface->asyncCall(QStringLiteral("Activate"), g.x(), g.y());
    });
    item->button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(item->button, &QToolButton::customContextMenuRequested, this,
            [this, key]() {
                Item *it = m_items.value(key);
                if (!it)
                    return;
                const QPoint g = it->button->mapToGlobal(QPoint(0, 0));
                it->iface->asyncCall(QStringLiteral("ContextMenu"), g.x(),
                                     g.y());
            });

    m_lay->addWidget(item->button);
    item->button->show();
    m_items.insert(key, item);
    if (m_owners)
        m_owners->addWatchedService(service);

    // Keep up with the application: a new icon, tooltip or status all matter.
    QDBusConnection bus = QDBusConnection::sessionBus();
    for (const char *sig : {"NewIcon", "NewToolTip", "NewStatus", "NewTitle"}) {
        bus.connect(service, path, QLatin1String(kItemInterface),
                    QLatin1String(sig), this, SLOT(onItemChanged()));
    }
    refreshItem(key);
}

void TrayHost::removeItem(const QString &key)
{
    Item *item = m_items.take(key);
    if (!item)
        return;
    if (m_owners)
        m_owners->removeWatchedService(item->service);
    item->button->deleteLater();
    delete item->iface;
    delete item;
}

void TrayHost::onItemChanged()
{
    for (const QString &key : m_items.keys())
        refreshItem(key);
}

// Ask the application for everything at once, asynchronously.
//
// This *must not* be a blocking read. The first version called
// QDBusInterface::property() from inside the RegisterStatusNotifierItem slot,
// which runs while the calling application is itself blocked waiting for our
// reply — a textbook deadlock: the property read sat there for the full 25 s
// timeout, the indicator came up iconless, and every other application trying
// to register queued behind it. Found by running a real SNI app against the
// panel; no unit test would have shown it.
//
// Asynchronous also means a hung indicator cannot freeze the panel, which is
// the other half of why this is right.
void TrayHost::refreshItem(const QString &key)
{
    Item *item = m_items.value(key);
    if (!item)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        item->service, item->path,
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("GetAll"));
    msg << QLatin1String(kItemInterface);
    auto *call = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg, 3000), this);
    connect(call, &QDBusPendingCallWatcher::finished, this,
            [this, key](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                const QDBusPendingReply<QVariantMap> reply = *w;
                if (!reply.isError())
                    applyProperties(key, reply.value());
            });
}

void TrayHost::applyProperties(const QString &key, const QVariantMap &props)
{
    Item *item = m_items.value(key);
    if (!item)
        return;
    // a{sv} arrives with each value wrapped in a QDBusVariant.
    auto unwrap = [&props](const char *name) {
        const QVariant v = props.value(QLatin1String(name));
        return v.canConvert<QDBusVariant>() ? v.value<QDBusVariant>().variant()
                                            : v;
    };

    const QString title = unwrap("Title").toString();
    const QString id = unwrap("Id").toString();
    const QString status = unwrap("Status").toString();
    item->button->setToolTip(title.isEmpty() ? id : title);

    // "Passive" is the application asking to be hidden; honour it rather than
    // showing a dead icon.
    item->button->setVisible(status != QLatin1String("Passive"));

    QIcon icon;
    const QString iconName = unwrap("IconName").toString();
    if (!iconName.isEmpty()) {
        icon = castalia::themeIcon(m_repo, iconName);        // our family
        if (icon.isNull())
            icon = QIcon::fromTheme(iconName);               // the system's
    }
    if (icon.isNull()) {
        const QPixmap raw = fromRawPixmaps(unwrap("IconPixmap"));
        if (!raw.isNull())
            icon = QIcon(raw);
    }
    if (icon.isNull()) {
        // Never leave a blank square: an indicator the user cannot see is an
        // indicator they cannot click.
        QPixmap pm(18, 18);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        castalia::drawMark(&p, QRectF(0, 0, 18, 18), m_repo);
        icon = QIcon(pm);
    }
    // Rasterise at the size we want rather than handing the button an icon
    // and hoping: inside a stylesheet-drawn QToolButton the icon was coming
    // out a third of its size, which on a 24 px tray reads as a speck.
    item->button->setIcon(QIcon(icon.pixmap(18, 18)));
}

#include "TrayHost.moc"
