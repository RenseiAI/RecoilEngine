#version 130
#extension GL_ARB_explicit_attrib_location : require

#define textureS3o1 diffuseTex
#define textureS3o2 shadingTex
	uniform sampler2D textureS3o1;
	uniform sampler2D textureS3o2;
	uniform samplerCube specularTex;
	uniform samplerCube reflectTex;

	uniform vec3 sunDir;
	uniform vec3 sunDiffuse;
	uniform vec3 sunAmbient;
	uniform vec3 sunSpecular;
#if (USE_SHADOWS == 1)
	in vec4 shadowVertexPos;
	uniform sampler2DShadow shadowTex;
	uniform sampler2D shadowColorTex;
	uniform float shadowDensity;
#endif

// Dynamic point/spot lights (replaces gl_LightSource[] FFP built-in)
#if (MAX_DYNAMIC_MODEL_LIGHTS > 0)
	uniform vec4 dynLightPosition[MAX_DYNAMIC_MODEL_LIGHTS];    // .xyz = world position
	uniform vec4 dynLightAmbient[MAX_DYNAMIC_MODEL_LIGHTS];     // .rgb = ambient color
	uniform vec4 dynLightDiffuse[MAX_DYNAMIC_MODEL_LIGHTS];     // .rgb = diffuse color
	uniform vec4 dynLightSpecular[MAX_DYNAMIC_MODEL_LIGHTS];    // .rgb = specular color
	uniform vec4 dynLightSpotParams[MAX_DYNAMIC_MODEL_LIGHTS];  // .xyz = spotDirection, .w = spotCosCutoff
	uniform vec4 dynLightAttenuation[MAX_DYNAMIC_MODEL_LIGHTS]; // .x = radius (or constAtten), .y = linearAtten, .z = quadAtten
#endif

// in opaque passes tc.a is always 1.0 [all objects], and alphaPass is 0.0
// in alpha passes tc.a is either one of alphaValues.xyzw [for units] *or*
// contains a distance fading factor [for features], and alphaPass is 1.0
// texture alpha-masking is done in both passes
uniform vec4 teamColor;
uniform vec4 nanoColor;
uniform vec4 alphaCtrl = vec4(0.0, 0.0, 0.0, 1.0); //always pass
uniform vec4 colorMult = vec4(1.0, 1.0, 1.0, 1.0); // ghost dimming, build preview alpha
uniform vec4 fogColor;

bool AlphaDiscard(float a) {
	float alphaTestGT = float(a > alphaCtrl.x) * alphaCtrl.y;
	float alphaTestLT = float(a < alphaCtrl.x) * alphaCtrl.z;

	return ((alphaTestGT + alphaTestLT + alphaCtrl.w) == 0.0);
}

in vec4 vertexWorldPos;
in vec3 cameraDir;
in float fogFactor;
in vec3 normalv;
in vec2 texCoord0;

// Fragment outputs
#if (DEFERRED_MODE == 1)
layout(location = GBUFFER_NORMTEX_IDX) out vec4 gbufferNormal;
layout(location = GBUFFER_DIFFTEX_IDX) out vec4 gbufferDiffuse;
layout(location = GBUFFER_SPECTEX_IDX) out vec4 gbufferSpecular;
layout(location = GBUFFER_EMITTEX_IDX) out vec4 gbufferEmissive;
layout(location = GBUFFER_MISCTEX_IDX) out vec4 gbufferMisc;
#else
out vec4 fragColor;
#endif

vec3 GetShadowMult(float NdotL) {
	#if (USE_SHADOWS == 1)
		vec3 shadowCoord = shadowVertexPos.xyz / shadowVertexPos.w;
		float sh = min(shadow2DProj(shadowTex, shadowVertexPos).r, smoothstep(0.0, 0.35, NdotL));
		vec3 shColor = texture2D(shadowColorTex, shadowCoord.xy).rgb;
		return mix(1.0, sh, shadowDensity) * shColor;
	#else
		return vec3(1.0);
	#endif
}

