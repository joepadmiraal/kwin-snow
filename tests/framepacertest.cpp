/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    When the effect asks for its next frame, checked without a compositor.

    The frame rate cap is the effect's whole power story (spec: Behaviour), and
    the one part of it that can be checked here is the part that decides how
    long to wait: a clock the test moves by hand, in place of frames arriving.
    That the compositor then delivers those frames is not checkable here -- and
    not in the nested session either, which never reaches the cap to begin with;
    see docs/development.md.
*/

#include "framepacer.h"
#include "settings.h"

#include <QTest>

using namespace Snow;
using namespace std::chrono_literals;

// A 30 fps cap, the spec's default, in the milliseconds the pacer answers a
// timer in. The schedule itself is kept finer than this -- a thirtieth of a
// second is 33.33 ms -- which is why the waits below are what a whole frame
// rounds down to rather than what it is.
static constexpr std::chrono::milliseconds s_frame = 33ms;

// Two displays that call themselves 60 Hz, in the millihertz KWin reports.
static constexpr uint32_t s_sixtyHertz = 60000;
static constexpr uint32_t s_nearlySixtyHertz = 59990;

class FramePacerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theIntervalComesFromTheCap();
    void aNonsenseCapIsStillSomethingToWaitFor();
    void aCapNearTheDisplaysOwnCadenceSnapsToIt();
    void aCapNowhereNearItIsLeftAlone();
    void aFrameAHairEarlyIsStillThatFrame();
    void aClaimedFrameSpendsTheScheduleToo();
    void aFrameOnScheduleWaitsAWholeInterval();
    void aLateFrameWaitsLessSoTheRateHolds();
    void aRunOfLateFramesDoesNotDriftOffTheCap();
    void aFasterDesktopIsNotAskedForFrames();
    void aFrameIsDueOnlyWhenTheScheduleSaysSo();
    void askingHowLongDoesNotMoveTheSchedule();
    void theSameInstantTwiceIsStillOneFrame();
    void aStallIsNotCaughtUpOn();
    void aResumeAsksStraightAway();
};

void FramePacerTest::theIntervalComesFromTheCap()
{
    // To the microsecond, because whole milliseconds are exactly what the
    // schedule must not be rounded to: 17 ms against a 16.67 ms refresh period
    // is a third of a millisecond of drift a frame, and a dropped frame every
    // fifty of them.
    const auto micros = [](const FramePacer &pacer) {
        return std::chrono::round<std::chrono::microseconds>(pacer.interval());
    };

    QCOMPARE(micros(FramePacer(30)), 33333us);
    QCOMPARE(micros(FramePacer(60)), 16667us);
    QCOMPARE(micros(FramePacer(10)), 100000us);

    // The default is the spec's default, so that nothing has to repeat it.
    QCOMPARE(FramePacer().interval(), FramePacer(Settings().frameRateCap).interval());
}

void FramePacerTest::aNonsenseCapIsStillSomethingToWaitFor()
{
    // A hand-edited config can say anything, and every one of these answers has
    // to be a wait rather than a division by zero or a busy loop.
    QCOMPARE(FramePacer(0).interval(), std::chrono::nanoseconds(1000ms));
    QCOMPARE(FramePacer(-30).interval(), std::chrono::nanoseconds(1000ms));
    QVERIFY(FramePacer(100000).interval() >= 1ms);
}

void FramePacerTest::aCapNearTheDisplaysOwnCadenceSnapsToIt()
{
    // A frame is only ever delivered on a refresh boundary, so a cap that names
    // the display's rate or a division of it is paced at exactly that many
    // refresh periods. Anything else walks through the refresh period and drops
    // a frame every lap, which is the hitch this is here to remove.
    const auto period = FramePacer::intervalFor(60, s_sixtyHertz);
    QCOMPARE(period, std::chrono::nanoseconds(16666667));

    QCOMPARE(FramePacer::intervalFor(30, s_sixtyHertz), 2 * period);
    QCOMPARE(FramePacer::intervalFor(15, s_sixtyHertz), 4 * period);

    // Including when the panel does not quite call itself 60: a cap of 30 on
    // 59.99 Hz still means every second refresh, and pacing it at a flat 33.33
    // ms would be the drift all over again.
    const auto nearlyPeriod = FramePacer::intervalFor(60, s_nearlySixtyHertz);
    QVERIFY(nearlyPeriod > period);
    QCOMPARE(FramePacer::intervalFor(30, s_nearlySixtyHertz), 2 * nearlyPeriod);

    // A cap above what the display can deliver is the display's own rate: that
    // is the one cap the hardware imposes for us.
    QCOMPARE(FramePacer::intervalFor(120, s_sixtyHertz), period);
    QCOMPARE(FramePacer::intervalFor(240, s_sixtyHertz), period);

    // An output that has not said what it refreshes at is the plain interval.
    QCOMPARE(FramePacer::intervalFor(30, 0), FramePacer::intervalFor(30));
}

