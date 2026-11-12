/*============================================================================================================================================
                                                             MATERIALCODEC.CPP
============================================================================================================================================*/
// 🧩 .MaterialSpace encoder & decoder implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/MaterialCodec.h"

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                MATERIAL CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload MaterialEncoder::BuildPayload(
    const MaterialSpaceDocumentModel&  Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.ExtensionCategory  = SpaceExtensionCategory::MaterialSpace;
    Payload.DocumentTitle      = Model.MaterialTitle;
    Payload.AuthoringWorkspace = "Frontier.MaterialWorkspace";

    toml::table ShadingMeta;
    ShadingMeta.insert_or_assign("MaterialTitle", Model.MaterialTitle);
    ShadingMeta.insert_or_assign("ShaderSourceTag", Model.ShaderSourceTag);
    ShadingMeta.insert_or_assign("IndexOfRefraction", Model.IndexOfRefraction);
    Payload.DomainManifest.insert_or_assign(".ShadingModel", std::move(ShadingMeta));

    if (!Model.ParameterSlots.empty())
    {
        toml::array SlotArray;
        for (const MaterialParameterSlotEntry& Slot : Model.ParameterSlots)
        {
            toml::table Entry;
            Entry.insert_or_assign("ParameterName", Slot.ParameterName);
            Entry.insert_or_assign("ScalarMagnitude", Slot.ScalarMagnitude);
            Entry.insert_or_assign("TextureSpaceAsset", Slot.TextureSpaceAsset);
            SlotArray.push_back(std::move(Entry));
        }
        Payload.DomainManifest.insert_or_assign(".ParameterSlot", std::move(SlotArray));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool MaterialEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const MaterialSpaceDocumentModel&  Model,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, BuildPayload(Model, OptionalRevision), OutDiagnostic);
}

bool MaterialDecoder::ExtractModel(
    const SpaceContainerPayload& Payload,
    MaterialSpaceDocumentModel&  OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.ExtensionCategory != SpaceExtensionCategory::MaterialSpace)
    {
        OutDiagnostic = "Expected .MaterialSpace container category.";
        return false;
    }

    OutModel               = MaterialSpaceDocumentModel{};
    OutModel.MaterialTitle = Payload.DocumentTitle;
    if (const toml::table* ShadingMeta = Payload.DomainManifest[".ShadingModel"].as_table())
    {
        OutModel.MaterialTitle     = (*ShadingMeta)["MaterialTitle"].value_or(Payload.DocumentTitle);
        OutModel.ShaderSourceTag   = (*ShadingMeta)["ShaderSourceTag"].value_or(std::string{ "DirectionalLitShading.slang" });
        OutModel.IndexOfRefraction = (*ShadingMeta)["IndexOfRefraction"].value_or(1.5);
    }

    if (const toml::array* SlotArray = Payload.DomainManifest[".ParameterSlot"].as_array())
    {
        for (const auto& SlotRef : *SlotArray)
        {
            if (const toml::table* Entry = SlotRef.as_table())
            {
                MaterialParameterSlotEntry Slot{};
                Slot.ParameterName     = (*Entry)["ParameterName"].value_or(std::string{});
                Slot.ScalarMagnitude   = (*Entry)["ScalarMagnitude"].value_or(0.0);
                Slot.TextureSpaceAsset = (*Entry)["TextureSpaceAsset"].value_or(std::string{});
                OutModel.ParameterSlots.push_back(std::move(Slot));
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool MaterialDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    MaterialSpaceDocumentModel&  OutModel,
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
