/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * RHI Migration Status (GrassDrawer)
 *
 * MIGRATED:
 * - Dynamic state: glDepthMask -> ctx->SetDepthWriteEnabled()
 * - Dynamic state: glEnable/glDisable(GL_BLEND) -> ctx->SetBlendEnabled()
 * - Dynamic state: glBlendFunc -> ctx->SetBlendFunc()
 * - Dynamic state: glBlendFuncSeparate -> ctx->SetBlendFuncSeparate()
 * - Dynamic state: glEnable/glDisable(GL_DEPTH_TEST) -> ctx->SetDepthTestEnabled()
 * - Dynamic state: glEnable/glDisable(GL_CULL_FACE) -> ctx->SetCullFaceEnabled() (in commented DrawShadow)
 * - Dynamic state: glPolygonOffset + glEnable/glDisable(GL_POLYGON_OFFSET_FILL) -> ctx->SetPolygonOffset() (in commented DrawShadow)
 *
 * - Matrix stack: glMatrixMode, glPushMatrix, glPopMatrix, glLoadIdentity, glMultMatrixf,
 *   glRotatef, glTranslatef, glOrtho -> RHI::MatrixStack + RHI::ScopedMatrixPush
 *   [x] All transform computation on CPU via MatrixStack
 *   Remaining GL: glLoadMatrixf flush calls before draws (grass shader
 *   reads gl_ModelViewProjectionMatrix from FFP state)
 *
 * - Texture binding: specular cubemap via cubeMapHandler.GetSpecularTexture()->Bind()
 * - Texture binding: shadow color via shadowHandler.GetColorTexture()->Bind()
 *
 * - Display lists: glGenLists/glNewList/glCallList replaced with VBO/VAO (Phase 4.3)
 * - Vertex attribs: CreateGrassBladeVBO attrib setup via SetVertexLayout (Phase 5.8)
 * - Buffer/Draw: VAO/VBO/EBO → RHI IRHIBuffer + DrawIndexed (Phase 5.10)
 * - Dead code: GL headless fallback VAO/VBO/EBO removed (Phase 7.2a)
 * - Blur pass: CVertexArray → RenderBuffer + AddQuadTriangles (Phase 7.2b)
 * - FBO blit: raw glBind/glBlitFramebufferEXT → FBO::Blit wrapper (Phase 7.2c)
 *
 * REMAINING (NOT MIGRATED):
 * - FFP deprecated: GL_ALPHA_TEST, glColor4f
 * - Migrated: GL_CLIP_PLANE0 → ctx->SetClipDistanceEnabled + SetClipPlaneEquation (CreateFarTex)
 * - Shadow depth texture (SetupShadowTexSampler / ResetShadowTexSamplerRaw)
 * - FBO operations: internal FBO class GL calls (blocked on FBO → IRHIFramebuffer migration)
 * - Texture parameters: glTexParameteri, glTexEnvi
 */

#include <cmath>

#include "GrassDrawer.h"
#include "Game/Camera.h"
#include "Game/CameraHandler.h"
#include "Map/Ground.h"
#include "Map/MapInfo.h"
#include "Map/ReadMap.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Env/ISky.h"
#include "Rendering/Env/SunLighting.h"
#include "Rendering/Env/CubeMapHandler.h"
#include "Rendering/GL/myGL.h"  // transitional: GL types still needed for fixed-function
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/GL/FBO.h"
#include "Rendering/RHI/RHIBuffer.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHITexture.h"
#include "Rendering/RHI/RHITypes.h"
#include "Rendering/Map/InfoTexture/IInfoTextureHandler.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/Textures/Bitmap.h"
#include "Sim/Misc/Wind.h"
#include "System/EventHandler.h"
#include "System/GlobalRNG.h"
#include "System/SpringMath.h"
#include "System/Config/ConfigHandler.h"
#include "System/Color.h"
#include "System/Exceptions.h"
#include "System/StringUtil.h"
#include "System/Threading/ThreadPool.h"
#include "System/TimeProfiler.h"
#include "System/Log/ILog.h"
#include "System/FileSystem/FileHandler.h"

#include "System/Misc/TracyDefs.h"

CONFIG(int, GrassDetail).defaultValue(7).headlessValue(0).minimumValue(0).description("Sets how detailed the engine rendered grass will be on any given map.");

// uses a 'synced' RNG s.t. grass turfs generated from the same
// seed also share identical sequences, otherwise an unpleasant
// shimmering effect occurs when zooming
#if 0
typedef CGlobalRNG<LCG16, true> GrassRNG;
#else
typedef CGlobalRNG<PCG32, true> GrassRNG;
#endif

static constexpr float turfSize        = 20.0f;            // single turf size
static constexpr float partTurfSize    = turfSize * 1.0f;  // single turf size
static constexpr int   grassSquareSize = 4;                // mapsquares per grass square
static constexpr int   grassBlockSize  = 4;                // grass squares per grass block
static constexpr int   blockMapSize    = grassSquareSize * grassBlockSize;

static constexpr int   GSSSQ = SQUARE_SIZE * grassSquareSize;
static constexpr int   BMSSQ = SQUARE_SIZE * blockMapSize;

static GrassRNG grng;



static float GetGrassBlockCamDist(const int x, const int y, const bool square = false)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float qx = x * GSSSQ;
	const float qz = y * GSSSQ;
	const float3 mid = float3(qx, CGround::GetHeightReal(qx, qz, false), qz);
	const float3 dif = camera->GetPos() - mid;
	return (square) ? dif.SqLength() : dif.Length();
}


static const bool GrassSort(const CGrassDrawer::GrassStruct* a, const CGrassDrawer::GrassStruct* b) {
	RECOIL_DETAILED_TRACY_ZONE;
	const float distA = GetGrassBlockCamDist((a->posX + 0.5f) * grassBlockSize, (a->posZ + 0.5f) * grassBlockSize, true);
	const float distB = GetGrassBlockCamDist((b->posX + 0.5f) * grassBlockSize, (b->posZ + 0.5f) * grassBlockSize, true);
	return (distA > distB);
}

static const bool GrassSortNear(const CGrassDrawer::InviewNearGrass& a, const CGrassDrawer::InviewNearGrass& b) {
	RECOIL_DETAILED_TRACY_ZONE;
	return (a.dist > b.dist);
}



//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
/// CGrassBlockDrawer

class CGrassBlockDrawer: public CReadMap::IQuadDrawer
{
public:
	std::vector<CGrassDrawer::InviewNearGrass> inviewGrass;
	std::vector<CGrassDrawer::InviewNearGrass> inviewNearGrass;
	std::vector<CGrassDrawer::GrassStruct*>    inviewFarGrass;
	int cx, cy;
	CGrassDrawer* gd;

	void ResetState() {
		inviewGrass.clear();
		inviewNearGrass.clear();
		inviewFarGrass.clear();

		cx = 0;
		cy = 0;

		gd = nullptr;
	}

