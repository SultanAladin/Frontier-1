/*============================================================================================================================================
                                                 DETERMINISTICPATHTRACERPANE.CPP
============================================================================================================================================*/
// 🧩 Dark studio viewport + 330 px inspector pane for Order 3 Unified Deterministic Path Tracer (GameRealtime & SceneFinalRender)

#include "Engine/Components/Panes/DeterministicPathTracerPane.h"

#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/ScalarSlider.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{

namespace
{

// Exact studio tokens matching Experimental/DeterministicPathTracer/Styles.css
constexpr ColorRgba StudioCanvasBg       {  11u,  12u,  14u, 255u }; // #0b0c0e
constexpr ColorRgba StudioPanelBg        {  19u,  21u,  25u, 255u }; // #131519
constexpr ColorRgba StudioControlBg      {  26u,  30u,  37u, 255u }; // #1a1e25
constexpr ColorRgba StudioControlHover   {  34u,  39u,  49u, 255u }; // #222731
constexpr ColorRgba StudioBorderLine     {  34u,  39u,  49u, 255u }; // #222731
constexpr ColorRgba StudioTextPrimary    { 228u, 231u, 236u, 255u }; // #e4e7ec
constexpr ColorRgba StudioTextMuted      { 133u, 140u, 153u, 255u }; // #858c99
constexpr ColorRgba StudioTextFaint      {  85u,  92u, 104u, 255u }; // #555c68
constexpr ColorRgba StudioAccentLime     { 199u, 240u,  58u, 255u }; // #c7f03a
constexpr ColorRgba StudioAccentSoftFill { 199u, 240u,  58u,  30u }; // #c7f03a1c
constexpr ColorRgba StudioOverlayGlass     {  13u,  15u,  19u, 224u }; // #0d0f13d6
constexpr ColorRgba StudioOverlayBorder    { 255u, 255u, 255u,  26u }; // #ffffff18
constexpr ColorRgba StudioVerifiedGreen      { 109u, 224u, 136u, 255u }; // #6de088
constexpr ColorRgba StudioFailRed        { 240u, 112u, 103u, 255u }; // #f07067

[[nodiscard]] bool ConstructStudioButton(
    PixelSpace&           Surface,
    const Rectangle2D     Bounds,
    const char*           Label,
    const bool            Active,
    const ControlPointer& Pointer,
    const float           FontSize = 11.5f) noexcept
{
    const bool Hovered = Pointer.Enabled && Bounds.Contains({ Pointer.X, Pointer.Y });
    const bool Clicked = Hovered && Pointer.Pressed;

    const ColorRgba Fill   = Active ? StudioAccentSoftFill : (Hovered ? StudioControlHover : StudioControlBg);
    const ColorRgba Border = Active ? StudioAccentLime     : (Hovered ? ColorRgba { 52u, 60u, 74u, 255u } : StudioBorderLine);
    const ColorRgba Ink    = Active ? StudioAccentLime     : (Hovered ? StudioTextPrimary : ColorRgba { 196u, 201u, 210u, 255u });

    Surface.FillRectangle(Bounds, Fill, 4.0f);
    Surface.StrokeRectangle(Bounds, Border, 4.0f, 1.0f);
    Surface.TextCentered(Bounds, FontSize, Ink, Label);
    return Clicked;
}

float ConstructSectionHeading(
    PixelSpace& Surface,
    const float LeftX,
    const float TopY,
    const float InnerW,
    const char* Title,
    const bool  ConstructTopHairline = true) noexcept
{
    float Y = TopY;
    if (ConstructTopHairline)
    {
        Surface.StrokeLine({ LeftX - 16.0f, Y }, { LeftX + InnerW + 16.0f, Y }, StudioBorderLine, 1.0f);
        Y += 10.0f;
    }
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::SemiBold,
        { LeftX, Y },
        10.5f,
        StudioTextFaint,
        Title);
    return Y + 18.0f;
}

[[nodiscard]] bool ConstructLabeledSliderRow(
    PixelSpace&           Surface,
    const float           LeftX,
    float&                InOutY,
    const float           InnerW,
    const char*           Label,
    const char*           ReadingText,
    const float           Minimum,
    const float           Maximum,
    const float           CurrentReading,
    const int32_t         SliderId,
    int32_t&              ActiveSliderId,
    const ControlPointer& Pointer,
    float&                OutReading) noexcept
{
    constexpr float LabelW   = 84.0f;
    constexpr float ReadoutW = 48.0f;
    const float     TrackX   = LeftX + LabelW;
    const float     TrackW   = std::max(40.0f, InnerW - LabelW - ReadoutW - 8.0f);
    const Rectangle2D RowRect   { LeftX, InOutY, InnerW, 20.0f };
    const Rectangle2D TrackRect { TrackX, InOutY + 2.0f, TrackW, 16.0f };
    const Rectangle2D ValRect   { LeftX + InnerW - ReadoutW, InOutY, ReadoutW, 20.0f };

    Surface.Text({ LeftX, InOutY + 3.0f }, 11.5f, StudioTextMuted, Label);
    Surface.TextRightAligned(ValRect, 11.0f, StudioTextMuted, ReadingText);

    const bool Dragging = (ActiveSliderId == SliderId);
    float      Updated  = CurrentReading;
    const ControlContact Contact = ScalarSlider::Construct(
        Surface,
        TrackRect,
        Minimum,
        Maximum,
        CurrentReading,
        Dragging,
        Pointer,
        Updated,
        1.0f,
        3.0f,
        11.0f);

    if (Contact.Pressed)
    {
        ActiveSliderId = SliderId;
    }
    if (!Pointer.Down && ActiveSliderId == SliderId)
    {
        ActiveSliderId = -1;
    }

    InOutY = RowRect.Bottom() + 5.0f;
    OutReading = Updated;
    return (Contact.Dragging || Contact.Pressed) && (std::abs(Updated - CurrentReading) > 1.0e-6f);
}

