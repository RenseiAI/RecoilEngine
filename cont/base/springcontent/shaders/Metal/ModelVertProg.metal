// ModelVertProg.metal
// Translated from GLSL/ModelVertProg.glsl
// Legacy model vertex shader with explicit uniforms (no FFP built-ins)

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

// Explicit uniforms replacing FFP built-ins
struct ModelMatrices {
    float4x4 modelMatrix;      // model transform only (not view)
    float4x4 viewProjMatrix;   // view * projection combined
    float3 cameraPosW;         // camera world position
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
    constant ModelMatrices& matrices [[buffer(0)]],
    constant FogParams& fog [[buffer(1)]],
    constant ModelUniforms& uniforms [[buffer(2)]])
{
    VertexOut out;

    // mat3(modelMatrix) works as normal matrix (rotation+translation, no non-uniform scale)
    out.normalv = float3x3(matrices.modelMatrix[0].xyz,
                           matrices.modelMatrix[1].xyz,
                           matrices.modelMatrix[2].xyz) * in.normal;

    float4 worldPos = matrices.modelMatrix * in.position;
    out.position = matrices.viewProjMatrix * worldPos;

    out.vertexWorldPos = worldPos;
    out.cameraDir = worldPos.xyz - matrices.cameraPosW;

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
    out.clipDistance[0] = worldPos.z;

    return out;
}
