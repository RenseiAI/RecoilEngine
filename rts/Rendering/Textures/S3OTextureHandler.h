/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef S3O_TEXTURE_HANDLER_H
#define S3O_TEXTURE_HANDLER_H

#include <cstdint>
#include <string>
#include <vector>

#include "Bitmap.h"
#include "System/Threading/SpringThreading.h"
#include "System/UnorderedMap.hpp"

struct S3DModel;
class CBitmap;

class CS3OTextureHandler
{
public:
	struct S3OTexMat {
		int num;

		uint32_t tex1;
		uint32_t tex2;

		uint32_t tex1SizeX;
		uint32_t tex1SizeY;

		uint32_t tex2SizeX;
		uint32_t tex2SizeY;
	};

	struct CachedS3OTex {
		uint32_t texID;
		uint32_t xsize;
		uint32_t ysize;
		bool invertAxis;
		bool invertAlpha;
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
