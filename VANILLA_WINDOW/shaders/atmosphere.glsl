// Physical atmosphere (Hillaire 2020, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique").
// Shared by the atmo_*.comp shaders (and later the sky / environment shaders).
//
// Units: kilometers. Atmosphere space: planet center at the origin, +Y up.
// The camera sits at (0, cameraHeight, 0); its horizontal position is ignored (the planet is huge).
// LUTs are built for a sun illuminance of 1: multiply by sunIlluminance when sampling.

#ifndef VANILLA_ATMOSPHERE_GLSL
#define VANILLA_ATMOSPHERE_GLSL

const float PI = 3.14159265358979;

// std430 mirror of VANILLA_GPUAtmosphere (vanilla_window_vulkan_utils.h)
struct AtmosphereParams {
    vec3  rayleighScattering; float rayleighScaleHeight;
    vec3  mieScattering;      float mieScaleHeight;
    vec3  mieExtinction;      float mieAnisotropy;
    vec3  ozoneAbsorption;    float bottomRadius;
    vec3  groundAlbedo;       float topRadius;
    vec3  sunDirection;       float cameraHeight;     // sunDirection points toward the sun
    vec3  sunIlluminance;     float sunCosHalfAngle;  // sun color * intensity; disc size
    vec3  nightColor;         float metersPerUnit;
    float groundHeight;       float aerialScale; float _pad1; float _pad2;   // aerialScale: distance multiplier
};

#define TRANSMITTANCE_LUT_W 256
#define TRANSMITTANCE_LUT_H 64
#define MULTISCATTER_LUT_SIZE 32
#define SKYVIEW_LUT_W 192
#define SKYVIEW_LUT_H 108
#define AERIAL_SIZE 32           // froxels per axis
#define AERIAL_DISTANCE_KM 32.0  // last slice ends here; slice s ends at AERIAL_DISTANCE_KM * ((s + 1) / AERIAL_SIZE)^2

// Ozone: tent profile centered at 25 km, 30 km wide.
#define OZONE_CENTER    25.0
#define OZONE_HALFWIDTH 15.0

// ---------------------------------------------------------------- geometry

// Nearest positive hit of a ray with a sphere at the origin, -1 if none.
float raySphereNearest(vec3 ro, vec3 rd, float radius) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) { return -1.0; }
    float s  = sqrt(disc);
    float t0 = -b - s;
    float t1 = -b + s;
    if (t0 >= 0.0) { return t0; }
    if (t1 >= 0.0) { return t1; }
    return -1.0;
}

// ---------------------------------------------------------------- medium

struct Medium {
    vec3 scattering;      // rayleigh + mie
    vec3 extinction;      // rayleigh + mie + ozone
    vec3 scatteringRay;
    vec3 scatteringMie;
};

Medium sampleMedium(AtmosphereParams a, vec3 p) {
    float h = max(length(p) - a.bottomRadius, 0.0);

    float densityRay   = exp(-h / a.rayleighScaleHeight);
    float densityMie   = exp(-h / a.mieScaleHeight);
    float densityOzone = max(0.0, 1.0 - abs(h - OZONE_CENTER) / OZONE_HALFWIDTH);

    Medium m;
    m.scatteringRay = a.rayleighScattering * densityRay;
    m.scatteringMie = a.mieScattering * densityMie;
    m.scattering    = m.scatteringRay + m.scatteringMie;
    m.extinction    = m.scatteringRay + a.mieExtinction * densityMie + a.ozoneAbsorption * densityOzone;
    return m;
}

float rayleighPhase(float cosTheta) {
    return 3.0 / (16.0 * PI) * (1.0 + cosTheta * cosTheta);
}

// Henyey-Greenstein. cosTheta = dot(viewDir, sunDir): forward peak when looking at the sun.
float miePhase(float g, float cosTheta) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * denom * sqrt(denom));
}

// ---------------------------------------------------------------- LUT parameterizations

// Transmittance LUT (Bruneton): x = distance to the top boundary, y = height.
vec2 transmittanceUv(AtmosphereParams a, float viewHeight, float viewZenithCos) {
    float H    = sqrt(max(0.0, a.topRadius * a.topRadius - a.bottomRadius * a.bottomRadius));
    float rho  = sqrt(max(0.0, viewHeight * viewHeight - a.bottomRadius * a.bottomRadius));
    float disc = viewHeight * viewHeight * (viewZenithCos * viewZenithCos - 1.0) + a.topRadius * a.topRadius;
    float d    = max(0.0, -viewHeight * viewZenithCos + sqrt(max(disc, 0.0)));
    float dMin = a.topRadius - viewHeight;
    float dMax = rho + H;
    float xMu  = (d - dMin) / max(dMax - dMin, 1e-6);
    float xR   = rho / max(H, 1e-6);
    return vec2(xMu, xR);
}

