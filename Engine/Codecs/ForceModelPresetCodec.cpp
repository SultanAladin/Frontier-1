/*============================================================================================================================================
                                                          FORCEMODELPRESETCODEC.CPP
============================================================================================================================================*/
// 🧩 Option A .ForceModel single-force preset encoder & decoder for reusable ContentBrowser presets

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ForceModelPresetCodec.h"
#include "Engine/Codecs/VectorFieldCodec.h"

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                              PRESET ENCODER & DECODER
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload ForceModelPresetEncoder::BuildPayload(
    const ForceModelPresetDocument&    Preset,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.SuffixCategory  = SpaceSuffixCategory::ForceModel;
    Payload.DocumentTitle      = Preset.PresetTitle;
    Payload.AuthoringWorkspace = "Frontier.ForcePresetLibrary";

    toml::table Header;
    Header.insert_or_assign("PresetTitle", Preset.PresetTitle);
    Header.insert_or_assign("AuthorNote", Preset.AuthorNote);
    Payload.DomainManifest.insert_or_assign(".VectorFieldHeader", std::move(Header));

    SerializeInlineForceSourcesToManifest(
        std::span<const SimulationSpace::ForceFields::ForceSourceRecordEntry>{ &Preset.ForceSource, 1u },
        {},
        Payload.DomainManifest);

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool ForceModelPresetEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const ForceModelPresetDocument&    Preset,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, BuildPayload(Preset, OptionalRevision), OutDiagnostic);
}

bool ForceModelPresetDecoder::ExtractPreset(
    const SpaceContainerPayload& Payload,
    ForceModelPresetDocument&    OutPreset,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.SuffixCategory != SpaceSuffixCategory::ForceModel)
    {
        OutDiagnostic = "Expected .ForceModel container category.";
        return false;
    }

    OutPreset             = ForceModelPresetDocument{};
    OutPreset.PresetTitle = Payload.DocumentTitle;

    if (const toml::table* Header = Payload.DomainManifest[".VectorFieldHeader"].as_table())
    {
        OutPreset.PresetTitle = (*Header)["PresetTitle"].value_or(Payload.DocumentTitle);
        OutPreset.AuthorNote  = (*Header)["AuthorNote"].value_or(std::string{});
    }

    std::vector<SimulationSpace::ForceFields::ForceSourceRecordEntry> Parsed;
    std::vector<std::string>                                          IgnoredLinks;
    DeserializeInlineForceSourcesFromManifest(Payload.DomainManifest, Parsed, IgnoredLinks);
    if (Parsed.empty())
    {
        OutDiagnostic = ".ForceModel container missing .ForceSource section.";
        return false;
    }
    OutPreset.ForceSource = std::move(Parsed.front());

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool ForceModelPresetDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    ForceModelPresetDocument&    OutPreset,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractPreset(Payload, OutPreset, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
