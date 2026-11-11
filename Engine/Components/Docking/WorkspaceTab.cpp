/*============================================================================================================================================
                                                         WORKSPACETAB.CPP
============================================================================================================================================*/
// 🧩 Unrounded (0.0f) trapezoidal docking tab geometry and patched Dear ImGui theme synchronization.

#include "Engine/Components/Docking/WorkspaceTab.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Frontier::Components
{

namespace
{

[[nodiscard]] ImVec4 ToImVec4(RgbaColor8 Color) noexcept
{
    constexpr float Inv = 1.0f / 255.0f;
    return ImVec4(static_cast<float>(Color.Red) * Inv, static_cast<float>(Color.Green) * Inv,
                  static_cast<float>(Color.Blue) * Inv, static_cast<float>(Color.Alpha) * Inv);
}

[[nodiscard]] ImVec4 QuadToImVec4(ColorQuad Color) noexcept
{
    return ImVec4(Color.Red, Color.Green, Color.Blue, Color.Alpha);
}

} // namespace

WorkspaceTabMetrics ConstructWorkspaceTabMetrics(float DisplayScale) noexcept
{
    const float         SafeScale = (DisplayScale > 0.1f) ? DisplayScale : 1.0f;
    WorkspaceTabMetrics Metrics{};
    Metrics.UpperCornerSlantPx = std::trunc(14.0f * SafeScale);
    Metrics.AdjacentOverlapPx  = std::trunc(24.0f * SafeScale);
    Metrics.TabBodyHeightPx    = std::trunc(24.0f * SafeScale);
    Metrics.StripTopMarginPx   = std::trunc(4.0f * SafeScale);
    Metrics.MinimumTabWidthPx  = std::trunc(92.0f * SafeScale);
    Metrics.CloseDiscRatio     = 0.80f;
    Metrics.DisplayScaleFactor = SafeScale;
    return Metrics;
}

WorkspaceTabContour ConstructWorkspaceTabContour(
    float                      OriginX,
    float                      OriginY,
    float                      WidthPx,
    float                      HeightPx,
    const WorkspaceTabMetrics &Metrics,
    bool                       EnableInvertedSlope) noexcept
{
    WorkspaceTabContour Contour{};
    if (WidthPx <= 1.0f || HeightPx <= 1.0f)
        return Contour;

    const float Slant      = std::min(Metrics.UpperCornerSlantPx, WidthPx * 0.16f);
    Contour.EffectiveSlant = Slant;
    Contour.VertexCount    = 4u;

    const float LeftX   = OriginX;
    const float RightX  = OriginX + WidthPx;
    const float TopY    = OriginY;
    const float BottomY = OriginY + HeightPx;

    if (!EnableInvertedSlope)
    {
        Contour.Vertices[0] = {LeftX, BottomY};
        Contour.Vertices[1] = {LeftX + Slant, TopY};
        Contour.Vertices[2] = {RightX - Slant, TopY};
        Contour.Vertices[3] = {RightX, BottomY};
    }
    else
    {
        Contour.Vertices[0] = {LeftX, TopY};
        Contour.Vertices[1] = {RightX, TopY};
        Contour.Vertices[2] = {RightX - Slant, BottomY};
        Contour.Vertices[3] = {LeftX + Slant, BottomY};
    }
    return Contour;
}

bool QueryPointInsideTabContour(const WorkspaceTabContour &Contour, ScreenPoint2D TestPoint) noexcept
{
    if (Contour.VertexCount < 3u)
        return false;

    bool        Inside        = false;
    std::size_t PreviousIndex = Contour.VertexCount - 1u;
    for (std::size_t Index = 0u; Index < Contour.VertexCount; PreviousIndex = Index++)
    {
        const ScreenPoint2D A = Contour.Vertices[Index];
        const ScreenPoint2D B = Contour.Vertices[PreviousIndex];
        if ((A.Y > TestPoint.Y) != (B.Y > TestPoint.Y))
        {
            const float IntersectX = (B.X - A.X) * (TestPoint.Y - A.Y) / (B.Y - A.Y) + A.X;
            if (TestPoint.X < IntersectX)
                Inside = !Inside;
        }
    }
    return Inside;
}

void ConfigureImGuiWorkspaceStyle(const WorkspaceTabMetrics &Metrics, const WorkspaceThemeSet &ThemeSet) noexcept
{
    ImGuiStyle &Style = ImGui::GetStyle();

    Style.WindowPadding        = ImVec2(10.0f, 10.0f);
    Style.WindowRounding       = 0.0f;
    Style.WindowBorderSize     = 1.0f;
    Style.ChildRounding        = 4.0f;
    Style.ChildBorderSize      = 1.0f;
    Style.FramePadding         = ImVec2(10.0f, 5.0f);
    Style.FrameRounding        = 4.0f;
    Style.FrameBorderSize      = 1.0f;
    Style.ItemSpacing          = ImVec2(8.0f, 6.0f);
    Style.ItemInnerSpacing     = ImVec2(6.0f, 4.0f);
    Style.IndentSpacing        = 14.0f;
    Style.ScrollbarSize        = 9.0f;
    Style.ScrollbarRounding    = 4.0f;
    Style.DockingSeparatorSize = 1.0f;

    // Strictly zero corner rounding on trapezoidal workspace tabs!
    Style.TabRounding                      = 0.0f;
    Style.TabBorderSize                    = 1.0f;
    Style.TabSlant                         = Metrics.UpperCornerSlantPx;
    Style.TabOverlap                       = Metrics.AdjacentOverlapPx;
    Style.TabHeight                        = Metrics.TabBodyHeightPx;
    Style.TabStripPadTop                   = Metrics.StripTopMarginPx;
    Style.TabButtonRounding                = 1.0f;
    Style.TabBarBorderSize                 = 0.0f;
    Style.TabBarOverlineSize               = 0.0f;
    Style.TabMinWidthBase                  = Metrics.MinimumTabWidthPx;
    Style.TabCloseButtonMinWidthSelected   = -1.0f;
    Style.TabCloseButtonMinWidthUnselected = -1.0f;

    ImVec4 *Colors                       = Style.Colors;
    Colors[ImGuiCol_Text]                = ToImVec4(ThemeSet.PrimaryLabel);
    Colors[ImGuiCol_TextDisabled]        = ToImVec4(ThemeSet.MutedLabel);
    Colors[ImGuiCol_WindowBg]            = ToImVec4(ThemeSet.PanelSurface);
    Colors[ImGuiCol_ChildBg]             = ToImVec4(ThemeSet.ElevatedSection);
    Colors[ImGuiCol_Border]              = ToImVec4(ThemeSet.HairlineBorder);
    Colors[ImGuiCol_BorderShadow]        = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    Colors[ImGuiCol_FrameBg]             = ToImVec4(ThemeSet.ElevatedSection);
    Colors[ImGuiCol_FrameBgHovered]      = ToImVec4(ThemeSet.HoveredTabFill);
    Colors[ImGuiCol_FrameBgActive]       = ToImVec4(ThemeSet.HoveredTabFill);
    Colors[ImGuiCol_TitleBg]             = ToImVec4(ThemeSet.WorkspaceBackdrop);
    Colors[ImGuiCol_TitleBgActive]       = ToImVec4(ThemeSet.WorkspaceBackdrop);
    Colors[ImGuiCol_TitleBgCollapsed]    = ToImVec4(ThemeSet.WorkspaceBackdrop);
    Colors[ImGuiCol_Header]              = ToImVec4(ThemeSet.ElevatedSection);
    Colors[ImGuiCol_HeaderHovered]       = ToImVec4(ThemeSet.HoveredTabFill);
    Colors[ImGuiCol_HeaderActive]        = ToImVec4(ThemeSet.HoveredTabFill);
    Colors[ImGuiCol_Separator]           = ToImVec4(ThemeSet.HairlineBorder);
    Colors[ImGuiCol_SeparatorHovered]    = ToImVec4(ThemeSet.AmberAccent);
    Colors[ImGuiCol_SeparatorActive]     = ToImVec4(ThemeSet.AmberAccent);
    Colors[ImGuiCol_Button]              = ToImVec4(ThemeSet.ElevatedSection);
    Colors[ImGuiCol_ButtonHovered]       = ToImVec4(ThemeSet.HoveredTabFill);
    Colors[ImGuiCol_ButtonActive]        = ToImVec4(ThemeSet.PanelSurface);
    Colors[ImGuiCol_Tab]                 = ToImVec4(ThemeSet.InactiveTabFill);
    Colors[ImGuiCol_TabHovered]          = ToImVec4(ThemeSet.HoveredTabFill);
    Colors[ImGuiCol_TabSelected]         = ToImVec4(ThemeSet.PanelSurface);
    Colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    Colors[ImGuiCol_TabDimmed]           = ToImVec4(ThemeSet.InactiveTabFill);
    Colors[ImGuiCol_TabDimmedSelected]   = ToImVec4(ThemeSet.PanelSurface);
    Colors[ImGuiCol_DockingEmptyBg]      = ToImVec4(ThemeSet.WorkspaceBackdrop);
    Colors[ImGuiCol_DockingPreview]      = ImVec4(0.231f, 0.510f, 0.965f, 0.35f);
}

void ConfigureImGuiWorkspaceTheme(const WorkspaceTabMetrics &Metrics, const ThemeCatalog &Theme) noexcept
{
    ConfigureImGuiWorkspaceStyle(Metrics);

    const ThemeSurfaceSet &Set     = Theme.QuerySurfaceSet();
    const ColorQuad        Accent  = Theme.QueryAccentColor();
    const ColorQuad        Darkest = Theme.QueryDarkestSurface();

    ImGuiStyle &Style                    = ImGui::GetStyle();
    ImVec4     *Colors                   = Style.Colors;
    Colors[ImGuiCol_Text]                = QuadToImVec4(Set.TextMain);
    Colors[ImGuiCol_TextDisabled]        = QuadToImVec4(Set.TextMuted);
    Colors[ImGuiCol_WindowBg]            = QuadToImVec4(Set.PanelBackground);
    Colors[ImGuiCol_ChildBg]             = QuadToImVec4(Set.SectionBackground);
    Colors[ImGuiCol_Border]              = QuadToImVec4(Set.SubtleBackground);
    Colors[ImGuiCol_TitleBg]             = QuadToImVec4(Darkest);
    Colors[ImGuiCol_TitleBgActive]       = QuadToImVec4(Darkest);
    Colors[ImGuiCol_TitleBgCollapsed]    = QuadToImVec4(Darkest);
    Colors[ImGuiCol_Separator]           = QuadToImVec4(Set.SubtleBackground);
    Colors[ImGuiCol_SeparatorHovered]    = QuadToImVec4(Accent);
    Colors[ImGuiCol_SeparatorActive]     = QuadToImVec4(Accent);
    Colors[ImGuiCol_Tab]                 = QuadToImVec4(Set.MainBackground);
    Colors[ImGuiCol_TabHovered]          = QuadToImVec4(Set.SubtleBackground);
    Colors[ImGuiCol_TabSelected]         = QuadToImVec4(Set.PanelBackground);
    Colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    Colors[ImGuiCol_TabDimmed]           = QuadToImVec4(Set.MainBackground);
    Colors[ImGuiCol_TabDimmedSelected]   = QuadToImVec4(Set.PanelBackground);
    Colors[ImGuiCol_DockingEmptyBg]      = QuadToImVec4(Set.MainBackground);
    Colors[ImGuiCol_DockingPreview]      = ImVec4(Accent.Red, Accent.Green, Accent.Blue, 0.35f);
}

} // namespace Frontier::Components
