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
    constexpr std::array<const char*, 12> Names = {{
        "Off",
        "Depth",
        "Albedo",
        "Shading Nrm",
        "Roughness",
        "Metalness",
        "Cluster ID",
        "Geom Nrm",
        "Sun Shadow",
        "Ambient Occ",
        "Direct Lit",
        "Specular"
    }};
    return Names[std::min<std::size_t>(Channel, Names.size() - 1u)];
}

[[nodiscard]] const char* QueryRenderEngineName(uint32_t EngineIdx) noexcept
{
    constexpr std::array<const char*, 6> Names = {{
        "MatCap",
        "OpenPBR Direct",
        "GTX SDF GI",
        "HighEnd BVH4 GI",
        "Path Tracer (Real-Time)",
        "Path Tracer (Final)",
    }};
    return Names[std::min<std::size_t>(EngineIdx, Names.size() - 1u)];
}

[[nodiscard]] const char* QueryShadowAlgorithmName(uint32_t AlgIdx) noexcept
{
    constexpr std::array<const char*, 5> Names = {{ "Contact PCSS", "PCSS + Filter", "SMRT", "PCF 3x3", "BVH4 Ray" }};
    return Names[std::min<std::size_t>(AlgIdx, Names.size() - 1u)];
}

[[nodiscard]] const char* QuerySdfPenumbraName(uint32_t ModeIdx) noexcept
{
    constexpr std::array<const char*, 6> Names = {{
        "0. Point SDF",
        "1. Analytic SDF Cone",
        "2. Voxel SDF Clipmap",
        "3. Voxel SDF + Cone GI",
        "4. Hybrid Sobol Cap",
        "5. SDF MC Ref (96spp)",
    }};
    return Names[std::min<std::size_t>(ModeIdx, Names.size() - 1u)];
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

    CanvasGpuTextureHandle = 0ull;
    CanvasGpuWidthPx       = 0u;
    CanvasGpuHeightPx      = 0u;

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

void ViewportPanel::AssignCanvasGpuTexture(uint64_t NativeTextureHandle, uint32_t WidthPx, uint32_t HeightPx) noexcept
{
    CanvasGpuTextureHandle = NativeTextureHandle;
    CanvasGpuWidthPx       = (NativeTextureHandle != 0ull) ? WidthPx : 0u;
    CanvasGpuHeightPx      = (NativeTextureHandle != 0ull) ? HeightPx : 0u;
}

void ViewportPanel::ClearCanvasRasterImage() noexcept
{
    CanvasGpuTextureHandle = 0ull;
    CanvasGpuWidthPx       = 0u;
    CanvasGpuHeightPx      = 0u;
    CanvasRasterTexture.DestroyPixels();
    CanvasRasterTexture.SetStatus(ImTextureStatus_Destroyed);
}

void ViewportPanel::ConfigureSceneTitle(std::string_view Title)
{
    SceneTitle.assign(Title.begin(), Title.end());
}

void ViewportPanel::ConfigureConstructCanvasDropCallback(ConstructCanvasDropCallback Callback)
{
    OnConstructCanvasDrop = std::move(Callback);
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
        ConstructSheetOpen      = false;
        ProjectionDropdownOpen  = false;
        HeaderDebugDropdownOpen = false;
    }
}

