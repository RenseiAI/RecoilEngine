/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#include "ProjectileDrawer.h"

#include <tuple>
#include <bit>

#include "Game/Camera.h"
#include "Game/CameraHandler.h"
#include "Game/GlobalUnsynced.h"
#include "Game/LoadScreen.h"
#include "Lua/LuaParser.h"
#include "Rendering/GroundFlash.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Units/UnitDrawer.h"
#include "Rendering/Env/ISky.h"
#include "Rendering/GL/FBO.h"
#include "Rendering/GL/SubState.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/Models/3DModelPiece.hpp"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/Textures/ColorMap.h"
#include "Rendering/Textures/TextureAtlas.h"
#include "Rendering/Common/ModelDrawerHelpers.h"
#include "Rendering/RHI/RHITypes.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIContext.h"

// RHI Migration Status (ProjectileDrawer):
//
// MIGRATED:
// - glLineWidth -> ctx->SetLineWidth()
// - glDepthMask -> ctx->SetDepthWriteEnabled()
// - glEnable/glDisable(GL_BLEND) -> ctx->SetBlendEnabled()
// - glBlendFunc -> ctx->SetBlendFunc()
// - glPolygonOffset + glEnable(GL_POLYGON_OFFSET_FILL) -> ctx->SetPolygonOffset()
// - glEnable/glDisable(GL_DEPTH_TEST) -> ctx->SetDepthTestEnabled()
// - Matrix stack: glPushMatrix/glPopMatrix, glMultMatrixf, glTranslatef3, glRotatef,
//   glMatrixMode, glLoadIdentity -> RHI::MatrixStack + RHI::ScopedMatrixPush
//   [x] DrawProjectileModel: all transform computation on CPU via mvStack member
//   [x] UpdatePerlin: projection/modelview computed on CPU via local stacks, GL flush removed
//   [x] glLoadMatrixf flush calls: Removed (shaders use uniform matrices, not FFP state)
// - Legacy FFP fog: REMOVED (glDisable(GL_FOG) was no-op with shader-based fog)
// - Perlin blend textures (8x 16x16 RGBA8):
//   [x] Init: glGenTextures + glBindTexture + glTexParameteri + glTexImage2D -> IRHIDevice::CreateTexture() + SetMin/MagFilter()
//   [x] Kill: glDeleteTextures -> unique_ptr reset
//   [x] UpdatePerlin: glBindTexture -> IRHITexture::Bind()
//   [x] GenerateNoiseTex: glBindTexture + glTexSubImage2D -> IRHITexture::Upload()
//   Changed perlinBlendTex from GLuint[8] to unique_ptr<IRHITexture>[8]
// - [x] GL_PROGRAM_POINT_SIZE: glIsEnabled query eliminated, unconditional disable/enable via RHI
// - Texture binding:
//   [x] textureAtlas: glActiveTexture + glBindTexture -> textureAtlas->GetRHITexture()->Bind(unit)
//   [x] groundFXAtlas: glActiveTexture + glBindTexture -> groundFXAtlas->GetRHITexture()->Bind(unit)
//
// REMAINING (to be migrated):
// 1. Framebuffer:
//   perlinFB (FBO) -> IRHIFramebuffer
//   perlinFB.Bind/Unbind -> IRHIContext::BeginRenderPass/EndRenderPass
//   perlinFB.AttachTexture -> IRHIFramebuffer::AttachColor
//
// 2. External texture bindings:
//   depthBufferCopy depth texture migrated to RHI via GetDepthBufferRHITexture()
#include "Sim/Misc/GlobalSynced.h"
#include "Sim/Misc/LosHandler.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/Projectiles/ExplosionGenerator.h"
#include "Sim/Projectiles/ProjectileHandler.h"
#include "Sim/Projectiles/PieceProjectile.h"
#include "Rendering/Env/Particles/Classes/FlyingPiece.h"
#include "Sim/Projectiles/WeaponProjectiles/WeaponProjectile.h"
#include "Sim/Weapons/WeaponDefHandler.h"
#include "Sim/Weapons/WeaponDef.h"
#include "System/Config/ConfigHandler.h"
#include "System/EventHandler.h"
#include "System/Exceptions.h"
#include "System/Log/ILog.h"
#include "System/SafeUtil.h"
#include "System/StringUtil.h"
#include "System/MathConstants.h"
#include "System/ScopedResource.h"

#include "System/Misc/TracyDefs.h"

CONFIG(int, SoftParticles).defaultValue(1).safemodeValue(0).description("Soften up CEG particles on clipping edges");

static uint32_t sortCamType = 0;
static bool CProjectileDrawOrderSortingPredicate(const CProjectile* p1, const CProjectile* p2) noexcept {
	return std::forward_as_tuple(p2->drawOrder, p1->GetSortDist(sortCamType), p1) > std::forward_as_tuple(p1->drawOrder, p2->GetSortDist(sortCamType), p2);
}

static bool CProjectileSortingPredicate(const CProjectile* p1, const CProjectile* p2) noexcept {
	return std::forward_as_tuple(p1->GetSortDist(sortCamType), p1) > std::forward_as_tuple(p2->GetSortDist(sortCamType), p2);
};

CProjectileDrawer* projectileDrawer = nullptr;

// can not be a CProjectileDrawer; destruction in global
// scope might happen after ~EventHandler (referenced by
// ~EventClient)
alignas(CProjectileDrawer) static std::byte projectileDrawerMem[sizeof(CProjectileDrawer)];


void CProjectileDrawer::InitStatic() {
	RECOIL_DETAILED_TRACY_ZONE;
	if (projectileDrawer == nullptr)
		projectileDrawer = new (projectileDrawerMem) CProjectileDrawer();

	projectileDrawer->Init();
}
void CProjectileDrawer::KillStatic(bool reload) {
	RECOIL_DETAILED_TRACY_ZONE;
	projectileDrawer->Kill();

	if (reload)
		return;

	spring::SafeDestruct(projectileDrawer);
	memset(projectileDrawerMem, 0, sizeof(projectileDrawerMem));
}

