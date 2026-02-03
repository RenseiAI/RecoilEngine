# Migration Agent Template

This is the base template for all subsystem migration agents. Each migration agent
replaces direct OpenGL calls in its owned files with calls to the RHI interface.

## General Rules for ALL Migration Agents

### Prerequisites
- RHI interface headers exist (`rts/Rendering/RHI/RHI*.h`)
- OpenGL backend exists (`rts/Rendering/RHI/OpenGL/GL*.h`)
- The RHI is accessible via a global or singleton (check `RHIFactory`)

### Migration Pattern

For each file, the migration follows this pattern:

**Before (direct GL):**
```cpp
#include "Rendering/GL/myGL.h"
// ...
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LEQUAL);
glBindTexture(GL_TEXTURE_2D, texID);
glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
```

**After (through RHI):**
```cpp
#include "Rendering/RHI/RHIContext.h"
// ...
auto& ctx = RHI::GetContext();
ctx.SetDepthTest(true, RHICompareFunc::LessEqual);
ctx.BindTexture(0, texture);
ctx.DrawIndexed(RHIPrimitiveType::Triangles, count, 0);
```

### Rules

1. **Only modify files you own.** If you discover a needed change in a file outside
   your ownership, document it in your output summary.

2. **One commit per file.** Makes bisection easy if something breaks.

3. **Preserve behavior exactly.** The migration must be invisible to the user.
   No visual differences, no performance changes (or minimal).

4. **Include both old and new headers during transition.** The RHI OpenGL backend
   still calls GL internally, so `myGL.h` may still be needed transitively. But
   the migrated file should not call GL directly.

5. **Handle GL state save/restore.** If the code uses `GL::SubState` or pushes/pops
   state, convert to RHI equivalents (scoped pipeline state).

6. **Replace GL enums.** Convert `GL_TRIANGLES` -> `RHIPrimitiveType::Triangles`,
   `GL_TEXTURE_2D` -> `RHITextureType::Texture2D`, etc.

7. **Replace GL texture IDs.** Convert raw `GLuint` texture handles to `RHITexture*`
   pointers or references.

8. **Replace GL buffer IDs.** Convert `VBO` usage to `RHIBuffer` usage.

9. **Replace FBO usage.** Convert `FBO` usage to `RHIFramebuffer` usage.

### What NOT to Do

- Do NOT rewrite rendering algorithms. Only change the API calls.
- Do NOT optimize anything. Preserve existing logic exactly.
- Do NOT remove legacy code paths. Wrap them.
- Do NOT change shader loading (that's the shader-pipeline agent's job).
