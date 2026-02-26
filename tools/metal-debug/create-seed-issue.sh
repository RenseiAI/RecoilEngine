#!/usr/bin/env bash
# Create the initial Linear issue that starts the Metal debug loop.
#
# Usage:
#   tools/metal-debug/create-seed-issue.sh
#   tools/metal-debug/create-seed-issue.sh --known-state "Black screen fixed, testing terrain"
#   tools/metal-debug/create-seed-issue.sh --focus "shader compilation failures"

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
AF_LINEAR="$REPO_ROOT/tools/af-linear.sh"

KNOWN_STATE="Fresh start — no previous iterations."
FOCUS_HINT="Run test and triage highest-priority issue."

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --known-state) KNOWN_STATE="$2"; shift 2 ;;
        --focus)       FOCUS_HINT="$2"; shift 2 ;;
        *)             echo "Unknown arg: $1"; exit 1 ;;
    esac
done

COMMIT_HASH=$(cd "$REPO_ROOT" && git rev-parse --short HEAD)
BRANCH=$(cd "$REPO_ROOT" && git branch --show-current)

BODY="$(cat <<ISSUE_EOF
## Metal Debug Loop — Iteration 1

**Agent prompt:** \`.claude/agents/debug-loop.md\`

### Iteration State
- ITERATION: 1
- PREVIOUS_ISSUE: none
- PREVIOUS_COMMIT: ${COMMIT_HASH} (branch: ${BRANCH})
- PREVIOUS_FIX: none
- KNOWN_STATE: ${KNOWN_STATE}
- FOCUS_HINT: ${FOCUS_HINT}

### Remaining Issues
No previous analysis — agent should run test and analyze from scratch.

### Machine-Readable Status (from last run)
(no previous run)
ISSUE_EOF
)"

echo "Creating seed issue in Recoil-on-Metal..."
echo ""
bash "$AF_LINEAR" create-issue \
  --project "Recoil-on-Metal" \
  --title "Metal debug loop — iteration 1" \
  --label "debug-loop" \
  --status "Backlog" \
  --body "$BODY"
