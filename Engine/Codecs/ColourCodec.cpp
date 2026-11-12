/*============================================================================================================================================
                                                              COLOURCODEC.CPP
============================================================================================================================================*/
// 🧩 .ColourSpace encoder & decoder implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ColourCodec.h"

#include <cstring>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 COLOUR CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload ColourEncoder::BuildPayload(
    const ColourSpaceDocumentModel&    Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.ExtensionCategory  = SpaceExtensionCategory::ColourSpace;
    Payload.DocumentTitle      = Model.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.ColourWorkspace";

    toml::table RampMeta;
    RampMeta.insert_or_assign("ReferencePrimaries", Model.ReferencePrimaries);
    RampMeta.insert_or_assign("WhitePointKelvin", Model.WhitePointKelvin);
    Payload.DomainManifest.insert_or_assign(".Ramp", std::move(RampMeta));

    if (!Model.Swatches.empty())
    {
        toml::array SwatchArray;
        for (const RadiometricSwatchEntry& Swatch : Model.Swatches)
        {
            toml::table Entry;
            Entry.insert_or_assign("SwatchName", Swatch.SwatchName);
            Entry.insert_or_assign("LinearRed", Swatch.LinearRed);
            Entry.insert_or_assign("LinearGreen", Swatch.LinearGreen);
            Entry.insert_or_assign("LinearBlue", Swatch.LinearBlue);
            Entry.insert_or_assign("Opacity", Swatch.Opacity);
            SwatchArray.push_back(std::move(Entry));
        }
        Payload.DomainManifest.insert_or_assign(".Swatch", std::move(SwatchArray));
    }

    if (!Model.SpectralSamplesNanometer.empty())
    {
        BinarySectionBlock SpectrumSection{};
        SpectrumSection.SectionIdentifier = "SpectralDistribution";
        SpectrumSection.SubFormatTag      = ".Spectrum";
        const std::size_t ByteCount       = Model.SpectralSamplesNanometer.size() * sizeof(float);
        SpectrumSection.PayloadBytes.resize(ByteCount);
        std::memcpy(SpectrumSection.PayloadBytes.data(), Model.SpectralSamplesNanometer.data(), ByteCount);
        Payload.BinarySections.push_back(std::move(SpectrumSection));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool ColourEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const ColourSpaceDocumentModel&    Model,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, BuildPayload(Model, OptionalRevision), OutDiagnostic);
}

bool ColourDecoder::ExtractModel(
    const SpaceContainerPayload& Payload,
    ColourSpaceDocumentModel&    OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.ExtensionCategory != SpaceExtensionCategory::ColourSpace)
    {
        OutDiagnostic = "Expected .ColourSpace container category.";
        return false;
    }

    OutModel               = ColourSpaceDocumentModel{};
    OutModel.DocumentTitle = Payload.DocumentTitle;
    if (const toml::table* Ramp = Payload.DomainManifest[".Ramp"].as_table())
    {
        OutModel.ReferencePrimaries = (*Ramp)["ReferencePrimaries"].value_or(std::string{ "ACEScg" });
        OutModel.WhitePointKelvin   = (*Ramp)["WhitePointKelvin"].value_or(6504.0);
    }

    if (const toml::array* SwatchArray = Payload.DomainManifest[".Swatch"].as_array())
    {
        for (const auto& SwatchRef : *SwatchArray)
        {
            if (const toml::table* Entry = SwatchRef.as_table())
            {
                RadiometricSwatchEntry Swatch{};
                Swatch.SwatchName  = (*Entry)["SwatchName"].value_or(std::string{});
                Swatch.LinearRed   = (*Entry)["LinearRed"].value_or(0.0);
                Swatch.LinearGreen = (*Entry)["LinearGreen"].value_or(0.0);
                Swatch.LinearBlue  = (*Entry)["LinearBlue"].value_or(0.0);
                Swatch.Opacity     = (*Entry)["Opacity"].value_or(1.0);
                OutModel.Swatches.push_back(std::move(Swatch));
            }
        }
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".Spectrum" && (Section.PayloadBytes.size() % sizeof(float)) == 0u)
        {
            const std::size_t SampleCount = Section.PayloadBytes.size() / sizeof(float);
            OutModel.SpectralSamplesNanometer.resize(SampleCount);
            if (SampleCount > 0u)
            {
                std::memcpy(OutModel.SpectralSamplesNanometer.data(), Section.PayloadBytes.data(), Section.PayloadBytes.size());
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool ColourDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    ColourSpaceDocumentModel&    OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractModel(Payload, OutModel, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
