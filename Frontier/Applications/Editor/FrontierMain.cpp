/*==========================================================================================================================================
                                                             APPLICATION ENTRY
                                     ------------------------------------------------------------------
                             The project browser opens first and closes behind the project window it launches.
==========================================================================================================================================*/

// 🧩 Application entry: the project browser opens first and closes behind the project window it launches.

#include "CommandLine.h"
#include "ProjectBrowserHost.h"

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Interface/ProjectContent.h"
#include "Engine/Interface/LogStreamPanel.h"
#include "Engine/Interface/ProjectDescriptor.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <system_error>

namespace Frontier
{
namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::filesystem::path ExecutableDirectory(const char* ArgumentZero)
{
    std::error_code Error;
    if (ArgumentZero == nullptr || ArgumentZero[0] == '\0')
    {
        return std::filesystem::current_path(Error);
    }
    const std::filesystem::path Absolute = std::filesystem::absolute(ArgumentZero, Error);
    return Absolute.has_parent_path() ? Absolute.parent_path() : std::filesystem::current_path(Error);
}

std::filesystem::path ResolveProjectsDirectory(const std::filesystem::path& FromExecutable)
{
    std::error_code             Error;
    const std::filesystem::path Working = std::filesystem::current_path(Error);
    const std::filesystem::path Candidates[] = {
        FromExecutable / "Projects",
        FromExecutable / ".." / "Projects",
        FromExecutable / ".." / ".." / "Frontier" / "Projects",
        Working / "Frontier" / "Projects",
        Working / "Projects",
    };
    for (const std::filesystem::path& Candidate : Candidates)
    {
        if (std::filesystem::is_directory(Candidate, Error))
        {
            return Candidate;
        }
    }
    return Candidates[3];   // [path] - Repository layout default, reported in enumeration diagnostics when missing
}

uint32_t EngineToken(const char* Name)
{
    const LogCategory* Category = LogCategories().QueryByName(Name);
    return Category != nullptr ? Category->Token : 0;
}

// Opens everything a project session needs before any window exists. Returns false with a printed diagnostic.
bool PrepareProjectSession(const std::filesystem::path& DescriptorPath,
                           const std::filesystem::path& FromExecutable,
                           ProjectDescriptor&           OutDescriptor)
{
    std::string Diagnostic;
    if (!DecodeProjectDescriptor(DescriptorPath, OutDescriptor, Diagnostic))
    {
        std::fprintf(stderr, "Frontier: cannot open project: %s\n", Diagnostic.c_str());
        return false;
    }

    ConfigureContentRoots(FromExecutable, OutDescriptor.ProjectDirectory / OutDescriptor.ContentLocation);
    ConfigureLogFile(OutDescriptor.ProjectDirectory / "Saved" / "Logs" / "Frontier.log");

    const uint32_t Declared = RegisterDescriptorLogCategories(OutDescriptor);

    char Line[512] = {};
    std::snprintf(Line, sizeof(Line), "Frontier session opened · %s", OutDescriptor.Name.c_str());
    WriteLog(EngineToken("Interface"), LogSeverity::Info, LogVerbosity::Display, Line, "FrontierMain.cpp");

    const std::filesystem::path EngineContent = QueryEngineContentDirectory();
    std::snprintf(Line, sizeof(Line), "Content roots configured · project %s · engine %s",
                  OutDescriptor.ContentLocation.c_str(),
                  EngineContent.empty() ? "(none staged)" : EncodeUtf8Path(EngineContent).c_str());
    WriteLog(EngineToken("Content"), LogSeverity::Info, LogVerbosity::Log, Line, "FrontierMain.cpp");

    std::snprintf(Line, sizeof(Line), "Log categories registered · %u declared by project · %u total",
                  Declared, LogCategories().QueryCount());
    WriteLog(EngineToken("Interface"), LogSeverity::Info, LogVerbosity::Log, Line, "FrontierMain.cpp");
    return true;
}

// Deterministic stand-in traffic so a headless session exercises the whole panel pipeline end to end.
// 📝 Clearly announced as synthetic in the stream itself; nothing here pretends to be a live renderer.
void EmitSyntheticTraffic(const ProjectDescriptor& Descriptor)
{
    WriteLog(EngineToken("Interface"), LogSeverity::Info, LogVerbosity::Display,
             "Headless diagnostic session · synthetic traffic follows", "FrontierMain.cpp");

    uint32_t Random = 2166136261u;
    auto     Next   = [&Random]() {
        Random = Random * 1664525u + 1013904223u;
        return (Random >> 16) & 0x7fffu;
    };

    char Line[256] = {};
    for (uint32_t Index = 0; Index < 36; ++Index)
    {
        switch (Index % 6)
        {
            case 0:
                std::snprintf(Line, sizeof(Line), "Pipeline cache hit · SurfaceRasterization (0.%02u ms)", 20 + Next() % 70);
                WriteLog(EngineToken("Render"), LogSeverity::Info, LogVerbosity::Log, Line, "FrontierMain.cpp");
                break;
            case 1:
                std::snprintf(Line, sizeof(Line), "Temporal resampling converged · reservoirs valid %u.%u%%",
                              96 + Next() % 4, Next() % 10);
                WriteLog(EngineToken("Illumination"), LogSeverity::Info, LogVerbosity::Log, Line, "FrontierMain.cpp");
                break;
            case 2:
                std::snprintf(Line, sizeof(Line), "Jolt world stepped · %u bodies", 900 + Next() % 700);
                WriteLog(EngineToken("Physics"), LogSeverity::Info, LogVerbosity::Verbose, Line, "FrontierMain.cpp");
                break;
            case 3:
                std::snprintf(Line, sizeof(Line), "Powertrain · gear %u · %u rpm", 2 + Next() % 5, 2800 + Next() % 3600);
                WriteLog(EngineToken("Vehicle"), LogSeverity::Info, LogVerbosity::Display, Line, "FrontierMain.cpp");
                break;
            case 4:
                std::snprintf(Line, sizeof(Line), "Cluster cull · %u%% rejected by HiZ", 30 + Next() % 28);
                WriteLog(EngineToken("Geometry"), LogSeverity::Info, LogVerbosity::Log, Line, "FrontierMain.cpp");
                break;
            default:
                std::snprintf(Line, sizeof(Line), "Sky transmittance sampled · sun elevation %u deg", Next() % 62);
                WriteLog(EngineToken("Environment"), LogSeverity::Info, LogVerbosity::Verbose, Line, "FrontierMain.cpp");
                break;
        }
    }

    // Declared user categories speak too — including a repeated line so repeat collapsing is visible.
    for (const ProjectLogCategory& Declared : Descriptor.LogCategories)
    {
        const LogCategory* Category = LogCategories().QueryByName(Declared.Name);
        if (Category == nullptr)
        {
            continue;
        }
        for (uint32_t Repeat = 0; Repeat < 5; ++Repeat)
        {
            WriteLog(Category->Token, LogSeverity::Info, LogVerbosity::Log,
                     "Leaf cluster detached · 22 leaves · gust 5.8 m/s", "FrontierMain.cpp");
        }
        WriteLog(Category->Token, LogSeverity::Warning, LogVerbosity::Log,
                 "Leaf instance budget 91% · oldest litter recycled", "FrontierMain.cpp");
    }

    WriteLog(EngineToken("Illumination"), LogSeverity::Error, LogVerbosity::Log,
             "NaN in reservoir stream at tile (141, 63) · reservoir reset for tile", "FrontierMain.cpp");
}

int RunHeadlessSession(const std::filesystem::path& DescriptorPath, const std::filesystem::path& FromExecutable)
{
    ProjectDescriptor Descriptor;
    if (!PrepareProjectSession(DescriptorPath, FromExecutable, Descriptor))
    {
        return 1;
    }

    EmitSyntheticTraffic(Descriptor);

    LogStreamPanel Panel;
    for (uint32_t Step = 0; Step < 16; ++Step)
    {
        Panel.Synchronize(LogStore(), LogCategories(), 0.25);
    }

    std::printf("Frontier headless · %s\n", Descriptor.Name.c_str());
    std::printf("  entries %llu · warnings %llu · errors %llu · visible %zu · categories %u · dropped %llu\n",
                static_cast<unsigned long long>(Panel.QueryTotalCount()),
                static_cast<unsigned long long>(Panel.QuerySeverityCount(LogSeverity::Warning)),
                static_cast<unsigned long long>(Panel.QuerySeverityCount(LogSeverity::Error) +
                                                 Panel.QuerySeverityCount(LogSeverity::Fatal)),
                Panel.QueryVisible().size(),
                LogCategories().QueryCount(),
                static_cast<unsigned long long>(Panel.QueryDroppedCount()));
    std::printf("  session log: %s\n",
                EncodeUtf8Path(Descriptor.ProjectDirectory / "Saved" / "Logs" / "Frontier.log").c_str());
    return 0;
}

#if !defined(FRONTIER_WITH_INTERFACE_SDK)
int RunBrowserListing(const std::filesystem::path& FromExecutable)
{
    const std::filesystem::path    Projects    = ResolveProjectsDirectory(FromExecutable);
    const ProjectBrowserEnumeration Enumeration = EnumerateProjects(Projects);

    std::printf("Frontier project browser (headless listing) · %s\n", EncodeUtf8Path(Projects).c_str());
    for (const ProjectBrowserEntry& Entry : Enumeration.Entries)
    {
        std::printf("  %s · %s\n", Entry.Descriptor.Name.c_str(),
                    EncodeUtf8Path(Entry.Descriptor.DescriptorPath).c_str());
    }
    for (const std::string& Diagnostic : Enumeration.Diagnostics)
    {
        std::printf("  skipped: %s\n", Diagnostic.c_str());
    }
    if (Enumeration.Entries.empty())
    {
        std::printf("  (no projects decoded)\n");
    }
    return 0;
}
#endif // !FRONTIER_WITH_INTERFACE_SDK

} // namespace
} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                 WINDOWED SESSION (SDK)
//------------------------------------------------------------------------------------------------------------------------

