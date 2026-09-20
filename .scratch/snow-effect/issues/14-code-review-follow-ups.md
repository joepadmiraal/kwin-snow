# Follow-ups from the review of the scissor and frame-clock changes

Status: ready-for-agent

Four sessions, each self-contained and each meant to be handed to a fresh Claude
Code session that has never seen this work. They came out of a review of the
uncommitted changes that added the repaint-region scissor to both painters
(`FlakePainter`, `CapPainter`), `Schedule::decided` to `FrameClock`, and
`src/frameprobe.{h,cpp}`.

None of the four is a defect in the shipped effect. The scissor fix and the
`Schedule::decided` fix were both found correct; these are follow-ups on the new
code around them.

| # in the review | Session | What it is |
| --- | --- | --- |
| 1 | **A** | The probe's per-output state is not per output |
| 2 | **B** | Investigate: draw calls multiply by damage-region rect count |
| 3 | **C** | Split `waitAfterFrame()` so the bug it caused cannot be written again |
| 4, 5, 6 | **D** | Three nits, grouped because each is a few lines |

## How to hand one over

Give the session the path to this file and the session letter, nothing else.
Each session brief below states its own context, so none of them needs the
review, the others, or this preamble.

Before starting, every session reads `CONTEXT.md` and the ADRs under `docs/adr/`
that touch its area — `AGENTS.md` says so, and ADR-0005 is the one that matters
for A, B and C.

Set `Status:` on the session's own heading to `claimed` before any work and to
`resolved` when it lands, and append what happened under `## Comments` at the
bottom of this file.

## Ordering and conflicts

Run them **A, then D, then B** — and **C** wherever it suits, including
alongside the others.

- **A before D.** Both touch `FrameProbe::beginFrame()`. They do not change the
  same lines, but A moves state into `struct Output` and D1 changes the
  signature, so doing A first saves a merge.
- **D before B.** D2 rewrites the first twenty lines of `FlakePainter::paint()`
  and D1 changes `beginFrame()`'s signature; B adds instrumentation next to
  both. Landing D first means B is written against the shape that is staying.
- **B last.** It may end at a human gate, and it is the only one whose scope is
  not known before it starts.
- **C is isolated.** It touches `framepacer.*`, `frameclock.*`,
  `framepacertest.cpp`, ADR-0005 and ticket 13 — no file that A, B or D go near.
  It is also the largest, so if two sessions can run at once, C alongside A is
  the fastest route through all four.

One reason to take C early rather than last: ticket 13
(`13-a-request-that-misses-its-frame.md`) is still open, its next step is the
offline replay in `13-pacing-sim.py`, and both the ticket and the script are
written against `waitAfterFrame()`. C updates them. Picking ticket 13 up before
C lands means doing that work twice.

---

## Session A — the probe's per-output state is not per output

Status: resolved

### What is wrong

`src/frameprobe.h` keeps a `struct Output` of what the probe remembers about one
output between frames, holds one per output in `m_outputs`, and looks the right
one up in `beginFrame()`. Two pieces of between-frame state were left outside
it:

- `int m_lastTranslucentWindows = -1;` — a flat member, compared against in
  `endFrame()` to report a change in how many windows the effect took out of the
  occluders.
- `Window m_window;` — the once-a-second summary accumulator, also a flat
  member.

`SnowEffect` calls the probe once per output per frame, so on a two-monitor
session both of these alternate between two outputs' values:

- The translucent count differs whenever the two monitors are not showing the
  same number of capped windows — two on the laptop panel, none on the
  external — so `endFrame()` sees a change on *every single frame* and logs
  `translucent windows 2 -> 0` then `0 -> 2`, for ever. At 60 Hz that is 120
  lines a second. The class doc in `frameprobe.h` says the probe reports
  interesting frames as they happen and sums the rest up once a second because
  that "is what keeps a log that is worth reading at 60 frames a second from
  being one nobody scrolls through" — this defeats exactly that, on exactly the
  multi-monitor setup ADR-0005 exists for.
- The summary line sums both outputs and names neither, so two 60 Hz monitors
  read as `120 frames, 60 stepped`, which looks like a refresh-rate anomaly
  rather than like two outputs. `widest present gap` likewise mixes them, so a
  stall on one monitor is indistinguishable from a stall on the other.

### The fix

Move both into `struct Output`:

- `lastTranslucentWindows` beside `lastPresent` and `lastStep`, still starting
  at `-1`.
- `Window window;` as a member of `Output`, and delete the flat `m_window`.

`summarise()` then takes the output it is summarising. `endFrame()` needs a
handle on the current output's entry: keep an `Output *m_current` set in
`beginFrame()` alongside `m_output`, and clear it in `endFrame()` with
`m_output`. A pointer into `std::unordered_map` is safe here — the standard
guarantees references and pointers to elements stay valid across rehashing, and
the only insertion is `beginFrame()`'s own.

