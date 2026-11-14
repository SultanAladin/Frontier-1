/*============================================================================================================================================
                                                              VIEWPORTPANEL.CPP
============================================================================================================================================*/
// 🧩 Workspace Viewport pane with Edit/Simulate/Play-Pause strip, Construct sheet, Split Viewport, Diagnostics, and Settings

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/ExclusiveSelector.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <vector>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool CaseInsensitiveMatch(std::string_view Haystack, std::string_view Needle) noexcept
{
    if (Needle.empty())
    {
        return true;
    }
    if (Haystack.size() < Needle.size())
    {
        return false;
    }

    for (std::size_t Offset = 0u; Offset + Needle.size() <= Haystack.size(); ++Offset)
    {
        bool Match = true;
        for (std::size_t Index = 0u; Index < Needle.size(); ++Index)
        {
            const auto Left  = static_cast<unsigned char>(Haystack[Offset + Index]);
            const auto Right = static_cast<unsigned char>(Needle[Index]);
            if (std::tolower(Left) != std::tolower(Right))
            {
                Match = false;
                break;
            }
        }
        if (Match)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const char* QueryQualityTierName(uint32_t Tier) noexcept
{
    constexpr std::array<const char*, 5> Names = {{ "Low", "Medium", "Standard", "High", "Ultra" }};
    return Names[std::min<std::size_t>(Tier, Names.size() - 1u)];
}

[[nodiscard]] const char* QueryDebugChannelName(uint32_t Channel) noexcept
{
    constexpr std::array<const char*, 5> Names = {{ "Off", "Depth", "Albedo", "Normal", "Roughness" }};
    return Names[std::min<std::size_t>(Channel, Names.size() - 1u)];
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             LIFECYCLE & MODE CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

ViewportPanel::ViewportPanel() noexcept
{
    for (std::size_t Index = 0u; Index < IntervalHistoryMs.size(); ++Index)
    {
        const float Phase         = static_cast<float>(Index) * 0.19f;
        IntervalHistoryMs[Index] = 16.67f + 0.75f * std::sin(Phase) + 0.28f * std::cos(Phase * 2.3f);
    }
}

ViewportPanel::~ViewportPanel() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void ViewportPanel::ConfigureSceneTitle(std::string_view Title)
{
    SceneTitle.assign(Title.begin(), Title.end());
}

void ViewportPanel::ConfigureExecutionMode(ViewportExecutionMode Mode, bool Paused) noexcept
{
    ExecutionMode = Mode;
    PlayPaused    = (Mode == ViewportExecutionMode::Play) ? Paused : false;
}

void ViewportPanel::TogglePlayPause() noexcept
{
    if (ExecutionMode != ViewportExecutionMode::Play)
    {
        ExecutionMode = ViewportExecutionMode::Play;
        PlayPaused    = false;
    }
    else
    {
        PlayPaused = !PlayPaused;
    }
}

void ViewportPanel::ConfigureSplitViewport(bool SplitEnabled) noexcept
{
    SplitViewportEnabled = SplitEnabled;
    if (!SplitViewportEnabled)
    {
        ActivePaneIndex = 0u;
    }
}

void ViewportPanel::ConfigureDiagnosticsVisible(bool Visible) noexcept
{
    DiagnosticsVisible = Visible;
}

void ViewportPanel::ConfigureSettingsSheetOpen(bool Open) noexcept
{
    SettingsSheetOpen = Open;
    if (Open)
    {
        ConstructSheetOpen     = false;
        ProjectionDropdownOpen = false;
    }
}

void ViewportPanel::ConfigureConstructSheetOpen(bool Open) noexcept
{
    ConstructSheetOpen = Open;
    if (Open)
    {
        SettingsSheetOpen      = false;
        ProjectionDropdownOpen = false;
    }
}

void ViewportPanel::ConfigurePrimaryProjection(ViewportProjectionPreset Preset) noexcept
{
    PrimaryProjection = Preset;
}

void ViewportPanel::ConfigureSecondaryProjection(ViewportProjectionPreset Preset) noexcept
{
    SecondaryProjection = Preset;
}

void ViewportPanel::PushDiagnosticsSample(double Δt) noexcept
{
    if (DiagnosticsPaused)
    {
        return;
    }

    const float ClampedMs = (Δt > 1.0e-5)
                                ? std::clamp(static_cast<float>(Δt * 1000.0), 1.0f, 120.0f)
                                : 16.67f;
    IntervalHistoryMs[HistoryWriteCursor % IntervalHistoryMs.size()] = ClampedMs;
    ++HistoryWriteCursor;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      CANVAS, DIAGNOSTICS & OVERLAY SUB-RENDERERS
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::RenderCanvasPane(
    PixelSpace&              Surface,
    Rectangle2D              PaneBounds,
    ViewportProjectionPreset Projection,
    uint32_t                 PaneIndex,
    uint32_t                 ConstructedCount,
    const PaneInputReading&  Input)
{
    if (PaneBounds.Width <= 4.0f || PaneBounds.Height <= 4.0f)
    {
        return;
    }

    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    Surface.PushClip(PaneBounds);

    // Subtle dark checker canvas matching reference layout (#161616 / #1B1B1B)
    Surface.FillRectangle(PaneBounds, ColorRgba{ 22, 22, 22, 255 });
    constexpr float CellSize = 24.0f;
    const int       Cols     = static_cast<int>( std::ceil(PaneBounds.Width / CellSize)) + 1;
    const int       Rows     = static_cast<int>(std::ceil(PaneBounds.Height / CellSize)) + 1;
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = (Row & 1); Col < Cols; Col += 2)
        {
            const float CellX = PaneBounds.X + static_cast<float>(Col) * CellSize;
            const float CellY = PaneBounds.Y + static_cast<float>(Row) * CellSize;
            Surface.FillRectangle(
                { CellX, CellY, std::min(CellSize, PaneBounds.Right() - CellX), std::min(CellSize, PaneBounds.Bottom() - CellY) },
                ColorRgba{ 27, 27, 27, 255 });
        }
    }

    if (SplitViewportEnabled)
    {
        // Active pane perimeter & top-left projection chip
        if (ActivePaneIndex == PaneIndex)
        {
            Surface.StrokeRectangle(PaneBounds.Inset(0.5f), ColorRgba{ 191, 162, 122, 115 }, 0.0f, 1.0f);
        }

        const char*       ProjName = WorkspaceAppearance::QueryProjectionPresetLabel(Projection);
        const float       ChipW    = std::max(132.0f, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.5f, ProjName) + 56.0f);
        const Rectangle2D ProjChip{ PaneBounds.X + 10.0f, PaneBounds.Y + 10.0f, ChipW, 30.0f };

        const ControlContact ChipHit = DropdownSelector::ConstructButton(Surface, ProjChip, ProjName, false, Input.PrimaryPointer);
        if (ChipHit.Pressed)
        {
            const uint8_t NextOrd = static_cast<uint8_t>((static_cast<uint8_t>(Projection) + 1u) %
                                                         static_cast<uint8_t>(ViewportProjectionPreset::Count));
            if (PaneIndex == 0u)
            {
                PrimaryProjection = static_cast<ViewportProjectionPreset>(NextOrd);
            }
            else
            {
                SecondaryProjection = static_cast<ViewportProjectionPreset>(NextOrd);
            }
        }
        else if (PaneBounds.Contains(Cursor) && Input.PrimaryPointer.Pressed && !ConstructSheetOpen && !SettingsSheetOpen)
        {
            ActivePaneIndex = PaneIndex;
        }
    }

    // Center quiet empty-scene prompt when 0 constructed (exact reference layout layout)
    if (ConstructedCount == 0u)
    {
        const char* LineOne = "No constructed entries";
        const char* LineTwo = "Ctrl+A to construct";
        const float W1      = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 12.5f, LineOne);
        const float W2      = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, LineTwo);
        const float MidY    = PaneBounds.Center().Y;

        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { PaneBounds.Center().X - W1 * 0.5f, MidY - 12.0f },
            12.5f,
            ColorRgba{ 118, 118, 118, 255 },
            LineOne);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { PaneBounds.Center().X - W2 * 0.5f, MidY + 8.0f },
            10.5f,
            ColorRgba{ 86, 86, 86, 255 },
            LineTwo);
    }
    else
    {
        char SummaryLine[96]{};
        std::snprintf(
            SummaryLine,
            sizeof(SummaryLine),
            "%u constructed %s in workspace",
            ConstructedCount,
            (ConstructedCount == 1u) ? "entry" : "entries");
        const float SumW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.5f, SummaryLine);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { PaneBounds.Center().X - SumW * 0.5f, PaneBounds.Center().Y - 6.0f },
            11.5f,
            ColorRgba{ 118, 118, 118, 255 },
            SummaryLine);
    }

    // Bottom-left analytical caption (matching reference layout)
    if (PaneIndex == 0u)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { PaneBounds.X + 12.0f, PaneBounds.Bottom() - 20.0f },
            8.5f,
            ColorRgba{ 92, 92, 92, 255 },
            "NATIVE VIEWPORT  -  ANALYTICAL MARKERS");
    }

    // Bottom-right reusable + | - | Fit navigation capsule above 3D X/Y/Z orientation triad sign
    if (!SplitViewportEnabled || ActivePaneIndex == PaneIndex)
    {
        LastCanvasPaneBounds = PaneBounds;
    }
    if (PaneBounds.Width >= 140.0f && PaneBounds.Height >= 186.0f &&
        (!SplitViewportEnabled || ActivePaneIndex == PaneIndex))
    {
        ControlPointer NavPointer = Input.PrimaryPointer;
        if (ConstructSheetOpen || SettingsSheetOpen)
        {
            NavPointer = {};
        }
        const ViewportNavigationDirective NavDirective =
            CapsuleButton::ConstructViewportNavigationCapsule(
                Surface,
                PaneBounds,
                NavPointer,
                ViewportNavigationCapsuleSpec{
                    "Zoom viewport in.",
                    "Zoom viewport out.",
                    "Fit active scene view.",
                    12.0f,
                    68.0f,
                });
        if (NavDirective == ViewportNavigationDirective::ZoomIn)
        {
            ViewportZoomScale = std::clamp(ViewportZoomScale * 1.15f, 0.25f, 4.0f);
        }
        else if (NavDirective == ViewportNavigationDirective::ZoomOut)
        {
            ViewportZoomScale = std::clamp(ViewportZoomScale / 1.15f, 0.25f, 4.0f);
        }
        else if (NavDirective == ViewportNavigationDirective::FitView)
        {
            ViewportZoomScale = 1.0f;
        }
        if (NavDirective != ViewportNavigationDirective::None && OnNavigationDirective)
        {
            OnNavigationDirective(NavDirective);
        }
    }

    // Bottom-right 3D X/Y/Z orientation triad sign
    WorkspaceAppearance::ConstructOrientationAxesSign(Surface, PaneBounds, Projection);

    Surface.PopClip();
}

