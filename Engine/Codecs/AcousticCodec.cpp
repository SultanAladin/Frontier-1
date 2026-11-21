/*============================================================================================================================================
                                                              ACOUSTICCODEC.CPP
============================================================================================================================================*/
// 🧩 .Acoustic Two-Tier Hybrid container encoder/decoder (TOML .AcousticProfile tier + 64B-aligned .AcousticWaveform float32 PCM tier)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/AcousticCodec.h"

#include <bit>
#include <cstring>

namespace Frontier::Codecs
{

namespace
{

void AppendLe32(std::vector<std::byte>& Buffer, uint32_t Word)
{
    Buffer.push_back(static_cast<std::byte>(Word & 0xFFu));
    Buffer.push_back(static_cast<std::byte>((Word >> 8u) & 0xFFu));
    Buffer.push_back(static_cast<std::byte>((Word >> 16u) & 0xFFu));
    Buffer.push_back(static_cast<std::byte>((Word >> 24u) & 0xFFu));
}

[[nodiscard]] bool ReadLe32(std::span<const std::byte> Bytes, std::size_t& Cursor, uint32_t& OutWord) noexcept
{
    if (Cursor + 4u > Bytes.size())
    {
        return false;
    }
    OutWord = static_cast<uint32_t>(std::to_integer<uint8_t>(Bytes[Cursor + 0u])) |
              (static_cast<uint32_t>(std::to_integer<uint8_t>(Bytes[Cursor + 1u])) << 8u) |
              (static_cast<uint32_t>(std::to_integer<uint8_t>(Bytes[Cursor + 2u])) << 16u) |
              (static_cast<uint32_t>(std::to_integer<uint8_t>(Bytes[Cursor + 3u])) << 24u);
    Cursor += 4u;
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 ACOUSTIC CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload AcousticEncoder::BuildPayload(
    const AcousticAssetDocument&       Document,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.SuffixCategory     = SpaceSuffixCategory::Acoustic;
    Payload.DocumentTitle      = Document.AssetTitle;
    Payload.AuthoringWorkspace = "Frontier.AudioSpace";

    toml::table ProfileSection;
    ProfileSection.insert_or_assign("AssetTitle",                Document.AssetTitle);
    ProfileSection.insert_or_assign("Bus",                       static_cast<int64_t>(Document.CueDescriptor.Bus));
    ProfileSection.insert_or_assign("PlaybackCycle",                   static_cast<int64_t>(Document.CueDescriptor.PlaybackCycle));
    ProfileSection.insert_or_assign("AttenuationCurve",          static_cast<int64_t>(Document.CueDescriptor.AttenuationCurve));
    ProfileSection.insert_or_assign("EnvironmentPreset",         static_cast<int64_t>(Document.Environment));
    ProfileSection.insert_or_assign("NominalGain",                  static_cast<double>(Document.CueDescriptor.NominalGain));
    ProfileSection.insert_or_assign("NominalPitch",                 static_cast<double>(Document.CueDescriptor.NominalPitch));
    ProfileSection.insert_or_assign("PitchJitterRange",          static_cast<double>(Document.CueDescriptor.PitchJitterRange));
    ProfileSection.insert_or_assign("GainJitterRange",           static_cast<double>(Document.CueDescriptor.GainJitterRange));
    ProfileSection.insert_or_assign("DopplerFactor",             static_cast<double>(Document.CueDescriptor.DopplerFactor));
    ProfileSection.insert_or_assign("SpatialBlend",              static_cast<double>(Document.CueDescriptor.SpatialBlend));
    ProfileSection.insert_or_assign("SpatialSpreadRadians",      static_cast<double>(Document.CueDescriptor.SpatialSpreadRadians));
    ProfileSection.insert_or_assign("ReferenceDistanceMeters",   static_cast<double>(Document.CueDescriptor.ReferenceDistanceMeters));
    ProfileSection.insert_or_assign("MaximumDistanceMeters",     static_cast<double>(Document.CueDescriptor.MaximumDistanceMeters));
    ProfileSection.insert_or_assign("RolloffExponent",           static_cast<double>(Document.CueDescriptor.RolloffExponent));
    ProfileSection.insert_or_assign("AirAbsorptionStrength",     static_cast<double>(Document.CueDescriptor.AirAbsorptionStrength));
    ProfileSection.insert_or_assign("AttackSeconds",             static_cast<double>(Document.CueDescriptor.AttackSeconds));
    ProfileSection.insert_or_assign("ReleaseSeconds",            static_cast<double>(Document.CueDescriptor.ReleaseSeconds));
    ProfileSection.insert_or_assign("LoopCrossfadeSamplePoints", static_cast<int64_t>(Document.CueDescriptor.LoopCrossfadeSamplePoints));
    ProfileSection.insert_or_assign("Priority",                  static_cast<int64_t>(Document.CueDescriptor.Priority));
    ProfileSection.insert_or_assign("VirtualizeWhenQuiet",       Document.CueDescriptor.VirtualizeWhenQuiet);
    Payload.DomainManifest.insert_or_assign(".AcousticProfile", std::move(ProfileSection));

    BinarySectionBlock WaveformSection{};
    WaveformSection.SectionIdentifier = "WaveformPcmPayload";
    WaveformSection.SubFormatTag      = ".AcousticWaveform";

    const std::size_t SampleByteCount = Document.Waveform.Samples.size() * sizeof(float);
    WaveformSection.PayloadBytes.reserve(24u + SampleByteCount);
    AppendLe32(WaveformSection.PayloadBytes, Document.Waveform.ChannelCount);
    AppendLe32(WaveformSection.PayloadBytes, Document.Waveform.SampleRateHz);
    AppendLe32(WaveformSection.PayloadBytes, Document.Waveform.SamplePointCount);
    AppendLe32(WaveformSection.PayloadBytes, Document.Waveform.LoopStartSamplePoint);
    AppendLe32(WaveformSection.PayloadBytes, Document.Waveform.LoopEndSamplePoint);
    AppendLe32(WaveformSection.PayloadBytes, Document.Waveform.LoopEnabled ? 1u : 0u);

    for (float SampleFloat : Document.Waveform.Samples)
    {
        AppendLe32(WaveformSection.PayloadBytes, std::bit_cast<uint32_t>(SampleFloat));
    }

    Payload.BinarySections.push_back(std::move(WaveformSection));

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool AcousticEncoder::EncodeToBytes(
    const AcousticAssetDocument&       Document,
    std::vector<std::byte>&            OutContainerBytes,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToBytes(BuildPayload(Document, OptionalRevision), OutContainerBytes, OutDiagnostic);
}

bool AcousticEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const AcousticAssetDocument&       Document,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, BuildPayload(Document, OptionalRevision), OutDiagnostic);
}

bool AcousticDecoder::ExtractDocument(
    const SpaceContainerPayload& Payload,
    AcousticAssetDocument&       OutDocument,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.SuffixCategory != SpaceSuffixCategory::Acoustic)
    {
        OutDiagnostic = "Expected .Acoustic container category.";
        return false;
    }

