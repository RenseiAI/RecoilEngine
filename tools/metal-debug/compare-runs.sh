#!/usr/bin/env bash
# Compare two Metal debug test runs and output a structured delta.
#
# Usage:
#   compare-runs.sh                    # second-latest vs latest
#   compare-runs.sh BEFORE_DIR         # specified vs latest
#   compare-runs.sh BEFORE_DIR AFTER_DIR

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUNS_DIR="$SCRIPT_DIR/runs"

# --- Metric extraction ---

extract_metrics() {
    local RUN_DIR="$1"
    local LOG="$RUN_DIR/infolog.txt"

    if [[ ! -f "$LOG" ]]; then
        echo "ERROR: No infolog.txt in $RUN_DIR" >&2
        return 1
    fi

    local CRASHES SHADER_OK SHADER_FAILS PSO_FAILS DRAW_COUNT DRAWIDX_COUNT DRAW_CALLS
    local WIDGET_EXIT FPS EXIT_CODE EXIT_REASON EXIT_CLASS SCREENSHOTS GAME_SCREENSHOTS

    CRASHES=$(grep -c 'SIGSEGV\|SIGABRT\|SIGBUS\|SIGFPE\|SIGILL\|EXC_BAD_ACCESS\|std::terminate' "$LOG" 2>/dev/null) || CRASHES=0
    SHADER_OK=$(grep -c 'Linked successfully' "$LOG" 2>/dev/null) || SHADER_OK=0
    SHADER_FAILS=$(grep -c 'Failed to compile\|Failed to link\|failed to link\|glslang parse failed' "$LOG" 2>/dev/null) || SHADER_FAILS=0
    PSO_FAILS=$(grep -c 'GetRenderPipelineState returned nil\|Failed to create' "$LOG" 2>/dev/null) || PSO_FAILS=0
    DRAW_COUNT=$(grep -c '\[MTL-Draw\]' "$LOG" 2>/dev/null) || DRAW_COUNT=0
    DRAWIDX_COUNT=$(grep -c '\[MTL-DrawIdx\]' "$LOG" 2>/dev/null) || DRAWIDX_COUNT=0
    DRAW_CALLS=$((DRAW_COUNT + DRAWIDX_COUNT))

    if grep -q '\[METAL-TEST\] EXIT_SUCCESS' "$LOG" 2>/dev/null; then
        WIDGET_EXIT=1
    else
        WIDGET_EXIT=0
    fi

    FPS=$(grep '\[METAL-TEST\] DONE' "$LOG" 2>/dev/null | sed -n 's/.*fps=\([0-9.]*\).*/\1/p' | tail -1)
    FPS="${FPS:-0}"

    EXIT_CODE=""
    EXIT_REASON=""
    if [[ -f "$RUN_DIR/exit_code.txt" ]]; then
        EXIT_CODE=$(cat "$RUN_DIR/exit_code.txt")
    fi
    if [[ -f "$RUN_DIR/exit_reason.txt" ]]; then
        EXIT_REASON=$(cat "$RUN_DIR/exit_reason.txt")
    fi

    if [[ "$CRASHES" -gt 0 ]]; then
        EXIT_CLASS="CRASH"
    elif [[ "$EXIT_CODE" == "0" && "$EXIT_REASON" != "TIMEOUT" ]]; then
        EXIT_CLASS="CLEAN"
    elif [[ "$WIDGET_EXIT" -eq 1 && "$EXIT_REASON" == "TIMEOUT" ]]; then
        EXIT_CLASS="SHUTDOWN_HANG"
    elif [[ "$EXIT_REASON" == "TIMEOUT" ]]; then
        EXIT_CLASS="RUNTIME_HANG"
    else
        EXIT_CLASS="ERROR"
    fi

    if [[ -d "$RUN_DIR/screenshots" ]]; then
        SCREENSHOTS=$(ls "$RUN_DIR/screenshots/"*.png 2>/dev/null | wc -l | tr -d ' ')
        GAME_SCREENSHOTS=$(ls "$RUN_DIR/screenshots/"screen_*.png 2>/dev/null | wc -l | tr -d ' ')
    else
        SCREENSHOTS=0
        GAME_SCREENSHOTS=0
    fi

    # Output as key=value lines (machine-parseable)
    echo "CRASHES=$CRASHES"
    echo "SHADER_OK=$SHADER_OK"
    echo "SHADER_FAILS=$SHADER_FAILS"
    echo "PSO_FAILS=$PSO_FAILS"
    echo "DRAW_CALLS=$DRAW_CALLS"
    echo "WIDGET_EXIT=$WIDGET_EXIT"
    echo "FPS=$FPS"
    echo "EXIT_CLASS=$EXIT_CLASS"
    echo "SCREENSHOTS=$SCREENSHOTS"
    echo "GAME_SCREENSHOTS=$GAME_SCREENSHOTS"
}

