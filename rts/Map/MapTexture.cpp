/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "MapTexture.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/RHI/RHITexture.h"

MapTexture::MapTexture()
	: texIDs{0, 0}
	, texDims{}
	, rhiTextures{}
{
}

MapTexture::~MapTexture() {
	// RHI textures are RAII; they will be deleted automatically via unique_ptr
	// Only delete raw GL texture if it wasn't wrapped by RHI
	if (rhiTextures[RAW_TEX_IDX] == nullptr && texIDs[RAW_TEX_IDX] != 0) {
		glDeleteTextures(1, &texIDs[RAW_TEX_IDX]);
	}

	// Do NOT delete a Lua-set texture here (managed by Lua)
	texIDs[RAW_TEX_IDX] = 0;
	texIDs[LUA_TEX_IDX] = 0;
}

void MapTexture::SetRawRHITexture(std::unique_ptr<RHI::IRHITexture> tex) {
	rhiTextures[RAW_TEX_IDX] = std::move(tex);
}

void MapTexture::SetLuaRHITexture(std::unique_ptr<RHI::IRHITexture> tex) {
	rhiTextures[LUA_TEX_IDX] = std::move(tex);
}
