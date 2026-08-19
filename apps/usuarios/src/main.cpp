// castalia-usuarios — the Castalia User Accounts panel (Bible §9,
// §10 XP-parity).
//
// The XP-era "Cuentas de usuario" applet: it lists the machine's real human
// accounts (read from the world-readable /etc/passwd — never /etc/shadow),
// highlights the one running this session, and shows each account's full
// name, home, login shell and groups. It reads everything with Qt/POSIX
// alone, so it renders anywhere including the offscreen CI gate. Account
// mutation needs privileges we never take: the one write it offers —
// "Cambiar mi contraseña" — hands off to `passwd` inside the Castalia
// terminal, and only for your own account; if the terminal isn't installed
// it says so instead of pretending.
//
// Usage: castalia-usuarios --theme human [--repo PATH] [--screenshot out.png]

#include <unistd.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include "Theme.h"

namespace {

struct Account {
    QString user, fullName, home, shell;
    int uid = -1, gid = -1;
};

// gid → group name, from the world-readable /etc/group.
QHash<int, QString> groupNames()
{
    QHash<int, QString> map;
    QFile f(QStringLiteral("/etc/group"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return map;
    const auto lines = QString::fromUtf8(f.readAll())
                           .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &ln : lines) {
        const auto c = ln.split(QLatin1Char(':'));
        if (c.size() >= 3)
            map.insert(c[2].toInt(), c[0]);
    }
    return map;
}

// Parse /etc/passwd into accounts (all of them; the caller filters).
QList<Account> readPasswd()
{
    QList<Account> out;
    QFile f(QStringLiteral("/etc/passwd"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    const auto lines = QString::fromUtf8(f.readAll())
                           .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &ln : lines) {
        const auto c = ln.split(QLatin1Char(':'));
        if (c.size() < 7)
            continue;
        Account a;
        a.user = c[0];
        a.uid = c[2].toInt();
        a.gid = c[3].toInt();
        a.fullName = c[4].section(QLatin1Char(','), 0, 0).trimmed();
        a.home = c[5];
        a.shell = c[6];
        out.append(a);
    }
    return out;
}

// A real login account: human UID range and an interactive shell.
bool isHuman(const Account &a)
{
    if (a.uid < 1000 || a.uid >= 60000)
        return false;
    return !(a.shell.endsWith(QStringLiteral("nologin"))
             || a.shell.endsWith(QStringLiteral("false"))
             || a.shell.isEmpty());
}

} // namespace

class Usuarios : public QWidget {
    Q_OBJECT
public:
    Usuarios(const QString &repo, const QString &themeId,
             const ThemeTokens &tokens)
        : m_repo(repo), m_themeId(themeId), m_tokens(tokens)
    {
        setWindowTitle(QStringLiteral("Cuentas de usuario — Castalia"));
        resize(560, 380);

        const int curUid = static_cast<int>(::getuid());
        const QList<Account> all = readPasswd();
        m_groups = groupNames();
        for (const Account &a : all) {
            if (isHuman(a))
                m_accounts.append(a);
            if (a.uid == curUid)
                m_current = a;
        }
        // Always show the session's own account, even if it falls outside the
        // human-UID range (e.g. a root-run live session) — the panel is never
        // emptier than the person reading it.
        bool haveCurrent = false;
        for (const Account &a : m_accounts)
            if (a.uid == curUid)
                haveCurrent = true;
        if (!haveCurrent && m_current.uid >= 0)
            m_accounts.prepend(m_current);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("UsHeader"));
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "#UsHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *title = new QLabel(head);
        title->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>Cuentas de "
            "usuario</span>&nbsp;&nbsp;<span style='color:%1'>%2 en este "
            "equipo</span>")
            .arg(colorTok("titlebar_text"),
                 QString::number(m_accounts.size())));
        hl->addWidget(title);
        hl->addStretch(1);
        root->addWidget(head);

        auto *body = new QHBoxLayout;
        body->setContentsMargins(14, 12, 14, 14);
        body->setSpacing(14);

        m_list = new QListWidget(this);
        m_list->setObjectName(QStringLiteral("UsList"));
        for (const Account &a : m_accounts) {
            const bool isCur = a.uid == m_current.uid;
            const QString name = a.fullName.isEmpty() ? a.user : a.fullName;
            auto *it = new QListWidgetItem(
                isCur ? QStringLiteral("%1  ·  tú").arg(name) : name, m_list);
            it->setIcon(castalia::themeIcon(m_repo, QStringLiteral("users")));
        }
        m_list->setIconSize(QSize(28, 28));
        connect(m_list, &QListWidget::currentRowChanged, this,
                &Usuarios::showAccount);
        body->addWidget(m_list, 2);

