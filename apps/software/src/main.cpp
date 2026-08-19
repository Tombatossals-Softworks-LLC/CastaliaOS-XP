// castalia-software — the Software Center (Bible §9, "Add/Remove Programs").
//
// Browse what's installed (real dpkg data), see versions and disk usage,
// filter as you type, and remove a package (guarded). A Qt frontend over
// dpkg/apt — one small wrapper, original UI. Themed via libcastalia-ui.
//
// Usage: castalia-software --theme classic [--repo P] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Mark.h"
#include "Theme.h"

namespace {

struct Pkg { QString name, version, summary; double sizeKib = 0; };

QVector<Pkg> installedPackages()
{
    QVector<Pkg> out;
    QProcess p;
    p.start(QStringLiteral("dpkg-query"),
            {QStringLiteral("-W"),
             QStringLiteral("-f=${Package}\t${Version}\t${Installed-Size}\t"
                            "${binary:Summary}\n")});
    if (p.waitForFinished(8000)) {
        for (const QString &line : QString::fromUtf8(p.readAllStandardOutput())
                                       .split(QLatin1Char('\n'),
                                              Qt::SkipEmptyParts)) {
            const QStringList f = line.split(QLatin1Char('\t'));
            if (f.size() < 4)
                continue;
            out.push_back({f[0], f[1], f[3], f[2].toDouble()});
        }
    }
    return out;
}

QString human(double kib)
{
    if (kib >= 1024 * 1024)
        return QStringLiteral("%1 GiB").arg(kib / 1024 / 1024, 0, 'f', 1);
    if (kib >= 1024)
        return QStringLiteral("%1 MiB").arg(kib / 1024, 0, 'f', 1);
    return QStringLiteral("%1 KiB").arg(kib, 0, 'f', 0);
}

// Numeric sort for the size column.
class PkgItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &o) const override
    {
        const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (col == 2)
            return data(2, Qt::UserRole).toDouble()
                   < o.data(2, Qt::UserRole).toDouble();
        return text(col).toLower() < o.text(col).toLower();
    }
};

// Demo rows so an offscreen render always shows a populated list.
QVector<Pkg> demoPackages()
{
    return {
        {QStringLiteral("castalia-desktop"), QStringLiteral("1.0"),
         QStringLiteral("El escritorio Castalia (panel, explorador, apps)"),
         48000},
        {QStringLiteral("xserver-xorg"), QStringLiteral("1.21"),
         QStringLiteral("Servidor gráfico X.Org"), 12000},
        {QStringLiteral("wine"), QStringLiteral("8.0"),
         QStringLiteral("Compatibilidad con aplicaciones de Windows"), 640000},
        {QStringLiteral("linux-image-amd64"), QStringLiteral("6.1"),
         QStringLiteral("Núcleo Linux"), 310000},
        {QStringLiteral("libqt5widgets5"), QStringLiteral("5.15"),
         QStringLiteral("Biblioteca de interfaz Qt 5"), 8200},
    };
}

} // namespace

