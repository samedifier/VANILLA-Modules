#version 460

// VANILLA WINDOW internal editor grid: anti-aliased lines of constant pixel width (fwidth),
// minor lines fade out when cells get too small on screen, everything fades with distance.
// X axis (z = 0) red, Z axis (x = 0) blue. Blended over the scene and the sky, no depth write.

layout(push_constant, std430) uniform GridPush {
    mat4 viewProjection;
    vec4 cameraHeight;
    vec4 params;
    vec4 minorColor;
    vec4 majorColor;
} grid;

layout(location = 0) in  vec3 worldPos;
layout(location = 0) out vec4 outColor;

// Coverage of grid lines about widthPx pixels wide (coord in cells).
float gridLine(vec2 coord, float widthPx) {
    vec2 d = max(fwidth(coord), vec2(1e-6));
    vec2 g = abs(fract(coord - 0.5) - 0.5) / d;   // distance to the nearest line, in pixels
    float l = min(g.x, g.y);
    return 1.0 - clamp(l - (widthPx * 0.5 - 0.5), 0.0, 1.0);
}

// Hides lines whose cells shrink below a few pixels (they would turn into a gray sheet / moire).
float densityFade(vec2 coord) {
    vec2 d = fwidth(coord);
    return 1.0 - smoothstep(0.15, 0.35, max(d.x, d.y));
}

// Coverage of the single line at coord = 0 (world units).
float axisLine(float coord, float widthPx) {
    float d = max(fwidth(coord), 1e-6);
    return 1.0 - clamp(abs(coord) / d - (widthPx * 0.5 - 0.5), 0.0, 1.0);
}

void main() {
    float cell       = grid.params.x;
    float majorEvery = grid.params.y;
    float fadeDist   = grid.params.z;

    vec2 p = worldPos.xz;
    vec2 minorCoord = p / cell;
    vec2 majorCoord = p / (cell * majorEvery);

    float minorA = gridLine(minorCoord, 1.0) * densityFade(minorCoord) * grid.minorColor.a;
    float majorA = gridLine(majorCoord, 1.5) * densityFade(majorCoord) * grid.majorColor.a;

    vec4 color = (majorA >= minorA) ? vec4(grid.majorColor.rgb, majorA)
                                    : vec4(grid.minorColor.rgb, minorA);

    float xAxis = axisLine(p.y, 2.0);   // z = 0: the X axis
    float zAxis = axisLine(p.x, 2.0);   // x = 0: the Z axis
    color = mix(color, vec4(0.85, 0.15, 0.15, 0.9), xAxis);
    color = mix(color, vec4(0.15, 0.35, 0.95, 0.9), zAxis);

    float dist = length(worldPos - grid.cameraHeight.xyz);
    color.a *= 1.0 - smoothstep(0.4 * fadeDist, fadeDist, dist);

    if (color.a < 1.0 / 255.0) { discard; }

    outColor = color;
}
