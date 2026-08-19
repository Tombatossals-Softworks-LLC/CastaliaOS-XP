// castalia-bienvenida — the Welcome & Help Center (Bible §9.4, §14.4).
//
// The warm first impression and the built-in, fully offline help: a topic
// sidebar and rich themed pages that explain the desktop, the apps, Windows
// compatibility, benchmarks and installing — each with quick-action buttons
// that launch the relevant tool. Pure Qt5 (QTextBrowser) + libcastalia-ui.
//
// Usage: castalia-bienvenida --theme classic [--repo P] [--topic N]
//                            [--screenshot out.png]

#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include "Mark.h"
#include "Theme.h"

namespace {

struct Action { QString label; QString bin; };
struct Topic {
    QString title;
    QString glyph;
    QString html;
    std::vector<Action> actions;
};

// The help content — concise, honest, first-party. Colors are filled from the
// active theme at build time so every page matches the current look.
std::vector<Topic> topics(const QString &accent, const QString &ink)
{
    auto p = [&](const QString &s) {
        return QStringLiteral("<p style='margin:0 0 10px'>%1</p>").arg(s);
    };
    auto h = [&](const QString &s) {
        return QStringLiteral("<h2 style='color:%1;margin:2px 0 8px;"
                              "font-size:19px'>%2</h2>").arg(accent, s);
    };
    auto li = [&](const QString &s) {
        return QStringLiteral("<li style='margin:0 0 6px'>%1</li>").arg(s);
    };
    Q_UNUSED(ink);
    std::vector<Topic> t;

    t.push_back({QCoreApplication::translate("Welcome", "Bienvenido"), QStringLiteral("🏰"),
        h(QCoreApplication::translate("Welcome", "Bienvenido a Castalia OS"))
        + p(QCoreApplication::translate("Welcome", "Castalia OS es un sistema operativo <b>original</b> "
            "de clase XP para PCs de los años 2000 — no un clon ni un tema. "
            "Recupera la comodidad de aquella época, reinventada: rápido, "
            "bello, reparable y honesto."))
        + p(QCoreApplication::translate("Welcome", "Todo aquí es nuestro: la concha, los iconos, los "
            "temas y los sonidos. No contiene código ni recursos de Microsoft."))
        + QStringLiteral("<ul>")
        + li(QCoreApplication::translate("Welcome", "🔒 <b>Sin telemetría y sin cuentas.</b> Tu equipo "
            "es tuyo."))
        + li(QCoreApplication::translate("Welcome", "🌐 <b>Funciona sin conexión.</b> Instalación y uso "
            "completos offline."))
        + li(QCoreApplication::translate("Welcome", "🪟 <b>Compatible con aplicaciones Windows®</b> "
            "seleccionadas mediante Wine."))
        + QStringLiteral("</ul>"),
        {{QCoreApplication::translate("Welcome", "Abrir el Centro de control"),
          QStringLiteral("castalia-control-center")}}});

    t.push_back({QCoreApplication::translate("Welcome", "El escritorio"), QStringLiteral("🖥"),
        h(QCoreApplication::translate("Welcome", "Tu escritorio"))
        + p(QCoreApplication::translate("Welcome", "La <b>barra de tareas</b> muestra tus ventanas "
            "abiertas de verdad; pulsa un botón para traer esa ventana al "
            "frente. El botón <b>Castalia</b> abre el menú de inicio con todas "
            "las aplicaciones y lugares."))
        + p(QCoreApplication::translate("Welcome", "Cambia el aspecto en un instante: hay <b>siete "
            "temas</b> (Human, Classic, Azul, Oliva, Plata, Medianoche y Alto "
            "contraste). Un solo interruptor cambia ventanas, panel y "
            "decoración a la vez."))
        + p(QCoreApplication::translate("Welcome", "Elige tu tema en el <b>Centro de control → "
            "Apariencia</b>.")),
        {{QCoreApplication::translate("Welcome", "Cambiar el tema"),
          QStringLiteral("castalia-control-center")},
         {QCoreApplication::translate("Welcome", "Abrir el explorador"),
          QStringLiteral("castalia-explorer")}}});

    t.push_back({QCoreApplication::translate("Welcome", "Aplicaciones"), QStringLiteral("🧩"),
        h(QCoreApplication::translate("Welcome", "Aplicaciones incluidas"))
        + QStringLiteral("<ul>")
        + li(QCoreApplication::translate("Welcome", "<b>Explorador</b> — navega por tus archivos."))
        + li(QCoreApplication::translate("Welcome", "<b>Pintura</b> — dibujo de mapa de bits."))
        + li(QCoreApplication::translate("Welcome", "<b>Notas</b> — editor de texto rápido."))
        + li(QCoreApplication::translate("Welcome", "<b>Calculadora</b> — estándar con teclado."))
        + li(QCoreApplication::translate("Welcome", "<b>Visor de imágenes</b> — ver, girar, ampliar."))
        + li(QCoreApplication::translate("Welcome", "<b>Diagnóstico</b> — información y rendimiento."))
        + QStringLiteral("</ul>")
        + p(QCoreApplication::translate("Welcome", "Todas comparten una misma biblioteca de interfaz y "
            "visten el tema activo al instante.")),
        {{QCoreApplication::translate("Welcome", "Abrir Pintura"), QStringLiteral("castalia-pintura")},
         {QCoreApplication::translate("Welcome", "Abrir Notas"), QStringLiteral("castalia-notas")}}});

    t.push_back({QCoreApplication::translate("Welcome", "Windows®"), QStringLiteral("🪟"),
        h(QCoreApplication::translate("Welcome", "Aplicaciones de Windows"))
        + p(QCoreApplication::translate("Welcome", "El <b>Gestor de aplicaciones Windows</b> ejecuta "
            "muchos programas de Windows mediante Wine, con un prefijo aislado "
            "por aplicación."))
        + p(QCoreApplication::translate("Welcome", "Somos honestos: Wine no lo ejecuta todo. Los "
            "controladores de Windows, el anticheat de núcleo y los juegos con "
            "DirectX moderno no funcionarán. Te decimos por adelantado si una "
            "aplicación es probable que funcione, con una escala clara "
            "(Platino, Oro, Plata, Bronce).")),
        {{QCoreApplication::translate("Welcome", "Abrir el gestor de Windows"),
          QStringLiteral("castalia-wine")}}});

    t.push_back({QCoreApplication::translate("Welcome", "Rendimiento"), QStringLiteral("📊"),
        h(QCoreApplication::translate("Welcome", "Información y rendimiento"))
        + p(QCoreApplication::translate("Welcome", "<b>Diagnóstico del sistema</b> te muestra el "
            "detalle de tu equipo (CPU, memoria, gráficos, discos y red) y un "
            "<b>banco de pruebas</b> que mide de verdad su rendimiento."))
        + p(QCoreApplication::translate("Welcome", "Cada cifra se toma en tu equipo, en tiempo real. "
            "Verás una puntuación por componente y una puntuación global "
            "Castalia.")),
        {{QCoreApplication::translate("Welcome", "Ejecutar el diagnóstico"),
          QStringLiteral("castalia-diagnostico")}}});

    t.push_back({QCoreApplication::translate("Welcome", "Instalar"), QStringLiteral("💽"),
        h(QCoreApplication::translate("Welcome", "Instalar en tu disco"))
        + p(QCoreApplication::translate("Welcome", "Estás usando Castalia en vivo. Cuando quieras "
            "instalarlo permanentemente, el <b>asistente de instalación</b> te "
            "guía paso a paso: disco, cuenta y resumen."))
        + p(QCoreApplication::translate("Welcome", "Es seguro: <b>nunca</b> borra un disco sin que "
            "escribas su nombre para confirmar, y la instalación se completa "
            "sin conexión.")),
        {{QCoreApplication::translate("Welcome", "Instalar Castalia OS…"),
          QStringLiteral("castalia-instalador")}}});

    t.push_back({QCoreApplication::translate("Welcome", "Ayuda"), QStringLiteral("❔"),
        h(QCoreApplication::translate("Welcome", "Ayuda y soporte"))
        + p(QCoreApplication::translate("Welcome", "Esta ayuda es <b>100% sin conexión</b>: siempre "
            "está disponible, sin Internet."))
        + QStringLiteral("<ul>")
        + li(QCoreApplication::translate("Welcome", "Abre el menú con el botón <b>Castalia</b> de la "
            "esquina."))
        + li(QCoreApplication::translate("Welcome", "Cada aplicación lleva su propia ayuda en el "
            "botón <b>?</b>."))
        + li(QCoreApplication::translate("Welcome", "¿Los gráficos fallan? Arranca en <b>modo "
            "seguro</b> desde el menú de arranque."))
        + QStringLiteral("</ul>")
        + p(QCoreApplication::translate("Welcome", "<span style='color:%1'>Castalia OS · Tombatossals "
            "Softworks. Windows® es marca de Microsoft; Castalia no está "
            "afiliado.</span>").arg(accent))});

    return t;
}

} // namespace

