/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "framepacer.h"

#include <QObject>
#include <QTimer>

#include <unordered_map>

namespace KWin
{
class LogicalOutput;
}

namespace Snow
{

/**
 * The clock the animation runs on: one schedule per output, and the one timer
 * that wakes the compositor for all of them.
 *
 * Nothing on a still desktop asks for a frame, so the effect asks for its own,
 * and that -- rather than anything it draws -- is what it costs. This is where
 * the asking is, and it is the whole of the spec's "Animation is capped at a
 * configurable frame rate".
 *
 * A schedule per output, because KWin renders each output separately and calls
 * an effect once per output per frame. A single shared schedule is reset by
 * whichever output painted last, so an output that something else repaints
 * continuously -- a video, a terminal cursor, a clock in a Panel -- holds the
 * timer off indefinitely and the snow stops on every *other* monitor. One
 * FramePacer per output is what makes each monitor's cap its own -- and it is
 * also what lets each of them be paced against its own refresh rate, which two
 * monitors need not share.
 *
 * One timer, though, because a repaint request is not per output in any useful
 * sense: KWin schedules a frame on every output for any repaint that is asked
 * for, so a second timer would only wake the same outputs twice. The timer is
 * armed for the earliest of the schedules, and the outputs that are due in the
 * frame it produces are the ones that step.
 */
class FrameClock : public QObject
{
    Q_OBJECT

public:
    explicit FrameClock(int frameRateCap, QObject *parent = nullptr);
    ~FrameClock() override;

    /** Pace every output at @a frameRateCap frames a second from here on. */
    void setFrameRateCap(int frameRateCap);

    /**
     * Run the clock, or stop it.
     *
     * Stopped is a stopped timer and nothing else: whatever is on screen stays
     * there, because nothing here repaints it. Started is a schedule beginning
     * now on every output, and a frame asked for straight away -- see
     * Suspension for when each of those happens.
     */
    void setRunning(bool running);

    /**
     * Whether the frame being prepared for @a output is one the snow is due to
     * move in -- and, when it is, this output's claim on it.
     *
     * False for every frame somebody else asked for in between, and false for
     * all of them while the clock is stopped.
     *
     * A frame is the snow's for either of two reasons. The schedule has reached
     * it, which is the ordinary one; or this clock asked for a frame and this
     * is the first one the output has been given since. The second is not a
     * second rate -- requestFrame() is what is paced, and it hands out one claim
     * per output per request -- it is the answer to a frame that the effect
     * asked for arriving a touch before the schedule expected it. Such a frame
     * is drawn whatever this says, so saying no to it would mean presenting the
     * whole screen with the snow standing exactly where the frame before left
     * it: one stalled frame, and then a double step to catch up. That, rather
     * than any frame rate, is what a stutter is made of.
     *
     * Asking is claiming, so it is asked once per output per frame; postPaint
     * is where the claim is spent.
     */
    bool isDue(KWin::LogicalOutput *output, FramePacer::Clock::time_point now);

    /**
     * Report that a frame of @a output has been rendered at @a now, which is
     * what decides when the next one is asked for.
     *
     * FramePacer::spendOn() advances the schedule against the moment isDue()
     * was asked; see Schedule. arm() then asks waitUntilDue() of every output
     * from @a now, when waiting starts, to find the next frame to ask for.
     *
     * Every frame reports, not only the ones this clock asked for: a desktop
     * painting faster than the cap for its own reasons is one the snow rides
     * along with, and the schedule has to know those frames happened or it
     * would ask for frames in between them.
     */
    void frameRendered(KWin::LogicalOutput *output, FramePacer::Clock::time_point now);

private:
    /**
     * One output's schedule, and what it is owed.
     *
     * The two flags are the two halves of one frame: `claimable` is set for
     * every output when a repaint is asked for and spent by the first frame
     * that output is given, and `animating` carries the answer from the
     * prePaint that claimed it to the postPaint that has to advance the
     * schedule for it.
     */
    struct Schedule {
        FramePacer pacer;
        bool claimable = false;
        bool animating = false;
        /**
         * The moment isDue() was asked, which is the moment the frame is about.
         *
         * Kept because postPaint happens a few milliseconds after prePaint, and
         * FramePacer::spendOn() asks whether the frame was due as well as
         * being told whether the snow moved in it. Asked again at postPaint it
         * can answer yes about a frame that answered no at prePaint -- a
         * schedule that came due while the frame was being rendered -- and then
         * the schedule is spent on a frame the snow stood still in. Since it
         * advances by a whole number of refresh periods, the next due moment
         * lands in the render window of the frame two later and does it again:
         * the animation locks out until something shifts the phase. Measured at
         * a step every 33 ms or one every 30 seconds depending on nothing but
         * where the phase happened to start.
         */
        FramePacer::Clock::time_point decided{};
    };

    void addOutput(KWin::LogicalOutput *output);
    void removeOutput(KWin::LogicalOutput *output);

    /** Pace @a output at the cap, snapped to that output's own refresh rate. */
    void pace(KWin::LogicalOutput *output, Schedule &schedule) const;

    /** Wait for the earliest of the schedules, or stop if there is nothing to wait for. */
    void arm(FramePacer::Clock::time_point now);

    /** Ask KWin for a frame. The one place that does. */
    void requestFrame();

    std::unordered_map<KWin::LogicalOutput *, Schedule> m_schedules;

    /**
     * Single-shot and re-armed frame by frame, because each wait is measured
     * against a schedule rather than being one fixed interval (FramePacer).
     * Precise, because a coarse timer is free to drag a 33 ms wait out by 5% to
     * coalesce it with other wakeups, and the point of the cap is that the
     * number it is set to is the number of frames there are.
     */
    QTimer m_timer;

    /**
     * When a frame was last asked for, which bounds how often one can be: the
     * schedules above say when the snow is due to move, and this says that
     * asking for the frame it moves in never happens twice in one interval.
     */
    FramePacer::Clock::time_point m_lastRequest;

    int m_frameRateCap;
    bool m_running = false;
};

} // namespace Snow
