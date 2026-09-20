/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    The Flake simulation and what it lands on, checked without a compositor.

    A Snowfall is a rectangle, a seed, a list of Catchers and some arithmetic --
    none of it needs a screen -- so "dump Flake positions over time and see
    whether they behave", and now "leave it running and see where the snow
    settles", are things a test can do directly rather than things that need a
    compositor and a pair of eyes. What the eyes are still for is whether the
    motion *feels* right, which is what the nested session and the published
    prototype are for; see docs/development.md.

    What a Snowline does with the snow once it has it -- the shares of a
    deposit, relaxation, melt, the depth cap -- is in snowlinetest.
*/

#include "catcher.h"
#include "settings.h"
#include "snowfall.h"

#include <QTest>

#include <cmath>
#include <limits>

using namespace Snow;

// One nested-session output, away from the origin so that anything accidentally
// working in output-local coordinates shows up as being off by 1600.
static const QRectF s_output(1600, 0, 1600, 1000);

static Settings settings(int density = 5, int fallSpeed = 5, int windStrength = 4,
                         FlakeStyle style = FlakeStyle::Depth)
{
    Settings result;
    result.density = density;
    result.fallSpeed = fallSpeed;
    result.windStrength = windStrength;
    result.flakeStyle = style;
    return result;
}

// A fixed seed everywhere: the simulation is random, the test is not.
static constexpr quint32 s_seed = 20260916;

/** Run @a seconds of simulation at 30 fps, the effect's default frame rate. */
static void run(Snowfall &snowfall, qreal seconds, qreal from = 0)
{
    const qreal delta = 1.0 / 30.0;
    for (qreal elapsed = from; elapsed < from + seconds; elapsed += delta) {
        snowfall.step(delta, elapsed);
    }
}

/**
 * The same, with Catchers under it -- Flakes land, and the snow they leave
 * settles once a frame, which is what SnowEffect::prePaintScreen() does.
 */
static void run(Snowfall &snowfall, const Settings &config, const QList<Catcher *> &catchers,
                qreal seconds, qreal from = 0)
{
    // One set of settings reaches the Flakes and the Snowlines by two different
    // routes, exactly as SnowfallRegistry and CatcherRegistry hand them on.
    snowfall.setSettings(config);

    const qreal delta = 1.0 / 30.0;
    for (qreal elapsed = from; elapsed < from + seconds; elapsed += delta) {
        snowfall.step(delta, elapsed, catchers);
        for (Catcher *catcher : catchers) {
            catcher->settle(delta, config);
        }
    }
}

/** A window-class Catcher whose top edge runs the width of the output at @a top. */
static Catcher windowAt(qreal top)
{
    return Catcher(CatcherClass::Window, nullptr, nullptr,
                   QRectF(s_output.left(), top, s_output.width(), s_output.bottom() - top));
}

/**
 * A Catcher running right across the band Flakes live in, wider than the output
 * itself. Nothing can wrap round its ends or blow in past them, which is what
 * makes "no Flake is ever below this" a statement about landing rather than
 * about the Flakes that went round the side.
 */
static Catcher wallToWall(qreal top)
{
    return Catcher(CatcherClass::Window, nullptr, nullptr,
                   QRectF(s_output.left() - 200, top, s_output.width() + 400, s_output.bottom() - top));
}

/** The ground of the output: a zero-height strip along its bottom edge. */
static Catcher theGround()
{
    return Catcher(CatcherClass::Ground, nullptr, nullptr,
                   QRectF(s_output.left(), s_output.bottom(), s_output.width(), 0));
}

static qreal meanDepth(const Catcher &catcher)
{
    const Snowline &snowline = catcher.snowline();
    qreal total = 0;
    for (int column = 0; column < snowline.columnCount(); ++column) {
        total += snowline.depth(column);
    }
    return total / snowline.columnCount();
}

class SnowfallTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void populationIsPerUnitArea();
    void populationScalesWithDensity();
    void aRaisedDensitySpawnsRatherThanAppears();
    void aLoweredDensityStopsSpawningRatherThanVanishes();
    void startsScatteredDownTheOutput();
    void flakesVaryFromOneAnother();
    void flakesFall();
    void fallSpeedFollowsTheSetting();
    void depthStyleVariesFallSpeedByZ();
    void previousYIsWhereTheFlakeWas();
    void windGustsRatherThanDrifts();
    void swaySurvivesNoWind();
    void flakesNeverLeaveTheirOutput();
    void flakesWrapAroundTheSides();
    void flakesPassingTheBottomRespawnAboveTheTop();
    void aResizedOutputRetargetsAndRecaptures();
    void theSameSeedGivesTheSameSnow();

    void flakesLandOnACatcher();
    void landingDoesNotUseUpTheSnowfall();
    void aFlakeCrossingTheSurfaceInOneStepStillLands();
    void aThinCapDoesNotLeak();
    void blownInFlakesAreDrawnInFrontOfAWindowOrBehindIt();
    void aDisabledClassIsTransparent();
    void aConcealedCatcherIsTransparent();
    void theTopmostCatcherCatchesFirst();
    void theDepthStyleBuildsAPileMoreSlowly();
    void withoutMeltItFillsToTheMaximumDepthAndStops();
    void meltUnderTheSnowfallRateFillsToTheCap();
    void meltOverTheSnowfallRateLeavesItBare();
    void snowMeltsAwayOnceItStopsFalling();

    void aGroundAcrossTheTopWouldTakeTheWholeSnowfall();
};

