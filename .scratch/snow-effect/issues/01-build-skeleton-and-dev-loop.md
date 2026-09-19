# Build skeleton and development loop

Status: ready-for-agent

Stand up a KWin effect plugin that loads and does nothing else, plus the
iteration loop everything after this depends on.

- CMake project against the installed `kwin-dev` 6.6.6, using
  `/usr/lib/x86_64-linux-gnu/cmake/KWin/KWinConfig.cmake`.
- `metadata.json` declaring plugin id `kwin_effect_snow`,
  `X-KDE-PluginInfo-Name: snow`, display name "Snow", category Appearance, and
  **enabled-by-default false**.
- A minimal `Effect` subclass that logs on load and unload.
- Install target writing to
  `~/.local/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/plugins/`.
- A dev script that launches a nested `kwin_wayland` with the effect enabled, so
  a crash kills only the nested compositor. Document the live-session
  verification path (install the `.so`, toggle the effect off and on) as the
  secondary loop.

Done when the effect appears in System Settings → Desktop Effects → Appearance,
toggles on and off without taking KWin down, and the nested loop turns an edit
into a running compositor in one command.

## Comments

Implemented. Built against the installed `kwin-dev` 6.6.6 via `find_package(KWin)`
and the exported `KWin::kwin` target.

**Naming.** The issue asked for plugin id `kwin_effect_snow` together with
`X-KDE-PluginInfo-Name: snow`. Under KF6 those cannot both hold: the plugin id
is derived from the `.so` basename, and a `KPlugin.Id` that disagrees is ignored
with a warning. Resolved in favour of the *effect* being named `snow` — the
plugin installs as `snow.so`, giving plugin id `snow`, `[Plugins] snowEnabled`
and, later, `[Effect-snow]`. This matches KWin's own effects (`blur.so`,
`cube.so`). `X-KDE-PluginInfo-Name: snow` is kept in the metadata as a record of
the same name, though nothing reads it any more. See `docs/development.md`.

`KDEInstallDirs`/`KDECMakeSettings` are deliberately not included: they reset
`CMAKE_INSTALL_PREFIX` to a system prefix, and this plugin only installs to one
hand-picked directory under `$HOME`.

**Verified.**

- Clean configure and build against KWin 6.6.6, no warnings.
- Installs to `~/.local/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/plugins/snow.so`.
- `KPluginMetaData` reports `id=snow`, `name=Snow`, `category=Appearance`,
  `enabledByDefault=false`, `valid=true` — which is what System Settings →
  Desktop Effects → Appearance lists from.
- Nested `kwin_wayland` loads the effect and logs on load and unload.
- Three `unloadEffect`/`loadEffect` cycles over D-Bus with the compositor still
  serving afterwards.
- `tools/dev.sh` turns an edit into a running compositor in one command;
  `--panel`, `--outputs`, `--scale`, `--app` all exercised.

**Not done:** `Status:` left as-is — `docs/agents/triage-labels.md` defines no
completed state.
