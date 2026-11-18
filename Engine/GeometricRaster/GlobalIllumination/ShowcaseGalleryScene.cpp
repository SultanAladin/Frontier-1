/*============================================================================================================================================
                                                           SHOWCASEGALLERYSCENE.CPP
============================================================================================================================================*/
// 🧩 Canonical Architectural Cornell-Studio Gallery World Scene, EditorCamera, and OutlinerPanel bidirectional registration

#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"

#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>

namespace Frontier::GeometricRaster
{

namespace
{

void BuildBoxGeometryAsset(ShowcaseGeometryAsset& OutAsset, Mathematics::Scalar3D HalfExtents)
{
    OutAsset = {};
    GeometrySpace::AppendBoxPrimitive(OutAsset.Network, { 0.0f, 0.0f, 0.0f }, HalfExtents, {}, {});
    (void)OutAsset.Pyramid.BuildFromPolygonNetwork(OutAsset.Network, 3u);
}

void BuildSphereGeometryAsset(ShowcaseGeometryAsset& OutAsset, float Radius, uint32_t Sectors, uint32_t Rings)
{
    OutAsset = {};
    GeometrySpace::AppendSpherePrimitive(OutAsset.Network, { 0.0f, 0.0f, 0.0f }, Radius, Sectors, Rings, {}, {});
    OutAsset.Network.LinkOppositeEdges();
    OutAsset.Network.RecomputeNormals();
    (void)OutAsset.Pyramid.BuildFromPolygonNetwork(OutAsset.Network, 3u);
}

void BuildShapeGeometryAsset(
    ShowcaseGeometryAsset& OutAsset,
    ShowcasePrimitiveShape Shape,
    Mathematics::Scalar3D  FullDimensions)
{
    OutAsset = {};
    const float HalfX = std::max(0.05f, FullDimensions.X * 0.5f);
    const float HalfY = std::max(0.05f, FullDimensions.Y * 0.5f);
    const float HalfZ = std::max(0.05f, FullDimensions.Z * 0.5f);

    switch (Shape)
    {
        case ShowcasePrimitiveShape::Sphere:
        {
            const float Radius = std::max(0.05f, std::max({ HalfX, HalfY, HalfZ }));
            GeometrySpace::AppendSpherePrimitive(OutAsset.Network, { 0.0f, 0.0f, 0.0f }, Radius, 32u, 20u, {}, {});
            break;
        }
        case ShowcasePrimitiveShape::Cylinder:
        {
            const float Radius = std::max(0.05f, 0.5f * (HalfX + HalfY));
            GeometrySpace::AppendCylinderFrustumPrimitive(
                OutAsset.Network,
                { 0.0f, 0.0f, -HalfZ },
                { 0.0f, 0.0f,  HalfZ },
                Radius,
                Radius,
                28u,
                {},
                {});
            break;
        }
        case ShowcasePrimitiveShape::Cone:
        {
            const float Radius = std::max(0.05f, 0.5f * (HalfX + HalfY));
            GeometrySpace::AppendCylinderFrustumPrimitive(
                OutAsset.Network,
                { 0.0f, 0.0f, -HalfZ },
                { 0.0f, 0.0f,  HalfZ },
                Radius,
                0.015f,
                28u,
                {},
                {});
            break;
        }
        case ShowcasePrimitiveShape::Torus:
        {
            const float OuterR = std::max(0.10f, 0.5f * (HalfX + HalfY));
            const float MinorR = std::clamp(HalfZ, 0.04f, OuterR * 0.45f);
            const float MajorR = std::max(MinorR + 0.03f, OuterR - MinorR);
            GeometrySpace::AppendTorusRingPrimitive(
                OutAsset.Network,
                { 0.0f, 0.0f, 0.0f },
                MajorR,
                MinorR,
                32u,
                18u,
                {},
                {});
            break;
        }
        case ShowcasePrimitiveShape::Box:
        default:
        {
            GeometrySpace::AppendBoxPrimitive(OutAsset.Network, { 0.0f, 0.0f, 0.0f }, { HalfX, HalfY, HalfZ }, {}, {});
            break;
        }
    }

    OutAsset.Network.LinkOppositeEdges();
    OutAsset.Network.RecomputeNormals();
    (void)OutAsset.Pyramid.BuildFromPolygonNetwork(OutAsset.Network, 3u);
}

struct LocalQuantizedKey
{
    int32_t QX = 0;
    int32_t QY = 0;
    int32_t QZ = 0;

    [[nodiscard]] bool operator==(const LocalQuantizedKey& Rhs) const noexcept
    {
        return QX == Rhs.QX && QY == Rhs.QY && QZ == Rhs.QZ;
    }

    [[nodiscard]] bool operator<(const LocalQuantizedKey& Rhs) const noexcept
    {
        if (QX != Rhs.QX) return QX < Rhs.QX;
        if (QY != Rhs.QY) return QY < Rhs.QY;
        return QZ < Rhs.QZ;
    }
};

struct LocalCornerTriEntry
{
    LocalQuantizedKey Key      {};
    uint32_t          TriIndex = 0u;

    [[nodiscard]] bool operator<(const LocalCornerTriEntry& Rhs) const noexcept
    {
        if (Key == Rhs.Key)
        {
            return TriIndex < Rhs.TriIndex;
        }
        return Key < Rhs.Key;
    }
};

[[nodiscard]] LocalQuantizedKey QuantizeLocalCorner(Mathematics::Axis3D Pos) noexcept
{
    return {
        static_cast<int32_t>(std::lround(Pos.X * 2048.0f)),
        static_cast<int32_t>(std::lround(Pos.Y * 2048.0f)),
        static_cast<int32_t>(std::lround(Pos.Z * 2048.0f)),
    };
}

void EnsureObjectLocalSmoothTriangles(ShowcaseWorldObjectEntry& Obj)
{
    if (Obj.GlassShell || !Obj.LocalSmoothTriangles.empty())
    {
        return;
    }

    GeometrySpace::PolygonNetwork LocalNet {};
    const float HalfX = std::max(0.04f, Obj.DimensionsMeters.X * 0.5f);
    const float HalfY = std::max(0.04f, Obj.DimensionsMeters.Y * 0.5f);
    const float HalfZ = std::max(0.04f, Obj.DimensionsMeters.Z * 0.5f);
    const Mathematics::Axis3D Origin { 0.0f, 0.0f, 0.0f };

    switch (Obj.PrimitiveShape)
    {
        case ShowcasePrimitiveShape::Sphere:
            GeometrySpace::AppendSpherePrimitive(LocalNet, Origin, std::max({ HalfX, HalfY, HalfZ }), 28u, 18u, {}, {}, 0u, Obj.MaterialSlot);
            break;
        case ShowcasePrimitiveShape::Cylinder:
            GeometrySpace::AppendCylinderFrustumPrimitive(
                LocalNet,
                { 0.0f, 0.0f, -HalfZ },
                { 0.0f, 0.0f,  HalfZ },
                0.5f * (HalfX + HalfY),
                0.5f * (HalfX + HalfY),
                24u,
                {},
                {},
                0u,
                Obj.MaterialSlot);
            break;
        case ShowcasePrimitiveShape::Cone:
            GeometrySpace::AppendCylinderFrustumPrimitive(
                LocalNet,
                { 0.0f, 0.0f, -HalfZ },
                { 0.0f, 0.0f,  HalfZ },
                0.5f * (HalfX + HalfY),
                0.015f,
                24u,
                {},
                {},
                0u,
                Obj.MaterialSlot);
            break;
        case ShowcasePrimitiveShape::Torus:
        {
            const float OuterR = std::max(0.10f, 0.5f * (HalfX + HalfY));
            const float MinorR = std::clamp(HalfZ, 0.04f, OuterR * 0.45f);
            const float MajorR = std::max(MinorR + 0.03f, OuterR - MinorR);
            GeometrySpace::AppendTorusRingPrimitive(LocalNet, Origin, MajorR, MinorR, 28u, 16u, {}, {}, 0u, Obj.MaterialSlot);
            break;
        }
        case ShowcasePrimitiveShape::Box:
        default:
            GeometrySpace::AppendBoxPrimitive(LocalNet, Origin, { HalfX, HalfY, HalfZ }, {}, {}, 0u, Obj.MaterialSlot);
            break;
    }

    struct RawTri
    {
        Mathematics::Axis3D P0 {};
        Mathematics::Axis3D P1 {};
        Mathematics::Axis3D P2 {};
        Mathematics::Axis3D AreaNormal {};
    };
    std::vector<RawTri> RawTris;
    RawTris.reserve(LocalNet.Faces.size() * 2u);

    for (const GeometrySpace::Face& CurrentFace : LocalNet.Faces)
    {
        if (CurrentFace.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex || CurrentFace.CornerCount < 3u)
        {
            continue;
        }
        std::vector<uint32_t> LoopVerts;
        LoopVerts.reserve(CurrentFace.CornerCount);
        uint32_t EdgeIdx = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < CurrentFace.CornerCount && EdgeIdx != GeometrySpace::InvalidNetworkIndex; ++Step)
        {
            const auto& Edge = LocalNet.Edges[EdgeIdx];
            LoopVerts.push_back(Edge.OriginVertexIndex);
            EdgeIdx = Edge.NextEdgeIndex;
            if (EdgeIdx == CurrentFace.BoundaryLoopEdgeIndex)
            {
                break;
            }
        }
        if (LoopVerts.size() < 3u)
        {
            continue;
        }
        const Mathematics::Axis3D P0 = LocalNet.Vertices[LoopVerts[0u]].Axes;
        for (std::size_t FanIdx = 1u; FanIdx + 1u < LoopVerts.size(); ++FanIdx)
        {
            const Mathematics::Axis3D P1 = LocalNet.Vertices[LoopVerts[FanIdx]].Axes;
            const Mathematics::Axis3D P2 = LocalNet.Vertices[LoopVerts[FanIdx + 1u]].Axes;
            const Mathematics::Axis3D AreaN = Mathematics::EvaluateCross(P1 - P0, P2 - P0);
            if (Mathematics::EvaluateLengthSquared(AreaN) <= 1.0e-12f)
            {
                continue;
            }
            RawTris.push_back({ P0, P1, P2, AreaN });
        }
    }

    if (RawTris.empty())
    {
        return;
    }

    // Precompute 49-degree crease-angle smooth Phong vertex normals ONCE in local object space
    std::vector<LocalCornerTriEntry> CornerEntries;
    CornerEntries.reserve(RawTris.size() * 3u);
    for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(RawTris.size()); ++Idx)
    {
        CornerEntries.push_back({ QuantizeLocalCorner(RawTris[Idx].P0), Idx });
        CornerEntries.push_back({ QuantizeLocalCorner(RawTris[Idx].P1), Idx });
        CornerEntries.push_back({ QuantizeLocalCorner(RawTris[Idx].P2), Idx });
    }
    std::sort(CornerEntries.begin(), CornerEntries.end());

    Obj.LocalSmoothTriangles.resize(RawTris.size());
    for (std::size_t Idx = 0u; Idx < RawTris.size(); ++Idx)
    {
        const RawTri& Tri = RawTris[Idx];
        const Mathematics::Axis3D UnitFaceN = Mathematics::NormalizeAxis(Tri.AreaNormal, { 0.0f, 0.0f, 1.0f });
        const std::array<Mathematics::Axis3D, 3u> Corners {{ Tri.P0, Tri.P1, Tri.P2 }};
        std::array<Mathematics::Axis3D, 3u> SmoothN {};

        for (std::size_t CornerIdx = 0u; CornerIdx < 3u; ++CornerIdx)
        {
            Mathematics::Axis3D SumN { 0.0f, 0.0f, 0.0f };
            const LocalCornerTriEntry Search { QuantizeLocalCorner(Corners[CornerIdx]), 0u };
            auto Iter = std::lower_bound(
                CornerEntries.begin(),
                CornerEntries.end(),
                Search,
                [](const LocalCornerTriEntry& Lhs, const LocalCornerTriEntry& Rhs) noexcept
                {
                    return Lhs.Key < Rhs.Key;
                });
            while (Iter != CornerEntries.end() && Iter->Key == Search.Key)
            {
                const Mathematics::Axis3D PeerAreaN = RawTris[Iter->TriIndex].AreaNormal;
                const Mathematics::Axis3D PeerUnitN = Mathematics::NormalizeAxis(PeerAreaN, UnitFaceN);
                if (Mathematics::EvaluateDot(UnitFaceN, PeerUnitN) > 0.65f)
                {
                    SumN = SumN + PeerAreaN;
                }
                ++Iter;
            }
            SmoothN[CornerIdx] = Mathematics::NormalizeAxis(SumN, UnitFaceN);
        }

        Obj.LocalSmoothTriangles[Idx] = {
            Tri.P0,
            Tri.P1,
            Tri.P2,
            SmoothN[0u],
            SmoothN[1u],
            SmoothN[2u],
        };
    }
}

