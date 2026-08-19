// castalia-pintura — the Castalia bitmap editor (Bible §9 "Accesorios").
//
// A real, self-contained raster paint program: pencil, line, rectangle,
// ellipse, flood fill and eraser, a classic color palette, adjustable brush
// width, multi-step undo, and open/save of ordinary image files. Pure Qt5 +
// libcastalia-ui theming — no Microsoft assets, no external engine.
//
// Usage:
//   castalia-pintura [IMAGE] --theme classic [--repo PATH] [--screenshot out]

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStack>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVector>
#include <QWidget>

#include "Mark.h"
#include "Theme.h"

namespace {

// The classic 28-swatch palette (two rows of fourteen). Original values —
// a warm, sea-and-sandstone-friendly spread, not anyone else's table.
const QColor kPalette[] = {
    QColor(0x1E, 0x1E, 0x1E), QColor(0x5A, 0x56, 0x4E),
    QColor(0xB3, 0x37, 0x2E), QColor(0xD8, 0x6A, 0x2C),
    QColor(0xE8, 0xC1, 0x3B), QColor(0x6E, 0x9E, 0x3A),
    QColor(0x2E, 0x8B, 0x57), QColor(0x2C, 0x66, 0x99),
    QColor(0x3E, 0x82, 0xB6), QColor(0x6A, 0x5A, 0xCD),
    QColor(0x8E, 0x4A, 0x9E), QColor(0xC0, 0x5A, 0x86),
    QColor(0x8B, 0x5A, 0x2B), QColor(0xFF, 0xFF, 0xFF),
    QColor(0x9A, 0x9A, 0x9A), QColor(0xC9, 0xC5, 0xBE),
    QColor(0xE8, 0x8A, 0x7E), QColor(0xF0, 0xB0, 0x6A),
    QColor(0xF3, 0xE0, 0x9A), QColor(0xB6, 0xD0, 0x86),
    QColor(0x86, 0xC7, 0xA1), QColor(0x8F, 0xB8, 0xDC),
    QColor(0xA9, 0xCC, 0xE8), QColor(0xB4, 0xA8, 0xE6),
    QColor(0xCB, 0xA6, 0xD6), QColor(0xE4, 0xAD, 0xC4),
    QColor(0xCF, 0xB0, 0x8A), QColor(0xEC, 0xE9, 0xE4),
};
const int kPaletteCount = int(sizeof(kPalette) / sizeof(kPalette[0]));

enum class Tool { Pencil, Line, Rect, Ellipse, Fill, Eraser };

// The drawing surface: owns the bitmap and all editing operations.
class Canvas : public QWidget {
    Q_OBJECT
public:
    explicit Canvas(QWidget *parent = nullptr) : QWidget(parent)
    {
        newImage(QSize(640, 440));
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
    }

    void newImage(QSize size)
    {
        m_image = QImage(size, QImage::Format_ARGB32);
        m_image.fill(Qt::white);
        m_undo.clear();
        setFixedSize(size);
        update();
        emit changed();
    }
    bool load(const QString &path)
    {
        QImage img(path);
        if (img.isNull())
            return false;
        m_image = img.convertToFormat(QImage::Format_ARGB32);
        m_undo.clear();
        setFixedSize(m_image.size());
        update();
        emit changed();
        return true;
    }
    bool save(const QString &path) const { return m_image.save(path); }

    void setTool(Tool t) { m_tool = t; }
    void setColor(const QColor &c) { m_color = c; }
    QColor color() const { return m_color; }
    void setPenWidth(int w) { m_width = w; }

    void undo()
    {
        if (m_undo.isEmpty())
            return;
        m_image = m_undo.pop();
        setFixedSize(m_image.size());
        update();
        emit changed();
    }
    bool canUndo() const { return !m_undo.isEmpty(); }
    QSize imageSize() const { return m_image.size(); }