void CProjectileDrawer::Init() {
	RECOIL_DETAILED_TRACY_ZONE;
	eventHandler.AddClient(this);

	loadscreen->SetLoadMessage("Creating Projectile Textures");

	textureAtlas  = new CTextureAtlas(CTextureAtlas::ATLAS_ALLOC_MP_LEGACY, 0, 0, "ExplosFXAtlas", true);
	groundFXAtlas = new CTextureAtlas(CTextureAtlas::ATLAS_ALLOC_MP_LEGACY, 0, 0, "GroundFXAtlas", true);

	LuaParser resourcesParser("gamedata/resources.lua", SPRING_VFS_MOD_BASE, SPRING_VFS_ZIP);
	LuaParser mapResParser("gamedata/resources_map.lua", SPRING_VFS_MAP_BASE, SPRING_VFS_ZIP);

	resourcesParser.Execute();

	const LuaTable& resTable = resourcesParser.GetRoot();
	const LuaTable& resGraphicsTable = resTable.SubTable("graphics");
	const LuaTable& resProjTexturesTable = resGraphicsTable.SubTable("projectileTextures");
	const LuaTable& resSmokeTexturesTable = resGraphicsTable.SubTable("smoke");
	const LuaTable& resGroundFXTexturesTable = resGraphicsTable.SubTable("groundfx");

	// used to block resources_map.* from overriding any of
	// resources.lua:{projectile, smoke, groundfx}textures,
	// as well as various defaults (repulsegfxtexture, etc)
	spring::unordered_set<std::string> blockedTexNames;

	ParseAtlasTextures(true, resProjTexturesTable, blockedTexNames, textureAtlas);
	ParseAtlasTextures(true, resGroundFXTexturesTable, blockedTexNames, groundFXAtlas);

	textureAtlas->SetMaxTexLevel(4);
	groundFXAtlas->SetMaxTexLevel(4);

	int smokeTexCount = -1;

	{
		// get the smoke textures, hold the count in 'smokeTexCount'
		if (resSmokeTexturesTable.IsValid()) {
			for (smokeTexCount = 0; true; smokeTexCount++) {
				const std::string& tex = resSmokeTexturesTable.GetString(smokeTexCount + 1, "");
				if (tex.empty())
					break;

				const std::string texName = "bitmaps/" + tex;
				const std::string smokeName = "ismoke" + IntToString(smokeTexCount, "%02i");

				textureAtlas->AddTexFromFile(smokeName, texName);
				blockedTexNames.insert(StringToLower(smokeName));
			}
		} else {
			// setup the defaults
			for (smokeTexCount = 0; smokeTexCount < 12; smokeTexCount++) {
				const std::string smokeNum = IntToString(smokeTexCount, "%02i");
				const std::string smokeName = "ismoke" + smokeNum;
				const std::string texName = "bitmaps/smoke/smoke" + smokeNum + ".tga";

				textureAtlas->AddTexFromFile(smokeName, texName);
				blockedTexNames.insert(StringToLower(smokeName));
			}
		}

		if (smokeTexCount <= 0) {
			// this needs to be an exception, other code
			// assumes at least one smoke-texture exists
			throw content_error("missing smoke textures");
		}
	}

	{
		// shield-texture memory
		std::array<char, 4 * perlinTexSize * perlinTexSize> perlinTexMem;
		perlinTexMem.fill(70);
		textureAtlas->AddTexFromMem("perlintex", perlinTexSize, perlinTexSize, CTextureAtlas::RGBA32, &perlinTexMem[0]);
		blockedTexNames.insert("perlintex");
	}

	blockedTexNames.insert("flare");
	blockedTexNames.insert("explo");
	blockedTexNames.insert("explofade");
	blockedTexNames.insert("heatcloud");
	blockedTexNames.insert("laserend");
	blockedTexNames.insert("laserfalloff");
	blockedTexNames.insert("randdots");
	blockedTexNames.insert("smoketrail");
	blockedTexNames.insert("wake");
	blockedTexNames.insert("flame");

	blockedTexNames.insert("sbtrailtexture");
	blockedTexNames.insert("missiletrailtexture");
	blockedTexNames.insert("muzzleflametexture");
	blockedTexNames.insert("repulsetexture");
	blockedTexNames.insert("dguntexture");
	blockedTexNames.insert("flareprojectiletexture");
	blockedTexNames.insert("sbflaretexture");
	blockedTexNames.insert("missileflaretexture");
	blockedTexNames.insert("beamlaserflaretexture");
	blockedTexNames.insert("bubbletexture");
	blockedTexNames.insert("geosquaretexture");
	blockedTexNames.insert("gfxtexture");
	blockedTexNames.insert("projectiletexture");
	blockedTexNames.insert("repulsegfxtexture");
	blockedTexNames.insert("sphereparttexture");
	blockedTexNames.insert("torpedotexture");
	blockedTexNames.insert("wrecktexture");
	blockedTexNames.insert("plasmatexture");

	if (mapResParser.Execute()) {
		// allow map-specified atlas textures (for gaia-projectiles and ground-flashes)
		const LuaTable& mapResTable = mapResParser.GetRoot();
		const LuaTable& mapResGraphicsTable = mapResTable.SubTable("graphics");
		const LuaTable& mapResProjTexturesTable = mapResGraphicsTable.SubTable("projectileTextures");
		const LuaTable& mapResGroundFXTexturesTable = mapResGraphicsTable.SubTable("groundfx");

		ParseAtlasTextures(false, mapResProjTexturesTable, blockedTexNames, textureAtlas);
		ParseAtlasTextures(false, mapResGroundFXTexturesTable, blockedTexNames, groundFXAtlas);
	}

	if (!textureAtlas->Finalize()) {
#ifndef HEADLESS
		LOG_L(L_ERROR, "Could not finalize projectile-texture atlas. Use fewer/smaller textures.");
#endif
	}


	flaretex        = &textureAtlas->GetTexture("flare");
	explotex        = &textureAtlas->GetTexture("explo");
	explofadetex    = &textureAtlas->GetTexture("explofade");
	heatcloudtex    = &textureAtlas->GetTexture("heatcloud");
	laserendtex     = &textureAtlas->GetTexture("laserend");
	laserfallofftex = &textureAtlas->GetTexture("laserfalloff");
	randdotstex     = &textureAtlas->GetTexture("randdots");
	smoketrailtex   = &textureAtlas->GetTexture("smoketrail");
	waketex         = &textureAtlas->GetTexture("wake");
	perlintex       = &textureAtlas->GetTexture("perlintex");
	flametex        = &textureAtlas->GetTexture("flame");

	smokeTextures.reserve(smokeTexCount);

	for (int i = 0; i < smokeTexCount; i++) {
		smokeTextures.push_back(&textureAtlas->GetTexture("ismoke" + IntToString(i, "%02i")));
	}

	sbtrailtex         = &textureAtlas->GetTextureWithBackup("sbtrailtexture",         "smoketrail"    );
	missiletrailtex    = &textureAtlas->GetTextureWithBackup("missiletrailtexture",    "smoketrail"    );
	muzzleflametex     = &textureAtlas->GetTextureWithBackup("muzzleflametexture",     "explo"         );
	repulsetex         = &textureAtlas->GetTextureWithBackup("repulsetexture",         "explo"         );
	dguntex            = &textureAtlas->GetTextureWithBackup("dguntexture",            "flare"         );
	flareprojectiletex = &textureAtlas->GetTextureWithBackup("flareprojectiletexture", "flare"         );
	sbflaretex         = &textureAtlas->GetTextureWithBackup("sbflaretexture",         "flare"         );
	missileflaretex    = &textureAtlas->GetTextureWithBackup("missileflaretexture",    "flare"         );
	beamlaserflaretex  = &textureAtlas->GetTextureWithBackup("beamlaserflaretexture",  "flare"         );
	bubbletex          = &textureAtlas->GetTextureWithBackup("bubbletexture",          "circularthingy");
	geosquaretex       = &textureAtlas->GetTextureWithBackup("geosquaretexture",       "circularthingy");
	gfxtex             = &textureAtlas->GetTextureWithBackup("gfxtexture",             "circularthingy");
	projectiletex      = &textureAtlas->GetTextureWithBackup("projectiletexture",      "circularthingy");
	repulsegfxtex      = &textureAtlas->GetTextureWithBackup("repulsegfxtexture",      "circularthingy");
	sphereparttex      = &textureAtlas->GetTextureWithBackup("sphereparttexture",      "circularthingy");
	torpedotex         = &textureAtlas->GetTextureWithBackup("torpedotexture",         "circularthingy");
	wrecktex           = &textureAtlas->GetTextureWithBackup("wrecktexture",           "circularthingy");
	plasmatex          = &textureAtlas->GetTextureWithBackup("plasmatexture",          "circularthingy");


	if (!groundFXAtlas->Finalize())
		LOG_L(L_ERROR, "Could not finalize groundFX texture atlas. Use fewer/smaller textures.");

	groundflashtex = &groundFXAtlas->GetTexture("groundflash");
	groundringtex = &groundFXAtlas->GetTexture("groundring");
	seismictex = &groundFXAtlas->GetTexture("seismic");


	for (int a = 0; a < 4; ++a) {
		perlinBlend[a] = 0.0f;
	}

	{
		auto* device = RHI::GetDevice();
		for (int a = 0; a < 8; ++a) {
			perlinBlendTex[a] = device->CreateTexture(
				RHI::TextureType::Texture2D,
				RHI::TextureFormat::RGBA8,
				perlinBlendTexSize, perlinBlendTexSize);
			perlinBlendTex[a]->SetMagFilter(RHI::TextureFilter::Linear);
			perlinBlendTex[a]->SetMinFilter(RHI::TextureFilter::Linear);
		}
	}


	// ProjectileDrawer is no-op constructed, has to be initialized manually
	perlinFB.Init(false);

	if (perlinFB.IsValid()) {
		// we never refresh the full texture (just the perlin part), so reload it on AT
		perlinFB.reloadOnAltTab = true;

		perlinFB.Bind();
		perlinFB.AttachTexture(textureAtlas->GetTexID());
		drawPerlinTex = perlinFB.CheckStatus("PROJECTILE-DRAWER-PERLIN");
		perlinFB.Unbind();
	}


	renderProjectiles.reserve(projectileHandler.maxParticles + projectileHandler.maxNanoParticles);
	for (auto& mr : modelRenderers) { mr.Clear(); }

	LoadWeaponTextures();

	fxShadowShader = shaderHandler->CreateProgramObject("[ProjectileDrawer::VFS]", "FX Shader shadow");
	fxShadowShader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/ProjFXVertShadowProg.glsl", "", GL_VERTEX_SHADER));
	fxShadowShader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/ProjFXFragShadowProg.glsl", "", GL_FRAGMENT_SHADER));
	fxShadowShader->SetFlag("USE_TEXTURE_ARRAY", false);

	using VAT = std::decay_t<decltype(CProjectile::GetPrimaryRenderBuffer())>::VertType;
	fxShadowShader->BindAttribLocations<VAT>();

	fxShadowShader->Link();
	fxShadowShader->Enable();

	fxShadowShader->SetUniform("atlasTex", 0);
	fxShadowShader->SetUniform("alphaCtrl", 0.0f, 1.0f, 0.0f, 0.0f);
	fxShadowShader->SetUniform("shadowColorMode", shadowHandler.shadowColorMode > 0 ? 1.0f : 0.0f);

	fxShadowShader->Disable();
	fxShadowShader->Validate();


	fxShader = shaderHandler->CreateProgramObject("[ProjectileDrawer::VFS]", "FX Shader");
	fxShader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/ProjFXVertProg.glsl", "", GL_VERTEX_SHADER));
	fxShader->AttachShaderObject(shaderHandler->CreateShaderObject("GLSL/ProjFXFragProg.glsl", "", GL_FRAGMENT_SHADER));
	fxShader->SetFlag("SMOOTH_PARTICLES", CheckSoftenExt());
	fxShader->SetFlag("DEPTH_CLIP01", RHI::GetDevice()->SupportClipSpaceControl());
	fxShader->SetFlag("USE_TEXTURE_ARRAY", false);

	using VAT = std::decay_t<decltype(CProjectile::GetPrimaryRenderBuffer())>::VertType;
	fxShader->BindAttribLocations<VAT>();

	fxShader->Link();
	fxShader->Enable();
	fxShader->SetUniform("atlasTex", 0);
	fxShader->SetUniform("depthTex", 15);
	fxShader->SetUniform("softenExponent", softenExponent[0], softenExponent[1]);
	fxShader->SetUniform("softenThreshold", softenThreshold[0]);

	fxShader->SetUniform("camPos", 0.0f, 0.0f, 0.0f);
	fxShader->SetUniform("fogColor", 0.0f, 0.0f, 0.0f);
	fxShader->SetUniform("fogParams", 0.0f, 0.0f);

	fxShader->Disable();

	fxShader->Validate();

	sdbc = std::make_unique<ScopedDepthBufferCopy>(false);

	EnableSoften(configHandler->GetInt("SoftParticles"));
}

