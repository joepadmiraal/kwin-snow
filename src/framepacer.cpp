/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "framepacer.h"

#include <algorithm>
#include <cmath>

namespace Snow
{

// How far off a whole number of refresh periods a cap may be and still be taken
// to mean that many: five per cent, which catches every cap that names the
// display's own rate or a division of it -- 30 against a panel that calls itself
// 59.99 Hz, 60 against 59.94 -- and catches nothing else. A cap further out than
// this asks for a rate no display can space evenly, and is left alone rather
// than rounded down to one it can; see intervalFor().
static constexpr double s_cadenceTolerance = 0.05;

FramePacer::FramePacer(int frameRateCap)
{
    setFrameRateCap(frameRateCap);
}

FramePacer::Duration FramePacer::intervalFor(int frameRateCap)
{
    // The cap itself is clamped to something that can be waited for by
    // frameInterval(), which is where what the setting means lives.
    return std::chrono::round<Duration>(std::chrono::duration<double>(Snow::frameInterval(frameRateCap)));
}

FramePacer::Duration FramePacer::intervalFor(int frameRateCap, uint32_t refreshRateMilliHz)
{
    const Duration requested = intervalFor(frameRateCap);
    if (refreshRateMilliHz == 0) {
        return requested;
    }

    // One refresh period, from millihertz: 1e12 nanoseconds of them a second.
    const Duration period(std::llround(1.0e12 / refreshRateMilliHz));
    if (period <= Duration::zero()) {
        return requested;
    }

    // A cap at or above the refresh rate is the refresh rate: the display is
    // the cap then, and saying so is what stops the schedule chasing frames
    // that cannot exist.
    const double periods = double(requested.count()) / double(period.count());
    if (periods <= 1.0 + s_cadenceTolerance) {
        return period;
    }

    // Otherwise the nearest whole number of refresh periods, if the cap is
    // near one. Nearest rather than next: a cap of 29 on a 60 Hz display is
    // asking for two periods and a bit, and the bit is worth less than the
    // even spacing that snapping to two buys.
    const double nearest = std::round(periods);
    if (std::abs(periods - nearest) > s_cadenceTolerance * nearest) {
        return requested;
    }

    return period * static_cast<long long>(nearest);
}

void FramePacer::setFrameRateCap(int frameRateCap)
{
    setInterval(intervalFor(frameRateCap));
}

void FramePacer::setInterval(Duration interval)
{
    m_interval = std::max<Duration>(interval, std::chrono::milliseconds(1));
}

void FramePacer::restart(Clock::time_point now)
{
    m_due = now;
}

std::chrono::milliseconds FramePacer::waitUntilDue(Clock::time_point now) const
{
    if (m_due <= now) {
        return std::chrono::milliseconds::zero();
    }

    // Rounded down, so that waiting this long lands at or a fraction before the
    // moment the frame is due rather than after it. The wait ends in a repaint
    // request, which has to reach the compositor before it begins the frame the
    // request is meant for: a fraction early costs nothing, and a fraction late
    // costs a whole refresh period. What makes early safe is that isDue() has
    // the same fraction of slack in it, so the frame that arrives is still the
    // frame that was scheduled.
    return std::chrono::floor<std::chrono::milliseconds>(m_due - now);
}

void FramePacer::spendOn(Clock::time_point decided, bool animated)
{
    if (!animated && !isDue(decided)) {
        // Still in the future: something else is painting the desktop faster
        // than the cap -- a video, a window being dragged -- and this frame is
        // one of theirs. The schedule is left where it is, so the cap goes on
        // deciding when the snow next moves rather than being reset by every
        // frame that goes past.
        return;
    }

    // The frame that was due has arrived, so the next one is due an interval
    // after that -- not an interval after this moment, which is what lets a
    // frame that came late be followed by a shorter wait.
    //
    // A schedule that has fallen behind altogether is moved up to the decision
    // instead: the compositor stalled, or several frames were missed, and
    // none of them is worth rendering late.
    m_due = std::max(m_due + m_interval, decided);
}

} // namespace Snow