class Welcome : public QWidget {
public:
    Welcome(const QString &repo, const QString &theme, const QColor &accent)
        : m_repo(repo), m_theme(theme), m_accent(accent)
    {
        setWindowTitle(QCoreApplication::translate("Welcome",
                                                   "Bienvenido a Castalia OS"));
        resize(720, 500);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->addWidget(buildHeader());

        auto *mid = new QHBoxLayout;
        mid->setContentsMargins(0, 0, 0, 0);
        mid->setSpacing(0);

        m_list = new QListWidget(this);
        m_list->setObjectName(QStringLiteral("WelcomeNav"));
        m_list->setFixedWidth(168);
        m_list->setFrameShape(QFrame::NoFrame);
        m_list->setStyleSheet(QStringLiteral(
            "#WelcomeNav{background:palette(alternate-base);border-right:"
            "1px solid palette(mid);padding:6px 0;}"
            "#WelcomeNav::item{padding:9px 14px;}"
            "#WelcomeNav::item:selected{background:%1;color:white;}")
            .arg(m_accent.name()));
        mid->addWidget(m_list);

        m_stack = new QStackedWidget(this);
        mid->addWidget(m_stack, 1);
        root->addLayout(mid, 1);

        for (const Topic &t : topics(m_accent.name(),
                                     palette().color(QPalette::Text).name())) {
            m_list->addItem(QStringLiteral("  %1  %2").arg(t.glyph, t.title));
            m_stack->addWidget(buildPage(t));
        }
        connect(m_list, &QListWidget::currentRowChanged,
                m_stack, &QStackedWidget::setCurrentIndex);
        m_list->setCurrentRow(0);

        // footer
        auto *foot = new QWidget(this);
        foot->setObjectName(QStringLiteral("WelcomeFoot"));
        foot->setStyleSheet(QStringLiteral(
            "#WelcomeFoot{border-top:1px solid palette(mid);}"));
        auto *fl = new QHBoxLayout(foot);
        fl->setContentsMargins(16, 8, 16, 8);
        auto *chk = new QCheckBox(
            QCoreApplication::translate("Welcome", "Mostrar al iniciar"), foot);
        chk->setChecked(true);
        fl->addWidget(chk);
        fl->addStretch(1);
        auto *close = new QPushButton(QCoreApplication::translate("Welcome", "Cerrar"), foot);
        connect(close, &QPushButton::clicked, this, &QWidget::close);
        fl->addWidget(close);
        root->addWidget(foot);
    }

private:
    QWidget *buildHeader()
    {
        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("WelcomeHead"));
        head->setFixedHeight(64);
        head->setStyleSheet(QStringLiteral(
            "#WelcomeHead{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(m_accent.lighter(112).name(), m_accent.darker(118).name()));
        auto *l = new QHBoxLayout(head);
        l->setContentsMargins(18, 0, 18, 0);
        auto *mark = new QLabel(head);
        QPixmap pm(42, 42);
        pm.fill(Qt::transparent);
        QPainter pt(&pm);
        pt.setRenderHint(QPainter::Antialiasing, true);
        castalia::drawMark(&pt, QRectF(2, 2, 38, 38));
        pt.end();
        mark->setPixmap(pm);
        l->addWidget(mark);
        auto *t = new QLabel(QCoreApplication::translate("Welcome",
            "<span style='color:white;font-size:18px;font-weight:bold'>"
            "Bienvenido a Castalia OS</span><br>"
            "<span style='color:#EAF1F7'>Tombatossals Softworks</span>"),
            head);
        l->addWidget(t);
        l->addStretch(1);
        return head;
    }

    QWidget *buildPage(const Topic &t)
    {
        auto *page = new QWidget(m_stack);
        auto *lay = new QVBoxLayout(page);
        lay->setContentsMargins(20, 16, 20, 16);
        auto *body = new QTextBrowser(page);
        body->setOpenExternalLinks(false);
        body->setFrameShape(QFrame::NoFrame);
        body->setStyleSheet(QStringLiteral("background:transparent;"));
        body->setHtml(QStringLiteral(
            "<div style='font-size:14px;line-height:1.5'>%1</div>")
            .arg(t.html));
        lay->addWidget(body, 1);
        if (!t.actions.empty()) {
            auto *row = new QHBoxLayout;
            for (const Action &a : t.actions) {
                auto *b = new QPushButton(a.label, page);
                b->setObjectName(QStringLiteral("WelcomeAction"));
                b->setCursor(Qt::PointingHandCursor);
                b->setStyleSheet(QStringLiteral(
                    "#WelcomeAction{font-weight:bold;border-color:%1;"
                    "padding:5px 14px;}").arg(m_accent.name()));
                const QString bin = a.bin;
                connect(b, &QPushButton::clicked, this,
                        [this, bin]() { launch(bin); });
                row->addWidget(b);
            }
            row->addStretch(1);
            lay->addLayout(row);
        }
        return page;
    }

    void launch(const QString &bin)
    {
        QProcess::startDetached(bin, {QStringLiteral("--repo"), m_repo,
                                      QStringLiteral("--theme"), m_theme});
    }

    QString m_repo, m_theme;
    QColor m_accent;
    QListWidget *m_list = nullptr;
    QStackedWidget *m_stack = nullptr;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-bienvenida"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("topic"), QStringLiteral("Initial topic 0-6"),
                   QStringLiteral("n"), QStringLiteral("0")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const QString accentStr =
        ThemeTokens::load(castalia::themeConfPath(repo, themeId))
            .str(QStringLiteral("colors"), QStringLiteral("accent"));
    castalia::applyTheme(&app, repo, themeId,
                         QStringLiteral("#WelcomeAction{border-radius:4px;}"));
    const QColor accent(accentStr.isEmpty() ? QStringLiteral("#3E82B6")
                                            : accentStr);

    Welcome w(repo, themeId, accent);
    w.show();

    const QString out = cli.value(QStringLiteral("screenshot"));
    if (!out.isEmpty())
        QTimer::singleShot(180, &app, [&]() {
            w.grab().save(out);
            app.quit();
        });
    return app.exec();
}