void CProjectileDrawer::Kill() {
	RECOIL_DETAILED_TRACY_ZONE;
	eventHandler.RemoveClient(this);
	autoLinkedEvents.clear();

	for (auto& tex : perlinBlendTex)
		tex.reset();
	spring::SafeDelete(textureAtlas);
	spring::SafeDelete(groundFXAtlas);

	smokeTextures.clear();

	renderProjectiles.clear();

	for (auto& dp : drawParticles)
		dp.clear();

	perlinFB.Kill();

	perlinTexObjects = 0;
	drawPerlinTex = false;

	drawSorted = true;

	shaderHandler->ReleaseProgramObjects("[ProjectileDrawer::VFS]");
	fxShader = nullptr;
	fxShadowShader = nullptr;
	sdbc = nullptr;

	configHandler->Set("SoftParticles", wantSoften);
}

void CProjectileDrawer::UpdateDrawFlags()
{
	ZoneScopedN("ProjectileDrawer::UpdateDrawFlags");

	for_mt(0, renderProjectiles.size(), [this](int i) {
		CProjectile* p = renderProjectiles[i];
		const bool hasModel = (p->model != nullptr);

		p->drawPos = p->GetDrawPos(globalRendering->timeOffset);

		p->previousDrawFlag = p->drawFlag;
		p->ResetDrawFlag();

		if (!CanDrawProjectile(p, p->GetAllyteamID()))
			return;

		p->SetDrawFlag(DrawFlags::SO_DRICON_FLAG); //reuse as a minimap draw indication

		for (uint32_t camType = CCamera::CAMTYPE_PLAYER; camType < CCamera::CAMTYPE_ENVMAP; ++camType) {
			if (camType == CCamera::CAMTYPE_UWREFL && !IWater::GetWater()->CanDrawReflectionPass())
				continue;

			if (camType == CCamera::CAMTYPE_SHADOW && !p->castShadow)
				continue;

			if (camType == CCamera::CAMTYPE_SHADOW && ((shadowHandler.shadowGenBits & CShadowHandler::SHADOWGEN_BIT_PROJ) == 0))
				continue;

			const CCamera* cam = CCameraHandler::GetCamera(camType);
			if (!cam->InView(p->drawPos, p->GetDrawRadius()))
				continue;

			p->SetSortDist(camType, cam->ProjectedDistance(p->drawPos));

			switch (camType)
			{
				case CCamera::CAMTYPE_PLAYER: {
					if (hasModel)
						p->AddDrawFlag(DrawFlags::SO_OPAQUE_FLAG);
					else
						p->AddDrawFlag(DrawFlags::SO_ALPHAF_FLAG);

					if (p->drawPos.y - p->GetDrawRadius() < 0.0f)
						p->AddDrawFlag(DrawFlags::SO_REFRAC_FLAG);

					// Special case of piece projectile, since it has a model and fire particle
					if (p->piece)
						p->AddDrawFlag(DrawFlags::SO_ALPHAF_FLAG);
				} break;
				case CCamera::CAMTYPE_UWREFL: {
					if (CModelDrawerHelper::ObjectVisibleReflection(p->drawPos, cam->GetPos(), p->GetDrawRadius()))
						p->AddDrawFlag(DrawFlags::SO_REFLEC_FLAG);
				} break;
				case CCamera::CAMTYPE_SHADOW: {
					if unlikely(hasModel)
						p->AddDrawFlag(DrawFlags::SO_SHOPAQ_FLAG);
					else
						p->AddDrawFlag(DrawFlags::SO_SHTRAN_FLAG);

					// Special case of piece projectile, since it has a model and fire particle
					if (p->piece)
						p->AddDrawFlag(DrawFlags::SO_SHTRAN_FLAG);
				} break;
			}
		}
	});

}

