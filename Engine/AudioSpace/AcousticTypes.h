/*============================================================================================================================================
                                                              ACOUSTICTYPES.H
============================================================================================================================================*/
// 🧩 Generational acoustic tokens, spatial layouts, voice descriptors, tunnel zones, lock-free envelopes, and telemetry metrics

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/AcousticConstants.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <array>
#include <cstdint>

namespace Frontier::AudioSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class SpatialSpeakerLayout : uint8_t
{
    Mono         = 0u, // [-] - Single centered mono sum channel
    Stereo       = 1u, // [-] - Front-left / front-right constant-power pair
    BinauralHrtf = 2u, // [-] - Headphone ITD/ILD Woodworth + pinna notch pair
    Surround51   = 3u, // [-] - 6 channels: FL, FR, C, LFE, SL, SR
    Surround71   = 4u, // [-] - 8 channels: FL, FR, C, LFE, SL, SR, RL, RR
};

enum class AudioBusCategory : uint8_t
{
    Master    = 0u, // [-] - Final stereo/surround master summing bus
    Ambience  = 1u, // [-] - World wind, room tone, and continuous environmental beds
    Effects   = 2u, // [-] - Collisions, debris, footsteps, and foley bursts
    Weapons   = 3u, // [-] - High-priority weapon discharge and impact transients
    Vehicles  = 4u, // [-] - Continuous RPM, turbine, and tyre slip drives
    Dialogue  = 5u, // [-] - Speech and radio comms (side-chain ducks Ambience)
    Interface = 6u, // [-] - Non-spatialized 2D UI clicks and confirmations
};

enum class PlaybackCycleCategory : uint8_t
{
    OneShotTrigger   = 0u, // [-] - Self-terminating event trigger with optional variation & jitter
    ContinuousLoop   = 1u, // [-] - Seamless phase-continuous loop with equal-power splice crossfade
    DrivenContinuous = 2u, // [-] - Continuous loop driven live by physics RPM/speed parameters
};

enum class DistanceAttenuationCurve : uint8_t
{
    InverseClamped = 0u, // [-] - Physical 1/r rolloff clamped below ReferenceDistanceMeters
    Logarithmic    = 1u, // [-] - Smooth dB-linear perceptual rolloff
    Linear         = 2u, // [-] - Strict linear ramp reaching zero at MaximumDistanceMeters
};

enum class AcousticEnvironmentPreset : uint8_t
{
    OpenField      = 0u, // [-] - Minimal early reflections, crisp air attenuation
    TunnelTube     = 1u, // [-] - Cylindrical waveguide comb modes, longitudinal slapback & treble damping
    ConcreteHangar = 2u, // [-] - Wide diffuse FDN decay with bright reflective tail
    Cavern         = 3u, // [-] - Long low-damped geological reverberation tail
    Underwater     = 4u, // [-] - 1482 m/s sound speed with resonant 450 Hz submersion high-cut filter
};

enum class AcousticZoneShapeCategory : uint8_t
{
    Sphere         = 0u, // [-] - Radial spherical environment volume
    AxisAlignedBox = 1u, // [-] - Axis-aligned architectural enclosure box
    TunnelCorridor = 2u, // [-] - Cylindrical tube segment with entrance/exit portal blending
};

enum class AcousticCommandOp : uint8_t
{
    TriggerVoice               = 0u, // [-] - Allocate and start an acoustic voice from descriptor
    ReleaseVoice               = 1u, // [-] - Trigger smooth ADSR release tail on active voice
    StopVoiceImmediate         = 2u, // [-] - Immediately silence and reclaim active voice slot
    AssignVoiceTransform       = 3u, // [-] - Update world position, velocity, and occlusion of voice
    AssignVoiceContinuousDrive = 4u, // [-] - Drive continuous loop gain, pitch, and occlusion
    AssignBusGain              = 5u, // [-] - Set linear gain target for an AudioBusCategory
    AssignBusMute              = 6u, // [-] - Toggle mute status for an AudioBusCategory
    AssignReceiverPose         = 7u, // [-] - Update acoustic receiver position, velocity, and axes
    AssignEnvironmentPreset    = 8u, // [-] - Set global fallback AcousticEnvironmentPreset
    AssignSpeakerLayout        = 9u, // [-] - Switch live SpatialSpeakerLayout geometry
};

