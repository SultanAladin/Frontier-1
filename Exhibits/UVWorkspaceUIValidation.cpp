/*============================================================================================================================================
                                                      UVWORKSPACEUIVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase 3 validation suite for UVOutlinerPanel, UVInspectorPanel, UVViewportHost, StandaloneWorkspace (FrontierUV) & FeatureSet UI

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometricRaster/UV/UVWorkspaceRasterizer.h"
#include "Engine/Workspaces/UV/UVWorkspaceHost.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                VERIFICATION ASSERTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool VerifyCondition(bool ConditionMet, const char* Description) noexcept
{
    if (!ConditionMet)
    {
        std::fprintf(stderr, "[UVWorkspaceUIValidation] FAILED: %s\n", Description);
        return false;
    }
    return true;
}

[[nodiscard]] bool CaptureDockedWorkspacePng(
    ImGuiIO&                                              Io,
    Frontier::Components::HeadlessWorkspaceDockContainer& Container,
    Frontier::ControlCentrePanel&                         ControlCentre,
    const std::filesystem::path&                          TargetPngPath)
{
    for (int StepIdx = 0; StepIdx < 3; ++StepIdx)
    {
        Io.DisplaySize = ImVec2(1840.0f, 980.0f);
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        Container.PresentWindowImGuiPanes(Frontier::Components::HostWindowToken{ 0u });
        ControlCentre.Render({ 0.0f, 0.0f, 1840.0f, 980.0f }, 1.0 / 60.0, 60.0f);
        ImGui::Render();
    }

    Frontier::Exhibits::SoftwareRasterCanvas Canvas(1840u, 980u, { 14, 14, 16, 255 });
    Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
    return VerifyCondition(Canvas.WritePngFile(TargetPngPath), TargetPngPath.filename().string().c_str());
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    using namespace Frontier;
    using namespace Frontier::Configuration;
    using namespace Frontier::GeometricRaster;
    using namespace Frontier::Workspaces;

    const std::filesystem::path RepoRoot  = Exhibits::ResolveRepositoryRootPath();
    const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
    const std::filesystem::path ImageDir  = RepoRoot / "Exhibits/Images/UV";
    std::filesystem::create_directories(ImageDir);

    VulkanInterchange VkInterchange {};
    std::string         VkDiag;
    if (!VerifyCondition(VkInterchange.InitializeInterchange(RepoRoot, &VkDiag), VkDiag.c_str()))
    {
        return 1;
    }

    UVWorkspaceRasterizer Rasterizer {};
    if (!VerifyCondition(Rasterizer.InitializeRasterizer(&VkInterchange, ShaderDir), "Initialized UVWorkspaceRasterizer"))
    {
        return 1;
    }

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
    unsigned char* AtlasPixels = nullptr;
    int            AtlasW      = 0;
    int            AtlasH      = 0;
    Io.Fonts->GetTexDataAsRGBA32(&AtlasPixels, &AtlasW, &AtlasH);

    // 1. Validate StandaloneWorkspace (FrontierUV) Layout & Share Clamping Semantics
    {
        EngineConfiguration StandaloneCfg(EngineConfiguration::ConstructStandaloneUVDefault());
        UVWorkspaceHost     StandaloneHost {};
        StandaloneHost.ConfigureLaunchPolicy(StandaloneCfg);
        if (!VerifyCondition(StandaloneHost.AttachVulkanRasterizer(&Rasterizer), "Attached Vulkan rasterizer to StandaloneHost"))
        {
            return 1;
        }

        // Verify WorkspaceDocument::AccessViewportProjection() aspect-locked 1:1 planar bounds & ContentBrowser WorkspaceCategory::UV binding
        Mathematics::UVAxes MinUV {};
        Mathematics::UVAxes MaxUV {};
        StandaloneHost.InspectSession().InspectViewportProjection().EvaluateAspectLockedPlanarBounds(800u, 400u, MinUV, MaxUV);
        const float SpanAspect = (MaxUV.U - MinUV.U) / std::max(1.0e-5f, (MaxUV.V - MinUV.V));
        if (!VerifyCondition(std::abs(SpanAspect - 2.0f) < 1.0e-3f, "ViewportProjection preserves 2:1 aspect ratio for square UDIM tiles"))
        {
            return 1;
        }
#if FRONTIER_EDITOR_ENABLED
        const auto SavedLayer1Tint = StandaloneHost.InspectSession().Asset.Layers.InspectLayer(1u)->LinearTintRGB;
        Editor::Explorer::ContentBrowser Browser {};
        StandaloneHost.AttachContentBrowser(&Browser);
        const auto BrowserReading = Browser.QueryPanel().InspectReading();
        if (!VerifyCondition(BrowserReading.ActiveWorkspace == WorkspaceCategory::UV, "AttachContentBrowser configures WorkspaceCategory::UV") ||
            !VerifyCondition(BrowserReading.PermittedCategoryCount == 2u, "UV ContentBrowser locks strictly to Colours & MatCap Materials") ||
            !VerifyCondition(Browser.AccessPanel().ApplyColourIndexToSlot(0u, "uv.priority_layer.tint"), "ColourCodex swatch tints UV Priority Layer"))
        {
            return 1;
        }
        const auto BrowserEntries = Browser.QueryPanel().QueryThumbnailRepository().QueryAllEntries();
        uint32_t StudioMatCapCount = 0u;
        std::size_t FirstMatCapEntryIdx = SIZE_MAX;
        for (std::size_t Idx = 0u; Idx < BrowserEntries.size(); ++Idx)
        {
            if (BrowserEntries[Idx].IndexHeader.MatCapPresetMarker != 0u)
            {
                if (FirstMatCapEntryIdx == SIZE_MAX)
                {
                    FirstMatCapEntryIdx = Idx;
                }
                ++StudioMatCapCount;
            }
        }
        if (!VerifyCondition(StudioMatCapCount == 16u && FirstMatCapEntryIdx != SIZE_MAX, "UV ContentBrowser exposes all 16 Studio MatCap spheres") ||
            !VerifyCondition(Browser.AccessPanel().ApplyMatCapEntryToSlot(FirstMatCapEntryIdx, "uv.viewport3d.surface"), "Applying a ContentBrowser MatCap updates active UV faces"))
        {
            return 1;
        }
        const auto AeroEntry = std::find_if(BrowserEntries.begin(), BrowserEntries.end(), [](const ContentRepositoryEntry& Entry)
        {
            return Entry.DisplayTitle == "Aero Coupe GT";
        });
        if (!VerifyCondition(AeroEntry != BrowserEntries.end(), "UV ContentBrowser registers the Aero Coupe GT sample asset") ||
            !VerifyCondition(
                Browser.AccessPanel().ActivateContentEntryAt(static_cast<std::size_t>(AeroEntry - BrowserEntries.begin())) &&
                    StandaloneHost.InspectSession().ActivePreset == GeometrySpace::SampleAssetPreset::AeroCoupeGT,
                "opening a ContentBrowser sample asset loads it into the UV workspace"))
        {
            return 1;
        }
        StandaloneHost.AccessSession().LoadSamplePreset(GeometrySpace::SampleAssetPreset::ControlledIntegrator);
        StandaloneHost.AccessSession().AssignSelectedLayerTint(SavedLayer1Tint);
#endif

        // Verify default vehicle sample asset (25,068-triangle Controlled Integrator) & share ceiling clamp (Layer 1 + Layer 2 <= 100%)
        if (!VerifyCondition(
                StandaloneHost.InspectSession().ActivePreset == GeometrySpace::SampleAssetPreset::ControlledIntegrator &&
                    StandaloneHost.InspectSession().Asset.Network.Faces.size() == 25068u,
                "Default UVWorkspaceSession sample asset is the 25,068-triangle Controlled Integrator vehicle"))
        {
            return 1;
        }

        StandaloneHost.AccessSession().SelectPriorityLayer(1u);
        StandaloneHost.AccessSession().AdjustSelectedLayerShare(95.0f); // Layer 2 ("Optics & Glass") has 15%, so Layer 1 clamps to 85%
        const auto* Layer1 = StandaloneHost.InspectSession().Asset.Layers.InspectLayer(1u);
        if (!VerifyCondition(Layer1 != nullptr && std::abs(Layer1->TargetSharePercent - 85.0f) < 1.0e-3f, "Layer share slider clamps cleanly at 100% ceiling"))
        {
            return 1;
        }
        StandaloneHost.AccessSession().AdjustSelectedLayerShare(55.0f);
        StandaloneHost.AccessSession().SelectionMask.ClearAll();
        StandaloneHost.AccessSession().SynchronizeSelectionToGPU();
        StandaloneHost.AccessSession().FramebuffersDirty = true;

        Components::HeadlessWorkspaceDockContainer StandaloneDock {};
        const auto WinDesc = Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier UV", 1840, 980);
        if (!VerifyCondition(StandaloneDock.Initialize(WinDesc), "Initialized StandaloneDock"))
        {
            return 1;
        }

        StandaloneHost.RegisterDockPanes(StandaloneDock);
        if constexpr (BuildConfiguration::EditorIncluded)
        {
            if (!VerifyCondition(StandaloneHost.QueryRegisteredTabCount() == 2u, "StandaloneWorkspace registers separate 2D UV and 3D Surface viewports"))
            {
                return 1;
            }
        }

        ThemeRepository Theme {};
        Theme.ConfigureTheme(ThemePreset::Dark);
        Components::ConfigureImGuiWorkspaceTheme(StandaloneDock.ActiveTabMetrics(), Theme);

        ControlCentrePanel ControlCentre {};
        ControlCentre.AttachTypefaces(&Typefaces);
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        ControlCentre.ApplyDraftAppearanceImmediate();
        ControlCentre.AccessNotificationStream().ClearPending();

        if (!CaptureDockedWorkspacePng(Io, StandaloneDock, ControlCentre, ImageDir / "02-StandaloneUVWorkspaceUI.png"))
        {
            return 1;
        }

        // Verify 2D UV and 3D Surface live in separate non-overlapping docked panes with dedicated projection policies
        {
            ImGuiWindow* Win2D = nullptr;
            ImGuiWindow* Win3D = nullptr;
            if (ImGuiContext* Ctx = ImGui::GetCurrentContext())
            {
                for (ImGuiWindow* Win : Ctx->Windows)
                {
                    if (Win != nullptr && Win->Active)
                    {
                        const std::string_view Name(Win->Name);
                        if (Name.starts_with("2D UV##"))
                        {
                            Win2D = Win;
                        }
                        else if (Name.starts_with("3D Surface##"))
                        {
                            Win3D = Win;
                        }
                    }
                }
            }
            if (!VerifyCondition(Win2D != nullptr && Win3D != nullptr, "Both '2D UV' and '3D Surface' ImGui windows exist in StandaloneDock") ||
                !VerifyCondition(
                    Win2D->Size.x >= 400.0f && Win3D->Size.x >= 400.0f &&
                        (Win2D->Pos.x + Win2D->Size.x <= Win3D->Pos.x + 2.0f),
                    "2D UV sits left of 3D Surface in distinct non-overlapping docked panes") ||
                !VerifyCondition(
                    StandaloneHost.Access2DViewportHost().QueryHostRole() == UVViewportHostRole::Dedicated2D &&
                        StandaloneHost.Access3DViewportHost().QueryHostRole() == UVViewportHostRole::Dedicated3D &&
                        StandaloneHost.Access2DViewportHost().InspectProjectionPane().QueryEffectiveSplitMode() == Editor::ViewportSplitMode::Planar2DOnly &&
                        StandaloneHost.Access3DViewportHost().InspectProjectionPane().QueryEffectiveSplitMode() == Editor::ViewportSplitMode::Spatial3DOnly,
                    "2D UV and 3D Surface enforce Dedicated2D / Dedicated3D split projection isolation"))
            {
                return 1;
            }
        }

        // Verify live window resize updates root dock container dimensions without requiring layout rebuild
        if (auto* HostSurface = StandaloneDock.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
        {
            HostSurface->ResizeSurface(2200u, 1200u, 1.0f);
            Io.DisplaySize = ImVec2(2200.0f, 1200.0f);
            Io.DeltaTime   = 1.0f / 60.0f;
            ImGui::NewFrame();
            StandaloneDock.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
            ImGui::Render();
            const auto ResizedExtent = HostSurface->InspectExtent();
            if (!VerifyCondition(
                    ResizedExtent.PixelWidth == 2200u && ResizedExtent.PixelHeight == 1200u,
                    "WorkspaceDockContainer propagates live host window resize"))
            {
                return 1;
            }
        }
        StandaloneDock.Finalize();
    }

    // 2. Validate FeatureSet Multi-Workspace Docked Layout + UDIMArchKit + 16-MatCap Shading Tab
    {
        EngineConfiguration FeatureSetCfg(EngineConfiguration::ConstructFeatureSetDefault());
        UVWorkspaceHost     SuiteHost {};
        SuiteHost.ConfigureLaunchPolicy(FeatureSetCfg);
        if (!VerifyCondition(SuiteHost.AttachVulkanRasterizer(&Rasterizer), "Attached Vulkan rasterizer to SuiteHost"))
        {
            return 1;
        }

        SuiteHost.AccessSession().LoadSamplePreset(GeometrySpace::SampleAssetPreset::UDIMArchKit);
        if (!VerifyCondition(SuiteHost.InspectSession().Asset.UDIMTiles.size() == 4u, "UDIMArchKit exposes 4 UDIM tiles"))
        {
            return 1;
        }
        SuiteHost.AccessSession().LoadSamplePreset(GeometrySpace::SampleAssetPreset::ControlledIntegrator);
        SuiteHost.AccessSession().ActiveInspectorTab = UVInspectorTab::Shading;

        Components::HeadlessWorkspaceDockContainer SuiteDock {};
        const auto WinDesc = Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier", 1840, 980);
        if (!VerifyCondition(SuiteDock.Initialize(WinDesc), "Initialized SuiteDock"))
        {
            return 1;
        }

        SuiteHost.RegisterDockPanes(SuiteDock);
        if constexpr (BuildConfiguration::EditorIncluded)
        {
            if (!VerifyCondition(SuiteHost.QueryRegisteredTabCount() == 6u, "FeatureSet registers separate 2D UV and 3D Surface viewports alongside 4 peer domain tabs"))
            {
                return 1;
            }
        }

        ThemeRepository Theme {};
        Theme.ConfigureTheme(ThemePreset::Nord);
        Components::ConfigureImGuiWorkspaceTheme(SuiteDock.ActiveTabMetrics(), Theme);

        ControlCentrePanel ControlCentre {};
        ControlCentre.AttachTypefaces(&Typefaces);
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Nord;
        ControlCentre.ApplyDraftAppearanceImmediate();
        ControlCentre.AccessNotificationStream().ClearPending();

        if (!CaptureDockedWorkspacePng(Io, SuiteDock, ControlCentre, ImageDir / "03-FeatureSetUVWorkspaceUI.png"))
        {
            return 1;
        }
        SuiteDock.Finalize();
    }

    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(GuiCtx);
    Rasterizer.TeardownRasterizer();
    VkInterchange.TeardownInterchange();

    std::printf("[UVWorkspaceUIValidation] Phase 3 passed: StandaloneWorkspace (FrontierUV) & FeatureSet docked UI snapshots verified.\n");
    return 0;
}
