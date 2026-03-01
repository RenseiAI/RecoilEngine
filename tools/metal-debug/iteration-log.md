# Metal Debug Iteration Log

## Current Status
- Last iteration: 12
- Commit: 1ad1171218
- Shaders: 87, Draw calls: 19idx+11, FPS: 54.6, PSO fails: 2 (cached)
- Exit: **CLEAN EXIT (code 0)**, Errors: **12**
- Visual: **FULL-SCREEN TERRAIN + UNITS + SKY** — no black rectangle, no blinking
- **Terrain covers entire 800x600** — previously only top-left ~25% showed ground.
  Root cause was Metal FBO Bind() no-op leaking Lua/MiniMap/RmlUi draws to screen.

## Priority Queue
1. Widget off-screen textures — `gl.RenderToTexture()` stale on Metal (scissor-clipped workaround)
2. Shadow pass verification — shadow shader variants compile, need to verify FBO + shadow map pipeline
3. BumpWater — requires `[[clip_distance]]` across all water-affected shaders. Water=0 anyway.
4. Last 2 PSO failures — both cached, non-crashing. Quick investigation.
5. Info textures (Combiner) — requires FFP builtin removal from Lua info shaders.
6. Tier 5: CI pipeline, macOS app bundle, final audit

## Confirmed Working (Post-Iteration 12)
- **Full-screen terrain** — terrain covers entire 800x600, no black rectangle or blinking
- **Units + particle effects** — commanders, infantry, battle smoke, construction sparks
- **Atmospheric sky** — ModernSky gradient rendering (Sky-0/Sky-1 shaders)
- **Grass pipeline** — all infrastructure ready, test map has 0 grass data
- **Clean shutdown** — Metal device destroyed, all Kill[] stages complete, exit code 0
- **Game simulation** — forcestart works, gf=189 in 600 frames, ~54.6 FPS
- **SIGABRT recovery** — Metal abort() during PSO creation caught + cached via sigsetjmp
- **Vertex output padding** — PadMissingVertexOutputs adds zero-initialized dummy outputs
- **PSO failure caching** — both NSError and SIGABRT failures cached, logged once per shader
- Viewport: 800x600, drawable matches, contentsScale=1.0
- SPIRV-Cross texture remapping: GL unit → Metal [[texture(N)]] index per shader stage
- Terrain shader: SMFShaderGLSL-Forward-Adv with correct texture bindings
- Clear(): mid-pass clears properly handled via fullscreen quad
- Screenshots: ReadPixels with Y-flip works correctly
- Loading screen: "Loading..." text renders with fonts
- STREFLOP_NEON with FPCR verification

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

### Iteration 5 — 2026-02-27 (STREFLOP_NEON on Apple ARM64) ★ MULTIPLAYER SYNC
- Run: 20260227_152213
- Commit: f36a7c168c
- Metrics: shaders=82/82 draws=10+11 fps=50.2 pso_fails=0 exit=SHUTDOWN_HANG screenshots=7
- Visual: Unchanged from iteration 4 — terrain textured, 2 ROAM patches visible.
- Milestone: **STREFLOP_NEON enabled on Apple ARM64** — deterministic floating-point for
  multiplayer sync is now active. Previously disabled (`NOT_USING_STREFLOP`), breaking
  cross-platform multiplayer. Verified bit-exact with x86_64 SSE (47,852/47,852 tests).
- Changes:
  1. **CMakeLists.txt**: Apple ARM64 → `STREFLOP_NEON` (was `NOT_USING_STREFLOP`)
  2. **streflop_cond.h**: Removed explicit `math::sqrt(float)` that conflicted with FastMath.h
     when included first (lmathlib.cpp). `using namespace streflop` import provides fallback.
  3. **FastMath.h**: Added `math::sqrt(int)` overload — resolves ambiguity between
     `sqrt(float)` and `sqrt(double)` for integer arguments.
  4. **dbl64_system.cpp** (new): Double-precision bridge delegating `streflop_libm::__ieee754_*`
     to system `<cmath>`. Needed because streflop only bundles flt-32 libm; on Linux these
     resolve from glibc, but macOS libm doesn't export the internal symbols.
  5. **streflop CMakeLists.txt**: ARM64 mode SOFT → NEON (all platforms, not just Apple).
