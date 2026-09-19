/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "settings.h"
#include "snowfall.h"

#include <QList>
#include <QObject>

#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>

namespace KWin
{
class LogicalOutput;
}

namespace Snow
{

/**
 * One Snowfall per output, and the clock they are all stepped from.
 *
 * The registry is the only thing here that knows about KWin: it turns outputs
 * into the plain rectangles a Snowfall works in and keeps the set in step as
 * outputs come, go and are rearranged, exactly as CatcherRegistry does for the
 * ground. Snowfall itself stays compositor-free, which is what lets the motion
 * be unit-tested.
 *
 * It also owns the clock, because the clock is shared. KWin renders each output
 * on its own schedule and hands the effect a separate present time for each, so
 * a Snowfall's step size is per output -- but gusts are the weather, and the
 * weather is one thing, so every Snowfall reads them from the same elapsed time
 * since the first frame.
 */
class SnowfallRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * The Snowfalls of every output there is now, each already full at
     * @a settings' density.
     *
     * The configuration arrives here rather than being read here, and it
     * arrives before the first Snowfall is built: a Snowfall fills itself the
     * moment it exists, and one built at the default density would then have to
     * grow into the configured one in front of somebody.
     */
    explicit SnowfallRegistry(const Settings &settings, QObject *parent = nullptr);
    ~SnowfallRegistry() override;

    /** The Snowfall over @a output, or nullptr when there is no such output. */
    Snowfall *snowfallFor(KWin::LogicalOutput *output) const;

    /** Every Snowfall, in the order KWin lists its outputs. */
    QList<Snowfall *> snowfalls() const;

    /**
     * Push new settings down to every Snowfall, from
     * SnowEffect::reconfigure().
     *
     * A Snowfall takes a copy and follows it from its next step: the population
     * walks toward the new `density` rather than jumping to it, and nothing
     * else in here is state that a setting could invalidate.
     */
    void setSettings(const Settings &settings);

    /**
     * Step @a output's Snowfall on to @a presentTime -- the moment the frame
     * being prepared is expected to be shown -- landing Flakes on @a catchers
     * as it goes, and return the seconds it stepped by.
     *
     * Called once per output per frame, from SnowEffect::prePaintScreen(). The
     * step it returns is the one the snow that just landed has to settle by,
     * which is why the clock is handed back out rather than kept: the Snowfalls
     * are per output and so is the frame, but the Snowlines belong to Catchers
     * and are settled by whoever owns those.
     */
    qreal advance(KWin::LogicalOutput *output, std::chrono::milliseconds presentTime, const QList<Catcher *> &catchers);

private:
    /** A Snowfall and when it was last stepped, on the registry's clock. */
    struct OutputSnowfall {
        std::unique_ptr<Snowfall> snowfall;
        /** Seconds since the first frame, or -1 before this output's first. */
        qreal lastElapsed = -1;
    };

    void addOutput(KWin::LogicalOutput *output);
    void removeOutput(KWin::LogicalOutput *output);
    void outputGeometryChanged(KWin::LogicalOutput *output);

    /** Seconds since the first frame the registry ever saw. */
    qreal elapsedAt(std::chrono::milliseconds presentTime);

    std::unordered_map<KWin::LogicalOutput *, OutputSnowfall> m_outputs;
    Settings m_settings;
    std::optional<std::chrono::milliseconds> m_origin;
};

} // namespace Snow
