/*==========================================================================================================================================
                                                          PROJECTBROWSERPANEL.H
==========================================================================================================================================*/
// 🧩 1:1 native port of the Project browser HTML prototype (Experimental/Project browser/): sample/attached file catalogue,
// reader with live reference previews, A4 Markdown documents, notes, version history, collapse panels and OLED/Light/Dark themes.

#pragma once
#include <vector>
#include "Engine/ContentInterchange/ProjectBrowserCatalogue.h"
#include "Engine/ContentInterchange/ProjectSpaceCodec.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Render/BrowserPreviewGeometry.h"
#include <array>
#include <chrono>
#include <functional>
#include <imgui.h>
#include <map>
#include <utility>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     PROJECT BROWSER
//------------------------------------------------------------------------------------------------------------------------

struct BrowserPreviewImage
{
    uint64_t Texture = 0; // ImTextureID as an integer; 0 means the tile has not arrived yet.
    float U0 = 0, V0 = 0, U1 = 1, V1 = 1;
    float Width = 384, Height = 246; // Capture pixels; imports report their decoded extent.
    bool Failed = false; // The host refused this file (unsupported encoding); draw the file icon instead.
};

enum class BrowserThumbFit
{
    Cover, // 🖼️ object-fit:cover crops the capture into the tile box.
    Contain // 🖼️ object-fit:contain letterboxes the capture on black.
};

class ProjectBrowserPanel
{
  public:
    using ProjectPathSelection = std::function<std::filesystem::path(bool)>;
    explicit ProjectBrowserPanel(ProjectPathSelection Selection = {});
    bool ConfigureDirectory(const std::filesystem::path &Directory);
    bool SelectProject(const std::filesystem::path &Path);
    bool CreateProject(const std::filesystem::path &Destination, const std::string &Name);
    bool RequestOpenProject();
    void ResetSelection() noexcept;
    void ConstructInterface();
    void ConfigurePalette(const ControlKitPalette &Palette) noexcept { Colours = Palette; }
    void ConfigurePreferences(const std::filesystem::path &Path);
    [[nodiscard]] std::filesystem::path ConsumeLaunchPath() { return std::exchange(LaunchPath, {}); }
    [[nodiscard]] const std::string &QueryDiagnostic() const noexcept { return Diagnostic; }
    [[nodiscard]] const std::filesystem::path &QuerySelection() const noexcept { return SelectedPath; }
    [[nodiscard]] static constexpr std::array<const char *, 1> QueryPresets() noexcept { return {"Blank Project"}; }
    // 📖 1:1 browser navigation (driven by the UI and by the validation harness).
    bool OpenSample(const std::string &Token);
    void CloseFile();
    size_t AttachPaths(const std::vector<std::filesystem::path> &Paths, bool AutoOpen = true);
    [[nodiscard]] bool IsReaderOpen() const noexcept { return !ActiveToken.empty(); }
    [[nodiscard]] const std::string &QueryActiveToken() const noexcept { return ActiveToken; }
    [[nodiscard]] size_t QueryEntryCount() const noexcept { return Entries.size(); }
    [[nodiscard]] std::vector<std::string> QueryFilteredTitles(
        const std::string &Filter, const std::string &Search, bool SortByName) const;
    [[nodiscard]] const std::vector<float> &QueryActuatorTriangles() const noexcept { return ActuatorTriangles; }
    [[nodiscard]] const std::vector<uint8_t> &QueryEonLookup() const noexcept { return EonLookup; }
    // 🧪 Validation hook: drives every reader state (views, revisions, tabs, zoom, themes, dialogs) headlessly.
    void DebugShow(const std::string &Token, int View, int Revision, bool Document, int ZoomPercent,
        const std::string &Tab, const std::string &Theme, bool Launch, bool ShowCreation);
    // 🖼️ Host-provided preview plumbing. Every callback is optional: without a renderer the browser stays fully
    // navigable and shows the same pending/unavailable placeholders as the prototype.
    std::function<BrowserPreviewImage(const std::string &Key)> QueryThumbnailImage; // Key = "token:revision:view".
    std::function<BrowserPreviewImage()> QueryLiveImage;
    std::function<void(const BrowserPreviewConfiguration &, const BrowserPreviewCamera &, int Channel, int Width,
        int Height)>
        ConfigureLivePreview;
    std::function<void(const std::string &Key, const BrowserPreviewConfiguration &, const BrowserPreviewCamera &,
        int Channel)>
        CaptureLiveThumbnail;
    std::function<BrowserPreviewImage(const std::string &Token, const std::vector<uint8_t> &ImageBytes)> QueryImportedImage;
    std::function<std::vector<std::filesystem::path>()> PickImportFiles;
    std::function<std::filesystem::path()> PickImportFolder;