Each output then opens and closes its own one-second window, so each emits its
own summary line. Those lines must name the output. Today the per-frame lines
say `Snow probe <name> -- ...` and the summary says `Snow probe 1.00 s -- ...`;
make the summary `Snow probe <name> 1.00 s -- ...` so the two read as one
family.

### Also update

- The class doc in `src/frameprobe.h` — "reported once a second" becomes "once a
  second per output".
- The worked example under *Watching frames* in `docs/development.md`. Its
  summary line has no output name and must gain one. The prose there is right as
  it stands; only the sample output changes.

### How to verify

`FrameProbe` takes a `KWin::LogicalOutput *` and cannot be unit-tested here, so
this is checked in the nested session, which is the primary loop
(`docs/development.md` → *Primary loop: nested compositor*):

```sh
KWIN_SNOW_PROBE=1 tools/dev.sh --outputs 2 --panel
```

`tools/dev.sh` `exec`s into `kwin_wayland`, so run it in the background and
capture its output. Two outputs and a `plasmashell` Panel give the two monitors
different capped-window counts, which is the condition that triggers the bug.

- **Before the fix**, `grep "translucent windows"` on the log shows a pair of
  lines per frame, continuously.
- **After the fix**, that grep is quiet except when a window actually opens or
  closes, and `grep " s -- "` shows two summary lines a second, one per output,
  each naming its own output and each reporting roughly the nested session's own
  frame count rather than double it.

Frame *timings* from a nested session mean nothing (`docs/development.md` says
so, and says why). Nothing in this session depends on them — rect counts, line
counts and output names are all honest nested.

### Out of scope

`m_outputs` is never pruned when a screen is removed. A stale entry leaks a
handful of bytes, and a new `LogicalOutput` allocated at a freed one's address
would inherit one bogus gap reading before correcting itself. It is
diagnostic-only and needs an `outputRemoved()` hook that does not exist yet.
Leave it; note it in `## Comments` if you want it tracked.

---

## Session B — investigate: draw calls multiply by damage-region rect count

Status: resolved
Type: research

This session produces **numbers and a recommendation**, not a fix. It may well
conclude that nothing should change, and that is a good outcome — say so
explicitly and resolve it.

### The question

Both painters now clip to the region KWin is repainting, by passing it to
`GLVertexBuffer` with `hardwareClipping = true`:

- `src/flakepainter.cpp`, `vbo->draw(deviceRegion, GL_TRIANGLES, planeFirst[plane], ..., true)`
- `src/cappainter.cpp`, `vbo->render(deviceRegion, GL_TRIANGLES, true)`

That overload sets a scissor rect and issues a draw **once per rect of the
region**. The Flakes do it inside a loop over 8 depth planes, so the cost is
`8 x rects` draw calls, each resubmitting that plane's whole vertex range. The
Caps do it once per capped window, so a session with several panels and windows
multiplies again. Before the change it was 8 draws and one per Cap.

This is **required for correctness** — a single scissor over the region's
bounding rect would draw into the gaps between the rects, which is the
double-blend the change exists to stop, and ADR-0005 now records. So the
question is not whether to clip but whether the rect counts in practice are
large enough for the multiplication to cost anything.

The bounding-rect pre-filter above the draw loop in `FlakePainter::paint()` does
not help here: when the damage rects are scattered, their bounding rect is most
of the screen and every Flake survives the filter.

### What to measure

The probe already logs `repaint N.NN logical Mpx in K rects`, but that `K` comes
from `FrameProbe::requested()`, which is handed `data.paint` in
`prePaintScreen` — what *the effect asked for*, in logical pixels. The number
this session needs is the rect count of the `deviceRegion` handed to
`SnowEffect::paintScreen()`, which is what KWin settled on and what the draw
loop actually iterates. They are not the same number and the second is not
recorded anywhere.

Add it. `SnowEffect::paintScreen()` already computes `m_paintedRegion`; report
it to the probe from there. Consider making this permanent rather than temporary
instrumentation — it is the number that governs the effect's draw-call count,
which is the sort of thing the probe exists for — but that is your call to make
and to justify.

### How to measure

Nested, which is representative for this: a damage region's rect count comes out
of KWin's damage tracking of real windows and does not depend on reaching the
frame cap.

```sh
KWIN_SNOW_PROBE=1 tools/dev.sh --panel
```

Drive it as `docs/development.md` → *Driving the nested session* describes. Worth
covering, because each produces a differently shaped region:

- an idle desktop with a Panel clock ticking — the small-repaint case the
  scissor was added for;
- a terminal with a blinking cursor, plus a second window;
- a window being dragged or resized;
- several windows updating at once.

Report the distribution of the rect count, not just its maximum — a p50 and a
worst case over a few thousand frames is enough to decide.

### How to read the answer

- **Rect counts stay in the low single digits.** The multiplication is nothing;
  8 planes x 3 rects is 24 draw calls of a few hundred quads each. Recommend no
  change, record the measured numbers here, and resolve.
