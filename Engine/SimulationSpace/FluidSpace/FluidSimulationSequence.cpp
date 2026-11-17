/*============================================================================================================================================
                                                        FLUIDSIMULATIONSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Fixed-step +Z-Up 3D Eulerian Fire/Smoke solver with adaptive headroom, sliding World Drift Grid, ForceFields, Particles & GI coupling

#include "Engine/SimulationSpace/FluidSpace/FluidSimulationSequence.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/SimulationSpace/FluidSpace/FluidPresets.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::FluidSpace
{

namespace
{

[[nodiscard]] float SmoothStepF(float E0, float E1, float X) noexcept
{
    const float T = std::clamp((X - E0) / std::max(1.0e-5f, E1 - E0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] float Hash3Scalar(int32_t X, int32_t Y, int32_t Z) noexcept
{
    uint32_t N = static_cast<uint32_t>(X * 374761393 + Y * 668265263 + Z * 2147483647);
    N = (N ^ (N >> 13u)) * 1274126177u;
    N = N ^ (N >> 16u);
    return static_cast<float>(N) * (1.0f / 4294967296.0f) * 2.0f - 1.0f;
}

[[nodiscard]] float ValueNoise3(float X, float Y, float Z) noexcept
{
    const int32_t IX = static_cast<int32_t>(std::floor(X));
    const int32_t IY = static_cast<int32_t>(std::floor(Y));
    const int32_t IZ = static_cast<int32_t>(std::floor(Z));
    const float FX = X - static_cast<float>(IX);
    const float FY = Y - static_cast<float>(IY);
    const float FZ = Z - static_cast<float>(IZ);
    const float UX = FX * FX * (3.0f - 2.0f * FX);
    const float UY = FY * FY * (3.0f - 2.0f * FY);
    const float UZ = FZ * FZ * (3.0f - 2.0f * FZ);
    const auto Mix = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };

    const float N000 = Hash3Scalar(IX, IY, IZ),         N100 = Hash3Scalar(IX + 1, IY, IZ);
    const float N010 = Hash3Scalar(IX, IY + 1, IZ),     N110 = Hash3Scalar(IX + 1, IY + 1, IZ);
    const float N001 = Hash3Scalar(IX, IY, IZ + 1),     N101 = Hash3Scalar(IX + 1, IY, IZ + 1);
    const float N011 = Hash3Scalar(IX, IY + 1, IZ + 1), N111 = Hash3Scalar(IX + 1, IY + 1, IZ + 1);

    return Mix(
        Mix(Mix(N000, N100, UX), Mix(N010, N110, UX), UY),
        Mix(Mix(N001, N101, UX), Mix(N011, N111, UX), UY),
        UZ);
}

[[nodiscard]] float Fbm2Noise3(float X, float Y, float Z) noexcept
{
    return 0.65f * ValueNoise3(X, Y, Z) + 0.35f * ValueNoise3(X * 2.13f + 7.3f, Y * 2.13f - 4.1f, Z * 2.13f + 5.7f);
}

[[nodiscard]] Mathematics::Axis3D CurlNoise3(float X, float Y, float Z) noexcept
{
    constexpr float Eps = 0.25f;
    const float N1 = ValueNoise3(X, Y + Eps, Z) - ValueNoise3(X, Y - Eps, Z);
    const float N2 = ValueNoise3(X, Y, Z + Eps) - ValueNoise3(X, Y, Z - Eps);
    const float N3 = ValueNoise3(X + Eps, Y, Z) - ValueNoise3(X - Eps, Y, Z);
    return { (N1 - N2) / (2.0f * Eps), (N2 - N3) / (2.0f * Eps), (N3 - N1) / (2.0f * Eps) };
}

[[nodiscard]] Mathematics::Axis3D CurlFbm2(float X, float Y, float Z) noexcept
{
    const auto C0 = CurlNoise3(X, Y, Z);
    const auto C1 = CurlNoise3(X * 2.25f + 11.4f, Y * 2.25f + 3.2f, Z * 2.25f - 6.8f);
    return { C0.X + 0.55f * C1.X, C0.Y + 0.55f * C1.Y, C0.Z + 0.55f * C1.Z };
}

[[nodiscard]] float HgPhase(float CosTheta, float G) noexcept
{
    const float G2 = G * G;
    const float Denom = std::max(1.0e-4f, std::pow(1.0f + G2 - 2.0f * G * CosTheta, 1.5f));
    return (1.0f - G2) / (4.0f * Constants::PiFloat * Denom);
}

[[nodiscard]] float DualHgPhase(float CosTheta, float G) noexcept
{
    return 0.28f * HgPhase(CosTheta, -0.25f) + 0.72f * HgPhase(CosTheta, G);
}

[[nodiscard]] bool IntersectBox(
    const Mathematics::Axis3D& Ro,
    const Mathematics::Axis3D& Rd,
    const Mathematics::Axis3D& BMin,
    const Mathematics::Axis3D& BMax,
    float&                     OutTNear,
    float&                     OutTFar) noexcept
{
    const auto SafeInv = [](float V) noexcept -> float { return (std::abs(V) > 1.0e-7f) ? (1.0f / V) : ((V >= 0.0f) ? 1.0e7f : -1.0e7f); };
    const float IX = SafeInv(Rd.X), IY = SafeInv(Rd.Y), IZ = SafeInv(Rd.Z);
    const float TX0 = (BMin.X - Ro.X) * IX, TX1 = (BMax.X - Ro.X) * IX;
    const float TY0 = (BMin.Y - Ro.Y) * IY, TY1 = (BMax.Y - Ro.Y) * IY;
    const float TZ0 = (BMin.Z - Ro.Z) * IZ, TZ1 = (BMax.Z - Ro.Z) * IZ;
    OutTNear = std::max({ std::min(TX0, TX1), std::min(TY0, TY1), std::min(TZ0, TZ1) });
    OutTFar  = std::min({ std::max(TX0, TX1), std::max(TY0, TY1), std::max(TZ0, TZ1) });
    return OutTFar > std::max(0.0f, OutTNear);
}

} // namespace

FluidSimulationSequence::FluidSimulationSequence()
{
    Configure(FluidPresets::ConstructPreset("ue5_pyro_default"));
}

void FluidSimulationSequence::Configure(const FluidSceneDocument& NewDocument)
{
    const uint32_t OldRes = HeroGrid.Resolution;
    const uint32_t NewRes = std::clamp(NewDocument.Domain.GridResolution, 16u, 128u);
    const uint32_t OldWRes = WorldGrid.Resolution;
    const uint32_t NewWRes = std::clamp(NewDocument.Domain.WorldGridResolution, 16u, 64u);
    const bool     ObstacleChanged =
        (NewDocument.Obstacle.Type != Document.Obstacle.Type) ||
        (NewDocument.Obstacle.RadiusMeters != Document.Obstacle.RadiusMeters) ||
        (NewDocument.Obstacle.CenterMeters.X != Document.Obstacle.CenterMeters.X) ||
        (NewDocument.Obstacle.CenterMeters.Y != Document.Obstacle.CenterMeters.Y) ||
        (NewDocument.Obstacle.CenterMeters.Z != Document.Obstacle.CenterMeters.Z);

    Document = NewDocument;

    if (OldRes != NewRes)
    {
        HeroGrid.Allocate(NewRes);
        WorldManager = FluidWorldGridManager {};
        WorldManager.UpdateAdaptiveHeroBounds(Document.Domain, 0.0f, 0.0f, HeroGrid);
        ObstaclesBakedOnce = false;
        ElapsedSeconds = 0.0f;
        StepCount      = 0ull;
    }

    if (WorldGrid.Thermo.empty() || OldWRes != NewWRes || WorldGrid.SpanMeters != Document.Domain.WorldGridSpanMeters)
    {
        const Mathematics::Axis3D WorldCenter {
            Document.Domain.OriginMeters.X,
            Document.Domain.OriginMeters.Y,
            Document.Domain.OriginMeters.Z + 0.30f * Document.Domain.WorldGridSpanMeters,
        };
        WorldGrid.Allocate(NewWRes, Document.Domain.WorldGridSpanMeters, WorldCenter);
    }

    if (ObstacleChanged)
    {
        ObstaclesBakedOnce = false;
    }
}

void FluidSimulationSequence::ResetSimulation()
{
    HeroGrid.ClearState();
    WorldGrid.ClearState();
    WorldManager = FluidWorldGridManager {};
    WorldManager.UpdateAdaptiveHeroBounds(Document.Domain, 0.0f, 0.0f, HeroGrid);
    ParticleCoupler.Reset();
    PendingBlasts.clear();
    PendingBrushes.clear();
    PendingSplats.clear();
    EmissiveLights.clear();
    ObstaclesBakedOnce = false;
    ElapsedSeconds     = 0.0f;
    StepCount          = 0ull;
    DynamicSurge01     = 0.0f;
    DynamicTarget01    = 0.0f;
    ShockwaveAge       = 99.0f;
}

void FluidSimulationSequence::TriggerBlast(
    const FluidBlastSpecification& Blast,
    const FluidStepContext&        Context)
{
    PendingBlasts.push_back(Blast);
    DynamicTarget01 = 1.0f;
    ShockwaveAge    = 0.0f;
    ShockwaveCenter = Blast.CenterWorld;
    ParticleCoupler.SpawnBlastShrapnel(Blast, Document.Particles, ElapsedSeconds);
    if (Context.MutableForceFields != nullptr)
    {
        FluidForceCoupler::EmitBlastForceImpulse(Blast, static_cast<double>(ElapsedSeconds), *Context.MutableForceFields);
    }
}

void FluidSimulationSequence::ApplyPyroBrush(
    const Mathematics::Axis3D& WorldPosition,
    const Mathematics::Axis3D& BrushVelocityMps,
    float                      BrushRadiusMeters,
    float                      Intensity)
{
    PendingBrushes.push_back({ WorldPosition, BrushVelocityMps, std::max(0.03f, BrushRadiusMeters), std::max(0.0f, Intensity) });
}

void FluidSimulationSequence::StepSimulation(
    float                   DeltaSeconds,
    const FluidStepContext& Context)
{
    if (Document.Domain.Paused || DeltaSeconds <= 0.0f)
    {
        return;
    }

    const float Dt = std::clamp(DeltaSeconds * std::max(0.05f, Document.Domain.TimeScale), 1.0e-4f, 0.033f);
    ElapsedSeconds += Dt;
    ShockwaveAge   += Dt;
    ++StepCount;

    // 1. Dynamic blast surge + adaptive uncapped vertical explosion ceiling
    if (DynamicTarget01 > DynamicSurge01)
    {
        DynamicSurge01 = std::min(1.0f, DynamicSurge01 + Dt * 3.8f);
        if (DynamicSurge01 >= 0.99f) DynamicTarget01 = 0.0f;
    }
    else
    {
        DynamicSurge01 = std::max(0.0f, DynamicSurge01 - Dt * 0.22f);
    }
    WorldManager.UpdateAdaptiveHeroBounds(Document.Domain, DynamicSurge01, Dt, HeroGrid);

    // 2. Bake GI SignedDistanceVolume / WideBoundingTree / Analytical obstacle occupancy
    if (!ObstaclesBakedOnce || Document.Obstacle.AutoMove || Context.BakeGiObstaclesNow)
    {
        FluidObstacleVoxelizer::BakeSceneGiCollisionMask(
            Context.SceneSdf,
            Context.SceneTree,
            Document.Obstacle,
            ElapsedSeconds,
            HeroGrid);
        ObstaclesBakedOnce = true;
    }

    // 3. Gather ballistic shrapnel particle splats before solver step
    ParticleCoupler.GatherFluidSplats(PendingSplats);

    // 4. Eulerian 4-stage Pyro Step
    ExecuteSplatAndObstacles(Dt, Context);
    ExecuteMacCormackAdvection(Dt);
    ExecuteVorticityAndCombustion(Dt, Context);
    ExecuteDivergenceAndPressure(Dt);

    // 5. Continuous sliding World Smoke Grid exchange
    const Mathematics::Axis3D DriftFocus {
        Context.WorldFocusPoint.X,
        Context.WorldFocusPoint.Y,
        Document.Domain.OriginMeters.Z + 0.5f * Document.Domain.WorldGridSpanMeters,
    };
    WorldManager.StepWorldDriftExchange(Document.Domain, Document.Combustion, DriftFocus, Dt, HeroGrid, WorldGrid);

    // 6. Advance internal embers/shrapnel + any external ParticleSpace slots
    ParticleCoupler.StepCoupledParticles(
        Document.Domain, Document.Emitter, Document.Particles, Document.Shading,
        HeroGrid, WorldGrid, Dt, ElapsedSeconds);

    if (!Context.ExternalParticles.empty())
    {
        FluidParticleCoupler::AdvectExternalParticleSlots(
            HeroGrid, WorldGrid, Document.Particles, Dt, Context.ExternalParticles);
    }

    // 7. Extract dynamic blackbody fire lights for GI lighting
    FluidGiLightExtractor::ExtractEmissiveLights(HeroGrid, Document.Shading, Document.GiCoupling, EmissiveLights);
}

void FluidSimulationSequence::ExecuteSplatAndObstacles(float Dt, const FluidStepContext& /*Context*/)
{
    const uint32_t R  = HeroGrid.Resolution;
    const uint32_t Rd = HeroGrid.ReadIdx();
    const float    FR = static_cast<float>(R);
    const Mathematics::Axis3D Span = HeroGrid.BoxMaxWorld - HeroGrid.BoxMinWorld;

    const Mathematics::Axis3D EmitCenter {
        Document.Domain.OriginMeters.X,
        Document.Domain.OriginMeters.Y,
        HeroGrid.BoxMinWorld.Z + Document.Emitter.HeightMeters,
    };

    for (uint32_t IZ = 0u; IZ < R; ++IZ)
    {
        const float WZ = HeroGrid.BoxMinWorld.Z + ((static_cast<float>(IZ) + 0.5f) / FR) * Span.Z;
        for (uint32_t IY = 0u; IY < R; ++IY)
        {
            const float WY = HeroGrid.BoxMinWorld.Y + ((static_cast<float>(IY) + 0.5f) / FR) * Span.Y;
            for (uint32_t IX = 0u; IX < R; ++IX)
            {
                const float WX = HeroGrid.BoxMinWorld.X + ((static_cast<float>(IX) + 0.5f) / FR) * Span.X;
                const uint32_t Idx = FluidGridVolume::FlatIndex(
                    static_cast<int32_t>(IX), static_cast<int32_t>(IY), static_cast<int32_t>(IZ), R);

                auto Vel = HeroGrid.VelocitySolid[Rd][Idx];
                auto Th  = HeroGrid.Thermo[Rd][Idx];

                if (HeroGrid.SceneSolidMask[Idx] > 0.5f)
                {
                    HeroGrid.VelocitySolid[Rd][Idx] = { 0.0f, 0.0f, 0.0f, 1.0f };
                    HeroGrid.Thermo[Rd][Idx]        = { 0.0f, 0.0f, 0.0f, 0.0f };
                    continue;
                }
                Vel.W = 0.0f;

                // Continuous Emitter (+Z up jet with multi-octave flickering flame tongues & optional Rankine tornado vortex)
                if (Document.Emitter.Enabled && Document.Emitter.Rate > 1.0e-3f)
                {
                    const bool  IsTornado = (Document.Emitter.Swirl >= 3.0f);
                    const float DX = WX - EmitCenter.X;
                    const float DY = WY - EmitCenter.Y;
                    const float HeightAbove = std::max(0.0f, WZ - EmitCenter.Z);
                    const float FunnelH = IsTornado ? std::min(1.0f, HeightAbove / std::max(0.5f, Span.Z * 0.65f)) : 0.0f;
                    const float DZ = IsTornado
                        ? ((WZ < EmitCenter.Z) ? (EmitCenter.Z - WZ) * 2.0f : HeightAbove * 0.28f)
                        : ((WZ - EmitCenter.Z) * 1.45f);
                    const float RawDist = IsTornado
                        ? std::sqrt(DX * DX + DY * DY + DZ * DZ * 0.35f)
                        : std::sqrt(DX * DX + DY * DY + DZ * DZ);
                    const float EmitR = std::max(0.04f, Document.Emitter.RadiusMeters) * (IsTornado ? (1.0f + FunnelH * 0.75f) : 1.0f);
                    const float Fbm = Fbm2Noise3(
                        WX * 8.5f + ElapsedSeconds * 1.6f,
                        WY * 8.5f - ElapsedSeconds * 1.3f,
                        WZ * 7.5f - ElapsedSeconds * 4.8f);
                    const float WarpedDist = RawDist * (1.0f + 0.28f * Fbm);
                    if (WarpedDist < EmitR * 1.55f)
                    {
                        const float HeightFade = IsTornado ? (1.0f - 0.62f * FunnelH) : 1.0f;
                        const float Flicker = std::max(0.15f, 0.85f + 0.52f * Fbm);
                        const float Falloff = std::exp(-(WarpedDist * WarpedDist) / (EmitR * EmitR * 0.42f)) *
                                              Document.Emitter.Rate * Flicker * HeightFade;
                        const float XYDist  = std::max(1.0e-4f, std::hypot(DX, DY));
                        const float TanX    = -DY / XYDist;
                        const float TanY    =  DX / XYDist;
                        const float RadX    =  DX / XYDist;
                        const float RadY    =  DY / XYDist;

                        const float RadialBias = IsTornado ? -0.85f : 0.35f;
                        const float TargetVX = TanX * Document.Emitter.Swirl + RadX * RadialBias + Fbm * 0.9f;
                        const float TargetVY = TanY * Document.Emitter.Swirl + RadY * RadialBias - Fbm * 0.8f;
                        const float TargetVZ = Document.Emitter.UpwardVelocityMps * (IsTornado ? 1.25f : 1.0f) * (0.82f + 0.42f * std::max(0.0f, Fbm));
                        const float Blend    = std::clamp(Falloff * Dt * 11.0f, 0.0f, 0.85f);

                        Vel.X += (TargetVX - Vel.X) * Blend;
                        Vel.Y += (TargetVY - Vel.Y) * Blend;
                        Vel.Z += (TargetVZ - Vel.Z) * Blend;

                        Th.X = std::max(Th.X, Document.Emitter.Smoke * Falloff * (0.6f + 0.4f * std::max(0.0f, Fbm)));
                        Th.Y = std::max(Th.Y, Document.Emitter.Temperature * Falloff);
                        Th.Z = std::max(Th.Z, Document.Emitter.Fuel * Falloff);
                    }
                }

                // Triggered Detonation Blasts (Multi-lobe Rayleigh-Taylor fireball with crisp FBM turbulence)
                for (const auto& B : PendingBlasts)
                {
                    const float DX = WX - B.CenterWorld.X;
                    const float DY = WY - B.CenterWorld.Y;
                    const float DZ = WZ - B.CenterWorld.Z;
                    const float D  = std::sqrt(DX * DX + DY * DY + DZ * DZ);
                    const float RMax = std::max(0.06f, B.RadiusMeters) * 2.45f;
                    if (D < RMax)
                    {
                        const float Theta = std::atan2(DY, DX);
                        const float Phi   = std::acos(std::clamp(DZ / std::max(1.0e-4f, D), -1.0f, 1.0f));
                        const float NoiseWarp = Fbm2Noise3(WX * 10.5f + B.Seed, WY * 10.5f - B.Seed, WZ * 10.5f);
                        const float Lobe  = 1.0f + 0.38f * std::sin(Theta * B.Lobes + B.Seed + NoiseWarp * 1.4f) *
                                                   std::cos(Phi * B.Lobes * 0.7f) + 0.22f * NoiseWarp;
                        const float W     = std::exp(-(D * D) / (B.RadiusMeters * B.RadiusMeters * 0.72f)) * std::max(0.18f, Lobe);
                        const float InvD  = 1.0f / std::max(1.0e-4f, D);
                        Vel.X += (DX * InvD * B.StrengthMps + B.DirectionalVelocity.X + NoiseWarp * 2.2f) * W;
                        Vel.Y += (DY * InvD * B.StrengthMps + B.DirectionalVelocity.Y - NoiseWarp * 2.2f) * W;
                        Vel.Z += (DZ * InvD * B.StrengthMps + B.DirectionalVelocity.Z) * W;
                        Th.X  = std::min(14.0f, Th.X + B.Smoke * W);
                        Th.Y  = std::min(14.0f, Th.Y + B.Temperature * W);
                        Th.Z  = std::min(14.0f, Th.Z + B.Fuel * W);
                    }
                }

                // Interactive 3D Pyro Brush
                for (const auto& Br : PendingBrushes)
                {
                    const float DX = WX - Br.Position.X;
                    const float DY = WY - Br.Position.Y;
                    const float DZ = WZ - Br.Position.Z;
                    const float D2 = DX * DX + DY * DY + DZ * DZ;
                    const float R2 = Br.Radius * Br.Radius;
                    if (D2 < R2 * 2.25f)
                    {
                        const float W = std::exp(-D2 / std::max(1.0e-5f, R2 * 0.55f)) * Br.Gain;
                        Vel.X += Br.Velocity.X * W * Dt * 8.0f;
                        Vel.Y += Br.Velocity.Y * W * Dt * 8.0f;
                        Vel.Z += Br.Velocity.Z * W * Dt * 8.0f;
                        Th.X  = std::min(12.0f, Th.X + 2.4f * W * Dt * 8.0f);
                        Th.Y  = std::min(12.0f, Th.Y + 4.2f * W * Dt * 8.0f);
                        Th.Z  = std::min(12.0f, Th.Z + 3.2f * W * Dt * 8.0f);
                    }
                }

                // ParticleSpace ballistic shrapnel smoke/fuel streamers
                for (const auto& Sp : PendingSplats)
                {
                    const float DX = WX - Sp.WorldPosition.X;
                    const float DY = WY - Sp.WorldPosition.Y;
                    const float DZ = WZ - Sp.WorldPosition.Z;
                    const float D2 = DX * DX + DY * DY + DZ * DZ;
                    const float R2 = Sp.RadiusMeters * Sp.RadiusMeters;
                    if (D2 < R2 * 4.0f)
                    {
                        const float W = std::exp(-D2 / std::max(1.0e-5f, R2));
                        Vel.X += Sp.VelocityMps.X * W * Dt * 2.2f;
                        Vel.Y += Sp.VelocityMps.Y * W * Dt * 2.2f;
                        Vel.Z += Sp.VelocityMps.Z * W * Dt * 2.2f;
                        Th.X  = std::min(12.0f, Th.X + Sp.Smoke * W * Dt * 6.0f);
                        Th.Y  = std::min(12.0f, Th.Y + Sp.Temperature * W * Dt * 6.0f);
                        Th.Z  = std::min(12.0f, Th.Z + Sp.Fuel * W * Dt * 6.0f);
                    }
                }

                HeroGrid.VelocitySolid[Rd][Idx] = Vel;
                HeroGrid.Thermo[Rd][Idx]        = Th;
            }
        }
    }

    PendingBlasts.clear();
    PendingBrushes.clear();
    PendingSplats.clear();
}