	void DrawQuad(int x, int y) {
		const float distSq = GetGrassBlockCamDist((x + 0.5f) * grassBlockSize, (y + 0.5f) * grassBlockSize, true);

		if (distSq > Square(gd->maxGrassDist))
			return;

		if (abs(x - cx) <= gd->detailedBlocks && abs(y - cy) <= gd->detailedBlocks)
			return DrawDetailQuad(x, y);

		DrawFarQuad(x, y);
	}

private:
	void DrawDetailQuad(const int x, const int y) {
		const float maxDetailedDist = gd->maxDetailedDist;

		// blocks close to the camera
		for (int y2 = y * grassBlockSize; y2 < (y + 1) * grassBlockSize; ++y2) {
			for (int x2 = x * grassBlockSize; x2 < (x + 1) * grassBlockSize; ++x2) {
				if (!gd->grassMap[y2 * mapDims.mapx / grassSquareSize + x2])
					continue;

				grng.Seed(y2 * mapDims.mapx / grassSquareSize + x2);

				const float dist  = GetGrassBlockCamDist(x2, y2, false);
				const float rdist = 1.0f + grng.NextFloat() * 0.5f;

				//TODO instead of adding grass turfs depending on their distance to the camera,
				//     there should be a fixed sized pool for mesh & billboard turfs
				//     and then we fill these pools with _preference_ for close distance turfs.
				//     So when a map has only less turfs, render them independent of the cam distance as mesh.
				//     -> see Ravaged_2
				if (dist < (maxDetailedDist + 128.f * rdist)) {
					// close grass (render as mesh)
					CGrassDrawer::InviewNearGrass iv;
					iv.dist = dist;
					iv.x = x2;
					iv.y = y2;
					inviewGrass.push_back(iv);
				}

				if (dist > maxDetailedDist) {
					// near but not close, save for later drawing
					CGrassDrawer::InviewNearGrass iv;
					iv.dist = dist;
					iv.x = x2;
					iv.y = y2;
					inviewNearGrass.push_back(iv);
				}
			}
		}
	}

	void DrawFarQuad(const int x, const int y) {
		const int curSquare = y * gd->blocksX + x;
		CGrassDrawer::GrassStruct* grass = &gd->grass[curSquare];
		grass->lastSeen = globalRendering->drawFrame;
		grass->posX = x;
		grass->posZ = y;
		inviewFarGrass.push_back(grass);
	}
};



static CGrassBlockDrawer blockDrawer;

// managed by WorldDrawer
CGrassDrawer* grassDrawer = nullptr;



CGrassDrawer::CGrassDrawer()
: CEventClient("[GrassDrawer]", 199992, false)
, blocksX(mapDims.mapx / grassSquareSize / grassBlockSize)
, blocksY(mapDims.mapy / grassSquareSize / grassBlockSize)
, grassBladeTex(nullptr)
, farTex(nullptr)
, farnearVA(2048)
, grassOff(false)
, updateBillboards(false)
, updateVisibility(false)
{
	blockDrawer.ResetState();
	grng.Seed(15);

	const int detail = configHandler->GetInt("GrassDetail");

	// load grass density from map
	{
		MapBitmapInfo grassbm;
		unsigned char* grassdata = readMap->GetInfoMap("grass", &grassbm);
		if (grassdata == nullptr) {
			LOG("[GrassDrawer] no grass info-map found for this map — grass disabled");
			grassOff = true;
			return;
		}

		if (grassbm.width != mapDims.mapx / grassSquareSize || grassbm.height != mapDims.mapy / grassSquareSize) {
			char b[128];
			SNPRINTF(b, sizeof(b), "grass-map has wrong size (%dx%d, should be %dx%d)\n",
				grassbm.width, grassbm.height, mapDims.mapx / grassSquareSize, mapDims.mapy / grassSquareSize);
			throw std::runtime_error(b);
		}

		grassMap.resize(mapDims.mapx * mapDims.mapy / (grassSquareSize * grassSquareSize));
		memcpy(grassMap.data(), grassdata, grassMap.size());
		readMap->FreeInfoMap("grass", grassdata);

		{
			int nonZero = 0;
			for (size_t k = 0; k < grassMap.size(); ++k)
				if (grassMap[k]) nonZero++;
			LOG("[GrassDrawer] grassMap: %d/%d squares have grass (%.1f%%)",
				nonZero, (int)grassMap.size(), 100.0f * nonZero / grassMap.size());
		}

		// some ATI drivers crash with grass enabled, default to disabled
		if ((detail == 0) || ((detail == 7) && globalRendering->haveAMD)) {
			LOG("[GrassDrawer] grass disabled (detail=%d, haveAMD=%d)", detail, globalRendering->haveAMD);
			grassOff = true;
			return;
		}

		// needed to create the far tex (Metal has blit via RHI, skip GLAD check)
		if (!RHI::IsMetalBackend() && !GLAD_GL_EXT_framebuffer_blit) {
			LOG("[GrassDrawer] no framebuffer blit support — grass disabled");
			grassOff = true;
			return;
		}

		LOG("[GrassDrawer] grass info-map found (%dx%d), detail=%d — initializing",
			grassbm.width, grassbm.height, detail);

	}

	// create/load blade texture
	{
		CBitmap grassBladeTexBM;
		if (!grassBladeTexBM.Load(mapInfo->grass.bladeTexName)) {
			// map didn't define a grasstex, so generate one
			grassBladeTexBM.Alloc(256, 64, 4);

			for (int a = 0; a < 16; ++a) {
				CreateGrassBladeTex(&grassBladeTexBM.GetRawMem()[a * 16 * 4]);
			}
		}
		//grassBladeTexBM.Save("blade.png", false);
		grassBladeTex = grassBladeTexBM.CreateTextureRHI();
		if (grassBladeTex) {
			grassBladeTex->SetWrapS(RHI::TextureWrap::ClampToEdge);
			grassBladeTex->SetWrapT(RHI::TextureWrap::ClampToEdge);
		}
	}

	// create shaders and finalize
	grass.resize(blocksX * blocksY);
	farnearVA.Initialize();
	ChangeDetail(detail);
	LoadGrassShaders();
	configHandler->NotifyOnChange(this, {"GrassDetail"});

	// eventclient
	autoLinkEvents = true;
	RegisterLinkedEvents(this);
	eventHandler.AddClient(this);
}

CGrassDrawer::~CGrassDrawer()
{
	eventHandler.RemoveClient(this);
	configHandler->RemoveObserver(this);

	DestroyGrassBladeVBO();
	// grassBladeTex and farTex are unique_ptr, auto-cleaned
	shaderHandler->ReleaseProgramObjects("[GrassDrawer]");
}



void CGrassDrawer::ChangeDetail(int detail) {
	RECOIL_DETAILED_TRACY_ZONE;
	// TODO: get rid of the magic constants
	const int detail_lim = std::min(3, detail);
	maxGrassDist = 800 + std::sqrt((float) detail) * 240;
	maxDetailedDist = 146 + detail * 24;
	detailedBlocks = int((maxDetailedDist + 128.f * 1.5f) / BMSSQ) + 1;
	numTurfs = 3 + int(detail_lim * 0.5f);
	strawPerTurf = std::min(50 + int(std::sqrt((float)detail_lim) * 10), mapInfo->grass.maxStrawsPerTurf);

	// recreate textures & XBOs
	CreateGrassBladeVBO();
	CreateFarTex();

	// reset  all cached blocks
	for (GrassStruct& pGS: grass) {
		ResetPos(pGS.posX, pGS.posZ);
	}
}


