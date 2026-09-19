/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "snowfall.h"

#include "catcher.h"

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Snow
{

/*
    The numbers below are the published prototype's, restated in the units the
    effect works in -- logical pixels and seconds. The prototype is the
    reference for how the motion should feel (spec: Reference), so where a
    constant has no better justification than "this looked right", it is
    because it did, over there, at 30 fps on a 1600x1000 desktop.
*/

// Flakes per megapixel of logical output area, per step of `density`: the
// prototype's 95 Flakes a step over its 1600x1000 canvas. An area rate, not a
// count, so a density of 5 reads the same at 1080p and at 4K (spec:
// Configuration).
static constexpr qreal s_flakesPerDensityPerMegapixel = 59.375;

// However large an output gets, it is never worth more Flakes than this. At the
// rate above a density of 10 wants ~7000 Flakes on a 4K output, so this is a
// guard against a nonsense geometry rather than a limit anyone can reach.
static constexpr int s_maximumFlakes = 20000;

// How fast a Snowfall grows toward a raised population, as a fraction of the
// target per second: an empty output fills in about three seconds, and one step
// of `density` arrives in well under one. Changing `density` "spawns faster"
// rather than adding Flakes instantly (spec: Behaviour), and this is how much
// faster. There is no matching rate for shrinking, because shrinking is not
// done by removing Flakes at all -- see step().
static constexpr qreal s_populationGrowthPerSecond = 0.35;

// Fall speed in logical px/s: the floor, and what each step of `fallSpeed` adds.
static constexpr qreal s_fallSpeedFloor = 40.0;
static constexpr qreal s_fallSpeedPerStep = 34.0;

// Under the `depth` Flake style, how much of that speed a Flake at the far
// plane keeps, and how much more it gains by the near one. Distant snow falling
// slower than near snow is most of what makes the parallax read as depth rather
// than as a size difference.
static constexpr qreal s_farFallFactor = 0.45;
static constexpr qreal s_fallFactorRange = 0.9;

// A full-strength gust in logical px/s, per step of `windStrength`.
static constexpr qreal s_gustPerWindStep = 26.0;

// How much of a gust a Flake picks up: the floor, and how much more the most
// responsive Flake takes. A gust is one number for the whole desktop, so this
// spread is what turns it into air rather than a conveyor belt.
static constexpr qreal s_windResponseFloor = 0.55;
static constexpr qreal s_windResponseRange = 0.9;

// Sway in logical px/s per unit of a Flake's own amplitude. Independent of
// `windStrength`: the gusts are the weather, the sway is the Flake's own
// flutter, and snow still wanders about on a still day.
static constexpr qreal s_swaySpeed = 12.0;

// How far outside the side edges of an output Flakes live, in logical px.
// Spawning within the wider band and wrapping at the narrower one means a Flake
// blows off one side and returns on the other rather than popping at the edge.
static constexpr qreal s_spawnMargin = 100.0;
static constexpr qreal s_wrapMargin = 110.0;

// How far below an output a Flake falls before it is spent. The ground Catcher
// normally catches it first; this is what happens when the desktop class is
// switched off, and the margin keeps the respawn off-screen.
static constexpr qreal s_bottomMargin = 30.0;

// The band above an output that new Flakes enter through, in logical px: a
// Flake starts at least s_entryOffset above the top edge and up to
// s_entryHeight higher, so a refilled population arrives as a drift rather than
// as one rank.
static constexpr qreal s_entryOffset = 20.0;
static constexpr qreal s_entryHeight = 200.0;

// How much snow one landing is worth, in logical pixels of depth spread over
// the landing Column and its two neighbours. The prototype's, and the unit that
// makes maxDepth and meltRate mean what the spec's defaults assume they mean.
static constexpr qreal s_flakeMass = 1.0;

// Under the `depth` Flake style, how much of that a Flake at the far plane
// carries and how much more it gains by the near one -- the same parallax that
// sizes it at paint time, followed through to what it leaves behind, so that
// distant snow builds a pile more slowly than near snow.
static constexpr qreal s_farMassFactor = 0.45;
static constexpr qreal s_massFactorRange = 0.9;

// How many Flakes describe() spells out. Enough to watch a few positions move
// between one dump and the next.
static constexpr int s_describedFlakes = 3;

Snowfall::Snowfall(const QRectF &geometry, const Settings &settings, quint32 seed)
    : m_geometry(geometry)
    , m_settings(settings)
    , m_random(seed)
{
    // Scattered, not queued above the top edge: switching the effect on should
    // look like snow that has been falling for a while, not like snow that
    // starts when you are watching.
    fill();
}

void Snowfall::setGeometry(const QRectF &geometry)
{
    m_geometry = geometry;
    // The population is a function of area and the wrap band a function of the
    // edges, so both follow from the next step; nothing has to be moved here.
}

void Snowfall::setSettings(const Settings &settings)
{
    m_settings = settings;
}

int Snowfall::targetFlakeCount() const
{
    const int density = std::clamp(m_settings.density, 1, 10);
    const qreal megapixels = m_geometry.width() * m_geometry.height() / 1.0e6;
    const qreal wanted = density * s_flakesPerDensityPerMegapixel * megapixels;
    return std::clamp(int(std::lround(wanted)), 0, s_maximumFlakes);
}

qreal Snowfall::gustAt(qreal elapsed)
{
    // Three sines at unrelated periods -- roughly 20 s, 48 s and 90 s -- which
    // beat against each other for long enough that the repeat is not something
    // anyone notices. Halved to land back in [-1, 1].
    return (std::sin(elapsed * 0.31)
            + std::sin(elapsed * 0.13 + 2.1) * 0.6
            + std::sin(elapsed * 0.07 + 4.3) * 0.4)
        / 2.0;
}

void Snowfall::step(qreal delta, qreal elapsed, const QList<Catcher *> &catchers)
{
    spawn(delta);

    // Once for the step rather than once per Flake per Catcher: a class being
    // Solid and a Catcher being concealed are both settled before the step
    // begins and neither moves inside it (see m_catching).
    m_catching.clear();
    for (Catcher *catcher : catchers) {
        if (catcher->isCatching(m_settings)) {
            m_catching.append(catcher);
        }
    }

    const int target = targetFlakeCount();
    const int windStrength = std::clamp(m_settings.windStrength, 0, 10);
    const int fallSpeed = std::clamp(m_settings.fallSpeed, 1, 10);

    const qreal gust = gustAt(elapsed) * windStrength * s_gustPerWindStep;
    const qreal fall = s_fallSpeedFloor + fallSpeed * s_fallSpeedPerStep;
    const bool parallax = m_settings.flakeStyle == FlakeStyle::Depth;
    const qreal spent = m_geometry.bottom() + s_bottomMargin;

    // By index, because a Flake can leave the population here as well as be
    // reused, and a Flake leaving moves the one behind it into its place.
    for (int index = 0; index < m_flakes.size();) {
        Flake &flake = m_flakes[index];
        flake.previousY = flake.y;

        const qreal speed = parallax ? fall * (s_farFallFactor + s_fallFactorRange * flake.z) : fall;
        flake.y += speed * delta;

        const qreal carried = gust * (s_windResponseFloor + s_windResponseRange * flake.windResponse);
        const qreal sway = std::sin(elapsed * flake.swayFrequency + flake.swayPhase)
            * flake.swayAmplitude * s_swaySpeed;
        flake.x += (carried + sway) * delta;

        flake.rotation += flake.rotationSpeed * delta;

        wrap(flake);

        // A Flake that has landed is snow now, and a Flake that fell past the
        // bottom of an output with nothing to catch it is gone: both leave the
        // sky one Flake short, and both are normally answered by putting it
        // back at the top rather than by shortening the population.
        if (land(flake) || flake.y > spent) {
            if (m_flakes.size() > target) {
                // Unless there are too many, which is `density` coming down or
                // the output shrinking. This is the whole of how a population
                // shrinks: a Flake is never plucked out of the air, it is one
                // that has finished falling and is not put back, so the snow
                // thins out over the time it takes to fall rather than at the
                // moment somebody moves a slider (spec: Behaviour).
                //
                // The last Flake takes its place. The list is in no spatial
                // order, so which one that is means nothing.
                m_flakes[index] = m_flakes.last();
                m_flakes.removeLast();
                continue;
            }
            respawn(flake, Entry::AboveTheTop);
        }

        ++index;
    }
}

bool Snowfall::land(Flake &flake) const
{
    // Settled again from scratch every step: a Flake is behind a Catcher for
    // exactly as long as it is inside one, so a window that closes, moves or
    // stops being Solid hands its Flakes back without anything having to
    // notice.
    flake.covered = false;

    const bool parallax = m_settings.flakeStyle == FlakeStyle::Depth;
    const qreal mass = parallax
        ? s_flakeMass * (s_farMassFactor + s_massFactorRange * flake.z)
        : s_flakeMass;

    // Topmost first, so a Flake stops on the window in front rather than on the
    // one behind it. A Catcher that is not catching -- its class disabled, or
    // itself off screen -- is not skipped over as a near miss: it is not there
    // at all, and the search carries on down as if the surface did not exist
    // (spec: Model, and Catcher::isCatching). Which is why it is not in
    // m_catching to begin with.
    for (int index = m_catching.size() - 1; index >= 0; --index) {
        Catcher *catcher = m_catching.at(index);

        const int column = catcher->columnAt(flake.x);
        if (column < 0) {
            continue;
        }

        const qreal surface = catcher->surfaceAt(column);
        if (flake.y < surface) {
            continue; // still in the air over this one
        }

        // Past the surface, and it belongs to this Catcher if it came down
        // through the catching edge: at the start of the step it was at or
        // above the top edge, and it is under the snow now.
        //
        // The top edge rather than the surface, and the difference between the
        // two is every Flake that was already under the surface when the step
        // began without having gone past the edge to get there. One that ends a
        // step just short of the surface is overtaken by the snow when another
        // Flake lands in its Column and raises the surface by more than the
        // gap; and one blown sideways arrives in a Column whose pile has
        // already grown past it. Both are inside the snow, which is not
        // somewhere a Flake can be, so they settle there.
        //
        // Testing where the Flake *ends up* instead -- inside the band between
        // the surface and the top edge -- only holds while that band is deeper
        // than a Flake falls in one frame. Under a thin Cap it is not, and the
        // Flake crosses the whole pile and comes out below the top edge in a
        // single step, which is a Flake visibly falling through the top of a
        // window. Measured at 33 in thirty seconds on one window at wind 10,
        // every one of them over a Column under a pixel deep, which is why it
        // showed while the snow was building and stopped once it was deep
        // (snowfalltest: aThinCapDoesNotLeak).
        //
        // What this deliberately does not catch is a Flake that came in past a
        // side edge, below the top: it never crossed the catching edge at all,
        // so it is not this Catcher's snow and it goes on falling.
        if (flake.previousY <= catcher->geometry().top()) {
            catcher->deposit(column, mass, m_settings);
            return true;
        }

        // Where it falls is in front of the window, because Flakes paint above
        // the whole stack (ADR-0003) -- unless the setting says otherwise, in
        // which case it is held back from the frame for as long as it is inside
        // the Catcher, which is what it looks like to pass behind one. Its own
        // fall is untouched either way, and so is everything it does to the
        // snow: the search goes on down the stack exactly as before, so a
        // Catcher below can still catch it (settings.h:
        // flakesInFrontOfWindows).
        if (!m_settings.flakesInFrontOfWindows && flake.y < catcher->geometry().bottom()) {
            flake.covered = true;
        }
    }

    return false;
}

void Snowfall::fill()
{
    const int target = targetFlakeCount();
    m_flakes.reserve(target);
    while (m_flakes.size() < target) {
        Flake flake;
        respawn(flake, Entry::Scattered);
        m_flakes.append(flake);
    }
}

void Snowfall::spawn(qreal delta)
{
    const int target = targetFlakeCount();
    if (m_flakes.size() >= target) {
        // Nothing owed, and nothing banked either: a population that has been
        // sitting at its target for a minute must not spend that minute's worth
        // of credit the moment `density` goes up.
        m_spawnCredit = 0;
        return;
    }

    m_spawnCredit += target * s_populationGrowthPerSecond * delta;

    const int wanted = std::min(target - int(m_flakes.size()), int(m_spawnCredit));
    m_spawnCredit -= wanted;

    m_flakes.reserve(m_flakes.size() + wanted);
    for (int spawned = 0; spawned < wanted; ++spawned) {
        // Into the band above the output, never where they can already be seen:
        // a raised `density` has to read as more snow arriving rather than as a
        // flash of Flakes appearing mid-air.
        Flake flake;
        respawn(flake, Entry::AboveTheTop);
        m_flakes.append(flake);
    }
}

void Snowfall::respawn(Flake &flake, Entry entry)
{
    flake.x = m_geometry.left() - s_spawnMargin + random() * (m_geometry.width() + 2 * s_spawnMargin);
    flake.y = entry == Entry::AboveTheTop
        ? m_geometry.top() - s_entryOffset - random() * s_entryHeight
        : m_geometry.top() + random() * m_geometry.height();

    // A respawned Flake has not travelled from wherever it was: leaving
    // previousY behind would hand ticket 05 a segment the length of the screen
    // and land the Flake on the first Catcher under it.
    flake.previousY = flake.y;

    // Wherever it was, it was not behind anything up here.
    flake.covered = false;

    // The prototype's spread, which is wide enough that no two Flakes read as
    // copies and narrow enough that none of them reads as wrong.
    flake.z = random();
    flake.radius = 2.6 + random() * 2.4;
    flake.swayAmplitude = 0.5 + random() * 1.5;
    flake.swayFrequency = 0.6 + random() * 1.1;
    flake.swayPhase = random() * 2 * std::numbers::pi;
    flake.windResponse = random();
    flake.rotation = random() * 2 * std::numbers::pi;
    flake.rotationSpeed = (random() - 0.5) * 1.2;
}

void Snowfall::wrap(Flake &flake) const
{
    // Modulo rather than a single add, so a Flake is brought back in however
    // far out it is -- an output that shrank can leave one a long way outside.
    const qreal span = m_geometry.width() + 2 * s_wrapMargin;
    if (span <= 0) {
        return;
    }

    const qreal lower = m_geometry.left() - s_wrapMargin;
    qreal offset = std::fmod(flake.x - lower, span);
    if (offset < 0) {
        offset += span;
    }
    flake.x = lower + offset;
}

qreal Snowfall::random()
{
    return m_random.generateDouble();
}

QString Snowfall::describe() const
{
    QStringList sample;
    for (int index = 0; index < std::min(s_describedFlakes, int(m_flakes.size())); ++index) {
        const Flake &flake = m_flakes.at(index);
        sample.append(QStringLiteral("#%1 (%2, %3) z %4")
                          .arg(index)
                          .arg(flake.x, 0, 'f', 1)
                          .arg(flake.y, 0, 'f', 1)
                          .arg(flake.z, 0, 'f', 2));
    }

    return QStringLiteral("%1 flakes of %2, %3")
        .arg(m_flakes.size())
        .arg(targetFlakeCount())
        .arg(sample.join(QStringLiteral(", ")));
}

} // namespace Snow
