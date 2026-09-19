/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    What a Cap is made of, checked without a compositor: the contour drawn over
    a Snowline, the ramp toward a Catcher's corners, the `contour` style's
    noise, and the profile the band is shaded with. All of it is arithmetic and
    a QImage -- the same split that keeps the Flake sprites checkable here while
    CapPainter keeps every line of KWin.

    Whether it *looks* like settled snow is the one thing left that needs the
    nested session and the published prototype; see docs/development.md.
*/

#include "cap.h"
#include "settings.h"
#include "snowline.h"

#include <QTest>

#include <cmath>

using namespace Snow;

// Wide enough that the two corner ramps do not meet, which is the ordinary
// case: 20 Columns against a ramp of 4 at each end.
static constexpr qreal s_catcherWidth = 100.0;

static Snowline levelSnowline(qreal width, qreal depth)
{
    Snowline snowline(width);
    for (int column = 0; column < snowline.columnCount(); ++column) {
        snowline.setDepth(column, depth);
    }
    return snowline;
}

class CapTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theCornerRampIsOutOfTheWayInTheMiddle();
    void theCornerRampComesDownToNothingAtBothEnds();
    void theCornerRampStaysSymmetricOnANarrowCatcher();
    void theContourNoiseIsTheSameEveryTimeItIsAsked();
    void theContourNoiseDoesNotFollowTheColumnCount();
    void theContourNoiseUsesItsWholeRange();
    void theShadedStyleDrawsWhatIsStandingThere();
    void theContourStyleNudgesEveryColumn();
    void aCapIsDrawnOverEveryColumnOfItsSnowline();
    void aDustingIsNotWorthDrawing();
    void theHeadroomCoversTheDeepestACapCanBeDrawn();
    void aCapIsDrawnAgainstTheSnowThatIsStanding();
    void theProfileRunsFromAboveTheSnowToInsideTheCatcher();
    void theProfileImageIsAnOpaqueBandAndABrightEdge();
    void theProfileImageDarkensDownward();
};

void CapTest::theCornerRampIsOutOfTheWayInTheMiddle()
{
    // Full depth everywhere from the fourth Column in to the fourth from the
    // end, which is every Column of a Catcher wider than two ramps.
    for (int column = s_capCornerColumns; column < 20 - s_capCornerColumns; ++column) {
        QCOMPARE(capCornerRamp(column, 20), 1.0);
    }
}

void CapTest::theCornerRampComesDownToNothingAtBothEnds()
{
    QCOMPARE(capCornerRamp(0, 20), 0.125);
    QCOMPARE(capCornerRamp(1, 20), 0.375);
    QCOMPARE(capCornerRamp(2, 20), 0.625);
    QCOMPARE(capCornerRamp(3, 20), 0.875);

    // And the mirror image at the other end, because a window has two corners.
    for (int column = 0; column < 20; ++column) {
        QCOMPARE(capCornerRamp(column, 20), capCornerRamp(19 - column, 20));
    }
}

void CapTest::theCornerRampStaysSymmetricOnANarrowCatcher()
{
    // Narrower than two ramps, where they overlap: taking whichever matched
    // first would leave one end of the Cap standing higher than the other.
    for (int count = 1; count <= 2 * s_capCornerColumns; ++count) {
        for (int column = 0; column < count; ++column) {
            QCOMPARE(capCornerRamp(column, count), capCornerRamp(count - 1 - column, count));
            QVERIFY(capCornerRamp(column, count) >= 0.0);
            QVERIFY(capCornerRamp(column, count) <= 1.0);
        }
    }
}

void CapTest::theContourNoiseIsTheSameEveryTimeItIsAsked()
{
    // A frame is not allowed to change the shape of a pile that did not change:
    // noise that is re-rolled per frame boils rather than settles.
    for (int column = 0; column < 200; ++column) {
        QCOMPARE(capContourNoise(column), capContourNoise(column));
        QVERIFY(capContourNoise(column) >= 0.0);
        QVERIFY(capContourNoise(column) <= 1.0);
    }
}

