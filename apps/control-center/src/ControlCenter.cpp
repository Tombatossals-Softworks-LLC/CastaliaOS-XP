#include "ControlCenter.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QProcess>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QImageReader>
#include <QSvgRenderer>
#include <QTextStream>
#include <QVBoxLayout>

#include "Locale.h"
#include "Mark.h"
#include "Theme.h"

namespace {

// A theme swatch: a miniature Castalia window (title-bar gradient + body +
// faint content) that previews the theme. A plain QWidget (not a QPushButton)
// so the global QSS button styling can't override its fixed preview size; the
// accent ring marks the active theme, name is a label beneath.
class ThemeSwatch : public QWidget {
    Q_OBJECT
public:
    ThemeSwatch(const QString &id, const QColor &top, const QColor &bottom,
                const QColor &accent, const QColor &surface,
                const QColor &surfaceAlt, QWidget *parent)
        : QWidget(parent), m_id(id), m_top(top), m_bottom(bottom),
          m_accent(accent), m_surface(surface), m_surfaceAlt(surfaceAlt)
    {
        setFixedSize(150, 92);
        setCursor(Qt::PointingHandCursor);
    }

    QString themeId() const { return m_id; }
    void setChecked(bool on) { if (m_checked != on) { m_checked = on; update(); } }
    bool isChecked() const { return m_checked; }

signals:
    void clicked(const QString &themeId);

protected:
    void mousePressEvent(QMouseEvent *) override { emit clicked(m_id); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect().adjusted(2, 2, -2, -2);

        QPainterPath frame;
        frame.addRoundedRect(r, 5, 5);
        p.save();
        p.setClipPath(frame);
        p.fillRect(r, m_surface);                       // window body
        QLinearGradient g(r.topLeft(), QPointF(r.left(), r.top() + 22));
        g.setColorAt(0, m_top);
        g.setColorAt(1, m_bottom);
        p.fillRect(QRectF(r.left(), r.top(), r.width(), 22), g);
        // three window-control dots
        p.setBrush(QColor(255, 255, 255, 210));
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 3; ++i)
            p.drawEllipse(QPointF(r.right() - 12 - i * 9, r.top() + 11), 2.4,
                          2.4);
        // faint body content: a sidebar + lines + an accent button
        p.setBrush(m_surfaceAlt);
        p.drawRect(QRectF(r.left(), r.top() + 22, 34, r.height() - 22));
        p.setBrush(m_accent);
        p.drawRoundedRect(QRectF(r.right() - 46, r.bottom() - 18, 36, 11), 2,
                          2);
        p.setPen(QPen(QColor(0, 0, 0, 45), 2));
        for (int i = 0; i < 3; ++i)
            p.drawLine(QPointF(r.left() + 44, r.top() + 34 + i * 11),
                       QPointF(r.right() - 12, r.top() + 34 + i * 11));
        p.restore();

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(m_checked ? m_accent : QColor(0, 0, 0, 55),
                      m_checked ? 2.5 : 1));
        p.drawRoundedRect(r, 5, 5);
    }

private:
    QString m_id;
    QColor m_top, m_bottom, m_accent, m_surface, m_surfaceAlt;
    bool m_checked = false;
};

QWidget *pageShell(const QString &title, const QString &subtitle)
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(22, 20, 22, 22);
    lay->setSpacing(4);
    auto *h = new QLabel(title);
    h->setStyleSheet(QStringLiteral("font-size:17px;font-weight:bold;"));
    lay->addWidget(h);
    auto *s = new QLabel(subtitle);
    s->setProperty("secondary", true);
    lay->addWidget(s);
    lay->addSpacing(10);
    return page;
}

} // namespace

