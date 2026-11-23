/*==========================================================================================================================================
                                                            CLOUDTRANSPORT.SLANG
==========================================================================================================================================*/
// 🧩 Ordered spherical volume intervals, Beer attenuation, and solar multiple-order scattering (31f137f single-source module).

#ifndef FRONTIER_PLANETARY_CLOUD_TRANSPORT
#define FRONTIER_PLANETARY_CLOUD_TRANSPORT

#include "CloudDensity.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                  SPHERICAL INTERVALS
//------------------------------------------------------------------------------------------------------------------------

float PlanetCloudAltitude(float3 Position, float PlanetRadiusM)
{
    return length(Position) - PlanetRadiusM;
}

float2 PlanetCloudSphere(float3 Position, float3 Direction, float RadiusM)
{
    float RadialDistanceM   = length(Position);
    float RayProjection     = dot(Position, Direction);
    float QuadraticConstant = (RadialDistanceM - RadiusM) * (RadialDistanceM + RadiusM);
    float Discriminant      = RayProjection * RayProjection - QuadraticConstant;
    if (Discriminant < 0.0) return float2(1.0, -1.0);
    float StableRoot    = -RayProjection - ((RayProjection < 0.0) ? -1.0 : 1.0) * sqrt(Discriminant);
    float CompanionRoot = (abs(StableRoot) > 1.0e-6) ? QuadraticConstant / StableRoot : StableRoot;
    return float2(min(StableRoot, CompanionRoot), max(StableRoot, CompanionRoot));
}

float PlanetCloudGroundDistance(float3 Origin, float3 Direction, float RadiusM)
{
    float2 Ground = PlanetCloudSphere(Origin, Direction, RadiusM);
    if (Ground.x > Ground.y || Ground.y <= 0.0) return 1.0e9;
    if (Ground.x > 0.0) return Ground.x;
    return (dot(Origin, Direction) < 0.0) ? 0.0 : 1.0e9;
}

