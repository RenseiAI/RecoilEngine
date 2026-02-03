// GroundDecalsFragProg.metal
// Translated from GLSL/GroundDecalsFragProg.glsl
// Ground decals fragment shader (explosion scars, tracks, etc.)
// Note: This is a complex shader with many features

#include <metal_stdlib>
using namespace metal;

constant float PI = 3.1415926535897932384626433832795;
constant float goldenAngle = 2.3999632297286533222315555066336;
constant float DECAL_EXPLOSION = 2.0;
constant float SMF_INTENSITY_MULT = 210.0 / 255.0;
constant float SMF_SHALLOW_WATER_DEPTH = 10.0;
constant float SMF_SHALLOW_WATER_DEPTH_INV = 1.0 / SMF_SHALLOW_WATER_DEPTH;
constant float EPS = 3e-3;

struct VertexOut {
    float4 position [[position]];
    float4 vTranformedPos0 [[flat]];
    float4 vTranformedPos1 [[flat]];
    float4 vTranformedPos2 [[flat]];
    float4 vTranformedPos3 [[flat]];
    float4 vTranformedPos4 [[flat]];
    float4 vuvMain [[flat]];
    float4 vuvNorm [[flat]];
    float4 vData1;
    float4 vData2 [[flat]];
    float4 vData3 [[flat]];
    float4 vData4 [[flat]];
    float3x3 vRotMat [[flat]];
};

struct DecalsUniforms {
    float4x4 modelViewProjectionMatrixInverse;
    float4 mapDims;
    float4 mapDimsPO2;
    float infoTexIntensityMul;
    float4 groundAmbientColor;  // .w = groundShadowDensity
    float3 groundDiffuseColor;
    float3 sunDir;
    float2 screenSizeInverse;
    float curAdjustedFrame;
#ifdef SMF_WATER_ABSORPTION
    float3 waterMinColor;
    float3 waterBaseColor;
    float3 waterAbsorbColor;
#endif
#ifdef HAVE_SHADOWS
    float4x4 shadowMatrix;
#endif
};

#define NORM2SNORM(value) ((value) * 2.0 - 1.0)
#define SNORM2NORM(value) ((value) * 0.5 + 0.5)

float3 GetTriangleBarycentric(float3 p, float3 p0, float3 p1, float3 p2) {
    float3 v0 = p2 - p0;
    float3 v1 = p1 - p0;
    float3 v2 = p - p0;

    float dot00 = dot(v0, v0);
    float dot01 = dot(v0, v1);
    float dot02 = dot(v0, v2);
    float dot11 = dot(v1, v1);
    float dot12 = dot(v1, v2);

    float invDenom = 1.0 / (dot00 * dot11 - dot01 * dot01);

    float s = (dot11 * dot02 - dot01 * dot12) * invDenom;
    float t = (dot00 * dot12 - dot01 * dot02) * invDenom;
    float q = 1.0 - s - t;
    return float3(s, t, q);
}

float3 BlackBody(float t) {
    float u = (0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t * t)
        / (1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t * t);

    float v = (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t * t)
        / (1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t * t);

    float x = 3.0 * u / (2.0 * u - 8.0 * v + 4.0);
    float y = 2.0 * v / (2.0 * u - 8.0 * v + 4.0);
    float z = 1.0 - x - y;

    float Y = 1.0;
    float X = (Y / y) * x;
    float Z = (Y / y) * z;

    float3x3 XYZtosRGB = float3x3(
         3.2404542, -1.5371385, -0.4985314,
        -0.9692660,  1.8760108,  0.0415560,
         0.0556434, -0.2040259,  1.0572252
    );

    float3 RGB = float3(X, Y, Z) * XYZtosRGB;
    return RGB * pow(0.0004 * t, 4.0);
}

