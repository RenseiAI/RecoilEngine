# Handoff Prompt: RecoilEngine ARM64 Metal Port — GL Migration Batch 10

## What was just completed

Sixteen commits on `arm64-metal-port` (Batches 3–9):

| Commit | Description | GL calls removed |
|--------|-------------|-----------------|
| `7f41d7a6d2` | glRectf → TypedRenderBuffer in 4 files | ~12 |
| `0c0b9dcd0d` | WorldDrawer DrawBelowWaterOverlay → TypedRenderBuffer | ~13 |
| `52e62b17b4` | CommandDrawer DrawQuedBuildingSquares → TypedRenderBuffer | ~10 |
| `73dd5ceb7f` | Remove FFP glDisable/glEnable(GL_TEXTURE_2D) and GL_LIGHTING | ~17 |
| `f0aea87baa` | LineDrawer DrawAll → TypedRenderBuffer, remove line stipple | ~16 |
| `30fb26d0dc` | ModelDrawerHelpers remove GL_TEXTURE_2D/GL_TEXTURE_CUBE_MAP enables | ~14 |
| `a8f5b832cb` | Remove FFP GL_ALPHA_TEST, GL_CURRENT_COLOR, GL_TEXTURE_2D from 4 files | ~9 |
| `c5e605a902` | DebugDrawerAI Texture → IRHITexture (R32F + swizzle) | ~14 |
| `8011e6101c` | MiniMap buttonsTextureID + minimapTex → IRHITexture | ~23 |
| `ec29a03d81` | InMapDrawView procedural texture → IRHITexture | ~10 |
| `251c639ebc` | Remove FFP GL_TEXTURE_CUBE_MAP enables from DebugCubeMapTexture + SkyBox | ~12 |
| `fd842181ac` | Remove FFP GL_TEXTURE_2D, GL_FOG, GL_LIGHTING from 4 Game/UI files | ~26 |
| `a257c634fd` | Remove FFP GL_TEXTURE_2D/GL_TEXTURE_1D from 9 Rendering files | ~16 |
| `c740db8f8b` | Remove GL_ALPHA_TEST and GL_TEXTURE_2D from 4 files | ~14 |
| `02e31a2e37` | Migrate glRectf cursor drawing to TypedRenderBuffer | ~4 |
| `b64409f402` | Remove FFP glTexEnvi, GL_DEPTH_TEXTURE_MODE, GL_ALPHA_TEST, GL_TEXTURE_* enables | ~9 |

Total: ~219 GL call sites removed in Batches 3–9.

## Current state (audited 2026-02-10, post Batch 9)

### What Batch 10 does: add shader-based `discard` to legacy model shader

Previous batches removed "safe no-ops" — calls that did nothing in shader rendering.
Batch 10 is the first *functional migration*: it replaces the fixed-function `GL_ALPHA_TEST`
pipeline stage with an equivalent `discard` statement in the legacy model fragment shader.

The GL4 model shader (`ModelFragProgGL4.glsl`) already has this pattern — it uses an
`alphaCtrl` vec4 uniform and `AlphaDiscard()` function. Batch 10 replicates this exact
pattern in the legacy GLSL shader (`ModelFragProg.glsl`), then replaces the FFP
`glAlphaFunc`/`glEnable(GL_ALPHA_TEST)` C++ calls with `modelShader->SetUniform("alphaCtrl", ...)`
— identical to how the GL4 C++ path already works.

### Alpha test threshold mapping

| Draw pass | FFP call | Uniform equivalent |
|-----------|----------|--------------------|
| Opaque | `glAlphaFunc(GL_GREATER, 0.5f)` | `alphaCtrl = (0.5, 1.0, 0.0, 0.0)` |
| Alpha | `glAlphaFunc(GL_GREATER, 0.1f)` | `alphaCtrl = (0.1, 1.0, 0.0, 0.0)` |
| Shadow | `glAlphaFunc(GL_GREATER, 0.5f)` | **KEPT as FFP** — shadow shader needs separate work |

### Shader discard reference (from GL4 path)