vec3 DynamicLighting(vec3 normal, vec3 diffuse, vec3 specular) {
	vec3 rgb = vec3(0.0);

	for (int i = 0; i < MAX_DYNAMIC_MODEL_LIGHTS; i++) {
		vec3 lightVec = dynLightPosition[i].xyz - vertexWorldPos.xyz;
		float lightDistance = length(lightVec);
		vec3 lightDir = lightVec / max(lightDistance, 1e-6);

		// compute half-vector from light direction + view direction (replaces FFP halfVector)
		vec3 viewDir = normalize(-cameraDir);
		vec3 halfVec = normalize(lightDir + viewDir);

		float lightRadius   = dynLightAttenuation[i].x;
		float lightScale    = float(lightDistance <= lightRadius);

		float lightCosAngDiff = clamp(dot(normal, lightDir), 0.0, 1.0);
		float lightCosAngSpec = clamp(dot(normal, halfVec), 0.0, 1.0);
		#ifdef OGL_SPEC_ATTENUATION
		float lightAttenuation =
			(dynLightAttenuation[i].x) +
			(dynLightAttenuation[i].y * lightDistance) +
			(dynLightAttenuation[i].z * lightDistance * lightDistance);

		lightAttenuation = 1.0 / max(lightAttenuation, 1.0);
		#else
		float lightAttenuation = 1.0 - min(1.0, ((lightDistance * lightDistance) / (lightRadius * lightRadius)));
		#endif

		float vectorDot = dot(-lightDir, dynLightSpotParams[i].xyz);
		float cutoffDot = dynLightSpotParams[i].w;

		lightScale *= float(vectorDot >= cutoffDot);

		rgb += (lightScale *                                    dynLightAmbient[i].rgb);
		rgb += (lightScale * lightAttenuation * (diffuse.rgb  * dynLightDiffuse[i].rgb * lightCosAngDiff));
		rgb += (lightScale * lightAttenuation * (specular.rgb * dynLightSpecular[i].rgb * pow(lightCosAngSpec, 4.0)));
	}

	return rgb;
}

void main(void)
{
	vec3 normal = normalize(normalv);

	float NdotLu = dot(normal, sunDir);
	float NdotL = max(NdotLu, 1e-3);
	vec3 light = NdotL * sunDiffuse + sunAmbient;

	vec4 diffuse     = texture2D(textureS3o1, texCoord0);
	vec4 extraColor  = texture2D(textureS3o2, texCoord0);

	vec3 reflectDir = reflect(cameraDir, normal);
	vec3 specular   = textureCube(specularTex, reflectDir).rgb * sunSpecular;
	vec3 reflection = textureCube(reflectTex,  reflectDir).rgb;

	vec3 shadowMult = GetShadowMult(NdotL);
	float alpha = teamColor.a * extraColor.a; // apply one-bit mask

	if (AlphaDiscard(alpha))
		discard;

	specular *= (extraColor.g * 4.0);
	// no highlights if in shadowMult; decrease light to ambient level
	specular *= shadowMult;
	light = mix(sunAmbient, light, shadowMult);


	reflection  = mix(light, reflection, extraColor.g); // reflection
	reflection += extraColor.rrr; // self-illum

#if (DEFERRED_MODE == 0)
	fragColor     = diffuse;
	fragColor.rgb = mix(fragColor.rgb, teamColor.rgb, fragColor.a); // teamcolor
	fragColor.rgb = fragColor.rgb * reflection + specular;
#endif

#if (DEFERRED_MODE == 0 && MAX_DYNAMIC_MODEL_LIGHTS > 0)
	fragColor.rgb += DynamicLighting(normal, diffuse.rgb, specular);
#endif

#if (DEFERRED_MODE == 1)
	gbufferNormal  = vec4((normal + vec3(1.0, 1.0, 1.0)) * 0.5, 1.0);
	gbufferDiffuse = colorMult * vec4(mix(                         diffuse.rgb, teamColor.rgb,   diffuse.a), alpha);
	gbufferDiffuse = vec4(mix(gbufferDiffuse.rgb, nanoColor.rgb, nanoColor.a), alpha);
	// do not premultiply reflection, leave it to the deferred lighting pass
	// gbufferDiffuse = vec4(mix(diffuse.rgb, teamColor.rgb, diffuse.a) * reflection, alpha);
	// allows standard-lighting reconstruction by lazy LuaMaterials using us
	gbufferSpecular = vec4(extraColor.rgb, alpha);
	gbufferEmissive = vec4(0.0, 0.0, 0.0, 0.0);
	gbufferMisc     = vec4(0.0, 0.0, 0.0, 0.0);
#else
	fragColor.rgb = mix(fogColor.rgb, fragColor.rgb, fogFactor); // fog
	fragColor.rgb = mix(fragColor.rgb, nanoColor.rgb, nanoColor.a); // wireframe or polygon color
	fragColor.a   = alpha;
	fragColor    *= colorMult; // ghost dimming, build preview alpha
#endif
}
