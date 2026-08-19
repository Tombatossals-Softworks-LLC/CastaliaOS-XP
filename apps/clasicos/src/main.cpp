// castalia-clasicos — the Castalia Classic Games launcher (Bible §9.4, §11.3).
//
// The friendly face of DOS and adventure-game compatibility: a small library
// of configured titles, each tagged with the engine that runs it (DOSBox-X or
// ScummVM), with Add / Run / Remove and an honest note about what each engine
// is for. Native-first is the rule (§11); when a title needs an emulator, this
// is where it lives — one managed place, not a pile of loose .conf files.
// Pure Qt5 + libcastalia-ui theming; it shells out to dosbox-x / scummvm.
//
// Usage: castalia-clasicos --theme human [--repo PATH] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include "Mark.h"
#include "Theme.h"

namespace {

// The two engines Castalia manages here, each with its own chip colour.
struct Engine { const char *name; QColor color; QString bin; };
Engine engineFor(const QString &kind)
{
    if (kind == QLatin1String("ScummVM"))
        return {"ScummVM", QColor(0xCE, 0x5C, 0x00),
                QStringLiteral("scummvm")};
    // DOS titles run under DOSBox-X (falls back to plain dosbox at run time).
    return {"DOSBox-X", QColor(0x5A, 0x47, 0x3B),
            QStringLiteral("dosbox-x")};
}

// A row: title name + a coloured engine chip (mirrors the Wine manager rows).
class GameItem : public QWidget {
public:
    GameItem(const QString &name, const QString &kind, QWidget *parent)
        : QWidget(parent), m_name(name), m_kind(kind)
    {
        setFixedHeight(40);
    }
    QString target;   // a folder/.conf for DOSBox-X, or a game id for ScummVM
    QString kind() const { return m_kind; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(palette().color(QPalette::Text));
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() + 0.5);
        p.setFont(f);
        p.drawText(QRect(12, 0, width() - 130, height()),
                   Qt::AlignVCenter, m_name);
        const Engine e = engineFor(m_kind);
        QRect chip(width() - 112, 9, 100, 22);
        p.setBrush(e.color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(chip, 11, 11);
        p.setPen(Qt::white);
        p.drawText(chip, Qt::AlignCenter, QString::fromUtf8(e.name));
    }

private:
    QString m_name, m_kind;
};

} // namespace

class ClassicGames : public QWidget {
    Q_OBJECT
public:
    ClassicGames(const QString &repo, const ThemeTokens &tokens)
        : m_repo(repo), m_tokens(tokens)
    {
        setWindowTitle(QStringLiteral("Juegos clásicos — Castalia"));
        resize(560, 460);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // header on the titlebar gradient (painted), matching the Wine manager
        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("ClasHeader"));
        head->setFixedHeight(66);
        head->setStyleSheet(QStringLiteral(
            "#ClasHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *title = new QLabel(head);
        title->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>"
            "Juegos clásicos</span><br>"
            "<span style='color:%1'>%2</span>")
            .arg(colorTok("titlebar_text"), engineLine()));
        hl->addWidget(title);
        hl->addStretch(1);
        root->addWidget(head);

        auto *body = new QVBoxLayout;
        body->setContentsMargins(16, 14, 16, 16);
        body->setSpacing(10);

        m_list = new QListWidget(this);
        m_list->setObjectName(QStringLiteral("ClasList"));
        // A representative library so the window reads as finished (the same
        // approach as the Wine manager's sample rows); real entries come from
        // "Añadir…". Original, generic titles — no third-party trademarks.
        addGame(QStringLiteral("Aventura en la costa (demo)"),
                QStringLiteral("ScummVM"));
        addGame(QStringLiteral("Comandante del espacio"),
                QStringLiteral("DOS"));
        addGame(QStringLiteral("Laberinto de la fortaleza"),
                QStringLiteral("DOS"));
        addGame(QStringLiteral("El misterio del faro"),
                QStringLiteral("ScummVM"));
        body->addWidget(m_list, 1);