void ViewportPanel::ConfigureConstructSheetOpen(bool Open) noexcept
{
    ConstructSheetOpen = Open;
    if (Open)
    {
        SettingsSheetOpen       = false;
        ProjectionDropdownOpen  = false;
        HeaderDebugDropdownOpen = false;
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

void ViewportPanel::ConfigureViewportTimingBreakdown(const ViewportTimingBreakdown& Breakdown) noexcept
{
    LastViewportTiming = Breakdown;
}

void ViewportPanel::ConfigureProbeReadoutText(std::string_view ReadoutText)
{
    ProbeReadoutText.assign(ReadoutText.begin(), ReadoutText.end());
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

        const ViewportRasterSurface LiveSurface = RasterCallback(Spec);
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

    if (!RenderedLiveSurface && CanvasGpuTextureHandle != 0ull && CanvasGpuWidthPx > 0u && CanvasGpuHeightPx > 0u)
    {
        Surface.FillRectangle(PaneBounds, ColorRgba{ 14, 15, 18, 255 });
        const float SourceAspect = static_cast<float>(CanvasGpuWidthPx) / static_cast<float>(CanvasGpuHeightPx);
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
        Surface.FillTexturedRectangle(
            PaneBounds,
            nullptr,
            CanvasGpuTextureHandle,
            ColorRgba{ 255u, 255u, 255u, 255u },
            UvMin,
            UvMax);
        RenderedLiveSurface = true;
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

        const ControlContact ChipHit = DropdownSelector::ConstructButton(Surface, ProjChip, ProjName, ProjectionDropdownOpen, Input.PrimaryPointer);
        if (ChipHit.Pressed)
        {
            ProjectionChipConsumed = true;
            ActivePaneIndex        = PaneIndex;
            ProjectionDropdownOpen = !ProjectionDropdownOpen;
            ConstructSheetOpen     = false;
            SettingsSheetOpen      = false;
            DebugDropdownOpen      = false;
        }
        else if (!OverlayCoversCanvas && PaneBounds.Contains(Cursor) &&
                 (Input.PrimaryPointer.Pressed || Input.SecondaryPressed))
        {
            ActivePaneIndex = PaneIndex;
        }
    }

    const bool ImGuiMouseActive =
        (ImGui::GetCurrentContext() != nullptr) &&
        (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
         ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
         ImGui::IsMouseDown(ImGuiMouseButton_Middle));
    const bool AnyMouseHeld = ImGuiMouseActive || Input.PrimaryPointer.Down;

    if (CapturedInteractionPaneIndex < 0 &&
        !OverlayCoversCanvas &&
        !ProjectionChipConsumed &&
        PaneBounds.Contains(Cursor) &&
        AnyMouseHeld)
    {
        CapturedInteractionPaneIndex = static_cast<int32_t>(PaneIndex);
        ActivePaneIndex              = PaneIndex;
    }

    const bool CapturedHere = (CapturedInteractionPaneIndex == static_cast<int32_t>(PaneIndex));
    if (InteractionCallback &&
        ((!OverlayCoversCanvas && !ProjectionChipConsumed && PaneBounds.Contains(Cursor)) || CapturedHere))
    {
        InteractionCallback(PaneIndex, PaneBounds, Input);
    }
    if (CapturedHere && !AnyMouseHeld)
    {
        CapturedInteractionPaneIndex = -1;
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
        const Point2D Origin = Surface.QueryOrigin();
        const float   Scale  = std::max(0.25f, Surface.QueryScale());
        LastCanvasPhysicalBounds = {
            Origin.X + PaneBounds.X * Scale,
            Origin.Y + PaneBounds.Y * Scale,
            PaneBounds.Width * Scale,
            PaneBounds.Height * Scale,
        };
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

    const float RealFps = 1000.0f / std::max(0.1f, AvgMs);
    char FpsUnitBuf[32]{};
    std::snprintf(FpsUnitBuf, sizeof(FpsUnitBuf), "ms  (%.1f fps)", RealFps);

    const float ProfMs = (LastViewportTiming.TotalFrameMs > 0.0f)
        ? LastViewportTiming.TotalFrameMs
        : CurMs;
    char ProfWhole[16]{};
    char ProfFrac[16]{};
    const int ProfWholeInt = static_cast<int>(ProfMs);
    const int ProfTenths   = std::clamp(static_cast<int>(std::round((ProfMs - static_cast<float>(ProfWholeInt)) * 10.0f)), 0, 9);
    std::snprintf(ProfWhole, sizeof(ProfWhole), "%d", ProfWholeInt);
    std::snprintf(ProfFrac, sizeof(ProfFrac), ".%d", ProfTenths);

    char ProfSub[64]{};
    std::snprintf(
        ProfSub,
        sizeof(ProfSub),
        "%ux%u  |  %s / %s",
        LastViewportTiming.RenderWidthPx,
        LastViewportTiming.RenderHeightPx,
        QueryEngineShortLabel(LastViewportTiming.EngineMode),
        QueryShadowShortLabel(LastViewportTiming.ShadowAlgorithm));

    char ProfDebug[80]{};
    std::snprintf(
        ProfDebug,
        sizeof(ProfDebug),
        "Log: FrontierTiming.log  |  Scene sync: %.2f ms",
        LastViewportTiming.SceneSyncGpuMs);

    const std::array<DiagnosticsMetricChannel, 4> Channels = {{
        DiagnosticsMetricChannel{
            "Frame time",
            "Timing",
            WholeMs,
            FracMs,
            FpsUnitBuf,
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
        DiagnosticsMetricChannel{
            "Stage profiler",
            "Profiler",
            ProfWhole,
            ProfFrac,
            "ms  (frame)",
            ProfSub,
            ProfDebug,
            {},
            0.0f,
            1.0f,
            0.0f,
            false,
            ColorRgba{ 142, 198, 152, 240 },
            {
                { "GPU World",      FormatMs(LastViewportTiming.GpuPrimaryTraceMs) },
                { "GPU Denoise",    FormatMs(LastViewportTiming.GpuDenoiseMs) },
                { "GPU Clouds+Sky", FormatMs(LastViewportTiming.GpuCloudsMs + LastViewportTiming.GpuSkyLutMs) },
                { "GPU Readback",   FormatMs(LastViewportTiming.GpuReadbackCopyMs) },
                { "GPU Frame Total",FormatMs(LastViewportTiming.GpuTotalPassMs) },
                { "UI + Gizmo",     FormatMs(LastViewportTiming.DesktopPollAndUiMs + LastViewportTiming.GizmoOverlayMs) },
                { "Present / Swap", FormatMs(LastViewportTiming.SwapchainPresentMs) },
            }
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

Rectangle2D ViewportPanel::QueryConstructDialogBounds(Rectangle2D Bounds) const noexcept
{
    const float DialogW = std::min(532.0f, std::max(300.0f, Bounds.Width - 36.0f));
    const float DialogH = std::min(346.0f, std::max(240.0f, Bounds.Height - 70.0f));
    return { Bounds.Center().X - DialogW * 0.5f, Bounds.Y + 94.0f, DialogW, DialogH };
}

void ViewportPanel::RenderConstructDialog(
    PixelSpace&             Overlay,
    Rectangle2D             Bounds,
    const PaneInputReading& Input)
{
    const Rectangle2D Dialog = QueryConstructDialogBounds(Bounds);

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

    if (OpenContentBrowserCallback)
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
}

void ViewportPanel::RenderSettingsSheet(
    PixelSpace&             Overlay,
    Rectangle2D             Bounds,
    Point2D                 GearButtonRightBottom,
    const PaneInputReading& Input)
{
    const float SheetW = 286.0f;
    const float SheetH = std::min(796.0f, std::max(426.0f, Bounds.Height - 22.0f));
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

    const bool AnyDropdownOpen = (EngineDropdownOpen || ShadowDropdownOpen || SdfPenumbraDropdownOpen || DebugDropdownOpen);

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

    // Trace Resolution capsules (Full / 1/2 / 1/4) for all render & shadow algorithms
    {
        WorkspaceAppearance::ConstructWeightedText(
            Overlay,
            FontWeightClass::Regular,
            { Sheet.X + 16.0f, RowY + 5.0f },
            11.5f,
            WorkspaceSurfaceColors::SecondaryInk,
            "Trace Res");

        constexpr std::array<const char*, 3> TraceLabels = {{ "Full", "1/2", "1/4" }};
        const float GroupW   = 138.0f;
        const float CapsuleW = (GroupW - 8.0f) / 3.0f;
        const float GroupX   = Sheet.Right() - 16.0f - GroupW;
        for (uint32_t ResIdx = 0u; ResIdx < 3u; ++ResIdx)
        {
            const Rectangle2D Capsule{ GroupX + static_cast<float>(ResIdx) * (CapsuleW + 4.0f), RowY + 1.0f, CapsuleW, 22.0f };
            const bool        Active = ((DisplaySettings.TraceResolutionIndex % 3u) == ResIdx);
            Overlay.FillRectangle(Capsule, Active ? ColorRgba{ 62, 62, 62, 255 } : ColorRgba{ 36, 36, 36, 255 }, 11.0f);
            Overlay.StrokeRectangle(Capsule, Active ? WorkspaceSurfaceColors::PreviewGoldInk : ColorRgba{ 255, 255, 255, 16 }, 11.0f, 1.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Overlay, FontWeightClass::Regular, 9.5f, TraceLabels[ResIdx]);
            WorkspaceAppearance::ConstructWeightedText(
                Overlay,
                FontWeightClass::Regular,
                { Capsule.Center().X - TW * 0.5f, Capsule.Y + 5.0f },
                9.5f,
                Active ? WorkspaceSurfaceColors::PrimaryInk : ColorRgba{ 162, 162, 162, 255 },
                TraceLabels[ResIdx]);
            if (!AnyDropdownOpen && Capsule.Contains(Cursor) && Input.PrimaryPointer.Pressed)
            {
                DisplaySettings.TraceResolutionIndex = ResIdx;
            }
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
            EngineDropdownOpen      = !EngineDropdownOpen;
            ShadowDropdownOpen      = false;
            SdfPenumbraDropdownOpen = false;
            DebugDropdownOpen       = false;
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
            ShadowDropdownOpen      = !ShadowDropdownOpen;
            EngineDropdownOpen      = false;
            SdfPenumbraDropdownOpen = false;
            DebugDropdownOpen       = false;
        }
    }
    RowY += 34.0f;

    // 2b. 6-Mode SDF Shadow & Penumbra Dropdown Button
    const Rectangle2D SdfPenumbraBtn{ Sheet.X + 16.0f, RowY - 2.0f, Sheet.Width - 32.0f, 28.0f };
    SdfPenumbraButtonBounds      = SdfPenumbraBtn;
    std::string SdfPenumbraLabel = "SDF Penumbra: ";
    SdfPenumbraLabel += QuerySdfPenumbraName(DisplaySettings.SdfShadowPenumbraIndex);
    {
        ControlPointer Ptr = Input.PrimaryPointer;
        if (AnyDropdownOpen) Ptr.Enabled = SdfPenumbraBtn.Contains(Cursor);
        if (DropdownSelector::ConstructButton(Overlay, SdfPenumbraBtn, SdfPenumbraLabel.c_str(), SdfPenumbraDropdownOpen, Ptr).Pressed)
        {
            SdfPenumbraDropdownOpen = !SdfPenumbraDropdownOpen;
            EngineDropdownOpen      = false;
            ShadowDropdownOpen      = false;
            DebugDropdownOpen       = false;
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
            DebugDropdownOpen       = !DebugDropdownOpen;
            EngineDropdownOpen      = false;
            ShadowDropdownOpen      = false;
            SdfPenumbraDropdownOpen = false;
        }
    }
    RowY += 36.0f;

    // 4. Atmosphere, Celestials & Volumetric Clouds Quick Controls
    if (RowY + 120.0f <= Sheet.Bottom())
    {
        WorkspaceAppearance::ConstructWeightedText(
            Overlay,
            FontWeightClass::Medium,
            { Sheet.X + 16.0f, RowY },
            9.5f,
            ColorRgba{ 138, 138, 138, 255 },
            "ATMOSPHERE & CLOUDS");
        RowY += 18.0f;

        if (RowY + 30.0f <= Sheet.Bottom()) ConstructToggleRow("Physical Sky",       DisplaySettings.AtmosphereSkyEnabled);
        if (RowY + 30.0f <= Sheet.Bottom()) ConstructToggleRow("Volumetric Clouds",  DisplaySettings.VolumetricCloudsEnabled);
        if (RowY + 30.0f <= Sheet.Bottom()) ConstructToggleRow("Cloud Shadows",      DisplaySettings.CloudShadowsEnabled);
        if (RowY + 30.0f <= Sheet.Bottom()) ConstructToggleRow("Sun, Moons & Stars", DisplaySettings.CelestialStarsEnabled);
        if (RowY + 30.0f <= Sheet.Bottom()) ConstructToggleRow("Day Cycle",          DisplaySettings.DayCyclePlaying);

        // Time of Day slider [0..24h]
        if (RowY + 36.0f <= Sheet.Bottom())
        {
            WorkspaceAppearance::ConstructWeightedText(Overlay, FontWeightClass::Regular, { Sheet.X + 16.0f, RowY + 2.0f }, 10.5f, WorkspaceSurfaceColors::SecondaryInk, "Time of Day");
            const uint32_t TotalMins = static_cast<uint32_t>(std::round(std::clamp(DisplaySettings.SolarTimeOfDayHours, 0.0f, 24.0f) * 60.0f)) % 1440u;
            char TimeLabel[24]{};
            std::snprintf(TimeLabel, sizeof(TimeLabel), "%02u:%02u", TotalMins / 60u, TotalMins % 60u);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Overlay, FontWeightClass::Regular, 10.0f, TimeLabel);
            WorkspaceAppearance::ConstructWeightedText(Overlay, FontWeightClass::Regular, { Sheet.Right() - 16.0f - TW, RowY + 2.0f }, 10.0f, WorkspaceSurfaceColors::PrimaryInk, TimeLabel);

            const Rectangle2D Track{ Sheet.X + 16.0f, RowY + 20.0f, Sheet.Width - 32.0f, 12.0f };
            const float NormT = std::clamp(DisplaySettings.SolarTimeOfDayHours / 24.0f, 0.0f, 1.0f);
            Overlay.FillRectangle(Track, ColorRgba{ 38, 38, 38, 255 }, 6.0f);
            Overlay.FillRectangle({ Track.X, Track.Y, std::max(8.0f, Track.Width * NormT), Track.Height }, ColorRgba{ 82, 82, 82, 255 }, 6.0f);
            Overlay.FillCircle({ Track.X + 6.0f + (Track.Width - 12.0f) * NormT, Track.Center().Y }, 5.5f, WorkspaceSurfaceColors::PrimaryInk);
            if (!AnyDropdownOpen && Track.Contains(Cursor) && Input.PrimaryPointer.Down)
            {
                const float NextT = std::clamp((Cursor.X - Track.X) / Track.Width, 0.0f, 1.0f);
                DisplaySettings.SolarTimeOfDayHours = std::round(NextT * 240.0f) / 10.0f;
            }
            RowY += 38.0f;
        }

        // Cloud Cover slider [0..100%]
        if (RowY + 36.0f <= Sheet.Bottom())
        {
            WorkspaceAppearance::ConstructWeightedText(Overlay, FontWeightClass::Regular, { Sheet.X + 16.0f, RowY + 2.0f }, 10.5f, WorkspaceSurfaceColors::SecondaryInk, "Cloud Cover");
            char CovLabel[24]{};
            std::snprintf(CovLabel, sizeof(CovLabel), "%.0f%%", std::clamp(DisplaySettings.CloudFraction, 0.0f, 1.0f) * 100.0f);
            const float CW = WorkspaceAppearance::MeasureWeightedTextWidth(Overlay, FontWeightClass::Regular, 10.0f, CovLabel);
            WorkspaceAppearance::ConstructWeightedText(Overlay, FontWeightClass::Regular, { Sheet.Right() - 16.0f - CW, RowY + 2.0f }, 10.0f, WorkspaceSurfaceColors::PrimaryInk, CovLabel);

            const Rectangle2D Track{ Sheet.X + 16.0f, RowY + 20.0f, Sheet.Width - 32.0f, 12.0f };
            const float NormC = std::clamp(DisplaySettings.CloudFraction, 0.0f, 1.0f);
            Overlay.FillRectangle(Track, ColorRgba{ 38, 38, 38, 255 }, 6.0f);
            Overlay.FillRectangle({ Track.X, Track.Y, std::max(8.0f, Track.Width * NormC), Track.Height }, ColorRgba{ 82, 82, 82, 255 }, 6.0f);
            Overlay.FillCircle({ Track.X + 6.0f + (Track.Width - 12.0f) * NormC, Track.Center().Y }, 5.5f, WorkspaceSurfaceColors::PrimaryInk);
            if (!AnyDropdownOpen && Track.Contains(Cursor) && Input.PrimaryPointer.Down)
            {
                const float NextT = std::clamp((Cursor.X - Track.X) / Track.Width, 0.0f, 1.0f);
                DisplaySettings.CloudFraction = std::round(NextT * 100.0f) / 100.0f;
            }
            RowY += 38.0f;
        }

        // Wind Speed slider [0..35 m/s]
        if (RowY + 36.0f <= Sheet.Bottom())
        {
            WorkspaceAppearance::ConstructWeightedText(Overlay, FontWeightClass::Regular, { Sheet.X + 16.0f, RowY + 2.0f }, 10.5f, WorkspaceSurfaceColors::SecondaryInk, "Wind Speed");
            char WindLabel[24]{};
            std::snprintf(WindLabel, sizeof(WindLabel), "%.0f m/s", std::clamp(DisplaySettings.CloudWindSpeedMps, 0.0f, 35.0f));
            const float WW = WorkspaceAppearance::MeasureWeightedTextWidth(Overlay, FontWeightClass::Regular, 10.0f, WindLabel);
            WorkspaceAppearance::ConstructWeightedText(Overlay, FontWeightClass::Regular, { Sheet.Right() - 16.0f - WW, RowY + 2.0f }, 10.5f, WorkspaceSurfaceColors::PrimaryInk, WindLabel);

            const Rectangle2D Track{ Sheet.X + 16.0f, RowY + 20.0f, Sheet.Width - 32.0f, 12.0f };
            const float NormW = std::clamp(DisplaySettings.CloudWindSpeedMps / 35.0f, 0.0f, 1.0f);
            Overlay.FillRectangle(Track, ColorRgba{ 38, 38, 38, 255 }, 6.0f);
            Overlay.FillRectangle({ Track.X, Track.Y, std::max(8.0f, Track.Width * NormW), Track.Height }, ColorRgba{ 82, 82, 82, 255 }, 6.0f);
            Overlay.FillCircle({ Track.X + 6.0f + (Track.Width - 12.0f) * NormW, Track.Center().Y }, 5.5f, WorkspaceSurfaceColors::PrimaryInk);
            if (!AnyDropdownOpen && Track.Contains(Cursor) && Input.PrimaryPointer.Down)
            {
                const float NextT = std::clamp((Cursor.X - Track.X) / Track.Width, 0.0f, 1.0f);
                DisplaySettings.CloudWindSpeedMps = std::round(NextT * 35.0f);
            }
        }
    }

    // Active Option Sheet rendered last for clean z-ordering
    if (EngineDropdownOpen)
    {
        constexpr std::array<const char*, 6> EngineOptions = {{
            "Engine: MatCap",
            "Engine: OpenPBR Direct",
            "Engine: GTX SDF GI",
            "Engine: HighEnd BVH4 GI",
            "Engine: Path Tracer (Real-Time)",
            "Engine: Path Tracer (Final)",
        }};
        const Rectangle2D DropBounds = DropdownSelector::QuerySheetBounds(EngineBtn, static_cast<uint32_t>(EngineOptions.size()));
        const uint32_t    CurIdx     = DisplaySettings.RenderEngineIndex % static_cast<uint32_t>(EngineOptions.size());
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
    else if (SdfPenumbraDropdownOpen)
    {
        constexpr std::array<const char*, 6> SdfOptions = {{
            "0. Point-Sample SDF (No Penumbra)",
            "1. Analytic SDF Cone Penumbra",
            "2. 3D Voxel SDF Clipmap Penumbra",
            "3. 3D Voxel SDF Penumbra + Cone GI",
            "4. Hybrid SDF + Sobol Spherical Cap",
            "5. SDF Monte Carlo PT Ref (96 SPP)",
        }};
        const Rectangle2D DropBounds = DropdownSelector::QuerySheetBounds(SdfPenumbraBtn, static_cast<uint32_t>(SdfOptions.size()));
        const uint32_t    CurIdx     = DisplaySettings.SdfShadowPenumbraIndex % static_cast<uint32_t>(SdfOptions.size());
        uint32_t          ChosenIdx  = CurIdx;
        if (DropdownSelector::ConstructOptionSheet(
                Overlay, SdfPenumbraBtn, SdfOptions.data(), static_cast<uint32_t>(SdfOptions.size()),
                CurIdx, Input.PrimaryPointer, ChosenIdx).Clicked)
        {
            DisplaySettings.SdfShadowPenumbraIndex = ChosenIdx;
            SdfPenumbraDropdownOpen                = false;
        }
        else if (Input.PrimaryPointer.Pressed && !DropBounds.Contains(Cursor) && !SdfPenumbraBtn.Contains(Cursor))
        {
            SdfPenumbraDropdownOpen = false;
        }
    }
    else if (DebugDropdownOpen)
    {
        constexpr std::array<const char*, 12> DebugOptions = {{
            "Debug View: Off",
            "Debug View: Depth",
            "Debug View: Albedo",
            "Debug View: Shading Normal",
            "Debug View: Roughness",
            "Debug View: Metalness",
            "Debug View: Material / Cluster ID",
            "Debug View: Geometric Normal",
            "Debug View: Solar Shadow",
            "Debug View: Ambient Occlusion",
            "Debug View: Direct Lighting",
            "Debug View: Specular & Reflections",
        }};
        const Rectangle2D DropBounds = DropdownSelector::QuerySheetBounds(DebugBtn, static_cast<uint32_t>(DebugOptions.size()));
        const uint32_t    CurrentIdx = DisplaySettings.DebugChannelIndex % 12u;
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
    const bool    OverlayOpenBefore = ConstructSheetOpen || SettingsSheetOpen || ProjectionDropdownOpen || HeaderDebugDropdownOpen;

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
        HeaderDebugDropdownOpen      = false;
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

    // 1b. Right Execution Mode Capsule Strip (Reusable ExecutionModeStrip: Edit / Simulate / Play-Pause)
    {
        const float TotalW = ExecutionModeStrip::PreferredStripWidth(false);
        const Rectangle2D StripBounds{ ToolbarRight - TotalW, HeadingY + 10.0f, TotalW, ExecutionModeStrip::RecommendedHeightPx };

        ExecutionModeStripReading StripReading {};
        StripReading.ActivePhase          = static_cast<ExecutionStripPhase>(ExecutionMode);
        StripReading.PlayPaused           = PlayPaused;
        StripReading.ShowPauseWhenPlaying = true;
        StripReading.ShowRewindButton     = false;
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

    // Header Debug View Dropdown Pill (1-click access to Lit + 15 modular debug views)
    {
        const float DebugStartX = ProjectionBtn.Right() + 8.0f;
        std::string HeaderDbgLabel = "Debug: ";
        HeaderDbgLabel += QueryDebugChannelName(DisplaySettings.DebugChannelIndex);
        const float DbgBtnW = std::clamp(
            WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, HeaderDbgLabel) + 38.0f,
            102.0f, 156.0f);
        if (DebugStartX + DbgBtnW + 8.0f < SplitBtn.X)
        {
            const Rectangle2D HeaderDbgBtn{ DebugStartX, ToolsRowY, DbgBtnW, 31.0f };
            HeaderDebugButtonBounds = HeaderDbgBtn;
            ControlPointer DbgPtr   = Input.PrimaryPointer;
            if (ConstructSheetOpen || SettingsSheetOpen || ProjectionDropdownOpen)
            {
                DbgPtr.Enabled = false;
            }
            if (DropdownSelector::ConstructButton(
                    Surface, HeaderDbgBtn, HeaderDbgLabel.c_str(), HeaderDebugDropdownOpen, DbgPtr).Pressed)
            {
                HeaderDebugDropdownOpen = !HeaderDebugDropdownOpen;
                ProjectionDropdownOpen  = false;
                ConstructSheetOpen      = false;
                SettingsSheetOpen       = false;
            }
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

    if (HeaderDebugDropdownOpen)
    {
        constexpr std::array<const char*, 12> HeaderDebugOptions = {{
            "00 · Lit (Off)",
            "01 · Depth",
            "02 · Albedo",
            "03 · Shading Normal",
            "04 · Roughness",
            "05 · Metalness",
            "06 · Material / Cluster ID",
            "07 · Geometric Normal",
            "08 · Solar Shadow",
            "09 · Ambient Occlusion",
            "10 · Direct Lighting",
            "11 · Specular & Reflections",
        }};
        const Rectangle2D DropSheet = DropdownSelector::QuerySheetBounds(
            HeaderDebugButtonBounds, static_cast<uint32_t>(HeaderDebugOptions.size()));
        const uint32_t CurIdx    = DisplaySettings.DebugChannelIndex % 12u;
        uint32_t       ChosenIdx = CurIdx;
        if (DropdownSelector::ConstructOptionSheet(
                ActiveOverlay, HeaderDebugButtonBounds, HeaderDebugOptions.data(),
                static_cast<uint32_t>(HeaderDebugOptions.size()), CurIdx, Input.PrimaryPointer, ChosenIdx).Clicked)
        {
            DisplaySettings.DebugChannelIndex = ChosenIdx;
            HeaderDebugDropdownOpen           = false;
        }
        else if (OverlayOpenBefore && Input.PrimaryPointer.Pressed &&
                 !DropSheet.Contains(Cursor) && !HeaderDebugButtonBounds.Contains(Cursor))
        {
            HeaderDebugDropdownOpen = false;
        }
    }

    if (ConstructSheetOpen)
    {
        RenderConstructDialog(ActiveOverlay, Bounds, Input);
        if (OverlayOpenBefore && Input.PrimaryPointer.Pressed && !ConstructBtn.Contains(Cursor) && !ConstructDragActive)
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
            const float SheetH = std::min(796.0f, std::max(426.0f, Bounds.Height - 22.0f));
            const float SheetX = std::clamp(SettingsBtn.Right() - SheetW, Bounds.X + 10.0f, std::max(Bounds.X + 10.0f, Bounds.Right() - SheetW - 10.0f));
            const Rectangle2D SheetRect{ SheetX, SettingsBtn.Bottom() + 6.0f, SheetW, SheetH };
            const Rectangle2D EngineSheetRect      = DropdownSelector::QuerySheetBounds(EngineButtonBounds, 6u);
            const Rectangle2D ShadowSheetRect      = DropdownSelector::QuerySheetBounds(ShadowButtonBounds, 5u);
            const Rectangle2D SdfPenumbraSheetRect = DropdownSelector::QuerySheetBounds(SdfPenumbraButtonBounds, 6u);
            const Rectangle2D DebugSheetRect       = DropdownSelector::QuerySheetBounds(DebugButtonBounds, 12u);
            const bool InDropdown =
                (EngineDropdownOpen      && EngineSheetRect.Contains(Cursor)) ||
                (ShadowDropdownOpen      && ShadowSheetRect.Contains(Cursor)) ||
                (SdfPenumbraDropdownOpen && SdfPenumbraSheetRect.Contains(Cursor)) ||
                (DebugDropdownOpen       && DebugSheetRect.Contains(Cursor));
            if (!SheetRect.Contains(Cursor) && !InDropdown)
            {
                SettingsSheetOpen       = false;
                EngineDropdownOpen      = false;
                ShadowDropdownOpen      = false;
                SdfPenumbraDropdownOpen = false;
                DebugDropdownOpen       = false;
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
