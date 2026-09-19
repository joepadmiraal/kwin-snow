/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    What a Flake is drawn as, checked without a compositor.

    A sprite is a QImage and a placement is arithmetic, so "is a near Flake
    larger and brighter than a distant one", "does the blob actually fall off to
    nothing" and "does the crystal have six arms" are all things a test can
    answer. What it cannot answer is whether the result reads as snow, which is
    what the nested session and the published prototype are for; see
    docs/development.md.

    The motion these sprites are attached to is in snowfalltest.
*/

#include "flakesprite.h"

#include <QTest>

#include <cmath>
#include <numbers>

using namespace Snow;

/** A Flake at depth @a z, with everything else fixed so only z varies. */
static Flake flake(qreal z, qreal radius = 4.0, qreal rotation = 0.0)
{
    Flake result;
    result.z = z;
    result.radius = radius;
    result.rotation = rotation;
    return result;
}

/** The alpha of @a image at design-unit offset (@a x, @a y) from its centre. */
static int alphaAt(const QImage &image, qreal x, qreal y, int extent)
{
    const qreal unit = image.width() / qreal(extent);
    const int px = int(std::lround(image.width() / 2.0 + x * unit));
    const int py = int(std::lround(image.height() / 2.0 + y * unit));
    return qAlpha(image.pixel(std::clamp(px, 0, image.width() - 1),
                              std::clamp(py, 0, image.height() - 1)));
}

class FlakeSpriteTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void blobIsOneSizeWhateverTheDepth();
    void depthScalesSizeWithZ();
    void depthScalesOpacityWithZ();
    void depthNeverOutgrowsItsRadius();
    void onlyCrystalTurns();
    void crystalIsTighterThanBlob();
    void sizeFollowsTheRadius();
    void blobFadesToNothing();
    void blobIsRoundAndCentred();
    void crystalHasSixArms();
    void crystalIsEmptyInTheCorners();
    void supersamplingChangesResolutionNotShape();
    void spritesArePremultipliedAndSquare();
};

void FlakeSpriteTest::blobIsOneSizeWhateverTheDepth()
{
    // The `blob` style is one uniform size and one opacity (spec:
    // Configuration): z is simulated all the same, and simply not read here.
    const FlakeSprite far = spriteFor(flake(0.05), FlakeStyle::Blob);
    const FlakeSprite near = spriteFor(flake(0.95), FlakeStyle::Blob);

    QCOMPARE(far.size, near.size);
    QCOMPARE(far.opacity, near.opacity);
    QVERIFY(far.opacity > 0.5);
}

void FlakeSpriteTest::depthScalesSizeWithZ()
{
    const FlakeSprite far = spriteFor(flake(0.0), FlakeStyle::Depth);
    const FlakeSprite near = spriteFor(flake(1.0), FlakeStyle::Depth);

    QVERIFY(near.size > far.size);
    // Parallax worth the name: the near plane is not a few per cent larger.
    QVERIFY(near.size > far.size * 2);

    // And monotonic in between, so nothing in the field reads as two ranks.
    qreal previous = 0;
    for (qreal z = 0; z <= 1.0; z += 0.1) {
        const qreal size = spriteFor(flake(z), FlakeStyle::Depth).size;
        QVERIFY(size > previous);
        previous = size;
    }
}

void FlakeSpriteTest::depthScalesOpacityWithZ()
{
    const FlakeSprite far = spriteFor(flake(0.0), FlakeStyle::Depth);
    const FlakeSprite near = spriteFor(flake(1.0), FlakeStyle::Depth);

    QVERIFY(far.opacity > 0.0); // distant snow is faint, never invisible
    QVERIFY(near.opacity > far.opacity * 2);
    QVERIFY(near.opacity <= 1.0);

    // The painter batches the style into depth planes and asks for the opacity
    // of a depth rather than of a Flake; the two have to agree.
    QCOMPARE(flakeOpacity(0.4, FlakeStyle::Depth), spriteFor(flake(0.4), FlakeStyle::Depth).opacity);
    QCOMPARE(flakeOpacity(0.4, FlakeStyle::Blob), spriteFor(flake(0.4), FlakeStyle::Blob).opacity);
}

void FlakeSpriteTest::depthNeverOutgrowsItsRadius()
{
    // The near plane is where the size is largest; a Flake there is still drawn
    // at about what the flat styles draw the same radius at, not at a blot.
    const qreal flat = spriteFor(flake(0.5), FlakeStyle::Blob).size;
    const qreal nearest = spriteFor(flake(1.0), FlakeStyle::Depth).size;

    QVERIFY(nearest > flat);
    QVERIFY(nearest < flat * 2);
}

void FlakeSpriteTest::onlyCrystalTurns()
{
    const qreal angle = 1.2;

    QCOMPARE(spriteFor(flake(0.5, 4.0, angle), FlakeStyle::Crystal).rotation, angle);
    // A round sprite turned is the sprite it already was, so the other two
    // styles carry the rotation the simulation keeps for them and ignore it.
    QCOMPARE(spriteFor(flake(0.5, 4.0, angle), FlakeStyle::Blob).rotation, 0.0);
    QCOMPARE(spriteFor(flake(0.5, 4.0, angle), FlakeStyle::Depth).rotation, 0.0);
}

