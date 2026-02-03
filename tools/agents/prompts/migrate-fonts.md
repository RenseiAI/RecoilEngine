# Agent: migrate-fonts

## Purpose
Migrate font rendering from direct GL to RHI.

## Owned Files
- `rts/Rendering/Fonts/glFontRenderer.cpp` / `glFontRenderer.h`
- `rts/Rendering/Fonts/CFontTexture.cpp` / `CFontTexture.h`

## Context

See `migrate-template.md` for general migration rules.
The codebase is at: `/Volumes/Supaku/personal/supaku-org/lib/RecoilEngine`

- `glFontRenderer.cpp` - 26 GL calls, glyph rendering with texture atlas
- `CFontTexture.cpp` - 6 GL calls, font atlas texture management

Font rendering uses texture atlases (generated via Freetype) and renders textured
quads for each glyph. The migration is straightforward: replace GL texture and
draw calls with RHI equivalents.

## Output
- Create branch `agent/migrate-fonts`
- One commit per file
