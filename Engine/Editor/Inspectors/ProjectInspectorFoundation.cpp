/*============================================================================================================================================
                                                       PROJECTINSPECTORFOUNDATION.CPP
============================================================================================================================================*/
// 🧩 Implementation of Project-Defined User UI Inspector loader (.Inspector.toml via tomlpp) and native Layer A renderer

#include "Engine/Editor/Inspectors/ProjectInspectorFoundation.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace Frontier
{

namespace
{
[[nodiscard]] ProjectFieldControl ParseControlName(std::string_view Name) noexcept
{
    if (Name == "toggle" || Name == "switch") return ProjectFieldControl::ToggleSwitch;
    if (Name == "choice" || Name == "select") return ProjectFieldControl::ChoiceGrid;
    if (Name == "colour" || Name == "color")  return ProjectFieldControl::ColourSwatch;
    if (Name == "metadata" || Name == "readout") return ProjectFieldControl::MetadataLine;
    if (Name == "action" || Name == "button") return ProjectFieldControl::ActionButton;
    return ProjectFieldControl::ScalarSlider;
}

[[nodiscard]] bool MatchesDomainPattern(std::string_view Pattern, std::string_view Tag) noexcept
{
    if (Pattern.empty() || Tag.empty()) return false;
    if (Pattern.back() == '*')
    {
        const std::string_view Prefix = Pattern.substr(0u, Pattern.size() - 1u);
        return Tag.size() >= Prefix.size() && Tag.substr(0u, Prefix.size()) == Prefix;
    }
    return Pattern == Tag;
}

[[nodiscard]] float EstimateSectionHeight(const ProjectSectionDescriptor& Sec) noexcept
{
    float H = InspectorMetrics::SectionPadTop + 30.0f + InspectorMetrics::SectionPadBottom;
    for (const auto& F : Sec.Fields)
    {
        switch (F.Control)
        {
            case ProjectFieldControl::ScalarSlider: H += 52.0f; break;
            case ProjectFieldControl::ToggleSwitch: H += 40.0f; break;
            case ProjectFieldControl::ChoiceGrid:
            {
                const uint32_t Cols = std::max(1u, F.ChoiceColumns);
                const uint32_t Rows = static_cast<uint32_t>((F.ChoiceOptions.size() + Cols - 1u) / Cols);
                H += 24.0f + static_cast<float>(std::max(1u, Rows)) * 34.0f + 8.0f;
                break;
            }
            case ProjectFieldControl::ColourSwatch: H += 46.0f; break;
            case ProjectFieldControl::MetadataLine: H += 22.0f; break;
            case ProjectFieldControl::ActionButton: H += 38.0f; break;
        }
    }
    return H;
}
} // namespace

void ProjectInspectorHost::RegisterBlueprint(ProjectInspectorBlueprint Blueprint)
{
    if (Blueprint.BlueprintId.empty() || Blueprint.TargetDomainTag.empty()) return;
    for (auto& Existing : Blueprints)
    {
        if (Existing.BlueprintId == Blueprint.BlueprintId)
        {
            Existing = std::move(Blueprint);
            return;
        }
    }
    Blueprints.push_back(std::move(Blueprint));
}

bool ProjectInspectorHost::LoadBlueprintFromTomlText(
    std::string_view TomlText,
    std::string_view VirtualSourcePath,
    std::string*     OutDiagnostic)
{
    try
    {
        const toml::table Root = toml::parse(TomlText, VirtualSourcePath);
        const auto* Header = Root["inspector"].as_table();
        if (!Header)
        {
            if (OutDiagnostic) *OutDiagnostic = "Missing [inspector] header table";
            return false;
        }

        ProjectInspectorBlueprint Bp {};
        Bp.BlueprintId     = (*Header)["id"].value_or(std::string{});
        Bp.TargetDomainTag = (*Header)["target_tag"].value_or(std::string{});
        if (auto Sub = (*Header)["subtitle"].value<std::string>()) Bp.Subtitle = *Sub;
        Bp.OverrideNative  = (*Header)["override_native"].value_or(false);
        Bp.SourceFilePath  = std::string(VirtualSourcePath);

        if (const auto* TagsArr = (*Header)["tags"].as_array())
        {
            for (const auto& Elem : *TagsArr)
            {
                if (auto S = Elem.value<std::string>()) Bp.TagLabels.push_back(*S);
            }
        }

        if (const auto* PagesArr = Root["pages"].as_array())
        {
            for (const auto& PageElem : *PagesArr)
            {
                const auto* PageTbl = PageElem.as_table();
                if (!PageTbl) continue;
                ProjectPageDescriptor Page {};
                Page.Title = (*PageTbl)["title"].value_or(std::string{ "Overview" });

                if (const auto* SecsArr = (*PageTbl)["sections"].as_array())
                {
                    for (const auto& SecElem : *SecsArr)
                    {
                        const auto* SecTbl = SecElem.as_table();
                        if (!SecTbl) continue;
                        ProjectSectionDescriptor Sec {};
                        Sec.Heading      = (*SecTbl)["heading"].value_or(std::string{ "Section" });
                        Sec.RightCaption = (*SecTbl)["caption"].value_or(std::string{});
                        Sec.TooltipText  = (*SecTbl)["tooltip"].value_or(std::string{});

                        if (const auto* FieldsArr = (*SecTbl)["fields"].as_array())
                        {
                            for (const auto& FieldElem : *FieldsArr)
                            {
                                const auto* FieldTbl = FieldElem.as_table();
                                if (!FieldTbl) continue;
                                ProjectFieldDescriptor F {};
                                F.Key   = (*FieldTbl)["key"].value_or(std::string{});
                                F.Label = (*FieldTbl)["label"].value_or(F.Key);
                                if (auto Ctrl = (*FieldTbl)["control"].value<std::string>()) F.Control = ParseControlName(*Ctrl);
                                F.UnitText    = (*FieldTbl)["unit"].value_or(std::string{});
                                F.FormatSpec  = (*FieldTbl)["format"].value_or(std::string{ "%.2f" });
                                F.TooltipText = (*FieldTbl)["tooltip"].value_or(std::string{});
                                F.Minimum     = static_cast<float>((*FieldTbl)["min"].value_or(0.0));
                                F.Maximum     = static_cast<float>((*FieldTbl)["max"].value_or(1.0));
                                if (F.Control == ProjectFieldControl::ToggleSwitch)
                                    F.DefaultToggle = (*FieldTbl)["default"].value_or(false);
                                else if (F.Control == ProjectFieldControl::ChoiceGrid)
                                    F.DefaultChoice = static_cast<uint32_t>(std::max<int64_t>(0, (*FieldTbl)["default"].value_or(int64_t{ 0 })));
                                else if (F.Control == ProjectFieldControl::MetadataLine)
                                    F.DefaultText = (*FieldTbl)["default"].value_or(std::string{});
                                else
                                    F.DefaultScalar = static_cast<float>((*FieldTbl)["default"].value_or(0.0));

                                F.ChoiceColumns = static_cast<uint32_t>(std::max<int64_t>(1, (*FieldTbl)["columns"].value_or(int64_t{ 2 })));
                                if (const auto* Opts = (*FieldTbl)["options"].as_array())
                                {
                                    for (const auto& O : *Opts)
                                    {
                                        if (auto OS = O.value<std::string>()) F.ChoiceOptions.push_back(*OS);
                                    }
                                }
                                if (const auto* Rgb = (*FieldTbl)["default_rgb"].as_array(); Rgb && Rgb->size() >= 3u)
                                {
                                    F.DefaultColour = Mathematics::LinearRGB{
                                        static_cast<float>((*Rgb)[0].value_or(0.8)),
                                        static_cast<float>((*Rgb)[1].value_or(0.8)),
                                        static_cast<float>((*Rgb)[2].value_or(0.8)),
                                    };
                                }
                                if (!F.Key.empty()) Sec.Fields.push_back(std::move(F));
                            }
                        }
                        Page.Sections.push_back(std::move(Sec));
                    }
                }
                Bp.Pages.push_back(std::move(Page));
            }
        }

        if (Bp.BlueprintId.empty() || Bp.TargetDomainTag.empty() || Bp.Pages.empty())
        {
            if (OutDiagnostic) *OutDiagnostic = "Blueprint requires non-empty id, target_tag, and at least one page";
            return false;
        }
        RegisterBlueprint(std::move(Bp));
        return true;
    }
    catch (const toml::parse_error& Ex)
    {
        if (OutDiagnostic) *OutDiagnostic = std::string(Ex.description());
        return false;
    }
    catch (const std::exception& Ex)
    {
        if (OutDiagnostic) *OutDiagnostic = Ex.what();
        return false;
    }
}

uint32_t ProjectInspectorHost::ScanProjectDirectory(
    const std::filesystem::path& ProjectDirectory,
    std::vector<std::string>*    OutDiagnostics)
{
    std::filesystem::path SearchDir = ProjectDirectory / "Inspectors";
    std::error_code Ec {};
    if (!std::filesystem::exists(SearchDir, Ec))
    {
        SearchDir = ProjectDirectory;
    }
    if (!std::filesystem::exists(SearchDir, Ec) || !std::filesystem::is_directory(SearchDir, Ec))
    {
        return 0u;
    }

    uint32_t LoadedCount = 0u;
    for (const auto& DirEntry : std::filesystem::directory_iterator(SearchDir, Ec))
    {
        if (!DirEntry.is_regular_file(Ec)) continue;
        const std::string Name = DirEntry.path().filename().string();
        if (Name.size() <= 15u || Name.substr(Name.size() - 15u) != ".Inspector.toml") continue;

        std::ifstream Stream(DirEntry.path());
        if (!Stream) continue;
        std::ostringstream Buffer;
        Buffer << Stream.rdbuf();
        std::string Diag {};
        if (LoadBlueprintFromTomlText(Buffer.str(), DirEntry.path().string(), &Diag))
        {
            ++LoadedCount;
        }
        else if (OutDiagnostics)
        {
            OutDiagnostics->push_back(DirEntry.path().string() + ": " + Diag);
        }
    }
    return LoadedCount;
}

void ProjectInspectorHost::ClearAll() noexcept
{
    Blueprints.clear();
    Readings.clear();
}

void ProjectInspectorHost::EraseEntryReadings(OutlinerSlotToken Token) noexcept
{
    Readings.erase(Token.Identifier);
}

const ProjectInspectorBlueprint* ProjectInspectorHost::ResolveBlueprint(const OutlinerRecordEntry& Entry) const noexcept
{
    for (const auto& Bp : Blueprints)
    {
        if (MatchesDomainPattern(Bp.TargetDomainTag, Entry.DomainTag))
        {
            return &Bp;
        }
    }
    return nullptr;
}

ProjectInstanceReadings& ProjectInspectorHost::AccessReadings(
    OutlinerSlotToken Token, const ProjectInspectorBlueprint& Blueprint)
{
    auto [It, Inserted] = Readings.try_emplace(Token.Identifier);
    for (const auto& Page : Blueprint.Pages)
    {
        for (const auto& Sec : Page.Sections)
        {
            for (const auto& F : Sec.Fields)
            {
                auto [FIt, FIns] = It->second.Fields.try_emplace(F.Key);
                if (FIns)
                {
                    FIt->second.ScalarReading = F.DefaultScalar;
                    FIt->second.ToggleReading = F.DefaultToggle;
                    FIt->second.ChoiceReading = F.DefaultChoice;
                    FIt->second.ColourReading = F.DefaultColour;
                    FIt->second.TextReading   = F.DefaultText;
                }
            }
        }
    }
    return It->second;
}

float ProjectInspectorHost::Construct(
    PixelSpace&                      Surface,
    float                            LeftX,
    float                            TopY,
    float                            InnerWidth,
    OutlinerRecordEntry&             Entry,
    const ProjectInspectorBlueprint& Blueprint,
    const PaneInputReading&          Input,
    bool&                            OutMutated,
    bool&                            OutPageSwitched)
{
    if (Blueprint.Pages.empty()) return TopY;
    if (ActivePage >= Blueprint.Pages.size()) ActivePage = 0u;

    std::vector<const char*> PageTitles {};
    PageTitles.reserve(Blueprint.Pages.size());
    for (const auto& P : Blueprint.Pages) PageTitles.push_back(P.Title.c_str());

    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, PageTitles, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    auto& InstanceData = AccessReadings(Entry.Token, Blueprint);
    const auto& Page   = Blueprint.Pages[ActivePage];

    uint32_t ControlSeq = 9200u + ActivePage * 100u;
    for (const auto& Sec : Page.Sections)
    {
        const float SecH = EstimateSectionHeight(Sec);
        const Rectangle2D S { LeftX, CursorY, InnerWidth, SecH };
        const char* SurfaceCaption = Sec.TooltipText.empty() ? Sec.RightCaption.c_str() : "";
        InspectorPrimitives::ConstructSectionSurface(Surface, S, Sec.Heading.c_str(), SurfaceCaption);

        if (!Sec.TooltipText.empty())
        {
            if (!Sec.RightCaption.empty())
            {
                const float CapW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, Sec.RightCaption);
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Regular,
                    { LeftX + InnerWidth - PadX - 22.0f - CapW, CursorY + InspectorMetrics::SectionPadTop + 2.0f },
                    10.5f, InspectorMetrics::MutedInk, Sec.RightCaption);
            }
            InspectorPrimitives::ConstructTooltipTrigger(
                Surface, { LeftX + InnerWidth - PadX - 8.0f, CursorY + InspectorMetrics::SectionPadTop + 8.0f },
                Sec.Heading, Sec.TooltipText, Input);
        }

        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        for (const auto& F : Sec.Fields)
        {
            auto& Reading = InstanceData.Fields[F.Key];
            const uint32_t Cid = ControlSeq++;
            const float TipOffset = F.TooltipText.empty() ? 0.0f : 20.0f;
            if (!F.TooltipText.empty())
            {
                InspectorPrimitives::ConstructTooltipTrigger(
                    Surface, { LeftX + PadX + BodyW - 8.0f, Y + 8.0f }, F.Label, F.TooltipText, Input);
            }

            switch (F.Control)
            {
                case ProjectFieldControl::ScalarSlider:
                    Y = InspectorPrimitives::ConstructScalarFieldRow(
                        Surface, LeftX + PadX, Y, BodyW - TipOffset, Cid,
                        F.Label.c_str(), F.UnitText.c_str(), F.FormatSpec.c_str(),
                        F.Minimum, F.Maximum, Reading.ScalarReading, ActiveDragId, Input, OutMutated, Entry.Locked);
                    break;
                case ProjectFieldControl::ToggleSwitch:
                    Y = InspectorPrimitives::ConstructToggleFieldRow(
                        Surface, LeftX + PadX, Y, BodyW - TipOffset,
                        F.Label.c_str(), Reading.ToggleReading, Input, OutMutated);
                    break;
                case ProjectFieldControl::ChoiceGrid:
                {
                    std::vector<const char*> Opts {};
                    Opts.reserve(F.ChoiceOptions.size());
                    for (const auto& O : F.ChoiceOptions) Opts.push_back(O.c_str());
                    int32_t Pick = -1;
                    Y = InspectorPrimitives::ConstructChoiceRow(
                        Surface, LeftX + PadX, Y, BodyW - TipOffset,
                        F.Label.c_str(), Opts, Reading.ChoiceReading, F.ChoiceColumns, Input, Pick);
                    if (Pick >= 0 && !Entry.Locked) { Reading.ChoiceReading = static_cast<uint32_t>(Pick); OutMutated = true; }
                    break;
                }
                case ProjectFieldControl::ColourSwatch:
                    Y = InspectorPrimitives::ConstructColourFieldRow(
                        Surface, LeftX + PadX, Y, BodyW - TipOffset, Cid,
                        F.Label.c_str(), Reading.ColourReading, ActiveDragId, Input, OutMutated, Entry.Locked);
                    break;
                case ProjectFieldControl::MetadataLine:
                    Y = InspectorPrimitives::ConstructMetadataRow(
                        Surface, LeftX + PadX, Y, BodyW - TipOffset,
                        F.Label.c_str(), Reading.TextReading.c_str());
                    break;
                case ProjectFieldControl::ActionButton:
                    if (InspectorPrimitives::ConstructSectionActionButton(Surface, LeftX + PadX, Y + 2.0f, BodyW, F.Label.c_str(), Input, true))
                    {
                        ++Reading.TriggerCount;
                        OutMutated = true;
                    }
                    Y += 38.0f;
                    break;
            }
        }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    if (ActivePage == 0u)
    {
        if (!Blueprint.TagLabels.empty())
        {
            std::vector<const char*> Tags {};
            Tags.reserve(Blueprint.TagLabels.size());
            for (const auto& T : Blueprint.TagLabels) Tags.push_back(T.c_str());
            CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        }
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }

    return CursorY;
}

} // namespace Frontier
