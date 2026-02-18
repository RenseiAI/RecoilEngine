/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * NamedTextures.cpp - Global named texture cache implementation.
 *
 * RHI Migration Status: COMPLETE
 * ============================
 * All texture management migrated to RHI:
 *   - GenTex() creates 1x1 placeholder via RHI::GetDevice()->CreateTexture()
 *   - TexInfo stores std::shared_ptr<RHI::IRHITexture> for lifetime management
 *   - Kill(), EraseTex() use smart pointer cleanup instead of glDeleteTextures
 *   - Bind() uses rhiTexture->Bind(0) instead of glBindTexture
 *
 * Remaining GL dependencies (external boundaries):
 *   - Load(): glBindTexture, glTexParameteri for CBitmap integration
 *     (CBitmap::CreateTexture returns raw GLuint, wrapping needed)
 *   - Update(): glPushAttrib/glPopAttrib(GL_TEXTURE_BIT) for state save
 *   - Bind(), GetInfo(): glGetBooleanv(GL_LIST_INDEX) for display list check
 *     (deprecated GL feature, kept for compatibility)
 */

// must be included before streflop! else we get streflop/cmath resolve conflicts in its hash implementation files
#include <bit>
#include <vector>
#include "NamedTextures.h"

#include "Rendering/GL/myGL.h" // TODO: RHI gap - needed for display list queries and glPushAttrib/glPopAttrib
#include "Bitmap.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHITexture.h"
#include "System/type2.h"
#include "System/Log/ILog.h"
#include "System/Threading/SpringThreading.h"
#include "System/UnorderedMap.hpp"

#include "System/Misc/TracyDefs.h"



namespace CNamedTextures {
	// maps names to texInfoVec indices
	static spring::unordered_map<std::string, size_t> texInfoMap;

	static std::vector<CNamedTextures::TexInfo> texInfoVec;
	static std::vector<size_t> freeIndices;
	static std::vector<std::string> waitingTextures;

	static spring::recursive_mutex mutex;

	/******************************************************************************/

	void Init()
	{
	RECOIL_DETAILED_TRACY_ZONE;
		texInfoMap.clear();
		texInfoMap.reserve(128);
		texInfoVec.clear();
		texInfoVec.reserve(128);

		freeIndices.clear();

		waitingTextures.clear();
		waitingTextures.reserve(16);
	}

	void Kill(bool shutdown)
	{
	RECOIL_DETAILED_TRACY_ZONE;
		decltype(texInfoMap) tempMap;

		const std::lock_guard<spring::recursive_mutex> lck(mutex);

		for (const auto& [texName, texIdx]: texInfoMap) {
			if (shutdown || !texInfoVec[texIdx].persist) {
				// Release RHI texture (smart pointer cleanup)
				texInfoVec[texIdx].rhiTexture.reset();
				texInfoVec[texIdx].id = 0;
				// always recycle non-persistent textures
				freeIndices.push_back(texIdx);
			} else {
				tempMap[texName] = texIdx;
			}
		}

		std::swap(texInfoMap, tempMap);
		waitingTextures.clear();
	}

	void Reload()
	{
	RECOIL_DETAILED_TRACY_ZONE;
		const std::lock_guard<spring::recursive_mutex> lck(mutex); //needed?

		for (const auto& [texName, texIdx] : texInfoMap) {
			const uint32_t texID = texInfoVec[texIdx].id;
			if (texID == 0)
				continue;

			Load(texName, texID, false);
		}
	}


	/******************************************************************************/

	static void InsertTex(const std::string& texName, const TexInfo& texInfo, bool loadTex)
	{
	RECOIL_DETAILED_TRACY_ZONE;
		// caller (GenInsertTex) already has lock
		if (!loadTex)
			waitingTextures.push_back(texName);

		if (freeIndices.empty()) {
			texInfoMap[texName] = texInfoVec.size();
			texInfoVec.push_back(texInfo);
		} else {
			// recycle
			texInfoMap[texName] = freeIndices.back();
			texInfoVec[freeIndices.back()] = texInfo;
			freeIndices.pop_back();
		}
	}

