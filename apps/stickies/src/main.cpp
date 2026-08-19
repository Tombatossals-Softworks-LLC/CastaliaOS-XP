// castalia-adhesivas — Notas adhesivas (Bible §9.3 "Accessories").
//
// A little corkboard of sticky notes: jot quick reminders on pastel cards,
// recolour them, and they persist to ~/.config/castalia/adhesivas.json so
// they're waiting for you next time. Pure Qt5 + libcastalia-ui theming, every
// card painted natively — no third-party assets (§3.9).
//
// Usage: castalia-adhesivas --theme classic [--repo P]
//        [--demo] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <vector>

#include "Theme.h"

namespace {

// The pastel note palette (background, header strip).
struct NoteColor {
    const char *name;
    QColor bg, strip;
};
const NoteColor kColors[] = {
    {"amarillo", QColor(0xFF, 0xF3, 0x9B), QColor(0xF2, 0xDF, 0x6A)},
    {"verde", QColor(0xCE, 0xF0, 0xB8), QColor(0xAF, 0xDC, 0x92)},
    {"rosa", QColor(0xFB, 0xCF, 0xE0), QColor(0xF0, 0xAC, 0xC8)},
    {"azul", QColor(0xC6, 0xE4, 0xF7), QColor(0x9E, 0xCC, 0xEE)},
    {"naranja", QColor(0xFF, 0xD8, 0xB0), QColor(0xF4, 0xBB, 0x83)},
};
constexpr int kNumColors = int(sizeof(kColors) / sizeof(kColors[0]));

} // namespace

class Board; // fwd

// A single sticky card: coloured, editable, recolourable, deletable.
class NoteCard : public QWidget {
    Q_OBJECT
public:
    NoteCard(const QString &text, int color, QWidget *parent)
        : QWidget(parent), m_color(((color % kNumColors) + kNumColors)
                                   % kNumColors)
    {
        setFixedSize(190, 168);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(8, 6, 8, 8);
        root->setSpacing(4);

        auto *head = new QHBoxLayout;
        head->setContentsMargins(0, 0, 0, 0);
        m_swatch = new QPushButton(this);
        m_swatch->setFixedSize(16, 16);
        m_swatch->setCursor(Qt::PointingHandCursor);
        m_swatch->setToolTip(tr("Cambiar color"));
        connect(m_swatch, &QPushButton::clicked, this, [this]() {
            m_color = (m_color + 1) % kNumColors;
            refreshSwatch();
            update();
            emit changed();
        });
        auto *del = new QPushButton(QStringLiteral("×"), this);
        del->setFixedSize(18, 18);
        del->setCursor(Qt::PointingHandCursor);
        del->setToolTip(tr("Eliminar nota"));
        del->setStyleSheet(QStringLiteral(
            "QPushButton{border:none;background:transparent;font-size:16px;"
            "font-weight:bold;color:#5a5030;}"
            "QPushButton:hover{color:#b03030;}"));
        connect(del, &QPushButton::clicked, this,
                [this]() { emit deleteRequested(this); });
        head->addWidget(m_swatch);
        head->addStretch(1);
        head->addWidget(del);
        root->addLayout(head);

        m_edit = new QTextEdit(this);
        m_edit->setPlainText(text);
        m_edit->setFrameShape(QFrame::NoFrame);
        m_edit->viewport()->setAutoFillBackground(false);
        m_edit->setStyleSheet(QStringLiteral(
            "QTextEdit{background:transparent;color:#33301c;font-size:13px;}"));
        connect(m_edit, &QTextEdit::textChanged, this,
                [this]() { emit changed(); });
        root->addWidget(m_edit, 1);
        refreshSwatch();
    }

    QString text() const { return m_edit->toPlainText(); }
    int color() const { return m_color; }

signals:
    void changed();
    void deleteRequested(NoteCard *card);

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const NoteColor &c = kColors[m_color];
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        // soft drop shadow
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 28));
        p.drawRoundedRect(r.translated(2, 3), 4, 4);
        // card body
        p.setBrush(c.bg);
        p.drawRoundedRect(r, 4, 4);
        // header strip
        QPainterPath strip;
        strip.addRoundedRect(QRectF(r.left(), r.top(), r.width(), 26), 4, 4);
        p.setBrush(c.strip);
        p.drawPath(strip);
        p.fillRect(QRectF(r.left(), r.top() + 12, r.width(), 14), c.strip);
        // folded corner
        QPainterPath fold;
        fold.moveTo(r.right() - 18, r.bottom());
        fold.lineTo(r.right(), r.bottom() - 18);
        fold.lineTo(r.right(), r.bottom());
        fold.closeSubpath();
        p.setBrush(c.strip.darker(108));
        p.drawPath(fold);
    }

