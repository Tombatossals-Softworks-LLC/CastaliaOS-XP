// castalia-calendario — the Castalia Calendar (Bible §9, XP-parity §10).
//
// A calm month calendar with a per-day notes pane: pick a day, jot a note, it
// saves itself. Notes live as one small text file per day under
// ~/.local/share/castalia/calendario, so they survive and are trivially
// backed up (and swept into a Restore Point, §9/P8). A dot marks days that
// already have a note. Pure Qt5 + libcastalia-ui theming; no runtime deps.
//
// The panel clock opens this (click the time), the XP-era ergonomic.
//
// Usage: castalia-calendario --theme human [--repo PATH] [--date YYYY-MM-DD]
//                            [--screenshot out.png]

#include <QApplication>
#include <QCalendarWidget>
#include <QColor>
#include <QCommandLineParser>
#include <QDate>
#include <QDir>
#include <QLocale>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextCharFormat>
#include <QTimer>
#include <QVBoxLayout>

#include "Theme.h"

namespace {

// One small text file per day: <data>/castalia/calendario/YYYY-MM-DD.txt.
QString notesDir()
{
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    // AppDataLocation already ends in the app name on some platforms; anchor
    // on a stable, predictable path under the user's data dir instead.
    const QString home = qEnvironmentVariable("HOME", QStringLiteral("/root"));
    Q_UNUSED(base);
    return home + QStringLiteral("/.local/share/castalia/calendario");
}

QString noteFile(const QDate &d)
{
    return notesDir() + QLatin1Char('/')
        + d.toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".txt");
}

QString readNote(const QDate &d)
{
    QFile f(noteFile(d));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

bool hasNote(const QDate &d)
{
    QFileInfo fi(noteFile(d));
    return fi.exists() && fi.size() > 0;
}

} // namespace

