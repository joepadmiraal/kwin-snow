/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    What a Snowline is and what it does to the snow in it, checked without a
    compositor: the geometry rules of ADR-0001, and then deposition, relaxation
    and melt. Everything here is pure arithmetic on a Catcher's own local X axis
    -- which is the point: if any of this needed a screen, the coordinates would
    not be local.

    The lifecycle rules that do need a compositor (minimise, desktop switch,
    Panel auto-hide, close) are checked in the nested session; see
    docs/development.md. Flakes actually finding a Snowline is in snowfalltest.
*/

#include "catcher.h"
#include "settings.h"
#include "snowline.h"

#include <QTest>

#include <cmath>

using namespace Snow;

// Depth values that are easy to recognise once they have been moved around:
// depth equals Column index, so the contents say where they are anchored.
static void seedPattern(Snowline &snowline)
{
    for (int column = 0; column < snowline.columnCount(); ++column) {
        snowline.setDepth(column, column);
    }
}

static Snowline seeded(qreal width)
{
    Snowline snowline(width);
    seedPattern(snowline);
    return snowline;
}

static Catcher seededCatcher(const QRectF &geometry)
{
    Catcher catcher(CatcherClass::Window, nullptr, nullptr, geometry);
    seedPattern(catcher.snowline());
    return catcher;
}

// The sum of every depth, which is what makes "relaxation moves snow about but
// never invents or loses any" something a test can state in one line.
static qreal totalSnow(const Snowline &snowline)
{
    qreal total = 0;
    for (int column = 0; column < snowline.columnCount(); ++column) {
        total += snowline.depth(column);
    }
    return total;
}

// One frame at the effect's default 30 fps, the rate the relaxation constants
// are stated against.
static constexpr qreal s_frame = 1.0 / 30.0;

// The spec's defaults, which is what the numbers below are worked out from.
static constexpr qreal s_maxDepth = 20.0;

class SnowlineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void spansTheCatcherInWholeColumns();
    void startsBare();
    void columnsAreIndexedInLocalX();
    void growingZeroExtendsOnTheRight();
    void shrinkingTruncatesOnTheRight();
    void resizingNeverRescales();
    void resizingWithinOneColumnChangesNothing();
    void movingACatcherLeavesItsSnowlineAlone();
    void movingACatcherCarriesItsSnowAlong();
    void resizingACatcherAnchorsAtItsLeftEdge();

    void depositMoundsTheColumnAndItsNeighbours();
    void depositOverTheEdgeFallsOff();
    void depositIsDiscardedAtTheMaximumDepth();
    void relaxationSpreadsASpikeIntoItsNeighbours();
    void relaxationNeverInventsOrLosesSnow();
    void relaxationLeavesASettledSlopeAlone();
    void relaxationHasNoPreferredDirection();
    void relaxationGoesAtTheSameSpeedWhateverTheFrameRate();
    void relaxationNeverOvershoots();
    void meltDecaysEveryColumnDownToBare();
    void meltRateZeroIsPermanent();
    void aLoweredMaximumDepthIsMeltedDownToRatherThanCutTo();
    void aDisabledClassMeltsFasterInsteadOfBeingCleared();
    void surfaceIsTheTopEdgeRaisedByItsSnow();
    void theSurfaceRampsToNothingTowardACorner();
    void theGroundHasNoCornersToRampToward();
    void solidityFollowsTheClassItBelongsTo();
};

void SnowlineTest::spansTheCatcherInWholeColumns()
{
    QCOMPARE(Snowline(100).columnCount(), 20); // 100 / 5
    QCOMPARE(Snowline(101).columnCount(), 21); // the last sliver gets a Column
    QCOMPARE(Snowline(0).columnCount(), 1);    // degenerate, but never empty
}

void SnowlineTest::startsBare()
{
    const Snowline snowline(100);
    QVERIFY(snowline.isEmpty());
    QCOMPARE(snowline.maximumDepth(), 0.0);
    QCOMPARE(snowline.depth(0), 0.0);
    QCOMPARE(snowline.depth(19), 0.0);
}