void transmittanceParams(AtmosphereParams a, vec2 uv, out float viewHeight, out float viewZenithCos) {
    float H   = sqrt(max(0.0, a.topRadius * a.topRadius - a.bottomRadius * a.bottomRadius));
    float rho = H * uv.y;
    viewHeight = sqrt(rho * rho + a.bottomRadius * a.bottomRadius);

    float dMin = a.topRadius - viewHeight;
    float dMax = rho + H;
    float d    = dMin + uv.x * (dMax - dMin);

    viewZenithCos = (d == 0.0) ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * viewHeight * d);
    viewZenithCos = clamp(viewZenithCos, -1.0, 1.0);
}

// Multi-scattering LUT: x = sun zenith cos, y = height.
vec2 multiScatterUv(AtmosphereParams a, float viewHeight, float sunZenithCos) {
    vec2 uv = vec2(sunZenithCos * 0.5 + 0.5,
                   (viewHeight - a.bottomRadius) / (a.topRadius - a.bottomRadius));
    uv = clamp(uv, 0.0, 1.0);
    // Texel centers
    return (uv * float(MULTISCATTER_LUT_SIZE - 1) + 0.5) / float(MULTISCATTER_LUT_SIZE);
}

// Sky-view LUT: x = world azimuth (0..2pi, wraps), y = zenith angle, squeezed toward the horizon.
vec3 skyViewDirection(AtmosphereParams a, vec2 uv) {
    float viewHeight = a.cameraHeight;
    float vHorizon   = sqrt(max(0.0, viewHeight * viewHeight - a.bottomRadius * a.bottomRadius));
    float beta       = acos(clamp(vHorizon / viewHeight, -1.0, 1.0));   // angle below the horizontal plane
    float zenithHorizonAngle = PI - beta;

    float viewZenithAngle;
    if (uv.y < 0.5) {
        float coord = 1.0 - 2.0 * uv.y;
        coord = 1.0 - coord * coord;
        viewZenithAngle = zenithHorizonAngle * coord;
    }
    else {
        float coord = uv.y * 2.0 - 1.0;
        viewZenithAngle = zenithHorizonAngle + beta * coord * coord;
    }

    float azimuth = uv.x * 2.0 * PI;
    float s = sin(viewZenithAngle);
    return vec3(s * cos(azimuth), cos(viewZenithAngle), s * sin(azimuth));
}

// Planet surface seen along dir from camPos (rays that hit the ground; zero otherwise), for a sun illuminance
// already applied (atmo.sunIlluminance): lambertian groundAlbedo lit by the sun through the air, then dimmed by
// the air between the ground and the camera. The air's own glow in front of it is in the sky-view LUT.
// Transmittance between two points of one ray: T(ground -> top) / T(camera -> top), both looking back up the ray.
vec3 groundRadiance(AtmosphereParams a, sampler2D transmittanceLut, vec3 camPos, vec3 dir) {
    float tGround = raySphereNearest(camPos, dir, a.bottomRadius);
    if (tGround < 0.0) { return vec3(0.0); }

    vec3  p      = camPos + tGround * dir;
    float height = length(p);
    vec3  up     = p / height;
    vec3  sunDir = normalize(a.sunDirection);
    float sunCos = dot(up, sunDir);

    vec3 toSun = texture(transmittanceLut, transmittanceUv(a, height, sunCos)).rgb;
    vec3 lit   = a.groundAlbedo / PI * a.sunIlluminance * toSun * max(sunCos, 0.0);

    float camHeight = length(camPos);
    vec3  tGroundUp = texture(transmittanceLut, transmittanceUv(a, height,    dot(-dir, up))).rgb;
    vec3  tCamUp    = texture(transmittanceLut, transmittanceUv(a, camHeight, dot(-dir, camPos / camHeight))).rgb;
    vec3  view      = clamp(tGroundUp / max(tCamUp, vec3(1e-6)), 0.0, 1.0);

    return lit * view;
}

vec2 skyViewUv(AtmosphereParams a, vec3 dir) {
    float viewHeight = a.cameraHeight;
    float vHorizon   = sqrt(max(0.0, viewHeight * viewHeight - a.bottomRadius * a.bottomRadius));
    float beta       = acos(clamp(vHorizon / viewHeight, -1.0, 1.0));
    float zenithHorizonAngle = PI - beta;

    float viewZenithAngle = acos(clamp(dir.y, -1.0, 1.0));

    float v;
    if (viewZenithAngle < zenithHorizonAngle) {
        float coord = viewZenithAngle / zenithHorizonAngle;
        coord = 1.0 - sqrt(max(1.0 - coord, 0.0));
        v = coord * 0.5;
    }
    else {
        float coord = (viewZenithAngle - zenithHorizonAngle) / max(beta, 1e-6);
        v = sqrt(clamp(coord, 0.0, 1.0)) * 0.5 + 0.5;
    }

    float u = atan(dir.z, dir.x) / (2.0 * PI);
    u = fract(u);   // repeat sampler on U

    // Keep v off the clamped edges.
    v = (v * float(SKYVIEW_LUT_H - 1) + 0.5) / float(SKYVIEW_LUT_H);
    return vec2(u, v);
}

#endif
