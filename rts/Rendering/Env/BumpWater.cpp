/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * @brief extended bump-mapping water shader
 */

// RHI Migration Status: COMPLETE
// ================================
// This file has been migrated to use RHI abstractions for Metal compatibility.
//
// Completed migrations:
// 1. Texture Management - All internal textures use std::unique_ptr<RHI::IRHITexture>
// 2. FBO Operations - All FBOs use std::unique_ptr<RHI::IRHIFramebuffer>
// 3. FFP Matrix Stack - Replaced with CMatrix44f + shader uniform u_mvpMatrix
// 4. glPushAttrib/glPopAttrib - Replaced with RHI::ScopedPipeline
// 5. Texture binding - Uses IRHITexture::Bind() where possible
// 6. Viewport - ctx->SetViewport() replaces glViewport
// 7. Clear - ctx->ClearColor()/ctx->Clear() replaces glClearColor/glClear
// 8. BlendColor - BlendState.blendColor replaces glBlendColor
// 9. Refraction copy - BlitFramebuffer replaces glCopyTexSubImage2D (color)
// 10. Fog state - Removed legacy FFP glPushAttrib(GL_FOG_BIT)/glDisable(GL_FOG)
//
// 11. coastUpdateTexture - Wrapped as non-owning RHI texture for binding/FBO attach
//
// Remaining GL calls (intentional):
// - coastUpdateTextureGL: Raw GLuint for lifecycle (glDeleteTextures only)
// - Shadow depth texture (SetupShadowTexSampler / ResetShadowTexSamplerRaw)
// - glCopyTexSubImage2D: Depth copy only (depth texture not in FBO, can't blit)
// - Shader creation: GL_VERTEX_SHADER/GL_FRAGMENT_SHADER via shaderHandler API
// - GLAD_GL_ARB_imaging: Feature detection constant
//
// GLSL shaders can be cross-compiled to MSL via SPIRV-Cross.

#include "BumpWater.h"

#include "ISky.h"
#include "SunLighting.h"
#include "WaterRendering.h"

#include "Game/Camera.h"
#include "Game/CameraHandler.h"
#include "Game/GlobalUnsynced.h"
#include "Map/MapInfo.h"
#include "Map/ReadMap.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Map/InfoTexture/IInfoTextureHandler.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/Textures/Bitmap.h"
#include "Rendering/Textures/TextureAtlas.h"
#include "System/FileSystem/FileHandler.h"
#include "System/FastMath.h"
#include "System/SpringMath.h"
#include "System/EventHandler.h"
#include "System/Config/ConfigHandler.h"
#include "System/TimeProfiler.h"
#include "System/Log/ILog.h"
#include "System/Exceptions.h"
#include "System/SpringFormat.h"
#include "System/StringUtil.h"
#include "System/Matrix44f.h"

#include "Rendering/RHI/RHIScopedState.h"

#include "System/Misc/TracyDefs.h"
#include <bit>
using std::string;
using std::vector;
using std::min;
using std::max;

CONFIG(int, BumpWaterTexSizeReflection).defaultValue(512).headlessValue(32).minimumValue(32).description("Sets the size of the framebuffer texture used to store the reflection in Bumpmapped water.");
CONFIG(int, BumpWaterReflection).defaultValue(1).headlessValue(0).minimumValue(0).maximumValue(2).description("Determines the amount of objects reflected in Bumpmapped water.\n0:=off, 1:=fast (skip terrain), 2:=full");
CONFIG(int, BumpWaterRefraction).defaultValue(1).headlessValue(0).minimumValue(0).maximumValue(1).description("Determines the method of refraction with Bumpmapped water.\n0:=off, 1:=screencopy, 2:=own rendering cycle (disabled)");
CONFIG(float, BumpWaterAnisotropy).defaultValue(0.0f).minimumValue(0.0f);
CONFIG(bool, BumpWaterUseDepthTexture).defaultValue(true).headlessValue(false);
CONFIG(int, BumpWaterDepthBits).defaultValue(24).minimumValue(16).maximumValue(32);
CONFIG(bool, BumpWaterBlurReflection).defaultValue(false);
CONFIG(bool, BumpWaterShoreWaves).defaultValue(true).headlessValue(false).safemodeValue(false).description("Enables rendering of shorewaves.");
CONFIG(bool, BumpWaterEndlessOcean).defaultValue(true).description("Sets whether Bumpmapped water will be drawn beyond the map edge.");
CONFIG(bool, BumpWaterDynamicWaves).defaultValue(true);
CONFIG(bool, BumpWaterUseUniforms).deprecated(true);
CONFIG(bool, BumpWaterOcclusionQuery).deprecated(true);

#define LOG_SECTION_BUMP_WATER "BumpWater"
LOG_REGISTER_SECTION_GLOBAL(LOG_SECTION_BUMP_WATER)

// use the specific section for all LOG*() calls in this source file
#ifdef LOG_SECTION_CURRENT
	#undef LOG_SECTION_CURRENT
#endif
#define LOG_SECTION_CURRENT LOG_SECTION_BUMP_WATER

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// HELPER FUNCTIONS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void GLSLDefineConst4f(string& str, const string& name, const float x, const float y, const float z, const float w)
{
	str += spring::format(string("#define ") + name + " vec4(%.12f,%.12f,%.12f,%.12f)\n", x, y, z, w);
}

static void GLSLDefineConstf4(string& str, const string& name, const float3& v, float alpha)
{
	str += spring::format(string("#define ") + name + " vec4(%.12f,%.12f,%.12f,%.12f)\n", v.x, v.y, v.z, alpha);
}

static void GLSLDefineConstf3(string& str, const string& name, const float3& v)
{
	str += spring::format(string("#define ") + name + " vec3(%.12f,%.12f,%.12f)\n", v.x, v.y, v.z);
}

static void GLSLDefineConstf2(string& str, const string& name, float x, float y)
{
	str += spring::format(string("#define ") + name + " vec2(%.12f,%.12f)\n", x, y);
}

static void GLSLDefineConstf1(string& str, const string& name, float x)
{
	str += spring::format(string("#define ") + name + " %.12f\n", x);
}


