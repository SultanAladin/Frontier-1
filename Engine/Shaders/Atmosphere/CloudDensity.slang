/*==========================================================================================================================================
                                                             CLOUDDENSITY.SLANG
==========================================================================================================================================*/
// 🧩 Seamless 3D planetary condensate across all 12 Frontier CloudCategoryType formations + OrbitalCyclone (31f137f core).

#ifndef FRONTIER_PLANETARY_CLOUD_DENSITY
#define FRONTIER_PLANETARY_CLOUD_DENSITY

#include "CloudNoise.slang"

static const float CloudPi = 3.14159265358979323846;

struct CloudSpecification
{
    float  RadiusM;
    float  Coverage;
    float  Extinction;
    float  FloorM;
    float  ThicknessM;
    float  FeatureScale;
    float  Anvil;
    float  PhaseG;
    float  Seed;
    float  PixelAngle;
    float  UniformExtinction;
    float  TimeSeconds;
    float2 WindVelocityMps;
    float  Shear;
    float  Veer;
    float  Turbulence;
    float  CycloneSpeedMps;
    float  CycloneEyeM;
    float3 CycloneAxis;
    float3 SunDirection;
    float3 SolarIrradiance;
    float3 SkyAmbient;
    float3 AtmosphereStrengths;
    float2 AirScaleHeightsM;
    uint   Steps;
    uint   LightSteps;
    uint   Formation;
    bool   UseClouds;
    bool   UseCloudShadows;
};

struct CloudDeck
{
    float BottomM;
    float TopM;
    float Amount;
    float Sigma;
    uint  Category;
};

struct CloudRayInterval
{
    float NearM;
    float FarM;
    uint  Layer;
};

struct CloudTransportIntegral
{
    float3 Radiance;
    float  Transmittance;
    float  DistanceM;
};