void SnowfallTest::populationIsPerUnitArea()
{
    // The point of an area rate: the same density at 1080p and at 4K puts the
    // same number of Flakes over the same amount of screen.
    const Snowfall hd(QRectF(0, 0, 1920, 1080), settings(), s_seed);
    const Snowfall uhd(QRectF(0, 0, 3840, 2160), settings(), s_seed);

    QCOMPARE(hd.flakes().size(), hd.targetFlakeCount());
    // Four times the area, four times the snow, give or take where the two
    // roundings to a whole Flake land.
    QVERIFY(std::abs(uhd.targetFlakeCount() - 4 * hd.targetFlakeCount()) <= 2);

    // And the prototype's own figure, for its own canvas: density 5 over
    // 1600x1000 is 475 Flakes.
    QCOMPARE(Snowfall(s_output, settings(), s_seed).targetFlakeCount(), 475);
}

void SnowfallTest::populationScalesWithDensity()
{
    const int sparse = Snowfall(s_output, settings(1), s_seed).targetFlakeCount();
    const int dense = Snowfall(s_output, settings(10), s_seed).targetFlakeCount();

    QCOMPARE(dense, 10 * sparse);

    // A density change moves the target, and the population follows it without
    // a new Snowfall; the two tests below are about how it follows it.
    Snowfall snowfall(s_output, settings(1), s_seed);
    QCOMPARE(snowfall.targetFlakeCount(), sparse);
    snowfall.setSettings(settings(10));
    QCOMPARE(snowfall.targetFlakeCount(), dense);
}

void SnowfallTest::aRaisedDensitySpawnsRatherThanAppears()
{
    Snowfall snowfall(s_output, settings(1), s_seed);
    const int sparse = snowfall.flakes().size();

    snowfall.setSettings(settings(10));
    const int target = snowfall.targetFlakeCount();

    // Not in one step, and not in one frame's worth of them: ten times the snow
    // arriving at once would read as a flash rather than as more snow falling
    // (spec: Behaviour, "Settings changed").
    run(snowfall, 0.1);
    QVERIFY2(snowfall.flakes().size() < sparse + target / 10,
             qPrintable(QStringLiteral("%1 Flakes after a tenth of a second").arg(snowfall.flakes().size())));

    // The ones that have arrived came in from above the top edge, where nobody
    // was looking at the sky they appeared in.
    int inTheOutput = 0;
    for (const Flake &flake : snowfall.flakes()) {
        if (flake.y > s_output.top()) {
            ++inTheOutput;
        }
    }
    QCOMPARE(inTheOutput, sparse);

    // And it does get there: a few seconds, not a few minutes.
    run(snowfall, 5, 0.1);
    QCOMPARE(snowfall.flakes().size(), target);
}

void SnowfallTest::aLoweredDensityStopsSpawningRatherThanVanishes()
{
    Snowfall snowfall(s_output, settings(10), s_seed);
    const int dense = snowfall.flakes().size();

    snowfall.setSettings(settings(1));
    const int target = snowfall.targetFlakeCount();

    // A step does not pluck Flakes out of the air: the population only comes
    // down as Flakes finish falling and are not put back, so after a tenth of a
    // second almost all of them are still there.
    run(snowfall, 0.1);
    QVERIFY2(snowfall.flakes().size() > dense - dense / 10,
             qPrintable(QStringLiteral("%1 Flakes of %2 after a tenth of a second")
                            .arg(snowfall.flakes().size())
                            .arg(dense)));

    // It takes about as long as a Flake takes to fall down the output, because
    // that is what it is waiting for.
    run(snowfall, 10, 0.1);
    QCOMPARE(snowfall.flakes().size(), target);
}

void SnowfallTest::startsScatteredDownTheOutput()
{
    // Switching the effect on should look like snow that has been falling for a
    // while, so the first population is spread over the output rather than
    // queued above it.
    const Snowfall snowfall(s_output, settings(), s_seed);

    int aboveTheMiddle = 0;
    for (const Flake &flake : snowfall.flakes()) {
        QVERIFY(flake.y >= s_output.top());
        QVERIFY(flake.y <= s_output.bottom());
        if (flake.y < s_output.center().y()) {
            ++aboveTheMiddle;
        }
    }

    const int count = snowfall.flakes().size();
    QVERIFY(aboveTheMiddle > count / 4);
    QVERIFY(aboveTheMiddle < 3 * count / 4);
}

void SnowfallTest::flakesVaryFromOneAnother()
{
    // A few hundred Flakes sharing a size, a sway or a response to the wind
    // read as one sheet rather than as snow.
    const Snowfall snowfall(s_output, settings(), s_seed);
    const QList<Flake> &flakes = snowfall.flakes();
    QVERIFY(flakes.size() > 100);

    const Flake &first = flakes.first();
    bool differs = false;
    for (const Flake &flake : flakes) {
        QVERIFY(flake.z >= 0 && flake.z <= 1);
        QVERIFY(flake.radius > 0);
        QVERIFY(flake.swayFrequency > 0);
        QVERIFY(flake.windResponse >= 0 && flake.windResponse <= 1);
        differs = differs
            || flake.z != first.z
            || flake.radius != first.radius
            || flake.windResponse != first.windResponse
            || flake.rotationSpeed != first.rotationSpeed;
    }
    QVERIFY(differs);
}

void SnowfallTest::flakesFall()
{
    Snowfall snowfall(s_output, settings(), s_seed);
    const QList<Flake> before = snowfall.flakes();

    snowfall.step(0.1, 0);

    const QList<Flake> &after = snowfall.flakes();
    QCOMPARE(after.size(), before.size());
    for (int index = 0; index < after.size(); ++index) {
        QVERIFY(after.at(index).y > before.at(index).y);
    }
}

