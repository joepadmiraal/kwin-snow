# A repaint request that misses the frame it was meant for

Status: ready-for-human

On a still desktop the effect's own frames are the only frames there are, and a
frame exists only if its repaint request reached KWin before KWin began
preparing it. A request a moment late goes into the frame after — a whole
refresh period — so a 33.3 ms step becomes 50 ms, followed by a 17 ms one as the
schedule takes the lateness back. The average rate holds; the instantaneous
cadence jitters by a refresh period either way.

`ready-for-human` rather than `ready-for-agent` because the first step is a
judgement call in front of a real screen (see *Gate the work on this*), and
because the mechanism is not yet understood — see *Where it goes wrong, and
where I stopped*.

## What this is not

Two things were found alongside this and are **already fixed**. Do not go
looking for them again.

- **The lock-out.** `FrameClock` used to spend a schedule on a frame the snow
  stood still in, because spending a schedule re-asks `isDue()` and
  `frameRendered()` handed it the postPaint clock rather than the prePaint one
  the frame was decided on. Since it advances by a whole number of refresh
  periods, the next due moment landed in the render window of the frame two
  later and it repeated — measured at anything between one skipped step and *one
  step in thirty seconds*, decided by nothing but the starting phase. Fixed by
  `Schedule::decided`; pinned by `aFrameThatWasNotDueDoesNotSpendTheSchedule`;
  recorded in [ADR-0005](../../../docs/adr/0005-one-frame-schedule-per-output.md).
  Spending now uses `FramePacer::spendOn(decided)` alone; `arm(now)` separately
  asks `waitUntilDue(now)` from the moment waiting starts.
- **A false positive in the probe.** `journal_no_showfps.log` shows `29 reported`
  in nearly every second. That was the probe judging the present gap against a
  refresh period, which is only meaningful when something else is driving the
  compositor. It now judges against the pacing interval.

## Evidence

`journal_no_showfps.log` (untracked, at the repo root when this was written —
move it next to this ticket if it is worth keeping), a still desktop with
nothing but the snow asking for frames:

```
Snow probe 1.00 s -- 30 frames, 30 stepped, …; widest present gap 50.0 ms,
    widest step gap 50.0 ms; repainted 69.1 logical Mpx
```

Every second, 30 frames and 30 steps — the rate is right — and a widest gap of
50 ms, which is three refresh periods where the cap asked for two.

Replayed offline (`13-pacing-sim.py`, 20 starting phases, 40 s each, 2 s of
warmup discarded):

| margin jitter | rate | steps off cadence |
| --- | --- | --- |
| ±1.0 ms | 29.55 fps | 5.93 % |
| ±2.5 ms | 29.55 fps | 10.70 % |
| ±5.0 ms | 29.55 fps | 20.08 % |

"Off cadence" is a 50.0 ms or 16.7 ms step where 33.3 was asked for.

## Why it happens

KWin starts preparing the frame for vblank *v* at `v − margin`, where the margin
grows with the measured render time (`RenderLoopPrivate` uses roughly
`2 × predictedRenderTime + safetyMargin`). `FrameClock`'s timer fires, calls
`addRepaintFull()`, and KWin puts that repaint on the first vblank it has not
already started. Land after `v − margin` and the frame is a period later than
intended.

The effect never aims at that deadline. Two things decide when it asks:

1. `FramePacer::waitUntilDue()`, which counts to the moment the frame is *due*,
   not to the moment a request for it has to be in; and
2. the `m_lastRequest` floor in `FrameClock::arm()`, which in practice is the
   one that binds. It is documented as a backstop — it stops an output that is
   not rendering at all from turning the clock into a spin — but it applies on
   every frame, and it schedules the next request exactly one interval after the
   previous one with no regard for where the vblank grid is.

## Where it goes wrong, and where I stopped

Four variants were built and measured. **None is worth implementing as it
stands**, and the last result is the one to pick up.

| variant | rate | off cadence (±2.5 ms) |
| --- | --- | --- |
| current | 29.55 fps | 10.70 % |
| subtract a measured lead from the timer wait | 29.55 fps | 10.70 % — *identical* |
| schedule kept in the `presentTime` timebase, + lead | 30.02 fps | 11.57 % |
| aim at the middle of the eligible window, lead from a decayed high-water mark | 30.02 fps | 12.83 % |
| …lead from a moving average | 30.02 fps | 11.57 % |
| …lead read from `RenderLoop::predictedRenderTime()` | 30.00 fps | 11.24 % |