bool CProjectileDrawer::CheckSoftenExt()
{
	RECOIL_DETAILED_TRACY_ZONE;
	static bool result =
		FBO::IsSupported() &&
		GLAD_GL_EXT_framebuffer_blit; //eval once
	return result;
}

void CProjectileDrawer::ParseAtlasTextures(
	const bool blockTextures,
	const LuaTable& textureTable,
	spring::unordered_set<std::string>& blockedTextures,
	CTextureAtlas* texAtlas
) {
	RECOIL_DETAILED_TRACY_ZONE;
	std::vector<std::string> subTables;
	spring::unordered_map<std::string, std::string> texturesMap;

	textureTable.GetMap(texturesMap);
	textureTable.GetKeys(subTables);

	for (auto texturesMapIt = texturesMap.begin(); texturesMapIt != texturesMap.end(); ++texturesMapIt) {
		const std::string textureName = StringToLower(texturesMapIt->first);

		// no textures added to this atlas are allowed
		// to be overwritten later by other textures of
		// the same name
		if (blockTextures)
			blockedTextures.insert(textureName);

		if (blockTextures || (blockedTextures.find(textureName) == blockedTextures.end()))
			texAtlas->AddTexFromFile(texturesMapIt->first, "bitmaps/" + texturesMapIt->second);
	}

	texturesMap.clear();

	for (size_t i = 0; i < subTables.size(); i++) {
		const LuaTable& textureSubTable = textureTable.SubTable(subTables[i]);

		if (!textureSubTable.IsValid())
			continue;

		textureSubTable.GetMap(texturesMap);

		for (auto texturesMapIt = texturesMap.begin(); texturesMapIt != texturesMap.end(); ++texturesMapIt) {
			const std::string textureName = StringToLower(texturesMapIt->first);

			if (blockTextures)
				blockedTextures.insert(textureName);

			if (blockTextures || (blockedTextures.find(textureName) == blockedTextures.end()))
				texAtlas->AddTexFromFile(texturesMapIt->first, "bitmaps/" + texturesMapIt->second);
		}

		texturesMap.clear();
	}
}

