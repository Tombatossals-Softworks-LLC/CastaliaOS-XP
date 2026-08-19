// castalia-reloj — Reloj (Bible §9.3 "Accessories").
//
// A clock accessory: a natively-painted analog face, a digital readout with
// the date, a stopwatch and a simple alarm. Pure Qt5 + libcastalia-ui theming,
// the clock face drawn with QPainter — no third-party assets (§3.9).
//
// Usage: castalia-reloj --theme classic [--repo P] [--time HH:MM:SS]
//        [--screenshot out.png]

#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QTime>
#include <QTimeEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>

#include "Mark.h"
#include "Theme.h"

// A round analog clock face, drawn natively. If a fixed time is set it stays
// put (for deterministic screenshots); otherwise it follows the system clock.
class AnalogClock : public QWidget {
    Q_OBJECT
public:
    explicit AnalogClock(const QColor &accent, QWidget *parent = nullptr)
        : QWidget(parent), m_accent(accent)
    {
        setFixedSize(220, 220);
    }

    void setFixedTime(const QTime &t)
    {
        m_fixed = t;
        m_hasFixed = t.isValid();
        update();
    }

    QTime displayTime() const
    {
        return m_hasFixed ? m_fixed : QTime::currentTime();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const QTime t = displayTime();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c = QRectF(rect()).center();
        const qreal R = std::min(width(), height()) / 2.0 - 4;
        p.translate(c);

        // rim
        QRadialGradient rim(QPointF(0, 0), R);
        rim.setColorAt(0.90, m_accent.lighter(115));
        rim.setColorAt(1.0, m_accent.darker(125));
        p.setPen(Qt::NoPen);
        p.setBrush(rim);
        p.drawEllipse(QPointF(0, 0), R, R);
        // face
        QRadialGradient face(QPointF(0, -R * 0.2), R);
        face.setColorAt(0, QColor(0xFF, 0xFF, 0xFF));
        face.setColorAt(1, QColor(0xEC, 0xEF, 0xF3));
        p.setBrush(face);
        p.drawEllipse(QPointF(0, 0), R * 0.9, R * 0.9);

        // ticks
        for (int i = 0; i < 60; ++i) {
            p.save();
            p.rotate(i * 6.0);
            const bool hour = (i % 5 == 0);
            p.setPen(QPen(hour ? QColor(0x33, 0x3A, 0x42)
                               : QColor(0x9A, 0xA3, 0xAD),
                          hour ? 2.4 : 1.0));
            p.drawLine(QPointF(0, -R * 0.9), QPointF(0, -R * (hour ? 0.80
                                                                   : 0.85)));
            p.restore();
        }
        // hour numbers
        QFont f = p.font();
        f.setPixelSize(int(R * 0.16));
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(0x2A, 0x30, 0x38));
        for (int h = 1; h <= 12; ++h) {
            const double a = h * 30.0 * M_PI / 180.0;
            const QPointF pos(std::sin(a) * R * 0.66, -std::cos(a) * R * 0.66);
            p.drawText(QRectF(pos.x() - 16, pos.y() - 12, 32, 24),
                       Qt::AlignCenter, QString::number(h));
        }

        // hands
        const double sec = t.second() + t.msec() / 1000.0;
        const double minu = t.minute() + sec / 60.0;
        const double hour = (t.hour() % 12) + minu / 60.0;
        auto hand = [&](double deg, qreal len, qreal w, const QColor &col,
                        qreal back = 0.0) {
            p.save();
            p.rotate(deg);
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            QPainterPath path;
            path.moveTo(-w, back);
            path.lineTo(w, back);
            path.lineTo(0, -len);
            path.closeSubpath();
            p.drawPath(path);
            p.restore();
        };
        hand(hour * 30.0, R * 0.5, 4.2, QColor(0x2A, 0x30, 0x38), R * 0.12);
        hand(minu * 6.0, R * 0.75, 3.2, QColor(0x3A, 0x42, 0x4C), R * 0.14);
        // second hand
        p.save();
        p.rotate(sec * 6.0);
        p.setPen(QPen(m_accent.darker(115), 1.4));
        p.drawLine(QPointF(0, R * 0.18), QPointF(0, -R * 0.82));
        p.restore();
        // cap
        p.setPen(Qt::NoPen);
        p.setBrush(m_accent.darker(120));
        p.drawEllipse(QPointF(0, 0), 5, 5);
    }

private:
    QColor m_accent;
    QTime m_fixed;
    bool m_hasFixed = false;
};