ControlCenter::ControlCenter(const QString &repoRoot, const QString &themeId,
                             QWidget *parent)
    : QMainWindow(parent), m_repo(repoRoot), m_theme(themeId)
{
    m_tokens = ThemeTokens::load(castalia::themeConfPath(repoRoot, themeId));
    setWindowTitle(QStringLiteral("Centro de control — Castalia"));
    resize(720, 500);

    auto *central = new QWidget(this);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_categories = new QListWidget(central);
    m_categories->setObjectName(QStringLiteral("CCategories"));
    m_categories->setFixedWidth(190);
    m_categories->setIconSize(QSize(24, 24));
    const struct { const char *icon; const char *label; } cats[] = {
        {"settings", "Apariencia"}, {"computer", "Pantalla"},
        {"image-viewer", "Salvapantallas"},
        {"disk", "Sonido"},         {"network", "Red"},
        {"home", "Energía"},        {"archive", "Recuperación"},
        {"charmap", "Idioma"},
        {"help", "Acerca de Castalia"},
    };
    for (const auto &c : cats) {
        auto *it = new QListWidgetItem(
            QIcon(QDir(m_repo).filePath(
                QStringLiteral("themes/icons/48/%1.svg").arg(
                    QLatin1String(c.icon)))),
            QString::fromUtf8(c.label), m_categories);
        Q_UNUSED(it);
    }
    root->addWidget(m_categories);

    m_pages = new QStackedWidget(central);
    m_pages->addWidget(buildAppearance());
    m_pages->addWidget(buildDisplay());
    m_pages->addWidget(buildScreensaver());
    m_pages->addWidget(buildSound());
    m_pages->addWidget(buildNetwork());
    m_pages->addWidget(buildPower());
    m_pages->addWidget(buildRecovery());
    m_pages->addWidget(buildLanguage());
    m_pages->addWidget(buildAbout());
    root->addWidget(m_pages, 1);

    connect(m_categories, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);
    m_categories->setCurrentRow(0);

    setCentralWidget(central);
}

QWidget *ControlCenter::buildAppearance()
{
    auto *page = pageShell(QStringLiteral("Apariencia"),
                           QStringLiteral("Elige un tema. La vista previa es "
                                          "inmediata; la elección se guarda "
                                          "para todo el escritorio."));
    auto *lay = static_cast<QVBoxLayout *>(page->layout());

    auto *grid = new QGridLayout;
    grid->setSpacing(14);

    const QStringList ids = castalia::availableThemes(m_repo);
    auto *swatches = new QList<ThemeSwatch *>();  // owned by page lifetime
    int col = 0, rowN = 0;
    for (const QString &id : ids) {
        const ThemeTokens t =
            ThemeTokens::load(castalia::themeConfPath(m_repo, id));
        auto *cell = new QWidget(page);
        auto *cl = new QVBoxLayout(cell);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(6);
        auto *sw = new ThemeSwatch(
            id, t.color(QStringLiteral("titlebar_top")),
            t.color(QStringLiteral("titlebar_bottom")),
            t.color(QStringLiteral("accent")),
            t.color(QStringLiteral("surface")),
            t.color(QStringLiteral("surface_alt")), cell);
        sw->setChecked(id == m_theme);
        swatches->append(sw);
        connect(sw, &ThemeSwatch::clicked, this,
                [this, swatches](const QString &id) {
                    for (ThemeSwatch *s : *swatches)
                        s->setChecked(s->themeId() == id);
                    applyTheme(id);
                });
        auto *nm = new QLabel(
            t.themeName().replace(QStringLiteral("Castalia "), QString()),
            cell);
        nm->setAlignment(Qt::AlignHCenter);
        cl->addWidget(sw, 0, Qt::AlignHCenter);
        cl->addWidget(nm);
        grid->addWidget(cell, rowN, col);
        if (++col == 3) { col = 0; ++rowN; }
    }
    grid->setColumnStretch(3, 1);
    lay->addLayout(grid);
    m_appearanceStatus = new QLabel(page);
    m_appearanceStatus->setProperty("secondary", true);
    lay->addSpacing(6);
    lay->addWidget(m_appearanceStatus);
    lay->addStretch(1);
    return page;
}

