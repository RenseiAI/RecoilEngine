/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * CNamedTextures - Global texture cache with string-based lookup.
 *
 * RHI Migration Status: PARTIAL
 * ============================
 * RHI texture creation already used in GenTex() and GenLoadTex():
 *   - RHI::GetDevice()->CreateTexture() for placeholder textures
 *   - GetNativeHandle() extracts raw handle, then releases ownership
 *
 * Remaining GL Dependencies:
 *   - Kill(), EraseTex() use glDeleteTextures for cleanup
 *   - Bind() uses glBindTexture directly
 *   - Load() uses glBindTexture, glTexParameteri for extra params
 *   - Update() uses glPushAttrib/glPopAttrib (GL_TEXTURE_BIT)
 *   - Bind(), GetInfo() check GL_LIST_INDEX for display list compilation
 *   - TexInfo.texType stores GL texture target enums
 *
 * Migration Path:
 * 1. Store std::unique_ptr<RHI::IRHITexture> instead of raw id in TexInfo
 * 2. Replace glDeleteTextures with unique_ptr destruction
 * 3. Replace glBindTexture with texture->Bind()
 * 4. Remove display list compilation checks (deprecated feature)
 * 5. Map texType from GL enums to RHI::TextureType
 */

#ifndef NAMED_TEXTURES_H
#define NAMED_TEXTURES_H

#include <cstdint>
#include <string>

namespace CNamedTextures {
	void Init();
	void Kill(bool shutdown = false);
	void Reload();

	static bool Load(const std::string& texName, uint32_t texID, bool genInsert = true);

	/**
	 * Reload textures we could not load because Bind() was called
	 * when compiling a DList.
	 * Otherwise, it would re-upload the texture-data on each call
	 * of the DList, so we delay it and load them here.
	 */
	void Update();

	bool Bind(const std::string& texName);
	bool Free(const std::string& texName);

	struct TexInfo {
		TexInfo()
			: id(0), xsize(-1), ysize(-1), texType(0), alpha(false), persist(false) {}
		uint32_t id;
		int xsize;
		int ysize;
		uint32_t texType;
		bool alpha;
		bool persist;
	};

	size_t GetInfoIndex(const std::string& texName);

	const TexInfo* GetInfo(const std::string& texName, bool forceLoad = false, bool persist = false, bool secondaryGLContext = false);
	const TexInfo* GetInfo(size_t texIdx);
}

#endif /* NAMED_TEXTURES_H */
