# Flake simulation

Status: ready-for-agent
Blocked by: 01

Simulate the falling Flakes, independent of what they land on.

- Per-output simulation. Flakes spawn above each output's top edge and never
  drift between outputs.
- Population from `density` (1–10) as flakes per unit of screen area, so visual
  density holds across resolutions.
- Per-Flake state: position, previous Y (needed for the surface-crossing test in
  issue 05), z, radius, sway phase and frequency, wind response, rotation.
- Fall speed from `fallSpeed` (1–10). When `flakeStyle` is `depth`, speed also
  scales with the per-Flake z.
- Wind: noise-driven gusts over time with per-Flake response, plus a per-Flake
  sway. Not a constant sideways drift.
- Flakes leaving the side of an output wrap; Flakes passing the bottom respawn.

Verifiable before any rendering exists by dumping Flake positions over time; the
published prototype is the reference for how the motion should feel.

## Comments

Implemented as `src/flake.h` (the per-Flake state), `src/settings.h` (the knobs
the simulation reads), `src/snowfall.{h,cpp}` (the falling snow of one output)
and `src/snowfallregistry.{h,cpp}` (one Snowfall per output, and the clock).
**Snowfall** is a new term and is now in `CONTEXT.md`.

**Every tuning constant is the prototype's**, restated in the units the effect
works in — logical pixels and seconds — and named. Where one has no better
justification than "this looked right", the comment says so: the prototype is
the reference for how the motion should feel, and inventing different numbers
would only drift away from it.

### Shape

**`Snowfall` has no KWin in it at all.** It is a `QRectF`, a seed and some
arithmetic; `SnowfallRegistry` is the only part that knows what an output is,
and it turns outputs into rectangles. That is what makes "dump Flake positions
over time", which the ticket asks for, something a unit test can do directly
rather than something needing a compositor and a pair of eyes. It mirrors
`Catcher`/`CatcherRegistry`, which is already the split in this codebase.

**There are two clocks, and the second one is the interesting one.** KWin
renders each output on its own schedule and calls `prePaintScreen` once per
output per frame with that output's own present time, so the *step size* is per
output. But gusts are the weather, and the weather is one thing: a gust crossing
a two-monitor desktop has to arrive at both. So `step()` also takes the absolute
seconds since the first frame, and `gustAt()` is a static function of nothing
else — every output and every Flake asks the same question and differs only in
how much of the answer it picks up (`windResponse`).

**Population is an area rate, never a count.** The prototype's 95 Flakes per
step of `density` over its 1600x1000 canvas is 59.375 per megapixel per step,
which is what holds visual density at 1080p and 4K alike (spec: Configuration).
It is retargeted at the top of every step, so ticket 09 has somewhere to put the
ramp it needs without touching anything else.

**`windStrength` 0 stills the gusts, not the Flakes.** Each Flake keeps its own
sway, which is independent of the setting — matching the prototype, and the spec
listing the sway separately from the gusts. Snow still wanders on a still day.

**A respawned Flake gets `previousY = y`.** Ticket 05 tests the segment
`previousY..y` against a Catcher's top surface; a respawn that left `previousY`
at the bottom of the screen would hand it a segment the length of the output and
land the Flake on the first Catcher underneath.

### What this does to the effect as a whole

`isActive()` now returns true and `postPaintScreen()` calls `addRepaintFull()`.
It has to: falling snow is not damage anything else reports, so without it the
simulation stops dead whenever the desktop is still. The consequence is that
**switching the effect on now drives the compositor at its full frame rate**
while drawing nothing at all. That is the cost the spec expects the frame rate
cap to be the control on, and ticket 08 is where the cap goes in.

`Settings` holds only the four keys the simulation reads. The rest of the two
tables in the spec arrive with their consumers, and ticket 09 backs the whole
struct with KConfigXT; `SnowfallRegistry::setSettings()` already fans a change
out to every Snowfall, so the live-reconfiguration path exists and is unused.

