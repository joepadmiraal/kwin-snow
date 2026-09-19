/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "snoweffect.h"

#include "cap.h"
#include "cappainter.h"
#include "catcherregistry.h"
#include "flakepainter.h"
#include "frameclock.h"
#include "snowfallregistry.h"
#include "suspension.h"

#include <QMatrix4x4>

#include <core/output.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <scene/scene.h>

namespace Snow
{

SnowEffect::SnowEffect()
{
    // Logged at info so a default-configured compositor shows it without any
    // QT_LOGGING_RULES tweaking; this is the signal the dev loop watches for.
    qCInfo(KWIN_EFFECT_SNOW) << "Snow effect loaded, built against KWin"
                             << KWIN_PLUGIN_VERSION_STRING;

    // Before the registries rather than through reconfigure() afterwards: a
    // Snowfall fills itself the moment it is built, and filling it to the
    // default density and then growing it to the configured one would make
    // switching the effect on at density 10 take three seconds to look right.
    m_settings = configuredSettings();
    qCDebug(KWIN_EFFECT_SNOW).noquote() << "Snow configured --" << describeSettings(m_settings);

    // Built after those lines so the first thing in the log is the effect, and
    // the Catchers it finds in the session it was switched on into follow it.
    m_catchers = std::make_unique<CatcherRegistry>();
    m_snowfalls = std::make_unique<SnowfallRegistry>(m_settings);
    m_flakePainter = std::make_unique<FlakePainter>();
    m_capPainter = std::make_unique<CapPainter>();

    m_frameClock = std::make_unique<FrameClock>(m_settings.frameRateCap);

    m_suspension = std::make_unique<Suspension>();
    connect(m_suspension.get(), &Suspension::changed, this, &SnowEffect::updateFrameClock);

    // Starts the snow if there is any reason to: switching the effect on into a
    // locked session or over a fullscreen window costs nothing at all.
    updateFrameClock();
}

SnowEffect::~SnowEffect()
{
    qCInfo(KWIN_EFFECT_SNOW) << "Snow effect unloaded";
}

void SnowEffect::reconfigure(ReconfigureFlags)
{
    m_settings = configuredSettings();

    // The Snowfalls take a copy each; the Catchers are handed this one by
    // reference on every frame. Nothing is reset on the way down -- see the
    // header for the three changes that have to melt rather than snap, and for
    // where each of them lives.
    m_snowfalls->setSettings(m_settings);

    // The frame rate cap is the one knob that is not read once a frame, so it
    // is the one that has to be pushed.
    updateFrameClock();

    qCDebug(KWIN_EFFECT_SNOW).noquote() << "Snow reconfigured --" << describeSettings(m_settings);
}

namespace
{

std::chrono::milliseconds monotonicMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Snow::FramePacer::Clock::now().time_since_epoch());
}

template<typename EffectsHandler>
void chainPrePaintScreen(EffectsHandler *handler, KWin::ScreenPrePaintData &data,
                         std::chrono::milliseconds presentTime)
{
    if constexpr (requires(EffectsHandler *h, KWin::ScreenPrePaintData &d, std::chrono::milliseconds t) {
                      h->prePaintScreen(d, t);
                  }) {
        handler->prePaintScreen(data, presentTime);
    } else {
        handler->prePaintScreen(data);
    }
}

template<typename EffectsHandler>
void chainPrePaintWindow(EffectsHandler *handler, KWin::RenderView *view, KWin::EffectWindow *window,
                         KWin::WindowPrePaintData &data, std::chrono::milliseconds presentTime)
{
    if constexpr (requires(EffectsHandler *h, KWin::RenderView *v, KWin::EffectWindow *w,
                           KWin::WindowPrePaintData &d, std::chrono::milliseconds t) {
                      h->prePaintWindow(v, w, d, t);
                  }) {
        handler->prePaintWindow(view, window, data, presentTime);
    } else {
        handler->prePaintWindow(view, window, data);
    }
}

} // namespace

