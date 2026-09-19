/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "snowfallregistry.h"

#include "snowlogging.h"

#include <QRandomGenerator>

#include <core/output.h>
#include <effect/effecthandler.h>

#include <algorithm>

namespace Snow
{

// The longest step the simulation will take in one go, in seconds, whatever the
// frame rate cap says. A frame that took longer than this is one where
// something else stalled the compositor -- a resume, a hitch, a suspension that
// has just ended -- and integrating it whole would teleport every Flake down
// the screen. The snow falls a little behind instead, which nobody can see.
static constexpr qreal s_maximumDelta = 0.05;

// ...but a cap below 20 fps asks for steps longer than that, and clamping those
// would quietly slow the snow down rather than stall it: a frame twice as long
// as the cap asked for is the first one that is a stall rather than jitter.
static qreal maximumDelta(const Settings &settings)
{
    return std::max(s_maximumDelta, 2.0 * frameInterval(settings.frameRateCap));
}

SnowfallRegistry::SnowfallRegistry(const Settings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    const QList<KWin::LogicalOutput *> screens = KWin::effects->screens();
    for (KWin::LogicalOutput *output : screens) {
        addOutput(output);
    }

    connect(KWin::effects, &KWin::EffectsHandler::screenAdded, this, &SnowfallRegistry::addOutput);
    connect(KWin::effects, &KWin::EffectsHandler::screenRemoved, this, &SnowfallRegistry::removeOutput);
}

SnowfallRegistry::~SnowfallRegistry() = default;

void SnowfallRegistry::addOutput(KWin::LogicalOutput *output)
{
    connect(output, &KWin::LogicalOutput::geometryChanged, this, [this, output] {
        outputGeometryChanged(output);
    });

    // Its own seed, so two outputs of the same size do not get the same snow
    // falling down both of them.
    const quint32 seed = QRandomGenerator::global()->generate();
    OutputSnowfall &entry = m_outputs[output];
    entry.snowfall = std::make_unique<Snowfall>(output->geometryF(), m_settings, seed);

    qCDebug(KWIN_EFFECT_SNOW).noquote()
        << "Snowfall on" << output->name() << "--" << entry.snowfall->describe();
}

void SnowfallRegistry::removeOutput(KWin::LogicalOutput *output)
{
    disconnect(output, nullptr, this, nullptr);
    // Flakes do not drift between outputs, so an output going away takes its
    // whole population with it; there is nowhere for them to go.
    m_outputs.erase(output);

    qCDebug(KWIN_EFFECT_SNOW).noquote() << "Snowfall gone with output" << output->name();
}

void SnowfallRegistry::outputGeometryChanged(KWin::LogicalOutput *output)
{
    const auto it = m_outputs.find(output);
    if (it != m_outputs.end()) {
        it->second.snowfall->setGeometry(output->geometryF());
    }
}

Snowfall *SnowfallRegistry::snowfallFor(KWin::LogicalOutput *output) const
{
    const auto it = m_outputs.find(output);
    return it == m_outputs.end() ? nullptr : it->second.snowfall.get();
}

QList<Snowfall *> SnowfallRegistry::snowfalls() const
{
    QList<Snowfall *> result;
    result.reserve(int(m_outputs.size()));

    const QList<KWin::LogicalOutput *> screens = KWin::effects->screens();
    for (KWin::LogicalOutput *output : screens) {
        if (Snowfall *snowfall = snowfallFor(output)) {
            result.append(snowfall);
        }
    }

    return result;
}

void SnowfallRegistry::setSettings(const Settings &settings)
{
    m_settings = settings;
    for (auto &[output, entry] : m_outputs) {
        entry.snowfall->setSettings(settings);
    }
}

qreal SnowfallRegistry::elapsedAt(std::chrono::milliseconds presentTime)
{
    if (!m_origin) {
        m_origin = presentTime;
    }
    return std::chrono::duration<qreal>(presentTime - *m_origin).count();
}

qreal SnowfallRegistry::advance(KWin::LogicalOutput *output, std::chrono::milliseconds presentTime, const QList<Catcher *> &catchers)
{
    const qreal elapsed = elapsedAt(presentTime);

    const auto it = m_outputs.find(output);
    if (it == m_outputs.end()) {
        return 0.0;
    }

    OutputSnowfall &entry = it->second;
    // An output's first frame has nothing to measure from, so it only starts
    // the clock. A present time that went backwards -- which KWin will hand out
    // across a mode switch -- is the same case.
    const qreal delta = entry.lastElapsed < 0 ? 0.0 : std::clamp(elapsed - entry.lastElapsed, 0.0, maximumDelta(m_settings));
    entry.lastElapsed = elapsed;

    entry.snowfall->step(delta, elapsed, catchers);

    return delta;
}

} // namespace Snow
