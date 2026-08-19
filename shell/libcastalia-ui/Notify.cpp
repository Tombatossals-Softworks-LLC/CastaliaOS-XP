#include "Notify.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QStringList>
#include <QVariantMap>

namespace castalia {

bool notify(const QString &appName, const QString &summary,
            const QString &body, const QString &icon, int timeoutMs)
{
    if (summary.isEmpty())
        return false;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;                      // no session bus: a plain X session
    QDBusInterface iface(QStringLiteral("org.freedesktop.Notifications"),
                         QStringLiteral("/org/freedesktop/Notifications"),
                         QStringLiteral("org.freedesktop.Notifications"), bus);
    if (!iface.isValid())
        return false;                      // nobody owns the name
    // The call is synchronous only as far as the bus: the server appends to
    // its history and returns an id without painting anything first, so this
    // costs a round trip, not a toast's lifetime.
    const QDBusReply<uint> reply =
        iface.call(QStringLiteral("Notify"), appName, 0u, icon, summary, body,
                   QStringList(), QVariantMap(), timeoutMs);
    return reply.isValid();
}

} // namespace castalia
