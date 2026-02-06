/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef GEOMETRYBUFFER_H
#define GEOMETRYBUFFER_H

#include "Rendering/GL/FBO.h"
#include "Rendering/RHI/RHIFramebuffer.h"
#include "Rendering/RHI/RHITexture.h"
#include "Rendering/RHI/RHITypes.h"
#include "System/type2.h"
#include <array>
#include <memory>

namespace GL {
	/**
	 * RHI Migration Status: PARTIALLY MIGRATED
	 *
	 * Completed:
	 *   - Texture storage: GLuint array -> std::array<std::unique_ptr<RHI::IRHITexture>>
	 *   - Texture creation: glGenTextures/glTexImage2D -> device->CreateTexture()
	 *   - MSAA textures: glTexImage2DMultisample -> CreateTexture(sampleCount)
	 *
	 * Remaining:
	 *   - FBO buffer still uses raw GL (not IRHIFramebuffer) for attachment
	 *   - glDrawBuffers still raw GL (IRHIFramebuffer::SetDrawBuffers exists but not used)
	 *   - glClear/glClearColor -> needs IRHIContext migration
	 *   - glViewport -> needs IRHIContext migration
	 *   - DrawDebug uses legacy immediate mode (glBegin/glEnd)
	 *   - GL_DEPTH_TEXTURE_MODE has no RHI equivalent
	 */
	struct GeometryBuffer {
	public:
		enum {
			ATTACHMENT_NORMTEX = 0, // shading (not geometric) normals
			ATTACHMENT_DIFFTEX = 1, // diffuse texture fragments
			ATTACHMENT_SPECTEX = 2, // specular texture fragments
			ATTACHMENT_EMITTEX = 3, // emissive texture fragments
			ATTACHMENT_MISCTEX = 4, // custom data for LuaObjectRendering shaders
			ATTACHMENT_ZVALTEX = 5, // fragment depth-values (must be last)
			ATTACHMENT_COUNT   = 6,
		};

		GeometryBuffer(const char* id): name(id) { Init(true); }
		~GeometryBuffer() { Kill(true); }

		void Init(bool ctor);
		void Kill(bool dtor);
		void Clear() const;

		void DetachTextures(const bool init);
		void DrawDebug(const unsigned int texID, const float2 texMins, const float2 texMaxs) const;
		void DrawDebug(const unsigned int texID) const { DrawDebug(texID, float2(0.0f, 0.0f), float2(1.0f, 1.0f)); }

		static void LoadViewport();

		bool HasAttachments() const { return (bufferTextures[0] != nullptr); }
		bool Valid() const { return (buffer.IsValid()); }
		bool Create(const int2 size);
		bool Update(const bool init);

		GLuint GetTextureTarget() const { return (msaa? GL_TEXTURE_2D_MULTISAMPLE: GL_TEXTURE_2D); }
		GLuint GetBufferTexture(unsigned int idx) const { return bufferTextures[idx] ? bufferTextures[idx]->GetNativeHandle() : 0; }
		GLuint GetBufferAttachment(unsigned int idx) const { return bufferAttachments[idx]; }

		/// Get the RHI texture object for the given attachment (for RHI consumers).
		RHI::IRHITexture* GetRHITexture(unsigned int idx) const { return bufferTextures[idx].get(); }

		/// Get the RHI texture type for this G-buffer's textures.
		RHI::TextureType GetRHITextureType() const {
			return msaa ? RHI::TextureType::Texture2DMS : RHI::TextureType::Texture2D;
		}
		/// Get the RHI texture format for the given attachment index.
		RHI::TextureFormat GetRHIAttachmentFormat(unsigned int idx) const {
			return (idx == ATTACHMENT_ZVALTEX)
				? RHI::TextureFormat::Depth32F
				: RHI::TextureFormat::RGBA8;
		}

		const FBO& GetObject() const { return buffer; }
		      FBO& GetObject()       { return buffer; }

		void Bind() { assert(!dead && !bound); buffer.Bind(); bound = true; }
		void UnBind() { assert(!dead && bound); buffer.Unbind(); bound = false; }

		void SetDepthRange(float nearDepth, float farDepth) const;

		int2 GetCurrSize() const { return currBufferSize; }
		int2 GetPrevSize() const { return prevBufferSize; }

		int2 GetWantedSize(bool allowed) const;

	private:
		FBO buffer;

		std::array<std::unique_ptr<RHI::IRHITexture>, ATTACHMENT_COUNT> bufferTextures;
		GLenum bufferAttachments[ATTACHMENT_COUNT];

		int2 prevBufferSize;
		int2 currBufferSize;

		const char* name = "";

		bool dead = false;
		bool bound = false;
		bool msaa = false;
	};
}

#endif
