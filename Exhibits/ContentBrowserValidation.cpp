/*============================================================================================================================================
                                                         CONTENTBROWSERVALIDATION.CPP
============================================================================================================================================*/
// 🧩 CTest validation and 8-image CPU visual snapshot suite for ContentBrowser, ThumbnailSheet, SSD/HDD streaming, UV masks & .ColourSwatch

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ContentBrowser/ColourSwatchBrowserView.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/ContentBrowser/ThumbnailSheetRepository.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Lifecycle/EditorLifecycle.h"
#include "Engine/Threading/Threads/Streaming/AssetStreamThread.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

using Frontier::ColorRgba;
using Frontier::ColourSwatchBrowserView;
using Frontier::ColourSwatchEntry;
using Frontier::ColourFilterGroup;
using Frontier::ColourRepositoryLayout;
using Frontier::ColourSlotRegistration;
using Frontier::ColourSortOrder;
using Frontier::ContentBrowserPanel;
using Frontier::ContentBrowserPointerInput;
using Frontier::ContentCategory;
using Frontier::ContentIndexEntry64;
using Frontier::ContentOriginFilter;
using Frontier::ContentShelfExtent;
using Frontier::ImportDestinationMode;
using Frontier::ImportIntakeConfiguration;
using Frontier::ImportIntakeResult;
using Frontier::PixelSpace;
using Frontier::Point2D;
using Frontier::Rectangle2D;
using Frontier::ThemeRepository;
using Frontier::ThumbnailSheetRepository;
using Frontier::TypographyRepository;
using Frontier::WorkspaceCategory;
using Frontier::Components::ConfigureImGuiWorkspaceTheme;
using Frontier::Components::DockPlacementDirection;
using Frontier::Components::HeadlessWorkspaceDockContainer;
using Frontier::Components::HostWindowToken;
using Frontier::Components::PaneDirectiveMask;
using Frontier::Components::RgbaColor8;
using Frontier::Components::WorkspaceDockPaneDescriptor;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;
using Frontier::Threading::AssetStreamThread;
using Frontier::Threading::StorageHardwareTier;
using Frontier::Windowing::WindowConfigurationDescriptor;

//------------------------------------------------------------------------------------------------------------------------
//                                                 VALIDATION ASSERTIONS
//------------------------------------------------------------------------------------------------------------------------

bool VerifyAssertion(bool Condition, const char* Description) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ContentBrowserValidation] FAILED: %s\n", Description);
        return false;
    }
    return true;
}

bool ValidateColourSwatchTomlPersistence()
{
    ColourSwatchBrowserView Codex{};
    if (!VerifyAssertion(Codex.QueryEntries().size() >= 38u, "Built-in ColourSwatchBrowserView contains >= 38 named colours"))
    {
        return false;
    }

    const std::size_t FigIdx = Codex.FindEntryByName("Fig Purple");
    const std::size_t PinIdx = Codex.FindEntryByName("Pineapple Yellow");
    const std::size_t BlzIdx = Codex.FindEntryByName("August Blaze");
    const std::size_t TanIdx = Codex.FindEntryByName("August Tangerine");
    const std::size_t MarIdx = Codex.FindEntryByName("August Marigold");
    const std::size_t LemIdx = Codex.FindEntryByName("August Lemon");

    if (!VerifyAssertion(FigIdx < Codex.QueryEntries().size() && PinIdx < Codex.QueryEntries().size(), "Built-in Fig Purple & Pineapple Yellow exist"))
    {
        return false;
    }

    const ColourSwatchEntry* FigEntry = Codex.QueryEntryAt(FigIdx);
    const ColourSwatchEntry* PinEntry = Codex.QueryEntryAt(PinIdx);
    const ColourSwatchEntry* BlzEntry = Codex.QueryEntryAt(BlzIdx);
    const ColourSwatchEntry* TanEntry = Codex.QueryEntryAt(TanIdx);
    const ColourSwatchEntry* MarEntry = Codex.QueryEntryAt(MarIdx);
    const ColourSwatchEntry* LemEntry = Codex.QueryEntryAt(LemIdx);

    if (!VerifyAssertion(FigEntry->HexUpper == "#3B1F3A" && !ColourSwatchBrowserView::QueryContrastInk(FigEntry->FillColour).DarkInk, "Fig Purple (#3B1F3A) selects white contrast ink") ||
        !VerifyAssertion(PinEntry->HexUpper == "#F6B800" && ColourSwatchBrowserView::QueryContrastInk(PinEntry->FillColour).DarkInk, "Pineapple Yellow (#F6B800) selects black contrast ink") ||
        !VerifyAssertion(
            BlzEntry != nullptr && BlzEntry->HexCompact == "FF7B00" && !ColourSwatchBrowserView::QueryContrastInk(BlzEntry->FillColour).DarkInk &&
            TanEntry != nullptr && TanEntry->HexCompact == "FF8800" && !ColourSwatchBrowserView::QueryContrastInk(TanEntry->FillColour).DarkInk &&
            MarEntry != nullptr && MarEntry->HexCompact == "FF9500" && ColourSwatchBrowserView::QueryContrastInk(MarEntry->FillColour).DarkInk &&
            LemEntry != nullptr && LemEntry->HexCompact == "FFEA00" && ColourSwatchBrowserView::QueryContrastInk(LemEntry->FillColour).DarkInk,
            "August Beige & Brown spectrum matches white ink on FF7B00..FF8800 and black ink on FF9500..FFEA00"))
    {
        return false;
    }

    const auto AugustIndices = Codex.QueryFilteredIndices(ColourFilterGroup::WarmAmberNeutrals, ColourSortOrder::Spectrum);
    if (!VerifyAssertion(AugustIndices.size() == 10u, "August Beige & Brown Colour Filter yields 10 spectrum bands"))
    {
        return false;
    }

    // Singular .ColourSwatch round-trip (case-insensitive load, canonical PascalCase .ColourSwatch write)
    const std::size_t CustomIdx = Codex.AddProjectColour("Studio Ultramarine", "#2454FF", "Blues");
    const std::filesystem::path TempSwatch = std::filesystem::temp_directory_path() / "StudioUltramarine.ColourSwatch";
    std::error_code             RemoveErr{};
    std::filesystem::remove(TempSwatch, RemoveErr);

    std::string Diagnostic{};
    if (!VerifyAssertion(Codex.SaveSingleSwatchFile(CustomIdx, TempSwatch, Diagnostic), "Save singular .ColourSwatch file") ||
        !VerifyAssertion(ColourSwatchBrowserView::MatchesSwatchSuffix("test.colourswatch"), "Case-insensitive .colourswatch extension match"))
    {
        return false;
    }

    ColourSwatchBrowserView SwatchReceiver{};
    std::size_t LoadedSwatchIdx = 0u;
    if (!VerifyAssertion(SwatchReceiver.LoadSingleSwatchFile(TempSwatch, Diagnostic, &LoadedSwatchIdx), "Load singular .ColourSwatch file") ||
        !VerifyAssertion(SwatchReceiver.QueryEntryAt(LoadedSwatchIdx)->HexUpper == "#2454FF", "Singular .ColourSwatch round-trips #2454FF"))
    {
        return false;
    }
    std::filesystem::remove(TempSwatch, RemoveErr);

    // Multi-swatch .toml round-trip
    const std::filesystem::path TempToml = std::filesystem::temp_directory_path() / "FrontierColourCodexValidation.toml";
    std::filesystem::remove(TempToml, RemoveErr);
    if (!VerifyAssertion(Codex.SaveToTomlFile(TempToml, Diagnostic), "Export ColourSwatchBrowserView .toml file"))
    {
        return false;
    }

    ColourSwatchBrowserView ReloadedCodex{};
    if (!VerifyAssertion(ReloadedCodex.LoadFromTomlFile(TempToml, Diagnostic), "Import ColourSwatchBrowserView .toml file") ||
        !VerifyAssertion(ReloadedCodex.FindEntryByName("Studio Ultramarine") < ReloadedCodex.QueryEntries().size(), "Reloaded .toml preserves Studio Ultramarine"))
    {
        return false;
    }
    std::filesystem::remove(TempToml, RemoveErr);
    return true;
}

