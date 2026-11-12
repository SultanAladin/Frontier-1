/*============================================================================================================================================
                                                                UVCODEC.CPP
============================================================================================================================================*/
// 🧩 .UVSpace (.UDIM + .UVTile) encoder & decoder implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/UVCodec.h"

#include <cstring>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   UV CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload UVEncoder::BuildPayload(
    const UVSpaceDocumentModel&        Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.ExtensionCategory  = SpaceExtensionCategory::UVSpace;
    Payload.DocumentTitle      = Model.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.UVSpaceWorkspace";

    toml::table UdimSpec;
    UdimSpec.insert_or_assign("GridColumns", static_cast<int64_t>(Model.GridColumns));
    UdimSpec.insert_or_assign("ReferenceScaleMeters", Model.ReferenceScaleMeters);
    UdimSpec.insert_or_assign("TileCount", static_cast<int64_t>(Model.Tiles.size()));
    Payload.DomainManifest.insert_or_assign(".UDIM", std::move(UdimSpec));

    if (!Model.Tiles.empty())
    {
        toml::array TileArray;
        for (const UVTileDescriptor& Tile : Model.Tiles)
        {
            const uint32_t CanonicalUdim = Tile.UDIMNumber >= 1001u ? Tile.UDIMNumber : ComputeUDIMNumber(Tile.TileU, Tile.TileV);
            toml::table    TileEntry;
            TileEntry.insert_or_assign("TileU", static_cast<int64_t>(Tile.TileU));
            TileEntry.insert_or_assign("TileV", static_cast<int64_t>(Tile.TileV));
            TileEntry.insert_or_assign("UDIMNumber", static_cast<int64_t>(CanonicalUdim));
            TileEntry.insert_or_assign("TexelDensityPxPerMeter", Tile.TexelDensityPxPerMeter);
            TileEntry.insert_or_assign("GutterPaddingPixels", Tile.GutterPaddingPixels);
            TileArray.push_back(std::move(TileEntry));
        }
        Payload.DomainManifest.insert_or_assign(".UVTile", std::move(TileArray));
    }

    if (!Model.ParameterizedUVPairs.empty())
    {
        BinarySectionBlock UVCoordinateSection{};
        UVCoordinateSection.SectionIdentifier = "UVTileCoordinates";
        UVCoordinateSection.SubFormatTag      = ".UVTile";
        const std::size_t ByteLength          = Model.ParameterizedUVPairs.size() * sizeof(float);
        UVCoordinateSection.PayloadBytes.resize(ByteLength);
        std::memcpy(UVCoordinateSection.PayloadBytes.data(), Model.ParameterizedUVPairs.data(), ByteLength);
        Payload.BinarySections.push_back(std::move(UVCoordinateSection));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool UVEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const UVSpaceDocumentModel&        Model,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, BuildPayload(Model, OptionalRevision), OutDiagnostic);
}

bool UVDecoder::ExtractModel(
    const SpaceContainerPayload& Payload,
    UVSpaceDocumentModel&        OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.ExtensionCategory != SpaceExtensionCategory::UVSpace)
    {
        OutDiagnostic = "Expected .UVSpace container category.";
        return false;
    }

    OutModel               = UVSpaceDocumentModel{};
    OutModel.DocumentTitle = Payload.DocumentTitle;
    if (const toml::table* UdimSpec = Payload.DomainManifest[".UDIM"].as_table())
    {
        OutModel.GridColumns          = static_cast<uint32_t>((*UdimSpec)["GridColumns"].value_or(int64_t{ 10 }));
        OutModel.ReferenceScaleMeters = (*UdimSpec)["ReferenceScaleMeters"].value_or(1.0);
    }

    if (const toml::array* TileArray = Payload.DomainManifest[".UVTile"].as_array())
    {
        for (const auto& TileRef : *TileArray)
        {
            if (const toml::table* TileEntry = TileRef.as_table())
            {
                UVTileDescriptor Tile{};
                Tile.TileU                  = static_cast<int32_t>((*TileEntry)["TileU"].value_or(int64_t{ 0 }));
                Tile.TileV                  = static_cast<int32_t>((*TileEntry)["TileV"].value_or(int64_t{ 0 }));
                Tile.UDIMNumber             = static_cast<uint32_t>((*TileEntry)["UDIMNumber"].value_or(
                    static_cast<int64_t>(UVEncoder::ComputeUDIMNumber(Tile.TileU, Tile.TileV))));
                Tile.TexelDensityPxPerMeter = (*TileEntry)["TexelDensityPxPerMeter"].value_or(512.0);
                Tile.GutterPaddingPixels    = (*TileEntry)["GutterPaddingPixels"].value_or(8.0);
                OutModel.Tiles.push_back(Tile);
            }
        }
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".UVTile" && (Section.PayloadBytes.size() % sizeof(float)) == 0u)
        {
            const std::size_t FloatCount = Section.PayloadBytes.size() / sizeof(float);
            OutModel.ParameterizedUVPairs.resize(FloatCount);
            if (FloatCount > 0u)
            {
                std::memcpy(OutModel.ParameterizedUVPairs.data(), Section.PayloadBytes.data(), Section.PayloadBytes.size());
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool UVDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    UVSpaceDocumentModel&        OutModel,
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
