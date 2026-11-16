/*============================================================================================================================================
                                                          SPECIALEFFECTSDYNAMICS.CPP
============================================================================================================================================*/
// 🧩 Implementation of Ferrofluid, Fracture, Shallow-Water Splashes, Transition Derez, Shield Barrier, Spiderweb & MoGraph Effectors

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/SpecialEffectsDynamics.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr float Pi = 3.14159265358979323846f;

float EvaluateEffectorVolumeInfluence(const EffectorVolumeDescriptor& Vol, float Px, float Py, float Pz) noexcept
{
    if (Vol.RadiusMeters <= 1e-4f) return 0.0f;
    const float Dx = Px - Vol.CenterM[0];
    const float Dy = Py - Vol.CenterM[1];
    const float Dz = Pz - Vol.CenterM[2];
    const float R  = Vol.RadiusMeters;
    float NormDist = 1.0f;

    switch (Vol.Shape)
    {
        case EffectorVolumeShape::Sphere:
            NormDist = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) / R;
            break;
        case EffectorVolumeShape::Box:
            NormDist = std::max({ std::fabs(Dx), std::fabs(Dy), std::fabs(Dz) }) / R;
            break;
        case EffectorVolumeShape::Torus:
        {
            const float RadXZ = std::sqrt(Dx * Dx + Dz * Dz);
            const float RingD = std::sqrt((RadXZ - R) * (RadXZ - R) + Dy * Dy);
            NormDist = RingD / std::max(0.25f, R * 0.42f);
            break;
        }
        case EffectorVolumeShape::Disc:
            NormDist = std::max(std::sqrt(Dx * Dx + Dz * Dz) / R, std::fabs(Dy) / std::max(0.25f, R * 0.35f));
            break;
        case EffectorVolumeShape::Cone:
        {
            const float Taper = std::clamp(1.0f - (Dy + R * 0.5f) / (R * 1.4f), 0.1f, 1.5f);
            NormDist = std::sqrt(Dx * Dx + Dz * Dz) / std::max(0.1f, R * Taper);
            break;
        }
    }
    const float Clamped = std::clamp(1.0f - NormDist, 0.0f, 1.0f);
    return Clamped * Clamped * (3.0f - 2.0f * Clamped);
}

} // namespace

void StepFerrofluidMeniscusDynamics(
    const FerrofluidMeniscusSpec&  Spec,
    const float                    OriginCenter[3],
    const InteractiveColliderSpec& Collider,
    std::span<ParticleSlot>        Slots,
    float                          ElapsedSeconds,
    float                          DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    float MagPos[3] {};
    if (Spec.FreeDraggableMag && Collider.BallActive)
    {
        for (int K = 0; K < 3; ++K) MagPos[K] = Collider.BallCenterM[K];
    }
    else
    {
        const float Angle = ElapsedSeconds * Spec.MagnetOrbitRadPs;
        const float OrbitR = Spec.LevitatedCrown ? 2.15f : 1.35f;
        MagPos[0] = OriginCenter[0] + std::cos(Angle) * OrbitR;
        MagPos[1] = OriginCenter[1] + (Spec.LevitatedCrown ? (0.45f * std::sin(Angle * 1.7f)) : 1.05f);
        MagPos[2] = OriginCenter[2] + std::sin(Angle) * OrbitR;
    }

    const float Spring = std::clamp(11.0f * DeltaSeconds, 0.0f, 1.0f);
    for (auto& Slot : Slots)
    {
        if (!QueryParticleSlotAlive(Slot)) continue;

        const float Dx = MagPos[0] - Slot.OriginAnchor[0];
        const float Dy = MagPos[1] - Slot.OriginAnchor[1];
        const float Dz = MagPos[2] - Slot.OriginAnchor[2];
        const float Dist = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) + 0.18f;
        const float FieldStrength = 1.0f / (Dist * Dist);

        // Rosensweig instability hexagonal standing ridges modulated by dipole intensity
        const float HexWave = 0.5f + 0.5f * std::cos(Slot.OriginAnchor[0] * 11.5f) * std::cos(Slot.OriginAnchor[2] * 11.5f);
        const float SpikeH  = std::clamp(FieldStrength * 1.45f * (0.35f + 0.65f * HexWave), 0.0f, 0.92f);

        float DirX = 0.0f, DirY = 1.0f, DirZ = 0.0f;
        if (Spec.LevitatedCrown)
        {
            const float Rx = Slot.OriginAnchor[0] - OriginCenter[0];
            const float Ry = Slot.OriginAnchor[1] - OriginCenter[1];
            const float Rz = Slot.OriginAnchor[2] - OriginCenter[2];
            const float RL = std::sqrt(Rx * Rx + Ry * Ry + Rz * Rz) + 1e-4f;
            DirX = Rx / RL; DirY = Ry / RL; DirZ = Rz / RL;
        }

        const float Tx = Slot.OriginAnchor[0] + DirX * SpikeH;
        const float Ty = Slot.OriginAnchor[1] + DirY * SpikeH;
        const float Tz = Slot.OriginAnchor[2] + DirZ * SpikeH;

        Slot.PositionMeters[0] += (Tx - Slot.PositionMeters[0]) * Spring;
        Slot.PositionMeters[1] += (Ty - Slot.PositionMeters[1]) * Spring;
        Slot.PositionMeters[2] += (Tz - Slot.PositionMeters[2]) * Spring;
        Slot.AuxScalar = std::clamp(SpikeH * 1.15f, 0.0f, 1.0f);
    }
}

