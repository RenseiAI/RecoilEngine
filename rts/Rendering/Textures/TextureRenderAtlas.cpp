/**
 * TextureRenderAtlas.cpp - GPU-rendered texture atlas implementation.
 *
 * RHI Migration Status: PARTIAL
 * ============================
 * Migrated:
 *   - Atlas storage uses RHI::IRHITexture (lines 391-408)
 *   - RHI::IRHIContext::SetViewport for FBO rendering (lines 437-441)
 *
 * Remaining GL (render-to-texture pipeline):
 *   - FBO attachment/rendering still uses GL (lines 418-449)
 *   - glTexParameteri for source texture sampling (lines 471-474)
 *   - glDrawBuffer/glReadBuffer for FBO attachment selection (lines 448-449)
 *   - filenameToTexID stores raw GLuint from CBitmap::CreateMipMapTexture (lines 203-206)
 *   - glDeleteTextures for intermediate texture cleanup (lines 146-151, 499-504)
 *
 * Note: Full migration blocked by FBO RHI wrapper and CBitmap RHI return types.
 */

#include "TextureRenderAtlas.h"

#include <algorithm>

#include "LegacyAtlasAlloc.h"
#include "QuadtreeAtlasAlloc.h"
#include "RowAtlasAlloc.h"
#include "MultiPageAtlasAlloc.hpp"

#include "Rendering/GlobalRendering.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/GL/myGL.h" // needed for FBO, SubState, RenderBuffers, TexBind (rendering pipeline)
#include "Rendering/GL/FBO.h"
#include "Rendering/GL/TexBind.h"
#include "Rendering/GL/SubState.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/Textures/Bitmap.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "System/Config/ConfigHandler.h"
#include "System/StringUtil.h"
#include "System/Log/ILog.h"
#include "fmt/format.h"

#include "System/Misc/TracyDefs.h"

namespace {

static RHI::TextureFormat GLInternalToRHIFormat(uint32_t glFormat) {
	switch (glFormat) {
		case 0x8058: return RHI::TextureFormat::RGBA8;   // GL_RGBA8
		case 0x8814: return RHI::TextureFormat::RGBA32F;  // GL_RGBA32F
		case 0x881A: return RHI::TextureFormat::RGBA16F;  // GL_RGBA16F
		default:     return RHI::TextureFormat::RGBA8;
	}
}

static constexpr const char* vsTRA = R"(
#version 130

in vec2 pos;
in vec2 uv;

out vec2 vUV;

void main() {
	vUV  = uv;
	gl_Position = vec4(pos, 0.0, 1.0);
}
)";

static constexpr const char* fsTRA = R"(
#version 130

uniform sampler2D tex;
uniform float lod;

in vec2 vUV;
out vec4 outColor;

void main() {
	outColor = textureLod(tex, vUV, lod);
}
)";
};

std::string CTextureRenderAtlas::UniqueSubTexture::GetName() const
{
	return fmt::format("{};{},{},{},{}", texID, subTexCoords.x1, subTexCoords.y1, subTexCoords.x2, subTexCoords.y2);
}

