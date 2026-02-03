/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * CBitmap - Image loading, manipulation, and texture creation.
 *
 * RHI Migration Status: MOSTLY COMPLETE
 * =====================================
 * RHI texture creation methods already implemented:
 *   - CreateTextureRHI() - creates RHI texture with ownership (preferred)
 *   - CreateDDSTextureRHI() - creates RHI texture from compressed DDS data
 *
 * Legacy GL methods still available (for backward compatibility):
 *   - CreateTexture() - returns raw GLuint
 *   - CreateMipMapTexture() - returns raw GLuint with mipmaps
 *   - CreateDDSTexture() - returns raw GLuint from DDS data
 *
 * Remaining RHI Gaps:
 *   - textype member stores GL enum values (GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP)
 *     TODO: Map to RHI::TextureType when used
 *   - dataType member stores GL data type (GL_UNSIGNED_BYTE, GL_FLOAT)
 *     TODO: Map to RHI data type when needed
 *   - Alloc() glType parameter uses GL enum values
 *
 * Migration Guide for Callers:
 *   // Before (GL):
 *   uint32_t texID = bitmap.CreateMipMapTexture();
 *   glBindTexture(GL_TEXTURE_2D, texID);
 *   // ... use texture ...
 *   glDeleteTextures(1, &texID);
 *
 *   // After (RHI):
 *   auto texture = bitmap.CreateTextureRHI();
 *   texture->Bind(0);
 *   // ... use texture ...
 *   // automatic cleanup via unique_ptr
 */

#ifndef _BITMAP_H
#define _BITMAP_H

#include <stdint.h>
#include <string>
#include <span>
#include <vector>
#include <memory>
#ifndef HEADLESS
	#include "nv_dds.h"
#endif // !HEADLESS
#include "System/float3.h"
#include "System/Color.h"
#include "Rendering/Textures/TextureCreationParams.hpp"

namespace RHI { class IRHITexture; }


struct SDL_Surface;

struct TextureCreationParams {
	float aniso = 0.0f;
	float lodBias = 0.0f;
	uint32_t texID = 0;
	int32_t reqNumLevels = 0;
	bool linearMipMapFilter = true;
	bool linearTextureFilter = true;
	uint32_t GetMinFilter(int32_t numLevels) const;
	uint32_t GetMagFilter() const;
};

class CBitmap {
public:
	CBitmap();
	CBitmap(const uint8_t* data, int xsize, int ysize, int channels = 4, uint32_t reqDataType = 0);
	CBitmap(const CBitmap& bmp): CBitmap() { *this = bmp; }
	CBitmap(CBitmap&& bmp) noexcept : CBitmap() { *this = std::move(bmp); }
	CBitmap& operator=(const CBitmap& bmp);
	CBitmap& operator=(CBitmap&& bmp) noexcept;

	~CBitmap();

	CBitmap CanvasResize(const int newx, const int newy, const bool center = true) const;
	CBitmap CreateRescaled(int newx, int newy) const;

	static bool CanBeKilled();
	static void InitPool(size_t size);
	static void KillPool();

	// TODO: RHI gap - glType parameter uses GL data type enum values (GL_UNSIGNED_BYTE, GL_FLOAT, etc.)
	void Alloc(int w, int h, int c, uint32_t glType);
	void Alloc(int w, int h, int c) { Alloc(w, h, c, 0x1401/*GL_UNSIGNED_BYTE*/); }
	void Alloc(int w, int h) { Alloc(w, h, channels); }
	void AllocDummy(const SColor fill = SColor(255, 0, 0, 255));

	int32_t GetReqNumLevels() const;

	int32_t GetIntFmt() const;
	int32_t GetExtFmt() const { return GetExtFmt(channels); }
	static int32_t GetExtFmt(uint32_t ch);
	static int32_t ExtFmtToChannels(int32_t extFmt);
	static uint32_t GetDataTypeSize(uint32_t glType);
	uint32_t GetDataTypeSize() const { return GetDataTypeSize(dataType); }

	bool CondReinterpret(int w, int h, int c, uint32_t dt);

	/// Load data from a file on the VFS
	bool Load(std::string const& filename, float defaultAlpha = 1.0f, uint32_t reqChannel = 4, uint32_t reqDataType = 0x1401/*GL_UNSIGNED_BYTE*/, bool forceReplaceAlpha = false);
	/// Load data from a gray-scale file on the VFS
	bool LoadGrayscale(std::string const& filename);

	bool Save(const std::string& filename, bool dontSaveAlpha, bool logged = false, uint32_t quality = 80) const;
	bool SaveGrayScale(const std::string& filename) const;
	bool SaveFloat(const std::string& filename) const;

	bool Empty() const { return (memIdx == size_t(-1)); } // implies size=0

	uint32_t CreateTexture(const GL::TextureCreationParams& tcp = GL::TextureCreationParams{}) const;
	uint32_t CreateMipMapTexture(float aniso = 0.0f, float lodBias = 0.0f, int32_t reqNumLevels = 0, uint32_t texID = 0) const;
	uint32_t CreateDDSTexture(const GL::TextureCreationParams& tcp = GL::TextureCreationParams{}) const;

	/// Create an RHI texture with ownership - preferred for new code
	std::unique_ptr<RHI::IRHITexture> CreateTextureRHI(float aniso = 0.0f, float lodBias = 0.0f) const;
	/// Create an RHI texture from compressed DDS data
	std::unique_ptr<RHI::IRHITexture> CreateDDSTextureRHI() const;

	void CreateAlpha(uint8_t red, uint8_t green, uint8_t blue);
	void ReplaceAlpha(float a = 1.0f);
	void SetTransparent(const SColor& c, const SColor trans = SColor(0, 0, 0, 0));

	void Renormalize(const float3& newCol);
	void Blur(int iterations = 1, float weight = 1.0f, int x = 0, int y = 0, int width = 0, int height = 0);
	void Fill(const SColor& c);

	void CopySubImage(const CBitmap& src, int x, int y);

	void ReverseYAxis();
	void InvertColors();
	void InvertAlpha();
	void MakeGrayScale();
	void Tint(const float tint[3]);

	/**
	 * Allocates a new SDL_Surface, and feeds it with the data of this bitmap.
	 * Note:
	 * - You have to free the surface with SDL_FreeSurface(surface)
	 *   if you do not need it anymore!
	 */
	SDL_Surface* CreateSDLSurface();

	const uint8_t* GetRawMem() const;
	      uint8_t* GetRawMem()      ;
	std::span<const uint8_t> GetSpan() const;

	size_t GetMemSize() const { return (xsize * ysize * channels * GetDataTypeSize()); }

private:
	// managed by pool
	size_t memIdx = size_t(-1);

public:
	int32_t xsize = 0;
	int32_t ysize = 0;
	int32_t channels = 4;
	uint32_t dataType = 0;

	// GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP, ...
	// not set to anything until Load is called
	// TODO: RHI gap - textype uses GL enum values as data; needs RHI::TextureType mapping
	int32_t textype = 0;
	#ifndef HEADLESS
	nv_dds::CDDSImage ddsimage;
	#endif

	bool compressed = false;
};

#endif // _BITMAP_H
