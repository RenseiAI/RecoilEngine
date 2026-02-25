/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "IWater.h"
#include "ISky.h"
#include "BasicWater.h"
// DEPRECATED: AdvWater, DynWater, RefractWater removed (ARM64 Metal port)
// They use ARB programs + immediate mode with no Metal equivalent.
// #include "AdvWater.h"
// #include "DynWater.h"
// #include "RefractWater.h"
#include "BumpWater.h"
#include "Game/Game.h"
#include "Game/GameHelper.h"
#include "Map/ReadMap.h"
#include "Map/BaseGroundDrawer.h"
#include "Rendering/Features/FeatureDrawer.h"
#include "Rendering/Units/UnitDrawer.h"
#include "Rendering/Env/Particles/ProjectileDrawer.h"
#include "Rendering/GL/myGL.h" // retained: GL_CLIP_PLANE2, glClipPlane (remaining unmigrated sites)
#include "Rendering/RHI/RHIDevice.h"
#include "Rendering/RHI/RHIContext.h"
#include "Rendering/RHI/RHIFactory.h"
#include "Sim/Projectiles/ExplosionListener.h"
#include "System/Config/ConfigHandler.h"
#include "System/EventHandler.h"
#include "System/Exceptions.h"
#include "System/SafeUtil.h"
#include "System/Log/ILog.h"

#include "System/Misc/TracyDefs.h"

// RHI-GAP: IWater uses legacy FFP clip planes (glClipPlane, GL_CLIP_PLANE2).
// Metal requires shader-based clipping via [[clip_distance]]. The water
// reflection/refraction passes set clip planes to cull geometry above/below
// the water surface. Migration requires:
// 1. All water-affected shaders to output gl_ClipDistance[n]
// 2. RHI context to enable/disable clip distances (available via
//    IRHIContext::SetClipDistanceEnabled)
// 3. Uniform buffer to pass clip plane equations to shaders
// This is deferred until shader-based clipping is implemented engine-wide.

CONFIG(int, Water)
.defaultValue(IWater::WATER_RENDERER_REFLECTIVE)
.safemodeValue(IWater::WATER_RENDERER_BASIC)
.headlessValue(0)
.minimumValue(0)
.maximumValue(IWater::NUM_WATER_RENDERERS - 1)
.description("Defines the type of water rendering. Can be set in game. Options are: 0 = Basic water, 1 = Reflective water, 2 = Reflective and Refractive water, 3 = Dynamic water, 4 = Bumpmapped water");

IWater::IWater()
	: drawReflection(false)
	, drawRefraction(false)
	, wireFrameMode(false)
{
	CExplosionCreator::AddExplosionListener(this);
}

std::unique_ptr<IWater> IWater::water = nullptr;

void IWater::ExplosionOccurred(const CExplosionParams& event) {
	RECOIL_DETAILED_TRACY_ZONE;
	AddExplosion(event.pos, event.damages.GetDefault(), event.craterAreaOfEffect);
}

void IWater::SetModelClippingPlane(const double* planeEq) {
	RECOIL_DETAILED_TRACY_ZONE;
	auto* ctx = RHI::GetDevice()->GetContext();
	ctx->SetClipPlaneEquation(2, planeEq);
}

void IWater::SetWater(int rendererMode)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// DEPRECATED: Only BumpWater (modern GLSL) and BasicWater (fallback) are
	// available. AdvWater/DynWater/RefractWater used ARB programs + immediate
	// mode with no Metal equivalent. Requests for deprecated renderers are
	// silently upgraded to BumpWater.
	static constexpr std::array<bool, NUM_WATER_RENDERERS> allowedModes = {
		true,   // WATER_RENDERER_BASIC
		false,  // WATER_RENDERER_REFLECTIVE (AdvWater - deprecated)
		false,  // WATER_RENDERER_DYNAMIC (DynWater - deprecated)
		false,  // WATER_RENDERER_REFL_REFR (RefractWater - deprecated)
		true,   // WATER_RENDERER_BUMPMAPPED
	};

	WATER_RENDERER selectedRendererID;
	if (rendererMode < 0) {
		if (water == nullptr) {
			selectedRendererID = static_cast<WATER_RENDERER>(configHandler->GetInt("Water"));
		} else {
			// cycle between Basic and BumpMapped only
			selectedRendererID = (water->GetID() == WATER_RENDERER_BUMPMAPPED)
				? WATER_RENDERER_BASIC
				: WATER_RENDERER_BUMPMAPPED;
		}
	} else {
		selectedRendererID = static_cast<WATER_RENDERER>(rendererMode);
	}

	// Force deprecated modes to BumpWater
	if (!allowedModes[selectedRendererID]) {
		LOG("Water renderer %d deprecated (ARM64 Metal port), using BumpWater", static_cast<int>(selectedRendererID));
		selectedRendererID = WATER_RENDERER_BUMPMAPPED;
	}

	if (water && water->GetID() == selectedRendererID)
		return;

	water = nullptr;
	try {
		switch (selectedRendererID)
		{
		case WATER_RENDERER_BASIC:
			water = std::make_unique<CBasicWater>();
			break;
		case WATER_RENDERER_BUMPMAPPED:
			water = std::make_unique<CBumpWater>();
			break;
		default:
			water = std::make_unique<CBumpWater>();
			break;
		}
		if (water)
			water->InitResources();
	} catch (const content_error& ex) {
		LOG_L(L_ERROR, "Loading \"%s\" water failed, error: %s", IWater::GetWaterName(selectedRendererID), ex.what());
		if (water)
			water->FreeResources(); //destructor is not called for an object throwing exception in a constructor
		water = nullptr;
	}

	// set it here as user preference.
	if (water)
		configHandler->Set("Water", static_cast<int>(water->GetID()));

	if (water == nullptr)
		water = std::make_unique<CBasicWater>();
}