- **Rect counts are regularly high.** Then a mitigation is worth costing. Two
  are correct and cheap, and one that looks obvious is wrong:
  - **Correct.** Intersect the region with the geometry being drawn before the
    draw loop: for a Cap, its own band is a thin strip, so most damage rects
    miss it entirely and cost a draw for nothing. For the Flakes, intersect with
    the bounding box of the surviving Flakes.
  - **Correct.** Collapse the region per band. `KWin::Region` stores rectangles
    in bands sharing a top and bottom edge; one scissor per band's bounding rect
    still never touches a pixel outside the region's rows, though it does touch
    columns between rects within a band — so this one needs thinking through
    before it is believed.
  - **Wrong, do not do it.** Collapsing the whole region to its bounding rect
    once some rect-count threshold is passed. That reintroduces the
    double-blend, which is the bug the change fixed. If you find yourself
    reaching for it, re-read ADR-0005's paragraph on the clip.

### The human gate

Everything above an agent can do alone. A human is needed only if you conclude a
mitigation is worth making and want the before/after *frame time* confirmed:
nested frame timings are the nested compositor's own ceiling rather than a
display's, so that comparison has to happen in a live session, which needs a
logout and login. The scenario that motivated the original scissor fix — KWin's
shake-cursor plugin drawing an oversized pointer while the mouse moves fast,
asking for small repaints by the hundred — also needs real pointer movement.

If it comes to that, set this session's `Status:` to `ready-for-human`, write
down exactly which numbers you want and the recipe for getting them, and stop.

---

## Session C — split `waitAfterFrame()` so the bug it caused cannot be written again

Status: resolved

The largest of the four, and the only one that changes shipped behaviour's
shape rather than its content. It shares no files with A, B or D.

### What is wrong

The frame-clock fix reviewed here was this, in `FrameClock::frameRendered()`:

```cpp
schedule.pacer.waitAfterFrame(schedule.decided, std::exchange(schedule.animating, false));

// ...but the wait for the next frame is measured from now, which is when
// the waiting actually starts.
arm(now);
```

`schedule.decided` is the moment `isDue()` was asked, in `prePaintScreen`.
Before the fix, `now` was passed — the `postPaintScreen` moment, a few
milliseconds later. `FramePacer::waitAfterFrame()` re-asks `isDue()` against
whatever moment it is given, so a schedule that came due *while the frame was
rendering* answered yes about a frame the snow had stood still in, spent itself
on it, and — advancing by a whole number of refresh periods — landed the next
due moment inside the render window of the frame two later and did it again. A
stable lock-out, measured at anything between one skipped step and one step in
thirty seconds depending only on where the phase started.

The fix is correct. The problem is that it is a fix by convention: nothing stops
the next reader passing `now` again, and nothing tests that it does not.

That is a consequence of `waitAfterFrame(now, animated)` doing two jobs against
one timestamp:

1. **spending the schedule**, which is about the moment the frame was *decided*; and
2. **answering how long to wait next**, which is about the moment the waiting
   *starts*.

Those are two different moments, and the signature admits only one. The comment
block above the call exists entirely to explain that the code passes the first
and then works around not having passed the second.

### Why this is worth the rewrite

`waitAfterFrame()`'s return value is **dead in production**.
`FrameClock::frameRendered()` discards it and calls `arm(now)`, which re-derives
the wait by asking `waitUntilDue()` of every schedule and taking the earliest —
it has to, because one timer serves every output. So the second job that forced
the single timestamp is not a job anything needs doing there.

The only consumers of the return value are the 26 call sites in
`tests/framepacertest.cpp`. The API's shape was being held in place by its tests
alone, and it was the shape that let the bug through.

### The change

Replace `waitAfterFrame()` with a method that only spends the schedule:

```cpp
void spendOn(Clock::time_point decided, bool animated = false);
```

`waitUntilDue(Clock::time_point now)` already exists, is already public and is
already tested; it becomes the only way to ask for a wait. `FrameClock` then
reads:

```cpp
schedule.pacer.spendOn(schedule.decided, std::exchange(schedule.animating, false));
arm(now);
```

and the workaround comment goes away, because there is no longer a second
timestamp being not-passed. The two moments are named by the two calls.

`spendOn` because *spend* is already this codebase's word for the operation:
`FrameClock::isDue()` says "the claim is spent whether or not the schedule
needed it", `frameRendered()` says "a frame the schedule has to spend", and the
test is called `aFrameThatWasNotDueDoesNotSpendTheSchedule`. Pick a different
name if you have a better one, but keep it in that vocabulary and say why under
`## Comments`.

### A design that looks better and is not

Having the pacer remember the moment itself — `isDue()` records `now`, and
`spend(animated)` takes no timestamp at all — would make the wrong moment
impossible to pass rather than merely hard to. **It does not work here.**
`FrameClock::isDue()` short-circuits:

```cpp
schedule.animating = claimed || schedule.pacer.isDue(now);
```

On a claimed frame — one the clock hands over because its own repaint request
came back, which is the common case — `FramePacer::isDue()` is never called, so
the pacer would never learn the moment for exactly the frames that most need
spending. It would also make the pure, unit-testable half of the design carry
hidden state that a query mutates.

Keep the timestamp explicit. Record this paragraph's reasoning in the header so
the next person does not rediscover it.

### The test rewrite

Every assertion of the form

```cpp
QCOMPARE(pacer.waitAfterFrame(t), expected);
```

becomes

```cpp
pacer.spendOn(t);
QCOMPARE(pacer.waitUntilDue(t), expected);
```

and every bare `pacer.waitAfterFrame(t);` becomes `pacer.spendOn(t);`. Two need
a moment's thought rather than the pattern:

- `aRunOfLateFramesDoesNotDriftOffTheCap()` advances its clock inside the loop:
  `now += pacer.waitAfterFrame(now) + 5ms;` becomes a `spendOn(now)` followed by
  `now += pacer.waitUntilDue(now) + 5ms;`.
- `aClaimedFrameSpendsTheScheduleToo()` already brackets its call with a
  `waitUntilDue()` on either side; make sure the before-and-after reading still
  says what the comment claims.

This is mechanical, but do not let it be thoughtless: several of these tests are
the only written record of why the pacing works the way it does, and the
comments matter more than the assertions. Read each one before changing it, and
leave its prose alone unless the change makes it untrue.

### What this earns

`aFrameThatWasNotDueDoesNotSpendTheSchedule` becomes a test of the whole rule
rather than of half of it. Today it exercises `FramePacer`, which was never
wrong — handed a not-due moment it has always declined to advance — while the
defect and the fix live in which timestamp `FrameClock` passes, which nothing
covers. After the split there is no second timestamp for `FrameClock` to pass,
so the test pins the rule end to end.

That is what makes two existing claims true, which today they are not:

- `docs/adr/0005-one-frame-schedule-per-output.md`: "`FrameClock` keeps that
  moment in `Schedule::decided`, and `aFrameThatWasNotDueDoesNotSpendTheSchedule`
  pins it."
- `.scratch/snow-effect/issues/13-a-request-that-misses-its-frame.md`, under
  *What this is not*: "Fixed by `Schedule::decided`; pinned by
  `aFrameThatWasNotDueDoesNotSpendTheSchedule`."

Leave both claims standing, and update the surrounding prose in each to describe
the new API. Do not weaken ADR-0005's account of the bug itself — that account
is accurate and hard-won.

### Every reference to update

`waitAfterFrame` appears in nine files. All of them:

| File | What is there |
| --- | --- |
| `src/framepacer.h` | the declaration, and a long doc comment |
| `src/framepacer.cpp` | the definition |
| `src/frameclock.cpp` | the call, plus the workaround comment above it |
| `src/frameclock.h` | `Schedule::decided`'s doc comment, and `frameRendered()`'s |
| `tests/framepacertest.cpp` | 26 call sites |
| `docs/adr/0005-one-frame-schedule-per-output.md` | the paragraph on spending against the decided moment |
| `.scratch/snow-effect/issues/13-a-request-that-misses-its-frame.md` | *What this is not*, first bullet |
| `.scratch/snow-effect/issues/13-pacing-sim.py` | a comment quoting the method's body in the model's header |

`waitAfterFrame()`'s doc comment in `src/framepacer.h` carries explanation that
must not be lost — why a frame the snow moved in spends the schedule even when
it was not due, and why a compositor that cannot keep up simply animates slower
rather than being caught up on. Split it between `spendOn()` and
`waitUntilDue()` rather than dropping either half.

`13-pacing-sim.py` is the offline replay ticket 13 is built on and is still
open; its header comment quotes the old method body as the model it implements.
Update the comment. Check whether the model's arithmetic needs anything beyond
that — it should not, since the behaviour is unchanged — and say either way
under `## Comments`, because ticket 13's next session will start from that
script.

### How to verify

`cmake --build build && ctest --test-dir build --output-on-failure`. All seven
suites, and `framepacer` in particular, must pass **unchanged in what they
assert**: this is a refactor, and a test whose expected value had to move is a
signal that the behaviour moved with it. If one does, stop and work out why
before changing the number.

Then confirm the effect still paces in the nested session
(`docs/development.md` → *Primary loop: nested compositor*):

```sh
KWIN_SNOW_PROBE=1 tools/dev.sh --panel
```

Frame *timings* from a nested session mean nothing — it never reaches the cap to
begin with, and `docs/development.md` says so. What is worth looking at is that
the snow moves continuously rather than in the stalling pattern the original bug
produced.

## Session D — three nits

Status: resolved

Three unrelated small corrections, grouped because each is a few lines. Do all
three; they are independent of one another and of the other sessions, except
that D2 should land before Session B starts.

