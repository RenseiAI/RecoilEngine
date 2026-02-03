/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _GLSL_COPY_STATE_H
#define _GLSL_COPY_STATE_H

// NOTE(RHI): This file is OpenGL-specific. It directly queries GL program
// state (uniforms, UBOs, SSBOs, attributes, transform feedback, geometry
// shader params) via glGetProgramiv/glGetActiveUniform/etc. and copies it
// between GL program objects during hot-reload/recompilation.
//
// Metal equivalent: For the Metal backend, shader state transfer during
// recompilation would use RHI::ShaderReflection data (extracted from
// SPIR-V via SPIRV-Cross) rather than runtime GL queries. The reflection
// data provides uniform names, types, and Metal buffer/texture indices.
// See rts/Rendering/RHI/ShaderReflection.h.

#include "System/UnorderedMap.hpp"
#include "Shader.h"
#include "ShaderStates.h"

typedef unsigned int GLuint;

namespace Shader {
	/**
	 * @brief
	 * Copies all hidden states related to a GLSL program,
	 * in particular uniforms, sampler bindings, UBO bindings,
	 * feedback state, geoshader state, ...
	 *
	 * @example
	 * Very useful when you want to recompile an uber-shader
	 * with a different #define-flagset.
	 */
	void GLSLCopyState(GLuint newProgID, GLuint oldProgID, IProgramObject::UniformStates* uniformStates);
}

#endif //_GLSL_COPY_STATE_H