void CapTest::theContourNoiseDoesNotFollowTheColumnCount()
{
    // The whole reason it is a function of the Column index: a Snowline
    // re-spans on resize, anchored at Column 0, so Column 7 has to keep both
    // its depth and the bump drawn on top of it whatever the Catcher's width
    // becomes (ADR-0001).
    const Snowline wide = levelSnowline(400, 5.0);
    const Snowline narrow = levelSnowline(100, 5.0);

    const QList<qreal> wideContour = capContour(wide, CapStyle::Contour, false);
    const QList<qreal> narrowContour = capContour(narrow, CapStyle::Contour, false);

    for (int column = 0; column < narrowContour.size(); ++column) {
        QCOMPARE(narrowContour.at(column), wideContour.at(column));
    }
}

void CapTest::theContourNoiseUsesItsWholeRange()
{
    qreal lowest = 1.0;
    qreal highest = 0.0;
    qreal total = 0;
    constexpr int columns = 1000;

    for (int column = 0; column < columns; ++column) {
        const qreal noise = capContourNoise(column);
        lowest = std::min(lowest, noise);
        highest = std::max(highest, noise);
        total += noise;
    }

    // Spread over the range rather than clustered: an offset that never reaches
    // its ends is a smaller offset, and one that leans to an end is a pile that
    // grew or shrank.
    QVERIFY(lowest < 0.02);
    QVERIFY(highest > 0.98);
    QVERIFY(std::abs(total / columns - 0.5) < 0.02);
}

void CapTest::theShadedStyleDrawsWhatIsStandingThere()
{
    const Snowline snowline = levelSnowline(s_catcherWidth, 6.0);
    const QList<qreal> contour = capContour(snowline, CapStyle::Shaded, true);

    for (int column = 0; column < snowline.columnCount(); ++column) {
        // Exactly the surface Catcher::surfaceAt() lands Flakes on: `shaded`
        // adds nothing to the depths, only shading to the band.
        QCOMPARE(contour.at(column), 6.0 * capCornerRamp(column, snowline.columnCount()));
    }
}

void CapTest::theContourStyleNudgesEveryColumn()
{
    const Snowline snowline = levelSnowline(s_catcherWidth, 6.0);
    const QList<qreal> shaded = capContour(snowline, CapStyle::Shaded, true);
    const QList<qreal> contour = capContour(snowline, CapStyle::Contour, true);

    int nudged = 0;
    for (int column = 0; column < snowline.columnCount(); ++column) {
        // Never far enough to read as a different depth -- under a third either
        // way -- and never below bare.
        QVERIFY(contour.at(column) >= shaded.at(column) * 0.7);
        QVERIFY(contour.at(column) <= shaded.at(column) * 1.2);
        QVERIFY(contour.at(column) >= 0.0);
        if (!qFuzzyCompare(contour.at(column), shaded.at(column))) {
            ++nudged;
        }
    }

    // A level Snowline comes out uneven, which is the whole point of the style.
    QCOMPARE(nudged, snowline.columnCount());
}

void CapTest::aCapIsDrawnOverEveryColumnOfItsSnowline()
{
    const Snowline snowline = levelSnowline(s_catcherWidth, 6.0);

    QCOMPARE(capContour(snowline, CapStyle::Shaded, true).size(), snowline.columnCount());
    QCOMPARE(capContour(snowline, CapStyle::Contour, false).size(), snowline.columnCount());

    // The ground has no corners to ramp toward: it is the bottom edge of a
    // whole output, and a Cap that faded out at the sides of the screen would
    // look like snow that had been swept.
    const QList<qreal> square = capContour(snowline, CapStyle::Shaded, false);
    QCOMPARE(square.first(), 6.0);
    QCOMPARE(square.last(), 6.0);
}

void CapTest::aDustingIsNotWorthDrawing()
{
    QVERIFY(!capIsVisible(levelSnowline(s_catcherWidth, 0.0)));
    QVERIFY(!capIsVisible(levelSnowline(s_catcherWidth, s_capVisibleDepth / 2)));
    QVERIFY(capIsVisible(levelSnowline(s_catcherWidth, s_capVisibleDepth)));

    // One pile is enough. The question is whether anything shows, not how much
    // snow a Snowline is holding in total.
    Snowline speck(s_catcherWidth);
    speck.setDepth(7, 4.0);
    QVERIFY(capIsVisible(speck));
}