static std::unique_ptr<RHI::IRHITexture> LoadTextureRHI(
	const string& filename, const float anisotropy = 0.0f, int* sizeX = nullptr, int* sizeY = nullptr)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CBitmap bm;

	if (!bm.Load(filename))
		throw content_error("[" LOG_SECTION_BUMP_WATER "] Could not load texture from file " + filename);

	if (sizeX != nullptr) {
		*sizeX = bm.xsize;
		*sizeY = bm.ysize;
	}

	return bm.CreateTextureRHI(anisotropy, 0.0f);
}


static TypedRenderBuffer<VA_TYPE_0> GenWaterPlaneBuffer(bool radial)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto rb = TypedRenderBuffer<VA_TYPE_0>(9 * 9 * 6, 0, IStreamBufferConcept::Types::SB_BUFFERDATA);

	if (radial) {
		// FIXME: more or less copied from SMFGroundDrawer
		const float xsize = static_cast<float>((mapDims.mapx * SQUARE_SIZE) >> 2);
		const float ysize = static_cast<float>((mapDims.mapy * SQUARE_SIZE) >> 2);

		const float alphainc = math::TWOPI / 32.0f;
		const float size = std::min(xsize, ysize);

		float3 p; p.y = 0.0f;

		for (int n = 0; n < 4; ++n) {
			const float k = (n == 3) ? 0.5f : 1.0f;

			const float r1 = (n + 0) * (n + 0) * size;
			const float r2 = (n + k) * (n + k) * size;

			for (float alpha = 0.0f; (alpha - math::TWOPI) < alphainc; alpha += alphainc) {
				p.x = r1 * fastmath::sin(alpha) + 2 * xsize;
				p.z = r1 * fastmath::cos(alpha) + 2 * ysize;
				rb.AddVertex({ p });

				p.x = r2 * fastmath::sin(alpha) + 2 * xsize;
				p.z = r2 * fastmath::cos(alpha) + 2 * ysize;
				rb.AddVertex({ p });
			}
		}
	}
	else {
		const int mapX = mapDims.mapx * SQUARE_SIZE;
		const int mapZ = mapDims.mapy * SQUARE_SIZE;

		for (int z = 0; z < 9; z++) {
			for (int x = 0; x < 9; x++) {
				const float3 v0{ (x + 0) * (mapX / 9.0f), 0.0f, (z + 0) * (mapZ / 9.0f) };
				const float3 v1{ (x + 0) * (mapX / 9.0f), 0.0f, (z + 1) * (mapZ / 9.0f) };
				const float3 v2{ (x + 1) * (mapX / 9.0f), 0.0f, (z + 0) * (mapZ / 9.0f) };
				const float3 v3{ (x + 1) * (mapX / 9.0f), 0.0f, (z + 1) * (mapZ / 9.0f) };

				rb.AddVertex({ v0 });
				rb.AddVertex({ v1 });
				rb.AddVertex({ v2 });

				rb.AddVertex({ v1 });
				rb.AddVertex({ v3 });
				rb.AddVertex({ v2 });
			}
		}
	}

	rb.SetReadonly();
	return rb;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// (DE-)CONSTRUCTOR
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CBumpWater::CBumpWater()

	: CEventClient("[CBumpWater]", 271923, false)
	, screenTextureX(globalRendering->viewSizeX)
	, screenTextureY(globalRendering->viewSizeY)
	, coastUpdateTextureGL(0)
{
	eventHandler.AddClient(this);
}

CBumpWater::~CBumpWater()
{
	RECOIL_DETAILED_TRACY_ZONE;
	FreeResources();
	eventHandler.RemoveClient(this);
}

