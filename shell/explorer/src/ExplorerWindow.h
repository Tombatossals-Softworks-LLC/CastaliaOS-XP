// Castalia Explorer — Phase 0 proof of concept (Bible §9.1, §7.1).
// A real file-manager window: places sidebar, address bar with working
// back/forward/up history, QFileSystemModel browsing with the Castalia icon
// family, icon/list view modes, and a live item-count status bar.
#pragma once

#include <QFileIconProvider>
#include <QMainWindow>

#include "ThemeTokens.h"

class QFileSystemModel;
class QLineEdit;
class QListView;
class QListWidget;
class QLabel;

// Serves the Castalia icon family instead of the platform's default icons.
class CastaliaIconProvider : public QFileIconProvider {
public:
    explicit CastaliaIconProvider(const QString &iconDir);
    QIcon icon(IconType type) const override;
    QIcon icon(const QFileInfo &info) const override;

private:
    QIcon m_folder;
    QIcon m_file;
    QIcon m_disk;
};

class ExplorerWindow : public QMainWindow {
    Q_OBJECT
public:
    ExplorerWindow(const ThemeTokens &tokens, const QString &iconDir,
                   const QString &startPath, QWidget *parent = nullptr);

public slots:
    void navigateTo(const QString &path, bool remember = true);

private slots:
    void goBack();
    void goForward();
    void goUp();
    void onActivated(const QModelIndex &index);
    void showContextMenu(const QPoint &pos);
    void updateStatus();

private:
    QIcon themedIcon(const QString &name) const;
    // Open a file in the matching Castalia app (image/text/archive) or the
    // system default; extract an archive into the current folder.
    void openPath(const QString &path);
    void extractHere(const QString &archive);
    QString repoRoot() const;
    QString themeId() const;

    ThemeTokens m_tokens;
    QString m_iconDir;
    QFileSystemModel *m_model = nullptr;
    QListView *m_view = nullptr;
    QListWidget *m_places = nullptr;
    QLineEdit *m_address = nullptr;
    QLabel *m_status = nullptr;
    QStringList m_backStack;
    QStringList m_forwardStack;
    QString m_current;
};