  private:
    struct CachedPreviewTriangles
    {
        std::vector<float> Triangles;
        BrowserPreviewStatistics Statistics;
        std::vector<BrowserUvSegment> Uv;
    };
    [[nodiscard]] const BrowserCatalogueEntry *FindEntry(const std::string &Token) const noexcept;
    [[nodiscard]] BrowserCatalogueEntry *FindMutable(const std::string &Token) noexcept;
    [[nodiscard]] std::vector<const BrowserCatalogueEntry *> FilterEntries() const;
    [[nodiscard]] BrowserPreviewConfiguration PreviewConfigFor(
        const BrowserCatalogueEntry &Entry, size_t View, int Revision) const;
    [[nodiscard]] std::string SnapshotKeyFor(const BrowserCatalogueEntry &Entry, size_t View, int Revision) const;
    [[nodiscard]] const CachedPreviewTriangles &PreviewTrianglesFor(
        const BrowserCatalogueEntry &Entry, int Revision);
    void ActivateToken(const std::string &Token);
    bool DrawLibraryRail(ImVec2 Origin, float Width, float Height, float HeaderH, float Scale);
    void ConstructThumbnailTile(ImVec2 Min, ImVec2 Size, const BrowserCatalogueEntry &Entry, size_t View,
        int Revision, BrowserThumbFit Fit, float Scale, float Rounding = 0, ImDrawFlags RoundFlags = 0,
        float Zoom = 1);
    void ConstructUvArt(
        ImVec2 Min, ImVec2 Size, const BrowserCatalogueEntry &Entry, int Revision, float Scale);
    void LoadPreferences();
    void PersistPreferences();
    void ResetLiveCamera(const BrowserCatalogueEntry &Entry);
    void ConfigureRevision(int Index);
    void ConfigureZoom(int Amount);
    void ToggleStar(const std::string &Token);
    void Notify(const std::string &Message);
    void OpenInFrontier();
    void SubmitComment();
    void CaptureNow();
    void ConstructLibrary(float Width);
    void ConstructReader(float Width);
    void ConstructReaderInspector(float Width, ImVec2 Origin, ImVec2 Avail, float HeaderH, float Scale);
    void ConstructLaunchDialog(float Width);
    void ConstructCreationModal();
    void ConstructToast(ImVec2 Origin, ImVec2 Size);
    ContentDirectory Directory;
    ControlKitPalette Colours;
    ProjectPathSelection PickProjectPath;
    ProjectSpaceDocument SelectedProject;
    std::filesystem::path SelectedPath, LaunchPath, PrefsPath;
    std::string Diagnostic, Filter = "all", Search, SortMode = "recent", ActiveTab = "details", ActiveToken;
    std::string CreateError, Toast, LiveSignature;
    std::vector<BrowserCatalogueEntry> Entries;
    std::vector<float> ActuatorTriangles;
    std::vector<uint8_t> EonLookup;
    std::map<std::string, CachedPreviewTriangles> PreviewCache;
    BrowserPreferences Prefs;
    BrowserPreviewCamera LiveCamera;
    int PreviewIndex = 0, ActiveRevision = 0, Zoom = 100, LiveChannel = 0, CaptureDelay = 0;
    uint64_t AttachSerial = 0;
    std::chrono::steady_clock::time_point ToastUntil{};
    std::array<char, 4096> DirectoryText{}, DestinationText{};
    std::array<char, 257> NameText{};
    std::array<char, 257> SearchText{};
    std::array<char, 2001> CommentText{};
    bool LeftCollapsed = false, RightCollapsed = false, ResponsiveConfigured = false;
    bool DocumentEnabled = false, LaunchOpen = false, Creating = false, LiveDragging = false;
    bool WasLiveHovered = false;
    bool FocusSearch = false, FocusBack = false, FocusBrowse = false;
    void ConstructCatalogue() = delete;
    void ConstructDetails() = delete;
    void ConstructCreation() = delete;
};
} // namespace Frontier
