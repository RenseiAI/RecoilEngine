#include "ModelDrawer.h"

#include "Map/Ground.h"
#include "Rendering/GL/LightHandler.h"
#include "Rendering/RHI/RHIFactory.h"
#include "System/Config/ConfigHandler.h"
#include "Rendering/Env/CubeMapHandler.h"
#include "Rendering/LuaObjectDrawer.h"

#include "System/Misc/TracyDefs.h"

void CModelDrawerConcept::InitStatic()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (initialized)
		return;

	cubeMapHandler.Init();
	wireFrameMode = false;

	lightHandler.Init(configHandler->GetInt("MaxDynamicModelLights"));

	deferredAllowed = configHandler->GetBool("AllowDeferredModelRendering");

	// Deferred rendering requires real FBO bind/unbind to redirect draws to G-buffer textures.
	// On Metal, FBO operations are stubs (no-ops), so deferred draws corrupt the screen.
	if (RHI::GetDefaultBackend() == RHI::Backend::Metal)
		deferredAllowed = false;

	// shared with FeatureDrawer!
	geomBuffer = LuaObjectDrawer::GetGeometryBuffer();
	deferredAllowed &= geomBuffer->Valid();

	IModelDrawerState::InitInstance<CModelDrawerStateGLSL>(MODEL_DRAWER_GLSL);
	IModelDrawerState::InitInstance<CModelDrawerStateGL4 >(MODEL_DRAWER_GL4 );

	initialized = true;
}

void CModelDrawerConcept::KillStatic(bool reload)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!initialized)
		return;

	cubeMapHandler.Free();
	geomBuffer = nullptr;

	for (int t = ModelDrawerTypes::MODEL_DRAWER_GLSL; t < ModelDrawerTypes::MODEL_DRAWER_CNT; ++t) {
		IModelDrawerState::KillInstance(t);
	}

	initialized = false;
}