QWidget *ControlCenter::buildDisplay()
{
    auto *page = pageShell(QStringLiteral("Pantalla"),
                           QStringLiteral("Resolución, animaciones y "
                                          "compositor."));
    auto *lay = static_cast<QVBoxLayout *>(page->layout());
    auto *form = new QFormLayout;
    auto *res = new QComboBox;
    res->addItems({QStringLiteral("800 × 600"), QStringLiteral("1024 × 768"),
                   QStringLiteral("1280 × 1024"),
                   QStringLiteral("1440 × 900")});
    res->setCurrentIndex(1);
    form->addRow(QStringLiteral("Resolución"), res);
    auto *reduce = new QCheckBox(QStringLiteral("Reducir animaciones"));
    reduce->setChecked(true);
    form->addRow(QString(), reduce);
    auto *comp = new QCheckBox(QStringLiteral("Compositor (efectos)"));
    form->addRow(QString(), comp);
    lay->addLayout(form);

    // ---- desktop wallpaper picker (XP "Display Properties > Desktop") ------
    lay->addSpacing(10);
    auto *wallHdr = new QLabel(QStringLiteral("Fondo de escritorio"), page);
    wallHdr->setStyleSheet(QStringLiteral("font-weight:bold;"));
    lay->addWidget(wallHdr);
    auto *wallHint = new QLabel(
        QStringLiteral("Elige un fondo. El escritorio lo aplica al instante."),
        page);
    wallHint->setProperty("secondary", true);
    lay->addWidget(wallHint);

    auto *wallGrid = new QGridLayout;
    wallGrid->setSpacing(10);
    struct Wall { QString label; QString value; };
    QList<Wall> walls;
    walls.append({QStringLiteral("Según el tema"), QString()});   // follow theme
    const QDir wdir(m_repo + QStringLiteral("/branding/wallpapers"));
    // Vector *and* photographic wallpapers — the shipped default is a JPEG.
    // Labels are derived from the file name, which reads fine for the themed
    // set ("Azure bay", "Olive dusk"). Where it does not, a proper name wins:
    // the picker is a user-facing list, not a directory listing.
    const QHash<QString, QString> named = {
        {QStringLiteral("valle-de-castalia"),
         QStringLiteral("Valle de Castalia")},
        {QStringLiteral("pradera-de-castalia"),
         QStringLiteral("Pradera de Castalia")},
        {QStringLiteral("castalia-neon"), QStringLiteral("Castalia neón")},
        {QStringLiteral("nebulosa-de-castalia"),
         QStringLiteral("Nebulosa de Castalia")},
        {QStringLiteral("mar-de-nubes"), QStringLiteral("Mar de nubes")},
        {QStringLiteral("castalia-minimal"),
         QStringLiteral("Castalia minimalista")},
    };
    for (const QFileInfo &fi :
         wdir.entryInfoList({QStringLiteral("*.svg"), QStringLiteral("*.jpg"),
                             QStringLiteral("*.jpeg"), QStringLiteral("*.png")},
                            QDir::Files, QDir::Name)) {
        QString name = named.value(fi.completeBaseName());
        if (name.isEmpty()) {
            name = fi.completeBaseName();
            name.replace(QLatin1Char('-'), QLatin1Char(' '));
            name[0] = name[0].toUpper();
        }
        walls.append({name,
                      QStringLiteral("branding/wallpapers/") + fi.fileName()});
    }

    auto *wallGroup = new QButtonGroup(page);
    int wc = 0, wr = 0;
    for (const Wall &w : walls) {
        auto *cell = new QWidget(page);
        auto *cl = new QVBoxLayout(cell);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(4);
        auto *btn = new QPushButton(cell);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedSize(132, 84);
        btn->setIconSize(QSize(120, 72));
        // Render a thumbnail from the SVG (or a token-coloured card for the
        // "follow the theme" option).
        QPixmap pm(120, 72);
        pm.fill(Qt::transparent);
        QPainter pt(&pm);
        pt.setRenderHint(QPainter::Antialiasing, true);
        if (w.value.isEmpty()) {
            pt.fillRect(pm.rect(), m_tokens.color(QStringLiteral("surface_alt")));
            pt.setPen(m_tokens.color(QStringLiteral("text_secondary")));
            pt.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("Tema"));
        } else if (w.value.endsWith(QLatin1String(".svg"),
                                    Qt::CaseInsensitive)) {
            QSvgRenderer r(m_repo + QLatin1Char('/') + w.value);
            if (r.isValid())
                r.render(&pt, QRectF(0, 0, 120, 72));
        } else {
            // A photograph: read it already scaled down. The default is
            // 2560x1664 and there are several thumbnails on this page —
            // decoding each one in full would cost more than the whole
            // Control Center is allowed (§16).
            QImageReader reader(m_repo + QLatin1Char('/') + w.value);
            reader.setAutoTransform(true);
            const QSize src = reader.size();
            if (src.isValid() && !src.isEmpty())
                reader.setScaledSize(
                    src.scaled(QSize(120, 72), Qt::KeepAspectRatioByExpanding));
            const QImage thumb = reader.read();
            if (!thumb.isNull())
                pt.drawImage(QPointF((120 - thumb.width()) / 2.0,
                                     (72 - thumb.height()) / 2.0), thumb);
        }
        pt.end();
        btn->setIcon(QIcon(pm));
        wallGroup->addButton(btn);
        const QString value = w.value;
        connect(btn, &QPushButton::clicked, this, [this, value]() {
            const bool ok = persistWallpaper(value);
            if (m_wallpaperStatus)
                m_wallpaperStatus->setText(
                    ok ? QStringLiteral("✓ Fondo aplicado al escritorio.")
                       : QStringLiteral("No se pudo guardar el fondo."));
        });
        cl->addWidget(btn, 0, Qt::AlignHCenter);
        auto *nm = new QLabel(w.label, cell);
        nm->setProperty("secondary", true);
        nm->setAlignment(Qt::AlignHCenter);
        cl->addWidget(nm);
        wallGrid->addWidget(cell, wr, wc);
        if (++wc == 4) { wc = 0; ++wr; }
    }
    wallGrid->setColumnStretch(4, 1);
    lay->addLayout(wallGrid);
    m_wallpaperStatus = new QLabel(page);
    m_wallpaperStatus->setProperty("secondary", true);
    lay->addWidget(m_wallpaperStatus);

    lay->addStretch(1);
    return page;
}

