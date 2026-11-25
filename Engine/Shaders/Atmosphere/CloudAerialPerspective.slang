/*==========================================================================================================================================
                                                        CLOUDAERIALPERSPECTIVE.SLANG
==========================================================================================================================================*/
// 🧩 Aerial scattering and optical-centroid foreground haze composition (31f137f single-source module).

#ifndef FRONTIER_CLOUD_AERIAL_PERSPECTIVE
#define FRONTIER_CLOUD_AERIAL_PERSPECTIVE 1

#include "CloudTransport.slang"

struct CloudAirSegment
{
    float3 Radiance;
    float3 Transmittance;
};

CloudAirSegment IntegrateCloudAir(
    CloudSpecification Specification,
    float3             Origin,
    float3             Direction,
    float              LimitM,
    uint               Steps,
    bool               CloudShafts)
{
    CloudAirSegment Integral;
    Integral.Radiance      = float3(0.0, 0.0, 0.0);
    Integral.Transmittance = float3(1.0, 1.0, 1.0);
    float2 Atmosphere      = PlanetCloudSphere(Origin, Direction, Specification.RadiusM + 100000.0);
    float  EnterM          = max(0.0, Atmosphere.x);
    float  ExitM           = min(LimitM, Atmosphere.y);
    if (ExitM <= EnterM) return Integral;

    float  Mu            = dot(Direction, Specification.SunDirection);
    float  RayleighPhase = (3.0 / (16.0 * CloudPi)) * (1.0 + Mu * Mu);
    float  AerosolPhase  = CloudTransportHG(Mu, 0.78);
    float3 BetaRayleigh  = QueryCloudRayleighExtinction(Specification);
    float3 BetaAerosol   = QueryCloudAerosolExtinction(Specification);
    float3 BetaOzone     = Specification.AtmosphereStrengths.z * float3(0.650e-6, 1.881e-6, 0.085e-6);

    // Match Frontier SkyView scatter brightness relative to cloud solar scale (0.24 / 0.72 = 0.333)
    float3 AirSolar = Specification.SolarIrradiance * 0.3333333;

    float ClosestM = clamp(-dot(Origin, Direction), EnterM, ExitM);
    for (uint SideIndex = 0u; SideIndex < 2u; ++SideIndex)
    {
        float LengthM = SideIndex == 0u ? ClosestM - EnterM : ExitM - ClosestM;
        if (LengthM < 0.1) continue;
        float PreviousM = SideIndex == 0u ? EnterM : ClosestM;
        for (uint Index = 0u; Index < Steps; ++Index)
        {
            float Fraction       = float(Index + 1u) / float(Steps);
            float RadialFraction = SideIndex == 0u ? 1.0 - Fraction : Fraction;
            float DistanceM =
                ClosestM + LengthM * (0.30 * RadialFraction + 0.70 * RadialFraction * RadialFraction * RadialFraction) *
                               (SideIndex == 0u ? -1.0 : 1.0);
            float  Ds                   = DistanceM - PreviousM;
            float  MidpointM            = (DistanceM + PreviousM) * 0.5;
            PreviousM                   = DistanceM;
            float3 Position             = Origin + Direction * MidpointM;
            float  AltitudeM            = max(0.0, PlanetCloudAltitude(Position, Specification.RadiusM));
            float3 Rayleigh             = BetaRayleigh * exp(-AltitudeM / max(400.0, Specification.AirScaleHeightsM.x));
            float3 Aerosol              = BetaAerosol * exp(-AltitudeM / max(200.0, Specification.AirScaleHeightsM.y));
            float3 Ozone                = BetaOzone * saturate(1.0 - abs(AltitudeM - 25000.0) / 16000.0);
            float3 Extinction           = max(float3(1.0e-8), Rayleigh + Aerosol + Ozone);
            float3 SegmentTransmittance = exp(-Extinction * Ds);
            float3 SunTransmittance     = CloudSolarAirTransmittance(Specification, Position);
            float  SunUp                = dot(normalize(Position), Specification.SunDirection);
            float  Twilight             = smoothstep(-0.21, 0.095, SunUp + AltitudeM * 0.000002);
            float  Visibility           = 1.0;
            if (CloudShafts && Mu > 0.69 && AltitudeM < 4300.0 && MidpointM < 65000.0 &&
                dot(SunTransmittance, SunTransmittance) > 0.0005)
            {
                Visibility = exp(-CloudSolarOpticalDepth(Specification, Position, 4u));
            }
            float3 Source = SunTransmittance * Visibility * (Rayleigh * RayleighPhase + 0.88 * Aerosol * AerosolPhase);
            Source += (Rayleigh + 0.48 * Aerosol) * Twilight * float3(0.019, 0.025, 0.034);
            Integral.Radiance += Integral.Transmittance * AirSolar * Source *
                                 (1.0 - SegmentTransmittance) / Extinction;
            Integral.Transmittance *= SegmentTransmittance;
        }
    }
    return Integral;
}

float3 CloudRadianceThroughAir(
    CloudSpecification     Specification,
    float3                 Origin,
    float3                 Direction,
    CloudTransportIntegral Cloud,
    uint                   AirSteps)
{
    if (Cloud.Transmittance >= 0.999999) return Cloud.Radiance;
    CloudAirSegment Front = IntegrateCloudAir(Specification, Origin, Direction, Cloud.DistanceM, AirSteps, false);
    return Front.Transmittance * Cloud.Radiance + (1.0 - Cloud.Transmittance) * Front.Radiance;
}

#endif
