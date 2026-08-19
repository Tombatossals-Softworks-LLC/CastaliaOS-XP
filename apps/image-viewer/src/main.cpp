// castalia-visor — the Castalia image viewer (Bible §9.3 "Image Viewer").
//
// View/zoom/rotate images, step through a folder, fit-to-window. Pure Qt5 +
// libcastalia-ui theming. Supports SVG via Qt's imageformats when present.
//
// Usage:
//   castalia-visor [IMAGE] --theme classic [--repo PATH] [--demo]
//   [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QScrollArea>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include "Recent.h"
#include "Theme.h"

class Visor : public QMainWindow {
    Q_OBJECT
public:
    explicit Visor()
    {
        m_view = new QLabel(this);
        m_view->setAlignment(Qt::AlignCenter);
        m_view->setBackgroundRole(QPalette::Base);
        m_scroll = new QScrollArea(this);
        m_scroll->setWidget(m_view);
        m_scroll->setWidgetResizable(true);
        m_scroll->setFrameShape(QFrame::NoFrame);
        setCentralWidget(m_scroll);

        auto *bar = addToolBar(QStringLiteral("Ver"));
        bar->setMovable(false);
        bar->addAction(QStringLiteral("Abrir…"), this, &Visor::open);
        bar->addSeparator();
        bar->addAction(QStringLiteral("◀"), this, [this]() { step(-1); });
        bar->addAction(QStringLiteral("▶"), this, [this]() { step(1); });
        bar->addSeparator();
        bar->addAction(QStringLiteral("＋"), this, [this]() { zoom(1.25); });
        bar->addAction(QStringLiteral("－"), this, [this]() { zoom(0.8); });
        bar->addAction(QStringLiteral("Ajustar"), this, [this]() { fit(); });
        bar->addAction(QStringLiteral("⟳"), this, [this]() { rotate(); });

        m_info = new QLabel(this);
        statusBar()->addWidget(m_info);
        resize(720, 540);
    }

    void loadImage(const QString &path)
    {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QImage img = reader.read();
        if (img.isNull())
            return;
        m_path = path;
        m_pix = QPixmap::fromImage(img);
        m_angle = 0;
        m_scale = 0;  // 0 => fit on first show
        redraw();
        // build the folder list for prev/next
        QDir d = QFileInfo(path).dir();
        m_siblings = d.entryList(supportedGlobs(), QDir::Files, QDir::Name);
        m_dir = d.absolutePath();
        m_index = m_siblings.indexOf(QFileInfo(path).fileName());
        setWindowTitle(QFileInfo(path).fileName()
                       + QStringLiteral(" — Visor"));
    }

protected:
    void keyPressEvent(QKeyEvent *e) override
    {
        switch (e->key()) {
        case Qt::Key_Left:  step(-1); break;
        case Qt::Key_Right: step(1); break;
        case Qt::Key_Plus:  zoom(1.25); break;
        case Qt::Key_Minus: zoom(0.8); break;
        case Qt::Key_0:     fit(); break;
        default: QMainWindow::keyPressEvent(e);
        }
    }
    void resizeEvent(QResizeEvent *e) override
    {
        QMainWindow::resizeEvent(e);
        if (m_scale == 0) redraw();
    }

private:
    static QStringList supportedGlobs()
    {
        QStringList g;
        for (const QByteArray &f : QImageReader::supportedImageFormats())
            g << QStringLiteral("*.") + QString::fromLatin1(f);
        return g;
    }
    void open()
    {
        const QString p = QFileDialog::getOpenFileName(
            this, QStringLiteral("Abrir imagen"), QDir::homePath(),
            QStringLiteral("Imágenes (%1)")
                .arg(supportedGlobs().join(QLatin1Char(' '))));
        if (!p.isEmpty()) {
            loadImage(p);
            castalia::recent::add(p,
                                  QStringLiteral("Visor de imágenes"));
        }
    }
    void step(int d)
    {
        if (m_siblings.isEmpty()) return;
        m_index = (m_index + d + m_siblings.size()) % m_siblings.size();
        loadImage(QDir(m_dir).filePath(m_siblings.at(m_index)));
    }
    void zoom(double f)
    {
        if (m_pix.isNull()) return;
        if (m_scale == 0) m_scale = currentFitScale();
        m_scale *= f;
        redraw();
    }
    void rotate() { m_angle = (m_angle + 90) % 360; redraw(); }
    void fit() { m_scale = 0; redraw(); }

    double currentFitScale() const
    {
        if (m_pix.isNull()) return 1.0;
        const QSize avail = m_scroll->viewport()->size();
        QSize s = m_pix.size();
        if (m_angle % 180) s.transpose();
        return qMin(double(avail.width()) / s.width(),
                    double(avail.height()) / s.height());
    }
    void redraw()
    {
        if (m_pix.isNull()) {
            m_view->setText(QStringLiteral("Abre una imagen (Ctrl+O)"));
            m_info->clear();
            return;
        }
        QPixmap p = m_pix;
        if (m_angle)
            p = p.transformed(QTransform().rotate(m_angle),
                              Qt::SmoothTransformation);
        const double sc = m_scale == 0 ? currentFitScale() : m_scale;
        const QPixmap scaled = p.scaled(p.size() * sc, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation);
        m_view->setPixmap(scaled);
        m_info->setText(QStringLiteral("%1 × %2  ·  %3%  ·  %4")
                            .arg(m_pix.width()).arg(m_pix.height())
                            .arg(qRound(sc * 100))
                            .arg(QFileInfo(m_path).fileName()));
    }

    QScrollArea *m_scroll = nullptr;
    QLabel *m_view = nullptr;
    QLabel *m_info = nullptr;
    QPixmap m_pix;
    QString m_path, m_dir;
    QStringList m_siblings;
    int m_index = -1;
    int m_angle = 0;
    double m_scale = 0;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-visor"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addPositionalArgument(QStringLiteral("image"),
                              QStringLiteral("Image to open"));
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Open a built-in sample image")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    castalia::applyTheme(&app, repo, cli.value(QStringLiteral("theme")));

    Visor w;
    QString img;
    if (!cli.positionalArguments().isEmpty())
        img = cli.positionalArguments().first();
    else if (cli.isSet(QStringLiteral("demo"))
             || cli.isSet(QStringLiteral("screenshot")))
        img = repo + QStringLiteral(
            "/docs/evidence/phase2-desktop-live.png");  // show our own art
    if (!img.isEmpty())
        w.loadImage(img);
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
