/*============================================================================================================================================
                                                            SPATIALACOUSTICS.CPP
============================================================================================================================================*/
// 🧩 Implementation of 3D VBAP surround panning, physical Doppler pitch shift, Woodworth binaural cues, tunnel zones & BVH4/Physics occlusion

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/AudioSpace/SpatialAcoustics.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <algorithm>
#include <cmath>

namespace Frontier::AudioSpace
{

namespace
{

[[nodiscard]] float Dot3(Mathematics::Axis3D A, Mathematics::Axis3D B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

[[nodiscard]] float LengthSquared3(Mathematics::Axis3D V) noexcept
{
    return V.X * V.X + V.Y * V.Y + V.Z * V.Z;
}

[[nodiscard]] Mathematics::Axis3D Cross3(Mathematics::Axis3D A, Mathematics::Axis3D B) noexcept
{
    return {
        A.Y * B.Z - A.Z * B.Y,
        A.Z * B.X - A.X * B.Z,
        A.X * B.Y - A.Y * B.X,
    };
}

[[nodiscard]] Mathematics::Axis3D NormalizeOrFallback(Mathematics::Axis3D V, Mathematics::Axis3D Fallback) noexcept
{
    const float LenSq = LengthSquared3(V);
    if (LenSq <= 1.0e-12f)
    {
        return Fallback;
    }
    const float InvLen = 1.0f / std::sqrt(LenSq);
    return { V.X * InvLen, V.Y * InvLen, V.Z * InvLen };
}

[[nodiscard]] float SmoothStep01(float Edge0, float Edge1, float X) noexcept
{
    if (Edge1 <= Edge0)
    {
        return (X >= Edge1) ? 1.0f : 0.0f;
    }
    const float T = std::clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

struct RingSpeakerSector
{
    uint32_t ChannelIndex = 0u;
    float    AngleRadians = 0.0f;
};

template <std::size_t RingCount>
void EvaluateRingVbap(
    const std::array<RingSpeakerSector, RingCount>& OrderedRing,
    uint32_t                                        TotalChannels,
    float                                           AzimuthRadians,
    float                                           SpreadBlend,
    std::array<float, Constants::MaxAcousticOutputChannels>& OutGains) noexcept
{
    OutGains.fill(0.0f);

    float WrappedAz = AzimuthRadians;
    while (WrappedAz > Constants::PiFloat)
    {
        WrappedAz -= Constants::TauFloat;
    }
    while (WrappedAz <= -Constants::PiFloat)
    {
        WrappedAz += Constants::TauFloat;
    }

    uint32_t LeftCh   = OrderedRing[RingCount - 1u].ChannelIndex;
    uint32_t RightCh  = OrderedRing[0].ChannelIndex;
    float    AngleA   = OrderedRing[RingCount - 1u].AngleRadians - Constants::TauFloat;
    float    AngleB   = OrderedRing[0].AngleRadians;
    float    WrappedT = WrappedAz;

    if (WrappedAz >= OrderedRing[RingCount - 1u].AngleRadians)
    {
        AngleA   = OrderedRing[RingCount - 1u].AngleRadians;
        AngleB   = OrderedRing[0].AngleRadians + Constants::TauFloat;
        WrappedT = WrappedAz;
    }
    else if (WrappedAz < OrderedRing[0].AngleRadians)
    {
        AngleA   = OrderedRing[RingCount - 1u].AngleRadians - Constants::TauFloat;
        AngleB   = OrderedRing[0].AngleRadians;
        WrappedT = WrappedAz;
    }
    else
    {
        for (std::size_t Index = 0u; Index + 1u < RingCount; ++Index)
        {
            if (WrappedAz >= OrderedRing[Index].AngleRadians && WrappedAz <= OrderedRing[Index + 1u].AngleRadians)
            {
                LeftCh   = OrderedRing[Index].ChannelIndex;
                RightCh  = OrderedRing[Index + 1u].ChannelIndex;
                AngleA   = OrderedRing[Index].AngleRadians;
                AngleB   = OrderedRing[Index + 1u].AngleRadians;
                WrappedT = WrappedAz;
                break;
            }
        }
    }

    const float Span       = std::max(1.0e-5f, AngleB - AngleA);
    const float Alpha      = std::clamp((WrappedT - AngleA) / Span, 0.0f, 1.0f);
    const float GainA      = std::cos(Alpha * 0.5f * Constants::PiFloat);
    const float GainB      = std::sin(Alpha * 0.5f * Constants::PiFloat);
    OutGains[LeftCh]      += GainA;
    OutGains[RightCh]     += GainB;

    if (SpreadBlend > 1.0e-5f)
    {
        const float OmniWeight = 1.0f / std::sqrt(static_cast<float>(RingCount));
        for (const RingSpeakerSector& Speaker : OrderedRing)
        {
            const float PointGain = OutGains[Speaker.ChannelIndex];
            OutGains[Speaker.ChannelIndex] = PointGain * (1.0f - SpreadBlend) + OmniWeight * SpreadBlend;
        }
    }

    // Constant-power re-normalization across non-LFE speakers
    float EnergySum = 0.0f;
    for (uint32_t Ch = 0u; Ch < TotalChannels; ++Ch)
    {
        if (Ch == 3u && TotalChannels >= 6u)
        {
            continue;
        }
        EnergySum += OutGains[Ch] * OutGains[Ch];
    }
    if (EnergySum > 1.0e-6f)
    {
        const float InvNorm = 1.0f / std::sqrt(EnergySum);
        for (uint32_t Ch = 0u; Ch < TotalChannels; ++Ch)
        {
            if (Ch == 3u && TotalChannels >= 6u)
            {
                continue;
            }
            OutGains[Ch] *= InvNorm;
        }
    }
}

[[nodiscard]] float ResolvePresetTunnelBaseline(AcousticEnvironmentPreset Preset) noexcept
{
    switch (Preset)
    {
        case AcousticEnvironmentPreset::TunnelTube:     return 0.68f;
        case AcousticEnvironmentPreset::ConcreteHangar: return 0.14f;
        case AcousticEnvironmentPreset::Cavern:         return 0.24f;
        case AcousticEnvironmentPreset::OpenField:      return 0.0f;
        case AcousticEnvironmentPreset::Underwater:     return 0.05f;
    }
    return 0.0f;
}

[[nodiscard]] float ResolvePresetReverbBaseline(AcousticEnvironmentPreset Preset) noexcept
{
    switch (Preset)
    {
        case AcousticEnvironmentPreset::OpenField:      return 0.06f;
        case AcousticEnvironmentPreset::TunnelTube:     return 0.42f;
        case AcousticEnvironmentPreset::ConcreteHangar: return 0.36f;
        case AcousticEnvironmentPreset::Cavern:         return 0.56f;
        case AcousticEnvironmentPreset::Underwater:     return 0.32f;
    }
    return 0.06f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  ACOUSTIC CALCULATIONS
//------------------------------------------------------------------------------------------------------------------------

float SpatialAcoustics::EvaluateDistanceGain(
    float                          DistanceMeters,
    const AcousticVoiceDescriptor& Descriptor) noexcept
{
    if (Descriptor.SpatialBlend <= 0.001f)
    {
        return 1.0f;
    }

    const float RefDist  = std::max(0.05f, Descriptor.ReferenceDistanceMeters);
    const float MaxDist  = std::max(RefDist + 0.1f, Descriptor.MaximumDistanceMeters);
    const float ClampedD = std::clamp(DistanceMeters, RefDist, MaxDist);
    const float Rolloff  = std::max(0.05f, Descriptor.RolloffExponent);

    float SpatialGain = 1.0f;
    switch (Descriptor.AttenuationCurve)
    {
        case DistanceAttenuationCurve::InverseClamped:
        {
            SpatialGain = RefDist / (RefDist + Rolloff * (ClampedD - RefDist));
            if (DistanceMeters >= MaxDist)
            {
                SpatialGain = 0.0f;
            }
            else if (DistanceMeters > MaxDist * 0.85f)
            {
                const float Fade = 1.0f - SmoothStep01(MaxDist * 0.85f, MaxDist, DistanceMeters);
                SpatialGain *= Fade;
            }
            break;
        }
        case DistanceAttenuationCurve::Logarithmic:
        {
            const float Ratio = ClampedD / RefDist;
            SpatialGain       = std::pow(Ratio, -Rolloff);
            const float Fade  = 1.0f - SmoothStep01(MaxDist * 0.85f, MaxDist, DistanceMeters);
            SpatialGain      *= Fade;
            break;
        }
        case DistanceAttenuationCurve::Linear:
        {
            SpatialGain = 1.0f - (ClampedD - RefDist) / (MaxDist - RefDist);
            break;
        }
    }

    SpatialGain = std::clamp(SpatialGain, 0.0f, 1.0f);
    return (1.0f - Descriptor.SpatialBlend) + Descriptor.SpatialBlend * SpatialGain;
}

float SpatialAcoustics::EvaluateDopplerPitchRatio(
    const AcousticReceiverPose& Receiver,
    Mathematics::Axis3D         OriginPositionMeters,
    Mathematics::Axis3D         OriginVelocityMps,
    float                       DopplerFactor) noexcept
{
    if (DopplerFactor <= 1.0e-5f)
    {
        return 1.0f;
    }

    const Mathematics::Axis3D Delta = OriginPositionMeters - Receiver.PositionMeters;
    const float               DistSq = LengthSquared3(Delta);
    if (DistSq <= 1.0e-8f)
    {
        return 1.0f;
    }

    const float               InvDist        = 1.0f / std::sqrt(DistSq);
    const Mathematics::Axis3D RadialToOrigin = { Delta.X * InvDist, Delta.Y * InvDist, Delta.Z * InvDist };

    const float SoundSpeedMps = Receiver.SubmergedCondition
                                    ? Constants::WaterSoundSpeedMps
                                    : Constants::DryAirSoundSpeedMps;

    const float Scale           = std::clamp(DopplerFactor, 0.0f, 2.0f);
    const float ReceiverRadial  = Dot3(Receiver.VelocityMps, RadialToOrigin) * Scale;
    const float OriginRadial    = Dot3(OriginVelocityMps, RadialToOrigin) * Scale;

    const float FloorSpeed  = SoundSpeedMps * Constants::AcousticSpeedClampFloorRatio;
    const float Numerator   = std::max(FloorSpeed * 0.5f, SoundSpeedMps + ReceiverRadial);
    const float Denominator = std::max(FloorSpeed, SoundSpeedMps + OriginRadial);

    return std::clamp(
        Numerator / Denominator,
        Constants::MinimumDopplerPitchRatio,
        Constants::MaximumDopplerPitchRatio);
}

float SpatialAcoustics::SmoothDopplerPitchRatio(
    float CurrentRatio,
    float TargetRatio,
    float BlockDurationSeconds,
    float SlewTimeSeconds) noexcept
{
    if (SlewTimeSeconds <= 1.0e-6f || BlockDurationSeconds <= 0.0f)
    {
        return TargetRatio;
    }
    const float Alpha = 1.0f - std::exp(-BlockDurationSeconds / SlewTimeSeconds);
    return CurrentRatio + Alpha * (TargetRatio - CurrentRatio);
}

float SpatialAcoustics::EvaluateAirAbsorptionHighCutHz(
    float DistanceMeters,
    float AbsorptionStrength,
    bool  SubmergedCondition) noexcept
{
    if (SubmergedCondition)
    {
        return 450.0f;
    }
    const float SafeDist = std::max(0.0f, DistanceMeters);
    const float Rate     = std::max(0.0f, AbsorptionStrength) * 0.018f;
    return std::clamp(20000.0f * std::exp(-Rate * SafeDist), 1200.0f, 20000.0f);
}

float SpatialAcoustics::EvaluateOcclusionHighCutHz(
    float OcclusionFactor,
    bool  SubmergedCondition) noexcept
{
    const float ClampedOcc = std::clamp(OcclusionFactor, 0.0f, 1.0f);
    const float CutoffHz   = 20000.0f * std::pow(0.025f, ClampedOcc);
    return SubmergedCondition ? std::min(CutoffHz, 450.0f) : std::clamp(CutoffHz, 450.0f, 20000.0f);
}

void SpatialAcoustics::EvaluateSpeakerPanningGains(
    SpatialSpeakerLayout                                      Layout,
    float                                                     AzimuthRadians,
    float                                                     ElevationRadians,
    float                                                     SpatialBlend,
    float                                                     SpatialSpreadRadians,
    std::array<float, Constants::MaxAcousticOutputChannels>&  OutSpeakerGains,
    float&                                                    OutLfeGain) noexcept
{
    OutSpeakerGains.fill(0.0f);
    OutLfeGain = 0.0f;

    const float ElevOmniBlend  = SmoothStep01(0.65f, 1.45f, std::abs(ElevationRadians)) * 0.70f;
    const float SpreadBlend    = std::clamp(SpatialSpreadRadians / Constants::PiFloat + ElevOmniBlend, 0.0f, 1.0f);
    const float SpatialWeight  = std::clamp(SpatialBlend, 0.0f, 1.0f);

    switch (Layout)
    {
        case SpatialSpeakerLayout::Mono:
        {
            OutSpeakerGains[0] = 1.0f;
            break;
        }
        case SpatialSpeakerLayout::Stereo:
        case SpatialSpeakerLayout::BinauralHrtf:
        {
            const float LeftProjection = std::sin(AzimuthRadians); // +1 = pure left, -1 = pure right
            const float PanParam       = 0.5f * (1.0f - LeftProjection * SpatialWeight);
            const float Angle          = PanParam * Constants::HalfPi;
            float       LeftGain       = std::cos(static_cast<float>(Angle));
            float       RightGain      = std::sin(static_cast<float>(Angle));
            if (SpreadBlend > 1.0e-5f)
            {
                constexpr float CenterIso = 0.70710678f;
                LeftGain  = LeftGain * (1.0f - SpreadBlend) + CenterIso * SpreadBlend;
                RightGain = RightGain * (1.0f - SpreadBlend) + CenterIso * SpreadBlend;
            }
            const float Norm = std::sqrt(std::max(1.0e-6f, LeftGain * LeftGain + RightGain * RightGain));
            OutSpeakerGains[0] = LeftGain / Norm;
            OutSpeakerGains[1] = RightGain / Norm;
            break;
        }
        case SpatialSpeakerLayout::Surround51:
        {
            // Ascending angle order around Frontier Z-up RHR (+left, -right): SR(-110), FR(-30), C(0), FL(+30), SL(+110)
            constexpr std::array<RingSpeakerSector, 5u> Ring51 = {{
                { 5u, -110.0f * Constants::DegToRadFloat },
                { 1u,  -30.0f * Constants::DegToRadFloat },
                { 2u,    0.0f                            },
                { 0u,   30.0f * Constants::DegToRadFloat },
                { 4u,  110.0f * Constants::DegToRadFloat },
            }};
            std::array<float, Constants::MaxAcousticOutputChannels> RingGains{};
            EvaluateRingVbap(Ring51, 6u, AzimuthRadians, SpreadBlend, RingGains);

            constexpr float Stereo2D = 0.70710678f;
            OutSpeakerGains[0] = (1.0f - SpatialWeight) * Stereo2D + SpatialWeight * RingGains[0];
            OutSpeakerGains[1] = (1.0f - SpatialWeight) * Stereo2D + SpatialWeight * RingGains[1];
            OutSpeakerGains[2] = SpatialWeight * RingGains[2];
            OutSpeakerGains[3] = 0.0f;
            OutSpeakerGains[4] = SpatialWeight * RingGains[4];
            OutSpeakerGains[5] = SpatialWeight * RingGains[5];
            OutLfeGain         = 0.25f * SpatialWeight;
            break;
        }
        case SpatialSpeakerLayout::Surround71:
        {
            // Ascending angle order: RR(-145), SR(-90), FR(-30), C(0), FL(+30), SL(+90), RL(+145)
            constexpr std::array<RingSpeakerSector, 7u> Ring71 = {{
                { 7u, -145.0f * Constants::DegToRadFloat },
                { 5u,  -90.0f * Constants::DegToRadFloat },
                { 1u,  -30.0f * Constants::DegToRadFloat },
                { 2u,    0.0f                            },
                { 0u,   30.0f * Constants::DegToRadFloat },
                { 4u,   90.0f * Constants::DegToRadFloat },
                { 6u,  145.0f * Constants::DegToRadFloat },
            }};
            std::array<float, Constants::MaxAcousticOutputChannels> RingGains{};
            EvaluateRingVbap(Ring71, 8u, AzimuthRadians, SpreadBlend, RingGains);

            constexpr float Stereo2D = 0.70710678f;
            OutSpeakerGains[0] = (1.0f - SpatialWeight) * Stereo2D + SpatialWeight * RingGains[0];
            OutSpeakerGains[1] = (1.0f - SpatialWeight) * Stereo2D + SpatialWeight * RingGains[1];
            OutSpeakerGains[2] = SpatialWeight * RingGains[2];
            OutSpeakerGains[3] = 0.0f;
            OutSpeakerGains[4] = SpatialWeight * RingGains[4];
            OutSpeakerGains[5] = SpatialWeight * RingGains[5];
            OutSpeakerGains[6] = SpatialWeight * RingGains[6];
            OutSpeakerGains[7] = SpatialWeight * RingGains[7];
            OutLfeGain         = 0.25f * SpatialWeight;
            break;
        }
    }
}

float SpatialAcoustics::EvaluateBinauralItdSamplePoints(
    float    AzimuthRadians,
    float    ElevationRadians,
    uint32_t SampleRateHz) noexcept
{
    // Woodworth spherical-head ITD formula projected onto horizontal ear axis: positive => delay right ear (sound on left)
    const float CosElev        = std::cos(ElevationRadians);
    const float HorizontalProj = std::clamp(std::sin(AzimuthRadians) * CosElev, -1.0f, 1.0f);
    const float Theta          = std::asin(std::abs(HorizontalProj));
    const float Sign           = (HorizontalProj >= 0.0f) ? 1.0f : -1.0f;
    const float DeltaMeters    = Constants::HumanHeadRadiusMeters * (Theta + std::sin(Theta));
    const float DelaySeconds   = (DeltaMeters / Constants::DryAirSoundSpeedMps) * Sign;
    return DelaySeconds * static_cast<float>(SampleRateHz);
}

float SpatialAcoustics::EvaluateBinauralHeadShadowCutoffHz(
    float AzimuthRadians,
    bool  RightEarChannel) noexcept
{
    // Sound on left (+Azimuth) shadows the right ear; sound behind also gets pinna treble rolloff
    const float LateralSin  = std::sin(AzimuthRadians);
    const float ShadowRatio = RightEarChannel ? std::max(0.0f, LateralSin) : std::max(0.0f, -LateralSin);
    const float RearFactor  = std::max(0.0f, -std::cos(AzimuthRadians));
    const float EarCutoff   = 18000.0f * std::pow(0.16f, ShadowRatio) * (1.0f - 0.30f * RearFactor);
    return std::clamp(EarCutoff, 1600.0f, 18000.0f);
}

float SpatialAcoustics::EvaluateZoneMembership(
    const AcousticZoneDescriptor& Zone,
    Mathematics::Axis3D           PointMeters,
    Mathematics::Axis3D*          OutNearestPortalMeters) noexcept
{
    const float Feather = std::max(0.05f, Zone.FeatherDistanceMeters);

    switch (Zone.Shape)
    {
        case AcousticZoneShapeCategory::Sphere:
        {
            const float Dist = std::sqrt(LengthSquared3(PointMeters - Zone.CenterMeters));
            if (OutNearestPortalMeters != nullptr)
            {
                const Mathematics::Axis3D Dir = NormalizeOrFallback(PointMeters - Zone.CenterMeters, { 1.0f, 0.0f, 0.0f });
                *OutNearestPortalMeters       = Zone.CenterMeters + Dir * Zone.RadiusMeters;
            }
            return 1.0f - SmoothStep01(std::max(0.0f, Zone.RadiusMeters - Feather), Zone.RadiusMeters + Feather, Dist);
        }
        case AcousticZoneShapeCategory::AxisAlignedBox:
        {
            const Mathematics::Axis3D Rel = PointMeters - Zone.CenterMeters;
            const float Dx = std::abs(Rel.X) - Zone.HalfExtentsMeters.X;
            const float Dy = std::abs(Rel.Y) - Zone.HalfExtentsMeters.Y;
            const float Dz = std::abs(Rel.Z) - Zone.HalfExtentsMeters.Z;
            const float OutsideDist = std::sqrt(
                std::max(0.0f, Dx) * std::max(0.0f, Dx) +
                std::max(0.0f, Dy) * std::max(0.0f, Dy) +
                std::max(0.0f, Dz) * std::max(0.0f, Dz));
            const float SignedDist = (OutsideDist > 0.0f) ? OutsideDist : std::max({ Dx, Dy, Dz });
            if (OutNearestPortalMeters != nullptr)
            {
                *OutNearestPortalMeters = {
                    std::clamp(PointMeters.X, Zone.CenterMeters.X - Zone.HalfExtentsMeters.X, Zone.CenterMeters.X + Zone.HalfExtentsMeters.X),
                    std::clamp(PointMeters.Y, Zone.CenterMeters.Y - Zone.HalfExtentsMeters.Y, Zone.CenterMeters.Y + Zone.HalfExtentsMeters.Y),
                    std::clamp(PointMeters.Z, Zone.CenterMeters.Z - Zone.HalfExtentsMeters.Z, Zone.CenterMeters.Z + Zone.HalfExtentsMeters.Z),
                };
            }
            return 1.0f - SmoothStep01(-Feather * 0.5f, Feather, SignedDist);
        }
        case AcousticZoneShapeCategory::TunnelCorridor:
        {
            const Mathematics::Axis3D AxisVec = Zone.TunnelEndMeters - Zone.TunnelStartMeters;
            const float               LenSq   = LengthSquared3(AxisVec);
            if (LenSq <= 1.0e-8f)
            {
                return 0.0f;
            }
            const float               LengthM = std::sqrt(LenSq);
            const Mathematics::Axis3D UnitDir = AxisVec * (1.0f / LengthM);
            const Mathematics::Axis3D ToPt    = PointMeters - Zone.TunnelStartMeters;
            const float               AxialM  = Dot3(ToPt, UnitDir);

            const float               ClampedAxial = std::clamp(AxialM, 0.0f, LengthM);
            const Mathematics::Axis3D ClosestAxis  = Zone.TunnelStartMeters + UnitDir * ClampedAxial;
            const float               RadialDistM  = std::sqrt(LengthSquared3(PointMeters - ClosestAxis));

            const float AxialOutside = (AxialM < 0.0f) ? (-AxialM) : ((AxialM > LengthM) ? (AxialM - LengthM) : 0.0f);
            const float AxialWeight  = 1.0f - SmoothStep01(0.0f, Feather, AxialOutside);
            const float RadialWeight = 1.0f - SmoothStep01(std::max(0.0f, Zone.RadiusMeters - Feather * 0.4f), Zone.RadiusMeters + Feather * 0.6f, RadialDistM);

            if (OutNearestPortalMeters != nullptr)
            {
                *OutNearestPortalMeters = (AxialM <= 0.5f * LengthM) ? Zone.TunnelStartMeters : Zone.TunnelEndMeters;
            }
            return std::clamp(AxialWeight * RadialWeight, 0.0f, 1.0f);
        }
    }

    return 0.0f;
}

AcousticSpatialSolution SpatialAcoustics::SolveSpatialAcoustics(
    const AcousticReceiverPose&              Receiver,
    const AcousticVoiceDescriptor&           Descriptor,
    SpatialSpeakerLayout                     Layout,
    AcousticEnvironmentPreset                FallbackPreset,
    std::span<const AcousticZoneDescriptor>  Zones) noexcept
{
    AcousticSpatialSolution Solution{};

    // Orthonormalize receiver basis safely
    const Mathematics::Axis3D Forward = NormalizeOrFallback(Receiver.ForwardAxis, { 1.0f, 0.0f, 0.0f });
    const Mathematics::Axis3D UpRaw   = NormalizeOrFallback(Receiver.UpAxis, { 0.0f, 0.0f, 1.0f });
    const Mathematics::Axis3D Left    = NormalizeOrFallback(Cross3(UpRaw, Forward), NormalizeOrFallback(Receiver.LeftAxis, { 0.0f, 1.0f, 0.0f }));
    const Mathematics::Axis3D Up      = NormalizeOrFallback(Cross3(Forward, Left), UpRaw);

    // Resolve strongest active acoustic zone coupling (supports interior resonance + portal escape radiation)
    float                      BestPriorityWeight = -1.0f;
    const AcousticZoneDescriptor* BestZone        = nullptr;
    float                      BestReceiverWeight = 0.0f;
    float                      BestOriginWeight   = 0.0f;
    Mathematics::Axis3D        PortalAnchor       = Descriptor.PositionMeters;

    for (const AcousticZoneDescriptor& Zone : Zones)
    {
        Mathematics::Axis3D ReceiverPortalCandidate{};
        const float WRecv   = EvaluateZoneMembership(Zone, Receiver.PositionMeters, &ReceiverPortalCandidate);
        const float WOrigin = EvaluateZoneMembership(Zone, Descriptor.PositionMeters, nullptr);
        const float WJoint  = std::max(WRecv, WOrigin);
        if (WJoint > 1.0e-4f)
        {
            const float Score = static_cast<float>(Zone.Priority) + WJoint;
            if (Score > BestPriorityWeight)
            {
                BestPriorityWeight = Score;
                BestZone           = &Zone;
                BestReceiverWeight = WRecv;
                BestOriginWeight   = WOrigin;
                PortalAnchor       = ReceiverPortalCandidate;
            }
        }
    }

    bool EffectiveSubmerged = Receiver.SubmergedCondition || (FallbackPreset == AcousticEnvironmentPreset::Underwater);
    float PresetTunnelMix   = ResolvePresetTunnelBaseline(FallbackPreset);
    float PresetReverbSend  = ResolvePresetReverbBaseline(FallbackPreset);
    Mathematics::Axis3D EffectiveOriginPos = Descriptor.PositionMeters;

    if (BestZone != nullptr)
    {
        const float ZoneBlend = std::clamp(0.65f * BestReceiverWeight + 0.35f * BestOriginWeight, 0.0f, 1.0f);
        if (BestZone->EnvironmentPreset == AcousticEnvironmentPreset::Underwater && BestReceiverWeight > 0.5f)
        {
            EffectiveSubmerged = true;
        }

        if (BestZone->Shape == AcousticZoneShapeCategory::TunnelCorridor)
        {
            // Portal blend when origin is inside tunnel while receiver stands outside looking toward tunnel mouth
            if (BestOriginWeight > 0.35f && BestReceiverWeight < 0.25f)
            {
                const float PortalBlend = std::clamp((BestOriginWeight - BestReceiverWeight) * 0.75f, 0.0f, 0.75f);
                Solution.PortalBlendFactor = PortalBlend;
                EffectiveOriginPos = {
                    Descriptor.PositionMeters.X * (1.0f - PortalBlend) + PortalAnchor.X * PortalBlend,
                    Descriptor.PositionMeters.Y * (1.0f - PortalBlend) + PortalAnchor.Y * PortalBlend,
                    Descriptor.PositionMeters.Z * (1.0f - PortalBlend) + PortalAnchor.Z * PortalBlend,
                };
            }

            const float SameTubeCoupling = std::max(BestReceiverWeight, BestOriginWeight * 0.65f);
            Solution.TunnelMix           = std::clamp(BestZone->TunnelResonanceMix * SameTubeCoupling, 0.0f, 1.0f);
            Solution.ReverbSend          = std::clamp(BestZone->ReverbWetMix * std::max(ZoneBlend, 0.25f), 0.0f, 1.0f);
            Solution.TunnelWidthMeters   = std::max(0.5f, BestZone->CrossSectionWidthMeters);
            Solution.TunnelHeightMeters  = std::max(0.5f, BestZone->CrossSectionHeightMeters);
            Solution.TunnelLengthMeters  = std::max(2.0f, std::sqrt(LengthSquared3(BestZone->TunnelEndMeters - BestZone->TunnelStartMeters)));
            Solution.TunnelWallReflectivity = std::clamp(BestZone->WallReflectivity, 0.0f, 0.97f);
            Solution.TunnelWallDampingHz = std::clamp(BestZone->HighFrequencyDampingHz, 400.0f, 18000.0f);
            Solution.WithinTunnelCorridor = (Solution.TunnelMix > 0.05f);
        }
        else
        {
            const float ZoneTunnel = (BestZone->EnvironmentPreset == AcousticEnvironmentPreset::TunnelTube)
                                         ? BestZone->TunnelResonanceMix * ZoneBlend
                                         : 0.0f;
            Solution.TunnelMix  = std::max(PresetTunnelMix * (1.0f - ZoneBlend), ZoneTunnel);
            Solution.ReverbSend = std::max(PresetReverbSend * (1.0f - ZoneBlend), BestZone->ReverbWetMix * ZoneBlend);
            Solution.WithinTunnelCorridor = (Solution.TunnelMix > 0.05f);
        }
    }
    else
    {
        Solution.TunnelMix            = PresetTunnelMix;
        Solution.ReverbSend           = PresetReverbSend;
        Solution.WithinTunnelCorridor = (Solution.TunnelMix > 0.05f);
    }

    Solution.SubmergedActive = EffectiveSubmerged;

    const Mathematics::Axis3D Delta      = EffectiveOriginPos - Receiver.PositionMeters;
    const float               DistanceM  = std::sqrt(LengthSquared3(Delta));
    const Mathematics::Axis3D UnitToOrig = NormalizeOrFallback(Delta, Forward);

    const float LocalFront = Dot3(UnitToOrig, Forward);
    const float LocalLeft  = Dot3(UnitToOrig, Left);
    const float LocalUp    = std::clamp(Dot3(UnitToOrig, Up), -1.0f, 1.0f);

    Solution.BinauralAzimuthRadians   = (DistanceM > 1.0e-4f) ? std::atan2(LocalLeft, LocalFront) : 0.0f;
    Solution.BinauralElevationRadians = (DistanceM > 1.0e-4f) ? std::asin(LocalUp) : 0.0f;

    AcousticReceiverPose EffectiveReceiver = Receiver;
    EffectiveReceiver.SubmergedCondition   = EffectiveSubmerged;

    Solution.DistanceGain = EvaluateDistanceGain(DistanceM, Descriptor);
    Solution.DopplerPitchTarget = EvaluateDopplerPitchRatio(
        EffectiveReceiver,
        Descriptor.PositionMeters,
        Descriptor.VelocityMps,
        Descriptor.DopplerFactor);

    const float PortalAttenOcclusion = std::clamp(
        Descriptor.OcclusionFactor + Solution.PortalBlendFactor * 0.35f,
        0.0f,
        1.0f);
    Solution.OcclusionHighCutHz     = EvaluateOcclusionHighCutHz(PortalAttenOcclusion, EffectiveSubmerged);
    Solution.AirAbsorptionHighCutHz = EvaluateAirAbsorptionHighCutHz(
        DistanceM,
        Descriptor.AirAbsorptionStrength,
        EffectiveSubmerged);

    // Automatically widen near-field sounds inside ReferenceDistance to avoid hard L/R head snapping
    const float NearFieldSpread = (DistanceM < Descriptor.ReferenceDistanceMeters && Descriptor.SpatialBlend > 0.0f)
                                      ? Constants::PiFloat * 0.45f * (1.0f - DistanceM / std::max(0.05f, Descriptor.ReferenceDistanceMeters))
                                      : 0.0f;
    const float EffectiveSpread = std::max(Descriptor.SpatialSpreadRadians, NearFieldSpread);

    EvaluateSpeakerPanningGains(
        Layout,
        Solution.BinauralAzimuthRadians,
        Solution.BinauralElevationRadians,
        Descriptor.SpatialBlend,
        EffectiveSpread,
        Solution.SpeakerGains,
        Solution.LfeGain);

    // Direct-path barrier attenuation scales loudness softly while leaving diffuse reverb tail perceptible
    const float DirectPathBlockFactor = 1.0f - 0.72f * std::clamp(Descriptor.OcclusionFactor, 0.0f, 1.0f);
    Solution.DistanceGain            *= DirectPathBlockFactor;
    Solution.EffectiveAudibility      = std::max(0.0f, Descriptor.NominalGain * Solution.DistanceGain);

    return Solution;
}

AcousticReceiverPose SpatialAcoustics::BuildReceiverPoseFromCamera(const Components::CameraPosture& Posture) noexcept
{
    AcousticReceiverPose Pose{};
    Pose.PositionMeters = Posture.CurrentEyePosition;

    const float DeltaT = (Posture.HasPreviousFrame && Posture.LastDeltaSeconds > 1.0e-5f)
                             ? Posture.LastDeltaSeconds
                             : 0.0f;
    if (DeltaT > 0.0f)
    {
        Pose.VelocityMps = (Posture.CurrentEyePosition - Posture.PreviousEyePosition) * (1.0f / DeltaT);
    }

    Pose.ForwardAxis = NormalizeOrFallback(Posture.ForwardAxis, { 0.0f, 1.0f, 0.0f });
    Pose.UpAxis      = NormalizeOrFallback(Posture.UpAxis, { 0.0f, 0.0f, 1.0f });
    Pose.LeftAxis    = NormalizeOrFallback(Cross3(Pose.UpAxis, Pose.ForwardAxis), { -Posture.RightAxis.X, -Posture.RightAxis.Y, -Posture.RightAxis.Z });
    return Pose;
}

float SpatialAcoustics::EvaluatePhysicsOcclusion(
    const SimulationSpace::PhysicsSpace::PhysicsExtension& Physics,
    Mathematics::Axis3D                                    ReceiverPositionMeters,
    Mathematics::Axis3D                                    OriginPositionMeters,
    uint32_t                                               CollisionChannelMask) noexcept
{
    const Mathematics::Axis3D Delta = OriginPositionMeters - ReceiverPositionMeters;
    const float               Dist  = std::sqrt(LengthSquared3(Delta));
    if (Dist <= 0.25f)
    {
        return 0.0f;
    }

    SimulationSpace::PhysicsSpace::RayCastQuery Query{};
    Query.OriginMeters      = ReceiverPositionMeters;
    Query.DirectionUnit     = Delta * (1.0f / Dist);
    Query.MaxDistanceMeters = std::max(0.05f, Dist - 0.15f);
    Query.ChannelMask       = CollisionChannelMask;

    const SimulationSpace::PhysicsSpace::RayCastHitReading Hit = Physics.ExecuteRayCast(Query);
    if (!Hit.HitDetected)
    {
        return 0.0f;
    }

    // Closer obstruction causes stronger direct-path occlusion [0.45..0.92]
    const float ClampedFrac = std::clamp(Hit.Fraction, 0.0f, 1.0f);
    return std::clamp(0.45f + 0.47f * (1.0f - ClampedFrac), 0.0f, 0.92f);
}

RayAcousticFieldReading SpatialAcoustics::EvaluateRayAcousticField(
    const SpatialAcceleration::WideBoundingTree& SceneTree,
    Mathematics::Axis3D                          ReceiverPositionMeters,
    Mathematics::Axis3D                          OriginPositionMeters) noexcept
{
    RayAcousticFieldReading Reading{};

    const Mathematics::Axis3D Delta = OriginPositionMeters - ReceiverPositionMeters;
    const float               Dist  = std::sqrt(LengthSquared3(Delta));
    if (Dist > 0.25f)
    {
        const Mathematics::Axis3D UnitDir = Delta * (1.0f / Dist);
        SpatialAcceleration::RaySegmentQuery Ray {};
        Ray.Origin          = ReceiverPositionMeters;
        Ray.UnitDirection   = UnitDir;
        Ray.MinimumDistance = 0.05f;
        Ray.MaximumDistance = std::max(0.1f, Dist - 0.1f);
        const auto Hit      = SceneTree.TraceClosestRay(Ray, false);
        Reading.OcclusionFactor = Hit.HitConfirmed
            ? std::clamp(0.55f + 0.35f * (1.0f - Hit.HitDistanceMeters / std::max(0.25f, Dist)), 0.0f, 0.92f)
            : 0.0f;
    }

    // Query 6 orthogonal axes around receiver via BVH4 rays to detect corridor/tunnel wall enclosure
    constexpr std::array<Mathematics::Axis3D, 6u> CardinalDirs = {{
        {  1.0f,  0.0f,  0.0f },
        { -1.0f,  0.0f,  0.0f },
        {  0.0f,  1.0f,  0.0f },
        {  0.0f, -1.0f,  0.0f },
        {  0.0f,  0.0f,  1.0f },
        {  0.0f,  0.0f, -1.0f },
    }};

    float EnclosedAxesCount = 0.0f;
    float SpanSumMeters     = 0.0f;
    int   SpanPairCount     = 0;

    std::array<float, 6u> ClearDistances {};
    for (std::size_t DirIdx = 0u; DirIdx < CardinalDirs.size(); ++DirIdx)
    {
        SpatialAcceleration::RaySegmentQuery AxisRay {};
        AxisRay.Origin          = ReceiverPositionMeters;
        AxisRay.UnitDirection   = CardinalDirs[DirIdx];
        AxisRay.MinimumDistance = 0.05f;
        AxisRay.MaximumDistance = 16.0f;
        const auto Hit          = SceneTree.TraceClosestRay(AxisRay, false);
        ClearDistances[DirIdx]  = Hit.HitConfirmed ? std::min(Hit.HitDistanceMeters, 16.0f) : 16.0f;
        if (ClearDistances[DirIdx] < 12.0f)
        {
            EnclosedAxesCount += 1.0f;
        }
    }

    for (std::size_t Pair = 0u; Pair < 3u; ++Pair)
    {
        const float PairSpan = ClearDistances[Pair * 2u] + ClearDistances[Pair * 2u + 1u];
        if (PairSpan < 24.0f)
        {
            SpanSumMeters += PairSpan;
            ++SpanPairCount;
        }
    }

    Reading.EnclosureTunnelMix      = SmoothStep01(2.0f, 5.0f, EnclosedAxesCount) * 0.85f;
    Reading.EffectiveDiameterMeters = (SpanPairCount > 0)
                                          ? std::clamp(SpanSumMeters / static_cast<float>(SpanPairCount), 1.0f, 24.0f)
                                          : 12.0f;
    return Reading;
}

} // namespace Frontier::AudioSpace
