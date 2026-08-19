// castalia-captura — the Screenshot tool (Bible §9.3).
//
// Capture the whole screen or a dragged region, preview it, then save (auto-
// named into ~/Imágenes/Capturas) or copy to the clipboard. Self-contained:
// QScreen::grabWindow — no external tool. A headless `--capture full --out`
// mode grabs and saves for scripts/proof. Themed via libcastalia-ui.
//
// Usage: castalia-captura --theme classic [--repo P] [--capture full --out f]
//        [--demo] [--screenshot out.png]

#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRubberBand>
#include <QScreen>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

#include "Mark.h"
#include "Notify.h"
#include "Theme.h"

namespace {
QPixmap grabScreen()
{
    QScreen *s = QGuiApplication::primaryScreen();
    return s ? s->grabWindow(0) : QPixmap();
}
} // namespace

// Fullscreen overlay to rubber-band a region out of an already-grabbed shot.
class RegionOverlay : public QWidget {
public:
    explicit RegionOverlay(const QPixmap &full)
        : m_full(full)
    {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                       | Qt::BypassWindowManagerHint);
        setCursor(Qt::CrossCursor);
        if (QScreen *s = QGuiApplication::primaryScreen())
            setGeometry(s->geometry());
        m_band = new QRubberBand(QRubberBand::Rectangle, this);
    }
    QPixmap result() const { return m_result; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.drawPixmap(0, 0, m_full);
        p.fillRect(rect(), QColor(0, 0, 0, 90));
        p.setPen(QColor(255, 255, 255, 180));
        p.drawText(rect().adjusted(0, 24, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
                   QStringLiteral("Arrastra para seleccionar · Esc para "
                                  "cancelar"));
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        m_origin = e->pos();
        m_band->setGeometry(QRect(m_origin, QSize()));
        m_band->show();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_band->isVisible())
            m_band->setGeometry(QRect(m_origin, e->pos()).normalized());
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        const QRect r = QRect(m_origin, e->pos()).normalized();
        if (r.width() > 4 && r.height() > 4)
            m_result = m_full.copy(r);
        close();
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Escape)
            close();
    }

private:
    QPixmap m_full, m_result;
    QRubberBand *m_band = nullptr;
    QPoint m_origin;
};

class Shooter : public QWidget {
public:
    Shooter(const QString &repo, const QColor &accent)
        : m_repo(repo), m_accent(accent)
    {
        setWindowTitle(QStringLiteral("Captura de pantalla — Castalia"));
        resize(560, 440);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->addWidget(buildBar());

        m_preview = new QLabel(this);
        m_preview->setAlignment(Qt::AlignCenter);
        m_preview->setMinimumHeight(280);
        m_preview->setStyleSheet(QStringLiteral(
            "background:#0B141E;color:#9DB3C6;border:1px solid palette(mid);"
            "margin:12px;border-radius:6px;"));
        m_preview->setText(QStringLiteral("Elige un modo de captura arriba."));
        root->addWidget(m_preview, 1);

        m_status = new QLabel(this);
        m_status->setContentsMargins(14, 4, 14, 10);
        m_status->setProperty("secondary", true);
        root->addWidget(m_status);
    }

    void showDemo()
    {
        QPixmap pm(repoImg());
        if (pm.isNull()) {
            pm = QPixmap(480, 300);
            pm.fill(m_accent);
        }
        setResult(pm);
        m_status->setText(QStringLiteral("Vista de ejemplo."));
    }
    void captureAndSave(const QString &out)
    {
        const QPixmap pm = grabScreen();
        if (!pm.isNull())
            pm.save(out);
    }

private:
    QString repoImg() const
    {
        return m_repo + QStringLiteral(
            "/docs/evidence/phase2-desktop-live.png");
    }

