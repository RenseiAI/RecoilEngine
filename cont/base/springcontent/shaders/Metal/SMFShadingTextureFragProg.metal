// SMFShadingTextureFragProg.metal
// Translated from GLSL/SMFShadingTextureFragProg.glsl
// SMF shading texture generation fragment shader

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 mapUV;
};

struct ShadingTextureOut {
    float4 shadingVal [[color(0)]];
    float2 normalXZ [[color(1)]];
};

struct ShadingTextureUniforms {
    float4 mapSizeP1;
    float4 groundAmbientColor;
    float4 groundDiffuseColor;
    float4 lightDir;
    float3 waterBaseColor;
    float3 waterAbsorb;
    float3 waterMinColor;
    float waterLevel;
};

constant float SMF_INTENSITY_MULT = 210.0 / 255.0;
constant float SQUARE_SIZE = 8.0;

float3 GetVertex(int2 xy, float4 mapSizeP1, texture2d<float> heightMapTex) {
    xy = clamp(xy, int2(0), int2(mapSizeP1.xy));
    return float3(
        float(xy.x) * SQUARE_SIZE,
        heightMapTex.read(uint2(xy)).x,
        float(xy.y) * SQUARE_SIZE
    );
}

float3 CalcFragmentNormal(float2 uv, float4 mapSizeP1, texture2d<float> heightMapTex) {
    int2 xy = int2(uv * mapSizeP1.xy);

    int2 bl = xy + int2(-1, -1);
    int2 bm = xy + int2( 0, -1);
    int2 br = xy + int2( 1, -1);
    int2 ml = xy + int2(-1,  0);
    int2 mm = xy + int2( 0,  0);
    int2 mr = xy + int2( 1,  0);
    int2 tl = xy + int2(-1,  1);
    int2 tm = xy + int2( 0,  1);
    int2 tr = xy + int2( 1,  1);

    float3 vbl = GetVertex(bl, mapSizeP1, heightMapTex);
    float3 vbm = GetVertex(bm, mapSizeP1, heightMapTex);
    float3 vbr = GetVertex(br, mapSizeP1, heightMapTex);
    float3 vml = GetVertex(ml, mapSizeP1, heightMapTex);
    float3 vmm = GetVertex(mm, mapSizeP1, heightMapTex);
    float3 vmr = GetVertex(mr, mapSizeP1, heightMapTex);
    float3 vtl = GetVertex(tl, mapSizeP1, heightMapTex);
    float3 vtm = GetVertex(tm, mapSizeP1, heightMapTex);
    float3 vtr = GetVertex(tr, mapSizeP1, heightMapTex);

    vbl -= vmm;
    vbm -= vmm;
    vbr -= vmm;
    vml -= vmm;
    vmr -= vmm;
    vtl -= vmm;
    vtm -= vmm;
    vtr -= vmm;

    float3 normal = float3(0);

    normal += cross(vtr, vmr);
    normal += cross(vmr, vbr);
    normal += cross(vbr, vbm);
    normal += cross(vbm, vbl);
    normal += cross(vbl, vml);
    normal += cross(vml, vtl);
    normal += cross(vtl, vtm);
    normal += cross(vtm, vtr);

    return normalize(normal);
}

float EncodeHeight(float relH) {
    return clamp((255 - 10.0 * relH) / 255, 0.0, 1.0);
}

float3 GetWaterHeightColor(float relH, float3 waterBaseColor, float3 waterAbsorb, float3 waterMinColor) {
    relH = clamp(relH, 0.0, 1024.0);
    float3 absorbColor = waterBaseColor - waterAbsorb * relH;
    return clamp(absorbColor, waterMinColor, float3(1));
}

fragment ShadingTextureOut smfShadingTextureFragProg(
    VertexOut in [[stage_in]],
    constant ShadingTextureUniforms& uniforms [[buffer(0)]],
    texture2d<float> heightMapTex [[texture(0)]])
{
    ShadingTextureOut out;

    float height = heightMapTex.read(uint2(in.mapUV * uniforms.mapSizeP1.xy)).x;

    float3 terrainNormal = CalcFragmentNormal(in.mapUV, uniforms.mapSizeP1, heightMapTex);
    out.normalXZ = float2(terrainNormal.x, terrainNormal.z);

    float posNdotL = max(dot(terrainNormal, uniforms.lightDir.xyz), 0.0);

    float3 lightVal = min((uniforms.groundAmbientColor.rgb + uniforms.groundDiffuseColor.rgb * posNdotL) * SMF_INTENSITY_MULT, float3(1));

    if (height < uniforms.waterLevel) {
        float relH = (uniforms.waterLevel - height);
        float lightIntensity = min((posNdotL + 0.2) * 2.0, 1.0);
        float3 waterHeightColor = GetWaterHeightColor(relH, uniforms.waterBaseColor, uniforms.waterAbsorb, uniforms.waterMinColor);

        if (height > uniforms.waterLevel - 10.0) {
            float wc = relH * 0.1;
            float3 lightColor = lightVal * (1.0 - wc);
            lightIntensity *= wc;

            out.shadingVal.rgb = waterHeightColor * lightIntensity + lightColor;
        } else {
            out.shadingVal.rgb = waterHeightColor * lightIntensity;
        }
        out.shadingVal.a = EncodeHeight(relH);
    } else {
        out.shadingVal = float4(lightVal, 1.0);
    }

    return out;
}
