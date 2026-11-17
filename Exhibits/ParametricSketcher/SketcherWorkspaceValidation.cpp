/*============================================================================================================================================
                                                    SKETCHERWORKSPACEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end validation of SketcherWorkspaceHost (docked Outliner/Viewport/DrawingSheet/Construct/Inspector + Vulkan B-rep + SVG icons + ProjectBrowser capability)

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherIcons.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherWorkspaceHost.h"
#include "Engine/Workspaces/WorkspaceCapabilityRegister.h"

#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

#ifndef FRONTIER_COMPILED_SHADER_DIR
#define FRONTIER_COMPILED_SHADER_DIR "CompiledShaders"
#endif

namespace
{

using namespace Frontier;
using namespace Frontier::Workspaces;
using namespace Frontier::Workspaces::Sketcher;

bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[SketcherWorkspaceValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

std::filesystem::path ResolveShaderDir(const std::filesystem::path& RepoRoot)
{
    if (const char* Env = std::getenv("FRONTIER_COMPILED_SHADER_DIR"))
    {
        if (*Env != '\0' && std::filesystem::exists(std::filesystem::path(Env) / "SketcherSurface.frag.spv"))
        {
            return std::filesystem::path(Env);
        }
    }
    if (std::filesystem::exists(std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR) / "SketcherSurface.frag.spv"))
    {
        return std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
    }
    return RepoRoot / "Build/CompiledShaders";
}

} // namespace

int main()
{
    uint32_t Passed = 0u;
    const std::filesystem::path RepoRoot = std::filesystem::path(FRONTIER_REPO_ROOT_DIR);

    // 1. Verify WorkspaceCapabilityRegister routes .solidarc and .step files to SketcherWorkspace first
    {
        WorkspaceCapabilityRegister Register {};
        const auto SolidArcCaps = Register.QueryPermittedForPath("Bracket.solidarc");
        const auto StepCaps     = Register.QueryPermittedForPath("Housing.step");
        if (!VerifyCheck(!SolidArcCaps.empty() && SolidArcCaps.front().Target == HostWorkspaceTarget::SketcherWorkspace,
                         ".solidarc routes to SketcherWorkspace") ||
            !VerifyCheck(!StepCaps.empty() && StepCaps.front().Target == HostWorkspaceTarget::SketcherWorkspace,
                         ".step routes to SketcherWorkspace"))
        {
            return 1;
        }
        ++Passed;
    }

    // 2. Verify all 66 SVG icons exist on disk
    {
        size_t SvgCount = 0u;
        const std::filesystem::path IconDir = RepoRoot / "EngineContent/Icons/Sketcher";
        if (std::filesystem::exists(IconDir))
        {
            for (const auto& Entry : std::filesystem::directory_iterator(IconDir))
            {
                if (Entry.is_regular_file() && Entry.path().extension() == ".svg")
                {
                    ++SvgCount;
                }
            }
        }
        if (!VerifyCheck(SvgCount == static_cast<size_t>(IconSymbol::Count), "All 66 Sketcher SVG icons present"))
        {
            return 1;
        }
        ++Passed;
    }

    // 3. Initialize VulkanInterchange + ImGui + HeadlessWorkspaceDockContainer + SketcherWorkspaceHost
    GeometricRaster::VulkanInterchange Device {};
    std::string VkDiag {};
    if (!VerifyCheck(Device.InitializeInterchange(RepoRoot, &VkDiag), "VulkanInterchange initialization"))
    {
        std::fprintf(stderr, "Vulkan diag: %s\n", VkDiag.c_str());
        return 1;
    }

    ImGuiContext* Gui = ImGui::CreateContext();
    ImGui::SetCurrentContext(Gui);
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    Io.DisplaySize = ImVec2(1440.0f, 900.0f);
    Io.DeltaTime   = 1.0f / 60.0f;
    Io.IniFilename = nullptr;

    TypographyRepository Typefaces {};
    (void)Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives");
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

    Components::HeadlessWorkspaceDockContainer Dock {};
    if (!VerifyCheck(Dock.Initialize(Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow("Sketcher Validation", 1440u, 900u)),
                     "HeadlessWorkspaceDockContainer initialization"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    Components::ConfigureImGuiWorkspaceStyle(Dock.ActiveTabMetrics());
    Components::ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), Theme);

    SketcherWorkspaceHost Host {};
    std::string HostDiag {};
    const std::filesystem::path ShaderDir = ResolveShaderDir(RepoRoot);
    if (!VerifyCheck(Host.Initialize(&Device, ShaderDir, RepoRoot / "EngineContent/Icons", {}, &HostDiag),
                     "SketcherWorkspaceHost initialization"))
    {
        std::fprintf(stderr, "Host diag: %s\n", HostDiag.c_str());
        Dock.Finalize();
        ImGui::DestroyContext(Gui);
        return 1;
    }
    Host.RegisterDockPanes(Dock);
    if (!VerifyCheck(Host.QueryRegisteredTabCount() == 5u, "5 dockable Sketcher panes registered"))
    {
        Host.Finalize();
        Dock.Finalize();
        ImGui::DestroyContext(Gui);
        return 1;
    }
    ++Passed;

    // 4. Build a Block - Cylinder cut solid inside the host document and step 2 docked ImGui frames
    {
        auto& Doc = Host.AccessState().Document;
        SketcherConstructRequest BoxReq {};
        BoxReq.Kind     = SketcherConstructKind::Block;
        BoxReq.Position = { -1.2, -0.9, -0.6 };
        BoxReq.Width    = 2.4;
        BoxReq.Depth    = 1.8;
        BoxReq.Height   = 1.2;
        if (!VerifyCheck(ConstructSketcherPrimitive(Doc, BoxReq).AcceptedCondition, "Construct Block in host"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }

        SketcherDrawingRequest CircleReq {};
        CircleReq.Kind            = SketcherDrawingKind::Circle;
        CircleReq.GridPlane       = 0;
        CircleReq.CentreCondition = true;
        CircleReq.FillCondition   = true;
        CircleReq.Points          = { { 0.0, 0.0 }, { 0.45, 0.0 } };
        if (!VerifyCheck(DrawSketcherSketch(Doc, CircleReq).AcceptedCondition, "Draw Circle sketch in host"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }
        const std::string SketchLabel = Doc.Tree.Steps.back().Label;
        if (!VerifyCheck(ExtrudeSketcherSketch(Doc, SketchLabel, 3.0, GeometrySpace::Parametric::FeatureCombination::Difference).AcceptedCondition,
                         "Extrude Difference cut in host"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }

        RefreshSketcherScene(Host.AccessState(), false);
        ConfigureSketcherStartupViews(Host.AccessState());
    }

    for (int FrameIdx = 0; FrameIdx < 2; ++FrameIdx)
    {
        ImGui::NewFrame();
        Host.BeginFrame(1.0 / 60.0);
        Dock.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
        Host.EndFrame(1.0 / 60.0, false);
        ImGui::Render();
    }

    const auto& Scene = DisplayedSketcherScene(Host.InspectState());
    const ImDrawData* DrawData = ImGui::GetDrawData();
    if (!VerifyCheck(!Scene.Faces.empty() && !Scene.Edges.empty(), "Host scene populated with B-rep faces and edges") ||
        !VerifyCheck(DrawData != nullptr && DrawData->CmdListsCount > 0, "Docked ImGui draw lists generated"))
    {
        Host.Finalize();
        Dock.Finalize();
        ImGui::DestroyContext(Gui);
        return 1;
    }
    ++Passed;

    Host.Finalize();
    Dock.Finalize();
    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(Gui);

    std::printf("[SketcherWorkspaceValidation] Passed=%u Failed=0 (Tabs=%u Faces=%zu Edges=%zu)\n",
                Passed, 5u, Scene.Faces.size(), Scene.Edges.size());
    return 0;
}