void FluidSimulationSequence::ExecuteMacCormackAdvection(float Dt)
{
    const uint32_t R  = HeroGrid.Resolution;
    const uint32_t Rd = HeroGrid.ReadIdx();
    const uint32_t Wr = HeroGrid.WriteIdx();
    const float    FR = static_cast<float>(R);
    const Mathematics::Axis3D Span = HeroGrid.BoxMaxWorld - HeroGrid.BoxMinWorld;
    const float InvSpanX = 1.0f / std::max(1.0e-4f, Span.X);
    const float InvSpanY = 1.0f / std::max(1.0e-4f, Span.Y);
    const float InvSpanZ = 1.0f / std::max(1.0e-4f, Span.Z);

    const float Damp = std::exp(-std::max(0.0f, Document.Combustion.VelocityDamping) * Dt);

    for (uint32_t IZ = 0u; IZ < R; ++IZ)
    {
        const float UZ = (static_cast<float>(IZ) + 0.5f) / FR;
        for (uint32_t IY = 0u; IY < R; ++IY)
        {
            const float UY = (static_cast<float>(IY) + 0.5f) / FR;
            for (uint32_t IX = 0u; IX < R; ++IX)
            {
                const float UX = (static_cast<float>(IX) + 0.5f) / FR;
                const uint32_t Idx = FluidGridVolume::FlatIndex(
                    static_cast<int32_t>(IX), static_cast<int32_t>(IY), static_cast<int32_t>(IZ), R);

                const auto& CurV = HeroGrid.VelocitySolid[Rd][Idx];
                if (CurV.W > 0.5f)
                {
                    HeroGrid.VelocitySolid[Wr][Idx] = { 0.0f, 0.0f, 0.0f, 1.0f };
                    HeroGrid.Thermo[Wr][Idx]        = {};
                    HeroGrid.Divergence[Idx]        = {};
                    continue;
                }

                const Mathematics::Axis3D BackUvw {
                    UX - CurV.X * Dt * InvSpanX,
                    UY - CurV.Y * Dt * InvSpanY,
                    UZ - CurV.Z * Dt * InvSpanZ,
                };

                auto AdvV = HeroGrid.SampleTrilinear(HeroGrid.VelocitySolid[Rd], BackUvw);
                auto AdvT = HeroGrid.SampleTrilinear(HeroGrid.Thermo[Rd], BackUvw);

                if (Document.Domain.MacCormackAdvection)
                {
                    const Mathematics::Axis3D FwdUvw {
                        BackUvw.X + AdvV.X * Dt * InvSpanX,
                        BackUvw.Y + AdvV.Y * Dt * InvSpanY,
                        BackUvw.Z + AdvV.Z * Dt * InvSpanZ,
                    };
                    const auto BF = HeroGrid.SampleTrilinear(HeroGrid.Thermo[Rd], FwdUvw);
                    const auto& CurT = HeroGrid.Thermo[Rd][Idx];
                    const FluidVoxelSample4 Corr {
                        AdvT.X + 0.45f * (CurT.X - BF.X),
                        AdvT.Y + 0.45f * (CurT.Y - BF.Y),
                        AdvT.Z + 0.45f * (CurT.Z - BF.Z),
                        AdvT.W,
                    };

                    const auto FetchT = [&](int32_t OX, int32_t OY, int32_t OZ) -> const FluidVoxelSample4&
                    {
                        return HeroGrid.Thermo[Rd][FluidGridVolume::FlatIndex(
                            static_cast<int32_t>(IX) + OX, static_cast<int32_t>(IY) + OY, static_cast<int32_t>(IZ) + OZ, R)];
                    };
                    const auto& NL = FetchT(-1, 0, 0); const auto& NR = FetchT(1, 0, 0);
                    const auto& ND = FetchT(0, -1, 0); const auto& NU = FetchT(0, 1, 0);
                    const auto& NB = FetchT(0, 0, -1); const auto& NF = FetchT(0, 0, 1);

                    AdvT.X = std::clamp(Corr.X, std::min({ CurT.X, NL.X, NR.X, ND.X, NU.X, NB.X, NF.X }), std::max({ CurT.X, NL.X, NR.X, ND.X, NU.X, NB.X, NF.X }));
                    AdvT.Y = std::clamp(Corr.Y, std::min({ CurT.Y, NL.Y, NR.Y, ND.Y, NU.Y, NB.Y, NF.Y }), std::max({ CurT.Y, NL.Y, NR.Y, ND.Y, NU.Y, NB.Y, NF.Y }));
                    AdvT.Z = std::clamp(Corr.Z, std::min({ CurT.Z, NL.Z, NR.Z, ND.Z, NU.Z, NB.Z, NF.Z }), std::max({ CurT.Z, NL.Z, NR.Z, ND.Z, NU.Z, NB.Z, NF.Z }));
                }

                float Smoke = std::max(0.0f, AdvT.X);
                float Temp  = std::max(0.0f, AdvT.Y);
                float Fuel  = std::max(0.0f, AdvT.Z);

                const float Ign        = SmoothStepF(0.08f, 0.65f, Temp + 0.5f * Fuel);
                const float BurnedFuel = std::min(Fuel, Fuel * Document.Combustion.BurnRate * (0.65f + 0.65f * Ign) * Dt);
                Fuel  = std::max(0.0f, Fuel - BurnedFuel);
                Temp += BurnedFuel * Document.Combustion.BurnHeat;
                Smoke += BurnedFuel * Document.Combustion.SootGeneration;

                const float RadCool = Document.Combustion.CoolingRate * (0.55f * Temp + 0.14f * Temp * Temp) * Dt;
                Temp  = std::max(0.0f, Temp - RadCool);
                Smoke = std::max(0.0f, Smoke * std::exp(-Document.Combustion.SmokeDissipation * Dt));
                Fuel  = std::max(0.0f, Fuel  * std::exp(-0.15f * Dt));

                if (!Document.Domain.EnclosedBox)
                {
                    const float EdgeXY = std::min({ UX, 1.0f - UX, UY, 1.0f - UY });
                    const float Sponge = SmoothStepF(0.0f, 0.06f, EdgeXY) * SmoothStepF(0.0f, 0.045f, 1.0f - UZ);
                    const float FireSponge = SmoothStepF(0.0f, 0.14f, EdgeXY) * SmoothStepF(0.0f, 0.08f, 1.0f - UZ);
                    Smoke *= Sponge;
                    Temp  *= FireSponge;
                    Fuel  *= FireSponge;
                }

                HeroGrid.VelocitySolid[Wr][Idx] = { AdvV.X * Damp, AdvV.Y * Damp, AdvV.Z * Damp, 0.0f };
                HeroGrid.Thermo[Wr][Idx]        = { Smoke, Temp, Fuel, Temp };
                HeroGrid.Divergence[Idx].Y      = BurnedFuel / std::max(Dt, 1.0e-4f);
            }
        }
    }

    HeroGrid.SwapPingPong();
}

