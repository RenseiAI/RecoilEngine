/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * World Drawer - Implementation
 *
 * RHI Migration Status: PARTIAL (~15 GL state calls migrated, ~10 marked RHI_TODO)
 * ---------------------------------------------------------------------------
 * Migrated:
 *   - glClearColor/glClear -> ctx->ClearColor()/Clear()
 *   - glDepthMask -> ctx->SetDepthWriteEnabled()
 *   - glEnable/glDisable(GL_DEPTH_TEST) -> ctx->SetDepthTestEnabled()
 *   - glEnable/glDisable(GL_BLEND) -> ctx->SetBlendEnabled()
 *   - glBlendFunc -> ctx->SetBlendFunc()
 *   - glDepthFunc -> ctx->SetDepthFunc()
 *   - DrawBelowWaterOverlay: FFP client arrays -> TypedRenderBuffer<VA_TYPE_C>
 *     (glEnableClientState/glVertexPointer/glDrawArrays/glColor4f removed)
 *
 * Remaining (marked RHI_TODO, blocked on infrastructure):
 *   - FFP matrix stack (glMatrixMode/glPushMatrix/glPopMatrix/glLoadIdentity/gluOrtho2D)
 *     in ResetMVPMatrices() - modern path uses uniform buffers; legacy GLSL needs FFP state
 *   - FFP clip planes (glClipPlane/glEnable/glDisable(GL_CLIP_PLANE3))
 *     in DrawAlphaObjects() - modern path uses gl_ClipDistance[] in shaders; legacy needs FFP
 */

#include "Rendering/GL/myGL.h"

#include "WorldDrawer.h"
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Sim/Units/UnitDefHandler.h"
#include "Sim/Features/FeatureDefHandler.h"
#include "Sim/Weapons/WeaponDefHandler.h"
#include "Rendering/Env/CubeMapHandler.h"
#include "Rendering/Env/GrassDrawer.h"
#include "Rendering/Env/IGroundDecalDrawer.h"
#include "Rendering/Env/ISky.h"
#include "Rendering/Env/SunLighting.h"
#include "Rendering/Env/WaterRendering.h"
#include "Rendering/Env/MapRendering.h"
#include "Rendering/Env/IWater.h"
#include "Rendering/CommandDrawer.h"
#include "Rendering/DebugColVolDrawer.h"
#include "Rendering/DebugVisibilityDrawer.h"
#include "Rendering/LineDrawer.h"
#include "Rendering/LuaObjectDrawer.h"
#include "Rendering/Features/FeatureDrawer.h"
#include "Rendering/Env/Particles/ProjectileDrawer.h"
#include "Rendering/Units/UnitDrawer.h"
#include "Rendering/IPathDrawer.h"
#include "Rendering/DepthBufferCopy.h"
#include "Rendering/SmoothHeightMeshDrawer.h"
#include "Rendering/InMapDrawView.h"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Map/InfoTexture/IInfoTextureHandler.h"
#include "Rendering/Models/IModelParser.h"
#include "Rendering/Models/3DModelVAO.hpp"
#include "Rendering/Models/ModelsLock.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "Rendering/Textures/ColorMap.h"
#include "Rendering/Textures/3DOTextureHandler.h"
#include "Rendering/Textures/S3OTextureHandler.h"
#include "Map/BaseGroundDrawer.h"
#include "Map/ReadMap.h"
#include "Game/Camera.h"
#include "Game/SelectedUnitsHandler.h"
#include "Game/Game.h"
#include "Game/GlobalUnsynced.h"
#include "Game/LoadScreen.h"
#include "Game/UI/CommandColors.h"
#include "Game/UI/GuiHandler.h"
#include "System/EventHandler.h"
#include "System/Exceptions.h"
#include "System/TimeProfiler.h"
#include "System/SafeUtil.h"
#include "System/Log/ILog.h"
#include "System/Config/ConfigHandler.h"
#include "System/LoadLock.h"

CONFIG(bool, PreloadModels).defaultValue(true).description("The engine will preload all models");

void CWorldDrawer::InitPre() const
{
	LuaObjectDrawer::Init();

	CColorMap::InitStatic();

	// these need to be loaded before featureHandler is created
	// (maps with features have their models loaded at startup)
	S3DModelVAO::Init();
	modelLoader.Init();

	loadscreen->SetLoadMessage("Creating Unit Textures");
	textureHandler3DO.Init();
	textureHandlerS3O.Init();

	loadscreen->SetLoadMessage("Creating Sky");

	ISky::SetSky();
	sunLighting->Init();

	CFeatureDrawer::InitStatic();
}

