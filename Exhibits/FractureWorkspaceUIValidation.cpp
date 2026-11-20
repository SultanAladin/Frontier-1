/*============================================================================================================================================
                                                    FRACTUREWORKSPACEUIVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end UI validation & 1440x900 visual snapshot suite for Port-8091 Project Zero Fracture Editor

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Workspaces/Fracture/FractureWorkspaceHost.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cstdio>
#include <filesystem>

namespace
{

using namespace Frontier;
using namespace Frontier::Workspaces::Fracture;
using Frontier::Components::RgbaColor8;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;

bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[FractureWorkspaceUIValidation] FAILED: %s\n", Message);
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
    Browser.ConfigureWorkspaceDomain(WorkspaceCategory::Fracture);
    Browser.ConfigureShelfExtent(ContentShelfExtent::Closed, true);

    FractureWorkspaceHost Workspace {};
    Workspace.AttachContentBrowserPanel(&Browser);

    const Rectangle2D WindowBounds { 0.0f, 0.0f, static_cast<float>(CanvasW), static_cast<float>(CanvasH) };
    const std::filesystem::path OutDir = RepoRoot / "Exhibits/Images/Fracture/CppUI";
    std::filesystem::create_directories(OutDir);

    auto RenderStep = [&](const ContentBrowserPointerInput& Input) {
        Io.DisplaySize = ImVec2(static_cast<float>(CanvasW), static_cast<float>(CanvasH));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        PixelSpace Canvas(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f);
        Workspace.RenderStudio(Canvas, WindowBounds, Input, 1.0 / 60.0);
        ImGui::Render();
    };

    auto SavePng = [&](const char* FileName) -> bool {
        SoftwareRasterCanvas Raster(CanvasW, CanvasH, RgbaColor8 { 9u, 9u, 9u, 255u });
        Raster.RasterizeImGuiBatches(ImGui::GetDrawData());
        return VerifyCheck(Raster.WritePngFile(OutDir / FileName), FileName);
    };

    ContentBrowserPointerInput Idle {};

    // 1. Default Port-8091 Edit Torus (Material shading, 3-dock Project Zero layout)
    for (int W = 0; W < 3; ++W) RenderStep(Idle);
    if (!VerifyCheck(Workspace.InspectSession().HasFractured, "Default primitive fractured"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    if (!VerifyCheck(Workspace.InspectSession().LastDrawnTriangles > 40u, "Viewport rendered 3D shards"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    if (!SavePng("FractureWorkspace_EditTorus.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 2. Energy Heatmap + Energy LOG Legend + Diagnostics 4-Channel Overlay
    Workspace.AccessSession().RenderOptions.RenderMode    = FractureRenderViewMode::Energy;
    Workspace.AccessSession().DiagnosticsSession.Visible  = true;
    Workspace.AccessSession().RenderOptions.ExplodeFactor = 0.25f;
    RenderStep(Idle);
    if (!SavePng("FractureWorkspace_EnergyDiag.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 3. Material OpenPBR Shading + Explode + Granite Interior Dual-Material
    Workspace.AccessSession().DiagnosticsSession.Visible  = false;
    Workspace.AccessSession().RenderOptions.RenderMode    = FractureRenderViewMode::StudioOpenPBR;
    Workspace.AccessSession().RenderOptions.ExplodeFactor = 0.18f;
    Workspace.AccessSession().MaterialId                  = "granite";
    Workspace.AccessSession().RebuildFracturePartition();
    RenderStep(Idle);
    if (!SavePng("FractureWorkspace_MaterialOpenPBR.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 4. JoltPhysics Simulate Mode (CollisionMesh exact compound shards) + Cells Mode + 60px Timeline + Kinetic Hero Curve
    Workspace.AccessSession().MaterialId                  = "concrete";
    Workspace.AccessSession().CollisionShapeMode          = Frontier::SimulationSpace::PhysicsSpace::FractureCollisionShapeMode::CollisionMesh;
    Workspace.AccessSession().RenderOptions.ExplodeFactor = 0.0f;
    Workspace.AccessSession().RebuildFracturePartition();
    Workspace.AccessSession().RenderOptions.RenderMode    = FractureRenderViewMode::Cells;
    Workspace.AccessSession().TransitionExecutionPhase(ExecutionStripPhase::Simulate);
    for (int F = 0; F < 28; ++F) RenderStep(Idle);
    if (!VerifyCheck(Workspace.InspectSession().HasSimulatedData, "JoltPhysics simulation active"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    const auto SimTel = Workspace.InspectSession().SimRunner.InspectTelemetry();
    if (!VerifyCheck(SimTel.CompletedSteps > 0ull, "Jolt stepped frames") ||
        !VerifyCheck(SimTel.CollisionMode == Frontier::SimulationSpace::PhysicsSpace::FractureCollisionShapeMode::CollisionMesh, "CollisionMesh mode active") ||
        !VerifyCheck(SimTel.TotalSubShapes > SimTel.FragmentCount, "Torus shards rejoined across seams into compound sub-shapes"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    if (!SavePng("FractureWorkspace_SimTorus.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 5. Debug Dropdown + 3D Collision Shape Debug Overlay (Exact CollisionMesh sub-hulls + Ground collider)
    Workspace.AccessSession().RenderOptions.RenderMode          = FractureRenderViewMode::StudioOpenPBR;
    Workspace.AccessSession().RenderOptions.ShowCollisionShapes = true;
    Workspace.AccessSession().DebugDropdownOpen                 = true;
    RenderStep(Idle);
    if (!SavePng("FractureWorkspace_CollisionDebug.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    Workspace.AccessSession().DebugDropdownOpen                 = false;

    // Verify ConvexHull collision mode + debug overlay also loads & steps cleanly
    Workspace.AccessSession().CollisionShapeMode = Frontier::SimulationSpace::PhysicsSpace::FractureCollisionShapeMode::ConvexHull;
    Workspace.AccessSession().TransitionExecutionPhase(ExecutionStripPhase::Edit);
    Workspace.AccessSession().RebuildFracturePartition();
    Workspace.AccessSession().TransitionExecutionPhase(ExecutionStripPhase::Simulate);
    RenderStep(Idle);
    if (!VerifyCheck(Workspace.InspectSession().SimRunner.InspectTelemetry().CollisionMode == Frontier::SimulationSpace::PhysicsSpace::FractureCollisionShapeMode::ConvexHull, "ConvexHull mode active"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    Workspace.AccessSession().RenderOptions.ShowCollisionShapes = false;

    // Verify all 6 fracture solvers on Torus produce watertight organic fragments
    Workspace.AccessSession().TransitionExecutionPhase(ExecutionStripPhase::Edit);
    for (uint32_t S = 0u; S < 6u; ++S)
    {
        Workspace.AccessSession().ApplySolverMethod(static_cast<Frontier::GeometrySpace::Fracture::FractureSolverMethod>(S));
        if (!VerifyCheck(Workspace.InspectSession().PartitionResult.Fragments.size() >= 4u, "Torus solver produced fragments") ||
            !VerifyCheck(Workspace.InspectSession().PartitionResult.Receipt.WatertightManifold, "Torus solver watertight conservation"))
        {
            ImGui::DestroyContext(Gui);
            return 1;
        }
    }

    // Verify all 9 analytical primitives switch cleanly with watertight conservation
    Workspace.AccessSession().TransitionExecutionPhase(ExecutionStripPhase::Edit);
    for (uint32_t P = 0u; P < FractureWorkspaceSession::PrimitiveCount; ++P)
    {
        Workspace.AccessSession().ApplyPrimitiveShape(static_cast<FractureIcons::PrimitiveShapeId>(P));
        if (!VerifyCheck(Workspace.InspectSession().PartitionResult.Receipt.WatertightManifold, "Primitive watertight conservation"))
        {
            ImGui::DestroyContext(Gui);
            return 1;
        }
        if (!VerifyCheck(Workspace.InspectSession().PartitionResult.Fragments.size() >= 4u, "Primitive produced fragments"))
        {
            ImGui::DestroyContext(Gui);
            return 1;
        }
    }

    ImGui::DestroyContext(Gui);
    std::printf("[FractureWorkspaceUIValidation] ALL CHECKS PASSED (5 PNG snapshots saved + 9 primitives verified).\n");
    return 0;
}
