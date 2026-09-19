# Flakes always paint above every window; there is no z-order setting

A snow effect would normally be expected to offer a choice of whether in-flight
flakes render in front of windows or behind them, and the absence of that
setting is conspicuous. It was removed because it does nothing: an enabled Snow
Catcher class is Solid, so a flake stops on the top edge of a window and can
never occupy a pixel that the window covers. With windows solid, painting flakes
above or below them produces the same image. The one configuration where the
orderings genuinely differ — windows not catching, so flakes fall past them to
the panel and ground — was cut in favour of always painting above.

The invariant is narrower than it sounds: it holds for a flake that comes down
onto a Catcher, which is every flake that reaches a window's top edge from
above, and it does not hold for one blown in past a left or right edge below
that line. Such a flake never crossed the catching edge, so it is not caught,
and it goes on falling in front of the window. What is *not* allowed is a flake
crossing the top edge inside the window's span, which is snow going through the
snow; see `Snowfall::land()`.

## Consequences

Flakes render in a single `postPaintScreen` pass with no per-window work. This
also removed a dependency on plasmashell's desktop window: painting flakes
"behind windows but in front of the wallpaper" has no natural hook in the effect
API and would have had to be done inside the desktop window's paint pass,
silently rendering nothing on any session without one. Caps still paint inside
each window's own `paintWindow`, where KWin's back-to-front order provides
occlusion for free.

## Revisited: the one case where the orderings differ

Flakes blown in past a side edge turned out to be a second case where painting
above and painting below are not the same image, and unlike the one this
decision turned down, it happens in the default configuration and every gust.
It is now the `flakesInFrontOfWindows` setting, defaulting to what the effect
has always done.

This is not the general z-order setting refused above, and the reasoning here
still stands. Nothing is painted behind a window: a flake the setting holds
back is not drawn at all while it is inside one, which is the same image as
being behind it and keeps the single pass and the independence from
plasmashell's desktop window. It changes no flake's fall and no catcher's snow
— `Snowfall::land()` runs the same either way — and a flake that comes down
onto a window lands on it whichever way the setting is set.
