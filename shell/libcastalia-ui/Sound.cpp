#include "Sound.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

namespace castalia {
namespace {

// The players we know how to drive, in preference order. Each takes the WAV
// path as its final argument, so one code path covers all of them.
struct Player {
    const char *bin;
    const char *arg;   // optional flag before the file, or nullptr
};
const Player kPlayers[] = {
    {"paplay", nullptr},    // PulseAudio / PipeWire's pulse shim
    {"pw-play", nullptr},   // PipeWire native
    {"aplay", "-q"},        // ALSA
};

// The first player present on this machine, or an empty string.
QString findPlayer(QString *flag)
{
    for (const Player &p : kPlayers) {
        const QString path = QStandardPaths::findExecutable(
            QString::fromLatin1(p.bin));
        if (!path.isEmpty()) {
            if (flag)
                *flag = p.arg ? QString::fromLatin1(p.arg) : QString();
            return path;
        }
    }
    return QString();
}

// ~/.config/castalia/sound.conf — "enabled = false" turns the scheme off.
// Absent or unreadable means sounds are on, matching a fresh install.
bool userWantsSound()
{
    const QString home = qEnvironmentVariable("HOME");
    if (home.isEmpty())
        return true;
    QFile f(home + QStringLiteral("/.config/castalia/sound.conf"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return true;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('#')))
            continue;
        if (!line.startsWith(QStringLiteral("enabled")))
            continue;
        const QString value = line.section(QLatin1Char('='), 1).trimmed()
                                  .remove(QLatin1Char('"')).toLower();
        return !(value == QStringLiteral("false")
                 || value == QStringLiteral("0")
                 || value == QStringLiteral("no"));
    }
    return true;
}

} // namespace

QString soundId(Sound sound)
{
    switch (sound) {
    case Sound::Startup:    return QStringLiteral("startup");
    case Sound::Shutdown:   return QStringLiteral("shutdown");
    case Sound::Notify:     return QStringLiteral("notify");
    case Sound::Error:      return QStringLiteral("error");
    case Sound::DeviceIn:   return QStringLiteral("device-in");
    case Sound::DeviceOut:  return QStringLiteral("device-out");
    case Sound::EmptyTrash: return QStringLiteral("empty-trash");
    }
    return QString();
}

QString soundPath(const QString &repoRoot, Sound sound)
{
    return QDir(repoRoot).filePath(
        QStringLiteral("branding/sound/wav/%1.wav").arg(soundId(sound)));
}

bool soundsEnabled()
{
    if (qEnvironmentVariable("CASTALIA_NO_SOUND") == QStringLiteral("1"))
        return false;
    // The offscreen platform is the CI render path: never spawn audio there.
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QStringLiteral("offscreen"))
        return false;
    if (!userWantsSound())
        return false;
    return !findPlayer(nullptr).isEmpty();
}

bool playSound(const QString &repoRoot, Sound sound)
{
    if (!soundsEnabled())
        return false;
    const QString wav = soundPath(repoRoot, sound);
    if (!QFileInfo::exists(wav))
        return false;
    QString flag;
    const QString player = findPlayer(&flag);
    if (player.isEmpty())
        return false;
    QStringList args;
    if (!flag.isEmpty())
        args << flag;
    args << wav;
    // Detached: the sound outlives the call and nothing ever waits on it.
    return QProcess::startDetached(player, args);
}

} // namespace castalia