void SnowlineTest::columnsAreIndexedInLocalX()
{
    const Snowline snowline(100);

    QCOMPARE(snowline.columnAt(0), 0);
    QCOMPARE(snowline.columnAt(4.999), 0);
    QCOMPARE(snowline.columnAt(5), 1);
    QCOMPARE(snowline.columnAt(99.9), 19);

    // The right edge belongs to the next Catcher along, not to this one.
    QCOMPARE(snowline.columnAt(100), -1);
    QCOMPARE(snowline.columnAt(-0.1), -1);

    QCOMPARE(snowline.columnCentre(0), 2.5);
    QCOMPARE(snowline.columnCentre(19), 97.5);
}

void SnowlineTest::growingZeroExtendsOnTheRight()
{
    Snowline snowline = seeded(100);
    snowline.setWidth(150);

    QCOMPARE(snowline.columnCount(), 30);
    for (int column = 0; column < 20; ++column) {
        QCOMPARE(snowline.depth(column), qreal(column)); // untouched
    }
    for (int column = 20; column < 30; ++column) {
        QCOMPARE(snowline.depth(column), 0.0); // bare, not stretched
    }
}

void SnowlineTest::shrinkingTruncatesOnTheRight()
{
    Snowline snowline = seeded(100);
    snowline.setWidth(50);

    QCOMPARE(snowline.columnCount(), 10);
    for (int column = 0; column < 10; ++column) {
        QCOMPARE(snowline.depth(column), qreal(column));
    }

    // Snow past the new right edge is gone, and growing back does not invent it.
    snowline.setWidth(100);
    QCOMPARE(snowline.columnCount(), 20);
    QCOMPARE(snowline.depth(9), 9.0);
    QCOMPARE(snowline.depth(10), 0.0);
}

void SnowlineTest::resizingNeverRescales()
{
    // A pile at one end has to stay at that end. Rescaling would drag it
    // inward, which is the smear ADR-0001 rejects.
    Snowline snowline(100);
    snowline.setDepth(19, 12);

    snowline.setWidth(300);
    QCOMPARE(snowline.depth(19), 12.0);
    QCOMPARE(snowline.maximumDepth(), 12.0); // one Column deep, not spread over six
    QCOMPARE(snowline.depth(59), 0.0);
}

void SnowlineTest::resizingWithinOneColumnChangesNothing()
{
    Snowline snowline = seeded(98); // 20 Columns, the last one partial
    const Snowline before = snowline;

    snowline.setWidth(96); // still 20 Columns
    QVERIFY(snowline == before);

    snowline.setWidth(100); // and still 20
    QVERIFY(snowline == before);
}

void SnowlineTest::movingACatcherLeavesItsSnowlineAlone()
{
    Catcher catcher = seededCatcher(QRectF(300, 400, 100, 60));
    const Snowline before = catcher.snowline();

    catcher.setGeometry(QRectF(900, 120, 100, 60)); // dragged, same size
    QVERIFY(catcher.snowline() == before);

    // Including across outputs: a Snowline is in logical pixels, so the scale
    // factor of wherever it landed never enters into it.
    catcher.setOutput(nullptr);
    catcher.setGeometry(QRectF(-1920, 0, 100, 60));
    QVERIFY(catcher.snowline() == before);
}

void SnowlineTest::movingACatcherCarriesItsSnowAlong()
{
    Catcher catcher = seededCatcher(QRectF(300, 400, 100, 60));

    // Column 4 sits 22.5 logical px into the Catcher wherever the Catcher is.
    QCOMPARE(catcher.columnAt(322.5), 4);
    QCOMPARE(catcher.columnAt(299), -1);
    QCOMPARE(catcher.columnAt(400), -1);

    catcher.setGeometry(QRectF(900, 400, 100, 60));
    QCOMPARE(catcher.columnAt(922.5), 4);
    QCOMPARE(catcher.columnAt(322.5), -1); // the snow went with the window
    QCOMPARE(catcher.snowline().depth(4), 4.0);
}

