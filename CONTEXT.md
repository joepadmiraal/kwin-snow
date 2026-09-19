# Snow Effect

A native KWin C++ compositor effect that renders falling snow over the Plasma
desktop and lets it accumulate on the top edges of windows and panels.

## Language

**Flake**:
A single animated snow particle with a position, size and drift, living in
screen coordinates until it lands or leaves the screen.
_Avoid_: Particle, snowflake, dot

**Snowfall**:
The falling Flakes of one output, together with the wind and the rules that
move them. The simulation is per output, so a Flake belongs to one Snowfall for
the whole of its life and never crosses to another.
_Avoid_: Field, emitter, particle system, snow storm

**Snow Catcher**:
Any surface that flakes can land on and accumulate against. Windows and panels
are both Snow Catchers; which classes are enabled is a configuration choice.
_Avoid_: Target, collider, surface

**Snowline**:
The accumulated snow belonging to one Snow Catcher, stored as a one-dimensional
array of depths indexed by column in the Catcher's own local X coordinates, so
it travels with the Catcher when it moves.
_Avoid_: Pile, heightmap, drift, accumulation buffer

**Column**:
One horizontal slot of a Snowline: the unit at which accumulated depth is
recorded and drawn.
_Avoid_: Bucket, slice, bin

**Panel**:
A Snow Catcher that KWin reports as a dock, including the primary taskbar and
any secondary or floating panels.
_Avoid_: Taskbar, dock, shelf

**Solid**:
The state of a Snow Catcher class that is enabled: flakes stop on contact and
become Snowline depth. A disabled class is transparent to flakes, which fall
past it toward the next Catcher below.
_Avoid_: Collidable, opaque, blocking

**Cap**:
The drawn band that renders a Snowline. The Snowline is the depth data; the Cap
is its appearance, and the two vary independently — Cap Style changes how a
Snowline looks without changing what it holds.
_Avoid_: Band, crust, snow layer
