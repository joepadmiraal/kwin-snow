/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "settings.h"

#include <QImage>
#include <QList>

namespace Snow
{

class Snowline;

/**
 * How many Columns at each end of a Snowline the drawn depth ramps to nothing
 * over.
 *
 * A window decoration has rounded corners, and snow drawn square across them
 * hangs over the rounding with nothing under it. Ramping the outermost few
 * Columns to zero approximates the curve well enough at five logical pixels a
 * Column. This is a correctness fix and deliberately not a setting: asking the
 * decoration for its real corner radius is out of scope (spec: Out of scope),
 * and a user has no way to answer the question better than this does.
 */
inline constexpr int s_capCornerColumns = 4;

/** Below this depth, in logical pixels, a Snowline is not worth drawing. */
inline constexpr qreal s_capVisibleDepth = 0.15;

/**
 * How far a Cap reaches down into its own Snow Catcher, in logical pixels.
 *
 * The band is drawn from the contour down to a line just inside the Catcher
 * rather than exactly on its top edge, so that there is no hairline of window
 * showing through between the snow and the frame it is standing on.
 * Limited to the local drawn depth so bare Columns have no underlap.
 */
inline constexpr qreal s_capUnderlap = 3.0;

/** How thick the bright line along a Cap's leading edge is, in logical pixels. */
inline constexpr qreal s_capEdgeWidth = 1.6;

/**
 * The profile image's two columns: the body of the Cap and the bright line
 * along its leading edge. They share one image, and so one texture and one
 * draw, because the only thing that differs between them is which column of it
 * a vertex samples; see CapPainter.
 */
inline constexpr int s_capProfileColumns = 2;
inline constexpr int s_capBodyColumn = 0;
inline constexpr int s_capEdgeColumn = 1;

/** How many texels tall the profile image is rendered. */
inline constexpr int s_capProfileHeight = 64;

/**
 * The drawn depth of every Column of @a snowline, in logical pixels.
 *
 * This is the Cap: the Snowline is the depth data, and what comes out here is
 * its appearance (CONTEXT.md: Cap). Two things separate the two. Every Catcher
 * with corners gets the ramp above, which is also applied by
 * Catcher::surfaceAt() so that Flakes land on the surface that is drawn. Under
 * the `contour` style each Column is additionally nudged by a static noise
 * offset, which is drawing only -- it is what makes the pile read as settled
 * rather than extruded, and it must not move the surface Flakes land on.
 */
QList<qreal> capContour(const Snowline &snowline, CapStyle style, bool roundedCorners);

/**
 * One Column of that, which is what CapPainter walks a Snowline with.
 *
 * The same rule stated one Column at a time, so that drawing a Cap does not
 * have to build a list the length of the Catcher and throw it away again in
 * every frame of every window on the desktop.
 */
qreal capDepthAt(const Snowline &snowline, int column, CapStyle style, bool roundedCorners);

/**
 * How much of @a column's depth is standing there, 0 to 1: the ramp toward a
 * Catcher's two corners.
 *
 * Symmetric even when a Catcher is narrower than two ramps, where the two
 * overlap and the shallower of them wins.
 */
qreal capCornerRamp(int column, int columnCount);

/**
 * The static per-Column offset of the `contour` style, 0 to 1.
 *
 * A pure function of the Column index, and deliberately not an array generated
 * once per Catcher: a Snowline re-spans on resize -- truncate right,
 * zero-extend, never rescale (ADR-0001) -- so a stored array would have to
 * follow the same rule or slide out of step with the depths it offsets. A
 * function cannot go out of step, and costs nothing to keep. The price is that
 * two Catchers of the same width get the same bumps, which nothing resolves at
 * a fifth of a Column's depth over five logical pixels.
 */
qreal capContourNoise(int column);

/** Whether @a snowline holds enough snow to be worth drawing a Cap for. */
bool capIsVisible(const Snowline &snowline);

/**
 * The depth a Cap is drawn against: @a maximumDepth, or the snow actually
 * standing on @a snowline when that is deeper.
 *
 * The two differ for a few seconds after `maxDepth` is lowered, where the snow
 * that was already standing is left to melt down to the new cap rather than
 * being cut to it (spec: Behaviour, "Settings changed"). Until it has, the Cap
 * is drawn against the depth that is really there: everything below is scaled
 * to the cap, so using the new one would clip the top off the band and flatten
 * the shading of exactly the Caps whose melting is the thing being watched.
 */
qreal capReferenceDepth(const Snowline &snowline, qreal maximumDepth);

/**
 * How far above its Catcher's top edge a Cap can reach, in logical pixels,
 * given a @a maximumDepth.
 *
 * This is the strip SnowEffect::prePaintWindow() has to add to what will be
 * painted: it is outside the window's own geometry, so without it KWin culls
 * the Cap away.
 */
qreal capHeadroom(qreal maximumDepth);

/**
 * Where the shading profile's top lies, as an offset from the Catcher's top
 * edge -- negative, because it is above it.
 *
 * The shading is anchored to the Catcher and not to the contour, which is what
 * makes depth read as depth: a deep pile shows the whole ramp from its bright
 * leading edge down to the shadow under it, and a dusting shows only the dark
 * end, so the two do not look like the same snow at different sizes.
 */
qreal capProfileTop(qreal maximumDepth);

/** How far the shading profile runs below capProfileTop(), in logical pixels. */
qreal capProfileSpan(qreal maximumDepth);

/**
 * The vertical profile a Cap is shaded with, @a height texels tall and
 * s_capProfileColumns wide.
 *
 * Column s_capBodyColumn is the band itself, top to bottom: the prototype
 * paints it as a flat fill with a blue-grey gradient laid over it, and since
 * both are flat in x the two together are one opaque vertical ramp, which is
 * what is stored here. Column s_capEdgeColumn is the bright line along the
 * leading edge, and is flat -- it is a column of this image rather than a
 * second texture only so that the whole Cap is one draw.
 *
 * Premultiplied ARGB, like the Flake sprites.
 */
QImage capProfileImage(int height = s_capProfileHeight);

} // namespace Snow