bool ValidateDualPathStreamingIsolation()
{
    if (!VerifyAssertion(sizeof(ContentIndexEntry64) == 64u, "ContentIndexEntry64 is exactly 64 bytes"))
    {
        return false;
    }

    const std::filesystem::path SandboxRoot = std::filesystem::temp_directory_path() / "FrontierContentBrowserTest";
    std::error_code             IoErr{};
    std::filesystem::remove_all(SandboxRoot, IoErr);
    std::filesystem::create_directories(SandboxRoot, IoErr);

    const std::filesystem::path IndexFile = SandboxRoot / "ContentIndex.Space";
    const std::filesystem::path SheetFile = SandboxRoot / "ThumbnailSheet.ImageSpace";

    // 1. Bake & persist 64B index + pre-baked 1024x1024 ThumbnailSheet container
    {
        ThumbnailSheetRepository SourceRepo{};
        std::string              Diagnostic{};
        if (!VerifyAssertion(SourceRepo.PersistBinarySheetIndex(IndexFile, SheetFile, Diagnostic), "Save ContentIndex.Space + ThumbnailSheet.ImageSpace"))
        {
            return false;
        }
    }

    // 2. Test HDD RotationalSequentialIo path: reverse-queued tiles coalesce into 2 MiB sweep with 0 backward seeks
    {
        ThumbnailSheetRepository HddRepo{};
        std::string              Diagnostic{};
        if (!VerifyAssertion(HddRepo.LoadBinaryIndexHeaderOnly(IndexFile, SheetFile, Diagnostic), "Fast header-only load of 64B ContentIndex.Space") ||
            !VerifyAssertion(!HddRepo.QueryEntryAt(0u)->TileResident, "Header-only load leaves tiles non-resident prior to streaming"))
        {
            return false;
        }

        AssetStreamThread HddStreamer{};
        (void)HddStreamer.Initialize(SandboxRoot, StorageHardwareTier::RotationalSequentialIo, nullptr);

        // Request indices in scrambled reverse order to prove C-SCAN elevator sort + 2 MiB coalescing
        std::vector<std::size_t> ScrambledIndices = { 19u, 4u, 15u, 0u, 11u, 2u, 22u, 7u };
        HddRepo.RequestVisibleTileStreaming(ScrambledIndices, HddStreamer);

        const auto HddMetrics = HddStreamer.InspectReading();
        HddStreamer.Finalize();

        if (!VerifyAssertion(HddMetrics.CompletedSliceCount == 8ull, "HDD path fulfilled all 8 tile slice requests") ||
            !VerifyAssertion(HddMetrics.BackwardSeekCount == 0ull, "HDD C-SCAN elevator sort incurred 0 backward seeks") ||
            !VerifyAssertion(HddMetrics.CoalescedSweepCount <= 2ull, "HDD 2 MiB window coalesced 8 tile reads into <= 2 sweeps") ||
            !VerifyAssertion(HddRepo.QueryEntryAt(19u)->TileResident && HddRepo.QueryEntryAt(0u)->TileResident, "Streamed HDD tiles marked resident"))
        {
            return false;
        }
    }

    // 3. Test SSD FlashStorageRandomIo path: exact priority-ordered slice reads without read-ahead bloat
    {
        ThumbnailSheetRepository SsdRepo{};
        std::string              Diagnostic{};
        (void)SsdRepo.LoadBinaryIndexHeaderOnly(IndexFile, SheetFile, Diagnostic);

        AssetStreamThread SsdStreamer{};
        (void)SsdStreamer.Initialize(SandboxRoot, StorageHardwareTier::FlashStorageRandomIo, nullptr);

        std::vector<std::size_t> VisibleIndices = { 5u, 1u, 9u, 3u };
        SsdRepo.RequestVisibleTileStreaming(VisibleIndices, SsdStreamer);

        const auto SsdMetrics = SsdStreamer.InspectReading();
        SsdStreamer.Finalize();

        if (!VerifyAssertion(SsdMetrics.CompletedSliceCount == 4ull, "SSD path fulfilled all 4 exact slice requests") ||
            !VerifyAssertion(SsdRepo.QueryEntryAt(5u)->TileResident && SsdRepo.QueryEntryAt(3u)->TileResident, "Streamed SSD tiles marked resident"))
        {
            return false;
        }
    }

    // 4. Test Two-Step Quarantine -> Scratch fallback -> Project migration & Strict UV Workspace Blocking
    {
        ThumbnailSheetRepository IntakeRepo{};
        ImportIntakeConfiguration Policy{};
        Policy.Mode               = ImportDestinationMode::ProjectImportedFolder;
        Policy.EngineContentRoot  = SandboxRoot / "EngineContent";
        Policy.ActiveProjectRoot.clear(); // Unsaved session -> automatic Scratch fallback
        Policy.ScratchSessionRoot = SandboxRoot / "Projects/_Scratch";
        IntakeRepo.ConfigureImportPolicy(Policy);

        // Create external test files to drop
        const std::filesystem::path ExternalDir     = SandboxRoot / "ExternalDrops";
        const std::filesystem::path DroppedSolid     = ExternalDir / "TrimPanel.gltf";
        const std::filesystem::path DroppedParticle = ExternalDir / "SparkBurst.particlespace";
        const std::filesystem::path DroppedForce    = ExternalDir / "TurbulentWind.simulationspace";
        std::filesystem::create_directories(ExternalDir, IoErr);
        {
            std::ofstream(DroppedSolid) << "{\"asset\":{\"version\":\"2.0\"}}\n";
            std::ofstream(DroppedParticle) << "ParticleCount = 4096\n";
            std::ofstream(DroppedForce) << "FieldStrength = 18.5\n";
        }

        // Strict UV workspace gate: Particles and ForceFields/Simulations MUST be blocked!
        const ImportIntakeResult BlockedParticles = IntakeRepo.IntakeExternalAssetDrop(DroppedParticle, WorkspaceCategory::UV);
        const ImportIntakeResult BlockedForce     = IntakeRepo.IntakeExternalAssetDrop(DroppedForce, WorkspaceCategory::UV);
        if (!VerifyAssertion(!BlockedParticles.Accepted, "UV workspace strictly blocks Particle assets on drop") ||
            !VerifyAssertion(!BlockedForce.Accepted, "UV workspace strictly blocks ForceField/Simulation assets on drop"))
        {
            return false;
        }

        // Valid Geometry drop in UV workspace -> Quarantines in Saved/ImportIntake then lands in _Scratch/Saved/Imported/Geometry
        const ImportIntakeResult AcceptedSolid = IntakeRepo.IntakeExternalAssetDrop(DroppedSolid, WorkspaceCategory::UV);
        if (!VerifyAssertion(AcceptedSolid.Accepted, "UV workspace accepts Geometry (.gltf) drop") ||
            !VerifyAssertion(!std::filesystem::exists(AcceptedSolid.QuarantinePath, IoErr), "Transient quarantine file cleaned up after conversion") ||
            !VerifyAssertion(std::filesystem::exists(AcceptedSolid.FinalSpacePath, IoErr), "Converted .GeometrySpace exists in Scratch fallback") ||
            !VerifyAssertion(AcceptedSolid.FinalSpacePath.extension() == ".GeometrySpace", "Converted file uses canonical .GeometrySpace extension"))
        {
            return false;
        }

        // Migrate Scratch imports into saved project -> Projects/SampleProject/Content/Imported/Geometry/
        const std::filesystem::path SavedProject = SandboxRoot / "Projects/SampleProject";
        std::string                 MigrationDiag{};
        const uint32_t              Migrated = IntakeRepo.MigrateScratchImportsToProject(SavedProject, MigrationDiag);
        if (!VerifyAssertion(Migrated == 1u, "MigrateScratchImportsToProject moves scratch import into Content/Imported/") ||
            !VerifyAssertion(
                std::filesystem::exists(SavedProject / "Content/Imported/Geometry/TrimPanel.GeometrySpace", IoErr),
                "Migrated TrimPanel.GeometrySpace resides in Projects/SampleProject/Content/Imported/Geometry/"))
        {
            return false;
        }

        // Verify Origin == Project searches BOTH Project Content AND EngineContent
        const auto EngineOnly  = IntakeRepo.QueryFilteredEntries(WorkspaceCategory::SceneWorld, ContentCategory::All, ContentOriginFilter::Engine);
        const auto ProjectPlus = IntakeRepo.QueryFilteredEntries(WorkspaceCategory::SceneWorld, ContentCategory::All, ContentOriginFilter::Project);
        if (!VerifyAssertion(ProjectPlus.size() > EngineOnly.size(), "Project origin searches both Project Content and EngineContent"))
        {
            return false;
        }
    }

    std::filesystem::remove_all(SandboxRoot, IoErr);
    return true;
}

