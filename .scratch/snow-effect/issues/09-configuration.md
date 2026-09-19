# Configuration and live reconfiguration

Status: ready-for-agent
Blocked by: 05, 06, 07, 08

Back every knob with KConfigXT and handle `reconfigure()`.

The config group is `[Effect-snow]` — the effect's name is `snow`, taken from
its plugin filename; see
[ADR-0004](../../../docs/adr/0004-effect-identity-is-the-plugin-filename.md).

Keys and defaults are the two tables in the spec. `maxDepth` and `meltRate` are
in **logical** pixels.

Live reconfiguration melts, it never snaps (spec: Behaviour):

- Disabling a Catcher class sets its Snowlines melting at an accelerated rate
  rather than clearing them.
- Lowering `maxDepth` lets melt bring existing depths down rather than
  truncating them.
- Changing `density` stops spawning or spawns faster, rather than adding or
  removing Flakes instantly.

The KCM is where someone is watching closely, so an instant state change there
reads as a bug.
## Comments

Implemented across `src/snowconfig.kcfg` and `src/snowconfig.kcfgc` (the
schema), `src/settings.{h,cpp}` (the struct, the reader, and the accelerated
melt rate), `src/snoweffect.{h,cpp}` (`reconfigure()`, and the effect now owning
the `Settings` everything else is handed), `src/snowline.cpp` (a lowered
`maxDepth` is no longer a truncation), `src/catcher.cpp` (a switched-off class
melts), `src/snowfall.{h,cpp}` (a changed `density` is approached rather than
jumped to), `src/cap.{h,cpp}` and `src/cappainter.cpp` (a Cap is drawn against
the snow standing on it) and `src/snowfallregistry.{h,cpp}` (the configuration
arrives before the first Snowfall). `tests/settingstest.cpp` is new, five cases;
`snowlinetest`, `snowfalltest` and `captest` gained five more between them, and
three existing cases changed because the behaviour they described is what this
ticket replaced.

No new glossary terms: a setting is not part of the model, so CONTEXT.md is
untouched.

### The schema and the struct both hold the defaults, and a test holds them together

`Snow::Settings` stays what the simulation reads: a plain copyable value with no
KConfig and no KWin in it, which is what lets the whole model be handed a
configuration in a unit test. `settings.cpp` is the only file that knows the
knobs live in a file at all, and it is not linked into any test.

That leaves the spec's defaults written out twice -- in the struct and in the
`.kcfg` -- because the schema is what a KCM's "Defaults" button restores and the
struct is what everything else reads, and nothing in the toolchain ties the two
together. `tests/settingstest.cpp` reads the schema as a file and checks it
against the struct key by key, along with the group name, the ranges and the
enum spellings. It is also the only place the spec's two Configuration tables
are written down as a list.

### `load()`, not `read()` -- found by the nested session

Every KWin effect reads its config with `XConfig::self()->read()`, which uses
KWin's in-memory copy of kwinrc, and KWin reparses that copy before calling
`reconfigure()`. So `read()` is right on the path a KCM takes.

Nothing reparses it before *loading* an effect, though, and switching an effect
on is exactly when its configuration is most likely to have just changed. The
nested session showed it: write `density=10`, `unloadEffect snow`,
`loadEffect snow`, and the effect came up at density 5 -- the value KWin had
parsed at session start. `load()` goes back to the file and the same test then
came up at 950 Flakes. It costs one ini reparse twice a session, and it is the
same reparse KWin does a moment earlier on the other path.

### Three changes melt, and each one lives with the thing it changes

`reconfigure()` is deliberately thin -- read, push down, re-pace the frame clock
-- because none of the three rules is really about configuration. Each is a rule
about the thing it changes, and lives there:

- **A switched-off Catcher class melts at 8 px/s** (`meltRateFor()` in
  settings.h, applied in `Catcher::settle()`), which clears a Cap at the default
  `maxDepth` in under three seconds. Never slower than the configured rate, so a
  configuration that already melts faster is left alone. The Cap is *drawn*
  while that happens: `SnowEffect::cappedCatcher()` no longer asks whether the
  class is Solid, because a class that has just been switched off still has snow
  on it and that snow is going rather than gone.
- **A lowered `maxDepth` is melted down to, not cut to** (`Snowline::deposit()`).
  The clamp used to be `min(cap, standing + mass)`, which quietly truncated a
  Column that was already over the cap -- and only where snow happened to be
  falling, so a lowered cap took the piles down in patches. A Column at or over
  the cap now takes nothing and loses nothing.
- **A changed `density` spawns faster or stops spawning** (`Snowfall::step()`
  and `Snowfall::spawn()`). Growth is capped at 0.35 of the target a second and
  every new Flake enters above the top edge; shrinking is not done by removing
  Flakes at all, but by not putting back the ones that land or fall past the
  bottom. A population therefore comes down over the time it takes snow to fall,
  which is a few seconds, and nothing ever appears or disappears in mid-air.

