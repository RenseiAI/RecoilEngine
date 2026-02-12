/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "HUDDrawer.h"

#include "Rendering/Fonts/glFont.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/GL/SubState.h"
#include "Rendering/RHI/RHITypes.h"
#include "Rendering/RHI/RHIFactory.h"

/**
 * RHI_MIGRATION_DOCS(HUDDrawer)
 *
 * Migration Status: PARTIAL - Pipeline state abstracted, matrix stack migrated to RHI::MatrixStack
 *
 * GL::SubState to RHI Mapping (Draw function):
 *   GL::SubState(DepthTest(GL_FALSE), Blending(GL_TRUE), BlendFunc(...))
 *   Maps to:
 *     RHI::PipelineDesc desc;
 *     desc.depthStencil.depthTestEnabled = false;
 *     desc.blend.enabled = true;
 *     desc.blend.srcColor = RHI::BlendFactor::SrcAlpha;
 *     desc.blend.dstColor = RHI::BlendFactor::OneMinusSrcAlpha;
 *
 * MIGRATED:
 *   - Immediate mode in DrawUnitDirectionArrow, DrawCameraDirectionArrow,
 *     DrawTargetReticle: glBegin/glEnd, glVertex*, glColor*
 *     -> TypedRenderBuffer<VA_TYPE_C>
 *   - FFP texturing: glEnable/glDisable(GL_TEXTURE_2D) -> removed (shader-based)
 *   - Matrix stack: glPushMatrix/glPopMatrix, glTranslatef, glScalef, glRotatef,
 *     glMultMatrixf, glLoadIdentity -> RHI::MatrixStack + RHI::ScopedMatrixPush
 *     [x] All transform computation on CPU via MatrixStack
 *     Remaining GL: glLoadMatrixf flush calls before draws (RenderBuffer shader
 *     reads gl_ModelViewProjectionMatrix from FFP state)
 *
 * Not migrated (deferred):
 *   - DrawModel: glColor4f before unit->localModel.Draw() (model drawing)
 *
 * Completion Criteria:
 *   [x] Convert immediate-mode drawing to vertex buffers
 *   [x] Replace matrix stack with RHI::MatrixStack (flush via glLoadMatrixf)
 *   [x] Document GL::SubState -> RHI::PipelineDesc mapping
 */
#include "Game/Camera.h"
#include "Game/GlobalUnsynced.h"
#include "Game/Players/Player.h"
#include "Game/Players/PlayerHandler.h"
#include "Sim/MoveTypes/MoveType.h"
#include "Sim/Units/Unit.h"
#include "Sim/Weapons/Weapon.h"
#include "Sim/Weapons/WeaponDef.h"
#include "Sim/Misc/GlobalSynced.h"
#include "System/MathConstants.h"
#include "System/SpringMath.h"

#include <cmath>

using namespace GL::State;


HUDDrawer* HUDDrawer::GetInstance()
{
	static HUDDrawer hud;
	return &hud;
}

void HUDDrawer::FlushMatrices() const
{
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(projStack.Top());
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(mvStack.Top());
}

void HUDDrawer::DrawModel(const CUnit* unit)
{
	RHI::ScopedMatrixPush projGuard(projStack);
	RHI::ScopedMatrixPush mvGuard(mvStack);

	projStack.Translate(-0.8f, -0.4f, 0.0f).MultMatrix(camera->GetProjectionMatrix());
	mvStack.Translate(0.0f, 0.0f, -unit->radius)
	       .Scale(1.0f / unit->radius, 1.0f / unit->radius, 1.0f / unit->radius);

	if (unit->moveType->UseHeading()) {
		mvStack.RotateX(-90.0f * math::DEG_TO_RAD)
		       .RotateZ(180.0f * math::DEG_TO_RAD);
	} else {
		CMatrix44f m(ZeroVector,
			float3(camera->GetRight().x, camera->GetUp().x, camera->GetDir().x),
			float3(camera->GetRight().y, camera->GetUp().y, camera->GetDir().y),
			float3(camera->GetRight().z, camera->GetUp().z, camera->GetDir().z));
		mvStack.MultMatrix(m);
	}

	FlushMatrices();
	glColor4f(1.0f, 1.0f, 1.0f, 0.25f);
	unit->localModel.Draw();
}