void SnowfallTest::fallSpeedFollowsTheSetting()
{
    // Blob, so every Flake falls at the setting's speed and the step is exact:
    // 40 + fallSpeed * 34 logical px/s.
    for (const int fallSpeed : {1, 5, 10}) {
        Snowfall snowfall(s_output, settings(5, fallSpeed, 0, FlakeStyle::Blob), s_seed);
        const qreal before = snowfall.flakes().first().y;

        snowfall.step(1.0, 0);

        QCOMPARE(snowfall.flakes().first().y - before, 40 + fallSpeed * 34.0);
    }
}

void SnowfallTest::depthStyleVariesFallSpeedByZ()
{
    // The parallax the `depth` style is named for: near Flakes overtake far
    // ones instead of the whole field descending in lockstep.
    Snowfall depth(s_output, settings(5, 5, 0, FlakeStyle::Depth), s_seed);
    Snowfall blob(s_output, settings(5, 5, 0, FlakeStyle::Blob), s_seed);

    const QList<Flake> before = depth.flakes();
    const qreal delta = 0.1;
    depth.step(delta, 0);
    blob.step(delta, 0);

    const qreal uniform = (40 + 5 * 34.0) * delta;
    qreal slowest = std::numeric_limits<qreal>::max();
    qreal fastest = 0;
    int compared = 0;
    for (int index = 0; index < before.size(); ++index) {
        const qreal depthStep = depth.flakes().at(index).y - before.at(index).y;
        const qreal blobStep = blob.flakes().at(index).y - before.at(index).y;
        if (depthStep <= 0 || blobStep <= 0) {
            continue; // this one went off the bottom and came back at the top
        }

        // Same seed, so the two Snowfalls hold the same Flakes: only the style
        // differs, and under `blob` it makes no difference at all.
        QCOMPARE(blobStep, uniform);
        slowest = std::min(slowest, depthStep);
        fastest = std::max(fastest, depthStep);
        ++compared;
    }

    QVERIFY(compared > 100);
    QVERIFY(fastest > slowest * 1.5);
    QVERIFY(slowest < uniform);
    QVERIFY(fastest > uniform);
}

void SnowfallTest::previousYIsWhereTheFlakeWas()
{
    // Ticket 05 tests the segment previousY..y against a Catcher's top surface,
    // so it has to be the previous step's y and not this one's.
    Snowfall snowfall(s_output, settings(), s_seed);
    snowfall.step(0.1, 0);
    const QList<Flake> before = snowfall.flakes();

    snowfall.step(0.1, 0.1);

    int compared = 0;
    for (int index = 0; index < snowfall.flakes().size(); ++index) {
        const Flake &flake = snowfall.flakes().at(index);
        if (flake.previousY == flake.y) {
            continue; // respawned: it has not travelled from anywhere
        }
        QCOMPARE(flake.previousY, before.at(index).y);
        QVERIFY(flake.previousY < flake.y);
        ++compared;
    }
    QVERIFY(compared > 100);
}

void SnowfallTest::windGustsRatherThanDrifts()
{
    // Not a constant sideways drift (spec: Configuration): the wind blows both
    // ways over a couple of minutes and averages out to about nothing.
    qreal total = 0;
    qreal strongestLeft = 0;
    qreal strongestRight = 0;
    for (int tick = 0; tick < 12000; ++tick) {
        const qreal gust = Snowfall::gustAt(tick / 100.0);
        QVERIFY(gust >= -1 && gust <= 1);
        total += gust;
        strongestLeft = std::min(strongestLeft, gust);
        strongestRight = std::max(strongestRight, gust);
    }

    QVERIFY(std::abs(total / 12000) < 0.05);
    QVERIFY(strongestLeft < -0.7);
    QVERIFY(strongestRight > 0.7);

    // And a Flake actually picks it up: at full strength the field is pushed
    // sideways, and the Flakes that respond most are pushed furthest.
    Snowfall snowfall(s_output, settings(5, 5, 10, FlakeStyle::Blob), s_seed);
    const QList<Flake> before = snowfall.flakes();
    snowfall.step(0.1, 5.0); // gustAt(5) is a strong right-hand gust

    qreal spread = 0;
    for (int index = 0; index < snowfall.flakes().size(); ++index) {
        const qreal moved = snowfall.flakes().at(index).x - before.at(index).x;
        spread = std::max(spread, std::abs(moved));
        QVERIFY(moved > 0); // the whole field goes with the gust
    }
    QVERIFY(spread > 0);
}

void SnowfallTest::swaySurvivesNoWind()
{
    // windStrength 0 stills the gusts, not the Flakes: each one keeps its own
    // flutter, and they do not all flutter the same way.
    Snowfall snowfall(s_output, settings(5, 5, 0, FlakeStyle::Blob), s_seed);
    const QList<Flake> before = snowfall.flakes();

    snowfall.step(0.1, 1.0);

    bool left = false;
    bool right = false;
    for (int index = 0; index < snowfall.flakes().size(); ++index) {
        const qreal moved = snowfall.flakes().at(index).x - before.at(index).x;
        left = left || moved < 0;
        right = right || moved > 0;
    }
    QVERIFY(left);
    QVERIFY(right);
}

