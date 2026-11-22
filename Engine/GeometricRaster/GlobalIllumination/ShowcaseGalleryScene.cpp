/*============================================================================================================================================
                                                           SHOWCASEGALLERYSCENE.CPP
============================================================================================================================================*/
// 🧩 Default & Blank World Scenes, EditorCamera, and OutlinerPanel bidirectional registration

#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"

#include "Engine/Codecs/ControlVehicleSurfaceDecoder.h"
#include "Engine/Codecs/GeometryCodec.h"
#include "Engine/Codecs/MaterialCodec.h"
#include "Engine/Codecs/PhotometrySpaceCodec.h"
#include "Engine/Codecs/VolumetricSpaceCodec.h"
#include "Engine/Codecs/WorldSpaceCodec.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/Tools/DefaultContentGenerator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <unordered_map>

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

void EnsureObjectLocalSmoothTriangles(ShowcaseWorldAssetEntry& Obj)
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

[[nodiscard]] uint32_t EncodeAnalyticalShapeCode(ShowcasePrimitiveShape Shape) noexcept
{
    switch (Shape)
    {
        case ShowcasePrimitiveShape::Box:      return 1u;
        case ShowcasePrimitiveShape::Sphere:   return 2u;
        case ShowcasePrimitiveShape::Cylinder: return 3u;
        case ShowcasePrimitiveShape::Cone:     return 4u;
        case ShowcasePrimitiveShape::Torus:    return 5u;
        default:                               return 1u;
    }
}

[[nodiscard]] bool BakeAssetSignedLocalSdfFromSmoothTriangles(
    std::span<const CachedLocalSmoothTriangle>    LocalTriangles,
    Mathematics::Scalar3D                         DimensionsMeters,
    DistanceFieldSpace::LocalSignedDistanceVolume& OutVolume)
{
    if (LocalTriangles.empty())
    {
        return false;
    }

    std::vector<SpatialAcceleration::RayTrianglePrimitive> LocalPrims;
    LocalPrims.reserve(LocalTriangles.size());
    for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(LocalTriangles.size()); ++Idx)
    {
        const CachedLocalSmoothTriangle& Tri = LocalTriangles[Idx];
        SpatialAcceleration::RayTrianglePrimitive Prim {};
        Prim.Corner0          = Tri.Corner0;
        Prim.Edge01           = Tri.Corner1 - Tri.Corner0;
        Prim.Edge02           = Tri.Corner2 - Tri.Corner0;
        Prim.SourceFaceIndex  = Idx;
        Prim.MaterialOrdinal  = (Tri.MaterialSlotOverride != 0xFFFFu) ? Tri.MaterialSlotOverride : 0u;
        Prim.OpaqueMask       = 1u;
        Prim.PrimitiveOrdinal = Idx;
        LocalPrims.push_back(Prim);
    }

    const float MaxSpan = std::max({ 0.25f, DimensionsMeters.X, DimensionsMeters.Y, DimensionsMeters.Z });
    const float Pitch   = std::max(0.056f, MaxSpan / 80.0f);
    const auto  ResFor  = [Pitch](float SpanMeters) noexcept -> uint32_t
    {
        const uint32_t Raw = static_cast<uint32_t>(std::lround(std::max(0.20f, SpanMeters) / Pitch));
        return std::clamp(((Raw + 3u) / 4u) * 4u, 16u, 80u);
    };
    const uint32_t ResX   = ResFor(DimensionsMeters.X);
    const uint32_t ResY   = ResFor(DimensionsMeters.Y);
    const uint32_t ResZ   = ResFor(DimensionsMeters.Z);
    const float    Margin = std::clamp(MaxSpan * 0.055f, 0.06f, 0.28f);
    return OutVolume.BakeSignedFromTrianglesAspect(LocalPrims, ResX, ResY, ResZ, Margin);
}

