/*============================================================================================================================================
                                                       LOCALSIGNEDDISTANCEVOLUME.CPP
============================================================================================================================================*/
// 🧩 Multi-threaded BVH4-accelerated per-asset compact local distance + normal/material volume baking & rigid 3D sampling

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/LocalSignedDistanceVolume.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>

namespace Frontier::DistanceFieldSpace
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

std::atomic<uint32_t> NextVolumeAssetSerial { 1u };

[[nodiscard]] uint16_t PackFloat32ToFloat16(float Value) noexcept
{
    uint32_t Bits = 0u;
    std::memcpy(&Bits, &Value, sizeof(uint32_t));
    const uint32_t Sign = (Bits >> 16u) & 0x8000u;
    const int32_t  Exp  = static_cast<int32_t>((Bits >> 23u) & 0xFFu) - 127 + 15;
    uint32_t       Mant = Bits & 0x007FFFFFu;

    if (Exp <= 0)
    {
        if (Exp < -10)
        {
            return static_cast<uint16_t>(Sign);
        }
        Mant = (Mant | 0x00800000u) >> static_cast<uint32_t>(1 - Exp);
        return static_cast<uint16_t>(Sign | ((Mant + 0x00001000u) >> 13u));
    }
    if (Exp >= 31)
    {
        return static_cast<uint16_t>(Sign | 0x7BFFu);
    }
    return static_cast<uint16_t>(Sign | (static_cast<uint32_t>(Exp) << 10u) | ((Mant + 0x00001000u) >> 13u));
}

[[nodiscard]] float UnpackFloat16ToFloat32(uint16_t HalfBits) noexcept
{
    const uint32_t Sign = (static_cast<uint32_t>(HalfBits) & 0x8000u) << 16u;
    const uint32_t Exp  = (static_cast<uint32_t>(HalfBits) >> 10u) & 0x1Fu;
    uint32_t       Mant = static_cast<uint32_t>(HalfBits) & 0x03FFu;

    uint32_t OutBits = 0u;
    if (Exp == 0u)
    {
        if (Mant == 0u)
        {
            OutBits = Sign;
        }
        else
        {
            uint32_t ShiftExp = 127u - 15u + 1u;
            while ((Mant & 0x0400u) == 0u)
            {
                Mant <<= 1u;
                --ShiftExp;
            }
            Mant &= 0x03FFu;
            OutBits = Sign | (ShiftExp << 23u) | (Mant << 13u);
        }
    }
    else if (Exp == 31u)
    {
        OutBits = Sign | 0x7F800000u | (Mant << 13u);
    }
    else
    {
        OutBits = Sign | ((Exp + 127u - 15u) << 23u) | (Mant << 13u);
    }

    float Result = 0.0f;
    std::memcpy(&Result, &OutBits, sizeof(float));
    return Result;
}

[[nodiscard]] uint32_t PackOctNormalAndMaterial(Mathematics::Axis3D UnitNormal, uint16_t MaterialOrdinal) noexcept
{
    const float InvL1 = 1.0f / std::max(1.0e-6f, std::fabs(UnitNormal.X) + std::fabs(UnitNormal.Y) + std::fabs(UnitNormal.Z));
    float OctX = UnitNormal.X * InvL1;
    float OctY = UnitNormal.Y * InvL1;
    if (UnitNormal.Z < 0.0f)
    {
        const float WrapX = (1.0f - std::fabs(OctY)) * ((OctX >= 0.0f) ? 1.0f : -1.0f);
        const float WrapY = (1.0f - std::fabs(OctX)) * ((OctY >= 0.0f) ? 1.0f : -1.0f);
        OctX = WrapX;
        OctY = WrapY;
    }
    const uint32_t QuantX = static_cast<uint32_t>(std::clamp(std::lround((OctX * 0.5f + 0.5f) * 1023.0f), 0l, 1023l));
    const uint32_t QuantY = static_cast<uint32_t>(std::clamp(std::lround((OctY * 0.5f + 0.5f) * 1023.0f), 0l, 1023l));
    const uint32_t Mat12  = static_cast<uint32_t>(std::min<uint16_t>(MaterialOrdinal, 4095u));
    return QuantX | (QuantY << 10u) | (Mat12 << 20u);
}