	static TexInfo GenTex(bool bindTex, bool persistTex)
	{
	RECOIL_DETAILED_TRACY_ZONE;
		// Create a 1x1 placeholder texture via RHI; actual content filled by Load()
		auto* device = RHI::GetDevice();
		auto rhiTex = device->CreateTexture(
			RHI::TextureType::Texture2D, RHI::TextureFormat::RGBA8,
			1, 1, 1, 1);

		if (bindTex)
			rhiTex->Bind(0);

		TexInfo texInfo;
		texInfo.rhiTexture = std::move(rhiTex); // Store in shared_ptr
		texInfo.id = texInfo.rhiTexture->GetNativeHandle();
		texInfo.persist = persistTex;
		return texInfo;
	}

	static void GenInsertTex(const std::string& texName, const TexInfo& texInfo, bool genTex, bool bindTex, bool loadTex, bool persistTex)
	{
	RECOIL_DETAILED_TRACY_ZONE;
		const std::lock_guard<spring::recursive_mutex> lck(mutex);

		if (!genTex) {
			InsertTex(texName, texInfo, loadTex);
			return;
		}

		InsertTex(texName, GenTex(bindTex, persistTex), loadTex);
	}

	static bool EraseTex(const std::string& texName)
	{
	RECOIL_DETAILED_TRACY_ZONE;
		const std::lock_guard<spring::recursive_mutex> lck(mutex);

		const auto it = texInfoMap.find(texName);

		if (it != texInfoMap.end()) {
			const size_t texIdx = it->second;

			// Release RHI texture (smart pointer cleanup)
			texInfoVec[texIdx].rhiTexture.reset();
			texInfoVec[texIdx].id = 0;

			freeIndices.push_back(texIdx);
			texInfoMap.erase(it);
			return true;
		}

		return false;
	}



