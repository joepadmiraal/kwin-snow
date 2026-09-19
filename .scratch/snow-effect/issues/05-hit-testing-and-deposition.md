# Hit testing, deposition, relaxation and melt

Status: ready-for-agent
Blocked by: 03, 04

Connect Flakes to Snowlines. This is the core of the effect.

- Hit test each Flake **topmost-first** down the stacking order. Skip Catchers
  whose class is disabled — a disabled class is transparent, and Flakes fall
  through it (spec: Model).
- A Flake lands when it crosses a Catcher's top surface, which is the Catcher's
  top edge minus the current depth at that Column, tested against the Flake's
  previous and current Y.
- Deposit into the landing Column, spreading a smaller share into each immediate
  neighbour. When `flakeStyle` is `depth`, deposited mass scales with the
  Flake's z.
- Relaxation pass each frame: move snow from a Column to a lower neighbour when
  the difference exceeds a threshold, so piles settle to an angle of repose
  instead of spiking.
- Clamp at `maxDepth`. Excess is **discarded**, not shed onto anything below.
- Melt: decay every Column by `meltRate` per second. `meltRate` 0 means
  permanent accumulation.
- Respawn a Flake at the top once it lands.

Done when leaving it running reaches a visible steady state where melt balances
snowfall, and disabling a Catcher class makes Flakes fall through to the next
one down.

## Comments

Ticket 03 left this ready to build on:

- `Catcher::columnAt(globalX)` is the one place global X becomes the local X a
  Snowline is indexed in; hit testing should go through it rather than
  subtracting `geometry().left()` again.
- `Snowline` already has `addDepth()`, `depth()`, `maximumDepth()` and
  `columnCentre()`. Column width is `Snowline::columnWidth`, 5 logical px.
- **Delete the diagnostics scaffolding when real snow lands here.** The
  `KWIN_SNOW_SNOWLINE_TEST` flag in `catcherregistry.cpp` — test-pattern
  seeding plus the once-a-second poll that reports the Catcher set — exists
  only because nothing can fill a Snowline yet. Once deposition works there is
  real snow to watch and all of it should go.

Implemented across `src/snowline.{h,cpp}` (deposit, relaxation, melt),
`src/catcher.{h,cpp}` (`isSolid()`, `surfaceAt()`, `deposit()`, `settle()`),
`src/snowfall.{h,cpp}` (the hit test) and `src/snoweffect.cpp` (the two calls a
frame). No new domain terms: hit testing is phrased in the glossary's **Solid**,
and `Catcher::isSolid(settings)` is the one place a class being disabled is read.
`Snowline::addDepth()` is gone: `deposit()` replaces it, and nothing else used it.

### Where the pieces went, and why

**Hit testing is in `Snowfall::step()`**, which now takes the Catcher list.
Landing is the end of a Flake's life and a Flake's life is Snowfall's — the
prototype puts it in the same loop for the same reason, and the alternative
(a separate pass after stepping) needs the respawn exposed to whatever drives
it. A `Catcher` is nothing but geometry and an array, so this costs Snowfall
none of the compositor-freedom ticket 04 built it for; it is still all checkable
in a unit test. `tests/snowfalltest` now compiles `catcher.cpp` and links
`KWin::kwin` for exactly the reason `snowlinetest` already did — to resolve the
`EffectWindow`/`LogicalOutput` calls in `describe()`, with `nullptr` passed for
both.

**Relaxation and melt are driven by `CatcherRegistry::settle(output, …)`**,
because the Snowlines hang off Catchers and nothing else can reach them all. It
is per output, not global: `prePaintScreen` is called once per output per frame,
and a Catcher belongs to exactly one output, so every Snowline settles exactly
once a frame however many monitors are drawing.

**The whole Catcher set is hit-tested, not just the painting output's.** A
window straddling two outputs should catch over the whole of its top edge, and
a Flake cannot reach a Catcher on an output it never leaves.

**Settling runs after landing**, so the snow that just arrived is part of the
pile that relaxes.

### Three places this departs from the prototype, all deliberate

**Relaxation is per second, not per frame.** The prototype moves 0.16 of the
excess each frame at its 30 fps cap; that is 4.8 per second, and stating it that
way means the snow settles at the same speed whatever `frameRateCap` ends up
being (ticket 08). The per-step fraction is capped at 0.25 so a long frame after
a stall cannot drive a Column past its neighbours and set it ringing.

**Relaxation reads the array as it was at the start of the pass.** The
prototype reads it as it writes it, which lets snow cascade rightward within one
sweep but never leftward, and over a minute that walks a pile sideways. Doing it
properly costs three saved doubles, not a copy of the array:
`m_depths[i + 1]` has not been touched yet when it is read, and what it reads
becomes the next Column's untouched middle. Both end Columns can also shed now,
where the prototype's loop skips them and lets an edge spike stand.