void FluidSimulationSequence::ExecuteVorticityAndCombustion(float Dt, const FluidStepContext& Context)
{
    const uint32_t R  = HeroGrid.Resolution;
    const uint32_t Rd = HeroGrid.ReadIdx();
    const float    FR = static_cast<float>(R);
    const Mathematics::Axis3D Span = HeroGrid.BoxMaxWorld - HeroGrid.BoxMinWorld;

    // 1. Compute 3D Curl vector & magnitude
    for (uint32_t IZ = 0u; IZ < R; ++IZ)
    {
        for (uint32_t IY = 0u; IY < R; ++IY)
        {
            for (uint32_t IX = 0u; IX < R; ++IX)
            {
                const int32_t X = static_cast<int32_t>(IX), Y = static_cast<int32_t>(IY), Z = static_cast<int32_t>(IZ);
                const auto& VL = HeroGrid.VelocitySolid[Rd][FluidGridVolume::FlatIndex(X - 1, Y, Z, R)];
                const auto& VR = HeroGrid.VelocitySolid[Rd][FluidGridVolume::FlatIndex(X + 1, Y, Z, R)];
                const auto& VD = HeroGrid.VelocitySolid[Rd][FluidGridVolume::FlatIndex(X, Y - 1, Z, R)];
                const auto& VU = HeroGrid.VelocitySolid[Rd][FluidGridVolume::FlatIndex(X, Y + 1, Z, R)];
                const auto& VB = HeroGrid.VelocitySolid[Rd][FluidGridVolume::FlatIndex(X, Y, Z - 1, R)];
                const auto& VF = HeroGrid.VelocitySolid[Rd][FluidGridVolume::FlatIndex(X, Y, Z + 1, R)];

                const float DwDy = 0.5f * (VU.Z - VD.Z);
                const float DvDz = 0.5f * (VF.Y - VB.Y);
                const float DuDz = 0.5f * (VF.X - VB.X);
                const float DwDx = 0.5f * (VR.Z - VL.Z);
                const float DvDx = 0.5f * (VR.Y - VL.Y);
                const float DuDy = 0.5f * (VU.X - VD.X);

                const float CX = DwDy - DvDz;
                const float CY = DuDz - DwDx;
                const float CZ = DvDx - DuDy;
                const float CM = std::sqrt(CX * CX + CY * CY + CZ * CZ);
                HeroGrid.Curl[FluidGridVolume::FlatIndex(X, Y, Z, R)] = { CX, CY, CZ, CM };
            }
        }
    }

    // 2. Vorticity confinement + Buoyancy (+Z) + Baroclinic Torque + Curl Turb + ForceFields
    const bool HasForces = Document.Combustion.CoupleForceFields &&
                           ((Context.ForceFieldSource != nullptr && Context.ForceFieldSource->QueryActiveSourceCount() > 0u) ||
                            Context.WindLatticeCascade != nullptr);

    for (uint32_t IZ = 0u; IZ < R; ++IZ)
    {
        const float UZ = (static_cast<float>(IZ) + 0.5f) / FR;
        const float WZ = HeroGrid.BoxMinWorld.Z + UZ * Span.Z;
        for (uint32_t IY = 0u; IY < R; ++IY)
        {
            const float UY = (static_cast<float>(IY) + 0.5f) / FR;
            const float WY = HeroGrid.BoxMinWorld.Y + UY * Span.Y;
            for (uint32_t IX = 0u; IX < R; ++IX)
            {
                const float UX = (static_cast<float>(IX) + 0.5f) / FR;
                const float WX = HeroGrid.BoxMinWorld.X + UX * Span.X;
                const int32_t X = static_cast<int32_t>(IX), Y = static_cast<int32_t>(IY), Z = static_cast<int32_t>(IZ);
                const uint32_t Idx = FluidGridVolume::FlatIndex(X, Y, Z, R);

                auto& Vel = HeroGrid.VelocitySolid[Rd][Idx];
                if (Vel.W > 0.5f)
                {
                    Vel = { 0.0f, 0.0f, 0.0f, 1.0f };
                    continue;
                }

                const auto& Th = HeroGrid.Thermo[Rd][Idx];
                const float Smoke = Th.X;
                const float Temp  = Th.Y;
                const float Fuel  = Th.Z;

                const float CL = HeroGrid.Curl[FluidGridVolume::FlatIndex(X - 1, Y, Z, R)].W;
                const float CR = HeroGrid.Curl[FluidGridVolume::FlatIndex(X + 1, Y, Z, R)].W;
                const float CD = HeroGrid.Curl[FluidGridVolume::FlatIndex(X, Y - 1, Z, R)].W;
                const float CU = HeroGrid.Curl[FluidGridVolume::FlatIndex(X, Y + 1, Z, R)].W;
                const float CB = HeroGrid.Curl[FluidGridVolume::FlatIndex(X, Y, Z - 1, R)].W;
                const float CF = HeroGrid.Curl[FluidGridVolume::FlatIndex(X, Y, Z + 1, R)].W;

                const float GX = 0.5f * (CR - CL);
                const float GY = 0.5f * (CU - CD);
                const float GZ = 0.5f * (CF - CB);
                const float GL = std::sqrt(GX * GX + GY * GY + GZ * GZ);
                float VortX = 0.0f, VortY = 0.0f, VortZ = 0.0f;
                if (GL > 1.0e-5f)
                {
                    const float NX = GX / GL, NY = GY / GL, NZ = GZ / GL;
                    const auto& Om = HeroGrid.Curl[Idx];
                    VortX = std::clamp(Document.Combustion.VorticityConfinement * (NY * Om.Z - NZ * Om.Y), -48.0f, 48.0f);
                    VortY = std::clamp(Document.Combustion.VorticityConfinement * (NZ * Om.X - NX * Om.Z), -48.0f, 48.0f);
                    VortZ = std::clamp(Document.Combustion.VorticityConfinement * (NX * Om.Y - NY * Om.X), -48.0f, 48.0f);
                }

                const float BuoyZ = Document.Combustion.Buoyancy * Temp - Document.Combustion.SmokeWeight * Smoke * 0.45f;

                const float TL = HeroGrid.Thermo[Rd][FluidGridVolume::FlatIndex(X - 1, Y, Z, R)].Y;
                const float TR = HeroGrid.Thermo[Rd][FluidGridVolume::FlatIndex(X + 1, Y, Z, R)].Y;
                const float TB = HeroGrid.Thermo[Rd][FluidGridVolume::FlatIndex(X, Y - 1, Z, R)].Y;
                const float TF = HeroGrid.Thermo[Rd][FluidGridVolume::FlatIndex(X, Y + 1, Z, R)].Y;
                const float BaroX = (TR - TL) * (0.35f * Document.Combustion.Buoyancy);
                const float BaroY = (TF - TB) * (0.35f * Document.Combustion.Buoyancy);

                const float Activity = std::clamp(Smoke * 0.65f + Temp * 0.85f + Fuel * 0.55f, 0.0f, 1.5f);
                const auto  Turb = CurlFbm2(
                    WX * Document.Combustion.TurbulenceScale * 0.95f + ElapsedSeconds * 0.75f,
                    WY * Document.Combustion.TurbulenceScale * 0.95f + ElapsedSeconds * 0.55f,
                    WZ * Document.Combustion.TurbulenceScale * 0.95f - ElapsedSeconds * 1.65f);

                // Helical Rankine vortex funnel confinement when high-swirl tornado mode is active
                float FunnelX = 0.0f, FunnelY = 0.0f, FunnelZ = 0.0f;
                if (Document.Emitter.Swirl >= 3.0f && Activity > 1.0e-3f)
                {
                    const float RDX = WX - Document.Domain.OriginMeters.X;
                    const float RDY = WY - Document.Domain.OriginMeters.Y;
                    const float RXY = std::hypot(RDX, RDY);
                    const float CoreR = 0.14f + UZ * 0.28f;
                    if (RXY > 1.0e-4f && RXY < CoreR * 3.8f)
                    {
                        const float TanX = -RDY / RXY, TanY = RDX / RXY;
                        const float RadX =  RDX / RXY, RadY = RDY / RXY;
                        const float Profile = (RXY * CoreR) / (RXY * RXY + CoreR * CoreR);
                        const float SwirlGain = Document.Emitter.Swirl * 3.4f * Profile * (1.15f - 0.35f * UZ);
                        const float PullGain  = (RXY > CoreR * 0.65f) ? (-2.35f * Document.Emitter.Swirl * 0.28f * Profile) : 0.35f;
                        FunnelX = TanX * SwirlGain + RadX * PullGain;
                        FunnelY = TanY * SwirlGain + RadY * PullGain;
                        FunnelZ = 14.0f * Profile * (1.10f - 0.35f * UZ);
                    }
                }

                Mathematics::Axis3D ExtVel { Document.Combustion.WindVelocityXY.X, Document.Combustion.WindVelocityXY.Y, 0.0f };
                if (HasForces && Activity > 1.0e-3f)
                {
                    ExtVel = FluidForceCoupler::SampleExternalForceVelocity(
                        Document.Combustion, { WX, WY, WZ }, ElapsedSeconds,
                        Context.ForceFieldSource, Context.WindLatticeCascade);
                }

                const float WindFade = SmoothStepF(0.02f, 0.30f, UZ);
                const float WindGain = Activity * WindFade;

                Vel.X += (VortX + BaroX + FunnelX + Turb.X * Document.Combustion.TurbulenceStrength * Activity + ExtVel.X * WindGain) * Dt;
                Vel.Y += (VortY + BaroY + FunnelY + Turb.Y * Document.Combustion.TurbulenceStrength * Activity + ExtVel.Y * WindGain) * Dt;
                Vel.Z += (VortZ + BuoyZ + FunnelZ + Turb.Z * Document.Combustion.TurbulenceStrength * Activity + ExtVel.Z * WindGain) * Dt;

                const float Spd = std::sqrt(Vel.X * Vel.X + Vel.Y * Vel.Y + Vel.Z * Vel.Z);
                if (Spd > 24.0f)
                {
                    const float Scale = 24.0f / Spd;
                    Vel.X *= Scale;
                    Vel.Y *= Scale;
                    Vel.Z *= Scale;
                }
            }
        }
    }
}

