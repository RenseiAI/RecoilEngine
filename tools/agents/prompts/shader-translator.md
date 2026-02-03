# Agent: shader-translator

## Purpose
Translate all 41 GLSL shader files to Metal Shading Language using the ShaderCompiler
infrastructure, and set up the pre-compiled metallib build step.

## Owned Files
- `cont/base/springcontent/shaders/Metal/` (NEW directory - translated MSL files)
- CMake shader compilation rules (document needed changes)

## Prerequisites
- ShaderCompiler exists (`shader-pipeline` agent completed)
- glslang and SPIRV-Cross are integrated

## Context

The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`

### Shader Files (41 total in cont/base/springcontent/shaders/GLSL/)

**Core rendering (highest priority):**
- ModelVertProg.glsl / ModelFragProg.glsl - unit/feature rendering
- ModelVertProgGL4.glsl / ModelFragProgGL4.glsl - GL4 model path
- SMFVertProg.glsl / SMFFragProg.glsl - map terrain
- ShadowGenVertProg.glsl / ShadowGenFragProg.glsl - shadow maps
- ShadowGenVertProgGL4.glsl / ShadowGenFragProgGL4.glsl - GL4 shadow path
- ShadowGenVertMapProg.glsl - map shadow gen

**Environment:**
- BumpWaterVS.glsl / BumpWaterFS.glsl
- BumpWaterCoastBlurVS.glsl / BumpWaterCoastBlurFS.glsl
- GrassVertProg.glsl / GrassFragProg.glsl
- ModernSkyVS.glsl / ModernSkyFS.glsl
- GroundDecalsVertProg.glsl / GroundDecalsFragProg.glsl
- CubeMapVS.glsl / CubeMapFS.glsl
- EquiRectConverterFS.glsl

**Effects:**
- ProjFXVertProg.glsl / ProjFXFragProg.glsl
- ProjFXVertShadowProg.glsl / ProjFXFragShadowProg.glsl

**UI:**
- Icons2DVS.glsl / Icons3DVS.glsl / IconsFS.glsl
- MiniMapVertProg.glsl / MiniMapFragProg.glsl
- ShapesVertProg.glsl / ShapesFragProg.glsl

**Map/Utility:**
- SMFBorderVertProg.glsl / SMFBorderFragProg.glsl
- SMFShadingTextureVertProg.glsl / SMFShadingTextureFragProg.glsl
- FullscreenTriangleVS.glsl / FullscreenTriangleTexFS.glsl

### Tasks

1. **Read each GLSL shader** to understand its inputs/outputs and any GLSL-specific features

2. **Run each through ShaderCompiler** (GLSL -> SPIR-V -> MSL)

3. **Verify MSL output** compiles with `xcrun -sdk macosx metal -c file.metal`

4. **Fix translation issues** - SPIRV-Cross may not handle all GLSL features perfectly.
   Common issues:
   - `gl_ClipDistance` - needs Metal `[[clip_distance]]` attribute
   - Preprocessor `#define` variants - shaders use `#ifdef` for feature toggling.
     Each variant may need separate compilation
   - Texture format qualifiers
   - Geometry shader features (if any)

5. **Save translated MSL** to `cont/base/springcontent/shaders/Metal/` with matching names
   (e.g., `ModelVertProg.metal`, `ModelFragProg.metal`)

6. **Create shader reflection data** for each shader pair, documenting the
   uniform-to-buffer-index mapping

7. **Set up metallib build step** - document the CMake additions needed:
   ```cmake
   # For each .metal file:
   # xcrun -sdk macosx metal -c file.metal -o file.air
   # xcrun -sdk macosx metallib file.air -o file.metallib
   ```

### Shader Variants

Some shaders use `#define` preprocessor directives for variants. The engine prepends
defines before compilation. For Metal, each variant combination needs a separate
MSL compilation or the defines need to be handled at runtime.

Check `Shader.cpp` to understand how `modDefStrs` and `rawDefStrs` work.

## Output
- Create branch `agent/shader-translator`
- Create `cont/base/springcontent/shaders/Metal/` directory
- Commit translated MSL files
- Document any shaders that fail translation and why
- Document the metallib build step for CMake
