/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "frameclock.h"

#include <core/output.h>
#include <effect/effecthandler.h>

#include <algorithm>
#include <utility>

namespace Snow
{

FrameClock::FrameClock(int frameRateCap, QObject *parent)
    : QObject(parent)
    , m_frameRateCap(frameRateCap)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &FrameClock::requestFrame);

    const QList<KWin::LogicalOutput *> screens = KWin::effects->screens();
    for (KWin::LogicalOutput *output : screens) {
        addOutput(output);
    }

    connect(KWin::effects, &KWin::EffectsHandler::screenAdded, this, &FrameClock::addOutput);
    connect(KWin::effects, &KWin::EffectsHandler::screenRemoved, this, &FrameClock::removeOutput);
}

FrameClock::~FrameClock() = default;

void FrameClock::addOutput(KWin::LogicalOutput *output)
{
    const auto [it, added] = m_schedules.try_emplace(output, Schedule{FramePacer(m_frameRateCap)});
    if (!added) {
        return;
    }

    // A mode switch changes the only cadence a frame can be delivered on, so
    // the schedule that is snapped to it has to be worked out again.
    connect(output, &KWin::LogicalOutput::currentModeChanged, this, [this, output] {
        const auto changed = m_schedules.find(output);
        if (changed != m_schedules.end()) {
            pace(output, changed->second);
            arm(FramePacer::Clock::now());
        }
    });

    pace(output, it->second);

    // A monitor plugged in mid-session has snow on it from its first frame
    // rather than from whenever the others happen to be due next.
    const FramePacer::Clock::time_point now = FramePacer::Clock::now();
    it->second.pacer.restart(now);
    arm(now);
}

void FrameClock::removeOutput(KWin::LogicalOutput *output)
{
    disconnect(output, nullptr, this, nullptr);
    m_schedules.erase(output);
    arm(FramePacer::Clock::now());
}

void FrameClock::pace(KWin::LogicalOutput *output, Schedule &schedule) const
{
    // The cap says how often the snow may move; the refresh rate says when a
    // frame can be delivered at all, and only the two together make a schedule
    // frames can actually land on (FramePacer::intervalFor).
    schedule.pacer.setInterval(FramePacer::intervalFor(m_frameRateCap, output->refreshRate()));
}

void FrameClock::setFrameRateCap(int frameRateCap)
{
    m_frameRateCap = frameRateCap;
    for (auto &[output, schedule] : m_schedules) {
        pace(output, schedule);
    }

    // Read here rather than once at load, which is what makes a cap changed in
    // the KCM the cap of the very next frame.
    arm(FramePacer::Clock::now());
}

void FrameClock::setRunning(bool running)
{
    m_running = running;

    if (!m_running) {
        // A stopped clock costs a stopped timer and nothing else.
        m_timer.stop();
        return;
    }

    // A resume has nothing to be on schedule with, so every output starts a
    // fresh one -- together, which is also what keeps one wakeup enough for all
    // of them. Nothing is going to repaint the desktop just because the snow
    // may fall again, so ask: this is the frame the animation starts from.
    const FramePacer::Clock::time_point now = FramePacer::Clock::now();
    for (auto &[output, schedule] : m_schedules) {
        schedule.pacer.restart(now);
        schedule.claimable = false;
        schedule.animating = false;
    }
    m_lastRequest = FramePacer::Clock::time_point{};

    requestFrame();
}

bool FrameClock::isDue(KWin::LogicalOutput *output, FramePacer::Clock::time_point now)
{
    if (!m_running) {
        return false;
    }

    const auto it = m_schedules.find(output);
    if (it == m_schedules.end()) {
        return false;
    }

    Schedule &schedule = it->second;
    // The claim is spent whether or not the schedule needed it: one repaint
    // request is worth one frame per output, and a second frame arriving before
    // the next request is somebody else's.
    const bool claimed = std::exchange(schedule.claimable, false);
    schedule.animating = claimed || schedule.pacer.isDue(now);
    return schedule.animating;
}

void FrameClock::frameRendered(KWin::LogicalOutput *output, FramePacer::Clock::time_point now)
{
    if (!m_running) {
        return;
    }

    const auto it = m_schedules.find(output);
    if (it == m_schedules.end()) {
        return;
    }

    Schedule &schedule = it->second;
    // The frame the snow moved in is a frame the schedule has to spend, even
    // when it was the claim rather than the schedule that handed it over --
    // otherwise the next frame is due at once and the snow steps twice inside
    // one refresh period.
    schedule.pacer.waitAfterFrame(now, std::exchange(schedule.animating, false));
    arm(now);
}

void FrameClock::arm(FramePacer::Clock::time_point now)
{
    if (!m_running) {
        return;
    }

    if (m_schedules.empty()) {
        // No outputs is no desktop to snow on; there is nothing to ask for.
        m_timer.stop();
        return;
    }

    // The earliest of the schedules, because one request wakes every output and
    // the ones that are due in the frame it produces are the ones that step.
    std::chrono::milliseconds wait = std::chrono::milliseconds::max();
    FramePacer::Duration shortest = FramePacer::Duration::max();
    for (const auto &[output, schedule] : m_schedules) {
        wait = std::min(wait, schedule.pacer.waitUntilDue(now));
        shortest = std::min(shortest, schedule.pacer.interval());
    }

    // ...but never sooner than an interval after the last request, which is
    // what makes the cap a cap. It is what stops an output that is not being
    // rendered at all -- switched off, or its render loop inhibited -- from
    // leaving a schedule permanently overdue and the clock asking for frames as
    // fast as the event loop turns; and it is why schedules that have drifted
    // apart cost no extra wakeups, since one request wakes every output and the
    // ones that were not due in it are due in the next.
    //
    // The shortest of the intervals, rather than one worked out from the cap:
    // an interval snapped to a display's cadence can be a shade under what the
    // cap alone would give, and a bound above it would hold that output's
    // frames back by one refresh period every few seconds -- which is the hitch
    // the snapping is there to remove.
    const FramePacer::Duration sinceRequest = now - m_lastRequest;
    if (sinceRequest < shortest) {
        wait = std::max(wait, std::chrono::ceil<std::chrono::milliseconds>(shortest - sinceRequest));
    }

    m_timer.start(wait);
}

void FrameClock::requestFrame()
{
    const FramePacer::Clock::time_point now = FramePacer::Clock::now();
    m_lastRequest = now;

    // Every output is given a claim on the frame this asks for, because the
    // request wakes every one of them. An output whose schedule had already
    // reached the frame does not need it; an output whose frame arrives a
    // fraction before its schedule expected it does, and it is the one that
    // would otherwise be drawn with the snow standing still (isDue).
    for (auto &[output, schedule] : m_schedules) {
        schedule.claimable = true;
    }

    // Falling snow is not damage anything else reports, so the effect has to ask
    // for every frame it animates -- and a full one, because a Flake can be
    // anywhere. Asking is the effect's whole running cost, so how often this is
    // called is the effect's power story: at most `frameRateCap` times a second
    // while the snow is falling, and not at all while it is not.
    //
    // Full rather than one output at a time because KWin schedules a frame on
    // every output for any repaint that is asked for (Scene::addLogicalRepaint):
    // asking per output would wake the same outputs anyway, and leave the ones
    // it did not name repainting nothing.
    KWin::effects->addRepaintFull();

    // The frame that request produces is what normally schedules the next one
    // (frameRendered). Arming here as well is the backstop for the frame that
    // never arrives: the wait below is an interval, because that is what the
    // request just spent.
    arm(now);
}

} // namespace Snow
