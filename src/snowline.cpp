/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "snowline.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace Snow
{

// How many Columns of a describe() line are spelled out at each end before it
// elides. Enough to show where the snow is anchored after a resize.
static constexpr int s_describedColumns = 4;

// How a landing is split between the Column the Flake came down in and each of
// its two neighbours. The prototype's shares, and they sum to exactly 1: a
// Flake in the middle of a Snowline is worth the same amount of snow wherever
// it lands, and only the edges lose the share that falls off the end.
static constexpr qreal s_depositCentreShare = 0.62;
static constexpr qreal s_depositNeighbourShare = 0.19;

// How far a Column may stand above a neighbour before relaxation starts moving
// snow across, in logical pixels: the angle of repose, expressed at the one
// Column width the whole effect works in.
static constexpr qreal s_reposeThreshold = 1.6;

// How much of the excess over that threshold moves per second. The prototype
// moved 0.16 of it per frame at its 30 fps cap, which is this rate -- restated
// per second so that the snow settles at the same speed whatever the frame rate
// turns out to be.
static constexpr qreal s_relaxRate = 4.8;

// However long a frame took, never move more than this fraction of the excess
// in one go. Two neighbours can each take a share, so anything approaching 0.5
// would let a Column overshoot past its neighbours and ring instead of settle.
static constexpr qreal s_maximumRelaxFraction = 0.25;

Snowline::Snowline(qreal width)
    : m_depths(columnCountFor(width), 0.0)
{
}

int Snowline::columnCountFor(qreal width)
{
    // Ceil, so the rightmost sliver of a Catcher still has a Column to land in.
    // Never zero: a Catcher with no width is degenerate, and an empty array
    // would make every caller check for it.
    return std::max(1, int(std::ceil(width / columnWidth)));
}

int Snowline::columnAt(qreal localX) const
{
    if (localX < 0) {
        return -1;
    }
    const int column = int(localX / columnWidth);
    return column < m_depths.size() ? column : -1;
}

qreal Snowline::columnCentre(int column) const
{
    return column * columnWidth + columnWidth / 2;
}

qreal Snowline::depth(int column) const
{
    if (column < 0 || column >= m_depths.size()) {
        return 0.0;
    }
    return m_depths.at(column);
}

void Snowline::setDepth(int column, qreal depth)
{
    if (column < 0 || column >= m_depths.size()) {
        return;
    }
    m_depths[column] = depth;
    // Either way: this is the one setter that can put a Column down as well as
    // up, and it has no pass of its own to work the new deepest out in.
    forgetMaximumDepth();
}

void Snowline::forgetMaximumDepth()
{
    m_maximumKnown = false;
}

void Snowline::addClamped(int column, qreal depth, qreal maximumDepth)
{
    if (column < 0 || column >= m_depths.size()) {
        return;
    }

    // A Column already at or over the cap takes nothing and, just as
    // importantly, loses nothing: lowering `maxDepth` leaves deeper snow
    // standing where it is, for the melt to bring down over the next few
    // seconds rather than for the next Flake to cut off (spec: Behaviour,
    // "Settings changed"). Clamping what is there would have been a snap, and
    // one that only happened where snow happened to be falling.
    const qreal standing = m_depths.at(column);
    if (standing >= maximumDepth) {
        return;
    }

    // Clamped rather than accumulated and clamped later: the snow over the cap
    // is gone the moment it lands, so a Snowline that has been full for an hour
    // melts away in exactly as long as one that filled up a second ago.
    const qreal landed = std::min(maximumDepth, standing + depth);
    m_depths[column] = landed;

    // A landing can only raise a Column, so the deepest is still known: it is
    // whichever of the two is larger.
    if (m_maximumKnown) {
        m_maximumDepth = std::max(m_maximumDepth, landed);
    }
}

void Snowline::deposit(int column, qreal mass, qreal maximumDepth)
{
    if (column < 0 || column >= m_depths.size()) {
        return;
    }

    addClamped(column, mass * s_depositCentreShare, maximumDepth);
    addClamped(column - 1, mass * s_depositNeighbourShare, maximumDepth);
    addClamped(column + 1, mass * s_depositNeighbourShare, maximumDepth);
}

void Snowline::relax(qreal delta)
{
    const qreal fraction = std::min(s_maximumRelaxFraction, s_relaxRate * delta);
    if (fraction <= 0) {
        return;
    }

    // Snow moves sideways here, so the deepest Column can come down as well as
    // change places -- and a Column's final depth is not settled until the
    // iteration after the one that writes it, which is why this is left to the
    // next reader rather than worked out on the way past. In the ordinary frame
    // that reader is melt(), immediately below in Catcher::settle().
    forgetMaximumDepth();

    // Every Column is measured against what its neighbours were at the start of
    // the pass, not against what earlier Columns in the same pass have already
    // made of them. Reading the array as it is written would let snow cascade
    // rightward in one sweep but never leftward, which over a minute walks a
    // pile sideways. Three saved values are enough to get that without copying
    // the array: m_depths[i + 1] has not been touched yet when it is read, and
    // what it reads becomes the next iteration's untouched middle.
    const int count = m_depths.size();
    qreal left = 0;
    qreal middle = m_depths.isEmpty() ? 0 : m_depths.first();

    for (int column = 0; column < count; ++column) {
        const qreal right = column + 1 < count ? m_depths.at(column + 1) : 0;

        if (column > 0 && middle - left > s_reposeThreshold) {
            const qreal moved = (middle - left) * fraction;
            m_depths[column] -= moved;
            m_depths[column - 1] += moved;
        }
        if (column + 1 < count && middle - right > s_reposeThreshold) {
            const qreal moved = (middle - right) * fraction;
            m_depths[column] -= moved;
            m_depths[column + 1] += moved;
        }

        left = middle;
        middle = right;
    }
}

void Snowline::melt(qreal delta, qreal rate)
{
    const qreal amount = rate * delta;
    if (amount <= 0) {
        // A melt rate of 0 is permanent accumulation (spec: Configuration).
        return;
    }

    // The walk every Column has to be put through anyway, so the deepest of
    // them comes out of it for nothing -- which is what makes the question the
    // paint path asks of every Catcher free for the rest of the frame.
    qreal deepest = 0;
    for (qreal &depth : m_depths) {
        depth = std::max(0.0, depth - amount);
        deepest = std::max(deepest, depth);
    }

    m_maximumDepth = deepest;
    m_maximumKnown = true;
}

qreal Snowline::maximumDepth() const
{
    if (!m_maximumKnown) {
        const auto deepest = std::max_element(m_depths.cbegin(), m_depths.cend());
        m_maximumDepth = deepest == m_depths.cend() ? 0.0 : std::max(0.0, *deepest);
        m_maximumKnown = true;
    }
    return m_maximumDepth;
}

bool Snowline::isEmpty() const
{
    // Every Column bare is the deepest one bare, and asking it that way is what
    // keeps the frame's first question about a Catcher that is holding nothing
    // -- which on a busy desktop is most of them -- from walking its Columns.
    return maximumDepth() <= 0.0;
}

void Snowline::setWidth(qreal width)
{
    const int count = columnCountFor(width);
    if (count == m_depths.size()) {
        // Every move lands here, and so does every resize too small to cross a
        // Column boundary. Nothing to do: local coordinates make it free.
        return;
    }

    // Anchored at Column 0 -- QList::resize truncates on the right and
    // value-initialises new Columns to a depth of zero, which is exactly the
    // rule ADR-0001 asks for. Shrinking loses the snow that fell off the right
    // edge; growing back does not bring it back, and should not.
    m_depths.resize(count);

    // Truncating on the right can take the deepest Column with it.
    forgetMaximumDepth();
}

QString Snowline::describe() const
{
    qreal total = 0;
    QStringList head;
    QStringList tail;
    for (int column = 0; column < m_depths.size(); ++column) {
        total += m_depths.at(column);
        if (column < s_describedColumns) {
            head.append(QString::number(m_depths.at(column), 'g', 4));
        } else if (column >= m_depths.size() - s_describedColumns) {
            tail.append(QString::number(m_depths.at(column), 'g', 4));
        }
    }

    const QString contents = tail.isEmpty()
        ? head.join(QLatin1Char(' '))
        : QStringLiteral("%1 .. %2").arg(head.join(QLatin1Char(' ')), tail.join(QLatin1Char(' ')));

    return QStringLiteral("%1 cols, sum %2, [%3]")
        .arg(m_depths.size())
        .arg(total, 0, 'f', 1)
        .arg(contents);
}

} // namespace Snow
