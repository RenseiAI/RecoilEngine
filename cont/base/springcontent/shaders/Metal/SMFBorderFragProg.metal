// SMFBorderFragProg.metal
// Translated from GLSL/SMFBorderFragProg.glsl
// SMF terrain border fragment shader

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float4 vVertCol;
    float2 vDiffuseUV;
    float2 vDetailsUV;
};

// SMF_INTENSITY_MULT
constant float4 diffuseMult = float4(110.0 / 255.0, 110.0 / 255.0, 110.0 / 255.0, 0.4);
constant float UV_BORDER_LEEWAY = 1e-2;

fragment float4 smfBorderFragProg(
    VertexOut in [[stage_in]],
    texture2d<float> diffuseTex [[texture(0)]],
    texture2d<float> detailsTex [[texture(1)]],
    sampler texSampler [[sampler(0)]])
{
    float2 cDiffuseUV = clamp(in.vDiffuseUV, float2(UV_BORDER_LEEWAY), float2(1.0 - UV_BORDER_LEEWAY));
    float4 diffuseCol = diffuseTex.sample(texSampler, cDiffuseUV) * diffuseMult;
    float4 detailsCol = detailsTex.sample(texSampler, in.vDetailsUV) * 2.0 - 1.0;

    return (diffuseCol + detailsCol) * in.vVertCol;
}
