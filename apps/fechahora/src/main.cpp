// castalia-fechahora — the Castalia Date & Time control panel (Bible §9,
// §10 XP-parity).
//
// The XP-era "Fecha y hora" applet, done honestly. It always shows a live
// Spanish clock and the machine's real time zone (read with no external
// process, from Qt's own IANA database), so it renders anywhere — including
// the offscreen CI gate. The *settable* parts speak the backend the machine
// actually has: `timedatectl` (systemd). It reports whether the clock is
// network-synchronised (NTP) and lets you change the time zone or toggle NTP;
// on a machine without `timedatectl` it says so instead of pretending. The
// 12/24-hour choice is a pure display preference, saved per-user.
//
// Applying a time-zone / NTP change goes through `timedatectl`, which asks
// polkit for authorisation itself — we never handle privileges here.
//
// Usage: castalia-fechahora --theme human [--repo PATH] [--screenshot out.png]

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>

#include "Theme.h"

namespace {

// Is systemd's timedatectl available? It is the only thing here that can
// actually change the clock configuration; without it we stay read-only.
bool haveTimedatectl()
{
    return !QStandardPaths::findExecutable(QStringLiteral("timedatectl"))
                .isEmpty();
}

// `timedatectl show` prints stable key=value lines (Timezone=…, NTP=yes,
// NTPSynchronized=yes). Parse one key; empty if absent or no backend.
QString timedatectlValue(const QString &key)
{
    QProcess p;
    p.start(QStringLiteral("timedatectl"), {QStringLiteral("show")});
    if (!p.waitForFinished(1200))
        return QString();
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    const auto lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &ln : lines) {
        const int eq = ln.indexOf(QLatin1Char('='));
        if (eq > 0 && ln.left(eq) == key)
            return ln.mid(eq + 1).trimmed();
    }
    return QString();
}

// Where the 12/24-hour display preference lives (one tiny line).
QString prefFile()
{
    const QString home = qEnvironmentVariable("HOME", QStringLiteral("/root"));
    return home + QStringLiteral("/.config/castalia/fechahora.conf");
}

bool loadUse24h()
{
    QFile f(prefFile());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return true;                 // 24-hour is the Spanish-default
    return !QString::fromUtf8(f.readAll()).contains(QStringLiteral("12"));
}

void saveUse24h(bool use24h)
{
    QDir().mkpath(QFileInfo(prefFile()).absolutePath());
    QFile f(prefFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(use24h ? "format=24\n" : "format=12\n");
}

} // namespace

class FechaHora : public QWidget {
    Q_OBJECT
public:
    FechaHora(const QString &repo, const ThemeTokens &tokens)
        : m_repo(repo), m_tokens(tokens), m_backend(haveTimedatectl()),
          m_use24h(loadUse24h())
    {
        setWindowTitle(QStringLiteral("Fecha y hora — Castalia"));
        resize(460, 340);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // header on the titlebar gradient (matches the other first-party apps)
        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("FhHeader"));
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "#FhHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *title = new QLabel(head);
        const QString sub = m_backend
            ? QStringLiteral("systemd · timedatectl")
            : QStringLiteral("solo lectura");
        title->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>Fecha y "
            "hora</span>&nbsp;&nbsp;<span style='color:%1'>%2</span>")
            .arg(colorTok("titlebar_text"), sub));
        hl->addWidget(title);
        hl->addStretch(1);
        root->addWidget(head);

        auto *body = new QVBoxLayout;
        body->setContentsMargins(20, 16, 20, 18);
        body->setSpacing(12);

        // The live readout — the reason most people open this at all.
        m_clock = new QLabel(this);
        m_clock->setAlignment(Qt::AlignHCenter);
        m_clock->setStyleSheet(QStringLiteral(
            "font-size:40px;font-weight:bold;color:%1;")
            .arg(colorTok("accent")));
        body->addWidget(m_clock);
        m_date = new QLabel(this);
        m_date->setAlignment(Qt::AlignHCenter);
        m_date->setStyleSheet(QStringLiteral("font-size:14px;"));
        body->addWidget(m_date);
        body->addSpacing(4);

        // Settings form: time zone, NTP, display format.
        auto *form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(8);

        m_tz = new QComboBox(this);
        m_tz->setMaxVisibleItems(18);
        for (const QByteArray &id : QTimeZone::availableTimeZoneIds())
            m_tz->addItem(QString::fromUtf8(id));
        const QString curTz = currentZoneId();
        int idx = m_tz->findText(curTz);
        if (idx < 0 && !curTz.isEmpty()) {
            // The machine's zone id isn't a verbatim entry in Qt's list (an
            // alias like "Etc/UTC"): show the real zone rather than silently
            // defaulting to the first item in the list.
            m_tz->insertItem(0, curTz);
            idx = 0;
        }
        if (idx >= 0)
            m_tz->setCurrentIndex(idx);
        m_tz->setEnabled(m_backend);
        connect(m_tz, &QComboBox::currentTextChanged, this,
                [this]() { m_apply->setEnabled(m_backend); refreshClock(); });
        form->addRow(QStringLiteral("Zona horaria:"), m_tz);

        m_ntp = new QCheckBox(
            QStringLiteral("Sincronizar la hora con la red (NTP)"), this);
        m_ntp->setEnabled(m_backend);
        m_ntp->setChecked(
            timedatectlValue(QStringLiteral("NTP")) == QStringLiteral("yes"));
        connect(m_ntp, &QCheckBox::toggled, this, &FechaHora::onNtpToggled);
        form->addRow(QString(), m_ntp);

        m_fmt = new QCheckBox(QStringLiteral("Formato de 24 horas"), this);
        m_fmt->setChecked(m_use24h);
        connect(m_fmt, &QCheckBox::toggled, this, [this](bool on) {
            m_use24h = on;
            saveUse24h(on);
            refreshClock();
        });
        form->addRow(QString(), m_fmt);
        body->addLayout(form);

        // Apply row (time-zone change) + honest note.
        auto *actions = new QHBoxLayout;
        m_note = new QLabel(this);
        m_note->setWordWrap(true);
        m_note->setProperty("secondary", true);
        if (!m_backend)
            m_note->setText(QStringLiteral(
                "No se detecta timedatectl (systemd): la zona horaria y la "
                "sincronización se muestran, pero para cambiarlas hace falta "
                "systemd."));
        else
            m_note->setText(
                m_ntp->isChecked()
                    ? QStringLiteral("El reloj se mantiene con la red.")
                    : QStringLiteral("El reloj no se sincroniza con la red."));
        actions->addWidget(m_note, 1);
        m_apply = new QPushButton(QStringLiteral("Aplicar zona"), this);
        m_apply->setObjectName(QStringLiteral("FhApply"));
        m_apply->setCursor(Qt::PointingHandCursor);
        m_apply->setEnabled(false);
        connect(m_apply, &QPushButton::clicked, this, &FechaHora::applyZone);
        actions->addWidget(m_apply);
        body->addLayout(actions);
        root->addLayout(body);

        // Tick the live clock once a second (cheap; no animation budget).
        auto *tick = new QTimer(this);
        tick->setInterval(1000);
        connect(tick, &QTimer::timeout, this, &FechaHora::refreshClock);
        tick->start();
        refreshClock();
    }

