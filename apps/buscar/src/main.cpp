// castalia-buscar — the Castalia File Search (Bible §9, XP-parity §10).
//
// The familiar "find a file by name" tool. Type part of a name, choose where
// to look (your home folder by default), and it walks the tree in a background
// thread so the window never freezes — results stream in, you can cancel, and
// double-clicking a hit opens it (or its folder) with the system handler.
// Pure Qt5 + libcastalia-ui theming; reuses the shared search icon.
//
// Usage: castalia-buscar --theme human [--repo PATH] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include "Theme.h"

// Walks a directory tree off the UI thread, emitting each name match. A
// cancel flag lets the window stop a long search immediately (§16: the shell
// stays responsive even on a spinning-rust FLOOR machine).
class SearchWorker : public QObject {
    Q_OBJECT
public:
    static constexpr int kCap = 2000;   // stop after this many hits
    void cancel() { m_cancel.store(true); }

public slots:
    void run(const QString &root, const QString &needle)
    {
        m_cancel.store(false);
        int hits = 0;
        const QString q = needle.toLower();
        QDirIterator it(root,
                        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (m_cancel.load())
                break;
            const QString path = it.next();
            if (it.fileName().toLower().contains(q)) {
                emit found(path);
                if (++hits >= kCap)
                    break;
            }
        }
        emit finished(hits, hits >= kCap, m_cancel.load());
    }

signals:
    void found(const QString &path);
    void finished(int count, bool capped, bool cancelled);

private:
    std::atomic<bool> m_cancel{false};
};

class Search : public QWidget {
    Q_OBJECT
public:
    Search(const QString &repo, const ThemeTokens &tokens)
        : m_repo(repo), m_tokens(tokens)
    {
        setWindowTitle(QStringLiteral("Buscar archivos — Castalia"));
        resize(560, 460);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("SrHeader"));
        head->setFixedHeight(50);
        head->setStyleSheet(QStringLiteral(
            "#SrHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        auto *title = new QLabel(head);
        title->setText(QStringLiteral(
            "<span style='color:%1;font-weight:bold'>Buscar archivos</span>")
            .arg(colorTok("titlebar_text")));
        hl->addWidget(title);
        hl->addStretch(1);
        root->addWidget(head);

        auto *body = new QVBoxLayout;
        body->setContentsMargins(16, 14, 16, 16);
        body->setSpacing(10);

        auto *form = new QHBoxLayout;
        m_query = new QLineEdit(this);
        m_query->setPlaceholderText(
            QStringLiteral("Parte del nombre del archivo…"));
        connect(m_query, &QLineEdit::returnPressed, this, &Search::toggle);
        form->addWidget(m_query, 1);
        m_where = new QLineEdit(
            qEnvironmentVariable("HOME", QStringLiteral("/root")), this);
        m_where->setFixedWidth(150);
        m_where->setToolTip(QStringLiteral("Carpeta donde buscar"));
        form->addWidget(m_where);
        m_go = new QPushButton(QStringLiteral("Buscar"), this);
        m_go->setObjectName(QStringLiteral("SrGo"));
        connect(m_go, &QPushButton::clicked, this, &Search::toggle);
        form->addWidget(m_go);
        body->addLayout(form);

        m_results = new QListWidget(this);
        m_results->setObjectName(QStringLiteral("SrList"));
        connect(m_results, &QListWidget::itemActivated, this,
                &Search::openItem);
        body->addWidget(m_results, 1);

        m_status = new QLabel(this);
        m_status->setProperty("secondary", true);
        m_status->setText(QStringLiteral(
            "Escribe un nombre y pulsa Buscar. Doble clic abre el resultado."));
        body->addWidget(m_status);
        root->addLayout(body);

        // Wire the worker on its own thread.
        m_worker = new SearchWorker;
        m_worker->moveToThread(&m_thread);
        connect(&m_thread, &QThread::finished, m_worker,
                &QObject::deleteLater);
        connect(this, &Search::startSearch, m_worker, &SearchWorker::run);
        connect(m_worker, &SearchWorker::found, this, &Search::onFound);
        connect(m_worker, &SearchWorker::finished, this, &Search::onFinished);
        m_thread.start();
    }

    ~Search() override
    {
        if (m_worker)
            m_worker->cancel();
        m_thread.quit();
        m_thread.wait(1500);
    }

signals:
    void startSearch(const QString &root, const QString &needle);

private slots:
    void toggle()
    {
        if (m_running) {          // acts as Cancel while a search runs
            m_worker->cancel();
            return;
        }
        const QString q = m_query->text().trimmed();
        const QString dir = m_where->text().trimmed();
        if (q.isEmpty() || !QFileInfo(dir).isDir()) {
            m_status->setText(QStringLiteral(
                "Escribe un nombre y una carpeta válida."));
            return;
        }
        m_results->clear();
        m_running = true;
        m_go->setText(QStringLiteral("Detener"));
        m_status->setText(QStringLiteral("Buscando en %1…").arg(dir));
        emit startSearch(dir, q);
    }

    void onFound(const QString &path)
    {
        auto *it = new QListWidgetItem(
            QStringLiteral("%1   —   %2")
                .arg(QFileInfo(path).fileName(), path),
            m_results);
        it->setData(Qt::UserRole, path);
        // Keep the UI light on huge result sets.
        if (m_results->count() % 64 == 0)
            m_status->setText(QStringLiteral("%1 resultados…")
                                  .arg(m_results->count()));
    }

    void onFinished(int count, bool capped, bool cancelled)
    {
        m_running = false;
        m_go->setText(QStringLiteral("Buscar"));
        QString msg = cancelled
            ? QStringLiteral("Búsqueda detenida (%1 resultados).").arg(count)
            : QStringLiteral("%1 resultado(s).").arg(count);
        if (capped)
            msg += QStringLiteral(" Se muestran los primeros %1.")
                       .arg(SearchWorker::kCap);
        m_status->setText(msg);
    }

    void openItem(QListWidgetItem *item)
    {
        const QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty())
            return;
        // Open files with the system handler; open folders directly.
        QProcess::startDetached(QStringLiteral("xdg-open"), {path});
    }

private:
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }

    QString m_repo;
    ThemeTokens m_tokens;
    QLineEdit *m_query = nullptr, *m_where = nullptr;
    QPushButton *m_go = nullptr;
    QListWidget *m_results = nullptr;
    QLabel *m_status = nullptr;
    QThread m_thread;
    SearchWorker *m_worker = nullptr;
    bool m_running = false;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-buscar"));
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
        QStringLiteral("#SrGo{font-weight:bold;border-color:%1;}").arg(accent));

    Search w(repo, tokens);
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
