# Agent: lua-migrator

## Purpose
Migrate all Lua OpenGL bindings from direct GL calls to the RHI interface.

## Owned Files
- `rts/Lua/LuaOpenGL.cpp` / `LuaOpenGL.h`
- `rts/Lua/LuaTextures.cpp` / `LuaTextures.h`
- `rts/Lua/LuaFBOs.cpp` / `LuaFBOs.h`
- `rts/Lua/LuaRBOs.cpp` / `LuaRBOs.h`
- `rts/Lua/LuaVAOImpl.cpp` / `LuaVAOImpl.h`
- `rts/Lua/LuaVBOImpl.cpp` / `LuaVBOImpl.h`
- `rts/Lua/LuaShaders.cpp` / `LuaShaders.h`
- `rts/Lua/LuaOpenGLUtils.cpp` / `LuaOpenGLUtils.h`
- `rts/Lua/LuaMaterial.cpp` / `LuaMaterial.h`
- `rts/Lua/LuaFonts.cpp` / `LuaFonts.h`
- `rts/Lua/LuaDisplayLists.h`
- `rts/Lua/LuaParser.cpp`
- `rts/Lua/LuaConstGL.h`

## Prerequisites
- RHI interfaces exist
- ShaderCompiler exists (for Lua shader creation on Metal)

## Context

The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`

### Lua GL API

`LuaOpenGL.cpp` exposes ~70+ functions to Lua scripts. These are called by game
mods/widgets as `gl.Texture()`, `gl.DepthTest()`, `gl.DrawArrays()`, etc.

**250 total GL calls across 13 Lua files.**

The largest file is `LuaOpenGL.cpp` with 146 GL calls covering:
- Texture binding and management
- State functions (depth test, blending, culling, polygon mode)
- Draw functions (arrays, elements)
- Viewport, scissor, clear
- Matrix operations (if any remain)

### Key Challenge: Lua Shader Creation

Lua scripts can create custom GLSL shaders via `gl.CreateShader()`:
```lua
local shader = gl.CreateShader({
    vertex = [[
        void main() { gl_Position = ... }
    ]],
    fragment = [[
        void main() { gl_FragColor = ... }
    ]],
})
```

On the Metal backend, this GLSL must be compiled to MSL at runtime via the
ShaderCompiler (GLSL -> SPIR-V -> MSL). This adds latency to shader creation
but is the only way to support arbitrary Lua-provided GLSL on Metal.

### GL Constants

`LuaConstGL.h` exposes GL enum constants like `GL_TRIANGLES`, `GL_TEXTURE_2D`,
`GL_SRC_ALPHA` to Lua. These need to map to backend-agnostic RHI enums.

Create a mapping table:
```cpp
// GL_TRIANGLES (0x0004) -> RHIPrimitiveType::Triangles
// GL_TEXTURE_2D (0x0DE1) -> RHITextureType::Texture2D
```

Lua scripts should continue to use the same constant values (for backward compatibility).
The mapping happens inside the Lua binding functions.

### Migration Order
1. `LuaConstGL.h` - constant mapping (foundation for everything else)
2. `LuaOpenGLUtils.cpp` - utility functions
3. `LuaTextures.cpp` - texture management
4. `LuaFBOs.cpp` - FBO operations
5. `LuaRBOs.cpp` - render buffer objects
6. `LuaVBOImpl.cpp` / `LuaVAOImpl.cpp` - buffer/array objects
7. `LuaShaders.cpp` - shader creation (needs ShaderCompiler)
8. `LuaMaterial.cpp` - material system
9. `LuaFonts.cpp` - font rendering
10. `LuaDisplayLists.h` - display lists (consider deprecating)
11. `LuaParser.cpp` - minimal GL
12. `LuaOpenGL.cpp` - the big one, do last (146 GL calls)

## Output
- Create branch `agent/lua-migrator`
- One commit per file
- Test with at least one BAR widget to verify backward compatibility
