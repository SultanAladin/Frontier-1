/*==========================================================================================================================================
                                                   BAKINGWORKSPACEUIVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Domain & ImGui UI exhibit for Baking Studio + ControlIcon 0..85 verification (3-column layout, 3D + 2D SDF split & Quick Menu).

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/Workspaces/Texturing/BakingWorkspaceHost.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <filesystem>
#include <string_view>

namespace
{

[[nodiscard]] bool VerifyCondition(bool Condition, const char* Description) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[BakingWorkspaceUIValidation] FAIL: %s\n", Description);
        return false;
    }
    return true;
}

[[nodiscard]] bool CaptureBakingWorkspacePng(
    ImGuiIO&                                              Io,
    Frontier::Components::HeadlessWorkspaceDockContainer& Dock,
    Frontier::ControlCentrePanel&                         ControlCentre,
    const std::filesystem::path&                          TargetPngPath,
    uint32_t                                              WidthPx  = 1600u,
    uint32_t                                              HeightPx = 980u)
{
    for (int Tick = 0; Tick < 3; ++Tick)
    {
        Io.DisplaySize = ImVec2(static_cast<float>(WidthPx), static_cast<float>(HeightPx));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        Dock.PresentWindowImGuiPanes(Frontier::Components::HostWindowToken{ 0u });
        ControlCentre.Render({ 0.0f, 0.0f, static_cast<float>(WidthPx), static_cast<float>(HeightPx) }, 1.0 / 60.0, 60.0f);
        ImGui::Render();
    }

    Frontier::Exhibits::SoftwareRasterCanvas Canvas(WidthPx, HeightPx, { 16, 17, 20, 255 });
    Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
    return VerifyCondition(Canvas.WritePngFile(TargetPngPath), TargetPngPath.filename().string().c_str());
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::Configuration;
    using namespace Frontier::Workspaces;

    const std::filesystem::path RepoRoot = Exhibits::ResolveRepositoryRootPath();
    const std::filesystem::path ImageDir = RepoRoot / "Exhibits/Images/Texturing";
    std::filesystem::create_directories(ImageDir);

    // 1. Verify ALL 86 ControlIcon SVG entries (0..85 including EraserTool..PinAnchor) flatten cleanly
    for (uint8_t Idx = 0u; Idx < static_cast<uint8_t>(ControlIcon::Count); ++Idx)
    {
        const ControlIcon      Icon     = static_cast<ControlIcon>(Idx);
        const VectorIconEntry& Entry    = VectorIconRepository::QueryControl(Icon);
        const auto             Contours = VectorIconRepository::FlattenSvgPath(Entry.SvgPath);
        if (!VerifyCondition(!Contours.empty(), Entry.Name.data()))
        {
            return 1;
        }
    }

    // 2. Validate BakingWorkspaceSession domain state & operations
    {
        BakingWorkspaceHost Host {};
        auto& Sess = Host.AccessSession();
        if (!VerifyCondition(Sess.Study == BakerStudyShape::UtahTeapot, "Default study is UtahTeapot") ||
            !VerifyCondition(Sess.TargetResolutionPx == 512u, "Default bake resolution is 512px") ||
            !VerifyCondition(Sess.AoSampleCount == 12, "Default AO ray count is 12") ||
            !VerifyCondition(Sess.VolumeGridDim == 36u, "Default Volume SDF grid dim is 36") ||
            !VerifyCondition(Sess.CountSelectedTargets() == 5u, "Default PBR + Volume preset selects 5 targets") ||
            !VerifyCondition(Sess.InspectedTarget == BakerTargetId::TangentNormal, "Default inspected target is TangentNormal"))
        {
            return 1;
        }

        Sess.ApplyQuickPreset(BakerQuickPreset::PbrStandard);
        if (!VerifyCondition(Sess.CountSelectedTargets() == 4u && Sess.QueryPresetMatched(BakerQuickPreset::PbrStandard), "PbrStandard preset selects 4 targets"))
        {
            return 1;
        }
        Sess.ApplyQuickPreset(BakerQuickPreset::SelectAll);
        if (!VerifyCondition(Sess.CountSelectedTargets() == 18u, "SelectAll selects all 18 targets"))
        {
            return 1;
        }
        Sess.ApplyQuickPreset(BakerQuickPreset::PbrPlusVolume);
        Sess.InspectTarget(BakerTargetId::VolumeSdf);
        Sess.ExecuteBakeSimulation();
        if (!VerifyCondition(Sess.SurfaceBaked && Sess.VolumeSdfBaked, Sess.ProgressStatus.c_str()) ||
            !VerifyCondition(Sess.QueryTargetReady(BakerTargetId::VolumeSdf), "VolumeSdf ready after bake"))
        {
            return 1;
        }

        Sess.SendBakedTargetsToPaint();
        if (!VerifyCondition(Sess.BridgedToPaint, "SendBakedTargetsToPaint activates bridge"))
        {
            return 1;
        }

        Sess.SelectStudy(BakerStudyShape::ShaderBall);
        if (!VerifyCondition(!Sess.SurfaceBaked && !Sess.VolumeSdfBaked, "Study change clears previous baked buffers"))
        {
            return 1;
        }
    }

    // 3. Initialize Typography + ImGui runtime
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

    // 4. Mount 3-Column Baking Studio Dock & Capture Exhibits 09..11
    {
        BakingWorkspaceHost BakingHost {};
        BakingHost.ConfigureLaunchPolicy(EngineConfiguration(EngineConfiguration::ConstructFeatureSetDefault()));

        Components::HeadlessWorkspaceDockContainer Dock {};
        const auto WinDesc = Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier Baking Studio", 1600, 980);
        if (!VerifyCondition(Dock.Initialize(WinDesc), "Initialized HeadlessWorkspaceDockContainer for Baking Studio"))
        {
            return 1;
        }
        BakingHost.RegisterDockPanes(Dock);

        ThemeRepository Theme {};
        Theme.ConfigureTheme(ThemePreset::Dark);
        Components::ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), Theme);

        ControlCentrePanel ControlCentre {};
        ControlCentre.AttachTypefaces(&Typefaces);
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        ControlCentre.ApplyDraftAppearanceImmediate();
        ControlCentre.AccessNotificationStream().ClearPending();

        // Exhibit 09: Main Baking Workspace (matches 05-HTML-baking.png)
        BakingHost.AccessSession().InspectTarget(BakerTargetId::BentNormal);
        if (!CaptureBakingWorkspacePng(Io, Dock, ControlCentre, ImageDir / "09-BakingWorkspaceMain.png"))
        {
            return 1;
        }

        // Verify Left -> Center -> Right 3-column dock alignment
        {
            ImGuiWindow* TargetsWin   = nullptr;
            ImGuiWindow* ViewportWin  = nullptr;
            ImGuiWindow* InspectorWin = nullptr;
            if (ImGuiContext* Ctx = ImGui::GetCurrentContext())
            {
                for (ImGuiWindow* Win : Ctx->Windows)
                {
                    if (Win != nullptr && Win->Active)
                    {
                        const std::string_view Name(Win->Name);
                        if (Name.starts_with("Bake Map Targets##")) TargetsWin = Win;
                        else if (Name.starts_with("Baking##")) ViewportWin = Win;
                        else if (Name.starts_with("Baker Parameters##")) InspectorWin = Win;
                    }
                }
            }
            if (!VerifyCondition(TargetsWin != nullptr && ViewportWin != nullptr && InspectorWin != nullptr, "All 3 Baking columns mounted") ||
                !VerifyCondition(TargetsWin->Pos.x + TargetsWin->Size.x <= ViewportWin->Pos.x + 2.0f && ViewportWin->Pos.x + ViewportWin->Size.x <= InspectorWin->Pos.x + 2.0f, "Left -> Center -> Right 3-column alignment verified"))
            {
                return 1;
            }
        }

        // Exhibit 10: Baked 3D + 2D Split inspecting Volume SDF + Inspector scrolled to Volume SDF & Export Bridge
        BakingHost.AccessSession().ExecuteBakeSimulation();
        BakingHost.AccessSession().ViewportLayout  = BakerViewportLayout::SplitView;
        BakingHost.AccessSession().InspectedTarget = BakerTargetId::VolumeSdf;
        BakingHost.AccessTargetsPane().AssignScrollOffsetY(120.0f);
        BakingHost.AccessInspectorPane().AssignScrollOffsetY(520.0f);
        if (!CaptureBakingWorkspacePng(Io, Dock, ControlCentre, ImageDir / "10-BakingWorkspaceSplitSdf.png"))
        {
            return 1;
        }

        // Exhibit 11: Baking Quick Menu overlay open (matches 06-HTML-baking-details.png)
        BakingHost.AccessTargetsPane().AssignScrollOffsetY(0.0f);
        BakingHost.AccessInspectorPane().AssignScrollOffsetY(0.0f);
        BakingHost.AccessSession().ViewportLayout = BakerViewportLayout::ThreeDOnly;
        BakingHost.AccessViewportPane().ConfigureQuickOverlayOpen(true);
        if (!CaptureBakingWorkspacePng(Io, Dock, ControlCentre, ImageDir / "11-BakingWorkspaceQuickMenu.png"))
        {
            return 1;
        }

        Dock.Finalize();
    }

    ImGui::DestroyContext(GuiCtx);
    TypographyRepository::AssignCurrent(nullptr);
    std::printf("[BakingWorkspaceUIValidation] ALL CHECKS PASSED\n");
    return 0;
}
