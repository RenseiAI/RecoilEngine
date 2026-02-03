/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef FBO_H
#define FBO_H

#include <vector>
#include <array>

#include "myGL.h"
#include "System/UnorderedMap.hpp"
#include "Rendering/RHI/RHIFramebuffer.h"
#include "Rendering/RHI/RHITypes.h"

// TODO: add multisample buffers

/**
 * @brief FBO
 *
 * Framebuffer Object class (EXT_framebuffer_object).
 *
 * RHI Migration Note:
 *   The RHI OpenGL backend (GLFramebuffer) wraps this class. Higher-level
 *   code should migrate to IRHIFramebuffer; use the helpers below to convert
 *   GL enums to RHI equivalents during the transition.
 */
class FBO
{
public:
	/**
	 * @brief IsSupported
	 *
	 * if FrameBuffers are supported by the current platform
	 */
	static bool IsSupported();
	static bool IsReady();
	static GLint GetCurrentBoundFBO();

	FBO(         ) { Init(false); }
	explicit FBO(bool noop) { Init( noop); }
	~FBO() { Kill(); }

	void Init(bool noop);
	void Kill();

	uint32_t GetId() const { return fboId; }

	/**
	 * @brief fboId
	 *
	 * GLuint pointing to the current framebuffer
	 */
	GLuint fboId = 0;

	/**
	 * @brief reloadOnAltTab
	 *
	 * bool save all attachments in system RAM and reloaded them on OpenGL-Context lost (alt-tab) (default: false)
	 */
	bool reloadOnAltTab = false;

	/**
	 * @brief check FBO status
	 */
	bool CheckStatus(const char* name);

	/**
	 * @brief get FBO status
	 */
	GLenum GetStatus();

	/**
	 * @return GL_MAX_SAMPLES or 0 if multi-sampling not supported
	 */
	static GLsizei GetMaxSamples();

	/**
	 * @brief IsValid
	 * @return whether a valid framebuffer exists
	 */
	bool IsValid() const;


	void AttachTextures(const GLuint* ids, const GLenum* attachments, const GLenum texTarget, const unsigned int texCount, const int mipLevel = 0, const int zSlice = 0) {
		for (unsigned int i = 0; i < texCount; i++) {
			AttachTexture(ids[i], texTarget, attachments[i], mipLevel, zSlice);
		}
	}

	/**
	 * @brief AttachTexture
	 * @param texTarget texture target (GL_TEXTURE_2D etc.)
	 * @param texId texture to attach
	 * @param attachment (GL_COLOR_ATTACHMENT0_EXT etc.)
	 * @param mipLevel miplevel to attach
	 * @param zSlice z offset (3d textures only)
	 */
	void AttachTexture(const GLuint texId, const GLenum texTarget = GL_TEXTURE_2D, const GLenum attachment = GL_COLOR_ATTACHMENT0_EXT, const int mipLevel = 0, const int zSlice = 0);

	void AttachTextureLayer(const GLuint texId, const GLenum attachment = GL_COLOR_ATTACHMENT0_EXT, const int mipLevel = 0, const int layer = 0);

	/**
	 * @brief AttachRenderBuffer
	 * @param rboId RenderBuffer to attach
	 * @param attachment
	 */
	void AttachRenderBuffer(const GLuint rboId, const GLenum attachment = GL_COLOR_ATTACHMENT0_EXT);

	/**
	 * @brief Creates a RenderBufferObject and attaches it to the FBO (it is also auto destructed)
	 * @param attachment
	 * @param format
	 * @param width
	 * @param height
	 */
	void CreateRenderBuffer(const GLenum attachment, const GLenum format, const GLsizei width, const GLsizei height);

	/**
	 * @brief Creates a multisampled RenderBufferObject and attaches it to the FBO (it is also auto destructed)
	 * @param attachment
	 * @param format
	 * @param width
	 * @param height
	 * @param samples
	 */
	void CreateRenderBufferMultisample(const GLenum attachment, const GLenum format, const GLsizei width, const GLsizei height, GLsizei samples);

	/**
	 * @brief Detach
	 * @param attachment
	 */
	void Detach(const GLenum attachment);

	/**
	 * @brief DetachAll
	 */
	void DetachAll();

