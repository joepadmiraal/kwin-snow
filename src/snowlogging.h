/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QLoggingCategory>

// One category for the whole effect. tools/dev.sh turns its debug output on, so
// anything logged at qCDebug is visible in the nested loop and quiet elsewhere.
Q_DECLARE_LOGGING_CATEGORY(KWIN_EFFECT_SNOW)