private slots:
    void refreshClock()
    {
        const QLocale loc;
        QDateTime now = QDateTime::currentDateTime();
        // Preview the selected zone even before "Aplicar" — the readout
        // follows the combo so the choice is legible immediately.
        const QByteArray sel = m_tz->currentText().toUtf8();
        const QTimeZone zone(sel);
        if (zone.isValid())
            now = now.toTimeZone(zone);
        const QString timeFmt = m_use24h ? QStringLiteral("HH:mm:ss")
                                         : QStringLiteral("hh:mm:ss AP");
        m_clock->setText(loc.toString(now.time(), timeFmt));
        m_date->setText(loc.toString(
            now.date(), QStringLiteral("dddd, d 'de' MMMM 'de' yyyy")));
    }

    void onNtpToggled(bool on)
    {
        if (m_backend)
            QProcess::startDetached(
                QStringLiteral("timedatectl"),
                {QStringLiteral("set-ntp"),
                 on ? QStringLiteral("true") : QStringLiteral("false")});
        m_note->setText(on
            ? QStringLiteral("El reloj se mantiene con la red.")
            : QStringLiteral("El reloj no se sincroniza con la red."));
    }

    void applyZone()
    {
        if (!m_backend)
            return;
        QProcess::startDetached(
            QStringLiteral("timedatectl"),
            {QStringLiteral("set-timezone"), m_tz->currentText()});
        m_apply->setEnabled(false);
    }

private:
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }

    // The machine's current zone: timedatectl if present (authoritative),
    // otherwise Qt's own view of the system zone — no external process.
    QString currentZoneId() const
    {
        if (m_backend) {
            const QString tz = timedatectlValue(QStringLiteral("Timezone"));
            if (!tz.isEmpty())
                return tz;
        }
        return QString::fromUtf8(QTimeZone::systemTimeZoneId());
    }

    QString m_repo;
    ThemeTokens m_tokens;
    bool m_backend = false;
    bool m_use24h = true;
    QLabel *m_clock = nullptr, *m_date = nullptr, *m_note = nullptr;
    QComboBox *m_tz = nullptr;
    QCheckBox *m_ntp = nullptr, *m_fmt = nullptr;
    QPushButton *m_apply = nullptr;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-fechahora"));
    // Spanish-first UI (§3): day/month names read in Spanish regardless of
    // the host locale, matching the rest of Castalia.
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
        QStringLiteral("#FhApply{font-weight:bold;border-color:%1;}")
            .arg(accent));

    FechaHora w(repo, tokens);
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
