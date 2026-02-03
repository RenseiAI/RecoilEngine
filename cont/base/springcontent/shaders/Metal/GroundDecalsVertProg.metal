// GroundDecalsVertProg.metal
// Translated from GLSL/GroundDecalsVertProg.glsl
// Ground decals vertex shader (explosion scars, tracks, etc.)

#include <metal_stdlib>
using namespace metal;

// Cube vertices for volumetric decals
constant float3 CUBE_VERT[36] = {
    // RIGHT
    float3( 1.0, -1.0, -1.0), float3( 1.0, -1.0,  1.0), float3( 1.0,  1.0,  1.0),
    float3( 1.0,  1.0,  1.0), float3( 1.0,  1.0, -1.0), float3( 1.0, -1.0, -1.0),
    // LEFT
    float3(-1.0, -1.0,  1.0), float3(-1.0, -1.0, -1.0), float3(-1.0,  1.0, -1.0),
    float3(-1.0,  1.0, -1.0), float3(-1.0,  1.0,  1.0), float3(-1.0, -1.0,  1.0),
    // TOP
    float3(-1.0,  1.0, -1.0), float3( 1.0,  1.0, -1.0), float3( 1.0,  1.0,  1.0),
    float3( 1.0,  1.0,  1.0), float3(-1.0,  1.0,  1.0), float3(-1.0,  1.0, -1.0),
    // BOTTOM
    float3(-1.0, -1.0, -1.0), float3(-1.0, -1.0,  1.0), float3( 1.0, -1.0, -1.0),
    float3( 1.0, -1.0, -1.0), float3(-1.0, -1.0,  1.0), float3( 1.0, -1.0,  1.0),
    // FRONT
    float3(-1.0, -1.0,  1.0), float3(-1.0,  1.0,  1.0), float3( 1.0,  1.0,  1.0),
    float3( 1.0,  1.0,  1.0), float3( 1.0, -1.0,  1.0), float3(-1.0, -1.0,  1.0),
    // BACK
    float3(-1.0,  1.0, -1.0), float3(-1.0, -1.0, -1.0), float3( 1.0, -1.0, -1.0),
    float3( 1.0, -1.0, -1.0), float3( 1.0,  1.0, -1.0), float3(-1.0,  1.0, -1.0)
};

