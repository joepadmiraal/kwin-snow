/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "settings.h"

#include <chrono>
#include <cstdint>

namespace Snow
{

/**
 * When to ask for the next animated frame, for one output.
 *
 * The effect asks for a repaint of the whole screen for every frame it
 * animates, and on a still desktop nothing else asks for any -- so how often
 * this says to ask is very nearly the whole of what the effect costs
 * (spec: Behaviour, "Animation is capped at a configurable frame rate").
 *
 * One output, because KWin renders each output on its own schedule and an
 * effect is called for each of them separately: a schedule shared between two
 * of them is a schedule the busier output can keep pushing out of reach of the
 * quieter one, which is snow that stops on the monitor nothing else is
 * painting. FrameClock keeps one of these per output for exactly that reason.
 *
 * It paces against a *schedule* rather than counting one interval from each
 * frame that arrives, and the difference is the cap actually holding. A repaint
 * request has to reach the compositor before it starts rendering the frame it
 * is meant for; one that arrives a moment after that goes into the frame after,
 * a whole refresh period later. Counted from the frame that arrives, every one
 * of those misses becomes the new cadence and a 30 fps cap settles at the 20 fps
 * its display can divide into. Counted from when the frame was *due*, the wait
 * after a late frame is shorter by exactly how late it was, so the next one
 * lands back on schedule and the rate averages the cap.
 *
 * The schedule is kept to the nanosecond and the interval is snapped to the
 * display's own cadence, and both are about the same thing: a frame is only
 * ever delivered on a refresh boundary, so a schedule that runs at any other
 * rate walks through the refresh period and every lap costs one frame that
 * arrives a whole period late. At a 30 fps cap on a 60 Hz display that is a
 * visible hitch every second or two -- which is what intervalFor() below
 * removes, by pacing at two refresh periods rather than at a rounded 33 ms.
 *
 * There is no KWin and no Qt in here, which is what lets the arithmetic be
 * checked in a unit test -- the frame rate itself cannot be: a nested
 * compositor does not reach the cap to begin with (docs/development.md).
 */
class FramePacer
{
public:
    using Clock = std::chrono::steady_clock;

    /**
     * The schedule's own unit. Nanoseconds rather than the milliseconds a timer
     * takes: a refresh period is 16.669 ms on the display this was written on,
     * and a schedule rounded to whole milliseconds drifts a third of a
     * millisecond a frame against it -- which is a dropped frame every fifty.
     * Rounding happens once, on the way out to the timer.
     */
    using Duration = std::chrono::nanoseconds;

    explicit FramePacer(int frameRateCap = Settings().frameRateCap);

    /** One frame at @a frameRateCap frames a second. */
    static Duration intervalFor(int frameRateCap);

    /**
     * One frame at @a frameRateCap frames a second, snapped to the cadence of a
     * display refreshing at @a refreshRateMilliHz millihertz.
     *
     * A frame is only ever delivered on a refresh boundary, so the only rates
     * that can be evenly spaced are the display's own rate divided by a whole
     * number. A cap that is within s_cadenceTolerance of one of those is taken
     * to mean it -- a cap of 30 on a 59.99 Hz display means every second
     * refresh, not 33 ms -- and the interval becomes exactly that many refresh
     * periods, which is a schedule that cannot walk out of phase.
     *
     * A cap that is nowhere near one of them is left exactly where it is: 25 fps
     * on a 60 Hz display cannot be evenly spaced by anybody, and rounding it to
     * 20 would be losing a fifth of the frame rate somebody asked for to fix
     * something that is not fixable. A cap at or above the refresh rate is the
     * display's own rate, which is the one cap hardware imposes for us.
     *
     * A @a refreshRateMilliHz of 0 is an output that has not said, and is the
     * plain interval above.
     */
    static Duration intervalFor(int frameRateCap, uint32_t refreshRateMilliHz);

    /** Pace at @a frameRateCap frames a second from here on. */
    void setFrameRateCap(int frameRateCap);

    /** Pace at @a interval from here on, whatever it was worked out from. */
    void setInterval(Duration interval);

    /** One frame at the cap. */
    Duration interval() const
    {
        return m_interval;
    }

    /**
     * Begin a schedule at @a now, with the next frame due straight away.
     *
     * This is a resume: the snow has not been animating, so there is nothing to
     * be on schedule with.
     */
    void restart(Clock::time_point now);

    /**
     * Whether the frame being prepared at @a now is one the animation is due
     * in.
     *
     * The question a frame that somebody else asked for has to be able to
     * answer: the effect is called for every frame of the output, not only the
     * ones it asked for, and a frame it is not due in is one it must not move
     * the snow in. See SnowEffect::prePaintScreen() for why -- it is about what
     * that frame repaints, not about the rate.
     *
     * Due a hair early as well as late, by slack(): the moment being compared
     * against is when the compositor started preparing the frame, which sits a
     * variable few milliseconds ahead of the refresh it is for. Held to the
     * schedule exactly, a frame that began a fraction early would not be one the
     * snow moves in -- and it is a frame the effect asked for, so it is drawn
     * either way, with the snow standing exactly where the previous one left it.
     * That stalled frame, not the rate, is what reads as a stutter.
     */
    bool isDue(Clock::time_point now) const
    {
        return m_due <= now + slack();
    }

    /**
     * How long from @a now until the next frame is due, without moving the
     * schedule; zero when it is due already.
     *
     * After spendOn(), zero means a compositor that cannot keep up with the
     * cap: the pacing never asks for more than one frame at a time, so a slow
     * compositor simply animates slower rather than catching up missed frames.
     */
    std::chrono::milliseconds waitUntilDue(Clock::time_point now) const;

    /**
     * Spend the schedule on the frame decided at @a decided, in which the snow
     * did or did not move (@a animated).
     *
     * The schedule advances for a frame that was due, and for one the snow
     * moved in whether it was due or not -- which is the frame FrameClock hands
     * out on the strength of its own repaint request having come back, rather
     * than on the strength of the schedule. Not advancing for that one would
     * leave the next frame due immediately and the snow stepping twice in a
     * refresh period.
     *
     * The timestamp is the prePaint decision, never the end of rendering: a
     * schedule that comes due during rendering must not be spent on a frame
     * the snow stood still in. Ask waitUntilDue() separately when waiting starts.
     *
     * Keep that moment explicit rather than remembering it in isDue():
     * FrameClock short-circuits `claimed || pacer.isDue(now)`, so a claimed
     * frame never asks the pacer and would leave a remembered moment stale.
     * isDue() stays a pure query, with no hidden state changed by asking it.
     */
    void spendOn(Clock::time_point decided, bool animated = false);

private:
    /**
     * How far either side of its due moment a frame still counts as that frame.
     *
     * A millisecond, which is more than the jitter between a compositor
     * deciding to prepare a frame and the refresh it is for, and comfortably
     * less than half a refresh period on any display anyone has -- which is the
     * bound that matters, because slack wider than that would let two
     * consecutive refreshes both answer to one scheduled frame. The eighth is
     * for a cap so far above the refresh rate that the interval itself is down
     * in that territory.
     */
    Duration slack() const
    {
        return std::min<Duration>(std::chrono::milliseconds(1), m_interval / 8);
    }

    Duration m_interval;
    Clock::time_point m_due;
};

} // namespace Snow
