/*============================================================================================================================================
                                                             TEXTURECODEC.CPP
============================================================================================================================================*/
// 🧩 .TextureSpace encoder & decoder implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/TextureCodec.h"

#include <cstring>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                TEXTURE CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload TextureEncoder::BuildPayload(
    const TextureSpaceDocumentModel&   Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.ExtensionCategory  = SpaceExtensionCategory::TextureSpace;
    Payload.DocumentTitle      = Model.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.TextureWorkspace";

    toml::table PackMeta;
    PackMeta.insert_or_assign("ChannelPackLayout", Model.ChannelPackLayout);
    PackMeta.insert_or_assign("LinkedUVSpacePath", Model.LinkedUVSpacePath);
    Payload.DomainManifest.insert_or_assign(".ChannelPack", std::move(PackMeta));

    if (!Model.TileLayers.empty())
    {
        toml::array LayerArray;
        for (const TextureTileLayerEntry& Layer : Model.TileLayers)
        {
            toml::table Entry;
            Entry.insert_or_assign("UDIMNumber", static_cast<int64_t>(Layer.UDIMNumber));
            Entry.insert_or_assign("MipChainLevels", static_cast<int64_t>(Layer.MipChainLevels));
            Entry.insert_or_assign("ResolutionWidth", static_cast<int64_t>(Layer.ResolutionWidth));
            LayerArray.push_back(std::move(Entry));
        }
        Payload.DomainManifest.insert_or_assign(".TexelLayer", std::move(LayerArray));
    }

    if (!Model.PackedTexelBytes.empty())
    {
        BinarySectionBlock TexelSection{};
        TexelSection.SectionIdentifier = "PackedTexelStream";
        TexelSection.SubFormatTag      = ".TexelLayer";
        TexelSection.PayloadBytes.resize(Model.PackedTexelBytes.size());
        std::memcpy(TexelSection.PayloadBytes.data(), Model.PackedTexelBytes.data(), Model.PackedTexelBytes.size());
        Payload.BinarySections.push_back(std::move(TexelSection));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool TextureEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const TextureSpaceDocumentModel&   Model,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, BuildPayload(Model, OptionalRevision), OutDiagnostic);
}

bool TextureDecoder::ExtractModel(
    const SpaceContainerPayload& Payload,
    TextureSpaceDocumentModel&   OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.ExtensionCategory != SpaceExtensionCategory::TextureSpace)
    {
        OutDiagnostic = "Expected .TextureSpace container category.";
        return false;
    }

    OutModel               = TextureSpaceDocumentModel{};
    OutModel.DocumentTitle = Payload.DocumentTitle;
    if (const toml::table* PackMeta = Payload.DomainManifest[".ChannelPack"].as_table())
    {
        OutModel.ChannelPackLayout = (*PackMeta)["ChannelPackLayout"].value_or(std::string{});
        OutModel.LinkedUVSpacePath = (*PackMeta)["LinkedUVSpacePath"].value_or(std::string{});
    }

    if (const toml::array* LayerArray = Payload.DomainManifest[".TexelLayer"].as_array())
    {
        for (const auto& LayerRef : *LayerArray)
        {
            if (const toml::table* Entry = LayerRef.as_table())
            {
                TextureTileLayerEntry Layer{};
                Layer.UDIMNumber      = static_cast<uint32_t>((*Entry)["UDIMNumber"].value_or(int64_t{ 1001 }));
                Layer.MipChainLevels  = static_cast<uint32_t>((*Entry)["MipChainLevels"].value_or(int64_t{ 1 }));
                Layer.ResolutionWidth = static_cast<uint32_t>((*Entry)["ResolutionWidth"].value_or(int64_t{ 2048 }));
                OutModel.TileLayers.push_back(Layer);
            }
        }
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".TexelLayer")
        {
            OutModel.PackedTexelBytes.resize(Section.PayloadBytes.size());
            if (!Section.PayloadBytes.empty())
            {
                std::memcpy(OutModel.PackedTexelBytes.data(), Section.PayloadBytes.data(), Section.PayloadBytes.size());
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool TextureDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    TextureSpaceDocumentModel&   OutModel,
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
