/*============================================================================================================================================
                                                             SPATIALACOUSTICS.H
============================================================================================================================================*/
// 🧩 Frontier RHR Z-up 3D spatial solver: surround VBAP (Mono/Stereo/Binaural/5.1/7.1), physical Doppler, tunnel coupling & occlusion

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/AudioSpace/AcousticTypes.h"

#include <array>
#include <cstdint>
#include <span>

namespace Frontier::Components
{
struct CameraPosture;
}

namespace Frontier::SpatialAcceleration
{
class WideBoundingTree;
}

namespace Frontier::SimulationSpace::PhysicsSpace
{
class PhysicsExtension;
}

namespace Frontier::AudioSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RayAcousticFieldReading
{
    float OcclusionFactor        = 0.0f; // [0-1] - BVH4 ray barrier obstruction between receiver and origin
    float EnclosureTunnelMix     = 0.0f; // [0-1] - Estimated architectural confinement around receiver
    float EffectiveDiameterMeters = 6.0f; // [m]   - Estimated local corridor diameter from orthogonal BVH4 ray distances
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class SpatialAcoustics
{
public:
    [[nodiscard]] static float EvaluateDistanceGain(
        float                          DistanceMeters,
        const AcousticVoiceDescriptor& Descriptor) noexcept;

    [[nodiscard]] static float EvaluateDopplerPitchRatio(
        const AcousticReceiverPose& Receiver,
        Mathematics::Axis3D         OriginPositionMeters,
        Mathematics::Axis3D         OriginVelocityMps,
        float                       DopplerFactor) noexcept;

    [[nodiscard]] static float SmoothDopplerPitchRatio(
        float CurrentRatio,
        float TargetRatio,
        float BlockDurationSeconds,
        float SlewTimeSeconds = Constants::DopplerSlewTimeSeconds) noexcept;

    [[nodiscard]] static float EvaluateAirAbsorptionHighCutHz(
        float DistanceMeters,
        float AbsorptionStrength,
        bool  SubmergedCondition) noexcept;

    [[nodiscard]] static float EvaluateOcclusionHighCutHz(
        float OcclusionFactor,
        bool  SubmergedCondition) noexcept;

    static void EvaluateSpeakerPanningGains(
        SpatialSpeakerLayout                                      Layout,
        float                                                     AzimuthRadians,
        float                                                     ElevationRadians,
        float                                                     SpatialBlend,
        float                                                     SpatialSpreadRadians,
        std::array<float, Constants::MaxAcousticOutputChannels>&  OutSpeakerGains,
        float&                                                    OutLfeGain) noexcept;

    [[nodiscard]] static float EvaluateBinauralItdSamplePoints(
        float    AzimuthRadians,
        float    ElevationRadians,
        uint32_t SampleRateHz) noexcept;

    [[nodiscard]] static float EvaluateBinauralHeadShadowCutoffHz(
        float AzimuthRadians,
        bool  RightEarChannel) noexcept;

    [[nodiscard]] static float EvaluateZoneMembership(
        const AcousticZoneDescriptor& Zone,
        Mathematics::Axis3D           PointMeters,
        Mathematics::Axis3D*          OutNearestPortalMeters = nullptr) noexcept;

    [[nodiscard]] static AcousticSpatialSolution SolveSpatialAcoustics(
        const AcousticReceiverPose&              Receiver,
        const AcousticVoiceDescriptor&           Descriptor,
        SpatialSpeakerLayout                     Layout,
        AcousticEnvironmentPreset                FallbackPreset,
        std::span<const AcousticZoneDescriptor>  Zones) noexcept;

    // Frontier engine integrations
    [[nodiscard]] static AcousticReceiverPose BuildReceiverPoseFromCamera(
        const Components::CameraPosture& Posture) noexcept;

    [[nodiscard]] static float EvaluatePhysicsOcclusion(
        const SimulationSpace::PhysicsSpace::PhysicsExtension& Physics,
        Mathematics::Axis3D                                    ReceiverPositionMeters,
        Mathematics::Axis3D                                    OriginPositionMeters,
        uint32_t                                               CollisionChannelMask = 0xFFFFFFFFu) noexcept;

    [[nodiscard]] static RayAcousticFieldReading EvaluateRayAcousticField(
        const SpatialAcceleration::WideBoundingTree& SceneTree,
        Mathematics::Axis3D                          ReceiverPositionMeters,
        Mathematics::Axis3D                          OriginPositionMeters) noexcept;
};

} // namespace Frontier::AudioSpace
