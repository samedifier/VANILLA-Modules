#version 450

// VANILLA WINDOW internal tone mapping pass: HDR (RGBA16F) -> display range.
// Embedded into the module (include/vanilla_window_tonemap_spv.h).
// Output stays linear: an sRGB swapchain / pipeline image does the encoding, as before.

layout(set = 0, binding = 0) uniform sampler2D hdrImage;

layout(push_constant, std430) uniform ToneMap {
    float exposure;
    uint  mode;      // 0 none (clamp), 1 ACES, 2 AgX
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

// ACES filmic fit (Krzysztof Narkowicz).
vec3 tonemapACES(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// AgX (Troy Sobotka), minimal approximation by Benjamin Wrensch, default look.
vec3 agxContrast(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 tonemapAgX(vec3 color) {
    const mat3 agxIn = mat3(
        0.842479062253094,  0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772,  0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 agxOut = mat3(
         1.19687900512017,  -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417,   -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;

    vec3 v = agxIn * max(color, vec3(1e-10));
    v = clamp(log2(v), minEv, maxEv);
    v = (v - minEv) / (maxEv - minEv);
    v = agxContrast(v);
    v = agxOut * v;
    return clamp(pow(max(v, vec3(0.0)), vec3(2.2)), 0.0, 1.0);   // back to linear
}

void main() {
    vec4 hdr = texture(hdrImage, uv);
    vec3 color = max(hdr.rgb, vec3(0.0)) * pc.exposure;

    if (pc.mode == 1u)      { color = tonemapACES(color); }
    else if (pc.mode == 2u) { color = tonemapAgX(color); }
    else                    { color = clamp(color, 0.0, 1.0); }

    outColor = vec4(color, hdr.a);
}
