#pragma once

/**
 * CTextureCollection - Manages a collection of named textures.
 *
 * RHI Migration Status: PARTIAL
 * ============================
 * - Currently stores raw GLuint texture IDs from CBitmap::CreateMipMapTexture()
 * - Uses glDeleteTextures for cleanup (see TextureCollection.cpp)
 *
 * Migration Path:
 * 1. When CBitmap::CreateTextureRHI() becomes the primary path, store
 *    std::unique_ptr<RHI::IRHITexture> instead of raw uint32_t IDs
 * 2. Replace glDeleteTextures calls with RHI destructor cleanup
 * 3. GetTextureID() would return GetNativeHandle() for backward compat
 *
 * Pattern mappings:
 *   glDeleteTextures(n, &ids)  ->  unique_ptr reset/clear (automatic cleanup)
 *   raw GLuint storage         ->  std::unique_ptr<RHI::IRHITexture>
 *   GetTextureID() return      ->  texture->GetNativeHandle()
 */

#include <vector>
#include "Bitmap.h"
#include "System/Color.h"

class CTextureCollection {
public:
	~CTextureCollection();
	CTextureCollection() = default;
	CTextureCollection(CTextureCollection&&) = default;
	CTextureCollection(const CTextureCollection&) = delete;

	CTextureCollection& operator=(CTextureCollection&&) = default;
	CTextureCollection& operator=(const CTextureCollection&) = delete;

	bool TextureExists(const std::string& name);
	bool TextureExists(const std::string& name, const std::string& backupName);

	uint32_t GetTextureID(const std::string& name);
	uint32_t GetTextureID(const std::string& name, const std::string& backupName);
	size_t GetTexturePos(const std::string& name);
	size_t GetTexturePos(const std::string& name, const std::string& backupName);

	size_t AddTexFromFile(const std::string& name, const std::string& filename);
	size_t AddTexFromBitmap(const std::string& name, const std::string& filename, const CBitmap& bitmap);
	size_t AddTexBlank(std::string name, int xsize, int ysize, const SColor& c);

	bool DeleteTex(const std::string& name);

	void Reload();

	size_t GetTexturesCount() const { return textureIDs.size(); }
	const auto& GetTextureIDs() const { return textureIDs; }
private:
	std::vector<std::string> textureNames;
	std::vector<std::string> texturePaths;
	std::vector<uint32_t> textureIDs;
	static constexpr size_t INVALID_TEXTURE_POS = size_t(-1);
};