```glsl
// In ModelFragProgGL4.glsl (already working):
uniform vec4 alphaCtrl = vec4(0.0, 0.0, 0.0, 1.0); //always pass

bool AlphaDiscard(float a) {
    float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
    float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;
    return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
}

// In main():
float alpha = teamCol.a * float(texColor2.a >= 0.5);
if (AlphaDiscard(alpha))
    discard;
```

### What NOT to touch

- **Shadow pass GL_ALPHA_TEST** (ModelDrawer.h:440-441, 474) — ShadowGenFragProg.glsl
  has `#if 0` around its discard. The shadow shader doesn't sample the diffuse texture
  for alpha, so enabling discard requires wiring up texture bindings. Separate task.
- **LuaObjectDrawer GL_ALPHA_TEST** — Lua materials may use custom shaders that lack discard.
  Requires per-material audit before removal.
- **SMFGroundDrawer GL_ALPHA_TEST** — terrain void alpha uses `mapInfo->map.voidAlphaMin`
  which is map-specific. SMFFragProg.glsl needs its own discard added first.
- **GrassDrawer GL_ALPHA_TEST** — inside `/* */` comment block (dead code). Don't touch.
- **glFontRenderer GL_ALPHA_TEST** — legacy FFP font renderer uses PushGLState pattern.
- **ISky::SetupFog()** — KEEP. The `glFog*` calls set `gl_Fog` built-in uniforms that
  shaders read directly. The enable/disable controls FFP fog blending (irrelevant for
  shaders), but the parameter setting is essential.
- **Anything in GL/, RHI/, Lua/, lib/ directories**

## Agent task assignments

Launch 2 agents using `rhi-migrator` subagent type. Agent 1 modifies shaders, Agent 2
modifies C++ code. They can run in parallel since Agent 2's C++ changes only depend on
the shader having the `alphaCtrl` uniform (which it adds as a default-valued uniform,
so the C++ code compiles and links regardless).

After both agents complete, verify with `git diff --stat` and rebuild.

---

### Agent 1: Add `alphaCtrl` + `discard` to legacy model shader — GLSL + Metal

**Prompt:**
```
Add shader-based alpha discard to the legacy model fragment shader. This replicates the
pattern already used by the GL4 model shader (ModelFragProgGL4.glsl).

Read ALL target files fully before making any edits.

FILES TO MODIFY:

1. cont/base/springcontent/shaders/GLSL/ModelFragProg.glsl

   Add the `alphaCtrl` uniform and `AlphaDiscard` function, then add a discard call.
   The exact changes:

   a) After the existing uniform declarations (after line 24 `uniform vec4 nanoColor;`),
      add:
      ```
      uniform vec4 alphaCtrl = vec4(0.0, 0.0, 0.0, 1.0); //always pass

      bool AlphaDiscard(float a) {
          float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
          float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;
          return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
      }
      ```
      This is copied exactly from ModelFragProgGL4.glsl lines 62-71.

   b) In main(), after `float alpha = teamColor.a * extraColor.a;` (line 95), add:
      ```
      if (AlphaDiscard(alpha))
          discard;
      ```
      This goes BEFORE the DEFERRED_MODE and forward rendering branches, so it applies
      to both paths (same as GL4 shader).

   Do NOT modify any other part of the shader. Keep all existing uniforms, varyings,
   lighting, fog, and deferred mode code exactly as-is.

2. cont/base/springcontent/shaders/Metal/ModelFragProg.metal

   Add the equivalent Metal changes:

   a) Add `float4 alphaCtrl;` to the `ModelUniforms` struct (after `nanoColor`).
      Use a comment: `// default: (0.0, 0.0, 0.0, 1.0) - always pass`

   b) Add the AlphaDiscard helper function before the fragment function:
      ```
      bool AlphaDiscard(float a, float4 alphaCtrl) {
          float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
          float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;
          return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
      }
      ```
      This matches the pattern in ModelFragProgGL4.metal.

   c) In the fragment function, after `float alpha = uniforms.teamColor.a * extraColor.a;`
      (line 125), add:
      ```
      if (AlphaDiscard(alpha, uniforms.alphaCtrl)) {
          discard_fragment();
      }
      ```
      This goes BEFORE the DEFERRED_MODE branch, same as the GLSL version.

IMPORTANT RULES:
- ONLY modify these 2 shader files
- Do NOT modify any C++ files
- Do NOT commit the changes
- Match the AlphaDiscard function EXACTLY as shown (copied from GL4 equivalents)

