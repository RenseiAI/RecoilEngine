# Metal Debug Iteration Log

## Current Status
- Last iteration: 2
- Commit: (pending)
- Shaders: 82/82, Draw calls: 21, FPS: 49.2, PSO fails: 0
- Exit: SHUTDOWN_HANG (widget EXIT_SUCCESS, process timeout)
- Visual: Black screen + sky gradient upper-right (unchanged visually)

## Priority Queue
1. BLACK SCREEN — 21 draw calls succeed (0 PSO failures) but framebuffer content not visible. Sky gradient in upper-right suggests viewport/scissor or framebuffer presentation issue. Investigate BeginDefaultRenderPass, present logic, viewport setup.
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
- Commit: 227557134b
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

### Iteration 2 — 2026-02-27 (gl_PerVertex output location fix)
- Run: 20260227_112255
- Commit: (pending)
- Metrics: shaders=82/82 draws=21 fps=49.2 pso_fails=0 exit=SHUTDOWN_HANG screenshots=7(7 game)
- Visual: Unchanged (black + sky gradient) — PSO failures resolved, draws succeed
- Fix: Added `FixGlPerVertexOutputLocations()` in MTLShader.mm. SPIRV-Cross generates
  `[[user(locnN)]]` qualifiers on fragment input gl_PerVertex builtins (gl_Color,
  gl_TexCoord, gl_FogFragCoord, gl_SecondaryColor) but NOT on vertex output builtins
  (gl_FrontColor, gl_TexCoord, etc.). Metal requires matching location qualifiers on
  both sides for PSO creation. The fix post-processes the vertex MSL after generation:
  1. Parses fragment's `fragmentMain_in` struct for member name → location mappings
  2. Maps fragment names to vertex equivalents (gl_Color → gl_FrontColor, etc.)
  3. Inserts `[[user(locnN)]]` into vertex's `vertexMain_out` struct members
  Root cause: SPIRV-Cross `member_attribute_qualifier()` for vertex outputs has
  `default: return ""` for unrecognized builtins (compatibility profile vars like
  gl_FrontColor). Fragment inputs go through `get_or_allocate_builtin_input_member_location`
  which allocates locations. This asymmetry is a SPIRV-Cross design limitation.
- Result: PSO failures dropped 2400 → 0. All Lua shader draws now succeed.
  Black screen persists — next priority is framebuffer/viewport investigation.
- Next: Investigate why 21 successful draw calls produce black screen. Check
  BeginDefaultRenderPass, drawable acquisition, viewport/scissor state, present logic.
