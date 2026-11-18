/*============================================================================================================================================
                                                           VOXELSDFCLIPMAP.CPP
============================================================================================================================================*/
// 🧩 Implementation of Unreal-Style 2-Cascade Voxel SDF Clipmap + Multi-Bounce Surface Cache + Dynamic Local Object Composition

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/VoxelSdfClipmap.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/MaterialSpace/Scattering/OpenPbrSurfaceScattering.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>

namespace Frontier::DistanceFieldSpace
{

namespace
{

struct ClosestTriHit
{
    float                  DistSq       = std::numeric_limits<float>::max();
    Mathematics::Axis3D    ClosestPt    {};
    Mathematics::Axis3D    Normal       { 0.0f, 0.0f, 1.0f };
    uint16_t               MaterialSlot = 0u;
    bool                   Valid        = false;
};

[[nodiscard]] float PointTriDistSq(
    Mathematics::Axis3D  Point,
    Mathematics::Axis3D  CornerA,
    Mathematics::Axis3D  EdgeAB,
    Mathematics::Axis3D  EdgeAC,
    Mathematics::Axis3D& OutClosest) noexcept
{
    const Mathematics::Axis3D DiffAP = Point - CornerA;
    const float Dot1 = Mathematics::EvaluateDot(EdgeAB, DiffAP);
    const float Dot2 = Mathematics::EvaluateDot(EdgeAC, DiffAP);
    if (Dot1 <= 0.0f && Dot2 <= 0.0f)
    {
        OutClosest = CornerA;
        return Mathematics::EvaluateLengthSquared(Point - OutClosest);
    }

    const Mathematics::Axis3D CornerB = CornerA + EdgeAB;
    const Mathematics::Axis3D DiffBP  = Point - CornerB;
    const float Dot3 = Mathematics::EvaluateDot(EdgeAB, DiffBP);
    const float Dot4 = Mathematics::EvaluateDot(EdgeAC, DiffBP);
    if (Dot3 >= 0.0f && Dot4 <= Dot3)
    {
        OutClosest = CornerB;
        return Mathematics::EvaluateLengthSquared(Point - OutClosest);
    }

    const float EdgeVC = Dot1 * Dot4 - Dot3 * Dot2;
    if (EdgeVC <= 0.0f && Dot1 >= 0.0f && Dot3 <= 0.0f)
    {
        const float W = Dot1 / std::max(1.0e-12f, Dot1 - Dot3);
        OutClosest = CornerA + EdgeAB * W;
        return Mathematics::EvaluateLengthSquared(Point - OutClosest);
    }

    const Mathematics::Axis3D CornerC = CornerA + EdgeAC;
    const Mathematics::Axis3D DiffCP  = Point - CornerC;
    const float Dot5 = Mathematics::EvaluateDot(EdgeAB, DiffCP);
    const float Dot6 = Mathematics::EvaluateDot(EdgeAC, DiffCP);
    if (Dot6 >= 0.0f && Dot5 <= Dot6)
    {
        OutClosest = CornerC;
        return Mathematics::EvaluateLengthSquared(Point - OutClosest);
    }

    const float EdgeVB = Dot5 * Dot2 - Dot1 * Dot6;
    if (EdgeVB <= 0.0f && Dot2 >= 0.0f && Dot6 <= 0.0f)
    {
        const float W = Dot2 / std::max(1.0e-12f, Dot2 - Dot6);
        OutClosest = CornerA + EdgeAC * W;
        return Mathematics::EvaluateLengthSquared(Point - OutClosest);
    }

    const float EdgeVA = Dot3 * Dot6 - Dot5 * Dot4;
    if (EdgeVA <= 0.0f && (Dot4 - Dot3) >= 0.0f && (Dot5 - Dot6) >= 0.0f)
    {
        const float W = (Dot4 - Dot3) / std::max(1.0e-12f, (Dot4 - Dot3) + (Dot5 - Dot6));
        OutClosest = CornerB + (CornerC - CornerB) * W;
        return Mathematics::EvaluateLengthSquared(Point - OutClosest);
    }

    const float Denom = 1.0f / std::max(1.0e-12f, EdgeVA + EdgeVB + EdgeVC);
    OutClosest = CornerA + EdgeAB * (EdgeVB * Denom) + EdgeAC * (EdgeVC * Denom);
    return Mathematics::EvaluateLengthSquared(Point - OutClosest);
}

[[nodiscard]] float PointAabbDistSq(
    Mathematics::Axis3D Pt,
    float MinX, float MaxX,
    float MinY, float MaxY,
    float MinZ, float MaxZ) noexcept
{
    if (MinX > MaxX) return std::numeric_limits<float>::max();
    const float DX = std::max({ 0.0f, MinX - Pt.X, Pt.X - MaxX });
    const float DY = std::max({ 0.0f, MinY - Pt.Y, Pt.Y - MaxY });
    const float DZ = std::max({ 0.0f, MinZ - Pt.Z, Pt.Z - MaxZ });
    return DX * DX + DY * DY + DZ * DZ;
}

[[nodiscard]] ClosestTriHit QueryClosestTriangleBvh(
    const SpatialAcceleration::WideBoundingTree& Tree,
    Mathematics::Axis3D                          QueryPoint,
    float                                        CutoffDist) noexcept
{
    ClosestTriHit Best {};
    if (Tree.WideEntries.empty()) return Best;

    Best.DistSq = CutoffDist * CutoffDist;
    std::array<uint32_t, 64u> Stack {};
    uint32_t Top = 0u;
    Stack[Top++] = 0u;

    while (Top > 0u)
    {
        const auto& Entry = Tree.WideEntries[Stack[--Top]];
        std::array<float, 4u>    Dists {};
        std::array<uint32_t, 4u> Order { 0u, 1u, 2u, 3u };
        for (uint32_t S = 0u; S < 4u; ++S)
        {
            Dists[S] = (Entry.BranchReference[S] == SpatialAcceleration::EmptyBranchReference)
                ? std::numeric_limits<float>::max()
                : PointAabbDistSq(
                      QueryPoint,
                      Entry.BranchBoundsMinX[S], Entry.BranchBoundsMaxX[S],
                      Entry.BranchBoundsMinY[S], Entry.BranchBoundsMaxY[S],
                      Entry.BranchBoundsMinZ[S], Entry.BranchBoundsMaxZ[S]);
        }
        // Visit farthest first on stack so nearest pops first
        for (int A = 0; A < 3; ++A)
        {
            for (int B = A + 1; B < 4; ++B)
            {
                if (Dists[Order[A]] < Dists[Order[B]])
                {
                    std::swap(Order[A], Order[B]);
                }
            }
        }

        for (uint32_t K = 0u; K < 4u; ++K)
        {
            const uint32_t S = Order[K];
            const uint32_t Ref = Entry.BranchReference[S];
            if (Ref == SpatialAcceleration::EmptyBranchReference || Dists[S] >= Best.DistSq)
            {
                continue;
            }
            if ((Ref & SpatialAcceleration::WideLeafReferenceMask) != 0u)
            {
                const uint32_t Start = Ref & ~SpatialAcceleration::WideLeafReferenceMask;
                const uint32_t Count = Entry.BranchPrimitiveCount[S];
                for (uint32_t P = 0u; P < Count; ++P)
                {
                    const auto& Tri = Tree.Primitives[Start + P];
                    Mathematics::Axis3D CP {};
                    const float D2 = PointTriDistSq(QueryPoint, Tri.Corner0, Tri.Edge01, Tri.Edge02, CP);
                    if (D2 < Best.DistSq)
                    {
                        Best.DistSq       = D2;
                        Best.ClosestPt    = CP;
                        Best.MaterialSlot = Tri.MaterialOrdinal;
                        Best.Normal       = Mathematics::NormalizeAxis(
                            Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02),
                            { 0.0f, 0.0f, 1.0f });
                        Best.Valid        = true;
                    }
                }
            }
            else if (Top < 63u)
            {
                Stack[Top++] = Ref;
            }
        }
    }
    return Best;
}

[[nodiscard]] float ComputeSignedDistanceWithMajorityVote(
    const SpatialAcceleration::WideBoundingTree& Tree,
    Mathematics::Axis3D                          Pt,
    float                                        CutoffDist,
    ClosestTriHit&                               OutHit) noexcept
{
    OutHit = QueryClosestTriangleBvh(Tree, Pt, CutoffDist);
    if (!OutHit.Valid)
    {
        return CutoffDist;
    }
    const float UnsignedD = std::sqrt(std::max(0.0f, OutHit.DistSq));
    if (UnsignedD < 1.0e-5f)
    {
        return 0.0f;
    }

    // 6-direction majority vote inside/outside check for robust closed & thin-shell sign reconstruction
    if (UnsignedD < CutoffDist * 0.85f)
    {
        static constexpr std::array<Mathematics::Axis3D, 6u> ProbeDirs {{
            {  1.0000f,  0.0000f,  0.0000f },
            { -1.0000f,  0.0000f,  0.0000f },
            {  0.0000f,  1.0000f,  0.0000f },
            {  0.0000f, -1.0000f,  0.0000f },
            {  0.0000f,  0.0000f,  1.0000f },
            {  0.57735f, 0.57735f, 0.57735f },
        }};
        int BackVotes = 0;
        for (const auto& Dir : ProbeDirs)
        {
            SpatialAcceleration::RaySegmentQuery Q {};
            Q.Origin          = Pt;
            Q.UnitDirection   = Dir;
            Q.MinimumDistance = 1.0e-4f;
            Q.MaximumDistance = 32.0f;
            const auto Hit = Tree.TraceClosestRay(Q, false);
            if (Hit.HitConfirmed && Mathematics::EvaluateDot(Hit.GeometricNormal, Dir) > 0.0f)
            {
                ++BackVotes;
            }
        }
        if (BackVotes >= 4)
        {
            return -UnsignedD;
        }
    }
    return UnsignedD;
}

void BuildOrthonormalDuff(Mathematics::Axis3D N, Mathematics::Axis3D& OutT, Mathematics::Axis3D& OutB) noexcept
{
    const float SignZ = (N.Z >= 0.0f) ? 1.0f : -1.0f;
    const float A     = -1.0f / (SignZ + N.Z);
    const float D     = N.X * N.Y * A;
    OutT = { 1.0f + SignZ * N.X * N.X * A, SignZ * D, -SignZ * N.X };
    OutB = { D, SignZ + N.Y * N.Y * A, -N.Y };
}

template <typename Fn>
void ParallelForZ(uint32_t ZCount, Fn&& Func)
{
    const uint32_t ThreadCount = std::min(ZCount, std::max(1u, std::thread::hardware_concurrency()));
    if (ThreadCount <= 1u || ZCount <= 2u)
    {
        Func(0u, ZCount);
        return;
    }
    std::vector<std::thread> Workers;
    Workers.reserve(ThreadCount);
    const uint32_t Chunk = (ZCount + ThreadCount - 1u) / ThreadCount;
    for (uint32_t T = 0u; T < ThreadCount; ++T)
    {
        const uint32_t Start = T * Chunk;
        const uint32_t End   = std::min(ZCount, Start + Chunk);
        if (Start < End)
        {
            Workers.emplace_back([&, Start, End]() { Func(Start, End); });
        }
    }
    for (auto& W : Workers)
    {
        W.join();
    }
}

[[nodiscard]] float SmoothStep(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-6f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] Mathematics::LinearRGB EvaluateDirectVoxelSurfaceLighting(
    const VoxelSurfaceCacheEntry&                         Voxel,
    Mathematics::Axis3D                                   WorldPos,
    const VoxelSdfClipmapVolume&                          Volume,
    const ClipmapLightingMask&                            Mask,
    const GeometricRaster::SolarDirectionalEmitter&       Sun,
    std::span<const VoxelPointLightSpec>                  PointLights,
    std::span<const GeometricRaster::PolygonalAreaQuadLight> AreaLights,
    Mathematics::LinearRGB                                SkyRadiance,
    std::span<const DynamicLocalSdfInstance>              DynamicObjects) noexcept
{
    Mathematics::LinearRGB TotalRad { 0.0f, 0.0f, 0.0f };
    if (Mask.EnableEmissive)
    {
        TotalRad = TotalRad + Voxel.EmissiveRgb;
    }

    const Mathematics::Axis3D N = Mathematics::NormalizeAxis(Voxel.SurfaceNormal, { 0.0f, 0.0f, 1.0f });
    const float DiffWeight = (1.0f - std::clamp(Voxel.Metalness, 0.0f, 1.0f));
    const Mathematics::LinearRGB DiffTint {
        Voxel.AlbedoRgb.R * DiffWeight + Voxel.AlbedoRgb.R * Voxel.Metalness * 0.25f,
        Voxel.AlbedoRgb.G * DiffWeight + Voxel.AlbedoRgb.G * Voxel.Metalness * 0.25f,
        Voxel.AlbedoRgb.B * DiffWeight + Voxel.AlbedoRgb.B * Voxel.Metalness * 0.25f,
    };

    if (Mask.EnableSkyFill)
    {
        const float SkyHem = 0.5f + 0.5f * N.Z;
        TotalRad.R += DiffTint.R * SkyRadiance.R * (0.14f + 0.16f * SkyHem);
        TotalRad.G += DiffTint.G * SkyRadiance.G * (0.14f + 0.16f * SkyHem);
        TotalRad.B += DiffTint.B * SkyRadiance.B * (0.14f + 0.16f * SkyHem);
    }

    // 1. Directional Sun
    if (Mask.EnableSun)
    {
        const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
        const float NdotL = std::max(0.0f, Mathematics::EvaluateDot(N, SunDir));
        if (NdotL > 0.01f)
        {
            const auto Sh = Volume.TraceClipmapSdfPenumbra(
                WorldPos, N, SunDir, 18.0f,
                std::max(0.025f, std::tan(Sun.SunAngularRadiusRadians)), true, DynamicObjects, false);
            const float Gain = (NdotL * Sh.Visibility) * (1.0f / Constants::PiFloat);
            TotalRad.R += DiffTint.R * Sun.SolarIrradiance.R * Gain;
            TotalRad.G += DiffTint.G * Sun.SolarIrradiance.G * Gain;
            TotalRad.B += DiffTint.B * Sun.SolarIrradiance.B * Gain;
        }
    }

    // 2. Point Lights (with Clipmap SDF soft penumbra!)
    if (Mask.EnablePoint)
    {
        for (const auto& Pt : PointLights)
        {
            const Mathematics::Axis3D Delta = Pt.PositionWorld - WorldPos;
            const float Dist2 = Mathematics::EvaluateLengthSquared(Delta);
            const float Dist  = std::sqrt(std::max(1.0e-5f, Dist2));
            if (Dist >= Pt.RangeMeters) continue;
            const Mathematics::Axis3D LDir = Delta * (1.0f / Dist);
            const float NdotL = std::max(0.0f, Mathematics::EvaluateDot(N, LDir));
            if (NdotL <= 0.01f) continue;

            const auto Sh = Volume.TraceClipmapSdfPenumbra(
                WorldPos, N, LDir, std::max(0.15f, Dist - Pt.RadiusMeters),
                std::max(0.05f, Pt.RadiusMeters), false, DynamicObjects, false);
            const float RangeRatio = std::clamp(Dist / std::max(0.5f, Pt.RangeMeters), 0.0f, 1.0f);
            const float SmoothWin  = std::max(0.0f, 1.0f - RangeRatio * RangeRatio);
            const float Atten = (Pt.Intensity * SmoothWin * SmoothWin) / std::max(0.18f, Dist2 + Pt.RadiusMeters * Pt.RadiusMeters);
            const float Gain  = NdotL * Sh.Visibility * Atten * (1.0f / Constants::PiFloat);
            TotalRad.R += DiffTint.R * Pt.Color.R * Gain;
            TotalRad.G += DiffTint.G * Pt.Color.G * Gain;
            TotalRad.B += DiffTint.B * Pt.Color.B * Gain;
        }
    }

    // 3. Area Quad Lights (with Clipmap SDF occlusion!)
    if (Mask.EnableArea)
    {
        for (const auto& Q : AreaLights)
        {
            const float MaxEmit = std::max({ Q.EmittedRadiance.R, Q.EmittedRadiance.G, Q.EmittedRadiance.B });
            if (MaxEmit <= 1.0e-4f) continue;

            const Mathematics::Axis3D Delta = Q.CenterWorld - WorldPos;
            const float Dist2 = std::max(0.12f, Mathematics::EvaluateLengthSquared(Delta));
            const float Dist  = std::sqrt(Dist2);
            const Mathematics::Axis3D LDir = Delta * (1.0f / Dist);
            const float NdotL = std::max(0.0f, Mathematics::EvaluateDot(N, LDir));
            if (NdotL <= 0.01f) continue;

            const Mathematics::Axis3D LNorm = Mathematics::NormalizeAxis(
                Mathematics::EvaluateCross(Q.RightTangent, Q.UpBitangent), { 0.0f, 0.0f, -1.0f });
            const float LCos = Q.TwoSidedEmission
                ? std::fabs(Mathematics::EvaluateDot(LNorm, LDir * -1.0f))
                : std::max(0.0f, Mathematics::EvaluateDot(LNorm, LDir * -1.0f));
            if (LCos <= 0.01f) continue;

            const auto Sh = Volume.TraceClipmapSdfPenumbra(
                WorldPos, N, LDir, std::max(0.15f, Dist - 0.12f),
                0.35f * (Q.HalfWidthMeters + Q.HalfHeightMeters), false, DynamicObjects, false);
            const float Area = 4.0f * Q.HalfWidthMeters * Q.HalfHeightMeters;
            const float Form = std::min(1.8f, (NdotL * LCos * Area) / (Constants::PiFloat * Dist2));
            const float Gain = Form * Sh.Visibility;
            TotalRad.R += DiffTint.R * Q.EmittedRadiance.R * Gain;
            TotalRad.G += DiffTint.G * Q.EmittedRadiance.G * Gain;
            TotalRad.B += DiffTint.B * Q.EmittedRadiance.B * Gain;
        }
    }

    return {
        std::clamp(TotalRad.R, 0.0f, 28.0f),
        std::clamp(TotalRad.G, 0.0f, 28.0f),
        std::clamp(TotalRad.B, 0.0f, 28.0f),
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              VOXEL CLIPMAP CASCADE METHODS
//------------------------------------------------------------------------------------------------------------------------

bool VoxelClipmapCascade::ContainsInsideMargin(Mathematics::Axis3D WorldPos, float MarginFrac) const noexcept
{
    if (DimX < 2u || DimY < 2u || DimZ < 2u) return false;
    const float Ux = (WorldPos.X - BoundsMin.X) * InvSpan.X;
    const float Uy = (WorldPos.Y - BoundsMin.Y) * InvSpan.Y;
    const float Uz = (WorldPos.Z - BoundsMin.Z) * InvSpan.Z;
    const float Hi = 1.0f - MarginFrac;
    return Ux > MarginFrac && Ux < Hi &&
           Uy > MarginFrac && Uy < Hi &&
           Uz > MarginFrac && Uz < Hi;
}

float VoxelClipmapCascade::SampleDistanceTrilinear(Mathematics::Axis3D WorldPos) const noexcept
{
    if (DimX < 2u || DimY < 2u || DimZ < 2u || Voxels.empty()) return 1.0e5f;
    const float FX = std::clamp((WorldPos.X - BoundsMin.X) * InvSpan.X * static_cast<float>(DimX - 1u), 0.0f, static_cast<float>(DimX - 1u));
    const float FY = std::clamp((WorldPos.Y - BoundsMin.Y) * InvSpan.Y * static_cast<float>(DimY - 1u), 0.0f, static_cast<float>(DimY - 1u));
    const float FZ = std::clamp((WorldPos.Z - BoundsMin.Z) * InvSpan.Z * static_cast<float>(DimZ - 1u), 0.0f, static_cast<float>(DimZ - 1u));

    const uint32_t X0 = static_cast<uint32_t>(std::floor(FX));
    const uint32_t Y0 = static_cast<uint32_t>(std::floor(FY));
    const uint32_t Z0 = static_cast<uint32_t>(std::floor(FZ));
    const uint32_t X1 = std::min(DimX - 1u, X0 + 1u);
    const uint32_t Y1 = std::min(DimY - 1u, Y0 + 1u);
    const uint32_t Z1 = std::min(DimZ - 1u, Z0 + 1u);
    const float TX = FX - static_cast<float>(X0);
    const float TY = FY - static_cast<float>(Y0);
    const float TZ = FZ - static_cast<float>(Z0);

    const uint32_t SXY = DimX * DimY;
    const auto At = [&](uint32_t X, uint32_t Y, uint32_t Z) noexcept -> float
    {
        return Voxels[static_cast<std::size_t>(Z) * SXY + static_cast<std::size_t>(Y) * DimX + X].SignedDistanceM;
    };

    const float C00 = At(X0, Y0, Z0) * (1.0f - TX) + At(X1, Y0, Z0) * TX;
    const float C10 = At(X0, Y1, Z0) * (1.0f - TX) + At(X1, Y1, Z0) * TX;
    const float C01 = At(X0, Y0, Z1) * (1.0f - TX) + At(X1, Y0, Z1) * TX;
    const float C11 = At(X0, Y1, Z1) * (1.0f - TX) + At(X1, Y1, Z1) * TX;
    return (C00 * (1.0f - TY) + C10 * TY) * (1.0f - TZ) + (C01 * (1.0f - TY) + C11 * TY) * TZ;
}

Mathematics::LinearRGB VoxelClipmapCascade::SampleRadiosityTrilinear(
    Mathematics::Axis3D WorldPos,
    float               ConeRadiusM) const noexcept
{
    if (DimX < 2u || DimY < 2u || DimZ < 2u || Voxels.empty()) return {};

    const auto SampleMip0 = [&]() noexcept -> Mathematics::LinearRGB
    {
        const float FX = std::clamp((WorldPos.X - BoundsMin.X) * InvSpan.X * static_cast<float>(DimX - 1u), 0.0f, static_cast<float>(DimX - 1u));
        const float FY = std::clamp((WorldPos.Y - BoundsMin.Y) * InvSpan.Y * static_cast<float>(DimY - 1u), 0.0f, static_cast<float>(DimY - 1u));
        const float FZ = std::clamp((WorldPos.Z - BoundsMin.Z) * InvSpan.Z * static_cast<float>(DimZ - 1u), 0.0f, static_cast<float>(DimZ - 1u));
        const uint32_t X0 = static_cast<uint32_t>(std::floor(FX));
        const uint32_t Y0 = static_cast<uint32_t>(std::floor(FY));
        const uint32_t Z0 = static_cast<uint32_t>(std::floor(FZ));
        const uint32_t X1 = std::min(DimX - 1u, X0 + 1u);
        const uint32_t Y1 = static_cast<uint32_t>(std::min(DimY - 1u, Y0 + 1u));
        const uint32_t Z1 = std::min(DimZ - 1u, Z0 + 1u);
        const float TX = FX - static_cast<float>(X0);
        const float TY = FY - static_cast<float>(Y0);
        const float TZ = FZ - static_cast<float>(Z0);
        const uint32_t SXY = DimX * DimY;
        const auto At = [&](uint32_t X, uint32_t Y, uint32_t Z) noexcept -> Mathematics::LinearRGB
        {
            return Voxels[static_cast<std::size_t>(Z) * SXY + static_cast<std::size_t>(Y) * DimX + X].BounceRadiance;
        };
        const auto C00 = At(X0, Y0, Z0) * (1.0f - TX) + At(X1, Y0, Z0) * TX;
        const auto C10 = At(X0, Y1, Z0) * (1.0f - TX) + At(X1, Y1, Z0) * TX;
        const auto C01 = At(X0, Y0, Z1) * (1.0f - TX) + At(X1, Y0, Z1) * TX;
        const auto C11 = At(X0, Y1, Z1) * (1.0f - TX) + At(X1, Y1, Z1) * TX;
        return (C00 * (1.0f - TY) + C10 * TY) * (1.0f - TZ) + (C01 * (1.0f - TY) + C11 * TY) * TZ;
    };

    const auto SampleGrid3 = [&](
        const std::vector<Mathematics::LinearRGB>& Grid,
        uint32_t                                   GX,
        uint32_t                                   GY,
        uint32_t                                   GZ) noexcept -> Mathematics::LinearRGB
    {
        const float FX = std::clamp((WorldPos.X - BoundsMin.X) * InvSpan.X * static_cast<float>(GX - 1u), 0.0f, static_cast<float>(GX - 1u));
        const float FY = std::clamp((WorldPos.Y - BoundsMin.Y) * InvSpan.Y * static_cast<float>(GY - 1u), 0.0f, static_cast<float>(GY - 1u));
        const float FZ = std::clamp((WorldPos.Z - BoundsMin.Z) * InvSpan.Z * static_cast<float>(GZ - 1u), 0.0f, static_cast<float>(GZ - 1u));
        const uint32_t X0 = static_cast<uint32_t>(std::floor(FX));
        const uint32_t Y0 = static_cast<uint32_t>(std::floor(FY));
        const uint32_t Z0 = static_cast<uint32_t>(std::floor(FZ));
        const uint32_t X1 = std::min(GX - 1u, X0 + 1u);
        const uint32_t Y1 = std::min(GY - 1u, Y0 + 1u);
        const uint32_t Z1 = std::min(GZ - 1u, Z0 + 1u);
        const float TX = FX - static_cast<float>(X0);
        const float TY = FY - static_cast<float>(Y0);
        const float TZ = FZ - static_cast<float>(Z0);
        const uint32_t SXY = GX * GY;
        const auto At = [&](uint32_t X, uint32_t Y, uint32_t Z) noexcept -> Mathematics::LinearRGB
        {
            return Grid[static_cast<std::size_t>(Z) * SXY + static_cast<std::size_t>(Y) * GX + X];
        };
        const auto C00 = At(X0, Y0, Z0) * (1.0f - TX) + At(X1, Y0, Z0) * TX;
        const auto C10 = At(X0, Y1, Z0) * (1.0f - TX) + At(X1, Y1, Z0) * TX;
        const auto C01 = At(X0, Y0, Z1) * (1.0f - TX) + At(X1, Y0, Z1) * TX;
        const auto C11 = At(X0, Y1, Z1) * (1.0f - TX) + At(X1, Y1, Z1) * TX;
        return (C00 * (1.0f - TY) + C10 * TY) * (1.0f - TZ) + (C01 * (1.0f - TY) + C11 * TY) * TZ;
    };

    const auto Rad0 = SampleMip0();
    if (ConeRadiusM <= VoxelSpacingM * 1.1f || Mip1DimX < 2u || Mip1Radiance.empty())
    {
        return Rad0;
    }

    const auto Rad1 = SampleGrid3(Mip1Radiance, Mip1DimX, Mip1DimY, Mip1DimZ);
    const float Mip1Thresh = VoxelSpacingM * 2.2f;
    if (ConeRadiusM <= Mip1Thresh || Mip2DimX < 2u || Mip2Radiance.empty())
    {
        const float T01 = std::clamp((ConeRadiusM - VoxelSpacingM * 1.1f) / std::max(0.03f, VoxelSpacingM * 1.1f), 0.0f, 1.0f);
        return Rad0 * (1.0f - T01) + Rad1 * T01;
    }

    const auto Rad2 = SampleGrid3(Mip2Radiance, Mip2DimX, Mip2DimY, Mip2DimZ);
    const float T12 = std::clamp((ConeRadiusM - Mip1Thresh) / std::max(0.05f, VoxelSpacingM * 2.6f), 0.0f, 1.0f);
    return Rad1 * (1.0f - T12) + Rad2 * T12;
}

Mathematics::Axis3D VoxelClipmapCascade::EvaluateNormal(Mathematics::Axis3D WorldPos) const noexcept
{
    const float H = std::max(0.02f, VoxelSpacingM * 0.90f);
    const float D0 = SampleDistanceTrilinear({ WorldPos.X + H, WorldPos.Y - H, WorldPos.Z - H });
    const float D1 = SampleDistanceTrilinear({ WorldPos.X - H, WorldPos.Y - H, WorldPos.Z + H });
    const float D2 = SampleDistanceTrilinear({ WorldPos.X - H, WorldPos.Y + H, WorldPos.Z - H });
    const float D3 = SampleDistanceTrilinear({ WorldPos.X + H, WorldPos.Y + H, WorldPos.Z + H });
    const Mathematics::Axis3D G {
         D0 - D1 - D2 + D3,
        -D0 - D1 + D2 + D3,
        -D0 + D1 - D2 + D3,
    };
    return Mathematics::NormalizeAxis(G, { 0.0f, 0.0f, 1.0f });
}

void VoxelClipmapCascade::RebuildMip1() noexcept
{
    if (DimX < 2u || DimY < 2u || DimZ < 2u) return;
    Mip1DimX = std::max(2u, (DimX + 1u) / 2u);
    Mip1DimY = std::max(2u, (DimY + 1u) / 2u);
    Mip1DimZ = std::max(2u, (DimZ + 1u) / 2u);
    Mip1Radiance.assign(static_cast<std::size_t>(Mip1DimX) * Mip1DimY * Mip1DimZ, {});

    const uint32_t SXY0 = DimX * DimY;
    const uint32_t SXY1 = Mip1DimX * Mip1DimY;
    for (uint32_t Z = 0u; Z < Mip1DimZ; ++Z)
    {
        for (uint32_t Y = 0u; Y < Mip1DimY; ++Y)
        {
            for (uint32_t X = 0u; X < Mip1DimX; ++X)
            {
                Mathematics::LinearRGB Sum {};
                float Active = 0.0f;
                for (uint32_t DZ = 0u; DZ < 2u; ++DZ)
                {
                    const uint32_t SZ = std::min(DimZ - 1u, Z * 2u + DZ);
                    for (uint32_t DY = 0u; DY < 2u; ++DY)
                    {
                        const uint32_t SY = std::min(DimY - 1u, Y * 2u + DY);
                        for (uint32_t DX = 0u; DX < 2u; ++DX)
                        {
                            const uint32_t SX = std::min(DimX - 1u, X * 2u + DX);
                            const auto& V = Voxels[static_cast<std::size_t>(SZ) * SXY0 + static_cast<std::size_t>(SY) * DimX + SX];
                            if (V.HasSurface)
                            {
                                Sum = Sum + V.BounceRadiance;
                                Active += 1.0f;
                            }
                        }
                    }
                }
                if (Active > 0.0f)
                {
                    // Energy-conserving dilution prevents sparse emissive voxels from spiking across wide blocks
                    Mip1Radiance[static_cast<std::size_t>(Z) * SXY1 + static_cast<std::size_t>(Y) * Mip1DimX + X] =
                        Sum * (1.0f / std::max(1.0f, Active * 0.62f + 8.0f * 0.38f));
                }
            }
        }
    }

    Mip2DimX = std::max(2u, (Mip1DimX + 1u) / 2u);
    Mip2DimY = std::max(2u, (Mip1DimY + 1u) / 2u);
    Mip2DimZ = std::max(2u, (Mip1DimZ + 1u) / 2u);
    Mip2Radiance.assign(static_cast<std::size_t>(Mip2DimX) * Mip2DimY * Mip2DimZ, {});
    const uint32_t SXY2 = Mip2DimX * Mip2DimY;
    for (uint32_t Z = 0u; Z < Mip2DimZ; ++Z)
    {
        for (uint32_t Y = 0u; Y < Mip2DimY; ++Y)
        {
            for (uint32_t X = 0u; X < Mip2DimX; ++X)
            {
                Mathematics::LinearRGB Sum {};
                float Active = 0.0f;
                for (uint32_t DZ = 0u; DZ < 2u; ++DZ)
                {
                    const uint32_t SZ = std::min(Mip1DimZ - 1u, Z * 2u + DZ);
                    for (uint32_t DY = 0u; DY < 2u; ++DY)
                    {
                        const uint32_t SY = std::min(Mip1DimY - 1u, Y * 2u + DY);
                        for (uint32_t DX = 0u; DX < 2u; ++DX)
                        {
                            const uint32_t SX = std::min(Mip1DimX - 1u, X * 2u + DX);
                            const auto& R = Mip1Radiance[static_cast<std::size_t>(SZ) * SXY1 + static_cast<std::size_t>(SY) * Mip1DimX + SX];
                            if ((R.R + R.G + R.B) > 1.0e-4f)
                            {
                                Sum = Sum + R;
                                Active += 1.0f;
                            }
                        }
                    }
                }
                if (Active > 0.0f)
                {
                    Mip2Radiance[static_cast<std::size_t>(Z) * SXY2 + static_cast<std::size_t>(Y) * Mip2DimX + X] =
                        Sum * (1.0f / std::max(1.0f, Active * 0.65f + 8.0f * 0.35f));
                }
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                             LOCAL OBJECT SDF ASSET & TRANSFORM
//------------------------------------------------------------------------------------------------------------------------

bool LocalObjectSdfAsset::BakeFromLocalTree(
    std::string_view                              Name,
    const SpatialAcceleration::WideBoundingTree&  LocalTree,
    const MaterialSpace::CompactMaterialRegister& Materials,
    float                                         TargetSpacingM,
    float                                         PaddingM)
{
    (void)TargetSpacingM;
    (void)PaddingM;
    DebugName = std::string(Name);
    if (LocalTree.Primitives.empty()) return false;

    // Build exact arena/f2d37e4b-frontier FacetAtlas + 80x40x32 BakedFacetDistance
    std::vector<Facet> Facets;
    Facets.reserve(LocalTree.Primitives.size());
    for (const auto& Prim : LocalTree.Primitives)
    {
        const ClipVector A { Prim.Corner0.X, Prim.Corner0.Y, Prim.Corner0.Z };
        const ClipVector B { Prim.Corner0.X + Prim.Edge01.X, Prim.Corner0.Y + Prim.Edge01.Y, Prim.Corner0.Z + Prim.Edge01.Z };
        const ClipVector C { Prim.Corner0.X + Prim.Edge02.X, Prim.Corner0.Y + Prim.Edge02.Y, Prim.Corner0.Z + Prim.Edge02.Z };
        const ClipVector N = Normalize(Cross(B - A, C - A));
        Facets.push_back(Facet{ {{ A, B, C }}, {{ N, N, N }}, static_cast<unsigned>(Prim.MaterialOrdinal) });
    }
    LocalAtlas.Construct(std::move(Facets));
    BakedDistance.Extent = { 80u, 40u, 32u };
    BakedDistance.Bake(LocalAtlas);

    LocalMin = { BakedDistance.Low.X,  BakedDistance.Low.Y,  BakedDistance.Low.Z  };
    LocalMax = { BakedDistance.High.X, BakedDistance.High.Y, BakedDistance.High.Z };
    DimX     = BakedDistance.Extent[0];
    DimY     = BakedDistance.Extent[1];
    DimZ     = BakedDistance.Extent[2];
    VoxelSpacingM = std::max({ BakedDistance.Spacing.X, BakedDistance.Spacing.Y, BakedDistance.Spacing.Z });
    const Mathematics::Axis3D Span = LocalMax - LocalMin;
    InvSpan = {
        1.0f / std::max(1.0e-5f, Span.X),
        1.0f / std::max(1.0e-5f, Span.Y),
        1.0f / std::max(1.0e-5f, Span.Z),
    };

    const uint32_t MatCount = std::max(1u, Materials.QuerySlotCount());
    const float Band   = VoxelSpacingM * 1.95f;
    const uint32_t SXY = DimX * DimY;
    LocalVoxels.assign(static_cast<std::size_t>(DimX) * DimY * DimZ, {});

    ParallelForZ(DimZ, [&](uint32_t ZStart, uint32_t ZEnd)
    {
        for (uint32_t Z = ZStart; Z < ZEnd; ++Z)
        {
            const float PZ = LocalMin.Z + static_cast<float>(Z) * BakedDistance.Spacing.Z;
            for (uint32_t Y = 0u; Y < DimY; ++Y)
            {
                const float PY = LocalMin.Y + static_cast<float>(Y) * BakedDistance.Spacing.Y;
                for (uint32_t X = 0u; X < DimX; ++X)
                {
                    const float PX = LocalMin.X + static_cast<float>(X) * BakedDistance.Spacing.X;
                    const std::size_t Idx = static_cast<std::size_t>(Z) * SXY + static_cast<std::size_t>(Y) * DimX + X;
                    const float D = BakedDistance.Samples[Idx];
                    VoxelSurfaceCacheEntry Entry {};
                    Entry.SignedDistanceM = D;
                    if (D <= Band)
                    {
                        const ClosestTriHit Hit = QueryClosestTriangleBvh(LocalTree, { PX, PY, PZ }, Band * 1.5f);
                        if (Hit.Valid)
                        {
                            const uint32_t Slot = std::min(static_cast<uint32_t>(Hit.MaterialSlot), MatCount - 1u);
                            const auto& Mat = Materials.InspectAuthoredMaterial(Slot);
                            Entry.MaterialSlot  = static_cast<uint16_t>(Slot);
                            Entry.AlbedoRgb     = Mat.Diffuse.BaseColor * Mat.Diffuse.BaseWeight;
                            Entry.EmissiveRgb   = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
                            Entry.SurfaceNormal = Hit.Normal;
                            Entry.Metalness     = Mat.Specular.Metalness;
                            Entry.Roughness     = Mat.Specular.Roughness;
                            Entry.HasSurface    = 1u;
                        }
                    }
                    LocalVoxels[Idx] = Entry;
                }
            }
        }
    });

    return true;
}

DistanceInterval LocalObjectSdfAsset::SampleConservativeLocalInterval(Mathematics::Axis3D LocalPos) const noexcept
{
    return BakedDistance.Sample({ LocalPos.X, LocalPos.Y, LocalPos.Z });
}

float LocalObjectSdfAsset::SampleConservativeLocalDistance(Mathematics::Axis3D LocalPos) const noexcept
{
    return BakedDistance.Sample({ LocalPos.X, LocalPos.Y, LocalPos.Z }).Raw;
}

bool LocalObjectSdfAsset::SampleLocalSurfaceCache(
    Mathematics::Axis3D     LocalPos,
    VoxelSurfaceCacheEntry& OutEntry) const noexcept
{
    if (DimX < 2u || DimY < 2u || DimZ < 2u || LocalVoxels.empty()) return false;
    if (LocalPos.X < LocalMin.X || LocalPos.X > LocalMax.X ||
        LocalPos.Y < LocalMin.Y || LocalPos.Y > LocalMax.Y ||
        LocalPos.Z < LocalMin.Z || LocalPos.Z > LocalMax.Z)
    {
        return false;
    }
    const float FX = std::clamp((LocalPos.X - LocalMin.X) * InvSpan.X * static_cast<float>(DimX - 1u), 0.0f, static_cast<float>(DimX - 1u));
    const float FY = std::clamp((LocalPos.Y - LocalMin.Y) * InvSpan.Y * static_cast<float>(DimY - 1u), 0.0f, static_cast<float>(DimY - 1u));
    const float FZ = std::clamp((LocalPos.Z - LocalMin.Z) * InvSpan.Z * static_cast<float>(DimZ - 1u), 0.0f, static_cast<float>(DimZ - 1u));
    const uint32_t IX = static_cast<uint32_t>(std::round(FX));
    const uint32_t IY = static_cast<uint32_t>(std::round(FY));
    const uint32_t IZ = static_cast<uint32_t>(std::round(FZ));
    OutEntry = LocalVoxels[static_cast<std::size_t>(IZ) * (DimX * DimY) + static_cast<std::size_t>(IY) * DimX + IX];
    return OutEntry.HasSurface != 0u;
}

Mathematics::Axis3D LocalObjectSdfAsset::EvaluateLocalNormal(Mathematics::Axis3D LocalPos) const noexcept
{
    const float H = std::max(0.015f, VoxelSpacingM * 0.90f);
    const float D0 = SampleConservativeLocalDistance({ LocalPos.X + H, LocalPos.Y - H, LocalPos.Z - H });
    const float D1 = SampleConservativeLocalDistance({ LocalPos.X - H, LocalPos.Y - H, LocalPos.Z + H });
    const float D2 = SampleConservativeLocalDistance({ LocalPos.X - H, LocalPos.Y + H, LocalPos.Z - H });
    const float D3 = SampleConservativeLocalDistance({ LocalPos.X + H, LocalPos.Y + H, LocalPos.Z + H });
    return Mathematics::NormalizeAxis({
         D0 - D1 - D2 + D3,
        -D0 - D1 + D2 + D3,
        -D0 + D1 - D2 + D3,
    }, { 0.0f, 0.0f, 1.0f });
}

RigidTransform3D RigidTransform3D::FromTranslationYawPitchRoll(
    Mathematics::Axis3D Translation,
    float               YawRad,
    float               PitchRad,
    float               RollRad,
    float               Scale) noexcept
{
    const float CY = std::cos(YawRad),   SY = std::sin(YawRad);
    const float CP = std::cos(PitchRad), SP = std::sin(PitchRad);
    const float CR = std::cos(RollRad),  SR = std::sin(RollRad);

    // R = Rz(Yaw) * Ry(Pitch) * Rx(Roll)
    const float R00 = CY * CP;
    const float R01 = CY * SP * SR - SY * CR;
    const float R02 = CY * SP * CR + SY * SR;
    const float R10 = SY * CP;
    const float R11 = SY * SP * SR + CY * CR;
    const float R12 = SY * SP * CR - CY * SR;
    const float R20 = -SP;
    const float R21 = CP * SR;
    const float R22 = CP * CR;

    RigidTransform3D Out {};
    Out.LocalToWorld = {
        R00 * Scale, R01 * Scale, R02 * Scale, Translation.X,
        R10 * Scale, R11 * Scale, R12 * Scale, Translation.Y,
        R20 * Scale, R21 * Scale, R22 * Scale, Translation.Z,
        0.0f,        0.0f,        0.0f,        1.0f,
    };

    const float InvS = 1.0f / std::max(1.0e-5f, Scale);
    const float W00 = R00 * InvS, W01 = R10 * InvS, W02 = R20 * InvS;
    const float W10 = R01 * InvS, W11 = R11 * InvS, W12 = R21 * InvS;
    const float W20 = R02 * InvS, W21 = R12 * InvS, W22 = R22 * InvS;
    const float TX = -(W00 * Translation.X + W01 * Translation.Y + W02 * Translation.Z);
    const float TY = -(W10 * Translation.X + W11 * Translation.Y + W12 * Translation.Z);
    const float TZ = -(W20 * Translation.X + W21 * Translation.Y + W22 * Translation.Z);
    Out.WorldToLocal = {
        W00,  W01,  W02,  TX,
        W10,  W11,  W12,  TY,
        W20,  W21,  W22,  TZ,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    return Out;
}

Mathematics::Axis3D RigidTransform3D::TransformPoint(const std::array<float, 16u>& M, Mathematics::Axis3D P) const noexcept
{
    return {
        M[0] * P.X + M[1] * P.Y + M[2]  * P.Z + M[3],
        M[4] * P.X + M[5] * P.Y + M[6]  * P.Z + M[7],
        M[8] * P.X + M[9] * P.Y + M[10] * P.Z + M[11],
    };
}

Mathematics::Axis3D RigidTransform3D::TransformDirection(const std::array<float, 16u>& M, Mathematics::Axis3D D) const noexcept
{
    return Mathematics::NormalizeAxis({
        M[0] * D.X + M[1] * D.Y + M[2]  * D.Z,
        M[4] * D.X + M[5] * D.Y + M[6]  * D.Z,
        M[8] * D.X + M[9] * D.Y + M[10] * D.Z,
    }, { 0.0f, 0.0f, 1.0f });
}

void DynamicLocalSdfInstance::UpdateWorldBounds() noexcept
{
    if (Asset == nullptr) return;
    Mathematics::Axis3D Mn {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D Mx { -1.0e9f, -1.0e9f, -1.0e9f };
    for (int CZ = 0; CZ < 2; ++CZ)
    {
        for (int CY = 0; CY < 2; ++CY)
        {
            for (int CX = 0; CX < 2; ++CX)
            {
                const Mathematics::Axis3D Corner {
                    CX ? Asset->LocalMax.X : Asset->LocalMin.X,
                    CY ? Asset->LocalMax.Y : Asset->LocalMin.Y,
                    CZ ? Asset->LocalMax.Z : Asset->LocalMin.Z,
                };
                const auto W = Transform.TransformPoint(Transform.LocalToWorld, Corner);
                Mn.X = std::min(Mn.X, W.X); Mn.Y = std::min(Mn.Y, W.Y); Mn.Z = std::min(Mn.Z, W.Z);
                Mx.X = std::max(Mx.X, W.X); Mx.Y = std::max(Mx.Y, W.Y); Mx.Z = std::max(Mx.Z, W.Z);
            }
        }
    }
    WorldAabbMin = Mn;
    WorldAabbMax = Mx;
}

//------------------------------------------------------------------------------------------------------------------------
//                                     3-TIER TOROIDAL CLIPMAP + SURFACE CACHE
//------------------------------------------------------------------------------------------------------------------------

void VoxelSdfClipmapVolume::SetClosedStaticBoxes(std::span<const ClosedBox> Boxes) noexcept
{
    ClosedBoxes.assign(Boxes.begin(), Boxes.end());
    ToroidalClipmap.ClosedSurfaces = ClosedBoxes;
}

void VoxelSdfClipmapVolume::BuildStaticClipmap(
    const SpatialAcceleration::WideBoundingTree&  StaticTree,
    const MaterialSpace::CompactMaterialRegister& Materials,
    Mathematics::Axis3D                           Clip0Min,
    Mathematics::Axis3D                           Clip0Max,
    uint32_t                                      Clip0DimX,
    uint32_t                                      Clip0DimY,
    uint32_t                                      Clip0DimZ,
    Mathematics::Axis3D                           Clip1Min,
    Mathematics::Axis3D                           Clip1Max,
    uint32_t                                      Clip1DimX,
    uint32_t                                      Clip1DimY,
    uint32_t                                      Clip1DimZ)
{
    BoundMaterials = &Materials;
    if (!StaticTree.Primitives.empty())
    {
        std::vector<Facet> StaticFacets;
        StaticFacets.reserve(StaticTree.Primitives.size());
        for (const auto& Prim : StaticTree.Primitives)
        {
            const ClipVector A { Prim.Corner0.X, Prim.Corner0.Y, Prim.Corner0.Z };
            const ClipVector B { Prim.Corner0.X + Prim.Edge01.X, Prim.Corner0.Y + Prim.Edge01.Y, Prim.Corner0.Z + Prim.Edge01.Z };
            const ClipVector C { Prim.Corner0.X + Prim.Edge02.X, Prim.Corner0.Y + Prim.Edge02.Y, Prim.Corner0.Z + Prim.Edge02.Z };
            const ClipVector N = Normalize(Cross(B - A, C - A));
            StaticFacets.push_back(Facet{ {{ A, B, C }}, {{ N, N, N }}, static_cast<unsigned>(Prim.MaterialOrdinal) });
        }
        StaticAtlas.Construct(std::move(StaticFacets));
    }
    ToroidalClipmap.ClosedSurfaces      = ClosedBoxes;
    ToroidalClipmap.StaticTriangleAtlas = StaticAtlas.Surfaces.empty() ? nullptr : &StaticAtlas;
    const Mathematics::Axis3D FocusCenter = (Clip0Min + Clip0Max) * 0.5f;
    ToroidalClipmap.Relocate({}, { FocusCenter.X, FocusCenter.Y, FocusCenter.Z }, true);

    const uint32_t MatCount = std::max(1u, Materials.QuerySlotCount());

    auto BakeCascade = [&](
        VoxelClipmapCascade& Dst,
        Mathematics::Axis3D  BMin,
        Mathematics::Axis3D  BMax,
        uint32_t             DX,
        uint32_t             DY,
        uint32_t             DZ)
    {
        Dst.BoundsMin = BMin;
        Dst.BoundsMax = BMax;
        Dst.DimX      = std::max(4u, DX);
        Dst.DimY      = std::max(4u, DY);
        Dst.DimZ      = std::max(4u, DZ);
        const Mathematics::Axis3D Span = BMax - BMin;
        Dst.InvSpan = {
            1.0f / std::max(1.0e-5f, Span.X),
            1.0f / std::max(1.0e-5f, Span.Y),
            1.0f / std::max(1.0e-5f, Span.Z),
        };
        Dst.VoxelSpacingM = std::max({
            Span.X / static_cast<float>(Dst.DimX - 1u),
            Span.Y / static_cast<float>(Dst.DimY - 1u),
            Span.Z / static_cast<float>(Dst.DimZ - 1u),
        });
        Dst.Voxels.assign(static_cast<std::size_t>(Dst.DimX) * Dst.DimY * Dst.DimZ, {});

        const float Cutoff = std::max(1.4f, Dst.VoxelSpacingM * 8.0f);
        const float Band   = Dst.VoxelSpacingM * 1.95f;
        const uint32_t SXY = Dst.DimX * Dst.DimY;

        ParallelForZ(Dst.DimZ, [&](uint32_t ZStart, uint32_t ZEnd)
        {
            for (uint32_t Z = ZStart; Z < ZEnd; ++Z)
            {
                const float PZ = BMin.Z + (static_cast<float>(Z) / static_cast<float>(Dst.DimZ - 1u)) * Span.Z;
                for (uint32_t Y = 0u; Y < Dst.DimY; ++Y)
                {
                    const float PY = BMin.Y + (static_cast<float>(Y) / static_cast<float>(Dst.DimY - 1u)) * Span.Y;
                    for (uint32_t X = 0u; X < Dst.DimX; ++X)
                    {
                        const float PX = BMin.X + (static_cast<float>(X) / static_cast<float>(Dst.DimX - 1u)) * Span.X;
                        ClosestTriHit Hit {};
                        const float D = ComputeSignedDistanceWithMajorityVote(StaticTree, { PX, PY, PZ }, Cutoff, Hit);
                        VoxelSurfaceCacheEntry Entry {};
                        Entry.SignedDistanceM = D;
                        if (Hit.Valid && std::fabs(D) <= Band)
                        {
                            const uint32_t Slot = std::min(static_cast<uint32_t>(Hit.MaterialSlot), MatCount - 1u);
                            const auto& Mat = Materials.InspectAuthoredMaterial(Slot);
                            Entry.MaterialSlot  = static_cast<uint16_t>(Slot);
                            Entry.AlbedoRgb     = Mat.Diffuse.BaseColor * Mat.Diffuse.BaseWeight;
                            Entry.EmissiveRgb   = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
                            Entry.SurfaceNormal = Hit.Normal;
                            Entry.Metalness     = Mat.Specular.Metalness;
                            Entry.Roughness     = Mat.Specular.Roughness;
                            Entry.HasSurface    = 1u;
                        }
                        Dst.Voxels[static_cast<std::size_t>(Z) * SXY + static_cast<std::size_t>(Y) * Dst.DimX + X] = Entry;
                    }
                }
            }
        });
    };

    BakeCascade(StaticClip0, Clip0Min, Clip0Max, Clip0DimX, Clip0DimY, Clip0DimZ);
    BakeCascade(StaticClip1, Clip1Min, Clip1Max, Clip1DimX, Clip1DimY, Clip1DimZ);

    // Clone StaticClip0 metadata & buffer into ActiveClip0 once
    ActiveClip0 = StaticClip0;
    HasDirtyPrev = false;
}

void VoxelSdfClipmapVolume::BakeStaticSurfaceCacheLighting(
    const ClipmapLightingMask&                            Mask,
    const GeometricRaster::SolarDirectionalEmitter&       Sun,
    std::span<const VoxelPointLightSpec>                  PointLights,
    std::span<const GeometricRaster::PolygonalAreaQuadLight> AreaLights,
    Mathematics::LinearRGB                                SkyRadiance,
    uint32_t                                              BouncePasses)
{
    auto InjectDirectCascade = [&](VoxelClipmapCascade& Cas)
    {
        const Mathematics::Axis3D Span = Cas.BoundsMax - Cas.BoundsMin;
        const uint32_t SXY = Cas.DimX * Cas.DimY;
        ParallelForZ(Cas.DimZ, [&](uint32_t ZStart, uint32_t ZEnd)
        {
            for (uint32_t Z = ZStart; Z < ZEnd; ++Z)
            {
                const float PZ = Cas.BoundsMin.Z + (static_cast<float>(Z) / static_cast<float>(Cas.DimZ - 1u)) * Span.Z;
                for (uint32_t Y = 0u; Y < Cas.DimY; ++Y)
                {
                    const float PY = Cas.BoundsMin.Y + (static_cast<float>(Y) / static_cast<float>(Cas.DimY - 1u)) * Span.Y;
                    for (uint32_t X = 0u; X < Cas.DimX; ++X)
                    {
                        const float PX = Cas.BoundsMin.X + (static_cast<float>(X) / static_cast<float>(Cas.DimX - 1u)) * Span.X;
                        auto& V = Cas.Voxels[static_cast<std::size_t>(Z) * SXY + static_cast<std::size_t>(Y) * Cas.DimX + X];
                        if (!V.HasSurface)
                        {
                            V.DirectRadiance = {};
                            V.BounceRadiance = {};
                            continue;
                        }
                        const auto DirRad = EvaluateDirectVoxelSurfaceLighting(
                            V, { PX, PY, PZ }, *this, Mask, Sun, PointLights, AreaLights, SkyRadiance, {});
                        V.DirectRadiance = DirRad;
                        V.BounceRadiance = DirRad;
                    }
                }
            }
        });
        Cas.RebuildMip1();
    };

    // Temporarily ensure ActiveClip0 matches StaticClip0 distance during static light bake
    ActiveClip0 = StaticClip0;
    InjectDirectCascade(StaticClip0);
    InjectDirectCascade(StaticClip1);
    ActiveClip0.Voxels = StaticClip0.Voxels;
    ActiveClip0.Mip1Radiance = StaticClip0.Mip1Radiance;

    // 2 Multi-bounce Radiosity Propagation passes across StaticClip0 + StaticClip1
    for (uint32_t Pass = 0u; Pass < BouncePasses; ++Pass)
    {
        auto PropagateCascade = [&](VoxelClipmapCascade& Cas, const VoxelClipmapCascade& PrevSame)
        {
            const Mathematics::Axis3D Span = Cas.BoundsMax - Cas.BoundsMin;
            const uint32_t SXY = Cas.DimX * Cas.DimY;
            std::vector<Mathematics::LinearRGB> NextBounce(Cas.Voxels.size());

            ParallelForZ(Cas.DimZ, [&](uint32_t ZStart, uint32_t ZEnd)
            {
                for (uint32_t Z = ZStart; Z < ZEnd; ++Z)
                {
                    const float PZ = Cas.BoundsMin.Z + (static_cast<float>(Z) / static_cast<float>(Cas.DimZ - 1u)) * Span.Z;
                    for (uint32_t Y = 0u; Y < Cas.DimY; ++Y)
                    {
                        const float PY = Cas.BoundsMin.Y + (static_cast<float>(Y) / static_cast<float>(Cas.DimY - 1u)) * Span.Y;
                        for (uint32_t X = 0u; X < Cas.DimX; ++X)
                        {
                            const std::size_t Idx = static_cast<std::size_t>(Z) * SXY + static_cast<std::size_t>(Y) * Cas.DimX + X;
                            const auto& V = Cas.Voxels[Idx];
                            if (!V.HasSurface)
                            {
                                NextBounce[Idx] = {};
                                continue;
                            }
                            const Mathematics::Axis3D WP {
                                Cas.BoundsMin.X + (static_cast<float>(X) / static_cast<float>(Cas.DimX - 1u)) * Span.X,
                                PY,
                                PZ,
                            };
                            const float DiffW = 1.0f - std::clamp(V.Metalness, 0.0f, 1.0f);
                            if (DiffW < 0.02f)
                            {
                                NextBounce[Idx] = V.DirectRadiance;
                                continue;
                            }

                            const Mathematics::Axis3D N = Mathematics::NormalizeAxis(V.SurfaceNormal, { 0.0f, 0.0f, 1.0f });
                            Mathematics::Axis3D T {}, B {};
                            BuildOrthonormalDuff(N, T, B);

                            // Deterministic 6-cone bounce gather
                            Mathematics::LinearRGB BounceIrr {};
                            for (uint32_t K = 0u; K < 6u; ++K)
                            {
                                const float R   = std::sqrt((static_cast<float>(K) + 0.5f) / 6.0f);
                                const float Ang = static_cast<float>(K) * 2.39996323f + 0.35f;
                                const Mathematics::Axis3D CDir = Mathematics::NormalizeAxis(
                                    T * (R * std::cos(Ang)) + B * (R * std::sin(Ang)) + N * std::sqrt(std::max(0.0f, 1.0f - R * R)));

                                float CT = Cas.VoxelSpacingM * 1.75f;
                                float CAlpha = 0.0f;
                                Mathematics::LinearRGB CRad {};
                                for (uint32_t St = 0u; St < 9u && CAlpha < 0.96f && CT < 12.0f; ++St)
                                {
                                    const Mathematics::Axis3D SP = WP + N * Cas.VoxelSpacingM + CDir * CT;
                                    int UsedCas = 2;
                                    float SD = 1.0e5f;
                                    float Vox = 0.15f;
                                    Mathematics::LinearRGB SRad {};
                                    if (PrevSame.ContainsInsideMargin(SP, 0.015f))
                                    {
                                        SD = PrevSame.SampleDistanceTrilinear(SP);
                                        Vox = PrevSame.VoxelSpacingM;
                                        SRad = PrevSame.SampleRadiosityTrilinear(SP, std::max(Vox * 0.85f, CT * 0.22f));
                                        UsedCas = 0;
                                    }
                                    else if (StaticClip1.ContainsInsideMargin(SP, 0.015f))
                                    {
                                        SD = StaticClip1.SampleDistanceTrilinear(SP);
                                        Vox = StaticClip1.VoxelSpacingM;
                                        SRad = StaticClip1.SampleRadiosityTrilinear(SP, std::max(Vox * 0.85f, CT * 0.22f));
                                        UsedCas = 1;
                                    }
                                    if (UsedCas == 2) break;

                                    const float ConeR = std::max(Vox * 0.80f, CT * 0.20f);
                                    if (SD < ConeR)
                                    {
                                        const float Occ = 1.0f - SmoothStep(-Vox * 0.2f, ConeR, SD);
                                        const float W   = (1.0f - CAlpha) * Occ;
                                        CRad   = CRad + SRad * W;
                                        CAlpha += W;
                                    }
                                    CT += std::max(SD * 0.88f, Vox * 0.92f);
                                }
                                BounceIrr = BounceIrr + CRad * (1.0f / 6.0f);
                            }

                            NextBounce[Idx] = {
                                std::min(32.0f, V.DirectRadiance.R + V.AlbedoRgb.R * DiffW * BounceIrr.R * 0.88f),
                                std::min(32.0f, V.DirectRadiance.G + V.AlbedoRgb.G * DiffW * BounceIrr.G * 0.88f),
                                std::min(32.0f, V.DirectRadiance.B + V.AlbedoRgb.B * DiffW * BounceIrr.B * 0.88f),
                            };
                        }
                    }
                }
            });

            for (std::size_t I = 0u; I < Cas.Voxels.size(); ++I)
            {
                Cas.Voxels[I].BounceRadiance = NextBounce[I];
            }
            Cas.RebuildMip1();
        };

        const VoxelClipmapCascade Prev0 = StaticClip0;
        const VoxelClipmapCascade Prev1 = StaticClip1;
        PropagateCascade(StaticClip0, Prev0);
        PropagateCascade(StaticClip1, Prev1);
    }

    ActiveClip0  = StaticClip0;
    HasDirtyPrev = false;
}

void VoxelSdfClipmapVolume::ComposeDynamicObjectsIntoActiveClip0(
    std::span<const DynamicLocalSdfInstance>              DynamicObjects,
    const ClipmapLightingMask&                            Mask,
    const GeometricRaster::SolarDirectionalEmitter&       Sun,
    std::span<const VoxelPointLightSpec>                  PointLights,
    std::span<const GeometricRaster::PolygonalAreaQuadLight> AreaLights,
    Mathematics::LinearRGB                                SkyRadiance,
    bool                                                  FullRefresh)
{
    if (ActiveClip0.DimX < 2u || StaticClip0.Voxels.size() != ActiveClip0.Voxels.size())
    {
        return;
    }

    if (FullRefresh || !HasDirtyPrev)
    {
        ActiveClip0.Voxels = StaticClip0.Voxels;
    }

    // Compute union AABB of all enabled dynamic objects
    Mathematics::Axis3D CurrMin {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D CurrMax { -1.0e9f, -1.0e9f, -1.0e9f };
    bool AnyDyn = false;
    for (const auto& Obj : DynamicObjects)
    {
        if (!Obj.Enabled || Obj.Asset == nullptr) continue;
        AnyDyn = true;
        CurrMin.X = std::min(CurrMin.X, Obj.WorldAabbMin.X);
        CurrMin.Y = std::min(CurrMin.Y, Obj.WorldAabbMin.Y);
        CurrMin.Z = std::min(CurrMin.Z, Obj.WorldAabbMin.Z);
        CurrMax.X = std::max(CurrMax.X, Obj.WorldAabbMax.X);
        CurrMax.Y = std::max(CurrMax.Y, Obj.WorldAabbMax.Y);
        CurrMax.Z = std::max(CurrMax.Z, Obj.WorldAabbMax.Z);
    }

    if (!AnyDyn)
    {
        ActiveClip0  = StaticClip0;
        HasDirtyPrev = false;
        return;
    }

    // Expand dirty region to cover dynamic shadows + local emissive/bounce influence (~2.2 m)
    constexpr float InfluencePadM = 2.25f;
    const Mathematics::Axis3D CurrPaddedMin = CurrMin - Mathematics::Axis3D{ InfluencePadM, InfluencePadM, 0.55f };
    const Mathematics::Axis3D CurrPaddedMax = CurrMax + Mathematics::Axis3D{ InfluencePadM, InfluencePadM, 1.35f };

    Mathematics::Axis3D DirtyMin = CurrPaddedMin;
    Mathematics::Axis3D DirtyMax = CurrPaddedMax;
    if (!FullRefresh && HasDirtyPrev)
    {
        DirtyMin.X = std::min(DirtyMin.X, DirtyPrevMin.X);
        DirtyMin.Y = std::min(DirtyMin.Y, DirtyPrevMin.Y);
        DirtyMin.Z = std::min(DirtyMin.Z, DirtyPrevMin.Z);
        DirtyMax.X = std::max(DirtyMax.X, DirtyPrevMax.X);
        DirtyMax.Y = std::max(DirtyMax.Y, DirtyPrevMax.Y);
        DirtyMax.Z = std::max(DirtyMax.Z, DirtyPrevMax.Z);
    }

    const Mathematics::Axis3D Span = ActiveClip0.BoundsMax - ActiveClip0.BoundsMin;
    auto WorldToVoxelIdx = [&](Mathematics::Axis3D P, uint32_t Dim, int Axis) noexcept -> uint32_t
    {
        const float MinV  = (Axis == 0) ? ActiveClip0.BoundsMin.X : ((Axis == 1) ? ActiveClip0.BoundsMin.Y : ActiveClip0.BoundsMin.Z);
        const float InvV  = (Axis == 0) ? ActiveClip0.InvSpan.X   : ((Axis == 1) ? ActiveClip0.InvSpan.Y   : ActiveClip0.InvSpan.Z);
        const float Norm  = std::clamp((P.X * (Axis == 0) + P.Y * (Axis == 1) + P.Z * (Axis == 2) - MinV) * InvV, 0.0f, 1.0f);
        return static_cast<uint32_t>(std::clamp(std::round(Norm * static_cast<float>(Dim - 1u)), 0.0f, static_cast<float>(Dim - 1u)));
    };

    const uint32_t X0 = WorldToVoxelIdx(DirtyMin, ActiveClip0.DimX, 0);
    const uint32_t Y0 = WorldToVoxelIdx(DirtyMin, ActiveClip0.DimY, 1);
    const uint32_t Z0 = WorldToVoxelIdx(DirtyMin, ActiveClip0.DimZ, 2);
    const uint32_t X1 = WorldToVoxelIdx(DirtyMax, ActiveClip0.DimX, 0);
    const uint32_t Y1 = WorldToVoxelIdx(DirtyMax, ActiveClip0.DimY, 1);
    const uint32_t Z1 = WorldToVoxelIdx(DirtyMax, ActiveClip0.DimZ, 2);

    const uint32_t SXY = ActiveClip0.DimX * ActiveClip0.DimY;
    const float DynBand = ActiveClip0.VoxelSpacingM * 1.95f;

    // Pass 1: Restore static voxels in dirty region & composite dynamic object SDF distances + vehicle surface cache
    ParallelForZ(Z1 - Z0 + 1u, [&](uint32_t RelS, uint32_t RelE)
    {
        for (uint32_t RZ = RelS; RZ < RelE; ++RZ)
        {
            const uint32_t Z = Z0 + RZ;
            const float PZ = ActiveClip0.BoundsMin.Z + (static_cast<float>(Z) / static_cast<float>(ActiveClip0.DimZ - 1u)) * Span.Z;
            for (uint32_t Y = Y0; Y <= Y1; ++Y)
            {
                const float PY = ActiveClip0.BoundsMin.Y + (static_cast<float>(Y) / static_cast<float>(ActiveClip0.DimY - 1u)) * Span.Y;
                for (uint32_t X = X0; X <= X1; ++X)
                {
                    const float PX = ActiveClip0.BoundsMin.X + (static_cast<float>(X) / static_cast<float>(ActiveClip0.DimX - 1u)) * Span.X;
                    const std::size_t Idx = static_cast<std::size_t>(Z) * SXY + static_cast<std::size_t>(Y) * ActiveClip0.DimX + X;
                    const Mathematics::Axis3D WP { PX, PY, PZ };

                    VoxelSurfaceCacheEntry Out = StaticClip0.Voxels[Idx];
                    float BestDynD = 1.0e5f;
                    const DynamicLocalSdfInstance* BestObj = nullptr;
                    Mathematics::Axis3D BestLocalPos {};

                    for (const auto& Obj : DynamicObjects)
                    {
                        if (!Obj.Enabled || Obj.Asset == nullptr) continue;
                        const Mathematics::Axis3D LP = Obj.Transform.TransformPoint(Obj.Transform.WorldToLocal, WP);
                        const float D = Obj.Asset->SampleConservativeLocalDistance(LP);
                        if (D < BestDynD)
                        {
                            BestDynD     = D;
                            BestObj      = &Obj;
                            BestLocalPos = LP;
                        }
                    }

                    if (BestObj != nullptr && BestDynD < Out.SignedDistanceM)
                    {
                        Out.SignedDistanceM = BestDynD;
                        if (std::fabs(BestDynD) <= DynBand)
                        {
                            VoxelSurfaceCacheEntry LocalSurf {};
                            if (BestObj->Asset->SampleLocalSurfaceCache(BestLocalPos, LocalSurf))
                            {
                                Out.AlbedoRgb     = LocalSurf.AlbedoRgb;
                                Out.EmissiveRgb   = LocalSurf.EmissiveRgb;
                                Out.Metalness     = LocalSurf.Metalness;
                                Out.Roughness     = LocalSurf.Roughness;
                                Out.MaterialSlot  = LocalSurf.MaterialSlot;
                                Out.HasSurface    = 1u;
                                Out.SurfaceNormal = BestObj->Transform.TransformDirection(
                                    BestObj->Transform.LocalToWorld,
                                    BestObj->Asset->EvaluateLocalNormal(BestLocalPos));

                                // Evaluate direct lighting on moved local surface + static bounce environment
                                Out.DirectRadiance = EvaluateDirectVoxelSurfaceLighting(
                                    Out, WP, *this, Mask, Sun, PointLights, AreaLights, SkyRadiance, {});
                                const float DiffW = 1.0f - std::clamp(Out.Metalness, 0.0f, 1.0f);
                                const auto EnvBounce = StaticClip0.SampleRadiosityTrilinear(
                                    WP + Out.SurfaceNormal * 0.45f, 0.35f);
                                Out.BounceRadiance = {
                                    std::min(32.0f, Out.DirectRadiance.R + Out.AlbedoRgb.R * DiffW * EnvBounce.R * 0.70f),
                                    std::min(32.0f, Out.DirectRadiance.G + Out.AlbedoRgb.G * DiffW * EnvBounce.G * 0.70f),
                                    std::min(32.0f, Out.DirectRadiance.B + Out.AlbedoRgb.B * DiffW * EnvBounce.B * 0.70f),
                                };
                            }
                        }
                    }

                    ActiveClip0.Voxels[Idx] = Out;
                }
            }
        }
    });

    // Pass 2: Modulate nearby static surface voxels (ground & adjacent walls) by dynamic shadow + dynamic vehicle emissive bounce!
    ParallelForZ(Z1 - Z0 + 1u, [&](uint32_t RelS, uint32_t RelE)
    {
        for (uint32_t RZ = RelS; RZ < RelE; ++RZ)
        {
            const uint32_t Z = Z0 + RZ;
            const float PZ = ActiveClip0.BoundsMin.Z + (static_cast<float>(Z) / static_cast<float>(ActiveClip0.DimZ - 1u)) * Span.Z;
            for (uint32_t Y = Y0; Y <= Y1; ++Y)
            {
                const float PY = ActiveClip0.BoundsMin.Y + (static_cast<float>(Y) / static_cast<float>(ActiveClip0.DimY - 1u)) * Span.Y;
                for (uint32_t X = X0; X <= X1; ++X)
                {
                    const std::size_t Idx = static_cast<std::size_t>(Z) * SXY + static_cast<std::size_t>(Y) * ActiveClip0.DimX + X;
                    auto& V = ActiveClip0.Voxels[Idx];
                    const auto& StaticV = StaticClip0.Voxels[Idx];
                    if (!StaticV.HasSurface || V.SignedDistanceM < StaticV.SignedDistanceM - 1.0e-4f)
                    {
                        continue;
                    }

                    const float PX = ActiveClip0.BoundsMin.X + (static_cast<float>(X) / static_cast<float>(ActiveClip0.DimX - 1u)) * Span.X;
                    const Mathematics::Axis3D WP { PX, PY, PZ };
                    const Mathematics::Axis3D N = Mathematics::NormalizeAxis(StaticV.SurfaceNormal, { 0.0f, 0.0f, 1.0f });

                    // Re-evaluate direct lighting including dynamic objects' occlusion so shadow under & behind moving car updates live!
                    const auto DynDirect = EvaluateDirectVoxelSurfaceLighting(
                        StaticV, WP, *this, Mask, Sun, PointLights, AreaLights, SkyRadiance, DynamicObjects);
                    const Mathematics::LinearRGB StaticBounceDelta {
                        std::max(0.0f, StaticV.BounceRadiance.R - StaticV.DirectRadiance.R),
                        std::max(0.0f, StaticV.BounceRadiance.G - StaticV.DirectRadiance.G),
                        std::max(0.0f, StaticV.BounceRadiance.B - StaticV.DirectRadiance.B),
                    };

                    // Add immediate 1-bounce radiosity from dynamic vehicle emissives/sunlit body onto nearby floor/walls!
                    Mathematics::LinearRGB DynBounce {};
                    const float DiffW = 1.0f - std::clamp(StaticV.Metalness, 0.0f, 1.0f);
                    if (DiffW > 0.02f)
                    {
                        Mathematics::Axis3D T {}, B {};
                        BuildOrthonormalDuff(N, T, B);
                        for (uint32_t K = 0u; K < 4u; ++K)
                        {
                            const float R   = std::sqrt((static_cast<float>(K) + 0.5f) * 0.25f);
                            const float Ang = static_cast<float>(K) * 2.39996323f + 0.4f;
                            const Mathematics::Axis3D CDir = Mathematics::NormalizeAxis(
                                T * (R * std::cos(Ang)) + B * (R * std::sin(Ang)) + N * std::sqrt(std::max(0.0f, 1.0f - R * R)));
                            const Mathematics::Axis3D ProbeP = WP + N * ActiveClip0.VoxelSpacingM + CDir * 0.75f;
                            if (ActiveClip0.ContainsInsideMargin(ProbeP, 0.015f))
                            {
                                DynBounce = DynBounce + ActiveClip0.SampleRadiosityTrilinear(ProbeP, 0.35f) * 0.25f;
                            }
                        }
                    }

                    V.DirectRadiance = DynDirect;
                    V.BounceRadiance = {
                        std::min(32.0f, DynDirect.R + StaticBounceDelta.R * 0.55f + StaticV.AlbedoRgb.R * DiffW * DynBounce.R * 0.45f),
                        std::min(32.0f, DynDirect.G + StaticBounceDelta.G * 0.55f + StaticV.AlbedoRgb.G * DiffW * DynBounce.G * 0.45f),
                        std::min(32.0f, DynDirect.B + StaticBounceDelta.B * 0.55f + StaticV.AlbedoRgb.B * DiffW * DynBounce.B * 0.45f),
                    };
                }
            }
        }
    });

    ActiveClip0.RebuildMip1();
    DirtyPrevMin = CurrPaddedMin;
    DirtyPrevMax = CurrPaddedMax;
    HasDirtyPrev = true;
    LastDynamicStats.DirtyVoxelCount = (X1 - X0 + 1u) * (Y1 - Y0 + 1u) * (Z1 - Z0 + 1u);
    LastDynamicStats.TotalClip0Voxels = static_cast<uint32_t>(ActiveClip0.Voxels.size());

    // Execute exact arena/f2d37e4b-frontier 3-tier 48^3 toroidal DistanceClipmap relocation
    const DynamicLocalSdfInstance* PrimaryDyn = nullptr;
    for (const auto& Obj : DynamicObjects)
    {
        if (Obj.Enabled && Obj.Asset != nullptr)
        {
            PrimaryDyn = &Obj;
            break;
        }
    }
    if (PrimaryDyn != nullptr)
    {
        ToroidalClipmap.Local = &PrimaryDyn->Asset->BakedDistance;
        const auto& M = PrimaryDyn->Transform.LocalToWorld;
        RigidPose Pose {};
        Pose.Translation = { M[3], M[7], M[11] };
        Pose.Cosine      = M[0];
        Pose.Sine        = M[4];
        const Mathematics::Axis3D FocusCenter = (ActiveClip0.BoundsMin + ActiveClip0.BoundsMax) * 0.5f;
        ToroidalClipmap.Relocate(Pose, { FocusCenter.X, FocusCenter.Y, FocusCenter.Z }, FullRefresh);
        const unsigned RewrittenTotal = ToroidalClipmap.Rewritten[0] + ToroidalClipmap.Rewritten[1] + ToroidalClipmap.Rewritten[2];
        if (ToroidalClipmap.Levels[0].Initialized)
        {
            LastDynamicStats.DirtyVoxelCount  = RewrittenTotal;
            LastDynamicStats.TotalClip0Voxels = DistanceClipmap::LevelCount * DistanceClipmap::Resolution * DistanceClipmap::Resolution * DistanceClipmap::Resolution;
        }
    }
}

DistanceInterval VoxelSdfClipmapVolume::SampleClipmapInterval(
    Mathematics::Axis3D WorldPos,
    unsigned*           OutTier) const noexcept
{
    if (ToroidalClipmap.Levels[0].Initialized)
    {
        return ToroidalClipmap.Sample({ WorldPos.X, WorldPos.Y, WorldPos.Z }, OutTier);
    }
    if (OutTier) *OutTier = UINT32_MAX;
    return { 0.0f, 0.0f };
}

bool VoxelSdfClipmapVolume::TraceHybridOccluded(
    Mathematics::Axis3D                      Origin,
    Mathematics::Axis3D                      Direction,
    float                                    Limit,
    std::span<const DynamicLocalSdfInstance> DynamicObjects,
    unsigned*                                OutSteps) const noexcept
{
    const ClipVector O { Origin.X, Origin.Y, Origin.Z };
    const ClipVector D { Direction.X, Direction.Y, Direction.Z };

    if (!StaticAtlas.Surfaces.empty())
    {
        if (StaticAtlas.Trace(O, D, Limit, true).Confirmed())
        {
            return true;
        }
    }

    // Exact arena/f2d37e4b-frontier CarOccluded: AABB check -> conservative Lower interval march -> exact triangle refinement
    for (const auto& Obj : DynamicObjects)
    {
        if (!Obj.Enabled || Obj.Asset == nullptr || Obj.Asset->LocalAtlas.Surfaces.empty()) continue;
        const auto LP0 = Obj.Transform.TransformPoint(Obj.Transform.WorldToLocal, Origin);
        const auto LD0 = Obj.Transform.TransformDirection(Obj.Transform.WorldToLocal, Direction);
        const ClipVector LocalOrigin { LP0.X, LP0.Y, LP0.Z };
        const ClipVector LocalDirection { LD0.X, LD0.Y, LD0.Z };
        float Near = 0.0f;
        if (!FacetAtlas::IntersectEnclosure(
                LocalOrigin, LocalDirection, Obj.Asset->LocalAtlas.Low, Obj.Asset->LocalAtlas.High, 0.0f, Limit, Near))
        {
            continue;
        }
        float Travel = Near;
        for (unsigned Step = 0u; Step < 96u; ++Step)
        {
            if (OutSteps) ++(*OutSteps);
            const auto Interval = SampleClipmapInterval(Origin + Direction * Travel);
            if (Interval.Lower < 1.0e-4f) break;
            Travel += Interval.Lower;
            if (Travel > Limit) break;
            const ClipVector P = LocalOrigin + LocalDirection * Travel;
            if (P.X < Obj.Asset->LocalAtlas.Low.X - 1.0e-4f || P.X > Obj.Asset->LocalAtlas.High.X + 1.0e-4f ||
                P.Y < Obj.Asset->LocalAtlas.Low.Y - 1.0e-4f || P.Y > Obj.Asset->LocalAtlas.High.Y + 1.0e-4f ||
                P.Z < Obj.Asset->LocalAtlas.Low.Z - 1.0e-4f || P.Z > Obj.Asset->LocalAtlas.High.Z + 1.0e-4f)
            {
                break;
            }
        }
        // Refinement on the original interval (never an advanced biased origin)
        if (Obj.Asset->LocalAtlas.Trace(LocalOrigin, LocalDirection, Limit, true).Confirmed())
        {
            return true;
        }
    }
    return false;
}

float VoxelSdfClipmapVolume::SampleClipmapDistance(
    Mathematics::Axis3D                      WorldPos,
    int&                                     OutCascade,
    std::span<const DynamicLocalSdfInstance> DynamicObjects) const noexcept
{
    if (ToroidalClipmap.Levels[0].Initialized)
    {
        unsigned Tier = 0u;
        const auto Interval = ToroidalClipmap.Sample({ WorldPos.X, WorldPos.Y, WorldPos.Z }, &Tier);
        if (Tier < DistanceClipmap::LevelCount)
        {
            OutCascade = static_cast<int>(Tier);
            return Interval.Raw;
        }
        OutCascade = static_cast<int>(DistanceClipmap::LevelCount);
        return 1.0e5f;
    }

    float D = 1.0e5f;
    if (ActiveClip0.ContainsInsideMargin(WorldPos, 0.018f))
    {
        OutCascade = 0;
        D = ActiveClip0.SampleDistanceTrilinear(WorldPos);
    }
    else if (StaticClip1.ContainsInsideMargin(WorldPos, 0.015f))
    {
        OutCascade = 1;
        D = StaticClip1.SampleDistanceTrilinear(WorldPos);
    }
    else
    {
        OutCascade = 2;
        return 1.0e5f;
    }

    if (!DynamicObjects.empty())
    {
        for (const auto& Obj : DynamicObjects)
        {
            if (!Obj.Enabled || Obj.Asset == nullptr) continue;
            const Mathematics::Axis3D LP = Obj.Transform.TransformPoint(Obj.Transform.WorldToLocal, WorldPos);
            D = std::min(D, Obj.Asset->SampleConservativeLocalDistance(LP));
        }
    }
    return D;
}

Mathematics::Axis3D VoxelSdfClipmapVolume::EvaluateClipmapNormal(
    Mathematics::Axis3D                      WorldPos,
    int                                      CascadeHint,
    std::span<const DynamicLocalSdfInstance> DynamicObjects) const noexcept
{
    const float H = (CascadeHint == 0)
        ? std::max(0.02f, ActiveClip0.VoxelSpacingM * 0.90f)
        : std::max(0.05f, StaticClip1.VoxelSpacingM * 0.90f);
    int Dummy = 0;
    const float D0 = SampleClipmapDistance({ WorldPos.X + H, WorldPos.Y - H, WorldPos.Z - H }, Dummy, DynamicObjects);
    const float D1 = SampleClipmapDistance({ WorldPos.X - H, WorldPos.Y - H, WorldPos.Z + H }, Dummy, DynamicObjects);
    const float D2 = SampleClipmapDistance({ WorldPos.X - H, WorldPos.Y + H, WorldPos.Z - H }, Dummy, DynamicObjects);
    const float D3 = SampleClipmapDistance({ WorldPos.X + H, WorldPos.Y + H, WorldPos.Z + H }, Dummy, DynamicObjects);
    return Mathematics::NormalizeAxis({
         D0 - D1 - D2 + D3,
        -D0 - D1 + D2 + D3,
        -D0 + D1 - D2 + D3,
    }, { 0.0f, 0.0f, 1.0f });
}

Mathematics::LinearRGB VoxelSdfClipmapVolume::SampleClipmapRadiosity(
    Mathematics::Axis3D WorldPos,
    int                 Cascade,
    float               ConeRadiusM) const noexcept
{
    if (Cascade == 0)
    {
        return ActiveClip0.SampleRadiosityTrilinear(WorldPos, ConeRadiusM);
    }
    if (Cascade == 1)
    {
        return StaticClip1.SampleRadiosityTrilinear(WorldPos, ConeRadiusM);
    }
    return {};
}

Mathematics::LinearRGB VoxelSdfClipmapVolume::SampleClipmapAlbedo(
    Mathematics::Axis3D WorldPos,
    int                 Cascade) const noexcept
{
    const VoxelClipmapCascade& Cas = (Cascade == 0) ? ActiveClip0 : StaticClip1;
    if (Cas.DimX < 2u || Cas.DimY < 2u || Cas.DimZ < 2u || Cas.Voxels.empty()) return {};
    const float RX = std::clamp((WorldPos.X - Cas.BoundsMin.X) * Cas.InvSpan.X, 0.0f, 1.0f) * static_cast<float>(Cas.DimX - 1u);
    const float RY = std::clamp((WorldPos.Y - Cas.BoundsMin.Y) * Cas.InvSpan.Y, 0.0f, 1.0f) * static_cast<float>(Cas.DimY - 1u);
    const float RZ = std::clamp((WorldPos.Z - Cas.BoundsMin.Z) * Cas.InvSpan.Z, 0.0f, 1.0f) * static_cast<float>(Cas.DimZ - 1u);
    const uint32_t IX = static_cast<uint32_t>(std::lround(RX));
    const uint32_t IY = static_cast<uint32_t>(std::lround(RY));
    const uint32_t IZ = static_cast<uint32_t>(std::lround(RZ));
    return Cas.Voxels[static_cast<std::size_t>(IZ) * (Cas.DimX * Cas.DimY) + static_cast<std::size_t>(IY) * Cas.DimX + IX].AlbedoRgb;
}

ClipmapShadowResult VoxelSdfClipmapVolume::TraceClipmapSdfPenumbra(
    Mathematics::Axis3D                      WorldPos,
    Mathematics::Axis3D                      SurfaceNormal,
    Mathematics::Axis3D                      ToLightDir,
    float                                    MaxDistM,
    float                                    LightAngularOrRadius,
    bool                                     IsDirectionalLight,
    std::span<const DynamicLocalSdfInstance> DynamicObjects,
    bool                                     ForceCoarseOnly) const noexcept
{
    ClipmapShadowResult Res {};
    const float NdotL = Mathematics::EvaluateDot(SurfaceNormal, ToLightDir);
    if (NdotL <= 0.002f)
    {
        Res.Visibility = 0.0f;
        return Res;
    }

    // Exact arena/f2d37e4b-frontier Penumbra algorithm (LocalPenumbra when ForceCoarseOnly=true, ClipPenumbra when false)
    const bool Global = !ForceCoarseOnly && ToroidalClipmap.Levels[0].Initialized;
    float Voxel = 0.125f;
    if (!Global && !DynamicObjects.empty() && DynamicObjects[0].Asset != nullptr)
    {
        const auto& Sp = DynamicObjects[0].Asset->BakedDistance.Spacing;
        Voxel = std::max({ Sp.X, Sp.Y, Sp.Z, 0.04f });
    }

    const Mathematics::Axis3D Origin = WorldPos + SurfaceNormal * 2.0e-4f;
    const float AngularRadius = IsDirectionalLight
        ? std::max(0.00465f, LightAngularOrRadius)
        : std::max(0.01f, LightAngularOrRadius / std::max(0.25f, MaxDistM));

    float Travel = Voxel;
    float Vis    = 1.0f;

    for (uint32_t Step = 0u; Step < 96u && Travel < MaxDistM; ++Step)
    {
        const Mathematics::Axis3D P = Origin + ToLightDir * Travel;
        float Distance = 1.0e5f;
        if (Global)
        {
            unsigned Tier = 0u;
            Distance = ToroidalClipmap.Sample({ P.X, P.Y, P.Z }, &Tier).Raw;
            if (Tier == UINT32_MAX) break;
        }
        else
        {
            for (const auto& Obj : DynamicObjects)
            {
                if (!Obj.Enabled || Obj.Asset == nullptr) continue;
                const auto LP = Obj.Transform.TransformPoint(Obj.Transform.WorldToLocal, P);
                Distance = std::min(Distance, Obj.Asset->SampleConservativeLocalDistance(LP));
            }
            for (const auto& Box : ClosedBoxes)
            {
                Distance = std::min(Distance, std::abs(SignedBoxDistance({ P.X, P.Y, P.Z }, Box)));
            }
            if (ClosedBoxes.empty() && !StaticAtlas.Surfaces.empty())
            {
                Distance = std::min(Distance, StaticAtlas.Nearest({ P.X, P.Y, P.Z }));
            }
        }

        if (Distance < 0.008f)
        {
            Res.Visibility   = 0.0f;
            Res.BlockerDistM = Travel;
            return Res;
        }
        const float StepVis = std::clamp(Distance / std::max(0.001f, Travel * AngularRadius), 0.0f, 1.0f);
        if (StepVis < Vis)
        {
            Vis = StepVis;
            Res.BlockerDistM = Travel;
        }
        Travel += std::max(Voxel * 0.25f, Distance * 0.9f);
    }

    Res.Visibility = Vis;
    return Res;
}

Mathematics::LinearRGB VoxelSdfClipmapVolume::EvaluateDeterministicConeGiOverPi(
    Mathematics::Axis3D                      WorldPos,
    Mathematics::Axis3D                      SurfaceNormal,
    Mathematics::LinearRGB                   SkyRadiance,
    uint32_t                                 ConeCount,
    std::span<const DynamicLocalSdfInstance> DynamicObjects) const noexcept
{
    const uint32_t Count = std::clamp(ConeCount, 12u, 24u);
    const float InvCount = 1.0f / static_cast<float>(Count);
    const Mathematics::Axis3D N = Mathematics::NormalizeAxis(SurfaceNormal, { 0.0f, 0.0f, 1.0f });
    Mathematics::Axis3D T {}, B {};
    BuildOrthonormalDuff(N, T, B);

    const float BaseBias = std::max(0.032f, ActiveClip0.VoxelSpacingM * 0.75f);
    const Mathematics::Axis3D Origin = WorldPos + N * BaseBias;

    Mathematics::LinearRGB GiIrr {};
    for (uint32_t K = 0u; K < Count; ++K)
    {
        // Pure deterministic 24-cone cosine-weighted Fibonacci lattice (zero PCG, zero pixel jitter, zero denoiser!)
        const float URad = (static_cast<float>(K) + 0.5f) * InvCount;
        const float R    = std::sqrt(std::clamp(URad, 0.015f, 0.96f));
        const float Ang  = static_cast<float>(K) * 2.39996323f + 0.31f;
        const Mathematics::Axis3D CDir = Mathematics::NormalizeAxis(
            T * (R * std::cos(Ang)) + B * (R * std::sin(Ang)) + N * std::sqrt(std::max(0.0f, 1.0f - R * R)));

        float CT = std::max(0.055f, ActiveClip0.VoxelSpacingM * 0.90f);
        float CAlpha = 0.0f;
        Mathematics::LinearRGB CCol {};

        for (uint32_t St = 0u; St < 24u && CAlpha < 0.97f && CT < 14.0f; ++St)
        {
            const Mathematics::Axis3D SP = Origin + CDir * CT;
            int Casc = 3;
            const float CD = SampleClipmapDistance(SP, Casc, DynamicObjects);
            if (Casc >= 3 || CD > 4.0e3f) break;

            const float Vox   = (Casc == 0) ? 0.125f : (Casc == 1) ? 0.25f : 0.50f;
            const float ConeR = std::max(Vox * 0.85f, CT * 0.32f);
            if (CD < ConeR)
            {
                float Occ = 1.0f - SmoothStep(-Vox * 0.15f, ConeR, CD);
                const auto SRad  = SampleClipmapRadiosity(SP, Casc, ConeR * 1.35f);
                const auto SurfN = EvaluateClipmapNormal(SP, Casc, DynamicObjects);
                const float FrontEmit = SmoothStep(-0.18f, 0.45f, Mathematics::EvaluateDot(SurfN, CDir * -1.0f)) * 1.15f;
                if (CT < 0.85f && std::max({ SRad.R, SRad.G, SRad.B }) < 0.25f)
                {
                    Occ = std::min(1.0f, Occ * 1.35f);
                }
                const float StepW = (1.0f - CAlpha) * Occ;
                CCol.R += std::min(SRad.R, 8.0f) * FrontEmit * StepW;
                CCol.G += std::min(SRad.G, 8.0f) * FrontEmit * StepW;
                CCol.B += std::min(SRad.B, 8.0f) * FrontEmit * StepW;
                CAlpha += StepW;
            }
            CT += std::max(CD * 0.78f, Vox * 0.68f);
        }

        const float SkyHem = 0.5f + 0.5f * CDir.Z;
        const Mathematics::LinearRGB SkyAmb {
            SkyRadiance.R * (0.45f + 0.85f * SkyHem),
            SkyRadiance.G * (0.45f + 0.85f * SkyHem),
            SkyRadiance.B * (0.45f + 0.85f * SkyHem),
        };
        const float Unocc = 1.0f - CAlpha;
        GiIrr.R += (CCol.R + SkyAmb.R * Unocc) * InvCount;
        GiIrr.G += (CCol.G + SkyAmb.G * Unocc) * InvCount;
        GiIrr.B += (CCol.B + SkyAmb.B * Unocc) * InvCount;
    }

    return {
        std::min(GiIrr.R, 18.0f),
        std::min(GiIrr.G, 18.0f),
        std::min(GiIrr.B, 18.0f),
    };
}

} // namespace Frontier::DistanceFieldSpace
