/*============================================================================================================================================
                                                         FLUIDPARTICLECOUPLER.CPP
============================================================================================================================================*/
// 🧩 Fluid-driven Ember/Ash advection in ParticleSlots & ballistic Shrapnel smoke/fire injection into Fluid grids

#include "Engine/SimulationSpace/FluidSpace/FluidParticleCoupler.h"
#include "Engine/SimulationSpace/FluidSpace/FluidPresets.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::FluidSpace
{

namespace
{

[[nodiscard]] float NextRand01(uint32_t& State) noexcept
{
    State = State * 1664525u + 1013904223u;
    return static_cast<float>((State >> 8u) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

void SampleCombinedFluid(
    const FluidGridVolume&       HeroGrid,
    const FluidWorldDriftVolume& WorldGrid,
    const Mathematics::Axis3D&   WorldPt,
    FluidVoxelSample4&           OutVel,
    FluidVoxelSample4&           OutThermo) noexcept
{
    OutVel    = HeroGrid.SampleWorldTrilinear(HeroGrid.VelocitySolid[HeroGrid.ReadIdx()], WorldPt);
    OutThermo = HeroGrid.SampleWorldTrilinear(HeroGrid.Thermo[HeroGrid.ReadIdx()], WorldPt);
    if (OutThermo.X <= 1.0e-4f && OutThermo.Y <= 1.0e-4f && WorldGrid.Resolution > 0u)
    {
        OutVel    = WorldGrid.SampleWorldTrilinear(WorldGrid.VelocitySolid, WorldPt);
        OutThermo = WorldGrid.SampleWorldTrilinear(WorldGrid.Thermo, WorldPt);
    }
}

} // namespace

void FluidParticleCoupler::Reset() noexcept
{
    EmberSlots.clear();
    ShrapnelSlots.clear();
    RngState = 0x9E3779B9u;
}

void FluidParticleCoupler::SpawnBlastShrapnel(
    const FluidBlastSpecification&       Blast,
    const FluidParticleCouplingSettings& Settings,
    float                                ElapsedSeconds) noexcept
{
    if (!Settings.ShrapnelEnabled) return;

    constexpr uint32_t ShrapnelBurstCount = 24u;
    for (uint32_t I = 0u; I < ShrapnelBurstCount; ++I)
    {
        const float Azimuth   = NextRand01(RngState) * 2.0f * Constants::PiFloat;
        const float ElevCos   = 0.15f + 0.85f * NextRand01(RngState);
        const float ElevSin   = std::sqrt(std::max(0.0f, 1.0f - ElevCos * ElevCos));
        const float Speed     = Blast.StrengthMps * (0.48f + 0.55f * NextRand01(RngState));
        const float VX        = std::cos(Azimuth) * ElevSin * Speed + Blast.DirectionalVelocity.X;
        const float VY        = std::sin(Azimuth) * ElevSin * Speed + Blast.DirectionalVelocity.Y;
        const float VZ        = ElevCos * Speed + Blast.DirectionalVelocity.Z;

        ParticleSpace::ParticleSlot S {};
        S.PositionMeters[0] = Blast.CenterWorld.X;
        S.PositionMeters[1] = Blast.CenterWorld.Y;
        S.PositionMeters[2] = Blast.CenterWorld.Z;
        S.VelocityMps[0]    = VX;
        S.VelocityMps[1]    = VY;
        S.VelocityMps[2]    = VZ;
        S.AgeSeconds        = 0.0f;
        S.LifespanSeconds   = 0.85f + 0.75f * NextRand01(RngState);
        S.SizeMeters        = std::max(0.03f, Blast.RadiusMeters * 0.45f);
        S.SeedScalar        = ElapsedSeconds + static_cast<float>(I) * 0.037f;
        S.AuxScalar         = Blast.Temperature * 0.65f;
        S.PackedTraits      = ParticleSpace::ParticleTraitAliveBit;
        S.ColourLinear[0]   = 1.0f;
        S.ColourLinear[1]   = 0.65f;
        S.ColourLinear[2]   = 0.22f;
        S.ColourLinear[3]   = 1.0f;
        ShrapnelSlots.push_back(S);
    }

    // Bound memory cleanly when repeated blasts occur
    if (ShrapnelSlots.size() > 192u)
    {
        ShrapnelSlots.erase(ShrapnelSlots.begin(), ShrapnelSlots.begin() + static_cast<std::ptrdiff_t>(ShrapnelSlots.size() - 192u));
    }
}

void FluidParticleCoupler::AdvectExternalParticleSlots(
    const FluidGridVolume&                 HeroGrid,
    const FluidWorldDriftVolume&           WorldGrid,
    const FluidParticleCouplingSettings&   Settings,
    float                                  DeltaSeconds,
    std::span<ParticleSpace::ParticleSlot> Slots) noexcept
{
    const float Dt = std::clamp(DeltaSeconds, 0.0f, 0.05f);
    if (Dt <= 0.0f) return;

    for (auto& Slot : Slots)
    {
        if (!ParticleSpace::QueryParticleSlotAlive(Slot)) continue;

        const Mathematics::Axis3D Pos { Slot.PositionMeters[0], Slot.PositionMeters[1], Slot.PositionMeters[2] };
        FluidVoxelSample4 FV {}, FT {};
        SampleCombinedFluid(HeroGrid, WorldGrid, Pos, FV, FT);

        const float Couple = std::clamp(Settings.AdvectionCoupling * Dt * 3.5f, 0.0f, 1.0f);
        Slot.VelocityMps[0] += (FV.X - Slot.VelocityMps[0]) * Couple;
        Slot.VelocityMps[1] += (FV.Y - Slot.VelocityMps[1]) * Couple;
        Slot.VelocityMps[2] += (FV.Z - Slot.VelocityMps[2]) * Couple + FT.Y * Settings.BuoyancyCoupling * 1.4f * Dt;
        Slot.PositionMeters[0] += Slot.VelocityMps[0] * Dt;
        Slot.PositionMeters[1] += Slot.VelocityMps[1] * Dt;
        Slot.PositionMeters[2] += Slot.VelocityMps[2] * Dt;
        Slot.AuxScalar = std::max(Slot.AuxScalar * std::exp(-0.9f * Dt), FT.Y);
    }
}

void FluidParticleCoupler::StepCoupledParticles(
    const FluidDomainSettings&           Domain,
    const FluidEmitterSettings&          Emitter,
    const FluidParticleCouplingSettings& Settings,
    const FluidShadingSettings&          Shading,
    const FluidGridVolume&               HeroGrid,
    const FluidWorldDriftVolume&         WorldGrid,
    float                                DeltaSeconds,
    float                                ElapsedSeconds)
{
    const float Dt = std::clamp(DeltaSeconds, 0.0f, 0.05f);

    // 1. Maintain Ember/Ash ParticleSlots
    if (!Settings.ShowEmbers || Settings.EmberCount == 0u)
    {
        EmberSlots.clear();
    }
    else
    {
        const uint32_t TargetCount = std::min(Settings.EmberCount, 4096u);
        if (EmberSlots.size() != TargetCount)
        {
            EmberSlots.resize(TargetCount);
            for (uint32_t I = 0u; I < TargetCount; ++I)
            {
                EmberSlots[I].AgeSeconds      = -1.0f;
                EmberSlots[I].LifespanSeconds = std::max(0.25f, Settings.EmberLifetime);
                EmberSlots[I].SeedScalar      = NextRand01(RngState);
                EmberSlots[I].PackedTraits    = 0u;
            }
        }

        const float FloorZ = HeroGrid.BoxMinWorld.Z + Emitter.HeightMeters;
        for (auto& E : EmberSlots)
        {
            E.AgeSeconds += Dt;
            const bool NeedRespawn = !ParticleSpace::QueryParticleSlotAlive(E);
            if (NeedRespawn && Emitter.Enabled)
            {
                const float Ang = NextRand01(RngState) * 2.0f * Constants::PiFloat;
                const float Rad = Emitter.RadiusMeters * std::sqrt(NextRand01(RngState));
                E.PositionMeters[0] = Domain.OriginMeters.X + std::cos(Ang) * Rad;
                E.PositionMeters[1] = Domain.OriginMeters.Y + std::sin(Ang) * Rad;
                E.PositionMeters[2] = FloorZ + NextRand01(RngState) * 0.12f;
                E.VelocityMps[0]    = (NextRand01(RngState) - 0.5f) * 0.7f;
                E.VelocityMps[1]    = (NextRand01(RngState) - 0.5f) * 0.7f;
                E.VelocityMps[2]    = Emitter.UpwardVelocityMps * (0.45f + 0.55f * NextRand01(RngState));
                E.AgeSeconds        = NextRand01(RngState) * 0.12f;
                E.LifespanSeconds   = std::max(0.25f, Settings.EmberLifetime * (0.65f + 0.7f * NextRand01(RngState)));
                E.SizeMeters        = 0.014f * std::max(0.2f, Settings.EmberSize);
                E.AuxScalar         = std::max(1.2f, Emitter.Temperature * 0.75f);
                E.PackedTraits      = ParticleSpace::ParticleTraitAliveBit;
            }
            else if (!NeedRespawn)
            {
                const Mathematics::Axis3D Pos { E.PositionMeters[0], E.PositionMeters[1], E.PositionMeters[2] };
                FluidVoxelSample4 FV {}, FT {};
                SampleCombinedFluid(HeroGrid, WorldGrid, Pos, FV, FT);

                const float Couple = std::clamp(Settings.AdvectionCoupling * Dt * 4.5f, 0.0f, 1.0f);
                E.VelocityMps[0] += (FV.X - E.VelocityMps[0]) * Couple + (std::sin(ElapsedSeconds * 5.0f + E.SeedScalar * 31.0f) * 0.45f) * Dt;
                E.VelocityMps[1] += (FV.Y - E.VelocityMps[1]) * Couple + (std::cos(ElapsedSeconds * 4.3f + E.SeedScalar * 19.0f) * 0.45f) * Dt;
                E.VelocityMps[2] += (FV.Z - E.VelocityMps[2]) * Couple + (FT.Y * Settings.BuoyancyCoupling * 2.0f - 0.45f * Settings.EmberAshiness) * Dt;
                E.PositionMeters[0] += E.VelocityMps[0] * Dt;
                E.PositionMeters[1] += E.VelocityMps[1] * Dt;
                E.PositionMeters[2] += E.VelocityMps[2] * Dt;

                const float Life01 = std::clamp(E.AgeSeconds / std::max(0.05f, E.LifespanSeconds), 0.0f, 1.0f);
                const float TempNow = std::max(FT.Y * 0.7f, E.AuxScalar * (1.0f - Life01));
                const auto Glow = FluidPresets::EvaluateBlackbodyPalette(TempNow, Shading.TemperatureScale, Shading.ColorPalette);
                const float Ash  = std::clamp(Settings.EmberAshiness * Life01, 0.0f, 1.0f);
                const float Gain = Settings.EmberIntensity * (1.0f - Ash);
                E.ColourLinear[0] = Glow.R * Gain + 0.14f * Ash;
                E.ColourLinear[1] = Glow.G * Gain + 0.13f * Ash;
                E.ColourLinear[2] = Glow.B * Gain + 0.12f * Ash;
                E.ColourLinear[3] = 1.0f - Life01;
            }
        }
    }

    // 2. Step Ballistic Shrapnel streamers under gravity + drag
    for (auto& S : ShrapnelSlots)
    {
        if (!ParticleSpace::QueryParticleSlotAlive(S)) continue;
        S.AgeSeconds       += Dt;
        S.VelocityMps[2]   -= 9.81f * Dt;
        const float Drag   = std::exp(-0.55f * Dt);
        S.VelocityMps[0]   *= Drag;
        S.VelocityMps[1]   *= Drag;
        S.VelocityMps[2]   *= Drag;
        S.PositionMeters[0] += S.VelocityMps[0] * Dt;
        S.PositionMeters[1] += S.VelocityMps[1] * Dt;
        S.PositionMeters[2] += S.VelocityMps[2] * Dt;
        if (S.PositionMeters[2] < HeroGrid.BoxMinWorld.Z)
        {
            S.PositionMeters[2] = HeroGrid.BoxMinWorld.Z;
            S.VelocityMps[2]    = std::abs(S.VelocityMps[2]) * 0.35f;
            S.VelocityMps[0]   *= 0.7f;
            S.VelocityMps[1]   *= 0.7f;
        }
        if (S.AgeSeconds >= S.LifespanSeconds)
        {
            S.PackedTraits &= ~ParticleSpace::ParticleTraitAliveBit;
        }
    }
}

void FluidParticleCoupler::GatherFluidSplats(std::vector<FluidParticleSplat>& OutSplats) const
{
    OutSplats.clear();
    for (const auto& S : ShrapnelSlots)
    {
        if (!ParticleSpace::QueryParticleSlotAlive(S)) continue;
        const float Life01 = std::clamp(1.0f - S.AgeSeconds / std::max(0.05f, S.LifespanSeconds), 0.0f, 1.0f);
        FluidParticleSplat Sp {};
        Sp.WorldPosition = { S.PositionMeters[0], S.PositionMeters[1], S.PositionMeters[2] };
        Sp.VelocityMps   = { S.VelocityMps[0], S.VelocityMps[1], S.VelocityMps[2] };
        Sp.RadiusMeters  = S.SizeMeters;
        Sp.Temperature   = S.AuxScalar * Life01;
        Sp.Fuel          = 1.35f * Life01;
        Sp.Smoke         = 2.10f * (0.35f + 0.65f * Life01);
        OutSplats.push_back(Sp);
    }
}

} // namespace Frontier::SimulationSpace::FluidSpace
