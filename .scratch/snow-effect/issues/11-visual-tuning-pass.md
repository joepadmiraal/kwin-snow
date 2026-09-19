# Visual tuning pass

Status: ready-for-human
Blocked by: 06, 07, 10

Run the finished effect on the live session and compare it against the canvas
prototype at <https://claude.ai/artifact/DkmdkzpmLB2ar7PuNv37iX>, which the
shipped defaults were chosen in.

Things the prototype cannot predict and that need eyes on the real thing:

- Whether the corner inset ramp actually hides Breeze's rounded decoration
  corners, or whether the Cap still pokes through. If it does, the fallback is
  querying the KDecoration3 decoration for its real radius, currently listed as
  out of scope.
- How the Cap reads against a floating Panel's gap.
- Whether `maxDepth` 20 still looks right at the real display scale.
- Whether `crystal` Flakes hold up at the real Flake sizes, or turn to noise.
- Whether 30 fps is enough for the motion to read as smooth.

Adjust defaults in the KCFG if any of these land differently than the prototype
suggested.

## Comments

One more thing for the eyes-on list, from ticket 03:

- **A left-edge resize slides the pile.** A Snowline is anchored at Column 0,
  so dragging a window's *left* border carries the snow along with the border
  rather than leaving it over the same screen X. ADR-0001 chose this over
  rescaling, which smears; whether the slide reads worse than the smear is a
  judgement that needs the real thing in front of you.

Two more from ticket 07, now that the Caps are drawn and there is something to
look at. They are one decision, not two: the second is invisible until the first
goes a certain way.

- **`meltRate` 0.4 is well to the melt side of visible, and the desktop shows no
  snow at all.** Measured steady state is a mean of 0.16–0.21 px on the ground
  against a `maxDepth` of 20, which draws as a window's three-pixel underlap and
  its bright edge and nothing more — a light rim along a titlebar rather than a
  pile. The ground never even gets there: 0.12 px mean, under the 0.15 px a Cap
  is drawn at, so at the shipped defaults nothing lies on the desktop.

  **This is faithful, not a bug.** Every parameter that decides accumulation
  against melt was compared with the prototype and they are identical — 475
  Flakes on 1600x1000, fall speed `40 + 5*34`, `0.45 + 0.9z` on both the speed
  and the landing mass of the `depth` style, 5 px Columns, melt 0.4. Only
  `maxDepth` differs (prototype 14, spec 20), and that sets where a pile clamps,
  not whether one forms. So the published reference is melt-dominated in exactly
  the same way, and the question is only whether that is the effect wanted.

- **An auto-hidden Panel goes on catching**, because its class is still Solid.
  Flakes stop on a surface nobody can see and the ground below it gets nothing.
  That is the most literal reading of "Snowline preserved across hide and show"
  and no snow is lost — it is all there when the Panel comes back — but it is a
  choice, and the alternative is making a hidden Panel transparent so Flakes
  fall past it to the ground.

  **Decide this after the melt rate, in the same sitting.** Today it cannot be
  seen: the ground does not accumulate enough to draw. Lower `meltRate` far
  enough that the desktop shows snow and it becomes a bare strip along the
  bottom edge whenever the Panel hides — so deciding it before the melt rate is
  deciding it blind.

## Findings, 2026-09-17

Run nested (`tools/dev.sh --panel`, 1600x1000 logical at the host's scale 2,
which is the same logical size as the live display) with screenshots taken from
inside the session, and with the accumulation arithmetic measured off the real
`Snowfall`/`Catcher`/`Snowline` in a throwaway harness rather than estimated.

Five of the seven items land where the prototype suggested and need no change:

- **The corner inset ramp is not being asked to hide anything.** Breeze at its
  defaults on KWin 6.6.6 draws *square* window corners — checked at native
  resolution on both top corners of a decorated Konsole — so nothing pokes
  through and the KDecoration3 fallback stays out of scope. The ramp still earns
  its place: at 4 Columns it reads as snow thinning off the ends rather than as
  a slab cut square, which is what it looks like in the shots either way.
- **The Cap against a floating Panel's gap reads correctly.** Two parallel
  contours — one on the Panel's own top edge, one on the ground showing through
  the gap below it — and they read as snow on the Panel and snow behind it
  rather than as a rendering fault.
- **`maxDepth` 20 is right at the real display scale.** A full Cap stands about
  three quarters of a 27 px titlebar. Proportionate, and clearly a pile.
