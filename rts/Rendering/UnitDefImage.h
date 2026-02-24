/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef UNIT_DEF_IMAGE
#define UNIT_DEF_IMAGE

#include <memory>

#include "System/creg/creg_cond.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/RHI/RHITexture.h"

struct UnitDefImage
{
	CR_DECLARE_STRUCT(UnitDefImage)

	UnitDefImage(): imageSizeX(-1), imageSizeY(-1), textureID(0) {
	}

	bool Free() {
		rhiTexture.reset();
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
	std::shared_ptr<RHI::IRHITexture> rhiTexture;  // RHI texture (owning on Metal)
};

#endif // UNIT_DEF_IMAGE