- Confirmed: `Streflop NEON mode, FPUCHECK NOT IMPLEMENTED` in infolog — NEON active.
- Also committed earlier in session: `-ffp-contract=off` fix in streflop libm, GitHub Actions
  workflow for x86_64 reference generation, reference results, upstream handoff doc.
- Next: Shutdown hang, font path error, texture swizzle, model rendering.

### Iteration 6 — 2026-02-27 (Shutdown hang fix) ★ CLEAN EXIT
- Run: 20260227_180235
- Commit: 2d0be0777a
- Metrics: shaders=82/82 draws=10+11 fps=49.2 pso_fails=0 exit=0 (clean) screenshots=7
- Visual: Unchanged — terrain textured, 2 ROAM patches, STREFLOP_NEON active.
- Root cause: `dispatch_semaphore_wait(DISPATCH_TIME_FOREVER)` in `~MTLContext()` blocked
  because the frame loop does `EndFrame→BeginFrame` in SwapBuffers, so at shutdown the last
  `BeginFrame` consumed a semaphore slot with no matching `EndFrame` signal. The destructor
  tried to acquire all 3 slots (MaxFramesInFlight) but only 2 were available → deadlock.
- Fix: Three fixes in MTLContext.mm:
  1. **~MTLContext**: Flush any pending command buffer from the last BeginFrame (commit without
     present, waitUntilCompleted), or signal semaphore if no buffer exists. Then wait with
     2-second timeout (safety net) instead of DISPATCH_TIME_FOREVER.
  2. **EndFrame early-return**: When `commandBuffer` is nil, signal the semaphore before
     returning — prevents permanent semaphore count leak.
  3. **Destructor timeout**: `break` on timeout instead of blocking forever.
- Result: Kill[7] → "Metal device destroyed" → Kill[8] → Kill[9] → exit code 0.
  Previously hung at Kill[7] and never reached Kill[8].
- Next: Font path error, texture swizzle, ModernSky, model rendering.

### Iteration 7 — 2026-02-28 (SIGABRT recovery + game simulation) ★ GAMEPLAY
- Run: 20260228_210817
- Commit: 66145de041
- Metrics: shaders=82/82 draws=11+23+2inst fps=47.4 pso_fails=255(graceful) exit=0 screenshots=7 gf=44
- Visual: Terrain textured, game simulation running. Draw counts increase during gameplay
  (5+29 loading → 11+23+2inst gameplay). No unit models visible yet.
- Root cause 1: **gf=0 (game never started)** — `forcestart` was commented out in the
  auto_test_widget.lua, and `StartPosType=2` required manual position selection in the GUI.
- Fix 1: Uncommented forcestart in auto_test_widget.lua. Changed `StartPosType=0` in
  script.txt with explicit `StartPosX/Z` coordinates for both teams.
- Root cause 2: **SIGABRT crash during PSO creation** — Metal's
  `newRenderPipelineStateWithDescriptor:` calls `abort()` (not NSException) for certain
  vertex descriptor + shader combinations where vertex-to-fragment output interfaces
  mismatch. The specific crash was in `[MTLVertexDescriptorInternal newSerializedDescriptor]`
  for a Lua shader (`aMirrorParams`, vtxLayout=1, stride=16).
- Fix 2: Three-layer defense in MTLPipeline.mm:
  1. **Pre-validation**: Check vertex function's `stageInputAttributes` against configured
     vertex descriptor locations. Skip PSO creation if active inputs aren't satisfied.
  2. **SIGABRT signal handler**: `sigsetjmp`/`siglongjmp` with `sa_handler` for SIGABRT.
     When Metal calls `abort()`, the handler longjmps back to the PSO creation callsite,
     logs the failure, and returns nil instead of terminating.
  3. **Failure caching**: `pipelineCache[key] = nil` in recovery path prevents repeated
     SIGABRT catches for the same shader+layout combination.
