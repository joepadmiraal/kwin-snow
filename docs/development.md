# Developing the Snow effect

## Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cmake --install build
```

Two plugins come out of it. The install prefix defaults to `~/.local`, putting
them at:

```
~/.local/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/plugins/snow.so
~/.local/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/configs/kwin_snow_config.so
```

KWin's `PluginEffectLoader` scans the `kwin/effects/plugins` namespace under
every Qt library path, and resolves an effect's configuration module in the
sibling `kwin/effects/configs` namespace. `~/.local/...` is **not** a Qt library
path by default, so it has to be named in `QT_PLUGIN_PATH`. Both loops below
handle that.

On a Debian or Ubuntu system the build needs, beyond a compiler and CMake:

```sh
sudo apt install kwin-dev extra-cmake-modules qt6-base-dev qt6-declarative-dev \
    libkf6config-dev libkf6coreaddons-dev libkf6windowsystem-dev \
    libkf6kcmutils-dev libkf6configwidgets-dev libkf6i18n-dev
```

The last three are the configuration module's alone: it is a widget dialog and
the effect is not, so nothing KWin loads links a toolkit.

The plugin is built against whatever `kwin-dev` is installed — 6.6.6 here — and
will need a rebuild, occasionally a source fix, on each KWin minor bump. That is
a deliberate trade; see
[ADR-0002](adr/0002-native-cpp-effect-accepting-abi-churn.md).

## Tests

`Catcher`, `Snowline`, `Snowfall`, the Flake sprites and the Cap contour are
plain C++ with no compositor in them, so most of the effect is checked without
one:

- the geometry rules of
  [ADR-0001](adr/0001-per-catcher-snowline-in-local-coordinates.md) — Columns in
  local coordinates, resize anchored at the left edge, never a rescale;
- the Flake motion — population per unit area, fall speed, gusts and sway,
  wrapping at the sides, respawning at the bottom, and no Flake ever leaving
  its output;
- what a Snowline does with snow — the shares of a deposit, the depth cap,
  relaxation toward an angle of repose, melt;
- and the two meeting — Flakes landing topmost-first, a disabled class being
  transparent, a Catcher that is off screen being transparent too while keeping
  what it was holding, a Flake crossing a surface in one step still landing, and
  both sides of the melt threshold: under the rate snow arrives at a Column
  fills to the cap, over it the Column stays bare;
- what a Flake is drawn as — a near Flake larger and brighter than a distant
  one, the blob falling off to nothing, the crystal having six arms and turning
  where the others do not;
- and what a Cap is drawn as — the ramp toward a Catcher's corners, the static
  per-Column noise of the `contour` style, and the profile the band is shaded
  with darkening downward from its leading edge.
- and when the next animated frame is asked for — the interval the frame rate
  cap means, a late frame being followed by a shorter wait so that the cap
  still holds, and a stall not being caught up on;
- and which frames the snow may move in — the ones the schedule says are due,
  not the ones something else asked for, and asking how long is left not moving
  the schedule ([ADR-0005](adr/0005-one-frame-schedule-per-output.md));
- and what a settings change does — a switched-off Catcher class melting rather
  than being cleared, a lowered `maxDepth` being melted down to rather than cut
  to, and a changed `density` spawning or ceasing to spawn rather than adding or
  removing Flakes mid-air;
- and that `src/snowconfig.kcfg` and `Snow::Settings` carry the same defaults,
  which is the one thing here that two files have to agree on and no tool
  checks;
- and that the configuration module offers every key the schema declares and no
  others, with its combo rows in the schema's order and its spin boxes on the
  schema's ranges — the same class of agreement, between the same schema and the
  widget names nothing else spells out.

```sh
ctest --test-dir build --output-on-failure
```

Everything that does need a compositor is checked in the nested session below,
and so is whether the snow *looks* and *feels* right, which no test can tell
you. The
published prototype linked from the spec is the reference for that.
`-DBUILD_TESTING=OFF` drops every test target, and with them the only dependency
on `Qt6::Test`. The configuration module's test loads the plugin as built rather
than linking its source, so it is the one test that wants a widget toolkit; it
runs offscreen, never saves, and reads a throwaway `kwinrc` rather than the
session's.

## Naming

The effect's identity is the **file name** of the plugin. KF6 derives the plugin
id from the basename of the `.so` and ignores a `KPlugin.Id` field (warning
about it if present), so `snow.so` is what makes the effect known to KWin as
`snow`. That is the name used by:

- `kwinrc`, as `[Plugins] snowEnabled`
- the D-Bus calls `loadEffect snow` / `unloadEffect snow`
- the config group `[Effect-snow]`

This matches how KWin's own effects are named (`blur.so`, `cube.so`). The
`X-KDE-PluginInfo-Name` key in `metadata.json` records the same name, but is
legacy metadata that KF6 and KWin no longer read.

The effect is **disabled by default** (`EnabledByDefault: false`), so installing
it never changes what a session does until it is switched on.

## Primary loop: nested compositor

```sh
tools/dev.sh
```

One command turns an edit into a running compositor: it builds, installs, and
launches a nested `kwin_wayland` in a window with the effect already enabled. A
crash kills only that window.

Options:

| Flag | Effect |
| --- | --- |
| `--width N` / `--height N` | Nested output size (default 1600x1000) |
| `--scale N` | Output scale, for checking logical-vs-device pixel handling |
| `--outputs N` | Several nested outputs, for the per-output simulation |
| `--app CMD` | Program to launch inside (default `konsole`) |
| `--panel` | Run `plasmashell` inside, to get a real Panel to catch snow |
| `--no-build` | Skip build and install, just launch |
| `-- ARGS` | Pass the rest through to `kwin_wayland` |

The script keeps the nested session isolated in two ways, so the live session is
never touched: `XDG_CONFIG_HOME` points at `build/dev-home`, which holds a
throwaway `kwinrc` with `[Plugins] snowEnabled=true`; and everything runs under
`dbus-run-session`, which is also what allows a second `plasmashell` for
`--panel`.

### Watching Snowlines

Every time the Catcher set changes shape — a window opens, closes, or starts or
stops catching — the effect logs the whole set, and each line carries that
Catcher's Column count and the sum of its depths. Real snow, so opening and
closing a throwaway window every few seconds is enough to watch a pile build
and then hold at its steady state:

```
Snow Catchers: now catches: sleep — Konsole -- 3
    ground on WL-0  surface y=1000 x=[0,1600]  (the ground)  snowline: 320 cols, sum 60.2, …
    window on WL-0  surface y=350 x=[600,1000]  Konsole  snowline: 80 cols, sum 24.9, …
