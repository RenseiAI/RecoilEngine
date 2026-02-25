/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#ifndef LUA_TEXTURES_H
#define LUA_TEXTURES_H

/**
 * Lua Texture Container
 *
 * Manages textures created by Lua scripts via gl.CreateTexture().
 * Textures are stored with their GL parameters (filter, wrap, format).
 *
 * RHI Migration Notes:
 * - The Texture struct stores GL enum values for target, format, etc.
 * - Use LuaGLConstMappings to convert to RHI types when needed
 * - The embedded FBO (tex.fbo) is legacy - prefer explicit LuaFBOs
 * - On Metal, parallel RHI vectors (rhiTexVec, rhiFBOVec) store RHI
 *   resources indexed identically to textureVec.
 *
 * RHI Migration Status: Phase 17.2 — Metal texture creation/bind/free
 * fully routed through RHI. GL path unchanged.
 */

#include <memory>
#include <string>
#include <vector>

#include "Rendering/GL/myGL.h"
#include "Rendering/RHI/RHITexture.h"
#include "Rendering/RHI/RHIFramebuffer.h"


class LuaTextures {
public:
	static constexpr char prefix = '!';

	~LuaTextures();
	LuaTextures() {
		textureVec.reserve(128);
		textureMap.reserve(128);
		rhiTexVec.reserve(128);
		rhiFBOVec.reserve(128);
		lastCode = 0;
	}

	void Clear() {
		textureVec.clear();
		textureMap.clear();
		freeIndices.clear();
		rhiTexVec.clear();
		rhiFBOVec.clear();
	}

	struct Texture {
		GLuint id = 0;

		// FIXME: obsolete, use raw FBO's
		GLuint fbo = 0;
		GLuint fboDepth = 0;

		GLenum target = GL_TEXTURE_2D;
		GLenum format = GL_RGBA8;

		GLsizei xsize   = 0;
		GLsizei ysize   = 0;
		GLsizei zsize   = 0;
		GLsizei samples = 0;

		GLint border = 0;

		GLenum min_filter = GL_LINEAR;
		GLenum mag_filter = GL_LINEAR;

		GLenum wrap_s = GL_REPEAT;
		GLenum wrap_t = GL_REPEAT;
		GLenum wrap_r = GL_REPEAT;

		GLenum cmpFunc = GL_NONE;

		GLfloat lodBias = 0.0f;

		GLfloat aniso = 0.0f;
	};

	std::string Create(const Texture& tex);
	bool Bind(const std::string& name) const;
	bool Free(const std::string& name);
	bool FreeFBO(const std::string& name);
	void FreeAll();
	void ChangeParams(const Texture& tex) const;
	void ApplyParams(const Texture& tex) const;

	size_t GetIdx(const std::string& name) const;

	const Texture* GetInfo(size_t texIdx) const { return ((texIdx < textureVec.size())? &textureVec[texIdx]: nullptr); }
	      Texture* GetInfo(size_t texIdx)       { return ((texIdx < textureVec.size())? &textureVec[texIdx]: nullptr); }

	const Texture* GetInfo(const std::string& name) const { return (GetInfo(GetIdx(name))); }
	      Texture* GetInfo(const std::string& name)       { return (GetInfo(GetIdx(name))); }

	// RHI accessors — return nullptr on GL path or if index is out of range
	RHI::IRHITexture* GetRHITexture(size_t idx) const;
	RHI::IRHITexture* GetRHITexture(const std::string& name) const;

private:
	int lastCode;

	// maps names to textureVec indices
	spring::unsynced_map<std::string, size_t> textureMap;

	std::vector<Texture> textureVec;
	std::vector<size_t> freeIndices;

	// RHI parallel storage — indexed identically to textureVec.
	// GL path stores nullptr entries to keep indices in sync.
	std::vector<std::unique_ptr<RHI::IRHITexture>>     rhiTexVec;
	std::vector<std::unique_ptr<RHI::IRHIFramebuffer>> rhiFBOVec;
};


#endif /* LUA_TEXTURES_H */
