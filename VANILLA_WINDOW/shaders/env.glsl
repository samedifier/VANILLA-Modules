// Environment lighting helpers shared by the env_*.comp shaders and sceneshader.frag.

#ifndef VANILLA_ENV_GLSL
#define VANILLA_ENV_GLSL

#define ENV_CUBE_SIZE      128
#define ENV_PREFILTER_MIPS 6

// Direction through texel (x, y) of cube face `face` (Vulkan face order +X -X +Y -Y +Z -Z).
vec3 cubeDirection(uint face, vec2 texel, float size) {
    vec2 uv = (texel + 0.5) / size * 2.0 - 1.0;
    float u = uv.x, v = uv.y;
    vec3 d;
    if      (face == 0u) { d = vec3( 1.0,  -v,  -u); }
    else if (face == 1u) { d = vec3(-1.0,  -v,   u); }
    else if (face == 2u) { d = vec3(   u, 1.0,   v); }
    else if (face == 3u) { d = vec3(   u,-1.0,  -v); }
    else if (face == 4u) { d = vec3(   u,  -v, 1.0); }
    else                 { d = vec3(  -u,  -v,-1.0); }
    return normalize(d);
}

// Solid angle of a cube texel (approximation, fine at 128^2).
float cubeTexelSolidAngle(vec2 texel, float size) {
    vec2 uv = (texel + 0.5) / size * 2.0 - 1.0;
    float t = 1.0 + uv.x * uv.x + uv.y * uv.y;
    return 4.0 / (size * size * t * sqrt(t));
}

// Real L2 spherical harmonics basis (9 values).
void shBasis(vec3 d, out float y[9]) {
    y[0] = 0.282095;
    y[1] = 0.488603 * d.y;
    y[2] = 0.488603 * d.z;
    y[3] = 0.488603 * d.x;
    y[4] = 1.092548 * d.x * d.y;
    y[5] = 1.092548 * d.y * d.z;
    y[6] = 0.315392 * (3.0 * d.z * d.z - 1.0);
    y[7] = 1.092548 * d.x * d.z;
    y[8] = 0.546274 * (d.x * d.x - d.y * d.y);
}

#endif