void RenderWorkspaceInspectorSlotOverlay(
    PixelSpace& Canvas,
    Rectangle2D AlbedoSlotRect,
    ColorRgba   AlbedoColour,
    Rectangle2D EmissionSlotRect,
    ColorRgba   EmissionColour)
{
    const Rectangle2D Tile{ AlbedoSlotRect.X - 16.0f, AlbedoSlotRect.Y - 40.0f, AlbedoSlotRect.Width + 32.0f, 156.0f };
    Canvas.FillRectangle(Tile, ColorRgba{ 28u, 28u, 31u, 245u }, 14.0f);
    Canvas.StrokeRectangle(Tile, ColorRgba{ 255u, 255u, 255u, 28u }, 14.0f, 1.0f);
    Canvas.Text({ Tile.X + 16.0f, Tile.Y + 13.0f }, 10.0f, ColorRgba{ 168u, 154u, 132u, 255u }, "MATERIAL COLOUR SLOTS (DROP TARGETS)");

    // Slot 1: Surface Albedo
    Canvas.FillRectangle(AlbedoSlotRect, ColorRgba{ 38u, 38u, 42u, 255u }, 9.0f);
    Canvas.StrokeRectangle(AlbedoSlotRect, ColorRgba{ 255u, 255u, 255u, 30u }, 9.0f, 1.0f);
    Canvas.FillRectangle({ AlbedoSlotRect.X + 8.0f, AlbedoSlotRect.Y + 6.0f, 36.0f, AlbedoSlotRect.Height - 12.0f }, AlbedoColour, 6.0f);
    const std::string AlbedoText = "Surface Albedo · " + ColourSwatchBrowserView::FormatHexUpper(AlbedoColour);
    Canvas.Text({ AlbedoSlotRect.X + 54.0f, AlbedoSlotRect.Y + 12.0f }, 11.0f, ColorRgba{ 228u, 228u, 230u, 255u }, AlbedoText.c_str());

    // Slot 2: Emission Tint
    Canvas.FillRectangle(EmissionSlotRect, ColorRgba{ 38u, 38u, 42u, 255u }, 9.0f);
    Canvas.StrokeRectangle(EmissionSlotRect, ColorRgba{ 255u, 255u, 255u, 30u }, 9.0f, 1.0f);
    Canvas.FillRectangle({ EmissionSlotRect.X + 8.0f, EmissionSlotRect.Y + 6.0f, 36.0f, EmissionSlotRect.Height - 12.0f }, EmissionColour, 6.0f);
    const std::string EmissionText = "Emission Tint · " + ColourSwatchBrowserView::FormatHexUpper(EmissionColour);
    Canvas.Text({ EmissionSlotRect.X + 54.0f, EmissionSlotRect.Y + 12.0f }, 11.0f, ColorRgba{ 228u, 228u, 230u, 255u }, EmissionText.c_str());
}

