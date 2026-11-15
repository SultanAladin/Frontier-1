/*============================================================================================================================================
                                                             TURBULENCEFIELD.CPP
============================================================================================================================================*/
// 🧩 Divergence-free ∇×(w·ψ) CurlNoise, AtmosphericGust, VorticityConfinement, and deterministic BrownianNoise laws

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/TurbulenceField.h"
#include "Engine/SimulationSpace/ForceFields/VectorField.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::SimulationSpace::ForceFields
{

namespace
{

[[nodiscard]] constexpr uint32_t HashMix32(uint32_t X) noexcept
{
    X ^= X >> 16u;
    X *= 0x7FEB352Du;
    X ^= X >> 15u;
    X *= 0x846CA68Bu;
    X ^= X >> 16u;
    return X;
}

[[nodiscard]] constexpr float HashFloat01(uint32_t X) noexcept
{
    return static_cast<float>(HashMix32(X) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

// Smooth C^∞ trigonometric 3D vector potential ψ(p) whose exact analytical derivatives give identically zero divergence
struct VectorPotentialSample
{
    SpatialVector3 Potential {};
    SpatialVector3 AnalyticalCurl {};
};

[[nodiscard]] VectorPotentialSample EvaluateSmoothPotentialCurl(
    const SpatialVector3& P,
    float                 ScaleMeters,
    uint32_t              OctaveCount,
    uint32_t              Seed,
    double                TimeSeconds) noexcept
{
    struct WaveMode
    {
        float Kx, Ky, Kz;
        float PhaseOffset;
        uint32_t ComponentMask;
    };

    // Orthogonal irrational-ratio wave vectors for isotropic turbulence
    constexpr std::array<WaveMode, 6> Modes = {{
        {  1.000f,  0.618f, -0.382f, 0.41f, 0u },
        { -0.465f,  1.000f,  0.754f, 1.73f, 1u },
        {  0.577f, -0.707f,  1.000f, 2.89f, 2u },
        { -0.819f,  0.414f,  0.523f, 4.12f, 0u },
        {  0.331f,  0.887f, -0.612f, 5.04f, 1u },
        { -0.642f, -0.519f,  0.911f, 0.95f, 2u },
    }};

    const uint32_t ClampedOctaves = std::clamp(OctaveCount, 1u, 5u);
    const float    BaseFreq       = Constants::TauFloat / std::max(0.5f, ScaleMeters);
    const float    SeedPhase      = HashFloat01(Seed) * Constants::TauFloat;
    const float    TimeDrift      = static_cast<float>(TimeSeconds) * 0.85f;

    // Accumulate potential ψ = (ψx, ψy, ψz) and Jacobian partials ∂ψi/∂xj
    SpatialVector3 Psi{};
    float dPsiX_dy = 0.0f, dPsiX_dz = 0.0f;
    float dPsiY_dx = 0.0f, dPsiY_dz = 0.0f;
    float dPsiZ_dx = 0.0f, dPsiZ_dy = 0.0f;

    float Amplitude = 1.0f;
    float FreqMul   = 1.0f;

    for (uint32_t Octave = 0u; Octave < ClampedOctaves; ++Octave)
    {
        for (std::size_t MIdx = 0u; MIdx < Modes.size(); ++MIdx)
        {
            const WaveMode& Mode  = Modes[MIdx];
            const float     Freq  = BaseFreq * FreqMul;
            const float     Kx    = Mode.Kx * Freq;
            const float     Ky    = Mode.Ky * Freq;
            const float     Kz    = Mode.Kz * Freq;
            const float     Phase = Mode.PhaseOffset + SeedPhase + static_cast<float>(Octave) * 1.618f +
                                    TimeDrift * (0.65f + 0.25f * static_cast<float>(MIdx % 3u));

            const float Arg = Kx * P.X + Ky * P.Y + Kz * P.Z + Phase;
            const float S   = Amplitude * std::sin(Arg);
            const float C   = Amplitude * std::cos(Arg);

            const uint32_t Comp = (Mode.ComponentMask + Octave) % 3u;
            if (Comp == 0u)
            {
                Psi.X    += S;
                dPsiX_dy += C * Ky;
                dPsiX_dz += C * Kz;
            }
            else if (Comp == 1u)
            {
                Psi.Y    += S;
                dPsiY_dx += C * Kx;
                dPsiY_dz += C * Kz;
            }
            else
            {
                Psi.Z    += S;
                dPsiZ_dx += C * Kx;
                dPsiZ_dy += C * Ky;
            }
        }
        Amplitude *= 0.5f;
        FreqMul   *= 2.03f;
    }

    VectorPotentialSample Out{};
    Out.Potential      = Psi;
    Out.AnalyticalCurl = {
        dPsiZ_dy - dPsiY_dz,
        dPsiX_dz - dPsiZ_dx,
        dPsiY_dx - dPsiX_dy,
        0.0f,
    };
    return Out;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           PUBLIC TURBULENCE FIELD FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SpatialVector3 EvaluateDivergenceFreeCurlPotential(
    const SpatialVector3& LocalMeters,
    float                 ScaleMeters,
    uint32_t              OctaveCount,
    uint32_t              Seed,
    double                TimeSeconds) noexcept
{
    return EvaluateSmoothPotentialCurl(LocalMeters, ScaleMeters, OctaveCount, Seed, TimeSeconds).AnalyticalCurl;
}

SpatialForceSample EvaluateTurbulenceModel(
    const ForceSourceRecordEntry& Entry,
    const SpatialVector3&         WorldPoint,
    const SpatialVector3&         BodyVelocity,
    double                        TimeSeconds) noexcept
{
    (void)BodyVelocity;

    SpatialForceSample Output{};
    if (!Entry.Enabled)
    {
        return Output;
    }

    const BasisVectors3  Basis  = ConstructOrthonormalBasisFromEuler(Entry.EulerDegrees);
    const SpatialVector3 Anchor = Entry.DynamicMotion ? Entry.EvaluatedCentreMeters : Entry.CentreMeters;
    const SpatialVector3 Delta  = WorldPoint - Anchor;

    float UnusedWave     = 0.0f;
    const float Temporal = EvaluateTemporalModulation(Entry, TimeSeconds, UnusedWave);

    if (Entry.Model == ForceModel::CurlNoise)
    {
        // Exact analytical ∇ × (w(r) * ψ(p)) so divergence ∇·v ≡ 0 across the entire bounded volume
        const float SafeR = std::max(0.25f, Entry.RadiusMeters);
        const float DistSq = VectorLengthSquared(Delta);
        const float U2     = DistSq / (SafeR * SafeR);
        if (U2 >= 1.0f)
        {
            return Output;
        }

        const float W = (Entry.Attenuation == ForceAttenuationMode::None)
                            ? 1.0f
                            : std::pow(1.0f - U2, 3.0f);

        const float dWdU2 = (Entry.Attenuation == ForceAttenuationMode::None)
                                ? 0.0f
                                : -3.0f * (1.0f - U2) * (1.0f - U2);
        const float ScaleGrad = (2.0f * dWdU2) / (SafeR * SafeR);
        const SpatialVector3 GradW = Delta * ScaleGrad;

        const VectorPotentialSample Pot = EvaluateSmoothPotentialCurl(
            Delta,
            Entry.TurbulenceScaleMeters,
            Entry.OctaveCount,
            Entry.Seed,
            TimeSeconds);

        // ∇ × (w ψ) = w (∇ × ψ) + (∇w) × ψ
        const SpatialVector3 ExactDivFree = Pot.AnalyticalCurl * W + CrossProduct(GradW, Pot.Potential);
        const float          NormScale    = Entry.Strength * Temporal * std::max(0.5f, Entry.TurbulenceScaleMeters * 0.14f);
        Output.AdvectionVelocity          = ExactDivFree * NormScale;
        return Output;
    }

    const float Atten = EvaluateSpatialAttenuation(Entry, Basis, WorldPoint);
    if (Atten <= 1.0e-6f)
    {
        return Output;
    }

    const float Scale = Entry.Strength * Atten * Temporal;
    switch (Entry.Model)
    {
        case ForceModel::AtmosphericGust:
        {
            const float Wave1 = std::sin(DotProduct(Delta, Basis.Forward) * 0.14f -
                                         static_cast<float>(TimeSeconds) * Entry.FrequencyHertz * Constants::TauFloat);
            const float Wave2 = std::cos(DotProduct(Delta, Basis.Left) * 0.21f +
                                         static_cast<float>(TimeSeconds) * Entry.FrequencyHertz * 3.7f);
            const float GustEnv = 0.45f + 0.55f * std::max(0.0f, 0.6f * Wave1 + 0.4f * Wave2);
            const SpatialVector3 Swirl = EvaluateDivergenceFreeCurlPotential(
                Delta,
                Entry.TurbulenceScaleMeters,
                std::min(Entry.OctaveCount, 3u),
                Entry.Seed,
                TimeSeconds);
            Output.AdvectionVelocity = Basis.Forward * (Scale * GustEnv) + Swirl * (Scale * 0.25f);
            break;
        }

        case ForceModel::VorticityConfinement:
        {
            const VectorPotentialSample Pot = EvaluateSmoothPotentialCurl(
                Delta,
                Entry.TurbulenceScaleMeters,
                Entry.OctaveCount,
                Entry.Seed,
                TimeSeconds);
            const float VortMag        = VectorLength(Pot.AnalyticalCurl);
            const SpatialVector3 Axis  = (VortMag > 1.0e-4f) ? (Pot.AnalyticalCurl / VortMag) : Basis.Up;
            Output.AdvectionVelocity   = Pot.AnalyticalCurl * (Scale * 0.35f);
            Output.LinearAcceleration  = CrossProduct(Axis, NormalizeSafe(Delta, Basis.Forward)) * (Scale * 0.6f);
            Output.AngularTorque       = Axis * (Scale * 0.2f);
            break;
        }

        case ForceModel::BrownianNoise:
        default:
        {
            const float TimeBin = std::floor(static_cast<float>(TimeSeconds) * std::max(1.0f, Entry.FrequencyHertz * 12.0f));
            const uint32_t Key  = HashMix32(Entry.Seed ^ static_cast<uint32_t>(TimeBin * 2654435761.0f) ^
                                            static_cast<uint32_t>(std::lround(Delta.X * 1.7f) * 73856093) ^
                                            static_cast<uint32_t>(std::lround(Delta.Y * 1.7f) * 19349663) ^
                                            static_cast<uint32_t>(std::lround(Delta.Z * 1.7f) * 83492791));
            const float Jx = HashFloat01(Key + 11u) * 2.0f - 1.0f;
            const float Jy = HashFloat01(Key + 29u) * 2.0f - 1.0f;
            const float Jz = HashFloat01(Key + 47u) * 2.0f - 1.0f;
            Output.AdvectionVelocity  = { Jx * Scale, Jy * Scale, Jz * Scale, 0.0f };
            Output.LinearAcceleration = { Jx * Scale * 0.4f, Jy * Scale * 0.4f, Jz * Scale * 0.4f, 0.0f };
            break;
        }
    }

    return Output;
}

} // namespace Frontier::SimulationSpace::ForceFields
