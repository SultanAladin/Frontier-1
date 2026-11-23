//============================================================================================================================================
//                                                        DYNAMICSKYSURFACE.SLANG
//============================================================================================================================================
// 🧩 Continuous analytic sky for the editor and game viewport. This path uses no lookup tables: it evaluates
//    wavelength-weighted Rayleigh, Mie, ozone, twilight, horizon and ground terms directly in FP32.

struct SkyPush
{
    float SunElevationDegrees;
    float SunAzimuthDegrees;
    float SunIlluminance;
    float SunTemperatureKelvin;
    float SunAngularRadiusDegrees;
    float SunDiscIntensity;
    float SkyIntensity;
    float ExposureCompensation;
    float GroundAlbedo;
    float RayleighDensity;
    float RayleighScaleHeightKilometres;
    float MieDensity;
    float MieScaleHeightKilometres;
    float MieAsymmetry;
    float OzoneDensity;
    float CameraAltitudeKilometres;
    uint Quality;
};

[[vk::push_constant]] ConstantBuffer<SkyPush> Sky;
[[vk::binding(0, 0)]] RWTexture2D<float4> SkyOutput;

static const float Pi = 3.14159265358979323846f;

float3 TemperatureColour(float Kelvin)
{
    float t = clamp(Kelvin, 1000.0f, 12000.0f) / 100.0f;
    float r = t <= 66.0f ? 1.0f : clamp(1.2929362f * pow(t - 60.0f, -0.1332048f), 0.0f, 1.0f);
    float g = t <= 66.0f ? clamp(0.3900816f * log(t) - 0.6318414f, 0.0f, 1.0f)
                         : clamp(1.1298909f * pow(t - 60.0f, -0.0755148f), 0.0f, 1.0f);
    float b = t >= 66.0f ? 1.0f : (t <= 19.0f ? 0.0f : clamp(0.5432068f * log(t - 10.0f) - 1.1962541f, 0.0f, 1.0f));
    return float3(r, g, b);
}

