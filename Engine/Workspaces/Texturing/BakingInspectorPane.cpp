/*============================================================================================================================================
                                                           BAKINGINSPECTORPANE.CPP
============================================================================================================================================*/
// 🧩 Implementation of BakingInspectorPane: study title, KPI trio, progress bar, raycaster sliders, volume SDF section & Paint bridge.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Texturing/BakingInspectorPane.h"

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

using namespace TexturingAppearance;

namespace
{

[[nodiscard]] bool PointInsideRect(Rectangle2D Rect, Point2D Cursor) noexcept
{
    return Cursor.X >= Rect.X && Cursor.X <= Rect.Right() && Cursor.Y >= Rect.Y && Cursor.Y <= Rect.Bottom();
}

} // namespace

BakingInspectorPane::~BakingInspectorPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void BakingInspectorPane::AttachSession(BakingWorkspaceSession* TargetSession) noexcept
{
    Session        = TargetSession;
    ScrollOffsetY  = 0.0f;
    ActiveSliderId = -1;
}

void BakingInspectorPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##BakingInspectorCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds { WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, DeltaSeconds);
    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, ActiveSliderId >= 0);
}

void BakingInspectorPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  DeltaSeconds)
{
    (void)DeltaSeconds;
    if (Session == nullptr || Bounds.Width < 180.0f || Bounds.Height < 180.0f)
    {
        return;
    }

    const TexturingThemeColors Theme   = QueryTexturingThemeColors();
    const Point2D              Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool                 Pressed = Input.PrimaryPointer.Pressed;

    if (!Input.PrimaryPointer.Down)
    {
        ActiveSliderId = -1;
    }

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PaneBackground, 0.0f);

    // Header
    constexpr float PadX       = 14.0f;
    constexpr float HeaderTopY = 12.0f;
    const float     InnerW     = std::max(80.0f, Bounds.Width - PadX * 2.0f);
    const float     HeaderH    = ConstructPaneHeadingBar(
        Surface,
        Bounds.X + PadX,
        Bounds.Y + HeaderTopY,
        InnerW,
        "Baker Parameters",
        "SURFACE + VOLUME SDF",
        Theme);

    // Footer
    constexpr float   FooterH = 26.0f;
    const Rectangle2D FooterBounds{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    ConstructPaneStatusFooter(Surface, FooterBounds, "Edge Dilation Padding Included", "Real Float32 SDF", Theme);

    // Scrollable region
    const float       ScrollTop = Bounds.Y + HeaderTopY + HeaderH;
    const Rectangle2D ScrollViewport{
        Bounds.X,
        ScrollTop,
        Bounds.Width,
        std::max(1.0f, FooterBounds.Y - ScrollTop),
    };

    if (PointInsideRect(ScrollViewport, Cursor) && std::abs(Input.WheelDeltaY) > 0.01f)
    {
        ScrollOffsetY = std::clamp(ScrollOffsetY - Input.WheelDeltaY * 32.0f, 0.0f, MaxScrollY);
    }

    Surface.PushClip(ScrollViewport);
    const float LeftX = ScrollViewport.X + PadX;
    float       CurY  = ScrollViewport.Y + 8.0f - ScrollOffsetY;

    // 1. Study Object Heading
    const BakerStudyDescriptor& StudyDesc = Session->QueryActiveStudyDescriptor();
    char TriMeta[64];
    std::snprintf(TriMeta, sizeof(TriMeta), "%u LOW · %u HIGH TRIS", Session->QueryActiveLowTriangles(), Session->QueryActiveHighTriangles());
    const char* StudyTitle = !Session->CustomLowAssetLabel.empty() ? Session->CustomLowAssetLabel.c_str() : StudyDesc.Label;
    CurY += ConstructSubjectHeadingBlock(Surface, LeftX, CurY, InnerW, ControlIcon::BakeBox, StudyTitle, TriMeta, Theme) + 10.0f;

    // 2. KPI Trio
    char ResVal[32];
    char ResSub[32];
    char SamplesVal[16];
    char VolumeVal[16];
    std::snprintf(ResVal, sizeof(ResVal), "%upx", Session->TargetResolutionPx);
    std::snprintf(ResSub, sizeof(ResSub), "%zu Maps", Session->CountSelectedTargets());
    std::snprintf(SamplesVal, sizeof(SamplesVal), "%d", Session->AoSampleCount);
    std::snprintf(VolumeVal, sizeof(VolumeVal), "%u³", Session->VolumeGridDim);

    const std::array<MetricTileReading, 3u> Kpis {{
        { "Resolution", ResVal,     ResSub,                                       true  },
        { "Rays / Px",  SamplesVal, "Hemisphere",                                 false },
        { "Volume SDF", VolumeVal,  Session->VolumeSdfBaked ? "Baked" : "Pending", false },
    }};
    CurY += ConstructMetricTrioTiles(Surface, LeftX, CurY, InnerW, Kpis, Theme) + 12.0f;

    // 3. Bake Progress Box
    {
        const Rectangle2D ProgBox{ LeftX, CurY, InnerW, 78.0f };
        BeginSectionContainer(Surface, ProgBox, Theme, 16.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { LeftX + 12.0f, CurY + 10.0f },
            InnerW - 24.0f,
            10.0f,
            Theme.TextMuted,
            Session->ProgressStatus);

        const Rectangle2D TrackRect{ LeftX + 12.0f, CurY + 28.0f, InnerW - 24.0f, 6.0f };
        Surface.FillRectangle(TrackRect, Theme.WellSurface, 3.0f);
        const float ClampedProg = std::clamp(Session->BakeProgress, 0.0f, 1.0f);
        if (ClampedProg > 0.005f)
        {
            Surface.FillRectangle({ TrackRect.X, TrackRect.Y, std::max(6.0f, TrackRect.Width * ClampedProg), TrackRect.Height }, Theme.HighlightFill, 3.0f);
        }

        const Rectangle2D BakeBtnRect{ LeftX + 12.0f, CurY + 42.0f, InnerW - 24.0f, 26.0f };
        if (ConstructCapsuleButtonControl(
                Surface,
                BakeBtnRect,
                Session->BakingActive ? "Baking..." : "Bake Selected Maps",
                Theme,
                Cursor,
                Pressed,
                false,
                true,
                ControlIcon::BakeBox,
                10.0f,
                13.0f,
                OptionalTooltip::FromTitled("Bake Selected Maps", "Compute all checked surface and volume SDF output targets.")))
        {
            Session->ExecuteBakeSimulation();
        }
        CurY += ProgBox.Height + 12.0f;
    }

    // Interactive slider row retaining ActiveSliderId across drags
    auto RenderFloatSlider = [&](int32_t SliderKey, const char* Label, float MinVal, float MaxVal, const char* UnitText, const char* Fmt, float& InOutScalar)
    {
        float Norm = std::clamp((InOutScalar - MinVal) / std::max(0.0001f, MaxVal - MinVal), 0.0f, 1.0f);
        bool  Drag = (ActiveSliderId == SliderKey);
        char  NumBuf[32];
        std::snprintf(NumBuf, sizeof(NumBuf), Fmt, InOutScalar);
        if (ConstructSliderReadoutRow(Surface, LeftX + 12.0f, CurY, InnerW - 24.0f, Label, NumBuf, UnitText, Norm, Theme, Input.PrimaryPointer, Drag))
        {
            InOutScalar = std::clamp(MinVal + Norm * (MaxVal - MinVal), MinVal, MaxVal);
        }
        if (Drag)
        {
            ActiveSliderId = SliderKey;
        }
        CurY += 52.0f;
    };

    // 4. Section 1: GPU Device & Surface Ray Settings
    {
        const float       SecStartY = CurY;
        const float       SecBodyH  = SurfaceSectionOpen ? (40.0f + 56.0f * 3.0f + 52.0f * 5.0f + 36.0f * 2.0f) : 38.0f;
        const Rectangle2D SecRect{ LeftX, SecStartY, InnerW, SecBodyH };
        BeginSectionContainer(Surface, SecRect, Theme, 18.0f);

        if (ConstructSectionHeaderRow(
                Surface,
                { LeftX + 12.0f, CurY + 10.0f, InnerW - 24.0f, 22.0f },
                ControlIcon::SlidersQuality,
                "GPU Device & Surface Ray Settings",
                SurfaceSectionOpen ? "-" : "+",
                SurfaceSectionOpen,
                Theme,
                Cursor,
                Pressed))
        {
            SurfaceSectionOpen = !SurfaceSectionOpen;
        }
        CurY += 38.0f;

        if (SurfaceSectionOpen)
        {
            static constexpr const char* DeviceOpts[3] { "Auto GPU", "GTX BVH4", "RTX Query" };
            uint32_t ChosenDevice = static_cast<uint32_t>(Session->ComputeDevice);
            if (ConstructSegmentedCapsuleRow(Surface, LeftX + 12.0f, CurY, InnerW - 24.0f, "Compute Backend", DeviceOpts, 3u, ChosenDevice, ChosenDevice, Theme, Input.PrimaryPointer))
            {
                Session->ComputeDevice = static_cast<BakerComputeDevice>(ChosenDevice);
            }
            CurY += 56.0f;

            static constexpr const char* ResOpts[4] { "256", "512", "1024", "2048" };
            static constexpr uint32_t    ResVals[4] { 256u, 512u, 1024u, 2048u };
            uint32_t ActiveResIdx = 1u;
            for (uint32_t I = 0u; I < 4u; ++I)
            {
                if (ResVals[I] == Session->TargetResolutionPx)
                {
                    ActiveResIdx = I;
                }
            }
            uint32_t ChosenResIdx = ActiveResIdx;
            if (ConstructSegmentedCapsuleRow(Surface, LeftX + 12.0f, CurY, InnerW - 24.0f, "Map Resolution", ResOpts, 4u, ActiveResIdx, ChosenResIdx, Theme, Input.PrimaryPointer))
            {
                Session->AssignResolution(ResVals[ChosenResIdx]);
            }
            CurY += 56.0f;

            static constexpr const char* NormOpts[2] { "OpenGL (+Y)", "DirectX (-Y)" };
            uint32_t ChosenNorm = static_cast<uint32_t>(Session->NormalConvention);
            if (ConstructSegmentedCapsuleRow(Surface, LeftX + 12.0f, CurY, InnerW - 24.0f, "Tangent Normal Convention", NormOpts, 2u, ChosenNorm, ChosenNorm, Theme, Input.PrimaryPointer))
            {
                Session->NormalConvention = static_cast<TangentNormalConvention>(ChosenNorm);
            }
            CurY += 56.0f;

            {
                float SamplesFloat = static_cast<float>(Session->AoSampleCount);
                RenderFloatSlider(101, "AO / Bent Samples", 4.0f, 36.0f, "ray", "%.0f", SamplesFloat);
                Session->AoSampleCount = std::clamp(static_cast<int32_t>(std::round(SamplesFloat * 0.5f)) * 2, 4, 36);
            }
            RenderFloatSlider(102, "Front Cage Offset", 0.02f, 0.80f, "m", "%.2f", Session->FrontCageOffsetMeters);
            RenderFloatSlider(103, "Rear Cage Offset", 0.02f, 0.80f, "m", "%.2f", Session->RearCageOffsetMeters);
            RenderFloatSlider(104, "AO Max Ray Distance", 0.10f, 2.00f, "m", "%.2f", Session->AoMaxRayDistanceMeters);
            {
                float PadFloat = static_cast<float>(Session->UvEdgePaddingPx);
                RenderFloatSlider(105, "UV Edge Padding", 0.0f, 14.0f, "px", "%.0f", PadFloat);
                Session->UvEdgePaddingPx = std::clamp(static_cast<int32_t>(std::round(PadFloat)), 0, 14);
            }

            ConstructSwitchControlRow(
                Surface,
                LeftX + 12.0f,
                CurY,
                InnerW - 24.0f,
                "Show High-Poly Wire Overlay",
                Session->ShowHighWireOverlay,
                Theme,
                Input.PrimaryPointer,
                OptionalTooltip::FromTitled("Show High-Poly Wire Overlay", "Overlay source high-resolution silhouette wireframe on the 3D view."));
            CurY += 36.0f;

            ConstructSwitchControlRow(
                Surface,
                LeftX + 12.0f,
                CurY,
                InnerW - 24.0f,
                "Show Translucent Cage",
                Session->ShowCageOverlay,
                Theme,
                Input.PrimaryPointer,
                OptionalTooltip::FromTitled("Show Translucent Cage", "Render the ray projection envelope cage over the 3D low mesh."));
            CurY += 38.0f;
        }
        CurY += 12.0f;
    }

    // 5. Section 2: 3D Volume SDF & Slice Inspector
    {
        const float       SecStartY = CurY;
        const float       SecBodyH  = VolumeSectionOpen ? (40.0f + 56.0f * 2.0f + 52.0f * 2.0f + 36.0f) : 38.0f;
        const Rectangle2D SecRect{ LeftX, SecStartY, InnerW, SecBodyH };
        BeginSectionContainer(Surface, SecRect, Theme, 18.0f);

        if (ConstructSectionHeaderRow(
                Surface,
                { LeftX + 12.0f, CurY + 10.0f, InnerW - 24.0f, 22.0f },
                ControlIcon::VolumeBoxes,
                "3D Volume SDF & Slice Inspector",
                VolumeSectionOpen ? "-" : "+",
                VolumeSectionOpen,
                Theme,
                Cursor,
                Pressed))
        {
            VolumeSectionOpen = !VolumeSectionOpen;
        }
        CurY += 38.0f;

        if (VolumeSectionOpen)
        {
            static constexpr const char* DimOpts[4] { "24³", "36³", "48³", "64³" };
            static constexpr uint32_t    DimVals[4] { 24u, 36u, 48u, 64u };
            uint32_t ActiveDimIdx = 1u;
            for (uint32_t I = 0u; I < 4u; ++I)
            {
                if (DimVals[I] == Session->VolumeGridDim)
                {
                    ActiveDimIdx = I;
                }
            }
            uint32_t ChosenDimIdx = ActiveDimIdx;
            if (ConstructSegmentedCapsuleRow(Surface, LeftX + 12.0f, CurY, InnerW - 24.0f, "Voxel Grid Dim", DimOpts, 4u, ActiveDimIdx, ChosenDimIdx, Theme, Input.PrimaryPointer))
            {
                Session->AssignVolumeGridDim(DimVals[ChosenDimIdx]);
            }
            CurY += 56.0f;

            const bool PrevSigned = Session->SignedInteriorDistance;
            ConstructSwitchControlRow(
                Surface,
                LeftX + 12.0f,
                CurY,
                InnerW - 24.0f,
                "Signed Interior Distance",
                Session->SignedInteriorDistance,
                Theme,
                Input.PrimaryPointer);
            if (Session->SignedInteriorDistance != PrevSigned)
            {
                Session->RefreshSdfSectionOnGpu();
            }
            CurY += 36.0f;

            RenderFloatSlider(201, "Bounds Padding", 0.05f, 0.45f, "", "%.2f", Session->VolumeBoundsPadding);

            static constexpr const char* AxisOpts[3] { "Z Slice", "Y Slice", "X Slice" };
            uint32_t ChosenAxis = static_cast<uint32_t>(Session->SectionAxis);
            if (ConstructSegmentedCapsuleRow(Surface, LeftX + 12.0f, CurY, InnerW - 24.0f, "Inspect Slice Axis", AxisOpts, 3u, ChosenAxis, ChosenAxis, Theme, Input.PrimaryPointer))
            {
                Session->SectionAxis = static_cast<VolumeSectionAxis>(ChosenAxis);
                Session->InspectTarget(BakerTargetId::VolumeSdf);
                Session->RefreshSdfSectionOnGpu();
            }
            CurY += 56.0f;

            const float PrevDepth = Session->SectionDepthNormalized;
            RenderFloatSlider(202, "Slice Depth", 0.0f, 1.0f, "", "%.2f", Session->SectionDepthNormalized);
            if (std::abs(Session->SectionDepthNormalized - PrevDepth) > 0.0005f)
            {
                Session->RefreshSdfSectionOnGpu();
            }
        }
        CurY += 12.0f;
    }

    // 6. Section 3: Export & Texture Paint Bridge
    {
        const float       SecStartY = CurY;
        const float       SecBodyH  = ExportSectionOpen ? 120.0f : 38.0f;
        const Rectangle2D SecRect{ LeftX, SecStartY, InnerW, SecBodyH };
        BeginSectionContainer(Surface, SecRect, Theme, 18.0f);

        if (ConstructSectionHeaderRow(
                Surface,
                { LeftX + 12.0f, CurY + 10.0f, InnerW - 24.0f, 22.0f },
                ControlIcon::DownloadExport,
                "Export & Texture Paint Bridge",
                ExportSectionOpen ? "-" : "+",
                ExportSectionOpen,
                Theme,
                Cursor,
                Pressed))
        {
            ExportSectionOpen = !ExportSectionOpen;
        }
        CurY += 38.0f;

        if (ExportSectionOpen)
        {
            const float HalfBtnW = (InnerW - 24.0f - 8.0f) * 0.5f;
            if (ConstructCapsuleButtonControl(
                    Surface,
                    { LeftX + 12.0f, CurY, HalfBtnW, 28.0f },
                    "Download Current PNG",
                    Theme,
                    Cursor,
                    Pressed,
                    false,
                    false,
                    ControlIcon::DownloadExport,
                    9.5f,
                    14.0f))
            {
                (void)Session->ExportInspectedTargetPng();
            }

            if (ConstructCapsuleButtonControl(
                    Surface,
                    { LeftX + 12.0f + HalfBtnW + 8.0f, CurY, HalfBtnW, 28.0f },
                    "Download .fvol SDF",
                    Theme,
                    Cursor,
                    Pressed && Session->VolumeSdfBaked,
                    Session->VolumeSdfBaked,
                    false,
                    ControlIcon::VolumeBoxes,
                    9.5f,
                    14.0f,
                    OptionalTooltip::FromTitled("Download .fvol SDF", Session->VolumeSdfBaked ? "Export signed distance volume grid as .fvol JSON." : "Bake Volume SDF first.")))
            {
                (void)Session->ExportVolumeSdfFvol();
            }
            CurY += 36.0f;

            if (ConstructCapsuleButtonControl(
                    Surface,
                    { LeftX + 12.0f, CurY, InnerW - 24.0f, 30.0f },
                    "Send Baked AO/Curv/Normals to Texture Paint",
                    Theme,
                    Cursor,
                    Pressed && Session->SurfaceBaked,
                    Session->BridgedToPaint,
                    false,
                    ControlIcon::BrushTool,
                    9.5f,
                    15.0f,
                    OptionalTooltip::FromTitled("Send to Texture Paint", Session->SurfaceBaked ? "Push baked surface maps directly into Texture Paint." : "Bake surface maps first.")))
            {
                Session->SendBakedTargetsToPaint();
            }
            CurY += 42.0f;
        }
        CurY += 12.0f;
    }

    const float ContentBottom = CurY + ScrollOffsetY + 12.0f;
    MaxScrollY                = std::max(0.0f, ContentBottom - ScrollViewport.Bottom());
    ScrollOffsetY             = std::clamp(ScrollOffsetY, 0.0f, MaxScrollY);

    Surface.PopClip();
    Surface.PopClip();
}

} // namespace Frontier::Workspaces