void SnowfallTest::flakesNeverLeaveTheirOutput()
{
    // The rule the per-output simulation exists for (spec: Rendering): whatever
    // the wind does, no Flake ends up over the next output along, where it
    // would be snow falling in mid-air.
    Snowfall snowfall(s_output, settings(10, 10, 10), s_seed);

    // A margin of a couple of Flake radii outside the edges is the wrap band,
    // which is off-screen; anything beyond it has escaped.
    const qreal slack = 120;
    run(snowfall, 60);

    for (const Flake &flake : snowfall.flakes()) {
        QVERIFY(flake.x >= s_output.left() - slack);
        QVERIFY(flake.x <= s_output.right() + slack);
        QVERIFY(flake.y <= s_output.bottom() + 40);
    }
}

void SnowfallTest::flakesWrapAroundTheSides()
{
    // A Flake blown off one side comes back on the other rather than vanishing,
    // so a strong wind does not empty the upwind edge of the output.
    Snowfall snowfall(s_output, settings(5, 1, 10, FlakeStyle::Blob), s_seed);

    bool wrapped = false;
    const qreal delta = 1.0 / 30.0;
    for (int tick = 0; tick < 3000 && !wrapped; ++tick) {
        const QList<Flake> before = snowfall.flakes();
        snowfall.step(delta, tick * delta);
        for (int index = 0; index < snowfall.flakes().size(); ++index) {
            // Nothing else can move a Flake a whole output's width sideways.
            if (std::abs(snowfall.flakes().at(index).x - before.at(index).x) > s_output.width()) {
                wrapped = true;
            }
        }
    }

    QVERIFY(wrapped);
    QCOMPARE(snowfall.flakes().size(), snowfall.targetFlakeCount());
}

void SnowfallTest::flakesPassingTheBottomRespawnAboveTheTop()
{
    Snowfall snowfall(s_output, settings(5, 10, 0, FlakeStyle::Blob), s_seed);
    const int count = snowfall.flakes().size();

    // Long enough that the Flake that started lowest has gone off the bottom.
    run(snowfall, 5);

    QCOMPARE(snowfall.flakes().size(), count); // respawned, not lost
    int recycled = 0;
    for (const Flake &flake : snowfall.flakes()) {
        if (flake.previousY == flake.y) {
            // Respawned on this very step: above the top edge, and starting
            // there rather than arriving from wherever it died. A respawn that
            // kept previousY would hand ticket 05 a segment the length of the
            // screen and land the Flake on the first Catcher below it.
            ++recycled;
            QVERIFY(flake.y < s_output.top());
        }
    }
    QVERIFY(recycled > 0);
}

void SnowfallTest::aResizedOutputRetargetsAndRecaptures()
{
    Snowfall snowfall(s_output, settings(5, 5, 0, FlakeStyle::Blob), s_seed);
    QCOMPARE(snowfall.flakes().size(), 475);

    // Half the output goes away: the Flakes that were over the half that is
    // gone are wrapped back in on the next step rather than left hanging
    // beyond the new edge.
    const QRectF smaller(1600, 0, 800, 1000);
    snowfall.setGeometry(smaller);
    snowfall.step(1.0 / 30.0, 0);

    QCOMPARE(snowfall.geometry(), smaller);
    QCOMPARE(snowfall.targetFlakeCount(), 238); // half the area, half the snow

    // The population follows the area the same way it follows `density`: by
    // attrition rather than by half the snow blinking out of the air.
    QCOMPARE(snowfall.flakes().size(), 475);
    run(snowfall, 10, 1.0 / 30.0);
    QCOMPARE(snowfall.flakes().size(), 238);

    for (const Flake &flake : snowfall.flakes()) {
        QVERIFY(flake.x >= smaller.left() - 120);
        QVERIFY(flake.x <= smaller.right() + 120);
    }
}

void SnowfallTest::theSameSeedGivesTheSameSnow()
{
    // Two outputs of the same size get different seeds, so the same snow does
    // not fall down both of them; that only works if the seed is what decides.
    Snowfall one(s_output, settings(), 1);
    Snowfall two(s_output, settings(), 2);
    Snowfall alsoOne(s_output, settings(), 1);

    run(one, 2);
    run(two, 2);
    run(alsoOne, 2);

    QCOMPARE(one.flakes().size(), two.flakes().size());
    QCOMPARE(one.flakes().first().x, alsoOne.flakes().first().x);
    QCOMPARE(one.flakes().first().y, alsoOne.flakes().first().y);
    QVERIFY(one.flakes().first().x != two.flakes().first().x);
}

void SnowfallTest::flakesLandOnACatcher()
{
    Settings config = settings();
    config.meltRate = 0;

    Catcher ground = theGround();
    QList<Catcher *> catchers{&ground};
    Snowfall snowfall(s_output, config, s_seed);

    QVERIFY(ground.snowline().isEmpty());
    run(snowfall, config, catchers, 20);

    // Twenty seconds of snow over the whole width of the output: every Column
    // of the ground should have had some, not just the ones under wherever the
    // population happened to start.
    QVERIFY(!ground.snowline().isEmpty());
    for (int column = 0; column < ground.snowline().columnCount(); ++column) {
        QVERIFY2(ground.snowline().depth(column) > 0,
                 qPrintable(QStringLiteral("Column %1 is bare").arg(column)));
    }
}

void SnowfallTest::landingDoesNotUseUpTheSnowfall()
{
    Settings config = settings();
    config.meltRate = 0;

    Catcher window = wallToWall(500);
    QList<Catcher *> catchers{&window};
    Snowfall snowfall(s_output, config, s_seed);

    run(snowfall, config, catchers, 10);
    const qreal firstTenSeconds = meanDepth(window);

    run(snowfall, config, catchers, 10, 10);
    const qreal secondTenSeconds = meanDepth(window) - firstTenSeconds;

    // The population is what `density` says it is whether or not anything is
    // catching, and the snow goes on arriving at the same rate: a Flake that
    // lands is put back at the top, not used up.
    QCOMPARE(snowfall.flakes().size(), snowfall.targetFlakeCount());
    QVERIFY(firstTenSeconds > 1.0);
    QVERIFY2(std::abs(secondTenSeconds - firstTenSeconds) < firstTenSeconds / 4,
             qPrintable(QStringLiteral("%1 px in the first ten seconds, %2 in the second")
                            .arg(firstTenSeconds)
                            .arg(secondTenSeconds)));
}

