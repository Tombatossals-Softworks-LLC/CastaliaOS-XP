// Castalia shell — the launch menu (Phase 0 proof of concept).
// Two-column layout per Bible §7.3: pinned apps left, places right, user
// header on the titlebar gradient, power row at the foot. Every entry wears
// its icon from the shared 48 px family, and the menu rises 6 px + fades in
// on open (≤150 ms, skipped under reduce-motion — §8.6).
//
// A search box sits at the top of the app column (§7.3): it filters the
// roster as you type, hides the categories that empty out, and Enter launches
// the first match — "press launch-key, type, Enter".
#pragma once

#include <QVector>
#include <QWidget>

#include "ThemeTokens.h"

class QLabel;
class QVBoxLayout;
class QLineEdit;
class QPushButton;

class CastaliaMenu : public QWidget {
    Q_OBJECT
public:
    explicit CastaliaMenu(const ThemeTokens &tokens, QWidget *parent = nullptr);

    // Type into the search box programmatically (the panel's --menu-query,
    // used by the render gate and the press kit to show search results).
    void setQuery(const QString &text);

    // True when the typed text is one of the words that reveal the hidden
    // entry. Static and pure so the panel's self-test can pin it.
    static bool isSecretWord(const QString &folded);

protected:
    void showEvent(QShowEvent *event) override;

private:
    // One searchable menu entry: the button, and the text we match against
    // (label folded to lowercase without accents, so "diagnostico" finds
    // "Diagnóstico del sistema").
    struct Entry {
        QPushButton *button = nullptr;
        QString needle;
    };
    // A category heading and the entries under it; the heading hides when the
    // filter empties its group.
    struct Group {
        QLabel *header = nullptr;
        QVector<Entry> entries;
    };

    // Rebuild the "recent documents" section from the freedesktop store
    // (§7.9). Called on every open: the list changes while the menu is shut.
    void rebuildRecent();
    void applyFilter(const QString &text);
    QPushButton *firstMatch() const;

    QLineEdit *m_search = nullptr;
    // The recent section is a fixed set of slots, created once and refilled on
    // every open: rebuilding widgets inside a live layout is how the first
    // version ended up drawing every entry on top of the last.
    QVector<QPushButton *> m_recentSlots;
    QPushButton *m_recentClear = nullptr;
    QLabel *m_recentHeader = nullptr;
    int m_recentGroup = -1;               // its index in m_groups
    QString m_repo;                       // asset tree, for the entry icons
    QLabel *m_empty = nullptr;
    QPushButton *m_secret = nullptr;   // the entry no menu ever lists
    QVector<Group> m_groups;
};