QWidget *ControlCenter::buildScreensaver()
{
    auto *page = pageShell(QStringLiteral("Salvapantallas"),
                           QStringLiteral("Elige una animación y pruébala. Se "
                                          "activa tras un tiempo de inactividad "
                                          "y se cierra al mover el ratón."));
    auto *lay = static_cast<QVBoxLayout *>(page->layout());
    auto *form = new QFormLayout;

    auto *mode = new QComboBox;
    mode->addItem(QStringLiteral("Ondas de Azure"), QStringLiteral("ondas"));
    mode->addItem(QStringLiteral("Mystify"), QStringLiteral("mystify"));
    mode->addItem(QStringLiteral("Campo estelar"),
                  QStringLiteral("estrellas"));
    mode->addItem(QStringLiteral("Aurora de Castalia"),
                  QStringLiteral("aurora"));
    form->addRow(QStringLiteral("Estilo"), mode);

    auto *idle = new QComboBox;
    idle->addItems({QStringLiteral("5 minutos"), QStringLiteral("10 minutos"),
                    QStringLiteral("15 minutos"), QStringLiteral("30 minutos"),
                    QStringLiteral("Nunca")});
    idle->setCurrentIndex(1);
    form->addRow(QStringLiteral("Activar tras"), idle);
    lay->addLayout(form);

    auto *test = new QPushButton(QStringLiteral("Probar ahora"));
    test->setObjectName(QStringLiteral("SaverTest"));
    connect(test, &QPushButton::clicked, this, [this, mode]() {
        QProcess::startDetached(
            QStringLiteral("castalia-salvapantallas"),
            {QStringLiteral("--mode"), mode->currentData().toString(),
             QStringLiteral("--repo"), m_repo,
             QStringLiteral("--theme"), m_theme});
    });
    auto *row = new QHBoxLayout;
    row->addWidget(test);
    row->addStretch(1);
    lay->addLayout(row);
    lay->addStretch(1);
    return page;
}