void SnowEffect::prePaintScreen(KWin::ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    // Which output this frame is for, for the Caps drawn inside it and for the
    // schedule it belongs to: neither paintWindow nor postPaintScreen is told.
    m_paintedOutput = data.screen;

    if (!m_frameClock->isDue(data.screen, FramePacer::Clock::now())) {
        // Not a frame the snow moves in, for either of two reasons.
        //
        // The clock is stopped: a suspension that leaves the snow on screen,
        // and somebody else is repainting. Stepping then would move the snow on
        // by one frame every time a clock in a Panel ticks over, which is a
        // twitch rather than an animation.
        //
        // Or the clock is running and this frame is not one of its: something
        // else is painting the desktop faster than the cap -- a window being
        // dragged, a video -- and those frames repaint only what that something
        // damaged. The snow rides along in them, drawn where it stands; moving
        // it would draw every Flake in its new place and leave the pixels of
        // its old one, which are outside the damage, standing until something
        // repaints them. That is the trail a dragged window pulls behind it,
        // and not stepping here is what there is instead of asking for the
        // whole screen to be repainted at somebody else's frame rate.
        chainPrePaintScreen(KWin::effects, data, presentTime);
        return;
    }

    // KWin renders each output separately and calls this once per output per
    // frame, with that output's own present time -- which is exactly the shape
    // the per-output simulation wants (spec: Rendering).
    //
    // The Catchers this output's Flakes can land on, which is not all of them:
    // landing lines are global, so the ground of a monitor standing above this
    // one runs straight across the top of it and would catch the whole snowfall
    // in the first pixel row (CatcherRegistry::catchersFor). A window still
    // goes in whole, so one straddling two outputs catches over all of its top
    // edge.
    const qreal delta = m_snowfalls->advance(data.screen, presentTime, m_catchers->catchersFor(data.screen));

    // Settle after the landing, not before: the snow that just arrived is part
    // of the pile that relaxes, which is what stops a heavy frame leaving a
    // spike standing for a frame before it settles.
    m_catchers->settle(data.screen, delta, m_settings);

    // The snow has moved, so the whole output has to be repainted for it: a
    // Flake can be anywhere, and the pixels it was drawn in last frame are
    // nowhere anything else reports as damaged. This is the same full repaint
    // the FrameClock asked for, said again in the frame itself -- which is what
    // makes a frame the snow rides along with as complete as one of its own.
    data.paint += data.screen->geometry();

    chainPrePaintScreen(KWin::effects, data, presentTime);
}

void SnowEffect::prePaintScreen(KWin::ScreenPrePaintData &data)
{
    prePaintScreen(data, monotonicMilliseconds());
}

void SnowEffect::paintScreen(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport,
                             int mask, const KWin::Region &deviceRegion, KWin::LogicalOutput *screen)
{
    // The ground Catcher is per output, and its Cap goes down in paintWindow,
    // which is not told which output it is painting; prePaintScreen kept it.
    m_groundCapPending = true;

    // Everything else first: the Flakes go over the whole window stack, and an
    // enabled Catcher class is Solid, so no Flake is ever behind a window to
    // begin with (ADR-0003). The Caps are drawn from inside this call, each in
    // its own Catcher's pass.
    KWin::effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);

    // The Flakes of this output and no others. A Flake belongs to one Snowfall
    // for the whole of its life and never crosses to another (spec: Rendering),
    // so there is nothing here to clip.
    if (Snowfall *snowfall = m_snowfalls->snowfallFor(screen)) {
        m_flakePainter->paint(renderTarget, viewport, snowfall->flakes(),
                              m_settings.flakeStyle);
    }
}

void SnowEffect::prePaintWindow(KWin::RenderView *view, KWin::EffectWindow *w,
                                KWin::WindowPrePaintData &data, std::chrono::milliseconds presentTime)
{
    if (const Catcher *catcher = cappedCatcher(w)) {
        // The Cap rises above the window's frame geometry, into a strip nothing
        // else has any reason to repaint. Saying so is what stops it being
        // culled; saying the window is translucent is what stops the strip
        // being treated as opaque window (spec: Rendering).
        // Against the snow that is standing, not against the configured cap:
        // a `maxDepth` that has just come down leaves Caps deeper than it for
        // a few seconds, and a strip cut to the new cap would take the top off
        // them (cap.h: capReferenceDepth).
        const qreal headroom = capHeadroom(capReferenceDepth(catcher->snowline(), m_settings.maxDepth));
        const QRectF strip(catcher->geometry().left(), catcher->geometry().top() - headroom,
                           catcher->geometry().width(), headroom);

        data.setTranslucent();
        data.devicePaint += view->mapToDeviceCoordinatesAligned(strip);
    }

    chainPrePaintWindow(KWin::effects, view, w, data, presentTime);
}

