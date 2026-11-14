/*============================================================================================================================================
                                                          ANALYTICPRIMITIVES.CPP
============================================================================================================================================*/
// 🧩 Implementation of parametric box, cylinder/frustum, torus ring, and 5 non-overlapping UDIM studio sample PolygonNetwork builders

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"

#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>

namespace Frontier::GeometrySpace
{

namespace
{

[[nodiscard]] UVPatchBounds SubdivideUVGridCell(
    int32_t  TileU,
    int32_t  TileV,
    uint32_t GridColumns,
    uint32_t GridRows,
    uint32_t CellX,
    uint32_t CellY,
    float    MarginUV = 0.014f) noexcept
{
    const float CellW = 1.0f / static_cast<float>(std::max(1u, GridColumns));
    const float CellH = 1.0f / static_cast<float>(std::max(1u, GridRows));
    const float OriginU = static_cast<float>(TileU) + static_cast<float>(CellX) * CellW;
    const float OriginV = static_cast<float>(TileV) + static_cast<float>(CellY) * CellH;
    return UVPatchBounds{
        { OriginU + MarginUV, OriginV + MarginUV },
        { OriginU + CellW - MarginUV, OriginV + CellH - MarginUV },
    };
}

[[nodiscard]] FaceShadingAssignment ConstructPartMaterial(
    Mathematics::Scalar3D Albedo,
    float                 Metallic,
    float                 Roughness,
    uint8_t               MatCapSlot) noexcept
{
    FaceShadingAssignment Result {};
    Result.AlbedoLinearRGB = Albedo;
    Result.Metallic        = Metallic;
    Result.Roughness       = Roughness;
    Result.Specular        = 0.68f;
    Result.MatCapBlend     = 0.62f;
    Result.MatCapSlot      = static_cast<uint8_t>(MatCapSlot & 15u);
    return Result;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                PARAMETRIC SOLID PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

void AppendBoxPrimitive(
    PolygonNetwork&              TargetNetwork,
    Mathematics::Axis3D          Center,
    Mathematics::Scalar3D        HalfExtents,
    UVPatchBounds                UVRegion,
    const FaceShadingAssignment& Shading,
    uint16_t                     PriorityLayerOrdinal,
    uint16_t                     PartGroupOrdinal)
{
    const uint32_t OriginV = static_cast<uint32_t>(TargetNetwork.Vertices.size());
    const std::array<Mathematics::Axis3D, 8u> Corners {{
        { Center.X - HalfExtents.X, Center.Y - HalfExtents.Y, Center.Z - HalfExtents.Z }, // 0
        { Center.X + HalfExtents.X, Center.Y - HalfExtents.Y, Center.Z - HalfExtents.Z }, // 1
        { Center.X + HalfExtents.X, Center.Y + HalfExtents.Y, Center.Z - HalfExtents.Z }, // 2
        { Center.X - HalfExtents.X, Center.Y + HalfExtents.Y, Center.Z - HalfExtents.Z }, // 3
        { Center.X - HalfExtents.X, Center.Y - HalfExtents.Y, Center.Z + HalfExtents.Z }, // 4
        { Center.X + HalfExtents.X, Center.Y - HalfExtents.Y, Center.Z + HalfExtents.Z }, // 5
        { Center.X + HalfExtents.X, Center.Y + HalfExtents.Y, Center.Z + HalfExtents.Z }, // 6
        { Center.X - HalfExtents.X, Center.Y + HalfExtents.Y, Center.Z + HalfExtents.Z }, // 7
    }};

    for (const Mathematics::Axis3D& Pos : Corners)
    {
        (void)TargetNetwork.AppendVertex(Pos);
    }

    // 📝 Pack 6 box faces into a non-overlapping 3x2 UV sub-grid with positive counter-clockwise UV winding
    const std::array<std::array<uint32_t, 4u>, 6u> QuadFaces {{
        {{ OriginV + 4u, OriginV + 5u, OriginV + 6u, OriginV + 7u }}, // +Z Front
        {{ OriginV + 1u, OriginV + 0u, OriginV + 3u, OriginV + 2u }}, // -Z Back
        {{ OriginV + 5u, OriginV + 1u, OriginV + 2u, OriginV + 6u }}, // +X Right
        {{ OriginV + 0u, OriginV + 4u, OriginV + 7u, OriginV + 3u }}, // -X Left
        {{ OriginV + 7u, OriginV + 6u, OriginV + 2u, OriginV + 3u }}, // +Y Top
        {{ OriginV + 0u, OriginV + 1u, OriginV + 5u, OriginV + 4u }}, // -Y Bottom
    }};

    const float SpanU = UVRegion.Maximum.U - UVRegion.Minimum.U;
    const float SpanV = UVRegion.Maximum.V - UVRegion.Minimum.V;
    const float PadU  = SpanU * 0.025f;
    const float PadV  = SpanV * 0.035f;

    for (uint32_t FaceIdx = 0u; FaceIdx < 6u; ++FaceIdx)
    {
        const uint32_t Col = FaceIdx % 3u;
        const uint32_t Row = FaceIdx / 3u;
        const float    U0  = UVRegion.Minimum.U + (static_cast<float>(Col) / 3.0f) * SpanU + PadU;
        const float    U1  = UVRegion.Minimum.U + (static_cast<float>(Col + 1u) / 3.0f) * SpanU - PadU;
        const float    V0  = UVRegion.Minimum.V + (static_cast<float>(Row) / 2.0f) * SpanV + PadV;
        const float    V1  = UVRegion.Minimum.V + (static_cast<float>(Row + 1u) / 2.0f) * SpanV - PadV;

        const std::array<Mathematics::UVAxes, 4u> QuadUV {{
            { U0, V0 },
            { U1, V0 },
            { U1, V1 },
            { U0, V1 },
        }};
        (void)TargetNetwork.AppendFace(QuadFaces[FaceIdx], QuadUV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
    }
}

void AppendCylinderFrustumPrimitive(
    PolygonNetwork&              TargetNetwork,
    Mathematics::Axis3D          OriginCenter,
    Mathematics::Axis3D          TopCenter,
    float                        FootRadius,
    float                        TopRadius,
    uint32_t                     RadialSegments,
    UVPatchBounds                UVRegion,
    const FaceShadingAssignment& Shading,
    uint16_t                     PriorityLayerOrdinal,
    uint16_t                     PartGroupOrdinal)
{
    const uint32_t Segments = std::max(6u, RadialSegments);
    const Mathematics::Axis3D AxisVec  = TopCenter - OriginCenter;
    const Mathematics::Axis3D AxisUnit = Mathematics::NormalizeAxis(AxisVec, { 0.0f, 1.0f, 0.0f });
    const Mathematics::Axis3D RefUp    = std::abs(AxisUnit.Y) < 0.92f
        ? Mathematics::Axis3D{ 0.0f, 1.0f, 0.0f }
        : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
    const Mathematics::Axis3D TangentU = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(RefUp, AxisUnit));
    const Mathematics::Axis3D TangentV = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(AxisUnit, TangentU));

    const uint32_t LowerRingStart = static_cast<uint32_t>(TargetNetwork.Vertices.size());
    for (uint32_t Seg = 0u; Seg < Segments; ++Seg)
    {
        const float θ = (static_cast<float>(Seg) / static_cast<float>(Segments)) * Constants::TauFloat;
        const Mathematics::Axis3D RadialDir = TangentU * std::cos(θ) + TangentV * std::sin(θ);
        (void)TargetNetwork.AppendVertex(OriginCenter + RadialDir * FootRadius);
    }
    const uint32_t TopRingStart = static_cast<uint32_t>(TargetNetwork.Vertices.size());
    for (uint32_t Seg = 0u; Seg < Segments; ++Seg)
    {
        const float θ = (static_cast<float>(Seg) / static_cast<float>(Segments)) * Constants::TauFloat;
        const Mathematics::Axis3D RadialDir = TangentU * std::cos(θ) + TangentV * std::sin(θ);
        (void)TargetNetwork.AppendVertex(TopCenter + RadialDir * TopRadius);
    }
    const uint32_t LowerCapVertex = TargetNetwork.AppendVertex(OriginCenter);
    const uint32_t UpperCapVertex  = TargetNetwork.AppendVertex(TopCenter);

    const float SpanU      = UVRegion.Maximum.U - UVRegion.Minimum.U;
    const float SpanV      = UVRegion.Maximum.V - UVRegion.Minimum.V;
    const float StripV0    = UVRegion.Minimum.V + SpanV * 0.04f;
    const float StripV1    = UVRegion.Minimum.V + SpanV * 0.54f;
    const float CapCenterV = UVRegion.Minimum.V + SpanV * 0.78f;
    const float CapRadiusU = std::min(SpanU * 0.21f, SpanV * 0.19f);

    // ① Lateral cylindrical quad strip (lower 54% of UVRegion)
    for (uint32_t Seg = 0u; Seg < Segments; ++Seg)
    {
        const uint32_t NextSeg = (Seg + 1u) % Segments;
        const float    U0      = UVRegion.Minimum.U + SpanU * (0.02f + 0.96f * (static_cast<float>(Seg) / static_cast<float>(Segments)));
        const float    U1      = UVRegion.Minimum.U + SpanU * (0.02f + 0.96f * (static_cast<float>(Seg + 1u) / static_cast<float>(Segments)));

        const std::array<uint32_t, 4u> SideLoop {{
            LowerRingStart + Seg,
            LowerRingStart + NextSeg,
            TopRingStart + NextSeg,
            TopRingStart + Seg,
        }};
        const std::array<Mathematics::UVAxes, 4u> SideUV {{
            { U0, StripV0 },
            { U1, StripV0 },
            { U1, StripV1 },
            { U0, StripV1 },
        }};
        (void)TargetNetwork.AppendFace(SideLoop, SideUV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
    }

    // ② Top & bottom disc caps (upper 42% left/right discs, positive CCW UV winding)
    const float LeftCapU  = UVRegion.Minimum.U + SpanU * 0.26f;
    const float RightCapU = UVRegion.Minimum.U + SpanU * 0.74f;

    for (uint32_t Seg = 0u; Seg < Segments; ++Seg)
    {
        const uint32_t NextSeg = (Seg + 1u) % Segments;
        const float    θ0      = (static_cast<float>(Seg) / static_cast<float>(Segments)) * Constants::TauFloat;
        const float    θ1      = (static_cast<float>(Seg + 1u) / static_cast<float>(Segments)) * Constants::TauFloat;

        const std::array<uint32_t, 3u> TopTri {{ UpperCapVertex, TopRingStart + Seg, TopRingStart + NextSeg }};
        const std::array<Mathematics::UVAxes, 3u> TopUV {{
            { LeftCapU, CapCenterV },
            { LeftCapU + std::cos(θ0) * CapRadiusU, CapCenterV + std::sin(θ0) * CapRadiusU },
            { LeftCapU + std::cos(θ1) * CapRadiusU, CapCenterV + std::sin(θ1) * CapRadiusU },
        }};
        (void)TargetNetwork.AppendFace(TopTri, TopUV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);

        const std::array<uint32_t, 3u> FirstTri {{ LowerCapVertex, LowerRingStart + NextSeg, LowerRingStart + Seg }};
        const std::array<Mathematics::UVAxes, 3u> OriginUV {{
            { RightCapU, CapCenterV },
            { RightCapU + std::cos(θ0) * CapRadiusU, CapCenterV + std::sin(θ0) * CapRadiusU },
            { RightCapU + std::cos(θ1) * CapRadiusU, CapCenterV + std::sin(θ1) * CapRadiusU },
        }};
        (void)TargetNetwork.AppendFace(FirstTri, OriginUV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
    }
}

void AppendTorusRingPrimitive(
    PolygonNetwork&              TargetNetwork,
    Mathematics::Axis3D          Center,
    float                        MajorRadius,
    float                        MinorRadius,
    uint32_t                     MajorSegments,
    uint32_t                     MinorSegments,
    UVPatchBounds                UVRegion,
    const FaceShadingAssignment& Shading,
    uint16_t                     PriorityLayerOrdinal,
    uint16_t                     PartGroupOrdinal)
{
    const uint32_t MajSegs   = std::max(8u, MajorSegments);
    const uint32_t MinSegs   = std::max(6u, MinorSegments);
    const uint32_t LowerStart = static_cast<uint32_t>(TargetNetwork.Vertices.size());

    for (uint32_t Maj = 0u; Maj < MajSegs; ++Maj)
    {
        const float φ = (static_cast<float>(Maj) / static_cast<float>(MajSegs)) * Constants::TauFloat;
        const float CosPhi = std::cos(φ), SinPhi = std::sin(φ);
        for (uint32_t Min = 0u; Min < MinSegs; ++Min)
        {
            const float θ    = (static_cast<float>(Min) / static_cast<float>(MinSegs)) * Constants::TauFloat;
            const float Tube = MajorRadius + MinorRadius * std::cos(θ);
            (void)TargetNetwork.AppendVertex({
                Center.X + Tube * CosPhi,
                Center.Y + MinorRadius * std::sin(θ),
                Center.Z + Tube * SinPhi,
            });
        }
    }

    const float SpanU = (UVRegion.Maximum.U - UVRegion.Minimum.U) * 0.94f;
    const float SpanV = (UVRegion.Maximum.V - UVRegion.Minimum.V) * 0.94f;
    const float OrigU = UVRegion.Minimum.U + (UVRegion.Maximum.U - UVRegion.Minimum.U) * 0.03f;
    const float OrigV = UVRegion.Minimum.V + (UVRegion.Maximum.V - UVRegion.Minimum.V) * 0.03f;

    for (uint32_t Maj = 0u; Maj < MajSegs; ++Maj)
    {
        const uint32_t NextMaj = (Maj + 1u) % MajSegs;
        const float    U0      = OrigU + SpanU * (static_cast<float>(Maj) / static_cast<float>(MajSegs));
        const float    U1      = OrigU + SpanU * (static_cast<float>(Maj + 1u) / static_cast<float>(MajSegs));

        for (uint32_t Min = 0u; Min < MinSegs; ++Min)
        {
            const uint32_t NextMin = (Min + 1u) % MinSegs;
            const float    V0      = OrigV + SpanV * (static_cast<float>(Min) / static_cast<float>(MinSegs));
            const float    V1      = OrigV + SpanV * (static_cast<float>(Min + 1u) / static_cast<float>(MinSegs));

            const std::array<uint32_t, 4u> QuadLoop {{
                LowerStart + Maj * MinSegs + Min,
                LowerStart + NextMaj * MinSegs + Min,
                LowerStart + NextMaj * MinSegs + NextMin,
                LowerStart + Maj * MinSegs + NextMin,
            }};
            const std::array<Mathematics::UVAxes, 4u> QuadUV {{
                { U0, V0 },
                { U1, V0 },
                { U1, V1 },
                { U0, V1 },
            }};
            (void)TargetNetwork.AppendFace(QuadLoop, QuadUV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              STUDIO SAMPLE ASSET BUILDERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] std::filesystem::path LocateControlledIntegratorContainer()
{
    std::error_code FsErr {};
    std::filesystem::path SearchDir = std::filesystem::current_path(FsErr);
    for (uint32_t Depth = 0u; Depth < 6u && !SearchDir.empty(); ++Depth)
    {
        const std::filesystem::path Candidate = SearchDir / "EngineContent/Geometry/ControlledIntegrator.GeometrySpace";
        if (std::filesystem::exists(Candidate, FsErr))
        {
            return Candidate;
        }
        const std::filesystem::path UpperDir = SearchDir.parent_path();
        if (UpperDir == SearchDir)
        {
            break;
        }
        SearchDir = UpperDir;
    }
    return std::filesystem::path("EngineContent/Geometry/ControlledIntegrator.GeometrySpace");
}

[[nodiscard]] bool TryLoadControlledIntegratorVehicle(ConstructedSampleAsset& OutAsset)
{
    static std::mutex             CacheMutex {};
    static bool                   CachePopulated = false;
    static ConstructedSampleAsset CachedVehicle  {};

    std::lock_guard<std::mutex> Lock(CacheMutex);
    if (CachePopulated)
    {
        OutAsset = CachedVehicle;
        return true;
    }

    const std::filesystem::path ContainerPath = LocateControlledIntegratorContainer();
    Codecs::SpaceContainerPayload Payload {};
    std::string                   Diagnostic {};
    if (!Codecs::SpaceContainerDecoder::DecodeFromFile(ContainerPath, Payload, Diagnostic) ||
        Payload.BinarySections.size() < 2u)
    {
        return false;
    }

    const auto& GeometryBytes = Payload.BinarySections[0u].PayloadBytes;
    const auto& UVBytes       = Payload.BinarySections[1u].PayloadBytes;
    if (GeometryBytes.size() < 8u)
    {
        return false;
    }

    uint32_t VertCount = 0u;
    uint32_t FaceCount = 0u;
    std::memcpy(&VertCount, GeometryBytes.data(), sizeof(uint32_t));
    std::memcpy(&FaceCount, GeometryBytes.data() + 4u, sizeof(uint32_t));

    const std::size_t ExpectedGeomBytes = 8ull + static_cast<std::size_t>(VertCount) * 12ull + static_cast<std::size_t>(FaceCount) * 16ull;
    const std::size_t ExpectedUVBytes   = static_cast<std::size_t>(FaceCount) * 24ull;
    if (VertCount == 0u || FaceCount == 0u || GeometryBytes.size() < ExpectedGeomBytes || UVBytes.size() < ExpectedUVBytes)
    {
        return false;
    }

    ConstructedSampleAsset Built {};
    Built.Network.NetworkTitle = "Controlled Integrator";
    Built.UDIMTiles            = { 1001u };
    (void)Built.Layers.ConstructLayer("Body Coat Hero", { 0.937f, 0.957f, 0.812f }, 55.0f, 1001u);
    (void)Built.Layers.ConstructLayer("Optics & Glass", { 0.620f, 0.812f, 1.000f }, 15.0f, 1001u);

    const std::array<FaceShadingAssignment, 6u> PartMaterials {{
        // 0: Body Coat Hero (metallic studio silver-azure clearcoat)
        { { 0.27f, 0.37f, 0.56f }, 0.72f, 0.18f, 0.88f, 0.52f, 2u },
        // 1: Vehicle Glass (smoked dielectric canopy)
        { { 0.05f, 0.08f, 0.12f }, 0.08f, 0.06f, 0.95f, 0.62f, 6u },
        // 2: Optics & Lights (crisp headlamp/taillamp optics)
        { { 0.88f, 0.78f, 0.62f }, 0.20f, 0.10f, 0.90f, 0.50f, 5u },
        // 3: Chassis & Trim (satin carbon-graphite aero underbody)
        { { 0.10f, 0.11f, 0.13f }, 0.36f, 0.42f, 0.48f, 0.35f, 1u },
        // 4: Tyre Rubber (vulcanized tread rubber)
        { { 0.06f, 0.065f, 0.072f }, 0.04f, 0.68f, 0.28f, 0.30f, 8u },
        // 5: Rim Alloy (CNC forged machined alloy)
        { { 0.70f, 0.73f, 0.78f }, 0.90f, 0.20f, 0.92f, 0.58f, 2u },
    }};

    Built.Network.ReserveStorage(VertCount, FaceCount * 3u, FaceCount);

    const std::byte* VertCursor = GeometryBytes.data() + 8u;
    for (uint32_t VIdx = 0u; VIdx < VertCount; ++VIdx)
    {
        float Coords[3] { 0.0f, 0.0f, 0.0f };
        std::memcpy(Coords, VertCursor + static_cast<std::size_t>(VIdx) * 12ull, 12ull);
        (void)Built.Network.AppendVertex({ Coords[0], Coords[1], Coords[2] });
    }

    const std::byte* FaceCursor = VertCursor + static_cast<std::size_t>(VertCount) * 12ull;
    const std::byte* UVCursor   = UVBytes.data();

    for (uint32_t FIdx = 0u; FIdx < FaceCount; ++FIdx)
    {
        uint32_t TriIndices[3] { 0u, 0u, 0u };
        uint16_t LayerOrd      = 0u;
        uint16_t PartGroup     = 0u;
        const std::byte* Rec   = FaceCursor + static_cast<std::size_t>(FIdx) * 16ull;
        std::memcpy(TriIndices, Rec, 12ull);
        std::memcpy(&LayerOrd,  Rec + 12ull, 2ull);
        std::memcpy(&PartGroup, Rec + 14ull, 2ull);

        float RawUV[6] {};
        std::memcpy(RawUV, UVCursor + static_cast<std::size_t>(FIdx) * 24ull, 24ull);

        const std::array<uint32_t, 3u> Loop {{ TriIndices[0], TriIndices[1], TriIndices[2] }};
        const std::array<Mathematics::UVAxes, 3u> LoopUV {{
            { RawUV[0], RawUV[1] },
            { RawUV[2], RawUV[3] },
            { RawUV[4], RawUV[5] },
        }};
        const FaceShadingAssignment& Mat = PartMaterials[std::min<std::size_t>(PartGroup, PartMaterials.size() - 1u)];
        (void)Built.Network.AppendFace(Loop, LoopUV, Mat, LayerOrd, static_cast<uint16_t>(PartGroup + 1u));
    }

    Built.Network.LinkOppositeEdges(true);
    Built.Network.RecomputeNormals();
    Built.Network.MarkDihedralSharpSeams(52.0f, false);
    Built.Layers.SynchronizePartitions(Built.Network);

    CachedVehicle  = std::move(Built);
    CachePopulated = true;
    OutAsset       = CachedVehicle;
    return true;
}

void ApplyBoundaryBasisConversionFromYUp(
    PolygonNetwork&                   Network,
    Mathematics::WorldCoordinateBasis TargetBasis)
{
    if (TargetBasis == Mathematics::WorldCoordinateBasis::RightHandedYUp)
    {
        return;
    }
    for (Vertex& Vert : Network.Vertices)
    {
        Vert.Axes = Mathematics::ConvertVectorBetweenBases(
            Vert.Axes,
            Mathematics::WorldCoordinateBasis::RightHandedYUp,
            TargetBasis);
    }
    Network.RecomputeNormals();
}

} // namespace

ConstructedSampleAsset ConstructSamplePolygonNetwork(
    SampleAssetPreset                 Preset,
    Mathematics::WorldCoordinateBasis TargetBasis)
{
    ConstructedSampleAsset Result {};

    switch (Preset)
    {
        case SampleAssetPreset::ControlledIntegrator:
        {
            if (TryLoadControlledIntegratorVehicle(Result))
            {
                ApplyBoundaryBasisConversionFromYUp(Result.Network, TargetBasis);
                return Result;
            }
            Result.Network.NetworkTitle = "Controlled Integrator";
            Result.UDIMTiles            = { 1001u };
            const uint16_t HeroLayer = Result.Layers.ConstructLayer("Body Coat Hero", { 0.937f, 0.957f, 0.812f }, 55.0f, 1001u);
            const uint16_t RotorLayer = Result.Layers.ConstructLayer("Optics & Glass", { 0.620f, 0.812f, 1.000f }, 15.0f, 1001u);

            const auto AluMat   = ConstructPartMaterial({ 0.77f, 0.80f, 0.85f }, 0.85f, 0.22f, 0u);
            const auto BrassMat = ConstructPartMaterial({ 0.85f, 0.64f, 0.28f }, 0.90f, 0.20f, 3u);
            const auto GunMat   = ConstructPartMaterial({ 0.22f, 0.24f, 0.28f }, 0.65f, 0.34f, 5u);

            AppendBoxPrimitive(Result.Network, { 0.0f, -0.35f, 0.0f }, { 0.95f, 0.18f, 0.72f }, SubdivideUVGridCell(0, 0, 3u, 3u, 0u, 0u), AluMat, HeroLayer, 1u);
            AppendCylinderFrustumPrimitive(Result.Network, { 0.0f, -0.17f, 0.0f }, { 0.0f, 0.52f, 0.0f }, 0.56f, 0.48f, 18u, SubdivideUVGridCell(0, 0, 3u, 3u, 1u, 0u), AluMat, HeroLayer, 2u);
            AppendTorusRingPrimitive(Result.Network, { 0.0f, 0.22f, 0.0f }, 0.62f, 0.08f, 20u, 10u, SubdivideUVGridCell(0, 0, 3u, 3u, 2u, 0u), BrassMat, RotorLayer, 3u);
            AppendCylinderFrustumPrimitive(Result.Network, { 0.0f, 0.52f, 0.0f }, { 0.0f, 0.88f, 0.0f }, 0.24f, 0.24f, 16u, SubdivideUVGridCell(0, 0, 3u, 3u, 0u, 1u), BrassMat, RotorLayer, 4u);
            AppendBoxPrimitive(Result.Network, { -0.72f, 0.05f, 0.0f }, { 0.16f, 0.22f, 0.45f }, SubdivideUVGridCell(0, 0, 3u, 3u, 1u, 1u), GunMat, 0u, 5u);
            AppendBoxPrimitive(Result.Network, { 0.72f, 0.05f, 0.0f }, { 0.16f, 0.22f, 0.45f }, SubdivideUVGridCell(0, 0, 3u, 3u, 2u, 1u), GunMat, 0u, 6u);
            break;
        }

        case SampleAssetPreset::AeroCoupeGT:
        {
            Result.Network.NetworkTitle = "Aero Coupe GT";
            Result.UDIMTiles            = { 1001u, 1002u };
            const uint16_t BodyLayer = Result.Layers.ConstructLayer("Bodycoat Hero", { 0.937f, 0.957f, 0.812f }, 55.0f, 1001u);
            const uint16_t TrimLayer = Result.Layers.ConstructLayer("Aero Trim", { 0.620f, 0.812f, 1.000f }, 25.0f, 1001u);

            const auto BodyMat  = ConstructPartMaterial({ 0.30f, 0.49f, 0.79f }, 0.62f, 0.22f, 5u);
            const auto GlassMat = ConstructPartMaterial({ 0.16f, 0.20f, 0.25f }, 0.15f, 0.08f, 8u);
            const auto TrimMat  = ConstructPartMaterial({ 0.21f, 0.22f, 0.24f }, 0.45f, 0.38f, 2u);
            const auto WheelMat = ConstructPartMaterial({ 0.76f, 0.78f, 0.82f }, 0.88f, 0.24f, 0u);

            // UDIM 1001: Bodycoat + Canopy + Aero Splitter/Wing
            AppendBoxPrimitive(Result.Network, { 0.0f, 0.28f, 0.0f }, { 0.92f, 0.22f, 1.95f }, SubdivideUVGridCell(0, 0, 2u, 2u, 0u, 0u), BodyMat, BodyLayer, 1u);
            AppendBoxPrimitive(Result.Network, { 0.0f, 0.64f, -0.18f }, { 0.72f, 0.18f, 0.96f }, SubdivideUVGridCell(0, 0, 2u, 2u, 1u, 0u), GlassMat, BodyLayer, 2u);
            AppendBoxPrimitive(Result.Network, { 0.0f, 0.10f, 2.02f }, { 0.96f, 0.05f, 0.18f }, SubdivideUVGridCell(0, 0, 2u, 2u, 0u, 1u), TrimMat, TrimLayer, 3u);
            AppendBoxPrimitive(Result.Network, { 0.0f, 0.76f, -1.86f }, { 0.90f, 0.04f, 0.18f }, SubdivideUVGridCell(0, 0, 2u, 2u, 1u, 1u), TrimMat, TrimLayer, 4u);

            // UDIM 1002: Four Radial Wheel Units
            const std::array<Mathematics::Axis3D, 4u> WheelCenters {{
                { -0.88f, 0.24f,  1.26f },
                {  0.88f, 0.24f,  1.26f },
                { -0.88f, 0.24f, -1.26f },
                {  0.88f, 0.24f, -1.26f },
            }};
            for (uint32_t WheelIdx = 0u; WheelIdx < 4u; ++WheelIdx)
            {
                const float SignX = WheelCenters[WheelIdx].X < 0.0f ? -1.0f : 1.0f;
                AppendCylinderFrustumPrimitive(
                    Result.Network,
                    { WheelCenters[WheelIdx].X - 0.14f * SignX, WheelCenters[WheelIdx].Y, WheelCenters[WheelIdx].Z },
                    { WheelCenters[WheelIdx].X + 0.14f * SignX, WheelCenters[WheelIdx].Y, WheelCenters[WheelIdx].Z },
                    0.34f,
                    0.34f,
                    16u,
                    SubdivideUVGridCell(1, 0, 2u, 2u, WheelIdx % 2u, WheelIdx / 2u),
                    WheelMat,
                    0u,
                    static_cast<uint16_t>(5u + WheelIdx));
            }
            break;
        }

        case SampleAssetPreset::TacticalCrate:
        {
            Result.Network.NetworkTitle = "Tactical Crate";
            Result.UDIMTiles            = { 1001u };
            const uint16_t LidLayer = Result.Layers.ConstructLayer("Reinforced Lid", { 0.937f, 0.957f, 0.812f }, 45.0f, 1001u);

            const auto OliveMat = ConstructPartMaterial({ 0.48f, 0.58f, 0.47f }, 0.32f, 0.44f, 6u);
            const auto LatchMat = ConstructPartMaterial({ 0.78f, 0.74f, 0.68f }, 0.82f, 0.26f, 1u);

            AppendBoxPrimitive(Result.Network, { 0.0f, 0.34f, 0.0f }, { 0.92f, 0.34f, 0.62f }, SubdivideUVGridCell(0, 0, 2u, 2u, 0u, 0u), OliveMat, 0u, 1u);
            AppendBoxPrimitive(Result.Network, { 0.0f, 0.76f, 0.0f }, { 0.96f, 0.10f, 0.66f }, SubdivideUVGridCell(0, 0, 2u, 2u, 1u, 0u), OliveMat, LidLayer, 2u);
            AppendBoxPrimitive(Result.Network, { -0.52f, 0.62f, 0.65f }, { 0.10f, 0.14f, 0.04f }, SubdivideUVGridCell(0, 0, 2u, 2u, 0u, 1u), LatchMat, LidLayer, 3u);
            AppendBoxPrimitive(Result.Network, {  0.52f, 0.62f, 0.65f }, { 0.10f, 0.14f, 0.04f }, SubdivideUVGridCell(0, 0, 2u, 2u, 1u, 1u), LatchMat, LidLayer, 4u);
            break;
        }

        case SampleAssetPreset::ArmoredBust:
        {
            Result.Network.NetworkTitle = "Armored Bust";
            Result.UDIMTiles            = { 1001u };
            const uint16_t VisorLayer = Result.Layers.ConstructLayer("Helm & Visor", { 0.910f, 0.760f, 0.478f }, 50.0f, 1001u);

            const auto AlloyMat = ConstructPartMaterial({ 0.78f, 0.74f, 0.69f }, 0.80f, 0.25f, 1u);
            const auto GoldMat  = ConstructPartMaterial({ 0.85f, 0.64f, 0.28f }, 0.92f, 0.18f, 3u);

            AppendCylinderFrustumPrimitive(Result.Network, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.56f, 0.0f }, 0.68f, 0.46f, 16u, SubdivideUVGridCell(0, 0, 2u, 2u, 0u, 0u), AlloyMat, 0u, 1u);
            AppendCylinderFrustumPrimitive(Result.Network, { 0.0f, 0.56f, 0.0f }, { 0.0f, 1.18f, 0.0f }, 0.34f, 0.30f, 18u, SubdivideUVGridCell(0, 0, 2u, 2u, 1u, 0u), AlloyMat, VisorLayer, 2u);
            AppendBoxPrimitive(Result.Network, { 0.0f, 0.88f, 0.28f }, { 0.26f, 0.10f, 0.10f }, SubdivideUVGridCell(0, 0, 2u, 2u, 0u, 1u), GoldMat, VisorLayer, 3u);
            AppendTorusRingPrimitive(Result.Network, { 0.0f, 0.56f, 0.0f }, 0.46f, 0.07f, 18u, 8u, SubdivideUVGridCell(0, 0, 2u, 2u, 1u, 1u), GoldMat, 0u, 4u);
            break;
        }

        case SampleAssetPreset::UDIMArchKit:
        {
            Result.Network.NetworkTitle = "UDIM Arch Kit";
            Result.UDIMTiles            = { 1001u, 1002u, 1003u, 1004u };
            const uint16_t ColumnLayer = Result.Layers.ConstructLayer("Fluted Columns", { 0.937f, 0.957f, 0.812f }, 40.0f, 1001u);
            const uint16_t ArchLayer   = Result.Layers.ConstructLayer("Vault & Cornice", { 0.608f, 0.855f, 0.773f }, 35.0f, 1002u);

            const auto StoneMat  = ConstructPartMaterial({ 0.76f, 0.74f, 0.70f }, 0.08f, 0.62f, 9u);
            const auto BronzeMat = ConstructPartMaterial({ 0.72f, 0.52f, 0.28f }, 0.86f, 0.28f, 3u);

            AppendBoxPrimitive(Result.Network, { 0.0f, 0.12f, 0.0f }, { 1.35f, 0.12f, 0.65f }, SubdivideUVGridCell(0, 0, 1u, 1u, 0u, 0u, 0.04f), StoneMat, 0u, 1u);
            AppendCylinderFrustumPrimitive(Result.Network, { -0.88f, 0.24f, 0.0f }, { -0.88f, 1.82f, 0.0f }, 0.22f, 0.19f, 16u, SubdivideUVGridCell(1, 0, 1u, 1u, 0u, 0u, 0.04f), StoneMat, ColumnLayer, 2u);
            AppendCylinderFrustumPrimitive(Result.Network, {  0.88f, 0.24f, 0.0f }, {  0.88f, 1.82f, 0.0f }, 0.22f, 0.19f, 16u, SubdivideUVGridCell(2, 0, 1u, 1u, 0u, 0u, 0.04f), StoneMat, ColumnLayer, 3u);
            AppendBoxPrimitive(Result.Network, { 0.0f, 1.98f, 0.0f }, { 1.40f, 0.16f, 0.68f }, SubdivideUVGridCell(3, 0, 1u, 1u, 0u, 0u, 0.04f), BronzeMat, ArchLayer, 4u);
            break;
        }
    }

    ApplyBoundaryBasisConversionFromYUp(Result.Network, TargetBasis);
    Result.Network.LinkOppositeEdges(true);
    Result.Network.RecomputeNormals();
    Result.Network.MarkDihedralSharpSeams(52.0f, false);
    Result.Layers.SynchronizePartitions(Result.Network);
    return Result;
}

} // namespace Frontier::GeometrySpace