[[nodiscard]] Mathematics::Axis3D RotateAroundZ(Mathematics::Axis3D Vec, float CosZ, float SinZ) noexcept
{
    return {
        Vec.X * CosZ - Vec.Y * SinZ,
        Vec.X * SinZ + Vec.Y * CosZ,
        Vec.Z,
    };
}

[[nodiscard]] bool NearlyEqualFloat(float Left, float Right) noexcept
{
    return std::fabs(Left - Right) <= 1.0e-4f;
}

[[nodiscard]] float SrgbByteToLinear(uint8_t ByteVal) noexcept
{
    const float S = static_cast<float>(ByteVal) / 255.0f;
    return (S <= 0.04045f) ? (S / 12.92f) : std::pow((S + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view Haystack, std::string_view Needle) noexcept
{
    if (Needle.empty() || Haystack.size() < Needle.size())
    {
        return false;
    }
    for (std::size_t Pos = 0u; Pos + Needle.size() <= Haystack.size(); ++Pos)
    {
        bool Match = true;
        for (std::size_t K = 0u; K < Needle.size(); ++K)
        {
            if (std::tolower(static_cast<unsigned char>(Haystack[Pos + K])) !=
                std::tolower(static_cast<unsigned char>(Needle[K])))
            {
                Match = false;
                break;
            }
        }
        if (Match)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] ShowcasePrimitiveShape InferShapeFromRecord(const OutlinerRecordEntry& Entry) noexcept
{
    if (ContainsCaseInsensitive(Entry.Title, "torus") || ContainsCaseInsensitive(Entry.Subtitle, "torus"))
    {
        return ShowcasePrimitiveShape::Torus;
    }
    if (ContainsCaseInsensitive(Entry.Title, "cone") || ContainsCaseInsensitive(Entry.Subtitle, "cone"))
    {
        return ShowcasePrimitiveShape::Cone;
    }
    if (ContainsCaseInsensitive(Entry.Title, "cylinder") || ContainsCaseInsensitive(Entry.Subtitle, "cylinder"))
    {
        return ShowcasePrimitiveShape::Cylinder;
    }
    if (ContainsCaseInsensitive(Entry.Title, "sphere") || ContainsCaseInsensitive(Entry.Subtitle, "sphere") ||
        ContainsCaseInsensitive(Entry.Title, "orb"))
    {
        return ShowcasePrimitiveShape::Sphere;
    }
    return ShowcasePrimitiveShape::Box;
}

[[nodiscard]] Mathematics::Scalar3D DefaultDimensionsForShape(ShowcasePrimitiveShape Shape) noexcept
{
    switch (Shape)
    {
        case ShowcasePrimitiveShape::Sphere:   return { 0.72f, 0.72f, 0.72f };
        case ShowcasePrimitiveShape::Cylinder: return { 0.68f, 0.68f, 0.84f };
        case ShowcasePrimitiveShape::Cone:     return { 0.74f, 0.74f, 0.88f };
        case ShowcasePrimitiveShape::Torus:    return { 0.86f, 0.86f, 0.28f };
        case ShowcasePrimitiveShape::Box:
        default:                               return { 0.72f, 0.72f, 0.72f };
    }
}

[[nodiscard]] const char* ShapeDisplayLabel(ShowcasePrimitiveShape Shape) noexcept
{
    switch (Shape)
    {
        case ShowcasePrimitiveShape::Sphere:   return "Sphere";
        case ShowcasePrimitiveShape::Cylinder: return "Cylinder";
        case ShowcasePrimitiveShape::Cone:     return "Cone";
        case ShowcasePrimitiveShape::Torus:    return "Torus";
        case ShowcasePrimitiveShape::Box:
        default:                               return "Box";
    }
}

void BuildCameraRay(
    const PerspectiveCameraFrame& Cam,
    float                         NormalizedU,
    float                         NormalizedV,
    Mathematics::Axis3D&          OutOrigin,
    Mathematics::Axis3D&          OutDirection) noexcept
{
    OutOrigin = Cam.CameraOrigin;
    const Mathematics::Axis3D Fwd   = Mathematics::NormalizeAxis(Cam.TargetPoint - Cam.CameraOrigin, { 0.0f, 1.0f, 0.0f });
    const Mathematics::Axis3D Right = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, Cam.WorldUpAxis), { 1.0f, 0.0f, 0.0f });
    const Mathematics::Axis3D Up    = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Right, Fwd), { 0.0f, 0.0f, 1.0f });

    const float Aspect  = static_cast<float>(std::max(1u, Cam.ViewportWidthPx)) / static_cast<float>(std::max(1u, Cam.ViewportHeightPx));
    const float TanHalf = std::tan(std::clamp(Cam.VerticalFovRadians, 0.10f, 2.60f) * 0.5f);
    const float NdcX    = (NormalizedU * 2.0f - 1.0f) * Aspect * TanHalf;
    const float NdcY    = (1.0f - NormalizedV * 2.0f) * TanHalf;

    OutDirection = Mathematics::NormalizeAxis(Fwd + Right * NdcX + Up * NdcY, Fwd);
}

[[nodiscard]] bool IntersectRayAabb(
    Mathematics::Axis3D   Origin,
    Mathematics::Axis3D   Dir,
    Mathematics::Axis3D   Center,
    Mathematics::Scalar3D HalfExtents,
    float&                OutDistance) noexcept
{
    float TMin = 0.01f;
    float TMax = 1.0e6f;

    const float Orig[3] = { Origin.X, Origin.Y, Origin.Z };
    const float RayD[3] = { Dir.X, Dir.Y, Dir.Z };
    const float BMin[3] = { Center.X - HalfExtents.X, Center.Y - HalfExtents.Y, Center.Z - HalfExtents.Z };
    const float BMax[3] = { Center.X + HalfExtents.X, Center.Y + HalfExtents.Y, Center.Z + HalfExtents.Z };

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        if (std::fabs(RayD[Axis]) < 1.0e-6f)
        {
            if (Orig[Axis] < BMin[Axis] || Orig[Axis] > BMax[Axis])
            {
                return false;
            }
        }
        else
        {
            const float InvD = 1.0f / RayD[Axis];
            float       T1   = (BMin[Axis] - Orig[Axis]) * InvD;
            float       T2   = (BMax[Axis] - Orig[Axis]) * InvD;
            if (T1 > T2)
            {
                std::swap(T1, T2);
            }
            TMin = std::max(TMin, T1);
            TMax = std::min(TMax, T2);
            if (TMin > TMax)
            {
                return false;
            }
        }
    }
    OutDistance = TMin;
    return true;
}

