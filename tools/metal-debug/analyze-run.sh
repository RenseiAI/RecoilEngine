#!/usr/bin/env bash
# Analyze a completed test run and produce a structured report for sub-agents.
#
# Usage:
#   tools/metal-debug/analyze-run.sh [RUN_DIR]
#   tools/metal-debug/analyze-run.sh          # uses latest run

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUN_DIR="${1:-$SCRIPT_DIR/runs/latest}"

if [[ ! -d "$RUN_DIR" ]]; then
    echo "ERROR: Run directory not found: $RUN_DIR"
    exit 1
fi

# Resolve symlink
RUN_DIR="$(cd "$RUN_DIR" && pwd)"
LOG="$RUN_DIR/infolog.txt"
STDOUT_LOG="$RUN_DIR/stdout.log"

if [[ ! -f "$LOG" ]]; then
    echo "ERROR: No infolog.txt in $RUN_DIR"
    exit 1
fi

echo "=== Analyzing: $RUN_DIR ==="

# 1. Exit status
echo ""
echo "## Exit Status"
if [[ -f "$RUN_DIR/exit_code.txt" ]]; then
    echo "Code: $(cat "$RUN_DIR/exit_code.txt")"
fi
if [[ -f "$RUN_DIR/exit_reason.txt" ]]; then
    echo "Reason: $(cat "$RUN_DIR/exit_reason.txt")"
fi

# 2. Crash indicators
echo ""
echo "## Crashes / Fatal Errors"
grep -n 'SIGSEGV\|SIGABRT\|SIGBUS\|SIGFPE\|SIGILL\|Segmentation\|Assertion\|FATAL\|std::terminate\|EXC_BAD_ACCESS' "$LOG" 2>/dev/null | head -10 || echo "(none)"

# 3. Shader compilation results
echo ""
echo "## Shader Compilation"
echo "### Successes"
grep 'Linked successfully' "$LOG" 2>/dev/null | sed 's/.*\[MTLShader\] //' | head -20 || echo "(none)"
echo ""
echo "### Failures"
grep -A2 'Failed to compile\|Failed to link\|failed to link\|glslang parse failed' "$LOG" 2>/dev/null | head -30 || echo "(none)"

# 4. Uniform binding info
echo ""
echo "## Uniform Bindings (per shader)"
grep "uniform '" "$LOG" 2>/dev/null | head -40 || echo "(none)"

# 5. Buffer upload ranges
echo ""
echo "## Buffer Upload Ranges"
grep 'buffer upload ranges' "$LOG" 2>/dev/null | head -20 || echo "(none)"

# 6. PSO creation
echo ""
echo "## PSO State (sampled)"
grep '\[MTL-PSO-Detail\]' "$LOG" 2>/dev/null | head -20 || echo "(none)"
echo ""
echo "### PSO Failures"
grep 'GetRenderPipelineState returned nil\|Failed to create' "$LOG" 2>/dev/null | head -10 || echo "(none)"

# 6b. Shader Validation errors (from MTL_SHADER_VALIDATION, written to stderr -> stdout.log)
echo ""
echo "## Shader Validation Errors"
if [[ -f "$STDOUT_LOG" ]]; then
    SV_ERRORS=$(grep -c 'MTLShaderValidation\|Shader Validation Error\|Invalid .* access\|out of bounds\|non-resident\|nil texture' "$STDOUT_LOG" 2>/dev/null) || SV_ERRORS=0
    echo "Count: $SV_ERRORS"
    grep -A3 'MTLShaderValidation\|Shader Validation Error\|Invalid .* access\|out of bounds\|non-resident\|nil texture' "$STDOUT_LOG" 2>/dev/null | head -30 || echo "(none — was shader validation enabled?)"
else
    SV_ERRORS=0
    echo "(no stdout.log — was run-test.sh used?)"
fi

# 7. Texture binding
echo ""
echo "## Texture Binding (sampled)"
grep '\[MTL-Tex\]' "$LOG" 2>/dev/null | head -10 || echo "(none)"

# 8. Render pass info
echo ""
echo "## Render Passes"
echo "Default passes: $(grep -c 'BeginDefaultRenderPass\|RHI Default Render Pass' "$LOG" 2>/dev/null || true)"
echo "FBO passes: $(grep -c 'BeginRenderPass.*framebuffer\|RHI Render Pass' "$LOG" 2>/dev/null || true)"

# 9. Draw call counts
echo ""
echo "## Draw Calls"
echo "Draw: $(grep -c '\[MTL-Draw\]' "$LOG" 2>/dev/null || true)"
echo "DrawIndexed: $(grep -c '\[MTL-DrawIdx\]' "$LOG" 2>/dev/null || true)"
echo "ApplyPipelineState failures: $(grep -c 'ApplyPipelineState.*false\|!renderEncoder\|!pipeline\|!currentShader' "$LOG" 2>/dev/null || true)"

# 10. Viewport info
echo ""
echo "## Viewport"
grep '\[MTLContext\] SetViewport' "$LOG" 2>/dev/null | head -5 || echo "(none)"

# 11. Warnings (unique, by count)
echo ""
echo "## Top Warnings (unique)"
grep '\[Warning\]\|WARNING' "$LOG" 2>/dev/null | sort | uniq -c | sort -rn | head -15 || echo "(none)"