### D1 — the probe's claimed cost is not its actual cost

`src/snoweffect.h`, the doc comment on the `m_probe` member, says the probe is
kept as a member rather than compiled in and out because "the cost of carrying
it is a bool test per frame". That is not quite true. `FrameProbe`'s methods all
return immediately unless `KWIN_SNOW_PROBE` was set, but the *arguments* are
evaluated at the call site regardless, and `SnowEffect::prePaintScreen()` passes:

```cpp
FramePacer::intervalFor(m_settings.frameRateCap, data.screen->refreshRate())
```

— a virtual call and a handful of floating-point operations including a
`std::llround` and a `std::round`, on every frame of every output, for a
diagnostic that is off.

The cost is tiny in absolute terms. Fix it anyway, because the comment is making
a promise about cost and the promise is worth keeping rather than downgrading:
change `FrameProbe::beginFrame()` to take the frame-rate cap instead of the
interval, and compute `FramePacer::intervalFor(cap, output->refreshRate())`
inside, after the `isEnabled()` guard. That makes the comment true and shortens
the call site. `FrameProbe` will need `#include "framepacer.h"`; it is in the
same target, so that is free. Update `beginFrame()`'s doc comment, which
currently describes the parameter as an interval.

Leave the other two probe inputs alone. `++m_translucentWindows` in
`prePaintWindow()` and `m_groundCapDrawn = true` in `paintWindow()` are
increments on branches that were already being taken, and they cost nothing
worth removing.

### D2 — an empty repaint region means "draw nothing", not "clip nothing"

`src/flakepainter.cpp`, near the top of `paint()`:

```cpp
const KWin::Rect deviceClip = deviceRegion.boundingRect();
const bool clipped = !deviceClip.isEmpty();
```

and the `visible` lambda below returns `!clipped || logicalClip.contains(...)`.
So an *empty* region falls through to "cull nothing" and builds every Flake's
vertices — and then draws none of them, because the scissored draw over an empty
region emits nothing. The result is correct and the work is wasted. It is also
backwards as a statement of intent: an empty region is the one case where
nothing can possibly be drawn.

Replace it with an early return, beside the two that are already there:

```cpp
if (deviceRegion.isEmpty()) {
    return;
}
```

`clipped` then goes away and `visible` becomes `logicalClip.contains(flake.x, flake.y)`.

`src/cappainter.cpp` has the same latent case — it builds the whole Cap's
vertices before a `render()` that would emit nothing — so give it the same early
return. Both are theoretical today, since KWin does not paint a frame with
nothing damaged; the value is that the code says what it means.

Keep the comment above the pre-filter. Its explanation of why the margin exists
and of the pre-filter being a cheap first cut rather than the real clip is still
the thing a reader needs.

### D3 — `frameprobe.cpp` is out of order in the source list

`src/CMakeLists.txt` lists `target_sources` alphabetically. `frameprobe.cpp` was
added between `frameclock.cpp` and `framepacer.cpp`; it belongs after
`framepacer.cpp`. One-line move.

### How to verify

`cmake --build build && ctest --test-dir build --output-on-failure` — all seven
suites, unchanged. D1 and D2 have no test coverage of their own (neither
`FrameProbe` nor `FlakePainter` can be unit-tested without a compositor), so a
clean build and a nested session that still snows is the check:

```sh
tools/dev.sh --panel
```

## Comments

### Session A — done

`lastTranslucentWindows` and `Window window` now live in `FrameProbe::Output`.
`struct Window` had to move above `struct Output` to be a member of it;
`summarise()` takes the output it is summarising and names it in the line;
`m_current` is set beside `m_output` in `beginFrame()` and cleared with it in
`endFrame()`, which also merged the null check into the `isEnabled()` guard
rather than adding a second early return. `endFrame()`'s per-frame line already
named the output and is unchanged. The class doc says "once a second per
output", and `docs/development.md`'s sample summary line gained the name (it had
to be rewrapped; the prose above it is untouched, as the brief says).

Verified nested, `KWIN_SNOW_PROBE=1 tools/dev.sh --outputs 2 --panel`, 45 s each
side of the fix:

- **Summaries, the part that reproduced.** Before: 39 lines in 40 s — one a
  second for the pair, unnamed, each reporting ~58 frames. After: 76 lines, 38
  per output, each naming its own output and reporting ~30 frames, which is what
  a nested output actually produces. That was the bug and it is gone.
- **The translucent thrash did not reproduce**, and the reason is worth writing
  down because it narrows where the flat member could bite. KWin calls
  `prePaintWindow()` for the Panel in *both* outputs' views, so
  `m_translucentWindows` came out the same on both and the flat member had
  nothing to alternate between: the before log has one `translucent windows
  0 -> 1` line, not a pair per frame. After the fix there are two, one per
  output, at the moment `plasmashell` came up — which is the right reading of
  the same event and also confirms each output now keeps its own count. Whether
  a real session with genuinely different per-output window sets produces the
  per-frame thrash the brief predicted is untested; the fix is correct either
  way, and the summary mixing alone justified it.