void StepFractureFissureDynamics(
    const FractureFissureSpec& Spec,
    const float                OriginCenter[3],
    std::span<ParticleSlot>    Slots,
    float                      ElapsedSeconds,
    float                      DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    const float FrontRadius = std::fmod(ElapsedSeconds * std::max(0.2f, Spec.FrontSpeedMps), 5.5f);
    const float HeaveSign   = Spec.SeismicBasalt ? 1.0f : -0.65f;

    for (auto& Slot : Slots)
    {
        if (!QueryParticleSlotAlive(Slot)) continue;

        const float Rx = Slot.OriginAnchor[0] - OriginCenter[0];
        const float Rz = Slot.OriginAnchor[2] - OriginCenter[2];
        const float R  = std::sqrt(Rx * Rx + Rz * Rz);
        const float CrackWave = std::clamp(1.0f - std::fabs(R - FrontRadius) * 1.6f, 0.0f, 1.0f);
        const bool  Passed    = (R < FrontRadius);

        // Voronoi fissure separation + basalt heave or ice slab depression
        const float OpenDisp = Passed ? (0.14f + 0.22f * CrackWave) : 0.0f;
        const float InvR     = 1.0f / (R + 0.05f);
        const float TargetX  = Slot.OriginAnchor[0] + Rx * InvR * OpenDisp;
        const float TargetZ  = Slot.OriginAnchor[2] + Rz * InvR * OpenDisp;
        const float TargetY  = Slot.OriginAnchor[1] + (Passed ? (HeaveSign * Spec.HeaveAmplitudeM * (0.35f + 0.65f * Slot.SeedScalar) * (0.5f + 0.5f * CrackWave)) : 0.0f);

        const float Blend = std::clamp(9.0f * DeltaSeconds, 0.0f, 1.0f);
        Slot.PositionMeters[0] += (TargetX - Slot.PositionMeters[0]) * Blend;
        Slot.PositionMeters[1] += (TargetY - Slot.PositionMeters[1]) * Blend;
        Slot.PositionMeters[2] += (TargetZ - Slot.PositionMeters[2]) * Blend;
        Slot.SpinRadians       += CrackWave * 2.5f * DeltaSeconds;
        Slot.AuxScalar          = CrackWave;
    }
}

