#if (GL_FRAGMENT_PRECISION_HIGH == 1)
// ancient GL3 ATI drivers confuse GLSL for GLSL-ES and require this
precision highp float;
#else
precision mediump float;
#endif

#extension GL_ARB_explicit_attrib_location : enable

layout(location = 0) in vec3 vertexPos;
layout(location = 1) in vec3 vertexNormal;
layout(location = 4) in vec4 vertexTexCoord;

out vec2 texCoord0;

uniform mat4 shadowViewMatrix = mat4(1.0);
uniform mat4 shadowProjectionMatrix = mat4(1.0);
uniform vec4 clipPlaneEquation0 = vec4(0.0, 0.0, 0.0, 1.0);
uniform vec4 clipPlaneEquation1 = vec4(0.0, 0.0, 0.0, 1.0);

out float gl_ClipDistance[2];

void main() {
	#if 0
		mat3 normalMatrix = mat3(transpose(inverse(shadowViewMatrix)));
	#else
		mat3 normalMatrix = mat3(shadowViewMatrix);
	#endif

	vec4 lightVertexPos = shadowViewMatrix * vec4(vertexPos, 1.0);
	vec3 lightVertexNormal = normalize(normalMatrix * vertexNormal);

	float NdotL = clamp(dot(lightVertexNormal, vec3(0.0, 0.0, 1.0)), 0.0, 1.0);

	//use old bias formula from GetShadowPCFRandom(), but this time to write down shadow depth map values
	const float cb = 1e-6;
	float bias = cb * tan(acos(NdotL));
	bias = clamp(bias, 0.0, 100.0 * cb);

	lightVertexPos.xy += vec2(0.5);
	lightVertexPos.z  += bias;

	gl_Position = shadowProjectionMatrix * lightVertexPos;

	gl_ClipDistance[0] = dot(vec4(vertexPos, 1.0), clipPlaneEquation0);
	gl_ClipDistance[1] = dot(vec4(vertexPos, 1.0), clipPlaneEquation1);
	texCoord0 = vertexTexCoord.st;
}