void SnowEffect::prePaintWindow(KWin::RenderView *view, KWin::EffectWindow *w, KWin::WindowPrePaintData &data)
{
    prePaintWindow(view, w, data, monotonicMilliseconds());
}

void SnowEffect::paintWindow(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport,
                             KWin::EffectWindow *w, int mask, const KWin::Region &deviceRegion,
                             KWin::WindowPaintData &data)
{
    KWin::effects->paintWindow(renderTarget, viewport, w, mask, deviceRegion, data);

    // The ground's Cap lies on the desktop: over the wallpaper and under every
    // window. The wallpaper is itself a window -- plasmashell's desktop window,
    // at the bottom of the stack -- so the moment just after it is painted is
    // the only point in a frame that is both. A session with no desktop window
    // has no wallpaper either and draws no ground Cap, which is the same
    // consequence ADR-0003 recorded for the Flakes.
    if (m_groundCapPending && w->isDesktop()) {
        m_groundCapPending = false;
        if (const Catcher *ground = cappedGround()) {
            m_capPainter->paint(renderTarget, viewport, viewport.projectionMatrix(), *ground, m_settings, 1.0);
        }
    }

    if (const Catcher *catcher = cappedCatcher(w)) {
        // After the window, so the band's underlap covers the top of the frame
        // rather than showing a hairline of titlebar through it. Folding the
        // window's own paint transform into the projection is what carries a
        // Cap along with a window some effect below is moving or fading.
        m_capPainter->paint(renderTarget, viewport,
                            viewport.projectionMatrix() * data.toMatrix(viewport.scale()),
                            *catcher, m_settings, data.opacity());
    }
}

const Catcher *SnowEffect::cappedCatcher(KWin::EffectWindow *window) const
{
    const Catcher *catcher = m_catchers->catcherFor(window);
    if (!catcher) {
        return nullptr;
    }

    // An auto-hidden Panel keeps its geometry as well as its Snowline: KWin
    // marks the window hidden and leaves frameGeometry exactly where it was, so
    // there is no move to notice and the Cap has to be suppressed by asking.
    if (window->isHidden()) {
        return nullptr;
    }

    return capIsVisible(catcher->snowline()) ? catcher : nullptr;
}

const Catcher *SnowEffect::cappedGround() const
{
    const Catcher *ground = m_catchers->ground(m_paintedOutput);
    if (!ground) {
        return nullptr;
    }
    return capIsVisible(ground->snowline()) ? ground : nullptr;
}

void SnowEffect::postPaintScreen()
{
    // The frame that has just been rendered is what schedules this output's
    // next one, which is how the pacing knows whether it is keeping up
    // (FramePacer) and how a desktop painting faster than the cap for its own
    // reasons costs the effect fewer frames of its own.
    m_frameClock->frameRendered(m_paintedOutput, FramePacer::Clock::now());

    KWin::effects->postPaintScreen();
}

void SnowEffect::updateFrameClock()
{
    // The cap is read here rather than once at load, which is what makes a cap
    // changed in the KCM the cap of the very next frame: reconfigure() runs
    // this too.
    m_frameClock->setFrameRateCap(m_settings.frameRateCap);

    // A running clock asks for the frame the animation starts again from. The
    // Snowlines are exactly as they were left -- nothing here resets them, and
    // nothing stepped them while the snow was stopped -- and the step that
    // frame takes is the clamped one the registry hands out after a gap.
    m_frameClock->setRunning(m_suspension->isAnimated());
}

bool SnowEffect::isActive() const
{
    return m_suspension->isDrawn();
}

int SnowEffect::requestedEffectChainPosition() const
{
    // Snow sits on top of the window stack, above effects that move windows
    // around, so Caps follow their Snow Catcher rather than lagging behind it.
    return 90;
}

} // namespace Snow