void FluidSimulationSequence::ExecuteDivergenceAndPressure(float /*Dt*/)
{
    const uint32_t R  = HeroGrid.Resolution;
    const uint32_t Rd = HeroGrid.ReadIdx();
    const bool     Enclosed = Document.Domain.EnclosedBox;

    // 1. Divergence + Combustion gas expansion
    for (uint32_t IZ = 0u; IZ < R; ++IZ)
    {
        for (uint32_t IY = 0u; IY < R; ++IY)
        {
            for (uint32_t IX = 0u; IX < R; ++IX)
            {
                const int32_t X = static_cast<int32_t>(IX), Y = static_cast<int32_t>(IY), Z = static_cast<int32_t>(IZ);
                const uint32_t Idx = FluidGridVolume::FlatIndex(X, Y, Z, R);
                if (HeroGrid.VelocitySolid[Rd][Idx].W > 0.5f)
                {
                    HeroGrid.Divergence[Idx].X = 0.0f;
                    continue;
                }

                const auto& VL = HeroGrid.VelocitySolid[Rd][FluidGridVolume::FlatIndex(X - 1, Y, Z, R)];
                const auto& VR = HeroGrid.VelocitySolid[Rd][FluidGridVolume::FlatIndex(X + 1, Y, Z, R)];
                const auto& VD = HeroGrid.VelocitySolid[Rd][FluidGridVolume::FlatIndex(X, Y - 1, Z, R)];
                const auto& VU = HeroGrid.VelocitySolid[Rd][FluidGridVolume::FlatIndex(X, Y + 1, Z, R)];
                const auto& VB = HeroGrid.VelocitySolid[Rd][FluidGridVolume::FlatIndex(X, Y, Z - 1, R)];
                const auto& VF = HeroGrid.VelocitySolid[Rd][FluidGridVolume::FlatIndex(X, Y, Z + 1, R)];

                const float UX0 = (VL.W > 0.5f || (Enclosed && IX == 0u)) ? 0.0f : VL.X;
                const float UX1 = (VR.W > 0.5f || (Enclosed && IX == R - 1u)) ? 0.0f : VR.X;
                const float UY0 = (VD.W > 0.5f || (Enclosed && IY == 0u)) ? 0.0f : VD.Y;
                const float UY1 = (VU.W > 0.5f || (Enclosed && IY == R - 1u)) ? 0.0f : VU.Y;
                const float UZ0 = (VB.W > 0.5f || IZ == 0u) ? 0.0f : VB.Z; // Ground floor boundary
                const float UZ1 = (VF.W > 0.5f || (Enclosed && IZ == R - 1u)) ? 0.0f : VF.Z;

                const float Div      = 0.5f * ((UX1 - UX0) + (UY1 - UY0) + (UZ1 - UZ0));
                const float React    = HeroGrid.Divergence[Idx].Y;
                const float FireSrc  = std::clamp(HeroGrid.Thermo[Rd][Idx].Y * 0.45f + HeroGrid.Thermo[Rd][Idx].Z * 0.35f, 0.0f, 6.0f);
                HeroGrid.Divergence[Idx].X = Div - React * Document.Combustion.CombustionExpansion * 0.055f;
                HeroGrid.Divergence[Idx].Z = FireSrc;
            }
        }
    }

    // 2. Jacobi Pressure + Fire Irradiance diffusion (respecting Neumann solid walls)
    const uint32_t Iters = std::clamp(Document.Domain.PressureIterations, 4u, 64u);
    uint32_t PRd = 0u, PWr = 1u;
    for (uint32_t It = 0u; It < Iters; ++It)
    {
        for (uint32_t IZ = 0u; IZ < R; ++IZ)
        {
            for (uint32_t IY = 0u; IY < R; ++IY)
            {
                for (uint32_t IX = 0u; IX < R; ++IX)
                {
                    const int32_t X = static_cast<int32_t>(IX), Y = static_cast<int32_t>(IY), Z = static_cast<int32_t>(IZ);
                    const uint32_t Idx = FluidGridVolume::FlatIndex(X, Y, Z, R);
                    const auto& CP     = HeroGrid.Pressure[PRd][Idx];
                    const float Div    = HeroGrid.Divergence[Idx].X;
                    const float FireS  = HeroGrid.Divergence[Idx].Z;

                    const auto SampleP = [&](int32_t SX, int32_t SY, int32_t SZ, bool Wall, bool OpenOutflow) -> const FluidVoxelSample4&
                    {
                        static const FluidVoxelSample4 ZeroP { 0.0f, 0.0f, 0.0f, 1.0f };
                        if (OpenOutflow) return ZeroP;
                        const uint32_t NIdx = FluidGridVolume::FlatIndex(SX, SY, SZ, R);
                        if (Wall || HeroGrid.VelocitySolid[Rd][NIdx].W > 0.5f) return CP;
                        return HeroGrid.Pressure[PRd][NIdx];
                    };

                    const auto& PL = SampleP(X - 1, Y, Z, Enclosed && IX == 0u,      !Enclosed && IX == 0u);
                    const auto& PR = SampleP(X + 1, Y, Z, Enclosed && IX == R - 1u, !Enclosed && IX == R - 1u);
                    const auto& PD = SampleP(X, Y - 1, Z, Enclosed && IY == 0u,      !Enclosed && IY == 0u);
                    const auto& PU = SampleP(X, Y + 1, Z, Enclosed && IY == R - 1u, !Enclosed && IY == R - 1u);
                    const auto& PB = SampleP(X, Y, Z - 1, IZ == 0u,                  false);
                    const auto& PF = SampleP(X, Y, Z + 1, Enclosed && IZ == R - 1u, !Enclosed && IZ == R - 1u);

                    const float PNew   = (PL.X + PR.X + PD.X + PU.X + PB.X + PF.X - Div) / 6.0f;
                    const float IrrNew = (PL.Y + PR.Y + PD.Y + PU.Y + PB.Y + PF.Y + FireS * 0.55f) / 6.28f;
                    HeroGrid.Pressure[PWr][Idx] = { PNew, IrrNew, 0.0f, 1.0f };
                }
            }
        }
        std::swap(PRd, PWr);
    }

    // 3. Pressure Gradient Subtraction
    for (uint32_t IZ = 0u; IZ < R; ++IZ)
    {
        for (uint32_t IY = 0u; IY < R; ++IY)
        {
            for (uint32_t IX = 0u; IX < R; ++IX)
            {
                const int32_t X = static_cast<int32_t>(IX), Y = static_cast<int32_t>(IY), Z = static_cast<int32_t>(IZ);
                const uint32_t Idx = FluidGridVolume::FlatIndex(X, Y, Z, R);
                auto& Vel = HeroGrid.VelocitySolid[Rd][Idx];
                if (Vel.W > 0.5f)
                {
                    Vel = { 0.0f, 0.0f, 0.0f, 1.0f };
                    continue;
                }
                const float PC = HeroGrid.Pressure[PRd][Idx].X;
                const auto PAt = [&](int32_t SX, int32_t SY, int32_t SZ, bool Wall, bool OpenOutflow) -> float
                {
                    if (OpenOutflow) return 0.0f;
                    const uint32_t NIdx = FluidGridVolume::FlatIndex(SX, SY, SZ, R);
                    if (Wall || HeroGrid.VelocitySolid[Rd][NIdx].W > 0.5f) return PC;
                    return HeroGrid.Pressure[PRd][NIdx].X;
                };

                const float PL = PAt(X - 1, Y, Z, Enclosed && IX == 0u,      !Enclosed && IX == 0u);
                const float PR = PAt(X + 1, Y, Z, Enclosed && IX == R - 1u, !Enclosed && IX == R - 1u);
                const float PD = PAt(X, Y - 1, Z, Enclosed && IY == 0u,      !Enclosed && IY == 0u);
                const float PU = PAt(X, Y + 1, Z, Enclosed && IY == R - 1u, !Enclosed && IY == R - 1u);
                const float PB = PAt(X, Y, Z - 1, IZ == 0u,                  false);
                const float PF = PAt(X, Y, Z + 1, Enclosed && IZ == R - 1u, !Enclosed && IZ == R - 1u);

                Vel.X -= 0.5f * (PR - PL);
                Vel.Y -= 0.5f * (PU - PD);
                Vel.Z -= 0.5f * (PF - PB);
                HeroGrid.Thermo[Rd][Idx].W = HeroGrid.Pressure[PRd][Idx].Y;
            }
        }
    }
}