struct VertexIn {
    float4 forcedHeight [[attribute(0)]];
    float4 posT [[attribute(1)]];  // posTL, posTR
    float4 posB [[attribute(2)]];  // posBR, posBL
    float4 uvMain [[attribute(3)]];  // L, T, R, B
    float4 uvNorm [[attribute(4)]];
    float4 createParams1 [[attribute(5)]];
    float4 createParams2 [[attribute(6)]];
    float4 createParams3 [[attribute(7)]];
    float4 createParams4 [[attribute(8)]];
    uint4 createParams5 [[attribute(9)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 vTranformedPos0 [[flat]];  // midpos
    float4 vTranformedPos1 [[flat]];
    float4 vTranformedPos2 [[flat]];
    float4 vTranformedPos3 [[flat]];
    float4 vTranformedPos4 [[flat]];
    float4 vuvMain [[flat]];
    float4 vuvNorm [[flat]];
    float4 vData1;
    float4 vData2 [[flat]];
    float4 vData3 [[flat]];
    float4 vData4 [[flat]];
    float3x3 vRotMat [[flat]];
};

struct DecalsUniforms {
    float4x4 modelViewProjectionMatrix;
    float4 mapDims;  // mapxy; 1.0 / mapxy
    float curAdjustedFrame;
};

#define NORM2SNORM(value) ((value) * 2.0 - 1.0)
#define SNORM2NORM(value) ((value) * 0.5 + 0.5)

constant float2 HM_TEXEL = float2(8.0, 8.0);

float HeightAtWorldPos(float2 wxz, float4 mapDims, texture2d<float> heightTex, sampler texSampler) {
    wxz += -HM_TEXEL * (wxz * mapDims.zw) + 0.5 * HM_TEXEL;
    float2 uvhm = clamp(wxz, HM_TEXEL, mapDims.xy - HM_TEXEL);
    uvhm *= mapDims.zw;
    return heightTex.sample(texSampler, uvhm, level(0.0)).x;
}

float3 GetFragmentNormal(float2 wxz, float4 mapDims, texture2d<float> groundNormalTex, sampler texSampler) {
    float3 normal;
    float4 sample = groundNormalTex.sample(texSampler, wxz * mapDims.zw, level(0.0));
    normal.xz = float2(sample.r, sample.a);
    normal.y = sqrt(1.0 - dot(normal.xz, normal.xz));
    return normal;
}

float3 RotateByNormalVector(float3 p, float3 newUpDir, float3 rotAxis) {
    float3 cm = float3(
        newUpDir.y,
        sqrt(newUpDir.x * newUpDir.x + newUpDir.z * newUpDir.z),
        (rotAxis.x * p.x + rotAxis.y * p.y + rotAxis.z * p.z) * (1.0 - newUpDir.y)
    );

    return float3(
        dot(cm, float3(p.x, (rotAxis.y * p.z - rotAxis.z * p.y), rotAxis.x)),
        dot(cm, float3(p.y, (rotAxis.z * p.x - rotAxis.x * p.z), rotAxis.y)),
        dot(cm, float3(p.z, (rotAxis.x * p.y - rotAxis.y * p.x), rotAxis.z))
    );
}

constant float DECAL_EXPLOSION = 2.0;

vertex VertexOut groundDecalsVertProg(
    VertexIn in [[stage_in]],
    uint vertexID [[vertex_id]],
    constant DecalsUniforms& uniforms [[buffer(0)]],
    texture2d<float> heightTex [[texture(0)]],
    texture2d<float> groundNormalTex [[texture(1)]],
    sampler texSampler [[sampler(0)]])
{
    VertexOut out;

    float3 relPos = CUBE_VERT[vertexID];

    // Extract params
    float alpha = in.createParams1.x;
    float alphaFalloff = in.createParams1.y;
    float glow = in.createParams1.z;
    float glowFalloff = in.createParams1.w;

    float rot = in.createParams2.x;
    float height = in.createParams2.y;
    float dotElimExp = in.createParams2.z;
    float cmAlphaMult = in.createParams2.w;

    float createFrameMin = in.createParams3.x;
    float createFrameMax = in.createParams3.y;
    float uvWrapDistance = in.createParams3.z;
    float uvTraveledDistance = in.createParams3.w;

    float3 forcedNormal = in.createParams4.xyz;
    float visMult = in.createParams4.w;

    uint decalType = (in.createParams5.x >> 0u) & 0xFu;

    // Calculate alpha
    float thisVertexCreateFrame = mix(createFrameMin, createFrameMax, float(relPos.x > 0.0));
    float vAlpha = alpha - (uniforms.curAdjustedFrame - thisVertexCreateFrame) * alphaFalloff;
    float vGlow = glow - (uniforms.curAdjustedFrame - thisVertexCreateFrame) * glowFalloff;
    float alphaMax = alpha - (uniforms.curAdjustedFrame - createFrameMax) * alphaFalloff;
    alphaMax *= visMult;
    vAlpha *= visMult;

    float vDecalType = float(decalType);

    // Early out for invisible decals
    if (alphaMax <= 0.0 || vDecalType <= 0.0) {
        out.vTranformedPos0 = float4(0);
        out.vTranformedPos1 = float4(0);
        out.vTranformedPos2 = float4(0);
        out.vTranformedPos3 = float4(0);
        out.vTranformedPos4 = float4(0);
        out.position = float4(2.0, 2.0, 2.0, 1.0);
        return out;
    }

    // Calculate midpoint
    float4 midPoint;
    midPoint.xz = (in.posT.xy + in.posT.zw + in.posB.zw + in.posB.xy) * 0.25;
    midPoint.y = HeightAtWorldPos(midPoint.xz, uniforms.mapDims, heightTex, texSampler);

    // Height mode handling
    float refHeight = in.forcedHeight.x;
    float minHeight = in.forcedHeight.y;
    float maxHeight = in.forcedHeight.z;
    float forceHeightMode = in.forcedHeight.w;

    midPoint.y = mix(midPoint.y, refHeight + clamp(midPoint.y - refHeight, minHeight, maxHeight), float(forceHeightMode == 1.0));
    midPoint.y = mix(midPoint.y, clamp(midPoint.y, minHeight, maxHeight), float(forceHeightMode == 2.0));

    float sa = sin(rot);
    float ca = cos(rot);

    // Calculate ground normal
    float3 groundNormal = float3(0);
    if (dot(forcedNormal, forcedNormal) == 0.0) {
        float2x2 rotMat2d = float2x2(ca, -sa, sa, ca);
        groundNormal += 2.0 * GetFragmentNormal(midPoint.xz, uniforms.mapDims, groundNormalTex, texSampler);
        groundNormal += 1.0 * GetFragmentNormal(rotMat2d * (in.posT.xy - midPoint.xz) + midPoint.xz, uniforms.mapDims, groundNormalTex, texSampler);
        groundNormal += 1.0 * GetFragmentNormal(rotMat2d * (in.posT.zw - midPoint.xz) + midPoint.xz, uniforms.mapDims, groundNormalTex, texSampler);
        groundNormal += 1.0 * GetFragmentNormal(rotMat2d * (in.posB.zw - midPoint.xz) + midPoint.xz, uniforms.mapDims, groundNormalTex, texSampler);
        groundNormal += 1.0 * GetFragmentNormal(rotMat2d * (in.posB.xy - midPoint.xz) + midPoint.xz, uniforms.mapDims, groundNormalTex, texSampler);
        groundNormal = normalize(groundNormal);
    } else {
        groundNormal = forcedNormal;
    }

    // Build rotation matrix
    float3 xDir = float3(ca, 0.0, sa);
    if (1.0 - groundNormal.y > 0.05) {
        float3 rotAxis = normalize(float3(groundNormal.z, 0.0, -groundNormal.x));
        xDir = RotateByNormalVector(xDir, groundNormal, rotAxis);
    }
    float3 zDir = normalize(cross(xDir, groundNormal));
    float3x3 rotMat = float3x3(xDir, groundNormal, zDir);
    out.vRotMat = rotMat;

    // Transform vertices
    out.vTranformedPos0 = midPoint;
    out.vTranformedPos1.xyz = rotMat * (float3(in.posB.z, 0.0, in.posB.w) - float3(midPoint.x, 0.0, midPoint.z)) + midPoint.xyz;
    out.vTranformedPos2.xyz = rotMat * (float3(in.posT.x, 0.0, in.posT.y) - float3(midPoint.x, 0.0, midPoint.z)) + midPoint.xyz;
    out.vTranformedPos3.xyz = rotMat * (float3(in.posT.z, 0.0, in.posT.w) - float3(midPoint.x, 0.0, midPoint.z)) + midPoint.xyz;
    out.vTranformedPos4.xyz = rotMat * (float3(in.posB.x, 0.0, in.posB.y) - float3(midPoint.x, 0.0, midPoint.z)) + midPoint.xyz;

    // Distances
    out.vTranformedPos1.w = distance(in.posB.zw, in.posB.xy) * 0.5;
    out.vTranformedPos2.w = distance(in.posT.xy, in.posB.zw) * 0.5;
    out.vTranformedPos3.w = distance(in.posT.zw, in.posT.xy) * 0.5;
    out.vTranformedPos4.w = distance(in.posB.xy, in.posT.zw) * 0.5;

    midPoint.w = sqrt(out.vTranformedPos1.w * out.vTranformedPos1.w + out.vTranformedPos3.w * out.vTranformedPos2.w + height * height);
    out.vTranformedPos0 = midPoint;

    out.vuvMain = in.uvMain;
    out.vuvNorm = in.uvNorm;

    // Determine which vertex
    float4 testResults = float4(
        float(all(relPos.xz == float2(-1.0, 1.0))),
        float(all(relPos.xz == float2(-1.0, -1.0))),
        float(all(relPos.xz == float2(1.0, -1.0))),
        float(all(relPos.xz == float2(1.0, 1.0)))
    );

    float3 worldPos = float3(0);
    worldPos += testResults.x * out.vTranformedPos1.xyz;
    worldPos += testResults.y * out.vTranformedPos2.xyz;
    worldPos += testResults.z * out.vTranformedPos3.xyz;
    worldPos += testResults.w * out.vTranformedPos4.xyz;

    // Extract tint colors
    float4 texTint = float4(
        float((in.createParams5.y >> 0u) & 0xFFu) / 255.0,
        float((in.createParams5.y >> 8u) & 0xFFu) / 255.0,
        float((in.createParams5.y >> 16u) & 0xFFu) / 255.0,
        float((in.createParams5.y >> 24u) & 0xFFu) / 255.0
    );

    float4 glowTintMin = float4(
        float((in.createParams5.z >> 0u) & 0xFFu) / 255.0,
        float((in.createParams5.z >> 8u) & 0xFFu) / 255.0,
        float((in.createParams5.z >> 16u) & 0xFFu) / 255.0,
        float((in.createParams5.z >> 24u) & 0xFFu) / 255.0
    );

    float4 glowTintMax = float4(
        float((in.createParams5.w >> 0u) & 0xFFu) / 255.0,
        float((in.createParams5.w >> 8u) & 0xFFu) / 255.0,
        float((in.createParams5.w >> 16u) & 0xFFu) / 255.0,
        float((in.createParams5.w >> 24u) & 0xFFu) / 255.0
    );

    out.vData3 = texTint;
    out.vData4 = mix(glowTintMin, glowTintMax, vAlpha * cmAlphaMult);

    // Fade in for explosions
    vAlpha *= mix(1.0, smoothstep(0.0, 6.0 * alpha, uniforms.curAdjustedFrame - thisVertexCreateFrame), float(vDecalType == DECAL_EXPLOSION));

    out.vData1 = float4(vAlpha, vGlow, dotElimExp, 0.0);
    out.vData2 = float4(height, uvWrapDistance, uvTraveledDistance, vDecalType);

    worldPos += relPos.y * height * groundNormal;

    out.position = uniforms.modelViewProjectionMatrix * float4(worldPos, 1.0);

    return out;
}