CTextureRenderAtlas::CTextureRenderAtlas(
	CTextureAtlas::AllocatorType allocType_,
	int atlasSizeX,
	int atlasSizeY,
	uint32_t glInternalType_,
	const std::string& atlasName_
	)
	: allocType(allocType_)
	, glInternalType(glInternalType_)
	, atlasName(atlasName_)
	, atlasFinalized(false)
	, atlasRendered(false)
{
	RECOIL_DETAILED_TRACY_ZONE;

	using MPLegacyAtlasAlloc = MultiPageAtlasAlloc<CLegacyAtlasAlloc>;
	using MPQuadtreeAtlasAlloc = MultiPageAtlasAlloc<CQuadtreeAtlasAlloc>;
	using MPRowAtlasAlloc = MultiPageAtlasAlloc<CRowAtlasAlloc>;

	static constexpr uint32_t MAX_TEXTURE_PAGES = 16;

	switch (allocType) {
		case CTextureAtlas::ATLAS_ALLOC_LEGACY:      { atlasAllocator = std::make_unique<   CLegacyAtlasAlloc>(                 ); } break;
		case CTextureAtlas::ATLAS_ALLOC_QUADTREE:    { atlasAllocator = std::make_unique< CQuadtreeAtlasAlloc>(                 ); } break;
		case CTextureAtlas::ATLAS_ALLOC_ROW:         { atlasAllocator = std::make_unique<      CRowAtlasAlloc>(                 ); } break;
		case CTextureAtlas::ATLAS_ALLOC_MP_LEGACY:   { atlasAllocator = std::make_unique<  MPLegacyAtlasAlloc>(MAX_TEXTURE_PAGES); } break;
		case CTextureAtlas::ATLAS_ALLOC_MP_QUADTREE: { atlasAllocator = std::make_unique<MPQuadtreeAtlasAlloc>(MAX_TEXTURE_PAGES); } break;
		case CTextureAtlas::ATLAS_ALLOC_MP_ROW:      { atlasAllocator = std::make_unique<     MPRowAtlasAlloc>(MAX_TEXTURE_PAGES); } break;
		default:                                     {                                                              assert(false); } break;
	}

	atlasSizeX = std::min<int>(RHI::GetDevice()->GetMaxTextureSize(), (atlasSizeX > 0) ? atlasSizeX : configHandler->GetInt("MaxTextureAtlasSizeX"));
	atlasSizeY = std::min<int>(RHI::GetDevice()->GetMaxTextureSize(), (atlasSizeY > 0) ? atlasSizeY : configHandler->GetInt("MaxTextureAtlasSizeY"));

	atlasAllocator->SetMaxSize(atlasSizeX, atlasSizeY);

	if (shaderRef == 0) {
		shader = shaderHandler->CreateProgramObject("[TextureRenderAtlas]", "TextureRenderAtlas");
		shader->AttachShaderObject(shaderHandler->CreateShaderObject(vsTRA, "", GL_VERTEX_SHADER));
		shader->AttachShaderObject(shaderHandler->CreateShaderObject(fsTRA, "", GL_FRAGMENT_SHADER));
		shader->BindAttribLocation("pos", 0);
		shader->BindAttribLocation("uv", 1);
		shader->Link();

		shader->Enable();
		shader->SetUniform("tex", 0);
		shader->SetUniform("lod", 0.0f);
		shader->Disable();
		shader->Validate();
	}

	shaderRef++;
}

CTextureRenderAtlas::~CTextureRenderAtlas()
{
	RECOIL_DETAILED_TRACY_ZONE;
	shaderRef--;

	if (shaderRef == 0)
		shaderHandler->ReleaseProgramObjects("[TextureRenderAtlas]");

	for (auto& [_, tID] : filenameToTexID) {
		if (tID) {
			// RHI_TODO: migrate once CBitmap returns RHI textures (CreateMipMapTexture -> CreateTextureRHI)
			glDeleteTextures(1, &tID);
			tID = 0;
		}
	}

	atlasTex = nullptr;
}

bool CTextureRenderAtlas::TextureExists(const std::string& texName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto it = nameToUniqueSubTexStr.find(texName);
	if (it == nameToUniqueSubTexStr.end())
		return false;

	return atlasAllocator->contains(it->second);
}

bool CTextureRenderAtlas::TextureExists(const std::string& texName, const std::string& texBackupName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	return TextureExists(texName) || TextureExists(texBackupName);
}

bool CTextureRenderAtlas::AddTexFromFile(const std::string& name, const std::string& fileName, const float4& subTexCoords)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (atlasFinalized)
		return false;

	// doesn't contain the texture already and can't find the file
	if (!filenameToTexID.contains(fileName) && !CFileHandler::FileExists(fileName, SPRING_VFS_ALL))
		return false;

	CBitmap bm;
	if (!bm.Load(fileName))
		return false;

	return AddTexFromBitmapRaw(name, bm, subTexCoords, fileName);
}

