// SMFVertProg.metal
// Translated from GLSL/SMFVertProg.glsl
// SMF terrain vertex shader

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 vertexPos [[attribute(0)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 halfDir;
    float fogFactor;
    float4 vertexWorldPos;
    float2 diffuseTexCoords;
    float clipDistance [[clip_distance]] [1];
};

struct Uniforms {
    float4x4 modelViewMatrix;
    float4x4 projectionMatrix;
    float4x4 modelViewMatrixInverse;
    float4x4 modelViewProjectionMatrix;
    int2 texSquare;
    float4 lightDir;
    float2 specularTexGen;  // 1.0/mapSize
};

struct FogParams {
    float4 color;
    float start;
    float end;
    float scale;
    float density;
};

constant float SMF_TEXSQR_SIZE = 1024.0;
constant float SMF_DETAILTEX_RES = 0.02;

float HeightAtWorldPos(float2 wxz, float2 specularTexGen, float2 mapSize, texture2d<float> heightMapTex, sampler texSampler) {
    const float2 HM_TEXEL = float2(8.0, 8.0);
    wxz += -HM_TEXEL * (wxz * specularTexGen) + 0.5 * HM_TEXEL;

    float2 uvhm = clamp(wxz, HM_TEXEL, mapSize - HM_TEXEL);
    uvhm *= specularTexGen;

    return heightMapTex.sample(texSampler, uvhm, level(0.0)).x;
}

vertex VertexOut smfVertProg(
    VertexIn in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]],
    constant FogParams& fog [[buffer(1)]],
    texture2d<float> heightMapTex [[texture(0)]],
    sampler texSampler [[sampler(0)]])
{
    VertexOut out;

    float2 mapSize = float2(1.0) / uniforms.specularTexGen;

    // Calculate view direction
    float3 viewDir = float3(uniforms.modelViewMatrixInverse * float4(0.0, 0.0, 0.0, 1.0));

    out.vertexWorldPos = float4(in.vertexPos, 1.0);
    out.vertexWorldPos.xz += float2(uniforms.texSquare) * SMF_TEXSQR_SIZE;
    out.vertexWorldPos.y = HeightAtWorldPos(out.vertexWorldPos.xz, uniforms.specularTexGen, mapSize, heightMapTex, texSampler);

    viewDir = normalize(viewDir - out.vertexWorldPos.xyz);
    out.halfDir = normalize(uniforms.lightDir.xyz + viewDir);

    // Calculate texture coordinates
    out.diffuseTexCoords = (out.vertexWorldPos.xz / SMF_TEXSQR_SIZE) - float2(uniforms.texSquare);

    // Transform vertex
    out.position = uniforms.modelViewProjectionMatrix * out.vertexWorldPos;

    float4 clipVertex = uniforms.modelViewMatrix * out.vertexWorldPos;
    out.clipDistance[0] = clipVertex.z;

#ifndef DEFERRED_MODE
    // Emulate linear fog
    float fogCoord = length(clipVertex.xyz);
    out.fogFactor = (fog.end - fogCoord) * fog.scale;
    out.fogFactor = clamp(out.fogFactor, 0.0, 1.0);
#else
    out.fogFactor = 1.0;
#endif

    return out;
}