FluidVoxelSample4 FluidSimulationSequence::SampleThermoAtWorldPoint(const Mathematics::Axis3D& WorldPoint) const noexcept
{
    auto Th = HeroGrid.SampleWorldTrilinear(HeroGrid.Thermo[HeroGrid.ReadIdx()], WorldPoint);
    if (Th.X <= 1.0e-4f && Th.Y <= 1.0e-4f && Document.Domain.WorldGridEnabled)
    {
        Th = WorldGrid.SampleWorldTrilinear(WorldGrid.Thermo, WorldPoint);
    }
    return Th;
}

FluidVoxelSample4 FluidSimulationSequence::SampleVelocityAtWorldPoint(const Mathematics::Axis3D& WorldPoint) const noexcept
{
    auto V = HeroGrid.SampleWorldTrilinear(HeroGrid.VelocitySolid[HeroGrid.ReadIdx()], WorldPoint);
    if (V.X == 0.0f && V.Y == 0.0f && V.Z == 0.0f && Document.Domain.WorldGridEnabled)
    {
        V = WorldGrid.SampleWorldTrilinear(WorldGrid.VelocitySolid, WorldPoint);
    }
    return V;
}

float FluidSimulationSequence::EvaluateFluidShadowTransmittance(
    const Mathematics::Axis3D& RayOriginWorld,
    const Mathematics::Axis3D& UnitDirToLight,
    float                      MaxDistanceMeters) const noexcept
{
    if (!Document.GiCoupling.CastGiShadows) return 1.0f;

    float Transmittance = 1.0f;
    const uint32_t Steps = std::clamp(Document.Shading.ShadowSteps, 2u, 12u);

    // 1. Hero Pyro Grid self/GI shadow march
    float TNear = 0.0f, TFar = 0.0f;
    if (IntersectBox(RayOriginWorld, UnitDirToLight, HeroGrid.BoxMinWorld, HeroGrid.BoxMaxWorld, TNear, TFar))
    {
        const float T0 = std::max(0.01f, TNear);
        const float T1 = std::min(MaxDistanceMeters, TFar);
        if (T1 > T0)
        {
            const float Dt = (T1 - T0) / static_cast<float>(Steps);
            const uint32_t Rd = HeroGrid.ReadIdx();
            for (uint32_t S = 0u; S < Steps; ++S)
            {
                const float T = T0 + (static_cast<float>(S) + 0.5f) * Dt;
                const auto Th = HeroGrid.SampleWorldTrilinear(HeroGrid.Thermo[Rd], RayOriginWorld + UnitDirToLight * T);
                if (Th.X > 1.0e-3f)
                {
                    Transmittance *= std::exp(-Th.X * Document.Shading.ShadowDensity * Document.GiCoupling.GiShadowStrength * Dt * 0.45f);
                }
            }
        }
    }

    // 2. World Drift Smoke Grid shadow march
    if (Document.Domain.WorldGridEnabled && WorldGrid.Resolution > 0u && Transmittance > 0.02f)
    {
        const float Half = 0.5f * WorldGrid.SpanMeters;
        const Mathematics::Axis3D WMin = WorldGrid.CenterWorld - Mathematics::Axis3D{ Half, Half, Half };
        const Mathematics::Axis3D WMax = WorldGrid.CenterWorld + Mathematics::Axis3D{ Half, Half, Half };
        if (IntersectBox(RayOriginWorld, UnitDirToLight, WMin, WMax, TNear, TFar))
        {
            const float T0 = std::max(0.01f, TNear);
            const float T1 = std::min(MaxDistanceMeters, TFar);
            if (T1 > T0)
            {
                const float Dt = (T1 - T0) / static_cast<float>(Steps);
                for (uint32_t S = 0u; S < Steps; ++S)
                {
                    const float T = T0 + (static_cast<float>(S) + 0.5f) * Dt;
                    const auto Th = WorldGrid.SampleWorldTrilinear(WorldGrid.Thermo, RayOriginWorld + UnitDirToLight * T);
                    if (Th.X > 1.0e-3f)
                    {
                        Transmittance *= std::exp(-Th.X * Document.Shading.ShadowDensity * Document.GiCoupling.GiShadowStrength * Dt * 0.25f);
                    }
                }
            }
        }
    }

    return std::clamp(Transmittance, 0.04f, 1.0f);
}

