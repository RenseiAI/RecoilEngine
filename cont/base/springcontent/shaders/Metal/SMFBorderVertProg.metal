// SMFBorderVertProg.metal
// Translated from GLSL/SMFBorderVertProg.glsl
// SMF terrain border vertex shader

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 vertexPos [[attribute(0)]];
    float4 vertexCol [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 vVertCol;
    float2 vDiffuseUV;
    float2 vDetailsUV;
};

struct SMFBorderUniforms {
    float4x4 modelViewProjectionMatrix;
    int2 texSquare;
    float4 mapSize;  // mapSize, 1.0/mapSize
    float borderMinHeight;
};

constant float SMF_TEXSQR_SIZE = 1024.0;
constant float4 detailPlaneS = float4(0.005, 0.000, 0.005, 0.5);
constant float4 detailPlaneT = float4(0.000, 0.005, 0.000, 0.5);

float HeightAtWorldPos(float2 wxz, float4 mapSize, texture2d<float> heightMapTex, sampler texSampler) {
    const float2 HM_TEXEL = float2(8.0, 8.0);
    wxz += -HM_TEXEL * (wxz * mapSize.zw) + 0.5 * HM_TEXEL;

    float2 uvhm = clamp(wxz, HM_TEXEL, mapSize.xy - HM_TEXEL);
    uvhm *= mapSize.zw;

    return heightMapTex.sample(texSampler, uvhm, level(0.0)).x;
}

vertex VertexOut smfBorderVertProg(
    VertexIn in [[stage_in]],
    constant SMFBorderUniforms& uniforms [[buffer(0)]],
    texture2d<float> heightMapTex [[texture(0)]],
    sampler texSampler [[sampler(0)]])
{
    VertexOut out;

    float4 vertexWorldPos = float4(in.vertexPos, 1.0);
    vertexWorldPos.xz += float2(uniforms.texSquare) * SMF_TEXSQR_SIZE;
    vertexWorldPos.y = mix(uniforms.borderMinHeight, HeightAtWorldPos(vertexWorldPos.xz, uniforms.mapSize, heightMapTex, texSampler), float(vertexWorldPos.y == 0.0));

    out.vVertCol = in.vertexCol;
    out.vDiffuseUV = (vertexWorldPos.xz * (1.0 / SMF_TEXSQR_SIZE)) - float2(uniforms.texSquare);
    out.vDetailsUV = float2(
        dot(vertexWorldPos, detailPlaneS),
        dot(vertexWorldPos, detailPlaneT)
    );

    out.position = uniforms.modelViewProjectionMatrix * vertexWorldPos;

    return out;
}
