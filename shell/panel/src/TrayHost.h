// Castalia shell — the system tray (Bible §7.4).
//
// "StatusNotifierItem + XEmbed fallback (old apps). Hosts network, volume,
// battery, update, and app indicators."
//
// This is the StatusNotifierItem half: the panel owns
// `org.kde.StatusNotifierWatcher` and acts as the host, so any application
// using SNI — libappindicator/libayatana, KDE apps, Telegram, Nextcloud,
// Qt's own QSystemTrayIcon on a DBus-capable desktop — gets an indicator in
// our panel. Until now the panel had a tray *frame* and hosted nothing.
//
// The XEmbed fallback for pre-SNI applications is not here yet; it needs X
// window reparenting rather than D-Bus and is tracked as the remaining half
// of §7.4.
#pragma once

#include <QDBusContext>
#include <QHash>
#include <QVariantMap>
#include <QPair>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QDBusServiceWatcher;

// QDBusContext belongs on the *exported* object, not on the adaptor: Qt sets
// the call context on whatever was passed to registerObject(), so an adaptor
// that inherits it and calls message() dereferences a null context and takes
// the panel down with it. (It did, on the first live registration.)
class TrayHost : public QWidget, protected QDBusContext {
    Q_OBJECT
public:
    explicit TrayHost(const QString &repoRoot, QWidget *parent = nullptr);

    // How many indicators are showing. Used by the panel's self-test.
    int itemCount() const;

    // Split what an application passes to RegisterStatusNotifierItem into the
    // bus service and object path it really means.
    //
    // The spec allows two shapes and applications use both: a bus name
    // ("org.kde.StatusNotifierItem-1234-1", object path assumed to be
    // /StatusNotifierItem) or a bare object path ("/org/ayatana/NotificationItem/x"),
    // in which case the service is the *sender's* unique name. Getting this
    // wrong means the indicator silently never appears, so it is a pure
    // function with a self-test rather than a branch buried in a slot.
    static QPair<QString, QString> splitItemService(const QString &argument,
                                                    const QString &sender);

    // The bus name this host claims, for a given process id.
    static QString hostServiceName(qint64 pid);

    // The unique bus name of whoever is calling us right now, or empty when
    // we are not inside a D-Bus call. The watcher adaptor needs it to resolve
    // a bare object path to a service.
    QString currentCallerService() const;

private slots:
    void addItem(const QString &service, const QString &path);
    void removeItem(const QString &key);
    // An item told us something changed. The signal carries no identity, so
    // every indicator is re-read — there are a handful of them, and a wrong
    // guess would leave a stale icon on the panel for the whole session.
    void onItemChanged();

private:
    // Ask an item for its properties (asynchronously — see the .cpp) and
    // apply what comes back.
    void refreshItem(const QString &key);
    void applyProperties(const QString &key, const QVariantMap &props);

    struct Item;
    QString m_repo;
    QHBoxLayout *m_lay = nullptr;
    QHash<QString, Item *> m_items;
    QDBusServiceWatcher *m_owners = nullptr;

    friend class StatusNotifierWatcherAdaptor;
};
