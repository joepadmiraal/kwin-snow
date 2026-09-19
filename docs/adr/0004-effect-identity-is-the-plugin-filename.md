# The effect's identity is its plugin filename, so the plugin is `snow.so`

The spec called for plugin id `kwin_effect_snow` alongside
`X-KDE-PluginInfo-Name: snow`, which is how a KDE4-era effect was named: a
`kwin_effect_*` library plus a separate short internal name. KF6 collapsed those
into one identifier. `KPluginMetaData::pluginId()` is derived from the basename
of the `.so`, `X-KDE-PluginInfo-Name` is no longer read by anything, and a
`KPlugin.Id` field that disagrees with the filename is ignored with a warning on
every scan. The two requirements cannot both hold.

We resolve it in favour of the effect being named **`snow`**: the plugin
installs as `snow.so`, so its plugin id is `snow`. This matches KWin's own
effects, which ship as `blur.so`, `cube.so`, `glide.so` — there is no
`kwin_effect_` prefix on a KWin 6 effect binary.

## Consequences

The name `snow` is what every other part of the system keys on, and renaming the
file later would silently orphan all of it:

- `kwinrc`, as `[Plugins] snowEnabled`
- the D-Bus calls `loadEffect snow` and `unloadEffect snow`
- the effect's own configuration group, `[Effect-snow]`

The KCM config plugin is unaffected and stays `kwin_snow_config.so`: an effect
finds its config module through the `X-KDE-ConfigModule` key in its metadata,
not by name convention, so the two are free to differ.

`KPlugin.Id` is deliberately absent from `metadata.json` — present and matching
it is merely redundant, present and differing is a warning.
`X-KDE-PluginInfo-Name: snow` is kept as a record of the same name, with the
understanding that nothing reads it.