void BuildCameraRay(
    const PerspectiveCameraBasis& Cam,
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
//                                              WORLD SCENE BUILDERS
//------------------------------------------------------------------------------------------------------------------------

bool LoadWorldSceneFromSpaceFile(
    const std::filesystem::path& WorldSpacePath,
    const std::filesystem::path& RepositoryRoot,
    ShowcaseWorldSceneBundle&    OutBundle,
    uint32_t                     ViewportWidthPx,
    uint32_t                     ViewportHeightPx,
    std::string&                 OutDiagnostic)
{
    Codecs::WorldSpaceDocumentModel WorldDoc{};
    if (!Codecs::WorldSpaceDecoder::DecodeFromFile(WorldSpacePath, WorldDoc, OutDiagnostic))
    {
        return false;
    }

    OutBundle = {};

    std::unordered_map<std::string, uint16_t> MaterialSlotByPath{};
    auto ResolveMaterialSlot = [&](const std::string& RelativeMatPath, std::string& OutMatTitle) -> uint16_t
    {
        if (RelativeMatPath.empty())
        {
            OutMatTitle = "DefaultClay";
            return 0u;
        }
        if (const auto Found = MaterialSlotByPath.find(RelativeMatPath); Found != MaterialSlotByPath.end())
        {
            OutMatTitle = OutBundle.Scene.MaterialRegister.InspectAuthoredMaterial(Found->second).MaterialName;
            return Found->second;
        }
        Codecs::MaterialSpaceDocumentModel MatDoc{};
        std::string                        MatDiag{};
        if (Codecs::MaterialDecoder::DecodeFromFile(RepositoryRoot / RelativeMatPath, MatDoc, MatDiag))
        {
            const uint16_t Slot = OutBundle.Scene.MaterialRegister.RegisterMaterial(MatDoc.AuthoredMaterial);
            MaterialSlotByPath.emplace(RelativeMatPath, Slot);
            OutMatTitle = MatDoc.AuthoredMaterial.MaterialName;
            return Slot;
        }
        OutMatTitle = "DefaultClay";
        return 0u;
    };

    // Environment & Solar Photometry
    if (!WorldDoc.EnvironmentVolumetricPath.empty())
    {
        Codecs::VolumetricSpaceDocumentModel VolDoc{};
        std::string                          VolDiag{};
        if (Codecs::VolumetricSpaceDecoder::DecodeFromFile(RepositoryRoot / WorldDoc.EnvironmentVolumetricPath, VolDoc, VolDiag))
        {
            OutBundle.Scene.Atmosphere  = VolDoc.Atmosphere;
            OutBundle.Scene.SkyRadiance = VolDoc.FallbackSkyRadiance;
        }
    }

    if (!WorldDoc.SolarPhotometryPath.empty())
    {
        Codecs::PhotometrySpaceDocumentModel SunDoc{};
        std::string                          SunDiag{};
        if (Codecs::PhotometrySpaceDecoder::DecodeFromFile(RepositoryRoot / WorldDoc.SolarPhotometryPath, SunDoc, SunDiag))
        {
            OutBundle.Scene.Atmosphere.Solar = SunDoc.SolarEphemeris;
            OutBundle.Scene.Sun              = SunDoc.SolarSource;
            if (SunDoc.SolarEphemeris.SolarEnabled)
            {
                const auto Solved                = CelestialEphemerisSolver::SolveSolarEphemeris(OutBundle.Scene.Atmosphere.Solar);
                OutBundle.Scene.Sun.DirectionToSun = Solved.WorldDirectionToSun;
            }
        }
    }

    // Geometry Placements (.GeometrySpace + .MaterialSpace + .SmoothNormals)
    for (const Codecs::WorldGeometryPlacementEntry& Placement : WorldDoc.GeometryPlacements)
    {
        auto                    GeomAsset = std::make_unique<ShowcaseGeometryAsset>();
        ShowcaseWorldAssetEntry Entry{};
        Entry.Title                = Placement.PlacementTitle;
        Entry.StableIdentifier     = Placement.StableIdentifier;
        Entry.Subtitle             = Placement.Subtitle;
        Entry.AuthorNotes          = Placement.AuthorNotes;
        Entry.CenterWorld          = Placement.CenterWorld;
        Entry.DimensionsMeters     = Placement.DimensionsMeters;
        Entry.AuthoredDimensions   = Placement.DimensionsMeters;
        Entry.RotationZDegrees     = Placement.RotationZDegrees;
        Entry.SphericalPrimitive   = Placement.SphericalPrimitive;
        Entry.GlassShell           = Placement.GlassShell;
        Entry.PrimitiveShape       = static_cast<ShowcasePrimitiveShape>(std::min<uint32_t>(Placement.PrimitiveShapeOrdinal, 4u));
        Entry.MaterialSlot         = ResolveMaterialSlot(Placement.PrimaryMaterialPath, Entry.MaterialTitle);
        Entry.ClusterInstanceIndex = static_cast<uint32_t>(OutBundle.Scene.ClusterInstances.size());

        Codecs::GeometrySpaceDocument GeoDoc{};
        std::string                   GeoDiag{};
        if (!Placement.GeometrySpacePath.empty() &&
            Codecs::GeometryDecoder::DecodeFromFile(RepositoryRoot / Placement.GeometrySpacePath, GeoDoc, GeoDiag) &&
            !GeoDoc.Network.Faces.empty())
        {
            std::unordered_map<uint32_t, uint16_t> SubSlotRemap{};
            for (const Codecs::GeometrySurfaceSlotLink& SlotLink : GeoDoc.SurfaceSlots)
            {
                std::string SubMatTitle{};
                SubSlotRemap[SlotLink.PartGroupOrdinal] = ResolveMaterialSlot(SlotLink.MaterialPath, SubMatTitle);
            }

            GeomAsset->Network = std::move(GeoDoc.Network);
            const std::size_t FaceCount = GeomAsset->Network.Faces.size();
            const bool HasSmoothCorners = (GeoDoc.FaceCornerNormals.size() >= FaceCount * 9u);
            Entry.LocalSmoothTriangles.reserve(FaceCount);

            for (std::size_t FaceIdx = 0u; FaceIdx < FaceCount; ++FaceIdx)
            {
                auto& Face = GeomAsset->Network.Faces[FaceIdx];
                uint16_t ResolvedSlot = Entry.MaterialSlot;
                if (const auto FoundSlot = SubSlotRemap.find(Face.PartGroupOrdinal); FoundSlot != SubSlotRemap.end())
                {
                    ResolvedSlot = FoundSlot->second;
                }
                Face.PartGroupOrdinal = ResolvedSlot;

                const uint32_t E0 = Face.BoundaryLoopEdgeIndex;
                if (E0 >= GeomAsset->Network.Edges.size())
                {
                    continue;
                }
                const uint32_t E1 = GeomAsset->Network.Edges[E0].NextEdgeIndex;
                const uint32_t E2 = (E1 < GeomAsset->Network.Edges.size()) ? GeomAsset->Network.Edges[E1].NextEdgeIndex : E0;
                if (E1 >= GeomAsset->Network.Edges.size() || E2 >= GeomAsset->Network.Edges.size())
                {
                    continue;
                }
                const uint32_t V0 = GeomAsset->Network.Edges[E0].OriginVertexIndex;
                const uint32_t V1 = GeomAsset->Network.Edges[E1].OriginVertexIndex;
                const uint32_t V2 = GeomAsset->Network.Edges[E2].OriginVertexIndex;
                if (V0 >= GeomAsset->Network.Vertices.size() ||
                    V1 >= GeomAsset->Network.Vertices.size() ||
                    V2 >= GeomAsset->Network.Vertices.size())
                {
                    continue;
                }

                CachedLocalSmoothTriangle SmoothTri{};
                SmoothTri.Corner0              = GeomAsset->Network.Vertices[V0].Axes;
                SmoothTri.Corner1              = GeomAsset->Network.Vertices[V1].Axes;
                SmoothTri.Corner2              = GeomAsset->Network.Vertices[V2].Axes;
                SmoothTri.MaterialSlotOverride = ResolvedSlot;
                if (HasSmoothCorners)
                {
                    const std::size_t Base = FaceIdx * 9u;
                    SmoothTri.Normal0 = { GeoDoc.FaceCornerNormals[Base + 0u], GeoDoc.FaceCornerNormals[Base + 1u], GeoDoc.FaceCornerNormals[Base + 2u] };
                    SmoothTri.Normal1 = { GeoDoc.FaceCornerNormals[Base + 3u], GeoDoc.FaceCornerNormals[Base + 4u], GeoDoc.FaceCornerNormals[Base + 5u] };
                    SmoothTri.Normal2 = { GeoDoc.FaceCornerNormals[Base + 6u], GeoDoc.FaceCornerNormals[Base + 7u], GeoDoc.FaceCornerNormals[Base + 8u] };
                    GeomAsset->Network.Vertices[V0].Normal = SmoothTri.Normal0;
                    GeomAsset->Network.Vertices[V1].Normal = SmoothTri.Normal1;
                    GeomAsset->Network.Vertices[V2].Normal = SmoothTri.Normal2;
                }
                else
                {
                    SmoothTri.Normal0 = Face.GeometricNormal;
                    SmoothTri.Normal1 = Face.GeometricNormal;
                    SmoothTri.Normal2 = Face.GeometricNormal;
                }
                Entry.LocalSmoothTriangles.push_back(SmoothTri);
            }
            // Nanite-like per-asset LOD bake: glass keeps LOD 1 as its last level (silhouette
            // protection); every asset locks material boundaries so face material slots survive.
            SpatialAcceleration::ClusterLodBakeConfiguration LodBake {};
            LodBake.MaximumLevels          = 3u;
            LodBake.CoarsestAllowedLevel   = Entry.GlassShell ? 1u : 0xFFFFFFFFu;
            LodBake.LockMaterialBoundaries = true;
            (void)GeomAsset->Pyramid.BuildFromPolygonNetwork(GeomAsset->Network, LodBake);
            for (const Codecs::GeometrySurfaceSlotLink& SlotLink : GeoDoc.SurfaceSlots)
            {
                if (ContainsCaseInsensitive(SlotLink.SlotTitle, "glass") ||
                    ContainsCaseInsensitive(SlotLink.MaterialPath, "windshield") ||
                    ContainsCaseInsensitive(SlotLink.MaterialPath, "glass"))
                {
                    if (const auto FoundSlot = SubSlotRemap.find(SlotLink.PartGroupOrdinal); FoundSlot != SubSlotRemap.end())
                    {
                        GeomAsset->Pyramid.ConfigureMaterialCoarsestAllowedLevel(
                            static_cast<uint8_t>(FoundSlot->second & 0xFFu),
                            1u);
                    }
                }
            }
            if (FaceCount > 64u && !Entry.GlassShell)
            {
                (void)BakeAssetSignedLocalSdfFromSmoothTriangles(
                    Entry.LocalSmoothTriangles,
                    Entry.AuthoredDimensions,
                    GeomAsset->LocalSdf);
            }
        }
        else
        {
            BuildBoxGeometryAsset(*GeomAsset, Placement.DimensionsMeters * 0.5f);
            for (auto& Face : GeomAsset->Network.Faces)
            {
                Face.PartGroupOrdinal = Entry.MaterialSlot;
            }
            SpatialAcceleration::ClusterLodBakeConfiguration LodBake {};
            LodBake.MaximumLevels        = 3u;
            LodBake.CoarsestAllowedLevel = Entry.GlassShell ? 1u : 0xFFFFFFFFu;
            LodBake.LockMaterialBoundaries = true;
            (void)GeomAsset->Pyramid.BuildFromPolygonNetwork(GeomAsset->Network, LodBake);
            EnsureObjectLocalSmoothTriangles(Entry);
        }

        const bool BakedLocalSdfPresent = !GeomAsset->LocalSdf.HalfDistanceSamples.empty();
        RigidClusterInstance Inst {};
        Inst.PyramidAsset        = &GeomAsset->Pyramid;
        Inst.WorldTranslation    = Entry.CenterWorld;
        Inst.PreviousTranslation = Entry.CenterWorld;
        Inst.UniformScale        = 1.0f;
        Inst.MaterialSlot        = Entry.MaterialSlot;
        Inst.TransparentGlass    = Entry.SphericalPrimitive && Entry.GlassShell;
        Inst.RotationZDegrees    = Entry.RotationZDegrees;
        Inst.NonUniformScale     = { 1.0f, 1.0f, 1.0f };
        Inst.LocalHalfExtents    = {
            std::max(0.02f, Entry.AuthoredDimensions.X * 0.5f),
            std::max(0.02f, Entry.AuthoredDimensions.Y * 0.5f),
            std::max(0.02f, Entry.AuthoredDimensions.Z * 0.5f),
        };
        Inst.AnalyticalShapeCode = BakedLocalSdfPresent ? 0u : EncodeAnalyticalShapeCode(Entry.PrimitiveShape);
        Inst.LocalSdfVolume      = BakedLocalSdfPresent ? &GeomAsset->LocalSdf : nullptr;
        OutBundle.Scene.ClusterInstances.push_back(Inst);
        OutBundle.DynamicGeometryAssets.push_back(std::move(GeomAsset));
        OutBundle.Objects.push_back(std::move(Entry));
    }

    // Area Luminaires (.PhotometrySpace)
    for (const Codecs::WorldAreaLuminairePlacement& AreaPlacement : WorldDoc.AreaLuminairePlacements)
    {
        if (!AreaPlacement.VisibilityEnabled || AreaPlacement.PhotometrySpacePath.empty())
        {
            continue;
        }
        Codecs::PhotometrySpaceDocumentModel AreaDoc{};
        std::string                          AreaDiag{};
        if (Codecs::PhotometrySpaceDecoder::DecodeFromFile(RepositoryRoot / AreaPlacement.PhotometrySpacePath, AreaDoc, AreaDiag))
        {
            auto Quad        = AreaDoc.AreaQuad;
            Quad.CenterWorld = AreaPlacement.CenterWorld;
            OutBundle.Scene.AreaLights.push_back(Quad);
        }
    }

    // Measured Luminaires (.PhotometrySpace 128x32 CandelaGrid)
    std::unordered_map<std::string, uint32_t> ProfileIndexByPath{};
    for (const Codecs::WorldMeasuredLuminairePlacement& LumPlacement : WorldDoc.MeasuredLuminairePlacements)
    {
        if (LumPlacement.PhotometrySpacePath.empty())
        {
            continue;
        }
        uint32_t ProfileIdx = 0u;
        if (const auto Found = ProfileIndexByPath.find(LumPlacement.PhotometrySpacePath); Found != ProfileIndexByPath.end())
        {
            ProfileIdx = Found->second;
        }
        else
        {
            Codecs::PhotometrySpaceDocumentModel MeasDoc{};
            std::string                          MeasDiag{};
            if (!Codecs::PhotometrySpaceDecoder::DecodeFromFile(RepositoryRoot / LumPlacement.PhotometrySpacePath, MeasDoc, MeasDiag) ||
                !MeasDoc.CandelaIncluded)
            {
                continue;
            }
            ProfileIdx = static_cast<uint32_t>(OutBundle.Scene.PhotometricProfiles.size());
            OutBundle.Scene.PhotometricProfiles.push_back(std::move(MeasDoc.CandelaProfile));
            ProfileIndexByPath.emplace(LumPlacement.PhotometrySpacePath, ProfileIdx);
        }

        PhotometricPointRadiator Rad{};
        Rad.PositionWorld    = LumPlacement.PositionWorld;
        Rad.BeamAxis         = LumPlacement.BeamAxis;
        Rad.ZeroAzimuthAxis  = LumPlacement.ZeroAzimuthAxis;
        Rad.PeakIntensityRgb = LumPlacement.PeakIntensityRgb;
        Rad.RangeMeters      = LumPlacement.RangeMeters;
        Rad.RadiusMeters     = LumPlacement.RadiusMeters;
        Rad.ProfileOrdinal   = ProfileIdx;
        OutBundle.Scene.PhotometricRadiators.push_back(Rad);
    }

    RebuildShowcaseSceneAcceleration(OutBundle);

    OutBundle.EditorCamera                  = WorldDoc.EditorCamera;
    OutBundle.EditorCamera.FarClipMeters    = std::max(5000.0f, WorldDoc.EditorCamera.FarClipMeters);
    OutBundle.EditorCamera.ViewportWidthPx  = std::max(1u, ViewportWidthPx);
    OutBundle.EditorCamera.ViewportHeightPx = std::max(1u, ViewportHeightPx);
    OutDiagnostic.clear();
    return true;
}

void BuildBlankPlaneSkyScene(
    ShowcaseWorldSceneBundle& OutBundle,
    uint32_t                  ViewportWidthPx,
    uint32_t                  ViewportHeightPx)
{
    OutBundle = {};

    // Single Ground Plane (48 m x 48 m x 0.16 m, top surface at Z = 0.0 m)
    MaterialSpace::SurfaceMaterial GroundMat {};
    GroundMat.MaterialName            = "GroundPlaneAsphalt";
    GroundMat.Diffuse.BaseWeight      = 1.0f;
    GroundMat.Diffuse.BaseColor       = { 0.22f, 0.23f, 0.25f };
    GroundMat.Specular.Roughness      = 0.54f;
    GroundMat.Specular.Metalness      = 0.0f;
    GroundMat.Specular.DielectricIOR  = 1.50f;
    GroundMat.Clearcoat.CoatWeight    = 0.04f;
    GroundMat.Clearcoat.CoatRoughness = 0.36f;
    const uint16_t GroundSlot = OutBundle.Scene.MaterialRegister.RegisterMaterial(GroundMat);

    const Mathematics::Scalar3D GroundDims   { 48.0f, 48.0f, 0.16f };
    const Mathematics::Axis3D   GroundCenter { 0.0f, 0.0f, -0.08f };
    BuildBoxGeometryAsset(OutBundle.GeometryAssets[0u], GroundDims * 0.5f);
    for (auto& Face : OutBundle.GeometryAssets[0u].Network.Faces)
    {
        Face.PartGroupOrdinal = GroundSlot;
    }
    (void)OutBundle.GeometryAssets[0u].Pyramid.BuildFromPolygonNetwork(OutBundle.GeometryAssets[0u].Network, 3u);
    {
        RigidClusterInstance GroundInst {};
        GroundInst.PyramidAsset        = &OutBundle.GeometryAssets[0u].Pyramid;
        GroundInst.WorldTranslation    = GroundCenter;
        GroundInst.PreviousTranslation = GroundCenter;
        GroundInst.UniformScale        = 1.0f;
        GroundInst.MaterialSlot        = GroundSlot;
        GroundInst.TransparentGlass    = false;
        GroundInst.NonUniformScale     = { 1.0f, 1.0f, 1.0f };
        GroundInst.LocalHalfExtents    = { GroundDims.X * 0.5f, GroundDims.Y * 0.5f, GroundDims.Z * 0.5f };
        GroundInst.AnalyticalShapeCode = 1u;
        OutBundle.Scene.ClusterInstances.push_back(GroundInst);
    }

    ShowcaseWorldAssetEntry GroundEntry {};
    GroundEntry.Title                = "GroundPlane";
    GroundEntry.StableIdentifier     = "ground-plane";
    GroundEntry.MaterialTitle        = GroundMat.MaterialName;
    GroundEntry.Subtitle             = "Box · 48.0 × 48.0 × 0.16 m · Z = 0 datum";
    GroundEntry.AuthorNotes          = "Blank scene ground plane under clear sky illumination.";
    GroundEntry.CenterWorld          = GroundCenter;
    GroundEntry.DimensionsMeters     = GroundDims;
    GroundEntry.AuthoredDimensions   = GroundDims;
    GroundEntry.MaterialSlot         = GroundSlot;
    GroundEntry.ClusterInstanceIndex = 0u;
    GroundEntry.PrimitiveShape       = ShowcasePrimitiveShape::Box;
    EnsureObjectLocalSmoothTriangles(GroundEntry);
    OutBundle.Objects.push_back(std::move(GroundEntry));

    // Clear Sky & Daylight Sun (no clouds, fog, moon, stars, or area lights)
    auto& Atmo = OutBundle.Scene.Atmosphere;
    Atmo.Medium.AtmosphereEnabled        = true;
    Atmo.Solar.SolarEnabled              = true;
    Atmo.Solar.CycleAnimationEnabled     = false;
    Atmo.Solar.LocalHours                = 10.5f;
    Atmo.Clouds.CloudsEnabled            = false;
    Atmo.Clouds.FollowWindEnabled        = false;
    Atmo.Clouds.WindSpeedMetersPerSecond = 0.0f;
    Atmo.Clouds.CloudRenderScale         = 1.00f;
    Atmo.Clouds.CloudMarchSteps          = 64u;
    Atmo.Clouds.CloudShadow.ShadowEnabled = false;
    Atmo.LocalCloud.LocalEnabled         = false;
    Atmo.LocalCloud.FollowWindEnabled    = false;
    Atmo.Moons[0].Visible                = false;
    Atmo.Stars.StarfieldEnabled          = false;

    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Atmo.Solar);
    OutBundle.Scene.Sun.DirectionToSun          = SolarSolved.WorldDirectionToSun;
    OutBundle.Scene.Sun.SolarIrradiance         = { 5.40f, 5.12f, 4.72f };
    OutBundle.Scene.Sun.SunAngularRadiusRadians = 0.0093f;
    OutBundle.Scene.Sun.RtxSamplesPerPixel      = 4u;
    OutBundle.Scene.SkyRadiance                 = { 0.42f, 0.58f, 0.88f };
    OutBundle.Scene.AreaLights.clear();
    OutBundle.Scene.PointLights.clear();
    OutBundle.Scene.PhotometricRadiators.clear();
    OutBundle.Scene.PhotometricProfiles.clear();

    RebuildShowcaseSceneAcceleration(OutBundle);

    OutBundle.EditorCamera.CameraOrigin       = { 6.50f, -6.50f, 2.60f };
    OutBundle.EditorCamera.TargetPoint        = { 0.00f, 0.00f, 0.20f };
    OutBundle.EditorCamera.WorldUpAxis        = { 0.00f, 0.00f, 1.00f };
    OutBundle.EditorCamera.VerticalFovRadians = 42.0f * Constants::DegToRadFloat;
    OutBundle.EditorCamera.NearClipMeters     = 0.12f;
    OutBundle.EditorCamera.FarClipMeters      = 5000.0f;
    OutBundle.EditorCamera.ViewportWidthPx    = std::max(1u, ViewportWidthPx);
    OutBundle.EditorCamera.ViewportHeightPx   = std::max(1u, ViewportHeightPx);
}

