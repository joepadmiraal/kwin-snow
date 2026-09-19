# Suspension and frame rate cap

Status: ready-for-agent
Blocked by: 05

The effect forces continuous full-screen repainting whenever it runs, so this
ticket is the whole of its power story.

- Suspend entirely while a fullscreen window is active.
- Suspend while the session is locked, and while it is idle.
- Cap animation at `frameRateCap` (default 30 fps) rather than running at the
  display's refresh rate.
- On resume, continue from the existing Snowlines rather than resetting.

Explicitly **not** suspending on battery power: silently stopping when a laptop
is unplugged makes the effect look broken, and the frame cap already addresses
most of the cost.
## Comments

Implemented across `src/suspension.{h,cpp}` (when nobody is looking, and what
that stops), `src/framepacer.{h,cpp}` (when to ask for the next frame),
`src/snoweffect.{h,cpp}` (a frame timer in place of the `addRepaintFull()` that
used to end every frame), `src/settings.h` (`frameRateCap`, and what the number
means) and `src/snowfallregistry.cpp` (the longest step the simulation will take
now follows the cap). `tests/framepacertest.cpp` is new, nine cases.

No new glossary terms: a suspension is not part of the model, it is the effect
deciding not to run, so CONTEXT.md is untouched.

### "Suspends entirely" and "suspends" turn out to be two different things

The spec's Behaviour table says a fullscreen window makes the effect "suspend
entirely" and a locked or idle session makes it "suspend", and this ticket
repeats the distinction. It is worth honouring:

- **A fullscreen window and a lock screen take the effect out of the frame.**
  `isActive()` returns false, so KWin leaves the effect out of every call for
  that frame. That is the right answer for both, because the Flakes are drawn
  above the whole window stack (ADR-0003): an effect that merely stopped
  animating would still be drawing snow over a film or over a lock screen, and
  a lock screen does repaint on its own — a clock ticking over is enough.
- **An idle session freezes instead.** The desktop is still what is on screen,
  and an effect that left the frame would have its snow flicker away the next
  time something unrelated repainted. That is the same "looks broken" this
  ticket refused to accept for battery power, so idle stops the animation and
  leaves the last frame standing. It costs nothing: those frames are somebody
  else's.

`Suspension::isAnimated()` and `isDrawn()` are those two answers, and
`SnowEffect::prePaintScreen()` steps nothing when the first is false — otherwise
an idle desktop would twitch the snow forward by a frame every time a Panel's
clock changed.

**Leaving the frame is worth more than not drawing.** `Effect::isActive()` is
what KWin filters `EffectsHandler::m_activeEffects` by at the start of each
frame, and that is the same list `EffectsHandler::blocksDirectScanout()` walks
(read out of libkwin 6.6.6 rather than assumed). So suspending over a
fullscreen window also stops the effect blocking KWin from scanning that
window's surface straight out to the display, which is the one power saving
here that does not go through the frame rate at all.

### The three reasons, and how each is noticed

| Reason | Read from | Told by |
| --- | --- | --- |
| The active window is fullscreen | `activeWindow()->isFullScreen()` | `windowActivated`, plus that one window's `windowFullScreenChanged` |
| The session is locked | `effects->isScreenLocked()` | `screenLockingChanged` |
| The session is idle | `KWin::IdleDetector`, 5 minutes | its own `idle` / `resumed` |

Three notes on those choices:

**"Fullscreen window active" is read as the window with the focus.** There is no
`windowFullScreenChanged` on `EffectsHandler` — it is a signal of each
`EffectWindow` — so the alternative reading, "any fullscreen window anywhere",
means a connection per window and a walk of the stacking order. The literal
reading needs one connection, re-made on each `windowActivated`, and it also
behaves better on more than one output: a fullscreen film on one screen and work
on the other gets its snow back the moment the work has the focus, which is the
moment its desktop is what is being looked at.

**The idle timeout is a constant, not a key.** Neither of the spec's
Configuration tables has one, and five minutes is late enough that nobody
watches the snow stop — Plasma's own defaults dim the screen at five minutes.
`FollowsInhibitors` is what keeps the snow falling through a film that is
watched rather than clicked through.

**`KWin::IdleDetector` registers itself with KWin's input redirection when it is
constructed**, so `Suspension` checks `KWin::input()` first and warns instead of
dereferencing null. There is no compositor without input, but a null
dereference in a plugin takes the session with it.

The order the reasons are tested in is not only for the log: the two that take
the effect out of the frame are tested first, so that a fullscreen film left
running until the session goes idle is still reported as the fullscreen window
it is. Reporting it as idle would leave the snow drawn over it.

### The cap is a timer, and the timer is paced against a schedule

`postPaintScreen()` used to end every frame with `addRepaintFull()`, which caps
nothing: the next frame then comes at the display's rate. It is now a
single-shot timer that asks for one frame per tick, started when the snow starts
and stopped when it stops — so a suspension costs a stopped timer rather than a
test inside a frame the effect asked for anyway, and the effect no longer needs
somebody else to paint a first frame before it can start.

