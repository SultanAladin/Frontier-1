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
#include "Engine/Components/Controls/ExecutionModeStrip.h"
#include "Engine/Components/Diagnostics/DiagnosticsOverlay.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
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

[[nodiscard]] const char* QueryRenderEngineName(uint32_t EngineIdx) noexcept
{
    constexpr std::array<const char*, 5> Names = {{ "MatCap", "OpenPBR Direct", "DDGI Raytracing", "SDF GI", "Path Tracer" }};
    return Names[std::min<std::size_t>(EngineIdx, Names.size() - 1u)];
}

[[nodiscard]] const char* QueryShadowAlgorithmName(uint32_t AlgIdx) noexcept
{
    constexpr std::array<const char*, 5> Names = {{ "Contact PCSS", "PCSS + Filter", "SMRT", "PCF 3x3", "BVH4 Ray" }};
    return Names[std::min<std::size_t>(AlgIdx, Names.size() - 1u)];
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
    CanvasRasterTexture.DestroyPixels();
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void ViewportPanel::AssignCanvasRasterImage(const uint8_t* RgbaPixels, uint32_t WidthPx, uint32_t HeightPx)
{
    if (RgbaPixels == nullptr || WidthPx == 0u || HeightPx == 0u)
    {
        return;
    }

    const bool NeedsCreate =
        CanvasRasterTexture.Width != static_cast<int>(WidthPx) ||
        CanvasRasterTexture.Height != static_cast<int>(HeightPx) ||
        CanvasRasterTexture.Pixels == nullptr;
    if (NeedsCreate)
    {
        CanvasRasterTexture.DestroyPixels();
        CanvasRasterTexture.SetStatus(ImTextureStatus_Destroyed);
        CanvasRasterTexture.Create(ImTextureFormat_RGBA32, static_cast<int>(WidthPx), static_cast<int>(HeightPx));
    }

    std::memcpy(
        CanvasRasterTexture.Pixels,
        RgbaPixels,
        static_cast<std::size_t>(WidthPx) * static_cast<std::size_t>(HeightPx) * 4u);

    if (!NeedsCreate && CanvasRasterTexture.Status == ImTextureStatus_OK)
    {
        CanvasRasterTexture.UpdateRect = {
            0u,
            0u,
            static_cast<unsigned short>(WidthPx),
            static_cast<unsigned short>(HeightPx),
        };
        CanvasRasterTexture.SetStatus(ImTextureStatus_WantUpdates);
    }
}

void ViewportPanel::ClearCanvasRasterImage() noexcept
{
    CanvasRasterTexture.DestroyPixels();
    CanvasRasterTexture.SetStatus(ImTextureStatus_Destroyed);
}

void ViewportPanel::ConfigureSceneTitle(std::string_view Title)
{
    SceneTitle.assign(Title.begin(), Title.end());
}

void ViewportPanel::ConfigureConstructPresets(std::vector<ViewportConstructPresetEntry> Presets)
{
    ConstructPresets = std::move(Presets);
}

void ViewportPanel::ConfigureConstructPresetCallback(std::function<void(uint32_t PresetIndex)> Callback)
{
    ConstructPresetCallback = std::move(Callback);
}

void ViewportPanel::ConfigureOpenContentBrowserCallback(std::function<void()> Callback)
{
    OpenContentBrowserCallback = std::move(Callback);
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
        ForceOverlaySheetOpen  = false;
    }
}

void ViewportPanel::ConfigureConstructSheetOpen(bool Open) noexcept
{
    ConstructSheetOpen = Open;
    if (Open)
    {
        SettingsSheetOpen      = false;
        ProjectionDropdownOpen = false;
        ForceOverlaySheetOpen  = false;
    }
}

void ViewportPanel::ConfigureRasterCallback(ViewportPaneRasterCallback Callback)
{
    RasterCallback = std::move(Callback);
}

void ViewportPanel::ConfigureInteractionCallback(ViewportPaneInteractionCallback Callback)
{
    InteractionCallback = std::move(Callback);
}

void ViewportPanel::ConfigureForceOverlayToolsVisible(bool Visible) noexcept
{
    ForceOverlayToolsVisible = Visible;
    if (!Visible)
    {
        ForceOverlaySheetOpen = false;
    }
}

void ViewportPanel::ConfigureForceOverlaySheetOpen(bool Open) noexcept
{
    ForceOverlaySheetOpen = Open;
    if (Open)
    {
        ConstructSheetOpen     = false;
        SettingsSheetOpen      = false;
        ProjectionDropdownOpen = false;
    }
}

void ViewportPanel::ConfigureForceOverlaySettings(const SimulationSpace::ForceFields::ForceOverlaySettings& Settings) noexcept
{
    ForceOverlay = Settings;
}

void ViewportPanel::ConfigureSimulationTimeSeconds(double TimeSeconds) noexcept
{
    SimulationTimeSeconds = std::max(0.0, TimeSeconds);
}

void ViewportPanel::ConfigureProbeReadoutText(std::string_view ReadoutText)
{
    ProbeReadoutText.assign(ReadoutText.begin(), ReadoutText.end());
}

bool ViewportPanel::QueryForceOverlayToolsVisible() const noexcept
{
    return ForceOverlayToolsVisible;
}

bool ViewportPanel::QueryForceOverlaySheetOpen() const noexcept
{
    return ForceOverlaySheetOpen;
}

const SimulationSpace::ForceFields::ForceOverlaySettings& ViewportPanel::QueryForceOverlaySettings() const noexcept
{
    return ForceOverlay;
}

SimulationSpace::ForceFields::ForceOverlaySettings& ViewportPanel::AccessForceOverlaySettings() noexcept
{
    return ForceOverlay;
}

double ViewportPanel::QuerySimulationTimeSeconds() const noexcept
{
    return SimulationTimeSeconds;
}

