/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "settings.h"

#include "snowconfig.h"

#include <QStringList>

#include <effect/effecthandler.h>

namespace Snow
{

/*
    The one file that knows the effect's knobs are a config file. Everything
    downstream takes a Settings by value, which is why none of the rest of the
    effect -- or of the tests -- has KConfigXT anywhere in it.
*/

static FlakeStyle flakeStyleFor(int stored)
{
    switch (stored) {
    case SnowConfig::EnumFlakeStyle::blob:
        return FlakeStyle::Blob;
    case SnowConfig::EnumFlakeStyle::crystal:
        return FlakeStyle::Crystal;
    case SnowConfig::EnumFlakeStyle::depth:
        return FlakeStyle::Depth;
    }
    // Not reachable through the KCM, but a hand-written kwinrc can say
    // anything; KConfigXT keeps the index it read rather than rejecting it.
    return Settings().flakeStyle;
}

static CapStyle capStyleFor(int stored)
{
    switch (stored) {
    case SnowConfig::EnumCapStyle::shaded:
        return CapStyle::Shaded;
    case SnowConfig::EnumCapStyle::contour:
        return CapStyle::Contour;
    }
    return Settings().capStyle;
}

static QString flakeStyleName(FlakeStyle style)
{
    switch (style) {
    case FlakeStyle::Blob:
        return QStringLiteral("blob");
    case FlakeStyle::Crystal:
        return QStringLiteral("crystal");
    case FlakeStyle::Depth:
        return QStringLiteral("depth");
    }
    Q_UNREACHABLE();
}

static QString capStyleName(CapStyle style)
{
    switch (style) {
    case CapStyle::Shaded:
        return QStringLiteral("shaded");
    case CapStyle::Contour:
        return QStringLiteral("contour");
    }
    Q_UNREACHABLE();
}

Settings configuredSettings()
{
    // KWin's own kwinrc object, rather than a second one opened by name: it is
    // the object KWin reparses before it calls an effect's reconfigure(), so
    // reading from it is what makes a KCM's Apply arrive here. Handing it over
    // again is ignored with a debug line, which is the right answer for the
    // only way that happens -- the effect being switched off and on again
    // inside a compositor whose copy of this plugin never went away.
    SnowConfig::instance(KWin::effects->config());

    // load() and not read(): read() would take the values KWin's copy of
    // kwinrc happens to be holding, and there is one path where those are
    // stale. KWin reparses the file before calling reconfigure(), so a KCM's
    // Apply is fine either way -- but nothing reparses it before *loading* an
    // effect, so an effect switched on after the file changed under it would
    // come up on whatever KWin last read. Reparsing is what KWin itself does a
    // moment earlier on the reconfigure path, and it happens here twice a
    // session rather than twice a frame.
    SnowConfig::self()->load();

    Settings settings;
    settings.snowOnWindows = SnowConfig::snowOnWindows();
    settings.snowOnPanels = SnowConfig::snowOnPanels();
    settings.snowOnDesktop = SnowConfig::snowOnDesktop();
    settings.flakeStyle = flakeStyleFor(SnowConfig::flakeStyle());
    settings.capStyle = capStyleFor(SnowConfig::capStyle());
    settings.density = SnowConfig::density();
    settings.fallSpeed = SnowConfig::fallSpeed();
    settings.windStrength = SnowConfig::windStrength();
    settings.flakesInFrontOfWindows = SnowConfig::flakesInFrontOfWindows();
    settings.maxDepth = SnowConfig::maxDepth();
    settings.meltRate = SnowConfig::meltRate();
    settings.frameRateCap = SnowConfig::frameRateCap();
    return settings;
}

QString describeSettings(const Settings &settings)
{
    const auto onOff = [](bool on) {
        return on ? QStringLiteral("on") : QStringLiteral("off");
    };

    QStringList parts;
    parts.append(QStringLiteral("windows %1, panels %2, desktop %3")
                     .arg(onOff(settings.snowOnWindows), onOff(settings.snowOnPanels),
                          onOff(settings.snowOnDesktop)));
    parts.append(QStringLiteral("%1 flakes, density %2, fall %3, wind %4")
                     .arg(flakeStyleName(settings.flakeStyle))
                     .arg(settings.density)
                     .arg(settings.fallSpeed)
                     .arg(settings.windStrength));
    parts.append(QStringLiteral("blown flakes %1 windows")
                     .arg(settings.flakesInFrontOfWindows ? QStringLiteral("in front of")
                                                          : QStringLiteral("behind")));
    parts.append(QStringLiteral("%1 caps, max depth %2 px, melt %3 px/s")
                     .arg(capStyleName(settings.capStyle))
                     .arg(settings.maxDepth)
                     .arg(settings.meltRate));
    parts.append(QStringLiteral("%1 fps").arg(settings.frameRateCap));

    return parts.join(QStringLiteral("; "));
}

} // namespace Snow
