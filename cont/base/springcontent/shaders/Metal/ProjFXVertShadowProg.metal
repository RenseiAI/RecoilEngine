// ProjFXVertShadowProg.metal
// Translated from GLSL/ProjFXVertShadowProg.glsl
// Projectile effects shadow vertex shader

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 pos [[attribute(0)]];
    float3 uvw [[attribute(1)]];
    float4 uvInfo [[attribute(2)]];
    float3 aparams [[attribute(3)]];
    float4 color [[attribute(4)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 vCol;
    float4 vUV;
    float vLayer;
    float vBF;
};

struct ProjFXShadowUniforms {
    float4x4 modelViewMatrix;
    float4x4 projectionMatrix;
};

vertex VertexOut projFXVertShadowProg(
    VertexIn in [[stage_in]],
    constant ProjFXShadowUniforms& uniforms [[buffer(0)]])
{
    VertexOut out;

    float ap = fract(in.aparams.z);

    float maxImgIdx = in.aparams.x * in.aparams.y - 1.0;
    ap *= maxImgIdx;

    float i0 = floor(ap);
    float i1 = i0 + 1.0;

    out.vBF = fract(ap);

    if (maxImgIdx > 1.0) {
        float2 uvDiff = (in.uvw.xy - in.uvInfo.xy);
        out.vUV = uvDiff.xyxy + float4(
            floor(fmod(i0, in.aparams.x)),
            floor(i0 / in.aparams.x),
            floor(fmod(i1, in.aparams.x)),
            floor(i1 / in.aparams.x)
        ) * in.uvInfo.zwzw;
        out.vUV /= in.aparams.xyxy;
        out.vUV += in.uvInfo.xyxy;
    } else {
        out.vUV = in.uvw.xyxy;
    }

    out.vLayer = in.uvw.z;
    out.vCol = in.color;

    float4 lightVertexPos = uniforms.modelViewMatrix * float4(in.pos, 1.0);
    lightVertexPos.xy += float2(0.5);
    out.position = uniforms.projectionMatrix * lightVertexPos;

    return out;
}
