#!/usr/bin/env bash
# Build, install and run the Snow effect in a nested kwin_wayland.
#
# The nested compositor is the primary development loop: the effect is a
# compositor plugin and will segfault while it is being written, and doing that
# to the live session repeatedly costs more than the fidelity gap. A crash here
# kills only the nested window.
#
# Usage: tools/dev.sh [options] [-- extra kwin_wayland args]
# See docs/development.md.

set -euo pipefail

REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO/build}"
PREFIX="${PREFIX:-$HOME/.local}"
DEV_HOME="${DEV_HOME:-$BUILD_DIR/dev-home}"

WIDTH=1600
HEIGHT=1000
SCALE=1
OUTPUTS=1
APP="konsole"
DO_BUILD=1
WITH_PANEL=0
EXTRA=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --width)    WIDTH="$2";   shift 2 ;;
        --height)   HEIGHT="$2";  shift 2 ;;
        --scale)    SCALE="$2";   shift 2 ;;
        --outputs)  OUTPUTS="$2"; shift 2 ;;
        --app)      APP="$2";     shift 2 ;;
        --panel)    WITH_PANEL=1; shift ;;
        --no-build) DO_BUILD=0;   shift ;;
        --)         shift; EXTRA=("$@"); break ;;
        -h|--help)
            sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'
            exit 0 ;;
        *)
            echo "dev.sh: unknown option '$1'" >&2
            exit 2 ;;
    esac
done

if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "dev.sh: WAYLAND_DISPLAY is unset; this script nests inside a running Wayland session." >&2
    exit 1
fi

if (( DO_BUILD )); then
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        cmake -B "$BUILD_DIR" -S "$REPO" -G Ninja \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_INSTALL_PREFIX="$PREFIX"
    fi
    cmake --build "$BUILD_DIR"
    cmake --install "$BUILD_DIR" >/dev/null
fi

# KWin scans the "kwin/effects/plugins" namespace under every Qt library path,
# and ~/.local/... is not one by default -- so name it explicitly.
PLUGIN_ROOT="$PREFIX/lib/$(gcc -print-multiarch 2>/dev/null || echo x86_64-linux-gnu)/qt6/plugins"
if [[ ! -f "$PLUGIN_ROOT/kwin/effects/plugins/snow.so" ]]; then
    echo "dev.sh: snow.so not found under $PLUGIN_ROOT -- run without --no-build." >&2
    exit 1
fi

# A throwaway config home, so the nested compositor can have the effect switched
# on without touching the live session's kwinrc.
mkdir -p "$DEV_HOME"
cat > "$DEV_HOME/kwinrc" <<EOF
[Plugins]
snowEnabled=true

[Compositing]
LatencyPolicy=Low
EOF

echo "dev.sh: launching nested kwin_wayland (${WIDTH}x${HEIGHT}, scale $SCALE, $OUTPUTS output(s))"
echo "dev.sh: config home $DEV_HOME  --  close the nested window to stop"

INNER="$APP"
if (( WITH_PANEL )); then
    # plasmashell is a single-instance D-Bus app; the isolated bus below is what
    # lets a second one run for Panel testing.
    INNER="plasmashell --no-respawn"
fi

# kwin_wayland parses every argument it is given, including ones meant for the
# program it launches, so anything with a leading dash gets claimed as a kwin
# option. Handing it a single script path sidesteps that.
INNER_SCRIPT="$DEV_HOME/inner.sh"
printf '#!/bin/sh\nexec %s\n' "$INNER" > "$INNER_SCRIPT"
chmod +x "$INNER_SCRIPT"

export XDG_CONFIG_HOME="$DEV_HOME"
export QT_PLUGIN_PATH="$PLUGIN_ROOT${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export QT_LOGGING_RULES="kwin.effect.snow.debug=true;kwin_core.debug=false"
export KWIN_COMPOSE="${KWIN_COMPOSE:-O2}"

exec dbus-run-session -- kwin_wayland \
    --width "$WIDTH" \
    --height "$HEIGHT" \
    --scale "$SCALE" \
    --output-count "$OUTPUTS" \
    --socket wayland-snowdev \
    --no-global-shortcuts \
    "${EXTRA[@]}" \
    "$INNER_SCRIPT"