class Reloj : public QWidget {
    Q_OBJECT
public:
    Reloj(const QColor &accent, const QTime &fixed) : m_accent(accent)
    {
        setWindowTitle(QStringLiteral("Reloj — Castalia"));
        resize(300, 470);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->addWidget(buildHeader());

        auto *body = new QVBoxLayout;
        body->setContentsMargins(16, 12, 16, 16);
        body->setSpacing(10);

        m_clock = new AnalogClock(m_accent, this);
        if (fixed.isValid())
            m_clock->setFixedTime(fixed);
        auto *clockRow = new QHBoxLayout;
        clockRow->addStretch(1);
        clockRow->addWidget(m_clock);
        clockRow->addStretch(1);
        body->addLayout(clockRow);

        m_digital = new QLabel(this);
        m_digital->setAlignment(Qt::AlignCenter);
        m_digital->setStyleSheet(QStringLiteral(
            "font-size:30px;font-weight:bold;color:%1;")
            .arg(m_accent.darker(120).name()));
        body->addWidget(m_digital);
        m_date = new QLabel(this);
        m_date->setAlignment(Qt::AlignCenter);
        m_date->setProperty("secondary", true);
        body->addWidget(m_date);

        body->addWidget(hline());

        // Stopwatch.
        auto *swTitle = new QLabel(QStringLiteral("Cronómetro"), this);
        swTitle->setStyleSheet(QStringLiteral("font-weight:bold;"));
        body->addWidget(swTitle);
        m_sw = new QLabel(QStringLiteral("00:00.0"), this);
        m_sw->setAlignment(Qt::AlignCenter);
        m_sw->setStyleSheet(QStringLiteral(
            "font-size:26px;font-family:'DejaVu Sans Mono';"));
        body->addWidget(m_sw);
        auto *swBtns = new QHBoxLayout;
        m_swBtn = new QPushButton(QStringLiteral("Iniciar"), this);
        m_swBtn->setObjectName(QStringLiteral("PrimaryBtn"));
        connect(m_swBtn, &QPushButton::clicked, this, &Reloj::toggleStopwatch);
        auto *swReset = new QPushButton(QStringLiteral("Reiniciar"), this);
        connect(swReset, &QPushButton::clicked, this, &Reloj::resetStopwatch);
        swBtns->addWidget(m_swBtn);
        swBtns->addWidget(swReset);
        body->addLayout(swBtns);

        body->addWidget(hline());

        // Alarm.
        auto *alRow = new QHBoxLayout;
        m_alarmOn = new QCheckBox(QStringLiteral("Alarma"), this);
        connect(m_alarmOn, &QCheckBox::toggled, this, [this](bool) {
            m_alarmFired = false;
            updateAlarmStatus();
        });
        m_alarmEdit = new QTimeEdit(this);
        m_alarmEdit->setDisplayFormat(QStringLiteral("HH:mm"));
        m_alarmEdit->setTime(QTime(7, 30));
        alRow->addWidget(m_alarmOn);
        alRow->addStretch(1);
        alRow->addWidget(m_alarmEdit);
        body->addLayout(alRow);
        m_alarmStatus = new QLabel(this);
        m_alarmStatus->setProperty("secondary", true);
        body->addWidget(m_alarmStatus);

        body->addStretch(1);
        root->addLayout(body, 1);

        m_tick = new QTimer(this);
        m_tick->setInterval(100);
        connect(m_tick, &QTimer::timeout, this, &Reloj::tick);
        m_tick->start();
        tick();
        updateAlarmStatus();
    }

private:
    QWidget *buildHeader()
    {
        auto *head = new QWidget(this);
        head->setFixedHeight(48);
        head->setStyleSheet(QStringLiteral(
            "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %1,"
            "stop:1 %2);")
            .arg(m_accent.lighter(112).name(), m_accent.darker(118).name()));
        auto *l = new QHBoxLayout(head);
        l->setContentsMargins(16, 0, 16, 0);
        auto *mk = new QLabel(head);
        QPixmap pm(30, 30);
        pm.fill(Qt::transparent);
        { QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
          castalia::drawMark(&p, QRectF(1, 1, 28, 28)); }
        mk->setPixmap(pm);
        l->addWidget(mk);
        auto *t = new QLabel(QStringLiteral(
            "<span style='color:white;font-size:15px;font-weight:bold'>"
            "Reloj</span>"), head);
        l->addWidget(t);
        l->addStretch(1);
        return head;
    }

    QWidget *hline()
    {
        auto *w = new QWidget(this);
        w->setFixedHeight(1);
        w->setStyleSheet(QStringLiteral("background:rgba(0,0,0,40);"));
        return w;
    }