- Result: Engine survives Metal abort(), game reaches gf=44. 255 PSO failures gracefully
  handled (vs 4 before crash in previous run). 1 actual SIGABRT recovery (rest cached).
  Draw calls increase from 5+29 (loading) to 11+23+2inst (gameplay) — indexed and instanced
  draws now active, suggesting model/unit rendering pipeline is executing.
- Next: Investigate Lua shader PSO failures (vertex-to-fragment mismatch), unit model
  visibility, ModernSky.

### Iteration 8 — 2026-02-28 (Vertex output padding + PSO caching) ★ ERROR REDUCTION
- Run: 20260228_212558
- Commit: (pending)
- Metrics: shaders=82/82 draws=11+15+2inst fps=47.5 pso_fails=2(cached) pso_ok=27 errors=13 exit=0 gf=43
- Visual: Terrain textured, game simulation running. 27 PSOs created successfully.
- Root cause: Lua shaders had fragment inputs (`[[user(locnN)]]`) that the vertex shader
  didn't output. OpenGL silently defaults unwritten varyings to zero; Metal requires explicit
  matching. Additionally, PSO failures weren't cached (NSError path), causing the same
  failure to retry every frame (~240 times).
- Fix: Three changes across MTLShader.mm, MTLPipeline.mm, MTLContext.mm:
  1. **PadMissingVertexOutputs()** (MTLShader.mm): New post-process step after
     `FixGlPerVertexOutputLocations`. Parses `fragmentMain_in` for all `[[user(locnN)]]`
     entries, checks which locations exist in `vertexMain_out`, and adds zero-initialized
     dummy members for missing locations. Matches OpenGL behavior of defaulting unwritten
     varyings to zero. 3 paddings applied: `gl_TexCoord_0`, `m_12_uv`, `m_12_mirrorParams`.
  2. **PSO failure caching** (MTLPipeline.mm): Added `pipelineCache[key] = nil` to the
     NSError failure path (was already in the SIGABRT path). Prevents retrying the same
     failing shader+layout combination every frame.
  3. **Nil PSO dedup logging** (MTLContext.mm): `GetRenderPipelineState returned nil`
     now logs once per shader pointer instead of every draw call. Reduces error count
     from ~240 repeated messages to 0.
- Result: Errors **1636 → 13** (99.2% reduction). PSO failures **255 → 2** (both cached).
  PSOs created OK: **27** (was ~8). Only non-rendering errors remain (ModernSky fallback,
  missing FeatureDefs, unknown SkirmishAI).
- Next: Unit model visibility, ModernSky, remaining 2 PSO failures.

### Iteration 9 — 2026-02-28 (Unit model rendering) ★ UNITS VISIBLE
- Run: 20260228_223559
- Commit: 03f0f878bd
- Metrics: shaders=82/82 draws=110-130/frame fps=~53 pso_fails=2(cached) exit=0 gf=164+
- Visual: **UNITS VISIBLE** — commanders + infantry textured, lit, with particle effects.
  ModelShaderGLSL-NoShadowStandard PSO created, 4 textures bound (diffuse/shading/specular/reflect).
- Root cause: **Units not spawning** — `MinimalSetup=1` in script.txt prevented commander
  placement, and factions weren't specified so auto-select had nothing to choose.
- Fix: Three changes to test configuration:
  1. **Removed `MinimalSetup=1`** from script.txt — was preventing commander spawn.
  2. **Added `Side=Armada` / `Side=Cortex`** to TEAM0/TEAM1 sections — factions must be
     explicitly specified for auto-select to work without lobby GUI.
  3. **Cheat+give widget commands** — auto_test_widget.lua now executes `cheat` then (after
     12 game-frame delay) `give armcom` and `give 5 armpw` to spawn test units. The delay is
     needed because `cheat` is async and must complete before `give` works.
