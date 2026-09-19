/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QtGlobal>

namespace Snow
{

/**
 * One falling snow particle.
 *
 * Plain data: everything that moves a Flake lives in Snowfall, which owns the
 * whole population of one output and steps it. Positions are global *logical*
 * pixels (ADR-0001), the same coordinates a Catcher's geometry is in, so hit
 * testing in ticket 05 can compare the two without converting anything.
 *
 * The per-Flake randomness is what keeps a few hundred Flakes from reading as
 * one rigid sheet: no two share a size, a sway, a response to the wind or a
 * spin, and under the `depth` Flake style no two share a fall speed either.
 */
struct Flake
{
    /** Where the Flake is now, in global logical pixels. */
    qreal x = 0;
    qreal y = 0;

    /**
     * Where the Flake was at the end of the previous step.
     *
     * A Flake moves further in one frame than a thin pile is deep, so landing
     * cannot be "is the Flake below the surface" without Flakes tunnelling
     * through shallow snow. Ticket 05 tests the segment previousY..y against a
     * Catcher's surface instead, which is why this is recorded here rather
     * than recomputed there.
     */
    qreal previousY = 0;

    /**
     * How near the viewer this Flake is: 0 is the far plane, 1 the near one.
     *
     * Only the `depth` Flake style reads it, where it scales fall speed here
     * and size and opacity at paint time (ticket 06), giving parallax.
     */
    qreal z = 0;

    /** Base radius in logical pixels, before any z scaling at paint time. */
    qreal radius = 0;

    /** The Flake's own side-to-side flutter: where in it, how fast, how far. */
    qreal swayPhase = 0;
    qreal swayFrequency = 0;
    qreal swayAmplitude = 0;

    /**
     * How much of a gust this Flake picks up, 0 (barely stirred) to 1 (fully
     * carried). A gust is one number for the whole desktop; this is what
     * spreads it out into something that looks like air rather than a conveyor.
     */
    qreal windResponse = 0;

    /**
     * Whether a Snow Catcher is standing between this Flake and the viewer, so
     * that it is not drawn this frame.
     *
     * Only ever set for a Flake that a gust blew in past a side edge, and only
     * while `flakesInFrontOfWindows` is off: that is the whole of what the
     * setting does. Settled once a step by Snowfall::land(), which is where the
     * Catchers already are, rather than asked again at paint time.
     */
    bool covered = false;

    /** Current angle and angular velocity, in radians. Drawn by `crystal`. */
    qreal rotation = 0;
    qreal rotationSpeed = 0;
};

} // namespace Snow