	/**
	 * @brief Bind
	 */
	void Bind();

	/**
	 * @brief Unbind
	 */
	static void Unbind();

	static bool Blit(
		int32_t fromID,
		int32_t toID,
		const std::array<int, 4>& srcRect,
		const std::array<int, 4>& dstRect,
		uint32_t mask = GL_DEPTH_BUFFER_BIT,
		uint32_t filter = GL_NEAREST
	);

	/**
	 * @brief GLContextLost (post atl-tab)
	 */
	static void GLContextLost();

	/**
	 * @brief GLContextReinit (pre atl-tab)
	 */
	static void GLContextReinit();

	/// Convert a GL attachment enum to the RHI color index (0-7), or -1 for depth/stencil.
	static int AttachmentToRHIColorIndex(GLenum attachment) {
		if (attachment >= GL_COLOR_ATTACHMENT0_EXT && attachment <= GL_COLOR_ATTACHMENT7_EXT)
			return static_cast<int>(attachment - GL_COLOR_ATTACHMENT0_EXT);
		return -1; // depth, stencil, or unknown
	}

	/// Convert a GL internal format to the corresponding RHI::TextureFormat.
	static RHI::TextureFormat FormatToRHI(GLenum format) {
		switch (format) {
		case GL_RGBA8:                 return RHI::TextureFormat::RGBA8;
		case GL_RGB8:                  return RHI::TextureFormat::RGB8;
		case GL_RG8:                   return RHI::TextureFormat::RG8;
		case GL_R8:                    return RHI::TextureFormat::R8;
		case GL_RGBA16F:               return RHI::TextureFormat::RGBA16F;
		case GL_RGB16F:                return RHI::TextureFormat::RGB16F;
		case GL_RG16F:                 return RHI::TextureFormat::RG16F;
		case GL_R16F:                  return RHI::TextureFormat::R16F;
		case GL_RGBA32F:               return RHI::TextureFormat::RGBA32F;
		case GL_RGB32F:                return RHI::TextureFormat::RGB32F;
		case GL_RG32F:                 return RHI::TextureFormat::RG32F;
		case GL_R32F:                  return RHI::TextureFormat::R32F;
		case GL_DEPTH_COMPONENT16:     return RHI::TextureFormat::Depth16;
		case GL_DEPTH_COMPONENT24:     return RHI::TextureFormat::Depth24;
		case GL_DEPTH_COMPONENT32F:    return RHI::TextureFormat::Depth32F;
		case GL_DEPTH24_STENCIL8:      return RHI::TextureFormat::Depth24Stencil8;
		case GL_DEPTH32F_STENCIL8:     return RHI::TextureFormat::Depth32FStencil8;
		case GL_SRGB8_ALPHA8:          return RHI::TextureFormat::SRGB8Alpha8;
		default:                       return RHI::TextureFormat::RGBA8;
		}
	}


private:
	bool valid = false;

	/**
	 * @brief rbos
	 *
	 * List with all Renderbuffer Objects that should be destructed with the FBO
	 */
	std::vector<GLuint> rboIDs;


	struct TexData {
	public:
		TexData() { id = 0; }
		TexData(const TexData& td) { assert(td.id == 0); } // = delete;
		TexData(TexData&& td) {
			id = td.id;

			xsize = td.xsize;
			ysize = td.ysize;
			zsize = td.zsize;

			target = td.target;
			format = td.format;
			type   = td.type;

			pixels = std::move(td.pixels);
		}

	public:
		GLuint id;
		GLsizei xsize, ysize, zsize;
		GLenum target, format, type;
		std::vector<unsigned char> pixels;
	};

	static std::vector<FBO*> activeFBOs;
	static spring::unordered_map<GLuint, TexData> fboTexData;

	static GLint maxAttachments;
	static GLsizei maxSamples;

	/**
	 * @brief DownloadAttachment
	 *
	 * copies the attachment content to sysram
	 */
	static void DownloadAttachment(const GLenum attachment);

	/**
	 * @brief GetTextureTargetByID
	 *
	 * detects the textureTarget just by the textureName/ID
	 */
	static GLenum GetTextureTargetByID(const GLuint id, const unsigned int i = 0);
};

#endif /* FBO_H */
