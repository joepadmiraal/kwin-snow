# The snow moves only in frames it asked for, and every output asks for its own

KWin calls an effect for every frame of every output, not only for the frames
the effect asked for, and a frame somebody else asked for repaints only what
they damaged. The snow was stepped in all of them, which is two bugs at once.

**A dragged window pulled a trail of Flakes behind it.** A drag repaints the
strip the window moved through and nothing else, so the Flakes drawn outside it
went into the back buffer at their new positions while the pixels of their old
ones stayed on screen — and stayed until the next frame the effect asked for
repainted the screen whole, a fraction of a second later. The region an effect
may add to is `ScreenPrePaintData::paint`, in global logical coordinates, which
KWin unions into that frame's damage (`WorkspaceScene::prePaint`). Note that
`WindowPrePaintData::devicePaint` — which the spec's Rendering section has
`prePaintWindow` expanding upward for the Cap — is *not* read anywhere in KWin
6.6.6; the Caps stay whole because every frame the snow moves in repaints the
whole output, not because of that line.

**Snow stopped on one monitor of two.** The schedule was one `FramePacer` for
the whole effect, re-armed by every frame of every output. A monitor that
something else repaints continuously — a video, a terminal cursor, a clock in a
Panel — kept pushing the timer out of reach, so the request that would have
woken the *quiet* monitor was never made and its snow froze while the busy one
went on falling.

So: one schedule per output, and a frame that is not due on an output is one the
snow is drawn in but not moved in — *clipped to what that frame repaints*. A
frame that *is* due adds the whole output to `data.paint`, which makes a frame
the snow rides along with as complete as one it asked for itself.

The clip is not optional and is not an optimisation. KWin renders into a buffer
it has used before and repaints only what has changed since that buffer was last
shown; outside that region the buffer is already right and is left alone — and
outside that region "already right" includes the snow, which is the whole reason
a frame the snow does not move in can leave it standing. Drawing it again there
lands a second premultiplied `over` on top of the first, and a Flake blended
twice is a brighter Flake until the next full repaint puts it back. KWin leaves
the scissor to whoever is drawing (`GLVertexBuffer::render`: "It's within the
caller's responsibility to enable `GL_SCISSOR_TEST`"), so both painters enable it
and pass the region through. This went unnoticed for as long as the only frames
between two of the effect's own were occasional small ones; KWin's shake-cursor
plugin, which draws an oversized pointer into the scene while the mouse is moved
quickly, asks for them by the hundred.

## Consequences

The frame rate cap now bounds how often the snow *moves*, not only how often the
effect asks. Under a desktop painting faster than the cap — a drag, a video —
the snow animates at the cap and rides along in those frames for free; it no
longer animates at the desktop's rate, and no longer makes every one of those
frames a full-screen repaint.

There is still one timer. A repaint request is not per output in any useful
sense — `Scene::addLogicalRepaint` schedules a frame on every output for any
region asked for — so a timer per output would wake the same outputs several
times over. `FrameClock` arms its one timer for the earliest of the schedules
and never asks twice within an interval. That second rule is what makes the cap
a cap: it keeps an output that is not rendering at all (switched off, its render
loop inhibited) from leaving a schedule permanently overdue and turning the
clock into a spin, and it means schedules that have drifted apart cost no extra
wakeups — one request wakes every output, and an output that was not due in that
frame is due in the next one.

A schedule is advanced against **the moment the frame was decided**, never
against the moment it finished rendering. `FramePacer::spendOn(decided)` asks
whether the frame was due as well as being told whether the snow moved in it,
and prePaint and postPaint are a few milliseconds apart: asked again at postPaint
it answers yes about a frame that answered no at prePaint, and the schedule is
spent on a frame the snow stood still in. Because it advances by a whole number
of refresh periods, the next due moment lands inside the render window of the
frame two later and it happens again — a stable lock-out, not a dropped frame.
Replayed against a 60 Hz loop it cost between one skipped step and *one step in
thirty seconds*, decided by nothing but where the phase happened to start, and
it only showed while something else was painting the desktop continuously, since
the effect's own frames are the only ones there is otherwise. `FrameClock` keeps
that moment in `Schedule::decided`, and `aFrameThatWasNotDueDoesNotSpendTheSchedule`
pins it.

Spending and waiting are separate operations: `spendOn(decided)` only advances
the schedule, and `waitUntilDue(now)` only reads how long remains when waiting
starts. `FrameClock::frameRendered()` spends each frame against its saved
decision, then `arm(now)` finds the earliest wait across all outputs. There is
no combined operation whose one timestamp has to serve both moments.

`FramePacer` stays free of KWin and Qt and stays unit-tested; the per-output
bookkeeping around it needs a compositor and, like the other registries, is not.
