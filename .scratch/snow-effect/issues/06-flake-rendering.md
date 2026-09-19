# Flake rendering

Status: ready-for-agent
Blocked by: 04

Draw the Flakes in a single `postPaintScreen` pass above every window. There is
no below-windows pass and no z-order setting (ADR-0003).

Three styles, selected by `flakeStyle`:

- `blob` — soft radial-falloff sprite, one uniform size.
- `crystal` — procedural six-point sprite with branches, rotating per Flake.
- `depth` — blob whose size and opacity scale with the per-Flake z, giving
  parallax against the fall speed already varied in issue 04.

Pre-render each sprite once to a texture rather than drawing geometry per Flake,
and batch the draw. The published prototype is the visual reference.

## Comments

Implemented as `src/flakesprite.{h,cpp}` (what a Flake looks like) and
`src/flakepainter.{h,cpp}` (turning that into pixels), with `SnowEffect`
growing the pass that calls it. No new domain terms: a sprite is how a **Flake**
is drawn, not a thing in the model.

**Every tuning constant is the prototype's**, restated in logical pixels, the
same way ticket 04 did it for the motion — the sprite sizes, the radial
gradient's stops and colours, the crystal's arm and branch lengths, the depth
style's size and opacity ramps. The prototype is the reference for whether it
reads as snow, so inventing different numbers would only drift away from it.

### Three places this departs from the ticket, all deliberate

**The pass is `paintScreen`, not `postPaintScreen`.** KWin 6.6 hands
`postPaintScreen()` nothing: no render target, no viewport, and its own
documentation says "you shouldn't paint anything here". `paintScreen` is the
same point in the frame — the Flakes go in after `effects->paintScreen()`
returns, which is after every window — and it is the call that has what drawing
needs. Everything ADR-0003 asks for holds: one pass, no per-window work, no
z-order option.

**The `depth` style is drawn in eight depth planes rather than one draw.** A
Flake's opacity under that style is a function of its z, and the generated
shader has one `modulation` for a whole draw, so a per-Flake opacity means
either a draw per Flake or a shader of our own — and the ticket asks for the
draw to be batched. Cutting the population into planes gives the batch back:
size, fall speed and landing mass still vary continuously per Flake, and only
the opacity is quantised, in steps of 0.7/8 that nothing in a field of moving
snow resolves. Drawing far plane first is a bonus the single draw did not have:
near Flakes now blend over distant ones rather than in list order. `blob` and
`crystal` have one flat opacity and so come out as the single draw the ticket
describes.