void CGrassDrawer::ConfigNotify(const std::string& key, const std::string& value) {
	RECOIL_DETAILED_TRACY_ZONE;
	ChangeDetail(configHandler->GetInt("GrassDetail"));
}


void CGrassDrawer::LoadGrassShaders() {
	RECOIL_DETAILED_TRACY_ZONE;
	#define sh shaderHandler
	grassShaders.resize(GRASS_PROGRAM_LAST, nullptr);

	static const std::string shaderNames[GRASS_PROGRAM_LAST] = {
		"grassNearAdvShader",
		"grassDistAdvShader",
		"grassShadGenShader"
	};
	static const std::string shaderDefines[GRASS_PROGRAM_LAST] = {
		"#define DISTANCE_NEAR\n",
		"#define DISTANCE_FAR\n",
		"#define SHADOW_GEN\n"
	};

	for (int i = 0; i < GRASS_PROGRAM_LAST; i++) {
		grassShaders[i] = sh->CreateProgramObject("[GrassDrawer]", shaderNames[i] + "GLSL");
		grassShaders[i]->AttachShaderObject(sh->CreateShaderObject("GLSL/GrassVertProg.glsl", shaderDefines[i], GL_VERTEX_SHADER));
		grassShaders[i]->AttachShaderObject(sh->CreateShaderObject("GLSL/GrassFragProg.glsl", shaderDefines[i], GL_FRAGMENT_SHADER));
		grassShaders[i]->Link();

		grassShaders[i]->Enable();
		grassShaders[i]->SetUniform("mapSizePO2", 1.0f / (mapDims.pwr2mapx * SQUARE_SIZE), 1.0f / (mapDims.pwr2mapy * SQUARE_SIZE));
		grassShaders[i]->SetUniform("mapSize",    1.0f / (mapDims.mapx     * SQUARE_SIZE), 1.0f / (mapDims.mapy     * SQUARE_SIZE));
		grassShaders[i]->SetUniform("bladeTex",        0);
		grassShaders[i]->SetUniform("grassShadingTex", 1);
		grassShaders[i]->SetUniform("shadingTex",      2);
		grassShaders[i]->SetUniform("infoMap",         3);
		grassShaders[i]->SetUniform("shadowMap",       4);
		grassShaders[i]->SetUniform("specularTex",     5);
		grassShaders[i]->SetUniform("shadowColorTex",  6);
		grassShaders[i]->SetUniform("infoTexIntensityMul", 1.0f);
		grassShaders[i]->SetUniform("groundShadowDensity", sunLighting->groundShadowDensity);
		grassShaders[i]->SetUniformMatrix4x4("shadowMatrix", false, shadowHandler.GetShadowMatrixRaw());
		grassShaders[i]->SetUniform4v("shadowParams", &shadowHandler.GetShadowParams().x);
		grassShaders[i]->Disable();
		grassShaders[i]->Validate();

		if ((grassOff = !grassShaders[i]->IsValid())) {
			LOG("[GrassDrawer] shader '%s' failed validation — grass disabled", shaderNames[i].c_str());
			break;
		}
	}

	#undef sh
}


