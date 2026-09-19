# Snow Catcher registry

Status: ready-for-agent
Blocked by: 01

Track which surfaces can catch snow, for the lifetime of each one.

- Classify every `EffectWindow` into a Catcher class: Panel (`isDock()`),
  window, or neither. The desktop ground is a synthetic Catcher per output, not
  an `EffectWindow`.
- Exclude top-edge Panels from catching entirely (spec: Behaviour). Vertical and
  floating Panels are included and catch on their own frame top edge.
- Subscribe to window added, closed, and `windowFrameGeometryChanged` so the
  registry stays in step with KWin.
- Per-output: a Catcher belongs to the output it is on, and the ground Catcher
  is per output.

No accumulation or rendering in this ticket — this is bookkeeping only, verified
by logging the Catcher set as windows open, close, move between outputs, and
change class.

## Comments

Implemented as `src/catcher.{h,cpp}` (the thing) and `src/catcherregistry.{h,cpp}`
(the bookkeeping). `SnowEffect` now owns a `CatcherRegistry` and is otherwise
unchanged — it still never asks to be painted. The logging category moved out to
`src/snowlogging.{h,cpp}` so the registry can log without including the effect.

**A Catcher is its catching surface.** `geometry().top()` is the line Flakes land
on, `geometry().left()` is the origin of the local X axis a Snowline will be
indexed in, and `geometry().width()` is how far that axis runs — all in global
*logical* pixels (ADR-0001). Windows and Panels take it from `frameGeometry()`;
the ground is a zero-height strip along the bottom edge of its output, which is
how the prototype models it and what keeps `top()` meaning the same thing for
all three classes. `catchers()` returns them in paint order: grounds first, then
windows and Panels bottom-to-top in KWin's stacking order, which is the order
ticket 05 hit-tests in reverse.

**Classification.** `isDock()` is a Panel. Anything else that is managed and is
not `isSpecialWindow()`, a popup, the lock screen, an input method or a drag
icon is a window. `isSpecialWindow()` is KWin's own predicate and drops the
desktop window (the ground is synthetic, one per output), notifications, OSDs,
splashes, toolbars and applet popups in one go. A window with no output is not a
Catcher, because a Catcher belongs to an output.

**Top-edge Panels.** Excluded when the Panel is wider than it is tall *and* its
frame top is within 1 logical px of its output's top. The orientation test is
what lets vertical Panels through: a left Panel is also flush with the top of
the screen, and the spec keeps it catching on its own frame top edge, correctly
a thin sliver. Verified both ways by moving a real Plasma panel between edges at
runtime.

**Floating Panels contradict the spec, and the spec loses.** The spec says
"Floating Panels catch on the Panel's own top edge, not the screen edge". They
cannot, because KWin does not report the Panel's own top edge: measured on a
bottom Panel, `frameGeometry().top()` moved from y=946 to y=930 when it started
floating — the 16 px floating gap is *inside* the frame, transparent, and the
effect has no way to ask where the visible edge is. Two consequences:

- A floating *top* Panel is indistinguishable from a flush one (frame top at the
  screen edge), so it is excluded. That follows the rule's own reasoning — a Cap
  there would be pinned under the screen edge — so it is left as is.
- A floating Panel anywhere else catches 16 px above where it looks like it
  should. Harmless bookkeeping now; it will be visible as soon as ticket 07
  draws Caps, and the fix, if it is worth one, belongs there.

**Verified** in the nested compositor (`tools/dev.sh`, real `plasmashell` and
`konsole`, driven over D-Bus with KWin and Plasma scripts). The registry logs
the whole Catcher set at `qCDebug` whenever the set changes shape, which is what
these were read from:

- Two outputs give two ground Catchers, `WL-0` and `WL-1`, each a zero-height
  strip at its own output's bottom edge.
- A Panel appears as a Catcher when `plasmashell` starts, a window when
  `konsole` opens, and disappears again when it closes.
- Moving a window across to the other output logs `moved output` and re-homes
  the Catcher; moving it within one output logs nothing, because nothing about
  the set changed.
- Panel to the top edge: `no longer catches`. To the left edge: catches again,
  as `x=[0,54]` with its surface at `y=0`. Back to the bottom: no log, because
  it is the same Catcher in a new place.
- A minimised window stays in the set (its Snowline has to survive, spec:
  Behaviour).
- Clean build against KWin 6.6.6, no warnings.

**Not verified:** output removal. The nested compositor exposes no way to
hot-unplug an output — no `VirtualOutputs` D-Bus object on `org.kde.KWin`, and
`kwin_wayland --output-count` is fixed at startup. The path is written to be
safe rather than tested: it drops the ground Catcher and clears the dangling
output pointer immediately, then re-homes the windows on the next event loop
turn, once KWin has finished moving them off.

**Glossary gap:** the third Catcher class has no entry in `CONTEXT.md`. The spec
calls it "the desktop ground" and the code calls it `Ground`; `/domain-modeling`
may want to give it a proper definition alongside **Panel**.

**Not done:** `Status:` left as-is — `docs/agents/triage-labels.md` still defines
no completed state.
