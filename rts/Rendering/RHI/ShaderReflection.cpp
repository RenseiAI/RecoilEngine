/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "ShaderReflection.h"

namespace RHI {

const ReflectedUniform* ShaderReflection::FindUniform(const std::string& name) const {
	for (const auto& u : uniforms)
		if (u.name == name) return &u;
	return nullptr;
}

const ReflectedAttribute* ShaderReflection::FindAttribute(const std::string& name) const {
	for (const auto& a : attributes)
		if (a.name == name) return &a;
	return nullptr;
}

const ReflectedSampler* ShaderReflection::FindSampler(const std::string& name) const {
	for (const auto& s : samplers)
		if (s.name == name) return &s;
	return nullptr;
}

void ShaderReflection::Clear() {
	uniforms.clear();
	attributes.clear();
	samplers.clear();
}

} // namespace RHI