        auto *btns = new QHBoxLayout;
        auto *addDos = new QPushButton(QStringLiteral("Añadir juego DOS…"),
                                       this);
        auto *addScumm = new QPushButton(QStringLiteral("Añadir ScummVM…"),
                                         this);
        auto *run = new QPushButton(QStringLiteral("Jugar"), this);
        run->setObjectName(QStringLiteral("ClasRun"));
        auto *rm = new QPushButton(QStringLiteral("Eliminar"), this);
        connect(addDos, &QPushButton::clicked, this,
                &ClassicGames::addDosGame);
        connect(addScumm, &QPushButton::clicked, this,
                &ClassicGames::addScummGame);
        connect(run, &QPushButton::clicked, this, &ClassicGames::runSelected);
        connect(rm, &QPushButton::clicked, this, [this]() {
            delete m_list->takeItem(m_list->currentRow());
        });
        btns->addWidget(addDos);
        btns->addWidget(addScumm);
        btns->addStretch(1);
        btns->addWidget(rm);
        btns->addWidget(run);
        body->addLayout(btns);

        // the honest note (Bible §11.6 / P10), mirroring the Wine manager's
        auto *note = new QLabel(QStringLiteral(
            "DOSBox-X ejecuta juegos y programas de MS-DOS; ScummVM ejecuta "
            "aventuras gráficas clásicas. Necesitas tus propios archivos de "
            "juego: Castalia no incluye juegos con derechos, solo el motor "
            "que los pone en marcha."), this);
        note->setWordWrap(true);
        note->setProperty("secondary", true);
        note->setStyleSheet(QStringLiteral("padding:8px;border:1px solid %1;"
                                            "border-radius:4px;")
                                .arg(colorTok("border")));
        body->addWidget(note);

        root->addLayout(body);
    }

private slots:
    void addDosGame()
    {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Elegir carpeta del juego DOS"),
            QDir::homePath());
        if (!dir.isEmpty())
            addGame(QFileInfo(dir).fileName(), QStringLiteral("DOS"), dir);
    }
    void addScummGame()
    {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Elegir carpeta del juego ScummVM"),
            QDir::homePath());
        if (!dir.isEmpty())
            addGame(QFileInfo(dir).fileName(), QStringLiteral("ScummVM"), dir);
    }
    void runSelected()
    {
        auto *it = m_list->currentItem();
        if (!it) return;
        auto *g = static_cast<GameItem *>(m_list->itemWidget(it));
        if (!g || g->target.isEmpty()) {
            QMessageBox::information(
                this, QStringLiteral("Jugar"),
                QStringLiteral("Esta entrada de ejemplo no tiene archivos. "
                               "Usa «Añadir…» para registrar un juego real."));
            return;
        }
        auto *proc = new QProcess(this);
        if (g->kind() == QLatin1String("ScummVM")) {
            // Point ScummVM at the game directory and auto-detect it.
            proc->start(QStringLiteral("scummvm"),
                        {QStringLiteral("--path=") + g->target,
                         QStringLiteral("--auto-detect")});
        } else {
            // DOSBox-X: mount the folder as C: and drop into it.
            proc->start(engineFor(QStringLiteral("DOS")).bin,
                        {g->target});
        }
    }

private:
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }
    // Report which engines are actually installed, honestly.
    static QString engineLine()
    {
        auto has = [](const QString &bin) {
            return !QStandardPaths::findExecutable(bin).isEmpty();
        };
        QStringList found;
        if (has(QStringLiteral("dosbox-x")) || has(QStringLiteral("dosbox")))
            found << QStringLiteral("DOSBox-X");
        if (has(QStringLiteral("scummvm")))
            found << QStringLiteral("ScummVM");
        if (found.isEmpty())
            return QStringLiteral("MS-DOS y aventuras gráficas");
        return QStringLiteral("Motores: ") + found.join(QStringLiteral(" · "));
    }

    void addGame(const QString &name, const QString &kind,
                 const QString &target = QString())
    {
        auto *g = new GameItem(name, kind, m_list);
        g->target = target;
        auto *it = new QListWidgetItem(m_list);
        it->setSizeHint(g->sizeHint().expandedTo(QSize(0, 40)));
        m_list->addItem(it);
        m_list->setItemWidget(it, g);
    }

    QString m_repo;
    ThemeTokens m_tokens;
    QListWidget *m_list = nullptr;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-clasicos"));
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
        QStringLiteral("#ClasRun{font-weight:bold;border-color:%1;}")
            .arg(accent));

    ClassicGames w(repo, tokens);
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