void BuildVehicleClearSkyDefaultScene(
    ShowcaseWorldSceneBundle& OutBundle,
    uint32_t                  ViewportWidthPx,
    uint32_t                  ViewportHeightPx)
{
    OutBundle = {};

    // Locate repository root containing Content/Worlds/VehicleClearSkyDefault.WorldSpace or EngineContent/
    std::filesystem::path RepoRoot = std::filesystem::current_path();
    for (int Depth = 0; Depth < 6; ++Depth)
    {
        if (std::filesystem::exists(RepoRoot / "Content" / "Worlds" / "VehicleClearSkyDefault.WorldSpace") ||
            std::filesystem::exists(RepoRoot / "EngineContent" / "EnvironmentArchives" / "ControlVehicle" / "ControlVehicleBody.face"))
        {
            break;
        }
        if (!RepoRoot.has_parent_path() || RepoRoot.parent_path() == RepoRoot)
        {
            break;
        }
        RepoRoot = RepoRoot.parent_path();
    }

    std::string SpaceDiag{};
    if (std::filesystem::exists(RepoRoot / "EngineContent" / "EnvironmentArchives" / "ControlVehicle" / "ControlVehicleBody.face"))
    {
        (void)Tools::DefaultContentGenerator::EnsureDefaultContentAvailable(RepoRoot, SpaceDiag);
    }
    const std::filesystem::path DefaultWorldSpace = RepoRoot / "Content" / "Worlds" / "VehicleClearSkyDefault.WorldSpace";
    if (std::filesystem::exists(DefaultWorldSpace) &&
        LoadWorldSceneFromSpaceFile(DefaultWorldSpace, RepoRoot, OutBundle, ViewportWidthPx, ViewportHeightPx, SpaceDiag))
    {
        return;
    }

    // Slot 0: Neutral studio ground plane (architectural concrete / dry asphalt)
    MaterialSpace::SurfaceMaterial GroundMat {};
    GroundMat.MaterialName            = "Ground Plane Asphalt";
    GroundMat.Diffuse.BaseWeight      = 1.0f;
    GroundMat.Diffuse.BaseColor       = { 0.22f, 0.23f, 0.25f };
    GroundMat.Specular.Roughness      = 0.54f;
    GroundMat.Specular.Metalness      = 0.0f;
    GroundMat.Specular.DielectricIOR  = 1.50f;
    GroundMat.Clearcoat.CoatWeight    = 0.04f;
    GroundMat.Clearcoat.CoatRoughness = 0.36f;
    const uint16_t GroundSlot = OutBundle.Scene.MaterialRegister.RegisterMaterial(GroundMat);

    // Slots 1..11: 11-slot OpenPBR automotive material register (daytime clear-sky: zero headlamp/taillamp nits)
    std::array<uint16_t, static_cast<std::size_t>(Codecs::VehicleSurfaceSlot::Count)> VehicleSlots {};
    const auto ConstructVehicleSlotMaterial = [](Codecs::VehicleSurfaceSlot Slot) -> MaterialSpace::SurfaceMaterial
    {
        MaterialSpace::SurfaceMaterial Mat {};
        switch (Slot)
        {
            case Codecs::VehicleSurfaceSlot::BodyCoat:
                Mat.MaterialName            = "ControlVehicleMetallicPaint";
                Mat.Diffuse.BaseColor       = { 0.09f, 0.28f, 0.74f };
                Mat.Specular.Metalness      = 0.72f;
                Mat.Specular.Roughness      = 0.18f;
                Mat.Specular.EdgeTintF82    = { 0.55f, 0.78f, 1.00f };
                Mat.Clearcoat.CoatWeight    = 1.00f;
                Mat.Clearcoat.CoatRoughness = 0.03f;
                break;
            case Codecs::VehicleSurfaceSlot::CanopyGloss:
                Mat.MaterialName            = "VehicleObsidianCanopy";
                Mat.Diffuse.BaseColor       = { 0.03f, 0.035f, 0.045f };
                Mat.Specular.Roughness      = 0.08f;
                Mat.Clearcoat.CoatWeight    = 1.00f;
                Mat.Clearcoat.CoatRoughness = 0.02f;
                break;
            case Codecs::VehicleSurfaceSlot::VehicleGlass:
                Mat.MaterialName            = "VehicleTintedWindshield";
                Mat.Diffuse.BaseColor       = { 0.05f, 0.09f, 0.13f };
                Mat.Specular.Roughness      = 0.025f;
                Mat.Specular.DielectricIOR  = 1.52f;
                Mat.Clearcoat.CoatWeight    = 1.00f;
                Mat.Clearcoat.CoatRoughness = 0.015f;
                break;
            case Codecs::VehicleSurfaceSlot::VehiclePlastic:
                Mat.MaterialName       = "VehicleIntakePolymer";
                Mat.Diffuse.BaseColor  = { 0.07f, 0.075f, 0.082f };
                Mat.Specular.Roughness = 0.52f;
                break;
            case Codecs::VehicleSurfaceSlot::TyreRubber:
                Mat.MaterialName       = "VulcanizedTyreRubber";
                Mat.Diffuse.BaseColor  = { 0.045f, 0.046f, 0.050f };
                Mat.Specular.Roughness = 0.68f;
                break;
            case Codecs::VehicleSurfaceSlot::RimAlloy:
                Mat.MaterialName         = "ForgedTitaniumRimAlloy";
                Mat.Diffuse.BaseColor    = { 0.84f, 0.86f, 0.90f };
                Mat.Specular.Metalness   = 0.96f;
                Mat.Specular.Roughness   = 0.16f;
                Mat.Specular.EdgeTintF82 = { 0.96f, 0.97f, 0.99f };
                break;
            case Codecs::VehicleSurfaceSlot::WheelCapAccent:
                Mat.MaterialName       = "AnodizedBronzeHubCap";
                Mat.Diffuse.BaseColor  = { 0.78f, 0.54f, 0.24f };
                Mat.Specular.Metalness = 0.92f;
                Mat.Specular.Roughness = 0.20f;
                break;
            case Codecs::VehicleSurfaceSlot::BrakeDisc:
                Mat.MaterialName        = "MachinedSteelBrakeRotor";
                Mat.Diffuse.BaseColor   = { 0.66f, 0.68f, 0.71f };
                Mat.Specular.Metalness  = 0.94f;
                Mat.Specular.Roughness  = 0.28f;
                Mat.Specular.Anisotropy = 0.55f;
                break;
            case Codecs::VehicleSurfaceSlot::VehicleTrim:
                Mat.MaterialName       = "SatinGraphiteTrim";
                Mat.Diffuse.BaseColor  = { 0.14f, 0.15f, 0.17f };
                Mat.Specular.Metalness = 0.40f;
                Mat.Specular.Roughness = 0.34f;
                break;
            case Codecs::VehicleSurfaceSlot::HeadlightLens:
                Mat.MaterialName            = "SmokedHeadlampHousing";
                Mat.Diffuse.BaseColor       = { 0.12f, 0.13f, 0.15f };
                Mat.Specular.Roughness      = 0.05f;
                Mat.Clearcoat.CoatWeight    = 0.92f;
                Mat.Clearcoat.CoatRoughness = 0.03f;
                break;
            case Codecs::VehicleSurfaceSlot::TaillightLens:
            default:
                Mat.MaterialName            = "RubyTaillampOptic";
                Mat.Diffuse.BaseColor       = { 0.36f, 0.025f, 0.028f };
                Mat.Specular.Roughness      = 0.06f;
                Mat.Clearcoat.CoatWeight    = 0.88f;
                Mat.Clearcoat.CoatRoughness = 0.035f;
                break;
        }
        return Mat;
    };
    for (std::size_t R = 0u; R < VehicleSlots.size(); ++R)
    {
        VehicleSlots[R] = OutBundle.Scene.MaterialRegister.RegisterMaterial(
            ConstructVehicleSlotMaterial(static_cast<Codecs::VehicleSurfaceSlot>(R)));
    }

    // Object 0: Ground Plane (24 m x 24 m x 0.16 m, top surface at Z = 0.0 m)
    const Mathematics::Scalar3D GroundDims   { 24.0f, 24.0f, 0.16f };
    const Mathematics::Axis3D   GroundCenter { 0.0f,  0.0f, -0.08f };
    BuildBoxGeometryAsset(OutBundle.GeometryAssets[0u], GroundDims * 0.5f);
    for (auto& Face : OutBundle.GeometryAssets[0u].Network.Faces)
    {
        Face.PartGroupOrdinal = GroundSlot;
    }
    (void)OutBundle.GeometryAssets[0u].Pyramid.BuildFromPolygonNetwork(OutBundle.GeometryAssets[0u].Network, 3u);
    {
        RigidClusterInstance GroundInst {};
        GroundInst.PyramidAsset        = &OutBundle.GeometryAssets[0u].Pyramid;
        GroundInst.WorldTranslation    = GroundCenter;
        GroundInst.PreviousTranslation = GroundCenter;
        GroundInst.UniformScale        = 1.0f;
        GroundInst.MaterialSlot        = GroundSlot;
        GroundInst.TransparentGlass    = false;
        GroundInst.NonUniformScale     = { 1.0f, 1.0f, 1.0f };
        GroundInst.LocalHalfExtents    = { GroundDims.X * 0.5f, GroundDims.Y * 0.5f, GroundDims.Z * 0.5f };
        GroundInst.AnalyticalShapeCode = 1u;
        OutBundle.Scene.ClusterInstances.push_back(GroundInst);
    }

    ShowcaseWorldAssetEntry GroundEntry {};
    GroundEntry.Title                = "GroundPlane";
    GroundEntry.StableIdentifier     = "ground-plane";
    GroundEntry.MaterialTitle        = GroundMat.MaterialName;
    GroundEntry.Subtitle             = "Box · 24.0 × 24.0 × 0.16 m · Z = 0 datum";
    GroundEntry.AuthorNotes          = "Outdoor Z=0 ground plane supporting ControlVehicle under clear sky illumination.";
    GroundEntry.CenterWorld          = GroundCenter;
    GroundEntry.DimensionsMeters     = GroundDims;
    GroundEntry.AuthoredDimensions   = GroundDims;
    GroundEntry.MaterialSlot         = GroundSlot;
    GroundEntry.ClusterInstanceIndex = 0u;
    GroundEntry.PrimitiveShape       = ShowcasePrimitiveShape::Box;
    EnsureObjectLocalSmoothTriangles(GroundEntry);
    OutBundle.Objects.push_back(std::move(GroundEntry));

    // Object 1: ControlVehicle (25,068 creased smooth-normal triangles, 4 rims + 4 tyres + body resting on Z = 0)
    Codecs::AssembledControlVehicle DecodedVehicle {};
    std::string VehicleDiag {};
    const bool VehicleDecoded = Codecs::ControlVehicleSurfaceDecoder::DecodeAssembledVehicle(
        RepoRoot, Codecs::VehicleCoordinateConvention::FrontierWorldZUp, DecodedVehicle, VehicleDiag);

    auto VehicleGeom = std::make_unique<ShowcaseGeometryAsset>();
    ShowcaseWorldAssetEntry VehicleEntry {};
    VehicleEntry.Title                = "ControlVehicle";
    VehicleEntry.StableIdentifier     = "control-vehicle";
    VehicleEntry.MaterialTitle        = "ControlVehicleMetallicPaint";
    VehicleEntry.MaterialSlot         = VehicleSlots[static_cast<std::size_t>(Codecs::VehicleSurfaceSlot::BodyCoat)];
    VehicleEntry.ClusterInstanceIndex = 1u;
    VehicleEntry.PrimitiveShape       = ShowcasePrimitiveShape::Box;

    if (VehicleDecoded && !DecodedVehicle.Triangles.empty())
    {
        const Mathematics::Axis3D Center = DecodedVehicle.BoundsCenter;
        const Mathematics::Scalar3D Span {
            std::max(0.20f, DecodedVehicle.BoundsMax.X - DecodedVehicle.BoundsMin.X),
            std::max(0.20f, DecodedVehicle.BoundsMax.Y - DecodedVehicle.BoundsMin.Y),
            std::max(0.20f, DecodedVehicle.BoundsMax.Z - DecodedVehicle.BoundsMin.Z),
        };
        VehicleEntry.CenterWorld        = Center;
        VehicleEntry.DimensionsMeters   = Span;
        VehicleEntry.AuthoredDimensions = Span;
        VehicleEntry.Subtitle           = "ControlVehicle · 25,068 tris · 11 OpenPBR slots";
        VehicleEntry.AuthorNotes        = "Decoded FCT1 ControlVehicle (Body + 4 Rims + 4 Tyres) resting tangent to Z = 0.";

        const uint32_t TriCount = static_cast<uint32_t>(DecodedVehicle.Triangles.size());
        VehicleGeom->Network.NetworkTitle = "ControlVehicle";
        VehicleGeom->Network.ReserveStorage(TriCount * 3u, TriCount * 3u, TriCount);
        VehicleEntry.LocalSmoothTriangles.reserve(TriCount);

        for (const Codecs::DecodedVehicleTriangle& Tri : DecodedVehicle.Triangles)
        {
            const uint16_t SlotMat = VehicleSlots[std::min<std::size_t>(
                static_cast<std::size_t>(Tri.SurfaceSlot),
                VehicleSlots.size() - 1u)];

            const Mathematics::Axis3D L0 = Tri.CornerA - Center;
            const Mathematics::Axis3D L1 = Tri.CornerB - Center;
            const Mathematics::Axis3D L2 = Tri.CornerC - Center;

            CachedLocalSmoothTriangle SmoothTri {};
            SmoothTri.Corner0              = L0;
            SmoothTri.Corner1              = L1;
            SmoothTri.Corner2              = L2;
            SmoothTri.Normal0              = Tri.NormalA;
            SmoothTri.Normal1              = Tri.NormalB;
            SmoothTri.Normal2              = Tri.NormalC;
            SmoothTri.MaterialSlotOverride = SlotMat;
            VehicleEntry.LocalSmoothTriangles.push_back(SmoothTri);

            const uint32_t V0 = VehicleGeom->Network.AppendVertex(L0);
            const uint32_t V1 = VehicleGeom->Network.AppendVertex(L1);
            const uint32_t V2 = VehicleGeom->Network.AppendVertex(L2);
            VehicleGeom->Network.Vertices[V0].Normal = Tri.NormalA;
            VehicleGeom->Network.Vertices[V1].Normal = Tri.NormalB;
            VehicleGeom->Network.Vertices[V2].Normal = Tri.NormalC;

            const std::array<uint32_t, 3u> Loop {{ V0, V1, V2 }};
            (void)VehicleGeom->Network.AppendFace(Loop, {}, {}, 0u, SlotMat);
        }
        (void)VehicleGeom->Pyramid.BuildFromPolygonNetwork(VehicleGeom->Network, 3u);
        const uint16_t GlassSlot = VehicleSlots[static_cast<std::size_t>(Codecs::VehicleSurfaceSlot::VehicleGlass)];
        VehicleGeom->Pyramid.ConfigureMaterialCoarsestAllowedLevel(static_cast<uint8_t>(GlassSlot & 0xFFu), 1u);
        (void)BakeAssetSignedLocalSdfFromSmoothTriangles(
            VehicleEntry.LocalSmoothTriangles,
            VehicleEntry.AuthoredDimensions,
            VehicleGeom->LocalSdf);
    }
    else
    {
        const Mathematics::Scalar3D FallbackSpan { 4.48f, 1.94f, 1.32f };
        VehicleEntry.CenterWorld        = { 0.0f, 0.0f, 0.66f };
        VehicleEntry.DimensionsMeters   = FallbackSpan;
        VehicleEntry.AuthoredDimensions = FallbackSpan;
        VehicleEntry.Subtitle           = "Vehicle Proxy Box";
        BuildBoxGeometryAsset(*VehicleGeom, FallbackSpan * 0.5f);
        EnsureObjectLocalSmoothTriangles(VehicleEntry);
    }

    {
        const bool VehicleSdfPresent = !VehicleGeom->LocalSdf.HalfDistanceSamples.empty();
        RigidClusterInstance VehicleInst {};
        VehicleInst.PyramidAsset        = &VehicleGeom->Pyramid;
        VehicleInst.WorldTranslation    = VehicleEntry.CenterWorld;
        VehicleInst.PreviousTranslation = VehicleEntry.CenterWorld;
        VehicleInst.UniformScale        = 1.0f;
        VehicleInst.MaterialSlot        = VehicleEntry.MaterialSlot;
        VehicleInst.TransparentGlass    = false;
        VehicleInst.RotationZDegrees    = VehicleEntry.RotationZDegrees;
        VehicleInst.NonUniformScale     = { 1.0f, 1.0f, 1.0f };
        VehicleInst.LocalHalfExtents    = {
            std::max(0.05f, VehicleEntry.AuthoredDimensions.X * 0.5f),
            std::max(0.05f, VehicleEntry.AuthoredDimensions.Y * 0.5f),
            std::max(0.05f, VehicleEntry.AuthoredDimensions.Z * 0.5f),
        };
        VehicleInst.AnalyticalShapeCode = VehicleSdfPresent ? 0u : 1u;
        VehicleInst.LocalSdfVolume      = VehicleSdfPresent ? &VehicleGeom->LocalSdf : nullptr;
        OutBundle.Scene.ClusterInstances.push_back(VehicleInst);
    }
    OutBundle.DynamicGeometryAssets.push_back(std::move(VehicleGeom));
    OutBundle.Objects.push_back(std::move(VehicleEntry));

    // Clear Sky & Crisp Daylight Sun (Zero Clouds, Zero Fog, Zero Indoor Area Lights)
    auto& Atmo = OutBundle.Scene.Atmosphere;
    Atmo.Medium.AtmosphereEnabled             = true;
    Atmo.Solar.SolarEnabled                   = true;
    Atmo.Solar.CycleAnimationEnabled          = false;
    Atmo.Solar.LocalHours                     = 10.5f;
    Atmo.Clouds.CloudsEnabled                 = false;
    Atmo.Clouds.FollowWindEnabled             = false;
    Atmo.Clouds.WindSpeedMetersPerSecond      = 0.0f;
    Atmo.Clouds.CloudRenderScale              = 1.00f;
    Atmo.Clouds.CloudMarchSteps               = 64u;
    Atmo.Clouds.CloudShadow.ShadowEnabled     = false;
    Atmo.LocalCloud.LocalEnabled              = false;
    Atmo.LocalCloud.FollowWindEnabled         = false;
    Atmo.Moons[0].Visible                     = false;
    Atmo.Stars.StarfieldEnabled               = false;

    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Atmo.Solar);
    OutBundle.Scene.Sun.DirectionToSun          = SolarSolved.WorldDirectionToSun;
    OutBundle.Scene.Sun.SolarIrradiance         = { 5.40f, 5.12f, 4.72f };
    OutBundle.Scene.Sun.SunAngularRadiusRadians = 0.0093f;
    OutBundle.Scene.Sun.RtxSamplesPerPixel      = 4u;
    OutBundle.Scene.SkyRadiance                 = { 0.42f, 0.58f, 0.88f };
    OutBundle.Scene.AreaLights.clear();
    OutBundle.Scene.PointLights.clear();
    OutBundle.Scene.PhotometricRadiators.clear();
    OutBundle.Scene.PhotometricProfiles.clear();

    RebuildShowcaseSceneAcceleration(OutBundle);

    // 3/4 Automotive Hero Camera
    OutBundle.EditorCamera.CameraOrigin       = { 4.85f, -4.15f, 1.68f };
    OutBundle.EditorCamera.TargetPoint        = { 0.10f,  0.00f, 0.62f };
    OutBundle.EditorCamera.WorldUpAxis        = { 0.00f,  0.00f, 1.00f };
    OutBundle.EditorCamera.VerticalFovRadians = 38.0f * Constants::DegToRadFloat;
    OutBundle.EditorCamera.NearClipMeters     = 0.12f;
    OutBundle.EditorCamera.FarClipMeters      = 5000.0f;
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
        Folder->Subtitle    = (InOutBundle.Objects.size() == 1u)
            ? "Blank Scene · Ground Plane & Clear Sky"
            : "Default Scene · Vehicle & Ground Plane";
        Folder->AuthorNotes = "Scene geometry collection.";
        Folder->Expanded    = true;
    }

    for (ShowcaseWorldAssetEntry& Obj : InOutBundle.Objects)
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
            Entry->RotationZ         = Obj.RotationZDegrees;
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
        Folder->AuthorNotes = "Physical Rayleigh/Mie/Ozone sky dome, 6-slot lunar discs, deterministic starfield, and 3D FBM volumetric clouds.";
        Folder->Expanded    = true;
    }

    InOutBundle.SunEmitterToken = TargetOutliner.ConstructFromTemplate(LightTemplateOrd, InOutBundle.WorldFolderToken);
    if (OutlinerRecordEntry* SunEntry = TargetOutliner.AccessEntry(InOutBundle.SunEmitterToken))
    {
        SunEntry->Category          = ConstructSlotCategory::LayerGroup;
        SunEntry->Title             = "Sun";
        SunEntry->CategoryLabel     = "World / Solar Light";
        SunEntry->Subtitle          = "Solar ephemeris & direct disc";
        SunEntry->AuthorNotes       = "3.91 mm/tx GTX Contact-Clip + BVH4 Penumbra directional sun and physical SkyView LUT driver.";
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

    InOutBundle.AreaLightBindings.clear();
    InOutBundle.CeilingSoftboxToken = {};
    if (!InOutBundle.Scene.AreaLights.empty())
    {
        InOutBundle.LightingFolderToken = TargetOutliner.ConstructFolder("Lighting");
        if (OutlinerRecordEntry* Folder = TargetOutliner.AccessEntry(InOutBundle.LightingFolderToken))
        {
            Folder->FolderTint  = ColorRgba{ 226, 152, 128, 255 };
            Folder->Subtitle    = "Analytic LTC area lights";
            Folder->AuthorNotes = "Clipped-horizon polygonal quad lights integrated via LinearCosineLightIntegration.";
            Folder->Expanded    = true;
        }

        InOutBundle.CeilingSoftboxToken = TargetOutliner.ConstructFromTemplate(LightTemplateOrd, InOutBundle.LightingFolderToken);
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
    }

    OutlinerFooterReadings Footer {};
    Footer.RealtimeText  = "60 fps";
    Footer.QualityText   = "GTX Tier 1";
    Footer.PrimarySlot   = "Sun 46°";
    Footer.SecondarySlot = InOutBundle.Scene.AreaLights.empty() ? "Clear Sky" : "LTC 1";
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

    if (InOutBundle.Objects.size() > 6u && InOutBundle.Objects[6u].SlotToken.Valid())
    {
        TargetOutliner.SelectSingle(InOutBundle.Objects[6u].SlotToken);
    }
    else if (InOutBundle.Objects.size() > 1u && InOutBundle.Objects[1u].SlotToken.Valid())
    {
        TargetOutliner.SelectSingle(InOutBundle.Objects[1u].SlotToken);
    }
    else if (!InOutBundle.Objects.empty() && InOutBundle.Objects[0u].SlotToken.Valid())
    {
        TargetOutliner.SelectSingle(InOutBundle.Objects[0u].SlotToken);
    }
}