void CGrassDrawer::EnableShader(const GrassShaderProgram type) {
	RECOIL_DETAILED_TRACY_ZONE;
	const float3 windSpeed = envResHandler.GetCurrentWindVec() * mapInfo->grass.bladeWaveScale;

	grassShader = grassShaders[type];
	grassShader->SetFlag("HAVE_INFOTEX", infoTextureHandler->IsEnabled());
	grassShader->SetFlag("HAVE_SHADOWS", shadowHandler.ShadowsLoaded());
	grassShader->Enable();

	grassShader->SetUniform("frame", gs->frameNum + globalRendering->timeOffset);
	grassShader->SetUniform3v("windSpeed", &windSpeed.x);
	grassShader->SetUniform3v("camPos",    &camera->GetPos().x);
	grassShader->SetUniform3v("camDir",    &camera->GetDir().x);
	grassShader->SetUniform3v("camUp",     &camera->GetUp().x);
	grassShader->SetUniform3v("camRight",  &camera->GetRight().x);

	grassShader->SetUniform("infoTexIntensityMul", float(infoTextureHandler->InMetalMode()) + 1.0f);
	grassShader->SetUniform("groundShadowDensity", sunLighting->groundShadowDensity);
	grassShader->SetUniformMatrix4x4("shadowMatrix", false, shadowHandler.GetShadowMatrixRaw());
	grassShader->SetUniform4v("shadowParams", &shadowHandler.GetShadowParams().x);

	grassShader->SetUniform3v("ambientLightColor",  &sunLighting->modelAmbientColor.x);
	grassShader->SetUniform3v("diffuseLightColor",  &sunLighting->modelDiffuseColor.x);
	grassShader->SetUniform3v("specularLightColor", &sunLighting->modelSpecularColor.x);
	grassShader->SetUniform3v("sunDir",             &mapInfo->light.sunDir.x);

	// Set fog uniforms (replaces FFP glFog* state, Phase 4.4)
	{
		float4 fc, fp;
		ISky::GetSky()->GetFogUniforms(fc, fp);
		grassShader->SetUniform4v("fogColor", &fc.x);
		grassShader->SetUniform4v("fogParams", &fp.x);
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

static float3 GetTurfParams(GrassRNG& rng, const int x, const int y)
{
	RECOIL_DETAILED_TRACY_ZONE;
	float3 result;
	result.x = (x + rng.NextFloat()) * GSSSQ;
	result.y = (y + rng.NextFloat()) * GSSSQ;
	result.z = rng.NextFloat() * 360.0f; // rotation
	return result;
}



void CGrassDrawer::FlushMatrices() const
{
	if (!RHI::IsMetalBackend()) {
		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf(projStack.Top());
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf(mvStack.Top());
	}

	// Update global matrix cache for RenderBuffer auto-sync (works on all backends)
	RenderBuffer::globalProjection = projStack.Top();
	RenderBuffer::globalModelView = mvStack.Top();
}


void CGrassDrawer::DrawNear(const std::vector<InviewNearGrass>& inviewGrass)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto* rhiDevice = RHI::GetDevice();
	const bool useRHI = (rhiDevice && grassBladeVB);

	// Bind RHI buffers + vertex layout once for all grass turfs
	if (useRHI) {
		auto* ctx = rhiDevice->GetContext();
		grassBladeVB->Bind();
		grassBladeIB->Bind();
		const RHI::VertexAttribute attrs[] = {
			{0, 0,                                              RHI::VertexFormat::Float3, 0},
			{1, static_cast<uint32_t>(offsetof(VA_TYPE_TN, s)), RHI::VertexFormat::Float2, 0},
			{2, static_cast<uint32_t>(offsetof(VA_TYPE_TN, n)), RHI::VertexFormat::Float3, 0},
		};
		const RHI::VertexLayout layout{attrs, 3, sizeof(VA_TYPE_TN)};
		ctx->SetVertexLayout(layout);
	}

	for (const InviewNearGrass& g: inviewGrass) {
		grng.Seed(g.y * mapDims.mapx / grassSquareSize + g.x);

//		const float distSq = GetGrassBlockCamDist(g.x, g.y, true);
		const float rdist  = 1.0f + grng.NextFloat() * 0.5f;
		const float alpha  = linearstep(maxDetailedDist, maxDetailedDist + 128.0f * rdist, g.dist);

		for (int a = 0; a < numTurfs; a++) {
			const float3& p = GetTurfParams(grng, g.x, g.y);
			float3 pos(p.x, CGround::GetHeightReal(p.x, p.y, false), p.y);

			pos.y -= CGround::GetSlope(p.x, p.y, false) * 30.0f;
			pos.y -= 2.0f * mapInfo->grass.bladeHeight * alpha;

			{
				RHI::ScopedMatrixPush mvGuard(mvStack);
				mvStack.Translate(pos.x, pos.y, pos.z)
				       .RotateY(p.z * math::DEG_TO_RAD);

				// Set per-turf matrix uniforms (replaces FFP FlushMatrices)
				grassShader->SetUniformMatrix4x4<float>("modelViewMatrix", false, mvStack.Top());
				{
					const CMatrix44f& mv = mvStack.Top();
					const float nm[9] = { mv[0], mv[1], mv[2], mv[4], mv[5], mv[6], mv[8], mv[9], mv[10] };
					grassShader->SetUniformMatrix3x3<float>("normalMatrix", false, nm);
				}

				if (useRHI) {
					rhiDevice->GetContext()->DrawIndexed(RHI::PrimitiveType::Triangles, grassBladeIndexCount);
				}
			}
		}
	}

	// Unbind RHI buffers + vertex layout
	if (useRHI) {
		auto* ctx = rhiDevice->GetContext();
		ctx->ClearVertexLayout();
		grassBladeIB->Unbind();
		grassBladeVB->Unbind();
	}
}


void CGrassDrawer::DrawBillboard(const int x, const int y, const float dist, VA_TYPE_TN* va_tn)
{
	RECOIL_DETAILED_TRACY_ZONE;
	GrassRNG trng; // need our own, this function may run threaded
	trng.Seed(y * mapDims.mapx / grassSquareSize + x);

	const float rDist = 1.0f + trng.NextFloat() * 0.5f;
	const float gStep = linearstep(maxGrassDist,  maxGrassDist + 127.0f, dist + 128.0f);
	const float dStep = linearstep(maxDetailedDist, maxDetailedDist + 128.0f * rDist, dist);
	const float alpha = std::min(1.0f - gStep, dStep);

	for (int a = 0; a < numTurfs; a++) {
		const float3& p = GetTurfParams(trng, x, y);
		const float3 pos(p.x, CGround::GetHeightReal(p.x, p.y, false) - CGround::GetSlope(p.x, p.y, false) * 30.0f, p.y);

		va_tn[a * 4 + 0] = { pos,         0.0f, 1.0f, float3(-partTurfSize, -partTurfSize, alpha) };
		va_tn[a * 4 + 1] = { pos, 1.0f / 16.0f, 1.0f, float3( partTurfSize, -partTurfSize, alpha) };
		va_tn[a * 4 + 2] = { pos, 1.0f / 16.0f, 0.0f, float3( partTurfSize,  partTurfSize, alpha) };
		va_tn[a * 4 + 3] = { pos,         0.0f, 0.0f, float3(-partTurfSize,  partTurfSize, alpha) };
	}
}


void CGrassDrawer::DrawQuadsAsTrianglesRHI(const float* vertData, unsigned int numFloats)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto* device = RHI::GetDevice();
	if (!device)
		return;

	const unsigned int numVerts = numFloats / VA_SIZE_TN;
	const unsigned int numQuads = numVerts / 4;
	if (numQuads == 0)
		return;

	const size_t vertBytes = numVerts * sizeof(VA_TYPE_TN);
	const unsigned int numIndices = numQuads * 6;

	// Ensure index buffer is large enough (quad→triangle pattern: 0,1,2, 0,2,3, ...)
	if (numQuads > farBillboardIBMaxQuads) {
		const unsigned int newMax = numQuads + 256; // headroom
		std::vector<unsigned int> indices(newMax * 6);
		for (unsigned int q = 0; q < newMax; ++q) {
			const unsigned int base = q * 4;
			indices[q * 6 + 0] = base + 0;
			indices[q * 6 + 1] = base + 1;
			indices[q * 6 + 2] = base + 2;
			indices[q * 6 + 3] = base + 0;
			indices[q * 6 + 4] = base + 2;
			indices[q * 6 + 5] = base + 3;
		}
		farBillboardIB = device->CreateBuffer(
			RHI::BufferType::Index, RHI::BufferUsage::Static,
			indices.size() * sizeof(unsigned int), indices.data());
		farBillboardIBMaxQuads = newMax;
	}

	// Create or resize vertex buffer
	if (!farBillboardVB || farBillboardVB->GetSize() < vertBytes) {
		farBillboardVB = device->CreateBuffer(
			RHI::BufferType::Vertex, RHI::BufferUsage::Stream,
			vertBytes, vertData);
	} else {
		farBillboardVB->Upload(vertData, 0, vertBytes);
	}

	auto* ctx = device->GetContext();

	farBillboardVB->Bind();
	farBillboardIB->Bind();

	const RHI::VertexAttribute attrs[] = {
		{0, 0,                                              RHI::VertexFormat::Float3, 0},
		{1, static_cast<uint32_t>(offsetof(VA_TYPE_TN, s)), RHI::VertexFormat::Float2, 0},
		{2, static_cast<uint32_t>(offsetof(VA_TYPE_TN, n)), RHI::VertexFormat::Float3, 0},
	};
	const RHI::VertexLayout layout{attrs, 3, sizeof(VA_TYPE_TN)};
	ctx->SetVertexLayout(layout);

	ctx->DrawIndexed(RHI::PrimitiveType::Triangles, numIndices);

	ctx->ClearVertexLayout();
	farBillboardIB->Unbind();
	farBillboardVB->Unbind();
}