void CWorldDrawer::InitPost() const
{
	char buf[512] = {0};

	CModelsLock::SetThreadSafety(true);
	const bool preloadMode = configHandler->GetBool("PreloadModels");
	{
		loadscreen->SetLoadMessage("Loading Models");

		if (preloadMode) {
			for (const auto& def : unitDefHandler->GetUnitDefsVec()) {
				def.PreloadModel();
			}

			for (const auto& def : featureDefHandler->GetFeatureDefsVec()) {
				def.PreloadModel();
			}

			for (const auto& def : weaponDefHandler->GetWeaponDefsVec()) {
				def.PreloadModel();
			}
		}
	}
	auto lock = CLoadLock::GetUniqueLock();
	{
		loadscreen->SetLoadMessage("Creating ShadowHandler");
		shadowHandler.Init();
	}
	{
		// SMFGroundDrawer accesses InfoTextureHandler, create it first
		loadscreen->SetLoadMessage("Creating InfoTextureHandler");
		IInfoTextureHandler::Create();
	}
	try {
		loadscreen->SetLoadMessage("Creating GroundDrawer");
		readMap->InitGroundDrawer();
	} catch (const content_error& e) {
		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf), "[WorldDrawer::%s] caught exception \"%s\"", __func__, e.what());
	}

	{
		loadscreen->SetLoadMessage("Creating GrassDrawer");
		grassDrawer = new CGrassDrawer();
	}
	{
		inMapDrawerView = new CInMapDrawView();
		pathDrawer = IPathDrawer::GetInstance();
	}
	{
		DepthBufferCopy::Init();
	}
	{
		IGroundDecalDrawer::Init();
	}
	{
		loadscreen->SetLoadMessage("Creating ProjectileDrawer & UnitDrawer");

		CProjectileDrawer::InitStatic();
		CUnitDrawer::InitStatic();
		// see ::InitPre
		// CFeatureDrawer::InitStatic();
	}

	// rethrow to force exit
	if (buf[0] != 0)
		throw content_error(buf);

	{
		loadscreen->SetLoadMessage("Creating Water");
		IWater::SetWater(-1);
	}
	{
		ISky::GetSky()->SetupFog();
	}
	lock = {}; //unlock
	{
		loadscreen->SetLoadMessage("Finalizing Models");
		modelLoader.DrainPreloadFutures(0);
		auto& mv = S3DModelVAO::GetInstance();
		if (preloadMode) {
			{
				auto lock = CLoadLock::GetUniqueLock();
				mv.UploadVBOs();
			}
			mv.SetSafeToDeleteVectors();
			modelLoader.LogErrors();
			CModelsLock::SetThreadSafety(false); //all models are already preloaded
		}
	}
}


void CWorldDrawer::Kill()
{
	infoTextureHandler = nullptr;

	IWater::KillWater();
	ISky::KillSky();
	spring::SafeDelete(grassDrawer);
	spring::SafeDelete(pathDrawer);
	shadowHandler.Kill();
	spring::SafeDelete(inMapDrawerView);

	CFeatureDrawer::KillStatic(gu->globalReload);
	CUnitDrawer::KillStatic(gu->globalReload); // depends on unitHandler, cubeMapHandler
	CProjectileDrawer::KillStatic(gu->globalReload);

	S3DModelVAO::Kill();
	modelLoader.Kill();

	textureHandler3DO.Kill();
	textureHandlerS3O.Kill();

	readMap->KillGroundDrawer();
	IGroundDecalDrawer::FreeInstance();
	DepthBufferCopy::Kill();
	LuaObjectDrawer::Kill();
	SmoothHeightMeshDrawer::FreeInstance();

	numUpdates = 0;
}




void CWorldDrawer::Update(bool newSimFrame)
{
	SCOPED_TIMER("Update::WorldDrawer");

	LuaObjectDrawer::Update(numUpdates == 0);
	readMap->UpdateDraw(numUpdates == 0);

	if (globalRendering->drawGround) {
		ZoneScopedN("GroundDrawer::Update");
		(readMap->GetGroundDrawer())->Update();
	}
	// XXX: done in CGame, needs to get updated even when !doDrawWorld
	// (it updates unitdrawpos which is used for maximized minimap too)
	// unitDrawer->Update();
	// lineDrawer.UpdateLineStipple();
	CUnitDrawer::UpdateStatic();
	CFeatureDrawer::UpdateStatic();
	projectileDrawer->UpdateDrawFlags();

	if (newSimFrame) {
		projectileDrawer->UpdateTextures();

		{
			SCOPED_TIMER("Update::WorldDrawer::{Sky,Water}");

			ISky::GetSky()->Update();
			IWater::GetWater()->Update();
		}

		// once every simframe is frequent enough here
		// NB: errors will not be logged until frame 0
		modelLoader.LogErrors();
	}

	numUpdates += 1;
}



