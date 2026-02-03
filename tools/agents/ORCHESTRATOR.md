# Agent Orchestration System

Specialized Claude Code agents for the ARM64+Metal port, designed for maximum parallelism.

## Architecture

Each agent is defined as a prompt file in `tools/agents/prompts/`. Agents are invoked
via Claude Code's `Task` tool with `subagent_type=Bash` or `subagent_type=general-purpose`.

Agents are organized into **tiers** based on dependencies. All agents within a tier
can run in parallel (single message, multiple Task tool calls). A tier must complete
before the next tier begins.

## Dependency Graph

```
Tier 1 (Phase 1 - ARM64 headless)
├── build-system        CMake ARM64 support
├── lib-patcher         streflop, simdjson, squish fixes
└── platform-mac        macOS platform code (CpuTopology, etc.)
    │
    ▼ [Headless build must succeed]
Tier 2 (Phases 2+3 foundations - can all run in parallel)
├── rhi-architect       Design + create RHI interface headers
├── gl-backend          OpenGL backend wrapping existing code
└── shader-pipeline     glslang/SPIRV-Cross integration
    │
    ▼ [RHI interfaces + GL backend must exist]
Tier 3 (Phase 2 migrations - all run in parallel, each owns specific files)
├── migrate-core        RenderBuffers, VBO, VAO, StreamBuffer, FBO, State
├── migrate-water       Water renderers (5 files)
├── migrate-sky         Sky renderers + cubemap (3 files)
├── migrate-terrain     Grass, decals, map rendering (10+ files)
├── migrate-models      Unit/model rendering (6 files)
├── migrate-effects     Particles, HUD, icons, debug (10+ files)
├── migrate-textures    Texture management (9 files)
├── migrate-fonts       Font rendering (2 files)
├── migrate-shaders     Shader system (3 files)
└── migrate-toplevel    WorldDrawer, GlobalRendering, GL utilities
    │
    ▼ [All migrations complete]
Tier 4 (Phases 3+4+5 - can all run in parallel)
├── metal-backend       Metal RHI implementation
├── shader-translator   Translate all 41 GLSL shaders to MSL
└── lua-migrator        Lua GL bindings -> RHI
    │
    ▼ [All backends working]
Tier 5 (Phase 6)
├── ci-pipeline         GitHub Actions, build scripts
├── app-bundle          macOS packaging
└── auditor             Final verification pass
```

## File Ownership Rules

**Critical:** Agents must ONLY modify files they own. This prevents merge conflicts
when running in parallel. Each agent's owned files are listed in its definition.

If an agent needs to modify a file it doesn't own, it should:
1. Document the needed change in a `changes-needed.md` file in its output
2. The orchestrator will resolve cross-cutting changes after the tier completes

## How to Run a Tier

### Example: Run all Tier 1 agents in parallel

In Claude Code, send a single message with 3 Task tool calls:

```
Task 1: subagent_type=general-purpose, prompt=<contents of prompts/build-system.md>
Task 2: subagent_type=general-purpose, prompt=<contents of prompts/lib-patcher.md>
Task 3: subagent_type=general-purpose, prompt=<contents of prompts/platform-mac.md>
```

### Example: Run all Tier 3 migration agents in parallel

Send a single message with up to 10 Task tool calls, one per migration agent.
Each operates on its own set of files with no overlap.

## Agent Output Protocol

Every agent must:
1. Create a branch from the current `arm64-metal-port` HEAD
   - Branch name: `agent/<agent-name>` (e.g., `agent/migrate-water`)
2. Make atomic, well-described commits for each logical change
3. On completion, output a summary of:
   - Files created/modified
   - Any cross-cutting changes needed (files outside ownership)
   - Any blockers or issues discovered
   - Build/test status if applicable

## Merge Protocol

After a tier completes:
1. Review each agent branch
2. Merge branches into `arm64-metal-port` one at a time
3. Resolve any conflicts (should be rare given file ownership)
4. Run build verification before starting next tier