void CProjectileDrawer::LoadWeaponTextures() {
	RECOIL_DETAILED_TRACY_ZONE;
	// post-process the synced weapon-defs to set unsynced fields
	// (this requires CWeaponDefHandler to have been initialized)
	for (WeaponDef& wd: const_cast<std::vector<WeaponDef>&>(weaponDefHandler->GetWeaponDefsVec())) {
		wd.visuals.texture1 = nullptr;
		wd.visuals.texture2 = nullptr;
		wd.visuals.texture3 = nullptr;
		wd.visuals.texture4 = nullptr;

		if (!wd.visuals.colorMapStr.empty())
			wd.visuals.colorMap = CColorMap::LoadFromDefString(wd.visuals.colorMapStr);

		if (wd.type == "Cannon") {
			wd.visuals.texture1 = plasmatex;
		} else if (wd.type == "AircraftBomb") {
			wd.visuals.texture1 = plasmatex;
		} else if (wd.type == "Shield") {
			wd.visuals.texture1 = perlintex;
		} else if (wd.type == "Flame") {
			wd.visuals.texture1 = flametex;

			if (wd.visuals.colorMap == nullptr) {
				wd.visuals.colorMap = CColorMap::LoadFromDefString(
					"1.0 1.0 1.0 0.1 "
					"0.025 0.025 0.025 0.10 "
					"0.0 0.0 0.0 0.0"
				);
			}
		} else if (wd.type == "MissileLauncher") {
			wd.visuals.texture1 = missileflaretex;
			wd.visuals.texture2 = missiletrailtex;
		} else if (wd.type == "TorpedoLauncher") {
			wd.visuals.texture1 = plasmatex;
		} else if (wd.type == "LaserCannon") {
			wd.visuals.texture1 = laserfallofftex;
			wd.visuals.texture2 = laserendtex;
		} else if (wd.type == "BeamLaser") {
			if (wd.largeBeamLaser) {
				wd.visuals.texture1 = &textureAtlas->GetTexture("largebeam");
				wd.visuals.texture2 = laserendtex;
				wd.visuals.texture3 = &textureAtlas->GetTexture("muzzleside");
				wd.visuals.texture4 = beamlaserflaretex;
			} else {
				wd.visuals.texture1 = laserfallofftex;
				wd.visuals.texture2 = laserendtex;
				wd.visuals.texture3 = beamlaserflaretex;
			}
		} else if (wd.type == "LightningCannon") {
			wd.visuals.texture1 = laserfallofftex;
		} else if (wd.type == "EmgCannon") {
			wd.visuals.texture1 = plasmatex;
		} else if (wd.type == "StarburstLauncher") {
			wd.visuals.texture1 = sbflaretex;
			wd.visuals.texture2 = sbtrailtex;
			wd.visuals.texture3 = explotex;
		} else {
			wd.visuals.texture1 = plasmatex;
			wd.visuals.texture2 = plasmatex;
		}

		// override the textures if we have specified names for them
		if (!wd.visuals.texNames[0].empty()) { wd.visuals.texture1 = &textureAtlas->GetTexture(wd.visuals.texNames[0]); }
		if (!wd.visuals.texNames[1].empty()) { wd.visuals.texture2 = &textureAtlas->GetTexture(wd.visuals.texNames[1]); }
		if (!wd.visuals.texNames[2].empty()) { wd.visuals.texture3 = &textureAtlas->GetTexture(wd.visuals.texNames[2]); }
		if (!wd.visuals.texNames[3].empty()) { wd.visuals.texture4 = &textureAtlas->GetTexture(wd.visuals.texNames[3]); }

		// trails can only be custom EG's, prefix is not required game-side
		if (!wd.visuals.ptrailExpGenTag.empty())
			wd.ptrailExplosionGeneratorID = explGenHandler.LoadCustomGeneratorID(wd.visuals.ptrailExpGenTag.c_str());

		if (!wd.visuals.impactExpGenTag.empty())
			wd.impactExplosionGeneratorID = explGenHandler.LoadGeneratorID(wd.visuals.impactExpGenTag.c_str());

		if (!wd.visuals.bounceExpGenTag.empty())
			wd.bounceExplosionGeneratorID = explGenHandler.LoadGeneratorID(wd.visuals.bounceExpGenTag.c_str());
	}
}

bool CProjectileDrawer::CanDrawProjectile(const CProjectile* pro, int allyTeam)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto& th = teamHandler;
	auto& lh = losHandler;
	return (gu->spectatingFullView || (th.IsValidAllyTeam(allyTeam) && th.Ally(allyTeam, gu->myAllyTeam)) || lh->InLos(pro, gu->myAllyTeam));
}

bool CProjectileDrawer::ShouldDrawProjectile(const CProjectile* p, uint8_t thisPassMask)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(p);

	if (p->drawFlag == 0)
		return false;

	assert(std::popcount(thisPassMask) == 1);
	return p->HasDrawFlag(static_cast<DrawFlags>(thisPassMask));
}

void CProjectileDrawer::DrawProjectilesMiniMap(const CMatrix44f* transform)
{
	ZoneScopedN("ProjectileDrawer::DrawMiniMap");

	// draw opaque first
	for (CProjectile* p : renderProjectiles) {
		if (!p->model)
			continue;

		if (!ShouldDrawProjectile(p, DrawFlags::SO_DRICON_FLAG))
			continue;

		p->DrawOnMinimap();
	}

	// draw alpha second
	for (CProjectile* p : renderProjectiles) {
		if (p->model)
			continue;

		if (!ShouldDrawProjectile(p, DrawFlags::SO_DRICON_FLAG))
			continue;

		p->DrawOnMinimap();
	}

	auto& sh = TypedRenderBuffer<VA_TYPE_C>::GetShader();

	auto* ctx = RHI::GetDevice()->GetContext();
	ctx->SetLineWidth(1.0f);

	// Note: glPointSize(1.0f); doesn't work here on AMD drivers.
	// AMD drivers draw huge circles instead of small point for some reason
	// so disable GL_PROGRAM_POINT_SIZE unconditionally for this draw
	ctx->SetProgramPointSizeEnabled(false);

	sh.Enable();
	{
		ZoneScopedN("DrawProjectilesMiniMap::MiniMapLinesRB");
		auto& linesRB = CProjectile::GetMiniMapLinesRB();
		if (transform) linesRB.SetTransformMatrix(*transform);
		linesRB.DrawArrays(GL_LINES);
	}
	{
		ZoneScopedN("DrawProjectilesMiniMap::MiniMapPointsRB");
		auto& pointsRB = CProjectile::GetMiniMapPointsRB();
		if (transform) pointsRB.SetTransformMatrix(*transform);
		pointsRB.DrawArrays(GL_POINTS);
	}
	sh.Disable();

	ctx->SetProgramPointSizeEnabled(true);
}

void CProjectileDrawer::DrawFlyingPieces(int modelType) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const FlyingPieceContainer& container = projectileHandler.flyingPieces[modelType];

	if (container.empty())
		return;

	FlyingPiece::BeginDraw();

	const FlyingPiece* last = nullptr;
	for (const FlyingPiece& fp: container) {
		const bool noLosTst = gu->spectatingFullView || teamHandler.AlliedTeams(gu->myTeam, fp.GetTeam());
		const bool inAirLos = noLosTst || losHandler->InAirLos(fp.GetPos(), gu->myAllyTeam);

		if (!inAirLos)
			continue;

		if (!camera->InView(fp.GetPos(), fp.GetRadius()))
			continue;

		fp.Draw(last);
		last = &fp;
	}

	FlyingPiece::EndDraw();
}

