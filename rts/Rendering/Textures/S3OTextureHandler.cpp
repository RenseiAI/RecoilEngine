/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * S3OTextureHandler.cpp - S3O model texture loading and caching.
 *
 * RHI Migration Status: COMPLETE
 * ===============================
 * - All texture storage migrated to std::shared_ptr<RHI::IRHITexture>
 * - Texture creation via CBitmap::CreateTextureRHI()
 * - Automatic cleanup via shared_ptr destruction
 * - No remaining GL calls (glDeleteTextures removed)
 */

#include "S3OTextureHandler.h"
#include "Rendering/RHI/RHITexture.h"

#include "System/FileSystem/FileHandler.h"
#include "System/FileSystem/SimpleParser.h"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Units/UnitDrawer.h"
#include "Rendering/Models/3DModel.hpp"
#include "Rendering/Models/ModelsLock.h"
#include "Rendering/Textures/Bitmap.h"
#include "System/StringUtil.h"
#include "System/Exceptions.h"
#include "System/Log/ILog.h"
#include "System/Platform/Threading.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

#include "System/Misc/TracyDefs.h"

#define LOG_SECTION_TEXTURE "Texture"
LOG_REGISTER_SECTION_GLOBAL(LOG_SECTION_TEXTURE)
#ifdef LOG_SECTION_CURRENT
        #undef LOG_SECTION_CURRENT
#endif
#define LOG_SECTION_CURRENT LOG_SECTION_TEXTURE

#define TEX_MAT_UID(pTxID, sTxID) ((std::uint64_t(pTxID) << 32u) | sTxID)


// The S3O texture handler uses two textures.
// The first contains diffuse color (RGB) and teamcolor (A)
// The second contains glow (R), reflectivity (G) and 1-bit Alpha (A).

//////////////////////////////////////////////////////////////////////
// S3OTexMat and CachedS3OTex method implementations
//////////////////////////////////////////////////////////////////////

void CS3OTextureHandler::S3OTexMat::UpdateNativeHandles()
{
	tex1 = tex1RHI ? tex1RHI->GetNativeHandle() : 0;
	tex2 = tex2RHI ? tex2RHI->GetNativeHandle() : 0;
}

uint32_t CS3OTextureHandler::CachedS3OTex::GetNativeHandle() const
{
	return texture ? texture->GetNativeHandle() : 0;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CS3OTextureHandler textureHandlerS3O;

void CS3OTextureHandler::Init()
{
	RECOIL_DETAILED_TRACY_ZONE;
	textures.reserve(128);

	// dummies
	textures.emplace_back();
	textures.emplace_back();
}

void CS3OTextureHandler::Kill()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Textures are now owned by shared_ptr and will be automatically cleaned up
	textures.clear();
	textureCache.clear();
	textureTable.clear();
	bitmapCache.clear();
}

void CS3OTextureHandler::Reload()
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto lock = CModelsLock::GetScopedLock(); //needed?

	for (auto& [texName, texData] : textureCache) {
		if (!texData.texture)
			continue;

		CBitmap bitmap;
		if (!bitmap.Load(texName) && !bitmap.Load("unittextures/" + texName))
			continue;

		{
			if (texData.invertAlpha)
				bitmap.InvertAlpha();
			if (texData.invertAxis)
				bitmap.ReverseYAxis();

			// Recreate the texture with RHI
			auto newTexture = bitmap.CreateTextureRHI();
			if (newTexture) {
				texData.texture = std::move(newTexture);
			}
		}
	}
}


void CS3OTextureHandler::PreloadTexture(S3DModel* model, bool invertAxis, bool invertAlpha)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto lock = CModelsLock::GetScopedLock();

	LoadAndCacheTexture(model, 0, invertAxis, invertAlpha, true);
	LoadAndCacheTexture(model, 1, invertAxis,       false, true); // never invert alpha for tex2
}


