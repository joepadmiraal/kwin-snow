/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "cap.h"

#include "snowline.h"

#include <QColor>
#include <QLinearGradient>
#include <QPainter>

#include <algorithm>
#include <limits>

namespace Snow
{

/*
    As with the Flake sprites in flakesprite.cpp, the numbers below are the
    published prototype's (spec: Reference), restated in logical pixels. The
    prototype is what "does this read as snow" is measured against, so a
    constant whose only justification is that it looked right over there says
    so.
*/

// What the `contour` style multiplies a Column's depth by: the floor, plus this
// much of the Column's own noise. Centred a little under 1, so the noise reads
// as an uneven top rather than as a pile that grew.
static constexpr qreal s_contourFloor = 0.72;
static constexpr qreal s_contourRange = 0.46;

// The deepest the `contour` style can draw a Column, as a multiple of what is
// standing there. It is over 1: the noise is centred a little below the depth
// and reaches a little above it, which is what makes an even pile read as an
// uneven one rather than as a shallower one.
static constexpr qreal s_contourCeiling = s_contourFloor + s_contourRange;

// Where the shading profile sits relative to the Catcher, in logical pixels: it
// starts this far above the deepest the snow can get, and runs to a little
// below the Catcher's top edge, so a Cap at the depth cap still has shadow left
// under it.
static constexpr qreal s_profileHeadStart = 6.0;
static constexpr qreal s_profileTail = 8.0;

// The snow itself, which is the flat fill the shading is laid over.
static const QColor s_capBody(244, 248, 251);

// The shadow under the leading edge: three stops down the profile, from nothing
// at the top through a knee to its full strength at the bottom. Blue-grey
// rather than black, because snow in shade takes the colour of the sky.
static const QColor s_shadowTop(154, 186, 209);
static const QColor s_shadowKnee(139, 173, 199);
static const QColor s_shadowFoot(108, 146, 176);
static constexpr qreal s_shadowKneeAt = 0.55;
static constexpr qreal s_shadowKneeAlpha = 0.30;
static constexpr qreal s_shadowFootAlpha = 0.55;

// The bright line along the leading edge, which is what reads as the lit top of
// the pile. Just short of opaque, for the same reason the Flakes are.
static const QColor s_capEdge(255, 255, 255);
static constexpr qreal s_capEdgeAlpha = 0.95;

qreal capCornerRamp(int column, int columnCount)
{
    const qreal fromLeft = (column + 0.5) / s_capCornerColumns;
    const qreal fromRight = (columnCount - column - 0.5) / s_capCornerColumns;
    // The smaller of the two, rather than the first that matches: on a Catcher
    // narrower than two ramps they overlap, and taking either one alone would
    // leave the Cap lopsided.
    return std::clamp(std::min(fromLeft, fromRight), 0.0, 1.0);
}

qreal capContourNoise(int column)
{
    // An integer hash rather than a random number: the same Column has to get
    // the same offset on every frame for the whole life of its Catcher, or the
    // top edge boils. See the header for why it is not an array.
    quint32 hash = quint32(column) * 2654435761u;
    hash ^= hash >> 15;
    hash *= 2246822519u;
    hash ^= hash >> 13;
    hash *= 3266489917u;
    hash ^= hash >> 16;
    return hash / qreal(std::numeric_limits<quint32>::max());
}

qreal capDepthAt(const Snowline &snowline, int column, CapStyle style, bool roundedCorners)
{
    qreal depth = snowline.depth(column);
    if (roundedCorners) {
        depth *= capCornerRamp(column, snowline.columnCount());
    }
    if (style == CapStyle::Contour) {
        depth *= s_contourFloor + capContourNoise(column) * s_contourRange;
    }
    return depth;
}

QList<qreal> capContour(const Snowline &snowline, CapStyle style, bool roundedCorners)
{
    const int count = snowline.columnCount();

    QList<qreal> contour;
    contour.reserve(count);

    for (int column = 0; column < count; ++column) {
        contour.append(capDepthAt(snowline, column, style, roundedCorners));
    }

    return contour;
}

bool capIsVisible(const Snowline &snowline)
{
    // The deepest Column, not the sum: a Snowline with one visible pile on it
    // and nothing else is worth drawing, and a wide one holding a hundredth of
    // a pixel everywhere is not.
    return snowline.maximumDepth() >= s_capVisibleDepth;
}

qreal capReferenceDepth(const Snowline &snowline, qreal maximumDepth)
{
    return std::max(maximumDepth, snowline.maximumDepth());
}

qreal capHeadroom(qreal maximumDepth)
{
    // The deepest the snow can stand, taken through the highest the `contour`
    // style's noise can lift it, plus the half of the leading edge's line drawn
    // above the contour -- rounded up to the whole width, which is a pixel of
    // slack rather than an exact bound to get wrong. This is a true bound and
    // not the prototype's: getting it short does not look softer, it cuts the
    // top off the Cap.
    return maximumDepth * s_contourCeiling + s_capEdgeWidth;
}

qreal capProfileTop(qreal maximumDepth)
{
    // Anchored at the depth cap, not at the ceiling capHeadroom() has to allow
    // for: the profile is a gradient and its top is its brightest stop, so the
    // sliver of a Cap that the noise lifts past the cap samples past the end of
    // it and is clamped to exactly that. Nothing is lost by it, and keeping the
    // anchor where the prototype put it keeps the shading the prototype's.
    return -(maximumDepth + s_profileHeadStart);
}

qreal capProfileSpan(qreal maximumDepth)
{
    return maximumDepth + s_profileHeadStart + s_profileTail;
}

/** @a colour at @a alpha, which is how the prototype states its stops. */
static QColor withAlpha(QColor colour, qreal alpha)
{
    colour.setAlphaF(alpha);
    return colour;
}

QImage capProfileImage(int height)
{
    QImage image(s_capProfileColumns, std::max(1, height), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const QRect body(s_capBodyColumn, 0, 1, image.height());
    const QRect edge(s_capEdgeColumn, 0, 1, image.height());

    QLinearGradient shadow(0, 0, 0, image.height());
    shadow.setColorAt(0.0, withAlpha(s_shadowTop, 0.0));
    shadow.setColorAt(s_shadowKneeAt, withAlpha(s_shadowKnee, s_shadowKneeAlpha));
    shadow.setColorAt(1.0, withAlpha(s_shadowFoot, s_shadowFootAlpha));

    QPainter painter(&image);
    // Fill then shade, in that order and in one column, which is the prototype
    // filling the band and clipping its gradient to it. Because both are flat
    // across the band, the two collapse into the single opaque ramp stored
    // here -- and a Cap into a single textured draw.
    painter.fillRect(body, s_capBody);
    painter.fillRect(body, shadow);
    painter.fillRect(edge, withAlpha(s_capEdge, s_capEdgeAlpha));

    return image;
}

} // namespace Snow
