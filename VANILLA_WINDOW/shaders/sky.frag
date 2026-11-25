#version 460
#extension GL_GOOGLE_include_directive : require

// VANILLA WINDOW internal sky pass: sky-view LUT + sun disc + night floor, in HDR.

#include "atmosphere.glsl"

layout(location = 0) in  vec2 ndc;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, std430) readonly buffer AtmosphereBuffer { AtmosphereParams atmo; };
layout(set = 0, binding = 1) uniform sampler2D skyViewLut;        // U repeat
layout(set = 0, binding = 2) uniform sampler2D transmittanceLut;

// Camera basis: dir = forward + ndc.x * right + ndc.y * up (right/up pre-scaled by tan(fov/2)).
// forward.w = 0 for orthographic cameras: every pixel looks along forward.
layout(push_constant, std430) uniform SkyPush {
    vec4 forward;
    vec4 right;
    vec4 up;
} cam;

void main() {
    vec3 dir = cam.forward.xyz;
    if (cam.forward.w > 0.5) {
        dir += ndc.x * cam.right.xyz + ndc.y * cam.up.xyz;
    }
    dir = normalize(dir);

    vec3 sunDir = normalize(atmo.sunDirection);

    vec3 color = texture(skyViewLut, skyViewUv(atmo, dir)).rgb * atmo.sunIlluminance;

    // Below the horizon: the planet's surface instead of black (matches the environment lighting's ground).
    color += groundRadiance(atmo, transmittanceLut, vec3(0.0, atmo.cameraHeight, 0.0), dir);

    // Sun disc: illuminance spread over the disc's solid angle, dimmed by the atmosphere.
    // Hidden below the horizon (the planet blocks it).
    float cosAngle = dot(dir, sunDir);
    if (cosAngle > atmo.sunCosHalfAngle && raySphereNearest(vec3(0.0, atmo.cameraHeight, 0.0), dir, atmo.bottomRadius) < 0.0) {
        float solidAngle = 2.0 * PI * (1.0 - atmo.sunCosHalfAngle);
        vec3  transmittance = texture(transmittanceLut, transmittanceUv(atmo, atmo.cameraHeight, dir.y)).rgb;

        // Soft edge over the outer 10% of the radius (keeps MSAA / FXAA from sparkling)
        float halfAngle = acos(atmo.sunCosHalfAngle);
        float edge = 1.0 - smoothstep(0.9 * halfAngle, halfAngle, acos(clamp(cosAngle, -1.0, 1.0)));

        color += atmo.sunIlluminance * transmittance / solidAngle * edge;
    }

    // Same light units as the scene: sceneshader.frag folds PI into the BRDF (lit surfaces are PI x
    // brighter than physical), so the atmosphere's radiance is scaled by PI to keep the ratio.
    color *= PI;

    color += atmo.nightColor;

    // RGBA16F tops out at 65504: keep the sun disc finite.
    outColor = vec4(min(color, vec3(32768.0)), 1.0);
}
