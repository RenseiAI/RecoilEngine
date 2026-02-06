/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * C3DOTextureHandler - Manages 3DO (Total Annihilation) model texture atlases.
 *
 * RHI Migration Status: COMPLETE (Texture Ownership)
 * ==================================================
 * - Init() uses RHI::GetDevice()->CreateTexture() for atlas creation
 * - Stores std::unique_ptr<RHI::IRHITexture> (atlas3do1, atlas3do2) for ownership
 * - Automatic cleanup via unique_ptr destructors (no manual glDeleteTextures)
 * - GetAtlasTex1ID/GetAtlasTex2ID provide backward compat via GetNativeHandle()
 *
 * Note: Still depends on GL for RecoilBuildMipmaps() - see 3DOTextureHandler.cpp
 */

#ifndef _3DO_TEXTURE_HANDLER_H
#define _3DO_TEXTURE_HANDLER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Rendering/GL/myGL.h" // still needed transitively by GL backend
#include "Rendering/RHI/RHITexture.h"
#include "Rendering/Textures/AtlasedTexture.hpp"
#include "Rendering/Textures/TAPalette.h"
#include "System/float4.h"
#include "System/UnorderedMap.hpp"

struct TexFile;

class C3DOTextureHandler
{
public:
	using UnitTexture = float4;

	void Init();
	void Kill();

	// NOTE: safe with unordered_map after all textures have been loaded
	UnitTexture* Get3DOTexture(const std::string& name);

	// Legacy interface - returns native GL handle for backward compat
	unsigned int GetAtlasTex1ID() const { return atlas3do1 ? atlas3do1->GetNativeHandle() : 0; }
	unsigned int GetAtlasTex2ID() const { return atlas3do2 ? atlas3do2->GetNativeHandle() : 0; }

	// RHI interface - preferred for new code
	RHI::IRHITexture* GetAtlasTex1() const { return atlas3do1.get(); }
	RHI::IRHITexture* GetAtlasTex2() const { return atlas3do2.get(); }

	unsigned int GetAtlasTexSizeX() const { return bigTexX; }
	unsigned int GetAtlasTexSizeY() const { return bigTexY; }

	const spring::unordered_map<std::string, UnitTexture>& GetAtlasTextures() const { return textures; }

private:
	std::vector<TexFile> LoadTexFiles();

	static TexFile CreateTex(const std::string& name, const std::string& name2, bool teamcolor = false);

private:
	spring::unordered_map<std::string, UnitTexture> textures;

	CTAPalette palette;

	std::unique_ptr<RHI::IRHITexture> atlas3do1;
	std::unique_ptr<RHI::IRHITexture> atlas3do2;
	int bigTexX = 0;
	int bigTexY = 0;
};

extern C3DOTextureHandler textureHandler3DO;

#endif /* _3DO_TEXTURE_HANDLER_H */
