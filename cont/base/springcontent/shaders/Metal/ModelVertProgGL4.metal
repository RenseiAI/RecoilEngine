// ModelVertProgGL4.metal
// Translated from GLSL/ModelVertProgGL4.glsl
// Modern GL4 model vertex shader with bone animation and instancing

#include <metal_stdlib>
using namespace metal;

// Transform structure for quaternion-based animation
struct Transform {
    float4 quat;
    float4 trSc;  // xyz = translation, w = scale
};

// Uniform buffer structures matching GL4 layout
struct UniformMatrixBuffer {
    float4x4 screenView;
    float4x4 screenProj;
    float4x4 screenViewProj;

    float4x4 cameraView;
    float4x4 cameraProj;
    float4x4 cameraViewProj;
    float4x4 cameraBillboardView;

    float4x4 cameraViewInv;
    float4x4 cameraProjInv;
    float4x4 cameraViewProjInv;

    float4x4 shadowView;
    float4x4 shadowProj;
    float4x4 shadowViewProj;

    float4x4 reflectionView;
    float4x4 reflectionProj;
    float4x4 reflectionViewProj;

    float4x4 orthoProj01;

    float4x4 mmDrawView;
    float4x4 mmDrawProj;
    float4x4 mmDrawViewProj;

    float4x4 mmDrawIMMView;
    float4x4 mmDrawIMMProj;
    float4x4 mmDrawIMMViewProj;

    float4x4 mmDrawDimView;
    float4x4 mmDrawDimProj;
    float4x4 mmDrawDimViewProj;
};

struct UniformParamsBuffer {
    float3 rndVec3;
    uint renderCaps;

    float4 timeInfo;
    float4 viewGeometry;
    float4 mapSize;
    float4 mapHeight;

    float4 fogColor;
    float4 fogParams;

    float4 sunDir;

    float4 sunAmbientModel;
    float4 sunAmbientMap;
    float4 sunDiffuseModel;
    float4 sunDiffuseMap;
    float4 sunSpecularModel;
    float4 sunSpecularMap;

    float4 shadowDensity;

    float4 windInfo;
    float2 mouseScreenPos;
    uint mouseStatus;
    uint mouseUnused;
    float4 mouseWorldPos;

    float4 teamColor[255];
};

struct VertexIn {
    float3 pos [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float3 T [[attribute(2)]];
    float3 B [[attribute(3)]];
    float4 uv [[attribute(4)]];
    uint3 bonesInfo [[attribute(5)]];  // boneIDsLow, boneWeights, boneIDsHigh
};

struct InstanceData {
    uint4 instData [[attribute(6)]];  // matOffset, uniOffset, teamInfo, bposeMatOffset
};

struct VertexOut {
    float4 position [[position]];
    float4 uvCoord [[centroid_perspective]];
    float4 teamCol;
    float4 worldPos;
    float3 worldNormal;
    float3 worldCameraDir;
    float4 shadowVertexPos;
    float fogFactor;
    float clipDistance [[clip_distance]] [3];
};

struct VertexUniforms {
    int cameraMode;
    int matrixMode;
    float4x4 staticModelMatrix;
    float4 clipPlane0;
    float4 clipPlane1;
    float4 clipPlane2;
    float teamColorAlpha;
};

// Quaternion math functions
float4 MultiplyQuat(float4 a, float4 b) {
    return float4(a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz),
                  a.w * b.w - dot(a.xyz, b.xyz));
}

float3 RotateByQuaternion(float4 q, float3 v) {
    return 2.0 * dot(q.xyz, v) * q.xyz +
           (q.w * q.w - dot(q.xyz, q.xyz)) * v +
           2.0 * q.w * cross(q.xyz, v);
}

float4 RotateByQuaternion4(float4 q, float4 v) {
    return float4(RotateByQuaternion(q, v.xyz), v.w);
}

float4 InvertNormalizedQuaternion(float4 q) {
    return float4(-q.x, -q.y, -q.z, q.w);
}

float3 ApplyTransform(Transform tra, float3 v) {
    return RotateByQuaternion(tra.quat, v * tra.trSc.w) + tra.trSc.xyz;
}

float4 ApplyTransform4(Transform tra, float4 v) {
    return float4(RotateByQuaternion(tra.quat, v.xyz * tra.trSc.w) + tra.trSc.xyz * v.w, v.w);
}

Transform ApplyTransformT(Transform parentTra, Transform childTra) {
    Transform result;
    result.quat = MultiplyQuat(parentTra.quat, childTra.quat);
    result.trSc = float4(
        parentTra.trSc.xyz + RotateByQuaternion(parentTra.quat, parentTra.trSc.w * childTra.trSc.xyz),
        parentTra.trSc.w * childTra.trSc.w
    );
    return result;
}

Transform InvertTransformAffine(Transform tra) {
    float4 invR = InvertNormalizedQuaternion(tra.quat);
    float invS = 1.0 / tra.trSc.w;
    Transform result;
    result.quat = invR;
    result.trSc = float4(RotateByQuaternion(invR, -tra.trSc.xyz * invS), invS);
    return result;
}

float4 SLerp(float4 qa, float4 qb, float t) {
    float cosHalfTheta = dot(qa, qb);
    float s = sign(cosHalfTheta);
    qb *= s;
    cosHalfTheta *= s;

    if (cosHalfTheta >= (1.0 - 0.005))
        return normalize(mix(qa, qb, t));

    float halfTheta = acos(cosHalfTheta);
    float ratioA = sin((1.0 - t) * halfTheta);
    float ratioB = sin(t * halfTheta);

    return normalize(qa * ratioA + qb * ratioB);
}

