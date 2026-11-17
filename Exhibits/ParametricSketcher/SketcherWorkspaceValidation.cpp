/*============================================================================================================================================
                                                    SKETCHERWORKSPACEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end validation of SketcherWorkspaceHost (docked Outliner/Viewport/DrawingSheet/Construct/Inspector + Vulkan B-rep + SVG icons + ProjectBrowser capability)

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Input/ImGuiInputAdapter.h"
#include "Engine/Input/InputExchange.h"
#include "Engine/Input/InputExtension.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherImagePlane.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherIcons.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherWorkspaceHost.h"
#include "Engine/Workspaces/WorkspaceCapabilityRegister.h"

#include <imgui.h>

#include <cmath>
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
    bool HasVulkan = Device.InitializeInterchange(RepoRoot, &VkDiag);
    if (!HasVulkan)
    {
#if defined(_WIN32)
        std::printf("[SKIP] No Vulkan ICD available on headless Windows runner (%s); running host/ImGui/CAD checks without GPU raster.\n", VkDiag.c_str());
#else
        std::fprintf(stderr, "[SketcherWorkspaceValidation] FAILED: VulkanInterchange initialization: %s\n", VkDiag.c_str());
        return 1;
#endif
    }

    ImGuiContext* Gui = ImGui::CreateContext();
    ImGui::SetCurrentContext(Gui);
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
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
    if (!VerifyCheck(Host.Initialize(HasVulkan ? &Device : nullptr, ShaderDir, RepoRoot / "EngineContent/Icons", {}, &HostDiag),
                     "SketcherWorkspaceHost initialization"))
    {
        std::fprintf(stderr, "Host diag: %s\n", HostDiag.c_str());
        Dock.Finalize();
        ImGui::DestroyContext(Gui);
        return 1;
    }
    Host.AccessState().WelcomeOpen = false;
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
        Host.EndFrame(1.0 / 60.0, true, true);
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

    // 5. InputExchange -> InputExtension -> ImGuiInputAdapter validation (numpad keys, punctuation, no stuck modifiers, overlay shield)
    {
        Input::InputExtension InputExt {};
        InputExt.Initialize();
        uint64_t Ts = 1'000'000ull;

        auto EmitKey = [&](Input::InputLogicalKey Key, Input::ButtonTransitionCategory Transition, uint16_t Mods)
        {
            Input::InputSignalPacket Pkt {};
            Pkt.TimestampNanoseconds = (Ts += 100'000ull);
            Pkt.Category             = Input::InputSignalCategory::KeyboardKey;
            Pkt.LogicalKey           = Key;
            Pkt.Transition           = Transition;
            Pkt.ModifierBitmask      = Mods;
            (void)InputExt.IntakeHardwareSignal(Pkt);
        };

        InputExt.BeginTick();
        EmitKey(Input::InputLogicalKey::LeftControl, Input::ButtonTransitionCategory::Pressed, Input::InputModifierBits::Control);
        EmitKey(Input::InputLogicalKey::Keypad1, Input::ButtonTransitionCategory::Pressed, 0u);
        EmitKey(Input::InputLogicalKey::LeftBracket, Input::ButtonTransitionCategory::Pressed, 0u);
        EmitKey(Input::InputLogicalKey::KeypadAdd, Input::ButtonTransitionCategory::Pressed, 0u);
        EmitKey(Input::InputLogicalKey::Minus, Input::ButtonTransitionCategory::Pressed, 0u);
        (void)InputExt.TickExchange();
        Input::InputExtension::AssignActiveExtension(&InputExt);

        if (!VerifyCheck(InputExt.InspectEditorSnapshot().ControlHeld &&
                             InputExt.InspectEditorSnapshot().QueryKeyPressed(Input::InputLogicalKey::Keypad1, false) &&
                             InputExt.InspectEditorSnapshot().QueryKeyPressed(Input::InputLogicalKey::LeftBracket, false) &&
                             InputExt.InspectEditorSnapshot().QueryKeyPressed(Input::InputLogicalKey::KeypadAdd, false) &&
                             InputExt.InspectEditorSnapshot().QueryKeyPressed(Input::InputLogicalKey::Minus, false),
                         "InputExtension records Keypad1, LeftBracket, KeypadAdd, Minus, ControlHeld"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }
        const std::size_t Forwarded = Input::ImGuiInputAdapter::ForwardEditorExchangeToImGui(InputExt, Io);
        ImGui::NewFrame();
        if (!VerifyCheck(Forwarded == 5u, "ImGuiInputAdapter drained 5 editor packets") ||
            !VerifyCheck(Io.KeyCtrl, "ImGuiInputAdapter sets Io.KeyCtrl") ||
            !VerifyCheck(ImGui::IsKeyPressed(ImGuiKey_Keypad1, false), "ImGuiInputAdapter propagates Keypad1") ||
            !VerifyCheck(ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false), "ImGuiInputAdapter propagates LeftBracket") ||
            !VerifyCheck(ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false), "ImGuiInputAdapter propagates KeypadAdd") ||
            !VerifyCheck(ImGui::IsKeyPressed(ImGuiKey_Minus, false), "ImGuiInputAdapter propagates Minus"))
        {
            ImGui::EndFrame();
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }
        ImGui::EndFrame();

        // Release LeftControl even when OS reports stale Control bit in ModifierBitmask: ControlHeld must clear cleanly
        InputExt.BeginTick();
        EmitKey(Input::InputLogicalKey::LeftControl, Input::ButtonTransitionCategory::Released, Input::InputModifierBits::Control);
        (void)InputExt.TickExchange();
        (void)Input::ImGuiInputAdapter::ForwardEditorExchangeToImGui(InputExt, Io);
        ImGui::NewFrame();
        if (!VerifyCheck(!InputExt.InspectEditorSnapshot().ControlHeld && !Io.KeyCtrl,
                         "Modifier release clears ControlHeld/KeyCtrl without sticking"))
        {
            ImGui::EndFrame();
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }
        ImGui::EndFrame();

        WorkspaceAppearance::AssignOverlayPointerShield(Rectangle2D { 10.0f, 10.0f, 120.0f, 80.0f });
        const bool BlockedInside  = WorkspaceAppearance::QueryOverlayPointerBlockedAt(Point2D { 40.0f, 40.0f });
        const bool ClearOutside   = !WorkspaceAppearance::QueryOverlayPointerBlockedAt(Point2D { 300.0f, 300.0f });
        WorkspaceAppearance::ClearOverlayPointerShield();
        Input::InputExtension::AssignActiveExtension(nullptr);
        if (!VerifyCheck(BlockedInside && ClearOutside, "WorkspaceAppearance overlay pointer shield blocks/passes properly"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }
        ++Passed;
    }

    // 6. CAD controls & UX: Ctrl+E menu, E last extrude mode, Ctrl+B bevel, C chamfer, G/R/S + .ImageSpace drop + Snap filter
    {
        auto StepHostedFrame = [&](auto&& InjectBeforeNewFrame)
        {
            InjectBeforeNewFrame();
            ImGui::NewFrame();
            Host.BeginFrame(1.0 / 60.0);
            Dock.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
            Host.EndFrame(1.0 / 60.0, true, true);
            ImGui::Render();
        };

        auto& State = Host.AccessState();
        if (!VerifyCheck(!State.Scene.Faces.empty() && !State.Scene.Edges.empty(), "Scene has faces and edges for CAD controls"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }

        // Ctrl+E opens ExtrudeTypeContextMenu (without triggering Export)
        State.Document.Selection = {};
        State.Document.Selection.Faces = { State.Scene.Faces.front().Name };
        if (!VerifyCheck(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
                         "No popup open before Ctrl+E"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }
        StepHostedFrame([&]()
        {
            Io.AddMousePosEvent(500.0f, 360.0f);
            Io.AddKeyEvent(ImGuiMod_Ctrl, true);
            Io.AddKeyEvent(ImGuiKey_E, true);
        });
        if (!VerifyCheck(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
                             !State.ExtrudeMenuRequest &&
                             State.PopupRequest.empty(),
                         "Ctrl+E opens ExtrudeTypeContextMenu instead of Export"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }
        StepHostedFrame([&]()
        {
            Io.AddKeyEvent(ImGuiKey_E, false);
            Io.AddKeyEvent(ImGuiMod_Ctrl, false);
            Io.AddKeyEvent(ImGuiKey_Escape, true);
        });
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_Escape, false); });
        if (!VerifyCheck(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
                         "Escape closes ExtrudeTypeContextMenu"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }

        // Set LastExtrudeMode = Symmetric and press E -> opens Extrude session with Symmetric mode
        State.Document.Selection.Faces = { State.Scene.Faces.front().Name };
        State.LastExtrudeMode = SketcherExtrudeMode::Symmetric;
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_E, true); });
        const bool ExtrudeOpened = State.Bevel.ActiveCondition &&
                                   State.Bevel.Verb == SketcherSessionVerb::Extrude &&
                                   State.Bevel.ExtrudeMode == SketcherExtrudeMode::Symmetric;
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_E, false); });
        if (!VerifyCheck(ExtrudeOpened, "E starts Extrude with LastExtrudeMode (Symmetric)"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_Escape, true); });
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_Escape, false); });

        // Ctrl+B opens Bevel Fillet; C with edge selected opens Chamfer
        State.Document.Selection = {};
        State.Document.Selection.Edges = { State.Scene.Edges.front().Name };
        StepHostedFrame([&]()
        {
            Io.AddKeyEvent(ImGuiMod_Ctrl, true);
            Io.AddKeyEvent(ImGuiKey_B, true);
        });
        const bool CtrlBFillet = State.Bevel.ActiveCondition &&
                                 State.Bevel.Verb == SketcherSessionVerb::Blend &&
                                 State.Bevel.Shape == GeometrySpace::Parametric::BlendProfile::Fillet;
        StepHostedFrame([&]()
        {
            Io.AddKeyEvent(ImGuiKey_B, false);
            Io.AddKeyEvent(ImGuiMod_Ctrl, false);
            Io.AddKeyEvent(ImGuiKey_Escape, true);
        });
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_Escape, false); });

        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_C, true); });
        const bool CChamfer = State.Bevel.ActiveCondition &&
                              State.Bevel.Verb == SketcherSessionVerb::Blend &&
                              State.Bevel.Shape == GeometrySpace::Parametric::BlendProfile::Chamfer;
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_C, false); });
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_Escape, true); });
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_Escape, false); });
        if (!VerifyCheck(CtrlBFillet && CChamfer, "Ctrl+B starts Fillet and C on edge starts Chamfer"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }

        // .ImageSpace drop intake + G (Move) transform on focused ImagePlane
        SketcherPicture DropPic {};
        DropPic.Width  = 2u;
        DropPic.Height = 2u;
        DropPic.Pixels.assign(2u * 2u * 4u, 220u);
        const std::filesystem::path DropPath =
            std::filesystem::temp_directory_path() / "SketcherWsDropRef.ImageSpace";
        std::string DropDiag {};
        if (!VerifyCheck(ExportSketcherPictureAsImageSpace(DropPic, DropPath, DropDiag, "WsDropPlane"),
                         "Create .ImageSpace drop fixture"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }
        const bool DroppedOk = Host.IntakeExternalFileDrop(DropPath.string(), &DropDiag);
        std::filesystem::remove(DropPath);
        if (!VerifyCheck(DroppedOk && State.Document.ImagePlanes.size() == 1u,
                         "IntakeExternalFileDrop adds .ImageSpace ImagePlane"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }
        const double OrigCornerX = State.Document.ImagePlanes[0].Corner.X;
        State.Document.Selection = {};
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_G, true); });
        const bool GrabStarted = State.Transform.ActiveCondition &&
                                 State.Transform.Verb == SketcherTransformVerb::Move &&
                                 State.Transform.TargetKind == SketcherTransformTarget::ImagePlanes;
        StepHostedFrame([&]()
        {
            Io.AddKeyEvent(ImGuiKey_G, false);
            Io.AddKeyEvent(ImGuiKey_X, true);
        });
        StepHostedFrame([&]()
        {
            Io.AddKeyEvent(ImGuiKey_X, false);
            Io.AddKeyEvent(ImGuiKey_0, true);
        });
        StepHostedFrame([&]()
        {
            Io.AddKeyEvent(ImGuiKey_0, false);
            Io.AddKeyEvent(ImGuiKey_Period, true);
        });
        StepHostedFrame([&]()
        {
            Io.AddKeyEvent(ImGuiKey_Period, false);
            Io.AddKeyEvent(ImGuiKey_5, true);
        });
        StepHostedFrame([&]()
        {
            Io.AddKeyEvent(ImGuiKey_5, false);
            Io.AddKeyEvent(ImGuiKey_Enter, true);
        });
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_Enter, false); });
        const double MovedCornerX = State.Document.ImagePlanes[0].Corner.X;
        if (!VerifyCheck(GrabStarted, "G starts Move transform on .ImageSpace ImagePlane") ||
            !VerifyCheck(std::abs((MovedCornerX - OrigCornerX) - 0.5) < 1.0e-5,
                         "G + X + 0.5 + Enter moves .ImageSpace plane by +0.5 m"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }

        // Verify R and S start Rotate & Scale sessions on body
        State.Document.Selection.Bodies = { State.Document.Summary.Bodies.front().Key };
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_R, true); });
        const bool RotateOk = State.Transform.ActiveCondition && State.Transform.Verb == SketcherTransformVerb::Rotate;
        StepHostedFrame([&]()
        {
            Io.AddKeyEvent(ImGuiKey_R, false);
            Io.AddKeyEvent(ImGuiKey_Escape, true);
        });
        StepHostedFrame([&]()
        {
            Io.AddKeyEvent(ImGuiKey_Escape, false);
            Io.AddKeyEvent(ImGuiKey_S, true);
        });
        const bool ScaleOk = State.Transform.ActiveCondition && State.Transform.Verb == SketcherTransformVerb::Scale;
        StepHostedFrame([&]()
        {
            Io.AddKeyEvent(ImGuiKey_S, false);
            Io.AddKeyEvent(ImGuiKey_Escape, true);
        });
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_Escape, false); });
        if (!VerifyCheck(RotateOk && ScaleOk, "R starts Rotate and S starts Scale on selected body"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }

        // Verify Snap Filter & ContextAwareSnap defaults and Shift+A Line drawing mode
        if (!VerifyCheck(State.Picker.Enabled && State.Picker.ContextAwareSnap && State.Picker.Filter.CurveEnabled && State.Picker.Filter.EndpointEnabled,
                         "Snap Filter & ContextAwareSnap enabled by default"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }
        StepHostedFrame([&]()
        {
            Io.AddKeyEvent(ImGuiMod_Shift, true);
            Io.AddKeyEvent(ImGuiKey_A, true);
        });
        const bool LineDrawingStarted = State.Drawing.ActiveCondition && State.Drawing.Kind == SketcherDrawingKind::Line;
        StepHostedFrame([&]()
        {
            Io.AddKeyEvent(ImGuiKey_A, false);
            Io.AddKeyEvent(ImGuiMod_Shift, false);
            Io.AddKeyEvent(ImGuiKey_Escape, true);
        });
        StepHostedFrame([&]() { Io.AddKeyEvent(ImGuiKey_Escape, false); });
        if (!VerifyCheck(LineDrawingStarted, "Shift+A starts Line drawing session"))
        {
            Host.Finalize();
            Dock.Finalize();
            ImGui::DestroyContext(Gui);
            return 1;
        }
        ++Passed;
    }

    Host.Finalize();
    Dock.Finalize();
    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(Gui);
    if (HasVulkan)
    {
        Device.TeardownInterchange();
    }

    std::printf("[SketcherWorkspaceValidation] Passed=%u Failed=0 (Tabs=%u Faces=%zu Edges=%zu)\n",
                Passed, 5u, Scene.Faces.size(), Scene.Edges.size());
    return 0;
}
