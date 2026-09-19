/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QList>
#include <QString>

namespace Snow
{

/**
 * The accumulated snow of one Snow Catcher.
 *
 * A Snowline is an array of depths indexed by Column in its Catcher's own
 * local X coordinates: Column 0 starts at the Catcher's left edge, and depth
 * grows upward from the Catcher's top edge. Both the Column width and the
 * depths are *logical* pixels (ADR-0001), never device pixels -- conversion
 * happens at paint time, which is what lets a Snowline survive its Catcher
 * moving to an output with a different scale factor.
 *
 * Local coordinates are the whole point of the arrangement: a Catcher that
 * moves does not have to tell its Snowline anything, because none of the
 * numbers in here mention the screen.
 */
class Snowline
{
public:
    /**
     * The width of one Column, in logical pixels. Five is the prototype's
     * value: fine enough that a pile reads as a contour rather than as steps,
     * coarse enough that a 4K-wide ground is under a thousand Columns.
     */
    static constexpr qreal columnWidth = 5.0;

    /** A Snowline with no snow in it, spanning @a width logical pixels. */
    explicit Snowline(qreal width);

    int columnCount() const
    {
        return m_depths.size();
    }

    /**
     * The Column covering @a localX -- an X in the Catcher's local coordinates,
     * measured from its left edge -- or -1 when that falls outside the Catcher.
     * The right edge is exclusive, so every point belongs to exactly one Column.
     */
    int columnAt(qreal localX) const;

    /**
     * The middle of @a column in local X, which is where its depth is treated
     * as being sampled. The last Column can be a partial one when the Catcher's
     * width is not a whole number of Columns; it is still drawn full width.
     */
    qreal columnCentre(int column) const;

    /** The depth at @a column, or 0 for a Column that does not exist. */
    qreal depth(int column) const;

    void setDepth(int column, qreal depth);

    /**
     * Land @a mass logical pixels of snow at @a column, spreading a smaller
     * share into each immediate neighbour so that a landing builds a small
     * mound rather than a single-Column spike.
     *
     * Every Column is clamped at @a maximumDepth and the excess is *discarded*:
     * a full Snowline stops taking snow rather than passing it on to whatever
     * is underneath (spec: Model). A Column already deeper than that -- which is
     * what a lowered `maxDepth` leaves behind -- is left standing for the melt
     * to bring down rather than cut to the new cap. Out-of-range Columns are
     * dropped, which is also what makes the neighbours of an edge Column free
     * to not exist.
     */
    void deposit(int column, qreal mass, qreal maximumDepth);

    /**
     * Settle toward an angle of repose over @a delta seconds: wherever a Column
     * stands more than a threshold above a neighbour, some of the difference
     * moves across. Snow is conserved -- nothing is created or lost here, only
     * moved sideways -- so a pile spreads out instead of growing a spike.
     */
    void relax(qreal delta);

    /**
     * Decay every Column by @a rate logical pixels per second over @a delta
     * seconds, never below bare. A @a rate of 0 is permanent accumulation.
     */
    void melt(qreal delta, qreal rate);

    /**
     * The deepest Column, or 0 when there is no snow.
     *
     * Kept rather than counted: this is the question the paint path asks of
     * every Catcher several times a frame -- is there a Cap to draw, and what
     * depth is it shaded against -- and a Snowline can be a thousand Columns
     * wide. The passes that already walk every Column work it out as they go,
     * so in the ordinary frame it has been answered before it is asked.
     */
    qreal maximumDepth() const;

    /** Whether every Column is bare. */
    bool isEmpty() const;

    /**
     * Re-span the Snowline to @a width logical pixels, anchored at the left
     * edge: Columns past the new right edge are dropped and new Columns arrive
     * bare. Deliberately *not* a rescale -- stretching existing snow to fit
     * smears it into something that reads as obviously fake (ADR-0001).
     *
     * A Catcher that only moved calls this with the width it already had, and
     * it is a no-op.
     */
    void setWidth(qreal width);

    /** One line summarising the contents, for logging. */
    QString describe() const;

    bool operator==(const Snowline &other) const
    {
        return m_depths == other.m_depths;
    }

private:
    static int columnCountFor(qreal width);

    /** Add @a depth at @a column, up to @a maximumDepth. Out-of-range is dropped. */
    void addClamped(int column, qreal depth, qreal maximumDepth);

    /** Say that the deepest Column is no longer known, so it is counted again. */
    void forgetMaximumDepth();

    QList<qreal> m_depths;

    /**
     * The deepest Column, when that is known.
     *
     * Only ever a cache of what a walk of m_depths would find. Everything that
     * can lower a depth either works it out on the way past -- melt() walks
     * every Column anyway -- or says it no longer knows, and the next reader
     * pays for the walk. Everything that can only raise one keeps it up to date
     * for nothing.
     */
    mutable qreal m_maximumDepth = 0;
    mutable bool m_maximumKnown = true;
};

} // namespace Snow