bool ViewportPanel::ConsumeRewindRequested() noexcept
{
    const bool Pending = RewindRequested;
    RewindRequested    = false;
    return Pending;
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
    const PaneInputReading&  Input,
    bool                     OverlayCoversCanvas)
{
    if (PaneBounds.Width <= 4.0f || PaneBounds.Height <= 4.0f)
    {
        return;
    }

    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    Surface.PushClip(PaneBounds);

    bool RenderedLiveSurface = false;
    if (RasterCallback)
    {
        ViewportPaneSurfaceSpecification Spec{};
        Spec.PaneIndex     = PaneIndex;
        Spec.WidthPx       = static_cast<uint32_t>(std::max(1.0f, std::round(PaneBounds.Width)));
        Spec.HeightPx      = static_cast<uint32_t>(std::max(1.0f, std::round(PaneBounds.Height)));
        Spec.Projection    = Projection;
        Spec.ExecutionMode = ExecutionMode;
        Spec.PlayPaused    = PlayPaused;
        Spec.Display       = DisplaySettings;
        Spec.ForceOverlay  = ForceOverlay;
        Spec.TimeSeconds   = SimulationTimeSeconds;

        const ViewportRasterSurface LiveSurface = RasterCallback(Spec);
        LastReportedForceCount                  = LiveSurface.ActiveForceCount;
        if (LiveSurface.SoftwareTextureData != nullptr || LiveSurface.NativeTextureHandle != 0ull)
        {
            Surface.FillTexturedRectangle(
                PaneBounds,
                LiveSurface.SoftwareTextureData,
                LiveSurface.NativeTextureHandle,
                ColorRgba{ 255u, 255u, 255u, 255u });
            RenderedLiveSurface = true;
        }
    }

    if (!RenderedLiveSurface && CanvasRasterTexture.Pixels != nullptr && CanvasRasterTexture.Width > 0 && CanvasRasterTexture.Height > 0)
    {
        Surface.FillRectangle(PaneBounds, ColorRgba{ 14, 15, 18, 255 });
        const float SourceAspect = static_cast<float>(CanvasRasterTexture.Width) / static_cast<float>(CanvasRasterTexture.Height);
        const float TargetAspect = PaneBounds.Width / std::max(1.0f, PaneBounds.Height);
        Point2D     UvMin{ 0.0f, 0.0f };
        Point2D     UvMax{ 1.0f, 1.0f };
        if (TargetAspect > SourceAspect)
        {
            const float CropH = SourceAspect / TargetAspect;
            UvMin.Y           = 0.5f * (1.0f - CropH);
            UvMax.Y           = 1.0f - UvMin.Y;
        }
        else if (SourceAspect > TargetAspect)
        {
            const float CropW = TargetAspect / SourceAspect;
            UvMin.X           = 0.5f * (1.0f - CropW);
            UvMax.X           = 1.0f - UvMin.X;
        }
        Surface.FillTexturedRectangle(PaneBounds, &CanvasRasterTexture, UvMin, UvMax);
        RenderedLiveSurface = true;
    }

    if (!RenderedLiveSurface)
    {
        // Subtle dark checker canvas matching reference layout (#161616 / #1B1B1B)
        Surface.FillRectangle(PaneBounds, ColorRgba{ 22, 22, 22, 255 });
        constexpr float CellSize = 24.0f;
        const int       Cols     = static_cast<int>(std::ceil(PaneBounds.Width / CellSize)) + 1;
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
    }

    bool ProjectionChipConsumed = false;
    if (SplitViewportEnabled || RenderedLiveSurface)
    {
        // Active pane perimeter & top-left projection chip
        if (SplitViewportEnabled && ActivePaneIndex == PaneIndex)
        {
            Surface.StrokeRectangle(PaneBounds.Inset(0.5f), ColorRgba{ 191, 162, 122, 115 }, 0.0f, 1.0f);
        }

        const char*       ProjName = WorkspaceAppearance::QueryProjectionPresetLabel(Projection);
        const float       ChipW    = std::max(122.0f, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, ProjName) + 52.0f);
        const Rectangle2D ProjChip{ PaneBounds.X + 10.0f, PaneBounds.Y + 10.0f, ChipW, 28.0f };

        const ControlContact ChipHit = DropdownSelector::ConstructButton(Surface, ProjChip, ProjName, false, Input.PrimaryPointer);
        if (ChipHit.Pressed)
        {
            ProjectionChipConsumed = true;
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
        else if (!OverlayCoversCanvas && PaneBounds.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            ActivePaneIndex = PaneIndex;
        }
    }

    if (ForceOverlayToolsVisible && RenderedLiveSurface)
    {
        const char* ModeBadge = ForceOverlay.Enabled ? "LIT + FORCE VIZ" : "LIT";
        const float BadgeW    = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, ModeBadge) + 20.0f;
        const Rectangle2D BadgeRect{ PaneBounds.Right() - BadgeW - 10.0f, PaneBounds.Y + 10.0f, BadgeW, 24.0f };
        Surface.FillRectangle(
            BadgeRect,
            ForceOverlay.Enabled ? ColorRgba{ 20, 34, 42, 224 } : ColorRgba{ 24, 24, 26, 208 },
            6.0f);
        Surface.StrokeRectangle(
            BadgeRect,
            ForceOverlay.Enabled ? ColorRgba{ 104, 220, 255, 130 } : ColorRgba{ 255, 255, 255, 32 },
            6.0f,
            1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { BadgeRect.X + 10.0f, BadgeRect.Y + 6.0f },
            9.5f,
            ForceOverlay.Enabled ? ColorRgba{ 104, 220, 255, 255 } : ColorRgba{ 192, 192, 198, 255 },
            ModeBadge);

        if (PaneIndex == 0u && !ProbeReadoutText.empty() && ForceOverlay.CrossSectionAnemometerActive)
        {
            const float ReadW = std::min(
                PaneBounds.Width - 110.0f,
                WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, ProbeReadoutText) + 22.0f);
            const Rectangle2D ProbePill{ PaneBounds.X + 12.0f, PaneBounds.Bottom() - 48.0f, ReadW, 22.0f };
            Surface.FillRectangle(ProbePill, ColorRgba{ 18, 22, 28, 218 }, 6.0f);
            Surface.StrokeRectangle(ProbePill, ColorRgba{ 104, 220, 255, 90 }, 6.0f, 1.0f);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { ProbePill.X + 10.0f, ProbePill.Y + 5.0f },
                std::max(12.0f, ProbePill.Width - 20.0f),
                10.0f,
                ColorRgba{ 210, 232, 244, 255 },
                ProbeReadoutText);
        }
    }

    if (InteractionCallback && !OverlayCoversCanvas && !ProjectionChipConsumed && PaneBounds.Contains(Cursor))
    {
        InteractionCallback(PaneIndex, PaneBounds, Input);
    }

    if (!RenderedLiveSurface)
    {
        // Center quiet empty-scene prompt when 0 constructed (exact reference layout)
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
    }
    else if (PaneIndex == 0u)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { PaneBounds.X + 12.0f, PaneBounds.Bottom() - 20.0f },
            8.5f,
            ColorRgba{ 138, 148, 160, 210 },
            "VULKAN 1.3 (+Z UP)  -  GPU ANALYTICAL RASTER");
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
    std::vector<float> Sorted(IntervalHistoryMs.begin(), IntervalHistoryMs.end());
    std::sort(Sorted.begin(), Sorted.end());
    const float MinMs = Sorted.front();
    const float MaxMs = Sorted.back();
    const float AvgMs = std::accumulate(Sorted.begin(), Sorted.end(), 0.0f) / static_cast<float>(Sorted.size());
    const float P95Ms = Sorted[static_cast<std::size_t>(Sorted.size() * 95u / 100u)];
    const float CurMs = IntervalHistoryMs[(HistoryWriteCursor + IntervalHistoryMs.size() - 1u) % IntervalHistoryMs.size()];

    auto FormatMs = [](float V) -> std::string
    {
        char Buf[24]{};
        std::snprintf(Buf, sizeof(Buf), "%.2f ms", V);
        return std::string(Buf);
    };

    char WholeMs[16]{};
    char FracMs[16]{};
    const int WholeInt = static_cast<int>(CurMs);
    const int Tenths   = std::clamp(static_cast<int>(std::round((CurMs - static_cast<float>(WholeInt)) * 10.0f)), 0, 9);
    std::snprintf(WholeMs, sizeof(WholeMs), "%d", WholeInt);
    std::snprintf(FracMs, sizeof(FracMs), ".%d", Tenths);

    std::vector<float> OrderedSamples(60u);
    for (std::size_t Step = 0u; Step < OrderedSamples.size(); ++Step)
    {
        OrderedSamples[Step] = IntervalHistoryMs[(HistoryWriteCursor + Step * 2u) % IntervalHistoryMs.size()];
    }

    char BigCnt[24]{};
    std::snprintf(BigCnt, sizeof(BigCnt), "%u", ConstructedCount);
    char VisSummary[64]{};
    std::snprintf(VisSummary, sizeof(VisSummary), "Visible: %u   Hidden: %u", VisibleCount, ConstructedCount - VisibleCount);

    char ScaleText[48]{};
    std::snprintf(ScaleText, sizeof(ScaleText), "%.0f", DisplaySettings.RenderScalePercent);

    const std::array<DiagnosticsMetricChannel, 3> Channels = {{
        DiagnosticsMetricChannel{
            "Frame time",
            "Timing",
            WholeMs,
            FracMs,
            "ms  (60.0 fps)",
            "",
            "",
            OrderedSamples,
            std::min(12.0f, MinMs - 1.0f),
            std::max(22.0f, MaxMs + 1.0f),
            16.67f,
            false,
            ColorRgba{ 142, 198, 152, 240 },
            {
                { "Average",  FormatMs(AvgMs) },
                { "Minimum",  FormatMs(MinMs) },
                { "95th pct", FormatMs(P95Ms) },
                { "Maximum",  FormatMs(MaxMs) },
            }
        },
        DiagnosticsMetricChannel{
            "Scene inventory",
            "Scene",
            BigCnt,
            "",
            "slots",
            VisSummary,
            "",
            {},
            0.0f,
            1.0f,
            0.0f,
            false,
            ColorRgba{ 142, 198, 152, 240 },
            {}
        },
        DiagnosticsMetricChannel{
            "Render configuration",
            "Render",
            ScaleText,
            "",
            "%",
            QueryQualityTierName(DisplaySettings.QualityTier),
            "",
            {},
            0.0f,
            1.0f,
            0.0f,
            false,
            ColorRgba{ 142, 198, 152, 240 },
            {}
        },
    }};

    DiagnosticsSession.Visible            = DiagnosticsVisible;
    DiagnosticsSession.Paused             = DiagnosticsPaused;
    DiagnosticsSession.ActiveChannelIndex = static_cast<uint32_t>(DiagnosticsCategory);

    DiagnosticsOverlaySpec Spec {};
    Spec.Title                   = "Diagnostics";
    Spec.ShortcutBadge           = "F3";
    Spec.FooterSampleLabel       = "120 samples";
    Spec.AllowDragResize         = false;
    Spec.ShowViewSpanControls    = false;
    Spec.DefaultWidthPx          = 324.0f;
    Spec.DefaultHeightPx         = 432.0f;
    Spec.Channels                = Channels;

    ControlPointer Pointer {};
    Pointer.X        = Input.PrimaryPointer.X;
    Pointer.Y        = Input.PrimaryPointer.Y;
    Pointer.Down     = Input.PrimaryPointer.Down;
    Pointer.Pressed  = Input.PrimaryPointer.Pressed;
    Pointer.Released = Input.PrimaryPointer.Released;

    const DiagnosticsOverlayResult OverlayOut = DiagnosticsOverlay::Construct(Surface, CanvasBounds, Spec, DiagnosticsSession, Pointer);
    DiagnosticsVisible  = DiagnosticsSession.Visible;
    DiagnosticsPaused   = DiagnosticsSession.Paused;
    DiagnosticsCategory = static_cast<DiagnosticsTabCategory>(OverlayOut.SelectedChannel);
}