bool CTextureRenderAtlas::AddTexFromBitmap(const std::string& name, const CBitmap& bm, const std::string& refFileName, const float4& subTexCoords)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (atlasFinalized)
		return false;

	return AddTexFromBitmapRaw(name, bm, subTexCoords, refFileName);
}


bool CTextureRenderAtlas::AddTexFromBitmapRaw(const std::string& name, const CBitmap& bm, const float4& subTexCoords, const std::string& refFileName)
{
	RECOIL_DETAILED_TRACY_ZONE;

	auto it = filenameToTexID.find(refFileName);
	if (it == filenameToTexID.end()) {
		// RHI_TODO: migrate to bm.CreateTextureRHI() once atlas rendering pipeline supports RHI textures
		it = filenameToTexID.emplace(refFileName, bm.CreateMipMapTexture()).first;
	}

	const auto uniqueSubTex = UniqueSubTexture(
		it->second,
		subTexCoords
	);
	const auto uniqueSubTexStr = uniqueSubTex.GetName();

	if (!atlasAllocator->contains(uniqueSubTexStr)) {
		int2 subTexSize = {
			static_cast<int>(bm.xsize * (subTexCoords.z - subTexCoords.x)),
			static_cast<int>(bm.ysize * (subTexCoords.w - subTexCoords.y))
		};
		atlasAllocator->AddEntry(uniqueSubTexStr, subTexSize);
		uniqueSubTextureMap[uniqueSubTexStr] = uniqueSubTex;
	}

	return nameToUniqueSubTexStr.emplace(name, uniqueSubTexStr).second;
}


bool CTextureRenderAtlas::AddTex(const std::string& name, int xsize, int ysize, const SColor& color, const std::string& refFileName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (atlasFinalized)
		return false;

	if (nameToUniqueSubTexStr.contains(name))
		return false;

	CBitmap bm;
	bm.AllocDummy(color);
	bm = bm.CreateRescaled(xsize, ysize);

	return AddTexFromBitmapRaw(name, bm, float4(0.0f, 0.0f, 1.0f, 1.0f), refFileName);
}

AtlasedTexture CTextureRenderAtlas::GetTexture(const std::string& texName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!atlasFinalized)
		return AtlasedTexture::DefaultAtlasTexture;

	auto it = nameToUniqueSubTexStr.find(texName);
	if (it == nameToUniqueSubTexStr.end())
		return AtlasedTexture::DefaultAtlasTexture;

	return AtlasedTexture(atlasAllocator->GetTexCoords(it->second));
}

AtlasedTexture CTextureRenderAtlas::GetTexture(const std::string& texName, const std::string& texBackupName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!atlasFinalized)
		return AtlasedTexture::DefaultAtlasTexture;

	auto it = nameToUniqueSubTexStr.find(texName);
	if (it != nameToUniqueSubTexStr.end())
		return AtlasedTexture(atlasAllocator->GetTexCoords(it->second));

	if (texBackupName.empty())
		return AtlasedTexture::DefaultAtlasTexture;

	return GetTexture(texBackupName);
}

std::vector<std::string> CTextureRenderAtlas::GetAllFileNames() const
{
	std::vector<std::string> fileNames;
	fileNames.reserve(filenameToTexID.size());
	for (const auto& [name, _] : filenameToTexID) {
		fileNames.emplace_back(name);
	}

	return fileNames;
}

uint32_t CTextureRenderAtlas::GetTexTarget() const
{
	return (atlasAllocator->GetNumPages() > 1) ?
		GL_TEXTURE_2D_ARRAY :
		GL_TEXTURE_2D;
}

uint32_t CTextureRenderAtlas::GetTexID() const
{
	if (!atlasRendered)
		return 0;

	return atlasTex->GetNativeHandle();
}

int CTextureRenderAtlas::GetMinDim() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return atlasAllocator->GetMinDim();
}

const int2& CTextureRenderAtlas::GetAtlasSize() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return atlasAllocator->GetAtlasSize();
}

