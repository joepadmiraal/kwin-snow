# KCM config plugin

Status: ready-for-agent
Blocked by: 09

Build `kwin_snow_config.so`, installed alongside the other effect config plugins
in `qt6/plugins/kwin/effects/configs/`, opened from the gear icon next to Snow
in System Settings → Desktop Effects.

Two tabs, splitting "what it looks like" from "how it behaves":

- **Basic**: snow on windows, snow on panels, snow on desktop, flake style, cap
  style, density.
- **Advanced**: fall speed, wind strength, max depth, melt rate, frame rate cap.

The gear icon only appears once the effect's `metadata.json` gains
`X-KDE-ConfigModule: kwin_snow_config`. That key is what links the two, so the
KCM keeps its `kwin_snow_config` name even though the effect itself is named
`snow`; see
[ADR-0004](../../../docs/adr/0004-effect-identity-is-the-plugin-filename.md).

The corner inset ramp is not a setting and must not appear here.

Label `meltRate` so that 0 reads clearly as permanent accumulation.
## Comments

Implemented as `src/kcm/configmodule.{h,cpp}` with `src/kcm/metadata.json` and
`src/kcm/CMakeLists.txt`, plus the one key in `src/metadata.json`
(`X-KDE-ConfigModule: kwin_snow_config`) that makes the gear appear at all.
`tests/kcmtest.cpp` is new. The top-level `CMakeLists.txt` gained three KF6
components and the `kwin/effects/configs` install directory; `src/CMakeLists.txt`
gained the subdirectory. `docs/development.md` and `tools/install-live.sh` say
there are now two plugins.

No new glossary terms, and nothing in `CONTEXT.md` changed: a dialog is not part
of the model. The user-facing wording deliberately does *not* use the glossary —
"Pile edge" rather than "Cap Style", "Snow settles on" rather than "Solid Snow
Catcher classes" — because the glossary is the language this codebase reasons in
and not the language somebody clicking a checkbox has ever met.

### It holds no state, and that is the whole design

Every widget is named `kcfg_<key>` and `KCModule::addConfig()` binds the lot to
the generated `SnowConfig` in one call. That is what Apply, Defaults, the
modified marker and the per-row default indicators all are — there is no `load()`
and no `defaults()` here, and `save()` exists only to add the D-Bus call.

The ranges come from the same schema, read back off the item
(`applySchemaRange()`), so 1–10, 0–10, 1–200 px, 0–100 px/s and 1–240 fps are
written down once. Ticket 09 left them in the schema for exactly this.

### The two names that nothing checks, and now a test does

Two things in this module are strings that have to match something else, with no
compiler and no tool between them:

- **the object names.** `kcfg_meltRate` is what makes that spin box the
  `meltRate` key. A misspelling gives a widget that moves and does nothing,
  which looks like it worked.
- **the combo rows.** `KConfigDialogManager` stores a non-editable combo box by
  its *index*, so a row inserted in the wrong place does not fail — it silently
  makes "Turning crystals" mean `depth`.

`tests/kcmtest.cpp` holds both against `snowconfig.kcfg` read as a file, which is
the same reading `settingstest` does and for the same reason. It loads the
plugin **as built** rather than linking the class, because the object names are
only worth checking in the artefact System Settings will load. Each combo row
carries its schema choice name as item data — which is also why the choice is
written beside its label in the source rather than in a trailing comment.

Checked by breaking it on purpose: `snowOnPanels` → `snowOnPanel` and the two
`capStyle` rows swapped both fail, naming the key and the index.

### `X-KDE-ConfigModule` is the whole of the link, and it is a bare name

KWin's `EffectsModel::requestConfigure()` takes the effect's
`X-KDE-ConfigModule` value, prepends `kwin/effects/configs/`, and hands the
result to `KCMultiDialog::addModule()` as a `KPluginMetaData` path — so the
value is a plugin id resolved in a sibling namespace under the Qt plugin path,
not a file path. The prefix goes on unconditionally (no `startsWith` guard in
the disassembly of 6.6.6), so a bare `kwin_snow_config` is the only form that
works from a binary effect — which is also what every binary effect in KWin
ships. Some KWin *scripts* carry the full path in the same key, which must be
reaching the dialog by some other route; nothing here depends on that.