float3 GetFragmentNormal(float2 wxz, float4 mapDims, texture2d<float> groundNormalTex, sampler texSampler) {
    float3 normal;
    float4 sample = groundNormalTex.sample(texSampler, wxz * mapDims.zw, level(0.0));
    normal.xz = float2(sample.r, sample.a);
    normal.y = sqrt(1.0 - dot(normal.xz, normal.xz));
    return normal;
}

float3 GetWorldPos(float2 texCoord, float sampledDepth, float4x4 mvpInverse) {
    float4 projPosition = float4(0.0, 0.0, 0.0, 1.0);

#ifdef DEPTH_CLIP01
    projPosition.xyz = float3(NORM2SNORM(texCoord), sampledDepth);
#else
    projPosition.xyz = NORM2SNORM(float3(texCoord, sampledDepth));
#endif

    float4 pos = mvpInverse * projPosition;
    return pos.xyz / pos.w;
}

#ifdef HAVE_SHADOWS
float3 GetShadowColor(float3 worldPos, float NdotL, float4x4 shadowMatrix, float groundShadowDensity,
                      depth2d<float> shadowTex, texture2d<float> shadowColorTex, sampler shadowSampler) {
    float4 shadowPos = shadowMatrix * float4(worldPos, 1.0);
    shadowPos.xy += float2(0.5);
    shadowPos /= shadowPos.w;

    float3 shadowColor = shadowColorTex.sample(shadowSampler, shadowPos.xy).rgb;
    float shadowFactor = shadowTex.sample_compare(shadowSampler, shadowPos.xy, shadowPos.z);
    shadowFactor = min(shadowFactor, smoothstep(0.0, 0.35, NdotL));

    return mix(float3(1.0), shadowFactor * shadowColor, groundShadowDensity);
}
#endif

constant float3 all0 = float3(0.0);
constant float3 all1 = float3(1.0);

bool ProjectOntoPlane(float3 worldPos, float3 BL, float3 TL, float3 TR, float3 BR, float3 projPlane, float4 midPoint, float u, thread float4& relUV) {
    float3 worldPosProj = worldPos - dot(worldPos - midPoint.xyz, projPlane) * projPlane;

    float3 bc = GetTriangleBarycentric(worldPosProj, BL, TL, TR);
    if (all(bc >= all0) && all(bc <= all1)) {
        relUV = bc.x * float4(0, u, 0, 1) + bc.y * float4(0, 0, 0, 0) + bc.z * float4(1, 0, 1, 0);
        return true;
    }

    bc = GetTriangleBarycentric(worldPosProj, TR, BR, BL);
    if (all(bc >= all0) && all(bc <= all1)) {
        relUV = bc.x * float4(1, 0, 1, 0) + bc.y * float4(1, u, 1, 1) + bc.z * float4(0, u, 0, 1);
        return true;
    }

    return false;
}

float4 GetColorByRelUV(float2 uvTL, float2 uvBL, float2 uvTR, float2 uvBR, float4 relUV,
#ifdef USE_TEXTURE_ARRAY
                       texture2d_array<float> atlasTex,
#else
                       texture2d<float> atlasTex,
#endif
                       sampler texSampler) {
#ifdef USE_TEXTURE_ARRAY
    float layer = floor(uvTL.x);
    uvTL.x = fract(uvTL.x);
    uvBL.x = fract(uvBL.x);
    float2 uv = mix(mix(uvTL, uvBL, relUV.x), mix(uvTR, uvBR, relUV.x), relUV.y);
    return atlasTex.sample(texSampler, uv, uint(layer));
#else
    float2 uv = mix(mix(uvTL, uvBL, relUV.x), mix(uvTR, uvBR, relUV.x), relUV.y);
    return atlasTex.sample(texSampler, uv);
#endif
}

float EllipsoidReduction(float3 xyz, float3 abc) {
    float s = (xyz.x*xyz.x) / (abc.x*abc.x) + (xyz.y*xyz.y) / (abc.y*abc.y) + (xyz.z*xyz.z) / (abc.z*abc.z);
    return 2.0 - max(s, 1.0);
}

