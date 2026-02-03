// SMFShadingTextureVertProg.metal
// Translated from GLSL/SMFShadingTextureVertProg.glsl
// SMF shading texture generation vertex shader

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 pos [[attribute(0)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 mapUV;
};

struct ShadingTextureUniforms {
    float4 mapSizeP1;  // mapSizeP1.xy = size, mapSizeP1.zw = 1/size
};

#define NORM2SNORM(value) ((value) * 2.0 - 1.0)
#define SNORM2NORM(value) ((value) * 0.5 + 0.5)

vertex VertexOut smfShadingTextureVertProg(
    VertexIn in [[stage_in]],
    constant ShadingTextureUniforms& uniforms [[buffer(0)]])
{
    VertexOut out;

    // pos is 0 -> 1
    out.mapUV = in.pos * uniforms.mapSizeP1.zw;

    out.position = float4(NORM2SNORM(out.mapUV), 0.0, 1.0);

    return out;
}