float4 PlanetCloudLayerSpans(float3 Origin, float3 Direction, float RadiusM, CloudDeck Layer, float LimitM)
{
    float2 Outer = PlanetCloudSphere(Origin, Direction, RadiusM + Layer.TopM);
    Outer        = float2(max(0.0, Outer.x), min(LimitM, Outer.y));
    if (Outer.y <= Outer.x) return float4(0.0, 0.0, 0.0, 0.0);
    float2 Inner = PlanetCloudSphere(Origin, Direction, RadiusM + Layer.BottomM);
    if (Inner.y <= Outer.x || Inner.x >= Outer.y || Inner.x >= Inner.y) return float4(Outer, 0.0, 0.0);
    return float4(Outer.x, min(Outer.y, Inner.x), max(Outer.x, Inner.y), Outer.y);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SOLAR EXTINCTION
//------------------------------------------------------------------------------------------------------------------------

float3 QueryCloudRayleighExtinction(CloudSpecification Specification)
{
    return float3(5.802e-6, 13.558e-6, 33.1e-6) * max(0.0, Specification.AtmosphereStrengths.x);
}

float3 QueryCloudAerosolExtinction(CloudSpecification Specification)
{
    return float3(4.40e-6) * max(0.0, Specification.AtmosphereStrengths.y);
}

float CloudAirColumn(float ScaleHeightM, float RadiusM, float AltitudeM, float Mu)
{
    float AbsoluteCosine = abs(Mu);
    float UpwardColumnM =
        2.0 * ScaleHeightM /
        (AbsoluteCosine + sqrt(AbsoluteCosine * AbsoluteCosine + 8.0 * ScaleHeightM / (CloudPi * RadiusM)));
    float ClosestApproachRiseM = (RadiusM + AltitudeM) * Mu * Mu / (1.0 + sqrt(saturate(1.0 - Mu * Mu)));
    float DownwardColumnM =
        sqrt(2.0 * CloudPi * (RadiusM + AltitudeM) * ScaleHeightM) * exp(min(9.0, ClosestApproachRiseM / ScaleHeightM)) -
        UpwardColumnM;
    return exp(-max(0.0, AltitudeM) / ScaleHeightM) * ((Mu < 0.0) ? DownwardColumnM : UpwardColumnM);
}

float3 CloudSolarAirTransmittance(CloudSpecification Specification, float3 Position)
{
    float RadiusM         = length(Position);
    float AltitudeM       = max(0.0, RadiusM - Specification.RadiusM);
    float Mu              = dot(Position, Specification.SunDirection) / RadiusM;
    float TangentBoundary = -sqrt(saturate(AltitudeM * (RadiusM + Specification.RadiusM) / (RadiusM * RadiusM)));
    float SolarVisibility = smoothstep(-0.00465, 0.00465, Mu - TangentBoundary);
    if (SolarVisibility <= 0.0) return float3(0.0, 0.0, 0.0);
    float RayleighColumnM =
        CloudAirColumn(max(400.0, Specification.AirScaleHeightsM.x), Specification.RadiusM, AltitudeM, Mu);
    float AerosolColumnM =
        CloudAirColumn(max(200.0, Specification.AirScaleHeightsM.y), Specification.RadiusM, AltitudeM, Mu);
    float  OzoneColumnM = 15000.0 * exp(-max(0.0, AltitudeM - 25000.0) / 8000.0) / max(0.15, abs(Mu));
    float3 Tau          = QueryCloudRayleighExtinction(Specification) * RayleighColumnM +
                          QueryCloudAerosolExtinction(Specification) * AerosolColumnM +
                          Specification.AtmosphereStrengths.z * float3(0.650e-6, 1.881e-6, 0.085e-6) * OzoneColumnM;
    return SolarVisibility * exp(-Tau);
}

float IntegrateCloudSolarDepthWithWeather(
    CloudSpecification Specification,
    CloudDeck          Layer,
    float3             Position,
    CloudWeatherSample Weather,
    float2             Span,
    uint               Steps,
    float              FootprintM)
{
    if (Span.y <= Span.x) return 0.0;
    float LengthM = Span.y - Span.x;
    float Tau     = 0.0;
    float StartM  = 0.0;
    for (uint Index = 0u; Index < Steps; ++Index)
    {
        float  Fraction       = float(Index + 1u) / float(Steps);
        float  EndM           = LengthM * Fraction * Fraction;
        float  SampleM        = Span.x + 0.5 * (StartM + EndM);
        float  SegmentM       = EndM - StartM;
        float3 SamplePosition = Position + Specification.SunDirection * SampleM;
        float3 Adv            = CloudWindCoordinates(Specification, SamplePosition, Layer.Category);
        float  SampleSigma    = SamplePlanetCloudShadowDensity(
            Specification, Layer, SamplePosition, Adv, Weather, max(FootprintM, 16.0 + 0.18 * SegmentM));
        Tau += SegmentM * SampleSigma;
        if (Tau >= 18.0) return 18.0;
        StartM = EndM;
    }
    return Tau;
}

float CloudSolarOpticalDepthWithWeather(
    CloudSpecification Specification,
    float3             Position,
    CloudWeatherSample Weather,
    uint               Steps,
    float              FootprintM = 0.0)
{
    if (!Specification.UseClouds || !Specification.UseCloudShadows || Specification.Coverage <= 0.001 ||
        Specification.Extinction <= 0.001)
        return 0.0;
    float     LimitM = PlanetCloudGroundDistance(Position, Specification.SunDirection, Specification.RadiusM);
    float     Tau    = 0.0;
    CloudDeck Layer  = PlanetCloudLayer(Specification, 0u);
    float4    Spans  = PlanetCloudLayerSpans(Position, Specification.SunDirection, Specification.RadiusM, Layer, LimitM);
    for (uint IntervalIndex = 0u; IntervalIndex < 2u && Tau < 18.0; ++IntervalIndex)
    {
        float2 Span = IntervalIndex == 0u ? Spans.xy : Spans.zw;
        Tau += IntegrateCloudSolarDepthWithWeather(Specification, Layer, Position, Weather, Span, Steps, FootprintM);
    }
    return min(18.0, Tau);
}

float CloudSolarOpticalDepth(CloudSpecification Specification, float3 Position, uint Steps, float FootprintM = 0.0)
{
    if (!Specification.UseClouds || !Specification.UseCloudShadows || Specification.Coverage <= 0.001 ||
        Specification.Extinction <= 0.001)
        return 0.0;
    CloudDeck          Layer   = PlanetCloudLayer(Specification, 0u);
    CloudWeatherSample Weather = CloudWeatherAtPoint(Specification, Position, Layer.Category);
    return CloudSolarOpticalDepthWithWeather(Specification, Position, Weather, Steps, FootprintM);
}

//------------------------------------------------------------------------------------------------------------------------
//                                          NORMALIZED PHASE AND REDISTRIBUTED LIGHT
//------------------------------------------------------------------------------------------------------------------------

float CloudTransportHG(float Mu, float Asymmetry)
{
    float AsymmetrySquared   = Asymmetry * Asymmetry;
    float AngularDenominator = max(1.0e-4, 1.0 + AsymmetrySquared - 2.0 * Asymmetry * Mu);
    return (1.0 - AsymmetrySquared) / (4.0 * CloudPi * AngularDenominator * sqrt(AngularDenominator));
}

float CloudDualLobe(float Mu, float Asymmetry, float Scale)
{
    return 0.86 * CloudTransportHG(Mu, Asymmetry * Scale) + 0.14 * CloudTransportHG(Mu, -0.25 * Scale);
}

float3 CloudScatteringOrderPhases(CloudSpecification Specification, float3 Direction)
{
    float Mu        = dot(Direction, Specification.SunDirection);
    float Asymmetry = clamp(Specification.PhaseG, 0.35, 0.92);
    return float3(CloudDualLobe(Mu, Asymmetry, 1.0),
                  CloudDualLobe(Mu, Asymmetry, 0.49),
                  CloudDualLobe(Mu, Asymmetry, 0.19));
}

float3 PlanetCloudSource(
    CloudSpecification Specification,
    CloudDeck          Layer,
    float3             Position,
    CloudWeatherSample Weather,
    float              Sigma,
    float3             OrderPhases,
    float3             ViewDirection,
    float              FootprintM)
{
    float3 Direct = Specification.SolarIrradiance * CloudSolarAirTransmittance(Specification, Position);
    float  SolarTau = 0.0;
    if (dot(Direct, Direct) > 1.0e-6)
        SolarTau = CloudSolarOpticalDepthWithWeather(Specification, Position, Weather, Specification.LightSteps, FootprintM);

    float3 AttenuatedOrders = exp(-SolarTau * float3(1.0, 0.35, 0.12));
    float3 Scattered        = Direct * dot(OrderPhases * float3(1.0, 0.45, 0.20), AttenuatedOrders);

    float HeightFraction =
        saturate((PlanetCloudAltitude(Position, Specification.RadiusM) - Layer.BottomM) / (Layer.TopM - Layer.BottomM));
    float SunUp = dot(normalize(Position), Specification.SunDirection);
    float Day   = smoothstep(-0.20, 0.23, SunUp);
    float Warm  = exp(-abs(SunUp - 0.045) * 9.5) * smoothstep(-0.20, -0.025, SunUp);

    float  SkyVisibility = lerp(1.0, 0.16 + 0.84 * exp(-0.24 * SolarTau), smoothstep(0.12, 0.50, SunUp));
    float  Twilight      = smoothstep(-0.30, -0.12, SunUp);
    float3 Sky =
        Specification.SkyAmbient * (0.025 * Twilight + 0.36 * Day) * lerp(0.48, 1.0, HeightFraction) * SkyVisibility;
    float3 Bounce = (1.0 - HeightFraction) * (float3(0.032, 0.048, 0.071) * Day + float3(0.077, 0.029, 0.015) * Warm);

    if (Specification.Formation == 10u) // Nacreous stratospheric iridescence
    {
        float Mu    = dot(ViewDirection, Specification.SunDirection);
        float Phase = Mu * 9.0 + HeightFraction * 5.0 + (Position.x + Position.y) * 0.00020;
        Scattered *= float3(0.98 + 0.28 * sin(Phase), 0.95 + 0.28 * sin(Phase + 2.094), 1.04 + 0.30 * sin(Phase + 4.188));
    }
    else if (Specification.Formation == 11u) // Pyrocumulus warm ember base under-glow
    {
        float CoreDense = smoothstep(Layer.Sigma * 0.22, Layer.Sigma * 0.85, Sigma);
        Bounce += float3(1.25, 0.34, 0.05) * pow(1.0 - HeightFraction, 2.8) * CoreDense;
    }

    return Scattered + Sky + Bounce * (dot(Specification.SolarIrradiance, float3(0.3333333)) / 16.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ORDERED VIEW INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

CloudTransportIntegral TracePlanetaryClouds(
    CloudSpecification Specification,
    float3             Origin,
    float3             Direction,
    float2             Pixel,
    float              LimitM)
{
    CloudTransportIntegral Integral;
    Integral.Radiance      = float3(0.0, 0.0, 0.0);
    Integral.Transmittance = 1.0;
    Integral.DistanceM     = 0.0;
    if (!Specification.UseClouds || Specification.Coverage <= 0.0 || Specification.Extinction <= 0.0) return Integral;

    CloudDeck Layer         = PlanetCloudLayer(Specification, 0u);
    float     ViewAltitudeM = length(Origin) - Specification.RadiusM;
    if (ViewAltitudeM < Layer.BottomM - 10.0 && Direction.z < -0.02 && Specification.UniformExtinction < 0.0)
        return Integral;

    LimitM                  = min(LimitM, PlanetCloudGroundDistance(Origin, Direction, Specification.RadiusM));
    float4    Spans         = PlanetCloudLayerSpans(Origin, Direction, Specification.RadiusM, Layer, LimitM);
    float     Jitter        = CloudBlueJitter(Pixel);
    float3    Phases        = CloudScatteringOrderPhases(Specification, Direction);

    for (uint IntervalIndex = 0u; IntervalIndex < 2u; ++IntervalIndex)
    {
        float NearM = IntervalIndex == 0u ? Spans.x : Spans.z;
        float FarM  = IntervalIndex == 0u ? Spans.y : Spans.w;
        if (FarM <= NearM) continue;
        float  LengthM     = FarM - NearM;
        float  Growth      = ViewAltitudeM > 19000.0 ? 0.0 : clamp(log(1.0 + LengthM / 1900.0), 0.1, 4.8);
        float  Denominator = max(1.0e-6, exp(Growth) - 1.0);
        float  LastM       = NearM;
        float  LitAtM      = -1.0e9;
        float3 Source      = float3(0.0, 0.0, 0.0);

        for (uint Index = 0u; Index < Specification.Steps && Integral.Transmittance > 0.0001; ++Index)
        {
            float  Fraction  = float(Index + 1u) / float(Specification.Steps);
            float  NextM     = NearM + LengthM * (Growth < 0.01 ? Fraction : (exp(Growth * Fraction) - 1.0) / Denominator);
            float  DistanceM = NextM - LastM;
            float  SampleM   = LastM + DistanceM * (0.40 + Jitter * 0.20);
            float3 Position  = Origin + Direction * SampleM;
            float  FootprintM = max(1.0, SampleM * Specification.PixelAngle);
            float3 Advected   = CloudWindCoordinates(Specification, Position, Layer.Category);
            CloudWeatherSample Weather = CloudWeatherFromAdvectedNormal(Specification, Advected / length(Advected));
            float  Sigma               = SamplePlanetCloudDensityAdvected(Specification, Layer, Position, Advected, Weather, FootprintM, false);
            if (Sigma > 1.0e-9)
            {
                float SegmentTransmittance = exp(-Sigma * DistanceM);
                float Weight               = Integral.Transmittance * (1.0 - SegmentTransmittance);
                float LightStrideM         = clamp(FootprintM * 0.75 + 120.0, 100.0, 280.0);
                if (SampleM - LitAtM >= LightStrideM)
                {
                    Source = PlanetCloudSource(Specification, Layer, Position, Weather, Sigma, Phases, Direction, FootprintM);
                    LitAtM = SampleM;
                }
                Integral.Radiance += Weight * Source;
                Integral.DistanceM += Weight * SampleM;
                Integral.Transmittance *= SegmentTransmittance;
            }
            LastM = NextM;
        }
    }

    float Opacity = 1.0 - Integral.Transmittance;
    if (Opacity > 1.0e-5) Integral.DistanceM /= Opacity;
    return Integral;
}

#endif