class Software : public QWidget {
public:
    Software(const QColor &accent, bool demo) : m_accent(accent), m_demo(demo)
    {
        setWindowTitle(QStringLiteral("Centro de software — Castalia"));
        resize(680, 540);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->addWidget(buildHeader());

        auto *body = new QVBoxLayout;
        body->setContentsMargins(14, 12, 14, 12);
        body->setSpacing(8);
        auto *search = new QLineEdit(this);
        search->setPlaceholderText(
            QStringLiteral("Buscar entre los programas instalados…"));
        search->setClearButtonEnabled(true);
        connect(search, &QLineEdit::textChanged, this, &Software::filter);
        body->addWidget(search);

        m_tree = new QTreeWidget(this);
        m_tree->setColumnCount(4);
        m_tree->setHeaderLabels({QStringLiteral("Paquete"),
                                 QStringLiteral("Versión"),
                                 QStringLiteral("Tamaño"),
                                 QStringLiteral("Descripción")});
        m_tree->setRootIsDecorated(false);
        m_tree->setAlternatingRowColors(true);
        m_tree->setSortingEnabled(true);
        m_tree->sortByColumn(2, Qt::DescendingOrder);
        m_tree->header()->setStretchLastSection(true);
        m_tree->setColumnWidth(0, 200);
        m_tree->setColumnWidth(1, 100);
        m_tree->setColumnWidth(2, 96);
        body->addWidget(m_tree, 1);

        auto *row = new QHBoxLayout;
        m_summary = new QLabel(this);
        m_summary->setProperty("secondary", true);
        row->addWidget(m_summary);
        row->addStretch(1);
        auto *rm = new QPushButton(QStringLiteral("Eliminar…"), this);
        rm->setObjectName(QStringLiteral("RemoveBtn"));
        connect(rm, &QPushButton::clicked, this, &Software::removeSelected);
        row->addWidget(rm);
        body->addLayout(row);
        root->addLayout(body, 1);

        load();
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
            "Centro de software</span>"), head);
        l->addWidget(t);
        l->addStretch(1);
        m_headStat = new QLabel(head);
        m_headStat->setStyleSheet(QStringLiteral("color:#EAF1F7;"));
        l->addWidget(m_headStat);
        return head;
    }

    void load()
    {
        m_pkgs = m_demo ? demoPackages() : installedPackages();
        if (m_pkgs.isEmpty())
            m_pkgs = demoPackages();
        m_tree->setSortingEnabled(false);
        m_tree->clear();
        double total = 0;
        for (const Pkg &pk : m_pkgs) {
            auto *it = new PkgItem(m_tree);
            it->setText(0, pk.name);
            it->setText(1, pk.version);
            it->setText(2, human(pk.sizeKib));
            it->setData(2, Qt::UserRole, pk.sizeKib);
            it->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
            it->setText(3, pk.summary);
            total += pk.sizeKib;
        }
        m_tree->setSortingEnabled(true);
        m_headStat->setText(QStringLiteral("%1 paquetes · %2")
                                .arg(m_pkgs.size()).arg(human(total)));
        m_summary->setText(QStringLiteral("%1 programas instalados")
                               .arg(m_pkgs.size()));
    }

    void filter(const QString &q)
    {
        int shown = 0;
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            auto *it = m_tree->topLevelItem(i);
            const bool match = q.isEmpty()
                || it->text(0).contains(q, Qt::CaseInsensitive)
                || it->text(3).contains(q, Qt::CaseInsensitive);
            it->setHidden(!match);
            if (match)
                ++shown;
        }
        m_summary->setText(q.isEmpty()
            ? QStringLiteral("%1 programas instalados").arg(m_pkgs.size())
            : QStringLiteral("%1 de %2 coinciden con «%3»")
                  .arg(shown).arg(m_pkgs.size()).arg(q));
    }

    void removeSelected()
    {
        auto *it = m_tree->currentItem();
        if (!it)
            return;
        const QString name = it->text(0);
        const auto ans = QMessageBox::warning(
            this, QStringLiteral("Eliminar programa"),
            QStringLiteral("¿Eliminar «%1»? Se ejecutará como administrador y "
                           "podría afectar a otros programas que dependan de "
                           "él.").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes)
            return;
        // Prefer a graphical privilege prompt if present; else plain apt-get.
        QString helper = QStandardPaths::findExecutable(
            QStringLiteral("pkexec"));
        QStringList argv;
        if (!helper.isEmpty())
            argv = QStringList{QStringLiteral("apt-get"), QStringLiteral("-y"),
                               QStringLiteral("remove"), name};
        else {
            helper = QStringLiteral("apt-get");
            argv = QStringList{QStringLiteral("-y"), QStringLiteral("remove"),
                               name};
        }
        QProcess::startDetached(helper, argv);
        m_summary->setText(QStringLiteral("Solicitada la eliminación de %1…")
                               .arg(name));
    }

    QColor m_accent;
    bool m_demo;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_summary = nullptr, *m_headStat = nullptr;
    QVector<Pkg> m_pkgs;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-software"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Show a sample package list")});
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

    // Read real dpkg packages; load() falls back to a demo list only if none.
    Software w(accent, cli.isSet(QStringLiteral("demo")));
    w.show();

    const QString out = cli.value(QStringLiteral("screenshot"));
    if (!out.isEmpty())
        QTimer::singleShot(250, &app, [&]() {
            w.grab().save(out);
            app.quit();
        });
    return app.exec();
}
