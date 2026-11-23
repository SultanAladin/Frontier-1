/*==========================================================================================================================================
                                                      TEXTURESTUDIOAPPLICATION.CPP
==========================================================================================================================================*/
// 🧩 Integrate native Pattern authoring and the selected-material preview into the shared Vulkan workspace loop.

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#ifndef FRONTIER_BUILD_COMMIT
#define FRONTIER_BUILD_COMMIT "local"
#endif
#include "Engine/Interface/WindowsApplication.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/VulkanPresentation.h"
#include "Engine/ContentInterchange/ProjectSpaceCodec.h"
#include "Engine/Render/MaterialLibraryRendering.h"
#include "Engine/Render/MaterialThumbnailAtlas.h"
#include "Engine/Interface/ThumbnailAtlasCache.h"
#include "Engine/Render/VectorSurfaceMaterialBridge.h"
#include "Engine/Render/VectorAuthoringCoverageRendering.h"
#include "Engine/Render/VectorSelectionEvaluation.h"
#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include "UI/TextureStudio/StampWorkspacePanel.h"
#include <imgui_impl_vulkan.h>
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/TextFileReplacement.h"
#include "Engine/Interface/TypefaceRegistry.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/TextureStudio/MaterialLibraryPanel.h"
#include <array>
#include <deque>
#include <map>
#include <set>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>

//------------------------------------------------------------------------------------------------------------------------
//                                                     STANDALONE APPLICATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
#ifdef _DEBUG
void AppendPatternPreviewDebug(std::string_view Description)
{
    Frontier::DiagnosticJournal::Append("PatternPreviewDebug", Description);
}
#else
void AppendPatternPreviewDebug(std::string_view) noexcept {}
#endif
// 📝 Per-user writable data root: %LOCALAPPDATA% on Windows, XDG_DATA_HOME (falling back to
// ~/.local/share) elsewhere. Mirrors the convention DiagnosticJournal already uses for logs.
std::filesystem::path QueryUserDataDirectory() noexcept
{
#ifdef _WIN32
    if (const auto *Local = _wgetenv(L"LOCALAPPDATA")) return std::filesystem::path(Local);
#else
    if (const auto *Local = std::getenv("XDG_DATA_HOME")) return std::filesystem::path(Local);
    if (const auto *Home = std::getenv("HOME")) return std::filesystem::path(Home) / ".local/share";
#endif
    return {};
}
// 📝 Surface a severe startup/shutdown failure to the user: a native message box on Windows,
// stderr (alongside the DiagnosticJournal entry already written by the caller) elsewhere.
void ReportSevereFailure(const char *Title, const std::string &Message) noexcept
{
#ifdef _WIN32
    MessageBoxA(nullptr, Message.c_str(), Title, MB_OK | MB_ICONERROR | MB_TASKMODAL);
#else
    std::cerr << Title << ": " << Message << '\n';
#endif
}
bool SameFileContent(const std::filesystem::path &First, const std::filesystem::path &Second) noexcept
{
    try
    {
        std::error_code FirstError, SecondError;
        const auto FirstBytes = std::filesystem::file_size(First, FirstError);
        const auto SecondBytes = std::filesystem::file_size(Second, SecondError);
        if (FirstError || SecondError || FirstBytes != SecondBytes) return false;
        std::ifstream A(First, std::ios::binary), B(Second, std::ios::binary);
        if (!A || !B) return false;
        std::array<char, 8192> BlockA{}, BlockB{};
        for (;;)
        {
            A.read(BlockA.data(), std::streamsize(BlockA.size()));
            B.read(BlockB.data(), std::streamsize(BlockB.size()));
            const auto CountA = A.gcount(), CountB = B.gcount();
            if (CountA != CountB) return false;
            if (!CountA) return true;
            for (std::streamsize At = 0; At < CountA; ++At)
                if (BlockA[size_t(At)] != BlockB[size_t(At)]) return false;
        }
    }
    catch (...) { return false; }
}
}

