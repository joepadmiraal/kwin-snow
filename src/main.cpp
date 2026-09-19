/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "snoweffect.h"

namespace Snow
{

// Disabled by default: EnabledByDefault is false in metadata.json, which KWin
// checks before it ever consults the factory.
KWIN_EFFECT_FACTORY(SnowEffect, "metadata.json")

} // namespace Snow

#include "main.moc"
