#version 120

// Explicit uniforms replacing FFP built-ins
// Note: modelMatrix contains the MODEL matrix only (not view).
//       viewProjMatrix contains VIEW * PROJECTION.
uniform mat4 modelMatrix;
uniform mat4 viewProjMatrix;
uniform vec3 cameraPosW;

varying vec4 vertexWorldPos;
varying vec3 cameraDir;
varying float fogFactor;
varying vec3 normalv;

#if (USE_SHADOWS == 1)
	uniform mat4 shadowMatrix;
	varying vec4 shadowVertexPos;
#endif

void main(void)
{
	// mat3(modelMatrix) works as normal matrix because unit transforms are
	// rotation+translation only (no non-uniform scale). The normals are
	// normalize()'d in the fragment shader, so uniform scale cancels out.
	normalv = mat3(modelMatrix) * gl_Normal;

	vec4 worldPos  = modelMatrix * gl_Vertex;
	gl_ClipVertex  = worldPos;                    // world space (same as before)
	gl_Position    = viewProjMatrix * worldPos;

	vertexWorldPos = worldPos;
	cameraDir      = worldPos.xyz - cameraPosW;

#if (USE_SHADOWS == 1)
	shadowVertexPos = shadowMatrix * vertexWorldPos;
	shadowVertexPos.xy += vec2(0.5);
#endif

	gl_TexCoord[0].st = gl_MultiTexCoord0.st;

#if (DEFERRED_MODE == 0)
	float fogCoord = length(cameraDir.xyz);
	fogFactor = (gl_Fog.end - fogCoord) * gl_Fog.scale; //gl_Fog.scale := 1.0 / (gl_Fog.end - gl_Fog.start)
	fogFactor = clamp(fogFactor, 0.0, 1.0);
#endif
}
