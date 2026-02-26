# Metal Debug Orchestrator — Automated Test-Fix-Repeat Loop

## Overview

This system automates Metal rendering debugging through a run → analyze → fix → build → repeat cycle,
using Claude Code sub-agents for parallel work. The human only needs to observe and approve.

## Directory Structure

```
tools/metal-debug/
├── run-test.sh          # Launch engine, capture logs/screenshots, enforce timeout
├── analyze-run.sh       # Extract structured diagnostics from a completed run
├── ORCHESTRATOR.md      # This file — instructions for the Claude agent
└── runs/
    ├── latest -> 20260226_143000/
    └── 20260226_143000/
        ├── infolog.txt          # Engine log
        ├── stdout.log           # stdout/stderr capture
        ├── exit_code.txt        # Process exit code
        ├── exit_reason.txt      # TIMEOUT or normal
        ├── springsettings.cfg   # Test config
        ├── summary.md           # Auto-generated summary
        ├── screenshots/         # PNG screenshots taken during run
        └── LuaUI/Widgets/       # Auto-test widget
```

## Orchestration Loop (for Claude Code)

### Step 1: Run the test
```bash
bash tools/metal-debug/run-test.sh --timeout 90 --frames 300 --resolution 800x600
```

Run this in the background. The engine will:
- Start in an 800x600 window (won't take over the screen)
- Use Metal backend
- Auto-load BAR game with NullAI opponent
- Take screenshots at frames 30, 90, 150, 210, 270
- Quit after 300 draw frames (or timeout at 90s)

### Step 2: Analyze the run
```bash
bash tools/metal-debug/analyze-run.sh
```

This extracts structured data from `runs/latest/infolog.txt`.

### Step 3: Dispatch parallel sub-agents

Based on the analysis, launch specialized agents IN PARALLEL:

| Agent Type | Trigger | Task |
|-----------|---------|------|
| `rhi-migrator` | Shader compilation failures | Fix GLSL→MSL translation issues |
| `metal-backend` | PSO creation failures | Fix pipeline state / vertex layout issues |
| `shader-translator` | MSL compile errors | Fix specific shader translation problems |
| `platform-fixer` | Crashes / hangs | Fix macOS-specific issues |
| `Explore` | Unknown rendering issues | Investigate specific symptoms in the codebase |

**Agent dispatch template:**

```
Agent 1 (shader-translator): "The following shaders failed MSL compilation: [list from analyze].
  Fix the shader translation. Here's the error: [error]. Here's the GLSL source: [path]."

Agent 2 (metal-backend): "PSO creation failed for shader '[name]' with error '[msg]'.
  The vertex layout is [layout]. Fix the pipeline descriptor construction."

Agent 3 (rhi-migrator): "The uniform '[name]' has vsBufferIndex=-1 fsBufIndex=-1.
  This means it wasn't found in SPIR-V reflection. Investigate why."
```

### Step 4: Build and verify
```bash
cmake --build build-arm64/ --target engine-legacy -j$(sysctl -n hw.ncpu)
```

Use `build-verifier` agent for this.

### Step 5: Repeat from Step 1

Continue until:
- Screenshots show visible terrain/units (not black)
- No shader compilation failures
- No PSO creation failures
- Auto-test widget reports stable FPS

## Key Log Patterns to Watch

| Pattern | Meaning | Action |
|---------|---------|--------|
| `[MTLShader] X: Linked successfully` | Shader compiled OK | Good |
| `[MTLShader] X: Failed to compile` | MSL won't compile | Fix shader translation |
| `GetRenderPipelineState returned nil` | PSO creation failed | Fix pipeline desc |
| `uniform 'X' NOT in reflection` | Missing uniform binding | Fix shader compiler reflection |
| `encoder=0x0` or `encoder=(null)` | No active render pass | Fix render pass lifecycle |
| `[METAL-TEST] EXIT_SUCCESS` | Clean test completion | Success |
| `TIMEOUT` in exit_reason.txt | Engine hung | Fix hang/deadlock |
| `SIGSEGV` / `SIGABRT` | Crash | Fix memory/lifetime bug |

## Screenshot Analysis

If screenshots exist, read them with the Read tool (it supports PNG images).
Compare against expected rendering:
- **All black**: Transform matrices wrong, or draws being skipped
- **Terrain visible but no units**: Model shader issue
- **Terrain + units but wrong colors**: Texture sampling issue
- **Correct rendering**: SUCCESS — move to performance optimization

## Parallel Work Strategy

Always maximize parallelism. Examples:

1. **While engine is running** (background): Read code, plan fixes for known issues
2. **After analysis**: Launch multiple fix agents simultaneously for independent bugs
3. **During build**: Analyze previous run's screenshots, update memory files
4. **Between iterations**: Compare screenshots across runs to track progress

## Configuration Reference

### Engine Flags
- `--metal-backend` — Use Metal instead of OpenGL
- `--window` — Force windowed mode
- `--write-dir PATH` — Redirect logs/screenshots to PATH
- `--config FILE` — Use specific springsettings.cfg
- `--safemode` — Disable problematic features

### Metal Environment Variables
- `MTL_DEBUG_LAYER=1` — Enable Metal validation (slower but catches API errors)
- `MTL_SHADER_VALIDATION=1` — Validate shader execution
- `METAL_CAPTURE=1` — (custom) Enable GPU trace capture

### Test Script
The game auto-starts via `cont/script.txt`:
- Map: Red Comet Remake 1.7
- Game: Beyond All Reason
- Player vs NullAI (minimal CPU load)
- MinimalSetup=1 (skip expensive UI)

---

## Linear-Driven Debug Loop

An automated debug loop where each test→fix→retest iteration runs as a separate Linear issue with a fresh agent context window. State passes between iterations via structured issue descriptions.

### Architecture

```
Governor (agent-fleet) scans Linear "Recoil-on-Metal" project
        ↓ dispatches Backlog issues
Worker Fleet (tools/start-worker-fleet.sh, CWD=RecoilEngine)
        ↓ creates worktree, runs agent session
Agent (.claude/agents/debug-loop.md)
        ↓ test → analyze → fix ONE thing → rebuild → retest → commit+push
        ↓
   VERDICT?
   ├─ FAIL → Create next iteration issue in Backlog → loop repeats
   └─ PASS → Done! No next issue created.
```

### Starting the Loop

```bash
# Create the seed issue (iteration 1)
bash tools/metal-debug/create-seed-issue.sh

# With known context from previous work
bash tools/metal-debug/create-seed-issue.sh --known-state "Black screen fixed, testing terrain"

# With a focus hint
bash tools/metal-debug/create-seed-issue.sh --focus "shader compilation failures"
```

### Launching the Worker Fleet

Workers must run from the RecoilEngine directory so worktrees, `.claude/agents/`, and build tools are available:

```bash
# Start with defaults from .env.local (5 workers, 1 agent each)
bash tools/start-worker-fleet.sh

# Dry run to verify config
bash tools/start-worker-fleet.sh --dry-run

# Override worker count
bash tools/start-worker-fleet.sh -w 3 -c 1
```

The fleet reads credentials from `.env.local` and resolves the `af-worker-fleet` binary from the sibling `agent-fleet` repo.

### Pausing and Resuming

- **Pause:** Add a `HOLD` comment on the latest Backlog issue. The governor skips issues with HOLD comments.
- **Resume:** Remove the HOLD comment. The governor dispatches on the next scan cycle.
- **Emergency stop:** Kill the worker fleet process (`Ctrl+C` or stop the terminal).

### Inspecting the Issue Chain

Each iteration issue links to the previous one via `PREVIOUS_ISSUE`. To see the full history:

1. Open the latest `debug-loop` issue in Linear
2. Follow `PREVIOUS_ISSUE` links back through the chain
3. Each issue contains: what was fixed, the commit hash, remaining issues, and machine-readable metrics

### Machine-Readable Verdict

`analyze-run.sh` outputs a `## Machine-Readable Status` section:

```
CRASHES=0          # Fatal signals (SIGSEGV, SIGABRT, etc.)
SHADER_FAILS=12    # Shader compilation/link failures
PSO_FAILS=0        # Pipeline state object creation failures
DRAW_CALLS=55      # Total Metal draw calls executed
CLEAN_EXIT=0       # 1 if exit code 0 and no timeout, else 0
SCREENSHOTS=0      # Number of screenshots captured
VERDICT=FAIL       # PASS only when all checks pass
```

**VERDICT=PASS** requires: `CRASHES=0`, `SHADER_FAILS=0`, `PSO_FAILS=0`, `CLEAN_EXIT>0`, `SCREENSHOTS>0`.

### Termination Criteria

The loop stops automatically when `VERDICT=PASS`. This means:
- No crashes
- All shaders compile successfully
- All PSOs create successfully
- Engine exits cleanly
- Screenshots are captured (game reached rendering)

### Design Decisions

- **One fix per iteration:** Keeps context lean, each fix gets its own commit (easy bisection), clean audit trail in Linear.
- **Direct push (no PR):** Debug iterations are small incremental fixes. The retest IS the QA. Full PR→QA→acceptance adds latency.
- **State in issue descriptions:** No shared filesystem between agent sessions (worktrees are ephemeral). Linear issues are the canonical state store.
- **Separate worker fleet:** Workers must run from the RecoilEngine repo so worktrees have access to the build system and agent prompts.