bool ValidateContentBrowserSnapshots()
{
    IMGUI_CHECKVERSION();
    ImGuiContext* GuiSession = ImGui::CreateContext();
    if (!VerifyAssertion(GuiSession != nullptr, "Initialize ImGui session"))
    {
        return false;
    }
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
    if (ImFont* DefaultFace = Typefaces.QueryFont(0u, Frontier::FontWeightClass::Regular))
    {
        Io.FontDefault = DefaultFace;
    }

    unsigned char* TexturePixels = nullptr;
    int            TextureWidth  = 0;
    int            TextureHeight = 0;
    Io.Fonts->GetTexDataAsRGBA32(&TexturePixels, &TextureWidth, &TextureHeight);

    constexpr uint32_t CanvasWidth  = 1440u;
    constexpr uint32_t CanvasHeight = 900u;
    const Rectangle2D  ViewportBounds{ 0.0f, 0.0f, static_cast<float>(CanvasWidth), static_cast<float>(CanvasHeight) };

    HeadlessWorkspaceDockContainer Dock{};
    const auto WindowDescriptor = WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier", CanvasWidth, CanvasHeight);
    if (!Dock.Initialize(WindowDescriptor))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    (void)Dock.RegisterPane(WorkspaceDockPaneDescriptor{ "Outliner", DockPlacementDirection::SplitLeft, 0.21f, PaneDirectiveMask::StandardPaneDirectives, {} });
    (void)Dock.RegisterPane(WorkspaceDockPaneDescriptor{ "Viewport", DockPlacementDirection::CenterTabGroup, 0.54f, PaneDirectiveMask::CentralViewportDirectives, {} });
    (void)Dock.RegisterPane(WorkspaceDockPaneDescriptor{ "Inspector", DockPlacementDirection::SplitRight, 0.25f, PaneDirectiveMask::StandardPaneDirectives, {} });

    ThemeRepository     Theme{};
    Theme.ConfigureTheme(Frontier::ThemePreset::Dark);
    Theme.ConfigureAccent(Frontier::AccentPreset::Blue);

    ContentBrowserPanel Browser{};
    Browser.AttachThemeRepository(&Theme);

    ColorRgba         SurfaceAlbedoColour = { 196u, 178u, 160u, 255u };
    ColorRgba         EmissionTintColour  = { 56u,  72u,  96u,  255u };
    const Rectangle2D AlbedoSlotRect      = { 1092.0f, 118.0f, 304.0f, 38.0f };
    const Rectangle2D EmissionSlotRect    = { 1092.0f, 168.0f, 304.0f, 38.0f };

    Browser.RegisterColourSlot(ColourSlotRegistration{ "Material.SurfaceAlbedo", "Surface Albedo", AlbedoSlotRect, &SurfaceAlbedoColour, {} });
    Browser.RegisterColourSlot(ColourSlotRegistration{ "Material.EmissionTint", "Emission Tint", EmissionSlotRect, &EmissionTintColour, {} });

    const std::filesystem::path OutputDirectory = RepositoryRoot / "Exhibits/Images/ContentBrowser";
    std::filesystem::create_directories(OutputDirectory);

    auto StepPresentation = [&](const ContentBrowserPointerInput& Input)
    {
        ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), Theme);
        Io.DisplaySize = ImVec2(static_cast<float>(CanvasWidth), static_cast<float>(CanvasHeight));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        Dock.PresentWindowImGuiPanes(HostWindowToken{ 0u });

        PixelSpace ForegroundCanvas(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f);
        RenderWorkspaceInspectorSlotOverlay(ForegroundCanvas, AlbedoSlotRect, SurfaceAlbedoColour, EmissionSlotRect, EmissionTintColour);
        Browser.RenderWithInput(ForegroundCanvas, ViewportBounds, Input, 1.0 / 60.0);
        ImGui::Render();
    };

    auto WriteCurrentSnapshot = [&](const char* FileName) -> bool
    {
        const ColorRgba      Backdrop = Theme.QueryDarkestSurface().ToRgba8();
        SoftwareRasterCanvas Raster(CanvasWidth, CanvasHeight, RgbaColor8{ Backdrop.Red, Backdrop.Green, Backdrop.Blue, 255 });
        Raster.RasterizeImGuiBatches(ImGui::GetDrawData());
        return VerifyAssertion(Raster.WritePngFile(OutputDirectory / FileName), FileName);
    };

    ContentBrowserPointerInput IdleInput{};

    // Snapshot 01: Closed bottom-edge "ContentBrowser" trapezoidal notch
    Browser.ConfigureShelfExtent(ContentShelfExtent::Closed, true);
    (void)Browser.ActivateCategory(ContentCategory::Materials);
    for (int Warmup = 0; Warmup < 3; ++Warmup)
    {
        StepPresentation(IdleInput);
    }
    if (!VerifyAssertion(!Browser.InspectReading().ColourFiltersVisible, "Colour Filters hidden when Materials is active") ||
        !WriteCurrentSnapshot("01-ClosedBottomNotch.png"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 02: FullHeight ContentBrowser -> Colours -> GridCircles -> Fig Purple (#3B1F3A, white ink)
    Browser.ConfigureShelfExtent(ContentShelfExtent::FullHeight, true);
    (void)Browser.ActivateCategory(ContentCategory::Colours);
    Browser.ConfigureColourLayout(ColourRepositoryLayout::GridCircles);
    Browser.ConfigureColourFilter(ColourFilterGroup::All);
    (void)Browser.SelectColourByName("Fig Purple");
    StepPresentation(IdleInput);
    if (!VerifyAssertion(Browser.InspectReading().ColourFiltersVisible, "Colour Filters visible when Colours is active") ||
        !WriteCurrentSnapshot("02-ColoursGridFigPurple.png"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 03: Colours -> GridCircles -> Pineapple Yellow (#F6B800, black ink)
    (void)Browser.SelectColourByName("Pineapple Yellow");
    StepPresentation(IdleInput);
    if (!WriteCurrentSnapshot("03-ColoursGridPineappleYellow.png"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 04: Colours -> VerticalStrips -> "August Beige & Brown" (FF7B00..FFEA00 rotated -90deg HEX labels)
    Browser.ConfigureColourLayout(ColourRepositoryLayout::VerticalStrips);
    Browser.ConfigureColourFilter(ColourFilterGroup::WarmAmberNeutrals);
    (void)Browser.SelectColourByName("August Amber");
    StepPresentation(IdleInput);
    if (!VerifyAssertion(Browser.InspectReading().VisibleEntryCount == 10u, "VerticalStrips renders 10 August Beige & Brown strips") ||
        !WriteCurrentSnapshot("04-ColoursVerticalStripsAugustBeigeAndBrown.png"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 05: Colours -> HorizontalStrips -> Stacked spectrum bands
    Browser.ConfigureColourLayout(ColourRepositoryLayout::HorizontalStrips);
    Browser.ConfigureColourFilter(ColourFilterGroup::WarmAmberNeutrals);
    (void)Browser.SelectColourByName("August Gold");
    StepPresentation(IdleInput);
    if (!WriteCurrentSnapshot("05-ColoursHorizontalStrips.png"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Interactive Test A + B -> Snapshot 06: SplitHalf shelf colour slot assignment
    Browser.ConfigureShelfExtent(ContentShelfExtent::SplitHalf, true);
    Browser.ConfigureColourLayout(ColourRepositoryLayout::GridCircles);
    Browser.ConfigureColourFilter(ColourFilterGroup::All);
    Browser.FocusColourSlot("Material.SurfaceAlbedo");
    StepPresentation(IdleInput);

    const std::size_t PinIndex = Browser.QueryColourSwatchBrowser().FindEntryByName("Pineapple Yellow");
    const Rectangle2D PinCell  = Browser.QueryRenderedColourCellBounds(PinIndex);
    {
        ContentBrowserPointerInput DblClick{};
        DblClick.CursorX       = PinCell.Center().X;
        DblClick.CursorY       = PinCell.Center().Y;
        DblClick.Pressed       = true;
        DblClick.DoubleClicked = true;
        StepPresentation(DblClick);
    }
    if (!VerifyAssertion(
            SurfaceAlbedoColour.Red == 246u && SurfaceAlbedoColour.Green == 184u && SurfaceAlbedoColour.Blue == 0u,
            "Double-clicking Pineapple Yellow updates Surface Albedo slot to #F6B800"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    Browser.ConfigureColourLayout(ColourRepositoryLayout::VerticalStrips);
    Browser.ConfigureColourFilter(ColourFilterGroup::WarmAmberNeutrals);
    StepPresentation(IdleInput);

    const std::size_t BlazeIndex = Browser.QueryColourSwatchBrowser().FindEntryByName("August Blaze");
    const Rectangle2D BlazeStrip = Browser.QueryRenderedColourCellBounds(BlazeIndex);
    {
        ContentBrowserPointerInput Press{};
        Press.CursorX  = BlazeStrip.Center().X;
        Press.CursorY  = BlazeStrip.Center().Y;
        Press.Pressed  = true;
        Press.HeldDown = true;
        StepPresentation(Press);

        ContentBrowserPointerInput Move{};
        Move.CursorX  = EmissionSlotRect.Center().X;
        Move.CursorY  = EmissionSlotRect.Center().Y;
        Move.HeldDown = true;
        StepPresentation(Move);

        if (!VerifyAssertion(Browser.InspectReading().DraggingColourActive, "Colour drag tracking activates across deadzone"))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }

        ContentBrowserPointerInput Release{};
        Release.CursorX  = EmissionSlotRect.Center().X;
        Release.CursorY  = EmissionSlotRect.Center().Y;
        Release.Released = true;
        StepPresentation(Release);
    }
    if (!VerifyAssertion(
            EmissionTintColour.Red == 255u && EmissionTintColour.Green == 123u && EmissionTintColour.Blue == 0u,
            "Dragging August Blaze vertical strip onto Emission Tint updates slot to #FF7B00"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    StepPresentation(IdleInput);
    if (!WriteCurrentSnapshot("06-SplitShelfColourSlotAssignment.png"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 07: Scene Workspace -> All Pre-Baked 1024x1024 ThumbnailSheet Assets
    Browser.ConfigureShelfExtent(ContentShelfExtent::FullHeight, true);
    Browser.ConfigureWorkspaceDomain(WorkspaceCategory::SceneWorld);
    Browser.ConfigureOriginFilter(ContentOriginFilter::All);
    (void)Browser.ActivateCategory(ContentCategory::All);
    StepPresentation(IdleInput);
    if (!VerifyAssertion(Browser.InspectReading().PermittedCategoryCount == 13u, "SceneWorld workspace permits 13 asset categories") ||
        !WriteCurrentSnapshot("07-SceneWorkspacePrebakedThumbnailSheet.png"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 08: Strict UV Workspace Mask (6 categories ONLY; Particles/Simulations/Audio blocked) + Imported Geometry Drop
    const std::filesystem::path TempDropDir = std::filesystem::temp_directory_path() / "FrontierUvDropSnapshot";
    std::error_code             DropErr{};
    std::filesystem::remove_all(TempDropDir, DropErr);
    std::filesystem::create_directories(TempDropDir, DropErr);

    ImportIntakeConfiguration UvImportPolicy{};
    UvImportPolicy.Mode               = ImportDestinationMode::ProjectImportedFolder;
    UvImportPolicy.ActiveProjectRoot  = TempDropDir / "Projects/SampleProject";
    UvImportPolicy.ScratchSessionRoot = TempDropDir / "Projects/_Scratch";
    Browser.ConfigureImportDestination(UvImportPolicy);
    Browser.ConfigureWorkspaceDomain(WorkspaceCategory::UV);

    if (!VerifyAssertion(!Browser.ActivateCategory(ContentCategory::Particles), "UV workspace rejects ActivateCategory(Particles)") ||
        !VerifyAssertion(!Browser.ActivateCategory(ContentCategory::Simulations), "UV workspace rejects ActivateCategory(Simulations)") ||
        !VerifyAssertion(Browser.InspectReading().PermittedCategoryCount == 6u, "UV workspace exposes strictly 6 permitted categories"))
    {
        std::filesystem::remove_all(TempDropDir, DropErr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    const std::filesystem::path HullDrop = TempDropDir / "CanopyHullSolid.gltf";
    {
        std::ofstream(HullDrop) << "{\"asset\":{\"version\":\"2.0\"}}\n";
    }
    const ImportIntakeResult DropRes = Browser.IntakeExternalAssetDrop(HullDrop);
    if (!VerifyAssertion(DropRes.Accepted, "Dropping CanopyHullSolid.gltf into UV workspace succeeds"))
    {
        std::filesystem::remove_all(TempDropDir, DropErr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    Browser.ConfigureOriginFilter(ContentOriginFilter::All);
    (void)Browser.ActivateCategory(ContentCategory::All);
    StepPresentation(IdleInput);
    if (!VerifyAssertion(Browser.QuerySearchFieldBounds().Width >= 140.0f, "Search capsule bounds rendered in ContentBrowser toolbar") ||
        !WriteCurrentSnapshot("08-UVWorkspaceStrictCategoryMaskAndImported.png"))
    {
        std::filesystem::remove_all(TempDropDir, DropErr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 09: Daylight Light Theme + Emerald Accent + Active Search Query + List View Icons
    Theme.ConfigureTheme(Frontier::ThemePreset::Light);
    Theme.ConfigureAccent(Frontier::AccentPreset::Emerald);
    Browser.ConfigureWorkspaceDomain(WorkspaceCategory::SceneWorld);
    (void)Browser.ActivateCategory(ContentCategory::All);
    Browser.ConfigureStandardLayout(Frontier::StandardRepositoryLayout::List);
    Browser.ConfigureSearchQuery("Standard");
    StepPresentation(IdleInput);
    if (!VerifyAssertion(Browser.InspectReading().VisibleEntryCount >= 1u, "Search query 'Standard' filters ContentBrowser list") ||
        !WriteCurrentSnapshot("09-LightThemeSearchAndListIcons.png"))
    {
        std::filesystem::remove_all(TempDropDir, DropErr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 10: Arctic Nord Theme + Cyan Accent + Grid View Icons
    Theme.ConfigureTheme(Frontier::ThemePreset::Nord);
    Theme.ConfigureAccent(Frontier::AccentPreset::Cyan);
    Browser.ConfigureSearchQuery({});
    Browser.ConfigureStandardLayout(Frontier::StandardRepositoryLayout::Grid);
    StepPresentation(IdleInput);
    if (!WriteCurrentSnapshot("10-NordThemeGridAndSearchIcons.png"))
    {
        std::filesystem::remove_all(TempDropDir, DropErr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 11: Dark Theme + Geometry Category showing transparent 3D OpenPBR models & FrontierWorldMirror props
    Theme.ConfigureTheme(Frontier::ThemePreset::Dark);
    Theme.ConfigureAccent(Frontier::AccentPreset::Blue);
    (void)Browser.ActivateCategory(ContentCategory::Geometry);
    StepPresentation(IdleInput);
    if (!WriteCurrentSnapshot("11-GeometryModelsTransparent3D.png"))
    {
        std::filesystem::remove_all(TempDropDir, DropErr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Verify transparent RGBA silhouettes on baked Geometry & Materials tiles
    const auto& Repo        = Browser.QueryThumbnailRepository();
    const auto  RepoEntries = Repo.QueryAllEntries();
    bool FoundTrafficCone = false;
    bool FoundStandardMat = false;
    for (std::size_t Idx = 0u; Idx < RepoEntries.size(); ++Idx)
    {
        const auto& E = RepoEntries[Idx];
        if (E.DisplayTitle == "Traffic Cone" || E.DisplayTitle == "Standard Preset")
        {
            std::vector<uint8_t> TileRgba{};
            if (!VerifyAssertion(Repo.QueryTileRgbaPixels(Idx, TileRgba) && !TileRgba.empty(), "Baked Geometry/Material tile pixels resident"))
            {
                std::filesystem::remove_all(TempDropDir, DropErr);
                ImGui::DestroyContext(GuiSession);
                return false;
            }
            bool HasTransparent = false;
            bool HasOpaque      = false;
            for (std::size_t Off = 0u; Off + 3u < TileRgba.size(); Off += 4u)
            {
                const uint8_t A = TileRgba[Off + 3u];
                HasTransparent = HasTransparent || (A == 0u);
                HasOpaque      = HasOpaque || (A == 255u);
            }
            if (!VerifyAssertion(HasTransparent && HasOpaque, "Baked 3D specimen tile has transparent background & opaque silhouette"))
            {
                std::filesystem::remove_all(TempDropDir, DropErr);
                ImGui::DestroyContext(GuiSession);
                return false;
            }
            if (E.DisplayTitle == "Traffic Cone") FoundTrafficCone = true;
            if (E.DisplayTitle == "Standard Preset") FoundStandardMat = true;
        }
    }
    if (!VerifyAssertion(FoundTrafficCone && FoundStandardMat, "Traffic Cone & Standard Preset exist in ThumbnailSheetRepository"))
    {
        std::filesystem::remove_all(TempDropDir, DropErr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    std::filesystem::remove_all(TempDropDir, DropErr);
    TypographyRepository::AssignCurrent(nullptr);
    Dock.Finalize();
    ImGui::DestroyContext(GuiSession);
    return true;
}

bool ValidateEditorDropPlacement()
{
    ImGuiContext* GuiSession = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiSession);
    ImGuiIO& Io = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    Io.DisplaySize = ImVec2(1440.0f, 900.0f);
    Frontier::Lifecycle::EditorStartupConfiguration Cfg{};
    Cfg.WindowDescriptor = Frontier::Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier", 1440u, 900u);
    Cfg.FontRepositoryRoot           = ResolveRepositoryRootPath() / "EngineContent/FontsArchives";
    Cfg.PopulateShowcaseScene        = true;

    Frontier::Lifecycle::EditorLifecycle Editor{};
    if (!VerifyAssertion(Editor.Initialize(Cfg), "EditorLifecycle initializes cleanly for drag-drop validation"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    unsigned char* FontPixels = nullptr;
    int            FontW      = 0;
    int            FontH      = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH);
    Io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    auto StepEditorFrame = [&]()
    {
        (void)Editor.ExecuteAuthoringTick(1.0 / 60.0);
        Io.DisplaySize = ImVec2(1440.0f, 900.0f);
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        Editor.PresentPrimaryEditorImGui(1.0 / 60.0);
        ImGui::Render();
    };

    // Run 2 frames so DockContainer splits Outliner/Viewport/Inspector and registers drop targets
    StepEditorFrame();
    StepEditorFrame();

    auto* Panes   = Editor.AccessWorkspaces().AccessActivePanes();
    auto* Scene   = Editor.AccessShowcaseScene();
    auto& Browser = Editor.AccessContentBrowser().AccessPanel();
    if (!VerifyAssertion(Panes != nullptr && Scene != nullptr, "Active workspace panes & ShowcaseWorldSceneBundle present"))
    {
        Editor.Finalize();
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    const Rectangle2D ViewportPhys = Panes->Viewport.QueryLastCanvasPhysicalBounds();
    const Rectangle2D OutlinerPhys = Panes->Outliner.QueryLastPhysicalBounds();
    if (!VerifyAssertion(ViewportPhys.Width > 100.0f && OutlinerPhys.Width > 100.0f && ViewportPhys.X > OutlinerPhys.X,
                         "Viewport & Outliner physical bounds recorded in multi-pane dock layout"))
    {
        Editor.Finalize();
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    const Rectangle2D ViewportDrop = Browser.MapPhysicalToLogicalBounds(ViewportPhys);
    const Rectangle2D OutlinerDrop = Browser.MapPhysicalToLogicalBounds(OutlinerPhys);
    const Point2D ViewportCenter{ ViewportDrop.X + ViewportDrop.Width * 0.5f, ViewportDrop.Y + ViewportDrop.Height * 0.62f };
    const Point2D OutlinerCenter{ OutlinerDrop.X + OutlinerDrop.Width * 0.5f, OutlinerDrop.Y + OutlinerDrop.Height * 0.50f };

    const auto FindRepoIndex = [&](std::string_view Title) -> std::size_t
    {
        const auto List = Browser.QueryThumbnailRepository().QueryAllEntries();
        for (std::size_t Idx = 0u; Idx < List.size(); ++Idx)
        {
            if (List[Idx].DisplayTitle == Title)
            {
                return Idx;
            }
        }
        return List.size();
    };

    const std::size_t InitialObjCount   = Scene->Objects.size();
    const std::size_t InitialLightCount = Scene->Scene.AreaLights.size();
    const std::size_t InitialForceCount = Editor.AccessForceAccumulator().QuerySources().size();

    // 1. Drop Traffic Cone onto 3D Viewport
    const std::size_t ConeIdx = FindRepoIndex("Traffic Cone");
    if (!VerifyAssertion(ConeIdx < Browser.QueryThumbnailRepository().QueryAllEntries().size(), "Traffic Cone entry located") ||
        !VerifyAssertion(Browser.ApplyContentEntryToDropZone(ConeIdx, "world.viewport", ViewportCenter),
                         "Drop Traffic Cone onto world.viewport zone") ||
        !VerifyAssertion(Scene->Objects.size() == InitialObjCount + 1u && Scene->Objects.back().Title == "Traffic Cone",
                         "Traffic Cone constructed in 3D ShowcaseWorldSceneBundle & Outliner"))
    {
        Editor.Finalize();
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // 2. Drop Dielectric Glass Material onto a row via world.outliner zone
    const std::size_t GlassIdx = FindRepoIndex("Dielectric Glass");
    if (!VerifyAssertion(GlassIdx < Browser.QueryThumbnailRepository().QueryAllEntries().size(), "Dielectric Glass entry located") ||
        !VerifyAssertion(Browser.ApplyContentEntryToDropZone(GlassIdx, "world.outliner", OutlinerCenter),
                         "Drop Dielectric Glass onto world.outliner zone"))
    {
        Editor.Finalize();
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    {
        const Frontier::OutlinerSlotToken SelectedAfterMat = Panes->Outliner.QueryPrimarySelection();
        bool AppliedToTarget = false;
        for (const auto& Obj : Scene->Objects)
        {
            if (Obj.SlotToken == SelectedAfterMat && Obj.GlassShell && Obj.MaterialTitle == "Dielectric Glass")
            {
                AppliedToTarget = true;
                break;
            }
        }
        if (!VerifyAssertion(AppliedToTarget, "Dielectric Glass applied OpenPBR glass material to dropped Outliner target"))
        {
            Editor.Finalize();
            ImGui::DestroyContext(GuiSession);
            return false;
        }
    }

    // 3. Drop Studio Softbox Light (Photometry) onto world.outliner
    const std::size_t LightIdx = FindRepoIndex("Studio Softbox Light");
    if (!VerifyAssertion(LightIdx < Browser.QueryThumbnailRepository().QueryAllEntries().size(), "Studio Softbox Light entry located") ||
        !VerifyAssertion(Browser.ApplyContentEntryToDropZone(LightIdx, "world.outliner", OutlinerCenter),
                         "Drop Studio Softbox Light onto world.outliner zone") ||
        !VerifyAssertion(Scene->Scene.AreaLights.size() == InitialLightCount + 1u,
                         "Studio Softbox Light spawned new LTC area luminaire"))
    {
        Editor.Finalize();
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // 4. Drop Turbulent Wind Field (Simulations) onto world.viewport
    const std::size_t WindIdx = FindRepoIndex("Turbulent Wind Field");
    if (!VerifyAssertion(WindIdx < Browser.QueryThumbnailRepository().QueryAllEntries().size(), "Turbulent Wind Field entry located") ||
        !VerifyAssertion(Browser.ApplyContentEntryToDropZone(WindIdx, "world.viewport", ViewportCenter),
                         "Drop Turbulent Wind Field onto world.viewport zone") ||
        !VerifyAssertion(Editor.AccessForceAccumulator().QuerySources().size() == InitialForceCount + 1u,
                         "Turbulent Wind Field spawned ForceField source in world"))
    {
        Editor.Finalize();
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // 5. Drop Studio MatCap onto WorldViewportMatCap
    const std::size_t MatCapIdx = FindRepoIndex("CNC Aluminum");
    if (!VerifyAssertion(MatCapIdx < Browser.QueryThumbnailRepository().QueryAllEntries().size(), "CNC Aluminum MatCap located") ||
        !VerifyAssertion(Browser.ApplyMatCapEntryToSlot(MatCapIdx, "WorldViewportMatCap"),
                         "Drop CNC Aluminum MatCap onto WorldViewportMatCap") ||
        !VerifyAssertion(Panes->Viewport.QueryDisplaySettings().RenderEngineIndex == 0u,
                         "MatCap drop switched Viewport RenderEngineIndex to MatCap (0)"))
    {
        Editor.Finalize();
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // 6. Drop Colour Swatch onto ActiveWorldSurfaceColour
    if (!VerifyAssertion(Browser.ApplyColourIndexToSlot(0u, "ActiveWorldSurfaceColour", ViewportCenter),
                         "Drop Colour Swatch onto ActiveWorldSurfaceColour"))
    {
        Editor.Finalize();
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    Editor.Finalize();
    ImGui::DestroyContext(GuiSession);
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    if (!ValidateColourSwatchTomlPersistence())
    {
        return 1;
    }
    if (!ValidateDualPathStreamingIsolation())
    {
        return 1;
    }
    if (!ValidateContentBrowserSnapshots())
    {
        return 1;
    }
    if (!ValidateEditorDropPlacement())
    {
        return 1;
    }
    std::printf("[ContentBrowserValidation] All ThumbnailSheet, SSD/HDD streaming, Quarantine Import, UV mask, .ColourSwatch, and Editor drag-drop checks passed.\n");
    return 0;
}
