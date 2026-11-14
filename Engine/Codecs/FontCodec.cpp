/*============================================================================================================================================
                                                               FONTCODEC.CPP
============================================================================================================================================*/
// 🧩 .FontSpace encoder & decoder implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/FontCodec.h"

#include <cstring>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FONT CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload FontEncoder::BuildPayload(
    const FontSpaceDocumentModel&      Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.ExtensionCategory  = SpaceExtensionCategory::FontSpace;
    Payload.DocumentTitle      = Model.TypefaceTitle;
    Payload.AuthoringWorkspace = "Frontier.TypographyWorkspace";

    toml::table CharacterMeta;
    CharacterMeta.insert_or_assign("TypefaceTitle", Model.TypefaceTitle);
    CharacterMeta.insert_or_assign("UnitsPerEm", static_cast<int64_t>(Model.UnitsPerEm));
    CharacterMeta.insert_or_assign("AscenderEm", Model.AscenderEm);
    CharacterMeta.insert_or_assign("DescenderEm", Model.DescenderEm);
    Payload.DomainManifest.insert_or_assign(".CharacterSet", std::move(CharacterMeta));

    if (!Model.KerningPairs.empty())
    {
        toml::array KerningArray;
        for (const KerningAdjustmentPair& Pair : Model.KerningPairs)
        {
            toml::table Entry;
            Entry.insert_or_assign("FirstCodepoint", static_cast<int64_t>(Pair.FirstCodepoint));
            Entry.insert_or_assign("SecondCodepoint", static_cast<int64_t>(Pair.SecondCodepoint));
            Entry.insert_or_assign("HorizontalEmStep", Pair.HorizontalEmStep);
            KerningArray.push_back(std::move(Entry));
        }
        Payload.DomainManifest.insert_or_assign(".Kerning", std::move(KerningArray));
    }

    if (!Model.SignedDistanceField.empty())
    {
        BinarySectionBlock ContourSection{};
        ContourSection.SectionIdentifier = "CharacterContourField";
        ContourSection.SubFormatTag      = ".CharacterSet";
        const std::size_t ByteLength     = Model.SignedDistanceField.size() * sizeof(float);
        ContourSection.PayloadBytes.resize(ByteLength);
        std::memcpy(ContourSection.PayloadBytes.data(), Model.SignedDistanceField.data(), ByteLength);
        Payload.BinarySections.push_back(std::move(ContourSection));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool FontEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const FontSpaceDocumentModel&      Model,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, BuildPayload(Model, OptionalRevision), OutDiagnostic);
}

bool FontDecoder::ExtractModel(
    const SpaceContainerPayload& Payload,
    FontSpaceDocumentModel&      OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.ExtensionCategory != SpaceExtensionCategory::FontSpace)
    {
        OutDiagnostic = "Expected .FontSpace container category.";
        return false;
    }

    OutModel               = FontSpaceDocumentModel{};
    OutModel.TypefaceTitle = Payload.DocumentTitle;
    if (const toml::table* CharacterMeta = Payload.DomainManifest[".CharacterSet"].as_table())
    {
        OutModel.TypefaceTitle = (*CharacterMeta)["TypefaceTitle"].value_or(Payload.DocumentTitle);
        OutModel.UnitsPerEm    = static_cast<uint32_t>((*CharacterMeta)["UnitsPerEm"].value_or(int64_t{ 2048 }));
        OutModel.AscenderEm    = (*CharacterMeta)["AscenderEm"].value_or(0.968);
        OutModel.DescenderEm   = (*CharacterMeta)["DescenderEm"].value_or(-0.242);
    }

    if (const toml::array* KerningArray = Payload.DomainManifest[".Kerning"].as_array())
    {
        for (const auto& PairRef : *KerningArray)
        {
            if (const toml::table* Entry = PairRef.as_table())
            {
                KerningAdjustmentPair Pair{};
                Pair.FirstCodepoint   = static_cast<uint32_t>((*Entry)["FirstCodepoint"].value_or(int64_t{ 0 }));
                Pair.SecondCodepoint  = static_cast<uint32_t>((*Entry)["SecondCodepoint"].value_or(int64_t{ 0 }));
                Pair.HorizontalEmStep = (*Entry)["HorizontalEmStep"].value_or(0.0);
                OutModel.KerningPairs.push_back(Pair);
            }
        }
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".CharacterSet" && (Section.PayloadBytes.size() % sizeof(float)) == 0u)
        {
            const std::size_t Count = Section.PayloadBytes.size() / sizeof(float);
            OutModel.SignedDistanceField.resize(Count);
            if (Count > 0u)
            {
                std::memcpy(OutModel.SignedDistanceField.data(), Section.PayloadBytes.data(), Section.PayloadBytes.size());
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool FontDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    FontSpaceDocumentModel&      OutModel,
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
