/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * UnitDefImage - Stores a texture for unit definition icons.
 *
 * RHI Migration Status: NOT MIGRATED
 * ==================================
 * - Stores raw GLuint textureID
 * - Free() uses glDeleteTextures directly
 *
 * Migration Path:
 * 1. Replace uint32_t textureID with std::unique_ptr<RHI::IRHITexture>
 * 2. Free() becomes: texture.reset(); return true;
 * 3. For backward compat, add GetTextureID() returning GetNativeHandle()
 *
 * Note: This is a simple struct used for unit buildpic/icon textures.
 * The CREG serialization may need adjustment for RHI texture ownership.
 */

#ifndef UNIT_DEF_IMAGE
#define UNIT_DEF_IMAGE

#include "System/creg/creg_cond.h"
#include "Rendering/GL/myGL.h" // TODO: RHI gap - needed for glDeleteTextures

struct UnitDefImage
{
	CR_DECLARE_STRUCT(UnitDefImage)

	UnitDefImage(): imageSizeX(-1), imageSizeY(-1), textureID(0) {
	}

	bool Free() {
		if (textureID != 0) {
			glDeleteTextures(1, &textureID);
			textureID = 0;
			return true;
		}
		return false;
	}

	int imageSizeX;
	int imageSizeY;
	uint32_t textureID;
};

#endif // UNIT_DEF_IMAGE