void CWorldDrawer::GenerateIBLTextures() const
{

	if (shadowHandler.ShadowsLoaded()) {
		SCOPED_TIMER("Draw::World::CreateShadows");
		SCOPED_GL_DEBUGGROUP("Draw::World::CreateShadows");

		game->SetDrawMode(CGame::gameShadowDraw);
		shadowHandler.CreateShadows();
		game->SetDrawMode(CGame::gameNormalDraw);
	}

	{
		SCOPED_TIMER("Draw::World::UpdateReflTex");
		SCOPED_GL_DEBUGGROUP("Draw::World::UpdateReflTex");
		cubeMapHandler.UpdateReflectionTexture();
	}

	SCOPED_GL_DEBUGGROUP("Draw::World::UpdateMisc");
	bool sunDirUpd = ISky::GetSky()->GetLight()->Update();
	bool sunLightUpd = sunLighting->IsUpdated();
	bool skyUpd = ISky::GetSky()->IsUpdated();
	bool waterUpd = waterRendering->IsUpdated();

	if (sunDirUpd) {
		SCOPED_TIMER("Draw::World::UpdateSpecTex");
		cubeMapHandler.UpdateSpecularTexture();
	}
	if (sunDirUpd || skyUpd) {
		SCOPED_TIMER("Draw::World::UpdateSkyTex");
		ISky::GetSky()->UpdateSkyTexture();
	}
	if (sunDirUpd || sunLightUpd || waterUpd) {
		SCOPED_TIMER("Draw::World::UpdateShadingTex");
		readMap->UpdateShadingTexture();
	}
}

