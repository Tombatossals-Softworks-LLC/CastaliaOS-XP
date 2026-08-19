// castalia-calc — the Castalia calculator (Bible §9.3 "Calculator").
//
// Standard arithmetic with a clear themed keypad, keyboard entry, and a
// running expression line. Pure Qt5 + libcastalia-ui theming.
//
// Usage: castalia-calc --theme classic [--repo PATH] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "Theme.h"

class Calculator : public QWidget {
    Q_OBJECT
public:
    Calculator()
    {
        setWindowTitle(QStringLiteral("Calculadora — Castalia"));
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(12, 12, 12, 12);
        root->setSpacing(8);

        m_expr = new QLabel(this);
        m_expr->setObjectName(QStringLiteral("CalcExpr"));
        m_expr->setAlignment(Qt::AlignRight);
        m_expr->setProperty("secondary", true);
        m_expr->setMinimumHeight(16);
        root->addWidget(m_expr);

        m_display = new QLineEdit(QStringLiteral("0"), this);
        m_display->setObjectName(QStringLiteral("CalcDisplay"));
        m_display->setReadOnly(true);
        m_display->setAlignment(Qt::AlignRight);
        m_display->setStyleSheet(
            QStringLiteral("font-size:26px;padding:8px;"));
        root->addWidget(m_display);

        auto *grid = new QGridLayout;
        grid->setSpacing(6);
        const char *keys[5][4] = {
            {"C", "±", "%", "÷"},
            {"7", "8", "9", "×"},
            {"4", "5", "6", "−"},
            {"1", "2", "3", "+"},
            {"0", ".", "⌫", "="},
        };
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 4; ++c) {
                auto *b = new QPushButton(QString::fromUtf8(keys[r][c]), this);
                b->setMinimumSize(52, 44);
                b->setCursor(Qt::PointingHandCursor);
                const bool op = (c == 3) || (r == 0 && c < 3)
                                || QString::fromUtf8(keys[r][c])
                                       == QStringLiteral("=");
                b->setObjectName(op ? QStringLiteral("CalcOp")
                                    : QStringLiteral("CalcNum"));
                if (QString::fromUtf8(keys[r][c]) == QStringLiteral("="))
                    b->setObjectName(QStringLiteral("CalcEquals"));
                connect(b, &QPushButton::clicked, this,
                        [this, b]() { press(b->text()); });
                grid->addWidget(b, r, c);
            }
        root->addLayout(grid);
    }

protected:
    void keyPressEvent(QKeyEvent *e) override
    {
        const QString t = e->text();
        if (!t.isEmpty() && QStringLiteral("0123456789.+-*/%").contains(t)) {
            QString mapped = t;
            if (t == "*") mapped = QStringLiteral("×");
            else if (t == "/") mapped = QStringLiteral("÷");
            else if (t == "-") mapped = QStringLiteral("−");
            press(mapped);
        } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter
                   || e->key() == Qt::Key_Equal) {
            press(QStringLiteral("="));
        } else if (e->key() == Qt::Key_Backspace) {
            press(QStringLiteral("⌫"));
        } else if (e->key() == Qt::Key_Escape) {
            press(QStringLiteral("C"));
        }
    }

private:
    void press(const QString &k)
    {
        if (k == QStringLiteral("C")) { m_cur.clear(); m_acc = 0; m_op.clear();
            m_expr->clear(); showValue(QStringLiteral("0")); return; }
        if (k == QStringLiteral("⌫")) {
            m_cur.chop(1); showValue(m_cur.isEmpty() ? QStringLiteral("0") : m_cur);
            return; }
        if (k == QStringLiteral("±")) {
            if (m_cur.startsWith('-')) m_cur.remove(0, 1);
            else if (!m_cur.isEmpty()) m_cur.prepend('-');
            showValue(m_cur.isEmpty() ? QStringLiteral("0") : m_cur); return; }
        if (QStringLiteral("0123456789.").contains(k)) {
            if (k == "." && m_cur.contains('.')) return;
            m_cur += k; showValue(m_cur); return; }
        // an operator or equals: fold the pending operation
        const double x = m_cur.isEmpty() ? m_last : m_cur.toDouble();
        if (!m_op.isEmpty()) m_acc = fold(m_acc, x, m_op);
        else m_acc = x;
        m_last = x;
        if (k == QStringLiteral("=")) {
            m_expr->setText(QString());
            showValue(trim(m_acc));
            m_op.clear(); m_cur = trim(m_acc);
            m_justEval = true;
        } else {
            m_op = k;
            m_expr->setText(trim(m_acc) + QStringLiteral(" ") + k);
            m_cur.clear();
        }
    }
    double fold(double a, double b, const QString &op)
    {
        if (op == QStringLiteral("+")) return a + b;
        if (op == QStringLiteral("−")) return a - b;
        if (op == QStringLiteral("×")) return a * b;
        if (op == QStringLiteral("÷")) return b != 0 ? a / b : 0;
        if (op == QStringLiteral("%")) return a * b / 100.0;
        return b;
    }
    static QString trim(double v)
    {
        QString s = QString::number(v, 'g', 12);
        return s;
    }
    void showValue(const QString &s) { m_display->setText(s); }

    QLabel *m_expr = nullptr;
    QLineEdit *m_display = nullptr;
    QString m_cur, m_op;
    double m_acc = 0, m_last = 0;
    bool m_justEval = false;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-calc"));
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
    castalia::applyTheme(&app, repo, cli.value(QStringLiteral("theme")),
        QStringLiteral("#CalcDisplay{background:#FFFFFF;color:#1E1E1E;}"
                       "#CalcOp{font-weight:bold;}"));

    Calculator w;
    w.resize(260, 340);
    w.show();

    const QString shot = cli.value(QStringLiteral("screenshot"));
    if (!shot.isEmpty())
        QTimer::singleShot(120, &app, [&]() {
            w.grab().save(shot); app.quit();
        });
    return app.exec();
}

#include "main.moc"
