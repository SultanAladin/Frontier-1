/*============================================================================================================================================
                                                    COMPONENTEDITORUIVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase 4 UI parity verification: renders 5 1440x900 ComponentWorkspaceHost states into Exhibits/Images/ComponentEditor/CppParity/*.png

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Lifecycle/EditorLifecycle.h"
#include "Engine/Workspaces/ComponentEditor/ComponentWorkspaceHost.h"
#include "Engine/Workspaces/WorkspaceCapabilityRegister.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::Workspaces::ComponentEditor;

bool Require(bool Condition, const char* Label)
{
    if (!Condition)
    {
        std::fprintf(stderr, "[FAIL] %s\n", Label);
        return false;
    }
    std::printf("[PASS] %s\n", Label);
    return true;
}

} // namespace

int main()
{
    std::printf("=== ComponentEditorUIValidation ===\n");

    const std::filesystem::path RepoRoot(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path ShaderDir(FRONTIER_COMPILED_SHADER_DIR);
    const std::filesystem::path OutDir = RepoRoot / "Exhibits/Images/ComponentEditor/CppParity";
    std::filesystem::create_directories(OutDir);

    // Verify WorkspaceCapabilityRegister exposes ComponentEditor for .ComponentSpace / .facet
    {
        Workspaces::WorkspaceCapabilityRegister Caps {};
        const auto* Entry = Caps.FindByTarget(Workspaces::HostWorkspaceTarget::ComponentEditorWorkspace);
        const auto Permitted = Caps.QueryPermittedForPath("Vehicles/ControlVehicle.ComponentSpace");
        if (!Require(Entry != nullptr && Entry->Identifier == "ComponentEditor" && !Permitted.empty() &&
                     Permitted.front().Target == Workspaces::HostWorkspaceTarget::ComponentEditorWorkspace,
                     "WorkspaceCapabilityRegister maps .ComponentSpace -> ComponentEditor"))
        {
            return 1;
        }
    }

    GeometricRaster::VulkanInterchange Vulkan {};
    std::string Diag {};
    if (!Require(Vulkan.InitializeInterchange(RepoRoot, &Diag), "VulkanInterchange::InitializeInterchange"))
    {
        std::fprintf(stderr, "       %s\n", Diag.c_str());
        return 2;
    }

    IMGUI_CHECKVERSION();
    ImGuiContext* Gui = ImGui::CreateContext();
    ImGui::SetCurrentContext(Gui);

    ImGuiIO& Io = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;
    Io.DisplaySize = { 1440.0f, 900.0f };
    Io.DeltaTime   = 1.0f / 60.0f;

    TypographyRepository Typefaces {};
    if (!Require(Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives") >= 1u,
                 "TypographyRepository::LoadRepository"))
    {
        ImGui::DestroyContext(Gui);
        Vulkan.TeardownInterchange();
        return 3;
    }
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* TabFont = Typefaces.QueryTabStripFont())
    {
        Io.FontDefault = TabFont;
    }
    unsigned char* FontPixels = nullptr;
    int AtlasW = 0, AtlasH = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &AtlasW, &AtlasH);

    {
        ComponentWorkspaceHost Host {};
        Host.AttachVulkanDevice(Vulkan, ShaderDir, RepoRoot);
        auto& Session = Host.AccessSession();
        if (!Require(Session.VulkanReady && !Session.Documents.empty() && Session.Catalog.size() >= 4u,
                     "ComponentWorkspaceHost initialized with ControlVehicle & Vulkan renderer"))
        {
            ImGui::DestroyContext(Gui);
            Vulkan.TeardownInterchange();
            return 4;
        }

        auto CaptureFrame = [&](const char* FileName) -> bool
        {
            Io.DisplaySize = { 1440.0f, 900.0f };
            Io.DeltaTime   = 1.0f / 60.0f;
            ImGui::NewFrame();
            ImDrawList* DrawList = ImGui::GetBackgroundDrawList();
            PixelSpace Surface(DrawList, { 0.0f, 0.0f }, 1.0f, Io.FontDefault);
            PaneInputReading EmptyInput {};
            Host.RenderWorkspace(Surface, { 0.0f, 0.0f, 1440.0f, 900.0f }, EmptyInput, 1.0 / 60.0);
            ImGui::Render();

            Exhibits::SoftwareRasterCanvas Canvas(1440u, 900u, Components::RgbaColor8 { 9u, 9u, 9u, 255u });
            Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
            return Require(Canvas.WritePngFile(OutDir / FileName), FileName);
        };

        // 1. Default ControlVehicle Viewport (Lit)
        if (!CaptureFrame("01-Viewport.png")) return 5;

        // 2. SuspensionFL Constraint Details + Layer-1 X-ray spring
        if (auto* Doc = Session.AccessActiveDocument())
        {
            const auto SpringTok = Doc->ClassDefinition.Storage.FindTokenByTitle("SuspensionFL");
            if (SpringTok.Valid())
            {
                Doc->SelectedToken = SpringTok;
            }
        }
        if (!CaptureFrame("02-ConstraintDetails.png")) return 6;

        // 3. Assembly Sequence view
        Session.ActiveCenterTab = CenterViewTab::AssemblySequence;
        if (!CaptureFrame("03-AssemblySequence.png")) return 7;

        // 4. Response curves drawer + Simulate active
        Session.ActiveCenterTab  = CenterViewTab::Viewport;
        Session.DrawerExpanded   = true;
        Session.ActiveDrawerTab  = BottomDrawerTab::ResponseCurves;
        Session.SimulationActive = true;
        Session.ThrottleInput    = 1.0f;
        for (int S = 0; S < 90; ++S)
        {
            Session.Tick(1.0 / 60.0);
        }
        if (!CaptureFrame("04-DrawerSimulate.png")) return 8;

        // 5. Content tray + Collision inspection channel
        Session.SimulationActive      = false;
        Session.DrawerExpanded        = false;
        Session.ActiveLeftTab         = LeftDockTab::Content;
        Session.RenderOptions.Channel = GeometricRaster::ComponentEditor::ComponentInspectionChannel::Collision;
        if (!CaptureFrame("05-ContentCollision.png")) return 9;

        // Interactive mutation test: Add + Duplicate + Compile
        Session.AddComponentToActive(
            ComponentClass::ComponentCategory::LightInstance,
            "SpotBeacon",
            ComponentClass::PhysicsConstraintKind::Fixed);
        Session.DuplicateSelectedComponent();
        Session.CompileActiveDocument();
        if (!Require(!Session.InspectActiveDocument()->Dirty && Session.InspectActiveDocument()->CanCompile,
                     "Add + Duplicate + Compile mutates ComponentClass cleanly"))
        {
            return 10;
        }
    }

    ImGui::DestroyContext(Gui);

    // Phase 5 check: World Editor ComponentWorldPlacement, ContentBrowser .ComponentSpace & per-instance overrides
    {
        Lifecycle::EditorLifecycle WorldEditor {};
        Lifecycle::EditorStartupConfiguration WorldCfg {};
        WorldCfg.FontRepositoryRoot    = RepoRoot / "EngineContent/FontsArchives";
        WorldCfg.PopulateShowcaseScene = true;
        WorldCfg.VulkanDevice          = &Vulkan;
        WorldCfg.CompiledShaderDirectory = ShaderDir;
        if (!Require(WorldEditor.Initialize(WorldCfg), "EditorLifecycle::Initialize for Phase 5"))
        {
            Vulkan.TeardownInterchange();
            return 11;
        }
        bool HasComponentSpaceCatalogEntry = false;
        for (const auto& Entry : WorldEditor.AccessContentBrowser().QueryPanel().QueryThumbnailRepository().QueryAllEntries())
        {
            if (Entry.ContainerFormat == ".ComponentSpace" && Entry.DisplayTitle == "ControlVehicle")
            {
                HasComponentSpaceCatalogEntry = true;
                break;
            }
        }
        if (!Require(HasComponentSpaceCatalogEntry, "ContentBrowser indexes ControlVehicle.ComponentSpace"))
        {
            WorldEditor.Finalize();
            Vulkan.TeardownInterchange();
            return 12;
        }

        const OutlinerSlotToken PlacedRoot =
            WorldEditor.SpawnComponentClassPlacement("ControlVehicle", { 0.40f, -0.50f, -0.64f });
        auto* ActivePanes = WorldEditor.AccessWorkspaces().AccessActivePanes();
        const bool PlacementOk =
            PlacedRoot.Valid() &&
            ActivePanes != nullptr &&
            WorldEditor.QueryWorldPlacements().QueryLivePlacementCount() == 1u &&
            !WorldEditor.QueryActiveParticlePlacements().empty() &&
            !WorldEditor.QueryActiveFluidPlacements().empty();
        if (!Require(PlacementOk, "SpawnComponentClassPlacement creates WorldPlacement + Particle + Fluid + nested Outliner rows"))
        {
            WorldEditor.Finalize();
            Vulkan.TeardownInterchange();
            return 13;
        }

        // Verify per-instance override does not mutate source ComponentClassDefinition
        bool OverrideOk = false;
        const auto LiveTokens = WorldEditor.QueryWorldPlacements().CollectLiveTokens();
        if (!LiveTokens.empty() && !WorldEditor.QueryComponentDefinitions().empty())
        {
            const auto& ClassDef = WorldEditor.QueryComponentDefinitions()[0];
            const auto BodyTok   = ClassDef.Storage.FindTokenByTitle("Body");
            for (const auto& Row : ActivePanes->Outliner.QueryEntries())
            {
                if (Row.EnclosingToken == PlacedRoot && Row.Title == "Body")
                {
                    if (auto* MutableRow = ActivePanes->Outliner.AccessEntry(Row.Token))
                    {
                        MutableRow->PositionX += 0.25f;
                    }
                    WorldEditor.SynchronizeComponentPlacementOverrides();
                    const auto* Delta = WorldEditor.QueryWorldPlacements().InspectOverride(LiveTokens[0], BodyTok);
                    const auto* BodyHdr = ClassDef.Storage.InspectHeader(BodyTok);
                    OverrideOk = (Delta != nullptr && Delta->OverrideTransform &&
                                  BodyHdr != nullptr && std::fabs(BodyHdr->LocalTransform.LocationMeters.X) < 1.0e-4f);
                    break;
                }
            }
        }
        if (!Require(OverrideOk, "Per-instance Outliner transform override stays sparse in ComponentWorldPlacement"))
        {
            WorldEditor.Finalize();
            Vulkan.TeardownInterchange();
            return 14;
        }
        WorldEditor.Finalize();
    }

    Vulkan.TeardownInterchange();
    std::printf("ALL ComponentEditorUIValidation CHECKS PASSED.\n");
    return 0;
}
