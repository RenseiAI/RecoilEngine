/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * CS3OTextureHandler - Manages S3O model textures (diffuse + secondary).
 *
 * RHI Migration Status: COMPLETE
 * ===============================
 * - Texture storage migrated to std::shared_ptr<RHI::IRHITexture>
 * - Texture creation via CBitmap::CreateTextureRHI()
 * - Automatic cleanup via shared_ptr (no glDeleteTextures)
 * - Backward-compatible getters via GetNativeHandle()
 *
 * Note: S3O models use two-texture material (tex1=diffuse+teamcolor, tex2=glow+reflect)
 */

#ifndef S3O_TEXTURE_HANDLER_H
#define S3O_TEXTURE_HANDLER_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include "Bitmap.h"
#include "Rendering/RHI/RHITexture.h"
#include "System/Threading/SpringThreading.h"
#include "System/UnorderedMap.hpp"

struct S3DModel;
class CBitmap;

class CS3OTextureHandler
{
public:
	struct S3OTexMat {
		int num;

		// RHI texture ownership
		std::shared_ptr<RHI::IRHITexture> tex1RHI;
		std::shared_ptr<RHI::IRHITexture> tex2RHI;

		// Backward-compatible raw texture handles for legacy consumers
		// Populated from RHI textures via GetNativeHandle()
		uint32_t tex1;
		uint32_t tex2;

		uint32_t tex1SizeX;
		uint32_t tex1SizeY;

		uint32_t tex2SizeX;
		uint32_t tex2SizeY;

		// Update raw handles from RHI textures (implemented in .cpp)
		void UpdateNativeHandles();
	};

	struct CachedS3OTex {
		std::shared_ptr<RHI::IRHITexture> texture;
		uint32_t xsize;
		uint32_t ysize;
		bool invertAxis;
		bool invertAlpha;

		// Backward-compatible getter for legacy consumers (implemented in .cpp)
		uint32_t GetNativeHandle() const;
	};

	void Init();
	void Kill();
	void Reload();

	void LoadTexture(S3DModel* model);
	void PreloadTexture(S3DModel* model, bool invertAxis, bool invertAlpha);

public:
	const S3OTexMat* GetTexture(uint32_t num) {
		if (num < textures.size())
			return &textures[num];

		return nullptr;
	}

private:
	uint32_t LoadAndCacheTexture(
		const S3DModel* model,
		uint32_t texNum,
		bool invertAxis,
		bool invertAlpha,
		bool preloadCall
	);
	uint32_t InsertTextureMat(const S3DModel* model);

private:
	typedef spring::unsynced_map<std::string, CachedS3OTex> TextureCache;
	typedef spring::unsynced_map<std::string, CBitmap> BitmapCache;
	typedef spring::unsynced_map<std::uint64_t, uint32_t> TextureTable;

	TextureCache textureCache; // stores individual primary- and secondary-textures by name
	TextureTable textureTable; // stores (primary, secondary) texture-pairs by unique ident
	BitmapCache bitmapCache;

	std::vector<S3OTexMat> textures;
};

extern CS3OTextureHandler textureHandlerS3O;

#endif /* S3O_TEXTURE_HANDLER_H */