void SnowfallTest::aFlakeCrossingTheSurfaceInOneStepStillLands()
{
    // The fastest Flakes the effect can produce, stepped at the longest frame
    // SnowfallRegistry will integrate in one go: about 19 logical px, which is
    // the whole of a settled pile. Testing where a Flake *is* rather than the
    // segment it travelled would let all of these straight through.
    Settings config = settings(5, 10);
    config.flakeStyle = FlakeStyle::Blob; // one speed for every Flake

    // A flat pile, already at the cap, so nothing landing on it moves the
    // surface: 490 is the line to cross for the whole of the step.
    config.maxDepth = 10;
    const qreal surface = 490;

    Catcher window = wallToWall(500);
    for (int column = 0; column < window.snowline().columnCount(); ++column) {
        window.snowline().setDepth(column, config.maxDepth);
    }

    QList<Catcher *> catchers{&window};
    Snowfall snowfall(s_output, config, s_seed);
    snowfall.setSettings(config);

    const QList<Flake> before = snowfall.flakes();
    snowfall.step(0.05, 0, catchers);
    const QList<Flake> after = snowfall.flakes();

    int crossed = 0;
    for (int index = 0; index < before.size(); ++index) {
        if (before.at(index).y > surface) {
            continue; // started under the pile, from the scattered fill
        }

        if (after.at(index).y < s_output.top()) {
            // Back above the top edge: it landed, and it only got there in one
            // step if it was within reach of the surface to begin with.
            crossed += before.at(index).y > surface - 19 ? 1 : 0;
            continue;
        }

        QVERIFY2(after.at(index).y <= surface,
                 qPrintable(QStringLiteral("Flake went from y %1 to %2, through the surface at %3")
                                .arg(before.at(index).y)
                                .arg(after.at(index).y)
                                .arg(surface)));
    }

    QVERIFY(crossed > 0); // and the case the test is about did come up
}

void SnowfallTest::aThinCapDoesNotLeak()
{
    // A Cap is only thin for the first few seconds, but that is when it is
    // watched, and a Flake that goes through one is a Flake visibly falling
    // through the top of a window.
    //
    // The landing test is "it was above the top edge when the step began and it
    // is under the snow now", and the reason it is not "it crossed the surface"
    // is in here. A Flake that steps sideways into a deeper Column arrives
    // under that Column's surface without having crossed it, and on a pile
    // deeper than one frame of fall it stops there, held up by the snow between
    // the surface and the top edge. On a pile of half a pixel it is through the
    // pile and out below the top edge in the same step, and it used to carry on
    // down the front of the window.
    Settings config = settings(5, 5, 10); // a hard wind: this needs the sideways step
    config.meltRate = 0;

    const QRectF frame(s_output.left() + 400, 300, 800, 500);
    Catcher window(CatcherClass::Window, nullptr, nullptr, frame);
    QList<Catcher *> catchers{&window};

    Snowfall snowfall(s_output, config, s_seed);
    snowfall.setSettings(config);

    const qreal delta = 1.0 / 30.0;
    int throughTheTop = 0;
    int inFrontOfTheWindow = 0;
    QString first;

    // From bare, so the whole of the run is the Cap climbing through the depths
    // a Flake can cross in one step.
    for (int frameIndex = 0; frameIndex < 30 * 30; ++frameIndex) {
        const qreal elapsed = frameIndex * delta;
        snowfall.step(delta, elapsed, catchers);
        window.settle(delta, config);

        for (const Flake &flake : snowfall.flakes()) {
            if (window.columnAt(flake.x) < 0 || flake.y <= frame.top() || flake.y >= frame.bottom()) {
                continue;
            }

            // Under the top edge and over the window. Fine in itself -- a gust
            // blows Flakes in past the side edges and they fall in front of the
            // window, which is the one place ADR-0003 allows -- but not if it
            // came down over the top edge, because then the window's own snow
            // was in the way.
            ++inFrontOfTheWindow;
            if (flake.previousY > frame.top()) {
                continue;
            }

            ++throughTheTop;
            if (first.isEmpty()) {
                first = QStringLiteral("y %1 -> %2 at %3 s, over a Column %4 px deep")
                            .arg(flake.previousY)
                            .arg(flake.y)
                            .arg(elapsed)
                            .arg(window.snowline().depth(window.columnAt(flake.x)));
            }
        }
    }

    QVERIFY2(throughTheTop == 0,
             qPrintable(QStringLiteral("%1 Flakes came down through the top edge: %2")
                            .arg(throughTheTop)
                            .arg(first)));

    // The Cap did build, so the Flakes that were kept out of the window were
    // kept out by landing on it rather than by there being none.
    QVERIFY(meanDepth(window) > 1.0);

    // And the Flakes blown in past the side edges are still falling in front of
    // the window, which is not a leak: they never crossed the catching edge.
    QVERIFY(inFrontOfTheWindow > 0);
}

