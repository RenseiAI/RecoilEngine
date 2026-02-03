# Agent Runbook

Step-by-step instructions for running the agent tiers in Claude Code.

## Prerequisites

- Working directory: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`
- Branch: `arm64-metal-port`
- All previous tiers merged before starting the next

---

## Tier 1: ARM64 Headless Build

**Goal:** Engine compiles in headless mode on macOS ARM64.
**Parallelism:** 3 agents running simultaneously.
**Estimated subagent turns:** 15-25 each.

### Invocation

Ask Claude Code to run these 3 agents in parallel in a single message:

```
Run these 3 agents in parallel:

Agent 1 - build-system:
Read tools/agents/prompts/build-system.md and execute all tasks described.
Create branch agent/build-system, make the changes, commit each logical change.

Agent 2 - lib-patcher:
Read tools/agents/prompts/lib-patcher.md and execute all tasks described.
Create branch agent/lib-patcher, make the changes, commit each logical change.

Agent 3 - platform-mac:
Read tools/agents/prompts/platform-mac.md and execute all tasks described.
Create branch agent/platform-mac, make the changes, commit each logical change.
```

### After Tier 1 Completes

1. Review each agent branch
2. Merge into `arm64-metal-port`:
```bash
git checkout arm64-metal-port
git merge agent/build-system
git merge agent/lib-patcher
git merge agent/platform-mac
```
3. Resolve any cross-cutting changes documented by agents
4. Run build-verifier agent:
```
Read tools/agents/prompts/build-verifier.md and attempt to build the engine
on macOS ARM64. Report all errors categorized by type.
```

---

## Tier 2: RHI Foundation

**Goal:** RHI interfaces, OpenGL backend, and shader pipeline exist.
**Parallelism:** 3 agents running simultaneously.
**Dependency:** Tier 1 merged.

### Invocation

```
Run these 3 agents in parallel:

Agent 1 - rhi-architect:
Read tools/agents/prompts/rhi-architect.md and execute all tasks described.
Create branch agent/rhi-architect, create all RHI interface headers, commit.

Agent 2 - shader-pipeline:
Read tools/agents/prompts/shader-pipeline.md and execute all tasks described.
Create branch agent/shader-pipeline, integrate glslang and SPIRV-Cross,
create ShaderCompiler, commit.

Agent 3 - gl-backend:
Read tools/agents/prompts/rhi-architect.md first to understand the RHI interfaces,
then read tools/agents/prompts/gl-backend.md and execute all tasks.
Create branch agent/gl-backend, implement OpenGL backend, commit.
```

**Note:** The gl-backend agent needs to read rhi-architect's output. If running truly
in parallel, gl-backend should read the RHI interface specs from rhi-architect.md
and implement against that specification. Alternatively, run rhi-architect first,
then gl-backend and shader-pipeline in parallel.

### After Tier 2 Completes

1. Merge rhi-architect first (other branches may depend on its headers)
2. Merge shader-pipeline and gl-backend
3. Run build-verifier

---

## Tier 3: Subsystem Migration

**Goal:** All rendering subsystems use RHI instead of direct GL.
**Parallelism:** Up to 10 agents simultaneously (maximum parallelism point).
**Dependency:** Tier 2 merged.

### Invocation

```
Run these agents in parallel (send in a single message):

Agent 1 - migrate-core:
Read tools/agents/prompts/migrate-template.md and tools/agents/prompts/migrate-core.md.
Execute all tasks. Branch: agent/migrate-core

Agent 2 - migrate-water:
Read tools/agents/prompts/migrate-template.md and tools/agents/prompts/migrate-water.md.
Execute all tasks. Branch: agent/migrate-water

Agent 3 - migrate-sky:
Read tools/agents/prompts/migrate-template.md and tools/agents/prompts/migrate-sky.md.
Execute all tasks. Branch: agent/migrate-sky

Agent 4 - migrate-terrain:
Read tools/agents/prompts/migrate-template.md and tools/agents/prompts/migrate-terrain.md.
Execute all tasks. Branch: agent/migrate-terrain

Agent 5 - migrate-models:
Read tools/agents/prompts/migrate-template.md and tools/agents/prompts/migrate-models.md.
Execute all tasks. Branch: agent/migrate-models

Agent 6 - migrate-effects:
Read tools/agents/prompts/migrate-template.md and tools/agents/prompts/migrate-effects.md.
Execute all tasks. Branch: agent/migrate-effects

Agent 7 - migrate-textures:
Read tools/agents/prompts/migrate-template.md and tools/agents/prompts/migrate-textures.md.
Execute all tasks. Branch: agent/migrate-textures

Agent 8 - migrate-fonts:
Read tools/agents/prompts/migrate-template.md and tools/agents/prompts/migrate-fonts.md.
Execute all tasks. Branch: agent/migrate-fonts

Agent 9 - migrate-shaders:
Read tools/agents/prompts/migrate-template.md and tools/agents/prompts/migrate-shaders.md.
Execute all tasks. Branch: agent/migrate-shaders

Agent 10 - migrate-toplevel:
Read tools/agents/prompts/migrate-template.md and tools/agents/prompts/migrate-toplevel.md.
Execute all tasks. Branch: agent/migrate-toplevel
```

**Important ordering note:** `migrate-core` should merge first since other subsystems
depend on the core GL classes (RenderBuffers, VBO, FBO, State). If agents run truly
in parallel, they should assume the core classes will be available via RHI and code
against the RHI interfaces rather than the old GL classes.

### After Tier 3 Completes

1. Merge `agent/migrate-core` first
2. Merge remaining migration branches (order doesn't matter)
3. Resolve any cross-cutting conflicts
4. Run auditor agent to verify no GL calls remain
5. Run build-verifier

---

## Tier 4: Metal Backend + Lua + Shaders

**Goal:** Metal rendering works, Lua API works through RHI, all shaders translated.
**Parallelism:** 3 agents simultaneously.
**Dependency:** Tier 3 merged.

### Invocation

```
Run these 3 agents in parallel:

Agent 1 - metal-backend:
Read tools/agents/prompts/metal-backend.md and execute all tasks.
Branch: agent/metal-backend

Agent 2 - lua-migrator:
Read tools/agents/prompts/lua-migrator.md and execute all tasks.
Branch: agent/lua-migrator

Agent 3 - shader-translator:
Read tools/agents/prompts/shader-translator.md and execute all tasks.
Branch: agent/shader-translator
```

### After Tier 4 Completes

1. Merge all three branches
2. Run full auditor scan
3. Run build-verifier for both OpenGL and Metal targets
4. Test rendering on Metal

---

## Tier 5: Polish

**Goal:** CI, packaging, final verification.
**Dependency:** Tier 4 merged.

This tier is typically done sequentially or with 2-3 small agents.

---

## Tips for Maximum Throughput

1. **Always send parallel agents in a single message.** Multiple Task tool calls in
   one message = true parallelism.

2. **Use background mode for long-running agents.** Add `run_in_background: true` to
   the Task tool call, then check progress with `Read` on the output file.

3. **Don't wait for all agents.** If one agent finishes early and doesn't conflict
   with the next tier's agents, you can start the next tier's non-conflicting agents.

4. **Run build-verifier frequently.** It's read-only and fast. Run it between tiers
   and even mid-tier to catch issues early.

5. **Agent branches isolate work.** Even if an agent makes a mistake, it's on its own
   branch. Review before merging.

6. **Cross-cutting changes.** If multiple agents need to modify the same file, have
   them document the needed change instead of making it. Resolve after merging.