[[nodiscard]] bool IntersectRaySphere(
    Mathematics::Axis3D Origin,
    Mathematics::Axis3D Dir,
    Mathematics::Axis3D Center,
    float               Radius,
    float&              OutDistance) noexcept
{
    const Mathematics::Axis3D Oc = Origin - Center;
    const float               B  = Mathematics::EvaluateDot(Oc, Dir);
    const float               C  = Mathematics::EvaluateDot(Oc, Oc) - Radius * Radius;
    const float               Disc = B * B - C;
    if (Disc < 0.0f)
    {
        return false;
    }
    const float SqrtD = std::sqrt(Disc);
    float       T     = -B - SqrtD;
    if (T < 0.01f)
    {
        T = -B + SqrtD;
    }
    if (T < 0.01f)
    {
        return false;
    }
    OutDistance = T;
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           CANONICAL GALLERY SCENE BUILDER
//------------------------------------------------------------------------------------------------------------------------

void BuildShowcaseGalleryScene(
    ShowcaseWorldSceneBundle& OutBundle,
    uint32_t                  ViewportWidthPx,
    uint32_t                  ViewportHeightPx,
    bool                      BuildCpuSdf)
{
    OutBundle = {};

    MaterialSpace::SurfaceMaterial NeutralPlaster {};
    NeutralPlaster.MaterialName             = "GalleryNeutralPlaster";
    NeutralPlaster.Diffuse.BaseColor        = { 0.78f, 0.77f, 0.75f };
    NeutralPlaster.Diffuse.DiffuseRoughness = 0.55f;
    NeutralPlaster.Specular.Roughness       = 0.65f;

    MaterialSpace::SurfaceMaterial CrimsonWall {};
    CrimsonWall.MaterialName                = "GalleryCrimsonWall";
    CrimsonWall.Diffuse.BaseColor           = { 0.86f, 0.08f, 0.07f };
    CrimsonWall.Diffuse.DiffuseRoughness    = 0.50f;
    CrimsonWall.Specular.Roughness          = 0.55f;

    MaterialSpace::SurfaceMaterial EmeraldWall {};
    EmeraldWall.MaterialName                = "GalleryEmeraldWall";
    EmeraldWall.Diffuse.BaseColor           = { 0.09f, 0.80f, 0.17f };
    EmeraldWall.Diffuse.DiffuseRoughness    = 0.50f;
    EmeraldWall.Specular.Roughness          = 0.55f;

    MaterialSpace::SurfaceMaterial MirrorChrome {};
    MirrorChrome.MaterialName               = "PolishedMirrorChrome";
    MirrorChrome.Diffuse.BaseColor          = { 0.94f, 0.95f, 0.97f };
    MirrorChrome.Specular.Metalness         = 1.0f;
    MirrorChrome.Specular.Roughness         = 0.035f;
    MirrorChrome.Specular.EdgeTintF82       = { 0.98f, 0.98f, 0.99f };

    MaterialSpace::SurfaceMaterial BrushedGold {};
    BrushedGold.MaterialName                = "AnisotropicGoldPedestal";
    BrushedGold.Diffuse.BaseColor           = { 0.96f, 0.76f, 0.32f };
    BrushedGold.Specular.Metalness          = 1.0f;
    BrushedGold.Specular.Roughness          = 0.18f;
    BrushedGold.Specular.Anisotropy         = 0.55f;
    BrushedGold.Specular.EdgeTintF82        = { 1.00f, 0.90f, 0.60f };

    MaterialSpace::SurfaceMaterial ClearcoatCobalt {};
    ClearcoatCobalt.MaterialName            = "ClearcoatCobaltArch";
    ClearcoatCobalt.Diffuse.BaseColor       = { 0.10f, 0.25f, 0.78f };
    ClearcoatCobalt.Specular.Roughness      = 0.28f;
    ClearcoatCobalt.Clearcoat.CoatWeight    = 1.0f;
    ClearcoatCobalt.Clearcoat.CoatRoughness = 0.04f;

    MaterialSpace::SurfaceMaterial AbbeCrownGlass {};
    AbbeCrownGlass.MaterialName                      = "AbbeCrownGlassOrb";
    AbbeCrownGlass.Diffuse.BaseWeight                = 0.0f;
    AbbeCrownGlass.Specular.Roughness                = 0.02f;
    AbbeCrownGlass.Specular.DielectricIOR            = 1.52f;
    AbbeCrownGlass.Transmission.TransmissionWeight   = 1.0f;
    AbbeCrownGlass.Transmission.AttenuationColor     = { 0.95f, 0.98f, 1.00f };
    AbbeCrownGlass.Transmission.AbbeDispersionNumber = 26.0f;

    const uint16_t SlotPlaster = OutBundle.Scene.MaterialRegister.RegisterMaterial(NeutralPlaster);
    const uint16_t SlotCrimson = OutBundle.Scene.MaterialRegister.RegisterMaterial(CrimsonWall);
    const uint16_t SlotEmerald = OutBundle.Scene.MaterialRegister.RegisterMaterial(EmeraldWall);
    const uint16_t SlotChrome  = OutBundle.Scene.MaterialRegister.RegisterMaterial(MirrorChrome);
    const uint16_t SlotGold    = OutBundle.Scene.MaterialRegister.RegisterMaterial(BrushedGold);
    const uint16_t SlotCobalt  = OutBundle.Scene.MaterialRegister.RegisterMaterial(ClearcoatCobalt);
    const uint16_t SlotGlass   = OutBundle.Scene.MaterialRegister.RegisterMaterial(AbbeCrownGlass);

    BuildBoxGeometryAsset(OutBundle.GeometryAssets[0u],    { 2.80f, 2.40f, 0.08f });
    BuildBoxGeometryAsset(OutBundle.GeometryAssets[1u],    { 2.80f, 0.08f, 1.45f });
    BuildBoxGeometryAsset(OutBundle.GeometryAssets[2u],    { 0.08f, 2.20f, 1.45f });
    BuildBoxGeometryAsset(OutBundle.GeometryAssets[3u],    { 1.35f, 1.15f, 0.07f });
    BuildBoxGeometryAsset(OutBundle.GeometryAssets[4u],    { 0.46f, 0.46f, 0.24f });
    BuildSphereGeometryAsset(OutBundle.GeometryAssets[5u], 0.52f, 36u, 22u);
    BuildSphereGeometryAsset(OutBundle.GeometryAssets[6u], 0.44f, 32u, 20u);
    BuildSphereGeometryAsset(OutBundle.GeometryAssets[7u], 0.40f, 32u, 20u);

    const auto AppendBoxEntry = [&](
        std::size_t           AssetOrdinal,
        const char*           Title,
        const char*           StableId,
        const char*           MaterialTitle,
        const char*           Subtitle,
        const char*           Notes,
        Mathematics::Axis3D   Center,
        Mathematics::Scalar3D HalfExtents,
        uint16_t              Slot,
        bool                  GlassShell)
    {
        const uint32_t InstanceIdx = static_cast<uint32_t>(OutBundle.Scene.ClusterInstances.size());
        OutBundle.Scene.ClusterInstances.push_back(
            { &OutBundle.GeometryAssets[AssetOrdinal].Pyramid, Center, Center, 1.0f, Slot, GlassShell });

        ShowcaseWorldObjectEntry Record {};
        Record.Title                = Title;
        Record.StableIdentifier     = StableId;
        Record.MaterialTitle        = MaterialTitle;
        Record.Subtitle             = Subtitle;
        Record.AuthorNotes          = Notes;
        Record.CenterWorld          = Center;
        Record.DimensionsMeters     = { HalfExtents.X * 2.0f, HalfExtents.Y * 2.0f, HalfExtents.Z * 2.0f };
        Record.AuthoredDimensions   = Record.DimensionsMeters;
        Record.MaterialSlot         = Slot;
        Record.ClusterInstanceIndex = InstanceIdx;
        Record.GlassShell           = GlassShell;
        Record.SphericalPrimitive   = false;
        Record.PrimitiveShape       = ShowcasePrimitiveShape::Box;
        OutBundle.Objects.push_back(std::move(Record));
    };

    const auto AppendSphereEntry = [&](
        std::size_t         AssetOrdinal,
        const char*         Title,
        const char*         StableId,
        const char*         MaterialTitle,
        const char*         Subtitle,
        const char*         Notes,
        Mathematics::Axis3D Center,
        float               Radius,
        uint32_t            /*Sectors*/,
        uint32_t            /*Rings*/,
        uint16_t            Slot,
        bool                GlassShell)
    {
        const uint32_t InstanceIdx = static_cast<uint32_t>(OutBundle.Scene.ClusterInstances.size());
        OutBundle.Scene.ClusterInstances.push_back(
            { &OutBundle.GeometryAssets[AssetOrdinal].Pyramid, Center, Center, 1.0f, Slot, GlassShell });

        const float Diameter = Radius * 2.0f;
        ShowcaseWorldObjectEntry Record {};
        Record.Title                = Title;
        Record.StableIdentifier     = StableId;
        Record.MaterialTitle        = MaterialTitle;
        Record.Subtitle             = Subtitle;
        Record.AuthorNotes          = Notes;
        Record.CenterWorld          = Center;
        Record.DimensionsMeters     = { Diameter, Diameter, Diameter };
        Record.AuthoredDimensions   = Record.DimensionsMeters;
        Record.MaterialSlot         = Slot;
        Record.ClusterInstanceIndex = InstanceIdx;
        Record.GlassShell           = GlassShell;
        Record.SphericalPrimitive   = true;
        Record.PrimitiveShape       = ShowcasePrimitiveShape::Sphere;
        OutBundle.Objects.push_back(std::move(Record));
    };

    AppendBoxEntry(
        0u,
        "Gallery Floor",
        "gallery-floor",
        "GalleryNeutralPlaster",
        "Box · 5.60 × 4.80 × 0.16 m · Neutral Plaster",
        "Architectural floor slab receiving 3.91 mm contact-clip shadows, LTC softbox illumination, and Crimson/Emerald bounce.",
        { 0.00f, 0.20f, -0.72f },
        { 2.80f, 2.40f, 0.08f },
        SlotPlaster,
        false);

    AppendBoxEntry(
        1u,
        "Gallery Back Wall",
        "gallery-back-wall",
        "GalleryNeutralPlaster",
        "Box · 5.60 × 0.16 × 2.90 m · Neutral Plaster",
        "North neutral plaster backdrop closing the Cornell-Studio chamber.",
        { 0.00f, 2.35f, 0.70f },
        { 2.80f, 0.08f, 1.45f },
        SlotPlaster,
        false);

    AppendBoxEntry(
        2u,
        "Crimson Left Wall",
        "crimson-left-wall",
        "GalleryCrimsonWall",
        "Box · 0.16 × 4.40 × 2.90 m · Crimson Bounce",
        "West sunlit Crimson wall driving warm diffuse color bleed into the gallery.",
        { -2.15f, 0.25f, 0.70f },
        { 0.08f, 2.20f, 1.45f },
        SlotCrimson,
        false);

    AppendBoxEntry(
        2u,
        "Emerald Right Wall",
        "emerald-right-wall",
        "GalleryEmeraldWall",
        "Box · 0.16 × 4.40 × 2.90 m · Emerald Bounce",
        "East Emerald wall reflecting saturated green indirect radiance onto the eastern specimen.",
        { 2.15f, 0.25f, 0.70f },
        { 0.08f, 2.20f, 1.45f },
        SlotEmerald,
        false);

    AppendBoxEntry(
        3u,
        "Clearcoat Roof Baffle",
        "clearcoat-roof-baffle",
        "ClearcoatCobaltArch",
        "Box · 2.70 × 2.30 × 0.14 m · Clearcoat Cobalt",
        "Overhead architectural baffle casting calibrated solar penumbra across the gallery floor.",
        { -0.45f, 0.55f, 1.92f },
        { 1.35f, 1.15f, 0.07f },
        SlotCobalt,
        false);

    AppendBoxEntry(
        4u,
        "Anisotropic Gold Pedestal",
        "gold-pedestal",
        "AnisotropicGoldPedestal",
        "Box · 0.92 × 0.92 × 0.48 m · Brushed Gold",
        "Anisotropic F82 conductor pedestal supporting the central mirror chrome sphere.",
        { -0.15f, 0.55f, -0.40f },
        { 0.46f, 0.46f, 0.24f },
        SlotGold,
        false);

    AppendSphereEntry(
        5u,
        "Mirror Chrome Sphere",
        "chrome-sphere",
        "PolishedMirrorChrome",
        "Sphere · r = 0.52 m · F82 Mirror Chrome",
        "Central hero conductor sphere showcasing Hi-Z screen-space reflections (GTX) and VNDF specular reflections (RTX).",
        { -0.15f, 0.55f, 0.34f },
        0.52f,
        36u,
        22u,
        SlotChrome,
        false);

    AppendSphereEntry(
        6u,
        "Plaster Specimen West",
        "plaster-west",
        "GalleryNeutralPlaster",
        "Sphere · r = 0.44 m · Neutral Plaster",
        "Western Oren-Nayar diffuse sphere capturing Crimson left-wall multi-bounce bleed.",
        { -1.25f, 0.65f, -0.20f },
        0.44f,
        32u,
        20u,
        SlotPlaster,
        false);

    AppendSphereEntry(
        6u,
        "Plaster Specimen East",
        "plaster-east",
        "GalleryNeutralPlaster",
        "Sphere · r = 0.44 m · Neutral Plaster",
        "Eastern Oren-Nayar diffuse sphere capturing Emerald right-wall multi-bounce bleed.",
        { 1.25f, 0.65f, -0.20f },
        0.44f,
        32u,
        20u,
        SlotPlaster,
        false);

    AppendSphereEntry(
        7u,
        "Abbe Crown Glass Orb",
        "abbe-glass-orb",
        "AbbeCrownGlassOrb",
        "Sphere · r = 0.40 m · Abbe 26 Crown Glass",
        "Foreground 2-layer dispersive dielectric orb (IOR 1.52, Abbe 26) refracting the gallery interior.",
        { 0.62f, -0.55f, -0.16f },
        0.40f,
        32u,
        20u,
        SlotGlass,
        true);

    RebuildShowcaseSceneAcceleration(OutBundle, BuildCpuSdf);

    OutBundle.Scene.Sun.DirectionToSun          = Mathematics::NormalizeAxis({ 0.42f, -0.55f, 0.72f });
    OutBundle.Scene.Sun.SunAngularRadiusRadians = 0.018f;
    OutBundle.Scene.Sun.SolarIrradiance         = { 5.40f, 5.10f, 4.65f };
    OutBundle.Scene.Sun.RtxSamplesPerPixel      = 6u;
    OutBundle.Scene.SkyRadiance                 = { 0.16f, 0.23f, 0.36f };

    PolygonalAreaQuadLight CeilingSoftbox {};
    CeilingSoftbox.CenterWorld      = { 0.55f, 0.35f, 1.82f };
    CeilingSoftbox.RightTangent     = { 1.0f, 0.0f, 0.0f };
    CeilingSoftbox.UpBitangent      = { 0.0f, -1.0f, 0.0f };
    CeilingSoftbox.HalfWidthMeters  = 0.85f;
    CeilingSoftbox.HalfHeightMeters = 0.65f;
    CeilingSoftbox.EmittedRadiance  = { 7.5f, 6.6f, 5.4f };
    OutBundle.Scene.AreaLights.push_back(CeilingSoftbox);

    OutBundle.Scene.LatticeConfig.GridMinimumWorld  = { -2.02f, -1.20f, -0.58f };
    OutBundle.Scene.LatticeConfig.GridMaximumWorld  = {  2.02f,  2.15f,  1.75f };
    OutBundle.Scene.LatticeConfig.CountX            = 7u;
    OutBundle.Scene.LatticeConfig.CountY            = 6u;
    OutBundle.Scene.LatticeConfig.CountZ            = 4u;
    OutBundle.Scene.LatticeConfig.FibonacciRayCount = 64u;
    OutBundle.Scene.LatticeConfig.BouncePasses      = 2u;

    if (BuildCpuSdf)
    {
        OutBundle.Scene.ClipmapConfig.Clip0CenterWorld      = { 0.0f, 0.45f, 0.55f };
        OutBundle.Scene.ClipmapConfig.Clip1CenterWorld      = { 0.0f, 0.45f, 0.55f };
        OutBundle.Scene.ClipmapConfig.Clip0HalfExtentMeters = 2.8f;
        OutBundle.Scene.ClipmapConfig.Clip1HalfExtentMeters = 5.6f;
        OutBundle.Scene.ClipmapConfig.Clip0Resolution       = 48u;
        OutBundle.Scene.ClipmapConfig.Clip1Resolution       = 48u;
        OutBundle.Scene.ClipmapConfig.RadiosityBounces      = 2u;
    }

    OutBundle.EditorCamera.CameraOrigin       = { 0.00f, -3.55f, 0.42f };
    OutBundle.EditorCamera.TargetPoint        = { 0.00f,  0.55f, 0.15f };
    OutBundle.EditorCamera.WorldUpAxis        = { 0.00f,  0.00f, 1.00f };
    OutBundle.EditorCamera.VerticalFovRadians = 43.0f * Constants::DegToRadFloat;
    OutBundle.EditorCamera.NearClipMeters     = 0.15f;
    OutBundle.EditorCamera.FarClipMeters      = 60.0f;
    OutBundle.EditorCamera.ViewportWidthPx    = std::max(1u, ViewportWidthPx);
    OutBundle.EditorCamera.ViewportHeightPx   = std::max(1u, ViewportHeightPx);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           OUTLINER REGISTRATION & SYNC
//------------------------------------------------------------------------------------------------------------------------

void RegisterShowcaseGalleryInOutliner(
    OutlinerPanel&            TargetOutliner,
    ShowcaseWorldSceneBundle& InOutBundle,
    bool                      PreserveConstructTemplates)
{
    TargetOutliner.ClearAll();

    uint32_t BoxTemplateOrd    = 0u;
    uint32_t SphereTemplateOrd = 1u;
    uint32_t LightTemplateOrd  = 2u;
    uint32_t CameraTemplateOrd = 3u;

    if (!PreserveConstructTemplates)
    {
        const std::array<OutlinerConstructTemplate, 7u> Templates = {{
            { "Add Box Geometry",      "Box Specimen",      "Geometry", "Box · OpenPBR Surface",      ConstructSlotCategory::GeometryVolume   },
            { "Add Sphere Geometry",   "Sphere Specimen",   "Geometry", "Sphere · OpenPBR Surface",   ConstructSlotCategory::GeometryVolume   },
            { "Add Cylinder Geometry", "Cylinder Specimen", "Geometry", "Cylinder · OpenPBR Surface", ConstructSlotCategory::GeometryVolume   },
            { "Add Cone Geometry",     "Cone Specimen",     "Geometry", "Cone · OpenPBR Surface",     ConstructSlotCategory::GeometryVolume   },
            { "Add Torus Geometry",    "Torus Specimen",    "Geometry", "Torus · OpenPBR Surface",    ConstructSlotCategory::GeometryVolume   },
            { "Add Area Light",        "Area Quad Light",   "Lighting", "LTC Polygonal Quad Light",   ConstructSlotCategory::PointLuminaire },
            { "Add Scene Camera",      "Scene Camera",      "Camera",   "Perspective Optical Camera", ConstructSlotCategory::OpticsCamera  },
        }};
        TargetOutliner.ConfigureConstructTemplates(Templates);
        LightTemplateOrd  = 5u;
        CameraTemplateOrd = 6u;
    }
    else
    {
        // Locate geometry / light / camera templates inside caller-configured list
        const auto Existing = TargetOutliner.QueryConstructTemplates();
        for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(Existing.size()); ++Idx)
        {
            if (Existing[Idx].Category == ConstructSlotCategory::GeometryVolume && ContainsCaseInsensitive(Existing[Idx].DefaultTitle, "Box"))
            {
                BoxTemplateOrd = Idx;
            }
            else if (Existing[Idx].Category == ConstructSlotCategory::GeometryVolume && ContainsCaseInsensitive(Existing[Idx].DefaultTitle, "Sphere"))
            {
                SphereTemplateOrd = Idx;
            }
            else if (Existing[Idx].Category == ConstructSlotCategory::PointLuminaire)
            {
                LightTemplateOrd = Idx;
            }
            else if (Existing[Idx].Category == ConstructSlotCategory::OpticsCamera)
            {
                CameraTemplateOrd = Idx;
            }
        }
    }

    InOutBundle.ShowcaseFolderToken = TargetOutliner.ConstructFolder("Showcase");
    if (OutlinerRecordEntry* Folder = TargetOutliner.AccessEntry(InOutBundle.ShowcaseFolderToken))
    {
        Folder->FolderTint  = WorkspaceSurfaceColors::ShowcaseSage;
        Folder->Subtitle    = "Cornell-Studio Gallery · 10 specimens";
        Folder->AuthorNotes = "Canonical C++ <-> HTML mirror geometry collection.";
        Folder->Expanded    = true;
    }

    for (ShowcaseWorldObjectEntry& Obj : InOutBundle.Objects)
    {
        const OutlinerSlotToken Slot = TargetOutliner.ConstructFromTemplate(
            Obj.SphericalPrimitive ? SphereTemplateOrd : BoxTemplateOrd,
            InOutBundle.ShowcaseFolderToken);
        Obj.SlotToken = Slot;

        if (OutlinerRecordEntry* Entry = TargetOutliner.AccessEntry(Slot))
        {
            Entry->Category          = ConstructSlotCategory::GeometryVolume;
            Entry->Title             = Obj.Title;
            Entry->CategoryLabel     = "Geometry · " + Obj.MaterialTitle;
            Entry->Subtitle          = Obj.Subtitle;
            Entry->AuthorNotes       = Obj.AuthorNotes;
            Entry->PositionX         = Obj.CenterWorld.X;
            Entry->PositionY         = Obj.CenterWorld.Y;
            Entry->PositionZ         = Obj.CenterWorld.Z;
            Entry->ScaleX            = Obj.DimensionsMeters.X;
            Entry->ScaleY            = Obj.DimensionsMeters.Y;
            Entry->ScaleZ            = Obj.DimensionsMeters.Z;
            Entry->VisibilityEnabled = true;
            Entry->CastShadows       = !Obj.GlassShell;
        }
    }

    InOutBundle.CamerasFolderToken = TargetOutliner.ConstructFolder("Cameras");
    if (OutlinerRecordEntry* Folder = TargetOutliner.AccessEntry(InOutBundle.CamerasFolderToken))
    {
        Folder->FolderTint  = ColorRgba{ 126, 178, 224, 255 };
        Folder->Subtitle    = "Editor & scene cameras";
        Folder->AuthorNotes = "Permanent Editor Camera synchronized 1:1 with HTML GTX viewport.";
        Folder->Expanded    = true;
    }

    InOutBundle.EditorCameraToken = TargetOutliner.ConstructFromTemplate(CameraTemplateOrd, InOutBundle.CamerasFolderToken);
    if (OutlinerRecordEntry* CamEntry = TargetOutliner.AccessEntry(InOutBundle.EditorCameraToken))
    {
        CamEntry->Category          = ConstructSlotCategory::OpticsCamera;
        CamEntry->Title             = "Editor Camera";
        CamEntry->CategoryLabel     = "Scene / Cameras";
        CamEntry->Subtitle          = "Editor · permanent · 43.0 deg FOV";
        CamEntry->AuthorNotes       = "Target (0.00, 0.55, 0.15) m · Near 0.15 m · Far 60.0 m · Exposure 1.35x.";
        CamEntry->PositionX         = InOutBundle.EditorCamera.CameraOrigin.X;
        CamEntry->PositionY         = InOutBundle.EditorCamera.CameraOrigin.Y;
        CamEntry->PositionZ         = InOutBundle.EditorCamera.CameraOrigin.Z;
        CamEntry->RotationX         = -3.8f;
        CamEntry->RotationY         = 0.0f;
        CamEntry->RotationZ         = 0.0f;
        CamEntry->ScaleX            = 1.0f;
        CamEntry->ScaleY            = 1.0f;
        CamEntry->ScaleZ            = 1.0f;
        CamEntry->VisibilityEnabled = true;
        CamEntry->CastShadows       = false;
    }

    InOutBundle.WorldFolderToken = TargetOutliner.ConstructFolder("World");
    if (OutlinerRecordEntry* Folder = TargetOutliner.AccessEntry(InOutBundle.WorldFolderToken))
    {
        Folder->FolderTint  = WorkspaceSurfaceColors::ShowcaseWarmGold;
        Folder->Subtitle    = "Sun, Moon, Stars & Volumetric Clouds";
        Folder->AuthorNotes = "Physical Rayleigh/Mie/Ozone sky dome, 4-slot lunar discs, deterministic starfield, and 3D FBM volumetric clouds.";
        Folder->Expanded    = true;
    }

    InOutBundle.SunEmitterToken = TargetOutliner.ConstructFromTemplate(LightTemplateOrd, InOutBundle.WorldFolderToken);
    if (OutlinerRecordEntry* SunEntry = TargetOutliner.AccessEntry(InOutBundle.SunEmitterToken))
    {
        SunEntry->Category          = ConstructSlotCategory::LayerGroup;
        SunEntry->Title             = "Sun";
        SunEntry->CategoryLabel     = "World / Solar Light";
        SunEntry->Subtitle          = "Solar ephemeris & direct disc";
        SunEntry->AuthorNotes       = "3.91 mm/tx GTX Contact-Clip + SDF Cone Penumbra directional sun and physical SkyView LUT driver.";
        SunEntry->PositionX         = InOutBundle.Scene.Sun.DirectionToSun.X;
        SunEntry->PositionY         = InOutBundle.Scene.Sun.DirectionToSun.Y;
        SunEntry->PositionZ         = InOutBundle.Scene.Sun.DirectionToSun.Z;
        SunEntry->VisibilityEnabled = InOutBundle.Scene.Atmosphere.Solar.SolarEnabled;
        SunEntry->CastShadows       = true;
    }

    InOutBundle.MoonBodyToken = TargetOutliner.ConstructFromTemplate(LightTemplateOrd, InOutBundle.WorldFolderToken);
    if (OutlinerRecordEntry* MoonEntry = TargetOutliner.AccessEntry(InOutBundle.MoonBodyToken))
    {
        MoonEntry->Category          = ConstructSlotCategory::LayerGroup;
        MoonEntry->Title             = "Moon";
        MoonEntry->CategoryLabel     = "World / Lunar Disc";
        MoonEntry->Subtitle          = "Luna · Slot 0 · 29.53 d phase";
        MoonEntry->AuthorNotes       = "Multi-slot lunar disc (6 presets: Luna, Ember, Glacier, Sulfur, Shroud, Shard).";
        MoonEntry->VisibilityEnabled = InOutBundle.Scene.Atmosphere.Moons[0].Visible;
        MoonEntry->CastShadows       = false;
    }

    InOutBundle.StarfieldToken = TargetOutliner.ConstructFromTemplate(LightTemplateOrd, InOutBundle.WorldFolderToken);
    if (OutlinerRecordEntry* StarsEntry = TargetOutliner.AccessEntry(InOutBundle.StarfieldToken))
    {
        StarsEntry->Category          = ConstructSlotCategory::LayerGroup;
        StarsEntry->Title             = "Stars";
        StarsEntry->CategoryLabel     = "World / Starfield";
        StarsEntry->Subtitle          = "Limiting 5.5 mag · scintillation";
        StarsEntry->AuthorNotes       = "Deterministic nocturnal starfield modulated by limiting magnitude and atmospheric scintillation.";
        StarsEntry->VisibilityEnabled = InOutBundle.Scene.Atmosphere.Stars.StarfieldEnabled;
        StarsEntry->CastShadows       = false;
    }

    InOutBundle.VolumetricCloudsToken = TargetOutliner.ConstructFromTemplate(LightTemplateOrd, InOutBundle.WorldFolderToken);
    if (OutlinerRecordEntry* CloudsEntry = TargetOutliner.AccessEntry(InOutBundle.VolumetricCloudsToken))
    {
        CloudsEntry->Category          = ConstructSlotCategory::LayerGroup;
        CloudsEntry->Title             = "Volumetric Clouds";
        CloudsEntry->CategoryLabel     = "World / Volumetric Clouds";
        CloudsEntry->Subtitle          = "Cumulus · 34% cover · 1.40 km floor";
        CloudsEntry->AuthorNotes       = "Raymarched 3D FBM cloud deck with dual-lobe Henyey-Greenstein and Beer-Powder self-shadowing.";
        CloudsEntry->VisibilityEnabled = InOutBundle.Scene.Atmosphere.Clouds.CloudsEnabled;
        CloudsEntry->CastShadows       = InOutBundle.Scene.Atmosphere.Clouds.CloudShadow.ShadowEnabled;
    }

    InOutBundle.LocalCloudToken = TargetOutliner.ConstructFromTemplate(LightTemplateOrd, InOutBundle.WorldFolderToken);
    if (OutlinerRecordEntry* LocalEntry = TargetOutliner.AccessEntry(InOutBundle.LocalCloudToken))
    {
        LocalEntry->Category          = ConstructSlotCategory::LayerGroup;
        LocalEntry->Title             = "Local Cloud";
        LocalEntry->CategoryLabel     = "World / Local Cloud Volume";
        LocalEntry->Subtitle          = "Bounded 3D cloud box · 200x200x100 m";
        LocalEntry->AuthorNotes       = "Spatially bounded 3D FBM cloud box in world meters.";
        LocalEntry->VisibilityEnabled = InOutBundle.Scene.Atmosphere.LocalCloud.LocalEnabled;
        LocalEntry->CastShadows       = false;
    }

    InOutBundle.LightingFolderToken = TargetOutliner.ConstructFolder("Lighting");
    if (OutlinerRecordEntry* Folder = TargetOutliner.AccessEntry(InOutBundle.LightingFolderToken))
    {
        Folder->FolderTint  = ColorRgba{ 226, 152, 128, 255 };
        Folder->Subtitle    = "Analytic LTC area lights";
        Folder->AuthorNotes = "Clipped-horizon polygonal quad lights integrated via LinearCosineLightIntegration.";
        Folder->Expanded    = true;
    }

    InOutBundle.CeilingSoftboxToken = TargetOutliner.ConstructFromTemplate(LightTemplateOrd, InOutBundle.LightingFolderToken);
    InOutBundle.AreaLightBindings.clear();
    if (OutlinerRecordEntry* LightEntry = TargetOutliner.AccessEntry(InOutBundle.CeilingSoftboxToken))
    {
        const auto& Softbox = InOutBundle.Scene.AreaLights.front();
        LightEntry->Category          = ConstructSlotCategory::PointLuminaire;
        LightEntry->Title             = "Ceiling Softbox";
        LightEntry->CategoryLabel     = "Lighting / LTC Area Quad";
        LightEntry->Subtitle          = "LTC Quad · 1.70 × 1.30 m · 7.5, 6.6, 5.4 nit";
        LightEntry->AuthorNotes       = "Overhead architectural softbox illuminating gallery floor and specular spheres.";
        LightEntry->PositionX         = Softbox.CenterWorld.X;
        LightEntry->PositionY         = Softbox.CenterWorld.Y;
        LightEntry->PositionZ         = Softbox.CenterWorld.Z;
        LightEntry->ScaleX            = Softbox.HalfWidthMeters * 2.0f;
        LightEntry->ScaleY            = Softbox.HalfHeightMeters * 2.0f;
        LightEntry->ScaleZ            = 1.0f;
        LightEntry->VisibilityEnabled = true;
        LightEntry->CastShadows       = false;

        InOutBundle.AreaLightBindings.push_back({
            InOutBundle.CeilingSoftboxToken,
            0u,
            Softbox.EmittedRadiance,
        });
    }

    OutlinerFooterReadings Footer {};
    Footer.RealtimeText  = "60 fps";
    Footer.QualityText   = "GTX Tier 1";
    Footer.PrimarySlot   = "Sun 46°";
    Footer.SecondarySlot = "LTC 1";
    char CamBuffer[48]{};
    std::snprintf(
        CamBuffer,
        sizeof(CamBuffer),
        "%.1f, %.1f, %.1f",
        InOutBundle.EditorCamera.CameraOrigin.X,
        InOutBundle.EditorCamera.CameraOrigin.Y,
        InOutBundle.EditorCamera.CameraOrigin.Z);
    Footer.CameraText = CamBuffer;
    TargetOutliner.ConfigureFooterReadings(Footer);

    if (!InOutBundle.Objects.empty() && InOutBundle.Objects[6u].SlotToken.Valid())
    {
        TargetOutliner.SelectSingle(InOutBundle.Objects[6u].SlotToken);
    }
}

void RebuildShowcaseSceneAcceleration(
    ShowcaseWorldSceneBundle& InOutBundle,
    bool                      RebuildSdfVolume)
{
    std::size_t TotalTriangleCount = 0u;
    for (ShowcaseWorldObjectEntry& Obj : InOutBundle.Objects)
    {
        if (Obj.ClusterInstanceIndex >= InOutBundle.Scene.ClusterInstances.size())
        {
            continue;
        }
        const auto& Inst = InOutBundle.Scene.ClusterInstances[Obj.ClusterInstanceIndex];
        if (Inst.UniformScale <= 1.0e-4f || Obj.GlassShell)
        {
            continue;
        }
        EnsureObjectLocalSmoothTriangles(Obj);
        TotalTriangleCount += Obj.LocalSmoothTriangles.size();
    }

    std::vector<SpatialAcceleration::RayTrianglePrimitive>  WorldPrims;
    std::vector<SpatialAcceleration::TriangleSmoothNormals> WorldNormals;
    WorldPrims.reserve(std::max<std::size_t>(12u, TotalTriangleCount));
    WorldNormals.reserve(std::max<std::size_t>(12u, TotalTriangleCount));

    uint32_t PrimCounter = 0u;
    for (ShowcaseWorldObjectEntry& Obj : InOutBundle.Objects)
    {
        if (Obj.ClusterInstanceIndex >= InOutBundle.Scene.ClusterInstances.size())
        {
            continue;
        }
        const auto& Inst = InOutBundle.Scene.ClusterInstances[Obj.ClusterInstanceIndex];
        if (Inst.UniformScale <= 1.0e-4f || Obj.GlassShell || Obj.LocalSmoothTriangles.empty())
        {
            continue;
        }

        const float Scale   = Inst.UniformScale;
        const float YawRad  = Obj.RotationZDegrees * 0.01745329252f;
        const bool  HasYaw  = std::fabs(YawRad) > 1.0e-5f;
        const float CosZ    = HasYaw ? std::cos(YawRad) : 1.0f;
        const float SinZ    = HasYaw ? std::sin(YawRad) : 0.0f;
        const auto  Center  = Obj.CenterWorld;
        const uint16_t Mat  = Obj.MaterialSlot;

        for (const CachedLocalSmoothTriangle& LocalTri : Obj.LocalSmoothTriangles)
        {
            const Mathematics::Axis3D L0 = LocalTri.Corner0 * Scale;
            const Mathematics::Axis3D L1 = LocalTri.Corner1 * Scale;
            const Mathematics::Axis3D L2 = LocalTri.Corner2 * Scale;

            const Mathematics::Axis3D W0 = Center + (HasYaw ? RotateAroundZ(L0, CosZ, SinZ) : L0);
            const Mathematics::Axis3D W1 = Center + (HasYaw ? RotateAroundZ(L1, CosZ, SinZ) : L1);
            const Mathematics::Axis3D W2 = Center + (HasYaw ? RotateAroundZ(L2, CosZ, SinZ) : L2);

            SpatialAcceleration::RayTrianglePrimitive Prim {};
            Prim.Corner0          = W0;
            Prim.Edge01           = W1 - W0;
            Prim.Edge02           = W2 - W0;
            Prim.SourceFaceIndex  = PrimCounter;
            Prim.MaterialOrdinal  = Mat;
            Prim.OpaqueMask       = 1u;
            Prim.PrimitiveOrdinal = PrimCounter;
            WorldPrims.push_back(Prim);

            SpatialAcceleration::TriangleSmoothNormals Smooth {};
            Smooth.Normal0 = HasYaw ? RotateAroundZ(LocalTri.Normal0, CosZ, SinZ) : LocalTri.Normal0;
            Smooth.Normal1 = HasYaw ? RotateAroundZ(LocalTri.Normal1, CosZ, SinZ) : LocalTri.Normal1;
            Smooth.Normal2 = HasYaw ? RotateAroundZ(LocalTri.Normal2, CosZ, SinZ) : LocalTri.Normal2;
            WorldNormals.push_back(Smooth);

            ++PrimCounter;
        }
    }

    if (WorldPrims.empty())
    {
        GeometrySpace::PolygonNetwork FallbackNet {};
        GeometrySpace::AppendBoxPrimitive(FallbackNet, { 0.0f, 0.20f, -50.0f }, { 0.05f, 0.05f, 0.05f }, {}, {});
        (void)InOutBundle.Scene.SceneTree.BuildFromPolygonNetwork(FallbackNet);
    }
    else
    {
        (void)InOutBundle.Scene.SceneTree.BuildFromTrianglePrimitivesWithNormals(WorldPrims, WorldNormals);
    }

    if (RebuildSdfVolume)
    {
        (void)InOutBundle.Scene.SceneSdf.BuildFromWideBoundingTree(InOutBundle.Scene.SceneTree, 0.09f, 0.45f);
    }
}

bool SynchronizeShowcaseGalleryFromOutliner(
    const OutlinerPanel&      SourceOutliner,
    ShowcaseWorldSceneBundle& InOutBundle,
    bool                      RebuildSdfVolume)
{
    bool Changed         = false;
    bool AccelInvalidated = false;

    for (ShowcaseWorldObjectEntry& Obj : InOutBundle.Objects)
    {
        if (!Obj.SlotToken.Valid() || Obj.ClusterInstanceIndex >= InOutBundle.Scene.ClusterInstances.size())
        {
            continue;
        }
        const OutlinerRecordEntry* Entry = SourceOutliner.FindEntry(Obj.SlotToken);
        auto&                      Inst  = InOutBundle.Scene.ClusterInstances[Obj.ClusterInstanceIndex];

        const bool  EffectiveVis = (Entry != nullptr) && SourceOutliner.QueryEffectiveVisibility(Obj.SlotToken);
        const float BaseExtent   = std::max(0.05f, std::max({ Obj.AuthoredDimensions.X, Obj.AuthoredDimensions.Y, Obj.AuthoredDimensions.Z }));
        float       UserScale    = 1.0f;
        if (Entry != nullptr && Obj.AuthoredDimensions.X > 1.0e-4f)
        {
            const float MaxDim = std::max(0.05f, std::max({ Entry->ScaleX, Entry->ScaleY, Entry->ScaleZ }));
            UserScale          = std::clamp(MaxDim / BaseExtent, 0.10f, 8.0f);
        }
        const float TargetScale = EffectiveVis ? UserScale : 0.0f;
        if (!NearlyEqualFloat(Inst.UniformScale, TargetScale))
        {
            Inst.UniformScale = TargetScale;
            Changed           = true;
            AccelInvalidated  = true;
        }

        if (Entry != nullptr)
        {
            const Mathematics::Axis3D NextPos{ Entry->PositionX, Entry->PositionY, Entry->PositionZ };
            if (!NearlyEqualFloat(Inst.WorldTranslation.X, NextPos.X) ||
                !NearlyEqualFloat(Inst.WorldTranslation.Y, NextPos.Y) ||
                !NearlyEqualFloat(Inst.WorldTranslation.Z, NextPos.Z))
            {
                Inst.PreviousTranslation = Inst.WorldTranslation;
                Inst.WorldTranslation    = NextPos;
                Obj.CenterWorld          = NextPos;
                Changed                  = true;
                AccelInvalidated         = true;
            }
        }
    }

    if (const OutlinerRecordEntry* CamEntry = SourceOutliner.FindEntry(InOutBundle.EditorCameraToken))
    {
        const Mathematics::Axis3D NextCam{ CamEntry->PositionX, CamEntry->PositionY, CamEntry->PositionZ };
        if (!NearlyEqualFloat(InOutBundle.EditorCamera.CameraOrigin.X, NextCam.X) ||
            !NearlyEqualFloat(InOutBundle.EditorCamera.CameraOrigin.Y, NextCam.Y) ||
            !NearlyEqualFloat(InOutBundle.EditorCamera.CameraOrigin.Z, NextCam.Z))
        {
            InOutBundle.EditorCamera.CameraOrigin = NextCam;
            Changed                               = true;
        }
    }

    if (const OutlinerRecordEntry* SunEntry = SourceOutliner.FindEntry(InOutBundle.SunEmitterToken))
    {
        const bool SunVisible = SourceOutliner.QueryEffectiveVisibility(InOutBundle.SunEmitterToken);
        const Mathematics::LinearRGB TargetIrr = SunVisible
            ? Mathematics::LinearRGB{ 5.40f, 5.10f, 4.65f }
            : Mathematics::LinearRGB{ 0.0f, 0.0f, 0.0f };
        if (!NearlyEqualFloat(InOutBundle.Scene.Sun.SolarIrradiance.R, TargetIrr.R))
        {
            InOutBundle.Scene.Sun.SolarIrradiance = TargetIrr;
            Changed                               = true;
        }
        if (InOutBundle.Scene.Atmosphere.Solar.SolarEnabled != SunVisible)
        {
            InOutBundle.Scene.Atmosphere.Solar.SolarEnabled = SunVisible;
            Changed                                         = true;
        }
        const Mathematics::Axis3D NextDir = Mathematics::NormalizeAxis(
            { SunEntry->PositionX, SunEntry->PositionY, SunEntry->PositionZ },
            { 0.42f, -0.55f, 0.72f });
        if (!NearlyEqualFloat(InOutBundle.Scene.Sun.DirectionToSun.X, NextDir.X) ||
            !NearlyEqualFloat(InOutBundle.Scene.Sun.DirectionToSun.Y, NextDir.Y) ||
            !NearlyEqualFloat(InOutBundle.Scene.Sun.DirectionToSun.Z, NextDir.Z))
        {
            InOutBundle.Scene.Sun.DirectionToSun = NextDir;
            Changed                              = true;
        }
    }

    if (SourceOutliner.FindEntry(InOutBundle.MoonBodyToken) != nullptr)
    {
        const bool MoonVis = SourceOutliner.QueryEffectiveVisibility(InOutBundle.MoonBodyToken);
        if (InOutBundle.Scene.Atmosphere.Moons[0].Visible != MoonVis)
        {
            InOutBundle.Scene.Atmosphere.Moons[0].Visible = MoonVis;
            Changed                                       = true;
        }
    }

    if (SourceOutliner.FindEntry(InOutBundle.StarfieldToken) != nullptr)
    {
        const bool StarsVis = SourceOutliner.QueryEffectiveVisibility(InOutBundle.StarfieldToken);
        if (InOutBundle.Scene.Atmosphere.Stars.StarfieldEnabled != StarsVis)
        {
            InOutBundle.Scene.Atmosphere.Stars.StarfieldEnabled = StarsVis;
            Changed                                             = true;
        }
    }

    if (const OutlinerRecordEntry* CloudsEntry = SourceOutliner.FindEntry(InOutBundle.VolumetricCloudsToken))
    {
        const bool CloudsVis = SourceOutliner.QueryEffectiveVisibility(InOutBundle.VolumetricCloudsToken);
        if (InOutBundle.Scene.Atmosphere.Clouds.CloudsEnabled != CloudsVis ||
            InOutBundle.Scene.Atmosphere.Clouds.CloudShadow.ShadowEnabled != CloudsEntry->CastShadows)
        {
            InOutBundle.Scene.Atmosphere.Clouds.CloudsEnabled             = CloudsVis;
            InOutBundle.Scene.Atmosphere.Clouds.CloudShadow.ShadowEnabled = CloudsEntry->CastShadows;
            Changed                                                       = true;
        }
    }

    if (SourceOutliner.FindEntry(InOutBundle.LocalCloudToken) != nullptr)
    {
        const bool LocalVis = SourceOutliner.QueryEffectiveVisibility(InOutBundle.LocalCloudToken);
        if (InOutBundle.Scene.Atmosphere.LocalCloud.LocalEnabled != LocalVis)
        {
            InOutBundle.Scene.Atmosphere.LocalCloud.LocalEnabled = LocalVis;
            Changed                                              = true;
        }
    }

    if (!InOutBundle.Scene.AreaLights.empty())
    {
        if (const OutlinerRecordEntry* SoftboxEntry = SourceOutliner.FindEntry(InOutBundle.CeilingSoftboxToken))
        {
            auto& Softbox = InOutBundle.Scene.AreaLights.front();
            const bool LightVisible = SourceOutliner.QueryEffectiveVisibility(InOutBundle.CeilingSoftboxToken);
            const Mathematics::LinearRGB TargetEmit = LightVisible
                ? Mathematics::LinearRGB{ 7.5f, 6.6f, 5.4f }
                : Mathematics::LinearRGB{ 0.0f, 0.0f, 0.0f };
            if (!NearlyEqualFloat(Softbox.EmittedRadiance.R, TargetEmit.R))
            {
                Softbox.EmittedRadiance = TargetEmit;
                Changed                 = true;
            }
            const Mathematics::Axis3D NextCenter{ SoftboxEntry->PositionX, SoftboxEntry->PositionY, SoftboxEntry->PositionZ };
            if (!NearlyEqualFloat(Softbox.CenterWorld.X, NextCenter.X) ||
                !NearlyEqualFloat(Softbox.CenterWorld.Y, NextCenter.Y) ||
                !NearlyEqualFloat(Softbox.CenterWorld.Z, NextCenter.Z))
            {
                Softbox.CenterWorld = NextCenter;
                Changed             = true;
            }
            const float NextHalfW = std::max(0.10f, SoftboxEntry->ScaleX * 0.5f);
            const float NextHalfH = std::max(0.10f, SoftboxEntry->ScaleY * 0.5f);
            if (!NearlyEqualFloat(Softbox.HalfWidthMeters, NextHalfW) ||
                !NearlyEqualFloat(Softbox.HalfHeightMeters, NextHalfH))
            {
                Softbox.HalfWidthMeters  = NextHalfW;
                Softbox.HalfHeightMeters = NextHalfH;
                Changed                  = true;
            }
        }
    }

    if (AccelInvalidated)
    {
        RebuildShowcaseSceneAcceleration(InOutBundle, RebuildSdfVolume);
    }

    return Changed;
}

bool SynchronizeShowcaseGallerySceneLive(
    OutlinerPanel&            InOutOutliner,
    ShowcaseWorldSceneBundle& InOutBundle,
    bool&                     OutRequiresAccelerationRebuild,
    bool                      RebuildSdfVolume)
{
    OutRequiresAccelerationRebuild = false;
    bool Changed                   = false;

    // 1. Adopt newly constructed geometry (CategoryOne) and LTC area lights (CategoryThree) from OutlinerPanel
    for (const OutlinerRecordEntry& Entry : InOutOutliner.QueryEntries())
    {
        if (Entry.Category == ConstructSlotCategory::Folder ||
            Entry.Category == ConstructSlotCategory::ForceField ||
            Entry.Token == InOutBundle.EditorCameraToken ||
            Entry.Token == InOutBundle.SunEmitterToken ||
            Entry.Token == InOutBundle.MoonBodyToken ||
            Entry.Token == InOutBundle.StarfieldToken ||
            Entry.Token == InOutBundle.VolumetricCloudsToken ||
            Entry.Token == InOutBundle.LocalCloudToken ||
            Entry.Token == InOutBundle.CeilingSoftboxToken)
        {
            continue;
        }

        if (Entry.Category == ConstructSlotCategory::GeometryVolume)
        {
            const bool KnownObject = std::any_of(
                InOutBundle.Objects.begin(),
                InOutBundle.Objects.end(),
                [&](const ShowcaseWorldObjectEntry& Obj) { return Obj.SlotToken == Entry.Token; });
            if (!KnownObject)
            {
                const ShowcasePrimitiveShape Shape = InferShapeFromRecord(Entry);
                const Mathematics::Scalar3D  Dims  = DefaultDimensionsForShape(Shape);

                auto DynAsset = std::make_unique<ShowcaseGeometryAsset>();
                BuildShapeGeometryAsset(*DynAsset, Shape, Dims);

                MaterialSpace::SurfaceMaterial SpawnMat {};
                SpawnMat.MaterialName             = "ConstructedSpecimenSurface";
                SpawnMat.Diffuse.BaseColor        = { 0.80f, 0.79f, 0.76f };
                SpawnMat.Diffuse.DiffuseRoughness = 0.45f;
                SpawnMat.Specular.Roughness       = 0.36f;
                const uint16_t MatSlot = InOutBundle.Scene.MaterialRegister.RegisterMaterial(SpawnMat);

                Mathematics::Axis3D SpawnCenter { Entry.PositionX, Entry.PositionY, Entry.PositionZ };
                if (NearlyEqualFloat(SpawnCenter.X, 0.0f) &&
                    NearlyEqualFloat(SpawnCenter.Y, 0.0f) &&
                    NearlyEqualFloat(SpawnCenter.Z, 0.0f))
                {
                    // Place flush on Gallery Floor top surface (-0.64 m) in foreground clear zone
                    const float OffsetIdx = static_cast<float>(InOutBundle.DynamicGeometryAssets.size() % 5u) - 2.0f;
                    SpawnCenter = { OffsetIdx * 0.48f, -0.95f, -0.64f + Dims.Z * 0.5f };
                    if (OutlinerRecordEntry* Mutable = InOutOutliner.AccessEntry(Entry.Token))
                    {
                        Mutable->PositionX = SpawnCenter.X;
                        Mutable->PositionY = SpawnCenter.Y;
                        Mutable->PositionZ = SpawnCenter.Z;
                        Mutable->ScaleX    = Dims.X;
                        Mutable->ScaleY    = Dims.Y;
                        Mutable->ScaleZ    = Dims.Z;
                        char SubBuf[96]{};
                        std::snprintf(
                            SubBuf,
                            sizeof(SubBuf),
                            "%s · %.2f × %.2f × %.2f m · OpenPBR",
                            ShapeDisplayLabel(Shape),
                            Dims.X,
                            Dims.Y,
                            Dims.Z);
                        Mutable->Subtitle = SubBuf;
                    }
                }

                const uint32_t InstanceIdx = static_cast<uint32_t>(InOutBundle.Scene.ClusterInstances.size());
                InOutBundle.Scene.ClusterInstances.push_back({
                    &DynAsset->Pyramid,
                    SpawnCenter,
                    SpawnCenter,
                    1.0f,
                    MatSlot,
                    false,
                });
                InOutBundle.DynamicGeometryAssets.push_back(std::move(DynAsset));

                ShowcaseWorldObjectEntry NewObj {};
                NewObj.Title                = Entry.Title;
                NewObj.StableIdentifier     = "constructed-" + std::to_string(Entry.Token.Identifier);
                NewObj.MaterialTitle        = SpawnMat.MaterialName;
                NewObj.Subtitle             = Entry.Subtitle;
                NewObj.AuthorNotes          = Entry.AuthorNotes;
                NewObj.CenterWorld          = SpawnCenter;
                NewObj.DimensionsMeters     = Dims;
                NewObj.AuthoredDimensions   = Dims;
                NewObj.MaterialSlot         = MatSlot;
                NewObj.ClusterInstanceIndex = InstanceIdx;
                NewObj.GlassShell           = false;
                NewObj.SphericalPrimitive   = (Shape == ShowcasePrimitiveShape::Sphere);
                NewObj.DedicatedMaterial    = true;
                NewObj.PrimitiveShape       = Shape;
                NewObj.SlotToken            = Entry.Token;
                InOutBundle.Objects.push_back(std::move(NewObj));

                Changed                        = true;
                OutRequiresAccelerationRebuild = true;
            }
        }
        else if (Entry.Category == ConstructSlotCategory::PointLuminaire)
        {
            const bool KnownLight = std::any_of(
                InOutBundle.AreaLightBindings.begin(),
                InOutBundle.AreaLightBindings.end(),
                [&](const ShowcaseAreaLightBinding& L) { return L.SlotToken == Entry.Token; });
            if (!KnownLight)
            {
                PolygonalAreaQuadLight NewQuad {};
                NewQuad.CenterWorld = { Entry.PositionX, Entry.PositionY, Entry.PositionZ };
                if (NearlyEqualFloat(NewQuad.CenterWorld.Z, 0.0f))
                {
                    NewQuad.CenterWorld = { 0.0f, -0.35f, 1.55f };
                    if (OutlinerRecordEntry* Mutable = InOutOutliner.AccessEntry(Entry.Token))
                    {
                        Mutable->PositionX = NewQuad.CenterWorld.X;
                        Mutable->PositionY = NewQuad.CenterWorld.Y;
                        Mutable->PositionZ = NewQuad.CenterWorld.Z;
                        Mutable->ScaleX    = 1.20f;
                        Mutable->ScaleY    = 0.90f;
                        Mutable->ScaleZ    = 1.0f;
                    }
                }
                NewQuad.RightTangent     = { 1.0f, 0.0f, 0.0f };
                NewQuad.UpBitangent      = { 0.0f, -1.0f, 0.0f };
                NewQuad.HalfWidthMeters  = 0.60f;
                NewQuad.HalfHeightMeters = 0.45f;
                NewQuad.EmittedRadiance  = { 5.8f, 5.4f, 4.8f };
                const uint32_t LightIdx  = static_cast<uint32_t>(InOutBundle.Scene.AreaLights.size());
                InOutBundle.Scene.AreaLights.push_back(NewQuad);
                InOutBundle.AreaLightBindings.push_back({ Entry.Token, LightIdx, NewQuad.EmittedRadiance });
                Changed = true;
            }
        }
    }

    // 2. Sync dynamic area light bindings (beyond index 0 Ceiling Softbox)
    for (ShowcaseAreaLightBinding& Binding : InOutBundle.AreaLightBindings)
    {
        if (Binding.SlotToken == InOutBundle.CeilingSoftboxToken ||
            Binding.AreaLightIndex >= InOutBundle.Scene.AreaLights.size())
        {
            continue;
        }
        const OutlinerRecordEntry* LightEntry = InOutOutliner.FindEntry(Binding.SlotToken);
        auto&                      Quad       = InOutBundle.Scene.AreaLights[Binding.AreaLightIndex];
        const bool                 Active     = (LightEntry != nullptr) && InOutOutliner.QueryEffectiveVisibility(Binding.SlotToken);
        const Mathematics::LinearRGB TargetRad = Active ? Binding.BaseEmittedRadiance : Mathematics::LinearRGB{ 0.0f, 0.0f, 0.0f };
        if (!NearlyEqualFloat(Quad.EmittedRadiance.R, TargetRad.R))
        {
            Quad.EmittedRadiance = TargetRad;
            Changed              = true;
        }
        if (LightEntry != nullptr)
        {
            const Mathematics::Axis3D NextPos{ LightEntry->PositionX, LightEntry->PositionY, LightEntry->PositionZ };
            if (!NearlyEqualFloat(Quad.CenterWorld.X, NextPos.X) ||
                !NearlyEqualFloat(Quad.CenterWorld.Y, NextPos.Y) ||
                !NearlyEqualFloat(Quad.CenterWorld.Z, NextPos.Z))
            {
                Quad.CenterWorld = NextPos;
                Changed          = true;
            }
        }
    }

    // 3. Synchronize existing gallery & constructed objects + camera + sun + softbox
    bool AccelInvalidated = false;
    for (ShowcaseWorldObjectEntry& Obj : InOutBundle.Objects)
    {
        if (!Obj.SlotToken.Valid() || Obj.ClusterInstanceIndex >= InOutBundle.Scene.ClusterInstances.size())
        {
            continue;
        }
        const OutlinerRecordEntry* Entry = InOutOutliner.FindEntry(Obj.SlotToken);
        auto&                      Inst  = InOutBundle.Scene.ClusterInstances[Obj.ClusterInstanceIndex];

        const bool  EffectiveVis      = (Entry != nullptr) && InOutOutliner.QueryEffectiveVisibility(Obj.SlotToken);
        const float AuthoredMaxExtent = std::max(0.05f, std::max({ Obj.AuthoredDimensions.X, Obj.AuthoredDimensions.Y, Obj.AuthoredDimensions.Z }));
        float       UserScale         = 1.0f;
        if (Entry != nullptr && Obj.AuthoredDimensions.X > 1.0e-4f)
        {
            const float MaxDim = std::max(0.05f, std::max({ Entry->ScaleX, Entry->ScaleY, Entry->ScaleZ }));
            UserScale          = std::clamp(MaxDim / AuthoredMaxExtent, 0.10f, 8.0f);
        }
        const float TargetScale = EffectiveVis ? UserScale : 0.0f;
        if (!NearlyEqualFloat(Inst.UniformScale, TargetScale))
        {
            Inst.UniformScale = TargetScale;
            Changed           = true;
            AccelInvalidated  = true;
        }
        if (Entry != nullptr)
        {
            const Mathematics::Axis3D NextPos{ Entry->PositionX, Entry->PositionY, Entry->PositionZ };
            if (!NearlyEqualFloat(Inst.WorldTranslation.X, NextPos.X) ||
                !NearlyEqualFloat(Inst.WorldTranslation.Y, NextPos.Y) ||
                !NearlyEqualFloat(Inst.WorldTranslation.Z, NextPos.Z))
            {
                Inst.PreviousTranslation = Inst.WorldTranslation;
                Inst.WorldTranslation    = NextPos;
                Obj.CenterWorld          = NextPos;
                Changed                  = true;
                AccelInvalidated         = true;
            }
            if (!NearlyEqualFloat(Obj.RotationZDegrees, Entry->RotationZ))
            {
                Obj.RotationZDegrees = Entry->RotationZ;
                Changed              = true;
                AccelInvalidated     = true;
            }
        }
    }

    if (AccelInvalidated || OutRequiresAccelerationRebuild)
    {
        RebuildShowcaseSceneAcceleration(InOutBundle, RebuildSdfVolume);
        OutRequiresAccelerationRebuild = true;
    }

    if (SynchronizeShowcaseGalleryFromOutliner(InOutOutliner, InOutBundle, RebuildSdfVolume))
    {
        Changed                        = true;
        OutRequiresAccelerationRebuild = true;
    }

    return Changed;
}

OutlinerSlotToken ConstructShowcasePrimitiveInOutliner(
    OutlinerPanel&            InOutOutliner,
    ShowcaseWorldSceneBundle& InOutBundle,
    ShowcasePrimitiveShape    Shape,
    Mathematics::Axis3D       WorldPosition,
    bool                      BuildCpuSdf)
{
    const OutlinerSlotToken ParentFolder = InOutBundle.ShowcaseFolderToken.Valid()
        ? InOutBundle.ShowcaseFolderToken
        : OutlinerSlotToken{};

    uint32_t TemplateIdx = 0u;
    const auto Templates = InOutOutliner.QueryConstructTemplates();
    const char* Label    = ShapeDisplayLabel(Shape);
    for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(Templates.size()); ++Idx)
    {
        if (Templates[Idx].Category == ConstructSlotCategory::GeometryVolume &&
            ContainsCaseInsensitive(Templates[Idx].DefaultTitle, Label))
        {
            TemplateIdx = Idx;
            break;
        }
    }

    const OutlinerSlotToken Slot = InOutOutliner.ConstructFromTemplate(TemplateIdx, ParentFolder);
    const Mathematics::Scalar3D Dims = DefaultDimensionsForShape(Shape);
    if (OutlinerRecordEntry* Entry = InOutOutliner.AccessEntry(Slot))
    {
        Entry->Category          = ConstructSlotCategory::GeometryVolume;
        Entry->Title             = std::string(Label) + " Specimen";
        Entry->CategoryLabel     = "Geometry · ConstructedSpecimenSurface";
        Entry->PositionX         = WorldPosition.X;
        Entry->PositionY         = WorldPosition.Y;
        Entry->PositionZ         = WorldPosition.Z;
        Entry->ScaleX            = Dims.X;
        Entry->ScaleY            = Dims.Y;
        Entry->ScaleZ            = Dims.Z;
        Entry->VisibilityEnabled = true;
        Entry->CastShadows       = true;
        char SubBuf[96]{};
        std::snprintf(SubBuf, sizeof(SubBuf), "%s · %.2f × %.2f × %.2f m · OpenPBR", Label, Dims.X, Dims.Y, Dims.Z);
        Entry->Subtitle = SubBuf;
    }

    bool NeedsRebuild = false;
    (void)SynchronizeShowcaseGallerySceneLive(InOutOutliner, InOutBundle, NeedsRebuild, BuildCpuSdf);
    InOutOutliner.SelectSingle(Slot);
    return Slot;
}