FluidRaymarchResult FluidSimulationSequence::EvaluateFluidSegment(
    const Mathematics::Axis3D&    RayOriginWorld,
    const Mathematics::Axis3D&    RayDirection,
    float                         MaxDistanceMeters,
    const Mathematics::Axis3D&    SunDirection,
    const Mathematics::LinearRGB& SolarIrradiance,
    const Mathematics::LinearRGB& SkyRadiance,
    float                         PixelJitter01) const noexcept
{
    FluidRaymarchResult Out {};
    const float SafeMax = std::max(0.0f, MaxDistanceMeters);
    if (SafeMax <= 0.02f || HeroGrid.Resolution == 0u) return Out;

    const float Jitter = 0.5f + 0.7f * (PixelJitter01 - 0.5f);
    const float CosTheta = Mathematics::EvaluateDot(RayDirection, SunDirection);
    const float Phase    = DualHgPhase(CosTheta, Document.Shading.PhaseAnisotropy);
    const uint32_t Steps = std::clamp(Document.Shading.RaymarchSteps, 16u, 144u);
    const uint32_t Rd    = HeroGrid.ReadIdx();

    // 1. Hero Pyro Volume (Depth-clipped [TEnter, min(TExit, SafeMax)])
    float TNear = 0.0f, TFar = 0.0f;
    if (IntersectBox(RayOriginWorld, RayDirection, HeroGrid.BoxMinWorld, HeroGrid.BoxMaxWorld, TNear, TFar))
    {
        const float TEnter = std::max(0.02f, TNear);
        const float TExit  = std::min(SafeMax, TFar);
        if (TExit > TEnter)
        {
            const Mathematics::Axis3D BoxSpan = HeroGrid.BoxMaxWorld - HeroGrid.BoxMinWorld;
            const float BoxDiag = std::sqrt(BoxSpan.X * BoxSpan.X + BoxSpan.Y * BoxSpan.Y + BoxSpan.Z * BoxSpan.Z);
            const float OpticalScale = 1.75f / std::max(1.0f, BoxDiag);
            const float Dt = (TExit - TEnter) / static_cast<float>(Steps);
            const float EffStep = Dt * OpticalScale;
            for (uint32_t S = 0u; S < Steps; ++S)
            {
                const float T = TEnter + (static_cast<float>(S) + Jitter) * Dt;
                const Mathematics::Axis3D P = RayOriginWorld + RayDirection * T;
                const auto Th = HeroGrid.SampleWorldTrilinear(HeroGrid.Thermo[Rd], P);
                float Smoke = Th.X;
                float Temp  = Th.Y;
                const float Irr = Th.W;

                if (Document.Shading.RenderChannel == FluidRenderChannel::SmokeDensity && Smoke > 0.005f)
                {
                    const float A = std::clamp(Smoke * Dt * 1.4f, 0.0f, 1.0f);
                    Out.RadianceHdr.R += Out.Transmittance * Smoke * 0.45f * A;
                    Out.RadianceHdr.G += Out.Transmittance * Smoke * 0.48f * A;
                    Out.RadianceHdr.B += Out.Transmittance * Smoke * 0.52f * A;
                    Out.Transmittance *= (1.0f - A);
                    continue;
                }
                if (Document.Shading.RenderChannel == FluidRenderChannel::Temperature && Temp > 0.005f)
                {
                    const auto C = FluidPresets::EvaluateBlackbodyPalette(Temp, Document.Shading.TemperatureScale, Document.Shading.ColorPalette);
                    const float A = std::clamp(Temp * Dt * 1.2f, 0.0f, 1.0f);
                    Out.RadianceHdr.R += Out.Transmittance * C.R * A;
                    Out.RadianceHdr.G += Out.Transmittance * C.G * A;
                    Out.RadianceHdr.B += Out.Transmittance * C.B * A;
                    Out.Transmittance *= (1.0f - A * 0.7f);
                    continue;
                }

                if (Smoke <= 0.004f && Temp <= 0.01f) continue;

                // Sub-voxel FBM pyro detail breakup for sharp rolling soot & licking flame tongues
                if (Document.Domain.VoxelQuantization <= 0.01f)
                {
                    const float Erode = Fbm2Noise3(P.X * 5.8f + ElapsedSeconds * 0.4f, P.Y * 5.8f - ElapsedSeconds * 0.3f, P.Z * 5.8f - ElapsedSeconds * 1.9f);
                    Smoke = std::max(0.0f, Smoke * (0.76f + 0.48f * Erode) - 0.045f * std::max(0.0f, 0.35f - Erode));
                    Temp  = std::max(0.0f, Temp  * (0.72f + 0.56f * Erode) - 0.06f * std::max(0.0f, 0.25f - Erode));
                    if (Smoke <= 0.004f && Temp <= 0.008f) continue;
                }

                const float SunT = EvaluateFluidShadowTransmittance(P, SunDirection, 24.0f);
                const float Powder = 1.0f - 0.45f * std::exp(-Smoke * Document.Shading.DensityExtinction * 2.2f);
                const float HeightAo = 0.55f + 0.60f * std::clamp((P.Z - HeroGrid.BoxMinWorld.Z) / std::max(0.2f, BoxSpan.Z), 0.0f, 1.0f);
                const auto InternalGlow = FluidPresets::EvaluateBlackbodyPalette(
                    std::max(0.65f, Irr * 1.25f), Document.Shading.TemperatureScale, Document.Shading.ColorPalette);
                const float Ig = Irr * Document.Shading.InternalScattering * (0.45f + 0.55f * std::exp(-Smoke * 0.9f));

                const Mathematics::LinearRGB SmokeScat {
                    Document.Shading.SmokeAlbedo * (SolarIrradiance.R * SunT * Phase * Powder * 2.2f + (SkyRadiance.R + 0.16f) * Document.Shading.AmbientIntensity * HeightAo * 1.6f) + InternalGlow.R * Ig,
                    Document.Shading.SmokeAlbedo * (SolarIrradiance.G * SunT * Phase * Powder * 2.2f + (SkyRadiance.G + 0.16f) * Document.Shading.AmbientIntensity * HeightAo * 1.6f) + InternalGlow.G * Ig,
                    Document.Shading.SmokeAlbedo * (SolarIrradiance.B * SunT * Phase * Powder * 2.2f + (SkyRadiance.B + 0.18f) * Document.Shading.AmbientIntensity * HeightAo * 1.6f) + InternalGlow.B * Ig,
                };
                const auto FireEmit = FluidPresets::EvaluateBlackbodyPalette(Temp, Document.Shading.TemperatureScale, Document.Shading.ColorPalette);

                const float SmokeExt = Smoke * Document.Shading.DensityExtinction * 0.48f;
                const float Ext      = std::max(0.002f, SmokeExt + Temp * 0.55f);
                const float StepT    = std::exp(-Ext * EffStep);
                const float IntW     = (1.0f - StepT) / Ext;
                const float FireGain = Document.Shading.FireIntensity * 2.25f;

                Out.RadianceHdr.R += Out.Transmittance * (SmokeScat.R * SmokeExt + FireEmit.R * FireGain) * IntW;
                Out.RadianceHdr.G += Out.Transmittance * (SmokeScat.G * SmokeExt + FireEmit.G * FireGain) * IntW;
                Out.RadianceHdr.B += Out.Transmittance * (SmokeScat.B * SmokeExt + FireEmit.B * FireGain) * IntW;
                Out.Transmittance *= StepT;
                if (Out.Transmittance < 0.008f) break;
            }
        }
    }

    // 2. Continuous Sliding World Smoke Drift Grid (blends seamlessly outside Hero bounds)
    if (Document.Domain.WorldGridEnabled && WorldGrid.Resolution > 0u && Out.Transmittance > 0.015f)
    {
        const float Half = 0.5f * WorldGrid.SpanMeters;
        const Mathematics::Axis3D WMin = WorldGrid.CenterWorld - Mathematics::Axis3D{ Half, Half, Half };
        const Mathematics::Axis3D WMax = WorldGrid.CenterWorld + Mathematics::Axis3D{ Half, Half, Half };
        if (IntersectBox(RayOriginWorld, RayDirection, WMin, WMax, TNear, TFar))
        {
            const float TEnter = std::max(0.05f, TNear);
            const float TExit  = std::min(SafeMax, TFar);
            if (TExit > TEnter)
            {
                const uint32_t WSteps = 44u;
                const float Dt = (TExit - TEnter) / static_cast<float>(WSteps);
                for (uint32_t S = 0u; S < WSteps; ++S)
                {
                    const float T = TEnter + (static_cast<float>(S) + Jitter) * Dt;
                    const Mathematics::Axis3D P = RayOriginWorld + RayDirection * T;
                    if (P.Z < HeroGrid.BoxMinWorld.Z - 0.05f) continue;

                    // Smooth perimeter transition so World drift smoke blends seamlessly without Hero border seams
                    const float HX = std::max(HeroGrid.BoxMinWorld.X - P.X, P.X - HeroGrid.BoxMaxWorld.X);
                    const float HY = std::max(HeroGrid.BoxMinWorld.Y - P.Y, P.Y - HeroGrid.BoxMaxWorld.Y);
                    const float HZ = std::max(HeroGrid.BoxMinWorld.Z - P.Z, P.Z - HeroGrid.BoxMaxWorld.Z);
                    const float DistOut = std::max({ HX, HY, HZ });
                    if (DistOut < -0.35f) continue;
                    const float EdgeBlend = SmoothStepF(-0.35f, 0.12f, DistOut);

                    const auto Th = WorldGrid.SampleWorldTrilinear(WorldGrid.Thermo, P);
                    float Smoke = Th.X * EdgeBlend;
                    const float Temp = Th.Y * EdgeBlend;
                    if (Smoke <= 0.004f && Temp <= 0.01f) continue;

                    const float Erode = Fbm2Noise3(
                        P.X * 2.1f - Document.Combustion.WindVelocityXY.X * ElapsedSeconds * 0.35f,
                        P.Y * 2.1f - Document.Combustion.WindVelocityXY.Y * ElapsedSeconds * 0.35f,
                        P.Z * 2.1f - ElapsedSeconds * 0.45f);
                    Smoke = std::max(0.0f, Smoke * (0.76f + 0.48f * Erode) - 0.025f * std::max(0.0f, 0.32f - Erode));
                    if (Smoke <= 0.004f && Temp <= 0.01f) continue;

                    const float SunT = EvaluateFluidShadowTransmittance(P, SunDirection, 24.0f);
                    const float Powder = 1.0f - 0.45f * std::exp(-Smoke * Document.Shading.DensityExtinction * 0.65f);
                    const float SmokeExt = Smoke * Document.Shading.DensityExtinction * 0.34f;
                    const float Ext      = std::max(0.002f, SmokeExt + Temp * 0.35f);
                    const float StepT    = std::exp(-Ext * Dt * 0.55f);
                    const float IntW     = (1.0f - StepT) / Ext;

                    const Mathematics::LinearRGB SmokeScat {
                        Document.Shading.SmokeAlbedo * (SolarIrradiance.R * SunT * Phase * Powder * 2.1f + (SkyRadiance.R + 0.16f) * Document.Shading.AmbientIntensity * 1.6f),
                        Document.Shading.SmokeAlbedo * (SolarIrradiance.G * SunT * Phase * Powder * 2.1f + (SkyRadiance.G + 0.16f) * Document.Shading.AmbientIntensity * 1.6f),
                        Document.Shading.SmokeAlbedo * (SolarIrradiance.B * SunT * Phase * Powder * 2.1f + (SkyRadiance.B + 0.18f) * Document.Shading.AmbientIntensity * 1.6f),
                    };
                    const auto FireEmit = FluidPresets::EvaluateBlackbodyPalette(Temp, Document.Shading.TemperatureScale, Document.Shading.ColorPalette);
                    Out.RadianceHdr.R += Out.Transmittance * (SmokeScat.R * SmokeExt + FireEmit.R * 0.45f) * IntW;
                    Out.RadianceHdr.G += Out.Transmittance * (SmokeScat.G * SmokeExt + FireEmit.G * 0.45f) * IntW;
                    Out.RadianceHdr.B += Out.Transmittance * (SmokeScat.B * SmokeExt + FireEmit.B * 0.45f) * IntW;
                    Out.Transmittance *= StepT;
                    if (Out.Transmittance < 0.008f) break;
                }
            }
        }
    }

    return Out;
}

} // namespace Frontier::SimulationSpace::FluidSpace
