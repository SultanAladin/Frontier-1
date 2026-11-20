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

    // Header: Project Zero "Map Baking  HIGH -> LOW CAGE" + Preset pill + Refresh (cards-bake.png parity)
    constexpr float PadX       = 14.0f;
    constexpr float HeaderTopY = 12.0f;
    const float     InnerW     = std::max(80.0f, Bounds.Width - PadX * 2.0f);
    const float     Hx         = Bounds.X + PadX;
    const float     Hy         = Bounds.Y + HeaderTopY;

    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::BakeBox, VectorIconPlacement{ Hx, Hy + 3.0f, 13.5f, ColorRgba{ 232u, 194u, 122u, 255u }, 1.7f });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { Hx + 19.0f, Hy + 1.0f }, 10.8f, Theme.TextPrimary, "Map Baking");
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Bold, { Hx + 88.0f, Hy + 3.0f }, 7.8f, ColorRgba{ 232u, 194u, 122u, 255u }, "HIGH -> LOW CAGE");

    const BakerStudyDescriptor& StudyDesc = Session->QueryActiveStudyDescriptor();
    const char* StudyTitle = !Session->CustomLowAssetLabel.empty() ? Session->CustomLowAssetLabel.c_str() : StudyDesc.Label;
    const Rectangle2D RefreshBtnR{ Bounds.Right() - PadX - 24.0f, Hy - 2.0f, 24.0f, 24.0f };
    const Rectangle2D PresetPillR{ std::max(Hx + 186.0f, RefreshBtnR.X - 112.0f), Hy - 2.0f, 108.0f, 24.0f };
    Surface.FillRectangle(PresetPillR, PresetPillR.Contains(Cursor) ? Theme.HoverSurface : Theme.SectionSurface, 7.0f);
    Surface.StrokeRectangle(PresetPillR, Theme.BorderSubtle, 7.0f, 1.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Medium, { PresetPillR.X + 8.0f, PresetPillR.Y + 5.0f }, PresetPillR.Width - 22.0f, 8.4f, Theme.TextPrimary, StudyTitle);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::ChevronDown, VectorIconPlacement{ PresetPillR.Right() - 14.0f, PresetPillR.Y + 7.5f, 9.0f, Theme.TextMuted, 1.6f });
    if (Pressed && PresetPillR.Contains(Cursor))
    {
        const uint32_t NextStudy = (static_cast<uint32_t>(Session->Study) + 1u) % 4u;
        Session->SelectStudy(static_cast<BakerStudyShape>(NextStudy));
    }

    Surface.FillRectangle(RefreshBtnR, RefreshBtnR.Contains(Cursor) ? Theme.HoverSurface : Theme.SectionSurface, 6.0f);
    Surface.StrokeRectangle(RefreshBtnR, Theme.BorderSubtle, 6.0f, 1.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::RefreshCycle, VectorIconPlacement{ RefreshBtnR.X + 6.0f, RefreshBtnR.Y + 6.0f, 12.0f, Theme.TextMuted, 1.65f });
    if (Pressed && RefreshBtnR.Contains(Cursor))
    {
        Session->ApplyQuickPreset(BakerQuickPreset::PbrPlusVolume);
    }

    Surface.StrokeLine({ Bounds.X, Hy + 26.0f }, { Bounds.Right(), Hy + 26.0f }, Theme.BorderSubtle, 1.0f);
    const float HeaderH = 30.0f;

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

    // 1. MESH PAIR Section ("Utah Teapot · 544 LOW · 8,704 HIGH TRIS" + Low -> High mesh cards + Bake 5 maps + Auto|GPU|CPU)
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Bold, { LeftX, CurY }, 8.2f, Theme.TextFaint, "MESH PAIR");
    CurY += 15.0f;

    char TriMeta[80];
    std::snprintf(
        TriMeta,
        sizeof(TriMeta),
        "%u LOW · %u HIGH TRIS · %uX%u",
        Session->QueryActiveLowTriangles(),
        Session->QueryActiveHighTriangles(),
        Session->TargetResolutionPx,
        Session->TargetResolutionPx);
    CurY += ConstructSubjectHeadingBlock(Surface, LeftX, CurY, InnerW, ControlIcon::BakeBox, StudyTitle, TriMeta, Theme) + 8.0f;

    // Side-by-side Low -> High Mesh Cards (cards-bake.png parity)
    const float MeshCardW = (InnerW - 22.0f) * 0.5f;
    const Rectangle2D LowCardR { LeftX, CurY, MeshCardW, 38.0f };
    const Rectangle2D HighCardR{ LeftX + MeshCardW + 22.0f, CurY, MeshCardW, 38.0f };
    Surface.FillRectangle(LowCardR, Theme.SectionSurface, 8.0f);
    Surface.StrokeRectangle(LowCardR, Theme.BorderSubtle, 8.0f, 1.0f);
    Surface.FillRectangle(HighCardR, Theme.SectionSurface, 8.0f);
    Surface.StrokeRectangle(HighCardR, Theme.BorderSubtle, 8.0f, 1.0f);

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Bold, { LowCardR.X + 8.0f, LowCardR.Y + 5.0f }, 7.4f, Theme.TextFaint, "LOW");
    char LowSub[48];
    std::snprintf(LowSub, sizeof(LowSub), "Low Mesh · %u tris", Session->QueryActiveLowTriangles());
    WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium, { LowCardR.X + 8.0f, LowCardR.Y + 19.0f }, MeshCardW - 14.0f, 8.6f, Theme.TextPrimary, LowSub);

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Bold, { LowCardR.Right() + 5.0f, CurY + 12.0f }, 9.5f, Theme.TextFaint, "->");

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Bold, { HighCardR.X + 8.0f, HighCardR.Y + 5.0f }, 7.4f, Theme.TextFaint, "HIGH");
    char HighSub[48];
    std::snprintf(HighSub, sizeof(HighSub), "High Mesh · %u tris", Session->QueryActiveHighTriangles());
    WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium, { HighCardR.X + 8.0f, HighCardR.Y + 19.0f }, MeshCardW - 14.0f, 8.6f, Theme.TextPrimary, HighSub);
    CurY += 46.0f;

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
    CurY += ConstructMetricTrioTiles(Surface, LeftX, CurY, InnerW, Kpis, Theme) + 10.0f;

    // 3. Bake Progress & Action Row ("|> Bake N maps" + "Auto | GPU | CPU" + telemetry)
    {
        const Rectangle2D ProgBox{ LeftX, CurY, InnerW, 96.0f };
        BeginSectionContainer(Surface, ProgBox, Theme, 14.0f);

        char BakeBtnLabel[48];
        std::snprintf(BakeBtnLabel, sizeof(BakeBtnLabel), Session->BakingActive ? "Baking..." : "Bake %zu maps", Session->CountSelectedTargets());
        const float BakeBtnW = (InnerW - 24.0f) * 0.50f;
        const Rectangle2D BakeBtnRect{ LeftX + 10.0f, CurY + 10.0f, BakeBtnW, 28.0f };
        if (ConstructCapsuleButtonControl(
                Surface,
                BakeBtnRect,
                BakeBtnLabel,
                Theme,
                Cursor,
                Pressed,
                false,
                true,
                ControlIcon::BakeBox,
                9.8f,
                13.0f,
                OptionalTooltip::FromTitled("Bake Selected Maps", "Compute all checked surface and volume SDF output targets.")))
        {
            Session->ExecuteBakeSimulation();
        }

        // Inline "Auto | GPU | CPU" pills on the right of the Bake button (cards-bake.png parity)
        static constexpr const char* ShortDev[3] { "Auto", "GPU", "CPU" };
        const float DevTotalW = InnerW - 20.0f - BakeBtnW - 6.0f;
        const float DevPillW  = (DevTotalW - 4.0f) / 3.0f;
        for (uint32_t Di = 0u; Di < 3u; ++Di)
        {
            const Rectangle2D DevR{ BakeBtnRect.Right() + 6.0f + static_cast<float>(Di) * (DevPillW + 2.0f), CurY + 10.0f, DevPillW, 28.0f };
            const bool DevAct = (static_cast<uint32_t>(Session->ComputeDevice) == Di);
            const bool DevHov = DevR.Contains(Cursor);
            Surface.FillRectangle(DevR, DevAct ? ColorRgba{ 238u, 238u, 236u, 255u } : (DevHov ? Theme.HoverSurface : Theme.WellSurface), 7.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, DevAct ? FontWeightClass::SemiBold : FontWeightClass::Medium, { DevR.X + 7.0f, DevR.Y + 7.5f }, 8.2f,
                DevAct ? ColorRgba{ 16u, 17u, 20u, 255u } : Theme.TextMuted, ShortDev[Di]);
            if (Pressed && DevHov)
            {
                Session->ComputeDevice = static_cast<BakerComputeDevice>(Di);
            }
        }

        const Rectangle2D TrackRect{ LeftX + 10.0f, CurY + 46.0f, InnerW - 20.0f, 5.0f };
        Surface.FillRectangle(TrackRect, Theme.WellSurface, 2.5f);
        const float ClampedProg = std::clamp(Session->BakeProgress, 0.0f, 1.0f);
        if (ClampedProg > 0.005f)
        {
            Surface.FillRectangle({ TrackRect.X, TrackRect.Y, std::max(6.0f, TrackRect.Width * ClampedProg), TrackRect.Height }, Theme.HighlightFill, 2.5f);
        }

        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + 10.0f, CurY + 57.0f }, InnerW - 20.0f, 8.6f, Theme.TextMuted, Session->ProgressStatus);

        char TeleBuf[64];
        std::snprintf(TeleBuf, sizeof(TeleBuf), "Last bake 48 ms · %zu maps   %ux%u · 4x AA",
            Session->CountSelectedTargets(), Session->TargetResolutionPx, Session->TargetResolutionPx);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Medium, { LeftX + 10.0f, CurY + 74.0f }, InnerW - 20.0f, 8.0f, Theme.TextFaint, TeleBuf);

        CurY += ProgBox.Height + 10.0f;
    }

    // 3B. Open Inspected Map Card (cards-bake.png parity: "Tangent Normal  OpenGL +Y | DirectX -Y" + side-by-side Cage front & Cage rear)
    {
        const Rectangle2D OpenMapR{ LeftX, CurY, InnerW, 114.0f };
        BeginSectionContainer(Surface, OpenMapR, Theme, 14.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Bold, { LeftX + 12.0f, CurY + 8.0f }, 8.0f, Theme.TextFaint, "OPEN MAP · GEOMETRY");
        char MapCntBuf[24];
        std::snprintf(MapCntBuf, sizeof(MapCntBuf), "%zu/18 on", Session->CountSelectedTargets());
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { OpenMapR.Right() - 56.0f, CurY + 8.0f }, 8.0f, Theme.TextFaint, MapCntBuf);

        const char* InspectedTitle = BakingWorkspaceSession::QueryTargetDescriptor(Session->InspectedTarget).Label;
        Surface.FillCircle({ LeftX + 18.0f, CurY + 31.0f }, 5.0f, ColorRgba{ 128u, 134u, 255u, 255u });
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { LeftX + 28.0f, CurY + 24.0f }, 10.0f, Theme.TextPrimary, InspectedTitle);

        static constexpr const char* NormShort[2] { "OpenGL +Y", "DirectX -Y" };
        for (uint32_t Ni = 0u; Ni < 2u; ++Ni)
        {
            const Rectangle2D NormR{ OpenMapR.Right() - 144.0f + static_cast<float>(Ni) * 68.0f, CurY + 21.0f, 64.0f, 20.0f };
            const bool NAct = (static_cast<uint32_t>(Session->NormalConvention) == Ni);
            Surface.FillRectangle(NormR, NAct ? ColorRgba{ 238u, 238u, 236u, 255u } : Theme.WellSurface, 5.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, NAct ? FontWeightClass::SemiBold : FontWeightClass::Medium, { NormR.X + 7.0f, NormR.Y + 3.5f }, 7.8f,
                NAct ? ColorRgba{ 16u, 17u, 20u, 255u } : Theme.TextMuted, NormShort[Ni]);
            if (Pressed && NormR.Contains(Cursor))
            {
                Session->NormalConvention = static_cast<TangentNormalConvention>(Ni);
            }
        }

        // Side-by-side Cage front & Cage rear readouts
        const float HalfCageW = (InnerW - 24.0f - 8.0f) * 0.5f;
        bool DragCf = (ActiveSliderId == 191);
        float CfNorm = std::clamp((Session->FrontCageOffsetMeters - 0.02f) / 0.78f, 0.0f, 1.0f);
        char CfBuf[24];
        std::snprintf(CfBuf, sizeof(CfBuf), "%.2f", Session->FrontCageOffsetMeters);
        if (ConstructSliderReadoutRow(Surface, LeftX + 12.0f, CurY + 52.0f, HalfCageW, "Cage front", CfBuf, "m", CfNorm, Theme, Input.PrimaryPointer, DragCf))
        {
            Session->FrontCageOffsetMeters = 0.02f + CfNorm * 0.78f;
        }
        if (DragCf) ActiveSliderId = 191;

        bool DragCr = (ActiveSliderId == 192);
        float CrNorm = std::clamp((Session->RearCageOffsetMeters - 0.02f) / 0.78f, 0.0f, 1.0f);
        char CrBuf[24];
        std::snprintf(CrBuf, sizeof(CrBuf), "%.2f", Session->RearCageOffsetMeters);
        if (ConstructSliderReadoutRow(Surface, LeftX + 12.0f + HalfCageW + 8.0f, CurY + 52.0f, HalfCageW, "Cage rear", CrBuf, "m", CrNorm, Theme, Input.PrimaryPointer, DragCr))
        {
            Session->RearCageOffsetMeters = 0.02f + CrNorm * 0.78f;
        }
        if (DragCr) ActiveSliderId = 192;

        CurY += OpenMapR.Height + 10.0f;
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