void CProjectileDrawer::DrawOpaque(bool drawReflection, bool drawRefraction)
{
	ZoneScopedN("ProjectileDrawer::DrawOpaque");

	using namespace GL::State;
	auto state = GL::SubState(
		Blending(GL_FALSE),
		DepthTest(GL_TRUE),
		DepthMask(GL_TRUE)
	);

	const uint8_t thisPassMask =
		(1 - (drawReflection || drawRefraction)) * DrawFlags::SO_OPAQUE_FLAG +
		(drawReflection * DrawFlags::SO_REFLEC_FLAG) +
		(drawRefraction * DrawFlags::SO_REFRAC_FLAG);

	ScopedModelDrawerImpl<CUnitDrawer> legacy(true, false);
	unitDrawer->SetupOpaqueDrawing(false);

	for (int modelType = MODELTYPE_3DO; modelType < MODELTYPE_CNT; modelType++) {
		CModelDrawerHelper::PushModelRenderState(modelType);

		const auto& mdlRenderer = modelRenderers[modelType];

		for (uint32_t i = 0, n = mdlRenderer.GetNumObjectBins(); i < n; i++) {
			if (mdlRenderer.GetObjectBin(i).empty())
				continue;

			CModelDrawerHelper::BindModelTypeTexture(modelType, mdlRenderer.GetObjectBinKey(i));

			for (CProjectile* p : mdlRenderer.GetObjectBin(i)) {
				if (!ShouldDrawProjectile(p, thisPassMask))
					continue;

				DrawProjectileModel(p);
			}

			CModelDrawerHelper::UnbindModelTypeTexture(modelType);
		}

		DrawFlyingPieces(modelType);

		CModelDrawerHelper::PopModelRenderState(modelType);
	}

	unitDrawer->ResetOpaqueDrawing(false);
}

void CProjectileDrawer::DrawAlpha(bool drawAboveWater, bool drawBelowWater, bool drawReflection, bool drawRefraction)
{
	ZoneScopedN("ProjectileDrawer::DrawAlpha");

	static constexpr std::array<float, 4> clipPlanes[] {
		{ 0.0f,  0.0f, 0.0f, 0.0f}, // never used
		{ 0.0f, -1.0f, 0.0f, 0.0f},
		{ 0.0f,  1.0f, 0.0f, 0.0f},
		{ 0.0f,  0.0f, 0.0f, 1.0f}
	};
	const auto& clipPlane = clipPlanes[1U * drawBelowWater + 2U * drawAboveWater];

	const uint8_t thisPassMask =
		(1 - (drawReflection || drawRefraction)) * DrawFlags::SO_ALPHAF_FLAG +
		(drawReflection * DrawFlags::SO_REFLEC_FLAG) +
		(drawRefraction * DrawFlags::SO_REFRAC_FLAG);

	for (auto& dp : drawParticles)
		dp.clear();

	{
		ZoneScopedN("ProjectileDrawer::DrawAlpha(DP)");
		for (CProjectile* p : renderProjectiles) {
			if (!ShouldDrawProjectile(p, thisPassMask))
				continue;

			drawParticles[drawSorted && p->drawSorted].emplace_back(p);
		}
	}

	// set static variable to facilite sorting
	sortCamType = camera->GetCamType();

	{
		ZoneScopedN("ProjectileDrawer::DrawAlpha(SO)");
		if (wantDrawOrder)
			std::sort(drawParticles[true].begin(), drawParticles[true].end(), CProjectileDrawOrderSortingPredicate);
		else
			std::sort(drawParticles[true].begin(), drawParticles[true].end(), CProjectileSortingPredicate);
	}

	{
		ZoneScopedN("ProjectileDrawer::DrawAlpha(DS)");
		for (auto p : drawParticles[ true]) {
			p->Draw();
		}
	}
	{
		ZoneScopedN("ProjectileDrawer::DrawAlpha(DU)");
		for (auto p : drawParticles[false]) {
			p->Draw();
		}
	}

	{
		ZoneScopedN("ProjectileDrawer::DrawAlpha(RR)");

		using namespace GL::State;
		auto state = GL::SubState(
			Blending(GL_TRUE),
			BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA),
			DepthTest(GL_TRUE),
			DepthMask(GL_FALSE),
			ClipDistance<0>(GL_TRUE)
		);

		eventHandler.DrawWorldPreParticles(drawAboveWater, drawBelowWater, drawReflection, drawRefraction);

		auto& rb = CExpGenSpawnable::GetPrimaryRenderBuffer();
		if (!rb.ShouldSubmit())
			return;

		const bool needSoften = (wantSoften > 0) && !drawReflection && !drawRefraction;

		textureAtlas->GetRHITexture()->Bind(0);

		if (needSoften) {
			if (auto* depthTex = depthBufferCopy->GetDepthBufferRHITexture(false))
				depthTex->Bind(15);
		}

		const auto camPlayer = CCameraHandler::GetCamera(CCamera::CAMTYPE_PLAYER);
		const auto& sky = ISky::GetSky();

		fxShader->Enable();
		fxShader->SetUniformMatrix4x4<float>("modelViewMatrix", false, camera->GetViewMatrix());
		fxShader->SetUniformMatrix4x4<float>("projectionMatrix", false, camera->GetProjectionMatrix());
		fxShader->SetFlag("SMOOTH_PARTICLES", needSoften);
		fxShader->SetFlag("USE_TEXTURE_ARRAY", (textureAtlas->GetNumPages() > 1));
		fxShader->SetUniform("clipPlane", clipPlane[0], clipPlane[1], clipPlane[2], clipPlane[3]);
		fxShader->SetUniform("alphaCtrl", 0.0f, 1.0f, 0.0f, 0.0f);
		fxShader->SetUniform("softenThreshold", CProjectileDrawer::softenThreshold[0]);

		fxShader->SetUniform("camPos", camPlayer->pos.x, camPlayer->pos.y, camPlayer->pos.z);
		fxShader->SetUniform("fogColor", sky->fogColor.x, sky->fogColor.y, sky->fogColor.z);
		fxShader->SetUniform("fogParams", sky->fogStart * camPlayer->GetFarPlaneDist(), sky->fogEnd * camPlayer->GetFarPlaneDist());

		rb.DrawElements(GL_TRIANGLES);

		fxShader->Disable();

		if (needSoften) {
			if (auto* depthTex = depthBufferCopy->GetDepthBufferRHITexture(false))
				depthTex->Unbind(15);
		}
	}
}

void CProjectileDrawer::DrawShadowOpaque()
{
	ZoneScopedN("ProjectileDrawer::DrawShadowOpaque");
	Shader::IProgramObject* po = shadowHandler.GetShadowGenProg(CShadowHandler::SHADOWGEN_PROGRAM_PROJECTILE);

	po->Enable();

	for (int modelType = MODELTYPE_3DO; modelType < MODELTYPE_CNT; modelType++) {
		CModelDrawerHelper::PushModelRenderState(modelType);

		const auto& mdlRenderer = modelRenderers[modelType];

		for (uint32_t i = 0, n = mdlRenderer.GetNumObjectBins(); i < n; i++) {
			if (mdlRenderer.GetObjectBin(i).empty())
				continue;

			CModelDrawerHelper::BindModelTypeTexture(modelType, mdlRenderer.GetObjectBinKey(i));

			for (CProjectile* p : mdlRenderer.GetObjectBin(i)) {
				if (!ShouldDrawProjectile(p, DrawFlags::SO_SHOPAQ_FLAG))
					continue;

				DrawProjectileModel(p);
			}

			CModelDrawerHelper::UnbindModelTypeTexture(modelType);
		}

		DrawFlyingPieces(modelType);

		CModelDrawerHelper::PopModelRenderState(modelType);
	}

	po->Disable();
}

