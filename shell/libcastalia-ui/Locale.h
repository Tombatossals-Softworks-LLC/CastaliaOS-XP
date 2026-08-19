// Castalia shell — the interface language (Bible §7.13, §12.3).
//
// Castalia is written **in Spanish**: the string literals in the source are
// what a Spanish user sees, and no translation file is consulted for them.
// Every other language is a Qt translation catalogue on top
// (`i18n/castalia_<lang>.ts` → `castalia_<lang>.qm`), loaded here.
//
// Two deliberate decisions:
//
//   * The default is Spanish **whatever the system locale says**. A machine
//     with LANG=en_US.UTF-8 still boots Castalia in Spanish unless somebody
//     chose otherwise, because the product is Spanish-first (§8) and because
//     an interface that changes language depending on an environment variable
//     makes every screenshot, render gate and self-test non-deterministic.
//     "Follow the system" is available, but it is a choice you make.
//   * The choice lives in one file, `~/.config/castalia/locale.conf`, the way
//     the theme does (§6.6). The Control Center writes it; the installer
//     writes it at first boot from what the user picked there (§14).
//
// Applying it is free for an app: castalia::applyTheme() calls applyLocale(),
// so every first-party binary is translated by linking libcastalia-ui.
#pragma once

#include <QString>
#include <QStringList>

class QCoreApplication;

namespace castalia {
namespace locale {

// The languages Castalia ships an interface in, in display order. "es" is
// the source language and has no catalogue.
QStringList available();

// The human name of a language code, in that language ("Español",
// "English") — what a language picker shows.
QString displayName(const QString &code);

// Resolve the interface language from a config value and the environment.
// Pure, and pinned by the self-test because every rule here is a decision:
//
//   configured empty or "es"  → "es"      (the source language)
//   configured "auto"         → the environment's language, if we ship it
//   configured anything else  → that, if we ship it; "es" otherwise
//
// `environment` is what LANGUAGE/LC_ALL/LANG carry ("en_GB.UTF-8", "es"…);
// only its language part is used.
QString resolve(const QString &configured, const QString &environment);

// The configured value from ~/.config/castalia/locale.conf, then
// /etc/castalia/locale.conf — the same order the theme resolves in (§6.6).
// Empty when neither exists.
QString configured();

// Load and install the translation for `code` into the application, looking
// for castalia_<code>.qm under the repo/asset tree and the installed prefix.
// Returns false when there is nothing to load — which is the normal, silent
// case for Spanish.
bool apply(QCoreApplication *app, const QString &repoRoot,
           const QString &code);

// The whole thing: resolve the language and apply it. Called by
// castalia::applyTheme(), so apps get it without asking.
QString applyConfigured(QCoreApplication *app, const QString &repoRoot);

} // namespace locale
} // namespace castalia
