#!/usr/bin/env bash
# Metal rendering test harness — launches engine, captures logs/screenshots, detects issues.
#
# Usage:
#   tools/metal-debug/run-test.sh [--timeout SECS] [--frames N] [--resolution WxH]
#
# Runs the engine windowed in a small window, captures infolog.txt and screenshots,
# then produces a structured summary for analysis.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENGINE_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$ENGINE_ROOT/build-arm64"
BINARY="$BUILD_DIR/spring"
CONT_DIR="$ENGINE_ROOT/cont"

# The engine needs data dirs to find game content and base Lua scripts.
# Portable mode requires unitsync.dylib (not built), so we use --isolation-dir
# which makes the engine use a single directory for both read/write data.
# SPRING_DATADIR adds additional read-only data dirs.

# Defaults
TIMEOUT=90
TARGET_FRAMES=300
RESOLUTION="800x600"
RUN_DIR="$SCRIPT_DIR/runs"
FULLSCREEN=0
NO_GAME=0

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout)  TIMEOUT="$2"; shift 2 ;;
        --frames)   TARGET_FRAMES="$2"; shift 2 ;;
        --resolution) RESOLUTION="$2"; shift 2 ;;
        --fullscreen) FULLSCREEN=1; shift ;;
        --no-game)  NO_GAME=1; shift ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

RES_W="${RESOLUTION%x*}"
RES_H="${RESOLUTION#*x}"

# Create timestamped run directory
RUN_ID="$(date +%Y%m%d_%H%M%S)"
RUN_PATH="$RUN_DIR/$RUN_ID"
mkdir -p "$RUN_PATH"

echo "=== Metal Debug Test Run: $RUN_ID ==="
echo "  Binary:     $BINARY"
echo "  Timeout:    ${TIMEOUT}s"
echo "  Resolution: ${RES_W}x${RES_H}"
echo "  Run dir:    $RUN_PATH"

# Check binary exists
if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: Binary not found at $BINARY — build first"
    exit 1
fi

# Backup existing springsettings.cfg in cont/ (where isolation mode will write)
SETTINGS="$CONT_DIR/springsettings.cfg"
SETTINGS_BAK="$CONT_DIR/springsettings.cfg.metal-debug-bak"
if [[ -f "$SETTINGS" ]]; then
    cp "$SETTINGS" "$SETTINGS_BAK"
fi

# Write test settings to cont/springsettings.cfg (isolation mode config)
cat > "$SETTINGS" <<EOF
FontFile = $CONT_DIR/fonts/FreeSansBold.otf
SmallFontFile = $CONT_DIR/fonts/FreeSansBold.otf
Fullscreen = $FULLSCREEN
XResolution = $RES_W
YResolution = $RES_H
XResolutionWindowed = $RES_W
YResolutionWindowed = $RES_H
WindowPosX = 50
WindowPosY = 50
GrassDetail = 0
Water = 0
SoftParticles = 0
AllowDeferredMapRendering = 0
AllowDeferredModelRendering = 0
MaxDynamicModelLights = 0
VSyncGame = 0
CamMode = 3
EOF

# Install Lua auto-test widget into the game's widget directory (VFS ZIP path).
# cont/LuaUI/Widgets/ (user widgets) is not reliably discovered by VFS.RAW_ONLY,
# but game archive widgets are always loaded via VFS.ZIP_ONLY.
WIDGET_DIR="$CONT_DIR/games/BYAR.sdd/LuaUI/Widgets"
WIDGET_FILE="$WIDGET_DIR/dbg_auto_test.lua"
sed "s/%%TARGET_FRAMES%%/$TARGET_FRAMES/g" \
    "$SCRIPT_DIR/auto_test_widget.lua" \
    > "$WIDGET_FILE"

# Force archive rescan so the new widget file is discovered
rm -f "$CONT_DIR/cache/ArchiveCache22.lua"

