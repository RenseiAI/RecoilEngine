# Interactive Metal Debug Loop Protocol

This document defines the structured iteration loop for debugging Metal rendering.
Each iteration targets ONE symptom, applies ONE fix, and measures the result.

## The Loop

```
1. ASSESS    Read analyze-run.sh output + screenshots, consult priority queue
2. DISPATCH  Send ONE sub-agent with narrow prompt (symptom + logs + files + constraint)
3. VERIFY    Check git diff --stat to confirm changes persisted
4. BUILD     cmake --build build-arm64/ --target engine-legacy -j$(sysctl -n hw.ncpu)
5. TEST      tools/metal-debug/run-test.sh
6. ANALYZE   tools/metal-debug/analyze-run.sh
7. COMPARE   tools/metal-debug/compare-runs.sh
8. DECIDE    IMPROVED -> commit + keep; REGRESSED -> git checkout -- <files>
9. UPDATE    Update iteration-log.md with results
10. RESET    After 3-5 iterations, save state and consider context reset
```

## Symptom-to-Agent Routing

| Symptom | Agent Type | Focus |
|---------|-----------|-------|
| Crash (SIGSEGV, SIGABRT) | `platform-fixer` | macOS-specific crash/hang fixes |
| Shader compilation failure | `shader-translator` | GLSL -> MSL translation issues |
| PSO creation failure | `metal-backend` | Pipeline descriptor, vertex layout, format |
| Black screen / missing geometry | `metal-backend` | Transform matrices, framebuffer present, viewport |
| Visual artifacts | `metal-backend` or `shader-translator` | Texture sampling, blending, winding |
| Shutdown hang | `platform-fixer` | Thread join, Metal cleanup, autorelease |
| Cosmetic warnings | `developer` | Font paths, texture swizzle, non-critical |

## Dispatch Prompt Template

A good dispatch prompt includes:
1. **Symptom** — One sentence describing what's wrong
2. **Evidence** — Specific log lines, error messages, metric values
3. **Files** — Which files to investigate (narrow scope)
4. **Constraint** — What NOT to change (preserve existing behavior)

### Example: Black Screen

```
Symptom: Engine renders 37 draw calls (10 Draw + 27 DrawIndexed) but the screen
is black except for a faint sky gradient in the upper-right corner.

Evidence:
- 84 shaders compile successfully, 0 PSO failures
- Widget reports 48.9 FPS with EXIT_SUCCESS
- Draw calls are issued (MTL-Draw/MTL-DrawIdx logging confirmed)
- Screenshots show black + faint sky gradient

Files to investigate:
- rts/Rendering/RHI/Metal/MTLContext.mm (BeginDefaultRenderPass, present logic)
- rts/Rendering/RHI/Metal/MTLDevice.mm (SetupWindowIntegration, drawable acquisition)
- rts/Rendering/GL/RenderBuffers.cpp (EndFrame/SwapBuffers flow)
- rts/Rendering/GlobalRendering.cpp (viewport setup)

Constraint: Do not modify the OpenGL backend. Only change Metal-specific code paths.
```

### Example: Shader Failure

```
Symptom: Shader "tree_shadow" fails MSL compilation with error "use of undeclared
identifier 'gl_FogFragCoord'".

Evidence:
- Log line: [MTLShader] Failed to compile tree_shadow: ...
- The shader uses gl_PerVertex builtin gl_FogFragCoord
- FixGlPerVertexDeclarations() in ShaderCompiler.cpp should handle this

Files to investigate:
- rts/Rendering/Shaders/ShaderCompiler.cpp (FixGlPerVertexDeclarations)
- cont/base/shaders/GLSL/tree_shadow.glsl (source shader)

Constraint: Fix must work for all shaders using gl_FogFragCoord, not just tree_shadow.
```

## Decision Rules

### IMPROVED
- Commit changes: `git add <files> && git commit -m "Phase N: <description>"`
- Update iteration-log.md with new metrics
- Move to next priority item if current one is resolved

### REGRESSED
- Revert changes: `git checkout -- <files>`
- Log the failed approach in iteration-log.md
- Re-assess with different approach or escalate

### NO_CHANGE
- If the fix compiled but had no effect, the diagnosis was wrong
- Re-read logs more carefully, consider different root cause
- Log findings in iteration-log.md

## Context Management

After 3-5 iterations, context may become large. To reset:
1. Update iteration-log.md with current state
2. Commit any pending changes
3. The next conversation starts fresh, reads iteration-log.md for state

## Quick Reference

```bash
# Build
cmake --build build-arm64/ --target engine-legacy -j$(sysctl -n hw.ncpu)

# Test
tools/metal-debug/run-test.sh

# Analyze latest run
tools/metal-debug/analyze-run.sh

# Compare latest two runs
tools/metal-debug/compare-runs.sh

# Compare specific runs
tools/metal-debug/compare-runs.sh runs/20260226_201844 runs/20260226_201955
```