QWidget *ControlCenter::buildSound()
{
    auto *page = pageShell(QStringLiteral("Sonido"),
                           QStringLiteral("Volumen y dispositivos."));
    auto *lay = static_cast<QVBoxLayout *>(page->layout());
    auto *form = new QFormLayout;
    auto *vol = new QSlider(Qt::Horizontal);
    vol->setValue(72);
    form->addRow(QStringLiteral("Volumen maestro"), vol);
    auto *dev = new QComboBox;
    dev->addItems({QStringLiteral("Intel HDA — Altavoces"),
                   QStringLiteral("Intel HDA — Auriculares")});
    form->addRow(QStringLiteral("Dispositivo"), dev);
    auto *mute = new QCheckBox(QStringLiteral("Silenciar"));
    form->addRow(QString(), mute);
    lay->addLayout(form);
    lay->addStretch(1);
    return page;
}

QWidget *ControlCenter::buildNetwork()
{
    auto *page = pageShell(QStringLiteral("Red"),
                           QStringLiteral("Conexiones cableadas e "
                                          "inalámbricas."));
    auto *lay = static_cast<QVBoxLayout *>(page->layout());
    auto *box = new QGroupBox(QStringLiteral("Ethernet (eth0)"));
    auto *bl = new QFormLayout(box);
    bl->addRow(QStringLiteral("Estado"),
               new QLabel(QStringLiteral("Conectado · 100 Mbps")));
    bl->addRow(QStringLiteral("Dirección IP"),
               new QLabel(QStringLiteral("192.168.1.42")));
    auto *dhcp = new QCheckBox(QStringLiteral("Automática (DHCP)"));
    dhcp->setChecked(true);
    bl->addRow(QString(), dhcp);
    lay->addWidget(box);
    lay->addStretch(1);
    return page;
}

QWidget *ControlCenter::buildPower()
{
    auto *page = pageShell(QStringLiteral("Energía"),
                           QStringLiteral("Perfil de rendimiento y "
                                          "suspensión."));
    auto *lay = static_cast<QVBoxLayout *>(page->layout());
    for (const auto &p : {QStringLiteral("Rendimiento"),
                          QStringLiteral("Equilibrado"),
                          QStringLiteral("Ahorro de energía")}) {
        auto *r = new QRadioButton(p);
        if (p == QStringLiteral("Equilibrado"))
            r->setChecked(true);
        lay->addWidget(r);
    }
    lay->addStretch(1);
    return page;
}

QWidget *ControlCenter::buildRecovery()
{
    auto *page = pageShell(
        QStringLiteral("Recuperación"),
        QStringLiteral("Puntos de restauración: si una actualización o un "
                       "cambio sale mal, vuelve a un estado anterior del "
                       "sistema. Tus archivos personales no se tocan."));
    auto *lay = static_cast<QVBoxLayout *>(page->layout());

    auto *list = new QListWidget(page);
    list->setObjectName(QStringLiteral("RestoreList"));
    lay->addWidget(list, 1);
    auto *status = new QLabel(page);
    status->setProperty("secondary", true);
    lay->addWidget(status);

    auto refresh = [list, status]() {
        list->clear();
        QProcess p;
        p.start(QStringLiteral("castalia-restore"), {QStringLiteral("list")});
        QStringList lines;
        if (p.waitForFinished(3000))
            lines = QString::fromUtf8(p.readAllStandardOutput())
                        .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const bool empty = lines.isEmpty()
            || lines.first().contains(QStringLiteral("sin puntos"));
        if (empty) {  // demo / offscreen: show a believable pair
            list->addItem(QStringLiteral(
                "20260709-1000  manual        2026-07-09 10:00  estado "
                "inicial"));
            list->addItem(QStringLiteral(
                "20260709-1200  pre-update    2026-07-09 12:00  antes de "
                "actualizar"));
            status->setText(QStringLiteral("Ejemplo — aún no hay puntos "
                                           "reales en este equipo."));
        } else {
            for (const QString &l : lines)
                list->addItem(l);
            status->setText(QStringLiteral("%1 punto(s) de restauración")
                                .arg(lines.size()));
        }
    };
    refresh();

    auto *row = new QHBoxLayout;
    auto *create = new QPushButton(
        QStringLiteral("Crear punto de restauración"), page);
    create->setObjectName(QStringLiteral("RestoreCreate"));
    // Creating a point is non-destructive; it runs in the background.
    connect(create, &QPushButton::clicked, this, [status, refresh]() {
        status->setText(QStringLiteral("Creando un punto de restauración… "
                                       "(puede tardar según el disco)"));
        QProcess::startDetached(
            QStringLiteral("castalia-restore"),
            {QStringLiteral("create"), QStringLiteral("--label"),
             QStringLiteral("Manual desde el Centro de control")});
    });
    row->addWidget(create);
    auto *reload = new QPushButton(QStringLiteral("Actualizar lista"), page);
    connect(reload, &QPushButton::clicked, this, [refresh]() { refresh(); });
    row->addWidget(reload);
    row->addStretch(1);
    lay->addLayout(row);

    auto *note = new QLabel(QStringLiteral(
        "Para <b>restaurar</b> un punto de forma segura, usa la entrada "
        "«Recuperación» del menú de arranque o el comando "
        "<tt>castalia-restore restore</tt>. Restaurar crea primero un punto "
        "de vuelta, por si acaso."), page);
    note->setWordWrap(true);
    note->setProperty("secondary", true);
    lay->addWidget(note);
    return page;
}

