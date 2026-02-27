# Metal Debug Iteration Log

## Current Status
- Last iteration: 4
- Commit: 5c07381883
- Shaders: 82/82, Draw calls: 10+11, FPS: 49.4, PSO fails: 0
- Exit: SHUTDOWN_HANG (widget EXIT_SUCCESS, process timeout)
- Visual: **TERRAIN TEXTURED** — correct SPIRV-Cross texture remapping. 2 patches visible (geometrically correct for camera angle).

## Priority Queue
1. SHUTDOWN_HANG — hangs at SpringApp::Kill[3] after widget exit
2. Font path error — doubled path in font loading
3. Texture swizzle warnings — incorrect texture format handling
4. ModernSky disabled — falls back to NullSky
5. Unit model rendering (game needs to advance past gf=0)

## Confirmed Working (Post-Phase 33)
- Viewport: 800x600, drawable matches, contentsScale=1.0
- SPIRV-Cross texture remapping: GL unit → Metal [[texture(N)]] index per shader stage
- Terrain shader: SMFShaderGLSL-Forward-Adv with correct texture bindings
- ROAM patch visibility: 2/24 patches visible at steep camera angle — geometrically correct
- Drawable: 800x600 matches viewport, present works correctly
- Clear(): mid-pass clears properly handled via fullscreen quad
- Screenshots: ReadPixels with Y-flip works correctly
- Loading screen: "Loading..." text renders with fonts

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
- Commit: c5c2610204
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

### Iteration 3 — 2026-02-27 (Clear() mid-pass wipe fix) ★ BREAKTHROUGH
- Run: 20260227_173458
- Commit: 95dbbff551
- Metrics: shaders=82/82 draws=5+17-21 fps=49.8 pso_fails=0 exit=SHUTDOWN_HANG screenshots=7(1 load+6 game)
- Visual: **TERRAIN VISIBLE** — textured terrain with surface detail, sky gradient, horizon line.
  Rendering appears confined to upper-left quadrant of screen.
- Root cause: Metal Clear() implementation restarted the render pass with LoadAction::Clear,
  wiping ALL previously drawn content. In OpenGL, glClear() is cheap and respects the scissor
  rect; the engine calls it ~700+ times per test run (Lua widgets, minimap, etc.). With the old
  Metal impl, each Clear() ended and restarted the render pass, wiping terrain, sky, and all draws.
- Fix: Four related issues fixed in MTLContext.mm:
  1. **Mid-pass color clears**: Draw a fullscreen triangle with clear color instead of restarting
     the render pass. Uses a dedicated clear shader + PSO. Respects scissor rect, preserves all
     previous draw content. (This was the main visual fix.)
  2. **Clear() FBO pointer loss**: EndRenderPass() cleared currentFramebuffer, so Clear() always
     fell through to BeginDefaultRenderPass (screen clear) even when clearing an FBO. Fixed by
     saving currentFramebuffer before EndRenderPass.
  3. **ClearColor/ClearDepth/ClearStencil**: These were incorrectly setting pending*Clear flags.
     They should only store values (like GL's glClearColor), not trigger clears. Actual clear
     triggered by Clear(color=true, ...).
  4. **pendingColorClear not consumed**: In BeginDefaultRenderPass, the Clear loadAction case
     didn't reset pendingColorClear, so every subsequent pass re-cleared.
- Result: Black screen → textured terrain visible! Screenshot sizes: 4KB → 280KB.
  Terrain texture detail, sky gradient, and horizon line all rendering correctly.
  Rendering confined to upper-left quadrant — likely viewport/Y-axis issue to investigate next.
- Next: Investigate viewport/quadrant issue — rendering appears in upper-left ~50% of screen.
  May be Metal Y-axis coordinate system difference or viewport setup.

### Iteration 4 — 2026-02-27 (SPIRV-Cross texture binding remapping)
- Run: 20260227_135740
- Commit: 5c07381883
- Metrics: shaders=82/82 draws=10+11 fps=49.4 pso_fails=0 exit=SHUTDOWN_HANG screenshots=7
- Visual: **TERRAIN TEXTURED** — correct texture binding, detailed terrain surface with diffuse
  textures properly applied. 2 ROAM patches visible out of 24 total.
- Root cause: SPIRV-Cross assigns `[[texture(N)]]` indices that differ from GL texture unit
  numbers. The engine binds textures by GL unit (diffuseTex→0, heightMapTex→1, etc.) but Metal
  shaders expect SPIRV-Cross-assigned indices (e.g., diffuseTex→[[texture(2)]]). Without
  remapping, textures were bound at wrong slots, causing incorrect or missing sampling.
- Fix: Three-part fix in MTLShader/MTLContext:
  1. **MTLShader::Link()**: Extract sampler reflection data to build `samplerInfoMap` mapping
     uniform name → Metal texture/sampler indices per shader stage (VS/FS independently).
  2. **MTLShader::SetUniform1i()**: Intercept sampler uniform calls. When `SetUniform1i("tex", N)`
     is called, record GL unit N → Metal texture index from samplerInfoMap. Don't store in
     uniform buffer (Metal binds textures directly, not via uniforms).
  3. **MTLContext::BindCurrentResources()**: Use shader's remap table. When remapping is active,
     skip unmapped GL units (prevents identity fallback from overwriting correct bindings).
- Also: Set `metalLayer.contentsScale = 1.0` and use `SDL_Metal_GetDrawableSize()` for safety.
- Viewport investigation: 800x600 drawable matches 800x600 viewport exactly. The "upper-left
  quadrant" appearance is NOT a viewport bug — it's the natural frustum coverage at the default
  camera angle (pos=3072,969,2105 dir=0,-0.86,-0.52, ~59° below horizontal). Only 2/24 ROAM
  patches pass frustum culling at this steep angle, which is geometrically correct.
- Next: Shutdown hang, font path error, texture swizzle, model rendering.