void CGrassDrawer::DrawFarBillboards(const std::vector<GrassStruct*>& inviewFarGrass)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// update far grass blocks
	if (updateBillboards) {
		updateBillboards = false;

		for_mt(0, inviewFarGrass.size(), [&](const int i) {
			GrassStruct& g = *inviewFarGrass[i];

			if (g.lastFar == 0) {
				// TODO: VA's need to be uploaded each frame, switch to VBO's
				// force the patch-quads to be recreated
				g.lastFar = globalRendering->drawFrame;
				g.lastDist = -1.0f;
			}

			const float distSq = GetGrassBlockCamDist((g.posX + 0.5f) * grassBlockSize, (g.posZ + 0.5f) * grassBlockSize, true);

			if (distSq == g.lastDist)
				return;

			const bool inAlphaRange1 = (    distSq < Square(maxDetailedDist + 128.0f * 1.5f)) || (    distSq > Square(maxGrassDist - 128.0f));
			const bool inAlphaRange2 = (g.lastDist < Square(maxDetailedDist + 128.0f * 1.5f)) || (g.lastDist > Square(maxGrassDist - 128.0f));

			if (!inAlphaRange1 && (inAlphaRange1 == inAlphaRange2))
				return;

			g.lastDist = distSq;
			CVertexArray* va = &g.va;
			va->Initialize();

			// (4*4)*numTurfs quads
			for (int y2 = g.posZ * grassBlockSize; y2 < (g.posZ + 1) * grassBlockSize; ++y2) {
				for (int x2 = g.posX * grassBlockSize; x2 < (g.posX  + 1) * grassBlockSize; ++x2) {
					if (!grassMap[y2 * mapDims.mapx / grassSquareSize + x2])
						continue;

					const float dist = GetGrassBlockCamDist(x2, y2);
					auto* va_tn = va->GetTypedVertexArray<VA_TYPE_TN>(numTurfs * 4);
					DrawBillboard(x2, y2, dist, va_tn);
				}
			}
		});
	}

	// render far grass blocks
	const bool useRHI = RHI::IsMetalBackend();
	for (GrassStruct* g: inviewFarGrass) {
		if (useRHI) {
			DrawQuadsAsTrianglesRHI(g->va.data(), g->va.drawIndex());
		} else {
			g->va.DrawArrayTN(GL_QUADS);
		}
	}
}


void CGrassDrawer::DrawNearBillboards(const std::vector<InviewNearGrass>& inviewNearGrass)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (farnearVA.drawIndex() == 0) {
		auto* va_tn = farnearVA.GetTypedVertexArray<VA_TYPE_TN>(inviewNearGrass.size() * numTurfs * 4);

		for_mt(0, inviewNearGrass.size(), [&](const int i) {
			const InviewNearGrass& gi = inviewNearGrass[i];
			DrawBillboard(gi.x, gi.y, gi.dist, &va_tn[i * numTurfs * 4]);
		});
	}

	if (RHI::IsMetalBackend()) {
		DrawQuadsAsTrianglesRHI(farnearVA.data(), farnearVA.drawIndex());
	} else {
		farnearVA.DrawArrayTN(GL_QUADS);
	}
}


void CGrassDrawer::Update()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// grass is never drawn in any special (non-opaque) pass
	const CCamera* cam = CCameraHandler::GetCamera(CCamera::CAMTYPE_PLAYER);

	// update visible turfs
	updateVisibility |= (oldCamPos != cam->GetPos());
	updateVisibility |= (oldCamDir != cam->GetDir());

	if (updateVisibility) {
		oldCamPos = cam->GetPos();
		oldCamDir = cam->GetDir();
		lastVisibilityUpdate = globalRendering->drawFrame;

		blockDrawer.ResetState();
		blockDrawer.cx = int(cam->GetPos().x / BMSSQ);
		blockDrawer.cy = int(cam->GetPos().z / BMSSQ);
		blockDrawer.gd = this;
		readMap->GridVisibility(nullptr, &blockDrawer, maxGrassDist, blockMapSize);

		// ATI crashes w/o an error when shadows are enabled!?
       const bool shadows = (shadowHandler.ShadowsLoaded() && globalRendering->amdHacks);

		if (!shadows) {
			std::sort(blockDrawer.inviewFarGrass.begin(), blockDrawer.inviewFarGrass.end(), GrassSort);
			std::sort(blockDrawer.inviewNearGrass.begin(), blockDrawer.inviewNearGrass.end(), GrassSortNear);
			farnearVA.Initialize();
			updateBillboards = true;
		}

		updateVisibility = false;
	}

	// collect garbage
	//   originally, this deleted the billboard VA of any patch that was not drawn for 50 frames
	//   now it only resets lastFar s.t. patches are forcibly recreated when they become visible
	//   again (reusing memory)
	//   pass negative coordinates since we do not want to set updateVisibility during this step
	for (const GrassStruct& gs: grass) {
		if ((gs.lastSeen != lastVisibilityUpdate) && (gs.lastSeen < globalRendering->drawFrame - 50) && gs.lastFar != 0) {
			ResetPos(-gs.posX, -gs.posZ);
		}
	}
}


// TODO [RHI cross-cutting]: Draw uses glPushAttrib/glPopAttrib (deprecated),
// glColor4f (fixed-function), delegates to Setup/Reset state functions.
void CGrassDrawer::Draw()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (grassOff)
		return;
	// GL path: check GL texture ID; Metal: RHI texture is checked at bind time
	if (!RHI::IsMetalBackend() && !readMap->GetGrassShadingTexture())
		return;

	if (!blockDrawer.inviewGrass.empty()) {
		SetupGlStateNear();
			DrawNear(blockDrawer.inviewGrass);
		ResetGlStateNear();
	}

	// ATI crashes w/o an error when shadows are enabled!?
    const bool shadows = (shadowHandler.ShadowsLoaded() && globalRendering->amdHacks);

	if (!shadows && (!blockDrawer.inviewFarGrass.empty() || !blockDrawer.inviewNearGrass.empty())) {
		SetupGlStateFar();
			DrawFarBillboards(blockDrawer.inviewFarGrass);
			DrawNearBillboards(blockDrawer.inviewNearGrass);
		ResetGlStateFar();
	}

}


void CGrassDrawer::DrawShadow()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Grass self-shadowing disabled — doesn't look good at low density
}


// TODO [RHI cross-cutting]: SetupGlStateNear/ResetGlStateNear use raw GLuint textures
// from multiple subsystems and legacy GL state (glActiveTextureARB, glBindTexture).
void CGrassDrawer::SetupGlStateNear()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// bind textures
	{
		if (grassBladeTex) {
			grassBladeTex->Bind(0);
		}
		if (auto* grassTex = readMap->GetRHITexture(MAP_BASE_GRASS_TEX))
			grassTex->Bind(1);
		if (auto* shadeTex = readMap->GetRHITexture(MAP_BASE_SHADING_TEX))
			shadeTex->Bind(2);
		if (auto* infoTex = infoTextureHandler->GetCurrentInfoRHITexture())
			infoTex->Bind(3);
		if (auto* specTex = cubeMapHandler.GetSpecularTexture())
			specTex->Bind(5);
	}

	// bind shader
	EnableShader(GRASS_PROGRAM_NEAR);

	if (shadowHandler.ShadowsLoaded()) {
		shadowHandler.SetupShadowTexSampler(4);
		if (auto* colorTex = shadowHandler.GetColorTexture())
			colorTex->Bind(6);
	}

	projStack.Push()
	         .LoadMatrix(camera->GetProjectionMatrix())
	         .MultMatrix(camera->GetViewMatrix());
	mvStack.Push().LoadIdentity();

	// Set explicit matrix uniforms (replaces FFP FlushMatrices, Phase 4.8d)
	grassShader->SetUniformMatrix4x4<float>("projectionMatrix", false, projStack.Top());
	grassShader->SetUniformMatrix4x4<float>("modelViewMatrix", false, mvStack.Top());
	{
		const CMatrix44f& mv = mvStack.Top();
		const float nm[9] = { mv[0], mv[1], mv[2], mv[4], mv[5], mv[6], mv[8], mv[9], mv[10] };
		grassShader->SetUniformMatrix3x3<float>("normalMatrix", false, nm);
	}

	// RHI dynamic state
	auto* ctx = RHI::GetDevice()->GetContext();
	ctx->SetBlendEnabled(false);
	ctx->SetDepthWriteEnabled(true);

}


