// castalia-monitor — the Task Manager / System Monitor (Bible §9).
//
// A live view of the running system, read straight from /proc (no deps): a
// sortable process table with per-process CPU% and memory and an "end task"
// button, plus a Rendimiento tab with a scrolling total-CPU graph, per-core
// bars and a memory meter. Pure Qt5 + libcastalia-ui.
//
// Usage: castalia-monitor --theme classic [--repo P] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <csignal>
#include <unistd.h>

#include "Mark.h"
#include "Theme.h"

namespace {

QString slurp(const QString &p)
{
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

int ncpu()
{
    int n = 0;
    for (const QString &l : slurp(QStringLiteral("/proc/stat"))
                                .split(QLatin1Char('\n')))
        if (l.startsWith(QStringLiteral("cpu")) && l.size() > 3
            && l.at(3).isDigit())
            ++n;
    return qMax(1, n);
}

QString human(double kib)
{
    if (kib >= 1024 * 1024)
        return QStringLiteral("%1 GiB").arg(kib / 1024 / 1024, 0, 'f', 1);
    if (kib >= 1024)
        return QStringLiteral("%1 MiB").arg(kib / 1024, 0, 'f', 1);
    return QStringLiteral("%1 KiB").arg(kib, 0, 'f', 0);
}

struct Proc {
    int pid = 0;
    QString name;
    unsigned long long ticks = 0;  // utime + stime
    double rssKib = 0;
    double cpu = 0;  // filled by the model
};

// Snapshot all processes from /proc.
QVector<Proc> readProcs()
{
    QVector<Proc> out;
    QDir proc(QStringLiteral("/proc"));
    const long pageKib = sysconf(_SC_PAGESIZE) / 1024;
    for (const QString &d : proc.entryList(QDir::Dirs, QDir::Name)) {
        bool ok = false;
        const int pid = d.toInt(&ok);
        if (!ok)
            continue;
        const QString stat = slurp(QStringLiteral("/proc/%1/stat").arg(pid));
        if (stat.isEmpty())
            continue;
        // name is in parens and may contain spaces/parens: split on last ')'.
        const int lp = stat.indexOf(QLatin1Char('('));
        const int rp = stat.lastIndexOf(QLatin1Char(')'));
        if (lp < 0 || rp < 0)
            continue;
        Proc p;
        p.pid = pid;
        p.name = stat.mid(lp + 1, rp - lp - 1);
        const QStringList f =
            stat.mid(rp + 2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // after the name, fields are 0-indexed: state=0 ... utime=11, stime=12,
        // rss(pages)=21
        if (f.size() > 21) {
            p.ticks = f[11].toULongLong() + f[12].toULongLong();
            p.rssKib = f[21].toDouble() * pageKib;
        }
        out.push_back(p);
    }
    return out;
}

unsigned long long cpuTotal()
{
    for (const QString &l : slurp(QStringLiteral("/proc/stat"))
                                .split(QLatin1Char('\n')))
        if (l.startsWith(QStringLiteral("cpu "))) {
            unsigned long long t = 0;
            for (const QString &v : l.mid(4).split(QLatin1Char(' '),
                                                   Qt::SkipEmptyParts))
                t += v.toULongLong();
            return t;
        }
    return 0;
}

QVector<double> perCoreUsage(QVector<unsigned long long> &prevIdle,
                             QVector<unsigned long long> &prevTot)
{
    QVector<double> use;
    int idx = 0;
    for (const QString &l : slurp(QStringLiteral("/proc/stat"))
                                .split(QLatin1Char('\n'))) {
        if (!(l.startsWith(QStringLiteral("cpu")) && l.size() > 3
              && l.at(3).isDigit()))
            continue;
        const QStringList f = l.mid(l.indexOf(QLatin1Char(' ')) + 1)
                                  .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        unsigned long long tot = 0;
        for (const QString &v : f)
            tot += v.toULongLong();
        const unsigned long long idle = f.value(3).toULongLong();
        if (prevIdle.size() <= idx) {
            prevIdle.resize(idx + 1);
            prevTot.resize(idx + 1);
        }
        const unsigned long long dt = tot - prevTot[idx];
        const unsigned long long di = idle - prevIdle[idx];
        use.push_back(dt ? qBound(0.0, 100.0 * (dt - di) / dt, 100.0) : 0.0);
        prevIdle[idx] = idle;
        prevTot[idx] = tot;
        ++idx;
    }
    return use;
}

// A tree item that sorts numerically (PID/CPU/memory via a stored value) and
// alphabetically for the process-name column.
class ProcItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &o) const override
    {
        const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (col == 1)
            return text(1).toLower() < o.text(1).toLower();
        return data(col, Qt::UserRole).toDouble()
               < o.data(col, Qt::UserRole).toDouble();
    }
};

} // namespace

