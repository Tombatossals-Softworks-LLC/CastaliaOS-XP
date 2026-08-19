// castalia-instalador — the Castalia graphical installer (Bible §14).
//
// A themed Qt5 wizard that collects the install choices and drives the shared,
// unit-tested Python backend (installer/castalia_installer). It shows the
// *exact* plan (backend --dry-run) before committing, and never runs a
// destructive install until the user types the target disk to confirm
// (§14.5 #1). Pure Qt5 + libcastalia-ui theming.
//
// Usage:
//   castalia-instalador --theme classic [--repo PATH] [--demo]
//                       [--page N] [--screenshot out.png]

#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QDir>
#include <QFormLayout>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "Mark.h"
#include "Theme.h"

namespace {

struct Choices {
    QString disk;
    long long diskMib = 0;
    QString diskLabel;
    QString hostname = QStringLiteral("castalia");
    QString username = QStringLiteral("usuario");
    QString fullName = QStringLiteral("Usuario de Castalia");
    bool autologin = false;
};

struct DiskEntry {
    QString path;
    long long sizeMib;
    QString label;
};

QString humanSize(long long mib)
{
    if (mib >= 1024)
        return QStringLiteral("%1 GiB").arg(mib / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MiB").arg(mib);
}

// Real block devices via lsblk; synthetic fallback for demo/offscreen.
QVector<DiskEntry> scanDisks(bool demo)
{
    QVector<DiskEntry> disks;
    if (!demo) {
        QProcess p;
        p.start(QStringLiteral("lsblk"),
                {QStringLiteral("-dnb"), QStringLiteral("-o"),
                 QStringLiteral("NAME,SIZE,TYPE,MODEL")});
        if (p.waitForFinished(2000)) {
            const QStringList lines =
                QString::fromUtf8(p.readAllStandardOutput())
                    .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QStringList f =
                    line.simplified().split(QLatin1Char(' '));
                if (f.size() < 3 || f[2] != QLatin1String("disk"))
                    continue;
                const long long bytes = f[1].toLongLong();
                QString model = f.size() > 3
                                    ? QStringList(f.mid(3)).join(QLatin1Char(' '))
                                    : QStringLiteral("disco");
                disks.push_back({QStringLiteral("/dev/") + f[0],
                                 bytes / (1024 * 1024), model});
            }
        }
    }
    if (disks.isEmpty())  // demo, offscreen CI, or nothing detected
        disks.push_back({QStringLiteral("/dev/sda"), 40 * 1024,
                         QStringLiteral("QEMU HARDDISK (demostración)")});
    return disks;
}

} // namespace

class Installer : public QWidget {
    Q_OBJECT
public:
    Installer(const QString &repo, const ThemeTokens &tokens, bool demo)
        : m_repo(repo), m_tokens(tokens), m_demo(demo)
    {
        setWindowTitle(QStringLiteral("Instalar Castalia OS"));
        resize(720, 500);
        auto *root = new QHBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        root->addWidget(buildSidebar());

        auto *rightCol = new QVBoxLayout;
        rightCol->setContentsMargins(0, 0, 0, 0);
        rightCol->setSpacing(0);
        m_stack = new QStackedWidget(this);
        m_stack->addWidget(buildWelcome());
        m_stack->addWidget(buildDiskPage());
        m_stack->addWidget(buildUserPage());
        m_stack->addWidget(buildSummary());
        m_stack->addWidget(buildProgress());
        rightCol->addWidget(m_stack, 1);
        rightCol->addWidget(buildNav());
        root->addLayout(rightCol, 1);

        goTo(0);
    }

