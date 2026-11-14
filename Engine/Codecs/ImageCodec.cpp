/*============================================================================================================================================
                                                              IMAGECODEC.CPP
============================================================================================================================================*/
// 🧩 .ImageSpace encoder & decoder implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ImageCodec.h"

#include <cstring>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 IMAGE CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload ImageEncoder::BuildPayload(
    const ImageSpaceDocumentModel&     Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.ExtensionCategory  = SpaceExtensionCategory::ImageSpace;
    Payload.DocumentTitle      = Model.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.ImageWorkspace";

    toml::table StackOrder;
    StackOrder.insert_or_assign("NominalWidthPx", static_cast<int64_t>(Model.NominalWidthPx));
    StackOrder.insert_or_assign("NominalHeightPx", static_cast<int64_t>(Model.NominalHeightPx));
    StackOrder.insert_or_assign("LayerCount", static_cast<int64_t>(Model.Layers.size()));
    Payload.DomainManifest.insert_or_assign(".LayerStackOrder", std::move(StackOrder));

    if (!Model.Layers.empty())
    {
        toml::array LayerArray;
        for (const ImageCanvasLayerDescriptor& Layer : Model.Layers)
        {
            toml::table Entry;
            Entry.insert_or_assign("LayerTitle", Layer.LayerTitle);
            Entry.insert_or_assign("OpacityFactor", Layer.OpacityFactor);
            Entry.insert_or_assign("VisibilityEnabled", Layer.VisibilityEnabled);
            LayerArray.push_back(std::move(Entry));
        }
        Payload.DomainManifest.insert_or_assign(".CanvasLayer", std::move(LayerArray));
    }

    if (!Model.LinearRgbaPreview.empty())
    {
        BinarySectionBlock PixelSection{};
        PixelSection.SectionIdentifier = "LinearRgbaCanvas";
        PixelSection.SubFormatTag      = ".CanvasLayer";
        const std::size_t ByteLength   = Model.LinearRgbaPreview.size() * sizeof(float);
        PixelSection.PayloadBytes.resize(ByteLength);
        std::memcpy(PixelSection.PayloadBytes.data(), Model.LinearRgbaPreview.data(), ByteLength);
        Payload.BinarySections.push_back(std::move(PixelSection));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool ImageEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const ImageSpaceDocumentModel&     Model,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, BuildPayload(Model, OptionalRevision), OutDiagnostic);
}

bool ImageDecoder::ExtractModel(
    const SpaceContainerPayload& Payload,
    ImageSpaceDocumentModel&     OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.ExtensionCategory != SpaceExtensionCategory::ImageSpace)
    {
        OutDiagnostic = "Expected .ImageSpace container category.";
        return false;
    }

    OutModel               = ImageSpaceDocumentModel{};
    OutModel.DocumentTitle = Payload.DocumentTitle;
    if (const toml::table* StackOrder = Payload.DomainManifest[".LayerStackOrder"].as_table())
    {
        OutModel.NominalWidthPx  = static_cast<uint32_t>((*StackOrder)["NominalWidthPx"].value_or(int64_t{ 2048 }));
        OutModel.NominalHeightPx = static_cast<uint32_t>((*StackOrder)["NominalHeightPx"].value_or(int64_t{ 2048 }));
    }

    if (const toml::array* LayerArray = Payload.DomainManifest[".CanvasLayer"].as_array())
    {
        for (const auto& LayerRef : *LayerArray)
        {
            if (const toml::table* Entry = LayerRef.as_table())
            {
                ImageCanvasLayerDescriptor Layer{};
                Layer.LayerTitle        = (*Entry)["LayerTitle"].value_or(std::string{});
                Layer.OpacityFactor     = (*Entry)["OpacityFactor"].value_or(1.0);
                Layer.VisibilityEnabled = (*Entry)["VisibilityEnabled"].value_or(true);
                OutModel.Layers.push_back(std::move(Layer));
            }
        }
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".CanvasLayer" && (Section.PayloadBytes.size() % sizeof(float)) == 0u)
        {
            const std::size_t FloatCount = Section.PayloadBytes.size() / sizeof(float);
            OutModel.LinearRgbaPreview.resize(FloatCount);
            if (FloatCount > 0u)
            {
                std::memcpy(OutModel.LinearRgbaPreview.data(), Section.PayloadBytes.data(), Section.PayloadBytes.size());
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool ImageDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    ImageSpaceDocumentModel&     OutModel,
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