enum class AudioOutputDriverCategory : uint8_t
{
    AutoSelect     = 0u, // [-] - Detect platform native endpoint then fall back to HeadlessBuffer
    HeadlessBuffer = 1u, // [-] - Deterministic offline/CI ring sink with WAV export
    WindowsWasapi  = 2u, // [-] - Native Windows IAudioClient float32 shared-mode endpoint
    LinuxAlsa      = 3u, // [-] - Dynamically loaded libasound.so.2 PCM endpoint
    LinuxPulse     = 4u, // [-] - Dynamically loaded libpulse-simple.so.0 endpoint
    AndroidAaudio  = 5u, // [-] - Dynamically loaded libaaudio.so low-latency endpoint
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WaveformToken
{
    uint32_t SlotIndex  = 0u; // [idx] - 1-based slot index (0 indicates invalid token)
    uint32_t Generation = 0u; // [gen] - Monotonic generation guard against stale reuse

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return SlotIndex != 0u;
    }

    [[nodiscard]] constexpr bool operator==(const WaveformToken&) const noexcept = default;
};

struct AcousticVoiceToken
{
    uint32_t SlotIndex  = 0u; // [idx] - 1-based voice slot index (0 indicates invalid token)
    uint32_t Generation = 0u; // [gen] - Monotonic generation guard against stolen/reclaimed slots

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return SlotIndex != 0u;
    }

    [[nodiscard]] constexpr bool operator==(const AcousticVoiceToken&) const noexcept = default;
};

struct AcousticZoneToken
{
    uint32_t SlotIndex  = 0u; // [idx] - 1-based zone slot index (0 indicates invalid token)
    uint32_t Generation = 0u; // [gen] - Monotonic generation guard

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return SlotIndex != 0u;
    }

    [[nodiscard]] constexpr bool operator==(const AcousticZoneToken&) const noexcept = default;
};

struct AcousticReceiverPose
{
    Mathematics::Axis3D PositionMeters     { 0.0f, 0.0f, 0.0f }; // [m]   - Receiver acoustic center position (RHR +Z up)
    Mathematics::Axis3D VelocityMps        { 0.0f, 0.0f, 0.0f }; // [m/s] - Receiver world velocity for Doppler calculation
    Mathematics::Axis3D ForwardAxis        { 1.0f, 0.0f, 0.0f }; // [-]   - Unit forward gaze axis (+X default)
    Mathematics::Axis3D LeftAxis           { 0.0f, 1.0f, 0.0f }; // [-]   - Unit left ear axis (+Y default)
    Mathematics::Axis3D UpAxis             { 0.0f, 0.0f, 1.0f }; // [-]   - Unit cranial up axis (+Z default)
    bool                SubmergedCondition = false;              // [-]   - True when receiver sits below water surface
};

struct AcousticVoiceDescriptor
{
    std::array<WaveformToken, Constants::MaxVoiceVariationSlots> WaveformSlots {}; // [id] - Up to 4 variation waveform tokens
    uint32_t                 VariationCount            = 1u;     // [count] - Valid variation entries in WaveformSlots [1..4]
    AudioBusCategory         Bus                       = AudioBusCategory::Effects;
    PlaybackCycleCategory  PlaybackCycle                   = PlaybackCycleCategory::OneShotTrigger;
    DistanceAttenuationCurve AttenuationCurve          = DistanceAttenuationCurve::InverseClamped;
    Mathematics::Axis3D      PositionMeters            { 0.0f, 0.0f, 0.0f }; // [m]   - Initial world origin position
    Mathematics::Axis3D      VelocityMps               { 0.0f, 0.0f, 0.0f }; // [m/s] - Initial world origin velocity
    float                    NominalGain                  = 1.0f;               // [-]   - Linear nominal cue gain
    float                    NominalPitch                 = 1.0f;               // [-]   - Nominal pitch multiplier
    float                    PitchJitterRange          = 0.0f;               // [-]   - Symmetric random pitch jitter (+/-)
    float                    GainJitterRange           = 0.0f;               // [-]   - Symmetric random linear gain jitter (+/-)
    float                    DopplerFactor             = 1.0f;               // [-]   - Physical Doppler strength [0 = off, 1 = 100%]
    float                    SpatialBlend              = 1.0f;               // [0-1] - 0 = non-spatialized 2D, 1 = full 3D surround
    float                    SpatialSpreadRadians      = 0.15f;              // [rad] - Angular panning width across neighboring channels
    float                    ReferenceDistanceMeters   = 2.0f;               // [m]   - Distance at which unity attenuation holds
    float                    MaximumDistanceMeters     = 120.0f;             // [m]   - Maximum audible distance / virtualization boundary
    float                    RolloffExponent           = 1.0f;               // [-]   - Rolloff slope factor for distance curve
    float                    OcclusionFactor           = 0.0f;               // [0-1] - Direct-path barrier obstruction [0 = open, 1 = blocked]
    float                    AirAbsorptionStrength     = 1.0f;               // [-]   - High-frequency atmospheric damping multiplier
    float                    AttackSeconds             = 0.005f;             // [s]   - Fade-in attack ramp duration
    float                    ReleaseSeconds            = 0.045f;             // [s]   - Fade-out release tail duration
    uint32_t                 LoopCrossfadeSamplePoints = 512u;               // [count] - Equal-power loop wrap splice sample points
    uint8_t                  Priority                  = 128u;               // [-]   - Voice stealing priority (255 = highest)
    bool                     VirtualizeWhenQuiet       = true;               // [-]   - Advance phase silently while out of active mix budget
};

