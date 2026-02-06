/**
 * TextureCollection.cpp - Implementation of named texture collection.
 *
 * RHI Migration Status: COMPLETE
 * ===============================
 * Migration applied:
 *   - Replaced std::vector<uint32_t> textureIDs with
 *     std::vector<std::unique_ptr<RHI::IRHITexture>> textures
 *   - Removed glDeleteTextures calls - automatic via unique_ptr destruction
 *   - AddTexFromBitmap: Uses bitmap.CreateTextureRHI() instead of CreateMipMapTexture()
 *   - AddTexBlank: Uses bitmap.CreateTextureRHI() for consistency
 *   - GetTextureID: Returns texture->GetNativeHandle() for backward compatibility
 *   - Reload: Uses CreateTextureRHI() for texture recreation
 */

#include "TextureCollection.h"

#include <algorithm>
#include <iterator>

#include "Rendering/RHI/RHITexture.h"
#include "System/Misc/TracyDefs.h"

CTextureCollection::~CTextureCollection()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Automatic cleanup via unique_ptr destructors
}

bool CTextureCollection::TextureExists(const std::string& name)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto it = std::ranges::find(textureNames, name);
	return it != textureNames.end();
}

bool CTextureCollection::TextureExists(const std::string& name, const std::string& backupName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (TextureExists(name))
		return true;

	if (TextureExists(backupName))
		return true;

	return false;
}

uint32_t CTextureCollection::GetTextureID(const std::string& name)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto it = std::ranges::find(textureNames, name);
	if (it == textureNames.end())
		return 0;

	const size_t idx = std::distance(textureNames.begin(), it);
	const auto& texture = textures[idx];
	return texture ? texture->GetNativeHandle() : 0;
}

uint32_t CTextureCollection::GetTextureID(const std::string& name, const std::string& backupName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (auto texID = GetTextureID(name); texID != 0)
		return texID;

	if (backupName.empty())
		return 0;

	return GetTextureID(backupName);
}

size_t CTextureCollection::GetTexturePos(const std::string& name)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto it = std::ranges::find(textureNames, name);
	if (it == textureNames.end())
		return INVALID_TEXTURE_POS;

	return std::distance(textureNames.begin(), it);
}

size_t CTextureCollection::GetTexturePos(const std::string& name, const std::string& backupName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (auto pos = GetTexturePos(name); pos != INVALID_TEXTURE_POS)
		return pos;

	if (backupName.empty())
		return INVALID_TEXTURE_POS;

	return GetTexturePos(backupName);
}

size_t CTextureCollection::AddTexFromFile(const std::string& name, const std::string& filename)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CBitmap bitmap;
	if (!bitmap.Load(filename))
		return INVALID_TEXTURE_POS;

	return AddTexFromBitmap(name, filename, bitmap);
}

size_t CTextureCollection::AddTexFromBitmap(const std::string& name, const std::string& filename, const CBitmap& bitmap)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto texture = bitmap.CreateTextureRHI();
	if (!texture)
		return INVALID_TEXTURE_POS;

	textures.emplace_back(std::move(texture));
	textureNames.emplace_back(name);
	texturePaths.emplace_back(filename);

	return textures.size() - 1;
}

size_t CTextureCollection::AddTexBlank(std::string name, int xsize, int ysize, const SColor& c)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CBitmap bitmap;
	bitmap.AllocDummy(c);
	bitmap = bitmap.CreateRescaled(xsize, ysize);

	auto texture = bitmap.CreateTextureRHI();
	if (!texture)
		return INVALID_TEXTURE_POS;

	textures.emplace_back(std::move(texture));
	textureNames.emplace_back(name);
	texturePaths.emplace_back("");

	return textures.size() - 1;
}

bool CTextureCollection::DeleteTex(const std::string& name)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto it = std::ranges::find(textureNames, name);
	if (it == textureNames.end())
		return false;

	const size_t pos = std::distance(textureNames.begin(), it);
	if (pos == textureNames.size() - 1) {
		textureNames.pop_back();
		texturePaths.pop_back();
		textures.pop_back(); // Automatic deletion via unique_ptr
		return true;
	}

	textureNames[pos] = textureNames.back(); textureNames.pop_back();
	texturePaths[pos] = texturePaths.back(); texturePaths.pop_back();

	textures[pos] = std::move(textures.back()); // Automatic deletion via unique_ptr move
	textures.pop_back();
	return true;
}

void CTextureCollection::Reload()
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(textures.size() == textureNames.size() && textureNames.size() == texturePaths.size());
	for (size_t i = 0; i < textures.size(); ++i) {
		if (texturePaths[i].empty())
			continue; //skip fallback textures

		CBitmap bitmap;
		if (!bitmap.Load(texturePaths[i]))
			continue; //skip missing texture files

		// Recreate texture - old one will be automatically deleted
		auto newTexture = bitmap.CreateTextureRHI();
		if (newTexture) {
			textures[i] = std::move(newTexture);
		}
	}
}

const std::vector<uint32_t>& CTextureCollection::GetTextureIDs() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Rebuild cache for backward compatibility
	textureIDs.clear();
	textureIDs.reserve(textures.size());
	for (const auto& texture : textures) {
		textureIDs.push_back(texture ? texture->GetNativeHandle() : 0);
	}
	return textureIDs;
}