void CBumpWater::InitResources(bool loadShader)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// LOAD USER CONFIGS
	reflTexSize  = std::bit_ceil <uint32_t> (configHandler->GetInt("BumpWaterTexSizeReflection"));
	reflection   = configHandler->GetInt("BumpWaterReflection");
	refraction   = configHandler->GetInt("BumpWaterRefraction");
	anisotropy   = configHandler->GetFloat("BumpWaterAnisotropy");
	depthCopy    = configHandler->GetBool("BumpWaterUseDepthTexture");
	depthBits    = configHandler->GetInt("BumpWaterDepthBits");
	depthBits    = std::min(depthBits, static_cast<char>(globalRendering->supportDepthBufferBitDepth));
	blurRefl     = configHandler->GetBool("BumpWaterBlurReflection");
	shoreWaves   = (configHandler->GetBool("BumpWaterShoreWaves")) && waterRendering->shoreWaves;
	endlessOcean = (configHandler->GetBool("BumpWaterEndlessOcean")) && waterRendering->hasWaterPlane
	               && ((readMap->HasVisibleWater()) || (waterRendering->forceRendering));
	dynWaves     = (configHandler->GetBool("BumpWaterDynamicWaves")) && (waterRendering->numTiles > 1);

	shoreWaves = shoreWaves && (FBO::IsSupported());
	dynWaves   = dynWaves && (FBO::IsSupported() && GLAD_GL_ARB_imaging);

	// LOAD TEXTURES
	foamTexture   = LoadTextureRHI(waterRendering->foamTexture);
	normalTexture = LoadTextureRHI(waterRendering->normalTexture, anisotropy, &normalTextureX, &normalTextureY);

	// caustic textures
	const vector<string>& causticNames = waterRendering->causticTextures;
	if (causticNames.empty()) {
		throw content_error("[" LOG_SECTION_BUMP_WATER "] no caustic textures");
	}
	for (int i = 0; i < (int)causticNames.size(); ++i) {
		caustTextures.push_back(LoadTextureRHI(causticNames[i]));
	}

	// CHECK SHOREWAVES TEXTURE SIZE
	// All modern GPUs support at least 4096x4096 RGBA16F
	if (shoreWaves) {
		if (mapDims.mapx > 4096 || mapDims.mapy > 4096) {
			shoreWaves = false;
			LOG_L(L_WARNING, "Can not display shorewaves (map too large)!");
		}
	}


	// SHOREWAVES
	if (shoreWaves) {
		waveRandTexture = LoadTextureRHI("bitmaps/shorewaverand.png");

		// Create coast texture using RHI
		auto* device = GetRHIDevice();
		// Use RGB8 as closest RHI format to GL_RGB5
		coastTexture = device->CreateTexture(
			RHI::TextureType::Texture2D,
			RHI::TextureFormat::RGB8,
			mapDims.mapx, mapDims.mapy, 1, 1, 1);
		coastTexture->SetWrapS(RHI::TextureWrap::ClampToEdge);
		coastTexture->SetWrapT(RHI::TextureWrap::ClampToEdge);
		coastTexture->SetMagFilter(RHI::TextureFilter::Nearest);
		coastTexture->SetMinFilter(RHI::TextureFilter::Nearest);


		{
			blurShader = shaderHandler->CreateProgramObject("[BumpWater]", "CoastBlurShader");
			blurShader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/BumpWaterCoastBlurVS.glsl", "", GL_VERTEX_SHADER));
			blurShader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/BumpWaterCoastBlurFS.glsl", "", GL_FRAGMENT_SHADER));
			blurShader->BindAttribLocations<VA_TYPE_T4>();
			blurShader->Link();

			if (!blurShader->IsValid()) {
				const char* fmt = "shorewaves-shader compilation error: %s";
				const char* log = (blurShader->GetLog()).c_str();

				LOG_L(L_ERROR, fmt, log);

				// string size is limited with content_error()
				throw content_error(string("[" LOG_SECTION_BUMP_WATER "] shorewaves-shader compilation error!"));
			}

			blurShader->Enable();
			blurShader->SetUniform("tex0", 0);
			blurShader->SetUniform("tex1", 1);
			blurShader->SetUniform("args", 0, 0);
			// Initialize u_mvpMatrix to identity (will be set per-frame in UpdateCoastmap)
			CMatrix44f identityMatrix;
			blurShader->SetUniformMatrix4x4("u_mvpMatrix", false, identityMatrix.m);
			blurShader->Disable();
			blurShader->Validate();

			if (!blurShader->IsValid()) {
				const char* fmt = "shorewaves-shader validation error: %s";
				const char* log = (blurShader->GetLog()).c_str();

				LOG_L(L_ERROR, fmt, log);
				throw content_error(string("[" LOG_SECTION_BUMP_WATER "] shorewaves-shader validation error!"));
			}
		}


		// Create coast FBO using RHI
		coastFBO = device->CreateFramebuffer();
		coastFBO->AttachColor(coastTexture.get(), 0);

		if ((shoreWaves = coastFBO->IsComplete())) {
			// initialize texture - bind and clear
			coastFBO->Bind();
			auto* ctx = device->GetContext();
			ctx->ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			ctx->Clear(true, false, false);
			coastFBO->Unbind();

			// fill with current heightmap/coastmap
			UnsyncedHeightMapUpdate(SRectangle(0, 0, mapDims.mapx, mapDims.mapy));
			UploadCoastline(true);
			UpdateCoastmap(true);

			eventHandler.InsertEvent(this, "UnsyncedHeightMapUpdate");
		} else {
			LOG_L(L_WARNING, "[BumpWater] Coast FBO not complete");
		}
	}


	// CREATE TEXTURES
	auto* device = GetRHIDevice();

	if (refraction > 0) {
		// CREATE REFRACTION TEXTURE
		refractTexture = device->CreateTexture(
			RHI::TextureType::Texture2D,
			RHI::TextureFormat::RGBA8,
			screenTextureX, screenTextureY, 1, 1, 1);
		refractTexture->SetMagFilter(RHI::TextureFilter::Nearest);
		refractTexture->SetMinFilter(RHI::TextureFilter::Nearest);
		refractTexture->SetWrapS(RHI::TextureWrap::ClampToEdge);
		refractTexture->SetWrapT(RHI::TextureWrap::ClampToEdge);
	}

	if (reflection > 0) {
		// CREATE REFLECTION TEXTURE
		reflectTexture = device->CreateTexture(
			RHI::TextureType::Texture2D,
			RHI::TextureFormat::RGBA8,
			reflTexSize, reflTexSize, 1, 1, 1);
		reflectTexture->SetMagFilter(RHI::TextureFilter::Linear);
		reflectTexture->SetMinFilter(RHI::TextureFilter::Linear);
		reflectTexture->SetWrapS(RHI::TextureWrap::ClampToEdge);
		reflectTexture->SetWrapT(RHI::TextureWrap::ClampToEdge);
	}

	if (depthCopy) {
		// CREATE DEPTH TEXTURE
		// Map depth bits to RHI format
		RHI::TextureFormat depthFormat = RHI::TextureFormat::Depth24;
		if (globalRendering->supportDepthBufferBitDepth >= 32)
			depthFormat = RHI::TextureFormat::Depth32F;
		else if (globalRendering->supportDepthBufferBitDepth <= 16)
			depthFormat = RHI::TextureFormat::Depth16;

		depthTexture = device->CreateTexture(
			RHI::TextureType::Texture2D,
			depthFormat,
			screenTextureX, screenTextureY, 1, 1, 1);
		depthTexture->SetMagFilter(RHI::TextureFilter::Nearest);
		depthTexture->SetMinFilter(RHI::TextureFilter::Nearest);
	}

	if (dynWaves) {
		// SETUP DYNAMIC WAVES
		tileOffsets.resize(waterRendering->numTiles * waterRendering->numTiles);

		// Move the loaded normalTexture to normalTexture2 and set to nearest filtering
		normalTexture2 = std::move(normalTexture);
		normalTexture2->SetMagFilter(RHI::TextureFilter::Nearest);
		normalTexture2->SetMinFilter(RHI::TextureFilter::Nearest);

		// Create new normalTexture for rendering output
		normalTexture = device->CreateTexture(
			RHI::TextureType::Texture2D,
			RHI::TextureFormat::RGBA8,
			normalTextureX, normalTextureY, 1, 0, 1);  // 0 mipLevels = auto-generate
		normalTexture->SetMagFilter(RHI::TextureFilter::Linear);
		normalTexture->SetMinFilter(RHI::TextureFilter::LinearMipmapLinear);
		if (anisotropy > 0.0f) {
			normalTexture->SetAnisotropy(anisotropy);
		}
		normalTexture->GenerateMipmaps();
	}

	// CREATE FBOs using RHI
	// Map depth bits to RHI format for renderbuffers
	RHI::TextureFormat depthRBOFormat = RHI::TextureFormat::Depth24;
	if (depthBits >= 32)
		depthRBOFormat = RHI::TextureFormat::Depth32F;
	else if (depthBits <= 16)
		depthRBOFormat = RHI::TextureFormat::Depth16;

	if (reflection > 0) {
		reflectFBO = device->CreateFramebuffer();
		reflectFBO->AttachColor(reflectTexture.get(), 0);
		reflectFBO->AttachRenderbuffer(depthRBOFormat, reflTexSize, reflTexSize, 0);
		if (!reflectFBO->IsComplete()) {
			LOG_L(L_WARNING, "[BumpWater] Reflection FBO not complete");
			reflection = 0;
		}
	}

	if (refraction > 0) {
		refractFBO = device->CreateFramebuffer();
		refractFBO->AttachColor(refractTexture.get(), 0);
		refractFBO->AttachRenderbuffer(depthRBOFormat, screenTextureX, screenTextureY, 0);
		if (!refractFBO->IsComplete()) {
			LOG_L(L_WARNING, "[BumpWater] Refraction FBO not complete");
			refraction = 0;
		}
	}

	if (dynWaves) {
		dynWavesFBO = device->CreateFramebuffer();
		dynWavesFBO->AttachColor(normalTexture.get(), 0);
		if (dynWavesFBO->IsComplete()) {
			UpdateDynWaves(true); // initialize
		} else {
			LOG_L(L_WARNING, "[BumpWater] DynWaves FBO not complete");
		}
	}


	/*
	 * DEFINE SOME SHADER RUNTIME CONSTANTS
	 * (I do not use Uniforms for that, because the GLSL compiler can not
	 * optimize those!)
	 */
	string definitions;
	if (reflection>0) definitions += "#define opt_reflection\n";
	if (refraction>0) definitions += "#define opt_refraction\n";
	if (shoreWaves)   definitions += "#define opt_shorewaves\n";
	if (depthCopy)    definitions += "#define opt_depth\n";
	if (blurRefl)     definitions += "#define opt_blurreflection\n";
	if (endlessOcean) definitions += "#define opt_endlessocean\n";

	GLSLDefineConstf3(definitions, "MapMid",                    float3(mapDims.mapx * SQUARE_SIZE * 0.5f, 0.0f, mapDims.mapy * SQUARE_SIZE * 0.5f));
	GLSLDefineConstf2(definitions, "ScreenInverse",             1.0f / globalRendering->viewSizeX, 1.0f / globalRendering->viewSizeY);
	GLSLDefineConstf2(definitions, "ScreenTextureSizeInverse",  1.0f / screenTextureX, 1.0f / screenTextureY);
	GLSLDefineConstf2(definitions, "ViewPos",                   globalRendering->viewPosX, globalRendering->viewPosY);

	GLSLDefineConstf4(definitions, "SurfaceColor",   waterRendering->surfaceColor*0.4, waterRendering->surfaceAlpha );
	GLSLDefineConstf4(definitions, "PlaneColor",     waterRendering->planeColor*0.4, waterRendering->surfaceAlpha );
	GLSLDefineConstf3(definitions, "DiffuseColor",   waterRendering->diffuseColor);
	GLSLDefineConstf3(definitions, "SpecularColor",  waterRendering->specularColor);
	GLSLDefineConstf1(definitions, "SpecularPower",  waterRendering->specularPower);
	GLSLDefineConstf1(definitions, "SpecularFactor", waterRendering->specularFactor);
	GLSLDefineConstf1(definitions, "AmbientFactor",  waterRendering->ambientFactor);
	GLSLDefineConstf1(definitions, "DiffuseFactor",  waterRendering->diffuseFactor * 15.0f);
	GLSLDefineConstf3(definitions, "SunDir"       ,  ISky::GetSky()->GetLight()->GetLightDir()); // FIXME: not a constant
	GLSLDefineConstf1(definitions, "FresnelMin",     waterRendering->fresnelMin);
	GLSLDefineConstf1(definitions, "FresnelMax",     waterRendering->fresnelMax);
	GLSLDefineConstf1(definitions, "FresnelPower",   waterRendering->fresnelPower);
	GLSLDefineConstf1(definitions, "ReflDistortion", waterRendering->reflDistortion);
	GLSLDefineConstf2(definitions, "BlurBase",       0.0f, waterRendering->blurBase / globalRendering->viewSizeY);
	GLSLDefineConstf1(definitions, "BlurExponent",   waterRendering->blurExponent);
	GLSLDefineConstf1(definitions, "PerlinStartFreq",  waterRendering->perlinStartFreq);
	GLSLDefineConstf1(definitions, "PerlinLacunarity", waterRendering->perlinLacunarity);
	GLSLDefineConstf1(definitions, "PerlinAmp",        waterRendering->perlinAmplitude);
	GLSLDefineConstf1(definitions, "WaveOffsetFactor",   waterRendering->waveOffsetFactor);
	GLSLDefineConstf1(definitions, "WaveLength",         waterRendering->waveLength);
	GLSLDefineConstf1(definitions, "WaveFoamDistortion", waterRendering->waveFoamDistortion);
	GLSLDefineConstf1(definitions, "WaveFoamIntensity",  waterRendering->waveFoamIntensity);
	GLSLDefineConstf1(definitions, "CausticsResolution", waterRendering->causticsResolution);
	GLSLDefineConstf1(definitions, "CausticsStrength",   waterRendering->causticsStrength);
	GLSLDefineConstf1(definitions, "shadowDensity",  sunLighting->groundShadowDensity);

	{
		const int mapX = mapDims.mapx  * SQUARE_SIZE;
		const int mapZ = mapDims.mapy * SQUARE_SIZE;
		const float shadingX = (float)mapDims.mapx / mapDims.pwr2mapx;
		const float shadingZ = (float)mapDims.mapy / mapDims.pwr2mapy;

		const float scaleX = (mapX > mapZ) ? (mapDims.mapy >> 6) / 16.0f * (float)mapX / mapZ : (mapDims.mapx >> 6) / 16.0f;
		const float scaleZ = (mapX > mapZ) ? (mapDims.mapy >> 6) / 16.0f : (mapDims.mapx >> 6) / 16.0f * (float)mapZ / mapX;
		GLSLDefineConst4f(definitions, "TexGenPlane", 1.0f/mapX, 1.0f/mapZ, scaleX/mapX, scaleZ/mapZ);
		GLSLDefineConst4f(definitions, "ShadingPlane", shadingX/mapX, shadingZ/mapZ, shadingX, shadingZ);
	}

	// LOAD SHADERS
	{
		waterShader = shaderHandler->CreateProgramObject("[BumpWater]", "WaterShader");
		waterShader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/BumpWaterVS.glsl", definitions, GL_VERTEX_SHADER));
		waterShader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/BumpWaterFS.glsl", definitions, GL_FRAGMENT_SHADER));
		using VAT = std::decay_t<decltype(rb)>::VertType;
		waterShader->BindAttribLocations<VAT>();
		waterShader->Link();

		if (!waterShader->IsValid()) {
			const char* fmt = "water-shader compilation error: %s";
			const char* log = (waterShader->GetLog()).c_str();
			LOG_L(L_ERROR, fmt, log);
			throw content_error(string("[" LOG_SECTION_BUMP_WATER "] water-shader compilation error!"));
		}

		// BIND TEXTURE UNIFORMS
		// NOTE: ATI shader validation code is stricter wrt. state,
		// so postpone the call until all texture uniforms are set
		waterShader->Enable();

		waterShader->SetUniform("normalmap"     , 0 );
		waterShader->SetUniform("heightmap"     , 1 );
		waterShader->SetUniform("caustic"       , 2 );
		waterShader->SetUniform("foam"          , 3 );
		waterShader->SetUniform("reflection"    , 4 );
		waterShader->SetUniform("refraction"    , 5 );
		waterShader->SetUniform("coastmap"      , 6 );
		waterShader->SetUniform("depthmap"      , 7 );
		waterShader->SetUniform("waverand"      , 8 );
		waterShader->SetUniform("shadowmap"     , 9 );
		waterShader->SetUniform("infotex"       , 10);
		waterShader->SetUniform("shadowColorTex", 11);
		waterShader->SetUniform("windVector"    , 15.0f, 15.0f);

		waterShader->Disable();
		waterShader->Validate();

		if (!waterShader->IsValid()) {
			const char* fmt = "water-shader validation error: %s";
			const char* log = (waterShader->GetLog()).c_str();

			LOG_L(L_ERROR, fmt, log);
			throw content_error(string("[" LOG_SECTION_BUMP_WATER "] water-shader validation error!"));
		}
	}

	rb = GenWaterPlaneBuffer(endlessOcean);
}

