# Native C++ effect, accepting KWin ABI churn

KWin's QML/JavaScript scripted-effect API is ABI-stable and survives compositor
upgrades untouched, which makes it the obvious first choice for a decorative
effect. We are writing a native C++ binary effect anyway, because the scripting
API cannot add custom paint passes or per-pixel particle rendering, and
accumulating snow on window top edges requires both. A standalone layer-shell
overlay was also rejected: it can draw over the screen but cannot read window
geometry or paint between stacking levels, so it cannot do occlusion.

## Consequences

KWin exports no stable public API for out-of-tree binary effects. The plugin is
built against the exact `kwin-dev` version installed (6.6.6) and will need a
rebuild, occasionally a source fix, on each KWin minor bump. This is accepted
rather than engineered around. Distribution beyond this machine — an X11 path
and CI across multiple KWin versions — remains out of scope. Release packaging
is now provided for the documented Ubuntu, Fedora, and Arch KWin lines, with a
rebuild required whenever a distribution's KWin ABI changes.