void ViewportPanel::RenderDiagnosticsPanel(
    PixelSpace&             Surface,
    Rectangle2D             CanvasBounds,
    uint32_t                ConstructedCount,
    uint32_t                VisibleCount,
    const PaneInputReading& Input)
{
    const float PanelW = std::min(324.0f, std::max(220.0f, CanvasBounds.Width - 28.0f));
    const float PanelH = std::min(432.0f, std::max(260.0f, CanvasBounds.Height - 28.0f));
    const Rectangle2D Panel{
        CanvasBounds.Right() - PanelW - 14.0f,
        CanvasBounds.Y + 14.0f,
        PanelW,
        PanelH
    };

    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    Surface.FillRectangle(Panel, ColorRgba{ 32, 32, 32, 246 }, 24.0f);
    Surface.StrokeRectangle(Panel, ColorRgba{ 255, 255, 255, 28 }, 24.0f, 1.0f);

    Surface.PushClip(Panel);

    // Header (diagnostics header)
    const Rectangle2D CloseBtn{ Panel.X + 14.0f, Panel.Y + 13.0f, 28.0f, 28.0f };
    const bool        CloseHover = CloseBtn.Contains(Cursor);
    Surface.FillCircle(CloseBtn.Center(), 14.0f, CloseHover ? ColorRgba{ 54, 58, 49, 255 } : ColorRgba{ 43, 45, 40, 255 });
    VectorIconRepository::ConstructControl(
        Surface,
        ControlIcon::CloseCross,
        VectorIconPlacement{ CloseBtn.X + 7.5f, CloseBtn.Y + 7.5f, 13.0f, ColorRgba{ 192, 197, 184, 255 }, 1.85f });
    if (CloseHover && Input.PrimaryPointer.Pressed)
    {
        DiagnosticsVisible = false;
    }

    WorkspaceAppearance::ConstructDiagnosticsIcon(Surface, { CloseBtn.Right() + 10.0f, Panel.Y + 19.0f }, 15.0f, ColorRgba{ 166, 185, 151, 255 });
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { CloseBtn.Right() + 31.0f, Panel.Y + 19.5f },
        12.0f,
        ColorRgba{ 214, 216, 209, 255 },
        "Diagnostics");
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Medium,
        { Panel.Right() - 32.0f, Panel.Y + 22.0f },
        8.0f,
        ColorRgba{ 129, 142, 119, 255 },
        "F3");

    // Category capsules: Timing / Scene / Render
    float TabY = Panel.Y + 50.0f;
    {
        const float TabW = (Panel.Width - 42.0f - 12.0f) / 3.0f;
        const std::array<std::pair<const char*, DiagnosticsTabCategory>, 3> Tabs = {{
            { "Timing", DiagnosticsTabCategory::Timing },
            { "Scene",  DiagnosticsTabCategory::Scene  },
            { "Render", DiagnosticsTabCategory::Render },
        }};
        for (std::size_t Idx = 0u; Idx < Tabs.size(); ++Idx)
        {
            const Rectangle2D TabRect{ Panel.X + 21.0f + static_cast<float>(Idx) * (TabW + 6.0f), TabY, TabW, 25.0f };
            const bool        Selected = (DiagnosticsCategory == Tabs[Idx].second);
            Surface.FillRectangle(TabRect, Selected ? ColorRgba{ 62, 68, 56, 255 } : ColorRgba{ 43, 45, 40, 255 }, 12.5f);
            Surface.StrokeRectangle(TabRect, Selected ? ColorRgba{ 166, 185, 151, 115 } : ColorRgba{ 255, 255, 255, 14 }, 12.5f, 1.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, Tabs[Idx].first);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { TabRect.Center().X - TW * 0.5f, TabRect.Y + 6.5f },
                10.0f,
                Selected ? ColorRgba{ 226, 232, 218, 255 } : ColorRgba{ 182, 188, 174, 255 },
                Tabs[Idx].first);
            if (TabRect.Contains(Cursor) && Input.PrimaryPointer.Pressed)
            {
                DiagnosticsCategory = Tabs[Idx].second;
            }
        }
    }

    float BodyY = TabY + 38.0f;
    if (DiagnosticsCategory == DiagnosticsTabCategory::Timing)
    {
        std::vector<float> Sorted(IntervalHistoryMs.begin(), IntervalHistoryMs.end());
        std::sort(Sorted.begin(), Sorted.end());
        const float MinMs = Sorted.front();
        const float MaxMs = Sorted.back();
        const float AvgMs = std::accumulate(Sorted.begin(), Sorted.end(), 0.0f) / static_cast<float>(Sorted.size());
        const float P95Ms = Sorted[static_cast<std::size_t>(Sorted.size() * 95u / 100u)];
        const float CurMs = IntervalHistoryMs[(HistoryWriteCursor + IntervalHistoryMs.size() - 1u) % IntervalHistoryMs.size()];

        Surface.FillRectangle({ Panel.X + 21.0f, BodyY + 2.0f, 3.0f, 14.0f }, ColorRgba{ 88, 153, 121, 255 }, 1.5f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { Panel.X + 31.0f, BodyY },
            12.5f,
            ColorRgba{ 214, 216, 209, 255 },
            "Frame time");

        char BigMs[24]{};
        std::snprintf(BigMs, sizeof(BigMs), "%.1f", CurMs);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Light,
            { Panel.X + 21.0f, BodyY + 22.0f },
            44.0f,
            ColorRgba{ 214, 216, 209, 255 },
            BigMs);
        const float BigW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 44.0f, BigMs);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Light,
            { Panel.X + 28.0f + BigW, BodyY + 48.0f },
            14.0f,
            ColorRgba{ 138, 145, 130, 255 },
            "ms  (60.0 fps)");

        // Sparkline plot
        const Rectangle2D Plot{ Panel.X + 21.0f, BodyY + 78.0f, Panel.Width - 42.0f, 92.0f };
        Surface.FillRectangle(Plot, ColorRgba{ 22, 24, 21, 255 }, 10.0f);
        Surface.StrokeLine({ Plot.X, Plot.Center().Y }, { Plot.Right(), Plot.Center().Y }, ColorRgba{ 255, 255, 255, 16 }, 1.0f);

        std::array<Point2D, 60> Poly{};
        const float             Lo = std::min(12.0f, MinMs - 1.0f);
        const float             Hi = std::max(22.0f, MaxMs + 1.0f);
        for (std::size_t Step = 0u; Step < Poly.size(); ++Step)
        {
            const std::size_t SampleIdx = (HistoryWriteCursor + Step * 2u) % IntervalHistoryMs.size();
            const float       NormX     = static_cast<float>(Step) / static_cast<float>(Poly.size() - 1u);
            const float       NormY     = std::clamp((IntervalHistoryMs[SampleIdx] - Lo) / std::max(0.5f, Hi - Lo), 0.0f, 1.0f);
            Poly[Step]                  = { Plot.X + 6.0f + NormX * (Plot.Width - 12.0f), Plot.Bottom() - 10.0f - NormY * (Plot.Height - 20.0f) };
        }
        Surface.StrokePolyline(Poly, ColorRgba{ 142, 198, 152, 240 }, 1.6f, false);

        float StatY = Plot.Bottom() + 12.0f;
        const std::array<std::pair<const char*, float>, 4> Rows = {{
            { "Average",    AvgMs },
            { "Minimum",    MinMs },
            { "95th pct",   P95Ms },
            { "Maximum",    MaxMs },
        }};
        for (const auto& [Name, Val] : Rows)
        {
            if (StatY + 20.0f > Panel.Bottom() - 40.0f)
            {
                break;
            }
            Surface.StrokeLine({ Panel.X + 21.0f, StatY }, { Panel.Right() - 21.0f, StatY }, ColorRgba{ 255, 255, 255, 15 }, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Panel.X + 21.0f, StatY + 5.0f }, 10.0f, ColorRgba{ 144, 153, 134, 255 }, Name);
            char ValStr[24]{};
            std::snprintf(ValStr, sizeof(ValStr), "%.2f ms", Val);
            const float VW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, ValStr);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Panel.Right() - 21.0f - VW, StatY + 5.0f }, 10.0f, ColorRgba{ 193, 202, 180, 255 }, ValStr);
            StatY += 22.0f;
        }
    }
    else if (DiagnosticsCategory == DiagnosticsTabCategory::Scene)
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Panel.X + 21.0f, BodyY }, 12.5f, ColorRgba{ 214, 216, 209, 255 }, "Scene inventory");
        char BigCnt[24]{};
        std::snprintf(BigCnt, sizeof(BigCnt), "%u", ConstructedCount);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { Panel.X + 21.0f, BodyY + 24.0f }, 44.0f, ColorRgba{ 214, 216, 209, 255 }, BigCnt);

        char VisSummary[64]{};
        std::snprintf(VisSummary, sizeof(VisSummary), "Visible: %u   Hidden: %u", VisibleCount, ConstructedCount - VisibleCount);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Panel.X + 21.0f, BodyY + 80.0f }, 11.0f, ColorRgba{ 158, 168, 148, 255 }, VisSummary);
    }
    else
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Panel.X + 21.0f, BodyY }, 12.5f, ColorRgba{ 214, 216, 209, 255 }, "Render configuration");
        char ScaleText[48]{};
        std::snprintf(ScaleText, sizeof(ScaleText), "%.0f%%", DisplaySettings.RenderScalePercent);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { Panel.X + 21.0f, BodyY + 24.0f }, 44.0f, ColorRgba{ 214, 216, 209, 255 }, ScaleText);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { Panel.X + 21.0f, BodyY + 80.0f },
            11.0f,
            ColorRgba{ 158, 168, 148, 255 },
            QueryQualityTierName(DisplaySettings.QualityTier));
    }

    // Footer controls (.debug-controls: Pause / 120 f / Clear)
    const float FooterY = Panel.Bottom() - 36.0f;
    Surface.StrokeLine({ Panel.X + 16.0f, FooterY - 4.0f }, { Panel.Right() - 16.0f, FooterY - 4.0f }, ColorRgba{ 255, 255, 255, 15 }, 1.0f);

    const Rectangle2D PauseBtn{ Panel.X + 21.0f, FooterY, 58.0f, 24.0f };
    Surface.FillRectangle(PauseBtn, ColorRgba{ 43, 45, 40, 255 }, 12.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { PauseBtn.X + 13.0f, PauseBtn.Y + 6.0f },
        9.5f,
        ColorRgba{ 192, 197, 184, 255 },
        DiagnosticsPaused ? "Resume" : "Pause");
    if (PauseBtn.Contains(Cursor) && Input.PrimaryPointer.Pressed)
    {
        DiagnosticsPaused = !DiagnosticsPaused;
    }

    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { Panel.Right() - 84.0f, FooterY + 7.0f },
        8.5f,
        ColorRgba{ 120, 133, 108, 255 },
        "120 samples");

    Surface.PopClip();
}

