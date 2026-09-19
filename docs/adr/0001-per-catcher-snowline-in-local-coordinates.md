# Snowlines are stored per Snow Catcher in local coordinates

Accumulated snow could be held as a single screen-space height array, indexed by
screen column, which is simpler to compute and needs no per-window state. We
store a separate Snowline per Snow Catcher instead, indexed by Column in that
Catcher's own local X coordinates, because it is the only model where the snow
belongs to the thing it landed on: dragging a window carries its Snowline with
it, raising a window does not teleport a pile onto it, and closing a window
disposes of its snow without touching anything else.

## Consequences

Snowline lifetime is bound to `EffectWindow` lifetime, so the effect must track
window add/remove/resize and maintain the arrays alongside them. Resize anchors
the array at the left edge and truncates or zero-extends rather than rescaling,
because rescaling stretches existing snow into a smear that reads as obviously
fake. Columns and depths are in logical pixels, not device pixels, so a Snowline
stays valid when a window moves between outputs with different scale factors.