Transform Lerp(Transform t0, Transform t1, float a) {
    a = clamp(a, 0.0, 1.0);
    Transform result;
    result.quat = SLerp(t0.quat, t1.quat, a);
    result.trSc = mix(t0.trSc, t1.trSc, a);
    return result;
}

uint GetUnpackedValue(uint packedValue, uint byteNum) {
    return (packedValue >> (8u * byteNum)) & 0xFFu;
}

vertex VertexOut modelVertProgGL4(
    VertexIn in [[stage_in]],
    InstanceData inst [[stage_in]],
    constant UniformMatrixBuffer& matrices [[buffer(0)]],
    constant UniformParamsBuffer& params [[buffer(1)]],
    constant VertexUniforms& uniforms [[buffer(2)]],
    constant Transform* transforms [[buffer(3)]])
{
    VertexOut out;

    bool staticModel = (uniforms.matrixMode > 0);

    float4 piecePos = float4(in.pos, 1.0);
    float4 normal4 = float4(in.normal, 0.0);

    uint bID0 = GetUnpackedValue(in.bonesInfo.x, 0u) + (GetUnpackedValue(in.bonesInfo.z, 0u) << 8u);

    Transform tx;
    float4 msPosition;
    float3 msNormal;

    if (staticModel) {
        tx = transforms[inst.instData.x + bID0];
        msPosition = ApplyTransform4(tx, piecePos);
        msNormal = ApplyTransform4(tx, normal4).xyz;
    } else {
        tx = Lerp(
            transforms[inst.instData.x + 2u * (1u + bID0) + 0u],
            transforms[inst.instData.x + 2u * (1u + bID0) + 1u],
            params.timeInfo.w
        );

        float4 weights = float4(
            float(GetUnpackedValue(in.bonesInfo.y, 0u)) / 255.0,
            float(GetUnpackedValue(in.bonesInfo.y, 1u)) / 255.0,
            float(GetUnpackedValue(in.bonesInfo.y, 2u)) / 255.0,
            float(GetUnpackedValue(in.bonesInfo.y, 3u)) / 255.0
        ) * float(tx.trSc.w > 0.0);

        msPosition = ApplyTransform4(tx, piecePos);
        msNormal = ApplyTransform4(tx, normal4).xyz;

        if (weights[0] < 1.0) {
            msPosition *= weights[0];
            msNormal *= weights[0];

            Transform bposeTra = transforms[inst.instData.w + bID0];

            for (uint bi = 1; bi < 3; ++bi) {
                uint bID = GetUnpackedValue(in.bonesInfo.x, bi) + (GetUnpackedValue(in.bonesInfo.z, bi) << 8u);
                if (bID == 0xFFFFu || weights[bi] == 0.0) continue;

                Transform bposeInvTra = InvertTransformAffine(transforms[inst.instData.w + bID]);
                Transform boneTx = Lerp(
                    transforms[inst.instData.x + 2u * (1u + bID) + 0u],
                    transforms[inst.instData.x + 2u * (1u + bID) + 1u],
                    params.timeInfo.w
                );

                float4 txPiecePos = ApplyTransform4(ApplyTransformT(boneTx, ApplyTransformT(bposeInvTra, bposeTra)), piecePos);
                float3 txPieceNormal = ApplyTransform4(ApplyTransformT(boneTx, ApplyTransformT(bposeInvTra, bposeTra)), normal4).xyz;

                msPosition += txPiecePos * weights[bi];
                msNormal += txPieceNormal * weights[bi];
            }
        }
    }

    float4 worldPos;
    float3 worldNormal;

    if (staticModel) {
        worldPos = uniforms.staticModelMatrix * msPosition;
        worldNormal = float3x3(uniforms.staticModelMatrix[0].xyz,
                               uniforms.staticModelMatrix[1].xyz,
                               uniforms.staticModelMatrix[2].xyz) * msNormal;
    } else {
        Transform txWorld = Lerp(
            transforms[inst.instData.x + 0u],
            transforms[inst.instData.x + 1u],
            params.timeInfo.w
        );
        worldPos = ApplyTransform4(txWorld, msPosition);
        txWorld.trSc = float4(0, 0, 0, 1);
        worldNormal = ApplyTransform4(txWorld, float4(msNormal, 0.0)).xyz;
    }

    out.clipDistance[0] = dot(msPosition, uniforms.clipPlane0);
    out.clipDistance[1] = dot(msPosition, uniforms.clipPlane1);
    out.clipDistance[2] = dot(worldPos, uniforms.clipPlane2);

    uint teamIndex = (inst.instData.z & 0x000000FFu);
    out.teamCol = params.teamColor[teamIndex];
    out.teamCol.a = uniforms.teamColorAlpha;

    out.uvCoord = in.uv;
    out.worldPos = worldPos;
    out.worldNormal = worldNormal;

    out.shadowVertexPos = matrices.shadowView * worldPos;
    out.shadowVertexPos.xy += float2(0.5);

    float4 cameraPos = matrices.cameraViewInv * float4(0, 0, 0, 1);
    out.worldCameraDir = cameraPos.xyz - worldPos.xyz;

#ifndef DEFERRED_MODE
    float fogDist = length(out.worldCameraDir);
    out.fogFactor = (params.fogParams.y - fogDist) * params.fogParams.w;
    out.fogFactor = clamp(out.fogFactor, 0.0, 1.0);
#else
    out.fogFactor = 1.0;
#endif

    // Camera mode selection
    if (uniforms.cameraMode == 1) {
        // Water reflection
        out.position = matrices.reflectionViewProj * worldPos;
    } else {
        // Normal or refraction
        out.position = matrices.cameraViewProj * worldPos;
    }

    return out;
}
