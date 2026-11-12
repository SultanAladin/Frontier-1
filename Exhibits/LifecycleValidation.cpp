/*============================================================================================================================================
                                                          LIFECYCLEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end validation for LifecycleSequence, RuntimeLifecycle, WorkspaceLifecycle, and EditorLifecycle

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Lifecycle/EditorLifecycle.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/Lifecycle/RuntimeLifecycle.h"
#include "Engine/Lifecycle/WorkspaceLifecycle.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    VALIDATION HELPERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool VerifyCondition(bool Condition, const char* Description)
{
    if (!Condition)
    {
        std::fprintf(stderr, "[LifecycleValidation] FAILED: %s\n", Description);
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            TEST 1: LIFECYCLESEQUENCE ORDERING
//------------------------------------------------------------------------------------------------------------------------

struct RecordedParticipant
{
    std::string               Identifier {};
    std::vector<std::string>* EventLog   = nullptr;
    bool                      FailOnInit = false;
    uint32_t                  TickTotal  = 0u;

    [[nodiscard]] bool Initialize() noexcept
    {
        if (EventLog != nullptr)
        {
            EventLog->push_back("Init:" + Identifier);
        }
        return !FailOnInit;
    }

    void Tick(double Δt) noexcept
    {
        if (Δt > 0.0)
        {
            ++TickTotal;
        }
        if (EventLog != nullptr)
        {
            EventLog->push_back("Tick:" + Identifier);
        }
    }

    void Suspend() noexcept
    {
        if (EventLog != nullptr)
        {
            EventLog->push_back("Suspend:" + Identifier);
        }
    }

    void Resume() noexcept
    {
        if (EventLog != nullptr)
        {
            EventLog->push_back("Resume:" + Identifier);
        }
    }

    void Finalize() noexcept
    {
        if (EventLog != nullptr)
        {
            EventLog->push_back("Finalize:" + Identifier);
        }
    }
};

static_assert(
    Frontier::Lifecycle::SuspendableParticipant<RecordedParticipant>,
    "RecordedParticipant must satisfy the SuspendableParticipant C++20 concept.");

[[nodiscard]] bool ValidateDeterministicSequenceAndRollback()
{
    using Frontier::Lifecycle::LifecyclePhase;
    using Frontier::Lifecycle::LifecycleSequence;

    // 1A: Forward Initialize -> Tick -> Reverse Suspend -> Forward Resume -> Reverse Finalize
    {
        std::vector<std::string> Events;
        RecordedParticipant      Alpha{ "Alpha", &Events, false, 0u };
        RecordedParticipant      Beta{ "Beta", &Events, false, 0u };
        RecordedParticipant      Gamma{ "Gamma", &Events, false, 0u };

        LifecycleSequence Sequence;
        const uint32_t    SlotAlpha = Sequence.RegisterParticipant("Alpha", Alpha);
        const uint32_t    SlotBeta  = Sequence.RegisterParticipant("Beta", Beta);
        const uint32_t    SlotGamma = Sequence.RegisterParticipant("Gamma", Gamma);
        if (!VerifyCondition(SlotAlpha == 0u && SlotBeta == 1u && SlotGamma == 2u, "Sequential participant slot indices"))
        {
            return false;
        }

        if (!VerifyCondition(Sequence.Initialize(), "Sequence Initialize succeeds") ||
            !VerifyCondition(Sequence.QueryPhase() == LifecyclePhase::Active, "Phase == Active"))
        {
            return false;
        }

        Sequence.Tick(1.0 / 60.0);
        if (!VerifyCondition(Sequence.Suspend(), "Sequence Suspend succeeds") ||
            !VerifyCondition(Sequence.QueryPhase() == LifecyclePhase::Suspended, "Phase == Suspended"))
        {
            return false;
        }

        // Tick while Suspended must be ignored
        Sequence.Tick(1.0 / 60.0);
        if (!VerifyCondition(Alpha.TickTotal == 1u && Gamma.TickTotal == 1u, "Suspended sequence skips Tick"))
        {
            return false;
        }

        if (!VerifyCondition(Sequence.Resume(), "Sequence Resume succeeds"))
        {
            return false;
        }

        Sequence.Finalize();
        if (!VerifyCondition(Sequence.QueryPhase() == LifecyclePhase::Finalized, "Phase == Finalized"))
        {
            return false;
        }

        const std::vector<std::string> Expected = {
            "Init:Alpha",     "Init:Beta",      "Init:Gamma",
            "Tick:Alpha",     "Tick:Beta",      "Tick:Gamma",
            "Suspend:Gamma",  "Suspend:Beta",   "Suspend:Alpha",
            "Resume:Alpha",   "Resume:Beta",    "Resume:Gamma",
            "Finalize:Gamma", "Finalize:Beta",  "Finalize:Alpha",
        };
        if (!VerifyCondition(Events == Expected, "Deterministic forward/reverse ordering matches exact trace"))
        {
            return false;
        }
    }

    // 1B: Partial failure rollback (Gamma fails -> Beta then Alpha finalized in reverse)
    {
        std::vector<std::string> RollbackEvents;
        RecordedParticipant      Alpha{ "Alpha", &RollbackEvents, false, 0u };
        RecordedParticipant      Beta{ "Beta", &RollbackEvents, false, 0u };
        RecordedParticipant      BrokenGamma{ "Gamma", &RollbackEvents, true, 0u };

        LifecycleSequence Sequence;
        (void)Sequence.RegisterParticipant("Alpha", Alpha);
        (void)Sequence.RegisterParticipant("Beta", Beta);
        (void)Sequence.RegisterParticipant("Gamma", BrokenGamma);

        const bool                     Initialized      = Sequence.Initialize();
        const std::vector<std::string> ExpectedRollback = {
            "Init:Alpha",
            "Init:Beta",
            "Init:Gamma",
            "Finalize:Beta",
            "Finalize:Alpha",
        };

        if (!VerifyCondition(!Initialized, "Sequence.Initialize returns false when Gamma fails") ||
            !VerifyCondition(Sequence.QueryPhase() == LifecyclePhase::Dormant, "Failed Initialize rolls back to Dormant") ||
            !VerifyCondition(Sequence.InspectMetrics().ActiveCount == 0u, "ActiveCount == 0 after rollback") ||
            !VerifyCondition(RollbackEvents == ExpectedRollback, "Reverse rollback finalizes Beta then Alpha only"))
        {
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            TEST 2: RUNTIMELIFECYCLE GAME LOOP
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool ValidateRuntimeLifecycleLoop()
{
    using Frontier::Lifecycle::LifecyclePhase;
    using Frontier::Lifecycle::RuntimeLifecycle;
    using Frontier::Lifecycle::RuntimeLoopConfiguration;
    using Frontier::Windowing::WindowConfigurationDescriptor;

    std::vector<std::string> DomainTrace;
    RecordedParticipant      SimulationDomain{ "SimulationSpace", &DomainTrace, false, 0u };
    RecordedParticipant      AudioDomain{ "AudioSpace", &DomainTrace, false, 0u };
    RecordedParticipant      RasterDomain{ "GeometricRaster", &DomainTrace, false, 0u };

    RuntimeLifecycle Runtime;
    (void)Runtime.RegisterDomainParticipant("SimulationSpace", SimulationDomain);
    (void)Runtime.RegisterDomainParticipant("AudioSpace", AudioDomain);
    (void)Runtime.RegisterDomainParticipant("GeometricRaster", RasterDomain);

    RuntimeLoopConfiguration Config{};
    Config.WindowDescriptor     = WindowConfigurationDescriptor::ConstructGameWindow("Frontier Game", 1280u, 720u);
    Config.DefaultStepSeconds   = 1.0 / 120.0;
    Config.MaximumStepSeconds   = 0.05;
    Config.SuspendWhenMinimized = true;
    Config.PreferHeadlessWindow = true;

    if (!VerifyCondition(Runtime.Initialize(Config), "RuntimeLifecycle Initialize"))
    {
        return false;
    }

    const uint32_t Executed = Runtime.ExecuteBoundedLoop(4u, 1.0 / 120.0);
    if (!VerifyCondition(Executed == 4u, "ExecuteBoundedLoop ran 4 ticks") ||
        !VerifyCondition(SimulationDomain.TickTotal == 4u && RasterDomain.TickTotal == 4u, "All runtime domains ticked 4 times"))
    {
        return false;
    }

    // Minimized window auto-suspends RuntimeLifecycle when SuspendWhenMinimized == true
    auto* Headless = Runtime.AccessHeadlessWindow();
    if (!VerifyCondition(Headless != nullptr, "AccessHeadlessWindow returns bound HeadlessPlatformWindow"))
    {
        return false;
    }

    Headless->ResizeSurface(0u, 0u, 1.0f);
    (void)Runtime.ExecuteSingleTick(1.0 / 120.0);
    if (!VerifyCondition(Runtime.QueryPhase() == LifecyclePhase::Suspended, "Minimized window suspends RuntimeLifecycle") ||
        !VerifyCondition(SimulationDomain.TickTotal == 4u, "Suspended RuntimeLifecycle pauses domain ticks"))
    {
        return false;
    }

    Headless->ResizeSurface(1280u, 720u, 1.0f);
    (void)Runtime.ExecuteSingleTick(1.0 / 120.0);
    if (!VerifyCondition(Runtime.QueryPhase() == LifecyclePhase::Active, "Restored window resumes RuntimeLifecycle") ||
        !VerifyCondition(SimulationDomain.TickTotal == 5u, "Resumed RuntimeLifecycle ticks domains again"))
    {
        return false;
    }

    Runtime.Finalize();
    return VerifyCondition(!Runtime.QueryRunning(), "RuntimeLifecycle Finalize stops running loop");
}

//------------------------------------------------------------------------------------------------------------------------
//                                           TEST 3: WORKSPACELIFECYCLE SESSIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool ValidateWorkspaceLifecycleMultiDocument()
{
    using Frontier::Lifecycle::WorkspaceDocumentDescriptor;
    using Frontier::Lifecycle::WorkspaceLifecycle;
    using Frontier::Lifecycle::WorkspaceSessionToken;

    WorkspaceLifecycle Workspace;
    if (!VerifyCondition(Workspace.Initialize(false), "WorkspaceLifecycle Initialize(false)") ||
        !VerifyCondition(Workspace.OpenSessionCount() == 0u, "Starts with 0 open sessions when requested"))
    {
        return false;
    }

    WorkspaceDocumentDescriptor FirstDoc{};
    FirstDoc.DocumentTitle = "ChassisAssembly";

    WorkspaceDocumentDescriptor SecondDoc{};
    SecondDoc.DocumentTitle = "SuspensionRig";

    const WorkspaceSessionToken TokenA = Workspace.OpenSession(FirstDoc);
    const WorkspaceSessionToken TokenB = Workspace.OpenSession(SecondDoc);

    if (!VerifyCondition(TokenA.Valid() && TokenB.Valid(), "Both workspace session tokens valid") ||
        !VerifyCondition(Workspace.OpenSessionCount() == 2u, "2 open workspace sessions") ||
        !VerifyCondition(Workspace.QueryActiveSession() == TokenB, "Second session is active"))
    {
        return false;
    }

    // Default startup scene inside every session MUST be empty (0 entries)
    const auto* ActivePanes = Workspace.InspectSessionPanes(TokenB);
    if (!VerifyCondition(ActivePanes != nullptr, "InspectSessionPanes returns valid panes") ||
        !VerifyCondition(ActivePanes->Outliner.QueryTotalCount() == 0u, "Default workspace session starts with 0 Outliner entries"))
    {
        return false;
    }

    // Revision undo/redo & save tracking
    if (!VerifyCondition(Workspace.RecordRevision(TokenB, "Construct WheelHub") == 1u, "RecordRevision -> 1") ||
        !VerifyCondition(Workspace.RecordRevision(TokenB, "Transform WheelHub") == 2u, "RecordRevision -> 2") ||
        !VerifyCondition(Workspace.InspectSession(TokenB).Modified, "Session marked Modified after revision") ||
        !VerifyCondition(Workspace.StepBackwardRevision(TokenB), "StepBackwardRevision succeeds") ||
        !VerifyCondition(Workspace.InspectSession(TokenB).RevisionCursor == 1u, "RevisionCursor == 1 after step back") ||
        !VerifyCondition(Workspace.StepForwardRevision(TokenB), "StepForwardRevision succeeds"))
    {
        return false;
    }

    Workspace.MarkSessionSaved(TokenB);
    if (!VerifyCondition(!Workspace.InspectSession(TokenB).Modified, "Modified cleared after MarkSessionSaved"))
    {
        return false;
    }

    Workspace.Tick(1.0 / 60.0);

    // Closing active TokenB falls back to TokenA and invalidates stale TokenB
    if (!VerifyCondition(Workspace.CloseSession(TokenB), "CloseSession(TokenB)") ||
        !VerifyCondition(Workspace.InspectSessionPanes(TokenB) == nullptr, "Closed TokenB rejected by generation check") ||
        !VerifyCondition(Workspace.QueryActiveSession() == TokenA, "Active session falls back to TokenA"))
    {
        return false;
    }

    Workspace.Finalize();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        TEST 4: EDITORLIFECYCLE AUTHORING ORCHESTRATION
//------------------------------------------------------------------------------------------------------------------------

#if FRONTIER_EDITOR_ENABLED
[[nodiscard]] bool ValidateEditorLifecycleAndRenderExhibit(const std::filesystem::path& OutputDirectory)
{
    using Frontier::Lifecycle::EditorLifecycle;
    using Frontier::Lifecycle::EditorStartupConfiguration;
    using Frontier::Windowing::WindowConfigurationDescriptor;

    std::filesystem::create_directories(OutputDirectory);

    IMGUI_CHECKVERSION();
    ImGuiContext* Context = ImGui::CreateContext();
    ImGui::SetCurrentContext(Context);
    ImGuiIO& Io = ImGui::GetIO();
    Io.IniFilename  = nullptr;
    Io.LogFilename  = nullptr;
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    Io.DisplaySize  = ImVec2(1440.0f, 900.0f);

    EditorLifecycle     Editor;
    RecordedParticipant GpuOverlayHook{ "SelectionSilhouetteOverlay", nullptr, false, 0u };
    const uint32_t      OverlaySlot = Editor.RegisterGpuViewportOverlay("SelectionSilhouetteOverlay", GpuOverlayHook);
    if (!VerifyCondition(OverlaySlot == 0u, "RegisterGpuViewportOverlay assigns slot 0"))
    {
        ImGui::DestroyContext(Context);
        return false;
    }

    EditorStartupConfiguration Config{};
    Config.WindowDescriptor   = WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier", 1440u, 900u);
    Config.FontRepositoryRoot = Frontier::Exhibits::ResolveRepositoryRootPath() / "EngineContent/FontsArchives";

    if (!VerifyCondition(Editor.Initialize(Config), "EditorLifecycle Initialize succeeds"))
    {
        ImGui::DestroyContext(Context);
        return false;
    }

    unsigned char* AtlasPixels = nullptr;
    int            AtlasWidth  = 0;
    int            AtlasHeight = 0;
    Io.Fonts->GetTexDataAsRGBA32(&AtlasPixels, &AtlasWidth, &AtlasHeight);

    // Verify empty default startup scene inside EditorLifecycle
    const auto  ActiveSession = Editor.AccessWorkspaces().QueryActiveSession();
    const auto* SessionPanes  = Editor.AccessWorkspaces().InspectSessionPanes(ActiveSession);
    if (!VerifyCondition(
            SessionPanes != nullptr && SessionPanes->Outliner.QueryTotalCount() == 0u,
            "EditorLifecycle starts with 0 scene entries"))
    {
        Editor.Finalize();
        ImGui::DestroyContext(Context);
        return false;
    }

    Frontier::Exhibits::SoftwareRasterCanvas Canvas(1440, 900, { 14, 14, 16, 255 });
    for (int WarmupStep = 0; WarmupStep < 3; ++WarmupStep)
    {
        (void)Editor.ExecuteAuthoringTick(1.0 / 60.0);
        Io.DisplaySize = ImVec2(1440.0f, 900.0f);
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        Editor.PresentPrimaryEditorImGui(1.0 / 60.0);
        ImGui::Render();
        if (WarmupStep == 2)
        {
            Canvas.Clear({ 14, 14, 16, 255 });
            Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        }
    }

    const bool WroteExhibit = Canvas.WritePngFile(OutputDirectory / "01-EditorLifecycleWorkspace.png");
    if (!VerifyCondition(WroteExhibit, "01-EditorLifecycleWorkspace.png written") ||
        !VerifyCondition(GpuOverlayHook.TickTotal == 3u, "GPU .slang viewport overlay participant ticked 3 times") ||
        !VerifyCondition(Editor.InspectReading().RegisteredPaneCount == 3u, "RegisteredPaneCount == 3 (Outliner, Viewport, Inspector)") ||
        !VerifyCondition(!Editor.InspectReading().ContentBrowserOpen, "ContentBrowser starts closed behind bottom notch"))
    {
        Editor.Finalize();
        ImGui::DestroyContext(Context);
        return false;
    }

    Editor.AccessContentBrowser().AccessPanel().ConfigureShelfExtent(Frontier::ContentShelfExtent::SplitHalf, true);
    if (!VerifyCondition(Editor.InspectReading().ContentBrowserOpen, "EditorLifecycle reports ContentBrowserOpen when SplitHalf"))
    {
        Editor.Finalize();
        ImGui::DestroyContext(Context);
        return false;
    }

    if (!VerifyCondition(Editor.Suspend(), "EditorLifecycle Suspend") ||
        !VerifyCondition(Editor.Resume(), "EditorLifecycle Resume"))
    {
        Editor.Finalize();
        ImGui::DestroyContext(Context);
        return false;
    }

    Editor.Finalize();
    const bool CleanExit = VerifyCondition(!Editor.InspectReading().AuthoringActive, "EditorLifecycle Finalize resets AuthoringActive");
    ImGui::DestroyContext(Context);
    return CleanExit;
}
#endif

} // namespace

int main()
{
    if (!ValidateDeterministicSequenceAndRollback())
    {
        return 1;
    }
    if (!ValidateRuntimeLifecycleLoop())
    {
        return 1;
    }
    if (!ValidateWorkspaceLifecycleMultiDocument())
    {
        return 1;
    }

#if FRONTIER_EDITOR_ENABLED
    const auto OutputDir = Frontier::Exhibits::ResolveRepositoryRootPath() / "Exhibits/Images/Lifecycle";
    if (!ValidateEditorLifecycleAndRenderExhibit(OutputDir))
    {
        return 1;
    }
#endif

    std::printf(
        "[LifecycleValidation] All LifecycleSequence, RuntimeLifecycle, WorkspaceLifecycle%s checks passed.\n",
        FRONTIER_EDITOR_ENABLED ? ", and EditorLifecycle" : "");
    return 0;
}
