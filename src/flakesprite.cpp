/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "flakesprite.h"

#include <QPainter>
#include <QRadialGradient>

#include <algorithm>

namespace Snow
{

/*
    As with the simulation in snowfall.cpp, the numbers below are the published
    prototype's (spec: Reference), restated in logical pixels. The prototype is
    what "does this read as snow" is measured against, so a constant whose only
    justification is that it looked right over there says so.
*/

// How wide a sprite is drawn, as a multiple of the Flake's radius. The radius
// is the Flake's own scale; the sprite image is mostly falloff around it, so
// the square it fills is several times larger. Crystal is the tighter of the
// two because its arms reach nearly to the edge of its image where the blob's
// gradient has long since faded out.
static constexpr qreal s_blobSpan = 4.2;
static constexpr qreal s_crystalSpan = 3.4;

// Under the `depth` Flake style, how much of its radius a Flake at the far
// plane keeps and how much more it gains by the near one. The same shape as the
// fall speed and the landing mass in snowfall.cpp, and for the same reason: one
// z, read consistently everywhere, is what makes the parallax hold together.
static constexpr qreal s_farRadiusFactor = 0.55;
static constexpr qreal s_radiusFactorRange = 1.0;

// And the same for opacity: distant snow is fainter as well as smaller and
// slower, which is most of what sells the depth.
static constexpr qreal s_farOpacity = 0.3;
static constexpr qreal s_opacityRange = 0.7;

// What `blob` and `crystal` draw at instead, where every Flake is at the same
// distance. Slightly short of opaque, so snow reads as snow rather than as
// paint.
static constexpr qreal s_flatOpacity = 0.9;

// The design size of each sprite image, in pixels before supersampling, and the
// prototype units every measurement below is in.
static constexpr int s_blobExtent = 32;
static constexpr int s_crystalExtent = 48;

// The blob's gradient: opaque white in the middle, a touch of the blue-white
// snow colour through the shoulder, and gone by the rim.
static const QColor s_blobCore(255, 255, 255);
static const QColor s_blobBody(244, 248, 251);
static constexpr qreal s_blobShoulder = 0.45;
static constexpr qreal s_blobShoulderAlpha = 0.85;

// The crystal, in its own 48x48 image with the origin at the centre: six arms
// reaching s_crystalArm up, each carrying a pair of branches at the distances
// in s_crystalBranches, angled up and out by s_crystalBranch in both directions.
static constexpr int s_crystalArms = 6;
static constexpr qreal s_crystalArm = 19.0;
static constexpr qreal s_crystalArmWidth = 2.6;
static constexpr qreal s_crystalBranchWidth = 1.8;
static constexpr qreal s_crystalBranch = 5.5;
static constexpr qreal s_crystalBranches[] = {13.0, 8.0};
static constexpr int s_crystalInk = 242; // 0.95 alpha, as the prototype has it

qreal flakeOpacity(qreal z, FlakeStyle style)
{
    if (style != FlakeStyle::Depth) {
        return s_flatOpacity;
    }
    return s_farOpacity + s_opacityRange * z;
}

FlakeSprite spriteFor(const Flake &flake, FlakeStyle style)
{
    const bool parallax = style == FlakeStyle::Depth;
    const qreal radius = parallax
        ? flake.radius * (s_farRadiusFactor + s_radiusFactorRange * flake.z)
        : flake.radius;

    FlakeSprite sprite;
    sprite.size = radius * (style == FlakeStyle::Crystal ? s_crystalSpan : s_blobSpan);
    sprite.opacity = flakeOpacity(flake.z, style);
    // A blob is round and a depth-cued blob is the same blob; turning either
    // would cost a trigonometric pair per Flake per frame to produce exactly
    // the image it already had.
    sprite.rotation = style == FlakeStyle::Crystal ? flake.rotation : 0.0;
    return sprite;
}

/** A transparent premultiplied image @a extent design units square. */
static QImage sprite(int extent, int supersample)
{
    const int side = extent * std::max(1, supersample);
    QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

QImage blobSpriteImage(int supersample)
{
    QImage image = sprite(s_blobExtent, supersample);

    const qreal centre = s_blobExtent / 2.0;
    QRadialGradient gradient(centre, centre, centre);
    gradient.setColorAt(0.0, s_blobCore);
    QColor shoulder = s_blobBody;
    shoulder.setAlphaF(s_blobShoulderAlpha);
    gradient.setColorAt(s_blobShoulder, shoulder);
    QColor rim = s_blobBody;
    rim.setAlphaF(0.0);
    gradient.setColorAt(1.0, rim);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    // Everything below is in design units; the supersampling is a scale on the
    // way out, so the prototype's numbers stay the prototype's numbers.
    painter.scale(image.width() / qreal(s_blobExtent), image.height() / qreal(s_blobExtent));
    painter.fillRect(QRectF(0, 0, s_blobExtent, s_blobExtent), gradient);

    return image;
}

QImage crystalSpriteImage(int supersample)
{
    QImage image = sprite(s_crystalExtent, supersample);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(image.width() / qreal(s_crystalExtent), image.height() / qreal(s_crystalExtent));
    painter.translate(s_crystalExtent / 2.0, s_crystalExtent / 2.0);

    QPen pen(QColor(255, 255, 255, s_crystalInk));
    pen.setCapStyle(Qt::RoundCap);

    for (int arm = 0; arm < s_crystalArms; ++arm) {
        painter.save();
        painter.rotate(arm * 360.0 / s_crystalArms);

        pen.setWidthF(s_crystalArmWidth);
        painter.setPen(pen);
        painter.drawLine(QPointF(0, 0), QPointF(0, -s_crystalArm));

        pen.setWidthF(s_crystalBranchWidth);
        painter.setPen(pen);
        for (const qreal along : s_crystalBranches) {
            const QPointF root(0, -along);
            painter.drawLine(root, root + QPointF(s_crystalBranch, -s_crystalBranch));
            painter.drawLine(root, root + QPointF(-s_crystalBranch, -s_crystalBranch));
        }

        painter.restore();
    }

    return image;
}

} // namespace Snow