void StepShallowWaterSplashDynamics(
    const ShallowWaterSplashSpec& Spec,
    const float                   OriginCenter[3],
    std::span<ParticleSlot>       Slots,
    float                         ElapsedSeconds,
    float                         DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    // Figure-8 rally car track coordinate when SplashVariant == 2
    const float Omega = Spec.VehicleSpeedMps / std::max(0.5f, Spec.TrackRadiusM);
    const float Phi   = ElapsedSeconds * Omega;
    const float CarX  = OriginCenter[0] + std::sin(Phi) * Spec.TrackRadiusM;
    const float CarZ  = OriginCenter[2] + std::sin(Phi * 2.0f) * Spec.TrackRadiusM * 0.5f;

    for (auto& Slot : Slots)
    {
        if (!QueryParticleSlotAlive(Slot)) continue;

        if (Spec.SplashVariant == 2u)
        {
            const float Dx = Slot.PositionMeters[0] - CarX;
            const float Dz = Slot.PositionMeters[2] - CarZ;
            const float D2 = Dx * Dx + Dz * Dz;
            if (D2 < 1.15f)
            {
                const float D = std::sqrt(D2) + 0.08f;
                const float Kick = (1.15f - D2) * Spec.VehicleSpeedMps * 2.2f * DeltaSeconds;
                Slot.VelocityMps[0] += (Dx / D) * Kick;
                Slot.VelocityMps[1] += Kick * 1.45f;
                Slot.VelocityMps[2] += (Dz / D) * Kick;
                Slot.AuxScalar = 1.0f;
            }
        }
        else
        {
            // Radial puddle crown ripple impulse
            const float Rx = Slot.PositionMeters[0] - OriginCenter[0];
            const float Rz = Slot.PositionMeters[2] - OriginCenter[2];
            const float R  = std::sqrt(Rx * Rx + Rz * Rz) + 0.05f;
            const float RingWave = std::sin(R * 5.2f - ElapsedSeconds * 7.0f) * std::exp(-R * 0.45f);
            Slot.VelocityMps[1] += RingWave * 4.5f * DeltaSeconds;
            Slot.AuxScalar       = std::clamp(0.5f + 0.5f * RingWave, 0.0f, 1.0f);
        }
    }
}