fragment float4 groundDecalsFragProg(
    VertexOut in [[stage_in]],
    constant DecalsUniforms& uniforms [[buffer(0)]],
#ifdef HIGH_QUALITY
    texture2d_ms<float> depthTex [[texture(0)]],
#else
    texture2d<float> depthTex [[texture(1)]],
#endif
#ifdef USE_TEXTURE_ARRAY
    texture2d_array<float> atlasTex [[texture(2)]],
#else
    texture2d<float> atlasTex [[texture(2)]],
#endif
    texture2d<float> groundNormalTex [[texture(3)]],
    texture2d<float> miniMapTex [[texture(4)]],
    texture2d<float> infoTex [[texture(5)]],
#ifdef HAVE_SHADOWS
    depth2d<float> shadowTex [[texture(6)]],
    texture2d<float> shadowColorTex [[texture(7)]],
#endif
    sampler texSampler [[sampler(0)]]
#ifdef HAVE_SHADOWS
    , sampler shadowSampler [[sampler(1)]]
#endif
)
{
    // Get world position from depth buffer
#ifdef HIGH_QUALITY
    float depthZO = depthTex.read(uint2(in.position.xy), 0).x;
#else
    float depthZO = depthTex.read(uint2(in.position.xy)).x;
#endif
    float3 worldPos = GetWorldPos(in.position.xy * uniforms.screenSizeInverse, depthZO, uniforms.modelViewProjectionMatrixInverse);

    // Extract UV coordinates
    float4 uvBL = float4(in.vuvMain.xw, in.vuvNorm.xw);
    float4 uvTL = float4(in.vuvMain.xy, in.vuvNorm.xy);
    float4 uvTR = float4(in.vuvMain.zy, in.vuvNorm.zy);
    float4 uvBR = float4(in.vuvMain.zw, in.vuvNorm.zw);

    float u = 1.0;
    float vUVWrapDist = in.vData2.y;
    if (vUVWrapDist > 0.0) {
        u = distance((in.vTranformedPos1.xyz + in.vTranformedPos2.xyz) * 0.5, (in.vTranformedPos3.xyz + in.vTranformedPos4.xyz) * 0.5) / vUVWrapDist;
    }

    float4 relUV;
    if (!ProjectOntoPlane(worldPos, in.vTranformedPos1.xyz, in.vTranformedPos2.xyz, in.vTranformedPos3.xyz, in.vTranformedPos4.xyz, in.vRotMat[1], in.vTranformedPos0, u, relUV)) {
        return float4(0.0);
    }

    if (vUVWrapDist > 0.0) {
        float vUVOffset = in.vData2.z;
        relUV.y = fmod(vUVOffset / vUVWrapDist + relUV.y, 1.0);
    }

    float vAlpha = in.vData1.x;
    float vGlow = in.vData1.y;
    float vDecalType = in.vData2.w;
    float vHeight = in.vData2.x;

    float alpha = clamp(vAlpha, 0.0, 1.0);
    float glow = clamp(vGlow, 0.0, 1.0);

    float4 mainCol = GetColorByRelUV(uvTL.xy, uvBL.xy, uvTR.xy, uvBR.xy, relUV, atlasTex, texSampler);
    float4 normVal = GetColorByRelUV(uvTL.zw, uvBL.zw, uvTR.zw, uvBR.zw, relUV, atlasTex, texSampler);

    mainCol *= 2.0 * in.vData3;  // vTintColor

    float3 mapDiffuse = miniMapTex.sample(texSampler, worldPos.xz * uniforms.mapDims.zw, level(0.0)).rgb;
    float3 mapDecalMix = 2.0 * mainCol.rgb * mapDiffuse.rgb;
    mainCol.rgb = mix(mainCol.rgb, mapDecalMix, float(vDecalType == DECAL_EXPLOSION));

    float3 N = GetFragmentNormal(worldPos.xz, uniforms.mapDims, groundNormalTex, texSampler);
    float3 T = normalize(in.vRotMat[0] - N * dot(in.vRotMat[0], N));
    float3 B = normalize(cross(N, T));

    float3x3 TBN = float3x3(T, B, N);
    float3 decalNormal = normalize(TBN * NORM2SNORM(normVal.xyz));
    float NdotL = max(dot(uniforms.sunDir, decalNormal), 0.0);
    float3 diffuseTerm = NdotL * uniforms.groundDiffuseColor;

#ifdef HAVE_SHADOWS
    float3 shadowCol = GetShadowColor(worldPos.xyz, dot(uniforms.sunDir, N), uniforms.shadowMatrix, uniforms.groundAmbientColor.w, shadowTex, shadowColorTex, shadowSampler);
#else
    float3 shadowCol = float3(1.0);
#endif

    float3 lightCol = diffuseTerm * shadowCol + uniforms.groundAmbientColor.rgb;

    float4 fragColor;
    fragColor.rgb = mainCol.rgb * lightCol;

    // Glow effect
    if (in.vData4.a == 0.0) {
        glow += smoothstep(0.75, 1.0, glow) * 0.2 * abs(sin(0.02 * uniforms.curAdjustedFrame));
        float relDistance = distance(worldPos.xyz, in.vTranformedPos0.xyz) / in.vTranformedPos0.w;
        relDistance = smoothstep(0.9, 0.1, relDistance);
        glow *= pow(relDistance, 7.0);
        glow *= smoothstep(-SMF_SHALLOW_WATER_DEPTH, 0.0, worldPos.y);

        float t = mix(1.0, 3700.0, glow);
        fragColor.rgb += BlackBody(normVal.w * t) * glow;
    } else {
        fragColor.rgb = mix(fragColor.rgb, in.vData4.rgb * normVal.w * glow, in.vData4.a);
    }

#ifdef SMF_WATER_ABSORPTION
    if (worldPos.y <= 0.0 && vDecalType == DECAL_EXPLOSION) {
        float3 waterShadeInt = uniforms.waterBaseColor;
        float waterShadeAlpha = -worldPos.y * SMF_SHALLOW_WATER_DEPTH_INV;
        float waterShadeDecay = 0.2 + (waterShadeAlpha * 0.1);
        float vertexStepHeight = min(1023.0, -worldPos.y);
        float waterLightInt = min(NdotL * 2.0 + 0.4, 1.0);

        waterShadeAlpha = min(1.0, waterShadeAlpha + float(worldPos.y <= -SMF_SHALLOW_WATER_DEPTH));
        waterShadeInt -= (uniforms.waterAbsorbColor * vertexStepHeight);
        waterShadeInt = max(uniforms.waterMinColor, waterShadeInt);
        waterShadeInt *= float3(SMF_INTENSITY_MULT * waterLightInt);
        waterShadeInt *= (1.0 - waterShadeDecay);

        fragColor.rgb = mix(fragColor.rgb, fragColor.rgb * waterShadeInt, waterShadeAlpha);
    }
#endif

    fragColor.a = mainCol.a * alpha;

    // Ellipsoid reduction for explosions
    float3 rotWorldPos = transpose(in.vRotMat) * (worldPos - in.vTranformedPos0.xyz);
    float3 ellipseAxes = float3(in.vTranformedPos1.w, vHeight, in.vTranformedPos2.w);
    fragColor.a *= mix(1.0, EllipsoidReduction(rotWorldPos, ellipseAxes), float(vDecalType == DECAL_EXPLOSION));

    float vDotElimExp = in.vData1.z;
    fragColor *= mix(1.0, pow(max(dot(in.vRotMat[1], N), 0.0), vDotElimExp), float(vDotElimExp > 0.0));

    // Edge smoothing
    fragColor.a *= smoothstep(0.0, EPS, relUV.x) * (1.0 - smoothstep(1.0 - EPS, 1.0, relUV.x)) *
                   smoothstep(0.0, EPS, relUV.y) * (1.0 - smoothstep(1.0 - EPS, 1.0, relUV.y));

    return fragColor;
}
