// castalia-volumen — the Castalia Volume Control (Bible §9, §10 XP-parity).
//
// A small, honest mixer: one slider and a mute button that drive the system's
// default audio sink. It speaks whichever backend the machine actually has —
// PulseAudio/PipeWire via `pactl`, or ALSA via `amixer` — and says so; on a
// machine with no sound stack it explains that instead of pretending. Opens
// from the panel's tray speaker. Pure Qt5 + libcastalia-ui theming.
//
// Usage: castalia-volumen --theme human [--repo PATH] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include "Theme.h"

namespace {

// The mixer backend actually present, in preference order.
enum class Kind { None, Pulse, Alsa };
struct Backend {
    Kind kind = Kind::None;
    QString name;
};
Backend detectBackend()
{
    if (!QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty())
        return {Kind::Pulse, QStringLiteral("PulseAudio / PipeWire")};
    if (!QStandardPaths::findExecutable(QStringLiteral("amixer")).isEmpty())
        return {Kind::Alsa, QStringLiteral("ALSA")};
    return {Kind::None, QString()};
}

// Run a command, return trimmed stdout (empty on failure/timeout).
QString run(const QString &bin, const QStringList &args)
{
    QProcess p;
    p.start(bin, args);
    if (!p.waitForFinished(1200))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput());
}

// First "NN%" in the text, or -1.
int firstPercent(const QString &text)
{
    QRegularExpression re(QStringLiteral("(\\d{1,3})%"));
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

} // namespace

class VolumeControl : public QWidget {
    Q_OBJECT
public:
    VolumeControl(const QString &repo, const ThemeTokens &tokens)
        : m_repo(repo), m_tokens(tokens), m_backend(detectBackend())
    {
        setWindowTitle(QStringLiteral("Control de volumen — Castalia"));
        resize(320, 180);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("VcHeader"));
        head->setFixedHeight(50);
        head->setStyleSheet(QStringLiteral(
            "#VcHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *title = new QLabel(head);
        const QString sub = m_backend.kind == Kind::None
            ? QStringLiteral("Sin sistema de sonido")
            : m_backend.name;
        title->setText(QStringLiteral(
            "<span style='color:%1;font-weight:bold'>Volumen</span>"
            "&nbsp;&nbsp;<span style='color:%1'>%2</span>")
            .arg(colorTok("titlebar_text"), sub));
        hl->addWidget(title);
        hl->addStretch(1);
        root->addWidget(head);

        auto *body = new QVBoxLayout;
        body->setContentsMargins(18, 16, 18, 16);
        body->setSpacing(12);

        auto *row = new QHBoxLayout;
        m_mute = new QPushButton(this);
        m_mute->setCheckable(true);
        m_mute->setCursor(Qt::PointingHandCursor);
        m_mute->setIcon(castalia::themeIcon(m_repo, QStringLiteral("speaker")));
        m_mute->setIconSize(QSize(22, 22));
        m_mute->setFixedWidth(44);
        connect(m_mute, &QPushButton::clicked, this, &VolumeControl::toggleMute);
        row->addWidget(m_mute);

        m_slider = new QSlider(Qt::Horizontal, this);
        m_slider->setRange(0, 100);
        m_slider->setEnabled(m_backend.kind != Kind::None);
        connect(m_slider, &QSlider::valueChanged, this,
                &VolumeControl::onSliderMoved);
        row->addWidget(m_slider, 1);

        m_pct = new QLabel(this);
        m_pct->setFixedWidth(44);
        m_pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(m_pct);
        body->addLayout(row);

        m_note = new QLabel(this);
        m_note->setWordWrap(true);
        m_note->setProperty("secondary", true);
        if (m_backend.kind == Kind::None)
            m_note->setText(QStringLiteral(
                "No se detecta PulseAudio/PipeWire ni ALSA. El control de "
                "volumen aparecerá cuando haya un sistema de sonido."));
        body->addWidget(m_note);
        root->addLayout(body);

        // Debounce writes so dragging the slider doesn't spawn a process per
        // pixel; the last value within 120 ms is the one applied.
        m_apply = new QTimer(this);
        m_apply->setSingleShot(true);
        m_apply->setInterval(120);
        connect(m_apply, &QTimer::timeout, this, &VolumeControl::applyVolume);

        loadState();
    }

private slots:
    void onSliderMoved(int)
    {
        updatePct();
        if (m_backend.kind != Kind::None && !m_loading)
            m_apply->start();
    }

    void applyVolume()
    {
        const int v = m_slider->value();
        if (m_backend.kind == Kind::Pulse)
            QProcess::startDetached(QStringLiteral("pactl"),
                {QStringLiteral("set-sink-volume"),
                 QStringLiteral("@DEFAULT_SINK@"),
                 QStringLiteral("%1%").arg(v)});
        else if (m_backend.kind == Kind::Alsa)
            QProcess::startDetached(QStringLiteral("amixer"),
                {QStringLiteral("set"), QStringLiteral("Master"),
                 QStringLiteral("%1%").arg(v)});
    }

    void toggleMute()
    {
        if (m_backend.kind == Kind::Pulse)
            QProcess::startDetached(QStringLiteral("pactl"),
                {QStringLiteral("set-sink-mute"),
                 QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("toggle")});
        else if (m_backend.kind == Kind::Alsa)
            QProcess::startDetached(QStringLiteral("amixer"),
                {QStringLiteral("set"), QStringLiteral("Master"),
                 QStringLiteral("toggle")});
        m_slider->setEnabled(m_backend.kind != Kind::None
                             && !m_mute->isChecked());
        updatePct();
    }

private:
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }
    void updatePct()
    {
        m_pct->setText(m_mute->isChecked()
            ? QStringLiteral("mudo")
            : QStringLiteral("%1%").arg(m_slider->value()));
    }
    // Read the current level + mute state from the backend (best effort).
    void loadState()
    {
        m_loading = true;
        int vol = 50;
        bool muted = false;
        if (m_backend.kind == Kind::Pulse) {
            const int p = firstPercent(
                run(QStringLiteral("pactl"),
                    {QStringLiteral("get-sink-volume"),
                     QStringLiteral("@DEFAULT_SINK@")}));
            if (p >= 0)
                vol = p;
            muted = run(QStringLiteral("pactl"),
                        {QStringLiteral("get-sink-mute"),
                         QStringLiteral("@DEFAULT_SINK@")})
                        .contains(QStringLiteral("yes"));
        } else if (m_backend.kind == Kind::Alsa) {
            const QString out = run(QStringLiteral("amixer"),
                                    {QStringLiteral("get"),
                                     QStringLiteral("Master")});
            const int p = firstPercent(out);
            if (p >= 0)
                vol = p;
            muted = out.contains(QStringLiteral("[off]"));
        }
        m_slider->setValue(vol);
        m_mute->setChecked(muted);
        m_slider->setEnabled(m_backend.kind != Kind::None && !muted);
        updatePct();
        m_loading = false;
    }

    QString m_repo;
    ThemeTokens m_tokens;
    Backend m_backend;
    QSlider *m_slider = nullptr;
    QPushButton *m_mute = nullptr;
    QLabel *m_pct = nullptr, *m_note = nullptr;
    QTimer *m_apply = nullptr;
    bool m_loading = false;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-volumen"));
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
    const ThemeTokens tokens = castalia::applyTheme(&app, repo, themeId);

    VolumeControl w(repo, tokens);
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