# --- Resolve run directories ---

if [[ $# -eq 0 ]]; then
    # Compare second-latest vs latest
    if [[ ! -L "$RUNS_DIR/latest" ]]; then
        echo "ERROR: No 'latest' symlink in $RUNS_DIR"
        exit 1
    fi
    # Resolve symlink physically to get the actual timestamped directory
    AFTER_DIR="$(cd -P "$RUNS_DIR/latest" && pwd)"
    AFTER_NAME="$(basename "$AFTER_DIR")"

    # Find second-latest: list timestamped dirs, exclude the one latest points to
    BEFORE_DIR=$(ls -d "$RUNS_DIR"/20* 2>/dev/null | grep -v "$AFTER_NAME" | sort | tail -1)
    if [[ -z "$BEFORE_DIR" || ! -d "$BEFORE_DIR" ]]; then
        echo "ERROR: Cannot find a second run to compare against"
        exit 1
    fi
elif [[ $# -eq 1 ]]; then
    BEFORE_DIR="$1"
    if [[ ! -L "$RUNS_DIR/latest" ]]; then
        echo "ERROR: No 'latest' symlink in $RUNS_DIR"
        exit 1
    fi
    AFTER_DIR="$(cd -P "$RUNS_DIR/latest" && pwd)"
elif [[ $# -eq 2 ]]; then
    BEFORE_DIR="$1"
    AFTER_DIR="$2"
else
    echo "Usage: compare-runs.sh [BEFORE_DIR [AFTER_DIR]]"
    exit 1
fi

# Resolve paths (physical, follows symlinks)
BEFORE_DIR="$(cd -P "$BEFORE_DIR" && pwd)"
AFTER_DIR="$(cd -P "$AFTER_DIR" && pwd)"

echo "=== Comparing Runs ==="
echo "  BEFORE: $(basename "$BEFORE_DIR")"
echo "  AFTER:  $(basename "$AFTER_DIR")"
echo ""

# --- Extract metrics ---

BEFORE_METRICS=$(extract_metrics "$BEFORE_DIR")
AFTER_METRICS=$(extract_metrics "$AFTER_DIR")

# Parse into variables
get_val() { echo "$1" | grep "^$2=" | cut -d= -f2; }

B_CRASHES=$(get_val "$BEFORE_METRICS" CRASHES)
B_SHADER_OK=$(get_val "$BEFORE_METRICS" SHADER_OK)
B_SHADER_FAILS=$(get_val "$BEFORE_METRICS" SHADER_FAILS)
B_PSO_FAILS=$(get_val "$BEFORE_METRICS" PSO_FAILS)
B_DRAW_CALLS=$(get_val "$BEFORE_METRICS" DRAW_CALLS)
B_WIDGET_EXIT=$(get_val "$BEFORE_METRICS" WIDGET_EXIT)
B_FPS=$(get_val "$BEFORE_METRICS" FPS)
B_EXIT_CLASS=$(get_val "$BEFORE_METRICS" EXIT_CLASS)
B_SCREENSHOTS=$(get_val "$BEFORE_METRICS" SCREENSHOTS)
B_GAME_SCREENSHOTS=$(get_val "$BEFORE_METRICS" GAME_SCREENSHOTS)

A_CRASHES=$(get_val "$AFTER_METRICS" CRASHES)
A_SHADER_OK=$(get_val "$AFTER_METRICS" SHADER_OK)
A_SHADER_FAILS=$(get_val "$AFTER_METRICS" SHADER_FAILS)
A_PSO_FAILS=$(get_val "$AFTER_METRICS" PSO_FAILS)
A_DRAW_CALLS=$(get_val "$AFTER_METRICS" DRAW_CALLS)
A_WIDGET_EXIT=$(get_val "$AFTER_METRICS" WIDGET_EXIT)
A_FPS=$(get_val "$AFTER_METRICS" FPS)
A_EXIT_CLASS=$(get_val "$AFTER_METRICS" EXIT_CLASS)
A_SCREENSHOTS=$(get_val "$AFTER_METRICS" SCREENSHOTS)
A_GAME_SCREENSHOTS=$(get_val "$AFTER_METRICS" GAME_SCREENSHOTS)

# --- Delta computation ---

delta() {
    local b="$1" a="$2"
    local d=$((a - b))
    if [[ $d -gt 0 ]]; then echo "+$d"
    elif [[ $d -lt 0 ]]; then echo "$d"
    else echo "="
    fi
}

echo "## Metrics Comparison"
echo ""
printf "%-20s %10s %10s %10s\n" "Metric" "Before" "After" "Delta"
printf "%-20s %10s %10s %10s\n" "---" "---" "---" "---"
printf "%-20s %10s %10s %10s\n" "Crashes" "$B_CRASHES" "$A_CRASHES" "$(delta "$B_CRASHES" "$A_CRASHES")"
printf "%-20s %10s %10s %10s\n" "Shader OK" "$B_SHADER_OK" "$A_SHADER_OK" "$(delta "$B_SHADER_OK" "$A_SHADER_OK")"
printf "%-20s %10s %10s %10s\n" "Shader Fails" "$B_SHADER_FAILS" "$A_SHADER_FAILS" "$(delta "$B_SHADER_FAILS" "$A_SHADER_FAILS")"
printf "%-20s %10s %10s %10s\n" "PSO Fails" "$B_PSO_FAILS" "$A_PSO_FAILS" "$(delta "$B_PSO_FAILS" "$A_PSO_FAILS")"
printf "%-20s %10s %10s %10s\n" "Draw Calls" "$B_DRAW_CALLS" "$A_DRAW_CALLS" "$(delta "$B_DRAW_CALLS" "$A_DRAW_CALLS")"
printf "%-20s %10s %10s %10s\n" "FPS" "$B_FPS" "$A_FPS" "-"
printf "%-20s %10s %10s %10s\n" "Screenshots" "$B_SCREENSHOTS" "$A_SCREENSHOTS" "$(delta "$B_SCREENSHOTS" "$A_SCREENSHOTS")"
printf "%-20s %10s %10s %10s\n" "Game Screenshots" "$B_GAME_SCREENSHOTS" "$A_GAME_SCREENSHOTS" "$(delta "$B_GAME_SCREENSHOTS" "$A_GAME_SCREENSHOTS")"
echo ""

echo "## Exit Status"
echo "  Before: EXIT_CLASS=$B_EXIT_CLASS  WIDGET_EXIT=$B_WIDGET_EXIT"
echo "  After:  EXIT_CLASS=$A_EXIT_CLASS  WIDGET_EXIT=$A_WIDGET_EXIT"
echo ""

# --- Error diff ---

extract_errors() {
    local LOG="$1/infolog.txt"
    # Strip timestamp prefix [t=...][f=...] so identical errors with different times match
    grep 'Error:\|FATAL\|Failed to compile\|Failed to link\|failed to link\|GetRenderPipelineState returned nil' "$LOG" 2>/dev/null \
        | sed 's/^\[t=[^]]*\]\[f=[^]]*\] //' | sort -u
}

BEFORE_ERRORS=$(extract_errors "$BEFORE_DIR")
AFTER_ERRORS=$(extract_errors "$AFTER_DIR")

NEW_ERRORS=$(comm -13 <(echo "$BEFORE_ERRORS") <(echo "$AFTER_ERRORS"))
RESOLVED_ERRORS=$(comm -23 <(echo "$BEFORE_ERRORS") <(echo "$AFTER_ERRORS"))

echo "## New Errors (in AFTER, not BEFORE)"
if [[ -n "$NEW_ERRORS" ]]; then
    echo "$NEW_ERRORS" | head -20
else
    echo "(none)"
fi
echo ""

echo "## Resolved Errors (in BEFORE, not AFTER)"
if [[ -n "$RESOLVED_ERRORS" ]]; then
    echo "$RESOLVED_ERRORS" | head -20
else
    echo "(none)"
fi
echo ""

# --- Regression check ---

REGRESSIONS=0
IMPROVEMENTS=0

check_regression() {
    local name="$1" before="$2" after="$3" direction="$4"
    # direction: "lower_better" (crashes, fails) or "higher_better" (shader_ok, draws, fps)
    if [[ "$direction" == "lower_better" ]]; then
        if [[ "$after" -gt "$before" ]]; then
            echo "  REGRESSION: $name increased ($before -> $after)"
            REGRESSIONS=$((REGRESSIONS + 1))
        elif [[ "$after" -lt "$before" ]]; then
            IMPROVEMENTS=$((IMPROVEMENTS + 1))
        fi
    else
        if [[ "$after" -lt "$before" ]]; then
            echo "  REGRESSION: $name decreased ($before -> $after)"
            REGRESSIONS=$((REGRESSIONS + 1))
        elif [[ "$after" -gt "$before" ]]; then
            IMPROVEMENTS=$((IMPROVEMENTS + 1))
        fi
    fi
}

echo "## Regression Check"
check_regression "Crashes" "$B_CRASHES" "$A_CRASHES" "lower_better"
check_regression "Shader Fails" "$B_SHADER_FAILS" "$A_SHADER_FAILS" "lower_better"
check_regression "PSO Fails" "$B_PSO_FAILS" "$A_PSO_FAILS" "lower_better"
check_regression "Shader OK" "$B_SHADER_OK" "$A_SHADER_OK" "higher_better"
check_regression "Draw Calls" "$B_DRAW_CALLS" "$A_DRAW_CALLS" "higher_better"
check_regression "Game Screenshots" "$B_GAME_SCREENSHOTS" "$A_GAME_SCREENSHOTS" "higher_better"

# Exit class regression (CLEAN > SHUTDOWN_HANG > RUNTIME_HANG > CRASH > ERROR)
exit_rank() {
    case "$1" in
        CLEAN)         echo 4 ;;
        SHUTDOWN_HANG) echo 3 ;;
        RUNTIME_HANG)  echo 2 ;;
        ERROR)         echo 1 ;;
        CRASH)         echo 0 ;;
        *)             echo -1 ;;
    esac
}
B_RANK=$(exit_rank "$B_EXIT_CLASS")
A_RANK=$(exit_rank "$A_EXIT_CLASS")
if [[ "$A_RANK" -lt "$B_RANK" ]]; then
    echo "  REGRESSION: Exit class degraded ($B_EXIT_CLASS -> $A_EXIT_CLASS)"
    REGRESSIONS=$((REGRESSIONS + 1))
elif [[ "$A_RANK" -gt "$B_RANK" ]]; then
    IMPROVEMENTS=$((IMPROVEMENTS + 1))
fi

if [[ "$REGRESSIONS" -eq 0 && "$IMPROVEMENTS" -eq 0 ]]; then
    echo "  (no changes detected)"
fi
echo ""

# --- Final result ---

if [[ "$REGRESSIONS" -gt 0 ]]; then
    RESULT="REGRESSED"
elif [[ "$IMPROVEMENTS" -gt 0 ]]; then
    RESULT="IMPROVED"
else
    RESULT="NO_CHANGE"
fi

echo "## Result"
echo "RESULT=$RESULT (improvements=$IMPROVEMENTS regressions=$REGRESSIONS)"
echo ""
echo "=== Comparison complete ==="
