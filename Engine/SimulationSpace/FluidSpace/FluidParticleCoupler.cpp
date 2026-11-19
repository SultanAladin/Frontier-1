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

    // Also spawn radial hot blast sparks + cooling ash into EmberSlots so pure blast presets sparkle realistically
    if (Settings.ShowEmbers && Settings.EmberCount > 0u)
    {
        const uint32_t TargetCount = std::min(Settings.EmberCount, 4096u);
        if (EmberSlots.size() < TargetCount)
        {
            EmberSlots.resize(TargetCount);
        }
        const uint32_t BurstSparks = std::min(TargetCount, 180u);
        for (uint32_t I = 0u; I < BurstSparks; ++I)
        {
            const uint32_t SlotIdx = (I * 7u + static_cast<uint32_t>(ElapsedSeconds * 60.0f)) % TargetCount;
            auto& E = EmberSlots[SlotIdx];
            const float Az = NextRand01(RngState) * 2.0f * Constants::PiFloat;
            const float El = 0.08f + 0.92f * NextRand01(RngState);
            const float Er = std::sqrt(std::max(0.0f, 1.0f - El * El));
            const float Spd = Blast.StrengthMps * (0.28f + 0.58f * NextRand01(RngState));
            const float Rad = Blast.RadiusMeters * (0.3f + 0.9f * NextRand01(RngState));

            E.PositionMeters[0] = Blast.CenterWorld.X + std::cos(Az) * Er * Rad;
            E.PositionMeters[1] = Blast.CenterWorld.Y + std::sin(Az) * Er * Rad;
            E.PositionMeters[2] = std::max(0.03f, Blast.CenterWorld.Z + El * Rad);
            E.VelocityMps[0]    = std::cos(Az) * Er * Spd + Blast.DirectionalVelocity.X;
            E.VelocityMps[1]    = std::sin(Az) * Er * Spd + Blast.DirectionalVelocity.Y;
            E.VelocityMps[2]    = El * Spd + Blast.DirectionalVelocity.Z;
            E.AgeSeconds        = NextRand01(RngState) * 0.08f;
            E.LifespanSeconds   = std::max(0.35f, Settings.EmberLifetime * (0.65f + 0.85f * NextRand01(RngState)));
            E.SizeMeters        = 0.012f * std::max(0.2f, Settings.EmberSize) * (0.7f + 0.7f * NextRand01(RngState));
            E.SeedScalar        = NextRand01(RngState);
            E.AuxScalar         = std::max(1.5f, Blast.Temperature * 0.75f);
            E.PackedTraits      = ParticleSpace::ParticleTraitAliveBit;
            E.ColourLinear[0]   = 1.0f;
            E.ColourLinear[1]   = 0.55f;
            E.ColourLinear[2]   = 0.14f;
            E.ColourLinear[3]   = 1.0f;
        }
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
    const FluidSourceSettings&          Emitter,
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
        const float SpanZ  = std::max(0.5f, HeroGrid.BoxMaxWorld.Z - HeroGrid.BoxMinWorld.Z);
        const bool  IsTornado = (Emitter.Swirl >= 3.0f);
        const bool  UseDebrisArray = IsTornado && Settings.EnableTornadoDebrisArray && !Settings.DebrisTypes.empty();
        const float DebrisRatio    = std::clamp(Settings.TornadoDebrisRatio, 0.0f, 1.0f);

        for (uint32_t Idx = 0u; Idx < EmberSlots.size(); ++Idx)
        {
            auto& E = EmberSlots[Idx];
            E.AgeSeconds += Dt;
            const bool NeedRespawn = !ParticleSpace::QueryParticleSlotAlive(E);
            if (NeedRespawn && Emitter.Enabled)
            {
                const float SlotFrac = static_cast<float>(Idx % 20u) / 20.0f;
                const bool IsDebris = UseDebrisArray && (SlotFrac < DebrisRatio);
                const FluidDebrisCategory DebrisCat = IsDebris
                    ? Settings.DebrisTypes[Idx % Settings.DebrisTypes.size()]
                    : FluidDebrisCategory::RockChunk;

                const float Ang = NextRand01(RngState) * 2.0f * Constants::PiFloat;
                // Light paper & leaves ride higher up the funnel; heavy rocks & metal swirl lower/mid-funnel
                float HeightSpan = 0.78f;
                float HeightMin  = 0.03f;
                if (IsDebris)
                {
                    if (DebrisCat == FluidDebrisCategory::PaperSheet || DebrisCat == FluidDebrisCategory::Leaf)
                    {
                        HeightMin  = 0.12f;
                        HeightSpan = 0.78f;
                    }
                    else if (DebrisCat == FluidDebrisCategory::RockChunk || DebrisCat == FluidDebrisCategory::MetalScrap)
                    {
                        HeightMin  = 0.02f;
                        HeightSpan = 0.56f;
                    }
                }
                const float SpawnH = IsTornado
                    ? (HeroGrid.BoxMinWorld.Z + HeightMin + NextRand01(RngState) * SpanZ * HeightSpan)
                    : (FloorZ + NextRand01(RngState) * 0.28f);
                const float H01 = std::clamp((SpawnH - HeroGrid.BoxMinWorld.Z) / SpanZ, 0.0f, 1.0f);
                const float Rad = IsTornado
                    ? ((0.09f + 0.26f * H01) * (0.65f + 0.55f * NextRand01(RngState)))
                    : (Emitter.RadiusMeters * (0.25f + 0.95f * std::sqrt(NextRand01(RngState))));

                E.PositionMeters[0] = Domain.OriginMeters.X + std::cos(Ang) * Rad;
                E.PositionMeters[1] = Domain.OriginMeters.Y + std::sin(Ang) * Rad;
                E.PositionMeters[2] = SpawnH;

                const float TanX = -std::sin(Ang), TanY = std::cos(Ang);
                const float SwirlSpd = IsTornado ? (Emitter.Swirl * (0.58f + 0.35f * NextRand01(RngState))) : (Emitter.Swirl * 0.45f);
                E.VelocityMps[0]    = TanX * SwirlSpd + (NextRand01(RngState) - 0.5f) * 1.1f;
                E.VelocityMps[1]    = TanY * SwirlSpd + (NextRand01(RngState) - 0.5f) * 1.1f;
                E.VelocityMps[2]    = Emitter.UpwardVelocityMps * (0.25f + 0.75f * NextRand01(RngState));
                E.AgeSeconds        = NextRand01(RngState) * 0.15f;
                E.LifespanSeconds   = std::max(0.30f, Settings.EmberLifetime * (0.55f + 0.85f * NextRand01(RngState)));
                E.SeedScalar        = NextRand01(RngState);
                if (IsDebris)
                {
                    float BaseSize = 0.028f;
                    switch (DebrisCat)
                    {
                    case FluidDebrisCategory::RockChunk:    BaseSize = 0.025f; break;
                    case FluidDebrisCategory::TwigStick:    BaseSize = 0.032f; break;
                    case FluidDebrisCategory::Leaf:         BaseSize = 0.027f; break;
                    case FluidDebrisCategory::PaperSheet:   BaseSize = 0.034f; break;
                    case FluidDebrisCategory::WoodSplinter: BaseSize = 0.031f; break;
                    case FluidDebrisCategory::MetalScrap:   BaseSize = 0.026f; break;
                    }
                    E.SizeMeters = BaseSize * std::max(0.4f, Settings.EmberSize) * (0.78f + 0.65f * NextRand01(RngState));
                    E.AuxScalar  = -1.0f - static_cast<float>(static_cast<uint8_t>(DebrisCat)); // Encodes FluidDebrisCategory
                }
                else
                {
                    E.SizeMeters = 0.011f * std::max(0.25f, Settings.EmberSize) * (0.65f + 0.85f * NextRand01(RngState));
                    E.AuxScalar  = (Emitter.Temperature > 0.4f) ? (Emitter.Temperature * (0.55f + 0.6f * NextRand01(RngState))) : 0.0f;
                }
                E.PackedTraits = ParticleSpace::ParticleTraitAliveBit;
            }
            else if (!NeedRespawn)
            {
                const Mathematics::Axis3D Pos { E.PositionMeters[0], E.PositionMeters[1], E.PositionMeters[2] };
                FluidVoxelSample4 FV {}, FT {};
                SampleCombinedFluid(HeroGrid, WorldGrid, Pos, FV, FT);

                const bool IsDebris = (E.AuxScalar < -0.5f);
                const uint32_t CatRaw = IsDebris
                    ? static_cast<uint32_t>(std::clamp(std::lround(-E.AuxScalar - 1.0f), 0l, 5l))
                    : 0u;
                const FluidDebrisCategory DebrisCat = static_cast<FluidDebrisCategory>(CatRaw);

                const float Ph = E.SeedScalar * 43.7f;
                const float Flutter = (IsDebris && (DebrisCat == FluidDebrisCategory::PaperSheet || DebrisCat == FluidDebrisCategory::Leaf)) ? 1.85f : 1.0f;
                const float Jx = (std::sin(ElapsedSeconds * 6.5f + Ph) * 0.95f + std::cos(ElapsedSeconds * 11.3f - Ph * 1.7f) * 0.55f) * Flutter;
                const float Jy = (std::cos(ElapsedSeconds * 5.9f + Ph * 1.3f) * 0.95f - std::sin(ElapsedSeconds * 10.7f + Ph) * 0.55f) * Flutter;
                const float Jz = (std::sin(ElapsedSeconds * 4.8f - Ph * 0.9f) * 0.45f) * Flutter;

                float Drag = 1.0f;
                float GravZ = -1.15f * (0.25f + 0.95f * std::clamp(Settings.EmberAshiness, 0.0f, 1.0f));
                float TargetRBase = 0.09f;
                float TargetRSlope = 0.24f;
                if (IsDebris)
                {
                    switch (DebrisCat)
                    {
                    case FluidDebrisCategory::RockChunk:
                        Drag = 0.30f; GravZ = -3.4f; TargetRBase = 0.13f; TargetRSlope = 0.20f; break;
                    case FluidDebrisCategory::TwigStick:
                        Drag = 0.62f; GravZ = -1.5f; TargetRBase = 0.14f; TargetRSlope = 0.26f; break;
                    case FluidDebrisCategory::Leaf:
                        Drag = 1.15f; GravZ = -0.55f; TargetRBase = 0.15f; TargetRSlope = 0.32f; break;
                    case FluidDebrisCategory::PaperSheet:
                        Drag = 1.28f; GravZ = -0.42f; TargetRBase = 0.16f; TargetRSlope = 0.34f; break;
                    case FluidDebrisCategory::WoodSplinter:
                        Drag = 0.50f; GravZ = -2.0f; TargetRBase = 0.14f; TargetRSlope = 0.24f; break;
                    case FluidDebrisCategory::MetalScrap:
                        Drag = 0.36f; GravZ = -2.9f; TargetRBase = 0.13f; TargetRSlope = 0.22f; break;
                    }
                }

                const float Couple = std::clamp(Settings.AdvectionCoupling * Drag * Dt * 4.2f, 0.0f, 1.0f);
                E.VelocityMps[0] += (FV.X - E.VelocityMps[0]) * Couple + Jx * Dt;
                E.VelocityMps[1] += (FV.Y - E.VelocityMps[1]) * Couple + Jy * Dt;

                // Tornado vortex orbit stabilization for typed debris & funnel particles
                if (IsTornado)
                {
                    const float RDX = E.PositionMeters[0] - Domain.OriginMeters.X;
                    const float RDY = E.PositionMeters[1] - Domain.OriginMeters.Y;
                    const float RXY = std::max(0.03f, std::hypot(RDX, RDY));
                    const float H01 = std::clamp((E.PositionMeters[2] - HeroGrid.BoxMinWorld.Z) / SpanZ, 0.0f, 1.0f);
                    const float TargetR = TargetRBase + H01 * TargetRSlope + 0.04f * std::sin(Ph + ElapsedSeconds * 2.1f);
                    const float TanX = -RDY / RXY, TanY = RDX / RXY;
                    const float RadX =  RDX / RXY, RadY = RDY / RXY;
                    const float RadialSpring = (TargetR - RXY) * 12.5f;
                    const float OrbitBoost   = Emitter.Swirl * (IsDebris ? 0.65f : 0.55f);
                    E.VelocityMps[0] += (TanX * OrbitBoost + RadX * RadialSpring) * Dt;
                    E.VelocityMps[1] += (TanY * OrbitBoost + RadY * RadialSpring) * Dt;
                }

                const float LiftZ = IsDebris
                    ? (FT.X * (DebrisCat == FluidDebrisCategory::PaperSheet || DebrisCat == FluidDebrisCategory::Leaf ? 0.72f : 0.35f))
                    : (FT.Y * Settings.BuoyancyCoupling * 1.45f);
                E.VelocityMps[2] += (FV.Z - E.VelocityMps[2]) * Couple + (LiftZ + GravZ + Jz) * Dt;

                E.PositionMeters[0] += E.VelocityMps[0] * Dt;
                E.PositionMeters[1] += E.VelocityMps[1] * Dt;
                E.PositionMeters[2] += E.VelocityMps[2] * Dt;
                if (E.PositionMeters[2] < HeroGrid.BoxMinWorld.Z + 0.02f)
                {
                    E.PositionMeters[2] = HeroGrid.BoxMinWorld.Z + 0.02f;
                    E.VelocityMps[2]    = std::abs(E.VelocityMps[2]) * 0.25f;
                }

                // Prevent pancake pooling near ceiling or outside active plume
                if (E.PositionMeters[2] > HeroGrid.BoxMinWorld.Z + SpanZ * 0.88f ||
                    (FT.X < 0.025f && FT.Y < 0.025f && E.AgeSeconds > E.LifespanSeconds * 0.35f))
                {
                    E.AgeSeconds += Dt * 1.65f;
                    if (E.AuxScalar > 0.0f) E.AuxScalar *= std::exp(-3.0f * Dt);
                }

                const float Life01 = std::clamp(E.AgeSeconds / std::max(0.05f, E.LifespanSeconds), 0.0f, 1.0f);
                const float Fade = std::sin(Life01 * Constants::PiFloat);
                if (IsDebris)
                {
                    const bool CharredHot = (Emitter.Temperature > 1.2f) && ((Idx % 4u) == 0u) && (Life01 < 0.50f) &&
                                            (DebrisCat == FluidDebrisCategory::PaperSheet || DebrisCat == FluidDebrisCategory::TwigStick || DebrisCat == FluidDebrisCategory::Leaf);
                    if (CharredHot)
                    {
                        const auto Glow = FluidPresets::EvaluateBlackbodyPalette(3.3f * (1.0f - Life01), Shading.TemperatureScale, Shading.ColorPalette);
                        E.ColourLinear[0] = 0.28f + Glow.R * 1.25f;
                        E.ColourLinear[1] = 0.14f + Glow.G * 1.05f;
                        E.ColourLinear[2] = 0.08f + Glow.B * 0.80f;
                    }
                    else
                    {
                        const float V = 0.82f + 0.36f * E.SeedScalar;
                        switch (DebrisCat)
                        {
                        case FluidDebrisCategory::RockChunk:
                            E.ColourLinear[0] = 0.22f * V; E.ColourLinear[1] = 0.21f * V; E.ColourLinear[2] = 0.20f * V; break;
                        case FluidDebrisCategory::TwigStick:
                            E.ColourLinear[0] = 0.34f * V; E.ColourLinear[1] = 0.23f * V; E.ColourLinear[2] = 0.13f * V; break;
                        case FluidDebrisCategory::Leaf:
                            if ((Idx % 3u) == 0u) // Autumn amber/russet leaf
                            {
                                E.ColourLinear[0] = 0.44f * V; E.ColourLinear[1] = 0.26f * V; E.ColourLinear[2] = 0.09f * V;
                            }
                            else // Olive/forest leaf
                            {
                                E.ColourLinear[0] = 0.19f * V; E.ColourLinear[1] = 0.34f * V; E.ColourLinear[2] = 0.12f * V;
                            }
                            break;
                        case FluidDebrisCategory::PaperSheet:
                            E.ColourLinear[0] = 0.72f * V; E.ColourLinear[1] = 0.69f * V; E.ColourLinear[2] = 0.62f * V; break;
                        case FluidDebrisCategory::WoodSplinter:
                            E.ColourLinear[0] = 0.46f * V; E.ColourLinear[1] = 0.33f * V; E.ColourLinear[2] = 0.19f * V; break;
                        case FluidDebrisCategory::MetalScrap:
                            E.ColourLinear[0] = 0.36f * V; E.ColourLinear[1] = 0.39f * V; E.ColourLinear[2] = 0.44f * V; break;
                        }
                    }
                    E.ColourLinear[3] = std::clamp(Fade * 1.25f, 0.0f, 0.92f);
                }
                else
                {
                    E.AuxScalar = std::max(0.0f, E.AuxScalar * std::exp(-0.55f * Dt));
                    const float TempNow = std::max(FT.Y * 0.65f, E.AuxScalar * (1.0f - Life01 * 0.72f));
                    const bool IsAshMote = (Emitter.Temperature < 0.4f) || (E.SeedScalar < Settings.EmberAshiness * 0.75f);
                    if (IsAshMote)
                    {
                        const float AshShade = 0.20f + 0.16f * E.SeedScalar;
                        E.ColourLinear[0] = AshShade;
                        E.ColourLinear[1] = AshShade * 0.96f;
                        E.ColourLinear[2] = AshShade * 0.92f;
                        E.ColourLinear[3] = std::clamp(Fade * 0.68f, 0.0f, 0.65f);
                    }
                    else
                    {
                        // Boosted warm blackbody color so embers stay vivid glowing orange/amber sparks, never dark black dots
                        const auto Glow = FluidPresets::EvaluateBlackbodyPalette(std::max(1.8f, TempNow * 1.35f), Shading.TemperatureScale, Shading.ColorPalette);
                        const float Gain = Settings.EmberIntensity * (1.45f + 0.85f * E.SeedScalar);
                        E.ColourLinear[0] = std::max(0.35f, Glow.R * Gain);
                        E.ColourLinear[1] = std::max(0.10f, Glow.G * Gain);
                        E.ColourLinear[2] = Glow.B * Gain;
                        E.ColourLinear[3] = std::clamp(Fade * 1.35f, 0.0f, 1.0f);
                    }
                }
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