void CBumpWater::FreeResources()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// RAII cleanup - smart pointers handle texture deletion automatically
	refractTexture.reset();
	reflectTexture.reset();
	depthTexture.reset();
	foamTexture.reset();
	normalTexture.reset();
	normalTexture2.reset();
	coastTexture.reset();
	waveRandTexture.reset();
	caustTextures.clear();

	// RHI framebuffers cleanup
	reflectFBO.reset();
	refractFBO.reset();
	coastFBO.reset();
	dynWavesFBO.reset();

	// coastUpdateTexture: clear RHI wrapper first (non-owning), then delete raw GL texture
	coastUpdateTexture.reset();
	if (coastUpdateTextureGL > 0) {
		glDeleteTextures(1, &coastUpdateTextureGL);
		coastUpdateTextureGL = 0;
	}

	tileOffsets.clear();
	shaderHandler->ReleaseProgramObjects("[BumpWater]");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///  UPDATE
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CBumpWater::Update()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!waterRendering->forceRendering && !readMap->HasVisibleWater())
		return;

	if (dynWaves)
		UpdateDynWaves();

	if (shoreWaves) {
		if ((gs->frameNum % 10) == 0 && !heightmapUpdates.empty())
			UploadCoastline();

		if ((gs->frameNum % 10) == 5 && !coastmapAtlasRects.empty())
			UpdateCoastmap();
	}
}


