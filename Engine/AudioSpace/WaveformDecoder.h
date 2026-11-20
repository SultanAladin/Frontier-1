/*============================================================================================================================================
                                                             WAVEFORMDECODER.H
============================================================================================================================================*/
// 🧩 RIFF WAV / procedural PCM waveform decoding, sample-rate conversion, Catmull-Rom cubic fractional sampling, and resident repository

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/AudioSpace/AcousticTypes.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::AudioSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WaveformBuffer
{
    uint32_t           ChannelCount         = 1u;     // [count] - Interleaved audio channels [1..8]
    uint32_t           SampleRateHz         = Constants::DefaultAcousticSampleRateHz; // [Hz]
    uint32_t           SamplePointCount     = 0u;     // [count] - Total sample points per channel
    uint32_t           LoopStartSamplePoint = 0u;     // [idx]   - Inclusive loop sustain start index
    uint32_t           LoopEndSamplePoint   = 0u;     // [idx]   - Exclusive loop sustain wrap boundary
    bool               LoopEnabled          = false;  // [-]     - True when waveform carries valid loop points
    std::vector<float> Samples              {};       // [-]     - Interleaved IEEE float32 [-1..+1] samples

    [[nodiscard]] double ComputeDurationSeconds() const noexcept;
};

struct WaveformSynthesisRecipe
{
    enum class SignalShape : uint8_t
    {
        SineTone       = 0u, // [-] - Pure sinusoidal calibration wave at FundamentalFrequencyHz
        HarmonicEngine = 1u, // [-] - Fundamental + 2nd/3rd/5th harmonic rich continuous tone
        ImpulseBurst   = 2u, // [-] - Sharp exponentially damped crack + low punch for weapon triggers
        PinkAmbience   = 3u, // [-] - Seamless Voss-McCartney filtered ambient bed
    };

    SignalShape Shape            = SignalShape::SineTone;
    uint32_t    ChannelCount     = 1u;
    uint32_t    SampleRateHz     = Constants::DefaultAcousticSampleRateHz;
    float       DurationSeconds  = 1.0f;
    float       FundamentalFrequencyHz  = 440.0f;
    float       Amplitude        = 0.8f;
    uint32_t    RandomSeed       = 1337u;
    bool        SeamlessLoop     = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class WaveformDecoder
{
public:
    [[nodiscard]] static bool DecodeWavFromBytes(
        std::span<const std::byte> RawWavBytes,
        WaveformBuffer&            OutWaveform,
        std::string&               OutDiagnostic);

    [[nodiscard]] static bool LoadWavFromFile(
        const std::filesystem::path& FilePath,
        WaveformBuffer&              OutWaveform,
        std::string&                 OutDiagnostic);

    [[nodiscard]] static bool EncodeWavToBytes(
        const WaveformBuffer& Waveform,
        std::vector<std::byte>& OutWavBytes,
        std::string&          OutDiagnostic);

    [[nodiscard]] static bool WriteWavToFile(
        const std::filesystem::path& FilePath,
        const WaveformBuffer&        Waveform,
        std::string&                 OutDiagnostic);

    [[nodiscard]] static WaveformBuffer ResampleToRate(
        const WaveformBuffer& SourceWaveform,
        uint32_t              TargetSampleRateHz);

    [[nodiscard]] static WaveformBuffer SynthesizeProceduralWaveform(
        const WaveformSynthesisRecipe& Recipe);

    [[nodiscard]] static float SampleChannelCubic(
        const WaveformBuffer& Waveform,
        uint32_t              ChannelIndex,
        double                SamplePosition,
        bool                  WrapLoopBounds) noexcept;
};

class WaveformRepository
{
public:
    WaveformRepository() noexcept  = default;
    ~WaveformRepository() noexcept = default;

    WaveformRepository(const WaveformRepository&)            = delete;
    WaveformRepository& operator=(const WaveformRepository&) = delete;

    void Reset() noexcept;

    [[nodiscard]] WaveformToken RegisterWaveform(WaveformBuffer Waveform, uint32_t TargetSampleRateHz = 0u) noexcept;
    bool                        ReclaimWaveform(WaveformToken Token) noexcept;
    [[nodiscard]] bool          AuthenticateToken(WaveformToken Token) const noexcept;

    [[nodiscard]] const WaveformBuffer* ResolveResidentWaveform(WaveformToken Token) const noexcept;
    [[nodiscard]] uint32_t              QueryResidentCount() const noexcept;

private:
    struct WaveformSlot
    {
        std::atomic<uint32_t> Generation     { 1u };
        std::atomic<bool>     Occupied       { false };
        std::atomic<bool>     ReadyForAudio  { false };
        WaveformBuffer        ResidentBuffer {};
    };

    std::array<WaveformSlot, Constants::MaxAcousticWaveforms> Slots {};
    std::atomic<uint32_t>                                     ResidentCounter { 0u };
};

} // namespace Frontier::AudioSpace