MaterialSpace::SurfaceMaterial BuildMaterialFromContentPreset(
    std::string_view DisplayTitle,
    ColorRgba        PreviewTint) noexcept
{
    const Mathematics::LinearRGB LinearTint {
        SrgbByteToLinear(PreviewTint.Red),
        SrgbByteToLinear(PreviewTint.Green),
        SrgbByteToLinear(PreviewTint.Blue),
    };

    MaterialSpace::SurfaceMaterial Mat{};
    if (ContainsCaseInsensitive(DisplayTitle, "glass") || ContainsCaseInsensitive(DisplayTitle, "dielectric"))
    {
        Mat = MaterialSpace::ConstructMaterialPreset(MaterialSpace::MaterialPresetCategory::AbbeCrownGlassDispersion);
    }
    else if (ContainsCaseInsensitive(DisplayTitle, "titanium") || ContainsCaseInsensitive(DisplayTitle, "brushed"))
    {
        Mat = MaterialSpace::ConstructMaterialPreset(MaterialSpace::MaterialPresetCategory::HeatBluedTitaniumIridescent);
        Mat.Diffuse.BaseColor     = LinearTint;
        Mat.Specular.Metalness    = 0.94f;
        Mat.Specular.Roughness    = 0.24f;
        Mat.Specular.Anisotropy   = 0.55f;
        Mat.ThinFilm.ThinFilmWeight = 0.18f;
    }
    else if (ContainsCaseInsensitive(DisplayTitle, "clearcoat") || ContainsCaseInsensitive(DisplayTitle, "automotive"))
    {
        Mat = MaterialSpace::ConstructMaterialPreset(MaterialSpace::MaterialPresetCategory::MetallicCarPaintGlinty);
        Mat.Diffuse.BaseColor       = LinearTint;
        Mat.Clearcoat.CoatWeight    = 1.0f;
        Mat.Clearcoat.CoatRoughness = 0.04f;
    }
    else if (ContainsCaseInsensitive(DisplayTitle, "wax") || ContainsCaseInsensitive(DisplayTitle, "subsurface"))
    {
        Mat = MaterialSpace::ConstructMaterialPreset(MaterialSpace::MaterialPresetCategory::SubsurfaceJadeOrganic);
        Mat.Diffuse.BaseColor           = LinearTint;
        Mat.Subsurface.SubsurfaceColor  = LinearTint;
        Mat.Subsurface.SubsurfaceWeight = 0.78f;
    }
    else
    {
        Mat.Diffuse.BaseColor        = LinearTint;
        Mat.Diffuse.BaseWeight       = 1.0f;
        Mat.Diffuse.DiffuseRoughness = 0.36f;
        Mat.Specular.Roughness       = 0.28f;
        Mat.Specular.DielectricIOR   = 1.50f;
    }
    Mat.MaterialName = std::string(DisplayTitle);
    return Mat;
}

