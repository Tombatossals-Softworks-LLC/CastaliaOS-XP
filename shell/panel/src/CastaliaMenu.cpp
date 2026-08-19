#include "CastaliaMenu.h"

#include "AppRoster.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QProcess>
#include <QScrollArea>
#include <QShowEvent>
#include <QLayoutItem>
#include <QVBoxLayout>
#include <QVector>

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

#include "Mark.h"
#include "Recent.h"
#include "Theme.h"

namespace {

// The user avatar circle, painted natively (no image assets needed).
class Avatar : public QWidget {
public:
    explicit Avatar(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(28, 28);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QRadialGradient g(QPointF(10, 9), 22);
        g.setColorAt(0.0, QColor(0xE6, 0xD7, 0xB4));
        g.setColorAt(1.0, QColor(0xB8, 0x96, 0x5E));
        p.setBrush(g);
        p.setPen(QPen(QColor(255, 255, 255, 190), 2));
        p.drawEllipse(rect().adjusted(1, 1, -1, -1));
    }
};

// Fold a label to what we match search input against: lowercase, accents
// stripped. Spanish-first labels are full of them, and nobody types
// "Diagnóstico" with the accent when they are in a hurry (§7.3 keyboard-first).
QString searchFold(const QString &text)
{
    const QString decomposed =
        text.toLower().normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar ch : decomposed) {
        if (ch.category() == QChar::Mark_NonSpacing
            || ch.category() == QChar::Mark_SpacingCombining)
            continue;
        out.append(ch);
    }
    return out;
}

QPushButton *menuItem(const QString &text, const char *objectName,
                      QWidget *parent, const QIcon &icon = QIcon(),
                      int iconPx = 18)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(QLatin1String(objectName));
    b->setCursor(Qt::PointingHandCursor);
    b->setFlat(true);
    if (!icon.isNull()) {
        b->setIcon(icon);
        b->setIconSize(QSize(iconPx, iconPx));
    }
    return b;
}

} // namespace

