/*============================================================================================================================================
                                                         FLUIDWORLDGRIDMANAGER.CPP
============================================================================================================================================*/
// 🧩 Implements adaptive explosion ceiling growth & sliding toroidal World Smoke Drift Grid

#include "Engine/SimulationSpace/FluidSpace/FluidWorldGridManager.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::FluidSpace
{

namespace
{

void RemapGridIntoExpandedBounds(
    FluidGridVolume&           Grid,
    const Mathematics::Axis3D& OldMin,
    const Mathematics::Axis3D& OldMax,
    const Mathematics::Axis3D& NewMin,
    const Mathematics::Axis3D& NewMax) noexcept
{
    const uint32_t R = Grid.Resolution;
    if (R == 0u) return;

    const float OldDx = std::max(1.0e-4f, OldMax.X - OldMin.X);
    const float OldDy = std::max(1.0e-4f, OldMax.Y - OldMin.Y);
    const float OldDz = std::max(1.0e-4f, OldMax.Z - OldMin.Z);
    const float NewDx = NewMax.X - NewMin.X;
    const float NewDy = NewMax.Y - NewMin.Y;
    const float NewDz = NewMax.Z - NewMin.Z;

    const uint32_t Rd = Grid.ReadIdx();
    const uint32_t Wr = Grid.WriteIdx();
    const float    FR = static_cast<float>(R);

    for (uint32_t IZ = 0u; IZ < R; ++IZ)
    {
        const float WZ = NewMin.Z + ((static_cast<float>(IZ) + 0.5f) / FR) * NewDz;
        const float UZ = (WZ - OldMin.Z) / OldDz;
        for (uint32_t IY = 0u; IY < R; ++IY)
        {
            const float WY = NewMin.Y + ((static_cast<float>(IY) + 0.5f) / FR) * NewDy;
            const float UY = (WY - OldMin.Y) / OldDy;
            for (uint32_t IX = 0u; IX < R; ++IX)
            {
                const float WX = NewMin.X + ((static_cast<float>(IX) + 0.5f) / FR) * NewDx;
                const float UX = (WX - OldMin.X) / OldDx;
                const uint32_t Idx = FluidGridVolume::FlatIndex(
                    static_cast<int32_t>(IX), static_cast<int32_t>(IY), static_cast<int32_t>(IZ), R);

                if (UX < 0.0f || UX > 1.0f || UY < 0.0f || UY > 1.0f || UZ < 0.0f || UZ > 1.0f)
                {
                    Grid.VelocitySolid[Wr][Idx] = {};
                    Grid.Thermo[Wr][Idx]        = {};
                    Grid.Pressure[Wr][Idx]      = {};
                }
                else
                {
                    const Mathematics::Axis3D OldUvw { UX, UY, UZ };
                    Grid.VelocitySolid[Wr][Idx] = Grid.SampleTrilinear(Grid.VelocitySolid[Rd], OldUvw);
                    Grid.Thermo[Wr][Idx]        = Grid.SampleTrilinear(Grid.Thermo[Rd], OldUvw);
                    Grid.Pressure[Wr][Idx]      = Grid.SampleTrilinear(Grid.Pressure[Rd], OldUvw);
                }
            }
        }
    }

    Grid.SwapPingPong();
}

} // namespace

void FluidWorldGridManager::UpdateAdaptiveHeroBounds(
    const FluidDomainSettings& Domain,
    float                      DynamicSurge01,
    float                      DeltaSeconds,
    FluidGridVolume&           InOutHeroGrid) noexcept
{
    const float MaxMult   = std::max(1.0f, Domain.DynamicBoundsMax);
    const float SurgeMult = Domain.DynamicBounds ? (1.0f + std::clamp(DynamicSurge01, 0.0f, 1.0f) * (MaxMult - 1.0f)) : 1.0f;
    const float BaseW     = std::max(0.4f, Domain.BoundsWidthMeters) * SurgeMult;
    const float BaseH     = std::max(0.4f, Domain.BoundsHeightMeters) * (1.0f + (SurgeMult - 1.0f) * 1.15f);

    // Inspect top 18% of the Hero grid for active smoke/temp/upward velocity; expand height smoothly if needed!
    if (Domain.AdaptiveCeilingEnabled && InOutHeroGrid.Resolution >= 8u)
    {
        const uint32_t R      = InOutHeroGrid.Resolution;
        const uint32_t Rd     = InOutHeroGrid.ReadIdx();
        const uint32_t TopMin = (R * 82u) / 100u;
        float          PeakTopActivity = 0.0f;
        for (uint32_t IZ = TopMin; IZ < R; ++IZ)
        {
            for (uint32_t IY = 0u; IY < R; ++IY)
            {
                for (uint32_t IX = 0u; IX < R; ++IX)
                {
                    const uint32_t Idx = FluidGridVolume::FlatIndex(
                        static_cast<int32_t>(IX), static_cast<int32_t>(IY), static_cast<int32_t>(IZ), R);
                    const auto& Th = InOutHeroGrid.Thermo[Rd][Idx];
                    const auto& Vl = InOutHeroGrid.VelocitySolid[Rd][Idx];
                    const float Activity = Th.X + Th.Y * 0.85f + std::max(0.0f, Vl.Z) * 0.12f;
                    PeakTopActivity = std::max(PeakTopActivity, Activity);
                }
            }
        }

        const float MaxHeadroom = std::max(0.0f, Domain.MaxExplosionCeilingMeters - BaseH);
        if (PeakTopActivity > 0.12f)
        {
            const float GrowRate = std::clamp(PeakTopActivity * 3.5f, 1.2f, 14.0f);
            AdaptiveTopGrowthMeters = std::min(MaxHeadroom, AdaptiveTopGrowthMeters + GrowRate * DeltaSeconds);
        }
        else if (AdaptiveTopGrowthMeters > 0.0f)
        {
            AdaptiveTopGrowthMeters = std::max(0.0f, AdaptiveTopGrowthMeters - 0.35f * DeltaSeconds);
        }
    }
    else
    {
        AdaptiveTopGrowthMeters = 0.0f;
    }

    const Mathematics::Axis3D OldMin = InOutHeroGrid.BoxMinWorld;
    const Mathematics::Axis3D OldMax = InOutHeroGrid.BoxMaxWorld;
    const Mathematics::Axis3D NewMin {
        Domain.OriginMeters.X - 0.5f * BaseW,
        Domain.OriginMeters.Y - 0.5f * BaseW,
        Domain.OriginMeters.Z,
    };
    const Mathematics::Axis3D NewMax {
        Domain.OriginMeters.X + 0.5f * BaseW,
        Domain.OriginMeters.Y + 0.5f * BaseW,
        Domain.OriginMeters.Z + BaseH + AdaptiveTopGrowthMeters,
    };

    const float Diff = std::abs(NewMin.X - OldMin.X) + std::abs(NewMax.X - OldMax.X) +
                       std::abs(NewMin.Y - OldMin.Y) + std::abs(NewMax.Y - OldMax.Y) +
                       std::abs(NewMin.Z - OldMin.Z) + std::abs(NewMax.Z - OldMax.Z);

    if (Diff > 0.08f && InOutHeroGrid.Resolution > 0u)
    {
        RemapGridIntoExpandedBounds(InOutHeroGrid, OldMin, OldMax, NewMin, NewMax);
        InOutHeroGrid.BoxMinWorld = NewMin;
        InOutHeroGrid.BoxMaxWorld = NewMax;
    }
    else if (InOutHeroGrid.BoxMaxWorld.X <= InOutHeroGrid.BoxMinWorld.X)
    {
        InOutHeroGrid.BoxMinWorld = NewMin;
        InOutHeroGrid.BoxMaxWorld = NewMax;
    }
}

void FluidWorldGridManager::StepWorldDriftExchange(
    const FluidDomainSettings&     Domain,
    const FluidCombustionSettings& Combustion,
    const Mathematics::Axis3D&     FocusWorldPoint,
    float                          DeltaSeconds,
    const FluidGridVolume&         HeroGrid,
    FluidWorldDriftVolume&         InOutWorldGrid) noexcept
{
    if (!Domain.WorldGridEnabled || InOutWorldGrid.Resolution == 0u)
    {
        return;
    }

    const uint32_t WR       = InOutWorldGrid.Resolution;
    const float    CellSize = InOutWorldGrid.SpanMeters / static_cast<float>(WR);

    // Slide the integer toroidal grid window if focus moved by >= 1 world cell (clearing only newly exposed inflow faces)
    const int32_t ShiftX = static_cast<int32_t>(std::trunc((FocusWorldPoint.X - InOutWorldGrid.CenterWorld.X) / CellSize));
    const int32_t ShiftY = static_cast<int32_t>(std::trunc((FocusWorldPoint.Y - InOutWorldGrid.CenterWorld.Y) / CellSize));
    if (ShiftX != 0 || ShiftY != 0)
    {
        const int32_t OldShiftX = InOutWorldGrid.ToroidalShift[0];
        const int32_t OldShiftY = InOutWorldGrid.ToroidalShift[1];
        InOutWorldGrid.ToroidalShift[0] += ShiftX;
        InOutWorldGrid.ToroidalShift[1] += ShiftY;
        InOutWorldGrid.CenterWorld.X    += static_cast<float>(ShiftX) * CellSize;
        InOutWorldGrid.CenterWorld.Y    += static_cast<float>(ShiftY) * CellSize;

        // Clear newly uncovered perimeter slabs so old wrapped smoke never reappears from behind
        for (uint32_t IZ = 0u; IZ < WR; ++IZ)
        {
            for (uint32_t IY = 0u; IY < WR; ++IY)
            {
                for (uint32_t IX = 0u; IX < WR; ++IX)
                {
                    const bool ClearX = (ShiftX > 0 && static_cast<int32_t>(IX) >= static_cast<int32_t>(WR) - ShiftX) ||
                                        (ShiftX < 0 && static_cast<int32_t>(IX) < -ShiftX);
                    const bool ClearY = (ShiftY > 0 && static_cast<int32_t>(IY) >= static_cast<int32_t>(WR) - ShiftY) ||
                                        (ShiftY < 0 && static_cast<int32_t>(IY) < -ShiftY);
                    if (ClearX || ClearY)
                    {
                        const uint32_t Idx = InOutWorldGrid.FlatIndexWrapped(
                            static_cast<int32_t>(IX), static_cast<int32_t>(IY), static_cast<int32_t>(IZ));
                        InOutWorldGrid.Thermo[Idx]        = {};
                        InOutWorldGrid.VelocitySolid[Idx] = {};
                    }
                }
            }
        }
        (void)OldShiftX;
        (void)OldShiftY;
    }

    // MacCormack-corrected advection on World Smoke Grid + deposit smoke from Hero grid periphery/interior
    const float Half = 0.5f * InOutWorldGrid.SpanMeters;
    const float Decay = std::exp(-std::max(0.005f, Combustion.SmokeDissipation * 0.20f) * DeltaSeconds);
    const float Cool  = std::exp(-std::max(0.04f, Combustion.CoolingRate * 0.45f) * DeltaSeconds);
    const std::vector<FluidVoxelSample4> PrevThermo = InOutWorldGrid.Thermo;
    const std::vector<FluidVoxelSample4> PrevVel    = InOutWorldGrid.VelocitySolid;

    std::vector<FluidVoxelSample4> PredThermo(PrevThermo.size());
    const uint32_t HRd = HeroGrid.ReadIdx();

    // Pass 1: Forward Semi-Lagrangian estimate
    for (uint32_t IZ = 0u; IZ < WR; ++IZ)
    {
        const float WZ = (InOutWorldGrid.CenterWorld.Z - Half) + (static_cast<float>(IZ) + 0.5f) * CellSize;
        for (uint32_t IY = 0u; IY < WR; ++IY)
        {
            const float WY = (InOutWorldGrid.CenterWorld.Y - Half) + (static_cast<float>(IY) + 0.5f) * CellSize;
            for (uint32_t IX = 0u; IX < WR; ++IX)
            {
                const float WX = (InOutWorldGrid.CenterWorld.X - Half) + (static_cast<float>(IX) + 0.5f) * CellSize;
                const uint32_t Idx = InOutWorldGrid.FlatIndexWrapped(
                    static_cast<int32_t>(IX), static_cast<int32_t>(IY), static_cast<int32_t>(IZ));

                const auto& CurV = PrevVel[Idx];
                const float VX = (std::abs(CurV.X) > 1.0e-3f) ? CurV.X : Combustion.WindVelocityXY.X;
                const float VY = (std::abs(CurV.Y) > 1.0e-3f) ? CurV.Y : Combustion.WindVelocityXY.Y;
                const float VZ = CurV.Z;

                const Mathematics::Axis3D BackWorld { WX - VX * DeltaSeconds, WY - VY * DeltaSeconds, WZ - VZ * DeltaSeconds };
                PredThermo[Idx] = InOutWorldGrid.SampleWorldTrilinear(PrevThermo, BackWorld);
            }
        }
    }

    // Pass 2: MacCormack error correction + Hero-to-World handoff
    for (uint32_t IZ = 0u; IZ < WR; ++IZ)
    {
        const float WZ = (InOutWorldGrid.CenterWorld.Z - Half) + (static_cast<float>(IZ) + 0.5f) * CellSize;
        for (uint32_t IY = 0u; IY < WR; ++IY)
        {
            const float WY = (InOutWorldGrid.CenterWorld.Y - Half) + (static_cast<float>(IY) + 0.5f) * CellSize;
            for (uint32_t IX = 0u; IX < WR; ++IX)
            {
                const float WX = (InOutWorldGrid.CenterWorld.X - Half) + (static_cast<float>(IX) + 0.5f) * CellSize;
                const uint32_t Idx = InOutWorldGrid.FlatIndexWrapped(
                    static_cast<int32_t>(IX), static_cast<int32_t>(IY), static_cast<int32_t>(IZ));

                const auto& CurV = PrevVel[Idx];
                const float VX = (std::abs(CurV.X) > 1.0e-3f) ? CurV.X : Combustion.WindVelocityXY.X;
                const float VY = (std::abs(CurV.Y) > 1.0e-3f) ? CurV.Y : Combustion.WindVelocityXY.Y;
                const float VZ = CurV.Z;

                const Mathematics::Axis3D BackWorld { WX - VX * DeltaSeconds, WY - VY * DeltaSeconds, WZ - VZ * DeltaSeconds };
                const Mathematics::Axis3D FwdWorld  { WX + VX * DeltaSeconds, WY + VY * DeltaSeconds, WZ + VZ * DeltaSeconds };
                const auto& Pred = PredThermo[Idx];
                const auto  Rev  = InOutWorldGrid.SampleWorldTrilinear(PredThermo, FwdWorld);
                const auto& CurT = PrevThermo[Idx];

                FluidVoxelSample4 AdvT {
                    std::max(0.0f, Pred.X + 0.5f * (CurT.X - Rev.X)),
                    std::max(0.0f, Pred.Y + 0.5f * (CurT.Y - Rev.Y)),
                    std::max(0.0f, Pred.Z + 0.5f * (CurT.Z - Rev.Z)),
                    0.0f,
                };
                // Safe MacCormack bounds around BackWorld sample to preserve sharp smoke wisps without overshoot
                const float MaxS = std::max(CurT.X, Pred.X) * 1.15f + 0.02f;
                const float MaxT = std::max(CurT.Y, Pred.Y) * 1.15f + 0.02f;
                AdvT.X = std::clamp(AdvT.X, 0.0f, MaxS) * Decay;
                AdvT.Y = std::clamp(AdvT.Y, 0.0f, MaxT) * Cool;
                AdvT.Z *= Cool;

                auto AdvV = InOutWorldGrid.SampleWorldTrilinear(PrevVel, BackWorld);
                const float EddiesY = std::sin(WX * 1.8f + WZ * 2.1f) * 0.65f;
                const float EddiesZ = std::cos(WX * 1.4f - WY * 1.7f) * 0.55f;
                AdvV.X += (Combustion.WindVelocityXY.X - AdvV.X) * std::min(1.0f, DeltaSeconds * 1.45f);
                AdvV.Y += (Combustion.WindVelocityXY.Y + EddiesY - AdvV.Y) * std::min(1.0f, DeltaSeconds * 1.35f);
                AdvV.Z += (AdvT.Y * Combustion.Buoyancy * 0.28f + AdvT.X * (0.45f + EddiesZ) - AdvT.X * Combustion.SmokeWeight * 0.05f) * DeltaSeconds;

                // Blend outgoing/rising Hero smoke into the World Drift grid so plumes continue seamlessly beyond Hero bounds
                const auto HeroTh = HeroGrid.SampleWorldTrilinear(HeroGrid.Thermo[HRd], { WX, WY, WZ });
                if (HeroTh.X > 0.008f || HeroTh.Y > 0.008f)
                {
                    const auto HeroVl = HeroGrid.SampleWorldTrilinear(HeroGrid.VelocitySolid[HRd], { WX, WY, WZ });
                    AdvT.X = std::max(AdvT.X, HeroTh.X * 1.10f);
                    AdvT.Y = std::max(AdvT.Y, HeroTh.Y * 0.72f);
                    AdvV.X = 0.45f * AdvV.X + 0.55f * HeroVl.X;
                    AdvV.Y = 0.45f * AdvV.Y + 0.55f * HeroVl.Y;
                    AdvV.Z = 0.45f * AdvV.Z + 0.55f * HeroVl.Z;
                }

                InOutWorldGrid.Thermo[Idx]        = AdvT;
                InOutWorldGrid.VelocitySolid[Idx] = AdvV;
            }
        }
    }
}

} // namespace Frontier::SimulationSpace::FluidSpace
