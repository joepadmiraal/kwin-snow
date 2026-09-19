/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <KCModule>

class QWidget;

namespace Snow
{

/**
 * The effect's configuration dialog: the gear beside Snow in
 * System Settings -> Desktop Effects.
 *
 * It holds no state of its own. Every widget it builds is named
 * `kcfg_<key>` after a key of `snowconfig.kcfg`, and addConfig() binds the lot
 * to the generated SnowConfig in one call -- which is what makes Apply,
 * Defaults, the "changed" marker and the per-row default indicators work
 * without a line of code each. The schema is also where the ranges come from,
 * so a spin box's bounds are not restated here.
 *
 * The plugin is `kwin_snow_config.so` although the effect is `snow.so`: an
 * effect finds its configuration module through the `X-KDE-ConfigModule` key in
 * its metadata rather than by name convention, so the two are free to differ,
 * and every other effect's module is named this way. See
 * docs/adr/0004-effect-identity-is-the-plugin-filename.md.
 */
class ConfigModule : public KCModule
{
    Q_OBJECT

public:
    explicit ConfigModule(QObject *parent, const KPluginMetaData &data);

    /**
     * Writes the keys, then tells the running compositor to re-read them.
     *
     * Without the second half a saved setting sits in kwinrc until something
     * else makes KWin reconfigure, which reads as Apply not working.
     */
    void save() override;

private:
    /** What the snow looks like. */
    QWidget *buildBasicTab(QWidget *parent);
    /** How it behaves. */
    QWidget *buildAdvancedTab(QWidget *parent);
};

} // namespace Snow
