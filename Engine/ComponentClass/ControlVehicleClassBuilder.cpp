/*============================================================================================================================================
                                                      CONTROLVEHICLECLASSBUILDER.CPP
============================================================================================================================================*/
// 🧩 Implementation of ControlVehicle .GeometrySpace ingestion (ControlledIntegrator.GeometrySpace), RearWing synthesis & constraints

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ControlVehicleClassBuilder.h"
#include "Engine/Codecs/GeometryCodec.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/Interchange/IndexedTriangleStreamInterchange.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace Frontier::ComponentClass
{

namespace
{

struct ExtractedWheelHub
{
    Mathematics::Axis3D CenterMeters { 0.0f, 0.50855f, 0.0f }; // [m] - Hub center in Y-up ground space
    float               MinY         = 0.0f;                   // [m] - Lowest tyre point
    float               MaxY         = 1.0171f;                // [m] - Highest tyre point
    uint32_t            RimFaceCount = 0u;                     // [cnt] - Matched rim triangle count
};

void AppendTiltedWingBox(
    GeometrySpace::PolygonNetwork& Network,
    Mathematics::Axis3D            Center,
    Mathematics::Scalar3D          HalfExtents,
    uint32_t                       CourseSlotId,
    float                          TiltRadians)
{
    const float Cs = std::cos(TiltRadians);
    const float Sn = std::sin(TiltRadians);

    std::array<uint32_t, 8u> V {};
    for (uint32_t Corner = 0u; Corner < 8u; ++Corner)
    {
        const float X = ((Corner & 1u) != 0u ? 1.0f : -1.0f) * HalfExtents.X;
        const float Y = ((Corner & 2u) != 0u ? 1.0f : -1.0f) * HalfExtents.Y;
        const float Z = ((Corner & 4u) != 0u ? 1.0f : -1.0f) * HalfExtents.Z;
        V[Corner] = Network.AppendVertex({
            Center.X + X * Cs - Y * Sn,
            Center.Y + X * Sn + Y * Cs,
            Center.Z + Z,
        });
    }

    const Interchange::CourseMaterialDescriptor* Mat =
        Interchange::IndexedTriangleStreamInterchange::InspectCourseMaterial(CourseSlotId);
    GeometrySpace::FaceShadingAssignment Shading {};
    if (Mat != nullptr)
    {
        Shading.AlbedoLinearRGB     = Mat->AlbedoLinearRGB;
        Shading.MatCapTintLinearRGB = Mat->AlbedoLinearRGB;
        Shading.Metallic            = Mat->Metallic;
        Shading.Roughness           = Mat->Roughness;
        Shading.Specular            = 0.5f;
        Shading.MatCapBlend         = 0.0f;
        Shading.MatCapSlot          = static_cast<uint8_t>(CourseSlotId & 0x0Fu);
    }

    constexpr std::array<std::array<uint32_t, 4u>, 6u> Quads {{
        { 0u, 2u, 3u, 1u },
        { 4u, 5u, 7u, 6u },
        { 0u, 1u, 5u, 4u },
        { 2u, 6u, 7u, 3u },
        { 0u, 4u, 6u, 2u },
        { 1u, 3u, 7u, 5u },
    }};

    const std::array<Mathematics::UVAxes, 3u> DefaultUVs {{
        { 0.10f, 0.10f },
        { 0.90f, 0.10f },
        { 0.50f, 0.90f },
    }};

    for (const auto& Q : Quads)
    {
        const std::array<uint32_t, 3u> T0 { V[Q[0]], V[Q[1]], V[Q[2]] };
        const std::array<uint32_t, 3u> T1 { V[Q[0]], V[Q[2]], V[Q[3]] };
        (void)Network.AppendFace(T0, DefaultUVs, Shading, 1u, static_cast<uint16_t>(CourseSlotId));
        (void)Network.AppendFace(T1, DefaultUVs, Shading, 1u, static_cast<uint16_t>(CourseSlotId));
    }
}

void CopyFaceIntoSubMesh(
    const GeometrySpace::PolygonNetwork&    Source,
    uint32_t                                FaceIndex,
    Mathematics::Axis3D                     OriginShiftMeters,
    std::unordered_map<uint32_t, uint32_t>& VertexRemap,
    GeometrySpace::PolygonNetwork&          Target)
{
    const GeometrySpace::Face& F = Source.Faces[FaceIndex];
    if (F.CornerCount < 3u || F.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex)
    {
        return;
    }

    std::vector<uint32_t> LoopIndices {};
    std::vector<Mathematics::UVAxes> LoopUVs {};
    LoopIndices.reserve(F.CornerCount);
    LoopUVs.reserve(F.CornerCount);

    uint32_t EdgeCursor = F.BoundaryLoopEdgeIndex;
    for (uint16_t Step = 0u; Step < F.CornerCount; ++Step)
    {
        const GeometrySpace::DirectedEdge& Edge = Source.Edges[EdgeCursor];
        const uint32_t SrcVert                  = Edge.OriginVertexIndex;
        auto Found                              = VertexRemap.find(SrcVert);
        uint32_t DstVert                        = 0u;
        if (Found == VertexRemap.end())
        {
            const Mathematics::Axis3D P = Source.Vertices[SrcVert].Axes;
            DstVert = Target.AppendVertex({
                P.X - OriginShiftMeters.X,
                P.Y - OriginShiftMeters.Y,
                P.Z - OriginShiftMeters.Z,
            });
            VertexRemap.emplace(SrcVert, DstVert);
        }
        else
        {
            DstVert = Found->second;
        }
        LoopIndices.push_back(DstVert);
        LoopUVs.push_back(Edge.ParameterUV);
        EdgeCursor = Edge.NextEdgeIndex;
    }

    (void)Target.AppendFace(LoopIndices, LoopUVs, F.Shading, F.PriorityLayerOrdinal, F.PartGroupOrdinal);
}

[[nodiscard]] Mathematics::Axis3D ComputeFaceCentroid(const GeometrySpace::PolygonNetwork& Source, uint32_t FaceIndex)
{
    const GeometrySpace::Face& F = Source.Faces[FaceIndex];
    if (F.CornerCount == 0u || F.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    Mathematics::Axis3D Sum { 0.0f, 0.0f, 0.0f };
    uint32_t EdgeCursor = F.BoundaryLoopEdgeIndex;
    for (uint16_t Step = 0u; Step < F.CornerCount; ++Step)
    {
        const Mathematics::Axis3D P = Source.Vertices[Source.Edges[EdgeCursor].OriginVertexIndex].Axes;
        Sum.X += P.X;
        Sum.Y += P.Y;
        Sum.Z += P.Z;
        EdgeCursor = Source.Edges[EdgeCursor].NextEdgeIndex;
    }
    const float Inv = 1.0f / static_cast<float>(F.CornerCount);
    return { Sum.X * Inv, Sum.Y * Inv, Sum.Z * Inv };
}

[[nodiscard]] std::filesystem::path ResolveControlledIntegratorPath(const std::filesystem::path& HintPath)
{
    std::error_code FsErr {};
    if (std::filesystem::is_regular_file(HintPath, FsErr) && HintPath.extension() == ".GeometrySpace")
    {
        return HintPath;
    }

    // Walk upward from HintPath to locate EngineContent/Geometry/ControlledIntegrator.GeometrySpace
    std::filesystem::path Walk = std::filesystem::is_directory(HintPath, FsErr) ? HintPath : HintPath.parent_path();
    for (uint32_t Depth = 0u; Depth < 6u && !Walk.empty(); ++Depth)
    {
        const std::filesystem::path Direct = Walk / "EngineContent" / "Geometry" / "ControlledIntegrator.GeometrySpace";
        if (std::filesystem::is_regular_file(Direct, FsErr))
        {
            return Direct;
        }
        const std::filesystem::path Sibling = Walk / "ControlledIntegrator.GeometrySpace";
        if (std::filesystem::is_regular_file(Sibling, FsErr))
        {
            return Sibling;
        }
        const std::filesystem::path Parent = Walk.parent_path();
        if (Parent == Walk)
        {
            break;
        }
        Walk = Parent;
    }

    return std::filesystem::path("EngineContent/Geometry/ControlledIntegrator.GeometrySpace");
}

} // namespace

GeometrySpace::ConstructedSampleAsset ControlVehicleClassBuilder::ConstructProceduralRearWing()
{
    GeometrySpace::ConstructedSampleAsset Out {};
    Out.Network.NetworkTitle = "RearWing";
    Out.UDIMTiles = { 1001u };

    constexpr float AoaDeg         = 8.0f;
    constexpr float BodyHalfWidth  = 1.2498f;
    constexpr float ChordHalf      = 0.17f;
    constexpr float PlaneHalfThick = 0.016f;
    constexpr float PlateHalfThick = 0.008f;
    constexpr float PlaneY         = 0.25f;

    const float Tilt = -AoaDeg * static_cast<float>(Constants::DegToRad);
    const float PlateZ = BodyHalfWidth - PlateHalfThick;
    const float Span   = PlateZ - PlateHalfThick;

    AppendTiltedWingBox(Out.Network, { 0.0f, PlaneY, 0.0f }, { ChordHalf, PlaneHalfThick, Span }, 8u, Tilt);

    const float TeX = -ChordHalf * std::cos(Tilt) - PlaneHalfThick * std::sin(Tilt);
    const float TeY = PlaneY - ChordHalf * std::sin(Tilt) + PlaneHalfThick * std::cos(Tilt);
    AppendTiltedWingBox(
        Out.Network,
        { TeX + 0.008f * std::cos(Tilt) - 0.022f * std::sin(Tilt), TeY + 0.008f * std::sin(Tilt) + 0.022f * std::cos(Tilt), 0.0f },
        { 0.008f, 0.022f, Span },
        6u,
        Tilt);

    AppendTiltedWingBox(Out.Network, { -0.01f, 0.23f, -PlateZ }, { 0.20f, 0.10f, PlateHalfThick }, 6u, 0.0f);
    AppendTiltedWingBox(Out.Network, { -0.01f, 0.23f,  PlateZ }, { 0.20f, 0.10f, PlateHalfThick }, 6u, 0.0f);
    AppendTiltedWingBox(Out.Network, {  0.02f, 0.11f,  -0.56f }, { 0.05f, 0.11f, 0.01f }, 10u, 0.0f);
    AppendTiltedWingBox(Out.Network, {  0.02f, 0.11f,   0.56f }, { 0.05f, 0.11f, 0.01f }, 10u, 0.0f);

    Out.Network.LinkOppositeEdges(false);
    Out.Network.MarkDihedralSharpSeams(40.0f, false);
    Out.Network.RecomputeNormals();
    Out.Layers.SynchronizePartitions(Out.Network, GeometrySpace::GeometryInstanceToken{ 0u, 1u });
    return Out;
}

bool ControlVehicleClassBuilder::ConstructFromControlledIntegrator(
    const std::filesystem::path& ControlledIntegratorPath,
    ControlVehicleBuildResult&   OutResult,
    std::string&                 OutDiagnostic)
{
    OutDiagnostic.clear();
    OutResult = ControlVehicleBuildResult{};

    Codecs::GeometrySpaceDocument SourceDoc {};
    if (!Codecs::GeometryDecoder::DecodeFromFile(ControlledIntegratorPath, SourceDoc, OutDiagnostic))
    {
        return false;
    }

    const GeometrySpace::PolygonNetwork& SourceNet = SourceDoc.Network;
    if (SourceNet.Faces.empty() || SourceNet.Vertices.empty())
    {
        OutDiagnostic = "ControlledIntegrator.GeometrySpace decoded with empty PolygonNetwork.";
        return false;
    }
    OutResult.DecodedSourceFaces = static_cast<uint32_t>(SourceNet.Faces.size());

    // 1. Extract exact 4-corner wheel hub coordinates from RimAlloy (PartGroupOrdinal == 6) in Y-up meters
    //    Corner indices: 0 = FL (+X, -Z), 1 = FR (+X, +Z), 2 = RL (-X, -Z), 3 = RR (-X, +Z)
    struct BoxBounds
    {
        Mathematics::Axis3D Min { 1.0e9f, 1.0e9f, 1.0e9f };
        Mathematics::Axis3D Max { -1.0e9f, -1.0e9f, -1.0e9f };
        uint32_t            Count = 0u;
    };
    std::array<BoxBounds, 4u> RimQuadrants {};
    for (uint32_t FIdx = 0u; FIdx < static_cast<uint32_t>(SourceNet.Faces.size()); ++FIdx)
    {
        const GeometrySpace::Face& F = SourceNet.Faces[FIdx];
        if (F.PartGroupOrdinal != 6u || F.CornerCount < 3u)
        {
            continue;
        }
        const Mathematics::Axis3D C = ComputeFaceCentroid(SourceNet, FIdx);
        const uint32_t Q            = (C.X >= 0.0f ? 0u : 2u) + (C.Z < 0.0f ? 0u : 1u);
        BoxBounds& B                = RimQuadrants[Q];
        ++B.Count;

        uint32_t EdgeCursor = F.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < F.CornerCount; ++Step)
        {
            const Mathematics::Axis3D P = SourceNet.Vertices[SourceNet.Edges[EdgeCursor].OriginVertexIndex].Axes;
            B.Min.X = std::min(B.Min.X, P.X); B.Max.X = std::max(B.Max.X, P.X);
            B.Min.Y = std::min(B.Min.Y, P.Y); B.Max.Y = std::max(B.Max.Y, P.Y);
            B.Min.Z = std::min(B.Min.Z, P.Z); B.Max.Z = std::max(B.Max.Z, P.Z);
            EdgeCursor = SourceNet.Edges[EdgeCursor].NextEdgeIndex;
        }
    }

    constexpr std::array<Mathematics::Axis3D, 4u> FallbackHubs {{
        {  1.7274f, 0.50855f, -1.0475f },
        {  1.7274f, 0.50855f,  1.0475f },
        { -1.6686f, 0.50855f, -1.0475f },
        { -1.6686f, 0.50855f,  1.0475f },
    }};

    std::array<ExtractedWheelHub, 4u> Hubs {};
    for (uint32_t Q = 0u; Q < 4u; ++Q)
    {
        Hubs[Q].RimFaceCount = RimQuadrants[Q].Count;
        if (RimQuadrants[Q].Count > 0u)
        {
            Hubs[Q].CenterMeters = {
                0.5f * (RimQuadrants[Q].Min.X + RimQuadrants[Q].Max.X),
                0.5f * (RimQuadrants[Q].Min.Y + RimQuadrants[Q].Max.Y),
                0.5f * (RimQuadrants[Q].Min.Z + RimQuadrants[Q].Max.Z),
            };
        }
        else
        {
            Hubs[Q].CenterMeters = FallbackHubs[Q];
        }
        Hubs[Q].MinY = 1.0e9f;
        Hubs[Q].MaxY = -1.0e9f;
    }

    // 2. Partition decoded ControlledIntegrator.GeometrySpace into Body, canonical FL Tyre, and canonical FL Rim
    //    GroundOffset matches chassis reference height (TyreRadius - 0.0914 m axle offset above body frame)
    constexpr float NominalAxleOffsetAboveBody = 0.0914f;
    const float ExtractedTyreRadius            = std::clamp(Hubs[0u].CenterMeters.Y, 0.30f, 0.70f);
    const float GroundOffset                   = ExtractedTyreRadius - NominalAxleOffsetAboveBody;

    CompiledGeometryRepositoryEntry BodyEntry {};
    BodyEntry.AssetIdentifier           = "ControlVehicleBody";
    BodyEntry.DisplayTitle              = "ControlledIntegrator Body";
    BodyEntry.SourceFormat              = "GeometrySpace";
    BodyEntry.Asset.Network.NetworkTitle = "ControlVehicleBody";
    BodyEntry.Asset.UDIMTiles           = SourceDoc.UDIMTiles;

    CompiledGeometryRepositoryEntry TyreEntry {};
    TyreEntry.AssetIdentifier           = "ControlVehicleTyre";
    TyreEntry.DisplayTitle              = "ControlledIntegrator Tyre";
    TyreEntry.SourceFormat              = "GeometrySpace";
    TyreEntry.Asset.Network.NetworkTitle = "ControlVehicleTyre";
    TyreEntry.Asset.UDIMTiles           = SourceDoc.UDIMTiles;

    CompiledGeometryRepositoryEntry RimEntry {};
    RimEntry.AssetIdentifier            = "ControlVehicleRim";
    RimEntry.DisplayTitle               = "ControlledIntegrator Rim";
    RimEntry.SourceFormat               = "GeometrySpace";
    RimEntry.Asset.Network.NetworkTitle = "ControlVehicleRim";
    RimEntry.Asset.UDIMTiles            = SourceDoc.UDIMTiles;

    std::unordered_map<uint32_t, uint32_t> BodyVertRemap {};
    std::unordered_map<uint32_t, uint32_t> TyreVertRemap {};
    std::unordered_map<uint32_t, uint32_t> RimVertRemap {};

    for (uint32_t FIdx = 0u; FIdx < static_cast<uint32_t>(SourceNet.Faces.size()); ++FIdx)
    {
        const GeometrySpace::Face& F = SourceNet.Faces[FIdx];
        const Mathematics::Axis3D C  = ComputeFaceCentroid(SourceNet, FIdx);

        // Identify whether face belongs to one of the 4 wheel wells (TyreRubber=5, RimAlloy=6)
        int32_t MatchedWheel = -1;
        if (F.PartGroupOrdinal == 5u || F.PartGroupOrdinal == 6u)
        {
            for (uint32_t Q = 0u; Q < 4u; ++Q)
            {
                const Mathematics::Axis3D& H = Hubs[Q].CenterMeters;
                if (std::fabs(C.X - H.X) < 0.56f && std::fabs(C.Y - H.Y) < 0.56f && std::fabs(C.Z - H.Z) < 0.28f)
                {
                    MatchedWheel = static_cast<int32_t>(Q);
                    break;
                }
            }
        }

        if (MatchedWheel >= 0)
        {
            if (F.PartGroupOrdinal == 5u)
            {
                Hubs[MatchedWheel].MinY = std::min(Hubs[MatchedWheel].MinY, C.Y);
                Hubs[MatchedWheel].MaxY = std::max(Hubs[MatchedWheel].MaxY, C.Y);
            }
            // Extract canonical FL wheel (Q == 0) into reusable Tyre and Rim repository assets
            if (MatchedWheel == 0)
            {
                if (F.PartGroupOrdinal == 5u)
                {
                    CopyFaceIntoSubMesh(SourceNet, FIdx, Hubs[0u].CenterMeters, TyreVertRemap, TyreEntry.Asset.Network);
                }
                else
                {
                    CopyFaceIntoSubMesh(SourceNet, FIdx, Hubs[0u].CenterMeters, RimVertRemap, RimEntry.Asset.Network);
                }
            }
        }
        else
        {
            CopyFaceIntoSubMesh(SourceNet, FIdx, { 0.0f, GroundOffset, 0.0f }, BodyVertRemap, BodyEntry.Asset.Network);
        }
    }

    auto FinalizeSubAsset = [](GeometrySpace::ConstructedSampleAsset& Asset)
    {
        Asset.Network.LinkOppositeEdges(false);
        Asset.Network.RecomputeNormals();
        Asset.Layers.SynchronizePartitions(Asset.Network, GeometrySpace::GeometryInstanceToken{ 0u, 1u });
    };
    FinalizeSubAsset(BodyEntry.Asset);
    FinalizeSubAsset(TyreEntry.Asset);
    FinalizeSubAsset(RimEntry.Asset);

    OutResult.GeometryRepository.push_back(std::move(BodyEntry));
    OutResult.GeometryRepository.push_back(std::move(TyreEntry));
    OutResult.GeometryRepository.push_back(std::move(RimEntry));

    {
        CompiledGeometryRepositoryEntry WingEntry {};
        WingEntry.AssetIdentifier = "RearWing";
        WingEntry.DisplayTitle    = "Rear Wing";
        WingEntry.SourceFormat    = "Procedural";
        WingEntry.Asset           = ConstructProceduralRearWing();
        OutResult.GeometryRepository.push_back(std::move(WingEntry));
    }

    // 3. Assemble ComponentClass hierarchy using wheel hubs & tyre radius measured from ControlledIntegrator.GeometrySpace
    ComponentClassDefinition& Def = OutResult.ClassDefinition;
    Def.Title                     = "ControlVehicle";
    Def.ClassArchetype            = ComponentClassArchetype::Driveable;
    Def.Storage.ResetWithSpatialOrigin("Origin");

    const ComponentInstanceToken Origin = Def.Storage.QueryOriginToken();

    const ComponentInstanceToken Body = Def.Storage.AppendInstance(
        ComponentCategory::GeometryInstance,
        "Body",
        Origin,
        ComponentTransform{ { 0.0f, GroundOffset, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (GeometryComponentSettings* G = Def.Storage.ResolveGeometry(Body))
    {
        G->AssetIdentifier  = "ControlVehicleBody";
        G->CastShadow       = true;
        G->CollisionProfile = GeometryCollisionProfile::BlockAll;
    }

    constexpr std::array<const char*, 4u> CornerSuffixes {{ "FL", "FR", "RL", "RR" }};
    std::vector<ComponentInstanceToken> WheelTokens {};
    WheelTokens.reserve(4u);

    for (uint32_t Q = 0u; Q < 4u; ++Q)
    {
        const bool Steerable         = (Q < 2u);
        const Mathematics::Axis3D& H = Hubs[Q].CenterMeters;
        const std::string SpringName = std::string("Suspension") + CornerSuffixes[Q];

        const ComponentInstanceToken Spring = Def.Storage.AppendInstance(
            ComponentCategory::ConstraintInstance,
            SpringName,
            Body,
            ComponentTransform{ { H.X, H.Y - GroundOffset, H.Z }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
        if (ConstraintComponentSettings* C = Def.Storage.ResolveConstraint(Spring))
        {
            C->Form                = PhysicsConstraintCategory::Spring;
            C->Axis                = ConstraintAxis::AxisY;
            C->NaturalFrequencyHz  = 2.00f;
            C->DampingRatioZeta    = 0.42f;
            C->TravelEachWayMeters = 0.090f;
            C->MinTravelMeters     = -0.090f;
            C->MaxTravelMeters     = 0.090f;
        }

        ComponentInstanceToken WheelMount = Spring;
        if (Steerable)
        {
            const std::string SteerName = std::string("Steer") + CornerSuffixes[Q];
            WheelMount = Def.Storage.AppendInstance(
                ComponentCategory::ConstraintInstance,
                SteerName,
                Spring,
                ComponentTransform{});
            if (ConstraintComponentSettings* C = Def.Storage.ResolveConstraint(WheelMount))
            {
                C->Form               = PhysicsConstraintCategory::Hinge;
                C->Axis               = ConstraintAxis::AxisY;
                C->DriveMode          = ConstraintDriveMode::Steer;
                C->MinAngleDegrees    = -32.0f;
                C->MaxAngleDegrees    = 32.0f;
                C->NormalizedEditPose = 0.50f;
            }
        }

        const std::string TyreName = std::string("Wheel") + CornerSuffixes[Q];
        const ComponentInstanceToken Tyre = Def.Storage.AppendInstance(
            ComponentCategory::GeometryInstance,
            TyreName,
            WheelMount,
            ComponentTransform{ { 0.0f, 0.0f, 0.0f }, { 0.0f, (H.Z > 0.0f) ? 180.0f : 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
        if (GeometryComponentSettings* G = Def.Storage.ResolveGeometry(Tyre))
        {
            G->AssetIdentifier  = "ControlVehicleTyre";
            G->CollisionProfile = GeometryCollisionProfile::BlockAll;
        }

        const std::string RimName = TyreName + "Rim";
        const ComponentInstanceToken Rim = Def.Storage.AppendInstance(
            ComponentCategory::GeometryInstance,
            RimName,
            Tyre,
            ComponentTransform{});
        if (GeometryComponentSettings* G = Def.Storage.ResolveGeometry(Rim))
        {
            G->AssetIdentifier  = "ControlVehicleRim";
            G->CollisionProfile = GeometryCollisionProfile::NoCollision;
        }

        WheelTokens.push_back(Tyre);
    }

    // Headlights (SpotBeam)
    for (int Side = 0; Side < 2; ++Side)
    {
        const ComponentInstanceToken Lamp = Def.Storage.AppendInstance(
            ComponentCategory::LightInstance,
            Side == 0 ? "HeadlightL" : "HeadlightR",
            Body,
            ComponentTransform{ { 2.95f, 0.42f, Side == 0 ? -0.78f : 0.78f }, { 0.0f, 0.0f, -4.0f }, { 1.0f, 1.0f, 1.0f } });
        if (LightComponentSettings* L = Def.Storage.ResolveLight(Lamp))
        {
            L->Form                   = LightSourceCategory::SpotBeam;
            L->LinearColourRGB        = { 0.96f, 0.97f, 1.00f };
            L->IntensityCandela       = 30.0f;
            L->AttenuationRangeMeters = 30.0f;
            L->ConeAngleDegrees       = 28.0f;
        }
    }

    // RearWing Dock + RearWing
    const ComponentInstanceToken WingDock = Def.Storage.AppendInstance(
        ComponentCategory::ConstraintInstance,
        "RearWingConstraint",
        Body,
        ComponentTransform{ { -2.9822f, 0.8367f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (ConstraintComponentSettings* C = Def.Storage.ResolveConstraint(WingDock))
    {
        C->Form              = PhysicsConstraintCategory::Dock;
        C->DockKey           = "wing";
        C->SnapRadiusMeters  = 0.30f;
        C->BreakForceNewtons = 1200.0f;
        C->DockConnected     = true;
    }

    const ComponentInstanceToken Wing = Def.Storage.AppendInstance(
        ComponentCategory::GeometryInstance,
        "RearWing",
        WingDock,
        ComponentTransform{});
    if (GeometryComponentSettings* G = Def.Storage.ResolveGeometry(Wing))
    {
        G->AssetIdentifier  = "RearWing";
        G->CollisionProfile = GeometryCollisionProfile::BlockAll;
    }
    if (ComponentInstanceHeader* H = Def.Storage.ResolveHeader(Wing))
    {
        H->TagCsv = "wing";
    }

    // TowStrap Tether
    const ComponentInstanceToken TowStrap = Def.Storage.AppendInstance(
        ComponentCategory::ConstraintInstance,
        "TowStrap",
        Body,
        ComponentTransform{ { -3.10f, 0.10f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (ConstraintComponentSettings* C = Def.Storage.ResolveConstraint(TowStrap))
    {
        C->Form              = PhysicsConstraintCategory::Tether;
        C->RopeLengthMeters  = 0.32f;
        C->BreakForceNewtons = 1800.0f;
    }

    // Forward indicator, ChaseCamera & FrontBumper collision trigger
    const ComponentInstanceToken Forward = Def.Storage.AppendInstance(
        ComponentCategory::DirectionArrow,
        "Forward",
        Body,
        ComponentTransform{ { 3.20f, 0.20f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (DirectionArrowSettings* A = Def.Storage.ResolveArrow(Forward))
    {
        A->LengthMeters = 0.80f;
    }

    const ComponentInstanceToken ChaseCam = Def.Storage.AppendInstance(
        ComponentCategory::CameraInstance,
        "ChaseCamera",
        Origin,
        ComponentTransform{ { -7.20f, 2.40f, 0.0f }, { 0.0f, 0.0f, -12.0f }, { 1.0f, 1.0f, 1.0f } });
    if (CameraComponentSettings* Cam = Def.Storage.ResolveCamera(ChaseCam))
    {
        Cam->VerticalFovDegrees = 62.0f;
    }

    const ComponentInstanceToken Bumper = Def.Storage.AppendInstance(
        ComponentCategory::CollisionVolume,
        "FrontBumperTrigger",
        Body,
        ComponentTransform{ { 3.05f, 0.10f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (CollisionVolumeSettings* Col = Def.Storage.ResolveCollision(Bumper))
    {
        Col->Form               = CollisionVolumeCategory::Box;
        Col->HalfExtentsMeters  = { 0.18f, 0.28f, 1.05f };
        Col->TriggerOverlapOnly = true;
    }

    // Chassis Jolt RigidBody component (paired with baked CompoundHull from ControlledIntegrator.GeometrySpace)
    const ComponentInstanceToken ChassisBodyComp = Def.Storage.AppendInstance(
        ComponentCategory::RigidBodyInstance,
        "ChassisRigidBody",
        Body,
        ComponentTransform{});
    if (RigidBodyComponentSettings* Rb = Def.Storage.ResolveRigidBody(ChassisBodyComp))
    {
        Rb->MotionKind        = RigidBodyMotionKind::Dynamic;
        Rb->ExplicitMassKg    = 1300.0f;
        Rb->LinearDamping     = 0.01f;
        Rb->AngularDamping    = 0.08f;
        Rb->CollisionChannel  = 2u;
        Rb->CollideWithMask   = 0xFFFFFFFFu;
        Rb->PhysicsMaterialId = 0u;
        Rb->AllowSleep        = false;
    }

    // VehicleDynamics behaviour
    const ComponentInstanceToken Vehicle = Def.Storage.AppendInstance(
        ComponentCategory::VehicleDynamics,
        "VehicleMovement",
        {});
    if (VehicleDynamicsSettings* V = Def.Storage.ResolveVehicle(Vehicle))
    {
        V->ChassisMassKg     = 1300.0f;
        V->WheelRadiusMeters = ExtractedTyreRadius;
        V->IdleRpm           = 700.0f;
        V->RedlineRpm        = 7200.0f;
        V->DragCoefficient   = 0.34f;
        V->FrontalAreaM2     = 2.15f;
        V->AeroLiftAreaM2    = 1.10f;
        V->FinalDriveRatio   = 3.55f;
        V->GearRatios        = { 4.2f, 3.1f, 2.5f, 1.9f, 1.5f, 1.2f };
        V->Interpolation     = CurveInterpolationForm::SmoothSpline;
        V->BoundWheelTokens  = std::move(WheelTokens);
        V->TorqueCurve       = {
            {  800.0f, 234.0f }, { 1200.0f, 300.0f }, { 1600.0f, 352.0f }, { 2000.0f, 392.0f },
            { 2400.0f, 428.0f }, { 2800.0f, 460.0f }, { 3200.0f, 486.0f }, { 3600.0f, 504.0f },
            { 4000.0f, 514.0f }, { 4400.0f, 518.0f }, { 4800.0f, 520.0f }, { 5200.0f, 520.0f },
            { 5800.0f, 520.0f }, { 6200.0f, 506.0f }, { 6600.0f, 486.0f }, { 7000.0f, 462.0f },
            { 7600.0f, 448.0f }, { 8500.0f, 442.0f },
        };
    }

    Def.FinalizeCompiledRevision();
    return true;
}

bool ControlVehicleClassBuilder::ConstructControlVehicle(
    const std::filesystem::path& SourcePathOrDirectory,
    ControlVehicleBuildResult&   OutResult,
    std::string&                 OutDiagnostic)
{
    const std::filesystem::path GeometrySpacePath = ResolveControlledIntegratorPath(SourcePathOrDirectory);
    return ConstructFromControlledIntegrator(GeometrySpacePath, OutResult, OutDiagnostic);
}

} // namespace Frontier::ComponentClass