void CBumpWater::UpdateWater(const CGame* game)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!waterRendering->forceRendering && !readMap->HasVisibleWater())
		return;

	if (refraction > 1) DrawRefraction(game);
	if (reflection > 0) DrawReflection(game);
	if (reflection || refraction) {
		// Unbind any bound FBO (legacy pattern for compatibility)
		if (reflectFBO) reflectFBO->Unbind();
		if (refractFBO) refractFBO->Unbind();
		globalRendering->LoadViewport();
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///  SHOREWAVES/COASTMAP
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CBumpWater::CoastAtlasRect::CoastAtlasRect(const SRectangle& rect)
{
	RECOIL_DETAILED_TRACY_ZONE;
	ix1 = std::max(rect.x1 - 15,            0);
	iy1 = std::max(rect.y1 - 15,            0);
	ix2 = std::min(rect.x2 + 15, mapDims.mapx);
	iy2 = std::min(rect.y2 + 15, mapDims.mapy);

	xsize = ix2 - ix1;
	ysize = iy2 - iy1;

	x1 = (ix1 + 0.5f) / (float)mapDims.mapx;
	x2 = (ix2 + 0.5f) / (float)mapDims.mapx;
	y1 = (iy1 + 0.5f) / (float)mapDims.mapy;
	y2 = (iy2 + 0.5f) / (float)mapDims.mapy;
	tx1 = tx2 = ty1 = ty2 = 0.0f;
	isCoastline = true;
}

void CBumpWater::UnsyncedHeightMapUpdate(const SRectangle& rect)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!shoreWaves || !readMap->HasVisibleWater())
		return;

	heightmapUpdates.push_back(rect);
}


