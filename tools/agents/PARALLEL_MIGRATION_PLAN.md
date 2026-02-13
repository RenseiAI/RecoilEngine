# Parallel GL→RHI Migration Plan

Updated 2026-02-13 based on fresh audit (~2,332 GL calls across 129 files).

## How Parallelism Works

Each **work unit** runs as a **separate Claude Code conversation** (separate terminal).
Each conversation:
1. Creates a branch: `migrate/<unit-name>` off `arm64-metal-port`
2. Only modifies its **owned files** (listed below)
3. Commits with descriptive messages
4. User merges back to `arm64-metal-port` when complete

**No two work units share files.** This eliminates merge conflicts entirely.

If a work unit discovers it needs a change in a file it doesn't own, it documents
the needed change in a `CROSS_CUTTING.md` file at the repo root and moves on.

## Prerequisite Infrastructure (Wave 0)

**Must be done first, before spawning parallel agents.**
These touch shared RHI headers that all other work units include.

### Wave 0: RHI Interface Extensions

Do this in the current conversation or a single dedicated conversation.

**Needed additions to `rts/Rendering/RHI/`:**
- `IRHIContext::SetLogicOpEnabled(bool)` + `SetLogicOp(LogicOp)` — for GuiHandler XOR
- `IRHIContext::SetAlphaTestEnabled(bool)` + `SetAlphaFunc(CompareFunc, float)` — FFP compat
  (or just remove and use shader discard — evaluate per call site)
- `RHITypes.h`: Add `LogicOp` enum (Invert, And, Or, Xor, etc.)

**Already exists (no action needed):**
- MatrixStack (`rts/Rendering/RHI/MatrixStack.h`) — ready to use
- Stencil methods (`SetStencilTestEnabled`, `SetStencilFunc`, `SetStencilOp`, `SetStencilMask`)
- Blend equation (through pipeline `BlendState`)
- Clip distances, depth clamp, seamless cubemaps

**Estimated effort:** 1-2 hours

---

## Wave 1: Independent Migration Units (5 parallel conversations)

Launch all 5 after Wave 0 merges. No file conflicts between any pair.

---

### Unit A: `migrate/map-terrain`

**Scope:** Map loading, terrain mesh, terrain textures.

**Owned files:**
```
rts/Map/SMF/SMFReadMap.cpp              (~45 GL calls)
rts/Map/SMF/SMFReadMap.h
rts/Map/SMF/SMFRenderState.cpp          (~36 GL calls)
rts/Map/SMF/SMFRenderState.h
rts/Map/SMF/SMFGroundTextures.cpp       (~27 GL calls)
rts/Map/SMF/SMFGroundTextures.h
rts/Map/SMF/ROAM/Patch.cpp             (~14 GL calls)
rts/Map/SMF/ROAM/Patch.h
rts/Map/SMF/ROAM/RoamMeshDrawer.cpp    (~13 GL calls)
rts/Map/SMF/ROAM/RoamMeshDrawer.h
rts/Map/SMF/Basic/BasicMeshDrawer.cpp   (~2 GL calls)
rts/Map/MapTexture.h                    (GLuint → IRHITexture refactor)
rts/Map/MapTexture.cpp                  (if exists)
```

**Total:** ~137 GL calls across 7 source files

**Key tasks:**
1. Refactor `MapTexture` to hold `IRHITexture*` alongside raw GLuint (backward compat)
2. Migrate texture binding in SMFRenderState
3. Migrate texture upload/creation in SMFReadMap
4. Migrate SMFGroundTextures texture state calls
5. Migrate ROAM mesh drawer GL state

**Blockers:** None (MapTexture refactor is self-contained within this unit)

**Prompt hint for conversation:**
```
You are migrating the Map/Terrain subsystem from direct OpenGL calls to the RHI
abstraction layer. Read CLAUDE.md for build instructions and RHI patterns.
Your owned files are under rts/Map/SMF/ and rts/Map/MapTexture.*.
Create branch migrate/map-terrain. Do NOT modify files outside your ownership.
```

---

### Unit B: `migrate/environment`

**Scope:** Sky, grass, water finish, cubemap debug.