# Ensure the widget is enabled in the LuaUI config (order > 0 = enabled).
# The engine saves widget enable/disable state to this config file;
# order=0 means disabled, order>0 means enabled+priority.
WIDGET_CONFIG="$CONT_DIR/LuaUI/Config/BYAR.lua"
if [[ -f "$WIDGET_CONFIG" ]]; then
    # Remove any existing disabled/enabled entry
    sed -i '' '/Metal Debug Auto-Test/d' "$WIDGET_CONFIG"
    # Insert as enabled (order=1) right after the "order = {" line
    sed -i '' '/^	order = {$/a\
\t\t["Metal Debug Auto-Test"] = 1,
' "$WIDGET_CONFIG"
fi

# Remove old infolog and screenshots to start clean
rm -f "$CONT_DIR/infolog.txt"
rm -f "$CONT_DIR/screenshots/"screen*.png 2>/dev/null || true

# Restore settings and clean up widget on exit
cleanup() {
    if [[ -f "$SETTINGS_BAK" ]]; then
        mv "$SETTINGS_BAK" "$SETTINGS"
    fi
    rm -f "$WIDGET_FILE"
    # Remove widget entry from LuaUI config so it doesn't persist
    if [[ -f "$WIDGET_CONFIG" ]]; then
        sed -i '' '/Metal Debug Auto-Test/d' "$WIDGET_CONFIG"
    fi
}
trap cleanup EXIT

# Use --isolation-dir to point engine at cont/ for all data (read + write).
# This makes the engine find gamedata/parse_tdf.lua, base archives, etc.
# export MTL_DEBUG_LAYER=1  # Enable for detailed Metal validation
if [[ "$NO_GAME" -eq 1 ]]; then
    LAUNCH_CMD="$BINARY --metal-backend --isolation-dir $CONT_DIR"
else
    LAUNCH_CMD="$BINARY --metal-backend --isolation-dir $CONT_DIR $CONT_DIR/script.txt"
fi
echo "  Command: $LAUNCH_CMD"
echo ""
echo "=== Launching engine ==="

# Launch engine — CWD doesn't matter when using --isolation-dir
$LAUNCH_CMD > "$RUN_PATH/stdout.log" 2>&1 &
ENGINE_PID=$!

echo "  PID: $ENGINE_PID"

# In no-game mode, take macOS screenshots since the Lua widget doesn't load
if [[ "$NO_GAME" -eq 1 ]]; then
    SCREENSHOTS_DIR="$RUN_PATH/screenshots"
    mkdir -p "$SCREENSHOTS_DIR"
    (
        sleep 3
        for i in 1 2 3 4 5; do
            if kill -0 $ENGINE_PID 2>/dev/null; then
                screencapture -x "$SCREENSHOTS_DIR/menu_${i}.png"
                echo "  [screencapture] Captured menu_${i}.png"
                sleep 2
            fi
        done
    ) &
    CAPTURE_PID=$!
fi

# Wait for engine to exit or timeout
WAIT_START=$(date +%s)
while kill -0 $ENGINE_PID 2>/dev/null; do
    NOW=$(date +%s)
    ELAPSED=$((NOW - WAIT_START))

    # Print progress dots
    if (( ELAPSED % 10 == 0 && ELAPSED > 0 )); then
        echo "  ... ${ELAPSED}s elapsed"
    fi

    if (( ELAPSED >= TIMEOUT )); then
        echo ""
        echo "  TIMEOUT after ${TIMEOUT}s — killing engine"
        kill -TERM $ENGINE_PID 2>/dev/null || true
        sleep 2
        kill -9 $ENGINE_PID 2>/dev/null || true
        echo "TIMEOUT" > "$RUN_PATH/exit_reason.txt"
        break
    fi
    sleep 1
done

# Collect exit status
wait $ENGINE_PID 2>/dev/null || true
EXIT_CODE=$?
echo "$EXIT_CODE" > "$RUN_PATH/exit_code.txt"

echo ""
echo "=== Collecting artifacts ==="

