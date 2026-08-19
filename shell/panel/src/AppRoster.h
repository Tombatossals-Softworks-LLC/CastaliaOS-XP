// Castalia shell — the first-party application roster.
//
// One table, two readers. The launch menu builds its grouped list from it
// (§7.3) and the Alt+Tab switcher looks up the icon for a window whose
// program publishes none of its own (§7.6). Keeping it in one place is the
// point: the same binary must not wear one icon in the menu and another in
// the switcher, and a typo in an icon name must fail a gate rather than
// silently render a blank square in two different places
// (`tools/tests/test_app_roster.py`).
#pragma once

#include <QString>
#include <QVector>

namespace castalia {
namespace apps {

// One launchable task. `args` lets a single binary appear as the right task:
// the notification server's menu entry opens its history, it does not start a
// second server (the session already runs one).
struct Entry {
    const char *label;
    const char *bin;
    const char *icon;          // name in the shared 48 px family (§8.4)
    const char *args = nullptr;
    // Extra words the menu search should match. The label is not always what
    // someone types: nobody looks for the Network Center by typing "centro",
    // they type "wifi" (§7.3, §7.8).
    const char *keywords = nullptr;
};

// A menu category and the tasks under it, in display order.
struct Group {
    const char *title;
    QVector<Entry> entries;
};

// The roster, grouped by category (§9 / the menu taxonomy) so the growing app
// list stays navigable.
const QVector<Group> &roster();

// The icon name a binary wears, or an empty string when the binary is not
// ours. Pure, and pinned by the panel's self-test — the switcher's fallback
// path runs through it for every window on screen.
QString iconForBinary(const QString &bin);

// The label and the category title **in the interface language**. The table
// above stores the Spanish source strings (marked with QT_TRANSLATE_NOOP, so
// lupdate collects them into the "AppRoster" context); these two translate
// them at the moment of use, which is the only moment a translator is
// guaranteed to be installed — the table is a static built once, possibly
// before the application object exists (§7.13).
QString label(const Entry &entry);
QString title(const Group &group);

} // namespace apps
} // namespace castalia