int CTextureRenderAtlas::GetNumTexLevels() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return atlasAllocator->GetNumTexLevels();
}

void CTextureRenderAtlas::SetMaxTexLevel(int maxLevels)
{
	RECOIL_DETAILED_TRACY_ZONE;
	atlasAllocator->SetMaxTexLevel(maxLevels);
}

bool CTextureRenderAtlas::IsValid() const
{
	return atlasFinalized && atlasRendered;
}

uint32_t CTextureRenderAtlas::DisownTexture()
{
	if (!atlasRendered)
		return 0;

	return atlasTex->DisownNativeHandle();
}

bool CTextureRenderAtlas::DumpTexture() const
{
	RECOIL_DETAILED_TRACY_ZONE;

	if (!IsValid()) {
		LOG_L(L_ERROR, "[CTextureRenderAtlas::%s] Can't dump invalid %s atlas", __func__, atlasName.c_str());
		return false;
	}
	const auto numLevels = atlasAllocator->GetNumTexLevels();
	const auto numPages = atlasAllocator->GetNumPages();

	if (numPages > 1) {
		for (uint32_t page = 0; page < numPages; ++page) {
			for (uint32_t level = 0; level < numLevels; ++level) {
				glSaveTextureArray(atlasTex->GetNativeHandle(), fmt::format("{}_{}_{}.png", atlasName, page, level).c_str(), level, page);
			}
		}
	}
	else {
		for (uint32_t level = 0; level < numLevels; ++level) {
			glSaveTexture(atlasTex->GetNativeHandle(), fmt::format("{}_{}.png", atlasName, level).c_str(), level);
		}
	}

	return true;
}

bool CTextureRenderAtlas::CalculateAtlas()
{
	if (atlasFinalized)
		return true;

	atlasFinalized = atlasAllocator->Allocate();
	LOG_L(L_INFO, "CTextureRenderAtlas::%s() atlas=%s atlasFinalized=%d", __func__, atlasName.c_str(), atlasFinalized);
	return atlasFinalized;
}