int RunTextureWorkspace(int ArgumentCount, char **Arguments)
{
    Frontier::DiagnosticJournal::Append("Startup", "Texture Workspace: Material Library / Preview / Inspector / experimental Pattern preview");
#ifdef _DEBUG
    AppendPatternPreviewDebug("Debug-only Pattern GPU tracing active; see the DiagnosticJournal log directory for TextureWorkspace-*.log");
#endif
    std::filesystem::path Directory;
    std::filesystem::path ShaderDirectory = "EngineContent/ShaderArchives/Surface";
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        if (std::string_view(Arguments[Index]) == "--library" && Index+1 < ArgumentCount) Directory = Arguments[++Index];
        else if (std::string_view(Arguments[Index]) == "--shaders" && Index+1 < ArgumentCount) ShaderDirectory = Arguments[++Index];
        else
        {
            std::cerr << "Usage: TextureWorkspace [--library ContentDirectory] [--shaders ShaderDirectory]\n";
            return 2;
        }
    }
    // 📝 Reuse the same portable executable-path resolution every other Frontier content lookup
    // already relies on (Windows/Linux/macOS), rather than hand-rolling it again here.
    const auto EngineContentDirectory = Frontier::ResolveContentDirectory("EngineContent");
    const auto ExecutableDirectory = EngineContentDirectory.empty() ?
        std::filesystem::current_path() : EngineContentDirectory.parent_path();
    if (!std::filesystem::exists(ShaderDirectory) && ShaderDirectory == "EngineContent/ShaderArchives/Surface")
        ShaderDirectory = ExecutableDirectory / ShaderDirectory;
    const auto ContentRoot = std::filesystem::exists("EngineContent/FontArchives") ? std::filesystem::current_path() : ExecutableDirectory;
    Frontier::DiagnosticJournal::Append("Paths", "Executable=" + Frontier::DiagnosticJournal::EncodePath(ExecutableDirectory));
    Frontier::DiagnosticJournal::Append("Paths", "Content=" + Frontier::DiagnosticJournal::EncodePath(ContentRoot) + " shaders=" + Frontier::DiagnosticJournal::EncodePath(ShaderDirectory));
    Frontier::ActivationOwnership AppearanceOwnership, LibraryOwnership, PatternOwnership, StampOwnership;
    Frontier::ControlCentrePanel *Appearance = nullptr;
    Frontier::MaterialLibraryPanel *Library = nullptr;
    Frontier::PatternWorkspacePanel *Pattern = nullptr;
    Frontier::StampWorkspacePanel *Stamp = nullptr;
    GLFWwindow *Window = nullptr;
    std::unique_ptr<Frontier::MaterialLibraryRendering> Rendering;
    std::unique_ptr<Frontier::VectorSurfaceMaterialBridge> MaterialBridge;
    std::unique_ptr<Frontier::VectorAuthoringCoverageRendering> CoverageRendering;
    std::optional<std::array<Frontier::MaterialTexturePyramid, 6>> PatternAuthoringMaps;
    bool PatternApplyRequested = false;
    Frontier::VectorAuthoringDocument SubmittedPatternDocument;
    std::optional<Frontier::VectorAuthoringDocument> ObservedPatternDocument;
    const std::filesystem::path PatternPreviewPath = "Pattern/live-preview";
    uint64_t PatternBridgeSubmission = 0;
    bool PatternBridgeRecorded = false;
    // 📝 Stamp shares the one GPU Float32 authoring-map bridge below with Pattern (mutually exclusive
    // workspaces never edit at once); its own document/submission bookkeeping stays separate so a
    // Stamp bake can never be mistaken for a Pattern one or vice versa.
    std::optional<std::array<Frontier::MaterialTexturePyramid, 6>> StampAuthoringMaps;
    bool StampApplyRequested = false;
    Frontier::VectorAuthoringDocument SubmittedStampDocument;
    std::optional<Frontier::VectorAuthoringDocument> ObservedStampDocument;
    const std::filesystem::path StampPreviewPath = "Stamp/live-preview";
    uint64_t StampBridgeSubmission = 0;
    bool StampBridgeRecorded = false;
    std::optional<Frontier::VectorAuthoringDocument> StampBridgeFaultedDocument;
    std::unique_ptr<Frontier::VectorSelectionEvaluation> SelectionEvaluation;
    std::unique_ptr<Frontier::MaterialThumbnailAtlas> PatternAtlas;
    // 📝 Second, smaller atlas: the open document's own per-layer row thumbnails (Pattern layer
    // stack preview), separate from the library gallery atlas above.
    std::unique_ptr<Frontier::MaterialThumbnailAtlas> PatternLayerAtlas;
    VkDescriptorSet PatternLayerAtlasDescriptor = VK_NULL_HANDLE;
    uint64_t PatternLayerAtlasPublished = 0;
    bool PatternLayerAtlasFaulted = false;
    // 📝 Stamp has no library-wide gallery to browse, only its own open document's layer-row
    // thumbnails — one small atlas, mirroring PatternLayerAtlas exactly.
    std::unique_ptr<Frontier::MaterialThumbnailAtlas> StampLayerAtlas;
    VkDescriptorSet StampLayerAtlasDescriptor = VK_NULL_HANDLE;
    uint64_t StampLayerAtlasPublished = 0;
    bool StampLayerAtlasFaulted = false;
    // 📝 At most one workspace's Float32 authoring maps overlay the selected material's preview at
    // once (whichever was most recently Applied); shared bookkeeping avoids any ambiguity about
    // which of Pattern's/Stamp's maps is the one actually showing.
    std::optional<std::array<Frontier::MaterialTexturePyramid, 6>> AssignedWorkspaceMaps;
    std::filesystem::path AssignedWorkspaceSelection;
    std::filesystem::path AssignedWorkspacePreviewPath;
    VkDescriptorSet AtlasDescriptor = VK_NULL_HANDLE, PreviewDescriptor = VK_NULL_HANDLE;
    // 📝 Display-size preview: superseded preview descriptors stay alive for in-flight frames, then are
    // reclaimed a few frames later instead of accumulating in the shared ImGui descriptor pool until
    // shutdown (that accumulation exhausted the pool: VK_ERROR_OUT_OF_POOL_MEMORY while editing).
    std::deque<std::pair<VkDescriptorSet, uint64_t>> RetiredPreviewDescriptors;
    uint64_t InterfaceFrame = 0;
    // 📝 Fault isolation: a failing GPU subsystem is journalled once and latched off (keyed where a
    // natural retry key exists) instead of aborting the whole application or retrying every frame.
    std::set<std::string> ReportedGpuFaults;
    const auto JournalGpuFaultOnce = [&](const char *Subsystem, const std::exception &Failure)
    {
        const std::string Key = std::string(Subsystem) + ": " + Failure.what();
        if (ReportedGpuFaults.insert(Key).second) Frontier::DiagnosticJournal::Append("GpuFaultIsolated", Key);
    };
    bool SelectionEvaluationFaulted = false, CoverageRenderingFaulted = false, PatternAtlasFaulted = false;
    std::optional<Frontier::VectorAuthoringDocument> PatternBridgeFaultedDocument;
    std::string MaterialRenderingFaultKey;
    VkImageView LastPreviewView = VK_NULL_HANDLE;
    VkDescriptorSet PatternAtlasDescriptor = VK_NULL_HANDLE;
    uint64_t PatternAtlasPublished = 0;
    // 📝 Environment sphere-preview atlas: CPU pixels staged once per catalogue generation.
    std::unique_ptr<Frontier::MaterialThumbnailAtlas> EnvironmentAtlas;
    VkDescriptorSet EnvironmentAtlasDescriptor = VK_NULL_HANDLE;
    uint64_t EnvironmentAtlasPublished = 0;
    bool EnvironmentAtlasFaulted = false;
    std::map<std::pair<size_t, uint32_t>, VkDescriptorSet> CoverageDescriptors;
    std::filesystem::path PatternSource;
    std::filesystem::path StampSource;
    // 📝 At most one of these is ever true; both false means the Material Library is on screen.
    bool PatternActive = false;
    bool StampActive = false;
    uint64_t RenderingRevision = UINT64_MAX, LayoutResetRevision = 0;
    bool LayoutBlocked = false;
    Frontier::ApplicationWindowCallbacks Callbacks;
    Callbacks.Title = "TextureWorkspace - Material Library [" FRONTIER_BUILD_COMMIT "]";
    Callbacks.MaterialRendering = true;
    Callbacks.Initialize = [&](GLFWwindow *CurrentWindow)
    {
        Window = CurrentWindow;
        Frontier::IconPresentation::Attach(Frontier::EncodeContentPath(ContentRoot / "EngineContent/Icons"));
        const auto LocalDirectory = QueryUserDataDirectory();
        const auto Preferences = LocalDirectory.empty() ? std::filesystem::path{} : LocalDirectory / "Frontier/Personal.projectspace";
        AppearanceOwnership.reset(new Frontier::ControlCentrePanel(ContentRoot / "EngineContent/FontArchives", Preferences));
        Appearance = static_cast<Frontier::ControlCentrePanel *>(AppearanceOwnership.get());
        Appearance->ConfigureContentBrowserVisibility(false);
        if (!Appearance->Initialize() || !Appearance->Activate()) throw std::runtime_error(Appearance->QueryDiagnostic());
        LibraryOwnership.reset(new Frontier::MaterialLibraryPanel());
        Library = static_cast<Frontier::MaterialLibraryPanel *>(LibraryOwnership.get());
        Library->PresentIcon = [](Frontier::IconSymbol Symbol, Frontier::PlanePoint Point, float Size, Frontier::ColorQuad Tint)
        {
            return Frontier::IconPresentation::Draw(ImGui::GetWindowDrawList(), Symbol, ImVec2(Point.X, Point.Y), Size, ImGui::GetStyle().Alpha,
                Frontier::IconDomain::Texture, ImGui::ColorConvertFloat4ToU32(ImVec4(Tint.Red, Tint.Green, Tint.Blue, Tint.Alpha)));
        };
        if (!Library->Initialize() || !Library->Activate()) throw std::runtime_error("Material Library activation failed.");
        if (auto *Registry = Frontier::TypefaceRegistry::QueryCurrent())
        {
            const int32_t Family = Registry->ResolveFamilyIndex("DM Sans");
            if (Family >= 0) Library->ConfigureTypeface(Registry->QueryHandle(uint32_t(Family), Frontier::FontWeightCategory::Regular),
                Registry->QueryHandle(uint32_t(Family), Frontier::FontWeightCategory::Light));
        }
        if (Directory.empty())
        {
            const auto Presets = ContentRoot / "EngineContent/MaterialArchives/TextureStudio";
            Directory = LocalDirectory.empty() ? Presets : LocalDirectory / "Frontier/TextureStudio/Materials";
            if (Directory != Presets)
            {
                // 📝 Mirror the shipped preset tree (materials, Textures/ maps, presets) with skip_existing, so
                // fresh installs resolve texture files and existing installs backfill missing additions
                // without touching user-edited materials.
                std::filesystem::create_directories(Directory);
                size_t Copied = 0, Scanned = 0, Refreshed = 0;
                for (const auto &Entry : std::filesystem::recursive_directory_iterator(Presets,
                    std::filesystem::directory_options::skip_permission_denied))
                {
                    if (!Entry.is_regular_file() || Copied + Refreshed >= 2048) continue;
                    const auto Relative = Entry.path().lexically_relative(Presets);
                    if (Relative.empty() || *Relative.begin() == "..") continue;
                    const auto Target = Directory / Relative;
                    std::error_code CopyError;
                    std::filesystem::create_directories(Target.parent_path(), CopyError);
                    if (CopyError) continue;
                    ++Scanned;
                    if (std::filesystem::exists(Target, CopyError) && !CopyError)
                    {
                        // 📝 skip_existing froze user libraries at their first seed, so shipped fixes (phase-1b
                        // texture authoring) never arrived. Refresh changed files, keeping a versioned backup of
                        // the displaced user copy so no edit is silently lost.
                        if (SameFileContent(Entry.path(), Target)) continue;
                        const std::string Stem = Target.filename().string() + ".user-backup";
                        std::filesystem::path Backup = Target.parent_path() / Stem;
                        for (int Version = 2; Version <= 8 && std::filesystem::exists(Backup, CopyError); ++Version)
                            Backup = Target.parent_path() / (Stem + "-" + std::to_string(Version));
                        std::filesystem::rename(Target, Backup, CopyError);
                        if (CopyError) continue;
                        if (!std::filesystem::copy_file(Entry.path(), Target, CopyError) || CopyError) continue;
                        ++Refreshed;
                        Frontier::DiagnosticJournal::Append("MaterialRefresh", Frontier::DiagnosticJournal::EncodePath(Relative));
                        continue;
                    }
                    if (CopyError) continue;
                    if (std::filesystem::copy_file(Entry.path(), Target, CopyError) && !CopyError) ++Copied;
                }
                Frontier::DiagnosticJournal::Append("MaterialBackfill", "scanned=" + std::to_string(Scanned) + " copied=" +
                    std::to_string(Copied) + " refreshed=" + std::to_string(Refreshed));
            }
        }
        if (!Library->ConfigureDirectory(Directory)) throw std::runtime_error(Library->QueryLibrary().QueryDiagnostic());
        if (std::filesystem::is_regular_file(Directory / "racing-green.material")) (void)Library->SelectMaterial("racing-green.material");
        PatternOwnership.reset(new Frontier::PatternWorkspacePanel());
        Pattern = static_cast<Frontier::PatternWorkspacePanel *>(PatternOwnership.get());
        if (!Pattern->Initialize() || !Pattern->Activate()) throw std::runtime_error("Pattern workspace activation failed.");
        (void)Pattern->ConfigurePresetDirectory(ContentRoot / "EngineContent/PatternArchives/TextureStudio");
        PatternSource = Directory / "pattern-workspace.vector.toml";
        Pattern->SaveSource = [&](std::string_view Source)
        {
            if (Source.size() > 4000000) return false;
            std::string Diagnostic;
            return Frontier::TextFileReplacement::Finalize(PatternSource, Source, Diagnostic);
        };
        Pattern->LoadSource = [&]() -> std::optional<std::string>
        {
            std::ifstream Input(PatternSource, std::ios::binary | std::ios::ate);
            const auto Bytes = Input.tellg();
            if (!Input || Bytes < 0 || Bytes > 4000000) return std::nullopt;
            std::string Source(size_t(Bytes), '\0');
            Input.seekg(0);
            if (!Input.read(Source.data(), Bytes)) return std::nullopt;
            return Source;
        };
        Pattern->ActivateMaterial = [&]()
        {
            PatternActive = false;
            AppendPatternPreviewDebug("Pattern workspace deactivated; Material Library preview selected.");
        };
        Pattern->ActivateStamp = [&]() { PatternActive = false; StampActive = true; };
        Library->ActivatePattern = [&]()
        {
            PatternActive = true;
            AppendPatternPreviewDebug("Pattern workspace activated; Float32 authoring-map generation enabled.");
        };
        Library->ActivateStamp = [&]() { StampActive = true; };
        if (auto *Registry = Frontier::TypefaceRegistry::QueryCurrent())
        {
            const int32_t Family = Registry->ResolveFamilyIndex("DM Sans");
            if (Family >= 0) Pattern->ConfigureTypeface(Registry->QueryHandle(uint32_t(Family), Frontier::FontWeightCategory::Regular),
                Registry->QueryHandle(uint32_t(Family), Frontier::FontWeightCategory::Light));
        }
        if (std::filesystem::is_regular_file(PatternSource))
        {
            std::ifstream Input(PatternSource, std::ios::binary | std::ios::ate);
            const auto Bytes = Input.tellg();
            if (Input && Bytes >= 0 && Bytes <= 4000000)
            {
                std::string Source(size_t(Bytes), '\0');
                Input.seekg(0);
                if (Input.read(Source.data(), Bytes))
                {
                    try
                    {
                        Pattern->ConfigureDocument(Frontier::DecodeVectorAuthoring(Source));
                    }
                    catch (const std::exception &Failure)
                    {
                        Frontier::DiagnosticJournal::Append("PatternSourceRefused", Failure.what());
                    }
                }
            }
        }
        // 📝 Stamp: same bounded-document lifecycle as Pattern above (own TOML source file, own
        // typeface, own Material/Pattern cross-navigation), just a 24-layer decal document instead
        // of a 64-layer tileable one.
        StampOwnership.reset(new Frontier::StampWorkspacePanel());
        Stamp = static_cast<Frontier::StampWorkspacePanel *>(StampOwnership.get());
        if (!Stamp->Initialize() || !Stamp->Activate()) throw std::runtime_error("Stamp workspace activation failed.");
        Stamp->ConfigureContentRoot(ContentRoot);
        StampSource = Directory / "stamp-workspace.vector.toml";
        Stamp->SaveSource = [&](std::string_view Source)
        {
            if (Source.size() > 4000000) return false;
            std::string Diagnostic;
            return Frontier::TextFileReplacement::Finalize(StampSource, Source, Diagnostic);
        };
        Stamp->LoadSource = [&]() -> std::optional<std::string>
        {
            std::ifstream Input(StampSource, std::ios::binary | std::ios::ate);
            const auto Bytes = Input.tellg();
            if (!Input || Bytes < 0 || Bytes > 4000000) return std::nullopt;
            std::string Source(size_t(Bytes), '\0');
            Input.seekg(0);
            if (!Input.read(Source.data(), Bytes)) return std::nullopt;
            return Source;
        };
        Stamp->ActivateMaterial = [&]() { StampActive = false; };
        Stamp->ActivatePattern = [&]() { StampActive = false; PatternActive = true; };
        if (auto *Registry = Frontier::TypefaceRegistry::QueryCurrent())
        {
            const int32_t Family = Registry->ResolveFamilyIndex("DM Sans");
            if (Family >= 0) Stamp->ConfigureTypeface(Registry->QueryHandle(uint32_t(Family), Frontier::FontWeightCategory::Regular),
                Registry->QueryHandle(uint32_t(Family), Frontier::FontWeightCategory::Light));
        }
        if (std::filesystem::is_regular_file(StampSource))
        {
            std::ifstream Input(StampSource, std::ios::binary | std::ios::ate);
            const auto Bytes = Input.tellg();
            if (Input && Bytes >= 0 && Bytes <= 4000000)
            {
                std::string Source(size_t(Bytes), '\0');
                Input.seekg(0);
                if (Input.read(Source.data(), Bytes))
                {
                    try
                    {
                        auto Document = Frontier::DecodeVectorAuthoring(Source);
                        if (!Document.Stamp) throw std::runtime_error("stamp-workspace.vector.toml did not decode as a Stamp document.");
                        Stamp->ConfigureDocument(std::move(Document));
                    }
                    catch (const std::exception &Failure)
                    {
                        Frontier::DiagnosticJournal::Append("StampSourceRefused", Failure.what());
                    }
                }
            }
        }
        LayoutResetRevision = Appearance->QueryLayoutResetRevision();
        if (!Preferences.empty() && std::filesystem::exists(Preferences))
        {
            Frontier::ProjectSpaceDocument Document;
            std::string Message;
            if (!Frontier::ProjectSpaceCodec::Decode(Preferences, Document, Message)) LayoutBlocked = true;
            else
            {
                if (const auto Layout = Document.Sections["workspace"]["texture_studio_layout"]; Layout)
                    LayoutBlocked = !Layout.is_table() || !Library->QueryPanels().ConfigureLayout(*Layout.as_table(), Message);
                if (!LayoutBlocked)
                    if (const auto Layout = Document.Sections["workspace"]["texture_studio_pattern_layout"]; Layout)
                        LayoutBlocked = !Layout.is_table() || !Pattern->QueryPanels().ConfigureLayout(*Layout.as_table(), Message);
                if (!LayoutBlocked)
                    if (const auto Layout = Document.Sections["workspace"]["texture_studio_stamp_layout"]; Layout)
                        LayoutBlocked = !Layout.is_table() || !Stamp->QueryPanels().ConfigureLayout(*Layout.as_table(), Message);
            }
            if (LayoutBlocked) Appearance->ConfigureLayoutDiagnostic("TextureWorkspace layout read refused; defaults remain session-only. " + Message);
        }
    };
    Callbacks.InitializeVulkan = [&](Frontier::VulkanPresentation &Display)
    {
        Rendering = std::make_unique<Frontier::MaterialLibraryRendering>();
        Rendering->Initialize(Display.QueryRasterAccess(), ShaderDirectory);
        // 📝 Disk-persist the baked gallery atlas so relaunching does not re-render every visible
        // material through the PBR pipeline again; best-effort and silently inert on any failure.
        Rendering->ConfigureAtlasCache(Frontier::QueryFrontierCacheDirectory() / "MaterialAtlas.atlas", 0x4D41544C41533100ull);
        Rendering->ConfigureEnvironments(ContentRoot / "EngineContent/EnvironmentArchives");
        EnvironmentAtlas = std::make_unique<Frontier::MaterialThumbnailAtlas>();
        EnvironmentAtlas->Initialize(Display.QueryRasterAccess(),
            Frontier::MaterialLibraryRendering::EnvironmentPreviewTile,
            Frontier::MaterialLibraryRendering::EnvironmentPreviewColumns);
        EnvironmentAtlasDescriptor = ImGui_ImplVulkan_AddTexture(EnvironmentAtlas->QueryImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        MaterialBridge = std::make_unique<Frontier::VectorSurfaceMaterialBridge>();
        MaterialBridge->Initialize(Display.QueryRasterAccess(), ShaderDirectory / "VectorSurfaceCompute.spv", 512, 512);
        AppendPatternPreviewDebug("Vulkan material bridge initialized; authoring_extent=512x512; channels=Colour,Normal,Roughness,Metalness,Occlusion; shader=" + Frontier::DiagnosticJournal::EncodePath(ShaderDirectory / "VectorSurfaceCompute.spv"));
        AtlasDescriptor = ImGui_ImplVulkan_AddTexture(Rendering->QueryAtlasView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        PreviewDescriptor = ImGui_ImplVulkan_AddTexture(Rendering->QueryPreviewView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        LastPreviewView = Rendering->QueryPreviewView();
        PatternAtlas = std::make_unique<Frontier::MaterialThumbnailAtlas>();
        PatternAtlas->Initialize(Display.QueryRasterAccess(), Frontier::PatternWorkspacePanel::PatternAtlasTile,
            Frontier::PatternWorkspacePanel::PatternAtlasColumns);
        PatternAtlasDescriptor = ImGui_ImplVulkan_AddTexture(PatternAtlas->QueryImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        PatternLayerAtlas = std::make_unique<Frontier::MaterialThumbnailAtlas>();
        PatternLayerAtlas->Initialize(Display.QueryRasterAccess(), Frontier::PatternWorkspacePanel::LayerAtlasTile,
            Frontier::PatternWorkspacePanel::LayerAtlasColumns);
        PatternLayerAtlasDescriptor = ImGui_ImplVulkan_AddTexture(PatternLayerAtlas->QueryImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        StampLayerAtlas = std::make_unique<Frontier::MaterialThumbnailAtlas>();
        StampLayerAtlas->Initialize(Display.QueryRasterAccess(), Frontier::StampWorkspacePanel::LayerAtlasTile,
            Frontier::StampWorkspacePanel::LayerAtlasColumns);
        StampLayerAtlasDescriptor = ImGui_ImplVulkan_AddTexture(StampLayerAtlas->QueryImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        CoverageRendering = std::make_unique<Frontier::VectorAuthoringCoverageRendering>();
        CoverageRendering->Initialize(Display.QueryRasterAccess(), ShaderDirectory / "VectorCoverageCompute.spv");
        SelectionEvaluation = std::make_unique<Frontier::VectorSelectionEvaluation>();
        SelectionEvaluation->Initialize(Display.QueryRasterAccess(), ShaderDirectory / "VectorSelectionCompute.spv");
        Pattern->QueryCoverage = [&](const Frontier::VectorCoverageProgram &Program, uint32_t DisplayExtent) -> uint64_t
        {
            try
            {
                // 📝 Pooled slot extents keep their images alive until Finalize, so lazily retained
                // descriptors stay valid without ever freeing a set a previous frame still samples.
                // 📝 DisplayExtent (the zoomed canvas board in device pixels) lets unchanged content
                // re-bake at a sharper or coarser pooled tier instead of the shown image always being
                // a fixed-resolution raster stretched by the zoom transform.
                const int Slot = CoverageRendering->SelectProgram(Program, DisplayExtent);
                if (Slot < 0) return uint64_t(0);
                const auto Key = std::make_pair(size_t(Slot), CoverageRendering->QuerySlotExtent(size_t(Slot)));
                const auto Position = CoverageDescriptors.find(Key);
                if (Position != CoverageDescriptors.end()) return uint64_t(Position->second);
                const auto View = CoverageRendering->QueryImageView(size_t(Slot));
                if (!View) return uint64_t(0);
                const auto Descriptor = ImGui_ImplVulkan_AddTexture(View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                CoverageDescriptors.emplace(Key, Descriptor);
                return uint64_t(Descriptor);
            }
            catch (const std::exception &Failure)
            {
                JournalGpuFaultOnce("PatternCoverage", Failure);
                return uint64_t(0);
            }
        };
        Pattern->QueryCoverageReady = [&]() { return CoverageRendering ? CoverageRendering->QuerySelectedReady() : true; };
        Pattern->QuerySelectionToken = [&](const Frontier::VectorAuthoringDocument &Document, Frontier::PlanePoint Point)
        {
            return SelectionEvaluation->QueryToken(Frontier::ConstructVectorSelectionProgram(Document), {Point.X, Point.Y});
        };
        Pattern->QueryPatternAtlas = [&]()
        {
            const auto Uv = PatternAtlas ? PatternAtlas->QueryUv(0) : Frontier::ThumbnailUvRectangle{};
            return Frontier::PatternWorkspacePanel::PatternAtlasImage{PatternAtlasDescriptor ? uint64_t(PatternAtlasDescriptor) : uint64_t(0),
                Uv.MinimumU, Uv.MinimumV, Uv.MaximumU, Uv.MaximumV};
        };
        Pattern->QueryLayerThumbnailAtlas = [&]()
        {
            const auto Uv = PatternLayerAtlas ? PatternLayerAtlas->QueryUv(0) : Frontier::ThumbnailUvRectangle{};
            return Frontier::PatternWorkspacePanel::PatternAtlasImage{PatternLayerAtlasDescriptor ? uint64_t(PatternLayerAtlasDescriptor) : uint64_t(0),
                Uv.MinimumU, Uv.MinimumV, Uv.MaximumU, Uv.MaximumV};
        };
        // 📝 Stamp shares the same generic CoverageRendering/SelectionEvaluation GPU singletons as
        // Pattern above (both work over the same VectorCoverageProgram/VectorAuthoringDocument
        // shapes) — only the fault-journal key and the dedicated layer atlas differ.
        Stamp->QueryCoverage = [&](const Frontier::VectorCoverageProgram &Program, uint32_t DisplayExtent) -> uint64_t
        {
            try
            {
                const int Slot = CoverageRendering->SelectProgram(Program, DisplayExtent);
                if (Slot < 0) return uint64_t(0);
                const auto Key = std::make_pair(size_t(Slot), CoverageRendering->QuerySlotExtent(size_t(Slot)));
                const auto Position = CoverageDescriptors.find(Key);
                if (Position != CoverageDescriptors.end()) return uint64_t(Position->second);
                const auto View = CoverageRendering->QueryImageView(size_t(Slot));
                if (!View) return uint64_t(0);
                const auto Descriptor = ImGui_ImplVulkan_AddTexture(View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                CoverageDescriptors.emplace(Key, Descriptor);
                return uint64_t(Descriptor);
            }
            catch (const std::exception &Failure)
            {
                JournalGpuFaultOnce("StampCoverage", Failure);
                return uint64_t(0);
            }
        };
        Stamp->QueryCoverageReady = [&]() { return CoverageRendering ? CoverageRendering->QuerySelectedReady() : true; };
        Stamp->QuerySelectionToken = [&](const Frontier::VectorAuthoringDocument &Document, Frontier::PlanePoint Point)
        {
            return SelectionEvaluation->QueryToken(Frontier::ConstructVectorSelectionProgram(Document), {Point.X, Point.Y});
        };
        Stamp->QueryLayerThumbnailAtlas = [&]()
        {
            const auto Uv = StampLayerAtlas ? StampLayerAtlas->QueryUv(0) : Frontier::ThumbnailUvRectangle{};
            return Frontier::StampWorkspacePanel::StampAtlasImage{StampLayerAtlasDescriptor ? uint64_t(StampLayerAtlasDescriptor) : uint64_t(0),
                Uv.MinimumU, Uv.MinimumV, Uv.MaximumU, Uv.MaximumV};
        };
        const auto AssignPatternMapsToPreview = [&]()
        {
            // 📝 Pattern edits only reach the selected material through an explicit Apply to material press.
            if (!PatternAuthoringMaps) return false;
            const auto *Applied = Library->QueryLibrary().QueryApplied();
            if (!Applied) return false;
            auto PreviewMaterial = *Applied;
            PreviewMaterial.AuthoringMaps = *PatternAuthoringMaps;
            AssignedWorkspaceMaps = *PatternAuthoringMaps;
            AssignedWorkspaceSelection = Library->QueryLibrary().QuerySelection();
            AssignedWorkspacePreviewPath = PatternPreviewPath;
            Rendering->ConfigureSelection(PatternPreviewPath, PreviewMaterial);
            RenderingRevision = Library->QueryLibrary().QueryRevision();
            return true;
        };
        const auto AssignStampMapsToPreview = [&]()
        {
            // 📝 Stamp edits only reach the selected material through an explicit Apply to material press.
            if (!StampAuthoringMaps) return false;
            const auto *Applied = Library->QueryLibrary().QueryApplied();
            if (!Applied) return false;
            auto PreviewMaterial = *Applied;
            PreviewMaterial.AuthoringMaps = *StampAuthoringMaps;
            AssignedWorkspaceMaps = *StampAuthoringMaps;
            AssignedWorkspaceSelection = Library->QueryLibrary().QuerySelection();
            AssignedWorkspacePreviewPath = StampPreviewPath;
            Rendering->ConfigureSelection(StampPreviewPath, PreviewMaterial);
            RenderingRevision = Library->QueryLibrary().QueryRevision();
            return true;
        };
        Display.ConfigureRendering([&](VkCommandBuffer Commands, uint64_t Completed, uint64_t Serial)
        {
            if (!SelectionEvaluationFaulted)
                try { SelectionEvaluation->ConstructCommands(Commands, Completed, Serial); }
                catch (const std::exception &Failure) { SelectionEvaluationFaulted = true; JournalGpuFaultOnce("SelectionEvaluation", Failure); }
            if (!CoverageRenderingFaulted)
                try { CoverageRendering->ConstructCommands(Commands, Completed, Serial); }
                catch (const std::exception &Failure) { CoverageRenderingFaulted = true; JournalGpuFaultOnce("CoverageRendering", Failure); }
            try
            {
            if (Pattern->ConsumeMaterialApplyRequest()) PatternApplyRequested = true;
            if (PatternApplyRequested && !PatternBridgeSubmission && PatternAuthoringMaps &&
                Pattern->QueryDocument() == SubmittedPatternDocument && AssignPatternMapsToPreview())
                PatternApplyRequested = false;
            if (PatternBridgeSubmission && PatternBridgeFaultedDocument &&
                *PatternBridgeFaultedDocument == SubmittedPatternDocument)
                PatternBridgeSubmission = 0;
            if (PatternBridgeSubmission && PatternBridgeSubmission <= Completed)
            {
                AppendPatternPreviewDebug("GPU readback retired; submission=" + std::to_string(PatternBridgeSubmission) + "; completed=" + std::to_string(Completed));
                auto ReplacementMaps = MaterialBridge->RetrieveAuthoringMaps(Completed);
                if (Pattern->QueryDocument() == SubmittedPatternDocument)
                {
                    PatternAuthoringMaps = std::move(ReplacementMaps);
                    AppendPatternPreviewDebug("Float32 authoring maps accepted; extent=512x512; channel_count=6; awaiting Apply to material.");
                    if (PatternApplyRequested && AssignPatternMapsToPreview()) PatternApplyRequested = false;
                }
                else
                {
                    PatternAuthoringMaps.reset();
                    AppendPatternPreviewDebug("Discarded retired Float32 maps because the authored Pattern changed while GPU work was in flight.");
                }
                PatternBridgeSubmission = 0;
            }
            if (PatternActive && !PatternBridgeSubmission && !StampBridgeSubmission && Pattern->QueryAuthoringSettled() &&
                !(PatternBridgeFaultedDocument && *PatternBridgeFaultedDocument == Pattern->QueryDocument()))
            {
                // 📝 Gestures publish a fresh draft every frame; regenerate Float32 maps only after the gesture ends.
                const auto CurrentPattern = Pattern->QueryDocument();
                if (CurrentPattern == SubmittedPatternDocument) ObservedPatternDocument.reset();
                else if (ObservedPatternDocument && *ObservedPatternDocument == CurrentPattern)
                {
                    ObservedPatternDocument.reset();
                    // 📝 Mark this exact document as submitted regardless of whether ConfigureDocument
                    // found a change worth re-dispatching: if it returns false because the compiled
                    // programs happen to equal the previous ones (e.g. two documents that render
                    // identically), SubmittedPatternDocument must still advance to CurrentPattern — if
                    // it didn't, CurrentPattern would never again equal SubmittedPatternDocument and
                    // this settle-detect/compile cycle would retrigger forever, hammering the GPU with
                    // a full 6-channel recompile every other frame even on a perfectly idle document.
                    const bool PatternProgramsChanged = MaterialBridge->ConfigureDocument(CurrentPattern, Completed);
                    SubmittedPatternDocument = CurrentPattern;
                    if (PatternProgramsChanged)
                    {
                        AppendPatternPreviewDebug("Pattern document compiled; layers=" + std::to_string(SubmittedPatternDocument.Layers.size()) + "; instructions=" + std::to_string(SubmittedPatternDocument.Instructions.size()) + "; cubic_segments=" + std::to_string(SubmittedPatternDocument.Segments.size()) + "; completed_serial=" + std::to_string(Completed));
                        PatternAuthoringMaps.reset();
                        const bool AssignedSnapshotLive = AssignedWorkspaceMaps.has_value() &&
                            AssignedWorkspaceSelection == Library->QueryLibrary().QuerySelection();
                        if (!AssignedSnapshotLive)
                        {
                            if (const auto *Applied = Library->QueryLibrary().QueryApplied())
                            {
                                Rendering->ConfigureSelection(Library->QueryLibrary().QuerySelection(), *Applied);
                                RenderingRevision = Library->QueryLibrary().QueryRevision();
                                AppendPatternPreviewDebug("Selected-material preview restored without pending Pattern maps while Float32 regeneration is in flight.");
                            }
                        }

                        if (MaterialBridge->ConstructCommands(Commands, Completed, Serial))
                        {
                            PatternBridgeSubmission = Serial;
                            PatternBridgeRecorded = true;
                            AppendPatternPreviewDebug("GPU Float32 map generation commands recorded; submission=" + std::to_string(Serial));
                        }
                    }
                }
                else ObservedPatternDocument = CurrentPattern;
            }
            }
            catch (const std::exception &Failure)
            {
                // 📝 The Pattern bridge faults against the document it was compiling; editing the
                // document re-arms it. Preview/atlas work below is unaffected.
                PatternBridgeSubmission = 0;
                PatternBridgeRecorded = false;
                PatternBridgeFaultedDocument = Pattern->QueryDocument();
                JournalGpuFaultOnce("PatternBridge", Failure);
            }
            try
            {
            // 📝 Stamp's own Float32 authoring-map pipeline, symmetric to Pattern's above and sharing
            // the same physical MaterialBridge (mutually exclusive active workspaces never overlap).
            if (Stamp->ConsumeMaterialApplyRequest()) StampApplyRequested = true;
            if (StampApplyRequested && !StampBridgeSubmission && StampAuthoringMaps &&
                Stamp->QueryDocument() == SubmittedStampDocument && AssignStampMapsToPreview())
                StampApplyRequested = false;
            if (StampBridgeSubmission && StampBridgeFaultedDocument &&
                *StampBridgeFaultedDocument == SubmittedStampDocument)
                StampBridgeSubmission = 0;
            if (StampBridgeSubmission && StampBridgeSubmission <= Completed)
            {
                auto ReplacementMaps = MaterialBridge->RetrieveAuthoringMaps(Completed);
                if (Stamp->QueryDocument() == SubmittedStampDocument)
                {
                    StampAuthoringMaps = std::move(ReplacementMaps);
                    if (StampApplyRequested && AssignStampMapsToPreview()) StampApplyRequested = false;
                }
                else StampAuthoringMaps.reset();
                StampBridgeSubmission = 0;
            }
            if (StampActive && !StampBridgeSubmission && !PatternBridgeSubmission && Stamp->QueryAuthoringSettled() &&
                !(StampBridgeFaultedDocument && *StampBridgeFaultedDocument == Stamp->QueryDocument()))
            {
                // 📝 Gestures publish a fresh draft every frame; regenerate Float32 maps only after the gesture ends.
                const auto CurrentStamp = Stamp->QueryDocument();
                if (CurrentStamp == SubmittedStampDocument) ObservedStampDocument.reset();
                else if (ObservedStampDocument && *ObservedStampDocument == CurrentStamp)
                {
                    ObservedStampDocument.reset();
                    // 📝 Same fix as the Pattern branch above: advance SubmittedStampDocument whether or
                    // not ConfigureDocument finds an actual program change, otherwise an unchanged
                    // (even a blank, idle) document can never again equal SubmittedStampDocument and this
                    // settle/compile cycle retriggers every other frame forever — the "stuck baking"
                    // symptom, and a steady stream of needless 6-channel recompiles that can starve or
                    // crash the app under sustained load.
                    const bool StampProgramsChanged = MaterialBridge->ConfigureDocument(CurrentStamp, Completed);
                    SubmittedStampDocument = CurrentStamp;
                    if (StampProgramsChanged)
                    {
                        StampAuthoringMaps.reset();
                        const bool AssignedSnapshotLive = AssignedWorkspaceMaps.has_value() &&
                            AssignedWorkspaceSelection == Library->QueryLibrary().QuerySelection();
                        if (!AssignedSnapshotLive)
                        {
                            if (const auto *Applied = Library->QueryLibrary().QueryApplied())
                            {
                                Rendering->ConfigureSelection(Library->QueryLibrary().QuerySelection(), *Applied);
                                RenderingRevision = Library->QueryLibrary().QueryRevision();
                            }
                        }
                        if (MaterialBridge->ConstructCommands(Commands, Completed, Serial))
                        {
                            StampBridgeSubmission = Serial;
                            StampBridgeRecorded = true;
                        }
                    }
                }
                else ObservedStampDocument = CurrentStamp;
            }
            }
            catch (const std::exception &Failure)
            {
                // 📝 The Stamp bridge faults against the document it was compiling; editing the
                // document re-arms it. Preview/atlas work below is unaffected.
                StampBridgeSubmission = 0;
                StampBridgeRecorded = false;
                StampBridgeFaultedDocument = Stamp->QueryDocument();
                JournalGpuFaultOnce("StampBridge", Failure);
            }
            // 📝 The gallery bakes its 24-card page on the CPU; upload once per page flip, gated by the panel revision.
            const auto EnvironmentPreviews = Rendering->QueryEnvironmentPreviews();
            if (!EnvironmentAtlasFaulted && EnvironmentAtlas && EnvironmentPreviews.Generation &&
                EnvironmentPreviews.Generation != EnvironmentAtlasPublished)
                try
                {
                    EnvironmentAtlas->StagePixels(EnvironmentPreviews.Pixels);
                    EnvironmentAtlas->ConstructUpload(Commands);
                    EnvironmentAtlasPublished = EnvironmentPreviews.Generation;
                }
                catch (const std::exception &Failure) { EnvironmentAtlasFaulted = true; JournalGpuFaultOnce("EnvironmentAtlasUpload", Failure); }
            const uint64_t PatternAtlasRevision = Pattern->QueryPatternAtlasRevision();
            if (!PatternAtlasFaulted && PatternAtlas && PatternAtlasRevision != PatternAtlasPublished)
                try
                {
                    PatternAtlas->StagePixels(Pattern->QueryPatternAtlasPixels());
                    PatternAtlas->ConstructUpload(Commands);
                    PatternAtlasPublished = PatternAtlasRevision;
                }
                catch (const std::exception &Failure) { PatternAtlasFaulted = true; JournalGpuFaultOnce("PatternAtlasUpload", Failure); }
            const uint64_t PatternLayerAtlasRevision = Pattern->QueryLayerAtlasRevision();
            if (!PatternLayerAtlasFaulted && PatternLayerAtlas && PatternLayerAtlasRevision != PatternLayerAtlasPublished)
                try
                {
                    const auto &LayerPixels = Pattern->QueryLayerAtlasPixels();
                    if (!LayerPixels.empty())
                    {
                        PatternLayerAtlas->StagePixels(LayerPixels);
                        PatternLayerAtlas->ConstructUpload(Commands);
                    }
                    PatternLayerAtlasPublished = PatternLayerAtlasRevision;
                }
                catch (const std::exception &Failure) { PatternLayerAtlasFaulted = true; JournalGpuFaultOnce("PatternLayerAtlasUpload", Failure); }
            const uint64_t StampLayerAtlasRevision = Stamp->QueryLayerAtlasRevision();
            if (!StampLayerAtlasFaulted && StampLayerAtlas && StampLayerAtlasRevision != StampLayerAtlasPublished)
                try
                {
                    const auto &LayerPixels = Stamp->QueryLayerAtlasPixels();
                    if (!LayerPixels.empty())
                    {
                        StampLayerAtlas->StagePixels(LayerPixels);
                        StampLayerAtlas->ConstructUpload(Commands);
                    }
                    StampLayerAtlasPublished = StampLayerAtlasRevision;
                }
                catch (const std::exception &Failure) { StampLayerAtlasFaulted = true; JournalGpuFaultOnce("StampLayerAtlasUpload", Failure); }
            {
                // 📝 Material rendering faults latch against the current catalogue revision + preview
                // configuration; any new edit re-arms evaluation. A partially recorded tile/preview
                // renderpass belongs to this discarded key and is never resubmitted unchanged.
                const auto PreviewState = Rendering->QueryPreviewConfiguration();
                const std::string RenderingKey = std::to_string(Library->QueryLibrary().QueryRevision()) + "|" +
                    std::to_string(uint32_t(PreviewState.Shape)) + "|" +
                    std::to_string(uint32_t(PreviewState.Environment)) + "|" +
                    std::to_string(PreviewState.Zoom) + "|" + std::to_string(PreviewState.Yaw) + "|" +
                    std::to_string(PreviewState.Pitch) + "|" + std::to_string(Rendering->QueryPreviewExtent());
                if (RenderingKey != MaterialRenderingFaultKey)
                    try { Rendering->ConstructCommands(Commands, Completed, Serial); }
                    catch (const std::exception &Failure)
                    {
                        MaterialRenderingFaultKey = RenderingKey;
                        JournalGpuFaultOnce("MaterialRendering", Failure);
                    }
            }
        }, [&]()
        {
            try
            {
                SelectionEvaluation->ConfirmSubmission();
                if (PatternAtlas) PatternAtlas->ConfirmSubmission();
                if (PatternLayerAtlas) PatternLayerAtlas->ConfirmSubmission();
                if (StampLayerAtlas) StampLayerAtlas->ConfirmSubmission();
                if (EnvironmentAtlas) EnvironmentAtlas->ConfirmSubmission();
                if (PatternBridgeRecorded || StampBridgeRecorded)
                {
                    // 📝 One shared physical bridge: whichever workspace recorded this submission's
                    // commands, the GPU work is the same submission and is confirmed exactly once.
                    MaterialBridge->ConfirmSubmission();
                    if (PatternBridgeRecorded) AppendPatternPreviewDebug("GPU Float32 map generation submission confirmed; submission=" + std::to_string(PatternBridgeSubmission));
                    PatternBridgeRecorded = false;
                    StampBridgeRecorded = false;
                }
                CoverageRendering->ConfirmSubmission();
                Rendering->ConfirmSubmission();
            }
            catch (const std::exception &Failure) { JournalGpuFaultOnce("GpuConfirm", Failure); }
            catch (...) { Frontier::DiagnosticJournal::Append("GpuConfirm", "unknown submission-confirm fault isolated"); }
        });
        Library->RequestThumbnails = [&](const std::vector<std::filesystem::path> &Paths)
        {
            try { Rendering->RequestLibrary(Library->QueryLibrary().QueryDirectory(), Paths); }
            catch (const std::exception &Failure) { JournalGpuFaultOnce("ThumbnailRequest", Failure); }
        };
        Library->QueryThumbnail = [&](const std::filesystem::path &Path)
        {
            const auto Thumbnail = Rendering->QueryThumbnail(Path);
            const auto &Uv = Thumbnail.Uv;
            return Frontier::MaterialPreviewImage{Thumbnail.Ready ? (uint64_t)AtlasDescriptor : 0,
                Uv.MinimumU, Uv.MinimumV, Uv.MaximumU, Uv.MaximumV};
        };
        Library->InvalidateThumbnails = [&]() { Rendering->InvalidateThumbnails(); };
        Library->InvalidateThumbnail = [&](const std::filesystem::path &Path) { Rendering->InvalidateThumbnail(Path); };
        Library->QueryThumbnailDiagnostic = [&](const std::filesystem::path &Path) { return Rendering->QueryThumbnailDiagnostic(Path); };
        Library->ConfigurePreview = [&](const Frontier::MaterialPreviewConfiguration &Configuration)
        {
            try { Rendering->ConfigurePreview(Configuration); }
            catch (const std::exception &Failure) { JournalGpuFaultOnce("PreviewConfigure", Failure); }
        };
        Library->ConfigurePreviewExtent = [&](uint32_t Extent) { Rendering->ConfigurePreviewExtent(Extent); };
        Library->QueryEnvironmentNames = [&]() { return Rendering->QueryEnvironmentNames(); };
        Library->QueryEnvironmentPreview = [&](size_t Index)
        {
            if (!EnvironmentAtlas || !EnvironmentAtlasDescriptor ||
                Index >= Rendering->QueryEnvironmentPreviews().Count) return Frontier::MaterialPreviewImage{};
            const auto Uv = EnvironmentAtlas->QueryUv(uint32_t(Index));
            return Frontier::MaterialPreviewImage{uint64_t(EnvironmentAtlasDescriptor),
                Uv.MinimumU, Uv.MinimumV, Uv.MaximumU, Uv.MaximumV};
        };
        Library->RescanEnvironments = [&]()
        {
            try
            {
                Rendering->ConfigureEnvironments(ContentRoot / "EngineContent/EnvironmentArchives");
                // 📝 Re-upload the selected residence so its own environment image matches the
                // rescanned catalogue (layer clamp covers the frames in between).
                const auto &Catalogue = Library->QueryLibrary();
                if (Catalogue.QueryApplied())
                    Rendering->ConfigureSelection(Catalogue.QuerySelection(), *Catalogue.QueryApplied());
            }
            catch (const std::exception &Failure) { JournalGpuFaultOnce("EnvironmentRescan", Failure); }
        };
        Library->QueryPreview = [&]()
        {
            // 📝 A retired descriptor was last sampled at most its retirement frame; the submission
            // owner fences every frame before recording the next, so four frames later it is free.
            while (!RetiredPreviewDescriptors.empty() && RetiredPreviewDescriptors.front().second + 4 <= InterfaceFrame)
            {
                if (RetiredPreviewDescriptors.front().first)
                    ImGui_ImplVulkan_RemoveTexture(RetiredPreviewDescriptors.front().first);
                RetiredPreviewDescriptors.pop_front();
            }
            const VkImageView CurrentView = Rendering->QueryPreviewView();
            try
            {
                if (CurrentView && CurrentView != LastPreviewView)
                {
                    LastPreviewView = CurrentView;
                    if (PreviewDescriptor) RetiredPreviewDescriptors.emplace_back(PreviewDescriptor, InterfaceFrame);
                    PreviewDescriptor = ImGui_ImplVulkan_AddTexture(CurrentView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
            }
            catch (const std::exception &Failure) { JournalGpuFaultOnce("PreviewTexture", Failure); }
            return Frontier::MaterialPreviewImage{Rendering->QueryPreviewReady() ? (uint64_t)PreviewDescriptor : 0};
        };
    };
    Callbacks.ConfigurePlatform = [&]()
    {
        if (Appearance->QueryLayoutResetRevision() != LayoutResetRevision)
        {
            Library->QueryPanels().ResetLayout();
            Pattern->QueryPanels().ResetLayout();
            Stamp->QueryPanels().ResetLayout();
            LayoutBlocked = false;
            LayoutResetRevision = Appearance->QueryLayoutResetRevision();
        }
        Library->QueryPanels().ConfigureDetachedWindows(Appearance->QueryDetachedWindows());
        Pattern->QueryPanels().ConfigureDetachedWindows(Appearance->QueryDetachedWindows());
        Stamp->QueryPanels().ConfigureDetachedWindows(Appearance->QueryDetachedWindows());
        Library->ConfigurePlatform();
        (void)Pattern->QueryPanels().ConfigurePlatform();
        (void)Stamp->QueryPanels().ConfigurePlatform();
        if (glfwWindowShouldClose(Window))
        {
            const bool Authorized = StampActive ? Stamp->RequestClose() : PatternActive ? Pattern->RequestClose() : Library->RequestClose();
            if (!Authorized) glfwSetWindowShouldClose(Window, GLFW_FALSE);
        }
    };
    Callbacks.PrepareInterface = [&]() { Appearance->PrepareInterface(); };
    Callbacks.ConstructInterface = [&](bool MainVisible)
    {
        ++InterfaceFrame;
        (void)Appearance->Integrate(ImGui::GetIO().DeltaTime);
        if (StampActive) (void)Stamp->Integrate(ImGui::GetIO().DeltaTime);
        else if (PatternActive) (void)Pattern->Integrate(ImGui::GetIO().DeltaTime);
        else (void)Library->Integrate(ImGui::GetIO().DeltaTime);
        if (MainVisible)
        {
            if (StampActive)
            {
                Stamp->EvaluateNavigationDelta = [&](Frontier::PlanePoint Displacement)
                {
                    return Appearance->QueryInput().QueryApplied().EvaluatePointerDelta(Displacement);
                };
                Stamp->ConfigureInterfaceScale(Appearance->QueryAppearance().QueryApplied().InterfaceScale / 100.f);
                Stamp->ConstructInterface();
            }
            else if (PatternActive)
            {
                Pattern->EvaluateNavigationDelta = [&](Frontier::PlanePoint Displacement)
                {
                    return Appearance->QueryInput().QueryApplied().EvaluatePointerDelta(Displacement);
                };
                Pattern->ConfigureInterfaceScale(Appearance->QueryAppearance().QueryApplied().InterfaceScale / 100.f);
                Pattern->ConstructInterface();
            }
            else
            {
                if (Rendering) Rendering->RequestLibrary(Library->QueryLibrary().QueryDirectory(), {});
                Library->EvaluateNavigationDelta = [&](Frontier::PlanePoint Displacement)
                {
                    return Appearance->QueryInput().QueryApplied().EvaluatePointerDelta(Displacement);
                };
                Library->ConfigureInterfaceScale(Appearance->QueryAppearance().QueryApplied().InterfaceScale / 100.f);
                Library->ConstructInterface();
                const auto &Catalogue = Library->QueryLibrary();
                if (Rendering && Catalogue.QueryApplied() && RenderingRevision != Catalogue.QueryRevision())
                {
                    // 📝 Consume the revision even on fault: retrying the identical material every frame
                    // would turn one bad input into an allocation storm (and pool exhaustion).
                    RenderingRevision = Catalogue.QueryRevision();
                    try
                    {
                        if (Catalogue.QuerySelection() != AssignedWorkspaceSelection) AssignedWorkspaceMaps.reset();
                        if (AssignedWorkspaceMaps)
                        {
                            auto PreviewMaterial = *Catalogue.QueryApplied();
                            PreviewMaterial.AuthoringMaps = *AssignedWorkspaceMaps;
                            Rendering->ConfigureSelection(AssignedWorkspacePreviewPath, PreviewMaterial);
                        }
                        else Rendering->ConfigureSelection(Catalogue.QuerySelection(), *Catalogue.QueryApplied());
                    }
                    catch (const std::exception &Failure) { JournalGpuFaultOnce("SelectionConfigure", Failure); }
                }
                if (Library->ConsumeClose()) glfwSetWindowShouldClose(Window, GLFW_TRUE);
            }
            Appearance->ConstructInterface();
        }
    };
    Callbacks.Finalize = [&]()
    {
        // 📝 The shared application loop has retired all GPU work before this callback.
        if (AtlasDescriptor) ImGui_ImplVulkan_RemoveTexture(AtlasDescriptor);
        if (PreviewDescriptor) ImGui_ImplVulkan_RemoveTexture(PreviewDescriptor);
        for (const auto &Retired : RetiredPreviewDescriptors)
            if (Retired.first) ImGui_ImplVulkan_RemoveTexture(Retired.first);
        RetiredPreviewDescriptors.clear();
        if (PatternAtlasDescriptor) ImGui_ImplVulkan_RemoveTexture(PatternAtlasDescriptor);
        if (PatternLayerAtlasDescriptor) ImGui_ImplVulkan_RemoveTexture(PatternLayerAtlasDescriptor);
        if (StampLayerAtlasDescriptor) ImGui_ImplVulkan_RemoveTexture(StampLayerAtlasDescriptor);
        if (EnvironmentAtlasDescriptor) ImGui_ImplVulkan_RemoveTexture(EnvironmentAtlasDescriptor);
        for (const auto &Entry : CoverageDescriptors)
            if (Entry.second) ImGui_ImplVulkan_RemoveTexture(Entry.second);
        CoverageDescriptors.clear();
        if (SelectionEvaluation) SelectionEvaluation->Finalize();
        if (MaterialBridge) MaterialBridge->Finalize();
        if (CoverageRendering) CoverageRendering->Finalize();
        if (PatternAtlas) PatternAtlas->Finalize();
        if (PatternLayerAtlas) PatternLayerAtlas->Finalize();
        if (StampLayerAtlas) StampLayerAtlas->Finalize();
        if (EnvironmentAtlas) EnvironmentAtlas->Finalize();
        SelectionEvaluation.reset();
        MaterialBridge.reset();
        CoverageRendering.reset();
        PatternAtlas.reset();
        PatternLayerAtlas.reset();
        StampLayerAtlas.reset();
        EnvironmentAtlas.reset();
        Rendering.reset();
        if (Library && Appearance && !LayoutBlocked)
        {
            std::string Message;
            if (Library->QueryPanels().QuerySeated())
            {
                const auto Layout = Library->QueryPanels().EncodeLayout();
                if (!Layout.empty() && !Appearance->PersistWorkspaceLayout(Layout, Message, "texture_studio_layout"))
                {
                    Frontier::DiagnosticJournal::Append("LayoutSaveError", Message);
                    std::cerr << "TextureWorkspace layout not saved: " << Message << '\n';
                }
            }
            if (Pattern && Pattern->QueryPanels().QuerySeated())
            {
                Message.clear();
                const auto Layout = Pattern->QueryPanels().EncodeLayout();
                if (!Layout.empty() && !Appearance->PersistWorkspaceLayout(Layout, Message, "texture_studio_pattern_layout"))
                {
                    Frontier::DiagnosticJournal::Append("PatternLayoutSaveError", Message);
                    std::cerr << "TextureWorkspace Pattern layout not saved: " << Message << '\n';
                }
            }
            if (Stamp && Stamp->QueryPanels().QuerySeated())
            {
                Message.clear();
                const auto Layout = Stamp->QueryPanels().EncodeLayout();
                if (!Layout.empty() && !Appearance->PersistWorkspaceLayout(Layout, Message, "texture_studio_stamp_layout"))
                {
                    Frontier::DiagnosticJournal::Append("StampLayoutSaveError", Message);
                    std::cerr << "TextureWorkspace Stamp layout not saved: " << Message << '\n';
                }
            }
        }
        PatternOwnership.reset();
        StampOwnership.reset();
        LibraryOwnership.reset();
        AppearanceOwnership.reset();
    };
    return Frontier::RunWindowsApplication(Callbacks);
}

int main(int ArgumentCount, char **Arguments)
{
    Frontier::DiagnosticJournal::Initialize("TextureWorkspace");
    try
    {
        return RunTextureWorkspace(ArgumentCount, Arguments);
    }
    catch (const std::exception &Failure)
    {
        Frontier::DiagnosticJournal::Append("SEVERE", Failure.what());
        const auto Description = std::string("Startup failed: ") + Failure.what() + "\nLogs: " +
            Frontier::DiagnosticJournal::EncodePath(Frontier::DiagnosticJournal::QueryFilename());
        ReportSevereFailure("Texture Workspace - Severe failure", Description);
        return 1;
    }
    catch (...)
    {
        Frontier::DiagnosticJournal::Append("SEVERE", "Unknown startup failure");
        ReportSevereFailure("Texture Workspace - Severe failure",
            "Unknown startup failure. Collect the DiagnosticJournal log (see Frontier/Logs).");
        return 1;
    }
}