class Calendario : public QWidget {
    Q_OBJECT
public:
    Calendario(const QString &repo, const ThemeTokens &tokens,
               const QDate &start)
        : m_repo(repo), m_tokens(tokens)
    {
        setWindowTitle(QStringLiteral("Calendario — Castalia"));
        resize(620, 420);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // header on the titlebar gradient (matches the other first-party apps)
        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("CalHeader"));
        head->setFixedHeight(58);
        head->setStyleSheet(QStringLiteral(
            "#CalHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(colorTok("titlebar_top"), colorTok("titlebar_bottom")));
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(16, 0, 16, 0);
        m_headLabel = new QLabel(head);
        hl->addWidget(m_headLabel);
        hl->addStretch(1);
        auto *today = new QPushButton(QStringLiteral("Hoy"), head);
        today->setObjectName(QStringLiteral("CalToday"));
        today->setCursor(Qt::PointingHandCursor);
        connect(today, &QPushButton::clicked, this, [this]() {
            m_cal->setSelectedDate(QDate::currentDate());
            m_cal->showToday();
        });
        hl->addWidget(today);
        root->addWidget(head);

        auto *body = new QHBoxLayout;
        body->setContentsMargins(14, 12, 14, 14);
        body->setSpacing(14);

        m_cal = new QCalendarWidget(this);
        // Spanish month/day names to match Castalia's Spanish-default UI,
        // independent of the host locale (so renders read "julio", "domingo").
        m_cal->setLocale(QLocale(QLocale::Spanish, QLocale::Spain));
        m_cal->setGridVisible(true);
        m_cal->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
        m_cal->setFirstDayOfWeek(Qt::Monday);
        m_cal->setSelectedDate(start);
        body->addWidget(m_cal, 3);

        auto *side = new QVBoxLayout;
        side->setSpacing(6);
        m_dayLabel = new QLabel(this);
        m_dayLabel->setStyleSheet(
            QStringLiteral("font-size:14px;font-weight:bold;"));
        side->addWidget(m_dayLabel);
        auto *hint = new QLabel(QStringLiteral("Nota del día"), this);
        hint->setProperty("secondary", true);
        side->addWidget(hint);
        m_note = new QPlainTextEdit(this);
        m_note->setPlaceholderText(
            QStringLiteral("Escribe una nota para este día…"));
        side->addWidget(m_note, 1);
        m_saved = new QLabel(this);
        m_saved->setProperty("secondary", true);
        side->addWidget(m_saved);
        body->addLayout(side, 2);
        root->addLayout(body, 1);

        // Load the starting day and keep notes in sync as the user navigates.
        connect(m_cal, &QCalendarWidget::selectionChanged, this,
                &Calendario::onDayChanged);
        // Autosave shortly after typing stops (no Save button to forget).
        m_autosave = new QTimer(this);
        m_autosave->setSingleShot(true);
        m_autosave->setInterval(600);
        connect(m_autosave, &QTimer::timeout, this, &Calendario::save);
        connect(m_note, &QPlainTextEdit::textChanged, this, [this]() {
            m_saved->setText(QStringLiteral("Guardando…"));
            m_autosave->start();
        });

        refreshMarks();
        onDayChanged();
    }

    ~Calendario() override { save(); }

private slots:
    void onDayChanged()
    {
        save();                 // persist the day we're leaving
        m_current = m_cal->selectedDate();
        // QLocale (defaulted to Spanish in main) gives localised names;
        // QDate::toString would ignore the default locale.
        const QLocale loc;
        m_headLabel->setText(QStringLiteral(
            "<span style='color:%1;font-size:16px;font-weight:bold'>%2</span>")
            .arg(colorTok("titlebar_text"),
                 loc.toString(m_current, QStringLiteral("MMMM yyyy"))));
        m_dayLabel->setText(
            loc.toString(m_current, QStringLiteral("dddd d 'de' MMMM")));
        m_loading = true;
        m_note->setPlainText(readNote(m_current));
        m_loading = false;
        m_saved->setText(hasNote(m_current) ? QStringLiteral("Nota guardada")
                                            : QString());
    }

    void save()
    {
        if (m_loading || !m_current.isValid())
            return;
        const QString text = m_note->toPlainText();
        const QString path = noteFile(m_current);
        QDir().mkpath(notesDir());
        if (text.trimmed().isEmpty()) {
            QFile::remove(path);        // empty note → no stray file / dot
        } else {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                f.write(text.toUtf8());
        }
        m_saved->setText(QStringLiteral("Nota guardada"));
        refreshMarks();
    }

private:
    QString colorTok(const char *key) const
    {
        return m_tokens.str(QStringLiteral("colors"),
                            QString::fromLatin1(key));
    }

    // Bold + accent-dot the days in view that already carry a note.
    void refreshMarks()
    {
        QTextCharFormat plain;
        // clear a generous window around the current month
        const QDate anchor = m_cal->selectedDate().isValid()
            ? m_cal->selectedDate() : QDate::currentDate();
        const QDate from = anchor.addDays(-60), to = anchor.addDays(60);
        for (QDate d = from; d <= to; d = d.addDays(1))
            m_cal->setDateTextFormat(d, plain);
        QTextCharFormat marked;
        marked.setFontWeight(QFont::Bold);
        marked.setForeground(QColor(colorTok("accent")));
        for (QDate d = from; d <= to; d = d.addDays(1))
            if (hasNote(d))
                m_cal->setDateTextFormat(d, marked);
    }

    QString m_repo;
    ThemeTokens m_tokens;
    QCalendarWidget *m_cal = nullptr;
    QPlainTextEdit *m_note = nullptr;
    QLabel *m_headLabel = nullptr, *m_dayLabel = nullptr, *m_saved = nullptr;
    QTimer *m_autosave = nullptr;
    QDate m_current;
    bool m_loading = false;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-calendario"));
    // Spanish-first UI (§3): month/day names read in Spanish regardless of
    // the host locale, matching the rest of Castalia.
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("date"),
                   QStringLiteral("Initial date YYYY-MM-DD"),
                   QStringLiteral("date")});
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
        QStringLiteral("#CalToday{font-weight:bold;border-color:%1;}")
            .arg(accent));

    QDate start = QDate::currentDate();
    const QString ds = cli.value(QStringLiteral("date"));
    if (!ds.isEmpty()) {
        const QDate parsed = QDate::fromString(ds, QStringLiteral("yyyy-MM-dd"));
        if (parsed.isValid())
            start = parsed;
    }

    Calendario w(repo, tokens, start);
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(150, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
