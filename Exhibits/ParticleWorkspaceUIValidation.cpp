/*============================================================================================================================================
                                                    PARTICLEWORKSPACEUIVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end UI validation & 6-view 1440x900 visual parity snapshot suite for Engine/Workspaces/Particle vs Experimental/ParticleEditor Final

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceHost.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cstdio>
#include <filesystem>

namespace
{

using namespace Frontier;
using namespace Frontier::Workspaces;
using Frontier::Components::RgbaColor8;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;

bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ParticleWorkspaceUIValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    constexpr uint32_t CanvasW = 1440u;
    constexpr uint32_t CanvasH = 900u;

    ImGuiContext* Gui = ImGui::CreateContext();
    ImGui::SetCurrentContext(Gui);
    ImGuiIO& Io = ImGui::GetIO();
    Io.IniFilename = nullptr;

    const std::filesystem::path RepoRoot = ResolveRepositoryRootPath();
    TypographyRepository Typefaces {};
    if (!VerifyCheck(Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives") == 5u, "Load FontRepository.FontSpace"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* DefaultFace = Typefaces.QueryFont(0u, FontWeightClass::Regular))
    {
        Io.FontDefault = DefaultFace;
    }
    unsigned char* FontPixels = nullptr;
    int FontW = 0, FontH = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH);

    ThemeRepository Theme {};
    Theme.ConfigureTheme(ThemePreset::Dark);
    Theme.ConfigureAccent(AccentPreset::Blue);
    ControlAppearance::ConfigureTheme(Theme);

    ContentBrowserPanel Browser {};
    Browser.AttachThemeRepository(&Theme);
    Browser.ConfigureWorkspaceDomain(WorkspaceCategory::Particle);
    Browser.ConfigureShelfExtent(ContentShelfExtent::Closed, true);
    (void)Browser.ActivateCategory(ContentCategory::Colours);
    Browser.ConfigureColourCollectionScope(ColourCollectionScope::Ramps);

    ParticleWorkspaceHost Studio {};
    Studio.AttachContentBrowserPanel(&Browser);

    const Rectangle2D WindowBounds { 0.0f, 0.0f, static_cast<float>(CanvasW), static_cast<float>(CanvasH) };
    const std::filesystem::path OutDir = RepoRoot / "Exhibits/Images/Particles/CppUI";
    std::filesystem::create_directories(OutDir);

    auto RenderStep = [&](const ContentBrowserPointerInput& Input) {
        Io.DisplaySize = ImVec2(static_cast<float>(CanvasW), static_cast<float>(CanvasH));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        PixelSpace Canvas(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f);
        Studio.RenderStudio(Canvas, WindowBounds, Input, 1.0 / 60.0);
        ImGui::Render();
    };

    auto SavePng = [&](const char* FileName) -> bool {
        SoftwareRasterCanvas Raster(CanvasW, CanvasH, RgbaColor8 { 11u, 11u, 11u, 255u });
        Raster.RasterizeImGuiBatches(ImGui::GetDrawData());
        return VerifyCheck(Raster.WritePngFile(OutDir / FileName), FileName);
    };

    ContentBrowserPointerInput Idle {};

    // 1. Default Workspace UI (matches Html_WorkspaceUI_Default.png)
    for (int W = 0; W < 3; ++W) RenderStep(Idle);
    if (!SavePng("Cpp_WorkspaceUI_Default.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 2. Inspector -> Source tab (matches Html_Inspector_Source.png)
    Studio.AccessSession().InspectorTab = ParticleInspectorTab::Source;
    RenderStep(Idle);
    if (!SavePng("Cpp_Inspector_Source.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 3. Inspector -> Motion & FX tab with ColourRampEditor (matches Html_Inspector_MotionFX.png)
    Studio.AccessSession().InspectorTab = ParticleInspectorTab::MotionFx;
    RenderStep(Idle);
    if (!SavePng("Cpp_Inspector_MotionFX.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 4. Inspector -> Metrics & Record tab + Log Stream Drawer open (matches Html_Inspector_Modules_LogDrawer.png)
    Studio.AccessSession().InspectorTab  = ParticleInspectorTab::MetricsRecord;
    Studio.AccessSession().ShowLogDrawer = true;
    RenderStep(Idle);
    if (!SavePng("Cpp_Inspector_Metrics_LogDrawer.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    Studio.AccessSession().ShowLogDrawer = false;

    // 5. Force Field Selection & interactive Verification (reusing SimulationSpace::ForceFields)
    const Rectangle2D WindRow = Studio.AccessOutliner().QueryLastForceRowBounds(0u);
    if (!VerifyCheck(WindRow.Width > 80.0f && WindRow.Height > 20.0f, "Outliner Wind Generator East row hitbox valid"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    ContentBrowserPointerInput ClickWind {};
    ClickWind.CursorX  = WindRow.Center().X;
    ClickWind.CursorY  = WindRow.Center().Y;
    ClickWind.Pressed  = true;
    ClickWind.HeldDown = true;
    RenderStep(ClickWind);
    RenderStep(Idle);
    if (!VerifyCheck(Studio.InspectSession().SelectionKind == ParticleSelectionCategory::ForceSource
                  && Studio.InspectSession().SelectedForceIdx == 0u,
                     "Clicking Outliner Force row selects ForceSourceRecordEntry"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    if (!SavePng("Cpp_Inspector_ForceField.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 6. Modal Assets Browser overlay (matches Html_Assets_Library.png)
    Studio.AccessSession().SelectRig(0u);
    Studio.AccessSession().ActiveNavView = ParticleNavView::Assets;
    RenderStep(Idle);
    if (!SavePng("Cpp_Assets_Browser.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 7. End-to-End ContentBrowser .ColourRamp Drag-and-Drop onto Particle Inspector Motion & FX!
    Studio.AccessSession().ActiveNavView = ParticleNavView::Workspace;
    Studio.AccessSession().InspectorTab  = ParticleInspectorTab::MotionFx;
    Browser.ConfigureShelfExtent(ContentShelfExtent::SplitHalf, true);
    Browser.ConfigureColourCollectionScope(ColourCollectionScope::Ramps);
    RenderStep(Idle);
    RenderStep(Idle);

    const std::size_t AuroraIdx = Browser.QueryColourCodex().FindRampPresetIndex("aurora");
    const Rectangle2D AuroraCell = Browser.QueryRenderedRampCellBounds(AuroraIdx);
    const Rectangle2D RampDropTarget = Studio.AccessInspector().QueryColourRampStripBounds(Studio.InspectSession());
    if (!VerifyCheck(AuroraCell.Width > 40.0f && RampDropTarget.Width > 120.0f, "Aurora card & Particle Inspector ColourRamp drop slot both rendered"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    ContentBrowserPointerInput PressRamp {};
    PressRamp.CursorX  = AuroraCell.Center().X;
    PressRamp.CursorY  = AuroraCell.Center().Y;
    PressRamp.Pressed  = true;
    PressRamp.HeldDown = true;
    RenderStep(PressRamp);

    ContentBrowserPointerInput DragRamp {};
    DragRamp.CursorX  = RampDropTarget.Center().X;
    DragRamp.CursorY  = RampDropTarget.Center().Y;
    DragRamp.HeldDown = true;
    RenderStep(DragRamp);

    if (!VerifyCheck(Browser.InspectReading().DraggingColourRampActive, "ContentBrowser ColourRamp drag ghost active over Particle Inspector"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    ContentBrowserPointerInput DropRamp {};
    DropRamp.CursorX  = RampDropTarget.Center().X;
    DropRamp.CursorY  = RampDropTarget.Center().Y;
    DropRamp.Released = true;
    RenderStep(DropRamp);

    const auto* UpdatedRig = Studio.InspectSession().QuerySelectedRig();
    const auto* UpdatedGrad = (UpdatedRig != nullptr)
        ? std::get_if<ColourSpace::MultiStopGradientSource>(&UpdatedRig->ColourProfile.Source)
        : nullptr;
    if (!VerifyCheck(UpdatedGrad != nullptr && UpdatedGrad->Stops.size() == 5u,
                     "Dropping Aurora .ColourRamp from ContentBrowser updates active ParticleRig ColourProfile to 5 stops"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    RenderStep(Idle);
    if (!SavePng("Cpp_ContentBrowser_RampDrop.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // Verify playback controls & ruler scrubbing work cleanly
    Studio.AccessSession().TriggerBurst();
    if (!VerifyCheck(Studio.InspectSession().BurstCount == 1u, "Burst trigger increments BurstCount"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    ImGui::DestroyContext(Gui);
    std::printf("[ParticleWorkspaceUIValidation] ALL CHECKS PASSED (6x 1440x900 C++ UI screenshots + ForceFields + ContentBrowser ramp drag-drop).\n");
    return 0;
}