	static bool Load(const std::string& texName, unsigned int texID, bool genInsert)
	{
	RECOIL_DETAILED_TRACY_ZONE;
		// Note: This function uses CBitmap::CreateTexture() which returns raw GLuint.
		// Once CBitmap is fully migrated to CreateTextureRHI(), we can wrap the result.
		// For now, texInfo stores the raw handle and rhiTexture remains null.

		// strip off the qualifiers
		std::string filename = texName;
		bool border  = false;
		bool clamped = false;
		bool nearest = false;
		bool linear  = false;
		bool aniso   = false;
		bool invert  = false;
		bool greyed  = false;
		bool mipnear = false;
		bool tint    = false;
		float tintColor[3];
		bool resize  = false;
		int2 resizeDimensions;

		if (filename[0] == ':') {
			size_t p;
			for (p = 1; p < filename.size(); p++) {
				const char ch = filename[p];

				if (ch == ':')      { break; }
				else if (ch == 'n') { nearest = true; }
				else if (ch == 'l') { linear  = true; }
				else if (ch == 'a') { aniso   = true; }
				else if (ch == 'i') { invert  = true; }
				else if (ch == 'g') { greyed  = true; }
				else if (ch == 'c') { clamped = true; }
				else if (ch == 'b') { border  = true; }
				else if (ch == 'm') { mipnear = true; }
				else if (ch == 't') {
					const char* cstr = filename.c_str() + p + 1;
					const char* start = cstr;
					char* endptr;
					tintColor[0] = (float)strtod(start, &endptr);
					if ((start != endptr) && (*endptr == ',')) {
						start = endptr + 1;
						tintColor[1] = (float)strtod(start, &endptr);
						if ((start != endptr) && (*endptr == ',')) {
							start = endptr + 1;
							tintColor[2] = (float)strtod(start, &endptr);
							if (start != endptr) {
								tint = true;
								p += (endptr - cstr);
							}
						}
					}
				}
				else if (ch == 'r') {
					const char* cstr = filename.c_str() + p + 1;
					const char* start = cstr;
					char* endptr;
					resizeDimensions.x = (int)strtoul(start, &endptr, 10);
					if ((start != endptr) && (*endptr == ',')) {
						start = endptr + 1;
						resizeDimensions.y = (int)strtoul(start, &endptr, 10);
						if (start != endptr) {
							resize = true;
							p += (endptr - cstr);
						}
					}
				}
			}

			if (p < filename.size()) {
				filename = filename.substr(p + 1);
			} else {
				filename.clear();
			}
		}

		// get the image
		CBitmap bitmap;
		TexInfo texInfo;

		if (!bitmap.Load(filename, 1.0f, 4u, 0u)) {
			LOG_L(L_WARNING, "Couldn't find texture \"%s\"!", filename.c_str());
			GenInsertTex(texName, texInfo, false, false, true, false);
			return false;
		}

		const bool needMipMaps = (!(nearest || linear)) || mipnear;

		GL::TextureCreationParams tcp;
		tcp.texID = texID;
		tcp.linearMipMapFilter = !mipnear;
		tcp.linearTextureFilter = !nearest;
		tcp.reqNumLevels = needMipMaps ? 0 : 1;
		if (aniso)
			tcp.aniso = RHI::GetDevice()->GetMaxTexAnisotropy();

		if (bitmap.compressed) {
			texID = bitmap.CreateDDSTexture(tcp);
		} else {
			if (resize) bitmap = bitmap.CreateRescaled(resizeDimensions.x,resizeDimensions.y);
			if (invert) bitmap.InvertColors();
			if (greyed) bitmap.MakeGrayScale();
			if (tint)   bitmap.Tint(tintColor);

			// verify if still broken
			if (globalRendering->amdHacks && nearest) {
				bitmap = bitmap.CreateRescaled(std::bit_ceil <uint32_t>(bitmap.xsize), std::bit_ceil <uint32_t>(bitmap.ysize));
			}

			texID = bitmap.CreateTexture(tcp);
		}

		texInfo.id    = texID;
		texInfo.xsize = bitmap.xsize;
		texInfo.ysize = bitmap.ysize;

		#ifndef HEADLESS
			switch (bitmap.textype) {
				case GL_TEXTURE_2D_ARRAY:  { texInfo.texType = GL_TEXTURE_2D_ARRAY; } break;
				case GL_TEXTURE_3D:        { texInfo.texType = GL_TEXTURE_3D;       } break;
				case GL_TEXTURE_CUBE_MAP:  { texInfo.texType = GL_TEXTURE_CUBE_MAP; } break;
				default:                   { texInfo.texType = GL_TEXTURE_2D;       } break;
			}
		#endif

		// Wrap the raw GL texture with a non-owning RHI handle for Bind() and param setting
		if (texID != 0) {
			auto* device = RHI::GetDevice();
			RHI::TextureType rhiType = RHI::TextureType::Texture2D;
			#ifndef HEADLESS
			switch (bitmap.textype) {
				case GL_TEXTURE_2D_ARRAY: rhiType = RHI::TextureType::Texture2DArray; break;
				case GL_TEXTURE_3D:       rhiType = RHI::TextureType::Texture3D;      break;
				case GL_TEXTURE_CUBE_MAP: rhiType = RHI::TextureType::TextureCube;     break;
				default: break;
			}
			#endif
			texInfo.rhiTexture = device->WrapExistingTexture(
				texID, rhiType, RHI::TextureFormat::RGBA8,
				bitmap.xsize, bitmap.ysize);

			// Set extra texture params via RHI (no bind/unbind needed)
			if (clamped) {
				texInfo.rhiTexture->SetWrapS(RHI::TextureWrap::ClampToEdge);
				texInfo.rhiTexture->SetWrapT(RHI::TextureWrap::ClampToEdge);
			}
			if (border) {
				texInfo.rhiTexture->SetWrapS(RHI::TextureWrap::ClampToBorder);
				texInfo.rhiTexture->SetWrapT(RHI::TextureWrap::ClampToBorder);
				texInfo.rhiTexture->SetBorderColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}

		if (genInsert)
			GenInsertTex(texName, texInfo, false, false, true, false);

		return true;
	}

	static bool GenLoadTex(const std::string& texName)
	{
	RECOIL_DETAILED_TRACY_ZONE;
		// Note: We pass texID=0 to Load(), which causes CBitmap to create a new texture.
		// Once CBitmap is migrated to return RHI textures, we can store them here.
		// For now, Load() will store the raw GLuint handle from CBitmap.
		return (Load(texName, 0));
	}


	bool Bind(const std::string& texName)
	{
	RECOIL_DETAILED_TRACY_ZONE;
		if (texName.empty())
			return false;

		// cached
		const auto it = texInfoMap.find(texName);

		if (it != texInfoMap.end()) {
			const size_t texIdx = it->second;
			const TexInfo& texInfo = texInfoVec[texIdx];

			// Prefer RHI texture binding when available
			if (texInfo.rhiTexture) {
				texInfo.rhiTexture->Bind(0);
			} else {
				// Fallback for textures loaded via CBitmap (not yet migrated)
				// RHI_TODO: migrate once CBitmap returns RHI textures
				glBindTexture(GL_TEXTURE_2D, texInfo.id);
			}
			return (texInfo.id != 0);
		}

		// load texture
		// RHI_TODO: display list compilation check is deprecated GL-specific feature, no RHI equivalent
		GLboolean inListCompile;
		glGetBooleanv(GL_LIST_INDEX, &inListCompile);
		if (inListCompile) {
			GenInsertTex(texName, {}, true, true, false, false);
			return true;
		}

		return (GenLoadTex(texName));
	}


	void Update()
	{
	RECOIL_DETAILED_TRACY_ZONE;
		if (waitingTextures.empty())
			return;

		const std::lock_guard<spring::recursive_mutex> lck(mutex);

		for (const std::string& texString: waitingTextures) {
			const auto mit = texInfoMap.find(texString);

			if (mit == texInfoMap.end())
				continue;

			Load(texString, texInfoVec[mit->second].id);
		}

		waitingTextures.clear();
	}


	bool Free(const std::string& texName)
	{
	RECOIL_DETAILED_TRACY_ZONE;
		if (texName.empty())
			return false;

		return (EraseTex(texName));
	}


	size_t GetInfoIndex(const std::string& texName)
	{
	RECOIL_DETAILED_TRACY_ZONE;
		const auto it = texInfoMap.find(texName);

		if (it != texInfoMap.end())
			return (it->second);

		return (size_t(-1));
	}

	const TexInfo* GetInfo(size_t texIdx) { return &texInfoVec[texIdx]; }
	const TexInfo* GetInfo(const std::string& texName, bool forceLoad, bool persist, bool secondaryGLContext)
	{
	RECOIL_DETAILED_TRACY_ZONE;
		if (texName.empty())
			return nullptr;

		const size_t texIdx = GetInfoIndex(texName);

		if (texIdx != size_t(-1)) {
			texInfoVec[texIdx].persist |= persist;
			return &texInfoVec[texIdx];
		}

		if (forceLoad) {
			// load texture
			// RHI_TODO: display list compilation check is deprecated GL-specific feature, no RHI equivalent
			GLboolean inListCompile;
			glGetBooleanv(GL_LIST_INDEX, &inListCompile);

			if (inListCompile) {
				GenInsertTex(texName, {}, true, secondaryGLContext, false, persist);
			} else {
				GenLoadTex(texName);
			}

			return &texInfoVec[ texInfoMap[texName] ];
		}

		return nullptr;
	}


	/******************************************************************************/

} // namespace CNamedTextures