    QWidget *buildBar()
    {
        auto *bar = new QWidget(this);
        bar->setObjectName(QStringLiteral("ShotBar"));
        bar->setStyleSheet(QStringLiteral(
            "#ShotBar{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(m_accent.lighter(112).name(), m_accent.darker(118).name()));
        auto *l = new QHBoxLayout(bar);
        l->setContentsMargins(12, 8, 12, 8);
        l->setSpacing(8);
        auto whiteBtn = [&](const QString &t) {
            auto *b = new QPushButton(t, bar);
            b->setStyleSheet(QStringLiteral(
                "QPushButton{background:rgba(255,255,255,0.9);color:#1E1E1E;"
                "font-weight:bold;border:none;border-radius:4px;padding:5px "
                "12px;}QPushButton:hover{background:white;}"));
            return b;
        };
        auto *full = whiteBtn(QStringLiteral("Pantalla completa"));
        auto *region = whiteBtn(QStringLiteral("Región"));
        connect(full, &QPushButton::clicked, this, &Shooter::captureFull);
        connect(region, &QPushButton::clicked, this, &Shooter::captureRegion);
        l->addWidget(full);
        l->addWidget(region);
        l->addStretch(1);
        auto *dly = new QLabel(QStringLiteral(
            "<span style='color:white'>Retardo</span>"), bar);
        l->addWidget(dly);
        m_delay = new QSpinBox(bar);
        m_delay->setRange(0, 10);
        m_delay->setSuffix(QStringLiteral(" s"));
        l->addWidget(m_delay);
        m_save = whiteBtn(QStringLiteral("Guardar"));
        m_copy = whiteBtn(QStringLiteral("Copiar"));
        connect(m_save, &QPushButton::clicked, this, &Shooter::save);
        connect(m_copy, &QPushButton::clicked, this, [this]() {
            if (!m_shot.isNull())
                QApplication::clipboard()->setPixmap(m_shot);
        });
        m_save->setEnabled(false);
        m_copy->setEnabled(false);
        l->addWidget(m_save);
        l->addWidget(m_copy);
        return bar;
    }

    void setResult(const QPixmap &pm)
    {
        m_shot = pm;
        m_preview->setPixmap(pm.scaled(m_preview->size() - QSize(8, 8),
                                       Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
        m_save->setEnabled(!pm.isNull());
        m_copy->setEnabled(!pm.isNull());
    }

    void afterDelay(const std::function<void()> &fn)
    {
        hide();
        QTimer::singleShot(m_delay->value() * 1000 + 250, this, [this, fn]() {
            fn();
            show();
            raise();
        });
    }

    void captureFull()
    {
        afterDelay([this]() {
            const QPixmap pm = grabScreen();
            setResult(pm);
            m_status->setText(pm.isNull()
                ? QStringLiteral("No se pudo capturar la pantalla.")
                : QStringLiteral("Pantalla completa · %1×%2")
                      .arg(pm.width()).arg(pm.height()));
        });
    }

    void captureRegion()
    {
        afterDelay([this]() {
            const QPixmap full = grabScreen();
            if (full.isNull())
                return;
            auto *ov = new RegionOverlay(full);
            ov->setAttribute(Qt::WA_DeleteOnClose, false);
            ov->showFullScreen();
            // modal-ish: process until closed
            while (ov->isVisible())
                QApplication::processEvents(QEventLoop::WaitForMoreEvents);
            if (!ov->result().isNull()) {
                setResult(ov->result());
                m_status->setText(QStringLiteral("Región · %1×%2")
                                      .arg(ov->result().width())
                                      .arg(ov->result().height()));
            }
            ov->deleteLater();
        });
    }

    void save()
    {
        if (m_shot.isNull())
            return;
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
            + QStringLiteral("/Capturas");
        QDir().mkpath(dir);
        int n = 1;
        QString path;
        do {
            path = QStringLiteral("%1/captura-%2.png").arg(dir)
                       .arg(n++, 3, 10, QLatin1Char('0'));
        } while (QFileInfo::exists(path) && n < 10000);
        if (m_shot.save(path)) {
            m_status->setText(QStringLiteral("Guardado en %1").arg(path));
            castalia::notify(QStringLiteral("Captura de pantalla"),
                             QStringLiteral("Captura guardada"), path,
                             QStringLiteral("camera"));
        }
    }

    QString m_repo;
    QColor m_accent;
    QLabel *m_preview = nullptr, *m_status = nullptr;
    QSpinBox *m_delay = nullptr;
    QPushButton *m_save = nullptr, *m_copy = nullptr;
    QPixmap m_shot;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-captura"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("capture"),
                   QStringLiteral("Headless: full → --out PNG"),
                   QStringLiteral("mode")});
    cli.addOption({QStringLiteral("out"), QStringLiteral("Output PNG"),
                   QStringLiteral("file")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Show a sample capture")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render UI to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const QString accentStr =
        ThemeTokens::load(castalia::themeConfPath(repo, themeId))
            .str(QStringLiteral("colors"), QStringLiteral("accent"));
    castalia::applyTheme(&app, repo, themeId);
    const QColor accent(accentStr.isEmpty() ? QStringLiteral("#3E82B6")
                                            : accentStr);

    // Headless capture-and-save (scripts / proof).
    if (cli.value(QStringLiteral("capture")) == QStringLiteral("full")) {
        Shooter s(repo, accent);
        const QString out = cli.value(QStringLiteral("out"));
        s.captureAndSave(out.isEmpty() ? QStringLiteral("captura.png") : out);
        return 0;
    }

    Shooter w(repo, accent);
    const bool shot = !cli.value(QStringLiteral("screenshot")).isEmpty();
    if (cli.isSet(QStringLiteral("demo")) || shot)
        w.showDemo();
    w.show();

    const QString sh = cli.value(QStringLiteral("screenshot"));
    if (!sh.isEmpty())
        QTimer::singleShot(250, &app, [&]() {
            w.grab().save(sh);
            app.quit();
        });
    return app.exec();
}