void FlakeSpriteTest::crystalIsTighterThanBlob()
{
    // The crystal's arms reach nearly to the edge of its image where the blob's
    // gradient has faded out, so the same radius fills a smaller square.
    const qreal crystal = spriteFor(flake(0.5), FlakeStyle::Crystal).size;
    const qreal blob = spriteFor(flake(0.5), FlakeStyle::Blob).size;

    QVERIFY(crystal < blob);
    QVERIFY(crystal > blob * 0.5);
}

void FlakeSpriteTest::sizeFollowsTheRadius()
{
    // The per-Flake radius spread is what keeps a few hundred Flakes from
    // reading as one rigid sheet, so it has to survive into the drawing.
    const qreal small = spriteFor(flake(0.5, 2.6), FlakeStyle::Blob).size;
    const qreal large = spriteFor(flake(0.5, 5.0), FlakeStyle::Blob).size;

    QVERIFY(large > small);
    QCOMPARE(large / small, 5.0 / 2.6);
}

void FlakeSpriteTest::blobFadesToNothing()
{
    const QImage blob = blobSpriteImage();

    // Opaque in the middle -- not quite 255, because the sample lands half a
    // pixel off the exact centre of the gradient -- and all but gone by the rim.
    QVERIFY(alphaAt(blob, 0, 0, 32) >= 250);
    QVERIFY(alphaAt(blob, 15.5, 0, 32) < 10);
    // Past the gradient's radius there is nothing at all, which is what the
    // corners of the quad sample.
    QCOMPARE(alphaAt(blob, 15.5, 15.5, 32), 0);

    // And never brightening on the way out: a step back up anywhere in there
    // would draw a ring, and a cliff would draw a disc.
    int previous = 256;
    for (qreal radius = 0; radius <= 16.0; radius += 0.25) {
        const int alpha = alphaAt(blob, radius, 0, 32);
        QVERIFY(alpha <= previous);
        previous = alpha;
    }
}

void FlakeSpriteTest::blobIsRoundAndCentred()
{
    const QImage blob = blobSpriteImage();

    // Equal in every direction at the same distance, which is what lets the
    // painter skip a rotation for this sprite entirely. The few levels of slack
    // are the sample landing on the pixel grid rather than on the exact circle.
    const int reference = alphaAt(blob, 6.0, 0, 32);
    QVERIFY(reference > 0);
    for (int degrees = 0; degrees < 360; degrees += 15) {
        const qreal angle = degrees * std::numbers::pi / 180.0;
        const int alpha = alphaAt(blob, 6.0 * std::cos(angle), 6.0 * std::sin(angle), 32);
        QVERIFY(std::abs(alpha - reference) <= 8);
    }
}

void FlakeSpriteTest::crystalHasSixArms()
{
    const QImage crystal = crystalSpriteImage();

    // Ink along each arm, and the same ink on all six: a crystal that had lost
    // an arm or landed off centre would show up here as one angle out of six.
    for (int arm = 0; arm < 6; ++arm) {
        const qreal angle = arm * std::numbers::pi / 3;
        const int alpha = alphaAt(crystal, 15.0 * std::sin(angle), -15.0 * std::cos(angle), 48);
        QVERIFY2(alpha > 200, qPrintable(QStringLiteral("arm %1 alpha %2").arg(arm).arg(alpha)));
    }

    // And nothing between them, which is what makes it six points rather than
    // a star-shaped smear.
    const qreal between = std::numbers::pi / 6;
    QCOMPARE(alphaAt(crystal, 15.0 * std::sin(between), -15.0 * std::cos(between), 48), 0);
}

void FlakeSpriteTest::crystalIsEmptyInTheCorners()
{
    const QImage crystal = crystalSpriteImage();

    // The quad's corners sample here, and the texture is clamped rather than
    // wrapped, so anything left in a corner would draw as a smudge.
    QCOMPARE(alphaAt(crystal, 23.5, 23.5, 48), 0);
    QCOMPARE(alphaAt(crystal, -23.5, 23.5, 48), 0);
}

void FlakeSpriteTest::supersamplingChangesResolutionNotShape()
{
    const QImage plain = blobSpriteImage(1);
    const QImage finer = blobSpriteImage(2);

    QCOMPARE(finer.width(), plain.width() * 2);
    // The same sprite, measured in the same design units: supersampling buys
    // resolution for an output at scale 2, not a different Flake. The two are
    // sampled on different pixel grids, so they agree closely rather than
    // exactly -- half a pixel of the finer grid is where the slack comes from.
    for (qreal radius = 0; radius <= 12.0; radius += 2.0) {
        QVERIFY(std::abs(alphaAt(finer, radius, 0, 32) - alphaAt(plain, radius, 0, 32)) <= 10);
    }
}

void FlakeSpriteTest::spritesArePremultipliedAndSquare()
{
    // Premultiplied is what lets the painter blend with GL_ONE and modulate
    // every channel alike; square is what lets it draw one quad per Flake.
    for (const QImage &image : {blobSpriteImage(), crystalSpriteImage()}) {
        QCOMPARE(image.format(), QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(image.width(), image.height());
        QVERIFY(!image.isNull());
    }
}

QTEST_GUILESS_MAIN(FlakeSpriteTest)

#include "flakespritetest.moc"
