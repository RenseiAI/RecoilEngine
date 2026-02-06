#pragma once

/**
 * CTextureCollection - Manages a collection of named textures.
 *
 * RHI Migration Status: COMPLETE
 * ===============================
 * - Stores std::unique_ptr<RHI::IRHITexture> instead of raw GLuint IDs
 * - Uses automatic RHI cleanup via unique_ptr destructors
 * - GetTextureID() returns GetNativeHandle() for backward compatibility
 *
 * Pattern mappings applied:
 *   glDeleteTextures(n, &ids)  ->  unique_ptr reset/clear (automatic cleanup)
 *   raw GLuint storage         ->  std::unique_ptr<RHI::IRHITexture>
 *   GetTextureID() return      ->  texture->GetNativeHandle()
 */

#include <vector>
#include <memory>
#include "Bitmap.h"
#include "System/Color.h"

namespace RHI { class IRHITexture; }

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

	size_t GetTexturesCount() const { return textures.size(); }
	// Backward compatibility: returns raw texture IDs for legacy code
	const std::vector<uint32_t>& GetTextureIDs() const;
	// Direct access to RHI textures for RHI-aware code
	RHI::IRHITexture* GetTexture(size_t index) const {
		return (index < textures.size()) ? textures[index].get() : nullptr;
	}
private:
	std::vector<std::string> textureNames;
	std::vector<std::string> texturePaths;
	std::vector<std::unique_ptr<RHI::IRHITexture>> textures;
	mutable std::vector<uint32_t> textureIDs; // Cached for GetTextureIDs() backward compat
	static constexpr size_t INVALID_TEXTURE_POS = size_t(-1);
};