void SnowfallTest::blownInFlakesAreDrawnInFrontOfAWindowOrBehindIt()
{
    // `flakesInFrontOfWindows` is a drawing choice and nothing else: the same
    // run twice, once each way, has to leave the same snow on the same
    // Catcher and the Flakes in the same places -- the only difference being
    // which of them the painter is handed (settings.h).
    const QRectF frame(s_output.left() + 400, 300, 800, 500);

    const auto run = [&frame](bool inFront, int *drawnOverTheWindow, int *heldBack) {
        Settings config = settings(5, 5, 10); // a hard wind: this needs Flakes blown sideways
        config.meltRate = 0;
        config.flakesInFrontOfWindows = inFront;

        Catcher window(CatcherClass::Window, nullptr, nullptr, frame);
        QList<Catcher *> catchers{&window};

        Snowfall snowfall(s_output, config, s_seed);
        snowfall.setSettings(config);

        const qreal delta = 1.0 / 30.0;
        for (int frameIndex = 0; frameIndex < 30 * 30; ++frameIndex) {
            snowfall.step(delta, frameIndex * delta, catchers);
            window.settle(delta, config);

            for (const Flake &flake : snowfall.flakes()) {
                if (!frame.contains(flake.x, flake.y)) {
                    continue;
                }
                ++(flake.covered ? *heldBack : *drawnOverTheWindow);
            }
        }

        return std::make_pair(window.snowline(), snowfall.flakes());
    };

    int drawnInFront = 0;
    int heldBackInFront = 0;
    const auto [snowInFront, flakesInFront] = run(true, &drawnInFront, &heldBackInFront);

    int drawnBehind = 0;
    int heldBackBehind = 0;
    const auto [snowBehind, flakesBehind] = run(false, &drawnBehind, &heldBackBehind);

    // On, which is the default: the gust blows Flakes in past the side edges
    // and they are drawn falling in front of the window.
    QVERIFY(drawnInFront > 0);
    QCOMPARE(heldBackInFront, 0);

    // Off: the same Flakes are there and none of them is drawn.
    QCOMPARE(heldBackBehind, drawnInFront);
    QCOMPARE(drawnBehind, 0);

    // And it cost the snow nothing either way.
    QVERIFY(snowInFront == snowBehind);
    QCOMPARE(flakesInFront.size(), flakesBehind.size());
    for (int index = 0; index < flakesInFront.size(); ++index) {
        QCOMPARE(flakesInFront.at(index).x, flakesBehind.at(index).x);
        QCOMPARE(flakesInFront.at(index).y, flakesBehind.at(index).y);
    }
}

void SnowfallTest::aDisabledClassIsTransparent()
{
    Settings config = settings();
    config.meltRate = 0;
    config.snowOnWindows = false;

    Catcher ground = theGround();
    Catcher window = wallToWall(500);
    QList<Catcher *> catchers{&ground, &window}; // paint order: the ground is below

    Snowfall snowfall(s_output, config, s_seed);
    run(snowfall, config, catchers, 20);

    // A disabled class is not there at all: Flakes fall through the window and
    // pile up on the ground underneath it (spec: Model).
    QVERIFY(window.snowline().isEmpty());
    QVERIFY(meanDepth(ground) > 1.0);

    // Switch it back on and the window takes the snow the ground was getting.
    // The first twenty seconds are the Flakes that were already below it when
    // it appeared, finishing their fall; after that the ground is in its shadow.
    config.snowOnWindows = true;
    run(snowfall, config, catchers, 20, 20);

    const qreal shadowed = meanDepth(ground);
    const qreal caught = meanDepth(window);
    run(snowfall, config, catchers, 60, 40);

    QVERIFY(meanDepth(window) - caught > 1.0);
    QVERIFY2(meanDepth(ground) - shadowed < (meanDepth(window) - caught) / 100,
             qPrintable(QStringLiteral("%1 px got past a Solid Catcher while %2 px landed on it")
                            .arg(meanDepth(ground) - shadowed)
                            .arg(meanDepth(window) - caught)));
}

void SnowfallTest::aConcealedCatcherIsTransparent()
{
    Settings config = settings();
    config.meltRate = 0;
    config.maxDepth = 20; // leave headroom to observe catching again after concealment

    Catcher ground = theGround();
    Catcher window = wallToWall(500);
    QList<Catcher *> catchers{&ground, &window}; // paint order: the ground is below

    Snowfall snowfall(s_output, config, s_seed);
    run(snowfall, config, catchers, 30);

    const qreal held = meanDepth(window);
    const qreal shadowed = meanDepth(ground);
    QVERIFY(held > 1.0);

    // Off screen -- minimised, auto-hidden, or on another virtual desktop --
    // and the Flakes go straight past to the ground, the same way they do past
    // a class that has been switched off.
    window.setConcealed(true);
    run(snowfall, config, catchers, 30, 30);

    QVERIFY2(meanDepth(ground) - shadowed > 1.0,
             qPrintable(QStringLiteral("only %1 px reached the ground under a concealed Catcher")
                            .arg(meanDepth(ground) - shadowed)));

    // And what it was holding is still there, waiting for it to come back
    // (spec: Behaviour, "Window minimised", "Panel auto-hidden").
    QCOMPARE(meanDepth(window), held);

    window.setConcealed(false);
    run(snowfall, config, catchers, 30, 60);
    QVERIFY(meanDepth(window) - held > 1.0);
}