void HUDDrawer::DrawUnitDirectionArrow(const CUnit* unit)
{
	if (unit->moveType->UseHeading()) {
		RHI::ScopedMatrixPush mvGuard(mvStack);

		mvStack.Translate(-0.8f, -0.4f, 0.0f)
		       .Scale(0.33f, 0.33f * globalRendering->aspectRatio, 0.33f)
		       .RotateZ((unit->heading * 180.0f / 32768 + 180) * math::DEG_TO_RAD);

		FlushMatrices();

		const SColor color(0.3f, 0.9f, 0.3f, 0.4f);
		auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_C>();
		auto& sh = rb.GetShader();
		sh.Enable();
		rb.AddVertex({ {-0.2f, -0.3f, 0.0f}, color });
		rb.AddVertex({ {-0.2f,  0.3f, 0.0f}, color });
		rb.AddVertex({ { 0.0f,  0.4f, 0.0f}, color });
		rb.AddVertex({ { 0.2f,  0.3f, 0.0f}, color });
		rb.AddVertex({ { 0.2f, -0.3f, 0.0f}, color });
		rb.AddVertex({ {-0.2f, -0.3f, 0.0f}, color });
		rb.DrawArrays(GL_TRIANGLE_FAN);
		sh.Disable();
	}
}
void HUDDrawer::DrawCameraDirectionArrow(const CUnit* unit)
{
	if (unit->moveType->UseHeading()) {
		RHI::ScopedMatrixPush mvGuard(mvStack);

		const float heading = GetHeadingFromVector(camera->GetDir().x, camera->GetDir().z) * 180.0f / 32768 + 180;
		mvStack.Translate(-0.8f, -0.4f, 0.0f)
		       .Scale(0.33f, 0.33f * globalRendering->aspectRatio, 0.33f)
		       .RotateZ(heading * math::DEG_TO_RAD)
		       .Scale(0.4f, 0.4f, 0.3f);

		FlushMatrices();

		const SColor color(0.4f, 0.4f, 1.0f, 0.6f);
		auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_C>();
		auto& sh = rb.GetShader();
		sh.Enable();
		rb.AddVertex({ {-0.2f, -0.3f, 0.0f}, color });
		rb.AddVertex({ {-0.2f,  0.3f, 0.0f}, color });
		rb.AddVertex({ { 0.0f,  0.5f, 0.0f}, color });
		rb.AddVertex({ { 0.2f,  0.3f, 0.0f}, color });
		rb.AddVertex({ { 0.2f, -0.3f, 0.0f}, color });
		rb.AddVertex({ {-0.2f, -0.3f, 0.0f}, color });
		rb.DrawArrays(GL_TRIANGLE_FAN);
		sh.Disable();
	}
}

void HUDDrawer::DrawWeaponStates(const CUnit* unit)
{
	projStack.LoadIdentity();
	FlushMatrices();

	font->glFormat(-0.9f, 0.35f, 1.0f, FONT_SCALE | FONT_NORM, "Health: %.0f / %.0f", (float) unit->health, (float) unit->maxHealth);

	if (playerHandler.Player(gu->myPlayerNum)->fpsController.mouse2)
		font->glPrint(-0.9f, 0.30f, 1.0f, FONT_SCALE | FONT_NORM, "Free-Fire Mode");

	int numWeaponsToPrint = 0;

	for (unsigned int a = 0; a < unit->weapons.size(); ++a) {
		const WeaponDef* wd = unit->weapons[a]->weaponDef;
		if (!wd->isShield) {
			++numWeaponsToPrint;
		}
	}

	if (numWeaponsToPrint > 0) {
		// we have limited space to draw whole list of weapons
		const float yMax = 0.25f;
		const float yMin = 0.00f;
		const float maxLineHeight = 0.045f;
		const float lineHeight = std::min((yMax - yMin) / numWeaponsToPrint, maxLineHeight);
		const float fontSize = 1.2f * (lineHeight / maxLineHeight);
		float yPos = yMax;

		for (unsigned int a = 0; a < unit->weapons.size(); ++a) {
			const CWeapon* w = unit->weapons[a];
			const WeaponDef* wd = w->weaponDef;

			if (!wd->isShield) {
				yPos -= lineHeight;

				if (wd->stockpile && !w->numStockpiled) {
					if (w->numStockpileQued) {
						font->glFormat(-0.9f, yPos, fontSize, FONT_SCALE | FONT_NORM, "%s: Stockpiling (%i%%)", wd->description.c_str(), int(100.0f * w->buildPercent + 0.5f));
					}
					else {
						font->glFormat(-0.9f, yPos, fontSize, FONT_SCALE | FONT_NORM, "%s: No ammo", wd->description.c_str());
					}
				} else if (w->reloadStatus > gs->frameNum) {
					font->glFormat(-0.9f, yPos, fontSize, FONT_SCALE | FONT_NORM, "%s: Reloading (%i%%)", wd->description.c_str(), 100 - int(100.0f * (w->reloadStatus - gs->frameNum) / int(w->reloadTime / unit->reloadSpeed) + 0.5f));
				} else if (!w->angleGood) {
					font->glFormat(-0.9f, yPos, fontSize, FONT_SCALE | FONT_NORM, "%s: Aiming", wd->description.c_str());
				} else {
					font->glFormat(-0.9f, yPos, fontSize, FONT_SCALE | FONT_NORM, "%s: Ready", wd->description.c_str());
				}
			}
		}
	}
}