// 🚧 The windowed path compiles only with the interface SDK present (Engine/Interface + WindowsApplication +
//    vendored ImGui/GLFW from the upstream checkout): define FRONTIER_WITH_INTERFACE_SDK and link those units.
//    It is written against the upstream contracts (SultanAladin/Frontier branch `Frontier` @9ec758d):
//    the project window composes exactly like TextureWorkspace (WindowsTextureStudio.cpp) — a
//    ControlCentrePanel (trapezoidal expansion-notch tab, appearance, fonts) over a PanelDockspace the
//    host owns, with NO project-summary header band. Compile-proved against the upstream headers
//    (Exhibits/LogStream/CompileProof.md); it has not run as an OS window in this environment.
#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/LogStreamPanelRegistration.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Interface/SceneDirectoryPanelRegistration.h"
#include "Engine/Interface/SceneInspectorPanel.h"
#include "Engine/Scene/EnvironmentRecords.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/SceneRecord.h"
#include "Engine/Interface/WindowsApplication.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <fstream>
#include <memory>

namespace Frontier
{
namespace
{

struct BrowserSession
{
    GLFWwindow*               Window = nullptr;
    ProjectBrowserEnumeration Listing;
    std::filesystem::path     Chosen;
};

struct WorkspaceSession
{
    GLFWwindow*                        Window = nullptr;
    std::unique_ptr<ControlCentrePanel> Centre;          // Notch tab + appearance + fonts (TextureStudio pattern)
    PanelDockspace                     Panels;           // Viewport | (Outliner over Log Stream), columns 50/50
    LogStreamPanel                     LogPanel;
    SceneDirectoryPanel                OutlinerPanel;
    // 📝 Ported environment records: construction registers them in the Outliner; in the editor
    //    profile each also builds its own Inspector description (Sun/sky/clouds/fog/wind scene).
    SunRecord                          Sun;
    AtmosphereRecord                   Atmosphere;
    CloudRecord                        Clouds;
    FogRecord                          Fog;
    WindRecord                         Wind;
    std::vector<std::unique_ptr<SceneRecord>> SpawnedRecords;   // Editor-spawned records live with the session
    std::string                        ProjectName;
    std::string                        ProjectLocation;  // [utf8]
    std::filesystem::path              LayoutPath;
    float                              InterfaceScale = 1.0f;
    bool                               Prepared        = false;
};

// The Viewport panel until the Phase 3 renderer records into it: the same honest placeholder the
// upstream editor shows for its Scene Canvas. The portable frame units it will present (render graph,
// barriers, target atlas, the Proving Grounds scene) are validated headlessly in
// Exhibits/RenderInfrastructure; nothing here fakes a rendered frame.
void ConstructViewportPanel(const WorkspaceSession& Session)
{
    const ImGuiStyle& Style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 24));
    ImGui::TextColored(Style.Colors[ImGuiCol_TextDisabled], "SCENE CANVAS");
    ImGui::Spacing();
    ImGui::TextWrapped("The VehicleHMI viewport connects here: the ReSTIR renderer port (Phase 3) records "
                       "into this panel through VulkanPresentation. Native scene rendering is not connected "
                       "yet — this surface will present the project document once the SDK build lands.");
    ImGui::Separator();
    const auto Row = [](const char* Label, const std::string& Value) {
        ImGui::TextDisabled("%s", Label);
        ImGui::SameLine(150);
        ImGui::TextWrapped("%s", Value.empty() ? "\u2014" : Value.c_str());
    };
    Row("Project", Session.ProjectName);
    Row("Location", Session.ProjectLocation);
    ImGui::PopStyleVar();
}

