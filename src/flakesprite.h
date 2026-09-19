/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "flake.h"
#include "settings.h"

#include <QImage>

namespace Snow
{

/**
 * How much finer than its design size a sprite image is rendered.
 *
 * A sprite is drawn at up to about 21 logical pixels across, against a design
 * size of 32 (blob) and 48 (crystal). On an output at scale 2 that is 42 device
 * pixels, which would magnify the blob; rendering the image at twice its design
 * size covers that without anyone having to re-render a texture when a window
 * crosses to a display with a different scale factor.
 */
inline constexpr int s_spriteSupersample = 2;

/**
 * Where one Flake's sprite goes and how strongly it is drawn.
 *
 * Pure geometry, so that "how big is a near Flake against a far one" is
 * something a unit test answers rather than something that needs a compositor
 * and a pair of eyes -- the same split that keeps Snowfall free of KWin. What
 * turns this into pixels is FlakePainter.
 */
struct FlakeSprite
{
    /** The side of the square the sprite image fills, in logical pixels. */
    qreal size = 0;

    /** 0 (invisible) to 1 (full strength). */
    qreal opacity = 0;

    /** Radians. Only `crystal` turns; the other two styles draw upright. */
    qreal rotation = 0;
};

/** Where and how strongly @a flake is drawn under @a style. */
FlakeSprite spriteFor(const Flake &flake, FlakeStyle style);

/**
 * The opacity of a Flake at depth @a z under @a style.
 *
 * Split out of spriteFor() because the painter batches the `depth` style into a
 * handful of depth planes and needs the opacity of a plane rather than of a
 * Flake; see FlakePainter.
 */
qreal flakeOpacity(qreal z, FlakeStyle style);

/**
 * The soft radial-falloff Flake of the `blob` and `depth` styles, rendered at
 * @a supersample times its design size.
 *
 * Premultiplied ARGB, transparent at the edges, so it can be drawn as a plain
 * quad with no shape of its own.
 */
QImage blobSpriteImage(int supersample = s_spriteSupersample);

/**
 * The procedural six-point Flake of the `crystal` style, rendered at
 * @a supersample times its design size.
 *
 * Six arms with two pairs of branches each, drawn once and then turned per
 * Flake at paint time -- which is the whole reason it is a sprite and not
 * geometry: the shape never changes, only its angle does.
 */
QImage crystalSpriteImage(int supersample = s_spriteSupersample);

} // namespace Snow