// The interface language (§7.13). Deliberately honest about two things a
// language picker usually hides: the choice only reaches an application when
// that application starts — Qt cannot retranslate widgets that already
// exist — and Spanish is the source language, so nothing else is ever more
// complete than it is.
QWidget *ControlCenter::buildLanguage()
{
    auto *page = pageShell(QStringLiteral("Idioma"),
                           QStringLiteral("El idioma de la interfaz de "
                                          "Castalia."));
    auto *lay = static_cast<QVBoxLayout *>(page->layout());

    auto *form = new QFormLayout;
    auto *combo = new QComboBox;
    for (const QString &code : castalia::locale::available())
        combo->addItem(castalia::locale::displayName(code), code);
    // "Follow the system" is offered, but it is a choice you make — it is not
    // the default, because a desktop that changes language when an
    // environment variable changes is a desktop nobody can screenshot twice.
    combo->addItem(QStringLiteral("Seguir el sistema"),
                   QStringLiteral("auto"));
    const QString current = castalia::locale::configured();
    const int found = combo->findData(current.isEmpty()
                                          ? QStringLiteral("es")
                                          : current.toLower());
    combo->setCurrentIndex(found >= 0 ? found : 0);
    form->addRow(QStringLiteral("Idioma"), combo);
    lay->addLayout(form);

    m_languageStatus = new QLabel(QStringLiteral(
        "Castalia está escrito en español; los demás idiomas son "
        "traducciones."));
    m_languageStatus->setProperty("secondary", true);
    m_languageStatus->setWordWrap(true);
    lay->addWidget(m_languageStatus);

    auto *row = new QHBoxLayout;
    auto *apply = new QPushButton(QStringLiteral("Aplicar"));
    connect(apply, &QPushButton::clicked, this, [this, combo]() {
        const QString code = combo->currentData().toString();
        if (!persistLanguage(code)) {
            m_languageStatus->setText(QStringLiteral(
                "No se pudo guardar la preferencia de idioma."));
            return;
        }
        m_languageStatus->setText(QStringLiteral(
            "Idioma guardado: %1. Las aplicaciones que abras a partir de "
            "ahora lo usarán; para cambiar el escritorio entero, cierra la "
            "sesión y vuelve a entrar.")
            .arg(combo->currentText()));
    });
    row->addWidget(apply);
    row->addStretch(1);
    lay->addLayout(row);
    lay->addStretch(1);
    return page;
}

