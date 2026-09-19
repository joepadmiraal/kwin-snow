/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "flake.h"
#include "settings.h"

#include <QList>
#include <QRandomGenerator>
#include <QRectF>
#include <QString>

namespace Snow
{

class Catcher;

/**
 * The falling snow of one output.
 *
 * The simulation is per output and a Flake never leaves the one it belongs to
 * (spec: Rendering): Flakes spawn above this output's top edge, wrap around its
 * sides, and respawn once they pass its bottom. A shared field spanning every
 * output would put Flakes in mid-air wherever two outputs differ in height or
 * vertical offset, which is why the population is cut up this way rather than
 * simulated once and clipped.
 *
 * There is no KWin in here at all -- a Snowfall knows a rectangle in global
 * logical pixels, a list of Catchers that is likewise nothing but geometry, and
 * nothing about what draws either -- which is what lets the motion and the
 * landing be checked in a unit test instead of by watching a compositor.
 * SnowfallRegistry is where outputs become rectangles, and CatcherRegistry is
 * where windows become Catchers.
 *
 * Note the two clocks in step(): the time *since the last step* moves Flakes,
 * and the time *since the snow started* is what gusts and sway are read from.
 * The second one has to be absolute, because it is shared: a gust is one number
 * for the whole desktop, so two outputs stepping from the same elapsed time see
 * the same weather even when their frames do not line up.
 */
class Snowfall
{
public:
    /**
     * A Snowfall over @a geometry, already full: the initial population is
     * scattered down the output rather than queued above it, so switching the
     * effect on looks like snow that has been falling for a while.
     */
    Snowfall(const QRectF &geometry, const Settings &settings, quint32 seed);

    QRectF geometry() const
    {
        return m_geometry;
    }

    /**
     * Follow the output. The population follows the new area over the next few
     * seconds, the same way it follows a change of `density`; Flakes the new
     * rectangle has left behind are wrapped or respawned on the next step.
     */
    void setGeometry(const QRectF &geometry);

    void setSettings(const Settings &settings);

    const QList<Flake> &flakes() const
    {
        return m_flakes;
    }

    /**
     * How many Flakes this output should be holding, from `density` and the
     * output's area. An area rate rather than a count is what makes a density
     * of 5 look the same at 1080p and at 4K (spec: Configuration).
     *
     * What it is *holding* only equals this at rest. A target that has moved --
     * `density` changed, or the output resized -- is approached rather than
     * jumped to; see step().
     */
    int targetFlakeCount() const;

    /**
     * Move every Flake on by @a delta seconds, with gusts and sway read from
     * @a elapsed seconds since the snow started falling, and land the ones that
     * crossed a surface of @a catchers on the way.
     *
     * @a catchers is in paint order, bottom to top, and is hit-tested in
     * reverse: a Flake stops on the first Solid Catcher whose surface it
     * crossed, counting down from the top of the stack (spec: Model). It is
     * the Catchers whose surfaces lie over this output, which the caller
     * selects: a window straddling two outputs is in both lists and catches
     * over the whole of its top edge, while a surface belonging to a monitor
     * above this one is in neither, because in global coordinates it is a line
     * across this output that no Flake here should ever reach
     * (CatcherRegistry::catchersFor).
     *
     * @a delta is expected to be a sane frame time; the caller that owns the
     * clock is the one that clamps it, so that a test can ask for any step it
     * likes.
     *
     * This is also where the population follows targetFlakeCount(), and it
     * follows it the long way round on purpose (spec: Behaviour, "Settings
     * changed"). Raising `density` spawns faster, into the band above the
     * output, so more snow *arrives*; lowering it stops spawning, so the Flakes
     * that land or fall past the bottom are simply not put back. Neither adds
     * or removes a Flake anyone is looking at.
     */
    void step(qreal delta, qreal elapsed, const QList<Catcher *> &catchers = {});

    /**
     * The wind at @a elapsed seconds, from -1 (hard left) to 1 (hard right).
     *
     * Three sine waves at unrelated periods, which over a minute reads as gusts
     * that rise, hold and die away rather than as a pattern. Static and
     * time-only because the weather is one thing: every output and every Flake
     * asks the same function and differs only in how much of it it picks up.
     */
    static qreal gustAt(qreal elapsed);

    /** One line of Flake positions, for the line logged when a Snowfall starts. */
    QString describe() const;

private:
    /** Where a new Flake starts: streaming in from above, or already falling. */
    enum class Entry {
        AboveTheTop,
        Scattered,
    };

    /** Refill @a flake as a new one. Reusing the slot keeps the population put. */
    void respawn(Flake &flake, Entry entry);

    /**
     * Fill the output to targetFlakeCount() in one go, with Flakes already
     * falling down it.
     *
     * The one place the population jumps, and it is a Snowfall that has no
     * population yet: a new output, or the effect being switched on. Everything
     * after that goes through spawn() and attrition, because by then there is
     * somebody watching.
     */
    void fill();

    /**
     * Add the Flakes this step is allowed to, which is at most
     * s_populationGrowthPerSecond of the target per second.
     *
     * Never removes any: a population over its target comes down by attrition
     * in step(), not by Flakes being taken out of the air.
     */
    void spawn(qreal delta);

    /**
     * Land @a flake on the topmost Catcher of m_catching it has reached the
     * surface of, and say whether one caught it.
     *
     * What counts as reaching it is the segment previousY..y crossing the
     * surface, rather than "is the Flake below it": a Flake at the near plane
     * covers most of a shallow pile's depth in a single frame, so testing the
     * point alone would let it tunnel through thin snow. A Flake that has ended
     * up inside the snow by some other route counts too; see the implementation
     * for the two ways that happens.
     *
     * Also where @a flake is told whether it is behind a Catcher rather than in
     * front of one (Flake::covered), which is why it is taken by reference. A
     * Flake blown in past a side edge is not caught -- it never crossed the
     * catching edge -- and `flakesInFrontOfWindows` is which of the two things
     * it does instead: fall on in front of the window, or wait to come out
     * below it. Neither is a change to where it is; only to whether it is
     * drawn.
     */
    bool land(Flake &flake) const;

    /** Slide @a flake back in through the opposite side of the output. */
    void wrap(Flake &flake) const;

    /** A number in [0, 1). */
    qreal random();

    QRectF m_geometry;
    Settings m_settings;
    QRandomGenerator m_random;
    QList<Flake> m_flakes;

    /**
     * The Catchers of the step being taken that are actually catching, in the
     * paint order they arrived in.
     *
     * Which of them are Solid and which are concealed is a question about the
     * Catcher and the settings, and neither moves during a step -- so it is
     * asked once a Catcher here rather than once a Catcher per Flake, which at
     * a few thousand Flakes over a desktop's worth of windows is the difference
     * between a few dozen answers a frame and a hundred thousand. Kept between
     * steps for the room it has claimed rather than for anything in it.
     */
    QList<Catcher *> m_catching;

    /**
     * The fraction of a Flake this Snowfall is owed, carried between steps.
     *
     * Without it a growth rate slower than one Flake a frame would round to
     * nothing every frame and the population would never move at all.
     */
    qreal m_spawnCredit = 0;
};

} // namespace Snow