bool AssignShowcaseEntryMaterial(
    OutlinerPanel&                        InOutOutliner,
    ShowcaseWorldSceneBundle&             InOutBundle,
    OutlinerSlotToken                     TargetSlot,
    const MaterialSpace::SurfaceMaterial& NextMaterial,
    std::string_view                      MaterialAlias)
{
    if (!TargetSlot.Valid())
    {
        return false;
    }

    for (ShowcaseWorldObjectEntry& Obj : InOutBundle.Objects)
    {
        if (Obj.SlotToken != TargetSlot)
        {
            continue;
        }

        MaterialSpace::SurfaceMaterial Authored = NextMaterial;
        if (!MaterialAlias.empty())
        {
            Authored.MaterialName = std::string(MaterialAlias);
        }

        if (!Obj.DedicatedMaterial)
        {
            Obj.MaterialSlot      = InOutBundle.Scene.MaterialRegister.RegisterMaterial(Authored);
            Obj.DedicatedMaterial = true;
            if (Obj.ClusterInstanceIndex < InOutBundle.Scene.ClusterInstances.size())
            {
                InOutBundle.Scene.ClusterInstances[Obj.ClusterInstanceIndex].MaterialSlot = Obj.MaterialSlot;
            }
        }
        else
        {
            (void)InOutBundle.Scene.MaterialRegister.UpdateMaterialSlot(Obj.MaterialSlot, Authored);
        }

        const bool IsGlass = (Authored.Transmission.TransmissionWeight > 0.25f);
        Obj.GlassShell     = IsGlass;
        if (Obj.ClusterInstanceIndex < InOutBundle.Scene.ClusterInstances.size())
        {
            InOutBundle.Scene.ClusterInstances[Obj.ClusterInstanceIndex].TransparentGlass = IsGlass;
        }

        Obj.MaterialTitle = Authored.MaterialName.empty() ? std::string("OpenPBR Material") : Authored.MaterialName;
        if (OutlinerRecordEntry* Entry = InOutOutliner.AccessEntry(TargetSlot))
        {
            Entry->CategoryLabel = "Geometry · " + Obj.MaterialTitle;
        }
        RebuildShowcaseSceneAcceleration(InOutBundle);
        return true;
    }

    return false;
}