```

A plain move or resize deliberately says nothing — that happens on every frame
of a drag — so to watch one, open a window rather than nudge it.

### Watching Flakes

Flakes are drawn, so watching them is a matter of looking — but only against
something to see them on. A nested session with no wallpaper is white, and white
snow on white reads as a faint ring rather than as a Flake, so:

```sh
tools/dev.sh --panel
```

runs `plasmashell` inside, which brings a real wallpaper and a real Panel with
it. One line is logged per output as its Snowfall starts:

```
Snowfall on WL-0 -- 475 flakes of 475, #0 (1117.8, 976.0) z 0.92, …
```

A **fullscreen window makes a poor test scene**: it is a Snow Catcher whose top
edge is the top of the output, so it catches every Flake in the first frame or
two of its fall and what is left is the handful the wind blew in from the sides.
That is the effect working, not failing — and it is what the fullscreen
suspension of the spec's Behaviour table exists for.

### Watching Caps

A Cap is drawn inside its own Catcher's paint pass, so looking at one is a
matter of having something for the snow to pile on. Two things make a short
session show less than you might expect:

- **A Cap takes a minute or two to arrive.** At the shipped defaults a window
  fills to `maxDepth` in about a minute and the ground in about two and a half,
  and neither is worth watching for before then. Snow arrives at
  0.036 × `density` logical px/s per Column on the ground and 0.070 × `density`
  on a window, at `fallSpeed` 5, and `meltRate` comes off that — so raising
  `density` or dropping `meltRate` to 0 is how to have something to look at
  sooner (see *Changing settings* below).

  Note that melt is a **threshold, not a balance**: over the rate above, a
  Column sits at bare for good; under it, the Column climbs to `maxDepth`
  whatever the melt rate is. There is no value that holds a pile half way up.
  The default is under the rate at every `density` but 1, which is what it is
  chosen for; the 0.4 the effect first shipped with was over it at
  `density` 5 and nothing lay anywhere.
- **The ground's Cap needs a wallpaper**, because it is drawn just after the
  desktop window — the only point in a frame that is over the desktop and under
  every window. `tools/dev.sh --panel` runs `plasmashell` and so has one; a bare
  nested session does not, and its ground Snowline fills up unseen. That is the
  same consequence [ADR-0003](adr/0003-no-below-windows-flake-pass.md) recorded
  for the Flakes, and the reason the Flakes escaped it by painting above
  everything instead.

A bottom Panel covers the ground's Cap, so move the Panel to another edge to see
both at once. Auto-hiding it does *not* do instead: a Catcher that is off screen
stops catching, so while the Panel is hidden its own Cap stops growing and the
ground below it fills up normally.

### Driving the nested session

The nested session is on its own D-Bus (`dbus-run-session`) and its
`kwin_wayland` is confined, so the bus address cannot be read out of
`/proc/<pid>/environ`. Have the `--app` script publish it instead, one line
before it execs the real program:

```sh
#!/bin/sh
printf 'DBUS_SESSION_BUS_ADDRESS=%s\n' "$DBUS_SESSION_BUS_ADDRESS" > /tmp/nested-env
exec plasmashell --no-respawn        # or konsole, or anything else
```

Anything run with that address, `WAYLAND_DISPLAY=wayland-snowdev` and the
session's `XDG_CONFIG_HOME` then talks to the nested session and nothing else:
`qdbus6 org.kde.KWin /Effects ...` to load and unload effects,
`qdbus6 org.kde.KWin /Scripting ...` to run a KWin script (`loadScript <file>
<name>`, `start`, then `unloadScript <name>` so the next one can run), a second
`konsole` for a window to catch snow on, and `spectacle -b -n -f -o shot.png` for
a screenshot of the nested output only. `print()` from a KWin script lands in
the same log as the effect.

One trap when restarting the session: `pkill -f wayland-snowdev` matches the
shell running it as well, so it kills the command that was about to relaunch.
`pkill -f "wayland-snowde[v]"` matches only the compositor.

### Changing settings

Every knob is a key of `[Effect-snow]` in the nested session's own `kwinrc`, and
the effect re-reads them all when it is told to — which is exactly what a KCM
does, so this is the real path rather than a test one:

```sh
kwriteconfig6 --file "$XDG_CONFIG_HOME/kwinrc" --group Effect-snow --key meltRate 0
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.reconfigureEffect snow
```

with the nested session's environment from the recipe above. The effect logs
what it now holds, which is also how to tell a key that did not arrive from one
that did nothing:

```
kwin.effect.snow: Snow reconfigured -- windows on, panels on, desktop on;
    depth flakes, density 5, fall 5, wind 4; contour caps, max depth 20 px,
    melt 0.05 px/s; 30 fps