**The shader is KWin's own, not one of ours.** `MapTexture | Modulate |
TransformColorspace` is what a window is painted with, so the snow is colour
managed the same way — it is sRGB on an sRGB output and correct on an HDR one —
and there is no hand-written GLSL to keep working across the ABI churn
ADR-0002 accepts. That shader is also what makes the depth planes the right
answer above.

### Shape

**`flakesprite` has no KWin in it and `flakepainter` has all of it.** Where a
Flake's sprite goes and how strongly it is drawn is arithmetic, and what the
blob and the crystal look like is a QImage, so both are checkable in a unit
test; only uploading, batching and drawing need a compositor. It is the same
split as `Snowfall`/`SnowfallRegistry` and `Catcher`/`CatcherRegistry`, and
`tests/flakespritetest` is the first test target here that links no KWin at all.

**`blob` and `depth` share one sprite.** What `depth` adds is a size and an
opacity per Flake, not a different shape (spec: Configuration), so there are two
textures, not three, and switching between those two styles does not re-upload
anything.

**The sprites are rendered at twice their design size.** A sprite is drawn at up
to about 21 logical pixels across against a design size of 32; on an output at
scale 2 that is 42 device pixels, which would magnify it. Twice over covers that
without anyone having to re-render a texture when a window crosses to a display
with a different scale factor.

**The crystal's shape is a texture and its angle is geometry.** The shape never
changes and only the angle does, which is the whole reason it is a sprite: six
arms with four branches each is 36 strokes a Flake a frame drawn directly, and
two triangles drawn this way.

**Nothing is drawn under QPainter compositing.** The snow falls, lands and melts
as usual and is simply not painted. A second renderer for a fallback that no
machine which can composite at all ends up on is not worth carrying.

### Tests

`tests/flakespritetest.cpp`, thirteen QTest cases, run with
`ctest --test-dir build`. They cover `blob` being one size and one opacity
whatever the z, `depth` scaling both with z and doing it monotonically, the two
agreeing about the opacity of a depth (which is what the depth planes ask for),
the near plane not outgrowing the flat styles, only `crystal` turning, the
crystal filling a tighter square, the per-Flake radius spread surviving into the
drawing, the blob being round, centred and falling off to nothing without a
step back up, the crystal having six arms with nothing between them and nothing
in its corners, supersampling buying resolution rather than a different Flake,
and both images being premultiplied and square.

`ctest --test-dir build` passes in Debug and Release, with no new warnings.

### Verified in the nested compositor

`tools/dev.sh --panel`, which runs `plasmashell` inside for a real wallpaper to
see white snow against. Screenshots were taken from inside the nested session,
so the live session was never involved. See `docs/development.md`.

| Check | Result |
| --- | --- |
| Flakes are drawn | Snow over the whole desktop, above the wallpaper and above the Panel |
| The whole population | 475 Flakes in, 2850 vertices out, every frame |
| `depth` | Small faint Flakes and large bright ones in the same field, reading as distance |
| `blob` | One opacity, the prototype's per-Flake radius spread, no parallax |
| `crystal` | Six-point crystals, each at its own angle, turning as they fall |
| Distribution | Even across the output over 20 s, no banding and no clumping |
| Scale | `viewport.scale()` 2 on the nested output; sizes and positions both right |
| Three `unloadEffect`/`loadEffect` cycles | Compositor still serving, textures rebuilt, snow still falling |
| Stability | No crash, no assertion failure, no new log noise |

**A fullscreen window makes a poor test scene**, which cost some time to work
out and is now in `docs/development.md`: it is a Snow Catcher whose top edge is
the top of the output, so it catches every Flake within a frame or two of the
spawn and what is left is the handful the wind blew in from the sides. That
looks exactly like a rendering bug and is the effect working. It is also what
the fullscreen suspension of the spec's Behaviour table exists for (ticket 08).

### Not verified

**Two outputs at genuinely different scale factors**, for the third ticket
running: the nested backend accepts `--scale` and ignores it — `--scale 1` and
`--scale 2` both produced a 1600x1000 logical output rendered at 3200x2000.
What stands in for it: the scale the positions and the sizes are multiplied by
is `viewport.scale()`, which is the same number the viewport's own projection is
built from, so a logical pixel is a logical pixel by construction; and the
effect did draw correctly at the scale 2 it actually got.

**An HDR output.** The snow goes through the colour management the window path
goes through, and there is no HDR display here to look at it on.

### Consequences worth knowing

- **The effect now draws on every frame it asks for**, which until ticket 08 is
  every frame the compositor can produce. Ticket 04 noted that switching the
  effect on drives the compositor at its full rate while drawing nothing; it now
  draws a few thousand vertices and two hundred kilobytes of texture as well.
  The frame rate cap is still the control the spec expects.
- **`Settings` grew nothing.** `flakeStyle` was already there, because ticket 04
  needed it for the fall speed; the painter reads the same field.
- **Caps are still not drawn**, so snow lands and piles up invisibly. That is
  ticket 07, and it is now the only part of the model with no pixels.

### Scaffolding deleted, as ticket 04 asked

`KWIN_SNOW_FLAKE_TEST` and the once-a-second Flake dump in
`snowfallregistry.cpp` are gone, along with `reportFlakes()`, `m_lastReport` and
the report interval, and the "Watching Flakes" section of
`docs/development.md` is now about looking at the snow. `Snowfall::describe()`
stays: the line logged when a Snowfall starts is event-driven, like
`logCatchers()`, and is what says how much snow an output got.

**Not done:** `Status:` left as-is — `docs/agents/triage-labels.md` still defines
no completed state.