void FramePacerTest::aCapNowhereNearItIsLeftAlone()
{
    // 25 fps on a 60 Hz display cannot be evenly spaced by anybody: two refresh
    // periods is 30 and three is 20. Rounding it to 20 would lose a fifth of
    // the frame rate somebody asked for to fix something that is not fixable,
    // so the cap is paced exactly as it was set.
    QCOMPARE(FramePacer::intervalFor(25, s_sixtyHertz), FramePacer::intervalFor(25));
    QCOMPARE(FramePacer::intervalFor(40, s_sixtyHertz), FramePacer::intervalFor(40));
}

void FramePacerTest::aFrameOnScheduleWaitsAWholeInterval()
{
    const FramePacer::Clock::time_point start{};
    FramePacer pacer(30);
    pacer.restart(start);

    // The frame the resume asked for, arriving promptly.
    QCOMPARE(pacer.waitAfterFrame(start + 2ms), s_frame - 2ms);
    // And the next, arriving when it was due.
    QCOMPARE(pacer.waitAfterFrame(start + s_frame), s_frame);
}

void FramePacerTest::aLateFrameWaitsLessSoTheRateHolds()
{
    const FramePacer::Clock::time_point start{};
    FramePacer pacer(30);
    pacer.restart(start);
    pacer.waitAfterFrame(start);

    // A frame that missed its refresh period and came half an interval late:
    // the wait that follows is short by exactly that, so the frame after it is
    // back on the schedule the cap set rather than a whole period behind it.
    QCOMPARE(pacer.waitAfterFrame(start + s_frame + 16ms), s_frame - 16ms);
}