```

`tools/dev.sh` rewrites `kwinrc` at every launch, so an `[Effect-snow]` group
written before it starts is thrown away; write the keys once the session is up.

**Three changes are deliberately not instant**, and watching them is the point:
switching a Catcher class off melts its Snowlines at 8 px/s instead of clearing
them, lowering `maxDepth` leaves deeper snow to melt down to it, and changing
`density` spawns faster or stops spawning rather than adding or removing Flakes
that somebody is looking at. Each takes a second or several, so give them one —
at 1 px/s, `meltRate` 0 and a pile a few pixels deep, the difference between
melting and clearing is a second of watching.

### The configuration dialog

The same keys have a dialog: the gear beside Snow in **System Settings →
Desktop Effects**, which is `kwin_snow_config.so` and opens on two tabs, Basic
and Advanced. It is what the `kwriteconfig6` recipe above imitates — it writes
the same group and then makes the same `reconfigureEffect snow` call — so the
three melting changes are exactly the thing to watch while clicking Apply.

The gear only appears because the effect's `metadata.json` carries
`X-KDE-ConfigModule: kwin_snow_config`; that key is the whole of the link
between the two plugins, and the module keeps its `kwin_snow_config` name even
though the effect is named `snow`. See
[ADR-0004](adr/0004-effect-identity-is-the-plugin-filename.md).

System Settings runs inside a nested session too, which is where to click
through it without a KCM crash landing on the live one:

```sh
tools/dev.sh --panel --app systemsettings
```

Two things it does *not* do, both on purpose: the corner inset ramp is not in it
because it approximates a decoration's rounded corners rather than being a
choice anyone would make, and neither is the idle timeout.

What the dialog itself does is bind widgets to keys by object name —
`kcfg_meltRate` is what makes that spin box the `meltRate` key — and read its
ranges off the schema rather than restating them. A misspelt name is a widget
that moves and changes nothing, which is why `tests/kcmtest.cpp` holds the two
together.

### Watching suspension

The effect stops animating whenever nobody is looking, and says so in one line
each time the answer changes:

```
kwin.effect.snow: Snow falling
kwin.effect.snow: Snow suspended: a fullscreen window is active
kwin.effect.snow: Snow frozen: the session is idle
kwin.effect.snow: Snow suspended: the session is locked
```

All three are reachable from outside the nested session (see the environment
recipe above):

- **Fullscreen** — a KWin script, since no shortcut is bound in a nested
  session: `workspace.activeWindow.fullScreen = true`, loaded and started over
  `org.kde.kwin.Scripting`. The property reads back `false` immediately
  afterwards; the window goes fullscreen a moment later anyway.
- **Locked** — `qdbus6 org.freedesktop.ScreenSaver /ScreenSaver Lock`. It cannot
  be undone from outside: `SetActive false` returns `false`, because unlocking
  means authenticating at the greeter. Worth knowing before you lock a session
  you still wanted to test in. A nested session also **inherits the live
  session's lock** through logind, so while the real screen is locked every
  nested session starts out suspended — `tools/dev.sh -- --no-lockscreen` is
  the way to test anything else while that is true.
- **Idle** — five minutes of no input at all. The nested session receives input
  only while you are interacting with its window, so this arrives on its own if
  you leave it alone; to watch it sooner, shorten `s_idleTimeout` in
  `src/suspension.cpp` for the run.

The two that are "suspended entirely" take the effect out of the frame, so
nothing of it is drawn and nothing of it runs. Idle instead leaves the last
frame standing: the snow is still on screen and pixel-identical from one
screenshot to the next.

### What the frame rate cap cannot be measured with

`frameRateCap` is the effect's whole power story, and the nested session is the
one thing here that cannot check it.

- **`showfps` is not an instrument.** Loading KWin's own `showfps` over D-Bus
  looks like the obvious way to count frames, but it drives repaints of its
  own: with the Snow effect *unloaded* it still reported 33-35 fps in an
  otherwise still session.
- **The nested compositor does not reach the cap to begin with.** Counting the
  effect's own frames (a temporary `qCInfo` in `prePaintScreen`) measured 28-31
  fps at the default cap of 30 -- and about 24 fps with the cap raised to 120,
  which is the nested compositor's own ceiling rather than the display's 59.
  So a capped effect can be seen holding its cap, but what an uncapped one
  would cost is not visible here at all. That is a live-session question.

The arithmetic the cap rests on -- when to ask for the next frame, and what a
late frame does to the rate -- is checked in `tests/framepacertest.cpp` instead.

Load and unload are logged. `tools/dev.sh` sets `QT_LOGGING_RULES` so the
effect's category is fully visible; you should see, on startup and shutdown:

```
kwin.effect.snow: Snow effect loaded, built against KWin 6.6.6
kwin.effect.snow: Snow effect unloaded
```

## Secondary loop: live session

Real panels, real windows, real hardware — use it to confirm something that
already works nested, not to iterate.

```sh
tools/install-live.sh
```

On its first run this writes `~/.config/plasma-workspace/env/kwin-snow-plugin-path.sh`,
which adds the prefix to `QT_PLUGIN_PATH` for the whole Plasma session. Plasma
only sources that directory at session start, so **log out and back in once**;
after that the script installs on every run. Both KWin and System Settings need
the variable — without it the effect will not appear in the list at all.

Then enable it under **System Settings → Desktop Effects → Appearance → Snow**,
and configure it from the gear beside it. System Settings caches nothing across
runs, so a newly installed `kwin_snow_config.so` is picked up by closing and
reopening it — unlike the effect itself, which needs the session restarted.

`tools/install-live.sh --toggle` additionally unloads and reloads the effect over
D-Bus, which is the quickest check that it comes up and goes down without taking
KWin with it:

```sh
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect snow
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect snow
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadedEffects
```

Toggling verifies behaviour, but it does **not** reliably pick up a rebuilt
`.so`: KWin keeps the shared object mapped for the life of the process, so new
code needs a full KWin restart. On Wayland that means restarting the session.
This is the whole reason the nested loop is the primary one.

Live-session logs go to the journal:

```sh
journalctl --user -f -o cat | grep -i snow
```

### Watching frames

A stutter is three different faults wearing the same coat, and telling them
apart is what `src/frameprobe.h` is for. Set `KWIN_SNOW_PROBE` in the
compositor's environment and the effect writes down what every frame did,
reporting the ones that stand out as they happen and summing the rest up once a
second:

```
kwin.effect.snow: Snow probe eDP-1 -- no frame for 216.7 ms (13.0 refreshes);
    snow stood still for 216.7 ms of a 33.3 ms step; frame 2.1 ms (sim 0.4 ms),
    repaint 2.30 logical Mpx asked in 1 rect, painted in 1 rect, 2 translucent
    + ground Cap, stepped
