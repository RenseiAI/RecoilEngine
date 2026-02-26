#!/usr/bin/env bash
# Launch an af-worker-fleet from the RecoilEngine repo directory.
# Workers run with RecoilEngine as CWD so worktrees, .claude/agents/,
# and build tools are all available to dispatched agent sessions.
#
# Usage:
#   tools/start-worker-fleet.sh              # start fleet with .env.local defaults
#   tools/start-worker-fleet.sh --dry-run    # show config without starting
#   tools/start-worker-fleet.sh -w 3 -c 1   # 3 workers, 1 agent each

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Load credentials from .env.local (same pattern as af-linear.sh)
if [[ -f "$REPO_ROOT/.env.local" ]]; then
  set -a
  # shellcheck disable=SC1090
  eval "$(grep -E '^[A-Z_]+=' "$REPO_ROOT/.env.local" | grep -v '^#')"
  set +a
fi

# Resolve af-worker-fleet binary
AF_FLEET=""

# Try sibling agent-fleet repo
SIBLING_FLEET="$REPO_ROOT/../../agent-fleet/node_modules/.bin/af-worker-fleet"
if [[ -x "$SIBLING_FLEET" ]]; then
  AF_FLEET="$SIBLING_FLEET"
fi

# Try sibling agentfactory repo
if [[ -z "$AF_FLEET" ]]; then
  SIBLING_AF="$REPO_ROOT/../../agentfactory/node_modules/.bin/af-worker-fleet"
  if [[ -x "$SIBLING_AF" ]]; then
    AF_FLEET="$SIBLING_AF"
  fi
fi

# Fall back to global install
if [[ -z "$AF_FLEET" ]]; then
  if command -v af-worker-fleet &>/dev/null; then
    AF_FLEET="af-worker-fleet"
  fi
fi

if [[ -z "$AF_FLEET" ]]; then
  echo "Error: af-worker-fleet not found." >&2
  echo "Looked in:" >&2
  echo "  $SIBLING_FLEET" >&2
  echo "  $SIBLING_AF" >&2
  echo "  \$PATH (global)" >&2
  exit 1
fi

echo "=== RecoilEngine Worker Fleet ==="
echo "  Binary:  $AF_FLEET"
echo "  CWD:     $REPO_ROOT"
echo "  Project: ${WORKER_PROJECTS:-all}"
echo ""

# Run from repo root so worktrees are created here
cd "$REPO_ROOT"
exec "$AF_FLEET" "$@"
