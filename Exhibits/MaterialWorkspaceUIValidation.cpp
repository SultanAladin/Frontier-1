/*==========================================================================================================================================
                                                 MATERIALWORKSPACEUIVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 End-to-end UI validation & 1440x860 visual proof suite for native C++ MaterialWorkspaceHost (Project Zero Editor + Direct Studio OpenPBR)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Workspaces/Material/MaterialWorkspaceHost.h"
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
        std::fprintf(stderr, "[MaterialWorkspaceUIValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    constexpr uint32_t CanvasW = 1440u;
    constexpr uint32_t CanvasH = 860u;

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

    MaterialWorkspaceHost Studio {};
    const Rectangle2D WindowBounds { 0.0f, 0.0f, static_cast<float>(CanvasW), static_cast<float>(CanvasH) };
    const std::filesystem::path OutDir = RepoRoot / "Exhibits/Images/Materials";
    std::filesystem::create_directories(OutDir);

    auto RenderStep = [&](const ContentBrowserPointerInput& Input) {
        Io.DisplaySize = ImVec2(static_cast<float>(CanvasW), static_cast<float>(CanvasH));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        PixelSpace Canvas(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f);
        Studio.RenderStudio(Canvas, WindowBounds, Input, 0.0);
        ImGui::Render();
    };

    auto SavePng = [&](const char* FileName) -> bool {
        SoftwareRasterCanvas Raster(CanvasW, CanvasH, RgbaColor8 { 11u, 11u, 11u, 255u });
        Raster.RasterizeImGuiBatches(ImGui::GetDrawData());
        return VerifyCheck(Raster.WritePngFile(OutDir / FileName), FileName);
    };

    ContentBrowserPointerInput Idle {};

    // 1. Default Workspace UI: Volcanic Pahoehoe Magma on Shaderball (Direct Studio OpenPBR, No GI)
    Studio.AccessSession().SelectPresetIdentity(MaterialSpace::Dynamic::DynamicPresetIdentity::VolcanicPahoehoeMagma);
    Studio.AccessSession().PlaybackClockSeconds = 1.45;
    for (int W = 0; W < 2; ++W) RenderStep(Idle);
    if (!SavePng("CppMaterialWorkspace_UI_Volcanic.png")) { ImGui::DestroyContext(Gui); return 1; }

    // 2. Switch to Hexagonal Energy Barrier + click surface to trigger a live 3D impact shockwave!
    Studio.AccessSession().ActiveTabIndex = 1u;
    Studio.AccessSession().SelectPresetIdentity(MaterialSpace::Dynamic::DynamicPresetIdentity::HexagonalEnergyBarrier);
    Studio.AccessSession().TriggerViewportImpactPulse({ 0.25f, 0.15f, 0.95f });
    for (int W = 0; W < 2; ++W) RenderStep(Idle);
    if (!SavePng("CppMaterialWorkspace_UI_Shield.png")) { ImGui::DestroyContext(Gui); return 1; }

    // 3. Switch to Surface Material Transition (Brushed Titanium <-> Liquid Obsidian) + add layer & bake button check
    Studio.AccessSession().ActiveTabIndex = 2u;
    Studio.AccessSession().SelectPresetIdentity(MaterialSpace::Dynamic::DynamicPresetIdentity::SurfaceMaterialTransition);
    Studio.AccessSession().AccessActiveRecipe().Parameters.TransitionProgress = 0.50f;
    Studio.AccessSession().AccessActiveRecipe().Parameters.PrimaryDriverOrdinal =
        static_cast<uint32_t>(MaterialSpace::Dynamic::ParameterDriverMode::ExternalTimeline);
    Studio.AccessSession().DirtyPreviewRequested = true;
    for (int W = 0; W < 2; ++W) RenderStep(Idle);
    if (!SavePng("CppMaterialWorkspace_UI_Transition.png")) { ImGui::DestroyContext(Gui); return 1; }

    // Verify layer stack interaction & static snapshot trigger
    const std::size_t BeforeLayers = Studio.InspectSession().InspectActiveRecipe().LayerStack.Layers.size();
    Studio.AccessSession().AppendProceduralLayer();
    if (!VerifyCheck(Studio.InspectSession().InspectActiveRecipe().LayerStack.Layers.size() == BeforeLayers + 1u, "AppendProceduralLayer increments stack"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    Studio.AccessSession().RequestChannelSnapshotBake();
    if (!VerifyCheck(Studio.InspectSession().ChannelBakeCompleted, "RequestChannelSnapshotBake flags completion"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    ImGui::DestroyContext(Gui);
    std::printf("[MaterialWorkspaceUIValidation] ALL CHECKS PASSED\n");
    return 0;
}
