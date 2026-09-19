# Cap rendering

Status: ready-for-agent
Blocked by: 03, 05

Draw each Snowline as a Cap, inside its own Catcher's `paintWindow` pass so that
KWin's back-to-front order provides occlusion for free (ADR-0003).

- In `prePaintWindow`, expand `WindowPrePaintData::devicePaint` upward by
  `maxDepth` (converted to device pixels) and call `setTranslucent()`, so the
  strip above the window is neither culled nor treated as opaque.
- Ramp depth to zero across the outermost few Columns to approximate rounded
  decoration corners. This is a correctness fix, **not** a setting — it must not
  appear in the KCM.
- Two styles, selected by `capStyle`:
  - `shaded` — smooth top contour with a soft shadow gradient under the leading
    edge.
  - `contour` — same, plus a static per-Column noise offset on the top edge so
    the pile reads as settled rather than extruded. The noise is generated once
    per Catcher, not per frame.
- The ground Catcher's Cap draws before any window, so windows correctly cover
  it.

Done when an overlapping window hides the Cap of the window beneath it with no
explicit clipping code, and a Cap is visible above a window's titlebar without
being cut off.

## Comments

Carried forward from ticket 03, which has the measurements:

- **A hidden Panel keeps its geometry.** Plasma's auto-hide does not move the
  Panel — KWin marks the window hidden and leaves `frameGeometry` exactly where
  it was (measured: `0,930 1600x70` both hidden and shown). So suppressing a
  hidden Panel's Cap means asking `EffectWindow::isHidden()`; there is no
  geometry change to notice. Its Snowline is preserved either way, which is
  what the spec asks for.
- **The `contour` noise array has to survive a resize too.** A Snowline
  re-spans on resize (truncate right, zero-extend, never rescale), so a
  per-Column noise array generated once per Catcher has to follow the same rule
  or the noise will slide out of step with the depths it offsets. Generating
  the noise as a pure function of the Column index sidesteps the problem
  entirely and is probably the cheaper answer.

Carried forward from ticket 02: a floating Panel's 16 px gap is *inside* its
frame, so a floating Panel catches 16 px above where it looks like it should.
That becomes visible here. See 02's comments for the measurement.

Implemented as `src/cap.{h,cpp}` (what a Cap is made of) and
`src/cappainter.{h,cpp}` (turning that into pixels), with `SnowEffect` growing
the two window hooks that call it. No new domain terms: **Cap** was already in
`CONTEXT.md`, and everything here is its contour, its shading and where it is
drawn.

**Every tuning constant is the prototype's**, restated in logical pixels, the
same way tickets 04 and 06 did it for the motion and the sprites: the ramp of
four Columns, the `contour` style's 0.72 + 0.46 multiplier, the band's fill
colour, the shadow's three stops and where they sit, the bright edge's width and
alpha, and the three-pixel underlap that hides the seam.

### The corner ramp is not only a drawing change

The ticket puts the ramp in the Cap. It is in `Catcher::surfaceAt()` as well,
which is where a Flake finds the top of a pile, because the two have to agree:
ramping only the drawing leaves snow standing invisibly over a rounded corner
and then holds Flakes in the air on top of it. The prototype does the same —
its `insetFactor()` is called from both its hit test and its `capPath()` — and
one shared `capCornerRamp()` is now the only place either asks.

The `contour` style's noise is the other way round and deliberately so: it is
drawing only, so the top edge can read as settled without moving the line
Flakes land on.

Two consequences:

- `SnowlineTest::surfaceIsTheTopEdgeRaisedByItsSnow` had to move its deposit
  clear of the ramp to go on measuring what it was measuring. Two cases were
  added for the ramp itself, one of them for the ground not having one.
- A Catcher narrower than two ramps has them overlap. The prototype takes
  whichever end matches first, which leaves such a Cap lopsided; this takes the
  smaller of the two, which is the same answer everywhere else and symmetric
  here.

### Where a Cap is drawn, and the one that has nowhere good to go