kwin.effect.snow: Snow probe eDP-1 -- translucent windows 2 -> 3
kwin.effect.snow: Snow probe eDP-1 1.00 s -- 60 frames, 30 stepped,
    1 reported; frame 3.9 ms worst, 1.4 ms mean; sim 0.9 ms worst; widest
    present gap 216.7 ms, widest step gap 216.7 ms; repainted 69.4 logical
    Mpx, painted in 1 rect median, 6 worst; translucent 2..3, ground Cap in
    30 frames
```

Each line is a handful of numbers, and which of them is wrong says whose fault
it is:

- **the present gap** — how far apart two consecutive frames of an output are,
  judged against the pacing interval rather than against a refresh period: when
  nothing else is painting the desktop, a frame every interval is all there is
  to expect. Wider than that means the compositor produced no frame when one was
  asked for, so nothing on screen moved and the snow is only the most visible
  thing that did not. The cause is outside this effect, though it may still be
  this effect's repaints that cost the time.
- **the step gap** — how far apart two frames the snow *moved* in are. Wide
  while the present gap stays at a refresh period means frames were produced and
  the snow was not stepped in them, which is `FrameClock`'s to answer for.
- **the translucent count** — how many windows the effect took out of the
  occluders by marking them translucent, which is its one lever on a frame that
  no repaint region records: a window that is not an occluder is one whose
  neighbours below it are painted whole rather than culled. A peak in KWin's own
  `Paint Amount` that lines up with a change here comes from here; one that does
  not, does not.
- **the frame time**, and the slice of it the simulation took. Over a refresh
  period is a frame that cannot be delivered on time whatever the schedule says;
  if `sim` is not most of it then the cost is in what the frame asked to be
  repainted rather than in the snow.
- **the painted rect count** — how many rectangles the region KWin settled on
  repainting is made of, which is a different number from the one in the same
  line's `asked` clause: that one is what the effect put in `data.paint`, in
  logical pixels, before KWin unioned it with everybody else's damage. Both
  painters scissor to the painted region and issue a draw per rect of it, so
  the Flakes cost eight draws a rect and every Cap one more. A frame that
  repaints the whole output is a single rect and is the cheapest of them in
  this one respect. The summary prints `32+` when the median falls in the
  histogram's overflow bucket (32 or more rects); the worst count stays exact.

`KWIN_SNOW_PROBE` is read once, when the effect is loaded, so it goes where the
session already picks environment up — the same directory `install-live.sh`
writes `QT_PLUGIN_PATH` into:

```sh
echo 'export KWIN_SNOW_PROBE=1' > ~/.config/plasma-workspace/env/kwin-snow-probe.sh
chmod +x ~/.config/plasma-workspace/env/kwin-snow-probe.sh
```

Then log out and back in, which a rebuilt `snow.so` needs anyway — KWin keeps
the shared object mapped, so unloading and loading the effect does not pick up
new code. Read it with `journalctl --user -f | grep "Snow probe"`. Delete the
file and log in again to turn it off.

**Do not measure this nested.** A nested compositor does not reach the frame cap
to begin with, so every number above is its host's rather than a display's.
