// castalia-escritor — Escritor, a WordPad-class rich text editor (Bible §9.3
// "Rich Text Editor"). Formatted documents with bold/italic/underline, fonts
// and sizes, colour, alignment and lists — open and save as HTML, plain text
// or ODF (.odt). Pure Qt5 (QTextEdit / QTextDocument) + libcastalia-ui
// theming; no external office engine, no third-party assets (§3.9).
//
// Usage: castalia-escritor --theme classic [--repo P] [file]
//        [--demo] [--screenshot out.png]

#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QTextCharFormat>
#include <QTextDocumentWriter>
#include <QTextEdit>
#include <QTextList>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>

#include "Recent.h"
#include "Theme.h"

class Escritor : public QMainWindow {
    Q_OBJECT
public:
    explicit Escritor(const QColor &accent) : m_accent(accent)
    {
        m_edit = new QTextEdit(this);
        m_edit->setAcceptRichText(true);
        m_edit->document()->setDefaultFont(QFont(QStringLiteral("DejaVu Sans"),
                                                 11));
        setCentralWidget(m_edit);
        resize(760, 620);

        buildMenu();
        buildToolbar();

        connect(m_edit, &QTextEdit::currentCharFormatChanged, this,
                &Escritor::syncFormatButtons);
        connect(m_edit->document(), &QTextDocument::modificationChanged, this,
                &Escritor::updateTitle);
        newDoc();
    }

    QTextEdit *edit() { return m_edit; }

    void loadFile(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Abrir"),
                                 tr("No se pudo abrir «%1».").arg(path));
            return;
        }
        const QByteArray data = f.readAll();
        castalia::recent::add(path, QStringLiteral("Escritor"));   // §7.9
        const QString lower = path.toLower();
        if (lower.endsWith(QLatin1String(".txt")))
            m_edit->setPlainText(QString::fromUtf8(data));
        else if (lower.endsWith(QLatin1String(".md")))
            m_edit->document()->setMarkdown(QString::fromUtf8(data));
        else
            m_edit->setHtml(QString::fromUtf8(data));
        m_file = path;
        m_edit->document()->setModified(false);
        updateTitle();
    }

    // Populate a sample formatted document (for --demo / screenshots).
    void loadSample()
    {
        m_edit->setHtml(QStringLiteral(
            "<h1 style='color:%1'>Escritor de Castalia</h1>"
            "<p>Un editor de <b>texto con formato</b>, ligero y "
            "<i>original</i>, al estilo de los clásicos — pero nuestro.</p>"
            "<p>Admite <u>subrayado</u>, <span style='color:#C4321E'>color"
            "</span>, tamaños de letra y alineación.</p>"
            "<ul><li>Listas con viñetas</li><li>y numeradas</li></ul>"
            "<p style='text-align:center'>Guarda como HTML, texto plano "
            "u <b>ODT</b>.</p>")
            .arg(m_accent.darker(115).name()));
        m_edit->document()->setModified(false);
        m_file.clear();
        updateTitle();
    }

protected:
    void closeEvent(QCloseEvent *e) override
    {
        if (maybeSave())
            e->accept();
        else
            e->ignore();
    }

