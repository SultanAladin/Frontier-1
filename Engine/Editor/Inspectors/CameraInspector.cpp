/*============================================================================================================================================
                                                           CAMERAINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Editor / Cine Camera Inspector (4 tabs: Overview, Focus, Body, Render)

#include "Engine/Editor/Inspectors/CameraInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

[[nodiscard]] uint8_t ClampByteCam(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}

[[nodiscard]] float ConstructInteractiveDropdownField(
    PixelSpace& Surface, float X, float Y, float W,
    const char* Label, const char* ValueText,
    const PaneInputReading& Input, bool& OutClicked) noexcept
{
    OutClicked = false;
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X, Y }, 11.2f, InspectorMetrics::MutedInk, Label);
    const Rectangle2D Drop { X, Y + 18.0f, W, 31.0f };
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool Hovered = Drop.Contains(Cursor);
    Surface.FillRectangle(Drop, Hovered ? ColorRgba{ 24u, 25u, 29u, 255u } : ColorRgba{ 12u, 12u, 12u, 255u }, 15.5f);
    Surface.StrokeRectangle(Drop, Hovered ? ColorRgba{ 255u, 255u, 255u, 52u } : ColorRgba{ 255u, 255u, 255u, 18u }, 15.5f, 1.0f);
    WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Drop.X + 14.0f, Drop.Y + 9.0f }, Drop.Width - 36.0f, 11.8f, InspectorMetrics::HeadingInk, ValueText);
    const Point2D Ch { Drop.Right() - 15.0f, Drop.Center().Y };
    Surface.StrokeLine({ Ch.X - 4.0f, Ch.Y - 2.0f }, { Ch.X, Ch.Y + 2.5f }, InspectorMetrics::HeadingInk, 1.4f);
    Surface.StrokeLine({ Ch.X, Ch.Y + 2.5f }, { Ch.X + 4.0f, Ch.Y - 2.0f }, InspectorMetrics::HeadingInk, 1.4f);
    if (Hovered && Input.PrimaryPointer.Pressed)
    {
        OutClicked = true;
    }
    return Drop.Bottom() + 12.0f;
}

} // namespace

void CameraInspector::UpdateCameraBodySurface(uint32_t RigPresetIndex)
{
    const uint64_t Key = 0xCA003303ull ^ (static_cast<uint64_t>(RigPresetIndex) << 8u);
    const uint32_t W = 444u, H = 310u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::CameraBodyPreview, 0u, { static_cast<float>(RigPresetIndex), 0.0f, 0.0f, 0.0f } };
    BodyPreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float V = static_cast<float>(Y) / static_cast<float>(H);
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float U = static_cast<float>(X) / static_cast<float>(W);
                const float D = std::hypot((U - 0.50f) * 1.4f, (V - 0.42f) * 1.6f);
                const float Glow = std::exp(-D * D * 2.2f) * 24.0f;
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteCam(22.0f + Glow);
                Pixels[Off + 1u] = ClampByteCam(24.0f + Glow * 1.05f);
                Pixels[Off + 2u] = ClampByteCam(29.0f + Glow * 1.25f);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float CameraInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, CameraInstanceData& Instance,
    ViewportDisplaySettings* DisplaySettings,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    const auto SyncCameraOptics = [&]() {
        if (DisplaySettings == nullptr) return;
        DisplaySettings->CameraFocalLengthMm  = std::clamp(Instance.FocalLengthMm, 12.0f, 200.0f);
        DisplaySettings->CameraSensorWidthMm  = std::clamp(Instance.SensorWidthMm, 12.0f, 70.0f);
        DisplaySettings->CameraApertureFStop  = std::clamp(Instance.ApertureFStop, 1.2f, 22.0f);
        DisplaySettings->CameraFocusDistanceM = std::clamp(Instance.FocusDistanceMeters, 0.3f, 100.0f);
        DisplaySettings->CameraRollDegrees    = std::clamp(Instance.CameraRollDegrees, -45.0f, 45.0f);
    };
    float CursorY = TopY;
    const float PadX  = 25.0f;
    const float BodyX = LeftX + PadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    // Permanent Editor Camera info banner above the page strip (matches camera__0..3)
    if (Entry.Title == "Editor Camera")
    {
        const Rectangle2D Banner { LeftX, CursorY, InnerWidth, 88.0f };
        Surface.FillRectangle(Banner, ColorRgba{ 24u, 25u, 27u, 255u }, 16.0f);
        Surface.StrokeRectangle(Banner, ColorRgba{ 255u, 255u, 255u, 22u }, 16.0f, 1.0f);
        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, Banner.X + 16.0f, Banner.Y + 14.0f, Banner.Width - 32.0f, 11.4f, 16.8f, InspectorMetrics::HeadingInk,
            "Editor navigation camera \xC2\xB7 always present. Cannot be deleted or hidden. Construct adds separate scene cameras.");
        CursorY = Banner.Bottom() + 18.0f;
    }

    static constexpr std::array<const char*, 4u> Pages = {{ "Overview", "Focus", "Body", "Render" }};
    CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, CursorY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    if (ActivePage == 0u)
    {
        // TAB 1: OVERVIEW — Lens + field of view (h = 516)
        const Rectangle2D LensCard { LeftX, CursorY, InnerWidth, 516.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, LensCard, "Lens + field of view");

        char FlBuf[16]{};
        std::snprintf(FlBuf, sizeof(FlBuf), "%.1f", Instance.FocalLengthMm);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, LensCard.Y + 62.0f, FlBuf, "mm", nullptr, 36.0f);

        // Optical schematic ray-cone diagram matching camera__0
        const float DiagY = LensCard.Y + 158.0f;
        const float SensorX = BodyX + 22.0f;
        const float IrisX   = BodyX + 78.0f;
        const float PlaneX  = BodyX + BodyW - 18.0f;
        const float HalfH   = 28.0f;
        // Sensor box
        Surface.StrokeRectangle({ SensorX - 4.0f, DiagY - 16.0f, 6.0f, 32.0f }, ColorRgba{ 140u, 152u, 178u, 165u }, 2.0f, 1.2f);
        // Aperture blades at iris plane
        Surface.FillRectangle({ IrisX - 2.0f, DiagY - 28.0f, 4.0f, 20.0f }, ColorRgba{ 186u, 174u, 224u, 215u }, 1.0f);
        Surface.FillRectangle({ IrisX - 2.0f, DiagY + 8.0f,  4.0f, 20.0f }, ColorRgba{ 186u, 174u, 224u, 215u }, 1.0f);
        // Ray cones & focus plane
        Surface.StrokeLine({ SensorX + 2.0f, DiagY - 14.0f }, { IrisX, DiagY }, ColorRgba{ 165u, 158u, 198u, 90u }, 1.0f);
        Surface.StrokeLine({ SensorX + 2.0f, DiagY + 14.0f }, { IrisX, DiagY }, ColorRgba{ 165u, 158u, 198u, 90u }, 1.0f);
        Surface.StrokeLine({ IrisX, DiagY }, { PlaneX, DiagY - HalfH }, ColorRgba{ 176u, 168u, 214u, 190u }, 1.3f);
        Surface.StrokeLine({ IrisX, DiagY }, { PlaneX, DiagY + HalfH }, ColorRgba{ 176u, 168u, 214u, 190u }, 1.3f);
        Surface.StrokeLine({ PlaneX, DiagY - HalfH }, { PlaneX, DiagY + HalfH }, ColorRgba{ 212u, 206u, 232u, 190u }, 1.5f);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, DiagY + 52.0f }, 9.6f, InspectorMetrics::DimInk, "Optical schematic \xC2\xB7 not to scale");
        char GateBuf[32]{};
        std::snprintf(GateBuf, sizeof(GateBuf), "%.1f mm / 1.778", Instance.SensorWidthMm);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - 86.0f, DiagY + 52.0f }, 9.6f, InspectorMetrics::DimInk, GateBuf);

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, LensCard.Y + 304.0f, BodyW, 11.4f, 16.5f, InspectorMetrics::MutedInk,
            "Optical schematic \xC2\xB7 not to scale. Sensor height follows the viewport aspect for the live camera.");

        bool LocalOptics = false;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, LensCard.Y + 382.0f, BodyW, 8401u, "Focal Length", "mm", "%.0f", 12.0f, 200.0f, Instance.FocalLengthMm, ActiveDragId, Input, LocalOptics);
        if (LocalOptics) { OutMutated = true; SyncCameraOptics(); }

        const float SensorH = std::clamp(Instance.SensorWidthMm, 12.0f, 70.0f) / 1.777778f;
        const float VFovRad = 2.0f * std::atan(SensorH / (2.0f * std::max(10.0f, Instance.FocalLengthMm)));
        const float VFovDeg = VFovRad * 57.29578f;
        const float FrameH  = 2.0f * std::max(0.3f, Instance.FocusDistanceMeters) * std::tan(VFovRad * 0.5f);
        const float FrameW  = FrameH * 1.777778f;
        char FovBuf[96]{};
        std::snprintf(FovBuf, sizeof(FovBuf), "Vertical FOV %.1f\xC2\xB0 \xC2\xB7 frame at %.1f m: %.2f \xC3\x97 %.2f m", VFovDeg, Instance.FocusDistanceMeters, FrameW, FrameH);
        (void)InspectorPrimitives::ConstructWrappedParagraph(Surface, BodyX, LensCard.Y + 454.0f, BodyW, 11.2f, 16.0f, InspectorMetrics::MutedInk, FovBuf);
        CursorY = LensCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 1u)
    {
        // TAB 2: FOCUS — Aperture study + live focus distance (h = 548)
        const Rectangle2D ApCard { LeftX, CursorY, InnerWidth, 548.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ApCard, "Aperture & focus study");

        char ApBuf[24]{};
        std::snprintf(ApBuf, sizeof(ApBuf), "f/%.1f", Instance.ApertureFStop);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, ApCard.Y + 62.0f, ApBuf, "", nullptr, 36.0f);

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, ApCard.Y + 116.5f, BodyW, 11.4f, 16.5f, InspectorMetrics::MutedInk,
            "Entrance pupil, focus plane, and hyperfocal limits update live with active camera optics.");

        // Polygonal iris diaphragm diagram centered
        const Point2D IrisC { ApCard.Center().X, ApCard.Y + 264.0f };
        const float OuterR = 54.0f;
        const float InnerR = std::clamp(34.0f / std::max(1.2f, Instance.ApertureFStop), 7.0f, 34.0f);
        Surface.FillCircle(IrisC, OuterR, ColorRgba{ 58u, 58u, 64u, 255u });
        Surface.StrokeCircle(IrisC, OuterR + 6.0f, ColorRgba{ 255u, 255u, 255u, 36u }, 1.0f);
        for (int B = 0; B < 8; ++B)
        {
            const float A = static_cast<float>(B) * 0.785398f;
            const Point2D P0 { IrisC.X + std::cos(A) * InnerR, IrisC.Y + std::sin(A) * InnerR };
            const Point2D P1 { IrisC.X + std::cos(A + 0.55f) * OuterR, IrisC.Y + std::sin(A + 0.55f) * OuterR };
            Surface.StrokeLine(P0, P1, ColorRgba{ 118u, 118u, 130u, 165u }, 1.1f);
        }
        Surface.FillCircle(IrisC, InnerR, ColorRgba{ 16u, 16u, 20u, 255u });
        Surface.StrokeCircle(IrisC, InnerR, ColorRgba{ 182u, 182u, 205u, 195u }, 1.2f);
        Surface.FillCircle(IrisC, 1.5f, ColorRgba{ 225u, 225u, 232u, 220u });

        bool LocalFocus = false;
        float FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, ApCard.Y + 370.0f, BodyW, 8410u, "Aperture", "f/", "%.1f", 1.2f, 22.0f, Instance.ApertureFStop, ActiveDragId, Input, LocalFocus);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8411u, "Focus Distance", "m", "%.1f", 0.3f, 100.0f, Instance.FocusDistanceMeters, ActiveDragId, Input, LocalFocus);
        if (LocalFocus) { OutMutated = true; SyncCameraOptics(); }

        const float FocalM = Instance.FocalLengthMm * 0.001f;
        const float HyperM = (FocalM * FocalM) / std::max(1e-6f, Instance.ApertureFStop * 0.00003f);
        char PupBuf[72]{};
        std::snprintf(PupBuf, sizeof(PupBuf), "Pupil %.1f mm \xC2\xB7 Hyperfocal %.1f m", Instance.FocalLengthMm / std::max(1.0f, Instance.ApertureFStop), HyperM);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, FY + 6.0f }, 11.4f, InspectorMetrics::MutedInk, PupBuf);
        CursorY = ApCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        // TAB 3: BODY — Camera body (h = 406: hero card + A Film / B Digital / C Mirrorless) + Sensor & Gate
        UpdateCameraBodySurface(Instance.RigPresetIndex);
        const Rectangle2D BodyCard { LeftX, CursorY, InnerWidth, 406.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, BodyCard, "Camera body");

        const Rectangle2D Hero { BodyX, BodyCard.Y + 62.0f, BodyW, 220.0f };
        BodyPreview.Present(Surface, Hero, 14.0f);

        // Clean stylized 3D camera silhouette in hero box
        const Point2D CamC { Hero.Center().X, Hero.Y + 86.0f };
        Surface.FillRectangle({ CamC.X - 38.0f, CamC.Y - 26.0f, 82.0f, 52.0f }, ColorRgba{ 44u, 48u, 58u, 255u }, 10.0f);
        Surface.StrokeRectangle({ CamC.X - 38.0f, CamC.Y - 26.0f, 82.0f, 52.0f }, ColorRgba{ 138u, 148u, 170u, 110u }, 10.0f, 1.2f);
        Surface.FillCircle({ CamC.X - 18.0f, CamC.Y + 6.0f }, 22.0f, ColorRgba{ 26u, 30u, 38u, 255u });
        Surface.StrokeCircle({ CamC.X - 18.0f, CamC.Y + 6.0f }, 22.0f, ColorRgba{ 186u, 154u, 94u, 185u }, 2.2f);
        Surface.FillCircle({ CamC.X - 18.0f, CamC.Y + 6.0f }, 14.0f, ColorRgba{ 22u, 34u, 54u, 255u });

        static constexpr std::array<const char*, 3u> HeroTitles = {{ "A \xC2\xB7 Film", "B \xC2\xB7 Digital", "C \xC2\xB7 Mirrorless" }};
        static constexpr std::array<const char*, 3u> HeroSubs   = {{ "35 mm cinema \xC2\xB7 prime lens", "Digital cinema \xC2\xB7 matte box", "Stills body \xC2\xB7 zoom lens" }};
        const uint32_t Preset = std::min(Instance.RigPresetIndex, 2u);
        const float HTW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 14.0f, HeroTitles[Preset]);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Hero.Center().X - HTW * 0.5f, Hero.Bottom() - 44.0f }, 14.0f, ColorRgba{ 246u, 246u, 246u, 255u }, HeroTitles[Preset]);
        const float HSW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.4f, HeroSubs[Preset]);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Hero.Center().X - HSW * 0.5f, Hero.Bottom() - 24.0f }, 10.4f, InspectorMetrics::MutedInk, HeroSubs[Preset]);

        // 3 selector tiles: A Film, B Digital, C Mirrorless
        static constexpr std::array<const char*, 3u> TileCodes = {{ "A", "B", "C" }};
        static constexpr std::array<const char*, 3u> TileNames = {{ "Film", "Digital", "Mirrorless" }};
        const float Gap = 8.0f;
        const float TW  = std::floor((BodyW - Gap * 2.0f) / 3.0f);
        for (uint32_t I = 0u; I < 3u; ++I)
        {
            const Rectangle2D T { BodyX + static_cast<float>(I) * (TW + Gap), Hero.Bottom() + 10.0f, TW, 84.0f };
            const bool Sel = (Preset == I);
            Surface.FillRectangle(T, ColorRgba{ 30u, 31u, 34u, 255u }, 12.0f);
            Surface.StrokeRectangle(T, Sel ? ColorRgba{ 216u, 220u, 228u, 215u } : ColorRgba{ 255u, 255u, 255u, 18u }, 12.0f, Sel ? 1.3f : 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { T.X + 7.0f, T.Y + 6.0f }, 9.2f, Sel ? ColorRgba{ 235u, 86u, 78u, 255u } : InspectorMetrics::DimInk, TileCodes[I]);
            Surface.FillRectangle({ T.Center().X - 12.0f, T.Y + 26.0f, 24.0f, 18.0f }, ColorRgba{ 58u, 64u, 78u, 255u }, 4.0f);
            Surface.FillCircle({ T.Center().X - 5.0f, T.Y + 36.0f }, 7.0f, ColorRgba{ 26u, 32u, 44u, 255u });
            const float NW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, TileNames[I]);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { T.Center().X - NW * 0.5f, T.Bottom() - 18.0f }, 10.0f, Sel ? ColorRgba{ 242u, 242u, 242u, 255u } : InspectorMetrics::MutedInk, TileNames[I]);
            if (T.Contains(Cursor) && Input.PrimaryPointer.Pressed) { Instance.RigPresetIndex = I; OutMutated = true; }
        }
        CursorY = BodyCard.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D SensCard { LeftX, CursorY, InnerWidth, 276.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, SensCard, "Sensor & gate");
        float SY = SensCard.Y + 62.0f;
        bool LocalSens = false;
        static constexpr std::array<const char*, 4u> Gates = {{ "Full 36", "S35", "Med 44", "APS-C" }};
        static constexpr std::array<float, 4u> GateWidths  = {{ 36.0f, 24.89f, 43.8f, 23.5f }};
        int32_t PickG = -1;
        SY = InspectorPrimitives::ConstructSegmentTrack(Surface, BodyX, SY, BodyW, "SENSOR GATE", Gates, std::min(Instance.SensorGateIndex, 3u), Input, PickG);
        if (PickG >= 0 && PickG < 4)
        {
            Instance.SensorGateIndex = static_cast<uint32_t>(PickG);
            Instance.SensorWidthMm   = GateWidths[static_cast<std::size_t>(PickG)];
            LocalSens = true;
        }
        SY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, SY, BodyW, 8420u, "Sensor Width", "mm", "%.1f", 12.0f, 70.0f, Instance.SensorWidthMm, ActiveDragId, Input, LocalSens);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, SY, BodyW, 8421u, "Camera Roll", "\xC2\xB0", "%.1f", -45.0f, 45.0f, Instance.CameraRollDegrees, ActiveDragId, Input, LocalSens);
        if (LocalSens) { OutMutated = true; SyncCameraOptics(); }
        CursorY = SensCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 4: RENDER — 7 live clickable render controls directly wired to ViewportDisplaySettings
        const Rectangle2D RenCard { LeftX, CursorY, InnerWidth, 560.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, RenCard, "Render settings");
        float RY = RenCard.Y + 68.0f;

        static constexpr std::array<const char*, 6u> EngineLabels = {{
            "0. MatCap Studio",
            "1. OpenPBR Direct",
            "2. RealTime GI \xC2\xB7 GTX Low-End",
            "3. RealTime GI \xC2\xB7 BVH4 High-End",
            "4. Path Tracer \xC2\xB7 Realtime",
            "5. Path Tracer \xC2\xB7 Final Render"
        }};
        static constexpr std::array<const char*, 5u> TierLabels     = {{ "Low", "Medium", "Standard", "High", "Ultra" }};
        static constexpr std::array<const char*, 5u> ShadowLabels   = {{ "Contact PCSS", "Plane PCSS", "SMRT Raymarch", "PCF Filter", "BVH RayQuery" }};
        static constexpr std::array<const char*, 6u> PenumbraLabels = {{ "Point SDF", "Analytic Cone", "Clipmap", "Voxel + Cone GI", "Hybrid Sobol", "MC Reference 96" }};
        static constexpr std::array<const char*, 2u> ReflLabels     = {{ "WideTree", "Sky Environment" }};
        static constexpr std::array<const char*, 3u> TraceLabels    = {{ "Full", "Half (1/2)", "Quarter (1/4)" }};
        static constexpr std::array<float, 5u>       ScaleSteps     = {{ 50.0f, 75.0f, 100.0f, 125.0f, 150.0f }};

        const uint32_t EngIdx   = DisplaySettings ? std::min<uint32_t>(DisplaySettings->RenderEngineIndex, 5u) : 2u;
        const uint32_t TierIdx  = DisplaySettings ? std::min<uint32_t>(DisplaySettings->QualityTier, 4u) : 1u;
        const uint32_t ShadIdx  = DisplaySettings ? std::min<uint32_t>(DisplaySettings->ShadowAlgorithmIndex, 4u) : 0u;
        const uint32_t PenIdx   = DisplaySettings ? std::min<uint32_t>(DisplaySettings->SdfShadowPenumbraIndex, 5u) : 3u;
        const uint32_t ReflIdx  = DisplaySettings ? std::min<uint32_t>(DisplaySettings->ReflectionSourceIndex, 1u) : 0u;
        const uint32_t TraceIdx = DisplaySettings ? std::min<uint32_t>(DisplaySettings->TraceResolutionIndex, 2u) : 0u;
        const float    ScalePct = DisplaySettings ? DisplaySettings->RenderScalePercent : 100.0f;
        char ScaleBuf[24]{};
        std::snprintf(ScaleBuf, sizeof(ScaleBuf), "%.0f %%", ScalePct);

        bool Clicked = false;
        RY = ConstructInteractiveDropdownField(Surface, BodyX, RY, BodyW, "Render engine", EngineLabels[EngIdx], Input, Clicked);
        if (Clicked && DisplaySettings) { DisplaySettings->RenderEngineIndex = (EngIdx + 1u) % 6u; OutMutated = true; }

        RY = ConstructInteractiveDropdownField(Surface, BodyX, RY, BodyW, "Quality tier", TierLabels[TierIdx], Input, Clicked);
        if (Clicked && DisplaySettings) { DisplaySettings->QualityTier = (TierIdx + 1u) % 5u; OutMutated = true; }

        RY = ConstructInteractiveDropdownField(Surface, BodyX, RY, BodyW, "Shadow algorithm", ShadowLabels[ShadIdx], Input, Clicked);
        if (Clicked && DisplaySettings) { DisplaySettings->ShadowAlgorithmIndex = (ShadIdx + 1u) % 5u; OutMutated = true; }

        RY = ConstructInteractiveDropdownField(Surface, BodyX, RY, BodyW, "SDF penumbra mode", PenumbraLabels[PenIdx], Input, Clicked);
        if (Clicked && DisplaySettings) { DisplaySettings->SdfShadowPenumbraIndex = (PenIdx + 1u) % 6u; OutMutated = true; }

        RY = ConstructInteractiveDropdownField(Surface, BodyX, RY, BodyW, "Reflection source", ReflLabels[ReflIdx], Input, Clicked);
        if (Clicked && DisplaySettings) { DisplaySettings->ReflectionSourceIndex = (ReflIdx + 1u) % 2u; OutMutated = true; }

        RY = ConstructInteractiveDropdownField(Surface, BodyX, RY, BodyW, "Trace resolution", TraceLabels[TraceIdx], Input, Clicked);
        if (Clicked && DisplaySettings) { DisplaySettings->TraceResolutionIndex = (TraceIdx + 1u) % 3u; OutMutated = true; }

        (void)ConstructInteractiveDropdownField(Surface, BodyX, RY, BodyW, "Render scale", ScaleBuf, Input, Clicked);
        if (Clicked && DisplaySettings)
        {
            uint32_t NextIdx = 0u;
            for (uint32_t S = 0u; S < ScaleSteps.size(); ++S)
            {
                if (ScalePct < ScaleSteps[S] - 1.0f) { NextIdx = S; break; }
            }
            DisplaySettings->RenderScalePercent = ScaleSteps[NextIdx];
            OutMutated = true;
        }
        CursorY = RenCard.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