void FormatIntegerWithCommas(const uint32_t ScalarReading, char* OutBuf, const std::size_t Cap) noexcept
{
    char Raw[32] {};
    std::snprintf(Raw, sizeof(Raw), "%u", ScalarReading);
    const std::size_t Len = std::strlen(Raw);
    std::size_t       W   = 0u;
    for (std::size_t I = 0u; I < Len && W + 1u < Cap; ++I)
    {
        if (I > 0u && ((Len - I) % 3u == 0u) && W + 1u < Cap)
        {
            OutBuf[W++] = ',';
        }
        OutBuf[W++] = Raw[I];
    }
    OutBuf[W] = '\0';
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              LIFECYCLE & TICK STEPPING
//------------------------------------------------------------------------------------------------------------------------

DeterministicPathTracerPane::DeterministicPathTracerPane()
    : ViewportTexture(std::make_unique<ImTextureData>())
{
}

DeterministicPathTracerPane::~DeterministicPathTracerPane()
{
    if (ViewportTexture != nullptr)
    {
        ViewportTexture->DestroyPixels();
    }
}

void DeterministicPathTracerPane::AttachSequence(GeometricRaster::DeterministicPathTraceSequence* const ActiveSequence) noexcept
{
    TracerSequence    = ActiveSequence;
    LastDigestText    = "—";
    RepeatVerdictText.clear();
}

void DeterministicPathTracerPane::SyncViewportTexture()
{
    if (TracerSequence == nullptr || !TracerSequence->Ready())
    {
        return;
    }
    if (!TracerSequence->ResolveDisplayRgba8(DisplayPixels))
    {
        return;
    }
    const uint32_t W = TracerSequence->QueryWidth();
    const uint32_t H = TracerSequence->QueryHeight();
    if (W == 0u || H == 0u || DisplayPixels.size() < static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 4u)
    {
        return;
    }

    if (ViewportTexture->Pixels == nullptr || TextureWidthPx != W || TextureHeightPx != H)
    {
        ViewportTexture->DestroyPixels();
        ViewportTexture->SetStatus(ImTextureStatus_Destroyed);
        ViewportTexture->Create(ImTextureFormat_RGBA32, static_cast<int>(W), static_cast<int>(H));
        TextureWidthPx  = W;
        TextureHeightPx = H;
    }
    std::memcpy(ViewportTexture->GetPixels(), DisplayPixels.data(), DisplayPixels.size());
    ViewportTexture->SetStatus(ImTextureStatus_WantUpdates);
}

void DeterministicPathTracerPane::StepIntegrationTick(
    const Rectangle2D Bounds,
    const double      DeltaSeconds,
    const uint32_t    SamplesOverride)
{
    if (TracerSequence == nullptr || !TracerSequence->Ready())
    {
        return;
    }

    const auto&    Cfg        = TracerSequence->QueryConfiguration();
    const float    ViewW      = std::max(64.0f, Bounds.Width - InspectorWidthPx);
    const float    ViewH      = std::max(64.0f, Bounds.Height);
    const uint32_t TargetW    = std::max(64u, static_cast<uint32_t>(std::lround(ViewW * Cfg.RenderScale)));
    const uint32_t TargetH    = std::max(64u, static_cast<uint32_t>(std::lround(ViewH * Cfg.RenderScale)));

    if (TracerSequence->QueryWidth() != TargetW || TracerSequence->QueryHeight() != TargetH)
    {
        (void)TracerSequence->ResizeAccumulation(TargetW, TargetH);
    }

    const uint32_t CurrentSamples = TracerSequence->QuerySampleIndexCount();
    if (!IntegrationPaused && CurrentSamples < Cfg.TargetSamples)
    {
        const uint32_t Remaining = Cfg.TargetSamples - CurrentSamples;
        const uint32_t Batch     = std::min(Remaining, (SamplesOverride > 0u) ? SamplesOverride : std::max(1u, Cfg.SampleBatch));
        if (TracerSequence->AdvanceSamples(Batch))
        {
            const double InstantRate = (DeltaSeconds > 1.0e-5) ? (static_cast<double>(Batch) / DeltaSeconds) : 0.0;
            SmoothedRate             = (SmoothedRate <= 0.0) ? InstantRate : (0.82 * SmoothedRate + 0.18 * InstantRate);
        }
        if (TracerSequence->QuerySampleIndexCount() >= Cfg.TargetSamples)
        {
            LastDigestText = TracerSequence->ComputeAccumulationDigest();
        }
    }

    SyncViewportTexture();
}

void DeterministicPathTracerPane::TriggerBitExactRepeatVerification(
    const uint32_t SampleTotal,
    const uint32_t AlternateBatch)
{
    if (TracerSequence == nullptr || !TracerSequence->Ready())
    {
        return;
    }
    std::string FirstDigest {};
    std::string RepeatDigest {};
    RepeatPassed   = TracerSequence->VerifyBitExactRepeat(SampleTotal, AlternateBatch, &FirstDigest, &RepeatDigest);
    LastDigestText = RepeatDigest;
    RepeatVerdictText = RepeatPassed
        ? ("Bit-exact match (" + FirstDigest + ") at batch 1 vs " + std::to_string(AlternateBatch))
        : ("Mismatch: " + FirstDigest + " vs " + RepeatDigest);
    SyncViewportTexture();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANE RENDERING
//------------------------------------------------------------------------------------------------------------------------

void DeterministicPathTracerPane::Render(
    PixelSpace&             Surface,
    const Rectangle2D       Bounds,
    const PaneInputReading& Input,
    const double            DeltaSeconds)
{
    (void)DeltaSeconds;
    const ControlPointer& Ptr = Input.PrimaryPointer;

    const float       ViewW        = std::max(120.0f, Bounds.Width - InspectorWidthPx);
    const Rectangle2D ViewportRect { Bounds.X, Bounds.Y, ViewW, Bounds.Height };
    const Rectangle2D PanelRect    { Bounds.X + ViewW, Bounds.Y, InspectorWidthPx, Bounds.Height };

    // 1. Viewport background & live path-traced image
    Surface.FillRectangle(ViewportRect, StudioCanvasBg);
    if (ViewportTexture != nullptr && ViewportTexture->Pixels != nullptr)
    {
        Surface.FillTexturedRectangle(ViewportRect, ViewportTexture.get(), { 0.0f, 0.0f }, { 1.0f, 1.0f });
    }
    else if (TracerSequence != nullptr && TracerSequence->Ready() && DisplayPixels.empty())
    {
        SyncViewportTexture();
        if (ViewportTexture->Pixels != nullptr)
        {
            Surface.FillTexturedRectangle(ViewportRect, ViewportTexture.get(), { 0.0f, 0.0f }, { 1.0f, 1.0f });
        }
    }

    // Orbit camera drag inside viewport
    const Point2D Cursor { Ptr.X, Ptr.Y };
    if (TracerSequence != nullptr && ViewportRect.Contains(Cursor) && ActiveSliderId < 0 && OpenDropdownId < 0)
    {
        if (Ptr.Pressed)
        {
            OrbitDragging   = true;
            PreviousPointer = Cursor;
        }
        if (std::abs(Input.WheelDeltaY) > 1.0e-4f)
        {
            auto Cam           = TracerSequence->QueryConfiguration().Camera;
            Cam.DistanceMeters = std::clamp(Cam.DistanceMeters * std::pow(0.90f, Input.WheelDeltaY), 1.4f, 95.0f);
            TracerSequence->UpdateCamera(Cam, true);
            SyncViewportTexture();
        }
    }
    if (OrbitDragging)
    {
        if (Ptr.Down && TracerSequence != nullptr)
        {
            const float Dx = Cursor.X - PreviousPointer.X;
            const float Dy = Cursor.Y - PreviousPointer.Y;
            if (std::abs(Dx) > 0.25f || std::abs(Dy) > 0.25f)
            {
                auto Cam         = TracerSequence->QueryConfiguration().Camera;
                Cam.YawRadians  -= Dx * 0.0055f;
                Cam.PitchRadians = std::clamp(Cam.PitchRadians + Dy * 0.0055f, -0.05f, 1.48f);
                TracerSequence->UpdateCamera(Cam, true);
                PreviousPointer = Cursor;
                SyncViewportTexture();
            }
        }
        else
        {
            OrbitDragging = false;
        }
    }

    // 2. Top-left overlay status selector + top-right profile selector + bottom 2 px #c7f03a convergence bar
    if (TracerSequence != nullptr)
    {
        const auto&    Cfg        = TracerSequence->QueryConfiguration();
        const uint32_t Spp        = TracerSequence->QuerySampleIndexCount();
        const uint32_t TargetSpp  = std::max(1u, Cfg.TargetSamples);
        const bool     Converged  = (Spp >= TargetSpp);
        const float    Progress   = std::clamp(static_cast<float>(Spp) / static_cast<float>(TargetSpp), 0.0f, 1.0f);

        char SppBuf[32] {}, TargetBuf[32] {}, OverlayText[128] {};
        FormatIntegerWithCommas(Spp, SppBuf, sizeof(SppBuf));
        FormatIntegerWithCommas(TargetSpp, TargetBuf, sizeof(TargetBuf));
        const double RateDisplay = (SmoothedRate > 0.1)
            ? SmoothedRate
            : ((TracerSequence->QuerySequenceTimings().LastDispatchMillis > 0.01) ? (1000.0 / TracerSequence->QuerySequenceTimings().LastDispatchMillis) : 0.0);
        std::snprintf(
            OverlayText,
            sizeof(OverlayText),
            "%s / %s spp   ·   %u × %u   ·   %.0f spp/s",
            SppBuf,
            TargetBuf,
            TracerSequence->QueryWidth(),
            TracerSequence->QueryHeight(),
            RateDisplay);

        const float       TextW     = Surface.MeasureTextWidth(11.5f, OverlayText);
        const Rectangle2D OverlayRect { ViewportRect.X + 14.0f, ViewportRect.Y + 14.0f, TextW + 34.0f, 26.0f };
        Surface.FillRectangle(OverlayRect, StudioOverlayGlass, 6.0f);
        Surface.StrokeRectangle(OverlayRect, StudioOverlayBorder, 6.0f, 1.0f);

        const ColorRgba DotColor = IntegrationPaused ? StudioTextFaint : StudioAccentLime;
        Surface.FillCircle({ OverlayRect.X + 13.0f, OverlayRect.Center().Y }, 3.5f, DotColor);
        Surface.Text({ OverlayRect.X + 23.0f, OverlayRect.Y + 6.5f }, 11.5f, StudioTextPrimary, OverlayText);

        const char* ProfileLabel =
            (Cfg.Profile == GeometricRaster::DeterministicRenderProfile::GameRealtime)
                ? "GameRealtime  ·  Blue-Noise + À-Trous"
                : ((Cfg.Profile == GeometricRaster::DeterministicRenderProfile::SceneFinalRender)
                       ? "SceneFinalRender  ·  Shared (0, 2) Exact"
                       : (Converged ? "Converged  ·  Custom Profile" : "Custom Profile"));
        const float       ProfW    = Surface.MeasureTextWidth(11.0f, ProfileLabel);
        const Rectangle2D ProfRect { ViewportRect.Right() - ProfW - 36.0f, ViewportRect.Y + 14.0f, ProfW + 22.0f, 26.0f };
        Surface.FillRectangle(ProfRect, StudioOverlayGlass, 6.0f);
        Surface.StrokeRectangle(ProfRect, StudioOverlayBorder, 6.0f, 1.0f);
        Surface.TextCentered(ProfRect, 11.0f, StudioAccentLime, ProfileLabel);

        // 2 px bottom convergence bar (#c7f03a)
        const Rectangle2D TrackBar { ViewportRect.X, ViewportRect.Bottom() - 2.0f, ViewportRect.Width, 2.0f };
        Surface.FillRectangle(TrackBar, ColorRgba { 255u, 255u, 255u, 16u });
        if (Progress > 0.0f)
        {
            Surface.FillRectangle({ TrackBar.X, TrackBar.Y, TrackBar.Width * Progress, 2.0f }, StudioAccentLime);
        }
    }

    // 3. Right-hand 330 px dark studio inspector (#131519)
    Surface.FillRectangle(PanelRect, StudioPanelBg);
    Surface.StrokeLine({ PanelRect.X, PanelRect.Y }, { PanelRect.X, PanelRect.Bottom() }, StudioBorderLine, 1.0f);

    const float LeftX  = PanelRect.X + 16.0f;
    const float InnerW = PanelRect.Width - 32.0f;

    // Header block
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::SemiBold,
        { LeftX, PanelRect.Y + 14.0f },
        13.5f,
        StudioTextPrimary,
        "Deterministic Path Tracer");
    Surface.Text(
        { LeftX, PanelRect.Y + 32.0f },
        11.0f,
        StudioTextMuted,
        "Scrambled Sobol' (0, 2) · OpenPBR · Vulkan 1.3");
    Surface.StrokeLine({ PanelRect.X, PanelRect.Y + 52.0f }, { PanelRect.Right(), PanelRect.Y + 52.0f }, StudioBorderLine, 1.0f);

    if (TracerSequence == nullptr || !TracerSequence->Ready())
    {
        Surface.Text({ LeftX, PanelRect.Y + 70.0f }, 12.0f, StudioTextMuted, "No active DeterministicPathTraceSequence.");
        return;
    }

    const Rectangle2D BodyClip { PanelRect.X + 1.0f, PanelRect.Y + 53.0f, PanelRect.Width - 1.0f, PanelRect.Height - 53.0f };
    if (BodyClip.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        InspectorScrollY = std::clamp(InspectorScrollY - Input.WheelDeltaY * 28.0f, 0.0f, 460.0f);
    }

    Surface.PushClip(BodyClip);
    float Y = BodyClip.Y + 10.0f - InspectorScrollY;

    auto        Cfg                 = TracerSequence->QueryConfiguration();
    bool        RebuildScene        = false;
    bool        ResetSamples        = false;
    bool        CameraChanged       = false;
    bool        RefreshDisplay      = false;
    Rectangle2D PaintDropdownBounds {};
    Rectangle2D FloorDropdownBounds {};
    Rectangle2D SkyDropdownBounds   {};

    // Profile selector (SceneFinalRender vs GameRealtime)
    {
        const float HalfW = (InnerW - 4.0f) * 0.5f;
        if (ConstructStudioButton(Surface, { LeftX, Y, HalfW, 24.0f }, "SceneFinalRender",
                             Cfg.Profile == GeometricRaster::DeterministicRenderProfile::SceneFinalRender, Ptr, 11.0f))
        {
            GeometricRaster::DeterministicPathTraceSequence::ApplyRenderProfile(
                GeometricRaster::DeterministicRenderProfile::SceneFinalRender, Cfg);
            ResetSamples   = true;
            RefreshDisplay = true;
        }
        if (ConstructStudioButton(Surface, { LeftX + HalfW + 4.0f, Y, HalfW, 24.0f }, "GameRealtime",
                             Cfg.Profile == GeometricRaster::DeterministicRenderProfile::GameRealtime, Ptr, 11.0f))
        {
            GeometricRaster::DeterministicPathTraceSequence::ApplyRenderProfile(
                GeometricRaster::DeterministicRenderProfile::GameRealtime, Cfg);
            ResetSamples   = true;
            RefreshDisplay = true;
        }
        Y += 32.0f;
    }

    // ---- VIEW SECTION ----
    Y = ConstructSectionHeading(Surface, LeftX, Y, InnerW, "VIEW", true);
    {
        constexpr std::array<const char*, 6> PresetLabels { "Hero", "Profile", "Front", "Spheres", "Rear", "Fleet" };
        constexpr std::array<GeometricRaster::StudioCameraPreset, 6> Presets {
            GeometricRaster::StudioCameraPreset::Hero,
            GeometricRaster::StudioCameraPreset::Profile,
            GeometricRaster::StudioCameraPreset::Front,
            GeometricRaster::StudioCameraPreset::Spheres,
            GeometricRaster::StudioCameraPreset::Rear,
            GeometricRaster::StudioCameraPreset::Fleet,
        };
        const float CellW = (InnerW - 8.0f) / 3.0f;
        for (std::size_t I = 0u; I < 6u; ++I)
        {
            const float Cx = LeftX + static_cast<float>(I % 3u) * (CellW + 4.0f);
            const float Cy = Y + static_cast<float>(I / 3u) * 26.0f;
            if (ConstructStudioButton(Surface, { Cx, Cy, CellW, 22.0f }, PresetLabels[I], Cfg.CameraPreset == Presets[I], Ptr))
            {
                Cfg.CameraPreset = Presets[I];
                Cfg.Camera       = GeometricRaster::DeterministicPathTraceSequence::QueryCameraPreset(Presets[I]);
                CameraChanged    = true;
            }
        }
        Y += 56.0f;

        char ValBuf[32] {};
        std::snprintf(ValBuf, sizeof(ValBuf), "%.0f°", Cfg.Camera.FieldOfViewDegrees);
        float NextFov = Cfg.Camera.FieldOfViewDegrees;
        if (ConstructLabeledSliderRow(Surface, LeftX, Y, InnerW, "Field of View", ValBuf, 18.0f, 75.0f, Cfg.Camera.FieldOfViewDegrees, 1, ActiveSliderId, Ptr, NextFov))
        {
            Cfg.Camera.FieldOfViewDegrees = NextFov;
            CameraChanged                 = true;
        }

        std::snprintf(ValBuf, sizeof(ValBuf), "%.3f m", Cfg.ApertureRadius);
        float NextAperture = Cfg.ApertureRadius;
        if (ConstructLabeledSliderRow(Surface, LeftX, Y, InnerW, "Aperture", ValBuf, 0.0f, 0.18f, Cfg.ApertureRadius, 2, ActiveSliderId, Ptr, NextAperture))
        {
            Cfg.ApertureRadius = NextAperture;
            Cfg.Profile        = GeometricRaster::DeterministicRenderProfile::Custom;
            ResetSamples       = true;
        }

        // Render Scale & Target Spp strips
        Surface.Text({ LeftX, Y + 4.0f }, 11.5f, StudioTextMuted, "Render Scale");
        constexpr std::array<const char*, 3> ScaleLabels { "50%", "75%", "100%" };
        constexpr std::array<float, 3>       ScaleVals   { 0.50f, 0.75f, 1.00f };
        const float SegLeft = LeftX + 84.0f;
        const float SegW3   = (InnerW - 84.0f - 8.0f) / 3.0f;
        for (std::size_t I = 0u; I < 3u; ++I)
        {
            if (ConstructStudioButton(Surface, { SegLeft + static_cast<float>(I) * (SegW3 + 4.0f), Y, SegW3, 21.0f },
                                 ScaleLabels[I], std::abs(Cfg.RenderScale - ScaleVals[I]) < 0.02f, Ptr, 11.0f))
            {
                Cfg.RenderScale = ScaleVals[I];
                ResetSamples    = true;
            }
        }
        Y += 26.0f;

        Surface.Text({ LeftX, Y + 4.0f }, 11.5f, StudioTextMuted, "Target");
        constexpr std::array<const char*, 4> TargetLabels { "64", "256", "1024", "4096" };
        constexpr std::array<uint32_t, 4>    TargetVals   { 64u, 256u, 1024u, 4096u };
        const float SegW4 = (InnerW - 84.0f - 12.0f) / 4.0f;
        for (std::size_t I = 0u; I < 4u; ++I)
        {
            if (ConstructStudioButton(Surface, { SegLeft + static_cast<float>(I) * (SegW4 + 4.0f), Y, SegW4, 21.0f },
                                 TargetLabels[I], Cfg.TargetSamples == TargetVals[I], Ptr, 10.8f))
            {
                Cfg.TargetSamples = TargetVals[I];
                RefreshDisplay    = true;
            }
        }
        Y += 27.0f;

        const float HalfAct = (InnerW - 6.0f) * 0.5f;
        if (ConstructStudioButton(Surface, { LeftX, Y, HalfAct, 23.0f }, IntegrationPaused ? "Resume" : "Pause", IntegrationPaused, Ptr))
        {
            IntegrationPaused = !IntegrationPaused;
        }
        if (ConstructStudioButton(Surface, { LeftX + HalfAct + 6.0f, Y, HalfAct, 23.0f }, "Restart", false, Ptr))
        {
            ResetSamples = true;
        }
        Y += 31.0f;
    }

    // ---- LOOK & LIGHTING SECTION ----
    Y = ConstructSectionHeading(Surface, LeftX, Y, InnerW, "LOOK & LIGHTING", true);
    {
        constexpr std::array<const char*, 5> PaintNames { "Authored Lime", "Racing Red", "Midnight Blue", "Pearl White", "Graphite" };
        constexpr std::array<const char*, 4> FloorNames { "Polished Epoxy", "Matte Concrete", "Black Mirror", "50% Lambert" };
        constexpr std::array<const char*, 3> SkyNames   { "Dark Studio", "Overcast Sky", "Warm Dusk" };

        const float DropLeft = LeftX + 84.0f;
        const float DropW    = InnerW - 84.0f;

        Surface.Text({ LeftX, Y + 4.0f }, 11.5f, StudioTextMuted, "Body Paint");
        PaintDropdownBounds = { DropLeft, Y, DropW, 22.0f };
        if (DropdownSelector::ConstructButton(Surface, PaintDropdownBounds, PaintNames[static_cast<uint32_t>(Cfg.PaintPreset)], OpenDropdownId == 1, Ptr).Pressed)
        {
            OpenDropdownId = (OpenDropdownId == 1) ? -1 : 1;
        }
        Y += 26.0f;

        char ValBuf[32] {};
        std::snprintf(ValBuf, sizeof(ValBuf), "%.2f", Cfg.PaintRoughness);
        float NextRough = Cfg.PaintRoughness;
        if (ConstructLabeledSliderRow(Surface, LeftX, Y, InnerW, "Paint Gloss", ValBuf, 0.02f, 0.55f, Cfg.PaintRoughness, 3, ActiveSliderId, Ptr, NextRough))
        {
            Cfg.PaintRoughness = NextRough;
            RebuildScene       = true;
        }

        Surface.Text({ LeftX, Y + 4.0f }, 11.5f, StudioTextMuted, "Studio Floor");
        FloorDropdownBounds = { DropLeft, Y, DropW, 22.0f };
        if (DropdownSelector::ConstructButton(Surface, FloorDropdownBounds, FloorNames[static_cast<uint32_t>(Cfg.FloorPreset)], OpenDropdownId == 2, Ptr).Pressed)
        {
            OpenDropdownId = (OpenDropdownId == 2) ? -1 : 2;
        }
        Y += 26.0f;

        Surface.Text({ LeftX, Y + 4.0f }, 11.5f, StudioTextMuted, "Sky Dome");
        SkyDropdownBounds = { DropLeft, Y, DropW, 22.0f };
        if (DropdownSelector::ConstructButton(Surface, SkyDropdownBounds, SkyNames[static_cast<uint32_t>(Cfg.EnvironmentPreset)], OpenDropdownId == 3, Ptr).Pressed)
        {
            OpenDropdownId = (OpenDropdownId == 3) ? -1 : 3;
        }
        Y += 26.0f;

        std::snprintf(ValBuf, sizeof(ValBuf), "%.1f", Cfg.KeyIntensity);
        float NextKey = Cfg.KeyIntensity;
        if (ConstructLabeledSliderRow(Surface, LeftX, Y, InnerW, "Key Softbox", ValBuf, 0.0f, 14.0f, Cfg.KeyIntensity, 4, ActiveSliderId, Ptr, NextKey))
        {
            Cfg.KeyIntensity = NextKey;
            RebuildScene     = true;
        }

        std::snprintf(ValBuf, sizeof(ValBuf), "%.1f", Cfg.RimIntensity);
        float NextRim = Cfg.RimIntensity;
        if (ConstructLabeledSliderRow(Surface, LeftX, Y, InnerW, "Rim Strip", ValBuf, 0.0f, 24.0f, Cfg.RimIntensity, 5, ActiveSliderId, Ptr, NextRim))
        {
            Cfg.RimIntensity = NextRim;
            RebuildScene     = true;
        }

        // IES Practical Lamps strip
        Surface.Text({ LeftX, Y + 4.0f }, 11.5f, StudioTextMuted, "IES Lamps");
        constexpr std::array<const char*, 4> LampLabels { "Off", "Beams", "Night", "Deck" };
        const uint32_t CurrentLampTab =
            (Cfg.PracticalMode == GeometricRaster::PracticalLampMode::Disabled) ? 0u :
            ((Cfg.PracticalMode == GeometricRaster::PracticalLampMode::CarParkLamps) ? 3u :
             (Cfg.NightRigActive ? 2u : 1u));
        const float SegW4 = (DropW - 12.0f) / 4.0f;
        for (std::size_t I = 0u; I < 4u; ++I)
        {
            if (ConstructStudioButton(Surface, { DropLeft + static_cast<float>(I) * (SegW4 + 4.0f), Y, SegW4, 21.0f },
                                 LampLabels[I], CurrentLampTab == I, Ptr, 10.8f))
            {
                if (I == 0u)      { Cfg.PracticalMode = GeometricRaster::PracticalLampMode::Disabled;      Cfg.NightRigActive = false; }
                else if (I == 1u) { Cfg.PracticalMode = GeometricRaster::PracticalLampMode::VehicleLamps;  Cfg.NightRigActive = false; }
                else if (I == 2u) { Cfg.PracticalMode = GeometricRaster::PracticalLampMode::VehicleLamps;  Cfg.NightRigActive = true;  }
                else              { Cfg.PracticalMode = GeometricRaster::PracticalLampMode::CarParkLamps;  Cfg.NightRigActive = true;  }
                RebuildScene = true;
            }
        }
        Y += 26.0f;

        std::snprintf(ValBuf, sizeof(ValBuf), "%+.1f EV", Cfg.ExposureStops);
        float NextExp = Cfg.ExposureStops;
        if (ConstructLabeledSliderRow(Surface, LeftX, Y, InnerW, "Exposure", ValBuf, -4.0f, 4.0f, Cfg.ExposureStops, 6, ActiveSliderId, Ptr, NextExp))
        {
            Cfg.ExposureStops = NextExp;
            RefreshDisplay    = true;
        }
        Y += 4.0f;
    }

    // ---- REAL-TIME & FLEET SECTION ----
    Y = ConstructSectionHeading(Surface, LeftX, Y, InnerW, "REAL-TIME & FLEET", true);
    {
        const float SegLeft = LeftX + 84.0f;
        const float SegSpan = InnerW - 84.0f;

        Surface.Text({ LeftX, Y + 4.0f }, 11.5f, StudioTextMuted, "Point Set");
        constexpr std::array<const char*, 3> PatternLabels { "Shared", "Pixel", "BlueNoise" };
        constexpr std::array<GeometricRaster::QuadraturePointPattern, 3> Patterns {
            GeometricRaster::QuadraturePointPattern::Shared,
            GeometricRaster::QuadraturePointPattern::PerPixel,
            GeometricRaster::QuadraturePointPattern::BlueNoise,
        };
        const float W3 = (SegSpan - 8.0f) / 3.0f;
        for (std::size_t I = 0u; I < 3u; ++I)
        {
            if (ConstructStudioButton(Surface, { SegLeft + static_cast<float>(I) * (W3 + 4.0f), Y, W3, 21.0f },
                                 PatternLabels[I], Cfg.PointPattern == Patterns[I], Ptr, 10.6f))
            {
                Cfg.PointPattern = Patterns[I];
                Cfg.Profile      = GeometricRaster::DeterministicRenderProfile::Custom;
                ResetSamples     = true;
            }
        }
        Y += 25.0f;

        const float W2 = (SegSpan - 4.0f) * 0.5f;
        Surface.Text({ LeftX, Y + 4.0f }, 11.5f, StudioTextMuted, "Visibility");
        if (ConstructStudioButton(Surface, { SegLeft, Y, W2, 21.0f }, "Ray Trace", !Cfg.VisibilityRasterActive, Ptr, 10.8f))
        {
            Cfg.VisibilityRasterActive = false;
            ResetSamples               = true;
        }
        if (ConstructStudioButton(Surface, { SegLeft + W2 + 4.0f, Y, W2, 21.0f }, "Raster ID", Cfg.VisibilityRasterActive, Ptr, 10.8f))
        {
            Cfg.VisibilityRasterActive = true;
            ResetSamples               = true;
        }
        Y += 25.0f;

        Surface.Text({ LeftX, Y + 4.0f }, 11.5f, StudioTextMuted, "Motion");
        if (ConstructStudioButton(Surface, { SegLeft, Y, W2, 21.0f }, "Restart", !Cfg.ReprojectionActive, Ptr, 10.8f))
        {
            Cfg.ReprojectionActive = false;
            ResetSamples           = true;
        }
        if (ConstructStudioButton(Surface, { SegLeft + W2 + 4.0f, Y, W2, 21.0f }, "Reproject", Cfg.ReprojectionActive, Ptr, 10.8f))
        {
            Cfg.ReprojectionActive = true;
            RefreshDisplay         = true;
        }
        Y += 25.0f;

        Surface.Text({ LeftX, Y + 4.0f }, 11.5f, StudioTextMuted, "Denoiser");
        if (ConstructStudioButton(Surface, { SegLeft, Y, W2, 21.0f }, "Off", !Cfg.DenoiseActive, Ptr, 10.8f))
        {
            Cfg.DenoiseActive = false;
            RefreshDisplay    = true;
        }
        if (ConstructStudioButton(Surface, { SegLeft + W2 + 4.0f, Y, W2, 21.0f }, "À-Trous", Cfg.DenoiseActive, Ptr, 10.8f))
        {
            Cfg.DenoiseActive = true;
            RefreshDisplay    = true;
        }
        Y += 25.0f;

        Surface.Text({ LeftX, Y + 4.0f }, 11.5f, StudioTextMuted, "Trace Res");
        constexpr std::array<const char*, 3> ResLabels { "Full", "1/2", "1/4" };
        constexpr std::array<uint32_t, 3>    ResScales { 1u, 2u, 4u };
        for (std::size_t I = 0u; I < 3u; ++I)
        {
            if (ConstructStudioButton(Surface, { SegLeft + static_cast<float>(I) * (W3 + 4.0f), Y, W3, 21.0f },
                                 ResLabels[I], Cfg.TraceScale == ResScales[I], Ptr, 10.8f))
            {
                Cfg.TraceScale = ResScales[I];
                Cfg.Profile    = GeometricRaster::DeterministicRenderProfile::Custom;
                ResetSamples   = true;
            }
        }
        Y += 25.0f;

        Surface.Text({ LeftX, Y + 4.0f }, 11.5f, StudioTextMuted, "Fleet Count");
        constexpr std::array<const char*, 7> FleetLabels { "1", "2", "4", "8", "16", "32", "50" };
        constexpr std::array<uint32_t, 7>    FleetCounts { 1u, 2u, 4u, 8u, 16u, 32u, 50u };
        const float WF = (SegSpan - 18.0f) / 7.0f;
        for (std::size_t I = 0u; I < 7u; ++I)
        {
            if (ConstructStudioButton(Surface, { SegLeft + static_cast<float>(I) * (WF + 3.0f), Y, WF, 21.0f },
                                 FleetLabels[I], Cfg.VehicleCount == FleetCounts[I], Ptr, 10.2f))
            {
                Cfg.VehicleCount = FleetCounts[I];
                if (FleetCounts[I] >= 8u && Cfg.CameraPreset != GeometricRaster::StudioCameraPreset::Fleet)
                {
                    Cfg.CameraPreset = GeometricRaster::StudioCameraPreset::Fleet;
                    Cfg.Camera       = GeometricRaster::DeterministicPathTraceSequence::QueryCameraPreset(GeometricRaster::StudioCameraPreset::Fleet);
                }
                RebuildScene = true;
            }
        }
        Y += 29.0f;
    }

    // ---- INTEGRATOR & DIAGNOSTICS SECTION ----
    Y = ConstructSectionHeading(Surface, LeftX, Y, InnerW, "INTEGRATOR & DIAGNOSTICS", true);
    {
        char ValBuf[32] {};
        std::snprintf(ValBuf, sizeof(ValBuf), "%u", Cfg.BounceLimit);
        float NextBounces = static_cast<float>(Cfg.BounceLimit);
        if (ConstructLabeledSliderRow(Surface, LeftX, Y, InnerW, "Max Bounces", ValBuf, 0.0f, 8.0f, NextBounces, 7, ActiveSliderId, Ptr, NextBounces))
        {
            Cfg.BounceLimit = static_cast<uint32_t>(std::lround(NextBounces));
            Cfg.DepthLimit  = std::max(Cfg.DepthLimit, Cfg.BounceLimit + 4u);
            ResetSamples    = true;
        }

        if (ConstructStudioButton(Surface, { LeftX, Y, InnerW, 24.0f }, "Verify Bit-Exact Repeat (8 spp)", false, Ptr, 11.2f))
        {
            TriggerBitExactRepeatVerification(8u, 4u);
        }
        Y += 30.0f;

        const auto& Scene = TracerSequence->QuerySceneSummary();
        char        TriBuf[32] {}, GeoText[96] {}, LampText[96] {};
        FormatIntegerWithCommas(Scene.TriangleCount, TriBuf, sizeof(TriBuf));
        std::snprintf(GeoText, sizeof(GeoText), "%s tris · %u wide4 (%u KB)", TriBuf, Scene.BoundsEntryCount, (Scene.BoundsByteSize + 512u) / 1024u);
        std::snprintf(LampText, sizeof(LampText), "%u area · %u .ies (%u tree)", Scene.AreaLightCount, Scene.PracticalLampCount, Scene.LightTreeClusterCount);

        const auto ConstructReadoutRow = [&](const char* Key, const char* Val, ColorRgba ValColor = StudioTextPrimary) {
            Surface.Text({ LeftX, Y }, 11.0f, StudioTextMuted, Key);
            Surface.TextRightAligned({ LeftX + 74.0f, Y - 2.0f, InnerW - 74.0f, 16.0f }, 11.0f, ValColor, Val);
            Y += 17.0f;
        };

        ConstructReadoutRow("Digest", LastDigestText.c_str(), StudioAccentLime);
        ConstructReadoutRow("Geometry", GeoText);
        ConstructReadoutRow("Luminaires", LampText);
        std::string AdapterStr(TracerSequence->QueryAdapterName());
        if (AdapterStr.size() > 28u)
        {
            AdapterStr.resize(28u);
        }
        ConstructReadoutRow("Adapter", AdapterStr.c_str(), StudioTextMuted);

        if (!RepeatVerdictText.empty())
        {
            Y += 2.0f;
            Surface.Text({ LeftX, Y }, 10.5f, RepeatPassed ? StudioVerifiedGreen : StudioFailRed, RepeatVerdictText.c_str());
        }
    }

    Surface.PopClip();

    if (OpenDropdownId == 1)
    {
        constexpr std::array<const char*, 5> PaintOpts { "Authored Lime", "Racing Red", "Midnight Blue", "Pearl White", "Graphite" };
        uint32_t Chosen = static_cast<uint32_t>(Cfg.PaintPreset);
        if (DropdownSelector::ConstructOptionSheet(Surface, PaintDropdownBounds, PaintOpts.data(), 5u, Chosen, Ptr, Chosen).Clicked)
        {
            Cfg.PaintPreset = static_cast<GeometricRaster::StudioPaintPreset>(Chosen);
            RebuildScene    = true;
            OpenDropdownId  = -1;
        }
    }
    else if (OpenDropdownId == 2)
    {
        constexpr std::array<const char*, 4> FloorOpts { "Polished Epoxy", "Matte Concrete", "Black Mirror", "50% Lambert" };
        uint32_t Chosen = static_cast<uint32_t>(Cfg.FloorPreset);
        if (DropdownSelector::ConstructOptionSheet(Surface, FloorDropdownBounds, FloorOpts.data(), 4u, Chosen, Ptr, Chosen).Clicked)
        {
            Cfg.FloorPreset = static_cast<GeometricRaster::StudioFloorPreset>(Chosen);
            RebuildScene    = true;
            OpenDropdownId  = -1;
        }
    }
    else if (OpenDropdownId == 3)
    {
        constexpr std::array<const char*, 3> SkyOpts { "Dark Studio", "Overcast Sky", "Warm Dusk" };
        uint32_t Chosen = static_cast<uint32_t>(Cfg.EnvironmentPreset);
        if (DropdownSelector::ConstructOptionSheet(Surface, SkyDropdownBounds, SkyOpts.data(), 3u, Chosen, Ptr, Chosen).Clicked)
        {
            Cfg.EnvironmentPreset = static_cast<GeometricRaster::StudioEnvironmentPreset>(Chosen);
            RebuildScene          = true;
            OpenDropdownId        = -1;
        }
    }

    // Apply pending configuration updates cleanly
    if (RebuildScene)
    {
        (void)TracerSequence->ConfigureStudioScene(Cfg);
        LastDigestText = "—";
        SyncViewportTexture();
    }
    else if (CameraChanged)
    {
        TracerSequence->AccessConfiguration() = Cfg;
        TracerSequence->UpdateCamera(Cfg.Camera, true);
        LastDigestText = "—";
        SyncViewportTexture();
    }
    else if (ResetSamples)
    {
        TracerSequence->AccessConfiguration() = Cfg;
        TracerSequence->ResetAccumulation(false);
        LastDigestText = "—";
        SyncViewportTexture();
    }
    else if (RefreshDisplay)
    {
        TracerSequence->AccessConfiguration() = Cfg;
        SyncViewportTexture();
    }
}

} // namespace Frontier