void RebuildShowcaseSceneAcceleration(ShowcaseWorldSceneBundle& InOutBundle)
{
    std::size_t TotalTriangleCount = 0u;
    for (ShowcaseWorldAssetEntry& Obj : InOutBundle.Objects)
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
    for (ShowcaseWorldAssetEntry& Obj : InOutBundle.Objects)
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
            Prim.MaterialOrdinal  = (!Obj.DedicatedMaterial && LocalTri.MaterialSlotOverride != 0xFFFFu)
                ? LocalTri.MaterialSlotOverride
                : Mat;
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
        InOutBundle.Scene.CompressedSceneTree.Reset();
        InOutBundle.Scene.GtxInteriorBoxes.clear();
    }
    else
    {
        (void)InOutBundle.Scene.SceneTree.BuildFromTrianglePrimitivesWithNormals(WorldPrims, WorldNormals);
        (void)InOutBundle.Scene.CompressedSceneTree.BuildFromPrimitives(WorldPrims, WorldNormals);
        InOutBundle.Scene.GtxInteriorBoxes.clear();
        for (const ShowcaseWorldAssetEntry& Obj : InOutBundle.Objects)
        {
            if (Obj.GlassShell || Obj.ClusterInstanceIndex >= InOutBundle.Scene.ClusterInstances.size())
            {
                continue;
            }
            const auto& Inst = InOutBundle.Scene.ClusterInstances[Obj.ClusterInstanceIndex];
            if (Inst.UniformScale <= 1.0e-4f)
            {
                continue;
            }
            // Skip floor horizontal slab; register vertical walls, roof baffle, pedestal, and specimen bounding volumes for GtxLowEnd SDF
            const Mathematics::Axis3D HalfExt {
                Obj.DimensionsMeters.X * 0.5f,
                Obj.DimensionsMeters.Y * 0.5f,
                Obj.DimensionsMeters.Z * 0.5f,
            };
            if (HalfExt.Z < 0.12f && Obj.CenterWorld.Z < -0.20f)
            {
                continue;
            }
            DistanceFieldSpace::ClosedOccluderBox Box {};
            Box.CenterWorld = Obj.CenterWorld;
            Box.HalfExtent  = HalfExt;
            InOutBundle.Scene.GtxInteriorBoxes.push_back(Box);
        }
    }
}