bool CTextureRenderAtlas::CreateAtlasTexture()
{
	if (!atlasFinalized)
		return true;

	if (atlasRendered)
		return true;

	LOG_L(L_INFO, "CTextureRenderAtlas::%s()[0] atlas=%s FBO::ready=%d", __func__, atlasName.c_str(), FBO::IsReady());

	if (!FBO::IsReady())
		return false;

	const auto numLevels = atlasAllocator->GetNumTexLevels();
	const auto numPages = atlasAllocator->GetNumPages();

	const auto atlasSize = atlasAllocator->GetAtlasSize();

	{
		auto* device = RHI::GetDevice();
		const auto rhiFormat = GLInternalToRHIFormat(glInternalType);
		atlasTex.reset();  // Destroy previous texture if re-entrant

		if (numPages > 1) {
			atlasTex = device->CreateTexture(
				RHI::TextureType::Texture2DArray, rhiFormat,
				atlasSize.x, atlasSize.y, numPages, numLevels);
		} else {
			atlasTex = device->CreateTexture(
				RHI::TextureType::Texture2D, rhiFormat,
				atlasSize.x, atlasSize.y, 1, numLevels);
		}
		atlasTex->SetMinFilter(RHI::TextureFilter::LinearMipmapLinear);
		atlasTex->SetMagFilter(RHI::TextureFilter::Linear);
		atlasTex->SetWrapS(RHI::TextureWrap::ClampToEdge);
		atlasTex->SetWrapT(RHI::TextureWrap::ClampToEdge);
	}

	{
		using namespace GL::State;
		auto state = GL::SubState(
			DepthTest(GL_FALSE),
			Blending(GL_FALSE),
			DepthMask(GL_FALSE)
		);

		FBO fbo;
		fbo.Init(false);
		fbo.Bind();
		if (numPages > 1)
			fbo.AttachTextureLayer(atlasTex->GetNativeHandle(), GL_COLOR_ATTACHMENT0, 0, 0);
		else
			fbo.AttachTexture(atlasTex->GetNativeHandle(), GL_TEXTURE_2D, GL_COLOR_ATTACHMENT0, 0);
		fbo.CheckStatus("TEXTURE-RENDER-ATLAS");

		atlasRendered = (fbo.IsValid() && atlasTex->GetNativeHandle() > 0);

		if (atlasRendered) {
			static const auto Norm2SNorm = [](float value) { return (value * 2.0f - 1.0f); };
			auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_2DT>();

			auto* ctx = RHI::GetDevice()->GetContext();

			for (uint32_t page = 0; page < numPages; ++page) {
				for (uint32_t level = 0; level < numLevels; ++level) {
					ctx->SetViewport({
						0.0f, 0.0f,
						static_cast<float>(std::max(atlasSize.x >> level, 1)),
						static_cast<float>(std::max(atlasSize.y >> level, 1))
					});

					// RHI_TODO: FBO attachment still uses GL; migrate once FBO has RHI wrapper
					if (numPages > 1)
						fbo.AttachTextureLayer(atlasTex->GetNativeHandle(), GL_COLOR_ATTACHMENT0, level, page);
					else
						fbo.AttachTexture(atlasTex->GetNativeHandle(), GL_TEXTURE_2D, GL_COLOR_ATTACHMENT0, level);

					// RHI_TODO: migrate to RHI framebuffer binding API once available
					glDrawBuffer(GL_COLOR_ATTACHMENT0);
					glReadBuffer(GL_COLOR_ATTACHMENT0);

					auto shEnToken = shader->EnableScoped();
					shader->SetUniform("lod", static_cast<float>(level));
					// draw
					for (auto& [uniqTexName, entry] : atlasAllocator->GetEntries()) {
						if (entry.texCoords.pageNum != page)
							continue;

						const auto atlasedTexCoords = atlasAllocator->GetTexCoords(uniqTexName);
						const auto& [srcTexID, srcSubTC] = uniqueSubTextureMap[uniqTexName];

						if (srcTexID == 0)
							continue;

						auto posTL = VA_TYPE_2DT{ .x = Norm2SNorm(atlasedTexCoords.x1), .y = Norm2SNorm(atlasedTexCoords.y1), .s = srcSubTC.x, .t = srcSubTC.y };
						auto posTR = VA_TYPE_2DT{ .x = Norm2SNorm(atlasedTexCoords.x2), .y = Norm2SNorm(atlasedTexCoords.y1), .s = srcSubTC.z, .t = srcSubTC.y };
						auto posBL = VA_TYPE_2DT{ .x = Norm2SNorm(atlasedTexCoords.x1), .y = Norm2SNorm(atlasedTexCoords.y2), .s = srcSubTC.x, .t = srcSubTC.w };
						auto posBR = VA_TYPE_2DT{ .x = Norm2SNorm(atlasedTexCoords.x2), .y = Norm2SNorm(atlasedTexCoords.y2), .s = srcSubTC.z, .t = srcSubTC.w };

						auto texBind = GL::TexBind(GL_TEXTURE_2D, srcTexID);

						// RHI_TODO: migrate to IRHITexture::SetMinFilter/SetMagFilter/SetWrap* once CBitmap returns RHI textures
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

						rb.AddQuadTriangles(
							std::move(posTL),
							std::move(posTR),
							std::move(posBR),
							std::move(posBL)
						);

						rb.DrawElements(GL_TRIANGLES);
					}
				}
			}
		}

		fbo.DetachAll();
		FBO::Unbind();
		globalRendering->LoadViewport();
	}

	LOG_L(L_INFO, "CTextureRenderAtlas::%s()[1] atlas=%s atlasRendered=%d", __func__, atlasName.c_str(), atlasRendered);

	if (!atlasRendered)
		return false;

	for (auto& [_, texID] : filenameToTexID) {
		if (texID) {
			// RHI_TODO: migrate once CBitmap returns RHI textures (CreateMipMapTexture -> CreateTextureRHI)
			glDeleteTextures(1, &texID);
			texID = 0;
		}
	}

	return true;
}