void CBumpWater::UploadCoastline(const bool forceFull)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// optimize update area (merge overlapping areas etc.)
	heightmapUpdates.Process(forceFull);

	// limit the to be updated areas
	unsigned int currentPixels = 0;
	unsigned int numCoastRects = 0;

	// select the to be updated areas
	while (!heightmapUpdates.empty()) {
		const SRectangle& cuRect1 = heightmapUpdates.front();

		if ((currentPixels + cuRect1.GetArea() <= 512 * 512) || forceFull) {
			currentPixels += cuRect1.GetArea();
			coastmapAtlasRects.emplace_back(cuRect1);
			heightmapUpdates.pop_front();
			continue;
		}

		break;
	}


	// create a texture atlas for the to-be-updated areas
	CTextureAtlas atlas;

	const float* heightMap = (!gs->PreSimFrame()) ? readMap->GetCornerHeightMapUnsynced() : readMap->GetCornerHeightMapSynced();

	for (size_t i = 0; i < coastmapAtlasRects.size(); i++) {
		CoastAtlasRect& caRect = coastmapAtlasRects[i];

		unsigned int a = 0;
		unsigned char* texpixels = (unsigned char*) atlas.AddGetTex(IntToString(i), caRect.xsize, caRect.ysize);

		for (int y = 0; y < caRect.ysize; ++y) {
			const int yindex  = (y + caRect.iy1) * mapDims.mapxp1 + caRect.ix1;
			const int yindex2 = y * caRect.xsize;

			for (int x = 0; x < caRect.xsize; ++x) {
				const int index  = yindex + x;
				const int index2 = (yindex2 + x) << 2;
				const float& height = heightMap[index];

				texpixels[index2    ] = (height > 10.0f)? 255 : 0; // isground
				texpixels[index2 + 1] = (height >  0.0f)? 255 : 0; // coastdist
				texpixels[index2 + 2] = (height <  0.0f)? CReadMap::EncodeHeight(height) : 255; // waterdepth
				texpixels[index2 + 3] = 0;
				a += (height > 0.0f);
			}
		}

		numCoastRects += (caRect.isCoastline = (a != 0 && a != (caRect.ysize * caRect.xsize)));
	}

	// create the texture atlas only if any coastal regions exist
	if (numCoastRects == 0 || !atlas.Finalize()) {
		coastmapAtlasRects.clear();
		return;
	}

	// must happen after atlas.Finalize()
	atlas.DisOwnTexture();

	coastUpdateTextureGL = atlas.GetTexID();
	atlasX = (atlas.GetSize()).x;
	atlasY = (atlas.GetSize()).y;

	// Wrap the atlas GLuint as a non-owning RHI texture for binding/FBO attach
	coastUpdateTexture = GetRHIDevice()->WrapExistingTexture(
		coastUpdateTextureGL, RHI::TextureType::Texture2D, RHI::TextureFormat::RGBA8,
		atlasX, atlasY);

	// save the area positions in the texture atlas
	for (size_t i = 0; i < coastmapAtlasRects.size(); i++) {
		CoastAtlasRect& r = coastmapAtlasRects[i];
		const auto& tex = atlas.GetTexture(IntToString(i));
		r.tx1 = tex.xstart;
		r.tx2 = tex.xend;
		r.ty1 = tex.ystart;
		r.ty2 = tex.yend;
	}
}


