// ShadowGenVertProg.metal
// Translated from GLSL/ShadowGenVertProg.glsl
// Legacy shadow generation vertex shader

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float4 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 texCoord [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
    float clipDistance [[clip_distance]] [1];
};

struct Uniforms {
    float4x4 modelViewMatrix;
    float4x4 projectionMatrix;
};

vertex VertexOut shadowGenVertProg(
    VertexIn in [[stage_in]],
    constant Uniforms& uniforms [[buffer(0)]])
{
    VertexOut out;

    float3x3 normalMatrix = float3x3(
        uniforms.modelViewMatrix[0].xyz,
        uniforms.modelViewMatrix[1].xyz,
        uniforms.modelViewMatrix[2].xyz
    );

    float4 lightVertexPos = uniforms.modelViewMatrix * in.position;
    float3 lightVertexNormal = normalize(normalMatrix * in.normal);

    float NdotL = clamp(dot(lightVertexNormal, float3(0.0, 0.0, 1.0)), 0.0, 1.0);

    // Bias calculation
    const float cb = 1e-6;
    float bias = cb * tan(acos(NdotL));
    bias = clamp(bias, 0.0, 100.0 * cb);

    lightVertexPos.xy += float2(0.5);
    lightVertexPos.z += bias;

    out.position = uniforms.projectionMatrix * lightVertexPos;
    out.clipDistance[0] = in.position.z;  // gl_ClipVertex = gl_Vertex
    out.texCoord = in.texCoord;

    return out;
}