**A window's Cap is drawn in its own `paintWindow`, after the window.** That is
the ticket's ADR-0003 point and it works exactly as advertised: nothing in the
effect clips anything, and an overlapping window hides the Cap of the window
beneath it because KWin painted it later. `prePaintWindow` adds the strip above
the frame to `devicePaint` and calls `setTranslucent()`; without the first the
Cap is culled away entirely.

`capHeadroom()` is that strip's height, and it is a **true bound rather than the
prototype's number**: the `contour` noise can lift a Column 18% past `maxDepth`,
and the leading edge's line is drawn half above the contour, so a headroom of
`maxDepth` alone cuts the top off a full Cap. The shading profile keeps the
prototype's anchor — it is a gradient, and the sliver above its top stop clamps
to exactly that stop, so nothing is lost by leaving it where it was.

**The ground's Cap is drawn just after the wallpaper**, which is the only point
in a frame that is both over the desktop and under every window. The wallpaper
is plasmashell's desktop window, so the hook is `paintWindow` on a window that
answers `isDesktop()`, guarded by a once-a-frame flag set in `paintScreen`
alongside the output being painted (`paintWindow` is not told which that is).

A session with no desktop window therefore draws no ground Cap. Measured in a
bare nested session: the ground Snowline filled to `320 cols, sum 4786.3` — every
Column at the `maxDepth` of 20 — with nothing drawn at the bottom of the screen,
while the window Caps in the same frame were correct. This is the consequence
ADR-0003 already recorded for the Flakes, which escaped it by painting above
everything; a Cap under the windows cannot. It is in `docs/development.md` as
"the ground's Cap needs a wallpaper".

### Shape

**`cap` has no KWin in it and `cappainter` has all of it**, the same split as
`flakesprite`/`flakepainter`, `Snowfall`/`SnowfallRegistry` and
`Catcher`/`CatcherRegistry`. A contour is arithmetic over a Snowline and the
shading is a QImage, so both are checkable in a unit test; only uploading,
batching and drawing need a compositor. `catcher.cpp` includes `cap.h` for the
ramp, so `snowlinetest` and `snowfalltest` compile `cap.cpp` too; `captest`
links no KWin at all.

**A Cap is one draw.** The band and the bright line along its leading edge are
shaded differently — the band by where it is on the screen, the line by
following the contour — which would ordinarily be two textures or two shader
pushes. They are instead two columns of one image: every vertex samples dead on
a texel centre in u, so nothing bleeds between them, and the whole Cap goes into
one vertex buffer and one `GL_TRIANGLES` call.

**The band's fill and its shadow are baked together.** The prototype fills the
band flat and then clips a blue-grey gradient over it; both are flat across the
band, so the two collapse into a single opaque vertical ramp, which is what the
profile image holds.

**The shader is KWin's own**, `MapTexture | Modulate | TransformColorspace`,
for the reasons ticket 06 gives: the snow is colour managed the way a window is,
and there is no hand-written GLSL to keep working across the ABI churn ADR-0002
accepts.

**A Cap carries the window's paint transform.** The model-view-projection handed
to the painter is `viewport.projectionMatrix() * data.toMatrix(viewport.scale())`
and the modulation is `data.opacity()`, so a Cap follows its window through
whatever effect below is moving, scaling or fading it rather than hanging in the
air at the frame geometry. The ground's Cap belongs to the output rather than to
the wallpaper it is drawn over and takes the projection alone.

**The `contour` noise is a function of the Column index**, as ticket 03's comment
suggested, not an array generated once per Catcher. A Snowline re-spans on
resize and an array would have to follow the same rule or slide out of step with
the depths it offsets; a function cannot. The price is that two Catchers of the
same width get the same bumps, which nothing resolves at a fifth of a Column's
depth over five logical pixels.

### Tests

`tests/captest.cpp`, fourteen QTest cases: the ramp being out of the way in the
middle and symmetric at both ends and on a Catcher too narrow for two of them,
the noise being the same every time it is asked, not following the Column count,
and using its whole range, `shaded` drawing exactly what Flakes land on,
`contour` nudging every Column without inverting or going below bare, a Cap
covering every Column, the ground having no corners, a dusting not being worth
drawing, the headroom covering the deepest a Cap can be drawn, the profile
running from above the snow to inside the Catcher, and the profile image being
an opaque band and a bright edge that darkens downward into the sky's colour.