    // A small demo composition so an empty-canvas screenshot still shows the
    // tools in action (mirrors how the image viewer shows its own art).
    void drawSample()
    {
        QPainter p(&m_image);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(m_image.rect(), Qt::white);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xA9, 0xCC, 0xE8));
        p.drawRect(40, 60, 220, 150);
        p.setBrush(QColor(0xE8, 0xC1, 0x3B));
        p.drawEllipse(QPoint(150, 135), 55, 55);
        p.setPen(QPen(QColor(0xB3, 0x37, 0x2E), 6, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(300, 200);
        path.cubicTo(360, 90, 460, 300, 560, 130);
        p.drawPath(path);
        p.setPen(QPen(QColor(0x2C, 0x66, 0x99), 3));
        p.drawRect(300, 250, 260, 140);
        p.setPen(QColor(0x1E, 0x1E, 0x1E));
        QFont f = p.font();
        f.setPointSize(22);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(300, 250, 260, 140), Qt::AlignCenter,
                   QStringLiteral("Pintura"));
        // the keep mark, bottom-left
        castalia::drawMark(&p, QRectF(20, m_image.height() - 52, 34, 34));
        update();
        emit changed();
    }

signals:
    void changed();
    void cursorMoved(QPoint pos);

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        // subtle checker so pure-white images still read as a canvas edge
        p.fillRect(rect(), QColor(0xF6, 0xF5, 0xF2));
        p.drawImage(0, 0, m_image);
        p.setPen(QColor(0xB8, 0xB4, 0xAD));
        p.drawRect(0, 0, m_image.width() - 1, m_image.height() - 1);
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        pushUndo();
        m_start = m_last = e->pos();
        m_drawing = true;
        if (m_tool == Tool::Fill) {
            floodFill(e->pos(), m_color);
            m_drawing = false;
            update();
            emit changed();
        } else if (m_tool == Tool::Pencil || m_tool == Tool::Eraser) {
            strokeTo(e->pos());
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        emit cursorMoved(e->pos());
        if (!m_drawing)
            return;
        switch (m_tool) {
        case Tool::Pencil:
        case Tool::Eraser:
            strokeTo(e->pos());
            break;
        case Tool::Line:
        case Tool::Rect:
        case Tool::Ellipse:
            m_image = m_backup;
            previewShape(e->pos());
            update();
            break;
        default:
            break;
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (!m_drawing)
            return;
        m_drawing = false;
        if (m_tool == Tool::Line || m_tool == Tool::Rect
            || m_tool == Tool::Ellipse) {
            m_image = m_backup;
            previewShape(e->pos());
        }
        update();
        emit changed();
    }

private:
    void pushUndo()
    {
        m_backup = m_image;                 // implicitly shared, cheap
        m_undo.push(m_image.copy());
        if (m_undo.size() > 16)
            m_undo.remove(0);
    }
    QPen pen(const QColor &c) const
    {
        return QPen(c, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    }
    void strokeTo(QPoint pos)
    {
        QPainter p(&m_image);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(pen(m_tool == Tool::Eraser ? QColor(Qt::white) : m_color));
        p.drawLine(m_last, pos);
        m_last = pos;
        update();
    }
    void previewShape(QPoint pos)
    {
        QPainter p(&m_image);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(pen(m_color));
        p.setBrush(Qt::NoBrush);
        if (m_tool == Tool::Line)
            p.drawLine(m_start, pos);
        else if (m_tool == Tool::Rect)
            p.drawRect(QRect(m_start, pos).normalized());
        else
            p.drawEllipse(QRect(m_start, pos).normalized());
    }
    void floodFill(QPoint seed, const QColor &fill)
    {
        if (!m_image.rect().contains(seed))
            return;
        const QRgb target = m_image.pixel(seed);
        const QRgb repl = fill.rgba();
        if (target == repl)
            return;
        const int w = m_image.width(), h = m_image.height();
        QStack<QPoint> st;
        st.push(seed);
        while (!st.isEmpty()) {
            const QPoint q = st.pop();
            if (q.x() < 0 || q.y() < 0 || q.x() >= w || q.y() >= h)
                continue;
            if (m_image.pixel(q) != target)
                continue;
            m_image.setPixel(q, repl);
            st.push(QPoint(q.x() + 1, q.y()));
            st.push(QPoint(q.x() - 1, q.y()));
            st.push(QPoint(q.x(), q.y() + 1));
            st.push(QPoint(q.x(), q.y() - 1));
        }
    }

    QImage m_image, m_backup;
    QStack<QImage> m_undo;
    Tool m_tool = Tool::Pencil;
    QColor m_color = QColor(0x2C, 0x66, 0x99);
    int m_width = 3;
    QPoint m_start, m_last;
    bool m_drawing = false;
};

} // namespace

class Pintura : public QMainWindow {
    Q_OBJECT
public:
    explicit Pintura()
    {
        setWindowTitle(QStringLiteral("Sin título — Pintura"));
        m_canvas = new Canvas(this);
        auto *scroll = new QScrollArea(this);
        scroll->setWidget(m_canvas);
        scroll->setAlignment(Qt::AlignCenter);
        scroll->setBackgroundRole(QPalette::Mid);
        setCentralWidget(scroll);

        buildFileBar();
        buildToolBar();
        buildPalette();

        m_pos = new QLabel(this);
        m_size = new QLabel(this);
        statusBar()->addWidget(m_pos, 1);
        statusBar()->addPermanentWidget(m_size);
        connect(m_canvas, &Canvas::cursorMoved, this, [this](QPoint p) {
            m_pos->setText(QStringLiteral("  %1, %2").arg(p.x()).arg(p.y()));
        });
        connect(m_canvas, &Canvas::changed, this, &Pintura::refreshStatus);
        refreshStatus();
        resize(880, 620);
    }