void SnowfallTest::theTopmostCatcherCatchesFirst()
{
    Settings config = settings();
    config.meltRate = 0;

    // Two windows at the same height, overlapping over the middle third.
    Catcher behind(CatcherClass::Window, nullptr, nullptr, QRectF(0, 500, 800, 500));
    Catcher inFront(CatcherClass::Window, nullptr, nullptr, QRectF(400, 500, 800, 500));
    QList<Catcher *> catchers{&behind, &inFront}; // paint order, bottom to top

    Snowfall snowfall(QRectF(0, 0, 1600, 1000), config, s_seed);
    run(snowfall, config, catchers, 20);

    // Over the overlap the window in front takes everything, so nothing lands
    // on the one behind there. Its covered half is not quite bare, because the
    // pile on its open half goes on relaxing sideways under the front window --
    // the top edge it is standing on carries on under there -- but it is a
    // shallow tail, not snowfall.
    qreal open = 0;
    qreal covered = 0;
    for (int column = 0; column < behind.snowline().columnCount(); ++column) {
        if (behind.snowline().columnCentre(column) < 400) {
            open += behind.snowline().depth(column);
        } else {
            covered += behind.snowline().depth(column);
        }
    }

    QVERIFY(open > 0);
    QVERIFY2(covered < open / 10, qPrintable(QStringLiteral("%1 px under the front window against %2 px clear of it").arg(covered).arg(open)));
    // And the far end, 400 px in, never sees any of it.
    QCOMPARE(behind.snowline().depth(behind.snowline().columnCount() - 1), 0.0);
    QVERIFY(meanDepth(inFront) > 1.0);
}

void SnowfallTest::theDepthStyleBuildsAPileMoreSlowly()
{
    Settings depth = settings();
    depth.meltRate = 0;
    Settings blob = depth;
    blob.flakeStyle = FlakeStyle::Blob;

    Catcher underDepth = windowAt(300);
    Catcher underBlob = windowAt(300);
    QList<Catcher *> depthCatchers{&underDepth};
    QList<Catcher *> blobCatchers{&underBlob};

    Snowfall depthSnow(s_output, depth, s_seed);
    Snowfall blobSnow(s_output, blob, s_seed);
    run(depthSnow, depth, depthCatchers, 30);
    run(blobSnow, blob, blobCatchers, 30);

    // A distant Flake is worth less snow than a near one and falls more slowly
    // to get there, so the same weather under the `depth` style builds a pile
    // at around three quarters of the rate a uniform one does.
    QVERIFY(meanDepth(underDepth) < meanDepth(underBlob));
    QVERIFY(meanDepth(underDepth) > meanDepth(underBlob) / 2);
}

void SnowfallTest::withoutMeltItFillsToTheMaximumDepthAndStops()
{
    Settings config = settings();
    config.meltRate = 0; // permanent accumulation

    Catcher ground = theGround();
    QList<Catcher *> catchers{&ground};
    Snowfall snowfall(s_output, config, s_seed);

    run(snowfall, config, catchers, 150);

    // Full, exactly full, and staying that way however long it snows: the
    // excess at the cap is discarded rather than shed onto anything below.
    QCOMPARE(ground.snowline().maximumDepth(), qreal(config.maxDepth));
    QCOMPARE(meanDepth(ground), qreal(config.maxDepth));

    run(snowfall, config, catchers, 30);
    QCOMPARE(ground.snowline().maximumDepth(), qreal(config.maxDepth));
}

void SnowfallTest::meltUnderTheSnowfallRateFillsToTheCap()
{
    // The shipped defaults, on a Catcher high enough up the output for the
    // snowfall to be worth something; see the ticket for why height matters.
    const Settings config = settings();
    Catcher window = windowAt(300);
    QList<Catcher *> catchers{&window};
    Snowfall snowfall(s_output, config, s_seed);

    run(snowfall, config, catchers, 120);
    const qreal filled = meanDepth(window);

    run(snowfall, config, catchers, 60, 120);

    // Melt under the rate snow arrives at does not hold a pile half way up: it
    // slows the climb and the Column still ends at the cap, where the excess is
    // discarded. The default is under the rate so that the snow lies at all
    // (settings.h: meltRate).
    QVERIFY2(filled > config.maxDepth * 0.9,
             qPrintable(QStringLiteral("mean depth %1 of a %2 px cap after two minutes")
                            .arg(filled)
                            .arg(config.maxDepth)));
    QVERIFY2(window.snowline().maximumDepth() > config.maxDepth - 1.0,
             qPrintable(QStringLiteral("deepest Column %1 of a %2 px cap")
                            .arg(window.snowline().maximumDepth())
                            .arg(config.maxDepth)));
    // Not still climbing: at the cap every Column trades the melt off it for
    // the next landing on it, so the mean holds rather than grows.
    QVERIFY(std::abs(meanDepth(window) - filled) < 0.5);
}

void SnowfallTest::meltOverTheSnowfallRateLeavesItBare()
{
    // The other side of the same threshold, and the reason the default is where
    // it is: a melt rate over what a Column is fed sits it at bare for good.
    //
    // 0.6 px/s, against the 0.42 px/s this window is actually fed at density 5
    // -- the 0.070 * density that settings.h quotes is the figure for a Column
    // of a window, and a window whose whole width is under the snowfall
    // collects a little more than that. The rate used to be 0.4, which is under
    // what was measured here rather than over it: the pile it left was creeping
    // up rather than sitting at bare, and it passed on the size of the creep
    // over a minute rather than on the property this test is named for.
    Settings config = settings();
    config.meltRate = 0.6;

    Catcher window = windowAt(300);
    QList<Catcher *> catchers{&window};
    Snowfall snowfall(s_output, config, s_seed);

    run(snowfall, config, catchers, 60);
    const qreal settled = meanDepth(window);

    run(snowfall, config, catchers, 60, 60);
    const qreal later = meanDepth(window);

    // Not nothing -- a landing is a lump that takes a second or two to melt out,
    // so there is always a little in flight -- but nowhere near a Cap, and not
    // climbing towards one however long it snows.
    QVERIFY(settled < 2.0);
    QVERIFY2(later < config.maxDepth / 4.0,
             qPrintable(QStringLiteral("mean depth %1 -> %2 over the second minute")
                            .arg(settled)
                            .arg(later)));
    QVERIFY2(std::abs(later - settled) < settled / 2,
             qPrintable(QStringLiteral("mean depth %1 -> %2 over the second minute")
                            .arg(settled)
                            .arg(later)));
}

