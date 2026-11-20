/*============================================================================================================================================
                                                 EMISSIVEGICANDIDATESEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Deterministic SH9 integration, surfel-cascade diffuse GI, and shared cosine path-trace reference

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/EmissiveGiCandidateSequence.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"
#include "Engine/MaterialSpace/Scattering/BlackbodyEmission.h"
#include "Engine/Mathematics/DeterministicSampling.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <vector>

namespace Frontier::GeometricRaster
{

namespace
{

struct TraceSurfaceReading
{
    bool                   Hit              = false;
    uint16_t               MaterialSlot     = 0u;
    Mathematics::Axis3D    PositionWorld    {};
    Mathematics::Axis3D    NormalWorld      { 0.0f, 0.0f, 1.0f };
    Mathematics::LinearRGB DiffuseAlbedo    {};
    Mathematics::LinearRGB SelfRadiance     {};
};

struct SurfelRadianceSample
{
    Mathematics::Axis3D    PositionWorld      {};
    Mathematics::Axis3D    NormalWorld        { 0.0f, 0.0f, 1.0f };
    Mathematics::LinearRGB OutgoingRadiance   {};
    float                  AreaSquareMeters   = 0.0f;
    float                  RadiusMeters       = 0.0f;
};

struct SurfelAccumulator
{
    Mathematics::Axis3D    PositionAreaSum    {};
    Mathematics::Axis3D    NormalAreaSum      {};
    Mathematics::LinearRGB RadianceAreaSum    {};
    float                  AreaSquareMeters   = 0.0f;
};

using SurfelCellKey = std::tuple<int32_t, int32_t, int32_t, uint8_t>;
using SurfelCellMap = std::map<SurfelCellKey, SurfelAccumulator>;
using SurfelCascade = std::array<std::vector<SurfelRadianceSample>, 3u>;

[[nodiscard]] Mathematics::LinearRGB MultiplyRgb(
    Mathematics::LinearRGB Left,
    Mathematics::LinearRGB Right) noexcept
{
    return { Left.R * Right.R, Left.G * Right.G, Left.B * Right.B };
}

[[nodiscard]] Mathematics::LinearRGB ClampNonNegativeRgb(Mathematics::LinearRGB Color) noexcept
{
    return {
        std::max(0.0f, Color.R),
        std::max(0.0f, Color.G),
        std::max(0.0f, Color.B),
    };
}

[[nodiscard]] float RgbEnergy(Mathematics::LinearRGB Color) noexcept
{
    return std::fabs(Color.R) + std::fabs(Color.G) + std::fabs(Color.B);
}

[[nodiscard]] Mathematics::LinearRGB EvaluateDiffuseAlbedo(
    const MaterialSpace::SurfaceMaterial& Material) noexcept
{
    const float DiffuseFraction = std::clamp(Material.Diffuse.BaseWeight, 0.0f, 1.0f) *
                                  (1.0f - std::clamp(Material.Specular.Metalness, 0.0f, 1.0f)) *
                                  (1.0f - std::clamp(Material.Transmission.TransmissionWeight, 0.0f, 1.0f));
    return Material.Diffuse.BaseColor * DiffuseFraction;
}

[[nodiscard]] Mathematics::LinearRGB EvaluateSkyRadiance(
    Mathematics::LinearRGB SkyRadiance,
    Mathematics::Axis3D     DirectionWorld) noexcept
{
    const float UpWeight = std::clamp(DirectionWorld.Z * 0.5f + 0.5f, 0.0f, 1.0f);
    return SkyRadiance * (0.65f + 0.70f * UpWeight);
}

[[nodiscard]] TraceSurfaceReading TraceSurface(
    const EmissiveGiSceneView& Scene,
    Mathematics::Axis3D       RayOrigin,
    Mathematics::Axis3D       RayDirection,
    float                     MaximumDistanceMeters,
    EmissiveGiMetrics&        InOutMetrics) noexcept
{
    ++InOutMetrics.RayQueryCount;
    SpatialAcceleration::RaySegmentQuery Ray {};
    Ray.Origin          = RayOrigin;
    Ray.UnitDirection   = Mathematics::NormalizeAxis(RayDirection, { 0.0f, 0.0f, 1.0f });
    Ray.MinimumDistance = 1.0e-3f;
    Ray.MaximumDistance = std::max(Ray.MinimumDistance, MaximumDistanceMeters);

    const auto Intersection = Scene.SceneTree.TraceClosestRay(Ray, false);
    if (!Intersection.HitConfirmed ||
        Intersection.MaterialOrdinal >= Scene.MaterialRegister.QuerySlotCount())
    {
        return {};
    }

    const auto& Material = Scene.MaterialRegister.InspectAuthoredMaterial(Intersection.MaterialOrdinal);
    TraceSurfaceReading Reading {};
    Reading.Hit              = true;
    Reading.MaterialSlot     = Intersection.MaterialOrdinal;
    Reading.PositionWorld    = RayOrigin + Ray.UnitDirection * Intersection.HitDistanceMeters;
    Reading.NormalWorld      = Mathematics::NormalizeAxis(Intersection.GeometricNormal, { 0.0f, 0.0f, 1.0f });
    if (Mathematics::EvaluateDot(Reading.NormalWorld, Ray.UnitDirection) > 0.0f)
    {
        Reading.NormalWorld = -Reading.NormalWorld;
    }
    Reading.DiffuseAlbedo = EvaluateDiffuseAlbedo(Material);
    Reading.SelfRadiance  = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Material.PorosityLight);
    return Reading;
}

[[nodiscard]] bool QueryVisibility(
    const EmissiveGiSceneView& Scene,
    Mathematics::Axis3D       OriginWorld,
    Mathematics::Axis3D       DirectionWorld,
    float                     MaximumDistanceMeters,
    EmissiveGiMetrics&        InOutMetrics) noexcept
{
    const float ClippedDistance = MaximumDistanceMeters - 2.0e-3f;
    if (ClippedDistance <= 1.5e-3f)
    {
        return true;
    }

    ++InOutMetrics.RayQueryCount;
    ++InOutMetrics.VisibilityRayCount;
    SpatialAcceleration::RaySegmentQuery Ray {};
    Ray.Origin          = OriginWorld;
    Ray.UnitDirection   = Mathematics::NormalizeAxis(DirectionWorld, { 0.0f, 0.0f, 1.0f });
    Ray.MinimumDistance = 1.0e-3f;
    Ray.MaximumDistance = ClippedDistance;
    return !Scene.SceneTree.QueryRayOccluded(Ray);
}

[[nodiscard]] Mathematics::LinearRGB EvaluateDirectDiffuseRadiance(
    const EmissiveGiSceneView& Scene,
    const TraceSurfaceReading& Surface,
    uint32_t                   PixelSeed,
    uint32_t                   SampleOrdinal,
    uint32_t                   BounceOrdinal,
    float                      MaximumTraceDistanceMeters,
    EmissiveGiMetrics&         InOutMetrics) noexcept
{
    if (!Surface.Hit || RgbEnergy(Surface.DiffuseAlbedo) <= 1.0e-6f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    constexpr float InvPi = 0.3183098861837907f;
    Mathematics::LinearRGB DirectRadiance {};
    const Mathematics::Axis3D BiasedOrigin = Surface.PositionWorld + Surface.NormalWorld * 2.0e-3f;
    const Mathematics::Axis3D SunDirection = Mathematics::NormalizeAxis(
        Scene.Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    const float SunCosine = std::max(0.0f, Mathematics::EvaluateDot(Surface.NormalWorld, SunDirection));
    if (SunCosine > 1.0e-5f &&
        QueryVisibility(Scene, BiasedOrigin, SunDirection, MaximumTraceDistanceMeters, InOutMetrics))
    {
        const Mathematics::LinearRGB IncidentSun = Scene.Sun.SolarIrradiance * (SunCosine * InvPi);
        DirectRadiance = DirectRadiance + MultiplyRgb(Surface.DiffuseAlbedo, IncidentSun);
    }

    constexpr uint32_t AreaLightSampleCount = 4u;
    for (uint32_t LightOrdinal = 0u; LightOrdinal < Scene.AreaLights.size(); ++LightOrdinal)
    {
        const auto& Light = Scene.AreaLights[LightOrdinal];
        const Mathematics::Axis3D LightNormal = Mathematics::NormalizeAxis(
            Mathematics::EvaluateCross(Light.RightTangent, Light.UpBitangent), { 0.0f, 0.0f, -1.0f });
        const float TangentAreaScale = Mathematics::EvaluateLength(
            Mathematics::EvaluateCross(Light.RightTangent, Light.UpBitangent));
        const float LightAreaSquareMeters = 4.0f * Light.HalfWidthMeters * Light.HalfHeightMeters * TangentAreaScale;
        if (LightAreaSquareMeters <= 1.0e-7f)
        {
            continue;
        }

        Mathematics::LinearRGB LightRadianceSum {};
        for (uint32_t Tap = 0u; Tap < AreaLightSampleCount; ++Tap)
        {
            const uint32_t LightSampleOrdinal = SampleOrdinal * AreaLightSampleCount + Tap + LightOrdinal * 17u;
            const auto LightSample = Mathematics::EvaluateDeterministicR2Sample(
                PixelSeed, LightSampleOrdinal, 4u + BounceOrdinal);
            const Mathematics::Axis3D LightPoint = Light.CenterWorld +
                Light.RightTangent * ((2.0f * LightSample.X - 1.0f) * Light.HalfWidthMeters) +
                Light.UpBitangent * ((2.0f * LightSample.Y - 1.0f) * Light.HalfHeightMeters);
            const Mathematics::Axis3D ToLight = LightPoint - Surface.PositionWorld;
            const float DistanceSquared = Mathematics::EvaluateLengthSquared(ToLight);
            const float DistanceMeters = std::sqrt(std::max(1.0e-8f, DistanceSquared));
            if (DistanceMeters >= MaximumTraceDistanceMeters)
            {
                continue;
            }

            const Mathematics::Axis3D LightDirection = ToLight * (1.0f / DistanceMeters);
            const float SurfaceCosine = std::max(0.0f, Mathematics::EvaluateDot(Surface.NormalWorld, LightDirection));
            const float LightCosineRaw = Mathematics::EvaluateDot(LightNormal, -LightDirection);
            const float LightCosine = Light.TwoSidedEmission ? std::fabs(LightCosineRaw) : std::max(0.0f, LightCosineRaw);
            if (SurfaceCosine <= 1.0e-5f || LightCosine <= 1.0e-5f ||
                !QueryVisibility(Scene, BiasedOrigin, LightDirection, DistanceMeters, InOutMetrics))
            {
                continue;
            }

            const float GeometryScale = SurfaceCosine * LightCosine * LightAreaSquareMeters * InvPi /
                                        (DistanceSquared * static_cast<float>(AreaLightSampleCount));
            LightRadianceSum = LightRadianceSum + Light.EmittedRadiance * GeometryScale;
        }
        DirectRadiance = DirectRadiance + MultiplyRgb(Surface.DiffuseAlbedo, LightRadianceSum);
    }

    for (const auto& Light : Scene.PointLights)
    {
        const Mathematics::Axis3D ToLight = Light.PositionWorld - Surface.PositionWorld;
        const float DistanceSquared = Mathematics::EvaluateLengthSquared(ToLight);
        const float DistanceMeters = std::sqrt(std::max(1.0e-8f, DistanceSquared));
        if (DistanceMeters >= Light.RangeMeters || DistanceMeters >= MaximumTraceDistanceMeters)
        {
            continue;
        }

        const Mathematics::Axis3D LightDirection = ToLight * (1.0f / DistanceMeters);
        const float SurfaceCosine = std::max(0.0f, Mathematics::EvaluateDot(Surface.NormalWorld, LightDirection));
        if (SurfaceCosine <= 1.0e-5f ||
            !QueryVisibility(Scene, BiasedOrigin, LightDirection, DistanceMeters, InOutMetrics))
        {
            continue;
        }

        const float RangeRatio = std::clamp(DistanceMeters / std::max(0.5f, Light.RangeMeters), 0.0f, 1.0f);
        const float RangeWindow = 1.0f - RangeRatio * RangeRatio;
        const float Attenuation = Light.Intensity * RangeWindow * RangeWindow /
                                  std::max(DistanceSquared, Light.RadiusMeters * Light.RadiusMeters);
        const Mathematics::LinearRGB IncidentPoint = Light.Color * (SurfaceCosine * InvPi * Attenuation);
        DirectRadiance = DirectRadiance + MultiplyRgb(Surface.DiffuseAlbedo, IncidentPoint);
    }

    return DirectRadiance;
}

void ConstructSampleBasis(
    Mathematics::Axis3D  UnitNormal,
    Mathematics::Axis3D& OutTangent,
    Mathematics::Axis3D& OutBitangent) noexcept
{
    GeometrySpace::ConstructDuffOrthonormalBasis(UnitNormal, OutTangent, OutBitangent);
}

[[nodiscard]] Mathematics::Axis3D EvaluateCosineHemisphereDirection(
    Mathematics::Axis3D                UnitNormal,
    Mathematics::DeterministicSample2D Sample) noexcept
{
    Mathematics::Axis3D Tangent {};
    Mathematics::Axis3D Bitangent {};
    ConstructSampleBasis(UnitNormal, Tangent, Bitangent);

    const float Radius = std::sqrt(std::clamp(Sample.X, 0.0f, 1.0f));
    const float Angle = Constants::TauFloat * Sample.Y;
    const float LocalX = Radius * std::cos(Angle);
    const float LocalY = Radius * std::sin(Angle);
    const float LocalZ = std::sqrt(std::max(0.0f, 1.0f - Sample.X));
    return Mathematics::NormalizeAxis(
        Tangent * LocalX + Bitangent * LocalY + UnitNormal * LocalZ,
        UnitNormal);
}

[[nodiscard]] Mathematics::Axis3D EvaluateUniformSphereDirection(
    Mathematics::Axis3D                UnitNormal,
    Mathematics::DeterministicSample2D Sample) noexcept
{
    Mathematics::Axis3D Tangent {};
    Mathematics::Axis3D Bitangent {};
    ConstructSampleBasis(UnitNormal, Tangent, Bitangent);

    const float LocalZ = 1.0f - 2.0f * Sample.X;
    const float Radius = std::sqrt(std::max(0.0f, 1.0f - LocalZ * LocalZ));
    const float Angle = Constants::TauFloat * Sample.Y;
    return Mathematics::NormalizeAxis(
        Tangent * (Radius * std::cos(Angle)) +
        Bitangent * (Radius * std::sin(Angle)) +
        UnitNormal * LocalZ,
        UnitNormal);
}

[[nodiscard]] std::array<float, 9u> EvaluateRealSh9Basis(Mathematics::Axis3D Direction) noexcept
{
    const float X = Direction.X;
    const float Y = Direction.Y;
    const float Z = Direction.Z;
    return {{
        0.2820947918f,
        0.4886025119f * Y,
        0.4886025119f * Z,
        0.4886025119f * X,
        1.0925484306f * X * Y,
        1.0925484306f * Y * Z,
        0.3153915653f * (3.0f * Z * Z - 1.0f),
        1.0925484306f * X * Z,
        0.5462742153f * (X * X - Y * Y),
    }};
}

[[nodiscard]] Mathematics::LinearRGB TraceIncidentRadiance(
    const EmissiveGiSceneView& Scene,
    Mathematics::Axis3D       RayOrigin,
    Mathematics::Axis3D       RayDirection,
    uint32_t                  PixelSeed,
    uint32_t                  SampleOrdinal,
    const EmissiveGiSettings& Settings,
    EmissiveGiMetrics&        InOutMetrics) noexcept
{
    const uint32_t BounceLimit = std::clamp(Settings.BounceLimit, 1u, 8u);
    Mathematics::LinearRGB Radiance {};
    Mathematics::LinearRGB Throughput { 1.0f, 1.0f, 1.0f };
    Mathematics::Axis3D PathOrigin = RayOrigin;
    Mathematics::Axis3D PathDirection = Mathematics::NormalizeAxis(RayDirection, { 0.0f, 0.0f, 1.0f });

    for (uint32_t BounceOrdinal = 0u; BounceOrdinal < BounceLimit; ++BounceOrdinal)
    {
        const TraceSurfaceReading Surface = TraceSurface(
            Scene, PathOrigin, PathDirection, Settings.MaximumTraceDistance, InOutMetrics);
        if (!Surface.Hit)
        {
            Radiance = Radiance + MultiplyRgb(Throughput, EvaluateSkyRadiance(Scene.SkyRadiance, PathDirection));
            break;
        }

        const Mathematics::LinearRGB DirectAndEmissive = Surface.SelfRadiance + EvaluateDirectDiffuseRadiance(
            Scene,
            Surface,
            PixelSeed,
            SampleOrdinal,
            BounceOrdinal,
            Settings.MaximumTraceDistance,
            InOutMetrics);
        Radiance = Radiance + MultiplyRgb(Throughput, DirectAndEmissive);

        if (BounceOrdinal + 1u >= BounceLimit || RgbEnergy(Surface.DiffuseAlbedo) <= 1.0e-6f)
        {
            break;
        }

        const auto BounceSample = Mathematics::EvaluateDeterministicR2Sample(
            PixelSeed, SampleOrdinal, 32u + BounceOrdinal);
        PathDirection = EvaluateCosineHemisphereDirection(Surface.NormalWorld, BounceSample);
        PathOrigin = Surface.PositionWorld + Surface.NormalWorld * 2.0e-3f;
        Throughput = MultiplyRgb(Throughput, Surface.DiffuseAlbedo);
    }

    return ClampNonNegativeRgb(Radiance);
}

[[nodiscard]] bool ResolvePrimaryAlbedo(
    const AnalyticResolvedSurfaceSample& PrimarySurface,
    const EmissiveGiSceneView&           Scene,
    Mathematics::LinearRGB&              OutAlbedo) noexcept
{
    if (!PrimarySurface.ValidHit || PrimarySurface.MaterialSlot >= Scene.MaterialRegister.QuerySlotCount())
    {
        return false;
    }

    OutAlbedo = EvaluateDiffuseAlbedo(Scene.MaterialRegister.InspectAuthoredMaterial(PrimarySurface.MaterialSlot));
    return RgbEnergy(OutAlbedo) > 1.0e-6f;
}

[[nodiscard]] Mathematics::LinearRGB EvaluatePathTraceIndirect(
    const EmissiveGiSceneView&           Scene,
    const AnalyticResolvedSurfaceSample& PrimarySurface,
    uint32_t                             PixelSeed,
    const EmissiveGiSettings&            Settings,
    EmissiveGiMetrics&                   InOutMetrics) noexcept
{
    Mathematics::LinearRGB Albedo {};
    if (!ResolvePrimaryAlbedo(PrimarySurface, Scene, Albedo))
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const uint32_t SampleCount = std::clamp(Settings.SamplesPerPixel, 1u, 256u);
    Mathematics::LinearRGB RadianceSum {};
    const Mathematics::Axis3D Normal = Mathematics::NormalizeAxis(
        PrimarySurface.TangentFrame.NormalZ, { 0.0f, 0.0f, 1.0f });
    for (uint32_t SampleOrdinal = 0u; SampleOrdinal < SampleCount; ++SampleOrdinal)
    {
        const auto Sample = Mathematics::EvaluateDeterministicR2Sample(PixelSeed, SampleOrdinal, 0u);
        const Mathematics::Axis3D Direction = EvaluateCosineHemisphereDirection(Normal, Sample);
        const Mathematics::LinearRGB Incident = TraceIncidentRadiance(
            Scene,
            PrimarySurface.WorldPosition + Normal * 2.0e-3f,
            Direction,
            PixelSeed,
            SampleOrdinal,
            Settings,
            InOutMetrics);
        RadianceSum = RadianceSum + MultiplyRgb(Albedo, Incident);
    }
    return RadianceSum * (1.0f / static_cast<float>(SampleCount));
}

[[nodiscard]] Mathematics::LinearRGB EvaluateStokesSh9Indirect(
    const EmissiveGiSceneView&           Scene,
    const AnalyticResolvedSurfaceSample& PrimarySurface,
    uint32_t                             PixelSeed,
    const EmissiveGiSettings&            Settings,
    EmissiveGiMetrics&                   InOutMetrics) noexcept
{
    Mathematics::LinearRGB Albedo {};
    if (!ResolvePrimaryAlbedo(PrimarySurface, Scene, Albedo))
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const uint32_t SampleCount = std::clamp(std::max(16u, Settings.SamplesPerPixel), 16u, 256u);
    const Mathematics::Axis3D Normal = Mathematics::NormalizeAxis(
        PrimarySurface.TangentFrame.NormalZ, { 0.0f, 0.0f, 1.0f });
    std::array<Mathematics::LinearRGB, 9u> ShCoefficients {};
    const float SolidAnglePerSample = 4.0f * Constants::PiFloat / static_cast<float>(SampleCount);

    for (uint32_t SampleOrdinal = 0u; SampleOrdinal < SampleCount; ++SampleOrdinal)
    {
        const auto Sample = Mathematics::EvaluateDeterministicR2Sample(PixelSeed, SampleOrdinal, 1u);
        const Mathematics::Axis3D Direction = EvaluateUniformSphereDirection(Normal, Sample);
        Mathematics::LinearRGB Incident {};
        if (Mathematics::EvaluateDot(Normal, Direction) > 0.0f)
        {
            Incident = TraceIncidentRadiance(
                Scene,
                PrimarySurface.WorldPosition + Normal * 2.0e-3f,
                Direction,
                PixelSeed,
                SampleOrdinal,
                Settings,
                InOutMetrics);
        }

        const auto Basis = EvaluateRealSh9Basis(Direction);
        for (uint32_t Coefficient = 0u; Coefficient < ShCoefficients.size(); ++Coefficient)
        {
            ShCoefficients[Coefficient] = ShCoefficients[Coefficient] +
                                          Incident * (Basis[Coefficient] * SolidAnglePerSample);
        }
    }

    constexpr std::array<float, 9u> CosineBandConvolution {{
        1.0f,
        2.0f / 3.0f,
        2.0f / 3.0f,
        2.0f / 3.0f,
        1.0f / 4.0f,
        1.0f / 4.0f,
        1.0f / 4.0f,
        1.0f / 4.0f,
        1.0f / 4.0f,
    }};
    const auto NormalBasis = EvaluateRealSh9Basis(Normal);
    Mathematics::LinearRGB Irradiance {};
    for (uint32_t Coefficient = 0u; Coefficient < ShCoefficients.size(); ++Coefficient)
    {
        const float ConvolutionScale = Constants::PiFloat *
            CosineBandConvolution[Coefficient] * NormalBasis[Coefficient];
        Irradiance = Irradiance + ShCoefficients[Coefficient] * ConvolutionScale;
    }
    return MultiplyRgb(Albedo, ClampNonNegativeRgb(Irradiance * (1.0f / Constants::PiFloat)));
}

[[nodiscard]] SurfelCellKey ConstructSurfelCellKey(
    Mathematics::Axis3D PositionWorld,
    Mathematics::Axis3D NormalWorld,
    float               CellWidthMeters) noexcept
{
    const uint8_t NormalOctant = static_cast<uint8_t>(
        (NormalWorld.X >= 0.0f ? 1u : 0u) |
        (NormalWorld.Y >= 0.0f ? 2u : 0u) |
        (NormalWorld.Z >= 0.0f ? 4u : 0u));
    return {
        static_cast<int32_t>(std::floor(PositionWorld.X / CellWidthMeters)),
        static_cast<int32_t>(std::floor(PositionWorld.Y / CellWidthMeters)),
        static_cast<int32_t>(std::floor(PositionWorld.Z / CellWidthMeters)),
        NormalOctant,
    };
}

void BuildSurfelCascades(
    const EmissiveGiSceneView& Scene,
    const EmissiveGiSettings&  Settings,
    SurfelCascade&             OutCascades,
    EmissiveGiMetrics&         InOutMetrics,
    uint64_t&                  InOutTemporaryMemoryBytes)
{
    constexpr std::array<float, 3u> CascadeCellWidthsMeters {{ 0.28f, 1.12f, 4.48f }};
    std::array<SurfelCellMap, 3u> CellMaps {};
    const auto& Primitives = Scene.SceneTree.Primitives;
    const uint32_t SurfelRayCount = std::clamp(Settings.SurfelRayCount, 1u, 32u);

    for (uint32_t PrimitiveOrdinal = 0u; PrimitiveOrdinal < Primitives.size(); ++PrimitiveOrdinal)
    {
        const auto& Primitive = Primitives[PrimitiveOrdinal];
        if (Primitive.MaterialOrdinal >= Scene.MaterialRegister.QuerySlotCount())
        {
            continue;
        }

        const auto& Material = Scene.MaterialRegister.InspectAuthoredMaterial(Primitive.MaterialOrdinal);
        const Mathematics::LinearRGB Albedo = EvaluateDiffuseAlbedo(Material);
        const Mathematics::LinearRGB Emission = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(
            Material.PorosityLight);
        if (RgbEnergy(Albedo) <= 1.0e-6f && RgbEnergy(Emission) <= 1.0e-6f)
        {
            continue;
        }

        const Mathematics::Axis3D CrossProduct = Mathematics::EvaluateCross(Primitive.Edge01, Primitive.Edge02);
        const float TriangleAreaSquareMeters = 0.5f * Mathematics::EvaluateLength(CrossProduct);
        if (TriangleAreaSquareMeters <= 1.0e-9f)
        {
            continue;
        }

        const Mathematics::Axis3D TriangleCenter = Primitive.Corner0 +
            (Primitive.Edge01 + Primitive.Edge02) * (1.0f / 3.0f);
        const Mathematics::Axis3D BaseNormal = Mathematics::NormalizeAxis(CrossProduct, { 0.0f, 0.0f, 1.0f });
        const uint32_t PixelSeed = Mathematics::ConstructPixelSampleSeed(PrimitiveOrdinal, 0x51F15E5Du);

        // Treat authored polygon surfaces as two-sided at the GI layer so interior-facing backs of room walls participate.
        for (uint32_t SideOrdinal = 0u; SideOrdinal < 2u; ++SideOrdinal)
        {
            const Mathematics::Axis3D Normal = SideOrdinal == 0u ? BaseNormal : -BaseNormal;
            TraceSurfaceReading Surface {};
            Surface.Hit              = true;
            Surface.MaterialSlot     = Primitive.MaterialOrdinal;
            Surface.PositionWorld    = TriangleCenter;
            Surface.NormalWorld      = Normal;
            Surface.DiffuseAlbedo    = Albedo;
            Surface.SelfRadiance     = Emission;

            Mathematics::LinearRGB OutgoingRadiance = Emission + EvaluateDirectDiffuseRadiance(
                Scene,
                Surface,
                PixelSeed,
                0u,
                SideOrdinal,
                Settings.MaximumTraceDistance,
                InOutMetrics);

            if (RgbEnergy(Albedo) > 1.0e-6f)
            {
                Mathematics::LinearRGB IndirectSum {};
                for (uint32_t RayOrdinal = 0u; RayOrdinal < SurfelRayCount; ++RayOrdinal)
                {
                    const auto Sample = Mathematics::EvaluateDeterministicR2Sample(
                        PixelSeed, RayOrdinal, 64u + SideOrdinal);
                    const Mathematics::Axis3D Direction = EvaluateCosineHemisphereDirection(Normal, Sample);
                    const Mathematics::LinearRGB Incident = TraceIncidentRadiance(
                        Scene,
                        TriangleCenter + Normal * 2.0e-3f,
                        Direction,
                        PixelSeed,
                        RayOrdinal,
                        Settings,
                        InOutMetrics);
                    IndirectSum = IndirectSum + MultiplyRgb(Albedo, Incident);
                }
                OutgoingRadiance = OutgoingRadiance + IndirectSum * (1.0f / static_cast<float>(SurfelRayCount));
            }

            for (uint32_t CascadeOrdinal = 0u; CascadeOrdinal < CellMaps.size(); ++CascadeOrdinal)
            {
                const SurfelCellKey Key = ConstructSurfelCellKey(
                    TriangleCenter, Normal, CascadeCellWidthsMeters[CascadeOrdinal]);
                SurfelAccumulator& Accumulator = CellMaps[CascadeOrdinal][Key];
                Accumulator.PositionAreaSum = Accumulator.PositionAreaSum + TriangleCenter * TriangleAreaSquareMeters;
                Accumulator.NormalAreaSum = Accumulator.NormalAreaSum + Normal * TriangleAreaSquareMeters;
                Accumulator.RadianceAreaSum = Accumulator.RadianceAreaSum + OutgoingRadiance * TriangleAreaSquareMeters;
                Accumulator.AreaSquareMeters += TriangleAreaSquareMeters;
            }
        }
    }

    const uint32_t SurfelLimit = std::clamp(Settings.SurfelLimitPerCascade, 1u, 65536u);
    for (uint32_t CascadeOrdinal = 0u; CascadeOrdinal < CellMaps.size(); ++CascadeOrdinal)
    {
        const auto& CellMap = CellMaps[CascadeOrdinal];
        auto& Cascade = OutCascades[CascadeOrdinal];
        const std::size_t CellCount = CellMap.size();
        const std::size_t Stride = std::max<std::size_t>(
            1u,
            (CellCount + static_cast<std::size_t>(SurfelLimit) - 1u) / static_cast<std::size_t>(SurfelLimit));
        Cascade.reserve(std::min<std::size_t>(CellCount, SurfelLimit));

        std::size_t CellOrdinal = 0u;
        for (const auto& [Key, Accumulator] : CellMap)
        {
            static_cast<void>(Key);
            if (CellOrdinal++ % Stride != 0u || Accumulator.AreaSquareMeters <= 1.0e-9f)
            {
                continue;
            }

            SurfelRadianceSample Surfel {};
            const float InverseArea = 1.0f / Accumulator.AreaSquareMeters;
            Surfel.PositionWorld = Accumulator.PositionAreaSum * InverseArea;
            Surfel.NormalWorld = Mathematics::NormalizeAxis(Accumulator.NormalAreaSum, { 0.0f, 0.0f, 1.0f });
            Surfel.OutgoingRadiance = Accumulator.RadianceAreaSum * InverseArea;
            Surfel.AreaSquareMeters = Accumulator.AreaSquareMeters;
            Surfel.RadiusMeters = std::sqrt(Accumulator.AreaSquareMeters / Constants::PiFloat);
            Cascade.push_back(Surfel);
        }

        InOutMetrics.SurfelCount += static_cast<uint32_t>(Cascade.size());
        const uint64_t MapNodeEstimate = static_cast<uint64_t>(CellMap.size()) *
            static_cast<uint64_t>(sizeof(SurfelCellKey) + sizeof(SurfelAccumulator) + 3u * sizeof(void*));
        InOutTemporaryMemoryBytes += MapNodeEstimate;
    }
}

[[nodiscard]] float FindNearestSurfelDistanceSquared(
    const std::vector<SurfelRadianceSample>& Surfels,
    Mathematics::Axis3D                       PositionWorld) noexcept
{
    float NearestDistanceSquared = std::numeric_limits<float>::max();
    for (const auto& Surfel : Surfels)
    {
        NearestDistanceSquared = std::min(
            NearestDistanceSquared,
            Mathematics::EvaluateLengthSquared(Surfel.PositionWorld - PositionWorld));
    }
    return NearestDistanceSquared;
}

[[nodiscard]] Mathematics::LinearRGB EvaluateSurfelCascadeIndirect(
    const EmissiveGiSceneView&           Scene,
    const AnalyticResolvedSurfaceSample& PrimarySurface,
    const SurfelCascade&                 Cascades,
    float                                MaximumTraceDistanceMeters,
    EmissiveGiMetrics&                   InOutMetrics) noexcept
{
    Mathematics::LinearRGB ReceiverAlbedo {};
    if (!ResolvePrimaryAlbedo(PrimarySurface, Scene, ReceiverAlbedo))
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const Mathematics::Axis3D ReceiverNormal = Mathematics::NormalizeAxis(
        PrimarySurface.TangentFrame.NormalZ, { 0.0f, 0.0f, 1.0f });
    const float FineDistanceSquared = FindNearestSurfelDistanceSquared(Cascades[0u], PrimarySurface.WorldPosition);
    const float MediumDistanceSquared = FindNearestSurfelDistanceSquared(Cascades[1u], PrimarySurface.WorldPosition);
    uint32_t CascadeOrdinal = 2u;
    if (FineDistanceSquared <= 0.65f * 0.65f)
    {
        CascadeOrdinal = 0u;
    }
    else if (MediumDistanceSquared <= 2.4f * 2.4f)
    {
        CascadeOrdinal = 1u;
    }

    if (Cascades[CascadeOrdinal].empty())
    {
        for (uint32_t Candidate = 0u; Candidate < Cascades.size(); ++Candidate)
        {
            if (!Cascades[Candidate].empty())
            {
                CascadeOrdinal = Candidate;
                break;
            }
        }
    }
    const auto& Surfels = Cascades[CascadeOrdinal];
    if (Surfels.empty())
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    constexpr uint32_t NearestLimit = 8u;
    std::array<const SurfelRadianceSample*, NearestLimit> NearestSurfels {};
    std::array<float, NearestLimit> NearestDistanceSquared {};
    NearestDistanceSquared.fill(std::numeric_limits<float>::max());
    for (const auto& Surfel : Surfels)
    {
        const float DistanceSquared = Mathematics::EvaluateLengthSquared(
            Surfel.PositionWorld - PrimarySurface.WorldPosition);
        for (uint32_t Slot = 0u; Slot < NearestLimit; ++Slot)
        {
            if (DistanceSquared < NearestDistanceSquared[Slot])
            {
                for (uint32_t Shift = NearestLimit - 1u; Shift > Slot; --Shift)
                {
                    NearestDistanceSquared[Shift] = NearestDistanceSquared[Shift - 1u];
                    NearestSurfels[Shift] = NearestSurfels[Shift - 1u];
                }
                NearestDistanceSquared[Slot] = DistanceSquared;
                NearestSurfels[Slot] = &Surfel;
                break;
            }
        }
    }

    constexpr std::array<float, 3u> CascadeMaximumDistanceMeters {{ 1.5f, 4.5f, 12.0f }};
    Mathematics::LinearRGB IrradianceOverPi {};
    float TotalFormFactor = 0.0f;
    for (uint32_t Slot = 0u; Slot < NearestLimit; ++Slot)
    {
        const SurfelRadianceSample* Surfel = NearestSurfels[Slot];
        if (Surfel == nullptr || NearestDistanceSquared[Slot] <= 1.0e-7f)
        {
            continue;
        }

        const float DistanceMeters = std::sqrt(NearestDistanceSquared[Slot]);
        if (DistanceMeters > CascadeMaximumDistanceMeters[CascadeOrdinal] ||
            DistanceMeters >= MaximumTraceDistanceMeters)
        {
            continue;
        }

        const Mathematics::Axis3D DirectionToSurfel =
            (Surfel->PositionWorld - PrimarySurface.WorldPosition) * (1.0f / DistanceMeters);
        const float ReceiverCosine = std::max(0.0f, Mathematics::EvaluateDot(ReceiverNormal, DirectionToSurfel));
        const float SurfelCosine = std::max(0.0f, Mathematics::EvaluateDot(Surfel->NormalWorld, -DirectionToSurfel));
        if (ReceiverCosine <= 1.0e-5f || SurfelCosine <= 1.0e-5f)
        {
            continue;
        }

        const Mathematics::Axis3D VisibilityOrigin =
            PrimarySurface.WorldPosition + ReceiverNormal * 2.0e-3f;
        if (!QueryVisibility(
                Scene, VisibilityOrigin, DirectionToSurfel, DistanceMeters, InOutMetrics))
        {
            continue;
        }

        const float FormFactor = std::clamp(
            Surfel->AreaSquareMeters * ReceiverCosine * SurfelCosine /
                (Constants::PiFloat * NearestDistanceSquared[Slot]),
            0.0f,
            0.5f);
        IrradianceOverPi = IrradianceOverPi + Surfel->OutgoingRadiance * FormFactor;
        TotalFormFactor += FormFactor;
    }

    if (TotalFormFactor > 1.0f)
    {
        IrradianceOverPi = IrradianceOverPi * (1.0f / TotalFormFactor);
    }
    return MultiplyRgb(ReceiverAlbedo, ClampNonNegativeRgb(IrradianceOverPi));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            EMISSIVE GI CANDIDATE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

EmissiveGiOutput EmissiveGiCandidateSequence::Evaluate(
    EmissiveGiMethod                               Method,
    std::span<const AnalyticResolvedSurfaceSample> PrimarySamples,
    uint32_t                                       WidthPx,
    uint32_t                                       HeightPx,
    const EmissiveGiSceneView&                     Scene,
    const EmissiveGiSettings&                      Settings)
{
    const auto BeginClock = std::chrono::steady_clock::now();
    EmissiveGiOutput Result {};
    Result.WidthPx = WidthPx;
    Result.HeightPx = HeightPx;
    const std::size_t PixelCount = static_cast<std::size_t>(WidthPx) * HeightPx;
    Result.IndirectRadiance.assign(PixelCount, { 0.0f, 0.0f, 0.0f });

    SurfelCascade SurfelCascades {};
    uint64_t TemporaryMemoryBytes = 0ull;
    if (Method == EmissiveGiMethod::SurfelCascadeDdgi)
    {
        BuildSurfelCascades(Scene, Settings, SurfelCascades, Result.Metrics, TemporaryMemoryBytes);
    }

    const std::size_t ResolvedCount = std::min(PixelCount, PrimarySamples.size());
    for (std::size_t PixelOrdinal = 0u; PixelOrdinal < ResolvedCount; ++PixelOrdinal)
    {
        const auto& PrimarySurface = PrimarySamples[PixelOrdinal];
        if (!PrimarySurface.ValidHit)
        {
            continue;
        }

        const uint32_t PixelX = static_cast<uint32_t>(PixelOrdinal % std::max(1u, WidthPx));
        const uint32_t PixelY = static_cast<uint32_t>(PixelOrdinal / std::max(1u, WidthPx));
        const uint32_t PixelSeed = Mathematics::ConstructPixelSampleSeed(PixelX, PixelY);

        switch (Method)
        {
            case EmissiveGiMethod::StokesSh9MultiBounce:
                Result.IndirectRadiance[PixelOrdinal] = EvaluateStokesSh9Indirect(
                    Scene, PrimarySurface, PixelSeed, Settings, Result.Metrics);
                break;
            case EmissiveGiMethod::SurfelCascadeDdgi:
                Result.IndirectRadiance[PixelOrdinal] = EvaluateSurfelCascadeIndirect(
                    Scene, PrimarySurface, SurfelCascades, Settings.MaximumTraceDistance, Result.Metrics);
                break;
            case EmissiveGiMethod::DeterministicPathTrace:
                Result.IndirectRadiance[PixelOrdinal] = EvaluatePathTraceIndirect(
                    Scene, PrimarySurface, PixelSeed, Settings, Result.Metrics);
                break;
        }
    }

    uint64_t MemoryBytes = static_cast<uint64_t>(Result.IndirectRadiance.capacity()) *
                           sizeof(Mathematics::LinearRGB);
    for (const auto& Cascade : SurfelCascades)
    {
        MemoryBytes += static_cast<uint64_t>(Cascade.capacity()) * sizeof(SurfelRadianceSample);
    }
    Result.Metrics.ApproximateMemoryBytes = MemoryBytes + TemporaryMemoryBytes;
    Result.Metrics.ElapsedMilliseconds = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - BeginClock).count());
    return Result;
}

} // namespace Frontier::GeometricRaster