int RunBrowserWindow(const std::filesystem::path& FromExecutable, std::filesystem::path& ChosenDescriptor)
{
    BrowserSession Session;
    Session.Listing = EnumerateProjects(ResolveProjectsDirectory(FromExecutable));

    ApplicationWindowCallbacks Callbacks;
    Callbacks.Title      = "Frontier · Project Browser";
    Callbacks.Initialize = [&Session](GLFWwindow* Window) { Session.Window = Window; };
    Callbacks.ConstructInterface = [&Session](bool) {
        const ImVec2 Display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(Display);
        if (ImGui::Begin("Project Browser", nullptr, ImGuiWindowFlags_NoDecoration))
        {
            ImGui::TextUnformatted("Open a project");
            ImGui::Separator();
            for (const ProjectBrowserEntry& Entry : Session.Listing.Entries)
            {
                if (ImGui::Button(Entry.Descriptor.Name.c_str(), ImVec2(420.0f, 0.0f)))
                {
                    Session.Chosen = Entry.Descriptor.DescriptorPath;
                    // 📝 The browser window closes behind the project window it launches.
                    glfwSetWindowShouldClose(Session.Window, GLFW_TRUE);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", EncodeUtf8Path(Entry.Descriptor.ProjectDirectory).c_str());
            }
            for (const std::string& Diagnostic : Session.Listing.Diagnostics)
            {
                ImGui::TextDisabled("skipped: %s", Diagnostic.c_str());
            }
        }
        ImGui::End();
    };

    const int Result = RunWindowsApplication(Callbacks);
    ChosenDescriptor = Session.Chosen;
    return Result;
}

int RunProjectWindow(const ProjectDescriptor& Descriptor, const std::filesystem::path& FromExecutable)
{
    (void)FromExecutable;

    WorkspaceSession Session;
    Session.ProjectName     = Descriptor.Name;
    Session.ProjectLocation = EncodeUtf8Path(Descriptor.ProjectDirectory);
    Session.LayoutPath      = Descriptor.ProjectDirectory / "Saved" / "WorkspaceLayout.toml";

    ApplicationWindowCallbacks Callbacks;
    Callbacks.Title            = "Frontier · " + Descriptor.Name;
    Callbacks.Initialize       = [&Session](GLFWwindow* Window) { Session.Window = Window; };
    Callbacks.InitializeVulkan = [](VulkanPresentation&) {
        WriteLog(LogCategories().QueryByName("Render") != nullptr ? LogCategories().QueryByName("Render")->Token : 0,
                 LogSeverity::Info, LogVerbosity::Log, "Vulkan presentation initialising", "FrontierMain.cpp");
    };
    Callbacks.ConfigurePlatform = [&Session]() { Session.Panels.ConfigurePlatform(); };
    Callbacks.RecoverWindows    = [&Session]() { Session.Panels.RecoverPlatformWindows(); };
    Callbacks.PrepareInterface  = [&Session, &Descriptor]() {
        if (Session.Prepared)
        {
            return;
        }
        Session.Prepared = true;

        Session.Centre = std::make_unique<ControlCentrePanel>(
            "EngineContent/FontArchives", Descriptor.ProjectDirectory / "Saved" / "ControlCentre.toml",
            Descriptor.ProjectDirectory);
        (void)Session.Centre->Initialize();
        (void)Session.Centre->Activate();

        Session.Panels.ConfigureDockspaceKey("Frontier project panels");
        DockPanelSpecification ViewportPanel;
        ViewportPanel.Key              = "Viewport";
        ViewportPanel.Caption          = "Viewport";
        ViewportPanel.ConstructContent = [&Session]() { ConstructViewportPanel(Session); };
        ViewportPanel.Open             = true;
        (void)Session.Panels.Register(ViewportPanel);
        (void)Session.Panels.Register(MakeLogStreamPanelSpecification(Session.LogPanel));
        (void)Session.Panels.Register(MakeSceneDirectoryPanelSpecification(Session.OutlinerPanel));
        DockPanelSpecification InspectorPanel;
        InspectorPanel.Key              = "Inspector";
        InspectorPanel.Caption          = "Inspector";
        InspectorPanel.ConstructContent = [&Session]() { ConstructSceneInspectorInterface(Session.OutlinerPanel); };
        InspectorPanel.Open             = true;
        (void)Session.Panels.Register(InspectorPanel);
        (void)SceneDirectory::Instance().ConfigureName(Session.Sun.Token(), "Sun");
        (void)SceneDirectory::Instance().ConfigureName(Session.Atmosphere.Token(), "Atmosphere");
        (void)SceneDirectory::Instance().ConfigureName(Session.Clouds.Token(), "Clouds");
        (void)SceneDirectory::Instance().ConfigureName(Session.Fog.Token(), "Fog");
        (void)SceneDirectory::Instance().ConfigureName(Session.Wind.Token(), "Wind");
        Session.Panels.ConfigureWorkspaceInsets(79.0f, 0.0f);   // [px] - Top band for the notch tab (TextureStudio)
        Session.Panels.ConfigureTabExtent(140.0f, 26.0f);       // [px]
        // Outliner stacked over the Log Stream in the right column; columns stay 50/50.
        (void)Session.Panels.ConfigureStackedPanels("Outliner", "LogStream", 420.0f);   // [px] upper height
        (void)Session.Panels.ConfigureColumns({{"Viewport", 0.5f}, {"LogStream", 0.5f}});   // 50/50 split

        std::error_code Error;
        if (std::filesystem::is_regular_file(Session.LayoutPath, Error))
        {
            try
            {
                const toml::table Saved = toml::parse_file(EncodeUtf8Path(Session.LayoutPath));
                std::string       Diagnostic;
                Session.Panels.ConfigureLayout(Saved, Diagnostic);
            }
            catch (const toml::parse_error&)
            {
                Session.Panels.ResetLayout();
            }
        }
        // 🚧 Project code-image loading (LoadLibrary + ConstructProjectInterchange negotiation + per-frame
        //    AdvanceProject) lands with the host SDK header; the descriptor's declared categories are already
        //    registered by PrepareProjectSession either way.
    };
    Callbacks.ConstructInterface = [&Session](bool) {
        if (!Session.Centre)
        {
            return;
        }
        const ImGuiIO& InputExchange = ImGui::GetIO();
        const double   Δτ            = InputExchange.DeltaTime;   // [s]

        Session.LogPanel.Synchronize(LogStore(), LogCategories(), Δτ);

        // Service Outliner spawn requests: the panel asks, the host creates the record (it then appears
        //    in the directory by construction). Spawned placeholders live for the session.
        while (const std::optional<RecordClassification> Spawn = Session.OutlinerPanel.TakeSpawnRequest())
        {
            auto Record = std::make_unique<SceneRecord>(*Spawn);
            (void)SceneDirectory::Instance().ConfigureName(
                Record->Token(), std::string(RecordClassificationName(*Spawn)) + " (spawned)");
            Session.SpawnedRecords.push_back(std::move(Record));
        }
        Session.OutlinerPanel.Synchronize(SceneDirectory::Instance());

        Session.Centre->PrepareInterface();
        const float InterfacePercent = Session.Centre->QueryAppearance().QueryApplied().InterfaceScale;   // [%]
        Session.InterfaceScale       = std::clamp(InterfacePercent / 100.0f, 0.6f, 2.0f);
        (void)Session.Centre->Integrate(Δτ);
        Session.Panels.ConstructInterface(Session.InterfaceScale);
        Session.Centre->ConstructInterface();   // Trapezoidal notch tab + drawer, drawn over the panels
    };
    Callbacks.Finalize = [&Session]() {
        std::error_code Error;
        std::filesystem::create_directories(Session.LayoutPath.parent_path(), Error);
        std::ofstream Out(Session.LayoutPath, std::ios::trunc);
        if (Out.is_open())
        {
            Out << Session.Panels.EncodeLayout();
        }
    };

    return RunWindowsApplication(Callbacks);
}

} // namespace
} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;

    const LaunchRequest         Request   = DecodeCommandLine(ArgumentCount, Arguments);
    const std::filesystem::path FromExecutable = ExecutableDirectory(ArgumentCount > 0 ? Arguments[0] : nullptr);

    switch (Request.Mode)
    {
        case LaunchMode::Help:
            std::printf("%s", LaunchUsageText());
            return 0;

        case LaunchMode::Invalid:
            std::fprintf(stderr, "Frontier: %s\n%s", Request.Diagnostic.c_str(), LaunchUsageText());
            return 2;

        case LaunchMode::Headless:
            return RunHeadlessSession(Request.ProjectDescriptor, FromExecutable);

        case LaunchMode::Project:
        {
#if defined(FRONTIER_WITH_INTERFACE_SDK)
            ProjectDescriptor Descriptor;
            if (!PrepareProjectSession(Request.ProjectDescriptor, FromExecutable, Descriptor))
            {
                return 1;
            }
            return RunProjectWindow(Descriptor, FromExecutable);
#else
            std::printf("Frontier: built without the interface SDK; running the headless diagnostic session instead.\n");
            return RunHeadlessSession(Request.ProjectDescriptor, FromExecutable);
#endif
        }

        case LaunchMode::Browser:
        {
#if defined(FRONTIER_WITH_INTERFACE_SDK)
            std::filesystem::path Chosen;
            const int             BrowserResult = RunBrowserWindow(FromExecutable, Chosen);
            if (BrowserResult != 0 || Chosen.empty())
            {
                return BrowserResult;
            }
            ProjectDescriptor Descriptor;
            if (!PrepareProjectSession(Chosen, FromExecutable, Descriptor))
            {
                return 1;
            }
            return RunProjectWindow(Descriptor, FromExecutable);
#else
            return RunBrowserListing(FromExecutable);
#endif
        }
    }
    return 0;
}
