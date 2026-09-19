# A Snow Catcher is offered to the outputs its surface lies over

A landing line is a line in *global* logical coordinates, and the outputs are
laid out in that same space — so a surface belonging to one monitor can lie
straight across another. Every Snow Catcher on the desktop was handed to every
Snowfall, on the reasoning that a Flake never leaves its own output and so could
never reach a Catcher it had no business landing on. That holds horizontally,
where a Catcher off to the side is answered by `Catcher::columnAt()` returning
-1. It does not hold vertically.

**Snow vanished on the monitor below.** The ground Catcher is a zero-height
strip along the bottom edge of its output, so on a laptop with an external
monitor above it — the panel at `160,1271 1920x1200`, the external at
`0,0 2259x1271` — the external's ground is the line `y = 1271`, which is the
laptop's top pixel row, spanning the whole of its width. Every Flake of the
laptop's Snowfall spawns in the band above `y = 1271` and crosses that line on
its first step: it landed on the *external* monitor's ground, deposited its mass
into a Snowline nobody could see it join, and respawned above the top to do it
again. What it looked like: the initial population, which is scattered down the
output rather than queued above it, fell and settled normally — a few seconds of
snow. After that, every Flake was caught in the top pixel row, which read as
artefacts along the top edge; and once that Snowline reached `maxDepth`, its
surface stood *above* the laptop's top edge and the Flakes were taken before
they were ever drawn. The snow disappeared, while the monitor above went on
snowing and its ground pile grew at twice the rate — which looks like nothing at
all, because snow along the bottom edge of the desktop is what a ground Cap is.

So: `CatcherRegistry::catchersFor()` hands each Snowfall the Catchers whose
surfaces lie over that output, and the two classes are asked differently.

The ground is asked **by identity**: a ground line sits exactly on the seam
between two stacked outputs, where no arithmetic can say whose it is. An output
gets its own ground and no other.

Everything else is asked **by geometry**: a window or Panel goes to every output
whose vertical range contains its landing line, half-open at the bottom so that
a window snapped to the top edge of the lower monitor belongs to the output it
is on rather than to the one whose bottom row it grazes. This keeps the case the
old reasoning was protecting — a window straddling two monitors side by side is
in both lists and catches over the whole of its top edge, each output's Flakes
landing on the part of it in front of them.

## Consequences

Which Catchers a Flake can land on is now a question about the output, and it is
answered in the registry, which is the one thing here that knows about outputs.
Snowfall itself is unchanged: it still hit-tests whatever list it is handed,
back to front, and still has no compositor in it.

A window straddling the seam *vertically* — its top edge on the upper monitor,
its body reaching down into the lower one — catches only the upper monitor's
Flakes. That is the same answer the half-open rule gives everywhere else: the
surface is not on the lower output, so the snow of that output falls past it.

The rule is geometric, so it costs a walk over the Catchers once per output per
frame rather than once per frame. That walk was already being made and thrown
away; what is new is the filter over it.