After editing, build to verify shaders don't cause compile errors:
  cmake --build /Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/build-arm64/ --target engine-headless -j$(sysctl -n hw.ncpu)
Fix any errors.
```

---

### Agent 2: Replace GL_ALPHA_TEST with `alphaCtrl` uniform in C++ model code — ~10 GL calls

**Prompt:**
```
Replace fixed-function GL_ALPHA_TEST calls with shader-based `alphaCtrl` uniform in the
model drawing system. The legacy GLSL shader now has an `alphaCtrl` uniform (added by
Agent 1) that works identically to the GL4 path.

Read ALL target files fully before making any edits.

FILES AND SPECIFIC CHANGES:

1. rts/Rendering/Common/ModelDrawerState.cpp

   a) In IModelDrawerState::SetupOpaqueDrawing() (~line 79-85):
      REMOVE these 2 lines inside `if (IsLegacy())`:
        glAlphaFunc(GL_GREATER, 0.5f);
        glEnable(GL_ALPHA_TEST);

      Also remove the RHI_TODO comment above them (lines 80-82).

      After removing, the `if (IsLegacy())` block will be empty. Remove the entire
      if-block (the `if (IsLegacy()) { }` wrapper).

   b) In IModelDrawerState::ResetOpaqueDrawing() (~line 95-96):
      REMOVE these 2 lines:
        if (IsLegacy())
            glDisable(GL_ALPHA_TEST);

   c) In IModelDrawerState::SetupAlphaDrawing() (~line 127-131):
      REMOVE these 3 lines inside `if (IsLegacy())`:
        // RHI_TODO: alpha test is legacy FFP state.
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.1f);

      Remove the entire if-block wrapper.

   d) In CModelDrawerStateGLSL::Enable() (~line 240-260):
      After the existing uniform sets (after the SetUniformMatrix4x4 "shadowMatrix" line,
      around line 257), add:
        // Alpha control — replaces legacy FFP glAlphaFunc/GL_ALPHA_TEST
        float gtThreshold = mix(0.5f, 0.1f, static_cast<float>(alphaPass));
        modelShader->SetUniform("alphaCtrl", gtThreshold, 1.0f, 0.0f, 0.0f);

      NOTE: `alphaPass` is already a parameter of this function. The `mix()` function
      is available from SpringMath.h (already included). This is the same pattern as
      CModelDrawerStateGL4::Enable() line 378-379.

   e) Update the file's migration status comment at the top (lines 1-20):
      - Change "PARTIAL" to "PARTIAL (alpha test migrated)"
      - In "Remaining GL calls": remove the GL_ALPHA_TEST line
      - In "Dependencies blocking full migration": update to note alpha test is done
      - Add a new migrated pattern: "Alpha test (legacy GLSL) -> shader discard via alphaCtrl uniform"

2. rts/Rendering/Common/ModelDrawer.h

   a) In DrawImpl<legacy>() (~line 342-346):
      REMOVE this line:
        glEnable(GL_ALPHA_TEST);

      KEEP the ISky::GetSky()->SetupFog() call — it sets gl_Fog uniforms that shaders read.

      After removal, the `if constexpr (legacy)` block should contain only SetupFog():
        if constexpr (legacy) {
            // RHI_TODO: legacy FFP state (fog). Only used in GLSL path.
            ISky::GetSky()->SetupFog();
        }

      Update the RHI_TODO comment to remove the alpha test reference.

   b) In DrawImpl<legacy>() cleanup (~line 359-363):
      REMOVE these 2 lines:
        glDisable(GL_FOG);
        glDisable(GL_TEXTURE_2D);

      Why safe: glDisable(GL_FOG) is FFP fog state cleanup. All rendering uses shaders
      that read gl_Fog uniforms directly — the FFP enable/disable doesn't affect shader
      fog. WorldDrawer::Draw() has its own glDisable(GL_FOG) at the end of the frame.
      glDisable(GL_TEXTURE_2D) is a no-op in shader rendering (removed from 15+ files
      in earlier batches).

      After removal, the `if constexpr (legacy)` cleanup block is empty. Remove it entirely.

   c) Update the file's migration status comment at the top (lines 1-22):
      - Remove GL_ALPHA_TEST from "Remaining GL calls"
      - Remove GL_FOG and GL_TEXTURE_2D from "Remaining GL calls"
      - Add to "Migrated patterns": alpha test → shader discard via alphaCtrl
      - Add note that shadow pass GL_ALPHA_TEST is intentionally kept