That is one directory the project did not install to before, so
`SNOW_CONFIG_INSTALL_DIR` sits beside `SNOW_PLUGIN_INSTALL_DIR` and the same
`QT_PLUGIN_PATH` covers both.

### Three smaller decisions

- **`K_PLUGIN_FACTORY_WITH_JSON`, not `K_PLUGIN_CLASS_WITH_JSON`.** The short
  macro derives the factory name by pasting `Factory` onto the class name, and
  `Snow::ConfigModule` does not survive the paste. KWin's own effect configs get
  away with `K_PLUGIN_CLASS` because `kcoreaddons_add_plugin()` defines the name
  for them — and that macro also decides where the plugin installs, which is the
  one thing this project keeps in its own hands.
- **`TRANSLATION_DOMAIN` as a compile definition, not
  `KLocalizedString::setApplicationDomain()`.** The module is loaded into System
  Settings' process, and setting the application domain there would take over
  System Settings' own. Nothing translates the effect, so every string comes back
  as written; the point is that it comes back from the right catalogue if it ever
  does not.
- **The schema is generated twice**, into `src/` and `src/kcm/`. The two plugins
  are loaded by two different processes, so what they share is the schema file;
  sharing the generated object would be sharing nothing and costing a library.

### `meltRate` 0 stops being a number

The bottom of the range is not "melts very slowly", it is a different rule — and
"0 px/s" says none of that. At the minimum the spin box shows
**"Never: snow keeps piling up"** instead, via `setSpecialValueText()`. The box
is already sized for that string at every other value, so it does not jump.

### Not in it

The corner inset ramp, as this ticket says, with a comment in `buildBasicTab()`
saying so — it approximates a decoration's rounded corners rather than being a
choice anyone would make. Nor the idle timeout.

### Verified in the nested compositor

`tools/dev.sh --app <script> -- --no-lockscreen` with `plasmashell` inside, and
the module driven from outside over the nested session's D-Bus — loaded from the
plugin as installed, so what ran is what System Settings will load.

| Check | Result |
| --- | --- |
| The gear | Snow appears under Appearance with the configure button *enabled*, where Sheet and Thumbnail Aside above and below it show the greyed-out one |
| `X-KDE-ConfigModule` resolves | `snow` → `kwin_snow_config` → `kwin/effects/configs/kwin_snow_config`, valid, name "Snow" — the same two steps `EffectsModel::requestConfigure()` takes |
| The dialog | Both tabs render under Breeze, and catch snow on their own title bar |
| What it opens on | Every key bound: 3 check boxes, 2 combo boxes on `depth`/`contour`, density 5, fall 5, wind 4, 20 px, 0,4 px/s, 30 fps — the spec's defaults, out of the schema |
| The ranges | 1–10, 0–10, 1–200, 0–100, 1–240, all read off the items rather than restated |
| `meltRate` 0 | The spin box reads **"Never: snow keeps piling up"**, and is already that wide at every other value, so it does not jump |
| Defaults | `representsDefaults` true after `defaults()`, with every widget back on the schema's value |
| Apply, what it writes | Only the changed keys, in `[Effect-snow]`, with the enums by name: `flakeStyle=crystal`, `capStyle=shaded` — which is what `configuredSettings()` reads |
| Apply, what it reaches | `Snow reconfigured -- ... melt 0 px/s` and then `... desktop off` in the effect's own log, one line per Apply |
| Modified marker | `needsSave` true after an edit, false after save |
| Tests | `ctest` green in Debug and Release, 7 of 7; no new warnings |

Two things the nested session was not used for: clicking Apply with a mouse
(the module was driven in-process instead, which exercises the same `save()`),
and the live session, which is unchanged from what `tools/install-live.sh`
already does apart from installing a second `.so`.

**Not done:** `Status:` left as-is — `docs/agents/triage-labels.md` still defines
no completed state.
