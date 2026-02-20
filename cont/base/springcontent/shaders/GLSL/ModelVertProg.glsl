#version 130

// Explicit uniforms replacing FFP built-ins
// Note: modelMatrix contains the MODEL matrix only (not view).
//       viewProjMatrix contains VIEW * PROJECTION.
uniform mat4 modelMatrix;
uniform mat4 viewProjMatrix;
uniform vec3 cameraPosW;
uniform vec4 fogParams; //%.x=start, .y=end, .z=unused, .w=scale (1/(end-start))
uniform vec4 clipPlane0 = vec4(0.0, 0.0, 0.0, 1.0); //upper construction clip plane
uniform vec4 clipPlane1 = vec4(0.0, 0.0, 0.0, 1.0); //lower construction clip plane
uniform vec4 clipPlane2 = vec4(0.0, 0.0, 0.0, 1.0); //water clip plane

out vec4 vertexWorldPos;
out vec3 cameraDir;
out float fogFactor;
out vec3 normalv;

#if (USE_SHADOWS == 1)
	uniform mat4 shadowMatrix;
	out vec4 shadowVertexPos;
#endif

out vec2 texCoord0;
out float gl_ClipDistance[3];

void main(void)
{
	// mat3(modelMatrix) works as normal matrix because unit transforms are
	// rotation+translation only (no non-uniform scale). The normals are
	// normalize()'d in the fragment shader, so uniform scale cancels out.
	normalv = mat3(modelMatrix) * gl_Normal;

	vec4 worldPos  = modelMatrix * gl_Vertex;
	gl_ClipDistance[0] = dot(gl_Vertex, clipPlane0); //model space (construction upper)
	gl_ClipDistance[1] = dot(gl_Vertex, clipPlane1); //model space (construction lower)
	gl_ClipDistance[2] = dot(worldPos, clipPlane2);  //world space (water)
	gl_Position    = viewProjMatrix * worldPos;

	vertexWorldPos = worldPos;
	cameraDir      = worldPos.xyz - cameraPosW;

#if (USE_SHADOWS == 1)
	shadowVertexPos = shadowMatrix * vertexWorldPos;
	shadowVertexPos.xy += vec2(0.5);
#endif

	texCoord0 = gl_MultiTexCoord0.st;

#if (DEFERRED_MODE == 0)
	float fogCoord = length(cameraDir.xyz);
	fogFactor = (fogParams.y - fogCoord) * fogParams.w; // fogParams: .y=end, .w=scale (1/(end-start))
	fogFactor = clamp(fogFactor, 0.0, 1.0);
#endif
}
