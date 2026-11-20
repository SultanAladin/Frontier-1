/*============================================================================================================================================
                                                               PROJECTBROWSER.H
============================================================================================================================================*/
// 🧩 Native C++20 Project Browser built strictly on PixelSpace, Engine/Components/Controls, TieredWorldRenderSequence, & ProjectGenerator

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Configuration/ProjectGenerator.h"
#include "Engine/GeometricRaster/WorldRendering/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/WorldRendering/TieredWorldRenderSequence.h"
#include "Engine/Workspaces/WorkspaceCapabilityRegister.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct ImTextureData;

namespace Frontier::GeometricRaster
{
class VulkanInterchange;
class VulkanWorldRenderSequence;
}

namespace Frontier::Editor::Explorer
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class BrowserFilterMode : uint8_t
{
    AllFiles = 0u,
    Recent   = 1u,
    Starred  = 2u,
    Projects = 3u,
    Cad      = 4u,
    Geometry = 5u,
};

enum class BrowserEntryCategory : uint8_t
{
    Project  = 0u,
    Cad      = 1u,
    Geometry = 2u,
};

enum class BrowserInspectorTab : uint8_t
{
    Details  = 0u,
    Comments = 1u,
    History  = 2u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct BrowserReviewNote
{
    std::string AuthorName {};
    std::string Initial    {};
    std::string BodyText   {};
    std::string Timestamp  {};
};

struct BrowserCollectionEntry
{
    std::string                   Identifier      {};
    std::string                   Title           {};
    std::string                   Suffix       {};
    BrowserEntryCategory          Category        = BrowserEntryCategory::Project;
    std::string                   SummaryCaption  {};
    std::string                   Description     {};
    std::string                   LastOpenedText  {};
    std::filesystem::path         FilePath        {};
    std::vector<std::string>      ViewTitles      {};
    std::vector<std::string>      RevisionTitles  {};
    std::vector<BrowserReviewNote> Notes          {};
    bool                          Starred         = false;
    bool                          LocalDiskEntry  = false;
    uint32_t                      CameraPresetIdx = 0u;
    std::unique_ptr<ImTextureData> ThumbnailTex   {};
    std::vector<uint8_t>          ThumbnailRgba   {};
    uint32_t                      ThumbnailW      = 0u;
    uint32_t                      ThumbnailH      = 0u;
};

struct BrowserWorkspaceLaunchDirective
{
    Workspaces::HostWorkspaceTarget TargetWorkspace  = Workspaces::HostWorkspaceTarget::FrontierWorld;
    std::string                     ProjectTitle     {};
    std::filesystem::path           SelectedFilePath {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ProjectBrowser
{
public:
    using LaunchWorkspaceCallback = std::function<void(const BrowserWorkspaceLaunchDirective&)>;

    ProjectBrowser();
    ~ProjectBrowser();

    ProjectBrowser(const ProjectBrowser&)            = delete;
    ProjectBrowser& operator=(const ProjectBrowser&) = delete;

    void AttachVulkanWorldRenderer(
        GeometricRaster::VulkanInterchange* DeviceInterchange,
        const std::filesystem::path&        CompiledShaderDirectory);

    void InitializeDefaultCollection(
        const std::filesystem::path& DefaultProjectsDirectory = {},
        const std::filesystem::path& FrontierRootDirectory    = {},
        const std::filesystem::path& FrontierBinariesDir      = {});

    void ReleaseGpuTextures() noexcept;

    void AssignLaunchWorkspaceCallback(LaunchWorkspaceCallback Callback);
    void ConfigureThemePreset(ThemePreset Preset) noexcept;
    [[nodiscard]] ThemePreset QueryThemePreset() const noexcept { return ActiveTheme; }

    void OpenNewProjectSheet(std::string_view InitialName = "AeroGallery", const std::filesystem::path& DestinationFolder = {});
    [[nodiscard]] Configuration::ProjectScaffoldOutcome TriggerCreateProjectFromDraft(bool LaunchImmediately);
    bool RegisterDiskPathInCollection(const std::filesystem::path& DiskPath, bool ActivateInReader = true);
    uint32_t ScanFolderForEntries(const std::filesystem::path& FolderPath);

    void ActivateEntryByIndex(int32_t EntryIndex) noexcept;
    void ReturnToLibrary() noexcept;

    [[nodiscard]] bool                            QueryReaderActive() const noexcept { return ActiveEntryIndex >= 0; }
    [[nodiscard]] int32_t                         QueryActiveEntryIndex() const noexcept { return ActiveEntryIndex; }
    [[nodiscard]] std::span<const BrowserCollectionEntry> QueryCollection() const noexcept { return Collection; }
    [[nodiscard]] const Workspaces::WorkspaceCapabilityRegister& QueryCapabilities() const noexcept { return CapabilityRegister; }

    void Render(Rectangle2D ViewportBounds, double DeltaSeconds, float DpiScale = 1.0f);

private:
    void EnsurePreviewRendererReady();
    void RenderThumbnailForEntry(BrowserCollectionEntry& Entry, uint32_t ViewOrdinal = 0u);
    void RefreshReaderPreviewTexture();

    void RenderLibraryMode(PixelSpace& Surface, Rectangle2D LogicalBounds, const ControlPointer& Pointer, double DeltaSeconds);
    void RenderReaderMode(PixelSpace& Surface, Rectangle2D LogicalBounds, const ControlPointer& Pointer, double DeltaSeconds);
    void RenderNewProjectModal(PixelSpace& Surface, Rectangle2D LogicalBounds, const ControlPointer& Pointer);
    void RenderDiskPathModal(PixelSpace& Surface, Rectangle2D LogicalBounds, const ControlPointer& Pointer);

    Workspaces::WorkspaceCapabilityRegister                         CapabilityRegister   {};
    std::vector<BrowserCollectionEntry>                             Collection           {};
    std::unique_ptr<GeometricRaster::ShowcaseWorldSceneBundle>      PreviewSceneBundle   {};
    std::unique_ptr<GeometricRaster::TieredWorldRenderSequence>     PreviewWorldRenderer {};
    std::unique_ptr<GeometricRaster::VulkanWorldRenderSequence>     GpuPreviewRenderer   {};
    std::unique_ptr<ImTextureData>                                  ReaderPreviewTex     {};
    std::vector<std::unique_ptr<ImTextureData>>                  ReaderViewThumbs     {};
    std::vector<uint8_t>                                         ReaderPreviewRgba    {};
    uint32_t                                                     ReaderPreviewW       = 0u;
    uint32_t                                                     ReaderPreviewH       = 0u;

    ThemePreset             ActiveTheme           = ThemePreset::Oled;
    BrowserFilterMode       ActiveFilter          = BrowserFilterMode::AllFiles;
    BrowserInspectorTab     ActiveInspectorTab    = BrowserInspectorTab::Details;
    int32_t                 ActiveEntryIndex      = -1;
    uint32_t                ActiveViewIndex       = 0u;
    uint32_t                ActiveRevisionIndex   = 0u;
    uint32_t                SortModeIndex         = 0u;
    float                   ReaderZoomPercent     = 100.0f;
    float                   LibraryScrollY        = 0.0f;
    bool                    LeftRailCollapsed     = false;
    bool                    RightPaneCollapsed    = false;
    bool                    SortDropdownOpen      = false;
    bool                    RevisionDropdownOpen  = false;
    bool                    NewProjectModalOpen   = false;
    bool                    DiskPathModalOpen     = false;
    bool                    DraftEnableFrontier   = true;
    bool                    DraftEnableUv         = true;

    TextEditBuffer          SearchInput           {};
    TextEditBuffer          CommentInput          {};
    TextEditBuffer          DraftProjectNameInput    {};
    TextEditBuffer          DraftDestinationDirInput {};
    TextEditBuffer          DiskPathInput            {};

    std::filesystem::path   DefaultProjectsDir    {};
    std::filesystem::path   FrontierRootDir       {};
    std::filesystem::path   FrontierBinariesDir   {};
    std::string             StatusBannerMessage   {};
    double                  StatusBannerRemaining = 0.0;
    LaunchWorkspaceCallback LaunchCallback        {};
};

} // namespace Frontier::Editor::Explorer