# With --isolation-dir, infolog/screenshots go to cont/
INFOLOG_SRC="$CONT_DIR/infolog.txt"
if [[ -f "$INFOLOG_SRC" ]]; then
    cp "$INFOLOG_SRC" "$RUN_PATH/"
    echo "  Copied infolog.txt ($(wc -l < "$RUN_PATH/infolog.txt") lines)"
else
    echo "  WARNING: No infolog.txt found"
fi

SCREENSHOTS_SRC="$CONT_DIR/screenshots"
if [[ -d "$SCREENSHOTS_SRC" ]]; then
    mkdir -p "$RUN_PATH/screenshots"
    cp "$SCREENSHOTS_SRC/"screen*.png "$RUN_PATH/screenshots/" 2>/dev/null || true
    SC_COUNT=$(ls "$RUN_PATH/screenshots/"*.png 2>/dev/null | wc -l || echo 0)
    echo "  Copied $SC_COUNT screenshots"
fi

# Generate summary
LOG="$RUN_PATH/infolog.txt"
{
    echo "# Test Run Summary: $RUN_ID"
    echo ""
    echo "## Exit"
    echo "- Code: $EXIT_CODE"
    if [[ -f "$RUN_PATH/exit_reason.txt" ]]; then
        echo "- Reason: $(cat "$RUN_PATH/exit_reason.txt")"
    fi
    echo ""

    if [[ -f "$LOG" ]]; then
        echo "## Data Directories"
        grep 'DataDir\|SpringData\|data.dir\|write.dir\|Using.*dir\|Isolation\|Portable' "$LOG" 2>/dev/null | head -10 || echo "(none)"
        echo ""

        echo "## Errors"
        ERR_COUNT=$(grep -c 'Error:\|FATAL\|SIGSEGV\|SIGABRT' "$LOG" 2>/dev/null) || ERR_COUNT=0
        echo "Count: $ERR_COUNT"
        echo '```'
        grep 'Error:\|FATAL\|SIGSEGV\|SIGABRT' "$LOG" 2>/dev/null | head -20 || echo "(none)"
        echo '```'
        echo ""

        echo "## Archive Scanner"
        grep -i 'archivescanner\|found.*archive\|scan.*dir' "$LOG" 2>/dev/null | head -10 || echo "(none)"
        echo ""

        echo "## Shader Compilation"
        echo "- Successes: $(grep -c 'Linked successfully' "$LOG" 2>/dev/null || true)"
        echo "- Failures: $(grep -c 'Failed to compile\|Failed to link\|failed to link' "$LOG" 2>/dev/null || true)"
        echo '```'
        grep 'Failed to compile\|Failed to link\|failed to link' "$LOG" 2>/dev/null | head -15 || echo "(none)"
        echo '```'
        echo ""

        echo "## Draw Calls"
        echo "- Draw: $(grep -c '\[MTL-Draw\]' "$LOG" 2>/dev/null || true)"
        echo "- DrawIndexed: $(grep -c '\[MTL-DrawIdx\]' "$LOG" 2>/dev/null || true)"
        echo ""

        echo "## Metal Pipeline (PSO)"
        grep '\[MTL-PSO\]' "$LOG" 2>/dev/null | head -10 || echo "(none)"
        echo ""

        echo "## Auto-Test Widget"
        grep '\[METAL-TEST\]' "$LOG" 2>/dev/null | tail -5 || echo "(not reached)"
        echo ""

        echo "## Screenshots"
        ls "$RUN_PATH/screenshots/"*.png 2>/dev/null | wc -l | tr -d ' '
        echo ""

        echo "## Warnings (first 20)"
        echo '```'
        grep 'Warning:' "$LOG" 2>/dev/null | head -20 || echo "(none)"
        echo '```'
    fi
} > "$RUN_PATH/summary.md"

# Symlink latest run
ln -sfn "$RUN_PATH" "$RUN_DIR/latest"

echo ""
echo "=== Run complete ==="
echo "  Exit code: $EXIT_CODE"
echo "  Run dir:   $RUN_PATH"
echo "  Latest:    $RUN_DIR/latest"
echo ""
cat "$RUN_PATH/summary.md"
