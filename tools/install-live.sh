#!/usr/bin/env bash
# Install the Snow effect into the live Plasma session -- the secondary loop.
#
# Use this to confirm something on real hardware, real panels and real windows
# after it already works nested. Prefer tools/dev.sh while writing code: a
# compositor plugin that segfaults here takes the whole session with it.
#
# Usage: tools/install-live.sh [--toggle]

set -euo pipefail

REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO/build}"
PREFIX="${PREFIX:-$HOME/.local}"
PLUGIN_ROOT="$PREFIX/lib/$(gcc -print-multiarch 2>/dev/null || echo x86_64-linux-gnu)/qt6/plugins"
ENV_FILE="$HOME/.config/plasma-workspace/env/kwin-snow-plugin-path.sh"

DO_TOGGLE=0
[[ "${1:-}" == "--toggle" ]] && DO_TOGGLE=1

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    cmake -B "$BUILD_DIR" -S "$REPO" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX="$PREFIX"
fi
cmake --build "$BUILD_DIR"
cmake --install "$BUILD_DIR"

# KWin and System Settings only scan $PLUGIN_ROOT if QT_PLUGIN_PATH names it,
# and Plasma sources this directory when the session starts.
if [[ ! -f "$ENV_FILE" ]]; then
    mkdir -p "$(dirname "$ENV_FILE")"
    cat > "$ENV_FILE" <<EOF
# Added by kwin-effect-snow so KWin and System Settings find effects in \$HOME.
export QT_PLUGIN_PATH="$PLUGIN_ROOT\${QT_PLUGIN_PATH:+:\$QT_PLUGIN_PATH}"
EOF
    chmod +x "$ENV_FILE"
    echo "install-live.sh: wrote $ENV_FILE"
    echo "install-live.sh: log out and back in once, then re-run this script."
    exit 0
fi

if [[ ":${QT_PLUGIN_PATH:-}:" != *":$PLUGIN_ROOT:"* ]]; then
    echo "install-live.sh: $ENV_FILE exists but QT_PLUGIN_PATH is not active in this session." >&2
    echo "install-live.sh: log out and back in, then re-run." >&2
    exit 1
fi

if (( DO_TOGGLE )); then
    # Verifies the effect loads and unloads against the live compositor. Note
    # this does NOT reliably pick up a rebuilt .so -- KWin keeps the shared
    # object mapped -- so for changed code, restart the session.
    qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect snow || true
    sleep 1
    qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect snow
    echo "install-live.sh: loaded effects now: $(qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadedEffects | grep -c '^snow$')"
fi

echo "install-live.sh: installed to $PLUGIN_ROOT/kwin/effects/plugins/snow.so"
echo "install-live.sh: and $PLUGIN_ROOT/kwin/effects/configs/kwin_snow_config.so"
echo "install-live.sh: enable under System Settings -> Desktop Effects -> Appearance -> Snow,"
echo "install-live.sh: and configure it from the gear beside it"
