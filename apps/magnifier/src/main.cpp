// castalia-lupa — Lupa (Bible §9.3 "Accessibility"). A screen magnifier: it
// grabs the area around the pointer and shows it enlarged, for anyone who
// needs a closer look. Pure Qt5 (QScreen::grabWindow) + libcastalia-ui
// theming; no third-party assets (§3.9).
//
// Usage: castalia-lupa --theme classic [--repo P] [--zoom N]
//        [--demo] [--screenshot out.png]

#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QCursor>
#include <QDir>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

#include "Theme.h"

// The magnified view. Its source is either a live screen grab (following the
// cursor) or, in demo mode, a fixed sample image so it renders meaningfully
// with no display attached.
class MagView : public QWidget {
    Q_OBJECT
public:
    explicit MagView(const QColor &accent, QWidget *parent = nullptr)
        : QWidget(parent), m_accent(accent)
    {
        setMinimumSize(480, 300);
        setAutoFillBackground(true);
    }

    void setZoom(int z) { m_zoom = std::clamp(z, 2, 12); update(); }
    int zoom() const { return m_zoom; }
    void setFollow(bool f) { m_follow = f; }

    // Live mode: the source is grabbed from the screen each refresh.
    void setLive() { m_demo = false; }
    // Demo mode: magnify a fixed image centred, no screen needed.
    void setDemoSource(const QPixmap &pm)
    {
        m_demo = true;
        m_source = pm;
        m_focus = QPoint(int(pm.width() * 0.5), int(pm.height() * 0.5));
        update();
    }

    void refresh()
    {
        if (m_demo)
            return; // demo source is static
        QScreen *scr = QGuiApplication::primaryScreen();
        if (!scr)
            return;
        m_source = scr->grabWindow(0);
        // grabWindow(0) starts at the screen's top-left, so map the global
        // cursor into the pixmap by subtracting that origin.
        m_focus = m_follow
                      ? (QCursor::pos() - scr->geometry().topLeft())
                      : QPoint(m_source.width() / 2, m_source.height() / 2);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x10, 0x14, 0x18));
        if (m_source.isNull()) {
            p.setPen(Qt::white);
            p.drawText(rect(), Qt::AlignCenter,
                       tr("Sin imagen que ampliar."));
            frame(p);
            return;
        }
        // The source rectangle that maps onto this view at the current zoom.
        const int cw = std::max(1, width() / m_zoom);
        const int ch = std::max(1, height() / m_zoom);
        int sx = m_focus.x() - cw / 2;
        int sy = m_focus.y() - ch / 2;
        sx = std::clamp(sx, 0, std::max(0, m_source.width() - cw));
        sy = std::clamp(sy, 0, std::max(0, m_source.height() - ch));
        const QRect src(sx, sy, std::min(cw, m_source.width()),
                        std::min(ch, m_source.height()));
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom <= 4);
        p.drawPixmap(rect(), m_source, src);
        // crosshair at the focus point
        p.setPen(QPen(m_accent.lighter(130), 1.2));
        const QPoint c = rect().center();
        p.drawLine(c.x() - 12, c.y(), c.x() + 12, c.y());
        p.drawLine(c.x(), c.y() - 12, c.x(), c.y() + 12);
        frame(p);
    }