void SnowlineTest::resizingACatcherAnchorsAtItsLeftEdge()
{
    Catcher catcher = seededCatcher(QRectF(300, 400, 100, 60));

    catcher.setGeometry(QRectF(300, 400, 160, 60)); // right edge dragged out
    QCOMPARE(catcher.snowline().columnCount(), 32);
    QCOMPARE(catcher.snowline().depth(19), 19.0);
    QCOMPARE(catcher.snowline().depth(20), 0.0);

    // A left-edge drag moves the anchor with the frame: local Column 0 is
    // still the leftmost Column, so the snow slides with the edge. ADR-0001
    // takes that over rescaling.
    catcher.setGeometry(QRectF(240, 400, 220, 60));
    QCOMPARE(catcher.snowline().columnCount(), 44);
    QCOMPARE(catcher.columnAt(240), 0);
    QCOMPARE(catcher.snowline().depth(19), 19.0);
}

void SnowlineTest::depositMoundsTheColumnAndItsNeighbours()
{
    Snowline snowline(100);
    snowline.deposit(5, 1.0, s_maxDepth);

    // 0.62 in the middle and 0.19 either side: a landing is a small mound, not
    // a single-Column spike, and the three shares are a whole Flake between them.
    QCOMPARE(snowline.depth(4), 0.19);
    QCOMPARE(snowline.depth(5), 0.62);
    QCOMPARE(snowline.depth(6), 0.19);
    QCOMPARE(totalSnow(snowline), 1.0);
    QCOMPARE(snowline.depth(3), 0.0);
}

void SnowlineTest::depositOverTheEdgeFallsOff()
{
    Snowline snowline(100);

    snowline.deposit(0, 1.0, s_maxDepth);
    QCOMPARE(snowline.depth(0), 0.62);
    QCOMPARE(snowline.depth(1), 0.19);
    QCOMPARE(totalSnow(snowline), 0.81); // the share over the left edge is gone

    // And a landing that misses the Catcher entirely leaves nothing behind.
    snowline.deposit(-1, 1.0, s_maxDepth);
    snowline.deposit(20, 1.0, s_maxDepth);
    QCOMPARE(totalSnow(snowline), 0.81);
}

void SnowlineTest::depositIsDiscardedAtTheMaximumDepth()
{
    Snowline snowline(100);
    snowline.setDepth(5, s_maxDepth);
    snowline.setDepth(4, s_maxDepth - 0.1);

    snowline.deposit(5, 1.0, s_maxDepth);

    // Full is full: the excess is discarded rather than shed onto whatever is
    // below (spec: Model), and it does not sit in the Column waiting for the
    // melt to let it in either.
    QCOMPARE(snowline.depth(5), s_maxDepth);
    QCOMPARE(snowline.depth(4), s_maxDepth);
    QCOMPARE(snowline.depth(6), 0.19);

    snowline.melt(1.0, 1.0);
    QCOMPARE(snowline.depth(5), s_maxDepth - 1.0);
}

void SnowlineTest::relaxationSpreadsASpikeIntoItsNeighbours()
{
    Snowline snowline(25); // five Columns
    snowline.setDepth(2, 10);

    snowline.relax(s_frame);

    // 0.16 of the 10 px difference each way, at 30 fps.
    QCOMPARE(snowline.depth(1), 1.6);
    QCOMPARE(snowline.depth(2), 6.8);
    QCOMPARE(snowline.depth(3), 1.6);
}

void SnowlineTest::relaxationNeverInventsOrLosesSnow()
{
    Snowline snowline(100);
    for (int column = 0; column < 20; ++column) {
        snowline.deposit(column % 7, 3.0, s_maxDepth);
    }
    const qreal before = totalSnow(snowline);

    for (int frame = 0; frame < 300; ++frame) {
        snowline.relax(s_frame);
    }

    QVERIFY(qAbs(totalSnow(snowline) - before) < 1e-9);
    QVERIFY(snowline.maximumDepth() < before / 3); // and it did spread out
}

void SnowlineTest::relaxationLeavesASettledSlopeAlone()
{
    // A step of 1.5 px is under the 1.6 px angle of repose, so this is what a
    // pile is allowed to look like once it has settled.
    Snowline snowline(25);
    for (int column = 0; column < 5; ++column) {
        snowline.setDepth(column, column * 1.5);
    }
    const Snowline before = snowline;

    snowline.relax(s_frame);

    QVERIFY(snowline == before);
}

