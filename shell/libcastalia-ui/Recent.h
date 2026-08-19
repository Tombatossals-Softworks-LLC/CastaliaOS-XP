// libcastalia-ui — the recent-documents list (Bible §7.9).
//
// "freedesktop `recently-used` list, surfaced in the menu and Explorer;
// per-user, clearable, local-only."
//
// The store is the standard `~/.local/share/recently-used.xbel`, so Castalia's
// recent documents *are* the desktop's: a file opened in our text editor shows
// up in a GTK app's recent list and the other way round. That interop is the
// whole reason to use somebody else's format instead of inventing one.
//
// Local-only and clearable by design (§P7): nothing leaves the machine, and
// clear() really removes the file rather than hiding it.
#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace castalia {
namespace recent {

struct Entry {
    QString path;        // absolute filesystem path
    QString mime;        // MIME type, when the recorder knew one
    QString appName;     // the application that last opened it
    QDateTime visited;   // when it did
};

// The recent list, newest first. `max` caps the result; entries whose file has
// since disappeared are skipped — a recent list that offers dead paths is
// worse than a short one.
QVector<Entry> list(int max = 20);

// Record that `appName` opened `path`. An existing entry for the same file is
// moved to the front rather than duplicated. Returns false if the store could
// not be written.
bool add(const QString &path, const QString &appName,
         const QString &mime = QString());

// Forget everything. XP had this exact command in its Start Menu, and so do
// we — a recent list you cannot clear is a log you did not ask for.
bool clear();

// The store's location (`XDG_DATA_HOME/recently-used.xbel`). Exposed for the
// self-test and for anything that wants to watch it.
QString storePath();

// How many entries are kept on disk. Older ones fall off the end.
int cap();

} // namespace recent
} // namespace castalia