bool ViewportPanel::QueryForceConstructAvailable() const noexcept
{
    if (!ConstructPresets.empty())
    {
        return true;
    }
    if (AttachedOutliner != nullptr)
    {
        for (const auto& Entry : AttachedOutliner->QueryConstructTemplates())
        {
            if (Entry.Category == ConstructSlotCategory::ForceField)
            {
                return true;
            }
        }
    }
    return false;
}

Rectangle2D ViewportPanel::QueryConstructDialogBounds(Rectangle2D Bounds) const noexcept
{
    if (QueryForceConstructAvailable())
    {
        const float DialogW = std::min(680.0f, std::max(360.0f, Bounds.Width - 28.0f));
        const float DialogH = std::min(510.0f, std::max(280.0f, Bounds.Height - 56.0f));
        return { Bounds.Center().X - DialogW * 0.5f, Bounds.Y + 94.0f, DialogW, DialogH };
    }
    const float DialogW = std::min(532.0f, std::max(300.0f, Bounds.Width - 36.0f));
    const float DialogH = std::min(346.0f, std::max(240.0f, Bounds.Height - 70.0f));
    return { Bounds.Center().X - DialogW * 0.5f, Bounds.Y + 94.0f, DialogW, DialogH };
}

void ViewportPanel::RenderConstructDialog(
    PixelSpace&             Overlay,
    Rectangle2D             Bounds,
    const PaneInputReading& Input)
{
    const bool        ForceMode = QueryForceConstructAvailable();
    const Rectangle2D Dialog    = QueryConstructDialogBounds(Bounds);

    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    Overlay.FillRectangle(Dialog, ColorRgba{ 23, 23, 23, 254 }, 14.0f);
    Overlay.StrokeRectangle(Dialog, ColorRgba{ 58, 58, 58, 255 }, 14.0f, 1.0f);

    Overlay.PushClip(Dialog);

    // Header
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

    if (ForceMode && OpenContentBrowserCallback)
    {
        const Rectangle2D BrowserBtn{ CloseBtn.X - 134.0f, Dialog.Y + 12.0f, 124.0f, 26.0f };
        const bool        BrowserHov = BrowserBtn.Contains(Cursor);
        Overlay.FillRectangle(BrowserBtn, BrowserHov ? ColorRgba{ 46, 42, 34, 255 } : ColorRgba{ 32, 32, 34, 255 }, 13.0f);
        Overlay.StrokeRectangle(BrowserBtn, BrowserHov ? WorkspaceSurfaceColors::SelectionAmber : ColorRgba{ 62, 62, 66, 255 }, 13.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Overlay,
            FontWeightClass::Medium,
            { BrowserBtn.X + 14.0f, BrowserBtn.Y + 6.5f },
            10.5f,
            BrowserHov ? WorkspaceSurfaceColors::SelectionAmber : ColorRgba{ 214, 214, 218, 255 },
            "Content Browser");
        if (BrowserHov && Input.PrimaryPointer.Pressed)
        {
            ConstructSheetOpen = false;
            OpenContentBrowserCallback();
        }
    }

    Overlay.StrokeLine({ Dialog.X, Dialog.Y + 48.0f }, { Dialog.Right(), Dialog.Y + 48.0f }, WorkspaceSurfaceColors::HairlineLight, 1.0f);

    if (!ForceMode)
    {
        // Generic minimal layout preserved 1:1 for WorkspacePanesValidation
        const float RailW = 128.0f;
        Overlay.StrokeLine({ Dialog.X + RailW, Dialog.Y + 48.0f }, { Dialog.X + RailW, Dialog.Bottom() }, WorkspaceSurfaceColors::HairlineLight, 1.0f);

        const std::array<std::pair<const char*, ConstructCategoryRail>, 3> Rails = {{
            { "All",          ConstructCategoryRail::All          },
            { "Items",        ConstructCategoryRail::ScenePrimitives },
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
                Choices.push_back({ "Add Item 1", "Type 1", ConstructSlotCategory::GeometryVolume, 0 });
                Choices.push_back({ "Add Item 2", "Type 2", ConstructSlotCategory::LayerGroup, 1 });
                Choices.push_back({ "Add Item 3", "Type 3", ConstructSlotCategory::PointLuminaire, 2 });
                Choices.push_back({ "Add Item 4", "Type 4", ConstructSlotCategory::OpticsCamera, 3 });
            }
        }
        if (ConstructRail != ConstructCategoryRail::ScenePrimitives)
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
        return;
    }

    // Expanded Force Construct Dialog: Search Strip + Domain/Preset Rail + Scrollable Bespoke Icon Grid
    const Rectangle2D SearchBox{ Dialog.X + 14.0f, Dialog.Y + 56.0f, Dialog.Width - 28.0f, 32.0f };
    Overlay.FillRectangle(SearchBox, ColorRgba{ 16, 16, 18, 255 }, 10.0f);
    Overlay.StrokeRectangle(SearchBox, ConstructSearchEditor.Active ? WorkspaceSurfaceColors::SelectionAmber : ColorRgba{ 48, 48, 52, 255 }, 10.0f, 1.0f);
    VectorIconRepository::ConstructControl(Overlay, ControlIcon::SearchGlass, VectorIconPlacement{ SearchBox.X + 10.0f, SearchBox.Y + 9.0f, 14.0f, ColorRgba{ 138, 138, 144, 255 }, 1.6f });
    const std::string_view SearchText(ConstructSearchEditor.Text, ConstructSearchEditor.Length);
    WorkspaceAppearance::ConstructTruncatedText(
        Overlay,
        FontWeightClass::Regular,
        { SearchBox.X + 32.0f, SearchBox.Y + 9.0f },
        SearchBox.Width - 44.0f,
        10.5f,
        SearchText.empty() ? ColorRgba{ 118, 118, 124, 255 } : WorkspaceSurfaceColors::PrimaryInk,
        SearchText.empty() ? "Type to filter 19 ForceModel laws, presets & entries..." : SearchText);
    if (SearchBox.Contains(Cursor) && Input.PrimaryPointer.Pressed)
    {
        ConstructSearchEditor.Active = true;
    }

    const float BodyTopY = SearchBox.Bottom() + 8.0f;
    Overlay.StrokeLine({ Dialog.X, BodyTopY }, { Dialog.Right(), BodyTopY }, WorkspaceSurfaceColors::HairlineLight, 1.0f);

    const float RailW = 138.0f;
    Overlay.StrokeLine({ Dialog.X + RailW, BodyTopY }, { Dialog.X + RailW, Dialog.Bottom() }, WorkspaceSurfaceColors::HairlineLight, 1.0f);

    const std::array<std::pair<const char*, ForceConstructCategory>, 6> ForceRails = {{
        { "All",             ForceConstructCategory::All             },
        { "VectorField",     ForceConstructCategory::VectorFieldModels      },
        { "StreamTransport",       ForceConstructCategory::StreamTransportModels        },
        { "Turbulence",      ForceConstructCategory::TurbulenceModels  },
        { "Force Presets",   ForceConstructCategory::Presets         },
        { "Scene Folders", ForceConstructCategory::SceneFolders },
    }};
    for (std::size_t Idx = 0u; Idx < ForceRails.size(); ++Idx)
    {
        const Rectangle2D RailRow{ Dialog.X + 10.0f, BodyTopY + 10.0f + static_cast<float>(Idx) * 32.0f, RailW - 20.0f, 28.0f };
        const bool        Active = (ForceConstructTab == ForceRails[Idx].second);
        if (Active || RailRow.Contains(Cursor))
        {
            Overlay.FillRectangle(RailRow, Active ? ColorRgba{ 44, 44, 46, 255 } : ColorRgba{ 33, 33, 35, 255 }, 8.0f);
        }
        WorkspaceAppearance::ConstructWeightedText(
            Overlay,
            FontWeightClass::Regular,
            { RailRow.X + 10.0f, RailRow.Y + 7.5f },
            10.5f,
            Active ? WorkspaceSurfaceColors::PrimaryInk : ColorRgba{ 164, 164, 170, 255 },
            ForceRails[Idx].first);
        if (RailRow.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            ForceConstructTab    = ForceRails[Idx].second;
            ConstructGridScrollY = 0.0f;
        }
    }

    auto DomainForForceModel = [](uint32_t Ord) noexcept -> uint32_t
    {
        if (Ord >= static_cast<uint32_t>(SimulationSpace::ForceFields::ForceModel::Count))
        {
            return 0u;
        }
        return static_cast<uint32_t>(
            SimulationSpace::ForceFields::QueryDomainForModel(
                static_cast<SimulationSpace::ForceFields::ForceModel>(Ord)));
    };

    struct ForceChoiceEntry
    {
        std::string_view      Label;
        std::string_view      Caption;
        ConstructSlotCategory Category;
        uint32_t              ForceModelOrdinal;
        bool                  FormulaOverride;
        int32_t               TemplateIndex;
        int32_t               PresetIndex;
    };
    std::vector<ForceChoiceEntry> Choices{};

    // Curated presets in All, Presets, or matching Domain rails
    const bool ShowPresets = (ForceConstructTab == ForceConstructCategory::All) ||
                             (ForceConstructTab == ForceConstructCategory::Presets) ||
                             (ForceConstructTab == ForceConstructCategory::VectorFieldModels) ||
                             (ForceConstructTab == ForceConstructCategory::StreamTransportModels) ||
                             (ForceConstructTab == ForceConstructCategory::TurbulenceModels);
    if (ShowPresets)
    {
        for (std::size_t P = 0u; P < ConstructPresets.size(); ++P)
        {
            const uint32_t PD = ConstructPresets[P].DomainOrdinal;
            if ((ForceConstructTab == ForceConstructCategory::VectorFieldModels && PD != 0u) ||
                (ForceConstructTab == ForceConstructCategory::StreamTransportModels && PD != 1u) ||
                (ForceConstructTab == ForceConstructCategory::TurbulenceModels && PD != 2u))
            {
                continue;
            }
            if (CaseInsensitiveMatch(ConstructPresets[P].Title, SearchText) ||
                CaseInsensitiveMatch(ConstructPresets[P].Subtitle, SearchText))
            {
                Choices.push_back({
                    ConstructPresets[P].Title,
                    ConstructPresets[P].Subtitle,
                    ConstructSlotCategory::ForceField,
                    ConstructPresets[P].ForceModelOrdinal,
                    ConstructPresets[P].FormulaOverride,
                    -1,
                    static_cast<int32_t>(P)
                });
            }
        }
    }

    // Outliner templates (19 ForceModel laws + Mesh/Light)
    if (AttachedOutliner != nullptr && ForceConstructTab != ForceConstructCategory::Presets)
    {
        const auto Templates = AttachedOutliner->QueryConstructTemplates();
        for (std::size_t T = 0u; T < Templates.size(); ++T)
        {
            const bool LawMatch = (Templates[T].Category == ConstructSlotCategory::ForceField);
            if (!CaseInsensitiveMatch(Templates[T].DropdownLabel, SearchText) &&
                !CaseInsensitiveMatch(Templates[T].DefaultTitle, SearchText) &&
                !CaseInsensitiveMatch(Templates[T].CategoryTitle, SearchText))
            {
                continue;
            }
            if (ForceConstructTab == ForceConstructCategory::VectorFieldModels && (!LawMatch || DomainForForceModel(Templates[T].ForceModelOrdinal) != 0u))
            {
                continue;
            }
            if (ForceConstructTab == ForceConstructCategory::StreamTransportModels && (!LawMatch || DomainForForceModel(Templates[T].ForceModelOrdinal) != 1u))
            {
                continue;
            }
            if (ForceConstructTab == ForceConstructCategory::TurbulenceModels && (!LawMatch || DomainForForceModel(Templates[T].ForceModelOrdinal) != 2u))
            {
                continue;
            }
            if (ForceConstructTab == ForceConstructCategory::SceneFolders && LawMatch)
            {
                continue;
            }
            Choices.push_back({
                Templates[T].DefaultTitle.empty() ? std::string_view(Templates[T].DropdownLabel) : std::string_view(Templates[T].DefaultTitle),
                Templates[T].CategoryTitle,
                Templates[T].Category,
                Templates[T].ForceModelOrdinal,
                false,
                static_cast<int32_t>(T),
                -1
            });
        }
    }

    if (ForceConstructTab == ForceConstructCategory::All || ForceConstructTab == ForceConstructCategory::SceneFolders)
    {
        if (CaseInsensitiveMatch("Folder", SearchText))
        {
            Choices.push_back({ "+ Add Folder", "Collection", ConstructSlotCategory::Folder, 0xFFFFFFFFu, false, -1, -1 });
        }
    }

    const Rectangle2D GridArea{ Dialog.X + RailW + 12.0f, BodyTopY + 10.0f, std::max(96.0f, Dialog.Right() - 18.0f - (Dialog.X + RailW + 12.0f)), Dialog.Bottom() - 10.0f - (BodyTopY + 10.0f) };
    const float    CellGap  = 9.0f;
    const uint32_t Columns  = std::max(1u, static_cast<uint32_t>((GridArea.Width + CellGap) / (112.0f + CellGap)));
    const float    TileSide = std::floor((GridArea.Width - static_cast<float>(Columns - 1u) * CellGap) / static_cast<float>(Columns));
    const uint32_t TotalRows = (static_cast<uint32_t>(Choices.size()) + Columns - 1u) / Columns;
    const float    ContentH  = static_cast<float>(TotalRows) * (TileSide + CellGap);
    const float    MaxScroll = std::max(0.0f, ContentH - GridArea.Height + 6.0f);
    if (GridArea.Contains(Cursor) && std::fabs(Input.WheelDeltaY) > 0.001f)
    {
        ConstructGridScrollY -= Input.WheelDeltaY * 36.0f;
    }
    ConstructGridScrollY = std::clamp(ConstructGridScrollY, 0.0f, MaxScroll);

    Overlay.PushClip(GridArea);
    for (std::size_t Idx = 0u; Idx < Choices.size(); ++Idx)
    {
        const uint32_t Col   = static_cast<uint32_t>(Idx) % Columns;
        const uint32_t Row   = static_cast<uint32_t>(Idx) / Columns;
        const float    CellX = GridArea.X + static_cast<float>(Col) * (TileSide + CellGap);
        const float    CellY = GridArea.Y + static_cast<float>(Row) * (TileSide + CellGap) - ConstructGridScrollY;
        if (CellY + TileSide < GridArea.Y || CellY > GridArea.Bottom())
        {
            continue;
        }
        const Rectangle2D Cell{ CellX, CellY, TileSide, TileSide };
        const bool        Hovered = GridArea.Contains(Cursor) && Cell.Contains(Cursor);

        Overlay.FillRectangle(Cell, Hovered ? ColorRgba{ 44, 44, 48, 255 } : ColorRgba{ 30, 30, 33, 255 }, 12.0f);
        Overlay.StrokeRectangle(
            Cell,
            Hovered ? WorkspaceSurfaceColors::SelectionAmber
                    : (Choices[Idx].PresetIndex >= 0 ? ColorRgba{ 88, 72, 48, 255 } : ColorRgba{ 54, 54, 58, 255 }),
            12.0f,
            1.0f);

        const float IconSide = std::clamp(TileSide * 0.36f, 28.0f, 40.0f);
        const Point2D IconOrigin{ Cell.Center().X - IconSide * 0.5f, Cell.Y + TileSide * 0.16f };
        if (Choices[Idx].Category == ConstructSlotCategory::ForceField && Choices[Idx].ForceModelOrdinal < 19u)
        {
            const ColorRgba Tint = Choices[Idx].FormulaOverride
                ? WorkspaceSurfaceColors::SelectionAmber
                : WorkspaceAppearance::QueryForceModelTint(Choices[Idx].ForceModelOrdinal);
            WorkspaceAppearance::ConstructForceModelIcon(Overlay, Choices[Idx].ForceModelOrdinal, IconOrigin, IconSide, Tint, 1.0f);
        }
        else
        {
            WorkspaceAppearance::ConstructSlotCategoryIcon(Overlay, Choices[Idx].Category, IconOrigin, IconSide);
        }

        const float LabelMaxW = TileSide - 12.0f;
        const float LabelW    = std::min(
            LabelMaxW,
            WorkspaceAppearance::MeasureWeightedTextWidth(Overlay, FontWeightClass::Regular, 10.5f, Choices[Idx].Label));
        WorkspaceAppearance::ConstructTruncatedText(
            Overlay,
            FontWeightClass::Regular,
            { Cell.Center().X - LabelW * 0.5f, Cell.Bottom() - 33.0f },
            LabelMaxW,
            10.5f,
            WorkspaceSurfaceColors::PrimaryInk,
            Choices[Idx].Label);

        const float CapW = std::min(
            LabelMaxW,
            WorkspaceAppearance::MeasureWeightedTextWidth(Overlay, FontWeightClass::Regular, 8.5f, Choices[Idx].Caption));
        WorkspaceAppearance::ConstructTruncatedText(
            Overlay,
            FontWeightClass::Regular,
            { Cell.Center().X - CapW * 0.5f, Cell.Bottom() - 17.0f },
            LabelMaxW,
            8.5f,
            Choices[Idx].PresetIndex >= 0 ? WorkspaceSurfaceColors::SelectionAmber : ColorRgba{ 148, 150, 158, 255 },
            Choices[Idx].Caption);

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            if (Choices[Idx].PresetIndex >= 0 && ConstructPresetCallback)
            {
                ConstructPresetCallback(static_cast<uint32_t>(Choices[Idx].PresetIndex));
            }
            else if (AttachedOutliner != nullptr)
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

    if (MaxScroll > 0.5f)
    {
        const Rectangle2D TrackRect{ GridArea.Right() + 4.0f, GridArea.Y + 2.0f, 4.0f, GridArea.Height - 4.0f };
        Overlay.FillRectangle(TrackRect, ColorRgba{ 36, 36, 40, 220 }, 2.0f);
        const float Ratio   = std::clamp(GridArea.Height / std::max(GridArea.Height, ContentH), 0.14f, 0.9f);
        const float ThumbH  = TrackRect.Height * Ratio;
        const float ScrollT = std::clamp(ConstructGridScrollY / MaxScroll, 0.0f, 1.0f);
        const float ThumbY  = TrackRect.Y + (TrackRect.Height - ThumbH) * ScrollT;
        Overlay.FillRectangle({ TrackRect.X, ThumbY, TrackRect.Width, ThumbH }, ColorRgba{ 126, 128, 136, 255 }, 2.0f);
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
    const float SheetH = 426.0f;
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
                if (Tier >= 4u)
                {
                    DisplaySettings.RenderEngineIndex    = 2u; // DDGI Raytracing
                    DisplaySettings.ShadowAlgorithmIndex = 4u; // WideTreeRayQuery
                }
                else
                {
                    DisplaySettings.RenderEngineIndex    = 3u; // SDF GI
                    DisplaySettings.ShadowAlgorithmIndex = 0u; // ContactPcss
                }
            }
        }
        RowY += 36.0f;
    }

    const bool AnyDropdownOpen = (EngineDropdownOpen || ShadowDropdownOpen || DebugDropdownOpen);

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
        ControlPointer Ptr = Input.PrimaryPointer;
        if (AnyDropdownOpen) Ptr.Enabled = false;
        if (ExclusiveSelector::ConstructCompact(Overlay, SwitchRect, ToggleRef, Ptr, 1.0f).Clicked)
        {
            ToggleRef = !ToggleRef;
        }
        RowY += 32.0f;
    };

    ConstructToggleRow("Global Illumination", DisplaySettings.GlobalIllumination);
    ConstructToggleRow("Reflections",         DisplaySettings.ReflectionsEnabled);
    ConstructToggleRow("Anti-aliasing",       DisplaySettings.AntiAliasingEnabled);
    ConstructToggleRow("Raytracing",          DisplaySettings.RaytracingEnabled);

    // BVH4 Reflection Raytrace vs Sky Environment toggle
    {
        bool BvhRefl = (DisplaySettings.ReflectionSourceIndex == 0u);
        WorkspaceAppearance::ConstructWeightedText(
            Overlay,
            FontWeightClass::Regular,
            { Sheet.X + 16.0f, RowY + 5.0f },
            11.5f,
            WorkspaceSurfaceColors::SecondaryInk,
            "BVH4 Specular Trace");
        const Rectangle2D SwitchRect{ Sheet.Right() - 16.0f - 44.0f, RowY + 1.0f, 44.0f, 22.0f };
        ControlPointer Ptr = Input.PrimaryPointer;
        if (AnyDropdownOpen) Ptr.Enabled = false;
        if (ExclusiveSelector::ConstructCompact(Overlay, SwitchRect, BvhRefl, Ptr, 1.0f).Clicked)
        {
            DisplaySettings.ReflectionSourceIndex = BvhRefl ? 1u : 0u;
        }
        RowY += 32.0f;
    }

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
    if (!AnyDropdownOpen && ScaleTrack.Contains(Cursor) && Input.PrimaryPointer.Down)
    {
        const float NextT                  = std::clamp((Cursor.X - ScaleTrack.X) / ScaleTrack.Width, 0.0f, 1.0f);
        DisplaySettings.RenderScalePercent = std::round((50.0f + NextT * 150.0f) / 5.0f) * 5.0f;
    }
    RowY += 46.0f;

    // 1. Render Engine Dropdown Button
    const Rectangle2D EngineBtn{ Sheet.X + 16.0f, RowY - 2.0f, Sheet.Width - 32.0f, 28.0f };
    EngineButtonBounds      = EngineBtn;
    std::string EngineLabel = "Engine: ";
    EngineLabel += QueryRenderEngineName(DisplaySettings.RenderEngineIndex);
    {
        ControlPointer Ptr = Input.PrimaryPointer;
        if (AnyDropdownOpen) Ptr.Enabled = EngineBtn.Contains(Cursor);
        if (DropdownSelector::ConstructButton(Overlay, EngineBtn, EngineLabel.c_str(), EngineDropdownOpen, Ptr).Pressed)
        {
            EngineDropdownOpen = !EngineDropdownOpen;
            ShadowDropdownOpen = false;
            DebugDropdownOpen  = false;
        }
    }
    RowY += 34.0f;

    // 2. Shadow Algorithm Dropdown Button
    const Rectangle2D ShadowBtn{ Sheet.X + 16.0f, RowY - 2.0f, Sheet.Width - 32.0f, 28.0f };
    ShadowButtonBounds      = ShadowBtn;
    std::string ShadowLabel = "Shadow: ";
    ShadowLabel += QueryShadowAlgorithmName(DisplaySettings.ShadowAlgorithmIndex);
    {
        ControlPointer Ptr = Input.PrimaryPointer;
        if (AnyDropdownOpen) Ptr.Enabled = ShadowBtn.Contains(Cursor);
        if (DropdownSelector::ConstructButton(Overlay, ShadowBtn, ShadowLabel.c_str(), ShadowDropdownOpen, Ptr).Pressed)
        {
            ShadowDropdownOpen = !ShadowDropdownOpen;
            EngineDropdownOpen = false;
            DebugDropdownOpen  = false;
        }
    }
    RowY += 34.0f;

    // 3. Debug View Dropdown Button
    const Rectangle2D DebugBtn{ Sheet.X + 16.0f, RowY - 2.0f, Sheet.Width - 32.0f, 28.0f };
    DebugButtonBounds      = DebugBtn;
    std::string DebugLabel = "Debug View: ";
    DebugLabel += QueryDebugChannelName(DisplaySettings.DebugChannelIndex);
    {
        ControlPointer Ptr = Input.PrimaryPointer;
        if (AnyDropdownOpen) Ptr.Enabled = DebugBtn.Contains(Cursor);
        if (DropdownSelector::ConstructButton(Overlay, DebugBtn, DebugLabel.c_str(), DebugDropdownOpen, Ptr).Pressed)
        {
            DebugDropdownOpen  = !DebugDropdownOpen;
            EngineDropdownOpen = false;
            ShadowDropdownOpen = false;
        }
    }

    // Active Option Sheet rendered last for clean z-ordering
    if (EngineDropdownOpen)
    {
        constexpr std::array<const char*, 5> EngineOptions = {{
            "Engine: MatCap",
            "Engine: OpenPBR Direct",
            "Engine: DDGI Raytracing",
            "Engine: SDF GI",
            "Engine: Path Tracer",
        }};
        const Rectangle2D DropBounds = DropdownSelector::QuerySheetBounds(EngineBtn, static_cast<uint32_t>(EngineOptions.size()));
        const uint32_t    CurIdx     = DisplaySettings.RenderEngineIndex % 5u;
        uint32_t          ChosenIdx  = CurIdx;
        if (DropdownSelector::ConstructOptionSheet(
                Overlay, EngineBtn, EngineOptions.data(), static_cast<uint32_t>(EngineOptions.size()),
                CurIdx, Input.PrimaryPointer, ChosenIdx).Clicked)
        {
            DisplaySettings.RenderEngineIndex = ChosenIdx;
            EngineDropdownOpen                = false;
        }
        else if (Input.PrimaryPointer.Pressed && !DropBounds.Contains(Cursor) && !EngineBtn.Contains(Cursor))
        {
            EngineDropdownOpen = false;
        }
    }
    else if (ShadowDropdownOpen)
    {
        constexpr std::array<const char*, 5> ShadowOptions = {{
            "Shadow: Contact PCSS",
            "Shadow: PCSS + Filter",
            "Shadow: SMRT",
            "Shadow: PCF 3x3",
            "Shadow: BVH4 Ray",
        }};
        const Rectangle2D DropBounds = DropdownSelector::QuerySheetBounds(ShadowBtn, static_cast<uint32_t>(ShadowOptions.size()));
        const uint32_t    CurIdx     = DisplaySettings.ShadowAlgorithmIndex % 5u;
        uint32_t          ChosenIdx  = CurIdx;
        if (DropdownSelector::ConstructOptionSheet(
                Overlay, ShadowBtn, ShadowOptions.data(), static_cast<uint32_t>(ShadowOptions.size()),
                CurIdx, Input.PrimaryPointer, ChosenIdx).Clicked)
        {
            DisplaySettings.ShadowAlgorithmIndex = ChosenIdx;
            ShadowDropdownOpen                   = false;
        }
        else if (Input.PrimaryPointer.Pressed && !DropBounds.Contains(Cursor) && !ShadowBtn.Contains(Cursor))
        {
            ShadowDropdownOpen = false;
        }
    }
    else if (DebugDropdownOpen)
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