void SnowlineTest::relaxationHasNoPreferredDirection()
{
    // Reading the array as it is written would let snow cascade one way and not
    // the other, which over a minute walks a pile sideways.
    Snowline snowline(50); // ten Columns
    snowline.setDepth(4, 12);
    snowline.setDepth(5, 12);

    for (int frame = 0; frame < 600; ++frame) {
        snowline.relax(s_frame);
    }

    for (int column = 0; column < 5; ++column) {
        QVERIFY(qAbs(snowline.depth(column) - snowline.depth(9 - column)) < 1e-9);
    }
}

void SnowlineTest::relaxationGoesAtTheSameSpeedWhateverTheFrameRate()
{
    Snowline slow(25);
    slow.setDepth(2, 10);
    slow.relax(s_frame);

    Snowline fast(25);
    fast.setDepth(2, 10);
    fast.relax(s_frame / 2);

    // Half the frame, half the settling: the rate is per second, so a machine
    // running at 60 fps settles its snow at the same speed as one at 30.
    QCOMPARE(10.0 - fast.depth(2), (10.0 - slow.depth(2)) / 2);
}

void SnowlineTest::relaxationNeverOvershoots()
{
    // A stalled compositor hands the effect a long frame. However long it is,
    // a Column must not be driven below the neighbours it is shedding to and
    // start ringing between them.
    Snowline snowline(25);
    snowline.setDepth(2, 10);

    snowline.relax(10.0);

    QVERIFY(snowline.depth(2) > snowline.depth(1));
    QVERIFY(snowline.depth(2) > snowline.depth(3));
    QCOMPARE(totalSnow(snowline), 10.0);
}

void SnowlineTest::meltDecaysEveryColumnDownToBare()
{
    Snowline snowline(25);
    snowline.setDepth(0, 5);
    snowline.setDepth(1, 0.1);

    snowline.melt(0.5, 0.4); // 0.4 px/s for half a second

    QCOMPARE(snowline.depth(0), 4.8);
    QCOMPARE(snowline.depth(1), 0.0); // bare, never negative

    snowline.melt(60, 0.4);
    QVERIFY(snowline.isEmpty());
}

void SnowlineTest::meltRateZeroIsPermanent()
{
    Snowline snowline = seeded(100);
    const Snowline before = snowline;

    snowline.melt(3600, 0);

    QVERIFY(snowline == before);
}

void SnowlineTest::aLoweredMaximumDepthIsMeltedDownToRatherThanCutTo()
{
    Snowline snowline(100);
    snowline.setDepth(5, 30);
    snowline.setDepth(6, 21);

    // The cap comes down to 20 while 30 px of snow is standing. Landing a Flake
    // on it is the one thing that could cut it down to the new cap, and it does
    // not: what is there is left for the melt (spec: Behaviour, "Settings
    // changed"). The Column just over the cap is left alone too.
    snowline.deposit(5, 1.0, 20.0);
    QCOMPARE(snowline.depth(5), 30.0);
    QCOMPARE(snowline.depth(6), 21.0);

    // Its neighbour on the other side is under the cap and takes its share as
    // usual, up to the new cap and no further.
    QCOMPARE(snowline.depth(4), 0.19);

    // And the melt is what brings the two deep ones down, in the ten seconds
    // the configured rate says rather than in the instant a checkbox does.
    snowline.melt(10, 1.0);
    QCOMPARE(snowline.depth(5), 20.0);
    QCOMPARE(snowline.depth(6), 11.0);

    // Once it is back under the cap it takes snow again.
    snowline.deposit(6, 1.0, 20.0);
    QCOMPARE(snowline.depth(6), 11.62);
}

