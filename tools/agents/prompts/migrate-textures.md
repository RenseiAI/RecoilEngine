# Agent: migrate-textures

## Purpose
Migrate texture management and loading from direct GL to RHI.

## Owned Files
- `rts/Rendering/Textures/Texture.cpp` / `Texture.h`
- `rts/Rendering/Textures/Bitmap.cpp` / `Bitmap.h`
- `rts/Rendering/Textures/TextureCollection.cpp` / `TextureCollection.h`
- `rts/Rendering/Textures/TextureRenderAtlas.cpp` / `TextureRenderAtlas.h`
- `rts/Rendering/Textures/NamedTextures.cpp` / `NamedTextures.h`
- `rts/Rendering/Textures/S3OTextureHandler.cpp` / `S3OTextureHandler.h`
- `rts/Rendering/Textures/3DOTextureHandler.cpp` / `3DOTextureHandler.h`
- `rts/Rendering/Textures/nv_dds.cpp` / `nv_dds.h`
- `rts/Rendering/UnitDefImage.h`

## Context

See `migrate-template.md` for general migration rules.
The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`

### Key Pattern

Most texture code follows this pattern:
```cpp
GLuint texID;
glGenTextures(1, &texID);
glBindTexture(GL_TEXTURE_2D, texID);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
```

Replace with:
```cpp
auto texture = RHI::CreateTexture({
    .type = RHITextureType::Texture2D,
    .format = RHITextureFormat::RGBA8,
    .width = w, .height = h,
    .minFilter = RHIFilter::Linear,
    .magFilter = RHIFilter::Linear,
});
texture->Upload(data, 0, w * h * 4);
```

### Important: Texture handles are used everywhere

`GLuint` texture IDs are stored in many places throughout the engine. The migration
needs to either:
1. Replace `GLuint` with `RHITexture*` in the texture management classes
2. Or provide a mapping layer from texture ID to RHITexture handle

Option 1 is cleaner but requires more changes. Option 2 is safer for incremental migration.

## Output
- Create branch `agent/migrate-textures`
- One commit per file
