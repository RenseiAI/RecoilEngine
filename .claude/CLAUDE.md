# RecoilEngine - ARM64 Metal Port

## Project Overview

RecoilEngine is a fork of the Recoil/Spring RTS engine being ported to macOS ARM64 with a Metal rendering backend. The primary branch is `arm64-metal-port`.

## Build

```bash
# Configure (first time)
cmake -B build-arm64 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES=arm64

# Build headless (no display required)
cmake --build build-arm64/ --target engine-headless -j$(sysctl -n hw.ncpu)

# Build full engine (requires display, links Metal)
cmake --build build-arm64/ --target engine-legacy -j$(sysctl -n hw.ncpu)
```

**Build targets:**
- `engine-headless` — No rendering, uses GLAD stubs. Primary CI/verification target.
- `engine-legacy` — Full OpenGL + Metal rendering. Requires display context.

## Architecture

### RHI (Render Hardware Interface)

The engine uses an abstraction layer at `rts/Rendering/RHI/` to support both OpenGL and Metal backends:

```
rts/Rendering/RHI/
├── RHITypes.h          # Enums, structs (BlendState, DepthStencilState, PipelineDesc, Viewport)
├── RHIDevice.h         # IRHIDevice interface (CreateTexture, CreateFramebuffer, CreatePipeline)
├── RHIContext.h        # IRHIContext interface (SetViewport, Clear, Draw, BlitFramebuffer)
├── RHIPipeline.h       # IRHIPipeline interface
├── RHIScopedState.h    # RAII wrappers (ScopedPipeline)
├── RHIFactory.h/cpp    # Backend selection, GetDefaultBackend(), CreateDevice()
├── OpenGL/             # OpenGL backend (GLDevice, GLContext, GLPipeline, etc.)
└── Metal/              # Metal backend (.mm files, gated by RHI_HAS_METAL)
```

**Key patterns:**
- Get the device: `auto* device = GetRHIDevice();`
- Get the context: `auto* ctx = device->GetContext();`
- Pipeline state: `RHI::PipelineDesc` + `RHI::ScopedPipeline` (RAII)
- Textures: `std::unique_ptr<RHI::IRHITexture>` via `device->CreateTexture()`
- Framebuffers: `std::unique_ptr<RHI::IRHIFramebuffer>` via `device->CreateFramebuffer()`

### Metal vs Headless separation

- Metal `.mm` sources are exported via `RHI_METAL_SOURCES` (separate from `RHI_SOURCES`)
- Headless build includes `engineSources` but NOT `engineMetalSources`
- `RHI_HAS_METAL` compile definition controls Metal code paths in `RHIFactory.cpp`
- `engine-legacy` sets `target_compile_definitions(engine-legacy PRIVATE RHI_HAS_METAL)`

### Headless stubs

When the GL RHI backend introduces new GLAD symbols, you must also add them to `rts/lib/headlessStubs/gladstub.cpp`:
- Add the `nullptr` declaration: `decltype(glad_glFoo) glad_glFoo = nullptr;`
- Add the init call: `glad_glFoo = MakeStubImpl(glad_glFoo);`

## Key Gotchas

### macOS platform differences
- `pthread_t` is a pointer type (not castable to `uint32_t`)
- No `prctl` — use `pthread_setname_np(name)` (single arg on macOS)
- No `cpu_set_t` or `sched_getaffinity` — `ThreadAffinityGuard` is no-op
- `sol::nil` unavailable — use `sol::lua_nil`
- System headers define macros `Always`, `None`, `Bool` — must `#undef` before RHI enums
- `-framework Cocoa` treated as file path by Unix Makefiles — use INTERFACE IMPORTED library

### CMake processing order
- `rts/Rendering/` subdirs process BEFORE `rts/builds/` subdirs
- `OPENGL_FOUND` is NOT set when RHI CMakeLists runs — don't gate OpenGL backend on it

### C++ name hiding in shader interface
- `GLSLProgramObject` private `SetUniform(UniformState*,...)` hides base class public templates
- Fix: Cast to `Shader::IProgramObject*` before calling template `SetUniform(const char*,...)`

### STREFLOP
- Disabled on Apple due to fenv.h issues
- Manual wrappers needed: `roundf`, `floorf`, `ceilf`, `cbrtf`, `using std::cbrt`

### SDL2/DevIL include paths
- SDL2 config sets include to `/include/SDL2` but code uses `#include <SDL2/...>`
- DevIL `IL_INCLUDE_DIR` points to `/include/IL` but code uses `#include <IL/il.h>`
- Fix: Add parent directories to include paths

## Migration Status (Audited 2026-02-12)

### Completed (Tiers 1-3 + Infrastructure)
- ARM64 headless build works
- RHI interface layer: 8 headers, 169 virtual methods (verified)
- OpenGL backend: 14 files, 160 methods — 100% complete (verified)
- Metal backend: 15 files, 138 methods — 100% complete (verified)
- Shader pipeline: GLSL -> SPIR-V -> MSL via glslang + SPIRV-Cross (verified)
- Shader translations: 41/41 GLSL shaders have MSL equivalents (verified)
- RHI Factory: backend selection working (verified)
- Phase 1 gap-filling COMPLETE: fence sync, buffer mapping, debug output, clip distances
- Legacy water DEPRECATED: DynWater/AdvWater/RefractWater removed from build (~522 GL calls excluded)
- Batches 3-9: ~219 GL call sites removed (FFP no-ops, immediate mode, texture migrations)
- BumpWater: ~85-90% migrated to RHI (~11 GL calls remain at external boundaries)

### In Progress (Tier 4.1 - Actual Code Migration)
- **~2,857 effective GL calls remain** across 117 compiled files (3,379 raw - 522 deprecated water)
- ~2,671 calls need migration (excluding GL backend files that stay as-is)
- Top files: LuaOpenGL (464), RmlUi (194), GuiHandler (130), Shader.cpp (107, GL backend), myGL.cpp (92), UnitDrawer (86), VertexArray.cpp (85), FBO.cpp (84), GrassDrawer (80), MiniMap (80)
- GlobalRendering: 50 GL calls remain (mostly SDL boundary + fallback paths)
- See `tools/agents/TIER_4_1_REMAINING_MIGRATION.md` for detailed breakdown

### GL Backend Files (Do NOT Migrate)
- `Shader.cpp` (107 calls) — IS the GL shader backend
- `GLSLCopyState.cpp` (42 calls) — GL-specific shader introspection
- `Texture.cpp` (37 calls) — IS the GL texture backend

### Not Started (Tier 5)
- CI pipeline (GitHub Actions)
- macOS app bundle packaging
- Final audit pass

## Agent System

Agent prompts for parallelized migration work live in `tools/agents/`. See:
- `tools/agents/ORCHESTRATOR.md` — Tier dependency graph, file ownership, merge protocol
- `tools/agents/RUNBOOK.md` — Step-by-step invocation instructions
- `tools/agents/prompts/` — Individual agent prompts (24 agents across 5 tiers)

## Code Style

- Follow existing codebase conventions (tabs for indentation in C++)
- Commit messages: imperative mood, explain "why" not "what"
- One logical change per commit
- Do not modify files outside the scope of the current task
- Preserve rendering behavior exactly during migration — no visual or performance changes
