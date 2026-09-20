#!/usr/bin/env python3
"""
Offline replay of FrameClock + FramePacer against a model of KWin's render loop.

For ticket 13. The case being modelled is a still desktop: the effect's own
frames are the only frames there are, so a frame for vblank `v` exists only if a
repaint was asked for before KWin began preparing it, at `v - margin`. A request
that lands a moment late goes into the frame after -- a whole refresh period.

The arithmetic below is the real thing, transcribed:

  FramePacer::isDue          due <= now + slack
  FramePacer::waitUntilDue   floor((due - now) / 1 ms)
  FramePacer::spendOn        if (animated || isDue(decided)) due = max(due + interval, decided)
  FrameClock::arm            max(waitUntilDue, ceil(interval - sinceRequest))
  FrameClock::isDue          claimed || pacer.isDue(now)

with the fix already in `Schedule::decided`: the schedule is advanced against
the moment the frame was *decided* (prePaint), never against the moment it
finished rendering (postPaint). Setting DECIDED_FIX = False brings back the
lock-out this ticket's "What this is not" section describes, which is worth
seeing once.

Usage:  python3 13-pacing-sim.py
"""

import random
from collections import Counter

MS = 1_000_000
PERIOD = round(1e12 / 59990)          # one refresh of the panel this was found on
INTERVAL = 2 * PERIOD                 # what intervalFor(30, 59990) returns
SLACK = min(1 * MS, INTERVAL // 8)    # FramePacer::slack()
SAFETY = 1.8 * MS                     # KWin's presentation safety margin, guessed
RENDER = 4 * MS                       # prePaint -> postPaint, from the probe's logs

DECIDED_FIX = True

# Which moment the timer aims at:
#   "due"      the frame's due moment            -- what the code does today
#   "window"   the middle of the span in which a request still lands on the
#              wanted vblank, with the margin estimated by `lead`
AIM = "due"

# How the render-ahead margin is estimated when AIM == "window":
#   "hi"    decayed high-water mark of the measured (presentTime - prePaint now)
#   "ema"   moving average of the same
#   "loop"  KWin's own number, as RenderLoop::predictedRenderTime() would give it
LEAD = "loop"

# Whether the m_lastRequest floor in arm() applies on every frame (what the code
# does today) or only when the last request never came back as a frame.
FLOOR_ALWAYS = True


def replay(phase_ns, seconds=40.0, jitter_ms=2.5, seed=7, warmup_s=2.0):
    """Replay one starting phase; the module-level AIM / LEAD / FLOOR_ALWAYS /
    DECIDED_FIX switches say which variant. Returns the gaps between steps in
    milliseconds, where each request landed, and how many steps there were."""
    rnd = random.Random(seed)
    vblanks = int(seconds * 1e9 / PERIOD)

    due = 0
    claimable = False
    animating = False
    last_request = -10**15
    timer_at = 0
    pending = None            # a repaint KWin has been asked for, not yet spent
    unanswered = False
    ema_render = RENDER
    lead_hi = 0.0
    lead_ema = 0.0
    steps = []
    landing = Counter()       # (deadline - request), ms: 0..period means on target

    def lead_for(kwin_margin):
        return {"hi": lead_hi, "ema": lead_ema, "loop": kwin_margin}[LEAD]

    def arm(now, kwin_margin):
        nonlocal timer_at
        if AIM == "window":
            target = due - lead_for(kwin_margin) - PERIOD / 2
            wait = 0 if target <= now else int(target - now) // MS
        else:
            wait = 0 if due <= now else (due - now) // MS
        if FLOOR_ALWAYS or unanswered:
            since = now - last_request
            if since < INTERVAL:
                wait = max(wait, -(-(INTERVAL - since) // MS))   # ceil to ms
        timer_at = now + wait * MS

    def fire(now, kwin_margin):
        nonlocal last_request, claimable, pending, unanswered
        last_request = now
        claimable = True
        unanswered = True
        if pending is None:
            pending = now        # KWin coalesces further requests into this frame
        arm(now, kwin_margin)

    for k in range(1, vblanks + 1):
        v = phase_ns + k * PERIOD
        base = min(2 * ema_render, 2 * PERIOD) + SAFETY      # what the effect could read
        margin = base + (rnd.uniform(-jitter_ms, jitter_ms) * MS if jitter_ms else 0)
        pre = v - margin                                     # KWin starts the frame here

        while timer_at is not None and timer_at <= pre:
            fire(timer_at, base)

        if pending is None or pending > pre:
            continue                                         # no frame for this vblank

        request, pending, unanswered = pending, None, False
        if v - phase_ns > warmup_s * 1e9:
            landing[round((pre - request) / MS)] += 1

        lead_hi = max(v - pre, lead_hi * 0.98)
        lead_ema = 0.9 * lead_ema + 0.1 * (v - pre) if lead_ema else (v - pre)

        # --- prePaintScreen ---
        decided = v if AIM == "window" else pre
        claimed, claimable = claimable, False
        animating = claimed or (due <= decided + SLACK)
        if animating:
            steps.append(v)

        render = RENDER * (rnd.uniform(0.8, 1.3) if jitter_ms else 1.0)
        post = pre + render
        while timer_at is not None and timer_at <= post:
            fire(timer_at, base)

        # --- postPaintScreen ---
        book = decided if DECIDED_FIX else post
        if animating or (due <= book + SLACK):
            due = max(due + INTERVAL, book)
        animating = False
        ema_render = 0.7 * ema_render + 0.3 * render
        arm(post, base)

    kept = [s for s in steps if s - phase_ns > warmup_s * 1e9]
    gaps = [round((b - a) / MS, 1) for a, b in zip(kept, kept[1:])]
    return gaps, landing, len(kept)


def sweep(phases=20, **kw):
    gaps, landing, steps = Counter(), Counter(), 0
    for p in range(phases):
        g, l, n = replay(p * PERIOD // phases, **kw)
        gaps.update(g)
        landing.update(l)
        steps += n
    return gaps, landing, steps


def report(label, jitters=(1.0, 2.5, 5.0)):
    seconds = 20 * 38.0
    for jit in jitters:
        gaps, _, steps = sweep(jitter_ms=jit)
        off = sum(n for g, n in gaps.items() if abs(g - 33.3) > 2)
        print(f"  {label:9} jitter +/-{jit} ms: {steps/seconds:5.2f} fps, "
              f"{100*off/max(1,steps):5.2f}% off cadence, "
              f"gaps {dict(sorted(gaps.items())[:5])}")


if __name__ == "__main__":
    print("cadence:")
    report("current")
    AIM, LEAD, FLOOR_ALWAYS = "window", "loop", False
    report("aim+loop")

    # The histogram the ticket asks somebody to explain. Aiming at the middle of
    # a one-period window should leave +/-8.3 ms of room and absorb +/-2.5 ms of
    # jitter outright, so this should be a narrow peak around 8 ms. It is not.
    print("\n(deadline - request) in ms, while aiming at the middle of the window;")
    print("0..16 means the request landed on the vblank it was meant for:")
    _, landing, _ = sweep(jitter_ms=2.5)
    for k in sorted(landing):
        print(f"  {k:4d} ms : {landing[k]:5d}")
