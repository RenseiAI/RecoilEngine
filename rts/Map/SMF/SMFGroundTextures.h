/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _SMF_GROUND_TEXTURES_H_
#define _SMF_GROUND_TEXTURES_H_

#include <memory>
#include <vector>

#include "Map/BaseGroundTextures.h"
#include "Rendering/GL/PBO.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHITexture.h"

class CSMFMapFile;
class CSMFReadMap;

class CSMFGroundTextures: public CBaseGroundTextures
{
public:
	CSMFGroundTextures(CSMFReadMap* rm);
	~CSMFGroundTextures() override;

	void DrawUpdate();
	bool SetSquareLuaTexture(int texSquareX, int texSquareY, int texID);
	bool GetSquareLuaTexture(int texSquareX, int texSquareY, int texID, int texSizeX, int texSizeY, int lodMin, int lodMax);
	void BindSquareTexture(int texSquareX, int texSquareY);

protected:
	void LoadTiles(CSMFMapFile& file);
	void LoadSquareTextures(const int mipLevel);
	void LoadSquareTexturesPersistent();
	void ConvolveHeightMap(const int mapWidth, const int mipLevel);
	bool RecompressTilesIfNeeded();
	void ExtractSquareTiles(const int texSquareX, const int texSquareY, const int mipLevel, GLint* tileBuf) const;
	void LoadSquareTexture(int x, int y, int level);
	void LoadSquareTexturePersistent(int x, int y);

	inline bool TexSquareInView(int, int) const;

	CSMFReadMap* smfMap;

private:
	struct GroundSquare {
		GroundSquare(): luaTextureID(0), texMipLevel(0), texDrawFrame(1) {}
		~GroundSquare() = default;
		GroundSquare(GroundSquare&&) = default;
		GroundSquare& operator=(GroundSquare&&) = default;
		GroundSquare(const GroundSquare&) = delete;
		GroundSquare& operator=(const GroundSquare&) = delete;

		bool HasLuaTexture() const { return (luaTextureID != 0); }

		void SetRawTexture(std::unique_ptr<RHI::IRHITexture> tex) { rhiTexture = std::move(tex); }
		void SetLuaTexture(unsigned int id) { luaTextureID = id; }
		void SetMipLevel(unsigned int l) { texMipLevel = l; }
		void SetDrawFrame(unsigned int f) { texDrawFrame = f; }

		RHI::IRHITexture* GetRHITexture() const { return rhiTexture.get(); }
		unsigned int GetLuaTextureID() const { return luaTextureID; }
		unsigned int GetMipLevel() const { return texMipLevel; }
		unsigned int GetDrawFrame() const { return texDrawFrame; }

	private:
		std::unique_ptr<RHI::IRHITexture> rhiTexture;
		unsigned int luaTextureID;
		unsigned int texMipLevel;
		unsigned int texDrawFrame;
	};

	// note: intentionally declared static (see ReadMap)
	static std::vector<GroundSquare> squares;

	static std::vector<int> tileMap;
	static std::vector<char> tiles;

	// FIXME? these are not updated at runtime
	static std::vector<float> heightMaxima;
	static std::vector<float> heightMinima;
	static std::vector<float> stretchFactors;

	// use Pixel Buffer Objects for async. uploading (DMA)
	PBO pbo;

	RHI::TextureFormat rhiTexFormat = RHI::TextureFormat::CompressedDXT1;
	// unsigned int pboUnsyncedBit = 0;
	bool smfTextureStreaming = false;
	float smfTextureLodBias = 0.0f;
};

#endif // _BF_GROUND_TEXTURES_H_
