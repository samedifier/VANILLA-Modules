// Ray-marched single scattering + multi-scattering lookup (Hillaire 2020).
// Include after atmosphere.glsl. The including shader declares:
//   sampler2D transmittanceLut;
//   sampler2D multiScatterLut;   (only read when useMultiScatter is true)

#ifndef VANILLA_ATMOSPHERE_SCATTER_GLSL
#define VANILLA_ATMOSPHERE_SCATTER_GLSL

struct ScatterResult {
    vec3 luminance;       // in-scattered light, sun illuminance = 1
    vec3 multiScatterAs1; // only meaningful with uniformPhase (multi-scattering LUT)
    vec3 transmittance;   // along the marched segment
};

vec3 sampleTransmittance(AtmosphereParams a, float height, float zenithCos) {
    return texture(transmittanceLut, transmittanceUv(a, height, zenithCos)).rgb;
}

// pos: atmosphere space (km), dir: normalized. tMaxLimit < 0 = no limit (march to the boundary).
ScatterResult integrateScattering(
    AtmosphereParams a, vec3 pos, vec3 dir, vec3 sunDir,
    float sampleCount, bool uniformPhase, bool useMultiScatter, bool withGround, float tMaxLimit)
{
    ScatterResult r;
    r.luminance       = vec3(0.0);
    r.multiScatterAs1 = vec3(0.0);
    r.transmittance   = vec3(1.0);

    float tBottom = raySphereNearest(pos, dir, a.bottomRadius);
    float tTop    = raySphereNearest(pos, dir, a.topRadius);

    float tMax;
    if (tBottom < 0.0) {
        if (tTop < 0.0) { return r; }   // outside, pointing away
        tMax = tTop;
    }
    else {
        tMax = (tTop > 0.0) ? min(tTop, tBottom) : tBottom;
    }
    if (tMaxLimit >= 0.0) { tMax = min(tMax, tMaxLimit); }

    float cosTheta   = dot(dir, sunDir);
    float phaseRay   = rayleighPhase(cosTheta);
    float phaseMie   = miePhase(a.mieAnisotropy, cosTheta);
    const float uniformPhaseValue = 1.0 / (4.0 * PI);

    float dt = tMax / sampleCount;
    vec3 throughput = vec3(1.0);

    for (float i = 0.0; i < sampleCount; i += 1.0) {
        float t = (i + 0.3) * dt;
        vec3  p = pos + t * dir;

        Medium m = sampleMedium(a, p);
        vec3 ext = max(m.extinction, vec3(1e-7));
        vec3 sampleTransmittanceValue = exp(-ext * dt);

        float height = length(p);
        vec3  up     = p / height;
        float sunZenithCos = dot(sunDir, up);

        vec3 transmittanceToSun = sampleTransmittance(a, height, sunZenithCos);

        vec3 phaseTimesScattering = uniformPhase
            ? m.scattering * uniformPhaseValue
            : m.scatteringMie * phaseMie + m.scatteringRay * phaseRay;

        // Planet shadow
        float tEarth = raySphereNearest(p, sunDir, a.bottomRadius);
        float earthShadow = (tEarth >= 0.0) ? 0.0 : 1.0;

        vec3 multiScatter = vec3(0.0);
        if (useMultiScatter) {
            multiScatter = texture(multiScatterLut, multiScatterUv(a, height, sunZenithCos)).rgb;
        }

        vec3 S = earthShadow * transmittanceToSun * phaseTimesScattering + multiScatter * m.scattering;

        // Analytic integration of S over the step (Hillaire's energy-conserving form)
        vec3 Sint  = (S - S * sampleTransmittanceValue) / ext;
        vec3 MSint = (m.scattering - m.scattering * sampleTransmittanceValue) / ext;

        r.luminance       += throughput * Sint;
        r.multiScatterAs1 += throughput * MSint;
        throughput        *= sampleTransmittanceValue;
    }

    // Light bounced off the ground (lambertian)
    if (withGround && tBottom >= 0.0 && tMax == tBottom) {
        vec3  p      = pos + tBottom * dir;
        float height = length(p);
        vec3  up     = p / height;
        float sunZenithCos = dot(sunDir, up);
        vec3  transmittanceToSun = sampleTransmittance(a, height, sunZenithCos);
        float NdotL = clamp(sunZenithCos, 0.0, 1.0);
        r.luminance += transmittanceToSun * throughput * NdotL * a.groundAlbedo / PI;
    }

    r.transmittance = throughput;
    return r;
}

#endif