void ViewportPanel::RenderConstructDialog(
    PixelSpace&             Overlay,
    Rectangle2D             Bounds,
    const PaneInputReading& Input)
{
    const float DialogW = std::min(532.0f, std::max(300.0f, Bounds.Width - 36.0f));
    const float DialogH = std::min(346.0f, std::max(240.0f, Bounds.Height - 70.0f));
    const Rectangle2D Dialog{
        Bounds.Center().X - DialogW * 0.5f,
        Bounds.Y + 94.0f,
        DialogW,
        DialogH
    };

    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    Overlay.FillRectangle(Dialog, ColorRgba{ 23, 23, 23, 254 }, 14.0f);
    Overlay.StrokeRectangle(Dialog, ColorRgba{ 58, 58, 58, 255 }, 14.0f, 1.0f);

    Overlay.PushClip(Dialog);

    // Header + Search
    WorkspaceAppearance::ConstructConstructIcon(Overlay, { Dialog.X + 18.0f, Dialog.Y + 16.0f }, 18.0f, WorkspaceSurfaceColors::PrimaryInk);
    WorkspaceAppearance::ConstructWeightedText(Overlay, FontWeightClass::Regular, { Dialog.X + 44.0f, Dialog.Y + 16.0f }, 14.0f, WorkspaceSurfaceColors::PrimaryInk, "Construct");
    WorkspaceAppearance::ConstructWeightedText(Overlay, FontWeightClass::Regular, { Dialog.X + 118.0f, Dialog.Y + 19.0f }, 9.5f, WorkspaceSurfaceColors::FaintInk, "Ctrl+A");

    const Rectangle2D CloseBtn{ Dialog.Right() - 38.0f, Dialog.Y + 12.0f, 26.0f, 26.0f };
    if (CloseBtn.Contains(Cursor))
    {
        Overlay.FillCircle(CloseBtn.Center(), 13.0f, ColorRgba{ 44, 44, 44, 255 });
    }
    VectorIconRepository::ConstructControl(Overlay, ControlIcon::CloseCross, VectorIconPlacement{ CloseBtn.X + 6.5f, CloseBtn.Y + 6.5f, 13.0f, ColorRgba{ 168, 168, 168, 255 }, 1.85f });
    if (CloseBtn.Contains(Cursor) && Input.PrimaryPointer.Pressed)
    {
        ConstructSheetOpen = false;
    }

    Overlay.StrokeLine({ Dialog.X, Dialog.Y + 48.0f }, { Dialog.Right(), Dialog.Y + 48.0f }, WorkspaceSurfaceColors::HairlineLight, 1.0f);

    // Left category rail (124px) + Right constructable grid
    const float RailW = 128.0f;
    Overlay.StrokeLine({ Dialog.X + RailW, Dialog.Y + 48.0f }, { Dialog.X + RailW, Dialog.Bottom() }, WorkspaceSurfaceColors::HairlineLight, 1.0f);

    const std::array<std::pair<const char*, ConstructCategoryRail>, 3> Rails = {{
        { "All",          ConstructCategoryRail::All          },
        { "Items",        ConstructCategoryRail::ConstructSet },
        { "Organization", ConstructCategoryRail::Organization },
    }};
    for (std::size_t Idx = 0u; Idx < Rails.size(); ++Idx)
    {
        const Rectangle2D RailRow{ Dialog.X + 10.0f, Dialog.Y + 60.0f + static_cast<float>(Idx) * 32.0f, RailW - 20.0f, 28.0f };
        const bool        Active = (ConstructRail == Rails[Idx].second);
        if (Active || RailRow.Contains(Cursor))
        {
            Overlay.FillRectangle(RailRow, Active ? ColorRgba{ 44, 44, 44, 255 } : ColorRgba{ 33, 33, 33, 255 }, 8.0f);
        }
        WorkspaceAppearance::ConstructWeightedText(
            Overlay,
            FontWeightClass::Regular,
            { RailRow.X + 10.0f, RailRow.Y + 7.5f },
            11.0f,
            Active ? WorkspaceSurfaceColors::PrimaryInk : ColorRgba{ 158, 158, 158, 255 },
            Rails[Idx].first);
        if (RailRow.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            ConstructRail = Rails[Idx].second;
        }
    }

    // Right matrix of 1:1 square constructable item boxes (Width == Height always, with rounded corners)
    const float    GridX    = Dialog.X + RailW + 14.0f;
    const float    GridW    = std::max(96.0f, Dialog.Right() - 14.0f - GridX);
    const float    CellGap  = 10.0f;
    const uint32_t Columns  = std::max(1u, static_cast<uint32_t>((GridW + CellGap) / (108.0f + CellGap)));
    const float    TileSide = std::floor((GridW - static_cast<float>(Columns - 1u) * CellGap) / static_cast<float>(Columns));

    struct ConstructEntryView
    {
        std::string_view      Label;
        std::string_view      Caption;
        ConstructSlotCategory Category;
        int32_t               TemplateIndex;
    };
    std::vector<ConstructEntryView> Choices{};

    if (ConstructRail != ConstructCategoryRail::Organization)
    {
        if (AttachedOutliner != nullptr)
        {
            const auto Templates = AttachedOutliner->QueryConstructTemplates();
            for (std::size_t Idx = 0u; Idx < Templates.size(); ++Idx)
            {
                if (CaseInsensitiveMatch(Templates[Idx].DropdownLabel, std::string_view(ConstructSearchEditor.Text, ConstructSearchEditor.Length)))
                {
                    Choices.push_back({ Templates[Idx].DropdownLabel, Templates[Idx].CategoryTitle, Templates[Idx].Category, static_cast<int32_t>(Idx) });
                }
            }
        }
        else
        {
            Choices.push_back({ "Add Item 1", "Type 1", ConstructSlotCategory::CategoryOne, 0 });
            Choices.push_back({ "Add Item 2", "Type 2", ConstructSlotCategory::CategoryTwo, 1 });
            Choices.push_back({ "Add Item 3", "Type 3", ConstructSlotCategory::CategoryThree, 2 });
            Choices.push_back({ "Add Item 4", "Type 4", ConstructSlotCategory::CategoryFour, 3 });
        }
    }
    if (ConstructRail != ConstructCategoryRail::ConstructSet)
    {
        Choices.push_back({ "+ Add Folder", "Collection", ConstructSlotCategory::Folder, -1 });
    }

    for (std::size_t Idx = 0u; Idx < Choices.size(); ++Idx)
    {
        const uint32_t Col   = static_cast<uint32_t>(Idx) % Columns;
        const uint32_t Row   = static_cast<uint32_t>(Idx) / Columns;
        const float    CellX = GridX + static_cast<float>(Col) * (TileSide + CellGap);
        const float    CellY = Dialog.Y + 60.0f + static_cast<float>(Row) * (TileSide + CellGap);
        if (CellY + TileSide > Dialog.Bottom() - 8.0f)
        {
            break;
        }
        const Rectangle2D Cell{ CellX, CellY, TileSide, TileSide };
        const bool        Hovered = Cell.Contains(Cursor);

        Overlay.FillRectangle(Cell, Hovered ? ColorRgba{ 43, 43, 43, 255 } : ColorRgba{ 32, 32, 32, 255 }, 12.0f);
        Overlay.StrokeRectangle(Cell, Hovered ? WorkspaceSurfaceColors::SelectionAmber : ColorRgba{ 58, 58, 58, 255 }, 12.0f, 1.0f);

        const float IconSide = std::clamp(TileSide * 0.38f, 28.0f, 46.0f);
        WorkspaceAppearance::ConstructSlotCategoryIcon(
            Overlay,
            Choices[Idx].Category,
            { Cell.Center().X - IconSide * 0.5f, Cell.Y + TileSide * 0.16f },
            IconSide);

        const float LabelMaxW = TileSide - 14.0f;
        const float LabelW    = std::min(
            LabelMaxW,
            WorkspaceAppearance::MeasureWeightedTextWidth(Overlay, FontWeightClass::Regular, 11.5f, Choices[Idx].Label));
        WorkspaceAppearance::ConstructTruncatedText(
            Overlay,
            FontWeightClass::Regular,
            { Cell.Center().X - LabelW * 0.5f, Cell.Bottom() - 34.0f },
            LabelMaxW,
            11.5f,
            WorkspaceSurfaceColors::PrimaryInk,
            Choices[Idx].Label);

        const float CapW = std::min(
            LabelMaxW,
            WorkspaceAppearance::MeasureWeightedTextWidth(Overlay, FontWeightClass::Regular, 9.0f, Choices[Idx].Caption));
        WorkspaceAppearance::ConstructTruncatedText(
            Overlay,
            FontWeightClass::Regular,
            { Cell.Center().X - CapW * 0.5f, Cell.Bottom() - 18.0f },
            LabelMaxW,
            9.0f,
            WorkspaceSurfaceColors::MutedInk,
            Choices[Idx].Caption);

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            if (AttachedOutliner != nullptr)
            {
                if (Choices[Idx].TemplateIndex >= 0)
                {
                    AttachedOutliner->ConstructFromTemplate(static_cast<uint32_t>(Choices[Idx].TemplateIndex));
                }
                else
                {
                    AttachedOutliner->ConstructFolder("Folder");
                }
            }
            ConstructSheetOpen = false;
        }
    }

    Overlay.PopClip();
}

