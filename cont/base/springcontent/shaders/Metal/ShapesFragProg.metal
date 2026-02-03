// ShapesFragProg.metal
// Translated from GLSL/ShapesFragProg.glsl
// Simple shapes fragment shader

#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4 meshColor;
};

fragment float4 shapesFragProg(constant Uniforms& uniforms [[buffer(0)]]) {
    return uniforms.meshColor;
}
