/*============================================================================================================================================
                                                    SURFELRADIOSITYLATTICESEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Order 2 GI Candidate 2: 2-bounce Bunnell finite-disk radiosity + Dual-Cascade C1-Hermite Octahedral Irradiance Lattice

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/SurfelRadiosityLatticeSequence.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace Frontier::GeometricRaster::GlobalIllumination
{

namespace
{

[[nodiscard]] Mathematics::Axis3D DecodeOctahedralUnitVector(float U, float V) noexcept
{
    // U, V in [-1, 1]
    Mathematics::Axis3D N { U, V, 1.0f - std::fabs(U) - std::fabs(V) };
    if (N.Z < 0.0f)
    {
        const float OldX = N.X;
        const float OldY = N.Y;
        N.X = (1.0f - std::fabs(OldY)) * ((OldX >= 0.0f) ? 1.0f : -1.0f);
        N.Y = (1.0f - std::fabs(OldX)) * ((OldY >= 0.0f) ? 1.0f : -1.0f);
    }
    return Mathematics::NormalizeAxis(N, { 0.0f, 0.0f, 1.0f });
}

void EncodeOctahedralSheetUv(Mathematics::Axis3D UnitDir, float& OutU01, float& OutV01) noexcept
{
    const float InvL1 = 1.0f / std::max(1.0e-8f, std::fabs(UnitDir.X) + std::fabs(UnitDir.Y) + std::fabs(UnitDir.Z));
    float OX = UnitDir.X * InvL1;
    float OY = UnitDir.Y * InvL1;
    if (UnitDir.Z < 0.0f)
    {
        const float TempX = OX;
        OX = (1.0f - std::fabs(OY)) * ((TempX >= 0.0f) ? 1.0f : -1.0f);
        OY = (1.0f - std::fabs(TempX)) * ((OY >= 0.0f) ? 1.0f : -1.0f);
    }
    OutU01 = std::clamp(0.5f * OX + 0.5f, 0.0f, 1.0f);
    OutV01 = std::clamp(0.5f * OY + 0.5f, 0.0f, 1.0f);
}

[[nodiscard]] float SmoothHermiteBake(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-6f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

// Segment visibility for cascade bake (lattice nodes have no shading normal, so march SDF along the segment directly)
[[nodiscard]] float EvaluateBakeSegmentVisibility(
    const DirectLighting::HybridOccluderEnvironment& Occluders,
    Mathematics::Axis3D                              FromPos,
    Mathematics::Axis3D                              ToPos,
    float                                            TargetRadius,
    bool                                             HighEndZeroSdf) noexcept
{
    const Mathematics::Axis3D Delta = ToPos - FromPos;
    const float FullDist = Mathematics::EvaluateLength(Delta);
    if (FullDist <= 0.06f)
    {
        return 1.0f;
    }
    const Mathematics::Axis3D Dir = Delta * (1.0f / FullDist);

    if (HighEndZeroSdf && Occluders.SceneBvh != nullptr)
    {
        SpatialAcceleration::RaySegmentQuery Ray {};
        Ray.Origin          = FromPos;
        Ray.UnitDirection   = Dir;
        Ray.MinimumDistance = 0.002f;
        Ray.MaximumDistance = FullDist - 0.035f;
        if (Ray.MaximumDistance <= Ray.MinimumDistance)
        {
            return 1.0f;
        }
        return Occluders.SceneBvh->QueryRayOccluded(Ray) ? 0.0f : 1.0f;
    }

    const float SelfGuardM = 0.20f;
    const float Margin     = std::max(SelfGuardM, TargetRadius * 0.65f);
    const float StartT     = 0.12f;
    const float EndT       = FullDist - Margin;
    if (EndT <= StartT)
    {
        return 1.0f;
    }
    constexpr uint32_t StepCount = 32u;
    constexpr float    InvSteps  = 1.0f / static_cast<float>(StepCount);
    const float        StepDt    = (EndT - StartT) * InvSteps;
    const float        AngRadius = std::clamp(TargetRadius / std::max(0.5f, FullDist), 0.025f, 0.075f);
    float OpticalDepth = 0.0f;
    for (uint32_t Step = 0u; Step < StepCount; ++Step)
    {
        const float U       = (static_cast<float>(Step) + 0.5f) * InvSteps;
        const float T       = StartT + (EndT - StartT) * U;
        const float RampIn  = SmoothHermiteBake(0.10f, 0.26f, T);
        const float RampOut = 1.0f - SmoothHermiteBake(EndT - 0.14f, EndT, T);
        const float Weight  = RampIn * RampOut;
        if (Weight <= 1.0e-4f)
        {
            continue;
        }
        const float Dist    = DirectLighting::EvaluateInteriorOccluderDistance(Occluders, FromPos + Dir * T);
        const float ConeR   = std::max(0.036f, T * AngRadius);
        const float StepVis = SmoothHermiteBake(-0.025f, ConeR * 0.72f, Dist + 0.015f);
        OpticalDepth += 2.6f * (1.0f - StepVis) * Weight * (StepDt / std::max(0.08f, ConeR));
        if (OpticalDepth > 6.5f)
        {
            return 0.0f;
        }
    }
    return std::exp(-OpticalDepth);
}

[[nodiscard]] Mathematics::LinearRGB SampleLatticeOctahedralSheet(
    const OctahedralIrradianceCascade& Cascade,
    std::size_t                        LatticeLinearIdx,
    Mathematics::Axis3D                UnitDir) noexcept
{
    float U01 = 0.5f, V01 = 0.5f;
    EncodeOctahedralSheetUv(UnitDir, U01, V01);
    const uint32_t Res = Cascade.OctaSheetRes;
    const float FX = U01 * static_cast<float>(Res - 1u);
    const float FY = V01 * static_cast<float>(Res - 1u);
    const uint32_t X0 = std::min(Res - 2u, static_cast<uint32_t>(FX));
    const uint32_t Y0 = std::min(Res - 2u, static_cast<uint32_t>(FY));
    const float TX = FX - static_cast<float>(X0);
    const float TY = FY - static_cast<float>(Y0);

    const std::size_t LinearStart = LatticeLinearIdx * static_cast<std::size_t>(Res) * Res;
    const auto At = [&](uint32_t X, uint32_t Y) noexcept -> Mathematics::LinearRGB
    {
        return Cascade.IrradianceSheets[LinearStart + static_cast<std::size_t>(Y) * Res + X];
    };
    const auto C00 = At(X0, Y0);
    const auto C10 = At(X0 + 1u, Y0);
    const auto C01 = At(X0, Y0 + 1u);
    const auto C11 = At(X0 + 1u, Y0 + 1u);

    const auto LerpRgb = [](Mathematics::LinearRGB A, Mathematics::LinearRGB B, float T) noexcept -> Mathematics::LinearRGB
    {
        return { A.R + (B.R - A.R) * T, A.G + (B.G - A.G) * T, A.B + (B.B - A.B) * T };
    };
    return LerpRgb(LerpRgb(C00, C10, TX), LerpRgb(C01, C11, TX), TY);
}

[[nodiscard]] float EvaluateChebyshevVisibilityWeight(
    const OctahedralIrradianceCascade& Cascade,
    std::size_t                        LatticeLinearIdx,
    Mathematics::Axis3D                ShadingPos,
    Mathematics::Axis3D                LatticePos) noexcept
{
    if (Cascade.ChebyshevMoments.empty())
    {
        return 1.0f;
    }
    const Mathematics::Axis3D Delta = ShadingPos - LatticePos;
    const float Dist = Mathematics::EvaluateLength(Delta);
    if (Dist <= 1.0e-4f)
    {
        return 1.0f;
    }
    float U01 = 0.5f, V01 = 0.5f;
    EncodeOctahedralSheetUv(Delta * (1.0f / Dist), U01, V01);
    const uint32_t Res = Cascade.OctaSheetRes;
    const uint32_t X   = std::min(Res - 1u, static_cast<uint32_t>(U01 * static_cast<float>(Res)));
    const uint32_t Y   = std::min(Res - 1u, static_cast<uint32_t>(V01 * static_cast<float>(Res)));
    const std::size_t LinearStart = (LatticeLinearIdx * static_cast<std::size_t>(Res) * Res + static_cast<std::size_t>(Y) * Res + X) * 2u;
    const float Mean   = Cascade.ChebyshevMoments[LinearStart];
    const float MeanSq = Cascade.ChebyshevMoments[LinearStart + 1u];
    if (Dist <= Mean)
    {
        return 1.0f;
    }
    const float Variance = std::max(1.0e-3f, MeanSq - Mean * Mean);
    const float Diff     = Dist - Mean;
    const float Weight   = Variance / (Variance + Diff * Diff);
    return std::max(0.05f, Weight * Weight * Weight);
}

[[nodiscard]] Mathematics::LinearRGB SampleCascadeSmoothHermite(
    const OctahedralIrradianceCascade& Cascade,
    Mathematics::Axis3D                ShadingPos,
    Mathematics::Axis3D                ShadingNormal) noexcept
{
    if (Cascade.IrradianceSheets.empty() || Cascade.GridDimX < 2u || Cascade.GridDimY < 2u || Cascade.GridDimZ < 2u)
    {
        return {};
    }

    const Mathematics::Axis3D Biased = ShadingPos + ShadingNormal * 0.08f;
    const float FX = std::clamp((Biased.X - Cascade.BoundsMin.X) / Cascade.CellSpacing.X, 0.0f, static_cast<float>(Cascade.GridDimX - 1u) - 1.0e-4f);
    const float FY = std::clamp((Biased.Y - Cascade.BoundsMin.Y) / Cascade.CellSpacing.Y, 0.0f, static_cast<float>(Cascade.GridDimY - 1u) - 1.0e-4f);
    const float FZ = std::clamp((Biased.Z - Cascade.BoundsMin.Z) / Cascade.CellSpacing.Z, 0.0f, static_cast<float>(Cascade.GridDimZ - 1u) - 1.0e-4f);

    const uint32_t IX0 = std::min(Cascade.GridDimX - 2u, static_cast<uint32_t>(FX));
    const uint32_t IY0 = std::min(Cascade.GridDimY - 2u, static_cast<uint32_t>(FY));
    const uint32_t IZ0 = std::min(Cascade.GridDimZ - 2u, static_cast<uint32_t>(FZ));
    const float    LX  = FX - static_cast<float>(IX0);
    const float    LY  = FY - static_cast<float>(IY0);
    const float    LZ  = FZ - static_cast<float>(IZ0);

    // C1 Hermite smoothstep eliminates trilinear cell-boundary derivative kinks on glossy & matte walls!
    const float HX = LX * LX * (3.0f - 2.0f * LX);
    const float HY = LY * LY * (3.0f - 2.0f * LY);
    const float HZ = LZ * LZ * (3.0f - 2.0f * LZ);

    Mathematics::LinearRGB Accum { 0.0f, 0.0f, 0.0f };
    float WeightSum = 0.0f;

    for (uint32_t DZ = 0u; DZ < 2u; ++DZ)
    {
        for (uint32_t DY = 0u; DY < 2u; ++DY)
        {
            for (uint32_t DX = 0u; DX < 2u; ++DX)
            {
                const uint32_t NX = IX0 + DX;
                const uint32_t NY = IY0 + DY;
                const uint32_t NZ = IZ0 + DZ;
                const std::size_t LatticeIdx = (static_cast<std::size_t>(NZ) * Cascade.GridDimY + NY) * Cascade.GridDimX + NX;
                const Mathematics::Axis3D LatticePos {
                    Cascade.BoundsMin.X + static_cast<float>(NX) * Cascade.CellSpacing.X,
                    Cascade.BoundsMin.Y + static_cast<float>(NY) * Cascade.CellSpacing.Y,
                    Cascade.BoundsMin.Z + static_cast<float>(NZ) * Cascade.CellSpacing.Z,
                };

                const float HermiteW =
                    ((DX == 0u) ? (1.0f - HX) : HX) *
                    ((DY == 0u) ? (1.0f - HY) : HY) *
                    ((DZ == 0u) ? (1.0f - HZ) : HZ);

                const Mathematics::Axis3D ToLattice = LatticePos - ShadingPos;
                const float BackfaceW = std::max(0.08f, 0.5f + 0.5f * Mathematics::EvaluateDot(
                    ShadingNormal, Mathematics::NormalizeAxis(ToLattice, ShadingNormal)));
                const float ChebW = EvaluateChebyshevVisibilityWeight(Cascade, LatticeIdx, ShadingPos, LatticePos);
                const float TotalW = HermiteW * BackfaceW * ChebW;

                const auto LatticeE = SampleLatticeOctahedralSheet(Cascade, LatticeIdx, ShadingNormal);
                Accum.R   += LatticeE.R   * TotalW;
                Accum.G += LatticeE.G * TotalW;
                Accum.B  += LatticeE.B  * TotalW;
                WeightSum   += TotalW;
            }
        }
    }

    if (WeightSum <= 1.0e-6f)
    {
        return {};
    }
    const float InvW = 1.0f / WeightSum;
    return { Accum.R * InvW, Accum.G * InvW, Accum.B * InvW };
}

} // namespace

float EvaluateBunnellDiskFormFactor(
    Mathematics::Axis3D ReceiverPos,
    Mathematics::Axis3D ReceiverNormal,
    Mathematics::Axis3D DiskPos,
    Mathematics::Axis3D DiskNormal,
    float               DiskArea,
    bool                TwoSidedDisk) noexcept
{
    const Mathematics::Axis3D Delta = DiskPos - ReceiverPos;
    const float DistSq = Mathematics::EvaluateLengthSquared(Delta);
    if (DistSq <= 1.0e-7f || DiskArea <= 0.0f)
    {
        return 0.0f;
    }
    const float InvDist = 1.0f / std::sqrt(DistSq);
    const Mathematics::Axis3D Wi = Delta * InvDist;
    const float CosR = Mathematics::EvaluateDot(ReceiverNormal, Wi);
    if (CosR <= 0.0f)
    {
        return 0.0f;
    }
    const float RawCosE = Mathematics::EvaluateDot(DiskNormal, Wi * -1.0f);
    const float CosE    = TwoSidedDisk ? std::fabs(RawCosE) : std::max(0.0f, RawCosE);
    if (CosE <= 0.0f)
    {
        return 0.0f;
    }
    // Bunnell finite-disk solid-angle ratio A*cosR*cosE / (pi*d^2 + A) stays bounded in [0, 1] as d -> 0
    return std::clamp((DiskArea * CosR * CosE) / (Constants::PiFloat * DistSq + DiskArea), 0.0f, 1.0f);
}

void SurfelRadiosityLatticeSequence::Reset() noexcept
{
    Surfels.clear();
    InnerCascade = {};
    OuterCascade = {};
}

void SurfelRadiosityLatticeSequence::BuildRelaxedLatticeFromScene(
    std::span<const SpatialAcceleration::RayTrianglePrimitive> Primitives,
    std::span<const Mathematics::LinearRGB>                    MaterialAlbedoBySlot,
    std::span<const Mathematics::LinearRGB>                    MaterialEmissiveBySlot,
    const DirectLighting::HybridOccluderEnvironment&           Occluders,
    const SurfelLatticeTierSettings&                           Settings)
{
    Reset();
    if (Primitives.empty())
    {
        return;
    }

    Mathematics::Axis3D SceneMin {  1.0e6f,  1.0e6f,  1.0e6f };
    Mathematics::Axis3D SceneMax { -1.0e6f, -1.0e6f, -1.0e6f };
    double TotalNonEmissiveArea = 0.0;

    for (const auto& Tri : Primitives)
    {
        const Mathematics::Axis3D Pts[3] { Tri.Corner0, Tri.Corner0 + Tri.Edge01, Tri.Corner0 + Tri.Edge02 };
        for (const auto& P : Pts)
        {
            SceneMin.X = std::min(SceneMin.X, P.X); SceneMax.X = std::max(SceneMax.X, P.X);
            SceneMin.Y = std::min(SceneMin.Y, P.Y); SceneMax.Y = std::max(SceneMax.Y, P.Y);
            SceneMin.Z = std::min(SceneMin.Z, P.Z); SceneMax.Z = std::max(SceneMax.Z, P.Z);
        }

        const uint16_t MatIdx = Tri.MaterialOrdinal;
        const Mathematics::LinearRGB Em = (MatIdx < MaterialEmissiveBySlot.size())
            ? MaterialEmissiveBySlot[MatIdx]
            : Mathematics::LinearRGB {};
        const float MaxEm = std::max({ Em.R, Em.G, Em.B });
        const Mathematics::Axis3D CrossE = Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02);
        const float DoubleArea = Mathematics::EvaluateLength(CrossE);
        if (DoubleArea <= 1.0e-6f)
        {
            continue;
        }
        const float Area = 0.5f * DoubleArea;
        if (MaxEm > 1.0e-3f)
        {
            // Subdivide large emissive panels into compact Bunnell disks (~0.12 m^2 each)
            const uint32_t SubCount = std::clamp(static_cast<uint32_t>(std::ceil(Area / 0.14f)), 1u, 16u);
            const float SubArea     = Area / static_cast<float>(SubCount);
            const Mathematics::Axis3D Normal = CrossE * (1.0f / DoubleArea);
            for (uint32_t S = 0u; S < SubCount; ++S)
            {
                const uint32_t Seed = static_cast<uint32_t>((Surfels.size() + S + 1u) * 2654435761u);
                float U = (static_cast<float>(S) + 0.5f) / static_cast<float>(SubCount);
                float V = static_cast<float>((Seed >> 8u) & 0xFFu) / 256.0f * (1.0f - U);
                RadiositySurfelDisk Disk {};
                Disk.Position        = Tri.Corner0 + Tri.Edge01 * U + Tri.Edge02 * V;
                Disk.Normal          = Normal;
                Disk.EmissiveRgb     = Em;
                Disk.AreaMeters      = SubArea;
                Disk.RadiusMeters    = std::sqrt(SubArea / Constants::PiFloat);
                Disk.EmissiveSurface = true;
                Disk.TwoSidedSurface = false;
                Disk.ExitantRadiance = Em;
                Surfels.push_back(Disk);
            }
        }
        else
        {
            TotalNonEmissiveArea += static_cast<double>(Area);
        }
    }

    // Populate ~320 diffuse bounce surfels across walls, floor, vehicle, and character
    if (TotalNonEmissiveArea > 1.0e-4)
    {
        const uint32_t TargetDiffuseSurfels = Settings.HighEndZeroSdfTier ? 360u : 256u;
        const double   AreaStep             = TotalNonEmissiveArea / static_cast<double>(TargetDiffuseSurfels);
        double         Accum                = 0.0;

        for (const auto& Tri : Primitives)
        {
            const uint16_t MatIdx = Tri.MaterialOrdinal;
            const bool EmissiveActive = (MatIdx < MaterialEmissiveBySlot.size()) &&
                (std::max({ MaterialEmissiveBySlot[MatIdx].R, MaterialEmissiveBySlot[MatIdx].G, MaterialEmissiveBySlot[MatIdx].B }) > 1.0e-3f);
            if (EmissiveActive)
            {
                continue;
            }
            const Mathematics::Axis3D CrossE = Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02);
            const float DoubleArea = Mathematics::EvaluateLength(CrossE);
            if (DoubleArea <= 1.0e-6f) continue;
            const float Area = 0.5f * DoubleArea;
            const Mathematics::Axis3D Normal = CrossE * (1.0f / DoubleArea);
            const Mathematics::LinearRGB Albedo = (MatIdx < MaterialAlbedoBySlot.size())
                ? MaterialAlbedoBySlot[MatIdx]
                : Mathematics::LinearRGB { 0.5f, 0.5f, 0.5f };

            Accum += static_cast<double>(Area);
            while (Accum >= AreaStep && Surfels.size() < TargetDiffuseSurfels + 256u)
            {
                Accum -= AreaStep;
                const uint32_t Seed = static_cast<uint32_t>(Surfels.size() * 747796405u + 2891336453u);
                float U = static_cast<float>(Seed & 0xFFFFu) / 65536.0f;
                float V = static_cast<float>(Seed >> 16u) / 65536.0f;
                if (U + V > 1.0f) { U = 1.0f - U; V = 1.0f - V; }

                RadiositySurfelDisk Disk {};
                Disk.Position     = Tri.Corner0 + Tri.Edge01 * U + Tri.Edge02 * V;
                Disk.Normal       = Normal;
                Disk.AlbedoRgb    = Albedo;
                Disk.AreaMeters   = static_cast<float>(AreaStep);
                Disk.RadiusMeters = std::sqrt(Disk.AreaMeters / Constants::PiFloat);
                Surfels.push_back(Disk);
            }
        }
    }

    // 2-bounce Bunnell surfel radiosity relaxation (+ optional solar bounce when SunRadiance > 0)
    const float MaxSun = std::max({ Settings.SunRadiance.R, Settings.SunRadiance.G, Settings.SunRadiance.B });
    for (int Bounce = 0; Bounce < 2; ++Bounce)
    {
        std::vector<Mathematics::LinearRGB> NextExitant(Surfels.size());
        for (std::size_t I = 0u; I < Surfels.size(); ++I)
        {
            const auto& Dst = Surfels[I];
            if (Dst.EmissiveSurface)
            {
                NextExitant[I] = Dst.EmissiveRgb;
                continue;
            }

            Mathematics::LinearRGB GatherE { 0.0f, 0.0f, 0.0f };
            if (MaxSun > 1.0e-4f)
            {
                const float NdotS = std::max(0.0f, Mathematics::EvaluateDot(Dst.Normal, Settings.SunDirection));
                if (NdotS > 0.0f)
                {
                    float SunVis = 1.0f;
                    if (Settings.HighEndZeroSdfTier && Occluders.SceneBvh != nullptr)
                    {
                        DirectLighting::HybridConeShadowSetup S {};
                        S.VogelRaySamples = 1u;
                        SunVis = DirectLighting::EvaluateHighEndBvhDirectionalShadow(
                            *Occluders.SceneBvh, Dst.Position, Dst.Normal, Settings.SunDirection, static_cast<uint32_t>(I), S);
                    }
                    else
                    {
                        SunVis = DirectLighting::EvaluateGtxHybridDirectionalShadow(
                            Occluders, Dst.Position, Dst.Normal, Settings.SunDirection);
                    }
                    GatherE.R   += Settings.SunRadiance.R   * NdotS * SunVis;
                    GatherE.G += Settings.SunRadiance.G * NdotS * SunVis;
                    GatherE.B  += Settings.SunRadiance.B  * NdotS * SunVis;
                }
            }

            for (std::size_t J = 0u; J < Surfels.size(); ++J)
            {
                if (I == J) continue;
                const auto& Src = Surfels[J];
                if (Bounce == 0 && !Src.EmissiveSurface) continue;
                const float FF = EvaluateBunnellDiskFormFactor(
                    Dst.Position, Dst.Normal, Src.Position, Src.Normal, Src.AreaMeters, Src.TwoSidedSurface);
                if (FF <= 1.0e-5f) continue;

                float Vis = 1.0f;
                if (Src.EmissiveSurface)
                {
                    if (Settings.HighEndZeroSdfTier && Occluders.SceneBvh != nullptr)
                    {
                        Vis = DirectLighting::EvaluateHighEndBvhLuminaireVisibility(
                            *Occluders.SceneBvh, Dst.Position, Dst.Normal, Src.Position);
                    }
                    else
                    {
                        Vis = DirectLighting::EvaluateGtxHybridLuminaireVisibility(
                            Occluders, Dst.Position, Dst.Normal, Src.Position, Src.RadiusMeters);
                    }
                }

                const float Weight = Constants::PiFloat * FF * Vis;
                GatherE.R   += Src.ExitantRadiance.R   * Weight;
                GatherE.G += Src.ExitantRadiance.G * Weight;
                GatherE.B  += Src.ExitantRadiance.B  * Weight;
            }

            NextExitant[I] = {
                (Dst.AlbedoRgb.R   * GatherE.R)   / Constants::PiFloat,
                (Dst.AlbedoRgb.G * GatherE.G) / Constants::PiFloat,
                (Dst.AlbedoRgb.B  * GatherE.B)  / Constants::PiFloat,
            };
        }
        for (std::size_t I = 0u; I < Surfels.size(); ++I)
        {
            Surfels[I].ExitantRadiance = NextExitant[I];
        }
    }

    // 3. Configure Dual-Cascade C1-Hermite Octahedral Lattice dimensions per WorldRenderingRedoPlan.md §4.1 & §4.2
    const Mathematics::Axis3D Center = (SceneMin + SceneMax) * 0.5f;
    const Mathematics::Axis3D HalfSpan = (SceneMax - SceneMin) * 0.5f;

    auto SetupCascadeGrid = [&](
        OctahedralIrradianceCascade& Cascade,
        uint32_t                     DX,
        uint32_t                     DY,
        uint32_t                     DZ,
        uint32_t                     SheetRes,
        Mathematics::Axis3D          MinB,
        Mathematics::Axis3D          MaxB)
    {
        Cascade.GridDimX     = DX;
        Cascade.GridDimY     = DY;
        Cascade.GridDimZ     = DZ;
        Cascade.OctaSheetRes = SheetRes;
        Cascade.BoundsMin    = MinB;
        Cascade.BoundsMax    = MaxB;
        Cascade.CellSpacing  = {
            (MaxB.X - MinB.X) / static_cast<float>(DX - 1u),
            (MaxB.Y - MinB.Y) / static_cast<float>(DY - 1u),
            (MaxB.Z - MinB.Z) / static_cast<float>(DZ - 1u),
        };
        const std::size_t EntryCount = static_cast<std::size_t>(DX) * DY * DZ;
        const std::size_t Texels    = EntryCount * SheetRes * SheetRes;
        Cascade.IrradianceSheets.assign(Texels, Mathematics::LinearRGB {});
        if (Settings.HighEndZeroSdfTier && Occluders.SceneBvh != nullptr)
        {
            Cascade.ChebyshevMoments.assign(Texels * 2u, 10.0f);
        }

        const uint32_t ThreadBudget = std::min(DZ, std::max(1u, std::thread::hardware_concurrency()));
        auto PopulateDepthBand = [&](uint32_t ZStart, uint32_t ZEnd)
        {
            // Precompute octahedral texel unit normals
            std::vector<Mathematics::Axis3D> SheetDirs(static_cast<std::size_t>(SheetRes) * SheetRes);
            for (uint32_t SY = 0u; SY < SheetRes; ++SY)
            {
                const float V = -1.0f + 2.0f * (static_cast<float>(SY) + 0.5f) / static_cast<float>(SheetRes);
                for (uint32_t SX = 0u; SX < SheetRes; ++SX)
                {
                    const float U = -1.0f + 2.0f * (static_cast<float>(SX) + 0.5f) / static_cast<float>(SheetRes);
                    SheetDirs[static_cast<std::size_t>(SY) * SheetRes + SX] = DecodeOctahedralUnitVector(U, V);
                }
            }

            for (uint32_t IZ = ZStart; IZ < ZEnd; ++IZ)
            {
                const float PZ = MinB.Z + static_cast<float>(IZ) * Cascade.CellSpacing.Z;
                for (uint32_t IY = 0u; IY < DY; ++IY)
                {
                    const float PY = MinB.Y + static_cast<float>(IY) * Cascade.CellSpacing.Y;
                    for (uint32_t IX = 0u; IX < DX; ++IX)
                    {
                        const float PX = MinB.X + static_cast<float>(IX) * Cascade.CellSpacing.X;
                        const Mathematics::Axis3D LatticePos { PX, PY, PZ };
                        const std::size_t LatticeIdx = (static_cast<std::size_t>(IZ) * DY + IY) * DX + IX;
                        const std::size_t SheetStart = LatticeIdx * SheetRes * SheetRes;

                        // HighEnd: 96 Fibonacci BVH4 rays populate Chebyshev depth moments (Mean, Mean^2) with 0 SDF!
                        if (Settings.HighEndZeroSdfTier && Occluders.SceneBvh != nullptr)
                        {
                            const uint32_t RayCount = std::max(16u, Settings.FibonacciMomentRays);
                            std::vector<float> DepthSum(SheetRes * SheetRes, 0.0f);
                            std::vector<float> DepthSqSum(SheetRes * SheetRes, 0.0f);
                            std::vector<float> WeightSum(SheetRes * SheetRes, 0.0f);

                            const float Golden = 2.39996323f;
                            for (uint32_t R = 0u; R < RayCount; ++R)
                            {
                                const float ZVal  = 1.0f - 2.0f * (static_cast<float>(R) + 0.5f) / static_cast<float>(RayCount);
                                const float RadXY = std::sqrt(std::max(0.0f, 1.0f - ZVal * ZVal));
                                const float Phi   = Golden * static_cast<float>(R);
                                const Mathematics::Axis3D RayDir { RadXY * std::cos(Phi), RadXY * std::sin(Phi), ZVal };

                                SpatialAcceleration::RaySegmentQuery MomentRay {};
                                MomentRay.Origin          = LatticePos;
                                MomentRay.UnitDirection   = RayDir;
                                MomentRay.MinimumDistance = 1.0e-2f;
                                MomentRay.MaximumDistance = 16.0f;
                                const auto Hit = Occluders.SceneBvh->TraceClosestRay(MomentRay, false);
                                const float D  = Hit.HitConfirmed ? Hit.HitDistanceMeters : 16.0f;

                                // Smooth cosine-lobe convolution across 16x16 octahedral sheet eliminates sparse-ray blotches
                                for (std::size_t TIdx = 0u; TIdx < SheetDirs.size(); ++TIdx)
                                {
                                    const float CosA = Mathematics::EvaluateDot(SheetDirs[TIdx], RayDir);
                                    if (CosA <= 0.15f)
                                    {
                                        continue;
                                    }
                                    const float C2   = CosA * CosA;
                                    const float Lobe = C2 * C2 * C2 * C2; // cos^8 directional stencil
                                    DepthSum[TIdx]   += D * Lobe;
                                    DepthSqSum[TIdx] += (D * D) * Lobe;
                                    WeightSum[TIdx]  += Lobe;
                                }
                            }

                            for (std::size_t TIdx = 0u; TIdx < SheetDirs.size(); ++TIdx)
                            {
                                const float W = WeightSum[TIdx];
                                const float MeanD   = (W > 0.0f) ? (DepthSum[TIdx] / W)   : 12.0f;
                                const float MeanSqD = (W > 0.0f) ? (DepthSqSum[TIdx] / W) : 144.0f;
                                Cascade.ChebyshevMoments[(SheetStart + TIdx) * 2u + 0u] = MeanD;
                                Cascade.ChebyshevMoments[(SheetStart + TIdx) * 2u + 1u] = MeanSqD;
                            }
                        }

                        // Accumulate non-emissive bounce surfels into cosine-convolved octahedral irradiance sheet
                        for (const auto& Src : Surfels)
                        {
                            if (Src.EmissiveSurface)
                            {
                                continue; // Near-field emissive handled directly at shading point to preserve sharp luminaire gradients
                            }
                            const Mathematics::Axis3D Delta = Src.Position - LatticePos;
                            const float DistSq = Mathematics::EvaluateLengthSquared(Delta);
                            if (DistSq <= 1.0e-5f) continue;
                            const float InvDist = 1.0f / std::sqrt(DistSq);
                            const Mathematics::Axis3D Wi = Delta * InvDist;
                            const float CosE = std::max(0.0f, Mathematics::EvaluateDot(Src.Normal, Wi * -1.0f));
                            if (CosE <= 0.0f) continue;

                            const float SolidWeight = (Src.AreaMeters * CosE) / (DistSq + Src.AreaMeters / Constants::PiFloat);
                            const float BakeVis     = EvaluateBakeSegmentVisibility(
                                Occluders, LatticePos, Src.Position, Src.RadiusMeters, Settings.HighEndZeroSdfTier);
                            if (BakeVis <= 1.0e-4f) continue;
                            for (std::size_t TIdx = 0u; TIdx < SheetDirs.size(); ++TIdx)
                            {
                                const float CosN = std::max(0.0f, Mathematics::EvaluateDot(SheetDirs[TIdx], Wi));
                                if (CosN <= 0.0f) continue;
                                auto& OutTexel = Cascade.IrradianceSheets[SheetStart + TIdx];
                                const float K  = SolidWeight * CosN * BakeVis;
                                OutTexel.R   += Src.ExitantRadiance.R   * K;
                                OutTexel.G += Src.ExitantRadiance.G * K;
                                OutTexel.B  += Src.ExitantRadiance.B  * K;
                            }
                        }
                    }
                }
            }
        };

        if (ThreadBudget <= 1u)
        {
            PopulateDepthBand(0u, DZ);
        }
        else
        {
            std::vector<std::thread> Threads;
            const uint32_t Chunk = (DZ + ThreadBudget - 1u) / ThreadBudget;
            for (uint32_t W = 0u; W < ThreadBudget; ++W)
            {
                const uint32_t Z0 = W * Chunk;
                const uint32_t Z1 = std::min(DZ, Z0 + Chunk);
                if (Z0 < Z1) Threads.emplace_back([&, Z0, Z1]() { PopulateDepthBand(Z0, Z1); });
            }
            for (auto& T : Threads) T.join();
        }
    };

    const Mathematics::Axis3D InnerMin = Center - HalfSpan * 0.65f;
    const Mathematics::Axis3D InnerMax = Center + HalfSpan * 0.65f;
    const Mathematics::Axis3D OuterMin = SceneMin - Mathematics::Axis3D { 0.35f, 0.35f, 0.20f };
    const Mathematics::Axis3D OuterMax = SceneMax + Mathematics::Axis3D { 0.35f, 0.35f, 0.20f };

    if (Settings.HighEndZeroSdfTier)
    {
        // HighEnd (§4.2): 19x15x6 inner + 9x7x4 outer, 16x16 octahedral sheets, 96 BVH4 Chebyshev rays, 0 SDF!
        SetupCascadeGrid(InnerCascade, 19u, 15u, 6u, 16u, InnerMin, InnerMax);
        SetupCascadeGrid(OuterCascade,  9u,  7u, 4u, 16u, OuterMin, OuterMax);
    }
    else
    {
        // GtxLowEnd (§4.1): 12x10x4 inner + 8x6x3 outer, 8x8 octahedral sheets
        SetupCascadeGrid(InnerCascade, 12u, 10u, 4u, 8u, InnerMin, InnerMax);
        SetupCascadeGrid(OuterCascade,  8u,  6u, 3u, 8u, OuterMin, OuterMax);
    }
}

Mathematics::LinearRGB SurfelRadiosityLatticeSequence::EvaluateTotalIrradiance(
    Mathematics::Axis3D                              ReceiverPos,
    Mathematics::Axis3D                              ReceiverNormal,
    const DirectLighting::HybridOccluderEnvironment& Occluders,
    const SurfelLatticeTierSettings&                 Settings,
    Mathematics::LinearRGB*                          OptionalOutNearEmissiveE,
    Mathematics::LinearRGB*                          OptionalOutCascadeGiE) const noexcept
{
    Mathematics::LinearRGB EmissiveE { 0.0f, 0.0f, 0.0f };
    for (const auto& Src : Surfels)
    {
        if (!Src.EmissiveSurface)
        {
            continue;
        }
        const float FF = EvaluateBunnellDiskFormFactor(
            ReceiverPos, ReceiverNormal, Src.Position, Src.Normal, Src.AreaMeters, Src.TwoSidedSurface);
        if (FF <= 1.0e-5f)
        {
            continue;
        }
        float Vis = 1.0f;
        if (Settings.HighEndZeroSdfTier && Occluders.SceneBvh != nullptr)
        {
            Vis = DirectLighting::EvaluateHighEndBvhLuminaireVisibility(
                *Occluders.SceneBvh, ReceiverPos, ReceiverNormal, Src.Position);
        }
        else
        {
            Vis = DirectLighting::EvaluateGtxHybridLuminaireVisibility(
                Occluders, ReceiverPos, ReceiverNormal, Src.Position, Src.RadiusMeters);
        }
        const float Weight = Constants::PiFloat * FF * Vis;
        EmissiveE.R   += Src.ExitantRadiance.R   * Weight;
        EmissiveE.G += Src.ExitantRadiance.G * Weight;
        EmissiveE.B  += Src.ExitantRadiance.B  * Weight;
    }

    // Smooth C1 blend between InnerCascade and OuterCascade near inner bounds margin
    const Mathematics::Axis3D InnerHalf = (InnerCascade.BoundsMax - InnerCascade.BoundsMin) * 0.5f;
    const Mathematics::Axis3D InnerCtr  = (InnerCascade.BoundsMax + InnerCascade.BoundsMin) * 0.5f;
    const float NormX = std::fabs(ReceiverPos.X - InnerCtr.X) / std::max(1.0e-3f, InnerHalf.X);
    const float NormY = std::fabs(ReceiverPos.Y - InnerCtr.Y) / std::max(1.0e-3f, InnerHalf.Y);
    const float NormZ = std::fabs(ReceiverPos.Z - InnerCtr.Z) / std::max(1.0e-3f, InnerHalf.Z);
    const float MaxN  = std::max({ NormX, NormY, NormZ });
    const float T     = std::clamp((MaxN - 0.72f) / 0.28f, 0.0f, 1.0f);
    const float Blend = T * T * (3.0f - 2.0f * T);

    Mathematics::LinearRGB CascadeE {};
    if (Blend <= 0.001f)
    {
        CascadeE = SampleCascadeSmoothHermite(InnerCascade, ReceiverPos, ReceiverNormal);
    }
    else if (Blend >= 0.999f)
    {
        CascadeE = SampleCascadeSmoothHermite(OuterCascade, ReceiverPos, ReceiverNormal);
    }
    else
    {
        const auto InE  = SampleCascadeSmoothHermite(InnerCascade, ReceiverPos, ReceiverNormal);
        const auto OutE = SampleCascadeSmoothHermite(OuterCascade, ReceiverPos, ReceiverNormal);
        CascadeE = {
            InE.R   * (1.0f - Blend) + OutE.R   * Blend,
            InE.G * (1.0f - Blend) + OutE.G * Blend,
            InE.B  * (1.0f - Blend) + OutE.B  * Blend,
        };
    }

    if (OptionalOutNearEmissiveE != nullptr)
    {
        *OptionalOutNearEmissiveE = EmissiveE;
    }
    if (OptionalOutCascadeGiE != nullptr)
    {
        *OptionalOutCascadeGiE = CascadeE;
    }
    return {
        EmissiveE.R   + CascadeE.R,
        EmissiveE.G + CascadeE.G,
        EmissiveE.B  + CascadeE.B,
    };
}

} // namespace Frontier::GeometricRaster::GlobalIllumination
