/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VAO.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/RHI/RHIFactory.h"
#include "System/Misc/TracyDefs.h"

static inline bool IsGLBackend() {
	return RHI::GetDefaultBackend() == RHI::Backend::OpenGL;
}

bool VAO::IsSupported()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!IsGLBackend())
		return false;
	static bool supported = GLAD_GL_ARB_vertex_array_object;
	return supported;
}

void VAO::Generate() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!IsGLBackend())
		return;
	if (id > 0)
		return;

	glGenVertexArrays(1, &id);
}

void VAO::Delete() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!IsGLBackend())
		return;
	if (id > 0) {
		glDeleteVertexArrays(1, &id);
		id = 0;
	}
}

void VAO::Bind() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!IsGLBackend())
		return;
	glBindVertexArray(GetId());
}

void VAO::Unbind() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!IsGLBackend())
		return;
	glBindVertexArray(0);
}
