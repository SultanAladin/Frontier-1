/*============================================================================================================================================
                                                            WAVEFORMDECODER.CPP
============================================================================================================================================*/
// 🧩 RIFF WAV / procedural PCM waveform decoding, sample-rate conversion, Catmull-Rom cubic interpolation, and slot repository

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/AudioSpace/WaveformDecoder.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

namespace Frontier::AudioSpace
{

namespace
{

[[nodiscard]] uint16_t ReadLe16(const std::byte* Ptr) noexcept
{
    return static_cast<uint16_t>(std::to_integer<uint8_t>(Ptr[0])) |
           (static_cast<uint16_t>(std::to_integer<uint8_t>(Ptr[1])) << 8u);
}

[[nodiscard]] uint32_t ReadLe32(const std::byte* Ptr) noexcept
{
    return static_cast<uint32_t>(std::to_integer<uint8_t>(Ptr[0])) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(Ptr[1])) << 8u) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(Ptr[2])) << 16u) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(Ptr[3])) << 24u);
}

[[nodiscard]] int32_t ReadLeSigned24(const std::byte* Ptr) noexcept
{
    const uint32_t Unsigned24 =
        static_cast<uint32_t>(std::to_integer<uint8_t>(Ptr[0])) |
        (static_cast<uint32_t>(std::to_integer<uint8_t>(Ptr[1])) << 8u) |
        (static_cast<uint32_t>(std::to_integer<uint8_t>(Ptr[2])) << 16u);
    if ((Unsigned24 & 0x00800000u) != 0u)
    {
        return static_cast<int32_t>(Unsigned24 | 0xFF000000u);
    }
    return static_cast<int32_t>(Unsigned24);
}

void WriteLe16(std::vector<std::byte>& Buffer, uint16_t Word)
{
    Buffer.push_back(static_cast<std::byte>(Word & 0xFFu));
    Buffer.push_back(static_cast<std::byte>((Word >> 8u) & 0xFFu));
}

void WriteLe32(std::vector<std::byte>& Buffer, uint32_t Word)
{
    Buffer.push_back(static_cast<std::byte>(Word & 0xFFu));
    Buffer.push_back(static_cast<std::byte>((Word >> 8u) & 0xFFu));
    Buffer.push_back(static_cast<std::byte>((Word >> 16u) & 0xFFu));
    Buffer.push_back(static_cast<std::byte>((Word >> 24u) & 0xFFu));
}

[[nodiscard]] bool MatchFourCc(const std::byte* Ptr, const char (&Tag)[5]) noexcept
{
    return std::to_integer<char>(Ptr[0]) == Tag[0] &&
           std::to_integer<char>(Ptr[1]) == Tag[1] &&
           std::to_integer<char>(Ptr[2]) == Tag[2] &&
           std::to_integer<char>(Ptr[3]) == Tag[3];
}

[[nodiscard]] float EvaluateCatmullRom(float P0, float P1, float P2, float P3, float Mu) noexcept
{
    const float Mu2 = Mu * Mu;
    const float Mu3 = Mu2 * Mu;
    const float A0  = -0.5f * P0 + 1.5f * P1 - 1.5f * P2 + 0.5f * P3;
    const float A1  = P0 - 2.5f * P1 + 2.0f * P2 - 0.5f * P3;
    const float A2  = -0.5f * P0 + 0.5f * P2;
    const float A3  = P1;
    return ((A0 * Mu3) + (A1 * Mu2) + (A2 * Mu)) + A3;
}

[[nodiscard]] uint32_t AdvanceXorShift32(uint32_t& Seed) noexcept
{
    uint32_t X = (Seed == 0u) ? 0x9E3779B9u : Seed;
    X ^= X << 13u;
    X ^= X >> 17u;
    X ^= X << 5u;
    Seed = X;
    return X;
}

