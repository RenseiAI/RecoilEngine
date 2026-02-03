/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef GEOMETRYBUFFER_H
#define GEOMETRYBUFFER_H

#include "Rendering/GL/FBO.h"
#include "Rendering/RHI/RHIFramebuffer.h"
#include "Rendering/RHI/RHITexture.h"
#include "Rendering/RHI/RHITypes.h"
#include "System/type2.h"

namespace GL {
	/**
	 * RHI Migration Note:
	 *   GeometryBuffer manages a G-buffer (deferred shading MRT).
	 *   Migration path:
	 *   - FBO buffer -> IRHIFramebuffer with multiple color attachments
	 *   - GLuint bufferTextureIDs -> IRHITexture* array
	 *   - glGenTextures/glTexImage2D -> device->CreateTexture()
	 *   - glClear/glClearColor -> IRHIContext::ClearColor/ClearDepth
	 *   - glViewport -> IRHIContext::SetViewport
	 *   - DrawDebug uses legacy immediate mode (glBegin/glEnd) -> needs RenderBuffer
	 *
	 *   RHI Gaps:
	 *   - IRHITexture has no glTexImage2DMultisample equivalent (MSAA textures)
	 *   - IRHIFramebuffer has no glDrawBuffers equivalent (MRT output selection)
	 *   - No RHI equivalent for GL_DEPTH_TEXTURE_MODE
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

		bool HasAttachments() const { return (bufferTextureIDs[0] != 0); }
		bool Valid() const { return (buffer.IsValid()); }
		bool Create(const int2 size);
		bool Update(const bool init);

		GLuint GetTextureTarget() const { return (msaa? GL_TEXTURE_2D_MULTISAMPLE: GL_TEXTURE_2D); }
		GLuint GetBufferTexture(unsigned int idx) const { return bufferTextureIDs[idx]; }
		GLuint GetBufferAttachment(unsigned int idx) const { return bufferAttachments[idx]; }

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

		GLuint bufferTextureIDs[ATTACHMENT_COUNT];
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