void CProjectileDrawer::DrawShadowTransparent()
{
	ZoneScopedN("ProjectileDrawer::DrawShadowTransparent");
	// Method #1 here: https://wickedengine.net/2018/01/18/easy-transparent-shadow-maps/

	// 1) Render opaque objects into depth stencil texture from light's point of view - done elsewhere

	// draw the model-less projectiles
	for (CProjectile* p : renderProjectiles) {
		if (!ShouldDrawProjectile(p, DrawFlags::SO_SHTRAN_FLAG))
			continue;

		p->Draw();
	}

	auto& rb = CExpGenSpawnable::GetPrimaryRenderBuffer();
	if (!rb.ShouldSubmit())
		return;

	// 2) Bind render target for shadow color filter: R11G11B10 works good
	shadowHandler.EnableColorOutput(true);

	// 3) Clear render target to 1,1,1,0 (RGBA) color - done elsewhere

	// 4) Apply depth stencil state with depth read, but no write
	//glEnable(GL_DEPTH_TEST);
	//glDepthMask(GL_FALSE);

	using namespace GL::State;
	auto state = GL::SubState(
		DepthTest(GL_TRUE),
		DepthMask(GL_FALSE),
		Blending(GL_TRUE),
		BlendFunc(GL_ZERO, GL_SRC_COLOR)
	);

	// 6) Render transparents in arbitrary order
	textureAtlas->GetRHITexture()->Bind(0);

	fxShadowShader->Enable();
	fxShadowShader->SetUniformMatrix4x4<float>("modelViewMatrix", false, camera->GetViewMatrix());
	fxShadowShader->SetUniformMatrix4x4<float>("projectionMatrix", false, camera->GetProjectionMatrix());
	fxShadowShader->SetFlag("USE_TEXTURE_ARRAY", (textureAtlas->GetNumPages() > 1));
	fxShadowShader->SetUniform("shadowColorMode", shadowHandler.shadowColorMode > 0 ? 1.0f : 0.0f);

	rb.DrawElements(GL_TRIANGLES);

	fxShadowShader->Disable();

	//shadowHandler.EnableColorOutput(false);
}



void CProjectileDrawer::DrawProjectileModel(const CProjectile* p)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(p->model);

	switch ((p->weapon * 2) + (p->piece * 1)) {
		case 2: {
			// weapon-projectile
			const CWeaponProjectile* wp = static_cast<const CWeaponProjectile*>(p);

			CUnitDrawer::SetTeamColor(wp->GetTeamID());

			{
				RHI::ScopedMatrixPush mvGuard(mvStack);
				mvStack.MultMatrix(wp->GetTransformMatrix(wp->GetProjectileType() == WEAPON_MISSILE_PROJECTILE));
				// Matrix is flushed to shader uniforms by model drawing code

				if (!p->luaDraw || !eventHandler.DrawProjectile(p))
					wp->model->DrawStatic();
			}
			return;
		} break;

		case 1: {
			// piece-projectile
			const CPieceProjectile* pp = static_cast<const CPieceProjectile*>(p);

			CUnitDrawer::SetTeamColor(pp->GetTeamID());

			{
				RHI::ScopedMatrixPush mvGuard(mvStack);
				mvStack.Translate(pp->drawPos);
				const auto [normAxis, len] = pp->spinVec.GetNormalized();
				if (len > float3::nrm_eps())
					mvStack.Rotate(pp->GetDrawAngle() * math::DEG_TO_RAD, normAxis);
				// Matrix is flushed to shader uniforms by model drawing code

				if (p->luaDraw && eventHandler.DrawProjectile(p))
					return;

				if ((pp->explFlags & PF_Recursive) != 0) {
					pp->omp->DrawStaticLegacyRec();
				}
				else {
					// non-recursive, only draw one piece
					pp->omp->DrawStaticLegacy(true, false);
				}
			}

			return;
		} break;

		default: {
		} break;
	}
}

void CProjectileDrawer::DrawGroundFlashes()
{
	RECOIL_DETAILED_TRACY_ZONE;
	const GroundFlashContainer& gfc = projectileHandler.groundFlashes;

	if (gfc.empty())
		return;

	auto* ctx = RHI::GetDevice()->GetContext();
	ctx->SetDepthWriteEnabled(false);
	ctx->SetBlendEnabled(true);
	ctx->SetBlendFunc(RHI::BlendFactor::SrcAlpha, RHI::BlendFactor::One);

	groundFXAtlas->GetRHITexture()->Bind(0);
	ctx->SetPolygonOffset(true, -20.0f, -1000.0f);

	bool depthTest = true;
	bool depthMask = false;

	const bool needSoften = (wantSoften > 0);

	auto& rb = CExpGenSpawnable::GetPrimaryRenderBuffer();

	if (needSoften) {
		if (auto* depthTex = depthBufferCopy->GetDepthBufferRHITexture(false))
			depthTex->Bind(15);
	}

	const auto camPlayer = CCameraHandler::GetCamera(CCamera::CAMTYPE_PLAYER);
	const auto& sky = ISky::GetSky();

	fxShader->Enable();
	fxShader->SetUniformMatrix4x4<float>("modelViewMatrix", false, camera->GetViewMatrix());
	fxShader->SetUniformMatrix4x4<float>("projectionMatrix", false, camera->GetProjectionMatrix());
	fxShader->SetFlag("USE_TEXTURE_ARRAY", (groundFXAtlas->GetNumPages() > 1));
	fxShader->SetUniform("alphaCtrl", 0.01f, 1.0f, 0.0f, 0.0f);
	fxShader->SetUniform("softenThreshold", -CProjectileDrawer::softenThreshold[1]);
	fxShader->SetUniform("camPos", camPlayer->pos.x, camPlayer->pos.y, camPlayer->pos.z);
	fxShader->SetUniform("fogColor", sky->fogColor.x, sky->fogColor.y, sky->fogColor.z);
	fxShader->SetUniform("fogParams", sky->fogStart * camPlayer->GetFarPlaneDist(), sky->fogEnd * camPlayer->GetFarPlaneDist());

	for (CGroundFlash* gf: gfc) {
		const bool inLos = gf->alwaysVisible || gu->spectatingFullView || losHandler->InAirLos(gf, gu->myAllyTeam);
		if (!inLos)
			continue;

		if (!camera->InView(gf->pos, gf->size))
			continue;

		bool depthTestWanted = needSoften ? false : gf->depthTest;

		if (depthTest != depthTestWanted || depthMask != gf->depthMask) {
			rb.DrawElements(GL_TRIANGLES);

			depthTest = depthTestWanted;
			ctx->SetDepthTestEnabled(depthTest);

			depthMask = gf->depthMask;
			ctx->SetDepthWriteEnabled(depthMask);
		}

		gf->Draw();
	}

	rb.DrawElements(GL_TRIANGLES);

	fxShader->Disable();

	if (needSoften) {
		if (auto* depthTex = depthBufferCopy->GetDepthBufferRHITexture(false))
			depthTex->Unbind(15);
	}

	ctx->SetPolygonOffset(false);
	ctx->SetBlendFunc(RHI::BlendFactor::SrcAlpha, RHI::BlendFactor::OneMinusSrcAlpha);
	ctx->SetBlendEnabled(false);
	ctx->SetDepthTestEnabled(true);
	ctx->SetDepthWriteEnabled(true);
}