void CGrassDrawer::ResetGlStateNear()
{
	RECOIL_DETAILED_TRACY_ZONE;
	//CBaseGroundDrawer* gd = readMap->GetGroundDrawer();

	grassShader->Disable();

	if (shadowHandler.ShadowsLoaded())
		shadowHandler.ResetShadowTexSamplerRaw();

	projStack.Pop();
	mvStack.Pop();

	// RHI dynamic state
	auto* ctx = RHI::GetDevice()->GetContext();
	ctx->SetBlendEnabled(true);
}


// TODO [RHI cross-cutting]: SetupGlStateFar/ResetGlStateFar use raw GLuint textures.
void CGrassDrawer::SetupGlStateFar()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// RHI dynamic state
	auto* ctx = RHI::GetDevice()->GetContext();
	ctx->SetBlendEnabled(true);
	ctx->SetBlendFunc(RHI::BlendFactor::SrcAlpha, RHI::BlendFactor::OneMinusSrcAlpha);
	ctx->SetDepthWriteEnabled(false);

	projStack.Push()
	         .LoadMatrix(camera->GetProjectionMatrix())
	         .MultMatrix(camera->GetViewMatrix());
	mvStack.Push().LoadIdentity();

	EnableShader(GRASS_PROGRAM_DIST);

	// Set explicit matrix uniforms (replaces FFP FlushMatrices, Phase 4.8d)
	grassShader->SetUniformMatrix4x4<float>("projectionMatrix", false, projStack.Top());
	grassShader->SetUniformMatrix4x4<float>("modelViewMatrix", false, mvStack.Top());
	{
		const CMatrix44f& mv = mvStack.Top();
		const float nm[9] = { mv[0], mv[1], mv[2], mv[4], mv[5], mv[6], mv[8], mv[9], mv[10] };
		grassShader->SetUniformMatrix3x3<float>("normalMatrix", false, nm);
	}

	if (farTex) {
		farTex->Bind(0);
	}
	if (auto* grassTex = readMap->GetRHITexture(MAP_BASE_GRASS_TEX))
		grassTex->Bind(1);
	if (auto* shadeTex = readMap->GetRHITexture(MAP_BASE_SHADING_TEX))
		shadeTex->Bind(2);
	if (auto* infoTex = infoTextureHandler->GetCurrentInfoRHITexture())
		infoTex->Bind(3);

	if (shadowHandler.ShadowsLoaded()) {
		shadowHandler.SetupShadowTexSampler(4);
		if (auto* colorTex = shadowHandler.GetColorTexture())
			colorTex->Bind(6);
	}
}


void CGrassDrawer::ResetGlStateFar()
{
	RECOIL_DETAILED_TRACY_ZONE;
	grassShader->Disable();

	projStack.Pop();
	mvStack.Pop();

	if (shadowHandler.ShadowsLoaded())
		shadowHandler.ResetShadowTexSamplerRaw();

	// RHI dynamic state
	auto* ctx = RHI::GetDevice()->GetContext();
	ctx->SetDepthWriteEnabled(true);
}


void CGrassDrawer::DestroyGrassBladeVBO()
{
	grassBladeVB.reset();
	grassBladeIB.reset();
	grassBladeIndexCount = 0;
}


void CGrassDrawer::CreateGrassBladeVBO()
{
	RECOIL_DETAILED_TRACY_ZONE;
	DestroyGrassBladeVBO();

	std::vector<VA_TYPE_TN> vertices;
	std::vector<unsigned int> indices;
	grng.Seed(15);

	for (int a = 0; a < strawPerTurf; ++a) {
		// draw a single blade — same vertex generation as the old display list
		const float lngRnd = grng.NextFloat();
		const float length = mapInfo->grass.bladeHeight * (1.0f + lngRnd);
		const float maxAng = mapInfo->grass.bladeAngle * std::max(grng.NextFloat(), 1.0f - smoothstep(0.0f, 1.0f, lngRnd));

		float3 sideVect;
		sideVect.x = grng.NextFloat() - 0.5f;
		sideVect.z = grng.NextFloat() - 0.5f;
		sideVect.ANormalize();
		float3 bendVect = sideVect.cross(UpVector); // direction to bend into
		sideVect *= mapInfo->grass.bladeWidth * (-0.15f * lngRnd + 1.0f);

		const float3 basePos = grng.NextVector2D() * (turfSize - (bendVect * std::sin(maxAng) * length).Length2D());

		// select one of the 16 color shadings
		const float xtexCoord = grng.NextInt(16) / 16.0f;
		const int numSections = 2 + int(maxAng * 1.2f + length * 0.2f);

		float3 normalBend = -bendVect;

		// track base vertex for this blade's strip
		const unsigned int baseVertex = static_cast<unsigned int>(vertices.size());

		// start btm
		vertices.push_back({basePos + sideVect - float3(0.0f, 3.0f, 0.0f), xtexCoord              , 0.f, normalBend});
		vertices.push_back({basePos - sideVect - float3(0.0f, 3.0f, 0.0f), xtexCoord + (1.0f / 16), 0.f, normalBend});

		for (float h = 0.0f; h < 1.0f; h += (1.0f / numSections)) {
			const float ang = maxAng * h;
			const float3 n = (normalBend * std::cos(ang) + UpVector * std::sin(ang)).ANormalize();
			const float3 edgePos  = (UpVector * std::cos(ang) + bendVect * std::sin(ang)) * length * h;
			const float3 edgePosL = edgePos - sideVect * (1.0f - h);
			const float3 edgePosR = edgePos + sideVect * (1.0f - h);

			vertices.push_back({basePos + edgePosR, xtexCoord + (1.0f / 32) * h              , h, (n + sideVect * 0.04f).ANormalize()});
			vertices.push_back({basePos + edgePosL, xtexCoord - (1.0f / 32) * h + (1.0f / 16), h, (n - sideVect * 0.04f).ANormalize()});
		}

		// end top tip (single vertex)
		const float3 edgePos = (UpVector * std::cos(maxAng) + bendVect * std::sin(maxAng)) * length;
		const float3 n = (normalBend * std::cos(maxAng) + UpVector * std::sin(maxAng)).ANormalize();
		vertices.push_back({basePos + edgePos, xtexCoord + (1.0f / 32), 1.0f, n});

		// convert this blade's triangle strip to indexed triangles
		const unsigned int stripVertCount = static_cast<unsigned int>(vertices.size()) - baseVertex;
		for (unsigned int i = 0; i + 2 < stripVertCount; ++i) {
			if (i % 2 == 0) {
				indices.push_back(baseVertex + i);
				indices.push_back(baseVertex + i + 1);
				indices.push_back(baseVertex + i + 2);
			} else {
				indices.push_back(baseVertex + i + 1);
				indices.push_back(baseVertex + i);
				indices.push_back(baseVertex + i + 2);
			}
		}
	}

	grassBladeIndexCount = static_cast<unsigned int>(indices.size());
	if (grassBladeIndexCount == 0)
		return;

	auto* device = RHI::GetDevice();
	if (!device)
		return; // headless: no buffers needed, draw is skipped when useRHI is false

	// RHI path: create vertex and index buffers (vertex layout applied at draw time)
	grassBladeVB = device->CreateBuffer(
		RHI::BufferType::Vertex, RHI::BufferUsage::Static,
		vertices.size() * sizeof(VA_TYPE_TN), vertices.data());
	grassBladeIB = device->CreateBuffer(
		RHI::BufferType::Index, RHI::BufferUsage::Static,
		indices.size() * sizeof(unsigned int), indices.data());
}