**Owned files:**
```
rts/Rendering/Env/SkyBox.cpp            (~46 GL calls)
rts/Rendering/Env/SkyBox.h
rts/Rendering/Env/GrassDrawer.cpp       (~41 GL calls)
rts/Rendering/Env/GrassDrawer.h
rts/Rendering/Env/BumpWater.cpp         (~11 GL calls, finish migration)
rts/Rendering/Env/BumpWater.h
rts/Rendering/Env/DebugCubeMapTexture.cpp (~22 GL calls)
rts/Rendering/Env/DebugCubeMapTexture.h
rts/Rendering/Env/MapRendering.cpp      (if GL calls)
rts/Rendering/Env/MapRendering.h
```

**Total:** ~120 GL calls across 4-5 source files

**Key tasks:**
1. SkyBox: Replace matrix stack FFP with RHI::MatrixStack, migrate cubemap binding
2. GrassDrawer: Replace display lists with VBO, migrate matrix stack + texture binding
3. BumpWater: Finish remaining ~11 GL calls (mostly GL state setting)
4. DebugCubeMapTexture: Replace GL texture/FBO calls with RHI

**Blockers:** None (MatrixStack already exists)

---

### Unit C: `migrate/particles-decals`

**Scope:** Projectile rendering, ground decals.

**Owned files:**
```
rts/Rendering/Env/Particles/ProjectileDrawer.cpp  (~33 GL calls)
rts/Rendering/Env/Particles/ProjectileDrawer.h
rts/Rendering/Env/Decals/GroundDecalHandler.cpp    (~38 GL calls)
rts/Rendering/Env/Decals/GroundDecalHandler.h
rts/Rendering/Env/Decals/GroundDecal.h
rts/Rendering/GroundFlash.cpp                      (~8 GL calls)
rts/Rendering/GroundFlash.h
```

**Total:** ~79 GL calls across 3 source files

**Key tasks:**
1. ProjectileDrawer: Migrate remaining matrix stack + GL state calls
2. GroundDecalHandler: Migrate texture binding + GL state
3. GroundFlash: Small cleanup

**Blockers:** None

---

### Unit D: `migrate/game-ui`

**Scope:** All Game/UI rendering code.

**Owned files:**
```
rts/Game/UI/GuiHandler.cpp              (~80 GL calls)
rts/Game/UI/GuiHandler.h
rts/Game/UI/MiniMap.cpp                 (~16 GL calls)
rts/Game/UI/MiniMap.h
rts/Game/UI/ProfileDrawer.cpp           (~29 GL calls)
rts/Game/UI/ProfileDrawer.h
rts/Game/UI/EndGameBox.cpp              (~19 GL calls)
rts/Game/UI/EndGameBox.h
rts/Game/UI/MouseCursor.cpp             (~12 GL calls)
rts/Game/UI/MouseCursor.h
rts/Game/UI/CursorIcons.cpp             (~8 GL calls)
rts/Game/UI/CursorIcons.h
rts/Game/UI/ResourceBar.cpp             (~6 GL calls)
rts/Game/UI/ResourceBar.h
rts/Game/UI/CommandColors.cpp           (if GL calls)
rts/Game/UI/KeyBindings.cpp             (if GL calls)
```

**Total:** ~170 GL calls across ~8 source files

**Key tasks:**
1. GuiHandler: Major migration — matrix stack, display lists, glLogicOp(GL_INVERT)
   - For glLogicOp: Use `IRHIContext::SetLogicOp()` if available (Wave 0),
     else document as CROSS_CUTTING.md blocker
2. MiniMap: Matrix stack + clip plane migration
3. ProfileDrawer/EndGameBox: Straightforward state + drawing migration
4. MouseCursor/CursorIcons: Texture binding + small state changes

**Blockers:** LogicOp for GuiHandler selection XOR (Wave 0)

---

### Unit E: `migrate/unit-model`

**Scope:** Unit rendering, model drawing, HUD.

**Owned files:**
```
rts/Rendering/Units/UnitDrawer.cpp          (~73 GL calls)
rts/Rendering/Units/UnitDrawer.h
rts/Rendering/Units/UnitDrawerState.cpp     (if GL calls)
rts/Rendering/Models/3DModelVAO.cpp         (~37 GL calls)
rts/Rendering/Models/3DModelVAO.h
rts/Rendering/Common/ModelDrawerHelpers.cpp (~26 GL calls)
rts/Rendering/Common/ModelDrawerHelpers.h
rts/Rendering/HUDDrawer.cpp                 (~20 GL calls)
rts/Rendering/HUDDrawer.h
rts/Rendering/Features/FeatureDrawer.cpp    (~5 GL calls)
rts/Rendering/Features/FeatureDrawer.h
```

