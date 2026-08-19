// Castalia shell — the mark, painted wherever the shell needs its identity.
//
// The official artwork is `branding/logo/castalia-logo.png` (the "C monogram
// keep"): the C as a thick azure ring with the keep standing in its bowl on
// green hills. drawMark() paints that file, scaled and centred.
//
// It falls back to a native QPainter re-drawing of the same mark
// (branding/logo/castalia-mark.svg's geometry) when the asset tree cannot be
// found — a rescue shell, an app started from an odd working directory or a
// build with no assets staged yet still gets its logo instead of a hole.
#pragma once

#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QSize>
#include <QString>

namespace castalia {

// Where the branding/, themes/ and build/out/ trees live. Resolved once, in
// this order: CASTALIA_REPO (set by castalia-session), the installed prefix
// /usr/share/castalia, then the working directory.
QString assetRoot(const QString &repoRoot = QString());

// The logo artwork scaled to fit inside `size`, aspect preserved. Cached per
// requested size — the panel repaints its orb far more often than the file
// changes. A null pixmap means the asset tree has no logo.
QPixmap markPixmap(const QSize &size, const QString &repoRoot = QString());

// Paint the mark centred in `rect` (aspect preserved).
void drawMark(QPainter *p, const QRectF &rect,
              const QString &repoRoot = QString());

// The vector re-drawing on its own, for callers that specifically want the
// geometry (the CI render gate pins it, so a broken asset path can never
// quietly pass as "the logo rendered fine").
void drawMarkVector(QPainter *p, const QRectF &rect);

} // namespace castalia
