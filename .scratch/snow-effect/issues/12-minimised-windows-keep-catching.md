# A minimised window goes on catching Flakes

Status: resolved
Blocked by: 11

Reported from the live session: minimise a window and the snow still stops in
mid-air along the line where its top edge used to be — a bare strip of sky the
width of the window, with nothing there to explain it.

## Cause

`CatcherRegistry::collect()` pushed `EffectWindow::isHidden()` into each Catcher
once a frame as its concealment, on the reading that one question covered a
minimise, an auto-hidden Panel and a desktop switch alike (issue 11, "the
auto-hidden Panel"). It does not. In KWin `isHidden()` is only the internal
hidden flag — `Window::m_hidden`, set through `setHidden()` for an unmapped X11
window or an auto-hidden Panel. Minimise, show desktop, the current virtual
desktop and the current activity are each a predicate of their own, which is
why `Window::isShown()` is the conjunction of all five rather than a reading of
`isHidden()`. So the auto-hidden Panel that issue 11 measured was fixed and the
minimised window it claimed came along for free never was.

The Cap is unaffected either way: it is painted from `paintWindow()`, so a
window KWin is not drawing has no Cap to draw. Only the hit testing was wrong,
which is why the symptom is invisible snow standing on an invisible surface.

## Fix

`CatcherRegistry::isConcealed()` now asks all five, and is also pushed into the
Catchers that the walk over the stacking order does not reach.

## Measured

Nested session, one Konsole window 620 px wide, `maxDepth` 20, the same window
minimised throughout. The sum of its Snowline's 124 Columns, sampled twice a
minute apart:

| Build  | At minimise + 30 s | 60 s later |
| ------ | ------------------ | ---------- |
| Before | 1156.9             | 2449.7 — filled to the 20 px cap while nobody could see it |
| After  | 1714.9             | 1280.3 — melting, catching nothing |

The "after" run starts higher because that window had been catching before it
was minimised; what matters is the direction.
