// libcastalia-ui — sending a desktop notification (Bible §7.4).
//
// The other half of castalia-notificaciones: this is what a first-party app
// calls when it finishes something the user walked away from. It speaks the
// freedesktop protocol over the session bus, so it works whether our server
// or somebody else's is listening.
//
// Deliberately a *separate* static library from castalia-ui: linking QtDBus
// into all 43 apps to serve the four that announce anything would be a real
// cost on the §16 FLOOR tier. Link `castalia-notify` only where you notify.
#pragma once

#include <QString>

namespace castalia {

// Post a notification. Fire-and-forget: returns false when nothing is
// listening (no session bus, no server) and never blocks the caller for more
// than the bus round trip.
//
// A notification is an *announcement*, not a channel — never make a feature
// depend on one arriving, and never send one for something the user is
// watching happen. `icon` is a name from the shared 48 px family
// ("trash", "update", "camera"); `timeoutMs` of -1 lets the server decide,
// 0 makes it stay until dismissed.
bool notify(const QString &appName, const QString &summary,
            const QString &body = QString(),
            const QString &icon = QString(), int timeoutMs = -1);

} // namespace castalia