IMPORTANT RULES:
- ONLY modify these 2 C++ files (ModelDrawerState.cpp, ModelDrawer.h)
- Do NOT touch the shadow pass in ModelDrawer.h (DrawShadowPassImpl) — keep GL_ALPHA_TEST there
- Do NOT touch LuaObjectDrawer.cpp, SMFGroundDrawer.cpp, or any other files
- Do NOT modify shader files
- Do NOT commit the changes

After editing, build:
  cmake --build /Volumes/Supaku/personal/supaku-org/lib/RecoilEngine/build-arm64/ --target engine-headless -j$(sysctl -n hw.ncpu)
Fix any errors.
```

---

## Build command

```bash
cmake --build build-arm64/ --target engine-headless -j$(sysctl -n hw.ncpu)
```

## Post-agent verification checklist

After both agents complete:

1. **Verify changes persisted**: `git diff --stat` — should show 4 files modified:
   - `cont/base/springcontent/shaders/GLSL/ModelFragProg.glsl`
   - `cont/base/springcontent/shaders/Metal/ModelFragProg.metal`
   - `rts/Rendering/Common/ModelDrawerState.cpp`
   - `rts/Rendering/Common/ModelDrawer.h`
2. **Build**: `cmake --build build-arm64/ --target engine-headless -j$(sysctl -n hw.ncpu)`
3. **If agent reported success but no changes**: re-run the agent or do manually
4. **Review shader changes**: `git diff cont/` — verify AlphaDiscard matches GL4 pattern exactly
5. **Review C++ changes**: `git diff rts/` — verify only listed GL calls were removed,
   shadow pass is untouched, and alphaCtrl uniform is set in GLSL Enable()
6. **Commit** with message below

## Commit style

```
Add shader discard to legacy model shader, remove GL_ALPHA_TEST from model draw system

Add alphaCtrl uniform and AlphaDiscard() to ModelFragProg.glsl and ModelFragProg.metal,
replicating the pattern already used by ModelFragProgGL4.glsl. This replaces the
fixed-function GL_ALPHA_TEST pipeline stage with an equivalent in-shader discard.