class Graph : public QWidget {
public:
    Graph(const QColor &accent, QWidget *parent = nullptr)
        : QWidget(parent), m_accent(accent)
    {
        setMinimumHeight(140);
    }
    void push(double v)
    {
        m_hist.push_back(qBound(0.0, v, 100.0));
        while (m_hist.size() > 160)
            m_hist.removeFirst();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor(0x0B, 0x14, 0x1E));
        p.setPen(QColor(255, 255, 255, 24));
        for (int i = 1; i < 4; ++i) {
            const double y = height() * i / 4.0;
            p.drawLine(0, y, width(), y);
        }
        if (m_hist.size() < 2)
            return;
        const double dx = double(width()) / 159.0;
        QPainterPath path;
        path.moveTo(0, height());
        for (int i = 0; i < m_hist.size(); ++i)
            path.lineTo(i * dx, height() - m_hist[i] / 100.0 * height());
        path.lineTo((m_hist.size() - 1) * dx, height());
        path.closeSubpath();
        QColor fill = m_accent;
        fill.setAlpha(90);
        p.fillPath(path, fill);
        QPen pen(m_accent, 2);
        p.setPen(pen);
        for (int i = 1; i < m_hist.size(); ++i)
            p.drawLine(QPointF((i - 1) * dx,
                               height() - m_hist[i - 1] / 100.0 * height()),
                       QPointF(i * dx,
                               height() - m_hist[i] / 100.0 * height()));
        p.setPen(Qt::white);
        p.drawText(QRect(6, 4, width() - 12, 20), Qt::AlignLeft,
                   QStringLiteral("CPU %1%").arg(m_hist.last(), 0, 'f', 0));
    }

private:
    QColor m_accent;
    QVector<double> m_hist;
};

class CoreBars : public QWidget {
public:
    CoreBars(const QColor &accent, QWidget *parent = nullptr)
        : QWidget(parent), m_accent(accent) { setMinimumHeight(64); }
    void set(const QVector<double> &v) { m_v = v; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        if (m_v.isEmpty())
            return;
        const double bw = double(width()) / m_v.size();
        for (int i = 0; i < m_v.size(); ++i) {
            const double h = m_v[i] / 100.0 * (height() - 18);
            QRectF track(i * bw + 3, 2, bw - 6, height() - 18);
            p.fillRect(track, QColor(0, 0, 0, 40));
            QColor c = m_accent;
            if (m_v[i] > 80) c = QColor(0xB3, 0x37, 0x2E);
            else if (m_v[i] > 55) c = QColor(0xC9, 0xA2, 0x27);
            p.fillRect(QRectF(track.x(), track.bottom() - h, track.width(), h),
                       c);
            p.setPen(palette().color(QPalette::Text));
            p.drawText(QRectF(i * bw, height() - 16, bw, 16),
                       Qt::AlignCenter, QString::number(i));
        }
    }

private:
    QColor m_accent;
    QVector<double> m_v;
};

