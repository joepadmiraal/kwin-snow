# Snow — KWin effect

A native KWin 6.6 C++ compositor effect. Snow falls over the Plasma desktop and
accumulates on the top edges of windows and panels.

Terms used below (**Flake**, **Snow Catcher**, **Snowline**, **Column**,
**Panel**, **Solid**, **Cap**) are defined in [`CONTEXT.md`](../../CONTEXT.md).

## Model

Every surface that can catch snow is a **Snow Catcher**. There are three
classes: windows, Panels (anything KWin reports as a dock), and the desktop
ground. Each class is independently enabled.

Each Catcher owns a **Snowline**: a one-dimensional array of depths indexed by
**Column** in that Catcher's own local X coordinates. Because the coordinates
are local, a Snowline travels with its Catcher.

An enabled Catcher class is **Solid**. A falling Flake is hit-tested against
Catchers topmost-first down the stacking order; it stops on the first Solid
Catcher whose top surface it crosses, and its mass is deposited into that
Catcher's Snowline. A disabled class is transparent to Flakes, which fall
through it to whatever is below.

Deposition spreads into neighbouring Columns, and a relaxation pass each frame
settles spikes toward an angle of repose, so piles look settled rather than
spiky. Depth is clamped at a configured maximum; excess at the cap is discarded,
not shed onto surfaces below. A configured melt rate decays every Column
continuously, and because neither that rate nor the rate snow arrives at depends
on how deep a Column already is, the two do not balance: melt under the arrival
rate lets a Column climb to the maximum, melt over it holds the Column at bare.
So `maxDepth` is what decides how deep a pile stands and `meltRate` is what
decides how long snow lasts once it stops arriving. Measured on the shipped
simulation at `fallSpeed` 5, snow arrives at 0.036 × `density` logical px/s per
Column on the desktop and 0.070 × `density` on a window.

All lengths in the model and in configuration — Column width, depths, maximum
depth — are **logical** pixels, converted to device pixels at paint time, so the
effect looks identical across displays with different scale factors and a
Snowline survives a window moving between them.

## Rendering

Two paint sites:

- **Caps** are drawn inside each Catcher's own `paintWindow` pass. KWin paints
  windows back-to-front, so an overlapping window correctly hides the Cap of the
  window beneath it, and no explicit occlusion logic is needed. Because a Cap
  rises above its window's frame geometry, `prePaintWindow` expands
  `WindowPrePaintData::devicePaint` upward by the maximum depth and calls
  `setTranslucent()`.
- **Flakes** are drawn in a single `postPaintScreen` pass above everything. See
  [ADR-0003](../../docs/adr/0003-no-below-windows-flake-pass.md) for why there is
  no z-order option.

Flakes land on `EffectWindow::frameGeometry()`'s top edge, which KWin documents
as excluding both server-side and client-side drop shadows, so decorated and
CSD windows need no special-casing. Depth is ramped to zero across the outermost
few Columns to approximate rounded decoration corners; this is a correctness fix,
not a setting.

A Snow Catcher that is off screen — minimised, auto-hidden, on another virtual
desktop — stops catching while it is away, and keeps what it was already
holding. Snow it would have caught falls past to whatever is below, so a hidden
Panel does not leave a bare strip of desktop under it.

The simulation is per-output. Flakes spawn above each output's top edge and do
not drift between outputs — cross-output travel produces flakes appearing in
mid-air wherever outputs differ in size or vertical position.

## Behaviour

| Event | Rule |
| --- | --- |
| Window moved | Snowline travels with it |
| Window resized | Anchor Snowline at the left edge; truncate or zero-extend. Never rescale |
| Window minimised | Snowline preserved; stops catching while it is away |
| Window closed | Snowline discarded, no falling debris |
| Virtual desktop switched | Snowline preserved; stops catching while it is away |
| Panel auto-hidden | Snowline preserved across hide and show; stops catching while hidden |
| Fullscreen window active | Effect suspends entirely |
| Session locked or idle | Effect suspends |
| Settings changed | Melt toward the new state, never snap |

