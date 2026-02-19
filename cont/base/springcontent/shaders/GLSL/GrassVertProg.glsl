#version 130

uniform mat4 modelViewMatrix = mat4(1.0);
uniform mat4 projectionMatrix = mat4(1.0);
uniform mat3 normalMatrix = mat3(1.0);

uniform vec2 mapSizePO2;     // (1.0 / pwr2map{x,z} * SQUARE_SIZE)
uniform vec2 mapSize;        // (1.0 /     map{x,z} * SQUARE_SIZE)

uniform mat4 shadowMatrix;
uniform vec4 shadowParams;

uniform vec3 camPos;
uniform vec3 camUp;
uniform vec3 camRight;

uniform float frame;
uniform vec3 windSpeed;

uniform vec3 sunDir;
uniform vec3 ambientLightColor;
uniform vec3 diffuseLightColor;
uniform vec4 fogParams; //%.x=start, .y=end, .z=unused, .w=scale (1/(end-start))

out vec3 normal;
out vec4 shadingTexCoords;
out vec2 bladeTexCoords;
out vec3 ambientDiffuseLightTerm;
out float alphaFade;
out float fogFactor;
#if defined(HAVE_SHADOWS) || defined(SHADOW_GEN)
  out vec4 shadowTexCoords;
#endif


const float PI = 3.14159265358979323846264;


//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
// Crytek - foliage animation
// src: http://http.developer.nvidia.com/GPUGems3/gpugems3_ch16.html
//

// This bends the entire plant in the direction of the wind.
// vPos: The world position of the plant *relative* to the base of the plant.
vec3 ApplyMainBending(in vec3 vPos, in vec2 vWind, in float fBendScale)
{
	float fLength = length(vPos);
	float fBF = vPos.y * fBendScale + 1.0;
	fBF *= fBF;
	fBF  = fBF * fBF - fBF;
	vPos.xz += vWind.xy * fBF;
	return normalize(vPos) * fLength;
}

vec2 SmoothCurve( vec2 x ) {
	return x * x * (3.0 - 2.0 * x);
}
vec2 TriangleWave( vec2 x ) {
	return abs( fract( x + 0.5 ) * 1.99 - 1.0 );
}
vec2 SmoothTriangleWave( vec2 x ) {
	// similar to sine wave, but faster
	return SmoothCurve( TriangleWave( x ) );
}

const vec2 V_FREQ = vec2(1.975, 0.793);

// This provides "chaotic" motion for leaves and branches (the entire plant, really)
void ApplyDetailBending(inout vec3 vPos, vec3 vNormal, float fDetailPhase, float fTime, float fSpeed, float fDetailAmp)
{
	float vWavesIn = fTime + fDetailPhase;
	vec2 vWaves = (fract( vec2(vWavesIn) * V_FREQ ) * 2.0 - 1.0 ) * fSpeed;
	vWaves = SmoothTriangleWave( vWaves );
	vPos.xyz += vNormal.xyz * vWaves.xxy * fDetailAmp;
}


//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////


void main() {
	vec2 texOffset = vec2(0.);
	alphaFade = gl_Color.a;

#ifndef DISTANCE_FAR
	// mesh grass
	normal = normalMatrix * gl_Normal;
	vec4 worldPos = modelViewMatrix * gl_Vertex;

	// anim
	vec3 objPos = mat3(modelViewMatrix) * gl_Vertex.xyz;
	worldPos.xyz += ApplyMainBending(objPos, windSpeed.xz, gl_MultiTexCoord0.s * 0.004 + 0.007) - objPos;
	ApplyDetailBending(worldPos.xyz, normal,
			gl_MultiTexCoord0.s,
			frame / 30.0,
			0.3,
			gl_MultiTexCoord0.t * 0.4);

	// compute ambient & diffuse lighting per-vertex, specular is per-pixel
	float fNdotL  = dot(normal, sunDir);
	float diffuseTerm = fNdotL * 0.4 + 0.6; // front surface //TODO make constants customizable?
	diffuseTerm = max(diffuseTerm, ((-fNdotL) * 0.3 + 0.7) * 0.8); // back surface //TODO make constants customizable?
	ambientDiffuseLightTerm = ambientLightColor + diffuseTerm * diffuseLightColor;
#else
	// billboards
	alphaFade *= gl_Normal.z; // alpha blend far turfs
	vec4 worldPos = /* modelViewMatrix * */ gl_Vertex; // MVM is identity in far draw pass

	// get the camera angle on the billboard and select the corresponding sprite
	float cosCamAngle = normalize(camPos.xyz - worldPos.xyz).y;
	float ang = acos(-cosCamAngle);
	texOffset.s = clamp(floor((ang + PI / 16.0 - PI / 2.0) / PI * 30.0), 0.0, 15.0) / 16.0;

	// billboard size
	vec2 billboardSize = gl_Normal.xy;

	// cut of lower half in horizontal views (the fartexture is empty in lower 50% in horizontal view!)
	billboardSize.y = max(billboardSize.y, billboardSize.y * cosCamAngle);

	// span the billboard
	worldPos.xyz += camRight * billboardSize.x;
	worldPos.xyz += camUp    * billboardSize.y;

	// adjust texcoord for cut of billboard
	texOffset.t = max((0.5 * cosCamAngle - 0.5), -gl_MultiTexCoord0.t);

	// anim
	float seed = fract(abs(dot(gl_Vertex.xyz, vec3(1.0))));
	vec3 objPos = (worldPos.xyz - gl_Vertex.xyz);
	worldPos.xyz += ApplyMainBending(objPos, windSpeed.xz, seed * 0.006 + 0.01) - objPos;
	ApplyDetailBending(worldPos.xyz, vec3(1., 0., 1.),
			seed,
			frame / 30.0,
			0.3,
			0.5 * max(1.0 - gl_MultiTexCoord0.t, cosCamAngle));

	// move up when looking down (to fix clipping issues)
	worldPos.y   += 5.0 * cosCamAngle;

	// compute ambient & diffuse lighting per-vertex
	ambientDiffuseLightTerm = ambientLightColor + diffuseLightColor;
#endif

#if defined(HAVE_SHADOWS) || defined(SHADOW_GEN)
	vec4 vertexShadowPos = shadowMatrix * worldPos;

	vertexShadowPos.xy += shadowParams.xy;
	shadowTexCoords = vertexShadowPos;
#endif

#ifdef SHADOW_GEN
	{
		bladeTexCoords = gl_MultiTexCoord0.st + texOffset;
		gl_Position = projectionMatrix * vertexShadowPos;
		return;
	}
#endif

	shadingTexCoords = worldPos.xzxz * vec4(mapSizePO2, mapSize);
	bladeTexCoords   = gl_MultiTexCoord0.st + texOffset;

	gl_Position = projectionMatrix * worldPos;

	fogFactor = distance(camPos, worldPos.xyz);
	fogFactor = (fogParams.y - fogFactor) * fogParams.w; // fogParams: .y=end, .w=scale
	fogFactor = clamp(fogFactor, 0.0, 1.0);
}