void CWorldDrawer::ResetMVPMatrices() const
{
	// RHI_TODO: FFP matrix stack - no RHI equivalent
	// Modern path uses uniform buffers; legacy GLSL path still needs FFP state
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(0, 1, 0, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	auto* device = RHI::GetDevice();
	auto* ctx = device->GetContext();

	ctx->SetBlendEnabled(true);
	ctx->SetDepthTestEnabled(false);
	ctx->SetBlendFunc(RHI::BlendFactor::SrcAlpha, RHI::BlendFactor::OneMinusSrcAlpha);
}



void CWorldDrawer::Draw() const
{
	SCOPED_TIMER("Draw::World");
	SCOPED_GL_DEBUGGROUP("Draw::World");

	auto* device = RHI::GetDevice();
	auto* ctx = device->GetContext();

	const auto& sky = ISky::GetSky();
	ctx->ClearColor(sky->fogColor.x, sky->fogColor.y, sky->fogColor.z, 0.0f);
	ctx->Clear(true, true, true);

	ctx->SetDepthWriteEnabled(true);
	ctx->SetDepthTestEnabled(true);
	ctx->SetBlendEnabled(false);
	ctx->SetBlendFunc(RHI::BlendFactor::SrcAlpha, RHI::BlendFactor::OneMinusSrcAlpha);

	camera->Update();

	DrawOpaqueObjects();
	DrawAlphaObjects();
	{
		SCOPED_TIMER("Draw::World::DrawWorld");
		SCOPED_GL_DEBUGGROUP("Draw::World::DrawWorld");
		eventHandler.DrawWorld();
	}


	DrawMiscObjects();
	DrawBelowWaterOverlay();
}


void CWorldDrawer::DrawOpaqueObjects() const
{
	CBaseGroundDrawer* gd = readMap->GetGroundDrawer();

	if (globalRendering->drawGround) {
		{
			SCOPED_TIMER("Draw::World::Terrain");
			SCOPED_GL_DEBUGGROUP("Draw::World::Terrain");
			gd->Draw(DrawPass::Normal);
			depthBufferCopy->MakeDepthBufferCopy();
		}
		{
			eventHandler.DrawPreDecals();
			SCOPED_TIMER("Draw::World::Decals");
			SCOPED_GL_DEBUGGROUP("Draw::World::Decals");
			groundDecals->Draw();
			projectileDrawer->DrawGroundFlashes();
		}
		{
			SCOPED_TIMER("Draw::World::Foliage");
			SCOPED_GL_DEBUGGROUP("Draw::World::Foliage");
			grassDrawer->Draw();
		}
		smoothHeightMeshDrawer->Draw(1.0f);
	}

	// not an opaque rendering, but makes sense to run after the terrain was rendered
	{
		const auto& sky = ISky::GetSky();
		sky->Draw();
	}

	selectedUnitsHandler.Draw();
	eventHandler.DrawWorldPreUnit();

	{
		SCOPED_TIMER("Draw::World::Models::Opaque");
		SCOPED_GL_DEBUGGROUP("Draw::World::Models::Opaque");
		unitDrawer->Draw(false);
		featureDrawer->Draw(false);
	}
	{
		SCOPED_TIMER("Draw::World::Models::Projectiles");
		SCOPED_GL_DEBUGGROUP("Draw::World::Models::Projectiles");
		projectileDrawer->DrawOpaque(false);
	}
	{
		SCOPED_TIMER("Draw::OpaqueObjects::Debug");
		SCOPED_GL_DEBUGGROUP("Draw::OpaqueObjects::Debug");
		DebugColVolDrawer::Draw();
		DebugVisibilityDrawer::DrawWorld();
		pathDrawer->DrawAll();
	}
}

void CWorldDrawer::DrawAlphaObjects() const
{
	auto* device = RHI::GetDevice();
	auto* ctx = device->GetContext();

	// transparent objects
	ctx->SetBlendEnabled(true);
	ctx->SetDepthFunc(RHI::CompareFunc::LessEqual);

	static const double belowPlaneEq[4] = {0.0f, -1.0f, 0.0f, 0.0f};
	static const double abovePlaneEq[4] = {0.0f,  1.0f, 0.0f, 0.0f};

	const bool hasWaterRendering = globalRendering->drawWater && readMap->HasVisibleWater();

	{
		SCOPED_TIMER("Draw::World::Models::Alpha");
		SCOPED_GL_DEBUGGROUP("Draw::World::Models::Alpha");
		// clip in model-space
		// RHI_TODO: FFP clip planes - no RHI equivalent
		// Modern path uses gl_ClipDistance[] in shaders; legacy GLSL needs FFP state
		if (hasWaterRendering) {
			glPushMatrix();
			glLoadIdentity();
			glClipPlane(GL_CLIP_PLANE3, belowPlaneEq);
			glPopMatrix();
			glEnable(GL_CLIP_PLANE3);
		}

		// draw alpha-objects below water surface (farthest)
		unitDrawer->DrawAlphaPass(false);
		featureDrawer->DrawAlphaPass(false);
	}
	{
		SCOPED_TIMER("Draw::World::Particles");
		SCOPED_GL_DEBUGGROUP("Draw::World::Particles");
		projectileDrawer->DrawAlpha(!hasWaterRendering, true, false, false);

		if (hasWaterRendering)
			glDisable(GL_CLIP_PLANE3);
	}

	if (!hasWaterRendering)
		return;

	// draw water (in-between)
	{
		SCOPED_TIMER("Draw::World::Water");
		SCOPED_GL_DEBUGGROUP("Draw::World::Water");

		const auto& water = IWater::GetWater();
		{
			ZoneScopedN("Draw::World::Water::UpdateWater");
			water->UpdateWater(game);
		}
		water->Draw();
		eventHandler.DrawWaterPost();
	}

	{
		SCOPED_TIMER("Draw::World::Models::Alpha");
		SCOPED_GL_DEBUGGROUP("Draw::World::Alpha");
		// RHI_TODO: FFP clip planes - no RHI equivalent
		// Modern path uses gl_ClipDistance[] in shaders; legacy GLSL needs FFP state
		glPushMatrix();
		glLoadIdentity();
		glClipPlane(GL_CLIP_PLANE3, abovePlaneEq);
		glPopMatrix();
		glEnable(GL_CLIP_PLANE3);

		// draw alpha-objects above water surface (closest)
		unitDrawer->DrawAlphaPass(false);
		featureDrawer->DrawAlphaPass(false);
	}
	{
		SCOPED_TIMER("Draw::World::Particles");
		SCOPED_GL_DEBUGGROUP("Draw::World::Particles");
		projectileDrawer->DrawAlpha(true, false, false, false);

		glDisable(GL_CLIP_PLANE3);
	}
}

void CWorldDrawer::DrawMiscObjects() const
{

	{
		// note: duplicated in CMiniMap::DrawWorldStuff()
		commandDrawer->DrawLuaQueuedUnitSetCommands();

		if (cmdColors.AlwaysDrawQueue() || guihandler->GetQueueKeystate()) {
			selectedUnitsHandler.DrawCommands();
		}
	}

	// either draw from here, or make {Dyn,Bump}Water use blending
	// pro: icons are drawn only once per frame, not every pass
	// con: looks somewhat worse for underwater / obscured icons
	if (!CUnitDrawer::UseScreenIcons())
		unitDrawer->DrawUnitIcons();

	lineDrawer.DrawAll();
	cursorIcons.Draw();

	mouse->DrawSelectionBox();
	guihandler->DrawMapStuff(false);

	if (globalRendering->drawMapMarks && !game->hideInterface) {
		inMapDrawerView->Draw();
	}
}



void CWorldDrawer::DrawBelowWaterOverlay() const
{

	if (!globalRendering->drawWater)
		return;
	if (mapRendering->voidWater)
		return;
	if (camera->GetPos().y >= 0.0f)
		return;

	auto* device = RHI::GetDevice();
	auto* ctx = device->GetContext();

	{
		const float3& cpos = camera->GetPos();
		const float vr = camera->GetFarPlaneDist() * 0.5f;

		ctx->SetDepthWriteEnabled(false);

		auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_C>();
		auto& sh = rb.GetShader();
		sh.Enable();

		const SColor color(0.0f, 0.5f, 0.3f, 0.50f);

		// Water surface quad
		{
			const float3 verts[] = {
				float3(cpos.x - vr, 0.0f, cpos.z - vr),
				float3(cpos.x - vr, 0.0f, cpos.z + vr),
				float3(cpos.x + vr, 0.0f, cpos.z + vr),
				float3(cpos.x + vr, 0.0f, cpos.z - vr)
			};

			// GL_QUADS: v0, v1, v2, v3 -> becomes top-left, top-right, bottom-right, bottom-left
			rb.AddQuadTriangles(
				{ verts[0], color },
				{ verts[1], color },
				{ verts[2], color },
				{ verts[3], color }
			);
		}

		// Underwater walls (quad strip)
		{
			const float3 verts[] = {
				float3(cpos.x - vr, 0.0f, cpos.z - vr),
				float3(cpos.x - vr,  -vr, cpos.z - vr),
				float3(cpos.x - vr, 0.0f, cpos.z + vr),
				float3(cpos.x - vr,  -vr, cpos.z + vr),
				float3(cpos.x + vr, 0.0f, cpos.z + vr),
				float3(cpos.x + vr,  -vr, cpos.z + vr),
				float3(cpos.x + vr, 0.0f, cpos.z - vr),
				float3(cpos.x + vr,  -vr, cpos.z - vr),
				float3(cpos.x - vr, 0.0f, cpos.z - vr),
				float3(cpos.x - vr,  -vr, cpos.z - vr),
			};

			// GL_QUAD_STRIP with 10 vertices produces 4 quads
			// Quad 0: v0, v1, v3, v2 (indices 0, 1, 3, 2)
			// Quad 1: v2, v3, v5, v4 (indices 2, 3, 5, 4)
			// Quad 2: v4, v5, v7, v6 (indices 4, 5, 7, 6)
			// Quad 3: v6, v7, v9, v8 (indices 6, 7, 9, 8)
			rb.AddQuadTriangles(
				{ verts[0], color },
				{ verts[2], color },
				{ verts[3], color },
				{ verts[1], color }
			);
			rb.AddQuadTriangles(
				{ verts[2], color },
				{ verts[4], color },
				{ verts[5], color },
				{ verts[3], color }
			);
			rb.AddQuadTriangles(
				{ verts[4], color },
				{ verts[6], color },
				{ verts[7], color },
				{ verts[5], color }
			);
			rb.AddQuadTriangles(
				{ verts[6], color },
				{ verts[8], color },
				{ verts[9], color },
				{ verts[7], color }
			);
		}

		rb.DrawElements(GL_TRIANGLES);
		sh.Disable();

		ctx->SetDepthWriteEnabled(true);
	}

	{
		// draw water-coloration quad in raw screenspace
		ResetMVPMatrices();

		auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_C>();
		auto& sh = rb.GetShader();
		sh.Enable();

		const SColor color(0.0f, 0.2f, 0.8f, 0.333f);

		const float3 verts[] = {
			float3(0.0f, 0.0f, -1.0f),
			float3(1.0f, 0.0f, -1.0f),
			float3(1.0f, 1.0f, -1.0f),
			float3(0.0f, 1.0f, -1.0f),
		};

		rb.AddQuadTriangles(
			{ verts[0], color },
			{ verts[1], color },
			{ verts[2], color },
			{ verts[3], color }
		);

		rb.DrawElements(GL_TRIANGLES);
		sh.Disable();
	}
}