    void tick()
    {
        m_clock->update();
        const QTime t = m_clock->displayTime();
        m_digital->setText(t.toString(QStringLiteral("HH:mm:ss")));
        const QDate d = QDate::currentDate();
        QString ds = QLocale(QLocale::Spanish)
                         .toString(d, QStringLiteral("dddd d 'de' MMMM 'de' "
                                                     "yyyy"));
        if (!ds.isEmpty())
            ds[0] = ds[0].toUpper();
        m_date->setText(ds);

        if (m_swRunning)
            refreshStopwatch();

        // Alarm check (only when following the live clock).
        if (m_alarmOn->isChecked() && !m_alarmFired) {
            const QTime now = QTime::currentTime();
            if (now.hour() == m_alarmEdit->time().hour()
                && now.minute() == m_alarmEdit->time().minute()) {
                m_alarmFired = true;
                QApplication::beep();
                updateAlarmStatus();
            }
        }
    }

    void toggleStopwatch()
    {
        if (m_swRunning) {
            m_swAccum += m_swElapsed.elapsed();
            m_swRunning = false;
            m_swBtn->setText(QStringLiteral("Reanudar"));
        } else {
            m_swElapsed.restart();
            m_swRunning = true;
            m_swBtn->setText(QStringLiteral("Parar"));
        }
    }

    void resetStopwatch()
    {
        m_swRunning = false;
        m_swAccum = 0;
        m_swBtn->setText(QStringLiteral("Iniciar"));
        m_sw->setText(QStringLiteral("00:00.0"));
    }

    void refreshStopwatch()
    {
        const qint64 ms = m_swAccum
                          + (m_swRunning ? m_swElapsed.elapsed() : 0);
        const qint64 tenths = (ms / 100) % 10;
        const qint64 secs = (ms / 1000) % 60;
        const qint64 mins = ms / 60000;
        m_sw->setText(QStringLiteral("%1:%2.%3")
                          .arg(mins, 2, 10, QLatin1Char('0'))
                          .arg(secs, 2, 10, QLatin1Char('0'))
                          .arg(tenths));
    }

    void updateAlarmStatus()
    {
        if (!m_alarmOn->isChecked())
            m_alarmStatus->setText(QStringLiteral("Alarma desactivada."));
        else if (m_alarmFired)
            m_alarmStatus->setText(QStringLiteral("⏰ ¡Es la hora!"));
        else
            m_alarmStatus->setText(
                QStringLiteral("Sonará a las %1.")
                    .arg(m_alarmEdit->time().toString(
                        QStringLiteral("HH:mm"))));
    }

    QColor m_accent;
    AnalogClock *m_clock = nullptr;
    QLabel *m_digital = nullptr, *m_date = nullptr, *m_sw = nullptr,
           *m_alarmStatus = nullptr;
    QPushButton *m_swBtn = nullptr;
    QCheckBox *m_alarmOn = nullptr;
    QTimeEdit *m_alarmEdit = nullptr;
    QTimer *m_tick = nullptr;
    QElapsedTimer m_swElapsed;
    qint64 m_swAccum = 0;
    bool m_swRunning = false, m_alarmFired = false;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-reloj"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("time"),
                   QStringLiteral("Freeze the hands at HH:MM:SS"),
                   QStringLiteral("hms")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo"))).absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const QString accentStr =
        ThemeTokens::load(castalia::themeConfPath(repo, themeId))
            .str(QStringLiteral("colors"), QStringLiteral("accent"));
    castalia::applyTheme(&app, repo, themeId);
    const QColor accent(accentStr.isEmpty() ? QStringLiteral("#3E82B6")
                                            : accentStr);

    QTime fixed;
    const QString ts = cli.value(QStringLiteral("time"));
    if (!ts.isEmpty()) {
        fixed = QTime::fromString(ts, QStringLiteral("HH:mm:ss"));
        if (!fixed.isValid())
            fixed = QTime::fromString(ts, QStringLiteral("HH:mm"));
    }
    // A pleasant default pose for screenshots when no --time is given.
    if (!cli.value(QStringLiteral("screenshot")).isEmpty() && !fixed.isValid())
        fixed = QTime(10, 9, 36);

    Reloj w(accent, fixed);
    w.show();

    const QString out = cli.value(QStringLiteral("screenshot"));
    if (!out.isEmpty())
        QTimer::singleShot(300, &app, [&]() {
            w.grab().save(out);
            app.quit();
        });
    return app.exec();
}

#include "main.moc"