    void showPage(int i) { goTo(i); }

private:
    // ---- sidebar: the step list over the titlebar gradient ---------------
    QWidget *buildSidebar()
    {
        auto *bar = new QWidget(this);
        bar->setObjectName(QStringLiteral("InstSidebar"));
        bar->setFixedWidth(196);
        bar->setStyleSheet(QStringLiteral(
            "#InstSidebar{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(tok("titlebar_top"), tok("titlebar_bottom")));
        auto *lay = new QVBoxLayout(bar);
        lay->setContentsMargins(18, 22, 12, 18);
        lay->setSpacing(4);

        auto *brand = new QLabel(bar);
        brand->setFixedHeight(48);
        brand->setText(QStringLiteral(
            "<span style='color:%1;font-size:17px;font-weight:bold'>"
            "Castalia OS</span>").arg(tok("titlebar_text")));
        lay->addWidget(brand);
        lay->addSpacing(14);

        const char *names[] = {"Bienvenida", "Disco", "Cuenta",
                               "Resumen", "Instalación"};
        for (const char *n : names) {
            auto *l = new QLabel(QString::fromUtf8(n), bar);
            l->setProperty("stepLabel", true);
            m_steps.push_back(l);
            lay->addWidget(l);
        }
        lay->addStretch(1);
        auto *foot = new QLabel(
            QStringLiteral("<span style='color:%1'>Tombatossals<br>"
                           "Softworks</span>").arg(tok("titlebar_text")),
            bar);
        lay->addWidget(foot);
        return bar;
    }

    QWidget *pageShell(const QString &title, const QString &subtitle,
                       QWidget *body)
    {
        auto *w = new QWidget(m_stack);
        auto *lay = new QVBoxLayout(w);
        lay->setContentsMargins(26, 24, 26, 12);
        lay->setSpacing(6);
        auto *h = new QLabel(title, w);
        h->setStyleSheet(QStringLiteral("font-size:19px;font-weight:bold;"));
        lay->addWidget(h);
        auto *s = new QLabel(subtitle, w);
        s->setWordWrap(true);
        s->setProperty("secondary", true);
        lay->addWidget(s);
        lay->addSpacing(8);
        lay->addWidget(body, 1);
        return w;
    }

    QWidget *buildWelcome()
    {
        auto *body = new QWidget;
        auto *lay = new QVBoxLayout(body);
        lay->setContentsMargins(0, 0, 0, 0);
        auto *mark = new QLabel(body);
        mark->setAlignment(Qt::AlignCenter);
        QPixmap pm(96, 96);
        pm.fill(Qt::transparent);
        QPainter pt(&pm);
        pt.setRenderHint(QPainter::Antialiasing, true);
        castalia::drawMark(&pt, QRectF(4, 4, 88, 88));
        pt.end();
        mark->setPixmap(pm);
        lay->addSpacing(20);
        lay->addWidget(mark);
        auto *msg = new QLabel(QStringLiteral(
            "Este asistente instalará Castalia OS en tu equipo.\n\n"
            "Puedes probar el sistema en vivo antes de instalar. La "
            "instalación se completa sin conexión a Internet y no requiere "
            "ninguna cuenta en línea."), body);
        msg->setWordWrap(true);
        msg->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        lay->addSpacing(16);
        lay->addWidget(msg);
        lay->addStretch(1);
        return pageShell(QStringLiteral("Bienvenido"),
                         QStringLiteral("Vamos a preparar tu instalación."),
                         body);
    }

    QWidget *buildDiskPage()
    {
        auto *body = new QWidget;
        auto *lay = new QVBoxLayout(body);
        lay->setContentsMargins(0, 0, 0, 0);
        m_diskList = new QListWidget(body);
        for (const DiskEntry &d : scanDisks(m_demo)) {
            auto *it = new QListWidgetItem(
                QStringLiteral("%1   ·   %2   ·   %3")
                    .arg(d.path, humanSize(d.sizeMib), d.label),
                m_diskList);
            it->setData(Qt::UserRole, d.path);
            it->setData(Qt::UserRole + 1, QVariant(d.sizeMib));
            it->setData(Qt::UserRole + 2, d.label);
        }
        m_diskList->setCurrentRow(0);
        lay->addWidget(m_diskList, 1);
        auto *warn = new QLabel(QStringLiteral(
            "⚠  Se borrará todo el contenido del disco seleccionado. "
            "Confirmarás esta acción en el resumen."), body);
        warn->setWordWrap(true);
        warn->setObjectName(QStringLiteral("InstWarn"));
        warn->setStyleSheet(QStringLiteral(
            "#InstWarn{color:#B3372E;padding:8px;border:1px solid #B3372E;"
            "border-radius:4px;}"));
        lay->addWidget(warn);
        return pageShell(QStringLiteral("Destino de la instalación"),
                         QStringLiteral("Elige el disco donde se instalará "
                                        "Castalia OS."), body);
    }

    QWidget *buildUserPage()
    {
        auto *body = new QWidget;
        auto *form = new QFormLayout(body);
        form->setContentsMargins(0, 0, 0, 0);
        form->setSpacing(10);
        m_fullName = new QLineEdit(QStringLiteral("Usuario de Castalia"), body);
        m_username = new QLineEdit(QStringLiteral("usuario"), body);
        m_hostname = new QLineEdit(QStringLiteral("castalia"), body);
        m_pass1 = new QLineEdit(body);
        m_pass2 = new QLineEdit(body);
        m_pass1->setEchoMode(QLineEdit::Password);
        m_pass2->setEchoMode(QLineEdit::Password);
        m_autologin = new QCheckBox(
            QStringLiteral("Iniciar sesión automáticamente"), body);
        form->addRow(QStringLiteral("Tu nombre"), m_fullName);
        form->addRow(QStringLiteral("Nombre de usuario"), m_username);
        form->addRow(QStringLiteral("Nombre del equipo"), m_hostname);
        form->addRow(QStringLiteral("Contraseña"), m_pass1);
        form->addRow(QStringLiteral("Repetir contraseña"), m_pass2);
        form->addRow(QString(), m_autologin);
        m_userHint = new QLabel(body);
        m_userHint->setProperty("secondary", true);
        form->addRow(QString(), m_userHint);
        for (QLineEdit *e : {m_username, m_pass1, m_pass2})
            connect(e, &QLineEdit::textChanged, this,
                    [this]() { updateNav(); });
        return pageShell(QStringLiteral("Tu cuenta"),
                         QStringLiteral("Crea el usuario principal del "
                                        "equipo."), body);
    }

    QWidget *buildSummary()
    {
        auto *body = new QWidget;
        auto *lay = new QVBoxLayout(body);
        lay->setContentsMargins(0, 0, 0, 0);
        m_plan = new QPlainTextEdit(body);
        m_plan->setReadOnly(true);
        m_plan->setObjectName(QStringLiteral("InstPlan"));
        m_plan->setStyleSheet(QStringLiteral(
            "#InstPlan{font-family:monospace;font-size:11px;}"));
        lay->addWidget(m_plan, 1);
        auto *row = new QHBoxLayout;
        m_confirmLbl = new QLabel(body);
        m_confirmLbl->setWordWrap(true);
        row->addWidget(m_confirmLbl, 1);
        lay->addLayout(row);
        m_confirm = new QLineEdit(body);
        m_confirm->setPlaceholderText(
            QStringLiteral("escribe el disco para confirmar (p. ej. /dev/sda)"));
        connect(m_confirm, &QLineEdit::textChanged, this,
                [this]() { updateNav(); });
        lay->addWidget(m_confirm);
        return pageShell(QStringLiteral("Resumen"),
                         QStringLiteral("Revisa lo que se hará. Nada se ha "
                                        "modificado todavía."), body);
    }

    QWidget *buildProgress()
    {
        auto *body = new QWidget;
        auto *lay = new QVBoxLayout(body);
        lay->setContentsMargins(0, 0, 0, 0);
        m_progress = new QProgressBar(body);
        m_progress->setRange(0, 0);  // busy until we start
        m_progress->setValue(0);
        lay->addWidget(m_progress);
        // The install slideshow: one warm tip at a time while the backend
        // works, crossfading every few seconds (stops with the install).
        m_tip = new QLabel(body);
        m_tip->setWordWrap(true);
        m_tip->setObjectName(QStringLiteral("InstTip"));
        m_tip->setStyleSheet(QStringLiteral(
            "#InstTip{padding:10px 12px;border:1px solid %1;"
            "border-radius:4px;font-size:13px;}")
            .arg(tok("border")));
        m_tip->setText(tipText(0));
        lay->addWidget(m_tip);
        m_log = new QPlainTextEdit(body);
        m_log->setReadOnly(true);
        m_log->setObjectName(QStringLiteral("InstLog"));
        m_log->setStyleSheet(QStringLiteral(
            "#InstLog{font-family:monospace;font-size:11px;}"));
        lay->addWidget(m_log, 1);
        return pageShell(QStringLiteral("Instalando"),
                         QStringLiteral("Copiando Castalia OS a tu disco…"),
                         body);
    }

    static QString tipText(int i)
    {
        static const char *tips[] = {
            "Castalia OS incluye 25 aplicaciones propias: desde el Escritor "
            "hasta el Buscaminas, todas con el mismo tema.",
            "Antes de cada actualización, el Centro de actualizaciones crea "
            "un Punto de restauración automáticamente.",
            "¿Un equipo antiguo? Castalia arranca con 512 MB de RAM y un "
            "Pentium 4 — el escritorio completo.",
            "Cambia de tema en el Centro de control: Human, Classic, Azul, "
            "Oliva, Plata, Medianoche y Alto contraste.",
            "Tus aplicaciones de Windows pueden seguir funcionando: el "
            "gestor de compatibilidad las instala en un clic.",
            "El instalador no toca la red: la instalación se completa "
            "entera sin conexión y sin cuentas en línea.",
        };
        return QString::fromUtf8(tips[i % (sizeof(tips) / sizeof(*tips))]);
    }

    QWidget *buildNav()
    {
        auto *bar = new QWidget(this);
        bar->setObjectName(QStringLiteral("InstNav"));
        auto *lay = new QHBoxLayout(bar);
        lay->setContentsMargins(16, 10, 16, 12);
        m_back = new QPushButton(QStringLiteral("Atrás"), bar);
        m_next = new QPushButton(QStringLiteral("Siguiente"), bar);
        m_next->setObjectName(QStringLiteral("InstPrimary"));
        m_next->setStyleSheet(QStringLiteral(
            "#InstPrimary{font-weight:bold;border-color:%1;}")
            .arg(tok("accent")));
        auto *quit = new QPushButton(QStringLiteral("Salir"), bar);
        connect(m_back, &QPushButton::clicked, this, [this]() {
            goTo(qMax(0, m_stack->currentIndex() - 1));
        });
        connect(m_next, &QPushButton::clicked, this, &Installer::onNext);
        connect(quit, &QPushButton::clicked, this, &QWidget::close);
        lay->addWidget(quit);
        lay->addStretch(1);
        lay->addWidget(m_back);
        lay->addWidget(m_next);
        return bar;
    }

    // ---- navigation & validation -----------------------------------------
    void goTo(int index)
    {
        const bool moved = m_stack->currentIndex() != index;
        m_stack->setCurrentIndex(index);
        // Crossfade the incoming page (180 ms). The effect is removed when
        // the animation ends so steady-state painting stays effect-free.
        if (moved && !castalia::reduceMotion()) {
            QWidget *page = m_stack->currentWidget();
            auto *fx = new QGraphicsOpacityEffect(page);
            page->setGraphicsEffect(fx);
            auto *anim = new QPropertyAnimation(fx, "opacity", page);
            anim->setDuration(180);
            anim->setStartValue(0.0);
            anim->setEndValue(1.0);
            anim->setEasingCurve(QEasingCurve::OutQuad);
            connect(anim, &QPropertyAnimation::finished, page,
                    [page]() { page->setGraphicsEffect(nullptr); });
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        }
        for (int i = 0; i < m_steps.size(); ++i) {
            const bool on = (i == index);
            m_steps[i]->setStyleSheet(QStringLiteral(
                "color:%1;%2")
                .arg(tok("titlebar_text"),
                     on ? QStringLiteral("font-weight:bold;") : QString()));
        }
        if (index == 3)
            refreshPlan();
        updateNav();
    }

    void updateNav()
    {
        const int i = m_stack->currentIndex();
        m_back->setVisible(i > 0 && i < 4);
        m_next->setVisible(i < 4);
        m_next->setText(i == 3 ? QStringLiteral("Instalar")
                               : QStringLiteral("Siguiente"));
        bool ok = true;
        if (i == 2)  // user page: valid username + matching passwords
            ok = formProblem().isEmpty();
        else if (i == 3)  // summary: typed confirmation must match the disk
            ok = m_confirm->text().trimmed() == currentDisk();
        m_next->setEnabled(ok);
        if (m_userHint)
            m_userHint->setText(formProblem());
    }

    QString currentDisk() const
    {
        auto *it = m_diskList ? m_diskList->currentItem() : nullptr;
        return it ? it->data(Qt::UserRole).toString() : QString();
    }

    QString userProblem() const
    {
        const QString u = m_username->text();
        for (const QChar &c : u)
            if (!(c.isLower() || c.isDigit()))
                return QStringLiteral("El usuario debe ir en minúsculas, sin "
                                      "espacios ni acentos.");
        if (u.isEmpty())
            return QStringLiteral("Escribe un nombre de usuario.");
        return QString();
    }

    // The account page's single live hint: username rules first, then the
    // password pairing — the user always sees the next thing to fix.
    QString formProblem() const
    {
        const QString user = userProblem();
        if (!user.isEmpty())
            return user;
        if (m_pass1->text() != m_pass2->text())
            return QStringLiteral("Las contraseñas no coinciden.");
        return QString();
    }

    void onNext()
    {
        const int i = m_stack->currentIndex();
        if (i == 3) {
            startInstall();
            return;
        }
        goTo(i + 1);
    }

    // ---- backend integration ---------------------------------------------
    QStringList backendArgs(bool dryRun) const
    {
        QStringList a{QStringLiteral("-m"),
                      QStringLiteral("castalia_installer"),
                      QStringLiteral("--disk"), currentDisk(),
                      QStringLiteral("--disk-size-mib"),
                      QString::number(currentDiskMib()),
                      QStringLiteral("--hostname"), m_hostname->text(),
                      QStringLiteral("--user"), m_username->text(),
                      QStringLiteral("--full-name"), m_fullName->text()};
        if (m_autologin->isChecked())
            a << QStringLiteral("--autologin");
        // The password is fed on stdin (see startInstall), never argv — so it
        // stays out of the process list. The flag makes the plan include the
        // set-password step even in the --dry-run preview.
        if (!m_pass1->text().isEmpty())
            a << QStringLiteral("--password-stdin");
        if (dryRun)
            a << QStringLiteral("--dry-run");
        else
            a << QStringLiteral("--confirm-erase") << currentDisk();
        return a;
    }

    long long currentDiskMib() const
    {
        auto *it = m_diskList ? m_diskList->currentItem() : nullptr;
        return it ? it->data(Qt::UserRole + 1).toLongLong() : 40 * 1024;
    }

    QProcessEnvironment backendEnv() const
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PYTHONPATH"),
                   m_repo + QStringLiteral("/installer"));
        return env;
    }