CastaliaMenu::CastaliaMenu(const ThemeTokens &tokens, QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("CastaliaMenu"));
    // Fixed size; the app list scrolls if the roster outgrows it. The height
    // covers the search box, the scrolling roster, the four pinned system
    // items and the power row, and still fits a 600 px screen with the panel
    // (§7.11: the menu must fit 800x600).
    setFixedSize(452, 508);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);

    // header on the titlebar gradient
    auto *head = new QWidget(this);
    head->setObjectName(QStringLiteral("MenuHeader"));
    head->setFixedHeight(46);
    auto *headLay = new QHBoxLayout(head);
    headLay->setContentsMargins(12, 0, 12, 0);
    headLay->setSpacing(10);
    headLay->addWidget(new Avatar(head));
    auto *user = new QLabel(QStringLiteral("Dave"), head);
    user->setObjectName(QStringLiteral("MenuUser"));
    headLay->addWidget(user);
    headLay->addStretch(1);
    root->addWidget(head);

    // two columns: pinned apps | places
    auto *cols = new QWidget(this);
    auto *grid = new QGridLayout(cols);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);

    // Launch a first-party app by binary name, passing the active theme and
    // asset tree (from CASTALIA_REPO, set by castalia-session in the live
    // session). Each app runs as its own process (§7.1). Extra args let the
    // "places" open the file manager straight at a folder.
    const QString repo = qEnvironmentVariable("CASTALIA_REPO",
                                              QStringLiteral("."));
    m_repo = repo;
    const QString panelTheme = tokens.themeId().isEmpty()
                                   ? QStringLiteral("classic") : tokens.themeId();
    // Resolve the theme at click time (not panel-launch time) so an app opened
    // after the user changes the theme in the Control Center wears the new one.
    auto launch = [repo, panelTheme](const QString &bin,
                                     const QStringList &extra) {
        const QString theme = castalia::activeThemeId(panelTheme);
        QStringList args{QStringLiteral("--repo"), repo,
                         QStringLiteral("--theme"), theme};
        args += extra;
        QProcess::startDetached(bin, args);
    };

    // The full first-party application roster (§9). The app list scrolls, so
    // the menu stays a fixed size however many apps ship; the system items
    // below (Control Center, Help, Install) stay pinned.
    auto *left = new QWidget(cols);
    left->setObjectName(QStringLiteral("MenuLeft"));
    auto *leftOuter = new QVBoxLayout(left);
    leftOuter->setContentsMargins(0, 0, 0, 0);
    leftOuter->setSpacing(0);

    // Search box (§7.3): filters the roster as you type; Enter launches the
    // first match. It queries app and settings *names* only — never the disk
    // — so it stays instant on the 512 MB floor (§7.8).
    auto *searchBox = new QWidget(left);
    auto *searchLay = new QHBoxLayout(searchBox);
    searchLay->setContentsMargins(6, 6, 6, 2);
    searchLay->setSpacing(6);
    auto *searchIcon = new QLabel(searchBox);
    searchIcon->setPixmap(castalia::themeIcon(repo, QStringLiteral("search"))
                              .pixmap(16, 16));
    searchLay->addWidget(searchIcon);
    m_search = new QLineEdit(searchBox);
    m_search->setObjectName(QStringLiteral("MenuSearch"));
    m_search->setPlaceholderText(tr("Buscar programas…"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(QStringLiteral("Buscar programas"));
    searchLay->addWidget(m_search, 1);
    leftOuter->addWidget(searchBox);

    auto *scroll = new QScrollArea(left);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea,QScrollArea>QWidget>QWidget{background:transparent;}"));
    auto *appHost = new QWidget;
    auto *leftLay = new QVBoxLayout(appHost);
    leftLay->setContentsMargins(4, 8, 4, 4);
    leftLay->setSpacing(2);

    // The roster lives in AppRoster (one table, shared with the Alt+Tab
    // switcher) so a binary cannot wear one icon here and another there.
    // Every entry wears its icon from the shared 48 px family (§8.4).
    const QVector<castalia::apps::Group> &groups = castalia::apps::roster();

    // Recent documents (§7.9) lead the roster: the thing you were doing last
    // is the likeliest thing you want next. The section is rebuilt on every
    // open — the list changes while the menu is closed — so it gets its own
    // header and layout here and its entries later.
    m_recentHeader = new QLabel(tr("Documentos recientes"), left);
    m_recentHeader->setObjectName(QStringLiteral("MenuSection"));
    m_recentHeader->setContentsMargins(6, 0, 6, 2);
    leftLay->addWidget(m_recentHeader);
    for (int i = 0; i < 8; ++i) {
        auto *slot = menuItem(QString(), "MenuApp", left,
                              castalia::themeIcon(repo,
                                                  QStringLiteral("documents")));
        slot->hide();
        connect(slot, &QPushButton::clicked, this, [this, slot]() {
            const QString path = slot->property("path").toString();
            if (path.isEmpty())
                return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            hide();
        });
        leftLay->addWidget(slot);
        m_recentSlots.append(slot);
    }
    m_recentClear = menuItem(tr("Vaciar la lista"),
                             "MenuApp", left,
                             castalia::themeIcon(repo,
                                                 QStringLiteral("trash")));
    m_recentClear->hide();
    connect(m_recentClear, &QPushButton::clicked, this, [this]() {
        castalia::recent::clear();
        rebuildRecent();
        applyFilter(m_search->text());
    });
    leftLay->addWidget(m_recentClear);
    m_groups.append(CastaliaMenu::Group{m_recentHeader, {}});
    m_recentGroup = m_groups.size() - 1;

    bool firstGroup = true;
    for (const castalia::apps::Group &g : groups) {
        auto *hdr = new QLabel(castalia::apps::title(g), left);
        hdr->setObjectName(QStringLiteral("MenuSection"));
        hdr->setContentsMargins(6, firstGroup ? 0 : 8, 6, 2);
        leftLay->addWidget(hdr);
        firstGroup = false;
        CastaliaMenu::Group searchGroup;
        searchGroup.header = hdr;
        for (const castalia::apps::Entry &a : g.entries) {
            const QString label = castalia::apps::label(a);
            auto *item = menuItem(
                label, "MenuApp", left,
                castalia::themeIcon(repo, QLatin1String(a.icon)));
            const QString bin = QLatin1String(a.bin);
            const QStringList extra = a.args
                ? QString::fromLatin1(a.args).split(QLatin1Char(' '))
                : QStringList();
            connect(item, &QPushButton::clicked, this,
                    [launch, bin, extra, this]() {
                        launch(bin, extra);
                        hide();
                    });
            leftLay->addWidget(item);
            // Match the category too, so "juegos" surfaces the games — and
            // index the Spanish source strings alongside the translated ones,
            // so an English interface still answers to "buscaminas" and a
            // Spanish keyword list keeps working in either language (§7.13).
            searchGroup.entries.append(
                {item, searchFold(QStringList{
                                      label, castalia::apps::title(g),
                                      QString::fromUtf8(a.label),
                                      QString::fromUtf8(g.title),
                                      a.keywords ? QString::fromUtf8(a.keywords)
                                                 : QString()}
                                      .join(QLatin1Char(' ')))});
        }
        m_groups.append(searchGroup);
    }
    // The entry that is in no category and on no list: type one of the words
    // in isSecretWord() and it appears. It runs the aurora screensaver — the
    // same curtains the desktop's own key sequence wakes.
    m_secret = menuItem(tr("✦ Aurora de Castalia"), "MenuApp",
                        left,
                        castalia::themeIcon(repo, QStringLiteral("shield")));
    m_secret->hide();
    connect(m_secret, &QPushButton::clicked, this, [launch, this]() {
        launch(QStringLiteral("castalia-salvapantallas"),
               {QStringLiteral("--mode"), QStringLiteral("aurora")});
        hide();
    });
    leftLay->addWidget(m_secret);

    // Shown only when a search matches nothing — silence would read as a bug.
    m_empty = new QLabel(tr("Sin resultados."), left);
    m_empty->setObjectName(QStringLiteral("MenuSection"));
    m_empty->setContentsMargins(6, 8, 6, 2);
    m_empty->hide();
    leftLay->addWidget(m_empty);
    leftLay->addStretch(1);
    scroll->setWidget(appHost);
    leftOuter->addWidget(scroll, 1);

    // Pinned system items below the scrolling app list.
    auto *sysBox = new QWidget(left);
    sysBox->setObjectName(QStringLiteral("MenuLeft"));
    auto *sysLay = new QVBoxLayout(sysBox);
    sysLay->setContentsMargins(4, 2, 4, 6);
    sysLay->setSpacing(2);
    auto *control = menuItem(tr("Centro de control"),
                             "MenuAllApps", sysBox,
                             castalia::themeIcon(repo,
                                                 QStringLiteral("settings")));
    connect(control, &QPushButton::clicked, this, [launch, this]() {
        launch(QStringLiteral("castalia-control-center"), {});
        hide();
    });
    sysLay->addWidget(control);
    auto *help = menuItem(tr("Centro de ayuda"),
                          "MenuAllApps", sysBox,
                          castalia::themeIcon(repo, QStringLiteral("help")));
    connect(help, &QPushButton::clicked, this, [launch, this]() {
        launch(QStringLiteral("castalia-bienvenida"), {});
        hide();
    });
    sysLay->addWidget(help);
    auto *about = menuItem(tr("Acerca de Castalia"),
                           "MenuAllApps", sysBox,
                           castalia::themeIcon(repo,
                                               QStringLiteral("computer")));
    connect(about, &QPushButton::clicked, this, [launch, this]() {
        launch(QStringLiteral("castalia-acerca"), {});
        hide();
    });
    sysLay->addWidget(about);
    // The graphical installer (§14) — safe to surface anywhere: it refuses
    // every destructive step until the target disk is typed to confirm.
    auto *install = menuItem(tr("Instalar Castalia OS…"),
                             "MenuAllApps", sysBox,
                             castalia::themeIcon(repo,
                                                 QStringLiteral("disk")));
    connect(install, &QPushButton::clicked, this, [launch, this]() {
        launch(QStringLiteral("castalia-instalador"), {});
        hide();
    });
    sysLay->addWidget(install);
    leftOuter->addWidget(sysBox);

    // The pinned system items are searchable too — §7.3's search covers apps
    // *and* settings. They have no category heading of their own.
    CastaliaMenu::Group pinned;
    for (QPushButton *b : {control, help, about, install})
        pinned.entries.append({b, searchFold(b->text())});
    m_groups.append(pinned);

    connect(m_search, &QLineEdit::textChanged, this,
            &CastaliaMenu::applyFilter);
    // Keyboard-first: type, Enter. Enter with nothing typed does nothing
    // rather than launching whatever happens to be first.
    connect(m_search, &QLineEdit::returnPressed, this, [this]() {
        if (m_search->text().trimmed().isEmpty())
            return;
        if (QPushButton *hit = firstMatch())
            hit->click();
    });

    // Places: open the file manager at a real path (§9.1).
    const QString home = qEnvironmentVariable(
        "HOME", QStringLiteral("/root"));
    auto *right = new QWidget(cols);
    right->setObjectName(QStringLiteral("MenuRight"));
    auto *rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(4, 8, 4, 8);
    rightLay->setSpacing(2);
    const struct { const char *label; QString path; const char *icon; }
        places[] = {
        // The *label* is translated; the *path* is not. A folder called
        // ~/Documentos does not rename itself because the menu is in
        // English (§7.13).
        {QT_TR_NOOP("Documentos"), home + QStringLiteral("/Documentos"),
         "documents"},
        {QT_TR_NOOP("Carpeta personal"), home, "home"},
        {QT_TR_NOOP("Equipo"), QStringLiteral("/"), "computer"},
        {QT_TR_NOOP("Lugares de red"), QStringLiteral("/mnt"), "network"},
    };
    for (const auto &pl : places) {
        auto *item = menuItem(tr(pl.label), "MenuPlace", right,
                              castalia::themeIcon(repo,
                                                  QLatin1String(pl.icon)));
        const QString path = pl.path;
        connect(item, &QPushButton::clicked, this, [launch, path, this]() {
            launch(QStringLiteral("castalia-explorer"),
                   {QStringLiteral("--path"), path});
            hide();
        });
        rightLay->addWidget(item);
    }
    rightLay->addStretch(1);

    grid->addWidget(left, 0, 0);
    grid->addWidget(right, 0, 1);
    grid->setColumnStretch(0, 4);
    grid->setColumnStretch(1, 3);
    root->addWidget(cols, 1);

    // Power row (§7.6). Every button goes through castalia-salir, the single
    // session/power dialog: "Bloquear" is not destructive so it runs straight
    // away (--action), while logging out and powering off open the dialog with
    // that tile preselected, so the confirmation always happens there.
    auto *foot = new QWidget(this);
    foot->setObjectName(QStringLiteral("MenuFoot"));
    auto *footLay = new QHBoxLayout(foot);
    footLay->setContentsMargins(10, 6, 10, 6);
    footLay->addStretch(1);
    auto *lock = menuItem(tr("Bloquear"), "PowerBtn", foot);
    connect(lock, &QPushButton::clicked, this, [launch, this]() {
        launch(QStringLiteral("castalia-salir"),
               {QStringLiteral("--action"), QStringLiteral("bloquear")});
        hide();
    });
    footLay->addWidget(lock);
    auto *logout = menuItem(tr("Cerrar sesión"), "PowerBtn", foot);
    connect(logout, &QPushButton::clicked, this, [launch, this]() {
        launch(QStringLiteral("castalia-salir"),
               {QStringLiteral("--focus"), QStringLiteral("cerrar-sesion")});
        hide();
    });
    footLay->addWidget(logout);
    auto *off = menuItem(tr("Apagar"), "PowerBtnSolid", foot);
    connect(off, &QPushButton::clicked, this, [launch, this]() {
        launch(QStringLiteral("castalia-salir"),
               {QStringLiteral("--focus"), QStringLiteral("apagar")});
        hide();
    });
    footLay->addWidget(off);
    root->addWidget(foot);
}

// Refill the recent section from the freedesktop store every desktop app on
// the machine shares. Slots are reused, never recreated: only the ones with a
// document are searchable, so a slot with nothing in it cannot be revealed by
// clearing the search box. The last row clears the list, exactly as XP's
// "Documentos recientes" did — a recent list you cannot clear is a log nobody
// asked for (§7.9, P7).
void CastaliaMenu::rebuildRecent()
{
    if (m_recentSlots.isEmpty() || m_recentGroup < 0)
        return;
    const QVector<castalia::recent::Entry> recent =
        castalia::recent::list(m_recentSlots.size());
    m_groups[m_recentGroup].entries.clear();

    for (int i = 0; i < m_recentSlots.size(); ++i) {
        QPushButton *slot = m_recentSlots.at(i);
        if (i >= recent.size()) {
            slot->setProperty("path", QString());
            slot->hide();
            continue;
        }
        const castalia::recent::Entry &e = recent.at(i);
        const QString name = QFileInfo(e.path).fileName();
        slot->setText(name);
        slot->setToolTip(e.path);
        slot->setProperty("path", e.path);
        slot->show();
        // Searchable by file name *and* by the folder it lives in, so
        // "informes" finds everything under ~/Documentos/informes.
        m_groups[m_recentGroup].entries.append(
            {slot, searchFold(name + QLatin1Char(' ') + e.path)});
    }

    const bool any = !recent.isEmpty();
    m_recentHeader->setVisible(any);
    m_recentClear->setVisible(any);
    if (any)
        m_groups[m_recentGroup].entries.append(
            {m_recentClear, searchFold(m_recentClear->text())});
}

void CastaliaMenu::setQuery(const QString &text)
{
    m_search->setText(text);      // textChanged runs the filter
    // …and then settle the layout by hand. Hiding two thirds of the roster
    // invalidates the app column's geometry, and a live session repaints on
    // the next event-loop pass — but --menu-shot grabs synchronously, so
    // without this the screenshot shows the *old* positions: a match far
    // down the list (say "servicios") is still parked below the viewport and
    // the shot comes out blank. It looked like a broken search; it was a
    // layout that had not run yet.
    for (QWidget *w : findChildren<QWidget *>())
        if (QLayout *l = w->layout())
            l->activate();
    if (QLayout *l = layout())
        l->activate();
}

// The words that reveal the hidden entry: the studio, and the thing itself.
bool CastaliaMenu::isSecretWord(const QString &folded)
{
    return folded == QLatin1String("tombatossals")
        || folded == QLatin1String("aurora")
        || folded == QLatin1String("konami");
}

// Hide every entry that does not match, and every category heading whose
// entries all went away. An empty query restores the full roster.
void CastaliaMenu::applyFilter(const QString &text)
{
    const QString needle = searchFold(text.trimmed());
    // Every keystroke re-decides the visibility of ~50 widgets. Freezing
    // updates first collapses that into a single relayout and repaint
    // instead of one per widget (§7.8: the menu stays instant on the floor).
    setUpdatesEnabled(false);
    int total = 0;
    for (Group &g : m_groups) {
        int shown = 0;
        for (const Entry &e : g.entries) {
            const bool visible = needle.isEmpty()
                                 || e.needle.contains(needle);
            e.button->setVisible(visible);
            shown += visible ? 1 : 0;
        }
        if (g.header)
            g.header->setVisible(shown > 0);
        total += shown;
    }
    const bool secret = isSecretWord(needle);
    m_secret->setVisible(secret);
    total += secret ? 1 : 0;
    m_empty->setVisible(total == 0 && !needle.isEmpty());
    setUpdatesEnabled(true);
}

QPushButton *CastaliaMenu::firstMatch() const
{
    // The hidden entry wins when it is showing: nothing else can be, since
    // no app label contains any of the secret words.
    if (m_secret && m_secret->isVisible())
        return m_secret;
    for (const Group &g : m_groups)
        for (const Entry &e : g.entries)
            if (e.button->isVisible())
                return e.button;
    return nullptr;
}

void CastaliaMenu::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Every open starts from a clean, complete roster with the caret ready:
    // "press launch-key, type, Enter" (§7.3). The recent list is re-read here
    // rather than at construction — the panel outlives a whole session.
    rebuildRecent();
    m_search->clear();
    m_search->setFocus();
    if (castalia::reduceMotion())
        return;
    // Rise 6 px + fade in, 150 ms. Both animations delete themselves; no
    // timer survives the entrance (§16 FLOOR budget).
    const QPoint target = pos();
    move(target + QPoint(0, 6));
    setWindowOpacity(0.0);
    auto *rise = new QPropertyAnimation(this, "pos", this);
    rise->setDuration(150);
    rise->setStartValue(target + QPoint(0, 6));
    rise->setEndValue(target);
    rise->setEasingCurve(QEasingCurve::OutCubic);
    rise->start(QAbstractAnimation::DeleteWhenStopped);
    auto *fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(150);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}
