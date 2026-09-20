/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "frameprobe.h"

#include "framepacer.h"
#include "snowlogging.h"

#include <core/output.h>
#include <core/region.h>

#include <algorithm>
#include <cstdlib>

namespace Snow
{

// How far over what was expected a gap has to run before the frame it belongs
// to is worth a line of its own. Half again: a frame that arrives a refresh
// period late is the thing being looked for, and one that arrives a hair either
// side of its expected moment is every frame there is.
static constexpr double s_gapTolerance = 1.5;

// How long a frame may take before it is worth a line, as a fraction of a
// refresh period. Three quarters rather than the whole: a frame that fills its
// budget is one that has nothing left for the jitter around it, so it is
// already in trouble by the time it goes over.
static constexpr double s_frameBudget = 0.75;

// How often the frames that were not worth a line of their own are summed up.
static constexpr std::chrono::seconds s_summaryInterval(1);

static double milliseconds(std::chrono::steady_clock::duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

// The median rect count of @a buckets over @a frames frames, the last bucket
// counting frames with that many rects or more. A median that lands in the last
// bucket answers "at least that many", which is enough: by then the number has
// already stopped being a small one.
template<std::size_t N>
static int medianRects(const std::array<int, N> &buckets, int frames)
{
    if (frames <= 0) {
        return 0;
    }

    int seen = 0;
    for (std::size_t rects = 0; rects < N; ++rects) {
        seen += buckets[rects];
        if (seen * 2 >= frames) {
            return int(rects);
        }
    }
    return int(N) - 1;
}

bool FrameProbe::isEnabled()
{
    // Read once and cached: an effect cannot be handed a new environment
    // without being reloaded, and this is asked on every frame.
    static const bool enabled = std::getenv("KWIN_SNOW_PROBE") != nullptr;
    return enabled;
}

void FrameProbe::beginFrame(KWin::LogicalOutput *output, bool due, std::chrono::milliseconds presentTime,
                            int frameRateCap)
{
    if (!isEnabled()) {
        return;
    }

    m_began = std::chrono::steady_clock::now();
    m_simulated = m_began;
    m_output = output;
    m_outputName = output ? output->name() : QStringLiteral("?");
    m_due = due;
    m_paintMegapixels = 0;
    m_paintRects = 0;
    m_paintedRects = -1;
    m_translucentWindows = 0;
    m_groundCap = false;

    // A refresh period from the output's own rate, because that is what a frame
    // is delivered on: the budget and the present gap are both measured in it,
    // and two monitors need not share one.
    const uint32_t refreshRate = output ? output->refreshRate() : 0;
    m_expectedFrameMilliseconds = refreshRate > 0 ? 1.0e6 / refreshRate : 0;

    // The step the pacing means on this output, worked out here rather than at
    // the call site so that a probe that is off costs nothing but the test
    // above. The same call FrameClock's own pacer is built from, so the two
    // cannot disagree about what a step is.
    m_expectedStepMilliseconds =
        std::chrono::duration<double, std::milli>(FramePacer::intervalFor(frameRateCap, refreshRate)).count();

    Output &previous = m_outputs[output];
    m_current = &previous;

    // Unset before an output's first frame, and the gaps stay zero rather than
    // being measured from an epoch. A first frame reports nothing and is not a
    // hitch.
    m_presentGap = previous.lastPresent
        ? std::chrono::duration<double, std::milli>(presentTime - *previous.lastPresent).count()
        : 0;
    previous.lastPresent = presentTime;

    m_stepGap = 0;
    if (due) {
        if (previous.lastStep) {
            m_stepGap = std::chrono::duration<double, std::milli>(presentTime - *previous.lastStep).count();
        }
        previous.lastStep = presentTime;
    }
}

void FrameProbe::simulated()
{
    if (!isEnabled()) {
        return;
    }

    m_simulated = std::chrono::steady_clock::now();
}

void FrameProbe::requested(const KWin::Region &paint)
{
    if (!isEnabled()) {
        return;
    }

    m_paintRects = int(paint.rects().size());

    double pixels = 0;
    for (const KWin::Rect &rect : paint.rects()) {
        pixels += double(rect.width()) * double(rect.height());
    }
    m_paintMegapixels = pixels / 1.0e6;
}

void FrameProbe::painted(const KWin::Region &region)
{
    if (!isEnabled()) {
        return;
    }

    m_paintedRects = int(region.rects().size());
}

void FrameProbe::marked(int translucentWindows, bool ground)
{
    if (!isEnabled()) {
        return;
    }

    m_translucentWindows = translucentWindows;
    m_groundCap = ground;
}

void FrameProbe::endFrame()
{
    // m_current is set by every beginFrame() the guard above lets through, so
    // an unset one is an endFrame() without a frame rather than a state to
    // report on.
    if (!isEnabled() || !m_current) {
        return;
    }

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double frame = milliseconds(now - m_began);
    const double sim = milliseconds(m_simulated - m_began);

    Output &output = *m_current;
    Window &window = output.window;

    ++window.frames;
    window.stepped += m_due ? 1 : 0;
    window.frameMillisecondsTotal += frame;
    window.frameMillisecondsMax = std::max(window.frameMillisecondsMax, frame);
    window.simMillisecondsMax = std::max(window.simMillisecondsMax, sim);
    window.presentGapMax = std::max(window.presentGapMax, m_presentGap);
    window.stepGapMax = std::max(window.stepGapMax, m_stepGap);
    window.paintMegapixelsTotal += m_paintMegapixels;
    // Only frames that reached paintScreen(), so that a frame which did not is
    // absent from the distribution rather than being counted as no rects at all.
    if (m_paintedRects >= 0) {
        ++window.paintedFrames;
        ++window.paintedRects[std::min(m_paintedRects, s_paintedRectBuckets - 1)];
        window.paintedRectsMax = std::max(window.paintedRectsMax, m_paintedRects);
    }
    window.translucentMin = window.translucentMin < 0
        ? m_translucentWindows
        : std::min(window.translucentMin, m_translucentWindows);
    window.translucentMax = std::max(window.translucentMax, m_translucentWindows);
    window.groundCaps += m_groundCap ? 1 : 0;

    // A window entering or leaving the occluders changes what KWin paints
    // without changing any region, so it is worth a line of its own wherever it
    // happens -- that is the whole point of watching it. Against this output's
    // own previous count: two monitors need not be showing the same windows,
    // and a count compared against the other one's changes in every frame.
    const bool markingChanged = output.lastTranslucentWindows >= 0
        && m_translucentWindows != output.lastTranslucentWindows;
    if (markingChanged) {
        qCInfo(KWIN_EFFECT_SNOW).noquote().nospace()
            << "Snow probe " << m_outputName << " -- translucent windows "
            << output.lastTranslucentWindows << " -> " << m_translucentWindows;
    }
    output.lastTranslucentWindows = m_translucentWindows;

    // Three independent reasons, reported together rather than one line each,
    // because the whole point of the probe is which of them a hitch came with.
    // Against the step, not against the refresh period. The effect cannot know
    // whether anything else is driving the compositor, and when nothing is, a
    // frame every step *is* the cadence -- judging those against a refresh
    // period reports every frame there is and buries the one that matters.
    const bool lateFrame = m_expectedStepMilliseconds > 0
        && m_presentGap > s_gapTolerance * m_expectedStepMilliseconds;
    const bool lateStep = m_expectedStepMilliseconds > 0
        && m_stepGap > s_gapTolerance * m_expectedStepMilliseconds;
    const bool slowFrame = m_expectedFrameMilliseconds > 0
        && frame > s_frameBudget * m_expectedFrameMilliseconds;

    if (lateFrame || lateStep || slowFrame) {
        ++window.reported;
        qCInfo(KWIN_EFFECT_SNOW).noquote().nospace()
            << "Snow probe " << m_outputName << " -- "
            << (lateFrame ? "no frame for " : "")
            << (lateFrame ? QString::number(m_presentGap, 'f', 1) + QStringLiteral(" ms (") +
                            QString::number(m_presentGap / m_expectedFrameMilliseconds, 'f', 1) +
                            QStringLiteral(" refreshes); ") : QString())
            << (lateStep ? QStringLiteral("snow stood still for ") +
                           QString::number(m_stepGap, 'f', 1) + QStringLiteral(" ms of a ") +
                           QString::number(m_expectedStepMilliseconds, 'f', 1) +
                           QStringLiteral(" ms step; ") : QString())
            << "frame " << QString::number(frame, 'f', 1) << " ms"
            << " (sim " << QString::number(sim, 'f', 1) << " ms)"
            << ", repaint " << QString::number(m_paintMegapixels, 'f', 2) << " logical Mpx asked in "
            << m_paintRects << (m_paintRects == 1 ? " rect" : " rects")
            << (m_paintedRects >= 0
                    ? QStringLiteral(", painted in ") + QString::number(m_paintedRects) +
                      (m_paintedRects == 1 ? QStringLiteral(" rect") : QStringLiteral(" rects"))
                    : QString())
            << ", " << m_translucentWindows << " translucent"
            << (m_groundCap ? " + ground Cap" : "")
            << (m_due ? ", stepped" : ", riding along");
    }

    summarise(output, now);
    m_output = nullptr;
    m_current = nullptr;
}

void FrameProbe::summarise(Output &output, std::chrono::steady_clock::time_point now)
{
    Window &window = output.window;

    if (window.opened == std::chrono::steady_clock::time_point{}) {
        window.opened = now;
        return;
    }

    if (now - window.opened < s_summaryInterval) {
        return;
    }

    // Named, because every output summarises its own second: an unnamed line
    // that summed two 60 Hz monitors would read as one monitor at 120, and a
    // stall on one of them would be indistinguishable from a stall on the other.
    const double seconds = std::chrono::duration<double>(now - window.opened).count();

    // The rect count of what was painted, which is what the draws are clipped
    // to and iterated over -- a median and a worst case, because it is the
    // typical frame that says what the draw-call count usually is and the tail
    // that says what it can become.
    const int median = medianRects(window.paintedRects, window.paintedFrames);
    qCInfo(KWIN_EFFECT_SNOW).noquote().nospace()
        << "Snow probe " << m_outputName << " " << QString::number(seconds, 'f', 2) << " s -- "
        << window.frames << " frames, " << window.stepped << " stepped, "
        << window.reported << " reported; frame "
        << QString::number(window.frameMillisecondsMax, 'f', 1) << " ms worst, "
        << QString::number(window.frames > 0 ? window.frameMillisecondsTotal / window.frames : 0.0, 'f', 1)
        << " ms mean; sim " << QString::number(window.simMillisecondsMax, 'f', 1) << " ms worst; "
        << "widest present gap " << QString::number(window.presentGapMax, 'f', 1) << " ms, "
        << "widest step gap " << QString::number(window.stepGapMax, 'f', 1) << " ms; "
        << "repainted " << QString::number(window.paintMegapixelsTotal, 'f', 1) << " logical Mpx"
        << ", painted in " << median << (median == s_paintedRectBuckets - 1 ? "+" : "")
        << (median == 1 ? " rect" : " rects") << " median, "
        << window.paintedRectsMax << " worst; "
        << "translucent " << window.translucentMin << ".." << window.translucentMax
        << ", ground Cap in " << window.groundCaps << " frames";

    window = Window{};
    window.opened = now;
}

} // namespace Snow
