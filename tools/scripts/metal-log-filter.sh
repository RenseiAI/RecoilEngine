#!/bin/bash
# metal-log-filter.sh — Extract crash-relevant lines from spring engine logs
#
# Usage:
#   ./tools/scripts/metal-log-filter.sh [logfile]
#   ./tools/scripts/metal-log-filter.sh              # reads cont/infolog.txt
#   spring --metal-backend 2>&1 | ./tools/scripts/metal-log-filter.sh -
#
# Shows: crashes, Metal/RHI messages, real errors, loading stage progress
# Hides: streflop, thread affinity, game content warnings, VFS, sound, models

set -euo pipefail

LOGFILE="${1:-cont/infolog.txt}"

if [ "$LOGFILE" = "-" ]; then
    INPUT="/dev/stdin"
else
    if [ ! -f "$LOGFILE" ]; then
        echo "Error: $LOGFILE not found" >&2
        exit 1
    fi
    INPUT="$LOGFILE"
fi

# Include → Exclude pipeline
grep -n -E \
    -e 'Error[]:)]|Warning[]:)]|Fatal[]:)]' \
    -e 'SEGV|SIGSEGV|SIGABRT|SIGBUS|SIGILL' \
    -e 'Segmentation|has crashed' \
    -e 'Stack trace|Stacktrace|frame #' \
    -e 'SetLoadMessage' \
    -e 'InitPools' \
    -e 'Switching to' \
    -e '\[MTL|\[Metal\]|\[RHI\]' \
    -e 'shader.*not found|shader.*fail' \
    "$INPUT" 2>/dev/null \
| grep -v -E \
    -e 'streflop' \
    -e 'thread.*affinity|CPU affinity' \
    -e 'IsPathOnSpinningDisk' \
    -e 'Loading (sounds|model|widget|gadget)' \
    -e '\[CSound\]|\[VFS\]|\[SpringVFS\]' \
    -e 'ScanAllDirs|LOG_SECTION' \
    -e 'ShaderCompiler.*TranslateSPIRV' \
    -e 'logical processors|Core Mask|Thread.*Mask|Cache Mask' \
    -e 'Texture swizzle not supported' \
    -e 'StartScript' \
    -e 'UDPListener.*loopback' \
    -e 'WeaponDefs.*Unknown tag' \
    -e 'CreateYardMap.*extra char' \
    -e 'could not find FeatureDef' \
    -e 'Removing feature def.*crash the engine' \
    -e 'Failed to load icon=' \
    -e 'Could not load primary texture\|could not load primary texture' \
    -e 'LoadAndCacheTexture.*could not load' \
    -e '^\s*$' \
2>/dev/null || true

# Summary
if [ "$LOGFILE" != "-" ] && [ -f "$LOGFILE" ]; then
    echo ""
    echo "--- Summary ---"
    E=$(grep -c 'Error[]:)]' "$INPUT" 2>/dev/null || echo 0)
    W=$(grep -c 'Warning[]:)]' "$INPUT" 2>/dev/null || echo 0)
    F=$(grep -c 'Fatal[]:)]' "$INPUT" 2>/dev/null || echo 0)
    CRASH=$(grep -c 'SIGSEGV\|SIGABRT\|Segmentation' "$INPUT" 2>/dev/null || echo 0)
    T=$(wc -l < "$INPUT" | tr -d ' ')
    echo "Lines: $T | Errors: $E | Warnings: $W | Fatal: $F | Crashes: $CRASH"
fi