Top-edge Panels are excluded from catching: a Cap pinned beneath the screen edge
reads as a rendering bug rather than as snow. Vertical Panels catch on their own
frame top edge, which is correctly a thin sliver. Floating Panels catch on the
Panel's own top edge, not the screen edge.

Animation is capped at a configurable frame rate, defaulting to 30 fps. The
effect forces continuous repainting whenever it runs, so this cap is the main
control on its power cost.

## Configuration

A KCM config plugin (`kwin_snow_config.so`) alongside the other effect config
plugins, in two tabs.

**Basic** — what it looks like:

| Key | Type | Default |
| --- | --- | --- |
| `snowOnWindows` | bool | true |
| `snowOnPanels` | bool | true |
| `snowOnDesktop` | bool | true |
| `flakeStyle` | enum: `blob`, `crystal`, `depth` | `depth` |
| `capStyle` | enum: `shaded`, `contour` | `contour` |
| `density` | int 1–10 | 5 |

**Advanced** — how it behaves:

| Key | Type | Default |
| --- | --- | --- |
| `fallSpeed` | int 1–10 | 5 |
| `windStrength` | int 0–10 | 4 |
| `flakesInFrontOfWindows` | bool | true |
| `maxDepth` | int, logical px | 20 |
| `meltRate` | real, logical px/s, 0 = permanent | 0.05 |
| `frameRateCap` | int fps | 30 |

`density` is flakes per unit of screen area, so visual density holds at 1080p and
4K alike. Wind is noise-driven gusts with per-Flake response, not a constant
sideways drift.

`flakesInFrontOfWindows` is what a gust does when it blows a Flake past the left
or right edge of a Snow Catcher, below the edge snow settles on: the Flake never
crossed the catching edge, so it is not caught, and it is either drawn falling in
front of the window (true) or held back until it comes out below it (false).
Nothing else changes between the two — the same Flakes fall at the same rate and
leave the same snow; it is only whether they are drawn while they are there.

**Flake styles.** `blob` is a soft radial-falloff sprite at one uniform size.
`crystal` is a rotating procedural six-point sprite. `depth` scales each Flake's
size, opacity and fall speed by a per-Flake z, giving parallax.

**Cap styles.** `shaded` is a smooth top contour with a soft shadow under the
leading edge. `contour` adds a static per-Column noise offset to the top edge so
the pile looks settled rather than extruded.

## Plugin

- Installed as `snow.so`, which under KF6 *is* the plugin id: the effect is
  known everywhere as `snow`, including `[Plugins] snowEnabled` and
  `[Effect-snow]`. Display name "Snow", category Appearance. See
  [ADR-0004](../../docs/adr/0004-effect-identity-is-the-plugin-filename.md) for
  why this is not `kwin_effect_snow`.
- **Disabled by default on install.**
- Built against `kwin-dev` 6.6.6; installs to
  `~/.local/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/plugins/`.
- Development runs against a nested `kwin_wayland`, with final verification on
  the live session. A compositor plugin will segfault during development and
  doing that to a live session repeatedly costs more than the fidelity gap.

## Out of scope

- Distribution to other machines: packaging, X11 support, version guards, CI
  across KWin releases. See
  [ADR-0002](../../docs/adr/0002-native-cpp-effect-accepting-abi-churn.md).
- Snow shedding off a Catcher onto whatever is below it.
- Snow sliding off a window that is dragged quickly.
- Querying a KDecoration3 decoration for its real corner radius instead of using
  the fixed inset ramp.

## Reference

A canvas prototype running this exact model — local-coordinate Column arrays,
topmost-first Solid hit testing, relaxation, melt, inset ramp, Caps in stacking
order, Flakes in one pass — is published at
<https://claude.ai/artifact/DkmdkzpmLB2ar7PuNv37iX>. The shipped defaults above
are the prototype's, with one exception found by the visual tuning pass
(ticket 11): `meltRate` is 0.05 rather than the prototype's 0.4, which was over
the rate snow arrives at and so left nothing standing anywhere.