**When the next frame is asked for is `FramePacer`, and it counts from when the
last frame was _due_ rather than from when it arrived.** A repaint request has
to reach the compositor before it starts rendering the frame it is meant for;
one that arrives a moment after that waits a whole refresh period. Measured from
the frame that arrives, every one of those misses becomes the new cadence and a
30 fps cap settles at whatever its display divides into. Measured from the
schedule, the wait after a late frame is shorter by exactly how late it was.
This is the one part of the cap that is unit-testable, and
`tests/framepacertest.cpp` is where it is tested — including that thirty frames
arriving 5 ms late each still take 30 intervals in total, which is the case a
fresh interval per frame gets wrong.

**A desktop that is already painting is never asked for a frame.** Those frames
push the schedule's own frame into the future, so while a video plays or a
window is dragged the effect asks for nothing and the snow rides along with
frames that were happening anyway. The consequence is worth stating plainly: the
cap caps what the effect *asks for*, so on a busy desktop the snow is smoother
than the cap at no extra cost, and the cap is what decides the frame rate again
the moment the desktop is still.

Two smaller things came out of the cap:

- **`frameInterval()` in `settings.h` clamps the cap to 1–240 fps**, because
  everything hangs off it: a hand-edited `frameRateCap=0` would otherwise be a
  division by zero and then a timer asking for frames as fast as the event loop
  can deliver them.
- **The longest step the simulation will take now follows the cap** — two capped
  frames, and never less than the 50 ms it was. A cap below 20 fps asks for
  steps longer than the old constant, and clamping those would have quietly
  slowed the snowfall down rather than absorbing a stall.

### On resume nothing is reset, and nothing catches up

The clock skips rather than runs: a suspended effect is not stepped, so no melt
and no snowfall happen while it is stopped and the Snowlines are exactly as they
were left. The first step after a resume is the clamped one the registry hands
out after a gap, which is a few pixels of fall that nobody can see.

Worth knowing, because it is a choice: a session locked for an hour comes back
with the snow it had an hour ago rather than a desktop that melted bare in the
meantime. Catching an hour of melt up would need an unbounded step, and
"continue from the existing Snowlines" is what the ticket asked for.

### Verified in the nested compositor

`tools/dev.sh` on a Debug build with `plasmashell` inside, driven from outside
over the nested session's D-Bus; the frame counts come from a temporary
`qCInfo` in `prePaintScreen` that has since been deleted, and the two settings
tweaks named below were reverted the same way. See docs/development.md, which
now describes all of this.

| Check | Result |
| --- | --- |
| Load into an unlocked session | `Snow falling`, snow visible, ~30 fps |
| Frame rate at the default cap | 28–31 frames a second, over a minute |
| The same session with the cap at 120 | ~24 fps — the nested compositor's own ceiling, not the display's 59 |
| A fullscreen window | `Snow suspended: a fullscreen window is active`, and then **no frames at all**: the effect is out of the frame, and screenshots show no Flake anywhere over it |
| Leaving fullscreen | `Snow falling`, and the whole screen differing between two screenshots again |
| An idle session (timeout shortened to 8 s for the run) | `Snow frozen: the session is idle`; 3–7 frames a second from other sources, all with the animation off; the Flakes still on screen and two screenshots 3 s apart **pixel-identical** |
| Resuming from idle on real input | `Snow falling`, ~30 fps again |
| Snowlines across a suspension (`meltRate` 0 for the run, for a pile deep enough to read) | Ground sum **771.561** before a 22 s fullscreen suspension and **787.51** on the first frame after it — where a running clock would have reached ~1430 and a reset would have been 0 |
| A locked session | `Snow suspended: the session is locked`, and no frames afterwards |
| Stability | No crash, no assertion, no new warnings; `ctest` green in Debug and Release |

Two things the nested session could not show:

- **Unlocking.** `SetActive false` returns `false` — unlocking means
  authenticating at the greeter — so the resume from a lock is the one
  transition not exercised. It is the same three lines the other two resumes
  run through. A nested session *inherits the live session's lock* through
  logind, which is how the locked state got verified in the shipping build
  without being asked for at all, and `-- --no-lockscreen` is how the rest of
  the table got verified while that was still true.
- **What an uncapped effect would cost.** The nested compositor tops out below
  the cap, so raising the cap changes nothing there. That the effect used to run
  at the display's refresh rate is visible in the code it replaced, not in a
  measurement here; the live session is where that number lives.

`showfps` is not usable as an instrument, which cost some time to discover:
with the Snow effect **unloaded** it still reported 33–35 fps in an otherwise
still session, because it drives repaints of its own.

### For ticket 09 and after

- **`frameRateCap` has no live path yet.** The interval is read from `Settings`
  at each resume, so a settings push is picked up at the next resume by itself;
  `reconfigure()` should call `SnowEffect::suspensionChanged()` (or whatever it
  becomes) so a changed cap takes effect at once.
- **The idle timeout is not a setting** and the spec has no key for it, so a KCM
  should not grow one without the spec growing one first.
- **Battery is deliberately not a reason**, as this ticket says: the frame cap
  is the answer to "costs too much on battery", and silently stopping when a
  cable comes out reads as a bug.

**Not done:** `Status:` left as-is — `docs/agents/triage-labels.md` still defines
no completed state.