struct ClosestPointTriangleResult
{
    float               DistSq = 0.0f;
    Mathematics::Axis3D Normal { 0.0f, 0.0f, 1.0f };
};

[[nodiscard]] ClosestPointTriangleResult EvaluatePointTriangleClosest(
    Mathematics::Axis3D                              Point,
    const SpatialAcceleration::RayTrianglePrimitive& Tri) noexcept
{
    const Mathematics::Axis3D A  = Tri.Corner0;
    const Mathematics::Axis3D AB = Tri.Edge01;
    const Mathematics::Axis3D AC = Tri.Edge02;
    const Mathematics::Axis3D AP = Point - A;

    const Mathematics::Axis3D FaceCross = Mathematics::EvaluateCross(AB, AC);
    const Mathematics::Axis3D FaceNorm  = Mathematics::NormalizeAxis(FaceCross, { 0.0f, 0.0f, 1.0f });

    const float D1 = Mathematics::EvaluateDot(AB, AP);
    const float D2 = Mathematics::EvaluateDot(AC, AP);
    if (D1 <= 0.0f && D2 <= 0.0f)
    {
        return { Mathematics::EvaluateLengthSquared(AP), FaceNorm };
    }

    const Mathematics::Axis3D BP = Point - (A + AB);
    const float D3 = Mathematics::EvaluateDot(AB, BP);
    const float D4 = Mathematics::EvaluateDot(AC, BP);
    if (D3 >= 0.0f && D4 <= D3)
    {
        return { Mathematics::EvaluateLengthSquared(BP), FaceNorm };
    }

    const float VC = D1 * D4 - D3 * D2;
    if (VC <= 0.0f && D1 >= 0.0f && D3 <= 0.0f)
    {
        const float V = D1 / (D1 - D3);
        return { Mathematics::EvaluateLengthSquared(Point - (A + AB * V)), FaceNorm };
    }

    const Mathematics::Axis3D CP = Point - (A + AC);
    const float D5 = Mathematics::EvaluateDot(AB, CP);
    const float D6 = Mathematics::EvaluateDot(AC, CP);
    if (D6 >= 0.0f && D5 <= D6)
    {
        return { Mathematics::EvaluateLengthSquared(CP), FaceNorm };
    }

    const float VB = D5 * D2 - D1 * D6;
    if (VB <= 0.0f && D2 >= 0.0f && D6 <= 0.0f)
    {
        const float W = D2 / (D2 - D6);
        return { Mathematics::EvaluateLengthSquared(Point - (A + AC * W)), FaceNorm };
    }

    const float VA = D3 * D6 - D5 * D4;
    if (VA <= 0.0f && (D4 - D3) >= 0.0f && (D5 - D6) >= 0.0f)
    {
        const float W = (D4 - D3) / ((D4 - D3) + (D5 - D6));
        return { Mathematics::EvaluateLengthSquared(Point - (A + AB + (AC - AB) * W)), FaceNorm };
    }

    const float Denom = 1.0f / (VA + VB + VC);
    const float V     = VB * Denom;
    const float W     = VC * Denom;
    return { Mathematics::EvaluateLengthSquared(Point - (A + AB * V + AC * W)), FaceNorm };
}

[[nodiscard]] float PointAabbDistanceSq(
    Mathematics::Axis3D P,
    float               MinX,
    float               MinY,
    float               MinZ,
    float               MaxX,
    float               MaxY,
    float               MaxZ) noexcept
{
    const float DX = std::max({ 0.0f, MinX - P.X, P.X - MaxX });
    const float DY = std::max({ 0.0f, MinY - P.Y, P.Y - MaxY });
    const float DZ = std::max({ 0.0f, MinZ - P.Z, P.Z - MaxZ });
    return DX * DX + DY * DY + DZ * DZ;
}

struct ExactClosestSurfaceSample
{
    float    DistanceMeters = 0.0f;
    uint32_t PackedNormMat  = 0u;
};