# 12. Metal-test widget output
echo ""
echo "## Auto-Test Widget"
grep '\[METAL-TEST\]\|\[METAL-SCREENSHOT\]' "$LOG" 2>/dev/null || echo "(widget not loaded or game didn't reach gameplay)"

# 13. Screenshots captured
echo ""
echo "## Screenshots"
if [[ -d "$RUN_DIR/screenshots" ]]; then
    ls -la "$RUN_DIR/screenshots/"*.png 2>/dev/null || echo "(none)"
else
    echo "(no screenshots directory)"
fi

# 14. Timing
echo ""
echo "## Timing"
grep 'Loading took\|LoadFinalize\|loading map\|game started' "$LOG" 2>/dev/null | head -5 || echo "(no timing data)"

# 15. Machine-readable status for automated debug loop
echo ""
echo "## Machine-Readable Status"

CRASHES=$(grep -c 'SIGSEGV\|SIGABRT\|SIGBUS\|SIGFPE\|SIGILL\|EXC_BAD_ACCESS\|std::terminate' "$LOG" 2>/dev/null) || CRASHES=0
SHADER_OK=$(grep -c 'Linked successfully' "$LOG" 2>/dev/null) || SHADER_OK=0
SHADER_FAILS=$(grep -c 'Failed to compile\|Failed to link\|failed to link\|glslang parse failed' "$LOG" 2>/dev/null) || SHADER_FAILS=0
PSO_FAILS=$(grep -c 'GetRenderPipelineState returned nil\|Failed to create' "$LOG" 2>/dev/null) || PSO_FAILS=0
if [[ -f "$STDOUT_LOG" ]]; then
    SV_ERRORS_STATUS=$(grep -c 'MTLShaderValidation\|Shader Validation Error\|Invalid .* access\|out of bounds\|non-resident\|nil texture' "$STDOUT_LOG" 2>/dev/null) || SV_ERRORS_STATUS=0
else
    SV_ERRORS_STATUS=0
fi
DRAW_COUNT=$(grep -c '\[MTL-Draw\]' "$LOG" 2>/dev/null) || DRAW_COUNT=0
DRAWIDX_COUNT=$(grep -c '\[MTL-DrawIdx\]' "$LOG" 2>/dev/null) || DRAWIDX_COUNT=0
DRAW_CALLS=$((DRAW_COUNT + DRAWIDX_COUNT))

# Widget exit status
if grep -q '\[METAL-TEST\] EXIT_SUCCESS' "$LOG" 2>/dev/null; then
    WIDGET_EXIT=1
else
    WIDGET_EXIT=0
fi

# FPS from widget DONE line
FPS=$(grep '\[METAL-TEST\] DONE' "$LOG" 2>/dev/null | sed -n 's/.*fps=\([0-9.]*\).*/\1/p' | tail -1)
FPS="${FPS:-0}"

# Exit classification
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
    # Widget reported success but process didn't exit cleanly — shutdown hang
    EXIT_CLASS="SHUTDOWN_HANG"
elif [[ "$EXIT_REASON" == "TIMEOUT" ]]; then
    EXIT_CLASS="RUNTIME_HANG"
else
    EXIT_CLASS="ERROR"
fi

# Clean exit: true for CLEAN or SHUTDOWN_HANG (game ran fine, hang is a separate issue)
if [[ "$EXIT_CLASS" == "CLEAN" || "$EXIT_CLASS" == "SHUTDOWN_HANG" ]]; then
    CLEAN_EXIT=1
else
    CLEAN_EXIT=0
fi

# Screenshots count (total and game-only)
if [[ -d "$RUN_DIR/screenshots" ]]; then
    SCREENSHOTS=$(ls "$RUN_DIR/screenshots/"*.png 2>/dev/null | wc -l | tr -d ' ')
    GAME_SCREENSHOTS=$(ls "$RUN_DIR/screenshots/"screen_*.png 2>/dev/null | wc -l | tr -d ' ')
else
    SCREENSHOTS=0
    GAME_SCREENSHOTS=0
fi

# Verdict: PASS when no crashes, no shader/PSO failures, clean exit, and game screenshots captured
if [[ "$CRASHES" -eq 0 && "$SHADER_FAILS" -eq 0 && "$PSO_FAILS" -eq 0 && "$CLEAN_EXIT" -gt 0 && "$GAME_SCREENSHOTS" -gt 0 ]]; then
    VERDICT="PASS"
else
    VERDICT="FAIL"
fi

echo "CRASHES=$CRASHES"
echo "SHADER_OK=$SHADER_OK"
echo "SHADER_FAILS=$SHADER_FAILS"
echo "PSO_FAILS=$PSO_FAILS"
echo "SV_ERRORS=$SV_ERRORS_STATUS"
echo "DRAW_CALLS=$DRAW_CALLS"
echo "WIDGET_EXIT=$WIDGET_EXIT"
echo "FPS=$FPS"
echo "EXIT_CLASS=$EXIT_CLASS"
echo "CLEAN_EXIT=$CLEAN_EXIT"
echo "SCREENSHOTS=$SCREENSHOTS"
echo "GAME_SCREENSHOTS=$GAME_SCREENSHOTS"
echo "VERDICT=$VERDICT"

echo ""
echo "=== Analysis complete ==="
