/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "InfoTexture.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHITexture.h"


CInfoTexture::CInfoTexture()
	: texture{}
	, texSize(0, 0)
{}

CInfoTexture::CInfoTexture(const std::string& _name, GL::Texture2D&& _texture, int2 _texSize)
	: texture(std::move(_texture))
	, name(_name)
	, texSize(_texSize)
{}

RHI::IRHITexture* CInfoTexture::GetRHITexture()
{
	const GLuint texId = GetTexture();
	if (texId == 0)
		return nullptr;

	// Invalidate cache if the underlying GL texture ID changed
	if (cachedTexId != texId) {
		rhiTexture.reset();
		cachedTexId = texId;
	}

	if (!rhiTexture) {
		rhiTexture = RHI::GetDevice()->WrapExistingTexture(
			texId,
			RHI::TextureType::Texture2D,
			RHI::TextureFormat::RGBA8,
			texSize.x, texSize.y
		);
	}

	return rhiTexture.get();
}
CInfoTexture::~CInfoTexture() = default;