private:
    void frame(QPainter &p)
    {
        p.setPen(QPen(m_accent.darker(115), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }

    QColor m_accent;
    QPixmap m_source;
    QPoint m_focus;
    int m_zoom = 3;
    bool m_follow = true;
    bool m_demo = false;
};

class Lupa : public QWidget {
    Q_OBJECT
public:
    explicit Lupa(const QColor &accent) : m_accent(accent)
    {
        setWindowTitle(QStringLiteral("Lupa — Castalia"));
        resize(560, 420);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->addWidget(buildHeader());

        auto *body = new QVBoxLayout;
        body->setContentsMargins(12, 10, 12, 12);
        body->setSpacing(8);
        m_view = new MagView(m_accent, this);
        body->addWidget(m_view, 1);

        auto *ctl = new QHBoxLayout;
        auto *zl = new QLabel(QStringLiteral("Aumento:"), this);
        zl->setProperty("secondary", true);
        ctl->addWidget(zl);
        for (int z : {2, 4, 8}) {
            auto *b = new QPushButton(QStringLiteral("%1×").arg(z), this);
            b->setCheckable(true);
            b->setChecked(z == m_view->zoom());
            connect(b, &QPushButton::clicked, this, [this, z]() {
                m_view->setZoom(z);
                syncZoomButtons();
            });
            m_zoomBtns.append({z, b});
            ctl->addWidget(b);
        }
        ctl->addStretch(1);
        m_follow = new QCheckBox(QStringLiteral("Seguir el cursor"), this);
        m_follow->setChecked(true);
        connect(m_follow, &QCheckBox::toggled, this,
                [this](bool on) { m_view->setFollow(on); });
        ctl->addWidget(m_follow);
        body->addLayout(ctl);
        root->addLayout(body, 1);

        m_timer = new QTimer(this);
        m_timer->setInterval(60);
        connect(m_timer, &QTimer::timeout, this, [this]() { m_view->refresh(); });
    }

    MagView *view() { return m_view; }

    void startLive()
    {
        m_view->setLive();
        m_timer->start();
        m_view->refresh();
    }

    void showDemo(const QPixmap &pm)
    {
        m_follow->setChecked(false);
        m_follow->setEnabled(false);
        m_view->setZoom(3);
        syncZoomButtons();
        m_view->setDemoSource(pm);
    }

private:
    QWidget *buildHeader()
    {
        auto *head = new QWidget(this);
        head->setFixedHeight(46);
        head->setStyleSheet(QStringLiteral(
            "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %1,"
            "stop:1 %2);")
            .arg(m_accent.lighter(112).name(), m_accent.darker(118).name()));
        auto *l = new QHBoxLayout(head);
        l->setContentsMargins(16, 0, 16, 0);
        auto *t = new QLabel(QStringLiteral(
            "<span style='color:white;font-size:15px;font-weight:bold'>"
            "Lupa</span>"), head);
        l->addWidget(t);
        l->addStretch(1);
        auto *hint = new QLabel(
            QStringLiteral("Amplía la zona bajo el puntero"), head);
        hint->setStyleSheet(QStringLiteral("color:#EAF1F7;"));
        l->addWidget(hint);
        return head;
    }

    void syncZoomButtons()
    {
        for (auto &zb : m_zoomBtns)
            zb.second->setChecked(zb.first == m_view->zoom());
    }

    QColor m_accent;
    MagView *m_view = nullptr;
    QCheckBox *m_follow = nullptr;
    QTimer *m_timer = nullptr;
    QVector<QPair<int, QPushButton *>> m_zoomBtns;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-lupa"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("zoom"), QStringLiteral("Initial zoom"),
                   QStringLiteral("n"), QStringLiteral("3")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Magnify a bundled sample image")});
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

    Lupa w(accent);
    bool zok = false;
    const int z = cli.value(QStringLiteral("zoom")).toInt(&zok);
    if (zok)
        w.view()->setZoom(z);

    const bool demo = cli.isSet(QStringLiteral("demo"));
    const bool shot = !cli.value(QStringLiteral("screenshot")).isEmpty();
    if (demo || shot) {
        // A detail-rich screenshot makes the magnification obvious; fall back
        // to the live desktop shot, then to a flat fill if neither is present.
        QPixmap sample(repo + QStringLiteral("/docs/evidence/g-diagnostico.png"));
        if (sample.isNull())
            sample = QPixmap(repo + QStringLiteral("/docs/evidence/g-monitor.png"));
        if (sample.isNull())
            sample = QPixmap(
                repo + QStringLiteral("/docs/evidence/phase2-desktop-live.png"));
        if (sample.isNull()) {
            sample = QPixmap(600, 400);
            sample.fill(accent.lighter(140));
        }
        w.showDemo(sample);
    } else {
        w.startLive();
    }
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