private:
    void buildMenu()
    {
        auto *file = menuBar()->addMenu(tr("&Archivo"));
        addAct(file, tr("Nuevo"), QKeySequence::New, [this]() { newDoc(); });
        addAct(file, tr("Abrir…"), QKeySequence::Open, [this]() { open(); });
        addAct(file, tr("Guardar"), QKeySequence::Save, [this]() { save(); });
        addAct(file, tr("Guardar como…"), QKeySequence::SaveAs,
               [this]() { saveAs(); });
        file->addSeparator();
        addAct(file, tr("Salir"), QKeySequence::Quit, [this]() { close(); });

        auto *fmt = menuBar()->addMenu(tr("&Formato"));
        addAct(fmt, tr("Color del texto…"), QKeySequence(),
               [this]() { pickColor(); });
        m_actBullet = addAct(fmt, tr("Lista con viñetas"), QKeySequence(),
                             [this]() { makeList(QTextListFormat::ListDisc); });
        m_actNumber = addAct(fmt, tr("Lista numerada"), QKeySequence(),
                             [this]() {
                                 makeList(QTextListFormat::ListDecimal);
                             });

        auto *help = menuBar()->addMenu(tr("A&yuda"));
        help->addAction(tr("Acerca de Escritor"), this, [this]() {
            QMessageBox::about(
                this, tr("Acerca de Escritor"),
                tr("Escritor de Castalia OS.\n\nUn editor de texto con formato "
                   "propio (Qt / QTextDocument): HTML, texto plano y ODT, sin "
                   "motor ofimático externo ni recursos de terceros."));
        });
    }

    void buildToolbar()
    {
        auto *tb = addToolBar(tr("Formato"));
        tb->setMovable(false);

        m_family = new QFontComboBox(tb);
        connect(m_family, &QFontComboBox::currentFontChanged, this,
                [this](const QFont &f) {
                    QTextCharFormat fmt;
                    fmt.setFontFamily(f.family());
                    merge(fmt);
                });
        tb->addWidget(m_family);

        m_size = new QComboBox(tb);
        m_size->setEditable(true);
        for (int s : {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 36, 48})
            m_size->addItem(QString::number(s));
        m_size->setCurrentText(QStringLiteral("11"));
        connect(m_size, &QComboBox::currentTextChanged, this,
                [this](const QString &t) {
                    bool ok = false;
                    const double pt = t.toDouble(&ok);
                    if (ok && pt > 0) {
                        QTextCharFormat fmt;
                        fmt.setFontPointSize(pt);
                        merge(fmt);
                    }
                });
        tb->addWidget(m_size);
        tb->addSeparator();

        m_bold = toggle(tb, tr("N"), tr("Negrita"), QKeySequence::Bold,
                        [this](bool on) {
                            QTextCharFormat f;
                            f.setFontWeight(on ? QFont::Bold : QFont::Normal);
                            merge(f);
                        });
        m_bold->setFont(bold(m_bold->font()));
        m_italic = toggle(tb, tr("C"), tr("Cursiva"), QKeySequence::Italic,
                          [this](bool on) {
                              QTextCharFormat f;
                              f.setFontItalic(on);
                              merge(f);
                          });
        { QFont f = m_italic->font(); f.setItalic(true); m_italic->setFont(f); }
        m_under = toggle(tb, tr("S"), tr("Subrayado"), QKeySequence::Underline,
                         [this](bool on) {
                             QTextCharFormat f;
                             f.setFontUnderline(on);
                             merge(f);
                         });
        { QFont f = m_under->font(); f.setUnderline(true); m_under->setFont(f); }
        tb->addSeparator();

        auto *ag = new QActionGroup(this);
        m_left = align(tb, ag, tr("Izq."), Qt::AlignLeft);
        m_center = align(tb, ag, tr("Centro"), Qt::AlignHCenter);
        m_right = align(tb, ag, tr("Der."), Qt::AlignRight);
        m_just = align(tb, ag, tr("Just."), Qt::AlignJustify);
        m_left->setChecked(true);
    }

    // ---- actions ------------------------------------------------------------
    template <typename F>
    QAction *addAct(QMenu *m, const QString &text, const QKeySequence &sc, F fn)
    {
        auto *a = m->addAction(text, this, fn);
        if (!sc.isEmpty())
            a->setShortcut(sc);
        return a;
    }
    template <typename F>
    QAction *toggle(QToolBar *tb, const QString &text, const QString &tip,
                    const QKeySequence &sc, F fn)
    {
        auto *a = tb->addAction(text);
        a->setCheckable(true);
        a->setToolTip(tip);
        a->setShortcut(sc);
        connect(a, &QAction::triggered, this, fn);
        return a;
    }
    QAction *align(QToolBar *tb, QActionGroup *ag, const QString &text,
                   Qt::Alignment al)
    {
        auto *a = tb->addAction(text);
        a->setCheckable(true);
        ag->addAction(a);
        connect(a, &QAction::triggered, this,
                [this, al]() { m_edit->setAlignment(al); });
        return a;
    }
    static QFont bold(QFont f) { f.setBold(true); return f; }

    void merge(const QTextCharFormat &fmt)
    {
        QTextCursor c = m_edit->textCursor();
        if (!c.hasSelection())
            c.select(QTextCursor::WordUnderCursor);
        c.mergeCharFormat(fmt);
        m_edit->mergeCurrentCharFormat(fmt);
    }

    void pickColor()
    {
        const QColor col = QColorDialog::getColor(
            m_edit->textColor(), this, tr("Color del texto"));
        if (col.isValid()) {
            QTextCharFormat f;
            f.setForeground(col);
            merge(f);
        }
    }

    void makeList(QTextListFormat::Style style)
    {
        QTextCursor c = m_edit->textCursor();
        QTextListFormat lf;
        lf.setStyle(style);
        c.createList(lf);
    }

    void syncFormatButtons(const QTextCharFormat &f)
    {
        if (m_bold)
            m_bold->setChecked(f.fontWeight() >= QFont::Bold);
        if (m_italic)
            m_italic->setChecked(f.fontItalic());
        if (m_under)
            m_under->setChecked(f.fontUnderline());
        if (m_family && !f.fontFamily().isEmpty())
            m_family->setCurrentFont(QFont(f.fontFamily()));
        if (m_size && f.fontPointSize() > 0)
            m_size->setCurrentText(
                QString::number(int(f.fontPointSize())));
        const Qt::Alignment al = m_edit->alignment();
        if (m_left)
            m_left->setChecked(al & Qt::AlignLeft);
        if (m_center)
            m_center->setChecked(al & Qt::AlignHCenter);
        if (m_right)
            m_right->setChecked(al & Qt::AlignRight);
        if (m_just)
            m_just->setChecked(al & Qt::AlignJustify);
    }

    // ---- file lifecycle -----------------------------------------------------
    void newDoc()
    {
        if (!maybeSave())
            return;
        m_edit->clear();
        m_file.clear();
        m_edit->document()->setModified(false);
        updateTitle();
    }

    void open()
    {
        if (!maybeSave())
            return;
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Abrir documento"), QString(),
            tr("Documentos (*.html *.htm *.txt *.md);;Todos los archivos (*)"));
        if (!path.isEmpty())
            loadFile(path);
    }

    bool save()
    {
        if (m_file.isEmpty())
            return saveAs();
        return writeTo(m_file);
    }

    bool saveAs()
    {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Guardar como"), QString(),
            tr("HTML (*.html);;Texto (*.txt);;OpenDocument (*.odt)"));
        if (path.isEmpty())
            return false;
        return writeTo(path);
    }

    bool writeTo(const QString &path)
    {
        const QString lower = path.toLower();
        if (lower.endsWith(QLatin1String(".txt"))) {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
                return fail(path);
            QTextStream(&f) << m_edit->toPlainText();
        } else if (lower.endsWith(QLatin1String(".odt"))) {
            QTextDocumentWriter w(path);
            w.setFormat("odf");
            if (!w.write(m_edit->document()))
                return fail(path);
        } else {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
                return fail(path);
            QTextStream(&f) << m_edit->toHtml();
        }
        m_file = path;
        m_edit->document()->setModified(false);
        updateTitle();
        return true;
    }

    bool fail(const QString &path)
    {
        QMessageBox::warning(this, tr("Guardar"),
                             tr("No se pudo guardar «%1».").arg(path));
        return false;
    }

    bool maybeSave()
    {
        if (!m_edit->document()->isModified())
            return true;
        const auto ans = QMessageBox::warning(
            this, tr("Escritor"),
            tr("El documento tiene cambios sin guardar. ¿Guardar ahora?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (ans == QMessageBox::Save)
            return save();
        return ans == QMessageBox::Discard;
    }

    void updateTitle()
    {
        const QString name = m_file.isEmpty()
                                 ? tr("Documento nuevo")
                                 : QFileInfo(m_file).fileName();
        const QString dirty =
            m_edit->document()->isModified() ? QStringLiteral("*") : QString();
        setWindowTitle(QStringLiteral("%1%2 — Escritor").arg(name, dirty));
    }

    QColor m_accent;
    QTextEdit *m_edit = nullptr;
    QFontComboBox *m_family = nullptr;
    QComboBox *m_size = nullptr;
    QAction *m_bold = nullptr, *m_italic = nullptr, *m_under = nullptr;
    QAction *m_left = nullptr, *m_center = nullptr, *m_right = nullptr,
            *m_just = nullptr;
    QAction *m_actBullet = nullptr, *m_actNumber = nullptr;
    QString m_file;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-escritor"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Open a sample formatted document")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.addPositionalArgument(QStringLiteral("file"),
                              QStringLiteral("Document to open"));
    cli.process(app);

    const QString repo = QDir(cli.value(QStringLiteral("repo"))).absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const QString accentStr =
        ThemeTokens::load(castalia::themeConfPath(repo, themeId))
            .str(QStringLiteral("colors"), QStringLiteral("accent"));
    castalia::applyTheme(&app, repo, themeId);
    const QColor accent(accentStr.isEmpty() ? QStringLiteral("#3E82B6")
                                            : accentStr);

    Escritor w(accent);
    const QStringList pos = cli.positionalArguments();
    if (cli.isSet(QStringLiteral("demo")))
        w.loadSample();
    else if (!pos.isEmpty())
        w.loadFile(pos.first());
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