void ViewportPanel::RenderForceOverlaySheet(
    PixelSpace&             Overlay,
    Rectangle2D             Bounds,
    Point2D                 ButtonBottomLeft,
    const PaneInputReading& Input)
{
    constexpr float SheetW = 296.0f;
    constexpr float SheetH = 398.0f;
    const float     SheetX = std::clamp(
        ButtonBottomLeft.X,
        Bounds.X + 10.0f,
        std::max(Bounds.X + 10.0f, Bounds.Right() - SheetW - 10.0f));
    const float       SheetY = ButtonBottomLeft.Y + 6.0f;
    const Rectangle2D Sheet{ SheetX, SheetY, SheetW, SheetH };
    const Point2D     Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    Overlay.FillRectangle(Sheet, ColorRgba{ 26, 26, 28, 248 }, 14.0f);
    Overlay.StrokeRectangle(Sheet, ColorRgba{ 58, 62, 70, 255 }, 14.0f, 1.0f);

    float RowY = Sheet.Y + 16.0f;
    WorkspaceAppearance::ConstructWeightedText(
        Overlay,
        FontWeightClass::SemiBold,
        { Sheet.X + 16.0f, RowY },
        11.0f,
        ColorRgba{ 104, 220, 255, 255 },
        "FORCE VISUALIZATION OVERLAY");
    RowY += 24.0f;

    auto DrawToggle = [&](const char* Label, bool& Flag)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Overlay,
            FontWeightClass::Regular,
            { Sheet.X + 16.0f, RowY + 4.0f },
            11.0f,
            WorkspaceSurfaceColors::SecondaryInk,
            Label);
        const Rectangle2D SwitchRect{ Sheet.Right() - 16.0f - 42.0f, RowY, 42.0f, 20.0f };
        if (ExclusiveSelector::ConstructCompact(Overlay, SwitchRect, Flag, Input.PrimaryPointer, 1.0f).Clicked)
        {
            Flag = !Flag;
        }
        RowY += 26.0f;
    };

    DrawToggle("Force Viz Master",    ForceOverlay.Enabled);
    DrawToggle("Slice Heatmap",       ForceOverlay.ShowHeatmap);
    DrawToggle("Vector Arrows",       ForceOverlay.ShowVectors);
    DrawToggle("3D RK4 Streamlines",  ForceOverlay.ShowStreamlines);
    DrawToggle("Reach Envelopes",     ForceOverlay.ShowBounds);
    DrawToggle("Motion Path & Orbits",ForceOverlay.ShowMotionCurves);
    DrawToggle("Cross-Section Probe", ForceOverlay.CrossSectionAnemometerActive);

    RowY += 4.0f;
    WorkspaceAppearance::ConstructWeightedText(
        Overlay,
        FontWeightClass::Regular,
        { Sheet.X + 16.0f, RowY + 4.0f },
        10.5f,
        WorkspaceSurfaceColors::SecondaryInk,
        "Slice Plane");

    constexpr std::array<std::pair<const char*, SimulationSpace::ForceFields::CrossSectionPlaneAxis>, 4> Planes = {{
        { "XY",   SimulationSpace::ForceFields::CrossSectionPlaneAxis::XYHorizontal },
        { "XZ",   SimulationSpace::ForceFields::CrossSectionPlaneAxis::XZLateral    },
        { "YZ",   SimulationSpace::ForceFields::CrossSectionPlaneAxis::YZFrontal    },
        { "All3", SimulationSpace::ForceFields::CrossSectionPlaneAxis::AllThreeAxes },
    }};
    float PlaneX = Sheet.Right() - 16.0f - 154.0f;
    for (const auto& Item : Planes)
    {
        const bool        Selected = (ForceOverlay.CrossSectionAxis == Item.second);
        const Rectangle2D Seg{ PlaneX, RowY, 36.0f, 22.0f };
        Overlay.FillRectangle(Seg, Selected ? ColorRgba{ 30, 68, 86, 255 } : ColorRgba{ 38, 38, 42, 255 }, 6.0f);
        Overlay.StrokeRectangle(Seg, Selected ? ColorRgba{ 104, 220, 255, 180 } : ColorRgba{ 62, 62, 68, 255 }, 6.0f, 1.0f);
        Overlay.TextCentered(Seg, 9.5f, Selected ? ColorRgba{ 226, 248, 255, 255 } : WorkspaceSurfaceColors::SecondaryInk, Item.first);
        if (Seg.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            ForceOverlay.CrossSectionAxis = Item.second;
        }
        PlaneX += 39.0f;
    }
    RowY += 30.0f;

    WorkspaceAppearance::ConstructWeightedText(
        Overlay,
        FontWeightClass::Regular,
        { Sheet.X + 16.0f, RowY + 4.0f },
        10.5f,
        WorkspaceSurfaceColors::SecondaryInk,
        "Stream Tint");
    constexpr std::array<std::pair<const char*, SimulationSpace::ForceFields::StreamlineColorMode>, 4> Tints = {{
        { "Speed", SimulationSpace::ForceFields::StreamlineColorMode::SpeedRamp    },
        { "Cyan",  SimulationSpace::ForceFields::StreamlineColorMode::CyanSingle   },
        { "Elev",  SimulationSpace::ForceFields::StreamlineColorMode::Elevation    },
        { "Cust",  SimulationSpace::ForceFields::StreamlineColorMode::CustomSingle },
    }};
    float TintX = Sheet.Right() - 16.0f - 170.0f;
    for (const auto& Item : Tints)
    {
        const bool        Selected = (ForceOverlay.StreamColorMode == Item.second);
        const Rectangle2D Seg{ TintX, RowY, 40.0f, 22.0f };
        Overlay.FillRectangle(Seg, Selected ? ColorRgba{ 30, 68, 86, 255 } : ColorRgba{ 38, 38, 42, 255 }, 6.0f);
        Overlay.StrokeRectangle(Seg, Selected ? ColorRgba{ 104, 220, 255, 180 } : ColorRgba{ 62, 62, 68, 255 }, 6.0f, 1.0f);
        Overlay.TextCentered(Seg, 9.5f, Selected ? ColorRgba{ 226, 248, 255, 255 } : WorkspaceSurfaceColors::SecondaryInk, Item.first);
        if (Seg.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            ForceOverlay.StreamColorMode = Item.second;
        }
        TintX += 43.0f;
    }
    RowY += 32.0f;

    auto DrawOverlaySlider = [&](const char* Label, float& Value, float MinV, float MaxV, const char* Format)
    {
        WorkspaceAppearance::ConstructWeightedText(Overlay, FontWeightClass::Regular, { Sheet.X + 16.0f, RowY }, 10.0f, WorkspaceSurfaceColors::SecondaryInk, Label);
        char ValText[32]{};
        std::snprintf(ValText, sizeof(ValText), Format, Value);
        const float ValW = WorkspaceAppearance::MeasureWeightedTextWidth(Overlay, FontWeightClass::Regular, 10.0f, ValText);
        WorkspaceAppearance::ConstructWeightedText(Overlay, FontWeightClass::Regular, { Sheet.Right() - 16.0f - ValW, RowY }, 10.0f, WorkspaceSurfaceColors::PrimaryInk, ValText);

        const Rectangle2D Track{ Sheet.X + 16.0f, RowY + 15.0f, Sheet.Width - 32.0f, 12.0f };
        const float       T = std::clamp((Value - MinV) / std::max(0.001f, MaxV - MinV), 0.0f, 1.0f);
        Overlay.FillRectangle(Track, ColorRgba{ 38, 38, 42, 255 }, 6.0f);
        Overlay.FillRectangle({ Track.X, Track.Y, std::max(8.0f, Track.Width * T), Track.Height }, ColorRgba{ 58, 134, 168, 255 }, 6.0f);
        Overlay.FillCircle({ Track.X + 6.0f + (Track.Width - 12.0f) * T, Track.Center().Y }, 5.5f, ColorRgba{ 228, 246, 255, 255 });
        if (Track.Contains(Cursor) && Input.PrimaryPointer.Down)
        {
            const float NextT = std::clamp((Cursor.X - Track.X) / Track.Width, 0.0f, 1.0f);
            Value             = MinV + NextT * (MaxV - MinV);
        }
        RowY += 32.0f;
    };

    DrawOverlaySlider("Thickness", ForceOverlay.StreamRibbonThickness, 0.25f, 3.0f, "%.2fx");
    DrawOverlaySlider("Emissive",  ForceOverlay.StreamEmissiveBoost,   0.0f,  3.0f, "%.2fx");
    DrawOverlaySlider("Opacity",   ForceOverlay.StreamOpacity,         0.1f,  1.0f, "%.2f");
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
    const bool    OverlayOpenBefore = ConstructSheetOpen || SettingsSheetOpen || ProjectionDropdownOpen || ForceOverlaySheetOpen;

    PushDiagnosticsSample(Δt);
    WorkspaceAppearance::ForwardInputToTextBuffer(ConstructSearchEditor, Input, Δt);
    if (Input.ShortcutAll)
    {
        ConfigureConstructSheetOpen(!ConstructSheetOpen);
    }
    if (Input.EscapePressed)
    {
        ConstructSheetOpen           = false;
        SettingsSheetOpen            = false;
        ProjectionDropdownOpen       = false;
        ForceOverlaySheetOpen        = false;
        ConstructSearchEditor.Active = false;
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

    // 1b. Right Execution Mode Capsule Strip (Reusable ExecutionModeStrip: Edit / Simulate / Play-Pause + optional Rewind)
    {
        const float PillW      = ExecutionModeStrip::PreferredStripWidth(false);
        const bool  ShowRewind = ForceOverlayToolsVisible && ((ToolbarRight - PillW - TagX) > 124.0f);
        const float TotalW     = ExecutionModeStrip::PreferredStripWidth(ShowRewind);
        const Rectangle2D StripBounds{ ToolbarRight - TotalW, HeadingY + 10.0f, TotalW, ExecutionModeStrip::RecommendedHeightPx };

        ExecutionModeStripReading StripReading {};
        StripReading.ActivePhase          = static_cast<ExecutionStripPhase>(ExecutionMode);
        StripReading.PlayPaused           = PlayPaused;
        StripReading.ShowPauseWhenPlaying = true;
        StripReading.ShowRewindButton     = ShowRewind;
        StripReading.OverlayBlockingInput = OverlayOpenBefore;

        ControlPointer Pointer {};
        Pointer.X        = Input.PrimaryPointer.X;
        Pointer.Y        = Input.PrimaryPointer.Y;
        Pointer.Down     = Input.PrimaryPointer.Down;
        Pointer.Pressed  = Input.PrimaryPointer.Pressed;
        Pointer.Released = Input.PrimaryPointer.Released;

        const ExecutionModeStripResult StripResult = ExecutionModeStrip::Construct(Surface, StripBounds, StripReading, Pointer);
        if (StripResult.PlayToggleRequested)
        {
            TogglePlayPause();
        }
        else if (StripResult.PhaseChanged)
        {
            ConfigureExecutionMode(static_cast<ViewportExecutionMode>(StripResult.SelectedPhase), false);
        }

        if (StripResult.RewindPressed)
        {
            RewindRequested       = true;
            SimulationTimeSeconds = 0.0;
        }

        if (ShowRewind)
        {
            char ClockText[24]{};
            std::snprintf(ClockText, sizeof(ClockText), "t %.1fs", SimulationTimeSeconds);
            const float ClockW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, ClockText);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { StripBounds.X - ClockW - 10.0f, HeadingY + 19.0f },
                10.0f,
                ColorRgba{ 168, 174, 182, 255 },
                ClockText);
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
            ForceOverlaySheetOpen  = false;
        }
    }

    // Right-aligned secondary tools: Split Viewport, Diagnostics, Settings (each 39x31 capsule)
    const Rectangle2D SettingsBtn{ ToolbarRight - 39.0f, ToolsRowY, 39.0f, 31.0f };
    const Rectangle2D DiagnosticsBtn{ SettingsBtn.X - 5.0f - 39.0f, ToolsRowY, 39.0f, 31.0f };
    const Rectangle2D SplitBtn{ DiagnosticsBtn.X - 5.0f - 39.0f, ToolsRowY, 39.0f, 31.0f };

    Rectangle2D ForceVizBtn{ ProjectionBtn.Right() + 8.0f, ToolsRowY, 128.0f, 31.0f };
    if (ForceOverlayToolsVisible && ForceVizBtn.Right() + 8.0f < SplitBtn.X)
    {
        const Rectangle2D ToggleZone{ ForceVizBtn.X, ForceVizBtn.Y, 96.0f, ForceVizBtn.Height };
        const Rectangle2D DropZone{ ToggleZone.Right(), ForceVizBtn.Y, 32.0f, ForceVizBtn.Height };
        const bool        VizActive  = ForceOverlay.Enabled;
        const bool        HoverAll   = !OverlayOpenBefore && ForceVizBtn.Contains(Cursor);

        Surface.FillRectangle(
            ForceVizBtn,
            VizActive ? ColorRgba{ 24, 48, 58, 255 } : (HoverAll ? ColorRgba{ 40, 40, 44, 255 } : ColorRgba{ 26, 26, 28, 255 }),
            15.5f);
        Surface.StrokeRectangle(
            ForceVizBtn,
            VizActive ? ColorRgba{ 104, 220, 255, 165 } : ColorRgba{ 255, 255, 255, 24 },
            15.5f,
            1.0f);
        Surface.StrokeLine(
            { ToggleZone.Right(), ForceVizBtn.Y + 6.0f },
            { ToggleZone.Right(), ForceVizBtn.Bottom() - 6.0f },
            VizActive ? ColorRgba{ 104, 220, 255, 72 } : ColorRgba{ 255, 255, 255, 22 },
            1.0f);

        const ColorRgba VizInk = VizActive ? ColorRgba{ 146, 232, 255, 255 } : ColorRgba{ 158, 162, 168, 255 };
        Surface.FillCircle({ ForceVizBtn.X + 14.0f, ForceVizBtn.Center().Y }, 3.8f, VizActive ? ColorRgba{ 104, 220, 255, 255 } : ColorRgba{ 110, 114, 120, 255 });
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { ForceVizBtn.X + 24.0f, ForceVizBtn.Y + 9.0f },
            10.5f,
            VizInk,
            "Force Viz");

        const Point2D CaretCenter{ DropZone.Center().X, DropZone.Center().Y };
        const std::array<Point2D, 3> Caret = {{
            { CaretCenter.X - 4.0f, CaretCenter.Y - 2.0f },
            { CaretCenter.X + 4.0f, CaretCenter.Y - 2.0f },
            { CaretCenter.X,        CaretCenter.Y + 3.0f },
        }};
        Surface.FillPolygon(Caret, VizInk);

        if (ToggleZone.Contains(Cursor) && Input.PrimaryPointer.Pressed && !ConstructSheetOpen && !SettingsSheetOpen)
        {
            ForceOverlay.Enabled = !ForceOverlay.Enabled;
        }
        else if (DropZone.Contains(Cursor) && Input.PrimaryPointer.Pressed && !ConstructSheetOpen && !SettingsSheetOpen)
        {
            ConfigureForceOverlaySheetOpen(!ForceOverlaySheetOpen);
        }
    }

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
        RenderCanvasPane(Surface, CanvasBounds, PrimaryProjection, 0u, ConstructedCount, Input, OverlayOpenBefore);
    }
    else
    {
        const float       PaneGap = 2.0f;
        const float       HalfW   = (CanvasBounds.Width - PaneGap) * 0.5f;
        const Rectangle2D LeftPane{ CanvasBounds.X, CanvasBounds.Y, HalfW, CanvasBounds.Height };
        const Rectangle2D RightPane{ LeftPane.Right() + PaneGap, CanvasBounds.Y, HalfW, CanvasBounds.Height };

        Surface.FillRectangle(CanvasBounds, ColorRgba{ 38, 38, 38, 255 });
        RenderCanvasPane(Surface, LeftPane, PrimaryProjection, 0u, ConstructedCount, Input, OverlayOpenBefore);
        RenderCanvasPane(Surface, RightPane, SecondaryProjection, 1u, ConstructedCount, Input, OverlayOpenBefore);
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
            const Rectangle2D DialogRect = QueryConstructDialogBounds(Bounds);
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
            const float SheetH = 426.0f;
            const float SheetX = std::clamp(SettingsBtn.Right() - SheetW, Bounds.X + 10.0f, std::max(Bounds.X + 10.0f, Bounds.Right() - SheetW - 10.0f));
            const Rectangle2D SheetRect{ SheetX, SettingsBtn.Bottom() + 6.0f, SheetW, SheetH };
            const Rectangle2D EngineSheetRect = DropdownSelector::QuerySheetBounds(EngineButtonBounds, 5u);
            const Rectangle2D ShadowSheetRect = DropdownSelector::QuerySheetBounds(ShadowButtonBounds, 5u);
            const Rectangle2D DebugSheetRect  = DropdownSelector::QuerySheetBounds(DebugButtonBounds, 5u);
            const bool InDropdown =
                (EngineDropdownOpen && EngineSheetRect.Contains(Cursor)) ||
                (ShadowDropdownOpen && ShadowSheetRect.Contains(Cursor)) ||
                (DebugDropdownOpen  && DebugSheetRect.Contains(Cursor));
            if (!SheetRect.Contains(Cursor) && !InDropdown)
            {
                SettingsSheetOpen  = false;
                EngineDropdownOpen = false;
                ShadowDropdownOpen = false;
                DebugDropdownOpen  = false;
            }
        }
    }

    if (ForceOverlayToolsVisible && ForceOverlaySheetOpen)
    {
        RenderForceOverlaySheet(ActiveOverlay, Bounds, { ForceVizBtn.X, ForceVizBtn.Bottom() }, Input);
        if (OverlayOpenBefore && Input.PrimaryPointer.Pressed && !ForceVizBtn.Contains(Cursor))
        {
            constexpr float SheetW = 296.0f;
            constexpr float SheetH = 398.0f;
            const float     SheetX = std::clamp(
                ForceVizBtn.X,
                Bounds.X + 10.0f,
                std::max(Bounds.X + 10.0f, Bounds.Right() - SheetW - 10.0f));
            const Rectangle2D SheetRect{ SheetX, ForceVizBtn.Bottom() + 6.0f, SheetW, SheetH };
            if (!SheetRect.Contains(Cursor))
            {
                ForceOverlaySheetOpen = false;
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
