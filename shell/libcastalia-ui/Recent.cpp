#include "Recent.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace castalia {
namespace recent {

namespace {

const int kCap = 50;

// The XBEL namespaces the freedesktop spec pins. They are written back
// verbatim so a GTK/Qt app reading the file after us finds what it expects.
const char *kBookmarkNs = "http://www.freedesktop.org/standards/desktop-bookmarks";
const char *kMimeNs = "http://www.freedesktop.org/standards/shared-mime-info";

QString isoNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

// Read the whole store. Unknown elements are skipped rather than dropped from
// the file: we only rewrite entries we understand, but we never claim to be
// the only writer.
QVector<Entry> readAll()
{
    QVector<Entry> out;
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;

    QXmlStreamReader xml(&f);
    Entry current;
    bool inBookmark = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("bookmark")) {
            current = Entry();
            inBookmark = true;
            const QString href =
                xml.attributes().value(QLatin1String("href")).toString();
            current.path = QUrl(href).toLocalFile();
            const QString visited =
                xml.attributes().value(QLatin1String("visited")).toString();
            current.visited = QDateTime::fromString(visited, Qt::ISODate);
            if (!current.visited.isValid()) {
                const QString modified =
                    xml.attributes().value(QLatin1String("modified")).toString();
                current.visited = QDateTime::fromString(modified, Qt::ISODate);
            }
        } else if (inBookmark && xml.isStartElement()
                   && xml.name() == QLatin1String("mime-type")) {
            current.mime =
                xml.attributes().value(QLatin1String("type")).toString();
        } else if (inBookmark && xml.isStartElement()
                   && xml.name() == QLatin1String("application")) {
            if (current.appName.isEmpty())
                current.appName =
                    xml.attributes().value(QLatin1String("name")).toString();
        } else if (xml.isEndElement()
                   && xml.name() == QLatin1String("bookmark")) {
            inBookmark = false;
            if (!current.path.isEmpty())
                out.append(current);
        }
    }
    // Newest first. The file itself is written in that order, but another
    // writer's is not guaranteed to be, and a wrong order here would show the
    // user their oldest documents as their newest.
    std::stable_sort(out.begin(), out.end(),
                     [](const Entry &a, const Entry &b) {
                         return a.visited > b.visited;
                     });
    return out;
}

bool writeAll(const QVector<Entry> &entries)
{
    const QString path = storePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    // QSaveFile: the recent list is rewritten in full on every open, and a
    // half-written XBEL would break every desktop app that reads it, not just
    // ours.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QXmlStreamWriter xml(&f);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("xbel"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    xml.writeNamespace(QLatin1String(kBookmarkNs), QStringLiteral("bookmark"));
    xml.writeNamespace(QLatin1String(kMimeNs), QStringLiteral("mime"));

    for (const Entry &e : entries) {
        const QString when = e.visited.isValid()
            ? e.visited.toUTC().toString(Qt::ISODate) : isoNow();
        xml.writeStartElement(QStringLiteral("bookmark"));
        xml.writeAttribute(QStringLiteral("href"),
                           QUrl::fromLocalFile(e.path).toString());
        xml.writeAttribute(QStringLiteral("added"), when);
        xml.writeAttribute(QStringLiteral("modified"), when);
        xml.writeAttribute(QStringLiteral("visited"), when);
        xml.writeStartElement(QStringLiteral("info"));
        xml.writeStartElement(QStringLiteral("metadata"));
        xml.writeAttribute(QStringLiteral("owner"),
                           QStringLiteral("http://freedesktop.org"));
        if (!e.mime.isEmpty()) {
            xml.writeStartElement(QLatin1String(kMimeNs),
                                  QStringLiteral("mime-type"));
            xml.writeAttribute(QStringLiteral("type"), e.mime);
            xml.writeEndElement();
        }
        xml.writeStartElement(QLatin1String(kBookmarkNs),
                              QStringLiteral("applications"));
        xml.writeStartElement(QLatin1String(kBookmarkNs),
                              QStringLiteral("application"));
        xml.writeAttribute(QStringLiteral("name"),
                           e.appName.isEmpty() ? QStringLiteral("Castalia")
                                               : e.appName);
        xml.writeAttribute(QStringLiteral("exec"), QStringLiteral("&apos;"));
        xml.writeAttribute(QStringLiteral("modified"), when);
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("1"));
        xml.writeEndElement();   // application
        xml.writeEndElement();   // applications
        xml.writeEndElement();   // metadata
        xml.writeEndElement();   // info
        xml.writeEndElement();   // bookmark
    }
    xml.writeEndElement();       // xbel
    xml.writeEndDocument();
    return f.commit();
}

} // namespace

QString storePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/recently-used.xbel");
}

int cap()
{
    return kCap;
}

QVector<Entry> list(int max)
{
    QVector<Entry> out;
    for (const Entry &e : readAll()) {
        if (!QFileInfo::exists(e.path))
            continue;               // the file is gone; do not offer it
        out.append(e);
        if (max > 0 && out.size() >= max)
            break;
    }
    return out;
}

bool add(const QString &path, const QString &appName, const QString &mime)
{
    const QFileInfo fi(path);
    if (path.isEmpty() || !fi.exists() || fi.isDir())
        return false;               // recent *documents*, not folders

    Entry entry;
    entry.path = fi.absoluteFilePath();
    entry.appName = appName;
    entry.mime = mime.isEmpty()
        ? QMimeDatabase().mimeTypeForFile(fi).name() : mime;
    entry.visited = QDateTime::currentDateTimeUtc();

    QVector<Entry> all = readAll();
    for (int i = all.size() - 1; i >= 0; --i)
        if (all.at(i).path == entry.path)
            all.removeAt(i);        // re-opening moves it up, never duplicates
    all.prepend(entry);
    while (all.size() > kCap)
        all.removeLast();
    return writeAll(all);
}

bool clear()
{
    const QString path = storePath();
    if (!QFile::exists(path))
        return true;
    return QFile::remove(path);
}

} // namespace recent
} // namespace castalia