- Result: Draw calls increased from ~36 (no units) to 110-130/frame (with units). Unit models
  render with full material pipeline: diffuse textures, shading maps, specular highlights,
  reflection cubemap. Particle effects (engine exhaust, construction sparks) also rendering.
- Next: ModernSky, shadow pass verification, grass rendering.

### Iteration 10 — 2026-03-01 (ModernSky atmospheric rendering) ★ SKY VISIBLE
- Run: 20260301_001521
- Commit: 120e9d8fe4
- Metrics: shaders=84(+2 Sky-0/Sky-1) draws=29 fps=52.9 pso_fails=2(cached) exit=0 gf=194 errors=12
- Visual: **Atmospheric sky gradient** replaces solid black background. Sky-0 PSO created OK.
  ModernSky Draw() was already fully RHI-migrated — only the constructor guard blocked it.
- Root cause: **Stale backend guard** in `ModernSky::ModernSky()` constructor:
  `if (RHI::GetDefaultBackend() != RHI::Backend::OpenGL) return;` threw `content_error`
  which fell back to NullSky. The guard was added early in the port when Metal rendering
  infrastructure was incomplete, but by this point all needed RHI methods were implemented.
- Fix: Removed the `RHI::GetDefaultBackend() != RHI::Backend::OpenGL` guard from
  ModernSky constructor. Draw()/DrawSun() already use RHI calls exclusively.
- Resolved error: `[ISky::SetSky] error creating ModernSky (falling back to NullSky)` —
  no longer appears in infolog. Error count 13 → 12.
- Next: Near grass rendering, shadow pass verification.

### Iteration 11 — 2026-03-01 (Grass rendering pipeline on Metal) ★ GRASS READY
- Run: 20260301_* (multiple diagnostic runs)
- Commit: (pending)
- Metrics: shaders=89(+5 grass) draws=19idx+11 fps=54.3 pso_fails=2(cached) exit=0 errors=12
- Visual: No visible grass — **test map has zero grass data** (0/24576 squares). All rendering
  infrastructure is complete and would produce grass on a map with grass content.