// A curved-atmosphere airmass approximation. The finite horizon denominator prevents the old singular white seam.
float OpticalMass(float altitudeSine)
{
    float raised = max(altitudeSine, 0.0f);
    return min(40.0f, 1.0f / max(raised + 0.025f * exp(-11.0f * raised), 0.025f));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 index : SV_DispatchThreadID)
{
    uint width, height;
    SkyOutput.GetDimensions(width, height);
    if (index.x >= width || index.y >= height)
        return;

    float2 uv = (float2(index.xy) + 0.5f) / float2(width, height);
    float azimuth = (uv.x - 0.5f) * (2.0f * Pi);
    float elevation = (0.5f - uv.y) * Pi;
    float cosElevation = cos(elevation);
    float3 view = float3(sin(azimuth) * cosElevation, sin(elevation), cos(azimuth) * cosElevation);

    float sunElevation = Sky.SunElevationDegrees * Pi / 180.0f;
    float sunAzimuth = Sky.SunAzimuthDegrees * Pi / 180.0f;
    float3 sun = normalize(float3(sin(sunAzimuth) * cos(sunElevation), sin(sunElevation),
                                  cos(sunAzimuth) * cos(sunElevation)));
    float mu = clamp(dot(view, sun), -1.0f, 1.0f);

    float rayleighPhase = 0.0596831f * (1.0f + mu * mu);
    float g = clamp(Sky.MieAsymmetry, -0.95f, 0.95f);
    float mieBase = max(1.0f + g * g - 2.0f * g * mu, 0.0001f);
    float miePhase = 0.1193662f * (1.0f - g * g) * (1.0f + mu * mu) /
                     ((2.0f + g * g) * mieBase * sqrt(mieBase));

    float quality = clamp(float(Sky.Quality), 0.0f, 3.0f);
    float horizonSoftness = lerp(0.038f, 0.014f, quality / 3.0f);
    float aboveHorizon = smoothstep(-horizonSoftness, horizonSoftness * 0.55f, view.y);
    float viewMass = OpticalMass(view.y);
    float solarMass = OpticalMass(sun.y);
    float rayleighAltitude = exp(-max(Sky.CameraAltitudeKilometres, 0.0f) /
                                  max(Sky.RayleighScaleHeightKilometres, 0.1f));
    float mieAltitude = exp(-max(Sky.CameraAltitudeKilometres, 0.0f) /
                            max(Sky.MieScaleHeightKilometres, 0.05f));

    // Relative RGB extinction sampled at long, middle and short visible wavelengths.
    float3 betaRayleigh = float3(0.18f, 0.42f, 1.0f) * max(Sky.RayleighDensity, 0.0f) * rayleighAltitude;
    float betaMie = max(Sky.MieDensity, 0.0f) * mieAltitude;
    float3 betaOzone = float3(0.08f, 0.18f, 0.025f) * max(Sky.OzoneDensity, 0.0f);
    float3 sunTransmission = exp(-(betaRayleigh * 0.080f + betaMie * 0.018f + betaOzone * 0.13f) * solarMass);
    float3 rayleighScatter = 1.0f - exp(-betaRayleigh * viewMass * 0.115f);
    float mieScatter = 1.0f - exp(-betaMie * viewMass * 0.032f);

    // Solar-altitude gates follow civil (-6°), nautical (-12°), and astronomical (-18°) twilight.
    float daylight = smoothstep(-0.105f, 0.035f, sun.y);
    float civilTwilight = smoothstep(-0.105f, 0.02f, sun.y) * (1.0f - smoothstep(0.035f, 0.30f, sun.y));
    float nauticalTwilight = smoothstep(-0.208f, -0.105f, sun.y) *
                              (1.0f - smoothstep(-0.105f, -0.07f, sun.y)) * (1.0f - civilTwilight);
    float astronomical = smoothstep(-0.31f, -0.18f, sun.y) * (1.0f - smoothstep(-0.18f, -0.12f, sun.y));
    float nightAmount = 1.0f - smoothstep(-0.31f, -0.105f, sun.y);

    float solarEnergy = max(Sky.SunIlluminance, 0.0f) * max(Sky.SkyIntensity, 0.0f);
    float3 dayScatter = (rayleighScatter * rayleighPhase +
                         float3(1.0f, 0.91f, 0.76f) * mieScatter * miePhase * 0.24f) *
                        sunTransmission * solarEnergy * daylight * 2.2f;

    // A narrow atmospheric band appears across the horizon as the sun reaches it. Toward the sun it is
    // near-white; away from the sun it falls to warm gold. Below the horizon it turns orange, never magenta.
    float horizonBand = exp(-abs(view.y) * lerp(45.0f, 82.0f, quality / 3.0f));
    float nearHorizonSun = smoothstep(-0.12f, 0.055f, sun.y) * (1.0f - smoothstep(0.07f, 0.28f, sun.y));
    float towardSun = pow(max(mu, 0.0f), 2.25f);
    float sunriseWhite = smoothstep(-0.018f, 0.012f, sun.y);
    float3 horizonColour = lerp(float3(1.0f, 0.34f, 0.025f),
                                float3(1.0f, 0.91f, 0.70f), sunriseWhite);
    float3 warmHorizon = horizonColour * horizonBand * (0.28f + 0.72f * towardSun) *
                         nearHorizonSun * solarEnergy * 0.52f;

    // Twilight opposite the warm band remains desaturated blue. Keeping it out of the civil warm interval
    // prevents the red-plus-blue mixture that produced the previous purple sunset.
    float blueBand = exp(-abs(view.y) * 7.0f);
    float3 nauticalSky = float3(0.012f, 0.035f, 0.105f) * blueBand *
                          (nauticalTwilight + astronomical * 0.42f) * max(Sky.SkyIntensity, 0.0f);
    float3 twilightZenith = float3(0.006f, 0.022f, 0.080f) * sqrt(max(view.y, 0.0f)) *
                            civilTwilight * max(Sky.SkyIntensity, 0.0f);

    float zenith = clamp(view.y, 0.0f, 1.0f);
    float3 nightSky = lerp(float3(0.00055f, 0.00085f, 0.0017f),
                           float3(0.0015f, 0.0032f, 0.0085f), zenith) *
                      nightAmount * max(Sky.SkyIntensity, 0.0f);

    float multipleScatter = quality >= 2.0f
                          ? (quality - 1.0f) * 0.026f * exp(-abs(view.y) * 4.0f) * daylight
                          : 0.0f;
    float3 scattered = dayScatter + warmHorizon + nauticalSky + twilightZenith + nightSky +
                       float3(0.27f, 0.43f, 0.72f) * multipleScatter;

    // The lower hemisphere follows the time of day and albedo rather than becoming a flat neutral-grey plane.
    float sunsetAmount = civilTwilight * nearHorizonSun;
    float3 groundDay = float3(0.0045f, 0.0055f, 0.0065f) +
                       Sky.GroundAlbedo * lerp(float3(0.014f, 0.020f, 0.030f),
                                               float3(0.030f, 0.018f, 0.008f), sunsetAmount) * daylight;
    float3 groundNight = float3(0.00035f, 0.00050f, 0.00090f) * nightAmount;
    float3 radiance = lerp(groundDay + groundNight, scattered, aboveHorizon);

    float discRadius = max(Sky.SunAngularRadiusDegrees, 0.001f) * Pi / 180.0f;
    float discCosine = cos(discRadius);
    float discEdge = lerp(0.26f, 0.68f, quality / 3.0f);
    float disc = smoothstep(discCosine, min(1.0f, discCosine + (1.0f - discCosine) * discEdge), mu);
    float discVisible = smoothstep(-sin(discRadius), sin(discRadius), sun.y) * aboveHorizon;
    float3 discColour = TemperatureColour(Sky.SunTemperatureKelvin) *
                        lerp(float3(1.0f, 0.72f, 0.38f), sunTransmission, smoothstep(-0.03f, 0.12f, sun.y));
    radiance += discColour * disc * discVisible * Sky.SunDiscIntensity * solarEnergy * 2.5f;

    // This surface is sampled directly by the interface, so exposure, bounded highlight compression and
    // display transfer happen here exactly once.
    radiance *= exp2(Sky.ExposureCompensation);
    radiance = radiance / (1.0f + radiance);
    radiance = pow(max(radiance, 0.0f), float3(1.0f / 2.2f));
    SkyOutput[index.xy] = float4(radiance, 1.0f);
}