OutlinerSlotToken ConstructShowcaseModelInOutliner(
    OutlinerPanel&            InOutOutliner,
    ShowcaseWorldSceneBundle& InOutBundle,
    std::string_view          DisplayTitle,
    ColorRgba                 PreviewTint,
    Mathematics::Axis3D       WorldPosition,
    OutlinerSlotToken         ExplicitEnclosure,
    bool                      BuildCpuSdf)
{
    const OutlinerRecordEntry* ExplicitFolder = ExplicitEnclosure.Valid() ? InOutOutliner.FindEntry(ExplicitEnclosure) : nullptr;
    OutlinerSlotToken TargetEnclosure = {};
    if (ExplicitFolder != nullptr)
    {
        TargetEnclosure = (ExplicitFolder->Category == ConstructSlotCategory::Folder)
                              ? ExplicitFolder->Token
                              : ExplicitFolder->EnclosingToken;
    }

    if (ContainsCaseInsensitive(DisplayTitle, "light") ||
        ContainsCaseInsensitive(DisplayTitle, "ies") ||
        ContainsCaseInsensitive(DisplayTitle, "softbox"))
    {
        if (!TargetEnclosure.Valid())
        {
            TargetEnclosure = InOutBundle.LightingFolderToken.Valid() ? InOutBundle.LightingFolderToken : InOutBundle.ShowcaseFolderToken;
        }
        uint32_t LightTemplateIdx = 0u;
        const auto Templates = InOutOutliner.QueryConstructTemplates();
        for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(Templates.size()); ++Idx)
        {
            if (Templates[Idx].Category == ConstructSlotCategory::PointLuminaire)
            {
                LightTemplateIdx = Idx;
                break;
            }
        }
        const OutlinerSlotToken Slot = InOutOutliner.ConstructFromTemplate(LightTemplateIdx, TargetEnclosure);
        if (OutlinerRecordEntry* Entry = InOutOutliner.AccessEntry(Slot))
        {
            Entry->Category          = ConstructSlotCategory::PointLuminaire;
            Entry->Title             = std::string(DisplayTitle);
            Entry->CategoryLabel     = "Photometry · LTC Area Luminaire";
            Entry->Subtitle          = "LTC Quad Light · 1.20 × 0.90 m";
            Entry->PositionX         = WorldPosition.X;
            Entry->PositionY         = WorldPosition.Y;
            Entry->PositionZ         = std::max(0.85f, WorldPosition.Z + 1.15f);
            Entry->ScaleX            = 1.20f;
            Entry->ScaleY            = 0.90f;
            Entry->ScaleZ            = 1.0f;
            Entry->VisibilityEnabled = true;
        }
        bool NeedsRebuild = false;
        (void)SynchronizeShowcaseGallerySceneLive(InOutOutliner, InOutBundle, NeedsRebuild, BuildCpuSdf);
        if (!InOutBundle.AreaLightBindings.empty())
        {
            auto& Binding = InOutBundle.AreaLightBindings.back();
            if (Binding.SlotToken == Slot && Binding.AreaLightIndex < InOutBundle.Scene.AreaLights.size())
            {
                const Mathematics::LinearRGB Rad {
                    5.8f * std::max(0.25f, SrgbByteToLinear(PreviewTint.Red)),
                    5.8f * std::max(0.25f, SrgbByteToLinear(PreviewTint.Green)),
                    5.8f * std::max(0.25f, SrgbByteToLinear(PreviewTint.Blue)),
                };
                Binding.BaseEmittedRadiance = Rad;
                InOutBundle.Scene.AreaLights[Binding.AreaLightIndex].EmittedRadiance = Rad;
            }
        }
        InOutOutliner.SelectSingle(Slot);
        return Slot;
    }

    if (ContainsCaseInsensitive(DisplayTitle, "camera"))
    {
        if (!TargetEnclosure.Valid())
        {
            TargetEnclosure = InOutBundle.CamerasFolderToken.Valid() ? InOutBundle.CamerasFolderToken : InOutBundle.ShowcaseFolderToken;
        }
        uint32_t CamTemplateIdx = 0u;
        const auto Templates = InOutOutliner.QueryConstructTemplates();
        for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(Templates.size()); ++Idx)
        {
            if (Templates[Idx].Category == ConstructSlotCategory::OpticsCamera)
            {
                CamTemplateIdx = Idx;
                break;
            }
        }
        const OutlinerSlotToken Slot = InOutOutliner.ConstructFromTemplate(CamTemplateIdx, TargetEnclosure);
        if (OutlinerRecordEntry* Entry = InOutOutliner.AccessEntry(Slot))
        {
            Entry->Category          = ConstructSlotCategory::OpticsCamera;
            Entry->Title             = std::string(DisplayTitle);
            Entry->CategoryLabel     = "Camera · Perspective Optical Rig";
            Entry->Subtitle          = "FOV 43° · Near 0.15 m";
            Entry->PositionX         = WorldPosition.X;
            Entry->PositionY         = WorldPosition.Y;
            Entry->PositionZ         = std::max(-0.20f, WorldPosition.Z + 0.35f);
            Entry->VisibilityEnabled = true;
        }
        InOutOutliner.SelectSingle(Slot);
        return Slot;
    }

    if (!TargetEnclosure.Valid())
    {
        TargetEnclosure = InOutBundle.ShowcaseFolderToken;
    }

    ShowcasePrimitiveShape Shape = ShowcasePrimitiveShape::Box;
    if (ContainsCaseInsensitive(DisplayTitle, "torus"))
    {
        Shape = ShowcasePrimitiveShape::Torus;
    }
    else if (ContainsCaseInsensitive(DisplayTitle, "cylinder cone") ||
             ContainsCaseInsensitive(DisplayTitle, "cylinder") ||
             ContainsCaseInsensitive(DisplayTitle, "tyre") ||
             ContainsCaseInsensitive(DisplayTitle, "fan") ||
             ContainsCaseInsensitive(DisplayTitle, "integrator"))
    {
        Shape = ShowcasePrimitiveShape::Cylinder;
    }
    else if (ContainsCaseInsensitive(DisplayTitle, "cone"))
    {
        Shape = ShowcasePrimitiveShape::Cone;
    }
    else if (ContainsCaseInsensitive(DisplayTitle, "sphere") ||
             ContainsCaseInsensitive(DisplayTitle, "ball") ||
             ContainsCaseInsensitive(DisplayTitle, "bust"))
    {
        Shape = ShowcasePrimitiveShape::Sphere;
    }

    Mathematics::Scalar3D Dims = DefaultDimensionsForShape(Shape);
    if (ContainsCaseInsensitive(DisplayTitle, "traffic cone"))
    {
        Dims = { 0.48f, 0.48f, 0.78f };
    }
    else if (ContainsCaseInsensitive(DisplayTitle, "cylinder cone"))
    {
        Dims = { 0.32f, 0.32f, 0.82f };
    }
    else if (ContainsCaseInsensitive(DisplayTitle, "torus"))
    {
        Dims = { 0.68f, 0.68f, 0.24f };
    }
    else if (ContainsCaseInsensitive(DisplayTitle, "tyre"))
    {
        Dims = { 0.66f, 0.66f, 0.44f };
    }

    const Mathematics::Axis3D FloorPlaced {
        WorldPosition.X,
        WorldPosition.Y,
        -0.64f + Dims.Z * 0.5f,
    };

    const OutlinerSlotToken Slot = ConstructShowcasePrimitiveInOutliner(
        InOutOutliner,
        InOutBundle,
        Shape,
        FloorPlaced,
        BuildCpuSdf);

    if (TargetEnclosure.Valid() && TargetEnclosure != InOutBundle.ShowcaseFolderToken)
    {
        (void)InOutOutliner.AttachToEnclosure(Slot, TargetEnclosure);
    }

    if (OutlinerRecordEntry* Entry = InOutOutliner.AccessEntry(Slot))
    {
        Entry->Title  = std::string(DisplayTitle);
        Entry->ScaleX = Dims.X;
        Entry->ScaleY = Dims.Y;
        Entry->ScaleZ = Dims.Z;
        char SubBuf[96]{};
        std::snprintf(SubBuf, sizeof(SubBuf), "%s · %.2f × %.2f × %.2f m · OpenPBR", ShapeDisplayLabel(Shape), Dims.X, Dims.Y, Dims.Z);
        Entry->Subtitle = SubBuf;
        for (ShowcaseWorldObjectEntry& Obj : InOutBundle.Objects)
        {
            if (Obj.SlotToken == Slot)
            {
                Obj.Title    = Entry->Title;
                Obj.Subtitle = Entry->Subtitle;
                break;
            }
        }
    }

    MaterialSpace::SurfaceMaterial ModelMat{};
    ModelMat.Diffuse.BaseColor = {
        SrgbByteToLinear(PreviewTint.Red),
        SrgbByteToLinear(PreviewTint.Green),
        SrgbByteToLinear(PreviewTint.Blue),
    };
    ModelMat.Diffuse.BaseWeight       = 1.0f;
    ModelMat.Diffuse.DiffuseRoughness = 0.38f;
    ModelMat.Specular.Roughness       = 0.30f;
    ModelMat.Specular.DielectricIOR   = 1.50f;
    if (ContainsCaseInsensitive(DisplayTitle, "traffic cone") || ContainsCaseInsensitive(DisplayTitle, "cylinder cone"))
    {
        ModelMat.Clearcoat.CoatWeight    = 0.30f;
        ModelMat.Clearcoat.CoatRoughness = 0.16f;
    }
    else if (ContainsCaseInsensitive(DisplayTitle, "torus") || ContainsCaseInsensitive(DisplayTitle, "titanium") ||
             ContainsCaseInsensitive(DisplayTitle, "fan") || ContainsCaseInsensitive(DisplayTitle, "hull"))
    {
        ModelMat.Specular.Metalness = 0.86f;
        ModelMat.Specular.Roughness = 0.24f;
    }
    else if (ContainsCaseInsensitive(DisplayTitle, "tyre"))
    {
        ModelMat.Diffuse.BaseColor = { 0.045f, 0.048f, 0.055f };
        ModelMat.Specular.Roughness = 0.78f;
    }
    ModelMat.MaterialName = std::string(DisplayTitle);

    (void)AssignShowcaseEntryMaterial(InOutOutliner, InOutBundle, Slot, ModelMat, DisplayTitle);
    InOutOutliner.SelectSingle(Slot);
    return Slot;
}