void CS3OTextureHandler::LoadTexture(S3DModel* model)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto lock = CModelsLock::GetScopedLock();

	const unsigned int tex1ID = LoadAndCacheTexture(model, 0, false, false, false);
	const unsigned int tex2ID = LoadAndCacheTexture(model, 1, false, false, false);

	const auto texTableIter = textureTable.find(TEX_MAT_UID(tex1ID, tex2ID));

	// even if both textures were already loaded as parts of
	// other models, their pair might form a unique material
	if (texTableIter == textureTable.end()) {
		model->textureType = InsertTextureMat(model);
	} else {
		model->textureType = texTableIter->second;
	}
}

unsigned int CS3OTextureHandler::LoadAndCacheTexture(
	const S3DModel* model,
	unsigned int texNum,
	bool invertAxis,
	bool invertAlpha,
	bool preloadCall
) {
	RECOIL_DETAILED_TRACY_ZONE;
	CBitmap* bitmap = nullptr;

	const auto& textureName = model->texs[texNum];
	const auto textureIt = textureCache.find(textureName);

	if (textureIt != textureCache.end() && textureIt->second.texture)
		return textureIt->second.GetNativeHandle();

	const auto bitmapIt = bitmapCache.find(textureName);

	if (bitmapIt != bitmapCache.end()) {
		// bitmap was previously preloaded but not yet loaded
		// if !preloading, we will now turn the bitmap into a
		// texture and cache it
		bitmap = &(bitmapIt->second);
	} else {
		// bitmap was not yet preloaded, meaning we are the
		// first to (all non-3DO model textures are always
		// preloaded)
#ifndef HEADLESS //?
		assert(preloadCall);
#endif

		auto pair = bitmapCache.emplace(textureName, CBitmap{});
		auto iter = pair.first;

		bitmap = &(iter->second);

		if (!bitmap->Load(textureName) && !bitmap->Load("unittextures/" + textureName)) {
			if (texNum == 0)
				LOG_L(L_WARNING, "[%s] could not load primary texture \"%s\" from model \"%s\"", __func__, textureName.c_str(), model->name.c_str());

			// file not found (or headless build), set a single pixel so model is visible
			bitmap->AllocDummy(SColor(255 * (texNum == 0), 0, 0, 255 * (1 - invertAlpha)));
		}

		if (invertAxis)
			bitmap->ReverseYAxis();
		if (invertAlpha)
			bitmap->InvertAlpha();
	}

	std::shared_ptr<RHI::IRHITexture> texture;
	if (!preloadCall) {
		auto uniqueTexture = bitmap->CreateTextureRHI();
		if (uniqueTexture) {
			texture = std::move(uniqueTexture);
		}
	}

#ifndef HEADLESS
	assert(preloadCall || texture);
#endif

	if (textureIt != textureCache.end() && texture) {
		assert(!preloadCall);
		textureIt->second.texture = texture;
	}
	else {
		//save main params from the preloadCall pass, such that data is stored correctly for Reload()
#ifndef HEADLESS //?
		assert( preloadCall);
#endif
		textureCache[textureName] = {
			texture,
			static_cast<uint32_t>(bitmap->xsize),
			static_cast<uint32_t>(bitmap->ysize),
			invertAxis,
			invertAlpha
		};
	}

	// don't generate a texture yet, just save the bitmap for later
	if (preloadCall)
		return 0;

	bitmapCache.erase(textureName);
	return texture ? texture->GetNativeHandle() : 0;
}


unsigned int CS3OTextureHandler::InsertTextureMat(const S3DModel* model)
{
	const CachedS3OTex& tex1 = textureCache[ model->texs[0] ];
	const CachedS3OTex& tex2 = textureCache[ model->texs[1] ];

	textures.emplace_back();

	S3OTexMat& texMat = textures.back();

	texMat.num       = textures.size() - 1;
	texMat.tex1RHI   = tex1.texture;
	texMat.tex2RHI   = tex2.texture;
	texMat.tex1SizeX = tex1.xsize;
	texMat.tex1SizeY = tex1.ysize;
	texMat.tex2SizeX = tex2.xsize;
	texMat.tex2SizeY = tex2.ysize;

	// Update backward-compatible raw handles
	texMat.UpdateNativeHandles();

	// Use native handles for the texture table UID
	textureTable[TEX_MAT_UID(texMat.tex1, texMat.tex2)] = texMat.num;

	return texMat.num;
}

