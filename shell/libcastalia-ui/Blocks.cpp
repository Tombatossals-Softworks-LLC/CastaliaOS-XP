#include "Blocks.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace castalia {
namespace blocks {

namespace {

Block fromJson(const QJsonObject &o)
{
    Block b;
    b.name = o.value(QStringLiteral("name")).toString();
    b.path = o.value(QStringLiteral("path")).toString();
    if (b.path.isEmpty() && !b.name.isEmpty())
        b.path = QStringLiteral("/dev/%1").arg(b.name);
    b.type = o.value(QStringLiteral("type")).toString();
    b.fstype = o.value(QStringLiteral("fstype")).toString();
    b.label = o.value(QStringLiteral("label")).toString();
    b.mountpoint = o.value(QStringLiteral("mountpoint")).toString();
    b.model = o.value(QStringLiteral("model")).toString().trimmed();
    b.transport = o.value(QStringLiteral("tran")).toString();
    // -b asks for bytes, but older util-linux hands the number back as a
    // string, and some builds still answer "8G" when -b is ignored.
    const QJsonValue size = o.value(QStringLiteral("size"));
    b.size = size.isString() ? size.toString().toLongLong()
                             : qint64(size.toDouble());
    auto flag = [&o](const char *key) {
        const QJsonValue v = o.value(QLatin1String(key));
        return v.isString() ? (v.toString() == QStringLiteral("1")
                               || v.toString() == QStringLiteral("true"))
                            : v.toBool();
    };
    b.removable = flag("rm");
    b.readOnly = flag("ro");
    for (const QJsonValue &child :
         o.value(QStringLiteral("children")).toArray())
        b.children.append(fromJson(child.toObject()));
    return b;
}

} // namespace

QVector<Block> parseLsblk(const QByteArray &json)
{
    QVector<Block> out;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return out;
    for (const QJsonValue &v :
         doc.object().value(QStringLiteral("blockdevices")).toArray())
        out.append(fromJson(v.toObject()));
    return out;
}

QVector<Block> list()
{
    const QString lsblk =
        QStandardPaths::findExecutable(QStringLiteral("lsblk"));
    if (lsblk.isEmpty())
        return {};
    QProcess p;
    p.start(lsblk, {QStringLiteral("-J"), QStringLiteral("-b"),
                    QStringLiteral("-o"),
                    QStringLiteral("NAME,PATH,SIZE,TYPE,FSTYPE,LABEL,"
                                   "MOUNTPOINT,RM,RO,MODEL,TRAN")});
    if (!p.waitForFinished(5000))
        return {};
    return parseLsblk(p.readAllStandardOutput());
}

QVector<Block> partitions(const QVector<Block> &disks)
{
    QVector<Block> out;
    for (const Block &disk : disks) {
        if (disk.type == QStringLiteral("part"))
            out.append(disk);
        out += partitions(disk.children);
    }
    return out;
}

QString humanBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("—");
    const char *unit[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = double(bytes);
    int i = 0;
    while (value >= 1024.0 && i < 4) {
        value /= 1024.0;
        ++i;
    }
    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', i >= 3 ? 1 : 0)
        .arg(QLatin1String(unit[i]));
}

} // namespace blocks
} // namespace castalia
