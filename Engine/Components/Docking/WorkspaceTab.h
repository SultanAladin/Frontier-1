/*============================================================================================================================================
                                                          WORKSPACETAB.H
============================================================================================================================================*/
// 🧩 Unrounded trapezoidal workspace docking tab geometry and patched Dear ImGui theme bridge.

#pragma once

#include "Engine/Components/Appearance/ThemeCatalog.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frontier::Components
{

inline constexpr std::size_t MaxWorkspaceTabVertices = 4;
inline constexpr float       DmSansCssToImGuiScale   = 1.302f;

struct ScreenPoint2D
{
    float X = 0.0f;
    float Y = 0.0f;
};

struct RgbaColor8
{
    uint8_t Red   = 0;
    uint8_t Green = 0;
    uint8_t Blue  = 0;
    uint8_t Alpha = 255;
};

struct WorkspaceTabMetrics
{
    float UpperCornerSlantPx = 14.0f; // [px] - Horizontal slope inset of upper corners (PatchA TabSlant)
    float AdjacentOverlapPx  = 24.0f; // [px] - Interlocking horizontal overlap between neighbor tabs (PatchB TabOverlap)
    float TabBodyHeightPx    = 24.0f; // [px] - Unrounded trapezoidal tab height (PatchB TabHeight)
    float StripTopMarginPx   = 4.0f;  // [px] - Strip clearance above tabs; total strip = 28 px (PatchB TabStripPadTop)
    float MinimumTabWidthPx  = 92.0f; // [px] - Minimum interactive tab width
    float CloseDiscRatio     = 0.80f; // [-]  - Circular button diameter relative to tab height
    float DisplayScaleFactor = 1.0f;  // [-]  - Per-monitor DPI scale factor
};

struct WorkspaceThemeSet
{
    RgbaColor8 WorkspaceBackdrop {  0,   0,   0, 255};
    RgbaColor8 PanelSurface      { 10,  10,  10, 255};
    RgbaColor8 ElevatedSection   { 20,  20,  21, 255};
    RgbaColor8 InactiveTabFill   {  6,   6,   7, 255};
    RgbaColor8 HoveredTabFill    { 28,  28,  30, 255};
    RgbaColor8 HairlineBorder    { 26,  26,  28, 255};
    RgbaColor8 PrimaryLabel      {242, 242, 245, 255};
    RgbaColor8 MutedLabel        {128, 128, 136, 255};
    RgbaColor8 EmeraldIndicator  { 34, 197,  94, 255};
    RgbaColor8 AmberAccent       {245, 158,  11, 255};
    RgbaColor8 RejectionCrimson  {239,  68,  68, 255};
};

struct WorkspaceTabContour
{
    std::array<ScreenPoint2D, MaxWorkspaceTabVertices> Vertices{};
    std::size_t                                        VertexCount    = 0;
    float                                              EffectiveSlant = 0.0f;
};

[[nodiscard]] constexpr float ComputeImGuiFontPixelSize(float BrowserCssPixels, float DisplayScale = 1.0f) noexcept
{
    return BrowserCssPixels * DmSansCssToImGuiScale * DisplayScale;
}

[[nodiscard]] WorkspaceTabMetrics ConstructWorkspaceTabMetrics(float DisplayScale = 1.0f) noexcept;

[[nodiscard]] WorkspaceTabContour ConstructWorkspaceTabContour(
    float                      OriginX,
    float                      OriginY,
    float                      WidthPx,
    float                      HeightPx,
    const WorkspaceTabMetrics &Metrics,
    bool                       EnableInvertedSlope = false) noexcept;

[[nodiscard]] bool QueryPointInsideTabContour(const WorkspaceTabContour &Contour, ScreenPoint2D TestPoint) noexcept;

void ConfigureImGuiWorkspaceStyle(
    const WorkspaceTabMetrics &Metrics,
    const WorkspaceThemeSet   &ThemeSet = WorkspaceThemeSet{}) noexcept;

void ConfigureImGuiWorkspaceTheme(
    const WorkspaceTabMetrics &Metrics,
    const ThemeCatalog        &Theme) noexcept;

} // namespace Frontier::Components
