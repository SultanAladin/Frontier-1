/*==========================================================================================================================================
                                                           WINDOWSFRONTIER.CPP
==========================================================================================================================================*/
// 🧩 Run the native Project Browser; opening a project activates the editor workspace for that project.

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#ifndef FRONTIER_BUILD_COMMIT
#define FRONTIER_BUILD_COMMIT "local"
#endif
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/WindowsApplication.h"
#include "Engine/Interface/VulkanPresentation.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Render/BrowserPreviewRendering.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/Editor/EditorWorkspace.h"
#include "UI/Editor/ProjectBrowserPanel.h"
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <objbase.h>
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <map>
#include <string>
#include <vector>

//------------------------------------------------------------------------------------------------------------------------
//                                                     PROJECT BROWSER APPLICATION
//------------------------------------------------------------------------------------------------------------------------

std::filesystem::path ChooseProjectPath(GLFWwindow *Window, bool Save)
{
    wchar_t       Filename[32768] = {};
    OPENFILENAMEW Selection{};
    Selection.lStructSize  = sizeof(Selection);
    Selection.hwndOwner    = glfwGetWin32Window(Window);
    Selection.lpstrFilter  = L"Frontier projects (*.projectspace)\0*.projectspace\0\0";
    Selection.lpstrFile    = Filename;
    Selection.nMaxFile    = 32768;
    Selection.lpstrDefExt = L"projectspace";
    Selection.Flags       = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                            (Save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const bool Accepted = Save ? GetSaveFileNameW(&Selection) != FALSE : GetOpenFileNameW(&Selection) != FALSE;
    return Accepted ? std::filesystem::path(Filename) : std::filesystem::path{};
}

std::vector<std::filesystem::path> ChooseImportFiles(GLFWwindow *Window)
{
    // 📎 Multi-select import picker; the prototype slices payloads to 200 files downstream.
    std::vector<wchar_t> Filenames(32768, L'\0');
    OPENFILENAMEW Selection{};
    Selection.lStructSize  = sizeof(Selection);
    Selection.hwndOwner    = glfwGetWin32Window(Window);
    Selection.lpstrFilter  = L"Supported files (*.png;*.jpg;*.jpeg;*.webp;*.md)\0*.png;*.jpg;*.jpeg;*.webp;*.md\0\0";
    Selection.lpstrFile    = Filenames.data();
    Selection.nMaxFile     = DWORD(Filenames.size());
    Selection.Flags        = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT |
                             OFN_EXPLORER;
    if (GetOpenFileNameW(&Selection) == FALSE) return {};
    std::vector<std::filesystem::path> Paths;
    const wchar_t *Cursor = Filenames.data();
    const std::wstring Head(Cursor);
    Cursor += Head.size() + 1;
    if (*Cursor == L'\0') Paths.emplace_back(Head); // 📎 Single selection returns the full path.
    else
        while (*Cursor != L'\0')
        {
            Paths.emplace_back(std::filesystem::path(Head) / Cursor);
            Cursor += wcslen(Cursor) + 1;
        }
    return Paths;
}

std::filesystem::path ChooseImportFolder(GLFWwindow *Window)
{
    wchar_t Display[MAX_PATH] = {};
    BROWSEINFOW Selection{};
    Selection.hwndOwner = glfwGetWin32Window(Window);
    Selection.pszDisplayName = Display;
    Selection.lpszTitle = L"Select a folder to import supported files from";
    Selection.ulFlags = BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
    if (const PIDLIST_ABSOLUTE Chosen = SHBrowseForFolderW(&Selection))
    {
        wchar_t Folder[MAX_PATH] = {};
        const bool Resolved = SHGetPathFromIDListW(Chosen, Folder) != FALSE;
        CoTaskMemFree(Chosen);
        if (Resolved) return std::filesystem::path(Folder);
    }
    return std::filesystem::path{};
}

int main()
{
    Frontier::DiagnosticJournal::Initialize("Frontier");
    Frontier::DiagnosticJournal::Append("Startup", "Frontier Project Browser with editor workspace activation");
    Frontier::ActivationOwnership AppearanceOwnership;
    Frontier::ControlCentrePanel *Appearance = nullptr;
    std::unique_ptr<Frontier::ProjectBrowserPanel> Browser;
    std::unique_ptr<Frontier::EditorWorkspace> Workspace;
    std::unique_ptr<Frontier::BrowserPreviewRendering> PreviewRendering;
    std::vector<Frontier::BrowserCatalogueEntry> PreviewCatalogue;
    std::map<VkImageView, VkDescriptorSet> PreviewDescriptors;
    VkImageView LastLiveView = VK_NULL_HANDLE;
    bool PreviewFailed = false;
    // 📝 Stored browser lambdas capture these by reference across frames; InitializeVulkan-scope
    // closures dangle after setup returns and AV on first touch (frame-2 tile resolve).
    const auto Describe = [&](VkImageView View) -> VkDescriptorSet
    {
        if (!View) return VK_NULL_HANDLE;
        const auto Cached = PreviewDescriptors.find(View);
        if (Cached != PreviewDescriptors.end()) return Cached->second;
        // 📝 Pooled target extents keep their images alive until Finalize, so lazily retained
        // descriptors stay valid without ever freeing a set a previous frame still samples.
        const auto Descriptor = ImGui_ImplVulkan_AddTexture(View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        PreviewDescriptors.emplace(View, Descriptor);
        return Descriptor;
    };
    const auto NoteFailure = [&](const char *Stage, const std::exception &Failure)
    {
        if (PreviewFailed) return;
        PreviewFailed = true; // 📝 Device loss fails every frame; journal the first refusal only.
        Frontier::DiagnosticJournal::Append("BrowserPreviewRefused", std::string(Stage) + ": " + Failure.what());
    };
    GLFWwindow *Window = nullptr;
    std::filesystem::path Preferences;
    bool ReturnRequested = false;
    Frontier::ApplicationWindowCallbacks Callbacks;
    Callbacks.Title = "Frontier - Project Browser [" FRONTIER_BUILD_COMMIT "]";
    Callbacks.Initialize = [&](GLFWwindow *Current)
    {
        Window = Current;
        Frontier::IconPresentation::Attach("EngineContent/Icons");
        const auto *Local = _wgetenv(L"LOCALAPPDATA");
        Preferences = Local ? std::filesystem::path(Local) / "Frontier/Personal.projectspace" : std::filesystem::path{};
        AppearanceOwnership.reset(new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences));
        Appearance = static_cast<Frontier::ControlCentrePanel *>(AppearanceOwnership.get());
        if (!Appearance->Initialize() || !Appearance->Activate())
            throw std::runtime_error("Frontier appearance initialization failed: " + Appearance->QueryDiagnostic());
        Browser = std::make_unique<Frontier::ProjectBrowserPanel>([&](bool Save) { return ChooseProjectPath(Window, Save); });
        PreviewCatalogue = Frontier::ConstructBrowserCatalogue();
        Browser->PickImportFiles = [&]() { return ChooseImportFiles(Window); };
        Browser->PickImportFolder = [&]() { return ChooseImportFolder(Window); };
        Frontier::DiagnosticJournal::Append("Startup", "Project Browser initialized");
    };
    Callbacks.InitializeVulkan = [&](Frontier::VulkanPresentation &Display)
    {
        std::filesystem::path ShaderDirectory = "EngineContent/ShaderArchives/Surface";
        if (!std::filesystem::exists(ShaderDirectory))
        {
            wchar_t Executable[MAX_PATH] = {};
            if (GetModuleFileNameW(nullptr, Executable, MAX_PATH))
                ShaderDirectory = std::filesystem::path(Executable).parent_path() / ShaderDirectory;
        }
        PreviewRendering = std::make_unique<Frontier::BrowserPreviewRendering>();
        PreviewRendering->Initialize(Display.QueryRasterAccess(), ShaderDirectory);
        Browser->QueryThumbnailImage = [&](const std::string &Key)
        {
            Frontier::BrowserPreviewImage Image;
            try
            {
                const auto ViewCut = Key.rfind(':');
                const auto RevisionCut = ViewCut == std::string::npos ? std::string::npos : Key.rfind(':', ViewCut - 1);
                if (ViewCut == std::string::npos || RevisionCut == std::string::npos) return Image;
                const std::string Token = Key.substr(0, RevisionCut);
                const size_t Revision = std::stoul(Key.substr(RevisionCut + 1, ViewCut - RevisionCut - 1));
                const size_t View = std::stoul(Key.substr(ViewCut + 1));
                const auto Entry = std::find_if(PreviewCatalogue.begin(), PreviewCatalogue.end(),
                    [&](const auto &Candidate) { return Candidate.Token == Token; });
                if (Entry == PreviewCatalogue.end()) return Image;
                const auto Configuration = Frontier::ConfigureBrowserPreview(*Entry, View, Revision);
                PreviewRendering->RequestThumbnail(Key, Configuration);
                if (!PreviewRendering->QueryThumbnail(Key).Ready) return Image;
                static int TracedReady = 0;
                if (TracedReady < 4)
                    Frontier::DiagnosticJournal::Append("BrowserTileReady", "key=" + Key);
                Image.Texture = (uint64_t)Describe(PreviewRendering->QueryThumbnailView(Key));
                if (TracedReady < 4)
                {
                    Frontier::DiagnosticJournal::Append("BrowserTileDescribed", "key=" + Key);
                    ++TracedReady;
                }
            }
            catch (const std::exception &Failure) { NoteFailure("QueryThumbnailImage", Failure); }
            return Image;
        };
        Browser->QueryLiveImage = [&]()
        {
            Frontier::BrowserPreviewImage Image;
            try
            {
                // Hold the last completed frame while the live target re-renders: the queue
                // samples after the browser submit, so the current view always holds fresh pixels
                // and only a recreated target (or the never-rendered first frames) reports null.
                if (PreviewRendering->QueryLiveReady()) LastLiveView = PreviewRendering->QueryLiveView();
                const auto Current = PreviewRendering->QueryLiveView();
                if (Current && Current == LastLiveView)
                    Image.Texture = (uint64_t)Describe(Current);
            }
            catch (const std::exception &Failure) { NoteFailure("QueryLiveImage", Failure); }
            return Image;
        };
        Browser->ConfigureLivePreview = [&](const Frontier::BrowserPreviewConfiguration &Configuration,
                                            const Frontier::BrowserPreviewCamera &Camera, int Channel, int Width,
                                            int Height)
        {
            try
            {
                PreviewRendering->ConfigureLive(Configuration, Camera, Channel, Width, Height);
            }
            catch (const std::exception &Failure) { NoteFailure("ConfigureLivePreview", Failure); }
        };
        Browser->CaptureLiveThumbnail = [&](const std::string &Key, const Frontier::BrowserPreviewConfiguration &Configuration,
                                           const Frontier::BrowserPreviewCamera &Camera, int Channel)
        {
            try
            {
                PreviewRendering->CaptureThumbnail(Key, Configuration, Camera, Channel);
            }
            catch (const std::exception &Failure) { NoteFailure("CaptureLiveThumbnail", Failure); }
        };
        Browser->QueryImportedImage = [&](const std::string &Token, const std::vector<uint8_t> &ImageBytes)
        {
            Frontier::BrowserPreviewImage Image;
            try
            {
                PreviewRendering->RequestImportImage(Token, ImageBytes);
                const auto Import = PreviewRendering->QueryImportImage(Token);
                if (!Import.Ready && !Import.Diagnostic.empty()) Image.Failed = true;
                if (!Import.Ready) return Image;
                Image.Texture = (uint64_t)Describe(PreviewRendering->QueryImportView(Token));
                Image.Width = float(Import.Width);
                Image.Height = float(Import.Height);
            }
            catch (const std::exception &Failure) { NoteFailure("QueryImportedImage", Failure); }
            return Image;
        };
        Display.ConfigureRendering(
            [&](VkCommandBuffer Commands, uint64_t Completed, uint64_t Serial)
            {
                try
                {
                    PreviewRendering->ConstructCommands(Commands, Completed, Serial);
                }
                catch (const std::exception &Failure) { NoteFailure("ConstructCommands", Failure); }
            },
            [&]() { PreviewRendering->ConfirmSubmission(); });
        Frontier::DiagnosticJournal::Append("Startup", "Browser preview rendering initialized");
    };
    const auto ActivateProject = [&](const std::filesystem::path &Requested)
    {
        Frontier::ProjectSpaceDocument Document;
        std::string Diagnostic;
        if (!Frontier::ProjectSpaceCodec::Decode(Requested, Document, Diagnostic))
            throw std::runtime_error("Project activation failed: " + Diagnostic);
        Workspace.reset();
        AppearanceOwnership.reset(new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences, Requested));
        Appearance = static_cast<Frontier::ControlCentrePanel *>(AppearanceOwnership.get());
        if (!Appearance->Initialize() || !Appearance->Activate())
            throw std::runtime_error("Project activation failed: " + Appearance->QueryDiagnostic());
        Workspace = std::make_unique<Frontier::EditorWorkspace>(*Appearance);
        Frontier::EditorProjectSummary Summary;
        Summary.Name = Document.Sections["project"]["name"].value_or(std::string("Project"));
        Summary.Location = Frontier::DiagnosticJournal::EncodePath(Requested);
        Summary.Identity = Document.Sections["project"]["identity"].value_or(std::string{});
        if (const auto *Levels = Document.Sections["levels"].as_array()) Summary.Levels = Levels->size();
        if (const auto *Roots = Document.Sections["content_roots"].as_array()) Summary.Roots = Roots->size();
        if (const auto *Catalogue = Document.Sections["catalogue"].as_array()) Summary.Catalogue = Catalogue->size();
        Summary.StartupLevel = Document.Sections["engine"]["startup_level"].value_or(std::string{});
        Workspace->ConfigureProject(std::move(Summary));
        Workspace->EvaluateReturnToBrowser = [&]() { ReturnRequested = true; };
        glfwSetWindowTitle(Window, ("Frontier - " + Workspace->QueryProject().Name).c_str());
        Frontier::DiagnosticJournal::Append("ProjectSelection", Frontier::DiagnosticJournal::EncodePath(Requested));
    };
    Callbacks.PrepareInterface = [&]() { Appearance->PrepareInterface(); };
    Callbacks.RecoverWindows = [&]() { if (Workspace) Workspace->QueryPanels().RecoverPlatformWindows(); };
    Callbacks.ConfigurePlatform = [&]()
    {
        const auto Requested = Browser->ConsumeLaunchPath();
        if (!Requested.empty()) ActivateProject(Requested);
        if (ReturnRequested && Workspace)
        {
            ReturnRequested = false;
            Workspace.reset();
            AppearanceOwnership.reset(new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences));
            Appearance = static_cast<Frontier::ControlCentrePanel *>(AppearanceOwnership.get());
            if (!Appearance->Initialize() || !Appearance->Activate())
                throw std::runtime_error("Project Browser reactivation failed: " + Appearance->QueryDiagnostic());
            Browser->ResetSelection();
            glfwSetWindowTitle(Window, Callbacks.Title.c_str());
            Frontier::DiagnosticJournal::Append("ProjectSelection", "Returned to the Project Browser");
        }
        if (Workspace) (void)Workspace->ConfigurePlatform();
    };
    Callbacks.ConstructInterface = [&](bool MainVisible)
    {
        if (Workspace)
        {
            (void)Appearance->Integrate(ImGui::GetIO().DeltaTime);
            Workspace->ConstructInterface();
            if (MainVisible) Appearance->ConstructInterface();
        }
        else if (MainVisible)
        {
            Browser->ConfigurePalette(Frontier::ControlKit::Palette());
            Browser->ConstructInterface();
        }
    };
    Callbacks.AfterRender = [&]() { if (Workspace) Workspace->IntegrateLayoutPersistence(ImGui::GetIO().DeltaTime); };
    Callbacks.Finalize = [&]()
    {
        // 📝 The shared application loop has retired all GPU work before this callback.
        for (const auto &Entry : PreviewDescriptors)
            if (Entry.second) ImGui_ImplVulkan_RemoveTexture(Entry.second);
        PreviewDescriptors.clear();
        if (PreviewRendering) PreviewRendering->Finalize();
        PreviewRendering.reset();
        Workspace.reset();
        Browser.reset();
        AppearanceOwnership.reset();
        Frontier::DiagnosticJournal::Append("Shutdown", "Frontier application resources reclaimed");
    };
    return Frontier::RunWindowsApplication(Callbacks);
}