    OutDocument            = AcousticAssetDocument{};
    OutDocument.AssetTitle = Payload.DocumentTitle;

    if (const toml::table* Profile = Payload.DomainManifest[".AcousticProfile"].as_table())
    {
        OutDocument.AssetTitle = (*Profile)["AssetTitle"].value_or(Payload.DocumentTitle);

        const int64_t RawBus = (*Profile)["Bus"].value_or(int64_t{ 2 });
        if (RawBus >= 0 && RawBus < static_cast<int64_t>(Constants::MaxAudioBusCount))
        {
            OutDocument.CueDescriptor.Bus = static_cast<AudioSpace::AudioBusCategory>(RawBus);
        }

        const int64_t RawCycle = (*Profile)["PlaybackCycle"].value_or(int64_t{ 0 });
        if (RawCycle >= 0 && RawCycle <= static_cast<int64_t>(AudioSpace::PlaybackCycleCategory::DrivenContinuous))
        {
            OutDocument.CueDescriptor.PlaybackCycle = static_cast<AudioSpace::PlaybackCycleCategory>(RawCycle);
        }

        const int64_t RawCurve = (*Profile)["AttenuationCurve"].value_or(int64_t{ 0 });
        if (RawCurve >= 0 && RawCurve <= static_cast<int64_t>(AudioSpace::DistanceAttenuationCurve::Linear))
        {
            OutDocument.CueDescriptor.AttenuationCurve = static_cast<AudioSpace::DistanceAttenuationCurve>(RawCurve);
        }

        const int64_t RawEnv = (*Profile)["EnvironmentPreset"].value_or(int64_t{ 0 });
        if (RawEnv >= 0 && RawEnv <= static_cast<int64_t>(AudioSpace::AcousticEnvironmentPreset::Underwater))
        {
            OutDocument.Environment = static_cast<AudioSpace::AcousticEnvironmentPreset>(RawEnv);
        }

        OutDocument.CueDescriptor.NominalGain                  = static_cast<float>((*Profile)["NominalGain"].value_or(1.0));
        OutDocument.CueDescriptor.NominalPitch                 = static_cast<float>((*Profile)["NominalPitch"].value_or(1.0));
        OutDocument.CueDescriptor.PitchJitterRange          = static_cast<float>((*Profile)["PitchJitterRange"].value_or(0.0));
        OutDocument.CueDescriptor.GainJitterRange           = static_cast<float>((*Profile)["GainJitterRange"].value_or(0.0));
        OutDocument.CueDescriptor.DopplerFactor             = static_cast<float>((*Profile)["DopplerFactor"].value_or(1.0));
        OutDocument.CueDescriptor.SpatialBlend              = static_cast<float>((*Profile)["SpatialBlend"].value_or(1.0));
        OutDocument.CueDescriptor.SpatialSpreadRadians      = static_cast<float>((*Profile)["SpatialSpreadRadians"].value_or(0.15));
        OutDocument.CueDescriptor.ReferenceDistanceMeters   = static_cast<float>((*Profile)["ReferenceDistanceMeters"].value_or(2.0));
        OutDocument.CueDescriptor.MaximumDistanceMeters     = static_cast<float>((*Profile)["MaximumDistanceMeters"].value_or(120.0));
        OutDocument.CueDescriptor.RolloffExponent           = static_cast<float>((*Profile)["RolloffExponent"].value_or(1.0));
        OutDocument.CueDescriptor.AirAbsorptionStrength     = static_cast<float>((*Profile)["AirAbsorptionStrength"].value_or(1.0));
        OutDocument.CueDescriptor.AttackSeconds             = static_cast<float>((*Profile)["AttackSeconds"].value_or(0.005));
        OutDocument.CueDescriptor.ReleaseSeconds            = static_cast<float>((*Profile)["ReleaseSeconds"].value_or(0.045));
        OutDocument.CueDescriptor.LoopCrossfadeSamplePoints = static_cast<uint32_t>((*Profile)["LoopCrossfadeSamplePoints"].value_or(int64_t{ 512 }));
        OutDocument.CueDescriptor.Priority                  = static_cast<uint8_t>((*Profile)["Priority"].value_or(int64_t{ 128 }));
        OutDocument.CueDescriptor.VirtualizeWhenQuiet       = (*Profile)["VirtualizeWhenQuiet"].value_or(true);
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".AcousticWaveform")
        {
            std::span<const std::byte> Bytes(Section.PayloadBytes.data(), Section.PayloadBytes.size());
            std::size_t                Cursor     = 0u;
            uint32_t                   LoopMarker = 0u;

            if (!ReadLe32(Bytes, Cursor, OutDocument.Waveform.ChannelCount) ||
                !ReadLe32(Bytes, Cursor, OutDocument.Waveform.SampleRateHz) ||
                !ReadLe32(Bytes, Cursor, OutDocument.Waveform.SamplePointCount) ||
                !ReadLe32(Bytes, Cursor, OutDocument.Waveform.LoopStartSamplePoint) ||
                !ReadLe32(Bytes, Cursor, OutDocument.Waveform.LoopEndSamplePoint) ||
                !ReadLe32(Bytes, Cursor, LoopMarker))
            {
                OutDiagnostic = "Truncated .AcousticWaveform header.";
                return false;
            }
            OutDocument.Waveform.LoopEnabled = (LoopMarker != 0u);

            const std::size_t ExpectedFloatCount =
                static_cast<std::size_t>(OutDocument.Waveform.ChannelCount) *
                static_cast<std::size_t>(OutDocument.Waveform.SamplePointCount);
            if (Cursor + ExpectedFloatCount * sizeof(uint32_t) > Bytes.size())
            {
                OutDiagnostic = "Truncated .AcousticWaveform PCM sample block.";
                return false;
            }

            OutDocument.Waveform.Samples.resize(ExpectedFloatCount);
            for (std::size_t Idx = 0u; Idx < ExpectedFloatCount; ++Idx)
            {
                uint32_t Bits = 0u;
                if (!ReadLe32(Bytes, Cursor, Bits))
                {
                    return false;
                }
                OutDocument.Waveform.Samples[Idx] = std::bit_cast<float>(Bits);
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool AcousticDecoder::DecodeFromBytes(
    std::span<const std::byte>   ContainerBytes,
    AcousticAssetDocument&       OutDocument,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromBytes(ContainerBytes, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractDocument(Payload, OutDocument, OutDiagnostic, OptionalOutRevision);
}

bool AcousticDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    AcousticAssetDocument&       OutDocument,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractDocument(Payload, OutDocument, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
