/*==========================================================================================================================================
                                                 MATERIALWORKSPACEUIVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Native Frontier UI exhibit for Material Workspace (Viewport-only + ContentBrowser, matching Pattern/Baking Studio chrome; Inspector deferred)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Workspaces/Material/MaterialWorkspaceHost.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace
{

[[nodiscard]] bool VerifyCondition(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[MaterialWorkspaceUIValidation] FAIL: %s\n", Message);
        return false;
    }
    return true;
}

[[nodiscard]] bool CaptureMaterialWorkspacePng(
    ImGuiIO&                                              Io,
    Frontier::Components::HeadlessWorkspaceDockContainer& Dock,
    Frontier::ControlCentrePanel&                         ControlCentre,
    Frontier::ContentBrowserPanel&                        Browser,
    Frontier::Workspaces::MaterialWorkspaceHost&          Host,
    const std::filesystem::path&                          TargetPngPath,
    uint32_t                                              WidthPx  = 1600u,
    uint32_t                                              HeightPx = 980u)
{
    const Frontier::Rectangle2D HostBounds { 0.0f, 0.0f, static_cast<float>(WidthPx), static_cast<float>(HeightPx) };
    const Frontier::ContentBrowserPointerInput IdlePtr {};

    for (int Tick = 0; Tick < 4; ++Tick)
    {
        Io.DisplaySize = ImVec2(static_cast<float>(WidthPx), static_cast<float>(HeightPx));
        Io.DeltaTime   = 1.0f / 60.0f;
        Browser.SynchronizeHostBounds(HostBounds, 1.0f);
        Host.SynchronizeContentBrowserOcclusion(1.0f);

        ImGui::NewFrame();
        Dock.PresentWindowImGuiPanes(Frontier::Components::HostWindowToken{ 0u });

        Frontier::PixelSpace OverlaySurface(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f);
        Browser.RenderWithInput(OverlaySurface, HostBounds, IdlePtr, 1.0 / 60.0);
        ControlCentre.Render(HostBounds, 1.0 / 60.0, 60.0f);
        ImGui::Render();
    }

    Frontier::Exhibits::SoftwareRasterCanvas Canvas(WidthPx, HeightPx, { 11u, 11u, 12u, 255u });
    Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
    return VerifyCondition(Canvas.WritePngFile(TargetPngPath), TargetPngPath.filename().string().c_str());
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::Configuration;
    using namespace Frontier::MaterialSpace::Dynamic;
    using namespace Frontier::Workspaces;

    const std::filesystem::path RepoRoot = Exhibits::ResolveRepositoryRootPath();
    const std::filesystem::path ImageDir = RepoRoot / "Exhibits/Images/Materials";
    std::filesystem::create_directories(ImageDir);

    // 1. Initialize ImGui + Frontier TypographyRepository (exact match to Pattern/Baking Studio exhibits)
    IMGUI_CHECKVERSION();
    ImGuiContext* GuiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiCtx);
    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    TypographyRepository Typefaces {};
    (void)Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives");
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* TabFont = Typefaces.QueryTabStripFont())
    {
        Io.FontDefault = TabFont;
    }

    unsigned char* FontSheetPixels = nullptr;
    int            FontSheetWidth  = 0;
    int            FontSheetHeight = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontSheetPixels, &FontSheetWidth, &FontSheetHeight);

    // 2. Initialize Vulkan 1.3 SwiftShader device for GPU Direct OpenPBR viewport + thumbnails
    GeometricRaster::VulkanInterchange VulkanDevice {};
    std::string VulkanDiag {};
    const bool VulkanReady = VulkanDevice.InitializeInterchange(RepoRoot, &VulkanDiag);
#ifdef FRONTIER_COMPILED_SHADER_DIR
    const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
#else
    const std::filesystem::path ShaderDir = RepoRoot / "build/CompiledShaders";
#endif
    const std::filesystem::path SpvPath   = ShaderDir / "DynamicMaterialResolve.comp.spv";

    // 3. Mount Material Workspace (Viewport-only + ContentBrowser; Inspector & Layer Stack intentionally deferred)
    {
        ThemeRepository Theme {};
        Theme.ConfigureTheme(ThemePreset::Dark);

        ContentBrowserPanel Browser {};
        Browser.AttachThemeRepository(&Theme);
        Browser.ConfigureOriginFilter(ContentOriginFilter::Project);

        MaterialWorkspaceHost MaterialHost {};
        MaterialHost.ConfigureLaunchPolicy(EngineConfiguration(EngineConfiguration::ConstructFeatureSetDefault()));
        if (VulkanReady && std::filesystem::exists(SpvPath))
        {
            (void)MaterialHost.AttachVulkanDevice(&VulkanDevice, ShaderDir);
        }
        MaterialHost.AttachContentBrowserPanel(&Browser);

        if (!VerifyCondition(MaterialHost.QueryPopulatedThumbnailCount() == 13u, "All 13 dynamic presets populated in ContentBrowser"))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }

        Components::HeadlessWorkspaceDockContainer Dock {};
        const auto WinDesc = Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier Material Studio", 1600, 980);
        if (!VerifyCondition(Dock.Initialize(WinDesc), "Initialized HeadlessWorkspaceDockContainer for Material Studio"))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
        MaterialHost.RegisterDockPanes(Dock);
        if (!VerifyCondition(MaterialHost.QueryRegisteredTabCount() == 1u, "Viewport-only single Material dock tab registered"))
        {
            Dock.Finalize();
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }

        Components::ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), Theme);

        ControlCentrePanel ControlCentre {};
        ControlCentre.AttachTypefaces(&Typefaces);
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        ControlCentre.ApplyDraftAppearanceImmediate();
        ControlCentre.AccessNotificationStream().ClearPending();

        // Proof 1: Full Material Viewport + Closed ContentBrowser bottom notch (Volcanic Pahoehoe Magma)
        MaterialHost.AccessSession().SelectPresetIdentity(DynamicPresetIdentity::VolcanicPahoehoeMagma);
        MaterialHost.AccessSession().PlaybackRunning      = false;
        MaterialHost.AccessSession().PlaybackClockSeconds = 1.45;
        Browser.ConfigureShelfExtent(ContentShelfExtent::Closed, true);
        if (!CaptureMaterialWorkspacePng(Io, Dock, ControlCentre, Browser, MaterialHost, ImageDir / "CppMaterialWorkspace_Viewport_Volcanic.png"))
        {
            Dock.Finalize();
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }

        // Verify strictly viewport-only dock pane (no deferred Inspector / Layer Stack windows mounted)
        {
            bool FoundMaterialWin = false;
            bool FoundDeferredWin = false;
            if (ImGuiContext* Ctx = ImGui::GetCurrentContext())
            {
                for (ImGuiWindow* Win : Ctx->Windows)
                {
                    if (Win != nullptr && Win->Active)
                    {
                        const std::string_view Name(Win->Name);
                        if (Name.starts_with("Material##")) FoundMaterialWin = true;
                        if (Name.starts_with("Inspector##") || Name.starts_with("Layers##")) FoundDeferredWin = true;
                    }
                }
            }
            if (!VerifyCondition(FoundMaterialWin && !FoundDeferredWin, "Strictly Viewport-only Material pane mounted (Inspector deferred)"))
            {
                Dock.Finalize();
                ImGui::DestroyContext(GuiCtx);
                return 1;
            }
        }

        // Proof 2: Material Viewport + ContentBrowser SplitHalf showing all 13 live Dynamic Material thumbnails (Hexagonal Energy Barrier)
        MaterialHost.AccessSession().SelectPresetIdentity(DynamicPresetIdentity::HexagonalEnergyBarrier);
        MaterialHost.AccessSession().TriggerViewportImpactPulse({ 0.30f, 0.28f, 0.91f });
        Browser.ConfigureShelfExtent(ContentShelfExtent::SplitHalf, true);
        (void)Browser.ActivateCategory(ContentCategory::Materials);
        Browser.ConfigureOriginFilter(ContentOriginFilter::Project);
        if (!CaptureMaterialWorkspacePng(Io, Dock, ControlCentre, Browser, MaterialHost, ImageDir / "CppMaterialWorkspace_ViewportBrowser_Shield.png"))
        {
            Dock.Finalize();
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }

        // Proof 3: ContentBrowser entry activation switches viewport to Surface Material Transition seamlessly
        bool ActivatedTransition = false;
        const auto& Entries = Browser.QueryThumbnailRepository().QueryAllEntries();
        for (std::size_t Idx = 0u; Idx < Entries.size(); ++Idx)
        {
            if (Entries[Idx].DisplayTitle == "Surface Material Transition")
            {
                ActivatedTransition = Browser.ActivateContentEntryAt(Idx);
                break;
            }
        }
        if (!VerifyCondition(
                ActivatedTransition &&
                MaterialHost.InspectSession().InspectActiveRecipe().PresetIdentity == DynamicPresetIdentity::SurfaceMaterialTransition,
                "Activating dynamic preset in ContentBrowser updates live Material Viewport"))
        {
            Dock.Finalize();
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }

        MaterialHost.AccessSession().ActiveSpecimen = PreviewSpecimenGeometry::ShaderballPedestal;
        if (!CaptureMaterialWorkspacePng(Io, Dock, ControlCentre, Browser, MaterialHost, ImageDir / "CppMaterialWorkspace_ViewportBrowser_Transition.png"))
        {
            Dock.Finalize();
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }

        Dock.Finalize();
    }

    if (VulkanReady)
    {
        VulkanDevice.TeardownInterchange();
    }

    ImGui::DestroyContext(GuiCtx);
    TypographyRepository::AssignCurrent(nullptr);
    std::printf("[MaterialWorkspaceUIValidation] ALL CHECKS PASSED (Viewport-only + ContentBrowser 13 Dynamic Material thumbnails)\n");
    return 0;
}