bool AssignShowcaseEntryBaseColor(
    OutlinerPanel&            InOutOutliner,
    ShowcaseWorldSceneBundle& InOutBundle,
    OutlinerSlotToken         TargetSlot,
    ColorRgba                 SrgbSwatchColor,
    std::string_view          SwatchAlias)
{
    if (!TargetSlot.Valid())
    {
        return false;
    }

    if (OutlinerRecordEntry* Entry = InOutOutliner.AccessEntry(TargetSlot))
    {
        if (Entry->Category == ConstructSlotCategory::Folder)
        {
            Entry->FolderTint = SrgbSwatchColor;
            return true;
        }
    }

    for (ShowcaseWorldObjectEntry& Obj : InOutBundle.Objects)
    {
        if (Obj.SlotToken != TargetSlot)
        {
            continue;
        }

        MaterialSpace::SurfaceMaterial Updated = InOutBundle.Scene.MaterialRegister.InspectAuthoredMaterial(Obj.MaterialSlot);
        Updated.Diffuse.BaseColor = {
            SrgbByteToLinear(SrgbSwatchColor.Red),
            SrgbByteToLinear(SrgbSwatchColor.Green),
            SrgbByteToLinear(SrgbSwatchColor.Blue),
        };
        if (!SwatchAlias.empty())
        {
            Updated.MaterialName = std::string(SwatchAlias);
        }

        if (!Obj.DedicatedMaterial)
        {
            Obj.MaterialSlot      = InOutBundle.Scene.MaterialRegister.RegisterMaterial(Updated);
            Obj.DedicatedMaterial = true;
            if (Obj.ClusterInstanceIndex < InOutBundle.Scene.ClusterInstances.size())
            {
                InOutBundle.Scene.ClusterInstances[Obj.ClusterInstanceIndex].MaterialSlot = Obj.MaterialSlot;
            }
        }
        else
        {
            (void)InOutBundle.Scene.MaterialRegister.UpdateMaterialSlot(Obj.MaterialSlot, Updated);
        }

        if (!SwatchAlias.empty())
        {
            Obj.MaterialTitle = std::string(SwatchAlias);
            if (OutlinerRecordEntry* Entry = InOutOutliner.AccessEntry(TargetSlot))
            {
                Entry->CategoryLabel = "Geometry · " + Obj.MaterialTitle;
            }
        }
        RebuildShowcaseSceneAcceleration(InOutBundle);
        return true;
    }

    return false;
}

