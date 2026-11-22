/*============================================================================================================================================
                                                       APOCALYPTICCITYBUILDER.CPP
============================================================================================================================================*/
// 🧩 Procedural ruined city racing world generator: convex-fractured skyscrapers, multi-floor & skybridge ramps, wrecks, flora, deer & destruction

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/ApocalypticCityBuilder.h"

#include "Engine/Codecs/ControlVehicleSurfaceDecoder.h"
#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"
#include "Engine/GeometrySpace/Fracture/ConvexPartitioning.h"
#include "Engine/GeometrySpace/Fracture/FastRuntimeFracture.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/Tools/DefaultContentGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace Frontier::GeometricRaster
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                             DETERMINISTIC NOISE & 3D ROTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint32_t MixSeed(uint32_t X) noexcept
{
    X ^= X >> 16u;
    X *= 0x7FEB352Du;
    X ^= X >> 15u;
    X *= 0x846CA68Bu;
    X ^= X >> 16u;
    return X;
}

[[nodiscard]] float UnitRandom(uint32_t& RngSeed) noexcept
{
    RngSeed = MixSeed(RngSeed + 0x9E3779B9u);
    return static_cast<float>(RngSeed & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

[[nodiscard]] float SignedRandom(uint32_t& RngSeed) noexcept
{
    return UnitRandom(RngSeed) * 2.0f - 1.0f;
}

[[nodiscard]] float LatticeHash3D(int32_t Ix, int32_t Iy, int32_t Iz) noexcept
{
    uint32_t H = static_cast<uint32_t>(Ix) * 374761393u +
                 static_cast<uint32_t>(Iy) * 668265263u +
                 static_cast<uint32_t>(Iz) * 2147483647u;
    H = MixSeed(H);
    return static_cast<float>(H & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

[[nodiscard]] float SmoothHermite(float T) noexcept
{
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] float CoherentNoise3D(Mathematics::Axis3D SamplePt) noexcept
{
    const int32_t X0 = static_cast<int32_t>(std::floor(SamplePt.X));
    const int32_t Y0 = static_cast<int32_t>(std::floor(SamplePt.Y));
    const int32_t Z0 = static_cast<int32_t>(std::floor(SamplePt.Z));
    const float   Fx = SmoothHermite(SamplePt.X - static_cast<float>(X0));
    const float   Fy = SmoothHermite(SamplePt.Y - static_cast<float>(Y0));
    const float   Fz = SmoothHermite(SamplePt.Z - static_cast<float>(Z0));

    const float C000 = LatticeHash3D(X0,     Y0,     Z0);
    const float C100 = LatticeHash3D(X0 + 1, Y0,     Z0);
    const float C010 = LatticeHash3D(X0,     Y0 + 1, Z0);
    const float C110 = LatticeHash3D(X0 + 1, Y0 + 1, Z0);
    const float C001 = LatticeHash3D(X0,     Y0,     Z0 + 1);
    const float C101 = LatticeHash3D(X0 + 1, Y0,     Z0 + 1);
    const float C011 = LatticeHash3D(X0,     Y0 + 1, Z0 + 1);
    const float C111 = LatticeHash3D(X0 + 1, Y0 + 1, Z0 + 1);

    const float X00 = C000 + (C100 - C000) * Fx;
    const float X10 = C010 + (C110 - C010) * Fx;
    const float X01 = C001 + (C101 - C001) * Fx;
    const float X11 = C011 + (C111 - C011) * Fx;
    const float Y0Mix = X00 + (X10 - X00) * Fy;
    const float Y1Mix = X01 + (X11 - X01) * Fy;
    return Y0Mix + (Y1Mix - Y0Mix) * Fz;
}

[[nodiscard]] float FractalSeismicNoise(Mathematics::Axis3D Pt, float TimeSec) noexcept
{
    const Mathematics::Axis3D P0 { Pt.X * 0.16f + TimeSec * 0.65f, Pt.Y * 0.16f - TimeSec * 0.42f, Pt.Z * 0.14f + TimeSec * 0.30f };
    const Mathematics::Axis3D P1 { Pt.X * 0.37f - TimeSec * 1.15f, Pt.Y * 0.37f + TimeSec * 0.88f, Pt.Z * 0.31f };
    return 0.65f * CoherentNoise3D(P0) + 0.35f * CoherentNoise3D(P1);
}

[[nodiscard]] Mathematics::Axis3D RotateEulerRpy(
    Mathematics::Axis3D LocalPt,
    float               RollDegrees,
    float               PitchDegrees,
    float               YawDegrees) noexcept
{
    const float RollRad  = RollDegrees * Constants::DegToRadFloat;
    const float PitchRad = PitchDegrees * Constants::DegToRadFloat;
    const float YawRad   = YawDegrees * Constants::DegToRadFloat;

    const float Cr = std::cos(RollRad);
    const float Sr = std::sin(RollRad);
    const float Cp = std::cos(PitchRad);
    const float Sp = std::sin(PitchRad);
    const float Cy = std::cos(YawRad);
    const float Sy = std::sin(YawRad);

    // Roll around +X, Pitch around +Y, Yaw around +Z
    const Mathematics::Axis3D AfterRoll {
        LocalPt.X,
        LocalPt.Y * Cr - LocalPt.Z * Sr,
        LocalPt.Y * Sr + LocalPt.Z * Cr,
    };
    const Mathematics::Axis3D AfterPitch {
        AfterRoll.X * Cp + AfterRoll.Z * Sp,
        AfterRoll.Y,
        -AfterRoll.X * Sp + AfterRoll.Z * Cp,
    };
    return {
        AfterPitch.X * Cy - AfterPitch.Y * Sy,
        AfterPitch.X * Sy + AfterPitch.Y * Cy,
        AfterPitch.Z,
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                        LOCAL GEOMETRY & MATERIAL REGISTRATION HELPERS
//------------------------------------------------------------------------------------------------------------------------

void AppendOrientedBoxToNetwork(
    GeometrySpace::PolygonNetwork& TargetNet,
    Mathematics::Axis3D            LocalCenter,
    Mathematics::Scalar3D          HalfExtents,
    float                          RollDegrees,
    float                          PitchDegrees,
    float                          YawDegrees,
    uint16_t                       MaterialSlot)
{
    const float Hx = std::max(0.008f, HalfExtents.X);
    const float Hy = std::max(0.008f, HalfExtents.Y);
    const float Hz = std::max(0.008f, HalfExtents.Z);

    const std::array<Mathematics::Axis3D, 8u> RawCorners {{
        { -Hx, -Hy, -Hz },
        {  Hx, -Hy, -Hz },
        {  Hx,  Hy, -Hz },
        { -Hx,  Hy, -Hz },
        { -Hx, -Hy,  Hz },
        {  Hx, -Hy,  Hz },
        {  Hx,  Hy,  Hz },
        { -Hx,  Hy,  Hz },
    }};

    std::array<uint32_t, 8u> VtxIndices {};
    for (std::size_t I = 0u; I < 8u; ++I)
    {
        const Mathematics::Axis3D Rotated = RotateEulerRpy(RawCorners[I], RollDegrees, PitchDegrees, YawDegrees);
        VtxIndices[I] = TargetNet.AppendVertex(LocalCenter + Rotated);
    }

    static constexpr std::array<std::array<uint32_t, 4u>, 6u> QuadLoops {{
        {{ 0u, 3u, 2u, 1u }}, // -Z bottom
        {{ 4u, 5u, 6u, 7u }}, // +Z top
        {{ 0u, 1u, 5u, 4u }}, // -Y south
        {{ 2u, 3u, 7u, 6u }}, // +Y north
        {{ 1u, 2u, 6u, 5u }}, // +X east
        {{ 3u, 0u, 4u, 7u }}, // -X west
    }};

    for (const auto& Quad : QuadLoops)
    {
        const std::array<uint32_t, 4u> Loop {{
            VtxIndices[Quad[0u]],
            VtxIndices[Quad[1u]],
            VtxIndices[Quad[2u]],
            VtxIndices[Quad[3u]],
        }};
        (void)TargetNet.AppendFace(Loop, {}, {}, 0u, MaterialSlot);
    }
}

void AppendTaperedTubeSegment(
    GeometrySpace::PolygonNetwork& TargetNet,
    Mathematics::Axis3D            StartPt,
    Mathematics::Axis3D            EndPt,
    float                          RadiusStart,
    float                          RadiusEnd,
    uint32_t                       RadialSides,
    uint16_t                       MaterialSlot)
{
    const Mathematics::Axis3D AxisVec = EndPt - StartPt;
    const float Len = std::sqrt(Mathematics::EvaluateLengthSquared(AxisVec));
    if (Len <= 1.0e-4f || RadialSides < 3u)
    {
        return;
    }
    const Mathematics::Axis3D TangentZ = AxisVec * (1.0f / Len);
    const Mathematics::Axis3D UpHint   = (std::fabs(TangentZ.Z) < 0.92f)
        ? Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f }
        : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
    const Mathematics::Axis3D TangentX = Mathematics::NormalizeAxis(
        Mathematics::EvaluateCross(UpHint, TangentZ), { 1.0f, 0.0f, 0.0f });
    const Mathematics::Axis3D TangentY = Mathematics::NormalizeAxis(
        Mathematics::EvaluateCross(TangentZ, TangentX), { 0.0f, 1.0f, 0.0f });

    std::vector<uint32_t> Ring0(RadialSides);
    std::vector<uint32_t> Ring1(RadialSides);
    const float R0 = std::max(0.004f, RadiusStart);
    const float R1 = std::max(0.003f, RadiusEnd);

    for (uint32_t S = 0u; S < RadialSides; ++S)
    {
        const float Angle = (static_cast<float>(S) / static_cast<float>(RadialSides)) * (2.0f * Constants::PiFloat);
        const float Ca    = std::cos(Angle);
        const float Sa    = std::sin(Angle);
        const Mathematics::Axis3D RadialDir = TangentX * Ca + TangentY * Sa;
        Ring0[S] = TargetNet.AppendVertex(StartPt + RadialDir * R0);
        Ring1[S] = TargetNet.AppendVertex(EndPt + RadialDir * R1);
    }

    for (uint32_t S = 0u; S < RadialSides; ++S)
    {
        const uint32_t NextS = (S + 1u) % RadialSides;
        const std::array<uint32_t, 4u> SideLoop {{ Ring0[S], Ring0[NextS], Ring1[NextS], Ring1[S] }};
        (void)TargetNet.AppendFace(SideLoop, {}, {}, 0u, MaterialSlot);
    }

    // End caps (triangle fan)
    const uint32_t CapStartCenter = TargetNet.AppendVertex(StartPt);
    const uint32_t CapEndCenter   = TargetNet.AppendVertex(EndPt);
    for (uint32_t S = 0u; S < RadialSides; ++S)
    {
        const uint32_t NextS = (S + 1u) % RadialSides;
        const std::array<uint32_t, 3u> StartTri {{ CapStartCenter, Ring0[NextS], Ring0[S] }};
        const std::array<uint32_t, 3u> EndTri   {{ CapEndCenter, Ring1[S], Ring1[NextS] }};
        (void)TargetNet.AppendFace(StartTri, {}, {}, 0u, MaterialSlot);
        (void)TargetNet.AppendFace(EndTri, {}, {}, 0u, MaterialSlot);
    }
}

void PopulateCenteredAssetEntry(
    ShowcaseGeometryAsset&   InOutGeom,
    ShowcaseWorldAssetEntry& InOutEntry,
    Mathematics::Axis3D      DesiredWorldOffset,
    uint32_t                 MaxPyramidLevels = 3u)
{
    if (InOutGeom.Network.Vertices.empty())
    {
        return;
    }

    Mathematics::Axis3D BMin = InOutGeom.Network.Vertices.front().Axes;
    Mathematics::Axis3D BMax = BMin;
    for (const GeometrySpace::Vertex& V : InOutGeom.Network.Vertices)
    {
        BMin.X = std::min(BMin.X, V.Axes.X);
        BMin.Y = std::min(BMin.Y, V.Axes.Y);
        BMin.Z = std::min(BMin.Z, V.Axes.Z);
        BMax.X = std::max(BMax.X, V.Axes.X);
        BMax.Y = std::max(BMax.Y, V.Axes.Y);
        BMax.Z = std::max(BMax.Z, V.Axes.Z);
    }

    const Mathematics::Axis3D LocalMid {
        0.5f * (BMin.X + BMax.X),
        0.5f * (BMin.Y + BMax.Y),
        0.5f * (BMin.Z + BMax.Z),
    };
    for (GeometrySpace::Vertex& V : InOutGeom.Network.Vertices)
    {
        V.Axes = V.Axes - LocalMid;
    }
    InOutGeom.Network.RecomputeNormals();
    (void)InOutGeom.Pyramid.BuildFromPolygonNetwork(InOutGeom.Network, MaxPyramidLevels);

    InOutEntry.CenterWorld = DesiredWorldOffset + LocalMid;
    InOutEntry.DimensionsMeters = {
        std::max(0.06f, BMax.X - BMin.X),
        std::max(0.06f, BMax.Y - BMin.Y),
        std::max(0.06f, BMax.Z - BMin.Z),
    };
    InOutEntry.AuthoredDimensions = InOutEntry.DimensionsMeters;

    InOutEntry.LocalSmoothTriangles.clear();
    InOutEntry.LocalSmoothTriangles.reserve(InOutGeom.Network.Faces.size() * 2u);
    for (const GeometrySpace::Face& Face : InOutGeom.Network.Faces)
    {
        if (Face.CornerCount < 3u || Face.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex)
        {
            continue;
        }
        std::vector<uint32_t> LoopVerts;
        LoopVerts.reserve(Face.CornerCount);
        uint32_t EdgeCursor = Face.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < Face.CornerCount && EdgeCursor < InOutGeom.Network.Edges.size(); ++Step)
        {
            LoopVerts.push_back(InOutGeom.Network.Edges[EdgeCursor].OriginVertexIndex);
            EdgeCursor = InOutGeom.Network.Edges[EdgeCursor].NextEdgeIndex;
        }
        if (LoopVerts.size() < 3u)
        {
            continue;
        }
        const Mathematics::Axis3D P0 = InOutGeom.Network.Vertices[LoopVerts[0u]].Axes;
        for (std::size_t Fan = 1u; Fan + 1u < LoopVerts.size(); ++Fan)
        {
            const Mathematics::Axis3D P1 = InOutGeom.Network.Vertices[LoopVerts[Fan]].Axes;
            const Mathematics::Axis3D P2 = InOutGeom.Network.Vertices[LoopVerts[Fan + 1u]].Axes;
            const Mathematics::Axis3D CrossN = Mathematics::EvaluateCross(P1 - P0, P2 - P0);
            if (Mathematics::EvaluateLengthSquared(CrossN) <= 1.0e-12f)
            {
                continue;
            }
            const Mathematics::Axis3D UnitN = Mathematics::NormalizeAxis(CrossN, { 0.0f, 0.0f, 1.0f });
            CachedLocalSmoothTriangle Tri {};
            Tri.Corner0              = P0;
            Tri.Corner1              = P1;
            Tri.Corner2              = P2;
            Tri.Normal0              = UnitN;
            Tri.Normal1              = UnitN;
            Tri.Normal2              = UnitN;
            Tri.MaterialSlotOverride = Face.PartGroupOrdinal;
            InOutEntry.LocalSmoothTriangles.push_back(Tri);
        }
    }
}

uint32_t RegisterDynamicAssetInBundle(
    ShowcaseWorldSceneBundle&              InOutBundle,
    std::unique_ptr<ShowcaseGeometryAsset> GeomAsset,
    ShowcaseWorldAssetEntry                Entry,
    uint32_t                               AnalyticalShapeCode = 1u,
    bool                                   InitiallyVisible    = true)
{
    const uint32_t ClusterIdx  = static_cast<uint32_t>(InOutBundle.Scene.ClusterInstances.size());
    Entry.ClusterInstanceIndex = ClusterIdx;

    RigidClusterInstance Inst {};
    Inst.PyramidAsset        = &GeomAsset->Pyramid;
    Inst.WorldTranslation    = Entry.CenterWorld;
    Inst.PreviousTranslation = Entry.CenterWorld;
    Inst.UniformScale        = InitiallyVisible ? 1.0f : 0.0f;
    Inst.MaterialSlot        = Entry.MaterialSlot;
    Inst.TransparentGlass    = Entry.GlassShell;
    Inst.RotationZDegrees    = Entry.RotationZDegrees;
    Inst.NonUniformScale     = { 1.0f, 1.0f, 1.0f };
    Inst.LocalHalfExtents    = {
        std::max(0.03f, Entry.AuthoredDimensions.X * 0.5f),
        std::max(0.03f, Entry.AuthoredDimensions.Y * 0.5f),
        std::max(0.03f, Entry.AuthoredDimensions.Z * 0.5f),
    };
    const bool LocalSdfPresent = !GeomAsset->LocalSdf.HalfDistanceSamples.empty() || !GeomAsset->LocalSdf.DistanceSamples.empty();
    Inst.AnalyticalShapeCode   = LocalSdfPresent ? 0u : AnalyticalShapeCode;
    Inst.LocalSdfVolume        = LocalSdfPresent ? &GeomAsset->LocalSdf : nullptr;

    InOutBundle.Scene.ClusterInstances.push_back(Inst);
    InOutBundle.DynamicGeometryAssets.push_back(std::move(GeomAsset));
    const uint32_t AssetSlotIdx = static_cast<uint32_t>(InOutBundle.Objects.size());
    InOutBundle.Objects.push_back(std::move(Entry));
    return AssetSlotIdx;
}

void AppendPolyhedronToNetworkWithMaterials(
    const GeometrySpace::Fracture::FracturePieceEntry& Piece,
    GeometrySpace::PolygonNetwork&                     TargetNet,
    Mathematics::Axis3D                                VertexTranslation,
    float                                              RollDeg,
    float                                              PitchDeg,
    float                                              YawDeg,
    uint16_t                                           ExteriorSlot,
    uint16_t                                           InteriorCutSlot)
{
    const auto& Polyhedron = Piece.Polyhedron;
    if (Polyhedron.Vertices.size() < 4u || Polyhedron.Faces.empty())
    {
        return;
    }
    std::vector<uint32_t> MappedVerts(Polyhedron.Vertices.size());
    for (std::size_t V = 0u; V < Polyhedron.Vertices.size(); ++V)
    {
        const Mathematics::Axis3D LocalV = Polyhedron.Vertices[V] - Piece.Centroid;
        const Mathematics::Axis3D RotV   = RotateEulerRpy(LocalV, RollDeg, PitchDeg, YawDeg);
        MappedVerts[V] = TargetNet.AppendVertex(Piece.Centroid + RotV + VertexTranslation);
    }

    for (const GeometrySpace::Fracture::ConvexFace& PFace : Polyhedron.Faces)
    {
        if (PFace.Indices.size() < 3u)
        {
            continue;
        }
        std::vector<uint32_t> Loop(PFace.Indices.size());
        for (std::size_t K = 0u; K < PFace.Indices.size(); ++K)
        {
            Loop[K] = MappedVerts[PFace.Indices[K]];
        }
        const uint16_t FaceMat = PFace.Interior ? InteriorCutSlot : ExteriorSlot;
        (void)TargetNet.AppendFace(Loop, {}, {}, 0u, FaceMat);
    }
}

[[nodiscard]] GeometrySpace::Fracture::FracturePartitionResult FractureWorldBoxDomain(
    Mathematics::Axis3D BoundsMin,
    Mathematics::Axis3D BoundsMax,
    Mathematics::Axis3D ImpactWorld,
    uint32_t            TargetPieces,
    uint32_t            Seed,
    const char*         MaterialIdentifier,
    double              EnergyJoules = 6500.0)
{
    const Mathematics::Axis3D Span = BoundsMax - BoundsMin;
    const Mathematics::Axis3D Mid  = (BoundsMin + BoundsMax) * 0.5f;
    GeometrySpace::Fracture::ConvexPolyhedron SourceBox =
        GeometrySpace::Fracture::ConvexPartitioning::ConstructBoxPolyhedron(
            std::max(0.08f, Span.X),
            std::max(0.08f, Span.Y),
            std::max(0.08f, Span.Z));
    for (Mathematics::Axis3D& V : SourceBox.Vertices)
    {
        V = V + Mid;
    }

    GeometrySpace::Fracture::FractureSpecification Spec {};
    Spec.Solver       = GeometrySpace::Fracture::FractureSolverMethod::FastRuntimeBoussinesq;
    Spec.MaterialId   = MaterialIdentifier;
    Spec.Seed         = Seed;
    Spec.Ceiling      = TargetPieces;
    Spec.EnergyJoules = EnergyJoules;
    Spec.MinimumSizeM = 0.04;
    Spec.ImpactPoint  = ImpactWorld;
    Spec.ImpactNormal = { 0.0f, -1.0f, 0.0f };
    return GeometrySpace::Fracture::FastRuntimeFracture::Execute(SourceBox, Spec);
}

//------------------------------------------------------------------------------------------------------------------------
//                                        WOODLAND DEER & FLORA PROCEDURAL SCULPTING
//------------------------------------------------------------------------------------------------------------------------

void SculptWoodlandDeerNetwork(
    GeometrySpace::PolygonNetwork& OutNet,
    uint16_t                       CoatMatSlot,
    uint16_t                       UnderbellyMatSlot,
    uint16_t                       AntlerMatSlot,
    uint16_t                       HoofMatSlot,
    bool                           StagWithAntlers)
{
    OutNet.Reset();
    OutNet.NetworkTitle = StagWithAntlers ? "WoodlandStagDeer" : "WoodlandHindDeer";

    // Forward travel axis = +X, lateral = +Y, vertical = +Z (feet at Z = -0.72, back at Z = +0.26, antlers to +0.86)
    // Torso barrel + chest + hindquarters
    AppendTaperedTubeSegment(OutNet, { -0.46f, 0.0f, 0.08f }, { 0.38f, 0.0f, 0.14f }, 0.21f, 0.24f, 10u, CoatMatSlot);
    AppendOrientedBoxToNetwork(OutNet, { -0.04f, 0.0f, -0.05f }, { 0.36f, 0.14f, 0.07f }, 0.0f, -3.0f, 0.0f, UnderbellyMatSlot);
    // White-tail rump tuft
    AppendTaperedTubeSegment(OutNet, { -0.46f, 0.0f, 0.14f }, { -0.60f, 0.0f, 0.23f }, 0.07f, 0.03f, 6u, UnderbellyMatSlot);

    // Graceful neck + head + tapered muzzle + ears
    AppendTaperedTubeSegment(OutNet, { 0.34f, 0.0f, 0.18f }, { 0.58f, 0.0f, 0.56f }, 0.13f, 0.085f, 8u, CoatMatSlot);
    AppendTaperedTubeSegment(OutNet, { 0.54f, 0.0f, 0.57f }, { 0.80f, 0.0f, 0.50f }, 0.095f, 0.048f, 8u, CoatMatSlot);
    AppendTaperedTubeSegment(OutNet, { 0.78f, 0.0f, 0.50f }, { 0.85f, 0.0f, 0.49f }, 0.042f, 0.025f, 6u, HoofMatSlot);
    // Left & right alert ears
    AppendTaperedTubeSegment(OutNet, { 0.55f,  0.06f, 0.62f }, { 0.48f,  0.17f, 0.75f }, 0.032f, 0.014f, 5u, CoatMatSlot);
    AppendTaperedTubeSegment(OutNet, { 0.55f, -0.06f, 0.62f }, { 0.48f, -0.17f, 0.75f }, 0.032f, 0.014f, 5u, CoatMatSlot);

    // Four slender articulated legs + cloven dark hooves
    struct LegSpec { float TopX; float TopY; float KneeX; float FootX; };
    static constexpr std::array<LegSpec, 4u> Legs {{
        {  0.30f,  0.12f,  0.34f,  0.32f },
        {  0.30f, -0.12f,  0.27f,  0.29f },
        { -0.36f,  0.13f, -0.43f, -0.37f },
        { -0.36f, -0.13f, -0.39f, -0.34f },
    }};
    for (const LegSpec& L : Legs)
    {
        AppendTaperedTubeSegment(OutNet, { L.TopX,  L.TopY,  0.04f }, { L.KneeX, L.TopY, -0.34f }, 0.068f, 0.040f, 6u, CoatMatSlot);
        AppendTaperedTubeSegment(OutNet, { L.KneeX, L.TopY, -0.34f }, { L.FootX, L.TopY, -0.66f }, 0.038f, 0.026f, 6u, CoatMatSlot);
        AppendTaperedTubeSegment(OutNet, { L.FootX, L.TopY, -0.66f }, { L.FootX + 0.02f, L.TopY, -0.72f }, 0.030f, 0.032f, 6u, HoofMatSlot);
    }

    if (StagWithAntlers)
    {
        for (float Side : { 1.0f, -1.0f })
        {
            // Main sweeping antler beam + brow tine + royal tine + crown fork
            const Mathematics::Axis3D SkullRoot { 0.58f, 0.045f * Side, 0.64f };
            const Mathematics::Axis3D MidBeam   { 0.50f, 0.19f  * Side, 0.84f };
            const Mathematics::Axis3D UpperBeam { 0.57f, 0.26f  * Side, 1.04f };
            const Mathematics::Axis3D CrownTipA { 0.66f, 0.24f  * Side, 1.18f };
            const Mathematics::Axis3D CrownTipB { 0.49f, 0.31f  * Side, 1.16f };
            const Mathematics::Axis3D BrowTine  { 0.68f, 0.14f  * Side, 0.82f };
            const Mathematics::Axis3D RoyalTine { 0.67f, 0.22f  * Side, 0.99f };

            AppendTaperedTubeSegment(OutNet, SkullRoot, MidBeam,   0.024f, 0.018f, 5u, AntlerMatSlot);
            AppendTaperedTubeSegment(OutNet, MidBeam,   UpperBeam, 0.018f, 0.014f, 5u, AntlerMatSlot);
            AppendTaperedTubeSegment(OutNet, UpperBeam, CrownTipA, 0.013f, 0.006f, 5u, AntlerMatSlot);
            AppendTaperedTubeSegment(OutNet, UpperBeam, CrownTipB, 0.012f, 0.006f, 5u, AntlerMatSlot);
            AppendTaperedTubeSegment(OutNet, SkullRoot * 0.4f + MidBeam * 0.6f, BrowTine, 0.014f, 0.006f, 5u, AntlerMatSlot);
            AppendTaperedTubeSegment(OutNet, MidBeam * 0.35f + UpperBeam * 0.65f, RoyalTine, 0.013f, 0.006f, 5u, AntlerMatSlot);
        }
    }
}

void AppendGrassClumpToNetwork(
    GeometrySpace::PolygonNetwork& TargetNet,
    Mathematics::Axis3D            RootWorld,
    float                          Scale,
    uint32_t&                      RngSeed,
    uint16_t                       LushGrassSlot,
    uint16_t                       DryGrassSlot)
{
    const uint32_t BladeCount = 14u;
    for (uint32_t B = 0u; B < BladeCount; ++B)
    {
        const float Azimuth   = UnitRandom(RngSeed) * (2.0f * Constants::PiFloat);
        const float RadialOff = UnitRandom(RngSeed) * 0.38f * Scale;
        const float BladeH    = (0.42f + 0.52f * UnitRandom(RngSeed)) * Scale;
        const float BendDist  = (0.16f + 0.28f * UnitRandom(RngSeed)) * Scale;
        const float HalfW     = (0.026f + 0.018f * UnitRandom(RngSeed)) * Scale;
        const uint16_t Slot   = (UnitRandom(RngSeed) < 0.72f) ? LushGrassSlot : DryGrassSlot;

        const float Ca = std::cos(Azimuth);
        const float Sa = std::sin(Azimuth);
        const Mathematics::Axis3D DirXY  { Ca, Sa, 0.0f };
        const Mathematics::Axis3D SideXY { -Sa * HalfW, Ca * HalfW, 0.0f };
        const Mathematics::Axis3D RootPt = RootWorld + DirXY * RadialOff;
        const Mathematics::Axis3D MidPt  = RootPt + DirXY * (BendDist * 0.42f) + Mathematics::Axis3D{ 0.0f, 0.0f, BladeH * 0.56f };
        const Mathematics::Axis3D TipPt  = RootPt + DirXY * BendDist + Mathematics::Axis3D{ 0.0f, 0.0f, BladeH };

        const uint32_t V0 = TargetNet.AppendVertex(RootPt - SideXY);
        const uint32_t V1 = TargetNet.AppendVertex(RootPt + SideXY);
        const uint32_t V2 = TargetNet.AppendVertex(MidPt + SideXY * 0.65f);
        const uint32_t V3 = TargetNet.AppendVertex(MidPt - SideXY * 0.65f);
        const uint32_t V4 = TargetNet.AppendVertex(TipPt);

        // Double-sided botanical blade so sunlight & GI illuminate both sides cleanly
        const std::array<uint32_t, 4u> LowerFront {{ V0, V1, V2, V3 }};
        const std::array<uint32_t, 4u> LowerBack  {{ V1, V0, V3, V2 }};
        const std::array<uint32_t, 3u> UpperFront {{ V3, V2, V4 }};
        const std::array<uint32_t, 3u> UpperBack  {{ V2, V3, V4 }};
        (void)TargetNet.AppendFace(LowerFront, {}, {}, 0u, Slot);
        (void)TargetNet.AppendFace(LowerBack,  {}, {}, 0u, Slot);
        (void)TargetNet.AppendFace(UpperFront, {}, {}, 0u, Slot);
        (void)TargetNet.AppendFace(UpperBack,  {}, {}, 0u, Slot);
    }
}

void AppendHangingVineCurtainToNetwork(
    GeometrySpace::PolygonNetwork& TargetNet,
    Mathematics::Axis3D            LedgeStart,
    Mathematics::Axis3D            LedgeEnd,
    float                          MaxDropMeters,
    uint32_t                       StrandCount,
    uint32_t&                      RngSeed,
    uint16_t                       VineStemSlot,
    uint16_t                       VineLeafSlot)
{
    for (uint32_t S = 0u; S < StrandCount; ++S)
    {
        const float T = (StrandCount > 1u) ? (static_cast<float>(S) / static_cast<float>(StrandCount - 1u)) : 0.5f;
        const Mathematics::Axis3D Anchor = LedgeStart + (LedgeEnd - LedgeStart) * T;
        const float DropLen = MaxDropMeters * (0.45f + 0.55f * UnitRandom(RngSeed));
        const uint32_t Segments = 5u;
        Mathematics::Axis3D PrevPt = Anchor;

        for (uint32_t Seg = 1u; Seg <= Segments; ++Seg)
        {
            const float Frac = static_cast<float>(Seg) / static_cast<float>(Segments);
            const Mathematics::Axis3D NextPt {
                Anchor.X + 0.14f * std::sin(Frac * 4.2f + static_cast<float>(S)) + 0.05f * SignedRandom(RngSeed),
                Anchor.Y + 0.14f * std::cos(Frac * 3.7f + static_cast<float>(S) * 1.3f) + 0.05f * SignedRandom(RngSeed),
                Anchor.Z - DropLen * Frac,
            };
            AppendTaperedTubeSegment(TargetNet, PrevPt, NextPt, 0.032f * (1.1f - 0.5f * Frac), 0.022f * (1.1f - 0.5f * Frac), 4u, VineStemSlot);

            // Broad ivy leaf clusters along hanging tendril
            for (uint32_t Lf = 0u; Lf < 2u; ++Lf)
            {
                const float LeafYaw  = UnitRandom(RngSeed) * 360.0f;
                const float LeafRoll = 25.0f + 35.0f * SignedRandom(RngSeed);
                AppendOrientedBoxToNetwork(
                    TargetNet,
                    (PrevPt + NextPt) * 0.5f + Mathematics::Axis3D{ 0.06f * SignedRandom(RngSeed), 0.06f * SignedRandom(RngSeed), 0.0f },
                    { 0.13f, 0.09f, 0.012f },
                    LeafRoll,
                    18.0f * SignedRandom(RngSeed),
                    LeafYaw,
                    VineLeafSlot);
            }
            PrevPt = NextPt;
        }
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           RUINED CITY WORLD LEVEL CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

void BuildApocalypticCityRacingScene(
    ShowcaseWorldSceneBundle&          OutBundle,
    ApocalypticCitySimulationRegister& OutSimulation,
    uint32_t                           ViewportWidthPx,
    uint32_t                           ViewportHeightPx,
    uint32_t                           RandomSeed)
{
    OutBundle     = {};
    OutSimulation = {};
    OutSimulation.ActiveCityLevel      = true;
    OutSimulation.HeroAutoDriveEnabled = true;

    uint32_t Rng = (RandomSeed != 0u) ? RandomSeed : 20261122u;

    std::filesystem::path RepoRoot = std::filesystem::current_path();
    for (uint32_t Depth = 0u; Depth < 6u; ++Depth)
    {
        if (std::filesystem::exists(RepoRoot / "EngineContent" / "EnvironmentArchives" / "ControlVehicle" / "ControlVehicleBody.face") ||
            std::filesystem::exists(RepoRoot / "CMakeLists.txt"))
        {
            break;
        }
        if (!RepoRoot.has_parent_path() || RepoRoot.parent_path() == RepoRoot)
        {
            break;
        }
        RepoRoot = RepoRoot.parent_path();
    }
    if (std::filesystem::exists(RepoRoot / "EngineContent" / "EnvironmentArchives" / "ControlVehicle" / "ControlVehicleBody.face"))
    {
        std::string GenDiag {};
        (void)Tools::DefaultContentGenerator::EnsureDefaultContentAvailable(RepoRoot, GenDiag);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 1. Physical OpenPBR Material Register (Asphalt, Concrete, Aggregate, Rebar, Glass, Flora, Deer, Vehicles)
    //--------------------------------------------------------------------------------------------------------------------
    auto RegisterMat = [&](const MaterialSpace::SurfaceMaterial& M) -> uint16_t
    {
        return OutBundle.Scene.MaterialRegister.RegisterMaterial(M);
    };

    MaterialSpace::SurfaceMaterial WetAsphalt {};
    WetAsphalt.MaterialName            = "FracturedBoulevardAsphalt";
    WetAsphalt.Diffuse.BaseColor       = { 0.11f, 0.115f, 0.125f };
    WetAsphalt.Specular.Roughness      = 0.48f;
    WetAsphalt.Specular.DielectricIOR  = 1.52f;
    WetAsphalt.Clearcoat.CoatWeight    = 0.14f;
    WetAsphalt.Clearcoat.CoatRoughness = 0.24f;
    const uint16_t MatAsphalt = RegisterMat(WetAsphalt);

    MaterialSpace::SurfaceMaterial SubgradeStone {};
    SubgradeStone.MaterialName       = "CraterSubgradeCrushedStone";
    SubgradeStone.Diffuse.BaseColor  = { 0.26f, 0.23f, 0.19f };
    SubgradeStone.Specular.Roughness = 0.88f;
    const uint16_t MatSubgrade = RegisterMat(SubgradeStone);

    MaterialSpace::SurfaceMaterial LanePaint {};
    LanePaint.MaterialName       = "WeatheredYellowHighwayStripe";
    LanePaint.Diffuse.BaseColor  = { 0.68f, 0.50f, 0.11f };
    LanePaint.Specular.Roughness = 0.62f;
    const uint16_t MatLaneStripe = RegisterMat(LanePaint);

    MaterialSpace::SurfaceMaterial FacadeConcrete {};
    FacadeConcrete.MaterialName       = "WeatheredArchitecturalConcrete";
    FacadeConcrete.Diffuse.BaseColor  = { 0.44f, 0.43f, 0.40f };
    FacadeConcrete.Specular.Roughness = 0.76f;
    const uint16_t MatConcreteExt = RegisterMat(FacadeConcrete);

    MaterialSpace::SurfaceMaterial WarmSandstoneFacade {};
    WarmSandstoneFacade.MaterialName       = "LimestoneCladdingFacade";
    WarmSandstoneFacade.Diffuse.BaseColor  = { 0.52f, 0.47f, 0.39f };
    WarmSandstoneFacade.Specular.Roughness = 0.72f;
    const uint16_t MatWarmFacade = RegisterMat(WarmSandstoneFacade);

    MaterialSpace::SurfaceMaterial FracturedAggregate {};
    FracturedAggregate.MaterialName       = "FracturedConcreteAggregateInterior";
    FracturedAggregate.Diffuse.BaseColor  = { 0.31f, 0.29f, 0.27f };
    FracturedAggregate.Specular.Roughness = 0.92f;
    const uint16_t MatConcreteCore = RegisterMat(FracturedAggregate);

    MaterialSpace::SurfaceMaterial OxidizedGirder {};
    OxidizedGirder.MaterialName         = "OxidizedStructuralSteelGirder";
    OxidizedGirder.Diffuse.BaseColor    = { 0.36f, 0.16f, 0.09f };
    OxidizedGirder.Specular.Metalness   = 0.68f;
    OxidizedGirder.Specular.Roughness   = 0.54f;
    OxidizedGirder.Specular.EdgeTintF82 = { 0.64f, 0.42f, 0.28f };
    const uint16_t MatRustSteel = RegisterMat(OxidizedGirder);

    MaterialSpace::SurfaceMaterial MasonryBrick {};
    MasonryBrick.MaterialName       = "WeatheredCinderMasonry";
    MasonryBrick.Diffuse.BaseColor  = { 0.46f, 0.24f, 0.17f };
    MasonryBrick.Specular.Roughness = 0.84f;
    const uint16_t MatMasonry = RegisterMat(MasonryBrick);

    MaterialSpace::SurfaceMaterial CurtainGlass {};
    CurtainGlass.MaterialName            = "ArchitecturalCurtainGlass";
    CurtainGlass.Diffuse.BaseColor       = { 0.16f, 0.42f, 0.52f };
    CurtainGlass.Specular.Metalness      = 0.24f;
    CurtainGlass.Specular.Roughness      = 0.03f;
    CurtainGlass.Specular.DielectricIOR  = 1.54f;
    CurtainGlass.Clearcoat.CoatWeight    = 1.00f;
    CurtainGlass.Clearcoat.CoatRoughness = 0.015f;
    const uint16_t MatCurtainGlass = RegisterMat(CurtainGlass);

    MaterialSpace::SurfaceMaterial LushGrass {};
    LushGrass.MaterialName                       = "OvergrownMeadowChlorophyll";
    LushGrass.Diffuse.BaseColor                  = { 0.14f, 0.36f, 0.09f };
    LushGrass.Specular.Roughness                 = 0.46f;
    LushGrass.Subsurface.SubsurfaceWeight        = 0.42f;
    LushGrass.Subsurface.SubsurfaceColor         = { 0.24f, 0.58f, 0.14f };
    LushGrass.Subsurface.MeanFreePathMillimeters = { 2.8f, 5.2f, 1.4f };
    LushGrass.Subsurface.PhaseAnisotropyG        = 0.35f;
    const uint16_t MatLushGrass = RegisterMat(LushGrass);

    MaterialSpace::SurfaceMaterial GoldenGrass {};
    GoldenGrass.MaterialName                       = "SunBleachedRyeGrass";
    GoldenGrass.Diffuse.BaseColor                  = { 0.42f, 0.36f, 0.14f };
    GoldenGrass.Specular.Roughness                 = 0.58f;
    GoldenGrass.Subsurface.SubsurfaceWeight        = 0.30f;
    GoldenGrass.Subsurface.SubsurfaceColor         = { 0.55f, 0.46f, 0.18f };
    const uint16_t MatDryGrass = RegisterMat(GoldenGrass);

    MaterialSpace::SurfaceMaterial VineStem {};
    VineStem.MaterialName       = "WoodyIvyVineTendril";
    VineStem.Diffuse.BaseColor  = { 0.18f, 0.15f, 0.09f };
    VineStem.Specular.Roughness = 0.78f;
    const uint16_t MatVineStem = RegisterMat(VineStem);

    MaterialSpace::SurfaceMaterial VineLeaf {};
    VineLeaf.MaterialName                       = "ClimbingIvyFoliage";
    VineLeaf.Diffuse.BaseColor                  = { 0.09f, 0.28f, 0.08f };
    VineLeaf.Specular.Roughness                 = 0.38f;
    VineLeaf.Subsurface.SubsurfaceWeight        = 0.46f;
    VineLeaf.Subsurface.SubsurfaceColor         = { 0.18f, 0.50f, 0.13f };
    VineLeaf.Subsurface.MeanFreePathMillimeters = { 2.4f, 4.8f, 1.2f };
    const uint16_t MatVineLeaf = RegisterMat(VineLeaf);

    MaterialSpace::SurfaceMaterial DeerCoat {};
    DeerCoat.MaterialName         = "WoodlandDeerRussetCoat";
    DeerCoat.Diffuse.BaseColor    = { 0.38f, 0.21f, 0.10f };
    DeerCoat.Specular.Roughness   = 0.74f;
    DeerCoat.Sheen.SheenWeight    = 0.42f;
    DeerCoat.Sheen.SheenRoughness = 0.52f;
    DeerCoat.Sheen.SheenColor     = { 0.68f, 0.48f, 0.30f };
    const uint16_t MatDeerCoat = RegisterMat(DeerCoat);

    MaterialSpace::SurfaceMaterial DeerBelly {};
    DeerBelly.MaterialName         = "WoodlandDeerCreamPelt";
    DeerBelly.Diffuse.BaseColor    = { 0.72f, 0.66f, 0.56f };
    DeerBelly.Specular.Roughness   = 0.76f;
    DeerBelly.Sheen.SheenWeight    = 0.35f;
    DeerBelly.Sheen.SheenRoughness = 0.55f;
    const uint16_t MatDeerBelly = RegisterMat(DeerBelly);

    MaterialSpace::SurfaceMaterial DeerAntler {};
    DeerAntler.MaterialName       = "KeratinAntlerIvory";
    DeerAntler.Diffuse.BaseColor  = { 0.66f, 0.59f, 0.46f };
    DeerAntler.Specular.Roughness = 0.44f;
    const uint16_t MatDeerAntler = RegisterMat(DeerAntler);

    MaterialSpace::SurfaceMaterial DeerHoof {};
    DeerHoof.MaterialName       = "ObsidianClovenHoof";
    DeerHoof.Diffuse.BaseColor  = { 0.08f, 0.07f, 0.07f };
    DeerHoof.Specular.Roughness = 0.36f;
    const uint16_t MatDeerHoof = RegisterMat(DeerHoof);

    // Hero ControlVehicle 11-slot OpenPBR materials + Wrecked Oxidized Car materials
    std::array<uint16_t, static_cast<std::size_t>(Codecs::VehicleSurfaceSlot::Count)> HeroVehicleSlots {};
    std::array<uint16_t, static_cast<std::size_t>(Codecs::VehicleSurfaceSlot::Count)> WreckVehicleSlots {};
    for (std::size_t R = 0u; R < HeroVehicleSlots.size(); ++R)
    {
        const auto SlotEnum = static_cast<Codecs::VehicleSurfaceSlot>(R);
        MaterialSpace::SurfaceMaterial HeroM {};
        MaterialSpace::SurfaceMaterial WreckM {};
        switch (SlotEnum)
        {
            case Codecs::VehicleSurfaceSlot::BodyCoat:
                HeroM.MaterialName            = "InterceptorCrimsonMetallic";
                HeroM.Diffuse.BaseColor       = { 0.66f, 0.07f, 0.05f };
                HeroM.Specular.Metalness      = 0.76f;
                HeroM.Specular.Roughness      = 0.16f;
                HeroM.Specular.EdgeTintF82    = { 0.96f, 0.55f, 0.42f };
                HeroM.Clearcoat.CoatWeight    = 1.0f;
                HeroM.Clearcoat.CoatRoughness = 0.035f;

                WreckM.MaterialName           = "ScorchedOxidizedChassis";
                WreckM.Diffuse.BaseColor      = { 0.27f, 0.14f, 0.09f };
                WreckM.Specular.Metalness     = 0.38f;
                WreckM.Specular.Roughness     = 0.78f;
                break;
            case Codecs::VehicleSurfaceSlot::VehicleGlass:
            case Codecs::VehicleSurfaceSlot::CanopyGloss:
                HeroM.MaterialName            = "ArmoredCanopyGlass";
                HeroM.Diffuse.BaseColor       = { 0.04f, 0.07f, 0.10f };
                HeroM.Specular.Roughness      = 0.03f;
                HeroM.Clearcoat.CoatWeight    = 1.0f;
                HeroM.Clearcoat.CoatRoughness = 0.02f;

                WreckM.MaterialName           = "SootedShatteredFrame";
                WreckM.Diffuse.BaseColor      = { 0.09f, 0.09f, 0.10f };
                WreckM.Specular.Roughness     = 0.68f;
                break;
            case Codecs::VehicleSurfaceSlot::RimAlloy:
                HeroM.MaterialName            = "ForgedGunmetalRim";
                HeroM.Diffuse.BaseColor       = { 0.74f, 0.76f, 0.80f };
                HeroM.Specular.Metalness      = 0.94f;
                HeroM.Specular.Roughness      = 0.18f;

                WreckM.MaterialName           = "RustedSteelWheel";
                WreckM.Diffuse.BaseColor      = { 0.30f, 0.20f, 0.14f };
                WreckM.Specular.Metalness     = 0.55f;
                WreckM.Specular.Roughness     = 0.72f;
                break;
            default:
                HeroM.MaterialName            = "VulcanizedRaceTrim";
                HeroM.Diffuse.BaseColor       = { 0.055f, 0.058f, 0.064f };
                HeroM.Specular.Roughness      = 0.64f;

                WreckM.MaterialName           = "CharredRubberTrim";
                WreckM.Diffuse.BaseColor      = { 0.06f, 0.058f, 0.055f };
                WreckM.Specular.Roughness     = 0.88f;
                break;
        }
        HeroVehicleSlots[R]  = RegisterMat(HeroM);
        WreckVehicleSlots[R] = RegisterMat(WreckM);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 2. Earth Sub-Grade & Fractured Multi-Lane Boulevard + Radial Blast Sinkhole Crater
    //--------------------------------------------------------------------------------------------------------------------
    {
        auto SubgradeGeom = std::make_unique<ShowcaseGeometryAsset>();
        SubgradeGeom->Network.NetworkTitle = "WarzoneSubgradeDatum";
        AppendOrientedBoxToNetwork(SubgradeGeom->Network, { 0.0f, 0.0f, -0.45f }, { 64.0f, 64.0f, 0.45f }, 0.0f, 0.0f, 0.0f, MatSubgrade);
        // Intact outer boulevard sections flanking central fractured cratered corridor
        AppendOrientedBoxToNetwork(SubgradeGeom->Network, { 0.0f, -22.0f, 0.05f }, { 14.0f, 12.0f, 0.09f }, 0.0f, 0.0f, 0.0f, MatAsphalt);
        AppendOrientedBoxToNetwork(SubgradeGeom->Network, { 0.0f,  26.0f, 0.05f }, { 14.0f, 14.0f, 0.09f }, 0.0f, 0.0f, 0.0f, MatAsphalt);
        // Faded highway center median double-yellow dashes & concrete sidewalks
        for (int Dash = -6; Dash <= 7; ++Dash)
        {
            const float YPos = static_cast<float>(Dash) * 5.2f;
            if (std::fabs(YPos - 1.5f) < 5.0f)
            {
                continue; // Interrupted across central blast crater
            }
            AppendOrientedBoxToNetwork(SubgradeGeom->Network, { -0.22f, YPos, 0.145f }, { 0.08f, 1.35f, 0.012f }, 0.0f, 0.0f, 0.0f, MatLaneStripe);
            AppendOrientedBoxToNetwork(SubgradeGeom->Network, {  0.22f, YPos, 0.145f }, { 0.08f, 1.35f, 0.012f }, 0.0f, 0.0f, 0.0f, MatLaneStripe);
        }
        // Raised concrete curbs along West (-X) and East (+X) tower frontages
        AppendOrientedBoxToNetwork(SubgradeGeom->Network, { -10.8f, 2.0f, 0.16f }, { 1.6f, 34.0f, 0.18f }, 0.0f, 0.0f, 0.0f, MatConcreteExt);
        AppendOrientedBoxToNetwork(SubgradeGeom->Network, {  10.8f, 2.0f, 0.16f }, { 1.6f, 34.0f, 0.18f }, 0.0f, 0.0f, 0.0f, MatConcreteExt);

        ShowcaseWorldAssetEntry Entry {};
        Entry.Title            = "CentralBoulevardRoadbed";
        Entry.StableIdentifier = "apoc-boulevard-roadbed";
        Entry.MaterialTitle    = WetAsphalt.MaterialName;
        Entry.Subtitle         = "Roadway · 128 × 128 m Warzone Sector";
        Entry.AuthorNotes      = "Multi-lane weathered asphalt boulevard, concrete sidewalks, and exposed crater subgrade.";
        Entry.MaterialSlot     = MatAsphalt;
        PopulateCenteredAssetEntry(*SubgradeGeom, Entry, { 0.0f, 0.0f, 0.0f }, 3u);
        (void)RegisterDynamicAssetInBundle(OutBundle, std::move(SubgradeGeom), std::move(Entry), 1u);
    }

    // Real 3D Convex Boussinesq-Fractured Boulevard Pavement & Blast Crater Heave
    {
        const Mathematics::Axis3D CraterImpact { 1.2f, 1.6f, 0.0f };
        const auto RoadFractured = FractureWorldBoxDomain(
            { -9.2f, -9.8f, -0.14f },
            {  9.2f, 12.2f,  0.16f },
            CraterImpact,
            22u,
            RandomSeed ^ 0xA5F417u,
            "concrete",
            9500.0);
        auto CraterRoadGeom = std::make_unique<ShowcaseGeometryAsset>();
        CraterRoadGeom->Network.NetworkTitle = "FracturedAsphaltCraterSlabs";

        for (const auto& Piece : RoadFractured.Fragments)
        {
            const float Dx   = Piece.Centroid.X - CraterImpact.X;
            const float Dy   = Piece.Centroid.Y - CraterImpact.Y;
            const float Dist = std::sqrt(Dx * Dx + Dy * Dy);
            // Inner crater drops & tilts sharply; rim slabs heave upward into jagged tarmac fangs
            const float RimFactor = std::exp(-std::pow((Dist - 3.6f) / 2.2f, 2.0f));
            const float PitDrop   = (Dist < 2.6f) ? (-0.26f * (1.0f - Dist / 2.6f)) : 0.0f;
            const float HeaveZ    = PitDrop + 0.44f * RimFactor;
            const float TiltMag   = 18.0f * RimFactor + 5.0f * SignedRandom(Rng);
            const float RadialAng = std::atan2(Dy, Dx);
            const float RollDeg   =  std::sin(RadialAng) * TiltMag;
            const float PitchDeg  = -std::cos(RadialAng) * TiltMag;

            AppendPolyhedronToNetworkWithMaterials(
                Piece,
                CraterRoadGeom->Network,
                { 0.0f, 0.0f, HeaveZ },
                RollDeg,
                PitchDeg,
                3.5f * SignedRandom(Rng),
                MatAsphalt,
                MatConcreteCore);
        }

        ShowcaseWorldAssetEntry Entry {};
        Entry.Title            = "FracturedBoulevardSinkhole";
        Entry.StableIdentifier = "apoc-fractured-sinkhole";
        Entry.MaterialTitle    = WetAsphalt.MaterialName;
        Entry.Subtitle         = "Roadway · 22 Convex Voronoi Asphalt Slabs";
        Entry.AuthorNotes      = "Radial-blast fractured asphalt pavement with buckled rim heave around central crater.";
        Entry.MaterialSlot     = MatAsphalt;
        PopulateCenteredAssetEntry(*CraterRoadGeom, Entry, { 0.0f, 0.0f, 0.0f }, 4u);
        (void)RegisterDynamicAssetInBundle(OutBundle, std::move(CraterRoadGeom), std::move(Entry), 1u);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 3. Buckled Elevated Highway Overpass Bridge & Launch Ramp into West Tower Floor 2 (Z = +3.8 m)
    //--------------------------------------------------------------------------------------------------------------------
    {
        auto BridgeGeom = std::make_unique<ShowcaseGeometryAsset>();
        BridgeGeom->Network.NetworkTitle = "BuckledViaductOverpass";

        // Collapsed south-to-north drivable bridge ramp: rises from street (X=-3.4, Y=-11.5, Z=0.0) up to viaduct deck (X=-3.4, Y=2.5, Z=3.85)
        // Slope = atan2(3.85, 14.0) = 15.38 degrees -> smooth drivable launch ramp!
        AppendOrientedBoxToNetwork(BridgeGeom->Network, { -3.4f, -4.5f, 1.88f }, { 3.1f, 7.35f, 0.24f }, -15.38f, 0.0f, 0.0f, MatAsphalt);
        AppendOrientedBoxToNetwork(BridgeGeom->Network, { -6.35f, -4.5f, 2.28f }, { 0.22f, 7.35f, 0.42f }, -15.38f, 0.0f, 0.0f, MatConcreteExt);
        AppendOrientedBoxToNetwork(BridgeGeom->Network, { -0.45f, -4.5f, 2.28f }, { 0.22f, 7.35f, 0.42f }, -15.38f, 0.0f, 0.0f, MatConcreteExt);

        // Elevated viaduct deck turning West into West Financial Tower 2nd Floor breach (Z = 3.80 m)
        AppendOrientedBoxToNetwork(BridgeGeom->Network, { -6.8f, 5.4f, 3.68f }, { 6.6f, 3.2f, 0.24f }, 0.0f, 0.0f, 0.0f, MatAsphalt);
        AppendOrientedBoxToNetwork(BridgeGeom->Network, { -6.8f, 8.45f, 4.18f }, { 6.2f, 0.22f, 0.46f }, 0.0f, 0.0f, 0.0f, MatConcreteExt);

        // Sheared eastern viaduct span (collapsed mid-air across +X side with protruding rusted steel box girders + rebar)
        AppendOrientedBoxToNetwork(BridgeGeom->Network, {  4.8f, 6.2f, 4.45f }, { 4.6f, 3.0f, 0.26f }, 6.0f, -14.0f, 4.0f, MatConcreteExt);
        for (int R = -2; R <= 2; ++R)
        {
            const float Ry = 6.2f + static_cast<float>(R) * 1.1f;
            AppendTaperedTubeSegment(BridgeGeom->Network, { -0.8f, Ry, 3.85f }, { 1.9f, Ry, 4.25f }, 0.09f, 0.07f, 6u, MatRustSteel);
        }

        // Massive cylindrical concrete bridge piers + cracked pier caps
        AppendTaperedTubeSegment(BridgeGeom->Network, { -3.4f, 4.8f, 0.0f }, { -3.4f, 4.8f, 3.55f }, 0.72f, 0.64f, 12u, MatConcreteExt);
        AppendTaperedTubeSegment(BridgeGeom->Network, {  6.2f, 6.2f, 0.0f }, {  6.0f, 6.2f, 3.95f }, 0.74f, 0.66f, 12u, MatConcreteExt);
        AppendOrientedBoxToNetwork(BridgeGeom->Network, { -3.4f, 4.8f, 3.45f }, { 2.8f, 0.85f, 0.32f }, 0.0f, 0.0f, 0.0f, MatConcreteCore);

        ShowcaseWorldAssetEntry Entry {};
        Entry.Title            = "BuckledViaductOverpassRamp";
        Entry.StableIdentifier = "apoc-buckled-overpass";
        Entry.MaterialTitle    = FacadeConcrete.MaterialName;
        Entry.Subtitle         = "Roadway · Drivable 15.4° Bridge Ramp -> West Tower F2";
        Entry.AuthorNotes      = "Collapsed reinforced-concrete highway viaduct forming a drivable ramp directly into West Tower Floor 2.";
        Entry.MaterialSlot     = MatConcreteExt;
        PopulateCenteredAssetEntry(*BridgeGeom, Entry, { 0.0f, 0.0f, 0.0f }, 4u);
        (void)RegisterDynamicAssetInBundle(OutBundle, std::move(BridgeGeom), std::move(Entry), 1u);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 4. Tower A ("West Financial Tower" at X in [-25, -11], Y in [-6, +12], 4 Drivable Stories + Interior Floor Ramps)
    //--------------------------------------------------------------------------------------------------------------------
    {
        auto WestTowerGeom = std::make_unique<ShowcaseGeometryAsset>();
        WestTowerGeom->Network.NetworkTitle = "WestFinancialTowerStructure";

        // 4 structural concrete floors: Ground Z=0.12, Floor 2 Z=3.80, Floor 3 Z=7.60, Roof/Floor 4 Z=11.40
        // Leave clean rectangular ramp stairwell openings so vehicles can drive continuously Floor 1 -> Floor 2 -> Floor 3!
        AppendOrientedBoxToNetwork(WestTowerGeom->Network, { -18.0f,  3.0f,  0.10f }, { 7.2f, 9.4f, 0.16f }, 0.0f, 0.0f, 0.0f, MatConcreteCore);
        // Floor 2 (Z = 3.80 m): north bay where viaduct enters + west ramp landing
        AppendOrientedBoxToNetwork(WestTowerGeom->Network, { -15.8f,  6.2f,  3.68f }, { 4.9f, 5.8f, 0.20f }, 0.0f, 0.0f, 0.0f, MatConcreteCore);
        AppendOrientedBoxToNetwork(WestTowerGeom->Network, { -22.4f,  2.8f,  3.68f }, { 2.6f, 9.0f, 0.20f }, 0.0f, 0.0f, 0.0f, MatConcreteCore);
        // Interior Drivable Pancake Ramp 1 (Ground Z=0.15 at Y=-5.2 -> Floor 2 Z=3.70 at Y=+5.8 along X=-15.6): 17.8 deg drivable slope
        AppendOrientedBoxToNetwork(WestTowerGeom->Network, { -15.6f, -1.8f,  1.92f }, { 2.35f, 5.8f, 0.18f }, -17.8f, 0.0f, 0.0f, MatConcreteExt);

        // Interior Drivable Pancake Ramp 2 (Floor 2 Z=3.72 at Y=+8.4 -> Floor 3 Z=7.60 at Y=-3.2 along X=-22.2): +18.4 deg drivable slope
        AppendOrientedBoxToNetwork(WestTowerGeom->Network, { -22.2f,  2.6f,  5.66f }, { 2.35f, 6.1f, 0.18f },  18.4f, 0.0f, 0.0f, MatConcreteExt);
        // Floor 3 (Z = 7.60 m): broad upper atrium launching onto the Building-to-Building Skybridge at X=-11.0, Y=-1.5!
        AppendOrientedBoxToNetwork(WestTowerGeom->Network, { -17.6f, -2.4f,  7.48f }, { 6.8f, 3.8f, 0.20f }, 0.0f, 0.0f, 0.0f, MatConcreteCore);
        // Partial shattered Roof Penthouse slab (Z = 11.40 m)
        AppendOrientedBoxToNetwork(WestTowerGeom->Network, { -20.2f,  5.5f, 11.30f }, { 4.8f, 5.4f, 0.22f }, 0.0f, 0.0f, 0.0f, MatConcreteExt);

        // Reinforced concrete perimeter columns & exposed steel H-beams
        for (float Cx : { -24.6f, -18.2f, -11.4f })
        {
            for (float Cy : { -5.8f, 0.4f, 6.2f, 11.8f })
            {
                const float TopZ = (Cx > -13.0f && Cy > 5.0f) ? 10.2f : 12.4f;
                AppendOrientedBoxToNetwork(WestTowerGeom->Network, { Cx, Cy, TopZ * 0.5f }, { 0.44f, 0.44f, TopZ * 0.5f }, 0.0f, 0.0f, 0.0f, MatConcreteExt);
                AppendOrientedBoxToNetwork(WestTowerGeom->Network, { Cx, Cy, TopZ + 0.75f }, { 0.12f, 0.12f, 0.85f }, 4.0f * SignedRandom(Rng), 6.0f * SignedRandom(Rng), 0.0f, MatRustSteel);
            }
        }

        // Boussinesq-fractured outer concrete spandrel walls on West Tower (leaving viaduct F2 & skybridge F3 portals wide open!)
        {
            const auto NorthFacade = FractureWorldBoxDomain(
                { -25.0f, 11.6f, 0.2f },
                { -11.2f, 12.3f, 11.2f },
                { -14.5f, 12.0f, 9.2f },
                16u,
                RandomSeed ^ 0xB1D601u,
                "concrete",
                8200.0);
            for (const auto& Frag : NorthFacade.Fragments)
            {
                // Drop upper-east corner pieces out of the static structure (they become dynamic collapsible chunks below!)
                if (Frag.Centroid.Z > 7.8f && Frag.Centroid.X > -16.5f)
                {
                    continue;
                }
                AppendPolyhedronToNetworkWithMaterials(Frag, WestTowerGeom->Network, {}, 0.0f, 0.0f, 0.0f, MatConcreteExt, MatConcreteCore);
            }
        }

        ShowcaseWorldAssetEntry Entry {};
        Entry.Title            = "WestFinancialTowerRuin";
        Entry.StableIdentifier = "apoc-west-tower";
        Entry.MaterialTitle    = FacadeConcrete.MaterialName;
        Entry.Subtitle         = "Building · 4 Drivable Floors + 2 Interior Pancake Ramps";
        Entry.AuthorNotes      = "Voronoi-fractured reinforced concrete skyscraper with drivable Floor 1->2->3 interior ramps.";
        Entry.MaterialSlot     = MatConcreteExt;
        PopulateCenteredAssetEntry(*WestTowerGeom, Entry, { 0.0f, 0.0f, 0.0f }, 4u);
        (void)RegisterDynamicAssetInBundle(OutBundle, std::move(WestTowerGeom), std::move(Entry), 1u);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 5. Building-to-Building Fallen Steel-Girder Skybridge Ramp (West Tower F3 Z=7.6m -> East Tower F2 Z=4.6m)
    //--------------------------------------------------------------------------------------------------------------------
    {
        auto SkybridgeGeom = std::make_unique<ShowcaseGeometryAsset>();
        SkybridgeGeom->Network.NetworkTitle = "FallenSkybridgeRamp";

        // Spans X in [-11.2, +11.2] (22.4 m across the central avenue at Y = -2.2 m), dropping from Z=7.55 m to Z=4.55 m
        // Pitch around +Y: +X is lower by 3.0 m over 22.4 m -> Pitch = +7.63 degrees!
        AppendOrientedBoxToNetwork(SkybridgeGeom->Network, { 0.0f, -2.2f, 6.05f }, { 11.6f, 2.55f, 0.22f }, 0.0f, 7.63f, 0.0f, MatAsphalt);
        // Heavy oxidized steel box girders & diagonal Warren truss struts along both sides of the skybridge
        AppendOrientedBoxToNetwork(SkybridgeGeom->Network, { 0.0f, -4.65f, 6.55f }, { 11.6f, 0.18f, 0.55f }, 0.0f, 7.63f, 0.0f, MatRustSteel);
        AppendOrientedBoxToNetwork(SkybridgeGeom->Network, { 0.0f,  0.25f, 6.55f }, { 11.6f, 0.18f, 0.55f }, 0.0f, 7.63f, 0.0f, MatRustSteel);
        AppendOrientedBoxToNetwork(SkybridgeGeom->Network, { 0.0f, -2.2f,  7.95f }, {  7.5f, 2.65f, 0.12f }, 0.0f, 7.63f, 0.0f, MatRustSteel);
        for (int Bay = -4; Bay <= 4; ++Bay)
        {
            const float Bx   = static_cast<float>(Bay) * 2.4f;
            const float Bz   = 6.05f - Bx * std::tan(7.63f * Constants::DegToRadFloat);
            AppendTaperedTubeSegment(SkybridgeGeom->Network, { Bx, -4.6f, Bz + 0.2f }, { Bx + 1.1f, -4.6f, Bz + 1.75f }, 0.08f, 0.08f, 5u, MatRustSteel);
            AppendTaperedTubeSegment(SkybridgeGeom->Network, { Bx,  0.2f, Bz + 0.2f }, { Bx + 1.1f,  0.2f, Bz + 1.75f }, 0.08f, 0.08f, 5u, MatRustSteel);
        }

        ShowcaseWorldAssetEntry Entry {};
        Entry.Title            = "BuildingToBuildingSkybridgeRamp";
        Entry.StableIdentifier = "apoc-skybridge-ramp";
        Entry.MaterialTitle    = OxidizedGirder.MaterialName;
        Entry.Subtitle         = "Ramp · 22.4 m Steel Skybridge (West F3 -> East F2)";
        Entry.AuthorNotes      = "Sheared structural steel skybridge ramp connecting West Tower Floor 3 directly into East Tower Floor 2.";
        Entry.MaterialSlot     = MatRustSteel;
        PopulateCenteredAssetEntry(*SkybridgeGeom, Entry, { 0.0f, 0.0f, 0.0f }, 4u);
        (void)RegisterDynamicAssetInBundle(OutBundle, std::move(SkybridgeGeom), std::move(Entry), 1u);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 6. Tower B ("East Commerce Tower" at X in [+11, +25], Y in [-9, +10], 3 Stories + Street Plunge Ramp)
    //--------------------------------------------------------------------------------------------------------------------
    {
        auto EastTowerGeom = std::make_unique<ShowcaseGeometryAsset>();
        EastTowerGeom->Network.NetworkTitle = "EastCommerceTowerStructure";

        // Ground slab + Floor 2 (Z = 4.55 m receiving Skybridge!) + sheared Floor 3 (Z = 9.10 m)
        AppendOrientedBoxToNetwork(EastTowerGeom->Network, { 18.0f,  0.5f, 0.10f }, { 7.2f, 9.8f, 0.16f }, 0.0f, 0.0f, 0.0f, MatConcreteCore);
        AppendOrientedBoxToNetwork(EastTowerGeom->Network, { 17.5f, -3.2f, 4.42f }, { 6.6f, 5.2f, 0.20f }, 0.0f, 0.0f, 0.0f, MatWarmFacade);
        AppendOrientedBoxToNetwork(EastTowerGeom->Network, { 19.2f,  4.8f, 9.05f }, { 5.4f, 4.8f, 0.22f }, 3.5f, -4.0f, 0.0f, MatWarmFacade);

        // Plunge Exit Ramp from East Tower Floor 2 (Z=4.50, Y=+1.6) down to North-East Boulevard (Z=0.10, Y=+15.2) along X=+14.4
        // Slope = 17.9 degrees -> exits straight through the Smashable Masonry Breach Wall at Y=+9.8!
        AppendOrientedBoxToNetwork(EastTowerGeom->Network, { 14.4f, 8.4f, 2.28f }, { 2.5f, 7.1f, 0.20f }, 17.9f, 0.0f, 0.0f, MatConcreteExt);

        // Structural columns + fractured limestone west/south curtain spandrels
        for (float Cx : { 11.4f, 18.0f, 24.4f })
        {
            for (float Cy : { -8.6f, -2.5f, 3.8f, 9.6f })
            {
                if (Cx < 13.0f && std::fabs(Cy + 2.2f) < 3.2f)
                {
                    continue; // Open portal for Skybridge arrival
                }
                AppendOrientedBoxToNetwork(EastTowerGeom->Network, { Cx, Cy, 5.2f }, { 0.42f, 0.42f, 5.2f }, 0.0f, 0.0f, 0.0f, MatWarmFacade);
            }
        }

        {
            const auto SouthFacade = FractureWorldBoxDomain(
                { 11.1f, -9.0f, 0.2f },
                { 24.6f, -8.3f, 9.6f },
                { 15.2f, -8.6f, 7.8f },
                15u,
                RandomSeed ^ 0xEA5702u,
                "concrete",
                8000.0);
            for (const auto& Frag : SouthFacade.Fragments)
            {
                if (Frag.Centroid.Z > 6.6f && Frag.Centroid.X < 18.0f)
                {
                    continue; // Missing blasted corner!
                }
                AppendPolyhedronToNetworkWithMaterials(Frag, EastTowerGeom->Network, {}, 0.0f, 0.0f, 0.0f, MatWarmFacade, MatConcreteCore);
            }
        }

        ShowcaseWorldAssetEntry Entry {};
        Entry.Title            = "EastCommerceTowerRuin";
        Entry.StableIdentifier = "apoc-east-tower";
        Entry.MaterialTitle    = WarmSandstoneFacade.MaterialName;
        Entry.Subtitle         = "Building · 3 Stories + Skybridge Landing + Plunge Ramp";
        Entry.AuthorNotes      = "Fractured limestone & concrete tower receiving the Floor 2 skybridge with exit ramp to street.";
        Entry.MaterialSlot     = MatWarmFacade;
        PopulateCenteredAssetEntry(*EastTowerGeom, Entry, { 0.0f, 0.0f, 0.0f }, 4u);
        (void)RegisterDynamicAssetInBundle(OutBundle, std::move(EastTowerGeom), std::move(Entry), 1u);
    }

    // Distant North Urban Canyon Ruin Silhouettes (multi-tier stepped skyscrapers with window spandrels & bombed-out roof notches)
    {
        auto CanyonGeom = std::make_unique<ShowcaseGeometryAsset>();
        CanyonGeom->Network.NetworkTitle = "NorthSkylineRuinedTowers";

        auto BuildSkylineRuinTower = [&](Mathematics::Axis3D FootprintCenter, float HalfW, float HalfD, float TotalH, uint16_t WallMat)
        {
            const uint32_t Stories = static_cast<uint32_t>(std::max(3.0f, std::floor(TotalH / 3.4f)));
            for (uint32_t S = 0u; S < Stories; ++S)
            {
                const float ZFloor = static_cast<float>(S) * 3.4f + 0.25f;
                const float Taper  = (S + 2u >= Stories) ? 0.82f : 1.0f;
                // Horizontal concrete spandrel slab
                AppendOrientedBoxToNetwork(CanyonGeom->Network, { FootprintCenter.X, FootprintCenter.Y, ZFloor }, { HalfW * Taper, HalfD * Taper, 0.26f }, 0.0f, 0.0f, 0.0f, WallMat);
                // Recessed reflective curtain-glass core on lower intact stories; open bombed-out columns on top 2 stories
                if (S + 2u < Stories)
                {
                    AppendOrientedBoxToNetwork(CanyonGeom->Network, { FootprintCenter.X, FootprintCenter.Y, ZFloor + 1.65f }, { HalfW * 0.91f, HalfD * 0.91f, 1.42f }, 0.0f, 0.0f, 0.0f, MatCurtainGlass);
                }
                for (int Col = -2; Col <= 2; ++Col)
                {
                    if (S + 1u == Stories && Col > 0)
                    {
                        continue; // Jagged collapsed upper-corner notch!
                    }
                    const float OffX = static_cast<float>(Col) * (HalfW * Taper * 0.45f);
                    AppendOrientedBoxToNetwork(CanyonGeom->Network, { FootprintCenter.X + OffX, FootprintCenter.Y - HalfD * Taper + 0.22f, ZFloor + 1.65f }, { 0.30f, 0.26f, 1.55f }, 0.0f, 0.0f, 0.0f, WallMat);
                }
            }
            // Exposed rusted roof antenna / twisted structural steel columns
            AppendOrientedBoxToNetwork(CanyonGeom->Network, { FootprintCenter.X - HalfW * 0.35f, FootprintCenter.Y, TotalH + 1.4f }, { 0.14f, 0.14f, 1.6f }, 5.0f, -8.0f, 0.0f, MatRustSteel);
        };

        BuildSkylineRuinTower({ -19.5f, 28.5f, 0.0f }, 6.6f, 5.8f, 19.5f, MatConcreteExt);
        BuildSkylineRuinTower({  19.8f, 30.0f, 0.0f }, 6.8f, 5.8f, 21.2f, MatWarmFacade);
        BuildSkylineRuinTower({   1.8f, 39.5f, 0.0f }, 7.8f, 5.4f, 24.5f, MatConcreteExt);

        ShowcaseWorldAssetEntry Entry {};
        Entry.Title            = "NorthCanyonSkylineTowers";
        Entry.StableIdentifier = "apoc-north-canyon";
        Entry.MaterialTitle    = FacadeConcrete.MaterialName;
        Entry.Subtitle         = "Building · 3 Stepped Fractured High-Rise Ruin Towers";
        Entry.MaterialSlot     = MatConcreteExt;
        PopulateCenteredAssetEntry(*CanyonGeom, Entry, { 0.0f, 0.0f, 0.0f }, 3u);
        (void)RegisterDynamicAssetInBundle(OutBundle, std::move(CanyonGeom), std::move(Entry), 1u);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 7. Hero Racing ControlVehicle + 3 Overturned / Rolled Wrecked ControlVehicle Hulls
    //--------------------------------------------------------------------------------------------------------------------
    Codecs::AssembledControlVehicle DecodedVehicle {};
    std::string VehicleDiag {};
    const bool VehicleGeometryReady = !RepoRoot.empty() &&
        Codecs::ControlVehicleSurfaceDecoder::DecodeAssembledVehicle(
            RepoRoot, Codecs::VehicleCoordinateConvention::FrontierWorldZUp, DecodedVehicle, VehicleDiag) &&
        !DecodedVehicle.Triangles.empty();

    auto BuildVehicleVariant = [&](
        std::string_view                                                                Title,
        std::string_view                                                                StableId,
        Mathematics::Axis3D                                                             WorldPos,
        float                                                                           RollDeg,
        float                                                                           PitchDeg,
        float                                                                           YawDeg,
        const std::array<uint16_t, static_cast<std::size_t>(Codecs::VehicleSurfaceSlot::Count)>& SlotIndices,
        uint32_t                                                                        TriangleStride) -> uint32_t
    {
        auto VehGeom = std::make_unique<ShowcaseGeometryAsset>();
        VehGeom->Network.NetworkTitle = std::string(Title);

        if (VehicleGeometryReady)
        {
            const Mathematics::Axis3D SrcCenter = DecodedVehicle.BoundsCenter;
            const uint32_t Step = std::max(1u, TriangleStride);
            for (std::size_t T = 0u; T < DecodedVehicle.Triangles.size(); T += Step)
            {
                const auto& Tri = DecodedVehicle.Triangles[T];
                const uint16_t Mat = SlotIndices[std::min<std::size_t>(
                    static_cast<std::size_t>(Tri.SurfaceSlot), SlotIndices.size() - 1u)];
                // Bake Roll & Pitch into local asset; Yaw stays dynamic in RotationZDegrees!
                const Mathematics::Axis3D P0 = RotateEulerRpy(Tri.CornerA - SrcCenter, RollDeg, PitchDeg, 0.0f);
                const Mathematics::Axis3D P1 = RotateEulerRpy(Tri.CornerB - SrcCenter, RollDeg, PitchDeg, 0.0f);
                const Mathematics::Axis3D P2 = RotateEulerRpy(Tri.CornerC - SrcCenter, RollDeg, PitchDeg, 0.0f);
                const uint32_t V0 = VehGeom->Network.AppendVertex(P0);
                const uint32_t V1 = VehGeom->Network.AppendVertex(P1);
                const uint32_t V2 = VehGeom->Network.AppendVertex(P2);
                const std::array<uint32_t, 3u> Loop {{ V0, V1, V2 }};
                (void)VehGeom->Network.AppendFace(Loop, {}, {}, 0u, Mat);
            }
        }
        else
        {
            AppendOrientedBoxToNetwork(VehGeom->Network, { 0.0f, 0.0f, 0.0f }, { 2.22f, 0.96f, 0.64f }, RollDeg, PitchDeg, 0.0f, SlotIndices[0u]);
        }

        ShowcaseWorldAssetEntry Entry {};
        Entry.Title            = std::string(Title);
        Entry.StableIdentifier = std::string(StableId);
        Entry.MaterialTitle    = (TriangleStride == 1u) ? "InterceptorCrimsonMetallic" : "ScorchedOxidizedChassis";
        Entry.Subtitle         = (TriangleStride == 1u)
            ? "Vehicle · Hero Racing ControlVehicle"
            : "Wreck · Overturned Scorched Vehicle Hull";
        Entry.RotationZDegrees = YawDeg;
        Entry.MaterialSlot     = SlotIndices[0u];
        PopulateCenteredAssetEntry(*VehGeom, Entry, WorldPos, 5u);
        return RegisterDynamicAssetInBundle(OutBundle, std::move(VehGeom), std::move(Entry), 1u);
    };

    // Hero Racing Vehicle on the starting boulevard charging toward the Buckled Viaduct Ramp!
    // Note: ControlVehicle forward nose is +X in local coordinates, so Yaw = +90 deg faces +Y (up the viaduct ramp)
    OutSimulation.HeroVehicleIndex = BuildVehicleVariant(
        "HeroInterceptorVehicle", "apoc-hero-vehicle",
        { -3.4f, -13.2f, 0.78f }, 0.0f, 0.0f, 90.0f, HeroVehicleSlots, 1u);

    // Multi-level racing course waypoints (Boulevard -> Viaduct Ramp -> West Tower F2 -> West Tower F3 -> Skybridge -> East Tower F2 -> Breach Wall -> Street!)
    OutSimulation.HeroCourseWaypoints = {
        { -3.4f, -13.2f, 0.78f }, // 0: Street launch
        { -3.4f,  -4.5f, 2.65f }, // 1: Climbing buckled viaduct bridge
        { -5.2f,   5.2f, 4.48f }, // 2: Viaduct top deck -> smashing West Tower F2 glass curtain!
        { -13.6f,  5.2f, 4.48f }, // 3: Inside West Tower Floor 2
        { -22.0f,  6.8f, 4.48f }, // 4: Entering interior Floor 2->3 pancake ramp
        { -22.0f, -2.4f, 8.28f }, // 5: Cresting onto West Tower Floor 3 atrium
        { -11.5f, -2.2f, 8.22f }, // 6: Launching onto Building-to-Building Skybridge!
        {   0.0f, -2.2f, 6.85f }, // 7: Mid-air across Skybridge steel girders (shattering skybridge glass!)
        {  14.2f, -2.0f, 5.25f }, // 8: Touchdown inside East Commerce Tower Floor 2
        {  14.4f,  9.8f, 2.35f }, // 9: Blasting straight through East Tower Smashable Masonry Breach Wall!
        {  12.0f, 17.5f, 0.78f }, // 10: Landing back on North-East Boulevard
        {   2.5f, 21.0f, 0.78f }, // 11: Sweeping around overgrown boulevard back toward main straight
    };

    auto BuildSculptedWreckHull = [&](
        std::string_view    Title,
        std::string_view    StableId,
        Mathematics::Axis3D WorldPos,
        float               RollDeg,
        float               PitchDeg,
        float               YawDeg) -> uint32_t
    {
        auto WreckGeom = std::make_unique<ShowcaseGeometryAsset>();
        WreckGeom->Network.NetworkTitle = std::string(Title);

        const uint16_t BodyMat = WreckVehicleSlots[static_cast<std::size_t>(Codecs::VehicleSurfaceSlot::BodyCoat)];
        const uint16_t RimMat  = WreckVehicleSlots[static_cast<std::size_t>(Codecs::VehicleSurfaceSlot::RimAlloy)];
        const uint16_t TyreMat = WreckVehicleSlots[static_cast<std::size_t>(Codecs::VehicleSurfaceSlot::TyreRubber)];

        // Lower chassis tub + popped crumpled hood + crushed roof greenhouse + bumpers + 4 tilted wheels
        GeometrySpace::PolygonNetwork RawNet {};
        AppendOrientedBoxToNetwork(RawNet, {  0.00f, 0.0f, -0.14f }, { 2.15f, 0.92f, 0.34f }, 0.0f,  0.0f, 0.0f, BodyMat);
        AppendOrientedBoxToNetwork(RawNet, {  1.32f, 0.0f,  0.26f }, { 0.78f, 0.86f, 0.06f }, 6.0f, -18.0f, 4.0f, BodyMat);
        AppendOrientedBoxToNetwork(RawNet, { -0.22f, 0.0f,  0.42f }, { 1.05f, 0.78f, 0.28f }, 4.0f,   5.0f, 0.0f, BodyMat);
        for (float Wx : { -1.32f, 1.34f })
        {
            for (float Wy : { -0.92f, 0.92f })
            {
                AppendTaperedTubeSegment(RawNet, { Wx, Wy - 0.13f, -0.34f }, { Wx, Wy + 0.13f, -0.34f }, 0.35f, 0.35f, 10u, TyreMat);
                AppendTaperedTubeSegment(RawNet, { Wx, Wy - 0.15f, -0.34f }, { Wx, Wy + 0.15f, -0.34f }, 0.21f, 0.21f, 8u,  RimMat);
            }
        }
        for (GeometrySpace::Vertex& V : RawNet.Vertices)
        {
            V.Axes = RotateEulerRpy(V.Axes, RollDeg, PitchDeg, 0.0f);
        }
        WreckGeom->Network = std::move(RawNet);

        ShowcaseWorldAssetEntry Entry {};
        Entry.Title            = std::string(Title);
        Entry.StableIdentifier = std::string(StableId);
        Entry.MaterialTitle    = "ScorchedOxidizedChassis";
        Entry.Subtitle         = "Wreck · Overturned Scorched Vehicle Hull";
        Entry.RotationZDegrees = YawDeg;
        Entry.MaterialSlot     = BodyMat;
        PopulateCenteredAssetEntry(*WreckGeom, Entry, WorldPos, 4u);
        return RegisterDynamicAssetInBundle(OutBundle, std::move(WreckGeom), std::move(Entry), 1u);
    };

    // 3 Overturned / Rolled Wrecked Cars: Foreground 25k-tri upside-down ControlVehicle + 2 sculpted crushed wrecks
    (void)BuildVehicleVariant(
        "OverturnedSedanWreckA", "apoc-wreck-sedan-a",
        { 3.6f, -5.8f, 0.74f }, 168.0f, -11.0f, 34.0f, WreckVehicleSlots, 1u);
    (void)BuildSculptedWreckHull(
        "SideRolledCoupeWreckB", "apoc-wreck-coupe-b",
        { -7.8f, -10.4f, 0.96f }, 79.0f, 16.0f, -48.0f);
    (void)BuildSculptedWreckHull(
        "CrushedViaductWreckC", "apoc-wreck-viaduct-c",
        { 6.8f, 2.8f, 0.86f }, -154.0f, 21.0f, 128.0f);

    //--------------------------------------------------------------------------------------------------------------------
    // 8. Reclaimed Nature: Overgrown Pavement-Crack Grass Meadows & Cascading Climbing Vines
    //--------------------------------------------------------------------------------------------------------------------
    {
        auto FloraGeom = std::make_unique<ShowcaseGeometryAsset>();
        FloraGeom->Network.NetworkTitle = "OvergrownBoulevardGrassMeadows";

        static constexpr std::array<Mathematics::Axis3D, 28u> GrassAnchors {{
            { -8.8f, -14.0f, 0.14f }, { -9.4f, -7.5f, 0.14f }, { -7.2f, -2.2f, 0.14f }, { -8.6f,  2.8f, 0.14f },
            { -9.2f,   9.6f, 0.14f }, { -6.5f, 14.5f, 0.14f }, { -2.4f, 16.8f, 0.14f }, {  2.8f, 15.2f, 0.14f },
            {  8.4f,  13.8f, 0.14f }, {  9.2f,  7.8f, 0.14f }, {  8.6f, -1.4f, 0.14f }, {  9.1f, -8.2f, 0.14f },
            {  5.4f, -12.6f, 0.14f }, {  1.6f, -9.4f, 0.14f }, { -0.8f, -6.6f, 0.14f }, {  4.8f, -2.2f, 0.24f },
            { -4.6f,   1.8f, 0.26f }, {  2.2f,  6.4f, 0.22f }, { -1.8f, 11.2f, 0.14f }, {  6.2f, 18.4f, 0.14f },
            { -5.8f,  20.2f, 0.14f }, {  0.6f, 23.5f, 0.14f }, { -11.4f,-11.0f,0.32f }, { 11.4f,-11.5f, 0.32f },
            { -11.2f, 14.8f, 0.32f }, { 11.2f, 15.5f, 0.32f }, {  3.4f, -16.5f,0.14f }, { -1.2f,-17.8f, 0.14f },
        }};
        for (const Mathematics::Axis3D& Root : GrassAnchors)
        {
            AppendGrassClumpToNetwork(FloraGeom->Network, Root, 0.95f + 0.55f * UnitRandom(Rng), Rng, MatLushGrass, MatDryGrass);
        }

        ShowcaseWorldAssetEntry Entry {};
        Entry.Title            = "OvergrownAsphaltGrassClusters";
        Entry.StableIdentifier = "apoc-overgrown-grass";
        Entry.MaterialTitle    = LushGrass.MaterialName;
        Entry.Subtitle         = "Flora · 28 Subsurface-Scattered Meadow Grass Clumps";
        Entry.AuthorNotes      = "Wild chlorophyll and sun-bleached rye grass reclaiming boulevard cracks and craters.";
        Entry.MaterialSlot     = MatLushGrass;
        PopulateCenteredAssetEntry(*FloraGeom, Entry, { 0.0f, 0.0f, 0.0f }, 4u);
        (void)RegisterDynamicAssetInBundle(OutBundle, std::move(FloraGeom), std::move(Entry), 1u);
    }

    {
        auto VinesGeom = std::make_unique<ShowcaseGeometryAsset>();
        VinesGeom->Network.NetworkTitle = "CascadingFacadeIvyVines";

        // Draped climbing vines down West Tower shattered ledges, Viaduct bridge deck, Skybridge girders & East Tower
        AppendHangingVineCurtainToNetwork(VinesGeom->Network, { -11.2f,  7.2f, 11.2f }, { -11.2f, 11.4f, 11.2f }, 6.4f, 9u, Rng, MatVineStem, MatVineLeaf);
        AppendHangingVineCurtainToNetwork(VinesGeom->Network, { -11.2f, -5.4f,  7.5f }, { -11.2f, -0.4f,  7.5f }, 4.8f, 7u, Rng, MatVineStem, MatVineLeaf);
        AppendHangingVineCurtainToNetwork(VinesGeom->Network, {  -4.8f, -4.6f,  6.4f }, {   4.8f, -4.6f,  5.2f }, 3.8f, 8u, Rng, MatVineStem, MatVineLeaf);
        AppendHangingVineCurtainToNetwork(VinesGeom->Network, {  11.2f, -8.2f,  8.8f }, {  11.2f, -4.2f,  8.8f }, 5.6f, 8u, Rng, MatVineStem, MatVineLeaf);
        AppendHangingVineCurtainToNetwork(VinesGeom->Network, {   1.2f,  6.2f,  4.4f }, {   7.8f,  6.2f,  4.2f }, 3.4f, 7u, Rng, MatVineStem, MatVineLeaf);

        ShowcaseWorldAssetEntry Entry {};
        Entry.Title            = "ClimbingTowerIvyCurtains";
        Entry.StableIdentifier = "apoc-climbing-vines";
        Entry.MaterialTitle    = VineLeaf.MaterialName;
        Entry.Subtitle         = "Flora · 5 Cascading Botanical Ivy & Tendril Curtains";
        Entry.AuthorNotes      = "Hanging woody vine tendrils and subsurface-scattered ivy foliage draped over ruined ledges.";
        Entry.MaterialSlot     = MatVineLeaf;
        PopulateCenteredAssetEntry(*VinesGeom, Entry, { 0.0f, 0.0f, 0.0f }, 4u);
        (void)RegisterDynamicAssetInBundle(OutBundle, std::move(VinesGeom), std::move(Entry), 1u);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 9. Freely Roaming Articulated Woodland Deer Herd (4 Autonomous Obstacle-Avoiding Agents)
    //--------------------------------------------------------------------------------------------------------------------
    struct DeerSpawnSpec
    {
        const char*         Name;
        Mathematics::Axis3D Pos;
        float               HeadingDeg;
        float               Speed;
        bool                Stag;
    };
    static constexpr std::array<DeerSpawnSpec, 4u> HerdSpecs {{
        { "WoodlandStagLeader",  {  1.8f, -12.4f, 0.72f }, 118.0f, 2.45f, true  },
        { "WoodlandHindGrazerA", {  5.2f, -14.8f, 0.72f }, 132.0f, 2.15f, false },
        { "WoodlandStagScoutB",  { -4.6f,  15.6f, 0.72f }, -42.0f, 2.55f, true  },
        { "WoodlandHindForager", {  3.8f,  18.2f, 0.72f }, -68.0f, 2.20f, false },
    }};

    for (std::size_t D = 0u; D < HerdSpecs.size(); ++D)
    {
        const DeerSpawnSpec& Spec = HerdSpecs[D];
        auto DeerGeom = std::make_unique<ShowcaseGeometryAsset>();
        SculptWoodlandDeerNetwork(DeerGeom->Network, MatDeerCoat, MatDeerBelly, MatDeerAntler, MatDeerHoof, Spec.Stag);

        ShowcaseWorldAssetEntry Entry {};
        Entry.Title            = Spec.Name;
        Entry.StableIdentifier = std::string("apoc-deer-") + std::to_string(D);
        Entry.MaterialTitle    = DeerCoat.MaterialName;
        Entry.Subtitle         = Spec.Stag
            ? "Wildlife · Roaming Antlered Woodland Stag"
            : "Wildlife · Roaming Woodland Hind";
        Entry.AuthorNotes      = "Autonomous obstacle-avoiding Woodland Deer roaming the overgrown boulevard.";
        Entry.RotationZDegrees = Spec.HeadingDeg;
        Entry.MaterialSlot     = MatDeerCoat;
        PopulateCenteredAssetEntry(*DeerGeom, Entry, Spec.Pos, 4u);
        const float GroundedZ = Entry.CenterWorld.Z;
        const uint32_t ObjIdx = RegisterDynamicAssetInBundle(OutBundle, std::move(DeerGeom), std::move(Entry), 1u);

        ApocalypticDeerWanderer Agent {};
        Agent.AssetSlotIndex       = ObjIdx;
        Agent.PositionWorld        = OutBundle.Objects[ObjIdx].CenterWorld;
        Agent.RestElevationZ       = GroundedZ;
        Agent.HeadingDegrees       = Spec.HeadingDeg;
        Agent.CruiseSpeedMetersSec = Spec.Speed;
        Agent.StridePhaseRadians   = static_cast<float>(D) * 1.57f;
        Agent.WanderSeed           = 17.3f * static_cast<float>(D + 1u);
        OutSimulation.RoamingDeer.push_back(Agent);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 10. Dynamic Destruction: Noise-Collapsing Building Bays, Collapsing Floors, Falling Debris,
    //     Smashable Abbe Glass Curtain Walls, and Smashable Radial-Fractured Masonry Breach Walls!
    //--------------------------------------------------------------------------------------------------------------------

    // 10A. Noise-Driven Collapsing Upper Facade Bays + Collapsing Interior Floor Slabs + Falling Girder Debris
    {
        const auto UnstableFacade = FractureWorldBoxDomain(
            { -15.8f, 8.6f, 8.2f },
            { -10.9f, 12.1f, 12.6f },
            { -12.2f, 10.8f, 9.8f },
            8u,
            RandomSeed ^ 0xC011A95Eu,
            "concrete",
            5200.0);

        for (std::size_t F = 0u; F < UnstableFacade.Fragments.size(); ++F)
        {
            const auto& Frag = UnstableFacade.Fragments[F];
            auto FragGeom    = std::make_unique<ShowcaseGeometryAsset>();
            FragGeom->Network.NetworkTitle = "UnstableTowerFacadeChunk_" + std::to_string(F);
            AppendPolyhedronToNetworkWithMaterials(Frag, FragGeom->Network, {}, 0.0f, 0.0f, 0.0f, MatConcreteExt, MatConcreteCore);

            ShowcaseWorldAssetEntry Entry {};
            Entry.Title            = "UnstableTowerChunk_" + std::to_string(F + 1u);
            Entry.StableIdentifier = "apoc-collapse-chunk-" + std::to_string(F);
            Entry.MaterialTitle    = FacadeConcrete.MaterialName;
            Entry.Subtitle         = "Dynamic Collapse · Seismic-Noise Facade Fragment";
            Entry.MaterialSlot     = MatConcreteExt;
            PopulateCenteredAssetEntry(*FragGeom, Entry, { 0.0f, 0.0f, 0.0f }, 3u);
            const Mathematics::Axis3D InitPos = Entry.CenterWorld;
            const float HalfZ = Entry.DimensionsMeters.Z * 0.5f;
            const uint32_t ObjIdx = RegisterDynamicAssetInBundle(OutBundle, std::move(FragGeom), std::move(Entry), 1u);

            ApocalypticFallingFragment Dyn {};
            Dyn.AssetSlotIndex      = ObjIdx;
            Dyn.InitialCenterWorld  = InitPos;
            Dyn.CurrentCenterWorld  = InitPos;
            Dyn.VelocityMetersSec   = { 1.4f + 0.9f * UnitRandom(Rng), 0.6f * SignedRandom(Rng), 0.0f };
            Dyn.YawRateDegPerSec    = 42.0f * SignedRandom(Rng);
            Dyn.RestElevationZ      = std::max(0.28f, HalfZ);
            Dyn.NoiseSeedOffset     = 3.1f * static_cast<float>(F);
            Dyn.StructuralThreshold = 0.35f + 0.28f * static_cast<float>(F);
            Dyn.Restitution         = 0.20f;
            OutSimulation.DynamicFragments.push_back(Dyn);
        }

        // 2 Dynamic Collapsing Interior Floor Slabs (hinging & dropping onto lower stories from structural fatigue noise!)
        for (uint32_t SlabIdx = 0u; SlabIdx < 2u; ++SlabIdx)
        {
            auto FloorGeom = std::make_unique<ShowcaseGeometryAsset>();
            FloorGeom->Network.NetworkTitle = "UnstableInteriorFloorBay_" + std::to_string(SlabIdx);
            const Mathematics::Axis3D SlabPos = (SlabIdx == 0u)
                ? Mathematics::Axis3D{ -19.2f, 8.8f, 7.55f }
                : Mathematics::Axis3D{  19.8f, -5.2f, 8.95f };
            AppendOrientedBoxToNetwork(FloorGeom->Network, SlabPos, { 2.3f, 2.1f, 0.20f }, 4.0f, -6.0f, 0.0f, MatConcreteCore);
            AppendOrientedBoxToNetwork(FloorGeom->Network, SlabPos + Mathematics::Axis3D{ 0.0f, 0.0f, -0.22f }, { 2.4f, 0.14f, 0.14f }, 4.0f, -6.0f, 0.0f, MatRustSteel);

            ShowcaseWorldAssetEntry Entry {};
            Entry.Title            = "CollapsingFloorSlab_" + std::to_string(SlabIdx + 1u);
            Entry.StableIdentifier = "apoc-collapsing-floor-" + std::to_string(SlabIdx);
            Entry.MaterialTitle    = FracturedAggregate.MaterialName;
            Entry.Subtitle         = "Dynamic Collapse · Interior Cantilever Floor Slab";
            Entry.MaterialSlot     = MatConcreteCore;
            PopulateCenteredAssetEntry(*FloorGeom, Entry, { 0.0f, 0.0f, 0.0f }, 3u);
            const Mathematics::Axis3D InitPos = Entry.CenterWorld;
            const uint32_t ObjIdx = RegisterDynamicAssetInBundle(OutBundle, std::move(FloorGeom), std::move(Entry), 1u);

            ApocalypticFallingFragment Dyn {};
            Dyn.AssetSlotIndex      = ObjIdx;
            Dyn.InitialCenterWorld  = InitPos;
            Dyn.CurrentCenterWorld  = InitPos;
            Dyn.VelocityMetersSec   = { 0.35f * SignedRandom(Rng), -0.45f, 0.0f };
            Dyn.YawRateDegPerSec    = 24.0f * SignedRandom(Rng);
            Dyn.RestElevationZ      = (SlabIdx == 0u) ? 4.02f : 4.78f; // Lands cleanly onto Floor 2 deck!
            Dyn.NoiseSeedOffset     = 19.4f + 7.2f * static_cast<float>(SlabIdx);
            Dyn.StructuralThreshold = 0.62f + 0.55f * static_cast<float>(SlabIdx);
            Dyn.Restitution         = 0.14f;
            OutSimulation.DynamicFragments.push_back(Dyn);
        }
    }

    // 10B. Two Smashable Abbe Glass Curtain Walls (West Tower F2 Entry + Skybridge Mid-Span Gallery)
    struct GlassCurtainSpec
    {
        const char*         Name;
        Mathematics::Axis3D Center;
        Mathematics::Axis3D HalfExt;
        Mathematics::Axis3D BurstDir;
        float               AutoTimeSec;
    };
    static constexpr std::array<GlassCurtainSpec, 2u> GlassSpecs {{
        { "WestTowerEntryGlassCurtain", { -10.9f,  5.2f, 5.25f }, { 0.08f, 2.65f, 1.45f }, { -1.0f, 0.1f, 0.15f }, 1.65f },
        { "SkybridgeGalleryGlassPane",  {   0.2f, -2.2f, 7.15f }, { 0.08f, 2.35f, 1.05f }, {  1.0f, 0.0f, 0.18f }, 3.85f },
    }};

    for (std::size_t G = 0u; G < GlassSpecs.size(); ++G)
    {
        const GlassCurtainSpec& Spec = GlassSpecs[G];
        auto IntactGlassGeom = std::make_unique<ShowcaseGeometryAsset>();
        IntactGlassGeom->Network.NetworkTitle = Spec.Name;
        // Mullion-framed architectural glass curtain wall (flat dielectric panes + steel mullions)
        AppendOrientedBoxToNetwork(IntactGlassGeom->Network, Spec.Center, { Spec.HalfExt.X, Spec.HalfExt.Y, Spec.HalfExt.Z }, 0.0f, 0.0f, 0.0f, MatCurtainGlass);
        for (int M = -2; M <= 2; ++M)
        {
            const float My = Spec.Center.Y + static_cast<float>(M) * (Spec.HalfExt.Y * 0.48f);
            AppendOrientedBoxToNetwork(IntactGlassGeom->Network, { Spec.Center.X, My, Spec.Center.Z }, { Spec.HalfExt.X + 0.03f, 0.045f, Spec.HalfExt.Z }, 0.0f, 0.0f, 0.0f, MatRustSteel);
        }

        ShowcaseWorldAssetEntry IntactEntry {};
        IntactEntry.Title            = Spec.Name;
        IntactEntry.StableIdentifier = std::string("apoc-glass-curtain-") + std::to_string(G);
        IntactEntry.MaterialTitle    = CurtainGlass.MaterialName;
        IntactEntry.Subtitle         = "Smashable Glass · Mullion-Framed Curtain Wall";
        IntactEntry.MaterialSlot     = MatCurtainGlass;
        IntactEntry.GlassShell       = false;
        PopulateCenteredAssetEntry(*IntactGlassGeom, IntactEntry, { 0.0f, 0.0f, 0.0f }, 3u);
        const uint32_t IntactIdx = RegisterDynamicAssetInBundle(OutBundle, std::move(IntactGlassGeom), std::move(IntactEntry), 1u, true);

        ApocalypticSmashableBarrier Barrier {};
        Barrier.IntactSlotIndex     = IntactIdx;
        Barrier.TriggerCenterWorld  = Spec.Center;
        Barrier.OutwardImpulseDir   = Spec.BurstDir;
        Barrier.TriggerRadiusMeters = 4.2f;
        Barrier.AutoBreachTimeSec   = Spec.AutoTimeSec;
        Barrier.BurstSpeedMetersSec = 9.2f;
        Barrier.GlassCurtain        = true;

        // Pre-fracture 8 convex glass shards (hidden at scale=0 until impact, zero runtime allocation on shatter!)
        const auto GlassShards = FractureWorldBoxDomain(
            Spec.Center - Spec.HalfExt,
            Spec.Center + Spec.HalfExt,
            Spec.Center,
            8u,
            RandomSeed ^ static_cast<uint32_t>(0x61A550u + G * 97u),
            "glass",
            1800.0);

        for (std::size_t S = 0u; S < GlassShards.Fragments.size(); ++S)
        {
            auto ShardGeom = std::make_unique<ShowcaseGeometryAsset>();
            ShardGeom->Network.NetworkTitle = std::string(Spec.Name) + "_Shard_" + std::to_string(S);
            AppendPolyhedronToNetworkWithMaterials(GlassShards.Fragments[S], ShardGeom->Network, {}, 0.0f, 0.0f, 0.0f, MatCurtainGlass, MatCurtainGlass);

            ShowcaseWorldAssetEntry ShardEntry {};
            ShardEntry.Title            = std::string(Spec.Name) + "_Shard" + std::to_string(S + 1u);
            ShardEntry.StableIdentifier = "apoc-glass-shard-" + std::to_string(G) + "-" + std::to_string(S);
            ShardEntry.MaterialTitle    = CurtainGlass.MaterialName;
            ShardEntry.Subtitle         = "Glass Shard · Specular Voronoi Fragment";
            ShardEntry.MaterialSlot     = MatCurtainGlass;
            ShardEntry.GlassShell       = false;
            PopulateCenteredAssetEntry(*ShardGeom, ShardEntry, { 0.0f, 0.0f, 0.0f }, 3u);
            const Mathematics::Axis3D InitPos = ShardEntry.CenterWorld;
            const uint32_t ShardObjIdx = RegisterDynamicAssetInBundle(OutBundle, std::move(ShardGeom), std::move(ShardEntry), 1u, false);

            ApocalypticFallingFragment Frag {};
            Frag.AssetSlotIndex     = ShardObjIdx;
            Frag.InitialCenterWorld = InitPos;
            Frag.CurrentCenterWorld = InitPos;
            Frag.YawRateDegPerSec   = 165.0f * SignedRandom(Rng);
            Frag.RestElevationZ     = (G == 0u) ? 3.92f : 6.22f;
            Frag.Restitution        = 0.32f;
            Frag.AwaitingImpact     = true;
            Frag.HiddenUntilShatter = true;
            const uint32_t FragSlot = static_cast<uint32_t>(OutSimulation.DynamicFragments.size());
            OutSimulation.DynamicFragments.push_back(Frag);
            Barrier.FragmentIndices.push_back(FragSlot);
        }
        OutSimulation.SmashableBarriers.push_back(std::move(Barrier));
    }

    // 10C. Smashable Pre-Fractured Cinder-Masonry Breach Wall across East Tower Plunge Ramp (X=14.4, Y=9.8, Z=2.45)
    {
        const Mathematics::Axis3D WallCenter { 14.4f, 9.8f, 2.75f };
        const auto BreachWall = FractureWorldBoxDomain(
            { 12.1f,  9.52f, 1.35f },
            { 16.7f, 10.08f, 4.25f },
            WallCenter,
            10u,
            RandomSeed ^ 0xB2EAC4u,
            "brick",
            4200.0);

        ApocalypticSmashableBarrier WallBarrier {};
        WallBarrier.IntactSlotIndex     = 0xFFFFFFFFu; // Pre-scored flush masonry blocks visible from t = 0!
        WallBarrier.TriggerCenterWorld  = WallCenter;
        WallBarrier.OutwardImpulseDir   = { 0.0f, 1.0f, 0.22f };
        WallBarrier.TriggerRadiusMeters = 4.5f;
        WallBarrier.AutoBreachTimeSec   = 5.15f;
        WallBarrier.BurstSpeedMetersSec = 10.5f;
        WallBarrier.GlassCurtain        = false;

        for (std::size_t B = 0u; B < BreachWall.Fragments.size(); ++B)
        {
            auto BlockGeom = std::make_unique<ShowcaseGeometryAsset>();
            BlockGeom->Network.NetworkTitle = "SmashableMasonryBlock_" + std::to_string(B);
            AppendPolyhedronToNetworkWithMaterials(BreachWall.Fragments[B], BlockGeom->Network, {}, 0.0f, 0.0f, 0.0f, MatMasonry, MatConcreteCore);

            ShowcaseWorldAssetEntry BlockEntry {};
            BlockEntry.Title            = "SmashableBreachBlock_" + std::to_string(B + 1u);
            BlockEntry.StableIdentifier = "apoc-breach-block-" + std::to_string(B);
            BlockEntry.MaterialTitle    = MasonryBrick.MaterialName;
            BlockEntry.Subtitle         = "Smashable Wall · Radial Convex Masonry Block";
            BlockEntry.MaterialSlot     = MatMasonry;
            PopulateCenteredAssetEntry(*BlockGeom, BlockEntry, { 0.0f, 0.0f, 0.0f }, 3u);
            const Mathematics::Axis3D InitPos = BlockEntry.CenterWorld;
            const float HalfZ = BlockEntry.DimensionsMeters.Z * 0.5f;
            const uint32_t BlockObjIdx = RegisterDynamicAssetInBundle(OutBundle, std::move(BlockGeom), std::move(BlockEntry), 1u, true);

            ApocalypticFallingFragment Frag {};
            Frag.AssetSlotIndex     = BlockObjIdx;
            Frag.InitialCenterWorld = InitPos;
            Frag.CurrentCenterWorld = InitPos;
            Frag.YawRateDegPerSec   = 110.0f * SignedRandom(Rng);
            Frag.RestElevationZ     = std::max(0.26f, HalfZ);
            Frag.Restitution        = 0.24f;
            Frag.AwaitingImpact     = true;
            Frag.HiddenUntilShatter = false;
            const uint32_t FragSlot = static_cast<uint32_t>(OutSimulation.DynamicFragments.size());
            OutSimulation.DynamicFragments.push_back(Frag);
            WallBarrier.FragmentIndices.push_back(FragSlot);
        }
        OutSimulation.SmashableBarriers.push_back(std::move(WallBarrier));
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 11. Golden-Hour Dramatic Post-Apocalyptic Atmosphere, Sun & Hero Wide-Angle Cinema Camera
    //--------------------------------------------------------------------------------------------------------------------
    auto& Atmo = OutBundle.Scene.Atmosphere;
    Atmo.Medium.AtmosphereEnabled         = true;
    Atmo.Solar.SolarEnabled               = true;
    Atmo.Solar.CycleAnimationEnabled      = false;
    Atmo.Solar.LocalHours                 = 10.35f; // Crisp mid-morning sunlight raking directly into the boulevard, vehicles, deer & towers!
    Atmo.Clouds.CloudsEnabled             = true;
    Atmo.Clouds.CloudFraction             = 0.34f;
    Atmo.Clouds.CloudRenderScale          = 0.50f;
    Atmo.Clouds.CloudMarchSteps           = 32u;
    Atmo.Clouds.FollowWindEnabled         = true;
    Atmo.Clouds.WindSpeedMetersPerSecond  = 4.5f;
    Atmo.Clouds.CloudShadow.ShadowEnabled = false;
    Atmo.LocalCloud.LocalEnabled          = false;
    Atmo.Moons[0].Visible                 = false;
    Atmo.Stars.StarfieldEnabled           = false;

    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Atmo.Solar);
    OutBundle.Scene.Sun.DirectionToSun          = SolarSolved.WorldDirectionToSun;
    OutBundle.Scene.Sun.SolarIrradiance         = { 6.20f, 5.75f, 5.10f };
    OutBundle.Scene.Sun.SunAngularRadiusRadians = 0.0093f;
    OutBundle.Scene.Sun.RtxSamplesPerPixel      = 4u;
    OutBundle.Scene.SkyRadiance                 = { 0.48f, 0.62f, 0.90f };
    OutBundle.Scene.AreaLights.clear();
    OutBundle.Scene.PointLights.clear();
    OutBundle.Scene.PhotometricRadiators.clear();
    OutBundle.Scene.PhotometricProfiles.clear();

    RebuildShowcaseSceneAcceleration(OutBundle);

    // Wide cinematic hero camera framing: Fractured Crater Boulevard + Overturned Wrecks + Roaming Deer + Hero Vehicle +
    // Buckled Overpass Ramp + West & East Fractured Towers + Fallen Steel Skybridge + Hanging Ivy Vines!
    OutBundle.EditorCamera.CameraOrigin       = {  8.8f, -21.5f, 4.45f };
    OutBundle.EditorCamera.TargetPoint        = { -2.4f,   1.8f, 4.10f };
    OutBundle.EditorCamera.WorldUpAxis        = {  0.0f,   0.0f, 1.00f };
    OutBundle.EditorCamera.VerticalFovRadians = 46.0f * Constants::DegToRadFloat;
    OutBundle.EditorCamera.NearClipMeters     = 0.15f;
    OutBundle.EditorCamera.FarClipMeters      = 5000.0f;
    OutBundle.EditorCamera.ViewportWidthPx    = std::max(1u, ViewportWidthPx);
    OutBundle.EditorCamera.ViewportHeightPx   = std::max(1u, ViewportHeightPx);
}

//------------------------------------------------------------------------------------------------------------------------
//                                        ORGANIZED OUTLINER REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

void RegisterApocalypticCityInOutliner(
    OutlinerPanel&            TargetOutliner,
    ShowcaseWorldSceneBundle& InOutBundle,
    bool                      PreserveConstructTemplates)
{
    // Reuse standard Showcase registration first so Camera, Sun, Atmosphere & Construct templates bind 1:1
    RegisterShowcaseGalleryInOutliner(TargetOutliner, InOutBundle, PreserveConstructTemplates);

    if (OutlinerRecordEntry* RootFolder = TargetOutliner.AccessEntry(InOutBundle.ShowcaseFolderToken))
    {
        RootFolder->Title       = "Apocalyptic City Level";
        RootFolder->Subtitle    = "Fractured Towers · Skybridge · Wrecks · Flora · Roaming Deer";
        RootFolder->FolderTint  = { 214u, 128u, 66u, 255u };
        RootFolder->AuthorNotes = "Procedural post-apocalyptic racing level with live noise collapse, smashable glass/walls & roaming wildlife.";
    }

    // Hide initial hidden glass shards in Outliner until their parent glass curtain is smashed
    for (const ShowcaseWorldAssetEntry& Obj : InOutBundle.Objects)
    {
        if (Obj.ClusterInstanceIndex < InOutBundle.Scene.ClusterInstances.size())
        {
            const bool VisibleNow = InOutBundle.Scene.ClusterInstances[Obj.ClusterInstanceIndex].UniformScale > 1.0e-4f;
            if (!VisibleNow && Obj.SlotToken.Valid())
            {
                if (OutlinerRecordEntry* Rec = TargetOutliner.AccessEntry(Obj.SlotToken))
                {
                    Rec->VisibilityEnabled = false;
                }
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                      REAL-TIME WARZONE & WILDLIFE SIMULATION STEP
//------------------------------------------------------------------------------------------------------------------------

bool StepApocalypticCitySimulation(
    ShowcaseWorldSceneBundle&          InOutBundle,
    ApocalypticCitySimulationRegister& InOutSimulation,
    OutlinerPanel*                     OptionalOutliner,
    double                             DeltaSeconds)
{
    if (!InOutSimulation.ActiveCityLevel)
    {
        return false;
    }

    const float Dt = std::clamp(static_cast<float>(DeltaSeconds), 0.0f, 0.05f);
    if (Dt <= 1.0e-5f)
    {
        return false;
    }

    InOutSimulation.ElapsedSeconds += static_cast<double>(Dt);
    const float SimTime = static_cast<float>(InOutSimulation.ElapsedSeconds);
    bool AnyTransformMutated = false;

    auto SynchronizeSimulatedSlot = [&](uint32_t ObjIdx, Mathematics::Axis3D NewPos, float NewYawDeg, bool Visible)
    {
        if (ObjIdx >= InOutBundle.Objects.size())
        {
            return;
        }
        ShowcaseWorldAssetEntry& Obj = InOutBundle.Objects[ObjIdx];
        Obj.CenterWorld              = NewPos;
        Obj.RotationZDegrees         = NewYawDeg;
        if (Obj.ClusterInstanceIndex < InOutBundle.Scene.ClusterInstances.size())
        {
            RigidClusterInstance& Inst = InOutBundle.Scene.ClusterInstances[Obj.ClusterInstanceIndex];
            Inst.PreviousTranslation   = Inst.WorldTranslation;
            Inst.WorldTranslation      = NewPos;
            Inst.RotationZDegrees      = NewYawDeg;
            Inst.UniformScale          = Visible ? 1.0f : 0.0f;
        }
        if (OptionalOutliner != nullptr && Obj.SlotToken.Valid())
        {
            if (OutlinerRecordEntry* Rec = OptionalOutliner->AccessEntry(Obj.SlotToken))
            {
                Rec->PositionX         = NewPos.X;
                Rec->PositionY         = NewPos.Y;
                Rec->PositionZ         = NewPos.Z;
                Rec->RotationZ         = NewYawDeg;
                Rec->VisibilityEnabled = Visible;
            }
        }
        AnyTransformMutated = true;
    };

    // 1. Advance Hero Racing ControlVehicle along the multi-building spline (Street -> Overpass -> Tower A F2/F3 -> Skybridge -> Tower B -> Breach!)
    Mathematics::Axis3D HeroPos { -9999.0f, -9999.0f, 0.0f };
    if (InOutSimulation.HeroVehicleIndex < InOutBundle.Objects.size())
    {
        if (InOutSimulation.HeroAutoDriveEnabled && InOutSimulation.HeroCourseWaypoints.size() >= 2u)
        {
            const std::size_t SegCount = InOutSimulation.HeroCourseWaypoints.size();
            InOutSimulation.HeroSplineProgress = std::fmod(InOutSimulation.HeroSplineProgress + Dt * 0.135f, 1.0f);
            const float Scaled = InOutSimulation.HeroSplineProgress * static_cast<float>(SegCount);
            const std::size_t I0 = static_cast<std::size_t>(std::floor(Scaled)) % SegCount;
            const std::size_t I1 = (I0 + 1u) % SegCount;
            const float LocalT   = SmoothHermite(Scaled - std::floor(Scaled));
            const Mathematics::Axis3D P0 = InOutSimulation.HeroCourseWaypoints[I0];
            const Mathematics::Axis3D P1 = InOutSimulation.HeroCourseWaypoints[I1];
            HeroPos = P0 + (P1 - P0) * LocalT;
            const Mathematics::Axis3D Tang = P1 - P0;
            const float YawDeg = std::atan2(Tang.Y, Tang.X) * Constants::RadToDegFloat;
            SynchronizeSimulatedSlot(InOutSimulation.HeroVehicleIndex, HeroPos, YawDeg, true);
        }
        else
        {
            HeroPos = InOutBundle.Objects[InOutSimulation.HeroVehicleIndex].CenterWorld;
        }
    }

    // 2. Check Smashable Abbe Glass Curtains & Masonry Breach Walls (triggered by Hero Vehicle impact OR shockwave timer!)
    for (ApocalypticSmashableBarrier& Barrier : InOutSimulation.SmashableBarriers)
    {
        if (Barrier.Triggered)
        {
            continue;
        }
        const float Dx = HeroPos.X - Barrier.TriggerCenterWorld.X;
        const float Dy = HeroPos.Y - Barrier.TriggerCenterWorld.Y;
        const float Dz = HeroPos.Z - Barrier.TriggerCenterWorld.Z;
        const float DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
        const bool  VehicleHit = DistSq <= (Barrier.TriggerRadiusMeters * Barrier.TriggerRadiusMeters);
        const bool  ShockwaveHit = SimTime >= Barrier.AutoBreachTimeSec;

        if (VehicleHit || ShockwaveHit)
        {
            Barrier.Triggered = true;
            if (Barrier.GlassCurtain)
            {
                ++InOutSimulation.ShatteredGlassCount;
            }
            else
            {
                ++InOutSimulation.BreachedWallCount;
            }

            if (Barrier.IntactSlotIndex < InOutBundle.Objects.size())
            {
                const auto& IntactObj = InOutBundle.Objects[Barrier.IntactSlotIndex];
                SynchronizeSimulatedSlot(Barrier.IntactSlotIndex, IntactObj.CenterWorld, IntactObj.RotationZDegrees, false);
            }

            for (uint32_t FragSlot : Barrier.FragmentIndices)
            {
                if (FragSlot >= InOutSimulation.DynamicFragments.size())
                {
                    continue;
                }
                ApocalypticFallingFragment& Frag = InOutSimulation.DynamicFragments[FragSlot];
                Frag.AwaitingImpact     = false;
                Frag.HiddenUntilShatter = false;
                Frag.Detached           = true;
                Frag.Settled            = false;
                const Mathematics::Axis3D Radial = Mathematics::NormalizeAxis(
                    Frag.InitialCenterWorld - Barrier.TriggerCenterWorld, { 0.0f, 0.0f, 1.0f });
                Frag.VelocityMetersSec =
                    Barrier.OutwardImpulseDir * Barrier.BurstSpeedMetersSec +
                    Radial * (Barrier.BurstSpeedMetersSec * 0.45f) +
                    Mathematics::Axis3D{ 0.0f, 0.0f, 2.2f };
                ++InOutSimulation.DetachedFragmentCount;
            }
        }
    }

    // 3. Step Seismic-Noise Building Collapse, Collapsing Floor Bays & Falling Shards/Masonry Debris
    for (ApocalypticFallingFragment& Frag : InOutSimulation.DynamicFragments)
    {
        if (Frag.AwaitingImpact || Frag.Settled)
        {
            continue;
        }
        if (!Frag.Detached)
        {
            const float NoisePulse = FractalSeismicNoise(
                Frag.InitialCenterWorld + Mathematics::Axis3D{ Frag.NoiseSeedOffset, 0.0f, 0.0f }, SimTime);
            if (NoisePulse > 0.48f)
            {
                Frag.AccumulatedFatigue += (NoisePulse - 0.48f) * Dt * 2.4f;
            }
            if (Frag.AccumulatedFatigue >= Frag.StructuralThreshold)
            {
                Frag.Detached = true;
                ++InOutSimulation.DetachedFragmentCount;
            }
            else
            {
                // Subtle pre-collapse structural tremor before snapping free!
                const float Tremor = 0.035f * (Frag.AccumulatedFatigue / std::max(0.1f, Frag.StructuralThreshold));
                const Mathematics::Axis3D ShakePos {
                    Frag.InitialCenterWorld.X + Tremor * std::sin(SimTime * 28.0f + Frag.NoiseSeedOffset),
                    Frag.InitialCenterWorld.Y + Tremor * std::cos(SimTime * 24.0f + Frag.NoiseSeedOffset),
                    Frag.InitialCenterWorld.Z,
                };
                SynchronizeSimulatedSlot(Frag.AssetSlotIndex, ShakePos, Frag.InitialYawDegrees, true);
                continue;
            }
        }

        Frag.VelocityMetersSec.Z -= 9.81f * Dt;
        Frag.CurrentCenterWorld   = Frag.CurrentCenterWorld + Frag.VelocityMetersSec * Dt;
        Frag.CurrentYawDegrees   += Frag.YawRateDegPerSec * Dt;

        if (Frag.CurrentCenterWorld.Z <= Frag.RestElevationZ)
        {
            Frag.CurrentCenterWorld.Z = Frag.RestElevationZ;
            if (std::fabs(Frag.VelocityMetersSec.Z) > 1.35f)
            {
                Frag.VelocityMetersSec.Z = -Frag.VelocityMetersSec.Z * Frag.Restitution;
                Frag.VelocityMetersSec.X *= 0.62f;
                Frag.VelocityMetersSec.Y *= 0.62f;
                Frag.YawRateDegPerSec    *= 0.55f;
            }
            else
            {
                Frag.VelocityMetersSec = { 0.0f, 0.0f, 0.0f };
                Frag.YawRateDegPerSec  = 0.0f;
                Frag.Settled           = true;
            }
        }
        SynchronizeSimulatedSlot(Frag.AssetSlotIndex, Frag.CurrentCenterWorld, Frag.CurrentYawDegrees, !Frag.HiddenUntilShatter);
    }

    // 4. Step Freely Roaming Obstacle-Avoiding Woodland Deer Herd
    for (ApocalypticDeerWanderer& Deer : InOutSimulation.RoamingDeer)
    {
        const float SteerNoise = FractalSeismicNoise(
            { Deer.WanderSeed, SimTime * 0.45f, Deer.PositionWorld.X * 0.08f }, SimTime * 0.25f) - 0.5f;
        Deer.HeadingDegrees += SteerNoise * 75.0f * Dt;

        // Soft boundary & building/crater/vehicle repulsion so deer stay cleanly on the open overgrown avenues
        Mathematics::Axis3D Repel { 0.0f, 0.0f, 0.0f };
        if (Deer.PositionWorld.X < -7.8f) { Repel.X += (-7.8f - Deer.PositionWorld.X) * 1.8f; }
        if (Deer.PositionWorld.X >  7.8f) { Repel.X -= (Deer.PositionWorld.X - 7.8f) * 1.8f; }
        if (Deer.PositionWorld.Y < -22.0f) { Repel.Y += (-22.0f - Deer.PositionWorld.Y) * 1.5f; }
        if (Deer.PositionWorld.Y >  24.0f) { Repel.Y -= (Deer.PositionWorld.Y - 24.0f) * 1.5f; }

        // Avoid central sinkhole pit (1.2, 1.6) & Hero Vehicle
        const Mathematics::Axis3D ToPit { Deer.PositionWorld.X - 1.2f, Deer.PositionWorld.Y - 1.6f, 0.0f };
        const float PitDist = std::sqrt(ToPit.X * ToPit.X + ToPit.Y * ToPit.Y);
        if (PitDist < 6.2f && PitDist > 1.0e-3f)
        {
            Repel = Repel + ToPit * ((6.2f - PitDist) / PitDist * 2.4f);
        }
        const Mathematics::Axis3D ToCar { Deer.PositionWorld.X - HeroPos.X, Deer.PositionWorld.Y - HeroPos.Y, 0.0f };
        const float CarDist = std::sqrt(ToCar.X * ToCar.X + ToCar.Y * ToCar.Y);
        float SpeedBoost = 1.0f;
        if (CarDist < 8.5f && CarDist > 1.0e-3f)
        {
            Repel = Repel + ToCar * ((8.5f - CarDist) / CarDist * 4.0f);
            SpeedBoost = 1.75f; // Alert bounding trot away from approaching race car!
        }

        const float CurRad = Deer.HeadingDegrees * Constants::DegToRadFloat;
        Mathematics::Axis3D DesiredDir {
            std::cos(CurRad) + Repel.X * 0.45f,
            std::sin(CurRad) + Repel.Y * 0.45f,
            0.0f,
        };
        DesiredDir = Mathematics::NormalizeAxis(DesiredDir, { 1.0f, 0.0f, 0.0f });
        Deer.HeadingDegrees = std::atan2(DesiredDir.Y, DesiredDir.X) * Constants::RadToDegFloat;

        const float ActiveSpeed = Deer.CruiseSpeedMetersSec * SpeedBoost;
        Deer.PositionWorld.X   += DesiredDir.X * ActiveSpeed * Dt;
        Deer.PositionWorld.Y   += DesiredDir.Y * ActiveSpeed * Dt;
        Deer.StridePhaseRadians = std::fmod(Deer.StridePhaseRadians + ActiveSpeed * Dt * 4.4f, 2.0f * Constants::PiFloat);
        Deer.PositionWorld.Z    = Deer.RestElevationZ + 0.065f * std::fabs(std::sin(Deer.StridePhaseRadians));

        SynchronizeSimulatedSlot(Deer.AssetSlotIndex, Deer.PositionWorld, Deer.HeadingDegrees, true);
    }

    return AnyTransformMutated;
}

} // namespace Frontier::GeometricRaster
