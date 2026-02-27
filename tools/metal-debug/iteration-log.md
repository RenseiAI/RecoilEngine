# Metal Debug Iteration Log

## Current Status
- Last iteration: 1
- Commit: (pending)
- Shaders: 82/82, Draw calls: 21, FPS: 49.4, PSO fails: 2400
- Exit: SHUTDOWN_HANG (widget EXIT_SUCCESS, process timeout at Kill[3])
- Visual: Black screen + sky gradient upper-right (unchanged — Lua draws now attempt PSO but fail)

## Priority Queue
1. PSO FAILURE — All Lua shader draws fail: "Fragment input(s) `user(locn1)` mismatching vertex shader output type(s) or not written by vertex shader". Vertex layout has 1 attrib (loc=0, stride=16) but frag shader expects locn1 output from vertex shader.
2. SHUTDOWN_HANG — hangs at SpringApp::Kill[3] after widget exit
3. Font path error — doubled path in font loading
4. Texture swizzle warnings — incorrect texture format handling
5. ModernSky disabled — falls back to NullSky

## Iteration History

### Iteration 0 — 2026-02-27 (baseline)
- Run: 20260227_104947
- Commit: 2b0d624ad4
- Metrics: shaders=82/82 draws=21 fps=49.0 exit=SHUTDOWN_HANG screenshots=7(7 game)
- Visual: Black screen + faint sky gradient upper-right quadrant
- Errors: ModernSky fallback, FeatureDef missing (corfast/corhack_dead), font path doubled, SkirmishAI unknown
- Notes: Engine loads BAR, enters gameplay, runs stable 300 frames at 49 FPS. All rendering
  infrastructure works (shaders compile, PSOs create, draw calls issue) but final
  framebuffer content is not visible. Sky gradient visible only in upper-right suggests
  viewport/scissor issue or framebuffer presentation problem. Shader/draw counts vary
  slightly between runs (82-84 shaders, 21-37 draws) due to non-deterministic game state.

### Iteration 1 — 2026-02-27 (LuaShaders BindShader fix)
- Run: 20260227_110312
- Commit: (pending)
- Metrics: shaders=82/82 draws=21 fps=49.4 pso_fails=2400 exit=SHUTDOWN_HANG screenshots=7(7 game)
- Visual: Unchanged (black + sky gradient) — draws now reach PSO step but fail
- Fix: Added `ctx->BindShader()` calls to `LuaShaders::UseShader()` and `LuaShaders::ActiveShader()`
  on Metal path. Previously, `rhiShader->Bind()` was called but `MTLContext::currentShader` was
  never set, causing ALL Lua widget draws to be silently skipped (ApplyPipelineState returned false).
- Result: Draws now correctly bind shader to Metal context (`shader=lua_shader` instead of `null`).
  But PSO creation fails for all Lua shaders: "Fragment input(s) `user(locn1)` mismatching
  vertex shader output type(s) or not written by vertex shader". Only 1 vertex attribute
  (loc=0, fmt=Float4, stride=16) provided but fragment shader expects locn1 from vertex shader.
- Next: Investigate Lua shader MSL vertex/fragment interface. The vertex shader must output
  locn1 (texcoords) but either SPIRV-Cross translation is dropping it, or the vertex layout
  needs additional attributes so Metal can match inputs.
