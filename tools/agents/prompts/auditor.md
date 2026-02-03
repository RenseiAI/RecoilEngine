# Agent: auditor

## Purpose
Verify the completeness of RHI migration by scanning for remaining direct GL calls
outside the OpenGL backend directory. Report any missed migrations.

## Owned Files
None (read-only agent). Produces a report file.

## Context

The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`

## Tasks

### 1. Scan for direct GL calls outside the GL backend

Search for these patterns in `rts/` EXCLUDING `rts/Rendering/RHI/OpenGL/` and
`rts/lib/` directories:

```
glDraw|glGen|glBind|glDelete|glEnable|glDisable|glBlend|glDepth|glStencil|
glViewport|glScissor|glClear|glTexImage|glTexParameter|glTexStorage|
glFramebuffer|glRenderbuffer|glBuffer|glMap|glUnmap|glUniform|glVertexAttrib|
glCreateShader|glCompileShader|glCreateProgram|glLinkProgram|glUseProgram|
glActiveTexture|glPixelStore|glReadPixels|glGetInteger|glGetFloat|glGetError|
GL_TRIANGLES|GL_TEXTURE_2D|GL_DEPTH_TEST|GL_BLEND
```

### 2. Categorize findings

For each remaining GL call, categorize as:
- **Direct call**: Still calling GL function directly (needs migration)
- **GL enum usage**: Using GL constants instead of RHI enums (needs conversion)
- **GL include**: Including `myGL.h` or `glad.h` directly (should use RHI headers)
- **Acceptable**: In test code, headless stubs, or platform-specific code (OK to keep)

### 3. Check RHI completeness

Verify that:
- All RHI interface methods have OpenGL implementations
- All RHI interface methods have Metal implementations (or stubs)
- The RHIFactory can create both backends
- The build system correctly selects the backend based on platform

### 4. Check shader pipeline

Verify that:
- All 41 GLSL shaders have corresponding MSL translations
- ShaderCompiler can compile all shaders without errors
- Reflection data is complete for all shaders

### 5. Produce report

Create `doc/AUDIT_REPORT.md` with:
- Total remaining GL calls by category
- Files still needing migration
- RHI coverage gaps
- Shader translation status
- Recommendations for resolution

## Output
- Create `doc/AUDIT_REPORT.md`
- Do NOT modify any source files
- Report should be actionable (specific file:line references)