Frame *timings* above are the nested compositor's own and mean nothing; the
counts, the names and the line counts are honest nested.

`ctest` — all seven suites pass, unchanged. Nothing here has test coverage
(`FrameProbe` takes a `KWin::LogicalOutput *`), so the nested session is the
check.

Still open, as the brief's *Out of scope* said: `m_outputs` is never pruned when
a screen is removed. Worth a ticket if an `outputRemoved()` hook ever lands —
today it leaks a handful of diagnostic-only bytes and, at worst, gives a
recycled `LogicalOutput` address one bogus gap reading. The fix moves *more*
state under that key (a `Window` per output), so the leak is a little larger and
a recycled address would also inherit a stale summary window; both are still
diagnostic-only and still only with `KWIN_SNOW_PROBE` set.

### Session D — done

All three landed; nothing in the brief turned out to be wrong on contact.

**D1.** `FrameProbe::beginFrame()` now takes `int frameRateCap` and works the
interval out itself, from the `refreshRate` it was already reading for the
frame budget, so the whole of `FramePacer::intervalFor()` sits behind the
`isEnabled()` guard along with everything else. `frameprobe.cpp` gained
`#include "framepacer.h"`; `snoweffect.cpp` still uses `FramePacer` elsewhere,
so nothing there had to change but the call, which is now one line. The
parameter's doc comment says what it is and why the probe rather than the
caller does the arithmetic. `snoweffect.h`'s claim about "a bool test per
frame" is now true, and is left standing as the brief intends.

**D2.** Both painters return early on an empty `deviceRegion`. In
`FlakePainter::paint()` the `clipped` flag and the conditional `logicalClip`
went with it — `logicalClip` is now built unconditionally, and `visible` is
`logicalClip.contains(flake.x, flake.y)`. The pre-filter's comment is
untouched. `CapPainter::paint()` got the same return beside its existing
three.

**D3.** `frameprobe.cpp` moved below `framepacer.cpp` in `src/CMakeLists.txt`.

Verified: clean build, and `ctest` passes all seven suites unchanged — no test
touches any of this (`FrameProbe` needs a `KWin::LogicalOutput *` and neither
painter can be driven without a compositor), so the nested session is the real
check.

`KWIN_SNOW_PROBE=1 tools/dev.sh --panel`, 40 s: the snow falls and piles up —
the ground Snowline climbs from 0 to a sum of 134 over the run and the Panel
registers as a Catcher — and the probe still reports against the right number,
`snow stood still for 66.0 ms of a 33.3 ms step`. That 33.3 ms is the cap of 30
snapped to two refresh periods of the nested 60 Hz output, which is what
`intervalFor()` returned when the call site computed it, so D1 moved the
arithmetic without changing its answer. Summaries read ~30 frames, ~30 stepped,
ground Cap in every frame.

Frame *timings* in those lines are the nested compositor's own and mean
nothing; the step interval, the frame counts and the Snowline sums are honest
nested.

D2's empty-region returns are still unexercised, as the brief said they would
be: KWin does not hand the effect a frame with nothing damaged, so neither
return fired. They are there because the code should say that an empty region
means draw nothing, not clip nothing.

### Session B — measured, and one number a human has to fetch

The instrumentation landed and is permanent; the measurement ran; the answer is
*it depends on one thing the nested session cannot show*, so this ends at the
human gate the brief provided for, with the recipe below.