[[nodiscard]] ExactClosestSurfaceSample QueryExactUnsignedDistanceViaBvh(
    const SpatialAcceleration::WideBoundingTree& Tree,
    Mathematics::Axis3D                          QueryPoint) noexcept
{
    if (Tree.WideEntries.empty() || Tree.Primitives.empty())
    {
        return { 1.0e6f, PackOctNormalAndMaterial({ 0.0f, 0.0f, 1.0f }, 0u) };
    }

    float               BestDistSq = std::numeric_limits<float>::max();
    Mathematics::Axis3D BestNormal { 0.0f, 0.0f, 1.0f };
    uint16_t            BestMat    = 0u;

    std::array<uint32_t, 64u> Stack {};
    uint32_t StackTop = 0u;
    Stack[StackTop++] = 0u;

    while (StackTop > 0u)
    {
        const uint32_t NodeIdx = Stack[--StackTop];
        const SpatialAcceleration::WideTreeEntry& Node = Tree.WideEntries[NodeIdx];

        struct ChildHit
        {
            uint32_t Slot   = 0u;
            float    DistSq = 0.0f;
        };
        std::array<ChildHit, 4u> Hits {};
        uint32_t HitCount = 0u;

        for (uint32_t Lane = 0u; Lane < 4u; ++Lane)
        {
            if (Node.BranchReference[Lane] == 0xFFFFFFFFu)
            {
                continue;
            }
            const float BoxSq = PointAabbDistanceSq(
                QueryPoint,
                Node.BranchBoundsMinX[Lane], Node.BranchBoundsMinY[Lane], Node.BranchBoundsMinZ[Lane],
                Node.BranchBoundsMaxX[Lane], Node.BranchBoundsMaxY[Lane], Node.BranchBoundsMaxZ[Lane]);
            if (BoxSq < BestDistSq)
            {
                Hits[HitCount++] = { Lane, BoxSq };
            }
        }

        for (uint32_t I = 1u; I < HitCount; ++I)
        {
            const ChildHit Key = Hits[I];
            uint32_t J = I;
            while (J > 0u && Hits[J - 1u].DistSq < Key.DistSq)
            {
                Hits[J] = Hits[J - 1u];
                --J;
            }
            Hits[J] = Key;
        }

        // Evaluate leaf lanes nearest-first, and push internal lanes farthest-first so LIFO pops nearest first.
        for (uint32_t H = 0u; H < HitCount; ++H)
        {
            const uint32_t Lane      = Hits[H].Slot;
            const uint32_t PrimCount = Node.BranchPrimitiveCount[Lane];
            if (PrimCount == 0u || Hits[H].DistSq >= BestDistSq)
            {
                continue;
            }
            const uint32_t LeafStart = Node.BranchReference[Lane] & 0x7FFFFFFFu;
            for (uint32_t Off = 0u; Off < PrimCount; ++Off)
            {
                const auto& Prim = Tree.Primitives[LeafStart + Off];
                const ClosestPointTriangleResult Hit = EvaluatePointTriangleClosest(QueryPoint, Prim);
                if (Hit.DistSq < BestDistSq)
                {
                    BestDistSq = Hit.DistSq;
                    BestNormal = Hit.Normal;
                    BestMat    = Prim.MaterialOrdinal;
                }
            }
        }

        for (int32_t H = static_cast<int32_t>(HitCount) - 1; H >= 0; --H)
        {
            const uint32_t Lane      = Hits[static_cast<uint32_t>(H)].Slot;
            const uint32_t PrimCount = Node.BranchPrimitiveCount[Lane];
            if (PrimCount > 0u || Hits[static_cast<uint32_t>(H)].DistSq >= BestDistSq)
            {
                continue;
            }
            if (StackTop < Stack.size())
            {
                Stack[StackTop++] = Node.BranchReference[Lane];
            }
        }
    }

    const float Dist = std::sqrt(std::max(0.0f, BestDistSq));
    return { Dist, PackOctNormalAndMaterial(BestNormal, BestMat) };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              CLOSED OCCLUDER BOX ANALYTIC
//------------------------------------------------------------------------------------------------------------------------

float ClosedOccluderBox::EvaluateSignedDistance(Mathematics::Axis3D WorldPos) const noexcept
{
    const float DX = std::fabs(WorldPos.X - CenterWorld.X) - HalfExtent.X;
    const float DY = std::fabs(WorldPos.Y - CenterWorld.Y) - HalfExtent.Y;
    const float DZ = std::fabs(WorldPos.Z - CenterWorld.Z) - HalfExtent.Z;
    const float OX = std::max(DX, 0.0f);
    const float OY = std::max(DY, 0.0f);
    const float OZ = std::max(DZ, 0.0f);
    const float Outside = std::sqrt(OX * OX + OY * OY + OZ * OZ);
    const float Inside  = std::min(std::max({ DX, DY, DZ }), 0.0f);
    return Outside + Inside;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            RIGID LOCAL VOLUME PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

void RigidLocalVolumePlacement::ConfigureRigidTransform(
    const LocalSignedDistanceVolume* Asset,
    Mathematics::Axis3D              WorldPosition,
    Mathematics::Axis3D              EulerRotationDegrees,
    float                            Scale,
    uint16_t                         OverrideMatSlot) noexcept
{
    constexpr float DegToRad = 0.01745329252f;
    const float Rx = EulerRotationDegrees.X * DegToRad;
    const float Ry = EulerRotationDegrees.Y * DegToRad;
    const float Rz = EulerRotationDegrees.Z * DegToRad;

    const float Cx = std::cos(Rx), Sx = std::sin(Rx);
    const float Cy = std::cos(Ry), Sy = std::sin(Ry);
    const float Cz = std::cos(Rz), Sz = std::sin(Rz);

    LocalToWorldRow0 = { Cz * Cy, Cz * Sy * Sx - Sz * Cx, Cz * Sy * Cx + Sz * Sx };
    LocalToWorldRow1 = { Sz * Cy, Sz * Sy * Sx + Cz * Cx, Sz * Sy * Cx - Cz * Sx };
    LocalToWorldRow2 = { -Sy,     Cy * Sx,                Cy * Cx                };

    WorldToLocalRow0 = { LocalToWorldRow0.X, LocalToWorldRow1.X, LocalToWorldRow2.X };
    WorldToLocalRow1 = { LocalToWorldRow0.Y, LocalToWorldRow1.Y, LocalToWorldRow2.Y };
    WorldToLocalRow2 = { LocalToWorldRow0.Z, LocalToWorldRow1.Z, LocalToWorldRow2.Z };

    VolumeAsset          = Asset;
    Translation          = WorldPosition;
    CosineYaw            = Cz;
    SineYaw              = Sz;
    ActiveToggle         = (Scale > 1.0e-4f);
    UniformScale         = std::max(1.0e-4f, Scale);
    MaterialSlotOverride = OverrideMatSlot;
    FullMatrixValid      = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             LOCAL SIGNED DISTANCE VOLUME
//------------------------------------------------------------------------------------------------------------------------

void LocalSignedDistanceVolume::Reset() noexcept
{
    ResolutionX       = 0u;
    ResolutionY       = 0u;
    ResolutionZ       = 0u;
    UniqueAssetSerial = 0u;
    LocalMin          = {};
    LocalMax          = {};
    VoxelSpacing      = {};
    HalfDistanceSamples.clear();
    DistanceSamples.clear();
    PackedNormalMaterialSamples.clear();
}

bool LocalSignedDistanceVolume::BakeFromTriangles(
    std::span<const SpatialAcceleration::RayTrianglePrimitive> InputPrimitives,
    uint32_t                                                   CubicResolution,
    float                                                      MarginMeters)
{
    return BakeFromTrianglesAspect(InputPrimitives, CubicResolution, CubicResolution, CubicResolution, MarginMeters);
}

bool LocalSignedDistanceVolume::BakeFromTrianglesAspect(
    std::span<const SpatialAcceleration::RayTrianglePrimitive> InputPrimitives,
    uint32_t                                                   ResX,
    uint32_t                                                   ResY,
    uint32_t                                                   ResZ,
    float                                                      MarginMeters)
{
    Reset();
    if (InputPrimitives.empty() || ResX < 8u || ResY < 8u || ResZ < 8u)
    {
        return false;
    }

    SpatialAcceleration::WideBoundingTree LocalBvh {};
    if (!LocalBvh.BuildFromTrianglePrimitives(InputPrimitives))
    {
        return false;
    }

    Mathematics::Axis3D MinPt { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    Mathematics::Axis3D MaxPt { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
    for (const auto& Tri : InputPrimitives)
    {
        const std::array<Mathematics::Axis3D, 3u> Corners {{ Tri.Corner0, Tri.Corner0 + Tri.Edge01, Tri.Corner0 + Tri.Edge02 }};
        for (const auto& C : Corners)
        {
            MinPt.X = std::min(MinPt.X, C.X);
            MinPt.Y = std::min(MinPt.Y, C.Y);
            MinPt.Z = std::min(MinPt.Z, C.Z);
            MaxPt.X = std::max(MaxPt.X, C.X);
            MaxPt.Y = std::max(MaxPt.Y, C.Y);
            MaxPt.Z = std::max(MaxPt.Z, C.Z);
        }
    }

    ResolutionX       = ResX;
    ResolutionY       = ResY;
    ResolutionZ       = ResZ;
    UniqueAssetSerial = NextVolumeAssetSerial.fetch_add(1u, std::memory_order_relaxed);
    LocalMin          = { MinPt.X - MarginMeters, MinPt.Y - MarginMeters, MinPt.Z - MarginMeters };
    LocalMax          = { MaxPt.X + MarginMeters, MaxPt.Y + MarginMeters, MaxPt.Z + MarginMeters };

    VoxelSpacing = {
        std::max(1.0e-5f, LocalMax.X - LocalMin.X) / static_cast<float>(ResolutionX - 1u),
        std::max(1.0e-5f, LocalMax.Y - LocalMin.Y) / static_cast<float>(ResolutionY - 1u),
        std::max(1.0e-5f, LocalMax.Z - LocalMin.Z) / static_cast<float>(ResolutionZ - 1u),
    };

    const std::size_t TotalVoxels =
        static_cast<std::size_t>(ResolutionX) *
        static_cast<std::size_t>(ResolutionY) *
        static_cast<std::size_t>(ResolutionZ);
    HalfDistanceSamples.resize(TotalVoxels, 0u);
    DistanceSamples.resize(TotalVoxels, 0.0f);
    PackedNormalMaterialSamples.resize(TotalVoxels, 0u);

    const uint32_t WorkerCount = std::max(1u, std::min<uint32_t>(ResolutionZ, std::thread::hardware_concurrency()));
    std::vector<std::thread> Workers;
    Workers.reserve(WorkerCount);

    for (uint32_t Worker = 0u; Worker < WorkerCount; ++Worker)
    {
        Workers.emplace_back([&, Worker]()
        {
            for (uint32_t Z = Worker; Z < ResolutionZ; Z += WorkerCount)
            {
                const float PosZ = LocalMin.Z + static_cast<float>(Z) * VoxelSpacing.Z;
                for (uint32_t Y = 0u; Y < ResolutionY; ++Y)
                {
                    const float PosY = LocalMin.Y + static_cast<float>(Y) * VoxelSpacing.Y;
                    const std::size_t RowBase =
                        (static_cast<std::size_t>(Z) * ResolutionY + Y) * ResolutionX;
                    for (uint32_t X = 0u; X < ResolutionX; ++X)
                    {
                        const float PosX = LocalMin.X + static_cast<float>(X) * VoxelSpacing.X;
                        const ExactClosestSurfaceSample Hit = QueryExactUnsignedDistanceViaBvh(LocalBvh, { PosX, PosY, PosZ });
                        const uint16_t PackedHalf           = PackFloat32ToFloat16(Hit.DistanceMeters);
                        HalfDistanceSamples[RowBase + X]         = PackedHalf;
                        DistanceSamples[RowBase + X]             = UnpackFloat16ToFloat32(PackedHalf);
                        PackedNormalMaterialSamples[RowBase + X] = Hit.PackedNormMat;
                    }
                }
            }
        });
    }
    for (auto& W : Workers)
    {
        W.join();
    }

    return true;
}

float LocalSignedDistanceVolume::SampleTrilinearLocal(Mathematics::Axis3D ClampedLocalPos) const noexcept
{
    if (HalfDistanceSamples.empty() || ResolutionX < 2u || ResolutionY < 2u || ResolutionZ < 2u)
    {
        return 1.0e6f;
    }

    const float GX = std::clamp((ClampedLocalPos.X - LocalMin.X) / VoxelSpacing.X, 0.0f, static_cast<float>(ResolutionX - 1u) - 1.0e-5f);
    const float GY = std::clamp((ClampedLocalPos.Y - LocalMin.Y) / VoxelSpacing.Y, 0.0f, static_cast<float>(ResolutionY - 1u) - 1.0e-5f);
    const float GZ = std::clamp((ClampedLocalPos.Z - LocalMin.Z) / VoxelSpacing.Z, 0.0f, static_cast<float>(ResolutionZ - 1u) - 1.0e-5f);

    const uint32_t X0 = static_cast<uint32_t>(GX);
    const uint32_t Y0 = static_cast<uint32_t>(GY);
    const uint32_t Z0 = static_cast<uint32_t>(GZ);
    const uint32_t X1 = std::min(X0 + 1u, ResolutionX - 1u);
    const uint32_t Y1 = std::min(Y0 + 1u, ResolutionY - 1u);
    const uint32_t Z1 = std::min(Z0 + 1u, ResolutionZ - 1u);

    const float TX = GX - static_cast<float>(X0);
    const float TY = GY - static_cast<float>(Y0);
    const float TZ = GZ - static_cast<float>(Z0);

    const auto Fetch = [this](uint32_t X, uint32_t Y, uint32_t Z) noexcept -> float
    {
        return DistanceSamples[(static_cast<std::size_t>(Z) * ResolutionY + Y) * ResolutionX + X];
    };

    const float C00 = Fetch(X0, Y0, Z0) * (1.0f - TX) + Fetch(X1, Y0, Z0) * TX;
    const float C10 = Fetch(X0, Y1, Z0) * (1.0f - TX) + Fetch(X1, Y1, Z0) * TX;
    const float C01 = Fetch(X0, Y0, Z1) * (1.0f - TX) + Fetch(X1, Y0, Z1) * TX;
    const float C11 = Fetch(X0, Y1, Z1) * (1.0f - TX) + Fetch(X1, Y1, Z1) * TX;
    const float C0  = C00 * (1.0f - TY) + C10 * TY;
    const float C1  = C01 * (1.0f - TY) + C11 * TY;
    return C0 * (1.0f - TZ) + C1 * TZ;
}

float LocalSignedDistanceVolume::SampleSmoothWorldDistance(
    const RigidLocalVolumePlacement& Placement,
    Mathematics::Axis3D              WorldPos) noexcept
{
    if (!Placement.ActiveToggle || Placement.VolumeAsset == nullptr || Placement.UniformScale <= 1.0e-4f)
    {
        return 1.0e6f;
    }

    const LocalSignedDistanceVolume& Vol = *Placement.VolumeAsset;
    const Mathematics::Axis3D Rel = (WorldPos - Placement.Translation) * (1.0f / Placement.UniformScale);
    Mathematics::Axis3D Local {};
    if (Placement.FullMatrixValid)
    {
        Local = {
            Mathematics::EvaluateDot(Placement.WorldToLocalRow0, Rel),
            Mathematics::EvaluateDot(Placement.WorldToLocalRow1, Rel),
            Mathematics::EvaluateDot(Placement.WorldToLocalRow2, Rel),
        };
    }
    else
    {
        Local = {
            Rel.X * Placement.CosineYaw + Rel.Y * Placement.SineYaw,
           -Rel.X * Placement.SineYaw   + Rel.Y * Placement.CosineYaw,
            Rel.Z,
        };
    }

    const Mathematics::Axis3D Clamped {
        std::clamp(Local.X, Vol.LocalMin.X, Vol.LocalMax.X),
        std::clamp(Local.Y, Vol.LocalMin.Y, Vol.LocalMax.Y),
        std::clamp(Local.Z, Vol.LocalMin.Z, Vol.LocalMax.Z),
    };

    const float BoundaryD = Vol.SampleTrilinearLocal(Clamped);
    const float OX = Local.X - Clamped.X;
    const float OY = Local.Y - Clamped.Y;
    const float OZ = Local.Z - Clamped.Z;
    const float BoxSq = OX * OX + OY * OY + OZ * OZ;
    if (BoxSq <= 1.0e-12f)
    {
        return BoundaryD * Placement.UniformScale;
    }
    return std::sqrt(BoundaryD * BoundaryD + BoxSq) * Placement.UniformScale;
}

} // namespace Frontier::DistanceFieldSpace