        auto *side = new QVBoxLayout;
        side->setSpacing(8);
        auto *form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setHorizontalSpacing(12);
        m_fFull = new QLabel(this);
        m_fUser = new QLabel(this);
        m_fUid = new QLabel(this);
        m_fHome = new QLabel(this);
        m_fShell = new QLabel(this);
        m_fGroup = new QLabel(this);
        for (QLabel *l : {m_fHome, m_fGroup})
            l->setWordWrap(true);
        form->addRow(QStringLiteral("Nombre:"), m_fFull);
        form->addRow(QStringLiteral("Usuario:"), m_fUser);
        form->addRow(QStringLiteral("UID:"), m_fUid);
        form->addRow(QStringLiteral("Carpeta:"), m_fHome);
        form->addRow(QStringLiteral("Intérprete:"), m_fShell);
        form->addRow(QStringLiteral("Grupos:"), m_fGroup);
        side->addLayout(form);
        side->addStretch(1);

        m_passwd = new QPushButton(
            QStringLiteral("Cambiar mi contraseña…"), this);
        m_passwd->setObjectName(QStringLiteral("UsPasswd"));
        m_passwd->setCursor(Qt::PointingHandCursor);
        connect(m_passwd, &QPushButton::clicked, this,
                &Usuarios::changePassword);
        side->addWidget(m_passwd);
        m_note = new QLabel(this);
        m_note->setProperty("secondary", true);
        m_note->setWordWrap(true);
        side->addWidget(m_note);
        body->addLayout(side, 3);
        root->addLayout(body, 1);

        // Select the current user by default (or the first account).
        int sel = 0;
        for (int i = 0; i < m_accounts.size(); ++i)
            if (m_accounts[i].uid == m_current.uid)
                sel = i;
        m_list->setCurrentRow(sel);
    }

private slots:
    void showAccount(int row)
    {
        if (row < 0 || row >= m_accounts.size())
            return;
        const Account &a = m_accounts[row];
        const bool isCur = a.uid == m_current.uid;
        m_fFull->setText(a.fullName.isEmpty() ? QStringLiteral("—")
                                              : a.fullName);
        m_fUser->setText(a.user);
        m_fUid->setText(QString::number(a.uid));
        m_fHome->setText(a.home);
        m_fShell->setText(a.shell);
        m_fGroup->setText(groupsFor(a));

        // You can only change your own password, and only when the Castalia
        // terminal is here to host `passwd` — no dead button otherwise.
        const bool termOk = !QStandardPaths::findExecutable(
            QStringLiteral("castalia-terminal")).isEmpty();
        m_passwd->setEnabled(isCur && termOk);
        if (!isCur)
            m_note->setText(QStringLiteral(
                "Solo puedes cambiar la contraseña de tu propia cuenta."));
        else if (!termOk)
            m_note->setText(QStringLiteral(
                "Para cambiar la contraseña hace falta la terminal de "
                "Castalia."));
        else
            m_note->setText(QStringLiteral(
                "Abre «passwd» en la terminal para cambiarla."));
    }

    void changePassword()
    {
        const QString term =
            QStandardPaths::findExecutable(QStringLiteral("castalia-terminal"));
        if (term.isEmpty())
            return;
        QProcess::startDetached(term, {
            QStringLiteral("--theme"), m_themeId,
            QStringLiteral("--repo"), m_repo,
            QStringLiteral("--run"), QStringLiteral("passwd")});
    }

private:
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }
    // Primary group for anyone; the full group set for the current session
    // (only `id` can enumerate supplementary groups reliably).
    QString groupsFor(const Account &a) const
    {
        const QString primary = m_groups.value(a.gid,
                                               QString::number(a.gid));
        if (a.uid != m_current.uid)
            return primary;
        QProcess p;
        p.start(QStringLiteral("id"), {QStringLiteral("-Gn")});
        if (p.waitForFinished(1000)) {
            const QString all = QString::fromUtf8(
                p.readAllStandardOutput()).trimmed();
            if (!all.isEmpty())
                return all.split(QLatin1Char(' '), Qt::SkipEmptyParts)
                    .join(QStringLiteral(", "));
        }
        return primary;
    }

    QString m_repo, m_themeId;
    ThemeTokens m_tokens;
    QList<Account> m_accounts;
    Account m_current;
    QHash<int, QString> m_groups;
    QListWidget *m_list = nullptr;
    QLabel *m_fFull = nullptr, *m_fUser = nullptr, *m_fUid = nullptr,
           *m_fHome = nullptr, *m_fShell = nullptr, *m_fGroup = nullptr,
           *m_note = nullptr;
    QPushButton *m_passwd = nullptr;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-usuarios"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const QString accent =
        ThemeTokens::load(castalia::themeConfPath(repo, themeId))
            .str(QStringLiteral("colors"), QStringLiteral("accent"));
    const ThemeTokens tokens = castalia::applyTheme(
        &app, repo, themeId,
        QStringLiteral("#UsPasswd{font-weight:bold;border-color:%1;}"
                       "#UsList::item{padding:6px;}")
            .arg(accent));

    Usuarios w(repo, themeId, tokens);
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
