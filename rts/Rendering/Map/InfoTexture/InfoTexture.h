/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/GL/myGL.h"
#include "Rendering/Textures/Texture.hpp"
#include "System/type2.h"
#include <memory>
#include <string>

namespace RHI { class IRHITexture; }

class CInfoTexture
{
public:
	CInfoTexture();
	CInfoTexture(const std::string& name, GL::Texture2D&& texture, int2 texSize);
	virtual ~CInfoTexture();

public:
	virtual GLuint GetTexture() { return texture.GetId(); }
	/// Returns a non-owning RHI texture wrapper for this info texture.
	/// Lazy-created and cached; auto-invalidates if the GL texture ID changes.
	RHI::IRHITexture* GetRHITexture();
	int2 GetTexSize()     const { return texSize; }
	const std::string& GetName() const { return name; }
protected:
	friend class IInfoTextureHandler;

	GL::Texture2D texture;
	std::string name;
	int2 texSize;

	mutable std::unique_ptr<RHI::IRHITexture> rhiTexture;
	mutable GLuint cachedTexId = 0;
};

class CDummyInfoTexture: public CInfoTexture {
public:
	CDummyInfoTexture() : CInfoTexture("dummy", {}, int2(0, 0)) {}
};