QWidget *ControlCenter::buildAbout()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(24, 24, 24, 24);
    lay->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    auto *markLabel = new QLabel;
    QPixmap pm(96, 96);
    pm.fill(Qt::transparent);
    { QPainter p(&pm); castalia::drawMark(&p, QRectF(0, 0, 96, 96)); }
    markLabel->setPixmap(pm);
    markLabel->setAlignment(Qt::AlignHCenter);
    lay->addWidget(markLabel);

    auto *name = new QLabel(QStringLiteral("Castalia OS"));
    name->setAlignment(Qt::AlignHCenter);
    name->setStyleSheet(QStringLiteral("font-size:22px;font-weight:bold;"));
    lay->addWidget(name);

    auto *ver = new QLabel(QStringLiteral("Classic · 1.0 «Peñíscola»"));
    ver->setAlignment(Qt::AlignHCenter);
    ver->setProperty("secondary", true);
    lay->addWidget(ver);
    lay->addSpacing(10);

    auto *pub = new QLabel(QStringLiteral("Tombatossals Softworks"));
    pub->setAlignment(Qt::AlignHCenter);
    lay->addWidget(pub);
    lay->addSpacing(6);

    auto *legal = new QLabel(QStringLiteral(
        "Sistema operativo independiente. Windows® es una marca registrada "
        "de Microsoft Corporation; Castalia OS no está afiliado, ni "
        "patrocinado, ni respaldado por Microsoft, y no comparte ninguno de "
        "sus recursos."));
    legal->setWordWrap(true);
    legal->setAlignment(Qt::AlignHCenter);
    legal->setProperty("secondary", true);
    legal->setMaximumWidth(440);
    lay->addWidget(legal);
    lay->addStretch(1);
    return page;
}

void ControlCenter::applyTheme(const QString &themeId)
{
    m_theme = themeId;
    m_tokens = castalia::applyTheme(qApp, m_repo, themeId);
    const bool saved = persistTheme(themeId);
    emit themeChanged(themeId);
    update();
    for (QWidget *w : findChildren<QWidget *>())
        w->update();
    if (m_appearanceStatus) {
        m_appearanceStatus->setText(
            saved ? QStringLiteral("✓ Tema guardado. Las ventanas nuevas y la "
                                   "próxima sesión lo usarán.")
                  : QStringLiteral("No se pudo guardar la preferencia de "
                                   "tema."));
    }
}

// Persist the choice where the session looks for it (§6.6): the same
// `id = "..."` line castalia-session parses from ~/.config/castalia/theme.conf.
bool ControlCenter::persistTheme(const QString &themeId)
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/castalia");
    if (!QDir().mkpath(dir))
        return false;
    QFile f(dir + QStringLiteral("/theme.conf"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream(&f)
        << "# Castalia active theme — written by the Control Center.\n"
        << "# castalia-session reads the id below to theme the whole desktop.\n"
        << "[meta]\n"
        << "id = \"" << themeId << "\"\n";
    return true;
}

// Persist the wallpaper choice where the desktop watches for it: an empty
// value means "follow the active theme's wallpaper".
bool ControlCenter::persistWallpaper(const QString &value)
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/castalia");
    if (!QDir().mkpath(dir))
        return false;
    QFile f(dir + QStringLiteral("/desktop.conf"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream(&f)
        << "# Castalia desktop settings — written by the Control Center.\n"
        << "# The desktop watches this file and reloads the wallpaper live.\n"
        << "# An empty wallpaper value follows the active theme.\n"
        << "wallpaper = \"" << value << "\"\n";
    return true;
}

// Persist the interface language where castalia::locale looks for it (§7.13),
// in the same flat shape as theme.conf so one parser reads both.
bool ControlCenter::persistLanguage(const QString &code)
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/castalia");
    if (!QDir().mkpath(dir))
        return false;
    QFile f(dir + QStringLiteral("/locale.conf"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream(&f)
        << "# Castalia interface language — written by the Control Center.\n"
        << "# \"es\" is the source language; \"auto\" follows the system.\n"
        << "[locale]\n"
        << "language = \"" << code << "\"\n";
    return true;
}

#include "ControlCenter.moc"