void ViewportPanel::RenderSettingsSheet(
    PixelSpace&             Overlay,
    Rectangle2D             Bounds,
    Point2D                 GearButtonRightBottom,
    const PaneInputReading& Input)
{
    const float SheetW = 286.0f;
    const float SheetH = 314.0f;
    const float SheetX = std::clamp(GearButtonRightBottom.X - SheetW, Bounds.X + 10.0f, std::max(Bounds.X + 10.0f, Bounds.Right() - SheetW - 10.0f));
    const float SheetY = GearButtonRightBottom.Y + 6.0f;
    const Rectangle2D Sheet{ SheetX, SheetY, SheetW, SheetH };

    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    Overlay.FillRectangle(Sheet, WorkspaceSurfaceColors::OverlaySheet, 16.0f);
    Overlay.StrokeRectangle(Sheet, ColorRgba{ 255, 255, 255, 30 }, 16.0f, 1.0f);

    WorkspaceAppearance::ConstructWeightedText(
        Overlay,
        FontWeightClass::Medium,
        { Sheet.X + 16.0f, Sheet.Y + 14.0f },
        9.5f,
        ColorRgba{ 138, 138, 138, 255 },
        "VIEWPORT SETTINGS");

    // Quality tier capsules (Low / Med / Std / High / Ultra)
    float RowY = Sheet.Y + 36.0f;
    {
        constexpr std::array<const char*, 5> ShortLabels = {{ "Low", "Med", "Std", "High", "Ultra" }};
        const float CapsuleW = (Sheet.Width - 32.0f - 16.0f) / 5.0f;
        for (uint32_t Tier = 0u; Tier < 5u; ++Tier)
        {
            const Rectangle2D Capsule{ Sheet.X + 16.0f + static_cast<float>(Tier) * (CapsuleW + 4.0f), RowY, CapsuleW, 24.0f };
            const bool        Active = (DisplaySettings.QualityTier == Tier);
            Overlay.FillRectangle(Capsule, Active ? ColorRgba{ 62, 62, 62, 255 } : ColorRgba{ 36, 36, 36, 255 }, 12.0f);
            Overlay.StrokeRectangle(Capsule, Active ? WorkspaceSurfaceColors::PreviewGoldInk : ColorRgba{ 255, 255, 255, 16 }, 12.0f, 1.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Overlay, FontWeightClass::Regular, 9.5f, ShortLabels[Tier]);
            WorkspaceAppearance::ConstructWeightedText(
                Overlay,
                FontWeightClass::Regular,
                { Capsule.Center().X - TW * 0.5f, Capsule.Y + 6.0f },
                9.5f,
                Active ? WorkspaceSurfaceColors::PrimaryInk : ColorRgba{ 162, 162, 162, 255 },
                ShortLabels[Tier]);
            if (Capsule.Contains(Cursor) && Input.PrimaryPointer.Pressed)
            {
                DisplaySettings.QualityTier = Tier;
            }
        }
        RowY += 36.0f;
    }

    // ExclusiveSelector rows for Global Illumination, Reflections, Anti-Aliasing, Raytracing
    auto ConstructToggleRow = [&](const char* Label, bool& ToggleRef)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Overlay,
            FontWeightClass::Regular,
            { Sheet.X + 16.0f, RowY + 5.0f },
            11.5f,
            WorkspaceSurfaceColors::SecondaryInk,
            Label);
        const Rectangle2D SwitchRect{ Sheet.Right() - 16.0f - 44.0f, RowY + 1.0f, 44.0f, 22.0f };
        if (ExclusiveSelector::ConstructCompact(Overlay, SwitchRect, ToggleRef, Input.PrimaryPointer, 1.0f).Clicked)
        {
            ToggleRef = !ToggleRef;
        }
        RowY += 34.0f;
    };

    ConstructToggleRow("Global Illumination", DisplaySettings.GlobalIllumination);
    ConstructToggleRow("Reflections",         DisplaySettings.ReflectionsEnabled);
    ConstructToggleRow("Anti-aliasing",       DisplaySettings.AntiAliasingEnabled);
    ConstructToggleRow("Raytracing",          DisplaySettings.RaytracingEnabled);

    // Render Scale slider (50% .. 200%)
    RowY += 4.0f;
    WorkspaceAppearance::ConstructWeightedText(Overlay, FontWeightClass::Regular, { Sheet.X + 16.0f, RowY + 4.0f }, 11.0f, WorkspaceSurfaceColors::SecondaryInk, "Render Scale");
    char ScaleLabel[24]{};
    std::snprintf(ScaleLabel, sizeof(ScaleLabel), "%.0f%%", DisplaySettings.RenderScalePercent);
    const float SW = WorkspaceAppearance::MeasureWeightedTextWidth(Overlay, FontWeightClass::Regular, 10.5f, ScaleLabel);
    WorkspaceAppearance::ConstructWeightedText(Overlay, FontWeightClass::Regular, { Sheet.Right() - 16.0f - SW, RowY + 4.0f }, 10.5f, WorkspaceSurfaceColors::PrimaryInk, ScaleLabel);

    const Rectangle2D ScaleTrack{ Sheet.X + 16.0f, RowY + 24.0f, Sheet.Width - 32.0f, 14.0f };
    const float       NormScale = std::clamp((DisplaySettings.RenderScalePercent - 50.0f) / 150.0f, 0.0f, 1.0f);
    Overlay.FillRectangle(ScaleTrack, ColorRgba{ 38, 38, 38, 255 }, 7.0f);
    Overlay.FillRectangle({ ScaleTrack.X, ScaleTrack.Y, std::max(10.0f, ScaleTrack.Width * NormScale), ScaleTrack.Height }, ColorRgba{ 82, 82, 82, 255 }, 7.0f);
    Overlay.FillCircle({ ScaleTrack.X + 7.0f + (ScaleTrack.Width - 14.0f) * NormScale, ScaleTrack.Center().Y }, 6.5f, WorkspaceSurfaceColors::PrimaryInk);
    if (ScaleTrack.Contains(Cursor) && Input.PrimaryPointer.Down)
    {
        const float NextT                  = std::clamp((Cursor.X - ScaleTrack.X) / ScaleTrack.Width, 0.0f, 1.0f);
        DisplaySettings.RenderScalePercent = std::round((50.0f + NextT * 150.0f) / 5.0f) * 5.0f;
    }
    RowY += 48.0f;

    // Debug View selector row (Reusable Studio Controls DropdownSelector style)
    const Rectangle2D DebugBtn{ Sheet.X + 16.0f, RowY - 2.0f, Sheet.Width - 32.0f, 30.0f };
    DebugButtonBounds      = DebugBtn;
    std::string DebugLabel = "Debug View: ";
    DebugLabel += QueryDebugChannelName(DisplaySettings.DebugChannelIndex);

    ControlPointer DebugPointer = Input.PrimaryPointer;
    if (DebugDropdownOpen)
    {
        DebugPointer.Enabled = DebugBtn.Contains(Cursor);
    }
    const ControlContact DebugHit = DropdownSelector::ConstructButton(Overlay, DebugBtn, DebugLabel.c_str(), DebugDropdownOpen, DebugPointer);
    if (DebugHit.Pressed)
    {
        DebugDropdownOpen = !DebugDropdownOpen;
    }

    if (DebugDropdownOpen)
    {
        constexpr std::array<const char*, 5> DebugOptions = {{
            "Debug View: Off",
            "Debug View: Depth",
            "Debug View: Albedo",
            "Debug View: Normal",
            "Debug View: Roughness",
        }};
        const Rectangle2D DropBounds = DropdownSelector::QuerySheetBounds(DebugBtn, static_cast<uint32_t>(DebugOptions.size()));
        const uint32_t    CurrentIdx = DisplaySettings.DebugChannelIndex % 5u;
        uint32_t          ChosenIdx  = CurrentIdx;
        if (DropdownSelector::ConstructOptionSheet(
                Overlay,
                DebugBtn,
                DebugOptions.data(),
                static_cast<uint32_t>(DebugOptions.size()),
                CurrentIdx,
                Input.PrimaryPointer,
                ChosenIdx).Clicked)
        {
            DisplaySettings.DebugChannelIndex = ChosenIdx;
            DebugDropdownOpen                 = false;
        }
        else if (Input.PrimaryPointer.Pressed && !DropBounds.Contains(Cursor) && !DebugBtn.Contains(Cursor))
        {
            DebugDropdownOpen = false;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                            TOP-LEVEL VIEWPORT PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt,
    PixelSpace*             OverlaySurface)
{
    if (Bounds.Width <= 4.0f || Bounds.Height <= 4.0f)
    {
        return;
    }

    PixelSpace&   ActiveOverlay     = (OverlaySurface != nullptr) ? *OverlaySurface : Surface;
    const Point2D Cursor            = { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    OverlayOpenBefore = ConstructSheetOpen || SettingsSheetOpen || ProjectionDropdownOpen;

    PushDiagnosticsSample(Δt);
    WorkspaceAppearance::ForwardInputToTextBuffer(ConstructSearchEditor, Input, Δt);
    if (Input.EscapePressed)
    {
        ConstructSheetOpen     = false;
        SettingsSheetOpen      = false;
        ProjectionDropdownOpen = false;
    }

    const uint32_t ConstructedCount = (AttachedOutliner != nullptr) ? AttachedOutliner->QueryTotalCount() : 0u;
    const uint32_t VisibleCount     = (AttachedOutliner != nullptr) ? AttachedOutliner->QueryVisibleCount() : 0u;

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, WorkspaceAppearance::QueryDockedPaneBackground());

    // 1. Viewport Toolbar (.viewport-toolbar: 51px heading row + 40px secondary toolbar row)
    const float ToolbarLeft  = Bounds.X + 12.0f;
    const float ToolbarRight = Bounds.Right() - 12.0f;
    const float HeadingY     = Bounds.Y;

    // 1a. Left Scene Identity (24px Showcase folder-scene icon + "Scene" / "0 constructed" + PREVIEW tag)
    WorkspaceAppearance::ConstructShowcaseFolderIcon(Surface, { ToolbarLeft, HeadingY + 13.5f }, 24.0f, WorkspaceSurfaceColors::ShowcaseSage, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { ToolbarLeft + 34.0f, HeadingY + 11.0f },
        13.0f,
        ColorRgba{ 226, 226, 226, 255 },
        SceneTitle);

    char ConstructedCaption[64]{};
    std::snprintf(ConstructedCaption, sizeof(ConstructedCaption), "%u constructed", ConstructedCount);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { ToolbarLeft + 34.0f, HeadingY + 28.0f },
        9.0f,
        WorkspaceSurfaceColors::FaintInk,
        ConstructedCaption);

    const float SceneNameW  = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 13.0f, SceneTitle);
    const float SubCaptionW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, ConstructedCaption);
    const float TagX        = ToolbarLeft + 34.0f + std::max(SceneNameW, SubCaptionW) + 12.0f;

    if (TagX + 58.0f < ToolbarRight - 210.0f)
    {
        const Rectangle2D PreviewTag{ TagX, HeadingY + 15.0f, 52.0f, 20.0f };
        Surface.FillRectangle(PreviewTag, ColorRgba{ 185, 151, 86, 22 }, 5.0f);
        Surface.StrokeRectangle(PreviewTag, ColorRgba{ 185, 151, 86, 52 }, 5.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { PreviewTag.X + 7.5f, PreviewTag.Y + 5.5f },
            8.0f,
            WorkspaceSurfaceColors::PreviewGoldInk,
            "PREVIEW");
    }

    // 1b. Right Execution Mode Capsule Strip (.viewport-modes: Edit / Simulate / Play-Pause)
    {
        const bool        ShowingPause = (ExecutionMode == ViewportExecutionMode::Play) && !PlayPaused;
        const char*       PlayLabel    = ShowingPause ? "Pause" : "Play";
        constexpr float   EditBtnW     = 58.0f;
        constexpr float   SimBtnW      = 72.0f;
        constexpr float   PlayBtnW     = 64.0f;
        const float       StripW       = 6.0f + EditBtnW + 2.0f + SimBtnW + 2.0f + PlayBtnW;
        const Rectangle2D ModeStrip{ ToolbarRight - StripW, HeadingY + 10.0f, StripW, 31.0f };

        Surface.FillRectangle(ModeStrip, ColorRgba{ 9, 9, 9, 255 }, 15.5f);
        Surface.StrokeRectangle(ModeStrip, ColorRgba{ 255, 255, 255, 18 }, 15.5f, 1.0f);

        auto ConstructModeCapsule = [&](Rectangle2D Capsule, const char* Label, bool Active, int IconSelector)
        {
            const bool Hovered = !OverlayOpenBefore && Capsule.Contains(Cursor);
            if (Active)
            {
                Surface.FillRectangle(Capsule, ColorRgba{ 44, 44, 44, 255 }, 12.5f);
                Surface.StrokeRectangle(Capsule, ColorRgba{ 255, 255, 255, 20 }, 12.5f, 1.0f);
            }
            else if (Hovered)
            {
                Surface.FillRectangle(Capsule, ColorRgba{ 30, 30, 30, 255 }, 12.5f);
            }

            const ColorRgba CapsuleBg = Active ? ColorRgba{ 44, 44, 44, 255 }
                                               : (Hovered ? ColorRgba{ 30, 30, 30, 255 } : WorkspaceSurfaceColors::ModeStripRecess);
            const ColorRgba Ink = Active ? ColorRgba{ 236, 236, 236, 255 }
                                         : (Hovered ? ColorRgba{ 176, 176, 176, 255 } : ColorRgba{ 122, 122, 122, 255 });
            const Point2D   IconPos{ Capsule.X + 9.0f, Capsule.Y + 6.5f };
            if (IconSelector == 0)
            {
                WorkspaceAppearance::ConstructEditIcon(Surface, IconPos, 12.0f, Ink);
            }
            else if (IconSelector == 1)
            {
                WorkspaceAppearance::ConstructSimulateIcon(Surface, IconPos, 12.0f, Ink, CapsuleBg);
            }
            else if (IconSelector == 2)
            {
                WorkspaceAppearance::ConstructPlayIcon(Surface, IconPos, 12.0f, Ink);
            }
            else
            {
                WorkspaceAppearance::ConstructPauseIcon(Surface, IconPos, 12.0f, WorkspaceSurfaceColors::SelectionAmber);
            }

            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { Capsule.X + 25.0f, Capsule.Y + 7.0f },
                10.0f,
                Ink,
                Label);

            return Hovered && Input.PrimaryPointer.Pressed;
        };

        const Rectangle2D EditCapsule{ ModeStrip.X + 3.0f, ModeStrip.Y + 3.0f, EditBtnW, 25.0f };
        const Rectangle2D SimCapsule{ EditCapsule.Right() + 2.0f, ModeStrip.Y + 3.0f, SimBtnW, 25.0f };
        const Rectangle2D PlayCapsule{ SimCapsule.Right() + 2.0f, ModeStrip.Y + 3.0f, PlayBtnW, 25.0f };

        if (ConstructModeCapsule(EditCapsule, "Edit", ExecutionMode == ViewportExecutionMode::Edit, 0))
        {
            ConfigureExecutionMode(ViewportExecutionMode::Edit, false);
        }
        if (ConstructModeCapsule(SimCapsule, "Simulate", ExecutionMode == ViewportExecutionMode::Simulate, 1))
        {
            ConfigureExecutionMode(ViewportExecutionMode::Simulate, false);
        }
        if (ConstructModeCapsule(PlayCapsule, PlayLabel, ExecutionMode == ViewportExecutionMode::Play, ShowingPause ? 3 : 2))
        {
            TogglePlayPause();
        }
    }

    // 1c. Secondary Toolbar Row (31px capsule controls below subtle separator)
    const float ToolbarSepY = HeadingY + 51.0f;
    Surface.StrokeLine({ ToolbarLeft, ToolbarSepY }, { ToolbarRight, ToolbarSepY }, ColorRgba{ 255, 255, 255, 14 }, 1.0f);
    const float ToolsRowY = ToolbarSepY + 9.0f;

    // Construct Ctrl+A cream capsule button
    const Rectangle2D ConstructBtn{ ToolbarLeft, ToolsRowY, 134.0f, 31.0f };
    {
        const bool Hovered = ConstructBtn.Contains(Cursor);
        Surface.FillRectangle(
            ConstructBtn,
            Hovered ? ColorRgba{ 252, 252, 250, 255 } : WorkspaceSurfaceColors::ConstructCream,
            15.5f);
        Surface.StrokeRectangle(ConstructBtn, ColorRgba{ 238, 238, 235, 255 }, 15.5f, 1.0f);

        WorkspaceAppearance::ConstructConstructIcon(
            Surface,
            { ConstructBtn.X + 12.0f, ConstructBtn.Y + 7.5f },
            16.0f,
            WorkspaceSurfaceColors::ConstructDarkInk);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { ConstructBtn.X + 34.0f, ConstructBtn.Y + 8.5f },
            11.0f,
            WorkspaceSurfaceColors::ConstructDarkInk,
            "Construct");

        Surface.StrokeLine(
            { ConstructBtn.X + 91.0f, ConstructBtn.Y + 8.0f },
            { ConstructBtn.X + 91.0f, ConstructBtn.Bottom() - 8.0f },
            ColorRgba{ 0, 0, 0, 36 },
            1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { ConstructBtn.X + 98.0f, ConstructBtn.Y + 10.0f },
            9.0f,
            ColorRgba{ 112, 112, 112, 255 },
            "Ctrl+A");

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            ConfigureConstructSheetOpen(!ConstructSheetOpen);
        }
    }

    // Focus reticle capsule icon button (39x31)
    const Rectangle2D FocusBtn{ ConstructBtn.Right() + 6.0f, ToolsRowY, 39.0f, 31.0f };
    {
        const bool Hovered = !OverlayOpenBefore && FocusBtn.Contains(Cursor);
        Surface.FillRectangle(FocusBtn, Hovered ? ColorRgba{ 43, 43, 43, 255 } : ColorRgba{ 26, 26, 26, 255 }, 15.5f);
        Surface.StrokeRectangle(FocusBtn, ColorRgba{ 255, 255, 255, 20 }, 15.5f, 1.0f);
        WorkspaceAppearance::ConstructFocusIcon(
            Surface,
            { FocusBtn.X + 11.5f, FocusBtn.Y + 7.5f },
            16.0f,
            Hovered ? ColorRgba{ 222, 222, 222, 255 } : ColorRgba{ 147, 147, 147, 255 });
        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            ViewportZoomScale = 1.0f;
            if (OnNavigationDirective)
            {
                OnNavigationDirective(ViewportNavigationDirective::FitView);
            }
        }
    }

    // Divider
    Surface.StrokeLine(
        { FocusBtn.Right() + 6.0f, ToolsRowY + 6.0f },
        { FocusBtn.Right() + 6.0f, ToolsRowY + 25.0f },
        ColorRgba{ 255, 255, 255, 20 },
        1.0f);

    // Projection selector dropdown (Reusable Studio Controls DropdownSelector style: Perspective / Orthographic / Top / Front / Right)
    const Rectangle2D ProjectionBtn{ FocusBtn.Right() + 13.0f, ToolsRowY, 158.0f, 31.0f };
    {
        const ViewportProjectionPreset ActivePreset =
            (SplitViewportEnabled && ActivePaneIndex == 1u) ? SecondaryProjection : PrimaryProjection;
        ControlPointer ProjPointer = Input.PrimaryPointer;
        if (ConstructSheetOpen || SettingsSheetOpen)
        {
            ProjPointer.Enabled = false;
        }
        const ControlContact ProjHit = DropdownSelector::ConstructButton(
            Surface,
            ProjectionBtn,
            WorkspaceAppearance::QueryProjectionPresetLabel(ActivePreset),
            ProjectionDropdownOpen,
            ProjPointer,
            1.0f,
            ControlIcon::CameraBody);
        if (ProjHit.Pressed)
        {
            ProjectionDropdownOpen = !ProjectionDropdownOpen;
            ConstructSheetOpen     = false;
            SettingsSheetOpen      = false;
            DebugDropdownOpen      = false;
        }
    }

    // Right-aligned secondary tools: Split Viewport, Diagnostics, Settings (each 39x31 capsule)
    const Rectangle2D SettingsBtn{ ToolbarRight - 39.0f, ToolsRowY, 39.0f, 31.0f };
    const Rectangle2D DiagnosticsBtn{ SettingsBtn.X - 5.0f - 39.0f, ToolsRowY, 39.0f, 31.0f };
    const Rectangle2D SplitBtn{ DiagnosticsBtn.X - 5.0f - 39.0f, ToolsRowY, 39.0f, 31.0f };

    auto ConstructSecondaryToolButton = [&](Rectangle2D Btn, bool ToggledOn, auto ConstructIconFn)
    {
        const bool      Hovered = Btn.Contains(Cursor);
        const ColorRgba BtnBg   = ToggledOn ? ColorRgba{ 46, 39, 30, 255 }
                                            : (Hovered ? ColorRgba{ 43, 43, 43, 255 } : ColorRgba{ 26, 26, 26, 255 });
        if (ToggledOn)
        {
            Surface.FillRectangle(Btn, BtnBg, 15.5f);
            Surface.StrokeRectangle(Btn, ColorRgba{ 191, 162, 122, 110 }, 15.5f, 1.0f);
        }
        else
        {
            Surface.FillRectangle(Btn, BtnBg, 15.5f);
            Surface.StrokeRectangle(Btn, ColorRgba{ 255, 255, 255, 20 }, 15.5f, 1.0f);
        }
        const ColorRgba Ink = ToggledOn ? ColorRgba{ 222, 193, 147, 255 }
                                        : (Hovered ? ColorRgba{ 222, 222, 222, 255 } : ColorRgba{ 147, 147, 147, 255 });
        ConstructIconFn(Point2D{ Btn.X + 11.5f, Btn.Y + 7.5f }, Ink, BtnBg);
        return Hovered && Input.PrimaryPointer.Pressed;
    };

    if (ConstructSecondaryToolButton(
            SplitBtn,
            SplitViewportEnabled,
            [&](Point2D Origin, ColorRgba Ink, ColorRgba BtnBg)
            { WorkspaceAppearance::ConstructSplitViewportIcon(Surface, Origin, 16.0f, Ink, BtnBg); }))
    {
        ConfigureSplitViewport(!SplitViewportEnabled);
    }

    if (ConstructSecondaryToolButton(
            DiagnosticsBtn,
            DiagnosticsVisible,
            [&](Point2D Origin, ColorRgba Ink, ColorRgba /*BtnBg*/)
            { WorkspaceAppearance::ConstructDiagnosticsIcon(Surface, Origin, 16.0f, Ink); }))
    {
        DiagnosticsVisible = !DiagnosticsVisible;
    }

    if (ConstructSecondaryToolButton(
            SettingsBtn,
            SettingsSheetOpen,
            [&](Point2D Origin, ColorRgba Ink, ColorRgba BtnBg)
            { WorkspaceAppearance::ConstructViewportSettingsIcon(Surface, Origin, 16.0f, Ink, BtnBg); }))
    {
        ConfigureSettingsSheetOpen(!SettingsSheetOpen);
        if (!SettingsSheetOpen)
        {
            DebugDropdownOpen = false;
        }
    }

    // 2. Viewport Canvas (.scene-image) above 46px footer
    constexpr float   FooterHeight = 46.0f;
    const float       CanvasTopY   = ToolsRowY + 42.0f;
    const Rectangle2D CanvasBounds{
        ToolbarLeft,
        CanvasTopY,
         std::max(20.0f, ToolbarRight - ToolbarLeft),
        std::max(20.0f, Bounds.Bottom() - FooterHeight - CanvasTopY)
    };

    if (!SplitViewportEnabled)
    {
        RenderCanvasPane(Surface, CanvasBounds, PrimaryProjection, 0u, ConstructedCount, Input);
    }
    else
    {
        const float       PaneGap = 2.0f;
        const float       HalfW   = (CanvasBounds.Width - PaneGap) * 0.5f;
        const Rectangle2D LeftPane{ CanvasBounds.X, CanvasBounds.Y, HalfW, CanvasBounds.Height };
        const Rectangle2D RightPane{ LeftPane.Right() + PaneGap, CanvasBounds.Y, HalfW, CanvasBounds.Height };

        Surface.FillRectangle(CanvasBounds, ColorRgba{ 38, 38, 38, 255 });
        RenderCanvasPane(Surface, LeftPane, PrimaryProjection, 0u, ConstructedCount, Input);
        RenderCanvasPane(Surface, RightPane, SecondaryProjection, 1u, ConstructedCount, Input);
    }

    if (DiagnosticsVisible)
    {
        RenderDiagnosticsPanel(Surface, CanvasBounds, ConstructedCount, VisibleCount, Input);
    }

    // 3. Viewport Footer (.viewport-footer: 46px height)
    const Rectangle2D FooterBounds{ ToolbarLeft, Bounds.Bottom() - FooterHeight, CanvasBounds.Width, FooterHeight };
    {
        float FootX = FooterBounds.X + 4.0f;
        const float FootY = FooterBounds.Y + 16.0f;

        const char* StatusWord = "READY";
        ColorRgba   StatusInk  = WorkspaceSurfaceColors::StatusGreen;
        if (ExecutionMode == ViewportExecutionMode::Simulate)
        {
            StatusWord = "SIMULATING";
            StatusInk  = WorkspaceSurfaceColors::PreviewGoldInk;
        }
        else if (ExecutionMode == ViewportExecutionMode::Play)
        {
            StatusWord = PlayPaused ? "PAUSED" : "PLAYING";
            StatusInk  = PlayPaused ? WorkspaceSurfaceColors::SelectionAmber : WorkspaceSurfaceColors::StatusGreen;
        }

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { FootX, FootY }, 10.0f, StatusInk, StatusWord);
        FootX += WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, StatusWord) + 8.0f;

        char TailSummary[160]{};
        std::snprintf(
            TailSummary,
            sizeof(TailSummary),
            "-  CONSTRUCTED %u  -  %u visible  -  %s%s",
            ConstructedCount,
            VisibleCount,
            QueryQualityTierName(DisplaySettings.QualityTier),
            SplitViewportEnabled ? "  -  SPLIT VIEW" : "");
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { FootX, FootY },
            std::max(20.0f, FooterBounds.Right() - FootX),
            10.0f,
            ColorRgba{ 118, 118, 118, 255 },
            TailSummary);
    }

    Surface.PopClip();

    // 4. Projection Dropdown (DropdownSelector::ConstructOptionSheet), Construct Dialog & Settings Sheet Overlays (zero shadow)
    if (ProjectionDropdownOpen)
    {
        constexpr std::array<const char*, 5> ProjectionOptions = {{
            "Perspective",
            "Orthographic",
            "Top",
            "Front",
            "Right",
        }};
        const ViewportProjectionPreset Current    =
            (SplitViewportEnabled && ActivePaneIndex == 1u) ? SecondaryProjection : PrimaryProjection;
        const uint32_t    CurrentIdx = static_cast<uint32_t>(Current);
        uint32_t          ChosenIdx  = CurrentIdx;
        const Rectangle2D DropSheet  = DropdownSelector::QuerySheetBounds(ProjectionBtn, static_cast<uint32_t>(ProjectionOptions.size()));

        if (DropdownSelector::ConstructOptionSheet(
                ActiveOverlay,
                ProjectionBtn,
                ProjectionOptions.data(),
                static_cast<uint32_t>(ProjectionOptions.size()),
                CurrentIdx,
                Input.PrimaryPointer,
                ChosenIdx).Clicked)
        {
            const auto SelectedPreset = static_cast<ViewportProjectionPreset>(ChosenIdx);
            if (SplitViewportEnabled && ActivePaneIndex == 1u)
            {
                SecondaryProjection = SelectedPreset;
            }
            else
            {
                PrimaryProjection = SelectedPreset;
            }
            ProjectionDropdownOpen = false;
        }
        else if (OverlayOpenBefore && Input.PrimaryPointer.Pressed && !DropSheet.Contains(Cursor) && !ProjectionBtn.Contains(Cursor))
        {
            ProjectionDropdownOpen = false;
        }
    }

    if (ConstructSheetOpen)
    {
        RenderConstructDialog(ActiveOverlay, Bounds, Input);
        if (OverlayOpenBefore && Input.PrimaryPointer.Pressed && !ConstructBtn.Contains(Cursor))
        {
            const float DialogW = std::min(532.0f, std::max(300.0f, Bounds.Width - 36.0f));
            const float DialogH = std::min(346.0f, std::max(240.0f, Bounds.Height - 70.0f));
            const Rectangle2D DialogRect{ Bounds.Center().X - DialogW * 0.5f, Bounds.Y + 94.0f, DialogW, DialogH };
            if (!DialogRect.Contains(Cursor))
            {
                ConstructSheetOpen = false;
            }
        }
    }

    if (SettingsSheetOpen)
    {
        RenderSettingsSheet(ActiveOverlay, Bounds, { SettingsBtn.Right(), SettingsBtn.Bottom() }, Input);
        if (OverlayOpenBefore && Input.PrimaryPointer.Pressed && !SettingsBtn.Contains(Cursor))
        {
            const float SheetW = 286.0f;
            const float SheetH = 314.0f;
            const float SheetX = std::clamp(SettingsBtn.Right() - SheetW, Bounds.X + 10.0f, std::max(Bounds.X + 10.0f, Bounds.Right() - SheetW - 10.0f));
            const Rectangle2D SheetRect{ SheetX, SettingsBtn.Bottom() + 6.0f, SheetW, SheetH };
            const Rectangle2D DebugSheetRect = DropdownSelector::QuerySheetBounds(DebugButtonBounds, 5u);
            if (!SheetRect.Contains(Cursor) && (!DebugDropdownOpen || !DebugSheetRect.Contains(Cursor)))
            {
                SettingsSheetOpen = false;
                DebugDropdownOpen = false;
            }
        }
    }
}

void ViewportPanel::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton("##FrontierViewportCanvas", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)));

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace WindowSurface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PixelSpace ForegroundSurface(ImGui::GetForegroundDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);

    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);
    Render(WindowSurface, LogicalBounds, Input, Δt, &ForegroundSurface);
    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, false);
}

} // namespace Frontier