    Canvas *canvas() const { return m_canvas; }

private:
    void buildFileBar()
    {
        auto *bar = addToolBar(QStringLiteral("Archivo"));
        bar->setMovable(false);
        bar->addAction(QStringLiteral("Nuevo"), this, [this]() {
            m_canvas->newImage(QSize(640, 440));
            m_path.clear();
            setWindowTitle(QStringLiteral("Sin título — Pintura"));
        });
        bar->addAction(QStringLiteral("Abrir…"), this, &Pintura::open);
        bar->addAction(QStringLiteral("Guardar…"), this, &Pintura::save);
        bar->addSeparator();
        m_undoAct = bar->addAction(QStringLiteral("Deshacer"), this,
                                   [this]() { m_canvas->undo(); });
        m_undoAct->setShortcut(QKeySequence::Undo);
        bar->addSeparator();
        bar->addWidget(new QLabel(QStringLiteral(" Grosor "), bar));
        auto *width = new QComboBox(bar);
        for (int w : {1, 2, 3, 5, 8, 12, 18})
            width->addItem(QStringLiteral("%1 px").arg(w), w);
        width->setCurrentIndex(2);
        connect(width, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, width](int) {
                    m_canvas->setPenWidth(width->currentData().toInt());
                });
        bar->addWidget(width);
    }

    void buildToolBar()
    {
        auto *bar = new QToolBar(QStringLiteral("Herramientas"), this);
        bar->setMovable(false);
        bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
        addToolBar(Qt::LeftToolBarArea, bar);
        auto *group = new QActionGroup(this);
        const struct { const char *label; Tool tool; } tools[] = {
            {"Lápiz", Tool::Pencil},   {"Línea", Tool::Line},
            {"Rectángulo", Tool::Rect}, {"Elipse", Tool::Ellipse},
            {"Relleno", Tool::Fill},   {"Goma", Tool::Eraser},
        };
        for (const auto &t : tools) {
            auto *a = bar->addAction(QString::fromUtf8(t.label));
            a->setCheckable(true);
            group->addAction(a);
            const Tool tool = t.tool;
            connect(a, &QAction::triggered, this,
                    [this, tool]() { m_canvas->setTool(tool); });
            if (t.tool == Tool::Pencil)
                a->setChecked(true);
        }
    }

