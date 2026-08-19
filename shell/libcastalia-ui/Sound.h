// libcastalia-ui — the system sound scheme (Bible §21.4, §8.6).
//
// The sounds themselves are original compositions rendered deterministically
// from branding/sound/palette.toml by tools/sound_gen.py. This is the thin
// playback side: which sound belongs to which event, whether sound is wanted
// at all, and how to hand a WAV to whatever audio stack the machine has.
//
// Playback is always fire-and-forget — nothing in the shell ever blocks or
// waits on audio, and a machine with no sound stack simply stays silent
// rather than erroring.

#pragma once

#include <QString>

namespace castalia {

// The sound ids in branding/sound/palette.toml. Kept as an enum so a typo is
// a compile error rather than a silent no-op.
enum class Sound {
    Startup,      // session start (greeter → desktop)
    Shutdown,     // session end / power off
    Notify,       // toast / message
    Error,        // blocking error
    DeviceIn,     // USB / media inserted
    DeviceOut,    // USB / media removed
    EmptyTrash,   // recycle bin emptied
};

// The palette id ("startup", "empty-trash", …) for a sound.
QString soundId(Sound sound);

// Absolute path to the rendered WAV for `sound` under `repoRoot`
// (branding/sound/wav/<id>.wav). Does not check that it exists.
QString soundPath(const QString &repoRoot, Sound sound);

// Whether system sounds should play at all. False when:
//   * CASTALIA_NO_SOUND=1 (kill switch for CI, kiosks and quiet rooms),
//   * the render is head-less (QT_QPA_PLATFORM=offscreen) — the CI gates
//     must never spawn audio processes,
//   * the user turned them off in ~/.config/castalia/sound.conf
//     ("enabled = false"), or
//   * the machine has no player we know how to drive.
bool soundsEnabled();

// Play a system sound, if sounds are enabled and the WAV is present.
// Returns true when a player was actually started. Never blocks; failures
// are silent by design (a missing audio stack is not an error worth a
// dialog).
bool playSound(const QString &repoRoot, Sound sound);

} // namespace castalia