- **`crystal` Flakes hold up**, and better than the prototype's warning
  suggested: at 2x they are crisp six-point sprites, not noise. The caveat is
  the other direction — the arms are 0.7 logical px wide at the middle Flake
  size, so on a 1x display they wash out toward blobs. It is not the default and
  nothing needs to change. What *is* visible is that `crystal` has no z, so the
  field is uniform in size and opacity and reads flatter than `depth`.
- **30 fps is enough.** Not measurable nested (the compositor tops out around
  24 fps whatever it is asked for), but the arithmetic settles it: a near Flake
  falls 283 logical px/s and is drawn 17-32 px across, so it advances 0.29-0.55
  of its own width per frame; a far one, 3.15 px against a 6-11 px sprite, comes
  out the same. A third to a half of a sprite width per frame reads as motion,
  not as steps. The live-session power cost is still unmeasured here.

And the left-edge resize from ticket 03:

- **The slide is a non-issue.** A window grown 200 px leftward carries its pile
  with the border, and the before/after shots are indistinguishable as pictures
  of snow — because `capContourNoise()` is a function of the Column index, which
  is anchored at the left edge too, so the bumps travel with the depths and
  nothing goes out of step. ADR-0001 stands, with nothing owed to it visually.

### The melt rate: `meltRate` 0.4 -> 0.05

The ticket was right that nothing lies anywhere, and the reason is sharper than
"melt-dominated". **Melt is a threshold, not a balance.** Snow arrives at a rate
that does not depend on how deep a Column already is, and melt takes away at a
rate that does not either, so there is no value that holds a pile part-way up:
below the arrival rate a Column climbs to `maxDepth`, above it the Column stays
bare. The spec's "steady state where melt balances snowfall" describes something
the model cannot do.

Measured off the simulation at `fallSpeed` 5, logical px/s per Column:

| `density` | ground | window |
| --- | --- | --- |
| 1 | 0.036 | 0.067 |
| 5 | 0.182 | 0.349 |
| 10 | 0.372 | 0.727 |

The shipped 0.4 sat just over both at `density` 5 — and *under* the window rate
at `density` 6, so one notch of the density slider flipped the whole effect
between a bare desktop and full Caps everywhere.

Changed to **0.05**, which is under the ground rate from `density` 2 up. A window
Cap fills in about a minute and the ground in about two and a half; snow that
stops being fed is gone in about seven. Confirmed on the running session at the
shipped defaults: both windows carry a full Cap and the desktop carries a
continuous contour along its bottom edge.

`maxDepth` is therefore what decides how deep a pile *looks*, and `meltRate` what
decides how long snow lingers once it stops arriving. Both are documented that
way now in `Snow::Settings` and in `docs/development.md`.

### The auto-hidden Panel: now transparent to Flakes

Confirmed on the real thing, and worse than it sounds. With the Panel
auto-hidden for 100 s its Snowline reached a mean of 11.4 px of snow nobody can
see, while the ground under it sat at 0.17 — a bare strip the full width of the
screen, which reads as the desktop class being switched off rather than as a
Panel being in the way.

`Catcher::isCatching()` now means Solid *and* not concealed, where concealed is
KWin's own `EffectWindow::isHidden()` pushed in by `CatcherRegistry` once a
frame. The Snowline is untouched and comes back with the Catcher, so "preserved
across hide and show" still holds; it simply stops taking Flakes out of the air
while nobody can see it.

One consequence worth stating, because it was not asked for and is the same
condition: `isHidden()` also covers a **minimised window** and a window on
**another virtual desktop**, both of which were catching invisibly for the same
reason and are now transparent too. That is what the spec's "Snowline preserved"
rows want; they are about keeping the snow, not about catching in the dark.

> **Wrong, on the last paragraph.** `EffectWindow::isHidden()` is only KWin's
> internal hidden flag; a minimised window and a window on another desktop are
> hidden by none of it, and went on catching. Fixed in issue 12.

### Still open

- **The live session.** Nothing here has run outside a nested compositor:
  `~/.config/plasma-workspace/env/kwin-snow-plugin-path.sh` does not exist, so
  `tools/install-live.sh` plus a logout and back in is the remaining step. The
  two things only it can answer are what an uncapped frame rate costs, and
  whether a real multi-monitor or 1x display changes any of the above.
