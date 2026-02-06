/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * CNamedTextures - Global texture cache with string-based lookup.
 *
 * RHI Migration Status: COMPLETE
 * ============================
 * All texture operations migrated to RHI:
 *   - TexInfo stores std::shared_ptr<RHI::IRHITexture> for managed lifetime
 *   - id field populated from GetNativeHandle() for Lua API compatibility
 *   - Texture creation via RHI::IRHIDevice::CreateTexture()
 *   - Texture binding via texture->Bind(unit)
 *   - Texture deletion via smart pointer cleanup
 *
 * Remaining GL Dependencies (external boundaries):
 *   - Load() uses glBindTexture, glTexParameteri for extra params from CBitmap
 *     (CBitmap::CreateTexture returns raw GLuint, not yet fully migrated)
 *   - Update() uses glPushAttrib/glPopAttrib (GL_TEXTURE_BIT) for state save/restore
 *   - Bind(), GetInfo() check GL_LIST_INDEX for display list compilation
 *     (deprecated GL feature, kept for compatibility)
 *   - TexInfo.texType stores GL texture target enums (for CBitmap integration)
 */

#ifndef NAMED_TEXTURES_H
#define NAMED_TEXTURES_H

#include <cstdint>
#include <string>
#include <memory>

namespace RHI { class IRHITexture; }

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

		// RHI texture object (owns the texture lifetime)
		std::shared_ptr<RHI::IRHITexture> rhiTexture;

		// Native handle for Lua API compatibility (populated from rhiTexture->GetNativeHandle())
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