void CGrassDrawer::CreateGrassBladeTex(unsigned char* buf)
{
	RECOIL_DETAILED_TRACY_ZONE;
	float3 redish = float3(0.95f, 0.70f, 0.4f);
	float3 col = mix(mapInfo->grass.color, redish, 0.1f * grng.NextFloat());
	col.x = std::clamp(col.x, 0.f, 1.f);
	col.y = std::clamp(col.y, 0.f, 1.f);
	col.z = std::clamp(col.z, 0.f, 1.f);

	SColor* img = reinterpret_cast<SColor*>(buf);
	for (int y=0; y<64; ++y) {
		for (int x=0; x<16; ++x) {
			const float brightness = smoothstep(-0.8f, 0.5f, y/63.0f) + ((x%2) == 0 ? 0.035f : 0.0f);
			const float3 c = col * brightness;
			img[y*256+x] = SColor(c.r, c.g, c.b, 1.0f);
		}
	}
}

// TODO [RHI cross-cutting]: CreateFarTex remaining GL:
// - FBO operations wrapped via FBO::Blit (internal GL calls blocked on FBO → IRHIFramebuffer migration)
// - FlushMatrices (grassBlade draws use FFP pipeline, no shader active)
// Migrated: viewport, clear, matrix stack, clip plane, blur pass (RenderBuffer).
void CGrassDrawer::CreateFarTex()
{
	RECOIL_DETAILED_TRACY_ZONE;
	//TODO create normalmap, too?
	const int sizeMod = 2;
	const int billboardSize = 256;
	const int numAngles = 16;
	const int texSizeX = billboardSize * numAngles;
	const int texSizeY = billboardSize;

	if (!farTex) {
		auto* device = RHI::GetDevice();
		if (device) {
			const int mipLevels = std::ceil(std::log((float)(std::max(texSizeX, texSizeY) + 1)));
			farTex = device->CreateTexture(RHI::TextureType::Texture2D, RHI::TextureFormat::RGBA8, texSizeX, texSizeY, 1, mipLevels);
			if (farTex) {
				farTex->SetMagFilter(RHI::TextureFilter::Linear);
				farTex->SetMinFilter(RHI::TextureFilter::LinearMipmapNearest);
				farTex->SetWrapS(RHI::TextureWrap::ClampToEdge);
				farTex->SetWrapT(RHI::TextureWrap::ClampToEdge);
			}
		}
	}

	FBO fboTex;
	fboTex.Bind();
	if (farTex) {
		fboTex.AttachTexture(farTex->GetNativeHandle());
	}
	fboTex.CheckStatus("GRASSDRAWER1");

	GLenum depthFormat = static_cast<GLenum>(CGlobalRendering::DepthBitsToFormat(globalRendering->supportDepthBufferBitDepth));

	FBO fbo;
	fbo.Bind();
	fbo.CreateRenderBuffer(GL_DEPTH_ATTACHMENT_EXT, depthFormat, texSizeX * sizeMod, texSizeY * sizeMod);
	fbo.CreateRenderBuffer(GL_COLOR_ATTACHMENT0_EXT, GL_RGBA8, texSizeX * sizeMod, texSizeY * sizeMod);
	fbo.CheckStatus("GRASSDRAWER2");

	if (!fboTex.IsValid() || !fbo.IsValid()) {
		if (RHI::IsMetalBackend()) {
			// Metal: far-tex billboard rendering unavailable, near grass blades still work
			farTex.reset();
			return;
		}
		grassOff = true;
		return;
	}

	mvStack.Push().LoadIdentity();
	projStack.Push();

	if (grassBladeTex) {
		grassBladeTex->Bind(0);
	}
	// RHI dynamic state
	auto* ctx = RHI::GetDevice()->GetContext();
	ctx->SetClipDistanceEnabled(0, true);
	ctx->SetBlendEnabled(false);
	ctx->SetDepthTestEnabled(true);
	ctx->SetDepthWriteEnabled(true);

	{
		ctx->SetViewport(RHI::Viewport{0.0f, 0.0f, static_cast<float>(texSizeX * sizeMod), static_cast<float>(texSizeY * sizeMod)});
		ctx->ClearColor(mapInfo->grass.color.r, mapInfo->grass.color.g, mapInfo->grass.color.b, 0.f);
		ctx->ClearDepth(1.0f);
		ctx->Clear(true, true, false);
		ctx->ClearColor(0.f, 0.f, 0.f, 0.f);
	}

	static const GLdouble eq[4] = {0.0, 1.0, 0.0, 0.0};

	// render turf from different vertical angles
	auto* rhiDevice = RHI::GetDevice();
	const bool useRHI = (rhiDevice && grassBladeVB);

	if (useRHI) {
		grassBladeVB->Bind();
		grassBladeIB->Bind();
		const RHI::VertexAttribute attrs[] = {
			{0, 0,                                              RHI::VertexFormat::Float3, 0},
			{1, static_cast<uint32_t>(offsetof(VA_TYPE_TN, s)), RHI::VertexFormat::Float2, 0},
			{2, static_cast<uint32_t>(offsetof(VA_TYPE_TN, n)), RHI::VertexFormat::Float3, 0},
		};
		const RHI::VertexLayout layout{attrs, 3, sizeof(VA_TYPE_TN)};
		ctx->SetVertexLayout(layout);
	}

	for (int a=0;a<numAngles;++a) {
		ctx->SetViewport(RHI::Viewport{static_cast<float>(a * billboardSize * sizeMod), 0.0f, static_cast<float>(billboardSize * sizeMod), static_cast<float>(billboardSize * sizeMod)});
		mvStack.LoadIdentity()
		       .RotateX(a * 90.0f / (numAngles - 1) * math::DEG_TO_RAD);
		projStack.LoadMatrix(CMatrix44f::OrthoProj(-partTurfSize, partTurfSize, partTurfSize, -partTurfSize, -turfSize, turfSize));
		FlushMatrices(); // needed: grassBlade draw uses FFP pipeline (no shader active)

		// Pre-transform clip plane by (MV^-1)^T for SetClipPlaneEquation (identity MV).
		// MV is RotateX(angle) which is orthogonal, so (MV^-1)^T = MV.
		const CMatrix44f& mv = mvStack.Top();
		const GLdouble eyeEq[4] = {
			mv[0]*eq[0] + mv[4]*eq[1] + mv[8]*eq[2]  + mv[12]*eq[3],
			mv[1]*eq[0] + mv[5]*eq[1] + mv[9]*eq[2]  + mv[13]*eq[3],
			mv[2]*eq[0] + mv[6]*eq[1] + mv[10]*eq[2] + mv[14]*eq[3],
			mv[3]*eq[0] + mv[7]*eq[1] + mv[11]*eq[2] + mv[15]*eq[3]
		};
		ctx->SetClipPlaneEquation(0, eyeEq);

		if (useRHI) {
			ctx->DrawIndexed(RHI::PrimitiveType::Triangles, grassBladeIndexCount);
		}
	}

	if (useRHI) {
		ctx->ClearVertexLayout();
		grassBladeIB->Unbind();
		grassBladeVB->Unbind();
	}

	ctx->SetClipDistanceEnabled(0, false);

	// scale down the rendered fartextures (MSAA) and write to the final texture
	FBO::Blit(fbo.fboId, fboTex.fboId,
		{0, 0, texSizeX * sizeMod, texSizeY * sizeMod},
		{0, 0, texSizeX, texSizeY},
		GL_COLOR_BUFFER_BIT, GL_LINEAR);

	// compute mipmaps
	if (farTex) {
		farTex->Bind(0);
		farTex->GenerateMipmaps();
	}

	// blur non-rendered areas, so in mipmaps color data isn't blurred with background color
	{
		const int mipLevels = std::ceil(std::log((float)(std::max(texSizeX, texSizeY) + 1)));

		mvStack.LoadIdentity();
		projStack.LoadIdentity();

		// RHI dynamic state for blending
		ctx->SetBlendEnabled(true);
		ctx->SetBlendFuncSeparate(RHI::BlendFactor::OneMinusDstAlpha, RHI::BlendFactor::DstAlpha, RHI::BlendFactor::Zero, RHI::BlendFactor::DstAlpha);

		// copy each mipmap to its predecessor background
		// -> fill background with blurred color data
		auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_T>();
		fboTex.Bind();
		for (int mipLevel = mipLevels - 2; mipLevel >= 0; --mipLevel) {
			if (farTex) {
				fboTex.AttachTexture(farTex->GetNativeHandle(), GL_TEXTURE_2D, GL_COLOR_ATTACHMENT0_EXT, mipLevel);
			}
			if (farTex) {
				farTex->SetMinLOD(mipLevel + 1.f);
				farTex->SetMaxLOD(mipLevel + 1.f);
			}
			ctx->SetViewport(RHI::Viewport{0.0f, 0.0f, static_cast<float>(texSizeX >> mipLevel), static_cast<float>(texSizeY >> mipLevel)});

			rb.AssertSubmission();
			rb.AddQuadTriangles(
				VA_TYPE_T{{-1.0f,  1.0f, 0.0f}, 0.0f, 1.0f},
				VA_TYPE_T{{ 1.0f,  1.0f, 0.0f}, 1.0f, 1.0f},
				VA_TYPE_T{{ 1.0f, -1.0f, 0.0f}, 1.0f, 0.0f},
				VA_TYPE_T{{-1.0f, -1.0f, 0.0f}, 0.0f, 0.0f}
			);
			rb.SetTransformMatrix(CMatrix44f::Identity());
			auto& sh = rb.GetShader();
			sh.Enable();
			rb.DrawArrays(GL_TRIANGLES);
			sh.Disable();
		}

		ctx->SetBlendFunc(RHI::BlendFactor::SrcAlpha, RHI::BlendFactor::OneMinusSrcAlpha);

		// recreate mipmaps from now blurred base level
		if (farTex) {
			farTex->SetMinLOD(-1000.f);
			farTex->SetMaxLOD( 1000.f);
		}
		if (farTex) {
			farTex->GenerateMipmaps();
		}
	}

	globalRendering->LoadViewport();
	projStack.Pop();
	mvStack.Pop();
	FlushMatrices();

	FBO::Unbind();
	//glSaveTexture(farTex, "grassfar.png");
}


