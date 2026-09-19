# Snowline storage and geometry rules

Status: ready-for-agent
Blocked by: 02

Give each Snow Catcher a Snowline and make it behave correctly under geometry
changes.

- A Snowline is an array of depths indexed by Column in the Catcher's local X
  coordinates. Column width and all depths are **logical** pixels
  (ADR-0001); conversion to device pixels happens at paint time only.
- Anchor the Catcher's top edge at `EffectWindow::frameGeometry()`, which KWin
  documents as excluding both server-side and client-side drop shadows.
- Move: nothing to do, local coordinates make it free. Assert this rather than
  assuming it.
- Resize: anchor the array at the left edge and truncate or zero-extend. **Do
  not rescale** — rescaling smears existing snow (ADR-0001).
- Minimise, virtual desktop switch, Panel auto-hide: Snowline preserved.
- Close: Snowline discarded.
- Window moves between outputs of different scale factors: Snowline survives
  unchanged, because it is stored in logical units.

Done when a Snowline seeded with a test pattern survives every event in that
list with its contents intact and correctly positioned.

## Comments

Implemented as `src/snowline.{h,cpp}`. A `Catcher` owns a `Snowline` by value,
so Catcher lifetime is Snowline lifetime with nothing to keep in step, and
`Catcher::setGeometry()` is the single place where geometry reaches the snow.

**Column width is 5 logical px**, the prototype's `COLW`, and Column count is
`max(1, ceil(width / 5))` — the same formula, so the ceil keeps a Column for
the rightmost sliver of a Catcher and a degenerate zero-width Catcher still has
an array rather than an empty one every caller would have to check for.

**The resize rule is `QList::resize`**, which truncates on the right and
value-initialises new Columns to zero — literally the rule ADR-0001 asks for,
anchored at Column 0, with no rescaling anywhere to be tempted by.

**Move is free, and that is now enforced three ways.** `Snowline::setWidth()`
returns early when the Column count is unchanged, which is the only path from a
geometry change to the data; `Catcher::setGeometry()` snapshots the Snowline in
debug builds and asserts it is untouched whenever the width did not change, so
a drag checks it on every frame; and the unit test compares contents across a
move. The assert survived a scripted 30-step drag in a debug nested session.

**`Catcher::columnAt(globalX)`** is the one place global X becomes local X.
Ticket 05 will hit-test through it; it is here because "correctly positioned"
is only checkable if something maps the two.

### Tests

`tests/snowlinetest.cpp`, eleven QTest cases, run with `ctest --test-dir build`.
This is a **new convention for the repo** — tickets 01 and 02 verified in the
nested compositor only. It earns its place because the geometry rules are pure
arithmetic that is tedious to provoke through a compositor and trivial to state
directly; everything that genuinely needs KWin is still verified nested. The
target is behind `BUILD_TESTING` (CTest's own default-on flag), so
`-DBUILD_TESTING=OFF` drops it and with it the only use of `Qt6::Test`. It
links `KWin::kwin` purely to resolve the `EffectWindow`/`LogicalOutput` calls
in `catcher.cpp`; no test touches either and both are passed as `nullptr`.

### Verified in the nested compositor

`KWIN_SNOW_SNOWLINE_TEST=1` seeds every new Snowline with depth == Column index
and polls once a second, reporting the Catcher set whenever it changes — the
poll is there because a minimise, a desktop switch and a Panel auto-hiding
reach the effect as no event at all, so there is nothing to hang a report on.
Driven by KWin and Plasma scripts over D-Bus, with `konsole` and a real
`plasmashell` Panel, on a debug build. `sum` below is the sum of all depths,
which is what makes "intact" checkable at a glance.

| Event | Result |
| --- | --- |
| Move, same size | `140 cols, sum 9730` before and after |
| Drag: 30 successive moves | unchanged, no assertion failure |
| Resize wider (700 → 900 px) | `180 cols, sum 9730` — head intact, tail `0 0 0 0` |
| Resize narrower (900 → 400 px) | `80 cols, sum 3160` = sum(0..79), truncated on the right |
| Minimise / unminimise | `minimized=true` then `false`, Snowline unreported, i.e. unchanged |
| Virtual desktop switch and back | `desktops=2`, `current=two` then back, Snowline unchanged |
| Panel auto-hide on / off | panel `hidden=true` then `false`, Snowline unchanged |
| Move between outputs | `WL-1` → `WL-0` and back, `80 cols, sum 3160` throughout |
| Close | Catcher and Snowline both gone |

**Panel auto-hide does not move the Panel.** KWin marks the window hidden and
leaves `frameGeometry` exactly where it was (`0,930 1600x70` both ways), so
there is no geometry event to mishandle. Worth knowing for ticket 07: a hidden
Panel's Cap has to be suppressed by asking whether the window is hidden, not by
noticing it went somewhere.

**Logical pixels, confirmed against the device buffer.** Launched at `--scale 1`
and `--scale 2` the nested output's device mode is 1600x1000 and 3200x2000
respectively, and the ground Snowline is `320 cols` in both — 1600 logical / 5.
Device pixels would have given 640 at scale 2.

### Not verified

**Two outputs at genuinely different scale factors.** `kwin_wayland --scale`
applies to every nested output at once, and a per-output `scale` hand-written
into `build/dev-home/kwinoutputconfig.json` is ignored by the nested backend —
both outputs still came up 1600x1000 logical. What is established instead: the
Snowline has no scale input at all (nothing in `snowline.cpp` mentions an
output), the column count is logical as shown above, and a cross-output move
with the Snowline intact was exercised on two same-scale outputs. The remaining
gap is only that the two outputs differed in nothing.

### Consequences worth knowing

- **Shrinking then growing back does not restore the snow.** The truncated
  Columns are gone and the regrown ones arrive bare. That is the ADR's rule
  working, not a bug, and it is what the unit test pins down.
- **A left-edge resize slides the pile.** Anchoring at Column 0 means the snow
  follows the left border rather than staying over the same screen X, so
  dragging a window's left edge drags its snow with it. ADR-0001 chose this
  over rescaling; it will be visible once ticket 07 draws Caps, and ticket 11
  is where to decide whether it reads badly enough to want anything else.
- The diagnostics flag, the seeding and the poll are all scaffolding for a
  Snowline that nothing can fill yet. Ticket 05 makes real snow watchable and
  all of it can go.

**Not done:** `Status:` left as-is — `docs/agents/triage-labels.md` still defines
no completed state.
