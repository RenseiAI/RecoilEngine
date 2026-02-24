static constexpr const char* vsRenderBufferSrc = R"(
// Version and extensions
%s

// VS input attributes
%s

uniform mat4 transformMatrix;

// VS output attributes
%s

void main() {
%s
	gl_Position = transformMatrix * %s;
}
)";

static constexpr const char* fsRenderBufferSrc = R"(
// Version and extensions
%s

uniform sampler2D tex;
uniform vec4 ucolor = vec4(1.0);

// FS input attributes
%s

// FS output attributes
out vec4 outColor;

uniform vec4 alphaCtrl = vec4(0.0, 0.0, 0.0, 1.0); //always pass

bool AlphaDiscard(float a) {
	float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
	float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;

	return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
}

void main() {
%s
	outColor *= ucolor;
	if (AlphaDiscard(outColor.a))
		discard;

}
)";

// RHI variant of the vertex shader: uses an explicit std140 UBO block so
// glslang generates proper uniform_buffers in SPIR-V (not gl_plain_uniforms).
// Both VS and FS declare the same block for consistent std140 layout.
static constexpr const char* vsRenderBufferSrcRHI = R"(
// Version and extensions
%s

// VS input attributes
%s

layout(std140) uniform RBUniforms {
	mat4 transformMatrix;
	vec4 ucolor;
	vec4 alphaCtrl;
};

// VS output attributes
%s

void main() {
%s
	gl_Position = transformMatrix * %s;
}
)";

// RHI variant of the fragment shader: uses the same std140 UBO block,
// no default uniform initializers, alpha discard is inlined to avoid
// SPIRV-Cross address-space mismatch (helper functions receiving
// constant-buffer parameters as thread refs).
static constexpr const char* fsRenderBufferSrcRHI = R"(
// Version and extensions
%s

uniform sampler2D tex;

layout(std140) uniform RBUniforms {
	mat4 transformMatrix;
	vec4 ucolor;
	vec4 alphaCtrl;
};

// FS input attributes
%s

// FS output attributes
out vec4 outColor;

void main() {
%s
	outColor *= ucolor;
	float _alphaTestGT = float(outColor.a > alphaCtrl.x) * alphaCtrl.y;
	float _alphaTestLT = float(outColor.a < alphaCtrl.x) * alphaCtrl.z;
	if ((_alphaTestGT + _alphaTestLT + alphaCtrl.w) == 0.0)
		discard;
}
)";