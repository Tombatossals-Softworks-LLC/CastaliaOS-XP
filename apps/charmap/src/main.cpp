// castalia-caracteres — Mapa de caracteres (Bible §9.3 "Accessories").
//
// The classic Character Map accessory: browse the glyphs of any installed
// font, block by block, pick the ones you want and copy them to the
// clipboard. The glyph grid is drawn natively; the rest is plain Qt5 +
// libcastalia-ui theming. No third-party assets (§3.9).
//
// Usage: castalia-caracteres --theme classic [--repo P] [--screenshot out.png]

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "Theme.h"

namespace {

// A curated set of Unicode blocks — the ranges people actually reach for.
struct Block {
    const char *name;
    uint start, end;
};
const Block kBlocks[] = {
    {"Latín básico", 0x0020, 0x007E},
    {"Latín-1 (acentos, ¿¡, ©)", 0x00A1, 0x00FF},
    {"Latín extendido A", 0x0100, 0x017F},
    {"Griego", 0x0391, 0x03C9},
    {"Cirílico", 0x0410, 0x044F},
    {"Puntuación general", 0x2013, 0x2043},
    {"Símbolos de moneda", 0x20A0, 0x20BF},
    {"Símbolos tipo letra (™, №)", 0x2100, 0x214F},
    {"Flechas", 0x2190, 0x21FF},
    {"Operadores matemáticos", 0x2200, 0x22FF},
    {"Dibujo de cajas", 0x2500, 0x257F},
    {"Bloques", 0x2580, 0x259F},
    {"Formas geométricas", 0x25A0, 0x25FF},
    {"Símbolos varios (☀ ☂ ★)", 0x2600, 0x26FF},
    {"Dingbats (✂ ✈ ✝)", 0x2700, 0x27BF},
};

} // namespace

// A scrollable grid of glyphs for one block, painted natively.
class CharGrid : public QWidget {
    Q_OBJECT
public:
    explicit CharGrid(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMouseTracking(true);
        setBlock(kBlocks[1].start, kBlocks[1].end);
    }

    void setGlyphFont(const QFont &f)
    {
        m_font = f;
        m_font.setPixelSize(int(kCell * 0.6));
        update();
    }

    void setBlock(uint start, uint end)
    {
        m_start = start;
        m_end = end < start ? start : end;
        m_sel = m_start;
        const int count = int(m_end - m_start + 1);
        m_rows = (count + kCols - 1) / kCols;
        setFixedSize(kCols * kCell + 1, m_rows * kCell + 1);
        emit selected(m_sel);
        update();
    }

    uint selection() const { return m_sel; }

signals:
    void selected(uint code);
    void activated(uint code); // double-click → append & copy

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        uint code;
        if (cellAt(e->pos(), &code)) {
            m_sel = code;
            emit selected(code);
            update();
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        uint code;
        if (cellAt(e->pos(), &code))
            emit activated(code);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        uint code;
        const int prev = m_hover;
        m_hover = cellAt(e->pos(), &code) ? int(code) : -1;
        if (m_hover != prev)
            update();
    }
    void leaveEvent(QEvent *) override
    {
        if (m_hover != -1) {
            m_hover = -1;
            update();
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), palette().base());
        p.setFont(m_font);
        const QColor grid = palette().color(QPalette::Mid);
        const QColor selCol = palette().color(QPalette::Highlight);
        for (uint code = m_start; code <= m_end; ++code) {
            const int i = int(code - m_start);
            const int col = i % kCols, row = i / kCols;
            const QRect cell(col * kCell, row * kCell, kCell, kCell);
            if (int(code) == m_hover)
                p.fillRect(cell, palette().color(QPalette::AlternateBase));
            if (code == m_sel) {
                p.fillRect(cell, selCol);
                p.setPen(palette().color(QPalette::HighlightedText));
            } else {
                p.setPen(palette().color(QPalette::Text));
            }
            p.drawText(cell, Qt::AlignCenter,
                       QString::fromUcs4(reinterpret_cast<const char32_t *>(
                                             &code), 1));
        }
        // light grid lines
        p.setPen(QPen(grid, 1));
        for (int c = 0; c <= kCols; ++c)
            p.drawLine(c * kCell, 0, c * kCell, m_rows * kCell);
        for (int r = 0; r <= m_rows; ++r)
            p.drawLine(0, r * kCell, kCols * kCell, r * kCell);
    }

private:
    bool cellAt(const QPoint &pos, uint *code) const
    {
        const int col = pos.x() / kCell, row = pos.y() / kCell;
        if (col < 0 || col >= kCols || row < 0 || row >= m_rows)
            return false;
        const uint c = m_start + uint(row * kCols + col);
        if (c > m_end)
            return false;
        *code = c;
        return true;
    }

    static constexpr int kCols = 16;
    static constexpr int kCell = 32;
    QFont m_font;
    uint m_start = 0x20, m_end = 0x7E, m_sel = 0x20;
    int m_rows = 1, m_hover = -1;
};

