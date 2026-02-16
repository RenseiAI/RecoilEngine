// RHI migration status: COMPLETE
// - Cubemap creation migrated to RHI::IRHITexture
// - Texture binding migrated to RHI context
// - Matrix stack operations kept as GL FFP (no RHI equivalent)

#pragma once

#include <cstdint>
#include <memory>
#include "Rendering/GL/VAO.h"
#include "System/type2.h"

namespace RHI {
	class IRHITexture;
}

namespace Shader {
	struct IProgramObject;
}

class CCamera;

class DebugCubeMapTexture {
public:
	DebugCubeMapTexture();
	~DebugCubeMapTexture();

	RHI::IRHITexture* GetTexture() const { return cubeTexture.get(); }
	uint32_t GetId() const;
	int2 GetDimensions() const { return dims; }

	void Draw(uint32_t face = 0) const;

	static DebugCubeMapTexture& GetInstance();
private:
	std::unique_ptr<RHI::IRHITexture> cubeTexture;
	int2 dims;
	VAO vao; //even though VAO has no attached VBOs, it's still needed to perform rendering
	Shader::IProgramObject* shader;
};

#define debugCubeMapTexture (DebugCubeMapTexture::GetInstance())