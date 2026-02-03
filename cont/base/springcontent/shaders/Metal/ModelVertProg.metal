// ModelVertProg.metal
// Translated from GLSL/ModelVertProg.glsl
// Legacy model vertex shader (GLSL 120 style)
// Note: This shader uses legacy GL built-ins that are translated to uniform buffers

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float4 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 texCoord [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 vertexWorldPos;
    float3 cameraDir;
    float fogFactor;
    float3 normalv;
    float2 texCoord;
#ifdef USE_SHADOWS
    float4 shadowVertexPos;
#endif
    float clipDistance [[clip_distance]] [1];
};

// Legacy GL built-in matrices - provided via uniform buffer
struct LegacyMatrices {
    float4x4 modelViewMatrix;       // gl_ModelViewMatrix (actually just model matrix)
    float4x4 projectionMatrix;      // gl_ProjectionMatrix
    float4x4 projectionMatrixInverse;
    float3x3 normalMatrix;          // gl_NormalMatrix
};

struct FogParams {
    float4 color;
    float start;
    float end;
    float scale;  // 1.0 / (end - start)
    float density;
};

struct ModelUniforms {
#ifdef USE_SHADOWS
    float4x4 shadowMatrix;
#endif
};

vertex VertexOut modelVertProg(
    VertexIn in [[stage_in]],
    constant LegacyMatrices& matrices [[buffer(0)]],
    constant FogParams& fog [[buffer(1)]],
    constant ModelUniforms& uniforms [[buffer(2)]])
{
    VertexOut out;

    out.normalv = matrices.normalMatrix * in.normal;

    // gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex
    float4 clipVertex = matrices.modelViewMatrix * in.position;
    out.position = matrices.projectionMatrix * clipVertex;

    out.vertexWorldPos = clipVertex;

    float4 cameraPos = matrices.projectionMatrixInverse * float4(0, 0, 0, 1);
    cameraPos.xyz /= cameraPos.w;

    out.cameraDir = out.vertexWorldPos.xyz - cameraPos.xyz;

#ifdef USE_SHADOWS
    out.shadowVertexPos = uniforms.shadowMatrix * out.vertexWorldPos;
    out.shadowVertexPos.xy += float2(0.5);
#endif

    out.texCoord = in.texCoord;

    // Fog calculation (only in non-deferred mode)
#ifndef DEFERRED_MODE
    float fogCoord = length(out.cameraDir.xyz);
    out.fogFactor = (fog.end - fogCoord) * fog.scale;
    out.fogFactor = clamp(out.fogFactor, 0.0, 1.0);
#else
    out.fogFactor = 1.0;
#endif

    // Clip distance for user clip planes
    out.clipDistance[0] = clipVertex.z;

    return out;
}
