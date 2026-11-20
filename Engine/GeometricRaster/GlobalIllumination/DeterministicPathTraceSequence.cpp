/*============================================================================================================================================
                                              DETERMINISTICPATHTRACESEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Deterministic multi-bounce path tracer: Owen-scrambled Sobol (0,2) quadrature, GGX VNDF, NEE and 2^24 fixed-point accumulation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/DeterministicPathTraceSequence.h"

#include "Engine/MaterialSpace/Scattering/BlackbodyEmission.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace Frontier::GeometricRaster
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr float  InversePi                     = 0.3183098861837907f;  // [1/rad] - 1/pi diffuse normalization
constexpr double FixedPointRadianceScale       = 16777216.0;           // [1/(cd/m2)] - 2^24 GPU-mirrored accumulator scale
constexpr float  RayOriginBiasMeters           = 2.0e-3f;              // [m] - Self-intersection bias along the shading normal
constexpr uint32_t MaximumLightingBounceDepth  = 8u;                   // [count] - Hard clamp on authored bounce depth
constexpr uint32_t AreaLightTapCount           = 4u;                   // [count] - Deterministic quads taps per area light
constexpr uint64_t FnvOffsetBasis              = 1469598103934665603ull; // [hash] - FNV-1a 64 initial value
constexpr uint64_t FnvPrime                    = 1099511628211ull;      // [hash] - FNV-1a 64 multiplier

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PathSurfaceReading
{
    bool                  Hit           = false;                    // [-]     - Closest hit confirmed
    uint16_t              MaterialSlot  = 0u;                       // [idx]   - Compact register slot
    Mathematics::Axis3D   PositionWorld {};                         // [m]     - Hit position
    Mathematics::Axis3D   NormalWorld   {};                         // [-]     - Shading normal facing the incoming ray
    Mathematics::LinearRGB DiffuseAlbedo {};                        // [-]     - Diffuse albedo (metalness/transmission weighted)
    Mathematics::LinearRGB SelfRadiance  {};                        // [cd/m2] - Planckian emission from the material
    Mathematics::LinearRGB BaseColor    { 0.04f, 0.04f, 0.04f };    // [-]     - Conductor base color
    float                 Metalness      = 0.0f;                    // [-]     - Authored metalness
    float                 Roughness      = 0.5f;                    // [-]     - Authored specular roughness
};

struct PixelAccumulator
{
    int64_t RadianceR = 0; // [cd/m2 * 2^24] - First channel fixed-point accumulator
    int64_t RadianceG = 0; // [cd/m2 * 2^24] - Second channel fixed-point accumulator
    int64_t RadianceB = 0; // [cd/m2 * 2^24] - Third channel fixed-point accumulator
    double  PathLengthSumMeters = 0.0; // [m] - Unscattered path length total
    uint32_t PathLengthCount = 0u;     // [count] - Scattered path length count
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    COLOR MECHANISMS
//------------------------------------------------------------------------------------------------------------------------

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

[[nodiscard]] int64_t QuantizeFixedPoint(double Radiance) noexcept
{
    const double Scaled = std::clamp(
        Radiance * FixedPointRadianceScale,
        -9.0e15,
        9.0e15);
    return static_cast<int64_t>(std::llround(Scaled));
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

// 📝 Schlick Fresnel evaluated for the sampled visible micronormal (keeps the metal tint physical without a spectral fit)
[[nodiscard]] Mathematics::LinearRGB EvaluateSchlickFresnel(
    Mathematics::LinearRGB ReflectanceAtNormal,
    float                  IncidentCosine) noexcept
{
    const float Factor = std::pow(std::clamp(1.0f - IncidentCosine, 0.0f, 1.0f), 5.0f);
    return {
        ReflectanceAtNormal.R + (1.0f - ReflectanceAtNormal.R) * Factor,
        ReflectanceAtNormal.G + (1.0f - ReflectanceAtNormal.G) * Factor,
        ReflectanceAtNormal.B + (1.0f - ReflectanceAtNormal.B) * Factor,
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   TRAVERSAL BACKENDS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] PathSurfaceReading TraceClosestSurface(
    const EmissiveGiSceneView& Scene,
    Mathematics::Axis3D        RayOrigin,
    Mathematics::Axis3D        RayDirection,
    float                      MaximumDistanceMeters,
    PathTraceMetrics&          InOutMetrics) noexcept
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

    ++InOutMetrics.SurfaceHitCount;
    const auto& Material = Scene.MaterialRegister.InspectAuthoredMaterial(Intersection.MaterialOrdinal);
    PathSurfaceReading Reading {};
    Reading.Hit            = true;
    Reading.MaterialSlot   = Intersection.MaterialOrdinal;
    Reading.PositionWorld  = RayOrigin + Ray.UnitDirection * Intersection.HitDistanceMeters;
    Reading.NormalWorld    = Mathematics::NormalizeAxis(Intersection.GeometricNormal, { 0.0f, 0.0f, 1.0f });
    if (Mathematics::EvaluateDot(Reading.NormalWorld, Ray.UnitDirection) > 0.0f)
    {
        Reading.NormalWorld = -Reading.NormalWorld;
    }
    Reading.DiffuseAlbedo = EvaluateDiffuseAlbedo(Material);
    Reading.SelfRadiance  = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Material.PorosityLight);
    Reading.BaseColor     = Material.Diffuse.BaseColor;
    Reading.Metalness     = std::clamp(Material.Specular.Metalness, 0.0f, 1.0f);
    Reading.Roughness     = std::clamp(Material.Specular.Roughness, 0.02f, 1.0f);
    return Reading;
}

[[nodiscard]] bool QueryUnoccluded(
    const EmissiveGiSceneView& Scene,
    Mathematics::Axis3D        OriginWorld,
    Mathematics::Axis3D        DirectionWorld,
    float                      MaximumDistanceMeters,
    PathTraceMetrics&          InOutMetrics) noexcept
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

//------------------------------------------------------------------------------------------------------------------------
//                                                  NEXT-EVENT LIGHTING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Explicit sun / quad-area / point next-event estimate at one path vertex (deterministic Sobol light taps)
[[nodiscard]] Mathematics::LinearRGB EvaluateDirectVertexRadiance(
    const EmissiveGiSceneView&        Scene,
    const PathSurfaceReading&         Surface,
    uint32_t                          PixelSeed,
    uint32_t                          SampleOrdinal,
    uint32_t                          BounceOrdinal,
    const PathTraceSettings&          Settings,
    PathTraceMetrics&                 InOutMetrics) noexcept
{
    if (!Surface.Hit || RgbEnergy(Surface.DiffuseAlbedo) <= 1.0e-6f || !Settings.EnableNextEventEstimation)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    Mathematics::LinearRGB DirectRadiance {};
    const Mathematics::Axis3D BiasedOrigin = Surface.PositionWorld + Surface.NormalWorld * RayOriginBiasMeters;

    const Mathematics::Axis3D SunDirection = Mathematics::NormalizeAxis(
        Scene.Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    const float SunCosine = std::max(0.0f, Mathematics::EvaluateDot(Surface.NormalWorld, SunDirection));
    if (SunCosine > 1.0e-5f &&
        QueryUnoccluded(Scene, BiasedOrigin, SunDirection, Settings.MaximumTraceDistance, InOutMetrics))
    {
        const Mathematics::LinearRGB IncidentSun = Scene.Sun.SolarIrradiance * (SunCosine * InversePi);
        DirectRadiance = DirectRadiance + MultiplyRgb(Surface.DiffuseAlbedo, IncidentSun);
    }

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
        for (uint32_t Tap = 0u; Tap < AreaLightTapCount; ++Tap)
        {
            const uint32_t TapOrdinal = SampleOrdinal * AreaLightTapCount + Tap + LightOrdinal * 17u;
            const auto LightSample = Mathematics::EvaluateOwenSobol02(
                PixelSeed, TapOrdinal, 8u + BounceOrdinal * 2u);
            const Mathematics::Axis3D LightPoint = Light.CenterWorld +
                Light.RightTangent * ((2.0f * LightSample.X - 1.0f) * Light.HalfWidthMeters) +
                Light.UpBitangent * ((2.0f * LightSample.Y - 1.0f) * Light.HalfHeightMeters);
            const Mathematics::Axis3D ToLight = LightPoint - Surface.PositionWorld;
            const float DistanceSquared = Mathematics::EvaluateLengthSquared(ToLight);
            const float DistanceMeters = std::sqrt(std::max(1.0e-8f, DistanceSquared));
            if (DistanceMeters >= Settings.MaximumTraceDistance)
            {
                continue;
            }

            const Mathematics::Axis3D LightDirection = ToLight * (1.0f / DistanceMeters);
            const float SurfaceCosine = std::max(0.0f, Mathematics::EvaluateDot(Surface.NormalWorld, LightDirection));
            const float LightCosineRaw = Mathematics::EvaluateDot(LightNormal, -LightDirection);
            const float LightCosine = Light.TwoSidedEmission
                ? std::fabs(LightCosineRaw)
                : std::max(0.0f, LightCosineRaw);
            if (SurfaceCosine <= 1.0e-5f || LightCosine <= 1.0e-5f ||
                !QueryUnoccluded(Scene, BiasedOrigin, LightDirection, DistanceMeters, InOutMetrics))
            {
                continue;
            }

            const float GeometryScale = SurfaceCosine * LightCosine * LightAreaSquareMeters * InversePi /
                                        (DistanceSquared * static_cast<float>(AreaLightTapCount));
            LightRadianceSum = LightRadianceSum + Light.EmittedRadiance * GeometryScale;
        }
        DirectRadiance = DirectRadiance + MultiplyRgb(Surface.DiffuseAlbedo, LightRadianceSum);
    }

    for (const auto& Light : Scene.PointLights)
    {
        const Mathematics::Axis3D ToLight = Light.PositionWorld - Surface.PositionWorld;
        const float DistanceSquared = Mathematics::EvaluateLengthSquared(ToLight);
        const float DistanceMeters = std::sqrt(std::max(1.0e-8f, DistanceSquared));
        if (DistanceMeters >= Light.RangeMeters || DistanceMeters >= Settings.MaximumTraceDistance)
        {
            continue;
        }

        const Mathematics::Axis3D LightDirection = ToLight * (1.0f / DistanceMeters);
        const float SurfaceCosine = std::max(0.0f, Mathematics::EvaluateDot(Surface.NormalWorld, LightDirection));
        if (SurfaceCosine <= 1.0e-5f ||
            !QueryUnoccluded(Scene, BiasedOrigin, LightDirection, DistanceMeters, InOutMetrics))
        {
            continue;
        }

        const float RangeRatio = std::clamp(DistanceMeters / std::max(0.5f, Light.RangeMeters), 0.0f, 1.0f);
        const float RangeWindow = 1.0f - RangeRatio * RangeRatio;
        const float Attenuation = Light.Intensity * RangeWindow * RangeWindow /
                                  std::max(DistanceSquared, Light.RadiusMeters * Light.RadiusMeters);
        const Mathematics::LinearRGB IncidentPoint = Light.Color * (SurfaceCosine * InversePi * Attenuation);
        DirectRadiance = DirectRadiance + MultiplyRgb(Surface.DiffuseAlbedo, IncidentPoint);
    }

    return DirectRadiance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PATH INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Indirect diffuse estimator from one primary hit; returns the per-sample SUM (the resolve pass divides by spp once)
[[nodiscard]] Mathematics::LinearRGB TraceIndirectDiffuseAtPrimary(
    const EmissiveGiSceneView&           Scene,
    const AnalyticResolvedSurfaceSample& PrimarySurface,
    uint32_t                             PixelSeed,
    const PathTraceSettings&             Settings,
    PathTraceMetrics&                    InOutMetrics,
    double&                              OutPathLengthSumMeters,
    uint32_t&                            OutPathCount) noexcept
{
    if (!PrimarySurface.ValidHit || PrimarySurface.MaterialSlot >= Scene.MaterialRegister.QuerySlotCount())
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const auto& Material = Scene.MaterialRegister.InspectAuthoredMaterial(PrimarySurface.MaterialSlot);
    const Mathematics::LinearRGB Albedo = EvaluateDiffuseAlbedo(Material);
    if (RgbEnergy(Albedo) <= 1.0e-6f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const uint32_t BounceLimit = std::clamp(Settings.MaximumBounceDepth, 1u, MaximumLightingBounceDepth);
    const uint32_t SampleCount = std::clamp(Settings.SamplesPerPixel, 1u, 512u);
    const Mathematics::Axis3D PrimaryNormal = Mathematics::NormalizeAxis(
        PrimarySurface.TangentFrame.NormalZ, { 0.0f, 0.0f, 1.0f });
    const Mathematics::TangentFrame3D PrimaryFrame = Mathematics::ConstructTangentFrame(PrimaryNormal);
    const Mathematics::Axis3D PrimaryOrigin = PrimarySurface.WorldPosition + PrimaryNormal * RayOriginBiasMeters;

    Mathematics::LinearRGB RadianceSum {};
    for (uint32_t SampleOrdinal = 0u; SampleOrdinal < SampleCount; ++SampleOrdinal)
    {
        const auto PrimarySample = Mathematics::EvaluateOwenSobol02(PixelSeed, SampleOrdinal, 0u);
        const Mathematics::Axis3D ScatterDirection =
            Mathematics::EvaluateCosineHemisphereDirection(PrimaryFrame, PrimarySample);

        Mathematics::LinearRGB Throughput = Albedo;
        Mathematics::LinearRGB Radiance {};
        Mathematics::Axis3D PathOrigin = PrimaryOrigin;
        Mathematics::Axis3D PathDirection = ScatterDirection;
        double PathLengthMeters = 0.0;

        for (uint32_t BounceOrdinal = 0u; BounceOrdinal < BounceLimit; ++BounceOrdinal)
        {
            ++InOutMetrics.ScatterRayCount;
            const PathSurfaceReading Surface = TraceClosestSurface(
                Scene, PathOrigin, PathDirection, Settings.MaximumTraceDistance, InOutMetrics);
            if (!Surface.Hit)
            {
                PathLengthMeters += Settings.MaximumTraceDistance;
                Radiance = Radiance + MultiplyRgb(
                    Throughput,
                    EvaluateSkyRadiance(Scene.SkyRadiance, PathDirection));
                break;
            }

            PathLengthMeters += Mathematics::EvaluateLength(Surface.PositionWorld - PathOrigin);
            Radiance = Radiance + MultiplyRgb(Throughput, Surface.SelfRadiance);
            Radiance = Radiance + MultiplyRgb(
                Throughput,
                EvaluateDirectVertexRadiance(
                    Scene, Surface, PixelSeed, SampleOrdinal, BounceOrdinal, Settings, InOutMetrics));

            if (BounceOrdinal + 1u >= BounceLimit || RgbEnergy(Surface.DiffuseAlbedo) <= 1.0e-6f)
            {
                break;
            }

            const auto BounceSample = Mathematics::EvaluateOwenSobol02(
                PixelSeed, SampleOrdinal, 32u + BounceOrdinal);
            const Mathematics::TangentFrame3D SurfaceFrame =
                Mathematics::ConstructTangentFrame(Surface.NormalWorld);
            PathDirection = Mathematics::EvaluateCosineHemisphereDirection(SurfaceFrame, BounceSample);
            PathOrigin = Surface.PositionWorld + Surface.NormalWorld * RayOriginBiasMeters;
            Throughput = MultiplyRgb(Throughput, Surface.DiffuseAlbedo);
        }

        RadianceSum = RadianceSum + Radiance;
        OutPathLengthSumMeters += PathLengthMeters;
        ++OutPathCount;
    }

    return RadianceSum;
}

// 📝 Beauty estimator: emissive + NEE + selected diffuse/GGX-specular lobe per vertex; returns the per-sample SUM
[[nodiscard]] Mathematics::LinearRGB TraceBeautyRadiance(
    const EmissiveGiSceneView& Scene,
    Mathematics::Axis3D        PrimaryOrigin,
    Mathematics::Axis3D        PrimaryDirection,
    uint32_t                   PixelSeed,
    const PathTraceSettings&   Settings,
    PathTraceMetrics&          InOutMetrics,
    double&                    OutPathLengthSumMeters) noexcept
{
    const uint32_t BounceLimit = std::clamp(Settings.MaximumBounceDepth, 1u, MaximumLightingBounceDepth);
    const uint32_t SampleCount = std::clamp(Settings.SamplesPerPixel, 1u, 512u);

    Mathematics::LinearRGB RadianceSum {};
    for (uint32_t SampleOrdinal = 0u; SampleOrdinal < SampleCount; ++SampleOrdinal)
    {
        Mathematics::LinearRGB Radiance {};
        Mathematics::LinearRGB Throughput { 1.0f, 1.0f, 1.0f };
        Mathematics::Axis3D PathOrigin = PrimaryOrigin;
        Mathematics::Axis3D PathDirection = PrimaryDirection;
        double PathLengthMeters = 0.0;

        for (uint32_t BounceOrdinal = 0u; BounceOrdinal < BounceLimit; ++BounceOrdinal)
        {
            ++InOutMetrics.ScatterRayCount;
            const PathSurfaceReading Surface = TraceClosestSurface(
                Scene, PathOrigin, PathDirection, Settings.MaximumTraceDistance, InOutMetrics);
            if (!Surface.Hit)
            {
                Radiance = Radiance + MultiplyRgb(
                    Throughput,
                    EvaluateSkyRadiance(Scene.SkyRadiance, PathDirection));
                break;
            }

            PathLengthMeters += Mathematics::EvaluateLength(Surface.PositionWorld - PathOrigin);
            Radiance = Radiance + MultiplyRgb(Throughput, Surface.SelfRadiance);
            Radiance = Radiance + MultiplyRgb(
                Throughput,
                EvaluateDirectVertexRadiance(
                    Scene, Surface, PixelSeed, SampleOrdinal, BounceOrdinal, Settings, InOutMetrics));

            if (BounceOrdinal + 1u >= BounceLimit)
            {
                break;
            }

            const Mathematics::TangentFrame3D SurfaceFrame =
                Mathematics::ConstructTangentFrame(Surface.NormalWorld);
            const Mathematics::Axis3D IncidentDirection = -PathDirection;
            const float SpecularProbability = Settings.EnableSpecularBounce
                ? std::clamp(
                      Surface.Metalness + (1.0f - Surface.Metalness) * 0.06f,
                      Settings.SpecularMinimumPdf,
                      1.0f - Settings.SpecularMinimumPdf)
                : 0.0f;

            const auto LobeSample = Mathematics::EvaluateOwenSobol02(
                PixelSeed, SampleOrdinal, 48u + BounceOrdinal);
            const Mathematics::Axis3D BiasedOrigin =
                Surface.PositionWorld + Surface.NormalWorld * RayOriginBiasMeters;

            const auto DirectionSample = Mathematics::EvaluateOwenSobol02(
                PixelSeed, SampleOrdinal, 64u + BounceOrdinal);

            if (LobeSample.X < SpecularProbability)
            {
                const Mathematics::LinearRGB ReflectanceAtNormal {
                    0.04f + (Surface.BaseColor.R - 0.04f) * Surface.Metalness,
                    0.04f + (Surface.BaseColor.G - 0.04f) * Surface.Metalness,
                    0.04f + (Surface.BaseColor.B - 0.04f) * Surface.Metalness,
                };
                const Mathematics::Axis3D ScatterDirection = Mathematics::EvaluateGgxVisibleNormalDirection(
                    IncidentDirection,
                    SurfaceFrame,
                    Surface.Roughness,
                    DirectionSample);
                const float ScatterCosine = Mathematics::EvaluateDot(Surface.NormalWorld, ScatterDirection);
                if (ScatterCosine <= 1.0e-5f)
                {
                    break;
                }

                // VNDF weight collapses to F * G1(L) for separable Smith masking, divided by the lobe-selection probability
                const float Alpha = std::max(1.0e-3f, Surface.Roughness * Surface.Roughness);
                const Mathematics::Axis3D VisibleNormal = Mathematics::NormalizeAxis(
                    ScatterDirection + IncidentDirection, Surface.NormalWorld);
                const float VisibleCosine = std::max(0.0f, Mathematics::EvaluateDot(VisibleNormal, IncidentDirection));
                const Mathematics::LinearRGB Fresnel = EvaluateSchlickFresnel(ReflectanceAtNormal, VisibleCosine);
                const Mathematics::LinearRGB LobeWeight = Fresnel * (
                    Mathematics::EvaluateGgxSmithMasking(ScatterCosine, Alpha) / SpecularProbability);
                Throughput = MultiplyRgb(Throughput, LobeWeight);
                PathDirection = ScatterDirection;
            }
            else
            {
                const Mathematics::LinearRGB DiffuseLobeWeight = Surface.DiffuseAlbedo *
                                                                 (1.0f / std::max(1.0e-4f, 1.0f - SpecularProbability));
                PathDirection = Mathematics::EvaluateCosineHemisphereDirection(SurfaceFrame, DirectionSample);
                Throughput = MultiplyRgb(Throughput, DiffuseLobeWeight);
            }

            if (RgbEnergy(Throughput) <= 1.0e-6f)
            {
                break;
            }
            PathOrigin = BiasedOrigin;
        }

        RadianceSum = RadianceSum + Radiance;
        OutPathLengthSumMeters += PathLengthMeters;
    }

    return RadianceSum;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ACCUMULATION OUTPUT
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint64_t HashPixelAccumulators(
    std::span<const PixelAccumulator> Pixels) noexcept
{
    uint64_t Hash = FnvOffsetBasis;
    for (const auto& Pixel : Pixels)
    {
        const int64_t Channels[3u] { Pixel.RadianceR, Pixel.RadianceG, Pixel.RadianceB };
        for (const int64_t Channel : Channels)
        {
            uint64_t Bits = 0ull;
            std::memcpy(&Bits, &Channel, sizeof(uint64_t));
            for (uint32_t ByteOrdinal = 0u; ByteOrdinal < 8u; ++ByteOrdinal)
            {
                Hash ^= (Bits >> (ByteOrdinal * 8u)) & 0xFFu;
                Hash *= FnvPrime;
            }
        }
    }
    return Hash;
}

void ResolveAccumulatedImage(
    const std::vector<PixelAccumulator>& Pixels,
    uint32_t                             WidthPx,
    uint32_t                             HeightPx,
    PathTraceRadianceChannel             Channel,
    const PathTraceSettings&             Settings,
    PathTraceMetrics                     Metrics,
    PathTraceOutput&                     OutOutput) noexcept
{
    const uint32_t SampleCount = std::clamp(Settings.SamplesPerPixel, 1u, 512u);
    const double InverseSampleCount = 1.0 / static_cast<double>(SampleCount);
    OutOutput.WidthPx  = WidthPx;
    OutOutput.HeightPx = HeightPx;
    OutOutput.Channel  = Channel;
    OutOutput.LinearRadiance.assign(Pixels.size(), {});
    OutOutput.PathLengthMeters.assign(Pixels.size(), 0.0f);

    for (std::size_t PixelOrdinal = 0u; PixelOrdinal < Pixels.size(); ++PixelOrdinal)
    {
        const auto& Pixel = Pixels[PixelOrdinal];
        const double Scale = InverseSampleCount / FixedPointRadianceScale;
        OutOutput.LinearRadiance[PixelOrdinal] = ClampNonNegativeRgb(Mathematics::LinearRGB {
            static_cast<float>(static_cast<double>(Pixel.RadianceR) * Scale),
            static_cast<float>(static_cast<double>(Pixel.RadianceG) * Scale),
            static_cast<float>(static_cast<double>(Pixel.RadianceB) * Scale),
        });
        OutOutput.PathLengthMeters[PixelOrdinal] = Pixel.PathLengthCount > 0u
            ? static_cast<float>(Pixel.PathLengthSumMeters / static_cast<double>(Pixel.PathLengthCount))
            : 0.0f;
    }

    OutOutput.FixedPointDigest = HashPixelAccumulators(Pixels);
    Metrics.ApproximateMemoryBytes =
        static_cast<uint64_t>(Pixels.size()) * sizeof(PixelAccumulator) +
        static_cast<uint64_t>(Pixels.size()) * sizeof(Mathematics::LinearRGB) +
        static_cast<uint64_t>(Pixels.size()) * sizeof(float);
    OutOutput.Metrics = Metrics;
}

// 📝 Deterministic row-band partition: every pixel owns its seed, so thread scheduling can never change a result
[[nodiscard]] uint32_t QueryBandCount(uint32_t HeightPx) noexcept
{
    const uint32_t HardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    const uint32_t ThreadCount = std::clamp(HardwareThreads, 1u, 16u);
    return (ThreadCount <= 1u || HeightPx < ThreadCount * 2u) ? 1u : ThreadCount;
}

// 📝 Band ordinal is passed explicitly so each band owns a private metric accumulator (zero shared mutable state)
template <typename BandCallback>
void PartitionRows(uint32_t HeightPx, BandCallback&& Callback)
{
    const uint32_t BandCount = QueryBandCount(HeightPx);
    if (BandCount <= 1u)
    {
        Callback(0u, 0u, HeightPx);
        return;
    }

    std::vector<std::thread> Workers {};
    Workers.reserve(BandCount - 1u);
    const uint32_t BandHeight = (HeightPx + BandCount - 1u) / BandCount;
    for (uint32_t BandOrdinal = 1u; BandOrdinal < BandCount; ++BandOrdinal)
    {
        const uint32_t FirstRow = BandOrdinal * BandHeight;
        const uint32_t LastRow = std::min(HeightPx, FirstRow + BandHeight);
        if (FirstRow < LastRow)
        {
            Workers.emplace_back([&Callback, BandOrdinal, FirstRow, LastRow]()
            {
                Callback(BandOrdinal, FirstRow, LastRow);
            });
        }
    }
    Callback(0u, 0u, std::min(HeightPx, BandHeight));
    for (auto& Worker : Workers)
    {
        Worker.join();
    }
}

// 📝 Integer ray counters are summed after the join: addition is associative, so the merge is bit-identical every run
[[nodiscard]] PathTraceMetrics MergeBandMetrics(std::span<const PathTraceMetrics> Bands) noexcept
{
    PathTraceMetrics Merged {};
    for (const auto& Band : Bands)
    {
        Merged.RayQueryCount      += Band.RayQueryCount;
        Merged.VisibilityRayCount += Band.VisibilityRayCount;
        Merged.ScatterRayCount    += Band.ScatterRayCount;
        Merged.SurfaceHitCount    += Band.SurfaceHitCount;
    }
    return Merged;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

PathTraceTraversalCapability DeterministicPathTraceSequence::ProbeTraversalBackend(
    PathTraceTraversalBackend Backend) noexcept
{
    PathTraceTraversalCapability Capability {};
    Capability.Backend = Backend;
    if (Backend == PathTraceTraversalBackend::RtxRayQuery)
    {
#if defined(FRONTIER_ENABLE_VULKAN_RAY_QUERY)
        Capability.HardwareRayQuerySupported = true;
        Capability.BackendLabel = "rtx_vk_khr_ray_query";
#else
        Capability.HardwareRayQuerySupported = false;
        Capability.BackendLabel = "rtx_ray_query_requested_bvh4_mirror";
#endif
    }
    return Capability;
}

PathTraceOutput DeterministicPathTraceSequence::EvaluateIndirectReference(
    PathTraceTraversalBackend                      Backend,
    std::span<const AnalyticResolvedSurfaceSample> PrimarySamples,
    uint32_t                                       WidthPx,
    uint32_t                                       HeightPx,
    const EmissiveGiSceneView&                     Scene,
    const PathTraceSettings&                       Settings)
{
    const auto BeginTime = std::chrono::steady_clock::now();
    const auto Capability = ProbeTraversalBackend(Backend);
    PathTraceMetrics Metrics {};
    Metrics.BackendLabel = Capability.BackendLabel;
    Metrics.HardwareRayQueryExecuted = Capability.HardwareRayQuerySupported;

    // Both backends execute the same estimator; the RTX build swaps the traversal calls for inline rayQueryEXT in the .slang mirror.
    std::vector<PixelAccumulator> Pixels(static_cast<std::size_t>(WidthPx) * HeightPx);
    std::vector<PathTraceMetrics> BandMetrics(QueryBandCount(HeightPx));
    PartitionRows(HeightPx, [&](uint32_t BandOrdinal, uint32_t FirstRow, uint32_t LastRow)
    {
        PathTraceMetrics& Band = BandMetrics[BandOrdinal];
        for (uint32_t PixelY = FirstRow; PixelY < LastRow; ++PixelY)
        {
            for (uint32_t PixelX = 0u; PixelX < WidthPx; ++PixelX)
            {
                const std::size_t PixelOrdinal = static_cast<std::size_t>(PixelY) * WidthPx + PixelX;
                if (PixelOrdinal >= PrimarySamples.size() || !PrimarySamples[PixelOrdinal].ValidHit)
                {
                    continue;
                }

                const uint32_t PixelSeed = Mathematics::ConstructQuadraturePixelSeed(PixelX, PixelY, 0u);
                double PathLengthSumMeters = 0.0;
                uint32_t PathCount = 0u;
                const Mathematics::LinearRGB Radiance = TraceIndirectDiffuseAtPrimary(
                    Scene,
                    PrimarySamples[PixelOrdinal],
                    PixelSeed,
                    Settings,
                    Band,
                    PathLengthSumMeters,
                    PathCount);

                auto& Pixel = Pixels[PixelOrdinal];
                Pixel.RadianceR = QuantizeFixedPoint(Radiance.R);
                Pixel.RadianceG = QuantizeFixedPoint(Radiance.G);
                Pixel.RadianceB = QuantizeFixedPoint(Radiance.B);
                Pixel.PathLengthSumMeters = PathLengthSumMeters;
                Pixel.PathLengthCount = PathCount;
            }
        }
    });

    const PathTraceMetrics Merged = MergeBandMetrics(BandMetrics);
    Metrics.RayQueryCount      = Merged.RayQueryCount;
    Metrics.VisibilityRayCount = Merged.VisibilityRayCount;
    Metrics.ScatterRayCount    = Merged.ScatterRayCount;
    Metrics.SurfaceHitCount    = Merged.SurfaceHitCount;
    Metrics.ElapsedMilliseconds = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - BeginTime).count());
    PathTraceOutput Output {};
    ResolveAccumulatedImage(
        Pixels, WidthPx, HeightPx, PathTraceRadianceChannel::IndirectDiffuse, Settings, Metrics, Output);
    return Output;
}

PathTraceOutput DeterministicPathTraceSequence::EvaluateBeauty(
    PathTraceTraversalBackend     Backend,
    const PerspectiveCameraBasis& Camera,
    uint32_t                      WidthPx,
    uint32_t                      HeightPx,
    const EmissiveGiSceneView&    Scene,
    const PathTraceSettings&      Settings)
{
    const auto BeginTime = std::chrono::steady_clock::now();
    const auto Capability = ProbeTraversalBackend(Backend);
    PathTraceMetrics Metrics {};
    Metrics.BackendLabel = Capability.BackendLabel;
    Metrics.HardwareRayQueryExecuted = Capability.HardwareRayQuerySupported;

    const OrthonormalCameraBasis CameraBasis = ComputeCameraBasis(Camera);
    std::vector<PixelAccumulator> Pixels(static_cast<std::size_t>(WidthPx) * HeightPx);
    std::vector<PathTraceMetrics> BandMetrics(QueryBandCount(HeightPx));
    PartitionRows(HeightPx, [&](uint32_t BandOrdinal, uint32_t FirstRow, uint32_t LastRow)
    {
        PathTraceMetrics& Band = BandMetrics[BandOrdinal];
        for (uint32_t PixelY = FirstRow; PixelY < LastRow; ++PixelY)
        {
            for (uint32_t PixelX = 0u; PixelX < WidthPx; ++PixelX)
            {
                const uint32_t PixelSeed = Mathematics::ConstructQuadraturePixelSeed(PixelX, PixelY, 1u);
                const auto Jitter = Mathematics::EvaluateOwenSobol02(PixelSeed, 0u, 251u);
                const float NdcX =
                    ((static_cast<float>(PixelX) + Jitter.X) / static_cast<float>(WidthPx)) * 2.0f - 1.0f;
                const float NdcY =
                    1.0f - ((static_cast<float>(PixelY) + Jitter.Y) / static_cast<float>(HeightPx)) * 2.0f;
                const Mathematics::Axis3D PrimaryDirection = Mathematics::NormalizeAxis(
                    CameraBasis.ForwardAxis + CameraBasis.RightAxis * (NdcX * CameraBasis.TanHalfFovX) +
                    CameraBasis.UpAxis * (NdcY * CameraBasis.TanHalfFovY),
                    CameraBasis.ForwardAxis);

                double PathLengthSumMeters = 0.0;
                const Mathematics::LinearRGB Radiance = TraceBeautyRadiance(
                    Scene,
                    CameraBasis.CameraOrigin,
                    PrimaryDirection,
                    PixelSeed,
                    Settings,
                    Band,
                    PathLengthSumMeters);

                const std::size_t PixelOrdinal = static_cast<std::size_t>(PixelY) * WidthPx + PixelX;
                auto& Pixel = Pixels[PixelOrdinal];
                Pixel.RadianceR = QuantizeFixedPoint(Radiance.R);
                Pixel.RadianceG = QuantizeFixedPoint(Radiance.G);
                Pixel.RadianceB = QuantizeFixedPoint(Radiance.B);
                Pixel.PathLengthSumMeters = PathLengthSumMeters;
                Pixel.PathLengthCount = Settings.SamplesPerPixel;
            }
        }
    });

    const PathTraceMetrics Merged = MergeBandMetrics(BandMetrics);
    Metrics.RayQueryCount      = Merged.RayQueryCount;
    Metrics.VisibilityRayCount = Merged.VisibilityRayCount;
    Metrics.ScatterRayCount    = Merged.ScatterRayCount;
    Metrics.SurfaceHitCount    = Merged.SurfaceHitCount;
    Metrics.ElapsedMilliseconds = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - BeginTime).count());
    PathTraceOutput Output {};
    ResolveAccumulatedImage(
        Pixels, WidthPx, HeightPx, PathTraceRadianceChannel::FullBeauty, Settings, Metrics, Output);
    return Output;
}

} // namespace Frontier::GeometricRaster