Subtracting a lead changes nothing because the `m_lastRequest` floor overrides
it — that is finding (2) above, and it has to be dealt with before any aiming
strategy can do anything at all.

The aiming variants get the *rate* right (30.0 fps against 29.55) and make the
*cadence* worse, trading 50 ms gaps for an equal number of 16.7 ms ones.

**The unexplained part.** There is exactly one refresh period of room in which a
request still lands on the wanted vblank: later than `(v − period) − margin` and
no later than `v − margin`. Aiming at the middle of it should leave ±8.3 ms of
slack and absorb ±2.5 ms of jitter completely. Instrumenting where requests
actually land gives a roughly **uniform** spread across the whole window and past
both ends of it, not a peak at the 8.3 ms being aimed at:

```
(deadline - request), ms:   0: 660   1:1383   2:1355   3:1389   4:1323   5:1387
                            6:1389   7:1372   8:1386   9:1394  10:1425  11:1365
                           12:1370  13:1311  14:1214  15:1027  16: 836  17: 591
                           18: 328  19: 192  20:  78  21:  23
```

(`python3 13-pacing-sim.py` prints exactly this, and the table above it.)

Either the aim is being overridden somewhere between `arm()`'s two call sites
per frame (`fire()` re-arms with the backstop floor, then `arm(post)` re-arms
again), or the replay model is wrong. **Start by explaining that histogram.**
Everything else is downstream of it.

## Gate the work on this

Check first whether the residual is visible at all, now that the lock-out is
gone. The motion is integrated from `presentTime` deltas
(`SnowfallRegistry::advance`), so every Flake is drawn at the position it should
occupy at the moment that frame is shown. What varies is the *sampling*, not the
position — a 50 ms step moves a Flake 50 ms worth and is shown 50 ms later. That
is much milder than a position error, and it may simply not be worth a redesign
of the clock.

If it is not visible: close this and say so. If it is, the rest follows.

## Leads

- **`RenderLoop` is reachable.** `LogicalOutput::backendOutput()->renderLoop()`
  gives `predictedRenderTime()`, `nextPresentationTimestamp()` and
  `lastPresentationTimestamp()` — public KWin headers, and the plugin already
  links `KWin::kwin`. That turns the margin from something to estimate into
  something to read. It is the single most promising lead. Note it is a
  `BackendOutput` API, so check what it does under a nested session and with an
  output that has no render loop.
- **Demote the `m_lastRequest` floor** in `FrameClock::arm()` to what its comment
  says it is: a backstop for a request that never came back as a frame. Track
  that explicitly (set a flag in `requestFrame()`, clear it in `frameRendered()`)
  rather than applying the floor unconditionally. The cap is already carried by
  the schedule advancing an interval per step.
- **There is no phase to lose at `cap == refreshRate`**: `intervalFor()` returns
  one refresh period and every frame is a step. Worth knowing as the escape
  hatch, and worth mentioning in the KCM's help text whatever else happens.

## Constraints

- **Do not raise the frame rate to fix the cadence.** The cap is the effect's
  whole power story — ADR-0005, and ticket 08. Asking for more frames than the
  cap, even briefly, is not a trade this effect gets to make.
- `FramePacer` stays free of KWin and Qt and stays unit-tested. Anything that
  needs a compositor belongs in `FrameClock`, which is not testable here.
- Whatever lands needs a regression test in the shape of
  `aFrameThatWasNotDueDoesNotSpendTheSchedule` — a named scenario with the
  reasoning in the comment, not a number.

## How to measure

The probe is in `src/frameprobe.{h,cpp}`; `docs/development.md` → *Watching
frames* has the recipe. In one line: put `KWIN_SNOW_PROBE=1` in
`~/.config/plasma-workspace/env/`, log out and in, and read
`journalctl --user -f | grep "Snow probe"`. The number that matters here is
**widest present gap** on a still desktop with nothing else painting; it should
be the pacing interval and is currently up to a refresh period more.

The offline replay is `13-pacing-sim.py` beside this file. It models KWin's
render loop — a frame for vblank *v* exists only if a repaint was asked for
before `v − margin` — and runs the real `FrameClock`/`FramePacer` arithmetic
against it, sweeping the starting phase. It is how every number in this ticket
was produced, and it is fast enough to iterate against before touching the
compositor.

## Comments