    void buildPalette()
    {
        auto *bar = new QToolBar(QStringLiteral("Colores"), this);
        bar->setMovable(false);
        addToolBar(Qt::BottomToolBarArea, bar);

        m_swatch = new QPushButton(bar);
        m_swatch->setObjectName(QStringLiteral("PaintCurrent"));
        m_swatch->setFixedSize(34, 34);
        m_swatch->setToolTip(QStringLiteral("Color actual — clic para elegir"));
        connect(m_swatch, &QPushButton::clicked, this, &Pintura::pickColor);
        bar->addWidget(m_swatch);
        bar->addSeparator();

        for (int i = 0; i < kPaletteCount; ++i) {
            const QColor c = kPalette[i];
            auto *b = new QPushButton(bar);
            b->setObjectName(QStringLiteral("PaintSwatch"));
            b->setFixedSize(18, 18);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(swatchQss(c));
            connect(b, &QPushButton::clicked, this,
                    [this, c]() { setColor(c); });
            bar->addWidget(b);
        }
        setColor(m_canvas->color());
    }

    static QString swatchQss(const QColor &c)
    {
        return QStringLiteral(
                   "QPushButton{background:%1;border:1px solid #6b6b6b;"
                   "border-radius:2px;}"
                   "QPushButton:hover{border:2px solid #1e1e1e;}")
            .arg(c.name());
    }
    void setColor(const QColor &c)
    {
        m_canvas->setColor(c);
        m_swatch->setStyleSheet(
            QStringLiteral("QPushButton{background:%1;border:2px solid "
                           "#3c3c3c;border-radius:3px;}")
                .arg(c.name()));
    }
    void pickColor()
    {
        const QColor c = QColorDialog::getColor(
            m_canvas->color(), this, QStringLiteral("Elegir color"));
        if (c.isValid())
            setColor(c);
    }
    void open()
    {
        const QString p = QFileDialog::getOpenFileName(
            this, QStringLiteral("Abrir imagen"), QDir::homePath(),
            QStringLiteral("Imágenes (*.png *.jpg *.jpeg *.bmp *.gif)"));
        if (p.isEmpty())
            return;
        if (m_canvas->load(p)) {
            m_path = p;
            setWindowTitle(QFileInfo(p).fileName()
                           + QStringLiteral(" — Pintura"));
        } else {
            QMessageBox::warning(this, QStringLiteral("Abrir"),
                                 QStringLiteral("No se pudo abrir la imagen."));
        }
    }
    void save()
    {
        QString p = QFileDialog::getSaveFileName(
            this, QStringLiteral("Guardar imagen"),
            m_path.isEmpty() ? QDir::homePath() + QStringLiteral("/dibujo.png")
                             : m_path,
            QStringLiteral("PNG (*.png);;JPEG (*.jpg);;BMP (*.bmp)"));
        if (p.isEmpty())
            return;
        if (!QFileInfo(p).fileName().contains(QLatin1Char('.')))
            p += QStringLiteral(".png");
        if (m_canvas->save(p)) {
            m_path = p;
            setWindowTitle(QFileInfo(p).fileName()
                           + QStringLiteral(" — Pintura"));
        } else {
            QMessageBox::warning(this, QStringLiteral("Guardar"),
                                 QStringLiteral("No se pudo guardar."));
        }
    }
    void refreshStatus()
    {
        if (m_undoAct)
            m_undoAct->setEnabled(m_canvas->canUndo());
        const QSize s = m_canvas->imageSize();
        m_size->setText(QStringLiteral("%1 × %2 px  ").arg(s.width())
                            .arg(s.height()));
    }

    Canvas *m_canvas = nullptr;
    QPushButton *m_swatch = nullptr;
    QAction *m_undoAct = nullptr;
    QLabel *m_pos = nullptr, *m_size = nullptr;
    QString m_path;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-pintura"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addPositionalArgument(QStringLiteral("image"),
                              QStringLiteral("Image to open"));
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Start with a sample drawing")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    castalia::applyTheme(&app, repo, cli.value(QStringLiteral("theme")),
                         QStringLiteral(
                             "#PaintCurrent{border-radius:3px;}"));

    Pintura w;
    if (!cli.positionalArguments().isEmpty())
        w.canvas()->load(cli.positionalArguments().first());
    else if (cli.isSet(QStringLiteral("screenshot"))
             || cli.isSet(QStringLiteral("demo")))
        w.canvas()->drawSample();
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot);
            app.quit();
        });
    return app.exec();
}

#include "main.moc"