ModelDrawerState: set alphaCtrl uniform in GLSL Enable() (same as GL4 path), remove
glAlphaFunc/GL_ALPHA_TEST from SetupOpaqueDrawing/ResetOpaqueDrawing/SetupAlphaDrawing.
ModelDrawer.h: remove GL_ALPHA_TEST, GL_FOG, GL_TEXTURE_2D from legacy DrawImpl path.
Shadow pass GL_ALPHA_TEST intentionally kept (needs ShadowGen shader work).

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
```

## Estimated impact

| Agent | Files | Est. GL calls removed | Complexity |
|-------|-------|----------------------|------------|
| 1: Shader alpha discard | 2 (.glsl, .metal) | 0 (shader-only) | Medium (functional change) |
| 2: C++ alpha test removal | 2 (.cpp, .h) | ~10 | Medium (uniform wiring) |
| **Total** | **4 files** | **~10** | |

## Full Project Audit (2026-02-12)

### Executive Summary

| Metric | Value |
|--------|-------|
| **Effective GL calls remaining** | **~2,857** (in compiled files) |
| **Raw GL calls in rts/ (excl lib/RHI backends)** | 3,379 across 117 files |
| **Excluded from build (deprecated water)** | 522 (DynWater/AdvWater/RefractWater) |
| **GL backend calls (keep as-is)** | 186 (Shader.cpp, GLSLCopyState.cpp, Texture.cpp) |
| **Net calls needing migration** | **~2,671** |

### Post-Batch 9 Commits

| Commit | Description |
|--------|-------------|
| `ee97f76` | Remove redundant glActiveTexture resets and migrate shadow compare mode to RHI |
| `cf4661f` | Remove redundant glBindTexture(0) unbinds and fix GrassDrawer RHI device creation bug |
| `c96e3ae` | Remove no-op FFP calls, stale glGetError, and commented-out GL code from 8 files |
| `79b1949` | Remove glPushAttrib/glPopAttrib, FFP texture enables, and dead DrawShadow code |
| `a590a55` | Remove no-op glColor calls from HUDDrawer/LuaObjectDrawer/GrassDrawer |

### Assessment: What remains after Batch 10

After 10 batches, ~229 safe GL calls will have been removed. This batch is significant
not for the call count but for establishing the pattern: shaders own alpha testing via
`alphaCtrl`, not FFP. This unblocks future batches that replicate the pattern in other
shader/subsystem pairs.

### Directly unblocked by Batch 10's pattern

| Target | Est. calls | What to do |
|--------|-----------|------------|
| LuaObjectDrawer GL_ALPHA_TEST | 5 | Verify Lua materials use shaders with alphaCtrl, then remove |
| Shadow pass GL_ALPHA_TEST | 3 | Enable discard in ShadowGenFragProg.glsl, wire up texture binding |
| SMFGroundDrawer GL_ALPHA_TEST | 6 | Add alphaCtrl to SMFFragProg.glsl, set voidAlphaMin uniform |
| ProjectileDrawer GL_ALPHA_TEST | 0 | Already commented out (dead code) |

### Still blocked (architectural)

| Pattern | Est. calls | Files | Required work |
|---------|-----------|-------|---------------|
| Matrix stack (glPush/PopMatrix etc.) | ~174 | 7+ files | CPU-side MatrixStack utility (prerequisite P1) |
| Display lists (glGenLists etc.) | ~27 | 6 files | VBO conversion per use case |
| Texture system GLuint -> IRHITexture | ~200+ | Many files | TextureAtlas + MapTexture refactor |
| glClipPlane -> gl_ClipDistance | ~24 | 4 files | Shader changes + uniform plane equations |
| CVertexArray -> TypedRenderBuffer | ~85 | 3 files | Mechanical, VertexArray.cpp can be removed |
| Lua GL API | ~643 | 5 files | Cannot change without breaking mods |
| RmlUi GL3 renderer | ~194 | 1 file | Needs RHI stencil/blend extensions |

### Recommended Execution Order (from audit)

| Step | Task | Calls Removed | Dependency |
|------|------|--------------|------------|
| 1 | Create MatrixStack utility (P1) | 0 (enabler) | None |
| 2 | Migrate glExtra.cpp (P3) | ~10 | None |
| 3 | Migrate VertexArray callers (A2) | ~85 | None |
| 4 | myGL.cpp ARB removal + ClearScreen (A3) | ~40 | None |
| 5 | GlobalRendering finish (C1) | ~10 | None |
| 6 | TextureAtlas -> IRHITexture (B1) | ~15 | None |
| 7 | MapTexture refactor (B2) | 0 (enabler) | None |
| 8 | SMFReadMap textures (B3) | ~51 | B2 |
| 9 | SMFRenderState binding (B4) | ~35 | B3 |
| 10 | Bitmap callers -> CreateTextureRHI (B5) | ~25 | None |
| 11 | HUDDrawer matrix stack (D1) | ~45 | P1 |
| 12 | UnitDrawer phased (C2) | ~60 | P1, B1 |
| 13 | GrassDrawer phased (C3) | ~60 | P1, B1, B2 |
| 14 | SkyBox + ProjectileDrawer + Decals (C4) | ~100 | B1, B2, P1 |
| 15 | MiniMap matrix + clip (D2) | ~60 | P1 |
| 16 | GuiHandler (D3) | ~100 | P1, P3 |
| 17 | FBO.cpp parallel wrapper (A4) | ~84 | None (background) |
| 18 | LuaOpenGL render state (E1) | ~30 | None |
| 19 | LuaOpenGL FFP + imm mode (E2-E4) | ~65 | P1 |
| 20 | LuaOpenGL textures + support (E5) | ~179 | B1 |
| 21 | RmlUi Renderer (D4) | ~194 | P4 |
| | **TOTAL** | ~2,248 | |

Remaining ~423 calls would be in ~70 smaller files (1-15 calls each) plus GL-backend code.

See `TIER_4_1_REMAINING_MIGRATION.md` for full roadmap details.
