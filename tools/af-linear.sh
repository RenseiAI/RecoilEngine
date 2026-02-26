#!/usr/bin/env bash
# af-linear wrapper for RecoilEngine
# Resolves the af-linear binary from sibling repos or global install,
# loads credentials from .env.local, and passes through all arguments.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Load credentials if available (grep valid lines to handle spaces around =)
if [[ -f "$REPO_ROOT/.env.local" ]]; then
  set -a
  # shellcheck disable=SC1090
  eval "$(grep -E '^[A-Z_]+=' "$REPO_ROOT/.env.local" | grep -v '^#')"
  set +a
fi

# Resolve af-linear binary
AF_LINEAR=""

# Try sibling agent-fleet repo
SIBLING_FLEET="$REPO_ROOT/../../agent-fleet/node_modules/.bin/af-linear"
if [[ -x "$SIBLING_FLEET" ]]; then
  AF_LINEAR="$SIBLING_FLEET"
fi

# Try sibling agentfactory repo
if [[ -z "$AF_LINEAR" ]]; then
  SIBLING_AF="$REPO_ROOT/../../agentfactory/node_modules/.bin/af-linear"
  if [[ -x "$SIBLING_AF" ]]; then
    AF_LINEAR="$SIBLING_AF"
  fi
fi

# Fall back to global install
if [[ -z "$AF_LINEAR" ]]; then
  if command -v af-linear &>/dev/null; then
    AF_LINEAR="af-linear"
  fi
fi

if [[ -z "$AF_LINEAR" ]]; then
  echo "Error: af-linear not found." >&2
  echo "Looked in:" >&2
  echo "  $SIBLING_FLEET" >&2
  echo "  $SIBLING_AF" >&2
  echo "  \$PATH (global)" >&2
  exit 1
fi

exec "$AF_LINEAR" "$@"
