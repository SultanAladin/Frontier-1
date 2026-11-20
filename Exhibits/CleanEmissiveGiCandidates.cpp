/*============================================================================================================================================
                                                   CLEANEMISSIVEGICANDIDATES.CPP
============================================================================================================================================*/
// 🧩 Fixed-scene shootout for two GTX shadows, two GTX GI candidates, and a deterministic path-trace reference

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/SolarShadowSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/EmissiveGiCandidateSequence.h"
#include "Engine/GeometricRaster/Visibility/DepthPyramidSequence.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"
#include "Engine/Mathematics/DeterministicSampling.h"
#include "Exhibits/CleanEmissiveGiCandidates.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

namespace
{

using namespace Frontier;

struct Scorecard
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Check(bool Condition, const char* Label)
    {
        if (Condition)
        {
            ++Passed;
            std::cout << "[CleanEmissiveGiCandidates][PASS] " << Label << '\n';
        }
        else
        {
            ++Failed;
            std::cerr << "[CleanEmissiveGiCandidates][FAIL] " << Label << '\n';
        }
    }
};

struct FixedShowcaseScene
{
    std::vector<SpatialAcceleration::RayTrianglePrimitive> Primitives {};
    MaterialSpace::CompactMaterialRegister                Materials {};
    SpatialAcceleration::WideBoundingTree                 SceneTree {};
    GeometricRaster::SolarDirectionalSource               Sun {};
    std::vector<GeometricRaster::PolygonalAreaQuadLight>   AreaLights {};
    std::vector<GeometricRaster::DirectPointLightSpec>     PointLights {};
    Mathematics::LinearRGB                                SkyRadiance { 0.16f, 0.23f, 0.36f };
    GeometricRaster::PerspectiveCameraBasis               Camera {};
};

struct ShadowEvaluation
{
    GeometricRaster::SolarShadowFieldResult Field {};
    float                                  ElapsedMilliseconds = 0.0f;
    uint64_t                               ApproximateMemoryBytes = 0ull;
};

struct ScalarErrorSummary
{
    std::size_t ComparedSampleCount = 0u;
    double      MeanAbsoluteError = 0.0;
    double      MeanSquaredError = 0.0;
    double      LeakageRatio = 0.0;
};

struct CandidateMeasurement
{
    std::string Family {};
    std::string Candidate {};
    double      MeanAbsoluteError = 0.0;
    double      MeanSquaredError = 0.0;
    double      LeakageRatio = 0.0;
    uint64_t    OutputHash = 0ull;
    uint64_t    RepeatedHash = 0ull;
    uint64_t    RayQueryCount = 0ull;
    uint32_t    SurfelCount = 0u;
    uint64_t    ApproximateMemoryBytes = 0ull;
    float       ElapsedMilliseconds = 0.0f;

    [[nodiscard]] bool IsDeterministic() const noexcept
    {
        return OutputHash == RepeatedHash;
    }
};

void AppendRayTriangle(
    std::vector<SpatialAcceleration::RayTrianglePrimitive>& InOutPrimitives,
    Mathematics::Axis3D                                   Corner0,
    Mathematics::Axis3D                                   Corner1,
    Mathematics::Axis3D                                   Corner2,
    uint16_t                                               MaterialSlot)
{
    SpatialAcceleration::RayTrianglePrimitive Primitive {};
    Primitive.Corner0 = Corner0;
    Primitive.Edge01 = Corner1 - Corner0;
    Primitive.Edge02 = Corner2 - Corner0;
    Primitive.MaterialOrdinal = MaterialSlot;
    Primitive.OpaqueMask = 1u;
    Primitive.SourceFaceIndex = static_cast<uint32_t>(InOutPrimitives.size());
    Primitive.PrimitiveOrdinal = static_cast<uint32_t>(InOutPrimitives.size());
    InOutPrimitives.push_back(Primitive);
}

void AppendBoxFace(
    std::vector<SpatialAcceleration::RayTrianglePrimitive>& InOutPrimitives,
    Mathematics::Axis3D                                   FaceCenter,
    Mathematics::Axis3D                                   TangentU,
    Mathematics::Axis3D                                   TangentV,
    float                                                  HalfExtentU,
    float                                                  HalfExtentV,
    uint16_t                                               MaterialSlot)
{
    const Mathematics::Axis3D Corner00 = FaceCenter - TangentU * HalfExtentU - TangentV * HalfExtentV;
    const Mathematics::Axis3D Corner10 = FaceCenter + TangentU * HalfExtentU - TangentV * HalfExtentV;
    const Mathematics::Axis3D Corner11 = FaceCenter + TangentU * HalfExtentU + TangentV * HalfExtentV;
    const Mathematics::Axis3D Corner01 = FaceCenter - TangentU * HalfExtentU + TangentV * HalfExtentV;
    AppendRayTriangle(InOutPrimitives, Corner00, Corner10, Corner11, MaterialSlot);
    AppendRayTriangle(InOutPrimitives, Corner00, Corner11, Corner01, MaterialSlot);
}