[[nodiscard]] float NextBipolarWhite(uint32_t& Seed) noexcept
{
    const uint32_t Bits = AdvanceXorShift32(Seed);
    return (static_cast<float>(Bits & 0x00FFFFFFu) / 8388608.0f) - 1.0f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  WAVEFORM BUFFER METHODS
//------------------------------------------------------------------------------------------------------------------------

double WaveformBuffer::ComputeDurationSeconds() const noexcept
{
    if (SampleRateHz == 0u || SamplePointCount == 0u)
    {
        return 0.0;
    }
    return static_cast<double>(SamplePointCount) / static_cast<double>(SampleRateHz);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   DECODER OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

bool WaveformDecoder::DecodeWavFromBytes(
    std::span<const std::byte> RawWavBytes,
    WaveformBuffer&            OutWaveform,
    std::string&               OutDiagnostic)
{
    OutDiagnostic.clear();
    OutWaveform = {};

    if (RawWavBytes.size() < 44u)
    {
        OutDiagnostic = "RIFF WAV buffer is smaller than minimum 44-byte header.";
        return false;
    }

    if (!MatchFourCc(RawWavBytes.data(), "RIFF") || !MatchFourCc(RawWavBytes.data() + 8u, "WAVE"))
    {
        OutDiagnostic = "Header does not match RIFF/WAVE signature.";
        return false;
    }

    uint16_t FormatCode         = 0u;
    uint16_t Channels           = 0u;
    uint32_t SampleRateHz       = 0u;
    uint16_t BitsPerSample      = 0u;
    bool     FmtChunkFound      = false;
    bool     SampleBlockFound   = false;
    std::span<const std::byte> SamplePayload {};
    uint32_t LoopStart          = 0u;
    uint32_t LoopEnd            = 0u;
    bool     LoopChunkFound     = false;

    std::size_t Cursor = 12u;
    while (Cursor + 8u <= RawWavBytes.size())
    {
        const std::byte* ChunkPtr  = RawWavBytes.data() + Cursor;
        const uint32_t   ChunkSize = ReadLe32(ChunkPtr + 4u);
        Cursor += 8u;

        if (Cursor + ChunkSize > RawWavBytes.size())
        {
            OutDiagnostic = "Truncated RIFF chunk encountered while scanning WAV.";
            return false;
        }

        if (MatchFourCc(ChunkPtr, "fmt "))
        {
            if (ChunkSize < 16u)
            {
                OutDiagnostic = "WAV fmt chunk is smaller than 16 bytes.";
                return false;
            }
            FormatCode    = ReadLe16(RawWavBytes.data() + Cursor + 0u);
            Channels      = ReadLe16(RawWavBytes.data() + Cursor + 2u);
            SampleRateHz  = ReadLe32(RawWavBytes.data() + Cursor + 4u);
            BitsPerSample = ReadLe16(RawWavBytes.data() + Cursor + 14u);

            if (FormatCode == 0xFFFEu && ChunkSize >= 40u)
            {
                FormatCode = ReadLe16(RawWavBytes.data() + Cursor + 24u);
            }
            FmtChunkFound = true;
        }
        else if (MatchFourCc(ChunkPtr, "data"))
        {
            SamplePayload    = RawWavBytes.subspan(Cursor, ChunkSize);
            SampleBlockFound = true;
        }
        else if (MatchFourCc(ChunkPtr, "smpl") && ChunkSize >= 36u)
        {
            const uint32_t LoopCount = ReadLe32(RawWavBytes.data() + Cursor + 28u);
            if (LoopCount > 0u && ChunkSize >= 60u)
            {
                LoopStart      = ReadLe32(RawWavBytes.data() + Cursor + 44u);
                LoopEnd        = ReadLe32(RawWavBytes.data() + Cursor + 48u);
                LoopChunkFound = (LoopEnd > LoopStart);
            }
        }

        Cursor += ChunkSize;
        if ((ChunkSize & 1u) != 0u && Cursor < RawWavBytes.size())
        {
            ++Cursor;
        }
    }

    if (!FmtChunkFound || !SampleBlockFound)
    {
        OutDiagnostic = "WAV file missing mandatory fmt or sample payload chunk.";
        return false;
    }

    if (Channels == 0u || Channels > Constants::MaxAcousticOutputChannels || SampleRateHz == 0u)
    {
        OutDiagnostic = "Unsupported WAV channel count or zero sample rate.";
        return false;
    }

    const uint32_t BytesPerSample = BitsPerSample / 8u;
    const uint32_t BytesPerPoint  = BytesPerSample * Channels;
    if (BytesPerPoint == 0u)
    {
        OutDiagnostic = "Invalid WAV bit depth.";
        return false;
    }

    const uint32_t TotalSamplePoints = static_cast<uint32_t>(SamplePayload.size() / BytesPerPoint);
    OutWaveform.ChannelCount         = Channels;
    OutWaveform.SampleRateHz         = SampleRateHz;
    OutWaveform.SamplePointCount     = TotalSamplePoints;
    OutWaveform.Samples.resize(static_cast<std::size_t>(TotalSamplePoints) * Channels, 0.0f);

    const std::size_t TotalFloats = OutWaveform.Samples.size();
    const std::byte*  BytesPtr    = SamplePayload.data();

    if (FormatCode == 1u && BitsPerSample == 16u)
    {
        constexpr float Scale = 1.0f / 32768.0f;
        for (std::size_t Idx = 0u; Idx < TotalFloats; ++Idx)
        {
            const int16_t Raw = static_cast<int16_t>(ReadLe16(BytesPtr + Idx * 2u));
            OutWaveform.Samples[Idx] = std::clamp(static_cast<float>(Raw) * Scale, -1.0f, 1.0f);
        }
    }
    else if (FormatCode == 1u && BitsPerSample == 24u)
    {
        constexpr float Scale = 1.0f / 8388608.0f;
        for (std::size_t Idx = 0u; Idx < TotalFloats; ++Idx)
        {
            const int32_t Raw = ReadLeSigned24(BytesPtr + Idx * 3u);
            OutWaveform.Samples[Idx] = std::clamp(static_cast<float>(Raw) * Scale, -1.0f, 1.0f);
        }
    }
    else if (FormatCode == 1u && BitsPerSample == 32u)
    {
        constexpr float Scale = 1.0f / 2147483648.0f;
        for (std::size_t Idx = 0u; Idx < TotalFloats; ++Idx)
        {
            const int32_t Raw = static_cast<int32_t>(ReadLe32(BytesPtr + Idx * 4u));
            OutWaveform.Samples[Idx] = std::clamp(static_cast<float>(Raw) * Scale, -1.0f, 1.0f);
        }
    }
    else if (FormatCode == 3u && BitsPerSample == 32u)
    {
        for (std::size_t Idx = 0u; Idx < TotalFloats; ++Idx)
        {
            float DecodedFloat = 0.0f;
            std::memcpy(&DecodedFloat, BytesPtr + Idx * 4u, sizeof(float));
            OutWaveform.Samples[Idx] = std::isfinite(DecodedFloat) ? DecodedFloat : 0.0f;
        }
    }
    else
    {
        OutDiagnostic = "Unsupported WAV encoding (expected PCM 16/24/32-bit or IEEE float32).";
        OutWaveform   = {};
        return false;
    }

    if (LoopChunkFound && LoopEnd <= TotalSamplePoints)
    {
        OutWaveform.LoopStartSamplePoint = LoopStart;
        OutWaveform.LoopEndSamplePoint   = LoopEnd;
        OutWaveform.LoopEnabled          = true;
    }
    else
    {
        OutWaveform.LoopStartSamplePoint = 0u;
        OutWaveform.LoopEndSamplePoint   = TotalSamplePoints;
        OutWaveform.LoopEnabled          = false;
    }

    return true;
}

bool WaveformDecoder::LoadWavFromFile(
    const std::filesystem::path& FilePath,
    WaveformBuffer&              OutWaveform,
    std::string&                 OutDiagnostic)
{
    std::ifstream Stream(FilePath, std::ios::binary | std::ios::ate);
    if (!Stream.is_open())
    {
        OutDiagnostic = "Unable to open WAV file: " + FilePath.string();
        return false;
    }

    const std::streamsize FileSize = Stream.tellg();
    if (FileSize <= 0)
    {
        OutDiagnostic = "WAV file is empty: " + FilePath.string();
        return false;
    }

    std::vector<std::byte> Bytes(static_cast<std::size_t>(FileSize));
    Stream.seekg(0, std::ios::beg);
    if (!Stream.read(reinterpret_cast<char*>(Bytes.data()), FileSize))
    {
        OutDiagnostic = "Failed reading WAV file payload: " + FilePath.string();
        return false;
    }

    return DecodeWavFromBytes(Bytes, OutWaveform, OutDiagnostic);
}

bool WaveformDecoder::EncodeWavToBytes(
    const WaveformBuffer& Waveform,
    std::vector<std::byte>& OutWavBytes,
    std::string&          OutDiagnostic)
{
    OutDiagnostic.clear();
    OutWavBytes.clear();

    if (Waveform.ChannelCount == 0u || Waveform.ChannelCount > Constants::MaxAcousticOutputChannels ||
        Waveform.SampleRateHz == 0u)
    {
        OutDiagnostic = "Cannot encode WAV with zero channels or zero sample rate.";
        return false;
    }

    const uint32_t SampleBytes     = static_cast<uint32_t>(Waveform.Samples.size() * sizeof(float));
    const uint32_t FmtChunkBytes   = 16u;
    const uint32_t SmplChunkBytes  = Waveform.LoopEnabled ? 60u : 0u;
    const uint32_t TotalPayload    = 4u + (8u + FmtChunkBytes) + (8u + SampleBytes) +
                                     (Waveform.LoopEnabled ? (8u + SmplChunkBytes) : 0u);

    OutWavBytes.reserve(8u + TotalPayload);

    auto AppendFourCc = [&](const char (&Tag)[5])
    {
        for (int Idx = 0; Idx < 4; ++Idx)
        {
            OutWavBytes.push_back(static_cast<std::byte>(Tag[Idx]));
        }
    };

    AppendFourCc("RIFF");
    WriteLe32(OutWavBytes, TotalPayload);
    AppendFourCc("WAVE");

    AppendFourCc("fmt ");
    WriteLe32(OutWavBytes, FmtChunkBytes);
    WriteLe16(OutWavBytes, 3u); // IEEE float32
    WriteLe16(OutWavBytes, static_cast<uint16_t>(Waveform.ChannelCount));
    WriteLe32(OutWavBytes, Waveform.SampleRateHz);
    WriteLe32(OutWavBytes, Waveform.SampleRateHz * Waveform.ChannelCount * 4u);
    WriteLe16(OutWavBytes, static_cast<uint16_t>(Waveform.ChannelCount * 4u));
    WriteLe16(OutWavBytes, 32u);

    AppendFourCc("data");
    WriteLe32(OutWavBytes, SampleBytes);
    const std::size_t OldSize = OutWavBytes.size();
    OutWavBytes.resize(OldSize + SampleBytes);
    if (SampleBytes > 0u)
    {
        std::memcpy(OutWavBytes.data() + OldSize, Waveform.Samples.data(), SampleBytes);
    }

    if (Waveform.LoopEnabled)
    {
        AppendFourCc("smpl");
        WriteLe32(OutWavBytes, SmplChunkBytes);
        for (int ZeroWord = 0; ZeroWord < 7; ++ZeroWord)
        {
            WriteLe32(OutWavBytes, 0u);
        }
        WriteLe32(OutWavBytes, 1u); // SampleLoopCount
        WriteLe32(OutWavBytes, 0u); // SamplerSpecificBytes
        WriteLe32(OutWavBytes, 0u); // CuePointIdentifier
        WriteLe32(OutWavBytes, 0u); // ForwardLoopMode
        WriteLe32(OutWavBytes, Waveform.LoopStartSamplePoint);
        WriteLe32(OutWavBytes, Waveform.LoopEndSamplePoint);
        WriteLe32(OutWavBytes, 0u);
        WriteLe32(OutWavBytes, 0u);
    }

    return true;
}

bool WaveformDecoder::WriteWavToFile(
    const std::filesystem::path& FilePath,
    const WaveformBuffer&        Waveform,
    std::string&                 OutDiagnostic)
{
    std::vector<std::byte> EncodedBytes{};
    if (!EncodeWavToBytes(Waveform, EncodedBytes, OutDiagnostic))
    {
        return false;
    }

    if (FilePath.has_parent_path())
    {
        std::error_code FsFailure{};
        std::filesystem::create_directories(FilePath.parent_path(), FsFailure);
    }

    std::ofstream Stream(FilePath, std::ios::binary | std::ios::trunc);
    if (!Stream.is_open())
    {
        OutDiagnostic = "Unable to create WAV output file: " + FilePath.string();
        return false;
    }

    Stream.write(reinterpret_cast<const char*>(EncodedBytes.data()), static_cast<std::streamsize>(EncodedBytes.size()));
    if (!Stream.good())
    {
        OutDiagnostic = "Failed writing WAV bytes to: " + FilePath.string();
        return false;
    }
    return true;
}

WaveformBuffer WaveformDecoder::ResampleToRate(
    const WaveformBuffer& SourceWaveform,
    uint32_t              TargetSampleRateHz)
{
    if (TargetSampleRateHz == 0u || SourceWaveform.SampleRateHz == 0u ||
        SourceWaveform.SampleRateHz == TargetSampleRateHz || SourceWaveform.SamplePointCount == 0u)
    {
        return SourceWaveform;
    }

    const double   RateScale         = static_cast<double>(TargetSampleRateHz) / static_cast<double>(SourceWaveform.SampleRateHz);
    const uint32_t OutputPointCount  = std::max(1u, static_cast<uint32_t>(std::llround(SourceWaveform.SamplePointCount * RateScale)));
    const double   SourceStep        = static_cast<double>(SourceWaveform.SampleRateHz) / static_cast<double>(TargetSampleRateHz);

    WaveformBuffer Converted{};
    Converted.ChannelCount         = SourceWaveform.ChannelCount;
    Converted.SampleRateHz         = TargetSampleRateHz;
    Converted.SamplePointCount     = OutputPointCount;
    Converted.LoopEnabled          = SourceWaveform.LoopEnabled;
    Converted.LoopStartSamplePoint = std::min(
        OutputPointCount - 1u,
        static_cast<uint32_t>(std::llround(SourceWaveform.LoopStartSamplePoint * RateScale)));
    Converted.LoopEndSamplePoint   = std::clamp(
        static_cast<uint32_t>(std::llround(SourceWaveform.LoopEndSamplePoint * RateScale)),
        Converted.LoopStartSamplePoint + 1u,
        OutputPointCount);
    Converted.Samples.resize(static_cast<std::size_t>(OutputPointCount) * Converted.ChannelCount, 0.0f);

    for (uint32_t OutIdx = 0u; OutIdx < OutputPointCount; ++OutIdx)
    {
        const double SrcPos = static_cast<double>(OutIdx) * SourceStep;
        for (uint32_t Ch = 0u; Ch < Converted.ChannelCount; ++Ch)
        {
            Converted.Samples[static_cast<std::size_t>(OutIdx) * Converted.ChannelCount + Ch] =
                SampleChannelCubic(SourceWaveform, Ch, SrcPos, SourceWaveform.LoopEnabled);
        }
    }

    return Converted;
}

WaveformBuffer WaveformDecoder::SynthesizeProceduralWaveform(const WaveformSynthesisRecipe& Recipe)
{
    const uint32_t SampleRate  = std::max(8000u, Recipe.SampleRateHz);
    const uint32_t Channels    = std::clamp(Recipe.ChannelCount, 1u, Constants::MaxAcousticOutputChannels);
    const uint32_t PointCount  = std::max(64u, static_cast<uint32_t>(std::llround(std::max(0.01f, Recipe.DurationSeconds) * SampleRate)));

    WaveformBuffer Buffer{};
    Buffer.ChannelCount         = Channels;
    Buffer.SampleRateHz         = SampleRate;
    Buffer.SamplePointCount     = PointCount;
    Buffer.LoopStartSamplePoint = 0u;
    Buffer.LoopEndSamplePoint   = PointCount;
    Buffer.LoopEnabled          = Recipe.SeamlessLoop;
    Buffer.Samples.resize(static_cast<std::size_t>(PointCount) * Channels, 0.0f);

    double   PhaseRadians     = 0.0;
    double   FreqHz           = static_cast<double>(std::max(10.0f, Recipe.FundamentalFrequencyHz));
    if (Recipe.SeamlessLoop)
    {
        const double ExactCycles = std::max(1.0, std::round(FreqHz * static_cast<double>(PointCount) / static_cast<double>(SampleRate)));
        FreqHz                   = ExactCycles * static_cast<double>(SampleRate) / static_cast<double>(PointCount);
    }

    const double PhaseStep    = Constants::Tau * FreqHz / static_cast<double>(SampleRate);
    uint32_t     RngSeed      = Recipe.RandomSeed;
    float        PinkFilter0  = 0.0f;
    float        PinkFilter1  = 0.0f;
    float        PinkFilter2  = 0.0f;

    for (uint32_t Idx = 0u; Idx < PointCount; ++Idx)
    {
        const double TimeSec = static_cast<double>(Idx) / static_cast<double>(SampleRate);
        float        Signal  = 0.0f;

        switch (Recipe.Shape)
        {
            case WaveformSynthesisRecipe::SignalShape::SineTone:
            {
                Signal = static_cast<float>(std::sin(PhaseRadians));
                break;
            }
            case WaveformSynthesisRecipe::SignalShape::HarmonicEngine:
            {
                const double H1 = std::sin(PhaseRadians);
                const double H2 = 0.45 * std::sin(2.0 * PhaseRadians);
                const double H3 = 0.25 * std::sin(3.0 * PhaseRadians);
                const double H5 = 0.12 * std::sin(5.0 * PhaseRadians);
                Signal          = static_cast<float>((H1 + H2 + H3 + H5) / 1.82);
                break;
            }
            case WaveformSynthesisRecipe::SignalShape::ImpulseBurst:
            {
                const float WhiteSample   = NextBipolarWhite(RngSeed);
                const float TransientEnv  = std::exp(-static_cast<float>(TimeSec) * 38.0f);
                const float BodyPunchEnv  = std::exp(-static_cast<float>(TimeSec) * 14.0f);
                const float PunchSine     = static_cast<float>(std::sin(PhaseRadians * std::exp(-TimeSec * 4.5)));
                Signal                    = 0.65f * TransientEnv * WhiteSample + 0.55f * BodyPunchEnv * PunchSine;
                break;
            }
            case WaveformSynthesisRecipe::SignalShape::PinkAmbience:
            {
                const float White = NextBipolarWhite(RngSeed);
                PinkFilter0       = 0.99765f * PinkFilter0 + White * 0.0990460f;
                PinkFilter1       = 0.96300f * PinkFilter1 + White * 0.2965164f;
                PinkFilter2       = 0.57000f * PinkFilter2 + White * 1.0526913f;
                const float Pink  = (PinkFilter0 + PinkFilter1 + PinkFilter2 + White * 0.1848f) * 0.16f;
                const float Wind  = 0.25f * static_cast<float>(std::sin(Constants::Tau * (static_cast<double>(Idx) / PointCount) * 2.0));
                Signal            = Pink * (0.75f + Wind);
                break;
            }
        }

        Signal *= Recipe.Amplitude;
        for (uint32_t Ch = 0u; Ch < Channels; ++Ch)
        {
            const float ChannelScale = (Channels > 1u && Ch == 1u && Recipe.Shape == WaveformSynthesisRecipe::SignalShape::PinkAmbience)
                                           ? -0.92f
                                           : 1.0f;
            Buffer.Samples[static_cast<std::size_t>(Idx) * Channels + Ch] = std::clamp(Signal * ChannelScale, -1.0f, 1.0f);
        }
        PhaseRadians += PhaseStep;
    }

    if (Recipe.SeamlessLoop && Recipe.Shape == WaveformSynthesisRecipe::SignalShape::PinkAmbience && PointCount >= 512u)
    {
        const uint32_t CrossfadeCount = std::min(512u, PointCount / 4u);
        for (uint32_t Idx = 0u; Idx < CrossfadeCount; ++Idx)
        {
            const float    Ratio  = static_cast<float>(Idx) / static_cast<float>(CrossfadeCount);
            const float    WeightA = std::cos(Ratio * 0.5f * Constants::PiFloat);
            const float    WeightB = std::sin(Ratio * 0.5f * Constants::PiFloat);
            const uint32_t TailIdx = PointCount - CrossfadeCount + Idx;
            for (uint32_t Ch = 0u; Ch < Channels; ++Ch)
            {
                const float TailSample = Buffer.Samples[static_cast<std::size_t>(TailIdx) * Channels + Ch];
                const float HeadSample = Buffer.Samples[static_cast<std::size_t>(Idx) * Channels + Ch];
                Buffer.Samples[static_cast<std::size_t>(Idx) * Channels + Ch] = HeadSample * WeightB + TailSample * WeightA;
            }
        }
    }

    return Buffer;
}

float WaveformDecoder::SampleChannelCubic(
    const WaveformBuffer& Waveform,
    uint32_t              ChannelIndex,
    double                SamplePosition,
    bool                  WrapLoopBounds) noexcept
{
    if (Waveform.SamplePointCount == 0u || Waveform.ChannelCount == 0u)
    {
        return 0.0f;
    }

    const uint32_t SafeChannel = std::min(ChannelIndex, Waveform.ChannelCount - 1u);
    const uint32_t LoopStart   = (WrapLoopBounds && Waveform.LoopEnabled) ? Waveform.LoopStartSamplePoint : 0u;
    const uint32_t LoopEnd     = (WrapLoopBounds && Waveform.LoopEnabled && Waveform.LoopEndSamplePoint > LoopStart)
                                     ? std::min(Waveform.LoopEndSamplePoint, Waveform.SamplePointCount)
                                     : Waveform.SamplePointCount;
    const uint32_t SpanLength  = std::max(1u, LoopEnd - LoopStart);

    auto FetchAtIndex = [&](int64_t RawIdx) noexcept -> float
    {
        int64_t ResolvedIdx = RawIdx;
        if (WrapLoopBounds)
        {
            const int64_t Rel = (RawIdx - static_cast<int64_t>(LoopStart)) % static_cast<int64_t>(SpanLength);
            ResolvedIdx       = static_cast<int64_t>(LoopStart) + (Rel < 0 ? Rel + static_cast<int64_t>(SpanLength) : Rel);
        }
        else
        {
            if (RawIdx < 0 || RawIdx >= static_cast<int64_t>(Waveform.SamplePointCount))
            {
                return 0.0f;
            }
        }
        return Waveform.Samples[static_cast<std::size_t>(ResolvedIdx) * Waveform.ChannelCount + SafeChannel];
    };

    const int64_t StartSample = static_cast<int64_t>(std::floor(SamplePosition));
    const float   Mu      = static_cast<float>(SamplePosition - static_cast<double>(StartSample));
    const float   S0      = FetchAtIndex(StartSample - 1);
    const float   S1      = FetchAtIndex(StartSample + 0);
    const float   S2      = FetchAtIndex(StartSample + 1);
    const float   S3      = FetchAtIndex(StartSample + 2);
    return EvaluateCatmullRom(S0, S1, S2, S3, Mu);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 WAVEFORM REPOSITORY METHODS
//------------------------------------------------------------------------------------------------------------------------

void WaveformRepository::Reset() noexcept
{
    for (WaveformSlot& Slot : Slots)
    {
        Slot.ReadyForAudio.store(false, std::memory_order_release);
        Slot.Occupied.store(false, std::memory_order_release);
        Slot.ResidentBuffer = {};
        const uint32_t NextGen = Slot.Generation.load(std::memory_order_relaxed) + 1u;
        Slot.Generation.store(NextGen == 0u ? 1u : NextGen, std::memory_order_release);
    }
    ResidentCounter.store(0u, std::memory_order_release);
}

WaveformToken WaveformRepository::RegisterWaveform(WaveformBuffer Waveform, uint32_t TargetSampleRateHz) noexcept
{
    if (Waveform.SamplePointCount == 0u || Waveform.ChannelCount == 0u)
    {
        return {};
    }

    if (TargetSampleRateHz != 0u && Waveform.SampleRateHz != TargetSampleRateHz)
    {
        Waveform = WaveformDecoder::ResampleToRate(Waveform, TargetSampleRateHz);
    }

    for (uint32_t Index = 0u; Index < Constants::MaxAcousticWaveforms; ++Index)
    {
        bool ExpectedFree = false;
        if (Slots[Index].Occupied.compare_exchange_strong(ExpectedFree, true, std::memory_order_acq_rel))
        {
            Slots[Index].ReadyForAudio.store(false, std::memory_order_release);
            Slots[Index].ResidentBuffer = std::move(Waveform);
            const uint32_t Gen          = std::max(1u, Slots[Index].Generation.load(std::memory_order_acquire));
            Slots[Index].ReadyForAudio.store(true, std::memory_order_release);
            ResidentCounter.fetch_add(1u, std::memory_order_relaxed);
            return WaveformToken{ Index + 1u, Gen };
        }
    }

    return {};
}

bool WaveformRepository::ReclaimWaveform(WaveformToken Token) noexcept
{
    if (!Token || Token.SlotIndex > Constants::MaxAcousticWaveforms)
    {
        return false;
    }

    WaveformSlot& Slot = Slots[Token.SlotIndex - 1u];
    if (!Slot.Occupied.load(std::memory_order_acquire) ||
        Slot.Generation.load(std::memory_order_acquire) != Token.Generation)
    {
        return false;
    }

    Slot.ReadyForAudio.store(false, std::memory_order_release);
    const uint32_t NextGen = Slot.Generation.load(std::memory_order_acquire) + 1u;
    Slot.Generation.store(NextGen == 0u ? 1u : NextGen, std::memory_order_release);
    Slot.ResidentBuffer = {};
    Slot.Occupied.store(false, std::memory_order_release);
    ResidentCounter.fetch_sub(1u, std::memory_order_relaxed);
    return true;
}

bool WaveformRepository::AuthenticateToken(WaveformToken Token) const noexcept
{
    if (!Token || Token.SlotIndex > Constants::MaxAcousticWaveforms)
    {
        return false;
    }
    const WaveformSlot& Slot = Slots[Token.SlotIndex - 1u];
    return Slot.Occupied.load(std::memory_order_acquire) &&
           Slot.ReadyForAudio.load(std::memory_order_acquire) &&
           Slot.Generation.load(std::memory_order_acquire) == Token.Generation;
}

const WaveformBuffer* WaveformRepository::ResolveResidentWaveform(WaveformToken Token) const noexcept
{
    if (!AuthenticateToken(Token))
    {
        return nullptr;
    }
    return &Slots[Token.SlotIndex - 1u].ResidentBuffer;
}

uint32_t WaveformRepository::QueryResidentCount() const noexcept
{
    return ResidentCounter.load(std::memory_order_acquire);
}

} // namespace Frontier::AudioSpace
