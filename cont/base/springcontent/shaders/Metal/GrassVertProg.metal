// GrassVertProg.metal
// Translated from GLSL/GrassVertProg.glsl
// Grass vegetation vertex shader with wind animation

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float4 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float4 color [[attribute(2)]];
    float2 texCoord [[attribute(3)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
    float3 normal;
    float4 shadingTexCoords;
    float2 bladeTexCoords;
    float3 ambientDiffuseLightTerm;
#if defined(HAVE_SHADOWS) || defined(SHADOW_GEN)
    float4 shadowTexCoords;
#endif
    float fogFragCoord;
};

struct GrassUniforms {
    float4x4 modelViewMatrix;
    float4x4 projectionMatrix;
    float3x3 normalMatrix;
    float2 mapSizePO2;
    float2 mapSize;
    float4x4 shadowMatrix;
    float4 shadowParams;
    float3 camPos;
    float3 camUp;
    float3 camRight;
    float frame;
    float3 windSpeed;
    float3 sunDir;
    float3 ambientLightColor;
    float3 diffuseLightColor;
};

struct FogParams {
    float4 color;
    float start;
    float end;
    float scale;
    float density;
};

constant float PI = 3.14159265358979323846264;

// Crytek foliage bending functions
float3 ApplyMainBending(float3 vPos, float2 vWind, float fBendScale) {
    float fLength = length(vPos);
    float fBF = vPos.y * fBendScale + 1.0;
    fBF *= fBF;
    fBF = fBF * fBF - fBF;
    vPos.xz += vWind.xy * fBF;
    return normalize(vPos) * fLength;
}

float2 SmoothCurve(float2 x) {
    return x * x * (3.0 - 2.0 * x);
}

float2 TriangleWave(float2 x) {
    return abs(fract(x + 0.5) * 1.99 - 1.0);
}

float2 SmoothTriangleWave(float2 x) {
    return SmoothCurve(TriangleWave(x));
}

constant float2 V_FREQ = float2(1.975, 0.793);

void ApplyDetailBending(thread float3& vPos, float3 vNormal, float fDetailPhase, float fTime, float fSpeed, float fDetailAmp) {
    float vWavesIn = fTime + fDetailPhase;
    float2 vWaves = (fract(float2(vWavesIn) * V_FREQ) * 2.0 - 1.0) * fSpeed;
    vWaves = SmoothTriangleWave(vWaves);
    vPos.xyz += vNormal.xyz * float3(vWaves.xxy) * fDetailAmp;
}

vertex VertexOut grassVertProg(
    VertexIn in [[stage_in]],
    constant GrassUniforms& uniforms [[buffer(0)]],
    constant FogParams& fog [[buffer(1)]])
{
    VertexOut out;

    float2 texOffset = float2(0.0);
    out.color = in.color;

#ifndef DISTANCE_FAR
    // Mesh grass
    out.normal = uniforms.normalMatrix * in.normal;
    float4 worldPos = uniforms.modelViewMatrix * in.position;

    // Wind animation
    float3 objPos = float3x3(uniforms.modelViewMatrix[0].xyz,
                             uniforms.modelViewMatrix[1].xyz,
                             uniforms.modelViewMatrix[2].xyz) * in.position.xyz;
    worldPos.xyz += ApplyMainBending(objPos, uniforms.windSpeed.xz, in.texCoord.x * 0.004 + 0.007) - objPos;
    ApplyDetailBending(worldPos.xyz, out.normal, in.texCoord.x, uniforms.frame / 30.0, 0.3, in.texCoord.y * 0.4);

    // Lighting
    float fNdotL = dot(out.normal, uniforms.sunDir);
    float diffuseTerm = fNdotL * 0.4 + 0.6;
    diffuseTerm = max(diffuseTerm, ((-fNdotL) * 0.3 + 0.7) * 0.8);
    out.ambientDiffuseLightTerm = uniforms.ambientLightColor + diffuseTerm * uniforms.diffuseLightColor;
#else
    // Billboards
    out.color.a *= in.normal.z;
    float4 worldPos = in.position;

    float cosCamAngle = normalize(uniforms.camPos.xyz - worldPos.xyz).y;
    float ang = acos(-cosCamAngle);
    texOffset.x = clamp(floor((ang + PI / 16.0 - PI / 2.0) / PI * 30.0), 0.0, 15.0) / 16.0;

    float2 billboardSize = in.normal.xy;
    billboardSize.y = max(billboardSize.y, billboardSize.y * cosCamAngle);

    worldPos.xyz += uniforms.camRight * billboardSize.x;
    worldPos.xyz += uniforms.camUp * billboardSize.y;

    texOffset.y = max((0.5 * cosCamAngle - 0.5), -in.texCoord.y);

    float seed = fract(abs(dot(in.position.xyz, float3(1.0))));
    float3 objPos = (worldPos.xyz - in.position.xyz);
    worldPos.xyz += ApplyMainBending(objPos, uniforms.windSpeed.xz, seed * 0.006 + 0.01) - objPos;
    ApplyDetailBending(worldPos.xyz, float3(1.0, 0.0, 1.0), seed, uniforms.frame / 30.0, 0.3, 0.5 * max(1.0 - in.texCoord.y, cosCamAngle));

    worldPos.y += 5.0 * cosCamAngle;
    out.ambientDiffuseLightTerm = uniforms.ambientLightColor + uniforms.diffuseLightColor;
#endif

#if defined(HAVE_SHADOWS) || defined(SHADOW_GEN)
    float4 vertexShadowPos = uniforms.shadowMatrix * worldPos;
    vertexShadowPos.xy += uniforms.shadowParams.xy;
    out.shadowTexCoords = vertexShadowPos;
#endif

#ifdef SHADOW_GEN
    out.bladeTexCoords = in.texCoord + texOffset;
    out.position = uniforms.projectionMatrix * vertexShadowPos;
    return out;
#endif

    out.shadingTexCoords = worldPos.xzxz * float4(uniforms.mapSizePO2, uniforms.mapSize);
    out.bladeTexCoords = in.texCoord + texOffset;

    out.position = uniforms.projectionMatrix * worldPos;

    out.fogFragCoord = distance(uniforms.camPos, worldPos.xyz);
    out.fogFragCoord = (fog.end - out.fogFragCoord) * fog.scale;
    out.fogFragCoord = clamp(out.fogFragCoord, 0.0, 1.0);

    return out;
}