void StepTransitionDerezDynamics(
    const TransitionDerezSpec& Spec,
    const float                OriginCenter[3],
    std::span<ParticleSlot>    Slots,
    float                      ElapsedSeconds,
    float                      DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    const float SweepPhase = std::fmod(ElapsedSeconds * 0.32f, 1.0f); // 0..1 sweep across object height
    const float SweepY     = OriginCenter[1] - 1.5f + SweepPhase * 3.6f;

    for (auto& Slot : Slots)
    {
        if (!QueryParticleSlotAlive(Slot)) continue;

        const float DeltaY = Slot.OriginAnchor[1] - SweepY;
        const bool  Triggered = (Spec.TransitionVariant == 4u) ? (DeltaY > 0.0f) : (DeltaY < 0.0f);
        Slot.AuxScalar = std::clamp(1.0f - std::fabs(DeltaY) * 1.4f, 0.0f, 1.0f);

        if (Spec.TransitionVariant == 7u) // Hologram Glitch horizontal shear slices
        {
            const float Quant = std::floor(ElapsedSeconds * std::max(1.0f, Spec.GlitchRateHz));
            const float Band  = std::sin(Slot.OriginAnchor[1] * 14.0f + Quant * 5.7f);
            const float Shift = (Band > 0.72f) ? (std::sin(Quant * 19.1f) * 0.55f) : 0.0f;
            Slot.PositionMeters[0] = Slot.OriginAnchor[0] + Shift;
            Slot.PositionMeters[1] = Slot.OriginAnchor[1];
            Slot.PositionMeters[2] = Slot.OriginAnchor[2];
            continue;
        }

        if (Triggered)
        {
            if (Spec.TransitionVariant == 3u) // Melt downward puddle
            {
                Slot.VelocityMps[1] -= 3.8f * DeltaSeconds;
                if (Slot.PositionMeters[1] < 0.04f)
                {
                    Slot.PositionMeters[1] = 0.04f;
                    Slot.VelocityMps[1]    = 0.0f;
                    Slot.VelocityMps[0]   += (Slot.PositionMeters[0] - OriginCenter[0]) * 0.45f * DeltaSeconds;
                    Slot.VelocityMps[2]   += (Slot.PositionMeters[2] - OriginCenter[2]) * 0.45f * DeltaSeconds;
                }
            }
            else if (Spec.TransitionVariant == 5u) // Voxel blast radial explosion
            {
                const float Rx = Slot.PositionMeters[0] - OriginCenter[0];
                const float Ry = Slot.PositionMeters[1] - OriginCenter[1];
                const float Rz = Slot.PositionMeters[2] - OriginCenter[2];
                const float RL = std::sqrt(Rx * Rx + Ry * Ry + Rz * Rz) + 0.05f;
                Slot.VelocityMps[0] += (Rx / RL) * 6.5f * DeltaSeconds;
                Slot.VelocityMps[1] += (Ry / RL) * 6.5f * DeltaSeconds;
                Slot.VelocityMps[2] += (Rz / RL) * 6.5f * DeltaSeconds;
            }
            else // CubeDerez / Butterfly / Shatter upward drift
            {
                Slot.VelocityMps[1] += 2.2f * DeltaSeconds;
                Slot.VelocityMps[0] += std::sin(ElapsedSeconds * 4.0f + Slot.SeedScalar * 18.0f) * 1.4f * DeltaSeconds;
                Slot.VelocityMps[2] += std::cos(ElapsedSeconds * 3.7f + Slot.SeedScalar * 15.0f) * 1.4f * DeltaSeconds;
            }
        }
        else
        {
            // Hold crisp intact surface shape prior to derez sweep
            const float Hold = std::clamp(12.0f * DeltaSeconds, 0.0f, 1.0f);
            Slot.PositionMeters[0] += (Slot.OriginAnchor[0] - Slot.PositionMeters[0]) * Hold;
            Slot.PositionMeters[1] += (Slot.OriginAnchor[1] - Slot.PositionMeters[1]) * Hold;
            Slot.PositionMeters[2] += (Slot.OriginAnchor[2] - Slot.PositionMeters[2]) * Hold;
            Slot.VelocityMps[0] *= 0.8f; Slot.VelocityMps[1] *= 0.8f; Slot.VelocityMps[2] *= 0.8f;
        }
    }
}

void StepShieldBarrierDynamics(
    const ShieldBarrierSpec&       Spec,
    const float                    OriginCenter[3],
    const InteractiveColliderSpec& Collider,
    std::span<ParticleSlot>        Slots,
    float                          ElapsedSeconds,
    float                          DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    const float Spin  = ElapsedSeconds * Spec.SpinRateRad;
    const float Cs    = std::cos(Spin), Sn = std::sin(Spin);
    const float Radius = 2.0f;

    for (auto& Slot : Slots)
    {
        if (!QueryParticleSlotAlive(Slot)) continue;

        const float Ax = Slot.OriginAnchor[0] - OriginCenter[0];
        const float Ay = Slot.OriginAnchor[1] - OriginCenter[1];
        const float Az = Slot.OriginAnchor[2] - OriginCenter[2];

        // Rotate anchor cleanly on shield surface
        float Sx = OriginCenter[0] + Ax * Cs - Az * Sn;
        float Sy = OriginCenter[1] + Ay;
        float Sz = OriginCenter[2] + Ax * Sn + Az * Cs;

        // Project onto dome for SphereDome form
        if (Spec.Form == ShieldBarrierForm::SphereDome)
        {
            const float Dx = Sx - OriginCenter[0], Dy = Sy - OriginCenter[1], Dz = Sz - OriginCenter[2];
            const float L  = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) + 1e-4f;
            Sx = OriginCenter[0] + (Dx / L) * Radius;
            Sy = OriginCenter[1] + (Dy / L) * Radius;
            Sz = OriginCenter[2] + (Dz / L) * Radius;
        }

        // Hex/interference ripple from interactive ball impact
        float Ripple = 0.0f;
        if (Collider.BallActive)
        {
            const float Bx = Sx - Collider.BallCenterM[0];
            const float By = Sy - Collider.BallCenterM[1];
            const float Bz = Sz - Collider.BallCenterM[2];
            const float Dist = std::sqrt(Bx * Bx + By * By + Bz * Bz);
            Ripple = std::sin(Dist * 8.5f - ElapsedSeconds * 11.0f) * std::exp(-Dist * 1.15f);
        }

        Slot.PositionMeters[0] = Sx;
        Slot.PositionMeters[1] = Sy;
        Slot.PositionMeters[2] = Sz;
        Slot.VelocityMps[0]    = 0.0f; Slot.VelocityMps[1] = 0.0f; Slot.VelocityMps[2] = 0.0f;
        Slot.AuxScalar         = std::clamp(0.35f + 0.65f * std::fabs(Ripple), 0.0f, 1.0f);
    }
}