**A Flake that ends up inside the snow settles there**, not only one whose
`previousY..y` segment crossed the surface. Measured: the segment test alone
leaks about one Flake in ten straight through a growing pile. Two ways it
happens — another Flake landing in the same Column raises the surface by 0.62 px,
which is more than the gap a Flake at the end of a frame typically has left; and
a gust carries a Flake sideways into a Column whose pile has already grown past
it. Either way the Flake is between the surface and the top edge holding it up,
which is not somewhere a Flake can be. With this in, a Solid Catcher passes
0.2% of what lands on it rather than 8%.

### Tuning findings for ticket 11

**At the spec's defaults, accumulation is a dusting, not a pile.** Melt at
0.4 px/s comfortably outpaces snowfall on a 1600x1000 desktop: measured steady
state on the ground is a mean depth of **0.16–0.21 px** (peaks under 2 px),
where `maxDepth` is 20. With `meltRate` 0 the same ground fills to exactly
20 px in about 140 s and stays there, so the machinery works — the defaults are
simply well to the melt side of visible.

**Accumulation rate depends on where a Catcher is, not only on how wide it is.**
A landed Flake respawns immediately at the top, so a Catcher high up the output
shortens the recycle loop for the Flakes that hit it and receives snow faster. A
Catcher spanning the full output width at y=300 reaches a genuine melt-balanced
steady state at a mean of ~2.1 px; a 400 px window on a 1600 px output at the
same height gets a quarter of that, because most Flakes still travel the whole
height to the ground. Worth knowing before anyone retunes `meltRate`.

**There is no equilibrium depth in between.** Accumulation is very nearly
independent of how deep the pile already is, so a Catcher either melts to bare
or fills to `maxDepth` and clamps. "Melt balances snowfall" is real, but it
balances at one of the two ends unless the rates are close.

### Tests

`tests/snowlinetest.cpp` gains thirteen cases — the deposit shares and what
falls off an edge, the depth cap discarding rather than banking the excess,
relaxation spreading a spike, conserving snow, leaving a settled slope alone,
having no preferred direction, scaling with the frame time and never
overshooting, melt down to bare and `meltRate` 0 being permanent, the surface
rising with its snow, and solidity following the class.

`tests/snowfalltest.cpp` gains nine — Flakes reaching every Column of a Catcher,
landing not using up the snowfall, a Flake crossing a surface in one 19 px step
still landing, a disabled class being transparent, the topmost Catcher winning,
the `depth` style building a pile more slowly, filling to `maxDepth` and
stopping, a melt-balanced steady state holding over a second minute, and snow
melting away once it stops falling.

`ctest --test-dir build` passes in Debug and Release, with no new warnings.

### Verified in the nested compositor

`tools/dev.sh` on a Debug build, 110 s, with a script opening and closing a
window every six seconds — each add and close makes the effect log the whole
Catcher set with each Snowline's sum, which is now real snow rather than a test
pattern. See `docs/development.md`.

| Check | Result |
| --- | --- |
| Snow arrives | Ground `sum 0.0` → `15.6` → `60.2` in the first 15 s |
| Steady state | Ground held `44.8`–`64.2` for the remaining 95 s, 19 samples |
| Matches the unit tests | `sum 52` over 320 Columns is a mean of 0.16 px |
| A window catches | Konsole's 80-Column Snowline held `16.7`–`27.5` throughout |
| Snowlines travel and survive | Column counts tracked each window's width, contents never reset |
| A disabled class | Rebuilt with `snowOnWindows = false`: every window Snowline stayed `sum 0.0` for the whole run while the ground held its steady state |
| Stability | No crash, no assertion failure, no new log noise |

### Scaffolding deleted, as the ticket asked

`KWIN_SNOW_SNOWLINE_TEST`, `Snowline::seedTestPattern()`, the once-a-second
Catcher poll and `CatcherRegistry::makeCatcher()` are all gone, along with the
`m_lastReport` the poll needed. The event-driven `logCatchers()` stays and is
now the way to watch real snow: every line already carried the Snowline's
Column count and the sum of its depths.

`KWIN_SNOW_FLAKE_TEST` in `snowfallregistry.cpp` stays — nothing draws a Flake
until ticket 06, which is what that one is waiting for.

### Consequences worth knowing

- **`Settings` grew five keys** — `snowOnWindows`, `snowOnPanels`,
  `snowOnDesktop`, `maxDepth` and `meltRate` — because the simulation now reads
  them. They are the spec's defaults and there is still no way to change them at
  runtime; ticket 09 backs them with KConfigXT. Disabling a class already stops
  it catching without clearing its Snowline, which is the behaviour ticket 09
  turns into an accelerated melt.
- **A pile relaxes sideways past the edge of what is catching it.** Where one
  window covers another, the back window's snow goes on spreading under the
  front one — correctly, since the top edge it is standing on carries on under
  there. Ticket 07 will draw it, hidden behind the front window.
- **`prePaintScreen` now builds the Catcher list once per output per frame**,
  which walks KWin's stacking order. Fine at this scale and untouched by ticket
  08's frame cap, but it is the obvious thing to cache if anything needs it.

**Not done:** `Status:` left as-is — `docs/agents/triage-labels.md` still defines
no completed state.