- Root causes and fixes (5 blockers resolved):
  1. **GrassDetail=0 override** — BAR mod's `luaintro/springconfig.lua` and
     `gui_options.lua` both call `Spring.SetConfigInt("GrassDetail", 0)`, overriding
     springsettings.cfg. Fix: run-test.sh now patches these Lua files to comment out
     the override, with backup/restore in cleanup.
  2. **GLAD_GL_EXT_framebuffer_blit check** — GrassDrawer constructor disabled grass
     when `GLAD_GL_EXT_framebuffer_blit == 0` (always 0 on Metal since GLAD isn't loaded).
     Fix: Added `!RHI::IsMetalBackend()` bypass.
  3. **GL texture ID check in Draw()** — `readMap->GetGrassShadingTexture()` returns GL
     texture ID (always 0 on Metal). Fix: Skip check on Metal backend.
  4. **Missing RHI texture for grassShadingTex** — `CreateGrassTex()` defaulted to minimap's
     GL texture ID (0 on Metal). `WrapMapTexture()` skips ID=0 textures. Fix: On non-GL
     backends, load minimap bitmap and create RHI texture via `SetMapTexFromBitmap()`.
  5. **GL_QUADS in far billboard path** — `CVertexArray::DrawArrayTN(GL_QUADS)` has no
     Metal equivalent. Fix: Added `DrawQuadsAsTrianglesRHI()` helper that converts quad
     vertex data to triangles via a dynamic VB + static quad→triangle index buffer.
     Uses same VA_TYPE_TN vertex layout as near grass blades.
- Additional changes:
  - Added `data()` accessor to CVertexArray for read-only access to vertex data
  - Added `farBillboardVB`/`farBillboardIB` members to GrassDrawer for RHI far billboard path
  - Added grassMap coverage diagnostic LOG (informational, kept)
  - run-test.sh: `GrassDetail=7`, BAR Lua grass override patching with cleanup
- Files modified: GrassDrawer.cpp, GrassDrawer.h, SMFReadMap.cpp, VertexArray.h, run-test.sh
- Verification: 0/24576 grass squares have data on test map. All 3 grass shaders compile
  to MSL (grassNearAdvShaderGLSL, grassDistAdvShaderGLSL, grassShadGenShaderGLSL).
  Far billboard path enters Draw() with 59 blocks but all have empty grassMap data.
  Near grass blade VBO created, DrawIndexed(Triangles) path ready.
- Next: Terrain rendering diagnostic (top-left quadrant issue), shadow pass verification.

### Iteration 12 — 2026-03-01 (Black rectangle fix + full-screen terrain) ★ FULL SCREEN
- Run: 20260301_181211
- Commit: 1ad1171218
- Metrics: shaders=87 draws=19idx+11 fps=54.6 pso_fails=2(cached) exit=0 gf=189 errors=12
- Visual: **TERRAIN FILLS ENTIRE 800x600 SCREEN** — no black rectangle, no alternating black
  frames. Units with battle smoke, particle effects, atmospheric sky all visible.
- Root cause: **Metal FBO Bind() is a no-op** — three subsystems called FBO::Bind() expecting
  rendering to redirect to off-screen textures, but on Metal all draws went to the screen:
  1. **LuaOpenGL::RenderToTexture** — Lua widgets called `gl.RenderToTexture()` which set viewport
     to texture size (400x300 = half screen) and drew to screen, producing the black rectangle.
  2. **MiniMap::UpdateTextureCache** — `FBO::IsSupported()` returns true on Metal (line 36-37 of
     FBO.cpp), so `renderToTexture=true`, causing minimap to render at reduced viewport to screen.
  3. **RmlUi layer compositing** — `BeginFrame`/`PushLayer`/`CompositeLayers` used FBO Bind()+Clear()
     which cleared the screen each frame, causing alternating black frames ("blinking").
- Failed approaches:
  - **Render pass break** (EndRenderPass→BeginRenderPass(FBO)→EndRenderPass→BeginDefaultRenderPass):
    Terrain became invisible even with LoadAction::Load for both color and depth. Draw counts normal
    (128+) but fragments didn't render. Root cause unclear — possibly Metal Store→Load within same
    command buffer doesn't preserve correctly.
  - **Clear FBO to transparent** (LoadAction::Clear with 0,0,0,0): Same terrain disappearance.
- Fix: Three targeted workarounds (11 files, 127 insertions, 42 deletions):
  1. **LuaOpenGL.cpp**: Scissor-clip RenderToTexture on Metal — set 1x1 scissor rect before Lua
     function execution, restore after. Lua code runs (no crash) but draws are invisible.
  2. **MiniMap.cpp**: Force `renderToTexture=false` on Metal — uses direct rendering path instead
     of broken FBO texture cache.
  3. **RmlUi_Renderer_GL3_Recoil.cpp**: Guard all FBO operations with `IsMetalBackend()` checks —
     BeginFrame, EndFrame, PushLayer, CompositeLayers, PopLayer all skip FBO paths on Metal.
  4. **MTLContext.mm**: Support explicit depth LoadAction in BeginDefaultRenderPass (for future
     render pass resume support).
  5. **WorldDrawer.cpp**: Viewport restore at start of Draw(), ClearColor alpha 1.0 (was 0.0).
  6. **Supporting**: ModernSky CullMode::None, DepthBufferCopy Metal skip, MTLDevice opaque=YES,
     SMFRenderState clipPlane2 default, NetCommands sync logging.
- Known limitation: Widget off-screen textures not updated on Metal (stale via scissor workaround).
  Widgets that depend on RenderToTexture will have empty/stale textures. Proper fix requires
  solving the render pass break terrain issue.
- Next: Widget texture rendering, shadow pass verification.
