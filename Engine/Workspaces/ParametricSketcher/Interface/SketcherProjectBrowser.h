/*====================================================================================================================================
                                                    SKETCHERPROJECTBROWSER.H
====================================================================================================================================*/
// 🧩 Native in-app SolidArc Project Browser: effortless PC folder/drive navigation (no OS popup), strict .solidarc + folder
//    filtering, and real-geometry CAD previews (MatCap + B&W drafting contour overlay, pure B&W CAD drafting, Studio MatCap)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

struct SketcherWorkspaceStatus;

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class SketcherCadPreviewStyle : uint8_t
{
    MatcapOverlay = 0u,                                                                      // Studio MatCap + crisp B&W CAD contour overlay
    DraftingBw    = 1u,                                                                      // Pure B&W CAD drafting (black occlusion + white silhouettes & hidden edges)
    MatcapStudio  = 2u,                                                                      // Clean Studio MatCap shading
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SketcherBrowserDiagnostic
{
    bool                    OpenCondition    = false;
    std::string             CurrentDirectory = {};
    SketcherCadPreviewStyle PreviewStyle     = SketcherCadPreviewStyle::MatcapOverlay;
    uint32_t                VisibleFolders   = 0u;
    uint32_t                VisibleSolidArcs = 0u;
    uint32_t                ProjectFolders   = 0u;
    uint32_t                RenderedPreviews = 0u;
    std::string             SelectedPath     = {};
};

struct SketcherProjectBrowserSession;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Opens the in-app SolidArc Project Browser at StartDirectory (or the current document folder / Content/SolidArc library).
void OpenSketcherProjectBrowser(SketcherWorkspaceStatus& Workspace, const std::filesystem::path& StartDirectory = {});

// Closes the Project Browser without altering the active SolidArc document.
void CloseSketcherProjectBrowser(SketcherWorkspaceStatus& Workspace);

// Navigates the in-app browser to DirectoryPath (recording Back/Forward history).
bool NavigateSketcherProjectBrowser(SketcherWorkspaceStatus& Workspace, const std::filesystem::path& DirectoryPath);

// Switches the CAD preview style for cards and the interactive 3D reader.
void ConfigureSketcherBrowserPreviewStyle(SketcherWorkspaceStatus& Workspace, SketcherCadPreviewStyle Style);

// Switches the Frontier ProjectBrowser theme preset (Oled / Dark / Light).
void ConfigureSketcherBrowserThemePreset(SketcherWorkspaceStatus& Workspace, ThemePreset Preset);

// Selects a visible entry by path (for interactive or scripted inspection).
bool SelectSketcherBrowserEntry(SketcherWorkspaceStatus& Workspace, const std::filesystem::path& EntryPath);

// Opens the currently selected .solidarc file into SolidArc (or descends into a selected folder).
bool ActivateSketcherBrowserSelection(SketcherWorkspaceStatus& Workspace);

// Renders the full-window in-app SolidArc Project Browser overlay when Workspace.ProjectBrowserOpen is true.
void DrawSketcherProjectBrowserOverlay(SketcherWorkspaceStatus& Workspace);

// Releases GPU/ImGui preview textures owned by the Project Browser session.
void ReleaseSketcherProjectBrowserTextures(SketcherWorkspaceStatus& Workspace) noexcept;

// Queries diagnostic counters and rendered thumbnail availability (used by automated validation suites).
[[nodiscard]] SketcherBrowserDiagnostic InspectSketcherProjectBrowser(const SketcherWorkspaceStatus& Workspace);

// Renders a standalone CAD preview RGBA8 buffer directly from any .solidarc file in the requested style.
[[nodiscard]] bool RenderSolidArcCadPreviewRgba(
    const std::filesystem::path& FilePath,
    SketcherCadPreviewStyle      Style,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    bool                         HiddenEdgesCondition,
    bool                         OrthographicCondition,
    float                        YawRadians,
    float                        PitchRadians,
    float                        ZoomFactor,
    std::vector<uint8_t>&        OutRgba);

} // namespace Frontier::Workspaces::Sketcher
