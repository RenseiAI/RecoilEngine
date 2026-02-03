# Metal Shaders for Recoil Engine

This directory contains Metal Shading Language (MSL) translations of the GLSL shaders
used by Recoil Engine. These shaders are required for the macOS/iOS Metal backend.

## Translation Overview

All 41 GLSL shaders have been manually translated to Metal Shading Language.
The translation handles:

- Legacy GLSL built-ins (gl_ModelViewMatrix, gl_ProjectionMatrix, gl_Fog, etc.)
- GLSL varying/attribute syntax to Metal stage_in/stage_out
- Texture sampling differences (sampler2D -> texture2d + sampler)
- User clip planes (gl_ClipVertex -> [[clip_distance]])
- Shadow comparison samplers (sampler2DShadow -> depth2d)

## Shader Files

### Core Rendering (Model)
| GLSL | Metal | Description |
|------|-------|-------------|
| ModelVertProg.glsl | ModelVertProg.metal | Legacy unit/feature vertex shader |
| ModelFragProg.glsl | ModelFragProg.metal | Legacy unit/feature fragment shader |
| ModelVertProgGL4.glsl | ModelVertProgGL4.metal | Modern GL4 model vertex with bone animation |
| ModelFragProgGL4.glsl | ModelFragProgGL4.metal | Modern GL4 model fragment shader |

### Core Rendering (Terrain - SMF)
| GLSL | Metal | Description |
|------|-------|-------------|
| SMFVertProg.glsl | SMFVertProg.metal | Terrain vertex shader |
| SMFFragProg.glsl | SMFFragProg.metal | Terrain fragment shader |
| SMFBorderVertProg.glsl | SMFBorderVertProg.metal | Terrain border vertex shader |
| SMFBorderFragProg.glsl | SMFBorderFragProg.metal | Terrain border fragment shader |
| SMFShadingTextureVertProg.glsl | SMFShadingTextureVertProg.metal | Shading texture generation VS |
| SMFShadingTextureFragProg.glsl | SMFShadingTextureFragProg.metal | Shading texture generation FS |

### Shadow Generation
| GLSL | Metal | Description |
|------|-------|-------------|
| ShadowGenVertProg.glsl | ShadowGenVertProg.metal | Legacy shadow vertex shader |
| ShadowGenFragProg.glsl | ShadowGenFragProg.metal | Legacy shadow fragment shader |
| ShadowGenVertProgGL4.glsl | ShadowGenVertProgGL4.metal | GL4 shadow vertex with bones |
| ShadowGenFragProgGL4.glsl | ShadowGenFragProgGL4.metal | GL4 shadow fragment shader |
| ShadowGenVertMapProg.glsl | ShadowGenVertMapProg.metal | Map shadow vertex shader |

### Environment
| GLSL | Metal | Description |
|------|-------|-------------|
| BumpWaterVS.glsl | BumpWaterVS.metal | Bump-mapped water vertex shader |
| BumpWaterFS.glsl | BumpWaterFS.metal | Bump-mapped water fragment shader |
| BumpWaterCoastBlurVS.glsl | BumpWaterCoastBlurVS.metal | Coast blur vertex shader |
| BumpWaterCoastBlurFS.glsl | BumpWaterCoastBlurFS.metal | Coast blur fragment shader |
| GrassVertProg.glsl | GrassVertProg.metal | Grass vegetation vertex shader |
| GrassFragProg.glsl | GrassFragProg.metal | Grass vegetation fragment shader |
| ModernSkyVS.glsl | ModernSkyVS.metal | Procedural sky vertex shader |
| ModernSkyFS.glsl | ModernSkyFS.metal | Procedural sky fragment shader |
| GroundDecalsVertProg.glsl | GroundDecalsVertProg.metal | Ground decals vertex shader |
| GroundDecalsFragProg.glsl | GroundDecalsFragProg.metal | Ground decals fragment shader |
| CubeMapVS.glsl | CubeMapVS.metal | Skybox cube map vertex shader |
| CubeMapFS.glsl | CubeMapFS.metal | Skybox cube map fragment shader |
| EquiRectConverterFS.glsl | EquiRectConverterFS.metal | Equirectangular to cubemap converter |

### Effects
| GLSL | Metal | Description |
|------|-------|-------------|
| ProjFXVertProg.glsl | ProjFXVertProg.metal | Projectile effects vertex shader |
| ProjFXFragProg.glsl | ProjFXFragProg.metal | Projectile effects fragment shader |
| ProjFXVertShadowProg.glsl | ProjFXVertShadowProg.metal | Projectile shadow vertex shader |
| ProjFXFragShadowProg.glsl | ProjFXFragShadowProg.metal | Projectile shadow fragment shader |

### UI
| GLSL | Metal | Description |
|------|-------|-------------|
| Icons2DVS.glsl | Icons2DVS.metal | 2D icons vertex shader |
| Icons3DVS.glsl | Icons3DVS.metal | 3D icons vertex shader |
| IconsFS.glsl | IconsFS.metal | Icons fragment shader |
| MiniMapVertProg.glsl | MiniMapVertProg.metal | MiniMap vertex shader |
| MiniMapFragProg.glsl | MiniMapFragProg.metal | MiniMap fragment shader |
| ShapesVertProg.glsl | ShapesVertProg.metal | Shapes vertex shader |
| ShapesFragProg.glsl | ShapesFragProg.metal | Shapes fragment shader |