void CapTest::theHeadroomCoversTheDeepestACapCanBeDrawn()
{
    const Settings settings;
    const qreal headroom = capHeadroom(settings.maxDepth);

    // The strip prePaintWindow() adds has to cover every pixel a Cap can put
    // in it, including the lift the `contour` noise can give a Column that is
    // already at the depth cap and the half of the leading edge's line drawn
    // above the contour. Short here means a Cap with its top cut off.
    const Snowline full = levelSnowline(s_catcherWidth, settings.maxDepth);
    for (const CapStyle style : {CapStyle::Shaded, CapStyle::Contour}) {
        for (const qreal depth : capContour(full, style, false)) {
            QVERIFY(depth + s_capEdgeWidth / 2 <= headroom);
        }
    }
}

void CapTest::aCapIsDrawnAgainstTheSnowThatIsStanding()
{
    const Settings settings;

    // Ordinarily the configured cap, because no Column can be over it.
    const Snowline full = levelSnowline(s_catcherWidth, settings.maxDepth);
    QCOMPARE(capReferenceDepth(full, settings.maxDepth), qreal(settings.maxDepth));
    QCOMPARE(capReferenceDepth(levelSnowline(s_catcherWidth, 2.0), settings.maxDepth),
             qreal(settings.maxDepth));

    // Lowering `maxDepth` leaves snow standing over it for as long as the melt
    // takes, and what is drawn follows that rather than the new cap: the
    // headroom the strip is cut to still covers it, and the shading still runs
    // from above the snow to inside the Catcher.
    const Snowline deep = levelSnowline(s_catcherWidth, 30.0);
    const qreal reference = capReferenceDepth(deep, 5.0);
    QCOMPARE(reference, 30.0);

    const qreal headroom = capHeadroom(reference);
    for (const CapStyle style : {CapStyle::Shaded, CapStyle::Contour}) {
        for (const qreal depth : capContour(deep, style, false)) {
            QVERIFY(depth + s_capEdgeWidth / 2 <= headroom);
        }
    }
    QVERIFY(capProfileTop(reference) < -deep.maximumDepth());
    QVERIFY(capProfileTop(reference) + capProfileSpan(reference) > s_capUnderlap);
}

void CapTest::theProfileRunsFromAboveTheSnowToInsideTheCatcher()
{
    const Settings settings;

    // Above the depth cap at its top, so a full Cap has its bright end to show,
    // and past the Catcher's own top edge at its bottom, so the underlap that
    // hides the seam is shaded rather than left flat.
    QVERIFY(capProfileTop(settings.maxDepth) < -settings.maxDepth);
    QVERIFY(capProfileTop(settings.maxDepth) + capProfileSpan(settings.maxDepth) > s_capUnderlap);
}

void CapTest::theProfileImageIsAnOpaqueBandAndABrightEdge()
{
    const QImage profile = capProfileImage();

    QCOMPARE(profile.width(), s_capProfileColumns);
    QCOMPARE(profile.height(), s_capProfileHeight);
    QCOMPARE(profile.format(), QImage::Format_ARGB32_Premultiplied);

    for (int row = 0; row < profile.height(); ++row) {
        // The band is opaque: it is snow, and whatever is behind a pile of snow
        // is behind a pile of snow.
        QCOMPARE(qAlpha(profile.pixel(s_capBodyColumn, row)), 255);
        // The leading edge is a highlight laid over it, so not quite.
        QCOMPARE(qAlpha(profile.pixel(s_capEdgeColumn, row)), 242);
        // And flat, because it follows the contour rather than the screen.
        QCOMPARE(profile.pixel(s_capEdgeColumn, row), profile.pixel(s_capEdgeColumn, 0));
    }
}

void CapTest::theProfileImageDarkensDownward()
{
    const QImage profile = capProfileImage();

    const QRgb top = profile.pixel(s_capBodyColumn, 0);
    const QRgb bottom = profile.pixel(s_capBodyColumn, profile.height() - 1);

    // The shadow under the leading edge is the whole of what makes a Cap read
    // as a pile with a lit top rather than as a white stripe.
    QVERIFY(qGray(bottom) < qGray(top));
    // And it is the sky's colour rather than black, so blue survives further
    // down than red does.
    QVERIFY(qBlue(bottom) - qRed(bottom) > qBlue(top) - qRed(top));

    int previous = qGray(top);
    for (int row = 1; row < profile.height(); ++row) {
        const int here = qGray(profile.pixel(s_capBodyColumn, row));
        QVERIFY(here <= previous);
        previous = here;
    }
}

QTEST_GUILESS_MAIN(CapTest)

#include "captest.moc"
