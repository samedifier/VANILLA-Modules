#version 450

// VANILLA WINDOW internal FXAA pass (after tone mapping, scene pipelines only).
// Classic FXAA (Timothy Lottes) in its compact form. Input is linear and tone mapped;
// luma uses sqrt() as a cheap perceptual curve.
// Embedded into the module (include/vanilla_window_tonemap_spv.h).

layout(set = 0, binding = 0) uniform sampler2D inputImage;

layout(push_constant, std430) uniform Fxaa {
    vec2 rcpFrame;   // 1 / resolution
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

const float FXAA_REDUCE_MIN = 1.0 / 128.0;
const float FXAA_REDUCE_MUL = 1.0 / 8.0;
const float FXAA_SPAN_MAX   = 8.0;

float luma(vec3 c) {
    return dot(sqrt(max(c, vec3(0.0))), vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 px = pc.rcpFrame;

    vec4 center = texture(inputImage, uv);
    float lumaNW = luma(texture(inputImage, uv + vec2(-1.0, -1.0) * px).rgb);
    float lumaNE = luma(texture(inputImage, uv + vec2( 1.0, -1.0) * px).rgb);
    float lumaSW = luma(texture(inputImage, uv + vec2(-1.0,  1.0) * px).rgb);
    float lumaSE = luma(texture(inputImage, uv + vec2( 1.0,  1.0) * px).rgb);
    float lumaM  = luma(center.rgb);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX)) * px;

    vec3 rgbA = 0.5 * (
        texture(inputImage, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture(inputImage, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(inputImage, uv + dir * -0.5).rgb +
        texture(inputImage, uv + dir *  0.5).rgb);

    float lumaB = luma(rgbB);
    vec3 color = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;

    outColor = vec4(color, center.a);
}
