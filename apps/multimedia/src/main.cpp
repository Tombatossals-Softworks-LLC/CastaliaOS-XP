// castalia-multimedia — the Castalia Media Player launcher (Bible §9, §11).
//
// Native-first (§11): rather than reinvent a media engine, this is a calm
// playlist that hands playback to the best backend actually installed on the
// machine — mpv, then VLC, then mplayer, falling back to the system handler.
// Add files or a whole folder, press Reproducir, and the real player opens
// with your list. Honest about which backend it found (or that none is
// installed). Pure Qt5 + libcastalia-ui theming; it shells out to the player.
//
// Usage: castalia-multimedia --theme human [--repo PATH] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include "Theme.h"

namespace {

// The media extensions we offer to add from a folder.
const QStringList kMediaGlobs = {
    QStringLiteral("*.mp3"), QStringLiteral("*.ogg"), QStringLiteral("*.flac"),
    QStringLiteral("*.wav"), QStringLiteral("*.m4a"), QStringLiteral("*.opus"),
    QStringLiteral("*.mp4"), QStringLiteral("*.mkv"), QStringLiteral("*.avi"),
    QStringLiteral("*.webm"), QStringLiteral("*.mov")};

// Pick the best installed player, in preference order. Empty name = none.
struct Backend { QString name; QString bin; };
Backend detectBackend()
{
    const struct { const char *bin; const char *name; } order[] = {
        {"mpv", "mpv"}, {"vlc", "VLC"}, {"mplayer", "MPlayer"}};
    for (const auto &b : order)
        if (!QStandardPaths::findExecutable(QLatin1String(b.bin)).isEmpty())
            return {QString::fromLatin1(b.name), QString::fromLatin1(b.bin)};
    return {QString(), QString()};
}

} // namespace

class MediaPlayer : public QWidget {
    Q_OBJECT
public:
    MediaPlayer(const QString &repo, const ThemeTokens &tokens)
        : m_repo(repo), m_tokens(tokens), m_backend(detectBackend())
    {
        setWindowTitle(QStringLiteral("Reproductor multimedia — Castalia"));
        resize(560, 440);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("MpHeader"));
        head->setFixedHeight(66);
        head->setStyleSheet(QStringLiteral(
            "#MpHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *title = new QLabel(head);
        const QString sub = m_backend.name.isEmpty()
            ? QStringLiteral("Ningún reproductor instalado")
            : QStringLiteral("Motor: ") + m_backend.name;
        title->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>"
            "Reproductor multimedia</span><br>"
            "<span style='color:%1'>%2</span>")
            .arg(colorTok("titlebar_text"), sub));
        hl->addWidget(title);
        hl->addStretch(1);
        root->addWidget(head);

        auto *body = new QVBoxLayout;
        body->setContentsMargins(16, 14, 16, 16);
        body->setSpacing(10);

        m_list = new QListWidget(this);
        m_list->setObjectName(QStringLiteral("MpList"));
        // A representative playlist so the window reads as finished (the same
        // approach as the other launchers); real items come from "Añadir…".
        // Original, generic titles — no third-party media.
        addSample(QStringLiteral("Bienvenida a Castalia.ogg"));
        addSample(QStringLiteral("Paseo por la bahía.mp4"));
        addSample(QStringLiteral("Nota de teclado.wav"));
        connect(m_list, &QListWidget::itemActivated, this,
                &MediaPlayer::playSelected);
        body->addWidget(m_list, 1);

        auto *btns = new QHBoxLayout;
        auto *addF = new QPushButton(QStringLiteral("Añadir archivos…"), this);
        auto *addD = new QPushButton(QStringLiteral("Añadir carpeta…"), this);
        auto *play = new QPushButton(QStringLiteral("Reproducir"), this);
        play->setObjectName(QStringLiteral("MpPlay"));
        auto *rm = new QPushButton(QStringLiteral("Quitar"), this);
        connect(addF, &QPushButton::clicked, this, &MediaPlayer::addFiles);
        connect(addD, &QPushButton::clicked, this, &MediaPlayer::addFolder);
        connect(play, &QPushButton::clicked, this, &MediaPlayer::playSelected);
        connect(rm, &QPushButton::clicked, this, [this]() {
            delete m_list->takeItem(m_list->currentRow());
        });
        btns->addWidget(addF);
        btns->addWidget(addD);
        btns->addStretch(1);
        btns->addWidget(rm);
        btns->addWidget(play);
        body->addLayout(btns);

        auto *note = new QLabel(this);
        note->setWordWrap(true);
        note->setProperty("secondary", true);
        note->setStyleSheet(QStringLiteral("padding:8px;border:1px solid %1;"
                                            "border-radius:4px;")
                                .arg(colorTok("border")));
        note->setText(m_backend.name.isEmpty()
            ? QStringLiteral("No se ha encontrado mpv, VLC ni MPlayer. "
                             "Instala uno desde el Centro de software para "
                             "reproducir audio y vídeo.")
            : QStringLiteral("La reproducción la realiza %1, el mejor "
                             "reproductor instalado en este equipo. Castalia "
                             "elige el motor; tú eliges la música.")
                  .arg(m_backend.name));
        body->addWidget(note);
        root->addLayout(body);
    }

private slots:
    void addFiles()
    {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("Añadir archivos multimedia"),
            QDir::homePath(),
            QStringLiteral("Multimedia (%1)").arg(kMediaGlobs.join(' ')));
        for (const QString &f : files)
            addPath(f);
    }
    void addFolder()
    {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Añadir una carpeta"), QDir::homePath());
        if (dir.isEmpty())
            return;
        QDirIterator it(dir, kMediaGlobs, QDir::Files);
        while (it.hasNext())
            addPath(it.next());
    }
    void playSelected()
    {
        if (m_backend.bin.isEmpty()) {
            QMessageBox::information(
                this, QStringLiteral("Reproducir"),
                QStringLiteral("No hay ningún reproductor instalado (mpv, VLC "
                               "o MPlayer)."));
            return;
        }
        // Gather real paths: the selection if any, else the whole playlist.
        QStringList paths;
        for (int i = 0; i < m_list->count(); ++i) {
            auto *it = m_list->item(i);
            const QString p = it->data(Qt::UserRole).toString();
            if (p.isEmpty())
                continue;
            if (!m_list->selectedItems().isEmpty()
                && !it->isSelected())
                continue;
            paths << p;
        }
        if (paths.isEmpty()) {
            QMessageBox::information(
                this, QStringLiteral("Reproducir"),
                QStringLiteral("Estas son entradas de ejemplo. Usa "
                               "«Añadir archivos…» para añadir tu música o "
                               "tus vídeos."));
            return;
        }
        QProcess::startDetached(m_backend.bin, paths);
    }

private:
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }
    void addSample(const QString &label)
    {
        auto *it = new QListWidgetItem(label, m_list);
        it->setData(Qt::UserRole, QString());   // no real path (sample)
    }
    void addPath(const QString &path)
    {
        auto *it = new QListWidgetItem(QFileInfo(path).fileName(), m_list);
        it->setData(Qt::UserRole, path);
        it->setToolTip(path);
    }

    QString m_repo;
    ThemeTokens m_tokens;
    Backend m_backend;
    QListWidget *m_list = nullptr;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-multimedia"));
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
        QStringLiteral("#MpPlay{font-weight:bold;border-color:%1;}")
            .arg(accent));

    MediaPlayer w(repo, tokens);
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
