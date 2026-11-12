/*============================================================================================================================================
                                                         WORKSPACEPANESVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation and CPU visual snapshot suite for generic Outliner/LayerStack, Showcase Folder/Entry Inspector, and Viewport panes

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <filesystem>
#include <vector>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      TYPE ALIASES
//------------------------------------------------------------------------------------------------------------------------

using Frontier::ControlCentrePanel;
using Frontier::DropRelativePlacement;
using Frontier::InspectorPanel;
using Frontier::OutlinerCategoryFilter;
using Frontier::OutlinerPanel;
using Frontier::OutlinerRecordEntry;
using Frontier::ConstructSlotCategory;
using Frontier::OutlinerSlotToken;
using Frontier::OutlinerTitleVariant;
using Frontier::OutlinerVisibilityFilter;
using Frontier::OutlinerVisibleRow;
using Frontier::Point2D;
using Frontier::ThemePreset;
using Frontier::ThemeRepository;
using Frontier::TypographyRepository;
using Frontier::ViewportExecutionMode;
using Frontier::ViewportPanel;
using Frontier::ViewportProjectionPreset;
using Frontier::Components::ConfigureImGuiWorkspaceTheme;
using Frontier::Components::DockPlacementDirection;
using Frontier::Components::HeadlessWorkspaceDockContainer;
using Frontier::Components::HostWindowToken;
using Frontier::Components::PaneDirectiveMask;
using Frontier::Components::RgbaColor8;
using Frontier::Components::WorkspaceDockPaneDescriptor;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;
using Frontier::Windowing::WindowConfigurationDescriptor;

//------------------------------------------------------------------------------------------------------------------------
//                                                 VALIDATION ASSERTIONS
//------------------------------------------------------------------------------------------------------------------------

bool VerifyAssertion(bool Condition, const char* Description) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[WorkspacePanesValidation] FAILED: %s\n", Description);
        return false;
    }
    return true;
}

bool ValidateOutlinerInspectorAndViewportSemantics()
{
    OutlinerPanel  Outliner{};
    InspectorPanel Inspector{};
    ViewportPanel  Viewport{};

    Inspector.AttachOutliner(&Outliner);
    Viewport.AttachOutliner(&Outliner);

    // 1. Default empty condition (zero pre-populated scene entries)
    if (!VerifyAssertion(
            Outliner.QueryTotalCount() == 0u &&
            Outliner.QueryVisibleCount() == 0u &&
            Outliner.QueryHiddenCount() == 0u &&
            Outliner.QuerySelectedTokens().empty(),
            "Outliner starts completely empty with zero pre-populated entries"))
    {
        return false;
    }

    if (!VerifyAssertion(
            std::string_view(Outliner.QueryHeadingText()) == "Outliner",
            "Default heading text is Outliner"))
    {
        return false;
    }

    Outliner.ConfigureTitleVariant(OutlinerTitleVariant::LayerStack);
    if (!VerifyAssertion(
            std::string_view(Outliner.QueryHeadingText()) == "LayerStack",
            "LayerStack title variant switches heading to LayerStack"))
    {
        return false;
    }
    Outliner.ConfigureTitleVariant(OutlinerTitleVariant::Outliner);

    // 2. Generic construct presets + Folder creation + Tree nesting + Cycle prevention
    const auto Templates = Outliner.QueryConstructTemplates();
    if (!VerifyAssertion(
            Templates.size() >= 3u &&
            Templates[0].DropdownLabel == "Add Item 1" &&
            Templates[1].DropdownLabel == "Add Item 2" &&
            Templates[2].DropdownLabel == "Add Item 3",
            "Default generic construct presets provide Add Item 1, Add Item 2, Add Item 3..."))
    {
        return false;
    }

    const OutlinerSlotToken ShowcaseFolder = Outliner.ConstructFolder("Showcase");
    const OutlinerSlotToken FirstSlot      = Outliner.ConstructFromTemplate(0u, ShowcaseFolder);
    const OutlinerSlotToken SecondSlot     = Outliner.ConstructFromTemplate(1u, ShowcaseFolder);
    const OutlinerSlotToken NestedFolder   = Outliner.ConstructFolder("Subgroup", ShowcaseFolder);
    const OutlinerSlotToken DeepSlot       = Outliner.ConstructFromTemplate(2u, NestedFolder);

    if (!VerifyAssertion(
            Outliner.CountDirectNested(ShowcaseFolder) == 3u &&
            Outliner.CountAllNested(ShowcaseFolder) == 4u &&
            Outliner.CountNestedFolders(ShowcaseFolder) == 1u,
            "Showcase folder direct, nested, and subfolder counts match constructed tree"))
    {
        return false;
    }

    if (!VerifyAssertion(
            !Outliner.AttachToEnclosure(ShowcaseFolder, DeepSlot),
            "AttachToEnclosure rejects cyclic self/nested enclosure"))
    {
        return false;
    }

    // 3. Order repositioning (BeforeTarget / AfterTarget / InsideTarget)
    if (!VerifyAssertion(
            Outliner.RepositionRelativeToTarget(SecondSlot, FirstSlot, DropRelativePlacement::BeforeTarget),
            "RepositionRelativeToTarget moves SecondSlot before FirstSlot"))
    {
        return false;
    }

    std::vector<OutlinerVisibleRow> Rows{};
    Outliner.BuildVisibleRows(Rows);
    if (!VerifyAssertion(
            Rows.size() == 5u &&
            Rows[0].Token == ShowcaseFolder &&
            Rows[1].Token == SecondSlot &&
            Rows[2].Token == FirstSlot,
            "Visible tree row order reflects BeforeTarget repositioning"))
    {
        return false;
    }

    // 4. Shift multi-select & Ctrl toggle select
    Outliner.SelectSingle(SecondSlot);
    Outliner.SelectWithModifiers(NestedFolder, true, false);
    if (!VerifyAssertion(
            Outliner.QuerySelectedTokens().size() == 3u &&
            Outliner.QuerySlotSelected(SecondSlot) &&
            Outliner.QuerySlotSelected(FirstSlot) &&
            Outliner.QuerySlotSelected(NestedFolder),
            "Shift multi-select captures contiguous visible tree range"))
    {
        return false;
    }

    Outliner.SelectWithModifiers(FirstSlot, false, true);
    if (!VerifyAssertion(
            Outliner.QuerySelectedTokens().size() == 2u &&
            !Outliner.QuerySlotSelected(FirstSlot),
            "Ctrl click toggles individual slot membership in multi-selection"))
    {
        return false;
    }

    // 5. Search & Visibility filtering
    if (OutlinerRecordEntry* DeepEntry = Outliner.AccessEntry(DeepSlot))
    {
        DeepEntry->VisibilityEnabled = false;
    }
    if (!VerifyAssertion(
            Outliner.QueryVisibleCount() == 4u && Outliner.QueryHiddenCount() == 1u,
            "Visible and Hidden counters update when slot visibility changes"))
    {
        return false;
    }

    Outliner.ConfigureVisibilityFilter(OutlinerVisibilityFilter::HiddenOnly);
    Outliner.BuildVisibleRows(Rows);
    if (!VerifyAssertion(
            Rows.size() == 3u && Rows.back().Token == DeepSlot,
            "HiddenOnly filter retains enclosing folder chain down to hidden slot"))
    {
        return false;
    }
    Outliner.ConfigureVisibilityFilter(OutlinerVisibilityFilter::All);
    if (OutlinerRecordEntry* DeepEntry = Outliner.AccessEntry(DeepSlot))
    {
        DeepEntry->VisibilityEnabled = true;
    }

    // 6. Inspector Add Notes persistence
    Outliner.SelectSingle(FirstSlot);
    Inspector.AssignActiveNotes("Primary validation entry with persisted review notes.");
    const OutlinerRecordEntry* FirstEntry = Outliner.FindEntry(FirstSlot);
    if (!VerifyAssertion(
            FirstEntry != nullptr &&
            FirstEntry->AuthorNotes == "Primary validation entry with persisted review notes." &&
            Inspector.QueryNotesBoxOpen(),
            "Inspector Add Notes persists AuthorNotes on selected OutlinerRecordEntry"))
    {
        return false;
    }

    // 7. Viewport Edit / Simulate / Play-Pause, Split Viewport, Diagnostics & Settings
    if (!VerifyAssertion(
            Viewport.QueryExecutionMode() == ViewportExecutionMode::Edit && !Viewport.QueryPlayPaused(),
            "Viewport defaults to Edit mode"))
    {
        return false;
    }

    Viewport.TogglePlayPause();
    if (!VerifyAssertion(
            Viewport.QueryExecutionMode() == ViewportExecutionMode::Play && !Viewport.QueryPlayPaused(),
            "First Play click enters active Play mode"))
    {
        return false;
    }

    Viewport.TogglePlayPause();
    if (!VerifyAssertion(
            Viewport.QueryExecutionMode() == ViewportExecutionMode::Play && Viewport.QueryPlayPaused(),
            "Second Play/Pause click pauses active Play session"))
    {
        return false;
    }

    Viewport.ConfigureSplitViewport(true);
    Viewport.ConfigureDiagnosticsVisible(true);
    if (!VerifyAssertion(
            Viewport.QuerySplitViewportEnabled() && Viewport.QueryDiagnosticsVisible(),
            "Split Viewport and Diagnostics toggles activate cleanly"))
    {
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           VISUAL SNAPSHOT EXHIBIT GENERATOR
//------------------------------------------------------------------------------------------------------------------------

bool RenderWorkspaceSnapshot(
    ImGuiIO&                        Io,
    HeadlessWorkspaceDockContainer& Container,
    ControlCentrePanel&             ControlCentre,
    const std::filesystem::path&    PngDestination)
{
    for (int StepIndex = 0; StepIndex < 3; ++StepIndex)
    {
        Io.DisplaySize = ImVec2(1600.0f, 980.0f);
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        Container.PresentWindowImGuiPanes(HostWindowToken{ 0u });
        ControlCentre.Render({ 0.0f, 0.0f, 1600.0f, 980.0f }, 1.0 / 60.0, 60.0f);
        ImGui::Render();
    }

    SoftwareRasterCanvas Canvas(1600, 980, RgbaColor8{ 14, 14, 14, 255 });
    Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
    return VerifyAssertion(Canvas.WritePngFile(PngDestination), PngDestination.filename().string().c_str());
}

bool GenerateWorkspacePaneExhibits(const std::filesystem::path& OutputDirectory)
{
    IMGUI_CHECKVERSION();
    ImGuiContext* GuiSession = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiSession);

    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    const auto           RepositoryRoot = ResolveRepositoryRootPath();
    TypographyRepository Typefaces{};
    if (!VerifyAssertion(
            Typefaces.LoadRepository(RepositoryRoot / "EngineContent/FontsArchives") == 5u,
            "Load FontRepository.FontSpace"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    TypographyRepository::AssignCurrent(&Typefaces);

    int32_t DmSansGroup = Typefaces.ResolveGroupIndex("DM Sans");
    if (DmSansGroup < 0)
    {
        DmSansGroup = 0;
    }
    if (ImFont* TabFont = Typefaces.QueryTabStripFont())
    {
        Io.FontDefault = TabFont;
    }
    else if (ImFont* DefaultFace = Typefaces.QueryFont(static_cast<uint32_t>(DmSansGroup), Frontier::FontWeightClass::Regular))
    {
        Io.FontDefault = DefaultFace;
    }

    unsigned char* FontPixels = nullptr;
    int            AtlasW     = 0;
    int            AtlasH     = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &AtlasW, &AtlasH);

    auto CaptureExhibitSnapshot = [&](
                               const char*     LeftTabTitle,
                               OutlinerPanel&  Outliner,
                               ViewportPanel&  Viewport,
                               InspectorPanel& Inspector,
                               const char*     FileName) -> bool
    {
        HeadlessWorkspaceDockContainer Container{};
        const auto                     WinDesc = WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier", 1600, 980);
        if (!Container.Initialize(WinDesc))
        {
            return false;
        }

        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            LeftTabTitle,
            DockPlacementDirection::SplitLeft,
            0.21f,
            PaneDirectiveMask::StandardPaneDirectives,
            [&]() { Outliner.PresentInImGuiPane(1.0 / 60.0, 1.0f); } });

        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Viewport",
            DockPlacementDirection::CenterTabGroup,
            0.55f,
            PaneDirectiveMask::CentralViewportDirectives,
            [&]() { Viewport.PresentInImGuiPane(1.0 / 60.0, 1.0f); } });

        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Inspector",
            DockPlacementDirection::SplitRight,
            0.26f,
            PaneDirectiveMask::StandardPaneDirectives,
            [&]() { Inspector.PresentInImGuiPane(1.0 / 60.0, 1.0f); } });

        ThemeRepository Theme{};
        Theme.ConfigureTheme(ThemePreset::Dark);
        ConfigureImGuiWorkspaceTheme(Container.ActiveTabMetrics(), Theme);

        ControlCentrePanel ControlCentre{};
        ControlCentre.AttachTypefaces(&Typefaces);
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        ControlCentre.ApplyDraftAppearanceImmediate();
        ControlCentre.AccessNotificationStream().ClearPending();

        const bool Ok = RenderWorkspaceSnapshot(Io, Container, ControlCentre, OutputDirectory / FileName);
        Container.Finalize();
        return Ok;
    };

    // Exhibit 01: Default Empty Workspace (Outliner + Viewport + Inspector with zero pre-populated entries)
    {
        OutlinerPanel  Outliner{};
        ViewportPanel  Viewport{};
        InspectorPanel Inspector{};
        Viewport.AttachOutliner(&Outliner);
        Inspector.AttachOutliner(&Outliner);

        if (!CaptureExhibitSnapshot("Outliner", Outliner, Viewport, Inspector, "01-EmptyDefaultWorkspace.png"))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }
    }

    // Exhibit 02: LayerStack variant with "+" Construct Dropdown open, collapsed "+ Add notes" button, & Viewport Construct Dialog
    {
        OutlinerPanel  LayerStack{};
        ViewportPanel  Viewport{};
        InspectorPanel Inspector{};
        LayerStack.ConfigureTitleVariant(OutlinerTitleVariant::LayerStack);
        const OutlinerSlotToken InitialLayer = LayerStack.ConstructFromTemplate(0u);
        LayerStack.SelectSingle(InitialLayer);
        LayerStack.OpenConstructDropdown();
        Viewport.AttachOutliner(&LayerStack);
        Viewport.ConfigureConstructSheetOpen(true);
        Inspector.AttachOutliner(&LayerStack);

        if (!CaptureExhibitSnapshot("LayerStack", LayerStack, Viewport, Inspector, "02-LayerStackAndConstructMenus.png"))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }
    }

    // Exhibit 03: Showcase Folder Inspector + Nested Outliner Tree + Filter Chips + Add Notes
    {
        OutlinerPanel  Outliner{};
        ViewportPanel  Viewport{};
        InspectorPanel Inspector{};
        Viewport.AttachOutliner(&Outliner);
        Inspector.AttachOutliner(&Outliner);

        const OutlinerSlotToken ShowcaseFolder = Outliner.ConstructFolder("Showcase");
        (void)Outliner.ConstructFromTemplate(0u, ShowcaseFolder);
        (void)Outliner.ConstructFromTemplate(1u, ShowcaseFolder);
        const OutlinerSlotToken SubFolder = Outliner.ConstructFolder("Subgroup", ShowcaseFolder);
        (void)Outliner.ConstructFromTemplate(2u, SubFolder);
        const OutlinerSlotToken HiddenSlot = Outliner.ConstructFromTemplate(3u, ShowcaseFolder);
        if (OutlinerRecordEntry* Entry = Outliner.AccessEntry(HiddenSlot))
        {
            Entry->VisibilityEnabled = false;
        }

        const OutlinerSlotToken SecondaryFolder = Outliner.ConstructFolder("Secondary");
        (void)Outliner.ConstructFromTemplate(0u, SecondaryFolder);

        Outliner.ConfigureFilterStripOpen(true);
        Outliner.SelectSingle(ShowcaseFolder);
        Inspector.AssignActiveNotes("Primary Showcase collection organizing generic workspace items and nested folders.");

        if (!CaptureExhibitSnapshot("Outliner", Outliner, Viewport, Inspector, "03-ShowcaseFolderInspectorAndTree.png"))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }
    }

    // Exhibit 04: Entry Inspector (Workspace Header Sizing + Notes + Tiles) & Split Viewport + Diagnostics + Play/Pause
    {
        OutlinerPanel  Outliner{};
        ViewportPanel  Viewport{};
        InspectorPanel Inspector{};
        Viewport.AttachOutliner(&Outliner);
        Inspector.AttachOutliner(&Outliner);

        const OutlinerSlotToken Folder    = Outliner.ConstructFolder("Showcase");
        const OutlinerSlotToken PrimaryIt = Outliner.ConstructFromTemplate(0u, Folder);
        const OutlinerSlotToken SecondIt  = Outliner.ConstructFromTemplate(1u, Folder);
        (void)Outliner.ConstructFromTemplate(2u, Folder);

        if (OutlinerRecordEntry* LockedEntry = Outliner.AccessEntry(SecondIt))
        {
            LockedEntry->Locked = true;
        }

        if (OutlinerRecordEntry* SlotEntry = Outliner.AccessEntry(PrimaryIt))
        {
            SlotEntry->PositionX = 12.5f;
            SlotEntry->PositionY = -4.0f;
            SlotEntry->PositionZ = 3.0f;
            SlotEntry->RotationY = 45.0f;
            SlotEntry->ScaleX    = 1.25f;
            SlotEntry->ScaleY    = 1.25f;
            SlotEntry->ScaleZ    = 1.25f;
            SlotEntry->Subtitle  = "Position 12.5, -4.0, 3.0 m";
        }

        Outliner.SelectSingle(PrimaryIt);
        Inspector.AssignActiveNotes("Reviewed transform placement and shadow caster settings.");

        Viewport.ConfigureExecutionMode(ViewportExecutionMode::Play, false);
        Viewport.ConfigureSplitViewport(true);
        Viewport.ConfigureSecondaryProjection(ViewportProjectionPreset::Top);
        Viewport.ConfigureDiagnosticsVisible(true);

        if (!CaptureExhibitSnapshot("Outliner", Outliner, Viewport, Inspector, "04-EntryInspectorSplitAndDiagnostics.png"))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }
    }

    // Exhibit 05: Shift Multi-Select + Right-Click Context Sheet + Outliner Settings & Viewport Settings Sheet
    {
        OutlinerPanel  Outliner{};
        ViewportPanel  Viewport{};
        InspectorPanel Inspector{};
        Viewport.AttachOutliner(&Outliner);
        Inspector.AttachOutliner(&Outliner);

        const OutlinerSlotToken Folder = Outliner.ConstructFolder("Showcase");
        const OutlinerSlotToken First  = Outliner.ConstructFromTemplate(0u, Folder);
        (void)Outliner.ConstructFromTemplate(1u, Folder);
        const OutlinerSlotToken Third  = Outliner.ConstructFromTemplate(2u, Folder);
        (void)Outliner.ConstructFromTemplate(3u, Folder);

        Outliner.SelectSingle(First);
        Outliner.SelectWithModifiers(Third, true, false);
        Outliner.OpenDirectiveSheetForSlot(Third, Point2D{ 92.0f, 340.0f });

        Viewport.ConfigureExecutionMode(ViewportExecutionMode::Simulate, false);
        Viewport.ConfigureSettingsSheetOpen(true);

        if (!CaptureExhibitSnapshot("Outliner", Outliner, Viewport, Inspector, "05-MultiSelectContextAndSettings.png"))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }
    }

    // Exhibit 06: Outliner Header Settings ("Import...") Dropdown + Scrolled Tree + Folder Inspector Bottom Cards
    {
        OutlinerPanel  Outliner{};
        ViewportPanel  Viewport{};
        InspectorPanel Inspector{};
        Viewport.AttachOutliner(&Outliner);
        Inspector.AttachOutliner(&Outliner);

        const OutlinerSlotToken ShowcaseFolder = Outliner.ConstructFolder("Showcase");
        for (uint32_t Index = 0u; Index < 26u; ++Index)
        {
            (void)Outliner.ConstructFromTemplate(Index % 4u, ShowcaseFolder);
        }
        Outliner.ConfigureScrollOffset(220.0f);
        Outliner.SelectSingle(ShowcaseFolder);
        Outliner.OpenHeaderSettingsDropdown();

        Inspector.AssignActiveNotes("Collection notes confirming bottom Folder tint swatches and Browse contents pagination.");
        Inspector.ConfigureScrollOffset(360.0f);
        Viewport.ConfigureExecutionMode(ViewportExecutionMode::Play, true);
        Viewport.ConfigureProjectionDropdownOpen(true);

        if (!CaptureExhibitSnapshot("Outliner", Outliner, Viewport, Inspector, "06-HeaderSettingsAndScrolledTree.png"))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }
    }

    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(GuiSession);
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    if (!ValidateOutlinerInspectorAndViewportSemantics())
    {
        return 1;
    }

    const std::filesystem::path OutputDirectory = ResolveRepositoryRootPath() / "Exhibits" / "Images" / "WorkspacePanes";
    if (!GenerateWorkspacePaneExhibits(OutputDirectory))
    {
        return 1;
    }

    std::printf("[WorkspacePanesValidation] All Outliner/LayerStack, Inspector, and Viewport validations passed.\n");
    return 0;
}