**Total:** ~161 GL calls across 5-6 source files

**Key tasks:**
1. UnitDrawer: Matrix stack migration, texture binding, GL state
2. 3DModelVAO: Replace immediate mode with typed render buffers
3. ModelDrawerHelpers: Texture state + matrix operations
4. HUDDrawer: Matrix stack + simple drawing
5. FeatureDrawer: Minor GL state cleanup

**Blockers:** None

---

## Wave 2: Dependent Migration Units (3 parallel conversations)

Launch after Wave 1 merges. These depend on underlying systems being migrated.

---

### Unit F: `migrate/lua-gl`

**Scope:** All Lua GL scripting bindings.

**Owned files:**
```
rts/Lua/LuaOpenGL.cpp                (~417 GL calls)
rts/Lua/LuaOpenGL.h
rts/Lua/LuaShaders.cpp               (~49 GL calls)
rts/Lua/LuaShaders.h
rts/Lua/LuaFBOs.cpp                  (~48 GL calls)
rts/Lua/LuaFBOs.h
rts/Lua/LuaTextures.cpp              (~44 GL calls)
rts/Lua/LuaTextures.h
rts/Lua/LuaVAOImpl.cpp               (~25 GL calls)
rts/Lua/LuaVAOImpl.h
rts/Lua/LuaOpenGLUtils.cpp           (~12 GL calls)
rts/Lua/LuaOpenGLUtils.h
rts/Lua/LuaMaterial.cpp               (~10 GL calls)
rts/Lua/LuaMaterial.h
rts/Lua/LuaFonts.cpp                  (~5 GL calls)
rts/Lua/LuaConstGL.cpp                (~2 GL calls)
rts/Lua/LuaAtlasTextures.h
```

**Total:** ~612 GL calls across ~10 source files

**Why Wave 2:** Lua wraps engine rendering — migrating it before the underlying
systems risks double-work or inconsistencies.

**Key tasks:**
1. LuaOpenGL: Massive file. Migrate in sections — state, textures, matrix, drawing
2. LuaShaders: Shader program management → RHI shader interface
3. LuaFBOs: FBO management → RHI framebuffer interface
4. LuaTextures: Texture creation/management → IRHITexture
5. LuaVAOImpl: VAO/VBO → RHI buffer interface

**Special concern:** Mod backward compatibility. Lua API must continue to accept
GL enum constants from mods. Translate GL enums to RHI enums at the Lua boundary.

---

### Unit G: `migrate/rmlui`

**Scope:** RmlUi HTML/CSS renderer backend.

**Owned files:**
```
rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.cpp  (~190 GL calls)
rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.h
```

**Total:** ~190 GL calls in 1 source file

**Why Wave 2:** Depends on stencil/blend extensions (Wave 0) and general
RHI maturity for framebuffer management.

**Key tasks:**
1. Replace all GL texture/framebuffer management with RHI
2. Replace stencil state with IRHIContext stencil methods
3. Replace blend state with RHI pipeline state
4. Replace viewport/scissor with IRHIContext methods
5. Replace shader program management with RHI shader interface

**Note:** This is a self-contained rewrite of one file. High effort but no
external dependencies once RHI interfaces are ready.

---

### Unit H: `migrate/gl-utilities-scatter`

**Scope:** GL utility classes + scattered small files.

