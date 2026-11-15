/*============================================================================================================================================
                                                          DYNAMICPARAMETERBLOCK.CPP
============================================================================================================================================*/
// 🧩 Implementation of live 256-byte DynamicParameterBlock driver resolution, impact pulse queue & asset-agnostic bounds normalization

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Dynamic/DynamicParameterBlock.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace::Dynamic
{

namespace
{

[[nodiscard]] float ClampFinite(float Scalar, float MinBound, float MaxBound, float Fallback) noexcept
{
    if (!std::isfinite(Scalar))
    {
        return Fallback;
    }
    return std::clamp(Scalar, MinBound, MaxBound);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void ResolveDynamicParameters(
    DynamicParameterBlock&     TargetBlock,
    const DynamicDriverInputs& DriverInputs) noexcept
{
    const float PrevClock = TargetBlock.ClockSeconds;
    const float NextClock = static_cast<float>(std::max(0.0, DriverInputs.ElapsedSeconds));
    const float DeltaSec  = std::max(0.0f, NextClock - PrevClock);
    TargetBlock.ClockSeconds = NextClock;

    const auto Driver = static_cast<ParameterDriverMode>(TargetBlock.PrimaryDriverOrdinal);
    const float EffectiveTime = NextClock * ClampFinite(TargetBlock.AnimationSpeed, 0.0f, 8.0f, 1.0f);

    switch (Driver)
    {
        case ParameterDriverMode::ConstantScalar:
            break;

        case ParameterDriverMode::PlaybackClock:
            TargetBlock.TransitionProgress = std::fmod(EffectiveTime * 0.25f, 1.0f);
            break;

        case ParameterDriverMode::PingPongOscillation:
            TargetBlock.TransitionProgress = 0.5f + 0.5f * std::sin(EffectiveTime * 0.95f);
            break;

        case ParameterDriverMode::ExternalTimeline:
            if (DriverInputs.UseExternalProgress)
            {
                TargetBlock.TransitionProgress = std::clamp(DriverInputs.ExternalProgress, 0.0f, 1.0f);
            }
            break;

        case ParameterDriverMode::WorldImpactPulse:
            for (auto& Slot : TargetBlock.ImpactOriginsAge)
            {
                if (Slot[3] >= 0.0f)
                {
                    Slot[3] += DeltaSec * std::max(0.15f, TargetBlock.AnimationSpeed);
                    if (Slot[3] > 2.4f)
                    {
                        Slot[3] = std::fmod(Slot[3], 2.4f);
                    }
                }
            }
            break;

        case ParameterDriverMode::WeatherClimate:
            TargetBlock.WeatherRainIntensity = std::clamp(DriverInputs.GlobalRainIntensity, 0.0f, 1.0f);
            TargetBlock.WeatherSnowDepth     = std::clamp(DriverInputs.GlobalSnowDepth, 0.0f, 1.0f);
            break;

        case ParameterDriverMode::ParticleLatticeLink:
            TargetBlock.TransitionProgress = DriverInputs.UseExternalProgress
                ? std::clamp(DriverInputs.ExternalProgress, 0.0f, 1.0f)
                : (0.5f + 0.5f * std::sin(EffectiveTime * 0.85f));
            break;
    }

    SanitizeDynamicParameterBlock(TargetBlock);
}

void TriggerImpactPulse(
    DynamicParameterBlock& TargetBlock,
    Mathematics::Axis3D    NormalizedLocalHitDirection) noexcept
{
    const Mathematics::Axis3D UnitDir = Mathematics::NormalizeAxis(NormalizedLocalHitDirection, { 0.0f, 0.0f, 1.0f });

    // Pick either an inactive slot or the oldest active ripple slot
    std::size_t TargetSlot = 0u;
    float OldestAge = -2.0f;
    for (std::size_t Index = 0u; Index < TargetBlock.ImpactOriginsAge.size(); ++Index)
    {
        const float Age = TargetBlock.ImpactOriginsAge[Index][3];
        if (Age < 0.0f)
        {
            TargetSlot = Index;
            break;
        }
        if (Age > OldestAge)
        {
            OldestAge  = Age;
            TargetSlot = Index;
        }
    }

    TargetBlock.ImpactOriginsAge[TargetSlot] = { UnitDir.X, UnitDir.Y, UnitDir.Z, 0.02f };
}

void AssignAssetLocalBounds(
    DynamicParameterBlock& TargetBlock,
    Mathematics::Axis3D    BoundsMin,
    Mathematics::Axis3D    BoundsMax) noexcept
{
    const float MinX = std::min(BoundsMin.X, BoundsMax.X);
    const float MinY = std::min(BoundsMin.Y, BoundsMax.Y);
    const float MinZ = std::min(BoundsMin.Z, BoundsMax.Z);
    const float MaxX = std::max(BoundsMin.X, BoundsMax.X);
    const float MaxY = std::max(BoundsMin.Y, BoundsMax.Y);
    const float MaxZ = std::max(BoundsMin.Z, BoundsMax.Z);

    TargetBlock.AssetBoundsMinPad = { MinX, MinY, MinZ, 0.0f };
    TargetBlock.AssetBoundsMaxPad = {
        std::max(MaxX, MinX + 1.0e-3f),
        std::max(MaxY, MinY + 1.0e-3f),
        std::max(MaxZ, MinZ + 1.0e-3f),
        0.0f
    };
}

Mathematics::Axis3D NormalizeAssetCoordinate(
    const DynamicParameterBlock& Block,
    Mathematics::Axis3D          LocalPosition) noexcept
{
    const float CenterX = 0.5f * (Block.AssetBoundsMinPad[0] + Block.AssetBoundsMaxPad[0]);
    const float CenterY = 0.5f * (Block.AssetBoundsMinPad[1] + Block.AssetBoundsMaxPad[1]);
    const float CenterZ = 0.5f * (Block.AssetBoundsMinPad[2] + Block.AssetBoundsMaxPad[2]);

    const float HalfX = std::max(1.0e-3f, 0.5f * (Block.AssetBoundsMaxPad[0] - Block.AssetBoundsMinPad[0]));
    const float HalfY = std::max(1.0e-3f, 0.5f * (Block.AssetBoundsMaxPad[1] - Block.AssetBoundsMinPad[1]));
    const float HalfZ = std::max(1.0e-3f, 0.5f * (Block.AssetBoundsMaxPad[2] - Block.AssetBoundsMinPad[2]));
    const float UniformHalf = std::max({ HalfX, HalfY, HalfZ });

    return {
        (LocalPosition.X - CenterX) / UniformHalf,
        (LocalPosition.Y - CenterY) / UniformHalf,
        (LocalPosition.Z - CenterZ) / UniformHalf
    };
}

void SanitizeDynamicParameterBlock(DynamicParameterBlock& TargetBlock) noexcept
{
    TargetBlock.ClockSeconds         = ClampFinite(TargetBlock.ClockSeconds,         0.0f,  1.0e6f, 0.0f);
    TargetBlock.AnimationSpeed       = ClampFinite(TargetBlock.AnimationSpeed,       0.0f,  8.0f,   1.0f);
    TargetBlock.TransitionProgress   = ClampFinite(TargetBlock.TransitionProgress,   0.0f,  1.0f,   0.5f);
    TargetBlock.DetailScale          = ClampFinite(TargetBlock.DetailScale,          0.25f, 32.0f,  4.0f);
    TargetBlock.ReliefStrength       = ClampFinite(TargetBlock.ReliefStrength,       0.0f,  2.5f,   0.65f);
    TargetBlock.OpacityScale         = ClampFinite(TargetBlock.OpacityScale,         0.0f,  1.0f,   1.0f);
    TargetBlock.WeatherRainIntensity = ClampFinite(TargetBlock.WeatherRainIntensity, 0.0f,  1.0f,   0.5f);
    TargetBlock.WeatherSnowDepth     = ClampFinite(TargetBlock.WeatherSnowDepth,     0.0f,  1.0f,   0.0f);
    TargetBlock.OpticalIor           = ClampFinite(TargetBlock.OpticalIor,           1.0f,  3.0f,   1.5f);
    TargetBlock.ThinFilmNanometers   = ClampFinite(TargetBlock.ThinFilmNanometers,   0.0f,  2000.0f,460.0f);
    TargetBlock.RoughnessBias        = ClampFinite(TargetBlock.RoughnessBias,       -0.6f,  0.6f,   0.0f);
    TargetBlock.MetalnessBias        = ClampFinite(TargetBlock.MetalnessBias,       -0.6f,  0.6f,   0.0f);
    TargetBlock.SeamGlowIntensity    = ClampFinite(TargetBlock.SeamGlowIntensity,    0.0f,  6.0f,   1.85f);
    TargetBlock.WaveAmplitude        = ClampFinite(TargetBlock.WaveAmplitude,        0.0f,  2.0f,   0.55f);
    TargetBlock.TurbulenceAmount     = ClampFinite(TargetBlock.TurbulenceAmount,     0.0f,  2.0f,   0.50f);
    TargetBlock.ThinFilmSpread       = ClampFinite(TargetBlock.ThinFilmSpread,       0.0f,  600.0f, 120.0f);

    for (uint32_t C = 0u; C < 3u; ++C)
    {
        TargetBlock.PrimaryColorRgba[C]   = ClampFinite(TargetBlock.PrimaryColorRgba[C],   0.0f, 1.0f, 0.1f);
        TargetBlock.SecondaryColorRgba[C] = ClampFinite(TargetBlock.SecondaryColorRgba[C], 0.0f, 1.0f, 0.5f);
        TargetBlock.TertiaryColorRgba[C]  = ClampFinite(TargetBlock.TertiaryColorRgba[C],  0.0f, 1.0f, 0.8f);
        TargetBlock.AccentRadianceRgba[C] = ClampFinite(TargetBlock.AccentRadianceRgba[C], 0.0f, 4.0f, 0.5f);
    }
    TargetBlock.AccentRadianceRgba[3] = ClampFinite(TargetBlock.AccentRadianceRgba[3], 0.0f, 12.0f, 1.5f);
}

} // namespace Frontier::MaterialSpace::Dynamic