void CProjectileDrawer::UpdateTextures() {
	RECOIL_DETAILED_TRACY_ZONE;
	if (perlinTexObjects > 0 && drawPerlinTex)
		UpdatePerlin();
}

void CProjectileDrawer::UpdatePerlin() {
	RECOIL_DETAILED_TRACY_ZONE;
	auto* ctx = RHI::GetDevice()->GetContext();

	perlinFB.Bind();
	ctx->SetViewport({
		static_cast<float>(perlintex->xstart * (textureAtlas->GetSize()).x),
		static_cast<float>(perlintex->ystart * (textureAtlas->GetSize()).y),
		static_cast<float>(perlinTexSize),
		static_cast<float>(perlinTexSize)
	});

	// CPU-side matrices for ortho projection + identity modelview
	// (Shader reads uniforms, not FFP state)
	RHI::MatrixStack projStack(CMatrix44f::ClipOrthoProj01());
	RHI::MatrixStack perlinMVStack; // identity

	ctx->SetDepthTestEnabled(false);
	ctx->SetDepthWriteEnabled(false);
	ctx->SetBlendEnabled(true);
	ctx->SetBlendFunc(RHI::BlendFactor::One, RHI::BlendFactor::One);

	unsigned char col[4];
	float time = globalRendering->lastFrameTime * gs->speedFactor * 0.003f;
	float speed = 1.0f;
	float size = 1.0f;

	auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_TC>();
	rb.AssertSubmission();

	auto& sh = rb.GetShader();
	sh.Enable();
	sh.SetUniform("alphaCtrl", 0.0f, 0.0f, 0.0f, 1.0f); // no test
	sh.Disable();

	for (int a = 0; a < 4; ++a) {
		perlinBlend[a] += time * speed;
		if (perlinBlend[a] > 1) {
			std::swap(perlinBlendTex[a * 2], perlinBlendTex[a * 2 + 1]);

			GenerateNoiseTex(perlinBlendTex[a * 2 + 1].get());
			perlinBlend[a] -= 1;
		}

		float tsize = 8.0f / size;

		if (a == 0)
			ctx->SetBlendEnabled(false);

		for (int b = 0; b < 4; ++b)
			col[b] = int((1.0f - perlinBlend[a]) * 16 * size);

		perlinBlendTex[a * 2]->Bind(0);

		rb.AddQuadTriangles(
			{ ZeroVector, 0,         0, col },
			{   UpVector, 0,     tsize, col },
			{   XYVector, tsize, tsize, col },
			{  RgtVector, tsize,     0, col }
		);
		sh.Enable();
		rb.DrawElements(GL_TRIANGLES);
		sh.Disable();

		if (a == 0)
			ctx->SetBlendEnabled(true);

		for (int b = 0; b < 4; ++b)
			col[b] = int(perlinBlend[a] * 16 * size);

		perlinBlendTex[a * 2 + 1]->Bind(0);

		rb.AddQuadTriangles(
			{ ZeroVector,     0,     0, col },
			{ UpVector  ,     0, tsize, col },
			{ XYVector  , tsize, tsize, col },
			{ RgtVector , tsize,     0, col }
		);
		sh.Enable();
		rb.DrawElements(GL_TRIANGLES);
		sh.Disable();

		speed *= 0.6f;
		size *= 2;
	}

	perlinFB.Unbind();
	globalRendering->LoadViewport();

	ctx->SetBlendFunc(RHI::BlendFactor::SrcAlpha, RHI::BlendFactor::OneMinusSrcAlpha);
	ctx->SetDepthTestEnabled(true);
	ctx->SetDepthWriteEnabled(true);
}

void CProjectileDrawer::GenerateNoiseTex(RHI::IRHITexture* tex)
{
	RECOIL_DETAILED_TRACY_ZONE;
	std::array<unsigned char, 4 * perlinBlendTexSize * perlinBlendTexSize> mem;

	for (int a = 0; a < perlinBlendTexSize * perlinBlendTexSize; ++a) {
		const unsigned char rnd = int(std::max(0.0f, guRNG.NextFloat() * 555.0f - 300.0f));

		mem[a * 4 + 0] = rnd;
		mem[a * 4 + 1] = rnd;
		mem[a * 4 + 2] = rnd;
		mem[a * 4 + 3] = rnd;
	}

	tex->Upload(0, 0, 0, perlinBlendTexSize, perlinBlendTexSize, mem.data());
}



void CProjectileDrawer::RenderProjectileCreated(const CProjectile* p)
{
	RECOIL_DETAILED_TRACY_ZONE;
	{
		const_cast<CProjectile*>(p)->SetRenderIndex(renderProjectiles.size());
		renderProjectiles.push_back(const_cast<CProjectile*>(p));
	}

	if (p->model != nullptr)
		modelRenderers[MDL_TYPE(p)].AddObject(p);
}

void CProjectileDrawer::RenderProjectileDestroyed(const CProjectile* p)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto ri = p->GetRenderIndex();
	if (ri >= renderProjectiles.size()) {
		assert(false);
		return;
	}

	renderProjectiles[ri] = renderProjectiles.back();
	renderProjectiles[ri]->SetRenderIndex(ri);
	renderProjectiles.pop_back();

	if (p->model != nullptr)
		modelRenderers[MDL_TYPE(p)].DelObject(p);
}