struct AcousticZoneDescriptor
{
    AcousticZoneShapeCategory Shape                    = AcousticZoneShapeCategory::TunnelCorridor;
    AcousticEnvironmentPreset EnvironmentPreset        = AcousticEnvironmentPreset::TunnelTube;
    Mathematics::Axis3D       CenterMeters             { 0.0f, 0.0f, 0.0f };   // [m]   - Center for Sphere / AxisAlignedBox
    Mathematics::Scalar3D     HalfExtentsMeters        { 8.0f, 4.0f, 3.0f };   // [m]   - Half extents for AxisAlignedBox
    Mathematics::Axis3D       TunnelStartMeters        { -20.0f, 0.0f, 0.0f }; // [m]   - Tunnel corridor portal A anchor
    Mathematics::Axis3D       TunnelEndMeters          { 20.0f, 0.0f, 0.0f };  // [m]   - Tunnel corridor portal B anchor
    float                     RadiusMeters             = 4.0f;                 // [m]   - Sphere or cylindrical tube radius
    float                     FeatherDistanceMeters    = 3.5f;                 // [m]   - Smooth portal transition zone width
    float                     CrossSectionWidthMeters  = 6.0f;                 // [m]   - Transverse width driving comb mode 1-2
    float                     CrossSectionHeightMeters = 4.5f;                 // [m]   - Vertical height driving comb mode 3-4
    float                     WallReflectivity         = 0.84f;                // [0-1] - Tunnel wall acoustic reflection coefficient
    float                     TunnelResonanceMix       = 0.72f;                // [0-1] - Wet waveguide resonance blend
    float                     ReverbWetMix             = 0.42f;                // [0-1] - FDN diffuse reverberation wet blend
    float                     ReverbDecaySeconds       = 2.1f;                 // [s]   - Target RT60 reverberation decay time
    float                     HighFrequencyDampingHz   = 3200.0f;              // [Hz]  - Wall high-frequency absorption cutoff
    uint8_t                   Priority                 = 100u;                 // [-]   - Resolution tie-breaker when overlapping zones
};

struct AcousticSpatialSolution
{
    std::array<float, Constants::MaxAcousticOutputChannels> SpeakerGains {}; // [-] - Per-channel linear VBAP/stereo/binaural gains
    float LfeGain                    = 0.0f;                      // [-]   - Low-frequency effects send for 5.1/7.1
    float DopplerPitchTarget         = 1.0f;                      // [-]   - Instantaneous Doppler pitch multiplier
    float DistanceGain               = 1.0f;                      // [-]   - Distance attenuation linear multiplier
    float OcclusionHighCutHz         = 20000.0f;                  // [Hz]  - Barrier low-pass filter cutoff
    float AirAbsorptionHighCutHz     = 20000.0f;                  // [Hz]  - Atmospheric high-frequency damping cutoff
    float TunnelMix                  = 0.0f;                      // [0-1] - Effective tunnel waveguide send ratio
    float ReverbSend                 = 0.0f;                      // [0-1] - Diffuse FDN reverberation send ratio
    float EffectiveAudibility        = 0.0f;                      // [-]   - Combined perceptual loudness for voice sorting
    float BinauralAzimuthRadians     = 0.0f;                      // [rad] - Signed horizontal azimuth (+left, -right)
    float BinauralElevationRadians   = 0.0f;                      // [rad] - Signed vertical elevation (+up, -down)
    float PortalBlendFactor          = 0.0f;                      // [0-1] - Tunnel mouth radiation factor when source inside & receiver outside
    float TunnelWidthMeters          = 6.0f;                      // [m]   - Effective corridor width from coupled zone
    float TunnelHeightMeters         = 4.5f;                      // [m]   - Effective corridor height from coupled zone
    float TunnelLengthMeters         = 40.0f;                     // [m]   - Effective corridor length from coupled zone
    float TunnelWallReflectivity     = 0.84f;                     // [0-1] - Coupled wall reflection factor
    float TunnelWallDampingHz        = 3200.0f;                   // [Hz]  - Coupled wall absorption cutoff
    bool  SubmergedActive            = false;                     // [-]   - True if underwater propagation physics apply
    bool  WithinTunnelCorridor       = false;                     // [-]   - True if voice/receiver shares tunnel waveguide coupling
};