### Tests

`tests/snowfalltest.cpp`, fifteen QTest cases, run with `ctest --test-dir build`.
Unlike `snowlinetest` it links **no KWin at all**, which is the point. They cover
population per unit area and per density step, the scattered initial fill, the
per-Flake variation, fall speed against the setting, `depth` varying it by z,
`previousY`, gusts averaging out over two minutes rather than drifting, sway
surviving `windStrength` 0, wrapping, respawning, an output resize retargeting,
and the same seed giving the same snow.

What they cannot check is whether the motion *feels* right. That needs the
published prototype side by side, and it needs something to draw the Flakes —
ticket 06.

### Verified in the nested compositor

`KWIN_SNOW_FLAKE_TEST=1` dumps count, gust and a few Flake positions once a
second per output; see `docs/development.md`. Driven with `tools/dev.sh` and
`kscreen-doctor` on a debug build.

| Check | Result |
| --- | --- |
| Density as an area rate | `475 flakes` on 1600x1000, `1900` on 3200x2000 — 4x the area, 4x the snow |
| Logical, not device, pixels | `475 flakes` at both `--scale 1` and `--scale 2` |
| Two outputs | Two Snowfalls, 475 each, different snow down each |
| No cross-output drift | WL-0's Flakes stayed in x 0–1600, WL-1's in 1600–3200, throughout |
| One weather | Both outputs reported the same gust at the same `t`, every second |
| Fall, and parallax | y climbing steadily; a z 0.09 Flake at ~100 px/s against a z 0.92 one at ~250 |
| Gusts, not drift | gust ran +0.42 → −0.90 → back over 20 s, and x followed it |
| Wrapping | `x −17.5` on one line, `x 1704.8` on the next, population unchanged |
| Respawn at the bottom | `y 994 → 1.8` with a fresh z, count still 475 |
| Output unplugged | `Snowfall gone with output WL-1`, no crash |
| Output plugged back in | New Snowfall, full population |
| Output moved (WL-1 to 0,1000) | Flakes followed: x wrapped into 0–1600, y into 1000–2000 |
| Three `unloadEffect`/`loadEffect` cycles | Compositor still serving, fresh Snowfall each time |

### Not verified

**Two outputs at genuinely different scale factors**, again — `kscreen-doctor
output.WL-0.scale.2` is accepted and ignored by the nested backend, the same
wall ticket 03 hit. What stands in for it: a Snowfall has no scale input at all,
and the population was identical at `--scale 1` and `--scale 2`, where device
pixels would have quartered it.

**How it looks.** Nothing draws a Flake yet, so everything above is positions in
a log. Ticket 06 is the first time anyone can say whether it reads as snow.

### Consequences worth knowing

- **Flakes currently fall through everything**, including Snow Catchers, and off
  the bottom of the output. Ticket 05 is what stops them; until then the ground
  Catcher is passed straight through.
- **A density increase spawns the new Flakes above the top edge**, so they
  stream in rather than appearing mid-screen. That is deliberate, but it is not
  yet the melt-toward-the-new-state ramp ticket 09 asks for — the population
  jumps to its new target on the next step.
- **An output going away takes its Flakes with it.** They have nowhere to go:
  Flakes do not cross outputs, which is the rule the per-output simulation
  exists for.
- **The diagnostics dump is scaffolding**, like the Snowline one before it.
  Ticket 06 puts Flakes on the screen and both can go.

### One unrelated fix

`src/catcher.cpp` did not compile in a Release build: a release `Q_ASSERT` still
compiles its condition, and ticket 03's `setGeometry` assertion names two locals
that are `#ifndef QT_NO_DEBUG`-ed out. Nothing had caught it because `dev.sh`
builds Debug. The assertion is now inside the same guard as the snapshot it
reads, and a clean Release configure, build and `ctest` pass.

**Not done:** `Status:` left as-is — `docs/agents/triage-labels.md` still defines
no completed state.