void FramePacerTest::aRunOfLateFramesDoesNotDriftOffTheCap()
{
    const FramePacer::Clock::time_point start{};
    FramePacer pacer(30);
    pacer.restart(start);

    // Every frame arrives 5 ms after it was asked for, which is the case the
    // schedule exists for: waiting a fresh interval each time would compound
    // those 5 ms into a rate well under the cap, and thirty frames of it makes
    // the difference plain.
    FramePacer::Clock::time_point now = start;
    for (int frame = 0; frame < 30; ++frame) {
        now += pacer.waitAfterFrame(now) + 5ms;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
    // Thirty frames at the cap is a second of them, and the last one's own
    // lateness is the only slack the schedule cannot take back.
    const auto thirtyFrames = std::chrono::ceil<std::chrono::milliseconds>(30 * pacer.interval());
    QVERIFY2(elapsed <= thirtyFrames + 5ms, qPrintable(QString::number(elapsed.count())));
    QVERIFY(elapsed >= thirtyFrames - 1ms);
}

void FramePacerTest::aFasterDesktopIsNotAskedForFrames()
{
    const FramePacer::Clock::time_point start{};
    FramePacer pacer(30);
    pacer.restart(start);
    pacer.waitAfterFrame(start);

    // Frames arriving at 60 fps because something else is painting the desktop.
    // Each one is answered with the time still left on the schedule rather than
    // an interval of its own, so the effect asks for nothing until they stop.
    QCOMPARE(pacer.waitAfterFrame(start + 17ms), s_frame - 17ms);
    QCOMPARE(pacer.waitAfterFrame(start + 25ms), s_frame - 25ms);

    // And when they do stop, the next frame is the one the cap wanted all along.
    QCOMPARE(pacer.waitAfterFrame(start + s_frame), s_frame);
}

void FramePacerTest::aFrameIsDueOnlyWhenTheScheduleSaysSo()
{
    const FramePacer::Clock::time_point start{};
    FramePacer pacer(30);
    pacer.restart(start);

    // A resume is due at once, and the frame it asks for answers it.
    QVERIFY(pacer.isDue(start));
    pacer.waitAfterFrame(start);

    // The frames in between belong to whatever else is painting the desktop.
    // Saying no to those is what keeps the snow out of a frame that repaints
    // only a corner of the screen -- see SnowEffect::prePaintScreen.
    QVERIFY(!pacer.isDue(start + 1ms));
    QVERIFY(!pacer.isDue(start + s_frame - 1ms));

    // And yes to the first frame at or after the moment the cap allows,
    // whoever it was that asked for it.
    QVERIFY(pacer.isDue(start + s_frame));
    QVERIFY(pacer.isDue(start + 10s));

    // A stopped clock is not a question this answers: FrameClock says no for
    // every output while it is stopped, and restarts the schedules on resume.
}

void FramePacerTest::aFrameAHairEarlyIsStillThatFrame()
{
    const FramePacer::Clock::time_point start{};
    FramePacer pacer(60);
    pacer.restart(start);
    pacer.waitAfterFrame(start);

    // The moment a frame is measured at is when the compositor began preparing
    // it, which sits a variable few hundred microseconds ahead of the refresh
    // it is for. Held to the schedule exactly, a frame that began a fraction
    // early would not be one the snow moves in -- and it is a frame the effect
    // asked for and one that repaints the whole screen, so it would be drawn
    // with every Flake standing where the frame before left it.
    QVERIFY(pacer.isDue(start + pacer.interval() - 500us));

    // A fraction, though, and not a refresh period: a cap below the display's
    // rate still means the frames in between belong to somebody else.
    QVERIFY(!pacer.isDue(start + pacer.interval() - 8ms));
}

void FramePacerTest::aClaimedFrameSpendsTheScheduleToo()
{
    const FramePacer::Clock::time_point start{};
    FramePacer pacer(30);
    pacer.restart(start);
    pacer.waitAfterFrame(start);

    // FrameClock hands the snow a frame that its own repaint request brought
    // back even when the schedule has not reached it. The schedule still has to
    // spend an interval on it: left where it was, the frame that was due 13 ms
    // later would be due as well, and the snow would step twice inside one
    // refresh period.
    QCOMPARE(pacer.waitUntilDue(start + 20ms), 13ms);
    QCOMPARE(pacer.waitAfterFrame(start + 20ms, true), 46ms);
    QVERIFY(!pacer.isDue(start + 33ms));

    // And the phase is the schedule's own rather than the early frame's: two
    // frames at the cap after the resume, not two and the 13 ms.
    QVERIFY(pacer.isDue(start + 67ms));
}

void FramePacerTest::askingHowLongDoesNotMoveTheSchedule()
{
    const FramePacer::Clock::time_point start{};
    FramePacer pacer(30);
    pacer.restart(start);
    pacer.waitAfterFrame(start);

    // FrameClock asks every output's schedule how long it has left, every time
    // it arms its one timer, and asking must cost nothing: the answer is the
    // same however often it is asked, and the frame that does arrive is still
    // measured against the schedule it would have been.
    QCOMPARE(pacer.waitUntilDue(start + 10ms), s_frame - 10ms);
    QCOMPARE(pacer.waitUntilDue(start + 10ms), s_frame - 10ms);
    QCOMPARE(pacer.waitAfterFrame(start + s_frame), s_frame);

    // Overdue is nothing to wait for rather than a negative wait, which a timer
    // cannot take.
    QCOMPARE(pacer.waitUntilDue(start + 10s), 0ms);
}

void FramePacerTest::theSameInstantTwiceIsStillOneFrame()
{
    const FramePacer::Clock::time_point start{};
    FramePacer pacer(30);
    pacer.restart(start);

    // A schedule belongs to one output, so the frames it is told about are that
    // output's alone; the second report of the same instant must not spend an
    // interval of its own.
    QCOMPARE(pacer.waitAfterFrame(start), s_frame);
    QCOMPARE(pacer.waitAfterFrame(start), s_frame);
    QCOMPARE(pacer.waitAfterFrame(start + s_frame), s_frame);
}

void FramePacerTest::aStallIsNotCaughtUpOn()
{
    const FramePacer::Clock::time_point start{};
    FramePacer pacer(30);
    pacer.restart(start);
    pacer.waitAfterFrame(start);

    // A second with no frames at all: something else had the compositor. The
    // frames that were missed are not owed -- the next one is due now, and the
    // one after it a whole interval later.
    QCOMPARE(pacer.waitAfterFrame(start + 1000ms), 0ms);
    QCOMPARE(pacer.waitAfterFrame(start + 1000ms), s_frame);
}

void FramePacerTest::aResumeAsksStraightAway()
{
    const FramePacer::Clock::time_point start{};
    FramePacer pacer(30);
    pacer.restart(start);

    // A resume has nothing to be on schedule with: the snow has not been
    // animating, and the frame it starts again from is due at once.
    QCOMPARE(pacer.waitAfterFrame(start), s_frame);

    pacer.restart(start + 5s);
    QCOMPARE(pacer.waitAfterFrame(start + 5s), s_frame);
}

QTEST_GUILESS_MAIN(FramePacerTest)

#include "framepacertest.moc"
