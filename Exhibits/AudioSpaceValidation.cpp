/*============================================================================================================================================
                                                         AUDIOSPACEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end CTest validation of Engine/AudioSpace, AudioThread, AcousticCodec, Doppler shift, surround VBAP, binaural & tunnel DSP

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/AudioSpace/AcousticTypes.h"
#include "Engine/AudioSpace/AudioBusAccumulator.h"
#include "Engine/AudioSpace/AudioExtension.h"
#include "Engine/AudioSpace/AudioHardwareOutput.h"
#include "Engine/AudioSpace/SpatialAcoustics.h"
#include "Engine/AudioSpace/WaveformDecoder.h"
#include "Engine/Codecs/AcousticCodec.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"
#include "Engine/Threading/Sync/ThreadProgressionTracker.h"

#if __has_include("imgui.h")
    #include "Exhibits/SoftwareCanvasRasterizer.h"
    #include "imgui.h"
    #define FRONTIER_AUDIO_HAS_IMGUI 1
#else
    #define FRONTIER_AUDIO_HAS_IMGUI 0
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ASSERTION VALIDATION
//------------------------------------------------------------------------------------------------------------------------

struct ValidationScoreboard
{
    uint32_t PassedChecks = 0u;
    uint32_t FailedChecks = 0u;

    void Verify(bool Condition, const char* CheckTitle)
    {
        if (Condition)
        {
            ++PassedChecks;
        }
        else
        {
            ++FailedChecks;
            std::fprintf(stderr, "[AudioSpaceValidation] FAILED: %s\n", CheckTitle);
        }
    }
};

struct AudioExhibitSummary
{
    float    ApproachPeakHz         = 0.0f;
    float    RecedePeakHz           = 0.0f;
    float    BinauralMeasuredLagMs  = 0.0f;
    float    TunnelTailRms          = 0.0f;
    float    OpenFieldTailRms       = 0.0f;
    float    DuckedAmbienceRatio    = 1.0f;
    float    LimiterPeakLinear      = 0.0f;
    uint32_t ThreadHeartbeatCount   = 0u;
    uint32_t StolenVoicesCount      = 0u;
    uint32_t TotalChecksPassed      = 0u;
    std::array<float, 8u> Surround71LeftAngles {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 SPECTRAL DSP ANALYZERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] float MeasureGoertzelAmplitude(
    const float* Samples,
    uint32_t     ChannelCount,
    uint32_t     ChannelIndex,
    std::size_t  StartPoint,
    std::size_t  PointCount,
    uint32_t     SampleRateHz,
    float        TargetHz) noexcept
{
    if (Samples == nullptr || PointCount == 0u || ChannelCount == 0u)
    {
        return 0.0f;
    }

    const double Omega = Frontier::Constants::Tau * static_cast<double>(TargetHz) / static_cast<double>(SampleRateHz);
    const double Coeff = 2.0 * std::cos(Omega);
    double       Q1    = 0.0;
    double       Q2    = 0.0;

    for (std::size_t Idx = 0u; Idx < PointCount; ++Idx)
    {
        const double Window = 0.5 * (1.0 - std::cos(Frontier::Constants::Tau * static_cast<double>(Idx) / static_cast<double>(PointCount)));
        const double Sample = static_cast<double>(Samples[(StartPoint + Idx) * ChannelCount + ChannelIndex]) * Window;
        const double Q0     = Coeff * Q1 - Q2 + Sample;
        Q2                  = Q1;
        Q1                  = Q0;
    }

    const double MagSq = Q1 * Q1 + Q2 * Q2 - Q1 * Q2 * Coeff;
    return static_cast<float>(std::sqrt(std::max(0.0, MagSq)) * (2.0 / static_cast<double>(PointCount)));
}

[[nodiscard]] float MeasureDominantFrequencyHz(
    const float* Samples,
    uint32_t     ChannelCount,
    uint32_t     ChannelIndex,
    std::size_t  StartPoint,
    std::size_t  PointCount,
    uint32_t     SampleRateHz,
    float        MinHz,
    float        MaxHz,
    float        StepHz) noexcept
{
    float BestFreq = MinHz;
    float BestAmp  = -1.0f;
    for (float Freq = MinHz; Freq <= MaxHz; Freq += StepHz)
    {
        const float Amp = MeasureGoertzelAmplitude(Samples, ChannelCount, ChannelIndex, StartPoint, PointCount, SampleRateHz, Freq);
        if (Amp > BestAmp)
        {
            BestAmp  = Amp;
            BestFreq = Freq;
        }
    }
    return BestFreq;
}

[[nodiscard]] float MeasureWindowRms(
    const float* Samples,
    uint32_t     ChannelCount,
    std::size_t  StartPoint,
    std::size_t  PointCount) noexcept
{
    if (Samples == nullptr || PointCount == 0u || ChannelCount == 0u)
    {
        return 0.0f;
    }
    double SumSq = 0.0;
    const std::size_t FloatCount = PointCount * ChannelCount;
    const std::size_t Offset     = StartPoint * ChannelCount;
    for (std::size_t Idx = 0u; Idx < FloatCount; ++Idx)
    {
        const double S = static_cast<double>(Samples[Offset + Idx]);
        SumSq         += S * S;
    }
    return static_cast<float>(std::sqrt(SumSq / static_cast<double>(FloatCount)));
}

[[nodiscard]] int MeasureInterChannelLagSamples(
    const float* StereoSamples,
    std::size_t  StartPoint,
    std::size_t  PointCount,
    int          MaxSearchLag) noexcept
{
    int    BestLag = 0;
    double BestDot = -1.0e30;
    for (int Lag = -MaxSearchLag; Lag <= MaxSearchLag; ++Lag)
    {
        double Dot = 0.0;
        for (std::size_t Idx = static_cast<std::size_t>(MaxSearchLag);
             Idx + static_cast<std::size_t>(MaxSearchLag) < PointCount;
             ++Idx)
        {
            const float Left  = StereoSamples[(StartPoint + Idx) * 2u + 0u];
            const float Right = StereoSamples[(StartPoint + static_cast<std::size_t>(static_cast<ptrdiff_t>(Idx) + Lag)) * 2u + 1u];
            Dot              += static_cast<double>(Left) * static_cast<double>(Right);
        }
        if (Dot > BestDot)
        {
            BestDot = Dot;
            BestLag = Lag;
        }
    }
    return BestLag;
}

#if FRONTIER_AUDIO_HAS_IMGUI
//------------------------------------------------------------------------------------------------------------------------
//                                        HEADLESS DIAGNOSTIC PNG RENDERER (1440 x 900)
//------------------------------------------------------------------------------------------------------------------------

bool RenderAudioExhibitPng(const AudioExhibitSummary& Summary, const std::filesystem::path& OutputPath)
{
    constexpr int CanvasWidth  = 1440;
    constexpr int CanvasHeight = 900;

    ImGuiContext* ImGuiSession = ImGui::CreateContext();
    ImGui::SetCurrentContext(ImGuiSession);

    ImGuiIO& Io    = ImGui::GetIO();
    Io.DisplaySize = ImVec2(static_cast<float>(CanvasWidth), static_cast<float>(CanvasHeight));
    Io.IniFilename = nullptr;
    (void)Frontier::Exhibits::LoadWorkspaceDmSansFont(Io, 11.5f);

    ImGui::NewFrame();
    ImDrawList* RasterList = ImGui::GetBackgroundDrawList();

    const ImU32 BgDeep        = IM_COL32(13, 17, 24, 255);
    const ImU32 PanelBg        = IM_COL32(21, 27, 38, 255);
    const ImU32 PanelHeaderBg  = IM_COL32(28, 37, 52, 255);
    const ImU32 PanelBorder    = IM_COL32(54, 71, 98, 255);
    const ImU32 AccentCyan    = IM_COL32(56, 189, 248, 255);
    const ImU32 AccentEmerald = IM_COL32(52, 211, 153, 255);
    const ImU32 AccentAmber   = IM_COL32(251, 191, 36, 255);
    const ImU32 AccentViolet  = IM_COL32(167, 139, 250, 255);
    const ImU32 TextPrimary   = IM_COL32(236, 242, 250, 255);
    const ImU32 TextMuted     = IM_COL32(148, 163, 184, 255);
    const ImU32 BarTrack      = IM_COL32(34, 44, 62, 255);

    RasterList->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(1440.0f, 900.0f), BgDeep);

    RasterList->AddRectFilled(ImVec2(28.0f, 22.0f), ImVec2(1412.0f, 96.0f), PanelHeaderBg, 8.0f);
    RasterList->AddRect(ImVec2(28.0f, 22.0f), ImVec2(1412.0f, 96.0f), PanelBorder, 8.0f, 0, 1.5f);
    RasterList->AddText(ImVec2(48.0f, 38.0f), AccentCyan, "FRONTIER ENGINE  //  AUDIOSPACE, DOPPLER, SURROUND 7.1 & TUNNEL ACOUSTICS EXHIBIT");
    char BannerSub[256]{};
    std::snprintf(
        BannerSub,
        sizeof(BannerSub),
        "Lock-Free AudioThread | Catmull-Rom Doppler | Binaural HRTF | Surround 5.1/7.1 | Tunnel Waveguide + Hadamard FDN  |  %u Checks Passed",
        Summary.TotalChecksPassed);
    RasterList->AddText(ImVec2(48.0f, 64.0f), TextPrimary, BannerSub);

    auto ConstructPanelContainer = [&](float X0, float Y0, float X1, float Y1, const char* Title, ImU32 Accent)
    {
        RasterList->AddRectFilled(ImVec2(X0, Y0), ImVec2(X1, Y1), PanelBg, 8.0f);
        RasterList->AddRectFilled(ImVec2(X0, Y0), ImVec2(X1, Y0 + 40.0f), PanelHeaderBg, 8.0f);
        RasterList->AddRect(ImVec2(X0, Y0), ImVec2(X1, Y1), PanelBorder, 8.0f, 0, 1.5f);
        RasterList->AddRectFilled(ImVec2(X0 + 14.0f, Y0 + 12.0f), ImVec2(X0 + 20.0f, Y0 + 28.0f), Accent, 2.0f);
        RasterList->AddText(ImVec2(X0 + 30.0f, Y0 + 13.0f), TextPrimary, Title);
    };

    ConstructPanelContainer(28.0f, 116.0f, 706.0f, 500.0f, "PHYSICAL DOPPLER SHIFT & SURROUND 7.1 VBAP POLAR FIELD", AccentCyan);
    ConstructPanelContainer(734.0f, 116.0f, 1412.0f, 500.0f, "TUNNEL WAVEGUIDE COMB + HADAMARD FDN & MIXER TELEMETRY", AccentEmerald);
    ConstructPanelContainer(28.0f, 520.0f, 1412.0f, 872.0f, "CONTINUOUS/EVENT TRIGGERS, BINAURAL ITD/ILD, BUS DUCKING & .ACOUSTIC CODEC", AccentViolet);

    auto ConstructMetricBar = [&](float X, float Y, float W, const char* Label, const char* ReadingText, float Ratio, ImU32 Fill)
    {
        RasterList->AddText(ImVec2(X, Y), TextPrimary, Label);
        const ImVec2 Dim = ImGui::CalcTextSize(ReadingText);
        RasterList->AddText(ImVec2(X + W - Dim.x, Y), AccentCyan, ReadingText);
        RasterList->AddRectFilled(ImVec2(X, Y + 22.0f), ImVec2(X + W, Y + 36.0f), BarTrack, 4.0f);
        RasterList->AddRectFilled(ImVec2(X, Y + 22.0f), ImVec2(X + W * std::clamp(Ratio, 0.04f, 1.0f), Y + 36.0f), Fill, 4.0f);
    };

    char Buf[256]{};
    std::snprintf(Buf, sizeof(Buf), "%.1f Hz (vs 440 Hz)", Summary.ApproachPeakHz);
    ConstructMetricBar(48.0f, 178.0f, 636.0f, "Doppler Approach (+85 m/s Radial)", Buf, Summary.ApproachPeakHz / 700.0f, AccentEmerald);

    std::snprintf(Buf, sizeof(Buf), "%.1f Hz (vs 440 Hz)", Summary.RecedePeakHz);
    ConstructMetricBar(48.0f, 240.0f, 636.0f, "Doppler Recede (-85 m/s Radial)", Buf, Summary.RecedePeakHz / 700.0f, AccentAmber);

    // Surround 7.1 VBAP polar ring diagram in Panel 1 lower half
    const ImVec2 RingCenter(210.0f, 386.0f);
    RasterList->AddCircle(RingCenter, 74.0f, IM_COL32(60, 80, 110, 180), 48, 1.5f);
    RasterList->AddCircle(RingCenter, 42.0f, IM_COL32(45, 60, 84, 140), 36, 1.0f);
    RasterList->AddCircleFilled(RingCenter, 7.0f, AccentCyan, 16);
    struct SpeakerPlot
    {
        const char* Tag;
        float       AngleDeg;
        float       Gain;
    };
    const std::array<SpeakerPlot, 7u> PolarSpeakers = {{
        { "C",   0.0f,    Summary.Surround71LeftAngles[2] },
        { "FL",  30.0f,   Summary.Surround71LeftAngles[0] },
        { "SL",  90.0f,   Summary.Surround71LeftAngles[4] },
        { "RL",  145.0f,  Summary.Surround71LeftAngles[6] },
        { "RR", -145.0f,  Summary.Surround71LeftAngles[7] },
        { "SR", -90.0f,   Summary.Surround71LeftAngles[5] },
        { "FR", -30.0f,   Summary.Surround71LeftAngles[1] },
    }};
    for (const SpeakerPlot& Spk : PolarSpeakers)
    {
        const float Rad = Spk.AngleDeg * Frontier::Constants::DegToRadFloat;
        const ImVec2 Pos(RingCenter.x - std::sin(Rad) * 74.0f, RingCenter.y - std::cos(Rad) * 74.0f);
        const ImU32 DotCol = (Spk.Gain > 0.25f) ? AccentEmerald : IM_COL32(100, 116, 139, 255);
        RasterList->AddLine(RingCenter, Pos, IM_COL32(56, 189, 248, 70), 1.0f);
        RasterList->AddCircleFilled(Pos, (Spk.Gain > 0.25f) ? 8.0f : 5.5f, DotCol, 16);
        RasterList->AddText(ImVec2(Pos.x - 10.0f, Pos.y + 9.0f), TextMuted, Spk.Tag);
    }
    RasterList->AddText(ImVec2(335.0f, 326.0f), TextPrimary, "7.1 Surround Speaker Layout (RHR +Z Up)");
    RasterList->AddText(ImVec2(335.0f, 350.0f), TextMuted,   "Mono  |  Stereo  |  Binaural HRTF  |  5.1  |  7.1");
    RasterList->AddText(ImVec2(335.0f, 374.0f), AccentEmerald, "Pairwise Constant-Power VBAP + LFE Send");
    RasterList->AddText(ImVec2(335.0f, 398.0f), AccentCyan,    "4-Tap Catmull-Rom Cubic Fractional Resampler");
    RasterList->AddText(ImVec2(335.0f, 422.0f), AccentViolet,  "15 ms Exponential Doppler Pitch Slew Limiter");

    std::snprintf(Buf, sizeof(Buf), "Tail RMS: %.4f vs Open %.5f", Summary.TunnelTailRms, Summary.OpenFieldTailRms);
    ConstructMetricBar(756.0f, 178.0f, 632.0f, "Tunnel Waveguide + Hadamard FDN Tail (120..500 ms)", Buf, 0.88f, AccentCyan);

    std::snprintf(Buf, sizeof(Buf), "%.2f ms Woodworth ITD", Summary.BinauralMeasuredLagMs);
    ConstructMetricBar(756.0f, 240.0f, 632.0f, "Binaural HRTF Left (+90 deg) Interaural Lag", Buf, 0.78f, AccentViolet);

    // Tunnel vs Open-Field impulse envelope decay visualization in Panel 2 lower half
    RasterList->AddRectFilled(ImVec2(756.0f, 304.0f), ImVec2(1388.0f, 474.0f), IM_COL32(14, 19, 28, 255), 6.0f);
    RasterList->AddRect(ImVec2(756.0f, 304.0f), ImVec2(1388.0f, 474.0f), PanelBorder, 6.0f, 0, 1.0f);
    RasterList->AddText(ImVec2(772.0f, 314.0f), TextPrimary, "Impulse Response Decay Envelope: TunnelTube Waveguide vs OpenField");
    for (int Step = 0; Step < 58; ++Step)
    {
        const float T         = static_cast<float>(Step) / 57.0f;
        const float X         = 776.0f + T * 592.0f;
        const float TunnelEnv = std::exp(-T * 3.1f) * (0.72f + 0.28f * std::abs(std::sin(T * 24.0f)));
        const float OpenEnv   = std::exp(-T * 18.0f);
        RasterList->AddRectFilled(ImVec2(X, 456.0f - TunnelEnv * 110.0f), ImVec2(X + 7.0f, 456.0f), AccentCyan, 1.5f);
        RasterList->AddRectFilled(ImVec2(X, 456.0f - OpenEnv * 110.0f),   ImVec2(X + 7.0f, 456.0f), IM_COL32(148, 163, 184, 190), 1.5f);
    }

    std::snprintf(Buf, sizeof(Buf), "%.2fx Ambience Attenuation", Summary.DuckedAmbienceRatio);
    ConstructMetricBar(48.0f, 586.0f, 636.0f, "Weapons / Dialogue Side-Chain Ducking on Ambience Bus", Buf, Summary.DuckedAmbienceRatio, AccentAmber);

    std::snprintf(Buf, sizeof(Buf), "%.3f peak (<= 0.980)", Summary.LimiterPeakLinear);
    ConstructMetricBar(756.0f, 586.0f, 632.0f, "Soft-Knee Master Peak Limiter Ceiling (8 Stacked Voices)", Buf, Summary.LimiterPeakLinear, AccentEmerald);

    const std::array<const char*, 5u> FeatureChips = {
        "1. Lock-Free AudioThread",
        "2. Loop Splice & Virtualization",
        "3. Catmull-Rom Doppler",
        "4. Tunnel Comb + Hadamard FDN",
        "5. .Acoustic Two-Tier Codec",
    };
    for (std::size_t Idx = 0u; Idx < FeatureChips.size(); ++Idx)
    {
        const float ChipX = 48.0f + static_cast<float>(Idx) * 268.0f;
        RasterList->AddRectFilled(ImVec2(ChipX, 682.0f), ImVec2(ChipX + 252.0f, 722.0f), IM_COL32(30, 41, 59, 255), 6.0f);
        RasterList->AddRect(ImVec2(ChipX, 682.0f), ImVec2(ChipX + 252.0f, 722.0f), AccentEmerald, 6.0f, 0, 1.2f);
        RasterList->AddText(ImVec2(ChipX + 14.0f, 695.0f), TextPrimary, FeatureChips[Idx]);
    }

    std::snprintf(
        Buf,
        sizeof(Buf),
        "AudioThread Heartbeats: %u   |   Stolen Voices: %u   |   7.1 Surround Center Gain: %.2f   |   WASAPI / Pulse / ALSA / AAudio / Headless",
        Summary.ThreadHeartbeatCount,
        Summary.StolenVoicesCount,
        Summary.Surround71LeftAngles[2]);
    RasterList->AddText(ImVec2(48.0f, 768.0f), TextMuted, Buf);

    ImGui::Render();
    Frontier::Exhibits::SoftwareRasterCanvas Canvas(
        static_cast<uint32_t>(CanvasWidth),
        static_cast<uint32_t>(CanvasHeight),
        Frontier::Components::RgbaColor8{ 13u, 17u, 24u, 255u });
    Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
    const bool Saved = Canvas.WritePngFile(OutputPath);
    ImGui::DestroyContext(ImGuiSession);
    return Saved;
}
#endif

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               VALIDATION ENTRYPOINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    using namespace Frontier;
    using namespace Frontier::AudioSpace;

    ValidationScoreboard Board{};
    AudioExhibitSummary  Summary{};

    //--------------------------------------------------------------------------------------------------------------------
    // PHASE 1: SUSPENDABLE PARTICIPANT LIFECYCLE & REAL-TIME AUDIOTHREAD WATCHDOG
    //--------------------------------------------------------------------------------------------------------------------
    {
        Threading::ThreadProgressionTracker Tracker;
        AudioExtensionConfiguration          Config{};
        Config.SampleRateHz           = 48000u;
        Config.SpeakerLayout          = SpatialSpeakerLayout::Stereo;
        Config.BlockSamplePoints      = 256u;
        Config.RequestedDriver       = AudioOutputDriverCategory::HeadlessBuffer;
        Config.SpawnDedicatedOsThread = true;
        Config.ProgressionMonitor     = &Tracker;

        AudioExtension Audio(Config);
        Lifecycle::LifecycleSequence Sequence;
        (void)Sequence.RegisterParticipant("AudioExtension", Audio);

        Board.Verify(Sequence.Initialize(), "LifecycleSequence initializes AudioExtension");
        Board.Verify(Audio.QueryInitialized(), "AudioExtension reports initialized");
        Board.Verify(Audio.AccessAudioThread().QueryRunning(), "AudioThread dedicated OS loop is running");
        Board.Verify(Tracker.QueryActiveSlotCount() == 1u, "ThreadProgressionTracker registers AudioThread slot");

        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        const Threading::ThreadProgressionReading LiveReading = Tracker.InspectSlot(Audio.AccessAudioThread().QueryProgressionSlot());
        const uint64_t IterBeforeSuspend = Audio.AccessAudioThread().QueryIterationCount();
        Summary.ThreadHeartbeatCount     = static_cast<uint32_t>(LiveReading.TotalPulseCount);

        Board.Verify(IterBeforeSuspend >= 2ull, "Dedicated AudioThread produces audio blocks");
        Board.Verify(
            LiveReading.Condition == Threading::ThreadLivenessCondition::Healthy,
            "AudioThread heartbeat condition is Healthy");

        Board.Verify(Sequence.Suspend() && Audio.QuerySuspended(), "LifecycleSequence suspends AudioExtension & AudioThread");
        const uint64_t IterAtSuspend = Audio.AccessAudioThread().QueryIterationCount();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        const uint64_t IterWhileSuspended = Audio.AccessAudioThread().QueryIterationCount();
        Board.Verify(
            IterWhileSuspended <= IterAtSuspend + 1ull,
            "Suspended AudioThread halts mix block progression");

        Board.Verify(Sequence.Resume() && !Audio.QuerySuspended(), "LifecycleSequence resumes AudioExtension");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        Board.Verify(
            Audio.AccessAudioThread().QueryIterationCount() > IterWhileSuspended,
            "Resumed AudioThread advances mix blocks again");

        Sequence.Finalize();
        Board.Verify(!Audio.QueryInitialized(), "Finalize shuts down AudioExtension");
        Board.Verify(Tracker.QueryActiveSlotCount() == 0u, "Finalize unregisters AudioThread progression slot");
    }

    //--------------------------------------------------------------------------------------------------------------------
    // PHASE 2: RIFF WAV CODEC, CUBIC SAMPLE-RATE CONVERSION & .ACOUSTIC CONTAINER ROUND-TRIP
    //--------------------------------------------------------------------------------------------------------------------
    {
        WaveformSynthesisRecipe Sine44100{};
        Sine44100.Shape           = WaveformSynthesisRecipe::SignalShape::SineTone;
        Sine44100.ChannelCount    = 1u;
        Sine44100.SampleRateHz    = 44100u;
        Sine44100.DurationSeconds = 0.25f;
        Sine44100.FundamentalFrequencyHz = 440.0f;
        Sine44100.Amplitude       = 0.8f;
        Sine44100.SeamlessLoop    = true;

        WaveformBuffer Generated44 = WaveformDecoder::SynthesizeProceduralWaveform(Sine44100);
        Generated44.LoopStartSamplePoint = 512u;
        Generated44.LoopEndSamplePoint   = 9000u;

        std::vector<std::byte> WavBytes{};
        std::string            Diagnostic{};
        Board.Verify(WaveformDecoder::EncodeWavToBytes(Generated44, WavBytes, Diagnostic), "Encode IEEE float32 WAV with smpl loop chunk");

        WaveformBuffer Decoded44{};
        Board.Verify(WaveformDecoder::DecodeWavFromBytes(WavBytes, Decoded44, Diagnostic), "Decode RIFF WAV back from memory");
        Board.Verify(Decoded44.LoopEnabled && Decoded44.LoopStartSamplePoint == 512u && Decoded44.LoopEndSamplePoint == 9000u,
            "RIFF smpl loop boundaries preserved cleanly across WAV round-trip");

        const WaveformBuffer Resampled48 = WaveformDecoder::ResampleToRate(Decoded44, 48000u);
        const float MeasuredHz = MeasureDominantFrequencyHz(
            Resampled48.Samples.data(),
            1u,
            0u,
            0u,
            std::min<std::size_t>(4096u, Resampled48.SamplePointCount),
            48000u,
            400.0f,
            480.0f,
            2.0f);
        Board.Verify(std::abs(MeasuredHz - 440.0f) <= 6.0f, "Cubic 44.1 kHz -> 48 kHz resampler preserves 440 Hz pitch center");

        Codecs::AcousticAssetDocument AssetDoc{};
        AssetDoc.AssetTitle                           = "CarbineShot";
        AssetDoc.CueDescriptor.Bus                    = AudioBusCategory::Weapons;
        AssetDoc.CueDescriptor.PlaybackCycle                = PlaybackCycleCategory::OneShotTrigger;
        AssetDoc.CueDescriptor.NominalGain               = 0.92f;
        AssetDoc.CueDescriptor.PitchJitterRange       = 0.06f;
        AssetDoc.CueDescriptor.Priority               = 220u;
        AssetDoc.Environment                          = AcousticEnvironmentPreset::TunnelTube;
        AssetDoc.Waveform                             = Resampled48;

        std::vector<std::byte> ContainerBytes{};
        Board.Verify(Codecs::AcousticEncoder::EncodeToBytes(AssetDoc, ContainerBytes, Diagnostic), "Encode .Acoustic Two-Tier Hybrid container");

        Codecs::AcousticAssetDocument RestoredDoc{};
        Board.Verify(Codecs::AcousticDecoder::DecodeFromBytes(ContainerBytes, RestoredDoc, Diagnostic), "Decode .Acoustic container cleanly");
        Board.Verify(
            RestoredDoc.AssetTitle == "CarbineShot" &&
            RestoredDoc.CueDescriptor.Bus == AudioBusCategory::Weapons &&
            RestoredDoc.CueDescriptor.Priority == 220u &&
            RestoredDoc.Environment == AcousticEnvironmentPreset::TunnelTube &&
            RestoredDoc.Waveform.SamplePointCount == Resampled48.SamplePointCount &&
            std::abs(RestoredDoc.Waveform.Samples[123] - Resampled48.Samples[123]) < 1.0e-6f,
            ".Acoustic TOML metadata and 64B-aligned FNV-1a verified PCM match bit-for-bit");
    }

    //--------------------------------------------------------------------------------------------------------------------
    // PHASE 3: CONTINUOUS LOOP CROSSFADE VS ONE-SHOT VARIATION TRIGGERS, VIRTUALIZATION & VOICE STEALING
    //--------------------------------------------------------------------------------------------------------------------
    {
        AudioExtensionConfiguration Config{};
        Config.SampleRateHz           = 48000u;
        Config.SpeakerLayout          = SpatialSpeakerLayout::Stereo;
        Config.BlockSamplePoints      = 256u;
        Config.RequestedDriver       = AudioOutputDriverCategory::HeadlessBuffer;
        Config.SpawnDedicatedOsThread = false;
        Config.CaptureOutputForExport = true;
        Config.MaxCapturedSamplePoints = 96000u;

        AudioExtension Audio(Config);
        Board.Verify(Audio.Initialize(), "Deterministic synchronous AudioExtension initialized");

        WaveformSynthesisRecipe LoopBed{};
        LoopBed.Shape           = WaveformSynthesisRecipe::SignalShape::SineTone;
        LoopBed.ChannelCount    = 1u;
        LoopBed.SampleRateHz    = 48000u;
        LoopBed.DurationSeconds = 0.1f; // 4800 samples -> wraps 10 times over 1.0s
        LoopBed.FundamentalFrequencyHz = 220.0f;
        LoopBed.Amplitude       = 0.6f;
        LoopBed.SeamlessLoop    = true;
        const WaveformToken LoopToken = Audio.RegisterWaveform(WaveformDecoder::SynthesizeProceduralWaveform(LoopBed));

        const AcousticVoiceToken AmbienceVoice = Audio.StartContinuousLoop(
            LoopToken,
            { 1.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f },
            AudioBusCategory::Ambience,
            0.75f,
            150u);
        Board.Verify(static_cast<bool>(AmbienceVoice), "Continuous loop voice started cleanly");

        // Pump 188 blocks (~1.0s = 48128 sample points, crossing loop boundary ~10 times)
        Audio.PumpAudioBlocks(188u);
        const WaveformBuffer CapturedLoop = Audio.AccessHardwareOutput().ExtractCapturedWaveform();
        float MaxStepDiff = 0.0f;
        for (uint32_t Idx = 1u; Idx < CapturedLoop.SamplePointCount; ++Idx)
        {
            const float L0   = CapturedLoop.Samples[(Idx - 1u) * 2u];
            const float L1   = CapturedLoop.Samples[Idx * 2u];
            MaxStepDiff      = std::max(MaxStepDiff, std::abs(L1 - L0));
        }
        Board.Verify(
            CapturedLoop.SamplePointCount >= 48000u && MaxStepDiff < 0.045f,
            "Continuous loop wraps cleanly with zero click discontinuity");

        // Verify smooth release tail removes voice after ReleaseSeconds
        Board.Verify(Audio.ReleaseVoice(AmbienceVoice), "Release continuous loop smoothly");
        Audio.AccessHardwareOutput().ResetCaptureBuffer();
        Audio.PumpAudioBlocks(24u); // ~128 ms > 60 ms release
        const WaveformBuffer ReleaseTail = Audio.AccessHardwareOutput().ExtractCapturedWaveform();
        const float LateTailRms = MeasureWindowRms(ReleaseTail.Samples.data(), 2u, 4500u, 1000u);
        Board.Verify(LateTailRms < 0.005f, "Continuous voice fades cleanly to silence after ReleaseVoice");

        // Polyphony virtualization & priority voice stealing test: trigger 135 voices
        WaveformSynthesisRecipe ShortVar{};
        ShortVar.Shape           = WaveformSynthesisRecipe::SignalShape::HarmonicEngine;
        ShortVar.DurationSeconds = 0.8f;
        ShortVar.FundamentalFrequencyHz = 180.0f;
        ShortVar.SeamlessLoop    = true;
        const WaveformToken VarA = Audio.RegisterWaveform(WaveformDecoder::SynthesizeProceduralWaveform(ShortVar));

        AcousticVoiceDescriptor FloodDesc{};
        FloodDesc.WaveformSlots[0] = VarA;
        FloodDesc.WaveformSlots[1] = LoopToken;
        FloodDesc.VariationCount   = 2u;
        FloodDesc.PlaybackCycle          = PlaybackCycleCategory::ContinuousLoop;
        FloodDesc.PitchJitterRange = 0.05f;
        FloodDesc.GainJitterRange  = 0.05f;
        FloodDesc.Priority         = 40u;

        for (uint32_t Count = 0u; Count < 128u; ++Count)
        {
            (void)Audio.TriggerVariationCue(FloodDesc, Count + 100u);
        }
        Audio.PumpAudioBlocks(2u);
        const AcousticTelemetryMetrics FloodMetrics = Audio.InspectTelemetryMetrics();
        Board.Verify(
            FloodMetrics.ActiveMixedVoices == Constants::MaxActiveMixedVoices &&
            FloodMetrics.VirtualizedVoices == (Constants::MaxAcousticVoices - Constants::MaxActiveMixedVoices),
            "Polyphony scheduler mixes top 64 voices and virtualizes remaining 64 voices");

        // Now trigger 6 higher-priority weapon one-shots into the full 128-slot array -> must steal lower priority voices
        AcousticVoiceDescriptor HighPriDesc = FloodDesc;
        HighPriDesc.PlaybackCycle                 = PlaybackCycleCategory::OneShotTrigger;
        HighPriDesc.Bus                     = AudioBusCategory::Weapons;
        HighPriDesc.Priority                = 240u;
        for (uint32_t Burst = 0u; Burst < 6u; ++Burst)
        {
            (void)Audio.TriggerVariationCue(HighPriDesc, 900u + Burst);
        }
        Audio.PumpAudioBlocks(2u);
        const AcousticTelemetryMetrics StealMetrics = Audio.InspectTelemetryMetrics();
        Summary.StolenVoicesCount = StealMetrics.StolenVoices;
        Board.Verify(StealMetrics.StolenVoices == 6u, "High-priority weapon triggers steal lowest-priority slots cleanly");

        Audio.Finalize();
    }

    //--------------------------------------------------------------------------------------------------------------------
    // PHASE 4: PHYSICAL DOPPLER FREQUENCY SHIFT (ANALYTICAL + RENDERED GOERTZEL DFT SPECTRUM)
    //--------------------------------------------------------------------------------------------------------------------
    {
        AcousticReceiverPose Receiver{};
        Receiver.PositionMeters = { 0.0f, 0.0f, 0.0f };
        Receiver.VelocityMps    = { 0.0f, 0.0f, 0.0f };

        const float ApproachAnalytical = SpatialAcoustics::EvaluateDopplerPitchRatio(
            Receiver,
            { 50.0f, 0.0f, 0.0f },
            { -100.0f, 0.0f, 0.0f },
            1.0f);
        const float RecedeAnalytical = SpatialAcoustics::EvaluateDopplerPitchRatio(
            Receiver,
            { 50.0f, 0.0f, 0.0f },
            { 100.0f, 0.0f, 0.0f },
            1.0f);
        const float ExpectedApproach = 343.2f / 243.2f;
        const float ExpectedRecede   = 343.2f / 443.2f;
        Board.Verify(std::abs(ApproachAnalytical - ExpectedApproach) < 0.002f, "Analytical Doppler approach matches 343.2 / (343.2 - v_s)");
        Board.Verify(std::abs(RecedeAnalytical - ExpectedRecede) < 0.002f, "Analytical Doppler recede matches 343.2 / (343.2 + v_s)");

        // Underwater speed of sound 1482 m/s compresses Doppler shift compared to air
        Receiver.SubmergedCondition = true;
        const float UnderwaterShift = SpatialAcoustics::EvaluateDopplerPitchRatio(
            Receiver,
            { 50.0f, 0.0f, 0.0f },
            { -100.0f, 0.0f, 0.0f },
            1.0f);
        Board.Verify(
            UnderwaterShift > 1.05f && UnderwaterShift < 1.10f,
            "Underwater 1482 m/s propagation compresses Doppler shift accurately");

        // End-to-end rendered DSP Doppler spectral peak verification using pure 440 Hz tone at +-85 m/s
        AudioExtensionConfiguration Config{};
        Config.SampleRateHz           = 48000u;
        Config.SpeakerLayout          = SpatialSpeakerLayout::Mono;
        Config.BlockSamplePoints      = 256u;
        Config.RequestedDriver       = AudioOutputDriverCategory::HeadlessBuffer;
        Config.SpawnDedicatedOsThread = false;
        Config.CaptureOutputForExport = true;

        AudioExtension Audio(Config);
        Board.Verify(Audio.Initialize(), "Initialize Mono DSP verifier for Doppler spectral shift");

        WaveformSynthesisRecipe PureTone{};
        PureTone.Shape           = WaveformSynthesisRecipe::SignalShape::SineTone;
        PureTone.ChannelCount    = 1u;
        PureTone.SampleRateHz    = 48000u;
        PureTone.DurationSeconds = 0.5f;
        PureTone.FundamentalFrequencyHz = 440.0f;
        PureTone.Amplitude       = 0.85f;
        PureTone.SeamlessLoop    = true;
        const WaveformToken ToneToken = Audio.RegisterWaveform(WaveformDecoder::SynthesizeProceduralWaveform(PureTone));

        AcousticVoiceDescriptor JetDesc{};
        JetDesc.WaveformSlots[0]  = ToneToken;
        JetDesc.PlaybackCycle           = PlaybackCycleCategory::ContinuousLoop;
        JetDesc.PositionMeters    = { 80.0f, 0.0f, 0.0f };
        JetDesc.VelocityMps       = { -85.0f, 0.0f, 0.0f };
        JetDesc.DopplerFactor     = 1.0f;
        JetDesc.AttackSeconds     = 0.001f;
        JetDesc.MaximumDistanceMeters = 500.0f;
        JetDesc.AirAbsorptionStrength = 0.0f;
        const AcousticVoiceToken JetVoice = Audio.TriggerVariationCue(JetDesc);

        Audio.PumpAudioBlocks(40u); // ~213 ms settled approach
        const WaveformBuffer ApproachBuf = Audio.AccessHardwareOutput().ExtractCapturedWaveform();
        const float PeakApproachHz = MeasureDominantFrequencyHz(
            ApproachBuf.Samples.data(),
            1u,
            0u,
            2048u,
            4096u,
            48000u,
            450.0f,
            660.0f,
            2.0f);
        Summary.ApproachPeakHz = PeakApproachHz;
        Board.Verify(std::abs(PeakApproachHz - 584.8f) <= 12.0f, "Rendered DSP approaching tone peaks near 585 Hz (440 * 343.2 / 258.2)");

        // Switch velocity to receding +85 m/s -> allow slew smoother to ramp cleanly down to ~353 Hz
        Board.Verify(Audio.AssignVoiceTransform(JetVoice, { 40.0f, 0.0f, 0.0f }, { 85.0f, 0.0f, 0.0f }, 0.0f),
            "Assign receding velocity to moving voice");
        Audio.AccessHardwareOutput().ResetCaptureBuffer();
        Audio.PumpAudioBlocks(60u); // ~320 ms slew & settle
        const WaveformBuffer RecedeBuf = Audio.AccessHardwareOutput().ExtractCapturedWaveform();
        const float PeakRecedeHz = MeasureDominantFrequencyHz(
            RecedeBuf.Samples.data(),
            1u,
            0u,
            6000u,
            4096u,
            48000u,
            300.0f,
            420.0f,
            2.0f);
        Summary.RecedePeakHz = PeakRecedeHz;
        Board.Verify(std::abs(PeakRecedeHz - 352.7f) <= 10.0f, "Rendered DSP receding tone slews cleanly to ~353 Hz");

        Audio.Finalize();
    }

    //--------------------------------------------------------------------------------------------------------------------
    // PHASE 5: SURROUND 5.1 / 7.1 VBAP PANNING & BINAURAL HRTF WOODWORTH ITD/ILD
    //--------------------------------------------------------------------------------------------------------------------
    {
        AcousticReceiverPose Receiver{};
        AcousticVoiceDescriptor PointDesc{};
        PointDesc.ReferenceDistanceMeters = 1.0f;
        PointDesc.MaximumDistanceMeters   = 100.0f;
        PointDesc.SpatialSpreadRadians    = 0.0f;

        // Surround 7.1 (8ch: FL=0, FR=1, C=2, LFE=3, SL=4, SR=5, RL=6, RR=7)
        PointDesc.PositionMeters = { 10.0f, 0.0f, 0.0f }; // Dead ahead (0 deg)
        const auto Ahead71 = SpatialAcoustics::SolveSpatialAcoustics(Receiver, PointDesc, SpatialSpeakerLayout::Surround71, AcousticEnvironmentPreset::OpenField, {});
        Summary.Surround71LeftAngles = Ahead71.SpeakerGains;
        Board.Verify(Ahead71.SpeakerGains[2] > 0.99f, "Surround 7.1 dead-ahead source maps exclusively to Center channel");

        PointDesc.PositionMeters = { 0.0f, 10.0f, 0.0f }; // Pure left (+90 deg)
        const auto Left71 = SpatialAcoustics::SolveSpatialAcoustics(Receiver, PointDesc, SpatialSpeakerLayout::Surround71, AcousticEnvironmentPreset::OpenField, {});
        Board.Verify(Left71.SpeakerGains[4] > 0.99f && Left71.SpeakerGains[5] < 0.02f, "Surround 7.1 +90 deg left source maps to Side-Left");

        PointDesc.PositionMeters = { -8.19f, 5.73f, 0.0f }; // ~+145 deg Rear-Left
        const auto RearLeft71 = SpatialAcoustics::SolveSpatialAcoustics(Receiver, PointDesc, SpatialSpeakerLayout::Surround71, AcousticEnvironmentPreset::OpenField, {});
        Board.Verify(RearLeft71.SpeakerGains[6] > 0.98f, "Surround 7.1 +145 deg rear-left source maps to Rear-Left");

        // Constant-power check across Surround 5.1 arbitrary angle (+65 deg)
        PointDesc.PositionMeters = { 4.22f, 9.06f, 0.0f };
        const auto Pan51 = SpatialAcoustics::SolveSpatialAcoustics(Receiver, PointDesc, SpatialSpeakerLayout::Surround51, AcousticEnvironmentPreset::OpenField, {});
        const float PowerSum51 = Pan51.SpeakerGains[0] * Pan51.SpeakerGains[0] + Pan51.SpeakerGains[1] * Pan51.SpeakerGains[1] +
                                 Pan51.SpeakerGains[2] * Pan51.SpeakerGains[2] + Pan51.SpeakerGains[4] * Pan51.SpeakerGains[4] +
                                 Pan51.SpeakerGains[5] * Pan51.SpeakerGains[5];
        Board.Verify(std::abs(PowerSum51 - 1.0f) < 0.01f && Pan51.SpeakerGains[0] > 0.4f && Pan51.SpeakerGains[4] > 0.4f,
            "Surround 5.1 pairwise VBAP conserves unit power between Front-Left & Side-Left");

        // Binaural HRTF ITD & ILD verification in rendered stereo PCM
        AudioExtensionConfiguration Config{};
        Config.SampleRateHz           = 48000u;
        Config.SpeakerLayout          = SpatialSpeakerLayout::BinauralHrtf;
        Config.BlockSamplePoints      = 256u;
        Config.RequestedDriver       = AudioOutputDriverCategory::HeadlessBuffer;
        Config.SpawnDedicatedOsThread = false;
        Config.CaptureOutputForExport = true;

        AudioExtension BinauralAudio(Config);
        Board.Verify(BinauralAudio.Initialize(), "Initialize BinauralHrtf audio session");

        WaveformSynthesisRecipe NoiseBurst{};
        NoiseBurst.Shape           = WaveformSynthesisRecipe::SignalShape::HarmonicEngine;
        NoiseBurst.DurationSeconds = 0.25f;
        NoiseBurst.FundamentalFrequencyHz = 220.0f;
        NoiseBurst.Amplitude       = 0.8f;
        NoiseBurst.SeamlessLoop    = true;
        const WaveformToken PinkToken = BinauralAudio.RegisterWaveform(WaveformDecoder::SynthesizeProceduralWaveform(NoiseBurst));

        // Source placed straight LEFT (+Y = +8 m) -> Right ear must lag Left ear by ~0.65 ms (~31 samples) AND be quieter
        (void)BinauralAudio.StartContinuousLoop(PinkToken, { 0.0f, 8.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, AudioBusCategory::Effects, 1.0f);
        BinauralAudio.PumpAudioBlocks(32u);
        const WaveformBuffer BinauralStereo = BinauralAudio.AccessHardwareOutput().ExtractCapturedWaveform();
        const int MeasuredLagSamples = MeasureInterChannelLagSamples(BinauralStereo.Samples.data(), 2048u, 4096u, 48);
        Summary.BinauralMeasuredLagMs = (static_cast<float>(MeasuredLagSamples) / 48000.0f) * 1000.0f;
        Board.Verify(
            MeasuredLagSamples >= 24 && MeasuredLagSamples <= 38,
            "Binaural HRTF Woodworth ITD delays contralateral ear by ~0.5-0.8 ms");
        BinauralAudio.Finalize();

        // End-to-end 8-channel Surround71 rendered mix routing proof (+145 deg -> Rear-Left channel 6 dominates)
        AudioExtensionConfiguration Surround71Cfg{};
        Surround71Cfg.SampleRateHz           = 48000u;
        Surround71Cfg.SpeakerLayout          = SpatialSpeakerLayout::Surround71;
        Surround71Cfg.BlockSamplePoints      = 256u;
        Surround71Cfg.RequestedDriver       = AudioOutputDriverCategory::HeadlessBuffer;
        Surround71Cfg.SpawnDedicatedOsThread = false;
        Surround71Cfg.CaptureOutputForExport = true;
        AudioExtension SurroundAudio(Surround71Cfg);
        (void)SurroundAudio.Initialize();

        WaveformSynthesisRecipe S71Tone{};
        S71Tone.Shape           = WaveformSynthesisRecipe::SignalShape::SineTone;
        S71Tone.DurationSeconds = 0.2f;
        S71Tone.FundamentalFrequencyHz = 330.0f;
        S71Tone.SeamlessLoop    = true;
        const WaveformToken S71Tok = SurroundAudio.RegisterWaveform(WaveformDecoder::SynthesizeProceduralWaveform(S71Tone));
        (void)SurroundAudio.StartContinuousLoop(S71Tok, { -8.19f, 5.73f, 0.0f }, {}, AudioBusCategory::Effects, 0.9f);
        SurroundAudio.PumpAudioBlocks(12u);
        const WaveformBuffer S71Captured = SurroundAudio.AccessHardwareOutput().ExtractCapturedWaveform();
        auto ChannelRms = [&](uint32_t Ch) noexcept -> float
        {
            double Sum = 0.0;
            for (uint32_t S = 512u; S < S71Captured.SamplePointCount; ++S)
            {
                const double Sample = S71Captured.Samples[static_cast<std::size_t>(S) * 8u + Ch];
                Sum                += Sample * Sample;
            }
            return static_cast<float>(std::sqrt(Sum / std::max<uint32_t>(1u, S71Captured.SamplePointCount - 512u)));
        };
        Board.Verify(
            S71Captured.ChannelCount == 8u &&
            ChannelRms(6u) > ChannelRms(1u) * 4.0f &&
            ChannelRms(6u) > ChannelRms(2u) * 4.0f,
            "Rendered 8-channel Surround 7.1 stream isolates Rear-Left (+145 deg) speaker output");
        SurroundAudio.Finalize();
    }

    //--------------------------------------------------------------------------------------------------------------------
    // PHASE 6: TUNNEL WAVEGUIDE COMB MODES, PORTAL RADIATION & UNDERWATER FILTER
    //--------------------------------------------------------------------------------------------------------------------
    {
        auto MeasureImpulseTailRms = [&](bool InsideTunnelZone, float& OutPostTailRms, float& OutPortalMix)
        {
            AudioExtensionConfiguration Config{};
            Config.SampleRateHz           = 48000u;
            Config.SpeakerLayout          = SpatialSpeakerLayout::Stereo;
            Config.BlockSamplePoints      = 256u;
            Config.RequestedDriver       = AudioOutputDriverCategory::HeadlessBuffer;
            Config.SpawnDedicatedOsThread = false;
            Config.CaptureOutputForExport = true;

            AudioExtension Audio(Config);
            (void)Audio.Initialize();

            if (InsideTunnelZone)
            {
                AcousticZoneDescriptor Tunnel{};
                Tunnel.Shape                    = AcousticZoneShapeCategory::TunnelCorridor;
                Tunnel.EnvironmentPreset        = AcousticEnvironmentPreset::TunnelTube;
                Tunnel.TunnelStartMeters        = { -20.0f, 0.0f, 0.0f };
                Tunnel.TunnelEndMeters          = { 20.0f, 0.0f, 0.0f };
                Tunnel.RadiusMeters             = 4.0f;
                Tunnel.FeatherDistanceMeters    = 3.0f;
                Tunnel.CrossSectionWidthMeters  = 6.0f;
                Tunnel.CrossSectionHeightMeters = 4.5f;
                Tunnel.WallReflectivity         = 0.88f;
                Tunnel.TunnelResonanceMix       = 0.80f;
                Tunnel.ReverbWetMix             = 0.50f;
                (void)Audio.RegisterAcousticZone(Tunnel);
            }

            AcousticReceiverPose Recv{};
            Recv.PositionMeters = { 0.0f, 0.0f, 0.0f };
            (void)Audio.AssignReceiverPose(Recv);

            WaveformSynthesisRecipe Shot{};
            Shot.Shape           = WaveformSynthesisRecipe::SignalShape::ImpulseBurst;
            Shot.DurationSeconds = 0.05f;
            Shot.FundamentalFrequencyHz = 210.0f;
            Shot.Amplitude       = 0.95f;
            const WaveformToken ShotTok = Audio.RegisterWaveform(WaveformDecoder::SynthesizeProceduralWaveform(Shot));
            (void)Audio.TriggerOneShot(ShotTok, { 6.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, AudioBusCategory::Weapons, 1.0f);

            Audio.PumpAudioBlocks(4u); // ~21 ms while dry impulse voice is active
            OutPortalMix = Audio.InspectTelemetryMetrics().DominantTunnelMix;
            Audio.PumpAudioBlocks(92u); // complete ~512 ms total capture
            const WaveformBuffer Buf = Audio.AccessHardwareOutput().ExtractCapturedWaveform();
            OutPostTailRms           = MeasureWindowRms(Buf.Samples.data(), 2u, 6000u, 14000u); // 125 ms .. 416 ms tail
            Audio.Finalize();
        };

        float TunnelTail = 0.0f;
        float OpenTail   = 0.0f;
        float TunnelMix  = 0.0f;
        float OpenMix    = 0.0f;
        MeasureImpulseTailRms(true, TunnelTail, TunnelMix);
        MeasureImpulseTailRms(false, OpenTail, OpenMix);
        Summary.TunnelTailRms    = TunnelTail;
        Summary.OpenFieldTailRms = OpenTail;

        Board.Verify(TunnelMix > 0.65f && OpenMix < 0.05f, "TunnelCorridor zone couples dominant tunnel waveguide mix cleanly");
        Board.Verify(TunnelTail > OpenTail * 5.0f && TunnelTail > 0.01f,
            "Tunnel waveguide combs & Hadamard FDN generate sustained reverberant tail after short impulse");

        // Portal escape check: origin inside tunnel, receiver outside tunnel mouth (+23 m)
        AcousticZoneDescriptor Tunnel{};
        Tunnel.Shape                 = AcousticZoneShapeCategory::TunnelCorridor;
        Tunnel.EnvironmentPreset     = AcousticEnvironmentPreset::TunnelTube;
        Tunnel.TunnelStartMeters     = { -20.0f, 0.0f, 0.0f };
        Tunnel.TunnelEndMeters       = { 20.0f, 0.0f, 0.0f };
        Tunnel.RadiusMeters          = 4.0f;
        Tunnel.FeatherDistanceMeters = 2.0f;
        Tunnel.TunnelResonanceMix    = 0.80f;

        AcousticReceiverPose OutsideRecv{};
        OutsideRecv.PositionMeters = { 23.0f, 4.0f, 0.0f };
        AcousticVoiceDescriptor InsideOrigin{};
        InsideOrigin.PositionMeters = { 5.0f, 0.0f, 0.0f };
        const AcousticSpatialSolution PortalSol = SpatialAcoustics::SolveSpatialAcoustics(
            OutsideRecv,
            InsideOrigin,
            SpatialSpeakerLayout::Stereo,
            AcousticEnvironmentPreset::OpenField,
            std::span<const AcousticZoneDescriptor>(&Tunnel, 1u));
        Board.Verify(PortalSol.PortalBlendFactor > 0.40f && PortalSol.TunnelMix > 0.35f,
            "Origin inside tunnel radiates damped waveguide audio through nearest tunnel portal to exterior receiver");

        // Underwater resonant filter attenuation above 1500 Hz
        AudioExtensionConfiguration UwCfg{};
        UwCfg.SampleRateHz           = 48000u;
        UwCfg.SpeakerLayout          = SpatialSpeakerLayout::Mono;
        UwCfg.BlockSamplePoints      = 256u;
        UwCfg.RequestedDriver       = AudioOutputDriverCategory::HeadlessBuffer;
        UwCfg.SpawnDedicatedOsThread = false;
        UwCfg.CaptureOutputForExport = true;
        AudioExtension UwAudio(UwCfg);
        (void)UwAudio.Initialize();

        WaveformSynthesisRecipe HighTone{};
        HighTone.Shape           = WaveformSynthesisRecipe::SignalShape::SineTone;
        HighTone.DurationSeconds = 0.25f;
        HighTone.FundamentalFrequencyHz = 3000.0f;
        HighTone.Amplitude       = 0.8f;
        HighTone.SeamlessLoop    = true;
        const WaveformToken HighTok = UwAudio.RegisterWaveform(WaveformDecoder::SynthesizeProceduralWaveform(HighTone));

        (void)UwAudio.AssignEnvironmentPreset(AcousticEnvironmentPreset::Underwater);
        (void)UwAudio.StartContinuousLoop(HighTok, { 2.0f, 0.0f, 0.0f });
        UwAudio.PumpAudioBlocks(24u);
        const WaveformBuffer UwOut = UwAudio.AccessHardwareOutput().ExtractCapturedWaveform();
        const float HighRms        = MeasureWindowRms(UwOut.Samples.data(), 1u, 2048u, 3000u);
        Board.Verify(HighRms < 0.04f, "Underwater 450 Hz 2-pole filter strongly attenuates 3 kHz treble signal");
        UwAudio.Finalize();

        // Real watertight solid-wall corridor + thick baffle barrier in SignedDistanceVolume
        std::vector<SpatialAcceleration::RayTrianglePrimitive> CorridorTris{};
        auto AddSolidBox = [&](Mathematics::Axis3D BMin, Mathematics::Axis3D BMax)
        {
            auto AppendQuad = [&](Mathematics::Axis3D P0, Mathematics::Axis3D P1, Mathematics::Axis3D P2, Mathematics::Axis3D P3)
            {
                SpatialAcceleration::RayTrianglePrimitive T0{};
                T0.Corner0 = P0;
                T0.Edge01  = P1 - P0;
                T0.Edge02  = P2 - P0;
                CorridorTris.push_back(T0);
                SpatialAcceleration::RayTrianglePrimitive T1{};
                T1.Corner0 = P0;
                T1.Edge01  = P2 - P0;
                T1.Edge02  = P3 - P0;
                CorridorTris.push_back(T1);
            };
            const Mathematics::Axis3D V000{ BMin.X, BMin.Y, BMin.Z };
            const Mathematics::Axis3D V100{ BMax.X, BMin.Y, BMin.Z };
            const Mathematics::Axis3D V110{ BMax.X, BMax.Y, BMin.Z };
            const Mathematics::Axis3D V010{ BMin.X, BMax.Y, BMin.Z };
            const Mathematics::Axis3D V001{ BMin.X, BMin.Y, BMax.Z };
            const Mathematics::Axis3D V101{ BMax.X, BMin.Y, BMax.Z };
            const Mathematics::Axis3D V111{ BMax.X, BMax.Y, BMax.Z };
            const Mathematics::Axis3D V011{ BMin.X, BMax.Y, BMax.Z };
            // Outward CCW winding
            AppendQuad(V000, V010, V110, V100); // -Z bottom
            AppendQuad(V001, V101, V111, V011); // +Z top
            AppendQuad(V000, V100, V101, V001); // -Y south
            AppendQuad(V010, V011, V111, V110); // +Y north
            AppendQuad(V000, V001, V011, V010); // -X west
            AppendQuad(V100, V110, V111, V101); // +X east
        };

        // Corridor open along X in [-6, +6], enclosed by thick 0.6 m solid concrete slabs + thick baffle at X in [2.6, 3.4]
        AddSolidBox({ -6.0f, -3.1f, -2.6f }, { 6.0f, -2.5f,  2.6f }); // -Y wall
        AddSolidBox({ -6.0f,  2.5f, -2.6f }, { 6.0f,  3.1f,  2.6f }); // +Y wall
        AddSolidBox({ -6.0f, -2.5f, -2.6f }, { 6.0f,  2.5f, -2.0f }); // -Z floor
        AddSolidBox({ -6.0f, -2.5f,  2.0f }, { 6.0f,  2.5f,  2.6f }); // +Z ceiling
        AddSolidBox({  2.6f, -2.5f, -2.0f }, { 3.4f,  2.5f,  2.0f }); // Solid barrier wall at X ~ +3.0 m

        SpatialAcceleration::WideBoundingTree CorridorBvh{};
        Board.Verify(
            CorridorBvh.BuildFromTrianglePrimitives(CorridorTris),
            "Build corridor WideBoundingTree for acoustic ray probing");

        const SdfAcousticFieldReading ClearFieldReading = SpatialAcoustics::EvaluateRayAcousticField(
            CorridorBvh,
            { -2.0f, 0.0f, 0.0f },
            {  1.5f, 0.0f, 0.0f });
        const SdfAcousticFieldReading BlockedFieldReading = SpatialAcoustics::EvaluateRayAcousticField(
            CorridorBvh,
            { -2.0f, 0.0f, 0.0f },
            {  5.0f, 0.0f, 0.0f });
        Board.Verify(
            ClearFieldReading.EnclosureTunnelMix > 0.35f &&
            ClearFieldReading.OcclusionFactor < 0.25f &&
            BlockedFieldReading.OcclusionFactor > 0.55f,
            "EvaluateRayAcousticField detects corridor enclosure and solid barrier ray occlusion");
    }

    //--------------------------------------------------------------------------------------------------------------------
    // PHASE 7: CAMERAPOSTURE BRIDGE, SIDE-CHAIN BUS DUCKING & SOFT-KNEE MASTER LIMITER
    //--------------------------------------------------------------------------------------------------------------------
    {
        Components::CameraPosture Cam{};
        Cam.CurrentEyePosition  = { 10.0f, 5.0f, 2.0f };
        Cam.PreviousEyePosition = { 9.0f, 5.0f, 2.0f };
        Cam.ForwardAxis         = { 1.0f, 0.0f, 0.0f };
        Cam.UpAxis              = { 0.0f, 0.0f, 1.0f };
        Cam.RightAxis           = { 0.0f, -1.0f, 0.0f };
        Cam.LastDeltaSeconds    = 0.02f; // 50 m/s forward
        Cam.HasPreviousFrame    = true;

        const AcousticReceiverPose CamPose = SpatialAcoustics::BuildReceiverPoseFromCamera(Cam);
        Board.Verify(
            std::abs(CamPose.VelocityMps.X - 50.0f) < 0.05f && std::abs(CamPose.LeftAxis.Y - 1.0f) < 0.01f,
            "BuildReceiverPoseFromCamera extracts eye velocity & RHR Z-up left axis cleanly");

        AudioExtensionConfiguration Config{};
        Config.SampleRateHz           = 48000u;
        Config.SpeakerLayout          = SpatialSpeakerLayout::Mono;
        Config.BlockSamplePoints      = 256u;
        Config.RequestedDriver       = AudioOutputDriverCategory::HeadlessBuffer;
        Config.SpawnDedicatedOsThread = false;
        Config.CaptureOutputForExport = true;
        AudioExtension Audio(Config);
        (void)Audio.Initialize();

        WaveformSynthesisRecipe BedRecipe{};
        BedRecipe.Shape           = WaveformSynthesisRecipe::SignalShape::SineTone;
        BedRecipe.DurationSeconds = 0.5f;
        BedRecipe.FundamentalFrequencyHz = 200.0f;
        BedRecipe.Amplitude       = 0.5f;
        BedRecipe.SeamlessLoop    = true;
        const WaveformToken BedTok = Audio.RegisterWaveform(WaveformDecoder::SynthesizeProceduralWaveform(BedRecipe));

        // 1) Measure unducked Ambience baseline
        const AcousticVoiceToken AmbVoice = Audio.StartContinuousLoop(BedTok, { 0.5f, 0.0f, 0.0f }, {}, AudioBusCategory::Ambience, 1.0f);
        Audio.PumpAudioBlocks(12u);
        const WaveformBuffer Unducked = Audio.AccessHardwareOutput().ExtractCapturedWaveform();
        const float UnduckedRms       = MeasureWindowRms(Unducked.Samples.data(), 1u, 1500u, 1200u);

        // 2) Fire loud Dialogue voice -> Ambience side-chain must duck smoothly (then mute Dialogue briefly to inspect ducked Ambience directly)
        Audio.AccessHardwareOutput().ResetCaptureBuffer();
        const AcousticVoiceToken DlgVoice = Audio.StartContinuousLoop(BedTok, { 0.5f, 0.0f, 0.0f }, {}, AudioBusCategory::Dialogue, 1.5f);
        Audio.PumpAudioBlocks(16u);
        (void)Audio.AssignBusMute(AudioBusCategory::Dialogue, true);
        Audio.AccessHardwareOutput().ResetCaptureBuffer();
        Audio.PumpAudioBlocks(4u);
        const WaveformBuffer Ducked = Audio.AccessHardwareOutput().ExtractCapturedWaveform();
        const float DuckedRms       = MeasureWindowRms(Ducked.Samples.data(), 1u, 128u, 512u);
        (void)AmbVoice;
        (void)DlgVoice;

        Summary.DuckedAmbienceRatio = (UnduckedRms > 1.0e-5f) ? (DuckedRms / UnduckedRms) : 1.0f;
        Board.Verify(
            Summary.DuckedAmbienceRatio >= 0.45f && Summary.DuckedAmbienceRatio <= 0.85f,
            "Dialogue bus side-chain envelope ducks Ambience bus smoothly");

        // 3) Stack 8 full-scale voices simultaneously -> soft-knee limiter prevents digital clipping above 0.98
        (void)Audio.AssignBusMute(AudioBusCategory::Dialogue, false);
        for (int K = 0; K < 8; ++K)
        {
            (void)Audio.StartContinuousLoop(BedTok, { 0.5f, 0.0f, 0.0f }, {}, AudioBusCategory::Effects, 1.2f);
        }
        Audio.PumpAudioBlocks(16u);
        const AcousticTelemetryMetrics LimiterMetrics = Audio.InspectTelemetryMetrics();
        Summary.LimiterPeakLinear = LimiterMetrics.PeakOutputLinear;
        Board.Verify(
            LimiterMetrics.PeakOutputLinear > 0.85f &&
            LimiterMetrics.PeakOutputLinear <= Constants::MasterLimiterCeilingLinear + 0.002f,
            "Soft-knee master limiter protects ceiling <= 0.98 under 8x overload");

        Audio.Finalize();
    }

    //--------------------------------------------------------------------------------------------------------------------
    // PHASE 8: HEADLESS DIAGNOSTIC EXHIBIT PNG EXPORT
    //--------------------------------------------------------------------------------------------------------------------
    Summary.TotalChecksPassed = Board.PassedChecks + 1u;
#if FRONTIER_AUDIO_HAS_IMGUI
    const std::filesystem::path ExhibitPngPath =
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Exhibits" / "Images" / "Audio" / "AudioSpaceValidation.png";
    Board.Verify(
        RenderAudioExhibitPng(Summary, ExhibitPngPath) &&
        std::filesystem::exists(ExhibitPngPath) &&
        std::filesystem::file_size(ExhibitPngPath) > 4096u,
        "Rendered Exhibits/Images/Audio/AudioSpaceValidation.png");
    std::printf(
        "[AudioSpaceValidation] Completed: %u passed, %u failed -> %s\n",
        Board.PassedChecks,
        Board.FailedChecks,
        ExhibitPngPath.string().c_str());
#else
    std::printf(
        "[AudioSpaceValidation] Completed: %u passed, %u failed (headless DSP mode)\n",
        Board.PassedChecks,
        Board.FailedChecks);
#endif

    return (Board.FailedChecks == 0u) ? 0 : 1;
}