void HUDDrawer::DrawTargetReticle(const CUnit* unit)
{
	// draw the reticle in world coordinates
	projStack.LoadMatrix(camera->GetProjectionMatrix());
	mvStack.LoadMatrix(camera->GetViewMatrix());

	RHI::ScopedMatrixPush mvGuard(mvStack);

	FlushMatrices();

	auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_C>();
	auto& sh = rb.GetShader();
	sh.Enable();

	for (unsigned int a = 0; a < unit->weapons.size(); ++a) {
		const CWeapon* w = unit->weapons[a];

		if (!w) {
			continue;
		}

		SColor color;
		switch (a) {
			case 0:  color = SColor(0.0f, 1.0f, 0.0f, 0.7f); break;
			case 1:  color = SColor(1.0f, 0.0f, 0.0f, 0.7f); break;
			default: color = SColor(0.0f, 0.0f, 1.0f, 0.7f); break;
		}

		if (w->HaveTarget()) {
			float3 pos = w->GetCurrentTargetPos();
			float3 v1 = (pos - camera->GetPos()).ANormalize();
			float3 v2 = (v1.cross(UpVector)).ANormalize();
			float3 v3 = (v2.cross(v1)).Normalize();
			float radius = 10.0f;

			if (w->GetCurrentTarget().type == Target_Unit)
				radius = w->GetCurrentTarget().unit->radius;

			// draw the target reticle circle
			for (int b = 0; b <= 80; ++b) {
				rb.AddVertex({ pos + (v2 * fastmath::sin(b * math::TWOPI / 80) + v3 * fastmath::cos(b * math::TWOPI / 80)) * radius, color });
			}
			rb.DrawArrays(GL_LINE_STRIP);

			if (!w->onlyForward) {
				const CPlayer* p = w->owner->fpsControlPlayer;
				const FPSUnitController& c = p->fpsController;
				const float dist = std::min(c.targetDist, w->range * 0.9f);

				pos = w->aimFromPos + w->wantedDir * dist;
				v1 = (pos - camera->GetPos()).ANormalize();
				v2 = (v1.cross(UpVector)).ANormalize();
				v3 = (v2.cross(v1)).ANormalize();
				radius = dist / 100.0f;

				// draw the aim reticle circle
				for (int b = 0; b <= 80; ++b) {
					rb.AddVertex({ pos + (v2 * fastmath::sin(b * math::TWOPI / 80) + v3 * fastmath::cos(b * math::TWOPI / 80)) * radius, color });
				}
				rb.DrawArrays(GL_LINE_STRIP);
			}

			// draw crosshair lines
			if (!w->onlyForward) {
				rb.AddVertex({ pos, color });
				rb.AddVertex({ w->GetCurrentTargetPos(), color });

				rb.AddVertex({ pos + (v2 * fastmath::sin(math::PI * 0.25f) + v3 * fastmath::cos(math::PI * 0.25f)) * radius, color });
				rb.AddVertex({ pos + (v2 * fastmath::sin(math::PI * 1.25f) + v3 * fastmath::cos(math::PI * 1.25f)) * radius, color });

				rb.AddVertex({ pos + (v2 * fastmath::sin(math::PI * -0.25f) + v3 * fastmath::cos(math::PI * -0.25f)) * radius, color });
				rb.AddVertex({ pos + (v2 * fastmath::sin(math::PI * -1.25f) + v3 * fastmath::cos(math::PI * -1.25f)) * radius, color });
			}
			if ((w->GetCurrentTargetPos() - camera->GetPos()).ANormalize().dot(camera->GetDir()) < 0.7f) {
				rb.AddVertex({ w->GetCurrentTargetPos(), color });
				rb.AddVertex({ camera->GetPos() + camera->GetDir() * 100.0f, color });
			}
			rb.DrawArrays(GL_LINES);
		}
	}

	sh.Disable();
}

void HUDDrawer::Draw(const CUnit* unit)
{
	if (unit == nullptr || !draw)
		return;

	// Save outer GL matrix state
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();

	// Reset member stacks to identity
	projStack.LoadIdentity();
	mvStack.LoadIdentity();
	FlushMatrices();

	auto state = GL::SubState(
		DepthTest(GL_FALSE),
		Blending(GL_TRUE),
		BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

	{
		RHI::ScopedMatrixPush mvGuard(mvStack);
		DrawUnitDirectionArrow(unit);
		DrawCameraDirectionArrow(unit);
	}

	state << DepthTest(GL_TRUE);
		DrawModel(unit);
		DrawWeaponStates(unit);

	state << DepthTest(GL_FALSE);
		DrawTargetReticle(unit);

	// Restore outer GL matrix state
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}