struct CloudWeatherSample
{
    float Moisture;
    float Type;
    float Lift;
    float Rain;
    float Bands;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 TANGENT WIND CHARACTERISTICS
//------------------------------------------------------------------------------------------------------------------------

float2 CloudRotationSinCos(float Theta)
{
    float Reduced  = Theta - 2.0 * CloudPi * floor(Theta / (2.0 * CloudPi) + 0.5);
    float Polarity = 1.0;
    if (Reduced > 0.5 * CloudPi)
    {
        Reduced  = CloudPi - Reduced;
        Polarity = -1.0;
    }
    if (Reduced < -0.5 * CloudPi)
    {
        Reduced  = -CloudPi - Reduced;
        Polarity = -1.0;
    }
    float T2 = Reduced * Reduced;
    float Sine =
        Reduced *
        (1.0 + T2 * (-1.0 / 6.0 +
                     T2 * (1.0 / 120.0 + T2 * (-1.0 / 5040.0 +
                                               T2 * (1.0 / 362880.0 + T2 * (-1.0 / 39916800.0 + T2 / 6227020800.0))))));
    float Cosine =
        Polarity *
        (1.0 + T2 * (-0.5 + T2 * (1.0 / 24.0 + T2 * (-1.0 / 720.0 + T2 * (1.0 / 40320.0 + T2 * (-1.0 / 3628800.0 +
                                                                                                T2 / 479001600.0))))));
    return float2(Sine, Cosine);
}

float3 RotateCloudPosition(float3 Position, float3 Axis, float Angle)
{
    if (Angle == 0.0) return Position;
    float2 Trig    = CloudRotationSinCos(Angle);
    float3 Rotated = Position * Trig.y + cross(Axis, Position) * Trig.x + Axis * dot(Axis, Position) * (1.0 - Trig.y);
    return Rotated * (length(Position) / max(1.0e-6, length(Rotated)));
}

float CloudGeodesicRadius(CloudSpecification Specification, float3 Normal)
{
    return Specification.RadiusM *
           atan2(length(cross(Specification.CycloneAxis, Normal)), dot(Specification.CycloneAxis, Normal));
}

float CloudCycloneAngularSpeed(CloudSpecification Specification, float RadiusM, float AltitudeM)
{
    float MaximumWindRadiusM = max(12000.0, Specification.CycloneEyeM * 1.8);
    float AngularSpeed       = 2.0 * Specification.CycloneSpeedMps * MaximumWindRadiusM /
                         (RadiusM * RadiusM + MaximumWindRadiusM * MaximumWindRadiusM);
    float Outflow = lerp(1.0, -0.16, smoothstep(7200.0, 14500.0, AltitudeM));
    return AngularSpeed * Outflow * exp(-pow(RadiusM / 620000.0, 4.0));
}

float3 CloudWindCoordinates(CloudSpecification Specification, float3 Position, uint Category)
{
    if (Specification.TimeSeconds == 0.0) return Position;
    float  Radius    = length(Position);
    float  AltitudeM = Radius - Specification.RadiusM;
    float  HeightKm  = max(0.0, AltitudeM - Specification.FloorM) * 0.001;
    float  WindTurn  = HeightKm * Specification.Veer;
    float2 Trig      = CloudRotationSinCos(WindTurn);
    float2 Wind =
        float2(Specification.WindVelocityMps.x * Trig.y - Specification.WindVelocityMps.y * Trig.x,
               Specification.WindVelocityMps.x * Trig.x + Specification.WindVelocityMps.y * Trig.y);
    float WindSpeed = length(Wind);
    if (WindSpeed > 1.0e-5)
    {
        float3 WindAxis  = float3(-Wind.y, Wind.x, 0.0) / WindSpeed;
        float  WindAngle = WindSpeed * Specification.TimeSeconds * (1.0 + Specification.Shear * HeightKm) / Specification.RadiusM;
        Position = RotateCloudPosition(Position, WindAxis, -WindAngle);
    }
    if (Specification.Formation == 12u)
    {
        float StormRadiusM = CloudGeodesicRadius(Specification, Position / Radius);
        float AngularSpeed = CloudCycloneAngularSpeed(Specification, StormRadiusM, AltitudeM);
        Position = RotateCloudPosition(Position, Specification.CycloneAxis, -AngularSpeed * Specification.TimeSeconds);
    }
    return Position;
}

// Map Frontier CloudCategoryType (0..11) + OrbitalCyclone (12) cleanly to single non-overlapping CloudDeck
CloudDeck PlanetCloudLayer(CloudSpecification Specification, uint Index)
{
    CloudDeck Layer;
    Layer.BottomM  = max(100.0, Specification.FloorM);
    Layer.TopM     = Layer.BottomM + max(100.0, Specification.ThicknessM);
    Layer.Amount   = Specification.Coverage;
    Layer.Sigma    = 0.0042 * Specification.Extinction;
    Layer.Category = 0u;

    uint Fmt = Specification.Formation;
    if (Fmt == 5u || Fmt == 10u) // Cirrus, Nacreous -> High ice sheets
    {
        Layer.Category = 2u;
        Layer.Sigma    = (Fmt == 10u ? 0.0012 : 0.00055) * Specification.Extinction;
    }
    else if (Fmt == 0u || Fmt == 4u || Fmt == 6u || Fmt == 8u || Fmt == 9u) // Stratus, Altostratus, Lenticular, KelvinHelmholtz, Asperitas
    {
        Layer.Category = 1u;
        Layer.Sigma    = (Fmt == 4u ? 0.0018 : 0.0032) * Specification.Extinction;
    }
    else if (Fmt == 12u) // OrbitalCyclone
    {
        Layer.Category = 0u;
        Layer.Sigma    = 0.0026 * Specification.Extinction;
    }
    else // Stratocumulus(1), Cumulus(2), Cumulonimbus(3), Mammatus(7), Pyrocumulus(11)
    {
        Layer.Category = 0u;
        Layer.Sigma    = (Fmt == 1u ? 0.0036 : 0.0045) * Specification.Extinction;
    }

    if (Index != 0u) Layer.Amount = 0.0;
    return Layer;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PLANETARY WEATHER ENVELOPE
//------------------------------------------------------------------------------------------------------------------------

CloudWeatherSample CloudWeatherFromAdvectedNormal(CloudSpecification Specification, float3 AdvectedNormal)
{
    float3 Coordinate = RotateCloudOctave(AdvectedNormal * 13.0) + Specification.Seed * float3(0.73, -0.46, 1.17);
    float  Broad      = CloudLatticeNoise(Coordinate + float3(12.8, -7.9, 42.1));
    float  Mesoscale =
        CloudLatticeNoise(RotateCloudOctave(AdvectedNormal * 470.0) + float3(17.4, Specification.Seed, 36.1));
    CloudWeatherSample Weather;
    Weather.Moisture = saturate(0.48 + 0.57 * (Broad - 0.5) + 0.43 * (Mesoscale - 0.5));
    Weather.Type     = Mesoscale;
    Weather.Lift     = saturate(0.42 + 0.68 * (Mesoscale - 0.5));
    Weather.Rain     = smoothstep(0.50, 0.80, Weather.Moisture);
    Weather.Bands    = Broad;
    return Weather;
}

CloudWeatherSample CloudWeatherAtPoint(CloudSpecification Specification, float3 Position, uint Category)
{
    float  Radius         = length(Position);
    float3 AdvectedNormal = CloudWindCoordinates(Specification, Position, Category) / Radius;
    return CloudWeatherFromAdvectedNormal(Specification, AdvectedNormal);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONVECTIVE CONDENSATE
//------------------------------------------------------------------------------------------------------------------------

float CloudCellEnvelope(float2 PositionM, float Height, float Coverage, float Seed, float ScaleM, float Expansion = 1.0)
{
    float2 Coordinate = PositionM / ScaleM;
    int2   Cell       = int2(floor(Coordinate));
    float  Envelope   = -1.0;
    for (int Y = -1; Y <= 1; ++Y)
        for (int X = -1; X <= 1; ++X)
        {
            int2   Address    = Cell + int2(X, Y);
            float4 Random     = DecodeCloudTexel(CloudLatticeHash(int3(Address, int(floor(Seed * 53.0)))));
            float2 Center     = float2(Address) + 0.5 + 0.76 * (Random.xy - 0.5);
            float  Occurrence = smoothstep(0.18, 0.65, Coverage + 0.30 * (Random.z - 0.5));
            float  Radius     = (0.19 + 0.27 * Random.w) * lerp(0.65, 1.15, Occurrence);
            if (Expansion > 1.001 && Random.z < 0.58) continue;
            Radius *= Expansion;
            float  TowerHeight = 0.30 + 0.58 * Random.z;
            float2 Tilt        = (Random.yw - 0.5) * (0.38 * Height * Height);
            float2 Offset      = (Coordinate - Center - Tilt) / Radius;
            if (dot(Offset, Offset) > 3.2) continue;
            float  Vertical  = (Height - 0.36 * TowerHeight) / max(0.1, TowerHeight * 0.64);
            float  Main      = 1.0 - sqrt(dot(Offset, Offset) + Vertical * Vertical);
            float2 BudOffset = Offset - (Random.yz - 0.5) * 0.95;
            float  BudHeight = (Height - TowerHeight * 0.70) / max(0.08, TowerHeight * 0.35);
            float  Bud       = 0.90 - sqrt(dot(BudOffset, BudOffset) * 1.65 + BudHeight * BudHeight);
            Envelope         = max(Envelope, max(Main, Bud) - (1.0 - Occurrence) * 0.75);
        }
    return Envelope;
}

float CloudConvectiveEnvelope(
    CloudSpecification Specification,
    float3             Advected,
    float              AltitudeM,
    float              Height,
    float              Amount,
    float              ScaleM,
    float              Expansion = 1.0)
{
    float3 Normal    = normalize(Advected);
    float3 SeaLevel  = Advected - Normal * AltitudeM;
    float3 Weight    = smoothstep(float3(0.40), float3(0.82), abs(Normal));
    Weight          *= Weight;
    float  Envelope  = 0.0;
    float  WeightSum = max(0.001, Weight.x + Weight.y + Weight.z);
    for (uint Axis = 0u; Axis < 3u; ++Axis)
    {
        float W = (Axis == 0u) ? Weight.z : ((Axis == 1u) ? Weight.x : Weight.y);
        if (W <= 0.001) continue;
        float2 PlanePos = (Axis == 0u) ? SeaLevel.xy : ((Axis == 1u) ? SeaLevel.yz : SeaLevel.xz);
        float  AxisSeed = Specification.Seed + ((Axis == 0u) ? 0.0 : ((Axis == 1u) ? 23.0 : 59.0));
        Envelope += W * CloudCellEnvelope(PlanePos, Height, Amount, AxisSeed, ScaleM, Expansion);
    }
    return Envelope / WeightSum;
}

float3 EvaluateCloudShapeDisplacement(float3 Coordinate)
{
    return float3(CloudLatticeNoise(Coordinate + float3(17.3, 43.7, 61.1)),
                  CloudLatticeNoise(RotateCloudOctave(Coordinate) + float3(53.1, 11.9, 37.7)),
                  CloudLatticeNoise(RotateCloudOctave(Coordinate * 1.19) + float3(29.3, 71.1, 13.7))) - 0.5;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RESOLVED CYCLONIC WEATHER
//------------------------------------------------------------------------------------------------------------------------

float CloudCycloneEnvelope(
    CloudSpecification Specification,
    float3             Advected,
    float              AltitudeM,
    float3             Coordinate,
    float              FootprintM,
    out float          HeightEnvelope)
{
    float3 Normal        = normalize(Advected);
    float  RadiusM       = CloudGeodesicRadius(Specification, Normal);
    float3 ReferenceAxis = abs(Specification.CycloneAxis.y) > 0.90 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0);
    float3 TangentX      = normalize(cross(ReferenceAxis, Specification.CycloneAxis));
    float3 TangentY      = cross(Specification.CycloneAxis, TangentX);
    float  Azimuth       = atan2(dot(Normal, TangentY), dot(Normal, TangentX));
    float  Warp          = CloudLatticeNoise(Coordinate * 0.000013 + 31.7) - 0.5;
    float  SpiralAngle =
        2.0 * Azimuth - 5.8 * log(max(0.8, RadiusM / max(9000.0, Specification.CycloneEyeM))) + 1.8 * Warp;
    float Spiral    = pow(saturate(0.5 + 0.5 * cos(SpiralAngle)), 2.7);
    float Broken    = CloudLatticeNoise(Coordinate * 0.000026 + 53.2);
    float Central   = 1.0 - smoothstep(100000.0, 240000.0, RadiusM);
    float Outer     = 1.0 - smoothstep(210000.0, 570000.0, RadiusM);
    float Rainbands = smoothstep(0.08, 0.70, Spiral) * (0.57 + 0.43 * Broken) * Outer;
    float Eyewall   = exp(-pow((RadiusM - Specification.CycloneEyeM * 1.65) / (Specification.CycloneEyeM * 0.65), 2.0));
    float EyeRadiusM = Specification.CycloneEyeM + max(0.0, AltitudeM - 1500.0) * 0.58 +
                       1500.0 * sin(3.0 * Azimuth + 0.4) + 1100.0 * sin(Azimuth - 0.7);
    float  Eye              = smoothstep(EyeRadiusM, EyeRadiusM + 4300.0 + 1600.0 * Warp, RadiusM);
    float3 BillowCoordinate = Coordinate + 18000.0 * Warp;
    float  BillowFootprintM = FootprintM * 1.6;
    float2 BillowPair       = SampleCloudShape(BillowCoordinate / 140000.0 + 0.713, BillowFootprintM, 140000.0).rg;
    float  MacroBillow      = BillowPair.x;
    float  MesoBillow       = BillowPair.y;
    float  SmallBillow      = Broken;
    float ConvectiveTopM =
        12100.0 + 5800.0 * (MacroBillow - 0.47) + 2400.0 * (MesoBillow - 0.47) + 500.0 * (SmallBillow - 0.47);
    float TopM        = lerp(2900.0 + 2900.0 * Rainbands + 1500.0 * MesoBillow, ConvectiveTopM, Central);
    TopM              = max(TopM, 12500.0 * Eyewall);
    float WaterHeight = smoothstep(900.0, 1350.0, AltitudeM) * (1.0 - smoothstep(TopM - 1250.0, TopM, AltitudeM));
    float Outflow     = smoothstep(100000.0, 185000.0, RadiusM) * (1.0 - smoothstep(215000.0, 440000.0, RadiusM));
    float OutflowTopM =
        11900.0 + 2400.0 * (MacroBillow - 0.47) + 2000.0 * (MesoBillow - 0.47) + 350.0 * (SmallBillow - 0.47);
    float IceHeight =
        smoothstep(10000.0, 10800.0, AltitudeM) * (1.0 - smoothstep(OutflowTopM - 360.0, OutflowTopM, AltitudeM));
    float IceFibers   = saturate(0.51 + 0.49 * Spiral + 0.40 * (MacroBillow - 0.47) + 0.12 * (MesoBillow - 0.47));
    HeightEnvelope    = max(WaterHeight, IceHeight * Outflow) * Eye;
    float CentralMass = Central * (0.72 + 0.32 * MacroBillow + 0.16 * MesoBillow);
    float Mass        = max(CentralMass, 0.98 * Rainbands) + 0.20 * Eyewall;
    Mass              = max(Mass * WaterHeight, Outflow * IceHeight * IceFibers * 0.72);
    return Mass * Eye;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               ONE DENSITY FROM GROUND TO SPACE
//------------------------------------------------------------------------------------------------------------------------

float SamplePlanetCloudDensityAdvected(
    CloudSpecification Specification,
    CloudDeck          Layer,
    float3             Position,
    float3             Advected,
    CloudWeatherSample Weather,
    float              FootprintM,
    bool               LightQuery)
{
    float Radius    = length(Position);
    float AltitudeM = Radius - Specification.RadiusM;
    float Height    = (AltitudeM - Layer.BottomM) / (Layer.TopM - Layer.BottomM);
    if (Height <= 0.0 || Height >= 1.0 || Layer.Amount <= 0.0) return 0.0;
    if (Specification.UniformExtinction >= 0.0) return Specification.UniformExtinction;

    float3 Coordinate      = RotateCloudOctave(Advected - float3(0.0, 0.0, Specification.RadiusM));
    Coordinate += Specification.Seed * float3(731.0, -1261.0, 3179.0);
    float  ScaleM          = max(900.0, 7200.0 * max(0.15, Specification.FeatureScale));
    float3 ShapeCoordinate = Coordinate / ScaleM + float3(0.143, 0.713, 0.337);
    uint   Fmt             = Specification.Formation;

    float Amount = saturate(Layer.Amount + 0.38 * (Weather.Moisture - 0.5));

    // Early clear-sky reject for classic convective clouds BEFORE expensive 3D texture sampling
    float ConvEnvelope = 0.0;
    bool  IsConvective = (Fmt == 1u || Fmt == 2u || Fmt == 3u || Fmt == 7u || Fmt == 11u);
    if (IsConvective)
    {
        float CellScaleM = ScaleM * ((Fmt == 1u || Fmt == 11u) ? 0.5525 : 0.85);
        ConvEnvelope = CloudConvectiveEnvelope(Specification, Advected, AltitudeM, Height, Amount, CellScaleM);
        if (ConvEnvelope < -0.92 && Fmt != 3u && Fmt != 11u) return 0.0;
    }

    float2 Shape           = float2(0.47, 0.47);
    float  Detail          = 0.48;
    float  ShapeFootprintM = FootprintM;
    if (!LightQuery && (Fmt == 8u || Fmt == 12u))
    {
        float WarpS = Fmt == 8u ? 2.2 : 3.7;
        float WarpA = Fmt == 8u ? 1.4 : 1.15;
        ShapeCoordinate += WarpA * (CloudLatticeNoise(Coordinate / (ScaleM * WarpS)) - 0.5);
        ShapeFootprintM *= 1.3;
    }

    if (Layer.Category != 2u || Fmt == 10u)
    {
        Shape = SampleCloudShape(ShapeCoordinate, ShapeFootprintM, ScaleM).rg;
        if (!LightQuery)
        {
            float FineFilter = 1.0 - smoothstep(ScaleM * 0.015, ScaleM * 0.11, FootprintM);
            if (FineFilter > 0.01)
            {
                float Billow =
                    SampleCloudShape(RotateCloudOctave(ShapeCoordinate) * 3.17 + 0.337, ShapeFootprintM, ScaleM / 3.17).g;
                Detail = lerp(0.48, Billow, FineFilter);
            }
        }
    }

    float Raw            = 0.0;
    float HeightEnvelope = smoothstep(0.0, 0.045, Height) * (1.0 - smoothstep(0.93, 1.0, Height));

    if (Fmt == 12u) // OrbitalCyclone + synoptic planetary cloud belts
    {
        float CycloneMass = CloudCycloneEnvelope(Specification, Advected, AltitudeM, Coordinate, FootprintM, HeightEnvelope);
        float SynopticEnv = smoothstep(0.02, 0.16, Height) * (1.0 - smoothstep(0.62, 0.96, Height));
        float SynopticRaw = 0.85 * (Shape.x - 0.44) + 0.30 * (Detail - 0.48) - (1.0 - Amount) * 0.22;
        if (CycloneMass <= 0.001)
        {
            HeightEnvelope = max(HeightEnvelope, SynopticEnv);
            Raw = SynopticRaw * SynopticEnv - (1.0 - SynopticEnv) * 0.12;
        }
        else
        {
            float Blended = max(CycloneMass, saturate(SynopticRaw) * 0.65 * SynopticEnv);
            HeightEnvelope = max(HeightEnvelope, SynopticEnv);
            Raw = Blended - (0.28 + 0.24 * (1.0 - Amount)) + 0.20 * (Shape.x - 0.5) + 0.06 * (Detail - 0.5);
        }
    }
    else if (Fmt == 5u || Fmt == 10u) // Cirrus(5), Nacreous(10)
    {
        float3 Fiber =
            RotateCloudOctave(Coordinate) * float3(0.000075, 0.0028, 0.00065) / max(0.3, Specification.FeatureScale);
        float Bend = CloudLatticeNoise(Coordinate * 0.00014 + 41.3);
        float Bank = CloudLatticeNoise(Coordinate * float3(0.000045, 0.000070, 0.00013) + 59.1);
        Fiber.y += 4.8 * Bend + 2.7 * Height * Height;
        Fiber.x += 0.60 * Height * Height;
        float Strand;
        if (LightQuery)
        {
            Strand = 0.65 * CloudLatticeNoise(Fiber + 3.7) + 0.35 * CloudLatticeNoise(Fiber * 2.13 + 16.9);
        }
        else
        {
            Fiber.z += 1.3 * CloudLatticeNoise(Coordinate * 0.00023 - 23.0);
            float Filter = FootprintM * 0.0028 / max(0.3, Specification.FeatureScale);
            Strand =
                0.64 * lerp(0.5, CloudLatticeNoise(Fiber + 3.7), 1.0 - smoothstep(0.6, 1.5, Filter)) +
                0.36 * lerp(0.5, CloudLatticeNoise(Fiber * 2.13 + 16.9), 1.0 - smoothstep(0.6, 1.5, Filter * 2.13));
        }
        float BankEnvelope = smoothstep(0.30, 0.67, Bank + (Amount - 0.6) * 0.35);
        if (Fmt == 10u)
        {
            float SilkRibbon = pow(0.5 + 0.5 * sin(Fiber.y * 0.45 + 3.0 * Bend), 0.78);
            Strand       = 0.55 * SilkRibbon + 0.45 * Shape.x;
            BankEnvelope = lerp(0.45, 1.0, BankEnvelope);
        }
        HeightEnvelope *= smoothstep(0.0, 0.23, Height) * (1.0 - smoothstep(0.55, 1.0, Height)) * BankEnvelope;
        Raw = Strand - lerp(0.70, 0.43, Amount) - (1.0 - HeightEnvelope) * 0.16;
    }
    else if (Fmt == 6u)
    {
        float MacroLobes = CloudLatticeNoise(Coordinate * float3(0.00011, 0.00024, 0.0) / max(0.25, Specification.FeatureScale) + 1.7);
        float Laminar    = pow(0.5 + 0.5 * sin(Height * CloudPi * 2.4 - 0.3 + (Shape.x - 0.5) * 0.85), 1.15);
        float LensMask   = smoothstep(0.42, 0.72, MacroLobes + (Shape.x - 0.5) * 0.24);
        HeightEnvelope *= smoothstep(0.04, 0.24, Height) * (1.0 - smoothstep(0.74, 0.96, Height));
        Raw = LensMask * lerp(0.72, Laminar, 0.58) - (1.0 - Amount) * 0.45 - (1.0 - HeightEnvelope) * 0.28;
    }
    else if (Fmt == 8u)
    {
        float TiltOffset = 320.0 * sin(Height * CloudPi) + 220.0 * Height * Height;
        float WavePhase  = (Coordinate.x + TiltOffset) * (0.0020 / max(0.25, Specification.FeatureScale)) +
                           (Shape.x - 0.5) * 2.8;
        float BreakCurl  = smoothstep(0.26, 0.88, 0.5 + 0.5 * sin(WavePhase + Height * 3.0));
        float MacroBand  = smoothstep(0.36, 0.72, CloudLatticeNoise(Coordinate * 0.00012 + 3.1));
        HeightEnvelope *= smoothstep(0.08, 0.30, Height) * (1.0 - smoothstep(0.72, 0.98, Height));
        Raw = MacroBand * (0.58 * BreakCurl + 0.42 * Shape.x) - lerp(0.56, 0.24, Amount) - (1.0 - HeightEnvelope) * 0.26;
    }
    else if (Fmt == 9u)
    {
        float WaveFilter = LightQuery ? 0.0 : (1.0 - smoothstep(320.0, 2200.0, FootprintM));
        float K          = 0.0018 / max(0.25, Specification.FeatureScale);
        float Swell      = 0.5;
        if (WaveFilter > 0.01)
        {
            Swell += 0.125 * WaveFilter * (
                sin((Coordinate.x * 0.966 + Coordinate.y * 0.259) * K + (Shape.x - 0.5) * 3.2) +
                cos((Coordinate.x * 0.391 + Coordinate.y * 0.920) * K * 1.08 - (Shape.y - 0.5) * 2.8) +
                sin((-Coordinate.x * 0.391 + Coordinate.y * 0.920) * K * 1.15 + (Detail - 0.5) * 2.4) +
                cos((-Coordinate.x * 0.927 + Coordinate.y * 0.375) * K * 0.96 - (Shape.x - 0.5) * 3.0));
        }
        HeightEnvelope *= smoothstep(0.05, 0.22, Height) * (1.0 - smoothstep(0.72, 0.96, Height));
        Raw = 0.62 * Shape.x + 0.38 * Swell - lerp(0.62, 0.26, Amount) - (1.0 - HeightEnvelope) * 0.28;
    }
    else if (Layer.Category == 1u)
    {
        float Fold     = CloudLatticeNoise(Coordinate * 0.00016 + 37.1);
        float Vertical = Height + (Fold - 0.5) * 0.25;
        HeightEnvelope *= smoothstep(0.0, 0.19, Vertical) * (1.0 - smoothstep(0.66, 1.0, Vertical));
        Raw = 0.72 * Shape.x + 0.28 * Shape.y - lerp(0.64, 0.25, Amount) - (1.0 - HeightEnvelope) * 0.36;
    }
    else
    {
        float Envelope = ConvEnvelope;
        if (Fmt == 3u)
        {
            float AnvilDisc = smoothstep(0.68, 0.88, Height) * (1.0 - smoothstep(0.92, 1.0, Height));
            Envelope = max(Envelope, AnvilDisc * (0.45 * Specification.Anvil + 0.40 * (Shape.x - 0.35)));
        }
        else if (Fmt == 7u && !LightQuery)
        {
            float PouchFilter = 1.0 - smoothstep(260.0, 1800.0, FootprintM);
            float K           = 0.0017 / max(0.25, Specification.FeatureScale);
            float PX          = Coordinate.x + (Shape.x - 0.5) * 420.0;
            float PY          = Coordinate.y - (Shape.y - 0.5) * 420.0;
            float Pouch       = pow(saturate((cos(PX * K) + cos((-0.5 * PX + 0.866025 * PY) * K) +
                                              cos((-0.5 * PX - 0.866025 * PY) * K) + 1.5) / 4.5), 1.15) * PouchFilter;
            float LowerMask   = 1.0 - smoothstep(0.18, 0.62, Height);
            Envelope += Pouch * LowerMask * 0.42;
        }
        else if (Fmt == 11u)
        {
            float TowerCap = smoothstep(0.70, 0.96, Height) * Specification.Anvil;
            Envelope = max(Envelope * 1.12, TowerCap * (0.42 + 0.45 * (Shape.x - 0.40)));
        }

        Raw = 0.90 * Envelope + 1.05 * (Shape.x - 0.45) + 0.37 * (Shape.y - 0.45) + 0.32 * (Detail - 0.5);
        Raw -= 0.06 + (1.0 - Amount) * 0.10;
    }

    bool CoherentBoundary = (Fmt == 3u || Fmt == 11u || Fmt == 12u);
    if (Raw <= (CoherentBoundary ? 0.0 : -0.08) || HeightEnvelope <= 0.0) return 0.0;

    if (!LightQuery && Layer.Category != 2u)
    {
        float ErosionFilter = 1.0 - smoothstep(24.0, 180.0, FootprintM);
        if (Raw < 0.45 && ErosionFilter > 0.0)
        {
            float3 ErosionCoordinate = Coordinate;
            ErosionCoordinate.xy += float2(Shape.y - 0.47, 0.47 - Shape.x) * (180.0 * Specification.Turbulence);
            float4 Erosion =
                SampleCloudErosion(ErosionCoordinate / 1550.0 + float3(0.419, 0.673, 0.613), FootprintM, 1550.0);
            float ErosionSignal = dot(Erosion - float4(0.47), float4(0.46, 0.29, 0.17, 0.08));
            float BoundaryShift = ErosionSignal * 0.64 * ErosionFilter * (1.0 - smoothstep(0.20, 0.45, Raw));
            if (CoherentBoundary) BoundaryShift = clamp(BoundaryShift, -0.35 * Raw, 0.35 * Raw);
            Raw += BoundaryShift;
        }
    }

    float Response   = Layer.Category == 2u ? 0.14 : 0.048;
    float Condensate = smoothstep(0.0, Response, Raw) * HeightEnvelope;
    return Layer.Sigma * Condensate;
}

float SamplePlanetCloudShadowDensity(
    CloudSpecification Specification,
    CloudDeck          Layer,
    float3             Position,
    float3             Advected,
    CloudWeatherSample Weather,
    float              FootprintM)
{
    float Radius    = length(Position);
    float AltitudeM = Radius - Specification.RadiusM;
    float Height    = (AltitudeM - Layer.BottomM) / (Layer.TopM - Layer.BottomM);
    if (Height <= 0.0 || Height >= 1.0 || Layer.Amount <= 0.0) return 0.0;
    if (Specification.UniformExtinction >= 0.0) return Specification.UniformExtinction;

    float3 Coordinate = RotateCloudOctave(Advected - float3(0.0, 0.0, Specification.RadiusM)) +
                        Specification.Seed * float3(731.0, -1261.0, 3179.0);
    float  ScaleM     = max(900.0, 7200.0 * max(0.15, Specification.FeatureScale));
    float  Amount     = saturate(Layer.Amount + 0.38 * (Weather.Moisture - 0.5));
    float  HeightEnv  = smoothstep(0.0, 0.12, Height) * (1.0 - smoothstep(0.75, 1.0, Height));
    uint   Fmt        = Specification.Formation;

    float2 Shape = SampleCloudShape(Coordinate / ScaleM + float3(0.143, 0.713, 0.337), FootprintM, ScaleM).rg;
    float  Raw   = 0.78 * (Shape.x - 0.45) + 0.28 * (Shape.y - 0.45) - (1.0 - Amount) * 0.42 - (1.0 - HeightEnv) * 0.25;
    if (Fmt == 12u)
    {
        float CycR = CloudGeodesicRadius(Specification, normalize(Advected));
        float Cyc  = (1.0 - smoothstep(100000.0, 260000.0, CycR)) *
                     smoothstep(Specification.CycloneEyeM, Specification.CycloneEyeM + 6000.0, CycR);
        Raw = max(Raw, Cyc - (0.28 + 0.24 * (1.0 - Amount)));
    }
    if (Raw <= 0.0 || HeightEnv <= 0.0) return 0.0;
    float Response = (Layer.Category == 2u) ? 0.14 : 0.06;
    return Layer.Sigma * smoothstep(0.0, Response, Raw) * HeightEnv;
}

float SamplePlanetCloudDensity(
    CloudSpecification Specification,
    CloudDeck          Layer,
    float3             Position,
    CloudWeatherSample Weather,
    float              FootprintM,
    bool               LightQuery)
{
    float3 Advected = CloudWindCoordinates(Specification, Position, Layer.Category);
    if (LightQuery) return SamplePlanetCloudShadowDensity(Specification, Layer, Position, Advected, Weather, FootprintM);
    return SamplePlanetCloudDensityAdvected(Specification, Layer, Position, Advected, Weather, FootprintM, false);
}

#endif