class CharMap : public QWidget {
    Q_OBJECT
public:
    explicit CharMap(const QColor &accent) : m_accent(accent)
    {
        setWindowTitle(QStringLiteral("Mapa de caracteres — Castalia"));
        resize(560, 560);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->addWidget(buildHeader());

        auto *body = new QVBoxLayout;
        body->setContentsMargins(14, 12, 14, 14);
        body->setSpacing(10);

        // Font + block pickers.
        auto *pickers = new QHBoxLayout;
        m_fontBox = new QComboBox(this);
        m_fontBox->addItems(QFontDatabase().families());
        const int dj = m_fontBox->findText(QStringLiteral("DejaVu Sans"));
        m_fontBox->setCurrentIndex(dj >= 0 ? dj : 0);
        connect(m_fontBox, &QComboBox::currentTextChanged, this,
                &CharMap::applyFont);
        m_blockBox = new QComboBox(this);
        for (const Block &b : kBlocks)
            m_blockBox->addItem(QString::fromUtf8(b.name));
        m_blockBox->setCurrentIndex(1);
        connect(m_blockBox,
                QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int i) {
                    m_grid->setBlock(kBlocks[i].start, kBlocks[i].end);
                });
        auto *fl = new QLabel(QStringLiteral("Fuente:"), this);
        fl->setProperty("secondary", true);
        auto *bl = new QLabel(QStringLiteral("Bloque:"), this);
        bl->setProperty("secondary", true);
        pickers->addWidget(fl);
        pickers->addWidget(m_fontBox, 2);
        pickers->addWidget(bl);
        pickers->addWidget(m_blockBox, 2);
        body->addLayout(pickers);

        // The grid, inside a scroll area.
        m_grid = new CharGrid(this);
        connect(m_grid, &CharGrid::selected, this, &CharMap::showGlyph);
        connect(m_grid, &CharGrid::activated, this, &CharMap::appendCode);
        auto *scroll = new QScrollArea(this);
        scroll->setWidget(m_grid);
        scroll->setWidgetResizable(false);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        body->addWidget(scroll, 1);

        // Preview + code point.
        auto *prev = new QHBoxLayout;
        m_preview = new QLabel(this);
        m_preview->setFixedSize(72, 72);
        m_preview->setAlignment(Qt::AlignCenter);
        m_preview->setObjectName(QStringLiteral("GlyphPreview"));
        m_preview->setStyleSheet(QStringLiteral(
            "#GlyphPreview{background:palette(base);border:1px solid %1;"
            "border-radius:4px;font-size:40px;}")
            .arg(m_accent.name()));
        m_codeLabel = new QLabel(this);
        m_codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        prev->addWidget(m_preview);
        prev->addWidget(m_codeLabel, 1);
        body->addLayout(prev);

        // Characters to copy + actions.
        auto *actions = new QHBoxLayout;
        m_toCopy = new QLineEdit(this);
        m_toCopy->setPlaceholderText(
            QStringLiteral("Caracteres para copiar…"));
        auto *pick = new QPushButton(QStringLiteral("Seleccionar"), this);
        connect(pick, &QPushButton::clicked, this,
                [this]() { appendCode(m_grid->selection()); });
        auto *copy = new QPushButton(QStringLiteral("Copiar"), this);
        copy->setObjectName(QStringLiteral("PrimaryBtn"));
        connect(copy, &QPushButton::clicked, this, &CharMap::copyOut);
        actions->addWidget(m_toCopy, 1);
        actions->addWidget(pick);
        actions->addWidget(copy);
        body->addLayout(actions);

        root->addLayout(body, 1);
        applyFont(m_fontBox->currentText());
        showGlyph(m_grid->selection());
    }

    CharGrid *grid() { return m_grid; }

private:
    QWidget *buildHeader()
    {
        auto *head = new QWidget(this);
        head->setFixedHeight(50);
        head->setStyleSheet(QStringLiteral(
            "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %1,"
            "stop:1 %2);")
            .arg(m_accent.lighter(112).name(), m_accent.darker(118).name()));
        auto *l = new QHBoxLayout(head);
        l->setContentsMargins(16, 0, 16, 0);
        auto *t = new QLabel(QStringLiteral(
            "<span style='color:white;font-size:15px;font-weight:bold'>"
            "Mapa de caracteres</span>"), head);
        l->addWidget(t);
        l->addStretch(1);
        return head;
    }

    void applyFont(const QString &family)
    {
        QFont f(family);
        m_grid->setGlyphFont(f);
        QFont pf(family);
        pf.setPixelSize(44);
        m_preview->setFont(pf);
        showGlyph(m_grid->selection());
    }

    void showGlyph(uint code)
    {
        const QString s =
            QString::fromUcs4(reinterpret_cast<const char32_t *>(&code), 1);
        m_preview->setText(s);
        m_codeLabel->setText(
            QStringLiteral("<b>U+%1</b>  ·  decimal %2  ·  «%3»")
                .arg(code, 4, 16, QLatin1Char('0'))
                .arg(code)
                .arg(s.toHtmlEscaped()));
    }

    void appendCode(uint code)
    {
        m_toCopy->setText(
            m_toCopy->text()
            + QString::fromUcs4(reinterpret_cast<const char32_t *>(&code), 1));
    }

    void copyOut()
    {
        const QString text = m_toCopy->text();
        if (text.isEmpty())
            return;
        QApplication::clipboard()->setText(text);
        m_codeLabel->setText(
            QStringLiteral("Copiado al portapapeles: «%1»")
                .arg(text.toHtmlEscaped()));
    }

    QColor m_accent;
    QComboBox *m_fontBox = nullptr, *m_blockBox = nullptr;
    CharGrid *m_grid = nullptr;
    QLabel *m_preview = nullptr, *m_codeLabel = nullptr;
    QLineEdit *m_toCopy = nullptr;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-caracteres"));
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

    const QString repo = QDir(cli.value(QStringLiteral("repo"))).absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const QString accentStr =
        ThemeTokens::load(castalia::themeConfPath(repo, themeId))
            .str(QStringLiteral("colors"), QStringLiteral("accent"));
    castalia::applyTheme(&app, repo, themeId);
    const QColor accent(accentStr.isEmpty() ? QStringLiteral("#3E82B6")
                                            : accentStr);

    CharMap w(accent);
    // A pleasant default selection for screenshots: the © sign.
    w.grid()->setBlock(0x00A1, 0x00FF);
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