void AppendBoxTriangles(
    std::vector<SpatialAcceleration::RayTrianglePrimitive>& InOutPrimitives,
    Mathematics::Axis3D                                   Center,
    Mathematics::Scalar3D                                 HalfExtents,
    uint16_t                                               MaterialSlot)
{
    AppendBoxFace(InOutPrimitives, Center + Mathematics::Axis3D{ HalfExtents.X, 0.0f, 0.0f },
                  { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, HalfExtents.Y, HalfExtents.Z, MaterialSlot);
    AppendBoxFace(InOutPrimitives, Center - Mathematics::Axis3D{ HalfExtents.X, 0.0f, 0.0f },
                  { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, HalfExtents.Z, HalfExtents.Y, MaterialSlot);
    AppendBoxFace(InOutPrimitives, Center + Mathematics::Axis3D{ 0.0f, HalfExtents.Y, 0.0f },
                  { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, HalfExtents.Z, HalfExtents.X, MaterialSlot);
    AppendBoxFace(InOutPrimitives, Center - Mathematics::Axis3D{ 0.0f, HalfExtents.Y, 0.0f },
                  { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, HalfExtents.X, HalfExtents.Z, MaterialSlot);
    AppendBoxFace(InOutPrimitives, Center + Mathematics::Axis3D{ 0.0f, 0.0f, HalfExtents.Z },
                  { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, HalfExtents.X, HalfExtents.Y, MaterialSlot);
    AppendBoxFace(InOutPrimitives, Center - Mathematics::Axis3D{ 0.0f, 0.0f, HalfExtents.Z },
                  { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, HalfExtents.Y, HalfExtents.X, MaterialSlot);
}

void AppendSphereTriangles(
    std::vector<SpatialAcceleration::RayTrianglePrimitive>& InOutPrimitives,
    Mathematics::Axis3D                                   Center,
    float                                                  Radius,
    uint32_t                                               LongitudeSegments,
    uint32_t                                               LatitudeRings,
    uint16_t                                               MaterialSlot)
{
    const uint32_t SafeLongitudeSegments = std::max(8u, LongitudeSegments);
    const uint32_t SafeLatitudeRings = std::max(4u, LatitudeRings);
    const auto Vertex = [Center, Radius, SafeLongitudeSegments, SafeLatitudeRings](uint32_t Latitude, uint32_t Longitude)
    {
        const float Theta = Constants::PiFloat * static_cast<float>(Latitude) / static_cast<float>(SafeLatitudeRings);
        const float Phi = Constants::TauFloat * static_cast<float>(Longitude) / static_cast<float>(SafeLongitudeSegments);
        const float SinTheta = std::sin(Theta);
        return Center + Mathematics::Axis3D{
            Radius * SinTheta * std::cos(Phi),
            Radius * SinTheta * std::sin(Phi),
            Radius * std::cos(Theta),
        };
    };

    for (uint32_t Latitude = 0u; Latitude < SafeLatitudeRings; ++Latitude)
    {
        for (uint32_t Longitude = 0u; Longitude < SafeLongitudeSegments; ++Longitude)
        {
            const uint32_t NextLongitude = Longitude + 1u;
            const Mathematics::Axis3D P00 = Vertex(Latitude, Longitude);
            const Mathematics::Axis3D P01 = Vertex(Latitude, NextLongitude);
            const Mathematics::Axis3D P10 = Vertex(Latitude + 1u, Longitude);
            const Mathematics::Axis3D P11 = Vertex(Latitude + 1u, NextLongitude);
            AppendRayTriangle(InOutPrimitives, P00, P10, P11, MaterialSlot);
            AppendRayTriangle(InOutPrimitives, P00, P11, P01, MaterialSlot);
        }
    }
}

[[nodiscard]] FixedShowcaseScene BuildFixedShowcaseScene(uint32_t WidthPx, uint32_t HeightPx)
{
    FixedShowcaseScene Scene {};

    MaterialSpace::SurfaceMaterial NeutralPlaster {};
    NeutralPlaster.MaterialName = "FixedNeutralPlaster";
    NeutralPlaster.Diffuse.BaseColor = { 0.78f, 0.77f, 0.75f };
    NeutralPlaster.Diffuse.DiffuseRoughness = 0.55f;
    NeutralPlaster.Specular.Roughness = 0.65f;

    MaterialSpace::SurfaceMaterial CrimsonWall {};
    CrimsonWall.MaterialName = "FixedCrimsonWall";
    CrimsonWall.Diffuse.BaseColor = { 0.86f, 0.08f, 0.07f };
    CrimsonWall.Diffuse.DiffuseRoughness = 0.50f;
    CrimsonWall.Specular.Roughness = 0.55f;

    MaterialSpace::SurfaceMaterial EmeraldWall {};
    EmeraldWall.MaterialName = "FixedEmeraldWall";
    EmeraldWall.Diffuse.BaseColor = { 0.09f, 0.80f, 0.17f };
    EmeraldWall.Diffuse.DiffuseRoughness = 0.50f;
    EmeraldWall.Specular.Roughness = 0.55f;

    MaterialSpace::SurfaceMaterial MirrorChrome {};
    MirrorChrome.MaterialName = "FixedMirrorChrome";
    MirrorChrome.Diffuse.BaseColor = { 0.94f, 0.95f, 0.97f };
    MirrorChrome.Specular.Metalness = 1.0f;
    MirrorChrome.Specular.Roughness = 0.035f;

    MaterialSpace::SurfaceMaterial BrushedGold {};
    BrushedGold.MaterialName = "FixedBrushedGold";
    BrushedGold.Diffuse.BaseColor = { 0.96f, 0.76f, 0.32f };
    BrushedGold.Specular.Metalness = 1.0f;
    BrushedGold.Specular.Roughness = 0.18f;
    BrushedGold.Specular.Anisotropy = 0.55f;

    MaterialSpace::SurfaceMaterial CobaltBaffle {};
    CobaltBaffle.MaterialName = "FixedCobaltBaffle";
    CobaltBaffle.Diffuse.BaseColor = { 0.10f, 0.25f, 0.78f };
    CobaltBaffle.Diffuse.BaseWeight = 0.85f;
    CobaltBaffle.Specular.Roughness = 0.28f;

    const uint16_t NeutralSlot = Scene.Materials.RegisterMaterial(NeutralPlaster);
    const uint16_t CrimsonSlot = Scene.Materials.RegisterMaterial(CrimsonWall);
    const uint16_t EmeraldSlot = Scene.Materials.RegisterMaterial(EmeraldWall);
    const uint16_t ChromeSlot = Scene.Materials.RegisterMaterial(MirrorChrome);
    const uint16_t GoldSlot = Scene.Materials.RegisterMaterial(BrushedGold);
    const uint16_t CobaltSlot = Scene.Materials.RegisterMaterial(CobaltBaffle);

    const auto AddBox = [&Scene](
        Mathematics::Axis3D Center,
        Mathematics::Scalar3D HalfExtents,
        uint16_t MaterialSlot)
    {
        AppendBoxTriangles(Scene.Primitives, Center, HalfExtents, MaterialSlot);
    };
    const auto AddSphere = [&Scene](
        Mathematics::Axis3D Center,
        float Radius,
        uint32_t LongitudeSegments,
        uint32_t LatitudeRings,
        uint16_t MaterialSlot)
    {
        AppendSphereTriangles(Scene.Primitives, Center, Radius, LongitudeSegments, LatitudeRings, MaterialSlot);
    };

    // Fixed architectural Cornell-studio layout; material ordinals are authored CompactMaterialRegister slots.
    AddBox({ 0.00f,  0.20f, -0.72f }, { 2.80f, 2.40f, 0.08f }, NeutralSlot);
    AddBox({ 0.00f,  2.35f,  0.70f }, { 2.80f, 0.08f, 1.45f }, NeutralSlot);
    AddBox({-2.15f,  0.25f,  0.70f }, { 0.08f, 2.20f, 1.45f }, CrimsonSlot);
    AddBox({ 2.15f,  0.25f,  0.70f }, { 0.08f, 2.20f, 1.45f }, EmeraldSlot);
    AddBox({-0.45f,  0.55f,  1.92f }, { 1.35f, 1.15f, 0.07f }, CobaltSlot);
    AddBox({-0.15f,  0.55f, -0.40f }, { 0.46f, 0.46f, 0.24f }, GoldSlot);
    AddSphere({-0.15f,  0.55f,  0.34f }, 0.52f, 36u, 22u, ChromeSlot);
    AddSphere({-1.25f,  0.65f, -0.20f }, 0.44f, 32u, 20u, NeutralSlot);
    AddSphere({ 1.25f,  0.65f, -0.20f }, 0.44f, 32u, 20u, NeutralSlot);

    Scene.Sun.DirectionToSun = Mathematics::NormalizeAxis({ 0.42f, -0.55f, 0.72f });
    Scene.Sun.SunAngularRadiusRadians = 0.018f;
    Scene.Sun.SolarIrradiance = { 5.40f, 5.10f, 4.65f };
    Scene.Sun.RtxSamplesPerPixel = 8u;

    GeometricRaster::PolygonalAreaQuadLight CeilingSoftbox {};
    CeilingSoftbox.CenterWorld = { 0.55f, 0.35f, 1.82f };
    CeilingSoftbox.RightTangent = { 1.0f, 0.0f, 0.0f };
    CeilingSoftbox.UpBitangent = { 0.0f, -1.0f, 0.0f };
    CeilingSoftbox.HalfWidthMeters = 0.85f;
    CeilingSoftbox.HalfHeightMeters = 0.65f;
    CeilingSoftbox.EmittedRadiance = { 7.5f, 6.6f, 5.4f };
    Scene.AreaLights.push_back(CeilingSoftbox);

    Scene.Camera.CameraOrigin = { 0.00f, -3.55f, 0.42f };
    Scene.Camera.TargetPoint = { 0.00f, 0.55f, 0.15f };
    Scene.Camera.WorldUpAxis = { 0.00f, 0.00f, 1.00f };
    Scene.Camera.VerticalFovRadians = 43.0f * Constants::DegToRadFloat;
    Scene.Camera.NearClipMeters = 0.15f;
    Scene.Camera.FarClipMeters = 60.0f;
    Scene.Camera.ViewportWidthPx = WidthPx;
    Scene.Camera.ViewportHeightPx = HeightPx;

    (void)Scene.SceneTree.BuildFromTrianglePrimitives(Scene.Primitives);
    return Scene;
}

[[nodiscard]] std::vector<GeometricRaster::AnalyticResolvedSurfaceSample> BuildPrimarySamples(
    const FixedShowcaseScene& Scene,
    uint32_t                 WidthPx,
    uint32_t                 HeightPx)
{
    std::vector<GeometricRaster::AnalyticResolvedSurfaceSample> Samples(
        static_cast<std::size_t>(WidthPx) * HeightPx);
    const auto Basis = GeometricRaster::ComputeCameraBasis(Scene.Camera);

    for (uint32_t PixelY = 0u; PixelY < HeightPx; ++PixelY)
    {
        for (uint32_t PixelX = 0u; PixelX < WidthPx; ++PixelX)
        {
            const float NdcX =
                ((static_cast<float>(PixelX) + 0.5f) / static_cast<float>(WidthPx)) * 2.0f - 1.0f;
            const float NdcY =
                1.0f - ((static_cast<float>(PixelY) + 0.5f) / static_cast<float>(HeightPx)) * 2.0f;
            const Mathematics::Axis3D RayDirection = Mathematics::NormalizeAxis(
                Basis.ForwardAxis + Basis.RightAxis * (NdcX * Basis.TanHalfFovX) +
                Basis.UpAxis * (NdcY * Basis.TanHalfFovY),
                Basis.ForwardAxis);

            SpatialAcceleration::RaySegmentQuery Ray {};
            Ray.Origin = Basis.CameraOrigin;
            Ray.MinimumDistance = Basis.NearClip;
            Ray.UnitDirection = RayDirection;
            Ray.MaximumDistance = Basis.FarClip;
            const auto Hit = Scene.SceneTree.TraceClosestRay(Ray, false);
            if (!Hit.HitConfirmed || Hit.MaterialOrdinal >= Scene.Materials.QuerySlotCount())
            {
                continue;
            }

            auto& Sample = Samples[static_cast<std::size_t>(PixelY) * WidthPx + PixelX];
            Sample.ValidHit = true;
            Sample.MaterialSlot = Hit.MaterialOrdinal;
            Sample.TriangleOrdinal = Hit.PrimitiveIndex;
            Sample.WorldPosition = Basis.CameraOrigin + RayDirection * Hit.HitDistanceMeters;
            Sample.WorldViewDirection = -RayDirection;
            Mathematics::Axis3D Normal = Mathematics::NormalizeAxis(Hit.GeometricNormal, { 0.0f, 0.0f, 1.0f });
            if (Mathematics::EvaluateDot(Normal, RayDirection) > 0.0f)
            {
                Normal = -Normal;
            }
            GeometrySpace::ConstructDuffOrthonormalBasis(
                Normal, Sample.TangentFrame.TangentX, Sample.TangentFrame.BitangentY);
            Sample.TangentFrame.NormalZ = Normal;
        }
    }
    return Samples;
}

[[nodiscard]] uint32_t EncodeSrgbByte(float Value) noexcept
{
    const float Safe = std::clamp(Value, 0.0f, 1.0f);
    const float Srgb = Safe <= 0.0031308f
        ? 12.92f * Safe
        : 1.055f * std::pow(Safe, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint32_t>(std::clamp(std::lround(Srgb * 255.0f), 0l, 255l));
}

[[nodiscard]] uint32_t ToneMapByte(float Radiance) noexcept
{
    const float SafeRadiance = std::isfinite(Radiance) ? std::max(0.0f, Radiance) : 0.0f;
    const float Reinhard = SafeRadiance / (1.0f + SafeRadiance);
    return EncodeSrgbByte(Reinhard);
}

void AppendBigEndian32(std::vector<uint8_t>& Output, uint32_t Value)
{
    Output.push_back(static_cast<uint8_t>((Value >> 24u) & 0xffu));
    Output.push_back(static_cast<uint8_t>((Value >> 16u) & 0xffu));
    Output.push_back(static_cast<uint8_t>((Value >> 8u) & 0xffu));
    Output.push_back(static_cast<uint8_t>(Value & 0xffu));
}

[[nodiscard]] uint32_t ComputeCrc32(std::span<const uint8_t> Bytes) noexcept
{
    uint32_t Crc = 0xffffffffu;
    for (const uint8_t Byte : Bytes)
    {
        Crc ^= Byte;
        for (uint32_t Bit = 0u; Bit < 8u; ++Bit)
        {
            Crc = (Crc >> 1u) ^ (0xedb88320u & (0u - (Crc & 1u)));
        }
    }
    return ~Crc;
}

void AppendPngChunk(
    std::vector<uint8_t>&   Png,
    const std::array<uint8_t, 4u>& ChunkType,
    std::span<const uint8_t> Data)
{
    AppendBigEndian32(Png, static_cast<uint32_t>(Data.size()));
    Png.insert(Png.end(), ChunkType.begin(), ChunkType.end());
    Png.insert(Png.end(), Data.begin(), Data.end());
    std::vector<uint8_t> CrcBytes(ChunkType.begin(), ChunkType.end());
    CrcBytes.insert(CrcBytes.end(), Data.begin(), Data.end());
    AppendBigEndian32(Png, ComputeCrc32(CrcBytes));
}

[[nodiscard]] bool WritePngRgba8(
    const std::filesystem::path& OutputPath,
    uint32_t                    WidthPx,
    uint32_t                    HeightPx,
    std::span<const uint8_t>    RgbaPixels)
{
    const std::size_t ExpectedSize = static_cast<std::size_t>(WidthPx) * HeightPx * 4u;
    if (WidthPx == 0u || HeightPx == 0u || RgbaPixels.size() != ExpectedSize)
    {
        return false;
    }

    std::vector<uint8_t> RawScanlines {};
    RawScanlines.reserve(static_cast<std::size_t>(HeightPx) * (static_cast<std::size_t>(WidthPx) * 4u + 1u));
    for (uint32_t Row = 0u; Row < HeightPx; ++Row)
    {
        RawScanlines.push_back(0u);
        const std::size_t RowStart = static_cast<std::size_t>(Row) * WidthPx * 4u;
        RawScanlines.insert(
            RawScanlines.end(),
            RgbaPixels.begin() + static_cast<std::ptrdiff_t>(RowStart),
            RgbaPixels.begin() + static_cast<std::ptrdiff_t>(RowStart + static_cast<std::size_t>(WidthPx) * 4u));
    }

    std::vector<uint8_t> Zlib {{ 0x78u, 0x01u }};
    std::size_t RawOffset = 0u;
    while (RawOffset < RawScanlines.size())
    {
        const std::size_t BlockSize = std::min<std::size_t>(65535u, RawScanlines.size() - RawOffset);
        const bool FinalBlock = RawOffset + BlockSize == RawScanlines.size();
        const uint16_t Length = static_cast<uint16_t>(BlockSize);
        const uint16_t InverseLength = static_cast<uint16_t>(~Length);
        Zlib.push_back(FinalBlock ? 0x01u : 0x00u);
        Zlib.push_back(static_cast<uint8_t>(Length & 0xffu));
        Zlib.push_back(static_cast<uint8_t>((Length >> 8u) & 0xffu));
        Zlib.push_back(static_cast<uint8_t>(InverseLength & 0xffu));
        Zlib.push_back(static_cast<uint8_t>((InverseLength >> 8u) & 0xffu));
        Zlib.insert(
            Zlib.end(),
            RawScanlines.begin() + static_cast<std::ptrdiff_t>(RawOffset),
            RawScanlines.begin() + static_cast<std::ptrdiff_t>(RawOffset + BlockSize));
        RawOffset += BlockSize;
    }

    uint32_t AdlerA = 1u;
    uint32_t AdlerB = 0u;
    for (const uint8_t Byte : RawScanlines)
    {
        AdlerA = (AdlerA + Byte) % 65521u;
        AdlerB = (AdlerB + AdlerA) % 65521u;
    }
    AppendBigEndian32(Zlib, (AdlerB << 16u) | AdlerA);

    std::vector<uint8_t> Png {{ 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u }};
    std::vector<uint8_t> Header {};
    AppendBigEndian32(Header, WidthPx);
    AppendBigEndian32(Header, HeightPx);
    Header.insert(Header.end(), { 8u, 6u, 0u, 0u, 0u });
    AppendPngChunk(Png, {{ 'I', 'H', 'D', 'R' }}, Header);
    AppendPngChunk(Png, {{ 'I', 'D', 'A', 'T' }}, Zlib);
    AppendPngChunk(Png, {{ 'I', 'E', 'N', 'D' }}, {});

    if (OutputPath.has_parent_path())
    {
        std::error_code DirectoryError {};
        std::filesystem::create_directories(OutputPath.parent_path(), DirectoryError);
        if (DirectoryError)
        {
            return false;
        }
    }
    std::ofstream Output(OutputPath, std::ios::binary);
    if (!Output)
    {
        return false;
    }
    Output.write(reinterpret_cast<const char*>(Png.data()), static_cast<std::streamsize>(Png.size()));
    return static_cast<bool>(Output);
}

void SetPixel(
    std::vector<uint8_t>& Pixels,
    uint32_t              WidthPx,
    uint32_t              HeightPx,
    uint32_t              X,
    uint32_t              Y,
    uint8_t               Red,
    uint8_t               Green,
    uint8_t               Blue)
{
    if (X >= WidthPx || Y >= HeightPx)
    {
        return;
    }
    const std::size_t Offset = (static_cast<std::size_t>(Y) * WidthPx + X) * 4u;
    Pixels[Offset + 0u] = Red;
    Pixels[Offset + 1u] = Green;
    Pixels[Offset + 2u] = Blue;
    Pixels[Offset + 3u] = 255u;
}

void DrawPanelNumber(
    std::vector<uint8_t>& Pixels,
    uint32_t              WidthPx,
    uint32_t              HeightPx,
    uint32_t              OriginX,
    uint32_t              OriginY,
    uint32_t              Number)
{
    constexpr std::array<std::array<uint8_t, 5u>, 3u> Glyphs {{
        {{ 0b010u, 0b110u, 0b010u, 0b010u, 0b111u }},
        {{ 0b110u, 0b001u, 0b010u, 0b100u, 0b111u }},
        {{ 0b110u, 0b001u, 0b010u, 0b001u, 0b110u }},
    }};
    if (Number < 1u || Number > Glyphs.size())
    {
        return;
    }
    const auto& Glyph = Glyphs[Number - 1u];
    for (uint32_t Row = 0u; Row < Glyph.size(); ++Row)
    {
        for (uint32_t Column = 0u; Column < 3u; ++Column)
        {
            if ((Glyph[Row] & (1u << (2u - Column))) != 0u)
            {
                SetPixel(Pixels, WidthPx, HeightPx, OriginX + Column, OriginY + Row, 255u, 255u, 255u);
            }
        }
    }
}

[[nodiscard]] bool WriteRadianceComparisonPng(
    const std::filesystem::path& OutputPath,
    uint32_t                    SourceWidthPx,
    uint32_t                    SourceHeightPx,
    const std::array<std::span<const Mathematics::LinearRGB>, 3u>& Images)
{
    constexpr uint32_t HeaderHeightPx = 18u;
    constexpr uint32_t PanelGapPx = 4u;
    const uint32_t WidthPx = SourceWidthPx * 3u + PanelGapPx * 4u;
    const uint32_t HeightPx = SourceHeightPx + HeaderHeightPx;
    std::vector<uint8_t> Pixels(static_cast<std::size_t>(WidthPx) * HeightPx * 4u, 0u);
    for (std::size_t Offset = 3u; Offset < Pixels.size(); Offset += 4u)
    {
        Pixels[Offset] = 255u;
    }

    constexpr std::array<std::array<uint8_t, 3u>, 3u> HeaderColors {{
        {{ 38u, 111u, 164u }},
        {{ 38u, 142u, 112u }},
        {{ 150u, 91u, 44u }},
    }};
    for (uint32_t Panel = 0u; Panel < Images.size(); ++Panel)
    {
        const uint32_t PanelOriginX = PanelGapPx + Panel * (SourceWidthPx + PanelGapPx);
        for (uint32_t Y = 0u; Y < HeaderHeightPx; ++Y)
        {
            for (uint32_t X = 0u; X < SourceWidthPx; ++X)
            {
                SetPixel(Pixels, WidthPx, HeightPx, PanelOriginX + X, Y,
                         HeaderColors[Panel][0u], HeaderColors[Panel][1u], HeaderColors[Panel][2u]);
            }
        }
        DrawPanelNumber(Pixels, WidthPx, HeightPx, PanelOriginX + 5u, 6u, Panel + 1u);

        const std::size_t SourceCount = std::min(
            Images[Panel].size(), static_cast<std::size_t>(SourceWidthPx) * SourceHeightPx);
        for (std::size_t PixelOrdinal = 0u; PixelOrdinal < SourceCount; ++PixelOrdinal)
        {
            const uint32_t X = static_cast<uint32_t>(PixelOrdinal % SourceWidthPx);
            const uint32_t Y = static_cast<uint32_t>(PixelOrdinal / SourceWidthPx) + HeaderHeightPx;
            const auto& Color = Images[Panel][PixelOrdinal];
            SetPixel(Pixels, WidthPx, HeightPx, PanelOriginX + X, Y,
                     static_cast<uint8_t>(ToneMapByte(Color.R)),
                     static_cast<uint8_t>(ToneMapByte(Color.G)),
                     static_cast<uint8_t>(ToneMapByte(Color.B)));
        }
    }
    return WritePngRgba8(OutputPath, WidthPx, HeightPx, Pixels);
}

[[nodiscard]] bool WriteShadowComparisonPng(
    const std::filesystem::path& OutputPath,
    uint32_t                    SourceWidthPx,
    uint32_t                    SourceHeightPx,
    const std::array<std::span<const float>, 3u>& Images)
{
    constexpr uint32_t HeaderHeightPx = 18u;
    constexpr uint32_t PanelGapPx = 4u;
    const uint32_t WidthPx = SourceWidthPx * 3u + PanelGapPx * 4u;
    const uint32_t HeightPx = SourceHeightPx + HeaderHeightPx;
    std::vector<uint8_t> Pixels(static_cast<std::size_t>(WidthPx) * HeightPx * 4u, 0u);
    for (std::size_t Offset = 3u; Offset < Pixels.size(); Offset += 4u)
    {
        Pixels[Offset] = 255u;
    }

    constexpr std::array<std::array<uint8_t, 3u>, 3u> HeaderColors {{
        {{ 129u, 91u, 42u }},
        {{ 74u, 110u, 69u }},
        {{ 91u, 78u, 148u }},
    }};
    for (uint32_t Panel = 0u; Panel < Images.size(); ++Panel)
    {
        const uint32_t PanelOriginX = PanelGapPx + Panel * (SourceWidthPx + PanelGapPx);
        for (uint32_t Y = 0u; Y < HeaderHeightPx; ++Y)
        {
            for (uint32_t X = 0u; X < SourceWidthPx; ++X)
            {
                SetPixel(Pixels, WidthPx, HeightPx, PanelOriginX + X, Y,
                         HeaderColors[Panel][0u], HeaderColors[Panel][1u], HeaderColors[Panel][2u]);
            }
        }
        DrawPanelNumber(Pixels, WidthPx, HeightPx, PanelOriginX + 5u, 6u, Panel + 1u);

        const std::size_t SourceCount = std::min(
            Images[Panel].size(), static_cast<std::size_t>(SourceWidthPx) * SourceHeightPx);
        for (std::size_t PixelOrdinal = 0u; PixelOrdinal < SourceCount; ++PixelOrdinal)
        {
            const uint32_t X = static_cast<uint32_t>(PixelOrdinal % SourceWidthPx);
            const uint32_t Y = static_cast<uint32_t>(PixelOrdinal / SourceWidthPx) + HeaderHeightPx;
            const float Visibility = std::clamp(Images[Panel][PixelOrdinal], 0.0f, 1.0f);
            const uint8_t Grey = static_cast<uint8_t>(std::lround(Visibility * 255.0f));
            SetPixel(Pixels, WidthPx, HeightPx, PanelOriginX + X, Y, Grey, Grey, Grey);
        }
    }
    return WritePngRgba8(OutputPath, WidthPx, HeightPx, Pixels);
}

[[nodiscard]] uint64_t HashRadiance(std::span<const Mathematics::LinearRGB> Pixels) noexcept
{
    uint64_t Hash = 1469598103934665603ull;
    for (const auto& Pixel : Pixels)
    {
        for (const float Channel : std::array<float, 3u>{{ Pixel.R, Pixel.G, Pixel.B }})
        {
            Hash ^= std::bit_cast<uint32_t>(Channel);
            Hash *= 1099511628211ull;
        }
    }
    return Hash;
}

[[nodiscard]] uint64_t HashVisibility(std::span<const float> Pixels) noexcept
{
    uint64_t Hash = 1469598103934665603ull;
    for (const float Pixel : Pixels)
    {
        Hash ^= std::bit_cast<uint32_t>(Pixel);
        Hash *= 1099511628211ull;
    }
    return Hash;
}

[[nodiscard]] ScalarErrorSummary CompareVisibility(
    std::span<const float> Candidate,
    std::span<const float> Reference,
    float                 VisibilityTolerance = 0.01f) noexcept
{
    ScalarErrorSummary Summary {};
    Summary.ComparedSampleCount = std::min(Candidate.size(), Reference.size());
    if (Summary.ComparedSampleCount == 0u)
    {
        return Summary;
    }

    double AbsoluteError = 0.0;
    double SquaredError = 0.0;
    double ShadowDeficit = 0.0;
    double PositiveShadowExcess = 0.0;
    for (std::size_t Ordinal = 0u; Ordinal < Summary.ComparedSampleCount; ++Ordinal)
    {
        const double Difference = static_cast<double>(Candidate[Ordinal]) - Reference[Ordinal];
        AbsoluteError += std::fabs(Difference);
        SquaredError += Difference * Difference;
        if (Reference[Ordinal] < 0.999f)
        {
            ShadowDeficit += 1.0 - static_cast<double>(Reference[Ordinal]);
            PositiveShadowExcess += std::max(0.0, Difference - VisibilityTolerance);
        }
    }
    const double SampleCount = static_cast<double>(Summary.ComparedSampleCount);
    Summary.MeanAbsoluteError = AbsoluteError / SampleCount;
    Summary.MeanSquaredError = SquaredError / SampleCount;
    Summary.LeakageRatio = PositiveShadowExcess / std::max(1.0e-9, ShadowDeficit);
    return Summary;
}

[[nodiscard]] std::span<const float> QueryComparableVisibility(
    const GeometricRaster::SolarShadowFieldResult& Field) noexcept
{
    return Field.FilteredVisibility.empty()
        ? std::span<const float>(Field.RawVisibility)
        : std::span<const float>(Field.FilteredVisibility);
}

[[nodiscard]] uint64_t QueryShadowArrayMemoryBytes(
    const GeometricRaster::SolarShadowFieldResult& Field) noexcept
{
    return static_cast<uint64_t>(Field.RawVisibility.capacity() +
                                 Field.FilteredVisibility.capacity() +
                                 Field.BlockerDistanceMeters.capacity()) * sizeof(float);
}

[[nodiscard]] ShadowEvaluation EvaluateShadows(
    GeometricRaster::SolarShadowAlgorithm Algorithm,
    const FixedShowcaseScene&             Scene,
    std::span<const GeometricRaster::AnalyticResolvedSurfaceSample> PrimarySamples,
    uint32_t                              WidthPx,
    uint32_t                              HeightPx,
    const GeometricRaster::OrthographicShadowCascade& FineCascade,
    const GeometricRaster::OrthographicShadowCascade& CoarseCascade)
{
    const auto BeginClock = std::chrono::steady_clock::now();
    ShadowEvaluation Evaluation {};
    Evaluation.Field = GeometricRaster::SolarShadowSequence::EvaluateSolarShadowFieldWithAlgorithm(
        Algorithm,
        Scene.Sun,
        WidthPx,
        HeightPx,
        PrimarySamples,
        Scene.SceneTree,
        &FineCascade,
        &CoarseCascade);
    Evaluation.ElapsedMilliseconds = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - BeginClock).count());
    Evaluation.ApproximateMemoryBytes = QueryShadowArrayMemoryBytes(Evaluation.Field);
    return Evaluation;
}

[[nodiscard]] bool WriteCsvReport(
    const std::filesystem::path&             OutputPath,
    std::span<const CandidateMeasurement>    Measurements,
    uint32_t                                 WidthPx,
    uint32_t                                 HeightPx,
    const GeometricRaster::EmissiveGiSettings& GiSettings,
    uint64_t                                 SharedShadowCascadeBytes,
    float                                    SharedShadowCascadeBuildMilliseconds)
{
    if (OutputPath.has_parent_path())
    {
        std::error_code DirectoryError {};
        std::filesystem::create_directories(OutputPath.parent_path(), DirectoryError);
        if (DirectoryError)
        {
            return false;
        }
    }

    std::ofstream Output(OutputPath);
    if (!Output)
    {
        return false;
    }
    Output << std::setprecision(10)
           << "metric,value\n"
           << "scene_id,clean_cornell_studio_v1\n"
           << "width_px," << WidthPx << '\n'
           << "height_px," << HeightPx << '\n'
           << "reference_spp," << GiSettings.SamplesPerPixel << '\n'
           << "bounce_limit," << GiSettings.BounceLimit << '\n'
           << "surfel_rays_per_sample," << GiSettings.SurfelRayCount << '\n'
           << "fixed_camera,origin_0_-3.55_0.42_target_0_0.55_0.15_fov_43deg\n"
           << "seed_policy,pixel_xy_r2_sequence_with_dimension_hash_shift\n"
           << "shadow_reference,WideTreeRayQuery_8_solar_disc_rays\n"
           << "shared_shadow_cascade_cache_bytes," << SharedShadowCascadeBytes << '\n'
           << "shared_shadow_cascade_build_ms," << SharedShadowCascadeBuildMilliseconds << '\n'
           << "family,candidate,mae,mse,leakage_ratio,deterministic,output_hash,repeated_hash,ray_queries,surfels,approx_memory_bytes,elapsed_ms\n";
    for (const auto& Measurement : Measurements)
    {
        Output << Measurement.Family << ','
               << Measurement.Candidate << ','
               << Measurement.MeanAbsoluteError << ','
               << Measurement.MeanSquaredError << ','
               << Measurement.LeakageRatio << ','
               << (Measurement.IsDeterministic() ? "true" : "false") << ','
               << Measurement.OutputHash << ','
               << Measurement.RepeatedHash << ','
               << Measurement.RayQueryCount << ','
               << Measurement.SurfelCount << ','
               << Measurement.ApproximateMemoryBytes << ','
               << Measurement.ElapsedMilliseconds << '\n';
    }
    return static_cast<bool>(Output);
}

[[nodiscard]] CandidateMeasurement MakeGiMeasurement(
    const char*                            Name,
    const GeometricRaster::EmissiveGiOutput& Candidate,
    const GeometricRaster::EmissiveGiOutput& Repeat,
    const GeometricRaster::EmissiveGiOutput& Reference)
{
    const auto Error = Exhibits::CleanEmissiveGi::CompareLinearRadiance(
        Candidate.IndirectRadiance, Reference.IndirectRadiance);
    CandidateMeasurement Measurement {};
    Measurement.Family = "gtx_gi";
    Measurement.Candidate = Name;
    Measurement.MeanAbsoluteError = Error.MeanAbsoluteError;
    Measurement.MeanSquaredError = Error.MeanSquaredError;
    Measurement.LeakageRatio = Error.DarkReferenceLeakageRatio;
    Measurement.OutputHash = HashRadiance(Candidate.IndirectRadiance);
    Measurement.RepeatedHash = HashRadiance(Repeat.IndirectRadiance);
    Measurement.RayQueryCount = Candidate.Metrics.RayQueryCount;
    Measurement.SurfelCount = Candidate.Metrics.SurfelCount;
    Measurement.ApproximateMemoryBytes = Candidate.Metrics.ApproximateMemoryBytes;
    Measurement.ElapsedMilliseconds = Candidate.Metrics.ElapsedMilliseconds;
    return Measurement;
}

[[nodiscard]] CandidateMeasurement MakeShadowMeasurement(
    const char*                         Name,
    const ShadowEvaluation&             Candidate,
    const ShadowEvaluation&             Repeat,
    const ShadowEvaluation&             Reference)
{
    const auto Error = CompareVisibility(
        QueryComparableVisibility(Candidate.Field), QueryComparableVisibility(Reference.Field));
    CandidateMeasurement Measurement {};
    Measurement.Family = "gtx_shadow";
    Measurement.Candidate = Name;
    Measurement.MeanAbsoluteError = Error.MeanAbsoluteError;
    Measurement.MeanSquaredError = Error.MeanSquaredError;
    Measurement.LeakageRatio = Error.LeakageRatio;
    Measurement.OutputHash = HashVisibility(QueryComparableVisibility(Candidate.Field));
    Measurement.RepeatedHash = HashVisibility(QueryComparableVisibility(Repeat.Field));
    Measurement.RayQueryCount = Candidate.Field.RayQueryCount;
    Measurement.ApproximateMemoryBytes = Candidate.ApproximateMemoryBytes;
    Measurement.ElapsedMilliseconds = Candidate.ElapsedMilliseconds;
    return Measurement;
}

void PrintMeasurement(const CandidateMeasurement& Measurement)
{
    std::cout << "[CleanEmissiveGiCandidates][RESULT] family=" << Measurement.Family
              << " candidate=" << Measurement.Candidate
              << " mae=" << Measurement.MeanAbsoluteError
              << " mse=" << Measurement.MeanSquaredError
              << " leakage=" << Measurement.LeakageRatio
              << " deterministic=" << (Measurement.IsDeterministic() ? "yes" : "no")
              << " ray_queries=" << Measurement.RayQueryCount
              << " surfels=" << Measurement.SurfelCount
              << " memory_bytes=" << Measurement.ApproximateMemoryBytes
              << " elapsed_ms=" << Measurement.ElapsedMilliseconds
              << '\n';
}

[[nodiscard]] bool VerifySamplingUtility() noexcept
{
    constexpr uint32_t SampleCount = 4096u;
    const uint32_t Seed = Mathematics::ConstructPixelSampleSeed(123u, 456u);
    double MeanX = 0.0;
    double MeanY = 0.0;
    for (uint32_t SampleOrdinal = 0u; SampleOrdinal < SampleCount; ++SampleOrdinal)
    {
        const auto First = Mathematics::EvaluateDeterministicR2Sample(Seed, SampleOrdinal, 7u);
        const auto Repeat = Mathematics::EvaluateDeterministicR2Sample(Seed, SampleOrdinal, 7u);
        if (!(First.X >= 0.0f && First.X < 1.0f && First.Y >= 0.0f && First.Y < 1.0f) ||
            First.X != Repeat.X || First.Y != Repeat.Y)
        {
            return false;
        }
        MeanX += First.X;
        MeanY += First.Y;
    }
    MeanX /= static_cast<double>(SampleCount);
    MeanY /= static_cast<double>(SampleCount);
    return std::fabs(MeanX - 0.5) < 0.02 && std::fabs(MeanY - 0.5) < 0.02;
}

} // namespace

int main()
{
    Scorecard Score {};
    Score.Check(VerifySamplingUtility(), "Pixel-keyed R2 samples stay in [0,1), repeat exactly, and have balanced means");

    constexpr uint32_t WidthPx = 64u;
    constexpr uint32_t HeightPx = 48u;
    FixedShowcaseScene Scene = BuildFixedShowcaseScene(WidthPx, HeightPx);
    Score.Check(!Scene.SceneTree.WideEntries.empty() && !Scene.SceneTree.Primitives.empty(),
                "Built fixed Cornell-studio geometry BVH4 from authored material ordinals");

    const auto PrimarySamples = BuildPrimarySamples(Scene, WidthPx, HeightPx);
    const std::size_t VisiblePrimaryCount = static_cast<std::size_t>(std::count_if(
        PrimarySamples.begin(), PrimarySamples.end(), [](const auto& Sample) { return Sample.ValidHit; }));
    Score.Check(VisiblePrimaryCount > PrimarySamples.size() / 5u,
                "Captured deterministic primary surface hits from the fixed camera");

    const GeometricRaster::EmissiveGiSceneView SceneView {
        Scene.SceneTree,
        Scene.Materials,
        Scene.Sun,
        std::span<const GeometricRaster::PolygonalAreaQuadLight>(Scene.AreaLights),
        std::span<const GeometricRaster::DirectPointLightSpec>(Scene.PointLights),
        Scene.SkyRadiance,
    };
    GeometricRaster::EmissiveGiSettings GiSettings {};
    GiSettings.SamplesPerPixel = 64u;
    GiSettings.BounceLimit = 4u;
    GiSettings.SurfelLimitPerCascade = 4096u;
    GiSettings.SurfelRayCount = 4u;
    GiSettings.MaximumTraceDistance = 60.0f;

    GeometricRaster::EmissiveGiCandidateSequence GiEvaluator {};
    const auto PathReference = GiEvaluator.Evaluate(
        GeometricRaster::EmissiveGiMethod::DeterministicPathTrace,
        PrimarySamples, WidthPx, HeightPx, SceneView, GiSettings);
    const auto PathReferenceRepeat = GiEvaluator.Evaluate(
        GeometricRaster::EmissiveGiMethod::DeterministicPathTrace,
        PrimarySamples, WidthPx, HeightPx, SceneView, GiSettings);
    const auto Stokes = GiEvaluator.Evaluate(
        GeometricRaster::EmissiveGiMethod::StokesSh9MultiBounce,
        PrimarySamples, WidthPx, HeightPx, SceneView, GiSettings);
    const auto StokesRepeat = GiEvaluator.Evaluate(
        GeometricRaster::EmissiveGiMethod::StokesSh9MultiBounce,
        PrimarySamples, WidthPx, HeightPx, SceneView, GiSettings);
    const auto SurfelCascade = GiEvaluator.Evaluate(
        GeometricRaster::EmissiveGiMethod::SurfelCascadeDdgi,
        PrimarySamples, WidthPx, HeightPx, SceneView, GiSettings);
    const auto SurfelCascadeRepeat = GiEvaluator.Evaluate(
        GeometricRaster::EmissiveGiMethod::SurfelCascadeDdgi,
        PrimarySamples, WidthPx, HeightPx, SceneView, GiSettings);

    std::array<CandidateMeasurement, 3u> GiMeasurements {{
        MakeGiMeasurement("Stokes_SH9_multi_bounce", Stokes, StokesRepeat, PathReference),
        MakeGiMeasurement("Surfel_cascade_DDGI", SurfelCascade, SurfelCascadeRepeat, PathReference),
        MakeGiMeasurement("Deterministic_path_trace_reference", PathReference, PathReferenceRepeat, PathReference),
    }};
    for (const auto& Measurement : GiMeasurements)
    {
        PrintMeasurement(Measurement);
        Score.Check(Measurement.IsDeterministic(), (Measurement.Candidate + " repeats bit-identically").c_str());
        Score.Check(std::isfinite(Measurement.MeanAbsoluteError) && std::isfinite(Measurement.MeanSquaredError),
                    (Measurement.Candidate + " produces finite linear-HDR error metrics").c_str());
    }
    Score.Check(Stokes.Metrics.RayQueryCount > 0u && PathReference.Metrics.RayQueryCount > 0u,
                "SH9 and path-reference methods perform measured BVH4 queries");
    Score.Check(SurfelCascade.Metrics.SurfelCount > 0u,
                "Surfel-cascade method builds deterministic spatial radiance samples");

    GeometricRaster::OrthographicShadowCascade FineCascade {};
    GeometricRaster::OrthographicShadowCascade CoarseCascade {};
    const auto ShadowBuildBegin = std::chrono::steady_clock::now();
    GeometricRaster::SolarShadowSequence::BuildTwoCascadeContactClips(
        Scene.Sun, { 0.0f, 0.55f, 0.15f }, Scene.SceneTree, FineCascade, CoarseCascade);
    const float ShadowCascadeBuildMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ShadowBuildBegin).count());
    const uint64_t SharedShadowCascadeBytes = static_cast<uint64_t>(
        FineCascade.ClosestLightDepth.capacity() + CoarseCascade.ClosestLightDepth.capacity()) * sizeof(float);
    Score.Check(!FineCascade.ClosestLightDepth.empty() && !CoarseCascade.ClosestLightDepth.empty(),
                "Built fixed fine/coarse solar shadow cascades once for both GTX candidates");

    const auto ContactPcss = EvaluateShadows(
        GeometricRaster::SolarShadowAlgorithm::ContactPcss, Scene, PrimarySamples,
        WidthPx, HeightPx, FineCascade, CoarseCascade);
    const auto ContactPcssRepeat = EvaluateShadows(
        GeometricRaster::SolarShadowAlgorithm::ContactPcss, Scene, PrimarySamples,
        WidthPx, HeightPx, FineCascade, CoarseCascade);
    const auto PcssPlaneAware = EvaluateShadows(
        GeometricRaster::SolarShadowAlgorithm::PcssPlaneAware, Scene, PrimarySamples,
        WidthPx, HeightPx, FineCascade, CoarseCascade);
    const auto PcssPlaneAwareRepeat = EvaluateShadows(
        GeometricRaster::SolarShadowAlgorithm::PcssPlaneAware, Scene, PrimarySamples,
        WidthPx, HeightPx, FineCascade, CoarseCascade);
    const auto ShadowReference = EvaluateShadows(
        GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery, Scene, PrimarySamples,
        WidthPx, HeightPx, FineCascade, CoarseCascade);
    const auto ShadowReferenceRepeat = EvaluateShadows(
        GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery, Scene, PrimarySamples,
        WidthPx, HeightPx, FineCascade, CoarseCascade);

    std::array<CandidateMeasurement, 3u> ShadowMeasurements {{
        MakeShadowMeasurement("Contact_PCSS", ContactPcss, ContactPcssRepeat, ShadowReference),
        MakeShadowMeasurement("PCSS_plane_aware", PcssPlaneAware, PcssPlaneAwareRepeat, ShadowReference),
        MakeShadowMeasurement("WideTree_ray_query_reference", ShadowReference, ShadowReferenceRepeat, ShadowReference),
    }};
    for (const auto& Measurement : ShadowMeasurements)
    {
        PrintMeasurement(Measurement);
        Score.Check(Measurement.IsDeterministic(), (Measurement.Candidate + " repeats bit-identically").c_str());
        Score.Check(Measurement.RayQueryCount > 0u || Measurement.Candidate == "PCSS_plane_aware",
                    (Measurement.Candidate + " reports BVH4 query work when that mode issues ray queries").c_str());
    }

    std::array<CandidateMeasurement, 6u> Measurements {{
        GiMeasurements[0u], GiMeasurements[1u], GiMeasurements[2u],
        ShadowMeasurements[0u], ShadowMeasurements[1u], ShadowMeasurements[2u],
    }};

    const std::filesystem::path OutputDirectory =
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Exhibits/Images/GlobalIllumination";
    const bool GiProofWritten = WriteRadianceComparisonPng(
        OutputDirectory / "CleanEmissiveGiCandidates.png",
        WidthPx,
        HeightPx,
        {{
            std::span<const Mathematics::LinearRGB>(Stokes.IndirectRadiance),
            std::span<const Mathematics::LinearRGB>(SurfelCascade.IndirectRadiance),
            std::span<const Mathematics::LinearRGB>(PathReference.IndirectRadiance),
        }});
    const bool ShadowProofWritten = WriteShadowComparisonPng(
        OutputDirectory / "CleanShadowCandidates.png",
        WidthPx,
        HeightPx,
        {{
            QueryComparableVisibility(ContactPcss.Field),
            QueryComparableVisibility(PcssPlaneAware.Field),
            QueryComparableVisibility(ShadowReference.Field),
        }});
    const bool ReportWritten = WriteCsvReport(
        OutputDirectory / "CleanEmissiveGiCandidates.csv",
        Measurements,
        WidthPx,
        HeightPx,
        GiSettings,
        SharedShadowCascadeBytes,
        ShadowCascadeBuildMs);
    Score.Check(GiProofWritten && ShadowProofWritten,
                "Wrote deterministic side-by-side GI and solar-shadow PNG comparison proofs");
    Score.Check(ReportWritten, "Wrote fixed-settings CSV with quality, leakage, determinism, timing, and memory");

    std::cout << "[CleanEmissiveGiCandidates][SUMMARY] passed=" << Score.Passed
              << " failed=" << Score.Failed
              << " scene=clean_cornell_studio_v1"
              << " resolution=" << WidthPx << 'x' << HeightPx
              << " spp=" << GiSettings.SamplesPerPixel
              << " bounces=" << GiSettings.BounceLimit
              << " shared_shadow_cascade_bytes=" << SharedShadowCascadeBytes
              << " shadow_preprocess_ms=" << ShadowCascadeBuildMs
              << " output=" << OutputDirectory.string() << '\n';
    return Score.Failed == 0u ? 0 : 1;
}