void CBumpWater::UpdateCoastmap(const bool initialize)
{
	RECOIL_DETAILED_TRACY_ZONE;

	auto* device = GetRHIDevice();

	// Setup RHI pipeline state: no blending, no depth write, no depth test
	RHI::PipelineDesc pipeDesc;
	pipeDesc.blend.enabled = false;
	pipeDesc.depthStencil.depthWriteEnabled = false;
	pipeDesc.depthStencil.depthTestEnabled = false;
	RHI::ScopedPipeline scopedPipeline(device, pipeDesc);

	// Bind FBO
	coastFBO->Bind();

	// Bind coastUpdateTexture via RHI wrapper
	coastUpdateTexture->Bind(1);
	coastUpdateTexture->SetMagFilter(RHI::TextureFilter::Nearest);
	coastUpdateTexture->SetMinFilter(RHI::TextureFilter::Nearest);

	// Bind coastTexture using RHI
	coastTexture->Bind(0);
	coastTexture->SetMagFilter(RHI::TextureFilter::Nearest);
	coastTexture->SetMinFilter(RHI::TextureFilter::Nearest);

	// Create orthographic projection matrix (replaces glOrtho)
	CMatrix44f orthoMatrix = CMatrix44f::OrthoProj(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);

	auto* ctx = device->GetContext();
	ctx->SetViewport({0, 0, (float)mapDims.mapx, (float)mapDims.mapy});

	// Attach coastTexture to FBO
	coastFBO->AttachColor(coastTexture.get(), 0);

	blurShader->Enable();
	blurShader->SetUniform("args", 0, 0);
	blurShader->SetUniformMatrix4x4("u_mvpMatrix", false, orthoMatrix.m);

	uint32_t numCoastRects = 0;

	auto& rbt4 = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_T4>();

	for (const CoastAtlasRect& r : coastmapAtlasRects) {
		rbt4.AddQuadTriangles(
			{ {r.x1, r.y1, 0.0f}, { r.tx1, r.ty1, 0.0f, 0.0f } },
			{ {r.x1, r.y2, 0.0f}, { r.tx1, r.ty2, 0.0f, 1.0f } },
			{ {r.x2, r.y2, 0.0f}, { r.tx2, r.ty2, 1.0f, 1.0f } },
			{ {r.x2, r.y1, 0.0f}, { r.tx2, r.ty1, 1.0f, 0.0f } }
		);
		numCoastRects += r.isCoastline;
	}
	rbt4.DrawElements(GL_TRIANGLES);

	if (numCoastRects > 0 && atlasX > 0 && atlasY > 0) {
		for (int i = 0; i < 5; ++i) {
			// Render to coastUpdateTexture via RHI FBO attach
			coastFBO->AttachColor(coastUpdateTexture.get(), 0);
			ctx->SetViewport({0, 0, (float)atlasX, (float)atlasY});
			blurShader->SetUniform("args", 1, i * 2 + 1);

			for (const CoastAtlasRect& r : coastmapAtlasRects) {
				if (!r.isCoastline)
					continue;

				rbt4.AddQuadTriangles(
					{ { r.tx1, r.ty1, 0.0f }, { r.x1, r.y1, 0.0f, 0.0f } },
					{ { r.tx1, r.ty2, 0.0f }, { r.x1, r.y2, 0.0f, 1.0f } },
					{ { r.tx2, r.ty2, 0.0f }, { r.x2, r.y2, 1.0f, 1.0f } },
					{ { r.tx2, r.ty1, 0.0f }, { r.x2, r.y1, 1.0f, 0.0f } }
				);
			}
			rbt4.DrawElements(GL_TRIANGLES);

			// Render back to coastTexture
			coastFBO->AttachColor(coastTexture.get(), 0);
			ctx->SetViewport({0, 0, (float)mapDims.mapx, (float)mapDims.mapy});
			blurShader->SetUniform("args", 0, i * 2 + 2);

			for (const CoastAtlasRect& r : coastmapAtlasRects) {
				if (!r.isCoastline)
					continue;

				rbt4.AddQuadTriangles(
					{ { r.x1, r.y1, 0.0f }, { r.tx1, r.ty1, 0.0f, 0.0f } },
					{ { r.x1, r.y2, 0.0f }, { r.tx1, r.ty2, 0.0f, 1.0f } },
					{ { r.x2, r.y2, 0.0f }, { r.tx2, r.ty2, 1.0f, 1.0f } },
					{ { r.x2, r.y1, 0.0f }, { r.tx2, r.ty1, 1.0f, 0.0f } }
				);
			}
			rbt4.DrawElements(GL_TRIANGLES);
		}
	}

	blurShader->Disable();
	coastFBO->Detach(0);  // Detach color attachment 0
	coastFBO->Unbind();

	// Generate mipmaps using RHI
	coastTexture->SetMagFilter(RHI::TextureFilter::Linear);
	coastTexture->SetMinFilter(RHI::TextureFilter::LinearMipmapNearest);
	coastTexture->GenerateMipmaps();

	// Delete UpdateAtlas texture: clear RHI wrapper first, then raw GL ID
	coastUpdateTexture.reset();
	glDeleteTextures(1, &coastUpdateTextureGL);
	coastUpdateTextureGL = 0;
	coastmapAtlasRects.clear();

	globalRendering->LoadViewport();
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///  DYNAMIC WAVES
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CBumpWater::UpdateDynWaves(const bool initialize)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!dynWaves || !dynWavesFBO || !dynWavesFBO->IsComplete())
		return;

	const unsigned char tiles  = waterRendering->numTiles; // (numTiles <= 16)
	const unsigned char ntiles = tiles * tiles;

	const float tilesize = 1.0f / tiles;
	const int modFrameNum = (gs->frameNum + 1) % 60;

	if (modFrameNum == 0) {
		for (unsigned char i = 0; i < ntiles; ++i) {
			do {
				tileOffsets[i] = (unsigned char)(guRNG.NextFloat()*ntiles);
			} while (tileOffsets[i] == i);
		}
	}

	auto* device = GetRHIDevice();

	// Compute blend alpha before creating pipeline (value depends on frame)
	const float blendAlpha = initialize ? 1.0f : (modFrameNum + 1) / 600.0f;

	// Setup RHI pipeline state: blending with constant alpha, no depth write, no depth test
	RHI::PipelineDesc pipeDesc;
	pipeDesc.blend.enabled = true;
	pipeDesc.blend.srcColor = RHI::BlendFactor::ConstantAlpha;
	pipeDesc.blend.dstColor = RHI::BlendFactor::OneMinusConstantAlpha;
	pipeDesc.blend.srcAlpha = RHI::BlendFactor::ConstantAlpha;
	pipeDesc.blend.dstAlpha = RHI::BlendFactor::OneMinusConstantAlpha;
	pipeDesc.blend.blendColor[0] = 1.0f;
	pipeDesc.blend.blendColor[1] = 1.0f;
	pipeDesc.blend.blendColor[2] = 1.0f;
	pipeDesc.blend.blendColor[3] = blendAlpha;
	pipeDesc.depthStencil.depthWriteEnabled = false;
	pipeDesc.depthStencil.depthTestEnabled = false;
	RHI::ScopedPipeline scopedPipeline(device, pipeDesc);

	// Bind FBO
	dynWavesFBO->Bind();

	// Bind normalTexture2 as source
	normalTexture2->Bind(0);

	auto* ctx = device->GetContext();
	ctx->SetViewport({0, 0, (float)normalTextureX, (float)normalTextureY});

	auto& rb2tc = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_2DTC>();

	for (unsigned char y = 0; y < tiles; ++y) {
		for (unsigned char x = 0; x < tiles; ++x) {
			uint8_t offset = tileOffsets[y * tiles + x];
			uint8_t tx = offset % tiles;
			uint8_t ty = (offset - tx)/tiles;
			rb2tc.AddQuadTriangles(
				{ (x + 0) * tilesize, (y + 0) * tilesize, (tx + 0) * tilesize, (ty + 0) * tilesize },
				{ (x + 0) * tilesize, (y + 1) * tilesize, (tx + 0) * tilesize, (ty + 1) * tilesize },
				{ (x + 1) * tilesize, (y + 1) * tilesize, (tx + 1) * tilesize, (ty + 1) * tilesize },
				{ (x + 1) * tilesize, (y + 0) * tilesize, (tx + 1) * tilesize, (ty + 0) * tilesize }
			);
		}
	}
	auto& rbSh = rb2tc.GetShader();
	rbSh.Enable();
	rb2tc.DrawElements(GL_TRIANGLES);
	rbSh.Disable();

	globalRendering->LoadViewport();
	dynWavesFBO->Unbind();

	// Generate mipmaps using RHI
	normalTexture->GenerateMipmaps();
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///  DRAW FUNCTIONS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CBumpWater::Draw()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!waterRendering->forceRendering && !readMap->HasVisibleWater())
		return;

	auto* device = GetRHIDevice();

	if (refraction == 1 && refractTexture && refractFBO) {
		// Blit from default framebuffer to refraction FBO
		auto* ctx = device->GetContext();
		ctx->BlitFramebuffer(nullptr, refractFBO.get(),
			globalRendering->viewPosX, globalRendering->viewPosY,
			globalRendering->viewPosX + globalRendering->viewSizeX,
			globalRendering->viewPosY + globalRendering->viewSizeY,
			0, 0, screenTextureX, screenTextureY,
			true, false);
	}

	if (depthCopy && depthTexture) {
		// Screen copy for depth texture
		// Note: Depth copies may need special handling on Metal (explicit depth resolve)
		depthTexture->Bind(0);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, globalRendering->viewPosX,
		                    globalRendering->viewPosY, globalRendering->viewSizeX, globalRendering->viewSizeY);
	}

	// Setup RHI pipeline state
	RHI::PipelineDesc pipeDesc;
	pipeDesc.depthStencil.depthWriteEnabled = (refraction >= 2);
	pipeDesc.depthStencil.depthTestEnabled = true;
	pipeDesc.blend.enabled = (refraction == 0);
	if (pipeDesc.blend.enabled) {
		pipeDesc.blend.srcColor = RHI::BlendFactor::SrcAlpha;
		pipeDesc.blend.dstColor = RHI::BlendFactor::OneMinusSrcAlpha;
	}
	pipeDesc.rasterizer.polygonOffsetEnabled = true;
	pipeDesc.rasterizer.polygonOffsetFactor = 0.0f;
	pipeDesc.rasterizer.polygonOffsetUnits = 2.0f;
	pipeDesc.rasterizer.polygonMode = wireFrameMode ? RHI::PolygonMode::Line : RHI::PolygonMode::Fill;
	RHI::ScopedPipeline scopedPipeline(device, pipeDesc);

	waterShader->SetFlag("opt_shadows", (shadowHandler.ShadowsLoaded()));
	waterShader->SetFlag("opt_infotex", infoTextureHandler->IsEnabled());

	waterShader->Enable();
	waterShader->SetUniform("eyePos", camera->GetPos().x, camera->GetPos().y, camera->GetPos().z);
	waterShader->SetUniform("frame", (gs->frameNum + globalRendering->timeOffset) / 15000.0f);

	if (shadowHandler.ShadowsLoaded()) {
		waterShader->SetUniformMatrix4x4("shadowMatrix", false, shadowHandler.GetShadowMatrixRaw());

		shadowHandler.SetupShadowTexSampler(GL_TEXTURE9);
		if (auto* colorTex = shadowHandler.GetColorTexture())
			colorTex->Bind(11);
	}

	// Bind textures
	const int causticTexNum = (gs->frameNum % caustTextures.size());

	if (auto* shadeTex = readMap->GetRHITexture(MAP_BASE_SHADING_TEX))
		shadeTex->Bind(1);

	// RHI textures
	if (caustTextures[causticTexNum]) caustTextures[causticTexNum]->Bind(2);
	if (foamTexture) foamTexture->Bind(3);
	if (reflectTexture) reflectTexture->Bind(4);
	if (refractTexture) refractTexture->Bind(5);
	if (coastTexture) coastTexture->Bind(6);
	if (depthTexture) depthTexture->Bind(7);
	if (waveRandTexture) waveRandTexture->Bind(8);

	if (auto* infoTex = infoTextureHandler->GetCurrentInfoRHITexture())
		infoTex->Bind(10);

	// Bind normalTexture last at unit 0
	if (normalTexture) normalTexture->Bind(0);

	rb.DrawArrays(endlessOcean ? GL_TRIANGLE_STRIP : GL_TRIANGLES);

	waterShader->Disable();

	if (shadowHandler.ShadowsLoaded())
		shadowHandler.ResetShadowTexSamplerRaw();
}

