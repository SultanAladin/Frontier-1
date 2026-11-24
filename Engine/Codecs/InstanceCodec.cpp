/*============================================================================================================================================
                                                            INSTANCECODEC.CPP
============================================================================================================================================*/
// 🧩 Implementation of .Instance TomlWithBinarySection encoder & decoder with multi-format SpaceSlots & Shipping cook

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/InstanceCodec.h"

#include <cstring>

namespace Frontier::Codecs
{

namespace
{

[[nodiscard]] toml::array BuildVec3TomlArray(const std::array<float, 3>& Values)
{
    toml::array Array{};
    Array.push_back(static_cast<double>(Values[0]));
    Array.push_back(static_cast<double>(Values[1]));
    Array.push_back(static_cast<double>(Values[2]));
    return Array;
}

[[nodiscard]] std::array<float, 3> ReadVec3TomlArray(
    const toml::table&          TargetSection,
    std::string_view            Key,
    const std::array<float, 3>& Fallback) noexcept
{
    const toml::array* Array = TargetSection[Key].as_array();
    if ((Array == nullptr) || (Array->size() < 3u))
    {
        return Fallback;
    }

    std::array<float, 3> Result = Fallback;
    for (size_t i = 0u; i < 3u; ++i)
    {
        if (const auto DoubleValue = (*Array)[i].value<double>())
        {
            Result[i] = static_cast<float>(*DoubleValue);
        }
        else if (const auto IntValue = (*Array)[i].value<int64_t>())
        {
            Result[i] = static_cast<float>(*IntValue);
        }
    }
    return Result;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSTANCE ENCODER
//------------------------------------------------------------------------------------------------------------------------

uint64_t InstanceEncoder::ComputeDocumentHash64(std::string_view RelativePath) noexcept
{
    if (RelativePath.empty())
    {
        return 0ull;
    }

    const std::span<const std::byte> ByteView(
        reinterpret_cast<const std::byte*>(RelativePath.data()),
        RelativePath.size());
    return SpaceContainerEncoder::ComputeFnv1aChecksum64(ByteView);
}

SpaceContainerPayload InstanceEncoder::BuildPayload(
    const InstanceDocumentModel&       Model,
    bool                               StripEditorDirectives,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.SuffixCategory     = SpaceSuffixCategory::Instance;
    Payload.SchemaVersion      = Model.SchemaVersion;
    Payload.DocumentTitle      = Model.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.WorldInstance";

    toml::table InstanceSection{};
    InstanceSection.insert("Name", Model.DocumentTitle);
    InstanceSection.insert("Category", std::string(World::QueryInstanceCategoryName(Model.Category)));
    InstanceSection.insert("CategoryTag", std::string(World::QueryInstanceCategoryTag(Model.Category)));
    InstanceSection.insert("Version", static_cast<int64_t>(Model.SchemaVersion));
    Payload.DomainManifest.insert("Instance", std::move(InstanceSection));

    toml::table TransformSection{};
    TransformSection.insert("PositionMeters", BuildVec3TomlArray(Model.PositionMeters));
    TransformSection.insert("RotationDegrees", BuildVec3TomlArray(Model.RotationDegrees));
    TransformSection.insert("ScaleScalar", BuildVec3TomlArray(Model.ScaleScalar));
    Payload.DomainManifest.insert("Transform", std::move(TransformSection));

    toml::table DirectivesSection{};
    DirectivesSection.insert(
        "EnableVisibility",
        World::QueryDirectiveEnabled(Model.DirectiveMask, World::InstanceDirectiveMask::EnableVisibility));
    DirectivesSection.insert(
        "EnableSimulationTick",
        World::QueryDirectiveEnabled(Model.DirectiveMask, World::InstanceDirectiveMask::EnableSimulationTick));
    DirectivesSection.insert(
        "EnableShadowProjection",
        World::QueryDirectiveEnabled(Model.DirectiveMask, World::InstanceDirectiveMask::EnableShadowProjection));
    DirectivesSection.insert(
        "EnableStaticPlacement",
        World::QueryDirectiveEnabled(Model.DirectiveMask, World::InstanceDirectiveMask::EnableStaticPlacement));
    DirectivesSection.insert(
        "EnableDistanceFieldContribution",
        World::QueryDirectiveEnabled(
            Model.DirectiveMask,
            World::InstanceDirectiveMask::EnableDistanceFieldContribution));
    Payload.DomainManifest.insert("Directives", std::move(DirectivesSection));

    if (!StripEditorDirectives)
    {
        toml::table EditorSection{};
        EditorSection.insert(
            "OutlinerTitle",
            Model.EditorDirectives.OutlinerTitle.empty()
                ? Model.DocumentTitle
                : Model.EditorDirectives.OutlinerTitle);
        EditorSection.insert("CategoryLabel", Model.EditorDirectives.CategoryLabel);
        EditorSection.insert("Subtitle", Model.EditorDirectives.Subtitle);
        EditorSection.insert(
            "IconPath",
            Model.EditorDirectives.IconPath.empty()
                ? std::string(World::QueryInstanceCategoryIconPath(Model.Category))
                : Model.EditorDirectives.IconPath);
        EditorSection.insert("RemovalProtected", Model.EditorDirectives.RemovalProtected);
        EditorSection.insert("DuplicateProtected", Model.EditorDirectives.DuplicateProtected);
        Payload.DomainManifest.insert("Editor", std::move(EditorSection));
    }

    if (!Model.SpaceSlots.empty())
    {
        toml::array SlotList{};
        for (const InstanceSpaceSlotEntry& Slot : Model.SpaceSlots)
        {
            toml::table SlotEntry{};
            SlotEntry.insert("SlotRole", Slot.SlotRole);
            SlotEntry.insert("FormatExtension", Slot.FormatExtension);
            SlotEntry.insert("DocumentPath", Slot.DocumentPath);
            const uint64_t ResolvedHash =
                (Slot.DocumentHash64 != 0ull) ? Slot.DocumentHash64 : ComputeDocumentHash64(Slot.DocumentPath);
            SlotEntry.insert("DocumentHash64", static_cast<int64_t>(ResolvedHash));
            SlotList.push_back(std::move(SlotEntry));
        }
        Payload.DomainManifest.insert("SpaceSlots", std::move(SlotList));
    }

    if (!Model.BinaryDefaultPayload.empty())
    {
        BinarySectionBlock DefaultsBlock{};
        DefaultsBlock.SectionIdentifier = "InstanceDefaults";
        DefaultsBlock.SubFormatTag      = ".InstanceDefaults";
        DefaultsBlock.PayloadBytes      = Model.BinaryDefaultPayload;
        DefaultsBlock.Checksum64        = SpaceContainerEncoder::ComputeFnv1aChecksum64(DefaultsBlock.PayloadBytes);
        Payload.BinarySections.push_back(std::move(DefaultsBlock));
    }

    if (!StripEditorDirectives && (OptionalRevision != nullptr))
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }

    return Payload;
}

bool InstanceEncoder::EncodeToBytes(
    const InstanceDocumentModel&       Model,
    std::vector<std::byte>&            OutContainerBytes,
    std::string&                       OutDiagnostic,
    bool                               StripEditorDirectives,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    const SpaceContainerPayload Payload = BuildPayload(Model, StripEditorDirectives, OptionalRevision);
    return SpaceContainerEncoder::EncodeToBytes(Payload, OutContainerBytes, OutDiagnostic);
}

bool InstanceEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const InstanceDocumentModel&       Model,
    std::string&                       OutDiagnostic,
    bool                               StripEditorDirectives,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    const SpaceContainerPayload Payload = BuildPayload(Model, StripEditorDirectives, OptionalRevision);
    return SpaceContainerEncoder::EncodeToFile(TargetPath, Payload, OutDiagnostic);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSTANCE DECODER
//------------------------------------------------------------------------------------------------------------------------

bool InstanceDecoder::DecodeFromPayload(
    const SpaceContainerPayload& Payload,
    InstanceDocumentModel&       OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OutOptionalRevision)
{
    if ((Payload.SuffixCategory != SpaceSuffixCategory::Instance) &&
        (Payload.SuffixCategory != SpaceSuffixCategory::InstanceSpace))
    {
        OutDiagnostic = "Expected .Instance or .InstanceSpace container payload.";
        return false;
    }

    InstanceDocumentModel Decoded{};
    Decoded.DocumentTitle = Payload.DocumentTitle;
    Decoded.SchemaVersion = Payload.SchemaVersion;

    if (const toml::table* InstanceSection = Payload.DomainManifest["Instance"].as_table())
    {
        if (const auto NameValue = (*InstanceSection)["Name"].value<std::string>())
        {
            Decoded.DocumentTitle = *NameValue;
        }
        if (const auto CategoryText = (*InstanceSection)["Category"].value<std::string>())
        {
            Decoded.Category = World::ResolveInstanceCategoryName(*CategoryText);
        }
        if (const auto VersionValue = (*InstanceSection)["Version"].value<int64_t>())
        {
            Decoded.SchemaVersion = static_cast<uint32_t>(*VersionValue);
        }
    }

    if (const toml::table* TransformSection = Payload.DomainManifest["Transform"].as_table())
    {
        Decoded.PositionMeters  = ReadVec3TomlArray(*TransformSection, "PositionMeters", Decoded.PositionMeters);
        Decoded.RotationDegrees = ReadVec3TomlArray(*TransformSection, "RotationDegrees", Decoded.RotationDegrees);
        Decoded.ScaleScalar     = ReadVec3TomlArray(*TransformSection, "ScaleScalar", Decoded.ScaleScalar);
    }

    if (const toml::table* DirectivesSection = Payload.DomainManifest["Directives"].as_table())
    {
        World::InstanceDirectiveMask Mask = World::InstanceDirectiveMask::None;
        if ((*DirectivesSection)["EnableVisibility"].value_or(true))
        {
            Mask = Mask | World::InstanceDirectiveMask::EnableVisibility;
        }
        if ((*DirectivesSection)["EnableSimulationTick"].value_or(false))
        {
            Mask = Mask | World::InstanceDirectiveMask::EnableSimulationTick;
        }
        if ((*DirectivesSection)["EnableShadowProjection"].value_or(true))
        {
            Mask = Mask | World::InstanceDirectiveMask::EnableShadowProjection;
        }
        if ((*DirectivesSection)["EnableStaticPlacement"].value_or(false))
        {
            Mask = Mask | World::InstanceDirectiveMask::EnableStaticPlacement;
        }
        if ((*DirectivesSection)["EnableDistanceFieldContribution"].value_or(false))
        {
            Mask = Mask | World::InstanceDirectiveMask::EnableDistanceFieldContribution;
        }
        Decoded.DirectiveMask = static_cast<uint32_t>(Mask);
    }

    if (const toml::table* EditorSection = Payload.DomainManifest["Editor"].as_table())
    {
        Decoded.EditorDirectives.OutlinerTitle      = (*EditorSection)["OutlinerTitle"].value_or(std::string{});
        Decoded.EditorDirectives.CategoryLabel      = (*EditorSection)["CategoryLabel"].value_or(std::string{});
        Decoded.EditorDirectives.Subtitle           = (*EditorSection)["Subtitle"].value_or(std::string{});
        Decoded.EditorDirectives.IconPath           = (*EditorSection)["IconPath"].value_or(std::string{});
        Decoded.EditorDirectives.RemovalProtected   = (*EditorSection)["RemovalProtected"].value_or(false);
        Decoded.EditorDirectives.DuplicateProtected = (*EditorSection)["DuplicateProtected"].value_or(false);
    }

    if (const toml::array* SlotList = Payload.DomainManifest["SpaceSlots"].as_array())
    {
        Decoded.SpaceSlots.reserve(SlotList->size());
        for (const toml::node& Element : *SlotList)
        {
            const toml::table* SlotEntry = Element.as_table();
            if (SlotEntry == nullptr)
            {
                continue;
            }

            InstanceSpaceSlotEntry ParsedSlot{};
            ParsedSlot.SlotRole        = (*SlotEntry)["SlotRole"].value_or(std::string{});
            ParsedSlot.FormatExtension = (*SlotEntry)["FormatExtension"].value_or(std::string{});
            ParsedSlot.DocumentPath    = (*SlotEntry)["DocumentPath"].value_or(std::string{});
            const int64_t RawHash      = (*SlotEntry)["DocumentHash64"].value_or(int64_t{ 0 });
            ParsedSlot.DocumentHash64  = (RawHash != 0)
                ? static_cast<uint64_t>(RawHash)
                : InstanceEncoder::ComputeDocumentHash64(ParsedSlot.DocumentPath);
            Decoded.SpaceSlots.push_back(std::move(ParsedSlot));
        }
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".InstanceDefaults")
        {
            Decoded.BinaryDefaultPayload = Section.PayloadBytes;
        }
    }

    if (OutOptionalRevision != nullptr)
    {
        std::string RevisionDiagnostic{};
        (void)RevisionDecoder::ExtractRevisionSections(Payload, *OutOptionalRevision, RevisionDiagnostic);
    }

    OutModel = std::move(Decoded);
    OutDiagnostic.clear();
    return true;
}

bool InstanceDecoder::DecodeFromBytes(
    std::span<const std::byte>   InputBytes,
    InstanceDocumentModel&       OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OutOptionalRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromBytes(InputBytes, Payload, OutDiagnostic))
    {
        return false;
    }

    return DecodeFromPayload(Payload, OutModel, OutDiagnostic, OutOptionalRevision);
}

bool InstanceDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    InstanceDocumentModel&       OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OutOptionalRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }

    return DecodeFromPayload(Payload, OutModel, OutDiagnostic, OutOptionalRevision);
}

} // namespace Frontier::Codecs
