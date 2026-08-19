// libcastalia-ui self-test — a head-less gate for the shared foundation every
// first-party app links (Bible §12.3, §17.4). No framework: plain assertions
// and an exit code, run in CI like the game self-tests.
//
//   castalia-ui-selftest [REPO]        # REPO defaults to "."

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "Aurora.h"
#include "Locale.h"
#include "Recent.h"
#include "Sound.h"
#include "Blocks.h"
#include "Theme.h"
#include "ThemeTokens.h"

namespace {

int g_fail = 0;
void check(bool ok, const char *what)
{
    if (!ok) {
        ++g_fail;
        qWarning("selftest FAIL: %s", what);
    }
}

// Write `contents` to <dir>/.config/castalia/theme.conf and point HOME at dir.
void writeThemeConfig(const QString &home, const QString &contents)
{
    const QString cfgDir = home + QStringLiteral("/.config/castalia");
    QDir().mkpath(cfgDir);
    QFile f(cfgDir + QStringLiteral("/theme.conf"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        QTextStream(&f) << contents;
    qputenv("HOME", home.toUtf8());
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString repo = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                  : QStringLiteral(".");

    // 1) availableThemes discovers the bundled themes in display order.
    {
        const QStringList ids = castalia::availableThemes(repo);
        check(ids.contains(QStringLiteral("human")), "human present");
        check(ids.contains(QStringLiteral("classic")), "classic present");
        check(ids.contains(QStringLiteral("medianoche")), "medianoche present");
        check(ids.contains(QStringLiteral("high-contrast")),
              "high-contrast present");
        check(ids.size() >= 7, "at least seven themes");
        // Human is the flagship and sorts first; classic follows; and
        // high-contrast is last of the known set.
        check(!ids.isEmpty() && ids.first() == QStringLiteral("human"),
              "human sorts first");
        check(ids.indexOf(QStringLiteral("human"))
                  < ids.indexOf(QStringLiteral("classic")),
              "human ranks before classic");
        check(ids.indexOf(QStringLiteral("medianoche"))
                  < ids.indexOf(QStringLiteral("high-contrast")),
              "medianoche ranks before high-contrast");
    }

    // 2) themeConfPath / themeQssPath build the expected locations.
    {
        const QString cp = castalia::themeConfPath(repo, QStringLiteral("azul"));
        check(cp.endsWith(QStringLiteral("themes/azul/theme.conf")),
              "themeConfPath layout");
        const QString qp = castalia::themeQssPath(repo, QStringLiteral("azul"));
        check(qp.endsWith(
                  QStringLiteral("build/out/themes/azul/castalia.qss")),
              "themeQssPath layout");
    }

    // The block layer (§9): one parser for the Disk Manager and the
    // Migration Assistant. util-linux has shipped `size` as a number and as
    // a string and `rm` as a bool and as "0" — all of it in the wild, all of
    // it here, because a disk list must not be lost to a type change.
    {
        const QByteArray json = R"({
          "blockdevices": [
            {"name":"sda","path":"/dev/sda","size":160041885696,"type":"disk",
             "rm":false,"ro":false,"model":"ST3160815AS","tran":"sata",
             "children":[
               {"name":"sda1","path":"/dev/sda1","size":"155041885696",
                "type":"part","fstype":"ext4","label":"castalia",
                "mountpoint":"/","rm":"0"},
               {"name":"sda2","size":5000000000,"type":"part",
                "fstype":"ntfs","mountpoint":null}]},
            {"name":"sdb","path":"/dev/sdb","size":8004304896,"type":"disk",
             "rm":true,"tran":"usb","children":[
               {"name":"sdb1","path":"/dev/sdb1","size":8004304896,
                "type":"part","fstype":"vfat","label":"COPIAS","rm":true}]}]
        })";
        const auto disks = castalia::blocks::parseLsblk(json);
        check(disks.size() == 2, "both disks are read");
        check(disks.at(0).children.size() == 2,
              "partitions come with their disk");
        check(disks.at(0).children.at(0).size == 155041885696LL,
              "a size given as a string is still a number");
        check(disks.at(0).children.at(0).mountpoint == QStringLiteral("/"),
              "the mount point is read");
        check(disks.at(0).children.at(1).path == QStringLiteral("/dev/sda2"),
              "a missing path is derived from the name");
        check(disks.at(1).removable, "rm:true marks the stick removable");
        check(!disks.at(0).removable, "the internal disk is not removable");
        check(castalia::blocks::parseLsblk("not json").isEmpty(),
              "garbage from lsblk is no disks, not a crash");
        check(castalia::blocks::parseLsblk(QByteArray()).isEmpty(),
              "no output is no disks");

        const auto parts = castalia::blocks::partitions(disks);
        check(parts.size() == 3, "every partition is flattened out");
        check(parts.at(0).name == QStringLiteral("sda1"),
              "…in the order the disks list them");

        check(castalia::blocks::humanBytes(160041885696LL)
                  == QStringLiteral("149.1 GiB"),
              "a 160 GB disk reads as its real capacity");
        check(castalia::blocks::humanBytes(0) == QStringLiteral("—"),
              "an unknown size reads as a dash, never 0 B");
    }

    // isLiveSession decides whether the desktop offers to install itself
    // (§14.5). Getting it wrong either way is bad: an installed system with
    // an "Instalar Castalia OS" icon, or a live image with no way to install.
    {
        const QByteArray saved = qgetenv("CASTALIA_LIVE");
        qputenv("CASTALIA_LIVE", "1");
        check(castalia::isLiveSession(), "the live launcher marks the session");
        qunsetenv("CASTALIA_LIVE");
        // /run/live/medium is live-boot's own mountpoint; on a build host it
        // does not exist, so an unmarked session is not live.
        check(!castalia::isLiveSession(),
              "an ordinary session is not a live one");
        if (!saved.isEmpty())
            qputenv("CASTALIA_LIVE", saved);
    }

    // 3) activeThemeId resolves ~/.config/castalia/theme.conf, with fallback.
    {
        QTemporaryDir home;
        check(home.isValid(), "temp HOME created");
        // no config yet -> fallback
        qputenv("HOME", home.path().toUtf8());
        check(castalia::activeThemeId(QStringLiteral("classic"))
                  == QStringLiteral("classic"),
              "activeThemeId falls back with no config");
        // the exact format the Control Center writes
        writeThemeConfig(home.path(),
                         QStringLiteral("# comment\n[meta]\n"
                                        "id = \"medianoche\"\n"));
        check(castalia::activeThemeId(QStringLiteral("classic"))
                  == QStringLiteral("medianoche"),
              "activeThemeId reads the persisted id");
        // a different id, no surrounding quotes-in-value edge cases
        writeThemeConfig(home.path(),
                         QStringLiteral("[meta]\nid = \"oliva\"\n"));
        check(castalia::activeThemeId(QStringLiteral("classic"))
                  == QStringLiteral("oliva"),
              "activeThemeId reflects a changed id");
    }

    // 4) ThemeTokens loads real values and reports validity.
    {
        const ThemeTokens t = ThemeTokens::load(
            castalia::themeConfPath(repo, QStringLiteral("medianoche")));
        check(t.themeId() == QStringLiteral("medianoche"),
              "medianoche tokens: id");
        check(t.color(QStringLiteral("surface")).isValid(),
              "medianoche tokens: surface colour parses");
        check(t.num(QStringLiteral("metrics"),
                    QStringLiteral("panel_height"), 0) > 0,
              "medianoche tokens: panel_height");
    }

    // 5) The sound scheme: ids map to real rendered WAVs, and the kill
    //    switch really silences playback (§21.4).
    {
        check(castalia::soundId(castalia::Sound::Startup)
                  == QStringLiteral("startup"),
              "soundId: startup");
        check(castalia::soundId(castalia::Sound::EmptyTrash)
                  == QStringLiteral("empty-trash"),
              "soundId: empty-trash");

        const QString wav = castalia::soundPath(repo,
                                                castalia::Sound::Startup);
        check(wav.endsWith(QStringLiteral("branding/sound/wav/startup.wav")),
              "soundPath: points into the sound palette");
        check(QFile::exists(wav), "soundPath: the rendered WAV exists");

        // Every enum member must resolve to a file that is really shipped.
        const castalia::Sound all[] = {
            castalia::Sound::Startup,   castalia::Sound::Shutdown,
            castalia::Sound::Notify,    castalia::Sound::Error,
            castalia::Sound::DeviceIn,  castalia::Sound::DeviceOut,
            castalia::Sound::EmptyTrash,
        };
        bool everyWavPresent = true;
        for (const castalia::Sound s : all) {
            if (castalia::soundId(s).isEmpty()
                || !QFile::exists(castalia::soundPath(repo, s)))
                everyWavPresent = false;
        }
        check(everyWavPresent, "every Sound enum member has a rendered WAV");

        const QByteArray prevNoSound = qgetenv("CASTALIA_NO_SOUND");
        qputenv("CASTALIA_NO_SOUND", "1");
        check(!castalia::soundsEnabled(),
              "CASTALIA_NO_SOUND=1 disables the sound scheme");
        check(!castalia::playSound(repo, castalia::Sound::Startup),
              "playSound is a no-op while sound is disabled");
        if (prevNoSound.isEmpty())
            qunsetenv("CASTALIA_NO_SOUND");
        else
            qputenv("CASTALIA_NO_SOUND", prevNoSound);
    }

    // 6) The Konami detector behind the desktop's aurora (the state machine
    //    only — the painting is exercised by the render gate).
    {
        const QVector<int> seq = castalia::KonamiDetector::sequence();
        check(seq.size() == 10, "konami: ten keys");

        castalia::KonamiDetector k;
        bool fired = false;
        for (int i = 0; i < seq.size(); ++i)
            fired = k.feed(seq.at(i));
        check(fired, "konami: the full sequence fires");
        check(k.progress() == 0, "konami: rearms after firing");

        // A wrong key mid-run cancels it…
        castalia::KonamiDetector broken;
        broken.feed(seq.at(0));
        broken.feed(seq.at(1));
        broken.feed(0x58 /* X */);
        check(broken.progress() == 0, "konami: a wrong key resets the run");

        // …but a wrong key that is itself the opening keeps that credit, so
        // ↑ ↑ ↑ ↓ ↓ … still works (the third ↑ restarts at one, not zero).
        castalia::KonamiDetector extra;
        for (const int key : {seq.at(0), seq.at(0), seq.at(0)})
            extra.feed(key);
        check(extra.progress() == 1,
              "konami: a repeated opening key restarts at one");
        bool extraFired = false;
        for (int i = 1; i < seq.size(); ++i)
            extraFired = extra.feed(seq.at(i));
        check(extraFired, "konami: fires after the stutter");

        // Nothing but the sequence fires it.
        castalia::KonamiDetector noise;
        bool noiseFired = false;
        for (const int key : {0x41, 0x42, 0x43, 0x01000013, 0x01000015})
            noiseFired = noise.feed(key) || noiseFired;
        check(!noiseFired, "konami: noise never fires");
    }

    // 7) The recent-documents store (§7.9): the freedesktop XBEL file every
    //    desktop app on the machine shares.
    {
        QTemporaryDir home;
        check(home.isValid(), "temp HOME for the recent store");
        qputenv("HOME", home.path().toUtf8());
        qputenv("XDG_DATA_HOME", (home.path() + "/.local/share").toUtf8());

        check(castalia::recent::storePath().endsWith(
                  QStringLiteral("recently-used.xbel")),
              "recent: the store is the freedesktop file");
        check(castalia::recent::list().isEmpty(),
              "recent: an absent store reads as empty");

        // Real files, because add() refuses paths that do not exist — a
        // recent list that offers dead paths is worse than a short one.
        auto touch = [&home](const QString &name) {
            const QString path = home.path() + QLatin1Char('/') + name;
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write("x");
            return path;
        };
        const QString a = touch(QStringLiteral("informe.txt"));
        const QString b = touch(QStringLiteral("hoja & nota.txt"));

        check(!castalia::recent::add(home.path() + QStringLiteral("/nope.txt"),
                                     QStringLiteral("Notas")),
              "recent: a missing file is not recorded");
        check(!castalia::recent::add(home.path(), QStringLiteral("Notas")),
              "recent: a folder is not a document");
        check(castalia::recent::add(a, QStringLiteral("Notas")),
              "recent: add writes");
        check(castalia::recent::add(b, QStringLiteral("Escritor")),
              "recent: add a second");

        QVector<castalia::recent::Entry> got = castalia::recent::list();
        check(got.size() == 2, "recent: both entries come back");
        check(got.first().path == b, "recent: newest first");
        check(got.first().appName == QStringLiteral("Escritor"),
              "recent: the recorder's name survives the round trip");
        check(!got.first().mime.isEmpty(),
              "recent: a MIME type is derived when not supplied");
        // '&' in a file name is the classic XML escaping bug: if the writer
        // did not escape it, the reader loses the entry entirely.
        check(got.first().path.contains(QLatin1Char('&')),
              "recent: an ampersand in a name survives");

        check(castalia::recent::add(a, QStringLiteral("Notas")),
              "recent: re-adding works");
        got = castalia::recent::list();
        check(got.size() == 2, "recent: re-opening moves, never duplicates");
        check(got.first().path == a, "recent: the re-opened file leads");

        check(castalia::recent::cap() > 0, "recent: the store is capped");
        check(castalia::recent::clear(), "recent: clear works");
        check(castalia::recent::list().isEmpty(), "recent: clear really clears");
        check(castalia::recent::clear(), "recent: clearing twice is not a fail");
    }

    // N) The interface language. Every rule in locale::resolve() is a product
    // decision (§7.13), so each one is pinned here rather than left to the
    // reader of the implementation.
    {
        using namespace castalia::locale;

        check(available().contains(QStringLiteral("es")), "locale: es ships");
        check(available().contains(QStringLiteral("en")), "locale: en ships");
        check(available().first() == QStringLiteral("es"),
              "locale: Spanish is the first language offered");
        check(displayName(QStringLiteral("en")) == QStringLiteral("English"),
              "locale: a language is named in its own language");
        check(displayName(QStringLiteral("zz")) == QStringLiteral("zz"),
              "locale: an unknown code names itself rather than throwing");

        // Nothing configured is Spanish, whatever the machine says: an
        // English system still boots Castalia in Spanish (§8).
        check(resolve(QString(), QStringLiteral("en_US.UTF-8"))
                  == QStringLiteral("es"),
              "locale: unconfigured is Spanish even on an English system");
        check(resolve(QStringLiteral("  "), QStringLiteral("en"))
                  == QStringLiteral("es"),
              "locale: blank is Spanish");
        check(resolve(QStringLiteral("en"), QStringLiteral("es"))
                  == QStringLiteral("en"),
              "locale: an explicit choice beats the environment");
        check(resolve(QStringLiteral("EN_gb"), QString())
                  == QStringLiteral("en"),
              "locale: a full locale name resolves to its language");
        check(resolve(QStringLiteral("fr"), QStringLiteral("en"))
                  == QStringLiteral("es"),
              "locale: a language we do not ship falls back to Spanish");

        // "auto" is the only value that reads the environment.
        check(resolve(QStringLiteral("auto"), QStringLiteral("en_GB.UTF-8"))
                  == QStringLiteral("en"),
              "locale: auto follows the system when we ship it");
        check(resolve(QStringLiteral("auto"), QStringLiteral("de_DE.UTF-8"))
                  == QStringLiteral("es"),
              "locale: auto falls back to Spanish for a language we lack");
        check(resolve(QStringLiteral("auto"), QString())
                  == QStringLiteral("es"),
              "locale: auto with no environment is Spanish");

        // The config file, and the precedence between the two locations.
        {
            QTemporaryDir home;
            const QString cfgDir = home.path()
                                   + QStringLiteral("/.config/castalia");
            QDir().mkpath(cfgDir);
            const QByteArray oldHome = qgetenv("HOME");
            qputenv("HOME", home.path().toUtf8());
            check(configured().isEmpty(),
                  "locale: no config file means no choice recorded");
            QFile f(cfgDir + QStringLiteral("/locale.conf"));
            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                QTextStream(&f) << "# elegido en el Centro de control\n"
                                   "[locale]\nlanguage = \"en\"\n";
            f.close();
            check(configured() == QStringLiteral("en"),
                  "locale: the config file is read, quotes and all");
            qputenv("HOME", oldHome);
        }

        // Spanish never loads a catalogue: it is the source language, so a
        // missing castalia_es.qm is correct, not a failure.
        check(!apply(&app, repo, QStringLiteral("es")),
              "locale: Spanish installs no translator");
        check(!apply(nullptr, repo, QStringLiteral("en")),
              "locale: no application, nothing to install");
    }

    if (g_fail == 0)
        qInfo("libcastalia-ui selftest: all checks passed");
    return g_fail == 0 ? 0 : 1;
}
