// castalia-teclado — the Castalia On-Screen Keyboard (Bible §9, accessibility
// §8.6). A pointer-operable keyboard for users who can't use a physical one
// (motor impairments, kiosk/touch machines). It never takes focus, so the
// keys it sends land in whatever window you were using; it synthesises real
// key events through xdotool (X11 XTEST). Pure Qt5 + libcastalia-ui theming.
//
// This is the accessibility roadmap made concrete beyond the High Contrast
// theme (§8.6): a first, honest keyboard-input aid.
//
// Usage: castalia-teclado --theme human [--repo PATH] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

#include "Theme.h"

namespace {

// One physical key: the label shown, and what to send. `special` names an
// xdotool keysym (e.g. "BackSpace"); otherwise the label's character is typed.
struct Key {
    const char *lower;    // label / char when unshifted
    const char *upper;    // label / char when shifted (letters); "" = same
    const char *special;  // xdotool keysym for non-text keys; "" = text key
    int span;             // column span (1 unless a wide key)
};

// A compact Spanish (es-ES) layout. Digits and letters cover the everyday
// case; Shift switches letter case. Wide keys span extra columns.
const QVector<QVector<Key>> kLayout = {
    {{"1","","",1},{"2","","",1},{"3","","",1},{"4","","",1},{"5","","",1},
     {"6","","",1},{"7","","",1},{"8","","",1},{"9","","",1},{"0","","",1},
     {"⌫","","BackSpace",2}},
    {{"q","Q","",1},{"w","W","",1},{"e","E","",1},{"r","R","",1},{"t","T","",1},
     {"y","Y","",1},{"u","U","",1},{"i","I","",1},{"o","O","",1},{"p","P","",1},
     {"↵","","Return",2}},
    {{"a","A","",1},{"s","S","",1},{"d","D","",1},{"f","F","",1},{"g","G","",1},
     {"h","H","",1},{"j","J","",1},{"k","K","",1},{"l","L","",1},
     {"ñ","Ñ","",1},{"@","","",1}},
    {{"⇧","","Shift",2},{"z","Z","",1},{"x","X","",1},{"c","C","",1},
     {"v","V","",1},{"b","B","",1},{"n","N","",1},{"m","M","",1},
     {",","","",1},{".","","",1}},
    {{"Espacio","","space",8},{"Tab","","Tab",2}},
};

} // namespace

class OnScreenKeyboard : public QWidget {
    Q_OBJECT
public:
    OnScreenKeyboard(const QString &repo, const ThemeTokens &tokens)
        : m_repo(repo), m_tokens(tokens)
    {
        setWindowTitle(QStringLiteral("Teclado en pantalla — Castalia"));
        // Never steal focus: keys we send must land in the user's real window,
        // not this keyboard. (A stays-on-top hint is deliberately omitted so
        // the window remains a normal, taskbar-listed window.)
        setAttribute(Qt::WA_ShowWithoutActivating, true);

        m_haveXdotool =
            !QStandardPaths::findExecutable(QStringLiteral("xdotool")).isEmpty();

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("KbHeader"));
        head->setFixedHeight(44);
        head->setStyleSheet(QStringLiteral(
            "#KbHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(14, 0, 14, 0);
        auto *title = new QLabel(head);
        title->setText(QStringLiteral(
            "<span style='color:%1;font-weight:bold'>Teclado en pantalla</span>")
            .arg(colorTok("titlebar_text")));
        hl->addWidget(title);
        hl->addStretch(1);
        root->addWidget(head);

        auto *grid = new QGridLayout;
        grid->setContentsMargins(8, 8, 8, 8);
        grid->setSpacing(4);
        for (int r = 0; r < kLayout.size(); ++r) {
            int col = 0;
            for (const Key &k : kLayout[r]) {
                auto *b = new QPushButton(QString::fromUtf8(k.lower), this);
                b->setFocusPolicy(Qt::NoFocus);       // do not take focus
                b->setMinimumSize(38, 34);
                b->setCursor(Qt::PointingHandCursor);
                if (QLatin1String(k.special) == QLatin1String("Shift")) {
                    b->setCheckable(true);
                    b->setObjectName(QStringLiteral("KbMod"));
                    connect(b, &QPushButton::toggled, this,
                            &OnScreenKeyboard::setShift);
                } else {
                    const Key key = k;
                    connect(b, &QPushButton::clicked, this,
                            [this, key]() { sendKey(key); });
                }
                if (QString::fromUtf8(k.lower).size() > 1
                    && QLatin1String(k.special).size() == 0)
                    b->setObjectName(QStringLiteral("KbWide"));
                grid->addWidget(b, r, col, 1, k.span);
                if (k.upper[0] != '\0')
                    m_letterKeys.append(b);
                col += k.span;
            }
        }
        auto *body = new QVBoxLayout;
        body->setContentsMargins(0, 0, 0, 0);
        body->addLayout(grid);
        m_status = new QLabel(this);
        m_status->setProperty("secondary", true);
        m_status->setContentsMargins(12, 0, 12, 8);
        if (!m_haveXdotool)
            m_status->setText(QStringLiteral(
                "Sugerencia: instala «xdotool» para enviar las teclas a otras "
                "ventanas."));
        body->addWidget(m_status);
        root->addLayout(body);
    }

private slots:
    void setShift(bool on)
    {
        m_shift = on;
        // Relabel the letter keys to reflect case.
        int i = 0;
        for (const QVector<Key> &row : kLayout)
            for (const Key &k : row)
                if (k.upper[0] != '\0' && i < m_letterKeys.size())
                    m_letterKeys[i++]->setText(QString::fromUtf8(
                        on ? k.upper : k.lower));
    }

    void sendKey(const Key &k)
    {
        if (QLatin1String(k.special).size() > 0) {
            runXdotool({QStringLiteral("key"),
                        QString::fromLatin1(k.special)});
            return;
        }
        const QString ch =
            QString::fromUtf8(m_shift && k.upper[0] ? k.upper : k.lower);
        runXdotool({QStringLiteral("type"), QStringLiteral("--"), ch});
        // Shift is one-shot (tap-shift for a single letter): untoggle it,
        // which relabels the keys back to lower case via setShift().
        if (m_shift)
            for (QPushButton *m :
                 findChildren<QPushButton *>(QStringLiteral("KbMod")))
                m->setChecked(false);
    }

private:
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }
    void runXdotool(const QStringList &args)
    {
        if (!m_haveXdotool) {
            m_status->setText(QStringLiteral(
                "«xdotool» no está instalado: no se pueden enviar teclas."));
            return;
        }
        QProcess::startDetached(QStringLiteral("xdotool"), args);
    }

    QString m_repo;
    ThemeTokens m_tokens;
    QVector<QPushButton *> m_letterKeys;
    QLabel *m_status = nullptr;
    bool m_shift = false;
    bool m_haveXdotool = false;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-teclado"));
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
        QStringLiteral("#KbMod:checked{border-color:%1;font-weight:bold;}")
            .arg(accent));

    OnScreenKeyboard w(repo, tokens);
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
