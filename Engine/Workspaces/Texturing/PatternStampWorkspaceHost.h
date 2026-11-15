/*============================================================================================================================================
                                                         PATTERNSTAMPWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Shared 3-column Workspace host mounting Layers (Left), Pattern/Stamp GPU Canvas & Tool Chrome (Center), Inspector (Right),
//    live Vulkan VectorCanvasRasterizer, ContentBrowser drag-drop & thumbnail sync, .VectorSpace save/load, and external file drop intake.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/GeometricRaster/Vector/VectorCanvasRasterizer.h"
#include "Engine/Workspaces/Texturing/PatternStampInspectorPane.h"
#include "Engine/Workspaces/Texturing/PatternStampLayerStackPane.h"
#include "Engine/Workspaces/Texturing/PatternStampStackSession.h"
#include "Engine/Workspaces/Texturing/PatternStampToolHeaderPane.h"

#if FRONTIER_EDITOR_ENABLED
#include "Engine/Editor/Explorer/ContentBrowser.h"
#endif

#include <cstdint>
#include <filesystem>
#include <string>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CLASS DECLARATION
//------------------------------------------------------------------------------------------------------------------------

class PatternStampWorkspaceHost
{
public:
    explicit PatternStampWorkspaceHost(PatternStampWorkspaceMode InitialMode = PatternStampWorkspaceMode::Pattern);

    void ConfigureMode(PatternStampWorkspaceMode TargetMode) noexcept;
    void ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept;
    void ConfigureRepositoryRoot(const std::filesystem::path& RepoRoot) noexcept;
    void AttachVulkanRasterizer(GeometricRaster::VectorCanvasRasterizer* TargetRasterizer) noexcept;
    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel);
#if FRONTIER_EDITOR_ENABLED
    void AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser);
#endif

    void RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer);

    [[nodiscard]] bool IntakeExternalFileDrop(
        const std::filesystem::path& DroppedPath,
        std::string&                 OutDiagnostic,
        float                        DropDocX = 256.0f,
        float                        DropDocY = 256.0f);

    [[nodiscard]] bool SaveVectorSpaceToBrowser(
        const std::filesystem::path& TargetPath = {},
        std::string*                 OptionalDiagnostic = nullptr);

    [[nodiscard]] bool LoadVectorSpaceFile(
        const std::filesystem::path& SourcePath,
        std::string*                 OptionalDiagnostic = nullptr);

    [[nodiscard]] bool ExportDocumentToSvgFile(
        const std::filesystem::path& TargetPath = {},
        std::string*                 OptionalDiagnostic = nullptr) const;

    [[nodiscard]] bool ExportDocumentToPngFile(
        const std::filesystem::path& TargetPath = {},
        std::string*                 OptionalDiagnostic = nullptr);

    [[nodiscard]] PatternStampWorkspaceSession&              AccessSession() noexcept       { return Session; }
    [[nodiscard]] const PatternStampWorkspaceSession&        InspectSession() const noexcept { return Session; }
    [[nodiscard]] PatternStampLayerStackPane&                AccessLayerStackPane() noexcept { return LayerStackPane; }
    [[nodiscard]] PatternStampToolHeaderPane&                AccessToolHeaderPane() noexcept { return ToolHeaderPane; }
    [[nodiscard]] PatternStampInspectorPane&                 AccessInspectorPane() noexcept  { return InspectorPane; }
    [[nodiscard]] uint32_t                                   QueryRegisteredTabCount() const noexcept { return RegisteredTabCount; }
    [[nodiscard]] GeometricRaster::VectorCanvasRasterizer*   QueryAttachedRasterizer() const noexcept { return BoundRasterizer; }

private:
    void BindPaneCallbacks();
    void SynchronizeDropTargetBounds(Rectangle2D CanvasWindowLogicalBounds) noexcept;
    void ExecuteCanvasCommandBridge(std::string_view CommandTag);
    void ActivateBrowserEntry(const ContentRepositoryEntry& Entry);
    void ApplyBrowserContentDrop(const ContentRepositoryEntry& Entry, Point2D DropLogicalPos);
    void AssignBrowserColourDrop(ColorRgba AssignedRgba, const ColourSwatchEntry& Entry, Point2D DropLogicalPos);
    void ResolveCanvasDropPick(Point2D DropLogicalPos, float& OutDocX, float& OutDocY, uint32_t& OutPickedLayerId) const;
    void PopulateBrowserStarterPresets();
    [[nodiscard]] std::filesystem::path ResolveWorkspaceVectorDirectory() const;
    [[nodiscard]] std::string           BuildSanitizedDocumentStem() const;

    Configuration::EngineConfiguration         ActiveEngineConfig {};
    std::filesystem::path                      RepositoryRootPath {};
    PatternStampWorkspaceSession               Session            { PatternStampWorkspaceMode::Pattern };
    PatternStampLayerStackPane                 LayerStackPane     {};
    PatternStampToolHeaderPane                 ToolHeaderPane     {};
    PatternStampInspectorPane                  InspectorPane      {};
    uint32_t                                   RegisteredTabCount = 0u;
    GeometricRaster::VectorCanvasRasterizer*   BoundRasterizer    = nullptr;
    ContentBrowserPanel*                       BoundBrowserPanel  = nullptr;
#if FRONTIER_EDITOR_ENABLED
    Editor::Explorer::ContentBrowser*          BoundBrowser       = nullptr;
#endif
};

} // namespace Frontier::Workspaces