void StepSpiderwebWeaveDynamics(
    const SpiderwebWeaveSpec& Spec,
    std::span<ParticleSlot>   Slots,
    float                     ElapsedSeconds,
    float                     DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    const float ExtrudeProgress = std::clamp(ElapsedSeconds * 0.45f * std::max(0.1f, Spec.SpinRateScale), 0.05f, 1.0f);
    const float Spring = std::clamp(10.0f * DeltaSeconds, 0.0f, 1.0f);

    for (std::size_t I = 0u; I < Slots.size(); ++I)
    {
        ParticleSlot& Slot = Slots[I];
        if (!QueryParticleSlotAlive(Slot)) continue;

        const float StrandFrac = static_cast<float>(I) / static_cast<float>(std::max<std::size_t>(1u, Slots.size() - 1u));
        const float Reveal     = std::clamp((ExtrudeProgress - StrandFrac * 0.7f) * 3.0f, 0.0f, 1.0f);

        // Catenary silk sag + subtle breeze vibration
        const float Breeze = std::sin(ElapsedSeconds * 2.8f + Slot.OriginAnchor[0] * 1.5f) * 0.06f * Reveal;
        const float Sag    = -Spec.SilkSagMeters * std::sin(StrandFrac * Pi) * Reveal;

        Slot.PositionMeters[0] += (Slot.OriginAnchor[0] - Slot.PositionMeters[0]) * Spring;
        Slot.PositionMeters[1] += (Slot.OriginAnchor[1] + Sag - Slot.PositionMeters[1]) * Spring;
        Slot.PositionMeters[2] += (Slot.OriginAnchor[2] + Breeze - Slot.PositionMeters[2]) * Spring;
        Slot.AuxScalar          = Spec.DewHighlight * Reveal;
    }
}

