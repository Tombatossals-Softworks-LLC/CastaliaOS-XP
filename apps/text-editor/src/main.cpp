// castalia-notas — the Castalia plain-text editor (Bible §9.3 "Text Editor").
//
// A fast, Notepad-ergonomic editor: open/save, find, word-wrap, encoding
// awareness, live line/column status. Pure Qt5 + libcastalia-ui theming.
//
// Usage:
//   castalia-notas [FILE] --theme classic [--repo PATH] [--screenshot out.png]

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>

#include "Recent.h"
#include "Theme.h"

class Notas : public QMainWindow {
    Q_OBJECT
public:
    explicit Notas(const QString &repo)
    {
        m_edit = new QPlainTextEdit(this);
        m_edit->setFrameShape(QFrame::NoFrame);
        m_edit->setTabStopDistance(32);
        setCentralWidget(m_edit);

        auto add = [](QMenu *m, const QString &text,
                      QKeySequence::StandardKey key, auto recv, auto slot) {
            QAction *a = m->addAction(text, recv, slot);
            a->setShortcut(key);
            return a;
        };
        QMenu *file = menuBar()->addMenu(QStringLiteral("&Archivo"));
        add(file, QStringLiteral("&Nuevo"), QKeySequence::New, this,
            &Notas::newDoc);
        add(file, QStringLiteral("&Abrir…"), QKeySequence::Open, this,
            &Notas::open);
        add(file, QStringLiteral("&Guardar"), QKeySequence::Save, this,
            &Notas::save);
        add(file, QStringLiteral("Guardar &como…"), QKeySequence::SaveAs,
            this, &Notas::saveAs);
        file->addSeparator();
        add(file, QStringLiteral("&Cerrar"), QKeySequence::Quit, this,
            &QWidget::close);

        QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edición"));
        add(edit, QStringLiteral("&Deshacer"), QKeySequence::Undo, m_edit,
            &QPlainTextEdit::undo);
        add(edit, QStringLiteral("&Rehacer"), QKeySequence::Redo, m_edit,
            &QPlainTextEdit::redo);
        edit->addSeparator();
        add(edit, QStringLiteral("&Buscar…"), QKeySequence::Find, this,
            &Notas::find);

        QMenu *view = menuBar()->addMenu(QStringLiteral("&Ver"));
        auto *wrap = view->addAction(QStringLiteral("Ajuste de línea"));
        wrap->setCheckable(true);
        wrap->setChecked(true);
        connect(wrap, &QAction::toggled, this, [this](bool on) {
            m_edit->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth
                                       : QPlainTextEdit::NoWrap);
        });

        menuBar()->addMenu(QStringLiteral("Ay&uda"))
            ->addAction(QStringLiteral("Acerca de Notas"), this, [this]() {
                QMessageBox::about(this, QStringLiteral("Notas"),
                    QStringLiteral("Castalia Notas — editor de texto\n"
                                   "Tombatossals Softworks"));
            });

        m_pos = new QLabel(this);
        statusBar()->addPermanentWidget(m_pos);
        connect(m_edit, &QPlainTextEdit::cursorPositionChanged,
                this, &Notas::updatePos);
        connect(m_edit->document(), &QTextDocument::modificationChanged,
                this, &Notas::updateTitle);

        Q_UNUSED(repo);
        resize(680, 480);
        newDoc();
    }

    void loadFile(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        QTextStream in(&f);
        m_edit->setPlainText(in.readAll());
        m_path = path;
        m_edit->document()->setModified(false);
        // §7.9: opening a document is what makes it "recent".
        castalia::recent::add(path, QStringLiteral("Notas"));
        updateTitle();
        updatePos();
    }

private slots:
    void newDoc()
    {
        m_edit->clear();
        m_path.clear();
        m_edit->document()->setModified(false);
        updateTitle();
        updatePos();
    }
    void open()
    {
        const QString p = QFileDialog::getOpenFileName(
            this, QStringLiteral("Abrir"), QDir::homePath());
        if (!p.isEmpty())
            loadFile(p);
    }
    void save()
    {
        if (m_path.isEmpty()) { saveAs(); return; }
        QFile f(m_path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return;
        QTextStream(&f) << m_edit->toPlainText();
        m_edit->document()->setModified(false);
        updateTitle();
    }
    void saveAs()
    {
        const QString p = QFileDialog::getSaveFileName(
            this, QStringLiteral("Guardar como"), QDir::homePath());
        if (!p.isEmpty()) { m_path = p; save(); }
    }
    void find()
    {
        bool ok = false;
        const QString needle = QInputDialog::getText(
            this, QStringLiteral("Buscar"), QStringLiteral("Texto:"),
            QLineEdit::Normal, m_lastFind, &ok);
        if (ok && !needle.isEmpty()) {
            m_lastFind = needle;
            if (!m_edit->find(needle)) {
                m_edit->moveCursor(QTextCursor::Start);
                m_edit->find(needle);
            }
        }
    }
    void updatePos()
    {
        const QTextCursor c = m_edit->textCursor();
        m_pos->setText(QStringLiteral("Ln %1, Col %2  ·  UTF-8")
                           .arg(c.blockNumber() + 1)
                           .arg(c.positionInBlock() + 1));
    }
    void updateTitle()
    {
        const QString name = m_path.isEmpty()
                                 ? QStringLiteral("Sin título")
                                 : QFileInfo(m_path).fileName();
        setWindowTitle((m_edit->document()->isModified()
                            ? QStringLiteral("• ") : QString())
                       + name + QStringLiteral(" — Notas"));
    }

private:
    QPlainTextEdit *m_edit = nullptr;
    QLabel *m_pos = nullptr;
    QString m_path;
    QString m_lastFind;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-notas"));

    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addPositionalArgument(QStringLiteral("file"),
                              QStringLiteral("File to open"));
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
    castalia::applyTheme(&app, repo, cli.value(QStringLiteral("theme")));

    Notas w(repo);
    if (!cli.positionalArguments().isEmpty())
        w.loadFile(cli.positionalArguments().first());
    else if (cli.isSet(QStringLiteral("screenshot")))
        w.findChild<QPlainTextEdit *>()->setPlainText(QStringLiteral(
            "Castalia Notas\n==============\n\n"
            "Un editor de texto rápido y sin distracciones.\n\n"
            "· Abrir, guardar, buscar\n"
            "· Ajuste de línea\n"
            "· Ln/Col y codificación en la barra de estado\n\n"
            "Peñíscola, Oropesa, Benicàssim…\n"));
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(120, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
