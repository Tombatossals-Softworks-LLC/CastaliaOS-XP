// castalia-predeterminados — "Programas predeterminados" (Default Programs) —
// Bible §9, §10 XP-parity.
//
// The XP-era "Set Default Programs", done to the freedesktop standard. For
// each everyday category — web browser, mail, text editor, image viewer, audio
// and video players, file manager — it reads the current default with
// `xdg-mime query default <type>` and offers every installed application that
// declares it handles that type (scanned from the standard applications dirs).
// Choosing one runs `xdg-mime default`, which writes the user's own
// ~/.config/mimeapps.list — no privilege, nothing system-wide touched. On a
// box without xdg-utils it says so instead of pretending.
//
// `--demo` shows a representative set (no xdg calls, nothing written) so the
// offscreen render gate and the live suite have something to display.
//
// Usage: castalia-predeterminados --theme human [--repo PATH] [--demo]
//                                 [--screenshot out.png]

#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include "Theme.h"

namespace {

struct Category {
    QString label, mime;
};

const QList<Category> &categories()
{
    static const QList<Category> c = {
        {QStringLiteral("Navegador web"), QStringLiteral("x-scheme-handler/http")},
        {QStringLiteral("Correo electrónico"),
         QStringLiteral("x-scheme-handler/mailto")},
        {QStringLiteral("Editor de texto"), QStringLiteral("text/plain")},
        {QStringLiteral("Visor de imágenes"), QStringLiteral("image/png")},
        {QStringLiteral("Reproductor de audio"), QStringLiteral("audio/mpeg")},
        {QStringLiteral("Reproductor de vídeo"), QStringLiteral("video/mp4")},
        {QStringLiteral("Gestor de archivos"),
         QStringLiteral("inode/directory")},
    };
    return c;
}

bool haveXdgMime()
{
    return !QStandardPaths::findExecutable(QStringLiteral("xdg-mime")).isEmpty();
}

QStringList appDirs()
{
    QStringList dirs = QStandardPaths::standardLocations(
        QStandardPaths::ApplicationsLocation);
    // Be exhaustive even if the env is minimal.
    for (const QString &d : {QStringLiteral("/usr/share/applications"),
                             QStringLiteral("/usr/local/share/applications")})
        if (!dirs.contains(d))
            dirs << d;
    return dirs;
}

// A handler: display name + .desktop id (the file name).
struct Handler {
    QString name, id;
};

} // namespace

class Predeterminados : public QWidget {
    Q_OBJECT
public:
    Predeterminados(const QString &repo, const ThemeTokens &tokens, bool demo)
        : m_repo(repo), m_tokens(tokens), m_demo(demo), m_xdg(haveXdgMime())
    {
        setWindowTitle(QStringLiteral("Programas predeterminados — Castalia"));
        resize(560, 400);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("DpHeader"));
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "#DpHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *icon = new QLabel(head);
        icon->setPixmap(castalia::themeIcon(m_repo, QStringLiteral("package"))
                            .pixmap(28, 28));
        hl->addWidget(icon);
        auto *title = new QLabel(head);
        const QString sub = m_demo ? QStringLiteral("vista de ejemplo")
            : (m_xdg ? QStringLiteral("xdg-mime")
                     : QStringLiteral("sin xdg-utils"));
        title->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>Programas "
            "predeterminados</span>&nbsp;&nbsp;<span style='color:%1'>%2</span>")
            .arg(colorTok("titlebar_text"), sub));
        hl->addWidget(title);
        hl->addStretch(1);
        root->addWidget(head);

        auto *body = new QVBoxLayout;
        body->setContentsMargins(22, 16, 22, 14);
        body->setSpacing(10);
        auto *form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(9);

        if (!m_demo)
            scanHandlers();
        for (const Category &c : categories()) {
            auto *combo = new QComboBox(this);
            combo->setMinimumWidth(280);
            const QList<Handler> hs = m_demo ? demoHandlers(c.mime)
                                             : m_handlers.value(c.mime);
            const QString cur = m_demo ? demoDefault(c.mime)
                                       : currentDefault(c.mime);
            int sel = -1;
            for (const Handler &h : hs) {
                combo->addItem(h.name, h.id);
                if (h.id == cur)
                    sel = combo->count() - 1;
            }
            if (hs.isEmpty()) {
                combo->addItem(cur.isEmpty() ? QStringLiteral("(ninguno)")
                                             : cur);
                combo->setEnabled(false);
            } else {
                if (sel < 0) {
                    combo->insertItem(0, cur.isEmpty()
                        ? QStringLiteral("(sin predeterminar)") : cur, QString());
                    sel = 0;
                }
                combo->setCurrentIndex(sel);
                combo->setEnabled(m_xdg && !m_demo);
                const QString mime = c.mime;
                connect(combo, QOverload<int>::of(&QComboBox::activated),
                        this, [this, combo, mime](int) {
                            applyDefault(mime,
                                         combo->currentData().toString());
                        });
            }
            form->addRow(c.label + QLatin1Char(':'), combo);
        }
        body->addLayout(form);
        body->addStretch(1);

        m_note = new QLabel(this);
        m_note->setWordWrap(true);
        m_note->setProperty("secondary", true);
        if (m_demo)
            m_note->setText(QStringLiteral(
                "Vista de ejemplo: no se han consultado ni modificado los "
                "programas predeterminados reales."));
        else if (!m_xdg)
            m_note->setText(QStringLiteral(
                "No se detecta xdg-utils (xdg-mime). Los programas "
                "predeterminados se podrán cambiar cuando esté disponible."));
        else
            m_note->setText(QStringLiteral(
                "Los cambios se guardan solo para tu usuario "
                "(~/.config/mimeapps.list)."));
        body->addWidget(m_note);
        root->addLayout(body, 1);
    }