void IWater::DrawReflections(const double* clipPlaneEqs, bool drawGround, bool drawSky) {
	RECOIL_DETAILED_TRACY_ZONE;
	game->SetDrawMode(CGame::gameReflectionDraw);

	{
		drawReflection = true;

		SCOPED_TIMER("Draw::Water::DrawReflections");
		SCOPED_GL_DEBUGGROUP("Draw::Water::DrawReflections");

		auto* ctx = RHI::GetDevice()->GetContext();

		// opaque; do not clip skydome (is drawn in camera space)
		if (drawSky) {
			ISky::GetSky()->Draw();
		}

		ctx->SetClipDistanceEnabled(2, true);
		// Ground clip plane: equation in world-space, glClipPlane transforms by current MV
		if (!RHI::IsMetalBackend())
			glClipPlane(GL_CLIP_PLANE2, &clipPlaneEqs[0]);

		if (drawGround)
			readMap->GetGroundDrawer()->Draw(DrawPass::WaterReflection);


		// rest needs the plane in model-space; V is combined with P
		SetModelClippingPlane(&clipPlaneEqs[4]);
		unitDrawer->Draw(true);
		featureDrawer->Draw(true);
		projectileDrawer->DrawOpaque(true);

		// transparent
		unitDrawer->DrawAlphaPass(true);
		featureDrawer->DrawAlphaPass(true);
		projectileDrawer->DrawAlpha(true, false, true, false);
		// sun-disc does not blend well with water

		eventHandler.DrawWorldReflection();
		ctx->SetClipDistanceEnabled(2, false);

		drawReflection = false;
	}

	game->SetDrawMode(CGame::gameNormalDraw);
}

void IWater::DrawRefractions(const double* clipPlaneEqs, bool drawGround, bool drawSky) {
	RECOIL_DETAILED_TRACY_ZONE;
	game->SetDrawMode(CGame::gameRefractionDraw);

	{
		drawRefraction = true;

		SCOPED_TIMER("Draw::Water::DrawRefractions");
		SCOPED_GL_DEBUGGROUP("Draw::Water::DrawRefractions");

		auto* ctx = RHI::GetDevice()->GetContext();

		ctx->SetClipDistanceEnabled(2, true);
		// Ground clip plane: equation in world-space, glClipPlane transforms by current MV
		if (!RHI::IsMetalBackend())
			glClipPlane(GL_CLIP_PLANE2, &clipPlaneEqs[0]);

		// opaque
		if (drawSky) {
			ISky::GetSky()->Draw();
		}
		if (drawGround) {
			readMap->GetGroundDrawer()->Draw(DrawPass::WaterRefraction);
		}


		SetModelClippingPlane(&clipPlaneEqs[4]);
		unitDrawer->Draw(false, true);
		featureDrawer->Draw(false, true);
		projectileDrawer->DrawOpaque(false, true);

		// transparent
		unitDrawer->DrawAlphaPass(false, true);
		featureDrawer->DrawAlphaPass(false, true);
		projectileDrawer->DrawAlpha(false, true, false, true);

		eventHandler.DrawWorldRefraction();
		ctx->SetClipDistanceEnabled(2, false);

		drawRefraction = false;
	}

	game->SetDrawMode(CGame::gameNormalDraw);
}