### Utility
| GLSL | Metal | Description |
|------|-------|-------------|
| FullscreenTriangleVS.glsl | FullscreenTriangleVS.metal | Fullscreen triangle vertex shader |
| FullscreenTriangleTexFS.glsl | FullscreenTriangleTexFS.metal | Fullscreen triangle fragment shader |

## Shader Variants

Many shaders use preprocessor defines for compile-time variants:
- `USE_SHADOWS` - Enable shadow mapping
- `DEFERRED_MODE` - G-buffer output for deferred rendering
- `HAVE_SHADOWS` - Shadow receiving
- `SHADOW_GEN` - Shadow map generation pass
- `DISTANCE_FAR` - Billboard mode for grass
- `HIGH_QUALITY` - MSAA depth buffer
- `SMF_ADV_SHADING` - Advanced terrain shading
- `SMF_WATER_ABSORPTION` - Water depth effects
- `USE_TEXTURE_ARRAY` - Texture array support
- `SMOOTH_PARTICLES` - Soft particle rendering
- `SIMPLIFIED_RENDERING` - Reduced cloud detail

These variants should be handled by compiling multiple versions of each shader
or using Metal function constants.

## Uniform Buffer Layout

Metal shaders use argument buffers for uniforms. The buffer bindings are:
- `[[buffer(0)]]` - Primary uniforms (matrices, params)
- `[[buffer(1)]]` - Secondary uniforms (fog, lighting)
- `[[buffer(2)]]` - Shader-specific uniforms
- `[[buffer(3)]]` - Transform buffer (for bone animation)

Texture bindings follow the order defined in each shader's parameters.

## Building Metal Libraries

### CMake Integration

Add to the CMake build system:

```cmake
if(APPLE)
    # Find all .metal files
    file(GLOB METAL_SOURCES
        "${CMAKE_SOURCE_DIR}/cont/base/springcontent/shaders/Metal/*.metal")

    # Create a custom target for shader compilation
    set(METALLIB_OUTPUT "${CMAKE_BINARY_DIR}/shaders/default.metallib")
    set(AIR_FILES "")

    foreach(METAL_SOURCE ${METAL_SOURCES})
        get_filename_component(SHADER_NAME ${METAL_SOURCE} NAME_WE)
        set(AIR_FILE "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.air")
        list(APPEND AIR_FILES ${AIR_FILE})

        add_custom_command(
            OUTPUT ${AIR_FILE}
            COMMAND xcrun -sdk macosx metal
                -c ${METAL_SOURCE}
                -o ${AIR_FILE}
                -std=metal2.1
            DEPENDS ${METAL_SOURCE}
            COMMENT "Compiling ${SHADER_NAME}.metal"
        )
    endforeach()

    add_custom_command(
        OUTPUT ${METALLIB_OUTPUT}
        COMMAND xcrun -sdk macosx metallib
            ${AIR_FILES}
            -o ${METALLIB_OUTPUT}
        DEPENDS ${AIR_FILES}
        COMMENT "Creating default.metallib"
    )

    add_custom_target(MetalShaders ALL DEPENDS ${METALLIB_OUTPUT})
endif()
```

### Manual Compilation

For each .metal file:
```bash
# Compile to AIR (Apple Intermediate Representation)
xcrun -sdk macosx metal -c file.metal -o file.air -std=metal2.1

# Link to metallib
xcrun -sdk macosx metallib *.air -o default.metallib
```

### Variant Compilation

For shaders with preprocessor variants, compile each variant:
```bash
# Example: ModelFragProg with shadows
xcrun -sdk macosx metal -c ModelFragProg.metal -o ModelFragProg_shadows.air \
    -DUSE_SHADOWS=1 -std=metal2.1

# Example: ModelFragProg without shadows
xcrun -sdk macosx metal -c ModelFragProg.metal -o ModelFragProg_noshadows.air \
    -std=metal2.1
```

## Known Limitations

1. **gl_LightSource array**: The legacy GLSL built-in light array is not fully
   implemented. Dynamic lighting for models uses a simplified approach.

2. **Geometry shaders**: Metal does not support geometry shaders. Any GLSL
   geometry shader functionality would need to be moved to vertex shaders
   or compute shaders.

3. **Texture format qualifiers**: Some GLSL texture format qualifiers may
   need adjustment for Metal's format system.

4. **Integer texture sampling**: Metal requires explicit integer texture types
   (texture2d<int>) which may need adjustment in some shaders.

## Testing

To verify Metal shader compilation:
```bash
cd cont/base/springcontent/shaders/Metal
for f in *.metal; do
    echo "Testing $f..."
    xcrun -sdk macosx metal -c "$f" -o /tmp/test.air -std=metal2.1 || echo "FAILED: $f"
done
```