void CBumpWater::DrawRefraction(const CGame* game)
{
	ZoneScopedN("BumpWater::DrawRefraction");

	if (!refractFBO)
		return;

	// Bind refraction FBO
	refractFBO->Bind();

	camera->Update();

	globalRendering->LoadViewport();
	const auto& sky = ISky::GetSky();

	// Clear with fog color
	auto* ctx = GetRHIDevice()->GetContext();
	ctx->ClearColor(sky->fogColor.x, sky->fogColor.y, sky->fogColor.z, 0.0f);
	ctx->Clear(true, true, false);

	const double clipPlaneEqs[2 * 4] = {
		0.0, -1.0, 0.0, 5.0, // ground
		0.0, -1.0, 0.0, 0.0, // models
	};

	const float3 oldsun = sunLighting->modelDiffuseColor;
	const float3 oldambient = sunLighting->modelAmbientColor;

	sunLighting->modelDiffuseColor *= float3(0.5f, 0.7f, 0.9f);
	sunLighting->modelAmbientColor *= float3(0.6f, 0.8f, 1.0f);

	DrawRefractions(&clipPlaneEqs[0], true, true);

	sunLighting->modelDiffuseColor = oldsun;
	sunLighting->modelAmbientColor = oldambient;
}


void CBumpWater::DrawReflection(const CGame* game)
{
	ZoneScopedN("BumpWater::DrawReflection");

	if (!reflectFBO)
		return;

	// Bind reflection FBO
	reflectFBO->Bind();

	const auto& sky = ISky::GetSky();

	// Clear with fog color
	auto* ctx = GetRHIDevice()->GetContext();
	ctx->ClearColor(sky->fogColor.x, sky->fogColor.y, sky->fogColor.z, 0.0f);
	ctx->Clear(true, true, false);

	const double clipPlaneEqs[2 * 4] = {
		0.0, 1.0, 0.0, 5.0, // ground; use d>0 to hide shoreline cracks
		0.0, 1.0, 0.0, 0.0, // models
	};

	CCamera* prvCam = CCameraHandler::GetSetActiveCamera(CCamera::CAMTYPE_UWREFL);
	CCamera* curCam = CCameraHandler::GetActiveCamera();

	{
		curCam->CopyStateReflect(prvCam);
		curCam->UpdateLoadViewport(0, 0, reflTexSize, reflTexSize);

		DrawReflections(&clipPlaneEqs[0], reflection > 1, true);
	}

	CCameraHandler::SetActiveCamera(prvCam->GetCamType());

	prvCam->Update();
	// done by caller
	// prvCam->LoadViewPort();
}
