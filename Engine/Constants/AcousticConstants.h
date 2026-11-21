/*============================================================================================================================================
                                                             ACOUSTICCONSTANTS.H
============================================================================================================================================*/
// 🧩 Shared compile-time acoustic physics constants, voice capacities, DSP buffer bounds, and octave center frequencies

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frontier::Constants
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t    DefaultAcousticSampleRateHz       = 48000u;     // [Hz]     - Native engine acoustic mixing sample rate
inline constexpr uint32_t    DefaultAcousticBlockSamplePoints  = 256u;       // [count]  - Multi-channel sample points per real-time pull block
inline constexpr uint32_t    MaxAcousticOutputChannels         = 8u;         // [count]  - Maximum spatial channel layout (7.1 surround)
inline constexpr uint32_t    MaxAudioBusCount                  = 7u;         // [count]  - Master + Ambience + Effects + Weapons + Vehicles + Dialogue + Interface

inline constexpr float       DryAirSoundSpeedMps               = 343.2f;     // [m/s]    - Speed of sound in dry ISA air at 20 °C
inline constexpr float       WaterSoundSpeedMps                = 1482.0f;    // [m/s]    - Speed of sound in fresh water at 20 °C
inline constexpr float       ReferenceSoundPressurePa          = 20.0e-6f;   // [Pa]     - ISO acoustic threshold of hearing reference
inline constexpr float       HumanHeadRadiusMeters             = 0.0875f;    // [m]      - Spherical Woodworth head radius for binaural ITD/ILD
inline constexpr float       AcousticSpeedClampFloorRatio      = 0.15f;      // [0-1]    - Subsonic denominator floor preventing Doppler singularity
inline constexpr float       MinimumDopplerPitchRatio          = 0.25f;      // [-]      - Lower clamp on Doppler frequency scaling
inline constexpr float       MaximumDopplerPitchRatio          = 4.0f;       // [-]      - Upper clamp on Doppler frequency scaling
inline constexpr float       DopplerSlewTimeSeconds            = 0.015f;     // [s]      - One-pole pitch slew constant eliminating zipper clicks

inline constexpr uint32_t    MaxAcousticVoices                 = 128u;       // [count]  - Preallocated polyphonic voice slots (zero RT alloc)
inline constexpr uint32_t    MaxActiveMixedVoices              = 64u;        // [count]  - Simultaneous DSP voice ceiling before virtualization
inline constexpr uint32_t    MaxVoiceVariationSlots            = 4u;         // [count]  - Maximum round-robin asset variations per cue
inline constexpr uint32_t    MaxAcousticWaveforms              = 256u;       // [count]  - Preallocated resident waveform buffer slots
inline constexpr uint32_t    MaxAcousticZones                  = 64u;        // [count]  - Preallocated spatial tunnel / environment volumes

inline constexpr std::size_t AudioCommandQueueCapacity         = 1024u;      // [count]  - Bounded lock-free MPMC command ring capacity
inline constexpr std::size_t StreamRingSampleCapacity          = 65536u;     // [count]  - Per-stream lock-free SPSC ring sample capacity
inline constexpr uint32_t    MaxBinauralDelaySamplePoints      = 128u;       // [count]  - Maximum per-voice ITD fractional delay line length
inline constexpr uint32_t    TunnelCombTapCount                = 4u;         // [count]  - Parallel waveguide comb modes in TunnelAcousticFilter
inline constexpr uint32_t    ReverbFdnLineCount                = 4u;         // [count]  - Hadamard 4x4 orthogonal feedback delay network lines
inline constexpr float       MasterLimiterCeilingLinear        = 0.98f;      // [0-1]    - Soft-knee master bus safety ceiling

inline constexpr std::array<float, 10u> OctaveBandCenterHz = {{
    31.5f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
}};

} // namespace Frontier::Constants