void StepEffectorLatticeDynamics(
    const EffectorLatticeSpec&     Spec,
    const float                    OriginCenter[3],
    const InteractiveColliderSpec& Collider,
    std::span<ParticleSlot>        Slots,
    float                          ElapsedSeconds,
    float                          DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    const float Contrast = std::max(0.25f, Spec.FalloffContrast);
    const float Spring   = std::clamp(10.0f * DeltaSeconds, 0.0f, 1.0f);

    for (auto& Slot : Slots)
    {
        if (!QueryParticleSlotAlive(Slot)) continue;

        const float Ax = Slot.OriginAnchor[0];
        const float Ay = Slot.OriginAnchor[1];
        const float Az = Slot.OriginAnchor[2];

        float DriverVal = 0.0f;
        switch (Spec.Driver)
        {
            case EffectorDriverCategory::FluidSimplexWave:
                DriverVal = 0.5f + 0.5f * std::sin(Ax * 1.45f + ElapsedSeconds * 1.8f) * std::cos(Az * 1.45f - ElapsedSeconds * 1.35f);
                break;

            case EffectorDriverCategory::OrbitingDual:
            {
                EffectorVolumeDescriptor Orb1 = Spec.PrimaryVolume;
                Orb1.CenterM[0] = OriginCenter[0] + std::cos(ElapsedSeconds * 1.2f) * 2.2f;
                Orb1.CenterM[2] = OriginCenter[2] + std::sin(ElapsedSeconds * 1.2f) * 2.2f;
                EffectorVolumeDescriptor Orb2 = Spec.PrimaryVolume;
                Orb2.CenterM[0] = OriginCenter[0] - std::cos(ElapsedSeconds * 1.2f) * 2.2f;
                Orb2.CenterM[2] = OriginCenter[2] - std::sin(ElapsedSeconds * 1.2f) * 2.2f;
                DriverVal = std::max(EvaluateEffectorVolumeInfluence(Orb1, Ax, Ay, Az),
                                     EvaluateEffectorVolumeInfluence(Orb2, Ax, Ay, Az));
                break;
            }

            case EffectorDriverCategory::CurlIslands:
                DriverVal = 0.5f + 0.5f * std::sin(Ax * 2.1f + std::cos(Az * 1.8f + ElapsedSeconds)) * std::cos(Ay * 1.5f - ElapsedSeconds * 0.7f);
                break;

            case EffectorDriverCategory::ExpandingPulseRing:
            {
                const float R = std::sqrt((Ax - OriginCenter[0]) * (Ax - OriginCenter[0]) + (Az - OriginCenter[2]) * (Az - OriginCenter[2]));
                const float RingR = std::fmod(ElapsedSeconds * 1.85f, 4.8f);
                DriverVal = std::exp(-std::pow((R - RingR) * 1.85f, 2.0f));
                break;
            }

            case EffectorDriverCategory::BallForceFieldsOnly:
                if (Collider.BallActive)
                {
                    EffectorVolumeDescriptor BallVol { EffectorVolumeShape::Sphere, { Collider.BallCenterM[0], Collider.BallCenterM[1], Collider.BallCenterM[2] }, Collider.BallRadiusMeters * 2.4f };
                    DriverVal = EvaluateEffectorVolumeInfluence(BallVol, Ax, Ay, Az);
                }
                break;

            case EffectorDriverCategory::Placeable3DEffectors:
                DriverVal = std::max(EvaluateEffectorVolumeInfluence(Spec.PrimaryVolume, Ax, Ay, Az),
                                     EvaluateEffectorVolumeInfluence(Spec.SecondaryVolume, Ax, Ay, Az));
                break;
        }

        if (Spec.BlendPlaceable && Spec.Driver != EffectorDriverCategory::Placeable3DEffectors)
        {
            const float Manual = std::max(EvaluateEffectorVolumeInfluence(Spec.PrimaryVolume, Ax, Ay, Az),
                                          EvaluateEffectorVolumeInfluence(Spec.SecondaryVolume, Ax, Ay, Az));
            DriverVal = std::max(DriverVal, Manual);
        }

        const float Shaped = std::pow(std::clamp(DriverVal, 0.0f, 1.0f), Contrast);
        const float TargetY = Ay + Shaped * 0.95f;

        Slot.PositionMeters[0] += (Ax - Slot.PositionMeters[0]) * Spring;
        Slot.PositionMeters[1] += (TargetY - Slot.PositionMeters[1]) * Spring;
        Slot.PositionMeters[2] += (Az - Slot.PositionMeters[2]) * Spring;
        Slot.SizeMeters         = std::max(0.015f, (0.12f + 0.88f * Shaped) * 0.095f);
        Slot.AuxScalar          = Shaped;
    }
}

} // namespace Frontier::ParticleSpace