bool SynchronizeShowcaseGalleryFromOutliner(
    const OutlinerPanel&      SourceOutliner,
    ShowcaseWorldSceneBundle& InOutBundle)
{
    bool Changed         = false;
    bool AccelInvalidated = false;

    for (ShowcaseWorldAssetEntry& Obj : InOutBundle.Objects)
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
            Inst.UniformScale    = TargetScale;
            Inst.NonUniformScale = { TargetScale, TargetScale, TargetScale };
            Changed              = true;
            AccelInvalidated     = true;
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
            if (!NearlyEqualFloat(Obj.RotationZDegrees, Entry->RotationZ) ||
                !NearlyEqualFloat(Inst.RotationZDegrees, Entry->RotationZ))
            {
                Obj.RotationZDegrees  = Entry->RotationZ;
                Inst.RotationZDegrees = Entry->RotationZ;
                Changed               = true;
                AccelInvalidated      = true;
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
        RebuildShowcaseSceneAcceleration(InOutBundle);
    }

    return Changed;
}

bool SynchronizeShowcaseGallerySceneLive(
    OutlinerPanel&            InOutOutliner,
    ShowcaseWorldSceneBundle& InOutBundle,
    bool&                     OutRequiresAccelerationRebuild)
{
    OutRequiresAccelerationRebuild = false;
    bool Changed                   = false;

    // 1. Adopt newly constructed geometry (CategoryOne) and LTC area lights (CategoryThree) from OutlinerPanel
    for (const OutlinerRecordEntry& Entry : InOutOutliner.QueryEntries())
    {
        if (Entry.Category == ConstructSlotCategory::Folder ||
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
                [&](const ShowcaseWorldAssetEntry& Obj) { return Obj.SlotToken == Entry.Token; });
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
                {
                    RigidClusterInstance SpawnInst {};
                    SpawnInst.PyramidAsset        = &DynAsset->Pyramid;
                    SpawnInst.WorldTranslation    = SpawnCenter;
                    SpawnInst.PreviousTranslation = SpawnCenter;
                    SpawnInst.UniformScale        = 1.0f;
                    SpawnInst.MaterialSlot        = MatSlot;
                    SpawnInst.TransparentGlass    = false;
                    SpawnInst.RotationZDegrees    = Entry.RotationZ;
                    SpawnInst.NonUniformScale     = { 1.0f, 1.0f, 1.0f };
                    SpawnInst.LocalHalfExtents    = { Dims.X * 0.5f, Dims.Y * 0.5f, Dims.Z * 0.5f };
                    SpawnInst.AnalyticalShapeCode = EncodeAnalyticalShapeCode(Shape);
                    SpawnInst.LocalSdfVolume      = nullptr;
                    InOutBundle.Scene.ClusterInstances.push_back(SpawnInst);
                }
                InOutBundle.DynamicGeometryAssets.push_back(std::move(DynAsset));

                ShowcaseWorldAssetEntry NewObj {};
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
                [&](const ShowcaseAreaLightLink& L) { return L.SlotToken == Entry.Token; });
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
    for (ShowcaseAreaLightLink& Binding : InOutBundle.AreaLightBindings)
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
    for (ShowcaseWorldAssetEntry& Obj : InOutBundle.Objects)
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
            Inst.UniformScale    = TargetScale;
            Inst.NonUniformScale = { TargetScale, TargetScale, TargetScale };
            Changed              = true;
            AccelInvalidated     = true;
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
            if (!NearlyEqualFloat(Obj.RotationZDegrees, Entry->RotationZ) ||
                !NearlyEqualFloat(Inst.RotationZDegrees, Entry->RotationZ))
            {
                Obj.RotationZDegrees  = Entry->RotationZ;
                Inst.RotationZDegrees = Entry->RotationZ;
                Changed               = true;
                AccelInvalidated      = true;
            }
        }
    }

    if (AccelInvalidated || OutRequiresAccelerationRebuild)
    {
        RebuildShowcaseSceneAcceleration(InOutBundle);
        OutRequiresAccelerationRebuild = true;
    }

    if (SynchronizeShowcaseGalleryFromOutliner(InOutOutliner, InOutBundle))
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
    Mathematics::Axis3D       WorldPosition)
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
    (void)SynchronizeShowcaseGallerySceneLive(InOutOutliner, InOutBundle, NeedsRebuild);
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

    for (ShowcaseWorldAssetEntry& Obj : InOutBundle.Objects)
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
    OutlinerSlotToken         ExplicitEnclosure)
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
        (void)SynchronizeShowcaseGallerySceneLive(InOutOutliner, InOutBundle, NeedsRebuild);
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
        FloorPlaced);

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
        for (ShowcaseWorldAssetEntry& Obj : InOutBundle.Objects)
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

    for (ShowcaseWorldAssetEntry& Obj : InOutBundle.Objects)
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

    for (const ShowcaseWorldAssetEntry& Obj : Bundle.Objects)
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
            const float YawRad = Obj.RotationZDegrees * 0.01745329252f;
            if (std::fabs(YawRad) > 1.0e-5f)
            {
                const float CosZ = std::cos(YawRad);
                const float SinZ = std::sin(YawRad);
                const Mathematics::Axis3D LocalOrig = RotateAroundZ(RayOrigin - Obj.CenterWorld, CosZ, -SinZ);
                const Mathematics::Axis3D LocalDir  = RotateAroundZ(RayDir, CosZ, -SinZ);
                Hit = IntersectRayAabb(LocalOrig, LocalDir, { 0.0f, 0.0f, 0.0f }, HalfExt, HitDist);
            }
            else
            {
                Hit = IntersectRayAabb(RayOrigin, RayDir, Obj.CenterWorld, HalfExt, HitDist);
            }
        }

        if (Hit && HitDist < ClosestDistance)
        {
            ClosestDistance = HitDist;
            ClosestToken    = Obj.SlotToken;
        }
    }

    for (const ShowcaseAreaLightLink& LightBind : Bundle.AreaLightBindings)
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
