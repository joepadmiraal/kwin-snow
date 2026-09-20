/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <QtGlobal>

#include <algorithm>

namespace Snow
{

/**
 * How a Flake is drawn -- and, for `Depth`, how fast it falls and how much snow
 * it is worth when it lands.
 *
 * The style reaches the simulation and not only the painter because `Depth` is
 * parallax: a Flake's z has to move it as well as size it, or the near ones
 * look large and the far ones small while the whole field descends in lockstep
 * (spec: Configuration, "Flake styles").
 */
enum class FlakeStyle {
    Blob,
    Crystal,
    Depth,
};

/**
 * How a Cap is drawn.
 *
 * A Cap is the appearance of a Snowline and the two vary independently
 * (CONTEXT.md: Cap), so this reaches the painter and nothing else: neither
 * style changes what a Snowline holds or where a Flake lands on it.
 */
enum class CapStyle {
    /** A smooth top contour with a soft shadow gradient under the leading edge. */
    Shaded,
    /** The same, with a static per-Column noise offset on the top edge. */
    Contour,
};

/**
 * The effect's knobs, with the spec's defaults.
 *
 * Every field is a key of `[Effect-snow]`, read out of `snowconfig.kcfg` by
 * configuredSettings() and pushed down from SnowEffect::reconfigure(). The
 * struct is what everything downstream actually reads: it is a plain copyable
 * value with no KConfig and no KWin in it, which is what lets the whole
 * simulation be handed a configuration in a unit test.
 *
 * The defaults below are written out twice -- here and in the .kcfg -- because
 * the schema is where a KCM's "Defaults" button reads from and this is where
 * everything else does. tests/settingstest.cpp checks the two agree, since
 * nothing in the toolchain does.
 */
struct Settings
{
    /**
     * Which Snow Catcher classes are Solid. A disabled class is transparent:
     * Flakes fall through it to whatever is below (spec: Model). Its Snowline
     * is not cleared -- it melts away at the accelerated rate below, so that
     * switching a class off in the KCM reads as snow going rather than as snow
     * disappearing (spec: Behaviour, "Settings changed").
     */
    bool snowOnWindows = true;
    bool snowOnPanels = true;
    bool snowOnDesktop = true;

    /** Flakes per unit of screen area, 1 to 10. Not a Flake count: see Snowfall. */
    int density = 5;

    /** 1 to 10, a drifting fall through a brisk one. */
    int fallSpeed = 5;

    /** 0 to 10, scaling the gusts. 0 leaves each Flake's own sway. */
    int windStrength = 4;

    /**
     * Whether a Flake blown in past the side edge of a Snow Catcher goes on
     * being drawn while it is over it.
     *
     * A gust carries Flakes sideways, and one that crosses a window's left or
     * right edge below the line snow settles on has not landed on anything: it
     * never crossed the catching edge, so it goes on falling (Snowfall::land).
     * Where it falls is in front of the window, because Flakes paint above the
     * whole stack (ADR-0003), and that is the wind made visible -- snow
     * streaming across a window face is the one place the gusts can be seen
     * doing something to a Flake other than moving it.
     *
     * Switched off, such a Flake is held back from the frame instead, which
     * reads as it passing behind the window: it disappears at the edge it went
     * in past and is drawn again where it comes out below. The simulation is
     * the same either way -- the same Flakes, falling at the same rate, leaving
     * the same snow on the same Catchers -- so this is a drawing choice and
     * nothing else.
     */
    bool flakesInFrontOfWindows = true;

    /**
     * How deep a Column may get, in logical pixels. Snow that would go over the
     * cap is discarded, not shed onto whatever is below (spec: Model).
     *
     * Lowering it does not cut the snow that is already deeper than it: that is
     * left for the melt, and for as long as the melt takes, a Cap is drawn
     * against what is standing rather than against the cap (cap.h:
     * capReferenceDepth).
     */
    int maxDepth = 8;

    /**
     * How fast every Column decays, in logical pixels per second. 0 means
     * permanent accumulation.
     *
     * This is a threshold rather than a balance, which is the one thing about
     * it that surprises. Snowfall adds at a rate that does not depend on how
     * deep a Column already is, and melt takes away at a rate that does not
     * either, so the two never settle against each other: below the snowfall
     * rate every Column climbs to maxDepth and stops there, and above it every
     * Column sits at bare. There is no setting that gives half a pile.
     *
     * The rate to beat, measured off this simulation at fallSpeed 5, is
     * 0.036 * density logical px/s per Column on the desktop and 0.070 *
     * density on a window, the desktop's being lower because a Flake that
     * falls all the way to it is a Flake that took longer to get there. The
     * default is under the lowest of those that anybody is likely to run --
     * density 2 -- so that snow lies at every density but the very lightest,
     * rather than appearing between one notch of the density slider and the
     * next. What it still buys at that rate is the other half of the job: snow
     * that has stopped being fed goes, over the minutes rather than the
     * seconds that meltRateFor() below is for.
     */
    qreal meltRate = 0.2;

    /**
     * How many frames a second the snow is animated at.
     *
     * The effect asks for a repaint of the whole screen for every frame it
     * animates and nothing else on a still desktop asks for any, so this is
     * very nearly the whole of what the effect costs -- which is why the spec
     * caps it well under a display's refresh rate rather than letting it run at
     * whatever the compositor offers.
     */
    int frameRateCap = 30;

    FlakeStyle flakeStyle = FlakeStyle::Depth;

    CapStyle capStyle = CapStyle::Contour;
};

/**
 * The seconds between two animated frames at @a frameRateCap frames a second.
 *
 * Clamped rather than trusted, because the whole effect hangs off it: a cap of
 * zero from a hand-edited config would otherwise be a division by zero and then
 * a timer asking for frames as fast as the event loop can deliver them. The
 * upper end is past the fastest display anyone has, so it reads as "no cap".
 */
inline qreal frameInterval(int frameRateCap)
{
    return 1.0 / std::clamp(frameRateCap, 1, 240);
}

/**
 * How fast the Snowline of a Snow Catcher whose class has just been switched
 * off melts, in logical pixels a second.
 *
 * Fast enough that a Cap at the default maximum depth is gone in under three
 * seconds, slow enough that what happens is visibly melting. Both halves
 * matter, and for the same reason: the KCM is where somebody is watching
 * closely, so snow that vanished the instant a checkbox moved would read as a
 * bug, and snow that took the configured melt rate's fifty seconds to go would
 * read as the checkbox not working.
 */
inline constexpr qreal s_disabledMeltRate = 8.0;

/**
 * The rate a Snowline melts at: the configured one while its Catcher's class
 * is @a solid, and the accelerated one while it is not.
 *
 * Never slower than it was melting anyway, so a configuration that already
 * melts faster than the accelerated rate is left to get on with it.
 */
inline qreal meltRateFor(bool solid, qreal meltRate)
{
    return solid ? meltRate : std::max(s_disabledMeltRate, meltRate);
}

/**
 * The Settings as `[Effect-snow]` has them right now.
 *
 * Defined in settings.cpp, which is the one file here that knows about
 * KConfigXT and about KWin's config object; the tests link the struct without
 * it.
 */
Settings configuredSettings();

/** One line of what @a settings hold, for the log line on a reconfigure. */
QString describeSettings(const Settings &settings);

} // namespace Snow