private slots:
    void applyDefault(const QString &mime, const QString &desktopId)
    {
        if (m_demo || !m_xdg || desktopId.isEmpty())
            return;
        QProcess::execute(QStringLiteral("xdg-mime"),
                          {QStringLiteral("default"), desktopId, mime});
    }

private:
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }

    QString currentDefault(const QString &mime) const
    {
        if (!m_xdg)
            return QString();
        QProcess p;
        p.start(QStringLiteral("xdg-mime"),
                {QStringLiteral("query"), QStringLiteral("default"), mime});
        if (!p.waitForFinished(1500))
            return QString();
        return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    }

    // Scan the applications dirs once: build mime → handlers.
    void scanHandlers()
    {
        for (const QString &dir : appDirs()) {
            QDir d(dir);
            const auto files = d.entryList({QStringLiteral("*.desktop")},
                                           QDir::Files);
            for (const QString &fn : files) {
                QSettings s(d.filePath(fn), QSettings::IniFormat);
                s.beginGroup(QStringLiteral("Desktop Entry"));
                if (s.value(QStringLiteral("Type")).toString()
                        != QStringLiteral("Application")
                    || s.value(QStringLiteral("NoDisplay")).toBool()) {
                    s.endGroup();
                    continue;
                }
                const QString name = s.value(QStringLiteral("Name")).toString();
                const QString mimes =
                    s.value(QStringLiteral("MimeType")).toString();
                s.endGroup();
                if (name.isEmpty() || mimes.isEmpty())
                    continue;
                for (const QString &m : mimes.split(QLatin1Char(';'),
                                                    Qt::SkipEmptyParts)) {
                    auto &list = m_handlers[m];
                    bool dup = false;
                    for (const Handler &h : list)
                        if (h.id == fn) { dup = true; break; }
                    if (!dup)
                        list.append({name, fn});
                }
            }
        }
    }

    QList<Handler> demoHandlers(const QString &mime) const
    {
        if (mime == QStringLiteral("x-scheme-handler/http"))
            return {{QStringLiteral("Navegador Castalia"),
                     QStringLiteral("castalia-web.desktop")},
                    {QStringLiteral("Firefox"),
                     QStringLiteral("firefox.desktop")}};
        if (mime == QStringLiteral("text/plain"))
            return {{QStringLiteral("Notas"),
                     QStringLiteral("castalia-notas.desktop")},
                    {QStringLiteral("Escritor"),
                     QStringLiteral("castalia-escritor.desktop")}};
        if (mime == QStringLiteral("image/png"))
            return {{QStringLiteral("Visor de imágenes"),
                     QStringLiteral("castalia-visor.desktop")}};
        if (mime.startsWith(QStringLiteral("audio"))
            || mime.startsWith(QStringLiteral("video")))
            return {{QStringLiteral("Reproductor multimedia"),
                     QStringLiteral("castalia-multimedia.desktop")}};
        if (mime == QStringLiteral("inode/directory"))
            return {{QStringLiteral("Castalia Explorer"),
                     QStringLiteral("castalia-explorer.desktop")}};
        if (mime == QStringLiteral("x-scheme-handler/mailto"))
            return {{QStringLiteral("(ninguno)"), QString()}};
        return {};
    }
    QString demoDefault(const QString &mime) const
    {
        const auto hs = demoHandlers(mime);
        return hs.isEmpty() ? QString() : hs.first().id;
    }

    QString m_repo;
    ThemeTokens m_tokens;
    bool m_demo = false, m_xdg = false;
    QHash<QString, QList<Handler>> m_handlers;
    QLabel *m_note = nullptr;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-predeterminados"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Show a representative read-only view")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const ThemeTokens tokens = castalia::applyTheme(&app, repo, themeId);

    Predeterminados w(repo, tokens, cli.isSet(QStringLiteral("demo")));
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