class Monitor : public QWidget {
public:
    Monitor(const QColor &accent) : m_accent(accent)
    {
        setWindowTitle(QStringLiteral("Monitor del sistema — Castalia"));
        resize(680, 540);
        m_ncpu = ncpu();
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->addWidget(buildHeader());
        auto *tabs = new QTabWidget(this);
        tabs->addTab(buildProcs(), QStringLiteral("Procesos"));
        tabs->addTab(buildPerf(), QStringLiteral("Rendimiento"));
        root->addWidget(tabs, 1);

        m_prevTotal = cpuTotal();
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this]() { tick(); });
        m_timer->start(1500);
        QTimer::singleShot(300, this, [this]() { tick(); });  // first paint
    }

private:
    QWidget *buildHeader()
    {
        auto *head = new QWidget(this);
        head->setFixedHeight(56);
        head->setStyleSheet(QStringLiteral(
            "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %1,"
            "stop:1 %2);")
            .arg(m_accent.lighter(112).name(), m_accent.darker(118).name()));
        auto *l = new QHBoxLayout(head);
        l->setContentsMargins(16, 0, 16, 0);
        auto *mk = new QLabel(head);
        QPixmap pm(36, 36);
        pm.fill(Qt::transparent);
        { QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
          castalia::drawMark(&p, QRectF(2, 2, 32, 32)); }
        mk->setPixmap(pm);
        l->addWidget(mk);
        auto *t = new QLabel(QStringLiteral(
            "<span style='color:white;font-size:15px;font-weight:bold'>"
            "Monitor del sistema</span>"), head);
        l->addWidget(t);
        l->addStretch(1);
        m_headStat = new QLabel(head);
        m_headStat->setStyleSheet(QStringLiteral("color:#EAF1F7;"));
        l->addWidget(m_headStat);
        return head;
    }

    QWidget *buildProcs()
    {
        auto *w = new QWidget(this);
        auto *lay = new QVBoxLayout(w);
        lay->setContentsMargins(12, 10, 12, 12);
        m_tree = new QTreeWidget(w);
        m_tree->setColumnCount(4);
        m_tree->setHeaderLabels({QStringLiteral("PID"),
                                 QStringLiteral("Proceso"),
                                 QStringLiteral("CPU %"),
                                 QStringLiteral("Memoria")});
        m_tree->setRootIsDecorated(false);
        m_tree->setAlternatingRowColors(true);
        m_tree->setSortingEnabled(true);
        m_tree->sortByColumn(2, Qt::DescendingOrder);
        m_tree->header()->setStretchLastSection(true);
        m_tree->setColumnWidth(0, 70);
        m_tree->setColumnWidth(1, 300);
        m_tree->setColumnWidth(2, 80);
        lay->addWidget(m_tree, 1);
        auto *row = new QHBoxLayout;
        m_count = new QLabel(w);
        m_count->setProperty("secondary", true);
        row->addWidget(m_count);
        row->addStretch(1);
        auto *kill = new QPushButton(QStringLiteral("Finalizar proceso"), w);
        kill->setObjectName(QStringLiteral("KillBtn"));
        connect(kill, &QPushButton::clicked, this, [this]() {
            auto *it = m_tree->currentItem();
            if (it)
                ::kill(it->text(0).toInt(), SIGTERM);
        });
        row->addWidget(kill);
        lay->addLayout(row);
        return w;
    }

    QWidget *buildPerf()
    {
        auto *w = new QWidget(this);
        auto *lay = new QVBoxLayout(w);
        lay->setContentsMargins(12, 10, 12, 12);
        lay->setSpacing(10);
        m_graph = new Graph(m_accent, w);
        lay->addWidget(m_graph);
        lay->addWidget(new QLabel(QStringLiteral("Uso por núcleo"), w));
        m_bars = new CoreBars(m_accent, w);
        lay->addWidget(m_bars);
        m_mem = new QLabel(w);
        lay->addWidget(m_mem);
        lay->addStretch(1);
        return w;
    }

    void tick()
    {
        // --- cpu totals ---
        const unsigned long long total = cpuTotal();
        const unsigned long long dTotal = total - m_prevTotal;

        // --- processes ---
        const QVector<Proc> procs = readProcs();
        double sumCpu = 0;
        m_tree->setSortingEnabled(false);
        // rebuild only when the set size changes a lot; else update in place.
        QHash<int, QTreeWidgetItem *> present;
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            auto *it = m_tree->topLevelItem(i);
            present.insert(it->text(0).toInt(), it);
        }
        QSet<int> seen;
        for (const Proc &p : procs) {
            double cpu = 0;
            if (m_prevTicks.contains(p.pid) && dTotal > 0)
                cpu = 100.0 * m_ncpu * (p.ticks - m_prevTicks[p.pid]) / dTotal;
            cpu = qBound(0.0, cpu, 100.0 * m_ncpu);
            sumCpu += cpu;
            m_prevTicks[p.pid] = p.ticks;
            seen.insert(p.pid);
            QTreeWidgetItem *it = present.value(p.pid);
            if (!it) {
                it = new ProcItem(m_tree);
                it->setText(0, QString::number(p.pid));
                it->setData(0, Qt::UserRole, p.pid);
                it->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
                it->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
            }
            it->setText(1, p.name);
            it->setText(2, QString::number(cpu, 'f', 1));
            it->setText(3, human(p.rssKib));
            it->setData(2, Qt::UserRole, cpu);       // numeric sort keys
            it->setData(3, Qt::UserRole, p.rssKib);
        }
        // remove gone pids
        for (auto i = present.constBegin(); i != present.constEnd(); ++i)
            if (!seen.contains(i.key()))
                delete i.value();
        m_tree->setSortingEnabled(true);
        m_count->setText(QStringLiteral("%1 procesos").arg(procs.size()));

        // --- perf graph ---
        double totalCpu = 0;
        const QVector<double> cores = perCoreUsage(m_prevIdle, m_prevCoreTot);
        for (double c : cores)
            totalCpu += c;
        if (!cores.isEmpty())
            totalCpu /= cores.size();
        m_graph->push(totalCpu);
        m_bars->set(cores);

        // --- memory ---
        double memTotal = 0, memAvail = 0;
        for (const QString &l : slurp(QStringLiteral("/proc/meminfo"))
                                    .split(QLatin1Char('\n'))) {
            if (l.startsWith(QStringLiteral("MemTotal:")))
                memTotal = l.split(QLatin1Char(' '), Qt::SkipEmptyParts)
                               .value(1).toDouble();
            else if (l.startsWith(QStringLiteral("MemAvailable:")))
                memAvail = l.split(QLatin1Char(' '), Qt::SkipEmptyParts)
                               .value(1).toDouble();
        }
        const double used = memTotal - memAvail;
        const double pct = memTotal > 0 ? 100.0 * used / memTotal : 0;
        if (m_mem)
            m_mem->setText(QStringLiteral("Memoria: %1 / %2  (%3%)")
                               .arg(human(used), human(memTotal))
                               .arg(pct, 0, 'f', 0));
        m_headStat->setText(QStringLiteral("CPU %1%  ·  RAM %2%")
                                .arg(totalCpu, 0, 'f', 0).arg(pct, 0, 'f', 0));
        m_prevTotal = total;
    }

    QColor m_accent;
    int m_ncpu = 1;
    QTabWidget *m_tabsUnused = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_count = nullptr, *m_mem = nullptr, *m_headStat = nullptr;
    Graph *m_graph = nullptr;
    CoreBars *m_bars = nullptr;
    QTimer *m_timer = nullptr;
    unsigned long long m_prevTotal = 0;
    QHash<int, unsigned long long> m_prevTicks;
    QVector<unsigned long long> m_prevIdle, m_prevCoreTot;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-monitor"));
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
    const QString accentStr =
        ThemeTokens::load(castalia::themeConfPath(repo, themeId))
            .str(QStringLiteral("colors"), QStringLiteral("accent"));
    castalia::applyTheme(&app, repo, themeId);
    const QColor accent(accentStr.isEmpty() ? QStringLiteral("#3E82B6")
                                            : accentStr);

    Monitor w(accent);
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(1200, &app, [&]() {  // 2 ticks → real CPU%
            w.grab().save(shot);
            app.quit();
        });
    return app.exec();
}