struct AcousticCommandEnvelope
{
    AcousticCommandOp       Operation       = AcousticCommandOp::AssignReceiverPose;
    AcousticVoiceToken      VoiceToken      {};
    AudioBusCategory        Bus             = AudioBusCategory::Effects;
    AcousticEnvironmentPreset EnvironmentPreset = AcousticEnvironmentPreset::OpenField;
    SpatialSpeakerLayout    SpeakerLayout   = SpatialSpeakerLayout::Stereo;
    AcousticVoiceDescriptor VoiceDescriptor {};
    AcousticReceiverPose    ReceiverPose    {};
    Mathematics::Axis3D     PositionMeters  { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D     VelocityMps     { 0.0f, 0.0f, 0.0f };
    float                   ScalarA         = 1.0f;               // [-]   - Linear gain or pitch multiplier
    float                   ScalarB         = 1.0f;               // [-]   - Secondary drive intensity or pitch
    float                   ScalarC         = 0.0f;               // [-]   - Occlusion factor [0-1]
    uint32_t                RandomSeed      = 1u;                 // [-]   - Deterministic per-trigger jitter PRNG seed
    bool                    ToggleCondition = false;              // [-]   - Mute / enable toggle condition
};

struct AcousticTelemetryMetrics
{
    uint64_t MixedSampleBlocks       = 0ull; // [count] - Completed DSP mix blocks since Initialize
    uint64_t RenderedSamplePoints    = 0ull; // [count] - Total multi-channel sample points emitted
    uint32_t ActiveMixedVoices       = 0u;   // [count] - Voices actively synthesizing audio last block
    uint32_t VirtualizedVoices       = 0u;   // [count] - Audible/silent voices advancing phase without DSP cost
    uint32_t StolenVoices            = 0u;   // [count] - Cumulative lower-priority voices evicted by polyphony cap
    uint32_t DroppedCommandCount     = 0u;   // [count] - Commands rejected by full lock-free ring
    uint32_t UnderrunCount           = 0u;   // [count] - Audio endpoint underruns observed
    uint64_t AudioThreadIterations   = 0ull; // [count] - Dedicated AudioThread loop cycles
    float    PeakOutputLinear        = 0.0f; // [0-1]   - Peak instantaneous sample magnitude across channels
    float    RmsOutputLinear         = 0.0f; // [0-1]   - Root-mean-square master block loudness
    float    DominantTunnelMix       = 0.0f; // [0-1]   - Highest active voice/receiver tunnel waveguide coupling
    float    ActiveDopplerShiftRatio = 1.0f; // [-]     - Strongest active voice Doppler pitch shift ratio
    float    CpuMixMicros            = 0.0f; // [us]    - Last audio block DSP synthesis duration
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline constexpr uint32_t ResolveChannelCount(SpatialSpeakerLayout Layout) noexcept
{
    switch (Layout)
    {
        case SpatialSpeakerLayout::Mono:         return 1u;
        case SpatialSpeakerLayout::Stereo:       return 2u;
        case SpatialSpeakerLayout::BinauralHrtf: return 2u;
        case SpatialSpeakerLayout::Surround51:   return 6u;
        case SpatialSpeakerLayout::Surround71:   return 8u;
    }
    return 2u;
}

} // namespace Frontier::AudioSpace