OutlinerSlotToken PickShowcaseGalleryEntryAtUv(
    const OutlinerPanel&            SourceOutliner,
    const ShowcaseWorldSceneBundle& Bundle,
    float                           NormalizedU,
    float                           NormalizedV) noexcept
{
    Mathematics::Axis3D RayOrigin {}, RayDir {};
    BuildCameraRay(Bundle.EditorCamera, NormalizedU, NormalizedV, RayOrigin, RayDir);

    float             ClosestDistance = std::numeric_limits<float>::max();
    OutlinerSlotToken ClosestToken    {};

    for (const ShowcaseWorldObjectEntry& Obj : Bundle.Objects)
    {
        if (!Obj.SlotToken.Valid() || !SourceOutliner.QueryEffectiveVisibility(Obj.SlotToken))
        {
            continue;
        }
        if (Obj.ClusterInstanceIndex >= Bundle.Scene.ClusterInstances.size())
        {
            continue;
        }
        const float Scale = Bundle.Scene.ClusterInstances[Obj.ClusterInstanceIndex].UniformScale;
        if (Scale <= 1.0e-4f)
        {
            continue;
        }

        float HitDist = 0.0f;
        bool  Hit     = false;
        if (Obj.PrimitiveShape == ShowcasePrimitiveShape::Sphere)
        {
            const float Radius = std::max(0.04f, 0.5f * Obj.DimensionsMeters.X * Scale);
            Hit                = IntersectRaySphere(RayOrigin, RayDir, Obj.CenterWorld, Radius, HitDist);
        }
        else
        {
            const Mathematics::Scalar3D HalfExt{
                std::max(0.04f, 0.5f * Obj.DimensionsMeters.X * Scale),
                std::max(0.04f, 0.5f * Obj.DimensionsMeters.Y * Scale),
                std::max(0.04f, 0.5f * Obj.DimensionsMeters.Z * Scale),
            };
            Hit = IntersectRayAabb(RayOrigin, RayDir, Obj.CenterWorld, HalfExt, HitDist);
        }

        if (Hit && HitDist < ClosestDistance)
        {
            ClosestDistance = HitDist;
            ClosestToken    = Obj.SlotToken;
        }
    }

    for (const ShowcaseAreaLightBinding& LightBind : Bundle.AreaLightBindings)
    {
        if (!LightBind.SlotToken.Valid() ||
            LightBind.AreaLightIndex >= Bundle.Scene.AreaLights.size() ||
            !SourceOutliner.QueryEffectiveVisibility(LightBind.SlotToken))
        {
            continue;
        }
        const auto& Quad = Bundle.Scene.AreaLights[LightBind.AreaLightIndex];
        float       HitDist = 0.0f;
        if (IntersectRayAabb(
                RayOrigin,
                RayDir,
                Quad.CenterWorld,
                { std::max(0.10f, Quad.HalfWidthMeters), std::max(0.10f, Quad.HalfHeightMeters), 0.08f },
                HitDist) &&
            HitDist < ClosestDistance)
        {
            ClosestDistance = HitDist;
            ClosestToken    = LightBind.SlotToken;
        }
    }

    return ClosestToken;
}

Mathematics::Axis3D RaycastShowcasePlacementAtUv(
    const ShowcaseWorldSceneBundle& Bundle,
    float                           NormalizedU,
    float                           NormalizedV,
    float                           HalfHeightMeters) noexcept
{
    Mathematics::Axis3D RayOrigin {}, RayDir {};
    BuildCameraRay(Bundle.EditorCamera, NormalizedU, NormalizedV, RayOrigin, RayDir);

    constexpr float GalleryFloorTopZ = -0.64f;
    if (std::fabs(RayDir.Z) > 1.0e-4f)
    {
        const float T = (GalleryFloorTopZ - RayOrigin.Z) / RayDir.Z;
        if (T > 0.15f && T < 30.0f)
        {
            const Mathematics::Axis3D Hit = RayOrigin + RayDir * T;
            return {
                std::clamp(Hit.X, -1.85f, 1.85f),
                std::clamp(Hit.Y, -1.80f, 1.95f),
                GalleryFloorTopZ + std::max(0.05f, HalfHeightMeters),
            };
        }
    }
    return { 0.0f, -0.85f, GalleryFloorTopZ + std::max(0.05f, HalfHeightMeters) };
}

} // namespace Frontier::GeometricRaster