private:
    void refreshSwatch()
    {
        m_swatch->setStyleSheet(QStringLiteral(
            "QPushButton{border:1px solid rgba(0,0,0,60);border-radius:8px;"
            "background:%1;}").arg(kColors[m_color].strip.darker(112).name()));
    }

    int m_color;
    QPushButton *m_swatch = nullptr;
    QTextEdit *m_edit = nullptr;
};

class Board : public QMainWindow {
    Q_OBJECT
public:
    explicit Board(bool persist) : m_persist(persist)
    {
        setWindowTitle(tr("Notas adhesivas — Castalia"));
        resize(660, 520);

        auto *tb = addToolBar(tr("Notas"));
        tb->setMovable(false);
        auto *add = tb->addAction(tr("Nueva nota"));
        connect(add, &QAction::triggered, this, [this]() {
            addNote(QString(), int(m_cards.size()) % kNumColors);
            relayout();
            save();
        });

        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_host = new QWidget;
        m_grid = new QGridLayout(m_host);
        m_grid->setContentsMargins(14, 14, 14, 14);
        m_grid->setSpacing(14);
        m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        scroll->setWidget(m_host);
        setCentralWidget(scroll);

        // debounce disk writes so typing doesn't hammer the file
        m_saveTimer = new QTimer(this);
        m_saveTimer->setSingleShot(true);
        m_saveTimer->setInterval(600);
        connect(m_saveTimer, &QTimer::timeout, this, &Board::writeNow);
    }

    void loadOrSeed()
    {
        if (m_persist && load())
            return;
        // first run (or --demo): a friendly starter set
        addNote(tr("¡Bienvenido a las notas adhesivas!\n\nEscribe aquí tus "
                   "recordatorios."), 0);
        addNote(tr("• Pan\n• Leche\n• Café"), 1);
        addNote(tr("Cambia el color con el círculo de arriba a la izquierda."),
                3);
        relayout();
    }

    void addSample()
    {
        addNote(tr("Reunión el jueves a las 10:00"), 2);
        addNote(tr("Llamar a Marta 📞"), 4);
        relayout();
    }

private:
    static constexpr int kCols = 3;

    void addNote(const QString &text, int color)
    {
        auto *card = new NoteCard(text, color, m_host);
        connect(card, &NoteCard::changed, this, &Board::scheduleSave);
        connect(card, &NoteCard::deleteRequested, this, &Board::removeNote);
        m_cards.push_back(card);
    }

    void removeNote(NoteCard *card)
    {
        for (auto it = m_cards.begin(); it != m_cards.end(); ++it)
            if (*it == card) {
                m_cards.erase(it);
                break;
            }
        card->deleteLater();
        relayout();
        save();
    }

    void relayout()
    {
        // detach everything, then re-place in a fixed-column grid
        while (m_grid->count() > 0) {
            QLayoutItem *item = m_grid->takeAt(0);
            delete item;
        }
        for (int i = 0; i < int(m_cards.size()); ++i)
            m_grid->addWidget(m_cards[i], i / kCols, i % kCols);
    }

    void scheduleSave()
    {
        if (m_persist)
            m_saveTimer->start();
    }

    void save()
    {
        if (m_persist)
            writeNow();
    }

    void writeNow()
    {
        if (!m_persist)
            return;
        QJsonArray arr;
        for (NoteCard *c : m_cards) {
            QJsonObject o;
            o[QStringLiteral("text")] = c->text();
            o[QStringLiteral("color")] = c->color();
            arr.append(o);
        }
        QDir().mkpath(configDir());
        QFile f(configPath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    }

    bool load()
    {
        QFile f(configPath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isArray())
            return false;
        const QJsonArray arr = doc.array();
        if (arr.isEmpty())
            return false;
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            addNote(o.value(QStringLiteral("text")).toString(),
                    o.value(QStringLiteral("color")).toInt());
        }
        relayout();
        return true;
    }

    static QString configDir()
    {
        return QStandardPaths::writableLocation(
                   QStandardPaths::GenericConfigLocation)
               + QStringLiteral("/castalia");
    }
    static QString configPath()
    {
        return configDir() + QStringLiteral("/adhesivas.json");
    }

    bool m_persist;
    QWidget *m_host = nullptr;
    QGridLayout *m_grid = nullptr;
    QTimer *m_saveTimer = nullptr;
    std::vector<NoteCard *> m_cards;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-adhesivas"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Show sample notes without touching saved "
                                  "data")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo"))).absolutePath();
    castalia::applyTheme(&app, repo, cli.value(QStringLiteral("theme")));

    const bool demo = cli.isSet(QStringLiteral("demo"));
    Board w(!demo); // demo mode never reads or writes the user's notes
    if (demo) {
        w.loadOrSeed();
        w.addSample();
    } else {
        w.loadOrSeed();
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
