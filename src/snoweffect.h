/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "settings.h"
#include "snowlogging.h"

#include <effect/effect.h>

#include <chrono>
#include <memory>

namespace Snow
{

class CapPainter;
class Catcher;
class CatcherRegistry;
class FlakePainter;
class FrameClock;
class SnowfallRegistry;
class Suspension;

/**
 * The Snow effect.
 *
 * The model is complete and so is the drawing of it: Flakes fall over every
 * output, land on the topmost Solid Snow Catcher under them, pile up into that
 * Catcher's Snowline, settle and melt away; the Flakes are drawn in one pass
 * above every window, and every Snowline is drawn as a Cap inside its own
 * Catcher's paint pass, where KWin's back-to-front order provides occlusion for
 * free. See CONTEXT.md for the terms and .scratch/snow-effect/spec.md for the
 * rest.
 *
 * Nothing else on a still desktop asks for a frame, so the effect asks for its
 * own -- and that, rather than anything it draws, is what it costs. The asking
 * is all in one place: the FrameClock, ticking at `frameRateCap` per output and
 * stopped whenever the Suspension says nobody is looking (spec: Behaviour).
 */
class SnowEffect : public KWin::Effect
{
    Q_OBJECT

public:
    SnowEffect();
    ~SnowEffect() override;

    /**
     * Re-read `[Effect-snow]` and push it down, without letting anything snap.
     *
     * KWin calls this on load and whenever the configuration changes, which in
     * practice means somebody pressing Apply in the KCM -- and so means
     * somebody watching the desktop closely at the moment it happens. Nothing
     * here resets or clears anything: a class switched off leaves its Snowlines
     * melting at the accelerated rate rather than losing them, a lowered
     * `maxDepth` leaves deeper snow to melt down to it, and a lowered `density`
     * stops spawning rather than taking Flakes out of the air. Each of the
     * three lives with the thing it changes -- Catcher::settle(),
     * Snowline::deposit(), Snowfall::step() -- because each of them is a rule
     * about that thing rather than about configuration.
     */
    void reconfigure(ReconfigureFlags flags) override;

    /**
     * Steps the Snowfall of the output about to be painted, settles its snow,
     * and says that the whole output has to be repainted for it.
     *
     * Only in the frames the FrameClock is due in, which are the frames this
     * effect asked for and the ones that happen to arrive when it was about to.
     * Every other frame is somebody else's -- a window being dragged, a video,
     * a clock in a Panel -- and repaints only the part of the screen they
     * damaged. The snow in one of those is drawn where it stands: moving it
     * would leave the Flakes it drew last frame standing in the pixels outside
     * that damage, which is the trail a dragged window used to pull along
     * behind it.
     *
     * A suspended effect steps nothing either: the clock skips rather than
     * runs, so the snow resumes exactly where it stopped however long ago that
     * was.
     */
    // KWin 6.6 passes presentTime; newer KWin releases do not. Keep both
    // overloads so the effect builds against either API generation.
    void prePaintScreen(KWin::ScreenPrePaintData &data, std::chrono::milliseconds presentTime);
    void prePaintScreen(KWin::ScreenPrePaintData &data);

    /**
     * Draws the Flakes of the output being painted, over everything else.
     *
     * One pass, above the whole window stack, with no per-window work and no
     * z-order setting (ADR-0003). The spec calls this a postPaintScreen pass,
     * which is the same point in the frame; it is here because this is the call
     * KWin hands a render target and a viewport to.
     */
    void paintScreen(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport,
                     int mask, const KWin::Region &deviceRegion, KWin::LogicalOutput *screen) override;

    /**
     * Makes room for the Cap of the window about to be painted.
     *
     * A Cap stands above its window's frame geometry, in a strip KWin has no
     * reason to expect anything in, so the strip is added to what will be
     * painted and the window stops claiming to be opaque up to its own top edge
     * (spec: Rendering).
     */
    void prePaintWindow(KWin::RenderView *view, KWin::EffectWindow *w, KWin::WindowPrePaintData &data,
                        std::chrono::milliseconds presentTime);
    void prePaintWindow(KWin::RenderView *view, KWin::EffectWindow *w, KWin::WindowPrePaintData &data);

    /**
     * Draws the Cap of the window being painted, over the window itself.
     *
     * Occlusion comes from the order KWin already paints in: this runs inside
     * each Catcher's own pass, back to front, so a window in front covers the
     * Cap of the one behind it without the effect clipping anything (ADR-0003).
     * The ground's Cap is drawn here too, just after the wallpaper, which is
     * the one point in a frame that is over the desktop and under every window.
     */
    void paintWindow(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport,
                     KWin::EffectWindow *w, int mask, const KWin::Region &deviceRegion,
                     KWin::WindowPaintData &data) override;

    /**
     * Reports the frame to the FrameClock of the output it was for, which is
     * what keeps the frame rate at the cap rather than at a beat between the
     * cap and the display.
     */
    void postPaintScreen() override;

    /**
     * Whether the effect is in the frame at all.
     *
     * KWin asks once per frame and leaves an effect that says no out of every
     * call above, which is what "suspends entirely" means for the two reasons
     * that are something else being on screen -- a fullscreen window, a lock
     * screen. It is more than not drawing: an effect that is out of the frame
     * does not block KWin from scanning a fullscreen surface straight out to
     * the display.
     */
    bool isActive() const override;

    int requestedEffectChainPosition() const override;

private:
    /**
     * The Catcher whose Cap @a window should be wearing right now, or nullptr
     * when it should be wearing none: it is a Panel that has auto-hidden, or
     * there is not enough snow on it to draw.
     *
     * Deliberately not a question about whether the class is Solid. A class
     * that has just been switched off still has snow on it, and that snow is
     * melting rather than gone (spec: Behaviour) -- so it goes on being drawn
     * until there is none of it left, which takes a second or two.
     */
    const Catcher *cappedCatcher(KWin::EffectWindow *window) const;

    /** The same question for the ground Catcher of the output being painted. */
    const Catcher *cappedGround() const;

    /**
     * Start, stop or re-pace the clock the animation runs on.
     *
     * Called whenever either of the two things it is a function of moves: the
     * Suspension, and the frame rate cap a reconfigure may have changed.
     */
    void updateFrameClock();

    std::unique_ptr<CatcherRegistry> m_catchers;
    std::unique_ptr<SnowfallRegistry> m_snowfalls;
    std::unique_ptr<FlakePainter> m_flakePainter;
    std::unique_ptr<CapPainter> m_capPainter;
    std::unique_ptr<Suspension> m_suspension;

    /**
     * The clock the animation runs on: one schedule per output, and the timer
     * whose every tick asks KWin for a frame.
     *
     * Running whenever the snow is falling and stopped whenever it is not, so
     * that "suspended" costs a stopped timer rather than a test inside a frame
     * the effect asked for anyway.
     */
    std::unique_ptr<FrameClock> m_frameClock;

    /**
     * The configuration, as of the last reconfigure().
     *
     * The effect is where it is read and where it is kept; everything that
     * works from it is handed a copy or handed this one by reference, which is
     * why nothing below here has to know that a configuration can change.
     */
    Settings m_settings;

    /** The output whose frame is being painted, for the ground's Cap. */
    KWin::LogicalOutput *m_paintedOutput = nullptr;
    /** Whether the ground's Cap still has to go down in this frame. */
    bool m_groundCapPending = false;
};

} // namespace Snow
