// castalia-wine — the Castalia Wine Prefix Manager (Bible §9.4, §11.2).
//
// The friendly face of Win32 compatibility: per-app prefixes, honest compat
// ratings, install/run/winetricks/remove, and a plain-language note about what
// Wine cannot do. Pure Qt5 + libcastalia-ui theming; it shells out to Wine.
//
// Usage: castalia-wine --theme classic [--repo PATH] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
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

// The honest Castalia compatibility scale (Bible §11.2).
struct Rating { const char *name; QColor color; };
Rating ratingFor(const QString &tier)
{
    if (tier == "Platino") return {"Platino", QColor(0x8E, 0x9A, 0xA6)};
    if (tier == "Oro")     return {"Oro", QColor(0xC9, 0xA2, 0x27)};
    if (tier == "Plata")   return {"Plata", QColor(0xA8, 0xAE, 0xB6)};
    if (tier == "Bronce")  return {"Bronce", QColor(0xB0, 0x7A, 0x43)};
    return {"No funciona", QColor(0xB3, 0x37, 0x2E)};
}

// A row: app name + a colored compat-rating chip.
class AppItem : public QWidget {
public:
    AppItem(const QString &name, const QString &tier, QWidget *parent)
        : QWidget(parent), m_name(name), m_tier(tier)
    {
        setFixedHeight(40);
    }
    QString exePath;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(palette().color(QPalette::Text));
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() + 0.5);
        p.setFont(f);
        p.drawText(QRect(12, 0, width() - 144, height()),
                   Qt::AlignVCenter, m_name);
        const Rating r = ratingFor(m_tier);
        QRect chip(width() - 122, 9, 110, 22);
        p.setBrush(r.color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(chip, 11, 11);
        p.setPen(Qt::white);
        p.drawText(chip, Qt::AlignCenter, QString::fromUtf8(r.name));
    }

private:
    QString m_name, m_tier;
};

} // namespace

class WineManager : public QWidget {
    Q_OBJECT
public:
    WineManager(const QString &repo, const ThemeTokens &tokens)
        : m_repo(repo), m_tokens(tokens)
    {
        setWindowTitle(QStringLiteral("Gestor de aplicaciones Windows — "
                                      "Castalia"));
        resize(560, 460);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // header on the titlebar gradient (painted)
        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("WineHeader"));
        head->setFixedHeight(66);
        head->setStyleSheet(QStringLiteral(
            "#WineHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *title = new QLabel(head);
        title->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>"
            "Aplicaciones de Windows®</span><br>"
            "<span style='color:%1'>%2</span>")
            .arg(colorTok("titlebar_text"), wineVersion()));
        hl->addWidget(title);
        hl->addStretch(1);
        root->addWidget(head);

        auto *body = new QVBoxLayout;
        body->setContentsMargins(16, 14, 16, 16);
        body->setSpacing(10);

        m_list = new QListWidget(this);
        m_list->setObjectName(QStringLiteral("WineList"));
        addApp(QStringLiteral("Hola desde Castalia (demo)"),
               QStringLiteral("Platino"),
               m_repo + QStringLiteral("/compat/win32-demo/hello.exe"));
        addApp(QStringLiteral("Editor clásico"), QStringLiteral("Oro"));
        addApp(QStringLiteral("Utilidad heredada"), QStringLiteral("Plata"));
        addApp(QStringLiteral("Juego con anticheat"),
               QStringLiteral("No funciona"));
        body->addWidget(m_list, 1);

        auto *btns = new QHBoxLayout;
        auto *add = new QPushButton(QStringLiteral("Añadir .exe…"), this);
        auto *run = new QPushButton(QStringLiteral("Ejecutar"), this);
        run->setObjectName(QStringLiteral("WineRun"));
        auto *tricks = new QPushButton(QStringLiteral("Winetricks"), this);
        auto *rm = new QPushButton(QStringLiteral("Eliminar"), this);
        connect(add, &QPushButton::clicked, this, &WineManager::addExe);
        connect(run, &QPushButton::clicked, this, &WineManager::runSelected);
        connect(rm, &QPushButton::clicked, this, [this]() {
            delete m_list->takeItem(m_list->currentRow());
        });
        btns->addWidget(add);
        btns->addStretch(1);
        btns->addWidget(tricks);
        btns->addWidget(rm);
        btns->addWidget(run);
        body->addLayout(btns);

        // the honest note (Bible §11.6 / P10)
        auto *note = new QLabel(QStringLiteral(
            "Wine ejecuta muchas aplicaciones de Windows, pero no todas: "
            "los controladores de Windows, el anticheat de núcleo y los "
            "juegos con DirectX moderno no funcionarán. Te decimos por "
            "adelantado si una aplicación es probable que funcione."), this);
        note->setWordWrap(true);
        note->setProperty("secondary", true);
        note->setStyleSheet(QStringLiteral("padding:8px;border:1px solid %1;"
                                            "border-radius:4px;")
                                .arg(colorTok("border")));
        body->addWidget(note);

        root->addLayout(body);
    }

private slots:
    void addExe()
    {
        const QString p = QFileDialog::getOpenFileName(
            this, QStringLiteral("Elegir programa de Windows"),
            QDir::homePath(), QStringLiteral("Programas (*.exe *.msi)"));
        if (!p.isEmpty())
            addApp(QFileInfo(p).fileName(), QStringLiteral("Plata"), p);
    }
    void runSelected()
    {
        auto *it = m_list->currentItem();
        if (!it) return;
        auto *w = static_cast<AppItem *>(m_list->itemWidget(it));
        if (!w || w->exePath.isEmpty()) {
            QMessageBox::information(
                this, QStringLiteral("Ejecutar"),
                QStringLiteral("Esta entrada de ejemplo no tiene .exe. "
                               "Usa «Añadir .exe…» para instalar uno real."));
            return;
        }
        // per-app prefix (§11.2): isolate each app
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QString prefix =
            QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
            + QStringLiteral("/.local/share/castalia/wine/")
            + QFileInfo(w->exePath).baseName();
        env.insert(QStringLiteral("WINEPREFIX"), prefix);
        env.insert(QStringLiteral("WINEDEBUG"), QStringLiteral("-all"));
        auto *proc = new QProcess(this);
        proc->setProcessEnvironment(env);
        proc->start(QStringLiteral("wine"), {w->exePath});
    }

private:
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }
    static QString wineVersion()
    {
        QProcess p;
        p.start(QStringLiteral("wine"), {QStringLiteral("--version")});
        if (p.waitForFinished(1500)) {
            const QString v = QString::fromUtf8(p.readAllStandardOutput())
                                  .trimmed();
            if (!v.isEmpty())
                return QStringLiteral("Compatibilidad Win32 · ") + v;
        }
        return QStringLiteral("Compatibilidad Win32 vía Wine");
    }
    void addApp(const QString &name, const QString &tier,
                const QString &exe = QString())
    {
        auto *w = new AppItem(name, tier, m_list);
        w->exePath = exe;
        auto *it = new QListWidgetItem(m_list);
        it->setSizeHint(w->sizeHint().expandedTo(QSize(0, 40)));
        m_list->addItem(it);
        m_list->setItemWidget(it, w);
    }

    QString m_repo;
    ThemeTokens m_tokens;
    QListWidget *m_list = nullptr;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-wine"));
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
        QStringLiteral("#WineRun{font-weight:bold;border-color:%1;}")
            .arg(accent));

    WineManager w(repo, tokens);
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