**What landed.** `FrameProbe::painted(const KWin::Region &)`, called from
`SnowEffect::paintScreen()` with `m_paintedRegion`, records that region's rect
count. Permanent rather than temporary, for the reason the brief offered: it is
the one input to this effect's draw-call count that nothing else records, and
the number already in the log -- `requested()`'s, from `data.paint` -- cannot
stand in for it, being the effect's own ask, in logical pixels, before KWin
unions it with everybody else's damage. The per-frame line now reads `repaint
N.NN logical Mpx asked in K rects, painted in N rects`, and the summary carries
a median and a worst over the second, from a bucket-per-count histogram in
`Window` rather than a mean -- a mean over a distribution that is 1 for
nineteen frames in twenty and 55 in the twentieth says nothing anybody wants.
`docs/development.md`'s worked example and its list of what the numbers mean
were updated with it.

One thing that fell out of writing it: a handful of frames per run reach
`endFrame()` without having gone through `paintScreen()` -- one or two at
startup, in every run. `m_paintedRects` is `-1` for those and they are left out
of the distribution rather than counted as a frame that painted nothing.

**The nested session cannot answer this, and that is the headline.** Under the
OpenGL backend -- `tools/dev.sh`'s default, `KWIN_COMPOSE=O2` -- the region
handed to `paintScreen()` was the *whole output, one rect, on every frame of
every run*: 1364 frames idle with a Panel, 1967 with two windows, 3175 with a
window dragged across the screen for a minute (1209 of them riding along), 1432
with three terminals printing at 10 Hz and the cap lowered to 5 fps so that four
frames in five were riding. Logging the region's bounding rect as well settled
it: 1507 of 1508 frames were exactly `0 0 3200 2000`, the output.

That is not the effect's own full-output repaints swallowing the damage. At a
cap of 5 the snow steps about once in five frames, and `DamageJournal::accumulate()`
unions only the last `bufferAge - 1` frames, so most riding frames should have
carried nothing but the terminals' text damage. They carried the output. The
remaining explanation is the one the journal's own header spells out: it returns
its `fallback` -- the whole output -- when it is handed no usable buffer age,
and the nested Wayland output layer evidently reports none. So the scissor path
that ADR-0005 exists for is never exercised nested with more than one rect: the
double-blend the change fixed cannot reproduce there, and cannot regress there
either.

**What a backend with real damage tracking shows.** `KWIN_COMPOSE=Q` -- the
QPainter backend -- does hand over the tracked damage, and the effect's own
prePaint arithmetic is backend-independent, so the region it computes is an
honest sample of what the damage looks like even though nothing is drawn there.
Same scenarios, same apps, 45 s each, nested 1600x1000 at scale 2, cap 30 except
where stated:

| Scenario | Frames | Riding | p50 | p90 | p99 | Worst | Frames over 1 rect |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Idle desktop, Panel clock | 1625 | 75 | 1 | 1 | 1 | 1 | 0 |
| Two windows + Panel | 1634 | 94 | 1 | 1 | 1 | 1 | 0 |
| A window dragged continuously | 2393 | 891 | 1 | 1 | 1 | 3 | 12 (0.5%) |
| Three terminals printing | 1956 | 417 | 1 | 1 | 1 | 55 | 9 (0.5%) |
| Three terminals printing, cap 5 | 1560 | 986 | 1 | 38 | 55 | 55 | 490 (31%) |

Stepped frames are 1 rect in every row, by construction -- a frame the snow
moves in asks for the whole output. Everything interesting is in the riding
frames, and what decides whether they carry one rect or forty is **how far apart
the steps are relative to the buffer age**. At a cap of 30 against a session
producing frames at about 35 Hz, a step falls inside nearly every frame's
accumulation window and the count is 1 at the p99. Drop the cap to 5 and the
same desktop spends a third of its frames on regions of tens of rects.

Translated into draws, with the ~4.4 Caps those runs carried: a 1-rect frame
costs 13 draw calls and the p99 of the cap-5 run costs **715** -- 440 for the
Flakes, eight planes over 55 rects, and the rest one per rect per Cap.

**How to read that.** The default cap on a 60 Hz display is the first row of the
table and needs nothing: the p99 is one rect. The cap-5 row is not a
configuration anybody is asked to use, but it is the shape of two that are.
A 30 fps cap on a 144 or 240 Hz display puts four to seven frames between steps,
and a buffer age of two or three covers only the first of them. And the case
that motivated the scissor in the first place -- the shake-cursor plugin asking
for small repaints by the hundred while the pointer moves -- is the same regime
by a different route. So the answer is neither of the brief's two clean ones: the
counts are in the low single digits *where the snow is stepping at something
near the frame rate*, and regularly in the tens where it is not.

**On the three mitigations.**

- **Intersecting the region with the geometry being drawn is the one worth
  doing, for the Caps.** A Cap's band is a strip a few dozen logical pixels tall
  across one Catcher's width; scattered damage inside a terminal's body misses
  the Panel's band and the ground's entirely. Intersecting first turns each
  Cap's cost from `rects` draws into the one or two that touch its band, and
  when the intersection comes out empty the early return Session D added already
  does the rest. On the cap-5 numbers that is most of the 275 Cap draws of a
  worst frame.
- **The same trick does little for the Flakes.** Their bounding box is the
  screen -- snow falls everywhere -- so intersecting with it leaves the 440
  standing. What would work there is finer: KWin's region rects do not overlap,
  so a pass over the population could record which planes have a Flake in each
  rect and the loop could skip the (rect, plane) pairs that are empty, which on
  small scattered damage is nearly all of them. It is a real change to the
  painter and should not be made on these numbers alone.
- **Collapsing per band is wrong, and I do not think it is a close call.**
  `KWin::Region` keeps rects in bands sharing a top and bottom edge, so a
  band's bounding rect stays inside the region's *rows* -- but it covers the
  columns between the rects of that band, and those pixels are outside the
  damage. Outside the damage the buffer already holds the previous frame's snow,
  which is exactly the premise ADR-0005 rests on, so drawing there lands the
  second premultiplied `over` the scissor exists to prevent. It is the
  bounding-rect bug with a smaller blast radius, not a different one. Rejected.

### What is wanted from a human

One live measurement, because the nested session cannot produce it and every
recommendation above turns on it.

```sh
echo 'export KWIN_SNOW_PROBE=1' > ~/.config/plasma-workspace/env/kwin-snow-probe.sh
chmod +x ~/.config/plasma-workspace/env/kwin-snow-probe.sh
```

Log out and back in -- which the rebuilt `snow.so` needs anyway -- then use the
desktop normally for a few minutes, including a window drag, a video or a
scrolling terminal, and a stretch of fast pointer movement over a window (the
shake-cursor case). Then:

```sh
journalctl --user --since "-10 min" | grep " s -- " | grep -o "painted in .*worst"
```

**The numbers wanted** are the `painted in N rect median, M worst` clause of the
per-second summary lines: the median across those lines, and how often the worst
runs into double figures. That is the whole question. If the medians are 1 and
the worsts stay in single figures, this resolves as *no change*, the scissor
stays as it is, and the measurement is already in the probe for the next person
who wonders. If the worsts are regularly in the tens, the Cap intersection above
is worth a ticket, and a before/after frame time for it has to come from the
same live session for the reason the brief gives.

Worth knowing while reading them: this laptop's panel is 60 Hz and the cap
defaults to 30, which is the row of the table that showed nothing. A 60 Hz
reading that comes back all ones does not clear the effect on a 144 Hz display;
it only says the common case is fine. If a high-refresh output is available at
all, it is the one to read.

### Session B — live results received, resolved with no painter change (2026-09-20)

The user supplied the live session's filtered probe summaries requested above:

- Every summary reports a median of **1 rect**; the median of those medians
  is therefore **1**.
- All but three summaries report a worst of **1 rect**. The exceptions are
  **23**, **5**, and **6**, each appearing once.
- Only **one summary window** reaches double figures; the overall observed
  maximum is **23 rects**.

These are per-output summary-window statistics, not individual frame samples.
The 23-rect maximum does not tell us whether one frame or several in that
window reached it, and the filtered output cannot establish a frame-level
spike percentage or a frame-time cost. It does establish that high rect counts
were confined to one summary window in this sample, while the typical count
remained 1 throughout.

**Recommendation: no painter change.** The isolated 23-rect maximum does not
meet the brief's criterion of regularly high counts. Keep the existing exact
region scissor and permanent probe instrumentation; these results do not
justify a Cap-intersection optimisation ticket or a change to the Flake draw
loop. Session B's live-measurement gate is satisfied and its status is resolved.

This conclusion applies to the tested setup and activity. The supplied clauses
omit output names, refresh rates, the configured cap, and scenario labels, so
they do not establish coverage of a high-refresh display or every scenario in
the recipe. Revisit if another configuration produces regularly high counts.
Only this issue record was updated; no source changes or new runtime tests were
needed to interpret the user's measurement.

### Session C — done (2026-09-20)

Replaced `waitAfterFrame()` with `void spendOn(decided, animated)`, retaining
the brief's name because spending is already the operation's vocabulary.
`FrameClock::frameRendered()` now spends against `Schedule::decided` and calls
`arm(now)` separately; the workaround comment is gone. `waitUntilDue()` is the
only wait query. The header preserves the reasons claimed frames spend the
schedule and slow compositors do not catch up missed frames, and explains why
remembering the decision inside `isDue()` would fail on short-circuited claims.
Updated both FrameClock comments, ADR-0005 and ticket 13 around that split.

All 26 test call sites now use the split operations, including the late-frame
loop and the claimed-frame before/after wait. All 50 assertions retain their
original expectations and the test prose is unchanged. The existing build had
`BUILD_TESTING=OFF`, so the first ctest invocation found stale binaries; enabled
testing with `cmake -S . -B build -DBUILD_TESTING=ON`, rebuilt the tests, then
verified all seven suites pass, including the rebuilt `framepacer` suite.

The replay needs **no arithmetic change**: it already spends against `decided`
and arms from `post` separately. Only its header's quoted method changed.
Compared the executable Python AST against HEAD (identical) and ran the replay:
the current model still reports 29.55 fps and 5.93%, 10.70%, 20.08% off cadence
at its three jitter settings, with the same aiming histogram. Ticket 13's
remaining investigation and status are unchanged.

Ran `KWIN_SNOW_PROBE=1 tools/dev.sh --no-build --panel` for 45 seconds against
the rebuilt plugin staged under `build/session-c-stage`, using an isolated
`DEV_HOME`. The probe recorded 43 summary windows, 1297 frames and 1286 steps,
with 29–32 steps in every window: continuous stepping with no sustained
lock-out. The widest present and step gaps were both 83 ms. This is a runtime
probe check, not a visual smoothness assessment or a live cap measurement;
nested timings retain the limitations described in `docs/development.md`.
The run ended at its planned timeout. Log: `build/session-c-nested.log`.
