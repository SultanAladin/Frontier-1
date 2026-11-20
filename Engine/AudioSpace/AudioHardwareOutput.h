/*============================================================================================================================================
                                                           AUDIOHARDWAREOUTPUT.H
============================================================================================================================================*/
// 🧩 Cross-platform hardware audio sink (WASAPI / runtime dlopen Pulse/ALSA / AAudio) with deterministic HeadlessBuffer fallback (BKL-009)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/AudioSpace/AcousticTypes.h"
#include "Engine/AudioSpace/WaveformDecoder.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Frontier::AudioSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AudioEndpointConfiguration
{
    AudioOutputDriverCategory RequestedDriver         = AudioOutputDriverCategory::AutoSelect;
    uint32_t                   SampleRateHz             = Constants::DefaultAcousticSampleRateHz; // [Hz]
    uint32_t                   ChannelCount             = 2u;                                     // [count]
    uint32_t                   BlockSamplePoints        = Constants::DefaultAcousticBlockSamplePoints; // [count]
    uint32_t                   HardwareBufferLatencyMs  = 20u;                                    // [ms]
    bool                       RealTimePacingInHeadless = false;                                  // [-] - Sleep block duration when headless
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class AudioHardwareOutput
{
public:
    AudioHardwareOutput() noexcept;
    ~AudioHardwareOutput() noexcept;

    AudioHardwareOutput(const AudioHardwareOutput&)            = delete;
    AudioHardwareOutput& operator=(const AudioHardwareOutput&) = delete;

    [[nodiscard]] bool OpenEndpoint(const AudioEndpointConfiguration& Configuration) noexcept;
    void               CloseEndpoint() noexcept;

    [[nodiscard]] bool SubmitInterleavedBlock(
        const float* InterleavedSamples,
        uint32_t     SamplePointCount) noexcept;

    [[nodiscard]] bool                       QueryEndpointOpen() const noexcept;
    [[nodiscard]] AudioOutputDriverCategory QueryActiveDriver() const noexcept;
    [[nodiscard]] uint32_t                   QueryActiveSampleRateHz() const noexcept;
    [[nodiscard]] uint32_t                   QueryActiveChannelCount() const noexcept;
    [[nodiscard]] uint64_t                   QuerySubmittedSamplePoints() const noexcept;
    [[nodiscard]] uint32_t                   QueryUnderrunCount() const noexcept;

    void                         ConfigureCaptureBuffer(bool CaptureEnabled, uint32_t MaxCapturedSamplePoints = 480000u);
    void                         ResetCaptureBuffer() noexcept;
    [[nodiscard]] WaveformBuffer ExtractCapturedWaveform() const;
    [[nodiscard]] bool           WriteCapturedWavFile(
        const std::filesystem::path& FilePath,
        std::string&                 OutDiagnostic) const;

private:
    struct PlatformEndpoint;
    std::unique_ptr<PlatformEndpoint> Platform {};
};

} // namespace Frontier::AudioSpace