void CGrassDrawer::ResetPos(const int grassBlockX, const int grassBlockZ)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (grassOff)
		return;

	// negative coors are passed during "garbage-collection" resets
	const int gbx = std::abs(grassBlockX);
	const int gbz = std::abs(grassBlockZ);

	assert(gbx < blocksX);
	assert(gbz < blocksY);

	grass[gbz * blocksX + gbx].lastFar = 0;

	updateBillboards = true;
	updateVisibility = (grassBlockX >= 0 && grassBlockZ >= 0);
}


void CGrassDrawer::ResetPos(const float3& pos)
{
	RECOIL_DETAILED_TRACY_ZONE;
	ResetPos(pos.x / BMSSQ, pos.z / BMSSQ);
}


void CGrassDrawer::AddGrass(const float3& pos, const uint8_t grassValue)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (grassMap.empty())
		return;

	const int x = int(pos.x) / GSSSQ;
	const int z = int(pos.z) / GSSSQ;
	assert(x >= 0 && x < (mapDims.mapx / grassSquareSize));
	assert(z >= 0 && z < (mapDims.mapy / grassSquareSize));

	grassMap[z * mapDims.mapx / grassSquareSize + x] = grassValue;
	ResetPos(pos);
}

void CGrassDrawer::RemoveGrass(const float3& pos)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (grassMap.empty())
		return;

	const int x = int(pos.x) / GSSSQ;
	const int z = int(pos.z) / GSSSQ;
	assert(x >= 0 && x < (mapDims.mapx / grassSquareSize));
	assert(z >= 0 && z < (mapDims.mapy / grassSquareSize));

	grassMap[z * mapDims.mapx / grassSquareSize + x] = 0;
	ResetPos(pos);
}

unsigned char CGrassDrawer::GetGrass(const float3& pos)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (grassMap.empty())
		return -1;

	const int x = int(pos.x) / GSSSQ;
	const int z = int(pos.z) / GSSSQ;
	assert(x >= 0 && x < (mapDims.mapx / grassSquareSize));
	assert(z >= 0 && z < (mapDims.mapy / grassSquareSize));

	return grassMap[z * mapDims.mapx / grassSquareSize + x];
}


void CGrassDrawer::UnsyncedHeightMapUpdate(const SRectangle& rect)
{
	RECOIL_DETAILED_TRACY_ZONE;
	for (int z = rect.z1; z <= rect.z2; ++z) {
		for (int x = rect.x1; x <= rect.x2; ++x) {
			ResetPos(float3(x, 0.f, z));
		}
	}
}