void SnowfallTest::snowMeltsAwayOnceItStopsFalling()
{
    Settings config = settings();
    config.meltRate = 0;

    Catcher window = windowAt(300);
    QList<Catcher *> catchers{&window};
    Snowfall snowfall(s_output, config, s_seed);
    run(snowfall, config, catchers, 60);
    QVERIFY(meanDepth(window) > 5.0);

    // Disabling the class stops it catching but does not clear it: what is
    // already there melts away, at the accelerated rate rather than the
    // configured one (spec: Behaviour, "Settings changed").
    config.snowOnWindows = false;
    config.meltRate = 0.4;
    run(snowfall, config, catchers, 0.5, 60);
    QVERIFY2(!window.snowline().isEmpty(),
             "a switched-off class melts, it is not cleared");

    // maxDepth over the accelerated rate is under three seconds from full to
    // bare -- against the fifty seconds the configured 0.4 px/s would take,
    // which is long enough to read as the setting not having worked.
    run(snowfall, config, catchers, 3, 60.5);
    QVERIFY(window.snowline().isEmpty());

    // And switching it back on before it has all gone picks up the snow that is
    // left rather than starting from bare.
    config.snowOnWindows = true;
    run(snowfall, config, catchers, 10, 64);
    QVERIFY(meanDepth(window) > 0);
}

void SnowfallTest::aGroundAcrossTheTopWouldTakeTheWholeSnowfall()
{
    // Two stacked outputs, in the logical geometry of the laptop and the
    // external monitor this was found on: the external standing above the
    // laptop, so that its bottom edge and the laptop's top edge are the same
    // line at y = 1271, and its width covers the whole of the laptop's.
    const QRectF external(0, 0, 2259, 1271);
    const QRectF laptop(160, 1271, 1920, 1200);

    Settings config = settings();
    config.meltRate = 0;

    Catcher externalGround(CatcherClass::Ground, nullptr, nullptr,
                           QRectF(external.left(), external.bottom(), external.width(), 0));
    Catcher laptopGround(CatcherClass::Ground, nullptr, nullptr,
                         QRectF(laptop.left(), laptop.bottom(), laptop.width(), 0));

    // What CatcherRegistry::catchersFor() hands the laptop's Snowfall: its own
    // ground, and not the ground of the monitor above it (ADR-0006).
    Snowfall snowfall(laptop, config, s_seed);
    QList<Catcher *> own{&laptopGround};
    run(snowfall, config, own, 20);

    QVERIFY(meanDepth(laptopGround) > 1.0);
    QVERIFY(externalGround.snowline().isEmpty());

    // The snow is falling down the output rather than standing at the top of
    // it: most of the population is past the first tenth of the screen.
    int belowTheTop = 0;
    for (const Flake &flake : snowfall.flakes()) {
        if (flake.y > laptop.top() + laptop.height() / 10) {
            ++belowTheTop;
        }
    }
    QVERIFY2(belowTheTop > snowfall.flakes().size() / 2,
             qPrintable(QStringLiteral("%1 of %2 Flakes are in the top tenth of the output")
                            .arg(belowTheTop)
                            .arg(snowfall.flakes().size())));

    // And what the unfiltered set did instead, which is the bug the filter
    // answers: the external monitor's ground is a line straight across the top
    // of the laptop, so it takes every Flake in the first pixel row. The
    // laptop's own ground stays bare and nothing is left falling below the line.
    Catcher takenExternal(CatcherClass::Ground, nullptr, nullptr, externalGround.geometry());
    Catcher takenLaptop(CatcherClass::Ground, nullptr, nullptr, laptopGround.geometry());
    // Paint order, bottom to top: the grounds, in the order the registry lists
    // the outputs.
    QList<Catcher *> everything{&takenExternal, &takenLaptop};

    Snowfall taken(laptop, config, s_seed);
    // Long enough for the initial population to have finished falling: it is
    // scattered down the output rather than queued above it, so those Flakes do
    // reach the laptop's own ground -- and they are the snow that was briefly
    // there before it all stopped. The slowest of them takes about 13 seconds
    // to cross the output.
    run(taken, config, everything, 20);

    const qreal landedBeforeItStopped = meanDepth(takenLaptop);
    const qreal takenAtTheTop = meanDepth(takenExternal);
    QVERIFY(landedBeforeItStopped > 0);

    // Every Flake after that enters through the band above the top edge and is
    // taken on the line before it is ever drawn: the snow on the laptop's
    // ground stops arriving, while the monitor above goes on collecting it.
    run(taken, config, everything, 10, 20);

    QVERIFY(meanDepth(takenExternal) > takenAtTheTop);
    QCOMPARE(meanDepth(takenLaptop), landedBeforeItStopped);
    for (const Flake &flake : taken.flakes()) {
        QVERIFY2(flake.y <= external.bottom(),
                 qPrintable(QStringLiteral("A Flake reached y = %1, past the line at %2")
                                .arg(flake.y)
                                .arg(external.bottom())));
    }
}

QTEST_GUILESS_MAIN(SnowfallTest)

#include "snowfalltest.moc"