void SnowlineTest::aDisabledClassMeltsFasterInsteadOfBeingCleared()
{
    Settings settings;
    QCOMPARE(meltRateFor(true, settings.meltRate), settings.meltRate);
    QCOMPARE(meltRateFor(false, settings.meltRate), s_disabledMeltRate);
    // Never slower than it was melting anyway.
    QCOMPARE(meltRateFor(false, 40.0), 40.0);

    Catcher window(CatcherClass::Window, nullptr, nullptr, QRectF(0, 100, 100, 60));
    for (int column = 0; column < window.snowline().columnCount(); ++column) {
        window.snowline().setDepth(column, s_maxDepth);
    }

    // Switching the class off leaves the snow standing and sets it melting: a
    // second in, most of it is still there.
    settings.snowOnWindows = false;
    window.settle(1.0, settings);
    QVERIFY(!window.snowline().isEmpty());
    QCOMPARE(window.snowline().maximumDepth(), s_maxDepth - s_disabledMeltRate);

    // And a second and a half later there is none of it left: gone in under
    // three seconds, rather than in the fifty the configured rate would take.
    window.settle(1.5, settings);
    QVERIFY(window.snowline().isEmpty());
}

void SnowlineTest::surfaceIsTheTopEdgeRaisedByItsSnow()
{
    Catcher catcher(CatcherClass::Window, nullptr, nullptr, QRectF(300, 400, 100, 60));
    Settings settings;

    // Column 10 of 20, well clear of the corner ramp at either end, so this
    // measures the snow and nothing else.
    QCOMPARE(catcher.columnAt(352.5), 10);

    // Bare, a Flake lands on the frame's top edge itself.
    QCOMPARE(catcher.surfaceAt(10), 400.0);

    catcher.deposit(10, 10.0, settings);

    // With snow on it, the landing line has risen by the depth -- which is what
    // makes a pile build on itself instead of everything landing on the frame.
    QCOMPARE(catcher.surfaceAt(10), 400.0 - 6.2);
    QCOMPARE(catcher.surfaceAt(9), 400.0 - 1.9);
    QCOMPARE(catcher.surfaceAt(18), 400.0);
}

void SnowlineTest::theSurfaceRampsToNothingTowardACorner()
{
    Catcher catcher(CatcherClass::Window, nullptr, nullptr, QRectF(300, 400, 100, 60));
    for (int column = 0; column < catcher.snowline().columnCount(); ++column) {
        catcher.snowline().setDepth(column, 8.0);
    }

    // The same depth everywhere, and the surface still comes down to meet the
    // frame at both ends: a decoration's corners are rounded, and snow drawn
    // square across them would hang over nothing -- so it is not there to land
    // on either.
    QCOMPARE(catcher.surfaceAt(0), 400.0 - 8.0 * 0.125);
    QCOMPARE(catcher.surfaceAt(1), 400.0 - 8.0 * 0.375);
    QCOMPARE(catcher.surfaceAt(19), 400.0 - 8.0 * 0.125);
    QCOMPARE(catcher.surfaceAt(18), 400.0 - 8.0 * 0.375);

    // And is untouched in the middle.
    QCOMPARE(catcher.surfaceAt(4), 400.0 - 8.0);
    QCOMPARE(catcher.surfaceAt(15), 400.0 - 8.0);
}

void SnowlineTest::theGroundHasNoCornersToRampToward()
{
    Catcher ground(CatcherClass::Ground, nullptr, nullptr, QRectF(0, 1000, 100, 0));
    for (int column = 0; column < ground.snowline().columnCount(); ++column) {
        ground.snowline().setDepth(column, 8.0);
    }

    QVERIFY(!ground.hasRoundedCorners());
    QCOMPARE(ground.surfaceAt(0), 1000.0 - 8.0);
    QCOMPARE(ground.surfaceAt(19), 1000.0 - 8.0);
}

void SnowlineTest::solidityFollowsTheClassItBelongsTo()
{
    const Catcher window(CatcherClass::Window, nullptr, nullptr, QRectF(0, 100, 100, 60));
    const Catcher panel(CatcherClass::Panel, nullptr, nullptr, QRectF(0, 200, 100, 60));
    const Catcher ground(CatcherClass::Ground, nullptr, nullptr, QRectF(0, 300, 100, 0));

    Settings settings;
    QVERIFY(window.isSolid(settings));
    QVERIFY(panel.isSolid(settings));
    QVERIFY(ground.isSolid(settings));

    settings.snowOnPanels = false;
    QVERIFY(window.isSolid(settings));
    QVERIFY(!panel.isSolid(settings));
    QVERIFY(ground.isSolid(settings));
}

QTEST_GUILESS_MAIN(SnowlineTest)

#include "snowlinetest.moc"