    void refreshPlan()
    {
        QProcess p;
        p.setProcessEnvironment(backendEnv());
        p.start(QStringLiteral("python3"), backendArgs(true));
        if (p.waitForFinished(4000)) {
            const QString plan =
                QString::fromUtf8(p.readAllStandardOutput());
            m_plan->setPlainText(plan);
            // The numbered plan is the progress bar's ground truth: the
            // backend logs one line per step, so N steps = N ticks.
            m_totalSteps = plan.count(QRegularExpression(
                QStringLiteral("^ *\\d+\\. "),
                QRegularExpression::MultilineOption));
        } else {
            m_plan->setPlainText(
                QStringLiteral("(no se pudo generar el plan)"));
            m_totalSteps = 0;
        }
        m_confirmLbl->setText(QStringLiteral(
            "Se borrará <b>%1</b>. Para confirmar, escribe exactamente "
            "<b>%1</b> abajo.").arg(currentDisk()));
    }

    void startInstall()
    {
        goTo(4);
        m_next->setVisible(false);
        m_back->setVisible(false);
        // Real progress: the dry-run plan told us how many steps there are;
        // the backend logs exactly one "castalia-install:" line per step.
        m_doneSteps = 0;
        if (m_totalSteps > 0) {
            m_progress->setRange(0, m_totalSteps);
            m_progress->setValue(0);
        }
        startSlideshow();
        m_log->appendPlainText(
            QStringLiteral("$ python3 %1\n")
                .arg(backendArgs(false).join(QLatin1Char(' '))));
        m_proc = new QProcess(this);
        m_proc->setProcessEnvironment(backendEnv());
        m_proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
            const QString chunk =
                QString::fromUtf8(m_proc->readAllStandardOutput());
            m_log->appendPlainText(chunk.trimmed());
            if (m_totalSteps > 0) {
                m_doneSteps += chunk.count(
                    QStringLiteral("castalia-install: "));
                m_progress->setValue(qMin(m_doneSteps, m_totalSteps));
            }
        });
        connect(m_proc,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus) {
                    stopSlideshow();
                    m_progress->setRange(0, qMax(1, m_totalSteps));
                    m_progress->setValue(qMax(1, m_totalSteps));
                    m_log->appendPlainText(
                        code == 0
                            ? QStringLiteral("\n✔ Instalación completada. "
                                             "Reinicia para usar Castalia OS.")
                            : QStringLiteral("\n✖ La instalación falló "
                                             "(código %1).").arg(code));
                });
        m_proc->start(QStringLiteral("python3"), backendArgs(false));
        // Hand the password to the backend over stdin (never argv), then
        // close the channel so its single readline() completes.
        if (!m_pass1->text().isEmpty() && m_proc->waitForStarted(3000)) {
            m_proc->write((m_pass1->text() + QLatin1Char('\n')).toUtf8());
            m_proc->closeWriteChannel();
        }
    }

    // ---- install slideshow -------------------------------------------------
    void startSlideshow()
    {
        if (!m_tipTimer) {
            m_tipTimer = new QTimer(this);
            m_tipTimer->setInterval(7000);
            connect(m_tipTimer, &QTimer::timeout, this, [this]() {
                m_tip->setText(tipText(++m_tipIndex));
                if (castalia::reduceMotion())
                    return;
                auto *fx = new QGraphicsOpacityEffect(m_tip);
                m_tip->setGraphicsEffect(fx);
                auto *anim = new QPropertyAnimation(fx, "opacity", m_tip);
                anim->setDuration(350);
                anim->setStartValue(0.0);
                anim->setEndValue(1.0);
                connect(anim, &QPropertyAnimation::finished, m_tip, [this]() {
                    m_tip->setGraphicsEffect(nullptr);
                });
                anim->start(QAbstractAnimation::DeleteWhenStopped);
            });
        }
        m_tipTimer->start();
    }

    void stopSlideshow()
    {
        if (m_tipTimer)
            m_tipTimer->stop();
    }

    QString tok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }

    QString m_repo;
    ThemeTokens m_tokens;
    bool m_demo;
    QStackedWidget *m_stack = nullptr;
    QVector<QLabel *> m_steps;
    QPushButton *m_back = nullptr, *m_next = nullptr;
    QListWidget *m_diskList = nullptr;
    QLineEdit *m_fullName = nullptr, *m_username = nullptr, *m_hostname = nullptr;
    QLineEdit *m_pass1 = nullptr, *m_pass2 = nullptr, *m_confirm = nullptr;
    QCheckBox *m_autologin = nullptr;
    QLabel *m_userHint = nullptr, *m_confirmLbl = nullptr;
    QLabel *m_tip = nullptr;
    QTimer *m_tipTimer = nullptr;
    QPlainTextEdit *m_plan = nullptr, *m_log = nullptr;
    QProgressBar *m_progress = nullptr;
    QProcess *m_proc = nullptr;
    int m_totalSteps = 0, m_doneSteps = 0, m_tipIndex = 0;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-instalador"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Use a synthetic disk (no probing)")});
    cli.addOption({QStringLiteral("page"), QStringLiteral("Initial page 0-4"),
                   QStringLiteral("n"), QStringLiteral("0")});
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
        QStringLiteral("#InstPrimary{padding:4px 18px;}"));

    const bool demo = cli.isSet(QStringLiteral("demo"))
                      || cli.isSet(QStringLiteral("screenshot"));
    Installer w(repo, tokens, demo);
    w.showPage(cli.value(QStringLiteral("page")).toInt());
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(200, &app, [&]() {
            w.grab().save(shot);
            app.quit();
        });
    return app.exec();
}

#include "main.moc"
