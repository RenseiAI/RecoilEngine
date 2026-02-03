// ProjFXVertProg.metal
// Translated from GLSL/ProjFXVertProg.glsl
// Projectile effects vertex shader

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
    float4 vUV [[centroid_perspective]];
    float vLayer;
    float vBF;
    float fogFactor;
    float4 vsPos;
    float2 screenUV [[center_no_perspective]];
    float clipDistance [[clip_distance]] [1];
};

struct ProjFXUniforms {
    float4x4 modelViewMatrix;
    float4x4 projectionMatrix;
    float2 fogParams;
    float3 camPos;
    float4 clipPlane;
};

#define NORM2SNORM(value) ((value) * 2.0 - 1.0)
#define SNORM2NORM(value) ((value) * 0.5 + 0.5)

vertex VertexOut projFXVertProg(
    VertexIn in [[stage_in]],
    constant ProjFXUniforms& uniforms [[buffer(0)]])
{
    VertexOut out;

    float ap = fract(in.aparams.z);

    float maxImgIdx = in.aparams.x * in.aparams.y - 1.0;
    ap *= maxImgIdx;

    float i0 = floor(ap);
    float i1 = i0 + 1.0;

    out.vBF = fract(ap);  // blending factor

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

    float fogDist = length(in.pos - uniforms.camPos);
    out.fogFactor = (uniforms.fogParams.y - fogDist) / (uniforms.fogParams.y - uniforms.fogParams.x);
    out.fogFactor = clamp(out.fogFactor, 0.0, 1.0);

    out.clipDistance[0] = dot(float4(in.pos, 1.0), uniforms.clipPlane);

    out.vsPos = uniforms.modelViewMatrix * float4(in.pos, 1.0);
    out.position = uniforms.projectionMatrix * out.vsPos;
    out.screenUV = SNORM2NORM(out.position.xy / out.position.w);

    return out;
}