**Owned files:**
```
rts/Rendering/GL/myGL.cpp               (~59 GL calls — utility functions)
rts/Rendering/GL/myGL.h
rts/Rendering/GL/glExtra.cpp             (~28 GL calls)
rts/Rendering/GL/glExtra.h
rts/Rendering/GL/LightHandler.cpp        (~34 GL calls)
rts/Rendering/GL/LightHandler.h
rts/Rendering/GL/VertexArray.cpp         (~85 GL calls — deprecated)
rts/Rendering/GL/VertexArray.h
rts/Rendering/GlobalRendering.cpp        (~50 GL calls — finish migration)
rts/Rendering/GlobalRendering.h
rts/Rendering/Textures/nv_dds.cpp        (~22 GL calls)
rts/Rendering/Textures/Bitmap.cpp        (~20 GL calls)
rts/Rendering/WorldDrawer.cpp            (~5 GL calls)
rts/Rendering/HAPFSPathDrawer.cpp        (~21 GL calls)
rts/Rendering/CommandDrawer.cpp           (~8 GL calls)
rts/Rendering/DebugDrawerAI.cpp           (~5 GL calls)
rts/Rendering/LineDrawer.cpp              (~5 GL calls)
rts/Rendering/ShadowHandler.cpp           (~10 GL calls)
rts/Rendering/Screenshot.cpp              (if GL calls)
rts/Rendering/AVIGenerator.cpp            (if GL calls)
rts/Rendering/Fonts/glFont.cpp            (~8 GL calls)
rts/Rendering/Textures/ColorMap.cpp       (if GL calls)
rts/Rendering/Textures/S3OTextureHandler.cpp  (if GL calls)
rts/Rendering/Textures/3DOTextureHandler.cpp  (if GL calls)
rts/Rendering/Textures/S3OTextureHandler.cpp  (if GL calls)
rts/Rendering/Textures/TextureRenderAtlas.cpp (~30 GL calls — FBO pipeline, partial)
rts/Rendering/Textures/ColorMap.cpp           (if GL calls)
rts/Rendering/Map/InfoTexture/*.cpp       (scattered calls)
rts/Rendering/SmoothHeightMeshDrawer.cpp  (~5 GL calls)
rts/Game/Game.cpp                         (if GL calls — rendering init)
rts/Game/Camera/OverviewController.cpp    (if GL calls)
rts/aGui/*.cpp                            (scattered GL calls)
```

**Total:** ~350+ GL calls across ~25 source files

**Key tasks:**
1. myGL.cpp: Remove dead ARB functions, clean FFP utilities
2. glExtra.cpp: Rewrite glSurfaceCircle/glBallisticCircle using TypedRenderBuffer
3. VertexArray: Mark callers as using TypedRenderBuffer instead (or deprecate fully)
4. LightHandler: Replace FFP lighting with shader uniforms (or remove if unused)
5. GlobalRendering: Finish remaining SDL-boundary GL calls
6. nv_dds/Bitmap: Migrate texture creation to IRHITexture
7. Scattered rendering files: Small GL state migrations

**Note:** This is the "long tail" unit. Many files, few calls each.

---

## Summary

| Wave | Units | Conversations | GL Calls | Can Start After |
|------|-------|---------------|----------|-----------------|
| 0 | RHI extensions | 1 (current) | 0 (infra) | Now |
| 1 | A,B,C,D,E | 5 parallel | ~667 | Wave 0 merged |
| 2 | F,G,H | 3 parallel | ~1,152 | Wave 1 merged |
| | **Total** | **9** | **~1,819** | |

Remaining ~513 calls are in:
- GL backend files (Shader.cpp, Texture.cpp, GLSLCopyState.cpp) — keep as-is (~186)
- GL wrapper classes (FBO.cpp, VBO.cpp) — keep as-is, RHI equivalents exist (~102)
- SDL/platform boundary (intentional GL, ~30)
- Other scattered (~195 — folded into Unit H)

## Conversation Startup Template

Paste this into each new Claude Code conversation, filling in the unit name and files:

```
I'm working on the RecoilEngine ARM64 Metal port. I need you to migrate GL calls
to the RHI abstraction layer for a specific subsystem.

Branch: Create `migrate/<UNIT-NAME>` off `arm64-metal-port`

Your OWNED files (only modify these):
<LIST OF FILES>

Rules:
- Read CLAUDE.md for build/RHI patterns
- Replace direct gl*() calls with RHI equivalents (IRHIDevice, IRHIContext, IRHITexture)
- Replace GL::SubState with RHI scoped pipeline state
- Replace glPushMatrix/glPopMatrix with RHI::MatrixStack + RHI::ScopedMatrixPush
- Replace GL::TexBind with IRHITexture::Bind()/Unbind()
- Keep #include "Rendering/GL/myGL.h" if GL enums are still needed for backward compat
- Preserve rendering behavior exactly — no visual changes
- One commit per file or logical change
- If you need changes in files you don't own, write them to CROSS_CUTTING.md
- Build check: cmake --build build-arm64/ --target engine-headless -j$(sysctl -n hw.ncpu)
```

## Merge Protocol

After each wave completes:
1. Merge each unit branch one at a time into `arm64-metal-port`
2. Build verify after each merge
3. Resolve any CROSS_CUTTING.md items
4. Run GL audit to confirm call count reduction
5. Start next wave
