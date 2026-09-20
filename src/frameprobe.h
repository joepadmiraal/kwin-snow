/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

#include <array>
#include <chrono>
#include <optional>
#include <unordered_map>

namespace KWin
{
class LogicalOutput;
class Region;
}

namespace Snow
{

/**
 * Per-frame timing, for hitches the code cannot account for.
 *
 * Nothing in this effect has a period of its own: the sprite textures are
 * uploaded once, the only timer is FrameClock's, and Flakes are scattered at
 * birth so their respawns never line up. So when snow visibly stalls on a
 * rhythm, the rhythm is coming from outside and the only way to find it is to
 * write down what each frame actually did and look at the ones that stand out.
 *
 * Three clocks are worth telling apart, and a single "it stutters" cannot:
 *
 * - **the present gap**, how far this frame's present time is from the previous
 *   frame's on the same output. Wider than a refresh period means the
 *   compositor did not produce a frame for that refresh -- it stalled, or
 *   something else held it up -- and the snow stands still because *nothing*
 *   moved.
 * - **the step gap**, how far apart two frames the snow moved in are. Wider
 *   than the pacing interval while the present gap stays at a refresh period
 *   means frames were produced and the snow was not stepped in them: that is
 *   this effect's own scheduling, and it is FrameClock's to answer for.
 * - **the frame time**, prePaintScreen to postPaintScreen, and the slice of it
 *   the simulation took. A frame time over a refresh period is a frame that
 *   cannot be delivered on time whatever the schedule says, and if the
 *   simulation is not most of it then the cost is in what the frame asked to be
 *   repainted rather than in the snow.
 *
 * Off unless `KWIN_SNOW_PROBE` is set in the compositor's environment, because
 * a diagnostic that is on by default is a diagnostic that eventually costs
 * somebody a frame. Read once: an effect cannot be handed a new environment
 * without being reloaded.
 *
 * Interesting frames are reported as they happen and everything else is
 * reported once a second per output, which is what keeps a log that is worth
 * reading at 60 frames a second from being one nobody scrolls through.
 */
class FrameProbe
{
public:
    /** Whether `KWIN_SNOW_PROBE` was set when the effect was loaded. */
    static bool isEnabled();

    /**
     * A frame of @a output is being prepared, to be shown at @a presentTime,
     * and @a due says whether it is one the snow moves in.
     *
     * @a frameRateCap is the configured cap, not the interval it works out to:
     * the interval is what a step gap is judged against, but working it out
     * costs a virtual call and a handful of rounding on every frame of every
     * output, and this is a diagnostic that is usually off. So it is worked out
     * in here, behind the isEnabled() guard -- from the same FramePacer the
     * pacing itself uses, so a gap this calls wide is one the pacing meant to
     * be narrow.
     */
    void beginFrame(KWin::LogicalOutput *output, bool due, std::chrono::milliseconds presentTime,
                    int frameRateCap);

    /** The Snowfall has been advanced and the Snowlines settled. */
    void simulated();

    /**
     * @a paint is what the frame was asked to repaint, as the effect left it --
     * in the global logical pixels `ScreenPrePaintData::paint` is kept in, not
     * in the device pixels the output is rendered at.
     */
    void requested(const KWin::Region &paint);

    /**
     * @a region is what KWin settled on repainting, in the output's own device
     * pixels: the region SnowEffect::paintScreen() is handed, which is not the
     * one requested() reports and is not in the same coordinates.
     *
     * Its *rect count* is what earns it a place here. Both painters scissor to
     * this region and GLVertexBuffer draws once per rect of it, so the Flakes
     * cost `8 x rects` draws and every Cap costs `rects` of its own. It is the
     * one input to this effect's draw-call count that nothing else records,
     * and the requested count cannot stand in for it: KWin unions the effect's
     * request with everybody else's damage, and a frame the snow does not move
     * in asks for a handful of Cap headroom strips and is painted over
     * whatever else was damaged.
     */
    void painted(const KWin::Region &region);