The same three rules apply to an output being resized, which also moves the
Flake target -- one rule rather than a special case.

**Worth stating: `meltRate` 0 means a lowered `maxDepth` never arrives.** Melt is
what brings existing depths down, so permanent accumulation keeps the deeper
snow permanently. That is the spec's own definition of 0 doing what it says, and
the new cap still holds everything that lands from then on.

### A Cap is drawn against the snow standing on it, not against the cap

Everything about a Cap's *drawing* was scaled to `maxDepth`: the strip
`prePaintWindow()` adds to what will be painted, and the shading profile's top
and span. Lowering `maxDepth` leaves Caps deeper than it for as long as the melt
takes, and scaling those to the new cap would have cut the top off the band and
flattened the shading of exactly the Caps whose melting is the thing being
watched. `capReferenceDepth()` is the deeper of the two, and is what both now
use.

### `frameRateCap` is the one knob that is not read once a frame

Everything else is read out of `Settings` at the point it is used, so pushing a
new struct down is the whole of the change. The frame rate lives in the
`FramePacer`, so `reconfigure()` calls `updateFrameClock()` -- which is
`suspensionChanged()` renamed, since it is now a function of two things rather
than one: the Suspension, and the cap.

### Verified in the nested compositor

`tools/dev.sh --app <script> -- --no-lockscreen` with `plasmashell` inside,
driven from outside over the nested session's D-Bus the way a KCM drives it:
`kwriteconfig6` into the session's kwinrc, then
`reconfigureEffect snow`. The numbers below come from a temporary `qCInfo` in
`prePaintScreen` -- frames a second, Flake count against target, and each
Snowline's sum and deepest Column -- that has since been deleted.
docs/development.md now describes the whole recipe under *Changing settings*.

| Check | Result |
| --- | --- |
| Load with no `[Effect-snow]` group | `Snow configured -- ... density 5 ... melt 0.4 px/s; 30 fps`: the spec's defaults, from the schema |
| `meltRate=0` and reconfigure | `Snow reconfigured -- ... melt 0 px/s`, and the piles stopped melting |
| Load with `density=10` written first | 950 Flakes in the Snowfall's first line, already scattered -- not 475 growing into 950 |
| `maxDepth` 20 → 5 at `meltRate=2` | deepest Column 12.11 → 10.11 → 8.07 → 6.03 over four seconds: 2 px/s, the configured rate, rather than a jump to 5 |
| `snowOnDesktop=false` at `meltRate=0` | ground 16.50 → 10.36 → 2.21 → 0 over two seconds, while the Panel's Snowline went on *growing* through the same frames: the melt is per class and accelerated, not a clear |
| The same, in screenshots | the ground's Cap drawn as a band before, drawn and visibly shallower a moment after, gone three seconds later |
| `density` 5 → 1 | 475 → 440 → 359 → 294 → 219 → 153 → 99 → 95 over six seconds, by attrition |
| `density` 1 → 10 | 95 → 305 → 656 → 950 over three seconds, every new Flake entering above the top edge |
| `frameRateCap` 30 → 5 | ~31 fps → ~8 fps on the next line, and back on restoring it. 8 rather than 5 because plasmashell repaints for its own reasons, which is the same reading ticket 08 recorded |
| `capStyle=10`, a nonsense value | fell back to the default rather than drawing nothing |
| Stability | no crash, no assertion, no new warnings; `ctest` green in Debug and Release |

Two things the nested session could not show: what an uncapped effect costs
(unchanged from ticket 08 -- the nested compositor tops out below the cap), and
the KCM itself, which is ticket 10.

### For ticket 10

- **The schema is `src/snowconfig.kcfg` and the KCM shares it.** It generates
  `Snow::SnowConfig` with mutators, so a `KCModule` can bind widgets to it by
  `kcfg_<key>` -- `kcfg_snowOnWindows`, `kcfg_flakeStyle` and so on, spelled
  exactly as the keys are.
- **`metadata.json` still has no `X-KDE-ConfigModule`,** which is what makes the
  gear icon appear; adding it before the KCM exists would give a gear that opens
  nothing.
- **The `.kcfg` carries no `<label>` or `<whatsthis>`.** Setting them needs
  `SetUserTexts=true` and an i18n domain, and the KCM is where the wording
  belongs.
- **Ranges are in the schema**, so a spin box can read its bounds from the item
  rather than restating them: 1–10, 0–10, 1–200 px, 0–100 px/s, 1–240 fps.
- **The corner inset ramp is still not a setting**, as this ticket's sibling
  says, and neither is the idle timeout.

**Not done:** `Status:` left as-is -- `docs/agents/triage-labels.md` still
defines no completed state.
