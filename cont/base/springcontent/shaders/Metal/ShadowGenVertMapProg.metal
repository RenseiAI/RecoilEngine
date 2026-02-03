// ShadowGenVertMapProg.metal
// Translated from GLSL/ShadowGenVertMapProg.glsl
// Map shadow generation vertex shader

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 vertexPos [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
    float clipDistance [[clip_distance]] [1];
};

struct Uniforms {
    float4x4 modelViewMatrix;
    float4x4 projectionMatrix;
    int2 texSquare;
    float4 mapSize;  // mapSize, 1.0/mapSize
    float borderMinHeight;
};

constant float SMF_TEXSQR_SIZE = 1024.0;

float HeightAtWorldPos(float2 wxz, float4 mapSize, texture2d<float> heightMapTex, sampler texSampler) {
    const float2 HM_TEXEL = float2(8.0, 8.0);
    wxz += -HM_TEXEL * (wxz * mapSize.zw) + 0.5 * HM_TEXEL;

    float2 uvhm = clamp(wxz, HM_TEXEL, mapSize.xy - HM_TEXEL);
    uvhm *= mapSize.zw;

    return heightMapTex.sample(texSampler, uvhm, level(0.0)).x;
}

vertex VertexOut shadowGenVertMapProg(
    VertexIn in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]],
    texture2d<float> heightMapTex [[texture(0)]],
    sampler texSampler [[sampler(0)]])
{
    VertexOut out;

    float4 vertexWorldPos = float4(in.vertexPos, 1.0);
    vertexWorldPos.xz += float2(uniforms.texSquare) * SMF_TEXSQR_SIZE;

    // If y == 0, use height from heightmap, otherwise use borderMinHeight
    float heightFromMap = HeightAtWorldPos(vertexWorldPos.xz, uniforms.mapSize, heightMapTex, texSampler);
    vertexWorldPos.y = mix(uniforms.borderMinHeight, heightFromMap, float(vertexWorldPos.y == 0.0));

    float4 lightVertexPos = uniforms.modelViewMatrix * vertexWorldPos;

    lightVertexPos.xy += float2(0.5);

    out.position = uniforms.projectionMatrix * lightVertexPos;
    out.clipDistance[0] = vertexWorldPos.z;
    out.texCoord = in.texCoord;

    return out;
}