    /**
     * What the effect asked KWin to paint differently this frame: @a
     * translucentWindows is how many windows it took out of the occluders by
     * marking them translucent, and @a ground whether the ground's Cap went
     * down.
     *
     * The repaint region is the effect's only *visible* lever on a frame, and
     * this is its only invisible one: a window that is not an occluder is one
     * whose neighbours below it are painted whole rather than culled, which
     * costs area that no region says anything about. If a peak in KWin's own
     * paint amount lines up with a change here, this is where it comes from;
     * if this holds steady across the peak, it does not.
     */
    void marked(int translucentWindows, bool ground);

    /** The frame has been rendered. */
    void endFrame();

private:
    /**
     * How many rects of the painted region are counted apart before a frame
     * goes in the last bucket of Window::paintedRects.
     *
     * A bucket per count rather than a running total, because the number worth
     * knowing about draw calls is the one a typical frame carries, and the mean
     * of a distribution that is one rect in nineteen frames of twenty and fifty
     * in the twentieth is a number nothing has. Thirty-two counts are kept
     * apart and everything above them shares the last bucket, which is already
     * far past the point where the answer is "many".
     */
    static constexpr int s_paintedRectBuckets = 33;

    /** What is being accumulated for one output's next summary line. */
    struct Window {
        std::chrono::steady_clock::time_point opened;
        int frames = 0;
        int stepped = 0;
        int reported = 0;
        double frameMillisecondsTotal = 0;
        double frameMillisecondsMax = 0;
        double simMillisecondsMax = 0;
        double presentGapMax = 0;
        double stepGapMax = 0;
        double paintMegapixelsTotal = 0;
        /** Frames counted in paintedRects, which is not every frame if one was not painted. */
        int paintedFrames = 0;
        /** How many frames carried each rect count, the last bucket being that many or more. */
        std::array<int, s_paintedRectBuckets> paintedRects{};
        int paintedRectsMax = 0;
        int translucentMin = -1;
        int translucentMax = -1;
        int groundCaps = 0;
    };

    /**
     * What the probe remembers about one output between frames.
     *
     * All of it, because the effect is called once per output per frame and
     * anything kept outside here alternates between the outputs' values: a
     * translucent count that differs between two monitors would report a change
     * in every frame, and a summary that summed both would read as one monitor
     * at twice the refresh rate.
     */
    struct Output {
        /** The previous frame's present time, or unset before the first. */
        std::optional<std::chrono::milliseconds> lastPresent;
        /** The previous *stepped* frame's present time. */
        std::optional<std::chrono::milliseconds> lastStep;
        /** The previous frame's count, so that a change is reported the once. */
        int lastTranslucentWindows = -1;
        /** This output's own one-second window, opened and closed by its own frames. */
        Window window;
    };

    void summarise(Output &output, std::chrono::steady_clock::time_point now);

    std::unordered_map<KWin::LogicalOutput *, Output> m_outputs;

    /** The frame between beginFrame() and endFrame(), which is never two. */
    KWin::LogicalOutput *m_output = nullptr;
    /**
     * That frame's entry in m_outputs, set with m_output and cleared with it.
     *
     * A pointer into the map is safe: the standard keeps references and
     * pointers to elements valid across rehashing, and beginFrame()'s own
     * lookup is the only insertion.
     */
    Output *m_current = nullptr;
    QString m_outputName;
    bool m_due = false;
    double m_expectedFrameMilliseconds = 0;
    double m_expectedStepMilliseconds = 0;
    double m_presentGap = 0;
    double m_stepGap = 0;
    double m_paintMegapixels = 0;
    int m_paintRects = 0;
    /** The painted region's rect count, or -1 in a frame painted() was not called for. */
    int m_paintedRects = -1;
    int m_translucentWindows = 0;
    bool m_groundCap = false;
    std::chrono::steady_clock::time_point m_began;
    std::chrono::steady_clock::time_point m_simulated;
};

} // namespace Snow