`tests/snowlinetest.cpp` gains two for the ramp reaching `Catcher::surfaceAt()`.

`ctest --test-dir build` passes in Debug and Release, with no new warnings.

### Verified in the nested compositor

`tools/dev.sh` with `plasmashell` inside for a real wallpaper and a real Panel,
driven over D-Bus with KWin and Plasma scripts, screenshots taken from inside
the nested session. Four builds, because there is still no way to change a
setting at runtime: the spec's defaults, `meltRate` 0 to fill a Cap to the depth
cap, `capStyle` `shaded`, and `snowOnWindows` false.

| Check | Result |
| --- | --- |
| A Cap above a titlebar | Drawn in full above the frame, not cut off, at depths up to the `maxDepth` of 20 |
| Occlusion, with no clipping code | The back window's Cap runs behind the front window and stops dead at its edge |
| The ground's Cap | Drawn along the bottom of the output, over the wallpaper and under the Panel |
| Corner ramp | Both ends of every window Cap come down to meet the frame |
| `contour` | Irregular top edge, settled rather than extruded |
| `shaded` | Smooth contour, same shadow, visibly the other style |
| An auto-hidden Panel | `hidden=true` and its Cap gone, with its Snowline preserved |
| A disabled class | `snowOnWindows` false: windows kept no snow and drew no Cap while the ground filled and drew one |
| A 30-step drag and a minimise | No stray Cap left behind, no assertion failure on a Debug build |
| Three `unloadEffect`/`loadEffect` cycles | Compositor still serving, profile texture rebuilt, Caps still drawn |
| Stability | No crash, no assertion failure, no new log noise |

### Tuning findings for ticket 11

**At the spec's defaults a Cap is a thin crust.** Ticket 05 measured the steady
state at a mean of 0.16–0.21 px against a `maxDepth` of 20; drawn, that is the
band's three-pixel underlap and its bright edge and almost nothing else — a
light rim along a window's top, which reads well enough but is not a pile. The
ground never gets there at all: measured at 0.12 px mean, under the 0.15 px a
Cap is drawn at, so at the defaults the desktop shows no snow lying on it.
Whether that is the effect wanted or an argument for a lower `meltRate` is
ticket 11's to settle.

**A floating Panel catches 16 px above where it looks like it should**, as
ticket 02 predicted this would make visible. Not re-measured here: the Panel in
these runs was flush. The fix would have to invent the Panel's visible edge,
since KWin does not report it, and it is left alone.

**A vertical Panel against the top of the screen draws its Cap off-screen.**
Its catching surface is its own frame top at y=0, which the spec keeps
deliberately ("correctly a thin sliver"), so the Cap standing on it is above the
output. Harmless, and the snow is still there if the Panel moves.

### Consequences worth knowing

- **`Settings` grew one key**, `capStyle`, with the spec's default of `contour`.
  Ticket 09 backs it with KConfigXT like the rest.
- **A disabled class stops drawing immediately**, which is the prototype's
  behaviour: its Snowline is kept and goes on melting, but the Cap is gone the
  moment the class is switched off. Ticket 09, which turns disabling into an
  accelerated melt, is where that should become a fade instead — and it is the
  ticket that makes the melt visible at all.
- **An auto-hidden Panel goes on catching.** Its class is still Solid, so Flakes
  stop on a surface nobody can see and the ground below gets nothing. That is
  the most literal reading of "Snowline preserved across hide and show" and the
  snow is all there when the Panel comes back, but it is a choice, and ticket 11
  is where to decide whether a hidden Panel should be transparent instead.
- **Every Cap is its own draw call.** A dozen Catchers is a dozen draws of a few
  thousand vertices, which is nothing next to the full-screen repaint the effect
  already asks for every frame — but it is per Catcher, where the Flakes are per
  output, so it is the part that grows with how many windows are open.

**Not done:** `Status:` left as-is — `docs/agents/triage-labels.md` still defines
no completed state.
